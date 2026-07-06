# Sitcom Title Controller

Status: Verified 2026-07-06
Engine: UE4.27
Origin: Sinfeld_Demo, `ASitcomTitleController` + `WBP_SitcomOpeningTitles`

## 1. System Design Intent

A data-driven opening-credits sequencer that runs independently of the
GameMode lifecycle. A placed actor owns an array of cue structs (id, type,
text, frame timings, layout box, per-cue glitch tuning); every tick it
computes each cue's opacity from a frame-based envelope and drives named
widget groups inside a single UMG widget. Cues that have no matching widget
in the WBP asset get their widgets constructed at runtime, so adding a title
is a pure data edit (+ button on the array) with no widget-designer work.
The WBP asset is never modified at runtime; everything is built into the
transient game copy at BeginPlay.

## 2. Dependencies

- C++ classes: `ASitcomTitleController : AActor` (game module),
  `FSitcomTitleCue` (USTRUCT), `ESitcomTitleCueType` enum
  (RatingBox / MainTitle / TwoLineCredit / LowerThird)
- Engine APIs (verified in 4.27 headers):
  - `UWidgetTree::ConstructWidget<T>` + `FindWidget` (Blueprint/WidgetTree.h)
  - `UPanelWidget::RemoveChildAt`, `GetChildIndex` (Components/PanelWidget.h)
  - `UCanvasPanel::AddChildToCanvas` (Components/CanvasPanel.h)
  - `UContentWidget::SetContent` (Components/ContentWidget.h)
  - `UVerticalBox::AddChildToVerticalBox`, `UOverlay::AddChildToOverlay`
  - `UTextBlock` public `Font` member + `SetFont/SetJustification`
- Content assets: one `WBP_` UserWidget with a root `CanvasPanel`
  (designed groups optional; everything can be runtime-built)
- Build.cs modules: `UMG`, `Slate`, `SlateCore`

## 3. How-To Graph Logic

Widget naming contract (the entire system keys off names):

```
{CueId}_Group      Border, one per cue, child of the root canvas
  MainTitle/LowerThird: {CueId}_Text            (TextBlock)
  TwoLineCredit:        {CueId}_Role, {CueId}_Name  (TextBlocks in a VerticalBox)
  RatingBox:            {CueId}_RatingImage + {CueId}_RatingTop/Main/Descriptor
{CueId}_Retainer   RetainerBox wrapper added at runtime (see glitch playbook)
```

Runtime flow:

```
Constructor
  -> ConstructorHelpers loads TitleWidgetClass, RatingTexture, GlitchMaterial
  -> LoadDefaultSitcomCredits() when Cues empty (pure code defaults)
BeginPlay
  -> CreateWidget(TitleWidgetClass); AddToViewport(ZOrder 9000)
  -> EnsureCueWidgetsExist()
       auto-assign CueId when None (Cue_%03d, collision-checked)
       for each cue with no {CueId}_Group: construct Border + children per
       type, clone FSlateFontInfo from any designed text block so fonts
       match, AddChildToCanvas
  -> WrapCueGroupsInRetainers()   (glitch; see wbp-glitch-effect.md)
  -> HideAllTitles()
Tick (bAutoDriveFromWorldTime)
  -> frame = RoundToInt((WorldTime - StartTime) * TargetFPS)   TargetFPS 29.97
  -> UpdateTitlesAtFrame(frame)
       HideAllTitles()  (opacity 0 + retainers Collapsed)
       for each cue with CueOpacityAtFrame > 0:
         ApplyCueToWidget(cue, opacity)   set text (ToUpper), style, position
         ApplyGlitchToCue(cue, CueGlitchAtFrame(cue, frame), frame)
```

Opacity envelope per cue (all int frames):
`StartFrame -> FadeInFrames (0..1 linear) -> HoldFrames (1) ->
FadeOutFrames (1..0 linear) -> 0`.

Layout: `Cue.Box` is an FVector4 of normalized (X,Y = top-left, Z,W = size)
against `DesignResolution` (1920x1080); applied to the canvas slot of the
cue's container every tick, so cues can be repositioned live.

Text fallback: single-line types display `Text`; when `Text` is empty,
display `Role + " " + Name` so data entered in the two-line fields still
renders.

## 4. Replication Steps

1. Create the C++ actor with the cue struct and envelope functions
   (`CueOpacityAtFrame`, `CueEndFrame`, `ApplyCueToWidget`,
   `EnsureCueWidgetsExist`, `FindWidgetByName` via WidgetTree).
2. Create a minimal WBP: UserWidget with root CanvasPanel named RootCanvas.
   Designed groups are optional seed content; name them per the contract.
3. Make a Blueprint child of the actor, place it in the level, set
   TitleWidgetClass if not resolved by ConstructorHelpers.
4. Author cues in the Details panel array. Only data required: type, text
   fields, StartFrame/FadeIn/Hold/FadeOut, Box.
5. Build with UBT (editor closed), relaunch, press Play.
6. For the fadeout glitch layer, apply wbp-glitch-effect.md on top.

## 5. UE4.27 Legacy Gotchas

- `UPanelWidget::ReplaceChildAt` only swaps the UObject content pointer and
  calls `PanelSlot->SynchronizeProperties()`; it never rebuilds the live
  Slate slot. On an already-constructed widget it is a silent visual no-op.
  Runtime reparenting must be `RemoveChildAt(index)` then
  `AddChildToCanvas(newWidget)` (which triggers TakeWidget ->
  SynchronizeProperties on the new child).
- After wrapping a group in a container, `Group->Slot` is the container's
  inner slot, not the canvas slot. All canvas-slot layout writes must
  target the outermost container.
- `UBlueprint.ParentClass` and `WidgetBlueprint.Animations` and
  `UUserWidget.WidgetTree` are protected from 4.27 Python. Inspect parents
  via asset registry tags (`ParentClass`, `NativeParentClass`); reach
  runtime child widgets via `unreal.find_object(None,
  "<widget_path>.WidgetTree.<ChildName>")`.
- Runtime-constructed TextBlocks default to Roboto. Clone `Font` from a
  designed text block found in the tree, or titles will not match.
- Editing a placed actor's properties via bridge python does NOT dirty the
  level package; `save_dirty_packages` is then a silent no-op that still
  returns True. Use `EditorLevelLibrary.save_current_level()` and verify
  the .umap LastWriteTime on disk.
- The engine ticks on while handlers run; a frozen `UpdateTitlesAtFrame`
  test state (auto-drive off) applies only to the PIE copy of the actor
  and dies with the session.

## 6. Verification

- Freeze-frame recipe (PIE): set `auto_drive_from_world_time` False on the
  PIE actor, call `UpdateTitlesAtFrame(frame)` twice across two bridge
  calls (one tick apart), then console `Shot SHOWUI`. Screenshots land in
  Saved/Screenshots/Windows/.
- Choose frames from the cue data: fadeout start = StartFrame + FadeIn +
  Hold (full opacity, peak glitch).
- Runtime-created widgets prove out via
  `find_object(... .WidgetTree.Cue_011_Group)` returning a Border and the
  on-screen text matching the cue fields.
- Failure signatures: title missing at its window = wrong StartFrame or
  empty Text on a single-line type; all titles frozen = auto-drive off or
  game paused; title invisible but text set = Slate reparent no-op (see
  gotcha 1).
