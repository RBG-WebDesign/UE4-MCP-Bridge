// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
class UBlueprint;

class FBPVariableOps
{
public:
    static bool AddVariable(UBlueprint* BP, const FString& VarName, const FString& TypeJson, const FString& DefaultValueJson, const FString& Category);
    static bool RemoveVariable(UBlueprint* BP, const FString& VarName);
    static bool SetVariableDefault(UBlueprint* BP, const FString& VarName, const FString& DefaultValueJson);
};
