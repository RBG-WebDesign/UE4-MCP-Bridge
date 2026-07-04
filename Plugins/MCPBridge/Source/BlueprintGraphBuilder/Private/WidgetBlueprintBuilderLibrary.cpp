#include "WidgetBlueprintBuilderLibrary.h"
#include "WidgetBuilder/WidgetBlueprintBuilder.h"
#include "WidgetBlueprint.h"

FString UWidgetBlueprintBuilderLibrary::BuildWidgetFromJSON(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& JsonString)
{
	UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] BuildWidgetFromJSON: path='%s', name='%s'"), *PackagePath, *AssetName);

	FString Error;
	FWidgetBlueprintBuilder Builder;
	if (!Builder.Build(PackagePath, AssetName, JsonString, Error))
	{
		return Error;
	}
	return TEXT("");
}

FString UWidgetBlueprintBuilderLibrary::RebuildWidgetFromJSON(
	UWidgetBlueprint* WidgetBlueprint,
	const FString& JsonString)
{
	UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] RebuildWidgetFromJSON"));

	FString Error;
	FWidgetBlueprintBuilder Builder;
	if (!Builder.Rebuild(WidgetBlueprint, JsonString, Error))
	{
		return Error;
	}
	return TEXT("");
}

FString UWidgetBlueprintBuilderLibrary::ValidateWidgetJSON(
	const FString& JsonString)
{
	FString Error;
	FWidgetBlueprintBuilder Builder;
	if (!Builder.Validate(JsonString, Error))
	{
		return Error;
	}
	return TEXT("");
}
