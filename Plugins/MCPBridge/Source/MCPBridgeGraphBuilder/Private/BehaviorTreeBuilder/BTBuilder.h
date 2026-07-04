#pragma once

#include "CoreMinimal.h"
#include "BTNodeRegistry.h"

class UBehaviorTree;

class FBTBuilder
{
public:
	/** Build BT runtime tree from JSON. Returns empty string on success, error on failure. */
	FString Build(UBehaviorTree* BT, const FString& JsonString);

private:
	FBTNodeRegistry Registry;
};
