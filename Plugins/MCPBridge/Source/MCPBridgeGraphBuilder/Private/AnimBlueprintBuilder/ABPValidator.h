#pragma once

#include "CoreMinimal.h"

struct FAnimBPBuildSpec;

class FAnimBPValidator
{
public:
	static TArray<FString> Validate(const FAnimBPBuildSpec& Spec);
};
