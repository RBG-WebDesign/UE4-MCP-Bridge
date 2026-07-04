#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;

class FWidgetBlueprintAssetFactory
{
public:
	static UWidgetBlueprint* CreateWidgetBlueprint(
		const FString& PackagePath,
		const FString& AssetName,
		FString& OutError
	);

	static UWidgetBlueprint* LoadWidgetBlueprint(
		const FString& AssetPath,
		FString& OutError
	);
};
