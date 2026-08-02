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
