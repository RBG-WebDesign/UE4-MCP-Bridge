// Copyright 2026 RareBird Games. All Rights Reserved.

#include "SClothOptimizerPanel.h"

#include "AssetRegistry/AssetData.h"
#include "ClothOptimizerLibrary.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/MessageDialog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace ClothOptimizerPanelStyle
{
    static const FLinearColor Background(0.018f, 0.024f, 0.035f, 1.0f);
    static const FLinearColor Card(0.035f, 0.045f, 0.063f, 1.0f);
    static const FLinearColor Border(0.12f, 0.15f, 0.20f, 1.0f);
    static const FLinearColor Text(0.90f, 0.93f, 0.97f, 1.0f);
    static const FLinearColor Muted(0.55f, 0.61f, 0.70f, 1.0f);
    static const FLinearColor Blue(0.10f, 0.42f, 0.82f, 1.0f);
    static const FLinearColor Green(0.19f, 0.74f, 0.45f, 1.0f);
    static const FLinearColor Amber(0.96f, 0.66f, 0.18f, 1.0f);
    static const FLinearColor Red(0.92f, 0.25f, 0.27f, 1.0f);
}

void SClothOptimizerPanel::Construct(const FArguments& InArgs)
{
    FabricProfileOptions = {
        MakeShared<FString>(TEXT("Heavy Denim")),
        MakeShared<FString>(TEXT("Heavy Denim Close Fit")),
        MakeShared<FString>(TEXT("Medium Denim")),
        MakeShared<FString>(TEXT("Medium Trench")),
        MakeShared<FString>(TEXT("Light Fabric")),
        MakeShared<FString>(TEXT("Mask Only"))
    };
    SelectedFabricProfile = FabricProfileOptions[0];

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(ClothOptimizerPanelStyle::Background)
        .Padding(FMargin(20.0f))
        [
            SNew(SScrollBox)
            + SScrollBox::Slot()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Cloth Optimizer")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
                    .ColorAndOpacity(ClothOptimizerPanelStyle::Text)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Analyze UE4.27 NvCloth assets locally. MCP and ranked optimization will use the same inspection service.")))
                    .AutoWrapText(true)
                    .ColorAndOpacity(ClothOptimizerPanelStyle::Muted)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(ClothOptimizerPanelStyle::Card)
                    .Padding(FMargin(14.0f))
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("1. Skeletal Mesh")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                            .ColorAndOpacity(ClothOptimizerPanelStyle::Text)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
                        [
                            SAssignNew(MeshPathText, STextBlock)
                            .Text(FText::FromString(TEXT("No skeletal mesh selected")))
                            .AutoWrapText(true)
                            .ColorAndOpacity(ClothOptimizerPanelStyle::Muted)
                        ]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
                            [
                                SNew(SButton)
                                .Text(FText::FromString(TEXT("Use Content Browser Selection")))
                                .OnClicked(this, &SClothOptimizerPanel::UseContentBrowserSelection)
                            ]
                            + SHorizontalBox::Slot().AutoWidth()
                            [
                                SNew(SButton)
                                .Text(FText::FromString(TEXT("Analyze")))
                                .IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
                                .OnClicked(this, &SClothOptimizerPanel::AnalyzeSelectedMesh)
                            ]
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(ClothOptimizerPanelStyle::Card)
                    .Padding(FMargin(14.0f))
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("2. Choose Cloth Parts")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                            .ColorAndOpacity(ClothOptimizerPanelStyle::Text)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Select only the embedded clothing assets you want to change.")))
                            .AutoWrapText(true)
                            .ColorAndOpacity(ClothOptimizerPanelStyle::Muted)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
                        [
                            SAssignNew(AssetSelectionBox, SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("Analyze a mesh to list its cloth parts.")))
                                .ColorAndOpacity(ClothOptimizerPanelStyle::Muted)
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(STextBlock).Text(FText::FromString(TEXT("Fabric Profile"))).ColorAndOpacity(ClothOptimizerPanelStyle::Muted)
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 8.0f)
                            [
                                SNew(SComboBox<TSharedPtr<FString>>)
                                .OptionsSource(&FabricProfileOptions)
                                .InitiallySelectedItem(SelectedFabricProfile)
                                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                                {
                                    return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
                                })
                                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
                                {
                                    SelectedFabricProfile = Item;
                                    Candidates.Reset();
                                    RebuildCandidateSummary();
                                })
                                [
                                    SNew(STextBlock)
                                    .Text_Lambda([this]() { return FText::FromString(SelectedFabricProfile.IsValid() ? *SelectedFabricProfile : TEXT("Mask Only")); })
                                ]
                            ]
                            + SVerticalBox::Slot().AutoHeight()
                            [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().FillWidth(0.5f).Padding(0.0f, 0.0f, 10.0f, 0.0f)
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("Smoothing Passes"))).ColorAndOpacity(ClothOptimizerPanelStyle::Muted)]
                                + SVerticalBox::Slot().AutoHeight()
                                [
                                    SNew(SSpinBox<int32>)
                                    .MinValue(1).MaxValue(12).Value_Lambda([this]() { return SmoothingIterations; })
                                    .OnValueChanged_Lambda([this](int32 Value) { SmoothingIterations = Value; Candidates.Reset(); RebuildCandidateSummary(); })
                                ]
                            ]
                            + SHorizontalBox::Slot().FillWidth(0.5f)
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("Strength"))).ColorAndOpacity(ClothOptimizerPanelStyle::Muted)]
                                + SVerticalBox::Slot().AutoHeight()
                                [
                                    SNew(SSpinBox<float>)
                                    .MinValue(0.05f).MaxValue(1.0f).Delta(0.05f).Value_Lambda([this]() { return SmoothingStrength; })
                                    .OnValueChanged_Lambda([this](float Value) { SmoothingStrength = Value; Candidates.Reset(); RebuildCandidateSummary(); })
                                ]
                            ]
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 10.0f)
                        [
                            SNew(SButton)
                            .Text(FText::FromString(TEXT("Optimize Selected (Conservative)")))
                            .IsEnabled_Lambda([this]() { return SelectedMesh.IsValid() && HasSelectedAssets(); })
                            .OnClicked(this, &SClothOptimizerPanel::OptimizeSelectedAssets)
                        ]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SAssignNew(CandidateBox, SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("No candidate generated.")))
                                .ColorAndOpacity(ClothOptimizerPanelStyle::Muted)
                            ]
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(ClothOptimizerPanelStyle::Card)
                    .Padding(FMargin(14.0f))
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Analysis")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                            .ColorAndOpacity(ClothOptimizerPanelStyle::Text)
                        ]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SAssignNew(ResultsBox, SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("Choose a skeletal mesh and click Analyze.")))
                                .ColorAndOpacity(ClothOptimizerPanelStyle::Muted)
                            ]
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(ClothOptimizerPanelStyle::Card)
                    .Padding(FMargin(14.0f))
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("3. Review and Apply")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                            .ColorAndOpacity(ClothOptimizerPanelStyle::Text)
                        ]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("This candidate only smooths existing Max Distance values and never increases vertex freedom. Full animation-scored optimization remains a later phase.")))
                            .AutoWrapText(true)
                            .ColorAndOpacity(ClothOptimizerPanelStyle::Muted)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
                        [
                            SNew(SButton)
                            .Text(FText::FromString(TEXT("Apply Selected Candidates")))
                            .IsEnabled_Lambda([this]() { return SelectedMesh.IsValid() && HasSelectedCandidates(); })
                            .OnClicked(this, &SClothOptimizerPanel::ApplySelectedCandidates)
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(StatusText, STextBlock)
                    .Text(FText::FromString(TEXT("Ready")))
                    .ColorAndOpacity(ClothOptimizerPanelStyle::Muted)
                ]
            ]
        ]
    ];

    UseContentBrowserSelection();
}

