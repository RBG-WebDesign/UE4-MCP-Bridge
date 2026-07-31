# UE4.27 PuerTS MCP Bridge Technical Handoff

Date: 2026-07-31  
Repository: `D:\Unreal Projects\UE4_Bridge`  
Test project: `D:\Unreal Projects\MASTER_PROJECT\SF_Repository\Sinfeld_240301\Tests\UE427PuerTSMCP`  
Scope: current working tree and the isolated UE4.27 test project. This report records the state handed over today. It does not claim every uncommitted change was authored today.

## Executive summary

The bridge is a working **internal prototype**, not a distributable reusable plugin yet. Its default editor control path was demonstrated in an isolated UE4.27 project:

```text
MCP client -> stdio -> TypeScript MCP server -> authenticated Windows named pipe
-> in-process PuerTS Node/V8 runtime -> MCPBridgePuerTS C++ service -> UE4 editor game thread
```

This path intentionally avoids HTTP. The prior TypeScript MCP server, HTTP listener, Python editor scripts, and C++ editor modules remain as a migration lane. They register only when `MCP_ENABLE_LEGACY_HTTP=1`; there is no automatic HTTP fallback.

The first native milestone was demonstrated: an MCP client connected to UE4.27, listed actors, changed a reflected property, saved the level, returned structured JSON, and undid the transaction. The native lane also created and applied a specialized aurora sky material, saved the level, and captured a viewport image. `npm run verify` built both TypeScript workspaces, passed tests, and passed default smoke checks with two documented skips.

The work is not ready for arbitrary UE4.27 projects without care. The PuerTS bundle is ignored by Git and its recorded version is contradictory. Native APIs are small, serial, editor-focused, and have a partial reflection serializer. The installer does not yet produce a version-pinned release. Python editor dependencies remain in `MCPBridge.uplugin` even though the intended native default should not require them. The dashboard is only partially converted from legacy HTTP behavior.

## 1. Installation and setup

### Installed and configured components

| Item | Current state | Evidence | Notes |
|---|---|---|---|
| Unreal Engine | UE4.27 target. Test editor observed as `4.27.2-18319896+++UE4+Release-4.27` | test `.uproject`, editor observation | Exact source revision used for build is not tracked here. |
| PuerTS | Local bundle at `Plugins/Puerts` | `Plugins/Puerts/Puerts.uplugin` | Manifest says `1.0.5`; documentation and installer say `Unreal_v1.0.9`. See discrepancy below. |
| In-engine JS runtime | PuerTS Node.js backend, V8 through Node 16 libraries | `Plugins/Puerts/Source/JsEnv/JsEnv.Build.cs` | `UseNodejs=true`, `Node16=true`, `UseQuickjs=false`. |
| External Node | `v24.12.0`, `C:\nvm4w\nodejs\node.exe` on test machine | environment check | Builds and launches MCP server, not embedded PuerTS Node. |
| TypeScript runtime | `puerts-runtime`, version `0.1.0`, TypeScript `5.5.4` | `puerts-runtime/package.json` | Compiles into plugin Content. |
| MCP server | MCP SDK `^1.12.1`, zod `^3.24.2`, TypeScript `5.3.3`, tsx `^4.21.0` | `mcp-server/package.json` | Local stdio MCP server. |
| Native bridge | `MCPBridgePuerTS` editor module | `Plugins/MCPBridge/Source/MCPBridgePuerTS/` | New, compiled in isolated test project. |
| Legacy bridge | Python HTTP listener and existing C++ modules | `Plugins/MCPBridge/Content/Python/`, `Source/` | Migration-only when native default is used. |

### Locations and configuration

```text
D:\Unreal Projects\UE4_Bridge\
  Plugins\MCPBridge\                 Main UE4.27 editor plugin
  Plugins\Puerts\                    Side-loaded PuerTS bundle, ignored by Git
  puerts-runtime\                    TypeScript source for in-engine runtime
  mcp-server\                        Stdio server and named-pipe client
  Scripts\install-mcp-bridge.ps1     Target-project installer

D:\Unreal Projects\MASTER_PROJECT\SF_Repository\Sinfeld_240301\
  Tests\UE427PuerTSMCP\              Isolated UE4.27 test project
```

The test `.uproject` enables `Puerts` and `MCPBridge`. It also enables `PythonScriptPlugin` and `EditorScriptingUtilities`, while using the native pipe. This is not the desired clean native-only configuration. A disabled `MCPPuerTSBridge` entry appears obsolete and must be confirmed before removal.

`MCPBridge.uplugin` is version `0.5.0`, UE `4.27.0`, editor-only and Win64-only. It loads `MCPBridgeEditorPanel`, `MCPBridgeGraphBuilder`, `MCPBridgePIEAgent`, `MCPBridgeClothOptimizer`, and `MCPBridgePuerTS`. The new module has public dependencies `Core`, `CoreUObject`, `Engine`; private dependencies `AssetRegistry`, `Json`, `JsonUtilities`, `JsEnv`, `Projects`, `UnrealEd`.

No engine source, game source, `Target.cs`, or game `Build.cs` change was observed for the new native bridge. `MCPBridge.uplugin` still declares `PythonScriptPlugin`, `EditorScriptingUtilities`, and `Puerts` as dependencies. This conflicts with a truly optional legacy Python lane.

