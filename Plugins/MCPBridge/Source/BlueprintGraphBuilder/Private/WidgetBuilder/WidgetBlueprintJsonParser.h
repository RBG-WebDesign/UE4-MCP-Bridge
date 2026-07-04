#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprintSpec.h"
#include "Dom/JsonObject.h"

class FWidgetBlueprintJsonParser
{
public:
	static bool Parse(const FString& JsonString, FWidgetBlueprintSpec& OutSpec, FString& OutError);

private:
	static bool ParseWidgetNode(
		const TSharedPtr<FJsonObject>& NodeObj,
		FWidgetNodeSpec& OutNode,
		const FString& Path,
		FString& OutError
	);

	static bool ParseSlotSpec(
		const TSharedPtr<FJsonObject>& SlotObj,
		FWidgetSlotSpec& OutSlot,
		const FString& Path,
		FString& OutError
	);

	static bool ParseProperties(
		const TSharedPtr<FJsonObject>& PropsObj,
		TMap<FString, TSharedPtr<FJsonValue>>& OutProperties,
		const FString& Path,
		FString& OutError
	);

	static bool ParseAnimations(
		const TArray<TSharedPtr<FJsonValue>>* AnimArray,
		TArray<FWidgetAnimationSpec>& OutAnimations,
		FString& OutError
	);
};
