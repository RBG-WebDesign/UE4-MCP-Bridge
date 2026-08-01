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
- [ ] Phase W complete and verified
- [ ] Phase P findings documented
- [ ] Phase L: serializer fixed
- [ ] Phase L: builder re-fronted
- [ ] Phase F1 material
- [ ] Phase F2 blueprint actor
- [ ] Phase F3 gameplay slice
- [ ] Phase F4 stamina feature
