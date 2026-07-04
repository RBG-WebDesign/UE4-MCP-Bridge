#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AutoPIEHelper.generated.h"

/**
 * Editor automation helpers exposed to Python/Blueprint.
 * Defers PIE start to the next engine tick so the calling thread
 * (Python game-thread handler) can return first.
 */
UCLASS()
class BLUEPRINTGRAPHBUILDER_API UAutoPIEHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Start a real PIE session on the next engine tick.
	 *  Safe to call from Python -- defers so the handler can finish first. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Automation")
	static void StartPIE();

	/** Stop the current PIE session on the next engine tick. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Automation")
	static void StopPIE();

	/** Returns true if a PIE session is currently active. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Automation")
	static bool IsPIERunning();

	/** Play a camera shake on the first player's camera (editor/Python use).
	 *  Handles the full chain: PIE World -> PlayerController(0) -> PlayerCameraManager -> PlayCameraShake.
	 *  Call from Python during PIE to trigger shakes without needing Cast/Get nodes in the graph.
	 *
	 *  @param ShakeClassPath  Asset path to the CameraShake Blueprint (e.g. "/Game/CS_Earthquake.CS_Earthquake_C")
	 *  @param Scale           Shake intensity multiplier (default 1.0)
	 *  @return True if the shake was played successfully
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Automation")
	static bool PlayCameraShakeOnPlayer(const FString& ShakeClassPath, float Scale = 1.0f);

	/** Play a camera shake by asset path -- callable from Blueprint graphs at runtime.
	 *  Full chain: WorldContext -> GetPlayerController(0) -> PlayerCameraManager -> PlayCameraShake.
	 *  Use this in event graphs to avoid needing Cast/GetPC/GetCamMgr node chains.
	 *
	 *  @param WorldContextObject  Automatic world context (hidden in Blueprint)
	 *  @param ShakeClassPath      Asset path (e.g. "/Game/CS_Earthquake.CS_Earthquake_C")
	 *  @param Scale               Shake intensity multiplier
	 */
	UFUNCTION(BlueprintCallable, Category="Automation", meta=(WorldContext="WorldContextObject"))
	static void PlayCameraShakeByPath(UObject* WorldContextObject, const FString& ShakeClassPath, float Scale = 1.0f);
};
