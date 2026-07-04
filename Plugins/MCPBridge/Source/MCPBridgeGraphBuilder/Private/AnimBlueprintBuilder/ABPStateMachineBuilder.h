#pragma once

#include "CoreMinimal.h"

struct FAnimBPBuildSpec;
struct FAnimBPBuildContext;

class FAnimBPStateMachineBuilder
{
public:
	static FString BuildStates(const FAnimBPBuildSpec& Spec, FAnimBPBuildContext& Ctx);
	static FString BuildTransitions(const FAnimBPBuildSpec& Spec, FAnimBPBuildContext& Ctx);
};
