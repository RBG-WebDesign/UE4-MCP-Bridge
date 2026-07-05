# MCP Bridge Architecture Audit (2026-07-04)

Full-repository audit of the UE4.27 MCP plugin system: what runs, what is tracked,
what is broken, and the plan to make a clean checkout produce a working,
production-grade bridge.

## 1. Architecture map (as it actually runs today)

```
MCP client (Claude Code / Codex / Cursor)
  |  stdio (MCP protocol)
  v
mcp-server/dist/index.js          [TRACKED]  83 tools, Zod schemas, history/undo
  |  HTTP POST / (JSON {command, params}), 300s client timeout
  v
Python listener  localhost:8080   [UNTRACKED: Content/Python/mcp_bridge]
  |  single-threaded HTTPServer on background thread
  |  queue -> register_slate_post_tick_callback (game thread, 10 cmds/tick)
  v
router.py COMMAND_ROUTES (104 routes in live tree, 76 in tracked plugin copy)
  |  handlers/*.py  (actors, blueprints, level, materials, project, system,
  |                  viewport, widgets, titles, effects, gameplay, promptbrush,
  |                  + live-only: optimization, project_index, panel, state)
  v
unreal module (Python API)  +  C++ libraries via Python bindings:
  - BlueprintGraphBuilderLibrary / BehaviorTreeBuilderLibrary /
    WidgetBlueprintBuilderLibrary / AnimBlueprintBuilderLibrary
    [UNTRACKED plugin: Plugins/BlueprintGraphBuilder - 104 files, 10.9k LOC]
  - MCPBridgePanel (status panel, profiler library)
    [UNTRACKED plugin: Plugins/MCPBridgePanel]
```

Layer ownership as found:

| Concern | Owner today | Notes |
|---|---|---|
| Tool schemas / client validation | TypeScript (Zod) | good |
| Routing | TS index.ts map + Python COMMAND_ROUTES | duplicated by convention (name == command), no consistency check until now |
| Transport | unreal-client.ts (only HTTP caller) | resolves `{success:false}` on connect errors; never rejects |
| Game-thread safety | listener.py tick queue | correct mechanism; long commands still block editor + all other requests |
| Validation | split: Zod (shape) / handlers (semantics) / C++ validators (BT, Widget, ABP) | graph-edit validation only in untracked C++ (BPPinOps: CanCreateConnection -> TryCreateConnection) |
| Asset mutation | Python handlers + C++ builders | compile+save fallback chain in blueprints.py is solid |
| Error handling | envelope {success, data, error} at every layer | consistent; router returns unknown-command error with available list |
| Job state | NONE | synchronous only; 60s server timeout vs 300s client timeout mismatch; timed-out commands still execute later (zombie execution) |
| Undo/history | TS history.ts + Python transactions.py (@transactional) | @transactional used by only 3 of 12+ handler modules |

## 2. Top structural risks (ranked)

1. **The tracked plugin cannot build.** `Plugins/MCPBridge/Source/*/*.Build.cs`
   rule classes are named `MCPBridgePanelBundledInactive` /
   `BlueprintGraphBuilderBundledInactive`. UBT requires rule class name ==
   module name, so a clean checkout's plugin is inert by construction.
   Hard constraint "clean checkout installs and builds" fails today.
2. **Everything that actually runs is untracked.** The live Python listener
   (`Content/Python/mcp_bridge`), both live C++ plugins
   (`Plugins/MCPBridgePanel`, `Plugins/BlueprintGraphBuilder`), the engine
   config (`Config/DefaultEngine.ini`), the `.uproject`, the recovery scripts
   (`Scripts/recover_mcp_bridge.*`), and CI (`.github/workflows/ci.yml`) are
   all untracked. A clone gets a stale subset.
3. **Tracked C++ lost the graph-safety layer.** The consolidation dropped
   `BlueprintInspector/` (graph reading) and `BlueprintMutator/` (~37 files),
   which contain the only schema-validated pin connection code
   (`BPPinOps.cpp: CanCreateConnection` before `TryCreateConnection`). The
   tracked `BlueprintGraphBuilderLibrary.cpp` connects exec pins with raw
   `MakeLinkTo` and no schema validation - violates the fragile-asset rule.
4. **Four advertised tools are dead in every tree.** `project_index_rebuild`,
   `project_index_query`, `project_semantic_diff`, `gameplay_pattern_search`
   are registered in TS and have handlers written (`handlers/project_index.py`,
   live tree only) but were never imported into any router. Callers get
   "Unknown command". Unit tests pass anyway because they run against a mock -
   the test suite structurally cannot catch TS<->Python drift. (FIXED in this
   pass: handler ported, routes wired, drift test added.)
