#pragma once

#include "CoreMinimal.h"

class UBehaviorTree;
class UBlackboardData;
class UBTNode;
class FBTNodeRegistry;

struct FBTNodeSpec
{
	FString Id;
	FString Type;
	FString Name;
	TMap<FString, FString> Params;
	TArray<FBTNodeSpec> Children;
	TArray<FBTNodeSpec> Decorators;
	TArray<FBTNodeSpec> Services;
};

struct FBTBuildSpec
{
	FBTNodeSpec Root;
};

struct FBTBuildContext
{
	UBehaviorTree* BehaviorTree = nullptr;
	UBlackboardData* Blackboard = nullptr;
	const FBTNodeRegistry* Registry = nullptr;
	TMap<FString, UBTNode*> NodeMap;  // Id -> created node instance
};
