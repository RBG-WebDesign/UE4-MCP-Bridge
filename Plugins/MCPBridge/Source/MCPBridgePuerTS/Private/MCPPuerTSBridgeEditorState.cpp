// Copyright 2026 RareBird Games. All Rights Reserved.
//
// Native fronts for the state that is not an asset: project input mappings,
// Content Browser folder visibility, a PIE camera shake, and the read-only
// half of the PIE agent.
//
// These four share one property that shapes the whole file: none of them is a
// UObject the editor transaction buffer can undo. Input mappings and hidden
// folders are ini state; a camera shake is a runtime effect that ends when the
// session does; the PIE agent reads a world nobody is saving. So none of these
// commands appears in IsToolMutating, exactly as viewport commands do not, and
// no transaction is opened around them. AGENTS.md's rule that every editor
// mutation is wrapped in a transaction is a rule about asset state; wrapping an
// ini write in an FScopedTransaction would produce a transaction record that
// undoes nothing and an undo that silently fails.
//
// What replaces the transaction, where a write can fail halfway, is a snapshot.
// PatchInputMappingsJson copies both mapping arrays before the first write and
// restores them exactly on any failure, then re-reads and compares the hash to
// decide whether the restore worked. That is the same discipline
// blueprint_member_patch applies to a Blueprint: the rollback is not believed,
// it is measured.
//
// The C++ underneath is entirely existing: UMCPBridgeInputLibrary (which exists
// because UE4.27 Python cannot construct an FKey), UFolderVisibilityLibrary,
// UAutoPIEHelper and UPIEAgentLibrary were all compiled into the plugin and
// reachable only through the legacy Python listener. Only the doorway is new.

#include "MCPPuerTSBridgeService.h"

#include "AutoPIEHelper.h"
#include "FolderVisibilityLibrary.h"
#include "GameFramework/InputSettings.h"
#include "InputCoreTypes.h"
#include "Json.h"
#include "MCPBridgeInputLibrary.h"
#include "Misc/SecureHash.h"
#include "PIEAgentLibrary.h"

namespace
{
    TSharedPtr<FJsonObject> ParseRequest(const FString& Json)
    {
        TSharedPtr<FJsonObject> Object;
        if (Json.IsEmpty())
        {
            return MakeShared<FJsonObject>();
        }
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
        {
            return nullptr;
        }
        return Object;
    }

    FString SerializeObject(const TSharedPtr<FJsonObject>& Object)
    {
        FString Output;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Output;
    }

    /** One action mapping as canonical text. Modifier flags are part of the
        identity because two mappings of the same action and key that differ
        only by Shift are two different bindings to Unreal. */
    FString ActionLine(const FInputActionKeyMapping& Mapping)
    {
        return FString::Printf(TEXT("A|%s|%s|%d%d%d%d"),
            *Mapping.ActionName.ToString(),
            *Mapping.Key.ToString(),
            Mapping.bShift ? 1 : 0,
            Mapping.bCtrl ? 1 : 0,
            Mapping.bAlt ? 1 : 0,
            Mapping.bCmd ? 1 : 0);
    }

    /** One axis mapping as canonical text. Scale is formatted to six decimals
        rather than printed at full precision, so a value that round-trips
        through JSON does not read as a different mapping. */
    FString AxisLine(const FInputAxisKeyMapping& Mapping)
    {
        return FString::Printf(TEXT("X|%s|%s|%.6f"),
            *Mapping.AxisName.ToString(),
            *Mapping.Key.ToString(),
            Mapping.Scale);
    }

