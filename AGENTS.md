# AGENTS.md

Canonical instructions for every AI coding agent working in this repository:
Claude Code, OpenAI Codex, Google Gemini, and any other MCP client.

`CLAUDE.md` and `GEMINI.md` are thin pointers to this file. Edit this one.
Do not fork this content per client. A previous copy of this file was produced by
find-and-replacing "Claude" with "Codex" throughout `CLAUDE.md`, which produced
broken paths and instructions that described a codebase that did not exist.

## What this project is

A local bridge that lets an AI client drive the Unreal Engine 4.27 editor through the Model Context Protocol:

```
MCP client --stdio--> MCP server (TypeScript) --authenticated named pipe--> PuerTS Node.js runtime (inside UE4) --> native MCPBridgePuerTS module --> UE4 game thread
```

## Strict Unreal Engine tooling protocol

1. Use only `puerts_*` tools for Unreal Editor operations.
2. Never use HTTP, REST, local web servers, Python sockets, shell commands, or workaround scripts to communicate with UE4.
3. If a native tool fails, report its exact error. Do not attempt a fallback transport.
4. HTTP/Python code is migration-only, disabled by default, and may be enabled only by a human setting `MCP_ENABLE_LEGACY_HTTP=1` before both UE4 and the MCP server start.
5. `engine_source_*` tools are allowed because they read local source and do not communicate with the editor.
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
| `mcp-server/src/` | MCP server: native PuerTS tool definitions and named-pipe client; legacy HTTP code is opt-in | TypeScript only |
| `mcp-server/incubator/` | Tools with no native support yet. Not compiled, not registered. | TypeScript |
| `Plugins/MCPBridge/Content/Python/` | Legacy HTTP listener, disabled unless explicitly opted in | Python only |
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

`npm run verify` is the gate for the TypeScript side. Run it before claiming any
change works.

Anything that touches the editor has a second gate, because `npm run verify`
knows nothing about which plugin a test project is running:

```bash
npm run install:check -- --project "D:\Unreal Projects\MyGame"
npm run install:sync  -- --project "D:\Unreal Projects\MyGame"
```

The plugin is installed into a target project by copying, and a copy goes stale
silently. It did: two projects carried an MCPBridge install, one was a day
behind, and a live run against it would have passed while proving nothing.
`install:check` compares the target's plugin against this checkout by content
and refuses on any difference; `install:sync` copies the changed declared files,
builds the target editor, and regenerates the manifest. `install:check` never
writes to the target. Every live acceptance script calls it before connecting,
so this is a gate you notice only when it saves you. Details and the target
list: `docs/MCP_BRIDGE_INSTALLER.md`.

Three more acceptances need no editor, no UBT and no target project. They build
their own fixtures under the temp directory and are the ones CI can run:

```bash
npm run test:security      # bad token, path escape, shell execution, property allowlist
npm run test:fresh-install # a throwaway project, the real installer, everything short of the compiler
npm run test:package       # what Scripts/package-mcp-bridge.ps1 actually produces
npm run test:editor-free   # all three
```

`.github/workflows/ci.yml` runs those plus `npm run verify`, and lists at the
bottom exactly which acceptances cannot run in CI because they need a live
UE4.27 editor. That split is worth reading before adding a test.

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
- `Plugins/Puerts`, the pinned vendored bundle. It is gitignored, so a fresh
  **git worktree does not have it** and `npm run build` warns, stages an
  incomplete `Content/JavaScript`, and leaves `install:check` reporting 22 extra
  files in the target. Link it from the main checkout once per worktree:

  ```powershell
  New-Item -ItemType Junction -Path <worktree>\Plugins\Puerts -Target <main checkout>\Plugins\Puerts
  ```

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

### `ue427`: one command for all three clients

The templates above are still the manual path. `Scripts/ue427.py` automates
the same result and adds diagnostics:

