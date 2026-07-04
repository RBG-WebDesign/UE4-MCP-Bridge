#pragma once

#include "CoreMinimal.h"

class UAnimGraphNode_Base;

class FAnimBPNodeRegistry
{
public:
	FAnimBPNodeRegistry();

	bool IsKnownPipelineType(const FString& Type) const;
	TSubclassOf<UAnimGraphNode_Base> GetPipelineNodeClass(const FString& Type) const;

private:
	TMap<FString, TSubclassOf<UAnimGraphNode_Base>> PipelineTypes;
	void RegisterTypes();
};
