// Copyright 2026 RareBird Games. All Rights Reserved.

#include "PIEAgentRuntime.h"

#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Internationalization/Regex.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    TSharedPtr<FJsonObject> ParseJson(const FString& Text)
    {
        TSharedPtr<FJsonObject> Object;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
    }

    FString SerializeJson(const TSharedPtr<FJsonObject>& Object)
    {
        FString Output;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Object.IsValid() ? Object.ToSharedRef() : MakeShared<FJsonObject>(), Writer);
        return Output;
    }

    TArray<TSharedPtr<FJsonValue>> VectorToJson(const FVector& Value)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        Result.Add(MakeShared<FJsonValueNumber>(Value.X));
        Result.Add(MakeShared<FJsonValueNumber>(Value.Y));
        Result.Add(MakeShared<FJsonValueNumber>(Value.Z));
        return Result;
    }

    bool CompareNumbers(double Left, const FString& Operator, double Right)
    {
        if (Operator == TEXT("==")) return FMath::IsNearlyEqual(Left, Right, 0.0001);
        if (Operator == TEXT("!=")) return !FMath::IsNearlyEqual(Left, Right, 0.0001);
        if (Operator == TEXT("<")) return Left < Right;
        if (Operator == TEXT("<=")) return Left <= Right;
        if (Operator == TEXT(">")) return Left > Right;
        if (Operator == TEXT(">=")) return Left >= Right;
        return false;
    }

    bool SplitComparison(const FString& Text, FString& OutLeft, FString& OutOperator, double& OutRight)
    {
        static const TCHAR* Operators[] = { TEXT("<="), TEXT(">="), TEXT("=="), TEXT("!="), TEXT("<"), TEXT(">") };
        for (const TCHAR* Operator : Operators)
        {
            const int32 Index = Text.Find(Operator);
            if (Index != INDEX_NONE)
            {
                OutLeft = Text.Left(Index).TrimStartAndEnd();
                OutOperator = Operator;
                OutRight = FCString::Atod(*Text.Mid(Index + FCString::Strlen(Operator)).TrimStartAndEnd());
                return true;
            }
        }
        return false;
    }

    FString ExtractLogPattern(const FString& Condition, int32 PrefixLength)
    {
        FString Pattern = Condition.Mid(PrefixLength).TrimStartAndEnd();
        Pattern.RemoveFromEnd(TEXT(")"));
        Pattern = Pattern.TrimStartAndEnd();
        Pattern.TrimQuotesInline();
        return Pattern;
    }
}

void FPIEAgentLogSink::Serialize(const TCHAR* Value, ELogVerbosity::Type Verbosity, const FName& Category)
{
    if (Owner)
    {
        Owner->CaptureLog(Value, Verbosity, Category);
    }
}

void UPIEAgentRuntime::Initialize()
{
    TickHandle = FTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UPIEAgentRuntime::Tick), 0.0f);
    LogSink = MakeUnique<FPIEAgentLogSink>(this);
    if (GLog)
    {
        GLog->AddOutputDevice(LogSink.Get());
    }
}

void UPIEAgentRuntime::Shutdown()
{
    if (TickHandle.IsValid())
    {
        FTicker::GetCoreTicker().RemoveTicker(TickHandle);
        TickHandle.Reset();
    }
    if (LogSink.IsValid() && GLog)
    {
        GLog->RemoveOutputDevice(LogSink.Get());
    }
    LogSink.Reset();
    if (UWorld* World = GetPIEWorld())
    {
        if (ActorSpawnedHandle.IsValid())
        {
            World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
        }
    }
    ActorSpawnedHandle.Reset();
    UnbindRecordingActors();
}

FString UPIEAgentRuntime::MakeResponse(bool bSuccess, const TSharedPtr<FJsonObject>& Data, const FString& Error) const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("success"), bSuccess);
    Root->SetObjectField(TEXT("data"), Data.IsValid() ? Data.ToSharedRef() : MakeShared<FJsonObject>());
    if (Error.IsEmpty())
    {
        Root->SetField(TEXT("error"), MakeShared<FJsonValueNull>());
    }
    else
    {
        Root->SetStringField(TEXT("error"), Error);
    }
    return SerializeJson(Root);
}

UWorld* UPIEAgentRuntime::GetPIEWorld() const
{
#if WITH_EDITOR
    return GEditor ? GEditor->PlayWorld : nullptr;
#else
    return nullptr;
#endif
}

APlayerController* UPIEAgentRuntime::GetPlayerController() const
{
    UWorld* World = GetPIEWorld();
    return World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
}

APawn* UPIEAgentRuntime::GetPawn() const
{
    APlayerController* Controller = GetPlayerController();
    return Controller ? Controller->GetPawn() : nullptr;
}

AActor* UPIEAgentRuntime::FindActor(const FString& Query) const
{
    UWorld* World = GetPIEWorld();
    if (!World || Query.IsEmpty())
    {
        return nullptr;
    }
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetName() == Query)
        {
            return *It;
        }
    }
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetName().Contains(Query) || It->GetClass()->GetName().Contains(Query))
        {
            return *It;
        }
    }
    return nullptr;
}

