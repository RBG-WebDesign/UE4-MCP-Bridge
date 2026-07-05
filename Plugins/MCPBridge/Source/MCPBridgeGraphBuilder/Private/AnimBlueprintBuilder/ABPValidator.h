// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAnimBPBuildSpec;

class FAnimBPValidator
{
public:
	static TArray<FString> Validate(const FAnimBPBuildSpec& Spec);
};