FReply SClothOptimizerPanel::UseContentBrowserSelection()
{
    TArray<FAssetData> SelectedAssets;
    if (GEditor)
    {
        GEditor->GetContentBrowserSelections(SelectedAssets);
    }

    SelectedMesh.Reset();
    for (const FAssetData& AssetData : SelectedAssets)
    {
        if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(AssetData.GetAsset()))
        {
            SelectedMesh = Mesh;
            break;
        }
    }

    if (SelectedMesh.IsValid())
    {
        MeshPathText->SetText(FText::FromString(SelectedMesh->GetPathName()));
        SetStatus(TEXT("Skeletal mesh loaded. Click Analyze to run cloth preflight."), ClothOptimizerPanelStyle::Green);
    }
    else
    {
        MeshPathText->SetText(FText::FromString(TEXT("No skeletal mesh selected")));
        SetStatus(TEXT("Select a skeletal mesh in the Content Browser."), ClothOptimizerPanelStyle::Amber);
    }
    return FReply::Handled();
}

FReply SClothOptimizerPanel::AnalyzeSelectedMesh()
{
    if (!SelectedMesh.IsValid())
    {
        SetStatus(TEXT("No skeletal mesh is selected."), ClothOptimizerPanelStyle::Red);
        return FReply::Handled();
    }

    FString Snapshot;
    FString Error;
    if (!UClothOptimizerLibrary::BuildClothSnapshot(SelectedMesh.Get(), Snapshot, Error))
    {
        SetStatus(Error, ClothOptimizerPanelStyle::Red);
        return FReply::Handled();
    }

    RebuildResults(Snapshot);
    SetStatus(TEXT("Analysis complete. No assets were modified."), ClothOptimizerPanelStyle::Green);
    return FReply::Handled();
}