bool UPIEAgentRuntime::ParseTarget(const TSharedPtr<FJsonObject>& Request, FVector& OutLocation, FString& OutDescription) const
{
    if (!Request.IsValid())
    {
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Location = nullptr;
    if (Request->TryGetArrayField(TEXT("location"), Location) && Location->Num() == 3)
    {
        OutLocation = FVector((*Location)[0]->AsNumber(), (*Location)[1]->AsNumber(), (*Location)[2]->AsNumber());
        OutDescription = OutLocation.ToString();
        return true;
    }
    FString ActorQuery;
    if (Request->TryGetStringField(TEXT("actor"), ActorQuery))
    {
        if (const AActor* Target = FindActor(ActorQuery))
        {
            OutLocation = Target->GetActorLocation();
            OutDescription = Target->GetName();
            return true;
        }
    }
    return false;
}

int32 UPIEAgentRuntime::BeginOperation(const FString& Command, double TimeoutSeconds)
{
    if (OperationState == TEXT("running"))
    {
        return -1;
    }
    CurrentOperationId = NextOperationId++;
    CurrentCommand = Command;
    OperationState = TEXT("running");
    OperationError.Empty();
    OperationData = MakeShared<FJsonObject>();
    OperationStartedWallSeconds = FPlatformTime::Seconds();
    OperationDeadlineWallSeconds = OperationStartedWallSeconds + FMath::Max(TimeoutSeconds, 0.1);
    return CurrentOperationId;
}

void UPIEAgentRuntime::CompleteOperation(bool bSuccess, const FString& Error, const TSharedPtr<FJsonObject>& Data)
{
    OperationState = bSuccess ? TEXT("succeeded") : TEXT("failed");
    OperationError = Error;
    if (Data.IsValid())
    {
        OperationData = Data;
    }
    if (!OperationData.IsValid())
    {
        OperationData = MakeShared<FJsonObject>();
    }
    OperationData->SetNumberField(TEXT("duration_sec"), FPlatformTime::Seconds() - OperationStartedWallSeconds);
    CurrentCommand.Empty();
    MovePawn.Reset();
    MovePathPoints.Reset();
    PressActionName = NAME_None;
    PressKeys.Reset();
    ExpectConditions.Reset();
}

FString UPIEAgentRuntime::GetOperationStatus(int32 OperationId) const
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    if (bReplayActive && OperationId == ReplayOperationId)
    {
        Data->SetNumberField(TEXT("operation_id"), ReplayOperationId);
        Data->SetStringField(TEXT("status"), TEXT("running"));
        Data->SetObjectField(TEXT("result"), OperationData.IsValid() ? OperationData.ToSharedRef() : MakeShared<FJsonObject>());
        return MakeResponse(true, Data);
    }
    if (OperationId != 0 && OperationId != CurrentOperationId)
    {
        Data->SetStringField(TEXT("status"), TEXT("unknown"));
        return MakeResponse(false, Data, FString::Printf(TEXT("Unknown operation id %d (current is %d)"), OperationId, CurrentOperationId));
    }
    Data->SetNumberField(TEXT("operation_id"), CurrentOperationId);
    Data->SetStringField(TEXT("status"), OperationState);
    Data->SetObjectField(TEXT("result"), OperationData.IsValid() ? OperationData.ToSharedRef() : MakeShared<FJsonObject>());
    if (!OperationError.IsEmpty())
    {
        Data->SetStringField(TEXT("error"), OperationError);
    }
    return MakeResponse(true, Data);
}

bool UPIEAgentRuntime::Tick(float DeltaSeconds)
{
    // Analog axes must be re-injected every frame; a single InputAxis call
    // decays back to zero on the next input processing pass.
    if (ActiveAxes.Num() > 0)
    {
        if (APlayerController* AxisController = GetPlayerController())
        {
            for (const TPair<FKey, float>& Axis : ActiveAxes)
            {
                AxisController->InputAxis(Axis.Key, Axis.Value, DeltaSeconds, 1, true);
            }
        }
    }
    if (OperationState == TEXT("running"))
    {
        if (CurrentCommand == TEXT("expect"))
        {
            TickExpect();
        }
        else if (FPlatformTime::Seconds() > OperationDeadlineWallSeconds)
        {
            CompleteOperation(false, FString::Printf(TEXT("%s timed out"), *CurrentCommand), nullptr);
        }
        else if (CurrentCommand == TEXT("move_to"))
        {
            TickMove();
        }
        else if (CurrentCommand == TEXT("press"))
        {
            TickPress();
        }
    }
    if (bReplayActive)
    {
        TickReplay();
    }
    return true;
}

void UPIEAgentRuntime::AppendScript(const FString& Command, const FString& ParamsJson)
{
    TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
    Step->SetStringField(TEXT("command"), Command);
    TSharedPtr<FJsonObject> Parameters = ParseJson(ParamsJson);
    Step->SetObjectField(TEXT("params"), Parameters.IsValid() ? Parameters.ToSharedRef() : MakeShared<FJsonObject>());
    ScriptLog.Add(Step);
}

double UPIEAgentRuntime::ReadPlayerCounter(const FString& CounterName, bool& bFound) const
{
    bFound = false;
    APlayerController* Controller = GetPlayerController();
    if (!Controller)
    {
        return 0.0;
    }
    UActorComponent* Manager = nullptr;
    for (UActorComponent* Component : Controller->GetComponents())
    {
        if (Component && Component->GetClass()->GetName().Contains(TEXT("MCPCollectionManager")))
        {
            Manager = Component;
            break;
        }
    }
    if (!Manager)
    {
        return 0.0;
    }
    static const TMap<FString, FName> Functions = {
        { TEXT("mango"), FName(TEXT("GetMangoCount")) },
        { TEXT("banana"), FName(TEXT("GetBananaCount")) },
        { TEXT("donathan_coin"), FName(TEXT("GetDonathanCoinCount")) },
        { TEXT("money"), FName(TEXT("GetCashCents")) },
    };
    const FName* FunctionName = Functions.Find(CounterName.ToLower());
    if (!FunctionName)
    {
        return 0.0;
    }
    UFunction* Function = Manager->FindFunction(*FunctionName);
    if (!Function || Function->NumParms != 1)
    {
        return 0.0;
    }
    struct FCounterResult { int32 ReturnValue = 0; } Parameters;
    Manager->ProcessEvent(Function, &Parameters);
    bFound = true;
    return CounterName.Equals(TEXT("money"), ESearchCase::IgnoreCase) ? Parameters.ReturnValue / 100.0 : Parameters.ReturnValue;
}

