#include "MCPPuerTSBridgeService.h"

#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Json.h"

namespace
{
struct FPhysicsActorSpec
{
    FString Label;
    UStaticMesh* Mesh = nullptr;
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    FVector Scale = FVector::OneVector;
    bool bSimulatePhysics = false;
    float MassKg = 10.0f;
    float LinearDamping = 0.01f;
    float AngularDamping = 0.0f;
};

bool ReadNumber(const FJsonObject& Object, const TCHAR* Name, double& Value, FString& OutError)
{
    if (!Object.HasField(Name)) return true;
    if (!Object.TryGetNumberField(Name, Value) || !FMath::IsFinite(Value))
    {
        OutError = FString::Printf(TEXT("%s must be a finite number."), Name);
        return false;
    }
    return true;
}

bool ReadVector(
    const FJsonObject& Object,
    const TCHAR* Name,
    const FVector& Default,
    FVector& Value,
    FString& OutError)
{
    Value = Default;
    if (!Object.HasField(Name)) return true;
    const TSharedPtr<FJsonObject>* Json = nullptr;
    if (!Object.TryGetObjectField(Name, Json) || Json == nullptr || !Json->IsValid())
    {
        OutError = FString::Printf(TEXT("%s must be an object."), Name);
        return false;
    }
    double X = Value.X;
    double Y = Value.Y;
    double Z = Value.Z;
    if (!ReadNumber(*Json->Get(), TEXT("x"), X, OutError)
        || !ReadNumber(*Json->Get(), TEXT("y"), Y, OutError)
        || !ReadNumber(*Json->Get(), TEXT("z"), Z, OutError)) return false;
    Value = FVector(X, Y, Z);
    return true;
}

bool ReadRotator(const FJsonObject& Object, FRotator& Value, FString& OutError)
{
    Value = FRotator::ZeroRotator;
    if (!Object.HasField(TEXT("rotation"))) return true;
    const TSharedPtr<FJsonObject>* Json = nullptr;
    if (!Object.TryGetObjectField(TEXT("rotation"), Json) || Json == nullptr || !Json->IsValid())
    {
        OutError = TEXT("rotation must be an object.");
        return false;
    }
    double Pitch = 0.0;
    double Yaw = 0.0;
    double Roll = 0.0;
    if (!ReadNumber(*Json->Get(), TEXT("pitch"), Pitch, OutError)
        || !ReadNumber(*Json->Get(), TEXT("yaw"), Yaw, OutError)
        || !ReadNumber(*Json->Get(), TEXT("roll"), Roll, OutError)) return false;
    Value = FRotator(Pitch, Yaw, Roll);
    return true;
}

TSharedPtr<FJsonObject> VectorJson(const FVector& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("x"), Value.X);
    Json->SetNumberField(TEXT("y"), Value.Y);
    Json->SetNumberField(TEXT("z"), Value.Z);
    return Json;
}

TSharedPtr<FJsonObject> RotatorJson(const FRotator& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("pitch"), Value.Pitch);
    Json->SetNumberField(TEXT("yaw"), Value.Yaw);
    Json->SetNumberField(TEXT("roll"), Value.Roll);
    return Json;
}
}

