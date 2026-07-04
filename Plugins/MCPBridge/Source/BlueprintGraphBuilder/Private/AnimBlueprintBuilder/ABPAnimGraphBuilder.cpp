#include "ABPAnimGraphBuilder.h"
#include "ABPBuildSpec.h"
#include "ABPNodeRegistry.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_Root.h"
#include "AnimationStateMachineGraph.h"
#include "EdGraph/EdGraph.h"

static UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Dir)
{
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->PinName.ToString() == PinName && Pin->Direction == Dir)
		{
			return Pin;
		}
	}
	return nullptr;
}

FString FAnimBPAnimGraphBuilder::Build(const FAnimBPBuildSpec& Spec, FAnimBPBuildContext& Ctx)
{
	// Find AnimGraph among function graphs
	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : Ctx.AnimBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->Schema && Graph->Schema->GetClass()->GetName().Contains(TEXT("AnimationGraphSchema")))
		{
			AnimGraph = Graph;
			break;
		}
	}
	if (!AnimGraph)
	{
		return TEXT("[ABPAnimGraphBuilder] AnimGraph not found in AnimBlueprint");
	}

	// Find existing Root node
	UAnimGraphNode_Root* RootNode = nullptr;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		RootNode = Cast<UAnimGraphNode_Root>(Node);
		if (RootNode) break;
	}
	if (!RootNode)
	{
		return TEXT("[ABPAnimGraphBuilder] Root node not found in AnimGraph");
	}

	// Create pipeline nodes in order
	float NodeX = RootNode->NodePosX - (Spec.AnimGraphPipeline.Num() * 300);
	for (int32 i = 0; i < Spec.AnimGraphPipeline.Num(); i++)
	{
		const FAnimBPAnimGraphNodeSpec& NodeSpec = Spec.AnimGraphPipeline[i];
		TSubclassOf<UAnimGraphNode_Base> NodeClass = Ctx.Registry->GetPipelineNodeClass(NodeSpec.Type);
		if (!NodeClass)
		{
			return FString::Printf(TEXT("[ABPAnimGraphBuilder] unknown pipeline type '%s'"), *NodeSpec.Type);
		}

		UAnimGraphNode_Base* NewNode = NewObject<UAnimGraphNode_Base>(AnimGraph, NodeClass);
		NewNode->CreateNewGuid();
		NewNode->PostPlacedNewNode();
		NewNode->AllocateDefaultPins();
		AnimGraph->AddNode(NewNode, false, false);
		NewNode->NodePosX = NodeX + (i * 300);
		NewNode->NodePosY = RootNode->NodePosY;

		// Configure Slot name
		if (NodeSpec.Type == TEXT("Slot"))
		{
			UAnimGraphNode_Slot* SlotNode = Cast<UAnimGraphNode_Slot>(NewNode);
			if (SlotNode)
			{
				SlotNode->Node.SlotName = FName(*NodeSpec.Name);
			}
		}

		// For StateMachine, capture the inner state machine graph
		if (NodeSpec.Type == TEXT("StateMachine"))
		{
			UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(NewNode);
			if (SMNode)
			{
				Ctx.StateMachineGraph = SMNode->GetStateMachineGraph();
				if (!Ctx.StateMachineGraph)
				{
					return TEXT("[ABPAnimGraphBuilder] StateMachine node has no state machine graph");
				}
			}
		}

		Ctx.AnimGraphNodeMap.Add(NodeSpec.Id, NewNode);
	}

	// Wire pose pins: last pipeline node -> Root
	if (Spec.AnimGraphPipeline.Num() > 0)
	{
		const FString& LastId = Spec.AnimGraphPipeline.Last().Id;
		UAnimGraphNode_Base* LastNode = Ctx.AnimGraphNodeMap.FindRef(LastId);
		if (LastNode)
		{
			UEdGraphPin* OutputPin = FindPinByName(LastNode, TEXT("Pose"), EGPD_Output);
			if (!OutputPin) OutputPin = FindPinByName(LastNode, TEXT("Result"), EGPD_Output);
			UEdGraphPin* RootInput = FindPinByName(RootNode, TEXT("Result"), EGPD_Input);
			if (!OutputPin)
				return FString::Printf(TEXT("[ABPAnimGraphBuilder] no output pose pin on node '%s'"), *LastId);
			if (!RootInput)
				return TEXT("[ABPAnimGraphBuilder] no input pin on Root node");
			OutputPin->MakeLinkTo(RootInput);
		}

		// Wire pipeline nodes in sequence: node[i] output -> node[i+1] input
		for (int32 i = 0; i < Spec.AnimGraphPipeline.Num() - 1; i++)
		{
			UAnimGraphNode_Base* Source = Ctx.AnimGraphNodeMap.FindRef(Spec.AnimGraphPipeline[i].Id);
			UAnimGraphNode_Base* Target = Ctx.AnimGraphNodeMap.FindRef(Spec.AnimGraphPipeline[i + 1].Id);
			if (Source && Target)
			{
				UEdGraphPin* SrcOut = FindPinByName(Source, TEXT("Pose"), EGPD_Output);
				if (!SrcOut) SrcOut = FindPinByName(Source, TEXT("Result"), EGPD_Output);
				UEdGraphPin* TgtIn = FindPinByName(Target, TEXT("Source"), EGPD_Input);
				if (!TgtIn) TgtIn = FindPinByName(Target, TEXT("Pose"), EGPD_Input);
				if (!SrcOut)
					return FString::Printf(TEXT("[ABPAnimGraphBuilder] no output pose pin on '%s'"), *Spec.AnimGraphPipeline[i].Id);
				if (!TgtIn)
					return FString::Printf(TEXT("[ABPAnimGraphBuilder] no input pose pin on '%s'"), *Spec.AnimGraphPipeline[i+1].Id);
				SrcOut->MakeLinkTo(TgtIn);
			}
		}
	}

	return FString();
}
