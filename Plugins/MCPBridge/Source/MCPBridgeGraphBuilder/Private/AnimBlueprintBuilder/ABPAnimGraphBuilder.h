#pragma once

#include "CoreMinimal.h"

struct FAnimBPBuildSpec;
struct FAnimBPBuildContext;

class FAnimBPAnimGraphBuilder
{
public:
	static FString Build(const FAnimBPBuildSpec& Spec, FAnimBPBuildContext& Ctx);
};