bool UMCPPuerTSBridgeService::BuildPhysicsSceneJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    if (GEditor == nullptr || ActiveTransaction == nullptr)
    {
        OutError = TEXT("Physics scene creation requires the editor and an active transaction.");
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (World == nullptr || World->GetCurrentLevel() == nullptr)
    {
        OutError = TEXT("The editor world is unavailable.");
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
    if (!FJsonSerializer::Deserialize(Reader, Root)
        || !Root.IsValid()
        || !Root->TryGetArrayField(TEXT("actors"), Actors)
        || Actors == nullptr
        || Actors->Num() < 1
        || Actors->Num() > 200)
    {
        OutError = TEXT("actors must contain between 1 and 200 physics actor specs.");
        return false;
    }

    TArray<FPhysicsActorSpec> Specs;
    Specs.Reserve(Actors->Num());
    for (int32 Index = 0; Index < Actors->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!(*Actors)[Index].IsValid() || !(*Actors)[Index]->TryGetObject(Object) || Object == nullptr || !Object->IsValid())
        {
            OutError = FString::Printf(TEXT("actors[%d] must be an object."), Index);
            return false;
        }
        FPhysicsActorSpec Spec;
        FString MeshPath;
        if (!(*Object)->TryGetStringField(TEXT("name"), Spec.Label)
            || Spec.Label.IsEmpty()
            || Spec.Label.Len() > 64
            || !(*Object)->TryGetStringField(TEXT("mesh"), MeshPath)
            || (!MeshPath.StartsWith(TEXT("/Engine/BasicShapes/")) && !MeshPath.StartsWith(TEXT("/Game/"))))
        {
            OutError = FString::Printf(TEXT("actors[%d] requires a short name and an approved mesh path."), Index);
            return false;
        }
        Spec.Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshPath));
        if (Spec.Mesh == nullptr)
        {
            OutError = FString::Printf(TEXT("Static mesh was not found: %s"), *MeshPath);
            return false;
        }
        if (!ReadVector(*Object->Get(), TEXT("location"), FVector::ZeroVector, Spec.Location, OutError)
            || !ReadVector(*Object->Get(), TEXT("scale"), FVector::OneVector, Spec.Scale, OutError)
            || !ReadRotator(*Object->Get(), Spec.Rotation, OutError)) return false;
        if (Spec.Scale.GetMin() <= 0.0f || Spec.Scale.GetMax() > 100.0f || Spec.Location.GetAbsMax() > 1000000.0f)
        {
            OutError = FString::Printf(TEXT("actors[%d] has unsafe scale or location values."), Index);
            return false;
        }
        if ((*Object)->HasField(TEXT("simulate_physics"))
            && !(*Object)->TryGetBoolField(TEXT("simulate_physics"), Spec.bSimulatePhysics))
        {
            OutError = FString::Printf(TEXT("actors[%d].simulate_physics must be boolean."), Index);
            return false;
        }
        double MassKg = Spec.MassKg;
        double LinearDamping = Spec.LinearDamping;
        double AngularDamping = Spec.AngularDamping;
        if (!ReadNumber(*Object->Get(), TEXT("mass_kg"), MassKg, OutError)
            || !ReadNumber(*Object->Get(), TEXT("linear_damping"), LinearDamping, OutError)
            || !ReadNumber(*Object->Get(), TEXT("angular_damping"), AngularDamping, OutError)) return false;
        if (MassKg < 0.1 || MassKg > 100000.0 || LinearDamping < 0.0 || LinearDamping > 100.0 || AngularDamping < 0.0 || AngularDamping > 100.0)
        {
            OutError = FString::Printf(TEXT("actors[%d] has unsafe physics values."), Index);
            return false;
        }
        Spec.MassKg = MassKg;
        Spec.LinearDamping = LinearDamping;
        Spec.AngularDamping = AngularDamping;
        Specs.Add(Spec);
    }

    TArray<AStaticMeshActor*> Spawned;
    TArray<TSharedPtr<FJsonValue>> Results;
    for (const FPhysicsActorSpec& Spec : Specs)
    {
        AStaticMeshActor* Actor = Cast<AStaticMeshActor>(GEditor->AddActor(
            World->GetCurrentLevel(),
            AStaticMeshActor::StaticClass(),
            FTransform(Spec.Rotation, Spec.Location, Spec.Scale),
            true,
            RF_Transactional));
        UStaticMeshComponent* Component = Actor != nullptr ? Actor->GetStaticMeshComponent() : nullptr;
        if (Actor == nullptr || Component == nullptr || !Component->SetStaticMesh(Spec.Mesh))
        {
            for (AStaticMeshActor* Created : Spawned) World->EditorDestroyActor(Created, true);
            OutError = FString::Printf(TEXT("Unreal could not create physics actor: %s"), *Spec.Label);
            return false;
        }
        Actor->SetActorLabel(Spec.Label, true);
        Actor->Tags.AddUnique(TEXT("MCPPhysics"));
        Component->SetMobility(Spec.bSimulatePhysics ? EComponentMobility::Movable : EComponentMobility::Static);
        Component->SetCollisionProfileName(Spec.bSimulatePhysics ? TEXT("PhysicsActor") : TEXT("BlockAll"));
        Component->SetGenerateOverlapEvents(false);
        Component->SetLinearDamping(Spec.LinearDamping);
        Component->SetAngularDamping(Spec.AngularDamping);
        Component->SetSimulatePhysics(Spec.bSimulatePhysics);
        if (Spec.bSimulatePhysics) Component->SetMassOverrideInKg(NAME_None, Spec.MassKg, true);
        Actor->MarkPackageDirty();
        Spawned.Add(Actor);

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Actor->GetName());
        Item->SetStringField(TEXT("label"), Actor->GetActorLabel());
        Item->SetStringField(TEXT("path"), Actor->GetPathName());
        Item->SetBoolField(TEXT("simulate_physics"), Spec.bSimulatePhysics);
        Results.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("count"), Results.Num());
    Result->SetArrayField(TEXT("actors"), Results);
    OutResultJson = SerializeJson(Result);
    return true;
}

