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
// The fourth is closed by SPLITTING the command in two rather than by making
// one command do both jobs. anim_blueprint_build creates and refuses an asset
// that exists; anim_blueprint_patch (at the bottom of this file) edits one that
// exists and refuses one that does not. Neither ever guesses which the caller
// meant, and build stays failure-atomic for the cheapest possible reason: the
// only state its failure has to undo is state it created.
//
// How the patch half became possible, since finding 0t said it was not:
//
// FIXED (lane W): FAnimBPBuilder::Rebuild cleared nothing, so rebuilding over an
// existing graph appended a second state machine and a second copy of every
// state rather than converging. FAnimBPBuilder::ClearGeneratedGraph is the pass
// it never had, and Rebuild now calls it. So a rerun converges.
//
// FIXED (lane Y): a rerun that FAILS is now recoverable. FBridgeAssetRollback
// can only delete an asset the command created, and finding 0t called the
// missing half "a content snapshot the boundary can restore". It turned out not
// to need a new copy of anything: the .uasset on disk already IS that snapshot,
// so FBridgeContentSnapshot refuses to start unless the asset is saved and
// clean, and restores with UPackageTools::ReloadPackages. See the comment on
// that class for why the in-memory alternative was rejected.

#include "MCPPuerTSBridgeService.h"

#include "AnimBlueprintBuilderLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "AssetRegistryModule.h"
#include "BlueprintGraphBuilderLibrary.h"
#include "Json.h"
#include "MCPBridgeAssetRollback.h"
#include "MCPBridgeContentSnapshot.h"
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

    /** The state names and from->to edges the REQUEST asked for, resolved from
        spec ids to state names so they can be compared against what
        CollectInspectedStructure reads back. Shared by build and patch: two
        copies of this would let the two commands disagree about what they asked
        for, and the visible failure is one of them verifying against a shape the
        other never requested. */
    void CollectRequestedStructure(
        const TSharedPtr<FJsonObject>& Spec,
        TArray<FString>& OutStateNames,
        TArray<FString>& OutTransitions)
    {
        TMap<FString, FString> StateNamesById;
        const TSharedPtr<FJsonObject>* StateMachine = nullptr;
        if (!Spec.IsValid() || !Spec->TryGetObjectField(TEXT("state_machine"), StateMachine))
        {
            return;
        }
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
                    OutStateNames.Add(Name);
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
                OutTransitions.Add(
                    (FromName != nullptr ? *FromName : From)
                    + TEXT("->") + (ToName != nullptr ? *ToName : To));
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
                 "Blueprint and never edits one that exists. To replace the contents of this "
                 "asset use anim_blueprint_patch, which clears the generated AnimGraph before "
                 "rebuilding it and restores the asset from disk if any step fails; to read it "
                 "use anim_blueprint_inspect; or build to a path that does not exist yet."),
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
    CollectRequestedStructure(Spec, RequestedStates, RequestedTransitions);

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