FString UPIEAgentRuntime::Observe(const FString& RequestJson, bool bAppendToScript)
{
    UWorld* World = GetPIEWorld();
    if (!World)
    {
        return MakeResponse(false, nullptr, TEXT("No PIE session is running"));
    }
    const TSharedPtr<FJsonObject> Request = ParseJson(RequestJson);
    double Radius = 1500.0;
    FString ClassFilter;
    int32 LogLines = 20;
    if (Request.IsValid())
    {
        if (Request->HasTypedField<EJson::Number>(TEXT("radius"))) Radius = Request->GetNumberField(TEXT("radius"));
        Request->TryGetStringField(TEXT("class_filter"), ClassFilter);
        if (Request->HasTypedField<EJson::Number>(TEXT("log_lines"))) LogLines = (int32)Request->GetNumberField(TEXT("log_lines"));
    }
    Radius = FMath::Max(Radius, 1.0);
    LogLines = FMath::Clamp(LogLines, 0, MaxLogEntries);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    APawn* Pawn = GetPawn();
    if (Pawn)
    {
        TSharedPtr<FJsonObject> PawnObject = MakeShared<FJsonObject>();
        PawnObject->SetArrayField(TEXT("location"), VectorToJson(Pawn->GetActorLocation()));
        PawnObject->SetArrayField(TEXT("velocity"), VectorToJson(Pawn->GetVelocity()));
        PawnObject->SetNumberField(TEXT("yaw"), Pawn->GetActorRotation().Yaw);
        PawnObject->SetNumberField(TEXT("pitch"), Pawn->GetActorRotation().Pitch);
        Data->SetObjectField(TEXT("pawn"), PawnObject.ToSharedRef());
    }

    const FVector Center = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
    TArray<TSharedPtr<FJsonValue>> Actors;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor == Pawn || Actor->IsPendingKill()) continue;
        if (FVector::Dist(Actor->GetActorLocation(), Center) > Radius) continue;
        if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().Contains(ClassFilter)) continue;
        TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
        ActorObject->SetStringField(TEXT("name"), Actor->GetName());
        ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
        ActorObject->SetArrayField(TEXT("location"), VectorToJson(Actor->GetActorLocation()));
        ActorObject->SetArrayField(TEXT("velocity"), VectorToJson(Actor->GetVelocity()));
        if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
        {
            ActorObject->SetBoolField(TEXT("simulating_physics"), Primitive->IsSimulatingPhysics());
            ActorObject->SetStringField(TEXT("collision_profile"), Primitive->GetCollisionProfileName().ToString());
        }
        Actors.Add(MakeShared<FJsonValueObject>(ActorObject.ToSharedRef()));
    }
    Data->SetArrayField(TEXT("actors"), Actors);
    Data->SetNumberField(TEXT("actor_count"), Actors.Num());

    TArray<TSharedPtr<FJsonValue>> Widgets;
    for (TObjectIterator<UUserWidget> It; It; ++It)
    {
        if (It->GetWorld() == World && It->IsInViewport())
        {
            Widgets.Add(MakeShared<FJsonValueString>(It->GetClass()->GetName()));
        }
    }
    Data->SetArrayField(TEXT("widgets"), Widgets);

    TSharedPtr<FJsonObject> Counters = MakeShared<FJsonObject>();
    const TArray<FString> CounterNames = { TEXT("money"), TEXT("mango"), TEXT("banana"), TEXT("donathan_coin") };
    for (const FString& Name : CounterNames)
    {
        bool bFound = false;
        const double Value = ReadPlayerCounter(Name, bFound);
        if (bFound) Counters->SetNumberField(Name, Value);
    }
    Data->SetObjectField(TEXT("counters"), Counters.ToSharedRef());

    TArray<TSharedPtr<FJsonValue>> Tail;
    {
        FScopeLock Lock(&LogMutex);
        const int32 Start = FMath::Max(0, LogRing.Num() - LogLines);
        for (int32 Index = Start; Index < LogRing.Num(); ++Index)
        {
            Tail.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("[%s] %s"), *LogRing[Index].Category.ToString(), *LogRing[Index].Message)));
        }
    }
    Data->SetArrayField(TEXT("log_tail"), Tail);
    if (bAppendToScript) AppendScript(TEXT("pie_agent_observe"), RequestJson);
    return MakeResponse(true, Data);
}

FString UPIEAgentRuntime::StartMoveTo(const FString& RequestJson, bool bAppendToScript)
{
    const TSharedPtr<FJsonObject> Request = ParseJson(RequestJson);
    APawn* Pawn = GetPawn();
    if (!Pawn) return MakeResponse(false, nullptr, TEXT("No PIE session or no player pawn"));
    FVector Target;
    FString TargetDescription;
    if (!ParseTarget(Request, Target, TargetDescription))
    {
        return MakeResponse(false, nullptr, TEXT("move_to needs 'location':[x,y,z] or 'actor':'<name>'"));
    }
    double Timeout = 20.0;
    if (Request.IsValid() && Request->HasTypedField<EJson::Number>(TEXT("timeout"))) Timeout = Request->GetNumberField(TEXT("timeout"));
    MoveAcceptanceRadius = 75.0f;
    if (Request.IsValid() && Request->HasTypedField<EJson::Number>(TEXT("acceptance_radius"))) MoveAcceptanceRadius = (float)Request->GetNumberField(TEXT("acceptance_radius"));
    const int32 OperationId = BeginOperation(TEXT("move_to"), Timeout);
    if (OperationId < 0)
    {
        return MakeResponse(false, nullptr, FString::Printf(TEXT("operation %d (%s) still running"), CurrentOperationId, *CurrentCommand));
    }
    MovePawn = Pawn;
    MovePathPoints.Reset();
    MovePathIndex = 0;
    MoveTargetDescription = TargetDescription;
    UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetPIEWorld(), Pawn->GetActorLocation(), Target, Pawn);
    if (Path && Path->IsValid() && Path->PathPoints.Num() > 1)
    {
        MovePathPoints = Path->PathPoints;
    }
    else
    {
        MovePathPoints = { Pawn->GetActorLocation(), Target };
    }
    if (bAppendToScript) AppendScript(TEXT("pie_agent_move_to"), RequestJson);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("operation_id"), OperationId);
    Data->SetStringField(TEXT("status"), TEXT("running"));
    Data->SetNumberField(TEXT("path_points"), MovePathPoints.Num());
    return MakeResponse(true, Data);
}

