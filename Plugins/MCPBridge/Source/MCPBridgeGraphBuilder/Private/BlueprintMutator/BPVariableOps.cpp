// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPVariableOps.h"
#include "BlueprintMutatorLibrary.h"
#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "../BlueprintInspector/BPTypeDescriptor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Json.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "BPVariableOps"

namespace
{
    /** After a full compile, KismetCompiler::CreateClassVariablesFromBlueprint empties
     *  FBPVariableDescription::DefaultValue (having already copied the value into the CDO).
     *  Syncs the description field back from the CDO so downstream readers (inspector,
     *  editor UI) still see the current default. Matches what
     *  FBlueprintEditorUtils::DuplicateVariableDescription does on BP duplication. */
    /** The JSON-to-ImportText rule now lives on UBlueprintMutatorLibrary, so
     *  the command layer deciding whether a default is ALREADY the requested
     *  one applies the identical rule. */
    FString JsonDefaultToImportText(const FString& DefaultValueJson, const bool bIsTextLike)
    {
        return UBlueprintMutatorLibrary::JsonDefaultToImportText(DefaultValueJson, bIsTextLike);
    }

    void SyncDefaultValueFromCDO(UBlueprint* Blueprint, const FName& VarFName)
    {
        if (!Blueprint) return;
        FString Exported;
        if (!UBlueprintMutatorLibrary::TryReadVariableDefaultFromCDO(Blueprint, VarFName, Exported)) return;
        const int32 Idx = Blueprint->NewVariables.IndexOfByPredicate(
            [&](const FBPVariableDescription& D){ return D.VarName == VarFName; });
        if (Idx == INDEX_NONE) return;
        Blueprint->NewVariables[Idx].DefaultValue = Exported;
    }
}

bool FBPVariableOps::AddVariable(UBlueprint* Blueprint, const FString& VarName, const FString& TypeJson, const FString& DefaultValueJson, const FString& Category)
{
    if (!Blueprint) { UE_LOG(LogBlueprintMutator, Warning, TEXT("AddVariable: null blueprint")); return false; }
    if (VarName.IsEmpty()) { UE_LOG(LogBlueprintMutator, Warning, TEXT("AddVariable: empty VarName")); return false; }

    const FName VarFName(*VarName);
    for (const FBPVariableDescription& Existing : Blueprint->NewVariables)
    {
        if (Existing.VarName == VarFName)
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("AddVariable: '%s' already exists"), *VarName);
            return false;
        }
    }

    const FEdGraphPinType PinType = FBPTypeDescriptor::FromJsonString(TypeJson);
    if (PinType.PinCategory.IsNone())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("AddVariable: failed to parse TypeJson for '%s'"), *VarName);
        return false;
    }

    const bool bOk = FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("AddVariable", "Soul Juice: Add Variable"),
        [&]() -> bool
        {
            const bool bTextLikePin =
                PinType.PinCategory == TEXT("string") ||
                PinType.PinCategory == TEXT("name") ||
                PinType.PinCategory == TEXT("text");
            const FString ImportDefault = DefaultValueJson.IsEmpty()
                ? DefaultValueJson
                : JsonDefaultToImportText(DefaultValueJson, bTextLikePin);
            const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarFName, PinType, ImportDefault);
            if (!bAdded)
            {
                UE_LOG(LogBlueprintMutator, Error, TEXT("AddVariable: AddMemberVariable returned false for '%s'"), *VarName);
                return false;
            }
            if (!Category.IsEmpty())
            {
                FBlueprintEditorUtils::SetBlueprintVariableCategory(
                    Blueprint, VarFName, /*InStructScope=*/nullptr, FText::FromString(Category));
            }
            return true;
        });
    if (bOk && !DefaultValueJson.IsEmpty())
    {
        // A brand new variable has no compiled property until this mutation's
        // own compile has run, so this is the first moment the question "can
        // that type hold that value" can be asked at all. Asked late beats not
        // asked: the compiler imports an unreadable float default as 0.0, which
        // is indistinguishable from a requested zero once it is written.
        UClass* GenClass = Blueprint->GeneratedClass;
        UObject* CDO = GenClass != nullptr ? GenClass->GetDefaultObject(/*bCreateIfNeeded=*/false) : nullptr;
        FProperty* Prop = GenClass != nullptr ? FindFProperty<FProperty>(GenClass, VarFName) : nullptr;
        if (CDO != nullptr && Prop != nullptr)
        {
            const bool bIsTextLike = Prop->IsA<FStrProperty>() || Prop->IsA<FNameProperty>() || Prop->IsA<FTextProperty>();
            const FString ImportString = JsonDefaultToImportText(DefaultValueJson, bIsTextLike);
            FDefaultConstructedPropertyElement Scratch(Prop);
            if (!UBlueprintMutatorLibrary::ImportDefaultValue(Prop, ImportString, Scratch.GetObjAddress(), CDO))
            {
                UE_LOG(LogBlueprintMutator, Warning,
                    TEXT("AddVariable: '%s' is not a value %s can hold (variable '%s'). The check can "
                         "only run after the variable exists, so this fails AFTER the mutation and the "
                         "enclosing transaction owns undoing it."),
                    *ImportString, *Prop->GetClass()->GetName(), *VarName);
                return false;
            }
        }
    }
    if (bOk)
    {
        // RunMutation -> CompileBlueprint empties NewVariables[i].DefaultValue after copying
        // it into the CDO (KismetCompiler.cpp:783). Sync it back so inspector reads are stable.
        SyncDefaultValueFromCDO(Blueprint, VarFName);
    }
    return bOk;
}

