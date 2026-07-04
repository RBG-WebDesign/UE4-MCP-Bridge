#include "AnimBlueprintBuilderLibrary.h"
#include "AnimBlueprintBuilder/ABPBuilder.h"
#include "Animation/AnimBlueprint.h"

FString UAnimBlueprintBuilderLibrary::BuildAnimBlueprintFromJSON(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& SkeletonPath,
	const FString& JsonString)
{
	UE_LOG(LogTemp, Log, TEXT("[AnimBPBuilder] BuildAnimBlueprintFromJSON called"));
	FAnimBPBuilder Builder;
	return Builder.Build(PackagePath, AssetName, SkeletonPath, JsonString);
}

FString UAnimBlueprintBuilderLibrary::RebuildAnimBlueprintFromJSON(
	UAnimBlueprint* AnimBlueprint,
	const FString& JsonString)
{
	UE_LOG(LogTemp, Log, TEXT("[AnimBPBuilder] RebuildAnimBlueprintFromJSON called"));
	if (!AnimBlueprint) return TEXT("[AnimBPBuilder] AnimBlueprint is null");
	FAnimBPBuilder Builder;
	return Builder.Rebuild(AnimBlueprint, JsonString);
}

FString UAnimBlueprintBuilderLibrary::ValidateAnimBlueprintJSON(
	const FString& JsonString)
{
	FAnimBPBuilder Builder;
	return Builder.Validate(JsonString);
}
