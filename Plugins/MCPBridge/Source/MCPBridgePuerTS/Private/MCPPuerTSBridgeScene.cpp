// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "MCPBridgeAssetRollback.h"
#include "MCPBridgeSceneSnapshot.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Json.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
    using namespace MCPBridgeScene;

    /** Every op scene_batch understands, in one place, so an unsupported op is
        answered with the vocabulary rather than with a shrug. Two ops, because
        a desired-state description of a level only needs two: an actor is meant
        to be there in a particular state, or it is meant to be gone. Spawn,
        modify, reparent, retag, refolder and set-property are all the first
        one. */
    const TCHAR* const SupportedOps[] = { TEXT("upsert_actor"), TEXT("delete_actor") };

    FString SupportedOpList()
    {
        TArray<FString> Names;
        for (const TCHAR* Name : SupportedOps) { Names.Add(Name); }
        return FString::Join(Names, TEXT(", "));
    }

    bool IsSupportedOp(const FString& Op)
    {
        for (const TCHAR* Name : SupportedOps) { if (Op == Name) { return true; } }
        return false;
    }

    FString ValueToJsonText(const TSharedPtr<FJsonValue>& Value)
    {
        if (!Value.IsValid()) { return FString(); }
        FString Text;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
        FJsonSerializer::Serialize(Value, TEXT(""), Writer);
        return Text;
    }

    /**
     * Does every field the caller SPECIFIED appear, with the same value, in the
     * value actually on the object?
     *
     * Subset rather than equality, for the reason the Blueprint member patch
     * gives: reflection reports every field of a struct and a caller writes the
     * two that matter, so full equality would refuse a correct request.
     *
     * The one difference from that copy is the number branch. A property value
     * here makes a round trip through FJsonObjectConverter and back out of a
     * float, so a caller who writes 3000 for an attenuation radius reads back
     * 3000.0000305; comparing those as text would call an unchanged level a
     * change and write it, dirtying the map on a converged rerun.
     *
     * ponytail: duplicated from MCPPuerTSBridgeBlueprintMember.cpp rather than
     * shared, because the two differ (that one has no tolerance) and hoisting
     * would edit a file this lane does not own. Merge them if a third caller
     * appears.
     */
    bool JsonSubsetMatches(const TSharedPtr<FJsonValue>& Desired, const TSharedPtr<FJsonValue>& Actual)
    {
        if (!Desired.IsValid() || !Actual.IsValid()) { return false; }

        if (Desired->Type == EJson::Number && Actual->Type == EJson::Number)
        {
            const double D = Desired->AsNumber();
            const double A = Actual->AsNumber();
            return FMath::Abs(D - A) <= FMath::Max(1.0e-4, FMath::Abs(D) * 1.0e-5);
        }
        if (Desired->Type != Actual->Type) { return false; }

        const TSharedPtr<FJsonObject>* DesiredObject = nullptr;
        const TSharedPtr<FJsonObject>* ActualObject = nullptr;
        if (Desired->TryGetObject(DesiredObject) && Actual->TryGetObject(ActualObject))
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*DesiredObject)->Values)
            {
                if (!JsonSubsetMatches(Pair.Value, (*ActualObject)->TryGetField(Pair.Key))) { return false; }
            }
            return true;
        }

        const TArray<TSharedPtr<FJsonValue>>* DesiredArray = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* ActualArray = nullptr;
        if (Desired->TryGetArray(DesiredArray) && Actual->TryGetArray(ActualArray))
        {
            if (DesiredArray->Num() != ActualArray->Num()) { return false; }
            for (int32 Index = 0; Index < DesiredArray->Num(); ++Index)
            {
                if (!JsonSubsetMatches((*DesiredArray)[Index], (*ActualArray)[Index])) { return false; }
            }
            return true;
        }
        return ValueToJsonText(Desired) == ValueToJsonText(Actual);
    }

    /** Actors a selector addresses. Case-insensitive on purpose, matching the
        service's own FindLevelActor: a label that differs only in case is a
        typo, and spawning a second actor beside it is the worse answer. More
        than one match is the caller's problem to disambiguate, not this
        function's to guess. */
    TArray<AActor*> MatchActors(UWorld* World, const FString& Kind, const FString& Value)
    {
        TArray<AActor*> Out;
        for (AActor* Actor : SortedLevelActors(World))
        {
            const bool bMatch = Kind == TEXT("name")
                ? Actor->GetName().Equals(Value, ESearchCase::IgnoreCase)
                : Kind == TEXT("path")
                    ? Actor->GetPathName().Equals(Value, ESearchCase::IgnoreCase)
                    : Actor->GetActorLabel().Equals(Value, ESearchCase::IgnoreCase);
            if (bMatch) { Out.Add(Actor); }
        }
        return Out;
    }

    /** One operation after the request has been parsed and its references
        resolved. The request object is kept rather than copied out, so the
        satisfied-predicate reads the same fields the applier writes. */
    struct FResolvedOp
    {
        FString Op;
        int32 Index = 0;
        const TSharedPtr<FJsonObject>* Spec = nullptr;
        FString Label;
        FString ActorKey;
        FString SelectKind;
        FString SelectValue;
        UClass* SpawnClass = nullptr;
        FString DesiredLabel;
        bool bHasDesiredLabel = false;
        bool bSatisfied = false;
    };

    AActor* ResolveOne(UWorld* World, const FResolvedOp& R)
    {
        const TArray<AActor*> Matches = MatchActors(World, R.SelectKind, R.SelectValue);
        return Matches.Num() == 1 ? Matches[0] : nullptr;
    }

    UActorComponent* FindComponentNamed(const AActor* Actor, const FString& Name)
    {
        for (UActorComponent* Component : SortedComponents(Actor))
        {
            if (Component->GetName().Equals(Name, ESearchCase::IgnoreCase)) { return Component; }
        }
        return nullptr;
    }

    /** Does this reflected property already hold the requested value? Read
        through the service's own reader, so the value compared against is the
        one a caller reads back with puerts_read_property. */
    bool PropertyMatches(
        const UMCPPuerTSBridgeService& Service,
        UObject* Object,
        const FString& PropertyName,
        const TSharedPtr<FJsonValue>& Desired)
    {
        FString ValueJson;
        FString ObjectPath;
        FString Error;
        if (!Service.ReadObjectPropertyJson(Object, PropertyName, ValueJson, ObjectPath, Error))
        {
            return false;
        }
        TSharedPtr<FJsonObject> Wrapper;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ValueJson);
        if (!FJsonSerializer::Deserialize(Reader, Wrapper) || !Wrapper.IsValid()) { return false; }
        return JsonSubsetMatches(Desired, Wrapper->TryGetField(TEXT("value")));
    }

    /**
     * Is this operation's requested end state ALREADY true of the level as it
     * stands right now?
     *
     * One definition, asked three times: against the pre-batch level to build
     * the plan, again immediately before each mutation to decide whether it is
     * still needed, and a third time after the batch to verify.
     *
     * The middle call is the one that matters and the one lane A paid for. A
     * batch is ordered, so an operation's precondition is whatever the
     * operations before it left, not what the level looked like when the
     * request was parsed. Deciding a skip against the pre-batch state skips
     * work that has since become necessary. Nothing here reads a cached answer;
     * every call re-resolves the selector and re-reads the actor.
     */
    bool IsOpSatisfied(const UMCPPuerTSBridgeService& Service, UWorld* World, const FResolvedOp& R)
    {
        AActor* Actor = ResolveOne(World, R);

        if (R.Op == TEXT("delete_actor"))
        {
            return Actor == nullptr;
        }

        // upsert_actor. An actor that is not there yet satisfies nothing.
        if (Actor == nullptr) { return false; }
        if (R.SpawnClass != nullptr && Actor->GetClass() != R.SpawnClass) { return false; }
        if (R.bHasDesiredLabel && Actor->GetActorLabel() != R.DesiredLabel) { return false; }

        const TSharedPtr<FJsonObject>& Spec = *R.Spec;

        const TSharedPtr<FJsonObject>* LocationObject = nullptr;
        if (Spec->TryGetObjectField(TEXT("location"), LocationObject))
        {
            const FVector Current = Actor->GetActorLocation();
            const FVector Desired(
                (*LocationObject)->HasField(TEXT("x")) ? (*LocationObject)->GetNumberField(TEXT("x")) : Current.X,
                (*LocationObject)->HasField(TEXT("y")) ? (*LocationObject)->GetNumberField(TEXT("y")) : Current.Y,
                (*LocationObject)->HasField(TEXT("z")) ? (*LocationObject)->GetNumberField(TEXT("z")) : Current.Z);
            if (!Current.Equals(Desired, 0.01f)) { return false; }
        }
        const TSharedPtr<FJsonObject>* RotationObject = nullptr;
        if (Spec->TryGetObjectField(TEXT("rotation"), RotationObject))
        {
            const FRotator Current = Actor->GetActorRotation();
            const FRotator Desired(
                (*RotationObject)->HasField(TEXT("pitch")) ? (*RotationObject)->GetNumberField(TEXT("pitch")) : Current.Pitch,
                (*RotationObject)->HasField(TEXT("yaw")) ? (*RotationObject)->GetNumberField(TEXT("yaw")) : Current.Yaw,
                (*RotationObject)->HasField(TEXT("roll")) ? (*RotationObject)->GetNumberField(TEXT("roll")) : Current.Roll);
            // FRotator::Equals normalizes the difference, so a caller asking for
            // yaw 270 is satisfied by the -90 reflection reports.
            if (!Current.Equals(Desired, 0.01f)) { return false; }
        }
        const TSharedPtr<FJsonObject>* ScaleObject = nullptr;
        if (Spec->TryGetObjectField(TEXT("scale"), ScaleObject))
        {
            const FVector Current = Actor->GetActorScale3D();
            const FVector Desired(
                (*ScaleObject)->HasField(TEXT("x")) ? (*ScaleObject)->GetNumberField(TEXT("x")) : Current.X,
                (*ScaleObject)->HasField(TEXT("y")) ? (*ScaleObject)->GetNumberField(TEXT("y")) : Current.Y,
                (*ScaleObject)->HasField(TEXT("z")) ? (*ScaleObject)->GetNumberField(TEXT("z")) : Current.Z);
            if (!Current.Equals(Desired, 0.001f)) { return false; }
        }

        FString Folder;
        if (Spec->TryGetStringField(TEXT("folder"), Folder)
            && Actor->GetFolderPath().ToString() != Folder)
        {
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
        if (Spec->TryGetArrayField(TEXT("tags"), Tags) && Tags != nullptr)
        {
            if (Actor->Tags.Num() != Tags->Num()) { return false; }
            for (int32 Index = 0; Index < Tags->Num(); ++Index)
            {
                if (Actor->Tags[Index].ToString() != (*Tags)[Index]->AsString()) { return false; }
            }
        }

        if (Spec->HasField(TEXT("attach_to")))
        {
            const AActor* Parent = Actor->GetAttachParentActor();
            FString DesiredParent;
            // A null or empty attach_to is the request to be attached to
            // nothing, which is a state to converge on like any other.
            Spec->TryGetStringField(TEXT("attach_to"), DesiredParent);
            if (DesiredParent.IsEmpty())
            {
                if (Parent != nullptr) { return false; }
            }
            else
            {
                if (Parent == nullptr) { return false; }
                if (!Parent->GetName().Equals(DesiredParent, ESearchCase::IgnoreCase)
                    && !Parent->GetActorLabel().Equals(DesiredParent, ESearchCase::IgnoreCase))
                {
                    return false;
                }
            }
        }

        const TSharedPtr<FJsonObject>* Properties = nullptr;
        if (Spec->TryGetObjectField(TEXT("properties"), Properties))
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
            {
                if (!PropertyMatches(Service, Actor, Pair.Key, Pair.Value)) { return false; }
            }
        }

        const TSharedPtr<FJsonObject>* Components = nullptr;
        if (Spec->TryGetObjectField(TEXT("components"), Components))
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Components)->Values)
            {
                UActorComponent* Component = FindComponentNamed(Actor, Entry.Key);
                if (Component == nullptr) { return false; }
                const TSharedPtr<FJsonObject>* ComponentProperties = nullptr;
                if (!Entry.Value->TryGetObject(ComponentProperties)) { return false; }
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ComponentProperties)->Values)
                {
                    if (!PropertyMatches(Service, Component, Pair.Key, Pair.Value)) { return false; }
                }
            }
        }
        return true;
    }

    /** Is Candidate already somewhere below Root in the attachment tree?
        Attaching Root to one of its own descendants makes a cycle that Unreal
        does not refuse for us. */
    bool IsDescendantOf(const AActor* Candidate, const AActor* Root)
    {
        for (const AActor* Walk = Candidate; Walk != nullptr; Walk = Walk->GetAttachParentActor())
        {
            if (Walk == Root) { return true; }
        }
        return false;
    }

    /** Actor classes scene_batch may spawn. The same limit spawn_actor carries,
        plus NavigationSystem, which is where ANavMeshBoundsVolume lives and
        which a level cannot be authored without. */
    bool IsSpawnableClassPath(const FString& ClassPath)
    {
        return ClassPath.StartsWith(TEXT("/Game/"))
            || ClassPath.StartsWith(TEXT("/Script/Engine."))
            || ClassPath.StartsWith(TEXT("/Script/NavigationSystem."))
            || ClassPath == TEXT("/Script/CinematicCamera.CineCameraActor");
    }
}

