# Wave three ownership

One writer per worktree. Recorded before any lane started, so a collision is a
rule break rather than an ambiguity. This file is the authority; if a lane and
this table disagree, this table is right.

Integration branch: `bridge/native-consolidation-2026-07-31`.
All lanes branched from `7997964`.

## Pre-work completed 2026-08-02

| Step | Result |
|---|---|
| Stop orphan agents | Done. PID 29320, a claude-code session started 08:55, killed with its whole tree. My session is 25068 and survived; the Claude desktop app 21624 was not touched. |
| Preserve their commits | Verified. `git log HEAD..<branch>` is 0 for all seven prior branches, so every commit any orphan produced is already an ancestor of the integration head. Nothing to rescue. Their unique findings, including `55b971e` "rename_component is not convergent", are preserved. |
| One writer per worktree | Confirmed. All six prior worktrees clean, 0 uncommitted files each. Ten new worktrees created empty at `7997964`. |
| Close UE4Editor 36268 | Done. 0 editors alive. |
| `git status` | Clean. |
| `install:check` | `install is current`, installed from `7b242f5`. |

## Lanes

| Lane | Branch | Worktree | Domain | Build rights | Editor |
|---|---|---|---|---|---|
| G | `lane/g-mutator-atomicity` | `lane-g` | BPMutatorHelpers failure atomicity, 19-point audit | **YES, exclusive on BridgeInstallTest** | no |
| H | `lane/h-member-patch` | `lane-h` | Deterministic member_patch acceptance | no | no |
| I | `lane/i-material` | `lane-i` | Material inspect, instances, parameters | no | no |
| J | `lane/j-animation` | `lane-j` | AnimBP inspect and build, state machines | no | no |
| K | `lane/k-ai-gameplay` | `lane-k` | Blackboard, EQS, navigation, perception | no | no |
| L | `lane/l-level-scene` | `lane-l` | scene_inspect, scene_batch, lighting, volumes | no | no |
| M | `lane/m-refront2` | `lane-m` | REFRONT groups 2 and 4, doc reconciliation | no | no |
| N | `lane/n-packaging` | `lane-n` | Package the PuerTS bundle, run UAT for real | **own fresh project only** | no |
| O | `lane/o-perf-live` | `lane-o` | Benchmark hardening, honest progress, orchestrator | no | no |
| P | `lane/p-slices` | `lane-p` | Seven vertical slice harnesses | no | no |

## Standing constraints this wave

- Only lane G touches `D:/Unreal Projects/BridgeInstallTest`. It is the only
  installed target: `Tests/UE427PuerTSMCP` does not exist on disk.
- Lane N may create and build its OWN throwaway project and must not touch
  BridgeInstallTest.
- No lane launches an interactive editor. The integrator runs every live proof,
  which is the split that made lane A and the wave two suites trustworthy.
- No lane merges, pushes, rebases, or edits the integration checkout. Wave two
  lost time to exactly that.
- Lanes commit as `implemented_unverified` or `implemented_partial`. Only the
  integrator promotes to `live_verified`, and only after a live run.

## Known dependency

REFRONT group 1, the 18 Blueprint editing tools, is blocked on lane G. That
library commits on failure today, so re-fronting onto it would ship 18 mutations
that cannot roll back. Lane M is designing group 1 on paper and landing groups 2
and 4 instead.

# Wave four ownership, 2026-08-03

Resumption wave. Every lane continues its OWN salvaged branch; none starts over.

## Verification before launch, all six gates

| Gate | Result |
|---|---|
| No orphan agents | One claude-code session alive, which is this one. Clean. |
| Salvaged branches exist | All eight at exactly the commits `docs/CONTINUE_HERE.md` names. No drift. |
| One writer per worktree | All eight clean, correct branch checked out. |
| Integration tree clean | Yes, at `cb88f4c`. |
| No editor before rebuilds | One editor alive, PID 38608, deliberately kept for lane Q's read-only diagnosis. NO lane may rebuild while it runs. |
| `install:check` | Current. The running editor's plugin carries the ValueToJsonText fix. |

