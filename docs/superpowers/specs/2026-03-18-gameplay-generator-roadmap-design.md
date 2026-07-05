# Gameplay Generator Roadmap -- Design Spec

Close the gap between "asset generator" and "autonomous gameplay generator" for UE4.27.
The bridge already has a working generation plane (prompt -> spec -> assets). This roadmap
adds the validation and repair planes, deepens the authoring surface, and replaces the
keyword-matching prompt interpreter with one that produces unique, prompt-faithful specs.

## Current State

The bridge has three layers that already work:

- `prompt_to_spec.py` -- converts a text prompt to a `BuildSpec` (keyword matching, hardcoded genre templates)
- `spec_schema.py` -- `BuildSpec` covering Blueprints, Widgets, Materials, Levels, AI (BT/BB/EQS), Audio, Sequencer, Localization, Cook labels, Anim assets
- `promptbrush.py` -- full generation pipeline: spec -> assets -> compile -> manifest

What is missing or shallow:

| Gap | Current state |
|---|---|
| PIE test harness | `acceptance_tests: List[str]` exists in BuildSpec but nothing launches PIE or asserts |
| Runtime telemetry | No capture of logs, AI state, possession, widget visibility after PIE |
| Authoring depth | Timelines absent; BT/EQS are asset stubs with no node graph content; ABP has no generator |
| Incremental repair | Compile loop retries saves but does not parse errors or patch graphs |
| Import pipeline | No handler for importing external meshes/textures/skeletons |
| Packaging validation | Cook labels generated but no cook execution or result validation |
| Source-control writes | No checkout/lock before saving .uasset files |
| Prompt interpreter | Keyword matching; produces same Blueprint set regardless of prompt |

## Three Planes

```
Generation Plane:   prompt -> spec -> assets         (exists, needs depth)
Validation Plane:   PIE -> telemetry -> assertions   (missing)
Repair Plane:       failure analysis -> patch -> recompile  (stub only)
```

All three planes run inside the same bridge architecture: `POST /` -> `router.py` ->
handlers. No new ports, no new listener process, no new URL namespace.

## Assistant Runtime Testing Policy

The validation plane defines runtime tools, but assistant authoring workflows
should not start PIE automatically when an edit is complete. Default to
editor-side validation, then hand off to the user for testing in Unreal. Use
`gameplay_pie_start`, telemetry, and runtime acceptance checks only when the
user explicitly requests runtime testing or the task is about PIE behavior.

## Architecture

### New files

```
unreal-plugin/Content/Python/mcp_bridge/
  handlers/
    gameplay.py              -- PIE control, telemetry capture (new handler)
  generation/
    pie_harness.py           -- PIE launch, assertion runner
    telemetry_capture.py     -- log scraping, AI state, possession, class resolution cache
    repair_engine.py         -- compile error parsing, spec and graph repair
    timeline_generator.py    -- Timeline asset creation and keyframe authoring
    import_pipeline.py       -- mesh/texture/skeleton import with reimport support
    cook_validator.py        -- cook subprocess trigger and log parser
    reference_validator.py   -- redirector resolution and soft-reference integrity
    intent_extractor.py      -- prompt -> IntentMap (actors, mechanics, relationships)
    spec_assembler.py        -- IntentMap -> BuildSpec via mechanics registry
    mechanics/               -- one file per mechanic, each returns BuildSpecFragment
      __init__.py
      player_movement.py
      collect_item.py
      door_trigger.py
      enemy_patrol.py
      hide_from_enemy.py
      main_menu.py
      game_over.py
  utils/
    source_control.py        -- checkout_or_add, lock inspection before .uasset writes

mcp-server/src/tools/
  gameplay.ts                -- TypeScript tool definitions for all gameplay_* commands
```

### Modified files

```
generation/prompt_to_spec.py    -- delegates to intent_extractor + spec_assembler
generation/spec_schema.py       -- add TimelineSpec, PIETestSpec, IntentMap, IntentRelation,
                                   BuildSpecFragment, ClassResolutionCache
generation/ai_generator.py      -- BT node graph content via C++ plugin (see Phase 3b)
generation/compile_loop.py      -- wire repair_engine into retry loop
handlers/promptbrush.py         -- add PIE harness phase after asset generation;
                                   add reference validation phase before manifest write
router.py                       -- register gameplay_* commands
mcp-server/src/index.ts         -- register gameplay tools
```

