// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;

class FWidgetBlueprintFinalizer
{
public:
	static bool Finalize(
		UWidgetBlueprint* WidgetBlueprint,
		bool bSave,
		FString& OutError
	);
};
