# Subagent integration log

One row per lane result the integration lead reviewed. A lane appears here only
after its branch was read, not when it was launched, because the point of the log
is what was accepted or rejected and why.

Evidence bar for acceptance: a live acceptance script with warm and cold phases,
an independent read-back, and file hash plus dirty-package plus source-control
quiescence. Compilation and mock tests alone are rejected.

## 2026-08-02

| Lane | Branch | Verdict | Reason |
|---|---|---|---|
| A (integrator, not a subagent) | `wip/blueprint-graph-patch-2026-08-02` @ `4e197ce` | **Checkpointed, not accepted** | Acceptance red: 20 checks failing. Plan mode, ambiguity refusal and batch atomicity are proven live; the apply path refuses batches containing a deferred `add_node` selector. Recorded in metadata as `implemented`, deliberately not `live_verified`. |

### Wave one, launched 2026-08-02

| Lane | Worktree | Branch | State |
|---|---|---|---|
| A finish `blueprint_graph_patch` | `_bridge_worktrees/lane-a` | `lane/a-graph-patch` | running |
| B project intelligence index | `_bridge_worktrees/lane-b` | `lane/b-project-intelligence` | running |
| C headless C++ authoring | `_bridge_worktrees/lane-c` | `lane/c-cpp-authoring` | running |

Three of six. D, E and F are not held back by capacity but by the rules
themselves: each needs a live editor and its own copied project, only two
installed targets exist, lane A is building into one of them, and E cannot start
until A lands because re-fronting builder groups onto a patch command whose apply
path is still red would build on sand. They launch in wave two once a project
copy exists per live lane.

Lane A is deliberately scoped to code and compile only. Its agent is forbidden
from launching an editor, so warm and cold live acceptance stays with the
integrator after merge. A lane cannot mark its own work live-verified.

Every lane was told the same thing in its own words: compilation and mocks do not
prove completion, and the report must say plainly what was not verified.

### Wave one results

| Lane | Branch | Verdict | Basis |
|---|---|---|---|
| A | `lane/a-graph-patch` @ `d72f848` | **Merged as `implemented`, NOT `live_verified`** | The lane claimed a compile and nothing more, which is exactly what its evidence supported. It said so itself: "proven to compile. It is not proven to work." Merged on that basis and verified live by the integrator, not by the lane. |
| B | `lane/b-project-intelligence` @ `e6fc7c7` | **Accepted** | Ten editor-free tests over a real project: 339 files, 841 symbols, a second run that re-indexes nothing, a touched file that re-indexes exactly itself with the reason recorded. |
| C | `lane/c-cpp-authoring` @ `99f1250` | **Accepted** | 56 editor-free tests. Parser proven against a captured UBT transcript carrying the real diagnostic forms this session hit. Reuses the existing cmd.exe UBT invocation rather than adding a second one. |

**Lane A, live acceptance run by the integrator.** The deferred-selector fix
works: the batch applies, and the added node, moved node, removed node and
changed pin default are all present in the asset. Check (9) passes with 0 changed
among nodes the patch never named, which is the claim the whole command rests on.

11 checks still failed, and they reduce to one real bug. Plan-time `bUnchanged`
for link operations is decided against the pre-batch graph, but operations inside
the same batch change that state. A disconnect followed by a reconnect of the
same link marks the reconnect "already linked" at plan time, the disconnect then
removes it at apply time, and the reconnect is skipped. The link is left broken,
`created_links` reads 2 instead of 3, and the later rerun legitimately applies the
missing connect, which is why convergence fails. One cause, four failing checks.
A twelfth failure was an assertion bug in the acceptance rather than a product
defect: the per-selector ambiguity text lives in `data.ambiguous_selectors`, not
in `errors`. Sent back to lane A with the diagnosis; the command stays at
`implemented`.

**Why lane B was accepted without live evidence.** Its scope was explicitly the
editor-free half, and it did not claim the other half. It designed the live half
against the command that already supplies each fact, and identified two facts -
material graph structure and asset reference edges - that have NO native command,
recording them as platform gaps to close rather than deriving them badly. A lane
that names the gap instead of papering over it is doing the capability-first rule
without being told.

**Central conflict resolution.** Lanes B and C each appended a suite to the
`test` chain in `mcp-server/package.json`, both flagged it loudly as instructed,
and both predicted the resolution would be "keep both". It was. That is the
one-writer-per-worktree rule earning its keep: the conflict was trivial, expected
and resolved in one place instead of two lanes racing over the same line.

**Lane C's honest seam.** `runUbt` and the diagnostic parser have never been
joined, because the lane was forbidden from building. Argument quoting through
cmd.exe for paths with spaces is the specific thing only a real run settles, and
generated code has never been compiled. Recorded as unverified, not claimed.

### Integrator work accepted into `bridge/native-consolidation-2026-07-31`

| Commit | Change | Evidence |
|---|---|---|
| `849ae54` | A failing Blueprint build never destroys the existing graph, in any mode | remove_unlisted acceptance warm and cold, `smoke:inspect`, `smoke:bt`, `npm run verify` |
| `e6efcf7` | Install manifest and sync gate | 21-case sabotage acceptance; both test projects synced, built and verified |
| `2fa0bca` | Session isolation across two live editors | Four-phase live acceptance with both editors open, `docs/evidence/session-isolation-*.json` |
| `5edf2e7` | Two-editor workflow runbook | Documentation only; describes behaviour proven in `2fa0bca` |
| `1b75267` | Canonical structural hash on Blueprint graph inspect | Live: present, stable across two reads, different for a different graph |

### Rejected claims

None yet. The one claim that would have been rejected was caught before it was
made: `puerts_blueprint_graph_patch` compiles, wires end to end and passes
`npm run verify`, and none of that is evidence the command works. It is recorded
as `implemented` with an empty `live_evidence` array.
