#include "BPVariableOps.h"
#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "../BlueprintInspector/BPTypeDescriptor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "BPVariableOps"

namespace
{
    /** After a full compile, KismetCompiler::CreateClassVariablesFromBlueprint empties
     *  FBPVariableDescription::DefaultValue (having already copied the value into the CDO).
     *  Syncs the description field back from the CDO so downstream readers (inspector,
     *  editor UI) still see the current default. Matches what
     *  FBlueprintEditorUtils::DuplicateVariableDescription does on BP duplication. */
    void SyncDefaultValueFromCDO(UBlueprint* Blueprint, const FName& VarFName)
    {
        if (!Blueprint) return;
        UClass* GenClass = Blueprint->GeneratedClass;
        UObject* CDO = GenClass ? GenClass->GetDefaultObject(/*bCreateIfNeeded=*/false) : nullptr;
        if (!CDO) return;
        FProperty* Prop = FindFProperty<FProperty>(CDO->GetClass(), VarFName);
        if (!Prop) return;
        void* Addr = Prop->ContainerPtrToValuePtr<void>(CDO);
        if (!Addr) return;
        const int32 Idx = Blueprint->NewVariables.IndexOfByPredicate(
            [&](const FBPVariableDescription& D){ return D.VarName == VarFName; });
        if (Idx == INDEX_NONE) return;
        FString Exported;
        Prop->ExportTextItem(Exported, Addr, Addr, nullptr, PPF_SerializedAsImportText);
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
            const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarFName, PinType, DefaultValueJson);
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
            // UE4.27 has no FBlueprintEditorUtils::SetBlueprintVariableDefaultValue; write the
            // string default directly onto the FBPVariableDescription (this is what
            // AddMemberVariable itself does at BlueprintEditorUtils.cpp:4747). The subsequent
            // CompileBlueprint inside RunMutation applies it to the CDO.
            Blueprint->NewVariables[VarIndex].DefaultValue = DefaultValueJson;
            return true;
        });
    if (bOk)
    {
        SyncDefaultValueFromCDO(Blueprint, VarFName);
    }
    return bOk;
}

#undef LOCTEXT_NAMESPACE