bool FBPVariableOps::RemoveVariable(UBlueprint* Blueprint, const FString& VarName)
{
    if (!Blueprint) { UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveVariable: null blueprint")); return false; }
    if (VarName.IsEmpty()) return false;

    const FName VarFName(*VarName);
    const bool bHasVar = Blueprint->NewVariables.ContainsByPredicate(
        [&](const FBPVariableDescription& D){ return D.VarName == VarFName; });
    if (!bHasVar)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveVariable: '%s' not found"), *VarName);
        return false;
    }
    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("RemoveVariable", "Soul Juice: Remove Variable"),
        [&]() -> bool
        {
            FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VarFName);
            return true;
        });
}

bool FBPVariableOps::SetVariableDefault(UBlueprint* Blueprint, const FString& VarName, const FString& DefaultValueJson)
{
    if (!Blueprint) { UE_LOG(LogBlueprintMutator, Warning, TEXT("SetVariableDefault: null blueprint")); return false; }
    const FName VarFName(*VarName);
    const int32 VarIndex = Blueprint->NewVariables.IndexOfByPredicate(
        [&](const FBPVariableDescription& D){ return D.VarName == VarFName; });
    if (VarIndex == INDEX_NONE)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("SetVariableDefault: '%s' not found"), *VarName);
        return false;
    }
    const bool bOk = FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("SetVariableDefault", "Soul Juice: Set Variable Default"),
        [&]() -> bool
        {
            // The description string alone is not enough for a pre-existing
            // variable: recompiling copies the OLD CDO's values onto the new
            // CDO (user-edit preservation), so the string never wins. Write
            // the CDO directly, like the editor's Details panel does, before
            // the compile so the value survives the copy.
            UClass* GenClass = Blueprint->GeneratedClass;
            UObject* CDO = GenClass ? GenClass->GetDefaultObject(/*bCreateIfNeeded=*/false) : nullptr;
            FProperty* Prop = GenClass ? FindFProperty<FProperty>(GenClass, VarFName) : nullptr;
            if (!CDO || !Prop)
            {
                UE_LOG(LogBlueprintMutator, Warning,
                    TEXT("SetVariableDefault: '%s' has no compiled property on %s; compile the blueprint first"),
                    *VarName, *Blueprint->GetName());
                return false;
            }
            void* Addr = Prop->ContainerPtrToValuePtr<void>(CDO);

            const bool bIsTextLike = Prop->IsA<FStrProperty>() || Prop->IsA<FNameProperty>() || Prop->IsA<FTextProperty>();
            const FString ImportString = JsonDefaultToImportText(DefaultValueJson, bIsTextLike);

            // Parsed into scratch BEFORE anything real is touched. ImportText
            // has no dry-run mode and writes what it managed to read, so the
            // old order (write the description, Modify the CDO, then discover
            // the value will not parse) left a description string and a dirtied
            // CDO behind on every refusal. Nothing below this point can fail on
            // the value.
            FDefaultConstructedPropertyElement Scratch(Prop);
            if (!UBlueprintMutatorLibrary::ImportDefaultValue(Prop, ImportString, Scratch.GetObjAddress(), CDO))
            {
                UE_LOG(LogBlueprintMutator, Warning,
                    TEXT("SetVariableDefault: '%s' is not a value %s can hold (variable '%s')"),
                    *ImportString, *Prop->GetClass()->GetName(), *VarName);
                return false;
            }

            Blueprint->NewVariables[VarIndex].DefaultValue = ImportString;

            FString OldExported;
            Prop->ExportTextItem(OldExported, Addr, Addr, nullptr, PPF_SerializedAsImportText);

            // Modify() returns whether the object actually reached the undo
            // buffer, and for a class default object it does not: UObject::Modify
            // delegates to SaveToTransactionBuffer, which fails silently when the
            // object is not RF_Transactional. Discarding this bool is what made
            // finding 0r invisible for so long, so it is logged rather than
            // dropped. The caller's own snapshot is what restores this write;
            // see UBlueprintMutatorLibrary::SnapshotVariableDefaults.
            const bool bCdoIsTransacted = CDO->Modify();
            if (!bCdoIsTransacted)
            {
                UE_LOG(LogBlueprintMutator, Verbose,
                    TEXT("SetVariableDefault: '%s' CDO write is NOT in the transaction buffer; ")
                    TEXT("a cancelled transaction will not restore it."), *VarName);
            }
            Prop->ImportText(*ImportString, Addr, PPF_SerializedAsImportText, CDO);

            // Loaded child class CDOs carry their own copy of the value. Any
            // child still holding the old default should follow the new one
            // (children that overrode it keep their override), mirroring
            // delta-serialization semantics.
            for (TObjectIterator<UClass> It; It; ++It)
            {
                UClass* Child = *It;
                if (Child == GenClass || !Child->IsChildOf(GenClass) ||
                    Child->HasAnyClassFlags(CLASS_NewerVersionExists))
                {
                    continue;
                }
                UObject* ChildCDO = Child->GetDefaultObject(/*bCreateIfNeeded=*/false);
                if (!ChildCDO)
                {
                    continue;
                }
                void* ChildAddr = Prop->ContainerPtrToValuePtr<void>(ChildCDO);
                FString ChildExported;
                Prop->ExportTextItem(ChildExported, ChildAddr, ChildAddr, nullptr, PPF_SerializedAsImportText);
                if (ChildExported == OldExported)
                {
                    ChildCDO->Modify();
                    Prop->ImportText(*ImportString, ChildAddr, PPF_SerializedAsImportText, ChildCDO);
                }
            }
            return true;
        });
    if (bOk)
    {
        SyncDefaultValueFromCDO(Blueprint, VarFName);
    }
    return bOk;
}

