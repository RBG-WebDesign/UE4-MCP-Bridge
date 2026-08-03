// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BlueprintMutatorLibrary.h"
#include "BPMutatorHelpers.h"
#include "BPVariableOps.h"
#include "BPInterfaceOps.h"
#include "BPSCSOps.h"
#include "BPNodeOps.h"
#include "BPPinOps.h"
#include "BPFunctionOps.h"
#include "BPEventDispatcherOps.h"
#include "../BlueprintInspector/BPGraphReader.h"
#include "BPGLogCategories.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/ITransaction.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "BlueprintMutatorLibrary"

void UBlueprintMutatorLibrary::RevertAndCancelTransaction(FScopedTransaction& Transaction)
{
    // Order matters: Cancel() clears GUndo, so the restore has to run first.
    if (GUndo != nullptr)
    {
        GUndo->Apply();
    }
    Transaction.Cancel();
}

bool UBlueprintMutatorLibrary::TryReadVariableDefaultFromCDO(const UBlueprint* Blueprint, FName VarName, FString& OutDefault)
{
    if (!Blueprint) { return false; }
    UClass* GenClass = Blueprint->GeneratedClass;
    UObject* CDO = GenClass ? GenClass->GetDefaultObject(/*bCreateIfNeeded=*/false) : nullptr;
    if (!CDO) { return false; }
    FProperty* Prop = FindFProperty<FProperty>(CDO->GetClass(), VarName);
    if (!Prop) { return false; }
    const void* Addr = Prop->ContainerPtrToValuePtr<void>(CDO);
    if (!Addr) { return false; }

    OutDefault.Reset();
    Prop->ExportTextItem(OutDefault, Addr, Addr, nullptr, PPF_SerializedAsImportText);
    return true;
}

FString UBlueprintMutatorLibrary::JsonDefaultToImportText(const FString& DefaultValueJson, bool bTextLike)
{
    FString Trimmed = DefaultValueJson.TrimStartAndEnd();
    const bool bQuoted = Trimmed.Len() >= 2 && Trimmed.StartsWith(TEXT("\"")) && Trimmed.EndsWith(TEXT("\""));
    if (!bQuoted || bTextLike)
    {
        return Trimmed;
    }
    FString Inner = Trimmed.Mid(1, Trimmed.Len() - 2);
    Inner.ReplaceInline(TEXT("\\\""), TEXT("\""));
    Inner.ReplaceInline(TEXT("\\\\"), TEXT("\\"));
    return Inner;
}

bool UBlueprintMutatorLibrary::ImportDefaultValue(const FProperty* Property, const FString& ImportText,
    void* ValueAddress, UObject* Owner)
{
    if (Property == nullptr || ValueAddress == nullptr)
    {
        return false;
    }
    const TCHAR* End = Property->ImportText(*ImportText, ValueAddress, PPF_SerializedAsImportText, Owner);
    if (End == nullptr)
    {
        return false;
    }
    while (*End == TEXT(' ') || *End == TEXT('\t') || *End == TEXT('\r') || *End == TEXT('\n'))
    {
        ++End;
    }
    if (*End != TEXT('\0'))
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("ImportDefaultValue: %s read only part of '%s' and stopped at '%s'. "
                 "Treated as a refusal, not a value."),
            *Property->GetClass()->GetName(), *ImportText, End);
        return false;
    }
    return true;
}

bool UBlueprintMutatorLibrary::SetNodeEnabled(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid, bool bEnabled)
{
    if (!Blueprint)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("SetNodeEnabled: null blueprint"));
        return false;
    }
    UEdGraph* Graph = FBPGraphReader::FindGraphByName(Blueprint, GraphName);
    if (!Graph)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("SetNodeEnabled: graph '%s' not found"), *GraphName);
        return false;
    }
    UEdGraphNode* Node = FBPGraphReader::FindNodeByGuid(Graph, NodeGuid);
    if (!Node)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("SetNodeEnabled: node '%s' not found in '%s'"), *NodeGuid, *GraphName);
        return false;
    }

    const ENodeEnabledState TargetState = bEnabled ? ENodeEnabledState::Enabled : ENodeEnabledState::Disabled;
    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("SetNodeEnabled", "Soul Juice: Set Node Enabled"),
        [&]() -> bool
        {
            Node->Modify();
            Node->SetEnabledState(TargetState, /*bUserAction=*/true);
            return true;
        });
}

