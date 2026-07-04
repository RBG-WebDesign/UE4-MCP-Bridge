# Widget Animation Pass 7 (Opacity v1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add build-time opacity animations to Widget Blueprints, baked as UWidgetAnimation objects playable via PlayAnimation() in Blueprint.

**Architecture:** Extend the existing JSON parse-validate-build pipeline with an animations array. A new FWidgetAnimationBuilder creates UWidgetAnimation objects with MovieScene opacity tracks after the widget tree is built and before finalize. No Python/TypeScript changes needed -- JSON passes through opaquely.

**Tech Stack:** UE4.27 C++ (UWidgetAnimation, UMovieScene, UMovieSceneFloatTrack), existing BlueprintGraphBuilder plugin

**Spec:** `docs/superpowers/specs/2026-03-18-widget-animation-pass7-design.md`

---

## File Map

### New files
| File | Responsibility |
|------|---------------|
| `Private/WidgetBuilder/WidgetAnimationBuilder.h` | Animation builder class declaration |
| `Private/WidgetBuilder/WidgetAnimationBuilder.cpp` | Creates UWidgetAnimation + MovieScene opacity tracks from spec |

### Modified files
| File | Change |
|------|--------|
| `BlueprintGraphBuilder.Build.cs` | Add MovieScene, MovieSceneTracks to PrivateDependencyModuleNames |
| `Public/WidgetBlueprintSpec.h` | Add FWidgetAnimationTrackSpec, FWidgetAnimationSpec structs; extend FWidgetBlueprintSpec |
| `Private/WidgetBuilder/WidgetBlueprintJsonParser.h` | Declare ParseAnimations method |
| `Private/WidgetBuilder/WidgetBlueprintJsonParser.cpp` | Parse animations array, add top-level key validation |
| `Private/WidgetBuilder/WidgetBlueprintValidator.h` | Declare ValidateAnimations method |
| `Private/WidgetBuilder/WidgetBlueprintValidator.cpp` | Validate animation specs against widget tree names |
| `Private/WidgetBuilder/WidgetBlueprintBuilder.cpp` | Wire animation builder into Build() and Rebuild() |

All paths relative to `ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/`.

---

### Task 1: Add module dependencies

**Files:**
- Modify: `BlueprintGraphBuilder.Build.cs:16-29`

- [ ] **Step 1: Add MovieScene and MovieSceneTracks to Build.cs**

In `BlueprintGraphBuilder.Build.cs`, add two entries to `PrivateDependencyModuleNames`:

```csharp
PrivateDependencyModuleNames.AddRange(new string[]
{
    "UnrealEd",
    "BlueprintGraph",
    "KismetCompiler",
    "Kismet",
    "GraphEditor",
    "Json",
    "JsonUtilities",
    "UMG",
    "UMGEditor",
    "Slate",
    "SlateCore",
    "MovieScene",
    "MovieSceneTracks",
});
```

- [ ] **Step 2: Commit**

```bash
git add ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/BlueprintGraphBuilder.Build.cs
git commit -m "feat(widget-builder): add MovieScene module dependencies for Pass 7"
```

---

### Task 2: Add animation spec structs

**Files:**
- Modify: `Public/WidgetBlueprintSpec.h`

- [ ] **Step 1: Add FWidgetAnimationTrackSpec and FWidgetAnimationSpec**

Add these structs after `FWidgetNodeSpec` and before `FWidgetBlueprintSpec` in `WidgetBlueprintSpec.h`:

```cpp
struct FWidgetAnimationTrackSpec
{
	FString Type;  // "opacity" (v1), "translation", "scale" (future)

	// Opacity track data (v1)
	float FromOpacity = 0.0f;
	float ToOpacity = 1.0f;
	bool bHasOpacityData = false;
};

struct FWidgetAnimationSpec
{
	FString Name;
	FString Target;
	float Duration = 0.0f;
	TArray<FWidgetAnimationTrackSpec> Tracks;
};
```

- [ ] **Step 2: Add Animations field to FWidgetBlueprintSpec**

Change the existing `FWidgetBlueprintSpec` to:

```cpp
struct FWidgetBlueprintSpec
{
	FWidgetNodeSpec Root;
	TArray<FWidgetAnimationSpec> Animations;
};
```

