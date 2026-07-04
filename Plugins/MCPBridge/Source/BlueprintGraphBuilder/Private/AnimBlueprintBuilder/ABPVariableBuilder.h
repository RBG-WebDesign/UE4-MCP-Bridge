#pragma once

#include "CoreMinimal.h"

class UAnimBlueprint;
struct FAnimBPVariableSpec;

class FAnimBPVariableBuilder
{
public:
	static FString AddVariables(
		UAnimBlueprint* AnimBP,
		const TArray<FAnimBPVariableSpec>& Variables
	);
};
