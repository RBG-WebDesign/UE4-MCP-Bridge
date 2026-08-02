# Project finish plan

Integration lead's plan for taking UE4_Bridge from working prototype to the
definition of finished in `docs/PROJECT_FINISH_SCOREBOARD.json`.

Every number here was read out of a generated file or counted from source on
2026-08-02. Where a prose count elsewhere in `docs/` disagrees, this file and the
scoreboard are right and the prose is stale.

## Where the project actually is

| Measure | Value |
|---|---:|
| Total tool registrations | 208 |
| Unique canonical capabilities | 164 |
| Registered by default | 26 (24 native pipe + 2 server-local) |
| `live_verified` | 17 |
| `live_partial` | 8 |
| `implemented`, not live-proven | 1 |
| `mock_only` | 60 |
| `untested` | 122 |

**17 of 164 is the only defensible completion figure.** The 170 `legacy_http`
tools are not registered unless `MCP_ENABLE_LEGACY_HTTP=1`, and 158 of them are
`legacy_untested`. Counting the 75 `KEEP` rows as finished would be exactly the
stale-prose accounting this plan exists to replace.

Migration actions already decided but not executed: REFRONT 54, PORT 39,
ALIAS 29, MERGE 10, KEEP 75, RETIRE 1 (`python_proxy`).

## What this session landed

| Commit | Capability |
|---|---|
| `849ae54` | A failing Blueprint build never destroys the existing graph, in any mode |
| `e6efcf7` | Install manifest and sync gate; a stale plugin can no longer reach an acceptance run |
| `2fa0bca` | Session isolation: two editors, no cross-routing, no fallback pipe |
| `5edf2e7` | Two-editor workflow runbook |
| `1b75267` | Canonical structural hash on Blueprint graph inspect |
| `4e197ce` | WIP checkpoint of `blueprint_graph_patch` on `wip/blueprint-graph-patch-2026-08-02` |

## Wave one

Six lanes. Dependencies, live-editor need and risk are in the scoreboard; the
short version is that A, D, E and F need a live editor and the two-editor rule
serialises them, while B and C are pure code and can run fully parallel.

```
A (patch)  ──┐
B (index)  ──┼── independent
C (c++)    ──┘
D (perf)   ── independent, needs editor
E (refront)── depends on A
F (release)── independent, needs editor
```

### Lane rules

One writer per worktree, one branch per lane. No subagent merges, pushes, edits
another worktree, or shares a live-writing Unreal project. Each live lane gets
its own copied test project, its own project-hashed pipe and its own session; the
session isolation landed in `2fa0bca` is what makes that safe, and
`npm run install:check` runs before any editor launch.

Compilation and mocks do not prove a lane complete. The evidence bar is a live
acceptance script with warm and cold phases, an independent read-back, and file
hash plus dirty-package plus source-control quiescence.

## Why wave one did not complete in this session

Capacity, and only capacity. Lanes A, E and F each need a live editor, a copied
project, and warm-plus-cold acceptance runs; a UE4.27 editor takes minutes to
reach an advertised session, the DLL must be unlocked before every rebuild, and
the max-two-editor rule serialises them. That did not fit in the remaining
session budget alongside integration.

Nothing was abandoned and nothing is falsely marked complete: the WIP is
committed at `4e197ce`, the integration branch is clean at `1b75267`, and the
continuation prompt below restarts the program exactly where it stopped.

## Continuation prompt

Regenerated 2026-08-02 after waves one and two. The earlier version pointed at
lane A, which is finished. Paste this verbatim into a fresh session.