### Environment and machine requirements

```text
MCP_UNREAL_PROJECT_ROOT=<target project root>
MCP_PUERTS_PIPE=<optional named-pipe override>
MCP_PUERTS_TOKEN_PATH=<optional explicit token path>
MCP_PUERTS_TOKEN=<optional raw token, least preferred>
UE_ENGINE_ROOT=<engine root, only for engine-source tools>
```

The plugin writes a bearer token to `<Project>\Saved\MCPPuerTSBridge\token.txt`. `MCP_PUERTS_TOKEN_PATH` takes priority over the project-root default. The test project uses a manual pipe override:

```ini
[MCPPuerTSBridge]
PipeName=\\.\pipe\UE427PuerTSMCP_UE427PuerTSMCP_81d778e7_skyshader5
```

This suffix is a temporary test artifact intended to avoid stale local editor pipe state. It is not portable configuration.

### Reproduce on another computer

1. Install UE4.27 C++ support. The only verified compiler stack is VS2019 toolset `14.29.30159` and Windows 10 SDK `10.0.26100.0`.
2. Install Node.js 18+. Run `node --version`.
3. Clone `UE4_Bridge` and obtain the exact PuerTS bundle for `Plugins/Puerts`. Record upstream URL, Git revision, archive checksum, and manifest version before trusting it. Those facts are currently Unknown.
4. In the bridge root run `npm install`, then `npm run verify`.
5. Create a disposable UE4.27 C++ project. Do not begin in a production game project.
6. Run:

   ```powershell
   .\Scripts\install-mcp-bridge.ps1 -Project 'D:\Path\To\MyProject\MyProject.uproject'
   ```

   Do not use `-CleanManaged` without reviewing target plugin folders. It removes managed plugin directories.
7. Verify that both `Plugins\MCPBridge` and `Plugins\Puerts` were copied, a pipe was written under `[MCPPuerTSBridge]`, and the MCP client config was generated.
8. Build `<ProjectName>Editor Win64 Development`, accept the UE rebuild prompt, then start the editor.
9. Start the MCP client with `MCP_UNREAL_PROJECT_ROOT` set. It reads the token from the project Saved directory. Restart the client after config changes.
10. Call `puerts_diagnostic`, `puerts_find_actors`, `puerts_set_property`, `puerts_save`, and `puerts_undo`. Verify each JSON result and UE Output Log.
11. Use `MCP_ENABLE_LEGACY_HTTP=1` only when testing old HTTP/Python tools.

## 2. What PuerTS does

PuerTS embeds a JavaScript engine in Unreal. This bundle uses Node/V8. `FMCPPuerTSBridgeModule::StartupModule()` creates a rooted `UMCPPuerTSBridgeService`, starts `PUERTS_NAMESPACE::FJsEnv`, injects the service as `bridge`, and executes compiled JavaScript from `Plugins/MCPBridge/Content/JavaScript`.

TypeScript compiles to JavaScript. `bootstrap.ts` hosts a Windows named pipe, accepts one JSON request per connection, serializes requests, validates against the tool registry, calls the native service, and returns JSON.

| Access type | What it provides | Current use |
|---|---|---|
| Reflected Unreal API | Supported UCLASS/USTRUCT/UENUM, UFUNCTION, UPROPERTY and potentially delegates | actor lookup, approved property access, selected functions |
| Custom native wrapper | Deliberate `UCLASS`/`UFUNCTION` C++ API | transactions, editor operations, screenshot, physics, sky material |
| Non-reflected C++ | Not automatically exposed | remains C++ only, exposed by a narrow wrapper if needed |
| Blueprint/editor APIs | Available only if reflected or wrapped | existing graph builders are still legacy lane |

PuerTS does not expose all C++. Slate internals, private engine APIs, templates, macros and non-reflected editor functionality need C++ wrappers. The correct bridge pattern is a small validated wrapper, not arbitrary native execution.

### Lifecycle, GC, threading and reload

- The default script root is within the target project and is validated by C++.
- Shutdown releases `FJsEnv`, resets the service, and removes its root.
- The bridge service uses `AddToRoot`. Other UObjects are subject to Unreal GC and PuerTS proxy lifetime. No long-lived proxy or asynchronous GC stress test has passed. Use paths or `TWeakObjectPtr` across async boundaries.
- `JsEnv.Build.cs` declares `NOT_THREAD_SAFE`. The bootstrap serializes commands and the C++ service rejects concurrent work. Unreal object operations occur on the game thread.
- PuerTS contains hot-reload support, but this bridge has no verified hot-reload command or lifecycle. Editor restart/rebuild was used. Hot reload is unverified.
- Delegate subscriptions were not tested. Do not rely on them until add, callback, remove and GC behavior are covered.

### Appropriate split

Use TypeScript for structured orchestration, schema validation, approved reflection operations and project workflow composition. Use native C++ for transactions, editor/private APIs, heavy loops, asset graph construction, thread crossing, object lifetime, security checks and stable performance. Use Blueprints for designer-facing composition and event wiring. PuerTS is not a substitute for an unrestricted native editor API.

### Limits