void UPIEAgentRuntime::TickMove()
{
    APawn* Pawn = MovePawn.Get();
    if (!Pawn)
    {
        CompleteOperation(false, TEXT("Pawn lost during move"), nullptr);
        return;
    }
    if (MovePathIndex >= MovePathPoints.Num())
    {
        CompleteOperation(true, FString(), nullptr);
        return;
    }
    const FVector Here = Pawn->GetActorLocation();
    FVector Next = MovePathPoints[MovePathIndex];
    Next.Z = Here.Z;
    const float Distance = FVector::Dist2D(Here, MovePathPoints[MovePathIndex]);
    const bool bLastPoint = MovePathIndex == MovePathPoints.Num() - 1;
    const float Acceptance = bLastPoint ? MoveAcceptanceRadius : 50.0f;
    if (Distance <= Acceptance)
    {
        ++MovePathIndex;
        if (MovePathIndex >= MovePathPoints.Num())
        {
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetBoolField(TEXT("arrived"), true);
            Data->SetArrayField(TEXT("final_location"), VectorToJson(Pawn->GetActorLocation()));
            Data->SetNumberField(TEXT("path_points"), MovePathPoints.Num());
            Data->SetStringField(TEXT("target"), MoveTargetDescription);
            CompleteOperation(true, FString(), Data);
        }
        return;
    }
    Pawn->AddMovementInput((Next - Here).GetSafeNormal2D(), 1.0f);
}

FString UPIEAgentRuntime::LookAt(const FString& RequestJson, bool bAppendToScript)
{
    APlayerController* Controller = GetPlayerController();
    APawn* Pawn = GetPawn();
    if (!Controller || !Pawn) return MakeResponse(false, nullptr, TEXT("No PIE session or no player pawn"));
    const TSharedPtr<FJsonObject> Request = ParseJson(RequestJson);
    FVector Target;
    FString TargetDescription;
    if (!ParseTarget(Request, Target, TargetDescription))
    {
        return MakeResponse(false, nullptr, TEXT("look_at needs 'location':[x,y,z] or 'actor':'<name>'"));
    }
    const FRotator LookRotation = (Target - Pawn->GetActorLocation()).Rotation();
    Controller->SetControlRotation(LookRotation);
    if (bAppendToScript) AppendScript(TEXT("pie_agent_look_at"), RequestJson);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("yaw"), LookRotation.Yaw);
    Data->SetNumberField(TEXT("pitch"), LookRotation.Pitch);
    Data->SetStringField(TEXT("target"), TargetDescription);
    return MakeResponse(true, Data);
}

bool UPIEAgentRuntime::DispatchAction(const FName& ActionName, EInputEvent InputEvent, int32& OutBindings)
{
    OutBindings = 0;
    UWorld* World = GetPIEWorld();
    if (!World) return false;
    TArray<UInputComponent*> Components;
    if (APlayerController* Controller = GetPlayerController())
    {
        if (Controller->InputComponent) Components.Add(Controller->InputComponent);
        if (APawn* Pawn = Controller->GetPawn())
        {
            if (Pawn->InputComponent) Components.AddUnique(Pawn->InputComponent);
        }
    }
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->InputComponent) Components.AddUnique(It->InputComponent);
    }
    for (UInputComponent* Component : Components)
    {
        for (int32 Index = 0; Index < Component->GetNumActionBindings(); ++Index)
        {
            FInputActionBinding& Binding = Component->GetActionBinding(Index);
            if (Binding.GetActionName() == ActionName && Binding.KeyEvent == InputEvent)
            {
                Binding.ActionDelegate.Execute(EKeys::Invalid);
                ++OutBindings;
            }
        }
    }
    return OutBindings > 0;
}

FString UPIEAgentRuntime::SetAxisState(const FString& AxisKeyName, float Value)
{
    const FKey Key(*AxisKeyName);
    if (!Key.IsValid()) return MakeResponse(false, nullptr, FString::Printf(TEXT("invalid axis key '%s'"), *AxisKeyName));
    if (FMath::IsNearlyZero(Value))
    {
        ActiveAxes.Remove(Key);
    }
    else
    {
        ActiveAxes.Add(Key, FMath::Clamp(Value, -1.0f, 1.0f));
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("axis"), AxisKeyName);
    Data->SetNumberField(TEXT("value"), Value);
    Data->SetNumberField(TEXT("active_axes"), ActiveAxes.Num());
    return MakeResponse(true, Data);
}

FString UPIEAgentRuntime::ClearAxisState()
{
    const int32 Cleared = ActiveAxes.Num();
    ActiveAxes.Empty();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("cleared"), Cleared);
    return MakeResponse(true, Data);
}

FString UPIEAgentRuntime::SetKeyState(const FString& KeyName, bool bPressed)
{
    APlayerController* Controller = GetPlayerController();
    if (!Controller) return MakeResponse(false, nullptr, TEXT("No PIE session"));
    const FKey Key(*KeyName);
    if (!Key.IsValid()) return MakeResponse(false, nullptr, FString::Printf(TEXT("invalid key '%s'"), *KeyName));
    Controller->InputKey(Key, bPressed ? IE_Pressed : IE_Released, bPressed ? 1.0f : 0.0f, false);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("key"), KeyName);
    Data->SetBoolField(TEXT("pressed"), bPressed);
    return MakeResponse(true, Data);
}

