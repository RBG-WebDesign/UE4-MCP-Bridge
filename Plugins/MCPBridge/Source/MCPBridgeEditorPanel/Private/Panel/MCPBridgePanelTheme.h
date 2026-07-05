// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

/**
 * Shared visual language for the MCP Bridge panel and its compound widgets.
 *
 * Single source of truth for colors, button styles, and layout scaling.
 * The main panel owns one instance and hands a TSharedRef to each child
 * widget so FButtonStyle pointers stay stable for the widget lifetime.
 */
struct FMCPBridgePanelTheme
{
	const FLinearColor AppBackground = FLinearColor(0.006f, 0.015f, 0.026f, 1.0f);
	const FLinearColor HeaderBackground = FLinearColor(0.012f, 0.028f, 0.044f, 1.0f);
	const FLinearColor CardBackground = FLinearColor(0.030f, 0.050f, 0.074f, 1.0f);
	const FLinearColor CardInsetBackground = FLinearColor(0.045f, 0.068f, 0.096f, 1.0f);
	const FLinearColor CardBorderColor = FLinearColor(0.120f, 0.175f, 0.235f, 1.0f);
	const FLinearColor ButtonNormal = FLinearColor(0.075f, 0.105f, 0.145f, 1.0f);
	const FLinearColor ButtonHover = FLinearColor(0.105f, 0.145f, 0.195f, 1.0f);
	const FLinearColor ButtonPressed = FLinearColor(0.050f, 0.075f, 0.110f, 1.0f);
	const FLinearColor PrimaryBlue = FLinearColor(0.150f, 0.455f, 0.920f, 1.0f);
	const FLinearColor PrimaryBlueHover = FLinearColor(0.205f, 0.555f, 1.000f, 1.0f);
	const FLinearColor PrimaryBluePressed = FLinearColor(0.090f, 0.320f, 0.720f, 1.0f);
	const FLinearColor TextPrimary = FLinearColor(0.930f, 0.950f, 0.980f, 1.0f);
	const FLinearColor TextSecondary = FLinearColor(0.740f, 0.790f, 0.860f, 1.0f);
	const FLinearColor TextMuted = FLinearColor(0.560f, 0.620f, 0.700f, 1.0f);
	const FLinearColor AccentBlue = FLinearColor(0.150f, 0.455f, 0.920f, 1.0f);
	const FLinearColor SuccessGreen = FLinearColor(0.235f, 0.740f, 0.445f, 1.0f);
	const FLinearColor WarningAmber = FLinearColor(1.000f, 0.690f, 0.270f, 1.0f);
	const FLinearColor ErrorRed = FLinearColor(0.930f, 0.320f, 0.320f, 1.0f);

	FSlateColorBrush ButtonNormalBrush;
	FSlateColorBrush ButtonHoverBrush;
	FSlateColorBrush ButtonPressedBrush;
	FSlateColorBrush PrimaryButtonNormalBrush;
	FSlateColorBrush PrimaryButtonHoverBrush;
	FSlateColorBrush PrimaryButtonPressedBrush;
	FSlateColorBrush TransparentBrush;
	FButtonStyle SecondaryButtonStyle;
	FButtonStyle PrimaryButtonStyle;
	FButtonStyle TransparentButtonStyle;

	// The main panel updates this when the responsive layout flips; all
	// scale-aware helpers read it so every child widget agrees.
	bool bCompact = false;

	FMCPBridgePanelTheme()
		: ButtonNormalBrush(ButtonNormal)
		, ButtonHoverBrush(ButtonHover)
		, ButtonPressedBrush(ButtonPressed)
		, PrimaryButtonNormalBrush(PrimaryBlue)
		, PrimaryButtonHoverBrush(PrimaryBlueHover)
		, PrimaryButtonPressedBrush(PrimaryBluePressed)
		, TransparentBrush(FLinearColor::Transparent)
	{
		Configure();
	}