- [ ] **Step 3: Commit**

```bash
git add ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/Public/WidgetBlueprintSpec.h
git commit -m "feat(widget-builder): add animation spec structs to WidgetBlueprintSpec"
```

---

### Task 3: Add animation parsing to JSON parser

**Files:**
- Modify: `Private/WidgetBuilder/WidgetBlueprintJsonParser.h`
- Modify: `Private/WidgetBuilder/WidgetBlueprintJsonParser.cpp`

- [ ] **Step 1: Declare ParseAnimations in header**

Add to the private section of `FWidgetBlueprintJsonParser` in `WidgetBlueprintJsonParser.h`:

```cpp
static bool ParseAnimations(
    const TArray<TSharedPtr<FJsonValue>>* AnimArray,
    TArray<FWidgetAnimationSpec>& OutAnimations,
    FString& OutError
);
```

- [ ] **Step 2: Add top-level key validation and animation parsing to Parse()**

In `WidgetBlueprintJsonParser.cpp`, add the valid top-level keys set at file scope (alongside existing valid key sets):

```cpp
static const TSet<FString> ValidTopLevelKeys = { TEXT("root"), TEXT("animations") };
```

Then modify the `Parse()` method. After the JSON is deserialized and before reading `root`, add top-level key validation:

```cpp
// Validate top-level keys
for (const auto& Pair : RootObj->Values)
{
    if (!ValidTopLevelKeys.Contains(Pair.Key))
    {
        OutError = FString::Printf(TEXT("[WidgetBuilder] Unknown top-level key '%s'"), *Pair.Key);
        return false;
    }
}
```

After the existing `ParseWidgetNode` call for `root` succeeds, add animation parsing:

```cpp
// Optional: animations
const TArray<TSharedPtr<FJsonValue>>* AnimArray = nullptr;
if (RootObj->TryGetArrayField(TEXT("animations"), AnimArray))
{
    if (!ParseAnimations(AnimArray, OutSpec.Animations, OutError))
    {
        return false;
    }
}
```

- [ ] **Step 3: Implement ParseAnimations**

Add the valid key sets for animation and track level:

```cpp
static const TSet<FString> ValidAnimationKeys = { TEXT("name"), TEXT("target"), TEXT("duration"), TEXT("tracks") };
static const TSet<FString> ValidOpacityTrackKeys = { TEXT("type"), TEXT("from"), TEXT("to") };
```

Implement `ParseAnimations`:

```cpp
bool FWidgetBlueprintJsonParser::ParseAnimations(
    const TArray<TSharedPtr<FJsonValue>>* AnimArray,
    TArray<FWidgetAnimationSpec>& OutAnimations,
    FString& OutError)
{
    for (int32 i = 0; i < AnimArray->Num(); ++i)
    {
        const TSharedPtr<FJsonObject>& AnimObj = (*AnimArray)[i]->AsObject();
        if (!AnimObj.IsValid())
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: not a valid object"), i);
            return false;
        }

        // Check for unknown keys
        for (const auto& Pair : AnimObj->Values)
        {
            if (!ValidAnimationKeys.Contains(Pair.Key))
            {
                OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: unknown key '%s'"), i, *Pair.Key);
                return false;
            }
        }

        FWidgetAnimationSpec AnimSpec;

        // Required: name
        if (!AnimObj->TryGetStringField(TEXT("name"), AnimSpec.Name))
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: missing required 'name' field"), i);
            return false;
        }

        // Required: target
        if (!AnimObj->TryGetStringField(TEXT("target"), AnimSpec.Target))
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: missing required 'target' field"), i);
            return false;
        }

        // Required: duration (TryGetNumberField takes double&, cast to float)
        double DurationVal;
        if (!AnimObj->TryGetNumberField(TEXT("duration"), DurationVal))
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: missing required 'duration' field"), i);
            return false;
        }
        AnimSpec.Duration = static_cast<float>(DurationVal);

        // Required: tracks
        const TArray<TSharedPtr<FJsonValue>>* TracksArray = nullptr;
        if (!AnimObj->TryGetArrayField(TEXT("tracks"), TracksArray))
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s': missing required 'tracks' field"), *AnimSpec.Name);
            return false;
        }

        for (int32 t = 0; t < TracksArray->Num(); ++t)
        {
            const TSharedPtr<FJsonObject>& TrackObj = (*TracksArray)[t]->AsObject();
            if (!TrackObj.IsValid())
            {
                OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: not a valid object"), *AnimSpec.Name, t);
                return false;
            }

            FWidgetAnimationTrackSpec TrackSpec;

            // Required: type
            if (!TrackObj->TryGetStringField(TEXT("type"), TrackSpec.Type))
            {
                OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: missing required 'type' field"), *AnimSpec.Name, t);
                return false;
            }

            // Parse track data based on type
            if (TrackSpec.Type == TEXT("opacity"))
            {
                // Check for unknown keys
                for (const auto& Pair : TrackObj->Values)
                {
                    if (!ValidOpacityTrackKeys.Contains(Pair.Key))
                    {
                        OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: unknown key '%s'"), *AnimSpec.Name, t, *Pair.Key);
                        return false;
                    }
                }

                // Required: from (TryGetNumberField takes double&, cast to float)
                double FromVal;
                if (!TrackObj->TryGetNumberField(TEXT("from"), FromVal))
                {
                    OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: opacity track missing 'from'"), *AnimSpec.Name, t);
                    return false;
                }
                TrackSpec.FromOpacity = static_cast<float>(FromVal);

                // Required: to
                double ToVal;
                if (!TrackObj->TryGetNumberField(TEXT("to"), ToVal))
                {
                    OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: opacity track missing 'to'"), *AnimSpec.Name, t);
                    return false;
                }
                TrackSpec.ToOpacity = static_cast<float>(ToVal);

                TrackSpec.bHasOpacityData = true;
            }
            // Unknown track types pass through to validator (which rejects them)

            AnimSpec.Tracks.Add(MoveTemp(TrackSpec));
        }

        OutAnimations.Add(MoveTemp(AnimSpec));
    }

    return true;
}
```

- [ ] **Step 4: Compile check**

Rebuild the plugin via UBT. Expected: compiles with no errors. The new parsing code is not yet called from Build/Rebuild but the types and methods must be valid.

- [ ] **Step 5: Commit**

```bash
git add ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/Private/WidgetBuilder/WidgetBlueprintJsonParser.h
git add ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/Private/WidgetBuilder/WidgetBlueprintJsonParser.cpp
git commit -m "feat(widget-builder): parse animations array from JSON"
```

---

### Task 4: Add animation validation

**Files:**
- Modify: `Private/WidgetBuilder/WidgetBlueprintValidator.h`
- Modify: `Private/WidgetBuilder/WidgetBlueprintValidator.cpp`

- [ ] **Step 1: Declare ValidateAnimations in header**

Add to the public section of `FWidgetBlueprintValidator` in `WidgetBlueprintValidator.h`:

```cpp
static bool ValidateAnimations(
    const TArray<FWidgetAnimationSpec>& Animations,
    const TSet<FString>& WidgetNames,
    FString& OutError
);
```

- [ ] **Step 2: Implement ValidateAnimations**

Add to `WidgetBlueprintValidator.cpp`:

```cpp
bool FWidgetBlueprintValidator::ValidateAnimations(
    const TArray<FWidgetAnimationSpec>& Animations,
    const TSet<FString>& WidgetNames,
    FString& OutError)
{
    TSet<FString> SeenAnimNames;

    for (int32 i = 0; i < Animations.Num(); ++i)
    {
        const FWidgetAnimationSpec& Anim = Animations[i];

        // Rule 1: name must not be empty
        if (Anim.Name.IsEmpty())
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: animation name is empty"), i);
            return false;
        }

        // Rule 2: name must be unique
        if (SeenAnimNames.Contains(Anim.Name))
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s': duplicate animation name"), *Anim.Name);
            return false;
        }
        SeenAnimNames.Add(Anim.Name);

        // Rule 3: target must exist in widget tree
        if (!WidgetNames.Contains(Anim.Target))
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s': target widget '%s' does not exist"), *Anim.Name, *Anim.Target);
            return false;
        }

        // Rule 4: duration must be > 0
        if (Anim.Duration <= 0.0f)
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s': duration must be > 0, got %f"), *Anim.Name, Anim.Duration);
            return false;
        }

        // Rule 5: tracks must have at least one entry
        if (Anim.Tracks.Num() == 0)
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s': tracks array is empty"), *Anim.Name);
            return false;
        }

        // Track-level validation
        TSet<FString> SeenTrackTypes;
        for (int32 t = 0; t < Anim.Tracks.Num(); ++t)
        {
            const FWidgetAnimationTrackSpec& Track = Anim.Tracks[t];

            // Rule 6: track type must be supported
            if (Track.Type != TEXT("opacity"))
            {
                OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: unsupported track type '%s'"), *Anim.Name, t, *Track.Type);
                return false;
            }

            // Rule 9: no duplicate track types within one animation
            if (SeenTrackTypes.Contains(Track.Type))
            {
                OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: duplicate track type '%s'"), *Anim.Name, t, *Track.Type);
                return false;
            }
            SeenTrackTypes.Add(Track.Type);

            // Rule 7: opacity from/to must be in [0, 1]
            if (Track.bHasOpacityData)
            {
                if (Track.FromOpacity < 0.0f || Track.FromOpacity > 1.0f)
                {
                    OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: opacity 'from' must be between 0 and 1, got %f"), *Anim.Name, t, Track.FromOpacity);
                    return false;
                }
                if (Track.ToOpacity < 0.0f || Track.ToOpacity > 1.0f)
                {
                    OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: opacity 'to' must be between 0 and 1, got %f"), *Anim.Name, t, Track.ToOpacity);
                    return false;
                }
            }
        }
    }

    return true;
}
```

- [ ] **Step 3: Wire ValidateAnimations into Validate()**

In `WidgetBlueprintValidator.cpp`, modify `Validate()`. The existing code calls `ValidateNode()` which populates `SeenNames`. After the `ValidateNode` call succeeds, add:

```cpp
// Validate animations against collected widget names
if (Spec.Animations.Num() > 0)
{
    if (!ValidateAnimations(Spec.Animations, SeenNames, OutError))
    {
        return false;
    }
}
```

This goes right before the final `return true;` in `Validate()`. The `SeenNames` set already contains all widget names from tree validation.

- [ ] **Step 4: Compile check**

Rebuild. Expected: compiles. Validation now runs for animation specs automatically through the existing Validate() -> ValidateNode() flow.

- [ ] **Step 5: Commit**

```bash
git add ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/Private/WidgetBuilder/WidgetBlueprintValidator.h
git add ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/Private/WidgetBuilder/WidgetBlueprintValidator.cpp
git commit -m "feat(widget-builder): add animation validation against widget tree"
```

---

### Task 5: Create FWidgetAnimationBuilder

**Files:**
- Create: `Private/WidgetBuilder/WidgetAnimationBuilder.h`
- Create: `Private/WidgetBuilder/WidgetAnimationBuilder.cpp`

- [ ] **Step 1: Create WidgetAnimationBuilder.h**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprintSpec.h"

class UWidgetBlueprint;
class UMovieScene;

class FWidgetAnimationBuilder
{
public:
	bool BuildAnimations(
		UWidgetBlueprint* WidgetBP,
		const TArray<FWidgetAnimationSpec>& Animations,
		FString& OutError
	);

private:
	bool BuildSingleAnimation(
		UWidgetBlueprint* WidgetBP,
		const FWidgetAnimationSpec& AnimSpec,
		FString& OutError
	);

	bool BuildOpacityTrack(
		UMovieScene* MovieScene,
		const FGuid& BindingGuid,
		const FWidgetAnimationTrackSpec& Track,
		FFrameNumber DurationFrames,
		const FString& AnimName,
		FString& OutError
	);
};
```

- [ ] **Step 2: Create WidgetAnimationBuilder.cpp**

```cpp
#include "WidgetAnimationBuilder.h"
#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"

bool FWidgetAnimationBuilder::BuildAnimations(
	UWidgetBlueprint* WidgetBP,
	const TArray<FWidgetAnimationSpec>& Animations,
	FString& OutError)
{
	for (const FWidgetAnimationSpec& AnimSpec : Animations)
	{
		if (!BuildSingleAnimation(WidgetBP, AnimSpec, OutError))
		{
			return false;
		}
	}
	return true;
}

