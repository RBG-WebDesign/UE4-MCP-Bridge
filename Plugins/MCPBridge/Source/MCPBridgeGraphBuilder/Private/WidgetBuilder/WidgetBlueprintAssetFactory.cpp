#include "WidgetBlueprintAssetFactory.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "WidgetBlueprintFactory.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "UObject/Package.h"

UWidgetBlueprint* FWidgetBlueprintAssetFactory::CreateWidgetBlueprint(
	const FString& PackagePath,
	const FString& AssetName,
	FString& OutError)
{
	// Check if asset already exists
	FString FullPath = PackagePath / AssetName;
	UObject* ExistingAsset = StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *FullPath);
	if (ExistingAsset)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Asset already exists at '%s'. Use RebuildWidgetFromJSON for existing assets."), *FullPath);
		return nullptr;
	}

	// Create via UWidgetBlueprintFactory + FAssetToolsModule
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = UUserWidget::StaticClass();

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* CreatedAsset = AssetTools.CreateAsset(AssetName, PackagePath, UWidgetBlueprint::StaticClass(), Factory);

	if (!CreatedAsset)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Failed to create UWidgetBlueprint at '%s'"), *FullPath);
		return nullptr;
	}

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(CreatedAsset);
	if (!WidgetBP)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Created asset is not a UWidgetBlueprint at '%s'"), *FullPath);
		return nullptr;
	}

	// Verify WidgetTree exists (factory should create it)
	if (!WidgetBP->WidgetTree)
	{
		OutError = TEXT("[WidgetBuilder] UWidgetBlueprint missing WidgetTree after creation. Factory path may be incorrect.");
		return nullptr;
	}

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(WidgetBP);

	UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] Created UWidgetBlueprint at '%s'"), *FullPath);
	return WidgetBP;
}

UWidgetBlueprint* FWidgetBlueprintAssetFactory::LoadWidgetBlueprint(
	const FString& AssetPath,
	FString& OutError)
{
	UObject* LoadedAsset = StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *AssetPath);
	if (!LoadedAsset)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Could not load asset at '%s'"), *AssetPath);
		return nullptr;
	}

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Asset at '%s' is not a UWidgetBlueprint"), *AssetPath);
		return nullptr;
	}

	if (!WidgetBP->WidgetTree)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] UWidgetBlueprint at '%s' has no WidgetTree"), *AssetPath);
		return nullptr;
	}

	return WidgetBP;
}