FString UPIEAgentRuntime::StartPress(const FString& RequestJson, bool bAppendToScript)
{
    APlayerController* Controller = GetPlayerController();
    if (!Controller) return MakeResponse(false, nullptr, TEXT("No PIE session"));
    const TSharedPtr<FJsonObject> Request = ParseJson(RequestJson);
    FString Action;
    FString KeyName;
    const TArray<TSharedPtr<FJsonValue>>* KeyNames = nullptr;
    if (Request.IsValid())
    {
        Request->TryGetStringField(TEXT("action"), Action);
        Request->TryGetStringField(TEXT("key"), KeyName);
        Request->TryGetArrayField(TEXT("keys"), KeyNames);
    }
    if (Action.IsEmpty() && KeyName.IsEmpty() && (!KeyNames || KeyNames->Num() == 0))
    {
        return MakeResponse(false, nullptr, TEXT("press needs 'action', 'key', or a non-empty 'keys' array"));
    }
    double HoldSeconds = 0.1;
    if (Request.IsValid() && Request->HasTypedField<EJson::Number>(TEXT("hold_seconds"))) HoldSeconds = Request->GetNumberField(TEXT("hold_seconds"));
    const int32 OperationId = BeginOperation(TEXT("press"), FMath::Max(5.0, HoldSeconds + 2.0));
    if (OperationId < 0)
    {
        return MakeResponse(false, nullptr, FString::Printf(TEXT("operation %d (%s) still running"), CurrentOperationId, *CurrentCommand));
    }
    PressActionName = Action.IsEmpty() ? NAME_None : FName(*Action);
    PressKeys.Reset();
    if (!KeyName.IsEmpty())
    {
        PressKeys.Add(FKey(*KeyName));
    }
    if (KeyNames)
    {
        for (const TSharedPtr<FJsonValue>& Value : *KeyNames)
        {
            const FKey Key(*Value->AsString());
            if (Key.IsValid())
            {
                PressKeys.AddUnique(Key);
            }
        }
    }
    PressBindingsTriggered = 0;
    PressReleaseWallSeconds = FPlatformTime::Seconds() + FMath::Max(0.0, HoldSeconds);
    if (PressActionName != NAME_None)
    {
        DispatchAction(PressActionName, IE_Pressed, PressBindingsTriggered);
    }
    else
    {
        for (const FKey& Key : PressKeys)
        {
            if (Key.IsValid())
            {
                Controller->InputKey(Key, IE_Pressed, 1.0f, false);
                ++PressBindingsTriggered;
            }
        }
    }
    if (bAppendToScript) AppendScript(TEXT("pie_agent_press"), RequestJson);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("operation_id"), OperationId);
    Data->SetStringField(TEXT("status"), TEXT("running"));
    Data->SetNumberField(TEXT("bindings_pressed"), PressBindingsTriggered);
    return MakeResponse(true, Data);
}

void UPIEAgentRuntime::TickPress()
{
    if (FPlatformTime::Seconds() < PressReleaseWallSeconds) return;
    int32 Released = 0;
    if (PressActionName != NAME_None)
    {
        DispatchAction(PressActionName, IE_Released, Released);
    }
    else if (PressKeys.Num() > 0)
    {
        if (APlayerController* Controller = GetPlayerController())
        {
            for (const FKey& Key : PressKeys)
            {
                if (Key.IsValid())
                {
                    Controller->InputKey(Key, IE_Released, 0.0f, false);
                    ++Released;
                }
            }
        }
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("bindings_pressed"), PressBindingsTriggered);
    Data->SetNumberField(TEXT("bindings_released"), Released);
    const bool bFired = PressBindingsTriggered > 0;
    CompleteOperation(bFired, bFired ? FString() : TEXT("No matching action binding was live (is the player in range of the interactable?)"), Data);
}

void UPIEAgentRuntime::CaptureLog(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
    FScopeLock Lock(&LogMutex);
    FPIEAgentLogEntry Entry;
    Entry.Sequence = NextLogSequence++;
    Entry.WallSeconds = FPlatformTime::Seconds();
    Entry.Category = Category;
    Entry.Verbosity = Verbosity;
    Entry.Message = Message;
    LogRing.Add(MoveTemp(Entry));
    if (LogRing.Num() > MaxLogEntries)
    {
        LogRing.RemoveAt(0, LogRing.Num() - MaxLogEntries, false);
    }
}

bool UPIEAgentRuntime::EventTypeEnabled(const FString& Type) const
{
    return RecordEventTypes.Num() == 0 || RecordEventTypes.Contains(Type);
}

bool UPIEAgentRuntime::ActorMatchesFilters(const AActor* Actor) const
{
    if (!Actor) return false;
    if (RecordClassFilters.Num() == 0) return true;
    const FString ClassName = Actor->GetClass()->GetName();
    for (const FString& Filter : RecordClassFilters)
    {
        if (ClassName.Contains(Filter)) return true;
    }
    return false;
}

void UPIEAgentRuntime::AddEvent(const FPIEAgentEvent& Event)
{
    if (bRecording && RecordedEvents.Num() < MaxRecordedEvents)
    {
        RecordedEvents.Add(Event);
    }
}

void UPIEAgentRuntime::HandleActorSpawned(AActor* Actor)
{
    if (!bRecording || !ActorMatchesFilters(Actor)) return;
    if (EventTypeEnabled(TEXT("spawn")))
    {
        FPIEAgentEvent Event;
        Event.Type = TEXT("spawn");
        Event.ActorName = Actor->GetName();
        Event.ActorClass = Actor->GetClass()->GetName();
        Event.Location = Actor->GetActorLocation();
        Event.WorldSeconds = GetPIEWorld() ? GetPIEWorld()->GetTimeSeconds() : 0.0f;
        Event.Frame = GFrameCounter;
        AddEvent(Event);
    }
    BindRecordingActor(Actor);
}

void UPIEAgentRuntime::BindRecordingActor(AActor* Actor)
{
    if (!Actor) return;
    Actor->OnDestroyed.AddUniqueDynamic(this, &UPIEAgentRuntime::HandleActorDestroyed);
    Actor->OnActorBeginOverlap.AddUniqueDynamic(this, &UPIEAgentRuntime::HandleActorBeginOverlap);
    Actor->OnActorEndOverlap.AddUniqueDynamic(this, &UPIEAgentRuntime::HandleActorEndOverlap);
    BoundRecordingActors.AddUnique(Actor);
}

void UPIEAgentRuntime::UnbindRecordingActors()
{
    for (const TWeakObjectPtr<AActor>& WeakActor : BoundRecordingActors)
    {
        if (AActor* Actor = WeakActor.Get())
        {
            Actor->OnDestroyed.RemoveDynamic(this, &UPIEAgentRuntime::HandleActorDestroyed);
            Actor->OnActorBeginOverlap.RemoveDynamic(this, &UPIEAgentRuntime::HandleActorBeginOverlap);
            Actor->OnActorEndOverlap.RemoveDynamic(this, &UPIEAgentRuntime::HandleActorEndOverlap);
        }
    }
    BoundRecordingActors.Reset();
}

