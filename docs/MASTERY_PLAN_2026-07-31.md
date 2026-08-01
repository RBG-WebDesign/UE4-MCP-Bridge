# UE4.27 mastery plan

Authored 2026-07-31 by the orchestrating session (Fable 5). Execution is
delegated to Opus 5 subagents per phase. The user is away for two days and has
pre-authorized: PIE, compiles, editor restarts, asset/material/blueprint
creation, and full gameplay feature construction in the test projects. Hard
rules that survive all permission grants: puerts_* transport only, exact
errors reported, no game code in the bridge plugin, no push, evidence before
success claims, checkpoint before destructive changes.

## Standing environment facts

- Session MCP server binds pipe `\\.\pipe\UE427PuerTSMCP_UE427PuerTSMCP_81d778e7_skyshader5`
  (test project UE427PuerTSMCP). That editor must be the one running for the
  session's puerts_* tools to work. Launch only via Scripts/start-ue4-project.ps1.
- Clean-install proof project: D:\Unreal Projects\BridgeInstallTest
  (pipe `UE427PuerTSMCP_BridgeInstallTest_eb10ef4f`). Reachable by spawning a
  dedicated server via env overrides (see puerts-live-smoke.mjs), not by the
  session tools. Use it for install/rebuild regression, not daily probing.
- Branch: bridge/native-consolidation-2026-07-31. Commit per completed phase.
- Verify gate: npm run verify (build, unit suites, pin check, inventory check, smoke).
- C++ changes require: edit in bridge repo, re-run installer or sync to the
  target project, Build.bat <Target>Editor Win64 Development, editor restart.

## Phase W: Wrap slice (compatibility router)

Goal: prove the router model on the 12 hybrid_candidate legacy names.

- New module `mcp-server/src/tools/compat.ts`: createCompatTools(puertsClient)
  returns alias ToolDefinitions for the 12 names in docs/TOOL_INVENTORY.json
  with target_replacement set. Registered only when MCP_COMPAT_ALIASES=1.
- Each alias maps legacy params to the native schema (example: actor_spawn
  {type,name,location} -> puerts_spawn_actor {class_path,location}), calls the
  native handler, and returns the native result wrapped with requested_tool,
  canonical_tool, backend fields.
- Unmappable legacy params fail loud with a clear error, never silently drop.
- Tests: mcp-server/tests/compat-tools.test.ts. Assert alias->canonical
  routing, param translation, the wrapper fields, and that aliases are absent
  without the flag. Update Scripts/generate-tool-inventory.mjs MODULES list,
  regenerate inventory (migration_state wrap for the 12), annotations for
  alias names, npm run verify green, commit.

## Phase P: Capability probe matrix

Goal: exercise every native tool against the live editor and record every
limitation with a reproduction. Output: docs/CAPABILITY_FINDINGS.md plus
reports/session JSON. Probe at minimum:

- Serializer coverage: read_property on struct, array, map, enum, object-ref,
  text, name properties (known defect: some return {}).
- call_function allowlist surface: what is callable, what errors.
- spawn_actor class coverage: engine classes, Blueprint classes by _C path.
- find_assets filters, set_property on component vs actor, save on titled and
  untitled maps, undo depth behavior, screenshot actor matching (known defect),
  sky_shader_create rerun behavior, physics_build/observe, pie_start/stop
  round trip, get_logs bounds.

## Phase L: Limitation removal

For each Phase P finding, smallest fix in the right layer. Known targets
before probing (add to this list as found):

1. viewport_screenshot full-path matching (task chip already filed).
2. Reflection serializer: structs/arrays/maps return {} - fix in
   puerts-runtime/src/runtime.ts serializer, C++ helper if needed.
3. No native asset/material/blueprint creation beyond the sky demo: re-front
   existing C++ builders (MCPBridgeGraphBuilder) through the MCPPuerTSBridge
   allowlist as puerts_blueprint_build (JSON in, compiled Blueprint out).
4. No console command execution: consider narrow native wrapper (needed for
   Puerts.Gen FULL automation); allowlist specific commands only.
5. Batch primitives: batch spawn/modify in one pipe request.

C++ additions follow the 7-step "Adding a new tool" checklist in AGENTS.md.
Editor rebuild cycle uses the test project; BridgeInstallTest re-proves the
installer after plugin changes.

## Phase F: Feature construction probe

With Phase L primitives, build progressively harder content in the test
project until something breaks; every break loops back to Phase L:

1. Material: authored material asset applied to a spawned mesh (beyond the
   hardcoded aurora demo).
2. Blueprint actor: BP with components, variables, one event graph, compiled,
   spawned, verified in PIE.
3. Gameplay slice: trigger volume + door + sound + HUD widget, PIE-verified
   via pie_agent-style observation or logs.
4. Stamina/sprint feature per the standing /goal (the full pipeline reference
   feature) once the above pass.

## Cadence and reporting

- One phase chunk per Opus 5 agent, orchestrator reviews diffs and evidence.
- Commit per completed chunk with evidence in the message.
- reports/session-<date>-<chunk>.json per chunk.
- Wakeup loop keeps the session advancing; on wake: check agent status, land
  finished work, dispatch the next chunk, update this plan's checkboxes.

## Progress

