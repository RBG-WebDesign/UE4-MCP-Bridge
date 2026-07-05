// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

class FBPFunctionOps
{
public:
    /**
     * Create a user function graph with typed parameters. Returns the graph name on success
     * (matches FunctionName arg unless UE rewrites it for uniqueness — shouldn't happen given
     * the pre-transaction collision check).
     */
    static FString AddFunction(UBlueprint* Blueprint, const FString& FunctionName, const FString& InputsJson, const FString& OutputsJson);

    /** Remove a named user function graph. Returns true on success. */
    static bool RemoveFunction(UBlueprint* Blueprint, const FString& FunctionName);
};