bool UMCPPuerTSBridgeService::InspectSceneJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Scene inspect request must be a JSON object.");
        return false;
    }

    UWorld* World = MCPBridgeScene::EditorWorld();
    if (World == nullptr)
    {
        OutError = TEXT("No editor world is loaded, so there is no level to inspect.");
        return false;
    }
    const FString LevelPath = MCPBridgeScene::LevelPackagePath(World);

    // An optional assertion, not a selector. A harness that names the level it
    // believes is loaded must not silently inspect a different one, because an
    // empty success against the wrong map is indistinguishable from an empty
    // map.
    FString ExpectedLevel;
    if (Request->TryGetStringField(TEXT("level_path"), ExpectedLevel) && !ExpectedLevel.IsEmpty()
        && ExpectedLevel != LevelPath)
    {
        OutError = FString::Printf(
            TEXT("The editor has %s loaded, not %s. scene_inspect reads the level the editor "
                 "actually has open and never loads one."), *LevelPath, *ExpectedLevel);
        return false;
    }

    UPackage* Package = World->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    const bool bIncludeComponents = !Request->HasTypedField<EJson::Boolean>(TEXT("include_components"))
        || Request->GetBoolField(TEXT("include_components"));

    TArray<FString> RequestedProperties;
    const TArray<TSharedPtr<FJsonValue>>* PropertyNames = nullptr;
    if (Request->TryGetArrayField(TEXT("include_properties"), PropertyNames) && PropertyNames != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *PropertyNames)
        {
            FString Name;
            if (Value->TryGetString(Name) && !Name.IsEmpty()) { RequestedProperties.Add(Name); }
        }
    }

    // Selectors narrow what is REPORTED. They never narrow what is hashed; see
    // MCPBridgeScene::StructureHash.
    TSet<FString> Filter;
    const TArray<TSharedPtr<FJsonValue>>* Selectors = nullptr;
    const bool bFiltered = Request->TryGetArrayField(TEXT("actors"), Selectors) && Selectors != nullptr;
    if (bFiltered)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Selectors)
        {
            FString Name;
            if (Value->TryGetString(Name)) { Filter.Add(Name.ToLower()); }
        }
    }

    const TArray<AActor*> Actors = MCPBridgeScene::SortedLevelActors(World);
    TArray<TSharedPtr<FJsonValue>> Reported;
    for (AActor* Actor : Actors)
    {
        if (bFiltered
            && !Filter.Contains(Actor->GetName().ToLower())
            && !Filter.Contains(Actor->GetActorLabel().ToLower())
            && !Filter.Contains(Actor->GetPathName().ToLower()))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Item = MCPBridgeScene::ActorJson(Actor);
        if (!bIncludeComponents) { Item->RemoveField(TEXT("components")); }

        if (RequestedProperties.Num() > 0)
        {
            TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
            for (const FString& Name : RequestedProperties)
            {
                FString ValueJson;
                FString ObjectPath;
                FString ReadError;
                if (!ReadObjectPropertyJson(Actor, Name, ValueJson, ObjectPath, ReadError)) { continue; }
                TSharedPtr<FJsonObject> Wrapper;
                TSharedRef<TJsonReader<>> ValueReader = TJsonReaderFactory<>::Create(ValueJson);
                if (FJsonSerializer::Deserialize(ValueReader, Wrapper) && Wrapper.IsValid())
                {
                    // Absent rather than null when the actor has no such
                    // property: a null would say the property exists and holds
                    // nothing, which is a different fact.
                    Values->SetField(Name, Wrapper->TryGetField(TEXT("value")));
                }
            }
            Item->SetObjectField(TEXT("properties"), Values);
        }
        Reported.Add(MakeShared<FJsonValueObject>(Item));
    }

    TArray<TSharedPtr<FJsonValue>> Starts;
    for (const TPair<FString, FVector>& Start : MCPBridgeScene::PlayerStarts(World))
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), Start.Key);
        Item->SetObjectField(TEXT("location"), MCPBridgeScene::VectorJson(Start.Value));
        Starts.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("level_path"), LevelPath);
    Result->SetStringField(TEXT("level_name"), FPackageName::GetShortName(LevelPath));
    Result->SetNumberField(TEXT("actor_count"), Actors.Num());
    Result->SetNumberField(TEXT("reported_actor_count"), Reported.Num());
    Result->SetBoolField(TEXT("filtered"), bFiltered);
    Result->SetArrayField(TEXT("actors"), Reported);
    Result->SetArrayField(TEXT("player_starts"), Starts);
    Result->SetStringField(TEXT("structure_hash_sha1"), MCPBridgeScene::StructureHash(World));
    Result->SetStringField(TEXT("structure_hash_basis"), MCPBridgeScene::StructureHashBasis());
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), Package != nullptr && Package->IsDirty());
    Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
    FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
    return true;
}

