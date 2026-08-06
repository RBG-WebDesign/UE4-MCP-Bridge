# Mock-Only to Confirmed Capability Plan

Date: 2026-08-04

## Objective

Replace the misleading `mock_only` backlog with evidence-backed dispositions while minimizing Unreal Editor launches.

The target is not "run 102 tools once." The target is:

1. Confirm each active canonical capability through the correct evidence contract.
2. Confirm compatibility aliases as schema adapters onto those canonical capabilities.
3. Refront, merge, or retire disabled legacy HTTP registrations instead of proving the obsolete transport.
4. Record evidence in generated metadata so the dashboard cannot become green from assertion alone.

## Current baseline

Generated from `docs/TOOL_INVENTORY.json` and `docs/CAPABILITY_SCOREBOARD.json`:

| Measure | Current value |
|---|---:|
| Total registrations | 308 |
| Canonical capabilities | 210 |
| `mock_only` registrations | 102 |
| Distinct canonical capabilities represented by those registrations | 72 |
| Native compatibility aliases | 55 |
| Disabled legacy HTTP registrations | 46 |
| Server-local registrations | 1 |

The 102 registrations divide by migration action as follows:

| Action | Registrations | Required treatment |
|---|---:|---|
| `ALIAS` | 77 | Prove the canonical target, then prove the adapter contract. |
| `REFRONT` | 14 | Expose existing C++ through the native pipe or merge it into an existing native operation. |
| `PORT` | 8 | Rebuild only capabilities still useful after overlap review. |
| `KEEP` | 2 | Give the capability the correct editor-free or live evidence contract. |
| `MERGE` | 1 | Prove the replacement and remove the duplicate readiness burden. |

## What "confirmed" means

One label is not sufficient for every backend. Use four terminal dispositions.

### 1. `live_verified`

For a tool that touches UE4.27 through the native named pipe. It requires a successful live call plus independent read-back. Mutating tools also require convergence, refusal safety, and cold persistence when they save assets or config.

### 2. `editor_free_verified`

For server-local planners and indexers that never communicate with Unreal. It requires deterministic output, strict schema refusal, stable hashes, and editor-free automated evidence. Add this verification state to the metadata vocabulary instead of pretending these tools need live editor proof.

### 3. `compat_verified`

For `native_pipe_alias` registrations. It means:

- the adapter schema and transformation are directly tested;
- the alias resolves only to the declared canonical native tool;
- representative live input through the alias produces the same observed state as the canonical tool;
- the canonical target itself is `live_verified`.

An alias must not raise the canonical live-verified count. It is compatibility evidence, not a new capability.

### 4. `replaced` or `retired`

For disabled legacy HTTP registrations whose behavior is fully covered by a native canonical capability, or whose product value no longer justifies migration. The replacement must be named and proven. The legacy registration stays disabled and is excluded from active readiness.

## Evidence contracts

### Read-only native capability

Required evidence:

1. Call the inspector twice against unchanged state.
2. Require identical canonical structure hashes and ordered output.
3. Require package dirty state to remain unchanged.
4. Require relevant file hashes and modification times to remain unchanged.
5. Restart UE4 once and require the same result during the cold pass.
6. Exercise one invalid selector and require a structured refusal.

### Asset or project-config mutation

Required evidence:

1. Inspect the initial state.
2. Run `plan_only` when supported and require zero mutation.
3. Apply the desired state once.
4. Require clean compile or the tool-specific success result.
5. Inspect with a separate read tool and compare against the request.
6. Apply the identical request again and require no changes and no save.
7. Send one invalid request and require refusal with no residue.
8. For an atomic tool, inject one controlled mid-batch failure and verify rollback by independent pre/post hashes.
9. Restart UE4 and repeat only the independent read.

### Scene mutation

Required evidence:

1. Inspect the loaded level and PlayerStarts.
2. Apply one `puerts_scene_batch` fixture.
3. Inspect the level independently.
4. Focus or fit affected actors and capture a viewport screenshot.
5. Rerun the same batch and require convergence.
6. Cold-read the saved level only if the test deliberately saved it.

### PIE capability

