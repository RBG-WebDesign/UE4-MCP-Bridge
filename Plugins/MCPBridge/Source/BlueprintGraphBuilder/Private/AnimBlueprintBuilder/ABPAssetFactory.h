#pragma once

#include "CoreMinimal.h"

class UAnimBlueprint;
class USkeleton;

class FAnimBPAssetFactory
{
public:
	static UAnimBlueprint* Create(
		const FString& PackagePath,
		const FString& AssetName,
		USkeleton* Skeleton,
		FString& OutError
	);

	static USkeleton* ResolveSkeleton(const FString& SkeletonPath, FString& OutError);
};
