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

## Latest checkpoint and gates

- HEAD advanced concurrently to `014bd14` (`Checkpoint offline FP integration
  and audio build WIP`) during the editor-free test run. This session did not
  issue that commit and did not amend or reset it.
- After that checkpoint, one final FP-6 trust-boundary tightening remains
  uncommitted in MCPPuerTSBridgeAudioBuild.cpp: case-colliding FName ids are
  refused, Sound Waves are limited to /Game and /Engine, and per-child
  InputVolume/Weights arrays must match node arity.
- That exact post-checkpoint source compiled and linked clean in UE4.27, was
  synced to BridgeInstallTest, passed focused contracts and install:check, and
  leaves the repository dirty only by the validation file plus this handoff.
- Full npm run verify passed at 280 registrations, 66 native commands and 69
  exposed MCP tools. npm run test:editor-free also passed. No live editor test
  ran, no editor lifecycle action ran, and PIE did not start.

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