// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
class UEdGraphPin;
class FJsonObject;

class FBPPinSerializer
{
public:
    static TSharedPtr<FJsonObject> Serialize(const UEdGraphPin* Pin);
};
