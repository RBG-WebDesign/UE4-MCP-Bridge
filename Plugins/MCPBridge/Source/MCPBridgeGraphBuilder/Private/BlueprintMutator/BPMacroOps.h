// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
class UBlueprint;

class FBPMacroOps
{
public:
    static FString AddMacro(UBlueprint* Blueprint, const FString& Name,
        const FString& InputsJson, const FString& OutputsJson, FString& OutError);
    static bool RemoveMacro(UBlueprint* Blueprint, const FString& Name, FString& OutError);
    static bool RenameMacro(UBlueprint* Blueprint, const FString& From, const FString& To, FString& OutError);
    static bool SetMacroSignature(UBlueprint* Blueprint, const FString& Name,
        const FString& InputsJson, const FString& OutputsJson, FString& OutError);
};