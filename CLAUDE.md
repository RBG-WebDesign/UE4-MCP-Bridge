# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Context
This is a local tooling bridge that lets Claude Code control UE4.27's editor through MCP.
Three components: TypeScript MCP server, Python listener inside UE4, and .mcp.json config.
The MCP server talks stdio to Claude Code and HTTP to the Python listener.
The Python listener executes commands against UE4's Python API on the game thread.

## Build and Dev Commands
```bash
npm install              # install all dependencies (workspace root)
npm run build            # compile TypeScript (mcp-server/src -> mcp-server/dist)
npm run dev              # watch mode -- recompiles on file changes
npm test                 # run unit tests (mock server, no UE4 needed)
npm run test:integration # run integration tests (requires UE4 running with listener)
```

Run a single test file directly:
```bash
npx tsx mcp-server/tests/actor-tools.test.ts
```

No linter is configured. No external test runner (no jest/mocha) -- tests use `tsx` to run TypeScript directly with a custom assert pattern. Unit tests live in `mcp-server/tests/` and use a mock HTTP server (`mock-server.ts`) that simulates UE4 responses -- no UE4 needed. Integration tests live in `mcp-server/tests/integration/` and hit the real UE4 listener.

`npm test` chains four unit test files in sequence: `actor-tools.test.ts`, `level-viewport-tools.test.ts`, `material-blueprint-tools.test.ts`, `gameplay-tools.test.ts`. If one fails, later files do not run.

The MCP server entry point is `mcp-server/dist/index.js` (ESM). Claude Code discovers it via `.mcp.json`. The workspace root `package.json` delegates all scripts to the `mcp-server` workspace.

### Prerequisites
- UE4.27 with the Python Editor Script Plugin enabled
- Node.js 18+

## Architecture

```
Claude Code --stdio--> MCP Server (TypeScript) --HTTP POST /:8080--> Python Listener (inside UE4) --> unreal module
                                                                                                   --> BlueprintGraphBuilder (C++ plugin, via Python bindings)

ShaderWeave (web app, future) --HTTP /shaderweave/v1/*:8080--> Python Listener (same) --> material custom expression ops
```

### MCP Server (`mcp-server/src/`)
- `index.ts` -- entry point, registers all tools, starts stdio transport
- `unreal-client.ts` -- the sole HTTP client that talks to UE4 (localhost:8080, 60s timeout)
- `types.ts` -- shared `ToolDefinition` interface (name, Zod schema, handler)
- `history.ts` -- undo/redo/checkpoint tracking
- `validation.ts` -- shared validation helpers
- `tools/` -- one file per tool group (actors, blueprints, level, materials, operations, project, system, viewport). Each exports a `create*Tools(client)` factory returning `ToolDefinition[]`.

`index.ts` collects all tool arrays, builds a lookup map, and tracks which commands are "modifying" (recorded in history for undo). Modifying commands: actor_spawn, actor_modify, actor_delete, actor_duplicate, actor_organize, actor_snap_to_socket, batch_spawn, material_create, material_apply, blueprint_create, blueprint_compile, blueprint_build_from_json, anim_blueprint_build_from_json, level_save.

The `tools/` directory has 11 files:
- `actors.ts`, `blueprints.ts`, `level.ts`, `materials.ts`, `operations.ts`, `project.ts`, `system.ts`, `viewport.ts` -- core tools
- `effects.ts` -- post-processing volumes, camera shakes (spawn/play/trigger), visual effects
- `promptbrush.ts` -- PromptBrush tools: `prompt_generate`, `prompt_status`, `prompt_spec_list`, `widget_build_from_json`
- `gameplay.ts` -- PIE start/stop, acceptance tests, telemetry

### Python Listener (`unreal-plugin/Content/Python/mcp_bridge/`)
- `listener.py` -- HTTP server on background thread, queues commands to game thread via `register_slate_post_tick_callback`
- `router.py` -- dispatches commands to handlers
- `handlers/` -- mirrors the MCP tool groups (actors, blueprints, level, materials, project, system, viewport)
- `utils/` -- serialization, transactions (UE4 undo wrappers), validation

Auto-started by `unreal-plugin/Content/Python/startup.py` when UE4 loads.

### Behavior Tree Workflow
When the user asks to read, inspect, duplicate, replicate, compare, or manipulate Behavior Trees, use the editor C++ bridge first. Do not rely on raw UE4 Python reflection for core Behavior Tree internals, because UE4.27 protects `RootNode`, `BlackboardAsset`, root decorators, and decorator ops from Python.

