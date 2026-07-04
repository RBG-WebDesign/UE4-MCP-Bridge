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
