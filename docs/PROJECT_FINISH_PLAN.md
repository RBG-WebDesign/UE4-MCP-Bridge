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

Paste this verbatim into a fresh session.

```
Continue the UE4_Bridge finish program as integration lead.

Repo: D:\Unreal Projects\UE4_Bridge
Integration branch: bridge/native-consolidation-2026-07-31 at 1b75267 (clean)
WIP branch: wip/blueprint-graph-patch-2026-08-02 at 4e197ce (do not discard)

Read docs/PROJECT_FINISH_PLAN.md and docs/PROJECT_FINISH_SCOREBOARD.json first.
They carry the measured state; ignore prose counts elsewhere that disagree.

Start with lane A, because lane E depends on it and it is two located bugs from
green:

1. Check out wip/blueprint-graph-patch-2026-08-02.
2. In UBlueprintGraphBuilderLibrary::PatchBlueprintGraphFromJSON pass 1, give a
   selector naming a node the batch will add its own deferred state. Today it is
   stored as key -> nullptr in PlannedNewNodes and the connect_pins branch reads
   that nullptr as "matched nothing", records an unmatched selector, and the
   whole batch is refused before applying. Do not weaken the unmatched or
   ambiguous refusals to fix it.
3. In Scripts/bp-graph-patch-acceptance.mjs the remove_node selector names
   K2Node_CommutativeAssociativeBinaryOperator for a multiply_float node. Read
   the real node_class from graph_inspect and use that.
4. node Scripts/bridge-install.mjs --sync --project "D:/Unreal Projects/BridgeInstallTest"
   (close the editor first; a running editor locks the DLL and the link fails).
5. Launch the editor with Scripts/start-ue4-project.ps1 -Confirm:$false, wait for
   Saved/MCPPuerTSBridge/session.json, then run the acceptance record phase, then
   restart and run --phase=cold.
6. Add the source-control quiescence and round-trip timing checks the script
   declares but never reaches.
7. Only when both phases are green, promote puerts_blueprint_graph_patch from
   "implemented" to "live_verified" in docs/TOOL_CAPABILITY_METADATA.json,
   regenerate with node Scripts/generate-tool-inventory.mjs --write, run
   npm run verify and npm run smoke:inspect, and merge into the integration
   branch as its own commit.

Then launch lanes B and C in parallel git worktrees (neither needs a live
editor), and lanes D and F after, honouring at most two live editors with a
unique copied project and session each. Lane E last, after A merges.

Update docs/PROJECT_FINISH_SCOREBOARD.json and
docs/SUBAGENT_INTEGRATION_LOG.md as each lane lands. Reject any lane result
whose evidence is compilation or mocks alone. Commit integrations separately.
Do not push.
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
