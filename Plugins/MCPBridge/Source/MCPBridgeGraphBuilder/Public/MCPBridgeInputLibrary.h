// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPBridgeInputLibrary.generated.h"

/**
 * Input mapping helpers for the MCP bridge.
 *
 * UE4.27 Python cannot construct FKey (the struct exposes no init
 * parameters to the reflection layer), so action/axis mapping mutation
 * has to happen in C++. Key names use FKey string form, e.g. "SpaceBar",
 * "W", "LeftMouseButton", "Gamepad_LeftX", "MouseX".
 */
UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UMCPBridgeInputLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Add an action mapping. Returns empty string on success, error message on failure. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MCPBridgeInput")
	static FString AddActionMapping(const FString& ActionName, const FString& KeyName,
		bool bShift = false, bool bCtrl = false, bool bAlt = false, bool bCmd = false);

	/** Remove action mappings by name. Empty KeyName removes all keys for the action. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MCPBridgeInput")
	static FString RemoveActionMapping(const FString& ActionName, const FString& KeyName);

	/** Add an axis mapping with scale. Returns empty string on success. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MCPBridgeInput")
	static FString AddAxisMapping(const FString& AxisName, const FString& KeyName, float Scale = 1.0f);

	/** Remove axis mappings by name. Empty KeyName removes all keys for the axis. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MCPBridgeInput")
	static FString RemoveAxisMapping(const FString& AxisName, const FString& KeyName);

	/** Validate that a key name resolves to a real FKey. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MCPBridgeInput")
	static bool IsValidKeyName(const FString& KeyName);
};