bool FWidgetAnimationBuilder::BuildSingleAnimation(
	UWidgetBlueprint* WidgetBP,
	const FWidgetAnimationSpec& AnimSpec,
	FString& OutError)
{
	// Step 1: Create UWidgetAnimation
	UWidgetAnimation* Anim = NewObject<UWidgetAnimation>(WidgetBP, FName(*AnimSpec.Name));
	UMovieScene* MovieScene = NewObject<UMovieScene>(Anim);
	Anim->MovieScene = MovieScene;

	UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] Animation '%s': created, target='%s', duration=%.2fs"),
		*AnimSpec.Name, *AnimSpec.Target, AnimSpec.Duration);

	// Step 2: Set playback range
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	FFrameNumber EndFrame = (AnimSpec.Duration * TickResolution).FloorToFrame();
	EndFrame = FMath::Max(EndFrame, FFrameNumber(1));
	MovieScene->SetPlaybackRange(FFrameNumber(0), EndFrame.Value);

	// Step 3: Bind widget
	// Use AddPossessable to get the canonical GUID, then use that same GUID for the binding.
	// This ensures MovieScene tracks and UMG animation binding reference the same object.
	FGuid BindingGuid = MovieScene->AddPossessable(AnimSpec.Target, UWidget::StaticClass());

	FWidgetAnimationBinding Binding;
	Binding.WidgetName = FName(*AnimSpec.Target);
	Binding.SlotWidgetName = NAME_None;
	Binding.AnimationGuid = BindingGuid;
	Anim->AnimationBindings.Add(Binding);

	UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] Animation '%s': bound to widget '%s'"),
		*AnimSpec.Name, *AnimSpec.Target);

	// Mark target widget as variable for animation binding
	if (WidgetBP->WidgetTree)
	{
		UWidget* TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*AnimSpec.Target));
		if (TargetWidget)
		{
			TargetWidget->bIsVariable = true;
		}
	}

	// Step 4: Build tracks
	for (const FWidgetAnimationTrackSpec& Track : AnimSpec.Tracks)
	{
		if (Track.Type == TEXT("opacity"))
		{
			if (!BuildOpacityTrack(MovieScene, BindingGuid, Track, EndFrame, AnimSpec.Name, OutError))
			{
				return false;
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[WidgetBuilder] Animation '%s': track type '%s' not implemented yet"),
				*AnimSpec.Name, *Track.Type);
		}
	}

	// Step 5: Register animation
	WidgetBP->Animations.Add(Anim);

	return true;
}

bool FWidgetAnimationBuilder::BuildOpacityTrack(
	UMovieScene* MovieScene,
	const FGuid& BindingGuid,
	const FWidgetAnimationTrackSpec& Track,
	FFrameNumber DurationFrames,
	const FString& AnimName,
	FString& OutError)
{
	// Create float track for RenderOpacity
	UMovieSceneFloatTrack* FloatTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(BindingGuid);
	if (!FloatTrack)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s': failed to create opacity track"), *AnimName);
		return false;
	}
	FloatTrack->SetPropertyNameAndPath(TEXT("RenderOpacity"), TEXT("RenderOpacity"));

	// Create section
	UMovieSceneFloatSection* Section = Cast<UMovieSceneFloatSection>(FloatTrack->CreateNewSection());
	if (!Section)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s': failed to create opacity section"), *AnimName);
		return false;
	}
	FloatTrack->AddSection(*Section);

	// Half-open range: [0, Duration+1)
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(DurationFrames.Value + 1)));

	// Add keyframes
	FMovieSceneFloatChannel* Channel = Section->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);
	if (!Channel)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s': failed to get float channel"), *AnimName);
		return false;
	}

	Channel->AddLinearKey(FFrameNumber(0), Track.FromOpacity);
	Channel->AddLinearKey(DurationFrames, Track.ToOpacity);

	UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] Animation '%s': opacity track %.2f -> %.2f"),
		*AnimName, Track.FromOpacity, Track.ToOpacity);

	return true;
}
```

**Compile-time risk notes for this file:**
- `#include "Animation/WidgetAnimation.h"` -- may need `#include "Animation/UMGSequencePlayer.h"` or similar. If header not found, search UE4.27 source for `UWidgetAnimation` definition.
- `FWidgetAnimationBinding` fields (`WidgetName`, `SlotWidgetName`, `AnimationGuid`) -- verify field names compile. If private, switch entirely to possessable path per spec.
- `Binding.SlotWidgetName = NAME_None` -- this field may not exist. Remove if it doesn't compile.
- `Channel->AddLinearKey()` -- if method doesn't exist, try `Channel->AddCubicKey()` or direct key manipulation via `TMovieSceneChannelData`.
- The code uses `AddPossessable()` to get the canonical GUID, then passes that same GUID to `FWidgetAnimationBinding`. This keeps both systems using one GUID. If `AddPossessable` doesn't exist or returns void, generate the GUID with `FGuid::NewGuid()` and add the possessable manually.