    TSharedPtr<FJsonObject> ActionToJson(const FInputActionKeyMapping& Mapping)
    {
        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("name"), Mapping.ActionName.ToString());
        Object->SetStringField(TEXT("key"), Mapping.Key.ToString());
        Object->SetBoolField(TEXT("shift"), Mapping.bShift);
        Object->SetBoolField(TEXT("ctrl"), Mapping.bCtrl);
        Object->SetBoolField(TEXT("alt"), Mapping.bAlt);
        Object->SetBoolField(TEXT("cmd"), Mapping.bCmd);
        return Object;
    }

    TSharedPtr<FJsonObject> AxisToJson(const FInputAxisKeyMapping& Mapping)
    {
        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("name"), Mapping.AxisName.ToString());
        Object->SetStringField(TEXT("key"), Mapping.Key.ToString());
        Object->SetNumberField(TEXT("scale"), Mapping.Scale);
        return Object;
    }

    /** Canonical hash of the whole mapping set. Sorted, so two reads of an
        unchanged project produce the same digest whatever order the ini
        happened to be written in. */
    FString MappingHash(
        const TArray<FInputActionKeyMapping>& Actions,
        const TArray<FInputAxisKeyMapping>& Axes)
    {
        TArray<FString> Lines;
        Lines.Reserve(Actions.Num() + Axes.Num());
        for (const FInputActionKeyMapping& Mapping : Actions)
        {
            Lines.Add(ActionLine(Mapping));
        }
        for (const FInputAxisKeyMapping& Mapping : Axes)
        {
            Lines.Add(AxisLine(Mapping));
        }
        Lines.Sort();
        const FString Joined = FString::Join(Lines, TEXT("\n"));
        uint8 Digest[FSHA1::DigestSize];
        const FTCHARToUTF8 Utf8(*Joined);
        FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Digest);
        return BytesToHex(Digest, FSHA1::DigestSize);
    }

    /** A desired action mapping as the spec writes it. */
    struct FDesiredAction
    {
        FInputActionKeyMapping Mapping;
        FString KeyName;
    };

    struct FDesiredAxis
    {
        FInputAxisKeyMapping Mapping;
        FString KeyName;
    };

    /** Read a {name, key, shift?, ctrl?, alt?, cmd?} entry. An unresolvable
        key name is refused here, by name, before anything is written: the
        library would refuse it too, but only after earlier entries in the same
        batch had already been saved to the ini. */
    bool ReadDesiredAction(const TSharedPtr<FJsonObject>& Entry, FDesiredAction& Out, FString& OutError)
    {
        FString Name;
        FString KeyName;
        if (!Entry->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        {
            OutError = TEXT("Every actions entry needs a non-empty 'name'.");
            return false;
        }
        if (!Entry->TryGetStringField(TEXT("key"), KeyName) || KeyName.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Action '%s' has no 'key'."), *Name);
            return false;
        }
        const FKey Key(*KeyName);
        if (!Key.IsValid())
        {
            OutError = FString::Printf(
                TEXT("Action '%s' names key '%s', which is not a valid FKey. Key names are FKey ")
                TEXT("string form: W, SpaceBar, LeftMouseButton, MouseX, Gamepad_LeftTrigger."),
                *Name, *KeyName);
            return false;
        }
        bool bShift = false;
        bool bCtrl = false;
        bool bAlt = false;
        bool bCmd = false;
        Entry->TryGetBoolField(TEXT("shift"), bShift);
        Entry->TryGetBoolField(TEXT("ctrl"), bCtrl);
        Entry->TryGetBoolField(TEXT("alt"), bAlt);
        Entry->TryGetBoolField(TEXT("cmd"), bCmd);
        Out.Mapping = FInputActionKeyMapping(*Name, Key, bShift, bCtrl, bAlt, bCmd);
        Out.KeyName = KeyName;
        return true;
    }

    bool ReadDesiredAxis(const TSharedPtr<FJsonObject>& Entry, FDesiredAxis& Out, FString& OutError)
    {
        FString Name;
        FString KeyName;
        if (!Entry->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        {
            OutError = TEXT("Every axes entry needs a non-empty 'name'.");
            return false;
        }
        if (!Entry->TryGetStringField(TEXT("key"), KeyName) || KeyName.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Axis '%s' has no 'key'."), *Name);
            return false;
        }
        const FKey Key(*KeyName);
        if (!Key.IsValid())
        {
            OutError = FString::Printf(
                TEXT("Axis '%s' names key '%s', which is not a valid FKey."), *Name, *KeyName);
            return false;
        }
        double Scale = 1.0;
        if (Entry->HasTypedField<EJson::Number>(TEXT("scale")))
        {
            Scale = Entry->GetNumberField(TEXT("scale"));
        }
        Out.Mapping = FInputAxisKeyMapping(*Name, Key, static_cast<float>(Scale));
        Out.KeyName = KeyName;
        return true;
    }

    /** Put both mapping arrays back exactly as they were found. Exact struct
        equality decides what to remove and what to re-add, so a restore never
        takes a modifier variant the snapshot still holds. */
    void RestoreMappings(
        UInputSettings* Settings,
        const TArray<FInputActionKeyMapping>& Actions,
        const TArray<FInputAxisKeyMapping>& Axes)
    {
        const TArray<FInputActionKeyMapping> CurrentActions = Settings->GetActionMappings();
        for (const FInputActionKeyMapping& Mapping : CurrentActions)
        {
            if (!Actions.Contains(Mapping))
            {
                Settings->RemoveActionMapping(Mapping, false);
            }
        }
        for (const FInputActionKeyMapping& Mapping : Actions)
        {
            if (!CurrentActions.Contains(Mapping))
            {
                Settings->AddActionMapping(Mapping, false);
            }
        }
        const TArray<FInputAxisKeyMapping> CurrentAxes = Settings->GetAxisMappings();
        for (const FInputAxisKeyMapping& Mapping : CurrentAxes)
        {
            if (!Axes.Contains(Mapping))
            {
                Settings->RemoveAxisMapping(Mapping, false);
            }
        }
        for (const FInputAxisKeyMapping& Mapping : Axes)
        {
            if (!CurrentAxes.Contains(Mapping))
            {
                Settings->AddAxisMapping(Mapping, false);
            }
        }
        Settings->ForceRebuildKeymaps();
        Settings->SaveKeyMappings();
    }

    TArray<TSharedPtr<FJsonValue>> StringValues(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Values.Num());
        for (const FString& Value : Values)
        {
            Out.Add(MakeShared<FJsonValueString>(Value));
        }
        return Out;
    }
}

