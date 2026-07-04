#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprintSpec.h"

class UWidgetBlueprint;
class UWidgetTree;
class UWidget;
class FWidgetClassRegistry;
class FWidgetChildAttachment;

class FWidgetTreeBuilder
{
public:
	FWidgetTreeBuilder(const FWidgetClassRegistry& InClassRegistry, FWidgetChildAttachment& InChildAttachment);

	UWidget* BuildTree(
		UWidgetBlueprint* WidgetBP,
		UWidgetTree* WidgetTree,
		const FWidgetBlueprintSpec& Spec,
		FString& OutError
	);

private:
	UWidget* BuildNode(
		UWidgetBlueprint* WidgetBP,
		UWidgetTree* WidgetTree,
		const FWidgetNodeSpec& Spec,
		UWidget* Parent,
		const FString& Path,
		FString& OutError
	);

	const FWidgetClassRegistry& ClassRegistry;
	FWidgetChildAttachment& ChildAttachment;
};