- [ ] **Step 3: Compile check**

Rebuild. This is where Risk 1/2/3 from the spec may surface. Fix compile errors based on the risk notes above. The key goal: it compiles. It doesn't need to work yet.

- [ ] **Step 4: Commit**

```bash
git add ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/Private/WidgetBuilder/WidgetAnimationBuilder.h
git add ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/Private/WidgetBuilder/WidgetAnimationBuilder.cpp
git commit -m "feat(widget-builder): add FWidgetAnimationBuilder with opacity track support"
```

---

### Task 6: Wire animation builder into Build/Rebuild pipeline

**Files:**
- Modify: `Private/WidgetBuilder/WidgetBlueprintBuilder.cpp`

- [ ] **Step 1: Add include**

Add at the top of `WidgetBlueprintBuilder.cpp`:

```cpp
#include "WidgetAnimationBuilder.h"
```

- [ ] **Step 2: Add animation building to Build()**

In `FWidgetBlueprintBuilder::Build()`, after `Tree->RootWidget = Root;` (line 62) and before the Finalize call (line 65), insert:

```cpp
// Step 6b: Build animations (after tree, before finalize)
if (Spec.Animations.Num() > 0)
{
    FWidgetAnimationBuilder AnimBuilder;
    if (!AnimBuilder.BuildAnimations(WidgetBP, Spec.Animations, OutError))
    {
        return false;
    }
}
```

- [ ] **Step 3: Add animation clearing and building to Rebuild()**

In `FWidgetBlueprintBuilder::Rebuild()`, add animation clearing alongside the existing tree clearing. After `Tree->RootWidget = nullptr;` (line 106), add:

```cpp
// Clear existing animations
WidgetBlueprint->Animations.Empty();
```

Then after `Tree->RootWidget = Root;` (line 116) and before the Finalize call (line 119), insert:

```cpp
// Build animations (after tree, before finalize)
if (Spec.Animations.Num() > 0)
{
    FWidgetAnimationBuilder AnimBuilder;
    if (!AnimBuilder.BuildAnimations(WidgetBlueprint, Spec.Animations, OutError))
    {
        return false;
    }
}
```

- [ ] **Step 4: Compile check**

Rebuild. Expected: compiles. The full pipeline is now wired: Parse -> Validate -> Build Tree -> Build Animations -> Finalize.

- [ ] **Step 5: Commit**

```bash
git add ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/Private/WidgetBuilder/WidgetBlueprintBuilder.cpp
git commit -m "feat(widget-builder): wire animation builder into Build/Rebuild pipeline"
```

---

### Task 7: Copy plugin to UE4 project and rebuild

**Files:**
- Binary output: `D:\Unreal Projects\CodePlayground\Plugins\BlueprintGraphBuilder\`

- [ ] **Step 1: Delete old DLLs**

Per project feedback: always delete old DLLs before rebuilding so editor loads fresh code.

```bash
rm -f "D:/Unreal Projects/CodePlayground/Plugins/BlueprintGraphBuilder/Binaries/Win64/"*.dll
rm -f "D:/Unreal Projects/CodePlayground/Plugins/BlueprintGraphBuilder/Binaries/Win64/"*.pdb
```

- [ ] **Step 2: Copy source files to project plugin directory**

Copy the entire `Source/` directory from the repo to the project plugin:

```bash
cp -r "d:/UE/UE_Bridge/ue4-plugin/BlueprintGraphBuilder/Source" "D:/Unreal Projects/CodePlayground/Plugins/BlueprintGraphBuilder/"
```

Also copy the Build.cs if it changed:

```bash
cp "d:/UE/UE_Bridge/ue4-plugin/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/BlueprintGraphBuilder.Build.cs" "D:/Unreal Projects/CodePlayground/Plugins/BlueprintGraphBuilder/Source/BlueprintGraphBuilder/"
```

- [ ] **Step 3: Rebuild from UE4 editor**

Open UE4 editor. It should detect source changes and offer to rebuild, or use Build from the toolbar. Fix any compile errors following the risk notes in Task 5.

- [ ] **Step 4: Verify plugin loaded**

Check Output Log for `[WidgetBuilder]` messages confirming the plugin loaded.

---

### Task 8: Integration test -- build widget with animations

- [ ] **Step 1: Run the spec test case via python_proxy**

Use the MCP bridge to call `BuildWidgetFromJSON` (or `RebuildWidgetFromJSON` if asset exists) with the test JSON from the spec:

```python
import unreal, json

