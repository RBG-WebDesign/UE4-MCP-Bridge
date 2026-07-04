#pragma once

#include "CoreMinimal.h"
#include "ABPNodeRegistry.h"

class UAnimBlueprint;

class FAnimBPBuilder
{
public:
	FString Build(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& SkeletonPath,
		const FString& JsonString
	);

	FString Rebuild(UAnimBlueprint* AnimBP, const FString& JsonString);
	FString Validate(const FString& JsonString);

private:
	FAnimBPNodeRegistry Registry;
};
