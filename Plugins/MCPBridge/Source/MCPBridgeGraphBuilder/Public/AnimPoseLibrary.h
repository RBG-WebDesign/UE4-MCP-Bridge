// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AnimPoseLibrary.generated.h"

class UAnimSequence;

/**
 * Write path for animation pose re-anchoring.
 *
 * Deliberately thin. Every decision -- which bones, what the correction is,
 * how strongly it applies to each key -- is made in Python
 * (mcp_bridge/utils/anim_math.py and handlers/animation.py) where it is unit
 * tested without an editor. This class receives an already-resolved plan and
 * applies it.
 *
 * That split exists because UE4.27 exposes no BlueprintCallable way to write
 * bone tracks: UAnimationBlueprintLibrary has RemoveBoneAnimation and
 * FinalizeBoneAnimation but no AddBoneAnimation. The only mutation hook is
 * GetRawAnimationTrackByName, which returns a mutable reference and is not
 * BlueprintCallable, so this much and no more has to live in C++.
 */
UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UAnimPoseLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Apply a re-anchor plan to an AnimSequence's raw rotation tracks.
	 *
	 * PlanJSON shape:
	 * {
	 *   "bones": [
	 *     {
	 *       "bone": "clavicle_l",
	 *       "delta_quat": [x, y, z, w],   // rotation taking target onto reference
	 *       "weights": [1.0, 0.98, ...]   // one per key, already profile-shaped
	 *     }
	 *   ],
	 *   "num_frames": 61
	 * }
	 *
	 * A bone whose weights array is longer than its current key count has that
	 * track expanded to match first, by repeating the existing keys. That is
	 * how a constant (single key) track accepts a time-varying correction.
	 *
	 * Returns a JSON report. On success: {"success": true, "bones_written": N,
	 * "keys_written": N, "tracks_expanded": [...]}. On failure:
	 * {"success": false, "error": "..."}. Never throws across the Python
	 * boundary.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="AnimPose")
	static FString ApplyReanchorPlan(UAnimSequence* Target, const FString& PlanJSON);

	/**
	 * Parse a plan without touching the asset. Returns the same report shape
	 * with "dry_run": true, so the Python layer can confirm the C++ side agrees
	 * with its own accounting before anything is written.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="AnimPose")
	static FString ValidateReanchorPlan(UAnimSequence* Target, const FString& PlanJSON);
};