bool UMCPPuerTSBridgeService::InspectInputMappingsJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    const TSharedPtr<FJsonObject> Request = ParseRequest(RequestJson);
    if (!Request.IsValid())
    {
        OutError = TEXT("input_mapping_info request is not a JSON object.");
        return false;
    }
    UInputSettings* Settings = UInputSettings::GetInputSettings();
    if (Settings == nullptr)
    {
        OutError = TEXT("UInputSettings is unavailable, so no input mapping can be read.");
        return false;
    }

    FString ActionFilter;
    FString AxisFilter;
    FString KeyFilter;
    Request->TryGetStringField(TEXT("action_name"), ActionFilter);
    Request->TryGetStringField(TEXT("axis_name"), AxisFilter);
    Request->TryGetStringField(TEXT("key"), KeyFilter);

    // Sorted copies, so the hash and the reported arrays agree and two reads of
    // an unchanged project return the same bytes.
    TArray<FInputActionKeyMapping> Actions = Settings->GetActionMappings();
    TArray<FInputAxisKeyMapping> Axes = Settings->GetAxisMappings();
    Actions.Sort([](const FInputActionKeyMapping& A, const FInputActionKeyMapping& B)
    {
        return ActionLine(A) < ActionLine(B);
    });
    Axes.Sort([](const FInputAxisKeyMapping& A, const FInputAxisKeyMapping& B)
    {
        return AxisLine(A) < AxisLine(B);
    });
    const FString Hash = MappingHash(Actions, Axes);

    TArray<TSharedPtr<FJsonValue>> ActionJson;
    for (const FInputActionKeyMapping& Mapping : Actions)
    {
        if (!ActionFilter.IsEmpty() && Mapping.ActionName.ToString() != ActionFilter)
        {
            continue;
        }
        if (!KeyFilter.IsEmpty() && Mapping.Key.ToString() != KeyFilter)
        {
            continue;
        }
        ActionJson.Add(MakeShared<FJsonValueObject>(ActionToJson(Mapping).ToSharedRef()));
    }
    TArray<TSharedPtr<FJsonValue>> AxisJson;
    for (const FInputAxisKeyMapping& Mapping : Axes)
    {
        if (!AxisFilter.IsEmpty() && Mapping.AxisName.ToString() != AxisFilter)
        {
            continue;
        }
        if (!KeyFilter.IsEmpty() && Mapping.Key.ToString() != KeyFilter)
        {
            continue;
        }
        AxisJson.Add(MakeShared<FJsonValueObject>(AxisToJson(Mapping).ToSharedRef()));
    }

    TSharedPtr<FJsonObject> Filters = MakeShared<FJsonObject>();
    Filters->SetStringField(TEXT("action_name"), ActionFilter);
    Filters->SetStringField(TEXT("axis_name"), AxisFilter);
    Filters->SetStringField(TEXT("key"), KeyFilter);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("source"), TEXT("UInputSettings"));
    Result->SetStringField(TEXT("saved_to"), TEXT("Config/DefaultInput.ini"));
    Result->SetObjectField(TEXT("filters"), Filters.ToSharedRef());
    Result->SetArrayField(TEXT("action_mappings"), ActionJson);
    Result->SetArrayField(TEXT("axis_mappings"), AxisJson);
    Result->SetNumberField(TEXT("action_count"), ActionJson.Num());
    Result->SetNumberField(TEXT("axis_count"), AxisJson.Num());
    // Totals are the unfiltered project, so a filtered read still says how much
    // it did not show rather than looking like an empty project.
    Result->SetNumberField(TEXT("action_total"), Actions.Num());
    Result->SetNumberField(TEXT("axis_total"), Axes.Num());
    // Always the whole set, never the filtered view: a hash of a filtered read
    // could not be compared against a patch's hash.
    Result->SetStringField(TEXT("mapping_hash_sha1"), Hash);
    OutResultJson = SerializeObject(Result);
    return true;
}

