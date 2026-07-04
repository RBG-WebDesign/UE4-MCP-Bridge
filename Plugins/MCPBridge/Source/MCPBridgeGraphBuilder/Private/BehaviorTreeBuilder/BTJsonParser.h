#pragma once

#include "CoreMinimal.h"

struct FBTBuildSpec;

class FBTJsonParser
{
public:
	/** Parse JSON string into FBTBuildSpec. Returns empty string on success, error on failure. */
	static FString Parse(const FString& JsonString, FBTBuildSpec& OutSpec);
};