- `puerts-runtime/typing/ue/ue.d.ts` is approximately 120,042 lines, but the exact generator command and source revision are Unknown. PuerTS has a `DeclarationGenerator` module; regenerate with a documented command and version stamp.
- The reflection serializer returned `{}` for some structs and arrays in live inspection, including material overrides and `RelativeScale3D`.
- The native catalog is allowlisted, not general reflection RPC.
- JavaScript filesystem/module guards are defense in depth, not an OS sandbox.
- Native pipe execution has only been tested on Win64.

## 3. Changes relative to original PuerTS

### Provenance discrepancy (RESOLVED 2026-07-31)

The bundle was compared file by file against a sparse clone of
`https://github.com/Tencent/puerts` at tag `Unreal_v1.0.9`, commit
`838ab762d83021c0407f13120f4004dcaf70cffe`, subtree `unreal/Puerts`,
CRLF-normalized. The bundle IS `Unreal_v1.0.9`:

- 945 of 947 upstream files byte-identical.
- One local modification: `Source/JsEnv/JsEnv.Build.cs` sets `UseNodejs = true`.
- Absent locally: `Content/JavaScript/react-umg/index.js`, `ThirdParty/Libnode_APL.xml`.
- 93 local additions, all `ThirdParty/nodejs_16/**` (the separate Node 16 dependency package).

The earlier `1.0.5` claim in `Puerts.uplugin` is upstream's own stale manifest;
the tag `Unreal_v1.0.9` ships that exact file.

The pin is now enforced: `Plugins/Puerts.lock.json` (tracked) holds SHA256
hashes of all 1038 bundle files. `Scripts/check-puerts-pin.mjs` verifies it in
`npm run verify` and the installer refuses a non-matching bundle. Regenerate
only deliberately with `--write`. See `docs/PUERTS.md` for the full record.

### Confirmed PuerTS changes

No direct PuerTS source edit is proven. The bridge adds code beside PuerTS. Observed bundle configuration, which is not proven to be modified today, uses Node 16, stages `libnode.dll`, sets `UseQuickjs=false`, and declares `NOT_THREAD_SAFE`.

### Native integration additions

| Path | Main type/function | Reason | State |
|---|---|---|---|
| `Source/MCPBridgePuerTS/MCPBridgePuerTS.Build.cs` | Module rules | Links PuerTS `JsEnv` and UE editor modules | Compiled |
| `Public/MCPPuerTSBridgeService.h` | `UMCPPuerTSBridgeService` | Defines allowed tools, config, transaction and auth boundary | Compiled |
| `Private/MCPPuerTSBridgeModule.cpp` | `StartupModule`, `ShutdownModule` | Owns `FJsEnv` lifecycle | Compiled and exercised |
| `Private/MCPPuerTSBridgeService.cpp` | `AcceptCommand`, `CompleteCommand` | JSON validation, allowlists, logs, transaction state | Compiled and core calls exercised |
| `Private/MCPPuerTSBridgePhysics.cpp` | physics handlers | Builds/observes limited physics test content | Compiled; final physics smoke not re-run |
| `Private/MCPPuerTSBridgeViewport.cpp` | screenshot helper | Native focus/capture, alpha normalization | Compiled and visually verified |
| `Private/MCPPuerTSBridgeMaterial.cpp` | `CreateAuroraSkyMaterialJson` | Specialized sky material proof | Compiled and visually verified, not generic |
| `puerts-runtime/src/bootstrap.ts` | pipe server | In-engine JSON IPC and command queue | Built and live exercised |
| `puerts-runtime/src/registry.ts` | 17 definitions | schemas, permission, timeout, function per tool | Built and unit-tested |
| `puerts-runtime/src/runtime.ts` | resolver/serializer | reflected values and object resolution | Partial serializer defect observed |
| `puerts-runtime/src/safety.ts` | Node guards | blocks shell, worker and REPL; restricts paths | Present, not formal sandbox |
| `puerts-runtime/types/puerts-bootstrap.d.ts` | manual types | bridge bootstrap declarations | Present |
| `puerts-runtime/typing/ue/ue.d.ts` | UE declaration snapshot | TypeScript API typing | generation provenance Unknown |
| `mcp-server/src/puerts-client.ts` | named-pipe client | existing MCP server adapter | Unit-tested and live exercised |
| `mcp-server/src/tools/puerts.ts` | `puerts_*` adapters | MCP schemas for native registry | Unit-tested |
| `mcp-server/tests/puerts-tools.test.ts` | test suite | tool, token and error contract | Passed |
| `Scripts/puerts-live-smoke.mjs` | live smoke | milestone and physics modes | milestone used; physics mode needs rerun |

The compiled JavaScript under `Plugins/MCPBridge/Content/JavaScript/` is untracked. It needs a clear policy: commit it, or package it deterministically during install/release.

### Existing bridge files modified

The working tree also changes `.gitignore`, `AGENTS.md`, `README.md`, workspace packages, installer, MCP configs, `MCPBridge.uplugin`, panel files, legacy Python listener/router/state/startup and selected actor, level, material and system handlers, plus actor/material TypeScript tools and tests. The detailed authorship of every line cannot be reconstructed from an uncommitted working tree. Review `git diff -- <path>` before commit.