bool UMCPPuerTSBridgeService::PatchInputMappingsJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const TSharedPtr<FJsonObject> Spec = ParseRequest(SpecJson);
    if (!Spec.IsValid())
    {
        OutError = TEXT("input_mapping_patch spec is not a JSON object.");
        return false;
    }
    UInputSettings* Settings = UInputSettings::GetInputSettings();
    if (Settings == nullptr)
    {
        OutError = TEXT("UInputSettings is unavailable, so no input mapping can be changed.");
        return false;
    }

    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    bool bRemoveUnlisted = false;
    Spec->TryGetBoolField(TEXT("remove_unlisted"), bRemoveUnlisted);

    // ---- Read the desired set. Nothing is written until all of it parses. ----
    TArray<FDesiredAction> DesiredActions;
    TArray<FDesiredAxis> DesiredAxes;
    const TArray<TSharedPtr<FJsonValue>>* RawActions = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* RawAxes = nullptr;
    const bool bHasActions = Spec->TryGetArrayField(TEXT("actions"), RawActions);
    const bool bHasAxes = Spec->TryGetArrayField(TEXT("axes"), RawAxes);
    if (bHasActions)
    {
        for (const TSharedPtr<FJsonValue>& Value : *RawActions)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value->TryGetObject(Entry))
            {
                OutError = TEXT("Every actions entry must be a JSON object.");
                return false;
            }
            FDesiredAction Desired;
            if (!ReadDesiredAction(*Entry, Desired, OutError))
            {
                return false;
            }
            DesiredActions.Add(Desired);
        }
    }
    if (bHasAxes)
    {
        for (const TSharedPtr<FJsonValue>& Value : *RawAxes)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value->TryGetObject(Entry))
            {
                OutError = TEXT("Every axes entry must be a JSON object.");
                return false;
            }
            FDesiredAxis Desired;
            if (!ReadDesiredAxis(*Entry, Desired, OutError))
            {
                return false;
            }
            DesiredAxes.Add(Desired);
        }
    }

    // remove_unlisted against an empty desired set would erase the project's
    // entire input configuration from a request that named nothing. That is
    // never what a caller meant, so it is a refusal rather than an obedience.
    if (bRemoveUnlisted && !bHasActions && !bHasAxes)
    {
        OutError = TEXT(
            "remove_unlisted needs a desired set to be unlisted against. Supply 'actions', 'axes', "
            "or both; a bare remove_unlisted would delete every mapping in the project.");
        return false;
    }

    // ---- Snapshot before anything is classified against it. ----
    const TArray<FInputActionKeyMapping> ActionsBefore = Settings->GetActionMappings();
    const TArray<FInputAxisKeyMapping> AxesBefore = Settings->GetAxisMappings();
    const FString PreHash = MappingHash(ActionsBefore, AxesBefore);

    // ---- Classify. ----
    TArray<FDesiredAction> ActionsToAdd;
    TArray<FDesiredAxis> AxesToAdd;
    TArray<FInputActionKeyMapping> ActionsToRemove;
    TArray<FInputAxisKeyMapping> AxesToRemove;
    TArray<FString> Unchanged;

    for (const FDesiredAction& Desired : DesiredActions)
    {
        if (ActionsBefore.Contains(Desired.Mapping))
        {
            Unchanged.Add(ActionLine(Desired.Mapping));
        }
        else
        {
            ActionsToAdd.Add(Desired);
        }
    }
    for (const FDesiredAxis& Desired : DesiredAxes)
    {
        if (AxesBefore.Contains(Desired.Mapping))
        {
            Unchanged.Add(AxisLine(Desired.Mapping));
        }
        else
        {
            AxesToAdd.Add(Desired);
        }
    }

    // Explicit removals: {name} alone removes every key bound to that name,
    // which is the legacy input_mapping_remove contract.
    const TArray<TSharedPtr<FJsonValue>>* RawRemoveActions = nullptr;
    if (Spec->TryGetArrayField(TEXT("remove_actions"), RawRemoveActions))
    {
        for (const TSharedPtr<FJsonValue>& Value : *RawRemoveActions)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value->TryGetObject(Entry))
            {
                OutError = TEXT("Every remove_actions entry must be a JSON object.");
                return false;
            }
            FString Name;
            FString KeyName;
            if (!(*Entry)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
            {
                OutError = TEXT("Every remove_actions entry needs a non-empty 'name'.");
                return false;
            }
            (*Entry)->TryGetStringField(TEXT("key"), KeyName);
            for (const FInputActionKeyMapping& Mapping : ActionsBefore)
            {
                if (Mapping.ActionName.ToString() != Name)
                {
                    continue;
                }
                if (!KeyName.IsEmpty() && Mapping.Key.ToString() != KeyName)
                {
                    continue;
                }
                ActionsToRemove.AddUnique(Mapping);
            }
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* RawRemoveAxes = nullptr;
    if (Spec->TryGetArrayField(TEXT("remove_axes"), RawRemoveAxes))
    {
        for (const TSharedPtr<FJsonValue>& Value : *RawRemoveAxes)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value->TryGetObject(Entry))
            {
                OutError = TEXT("Every remove_axes entry must be a JSON object.");
                return false;
            }
            FString Name;
            FString KeyName;
            if (!(*Entry)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
            {
                OutError = TEXT("Every remove_axes entry needs a non-empty 'name'.");
                return false;
            }
            (*Entry)->TryGetStringField(TEXT("key"), KeyName);
            for (const FInputAxisKeyMapping& Mapping : AxesBefore)
            {
                if (Mapping.AxisName.ToString() != Name)
                {
                    continue;
                }
                if (!KeyName.IsEmpty() && Mapping.Key.ToString() != KeyName)
                {
                    continue;
                }
                AxesToRemove.AddUnique(Mapping);
            }
        }
    }

    // Downward convergence: only within the halves the caller actually stated,
    // so a spec that lists axes and no actions never prunes actions.
    if (bRemoveUnlisted)
    {
        if (bHasActions)
        {
            for (const FInputActionKeyMapping& Mapping : ActionsBefore)
            {
                const bool bWanted = DesiredActions.ContainsByPredicate(
                    [&Mapping](const FDesiredAction& Desired) { return Desired.Mapping == Mapping; });
                if (!bWanted)
                {
                    ActionsToRemove.AddUnique(Mapping);
                }
            }
        }
        if (bHasAxes)
        {
            for (const FInputAxisKeyMapping& Mapping : AxesBefore)
            {
                const bool bWanted = DesiredAxes.ContainsByPredicate(
                    [&Mapping](const FDesiredAxis& Desired) { return Desired.Mapping == Mapping; });
                if (!bWanted)
                {
                    AxesToRemove.AddUnique(Mapping);
                }
            }
        }
    }

    const int32 ExpectedChangeCount =
        ActionsToAdd.Num() + AxesToAdd.Num() + ActionsToRemove.Num() + AxesToRemove.Num();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> AddedJson;
    TArray<TSharedPtr<FJsonValue>> RemovedJson;
    for (const FDesiredAction& Desired : ActionsToAdd)
    {
        AddedJson.Add(MakeShared<FJsonValueObject>(ActionToJson(Desired.Mapping).ToSharedRef()));
    }
    for (const FDesiredAxis& Desired : AxesToAdd)
    {
        AddedJson.Add(MakeShared<FJsonValueObject>(AxisToJson(Desired.Mapping).ToSharedRef()));
    }
    for (const FInputActionKeyMapping& Mapping : ActionsToRemove)
    {
        RemovedJson.Add(MakeShared<FJsonValueObject>(ActionToJson(Mapping).ToSharedRef()));
    }
    for (const FInputAxisKeyMapping& Mapping : AxesToRemove)
    {
        RemovedJson.Add(MakeShared<FJsonValueObject>(AxisToJson(Mapping).ToSharedRef()));
    }
    Unchanged.Sort();
    Result->SetBoolField(TEXT("plan_only"), bPlanOnly);
    Result->SetBoolField(TEXT("remove_unlisted"), bRemoveUnlisted);
    Result->SetArrayField(TEXT("mappings_to_add"), AddedJson);
    Result->SetArrayField(TEXT("mappings_to_remove"), RemovedJson);
    Result->SetArrayField(TEXT("unchanged_operations"), StringValues(Unchanged));
    Result->SetNumberField(TEXT("expected_change_count"), ExpectedChangeCount);
    Result->SetStringField(TEXT("pre_mapping_hash_sha1"), PreHash);
    Result->SetStringField(TEXT("saved_to"), TEXT("Config/DefaultInput.ini"));

    if (bPlanOnly)
    {
        Result->SetNumberField(TEXT("applied_change_count"), 0);
        // A no-op batch is the one case where the resulting hash is knowable
        // without applying anything: it is the current hash by definition.
        if (ExpectedChangeCount == 0)
        {
            Result->SetStringField(TEXT("predicted_mapping_hash_sha1"), PreHash);
        }
        else
        {
            Result->SetStringField(TEXT("prediction_unavailable_reason"),
                TEXT("A batch that changes mappings cannot be hashed without applying it."));
        }
        Result->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
        OutResultJson = SerializeObject(Result);
        return true;
    }

    // ---- Apply. Every write goes through UMCPBridgeInputLibrary, so the
    // library's own key validation and duplicate rejection stay the single
    // authority on what a valid mapping is. ----
    TArray<TSharedPtr<FJsonValue>> Errors;
    for (const FInputActionKeyMapping& Mapping : ActionsToRemove)
    {
        Settings->RemoveActionMapping(Mapping, false);
    }
    for (const FInputAxisKeyMapping& Mapping : AxesToRemove)
    {
        Settings->RemoveAxisMapping(Mapping, false);
    }
    for (const FDesiredAction& Desired : ActionsToAdd)
    {
        const FString Failure = UMCPBridgeInputLibrary::AddActionMapping(
            Desired.Mapping.ActionName.ToString(), Desired.KeyName,
            Desired.Mapping.bShift, Desired.Mapping.bCtrl, Desired.Mapping.bAlt, Desired.Mapping.bCmd);
        if (!Failure.IsEmpty())
        {
            Errors.Add(MakeShared<FJsonValueString>(
                FString::Printf(TEXT("add action %s=%s: %s"),
                    *Desired.Mapping.ActionName.ToString(), *Desired.KeyName, *Failure)));
        }
    }
    for (const FDesiredAxis& Desired : AxesToAdd)
    {
        const FString Failure = UMCPBridgeInputLibrary::AddAxisMapping(
            Desired.Mapping.AxisName.ToString(), Desired.KeyName, Desired.Mapping.Scale);
        if (!Failure.IsEmpty())
        {
            Errors.Add(MakeShared<FJsonValueString>(
                FString::Printf(TEXT("add axis %s=%s: %s"),
                    *Desired.Mapping.AxisName.ToString(), *Desired.KeyName, *Failure)));
        }
    }
    Settings->ForceRebuildKeymaps();
    Settings->SaveKeyMappings();

    // ---- Verify by reading, not by believing the writes. ----
    TArray<FInputActionKeyMapping> ActionsAfter = Settings->GetActionMappings();
    TArray<FInputAxisKeyMapping> AxesAfter = Settings->GetAxisMappings();
    for (const FDesiredAction& Desired : ActionsToAdd)
    {
        if (!ActionsAfter.Contains(Desired.Mapping))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("verify: action %s=%s is not present after the write."),
                *Desired.Mapping.ActionName.ToString(), *Desired.KeyName)));
        }
    }
    for (const FDesiredAxis& Desired : AxesToAdd)
    {
        if (!AxesAfter.Contains(Desired.Mapping))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("verify: axis %s=%s is not present after the write."),
                *Desired.Mapping.AxisName.ToString(), *Desired.KeyName)));
        }
    }
    for (const FInputActionKeyMapping& Mapping : ActionsToRemove)
    {
        if (ActionsAfter.Contains(Mapping))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("verify: action %s=%s is still present after the removal."),
                *Mapping.ActionName.ToString(), *Mapping.Key.ToString())));
        }
    }
    for (const FInputAxisKeyMapping& Mapping : AxesToRemove)
    {
        if (AxesAfter.Contains(Mapping))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("verify: axis %s=%s is still present after the removal."),
                *Mapping.AxisName.ToString(), *Mapping.Key.ToString())));
        }
    }

    if (Errors.Num() > 0)
    {
        RestoreMappings(Settings, ActionsBefore, AxesBefore);
        const FString RolledBackHash = MappingHash(
            Settings->GetActionMappings(), Settings->GetAxisMappings());
        Result->SetNumberField(TEXT("applied_change_count"), 0);
        Result->SetStringField(TEXT("post_mapping_hash_sha1"), RolledBackHash);
        // Reported from a re-read, never from the fact that the restore ran.
        Result->SetBoolField(TEXT("rollback_succeeded"), RolledBackHash == PreHash);
        Result->SetArrayField(TEXT("errors"), Errors);
        OutResultJson = SerializeObject(Result);
        OutError = TEXT("input_mapping_patch failed and the previous mappings were restored.");
        return false;
    }

    ActionsAfter.Sort([](const FInputActionKeyMapping& A, const FInputActionKeyMapping& B)
    {
        return ActionLine(A) < ActionLine(B);
    });
    AxesAfter.Sort([](const FInputAxisKeyMapping& A, const FInputAxisKeyMapping& B)
    {
        return AxisLine(A) < AxisLine(B);
    });
    TArray<TSharedPtr<FJsonValue>> ActionJson;
    for (const FInputActionKeyMapping& Mapping : ActionsAfter)
    {
        ActionJson.Add(MakeShared<FJsonValueObject>(ActionToJson(Mapping).ToSharedRef()));
    }
    TArray<TSharedPtr<FJsonValue>> AxisJson;
    for (const FInputAxisKeyMapping& Mapping : AxesAfter)
    {
        AxisJson.Add(MakeShared<FJsonValueObject>(AxisToJson(Mapping).ToSharedRef()));
    }
    Result->SetNumberField(TEXT("applied_change_count"), ExpectedChangeCount);
    Result->SetStringField(TEXT("post_mapping_hash_sha1"), MappingHash(ActionsAfter, AxesAfter));
    Result->SetBoolField(TEXT("converged"), ExpectedChangeCount == 0);
    Result->SetArrayField(TEXT("action_mappings"), ActionJson);
    Result->SetArrayField(TEXT("axis_mappings"), AxisJson);
    Result->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
    OutResultJson = SerializeObject(Result);
    return true;
}

