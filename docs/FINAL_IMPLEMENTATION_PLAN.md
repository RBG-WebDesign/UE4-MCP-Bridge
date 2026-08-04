# Final implementation plan

Written 2026-08-03 by the integration lead, from generated files and live runs in
this session, not from prose. Where any older document disagrees with the
numbers here, these numbers were measured later and win. Machine-readable twin:
`docs/FINAL_IMPLEMENTATION_PLAN.json`. Executor prompt:
`docs/WORKHORSE_EXECUTION_PROMPT.md`.

## The finish line

Claude or Codex can create, patch, compile, inspect, run, verify and repair
ordinary UE4.27 game content with minimal manual editor work: gameplay
mechanics, Blueprints, UI screens and menus, AI, animation setups, materials,
level interactions, audio, input, physics, Sequencer content, and C++
extensions.

Whole-game generation from one prompt is a stretch goal. It is NOT the release
gate, and no item below may be blocked on it.

## Measured state, 2026-08-03

| Measure | Value |
|---|---:|
| Registrations | 273 |
| Unique canonical capabilities | 196 |
| Native pipe commands | 60 |
| Native aliases | 40 |
| Server-local | 3 |
| Legacy HTTP (opt-in only) | 170 |
| `live_verified` | 19 |
| `implemented` (compiles, unproven) | 30 |
| `live_partial` | 8 |
| `untested` | 78 |
| Migration actions open | REFRONT 28 (6 really MERGE, so 22 genuine), PORT 39, MERGE 10, RETIRE 1 |

Vertical slices, last live run (all seven exist; five can attempt):

| Slice | Verdict | Checks |
|---|---|---|
| ui | **PASS** | 20 / 0 |
| ai | **PASS** | 22 / 0 |
| gameplay | PRESENT_BUT_FAILING | 16 / 1 (hidden-pin hash fix compiled, unrun) |
| materials | rewritten, UNRUN | authors its own master material now |
| level | rewritten, UNRUN | three causes fixed; lighting no longer bakes |
| animation | UNBLOCKED, unrun | anim_blueprint_patch merged and compiled |
| cinematics | UNBLOCKED, unrun | sequence_render_start merged; png/jpg/bmp/exr only |

**Wave A is COMPLETE**: X, Z and Y merged, batch-compiled clean (after a
finding-0m intermediate purge and dropping AVI capture, whose header installed
engines do not ship), verify green at 273 registrations / 60 native tools.
First action now: relaunch the editor and re-run all seven slices, then RB-2's
one remaining gameplay red and RB-6's promotion sweep.

## Honest completion estimate

- Stage-1 domain coverage (callable implementation per domain): ~80%.
- Integrated game-ready reliability (a prompt produces a working feature):
  ~35–40%. The gap is proof and joins, not code volume.
- The long pole: live evidence for ~78 untested capabilities plus the feature
  acceptance library, measured in editor round trips.

## Domain inventory

