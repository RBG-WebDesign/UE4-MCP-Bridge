// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PIEAgentLibrary.generated.h"

/**
 * Game-thread entry points for the PIE gameplay agent.
 *
 * Every function accepts or returns JSON so Python remains a thin transport
 * adapter. Long operations run from the C++ ticker and are polled by the Node
 * MCP server. No function blocks the game thread waiting for a future frame.
 */
UCLASS()
class MCPBRIDGEPIEAGENT_API UPIEAgentLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString StartMoveTo(const FString& RequestJson);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString LookAt(const FString& RequestJson);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString StartPress(const FString& RequestJson);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString Observe(const FString& RequestJson);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString ReadProperty(const FString& RequestJson);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString RecordStart(const FString& RequestJson);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString RecordStop();

    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString StartExpect(const FString& RequestJson);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString StartReplay(const FString& RequestJson);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString GetOperationStatus(int32 OperationId);

    // Raw per-key press/release for scripted input harnesses. Unlike
    // StartPress there is no operation bookkeeping and no timed release, so
    // several keys can be held concurrently (e.g. W held while D taps).
    // The caller owns releasing every key it pressed.
    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString SetKeyState(const FString& KeyName, bool bPressed);

    // Hold an analog axis (e.g. Gamepad_LeftX) at a value until cleared; the
    // runtime re-injects it every tick. Value 0 releases that axis.
    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString SetAxisState(const FString& AxisKeyName, float Value);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="MCP Bridge|PIE Agent")
    static FString ClearAxisState();
};
