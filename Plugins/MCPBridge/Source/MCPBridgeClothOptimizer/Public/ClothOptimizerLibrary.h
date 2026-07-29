// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ClothOptimizerLibrary.generated.h"

class USkeletalMesh;

/** Shared read-only cloth inspection used by the local panel and MCP handlers. */
UCLASS()
class MCPBRIDGECLOTHOPTIMIZER_API UClothOptimizerLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Inspect a skeletal mesh and return a versioned JSON snapshot. */
    UFUNCTION(BlueprintCallable, Category = "MCP Bridge|Cloth Optimizer")
    static FString InspectClothAsset(const FString& SkeletalMeshPath);

    /** Apply a fabric profile without changing cloth paint or the Physics Asset. */
    UFUNCTION(BlueprintCallable, Category = "MCP Bridge|Cloth Optimizer")
    static bool ApplyFabricProfile(
        const FString& SkeletalMeshPath,
        const FString& ClothingAssetName,
        const FString& FabricProfile,
        FString& OutBackupPath,
        FString& OutError);

    /** Smooth the selected Max Distance mask without increasing vertex freedom. */
    UFUNCTION(BlueprintCallable, Category = "MCP Bridge|Cloth Optimizer")
    static bool SmoothMaxDistanceTransition(
        const FString& SkeletalMeshPath,
        const FString& ClothingAssetName,
        int32 Iterations,
        float Strength,
        FString& OutSummary,
        FString& OutBackupPath,
        FString& OutError);

    /** Author a smooth hem-to-knee Max Distance gradient on fitted trousers. */
    UFUNCTION(BlueprintCallable, Category = "MCP Bridge|Cloth Optimizer")
    static bool ApplyLowerLegGradient(
        const FString& SkeletalMeshPath,
        const FString& ClothingAssetName,
        float KneeHeightFraction,
        FString& OutSummary,
        FString& OutBackupPath,
        FString& OutError);

    /** Inspect a loaded skeletal mesh and return a versioned JSON snapshot. */
    static bool BuildClothSnapshot(USkeletalMesh* SkeletalMesh, FString& OutJson, FString& OutError);

    /** Generate a conservative, non-expanding smoothed Max Distance candidate. */
    static bool GenerateMaxDistanceCandidate(
        USkeletalMesh* SkeletalMesh,
        const FString& ClothingAssetPath,
        int32 Iterations,
        float Strength,
        TArray<float>& OutValues,
        FString& OutContentHash,
        FString& OutSummary,
        FString& OutError);

    /** Apply one generated Max Distance candidate after topology validation and backup. */
    static bool ApplyMaxDistanceCandidate(
        USkeletalMesh* SkeletalMesh,
        const FString& ClothingAssetPath,
        const FString& ExpectedContentHash,
        const TArray<float>& Values,
        const FString& FabricProfile,
        FString& OutBackupPath,
        FString& OutError);
};
