// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Panel/MCPBridgePanelTheme.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DECLARE_DELEGATE_OneParam(FMCPOnQueueCommand, const FString&);

/**
 * Actions card, tiered by importance:
 *   1. Protocol - the agent-handoff workflow (primary styled, on top).
 *   2. Bridge   - diagnostics and result handoffs.
 *   3. Utilities - low-priority editor conveniences (compact footer row).
 * Command dispatch stays with the owning panel via OnQueueCommand.
 */
class SMCPBridgeActionsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMCPBridgeActionsPanel)
		: _bCompactLayout(false)
	{}
		SLATE_ARGUMENT(TSharedPtr<FMCPBridgePanelTheme>, Theme)
		SLATE_ARGUMENT(bool, bCompactLayout)
		SLATE_EVENT(FMCPOnQueueCommand, OnQueueCommand)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Theme = InArgs._Theme.ToSharedRef();
		OnQueueCommand = InArgs._OnQueueCommand;
		bCompactLayout = InArgs._bCompactLayout;

		ChildSlot
		[
			FMCPBridgePanelTheme::MakeCard(Theme.ToSharedRef(), TEXT("Actions"), TEXT("Agent handoff first, utilities below."),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))
				[
					MakeProtocolTier()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 14.0f))
				[
					MakeBridgeTier()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MakeUtilitiesTier()
				])
		];
	}

