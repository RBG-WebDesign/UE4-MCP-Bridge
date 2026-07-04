#include "BPNodeOps.h"
#include "BPMutatorHelpers.h"
#include "BPNodeRegistry.h"
#include "BPGLogCategories.h"
#include "../BlueprintInspector/BPGraphReader.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"

#define LOCTEXT_NAMESPACE "BPNodeOps"

FString FBPNodeOps::AddNode(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeType, FVector2D Position, const FString& ConfigJson)
{
    if (!Blueprint) { UE_LOG(LogBlueprintMutator, Warning, TEXT("AddNode: null blueprint")); return FString(); }
    UEdGraph* Graph = FBPGraphReader::FindGraphByName(Blueprint, GraphName);
    if (!Graph) { UE_LOG(LogBlueprintMutator, Warning, TEXT("AddNode: graph '%s' not found"), *GraphName); return FString(); }

    const FBPNodeRegistry::FFactory Factory = FBPNodeRegistry::Find(NodeType);
    if (!Factory)
    {
        const FString Registered = FString::Join(FBPNodeRegistry::ListRegistered(), TEXT(", "));
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("AddNode: node_type '%s' not registered. Registered: [%s]"),
            *NodeType, *Registered);
        return FString();
    }

    FString ResultGuid;
    const bool bOk = FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("AddNode", "Soul Juice: Add Node"),
        [&]() -> bool
        {
            UEdGraphNode* NewNode = Factory(Graph, Position, ConfigJson);
            if (!NewNode)
            {
                UE_LOG(LogBlueprintMutator, Error,
                    TEXT("AddNode: factory for '%s' returned nullptr"), *NodeType);
                return false;
            }
            ResultGuid = NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
            return true;
        });
    return bOk ? ResultGuid : FString();
}

bool FBPNodeOps::DeleteNode(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid)
{
    if (!Blueprint) { UE_LOG(LogBlueprintMutator, Warning, TEXT("DeleteNode: null BP")); return false; }
    UEdGraph* Graph = FBPGraphReader::FindGraphByName(Blueprint, GraphName);
    if (!Graph) { UE_LOG(LogBlueprintMutator, Warning, TEXT("DeleteNode: graph '%s' not found"), *GraphName); return false; }
    UEdGraphNode* Node = FBPGraphReader::FindNodeByGuid(Graph, NodeGuid);
    if (!Node) { UE_LOG(LogBlueprintMutator, Warning, TEXT("DeleteNode: node '%s' not found"), *NodeGuid); return false; }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("DeleteNode", "Soul Juice: Delete Node"),
        [&]() -> bool
        {
            Node->Modify();
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin) Pin->BreakAllPinLinks(/*bNotifyNodes=*/true);
            }
            Graph->Modify();
            Graph->RemoveNode(Node);
            return true;
        });
}

bool FBPNodeOps::MoveNode(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid, FVector2D NewPosition)
{
    if (!Blueprint) return false;
    UEdGraph* Graph = FBPGraphReader::FindGraphByName(Blueprint, GraphName);
    if (!Graph) { UE_LOG(LogBlueprintMutator, Warning, TEXT("MoveNode: graph '%s' not found"), *GraphName); return false; }
    UEdGraphNode* Node = FBPGraphReader::FindNodeByGuid(Graph, NodeGuid);
    if (!Node) { UE_LOG(LogBlueprintMutator, Warning, TEXT("MoveNode: node '%s' not found"), *NodeGuid); return false; }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("MoveNode", "Soul Juice: Move Node"),
        [&]() -> bool
        {
            Node->Modify();
            Node->NodePosX = FMath::RoundToInt(NewPosition.X);
            Node->NodePosY = FMath::RoundToInt(NewPosition.Y);
            return true;
        });
}

bool FBPNodeOps::SetCallFunctionTarget(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid, UClass* TargetClass, const FString& FunctionName)
{
    if (!Blueprint) { UE_LOG(LogBlueprintMutator, Warning, TEXT("SetCallFunctionTarget: null BP")); return false; }
    if (!TargetClass) { UE_LOG(LogBlueprintMutator, Warning, TEXT("SetCallFunctionTarget: null TargetClass")); return false; }
    if (FunctionName.IsEmpty()) { UE_LOG(LogBlueprintMutator, Warning, TEXT("SetCallFunctionTarget: empty FunctionName")); return false; }

    UEdGraph* Graph = FBPGraphReader::FindGraphByName(Blueprint, GraphName);
    if (!Graph) { UE_LOG(LogBlueprintMutator, Warning, TEXT("SetCallFunctionTarget: graph '%s' not found"), *GraphName); return false; }
    UEdGraphNode* Node = FBPGraphReader::FindNodeByGuid(Graph, NodeGuid);
    if (!Node) { UE_LOG(LogBlueprintMutator, Warning, TEXT("SetCallFunctionTarget: node '%s' not found"), *NodeGuid); return false; }

    UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
    if (!CallNode)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("SetCallFunctionTarget: node '%s' is not a K2Node_CallFunction (got %s)"),
            *NodeGuid, *Node->GetClass()->GetName());
        return false;
    }

    UFunction* NewFunc = TargetClass->FindFunctionByName(*FunctionName);
    if (!NewFunc)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("SetCallFunctionTarget: function '%s' not found on '%s'"),
            *FunctionName, *TargetClass->GetName());
        return false;
    }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("SetCallFunctionTarget", "Soul Juice: Set Call Function Target"),
        [&]() -> bool
        {
            CallNode->Modify();
            CallNode->SetFromFunction(NewFunc);
            CallNode->ReconstructNode();
            return true;
        });
}

#undef LOCTEXT_NAMESPACE
