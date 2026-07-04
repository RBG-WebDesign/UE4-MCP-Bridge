#include "BTEditorGraphSync.h"

#if WITH_EDITOR

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "AIGraphNode.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Most BehaviorTreeEditor classes are NOT DLL-exported in UE4.27.
// UBehaviorTreeGraphNode_Composite IS exported. For everything else,
// resolve UClass at runtime via FindObject to avoid linker errors.
// We use UAIGraphNode* (exported from AIGraph) as the common base type.

static UClass* GetBTGraphNodeClass_Task()
{
	static UClass* Cls = FindObject<UClass>(ANY_PACKAGE, TEXT("BehaviorTreeGraphNode_Task"));
	return Cls;
}

static UClass* GetBTGraphNodeClass_Decorator()
{
	static UClass* Cls = FindObject<UClass>(ANY_PACKAGE, TEXT("BehaviorTreeGraphNode_Decorator"));
	return Cls;
}

static UClass* GetBTGraphNodeClass_Service()
{
	static UClass* Cls = FindObject<UClass>(ANY_PACKAGE, TEXT("BehaviorTreeGraphNode_Service"));
	return Cls;
}

static UClass* GetBTGraphClass()
{
	static UClass* Cls = FindObject<UClass>(ANY_PACKAGE, TEXT("BehaviorTreeGraph"));
	return Cls;
}

static UClass* GetBTGraphSchemaClass()
{
	static UClass* Cls = FindObject<UClass>(ANY_PACKAGE, TEXT("EdGraphSchema_BehaviorTree"));
	return Cls;
}