test_json = {
    "root": {
        "type": "CanvasPanel",
        "name": "Root",
        "children": [
            {
                "type": "TextBlock",
                "name": "QTEText",
                "properties": {
                    "text": "PRESS X",
                    "renderOpacity": 0.0
                },
                "slot": {
                    "position": {"x": 400, "y": 300},
                    "size": {"x": 200, "y": 50}
                }
            }
        ]
    },
    "animations": [
        {
            "name": "FadeIn",
            "target": "QTEText",
            "duration": 0.25,
            "tracks": [
                {
                    "type": "opacity",
                    "from": 0.0,
                    "to": 1.0
                }
            ]
        },
        {
            "name": "FadeOut",
            "target": "QTEText",
            "duration": 0.25,
            "tracks": [
                {
                    "type": "opacity",
                    "from": 1.0,
                    "to": 0.0
                }
            ]
        }
    ]
}

err = unreal.WidgetBlueprintBuilderLibrary.build_widget_from_json(
    "/Game/Tests",
    "WBP_ANIM_TEST",
    json.dumps(test_json)
)
print("ANIM TEST:", "PASS" if err == "" else "FAIL", err)
```

Expected output: `ANIM TEST: PASS`

- [ ] **Step 2: Verify in editor**

Open `WBP_ANIM_TEST` in the Widget Blueprint editor. Check:
1. Hierarchy shows Root > QTEText
2. Animations panel shows "FadeIn" and "FadeOut"
3. Selecting FadeIn shows an opacity track
4. Blueprint compiles clean

- [ ] **Step 3: Verify rebuild works**

Run the same JSON through `RebuildWidgetFromJSON`:

```python
wbp = unreal.load_asset("/Game/Tests/WBP_ANIM_TEST")
err = unreal.WidgetBlueprintBuilderLibrary.rebuild_widget_from_json(wbp, json.dumps(test_json))
print("REBUILD:", "PASS" if err == "" else "FAIL", err)
```

Expected: `REBUILD: PASS`

- [ ] **Step 4: Verify backward compatibility -- JSON without animations**

Run existing tree-only JSON to confirm animations are optional:

```python
no_anim_json = {
    "root": {
        "type": "CanvasPanel",
        "name": "Root",
        "children": [
            {"type": "TextBlock", "name": "Label", "properties": {"text": "Hello"}}
        ]
    }
}
err = unreal.WidgetBlueprintBuilderLibrary.build_widget_from_json(
    "/Game/Tests", "WBP_NO_ANIM_TEST", json.dumps(no_anim_json))
print("NO ANIM:", "PASS" if err == "" else "FAIL", err)
```

Expected: `NO ANIM: PASS`

---

### Task 9: Validation failure tests

- [ ] **Step 1: Test all 9 validation error cases**

Run each of these through `ValidateWidgetJSON` and confirm they produce error messages:

```python
import unreal, json

base = {"root": {"type": "CanvasPanel", "name": "Root", "children": [{"type": "TextBlock", "name": "T"}]}}

