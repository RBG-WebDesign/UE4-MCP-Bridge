#pragma once

#include "CoreMinimal.h"

struct FBTBuildSpec;
struct FBTBuildContext;

class FBTNodeFactory
{
public:
	/** Two-phase build: create all nodes, then wire them. Returns empty string on success. */
	static FString BuildTree(const FBTBuildSpec& Spec, FBTBuildContext& Ctx);
};