```bash
python Scripts/ue427.py install --agent all --scope user   # or: npm run ue427 -- install
python Scripts/ue427.py doctor      # build, skill install, MCP config, project version, live session
python Scripts/ue427.py repair      # fix what is safe to fix, then re-run doctor
python Scripts/ue427.py update      # git pull, rebuild, reinstall
python Scripts/ue427.py verify      # prove each client actually discovers the skill
python Scripts/ue427.py start claude|codex|gemini|antigravity
```

The repository root also ships `ue427.cmd` (Windows) and `ue427` (POSIX)
as shims for the same CLI.

`install` links the canonical skill at `skills/unreal-engine-4-27` into
`~/.claude/skills` and the shared `~/.agents/skills` that Codex and Gemini both
read, and registers **the existing `unreal-bridge` server** with each client
through that client's own configuration mechanism. Links, not copies, so
editing the skill in this repository reaches every agent with no reinstall.

It configures no second server and no HTTP transport. Editor traffic stays on
the authenticated named pipe, per the strict protocol above.

`doctor` catches the failure that is otherwise silent: `MCP_UNREAL_PROJECT_ROOT`
pointing at a project that has no editor running, which surfaces to tools only
as a `session_missing` refusal. It compares the configured project against the
session manifests actually advertised on disk and names both paths. It also
delegates repository health to `Scripts/bridge-doctor.mjs` rather than
duplicating it.

Acceptance: `npm run test:ue427-skill` (editor-free, included in
`npm run test:editor-free`).

## The `unreal-engine-4-27` skill (`skills/unreal-engine-4-27/`)

The canonical, client-agnostic interface guide for this bridge. One source,
installed to every agent. It documents the required session workflow, the real
tool catalog with safety classifications, batching over round trips, PIE rules,
failure codes, and the rule that a missing capability is a bridge gap to fix
rather than a reason to reach for another transport.

`references/tool-catalog.md` is generated from `mcp-server/src/annotations.ts`
by `python Scripts/ue427.py catalog`. The acceptance suite fails when it drifts,
so adding a tool means regenerating it.

Do not confuse this with the scenario prompt templates that are flat `.md` files
directly inside `skills/`; those feed the orchestrator and PromptBrush.

## MCP server internals (`mcp-server/src/`)

- `index.ts` - registers the native catalog by default, starts stdio, and warns at startup
  about any registered tool missing annotations
- `puerts-client.ts` - authenticated Windows named-pipe client for the native editor runtime.
- `unreal-client.ts` - legacy HTTP client, reachable only when `MCP_ENABLE_LEGACY_HTTP=1`.
  Configurable host/port, optional auth token, 60s default timeout. Connection
  errors and timeouts resolve with `{success: false}` rather than rejecting.
- `types.ts` - the `ToolDefinition` interface (name, description, inputSchema,
  optional annotations, handler)
- `annotations.ts` - central read-only / mutating / destructive classification for
  every registered tool. Reviewable in one file on purpose.
- `history.ts` - undo/redo/checkpoint tracking
- `validation.ts` - shared validation helpers
- `tools/` - 22 modules, each exporting a `create*Tools(client)` factory that
  returns `ToolDefinition[]`

The 22 tool modules: `actors`, `animation`, `blueprint-graph`, `blueprints`,
`cloth`, `content`, `cpp`, `effects`, `engine-source`, `gamedev`, `gameplay`,
`intelligence`, `level`, `materials`, `operations`, `optimization`, `pie-agent`,
`project`, `promptbrush`, `system`, `titles`, `viewport`.

`engine-source` is the only module whose factory takes no client: it is
server-local and reads the engine from disk.

## Legacy Python listener (`Plugins/MCPBridge/Content/Python/mcp_bridge/`)

This listener is disabled by default. It exists for migration testing only and is never an automatic fallback.

- `listener.py` - HTTP server on a background thread; queues commands to the game
  thread via `register_slate_post_tick_callback`
- `router.py` - `COMMAND_ROUTES` maps command strings to handler functions
- `handlers/` - 22 handler modules mirroring the tool groups
- `utils/` - serialization, UE4 transaction wrappers, validation

`startup.py` loads with UE4, but starts this listener only when `MCP_ENABLE_LEGACY_HTTP=1`.

### Threading constraint

