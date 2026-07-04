#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WidgetBlueprintBuilderLibrary.generated.h"

class UWidgetBlueprint;

UCLASS()
class BLUEPRINTGRAPHBUILDER_API UWidgetBlueprintBuilderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a new UWidgetBlueprint from JSON.
	 * Returns empty string on success, error message on failure.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="BlueprintGraphBuilder")
	static FString BuildWidgetFromJSON(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& JsonString
	);

	/**
	 * Replace the widget tree of an existing UWidgetBlueprint from JSON.
	 * Returns empty string on success, error message on failure.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="BlueprintGraphBuilder")
	static FString RebuildWidgetFromJSON(
		UWidgetBlueprint* WidgetBlueprint,
		const FString& JsonString
	);

	/**
	 * Validate JSON without creating an asset.
	 * Returns empty string if valid, error message if invalid.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="BlueprintGraphBuilder")
	static FString ValidateWidgetJSON(
		const FString& JsonString
	);
};
