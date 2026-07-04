#include "WidgetTreeBuilder.h"
#include "WidgetClassRegistry.h"
#include "WidgetChildAttachment.h"
#include "WidgetSlotPropertyApplier.h"
#include "WidgetPropertyApplier.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelSlot.h"

FWidgetTreeBuilder::FWidgetTreeBuilder(const FWidgetClassRegistry& InClassRegistry, FWidgetChildAttachment& InChildAttachment)
	: ClassRegistry(InClassRegistry)
	, ChildAttachment(InChildAttachment)
{
}

UWidget* FWidgetTreeBuilder::BuildTree(
	UWidgetBlueprint* WidgetBP,
	UWidgetTree* WidgetTree,
	const FWidgetBlueprintSpec& Spec,
	FString& OutError)
{
	return BuildNode(WidgetBP, WidgetTree, Spec.Root, nullptr, Spec.Root.Name, OutError);
}

UWidget* FWidgetTreeBuilder::BuildNode(
	UWidgetBlueprint* WidgetBP,
	UWidgetTree* WidgetTree,
	const FWidgetNodeSpec& Spec,
	UWidget* Parent,
	const FString& Path,
	FString& OutError)
{
	// Step 1: Resolve widget class
	TSubclassOf<UWidget> WidgetClass = ClassRegistry.ResolveWidgetClass(Spec.Type);
	if (!WidgetClass)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Could not resolve widget class for type '%s'"), *Path, *Spec.Type);
		return nullptr;
	}

	// Step 2: Construct widget
	UWidget* Widget = WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*Spec.Name));
	if (!Widget)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] %s: ConstructWidget failed for type '%s'"), *Path, *Spec.Type);
		return nullptr;
	}

	UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] %s: Constructed %s as '%s'"), *Path, *Spec.Type, *Spec.Name);

	// Step 2b: Apply widget properties
	if (Spec.Properties.Num() > 0)
	{
		if (!FWidgetPropertyApplier::ApplyProperties(Widget, Spec.Type, Spec.Properties, Path, OutError))
		{
			return nullptr;
		}
	}

	// Step 3: Attach to parent (skip for root -- root is assigned to WidgetTree->RootWidget by caller)
	if (Parent)
	{
		UPanelSlot* Slot = ChildAttachment.AttachChild(Parent, Widget, Path, OutError);
		if (!Slot)
		{
			return nullptr;
		}

		// Apply slot properties if specified in JSON
		if (Spec.bHasSlot)
		{
			if (!FWidgetSlotPropertyApplier::ApplySlotProperties(Slot, Spec.Slot, Path, OutError))
			{
				return nullptr;
			}
		}
	}

	// Step 4: Recurse into children
	for (int32 i = 0; i < Spec.Children.Num(); ++i)
	{
		const FWidgetNodeSpec& ChildSpec = Spec.Children[i];
		FString ChildPath = FString::Printf(TEXT("%s.%s"), *Path, *ChildSpec.Name);

		UWidget* Child = BuildNode(WidgetBP, WidgetTree, ChildSpec, Widget, ChildPath, OutError);
		if (!Child)
		{
			return nullptr;
		}
	}

	return Widget;
}
