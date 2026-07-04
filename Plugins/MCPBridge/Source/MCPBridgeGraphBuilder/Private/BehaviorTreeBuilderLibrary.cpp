#include "BehaviorTreeBuilderLibrary.h"
#include "BehaviorTreeBuilder/BTBuilder.h"
#include "BehaviorTree/BehaviorTree.h"

FString UBehaviorTreeBuilderLibrary::BuildBehaviorTreeFromJSON(
	UBehaviorTree* BehaviorTree,
	const FString& JsonString)
{
	UE_LOG(LogTemp, Log, TEXT("[BehaviorTreeBuilder] BuildBehaviorTreeFromJSON called"));

	if (!BehaviorTree)
	{
		return TEXT("[BehaviorTreeBuilder] BehaviorTree is null");
	}

	FBTBuilder Builder;
	return Builder.Build(BehaviorTree, JsonString);
}