| Domain | create | patch | inspect | verify | State |
|---|---|---|---|---|---|
| Blueprint class/graph | build | graph_patch, member_patch | graph_inspect | hash + compile report | **live_verified core**; graph_patch and member_patch both proven warm+cold |
| Widget/UI | widget_build | widget_bind | widget_inspect (+bindings, +animations) | hash | build+inspect live_verified; bind implemented; slice at 19/20 |
| Behavior Tree / Blackboard | behavior_tree_build, blackboard_build | desired-state rerun | behavior_tree_inspect, blackboard_inspect | hash | BT live_verified; blackboard implemented |
| AI (EQS/nav/perception) | ai_perception_build, nav_build | desired-state rerun | eqs_inspect, nav_inspect, nav_query, ai_controller_inspect | read-back | implemented; no eqs_build BY DESIGN (UpdateAsset wipes it — cited) |
| Animation | anim_blueprint_build (create-only) | **BLOCKED** (lane Y in flight) | anim_blueprint/montage/blend_space_inspect | hash | patch needs content snapshot; clear pass exists and makes failure worse (finding 0t) |
| Materials | material_build, material_instance_build, texture_import | desired-state rerun | material_inspect | hash + compile result | implemented, never run live; Modify() ownership settled by lane T |
| Level/scene | scene_batch, spawn_actor (restored params) | scene_batch upsert | scene_inspect, find_actors (restored params) | hash + PlayerStart rules | implemented; slice 3/8 |
| Lighting | lighting_build (start/status, honest non-wait) | n/a | status + NumLightingUnbuiltObjects | banner counter | implemented; needs swarm_available precondition (finding 0u) |
| Physics | physics_build | — | physics_observe | live | live_verified |
| Sequencer | sequence_build | desired-state upsert | sequence_inspect | hash | implemented; render **BLOCKED** sync (lane Z in flight) |
| Audio | — | — | audio_inspect | hash | read-only; cue builder feasible, blocked on same snapshot gap |
| Cloth | — | — | cloth_inspect | pass-through | read-only BY DESIGN (writers don't cancel transactions) |
| Input | input_mapping_patch | desired-state | input_mapping_info | read-back | implemented |
| C++ authoring | generator + UBT + diagnostic parser | Build.cs editor | compiler read-back | 56 tests | editor-free proven; never joined end-to-end live |
| Class defaults | class_defaults_patch | desired-state | read-back via read_property | export compare | implemented, 0r-aware |

## The findings ledger (do not re-derive)

0g transaction cancel restores nothing by itself · 0m content equality ≠ build
equality (delete target Intermediate) · 0n no delete-asset primitive; fresh
fixture paths are the law · 0o ImportText non-null is not a type check · 0p
defaults live on the CDO; description is compiler-emptied scratch · 0q event
dispatcher was half-created; compile messages now readable via a converged
member_patch call · 0r CDO writes are OUTSIDE transactions (Modify() returns
false and was discarded); boundary snapshots are the fix · 0s/0t nav/AnimBP
specifics · 0u Swarm is environmental; lighting_build must report
swarm_available.

Merge law: `Scripts/merge-lane.mjs`, one lane at a time; never line-merge
registry.ts / tools/puerts.ts / the service header (graft); metadata is a JSON
key-merge; regenerate inventory; the tool-count assertion comes from a real run.
`bUseUnity=false` stays.

## Work packages

Full structured list with every field (ID, files, dependencies, tests,
acceptance, rollback, evidence, risk, lane, merge order) is in
`FINAL_IMPLEMENTATION_PLAN.json`. Summary by category:

### 1. Release blockers (gate)
- **RB-1** Merge wave six (X, Y, Z) with merge-lane.mjs; batch compile; live re-run.
- **RB-2** Gameplay slice 0/4 → green. A JOIN defect, not a primitive gap.
- **RB-3** Materials + level slices live-debug (first live runs of material_build/texture_import/scene_batch/lighting_build).
- **RB-4** Async job API landed (lane Z) + sequence_render on it; migrate lighting_build/nav_build shapes additively.
- **RB-5** AnimBP patch via content snapshot (lane Y) OR documented engine blocker with the sharper reason.
- **RB-6** Promotion sweep: run live acceptance for the 30 `implemented` tools; promote what passes; findings for what fails.

### 2. Missing feature-dev primitives
- **FP-1** Runtime observation: read a variable/state off a running PIE actor (pie_agent read half exists; join it). Without it no slice proves BEHAVIOUR.
- **FP-2** Input simulation in PIE (pie_agent press/move re-front, user-gated).
- **FP-3** Delete-asset primitive (finding 0n; enables fixture reset and downward convergence).
- **FP-4** remove_unlisted components scope (finding 0n asymmetry).
- **FP-5** Level save/load/create as commands (level_save exists legacy-only).
- **FP-6** Sound cue builder (blocked on the 0n/0t snapshot pattern; feasible per lane W).

### 3. Domain completion — the feature acceptance library
One script per feature, modelled on bp-graph-patch-acceptance (warm+cold,
independent read-back, file hash, dirty/source-control quiescence): health and
damage · sprint/stamina · interaction · doors and locks · inventory and
equipment · save/load · weapons and projectiles · HUD · pause/settings/main
menus · controller navigation · dialogue · AI patrol/chase/search/perception ·
locomotion AnimBP · montages and notifies · material effects · level
interaction · Sequencer events · packaged feature demo. Each is BOTH the test
and the recipe; failures name the missing primitive (slice-harness pattern).

### 4. Legacy preservation and migration
- 22 genuine REFRONT remaining (PIE agent write half 7, AnimPose 5, Cloth 4 write-blocked, AutoPIE 2, DataTable 2, AnimBP 1, blueprint_compile 1).
- 39 PORT: port or alias-with-evidence each; parameter parity per finding 0s (the spawn_actor lesson: aliases that describe loss as design).
- 10 MERGE + 1 RETIRE (python_proxy) executed in metadata.
- Legacy listener stays opt-in; never a fallback.

### 5. Autonomous orchestration
- **AO-1** bridge-orchestrator live proof (probe→build→inspect→repair→review; PIE behind --pie).
- **AO-2** Extend with member_patch/scene_batch/material stages + FP-1 verification.
- **AO-3** One-prompt feature demo through the orchestrator = the release demo.

### 6. Performance and long jobs
- **PJ-1** First live perf evidence file (harness ready; never run).
- **PJ-2** Round-trip budgets per workflow recorded in PERF_RUNBOOK.
- **PJ-3** Job API adopted for builds/cook/package/long PIE (on lane Z's base).

### 7. Installer, packaging, CI, security, release
- **RL-1** Packaged-zip LOADS proof (RELEASE.md 3a steps 5–7; the one unrun step).
- **RL-2** Zip config: project-derived fallback implemented and editor-free package acceptance green; native compile and two-project live proof remain.
- **RL-3** Teammate install proof executed by a human once.
- **RL-4** CI: editor-free jobs green in Actions; live suite list documented as human-run.
- **RL-5** Binary packaging: permanently blocked in 4.27 (BuildPluginCommand writes a one-plugin descriptor; cited). Source-zip is the ship vehicle. Document as engine limitation.

### 8. Final audit and simplification (Stage 2 only)
- **FA-1** Ponytail whole-repo audit; apply capability-preserving simplifications only.
- **FA-2** Strongest-model independent audit with the findings ledger as input.
- **FA-3** Known debt: 8 drifted asset-path resolver copies (lane W flag) · member_patch stores JSON quoting on set_variable_default where build does not (measured Unknown) · REFRONT_MAP/PLAN reconciliation notes · stale AGENTS.md lines ("widget_build has no inspector") · scoreboard regeneration.
- **FA-4** Full acceptance library run, clean project, warm+cold.
- **FA-5** Freeze, package, release.

## Reconciliations (asked and answered)

- **268 vs 196**: 72 registrations are aliases/duplicate names by design; aliases are compatibility surface, not capability. Count capabilities by canonical name only.
- **Default vs legacy**: 98 default-registered (55+40+3); 170 legacy behind MCP_ENABLE_LEGACY_HTTP=1. Legacy is migration fuel, never a fallback.
- **Verified vs implemented**: 19 vs 30. The 30 compile and load; they are RB-6.
- **REFRONT 28**: 6 are MERGE per REFRONT_MAP reconciliation → 22 genuine.
- **Stale roadmap rows**: P2 two-project test proven in 2fa0bca; P1 runbook teammate test still never run (RL-3).
- **Slice failures**: each maps to a work item above; none is unexplained.
- **Fixture drift**: fresh-path-per-run is law (0n); mutator-atomicity and member-patch acceptances comply; audit the rest in FA-3.
- **Uncompiled branches**: after wave six merges, none should remain; verify with `git branch --no-merged`.
- **Engine limits requiring async or honest blockers**: sequence_render (RB-4), Lightmass (start/status + swarm_available), UAT binary packaging (RL-5), EQS build (by-design refusal), cloth writers (read-only until transactional).

## Execution waves

Rules for every wave: one writer per worktree; only the integrator merges, one
branch at a time via merge-lane.mjs; one build owner and one editor owner per
installed project, max two editors; offline lanes never build; red acceptance
blocks promotion, not unrelated lanes; implemented_unverified is a valid
construction state; no duplicate transport/rollback/inspection/indexing/
registry/job systems; regenerate generated docs once per wave; typecheck both
tsconfigs before UBT; Ponytail review after each wave's merge set, non-blocking.

**Wave A (current, in flight)** — X slices · Y AnimBP snapshot · Z async jobs.
Integration checkpoint: merge X→Z→Y, batch compile, smoke, re-run slices.
Conflict files: the usual seven (see merge law). Editor/build: X.

**Wave B — joins and first live proof of the new surface**
- B1 (live; editor+build owner): gameplay slice join debug; materials/level live defects; RB-6 promotion sweep batch 1.
- B2 (offline): FP-1 runtime observation + FP-2 input sim re-front.
- B3 (offline): FP-3 delete-asset + FP-4 remove_unlisted components + FP-5 level commands.
Checkpoint: slices ui/ai/gameplay/materials/level green or each red check named as a work item. Acceptance gate: mcp-smoke 12/12, no regression in the five proven suites.

**Wave C — feature library alpha**
- C1 (live): acceptance library items 1–6 (health, sprint, interaction, doors, inventory, save/load) driven through the orchestrator (AO-2).
- C2 (offline): library items 7–12 authored against mocks, handed to integrator for live run.
- C3 (offline): PORT batch 1 (highest-value legacy ports with parity tests).
Checkpoint: ≥6 features prove warm+cold. Gate: zero dirty residue, all hashes independent.

**Wave D — long jobs and release engineering**
- D1 (live): sequence_render live proof on job API; lighting swarm_available; PJ-1 perf evidence.
- D2 (offline): RL-2 zip config; RL-4 CI wiring.
- D3 (human+integrator): RL-1 packaged-zip loads; RL-3 teammate proof.
Checkpoint: cinematics slice attempts; job API live_verified.

**Wave E — completion sweep**
- E1 (live): acceptance library 13–18 incl. Sequencer events and the packaged feature demo; remaining promotion sweep.
- E2 (offline): PORT batch 2, REFRONT remainder, MERGE/RETIRE metadata execution.
Checkpoint: Stage-1 definition met.

**Stage 2 (serial, freeze first)** — FA-1..FA-5. No new features. The strongest
available model runs the independent audit with CAPABILITY_FINDINGS.md and this
plan as input; its findings become the final repair list; then the full library
on a clean project, package, release.

## Stage definitions

**Stage 1 complete** when every domain row above has callable create/patch/
inspect/verify OR a documented engine blocker with a citation; all seven slices
attempt a complete result; the acceptance library exists and ≥12 features pass
live warm+cold; no `implemented` tool remains that has never been run live.

**Stage 2 complete (release)** when: features frozen; both audits run and their
blockers repaired; full library green on a clean project; packaged source zip
proven to LOAD and serve; teammate proof executed by a human; scoreboard
regenerated with accurate evidence for every default capability; CONTINUE_HERE
replaced by release notes.

## Critical path

Wave A merge → gameplay join (RB-2) → FP-1 runtime observation → orchestrator
live (AO-1/2) → feature library (C, E) → job API live + render (D) → audits →
release. Maximum safe parallelism: **3 lanes** while one installed target
exists (1 live + 2 offline); +1 offline lane only if it touches none of the
seven conflict files. The single editor is the bottleneck, not agent capacity.