### Command namespace

New MCP commands all use `gameplay_` prefix. Each requires a matching TypeScript tool
definition in `mcp-server/src/tools/gameplay.ts` following the same pattern as
`mcp-server/src/tools/blueprints.ts`.

| Command | Handler | Purpose |
|---|---|---|
| `gameplay_pie_start` | gameplay.py | Launch PIE session |
| `gameplay_pie_stop` | gameplay.py | End PIE session |
| `gameplay_telemetry_snapshot` | gameplay.py | Capture current runtime state |
| `gameplay_run_acceptance_tests` | gameplay.py | Run BuildSpec acceptance_tests against live PIE |
| `gameplay_cook_validate` | gameplay.py | Trigger cook subprocess and return parsed errors |
| `gameplay_import_asset` | gameplay.py | Import external file into content browser |
| `gameplay_validate_references` | gameplay.py | Check and repair soft references and redirectors |

Minimum Zod schema fields per command:

| Command | Required params | Optional params |
|---|---|---|
| `gameplay_pie_start` | (none) | `level_path: string` |
| `gameplay_pie_stop` | (none) | (none) |
| `gameplay_telemetry_snapshot` | (none) | (none) |
| `gameplay_run_acceptance_tests` | `tests: string[]` | `timeout_seconds: number` |
| `gameplay_cook_validate` | `map_path: string` | `platform: string`, `packages_only: boolean` |
| `gameplay_import_asset` | `source_path: string`, `content_path: string` | `options: object` |
| `gameplay_validate_references` | `manifest_path: string` | (none) |

Note: `gameplay_pie_simulate_input` is deferred. UE4.27 Python has no direct API for
injecting input actions into a running PIE `PlayerController` from an external caller.
Revisit when a C++ input injection plugin is available.

## Phased Roadmap

### Phase 1 -- Validation Plane (PIE harness + telemetry)

**What:** Add the ability to launch PIE, capture runtime state, and evaluate
`acceptance_tests` in a BuildSpec against observed outcomes.

**Why first:** Every other gap depends on knowing whether the game actually works.
Compile success is not gameplay success.

**UE4.27 Python API surface:**

PIE control:
- `unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).play_in_editor(in_editor=False)` -- launches PIE in a separate window, avoiding game-thread blocking
- `unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).request_end_play_map()` -- ends PIE
- PIE is asynchronous. After calling `play_in_editor()`, the harness polls the log file
  for the string `"PIE: play in editor start"` (the exact string UE4.27 emits when PIE is
  ready). Poll interval: 0.5s, max wait: 30s, via `register_slate_post_tick_callback`.
  If the marker is not found within 30s, `wait_for_pie_ready` returns False and
  `run_assertions` fails all predicates with `observed: "PIE timeout"`.

PIE world access:
- `unreal.EditorLevelLibrary` is for the **world editor** and explicitly should not be used
  in PIE mode. Once PIE is active, use `unreal.EditorLevelLibrary.get_pie_worlds(False)`
  or `unreal.EditorLevelLibrary.get_game_world()` to get the live game world.
- All `GameplayStatics` calls must use this PIE world, not the editor world.
- `unreal.GameplayStatics.get_player_controller(pie_world, 0).get_controlled_pawn()` -- possession check

Log capture:
- UE4.27 Python does not provide a log subscription API. Log scraping reads the current
  session log file from `unreal.Paths.project_log_dir() + ProjectName + ".log"`.
  A cursor position is stored per snapshot so only new lines since the last snapshot are returned.

Widget visibility:
- `unreal.WidgetBlueprintLibrary` does not expose a runtime widget stack inspector,
  but `unreal.WidgetLibrary.get_all_widgets_of_class(pie_world, widget_class, ...)` does.
- The `widget_visible` predicate uses two strategies:
  1. **Direct runtime query** (preferred): resolve the widget class from the class resolution
     cache and call `get_all_widgets_of_class`. If any instance is found and visible, pass.
  2. **Log marker fallback**: if class resolution fails, check for `[WidgetVisible:<name>]`
     in the log. Generated Widget Blueprints emit this line via a `PrintString` node on
     their Construct event. This is a pipeline convention, not an API guarantee.

