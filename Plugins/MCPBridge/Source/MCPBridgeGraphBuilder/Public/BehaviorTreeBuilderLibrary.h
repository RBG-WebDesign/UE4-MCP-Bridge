// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BehaviorTreeBuilderLibrary.generated.h"

class UBehaviorTree;

UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UBehaviorTreeBuilderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Build a BehaviorTree node graph from JSON.
	 * The BehaviorTree must already exist and have its BlackboardAsset assigned.
	 * Returns empty string on success, error message on failure.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="BlueprintGraphBuilder")
	static FString BuildBehaviorTreeFromJSON(
		UBehaviorTree* BehaviorTree,
		const FString& JsonString
	);
};