UE4's Python runs on the game thread; the HTTP server runs on a background
thread. Every `unreal.*` call must be marshaled to the game thread through
`register_slate_post_tick_callback`. Never call `unreal.*` from the HTTP handler
directly.

A consequence worth knowing when debugging: if the editor's game thread is busy
or blocked, the listener accepts the TCP connection and then never replies, so
requests look like hangs rather than refusals.

### Play In Editor

UE4.27 editor scripting does not raise during PIE. It logs "The Editor is
currently in a play mode" and returns nothing, so an unguarded handler answers
success with zero actors while a full level is being played. An empty success is
worse than an error, because the caller cannot tell it from an empty level.

`route_command` therefore refuses editor-only commands while PIE runs, with a
message naming the command and pointing at `gameplay_pie_stop` or the
`pie_agent_*` tools. The policy lives in `utils/editor_state.py` as an
**allowlist** of PIE-safe commands, so a tool added later is guarded by default
rather than needing someone to remember. If a new command genuinely works during
play, add it to `PIE_SAFE_COMMANDS` deliberately.

### Legacy HTTP protocol

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

## Project-local extensions

The bridge is a general UE4.27 control surface. Tools that only make sense for one
game do not belong in the shipped catalog. They go in an extension.

An extension is a directory with an `extension.json` manifest declaring tools that
forward to listener commands. There is no TypeScript to compile in the host
project: pass-through tools are the common shape, so a manifest covers them.

Discovery: `UNREAL_BRIDGE_EXTENSIONS` (a `;` or `,` delimited list of directories
or manifest files) wins; otherwise `<cwd>/BridgeExtensions/<name>/extension.json`.

Three rules make this safe:

- **Namespaced.** Every tool is prefixed with the extension name, so
  `sinfeld_locomotion_capture_start` is visibly not a bridge tool and cannot
  shadow one. A collision with a core tool skips the whole extension.
- **Disabled by default.** `enabled` defaults to `false`. An extension can be
  written and reviewed long before its listener handlers exist; registering it in
  that state would advertise tools that fail on every call. Set `enabled: true`
  only once the commands are real, and record a `disabledReason` until then.
- **Never fatal.** A malformed manifest is reported on stderr and skipped. A
  broken project-local file must not stop the bridge from starting.

Validate a manifest without starting the server:

```bash
npx tsx mcp-server/tests/extension-manifest-check.ts <extension dir>
```

It prints the tool names that would register, their annotations, and the listener
commands they need. **The core `registry-consistency` test does not cover
extensions** — it only scans the bridge's own `src/`. An extension's commands are
the host project's responsibility.

The Sinfeld extension lives at
`SF_Repository/Sinfeld_240301/BridgeExtensions/sinfeld/`. It declares 25 locomotion
and fixed-camera tools and is **disabled**: `locomotion_debug` and
`fixed_camera_locomotion_debug` do not exist in the listener yet.

## Adding a new tool

1. Add the MCP schema to `mcp-server/src/tools/puerts.ts`.
2. Add the TypeScript execution function and registry entry in `puerts-runtime/src/registry.ts`.
3. Add the minimum UE4.27 C++ method to `MCPBridgePuerTS` when reflection alone is insufficient.
4. Add the native tool name to the C++ allowlist and keep permissions narrow.
5. Classify the prefixed MCP tool in `mcp-server/src/annotations.ts`.
6. Update the PuerTS declarations stored in `puerts-runtime/typing/`.
7. Run `npm run verify`, compile the isolated UE4.27 test project, then run `npm run smoke:editor`.

Do not prototype new editor operations through `python_proxy`. Do not add HTTP routes for native tools. The old Python route workflow is migration-only and must remain behind the explicit legacy opt-in.

## API lookup: three systems, use the right one

| Need | Use |
|---|---|
| Reflected C++ (UCLASS/USTRUCT/UENUM members, signatures, `#include` paths, deprecation) | `unreal-api` MCP: `search_unreal_api`, `get_function_signature`, `get_include_path`, `get_class_reference` |
| Non-reflected C++ (Slate widgets, `FRunnable`, `FEditorFileUtils`, macros) | `engine_source_search` / `engine_source_read` in this server |
| Python `unreal` module | Context7 `/radial-hks/unreal-python-stubhub` |