tests = [
    ("nonexistent target", {**base, "animations": [{"name": "A", "target": "Nope", "duration": 0.5, "tracks": [{"type": "opacity", "from": 0, "to": 1}]}]}),
    ("duplicate name", {**base, "animations": [
        {"name": "A", "target": "T", "duration": 0.5, "tracks": [{"type": "opacity", "from": 0, "to": 1}]},
        {"name": "A", "target": "T", "duration": 0.5, "tracks": [{"type": "opacity", "from": 1, "to": 0}]}
    ]}),
    ("empty name", {**base, "animations": [{"name": "", "target": "T", "duration": 0.5, "tracks": [{"type": "opacity", "from": 0, "to": 1}]}]}),
    ("zero duration", {**base, "animations": [{"name": "A", "target": "T", "duration": 0, "tracks": [{"type": "opacity", "from": 0, "to": 1}]}]}),
    ("negative duration", {**base, "animations": [{"name": "A", "target": "T", "duration": -1.5, "tracks": [{"type": "opacity", "from": 0, "to": 1}]}]}),
    ("empty tracks", {**base, "animations": [{"name": "A", "target": "T", "duration": 0.5, "tracks": []}]}),
    ("out of range from", {**base, "animations": [{"name": "A", "target": "T", "duration": 0.5, "tracks": [{"type": "opacity", "from": 2.0, "to": 1}]}]}),
    ("out of range to", {**base, "animations": [{"name": "A", "target": "T", "duration": 0.5, "tracks": [{"type": "opacity", "from": 0.5, "to": -0.5}]}]}),
    ("unsupported type", {**base, "animations": [{"name": "A", "target": "T", "duration": 0.5, "tracks": [{"type": "translation", "from": 0, "to": 1}]}]}),
    ("duplicate track type", {**base, "animations": [{"name": "A", "target": "T", "duration": 0.5, "tracks": [
        {"type": "opacity", "from": 0, "to": 1},
        {"type": "opacity", "from": 1, "to": 0}
    ]}]}),
]

for desc, data in tests:
    err = unreal.WidgetBlueprintBuilderLibrary.validate_widget_json(json.dumps(data))
    status = "PASS" if err != "" else "FAIL (expected error)"
    print(f"  {desc}: {status} -> {err[:80] if err else 'NO ERROR'}")
```

Expected: all 10 print PASS with descriptive error messages.

- [ ] **Step 2: Test positive cases (rules 10 and 11)**

Verify these are NOT rejected:

```python
# Rule 10: from == to is valid (no-op animation)
noop = {**base, "animations": [{"name": "Noop", "target": "T", "duration": 0.5, "tracks": [{"type": "opacity", "from": 0.5, "to": 0.5}]}]}
err = unreal.WidgetBlueprintBuilderLibrary.validate_widget_json(json.dumps(noop))
print("  from==to valid:", "PASS" if err == "" else "FAIL", err)

# Rule 11: multiple animations targeting same widget
multi = {**base, "animations": [
    {"name": "FadeIn", "target": "T", "duration": 0.5, "tracks": [{"type": "opacity", "from": 0, "to": 1}]},
    {"name": "FadeOut", "target": "T", "duration": 0.5, "tracks": [{"type": "opacity", "from": 1, "to": 0}]}
]}
err = unreal.WidgetBlueprintBuilderLibrary.validate_widget_json(json.dumps(multi))
print("  multi-anim same target:", "PASS" if err == "" else "FAIL", err)
```

Expected: both print PASS with empty error.

- [ ] **Step 3: Commit validation test results**

No code changes needed. If all tests pass, the validation layer is complete.

---

### Task 10: PlayAnimation runtime verification (manual)

- [ ] **Step 1: Create a test Actor Blueprint**

In UE4 editor, create a simple Actor Blueprint that:
1. Creates a widget from `WBP_ANIM_TEST`
2. Adds it to viewport
3. On BeginPlay, calls `PlayAnimation(FadeIn)` on the widget

- [ ] **Step 2: Verify fade animation plays**

If runtime testing has been explicitly requested or approved, run PIE. The "PRESS X" text should fade from invisible (opacity 0) to visible (opacity 1) over 0.25 seconds.

If nothing animates but the animation exists in the panel, check:
- Property path: should be `RenderOpacity` exactly
- Widget binding: widget should be marked as variable (`bIsVariable = true`)
- Object flags: animation may need `RF_Transactional`

- [ ] **Step 3: Final commit**

```bash
git commit --allow-empty -m "feat(widget-builder): Pass 7 complete -- build-time opacity animations verified"
```