// ---------------------------------------------------------------------------
// anim_blueprint_patch
//
// The convergent half that finding 0t said could not ship. What changed is not
// the builder: lane W's ClearGeneratedGraph already made a rerun CONVERGE. What
// changed is that a failed rerun is now RECOVERABLE, through
// FBridgeContentSnapshot, whose whole idea is that the snapshot is the file on
// disk and the restore is UPackageTools::ReloadPackages.
//
// Three properties, in the order they are established:
//
//   1. Failure-atomic. Capture() refuses unless the asset is saved and clean, so
//      a byte-exact restore source is known to exist before anything is touched.
//      Every failure exit cancels the transaction, reloads the package, and then
//      re-reads the asset through anim_blueprint_inspect to decide whether the
//      rollback worked - the rule finding 0r settled. rollback_succeeded is a
//      measurement, not a claim.
//   2. Nothing is written to disk until the result is verified. That is what
//      makes even a FAILED restore recoverable: the .uasset is untouched at
//      every failure exit, so the worst case is a wrong object in memory, which
//      reopening the editor fixes. Finding 0t's emptied AnimBlueprint was
//      unrecoverable because it had already been saved.
//   3. Convergent, and honest about which sense. The requested states and
//      transitions are compared against what the inspector reads back, by name,
//      so a second identical patch produces the same ANSWER. The structure hash
//      is not promised to be stable across a rerun: anim_blueprint_inspect
//      derives node identity from the node's UObject name, and clearing and
//      rebuilding a graph reassigns those. Rollback comparison is unaffected,
//      because a restore reloads the same file and gets the same names back.
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::PatchAnimBlueprintJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Animation Blueprint patch requires an active transaction.");
        return false;
    }

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Animation Blueprint patch spec must be a JSON object.");
        return false;
    }

    FString AssetPath;
    if (!Spec->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutError = TEXT("asset_path is required.");
        return false;
    }
    AssetPath = AssetPath.TrimStartAndEnd();
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

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = AssetPath + TEXT(".") + AssetName;

    // The exact inverse of the build command's refusal. build creates and
    // refuses an existing asset; patch edits and refuses a missing one. Together
    // they cover the whole space with no path where either guesses.
    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData Existing = AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath));
    if (!Existing.IsValid())
    {
        OutError = FString::Printf(
            TEXT("No asset exists at '%s'. anim_blueprint_patch edits an Animation Blueprint "
                 "that is already there and never creates one; use anim_blueprint_build."),
            *ObjectPath);
        return false;
    }
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Existing.GetAsset());
    if (AnimBlueprint == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The asset at '%s' is a %s, not an Animation Blueprint."),
            *ObjectPath, *Existing.AssetClass.ToString());
        return false;
    }

    const FString ValidationError =
        UAnimBlueprintBuilderLibrary::ValidateAnimBlueprintJSON(SpecJson);
    if (!ValidationError.IsEmpty())
    {
        OutError = ValidationError;
        return false;
    }

    // What the asset looks like BEFORE, read by the same inspector that will
    // decide afterwards whether a rollback put it back. Read before the snapshot
    // is captured so a failure here costs nothing.
    FString HashBefore;
    auto ReadStructureHash = [&](FString& OutHash) -> bool
    {
        TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("asset_path"), AssetPath);
        FString Json;
        FString Error;
        if (!InspectAnimBlueprintJson(SerializeJson(Request), Json, Error)) { return false; }
        TSharedPtr<FJsonObject> Parsed;
        TSharedRef<TJsonReader<>> ParsedReader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(ParsedReader, Parsed) || !Parsed.IsValid()) { return false; }
        return Parsed->TryGetStringField(TEXT("structure_hash_sha1"), OutHash);
    };
    if (!ReadStructureHash(HashBefore))
    {
        OutError = FString::Printf(
            TEXT("'%s' could not be read before patching, so a failed patch could not be "
                 "checked for damage. Refusing rather than editing blind."), *ObjectPath);
        return false;
    }

    // The gate. Everything below is safe only because this succeeded: it proves
    // a byte-exact restore source is on disk right now.
    FBridgeContentSnapshot Snapshot;
    FString SnapshotRefusal;
    if (!Snapshot.Capture(AssetPath, SnapshotRefusal))
    {
        OutError = SnapshotRefusal;
        return false;
    }

    TArray<FString> RequestedStates;
    TArray<FString> RequestedTransitions;
    CollectRequestedStructure(Spec, RequestedStates, RequestedTransitions);

    const bool bSave = !Spec->HasField(TEXT("save")) || Spec->GetBoolField(TEXT("save"));

    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;

    auto FailRestored = [&](const FString& CompileStatus) -> bool
    {
        // Cancel first. The transaction's undo records reference objects the
        // reload is about to destroy, and the reload resets the undo buffer
        // afterwards for that reason.
        if (ActiveTransaction != nullptr)
        {
            ActiveTransaction->Cancel();
        }
        FString RestoreError;
        const bool bMechanismOk = Snapshot.Restore(RestoreError);
        if (!RestoreError.IsEmpty())
        {
            Errors.Add(MakeShared<FJsonValueString>(RestoreError));
        }

        // The measurement that decides rollback_succeeded. Re-resolved through
        // the asset registry rather than through the pointer above, which the
        // reload invalidated.
        FString HashAfter;
        const bool bReadBack = ReadStructureHash(HashAfter);
        const bool bRolledBack = bMechanismOk && bReadBack && HashAfter == HashBefore;
        if (!bRolledBack)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("The rollback did NOT restore '%s': the structure hash reads %s and %s "
                     "was expected. The .uasset on disk was never written by this command, so "
                     "closing and reopening the editor recovers the asset."),
                *ObjectPath,
                bReadBack ? *HashAfter : TEXT("<unreadable>"),
                *HashBefore)));
        }

        TSharedPtr<FJsonObject> Failed = MakeShared<FJsonObject>();
        Failed->SetStringField(TEXT("asset_path"), AssetPath);
        Failed->SetStringField(TEXT("object_path"), ObjectPath);
        Failed->SetBoolField(TEXT("patched"), false);
        Failed->SetStringField(TEXT("compile_status"), CompileStatus);
        Failed->SetBoolField(TEXT("saved"), false);
        Failed->SetStringField(TEXT("structure_hash_sha1_before"), HashBefore);
        Failed->SetStringField(TEXT("structure_hash_sha1"), bReadBack ? HashAfter : FString());
        Failed->SetBoolField(TEXT("rollback_succeeded"), bRolledBack);
        Failed->SetStringField(TEXT("rollback_verified_by"), TEXT("anim_blueprint_inspect"));
        Failed->SetArrayField(TEXT("requested_states"), AnimStringsToJson(RequestedStates));
        Failed->SetArrayField(TEXT("requested_transitions"), AnimStringsToJson(RequestedTransitions));
        Failed->SetArrayField(TEXT("errors"), Errors);
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("The patch failed. The asset was restored from the copy on disk; nothing was "
                 "written, so the file is exactly as it was before this command ran.")));
        if (Snapshot.WasCaptured())
        {
            Warnings.Add(MakeShared<FJsonValueString>(
                TEXT("Restoring from disk clears the editor's undo history, because the undo "
                     "records point at objects the reload destroyed.")));
        }
        Failed->SetArrayField(TEXT("warnings"), Warnings);
        Failed->SetObjectField(TEXT("restore"), Snapshot.BuildEvidence());
        OutResultJson = SerializeJson(Failed);
        return true;
    };

    // --- Mutation starts here. ---
    const FString RebuildError =
        UAnimBlueprintBuilderLibrary::RebuildAnimBlueprintFromJSON(AnimBlueprint, SpecJson);
    if (!RebuildError.IsEmpty())
    {
        Errors.Add(MakeShared<FJsonValueString>(RebuildError));
        return FailRestored(TEXT("Restored"));
    }

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
        return FailRestored(CompileStatus);
    }

    // The independent read-back, before anything reaches disk.
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
            return FailRestored(CompileStatus);
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
        return FailRestored(CompileStatus);
    }

    // Duplication is what the clear pass exists to stop, so it is checked rather
    // than assumed. A spec declares one state machine; two means the clear pass
    // did not run or did not reach this graph, and that is the exact damage
    // finding 0t describes.
    if (StateMachineCount > 1)
    {
        Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
            TEXT("Patching left %d state machines in '%s' where the spec declares one. The "
                 "AnimGraph clear pass did not converge; the asset was restored."),
            StateMachineCount, *ObjectPath)));
        return FailRestored(CompileStatus);
    }

    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(ObjectPath, SaveError);
        if (!bSaved)
        {
            Errors.Add(MakeShared<FJsonValueString>(
                FString::Printf(TEXT("Asset save failed: %s"), *SaveError)));
            return FailRestored(CompileStatus);
        }
    }

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
    Result->SetStringField(TEXT("target_skeleton"),
        AnimBlueprint->TargetSkeleton != nullptr
            ? AnimBlueprint->TargetSkeleton->GetPathName()
            : FString());
    Result->SetBoolField(TEXT("patched"), true);
    Result->SetBoolField(TEXT("compiled"), bCompileSucceeded);
    Result->SetStringField(TEXT("compile_status"), CompileStatus);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetStringField(TEXT("structure_hash_sha1_before"), HashBefore);
    Result->SetStringField(TEXT("structure_hash_sha1"), StructureHash);
    Result->SetObjectField(TEXT("verification"), Verification);
    Result->SetBoolField(TEXT("convergent"), true);
    Result->SetStringField(TEXT("convergence_note"),
        TEXT("Rerunning the same spec produces the same states and transitions, because the "
             "builder clears the generated AnimGraph before repopulating it. structure_hash_sha1 "
             "is NOT promised to be stable across a rerun: anim_blueprint_inspect derives node "
             "identity from each node's UObject name, and a clear-and-rebuild reassigns those. "
             "Compare verification.actual_states and actual_transitions to decide convergence."));
    Result->SetArrayField(TEXT("errors"), Errors);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    Result->SetObjectField(TEXT("restore"), Snapshot.BuildEvidence());
    OutResultJson = SerializeJson(Result);
    return true;
}