bool SClothOptimizerPanel::HasSelectedAssets() const
{
    return AssetChoices.ContainsByPredicate([](const TSharedPtr<FClothOptimizerAssetChoice>& Choice)
    {
        return Choice.IsValid() && Choice->bSelected;
    });
}

bool SClothOptimizerPanel::HasSelectedCandidates() const
{
    return AssetChoices.ContainsByPredicate([this](const TSharedPtr<FClothOptimizerAssetChoice>& Choice)
    {
        return Choice.IsValid() && Choice->bSelected && Candidates.Contains(Choice->Path);
    });
}

FReply SClothOptimizerPanel::OptimizeSelectedAssets()
{
    Candidates.Reset();
    for (const TSharedPtr<FClothOptimizerAssetChoice>& Choice : AssetChoices)
    {
        if (!Choice.IsValid() || !Choice->bSelected)
        {
            continue;
        }

        FClothOptimizerCandidate Candidate;
        Candidate.FabricProfile = SelectedFabricProfile.IsValid() ? *SelectedFabricProfile : TEXT("Mask Only");
        FString Error;
        if (!UClothOptimizerLibrary::GenerateMaxDistanceCandidate(
            SelectedMesh.Get(),
            Choice->Path,
            SmoothingIterations,
            SmoothingStrength,
            Candidate.Values,
            Candidate.ContentHash,
            Candidate.Summary,
            Error))
        {
            SetStatus(FString::Printf(TEXT("%s: %s"), *Choice->Name, *Error), ClothOptimizerPanelStyle::Red);
            RebuildCandidateSummary();
            return FReply::Handled();
        }
        Candidates.Add(Choice->Path, MoveTemp(Candidate));
    }

    RebuildCandidateSummary();
    SetStatus(TEXT("Candidate generation complete. Review the summaries before applying."), ClothOptimizerPanelStyle::Green);
    return FReply::Handled();
}

FReply SClothOptimizerPanel::ApplySelectedCandidates()
{
    if (FMessageDialog::Open(
        EAppMsgType::YesNo,
        FText::FromString(TEXT("Apply the generated Max Distance candidates to the selected clothing assets? A JSON backup will be written before each change, and the operation supports Undo."))) != EAppReturnType::Yes)
    {
        return FReply::Handled();
    }

    TArray<FString> BackupPaths;
    for (const TSharedPtr<FClothOptimizerAssetChoice>& Choice : AssetChoices)
    {
        if (!Choice.IsValid() || !Choice->bSelected)
        {
            continue;
        }
        const FClothOptimizerCandidate* Candidate = Candidates.Find(Choice->Path);
        if (!Candidate)
        {
            continue;
        }

        FString BackupPath;
        FString Error;
        if (!UClothOptimizerLibrary::ApplyMaxDistanceCandidate(
            SelectedMesh.Get(),
            Choice->Path,
            Candidate->ContentHash,
            Candidate->Values,
            Candidate->FabricProfile,
            BackupPath,
            Error))
        {
            SetStatus(FString::Printf(TEXT("%s: %s"), *Choice->Name, *Error), ClothOptimizerPanelStyle::Red);
            return FReply::Handled();
        }
        BackupPaths.Add(BackupPath);
    }

    Candidates.Reset();
    RebuildCandidateSummary();
    AnalyzeSelectedMesh();
    SetStatus(FString::Printf(TEXT("Applied selected candidates. %d backup file(s) written under Saved/ClothMCP/Backups."), BackupPaths.Num()), ClothOptimizerPanelStyle::Green);
    return FReply::Handled();
}

void SClothOptimizerPanel::SetStatus(const FString& Message, const FLinearColor& Color)
{
    if (StatusText.IsValid())
    {
        StatusText->SetText(FText::FromString(Message));
        StatusText->SetColorAndOpacity(Color);
    }
}

