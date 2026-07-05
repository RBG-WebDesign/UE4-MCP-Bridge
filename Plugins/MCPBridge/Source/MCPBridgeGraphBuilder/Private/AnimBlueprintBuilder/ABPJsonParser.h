// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAnimBPBuildSpec;

class FAnimBPJsonParser
{
public:
	static FString Parse(const FString& JsonString, FAnimBPBuildSpec& OutSpec);
};
