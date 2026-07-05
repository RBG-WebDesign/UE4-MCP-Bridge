// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
class UEdGraphNode;
class FJsonObject;

class FBPNodeSerializer
{
public:
    static TSharedPtr<FJsonObject> Serialize(UEdGraphNode* Node);
};
