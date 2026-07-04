#pragma once

#include "CoreMinimal.h"

class UBehaviorTree;

class FBTEditorGraphSync
{
public:
	/** Reconstruct editor graph from runtime tree. Non-fatal -- logs warnings on failure. */
	static void Sync(UBehaviorTree* BT);
};