static UAIGraphNode* CreateGraphNodeForRuntime(UBTNode* RuntimeNode, UEdGraph* BTGraph)
{
	UAIGraphNode* GraphNode = nullptr;

	if (Cast<UBTCompositeNode>(RuntimeNode))
	{
		// UBehaviorTreeGraphNode_Composite IS exported
		GraphNode = NewObject<UBehaviorTreeGraphNode_Composite>(BTGraph);
	}
	else if (Cast<UBTTaskNode>(RuntimeNode))
	{
		UClass* TaskNodeClass = GetBTGraphNodeClass_Task();
		if (TaskNodeClass)
		{
			GraphNode = Cast<UAIGraphNode>(NewObject<UObject>(BTGraph, TaskNodeClass));
		}
	}
	else if (Cast<UBTDecorator>(RuntimeNode))
	{
		UClass* DecNodeClass = GetBTGraphNodeClass_Decorator();
		if (DecNodeClass)
		{
			GraphNode = Cast<UAIGraphNode>(NewObject<UObject>(BTGraph, DecNodeClass));
		}
	}
	else if (Cast<UBTService>(RuntimeNode))
	{
		UClass* SvcNodeClass = GetBTGraphNodeClass_Service();
		if (SvcNodeClass)
		{
			GraphNode = Cast<UAIGraphNode>(NewObject<UObject>(BTGraph, SvcNodeClass));
		}
	}

	if (GraphNode)
	{
		GraphNode->NodeInstance = RuntimeNode;
		BTGraph->AddNode(GraphNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
		GraphNode->AllocateDefaultPins();
	}

	return GraphNode;
}

static void SyncComposite(
	UBTCompositeNode* Composite,
	UAIGraphNode* ParentGraphNode,
	UEdGraph* BTGraph)
{
	if (!Composite || !ParentGraphNode) return;

	// Add services as sub-nodes of the composite graph node
	for (UBTService* Svc : Composite->Services)
	{
		if (!Svc) continue;
		UAIGraphNode* SvcGraphNode = CreateGraphNodeForRuntime(Svc, BTGraph);
		if (SvcGraphNode)
		{
			ParentGraphNode->AddSubNode(SvcGraphNode, BTGraph);
		}
	}

	for (const FBTCompositeChild& Child : Composite->Children)
	{
		UBTNode* ChildNode = Child.ChildComposite
			? static_cast<UBTNode*>(Child.ChildComposite)
			: static_cast<UBTNode*>(Child.ChildTask);

		if (!ChildNode) continue;

		UAIGraphNode* ChildGraphNode = CreateGraphNodeForRuntime(ChildNode, BTGraph);
		if (!ChildGraphNode) continue;

		// Connect parent output pin to child input pin
		if (ParentGraphNode->Pins.Num() > 0 && ChildGraphNode->Pins.Num() > 0)
		{
			UEdGraphPin* OutputPin = nullptr;
			for (UEdGraphPin* Pin : ParentGraphNode->Pins)
			{
				if (Pin->Direction == EGPD_Output)
				{
					OutputPin = Pin;
					break;
				}
			}
			UEdGraphPin* InputPin = nullptr;
			for (UEdGraphPin* Pin : ChildGraphNode->Pins)
			{
				if (Pin->Direction == EGPD_Input)
				{
					InputPin = Pin;
					break;
				}
			}
			if (OutputPin && InputPin)
			{
				OutputPin->MakeLinkTo(InputPin);
			}
		}

		// Add decorators as sub-nodes
		for (UBTDecorator* Dec : Child.Decorators)
		{
			if (!Dec) continue;
			UAIGraphNode* DecGraphNode = CreateGraphNodeForRuntime(Dec, BTGraph);
			if (DecGraphNode)
			{
				ChildGraphNode->AddSubNode(DecGraphNode, BTGraph);
			}
		}

		// Recurse if child is composite
		if (Child.ChildComposite)
		{
			SyncComposite(Child.ChildComposite, ChildGraphNode, BTGraph);
		}
	}
}

void FBTEditorGraphSync::Sync(UBehaviorTree* BT)
{
	if (!BT || !BT->RootNode)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BTEditorGraphSync] BT or RootNode is null, skipping sync"));
		return;
	}

	// Get or create editor graph using runtime-resolved classes (not exported)
	UEdGraph* BTGraph = BT->BTGraph;
	if (!BTGraph)
	{
		UClass* GraphClass = GetBTGraphClass();
		UClass* SchemaClass = GetBTGraphSchemaClass();
		if (GraphClass && SchemaClass)
		{
			BTGraph = FBlueprintEditorUtils::CreateNewGraph(
				BT, TEXT("BehaviorTreeGraph"),
				GraphClass,
				SchemaClass
			);
			BT->BTGraph = BTGraph;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[BTEditorGraphSync] could not resolve BehaviorTreeGraph/Schema classes"));
			return;
		}
	}

	// Clear existing graph nodes safely
	BTGraph->Modify();
	for (int32 i = BTGraph->Nodes.Num() - 1; i >= 0; --i)
	{
		if (BTGraph->Nodes[i])
		{
			BTGraph->RemoveNode(BTGraph->Nodes[i]);
		}
	}

	// Create root graph node
	UAIGraphNode* RootGraphNode = CreateGraphNodeForRuntime(BT->RootNode, BTGraph);
	if (!RootGraphNode)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BTEditorGraphSync] failed to create root graph node"));
		return;
	}

	// Recursively sync the tree
	SyncComposite(BT->RootNode, RootGraphNode, BTGraph);

	// Finalize: call UpdateAsset if available (UBehaviorTreeGraph may override it)
	UFunction* UpdateFunc = BTGraph->FindFunction(TEXT("UpdateAsset"));
	if (UpdateFunc)
	{
		BTGraph->ProcessEvent(UpdateFunc, nullptr);
	}
	BT->MarkPackageDirty();

	UE_LOG(LogTemp, Log, TEXT("[BTEditorGraphSync] synced editor graph for '%s'"), *BT->GetName());
}

#else  // !WITH_EDITOR

void FBTEditorGraphSync::Sync(UBehaviorTree* BT)
{
	// No-op outside editor
}

#endif  // WITH_EDITOR
