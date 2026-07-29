# AGENTS.md

Canonical instructions for every AI coding agent working in this repository:
Claude Code, OpenAI Codex, Google Gemini, and any other MCP client.

`CLAUDE.md` and `GEMINI.md` are thin pointers to this file. Edit this one.
Do not fork this content per client. A previous copy of this file was produced by
find-and-replacing "Claude" with "Codex" throughout `CLAUDE.md`, which produced
broken paths and instructions that described a codebase that did not exist.

## What this project is

A local bridge that lets an AI client drive the Unreal Engine 4.27 editor through
the Model Context Protocol. Three layers:

```
MCP client  --stdio-->  MCP server (TypeScript)  --HTTP POST :8080-->  Python listener (in UE4)  -->  unreal module
                                                                                                 -->  C++ plugin modules via Python bindings
```

The server also ships two tools that never touch the editor at all
(`engine_source_search`, `engine_source_read`); those read the installed engine
source from disk and work with Unreal closed.

**Engine target is UE4.27 only.** If an API exists in UE5 but is not confirmed in
4.27, do not use it. See "UE4.27 API safety" below.

## Repository layout

This is the bridge repo, and it is bridge-only. No game Content, Source, or
Config lives here.

| Path | Owns | Language |
|---|---|---|
| `mcp-server/src/` | MCP server: tool definitions, transport, HTTP client | TypeScript only |
| `mcp-server/incubator/` | Tools with no listener support yet. Not compiled, not registered. | TypeScript |
| `Plugins/MCPBridge/Content/Python/` | The listener that runs inside UE4 | Python only |
| `Plugins/MCPBridge/Source/` | Editor C++ modules, built by UBT (not npm) | C++ only |
| `docs/` | Documentation, specs, playbooks | Markdown only |
| `clients/` | Ready-to-paste MCP configs for Codex and Gemini | TOML / JSON |
| `Scripts/` | Repo automation | Node / PowerShell / Python |

These boundaries are hard. `Plugins/MCPBridge/` is the single source of truth for
everything that runs inside UE4; there are no other copies.

## Build, test, verify

```bash
npm install          # workspace root
npm run build        # mcp-server/src -> mcp-server/dist
npm test             # 15 unit suites, mock listener, no UE4 needed
npm run smoke        # drive the built server over stdio like a real client
npm run verify       # build + test + smoke, one command
```

`npm run verify` is the gate. Run it before claiming any change works.

Other useful entry points:

```bash
npm run inspect          # MCP Inspector web UI against the built server
npm run inspect:list     # Inspector CLI: dump tools/list
npm run smoke:editor     # smoke test, but FAIL (not SKIP) if the editor is down
npm run test:integration # hits a live UE4 listener
npx tsx mcp-server/tests/actor-tools.test.ts   # single suite
```

There is no linter and no external test runner. Tests are plain TypeScript run
through `tsx` with a custom assert helper. Unit tests use a mock HTTP server
(`tests/mock-server.ts`) that simulates the listener.

`npm test` chains its suites with `&&`, so a failure stops everything after it.

### Prerequisites

- UE4.27 with the Python Editor Script Plugin enabled
- Node.js 18+
- `UE_ENGINE_ROOT` pointing at the engine root (the directory containing
  `Engine/Source`). Currently `D:/UE/UE_4.27`. Without it the `engine_source_*`
  tools cannot find the engine: their fallback reads `EngineAssociation` from a
  `.uproject`, and a bridge-only clone has none.

## Client setup

The server is one binary; each client is configured differently.

| Client | Config file | Provided template |
|---|---|---|
| Claude Code | `.mcp.json` in the repo root | already committed |
| OpenAI Codex | `~/.codex/config.toml` | `clients/codex-config.toml` |
| Google Gemini | `~/.gemini/settings.json` or `.gemini/settings.json` | `clients/gemini-settings.json` |

Codex and Gemini do not read `.mcp.json`. Their templates use absolute paths
because they launch the server from their own working directory, and both set
`UE_ENGINE_ROOT`.

MCP servers connect at client startup. After `npm run build`, restart the client
or the new tools will not appear.

## MCP server internals (`mcp-server/src/`)

- `index.ts` - registers every tool, starts the stdio transport, warns at startup
  about any registered tool missing annotations
- `unreal-client.ts` - the only code that makes HTTP calls to the listener.
  Configurable host/port, optional auth token, 60s default timeout. Connection
  errors and timeouts resolve with `{success: false}` rather than rejecting.
- `types.ts` - the `ToolDefinition` interface (name, description, inputSchema,
  optional annotations, handler)
- `annotations.ts` - central read-only / mutating / destructive classification for
  all 146 tools. Reviewable in one file on purpose.
- `history.ts` - undo/redo/checkpoint tracking
- `validation.ts` - shared validation helpers
- `tools/` - 21 modules, each exporting a `create*Tools(client)` factory that
  returns `ToolDefinition[]`

The 21 tool modules: `actors`, `animation`, `blueprint-graph`, `blueprints`,
`cloth`, `content`, `cpp`, `effects`, `engine-source`, `gamedev`, `gameplay`,
`intelligence`, `level`, `materials`, `operations`, `pie-agent`, `project`,
`promptbrush`, `system`, `titles`, `viewport`.

