# Continue here

Updated 2026-08-03, end of the planning session.

**READ docs/FINAL_IMPLEMENTATION_PLAN.md FIRST.** It supersedes the wave notes
below, carries every remaining work package with dependencies and merge order,
and `docs/WORKHORSE_EXECUTION_PROMPT.md` is the self-contained prompt that
executes it.

## State at session end

- ALL lanes merged: X, Y and Z are ancestors of HEAD. Every worktree clean,
  every branch merged, no editor running, integration tree clean.
- The batch compile PASSED: fresh DLLs, install matches the repository,
  verify green at 273 registrations / 60 native tools.
- UI slice PASS 20/0 and AI slice PASS 22/0, both run live. Gameplay 16/1.
- UNCOMPILED: nothing. UNRUN despite compiling: the hidden-pin hash fix
  (finding 0z), anim_blueprint_patch, sequence_render_start, the job API,
  materials and level slices as rewritten, and 30 implemented tools that have
  never been run live (RB-6).

## Current offline construction update

- RL-2 project-specific default pipes are implemented, package-tested and linked.
  Two-project live collision proof remains.
- FP-2 exposes all nine existing UPIEAgentLibrary control operations through
  `puerts_pie_agent_control`. TypeScript, focused tests, UBT, final linking and
  install:check pass. User-authorized live proof remains.
- FP-3 exposes confirmed, reference-aware `/Game` asset deletion through
  `puerts_delete_asset`. Its editor-free safety contract, UBT, final linking and
  install:check pass. Destructive live create-delete-verify proof remains.
- FP-4 extends `blueprint_build.remove_unlisted` to MCPManaged components.
  Focused contract tests, UE4.27 compilation, final linking and install:check
  pass. Warm and cold live convergence proof remains user-gated.
- FP-5 exposes native puerts_level_create, puerts_level_load and
  puerts_level_save, with legacy level_new and level_save aliases.
  Parameter-parity tests, UHT, UBT, final linking and install:check pass.
  Scripts/level-lifecycle-acceptance.mjs provides warm and cold proof but has
  not been run because editor lifecycle actions remain user-gated.
- FP-6 exposes native puerts_audio_build for desired-state Sound Cue graphs.
  It supports seven UE4.27 node types, stable ids, ordered child links, Sound
  Wave references and reflected editable properties. Focused contracts, UHT,
  UBT, library creation, DLL linking, generated inventory and install:check
  pass. Scripts/audio-build-acceptance.mjs provides warm and cold proof but has
  not been run because editor lifecycle actions remain user-gated. PIE playback
  remains a separate explicit authorization.
- Do not launch, close, or otherwise manage Unreal Editor until the user
  explicitly asks. Do not start PIE without a separate explicit request.

## Stabilization checkpoint, 2026-08-03

- Checkpoint branch: `checkpoint/stabilized-2026-08-03`.
- Checkpoint commit: `a624adf5ea9177e2daae0b3ef6a2da8464e4e6dc`.
- Base preservation commit: `014bd14` (`Checkpoint offline FP integration and audio build WIP`).
- The competing Codex source thread is idle and explicitly blocked from further
  repository writes. This integration thread is the sole writer.
- Unreal Editor was not launched, closed, inspected or otherwise managed during
  stabilization. No install sync was run by the integrator.

Files preserved by `014bd14`:

- `Plugins/MCPBridge/Source/MCPBridgePuerTS/MCPBridgePuerTS.Build.cs`
- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeAudio.cpp`
- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeAudioBuild.cpp`
- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeBlueprint.cpp`
- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeEditorState.cpp`
- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeService.cpp`
- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Public/MCPPuerTSBridgeService.h`
- `Scripts/audio-build-acceptance.mjs`
- `Scripts/bp-member-patch-acceptance.mjs`
- `Scripts/bp-remove-unlisted-acceptance.mjs`
- `Scripts/level-lifecycle-acceptance.mjs`
- `docs/CAPABILITY_FINDINGS.md`
- `docs/CAPABILITY_PRESERVATION_AUDIT.md`
- `docs/CAPABILITY_SCOREBOARD.json`
- `docs/CONTINUE_HERE.md`
- `docs/FINAL_IMPLEMENTATION_PLAN.json`
- `docs/FINAL_IMPLEMENTATION_PLAN.md`
- `docs/PROJECT_FINISH_SCOREBOARD.json`
- `docs/REFRONT_MAP.md`
- `docs/RELEASE.md`
- `docs/SUBAGENT_INTEGRATION_LOG.md`
- `docs/TOOL_CAPABILITY_METADATA.json`
- `docs/TOOL_INVENTORY.json`
- `mcp-server/src/annotations.ts`
- `mcp-server/src/tools/compat.ts`
- `mcp-server/src/tools/puerts.ts`
- `mcp-server/tests/compat-tools.test.ts`
- `mcp-server/tests/puerts-tools.test.ts`
- `puerts-runtime/src/registry.ts`
- `puerts-runtime/src/types.ts`
- `puerts-runtime/types/puerts-bootstrap.d.ts`
- `puerts-runtime/typing/ue/ue.d.ts`
- `skills/unreal-engine-4-27/references/tool-catalog.md`

