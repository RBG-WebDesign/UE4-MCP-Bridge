#include "WidgetBlueprintBuilder.h"
#include "WidgetBlueprintJsonParser.h"
#include "WidgetBlueprintValidator.h"
#include "WidgetBlueprintAssetFactory.h"
#include "WidgetTreeBuilder.h"
#include "WidgetClassRegistry.h"
#include "WidgetChildAttachment.h"
#include "WidgetBlueprintFinalizer.h"
#include "WidgetBlueprintSpec.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"

bool FWidgetBlueprintBuilder::Build(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& JsonString,
	FString& OutError)
{
	// Step 1: Parse
	FWidgetBlueprintSpec Spec;
	if (!FWidgetBlueprintJsonParser::Parse(JsonString, Spec, OutError))
	{
		return false;
	}

	// Step 2: Validate
	FWidgetClassRegistry Registry;
	if (!FWidgetBlueprintValidator::Validate(Spec, Registry, OutError))
	{
		return false;
	}

	// Step 3: Create asset
	UWidgetBlueprint* WidgetBP = FWidgetBlueprintAssetFactory::CreateWidgetBlueprint(PackagePath, AssetName, OutError);
	if (!WidgetBP)
	{
		return false;
	}

	UWidgetTree* Tree = WidgetBP->WidgetTree;

	// Step 4: Clear existing tree (reverse iterate)
	TArray<UWidget*> ExistingWidgets;
	Tree->GetAllWidgets(ExistingWidgets);
	for (int32 i = ExistingWidgets.Num() - 1; i >= 0; --i)
	{
		Tree->RemoveWidget(ExistingWidgets[i]);
	}
	Tree->RootWidget = nullptr;

	// Step 5: Build tree
	FWidgetChildAttachment ChildAttachment;
	FWidgetTreeBuilder TreeBuilder(Registry, ChildAttachment);
	UWidget* Root = TreeBuilder.BuildTree(WidgetBP, Tree, Spec, OutError);
	if (!Root)
	{
		return false;
	}

	// Step 6: Assign root (root must NOT be attached via AddChild)
	Tree->RootWidget = Root;

	// Step 7: Finalize (bSave = true for new assets)
	return FWidgetBlueprintFinalizer::Finalize(WidgetBP, true, OutError);
}

bool FWidgetBlueprintBuilder::Rebuild(
	UWidgetBlueprint* WidgetBlueprint,
	const FString& JsonString,
	FString& OutError)
{
	if (!WidgetBlueprint)
	{
		OutError = TEXT("[WidgetBuilder] WidgetBlueprint is null");
		return false;
	}

	UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
	if (!Tree)
	{
		OutError = TEXT("[WidgetBuilder] WidgetBlueprint has no WidgetTree");
		return false;
	}

	// Steps 1-2: Parse and validate
	FWidgetBlueprintSpec Spec;
	if (!FWidgetBlueprintJsonParser::Parse(JsonString, Spec, OutError))
	{
		return false;
	}

	FWidgetClassRegistry Registry;
	if (!FWidgetBlueprintValidator::Validate(Spec, Registry, OutError))
	{
		return false;
	}

	// Step 4: Clear tree
	TArray<UWidget*> ExistingWidgets;
	Tree->GetAllWidgets(ExistingWidgets);
	for (int32 i = ExistingWidgets.Num() - 1; i >= 0; --i)
	{
		Tree->RemoveWidget(ExistingWidgets[i]);
	}
	Tree->RootWidget = nullptr;

	// Step 5-6: Build and assign
	FWidgetChildAttachment ChildAttachment;
	FWidgetTreeBuilder TreeBuilder(Registry, ChildAttachment);
	UWidget* Root = TreeBuilder.BuildTree(WidgetBlueprint, Tree, Spec, OutError);
	if (!Root)
	{
		return false;
	}
	Tree->RootWidget = Root;

	// Step 7: Finalize (bSave = false for rebuild)
	return FWidgetBlueprintFinalizer::Finalize(WidgetBlueprint, false, OutError);
}

bool FWidgetBlueprintBuilder::Validate(
	const FString& JsonString,
	FString& OutError)
{
	FWidgetBlueprintSpec Spec;
	if (!FWidgetBlueprintJsonParser::Parse(JsonString, Spec, OutError))
	{
		return false;
	}

	FWidgetClassRegistry Registry;
	return FWidgetBlueprintValidator::Validate(Spec, Registry, OutError);
}