Verified behavioral changes include native-default tool registration, explicit legacy HTTP opt-in, conditional legacy Python listener startup, installer copy/config support, partial native panel awareness, screenshot alpha fix, and a stock-sky workaround.

### Temporary workarounds

- The aurora tool is Custom-HLSL with three visible material expressions, not a general material node graph builder.
- It creates a separate static mesh sky actor because stock Blueprint sky component reconstruction overwrote direct assignment.
- The original sky is reduced to `0.000001` scale and hidden in game. This is test-only behavior.
- UE4.27 `UEditorEngine::AddActor` did not apply requested scale, so code applies transform after spawn.
- Screenshot alpha is forced to 255 after `ReadPixels`; this is local bridge behavior, not a PuerTS patch.

## 4. Unreal Engine MCP Bridge integration

### Architecture

```text
Default native route
AI client -> stdio JSON-RPC -> mcp-server/src/index.ts
-> mcp-server/src/puerts-client.ts -> named pipe + bearer token
-> PuerTS Node/V8 bootstrap + TypeScript registry
-> UMCPPuerTSBridgeService C++ allowlisted operation
-> UE4 editor game thread -> structured JSON result

Legacy migration route, explicit opt-in
AI client -> stdio JSON-RPC -> existing TypeScript factories/UnrealClient
-> HTTP POST 127.0.0.1:8080 -> Python listener
-> Slate post-tick callback -> Python unreal module / existing C++ libraries
```

Before the merge, UE4 Bridge was a TypeScript MCP server that used HTTP to a Python listener, which marshaled calls to UE4's game thread. PuerTS was added as a direct in-process JavaScript runtime. It has not replaced every legacy capability.

| Layer | Responsibility now | Status |
|---|---|---|
| MCP server | Public MCP schemas, client transport selection, local engine-source tools | Native default works |
| PuerTS runtime | pipe server, request sequencing, safety wrappers, TypeScript registry | 17 tools work as prototype |
| C++ service | safety, transactions, editor APIs, hard-to-reflect operations | limited native catalog works |
| Legacy Python | existing broad automation and Python proxy | opt-in only |
| Existing C++ builders | Blueprint graph, behavior tree, animation, widget design, PIE agent, cloth | not migrated to native catalog |
| Editor panel | human status/recovery | partially native-aware, still legacy overlap |

### Native commands

Current native registry: `diagnostic`, `find_assets`, `find_actors`, `read_property`, `set_property`, `call_function`, `spawn_actor`, `delete_actor`, `sky_shader_create`, `physics_build`, `physics_observe`, `viewport_screenshot`, `save`, `pie_start`, `pie_stop`, `get_logs`, `undo`. MCP exposes them as `puerts_*`.

Each request and response is JSON. Native responses normalize `success`, `message`, `changed_assets`, `changed_actors`, `warnings`, `errors`, `log_output`, and `transaction_id`.

### Legacy commands and integration gaps

The legacy catalog retains broad asset, material, Blueprint, graph-builder, behavior, animation, cloth, project and viewport capabilities not in the 17 native tools. It is not available in native-default mode. `Scripts/editor-acceptance.mjs` expects 187 mixed tools, uses legacy commands, and must be treated as legacy acceptance only.

Overlaps remain for save, actor work, logs, PIE and screenshot. No enforced command ownership table exists. The panel can show legacy-style disconnected state despite a native runtime. Legacy `python_proxy` has arbitrary Python execution scope and is outside native safety controls. The plugin descriptor still forces Python dependencies. Existing advanced material/graph tooling was not ported; the sky demo is not generic node editing.

The intended final architecture is one MCPBridge plugin containing the native service, runtime, command contract, logging and panel. Project-specific capability belongs in extension manifests. Legacy HTTP/Python should become a versioned optional compatibility plugin or be migrated behind the native contract, not remain a second default system.

## 5. Intended role in the full system

The intended reusable plugin should choose the smallest correct implementation layer.

| Need | Preferred layer | Reason |
|---|---|---|
| High-volume, editor-only or private API work | native C++ | transactions, performance, lifetime and API control |
| Existing reflected property/object operation | PuerTS plus native allowlist | structured orchestration with low ceremony |
| Existing project class/asset/system | inspect and reuse | prevents duplicate systems |
| Designer composition/event wiring | Blueprint | editable UE4 content |
| New reusable gameplay behavior | project C++ with Blueprint hooks | testable packaged behavior |
| Asset/material graph creation | dedicated builder | reflection alone is insufficient |
| Existing legacy-only tool | explicit legacy call | no false claim of native support |

The MCP server should use a capability/ownership map rather than model guesswork. Each feature part must have one owner. The layers should share schemas and transactions, not duplicate functionality.

## 6. Desired workflow

Example: “Create a third-person stamina and sprint system with movement, input, UI, settings, sound, animation hooks, save support, and debugging.”