void SClothOptimizerPanel::AddResultLine(const FString& Label, const FString& Value, const FLinearColor& ValueColor)
{
    ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.42f)
        [
            SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(ClothOptimizerPanelStyle::Muted)
        ]
        + SHorizontalBox::Slot().FillWidth(0.58f)
        [
            SNew(STextBlock).Text(FText::FromString(Value)).AutoWrapText(true).ColorAndOpacity(ValueColor)
        ]
    ];
}

void SClothOptimizerPanel::RebuildResults(const FString& SnapshotJson)
{
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SnapshotJson);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        SetStatus(TEXT("The cloth snapshot could not be parsed."), ClothOptimizerPanelStyle::Red);
        return;
    }

    ResultsBox->ClearChildren();
    const TArray<TSharedPtr<FJsonValue>>* ClothAssets = nullptr;
    Root->TryGetArrayField(TEXT("clothAssets"), ClothAssets);
    const int32 ClothAssetCount = ClothAssets ? ClothAssets->Num() : 0;

    TSet<FString> PreviouslySelectedPaths;
    for (const TSharedPtr<FClothOptimizerAssetChoice>& ExistingChoice : AssetChoices)
    {
        if (ExistingChoice.IsValid() && ExistingChoice->bSelected)
        {
            PreviouslySelectedPaths.Add(ExistingChoice->Path);
        }
    }
    AssetChoices.Reset();
    Candidates.Reset();
    if (ClothAssets)
    {
        for (const TSharedPtr<FJsonValue>& AssetValue : *ClothAssets)
        {
            const TSharedPtr<FJsonObject> Asset = AssetValue.IsValid() ? AssetValue->AsObject() : nullptr;
            if (Asset.IsValid())
            {
                TSharedPtr<FClothOptimizerAssetChoice> Choice = MakeShared<FClothOptimizerAssetChoice>();
                Choice->Name = Asset->GetStringField(TEXT("name"));
                Choice->Path = Asset->GetStringField(TEXT("path"));
                Choice->bSelected = PreviouslySelectedPaths.Contains(Choice->Path);
                AssetChoices.Add(Choice);
            }
        }
    }
    RebuildAssetSelection();
    RebuildCandidateSummary();
    AddResultLine(TEXT("Compatibility"), ClothAssetCount > 0 ? TEXT("NvCloth asset detected") : TEXT("No compatible cloth asset"), ClothAssetCount > 0 ? ClothOptimizerPanelStyle::Green : ClothOptimizerPanelStyle::Red);
    AddResultLine(TEXT("Skeletal Mesh"), Root->GetStringField(TEXT("skeletalMesh")), ClothOptimizerPanelStyle::Text);
    AddResultLine(TEXT("LOD Count"), FString::FromInt(static_cast<int32>(Root->GetNumberField(TEXT("lodCount")))), ClothOptimizerPanelStyle::Text);
    AddResultLine(TEXT("Cloth Assets"), FString::FromInt(ClothAssetCount), ClothAssetCount > 0 ? ClothOptimizerPanelStyle::Green : ClothOptimizerPanelStyle::Red);

    const TSharedPtr<FJsonObject>* Physics = nullptr;
    if (Root->TryGetObjectField(TEXT("meshPhysicsAsset"), Physics) && Physics && Physics->IsValid())
    {
        AddResultLine(TEXT("Physics Asset"), (*Physics)->GetStringField(TEXT("path")), (*Physics)->GetStringField(TEXT("path")).IsEmpty() ? ClothOptimizerPanelStyle::Red : ClothOptimizerPanelStyle::Text);
        AddResultLine(TEXT("Bodies / Constraints"), FString::Printf(TEXT("%d / %d"), static_cast<int32>((*Physics)->GetNumberField(TEXT("bodyCount"))), static_cast<int32>((*Physics)->GetNumberField(TEXT("constraintCount")))), ClothOptimizerPanelStyle::Text);
    }

    if (!ClothAssets)
    {
        return;
    }

    for (const TSharedPtr<FJsonValue>& AssetValue : *ClothAssets)
    {
        const TSharedPtr<FJsonObject> Asset = AssetValue.IsValid() ? AssetValue->AsObject() : nullptr;
        if (!Asset.IsValid())
        {
            continue;
        }
        ResultsBox->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 6.0f)[SNew(SSeparator)];
        AddResultLine(TEXT("Clothing Asset"), Asset->GetStringField(TEXT("name")), ClothOptimizerPanelStyle::Blue);
        AddResultLine(TEXT("Topology Hash"), Asset->GetStringField(TEXT("contentHash")), ClothOptimizerPanelStyle::Muted);

        const TArray<TSharedPtr<FJsonValue>>* Lods = nullptr;
        if (Asset->TryGetArrayField(TEXT("lods"), Lods) && Lods)
        {
            for (const TSharedPtr<FJsonValue>& LodValue : *Lods)
            {
                const TSharedPtr<FJsonObject> Lod = LodValue.IsValid() ? LodValue->AsObject() : nullptr;
                if (!Lod.IsValid())
                {
                    continue;
                }
                AddResultLine(
                    FString::Printf(TEXT("Cloth LOD %d"), static_cast<int32>(Lod->GetNumberField(TEXT("lod")))),
                    FString::Printf(TEXT("%d physical vertices, %d triangles, %d fixed"), static_cast<int32>(Lod->GetNumberField(TEXT("physicalVertexCount"))), static_cast<int32>(Lod->GetNumberField(TEXT("triangleCount"))), static_cast<int32>(Lod->GetNumberField(TEXT("fixedVertexCount")))),
                    ClothOptimizerPanelStyle::Text);

                const TArray<TSharedPtr<FJsonValue>>* Masks = nullptr;
                if (Lod->TryGetArrayField(TEXT("masks"), Masks) && Masks)
                {
                    for (const TSharedPtr<FJsonValue>& MaskValue : *Masks)
                    {
                        const TSharedPtr<FJsonObject> Mask = MaskValue.IsValid() ? MaskValue->AsObject() : nullptr;
                        if (!Mask.IsValid())
                        {
                            continue;
                        }
                        const bool bValidLength = Mask->GetBoolField(TEXT("validLength"));
                        AddResultLine(
                            FString::Printf(TEXT("Mask: %s"), *Mask->GetStringField(TEXT("target"))),
                            FString::Printf(TEXT("%d values, %.3f to %.3f%s"), static_cast<int32>(Mask->GetNumberField(TEXT("valueCount"))), Mask->GetNumberField(TEXT("minimum")), Mask->GetNumberField(TEXT("maximum")), bValidLength ? TEXT("") : TEXT(" (invalid length)")),
                            bValidLength ? ClothOptimizerPanelStyle::Green : ClothOptimizerPanelStyle::Red);
                    }
                }
            }
        }
    }
}