`engine-source` is the only module whose factory takes no client: it is
server-local and reads the engine from disk.

## Python listener (`Plugins/MCPBridge/Content/Python/mcp_bridge/`)

- `listener.py` - HTTP server on a background thread; queues commands to the game
  thread via `register_slate_post_tick_callback`
- `router.py` - `COMMAND_ROUTES` maps command strings to handler functions
- `handlers/` - 22 handler modules mirroring the tool groups
- `utils/` - serialization, UE4 transaction wrappers, validation

Auto-started by `Plugins/MCPBridge/Content/Python/startup.py` when UE4 loads.

### Threading constraint

UE4's Python runs on the game thread; the HTTP server runs on a background
thread. Every `unreal.*` call must be marshaled to the game thread through
`register_slate_post_tick_callback`. Never call `unreal.*` from the HTTP handler
directly.

A consequence worth knowing when debugging: if the editor's game thread is busy
or blocked, the listener accepts the TCP connection and then never replies, so
requests look like hangs rather than refusals.

### HTTP protocol

```json
POST http://localhost:8080/
{"command": "actor_spawn", "params": {"type": "StaticMeshActor", "name": "MyActor"}}
```

The listener always answers with:

```json
{"success": true, "data": {}, "error": null}
```

ShaderWeave shares this listener but uses its own `/shaderweave/v1/*` namespace,
not the `POST /` command router. Do not mix its handlers into `handlers/` or its
routes into `router.py`.

## C++ plugin modules (`Plugins/MCPBridge/Source/`)

Compiled by Unreal Build Tool inside a UE4 project, not by `npm run build`.

| Module | Purpose |
|---|---|
| `MCPBridgeGraphBuilder` | Blueprint / Widget / Behavior Tree / Anim Blueprint builders from JSON, plus AnimPose, CanonFont, FolderVisibility, GarmentMesh libraries |
| `MCPBridgePIEAgent` | Runtime play-test agent: move, look, press, observe, record, replay |
| `MCPBridgeClothOptimizer` | NvCloth inspection and tuning, plus its editor panel |
| `MCPBridgeEditorPanel` | The in-editor bridge status panel |

Builder subsystems inside `MCPBridgeGraphBuilder`:

- **Blueprint Graph Builder** (11 passes complete) - `UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON`
- **Behavior Tree Builder** (complete, 26 node types) - `UBehaviorTreeBuilderLibrary::BuildBehaviorTreeFromJSON`
- **Animation Blueprint Builder** (v1 complete) - `UAnimBlueprintBuilderLibrary::BuildAnimBlueprintFromJSON`
- **Widget Blueprint Builder** (design complete, implementation not started) - `UWidgetBlueprintBuilderLibrary`

Specs live in `docs/superpowers/specs/`.

## Adding a new tool

1. Prototype through `python_proxy` first. It is the escape hatch; every new tool
   should be proven there before getting a dedicated handler.
2. Verify any `unreal` Python API you need against Context7
   (`/radial-hks/unreal-python-stubhub`) rather than guessing.
3. Add the Python handler in `Plugins/MCPBridge/Content/Python/mcp_bridge/handlers/`.
   Wrap editor mutations in the `@transactional` decorator from
   `utils/transactions.py`.
4. Register the command in `router.py`'s `COMMAND_ROUTES`.
5. Add the TypeScript definition in the matching `mcp-server/src/tools/` module.
6. If it modifies editor state, add it to `modifyingCommands` in `index.ts`.
7. Classify it in `mcp-server/src/annotations.ts`. The server warns at startup
   about anything missing, and the smoke test fails on it.
8. Run `npm run verify`.

`tests/registry-consistency.test.ts` enforces steps 3-5: it fails if the server
advertises a command with no route in `router.py`, or if a route exists that
nothing calls and that is not on the internal-only allowlist. Do not work around
it by adding to the allowlist; write the missing half.

If you have written only the TypeScript half, put the file in
`mcp-server/incubator/` rather than registering it. See that directory's README.

## API lookup: three systems, use the right one

| Need | Use |
|---|---|
| Reflected C++ (UCLASS/USTRUCT/UENUM members, signatures, `#include` paths, deprecation) | `unreal-api` MCP: `search_unreal_api`, `get_function_signature`, `get_include_path`, `get_class_reference` |
| Non-reflected C++ (Slate widgets, `FRunnable`, `FEditorFileUtils`, macros) | `engine_source_search` / `engine_source_read` in this server |
| Python `unreal` module | Context7 `/radial-hks/unreal-python-stubhub` |

Before writing a UE C++ call you have not verified in the current conversation,
look it up. When `unreal-api` reports "not found", fall back to
`engine_source_search` with a `module` filter to keep the scan fast.

## Architecture playbooks (`docs/playbooks/`)