bool UMCPPuerTSBridgeService::SetFolderVisibilityJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const TSharedPtr<FJsonObject> Spec = ParseRequest(SpecJson);
    if (!Spec.IsValid())
    {
        OutError = TEXT("folder_visibility spec is not a JSON object.");
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* RawHidden = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* RawHide = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* RawShow = nullptr;
    const bool bHasHidden = Spec->TryGetArrayField(TEXT("hidden"), RawHidden);
    const bool bHasHide = Spec->TryGetArrayField(TEXT("hide"), RawHide);
    const bool bHasShow = Spec->TryGetArrayField(TEXT("show"), RawShow);
    if (bHasHidden && (bHasHide || bHasShow))
    {
        OutError = TEXT(
            "'hidden' is the whole desired set and 'hide'/'show' are a delta against the current "
            "one. A request carrying both has no single correct reading, so it is refused rather "
            "than resolved by a rule the caller cannot see. Send one shape or the other.");
        return false;
    }
    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));

    auto ReadPaths = [](const TArray<TSharedPtr<FJsonValue>>* Raw, TArray<FString>& Out, FString& Error)
    {
        if (Raw == nullptr)
        {
            return true;
        }
        for (const TSharedPtr<FJsonValue>& Value : *Raw)
        {
            FString Path;
            if (!Value->TryGetString(Path) || Path.IsEmpty())
            {
                Error = TEXT("Every folder entry must be a non-empty string path under /Game.");
                return false;
            }
            Out.AddUnique(Path);
        }
        return true;
    };

    TArray<FString> DesiredHidden;
    TArray<FString> RequestedHide;
    TArray<FString> RequestedShow;
    if (!ReadPaths(bHasHidden ? RawHidden : nullptr, DesiredHidden, OutError)
        || !ReadPaths(bHasHide ? RawHide : nullptr, RequestedHide, OutError)
        || !ReadPaths(bHasShow ? RawShow : nullptr, RequestedShow, OutError))
    {
        return false;
    }

    const TArray<FString> Before = UFolderVisibilityLibrary::GetHiddenFolders();
    TArray<FString> ToHide;
    TArray<FString> ToShow;
    if (bHasHidden)
    {
        for (const FString& Path : DesiredHidden)
        {
            if (!Before.Contains(Path))
            {
                ToHide.Add(Path);
            }
        }
        for (const FString& Path : Before)
        {
            if (!DesiredHidden.Contains(Path))
            {
                ToShow.Add(Path);
            }
        }
    }
    else
    {
        for (const FString& Path : RequestedHide)
        {
            if (!Before.Contains(Path))
            {
                ToHide.Add(Path);
            }
        }
        for (const FString& Path : RequestedShow)
        {
            if (Before.Contains(Path))
            {
                ToShow.Add(Path);
            }
        }
    }

    TArray<FString> Unchanged;
    for (const FString& Path : RequestedHide)
    {
        if (Before.Contains(Path))
        {
            Unchanged.Add(Path);
        }
    }
    for (const FString& Path : RequestedShow)
    {
        if (!Before.Contains(Path))
        {
            Unchanged.Add(Path);
        }
    }
    if (bHasHidden)
    {
        for (const FString& Path : DesiredHidden)
        {
            if (Before.Contains(Path))
            {
                Unchanged.Add(Path);
            }
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("plan_only"), bPlanOnly);
    Result->SetStringField(TEXT("mode"), bHasHidden ? TEXT("desired_set") : TEXT("delta"));
    Result->SetArrayField(TEXT("folders_to_hide"), StringValues(ToHide));
    Result->SetArrayField(TEXT("folders_to_show"), StringValues(ToShow));
    Result->SetArrayField(TEXT("unchanged"), StringValues(Unchanged));
    Result->SetNumberField(TEXT("expected_change_count"), ToHide.Num() + ToShow.Num());
    Result->SetArrayField(TEXT("hidden_before"), StringValues(Before));
    Result->SetStringField(TEXT("persisted_to"), TEXT("Config/FolderVisibility.ini"));

    TArray<TSharedPtr<FJsonValue>> Errors;
    int32 Applied = 0;
    if (!bPlanOnly)
    {
        for (const FString& Path : ToHide)
        {
            if (UFolderVisibilityLibrary::HideFolder(Path))
            {
                ++Applied;
            }
            else
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("hide %s: rejected. A hidden folder must be a subfolder of /Game; /Game "
                         "itself cannot be hidden."), *Path)));
            }
        }
        for (const FString& Path : ToShow)
        {
            if (UFolderVisibilityLibrary::ShowFolder(Path))
            {
                ++Applied;
            }
            else
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("show %s: rejected. It is not on the hidden list this feature owns."), *Path)));
            }
        }
    }

    // Read back rather than echo: the hidden set reported is the one
    // GetHiddenFolders answers with after the work.
    const TArray<FString> After = UFolderVisibilityLibrary::GetHiddenFolders();
    Result->SetNumberField(TEXT("applied_change_count"), Applied);
    Result->SetArrayField(TEXT("hidden"), StringValues(After));
    Result->SetBoolField(TEXT("converged"), ToHide.Num() + ToShow.Num() == 0);
    Result->SetArrayField(TEXT("errors"), Errors);
    OutResultJson = SerializeObject(Result);
    if (Errors.Num() > 0)
    {
        OutError = TEXT("folder_visibility could not apply every change; see errors.");
        return false;
    }
    return true;
}