1. Inspect project modules, character classes, input mappings, save classes, HUD widgets, animation Blueprints, audio assets, data tables and naming patterns.
2. Report reuse candidates and create one implementation plan with ownership for gameplay, Blueprint, UI, save, animation, audio and debug behavior.
3. Put stamina state and movement rules in project C++. Expose tuning/events through UPROPERTY/UFUNCTION. Use Blueprint for bindings. Use PuerTS for approved editor setup.
4. Build gameplay, input integration and save serialization using UE4.27 APIs, not UE5 Enhanced Input.
5. Create/modify Blueprints, animation hooks, widgets, settings UI and asset references. Compile after changes.
6. Connect input, UI, sound, animation and save behavior.
7. Compile, inspect references and save assets. Ask before PIE.
8. In PIE validate sprint, depletion, regeneration, UI, sound, animation, settings and save/reload.
9. Return all source files/assets/levels changed, compile results, tests, warnings and transaction IDs.
10. Leave the project clean and usable, without hidden scaffolding or uncompiled assets.

The present bridge can support inspection, selected property edits, actor setup, save/undo and some native operations. It does not yet supply the complete integrated feature-generation workflow.

## 7. Main goals

1. Reusable UE4.27 plugin with minimal project setup.
2. Broad stable access through C++, PuerTS, reflection, Blueprint/editor builders and MCP commands.
3. Faster implementation through reusable commands and existing project systems.
4. Complete feature creation across gameplay, behavior, UI, menus, assets, settings, debugging and integration.
5. Maintainability through explicit module boundaries, schemas, ownership and extension points.

## 8. Scalability and architecture

Recommended structure:

```text
Plugins/MCPBridge/
  Source/MCPBridgeCore/       Shared result, config, transaction types
  Source/MCPBridgePuerTS/     Native PuerTS host and C++ services
  Source/MCPBridgeEditor/     Editor builders and panel
  Content/JavaScript/         Generated runtime only
puerts-runtime/src/core/      protocol, safety, serializer
puerts-runtime/src/tools/     capability groups
mcp-server/src/native/        pipe client and MCP adapters
mcp-server/src/legacy/        temporary HTTP compatibility
extensions/<project>/         project-owned commands and content
```

Do not split existing modules merely for aesthetics. First make command ownership explicit. Add a shared C++ core only when result/config/transaction duplication is real.

Rules:

- Keep one canonical command registry with owner, transport, permission and schema.
- Use one response model across native and legacy lanes.
- Make C++ wrappers narrow and documented. Never export arbitrary native execution.
- Group TypeScript tools by capability.
- Mark generated files with generator/version metadata and commit or reproduce them deterministically.
- Keep project code in namespaced `BridgeExtensions`, not core tool folders.
- Include request ID and transaction ID in logs/errors.
- Version the wire contract, plugin, PuerTS bundle and declaration snapshot together.

| Current violation | Action |
|---|---|
| Python dependencies in main descriptor | split legacy plugin or remove requirement after migration |
| Panel mixes native status with HTTP actions | make native health primary and legacy explicit |
| Generated JavaScript untracked | RESOLVED 2026-07-31: policy is generated-never-tracked; `npm run build` produces all of Content/JavaScript (tsc output plus `Scripts/stage-puerts-runtime.mjs` staging from the pinned bundle) |
| PuerTS ignored/unpinned | use checksum-locked artifact or submodule |
| Disabled `MCPPuerTSBridge` test entry | verify then remove |
| Mixed 187-tool acceptance script | split native and legacy acceptance |
| `docs/PUERTS.md` stale version/tool count | correct after provenance verification |

## 9. Performance

PuerTS removes default loopback HTTP overhead. It does not make UE editor work inherently cheap. Asset loading, shader compilation, saves, Blueprint compilation and game-thread blocking dominate many operations.

Observed on the isolated 56-actor test scene: native actor query was roughly `0.036` to `0.057 ms`; JSON serialization roughly `0.286 ms`; total diagnostic roughly `1.2` to `1.6 ms`. A final sky call reported about `1.86 ms` native duration; save about `100.7 ms`; screenshot about `200.9 ms`. These are single-machine observations, not benchmarks or SLOs.

Required policy:

- Batch repeated actor/property operations.
- Use native C++ loops for large sets, asset registry scans, image data, material graphs and physics construction.
- Cache short-lived asset lookup results, never stale raw UObject pointers.
- Keep payloads below the 1 MiB cap and use paths/filters instead of level dumps.
- Measure queue, pipe, native, serialization, shader compile and save separately.
- Bound log capture, currently 2,000 lines.
- Add job/progress/cancel API for work longer than a command timeout. It does not exist now.
- Keep UE calls on game thread.

Suggested targets, not achieved claims: p95 query of 500 loaded actors below 50 ms; p95 simple property mutation below 100 ms excluding save/compile; progress for any task above 500 ms.

Current risks: global serial command queue, incomplete serializer, broad asset scans, unbounded shader/Blueprint work, no startup/hot reload baseline.

## 10. Current project status