	void Configure()
	{
		SecondaryButtonStyle = FButtonStyle()
			.SetNormal(ButtonNormalBrush)
			.SetHovered(ButtonHoverBrush)
			.SetPressed(ButtonPressedBrush)
			.SetDisabled(ButtonPressedBrush)
			.SetNormalPadding(FMargin(0.0f))
			.SetPressedPadding(FMargin(0.0f));

		PrimaryButtonStyle = FButtonStyle()
			.SetNormal(PrimaryButtonNormalBrush)
			.SetHovered(PrimaryButtonHoverBrush)
			.SetPressed(PrimaryButtonPressedBrush)
			.SetDisabled(ButtonPressedBrush)
			.SetNormalPadding(FMargin(0.0f))
			.SetPressedPadding(FMargin(0.0f));

		TransparentButtonStyle = FButtonStyle()
			.SetNormal(TransparentBrush)
			.SetHovered(TransparentBrush)
			.SetPressed(TransparentBrush)
			.SetDisabled(TransparentBrush)
			.SetNormalPadding(FMargin(0.0f))
			.SetPressedPadding(FMargin(0.0f));
	}

	int32 ScaledFont(const int32 Size) const
	{
		const float Scale = bCompact ? 0.60f : 1.0f;
		return FMath::Max(8, FMath::RoundToInt(static_cast<float>(Size) * Scale));
	}

	FSlateFontInfo FontRegular(const int32 Size) const
	{
		return FCoreStyle::GetDefaultFontStyle("Regular", ScaledFont(Size));
	}

	FSlateFontInfo FontBold(const int32 Size) const
	{
		return FCoreStyle::GetDefaultFontStyle("Bold", ScaledFont(Size));
	}

	float ScaledSize(const float Value) const
	{
		const float Scale = bCompact ? 0.72f : 1.0f;
		return Value * Scale;
	}

	FMargin ScaledMargin(const float Uniform) const
	{
		return FMargin(ScaledSize(Uniform));
	}

	FMargin ScaledMargin(const float Left, const float Top, const float Right, const float Bottom) const
	{
		return FMargin(ScaledSize(Left), ScaledSize(Top), ScaledSize(Right), ScaledSize(Bottom));
	}

	FSlateColor PrimaryColor() const { return FSlateColor(TextPrimary); }
	FSlateColor SecondaryColor() const { return FSlateColor(TextSecondary); }
	FSlateColor MutedColor() const { return FSlateColor(TextMuted); }

	FLinearColor ResultColor(const FString& Status) const
	{
		if (Status.Equals(TEXT("success"), ESearchCase::IgnoreCase) || Status.Equals(TEXT("ok"), ESearchCase::IgnoreCase))
		{
			return SuccessGreen;
		}
		if (Status.Equals(TEXT("warning"), ESearchCase::IgnoreCase) || Status.Equals(TEXT("warn"), ESearchCase::IgnoreCase))
		{
			return WarningAmber;
		}
		if (Status.Equals(TEXT("error"), ESearchCase::IgnoreCase) || Status.Equals(TEXT("failed"), ESearchCase::IgnoreCase) || Status.Equals(TEXT("failure"), ESearchCase::IgnoreCase))
		{
			return ErrorRed;
		}
		return TextMuted;
	}

	/** Standard card chrome shared by every panel section. */
	static TSharedRef<SWidget> MakeCard(
		const TSharedRef<FMCPBridgePanelTheme>& Theme,
		const FString& Title,
		const FString& Subtitle,
		TSharedRef<SWidget> Content)
	{
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(Theme->CardBorderColor)
			.Padding(Theme->ScaledSize(1.0f))
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(Theme->CardBackground)
				.Padding(Theme->ScaledMargin(18.0f, 16.0f, 18.0f, 16.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(Theme->ScaledMargin(0.0f, 0.0f, 0.0f, 14.0f))
					[
						MakeCardTitle(Theme, Title, Subtitle)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						Content
					]
				]
			];
	}

	static TSharedRef<SWidget> MakeCardTitle(
		const TSharedRef<FMCPBridgePanelTheme>& Theme,
		const FString& Title,
		const FString& Subtitle)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(Theme->ScaledMargin(0.0f, 0.0f, 10.0f, 0.0f))
			[
				SNew(SBox)
				.WidthOverride(Theme->ScaledSize(4.0f))
				.HeightOverride(Theme->ScaledSize(24.0f))
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(Theme->PrimaryBlue)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Title))
					.Font(Theme->FontBold(18))
					.ColorAndOpacity(Theme->PrimaryColor())
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(Theme->ScaledMargin(0.0f, 3.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Subtitle))
					.Font(Theme->FontRegular(12))
					.ColorAndOpacity(Theme->MutedColor())
					.AutoWrapText(true)
				]
			];
	}
};