bool UMCPPuerTSBridgeService::PlayCameraShakeJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError) const
{
    const TSharedPtr<FJsonObject> Spec = ParseRequest(SpecJson);
    if (!Spec.IsValid())
    {
        OutError = TEXT("camera_shake spec is not a JSON object.");
        return false;
    }
    FString ShakeClass;
    if (!Spec->TryGetStringField(TEXT("shake_class"), ShakeClass) || ShakeClass.IsEmpty())
    {
        OutError = TEXT("camera_shake needs 'shake_class': the path to a CameraShake Blueprint.");
        return false;
    }
    double Scale = 1.0;
    if (Spec->HasTypedField<EJson::Number>(TEXT("scale")))
    {
        Scale = Spec->GetNumberField(TEXT("scale"));
    }
    if (Scale <= 0.0)
    {
        OutError = FString::Printf(
            TEXT("camera_shake scale must be greater than zero; got %f. A zero scale plays a shake "
                 "with no amplitude, which is indistinguishable from a shake that did not play."),
            Scale);
        return false;
    }
    if (!UAutoPIEHelper::IsPIERunning())
    {
        OutError = TEXT(
            "No PIE session is running, and a camera shake needs a player camera to shake. Start "
            "Play In Editor first (puerts_pie_start). This is refused rather than reported as a "
            "shake nobody could have seen.");
        return false;
    }

    const bool bPlayed = UAutoPIEHelper::PlayCameraShakeOnPlayer(ShakeClass, static_cast<float>(Scale));
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("shake_class"), ShakeClass);
    Result->SetNumberField(TEXT("scale"), Scale);
    Result->SetBoolField(TEXT("played"), bPlayed);
    Result->SetBoolField(TEXT("persisted"), false);
    OutResultJson = SerializeObject(Result);
    if (!bPlayed)
    {
        OutError = FString::Printf(
            TEXT("Could not play '%s'. The path must resolve to a CameraShake class (a Blueprint "
                 "path usually ends in _C), and PlayerController 0 must have a PlayerCameraManager."),
            *ShakeClass);
        return false;
    }
    return true;
}