bool UBlueprintMutatorLibrary::BreakPinLinks(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid, const FString& PinName)
{
    if (!Blueprint)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("BreakPinLinks: null blueprint"));
        return false;
    }
    UEdGraph* Graph = FBPGraphReader::FindGraphByName(Blueprint, GraphName);
    if (!Graph) { UE_LOG(LogBlueprintMutator, Warning, TEXT("BreakPinLinks: graph '%s' not found"), *GraphName); return false; }
    UEdGraphNode* Node = FBPGraphReader::FindNodeByGuid(Graph, NodeGuid);
    if (!Node)  { UE_LOG(LogBlueprintMutator, Warning, TEXT("BreakPinLinks: node '%s' not found"), *NodeGuid); return false; }

    UEdGraphPin* Pin = nullptr;
    for (UEdGraphPin* P : Node->Pins)
    {
        if (P && P->PinName.ToString() == PinName) { Pin = P; break; }
    }
    if (!Pin)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("BreakPinLinks: pin '%s' not found on node '%s'"), *PinName, *NodeGuid);
        return false;
    }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("BreakPinLinks", "Soul Juice: Break Pin Links"),
        [&]() -> bool
        {
            Node->Modify();
            Pin->BreakAllPinLinks(/*bNotifyNodes=*/true);
            return true;
        });
}
bool UBlueprintMutatorLibrary::AddVariable(UBlueprint* Blueprint, const FString& VarName, const FString& TypeJson, const FString& DefaultValueJson, const FString& Category)
{
    return FBPVariableOps::AddVariable(Blueprint, VarName, TypeJson, DefaultValueJson, Category);
}
bool UBlueprintMutatorLibrary::RemoveVariable(UBlueprint* Blueprint, const FString& VarName)
{
    return FBPVariableOps::RemoveVariable(Blueprint, VarName);
}
bool UBlueprintMutatorLibrary::SetVariableDefault(UBlueprint* Blueprint, const FString& VarName, const FString& DefaultValueJson)
{
    return FBPVariableOps::SetVariableDefault(Blueprint, VarName, DefaultValueJson);
}
bool UBlueprintMutatorLibrary::AddInterfaceImplementation(UBlueprint* Blueprint, UClass* InterfaceClass)
{
    return FBPInterfaceOps::AddInterfaceImplementation(Blueprint, InterfaceClass);
}
bool UBlueprintMutatorLibrary::RemoveInterfaceImplementation(UBlueprint* Blueprint, UClass* InterfaceClass)
{
    return FBPInterfaceOps::RemoveInterfaceImplementation(Blueprint, InterfaceClass);
}
bool UBlueprintMutatorLibrary::RemoveSCSNode(UBlueprint* Blueprint, const FString& ComponentName)
{
    return FBPSCSOps::RemoveSCSNode(Blueprint, ComponentName);
}
bool UBlueprintMutatorLibrary::RenameSCSNode(UBlueprint* Blueprint, const FString& OldName, const FString& NewName)
{
    return FBPSCSOps::RenameSCSNode(Blueprint, OldName, NewName);
}

FString UBlueprintMutatorLibrary::AddNode(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeType, FVector2D Position, const FString& ConfigJson)
{
    return FBPNodeOps::AddNode(Blueprint, GraphName, NodeType, Position, ConfigJson);
}
bool UBlueprintMutatorLibrary::DeleteNode(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid)
{
    return FBPNodeOps::DeleteNode(Blueprint, GraphName, NodeGuid);
}
bool UBlueprintMutatorLibrary::MoveNode(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid, FVector2D NewPosition)
{
    return FBPNodeOps::MoveNode(Blueprint, GraphName, NodeGuid, NewPosition);
}
bool UBlueprintMutatorLibrary::ConnectPins(UBlueprint* Blueprint, const FString& GraphName,
    const FString& SrcNodeGuid, const FString& SrcPinName,
    const FString& DstNodeGuid, const FString& DstPinName)
{
    return FBPPinOps::ConnectPins(Blueprint, GraphName, SrcNodeGuid, SrcPinName, DstNodeGuid, DstPinName);
}
bool UBlueprintMutatorLibrary::SetCallFunctionTarget(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid, UClass* TargetClass, const FString& FunctionName)
{
    return FBPNodeOps::SetCallFunctionTarget(Blueprint, GraphName, NodeGuid, TargetClass, FunctionName);
}

// --- Tier 4: structural (Phase 4) ---

FString UBlueprintMutatorLibrary::AddFunction(UBlueprint* Blueprint, const FString& FunctionName, const FString& InputsJson, const FString& OutputsJson)
{
    return FBPFunctionOps::AddFunction(Blueprint, FunctionName, InputsJson, OutputsJson);
}
bool UBlueprintMutatorLibrary::RemoveFunction(UBlueprint* Blueprint, const FString& FunctionName)
{
    return FBPFunctionOps::RemoveFunction(Blueprint, FunctionName);
}
FString UBlueprintMutatorLibrary::AddEventDispatcher(UBlueprint* Blueprint, const FString& DispatcherName, const FString& SignatureJson)
{
    return FBPEventDispatcherOps::AddEventDispatcher(Blueprint, DispatcherName, SignatureJson);
}
bool UBlueprintMutatorLibrary::RemoveEventDispatcher(UBlueprint* Blueprint, const FString& DispatcherName)
{
    return FBPEventDispatcherOps::RemoveEventDispatcher(Blueprint, DispatcherName);
}

#undef LOCTEXT_NAMESPACE
