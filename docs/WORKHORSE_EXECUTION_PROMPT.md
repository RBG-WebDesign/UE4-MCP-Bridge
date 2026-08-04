# Workhorse execution prompt

Paste everything below the line into a fresh session of a high-capacity coding
model. It is self-contained.

---

Execute the UE4_Bridge finish program as integration lead, continuously, until
the release contract passes or an actual external limit prevents progress.

Repo: D:\Unreal Projects\UE4_Bridge
Integration branch: bridge/native-consolidation-2026-07-31
Engine: UE4.27 ONLY, at D:/UE/UE_4.27. Installed target:
D:/Unreal Projects/BridgeInstallTest (the ONLY one; Tests/UE427PuerTSMCP does
not exist).

Wave A is DONE: lane/x-slice-green, lane/z-async-jobs and lane/y-animbp-snapshot
are all ancestors of HEAD, batch-compiled clean, verify green at 273
registrations / 60 native tools. Do not merge them again and do not relaunch
those lanes. Since that merge, only three of seven slices have run live: ui
PASS 20/0, ai PASS 22/0, gameplay PRESENT_BUT_FAILING 16/1 (the hidden-pin
hash fix is compiled but its rerun is what's missing). materials, level,
animation and cinematics were rewritten this session and have never run
against the merged code. Your first live action is the slice re-run below,
not a merge.

READ FIRST, in this order, before any action:
1. docs/FINAL_IMPLEMENTATION_PLAN.md and .json — the plan you are executing.
   The JSON carries every work package with files, deps, tests, acceptance,
   rollback, evidence, risk, lane and merge order. Do not re-plan.
2. AGENTS.md — binding: puerts_* tools only, no HTTP/socket/shell fallback to
   the editor, UE4.27 API safety table, code standards (no em dashes, no
   filler, strict TS, no `any`), testing etiquette (PIE is user-gated).
3. docs/CAPABILITY_FINDINGS.md findings 0g through 0u — the defects already
   found and settled. Re-deriving any of them is wasted budget; reopening a
   FIXED one requires new evidence. The file's own "## Unknown" section
   header says "None open" — that line is stale; the file grew past it
   without updating it. Two are genuinely open and not yet in any work
   package's ID: set_variable_default reads back double-JSON-quoted where
   blueprint_build does not (line ~2142); whether RebuildAll alone (without
   ProcessRegistrationCandidates/UpdateInvokers) produces a complete navmesh
   is read from source only, never run live (line ~2416). Fold both into
   whichever work package touches that code, or file them as new findings.
4. docs/CONTINUE_HERE.md and docs/SUBAGENT_INTEGRATION_LOG.md — branch state
   and the verdicts record.

FIRST ACTIONS, always:
- git status (must be clean), git worktree list, and
  `git log --oneline HEAD..lane/<name>` for every lane/* branch. Wave A's
  three lanes are already merged (see above) — this check is for wave B and
  later lane work only, not a signal to re-run wave A's merge.
- Verify no orphan agents: one claude-code session only.
- npm run verify must be green before and after every merge.
- Launch the editor (Scripts/start-ue4-project.ps1 -Confirm:$false against
  D:/Unreal Projects/BridgeInstallTest), wait for
  Saved/MCPPuerTSBridge/session.json to advertise a live pid, then run all
  seven slices: `npm run slice:ui ; npm run slice:ai ; npm run slice:gameplay ;
  npm run slice:materials ; npm run slice:level ; npm run slice:animation ;
  npm run slice:cinematics`. This is the actual next action, before touching
  any work package.

THE MERGE LAW (violating this cost a full session):
- One branch at a time via `node Scripts/merge-lane.mjs lane/<name>`.
- NEVER line-merge puerts-runtime/src/registry.ts,
  mcp-server/src/tools/puerts.ts, or MCPPuerTSBridgeService.h. Git factors out
  the common suffix of conflicting hunks; a union closes one declaration and
  leaves another open, surfacing hundreds of lines away. The script grafts
  whole brace-matched units instead.
- docs/TOOL_CAPABILITY_METADATA.json merges as JSON by key. Inventory,
  capability scoreboard and preservation audit are REGENERATED
  (`node Scripts/generate-tool-inventory.mjs --write`), never merged.
- The tool-count assertion in puerts-tools.test.ts is read from a real run:
  build, then `createPuertsTools(new PuerTSClient()).length`.
- Typecheck BOTH tsconfigs before UBT ever sees the code.
- After the merge set: close the editor (it locks the DLL), then
  `node Scripts/bridge-install.mjs --sync --project "D:/Unreal Projects/BridgeInstallTest"`.
  If the linker names a symbol in neither header, delete the TARGET's
  Plugins/MCPBridge/Intermediate and rebuild (finding 0m). bUseUnity stays
  false on MCPBridgePuerTS.

THE EVIDENCE LAW:
- A lane cannot mark its own work live_verified. Only you promote, only after
  running the acceptance yourself, warm AND cold (cold = after an editor
  restart).
- install:check before AND after every live run; a concurrent writer drifting
  the target mid-run has happened and the gate caught it.
- Every harness needs a control that must SUCCEED and must MOVE state. Three
  atomicity levers once passed purely because everything failed and nothing
  could move.
- Fresh fixture path per run, asserted unused (graph_inspect must FAIL on it
  first). There is no delete-asset primitive until FP-3 lands.
- Compilation and mocks are never live proof. A watcher must poll for a marker
  the watched file will actually contain.
- CDO writes are OUTSIDE transactions (finding 0r): boundary snapshot +
  restore; check Modify()'s return value.
- Fix the product, not the assertion. Do not weaken a slice or acceptance to
  make it pass.

THE LANE LAW:
- One writer per worktree (D:/Unreal Projects/_bridge_worktrees/lane-*), one
  branch per lane, lanes commit before stopping, lanes never merge/push/rebase
  or edit the integration checkout or the shared logs.
- Max two editors. One build owner and one editor owner per installed project
  per wave. Offline lanes never build; they report READY TO BUILD and stop.
- Max safe parallelism: 3 lanes (1 live + 2 offline). A 4th offline lane only
  if it touches none of: registry.ts, tools/puerts.ts, the service header,
  annotations.ts, puerts-bootstrap.d.ts, Build.cs, puerts-tools.test.ts.
- Launch editors with Scripts/start-ue4-project.ps1 -Confirm:$false; wait for
  Saved/MCPPuerTSBridge/session.json advertising a live pid.
- Red acceptance blocks promotion of that capability only. It never stops
  unrelated lanes. implemented_unverified is a valid construction state.
- Record every product defect in docs/CAPABILITY_FINDINGS.md as you find it,
  with the measurement that distinguishes candidate causes. Check the READER
  before the writer: this codebase has repeatedly had the reader be wrong.

EXECUTION ORDER (details and gates in FINAL_IMPLEMENTATION_PLAN.json):
- Wave A: DONE (merge, batch compile, smoke). The re-run of all seven slices
  is the FIRST ACTION above, not a separate wave A step — do it before Wave B.
- Wave B: gameplay join (RB-2), materials/level live debug (RB-3), promotion
  sweep batch 1 (RB-6); offline FP-1/FP-2 and FP-3/FP-4/FP-5.
- Wave C: feature acceptance library 1-6 live via orchestrator; 7-12 authored
  offline; PORT batch 1.
- Wave D: sequence_render on the job API, first perf evidence file, lighting
  swarm_available; RL-2 zip pipe config, RL-4 CI; human items RL-1/RL-3.
- Wave E: library 13-18, promotion sweep remainder, PORT/REFRONT/MERGE/RETIRE
  completion.
- Stage 2, SERIAL, freeze first: Ponytail audit, strongest-model independent
  audit (feed it CAPABILITY_FINDINGS.md and the plan), repair blockers, full
  library on a clean project, packaged-zip LOAD proof, release.

After every wave: regenerate inventory and scoreboard once, update
SUBAGENT_INTEGRATION_LOG.md with verdicts (including rejected claims), update
CONTINUE_HERE.md so an interrupted session resumes from branches rather than
guesswork, run Ponytail review (non-blocking), and launch the next
dependency-ready wave immediately.

KNOWN EXTERNAL LIMITS (do not burn budget re-attempting; cite them):
- Binary plugin packaging is impossible in 4.27: BuildPluginCommand writes a
  one-plugin host descriptor (BuildPluginCommand.Automation.cs:66,129,133).
  Source zip is the ship vehicle and is proven to install and compile.
- sequence_render cannot run synchronously on the game thread; it ships on the
  async job API or not at all.
- Lightmass/Swarm is environmental (finding 0u): binaries present, agent
  starts manually. lighting_build must report swarm_available; automated tests
  must not bake lighting.
- eqs_build is refused BY DESIGN: UEnvironmentQueryGraph::UpdateAsset rebuilds
  Options from the editor graph and would silently wipe a built asset.
- Cloth writers stay read-only until their transactions provably cancel.

STOP CONDITIONS:
- The release contract in FINAL_IMPLEMENTATION_PLAN.md Stage 2 passes, or
- an actual external limit prevents progress (name it, cite it, and stop that
  item only), or
- capacity ends: then commit every branch, salvage every uncommitted change,
  update CONTINUE_HERE.md with exact commits and next commands, leave the
  integration tree clean, state plainly what is uncompiled or unverified, and
  do NOT claim completion.

Do not push. Do not start PIE without an explicit user go. Report progress by
scoreboard deltas and live-suite output, never by intention.
