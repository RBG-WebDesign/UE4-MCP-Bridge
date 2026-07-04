#pragma once
#include "CoreMinimal.h"
class UEdGraphNode;
class FJsonObject;

class FBPNodeSerializer
{
public:
    static TSharedPtr<FJsonObject> Serialize(UEdGraphNode* Node);
};