## Lanes

| Lane | Branch | Resumes from | Build rights | Editor |
|---|---|---|---|---|
| Q | `lane/q-finding-0p` | new, off `cb88f4c` | no | **read-only on the live editor, may not launch or close it** |
| I | `lane/i-material` | `a5053a7` | no | no |
| J | `lane/j-animation` | `19a4c31` | no | no |
| K | `lane/k-ai-gameplay` | `4ba3cf6` | no | no |
| L | `lane/l-level-scene` | `ce5983b` | no | no |
| M | `lane/m-refront2` | `5e5b91e` | no | no |
| N | `lane/n-packaging` | `ef7257a` | **own fresh project only** | no |
| O | `lane/o-perf-live` | `5962855` | no | no |
| P | `lane/p-slices` | `32c2161` | no | no |

## The dependency that changed

REFRONT group 1, the 18 Blueprint editing tools, was blocked and is now
UNBLOCKED. `BPMutatorHelpers` cancels on its failure path and that is proven
live by `Scripts/mutator-atomicity.mjs`, all checks plus the control. Lane M was
previously told to design group 1 on paper; it may now land it. No lane may
modify `BPMutatorHelpers.cpp`.

## Compile scheduling

No lane rebuilds `BridgeInstallTest` this wave. Lanes I, J, K, L and M all carry
C++ that needs UHT and UBT; they finish everything else, report READY TO BUILD,
and stop. The integrator batches those compiles after merging, because five
lanes building into one shared project is the collision that cost lane A two
sessions. Lane N builds only its own throwaway project and cannot collide.

# Wave five ownership, 2026-08-03

Stage-one completion wave. Every lane closes a domain that has no callable
implementation, except lane R which settles the last red check on a merged one.

## Pre-flight

| Gate | Result |
|---|---|
| Integration tree | Clean at `df14297` |
| Unmerged branches | None. All nine wave-four lanes are ancestors of HEAD. |
| Orphan agents | None. One claude-code session, which is this one. |
| Editors | One, PID 26764, assigned to lane R |
| `install:check` | Current |

## Lanes

| Lane | Branch | Domain | Build rights | Editor |
|---|---|---|---|---|
| R | `lane/r-finding-0q` | Finding 0q: why a patched Blueprint compiles with warnings | **YES, exclusive** | **YES, exclusive** |
| S | `lane/s-sequencer` | Sequencer and cinematics, the last empty domain | no | no |
| T | `lane/t-material-graph` | Material graph authoring and textures | no | no |
| U | `lane/u-level-lighting` | Lighting build, class defaults, and the capability regression | no | no |
| V | `lane/v-widget-bind` | Widget bindings and animations | no | no |
| W | `lane/w-domain-gaps` | Navigation build, AnimBP patch, audio, cloth | no | no |

Lane R holds both the editor and the build lock because 0q can only be settled
live, and because five lanes building into one project is the collision that
cost this program two sessions. Everyone else writes and compiles nothing; the
integrator batches their compiles after merging.

## What each lane was told not to repeat

- A no-op decided at PLAN time against pre-batch state is wrong when an earlier
  operation in the same batch moved that state. Shipped three times here.
- `CDO->Modify()` returns whether the object reached the undo buffer, and for a
  class default object it does not. Discarding that bool was finding 0r.
- A fixed fixture path produces false reds on the second run. Findings 0n, 0r.
- If a mutation cannot be transactional, convergent, independently inspected and
  failure-atomic, ship the read half and name the blocker. That is an expected
  outcome, not a failure.

## Stage-one gap this wave is aimed at

The seven slice harnesses currently need 16 distinct primitives. Ten were
unowned before this wave. After it, the unowned set should be empty or named.
