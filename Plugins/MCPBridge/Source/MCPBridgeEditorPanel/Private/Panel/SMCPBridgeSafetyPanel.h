// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Panel/MCPBridgePanelTheme.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DECLARE_DELEGATE_RetVal_OneParam(bool, FMCPIsSafetyEnabled, const FString&);
DECLARE_DELEGATE_OneParam(FMCPOnSafetyToggle, const FString&);

/**
 * Safety card: the permission toggles that gate destructive bridge
 * operations. State lives with the owning panel; this widget only reads
 * through IsSafetyEnabled and requests changes through OnToggleSafety.
 */
class SMCPBridgeSafetyPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMCPBridgeSafetyPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FMCPBridgePanelTheme>, Theme)
		SLATE_EVENT(FMCPIsSafetyEnabled, IsSafetyEnabled)
		SLATE_EVENT(FMCPOnSafetyToggle, OnToggleSafety)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Theme = InArgs._Theme.ToSharedRef();
		IsSafetyEnabled = InArgs._IsSafetyEnabled;
		OnToggleSafety = InArgs._OnToggleSafety;

		ChildSlot
		[
			FMCPBridgePanelTheme::MakeCard(Theme.ToSharedRef(), TEXT("Safety"), TEXT("Destructive actions stay off until the user enables them."),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("allow_actor_delete"), TEXT("Allow Actor Delete"), TEXT("Delete actors by name or pattern."))]
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("allow_asset_delete"), TEXT("Allow Asset Delete"), TEXT("Delete assets from the content browser."))]
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("allow_console_command"), TEXT("Allow Console Command"), TEXT("Run allowlisted console commands."))]
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("allow_python_proxy"), TEXT("Allow Python Proxy"), TEXT("Execute Unreal Python from MCP tools."))]
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("auto_compile_blueprints"), TEXT("Auto Compile Blueprints"), TEXT("Compile generated or changed Blueprint assets."))]
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("auto_save_after_operations"), TEXT("Auto Save After Operations"), TEXT("Save assets after successful batch actions."))]
				+ SVerticalBox::Slot().AutoHeight()[MakeSafetyToggle(TEXT("pie_commands_enabled"), TEXT("PIE Commands Enabled"), TEXT("Allow start, stop, and status for Play In Editor."))])
		];
	}

private:
	TSharedPtr<FMCPBridgePanelTheme> Theme;
	FMCPIsSafetyEnabled IsSafetyEnabled;
	FMCPOnSafetyToggle OnToggleSafety;

	bool GetToggleState(const FString& Key) const
	{
		return IsSafetyEnabled.IsBound() ? IsSafetyEnabled.Execute(Key) : false;
	}

	TSharedRef<SWidget> MakeSafetyToggle(const FString Key, const FString Label, const FString HelpText)
	{
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(Theme->CardInsetBackground)
			.Padding(Theme->ScaledMargin(12.0f, 10.0f, 12.0f, 10.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(Label))
						.Font(Theme->FontBold(14))
						.ColorAndOpacity(Theme->PrimaryColor())
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 2.0f, 0.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(HelpText))
						.Font(Theme->FontRegular(12))
						.ColorAndOpacity(Theme->MutedColor())
						.AutoWrapText(true)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(Theme->ScaledMargin(12.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(SButton)
					.ButtonStyle(&Theme->TransparentButtonStyle)
					.OnClicked_Lambda([this, Key]()
					{
						OnToggleSafety.ExecuteIfBound(Key);
						return FReply::Handled();
					})
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor_Lambda([this, Key]() { return GetToggleState(Key) ? Theme->PrimaryBlue : Theme->ButtonNormal; })
						.Padding(Theme->ScaledMargin(12.0f, 6.0f, 12.0f, 6.0f))
						[
							SNew(SBox)
							.WidthOverride(Theme->ScaledSize(34.0f))
							[
								SNew(STextBlock)
								.Text_Lambda([this, Key]() { return FText::FromString(GetToggleState(Key) ? TEXT("ON") : TEXT("OFF")); })
								.Font(Theme->FontBold(11))
								.Justification(ETextJustify::Center)
								.ColorAndOpacity_Lambda([this, Key]() { return FSlateColor(GetToggleState(Key) ? FLinearColor::White : Theme->TextMuted); })
							]
						]
					]
				]
			];
	}
};