Before writing a UE C++ call you have not verified in the current conversation,
look it up. When `unreal-api` reports "not found", fall back to
`engine_source_search` with a `module` filter to keep the scan fast.

## Product goal: prompt-to-working-Unreal-feature speed

Optimize for the fewest editor round trips, not the fewest lines of code.

Prefer high-level desired-state operations backed by reusable native primitives.

A successful command should be transactional, convergent, independently
verifiable, and suitable for composition into autonomous workflows.

Do not force the agent to perform hundreds of low-level MCP calls when one batch
or upsert operation can express the same intent.

Do not use correctness work as an excuse for slow interfaces. Build correctness
into fast primitives through validation, rollback, inspection, and automatic
verification.

When a workflow is repeatedly assembled from the same low-level calls, promote
it into a reusable PuerTS workflow or native batch command.

The target experience is that Claude or Codex can create, compile, run, inspect,
repair, and verify a substantial UE4.27 feature from one prompt with minimal
manual editor interaction.

### What this means in practice

The fast path is: plan in TypeScript, send one or a few native batch commands,
run them in a transaction, read the result back with an independent inspector,
verify by compile or PIE, return a structured result. The slow path this
replaces is create-node, inspect, connect-pin, inspect, set-property, inspect.

Every major builder needs an inspector with the same canonical data shape, so
desired state can be compared against actual state without a human opening the
editor. `blueprint_build` / `graph_inspect` and `behavior_tree_build` /
`behavior_tree_inspect` are the pattern; `widget_build` has no inspector yet.

Failures must name the exact location and be recoverable in the same breath: an
error code, the asset, graph, node and pin, the closest matching names, and the
rollback result. Not a modal dialog, not dirty residue, not "operation failed".

Use native C++ for asset creation and mutation, graph editing, transactions and
rollback, compilation, PIE control, viewport capture, package and
source-control state. Use PuerTS for planning, orchestration, reconciliation,
retry logic and project recipes.

## Capability-first development

A reference feature is an acceptance test for the bridge, not the product. It
may expose a platform gap; the gap must be fixed before the feature expands.

For every feature task:

1. Identify the reusable bridge capability the feature requires.
2. Implement and test that capability separately.
3. Prove it on a small feature fixture.
4. Return to the full feature only after the capability passes.
5. Never solve a bridge limitation with a feature-specific generator or
   workaround. A schema limit is something to remove, not bend a feature
   around.

One capability per session. A session that lands one capability with evidence
beats a session that half-lands three.

Debugging discipline: list at most three likely causes, add one diagnostic
that distinguishes them, run one test, remove disproven causes, make the
smallest fix. Do not stack hypotheses before measuring.

Separate gameplay assets from test-driver assets. Validation sequencing never
lives inside a production graph.

Any project-state query discrepancy (wrong actor count, empty result that
should not be) becomes a tracked Unknown in docs/CAPABILITY_FINDINGS.md
immediately. A tool that controls Unreal must trust its state queries.

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

- The MCP server never imports Unreal modules. Editor operations use the authenticated named-pipe client.
- The client addresses one editor, by session, and never falls back. Each editor
  publishes `Saved/MCPPuerTSBridge/session.json` (session id, nonce, PID, process
  creation time, project path, pipe name, heartbeat, shutdown state), written by
  moving a staged file into place. A request carries the nonce and the editor
  refuses a mismatch; every response carries the editor's identity and the client
  refuses a reply that came from somewhere else. No advertised session means a
  structured refusal (`session_error_code`), never a guessed pipe name: with two
  editors open, guessing means authoring assets in the wrong project and
  reporting success. `MCP_UNREAL_PROJECT_ROOT` selects the target;
  `MCP_PUERTS_SESSION_ID` pins one exactly.
- PuerTS executes approved TypeScript in UE4 and delegates privileged operations to the native C++ safety boundary.
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