5. **Python trees diverged file-by-file.** Live tree is ahead everywhere
   (e.g. listener 300s timeout + state module vs plugin 60s), plus 28 extra
   routes (optimization suite, bridge panel/status commands) and extra modules
   (panel.py, state.py, optimization/, project_index.py, gameplay_framework.py).
6. **No async job system.** One long command (bake, cook, big index rebuild)
   blocks the single-threaded HTTP server AND the editor tick; a server-side
   timeout abandons the request but the queued command still executes later
   with nobody listening (silent mutation).
7. **Docs contradict each other.** Tracked CLAUDE.md declares
   `Plugins/MCPBridge` the single source of truth and says the old trees were
   removed; untracked `docs/MCP_BRIDGE_MAINTENANCE.md` correctly documents the
   opposite (panel plugin + project Content/Python are active).
8. **UE5 token in tracked tree.** `generation/pie_harness.py` calls
   `unreal.LevelEditorSubsystem` as a "UE5+" fallback (guarded by try/except,
   so harmless at runtime, but violates the project's forbidden-token rule).
9. **`@transactional` coverage is thin.** Only actors/blueprints/materials use
   it; level, effects, widgets, titles mutations are not uniformly wrapped.
10. **Installer is good but unvalidated end-to-end.** `Scripts/install-mcp-bridge.ps1`
    references only tracked paths (verified), but installs the unbuildable
    plugin (risk 1), so a fresh install cannot produce working C++ builders.

## 3. Tool gap matrix (current 83 tools vs the full game-dev surface)

Status of what exists (by group), then the missing surface.

| Group | Tools | Status | Route/Handler | Validation | Save | Tests | Risk |
|---|---|---|---|---|---|---|---|
| System/bridge | test_connection, help, ue_logs, clear_output_log, restart_listener, python_proxy, bridge_* | implemented | system.py | good | n/a | mock | low (python_proxy is the unguarded escape hatch by design) |
| Project | project_info, asset_list/info, asset_save_many, project_enable_plugins, input_mapping_info | implemented | project.py | good | explicit | mock | low |
| Intelligence | project_index_*, project_semantic_diff, gameplay_pattern_search | **advertised-only (dead)** -> fixed this pass | none -> project_index.py | n/a | n/a (read-only cache) | mock only | was high (silent failure) |
| Actors | spawn/modify/delete/duplicate/organize/snap, batch_spawn, placement_validate | implemented | actors.py | validate param, transactional | level dirty (needs level_save) | mock + integration | medium |
| Level | level_actors, level_outliner, level_save | implemented | level.py | ok | explicit | mock | low |
| Viewport | camera/focus/fit/look_at/mode/render_mode/bounds/screenshot | implemented | viewport.py | ok | n/a (correctly non-transactional) | mock | low |
| Materials | create/apply/info/list | implemented | materials.py | ok | explicit save | mock + integration | low |
| Blueprints | create/compile/info/list/document, build_from_json, build_from_description | implemented | blueprints.py -> C++ | C++ validators; **pin-level schema validation only in untracked tree** | compile+save fallback chain | mock + integration | **high until C++ tree synced** |
| Widgets/titles | widget_build_from_json, title_* (10 tools) | implemented | widgets.py/titles.py -> C++ | C++ validator | save | mock | medium |
| Effects | pp_volume_*, pp_preset, camera_shake_*, console_effect | implemented | effects.py | ok | save | mock | medium (trigger placement rules doc'd) |
| Gameplay | pie_start/stop, acceptance tests, telemetry | implemented | gameplay.py | ok | n/a | mock | medium (PIE policy: user-initiated only) |
| AI/BT | anim_blueprint_build_from_json, BT via blueprint_build_from_json path | implemented (C++) | blueprints.py -> BTBuilder | BTValidator | save | C++-side only | medium |
| Operations | undo/redo/checkpoint/history/batch | implemented (TS-side orchestration) | operations.ts | ok | n/a | mock | low |
| PromptBrush | prompt_generate/status/spec_list | implemented, external plugin dependency | promptbrush.py | ok | n/a | none | medium (depends on out-of-repo plugin) |
| Optimization suite | 24 optimization_* routes | live tree only, **no TS tools** (panel-driven) | optimization.py (untracked) | ? | ? | none | medium |

**Missing for full game creation** (priority order):

1. C++ generation/build: create class, module deps, UBT build, error parse. (nothing exists)
2. Blueprint member/graph editing as MCP tools: add variable/function/component/
   event dispatcher, add/find/delete node, connect pins with diffs. C++ exists
   (BlueprintMutator/Inspector, untracked) but is NOT exposed as MCP tools.
3. Gameplay framework wizards: GameMode/GameInstance/PlayerController/etc. +
   default-map config (blueprint_create can parent to these; no config writer).
4. Input mapping mutation: read exists (input_mapping_info); add/remove
   action/axis mappings missing.
5. Camera rigs: SpringArm+Camera presets missing (components possible via
   blueprint_build_from_json components block).
6. AI: blackboard_create/bt_create as first-class tools (C++ builder exists);
   AIPerception, NavMeshBoundsVolume spawn (actor_spawn may cover), nav build.
7. Data assets: DataTable/CurveTable/PrimaryDataAsset CRUD. (nothing)
8. Audio: AudioComponent, attenuation, cues. (nothing)
9. Animation: blendspaces, montage assignment beyond ABP v1. (partial)
10. Packaging/cook: cook_generator.py exists in generation/ but no tool route.
11. Job system: none (required for UBT builds, cooks, index rebuilds).

## 4. Target architecture

Keep the 3-layer split; make the tracked plugin the only distribution unit.

```
mcp-server/ (TypeScript)        - schemas, routing, job orchestration, history
Plugins/MCPBridge/ (tracked)    - THE product. Contains:
  MCPBridge.uplugin             - modules: MCPBridgePanel, BlueprintGraphBuilder
  Source/BlueprintGraphBuilder/ - full tree incl. BlueprintInspector + BlueprintMutator
  Source/MCPBridgePanel/        - panel + profiler
  Content/Python/mcp_bridge/    - full live tree (listener, router, handlers,
                                  optimization, panel, state, project_index)
Host project                    - enables MCPBridge plugin; DefaultEngine.ini
                                  points Python at the PLUGIN Content/Python
```

Responsibility rules (confirmed correct, keep):
- TypeScript: MCP schemas, client-facing validation, tool->command routing,
  job_id issuance and polling, structured envelopes, history.
- Python: editor scripting, asset ops, level ops, orchestration of C++ libs,
  transactions, save/compile policy.
- C++: Blueprint graph mutation (schema-validated pin ops), BT/Widget/ABP
  builders, anything UE4.27 Python reflection blocks (BT internals, K2 nodes).

Job system design (Milestone 4): Python-side job table (state.py exists in the
live tree as the seed), `job_start` returns job_id; long handlers run their
unreal work in tick-sliced steps or subprocess (UBT/cook); TS exposes
job_status/job_cancel; the HTTP request always returns fast. Server and client
timeouts aligned; queued-but-timed-out commands are dropped, not zombie-executed.

## 5. Roadmap (15 milestones, order confirmed by audit)

M1  Repo/install truth: sync live Python + full C++ into Plugins/MCPBridge,
    restore buildable Build.cs names, track recovery scripts + CI, fix docs.
    Acceptance: fresh clone -> installer -> UBT builds plugin -> listener up.
M2  Registry consistency: static test that every TS-sent command has a router
    route (and no orphan routes); runs in npm test with no editor. [DONE this pass]
M3  Error/result envelope hardening: uniform envelope everywhere, timeout
    alignment, drop-zombie-commands fix in listener.
M4  Async job system (state.py -> job table, job_status/job_cancel tools).
M5  Blueprint asset safety: @transactional coverage on all mutating handlers,
    compile+save+revert-on-fail policy everywhere.
M6  Graph connector: expose BlueprintMutator ops (add_node/connect_pins/diff)
    as MCP tools; all connections via K2Schema validation.
M7  C++ class generation + UBT build tool + compiler error parser (job-based).
M8  Level assembly completions (folders, volumes, nav bounds are mostly there).
M9  Gameplay framework wizards + project settings writer.
M10 Input/camera/interaction templates.
M11 AI first-class tools (blackboard_create, behavior_tree_create, perception,
    nav build) on the existing BT builder.
M12 UMG expansion on WidgetBuilder.
M13 Materials/post/anim/audio helpers.
M14 Game template generators (extend generation/mechanics/*).
M15 Clean-checkout E2E validation + docs freeze.

## 6. Fixes completed in this pass (2026-07-04)

1. Ported `handlers/project_index.py` into the tracked plugin tree and wired
   `project_index_rebuild`, `project_index_query`, `project_semantic_diff`,
   `gameplay_pattern_search` into BOTH routers (tracked plugin + live). The 4
   dead tools now execute; verified live against the running editor.
2. Added `mcp-server/tests/registry-consistency.test.ts`: parses the plugin
   router's COMMAND_ROUTES and every `sendCommand("...")` in TS source; fails
   on any advertised command without a route. Wired into `npm test`.
3. Synced the full C++ source (BlueprintInspector, BlueprintMutator, current
   library versions - 104 files) from the live plugins into
   `Plugins/MCPBridge/Source`, and synced the full live Python tree
   (optimization suite, panel, state, project_index, console_safety) into
   `Plugins/MCPBridge/Content/Python`. The modules were renamed to
   `MCPBridgeGraphBuilder` and `MCPBridgeEditorPanel` (Build.cs rule class,
   .uplugin, API export macros, IMPLEMENT_MODULE) so the unified plugin is
   buildable on a clean checkout AND can coexist with the legacy
   `Plugins/BlueprintGraphBuilder` / `Plugins/MCPBridgePanel` trees still
   present in this host project - UBT compiles every Build.cs under Plugins/
   into one rules assembly, so restoring the old class names would have broken
   the host project's next compile. UCLASS names (BlueprintGraphBuilderLibrary
   etc.) are unchanged, so Python callers are unaffected. Installer manifest
   and CLAUDE.md paths updated. `host project data (mcp_bridge/data/) is
   intentionally NOT shipped in the plugin.

## 7. Status update (end of 2026-07-04 session)

Completed after the initial audit pass, all verified live against the editor:

- Host project switched to the unified tracked plugin (editor restart cycle):
  .uproject enables MCPBridge, DefaultEngine.ini points at the plugin Python,
  legacy Plugins/MCPBridgePanel + Plugins/BlueprintGraphBuilder moved to
  _legacy_plugins/, project Content/Python retired (data/ kept in place).
  Backups in _legacy_bridge_backup/.
- M2 registry consistency test (runs first in npm test).
- M3 listener reliability: timed-out commands are cancelled instead of
  zombie-executing; restart_listener defers off the request path (was a
  deadlock); client timeout (320s) now exceeds server timeout (300s).
- M6 Blueprint graph editing: 18 tools over the C++ Inspector/Mutator;
  schema-validated pin connections; transaction + compile + save per
  mutation.
- M4+M7 job system (subprocess jobs, cancellation, exclusive kinds) +
  cpp_class_create + cpp_build with structured compiler-error parsing.
- M9-M11: input mapping tools + presets (via new UMCPBridgeInputLibrary;
  FKey is not constructible from 4.27 Python), gameplay_framework_create,
  project_settings_maps, camera_rig_create, blackboard_create +
  behavior_tree_create (via new UMCPBridgeAILibrary; BlackboardAsset is
  protected from Python), ai_nav_rebuild.
- M5 (partial): transactions added to the six level-mutating effects
  handlers; blueprint_graph and gamedev mutations are transactional from
  birth.
- Clean-checkout plugin build re-verified via AutomationTool BuildPlugin
  after the C++ additions (BUILD SUCCESSFUL).

## 8. M13/M14 addendum (2026-07-05)

Completed and verified live:
- Material instances: material_instance_create / set_params (scalar/vector/
  texture overrides; missing params rejected explicitly).
- DataTables: data_table_create / fill_from_json. IMPORTANT FINDING -
  unreal.DataTableFunctionLibrary.fill_data_table_from_json_string shows a
  MODAL summary dialog on the game thread that hangs the bridge (observed:
  three stacked modals had to be closed via Win32 PostMessage during
  testing). Replaced with the C++ UMCPBridgeDataLibrary wrapper around
  UDataTable::CreateTableFromJSONString, which returns import problems as
  structured data with no dialog. Verified: 2-row GameplayTagTableRow fill in
  0.76s, structured problem reporting for malformed rows.
- Audio: audio_component_add (AudioComponent + optional SoundBase).
- Maps: level_new (guards against unsaved changes before switching levels).
- M14: game_template_create composes framework + camera rig + input preset +
  optional map/default game mode. Verified a full third_person template.

## 9. Remaining risks / not done

- Widget/title/asset-creation handlers are not transaction-wrapped (they
  validate via compile+save instead; acceptable but inconsistent).
- Cook/packaging tools are not implemented (cook_generator.py exists in
  generation/ but has no tool route). CurveTable and PrimaryDataAsset CRUD
  not implemented.
- SetCallFunctionTarget exists in C++ but is not exposed as an MCP tool.
- project_settings_maps was not exercised against a real map switch (writes
  ini + CDO; logic reviewed, but no live round-trip).
- level_new / game_template_create with create_map=True switch the loaded
  level; only the create_map=False path was exercised live (to avoid
  disturbing the user's open level).
- CI workflow (.github/workflows/ci.yml) remains untracked and unreviewed.
- Legacy trees in _legacy_plugins/ and _legacy_bridge_backup/ can be deleted
  after a week of stable operation.