Default path:
- Use `USFBehaviorTreeReplicationLibrary` from the `Sinfeld_DemoEditor` module.
- Select the Behavior Tree in the Content Browser, then call `unreal.SFBehaviorTreeReplicationLibrary.get_first_selected_behavior_tree()`.
- Export structure with `export_behavior_tree_to_json()` or `export_behavior_tree_to_json_file()`.
- For replication, start from `duplicate_behavior_tree_asset()` and then adjust the duplicated tree.
- Treat graph generation from JSON as a separate editor-tooling layer. Do not hand-edit `.uasset` files directly.

Reference: `docs/BEHAVIOR_TREE_REPLICATION.md`.

### Threading Constraint
UE4's Python environment runs on the game thread. The HTTP server runs on a background thread. All `unreal` module calls must be marshaled to the game thread through `register_slate_post_tick_callback` in `listener.py`. Never call `unreal.*` from the HTTP handler directly.

### HTTP Protocol
The MCP server POSTs JSON to `http://localhost:8080/`:
```json
{"command": "actor_spawn", "params": {"type": "StaticMeshActor", "name": "MyActor"}}
```
The listener always responds with:
```json
{"success": true, "data": {...}, "error": null}
```
The `UnrealClient.sendCommand()` in `unreal-client.ts` is the only code that makes these HTTP calls. Connection errors and timeouts resolve (not reject) with `{success: false}`.

### BlueprintGraphBuilder C++ Plugin (`ue4-plugin/BlueprintGraphBuilder/`)
A UE4.27 editor plugin (C++) that builds Blueprint event graphs, Widget Blueprints, Behavior Trees, and Animation Blueprints from JSON. Compiled inside a UE4 project (copied to `YourProject/Plugins/`), not by `npm run build`. Contains four subsystems:

**Blueprint Graph Builder** (11 passes complete) -- builds event graphs from JSON. Exposes `UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON` to Python. The Python handler for `blueprint_build_from_json` calls this.

**Widget Blueprint Builder** (design spec complete, implementation not started) -- builds UMG Widget Blueprints from JSON. Lives under `Private/WidgetBuilder/` subdirectory. Exposes `UWidgetBlueprintBuilderLibrary` with `BuildWidgetFromJSON`, `RebuildWidgetFromJSON`, `ValidateWidgetJSON`. Spec: `docs/superpowers/specs/2026-03-18-widget-blueprint-builder-design.md`.

**Behavior Tree Builder** (complete) -- builds BT node graphs from JSON with full blackboard support. Lives under `Private/BehaviorTreeBuilder/` subdirectory. Exposes `UBehaviorTreeBuilderLibrary::BuildBehaviorTreeFromJSON`. Supports 26 node types across 4 categories:
- Composites: Selector, Sequence, SimpleParallel
- Tasks: MoveTo, Wait, WaitBlackboardTime, RotateToFaceBBEntry, PlayAnimation, MakeNoise, RunBehavior, PlaySound, FinishWithResult, SetTagCooldown
- Decorators: Blackboard (IsSet/IsNotSet + arithmetic: Equal/NotEqual/Less/LessOrEqual/Greater/GreaterOrEqual), ForceSuccess, Loop, TimeLimit, Cooldown, CompareBBEntries, IsAtLocation, DoesPathExist, TagCooldown, ConditionalLoop, KeepInCone, IsBBEntryOfClass
- Services: DefaultFocus

Blackboard key selectors are resolved via reflection (`ResolveSelectedKey`). Arithmetic conditions on the Blackboard decorator use `EArithmeticKeyOperation` for Int/Float keys, with `int_value`/`float_value` comparison params. Services attach to composite nodes via `UBTCompositeNode::Services`. Editor graph sync creates graph nodes for all categories including services. Spec: `docs/superpowers/specs/2026-03-19-behavior-tree-builder-design.md`.

**Animation Blueprint Builder** (complete) -- builds Animation Blueprints from JSON targeting UE4's AnimGraph system. Lives under `Private/AnimBlueprintBuilder/` subdirectory. Exposes `UAnimBlueprintBuilderLibrary` with `BuildAnimBlueprintFromJSON`, `RebuildAnimBlueprintFromJSON`, `ValidateAnimBlueprintJSON`. v1 supports: bool variables, StateMachine + Slot pipeline, states with SequencePlayer, transitions with bool_variable and time_remaining conditions, event graph delegation to BlueprintGraphBuilder. Spec: `docs/superpowers/specs/2026-03-19-anim-blueprint-builder-design.md`.