Runtime class resolution cache:
- Phase 1 introduces `ClassResolutionCache` (in `telemetry_capture.py`) that maps asset names
  to resolved UClass objects and content paths. Populated during generation and reused by
  assertions and repair. Avoids repeated `load_object` calls during PIE.

**New files:**
- `generation/pie_harness.py` -- `launch_pie()`, `stop_pie()`, `wait_for_pie_ready(timeout_s=30) -> bool`, `run_assertions(tests: List[PIETestSpec]) -> List[AssertionResult]`
- `generation/telemetry_capture.py` -- `snapshot(pie_world) -> TelemetryFrame`, `ClassResolutionCache`
- `handlers/gameplay.py` -- wraps harness and capture behind MCP commands

**Predicate format:** `acceptance_tests` entries use a structured `PIETestSpec` dataclass:

```python
@dataclass
class PIETestSpec:
    predicate: str           # "pawn_possessed", "widget_visible", "log_contains",
                             # "ai_state", "survive"
    target: Optional[str]    # class name, widget name, log string, actor name
    expected: Optional[str]  # expected state value for ai_state
    timeout_seconds: float = 5.0
```

String shorthand in `acceptance_tests: List[str]` is still supported for backwards
compatibility and parsed into `PIETestSpec` objects at runtime:

```
"pawn_possessed:BP_Character"     -> PIETestSpec("pawn_possessed", "BP_Character")
"widget_visible:WBP_HUD"         -> PIETestSpec("widget_visible", "WBP_HUD")
"log_contains:GameStarted"       -> PIETestSpec("log_contains", "GameStarted")
"ai_state:BP_Enemy:Patrol"       -> PIETestSpec("ai_state", "BP_Enemy", expected="Patrol")
"survive:5"                      -> PIETestSpec("survive", timeout_seconds=5.0)
```

The `survive` predicate waits N seconds then checks that no `Fatal:` or
`Error: (Assertion failed)` line appeared in the log. If PIE crashes and the HTTP
listener stops responding, `run_assertions` returns timeout failures for all remaining predicates.

**TypeScript:** `mcp-server/src/tools/gameplay.ts` defines all commands using the schema
table above, following the same pattern as `blueprints.ts`.

**Success criteria:**
- `gameplay_pie_start` launches PIE and returns `{success: true}` once the log marker is found
- `gameplay_run_acceptance_tests` returns `[{predicate, passed, observed}]` per predicate
- `widget_visible:WBP_HUD` passes via runtime widget query, not just log marker
- A PromptBrush run can assert `log_contains:TriggerFired` and receive a real pass/fail

**Prerequisites:** None.

---

### Phase 2 -- Prompt Interpreter (unique specs)

**What:** Replace `prompt_to_spec.py`'s keyword matching and hardcoded genre templates
with a structured interpreter that reads the prompt and produces a unique `BuildSpec`
reflecting what was actually asked.

**Why:** "Make a horror game" currently always produces the same Blueprint list.
Full autonomous generation requires the spec to vary with the prompt -- including
the relationships between actors and mechanics, not just their presence.

**Approach:** Two-pass strategy:

1. **Intent extraction** -- parse the prompt for: genre, named actors, named mechanics,
   win/lose conditions, named maps, named UI screens, named audio cues, and crucially,
   **relationships** between actors and mechanics (e.g. "door opens when all coins are
   collected" is a relationship, not just two actors).

2. **Spec assembly** -- build `BuildSpec` from `IntentMap` using a mechanics registry.
   Each mechanic exports `fragment(intent: IntentMap) -> BuildSpecFragment`. Fragments
   are merged by concatenating lists and merging dicts.

`IntentMap` fields:
```python
@dataclass
class IntentRelation:
    source: str              # e.g. "door"
    verb: str                # e.g. "opens_when"
    target: str              # e.g. "all_coins_collected"
    condition: Optional[str] # e.g. "coin_counter >= total_coins"
    timing: Optional[str]    # e.g. "on_overlap", "on_timer", "on_trigger"
    scope: Optional[str]     # e.g. "level", "actor", "global"


@dataclass
class IntentMap:
    genre: str                           # detected or "generic"
    actors: List[str]                    # named entities ("coin", "door", "ghost")
    mechanics: List[str]                 # detected verb phrases ("collect", "hide", "open_when")
    relationships: List[IntentRelation]  # causal/temporal links between actors and mechanics
    win_condition: Optional[str]         # "all_coins_collected", "reach_exit", etc.
    lose_condition: Optional[str]        # "caught_by_enemy", "timer_expired", etc.
    map_names: List[str]                 # named levels ("dungeon", "lobby")
    ui_screens: List[str]                # named UI ("hud", "game_over", "main_menu")
    audio_cues: List[str]                # named sounds ("footstep", "coin_collect")
    raw_prompt: str                      # original prompt text
```

`BuildSpecFragment` is a partial `BuildSpec` (all list fields default to empty, no required
fields). The assembler merges fragments by appending all lists and taking the first non-None
value for scalar fields.

Starter mechanics registry (minimum viable):
- `mechanics/player_movement.py` -- Character, PlayerController, input mappings
- `mechanics/collect_item.py` -- pickup actor, counter variable, win trigger Blueprint graph
- `mechanics/door_trigger.py` -- door Blueprint with TimelineSpec lerp-open
- `mechanics/enemy_patrol.py` -- AI enemy with BehaviorTreeSpec Sequence/MoveTo/Wait
- `mechanics/hide_from_enemy.py` -- AI perception, stealth variable on PlayerCharacter, lose trigger
- `mechanics/main_menu.py` -- WBP_MainMenu with Play/Quit buttons
- `mechanics/game_over.py` -- WBP_GameOver with score display and retry

**New files:**
- `generation/intent_extractor.py` -- `extract_intent(prompt: str) -> IntentMap`
- `generation/spec_assembler.py` -- `assemble(intent: IntentMap) -> BuildSpec`
- `generation/mechanics/__init__.py` + 7 mechanic files listed above

**Modified files:**
- `generation/prompt_to_spec.py` -- delegates to `extract_intent` + `assemble`
- `generation/spec_schema.py` -- add `IntentMap`, `IntentRelation`, `BuildSpecFragment`

**Success criteria:**
- Prompt "player collects coins and a door opens when all coins are collected" produces
  `BP_Coin`, `BP_Door`, `BP_GameMode` with a coin counter -- but not `BP_Enemy`. The
  `IntentMap.relationships` contains `IntentRelation(source="door", verb="opens_when", target="all_coins_collected")`.
- Prompt "player must hide from a monster" produces `BP_Enemy`, `BP_AIController`,
  stealth variable on `BP_PlayerCharacter` -- but not `BP_Coin` or `BP_Door`
- Neither output contains hardcoded genre template names from the old `prompt_to_spec.py`

**Prerequisites:** None (parallel to Phase 1).

---

### Phase 3a -- Timelines

**What:** Add `TimelineSpec` to `spec_schema.py` and `timeline_generator.py` that creates
`UCurveFloat` / `UCurveVector` assets and inserts `UK2Node_Timeline` graph nodes via
a new C++ plugin pass (Pass 12).

**Why:** Timelines are the most common way to drive smooth movement, fade effects, and
timed door-open sequences in UE4 gameplay Blueprints. Without them, generated content
can only express instant state changes.

**Approach:**
1. Create `UCurveFloat` / `UCurveVector` assets via `AssetToolsHelpers`
2. Emit a `TimelineNode` entry in the Blueprint's `graph_json`
3. Pass 12 of `BlueprintGraphBuilderLibrary` processes it into a `UK2Node_Timeline`
   wired to the curve asset

Note: Timelines cannot be added to a Blueprint's SCS from Python -- the SCS node API
for `TimelineComponent` is not exposed in UE4.27.

`TimelineSpec` fields:
```python
@dataclass
class TimelineSpec:
    name: str                          # e.g. "DoorOpen"
    content_path: str                  # where to store the CurveFloat asset
    duration: float                    # seconds
    tracks: List[Dict[str, Any]]       # [{"name", "type": "float|vector", "keyframes": [{"time", "value"}]}]
    auto_play: bool = False
    loop: bool = False
```

**New files:**
- `generation/timeline_generator.py` -- creates CurveFloat assets and returns `graph_json` fragment
- `ue4-plugin/BlueprintGraphBuilder/Private/Pass12_Timeline.cpp` -- C++ pass 12: `UK2Node_Timeline` insertion

**Modified files:**
- `generation/spec_schema.py` -- add `TimelineSpec`, add `timelines: List[TimelineSpec]` to `BuildSpec`
- `generation/blueprint_generator.py` -- call `timeline_generator` before graph build

**Success criteria:**
- `UK2Node_Timeline` node is present in the door Blueprint's event graph (verifiable in the Blueprint editor, no PIE required)
- `CurveFloat` asset exists at the specified `content_path`
- Bonus (requires Phase 1): door rotates over 1 second in PIE when trigger fires

**Prerequisites:** None. Phase 1 is recommended for full runtime validation but not required to merge.

---

### Phase 3b -- Behavior Tree Node Graphs

**What:** Add actual node graph content to generated Behavior Trees. Currently
`generate_behavior_tree()` creates a BT asset shell with a Blackboard assigned but no
composite or task nodes -- enemies stand still.

**API constraint:** `UBehaviorTreeGraphNode` is in the `BehaviorTreeEditor` module and
is not exposed to UE4.27 Python. Requires a C++ plugin extension.

**Approach:** Add `BehaviorTreeBuilderLibrary` to the existing `BlueprintGraphBuilder`
plugin (`Private/BehaviorTreeBuilder/`):

```cpp
static bool BuildBehaviorTreeFromJSON(
    UBehaviorTree* BehaviorTree,
    const FString& JsonString,
    FString& OutError
);
```

JSON schema (mirrors `BehaviorTreeSpec.root`):
```json
{
  "type": "Sequence",
  "children": [
    {"type": "BTTask_MoveTo", "params": {"accept_radius": 50.0}},
    {"type": "BTTask_Wait", "params": {"wait_time": 2.0}}
  ]
}
```

**New files:**
- `ue4-plugin/BlueprintGraphBuilder/Private/BehaviorTreeBuilder/BehaviorTreeBuilderLibrary.h`
- `ue4-plugin/BlueprintGraphBuilder/Private/BehaviorTreeBuilder/BehaviorTreeBuilderLibrary.cpp`

**Modified files:**
- `generation/ai_generator.py` -- call `BehaviorTreeBuilderLibrary.BuildBehaviorTreeFromJSON` after shell creation
- `ue4-plugin/BlueprintGraphBuilder/BlueprintGraphBuilder.Build.cs` -- add `AIModule`, `BehaviorTreeEditor`

**Success criteria:** Generated enemy BT contains Selector -> Sequence -> BTTask_MoveTo ->
BTTask_Wait visible in the BT editor. Enemy moves and waits in PIE.

**Prerequisites:** None.

---

### Phase 3c -- Anim Blueprint State Machines

**What:** Add an Anim Blueprint generator. `AnimBlueprintSpec` exists in the schema
but no generator creates ABP assets or wires state machine transitions.

**API constraint:** `UAnimationStateMachineGraph` is in the `AnimGraph` module and is
not exposed to UE4.27 Python. Requires a C++ plugin extension.

**Approach:** Add `AnimBlueprintBuilderLibrary` to the existing plugin (`Private/AnimBuilder/`):

```cpp
static bool BuildAnimBlueprintFromJSON(
    UAnimBlueprint* AnimBlueprint,
    const FString& JsonString,
    FString& OutError
);
```

JSON schema (mirrors `AnimBlueprintSpec.state_machines`):
```json
{
  "state_machines": [{
    "name": "LocomotionSM",
    "states": [
      {"name": "Idle", "anim_sequence_path": "/Game/Anims/Idle"},
      {"name": "Walk", "anim_sequence_path": "/Game/Anims/Walk"}
    ],
    "transitions": [
      {"from": "Idle", "to": "Walk", "condition": "Speed > 10"}
    ]
  }]
}
```

**New files:**
- `generation/anim_blueprint_generator.py` -- `generate_anim_blueprint(spec: AnimBlueprintSpec)`
- `ue4-plugin/BlueprintGraphBuilder/Private/AnimBuilder/AnimBlueprintBuilderLibrary.h`
- `ue4-plugin/BlueprintGraphBuilder/Private/AnimBuilder/AnimBlueprintBuilderLibrary.cpp`

**Modified files:**
- `handlers/promptbrush.py` -- add `generate_all_anim_blueprints` call
- `ue4-plugin/BlueprintGraphBuilder/BlueprintGraphBuilder.Build.cs` -- add `AnimGraph`, `AnimGraphRuntime`

**Success criteria:** Generated character has an ABP with Idle/Walk state machine visible
in the editor. Character plays Idle and transitions to Walk at `Speed > 10` in PIE.

**Prerequisites:** None.

---

### Phase 4 -- Incremental Repair Engine

**What:** When Blueprint compilation fails, parse the error, patch the spec or graph
fragment that caused it, and recompile -- without regenerating the full BuildSpec.

**Why:** The current compile loop retries the same `save_asset()` call up to 3 times.
Retrying a broken graph produces the same failure every time.

**Error source:** Two sources, read in sequence:
1. Exception string from `save_asset()` (immediate, may be partial)
2. New `LogKismet: Error:` lines from the log file, scraped via the log-cursor utility
   (shared with `telemetry_capture.py`)

**Two repair target classes:**

`SpecRepairAction` -- changes `BlueprintSpec` fields without touching generated graph JSON.
Use when the intent itself is wrong (e.g. wrong parent class, missing variable declaration).

`GraphRepairAction` -- patches the generated `graph_json` without changing the `BlueprintSpec`.
Use when the intent is correct but the builder produced bad output (e.g. bad function
reference, pin type mismatch in emitted JSON).

**The repair engine prefers `GraphRepairAction` first.** Spec-level repair is used only
when graph repair cannot satisfy the spec (e.g. the requested function does not exist
on any accessible class).

**Repair strategies by error class:**

| Error pattern | Repair type | Action |
|---|---|---|
| `Cannot find function 'X'` | GraphRepair | Remove the node calling X from `graph_json` |
| `Variable 'X' not found` | SpecRepair | Add variable X with inferred type to `BlueprintSpec.variables` |
| `Pin type mismatch: A to B` | GraphRepair | Insert conversion node between mismatched pins |
| `Circular dependency` | GraphRepair | Remove cross-reference node from dependent Blueprint |
| `Missing parent class` | SpecRepair | Set `parent_class` to `Actor` |

**New files:**
- `generation/repair_engine.py`:
  - `parse_compile_errors(log_lines: List[str], exception: str) -> List[Union[SpecRepairAction, GraphRepairAction]]`
  - `apply_repair(spec: BuildSpec, bp_name: str, action: RepairAction) -> BuildSpec`

**Modified files:**
- `generation/compile_loop.py` -- on failure, scrape log, call repair engine, retry

**Success criteria:**
- Blueprint with broken function reference compiles after one graph repair pass
- Blueprint with missing variable compiles after one spec repair pass (variable added)
- Blueprint with pin type mismatch compiles after one graph repair pass (conversion node inserted)
- All three verified without human input

**Prerequisites:** The log-cursor file-reading utility from `telemetry_capture.py` is
reused here. If implementing Phase 4 before Phase 1, write the cursor logic inline in
`repair_engine.py` and extract it to `telemetry_capture.py` when Phase 1 is built.

---

### Phase 4.5 -- Reference and Redirector Integrity

**What:** Validate all generated asset paths in the manifest, resolve redirectors, verify
soft references still point to live assets, and rewrite dangling soft object paths after
renames or moves. Fail the run if unresolved dangling references remain.

**Why:** UE4.27 asset automation breaks silently when assets are renamed, moved, or
regenerated across PromptBrush runs. `save_asset()` succeeds even when a Blueprint holds
a stale soft reference to an asset that was deleted or moved in a previous run. This goes
undetected until PIE or cook.

**UE4.27 Python API surface:**
- `unreal.AssetTools.find_soft_references_to_object(asset)` -- find all referencing assets
- `unreal.AssetTools.rename_assets([move_data])` -- atomic rename + redirector creation
- `unreal.EditorAssetLibrary.does_asset_exist(path)` -- quick live-asset check
- `unreal.AssetRegistryHelpers.get_asset_registry()` -- query all known assets

**Validation steps:**
1. For each asset path in manifest, call `does_asset_exist()` -- fail if missing
2. For each asset, get its soft references and verify each target exists
3. For assets that have moved (old path in manifest vs. new path in registry), call
   `rename_assets` to create a redirector from old to new
4. Report all dangling references that could not be auto-resolved

**New files:**
- `generation/reference_validator.py`:
  - `validate_manifest(manifest_path: str) -> ReferenceReport`
  - `resolve_redirectors(asset_paths: List[str]) -> List[str]`
  - `find_dangling_refs(asset_paths: List[str]) -> List[DanglingRef]`

**Modified files:**
- `handlers/promptbrush.py` -- run reference validation before writing the final manifest
- `router.py` -- register `gameplay_validate_references`
- `handlers/gameplay.py` -- `handle_validate_references` wraps `reference_validator`

**Success criteria:**
- A PromptBrush run where a Blueprint was regenerated with a new content path reports
  the old path as a redirector, not a missing asset
- A run where a soft reference was broken (target asset deleted) fails with a
  `DanglingRef` report naming the source and target assets
- A clean run with all assets intact returns `{dangling: [], redirectors_created: N}`

**Prerequisites:** Phase 1 (PIE failures due to dangling refs are easier to diagnose
with telemetry; Phase 4.5 prevents them from reaching PIE in the first place).

---

### Phase 5 -- Import Pipeline

**What:** Import external files (FBX meshes, PNG textures, WAV audio, skeletal meshes)
into the content browser, handle reimport when the source file changes, and normalize
imported assets (auto-assign materials, fix skeleton references, generate LODs).

**Import policy decision:** PromptBrush treats imported assets as **external sources**.
They are reimported from their source file path; in-editor edits are not protected.
`reimport_asset()` always overwrites the content-browser asset from the source file.
This is correct for automated pipelines where the source file is authoritative.

**UE4.27 Python API surface:**
- `unreal.AssetImportTask` -- configure one import (source, destination, options)
- `unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])` -- batch import
- `unreal.FbxImportUI` -- FBX options: `import_mesh`, `import_as_skeletal`, `skeleton`, `generate_lod_group`
- `unreal.EditorAssetLibrary.reimport_async(path)` -- reimport from original source
- `unreal.ImportSubsystem` -- post-import delegate for normalization

**New files:**
- `generation/import_pipeline.py`:
  - `import_file(source_path: str, content_path: str, options: Dict[str, Any]) -> ImportResult`
  - `reimport_asset(content_path: str) -> ImportResult`
  - `normalize_skeletal_mesh(content_path: str, skeleton_path: str) -> bool`

**Modified files:**
- `router.py` -- register `gameplay_import_asset`
- `handlers/gameplay.py` -- `handle_import_asset` calls `import_pipeline.import_file`

**Success criteria:** FBX character mesh imports, receives auto-assigned materials via
post-import hook, gets skeleton assigned to an existing `USkeleton` -- one command, no dialogs.

**Prerequisites:** None (parallel to all other phases).

---

### Phase 6 -- Packaging Validation

**What:** Trigger a cook and parse the cook log to verify generated assets cooked
cleanly -- no missing references, no stripped content.

**Why:** Assets that compile in-editor can fail to cook: unresolved soft references,
editor-only code in runtime Blueprints, or AssetManager rules that exclude generated content.

**Two cook modes:**

1. **Package-scoped** (fast, default): cook only the packages listed in the latest
   manifest's `primary_asset_labels`. Uses `PrimaryAssetLabel` assets already generated
   by the pipeline. Suitable for CI-style iteration checks.

2. **Map-scoped** (thorough, opt-in via `map_path` param): full map cook.
   Required before shipping.

Both modes use the editor commandlet subprocess approach -- NOT `RunUAT.bat` from inside
the editor (file lock conflict). The subprocess is:

```
UE4Editor-Cmd.exe <ProjectFile> -run=Cook -TargetPlatform=WindowsNoEditor
  -fileopenlog -stdout -unattended -NoLogTimes [-map=<MapPath>]
```

`UE4Editor-Cmd.exe` path is resolved at runtime:
```python
unreal.Paths.engine_dir() + "Binaries/Win64/UE4Editor-Cmd.exe"
```
Project file path: `unreal.Paths.get_project_file_path()`.

All assets must be saved before the subprocess starts (the subprocess reads from disk,
not from editor memory).

**New files:**
- `generation/cook_validator.py`:
  - `run_cook(map_path: Optional[str] = None) -> CookResult`
  - `parse_cook_log(log: str) -> List[CookError]`

`CookError` fields: `asset_path`, `error_type` (`missing_ref`, `editor_only`, `stripped`), `message`

**Modified files:**
- `router.py` -- register `gameplay_cook_validate`
- `handlers/gameplay.py` -- `handle_cook_validate`, passes `map_path=None` for package-scoped mode
- `handlers/promptbrush.py` -- optional cook validation phase after manifest write

**Success criteria:**
- Package-scoped cook (`packages_only: true`) returns results for just the latest manifest's assets
- Map-scoped cook returns full `CookError` list or `{success: true, errors: []}`
- Cook subprocess exits within 5 minutes for a standard generated level

**Prerequisites:** Phase 1 (verify PIE works before paying cook cost), Phase 5 (real assets before cook).

---

### Phase 7 -- Source-Control Aware Writes

**What:** Before saving any `.uasset` file, check it out from source control and mark
new files for add. If a file is locked, report the lock owner instead of overwriting.

**Why:** Silent overwrite of a checked-out `.uasset` causes binary merge conflicts in
Perforce. Git LFS requires explicit `git add` for new binary assets.

**UE4.27 Python API surface:**
- `unreal.SourceControl.check_out_or_add_file(path)` -- preferred single call for the
  common case: checks out if existing, marks for add if new
- `unreal.SourceControl.get_file_states([paths])` -- inspect `SourceControlState` for
  lock owner, read-only status, check-out status
- `unreal.SourceControl.get_provider()` -- returns active provider or None

**Write flow for every generator:**
1. Resolve asset file path
2. Call `SourceControl.check_out_or_add_file(path)`
3. If False, inspect `SourceControlState` for lock owner and return `{success: false, error: "Locked by: <owner>"}`
4. Only then call `save_asset()`

**Behavior when no source control is configured:** `get_provider()` returns None.
`checkout_before_write` returns True immediately, all writes proceed.

**New files:**
- `utils/source_control.py`:
  - `checkout_before_write(path: str) -> bool`
  - `get_lock_owner(path: str) -> Optional[str]`

**Modified files:** Nine generator files (`blueprint_generator.py`, `widget_generator.py`,
`asset_generator.py`, `level_generator.py`, `audio_generator.py`, `sequence_generator.py`,
`localization_generator.py`, `cook_generator.py`, `ai_generator.py`) -- each `save_asset()`
call preceded by `source_control.checkout_before_write()`.

**Success criteria:**
- Existing Blueprint in Perforce workspace is checked out before overwriting
- New Blueprint is marked for add
- Locked file returns error with lock owner, skips asset without crash
- No SC provider configured: all writes proceed without error

**Prerequisites:** None. Lowest urgency -- relevant only in team environments.

---

## Dependency Graph

```
Phase 1 (PIE harness)       -----> Phase 4 (log-cursor utility reused)
Phase 1 (PIE harness)       -----> Phase 4.5 (telemetry confirms refs before PIE)
Phase 1 (PIE harness)       -----> Phase 6 (verify PIE before cook investment)
Phase 4.5 (ref integrity)   -----> Phase 6 (clean refs before cook)
Phase 5 (import pipeline)   -----> Phase 6 (real assets before cook)
Phase 2 (prompt interp)     independent
Phase 3a (timelines)        independent
Phase 3b (BT graphs)        independent (C++ plugin work)
Phase 3c (anim BP)          independent (C++ plugin work)
Phase 7 (source control)    independent
```

Recommended build order:
1. Phase 1 (PIE harness)
2. Phase 2 + Phase 3a + 3b + 3c in parallel
3. Phase 4 (after Phase 1, log cursor)
4. Phase 4.5 (after Phase 1)
5. Phase 5 (any time)
6. Phase 6 (after Phase 1 + Phase 4.5 + Phase 5)
7. Phase 7 (any time)

## What This Does Not Cover

- **Multiplayer / replication** -- UE4.27 Python does not expose replication graph authoring. RPCs require C++ or manual Blueprint editing.
- **Animation retargeting** -- Editor UI only, no Python API.
- **Packaging to non-Windows platforms** -- Cross-compilation toolchains are outside the bridge's scope.
- **PIE crashes that kill the editor process** -- The harness returns timeout failures. A crash watchdog is out of scope.
- **Input simulation during PIE** -- No Python API for injecting actions into a running PIE PlayerController. Deferred pending a C++ injection plugin.
- **Content quality** -- The bridge generates structurally correct assets. Fun, balance, and polish are outside the scope of automation.