void SClothOptimizerPanel::RebuildAssetSelection()
{
    if (!AssetSelectionBox.IsValid())
    {
        return;
    }
    AssetSelectionBox->ClearChildren();
    if (AssetChoices.Num() == 0)
    {
        AssetSelectionBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("No compatible clothing assets found."))).ColorAndOpacity(ClothOptimizerPanelStyle::Red)];
        return;
    }

    for (const TSharedPtr<FClothOptimizerAssetChoice>& Choice : AssetChoices)
    {
        AssetSelectionBox->AddSlot().AutoHeight().Padding(0.0f, 3.0f)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([Choice]() { return Choice->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Choice](ECheckBoxState State)
            {
                Choice->bSelected = State == ECheckBoxState::Checked;
                Candidates.Remove(Choice->Path);
                RebuildCandidateSummary();
            })
            [
                SNew(STextBlock)
                .Text(FText::FromString(Choice->Name))
                .ColorAndOpacity(ClothOptimizerPanelStyle::Text)
            ]
        ];
    }
}

void SClothOptimizerPanel::RebuildCandidateSummary()
{
    if (!CandidateBox.IsValid())
    {
        return;
    }
    CandidateBox->ClearChildren();
    if (Candidates.Num() == 0)
    {
        CandidateBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("No candidate generated."))).ColorAndOpacity(ClothOptimizerPanelStyle::Muted)];
        return;
    }
    for (const TSharedPtr<FClothOptimizerAssetChoice>& Choice : AssetChoices)
    {
        if (!Choice.IsValid())
        {
            continue;
        }
        const FClothOptimizerCandidate* Candidate = Candidates.Find(Choice->Path);
        if (!Candidate)
        {
            continue;
        }
        CandidateBox->AddSlot().AutoHeight().Padding(0.0f, 3.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(FString::Printf(TEXT("%s [%s]: %s"), *Choice->Name, *Candidate->FabricProfile, *Candidate->Summary)))
            .AutoWrapText(true)
            .ColorAndOpacity(ClothOptimizerPanelStyle::Green)
        ];
    }
}
