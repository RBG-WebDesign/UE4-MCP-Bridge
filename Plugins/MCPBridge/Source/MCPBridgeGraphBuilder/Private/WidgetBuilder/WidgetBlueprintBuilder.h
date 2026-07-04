#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;

class FWidgetBlueprintBuilder
{
public:
	bool Build(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& JsonString,
		FString& OutError
	);

	bool Rebuild(
		UWidgetBlueprint* WidgetBlueprint,
		const FString& JsonString,
		FString& OutError
	);

	bool Validate(
		const FString& JsonString,
		FString& OutError
	);
};