private:
	TSharedPtr<FMCPBridgePanelTheme> Theme;
	FMCPOnQueueCommand OnQueueCommand;
	bool bCompactLayout = false;

	/** Tier 1: the core agent protocol hooks, visually dominant. */
	TSharedRef<SWidget> MakeProtocolTier()
	{
		if (bCompactLayout)
		{
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakePrimaryActionButton(TEXT("Copy Chat Handoff"), TEXT("bridge_copy_connection_prompt"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakePrimaryActionButton(TEXT("Copy Project Handoff"), TEXT("bridge_copy_project_info"))]
				+ SVerticalBox::Slot().AutoHeight()[MakePrimaryActionButton(TEXT("Test Connection"), TEXT("test_connection"))];
		}

		return SNew(SUniformGridPanel)
			.SlotPadding(Theme->ScaledMargin(6.0f))
			+ SUniformGridPanel::Slot(0, 0)[MakePrimaryActionButton(TEXT("Copy Chat Handoff"), TEXT("bridge_copy_connection_prompt"))]
			+ SUniformGridPanel::Slot(1, 0)[MakePrimaryActionButton(TEXT("Copy Project Handoff"), TEXT("bridge_copy_project_info"))]
			+ SUniformGridPanel::Slot(2, 0)[MakePrimaryActionButton(TEXT("Test Connection"), TEXT("test_connection"))];
	}

	/** Tier 2: diagnostics and result handoffs. */
	TSharedRef<SWidget> MakeBridgeTier()
	{
		if (bCompactLayout)
		{
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeSecondaryActionButton(TEXT("Run Self-Test"), TEXT("bridge_self_test"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeSecondaryActionButton(TEXT("Copy Latest Result"), TEXT("bridge_copy_latest_result"))]
				+ SVerticalBox::Slot().AutoHeight()[MakeSecondaryActionButton(TEXT("Copy Recovery Command"), TEXT("bridge_copy_recovery_command"))];
		}

		return SNew(SUniformGridPanel)
			.SlotPadding(Theme->ScaledMargin(6.0f))
			+ SUniformGridPanel::Slot(0, 0)[MakeSecondaryActionButton(TEXT("Run Self-Test"), TEXT("bridge_self_test"))]
			+ SUniformGridPanel::Slot(1, 0)[MakeSecondaryActionButton(TEXT("Copy Latest Result"), TEXT("bridge_copy_latest_result"))]
			+ SUniformGridPanel::Slot(2, 0)[MakeSecondaryActionButton(TEXT("Copy Recovery Command"), TEXT("bridge_copy_recovery_command"))];
	}

	/** Tier 3: editor conveniences, deliberately quiet. */
	TSharedRef<SWidget> MakeUtilitiesTier()
	{
		TSharedRef<SWidget> Buttons = bCompactLayout
			? StaticCastSharedRef<SWidget>(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 6.0f))[MakeUtilityButton(TEXT("Screenshot"), TEXT("viewport_screenshot"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 6.0f))[MakeUtilityButton(TEXT("Save Current Level"), TEXT("level_save"))]
				+ SVerticalBox::Slot().AutoHeight()[MakeUtilityButton(TEXT("Open Output Log"), TEXT("bridge_open_output_log"))])
			: StaticCastSharedRef<SWidget>(
				SNew(SUniformGridPanel)
				.SlotPadding(Theme->ScaledMargin(4.0f))
				+ SUniformGridPanel::Slot(0, 0)[MakeUtilityButton(TEXT("Screenshot"), TEXT("viewport_screenshot"))]
				+ SUniformGridPanel::Slot(1, 0)[MakeUtilityButton(TEXT("Save Current Level"), TEXT("level_save"))]
				+ SUniformGridPanel::Slot(2, 0)[MakeUtilityButton(TEXT("Open Output Log"), TEXT("bridge_open_output_log"))]);

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 6.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(Theme->ScaledMargin(0.0f, 0.0f, 8.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Utilities")))
					.Font(Theme->FontBold(11))
					.ColorAndOpacity(Theme->MutedColor())
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.HeightOverride(1.0f)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(Theme->CardBorderColor)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				Buttons
			];
	}

	FReply HandleClick(const FString CommandName)
	{
		OnQueueCommand.ExecuteIfBound(CommandName);
		return FReply::Handled();
	}

	TSharedRef<SWidget> MakePrimaryActionButton(const FString& Label, const FString& CommandName)
	{
		return SNew(SBox)
			.HeightOverride(Theme->ScaledSize(52.0f))
			.MinDesiredWidth(Theme->ScaledSize(150.0f))
			[
				SNew(SButton)
				.ButtonStyle(&Theme->PrimaryButtonStyle)
				.ContentPadding(Theme->ScaledMargin(12.0f, 0.0f, 12.0f, 0.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked(FOnClicked::CreateSP(this, &SMCPBridgeActionsPanel::HandleClick, CommandName))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(Theme->FontBold(13))
					.Justification(ETextJustify::Center)
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
					.AutoWrapText(true)
				]
			];
	}

	TSharedRef<SWidget> MakeSecondaryActionButton(const FString& Label, const FString& CommandName)
	{
		return SNew(SBox)
			.HeightOverride(Theme->ScaledSize(46.0f))
			.MinDesiredWidth(Theme->ScaledSize(150.0f))
			[
				SNew(SButton)
				.ButtonStyle(&Theme->SecondaryButtonStyle)
				.ContentPadding(Theme->ScaledMargin(12.0f, 0.0f, 12.0f, 0.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked(FOnClicked::CreateSP(this, &SMCPBridgeActionsPanel::HandleClick, CommandName))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(Theme->FontBold(13))
					.Justification(ETextJustify::Center)
					.ColorAndOpacity(Theme->PrimaryColor())
					.AutoWrapText(true)
				]
			];
	}

	TSharedRef<SWidget> MakeUtilityButton(const FString& Label, const FString& CommandName)
	{
		return SNew(SBox)
			.HeightOverride(Theme->ScaledSize(32.0f))
			.MinDesiredWidth(Theme->ScaledSize(120.0f))
			[
				SNew(SButton)
				.ButtonStyle(&Theme->SecondaryButtonStyle)
				.ContentPadding(Theme->ScaledMargin(10.0f, 0.0f, 10.0f, 0.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked(FOnClicked::CreateSP(this, &SMCPBridgeActionsPanel::HandleClick, CommandName))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(Theme->FontRegular(12))
					.Justification(ETextJustify::Center)
					.ColorAndOpacity(Theme->SecondaryColor())
				]
			];
	}
};