- [x] Plan authored
- [x] Phase W complete and verified (commit 92ee569; 12 aliases, 148-assertion suite, verify green; agent also fixed a stale overlap table - real count was 10, restored to 12 with substitutes ue_logs and asset_save_many)
- [x] Phase P findings documented (docs/CAPABILITY_FINDINGS.md; first batch - serializer/marshaling localized, call_function surface mapped, BP spawn proven)
- [x] Phase L: serializer fixed (struct and array marshaling in both directions;
      reads now go through native FJsonObjectConverter for object paths too, and
      the untyped `value` schema that made clients stringify structs is gone;
      live acceptance in docs/CAPABILITY_FINDINGS.md, verify green)
- [x] Phase L: builder re-fronted (`puerts_blueprint_build`: JSON spec in,
      compiled and saved Blueprint actor out, through the existing
      MCPBridgeGraphBuilder rather than a rewrite. Validate-before-mutate, so a
      rejected spec never creates an asset; idempotent on rerun. Real node
      vocabulary is eight types, not the eleven passes the specs describe;
      recorded in docs/CAPABILITY_FINDINGS.md)
- [x] Phase L: component properties on generated Blueprints (`properties` per
      component, marshaled through FJsonObjectConverter with UObject references
      resolved by explicit load; two silent-no-op holes in the converter's
      ImportText fallback found by probing and closed)
- [x] Phase F1 material (BP_ProbeDoor rebuilt with an engine mesh, an engine
      material, and the Game-authored M_NativeAuroraSky on a second component;
      spawned and visually confirmed in
      `Saved/Screenshots/MCPBridge/phase-f1-component-properties.png`; idempotent
      rerun and changed-value rerun both proven, level returned to 12 actors)
- [x] Phase F2 blueprint actor (BP_ProbeTrigger and BP_ProbeDropper authored
      from JSON alone, spawned, and proven in PIE: `MCP_TRIGGER_ALIVE` from the
      generated BeginPlay, then `MCP_OVERLAP_ENTER`/`MCP_OVERLAP_EXIT` twice as
      a JSON-authored physics body fell through the trigger volume. Four PIE
      round trips, log lines and timings in
      `reports/session-2026-08-01-f2-pie.json`; screenshot
      `Saved/Screenshots/MCPBridge/phase-f2-scene-before-pie.png`; level back to
      12 actors)
- [x] Phase L: mutator re-front (`puerts_blueprint_build` takes `variables`, and
      its graph vocabulary is 26 node types instead of eight: the builder keeps
      eleven local dispatch cases and asks `FBPNodeRegistry` for the rest, gated
      through `GetSupportedNodeTypes` so the MCP enum and the dispatch cannot
      diverge. Named `Operator` nodes cover the comparison and math calls a door
      and a stamina bar need. All 26 types built live and compiled clean;
      details and two engine gotchas in docs/CAPABILITY_FINDINGS.md)
- [x] Phase F3 gameplay slice - **complete**. Trigger volume, moving door,
      sound and HUD widget all fire in one PIE session, from JSON specs and
      three spawns:
      `[BP_ProbeHUDHost_C_1] MCP_HUD created=1 in_viewport=true`,
      `MCP_HUD after_1s in_viewport=true`,
      `MCP_HUD painted_size=X=1480.908 Y=1080.192`,
      `[BP_ProbeDoorV3_C_1] MCP_DOOR_OPENING panel=X=950.000 Y=0.000 Z=220.000`,
      `MCP_DOOR_SOUND spawned=1 playing=true`,
      `MCP_DOOR_OPENED panel=X=950.000 Y=0.000 Z=620.000`,
      `MCP_DOOR_SOUND after_1s component_valid=false`.
      **Sound** needed no new C++: `CallFunction` was never gated to a function
      list, so `GameplayStatics.SpawnSoundAtLocation` with an engine SoundWave
      on its `Sound` pin was already reachable. It returns the AudioComponent,
      which is what makes the playing state readable; a void `PlaySound*` would
      have proved nothing. Audible is not provable from here and is not claimed.
      **Widget** got one new native command, `puerts_widget_build`, fronting the
      `UWidgetBlueprintBuilderLibrary` that was already implemented and compiled
      in `MCPBridgeGraphBuilder` despite its spec saying otherwise. Details,
      rejection sweep and three new limitations (20 unresolved connections are
      log-only, 21 const BlueprintCallable is pure, 22 BlueprintInternalUseOnly
      does not block a spawned CallFunction) in docs/CAPABILITY_FINDINGS.md;
      evidence in reports/session-2026-08-02-sound-widget.json; screenshot
      `Saved/Screenshots/MCPBridge/phase-f3-door-sound-hud.png`.
      The earlier state of this line, for the record: the **door was done**:
      `/Game/MCPGenerated/BP_ProbeDoorV2` is authored from one JSON spec with a
      `bIsOpen` variable, a BoxComponent trigger and a 19-node graph, and in PIE
      it logs `MCP_DOOR_OPENING panel=X=950.000 Y=0.000 Z=220.000` then, after a
      1 s Delay, `MCP_DOOR_OPENED panel=X=950.000 Y=0.000 Z=620.000` - the panel
      reporting its own position 400 uu higher, so the motion is measured rather
      than claimed, and the guard variable limits it to one opening per run.
      Evidence in reports/session-2026-08-02-mutator-refront.json; screenshot
      `Saved/Screenshots/MCPBridge/phase-f3-door-fit.png`. That entry called
      sound and the HUD unreachable "because there is no tool". Half of that was
      a wrong reading: the audio half needed no tool. Still true: there is no
      Timeline node of any kind.
- [ ] Phase F4 stamina feature
