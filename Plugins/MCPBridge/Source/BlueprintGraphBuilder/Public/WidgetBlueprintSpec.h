#pragma once

#include "CoreMinimal.h"
#include "Serialization/JsonTypes.h"
#include "Dom/JsonValue.h"
#include "Layout/Margin.h"
#include "Math/Vector2D.h"
#include "Containers/UnrealString.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"


enum class EWidgetCategory : uint8
{
	Leaf,
	Content,
	Panel
};

struct FWidgetPropertyDescriptor
{
	FString Name;
	EJson ExpectedType; // EJson::String, EJson::Number, EJson::Boolean, EJson::Object

	FWidgetPropertyDescriptor() : ExpectedType(EJson::None) {}
	FWidgetPropertyDescriptor(const FString& InName, EJson InType)
		: Name(InName), ExpectedType(InType) {}
};

struct FWidgetSlotSpec
{
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
	FVector2D Alignment = FVector2D::ZeroVector;
	FMargin Padding = FMargin(0);
	int32 ZOrder = 0;
	bool bAutoSize = false;

	bool bHasPosition = false;
	bool bHasSize = false;
	bool bHasAlignment = false;
	bool bHasPadding = false;
	bool bHasZOrder = false;
	bool bHasAutoSize = false;
};

struct FWidgetNodeSpec
{
	FString Type;
	FString Name;
	TMap<FString, TSharedPtr<FJsonValue>> Properties;
	FWidgetSlotSpec Slot;
	bool bHasSlot = false;
	TArray<FWidgetNodeSpec> Children;
};

struct FWidgetAnimationTrackSpec
{
	FString Type;  // "opacity" (v1), "translation", "scale" (future)

	// Opacity track data (v1)
	float FromOpacity = 0.0f;
	float ToOpacity = 1.0f;
	bool bHasOpacityData = false;
};

struct FWidgetAnimationSpec
{
	FString Name;
	FString Target;
	float Duration = 0.0f;
	TArray<FWidgetAnimationTrackSpec> Tracks;
};

struct FWidgetBlueprintSpec
{
	FWidgetNodeSpec Root;
	TArray<FWidgetAnimationSpec> Animations;
};