Required evidence:

1. Author or load one dedicated test-driver fixture separate from gameplay assets.
2. Start PIE once with explicit user authorization.
3. Execute all related control operations sequentially.
4. Query a measurable runtime state after each operation family.
5. Test one safe refusal while PIE is running.
6. Stop PIE in `finally`.
7. Require the editor to return to a healthy diagnostic state.

### Compatibility alias

Required evidence:

1. Default catalog does not expose the alias unless `MCP_COMPAT_ALIASES=1`.
2. Alias annotations are no weaker than the canonical target.
3. Input transformation is covered by a focused editor-free contract.
4. Invalid or unsupported legacy fields are refused, not silently dropped.
5. A live call through the alias and a canonical call converge on the same independently inspected state.

### Server-local capability

Required evidence:

1. Run twice without Unreal and require byte-identical output and plan hash.
2. Reject unknown fields and unsafe paths.
3. Confirm no editor client or transport object is constructed.
4. Record an editor-free evidence artifact.

## Phase 0: Fix the accounting before testing

### Deliverables

Add a generated `docs/MOCK_PROMOTION_MATRIX.json` containing one row per `mock_only` registration:

```json
{
  "name": "actor_spawn",
  "backend": "native_pipe_alias",
  "canonical_capability": "actors.spawn",
  "migration_action": "ALIAS",
  "replacement_tool": "puerts_spawn_actor",
  "replacement_verification": "live_partial",
  "disposition": "finish_canonical_then_verify_alias",
  "evidence_class": "scene_mutation",
  "fixture": "compat_scene",
  "acceptance_script": "Scripts/mock-promotion-suite.mjs"
}
```

The generator must fail when:

- an active mock registration has no disposition;
- an alias names a missing replacement;
- an alias is classified stronger than its target;
- a `live_verified`, `compat_verified`, or `editor_free_verified` row names a missing evidence file;
- two registrations claim different canonical capability identities for the same adapter path without an explicit note.

### Metadata changes

Extend `docs/TOOL_CAPABILITY_METADATA.json` verification vocabulary with:

- `editor_free_verified`;
- `compat_verified`;
- `replaced`.

Keep `retired` as lifecycle or migration disposition. Do not count `replaced` or retired legacy registrations as unfinished active product capabilities.

### Dashboard changes

Display three separate counts:

1. Active canonical capabilities by proof state.
2. Compatibility registrations by adapter proof state.
3. Disabled legacy registrations by migration disposition.

Do not combine these into one completion percentage.

## Phase 1: Close editor-free evidence first

This phase requires no Unreal launch.

### Blueprint production planner

The single server-local mock registration is `blueprint_production_plan`.

Required work:

1. Finish the five-file planner, fixture, runner, and focused-test reconciliation documented in `docs/CLAUDE_FABLE_OPUS_HANDOFF.md`.
2. Require all generated builder connections to use `node.pin` form.
3. Require all node routing keys and pin defaults to live inside `params`.
4. Require variable descriptors to lower to the actual builder schema.
5. Separate warm executability from cold proof availability.
6. Run the planner suite twice and compare complete JSON and `plan_hash_sha1`.
7. Add malformed schemas, unsafe paths, dangling connections, dependency cycles, and unsupported capability refusals.
8. Promote the planner to `editor_free_verified`, not `live_verified`.

### Alias adapter contracts

Run every native alias through the compatibility handler without Unreal:

- parse strict input;
- capture the exact canonical tool name and transformed arguments;
- compare annotations;
- require unsupported fields to fail;
- require no adapter to use legacy HTTP.

This closes the code contract but does not yet mark aliases `compat_verified`. Live representative evidence comes later.

### Required gate

```powershell
npx tsx mcp-server/tests/blueprint-production.test.ts
npx tsx mcp-server/tests/compat-tools.test.ts
npm run verify
```

## Phase 2: Normalize duplicates before writing new code

The shortest path is deletion and merging, not porting every old name.

### Blueprint legacy operations

These legacy registrations already map to the stronger native batch tools:

- add, remove, or change variables;
- add or remove functions;
- add or remove interfaces;
- add or remove event dispatchers;
- component remove and rename;
- node add, delete, and move;
- pin connect;
- build from JSON.

Disposition:

- canonicalize member operations under `puerts_blueprint_member_patch`;
- canonicalize graph operations under `puerts_blueprint_graph_patch`;
- canonicalize desired-state builds under `puerts_blueprint_build`;
- mark disabled legacy registrations `replaced` once the native target and compatibility adapter have evidence.

Do not live-test the disabled HTTP route.

### Blueprint enabled state and break-pin operations

`blueprint_node_set_enabled` and `blueprint_pins_break` were recently implemented as operations on the native graph patch path.

Disposition:

1. Confirm the operations are registered in the native schema, runtime registry, allowlist, annotations, and inspector read-back.
2. Add them to the Blueprint warm and cold acceptance fixture.
3. Change their migration action from `REFRONT` to `MERGE` or `ALIAS` onto `puerts_blueprint_graph_patch`.

Do not create two additional top-level tools.

### PIE agent legacy operations

The seven refront entries for look-at, move-to, press, record start, record stop, replay, and telemetry should converge on:

- `puerts_pie_agent_control` for mutations;
- `puerts_pie_agent_query` for observation.

Disposition:

1. Verify every operation enum is present in the canonical schemas and runtime registry.
2. Use one PIE fixture and one PIE session to exercise all operations.
3. Mark the legacy registrations replaced after the canonical operation set passes.

Do not publish seven more native top-level commands.

### Material instance legacy tools

Merge:

- `material_instance_create`;
- `material_instance_set_params`.

Replacement: `puerts_material_instance_build` plus `puerts_material_inspect`.

Prove scalar, vector, texture, and static-switch reconciliation in one asset fixture. Mark both legacy registrations replaced.

### Audio component add

First determine whether `puerts_blueprint_build` can add and configure an `AudioComponent` with a Sound asset reference. If yes, document the composition recipe and mark `audio_component_add` replaced. Add a new native capability only if the desired-state Blueprint builder cannot express the operation generically.

### Game template creation

Do not port `game_template_create` as an editor primitive. A game template is a recipe composed from Blueprint, input, project settings, and scene desired-state tools. Fold it into `blueprint_production_plan` or a server-local recipe planner after the physics pickup capstone passes.

### Gameplay acceptance runner

Retire `gameplay_run_acceptance_tests` as an editor tool. Repository acceptance scripts already provide safer ownership, source control, evidence files, and explicit PIE authorization. A tool that runs an open-ended test library inside the editor adds no primitive.

## Phase 3: Finish canonical native prerequisites

Before alias proof, finish the canonical targets currently below `live_verified`.

### Group A: currently `live_partial`

Targets affecting seven mock aliases:

- `puerts_spawn_actor`;
- `puerts_delete_actor`;
- `puerts_pie_start`;
- `puerts_pie_stop`;
- `puerts_get_logs`.

Required work:

- complete independent state verification;
- exercise refusal behavior;
- ensure start and stop are measured through diagnostic state;
- record one consolidated evidence artifact.

### Group B: currently `implemented`

Targets affecting 29 mock aliases include:

- scene batch;
- navigation build;
- Blackboard build;
- camera shake;
- cloth inspection;
- folder visibility;
- input mapping read and patch;
- level create and save;
- PIE agent control and query.

Use one fixture per domain, not one fixture per alias. Each mutator must have an independent inspector or an equivalent canonical state query before promotion.

### Group C: currently `implemented_unverified`

`puerts_project_settings_maps` blocks one alias.

Required proof:

1. Snapshot current map settings.
2. Run `plan_only`.
3. Apply a safe temporary generated-map setting.
4. Read config independently.
5. Restore the exact snapshot in `finally`.
6. Confirm the restored config hash.

## Phase 4: Refront the AnimPose family

Five legacy registrations point at reusable `AnimPoseLibrary` C++:

- `anim_pose_snapshot`;
- `anim_pose_delta`;
- `anim_root_motion_analyze`;
- `anim_reanchor`;
- `anim_batch_reanchor`.

### Design decision