bool UMCPPuerTSBridgeService::ApplySceneBatchJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Scene batch spec must be a JSON object.");
        return false;
    }

    UWorld* World = MCPBridgeScene::EditorWorld();
    if (World == nullptr || World->GetCurrentLevel() == nullptr)
    {
        OutError = TEXT("No editor world is loaded, so there is no level to change.");
        return false;
    }
    const FString LevelPath = MCPBridgeScene::LevelPackagePath(World);

    FString ExpectedLevel;
    if (Spec->TryGetStringField(TEXT("level_path"), ExpectedLevel) && !ExpectedLevel.IsEmpty()
        && ExpectedLevel != LevelPath)
    {
        OutError = FString::Printf(
            TEXT("The editor has %s loaded, not %s. scene_batch changes the level the editor "
                 "actually has open and never loads one."), *LevelPath, *ExpectedLevel);
        return false;
    }

    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    const bool bVerify = !Spec->HasTypedField<EJson::Boolean>(TEXT("verify"))
        || Spec->GetBoolField(TEXT("verify"));

    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Spec->TryGetArrayField(TEXT("operations"), Operations) || Operations == nullptr)
    {
        OutError = TEXT("operations must be an array.");
        return false;
    }
    if (Operations->Num() == 0)
    {
        OutError = TEXT("operations must be a non-empty array; a batch with no operations is not a request.");
        return false;
    }

    const FString PreHash = MCPBridgeScene::StructureHash(World);

    // --- Pass 1: resolve, validate and REFUSE, mutating nothing.
    //
    // The satisfied/not-satisfied split this pass produces is a PLAN, not a
    // decision. Whether an operation runs is settled at apply time against the
    // state the operations before it left; see IsOpSatisfied.
    TArray<FResolvedOp> Resolved;
    TArray<FString> Refusals;
    TSet<FString> SeenActorKeys;
    TSet<FString> LabelsCreatedInBatch;

    for (int32 Index = 0; Index < Operations->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* OpObject = nullptr;
        if (!(*Operations)[Index]->TryGetObject(OpObject))
        {
            Refusals.Add(FString::Printf(TEXT("operation %d is not an object."), Index));
            continue;
        }

        FResolvedOp R;
        R.Index = Index;
        R.Spec = OpObject;
        (*OpObject)->TryGetStringField(TEXT("op"), R.Op);
        if (!IsSupportedOp(R.Op))
        {
            Refusals.Add(FString::Printf(
                TEXT("operation %d names an unsupported op '%s'. Supported: %s."),
                Index, *R.Op, *SupportedOpList()));
            continue;
        }

        // --- The selector. An explicit select addresses an actor that must
        //     exist; a bare label is the upsert identity and may be absent.
        const TSharedPtr<FJsonObject>* Select = nullptr;
        if ((*OpObject)->TryGetObjectField(TEXT("select"), Select))
        {
            for (const TCHAR* Kind : { TEXT("name"), TEXT("path"), TEXT("label") })
            {
                FString Value;
                if ((*Select)->TryGetStringField(Kind, Value) && !Value.IsEmpty())
                {
                    if (!R.SelectKind.IsEmpty())
                    {
                        Refusals.Add(FString::Printf(
                            TEXT("operation %d gives more than one selector key (%s and %s). One "
                                 "actor is addressed one way, or the two can disagree."),
                            Index, *R.SelectKind, Kind));
                        R.SelectKind.Reset();
                        break;
                    }
                    R.SelectKind = Kind;
                    R.SelectValue = Value;
                }
            }
        }
        if ((*OpObject)->TryGetStringField(TEXT("label"), R.DesiredLabel) && !R.DesiredLabel.IsEmpty())
        {
            R.bHasDesiredLabel = true;
        }
        const bool bSelectorGiven = !R.SelectKind.IsEmpty();
        if (!bSelectorGiven)
        {
            if (R.Op == TEXT("delete_actor"))
            {
                Refusals.Add(FString::Printf(
                    TEXT("operation %d (delete_actor) requires a select object: {\"name\":...}, "
                         "{\"path\":...} or {\"label\":...}."), Index));
                continue;
            }
            if (!R.bHasDesiredLabel)
            {
                Refusals.Add(FString::Printf(
                    TEXT("operation %d (upsert_actor) requires either a select object or a label. "
                         "Without one there is no actor identity to converge on."), Index));
                continue;
            }
            R.SelectKind = TEXT("label");
            R.SelectValue = R.DesiredLabel;
        }
        R.Label = FString::Printf(TEXT("%s %s=%s"), *R.Op, *R.SelectKind, *R.SelectValue);
        R.ActorKey = FString::Printf(TEXT("%s:%s"), *R.SelectKind, *R.SelectValue.ToLower());

        const TArray<AActor*> Matches = MatchActors(World, R.SelectKind, R.SelectValue);
        if (Matches.Num() > 1)
        {
            // Name the matches, not just the count. A refusal that says
            // "ambiguous" and stops leaves the caller with no way to fix it.
            TArray<FString> Named;
            for (const AActor* Actor : Matches)
            {
                Named.Add(FString::Printf(TEXT("%s (label '%s', class %s)"),
                    *Actor->GetName(), *Actor->GetActorLabel(), *Actor->GetClass()->GetName()));
            }
            Refusals.Add(FString::Printf(
                TEXT("operation %d (%s): the selector matches %d actors: %s. Address one of them "
                     "by {\"name\":...}, which is unique within a level, rather than by label."),
                Index, *R.Label, Matches.Num(), *FString::Join(Named, TEXT("; "))));
            continue;
        }
        AActor* Existing = Matches.Num() == 1 ? Matches[0] : nullptr;
        if (Existing == nullptr && bSelectorGiven && R.Op == TEXT("upsert_actor"))
        {
            Refusals.Add(FString::Printf(
                TEXT("operation %d (%s): no actor matches that selector. A select addresses an "
                     "actor that exists; to spawn one, give a label instead and omit select."),
                Index, *R.Label));
            continue;
        }

        if (R.Op == TEXT("upsert_actor"))
        {
            FString ClassPath;
            if ((*OpObject)->TryGetStringField(TEXT("class"), ClassPath) && !ClassPath.IsEmpty())
            {
                if (!IsSpawnableClassPath(ClassPath))
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (%s): actor classes are limited to /Game/, "
                             "/Script/Engine., /Script/NavigationSystem. and "
                             "/Script/CinematicCamera.CineCameraActor; '%s' is none of those."),
                        Index, *R.Label, *ClassPath));
                    continue;
                }
                R.SpawnClass = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath);
                if (R.SpawnClass == nullptr)
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (%s): '%s' did not load as an Actor class."),
                        Index, *R.Label, *ClassPath));
                    continue;
                }
            }
            else if (Existing == nullptr)
            {
                Refusals.Add(FString::Printf(
                    TEXT("operation %d (%s): no actor of that identity exists, so this operation "
                         "must spawn one, and a spawn requires 'class'."), Index, *R.Label));
                continue;
            }

            if (Existing != nullptr && R.SpawnClass != nullptr && Existing->GetClass() != R.SpawnClass)
            {
                Refusals.Add(FString::Printf(
                    TEXT("operation %d (%s): that actor is a %s, not the requested %s. There is no "
                         "change-class primitive; delete_actor then upsert_actor if the replacement "
                         "is intended."),
                    Index, *R.Label, *Existing->GetClass()->GetPathName(), *R.SpawnClass->GetPathName()));
                continue;
            }

            // A label that already belongs to a different actor would be
            // silently accepted by SetActorLabel and then never converge,
            // because the satisfied-predicate compares the label it asked for.
            if (R.bHasDesiredLabel)
            {
                bool bLabelTakenByAnother = false;
                for (const AActor* Actor : MatchActors(World, TEXT("label"), R.DesiredLabel))
                {
                    if (Actor != Existing) { bLabelTakenByAnother = true; }
                }
                if (bLabelTakenByAnother || LabelsCreatedInBatch.Contains(R.DesiredLabel.ToLower()))
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (%s): label '%s' already belongs to a different actor. "
                             "Labels are the identity this command upserts on, so two actors may "
                             "not share one."), Index, *R.Label, *R.DesiredLabel));
                    continue;
                }
                LabelsCreatedInBatch.Add(R.DesiredLabel.ToLower());
            }

            // Property writes are gated by the same allowlist puerts_set_property
            // uses. Checked here against the actor, or against the spawn class
            // default object when there is no actor yet, so an unapproved
            // property is a refusal before anything is spawned.
            UObject* PropertyProbe = Existing != nullptr
                ? static_cast<UObject*>(Existing)
                : (R.SpawnClass != nullptr ? R.SpawnClass->GetDefaultObject() : nullptr);
            const TSharedPtr<FJsonObject>* Properties = nullptr;
            if (PropertyProbe != nullptr && (*OpObject)->TryGetObjectField(TEXT("properties"), Properties))
            {
                TArray<FString> Unapproved;
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
                {
                    if (!IsWritablePropertyAllowed(PropertyProbe, Pair.Key)) { Unapproved.Add(Pair.Key); }
                }
                if (Unapproved.Num() > 0)
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (%s): %s is not on the writable-property allowlist. "
                             "Widening it is a deliberate edit to AllowedWritableProperties in "
                             "[MCPPuerTSBridge], not something a request may do."),
                        Index, *R.Label, *FString::Join(Unapproved, TEXT(", "))));
                    continue;
                }
            }

            const TSharedPtr<FJsonObject>* Components = nullptr;
            if ((*OpObject)->TryGetObjectField(TEXT("components"), Components))
            {
                bool bComponentRefused = false;
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Components)->Values)
                {
                    const TSharedPtr<FJsonObject>* ComponentProperties = nullptr;
                    if (!Entry.Value->TryGetObject(ComponentProperties))
                    {
                        Refusals.Add(FString::Printf(
                            TEXT("operation %d (%s): components.%s must be an object of property "
                                 "names to values."), Index, *R.Label, *Entry.Key));
                        bComponentRefused = true;
                        continue;
                    }
                    // Only checkable when the actor is already there. A
                    // component of an actor this batch is about to spawn does
                    // not exist yet, so its allowlist check happens at apply
                    // time and a refusal there rolls the whole batch back.
                    if (Existing == nullptr) { continue; }
                    UActorComponent* Component = FindComponentNamed(Existing, Entry.Key);
                    if (Component == nullptr)
                    {
                        TArray<FString> Available;
                        for (const UActorComponent* Candidate : SortedComponents(Existing))
                        {
                            Available.Add(Candidate->GetName());
                        }
                        Refusals.Add(FString::Printf(
                            TEXT("operation %d (%s): '%s' has no component named '%s'. It has: %s. "
                                 "This command sets properties on components an actor already has "
                                 "and does not add new ones."),
                            Index, *R.Label, *Existing->GetName(), *Entry.Key,
                            *FString::Join(Available, TEXT(", "))));
                        bComponentRefused = true;
                        continue;
                    }
                    TArray<FString> Unapproved;
                    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ComponentProperties)->Values)
                    {
                        if (!IsWritablePropertyAllowed(Component, Pair.Key)) { Unapproved.Add(Pair.Key); }
                    }
                    if (Unapproved.Num() > 0)
                    {
                        Refusals.Add(FString::Printf(
                            TEXT("operation %d (%s): component '%s' property %s is not on the "
                                 "writable-property allowlist."),
                            Index, *R.Label, *Entry.Key, *FString::Join(Unapproved, TEXT(", "))));
                        bComponentRefused = true;
                    }
                }
                if (bComponentRefused) { continue; }
            }

            FString AttachTo;
            if ((*OpObject)->TryGetStringField(TEXT("attach_to"), AttachTo) && !AttachTo.IsEmpty())
            {
                const TArray<AActor*> Parents = MatchActors(World, TEXT("name"), AttachTo).Num() == 1
                    ? MatchActors(World, TEXT("name"), AttachTo)
                    : MatchActors(World, TEXT("label"), AttachTo);
                if (Parents.Num() != 1)
                {
                    // Not fatal on its own: an earlier operation in this batch
                    // may be about to create the parent. It is fatal only if it
                    // is still unresolvable when this operation runs.
                    if (!LabelsCreatedInBatch.Contains(AttachTo.ToLower()))
                    {
                        Refusals.Add(FString::Printf(
                            TEXT("operation %d (%s): attach_to '%s' matches %d actors and no "
                                 "earlier operation in this batch creates it."),
                            Index, *R.Label, *AttachTo, Parents.Num()));
                        continue;
                    }
                }
                else if (Existing != nullptr && IsDescendantOf(Parents[0], Existing))
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (%s): attaching to '%s' would make an attachment cycle, "
                             "because '%s' is already below '%s'."),
                        Index, *R.Label, *AttachTo, *Parents[0]->GetName(), *Existing->GetName()));
                    continue;
                }
            }
        }

        R.bSatisfied = IsOpSatisfied(*this, World, R);

        if (SeenActorKeys.Contains(R.ActorKey))
        {
            Refusals.Add(FString::Printf(
                TEXT("operation %d (%s) is the second operation in this batch on %s. A batch that "
                     "names one actor twice has no single desired state to converge on."),
                Index, *R.Label, *R.ActorKey));
            continue;
        }
        SeenActorKeys.Add(R.ActorKey);
        Resolved.Add(R);
    }

    TArray<TSharedPtr<FJsonValue>> ToApply;
    TArray<TSharedPtr<FJsonValue>> Unchanged;
    for (const FResolvedOp& R : Resolved)
    {
        (R.bSatisfied ? Unchanged : ToApply).Add(MakeShared<FJsonValueString>(R.Label));
    }

    TArray<TSharedPtr<FJsonValue>> PlayerStartJson;
    for (const TPair<FString, FVector>& Start : MCPBridgeScene::PlayerStarts(World))
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), Start.Key);
        Item->SetObjectField(TEXT("location"), MCPBridgeScene::VectorJson(Start.Value));
        PlayerStartJson.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("level_path"), LevelPath);
    Result->SetNumberField(TEXT("requested_operation_count"), Operations->Num());
    Result->SetArrayField(TEXT("operations_to_apply"), ToApply);
    Result->SetArrayField(TEXT("unchanged_operations"), Unchanged);
    Result->SetNumberField(TEXT("expected_change_count"), ToApply.Num());
    Result->SetStringField(TEXT("pre_structure_hash"), PreHash);
    Result->SetArrayField(TEXT("player_starts"), PlayerStartJson);

    if (Refusals.Num() > 0)
    {
        OutError = FString::Join(Refusals, TEXT(" "));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        return false;
    }

    // --- Plan mode returns here, having changed nothing. ---
    if (bPlanOnly)
    {
        Result->SetBoolField(TEXT("plan_only"), true);
        if (ToApply.Num() == 0)
        {
            Result->SetStringField(TEXT("predicted_structure_hash"), PreHash);
        }
        else
        {
            Result->SetStringField(TEXT("prediction_unavailable_reason"),
                TEXT("the batch spawns or moves actors, and a spawned actor's object name is "
                     "assigned by the engine when it is made. A predicted hash is offered only "
                     "for a no-op batch, where it is the current hash by definition."));
        }
        Result->SetStringField(TEXT("player_start_check"),
            TEXT("applied at mutation time against the actor's real GetActorBounds, not predicted "
                 "here: the extent of an unspawned brush volume is a guess, and a guess that "
                 "refuses a legal placement or passes an illegal one is worse than a measurement "
                 "inside a boundary that leaves nothing behind. player_starts above is what the "
                 "check will compare against."));
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        return true;
    }

    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Scene batching requires an active transaction.");
        return false;
    }

    // --- One rollback boundary around the whole batch. The level package is
    //     what a failed batch can leave dirty, so that is what is snapshotted.
    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(LevelPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    bool bRollbackAttempted = false;
    bool bRollbackSucceeded = false;
    TArray<TSharedPtr<FJsonValue>> Warnings;

    // Declared before the failure path so a refusal partway through the batch
    // reports how far it got. A rollback that says only "it failed" leaves the
    // caller guessing which operation was the last one to run.
    TArray<TSharedPtr<FJsonValue>> Applied;
    TArray<TSharedPtr<FJsonValue>> Skipped;
    TArray<TSharedPtr<FJsonValue>> SpawnedActors;

    auto FailWithRollback = [&](const FString& Error) -> bool
    {
        bRollbackAttempted = true;
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }
        Rollback.Rollback();
        // Trust the read-back, not the undo. A cancelled transaction looks
        // transactional; the only thing that settles whether the level came back
        // is hashing it again.
        const FString Recovered = MCPBridgeScene::StructureHash(World);
        bRollbackSucceeded = (Recovered == PreHash);
        OutError = bRollbackSucceeded
            ? Error
            : FString::Printf(
                TEXT("%s The rollback did NOT restore the level: the structure hash is %s, "
                     "expected %s. Treat this level as damaged and do not save it."),
                *Error, *Recovered, *PreHash);

        Result->SetBoolField(TEXT("rollback_attempted"), true);
        Result->SetBoolField(TEXT("rollback_succeeded"), bRollbackSucceeded);
        Result->SetStringField(TEXT("post_structure_hash"), Recovered);
        // What the applier had done when it hit the wall. All of it is rolled
        // back; this says where the batch stopped, not what survived.
        Result->SetArrayField(TEXT("applied_before_failure"), Applied);
        Result->SetArrayField(TEXT("unchanged_before_failure"), Skipped);
        Result->SetArrayField(TEXT("spawned_before_failure"), SpawnedActors);
        Result->SetObjectField(TEXT("cleanup"), Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
        Result->SetArrayField(TEXT("warnings"), Warnings);
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        return false;
    };

    /** The AGENTS.md trigger-volume rule, applied to one actor that has just
        been placed. Overlap refuses; anything inside 1.5x the extent warns. */
    auto CheckPlayerStartClearance = [&](AActor* Actor, const FResolvedOp& R, FString& OutRefusal) -> bool
    {
        if (!MCPBridgeScene::IsTriggerVolume(Actor)) { return true; }
        FVector Origin = FVector::ZeroVector;
        FVector Extent = FVector::ZeroVector;
        Actor->GetActorBounds(false, Origin, Extent);
        for (const TPair<FString, FVector>& Start : MCPBridgeScene::PlayerStarts(World))
        {
            const FVector Delta = (Start.Value - Origin).GetAbs();
            if (Delta.X <= Extent.X && Delta.Y <= Extent.Y && Delta.Z <= Extent.Z)
            {
                OutRefusal = FString::Printf(
                    TEXT("operation %d (%s): the trigger volume '%s' contains PlayerStart '%s' at "
                         "(%s, %s, %s). OnBeginOverlap fires only on an outside-to-inside "
                         "transition, so a player who spawns already inside never fires it. Move "
                         "the volume clear of the PlayerStart."),
                    R.Index, *R.Label, *Actor->GetName(), *Start.Key,
                    *MCPBridgeScene::FormatScalar(Start.Value.X),
                    *MCPBridgeScene::FormatScalar(Start.Value.Y),
                    *MCPBridgeScene::FormatScalar(Start.Value.Z));
                return false;
            }
            const FVector Clearance = Extent * 1.5f;
            if (Delta.X <= Clearance.X && Delta.Y <= Clearance.Y && Delta.Z <= Clearance.Z)
            {
                Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (%s): the trigger volume '%s' is within 1.5x its own extent "
                         "of PlayerStart '%s'. It does not contain the PlayerStart, so it is not "
                         "refused, but the clearance AGENTS.md asks for is not there."),
                    R.Index, *R.Label, *Actor->GetName(), *Start.Key)));
            }
        }
        return true;
    };

    // --- Apply. ---
    for (const FResolvedOp& R : Resolved)
    {
        // Asked here, not read from the plan. The batch is ordered, so this
        // operation's precondition is whatever the operations before it left.
        if (IsOpSatisfied(*this, World, R))
        {
            Skipped.Add(MakeShared<FJsonValueString>(R.Label));
            continue;
        }

        AActor* Actor = ResolveOne(World, R);

        if (R.Op == TEXT("delete_actor"))
        {
            if (Actor == nullptr) { continue; }
            const FString DeletedName = Actor->GetName();
            Actor->Modify();
            if (!World->EditorDestroyActor(Actor, true))
            {
                return FailWithRollback(FString::Printf(
                    TEXT("operation %d (%s): Unreal refused to destroy '%s'. The whole batch is "
                         "rolled back rather than left half-applied."),
                    R.Index, *R.Label, *DeletedName));
            }
            Applied.Add(MakeShared<FJsonValueString>(R.Label));
            continue;
        }

        // --- upsert_actor ---
        const TSharedPtr<FJsonObject>& OpSpec = *R.Spec;

        const FVector DefaultLocation = Actor != nullptr ? Actor->GetActorLocation() : FVector::ZeroVector;
        const FRotator DefaultRotation = Actor != nullptr ? Actor->GetActorRotation() : FRotator::ZeroRotator;
        const FVector DefaultScale = Actor != nullptr ? Actor->GetActorScale3D() : FVector::OneVector;

        FVector Location = DefaultLocation;
        FRotator Rotation = DefaultRotation;
        FVector Scale = DefaultScale;
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (OpSpec->TryGetObjectField(TEXT("location"), Object))
        {
            Location.X = (*Object)->HasField(TEXT("x")) ? (*Object)->GetNumberField(TEXT("x")) : Location.X;
            Location.Y = (*Object)->HasField(TEXT("y")) ? (*Object)->GetNumberField(TEXT("y")) : Location.Y;
            Location.Z = (*Object)->HasField(TEXT("z")) ? (*Object)->GetNumberField(TEXT("z")) : Location.Z;
        }
        if (OpSpec->TryGetObjectField(TEXT("rotation"), Object))
        {
            Rotation.Pitch = (*Object)->HasField(TEXT("pitch")) ? (*Object)->GetNumberField(TEXT("pitch")) : Rotation.Pitch;
            Rotation.Yaw = (*Object)->HasField(TEXT("yaw")) ? (*Object)->GetNumberField(TEXT("yaw")) : Rotation.Yaw;
            Rotation.Roll = (*Object)->HasField(TEXT("roll")) ? (*Object)->GetNumberField(TEXT("roll")) : Rotation.Roll;
        }
        if (OpSpec->TryGetObjectField(TEXT("scale"), Object))
        {
            Scale.X = (*Object)->HasField(TEXT("x")) ? (*Object)->GetNumberField(TEXT("x")) : Scale.X;
            Scale.Y = (*Object)->HasField(TEXT("y")) ? (*Object)->GetNumberField(TEXT("y")) : Scale.Y;
            Scale.Z = (*Object)->HasField(TEXT("z")) ? (*Object)->GetNumberField(TEXT("z")) : Scale.Z;
        }

        bool bSpawned = false;
        if (Actor == nullptr)
        {
            if (R.SpawnClass == nullptr)
            {
                return FailWithRollback(FString::Printf(
                    TEXT("operation %d (%s): by the time this operation ran there was no actor to "
                         "change and no class to spawn one from."), R.Index, *R.Label));
            }
            Actor = GEditor->AddActor(
                World->GetCurrentLevel(), R.SpawnClass, FTransform(Rotation, Location), true, RF_Transactional);
            if (!IsValid(Actor))
            {
                return FailWithRollback(FString::Printf(
                    TEXT("operation %d (%s): Unreal could not spawn a %s."),
                    R.Index, *R.Label, *R.SpawnClass->GetPathName()));
            }
            bSpawned = true;
        }
        else if (R.SpawnClass != nullptr && Actor->GetClass() != R.SpawnClass)
        {
            // Pass 1 ruled this out against the PRE-batch level, which an
            // earlier operation may since have moved.
            return FailWithRollback(FString::Printf(
                TEXT("operation %d (%s): by the time this operation ran, the actor it addresses was "
                     "a %s rather than the requested %s. There is no change-class primitive."),
                R.Index, *R.Label, *Actor->GetClass()->GetPathName(), *R.SpawnClass->GetPathName()));
        }

        Actor->Modify();

        if (R.bHasDesiredLabel && Actor->GetActorLabel() != R.DesiredLabel)
        {
            Actor->SetActorLabel(R.DesiredLabel);
            if (Actor->GetActorLabel() != R.DesiredLabel)
            {
                return FailWithRollback(FString::Printf(
                    TEXT("operation %d (%s): the label was set to '%s' and read back as '%s'. "
                         "Unreal renamed it, so this operation could never converge."),
                    R.Index, *R.Label, *R.DesiredLabel, *Actor->GetActorLabel()));
            }
        }

        FString Folder;
        if (OpSpec->TryGetStringField(TEXT("folder"), Folder))
        {
            Actor->SetFolderPath(FName(*Folder));
        }

        const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
        if (OpSpec->TryGetArrayField(TEXT("tags"), Tags) && Tags != nullptr)
        {
            Actor->Tags.Reset();
            for (const TSharedPtr<FJsonValue>& Value : *Tags)
            {
                FString Tag;
                if (Value->TryGetString(Tag)) { Actor->Tags.Add(FName(*Tag)); }
            }
        }

        // Attachment before the transform, so a KeepWorldTransform attach cannot
        // undo the placement that follows it.
        if (OpSpec->HasField(TEXT("attach_to")))
        {
            FString AttachTo;
            OpSpec->TryGetStringField(TEXT("attach_to"), AttachTo);
            if (AttachTo.IsEmpty())
            {
                Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
            }
            else
            {
                TArray<AActor*> Parents = MatchActors(World, TEXT("name"), AttachTo);
                if (Parents.Num() != 1) { Parents = MatchActors(World, TEXT("label"), AttachTo); }
                if (Parents.Num() != 1)
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("operation %d (%s): attach_to '%s' matched %d actors when this "
                             "operation ran."), R.Index, *R.Label, *AttachTo, Parents.Num()));
                }
                if (IsDescendantOf(Parents[0], Actor))
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("operation %d (%s): attaching to '%s' would make an attachment cycle."),
                        R.Index, *R.Label, *AttachTo));
                }
                Parents[0]->Modify();
                Actor->AttachToActor(Parents[0], FAttachmentTransformRules::KeepWorldTransform);
            }
        }

        Actor->SetActorLocation(Location);
        Actor->SetActorRotation(Rotation);
        Actor->SetActorScale3D(Scale);

        const TSharedPtr<FJsonObject>* Properties = nullptr;
        if (OpSpec->TryGetObjectField(TEXT("properties"), Properties))
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
            {
                TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
                Wrapper->SetField(TEXT("value"), Pair.Value);
                FString ObjectPath;
                FString SetError;
                if (!SetObjectPropertyJson(
                        Actor, Pair.Key, ValueToJsonText(MakeShared<FJsonValueObject>(Wrapper)),
                        ObjectPath, SetError))
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("operation %d (%s): could not set '%s' on '%s': %s"),
                        R.Index, *R.Label, *Pair.Key, *Actor->GetName(), *SetError));
                }
            }
        }

        const TSharedPtr<FJsonObject>* Components = nullptr;
        if (OpSpec->TryGetObjectField(TEXT("components"), Components))
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Components)->Values)
            {
                UActorComponent* Component = FindComponentNamed(Actor, Entry.Key);
                if (Component == nullptr)
                {
                    TArray<FString> Available;
                    for (const UActorComponent* Candidate : SortedComponents(Actor))
                    {
                        Available.Add(Candidate->GetName());
                    }
                    return FailWithRollback(FString::Printf(
                        TEXT("operation %d (%s): '%s' has no component named '%s'. It has: %s."),
                        R.Index, *R.Label, *Actor->GetName(), *Entry.Key,
                        *FString::Join(Available, TEXT(", "))));
                }
                const TSharedPtr<FJsonObject>* ComponentProperties = nullptr;
                Entry.Value->TryGetObject(ComponentProperties);
                Component->Modify();
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ComponentProperties)->Values)
                {
                    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
                    Wrapper->SetField(TEXT("value"), Pair.Value);
                    FString ObjectPath;
                    FString SetError;
                    if (!SetObjectPropertyJson(
                            Component, Pair.Key, ValueToJsonText(MakeShared<FJsonValueObject>(Wrapper)),
                            ObjectPath, SetError))
                    {
                        return FailWithRollback(FString::Printf(
                            TEXT("operation %d (%s): could not set '%s' on component '%s': %s"),
                            R.Index, *R.Label, *Pair.Key, *Entry.Key, *SetError));
                    }
                }
            }
        }

        // Brush volumes rebuild their bounds here, so the PlayerStart check
        // below measures the volume as it now is rather than as it was.
        Actor->PostEditMove(true);

        FString ClearanceRefusal;
        if (!CheckPlayerStartClearance(Actor, R, ClearanceRefusal))
        {
            return FailWithRollback(ClearanceRefusal);
        }

        if (bSpawned) { SpawnedActors.Add(MakeShared<FJsonValueString>(Actor->GetName())); }
        Applied.Add(MakeShared<FJsonValueString>(R.Label));
    }

    // --- Read the level back independently and verify. ---
    const FString PostHash = MCPBridgeScene::StructureHash(World);
    Result->SetStringField(TEXT("post_structure_hash"), PostHash);
    Result->SetArrayField(TEXT("applied_operations"), Applied);
    Result->SetNumberField(TEXT("applied_operation_count"), Applied.Num());
    Result->SetArrayField(TEXT("spawned_actors"), SpawnedActors);
    // Overwrites the plan's list with what the applier actually found when it
    // reached each operation. operations_to_apply and expected_change_count are
    // deliberately left as the plan, so a caller who compares them against
    // applied_operation_count can see where the two disagreed.
    Result->SetArrayField(TEXT("unchanged_operations"), Skipped);
    Result->SetBoolField(TEXT("converged"), Applied.Num() == 0 && PostHash == PreHash);

    TArray<TSharedPtr<FJsonValue>> VerificationMismatches;
    if (bVerify)
    {
        // The independent check is the point. The loop above reports what it
        // did; this asks the level. Every operation is re-run through the SAME
        // satisfied-predicate pass 1 used, against freshly read state.
        for (const FResolvedOp& R : Resolved)
        {
            if (!IsOpSatisfied(*this, World, R))
            {
                VerificationMismatches.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (%s) is not true of the level after the batch."),
                    R.Index, *R.Label)));
            }
        }
        if (Applied.Num() > 0 && PostHash == PreHash)
        {
            VerificationMismatches.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("%d operation(s) reported as applied, but the structure hash is unchanged at "
                     "%s. Either nothing actually changed or the change is invisible to the scene "
                     "snapshot; neither is a result to keep."), Applied.Num(), *PostHash)));
        }
        if (Applied.Num() == 0 && PostHash != PreHash)
        {
            VerificationMismatches.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("no operation was applied, but the structure hash moved from %s to %s."),
                *PreHash, *PostHash)));
        }
    }
    Result->SetArrayField(TEXT("verification_mismatches"), VerificationMismatches);

    if (VerificationMismatches.Num() > 0)
    {
        return FailWithRollback(FString::Printf(
            TEXT("%d verification mismatch(es) after the scene batch, so it is rolled back."),
            VerificationMismatches.Num()));
    }

    Rollback.Commit();

    UPackage* Package = World->GetOutermost();
    Result->SetBoolField(TEXT("level_package_dirty"), Package != nullptr && Package->IsDirty());
    // No save, on purpose. Writing a level to disk is puerts_save's job and is
    // classified destructive there; keeping it out of this command is what lets
    // a failed batch be undone with nothing on disk to clean up.
    Result->SetStringField(TEXT("save_note"),
        TEXT("scene_batch never saves. The level is left dirty in the editor; call puerts_save to "
             "write it, and only after reading the result back with scene_inspect."));
    Result->SetBoolField(TEXT("rollback_attempted"), bRollbackAttempted);
    Result->SetBoolField(TEXT("rollback_succeeded"), bRollbackSucceeded);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    Result->SetObjectField(TEXT("cleanup"), Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
    Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

    // The single-property undo snapshot SetObjectPropertyJson leaves behind is a
    // fallback the undo tool uses when GEditor->UndoTransaction fails. After a
    // batch it holds one property out of many, and replaying that would restore
    // a fraction of the change while reporting success. Clear it: an honest
    // refusal from undo beats a partial restore.
    ActiveUndoActorName.Reset();
    ActiveUndoPropertyName.Reset();
    ActiveUndoValueJson.Reset();

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
    FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
    return true;
}
