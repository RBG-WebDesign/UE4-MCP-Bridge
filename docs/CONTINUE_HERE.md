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

## Exact next commands

Launch the editor with Scripts/start-ue4-project.ps1 -Confirm:$false against
D:/Unreal Projects/BridgeInstallTest, wait for session.json, then:

    npm run slice:ui ; npm run slice:ai ; npm run slice:gameplay
    npm run slice:materials ; npm run slice:level
    npm run slice:animation ; npm run slice:cinematics

Then RB-2's last gameplay red, then the RB-6 promotion sweep, per
docs/FINAL_IMPLEMENTATION_PLAN.md.

## Paste this into a fresh session

```
Continue the UE4_Bridge finish program as integration lead.

Repo: D:\Unreal Projects\UE4_Bridge
Branch: bridge/native-consolidation-2026-07-31 (clean, verify green, 256 tools)

Read docs/CONTINUE_HERE.md, then docs/CAPABILITY_FINDINGS.md findings 0m to 0r.
All nine lanes are merged and the batch compile passes. Do not re-merge them and
do not relaunch those lanes.

1. Merge whatever wave five committed, one lane at a time, with
   node Scripts/merge-lane.mjs lane/<name>. Findings 0p and 0r are CLOSED; do
   not reopen them.
2. Batch compile after the merge set. Roughly every lane carries uncompiled C++
   and only a real build finds cross-lane collisions.
3. Settle finding 0q if lane R did not: read compile_status on the FULL member-patch fixture
   immediately after it is built and BEFORE the patch runs. If it is already
   UpToDateWithWarnings the patch is innocent and the assertion is wrong.
4. Close the capability regression lane P found: puerts_spawn_actor lost name,
   folder and scale, and puerts_find_actors lost include_transforms and
   folder_filter, against AGENTS.md's rule that legacy capability is preserved.
   scene_batch and scene_inspect are merged and compiled now, so this is wiring.
5. Run the seven slice harnesses against the live editor (npm run slice:summary).
   Nothing has ever run them with the new commands present.
6. Run the perf harness live for its first evidence file, and lane N's
   docs/RELEASE.md section 3a steps 5-7 to prove the packaged plugin LOADS.

Rules that earned their place: merge one branch at a time and resolve centrally;
install:check before AND after every live run; a lane cannot mark its own work
live_verified; every harness needs a control that must SUCCEED and move state;
compilation and mocks are not live proof.

Commit integrations separately. Do not push.
```

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
6. **A zip install has no `[MCPPuerTSBridge]` section** and the compiled default
   pipe name is one constant, so two zip installs on one machine contend for one
   pipe.
