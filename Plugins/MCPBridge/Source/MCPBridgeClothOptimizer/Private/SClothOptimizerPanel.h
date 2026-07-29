// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class SVerticalBox;
class USkeletalMesh;

struct FClothOptimizerAssetChoice
{
    FString Name;
    FString Path;
    bool bSelected = false;
};

struct FClothOptimizerCandidate
{
    FString ContentHash;
    FString Summary;
    FString FabricProfile;
    TArray<float> Values;
};

/** Local, dockable cloth analysis and conservative fabric-profile panel. */
class SClothOptimizerPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SClothOptimizerPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TWeakObjectPtr<USkeletalMesh> SelectedMesh;
    TSharedPtr<STextBlock> MeshPathText;
    TSharedPtr<STextBlock> StatusText;
    TSharedPtr<SVerticalBox> ResultsBox;
    TSharedPtr<SVerticalBox> AssetSelectionBox;
    TSharedPtr<SVerticalBox> CandidateBox;
    TArray<TSharedPtr<FClothOptimizerAssetChoice>> AssetChoices;
    TMap<FString, FClothOptimizerCandidate> Candidates;
    TArray<TSharedPtr<FString>> FabricProfileOptions;
    TSharedPtr<FString> SelectedFabricProfile;
    int32 SmoothingIterations = 3;
    float SmoothingStrength = 0.25f;

    FReply UseContentBrowserSelection();
    FReply AnalyzeSelectedMesh();
    FReply OptimizeSelectedAssets();
    FReply ApplySelectedCandidates();
    bool HasSelectedAssets() const;
    bool HasSelectedCandidates() const;
    void SetStatus(const FString& Message, const FLinearColor& Color);
    void RebuildResults(const FString& SnapshotJson);
    void RebuildAssetSelection();
    void RebuildCandidateSummary();
    void AddResultLine(const FString& Label, const FString& Value, const FLinearColor& ValueColor);
};