| Area | Status | Evidence | Known issue | Required next action |
|---|---|---|---|---|
| Native named pipe | Working prototype | live diagnostic/tool calls | Win64 only; ACL unverified | security test/release config |
| First MCP milestone | Tested successfully | list, property edit, save, JSON, undo | no committed editor transcript | automate result fixture |
| C++ native module | Compiled/loaded | UE4.27 editor build passed | no fresh final install proof | install to new disposable project |
| TS registry | Built/unit-tested | 17 tools, test passed | docs corrected to 17 on 2026-07-31 | none |
| PuerTS pin | Verified 2026-07-31 | full-tree diff vs upstream tag; `Plugins/Puerts.lock.json`; enforced in verify and installer | uplugin VersionName stale upstream | none |
| Actor/property | Partially tested | core path successful | structs/arrays serialize as `{}` | type coverage/tests |
| Asset/function | Implemented | code/schema present | full live coverage not recorded | acceptance test |
| Spawn/delete | Implemented | code/schema present | final delete regression absent | test confirm/undo |
| PIE | Implemented, partial history | code exists | user approval required, timing coverage weak | isolated start/stop test |
| Physics | Compiled | helper + script exists | final physics smoke not rerun | run/record smoke |
| Screenshot | Tested successfully | aurora PNG visually reviewed | active viewport dependency | failure/headless tests |
| Aurora sky | Specialized demo tested | saved material/actor/level | not generic material editing | move to example or build graph API |
| Advanced material nodes | Not complete natively | legacy tools exist | PuerTS itself is not proven limited | expose builder or define graph JSON |
| Blueprint builders | Legacy capability | existing C++ module/docs | not native default, Widget incomplete | migrate by use case |
| Panel | Partial | source changes | mixed transport state | refactor health UI |
| Installer | Partial | copy/config logic | non-atomic, bundle unpinned | fresh install/hash/rollback |
| `npm run verify` | Passed with skips | build/tests/smoke: 8 pass, 0 fail, 2 skip | no token in bridge root, no UE_ENGINE_ROOT | controlled integration CI |
| Packaging | Not ready | source layout exists | ignored PuerTS, untracked JS, editor deps | package/staging test |
| Multi-project reuse | Partial | project-hash pipe installer | manual test suffix | two-project test |

The isolated project contains `/Game/MCPGenerated/M_NativeAuroraSky.M_NativeAuroraSky` and `/Game/MCPTests/FullAcceptance.FullAcceptance:PersistentLevel.MCP_NativeAuroraSkybox`. `/Game/MCPTests/FullAcceptance` was saved. The original `SkySphere` remains but is hidden and nearly zero scale. This proves the specialized demo only.

## 11. Risk register

| Risk | Rank | Mitigation |
|---|---|---|
| PuerTS provenance/version mismatch | Closed 2026-07-31 | pinned to Unreal_v1.0.9 commit 838ab762d830 with enforced checksum manifest |
| Untracked native/runtime output | High | review and commit source, define output policy |
| Python hard dependency | High | optional legacy plugin or remove descriptor dependency |
| Legacy arbitrary Python proxy | High | keep opt-in, explicit authorization/audit or remove public exposure |
| JS guards not an OS sandbox | High | OS isolation for untrusted scripts, minimal native surface |
| Pipe ACL/token storage unknown | High | explicit ACL, token protection/rotation, negative tests |
| UE4.27 editor/private API variation | Medium | isolate wrappers, test supported build |
| PuerTS UE4.27 support provenance | Medium | clean build from pinned source |
| Packaging/staging | High | package test against clean project |
| Platform support | Medium | declare Windows editor-only scope |
| Serializer defects | Medium | supported type table and tests |
| UObject lifetime/GC | Medium | paths/weak refs, GC/async stress tests |
| Thread safety | Medium | retain serial game-thread model, timeout/cancel tests |
| Blueprint generation gaps | Medium | use C++ builders, finish only needed paths |
| Large project performance | Medium | batches, cache, p95 measurements |
| Unreal.js conflict | Medium | installer detects/refuses duplicate V8 plugin |
| Maintenance cost of two lanes | Medium | migration plan and ownership table |

## 12. Team handoff

### Entry points

| Area | Path |
|---|---|
| Native module | `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeModule.cpp` |
| Native service | `.../Public/MCPPuerTSBridgeService.h`, `.../Private/MCPPuerTSBridgeService.cpp` |
| TypeScript runtime | `puerts-runtime/src/bootstrap.ts` |
| Tool registry | `puerts-runtime/src/registry.ts` |
| Safety/serializer | `puerts-runtime/src/safety.ts`, `runtime.ts` |
| MCP native adapter | `mcp-server/src/puerts-client.ts`, `tools/puerts.ts` |
| MCP registration | `mcp-server/src/index.ts`, `annotations.ts` |
| Legacy listener | `Plugins/MCPBridge/Content/Python/mcp_bridge/listener.py`, `router.py`, `startup.py` |
| Installer | `Scripts/install-mcp-bridge.ps1` |
| Unit test | `mcp-server/tests/puerts-tools.test.ts` |
| Live smoke | `Scripts/puerts-live-smoke.mjs` |

```powershell
Set-Location 'D:\Unreal Projects\UE4_Bridge'
npm install
npm run verify

$env:MCP_PUERTS_TOKEN_PATH = 'D:\Path\To\Project\Saved\MCPPuerTSBridge\token.txt'
$env:MCP_UNREAL_PROJECT_ROOT = 'D:\Path\To\Project'
npm run smoke:puerts
npm run smoke:physics
```

The recorded verification build and tests passed. Default smoke had eight passes and two skips: `UE_ENGINE_ROOT` was not set for engine-source validation, and the canonical bridge root lacked a live test-project token. This is not equivalent to automated editor integration.

Verified UE build form:

```powershell
& 'D:\UE\UE_4.27\Engine\Build\BatchFiles\Build.bat' `
  UE427PuerTSMCPEditor Win64 Development `
  'D:\Unreal Projects\MASTER_PROJECT\SF_Repository\Sinfeld_240301\Tests\UE427PuerTSMCP\UE427PuerTSMCP.uproject' `
  -WaitMutex -NoHotReload
```

Branch: `consolidate/unify-bridge-2026-07-29` at handoff time. Observed 2026-07-31 later that day: the working tree is on `main` at the same HEAD `042828a`, with the same uncommitted changes. AGENTS.md forbids pushing local `main`; branch from `origin/main` before staging bridge work. Native additions are uncommitted. Review `git status --short` and `git diff` before staging or distribution.

Debug and recovery:

- Read `<Project>\Saved\MCPPuerTSBridge\token.txt` and compare pipe config with `puerts_diagnostic`.
- On connection failure: close stale editors, confirm pipe/token/project root, rebuild plugin, restart Unreal and MCP client.
- On pipe timeout with editors apparently running, check for exited-process remnants before anything else. If `Get-Process UE4Editor` entries show `HasExited = True`, one reported thread and no window title while `\\.\pipe\UE427PuerTSMCP*` names remain, treat the editor state as dead and the pipe ownership and root cause as Unknown. The displayed memory and handle counts are stale process-object data. Do not launch another editor; it adds another confused instance. Reboot Windows, launch exactly one editor through `Scripts/start-ue4-project.ps1`, wait for the project window to finish loading, then run `puerts_diagnostic`. Investigate Event Viewer or drivers only if the failure recurs after a clean reboot. Observed 2026-07-31: five such remnants survived `Stop-Process -Force`, and `taskkill` reported no running instance for each.
- If panel says disconnected: determine whether it reports legacy HTTP or native PuerTS status.
- If a read returns `{}`, add serializer support for that UE type.
- On timeout: inspect PIE, blocked game thread, asset/shader compile and Output Log. Native limits clamp between 100 ms and 30 s.
- On partial mutation failure: use returned transaction ID and `puerts_undo` before further saves. There is no automatic rollback.

Open questions: exact PuerTS revision and license/distribution status; whether Python becomes a separate plugin; which legacy builders matter first; intended packaged-game scope; local AI threat model; status of obsolete `MCPPuerTSBridge` entry.

## 13. Prioritized roadmap

### Immediate stabilization

| Priority | Task | Expected result | Dependencies | Risk | Done when |
|---:|---|---|---|---|---|
| P0 | Pin PuerTS | DONE 2026-07-31: revision and checksum manifest verified by installer and npm run verify | upstream access | Low | revision/checksum verified by installer |
| P0 | Review/commit working tree | recoverable source | code review | Medium | native code/runtime/tests committed |
| P0 | Correct docs/counts | no false claims | registry | Low | docs match 17 tools and actual bundle |
| P0 | Native-only dependencies | Python no longer required | descriptor decision | Medium | clean project opens with MCPBridge+PuerTS only |
| P0 | Fresh-install smoke | installer proof | pinned bundle | Medium | new project compiles and passes milestone |

### Reusable plugin packaging

| Priority | Task | Expected result | Dependencies | Risk | Done when |
|---:|---|---|---|---|---|
| P1 | Harden installer | checks, rollback, diagnostics | P0 | Medium | repeatable update preserves content |
| P1 | Pipe/token hardening | unauthorized client rejected | security design | High | ACL/token negative test passes |
| P1 | Remove stale test config | test equals supported install | fresh install | Low | no obsolete entry/manual suffix |
| P1 | Package validation | installable artifact | pinned PuerTS | High | staged plugin passes clean-project test |

### Unified command architecture

| Priority | Task | Expected result | Dependencies | Risk | Done when |
|---:|---|---|---|---|---|
| P1 | Ownership map | STARTED 2026-07-31: `docs/TOOL_INVENTORY.json` freezes all 189 tools with backend, contract, and migration state, enforced in `npm run verify`; policy in `docs/TOOL_MIGRATION.md` | legacy inventory | Medium | registry labels native/legacy/local/extension |
| P1 | Shared result contract | consistent responses | C++/TS/Python edits | Medium | contract tests pass in both lanes |
| P1 | Serializer repair | supported values correct | type list | Medium | structs, arrays, maps, enums and refs tested |
| P2 | Dashboard refactor | native health primary | panel work | Low | native-only status is accurate |

### Full feature-generation workflow

| Priority | Task | Expected result | Dependencies | Risk | Done when |
|---:|---|---|---|---|---|
| P2 | Project inspection | reuse-aware planning | project queries | Medium | finds character/input/UI/save conventions |
| P2 | Blueprint/asset migration | needed builders under stable contract | legacy audit | Medium | one complete Blueprint feature passes |
| P2 | Acceptance harness | end-to-end feature validation | user-approved PIE | Medium | stamina/sprint sample builds, runs, saves, reports |
| P3 | React-style UMG research | evidence-based decision | stable widget builder | Medium | prototype determines need; do not install React-UMG now |

### Performance, tests, adoption

| Priority | Task | Expected result | Dependencies | Risk | Done when |
|---:|---|---|---|---|---|
| P2 | Timing traces and batches | measurable p50/p95 | request IDs | Low | 500-actor reference test meets agreed target |
| P2 | Long-job API | progress/cancel | protocol version | Medium | long tasks do not freeze client/editor |
| P1 | Native editor integration suite | real UE4.27 coverage | disposable project automation | Medium | actor/property/undo/PIE/screenshot tests unattended |
| P2 | Two-project test | pipe isolation | installer | Medium | no cross-talk between editors |
| P2 | Security regressions | unsafe calls rejected | threat model | High | bad token/path/shell/property tests pass |
| P1 | Runbook | independent developer install | P0/P1 | Low | fresh teammate completes install |

## 14. Final technical assessment

The current system is a working internal prototype. It is not a reusable install-anywhere plugin yet because PuerTS is unpinned/ignored, generated/runtime files are not governed, Python remains a declared dependency, and final clean installation has not been proven.

It is close to controlled installation into another Windows UE4.27 C++ project, but not close enough to promise arbitrary-project setup. The shortest path is: pin and commit PuerTS/integration artifacts, make native-only dependencies real, run a clean installer acceptance test, then add security and serializer coverage.

It is farther from complete feature creation. The architecture can support complete features, and legacy builders offer useful existing capability, but no unified tested workflow currently discovers project conventions, selects an implementation layer, builds gameplay/UI/assets, validates them and reports a finished integrated feature.

Reliable: TypeScript build/tests, direct named-pipe JSON flow, core transaction path, actor/property/save/undo milestone, and native screenshot in the isolated test project.

Experimental: PuerTS provenance, clean install, packaging, security hardening, hot reload, reflection serialization, generic material nodes, physics smoke, legacy migration, panel correctness and multi-project isolation.

Keep the direct named pipe default, strict JSON contract, native C++ safety boundary, transactions, allowlists, explicit legacy opt-in, project extension concept, and the decision not to install Unreal.js or React-UMG. Change the Python hard dependency, untracked artifact policy, stale docs, mixed panel transport state, duplicate command ownership, manual test pipe suffix and the mistaken equivalence between Custom-HLSL demo and generic material support.

The practical route is not to port all legacy tools. Establish one reproducible native baseline first, then migrate only the legacy capabilities needed for actual complete features.

### Current working-tree file inventory

No tracked file is reported as deleted by `git status --short`. This is the complete file-level inventory at handoff, not a claim that each listed file was changed today.

**Modified tracked files**

```text
.gitignore; AGENTS.md; README.md; package.json; package-lock.json
Scripts/install-mcp-bridge.ps1; Scripts/mcp-smoke.mjs
clients/codex-config.toml; clients/gemini-settings.json
Plugins/MCPBridge/MCPBridge.uplugin
Plugins/MCPBridge/Content/Python/startup.py
Plugins/MCPBridge/Content/Python/mcp_bridge/listener.py
Plugins/MCPBridge/Content/Python/mcp_bridge/router.py
Plugins/MCPBridge/Content/Python/mcp_bridge/state.py
Plugins/MCPBridge/Content/Python/mcp_bridge/handlers/actors.py
Plugins/MCPBridge/Content/Python/mcp_bridge/handlers/level.py
Plugins/MCPBridge/Content/Python/mcp_bridge/handlers/materials.py
Plugins/MCPBridge/Content/Python/mcp_bridge/handlers/system.py
Plugins/MCPBridge/Source/MCPBridgeEditorPanel/MCPBridgeEditorPanel.Build.cs
Plugins/MCPBridge/Source/MCPBridgeEditorPanel/Private/MCPBridgePanelModule.cpp
mcp-server/package.json; mcp-server/src/annotations.ts; mcp-server/src/index.ts
mcp-server/src/tools/actors.ts; mcp-server/src/tools/materials.ts
mcp-server/tests/actor-tools.test.ts; mcp-server/tests/material-blueprint-tools.test.ts
```

**Untracked files/directories**

```text
Plugins/MCPBridge/Content/JavaScript/{bootstrap,registry,runtime,safety,types}.js
Plugins/MCPBridge/Content/JavaScript/puerts/*.js
Plugins/MCPBridge/Source/MCPBridgePuerTS/MCPBridgePuerTS.Build.cs
Plugins/MCPBridge/Source/MCPBridgePuerTS/Public/MCPPuerTSBridgeService.h
Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/{MCPPuerTSBridgeModule,MCPPuerTSBridgeService,MCPPuerTSBridgePhysics,MCPPuerTSBridgeViewport,MCPPuerTSBridgeMaterial}.cpp
Scripts/build-castle.mjs; Scripts/editor-acceptance.mjs; Scripts/puerts-live-smoke.mjs
docs/PUERTS.md
mcp-server/src/puerts-client.ts; mcp-server/src/tools/puerts.ts; mcp-server/tests/puerts-tools.test.ts
puerts-runtime/package.json; puerts-runtime/tsconfig.json
puerts-runtime/src/{bootstrap,registry,runtime,safety,types}.ts
puerts-runtime/types/puerts-bootstrap.d.ts
puerts-runtime/typing/ue/ue.d.ts
```

The `puerts/*.js` subtree is generated PuerTS support output. Capture its individual file list in the release manifest. The PuerTS source bundle itself is ignored and therefore absent from Git inventory.
