# Performance runbook

The exact commands that turn a live UE4.27 editor into a committed results file,
and how to tell a good result from a bad one.

Everything here needs a running editor. Nothing in this file can be done by an
agent that is forbidden from launching one. The design and the editor-free tests
are `docs/PERF_AND_LONG_JOBS.md` and `npm run test:perf`; this is the other half.

## Before you start

- One editor, one project. The harness addresses a session, never guesses a
  pipe, and refuses when it cannot tell which editor it is talking to. Two
  editors open is fine as long as `MCP_UNREAL_PROJECT_ROOT` names the one you
  mean.
- Close what you can. A shader compile, a source-control sync or an asset import
  in the background lands in the numbers, and the results file has no field that
  records "the machine was busy".
- Do not run this on a machine you are also using. A single Chrome tab doing
  layout work is visible in a p95 measured in milliseconds.

## 1. Build and gate

```powershell
cd 'D:\Unreal Projects\UE4_Bridge'
npm run verify
```

`verify` includes `npm run test:perf`. If it fails, stop: a harness that does not
pass its own unit tests is not measuring anything.

```powershell
npm run install:check -- --project 'D:\Unreal Projects\BridgeInstallTest'
```

This must pass before the editor is launched. The harness calls
`requireCurrentInstall()` itself and refuses on a mismatch, but finding out now
is cheaper than finding out after the editor is up. A stale install produces a
run that looks identical to a good one and proves nothing about this checkout.

## 2. Start the editor

```powershell
.\Scripts\start-ue4-project.ps1 -Project 'D:\Unreal Projects\BridgeInstallTest'
```

Wait for the project window to finish loading. Then confirm the bridge is up
before benchmarking anything, so a failure here is diagnosed as a bridge problem
rather than read as a slow number:

```powershell
$env:MCP_UNREAL_PROJECT_ROOT = 'D:\Unreal Projects\BridgeInstallTest'
Get-Content "$env:MCP_UNREAL_PROJECT_ROOT\Saved\MCPPuerTSBridge\session.json"
```

`shutdown_state` must be `running` and `editor_pid` must be a live process. If
the file is absent, the editor did not publish a session and the harness will
refuse with `session_missing`.

## 3. The read-only baseline

Run this first, every time. It mutates nothing except one property it writes
back to the value it already held, and it does not save.

```powershell
node Scripts\perf-benchmark.mjs --label baseline --runs 20 --warmup 3
```

Writes `docs/evidence/perf-run-baseline-<date>.json`.

Then run it **again**, unchanged:

```powershell
node Scripts\perf-benchmark.mjs --label baseline-b --runs 20 --warmup 3
node Scripts\perf-benchmark.mjs --label baseline-b --no-write `
  --baseline docs\evidence\perf-run-baseline-<date>.json
```

Two runs at the same commit on the same machine is not ceremony. One run
measures the machine's mood. Section 5 of `docs/PERF_AND_LONG_JOBS.md` will not
let a single run be called a baseline, and this is why.

## 4. The full run

Only after the baseline pair agrees. These write.

```powershell
# adds the compile pair and the authoring workflow.
# CREATES /Game/MCPGenerated/BP_BridgePerfFixture if it does not exist.
node Scripts\perf-benchmark.mjs --label full --runs 20 --warmup 3 `
  --include-compile --include-save --include-screenshot
```

PIE is separate and requires the user to ask for it. AGENTS.md forbids starting
PIE unprompted, and `pie_cycle` starts and stops a real play session:

```powershell
node Scripts\perf-benchmark.mjs --label pie --runs 20 --warmup 3 --include-pie
```

## 5. Comparing a change

```powershell
node Scripts\perf-benchmark.mjs --label after-<change> `
  --baseline docs\evidence\perf-run-baseline-<date>.json
```

`compareRuns` matches scenarios by name and prints the p50 and p95 delta.
Scenarios measured in only one of the two runs produce no row rather than a
fabricated zero.

Before reading any delta, check `environment.bridge_commit` and
`environment.editor.actor_count_total` in both files. A different commit or a
different level is a different measurement, not a regression.

## 6. What a good result looks like

A good run:

- **exit code 0**, a results file written, and it validated against
  `docs/evidence/perf-run.schema.json` before it was written.
- `totals.scenarios_skipped` accounted for. Every skip you did not ask for by
  omitting a flag is a finding. `no Blueprint under /Game to inspect` means the
  project is not representative; `no actor in the editor level` means the level
  is empty and `find_actors_500` measured an empty query.
- `find_actors_500` reports `target_met: true` (p95 under 50 ms), and
  `set_property_bHidden` reports `target_met: true` (p95 under 100 ms). Note the
  caveat that a 50 ms target for "500 actors" measured on a 56-actor level did
  not query 500 actors; `environment.editor.actor_count_total` is how you check.
- `client_round_trip_ms.max` within roughly 3x of `p50` for the cheap read
  scenarios. A max ten times the median means something else on the machine took
  the game thread, and the run should be repeated rather than interpreted.
- `blueprint_build_compile` clearly above `blueprint_build_no_compile`. The
  difference is the Blueprint compile. If they are equal, compile was skipped
  and the number is not a compile time.
- `workflow_survey_project` at exactly 4 round trips and
  `workflow_author_and_verify` at exactly 3. These counts are declared, not
  measured, so a mismatch means the workflow definition changed.
- Two runs at the same commit whose p95 values agree within a tolerance you
  state in whatever document cites them.

## 7. What a bad result looks like, and what it means

| Symptom | What it is |
|---|---|
| exit 2, `"refused": true`, `reason_code: "session_missing"` | no editor is serving that project root. Not a slow bridge. No file was written. |
| exit 2, `reason_code: "session_stale"` | the manifest names a dead pid. Check for exited-process remnants before launching another editor; see the handoff's debug section. |
| exit 2, `reason_code: "session_identity_mismatch"` | the reply came from a different editor than the one addressed. Stop and investigate; do not benchmark. |
| exit 1 with schema problems listed | the harness built a report the schema rejects. A code defect in the harness, not a measurement. |
| `install:check` refuses | the target's plugin differs from this checkout. Any numbers you got before this would have described a different build. |
| every scenario skipped with `warm-up failed` | the editor is up but refusing commands. Most likely PIE is running: `AcceptCommand` blocks editor-only tools during play. |
| `pie_cycle` skipped, `PIE never became playable` | the play session did not come up within 60 s, or `physics_observe` stopped answering. This is a finding about PIE, not about the harness. |
| `editor_startup_to_bridge_ready` skipped | the session manifest has no usable `process_start_time`; that field is Windows-only. |
| p95 far above p50 across every scenario | the machine was busy. Repeat the run. Do not average the two. |
| a p95 improvement with a higher `totals.round_trips` | not an improvement. AGENTS.md optimises for fewest round trips; check whether a workflow grew a step. |

## 8. What to commit

Commit the results files under `docs/evidence/`. They are small, they are the
only durable record of a live run, and the schema makes them readable by a later
tool without the prose around them.

Do not commit a results file as a baseline unless all five conditions in section
5 of `docs/PERF_AND_LONG_JOBS.md` hold. A file named `baseline` that was one run
on a busy machine is worse than no baseline, because the next person will
compare against it.