bool FBPVariableOps::RenameVariable(UBlueprint* Blueprint, const FString& OldName, const FString& NewName)
{
    if (!Blueprint || OldName.IsEmpty() || NewName.IsEmpty()) return false;
    if (OldName == NewName) return true;
    const FName Old(*OldName);
    const FName New(*NewName);
    if (!Blueprint->NewVariables.ContainsByPredicate(
            [&](const FBPVariableDescription& V){ return V.VarName == Old; })
        || Blueprint->NewVariables.ContainsByPredicate(
            [&](const FBPVariableDescription& V){ return V.VarName == New; }))
    {
        return false;
    }
    return FBPMutatorHelpers::RunMutation(Blueprint,
        LOCTEXT("RenameVariable", "MCP Bridge: Rename Variable"), [&]()
        {
            FBlueprintEditorUtils::RenameMemberVariable(Blueprint, Old, New);
            return Blueprint->NewVariables.ContainsByPredicate(
                [&](const FBPVariableDescription& V){ return V.VarName == New; });
        });
}

bool FBPVariableOps::SetVariableMetadata(UBlueprint* Blueprint, const FString& VarName,
    const FString& MetadataJson, FString& OutError)
{
    if (!Blueprint) { OutError = TEXT("Blueprint is null."); return false; }
    const FName Name(*VarName);
    const int32 Index = Blueprint->NewVariables.IndexOfByPredicate(
        [&](const FBPVariableDescription& V){ return V.VarName == Name; });
    if (Index == INDEX_NONE) { OutError = FString::Printf(TEXT("Variable '%s' not found."), *VarName); return false; }
    TSharedPtr<FJsonObject> Metadata;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MetadataJson);
    if (!FJsonSerializer::Deserialize(Reader, Metadata) || !Metadata.IsValid())
    {
        OutError = TEXT("Variable metadata must be a JSON object.");
        return false;
    }
    static const TSet<FString> Known = {
        TEXT("category"), TEXT("tooltip"), TEXT("editable"), TEXT("private"),
        TEXT("expose_on_spawn"), TEXT("replicated") };
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Metadata->Values)
    {
        if (!Known.Contains(Pair.Key))
        {
            OutError = FString::Printf(TEXT("Unsupported variable metadata key '%s'."), *Pair.Key);
            return false;
        }
    }
    return FBPMutatorHelpers::RunMutation(Blueprint,
        LOCTEXT("SetVariableMetadata", "MCP Bridge: Set Variable Metadata"), [&]()
        {
            FBPVariableDescription& Variable = Blueprint->NewVariables[Index];
            FString Text;
            if (Metadata->TryGetStringField(TEXT("category"), Text))
            {
                FBlueprintEditorUtils::SetBlueprintVariableCategory(
                    Blueprint, Name, nullptr, FText::FromString(Text), /*bDontRecompile=*/true);
            }
            if (Metadata->TryGetStringField(TEXT("tooltip"), Text))
            {
                FBlueprintEditorUtils::SetBlueprintVariableMetaData(
                    Blueprint, Name, nullptr, FName(TEXT("tooltip")), Text);
            }
            auto SetMetadataBool = [&](const TCHAR* JsonKey, const TCHAR* MetadataKey)
            {
                bool bValue = false;
                if (!Metadata->TryGetBoolField(JsonKey, bValue)) return;
                const FName Key(MetadataKey);
                if (bValue) FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Name, nullptr, Key, TEXT("true"));
                else FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, Name, nullptr, Key);
            };
            SetMetadataBool(TEXT("private"), TEXT("BlueprintPrivate"));
            SetMetadataBool(TEXT("expose_on_spawn"), TEXT("ExposeOnSpawn"));
            bool bFlag = false;
            if (Metadata->TryGetBoolField(TEXT("editable"), bFlag))
            {
                bFlag ? Variable.PropertyFlags |= CPF_Edit : Variable.PropertyFlags &= ~CPF_Edit;
            }
            if (Metadata->TryGetBoolField(TEXT("replicated"), bFlag))
            {
                bFlag ? Variable.PropertyFlags |= CPF_Net : Variable.PropertyFlags &= ~(CPF_Net | CPF_RepNotify);
                if (!bFlag) Variable.RepNotifyFunc = NAME_None;
            }
            return true;
        });
}

#undef LOCTEXT_NAMESPACE
