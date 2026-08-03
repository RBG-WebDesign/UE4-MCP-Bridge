// Copyright 2026 RareBird Games. All Rights Reserved.
//
// anim_blueprint_build: a native front for UAnimBlueprintBuilderLibrary, which
// was compiled into MCPBridgeGraphBuilder and reachable only through the legacy
// Python listener.
//
// docs/REFRONT_MAP.md group 5 lists that library as BLOCKED, on four counts: no
// transaction, no rollback, no independent inspector, and only partial
// convergence. Three of the four are closed here rather than in the library:
// the transaction comes from IsToolMutating, the rollback boundary is the same
// FBridgeAssetRollback the Blueprint, Widget and Behavior Tree builders use, and
// the inspector is anim_blueprint_inspect beside this file.
//
// The fourth is not closed, and this command does not pretend otherwise. It is
// CREATE-ONLY: it refuses when the target asset already exists. The reason is a
// specific defect, not caution -
// AnimBlueprintBuilder/ABPBuilder.cpp:147-148 says in its own comment that
// Rebuild assumes a clean AnimBlueprint and clears nothing, so rebuilding over
// an existing graph appends a second state machine and a second copy of every
// state rather than converging. Worse, that damage is not recoverable by this
// boundary: FBridgeAssetRollback can delete an asset this command created, but
// it cannot restore the previous contents of an asset that already existed, and
// FKismetEditorUtilities::CompileBlueprint inside the builder puts a compile
// between the transaction and any undo. So a patch path would be a mutation
// with no way back, which is exactly what the REFRONT map's failure-atomic
// column exists to stop. Fix the builder's clear pass first; then a patch
// command becomes writable.
//
// What that leaves is honest and useful: creating a new Animation Blueprint is
// failure-atomic, because the only state a failure has to undo is state this
// command created.

#include "MCPPuerTSBridgeService.h"

#include "AnimBlueprintBuilderLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "AssetRegistryModule.h"
#include "BlueprintGraphBuilderLibrary.h"
#include "Json.h"
#include "MCPBridgeAssetRollback.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace
{
    TArray<TSharedPtr<FJsonValue>> AnimStringsToJson(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Values.Num());
        for (const FString& Value : Values) { Out.Add(MakeShared<FJsonValueString>(Value)); }
        return Out;
    }

    /** The state name inside an inspector state id ("<machine>/state:Walk").
        Comparing whole ids would make verification depend on the state machine
        node's object name, which the request never chose. */
    FString StateNameFromId(const FString& StateId)
    {
        int32 Index = INDEX_NONE;
        if (StateId.FindLastChar(TEXT(':'), Index) && Index + 1 < StateId.Len())
        {
            return StateId.Mid(Index + 1);
        }
        return StateId;
    }

    /** Every state name and every from->to edge the inspector found, pulled out
        of its response so the request can be checked against the asset rather
        than against the builder's own report. */
    void CollectInspectedStructure(
        const TSharedPtr<FJsonObject>& Inspection,
        TArray<FString>& OutStateNames,
        TArray<FString>& OutTransitions,
        int32& OutStateMachineCount)
    {
        OutStateMachineCount = 0;
        const TArray<TSharedPtr<FJsonValue>>* Machines = nullptr;
        if (!Inspection.IsValid() || !Inspection->TryGetArrayField(TEXT("state_machines"), Machines))
        {
            return;
        }
        OutStateMachineCount = Machines->Num();
        for (const TSharedPtr<FJsonValue>& MachineValue : *Machines)
        {
            const TSharedPtr<FJsonObject>* Machine = nullptr;
            if (!MachineValue->TryGetObject(Machine)) { continue; }

            const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
            if ((*Machine)->TryGetArrayField(TEXT("states"), States))
            {
                for (const TSharedPtr<FJsonValue>& StateValue : *States)
                {
                    const TSharedPtr<FJsonObject>* State = nullptr;
                    if (!StateValue->TryGetObject(State)) { continue; }
                    FString Name;
                    if ((*State)->TryGetStringField(TEXT("name"), Name)) { OutStateNames.Add(Name); }
                }
            }

            const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
            if ((*Machine)->TryGetArrayField(TEXT("transitions"), Transitions))
            {
                for (const TSharedPtr<FJsonValue>& TransitionValue : *Transitions)
                {
                    const TSharedPtr<FJsonObject>* Transition = nullptr;
                    if (!TransitionValue->TryGetObject(Transition)) { continue; }
                    FString From;
                    FString To;
                    (*Transition)->TryGetStringField(TEXT("from"), From);
                    (*Transition)->TryGetStringField(TEXT("to"), To);
                    OutTransitions.Add(StateNameFromId(From) + TEXT("->") + StateNameFromId(To));
                }
            }
        }
    }
}

