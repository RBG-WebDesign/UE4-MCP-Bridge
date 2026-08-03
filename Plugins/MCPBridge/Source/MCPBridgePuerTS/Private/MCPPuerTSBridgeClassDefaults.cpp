// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "MCPBridgeAssetRollback.h"

#include "Engine/Blueprint.h"
#include "JsonObjectConverter.h"
#include "Json.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
    FString ClassDefaultValueToJsonText(const TSharedPtr<FJsonValue>& Value)
    {
        if (!Value.IsValid()) { return FString(); }
        FString Text;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
        FJsonSerializer::Serialize(Value, TEXT(""), Writer);
        return Text;
    }

    /** The exported text of one property's CURRENT value on an object. This is
        the single representation this command compares with, snapshots and
        restores, so a value cannot look changed to one step and unchanged to
        another. */
    bool ExportPropertyText(const FProperty* Property, const UObject* Object, FString& OutText)
    {
        const void* Address = Property->ContainerPtrToValuePtr<void>(Object);
        if (Address == nullptr) { return false; }
        OutText.Reset();
        Property->ExportTextItem(OutText, Address, nullptr, nullptr, PPF_SerializedAsImportText);
        return true;
    }

    /**
     * The exported text the REQUESTED value would produce, computed on a scratch
     * copy of the property rather than on the object.
     *
     * Two things fall out of doing it this way, and both are the reason it is
     * done this way. An invalid value is rejected here, before the class default
     * object is touched at all, so a bad request never reaches the mutation
     * pass. And convergence is decided by comparing two strings produced by the
     * SAME import-and-export path, rather than by a hand-written JSON
     * comparator with its own idea of when two numbers or two object references
     * are equal - the failure that finding 0o records one asset kind over.
     */
    bool ExportRequestedText(
        const FProperty* Property,
        const TSharedPtr<FJsonValue>& Desired,
        FString& OutText)
    {
        void* Scratch = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
        Property->InitializeValue(Scratch);
        const bool bConverted = FJsonObjectConverter::JsonValueToUProperty(
            Desired, const_cast<FProperty*>(Property), Scratch);
        if (bConverted)
        {
            OutText.Reset();
            Property->ExportTextItem(OutText, Scratch, nullptr, nullptr, PPF_SerializedAsImportText);
        }
        Property->DestroyValue(Scratch);
        FMemory::Free(Scratch);
        return bConverted;
    }

    /** Property names on this class closest to one that was not found, so a
        typo is answered with the fix rather than with "not found". */
    FString NearestPropertyNames(const UClass* Class, const FString& Requested)
    {
        TArray<FString> Near;
        for (TFieldIterator<FProperty> It(Class); It && Near.Num() < 8; ++It)
        {
            const FString Name = It->GetName();
            if (Name.Contains(Requested) || Requested.Contains(Name)) { Near.Add(Name); }
        }
        return Near.Num() > 0 ? FString::Join(Near, TEXT(", ")) : FString(TEXT("none close"));
    }

    struct FClassDefaultOp
    {
        FString Name;
        FProperty* Property = nullptr;
        TSharedPtr<FJsonValue> Desired;
        FString RequestedText;
        FString CurrentText;
        bool bSatisfied = false;
    };
}