void UPIEAgentRuntime::HandleActorDestroyed(AActor* DestroyedActor)
{
    if (!EventTypeEnabled(TEXT("destroy"))) return;
    FPIEAgentEvent Event;
    Event.Type = TEXT("destroy");
    Event.ActorName = DestroyedActor ? DestroyedActor->GetName() : FString();
    Event.ActorClass = DestroyedActor ? DestroyedActor->GetClass()->GetName() : FString();
    Event.Location = DestroyedActor ? DestroyedActor->GetActorLocation() : FVector::ZeroVector;
    Event.WorldSeconds = GetPIEWorld() ? GetPIEWorld()->GetTimeSeconds() : 0.0f;
    Event.Frame = GFrameCounter;
    AddEvent(Event);
}

void UPIEAgentRuntime::HandleActorBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
    if (!EventTypeEnabled(TEXT("overlap"))) return;
    FPIEAgentEvent Event;
    Event.Type = TEXT("overlap_begin");
    Event.ActorName = OverlappedActor ? OverlappedActor->GetName() : FString();
    Event.ActorClass = OverlappedActor ? OverlappedActor->GetClass()->GetName() : FString();
    Event.OtherActorName = OtherActor ? OtherActor->GetName() : FString();
    Event.Location = OverlappedActor ? OverlappedActor->GetActorLocation() : FVector::ZeroVector;
    Event.WorldSeconds = GetPIEWorld() ? GetPIEWorld()->GetTimeSeconds() : 0.0f;
    Event.Frame = GFrameCounter;
    AddEvent(Event);
}

void UPIEAgentRuntime::HandleActorEndOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
    if (!EventTypeEnabled(TEXT("overlap"))) return;
    FPIEAgentEvent Event;
    Event.Type = TEXT("overlap_end");
    Event.ActorName = OverlappedActor ? OverlappedActor->GetName() : FString();
    Event.ActorClass = OverlappedActor ? OverlappedActor->GetClass()->GetName() : FString();
    Event.OtherActorName = OtherActor ? OtherActor->GetName() : FString();
    Event.Location = OverlappedActor ? OverlappedActor->GetActorLocation() : FVector::ZeroVector;
    Event.WorldSeconds = GetPIEWorld() ? GetPIEWorld()->GetTimeSeconds() : 0.0f;
    Event.Frame = GFrameCounter;
    AddEvent(Event);
}

FString UPIEAgentRuntime::RecordStart(const FString& RequestJson, bool bAppendToScript)
{
    UWorld* World = GetPIEWorld();
    if (!World) return MakeResponse(false, nullptr, TEXT("No PIE session is running"));
    if (bRecording) return MakeResponse(false, nullptr, TEXT("Recording already active; call pie_agent_record_stop first"));
    const TSharedPtr<FJsonObject> Request = ParseJson(RequestJson);
    RecordEventTypes.Reset();
    RecordClassFilters.Reset();
    RecordLogCategories.Reset();
    RecordedEvents.Reset();
    MaxRecordedEvents = 4096;
    if (Request.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Request->TryGetArrayField(TEXT("events"), Array)) for (const TSharedPtr<FJsonValue>& Value : *Array) RecordEventTypes.Add(Value->AsString());
        if (Request->TryGetArrayField(TEXT("class_filters"), Array)) for (const TSharedPtr<FJsonValue>& Value : *Array) RecordClassFilters.Add(Value->AsString());
        if (Request->TryGetArrayField(TEXT("log_categories"), Array)) for (const TSharedPtr<FJsonValue>& Value : *Array) RecordLogCategories.Add(Value->AsString());
        if (Request->HasTypedField<EJson::Number>(TEXT("max_events"))) MaxRecordedEvents = FMath::Max(1, (int32)Request->GetNumberField(TEXT("max_events")));
    }
    bRecording = true;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (ActorMatchesFilters(*It)) BindRecordingActor(*It);
    }
    ActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UPIEAgentRuntime::HandleActorSpawned));
    if (bAppendToScript) AppendScript(TEXT("pie_agent_record_start"), RequestJson);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("recording"), true);
    Data->SetNumberField(TEXT("pre_bound_actors"), BoundRecordingActors.Num());
    return MakeResponse(true, Data);
}

FString UPIEAgentRuntime::RecordStop(bool bAppendToScript)
{
    if (!bRecording) return MakeResponse(false, nullptr, TEXT("No recording is active"));
    bRecording = false;
    if (UWorld* World = GetPIEWorld())
    {
        if (ActorSpawnedHandle.IsValid()) World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
    }
    ActorSpawnedHandle.Reset();
    UnbindRecordingActors();
    TArray<TSharedPtr<FJsonValue>> Events;
    for (const FPIEAgentEvent& Event : RecordedEvents)
    {
        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("type"), Event.Type);
        Object->SetStringField(TEXT("actor"), Event.ActorName);
        Object->SetStringField(TEXT("class"), Event.ActorClass);
        if (!Event.OtherActorName.IsEmpty()) Object->SetStringField(TEXT("other"), Event.OtherActorName);
        Object->SetArrayField(TEXT("location"), VectorToJson(Event.Location));
        Object->SetNumberField(TEXT("world_seconds"), Event.WorldSeconds);
        Object->SetNumberField(TEXT("frame"), (double)Event.Frame);
        Events.Add(MakeShared<FJsonValueObject>(Object.ToSharedRef()));
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("events"), Events);
    Data->SetNumberField(TEXT("event_count"), Events.Num());
    if (bAppendToScript) AppendScript(TEXT("pie_agent_record_stop"), TEXT("{}"));
    return MakeResponse(true, Data);
}

int32 UPIEAgentRuntime::CountActors(const FString& ClassPattern) const
{
    UWorld* World = GetPIEWorld();
    if (!World) return 0;
    int32 Count = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (!It->IsPendingKill() && It->GetClass()->GetName().Contains(ClassPattern)) ++Count;
    }
    return Count;
}

bool UPIEAgentRuntime::LogMatchesSince(uint64 StartSequence, const FString& Pattern, FString& OutMatch) const
{
    const FRegexPattern Regex(Pattern);
    FScopeLock Lock(&LogMutex);
    for (const FPIEAgentLogEntry& Entry : LogRing)
    {
        if (Entry.Sequence < StartSequence) continue;
        FRegexMatcher Matcher(Regex, Entry.Message);
        if (Matcher.FindNext())
        {
            OutMatch = Entry.Message.Left(200);
            return true;
        }
    }
    return false;
}