`a624adf` additionally preserves:

- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeAudioBuild.cpp`
  with final FP-6 trust-boundary validation for FName-colliding ids, Sound Wave
  roots and per-child array arity.
- `docs/CONTINUE_HERE.md` with the source task's final handoff.

### Compile and test status

- The source task reported UE4.27 UHT, UBT, library creation, DLL linking and
  install parity green after the final FP-6 validation change. This integrator
  did not repeat the native build during stabilization.
- `npm run verify` passed on 2026-08-03: both TypeScript projects built, all
  editor-free suites passed, inventory matched at 280 catalog tools, MCP smoke
  exposed 69 tools, and smoke finished 8 passed, 0 failed, 4 skipped.
- `npm run test:editor-free` was reported green by the source task.
- The four smoke skips require an engine root or live editor session. They are
  not editor-free failures.
- No editor-free test is currently red.
- The last recorded live gameplay slice remains 16 passed, 1 failed. It was not
  rerun during stabilization.

### Editor-dependent verification still required

- Batch-run all seven slices against one clean installed target. Materials,
  level, animation and cinematics still lack a post-Wave-A merged live run.
- Rerun gameplay to verify the compiled hidden-pin hash fix.
- Prove RL-2 with two distinct UE4.27 projects using derived default pipes.
- Run FP-2 PIE-agent behavior only after separate explicit PIE authorization.
- Run FP-3 asset deletion with fresh positive and referenced controls.
- Run FP-4 Blueprint component convergence warm and cold.
- Run FP-5 level lifecycle warm and cold.
- Run FP-6 Sound Cue build and inspect warm and cold. Playback remains a
  separate PIE gate.

Resume construction from `docs/FINAL_IMPLEMENTATION_PLAN.json`. A red live
capability blocks only its promotion, not unrelated editor-free construction.

## Exact next commands

After the user explicitly authorizes editor lifecycle actions, launch with
Scripts/start-ue4-project.ps1 -Confirm:$false against
D:/Unreal Projects/BridgeInstallTest, wait for session.json, then:

    npm run slice:ui ; npm run slice:ai ; npm run slice:gameplay
    npm run slice:materials ; npm run slice:level
    npm run slice:animation ; npm run slice:cinematics

Then RB-2's last gameplay red, then the RB-6 promotion sweep, per
docs/FINAL_IMPLEMENTATION_PLAN.md.

### FP-3 live acceptance, explicit authorization required

1. Run `install:check` before the live test.
2. Choose a fresh `/Game/MCPTests/AssetDelete_<run-id>` path and prove it is
   absent before creating anything there.
3. Create a disposable asset as the positive control and prove that it exists.
4. Call `puerts_delete_asset` with `confirm: true` and `force: false`; verify
   both Asset Registry and package-file absence from the returned result.
5. Call the same delete again; it must converge with `already_absent: true`.
6. Create a second disposable asset with a real referencer. Safe deletion must
   fail without moving state. Test `force: true` only with separate destructive
   authorization because it can clear references.
7. Run `install:check` after the live test. Repeat after a user-managed editor
   restart for cold proof. Do not start PIE.

### FP-4 live acceptance, explicit authorization required

1. Run `install:check`, then set `MCP_UNREAL_PROJECT_ROOT` to BridgeInstallTest.
2. Run `node Scripts/bp-remove-unlisted-acceptance.mjs --phase=record`.
   Its component fixture asserts a fresh path, creates three components as a
   state-moving control, proves plan-only keeps the member hash stable, removes
   exactly one unlisted managed component, inspects the result independently,
   proves the rerun removes nothing, blocks a retained-child removal, and
   induces a later graph failure to verify the component and member hash roll back.
3. After a user-managed editor restart, run
   `node Scripts/bp-remove-unlisted-acceptance.mjs --phase=cold`.
4. Run `install:check` again. Do not start PIE.

### FP-5 live acceptance, explicit authorization required

1. Start with a clean BridgeInstallTest editor and set
   MCP_UNREAL_PROJECT_ROOT to that project.
2. Run node Scripts/level-lifecycle-acceptance.mjs.
   It checks the install before and after, asserts three fresh fixture paths,
   creates and saves a map, moves state with a marker actor, proves a dirty-map
   load refusal leaves state intact, exercises template_path and save_all,
   and verifies with scene_inspect plus package hashes.
3. After a user-managed editor restart, run
   node Scripts/level-lifecycle-acceptance.mjs --phase=cold.
4. Do not start PIE. The harness creates fresh maps and does not delete them.

### FP-6 live acceptance, explicit authorization required

1. Start with a clean BridgeInstallTest editor and set
   MCP_UNREAL_PROJECT_ROOT to that project.
2. Run node Scripts/audio-build-acceptance.mjs.
   It checks install parity before and after, asserts a fresh fixture path,
   creates a one-node Sound Cue as a state-moving control, plans and applies a
   two-node update, verifies it with audio_inspect and file hashes, proves a
   convergent rerun does not rewrite the package, and proves an invalid graph
   leaves both the inspector hash and saved bytes unchanged.
3. After a user-managed editor restart, run
   node Scripts/audio-build-acceptance.mjs --phase=cold.
4. Playback remains pending. A human must explicitly authorize PIE, then place
   the saved cue on an Audio Component and verify playing state and completion.
   Audible output is not machine-provable. The harness itself never starts PIE.

## Paste this into a fresh session

This used to duplicate an execution prompt inline. That copy drifted out of
sync with reality (it described wave five's merge as still pending after wave
A had already merged and compiled) and gave a fresh session wrong first
actions. There is now exactly one paste-able executor prompt, kept current in
one place: **`docs/WORKHORSE_EXECUTION_PROMPT.md`**. Paste that file's content
below its `---` line into a fresh session. It already states the current
state (wave A merged, three of seven slices run) and the correct first
action (relaunch, run all seven slices, then RB-2 and RB-6).

## Merge lessons, do not rediscover these

**Never line-merge `registry.ts`, `mcp-server/src/tools/puerts.ts`, or
`MCPPuerTSBridgeService.h`.** Git factors out the common suffix of two
conflicting hunks, so both sides end mid-declaration and share one tail. A union
closes one and leaves the other open, and it surfaces as an unbalanced brace or
a UHT "Missing '*' in Expected a pointer type" hundreds of lines away.

Graft whole brace-matched units from the lane's own file instead. That recipe is
now `Scripts/merge-lane.mjs`, and its header explains the failure it prevents.

**Also learned:**

- Never dedupe by trimmed line content when unioning code. It eats repeated
  closing braces and corrupts silently.
- The tool-count assertions in `puerts-tools.test.ts` and `compat-tools.test.ts`
  are shared scalars every lane moves. Take the real number from a run.
- `TOOL_INVENTORY.json`, `CAPABILITY_SCOREBOARD.json` and
  `CAPABILITY_PRESERVATION_AUDIT.md` are GENERATED. Regenerate, never merge.
- `bUseUnity = false` is set on MCPBridgePuerTS deliberately. One command per
  .cpp with its own anonymous-namespace helpers collides under a unity build,
  and the collision scales with the number of commands. Do not turn it back on
  without renaming every file-local helper.

## Open defects, highest value first

1. **Capability regression** (lane P): `scene_batch`'s inputs shipped in the
   legacy catalog and did not survive the native migration.
2. **Finding 0q**: `blueprint_member_patch` leaves the Blueprint compiling with
   warnings where a fresh one does not. Lane R is on it.
4. **Compile messages are unreachable.** A caller told `UpToDateWithWarnings`
   cannot find out what the warnings were through any command.
5. **Packaging: source YES, binaries NO.** Lane N ran `-RunUAT` and proved it
   cannot work in 4.27 with an engine citation. The zip installs and compiles;
   it has never been proven to LOAD.
6. **RL-2 is implemented, editor-free proven and linked.** An
   unconfigured zip install now derives its pipe from project name and canonical
   path; an ini override still wins. `npm run test:package` and `install:check` pass. The
   two-project session proof remains.

## Editor-free construction wave, 2026-08-03

Integration HEAD after this wave is `93f1617` on
`bridge/native-consolidation-2026-07-31`.

Merged work:

- `lane/b4-port-batch1` at `20e3724`, merged by `f1b370e`: re-fronted
  `blueprint_info`, `viewport_fit` and `viewport_focus` onto existing native
  graph inspection and viewport capture. Unsupported legacy distance and
  padding semantics refuse loudly.
- Generated inventory and scoreboards were refreshed once in `4480e46`.
  Current measured catalog: 283 tools, including 66 native pipe commands,
  44 native pipe aliases and 69 exposed native/server-local tools.
- `lane/d-ci-release` at `f36902b`, merged by `ceee482`: CI now covers bridge
  and lane branches, runs the canonical editor-free suite, and proves package
  refusal and artifact checks without attempting unsupported UE4.27 binary
  plugin packaging. A hosted Actions run is still unverified.
- `lane/c-feature-library-7-12` at `adb056d`, merged by `93f1617`: authored
  weapons/projectiles and HUD acceptance harnesses with warm/cold phases,
  state-moving controls, independent inspectors, convergence checks and
  optional PIE behind `--pie`. They are implemented_unverified.

`npm run verify` passed immediately before and after every merge. The final
run built both TypeScript projects, passed all editor-free suites, matched the
283-tool inventory, and finished smoke at 8 passed, 0 failed, 4 expected
skips. No Unreal build, install sync, editor action, live capability call or
PIE session occurred in this wave.

The feature lane's first freshness claim was rejected because `find_assets`
was not the required independent reader. Commit `adb056d` repaired the shared
harness with an expected-refusal control and placed `graph_inspect` or
`widget_inspect` before every fixture build. The repaired construction was
accepted, but no evidence state was promoted.

Next editor-free construction priorities are feature acceptance harnesses
9 through 12, a second small legacy re-fronting batch, and autonomous
inspect-build-verify-repair orchestration. Continue to batch all live proof.

Ponytail finding: `feature-hud.mjs` and `feature-weapons-projectiles.mjs`
duplicate the same fixture search. Fold search plus independent reader refusal
into one existing harness method before feature harnesses 9 through 12.

## Editor-free completion wave, 2026-08-03

Integration HEAD is `44a8dc5` on
`bridge/native-consolidation-2026-07-31`.

Merged work:

- `lane/e2-port-batch2` at `5d2acd3`, merged by `3f43193`: added
  `widget_build_from_json`, `behavior_tree_create` and `blackboard_create`
  compatibility aliases. Empty Behavior Trees refuse loudly. Unsafe
  `blueprint_list`, `asset_info` and `level_outliner` aliases were rejected
  because native return shapes do not preserve their legacy contracts.
- Inventory regenerated from built output in `1a61f1b`: 286 catalog tools,
  47 native aliases, 66 native commands and 69 exposed native/server-local
  tools.
- `lane/e2-feature-library-9-12` at `d244adb`, merged by `e82dd24`: authored
  menus, controller navigation, dialogue and AI patrol/chase/search/perception
  acceptances. The earlier Ponytail finding is closed by one shared fixture
  freshness helper. Every control and full-state writer must succeed before
  independent inspection. All four remain implemented_unverified.
- `lane/ao2-orchestrator` at `135ffd2`, merged by `44a8dc5`: AO-2 now runs
  member patch, scene batch, material and optional runtime observation stages
  through the existing sequential, resumable orchestrator. Runtime state is
  never resumed and PIE remains behind `--pie`.

Final `npm run verify` is green: both TypeScript projects build, all unit and
performance suites pass, orchestrator 33/0, inventory 286/286, and smoke 8
passed, 0 failed, 4 expected skips. One intermediate run hit the fixed mock
port `18772` while an isolated lane verify was active; an uncontended rerun
passed without process management. No editor action, install sync, native
compile, live bridge call or PIE occurred.

Outstanding editor-dependent gates:

- Run feature acceptances 7 through 12 warm and cold; controller focus
  traversal may expose a missing primitive.
- Run AO-2 warm and cold for member, scene and material stages.
- AO-2 runtime observation is implemented_partial: the native PIE service
  allowlist at `MCPPuerTSBridgeService.cpp:675` still refuses
  `pie_agent_query` while PIE is active.
- All previously recorded slice and FP live gates remain batched.

Ponytail review: Lean already. Ship. The prior duplicated fixture search was
removed; no further capability-preserving deletion was found in this wave.

## Editor-free completion wave 3, 2026-08-03

Integration HEAD is `94cf563` on
`bridge/native-consolidation-2026-07-31`.

Merged work:

- `lane/fp1-pie-query-allowlist` at `e1099b4`, merged by `43f4431`:
  `pie_agent_query` is now the fourth and only new command allowed during PIE.
  Authentication, session checks and the mutating `pie_agent_control` block are
  unchanged. UE4.27 UHT/UBT/link and user-authorized live PIE proof remain.
- `lane/e2-port-batch3` at `5895ecc`, merged by `36c736a`: re-fronted
  `anim_blueprint_build_from_json` and the shape-compatible half of
  `blueprint_inspect`. Macro and node-detail actions refuse loudly.
- Inventory regenerated from built output in `566649e`: 288 catalog tools,
  49 native aliases, 66 native commands and 69 exposed native/server-local
  tools.
- `lane/e-feature-library-13-18` at `bc1a3c6`, merged by `94cf563`: authored
  locomotion AnimBP, montage/notifies, material effects, level interaction,
  Sequencer events and packaged-demo harnesses. Every dependent writer now
  has an explicit success guard before inspection.

Final `npm run verify` is green: both TypeScript projects build, all suites
pass, orchestrator 33/0, inventory 288/288, and smoke 8 passed, 0 failed, 4
expected skips. No integrator editor action, install sync, native compile,
live bridge call or PIE occurred.

An external writer changed all seven tracked slice evidence files during the
wave. Raw evidence was preserved without promotion in `e0e13b8`: UI 20/0, AI
22/0, gameplay 17/0, materials 17/2, level 14/2, then animation and cinematics
UNPROVEN_NO_EDITOR. Materials reported shader_map_valid false and saved false;
level save and viewport capture timed out. The batch was not initiated or
supervised by this integrator, so none of its green results promote evidence.

Precise remaining blockers added by features 13 through 18:

- Montage mutation remains read-only until FAnimLinkableElement relinking and
  next-section chains are failure-atomic.
- Sequencer Event tracks need atomic director Blueprint endpoint authoring.
- Packaged demo needs native project-settings routing and an async cook,
  stage and package job.
## Code-first wave stopped

Unreal and live editor tests stayed off. The TypeScript build passed; the
focused PuerTS contract was blocked by named-pipe EPERM in the sandbox. The
dirty tree now includes montage and sequence-event fronts, the DataTable native
front, AnimPose validation, scene aliases, shared metadata, generated inventory,
and focused contract edits.

Remaining repository-side items and owners:

- AnimPose public native fronts for snapshot, delta, root-motion analysis,
  reanchor, and batch reanchor: root shared service, PuerTS registry/types,
  MCP schemas, annotations, and the animation domain.
- Wave 1 native contract repairs and partial legacy-facing fronts:
  root shared service plus the listed Blueprint, editor-state, cloth,
  inspect, folder, input, asset, audio, camera, material, and post-process
  domain files from docs/CODE_FIRST_100_PLAN.md.
- DataTable/gameplay-pattern and project/intelligence native or server-local
  integration: project-index helpers, content/gameplay/project/intelligence
  wrappers, and focused fixtures.
- UI/title, scene, material, gameplay, and viewport strict adapters:
  title/promptbrush, actor/level, material/effects, gameplay, viewport,
  operations, cloth, and compat fronts.
- Final shared reconciliation: service allowlist, registry/types, MCP
  schemas, annotations, generated metadata/catalogs, and plan checkmarks.

Review-only artifacts kept:

- .codex_patches/wave_remaining_root.diff montage notify-count fix.
- .codex_patches/wave_remaining_ui_scene_v3.diff strict actor schemas.

Rejected artifacts:

- .codex_patches/wave_remaining_anim.diff: converged detection misses
  ordinary non-identity deltas.
- .codex_patches/wave_remaining_data_project.diff: unintegrated speculative
  snapshot abstractions.
## Exact remaining owner paths

- AnimPose fronts: Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/AnimPoseLibrary.h;
  Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Private/AnimPose/AnimPoseLibrary.cpp;
  mcp-server/src/tools/animation.ts; mcp-server/tests/animation-tools.test.ts;
  plus root-owned MCPPuerTSBridgeService.h/.cpp, puerts-runtime/src/registry.ts,
  puerts-runtime/types/puerts-bootstrap.d.ts, mcp-server/src/tools/puerts.ts,
  and mcp-server/src/annotations.ts.
- Wave 1 contract repairs: MCPPuerTSBridgeBlueprintMember.cpp,
  MCPPuerTSBridgeEditorState.cpp, MCPPuerTSBridgeCloth.cpp,
  MCPPuerTSBridgeInspect.cpp, FolderVisibilityLibrary.cpp,
  MCPBridgeInputLibrary.cpp, and their matching native contract tests.
- Partial legacy-facing fronts: mcp-server/src/tools/content.ts,
  mcp-server/src/tools/materials.ts, mcp-server/src/tools/effects.ts,
  mcp-server/src/tools/gameplay.ts, and matching focused tests.
- DataTable and pattern fronts: MCPBridgeDataLibrary.h/.cpp,
  mcp-server/src/tools/content.ts, mcp-server/src/tools/gameplay.ts,
  and the gameplay-pattern focused tests.
- UI/title workflows: mcp-server/src/tools/titles.ts,
  mcp-server/src/tools/promptbrush.ts, mcp-server/src/tools/compat.ts,
  and mcp-server/tests/widget-title-tools.test.ts.
- Scene and level adapters: mcp-server/src/tools/actors.ts, level.ts,
  compat.ts, MCPPuerTSBridgeScene.cpp, actor-tools.test.ts,
  and level-viewport-tools.test.ts.
- Materials/audio/effects: mcp-server/src/tools/content.ts, materials.ts,
  effects.ts, compat.ts, MCPPuerTSBridgeMaterial.cpp,
  MCPPuerTSBridgeMaterialBuild.cpp, MCPPuerTSBridgeMaterialInstance.cpp,
  and MCPPuerTSBridgeAudioBuild.cpp.
- Gameplay/cameras: mcp-server/src/tools/gamedev.ts, gameplay.ts, effects.ts,
  compat.ts, the camera-shake command file, and MCPPuerTSBridgeEditorState.cpp.
- Project/intelligence: mcp-server/src/tools/project.ts, intelligence.ts,
  mcp-server/src/project-index.ts, project-index tests, and server-local
  descriptor helpers.
- Viewport/operations/config: mcp-server/src/tools/viewport.ts, operations.ts,
  cloth.ts, compat.ts, MCPPuerTSBridgeViewport.cpp, MCPPuerTSBridgeCloth.cpp,
  FolderVisibilityLibrary.cpp, MCPBridgeInputLibrary.cpp, and focused tests.
- Final shared reconciliation: MCPPuerTSBridgeService.h/.cpp,
  puerts-runtime/src/registry.ts, puerts-runtime/types/puerts-bootstrap.d.ts,
  mcp-server/src/tools/puerts.ts, mcp-server/src/annotations.ts,
  docs/TOOL_CAPABILITY_METADATA.json, docs/TOOL_INVENTORY.json,
  docs/CAPABILITY_SCOREBOARD.json, docs/CAPABILITY_PRESERVATION_AUDIT.md,
  skills/unreal-engine-4-27/references/tool-catalog.md, and CODE_FIRST_100_PLAN.md.

Ponytail review: Lean already. Ship. No capability-preserving deletion remains
after the shared feature fixture helper and existing orchestrator reuse.

## Latest code-first handoff

The DataTable native front is now implemented and registered as
`puerts_data_table_build` / `data_table_build`, with generated inventory entry
and skill catalog entry. Remaining repository work is the other Wave 1 to 3
items listed above, especially animation pose native fronts and gameplay-pattern,
project, UI, scene, material, camera, and viewport migrations.
## Integration correction, 2026-08-04

This section supersedes the stale sandbox and artifact notes above.

- The stale Claude process tree was stopped, and repeated source hashes stayed stable.
- Late-writer patch files were quarantined outside the repository without deletion.
- The DataTable slice is integrated end to end: native C++ desired-state builder,
  PuerTS runtime command, MCP schema, annotations, two compatibility aliases,
  metadata, inventory, catalog, and focused tests.
- DataTable mutation now validates through a transient table, detects no-op state,
  reads back before save, and reports verified rollback evidence on failure.
- Editor-free gates are green: TypeScript build, PuerTS contracts, compatibility
  contracts at 438 assertions, content tools, and inventory at 307 tools.
- The final metadata-marked ALIAS gap, `blueprint_build_from_json`, now preserves
  the legacy existing-asset precondition and result vocabulary through native
  graph inspection plus `puerts_blueprint_build`.
- Unreal, UBT, plugin install sync, live MCP calls, and PIE remain untouched.

The repository is not yet at code-complete 100. The remaining owner paths above
still apply except that the DataTable front is finished. Gameplay-pattern,
AnimPose public fronts, Wave 1 contract repairs, project and intelligence,
UI and title, scene and material, camera, viewport, and final reconciliation
remain repository-side work.

## Repository state, 2026-08-05

Read this before touching git. An aborted history rewrite happened on this
date and left specific conditions a fresh session would otherwise misread.

**There is exactly one repository: `D:\Unreal Projects\UE4_Bridge`.** A second
copy at `UE4-Bridge-Recovered` existed briefly during recovery and was deleted
after verifying every commit in it was reachable elsewhere. If a second bridge
checkout ever appears again, it is a recovery artifact, not a worktree; do not
edit in it.

- Branch `bridge/native-consolidation-2026-07-31`, HEAD `0b39132`.
- `origin` is `https://github.com/RBG-WebDesign/UE4-MCP-Bridge.git`.
- **Nothing from 2026-08-05 has been pushed.** Six commits are local only.