bool UMCPPuerTSBridgeService::PatchClassDefaultsJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Class defaults patch spec must be a JSON object.");
        return false;
    }

    FString AssetPath;
    if (!Spec->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutError = TEXT("asset_path is required.");
        return false;
    }
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/")) || AssetPath.Contains(TEXT("..")))
    {
        OutError = TEXT("Blueprint assets are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    const bool bVerify = !Spec->HasTypedField<EJson::Boolean>(TEXT("verify"))
        || Spec->GetBoolField(TEXT("verify"));
    const bool bSave = !Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        || Spec->GetBoolField(TEXT("save"));

    const TSharedPtr<FJsonObject>* Properties = nullptr;
    if (!Spec->TryGetObjectField(TEXT("properties"), Properties) || Properties == nullptr)
    {
        OutError = TEXT("properties must be an object of {propertyName: value}.");
        return false;
    }
    if ((*Properties)->Values.Num() == 0)
    {
        OutError = TEXT("properties must name at least one property; a patch that names none is not a request.");
        return false;
    }

    // Patching never creates an asset, for the reason blueprint_member_patch
    // gives: a patch against something that is not there is a mistake in the
    // request, and creating a Blueprint to receive it turns that mistake into an
    // asset nobody asked for.
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *FPackageName::GetShortName(AssetPath));
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
    if (Blueprint == nullptr)
    {
        OutError = FString::Printf(
            TEXT("No Blueprint at %s. Patching changes an existing Blueprint and never creates one; "
                 "use blueprint_build to author the asset first."), *AssetPath);
        return false;
    }
    UClass* GeneratedClass = Blueprint->GeneratedClass;
    UObject* CDO = GeneratedClass != nullptr ? GeneratedClass->GetDefaultObject(false) : nullptr;
    if (CDO == nullptr)
    {
        OutError = FString::Printf(
            TEXT("%s has no generated class default object. A Blueprint that has never compiled has "
                 "no class defaults to patch; compile it first."), *AssetPath);
        return false;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("class_path"), GeneratedClass->GetPathName());
    Result->SetStringField(TEXT("class_default_object"), CDO->GetPathName());

    UPackage* Package = Blueprint->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);

    // --- Pass 1: resolve, validate and REFUSE, mutating nothing. ---
    TArray<FClassDefaultOp> Resolved;
    TArray<FString> Refusals;

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
    {
        FClassDefaultOp Op;
        Op.Name = Pair.Key;
        Op.Desired = Pair.Value;

        Op.Property = FindFProperty<FProperty>(CDO->GetClass(), *Op.Name);
        if (Op.Property == nullptr)
        {
            Refusals.Add(FString::Printf(
                TEXT("'%s' is not a reflected property of %s. Closest names: %s."),
                *Op.Name, *CDO->GetClass()->GetName(), *NearestPropertyNames(CDO->GetClass(), Op.Name)));
            continue;
        }

        // A Blueprint's OWN variable has a second home: FBPVariableDescription
        // carries its default too, and writing only the CDO would leave the two
        // disagreeing until the next compile picked one. That field belongs to
        // blueprint_member_patch's set_variable_default, which writes both.
        const bool bOwnVariable = Blueprint->NewVariables.ContainsByPredicate(
            [&](const FBPVariableDescription& Description){ return Description.VarName.ToString() == Op.Name; });
        if (bOwnVariable)
        {
            Refusals.Add(FString::Printf(
                TEXT("'%s' is a member variable this Blueprint declares, not an inherited class "
                     "default. Its default lives on the variable description as well as on the CDO, "
                     "and only blueprint_member_patch (set_variable_default) writes both."), *Op.Name));
            continue;
        }

        if (!IsWritablePropertyAllowed(CDO, Op.Name))
        {
            Refusals.Add(FString::Printf(
                TEXT("'%s' is not on the writable-property allowlist. Widening it is a deliberate "
                     "edit to AllowedWritableProperties in [MCPPuerTSBridge], not something a "
                     "request may do."), *Op.Name));
            continue;
        }

        if (!ExportRequestedText(Op.Property, Op.Desired, Op.RequestedText))
        {
            Refusals.Add(FString::Printf(
                TEXT("the value given for '%s' (%s) is not valid for a %s. Nothing was written."),
                *Op.Name, *ClassDefaultValueToJsonText(Op.Desired), *Op.Property->GetClass()->GetName()));
            continue;
        }
        if (!ExportPropertyText(Op.Property, CDO, Op.CurrentText))
        {
            Refusals.Add(FString::Printf(TEXT("'%s' could not be read off the class default object."), *Op.Name));
            continue;
        }
        Op.bSatisfied = (Op.RequestedText == Op.CurrentText);
        Resolved.Add(Op);
    }

    TArray<TSharedPtr<FJsonValue>> ToApply;
    TArray<TSharedPtr<FJsonValue>> Unchanged;
    for (const FClassDefaultOp& Op : Resolved)
    {
        (Op.bSatisfied ? Unchanged : ToApply).Add(MakeShared<FJsonValueString>(Op.Name));
    }
    Result->SetArrayField(TEXT("properties_to_apply"), ToApply);
    Result->SetArrayField(TEXT("unchanged_properties"), Unchanged);
    Result->SetNumberField(TEXT("expected_change_count"), ToApply.Num());

    auto Answer = [&](bool bSuccess) -> bool
    {
        Result->SetBoolField(TEXT("package_dirty_after"), Package != nullptr && Package->IsDirty());
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        return bSuccess;
    };

    if (Refusals.Num() > 0)
    {
        OutError = FString::Join(Refusals, TEXT(" "));
        return Answer(false);
    }
    if (bPlanOnly)
    {
        Result->SetBoolField(TEXT("plan_only"), true);
        return Answer(true);
    }
    if (ToApply.Num() == 0)
    {
        // Convergent: nothing is written, nothing is dirtied, nothing is saved.
        Result->SetBoolField(TEXT("converged"), true);
        Result->SetNumberField(TEXT("applied_property_count"), 0);
        Result->SetBoolField(TEXT("saved"), false);
        return Answer(true);
    }
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Class default patching requires an active transaction.");
        return false;
    }

    // --- The boundary, and the reason this command owns one at all.
    //
    // Finding 0r, measured rather than assumed: UObject::Modify delegates to
    // SaveToTransactionBuffer, which silently does nothing for an object that is
    // not RF_Transactional, and a class default object is not. A cancelled
    // transaction therefore leaves a CDO write standing. So the transaction is
    // NOT the rollback here: the exported text of every property this command is
    // about to touch is snapshotted first, restored on any failure, and whether
    // the restore worked is decided by reading the values again.
    //
    // Modify()'s return value is kept rather than discarded, and reported as
    // transaction_covers_cdo, so the next author who assumes a transaction
    // covers a CDO write finds out from the response instead of from an
    // acceptance three months later.
    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    TMap<FString, FString> TextBefore;
    for (const FClassDefaultOp& Op : Resolved) { TextBefore.Add(Op.Name, Op.CurrentText); }

    const bool bTransactionCoversCDO = CDO->Modify();
    Result->SetBoolField(TEXT("transaction_covers_cdo"), bTransactionCoversCDO);
    if (!bTransactionCoversCDO)
    {
        Result->SetStringField(TEXT("transaction_note"),
            TEXT("CDO->Modify() returned false: this class default object did not reach the undo "
                 "buffer, so editor undo will not revert this write. The command's own "
                 "snapshot-and-restore boundary is what makes a failure atomic, and Ctrl+Z in the "
                 "editor is not a way to take this change back. Finding 0r."));
    }

    auto RestoreDefaults = [&]() -> int32
    {
        int32 Restored = 0;
        for (const FClassDefaultOp& Op : Resolved)
        {
            const FString* Old = TextBefore.Find(Op.Name);
            if (Old == nullptr) { continue; }
            FString Current;
            if (!ExportPropertyText(Op.Property, CDO, Current) || Current == *Old) { continue; }
            // Modify(false), not Modify(). This runs on the FAILURE path, whose
            // contract is that the asset comes back exactly as the patch found
            // it, and the default overload marks the package dirty - trading a
            // wrong value for a wrong dirty flag.
            CDO->Modify(false);
            void* Address = Op.Property->ContainerPtrToValuePtr<void>(CDO);
            Op.Property->ImportText(**Old, Address, PPF_SerializedAsImportText, CDO);
            ++Restored;
        }
        return Restored;
    };

    auto FailWithRollback = [&](const FString& Error) -> bool
    {
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }
        Rollback.Rollback();
        const int32 RestoredCount = RestoreDefaults();

        // Trust the read-back, not the undo, and not the restore either.
        TArray<FString> StillWrong;
        for (const FClassDefaultOp& Op : Resolved)
        {
            FString Current;
            const FString* Old = TextBefore.Find(Op.Name);
            if (Old != nullptr && ExportPropertyText(Op.Property, CDO, Current) && Current != *Old)
            {
                StillWrong.Add(FString::Printf(TEXT("%s is %s, expected %s"), *Op.Name, *Current, **Old));
            }
        }
        const bool bRollbackSucceeded = StillWrong.Num() == 0;
        Result->SetBoolField(TEXT("rollback_attempted"), true);
        Result->SetBoolField(TEXT("rollback_succeeded"), bRollbackSucceeded);
        Result->SetNumberField(TEXT("restored_property_count"), RestoredCount);
        Result->SetObjectField(TEXT("cleanup"), Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
        OutError = bRollbackSucceeded
            ? Error
            : FString::Printf(
                TEXT("%s The rollback did NOT restore the class defaults: %s. Treat this asset as "
                     "damaged and do not save it."), *Error, *FString::Join(StillWrong, TEXT("; ")));
        return Answer(false);
    };

    // --- Apply. ---
    TArray<TSharedPtr<FJsonValue>> Applied;
    for (const FClassDefaultOp& Op : Resolved)
    {
        if (Op.bSatisfied) { continue; }
        FString WrittenPath;
        FString WriteError;
        const FString ValueJson = FString::Printf(
            TEXT("{\"value\":%s}"), *ClassDefaultValueToJsonText(Op.Desired));
        if (!SetObjectPropertyJson(CDO, Op.Name, ValueJson, WrittenPath, WriteError))
        {
            return FailWithRollback(FString::Printf(
                TEXT("could not set '%s' on the class default object: %s"), *Op.Name, *WriteError));
        }
        Applied.Add(MakeShared<FJsonValueString>(Op.Name));
    }
    Result->SetArrayField(TEXT("applied_properties"), Applied);
    Result->SetNumberField(TEXT("applied_property_count"), Applied.Num());
    Result->SetBoolField(TEXT("converged"), false);

    // --- Verify by reading the class default object again. ---
    TArray<TSharedPtr<FJsonValue>> Mismatches;
    if (bVerify)
    {
        for (const FClassDefaultOp& Op : Resolved)
        {
            FString Current;
            if (!ExportPropertyText(Op.Property, CDO, Current) || Current != Op.RequestedText)
            {
                Mismatches.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("%s reads back as %s after the patch, not the requested %s."),
                    *Op.Name, *Current, *Op.RequestedText)));
            }
        }
    }
    Result->SetArrayField(TEXT("verification_mismatches"), Mismatches);
    if (Mismatches.Num() > 0)
    {
        return FailWithRollback(FString::Printf(
            TEXT("%d class default(s) did not read back as requested, so the patch is rolled back."),
            Mismatches.Num()));
    }

    // Saved only after the read-back agreed. An unsaved CDO edit is still a real
    // edit in this editor session, which is why the dirty flag is reported
    // either way.
    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(AssetPath, SaveError);
        if (!bSaved)
        {
            Result->SetStringField(TEXT("save_error"), SaveError);
        }
    }
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetStringField(TEXT("instance_note"),
        TEXT("This writes the CLASS default. Actors already placed in a level keep the value they "
             "were placed with; use scene_batch to change those."));
    Rollback.Commit();
    Result->SetObjectField(TEXT("cleanup"), Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
    return Answer(true);
}
