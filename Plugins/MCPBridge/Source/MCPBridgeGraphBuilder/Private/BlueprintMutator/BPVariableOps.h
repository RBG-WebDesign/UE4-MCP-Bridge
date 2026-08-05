// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
class UBlueprint;

class FBPVariableOps
{
public:
    static bool AddVariable(UBlueprint* BP, const FString& VarName, const FString& TypeJson, const FString& DefaultValueJson, const FString& Category);
    static bool RemoveVariable(UBlueprint* BP, const FString& VarName);
    static bool RenameVariable(UBlueprint* BP, const FString& OldName, const FString& NewName);
    static bool SetVariableMetadata(UBlueprint* BP, const FString& VarName, const FString& MetadataJson, FString& OutError);
    static bool SetVariableDefault(UBlueprint* BP, const FString& VarName, const FString& DefaultValueJson);
};