bool UPIEAgentRuntime::EvaluateConditions(TArray<TSharedPtr<FJsonValue>>& OutResults, bool& OutHasNegative, bool& OutNegativeFailed) const
{
    OutResults.Reset();
    OutHasNegative = false;
    OutNegativeFailed = false;
    bool bAllPositivePassed = true;
    for (const FString& Condition : ExpectConditions)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("condition"), Condition);
        bool bPassed = false;
        FString Observed;
        FString Left;
        FString Operator;
        double Right = 0.0;
        if (Condition.StartsWith(TEXT("no_log_matching(")))
        {
            OutHasNegative = true;
            FString Match;
            const bool bMatched = LogMatchesSince(ExpectStartLogSequence, ExtractLogPattern(Condition, 16), Match);
            bPassed = !bMatched;
            Observed = bMatched ? Match : TEXT("no match");
            OutNegativeFailed |= bMatched;
        }
        else if (Condition.StartsWith(TEXT("log_matching(")))
        {
            FString Match;
            bPassed = LogMatchesSince(ExpectStartLogSequence, ExtractLogPattern(Condition, 13), Match);
            Observed = bPassed ? Match : TEXT("no match yet");
            if (!bPassed) bAllPositivePassed = false;
        }
        else if (SplitComparison(Condition, Left, Operator, Right))
        {
            double Value = 0.0;
            bool bFound = false;
            if (Left.StartsWith(TEXT("actor_count(")))
            {
                FString Pattern = Left.Mid(12).TrimStartAndEnd();
                Pattern.RemoveFromEnd(TEXT(")"));
                Value = CountActors(Pattern);
                bFound = true;
            }
            else if (Left.StartsWith(TEXT("player.")))
            {
                Value = ReadPlayerCounter(Left.Mid(7), bFound);
            }
            bPassed = bFound && CompareNumbers(Value, Operator, Right);
            Observed = bFound ? FString::SanitizeFloat(Value) : TEXT("<unknown lhs>");
            if (!bPassed) bAllPositivePassed = false;
        }
        else
        {
            Observed = TEXT("<unparseable condition>");
            bAllPositivePassed = false;
        }
        Result->SetBoolField(TEXT("passed"), bPassed);
        Result->SetStringField(TEXT("observed"), Observed);
        OutResults.Add(MakeShared<FJsonValueObject>(Result.ToSharedRef()));
    }
    return bAllPositivePassed;
}

FString UPIEAgentRuntime::StartExpect(const FString& RequestJson, bool bAppendToScript)
{
    if (!GetPIEWorld()) return MakeResponse(false, nullptr, TEXT("No PIE session is running"));
    const TSharedPtr<FJsonObject> Request = ParseJson(RequestJson);
    const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
    if (!Request.IsValid() || !Request->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions->Num() == 0)
    {
        return MakeResponse(false, nullptr, TEXT("expect needs 'conditions': [\"...\"]"));
    }
    double WithinSeconds = 5.0;
    if (Request->HasTypedField<EJson::Number>(TEXT("within_seconds"))) WithinSeconds = Request->GetNumberField(TEXT("within_seconds"));
    const int32 OperationId = BeginOperation(TEXT("expect"), WithinSeconds);
    if (OperationId < 0)
    {
        return MakeResponse(false, nullptr, FString::Printf(TEXT("operation %d (%s) still running"), CurrentOperationId, *CurrentCommand));
    }
    ExpectConditions.Reset();
    for (const TSharedPtr<FJsonValue>& Condition : *Conditions) ExpectConditions.Add(Condition->AsString());
    {
        FScopeLock Lock(&LogMutex);
        ExpectStartLogSequence = NextLogSequence;
    }
    if (bAppendToScript) AppendScript(TEXT("pie_agent_expect"), RequestJson);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("operation_id"), OperationId);
    Data->SetStringField(TEXT("status"), TEXT("running"));
    return MakeResponse(true, Data);
}

void UPIEAgentRuntime::TickExpect()
{
    TArray<TSharedPtr<FJsonValue>> Results;
    bool bHasNegative = false;
    bool bNegativeFailed = false;
    const bool bPositivesPassed = EvaluateConditions(Results, bHasNegative, bNegativeFailed);
    auto Finish = [this, &Results](bool bPassed)
    {
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetBoolField(TEXT("passed"), bPassed);
        Data->SetArrayField(TEXT("conditions"), Results);
        CompleteOperation(bPassed, bPassed ? FString() : TEXT("One or more conditions failed"), Data);
    };
    if (bNegativeFailed)
    {
        Finish(false);
        return;
    }
    const bool bDeadline = FPlatformTime::Seconds() >= OperationDeadlineWallSeconds - 0.05;
    if (bPositivesPassed && (!bHasNegative || bDeadline))
    {
        Finish(true);
        return;
    }
    if (bDeadline) Finish(bPositivesPassed);
}

