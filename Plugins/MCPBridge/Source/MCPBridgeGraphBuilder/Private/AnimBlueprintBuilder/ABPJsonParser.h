#pragma once

#include "CoreMinimal.h"

struct FAnimBPBuildSpec;

class FAnimBPJsonParser
{
public:
	static FString Parse(const FString& JsonString, FAnimBPBuildSpec& OutSpec);
};