Prefer one canonical native tool with an `op` discriminator if all five operations share the same asset, skeleton, pose, and result contract. Use separate tools only where permissions, timeouts, or destructive behavior differ materially.

### Native implementation sequence

1. Confirm each UE4.27 C++ entry point and include path.
2. Define exact JSON schemas in `mcp-server/src/tools/puerts.ts`.
3. Add TypeScript execution functions and registry entries in `puerts-runtime/src/registry.ts`.
4. Add the minimum bridge service wrapper and narrow allowlist entries.
5. Add PuerTS typings.
6. Classify read operations separately from mutations in `annotations.ts`.
7. Add an independent pose or root-motion inspector shape.
8. Run editor-free contracts.
9. Compile once through `install:sync`.
10. Run one warm campaign against a small generated animation fixture.
11. Rerun for convergence.
12. Restart and perform a cold read.

If reanchor changes source animation assets destructively, use duplicated generated fixtures and a pre-mutation source-control checkpoint.

## Phase 5: Consolidate project intelligence ports

Four mock project-intelligence registrations are related:

- `project_index_rebuild`;
- `project_index_query`;
- `project_semantic_diff`;
- `gameplay_pattern_search`.

Build one server-local index rather than four unrelated editor tools.

### Scope

Index only repository and project files that are safe to parse without Unreal:

- C++ declarations and definitions;
- Config files;
- generated tool inventory and metadata;
- asset registry exports or native inspection snapshots supplied as input;
- playbooks and capability findings.

Do not parse `.uasset` bytes or communicate with Unreal from the server-local index.

### Operations

- `rebuild`: create a deterministic index with content hashes;
- `query`: exact, prefix, symbol, and bounded text search;
- `semantic_diff`: compare two saved canonical snapshots;
- `pattern_search`: match documented gameplay patterns and playbooks.

### Evidence

- deterministic rebuild hash;
- no-op second rebuild;
- strict root containment;
- stale-file removal;
- query ordering;
- malformed index recovery;
- editor-free performance budget;
- no editor or network dependency.

Promote this family to `editor_free_verified`.

## Phase 6: One consolidated live campaign

Do not launch Unreal for every tool.

### Build and installation

1. Freeze repository code for the campaign.
2. Run `npm run verify` once.
3. Close UE4.
4. Run one `install:sync` against `D:\Unreal Projects\BridgeInstallTest`.
5. Reopen UE4.27 once.
6. Run `install:check`, project-version verification, and `puerts_diagnostic`.
7. Save a recovery point.

### Warm campaign fixtures

Use eight bounded fixtures:

| Fixture | Capability families |
|---|---|
| `compat_scene` | actor spawn/delete, property adapter, scene batch, asset find/save, level actors, viewport screenshot |
| `compat_blueprint` | Blueprint build, member patch operations, graph patch operations, widget build |
| `compat_ai` | Blackboard, Behavior Tree, navigation |
| `compat_input_project` | input mappings, folder visibility, project map settings with restoration |
| `compat_level` | level create/save with generated package paths |
| `compat_material_audio` | material instance reconciliation and AudioComponent composition |
| `compat_pie` | PIE start/stop, input, movement, look, observe, record, replay, logs |
| `anim_pose` | pose snapshot, delta, root motion, reanchor operations |

### Alias server mode

Start the MCP server with `MCP_COMPAT_ALIASES=1`. Keep legacy HTTP disabled.

For each alias:

1. Call the alias once against its family fixture.
2. Inspect with the canonical read tool.
3. Call the canonical desired state if needed.
4. Require convergence on the same structure hash.
5. Record alias name, transformed canonical request, response, and independent observation.

All editor calls remain sequential.

### Failure behavior

- Stop dependent operations after the first failure in a fixture.
- Continue only fixtures whose state is independent and known clean.
- Record at most three likely causes.
- Add one diagnostic that distinguishes them.
- Repair the smallest measured defect.
- Do not rerun the whole library after each fix. Rerun the failed fixture, then run one final consolidated pass.

## Phase 7: One cold campaign

After the warm campaign succeeds:

