#pragma once
#include "CoreMinimal.h"
class UBlueprint;

class FBPSCSOps
{
public:
    static bool RemoveSCSNode(UBlueprint* BP, const FString& ComponentName);
    static bool RenameSCSNode(UBlueprint* BP, const FString& OldName, const FString& NewName);
};
