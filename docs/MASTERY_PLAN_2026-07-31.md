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
- [x] Phase L: unresolved graph connections fail the build (limitation 20
      closed). `blueprint_build` counts the links `MakeLinkTo` actually made
      against the number the spec asked for; any shortfall is an error naming
      every dropped pair, and an error means the asset is not saved.
      `graph.connection_count` is now the number made, with
      `connections_requested` and `unresolved_connections` beside it. Unit test
      in `mcp-server/tests/puerts-tools.test.ts`; live proof on
      `/Game/MCPGenerated/BP_ProbeConn`. It caught a real bug in the F4 graph on
      its first run. `InputKey` was advertised in the same build cycle.
- [x] Phase L: a Blueprint no longer has to be an Actor (limitation 23 closed),
      and a wildcard pin types itself (limitation 26 closed). `blueprint_build`
      keeps only the engine's own parent rule,
      `FKismetEditorUtilities::CanCreateBlueprintOfClass`, and gates the
      actor-only capabilities instead: the `components` array and the
      BeginPlay / Tick / ActorBeginOverlap / ActorEndOverlap / InputKey node
      types are rejected by name, before the asset exists, when the parent is
      not an Actor. Proven live in both directions on
      `/Game/MCPGenerated/BP_ProbeNonActor` (three rejections, `find_assets`
      `count 0` afterwards) and by three assets that were unreachable before:
      `BP_StaminaSave` (SaveGame), `BP_ProbeDataOnly` (UObject, with a graph),
      `BP_ProbeStaminaComp` (ActorComponent). In the same cycle the builder
      started announcing pin changes the way the graph editor does -
      `PinConnectionListChanged` on both ends of every connection,
      `PinDefaultValueChanged` after every pin default - so `Cast` and
      `meta=(DeterminesOutputType)` resolve instead of staying wildcards, plus
      the `AsResult` cast pin role and `scope "target"` on VariableGet and
      VariableSet. Unit test `nonActorParentSuite` in
      `mcp-server/tests/puerts-tools.test.ts`.
- [x] Phase F4 stamina feature - **complete, including the cross-session save
      and load round trip.** `/Game/MCPGenerated/BP_StaminaCharacter`
      (parent Character, one component, a 198-node / 252-connection graph) plus
      `/Game/MCPGenerated/WBP_StaminaHUD` and `/Game/MCPGenerated/BP_StaminaSave`,
      authored from JSON in two passes and converging with zero duplicates on a
      second full run (19 assets before and after, `created false` everywhere).
      Met, with the log line that proves it:
      - sprint raises speed: `maxWalkSpeed=420.0` walking,
        `maxWalkSpeed=900.0` sprinting, read back through
        `MovementComponent.GetMaxSpeed`, and the pawn's own velocity agrees
        (`speed=419.999878` / `speed=900.000061`)
      - stamina drains to zero: `stamina=100.0` -> `91.582626` -> `66.510498`
        -> `41.335423` -> `16.133768` ->
        `MCP_STAM_EMPTY stamina hit zero, sprint force-stopped`
      - regen after the delay: `t=7 stamina=0.0`, `t=8 stamina=15.068813`
      - sprint re-allowed: `MCP_STAM_READY sprint re-allowed at stamina=30.192904`,
        then `t=10 sprinting=true maxWalkSpeed=900.0`
      - HUD tracked the value: `hudPercent` 1.0 / 0.915826 / 0.665105 /
        0.413354 / 0.161338 against the same stamina figures, read back off the
        widget with `GetRenderOpacity` rather than echoed from the variable
      - HUD on screen: `MCP_STAM_HUD created=true in_viewport=true`
      - possession: `MCP_STAM_POSSESS player_controlled=true`
      - animation surface: `MCP_ANIM_SPRINT_START placeholder fired t=2.007949`
        and `MCP_ANIM_SPRINT_STOP placeholder fired t=6.033392` on every state
        edge, from CustomEvents called by the graph itself
      - **save and load of a value, across two PIE sessions.** Session one:
        `MCP_SAVE_PRECHECK slot_exists_at_boot=false`, the engine's own
        `[LogStreaming] Failed to read file '.../Saved/SaveGames/MCPStamina.sav' error.`,
        `MCP_LOAD found=false restored=none`, then
        `MCP_SAVE object_valid=true wrote=true stamina_at_save=16.133768`, and
        `Saved/SaveGames/MCPStamina.sav` (1325 bytes) on disk afterwards.
        Session two, fresh state: `MCP_SAVE_PRECHECK slot_exists_at_boot=true`,
        `MCP_STAM t=1.009965 stamina=100.0` (the variable's own default), then
        `MCP_LOAD found=true restored=16.133768 stamina_now=16.133768`. The
        value crossed a session boundary through a generated SaveGame subclass,
        a typed Cast, and a target-scoped VariableSet and VariableGet.
      Still open, and honestly so:
      - **ProgressBar.SetPercent / TextBlock.SetText.** No Blueprint-reachable
        way to get a named child widget out of a created UUserWidget
        (limitation 25). Target-scoped variable access, added this chunk, is
        one of the three fixes that entry listed, but it does not close 25: the
        generated widget class has no member for its own children to name. The
        HUD is driven through `SetRenderOpacity` on the user widget instead,
        which is a real per-tick drive with a real read-back.
      - **the input path firing.** `InputKey LeftShift`, `K` and `L` build and
        bind, but nothing in the catalog can press a key (limitation 31), so
        the PIE proof drives the same variables from the Tick timeline.
      Also found this chunk: a graph spec is the whole graph and variables are
      additive, so a generated Blueprint cannot be patched and accumulates dead
      members (32); 200 nodes is a real ceiling once a quarter of the graph is
      string composition (33); and moving a Character by writing its capsule's
      RelativeLocation lets it fall through the floor (34). One tracked Unknown:
      `diagnostic` answered `actor_count_total 0` once for a 12-actor level.
      Evidence in `reports/session-2026-08-02-stamina-save.json` and
      `reports/session-2026-08-02-f4-stamina.json`; level back to 12 actors.