Before any structural engine or blueprint generation task, read
`docs/playbooks/` for an existing recipe and follow it instead of re-deriving the
solution. Playbooks are verified specs of solved UE4.27 systems: design intent,
dependencies, graph logic, replication steps, and engine gotchas. When you build
or materially change a major system, writing or updating its playbook is part of
finishing the work. Template: `docs/playbooks/_TEMPLATE.md`.

The server ships this instruction to any project it connects to, via
`instructions` in `mcp-server/src/index.ts`.

## Architecture rules

- The MCP server never imports or references Unreal modules. It only sends HTTP.
- The Python listener never imports MCP SDK modules. It only receives HTTP.
- Every tool that modifies editor state is wrapped in a UE4 transaction.
- Every actor manipulation tool supports the `validate` parameter.
- Viewport operations (camera moves, mode switches, render modes) are NOT
  transactable. Do not wrap them in transactions.

## UE4.27 API safety: forbidden UE5 patterns

Scan for these before compiling. If found, replace with the 4.27 equivalent.

| UE5 (forbidden) | UE4.27 (use instead) | System |
|---|---|---|
| `EnhancedInputComponent` | `InputComponent` | Input |
| `EnhancedInputSubsystem` | `BindAxis` / `BindAction` | Input |
| `UE::Tasks`, `Tasks::Launch` | `FAsyncTask` / `FTimerManager` / `SetTimer` | Async |
| `MassAI` | `BehaviorTree` + `AIController` | AI |
| `SmartObjects` | manual triggers / overlap volumes | AI |
| `StateTree` | `BehaviorTree` | AI |
| `AnimNext` | `UAnimInstance` / `Montage_Play` | Animation |
| `LevelEditorSubsystem` | `GEditor` direct access | Editor |
| `EditorUtilitySubsystem` | `FKismetEditorUtilities` | Editor |
| `EditorPlaySessionSubsystem` | `GEditor->RequestPlaySession` | Play |

**Camera shakes:** this 4.27.2 build uses the UE5-transitional API,
`UCameraShakeBase` (`Camera/CameraShakeBase.h`) with `StartCameraShake()` on
`APlayerCameraManager`. The older `UCameraShake` / `PlayCameraShake` names do not
exist here.

## Behavior Tree workflow

UE4.27 protects `RootNode`, `BlackboardAsset`, root decorators, and decorator ops
from Python. To read, inspect, duplicate, or compare Behavior Trees, use the
editor C++ bridge, not raw Python reflection:

- `USFBehaviorTreeReplicationLibrary` from the `Sinfeld_DemoEditor` module
- Select the tree in the Content Browser, then
  `unreal.SFBehaviorTreeReplicationLibrary.get_first_selected_behavior_tree()`
- Export with `export_behavior_tree_to_json()`
- For replication, start from `duplicate_behavior_tree_asset()`

Never hand-edit `.uasset` files. Reference: `docs/BEHAVIOR_TREE_REPLICATION.md`.

## Visual feedback loop

After any spatial operation (`actor_spawn`, `actor_modify`, `actor_duplicate`,
`batch_spawn`, `actor_snap_to_socket`), call `viewport_focus` on the affected
actor then `viewport_screenshot`. For multi-actor operations use `viewport_fit`
then `viewport_screenshot`. This is default behavior, not an optional extra.

## Testing etiquette with a live editor

When an authoring job is done, stop after lightweight editor-side checks and let
the user test in Unreal. Do not start PIE on your own.

Editor-side checks that are fine unprompted: `level_actors`, `asset_info`,
`blueprint_compile`, `placement_validate`, `viewport_fit`, `viewport_screenshot`.

Requires the user to ask first: `pie_start`, `gameplay_pie_start`, runtime
telemetry, acceptance tests, and every `pie_agent_*` tool.

## Trigger volume placement

1. Never place a trigger volume on top of a PlayerStart. `OnBeginOverlap` fires
   only on an outside-to-inside transition; a player who spawns already inside
   never fires it.
2. Keep at least 1.5x the volume's extent away from any PlayerStart.
3. Query the PlayerStart location and compare against the planned position and
   extent before spawning. Warn or refuse on overlap.
4. Ask before starting PIE to test it.

## Code standards

- TypeScript: strict mode, explicit types, no `any`
- Python: type hints on all signatures, docstrings on all handlers
- Every handler returns `{success: bool, data: any, error?: string}`
- No em dashes in comments or documentation
- No filler language (delve, explore, leverage, robust, utilize)
- Write documentation for a programmer, not for a VP

## Safety

- Before a destructive change, make a source control checkpoint.
- Destructive means: deleting assets or source files, renaming public classes,
  changing serialization formats, replacing project config, modifying engine
  source, removing plugins, migrating large content groups, cleaning build
  directories outside the active project.
- Ask before anything that can permanently lose data.
- Do not commit, push, merge, or reset unless asked.
- Do not report success before validation finishes. If a build or test fails, say
  so and show the output.

**The git remote is public and is the bridge only.** Never push a local `main`
whose history predates the bridge/game boundary; game source is in that history
even where the paths are untracked at HEAD. Branch from `origin/main` for bridge
work. `.githooks/pre-push` blocks such a push when enabled with
`git config core.hooksPath .githooks`.
