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