#include "ABPNodeRegistry.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_Slot.h"

FAnimBPNodeRegistry::FAnimBPNodeRegistry()
{
	RegisterTypes();
}

void FAnimBPNodeRegistry::RegisterTypes()
{
	PipelineTypes.Add(TEXT("StateMachine"), UAnimGraphNode_StateMachine::StaticClass());
	PipelineTypes.Add(TEXT("Slot"), UAnimGraphNode_Slot::StaticClass());
}

bool FAnimBPNodeRegistry::IsKnownPipelineType(const FString& Type) const
{
	return PipelineTypes.Contains(Type);
}

TSubclassOf<UAnimGraphNode_Base> FAnimBPNodeRegistry::GetPipelineNodeClass(const FString& Type) const
{
	const auto* Found = PipelineTypes.Find(Type);
	return Found ? *Found : nullptr;
}