bool UMCPPuerTSBridgeService::ObservePhysicsSceneJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    TSet<FString> Requested;
    if (!RequestJson.IsEmpty())
    {
        TSharedPtr<FJsonObject> Root;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            OutError = TEXT("Physics observation request must be valid JSON.");
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
        if (Root->TryGetArrayField(TEXT("actors"), Names) && Names != nullptr)
        {
            if (Names->Num() > 200)
            {
                OutError = TEXT("Physics observation is limited to 200 actors.");
                return false;
            }
            for (const TSharedPtr<FJsonValue>& Name : *Names)
            {
                FString Value;
                if (!Name.IsValid() || !Name->TryGetString(Value) || Value.IsEmpty())
                {
                    OutError = TEXT("actors must be an array of non-empty strings.");
                    return false;
                }
                Requested.Add(Value);
            }
        }
    }

    UWorld* World = GEditor != nullptr && GEditor->PlayWorld != nullptr
        ? GEditor->PlayWorld
        : (GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr);
    if (World == nullptr)
    {
        OutError = TEXT("No editor or PIE world is available.");
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Results;
    for (TActorIterator<AStaticMeshActor> It(World); It && Results.Num() < 200; ++It)
    {
        AStaticMeshActor* Actor = *It;
        const FString Label = Actor->GetActorLabel();
        if (Requested.Num() == 0 && !Actor->Tags.Contains(TEXT("MCPPhysics"))) continue;
        if (Requested.Num() > 0 && !Requested.Contains(Label) && !Requested.Contains(Actor->GetName())) continue;
        UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Actor->GetName());
        Item->SetStringField(TEXT("label"), Label);
        Item->SetObjectField(TEXT("location"), VectorJson(Actor->GetActorLocation()));
        Item->SetObjectField(TEXT("rotation"), RotatorJson(Actor->GetActorRotation()));
        Item->SetObjectField(TEXT("linear_velocity"), VectorJson(Component != nullptr ? Component->GetPhysicsLinearVelocity() : FVector::ZeroVector));
        Item->SetObjectField(TEXT("angular_velocity"), VectorJson(Component != nullptr ? Component->GetPhysicsAngularVelocityInDegrees() : FVector::ZeroVector));
        Item->SetBoolField(TEXT("simulating"), Component != nullptr && Component->IsSimulatingPhysics());
        Results.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("world"), GEditor != nullptr && GEditor->PlayWorld != nullptr ? TEXT("pie") : TEXT("editor"));
    Result->SetNumberField(TEXT("count"), Results.Num());
    Result->SetArrayField(TEXT("actors"), Results);
    OutResultJson = SerializeJson(Result);
    return true;
}
