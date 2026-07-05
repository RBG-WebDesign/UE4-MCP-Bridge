// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

class FBPEventDispatcherOps
{
public:
    /**
     * Create a delegate signature graph under Blueprint->DelegateSignatureGraphs.
     * Returns the graph name on success, empty string on failure.
     */
    static FString AddEventDispatcher(UBlueprint* Blueprint, const FString& DispatcherName, const FString& SignatureJson);

    /** Remove a delegate signature graph by name. */
    static bool RemoveEventDispatcher(UBlueprint* Blueprint, const FString& DispatcherName);
};