1. Save verified generated assets.
2. Close UE4 normally.
3. Reopen the same UE4.27 project once.
4. Verify the session identity and install parity.
5. Run inspectors only.
6. Compare structure hashes and relevant package hashes against the warm evidence.
7. Do not mutate during cold proof.

PIE control aliases do not require cold execution. Their authored driver assets do require cold inspection if saved.

## Acceptance runner

Create `Scripts/mock-promotion-suite.mjs` by reusing `Scripts/slice-harness.mjs`.

Required options:

```text
--phase=warm|cold
--families=scene,blueprint,ai,input,level,material_audio,pie,anim_pose
--compat-aliases
--execute
```

Safety requirements:

- refuse without `MCP_UNREAL_PROJECT_ROOT`;
- require `MCP_MOCK_PROMOTION_LIVE=1` and `--execute` for mutations;
- call `install:check` before connecting;
- verify UE4.27;
- record exact session ID and project path;
- never enable legacy HTTP;
- stop a fixture after its first failed dependency;
- stop PIE in `finally`;
- always write the evidence artifact, including failures;
- never update metadata automatically from a failed or partial run.

Evidence layout:

```text
docs/evidence/mock-promotion/2026-08-04/
  manifest.json
  editor-free.json
  warm-scene.json
  warm-blueprint.json
  warm-ai.json
  warm-input-project.json
  warm-level.json
  warm-material-audio.json
  warm-pie.json
  warm-anim-pose.json
  cold.json
  summary.json
```

## Promotion workflow

Metadata promotion is a separate reviewed change after evidence exists.

For each row:

1. Confirm the evidence file exists.
2. Confirm its project path, session ID, UE version, install hash, and result.
3. Confirm an independent inspector supplied the observed truth.
4. Confirm warm and cold requirements for that evidence class.
5. Update only `docs/TOOL_CAPABILITY_METADATA.json`.
6. Regenerate inventory, scoreboard, preservation audit, skill catalog, and dashboard inputs once.
7. Run `npm run check:inventory` and `npm run verify`.
8. Review the generated diff for unsupported green states.

Never hand-edit `TOOL_INVENTORY.json` or `CAPABILITY_SCOREBOARD.json`.

## Recommended execution order

1. Accounting schema and promotion matrix.
2. Blueprint planner editor-free closure.
3. Compatibility adapter editor-free contracts.
4. Duplicate merge and retirement metadata.
5. Blueprint graph operations in the existing capstone.
6. Canonical scene, input, level, AI, material, audio, and PIE prerequisites.
7. AnimPose native refront.
8. Project-intelligence server-local consolidation.
9. One build and install.
10. One warm campaign.
11. Targeted repairs only.
12. One final warm rerun.
13. One cold restart and read-only proof.
14. Metadata promotion and dashboard regeneration.

## Definition of done

The campaign is complete when:

- no active `native_pipe`, `native_pipe_alias`, or `server_local` registration remains `mock_only`;
- every active canonical capability is `live_verified` or `editor_free_verified` as appropriate;
- every supported alias is `compat_verified` and points to a confirmed canonical target;
- every disabled legacy mock registration is explicitly `replaced`, `retired`, or still blocked with a named reason;
- no live evidence uses HTTP or another fallback transport;
- every mutator has independent read-back;
- saved asset/config mutations have warm, convergence, refusal, rollback where applicable, and cold evidence;
- the full TypeScript gate, UBT install, live smoke, warm campaign, and cold campaign pass;
- generated counts reconcile without manual edits;
- the dashboard distinguishes canonical readiness from compatibility and legacy registration counts.

## Expected result

This plan does not create 102 new tools and does not require 102 editor launches.

Expected closure path:

- 55 native aliases receive compatibility evidence in family batches;
- 46 legacy registrations are merged, refronted, or retired without enabling HTTP;
- one server-local planner receives editor-free evidence;
- 14 refront items collapse into the existing Blueprint and PIE operation families plus one AnimPose family;
- eight port items mostly merge into material, audio, recipe, and project-intelligence capabilities rather than becoming eight more public tools.

The result is a smaller, more truthful product surface with stronger proof.
