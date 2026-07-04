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
#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "MovieScene.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "MovieSceneSection.h"

static UWidget* FindWidgetByName(UWidgetTree* WidgetTree, const FString& WidgetName)
{
	if (!WidgetTree)
	{
		return nullptr;
	}

	TArray<UWidget*> Widgets;
	WidgetTree->GetAllWidgets(Widgets);
	for (UWidget* Widget : Widgets)
	{
		if (Widget && Widget->GetName() == WidgetName)
		{
			return Widget;
		}
	}
	return nullptr;
}

static bool ApplyWidgetAnimations(
	UWidgetBlueprint* WidgetBP,
	UWidgetTree* WidgetTree,
	const FWidgetBlueprintSpec& Spec,
	FString& OutError)
{
	if (!WidgetBP || !WidgetTree || Spec.Animations.Num() == 0)
	{
		return true;
	}

	WidgetBP->Animations.Empty();

	for (const FWidgetAnimationSpec& AnimSpec : Spec.Animations)
	{
		UWidget* TargetWidget = FindWidgetByName(WidgetTree, AnimSpec.Target);
		if (!TargetWidget)
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s' target widget '%s' not found"), *AnimSpec.Name, *AnimSpec.Target);
			return false;
		}

		UWidgetAnimation* Animation = NewObject<UWidgetAnimation>(WidgetBP, FName(*AnimSpec.Name), RF_Transactional);
		if (!Animation)
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] Failed to create animation '%s'"), *AnimSpec.Name);
			return false;
		}

		UMovieScene* MovieScene = NewObject<UMovieScene>(Animation, NAME_None, RF_Transactional);
		if (!MovieScene)
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] Failed to create MovieScene for animation '%s'"), *AnimSpec.Name);
			return false;
		}

		Animation->MovieScene = MovieScene;
		Animation->SetDisplayLabel(AnimSpec.Name);

		const int32 DurationFrames = FMath::Max(1, FMath::RoundToInt(AnimSpec.Duration * 30.0f));
		MovieScene->SetPlaybackRange(0, DurationFrames);
		MovieScene->SetDisplayRate(FFrameRate(30, 1));
		MovieScene->SetTickResolutionDirectly(FFrameRate(30, 1));

		FGuid BindingGuid = MovieScene->AddPossessable(AnimSpec.Target, TargetWidget->GetClass());

		FWidgetAnimationBinding Binding;
		Binding.WidgetName = FName(*AnimSpec.Target);
		Binding.SlotWidgetName = NAME_None;
		Binding.AnimationGuid = BindingGuid;
		Binding.bIsRootWidget = (WidgetTree->RootWidget == TargetWidget);
		Animation->GetBindings().Add(Binding);

		for (const FWidgetAnimationTrackSpec& TrackSpec : AnimSpec.Tracks)
		{
			if (TrackSpec.Type != TEXT("opacity"))
			{
				OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s' unsupported track type '%s'"), *AnimSpec.Name, *TrackSpec.Type);
				return false;
			}

			UMovieSceneFloatTrack* Track = MovieScene->AddTrack<UMovieSceneFloatTrack>(BindingGuid);
			if (!Track)
			{
				OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s' failed to create opacity track"), *AnimSpec.Name);
				return false;
			}

			Track->SetPropertyNameAndPath(TEXT("RenderOpacity"), TEXT("RenderOpacity"));
			UMovieSceneFloatSection* Section = Cast<UMovieSceneFloatSection>(Track->CreateNewSection());
			if (!Section)
			{
				OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s' failed to create opacity section"), *AnimSpec.Name);
				return false;
			}

			Section->SetRange(TRange<FFrameNumber>::Inclusive(FFrameNumber(0), FFrameNumber(DurationFrames)));
			Track->AddSection(*Section);

			FMovieSceneFloatChannel& Channel = Section->GetChannel();
			Channel.AddCubicKey(FFrameNumber(0), TrackSpec.FromOpacity);
			Channel.AddCubicKey(FFrameNumber(DurationFrames), TrackSpec.ToOpacity);
		}

		WidgetBP->Animations.Add(Animation);
	}

	return true;
}

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

	// Step 7: Add widget animations from the parsed spec
	if (!ApplyWidgetAnimations(WidgetBP, Tree, Spec, OutError))
	{
		return false;
	}

	// Step 8: Finalize (bSave = true for new assets)
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

	// Step 7: Add widget animations from the parsed spec
	if (!ApplyWidgetAnimations(WidgetBlueprint, Tree, Spec, OutError))
	{
		return false;
	}

	// Step 8: Finalize (bSave = false for rebuild)
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
