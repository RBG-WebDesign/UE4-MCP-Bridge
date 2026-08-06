// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Misc/OutputDevice.h"
#include "PIEAgentRuntime.generated.h"

class AActor;
class APawn;
class APlayerController;
class UInputComponent;
class UWorld;
class FJsonObject;
class FJsonValue;

struct FPIEAgentLogEntry
{
    uint64 Sequence = 0;
    double WallSeconds = 0.0;
    FName Category = NAME_None;
    ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
    FString Message;
};

struct FPIEAgentEvent
{
    FString Type;
    FString ActorName;
    FString ActorClass;
    FString OtherActorName;
    FString Category;
    FString Message;
    FVector Location = FVector::ZeroVector;
    float WorldSeconds = 0.0f;
    uint64 Frame = 0;
};

class FPIEAgentLogSink final : public FOutputDevice
{
public:
    explicit FPIEAgentLogSink(class UPIEAgentRuntime* InOwner) : Owner(InOwner) {}
    virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;

private:
    UPIEAgentRuntime* Owner;
};

UCLASS()
class UPIEAgentRuntime : public UObject
{
    GENERATED_BODY()

public:
    void Initialize();
    void Shutdown();

    FString StartMoveTo(const FString& RequestJson, bool bAppendToScript = true);
    FString LookAt(const FString& RequestJson, bool bAppendToScript = true);
    FString StartPress(const FString& RequestJson, bool bAppendToScript = true);
    FString SetKeyState(const FString& KeyName, bool bPressed);
    FString SetAxisState(const FString& AxisKeyName, float Value);
    FString ClearAxisState();
    FString Observe(const FString& RequestJson, bool bAppendToScript = true);
    FString ReadProperty(const FString& RequestJson, bool bAppendToScript = true);
    FString RecordStart(const FString& RequestJson, bool bAppendToScript = true);
    FString RecordStop(bool bAppendToScript = true);
    FString StartExpect(const FString& RequestJson, bool bAppendToScript = true);
    FString StartReplay(const FString& RequestJson);
    FString GetOperationStatus(int32 OperationId) const;

    void CaptureLog(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category);

    UFUNCTION()
    void HandleActorDestroyed(AActor* DestroyedActor);

    UFUNCTION()
    void HandleActorBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);

    UFUNCTION()
    void HandleActorEndOverlap(AActor* OverlappedActor, AActor* OtherActor);

private:
    bool Tick(float DeltaSeconds);
    UWorld* GetPIEWorld() const;
    APlayerController* GetPlayerController() const;
    APawn* GetPawn() const;
    AActor* FindActor(const FString& Query) const;
    AActor* FindActorForRead(const FString& Query, FString& OutError) const;
    bool ParseTarget(const TSharedPtr<FJsonObject>& Request, FVector& OutLocation, FString& OutDescription) const;

    int32 BeginOperation(const FString& Command, double TimeoutSeconds);
    void CompleteOperation(bool bSuccess, const FString& Error, const TSharedPtr<FJsonObject>& Data);
    void TickMove();
    void TickPress();
    void TickExpect();
    void TickReplay();
    bool DispatchAction(const FName& ActionName, EInputEvent InputEvent, int32& OutBindings);

    void AppendScript(const FString& Command, const FString& ParamsJson);
    void BindRecordingActor(AActor* Actor);
    void UnbindRecordingActors();
    void HandleActorSpawned(AActor* Actor);
    void AddEvent(const FPIEAgentEvent& Event);
    bool EventTypeEnabled(const FString& Type) const;
    bool ActorMatchesFilters(const AActor* Actor) const;

    bool EvaluateConditions(TArray<TSharedPtr<FJsonValue>>& OutResults, bool& OutHasNegative, bool& OutNegativeFailed) const;
    double ReadPlayerCounter(const FString& CounterName, bool& bFound) const;
    int32 CountActors(const FString& ClassPattern) const;
    bool LogMatchesSince(uint64 StartSequence, const FString& Pattern, FString& OutMatch) const;

    FString MakeResponse(bool bSuccess, const TSharedPtr<FJsonObject>& Data, const FString& Error = FString()) const;

    FDelegateHandle TickHandle;
    FDelegateHandle ActorSpawnedHandle;
    TUniquePtr<FPIEAgentLogSink> LogSink;

    mutable FCriticalSection LogMutex;
    TArray<FPIEAgentLogEntry> LogRing;
    uint64 NextLogSequence = 1;
    static const int32 MaxLogEntries = 1000;

    bool bRecording = false;
    TSet<FString> RecordEventTypes;
    TArray<FString> RecordClassFilters;
    TArray<FString> RecordLogCategories;
    int32 MaxRecordedEvents = 4096;
    TArray<FPIEAgentEvent> RecordedEvents;
    TArray<TWeakObjectPtr<AActor>> BoundRecordingActors;

    int32 NextOperationId = 1;
    int32 CurrentOperationId = 0;
    FString CurrentCommand;
    FString OperationState = TEXT("idle");
    FString OperationError;
    TSharedPtr<FJsonObject> OperationData;
    double OperationStartedWallSeconds = 0.0;
    double OperationDeadlineWallSeconds = 0.0;

    TWeakObjectPtr<APawn> MovePawn;
    TArray<FVector> MovePathPoints;
    int32 MovePathIndex = 0;
    float MoveAcceptanceRadius = 75.0f;
    FString MoveTargetDescription;

    // Analog axis values re-injected into the PlayerController every tick
    // until cleared; supports scripted stick input at arbitrary angles.
    TMap<FKey, float> ActiveAxes;

    FName PressActionName = NAME_None;
    TArray<FKey> PressKeys;
    double PressReleaseWallSeconds = 0.0;
    int32 PressBindingsTriggered = 0;

    TArray<FString> ExpectConditions;
    uint64 ExpectStartLogSequence = 0;

    bool bReplayActive = false;
    bool bReplayStepRunning = false;
    TArray<TSharedPtr<FJsonObject>> ReplaySteps;
    TArray<TSharedPtr<FJsonValue>> ReplayResults;
    int32 ReplayStepIndex = 0;
    int32 ReplaySeed = 1337;
    int32 ReplayOperationId = 0;
    double ReplayDeadlineWallSeconds = 0.0;

    TArray<TSharedPtr<FJsonObject>> ScriptLog;
};

UPIEAgentRuntime* GetPIEAgentRuntime();
void SetPIEAgentRuntime(UPIEAgentRuntime* Runtime);
