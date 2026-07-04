#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprintSpec.h"

class FWidgetClassRegistry;

class FWidgetBlueprintValidator
{
public:
	static bool Validate(
		const FWidgetBlueprintSpec& Spec,
		const FWidgetClassRegistry& Registry,
		FString& OutError
	);

private:
	static bool ValidateNode(
		const FWidgetNodeSpec& Node,
		const FWidgetClassRegistry& Registry,
		TSet<FString>& SeenNames,
		const FString& Path,
		FString& OutError
	);
};