### Do not rewrite history for the Windows user name

It was removed from the two tracked `reports/session-2026-08-02-*.json` files by
an ordinary commit (`0b39132`). It remains in published history on `origin/main`
(`23fc605`, `e751de0`, `6e602e8`, `e488552`) and inside committed binary `.pdb`
blobs under `Plugins/MCPBridge/Binaries/`, which no text replacement reaches.
A `git filter-repo` pass was run and then abandoned: it rewrote 616 commits,
removed the `origin` remote, and still did not purge the binaries. The decision
is to leave published history alone. Do not reopen this.

Consequence worth knowing: GitHub now has **only `refs/heads/main`**. The
`lane/*` and `checkpoint/*` branches were deleted from the remote at some point,
so this clone's `refs/remotes/origin/*` are the only surviving copies of three
bridge WIP commits (`b8a0a35`, `7bb2fd3`, `c842eaa`). Do not prune them.

### The dirty tree is expected

50 modified and 14 untracked files, nearly all pre-existing native-consolidation
work that predates 2026-08-05. **Do not `git add -A`.** Stage only files related
to one change per commit. `stash@{0}` holds a redundant copy of the same work
from the aborted rewrite; it is safe to drop once the tree is reviewed.

A full backup bundle of the pre-rewrite repository (all 61 refs) was written to
the session scratchpad. It is a temp directory and may be cleaned; the active
repository no longer depends on it.

### Landed on 2026-08-05

- `puerts_project_settings_patch`: one command reaching every Project Settings
  page by writing config properties on the page's settings class CDO and
  persisting through `UpdateDefaultConfigFile`. Refuses a property without
  `CPF_Config` rather than changing it in memory only. Live acceptance is
  `npm run test:project-settings`.
- `.mcp.json` is portable. `UE_ENGINE_ROOT` and `MCP_UNREAL_PROJECT_ROOT` now
  come from the gitignored `bridge.local.json`, seeded into the environment by
  `mcp-server/src/local-config.ts`, so no client config carries a machine path.
- `Scripts/setup-unreal-project.ps1` points the bridge at any 4.27 project in
  one command and discovers the engine root from the project's
  `EngineAssociation`. `-EngineRoot` is an override, not a requirement.
- `unreal-api` runs from a pinned venv (`mcp==1.29.0`) through
  `Scripts/start-unreal-api-mcp.mjs`. `uvx unreal-api-mcp` resolves `mcp` 2.0.0,
  which dropped `fastmcp`, and crashes on import.
- The editor panel reports installed-plugin provenance and adds an
  `install:check` instruction to the copied chat prompt when the source tree was
  dirty or the install manifest is absent.