bool UMCPPuerTSBridgeService::QueryPIEAgentJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    const TSharedPtr<FJsonObject> Request = ParseRequest(RequestJson);
    if (!Request.IsValid())
    {
        OutError = TEXT("pie_agent_query request is not a JSON object.");
        return false;
    }
    FString Op;
    if (!Request->TryGetStringField(TEXT("op"), Op) || Op.IsEmpty())
    {
        OutError = TEXT("pie_agent_query needs 'op': observe, status or expect.");
        return false;
    }

    // The agent's own request shape is whatever the caller sent minus the
    // discriminator, so the library keeps ownership of its parameters and this
    // command never has to restate them in a second place.
    TSharedPtr<FJsonObject> Forward = MakeShared<FJsonObject>(*Request);
    Forward->RemoveField(TEXT("op"));
    const FString ForwardJson = SerializeObject(Forward);

    if (Op == TEXT("observe"))
    {
        OutResultJson = UPIEAgentLibrary::Observe(ForwardJson);
        return true;
    }
    if (Op == TEXT("expect"))
    {
        OutResultJson = UPIEAgentLibrary::StartExpect(ForwardJson);
        return true;
    }
    if (Op == TEXT("status"))
    {
        double OperationId = 0.0;
        if (Request->HasTypedField<EJson::Number>(TEXT("operation_id")))
        {
            OperationId = Request->GetNumberField(TEXT("operation_id"));
        }
        OutResultJson = UPIEAgentLibrary::GetOperationStatus(static_cast<int32>(OperationId));
        return true;
    }

    OutError = FString::Printf(
        TEXT("Unknown pie_agent_query op '%s'. This command carries the READ-ONLY half of the PIE ")
        TEXT("agent: observe, status, expect. The write half (move_to, look_at, press, ")
        TEXT("record_start, record_stop, replay) is not exposed natively yet and is user-gated ")
        TEXT("by AGENTS.md in any case."),
        *Op);
    return false;
}
