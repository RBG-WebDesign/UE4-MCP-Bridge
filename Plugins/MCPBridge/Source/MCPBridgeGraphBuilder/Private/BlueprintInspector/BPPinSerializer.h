#pragma once
#include "CoreMinimal.h"
class UEdGraphPin;
class FJsonObject;

class FBPPinSerializer
{
public:
    static TSharedPtr<FJsonObject> Serialize(const UEdGraphPin* Pin);
};