### PromptBrush (`promptbrush.ts` + external plugin)
Generates complete UE4.27 gameplay systems from a single natural language prompt. Creates Blueprint classes, Widget Blueprints, materials, data assets, maps, and input mappings in one pass. Exposed as `prompt_generate`, `prompt_status`, `prompt_spec_list` MCP tools. The UE4 side is a separate C++ plugin (`PromptBrush`) that lives outside this repo at `D:\Unreal Projects\CodePlayground\Plugins\PromptBrush\`. The plugin must be copied into the target project's `Plugins/` folder and enabled before `prompt_generate` will work. See `README_PROMPTBRUSH.md` for setup.

### ShaderWeave Bridge (`unreal-plugin/Content/Python/mcp_bridge/shaderweave/`)
A separate product that shares the UE_Bridge HTTP listener. Pushes HLSL into Material Custom Expression nodes and returns compile feedback. Uses its own URL namespace (`/shaderweave/v1/*`) separate from the existing `POST /` command router. ShaderWeave is its own repo/product -- UE_Bridge only hosts the transport and UE4 execution layer. Spec: `docs/superpowers/specs/2026-03-18-shaderweave-bridge-mvp-design.md`.

### Pattern System (`mcp-server/src/patterns/`)
The `blueprint_build_from_description` tool uses a pattern registry to translate natural language into blueprint graph fragments. Patterns are registered via `registerPattern()` in the registry and merged with auto-wiring logic. Available patterns include `on_begin_play`, `print_string`, `print_float`, `get_actor_location`, `loop_print`, `move_actor_up`. To add a new pattern, create it in the patterns directory and register it in the registry.

### Adding a New Tool
1. Prototype using `python_proxy` first
2. Look up any `unreal` module APIs you need via Context7 before writing the handler:
   ```
   Tool: mcp__plugin_context7_context7__query-docs
   libraryId: /radial-hks/unreal-python-stubhub
   query: <class or method name>
   ```
   This has 57K+ snippets with exact signatures, class hierarchies, and property types. Use it to verify method names, parameter types, and return values rather than guessing.
3. Add a Python handler in `unreal-plugin/Content/Python/mcp_bridge/handlers/`
4. Register the command in `router.py`'s `COMMAND_ROUTES` dict (maps command string to handler function)
5. Add the TypeScript tool definition in the matching `mcp-server/src/tools/` file
6. If the tool modifies editor state, add it to the `modifyingCommands` set in `index.ts`
7. Wrap editor mutations in a UE4 transaction using the `@transactional` decorator from `utils/transactions.py`

## UE4.27 C++ API Lookup (unreal-api MCP)

A second MCP server (`unreal-api`) runs alongside `unreal-bridge` and serves UE4.27 C++ API documentation from a SQLite database. Use it to verify C++ API usage instead of guessing.

| When | Tool | Example |
|------|------|---------|
| Unsure about a function's parameters or return type | `search_unreal_api` or `get_function_signature` | `get_function_signature("AActor::GetActorLocation")` |
| Need the `#include` for a type | `get_include_path` | `get_include_path("ACharacter")` |
| Want to see all members on a class | `get_class_reference` | `get_class_reference("UBTCompositeNode")` |
| Searching for an API by keyword | `search_unreal_api` | `search_unreal_api("spawn actor")` |
| Checking if an API is deprecated | `get_deprecation_warnings` | `get_deprecation_warnings("K2_AttachRootComponentTo")` |

**Rules:**
- Before writing a UE C++ API call you haven't verified in this conversation, check it with `get_function_signature`
- Before adding a `#include`, verify with `get_include_path` if unsure
- Covers: all Engine Runtime/Editor/Developer modules, built-in plugins, Blueprint graph internals (UK2Node subclasses, EdGraphSchema, KismetCompiler)
- Does NOT cover: third-party plugins or marketplace assets

**Two API lookup systems are available -- use the right one:**
- **unreal-api MCP** (`search_unreal_api`, `get_function_signature`, etc.) -- for **C++ API**: class hierarchies, function signatures, `#include` paths, deprecation. Use when writing C++ in `ue4-plugin/`.
- **Context7 StubHub** (`mcp__plugin_context7_context7__query-docs` with `/radial-hks/unreal-python-stubhub`) -- for **Python API**: `unreal` module method signatures, property types, Python class wrappers. Use when writing Python handlers in `unreal-plugin/`.

## Architecture Rules
- The MCP server never imports or references Unreal modules. It only sends HTTP.
- The Python listener never imports MCP SDK modules. It only receives HTTP.
- Every tool that modifies editor state must be wrapped in a UE4 transaction for undo support.
- Every actor manipulation tool must support the validate parameter.
- The python_proxy tool is the escape hatch. Any new tool should first be prototyped
  through python_proxy before getting its own dedicated handler.
- Viewport operations (camera moves, mode switches, render modes) are NOT transactable.
  Do not wrap them in UE4 transactions.

## Visual Feedback Loop
After any spatial operation (actor_spawn, actor_modify, actor_duplicate, batch_spawn,
actor_snap_to_socket), use viewport_focus on the affected actor followed by
viewport_screenshot to visually verify the result. For multi-actor operations, use
viewport_fit followed by viewport_screenshot for an overview. This visual feedback
loop is the default behavior, not an optional extra.

## UE Bridge MCP Testing Workflow
When a UE Bridge MCP authoring job is complete, stop after lightweight editor-side
sanity checks and let the user test the result in Unreal first. Do not start PIE
automatically, do not run gameplay runtime checks automatically, and do not ask
what the user wants next unless there is a blocker or a necessary design choice.

Use editor-side checks such as `level_actors`, `asset_info`, `blueprint_compile`,
`placement_validate`, `viewport_fit`, or `viewport_screenshot` when they help
confirm the requested edit exists. Only call `pie_start`, `gameplay_pie_start`,
runtime telemetry, or acceptance-test commands when the user explicitly asks for
a runtime test, or when the current task is specifically to debug PIE behavior.

## UE4.27 API Safety -- Forbidden UE5 Patterns

**Engine target: Unreal Engine 4.27. If an API exists in UE5 but is not confirmed in UE4.27, do not use it. Fall back to known UE4.27 patterns.**

Forbidden tokens and their UE4.27 replacements:

| UE5 (FORBIDDEN) | UE4.27 (USE INSTEAD) | System |
|---|---|---|
| `EnhancedInputComponent` | `InputComponent` | Input |
| `EnhancedInputSubsystem` | `BindAxis` / `BindAction` | Input |
| `UE::Tasks` | `FAsyncTask` / `FTimerManager` | Async |
| `Tasks::Launch` | `SetTimer` / `SetTimerForNextTick` | Async |
| `MassAI` | `BehaviorTree` + `AIController` | AI |
| `SmartObjects` | manual triggers / overlap volumes | AI |
| `StateTree` | `BehaviorTree` | AI |
| `AnimNext` | `UAnimInstance` / `Montage_Play` | Animation |
| `LevelEditorSubsystem` | `GEditor` direct access | Editor |
| `EditorUtilitySubsystem` | `FKismetEditorUtilities` | Editor |
| `EditorPlaySessionSubsystem` | `GEditor->RequestPlaySession` | Play |

When writing or reviewing C++ for this project, scan for any token in the FORBIDDEN column. If found, replace with the UE4.27 equivalent before compiling.

**Camera shake API for UE4.27.2:** This engine version uses the UE5-transitional API: `UCameraShakeBase` (header: `Camera/CameraShakeBase.h`), `StartCameraShake()` on `APlayerCameraManager`. The older `UCameraShake` / `PlayCameraShake` names do not exist in this build.

## Trigger Volume Placement Rules
When spawning any overlap-based trigger actor (ShakeTriggerActor, kill volumes, pickup zones, etc.):

1. **Never place a trigger volume on top of the PlayerStart.** `OnBeginOverlap` only fires on state transition (outside -> inside). If the player spawns already inside the volume, the event never fires.
2. **Minimum clearance:** Place trigger volumes at least 1.5x the volume's extent away from any PlayerStart location.
3. **Verify before spawning:** Query the PlayerStart location and compare against the planned trigger position + extent. Reject or warn if they overlap.
4. **Runtime test pattern:** Ask the user before starting PIE. If they approve a runtime test, start PIE and walk the player into the volume. Check Output Log for the actor's log messages. Do not assume placement is correct without runtime verification.

These rules apply to all tools that spawn overlap-based actors: `camera_shake_trigger`, and any future trigger/zone tools.

## Code Standards
- TypeScript: strict mode, explicit types, no `any`
- Python: type hints on all function signatures, docstrings on all handlers
- All tool handlers must return a consistent JSON shape: `{success: bool, data: any, error?: string}`
- No em dashes in comments or documentation
- No academic filler language (delve, explore, leverage, robust, utilize)
- Write documentation like you're explaining to a programmer, not selling to a VP

### Orchestrator (`orchestrator.mjs`)
A Node.js script at the repo root that coordinates multi-agent task batches. It reads/writes `decisions.md` (structured log of orchestrator sessions and agent findings) and `task-queue.md` (active/pending/completed task tracking). These two files are orchestrator output -- do not hand-edit them. Run via `node orchestrator.mjs` when coordinating parallel agent work.

The root `agents/` and `skills/` directories contain scenario prompt templates (e.g., `build_blueprint_graph_from_schema.md`, `create_map_and_place_actors.md`) used by the orchestrator and PromptBrush to generate structured build specs. These are distinct from the research agents in `.claude/agents/`.

## Active Workstreams
Multiple agents may work on this repo concurrently. Each workstream has its own spec and plan docs.

| Workstream | Location | Status | Spec |
|---|---|---|---|
| Blueprint Graph Builder | `ue4-plugin/BlueprintGraphBuilder/` | 11 passes complete | `docs/superpowers/specs/2026-03-17-blueprint-graph-builder-design.md` |
| Widget Blueprint Builder | `ue4-plugin/BlueprintGraphBuilder/Private/WidgetBuilder/` | Design complete, Pass 1 planned | `docs/superpowers/specs/2026-03-18-widget-blueprint-builder-design.md` |
| ShaderWeave Bridge | `unreal-plugin/Content/Python/mcp_bridge/shaderweave/` | Design complete | `docs/superpowers/specs/2026-03-18-shaderweave-bridge-mvp-design.md` |
| Behavior Tree Builder | `ue4-plugin/BlueprintGraphBuilder/Private/BehaviorTreeBuilder/` | Complete (26 node types) | `docs/superpowers/specs/2026-03-19-behavior-tree-builder-design.md` |
| Animation Blueprint Builder | `ue4-plugin/BlueprintGraphBuilder/Private/AnimBlueprintBuilder/` | Complete (v1) | `docs/superpowers/specs/2026-03-19-anim-blueprint-builder-design.md` |
| PromptBrush | External plugin + `mcp-server/src/tools/promptbrush.ts` | Active | `README_PROMPTBRUSH.md` |

ShaderWeave is a separate product that shares the UE_Bridge listener. It uses `/shaderweave/v1/*` URL paths, not the `POST /` command router. Do not mix ShaderWeave handlers into `handlers/` or ShaderWeave routes into `router.py`. Note: `listener.py` requires minimal path-routing changes for ShaderWeave (see ShaderWeave spec for details).

## Agents and Skills (`.claude/`)

### Research Agents (`.claude/agents/`)
Dispatch these for codebase questions instead of guessing. They search actual files and return answers with paths and line numbers.

| Agent | Purpose |
|---|---|
| `project-researcher` | Generalist -- searches codebase, specs, plans, code patterns |
| `ue4-cpp-expert` | UE4.27 C++ APIs, class hierarchies, plugin patterns, node graph internals |
| `bridge-architecture` | Cross-layer data flow (TS -> Python -> C++), HTTP protocol, tool registration |
| `spec-and-plan-reader` | Design specs, implementation plans, workstream status, JSON schemas |
| `orchestrator` | Coordinates multi-agent work, delegates to specialists, verifies builds/tests |
| `documentation` | Reads and summarizes docs, READMEs, and inline comments |
| `integration-test` | Runs and interprets integration tests against a live UE4 instance |
| `mcp-server` | MCP server internals: tool registration, Zod schemas, transport layer |
| `unreal-python` | Python handler patterns, UE4 Python API, threading, routing |
| `validation-safety` | Reviews code for UE4.27 API safety, forbidden UE5 tokens, transaction correctness |

### Skills (`.claude/skills/`)

| Skill | Purpose |
|---|---|
| `deep-research` | Routing hints for dispatching questions to the right research agent |
| `project-context` | Quick-reference card: architecture, file ownership, workstreams, C++ plugin pattern |
| `bridge-http-protocol` | Request/response contract between TS MCP server and Python listener |
| `mcp-tool-pattern` | Step-by-step template for adding a new MCP tool |
| `ue4-transaction-system` | Undo/redo transaction scope rules |
| `unreal-python-api` | UE4.27 Python API reference (includes Context7 live lookup with 57K+ snippets) |

## File Ownership
- `mcp-server/` is TypeScript only
- `unreal-plugin/` is Python only
- `ue4-plugin/` is C++ only (UE4 plugin, compiled by Unreal Build Tool, not npm)
- `docs/` is Markdown only
- These boundaries are hard. No cross-contamination.
