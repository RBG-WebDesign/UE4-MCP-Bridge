#pragma once

#include "CoreMinimal.h"

struct FBTBuildSpec;
struct FBTNodeSpec;
class FBTNodeRegistry;
class UBlackboardData;

class FBTValidator
{
public:
	/** Validate spec against registry and blackboard. Returns accumulated errors. */
	static TArray<FString> Validate(
		const FBTBuildSpec& Spec,
		const FBTNodeRegistry& Registry,
		UBlackboardData* Blackboard
	);

private:
	static void ValidateNode(
		const FBTNodeSpec& Node,
		const FBTNodeRegistry& Registry,
		UBlackboardData* Blackboard,
		TSet<FString>& SeenIds,
		const FString& Path,
		TArray<FString>& OutErrors
	);
};