FString UPIEAgentRuntime::StartReplay(const FString& RequestJson)
{
    if (!GetPIEWorld()) return MakeResponse(false, nullptr, TEXT("No PIE session is running; start PIE then replay"));
    const TSharedPtr<FJsonObject> Request = ParseJson(RequestJson);
    bool bExport = false;
    if (Request.IsValid() && Request->TryGetBoolField(TEXT("export"), bExport) && bExport)
    {
        TArray<TSharedPtr<FJsonValue>> Steps;
        for (const TSharedPtr<FJsonObject>& Step : ScriptLog) Steps.Add(MakeShared<FJsonValueObject>(Step.ToSharedRef()));
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetArrayField(TEXT("script"), Steps);
        Data->SetNumberField(TEXT("step_count"), Steps.Num());
        return MakeResponse(true, Data);
    }
    if (bReplayActive || OperationState == TEXT("running")) return MakeResponse(false, nullptr, TEXT("An operation or replay is already running"));
    ReplaySteps.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Script = nullptr;
    if (Request.IsValid() && Request->TryGetArrayField(TEXT("script"), Script))
    {
        for (const TSharedPtr<FJsonValue>& Value : *Script)
        {
            if (Value->Type == EJson::Object) ReplaySteps.Add(Value->AsObject());
        }
    }
    else
    {
        ReplaySteps = ScriptLog;
    }
    if (ReplaySteps.Num() == 0) return MakeResponse(false, nullptr, TEXT("Nothing to replay: no script given and session log is empty"));
    ReplaySeed = 1337;
    double BudgetSeconds = 120.0;
    if (Request.IsValid() && Request->HasTypedField<EJson::Number>(TEXT("seed"))) ReplaySeed = (int32)Request->GetNumberField(TEXT("seed"));
    if (Request.IsValid() && Request->HasTypedField<EJson::Number>(TEXT("budget_seconds"))) BudgetSeconds = Request->GetNumberField(TEXT("budget_seconds"));
    FMath::RandInit(ReplaySeed);
    FMath::SRandInit(ReplaySeed);
    ReplayResults.Reset();
    ReplayStepIndex = 0;
    bReplayStepRunning = false;
    bReplayActive = true;
    ReplayOperationId = NextOperationId++;
    ReplayDeadlineWallSeconds = FPlatformTime::Seconds() + FMath::Max(BudgetSeconds, 0.1);
    OperationState = TEXT("idle");
    CurrentCommand.Empty();
    OperationError.Empty();
    OperationData = MakeShared<FJsonObject>();
    if (!bExport)
    {
        AppendScript(TEXT("pie_agent_replay"), RequestJson);
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("operation_id"), ReplayOperationId);
    Data->SetStringField(TEXT("status"), TEXT("running"));
    Data->SetNumberField(TEXT("steps"), ReplaySteps.Num());
    Data->SetNumberField(TEXT("seed"), ReplaySeed);
    return MakeResponse(true, Data);
}

void UPIEAgentRuntime::TickReplay()
{
    if (FPlatformTime::Seconds() > ReplayDeadlineWallSeconds)
    {
        bReplayActive = false;
        CurrentOperationId = ReplayOperationId;
        OperationState = TEXT("failed");
        OperationError = TEXT("Replay budget expired");
        OperationData = MakeShared<FJsonObject>();
        OperationData->SetArrayField(TEXT("steps"), ReplayResults);
        OperationData->SetNumberField(TEXT("steps_run"), ReplayResults.Num());
        return;
    }
    if (bReplayStepRunning)
    {
        if (OperationState == TEXT("running")) return;
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("command"), ReplaySteps[ReplayStepIndex]->GetStringField(TEXT("command")));
        Result->SetBoolField(TEXT("success"), OperationState == TEXT("succeeded"));
        if (!OperationError.IsEmpty()) Result->SetStringField(TEXT("error"), OperationError);
        ReplayResults.Add(MakeShared<FJsonValueObject>(Result.ToSharedRef()));
        bReplayStepRunning = false;
        ++ReplayStepIndex;
    }
    if (ReplayStepIndex >= ReplaySteps.Num())
    {
        bReplayActive = false;
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetArrayField(TEXT("steps"), ReplayResults);
        Data->SetNumberField(TEXT("steps_run"), ReplayResults.Num());
        bool bAllPassed = true;
        for (const TSharedPtr<FJsonValue>& Value : ReplayResults)
        {
            bool bStepPassed = false;
            Value->AsObject()->TryGetBoolField(TEXT("success"), bStepPassed);
            bAllPassed &= bStepPassed;
        }
        Data->SetBoolField(TEXT("all_passed"), bAllPassed);
        CurrentOperationId = ReplayOperationId;
        OperationState = bAllPassed ? TEXT("succeeded") : TEXT("failed");
        OperationError = bAllPassed ? FString() : TEXT("One or more replay steps failed");
        OperationData = Data;
        return;
    }
    TSharedPtr<FJsonObject> Step = ReplaySteps[ReplayStepIndex];
    FString Command;
    if (!Step->TryGetStringField(TEXT("command"), Command))
    {
        ++ReplayStepIndex;
        return;
    }
    FString ParamsJson = TEXT("{}");
    if (Step->HasTypedField<EJson::Object>(TEXT("params")))
    {
        ParamsJson = SerializeJson(Step->GetObjectField(TEXT("params")));
    }
    FString Response;
    bool bAsync = false;
    if (Command == TEXT("pie_agent_move_to")) { Response = StartMoveTo(ParamsJson, false); bAsync = true; }
    else if (Command == TEXT("pie_agent_press")) { Response = StartPress(ParamsJson, false); bAsync = true; }
    else if (Command == TEXT("pie_agent_expect")) { Response = StartExpect(ParamsJson, false); bAsync = true; }
    else if (Command == TEXT("pie_agent_look_at")) Response = LookAt(ParamsJson, false);
    else if (Command == TEXT("pie_agent_observe")) Response = Observe(ParamsJson, false);
    else if (Command == TEXT("pie_agent_record_start")) Response = RecordStart(ParamsJson, false);
    else if (Command == TEXT("pie_agent_record_stop")) Response = RecordStop(false);
    else if (Command == TEXT("pie_agent_replay"))
    {
        ++ReplayStepIndex;
        return;
    }
    else
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("command"), Command);
        Result->SetBoolField(TEXT("success"), false);
        Result->SetStringField(TEXT("error"), TEXT("Unknown replay command"));
        ReplayResults.Add(MakeShared<FJsonValueObject>(Result.ToSharedRef()));
        ++ReplayStepIndex;
        return;
    }
    const TSharedPtr<FJsonObject> Parsed = ParseJson(Response);
    bool bSuccess = false;
    if (Parsed.IsValid()) Parsed->TryGetBoolField(TEXT("success"), bSuccess);
    if (bAsync && bSuccess)
    {
        bReplayStepRunning = true;
        return;
    }
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("command"), Command);
    Result->SetBoolField(TEXT("success"), bSuccess);
    ReplayResults.Add(MakeShared<FJsonValueObject>(Result.ToSharedRef()));
    ++ReplayStepIndex;
}
