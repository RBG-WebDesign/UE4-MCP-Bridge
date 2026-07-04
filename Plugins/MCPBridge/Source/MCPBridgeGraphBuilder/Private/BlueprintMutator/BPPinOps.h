#pragma once
#include "CoreMinimal.h"

class UBlueprint;

class FBPPinOps
{
public:
    static bool ConnectPins(UBlueprint* BP, const FString& GraphName,
        const FString& SrcNodeGuid, const FString& SrcPinName,
        const FString& DstNodeGuid, const FString& DstPinName);
};
