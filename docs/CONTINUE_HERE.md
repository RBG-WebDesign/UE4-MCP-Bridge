# Continue here

Written 2026-08-02 by the integration lead, at the point an account session
token limit stopped nine of ten wave-three lanes. Nothing below is a guess about
what happened; every branch named here exists and every claim is checkable.

## State

- Integration branch: `bridge/native-consolidation-2026-07-31`, working tree clean.
- `npm run verify`: green, 209 tools, frozen count 209.
- `install:check` against `D:/Unreal Projects/BridgeInstallTest`: current.
- One installed target exists. `Tests/UE427PuerTSMCP` does not exist on disk.
- Orphan agents from earlier sessions are dead. Their commits are all ancestors of HEAD.
- An editor may still be running. Close it before any rebuild.

## What is proven live

Eleven acceptance suites plus, new this wave, `Scripts/mutator-atomicity.mjs`
green including its control. That control matters: three of its levers passed on
an earlier run only because everything failed and nothing could move.

## What is half-done, and it is a lot

Eight branches carry uncompiled, unreviewed, mid-implementation work. They are
NOT designs and NOT throwaway. Resume them before starting anything new.

| Branch | WIP commit | Domain |
|---|---|---|
| `lane/i-material` | `a5053a7` | material inspect, instances, parameters |
| `lane/j-animation` | `19a4c31` | AnimBP inspect and build |
| `lane/k-ai-gameplay` | `4ba3cf6` | blackboard, EQS, navigation, perception |
| `lane/l-level-scene` | `ce5983b` | scene_inspect, scene_batch |
| `lane/m-refront2` | `5e5b91e` | REFRONT groups 2 and 4 |
| `lane/n-packaging` | `ef7257a` | bundle the pinned PuerTS runtime, run UAT |
| `lane/o-perf-live` | `5962855` | benchmark hardening, orchestrator |
| `lane/p-slices` | `32c2161` | seven vertical slice harnesses |

Their worktrees are at `D:/Unreal Projects/_bridge_worktrees/lane-<letter>`.

## Paste this into a fresh session

```
Continue the UE4_Bridge finish program as integration lead.

Repo: D:\Unreal Projects\UE4_Bridge
Branch: bridge/native-consolidation-2026-07-31 (clean)

Read docs/PROJECT_FINISH_SCOREBOARD.json, docs/SUBAGENT_INTEGRATION_LOG.md and
docs/CAPABILITY_FINDINGS.md findings 0m, 0n, 0o and 0p first. They carry the
measured state. Ignore prose counts elsewhere that disagree.

Do not restart planning. Do not re-derive the REFRONT map.

1. Close any running UE4Editor before rebuilding; it locks the DLL.
2. Resume the eight WIP branches listed in docs/CONTINUE_HERE.md, one lane per
   worktree, one writer each. They are mid-implementation and uncompiled. Each
   lane reviews its own WIP, finishes it, compiles, and reports what it did NOT
   verify. Only one lane holds build rights on BridgeInstallTest at a time.
3. Diagnose finding 0p before touching the writer: set a float default through
   blueprint_member_patch, then read the CDO with puerts_read_property and
   compare against what graph_inspect reports for the same variable. If the CDO
   holds the value and the inspector says empty, the READER is wrong. Then
   re-run Scripts/bp-member-patch-acceptance.mjs warm and cold. It is at 2 red
   checks, down from 8.
4. Re-front REFRONT group 1, the 18 Blueprint editing tools. It is unblocked:
   BPMutatorHelpers now cancels on failure and that is proven live.
5. Redo lane G's 19-point mutator audit. It was never delivered. Coverage of
   every entry point is currently argued from one shared wrapper, not
   established.
6. Run the perf harness live for its first evidence file, and run the teammate
   and fresh-project install proofs, which have never been executed by anyone.

Integrator rules that earned their place this session:
- Merge one branch at a time. Lanes report; they do not merge, do not edit the
  integration checkout, and do not write the shared log.
- install:check before every live run, and again after, because a concurrent
  writer can drift the target mid-acceptance. That gate caught it twice.
- A lane cannot mark its own work live_verified.
- Every harness needs a control that must SUCCEED and must move state.
  Three atomicity levers passed while proving nothing before one was added.
- Compilation and mocks are not live verification.

Commit integrations separately. Do not push.
```

## Two traps worth knowing before you start

**A test that exits 0 having run nothing.** `mutator-atomicity.mjs` did exactly
that: its entry guard compared `import.meta.url` against a hand-built
`file://` + `argv[1]`, which on Windows never matches. Use `pathToFileURL`.
`bridge-install.mjs` had the same broken clause, hidden behind an `endsWith`
fallback. If a new script prints nothing and exits 0, suspect this first.

**Content equality is not build equality.** Finding 0m. A target whose sources
match the repository can still hold UHT reflection for a `UFUNCTION` that no
longer exists, and the linker will blame a symbol that is in neither header.
Delete the target's `Plugins/MCPBridge/Intermediate` and rebuild.