```
Continue the UE4_Bridge finish program as integration lead.

Repo: D:\Unreal Projects\UE4_Bridge
Integration branch: bridge/native-consolidation-2026-07-31, clean, nothing pushed.
Read docs/PROJECT_FINISH_SCOREBOARD.json first. It carries the measured state,
the two reasoned negatives, and an open provenance incident. Ignore prose counts
elsewhere that disagree with it.

RUN LANES SEQUENTIALLY, one writer at a time. Wave one ran three concurrent lanes
with no incident; wave two ran three and produced four reports of an unidentified
writer touching worktrees and the integration branch. The hook hypothesis is
DISPROVEN (no git hooks, and no Claude hook that runs git). The remaining
hypothesis is concurrent agents sharing one .git with a drifting cwd. It is
unproven. Either demonstrate it or avoid it; do not assume it away.

FIRST, finish lane E. Its branch lane/e-refront is at dcc2099, unmerged, and is
the only lane result not yet integrated:
1. Review the out-of-scope change on that branch to lane A's shipped
   blueprint_graph_patch (MCPPuerTSBridgeBlueprint.cpp +33, runtime.ts +8, part
   of registry.ts). Lane E did not author it, reverted it twice, and could not
   stop it returning. Judge it as lane A's file. Keep or drop it deliberately.
2. Its C++ compiled but never linked: LNK1104, an editor held the DLLs. Close
   every editor, then run
   node Scripts/bridge-install.mjs --sync --project "D:/Unreal Projects/BridgeInstallTest"
   If UBT reports up-to-date after a failed link, delete the DLL and rebuild. A
   stale DLL against current objects has bitten this session twice.
3. Run Scripts/bp-member-patch-acceptance.mjs warm, restart the editor, run it
   cold. It has never executed. Only if both are green, promote
   puerts_blueprint_member_patch from "implemented" to "live_verified" in
   docs/TOOL_CAPABILITY_METADATA.json and regenerate with
   node Scripts/generate-tool-inventory.mjs --write
4. Lane E found blueprint_node_set_enabled is blocked: it needs a
   set_node_enabled op in PatchBlueprintGraphFromJSON, which update_node cannot
   do because update_node writes pin defaults only.

THEN wave three, sequentially, in this order:
- The remaining 38 genuine REFRONT tools. docs/REFRONT_PLAN.md has a verdict per
  builder. Take one builder group end to end with a live acceptance each. Lane E
  re-classified 5 of 17 BlueprintMutator tools as MERGE rather than REFRONT;
  expect more, and check each verdict against source rather than trusting the
  paper assignment.
- The 16 native-wrapper gaps, listed in docs/CAPABILITY_SCOREBOARD.json under
  native_wrappers_required.
- Two capabilities have NO native command at all and block the project index's
  live half: material graph inspection, and asset reference edges via
  IAssetRegistry GetReferencers/GetDependencies. Lane B recorded them as platform
  gaps. Close them before extending the index.
- The one-prompt authoring workflows for gameplay, UI, AI, animation, materials
  and level authoring. These are the actual definition of finished and nothing in
  waves one or two touched them.

DO NOT re-litigate two settled negatives:
- Progress and cancellation cannot be built without changing the command queue.
  uv_run is pumped on the game thread, so a status query is never read off the
  pipe while a long native call holds it. docs/PERFORMANCE.md specifies the
  change. next_offset paging on the scanning tools is the recommended first step
  and needs no C++ at all.
- Scripts/package-mcp-bridge.ps1 does not produce an installable artifact. Four
  reasons in the scoreboard under packaging_verdict, proven by packaging a
  deliberately gutted tree.

Every live proof is the integrator's. Agents write and compile; they do not bless
their own work. That split is what made the graph patch trustworthy and it caught
three bugs no compile would have found.
```

## Standing constraints

- At most two live Unreal editors; each live lane gets a unique copied project,
  pipe and session.
- `npm run install:check` before any editor launch.
- Public names, schemas, aliases, builders and evidence are preserved. PuerTS is
  additive.
- Every mutation is transactional, convergent, independently inspected and
  failure-atomic.
- No second transport, rollback, inspection, indexing or registry system.
- The integrator owns merges, inventory, metadata and the scoreboard.
