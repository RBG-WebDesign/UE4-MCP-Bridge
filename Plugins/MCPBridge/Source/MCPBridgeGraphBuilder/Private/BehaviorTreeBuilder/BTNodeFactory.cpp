#include "BTNodeFactory.h"
#include "BTBuildSpec.h"
#include "BTNodeRegistry.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"

// Phase A: Recursively create all nodes
static FString CreateNodes(
	const FBTNodeSpec& Spec,
	FBTBuildContext& Ctx)
{
	UBTNode* Node = nullptr;
	const FBTNodeRegistry* Registry = Ctx.Registry;

	if (Registry->IsComposite(Spec.Type))
	{
		TSubclassOf<UBTCompositeNode> NodeClass = Registry->GetCompositeClass(Spec.Type);
		Node = NewObject<UBTCompositeNode>(Ctx.BehaviorTree, NodeClass);
	}
	else if (Registry->IsTask(Spec.Type))
	{
		TSubclassOf<UBTTaskNode> NodeClass = Registry->GetTaskClass(Spec.Type);
		Node = NewObject<UBTTaskNode>(Ctx.BehaviorTree, NodeClass);
	}
	else if (Registry->IsDecorator(Spec.Type))
	{
		TSubclassOf<UBTDecorator> NodeClass = Registry->GetDecoratorClass(Spec.Type);
		Node = NewObject<UBTDecorator>(Ctx.BehaviorTree, NodeClass);
	}
	else if (Registry->IsService(Spec.Type))
	{
		TSubclassOf<UBTService> NodeClass = Registry->GetServiceClass(Spec.Type);
		Node = NewObject<UBTService>(Ctx.BehaviorTree, NodeClass);
	}

	if (!Node)
	{
		return FString::Printf(TEXT("[BTNodeFactory] failed to create node '%s' (type: %s)"), *Spec.Id, *Spec.Type);
	}

	// Set display name
	if (!Spec.Name.IsEmpty())
	{
		Node->NodeName = Spec.Name;
	}

	// Apply params (handles BB key resolution)
	Registry->ApplyParams(Node, Spec.Type, Spec.Params, Ctx.Blackboard);

	// Initialize from asset (binds BB key selectors)
	Node->InitializeFromAsset(*Ctx.BehaviorTree);

	// Store in node map
	Ctx.NodeMap.Add(Spec.Id, Node);

	// Recurse into children
	for (const FBTNodeSpec& ChildSpec : Spec.Children)
	{
		FString Error = CreateNodes(ChildSpec, Ctx);
		if (!Error.IsEmpty()) return Error;
	}

	// Recurse into decorators
	for (const FBTNodeSpec& DecSpec : Spec.Decorators)
	{
		FString Error = CreateNodes(DecSpec, Ctx);
		if (!Error.IsEmpty()) return Error;
	}

	// Recurse into services
	for (const FBTNodeSpec& SvcSpec : Spec.Services)
	{
		FString Error = CreateNodes(SvcSpec, Ctx);
		if (!Error.IsEmpty()) return Error;
	}

	return FString();
}

// Phase B: Recursively wire nodes
static FString WireNodes(
	const FBTNodeSpec& Spec,
	FBTBuildContext& Ctx)
{
	UBTNode** FoundNode = Ctx.NodeMap.Find(Spec.Id);
	if (!FoundNode || !*FoundNode)
	{
		return FString::Printf(TEXT("[BTNodeFactory] node '%s' not found in NodeMap during wiring"), *Spec.Id);
	}

	UBTCompositeNode* Composite = Cast<UBTCompositeNode>(*FoundNode);
	if (!Composite)
	{
		// Task or decorator or service -- nothing to wire from here
		return FString();
	}

	// Wire services onto the composite
	for (const FBTNodeSpec& SvcSpec : Spec.Services)
	{
		UBTNode** SvcFound = Ctx.NodeMap.Find(SvcSpec.Id);
		if (SvcFound && *SvcFound)
		{
			UBTService* Svc = Cast<UBTService>(*SvcFound);
			if (Svc)
			{
				Composite->Services.Add(Svc);
			}
		}
	}

	// Wire each child into the composite
	for (int32 i = 0; i < Spec.Children.Num(); ++i)
	{
		const FBTNodeSpec& ChildSpec = Spec.Children[i];

		UBTNode** ChildFound = Ctx.NodeMap.Find(ChildSpec.Id);
		if (!ChildFound || !*ChildFound)
		{
			return FString::Printf(TEXT("[BTNodeFactory] child '%s' not found during wiring"), *ChildSpec.Id);
		}

		FBTCompositeChild CompositeChild;
		CompositeChild.ChildComposite = Cast<UBTCompositeNode>(*ChildFound);
		CompositeChild.ChildTask = Cast<UBTTaskNode>(*ChildFound);

		// Attach decorators defined on this child
		for (const FBTNodeSpec& DecSpec : ChildSpec.Decorators)
		{
			UBTNode** DecFound = Ctx.NodeMap.Find(DecSpec.Id);
			if (DecFound && *DecFound)
			{
				UBTDecorator* Dec = Cast<UBTDecorator>(*DecFound);
				if (Dec)
				{
					CompositeChild.Decorators.Add(Dec);
				}
			}
		}

		Composite->Children.Add(CompositeChild);

		// Recurse to wire grandchildren
		FString Error = WireNodes(ChildSpec, Ctx);
		if (!Error.IsEmpty()) return Error;
	}

	return FString();
}

FString FBTNodeFactory::BuildTree(const FBTBuildSpec& Spec, FBTBuildContext& Ctx)
{
	// Phase A: Create all nodes
	FString CreateError = CreateNodes(Spec.Root, Ctx);
	if (!CreateError.IsEmpty()) return CreateError;

	// Phase B: Wire nodes
	FString WireError = WireNodes(Spec.Root, Ctx);
	if (!WireError.IsEmpty()) return WireError;

	return FString();
}
