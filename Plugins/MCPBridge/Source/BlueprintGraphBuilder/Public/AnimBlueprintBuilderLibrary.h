#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AnimBlueprintBuilderLibrary.generated.h"

class UAnimBlueprint;

UCLASS()
class BLUEPRINTGRAPHBUILDER_API UAnimBlueprintBuilderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a new AnimBlueprint from JSON.
	 * Returns empty string on success, error message on failure.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="BlueprintGraphBuilder")
	static FString BuildAnimBlueprintFromJSON(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& SkeletonPath,
		const FString& JsonString
	);

	/**
	 * Replace the contents of an existing AnimBlueprint from JSON.
	 * Returns empty string on success, error message on failure.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="BlueprintGraphBuilder")
	static FString RebuildAnimBlueprintFromJSON(
		UAnimBlueprint* AnimBlueprint,
		const FString& JsonString
	);

	/**
	 * Validate JSON without creating an asset.
	 * Returns empty string if valid, error message if invalid.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="BlueprintGraphBuilder")
	static FString ValidateAnimBlueprintJSON(
		const FString& JsonString
	);
};
