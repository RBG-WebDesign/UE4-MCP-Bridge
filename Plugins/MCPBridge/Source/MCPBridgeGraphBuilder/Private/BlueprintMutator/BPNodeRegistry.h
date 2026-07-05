// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class UEdGraph;
class UEdGraphNode;

class FBPNodeRegistry
{
public:
    using FFactory = TFunction<UEdGraphNode*(UEdGraph* Graph, FVector2D Position, const FString& ConfigJson)>;

    static void Initialize();
    static FFactory Find(const FString& NodeType);
    static TArray<FString> ListRegistered();

private:
    static void Register(const FString& NodeType, FFactory Factory);
    static TMap<FString, FFactory>& Map();
};
