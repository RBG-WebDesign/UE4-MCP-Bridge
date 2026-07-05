// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPBridgeAILibrary.generated.h"

class UBehaviorTree;
class UBlackboardData;

/**
 * AI asset helpers for the MCP bridge.
 *
 * UE4.27 protects UBehaviorTree::BlackboardAsset and FBlackboardEntry from
 * Python reflection, so blackboard wiring happens here. Key types: Bool,
 * Int, Float, String, Name, Vector, Rotator, Object, Class.
 */
UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UMCPBridgeAILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Add keys to a BlackboardData asset. KeysJson is an array:
	 * [{"name": "TargetActor", "type": "Object", "base_class": "/Script/Engine.Actor"}, ...]
	 * Existing keys with the same name are skipped (idempotent).
	 * Returns empty string on success, error message on failure.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MCPBridgeAI")
	static FString AddBlackboardKeys(UBlackboardData* Blackboard, const FString& KeysJson);

	/** Assign the blackboard asset on a BehaviorTree. Returns empty string on success. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MCPBridgeAI")
	static FString SetBlackboardAsset(UBehaviorTree* BehaviorTree, UBlackboardData* Blackboard);

	/** List key names and types on a BlackboardData asset as JSON. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MCPBridgeAI")
	static FString ListBlackboardKeys(UBlackboardData* Blackboard);
};
