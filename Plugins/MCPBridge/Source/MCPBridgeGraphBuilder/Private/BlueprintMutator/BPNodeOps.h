// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

class UBlueprint;
class UClass;

class FBPNodeOps
{
public:
    static FString AddNode(UBlueprint* BP, const FString& GraphName, const FString& NodeType, FVector2D Position, const FString& ConfigJson);
    static bool DeleteNode(UBlueprint* BP, const FString& GraphName, const FString& NodeGuid);
    static bool MoveNode(UBlueprint* BP, const FString& GraphName, const FString& NodeGuid, FVector2D NewPosition);
    static bool SetCallFunctionTarget(UBlueprint* BP, const FString& GraphName, const FString& NodeGuid, UClass* TargetClass, const FString& FunctionName);
};