bool UMCPPuerTSBridgeService::BuildAnimBlueprintJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    // The same guard BuildBehaviorTreeJson and BuildWidgetJson open with. It is
    // the only thing standing between an allowlist that lists this command and
    // an IsToolMutating that forgets it: without the transaction, a failure
    // path would have nothing to cancel.
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Animation Blueprint build requires an active transaction.");
        return false;
    }

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Animation Blueprint build spec must be a JSON object.");
        return false;
    }

    FString AssetPath;
    if (!Spec->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutError = TEXT("asset_path is required.");
        return false;
    }
    AssetPath = AssetPath.TrimStartAndEnd();
    // Authoring stays inside /Game/MCPGenerated/, the same limit every other
    // native build command applies. Reading is wider; writing is not.
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/")))
    {
        OutError = TEXT("Animation Blueprint authoring is limited to /Game/MCPGenerated/.");
        return false;
    }
    if (!FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = FString::Printf(TEXT("'%s' is not a valid package path."), *AssetPath);
        return false;
    }

    FString SkeletonPath;
    if (!Spec->TryGetStringField(TEXT("skeleton_path"), SkeletonPath) || SkeletonPath.IsEmpty())
    {
        OutError = TEXT("skeleton_path is required: an Animation Blueprint has no meaning without a skeleton.");
        return false;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString PackageFolder = FPackageName::GetLongPackagePath(AssetPath);
    const FString ObjectPath = AssetPath + TEXT(".") + AssetName;

    // The refusal that makes the rest of this command safe. It runs before any
    // validation so the reason a caller gets is the real one, and it names the
    // read that tells them what is already there.
    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    if (AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath)).IsValid())
    {
        OutError = FString::Printf(
            TEXT("An asset already exists at '%s'. anim_blueprint_build creates a new Animation "
                 "Blueprint and never edits one that exists: the underlying builder's rebuild "
                 "path clears nothing (AnimBlueprintBuilder/ABPBuilder.cpp:147), so running it "
                 "over an existing graph would append a second state machine rather than "
                 "converge, and that is not recoverable. Read the asset with "
                 "anim_blueprint_inspect, or build to a path that does not exist yet."),
            *ObjectPath);
        return false;
    }

    // The builder owns the spec grammar. Validating through its own validator
    // rather than reimplementing it here is what keeps the two from drifting,
    // and it happens before anything is created.
    const FString ValidationError =
        UAnimBlueprintBuilderLibrary::ValidateAnimBlueprintJSON(SpecJson);
    if (!ValidationError.IsEmpty())
    {
        OutError = ValidationError;
        return false;
    }

    const bool bSave = !Spec->HasField(TEXT("save")) || Spec->GetBoolField(TEXT("save"));

    // What was asked for, kept so the read-back can be compared against it.
    TArray<FString> RequestedStates;
    TArray<FString> RequestedTransitions;
    {
        TMap<FString, FString> StateNamesById;
        const TSharedPtr<FJsonObject>* StateMachine = nullptr;
        if (Spec->TryGetObjectField(TEXT("state_machine"), StateMachine))
        {
            const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
            if ((*StateMachine)->TryGetArrayField(TEXT("states"), States))
            {
                for (const TSharedPtr<FJsonValue>& StateValue : *States)
                {
                    const TSharedPtr<FJsonObject>* State = nullptr;
                    if (!StateValue->TryGetObject(State)) { continue; }
                    FString Id;
                    FString Name;
                    (*State)->TryGetStringField(TEXT("id"), Id);
                    (*State)->TryGetStringField(TEXT("name"), Name);
                    if (!Name.IsEmpty())
                    {
                        RequestedStates.Add(Name);
                        StateNamesById.Add(Id, Name);
                    }
                }
            }
            const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
            if ((*StateMachine)->TryGetArrayField(TEXT("transitions"), Transitions))
            {
                for (const TSharedPtr<FJsonValue>& TransitionValue : *Transitions)
                {
                    const TSharedPtr<FJsonObject>* Transition = nullptr;
                    if (!TransitionValue->TryGetObject(Transition)) { continue; }
                    FString From;
                    FString To;
                    (*Transition)->TryGetStringField(TEXT("from"), From);
                    (*Transition)->TryGetStringField(TEXT("to"), To);
                    const FString* FromName = StateNamesById.Find(From);
                    const FString* ToName = StateNamesById.Find(To);
                    RequestedTransitions.Add(
                        (FromName != nullptr ? *FromName : From)
                        + TEXT("->") + (ToName != nullptr ? *ToName : To));
                }
            }
        }
    }

    // --- Mutation starts here. ---
    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;

    // The builder creates the asset itself, so it cannot be tracked before the
    // call. It is tracked immediately after, on the failure path as well as the
    // success path, because a build that failed halfway leaves exactly the
    // registered, dirty asset the rollback boundary exists to remove.
    auto TrackWhateverWasCreated = [&]()
    {
        const FAssetData Created = AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath));
        if (Created.IsValid())
        {
            Rollback.TrackCreated(Created.GetAsset());
        }
    };

    auto FailRolledBack = [&](const FString& CompileStatus) -> bool
    {
        // Cancel first: the transaction's undo records reference objects the
        // rollback is about to destroy, and they must be replayed while those
        // objects are still live.
        if (ActiveTransaction != nullptr)
        {
            ActiveTransaction->Cancel();
        }
        Rollback.Rollback();

        TSharedPtr<FJsonObject> Failed = MakeShared<FJsonObject>();
        Failed->SetStringField(TEXT("asset_path"), AssetPath);
        Failed->SetStringField(TEXT("object_path"), FString());
        Failed->SetBoolField(TEXT("created"), false);
        Failed->SetStringField(TEXT("compile_status"), CompileStatus);
        Failed->SetBoolField(TEXT("saved"), false);
        Failed->SetArrayField(TEXT("requested_states"), AnimStringsToJson(RequestedStates));
        Failed->SetArrayField(TEXT("requested_transitions"), AnimStringsToJson(RequestedTransitions));
        Failed->SetArrayField(TEXT("errors"), Errors);
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("The build failed and was rolled back: no asset, package, or file remains.")));
        Failed->SetArrayField(TEXT("warnings"), Warnings);
        Failed->SetObjectField(TEXT("cleanup"),
            Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
        OutResultJson = SerializeJson(Failed);
        return true;
    };

    const FString BuildError = UAnimBlueprintBuilderLibrary::BuildAnimBlueprintFromJSON(
        PackageFolder, AssetName, SkeletonPath, SpecJson);
    TrackWhateverWasCreated();
    if (!BuildError.IsEmpty())
    {
        Errors.Add(MakeShared<FJsonValueString>(BuildError));
        return FailRolledBack(TEXT("RolledBack"));
    }

    UAnimBlueprint* AnimBlueprint =
        Cast<UAnimBlueprint>(AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath)).GetAsset());
    if (AnimBlueprint == nullptr)
    {
        Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
            TEXT("The builder reported success but no Animation Blueprint exists at '%s'."),
            *ObjectPath)));
        return FailRolledBack(TEXT("RolledBack"));
    }

    // A compile the caller can read, rather than a compile the builder swallowed.
    // The builder compiles internally and returns only a message on failure, so
    // a build that succeeded with warnings looked identical to one that did not
    // compile at all.
    FString CompileStatus = TEXT("Unknown");
    bool bCompileSucceeded = false;
    {
        const FString ReportJson = UBlueprintGraphBuilderLibrary::CompileAndReport(AnimBlueprint);
        TSharedPtr<FJsonObject> Report;
        TSharedRef<TJsonReader<>> ReportReader = TJsonReaderFactory<>::Create(ReportJson);
        if (FJsonSerializer::Deserialize(ReportReader, Report) && Report.IsValid())
        {
            Report->TryGetBoolField(TEXT("success"), bCompileSucceeded);
            Report->TryGetStringField(TEXT("status"), CompileStatus);
            const TArray<TSharedPtr<FJsonValue>>* ReportErrors = nullptr;
            if (Report->TryGetArrayField(TEXT("errors"), ReportErrors)) { Errors.Append(*ReportErrors); }
            const TArray<TSharedPtr<FJsonValue>>* ReportWarnings = nullptr;
            if (Report->TryGetArrayField(TEXT("warnings"), ReportWarnings)) { Warnings.Append(*ReportWarnings); }
        }
        else
        {
            Errors.Add(MakeShared<FJsonValueString>(
                TEXT("The Animation Blueprint compiler report could not be parsed.")));
        }
    }
    if (!bCompileSucceeded)
    {
        return FailRolledBack(CompileStatus);
    }

    // The independent read-back. This is the whole reason the inspector shipped
    // first: without it, "the build reported four states" and "the asset holds
    // four states" would be the same sentence from the same code.
    TSharedPtr<FJsonObject> Inspection;
    {
        TSharedPtr<FJsonObject> InspectRequest = MakeShared<FJsonObject>();
        InspectRequest->SetStringField(TEXT("asset_path"), AssetPath);
        FString InspectionJson;
        FString InspectionError;
        if (!InspectAnimBlueprintJson(SerializeJson(InspectRequest), InspectionJson, InspectionError))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("The Animation Blueprint could not be read back: %s"), *InspectionError)));
            return FailRolledBack(CompileStatus);
        }
        TSharedRef<TJsonReader<>> InspectionReader = TJsonReaderFactory<>::Create(InspectionJson);
        FJsonSerializer::Deserialize(InspectionReader, Inspection);
    }

    TArray<FString> ActualStates;
    TArray<FString> ActualTransitions;
    int32 StateMachineCount = 0;
    CollectInspectedStructure(Inspection, ActualStates, ActualTransitions, StateMachineCount);

    TArray<FString> MissingStates;
    for (const FString& State : RequestedStates)
    {
        if (!ActualStates.Contains(State)) { MissingStates.Add(State); }
    }
    TArray<FString> MissingTransitions;
    for (const FString& Transition : RequestedTransitions)
    {
        if (!ActualTransitions.Contains(Transition)) { MissingTransitions.Add(Transition); }
    }
    if (MissingStates.Num() > 0 || MissingTransitions.Num() > 0)
    {
        Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
            TEXT("The Animation Blueprint compiled, but reading it back found %d requested "
                 "state(s) and %d requested transition(s) missing: %s"),
            MissingStates.Num(), MissingTransitions.Num(),
            *FString::Join(MissingStates.Num() > 0 ? MissingStates : MissingTransitions, TEXT(", ")))));
        return FailRolledBack(CompileStatus);
    }

    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(ObjectPath, SaveError);
        if (!bSaved)
        {
            // A half-written asset is the same hazard as a half-built one.
            Errors.Add(MakeShared<FJsonValueString>(
                FString::Printf(TEXT("Asset save failed: %s"), *SaveError)));
            return FailRolledBack(CompileStatus);
        }
    }

    // Past every failure exit: keep what the request made.
    Rollback.Commit();

    Warnings.Add(MakeShared<FJsonValueString>(
        TEXT("Animation Blueprint asset creation is not undoable: undo restores level and actor "
             "state, not the new package.")));

    FString StructureHash;
    if (Inspection.IsValid())
    {
        Inspection->TryGetStringField(TEXT("structure_hash_sha1"), StructureHash);
    }

    TSharedPtr<FJsonObject> Verification = MakeShared<FJsonObject>();
    Verification->SetArrayField(TEXT("requested_states"), AnimStringsToJson(RequestedStates));
    Verification->SetArrayField(TEXT("actual_states"), AnimStringsToJson(ActualStates));
    Verification->SetArrayField(TEXT("requested_transitions"), AnimStringsToJson(RequestedTransitions));
    Verification->SetArrayField(TEXT("actual_transitions"), AnimStringsToJson(ActualTransitions));
    Verification->SetNumberField(TEXT("state_machine_count"), StateMachineCount);
    Verification->SetBoolField(TEXT("verified"), true);
    Verification->SetStringField(TEXT("verified_by"), TEXT("anim_blueprint_inspect"));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("object_path"), AnimBlueprint->GetPathName());
    Result->SetStringField(TEXT("generated_class_path"),
        AnimBlueprint->GeneratedClass != nullptr
            ? AnimBlueprint->GeneratedClass->GetPathName()
            : FString());
    Result->SetStringField(TEXT("skeleton_path"), SkeletonPath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("compiled"), bCompileSucceeded);
    Result->SetStringField(TEXT("compile_status"), CompileStatus);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetStringField(TEXT("structure_hash_sha1"), StructureHash);
    Result->SetObjectField(TEXT("verification"), Verification);
    // Stated on every success rather than left for a caller to discover: this
    // command is not convergent, it is create-only, and a rerun is a refusal.
    Result->SetBoolField(TEXT("convergent"), false);
    Result->SetStringField(TEXT("convergence_note"),
        TEXT("anim_blueprint_build creates a new asset and refuses an existing one. Rerunning "
             "the same spec against the same path is a refusal, not a no-op."));
    Result->SetArrayField(TEXT("errors"), Errors);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    Result->SetObjectField(TEXT("cleanup"),
        Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
    OutResultJson = SerializeJson(Result);
    return true;
}
