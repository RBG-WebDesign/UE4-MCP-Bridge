#pragma once

#include "CoreMinimal.h"

class UAnimBlueprint;
class USkeleton;
class UAnimStateNode;
class UAnimGraphNode_Base;
class UAnimationStateMachineGraph;

struct FAnimBPVariableSpec
{
	FString Name;
	FString Type;       // "bool" for v1
	FString Default;    // "true" or "false"
};

struct FAnimBPTransitionConditionSpec
{
	FString Type;       // "bool_variable" or "time_remaining"
	FString Variable;   // for bool_variable
	FString Value;      // "true" or "false" for bool_variable
	float Threshold = 0.1f; // for time_remaining
};

struct FAnimBPTransitionSpec
{
	FString From;       // state id
	FString To;         // state id
	float BlendTime = 0.2f;
	FAnimBPTransitionConditionSpec Condition;
};

struct FAnimBPStateSpec
{
	FString Id;
	FString Name;
	FString Animation;  // asset path to AnimSequence
	bool bLooping = true;
	bool bIsEntry = false;
};

struct FAnimBPAnimGraphNodeSpec
{
	FString Id;
	FString Type;       // "StateMachine" or "Slot"
	FString Name;
};

struct FAnimBPBuildSpec
{
	TArray<FAnimBPVariableSpec> Variables;
	TArray<FAnimBPAnimGraphNodeSpec> AnimGraphPipeline;
	TArray<FAnimBPStateSpec> States;
	TArray<FAnimBPTransitionSpec> Transitions;
	FString EventGraphJson;
};

struct FAnimBPBuildContext
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	USkeleton* Skeleton = nullptr;
	const class FAnimBPNodeRegistry* Registry = nullptr;
	UAnimationStateMachineGraph* StateMachineGraph = nullptr;
	TMap<FString, UAnimStateNode*> StateNodeMap;
	TMap<FString, UAnimGraphNode_Base*> AnimGraphNodeMap;
};
