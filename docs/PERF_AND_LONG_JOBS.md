# Performance and long-running work

How the bridge is measured, what a benchmark run consists of, how runs are
compared, what the targets are, and what would have to change before a job that
outlives a command timeout can report progress or be cancelled.

Three things this document is careful about:

- The numbers in `docs/UE427_PUERST_MCP_HANDOFF_2026-07-31.md` section 9 are
  **observations**, not baselines. Section 5 says exactly what would make them
  baselines.
- A scenario that cannot run is recorded as skipped with its reason. Nothing in
  the harness reports a zero for work that did not happen.
- Progress and cancellation for long jobs **do not exist today**. Section 6
  answers whether they are possible at all, with a file and line for every
  claim about how the command queue behaves, and ships no API, because an API
  that cannot report progress is worse than none.

The runbook for producing a results file against a live editor is
`docs/evidence/PERF_RUNBOOK.md`.

## 1. The layers, and what each one can be measured at

A single tool call passes through six places where time is spent. They are
measured separately because a regression in one looks exactly like a regression
in another from the outside.

| Layer | What it covers | How it is measured today |
|---|---|---|
| Client round trip | MCP `tools/call` in, JSON text out. Includes everything below. | `Scripts/perf-benchmark.mjs`, `process.hrtime.bigint()` around the stdio call |
| MCP server | Zod parse, structured-parameter decode, response stringify | not separately instrumented; appears as round trip minus native duration |
| Named pipe | connect, write one line, read one line, close | not separately instrumented; same residual as above |
| Runtime (PuerTS) | registry dispatch, reflection, result assembly | not separately instrumented |
| Native command | `AcceptCommand` to `CompleteCommand` on the game thread | `native_duration_ms`, set by the C++ service in `CompleteCommand` and present on **every** native response |
| Named sub-phases | actor query, JSON serialization | `puerts_diagnostic` reports `native_actor_query_ms` and `json_snapshot_serialization_ms`; `puerts_blueprint_graph_patch` reports `elapsed_ms` |
| Compile | Blueprint compile inside a build | not separated from the build command's total |
| Save | package write | only as the total of a `puerts_save` call |

The gap between client round trip and `native_duration_ms` is the bridge's own
overhead: MCP parse, pipe, and runtime dispatch. The harness records both for
every scenario, so that gap is a derived number rather than a guess.

**Not instrumented, and honestly so:** the pipe alone, the runtime dispatch
alone, and compile as distinct from build. Adding a per-layer timestamp to the
wire envelope would fix all three; that is a separate change to the response
contract and is not part of this wave.

## 2. What a benchmark run is

`Scripts/perf-benchmark.mjs`, driven by the integrator against a live editor:

```bash
node Scripts/perf-benchmark.mjs --label baseline --runs 20 --warmup 3
node Scripts/perf-benchmark.mjs --label after-change --baseline docs/evidence/perf-run-baseline-2026-08-02.json
```

A run is:

1. `requireCurrentInstall()`. A run against a stale install in the target
   project proves nothing about this checkout and looks identical to a passing
   one, so it is refused before the editor is touched.
2. Spawn the built server on stdio and `initialize`, exactly as a real client.
3. **Probe** with `puerts_diagnostic`. If the probe does not succeed, the run
   stops, prints the structured refusal (`session_missing`, `session_stale`,
   `session_shut_down`, `session_project_mismatch`, and so on, taken from the
   client's own `session_error_code`), writes no results file, and exits 2.
   There is no mode in which the harness produces a number without an editor.
4. Per scenario: `--warmup` calls that are discarded, then `--runs` calls that
   are timed. A failure during the measured phase skips the whole scenario with
   the editor's error recorded, rather than reporting a partial distribution.
5. Write `docs/evidence/perf-run-<label>-<date>.json`, validated against
   `docs/evidence/perf-run.schema.json` before it is written.

Warm-up is discarded on purpose. The first call after an idle editor pays for
asset loads, lazily-built asset-registry state and a cold pipe; including it
turns a 20-sample distribution into a 19-sample distribution plus one outlier
that dominates max.

### Scenarios

Per-command scenarios. One round trip per iteration, `--runs` iterations each.

| Scenario | Tool | Layer under test | Target / gate |
|---|---|---|---|
| `diagnostic` | `puerts_diagnostic` | round-trip floor with a near-empty payload | none |
| `find_actors_500` | `puerts_find_actors` limit 500 | level query | p95 < 50 ms |
| `find_assets_blueprints` | `puerts_find_assets` | asset registry scan | none, recorded for regressions |
| `read_property_bHidden` | `puerts_read_property` | reflected read | none |
| `set_property_bHidden` | `puerts_set_property` | transacted mutation | p95 < 100 ms |
| `graph_inspect` | `puerts_graph_inspect` | Blueprint read, no pins | none |
| `graph_inspect_with_pins` | `puerts_graph_inspect` | the same read with pin serialization | none |
| `blueprint_build_no_compile` | `puerts_blueprint_build` | convergent no-op build, compile off | `--include-compile` |
| `blueprint_build_compile` | `puerts_blueprint_build` | the same build with compile on | `--include-compile` |
| `save_level` | `puerts_save` | package write | `--include-save` |
| `viewport_screenshot` | `puerts_viewport_screenshot` | render thread capture | `--include-screenshot` |

Composed scenarios. One iteration is a whole sequence, and the headline number
is the round-trip count.

| Scenario | Shape | Round trips | Gate |
|---|---|---|---|
| `workflow_survey_project` | diagnostic, find_actors, find_assets, graph_inspect | 4 | needs one Blueprint under `/Game` |
| `workflow_author_and_verify` | build plan_only, build with compile, graph_inspect | 3 | `--include-compile` |
| `pie_cycle` | start, poll to a PIE world, stop, poll back to the editor world | variable, counted | `--include-pie` |
| `editor_startup_to_bridge_ready` | derived from the session manifest | 0 | always |

Notes that change how the numbers should be read:

- `set_property_bHidden` writes `bHidden` back to the value it already holds.
  `Actor.bHidden` is on the native writable allowlist, so this exercises the
  full mutation path (transaction open, `Modify`, `PostEditChangeProperty`,
  `MarkPackageDirty`, transaction close) without changing what the level looks
  like. **It does dirty the level package.** The default run does not save.
- **The compile cost is a difference, not a measurement.**
  `blueprint_build_compile` minus `blueprint_build_no_compile` is
  `FKismetEditorUtilities::CompileBlueprint`
  (`BlueprintGraphBuilderLibrary.cpp:1950`). The bridge cannot time the compile
  separately today because nothing instruments inside the build command, so the
  difference between two otherwise identical runs is the only honest way to get
  it. Both are convergent no-op builds against the same fixture, so the only
  thing that differs is the compile.
- `--include-compile` **creates** `/Game/MCPGenerated/BP_BridgePerfFixture` if it
  is absent. That is a mutation, which is why it is opt-in and why the fixture
  path is overridable with `--blueprint`.
- `pie_cycle` polls `puerts_physics_observe`, one of the three tools
  `AcceptCommand` permits during play (`MCPPuerTSBridgeService.cpp:495-499`),
  and waits for its `world` field (`MCPBridgePuerTSPhysics.cpp:298`) to flip.
  Timing the `pie_start` round trip alone would be meaningless:
  `RequestPlaySession` only queues (`MCPPuerTSBridgeService.cpp:1177`), and a
  stop sent immediately after cancels the queued request rather than ending a
  session (`:1193-1195`), so the pair would report a PIE session that never
  happened. Every poll counts as a round trip.
- `editor_startup_to_bridge_ready` is `created_at` minus `process_start_time`
  from `Saved/MCPPuerTSBridge/session.json`: OS process creation
  (`MCPPuerTSBridgeService.cpp:62-76`) to the moment the bridge first published
  a session (`:290-292`). It measures an editor restart without performing one,
  which matters because the harness is not permitted to launch editors. It is
  one sample per editor session and cannot be repeated within a run.

### Skipped is not zero

A scenario that cannot run records `status: "skipped"` with a `skip_reason`, and
carries no statistics at all. There is no path that writes a zero or an empty
distribution for work that did not happen, because a zero sorts as the fastest
result and a missing scenario is not a fast one.

The reasons a scenario skips, all recorded verbatim: it was not requested, the
level has no actor, the project has no Blueprint, a warm-up call failed, a
measured iteration failed, a workflow step failed (named, with its index), PIE
never became playable within the timeout, the editor was already playing, or the
session manifest has no usable timestamp pair. A failure part way through a
scenario skips the **whole** scenario rather than reporting a distribution over
whichever iterations happened to succeed.

### Round trips

The results file records `totals.round_trips`: every `tools/call` the run made,
including probes, warm-ups and PIE readiness polls. Round-trip count is the
number the product goal in AGENTS.md is actually about. A change that halves
per-call latency but doubles the number of calls has made the bridge slower, and
only this number shows it.

Per-scenario `round_trips_per_iteration` is 1 for the per-command scenarios, the
step count for a workflow, and the real poll-inclusive count for `pie_cycle`.
Workflows also carry a `steps[]` breakdown, so a workflow that got slower names
the step that did it without another editor session.

## 3. Results file format

`docs/evidence/perf-run.schema.json` is the only definition of the format.
`validateRunReport` in `Scripts/perf-stats.mjs` reads that file rather than
restating its fields, so the schema and the validator cannot drift.

The shape:

```text
schema_version, kind: "bridge-perf-run"
run          run_id, label, started_at, finished_at, iterations, warmup
environment  host, platform, cpu_model, cpu_count, node_version,
             bridge_commit, project_root, editor{session_id, editor_pid,
             actor_count_total, is_game_thread}
scenarios[]  name, tool, status(measured|skipped), skip_reason, layer,
             iterations, round_trips_per_iteration,
             client_round_trip_ms{count,min,p50,p95,max,mean,samples},
             native_duration_ms{...}, response_bytes{...},
             target_ms, target_met, target_basis
totals       round_trips, wall_ms, scenarios_measured, scenarios_skipped
```

Percentiles are **nearest-rank**: the value at `ceil(p * n)` in ascending order,
no interpolation. Every reported percentile is therefore a measurement that
actually happened rather than an average of two that did not. With `--runs 20`,
p95 is the second-slowest sample.

Raw `samples` are kept for the latency distributions so a later question ("what
was p99?", "was it bimodal?") can be answered from the committed file without
another editor session. `response_bytes` drops its samples: the payload size is
near-constant per scenario and twenty copies of it is noise.

`environment.bridge_commit` and `environment.editor.actor_count_total` are the
two fields that make one run comparable to another. A run at a different commit,
or against a level with a different actor count, is a different measurement, not
a regression.

Comparison is `--baseline <file>`: `compareRuns` matches scenarios by name and
prints the p50 and p95 delta as a percentage. Scenarios measured in only one of
the two runs produce no row, rather than a fabricated zero.

## 4. Targets

From handoff section 9, restated here as what the harness checks:

| Target | Scenario | Status |
|---|---|---|
| p95 query of 500 loaded actors < 50 ms | `find_actors_500` | checked, reported as `target_met` |
| p95 simple property mutation < 100 ms, excluding save and compile | `set_property_bHidden` | checked, reported as `target_met` |
| progress for any task above 500 ms | none | **not achievable today**, see section 6 |

`target_met` is reported, not enforced: a missed target on one machine is
information, and a harness that fails a build on it would be turned off. The
integrator decides when a target becomes a gate.

Timeouts, stated precisely because they are easy to misread as work deadlines:

| Number | Where | What it actually bounds |
|---|---|---|
| 100 ms to 30 s clamp | `MCPPuerTSBridgeService.cpp:224` | the socket idle budget handed to `bootstrap.ts:71`. Not a work deadline. |
| 7 s / 15 s / 30 s per tool | `mcp-server/src/tools/puerts.ts:615-626` | how long the **client** waits. The editor keeps working past it. |
| `executionTimeoutMs` | `registry.ts:757-766` | nothing today. See section 6.1 F4. |

Nothing aborts a running native command. Anything that cannot finish inside the
client's budget is not a bounded operation, it is an unbounded one whose caller
gave up; that is the subject of section 6.

## 5. Observations are not baselines

Handoff section 9 records, from an isolated 56-actor test scene:

| Observation | Value |
|---|---|
| native actor query | 0.036 - 0.057 ms |
| JSON serialization | ~0.286 ms |
| total diagnostic | 1.2 - 1.6 ms |
| a sky call's native duration | ~1.86 ms |
| save | ~100.7 ms |
| screenshot | ~200.9 ms |

These are single-machine, single-session, single-sample observations. They are
useful as an order of magnitude and useless as a regression gate: there is no
recorded sample count, no distribution, no machine description, no commit, and
no scene description beyond the actor count.

They become baselines when all of the following hold:

1. A results file exists at `docs/evidence/perf-run-baseline-<date>.json`,
   produced by `Scripts/perf-benchmark.mjs`, validating against the schema.
2. It records `bridge_commit`, the machine, and `editor.actor_count_total`, so
   a later run can state whether it is comparable.
3. Each figure is a distribution over at least 20 measured iterations after
   warm-up, not one sample.
4. The run is reproduced at least once on the same machine at the same commit,
   and the two runs' p95 values agree within a stated tolerance. One run
   measures the machine's mood; two agreeing runs measure the bridge.
5. The document naming the baseline says which scene it was taken on. A p95 for
   "query 500 actors" taken on a 56-actor level did not query 500 actors.

Until then, nothing in this repository should be described as an SLO, and no
change should be described as a regression or an improvement on the strength of
the section 9 numbers alone.

## 6. Long jobs: progress, cancellation and status

**Answer, in one line: progress is reachable with a change that does not touch
the command queue; cancellation is not reachable at all for the operations that
need it most, and no amount of client-side work changes either.**

The long form is below, with a file and line for every claim about how the queue
behaves. Nothing here ships an API. An API that cannot report progress is worse
than no API, because a caller that polls it learns nothing and concludes the job
is stuck.

Reproduce the citations:

```bash
git grep -n "ActiveCommandId.IsEmpty" Plugins/MCPBridge/Source/MCPBridgePuerTS
git grep -n "commandQueue\|AcceptCommand\|setTimeout" puerts-runtime/src/bootstrap.ts
git grep -cn "await " puerts-runtime/src/registry.ts        # expect 1
```

### 6.1 The model, from the source

Six facts. Each was read out of the file named, not inferred from behaviour.

**F1. One command at a time, refused in C++.**
`UMCPPuerTSBridgeService::AcceptCommand` opens with a busy check
(`MCPPuerTSBridgeService.cpp:415-421`):

```cpp
if (!ActiveCommandId.IsEmpty())
{
    ... BuildErrorResponse(TEXT("Bridge is busy."),
        TEXT("Commands are serialized on the Unreal game thread."));
}
```

`ActiveCommandId` is set at `MCPPuerTSBridgeService.cpp:516` and cleared by
`EndActiveCommand` at `1419-1430`, called from `CompleteCommand` at `597`. A
`job_status` call is a second command, so this check alone refuses it.

**F2. The script layer serializes before the C++ check is even reached.**
`puerts-runtime/src/bootstrap.ts:44` holds `commandQueue: Promise<void>`, and
`:86` chains every incoming line onto it. `AcceptCommand` is called from inside
`executeLine` at `:49`. So a second request waits in the JavaScript chain and
never reaches the busy check while the first is running. Two independent
serialization points, one in each language.

**F3. The Node event loop turns on the game thread, and only when it is free.**
`JsEnv.Build.cs:42,64` sets `ThreadSafe = false` and defines `NOT_THREAD_SAFE`.
`FJsEnvImpl::StartPolling` dispatches `UvRunOnce` with
`ENamedThreads::GameThread` (`JsEnvImpl.cpp:187-188`), and `UvRunOnce` calls
`uv_run(&NodeUVLoop, UV_RUN_NOWAIT)` (`JsEnvImpl.cpp:233`). Both files live in
the PuerTS bundle, which is gitignored; `Plugins/Puerts.lock.json` pins its
contents, so these line numbers are stable for the pinned revision.

This is the fact that kills naive progress. `bootstrap.ts` uses
`net.createServer`, so a second pipe connection is accepted by the OS. But while
a long native call runs, the game thread is inside that call, `uv_run` is never
reached, and the second socket's `data` event never fires. A status query is not
merely refused. It is not read off the pipe until the work it asks about has
already finished.

**F4. Every registered tool blocks the loop synchronously, so the runtime's own
timeout cannot fire.**
`ToolRegistry.execute` races the tool against a `setTimeout`
(`registry.ts:757-766`). That guard is correct in principle and inert in
practice: `grep -c "await " puerts-runtime/src/registry.ts` returns **1**, and
that one hit is the `Promise.race` on line 766 itself. Every tool body is a
straight-line synchronous call into C++ (`registry.ts:306`
`context.bridge.BuildBlueprintJson(...)`, `:606` `ObservePhysicsSceneJson`,
`:628` `SaveProjectAsset`, and so on). An `async` function with no `await` runs
to completion before it returns, so by the time the loop could service the
timer, the work is done and `finally` clears it.

`executionTimeoutMs` is therefore **not enforced for any tool in the registry
today**. It will start working the day a tool awaits something real. Until then
it should not be cited as a bound on anything.

**F5. The 30 s ceiling is a socket idle budget, not a work deadline.**
`RequestTimeoutMilliseconds` is clamped to `[100, 30000]` at
`MCPPuerTSBridgeService.cpp:224` and read by `GetRequestTimeoutMilliseconds` at
`:1298`. Its only consumer is `bootstrap.ts:71`, which sets
`socket.setTimeout(budget + 2000)`. That callback is a libuv timer, so by F3 it
cannot fire while the game thread is blocked either. Nothing anywhere aborts a
running native command. The clamp bounds a number; it does not bound work.

**F6. The transaction dies with the command.**
`AcceptCommand` constructs `ActiveTransaction` as an `FScopedTransaction` at
`MCPPuerTSBridgeService.cpp:522-525`, and `EndActiveCommand` calls
`ActiveTransaction.Reset()` at `:1421`. A command that returns early to free the
pipe closes its transaction on the way out. Whatever it does afterwards is
outside the transaction, and outside the rollback and undo guarantees AGENTS.md
requires of every mutating tool.

Corroboration that this is understood in the code and not only here: the session
heartbeat carries a comment saying it stalls for exactly this reason
(`MCPPuerTSBridgeService.cpp:294-300`, "it runs on the game thread, so a long
Blueprint compile stalls it while the editor is perfectly alive").

### 6.2 What a client timeout does today, and why it is not cancellation

`PuerTSClient.call` has a per-tool budget: 7 s by default, 15 s for the
inspectors, 30 s for the builders (`mcp-server/src/tools/puerts.ts:615-626`).
When it elapses the client stops waiting. The editor does not stop working. The
native command keeps running, keeps its transaction open, and eventually calls
`CompleteCommand` into a socket nobody is reading. The caller sees a failure for
work that may well have succeeded, and by F6 the transaction it would need to
undo has already closed under an id it never received.

That is the failure the raised per-tool budgets exist to avoid, and it is the
exact behaviour a real cancel would have to replace. It is a client giving up,
not a job stopping.

### 6.3 Progress without changing the command queue

There is a cheaper answer than restructuring the queue, and it is worth stating
before the expensive one because it is the one to build first.

**A read-only status channel that never touches the game thread.** The blocked
resource is the game thread. Nothing forces status to be served from it.

- The service gains a small progress record: an atomic step counter, an atomic
  total, and a stage string behind a critical section, written by the game
  thread at checkpoints it already passes through.
- A background `FRunnable` serves a second named pipe. It reads the record and
  answers. It calls no `UObject` API, takes no game-thread lock, and touches no
  Unreal container that the game thread mutates. It is answerable while the game
  thread is inside a compile.
- The MCP server polls that pipe on its own timer and emits MCP
  `notifications/progress`, which the SDK already supports and which nothing in
  `mcp-server/src/` uses today (`git grep -n progressToken mcp-server/src`
  returns nothing).

This changes no queue behaviour: `AcceptCommand` still refuses concurrent
commands, the runtime is still serialized, F1 through F6 all still hold. The
status channel is not a command.

What it costs, honestly:

- A second pipe means a second ACL and a second auth decision. The existing pipe
  took a bearer token plus a session nonce to get right; this one carries no
  authority to change anything, but it does leak asset paths and progress, so it
  needs at least the same token.
- Every long operation must be instrumented with checkpoints. Nothing reports
  progress for free.
- **The checkpoints do not exist where the time goes.** The two dominant costs
  are single engine calls with no interior:
  `FKismetEditorUtilities::CompileBlueprint`
  (`BlueprintGraphBuilderLibrary.cpp:1950`) and
  `UEditorLoadingAndSavingUtils::SavePackages`
  (`MCPPuerTSBridgeService.cpp:1126`). A progress bar over an eleven-pass
  Blueprint build would move through the passes and then sit motionless on
  "compiling" for most of the wall time.

That last point is the honest limit. This design reports *which stage*, not *how
far through the slow stage*. That is still worth having, because "stage 9 of 11,
compiling" and "no response for 40 s" are different messages to a caller, and
only one of them prevents a client from giving up on live work. It should be
named `stage`, not `percent`, so nobody reads a promise into it.

### 6.4 Cancellation, and why it is a different answer

Cancellation cannot be delivered by the status channel, because acting on it
means the game thread has to notice a flag and unwind. That requires:

1. A cancel point in the operation. By F3 the operation must reach one while
   holding the game thread, so it must be a check the bridge's own code
   performs between units of work it owns.
2. A defined state to unwind to. By F6 an `FScopedTransaction` unwinds correctly
   only if the whole command aborts, which is the one case where a cancel is
   pointless.
3. Nothing uninterruptible in between. `CompileBlueprint` and `SavePackages` are
   uninterruptible, and they are where the seconds are.

So the honest scope for cancellation is: **between the passes the bridge owns,
and never inside an engine call**. A cancel requested during a compile takes
effect after the compile finishes. If that is shipped, the response must say so
per job, with a `cancellable_now` flag that is false while inside such a call.
An API that accepts a cancel and then appears to ignore it for 40 s is worse
than one that refuses.

For a genuinely long job to be cancellable at a useful granularity, it must stop
holding the game thread at all, which is section 6.5.

### 6.5 The change required for real long jobs

The job must not hold the game thread, and must not hold the active-command
slot. That means three changes, in this order. None is a client-side change.

**Change 1: chunked execution on the game-thread ticker.**
A long operation is restructured as a state machine that does a bounded slice of
work per `FTicker` callback and returns. Between slices the game thread is free,
so `UvRunOnce` is reached, so the pipe is serviced. Without this, changes 2 and
3 are decorative. The slice budget must be a config value (start at 5 ms of
game-thread time per tick, measured, not assumed) because it trades job
throughput directly against editor responsiveness.

**Change 2: a job slot in the service, separate from the command slot.**
`UMCPPuerTSBridgeService` gains `TMap<FString, FBridgeJob>` where `FBridgeJob`
holds: `JobId`, `ToolName`, `State` (`queued`/`running`/`succeeded`/`failed`/
`cancelled`), `StepsDone`, `StepsTotal`, `StartedAtSeconds`,
`LastProgressAtSeconds`, `Message`, `bCancelRequested`, and the completed
result. `AcceptCommand`'s busy check stays exactly as it is for commands; job
state lives beside it, not inside `ActiveCommandId`.

The starting command (`..._start`) accepts, registers the job, schedules the
ticker, and calls `CompleteCommand` immediately with `{job_id, state:"queued"}`.
The command slot is free from that moment. `job_status` and `job_cancel` are
ordinary short commands that read and write the job map; they are fast and
never block, so they cannot deadlock behind the job.

Cancellation is cooperative and cannot be otherwise: `job_cancel` sets
`bCancelRequested`, and the next tick slice observes it and unwinds. A job that
enters a single uninterruptible engine call (a Blueprint compile, a package
save) is not cancellable during that call, and the API must say so per job
rather than promise a cancel it cannot deliver. `FBridgeJob` therefore also
needs `bCancellableNow`, set false while inside such a call.

**Change 3: transaction ownership moves to the job.**
`FScopedTransaction` is stack-scoped and cannot span ticks. A chunked job needs
`GEditor->BeginTransaction` / `EndTransaction` (or `CancelTransaction` on the
cancel path) held by the job record, with the same rollback ledger the builders
already use. This is the riskiest of the three: an editor transaction held open
across many ticks is visible to the user, and anything else that opens a
transaction meanwhile nests inside it. Until this is designed and tested, the
honest scope for job-based work is **read-only** long jobs (large scans,
inspections, reports), where no transaction is needed at all.

### 6.6 What each change breaks

Not risks. Things that stop working the way they do now, and would need a
decision before any of this lands.

| Change | What breaks |
|---|---|
| 1, chunked execution | Editor state can change between slices. Today a command sees one consistent world for its whole run because nothing else gets a turn. A chunked builder can have an asset renamed, an actor deleted or a PIE session started underneath it, so every slice has to revalidate what the previous slice resolved. That is the real cost, and it is much larger than writing the state machine. |
| 1, chunked execution | `native_duration_ms` stops meaning what it means today. It is wall time from `AcceptCommand` to `CompleteCommand` (`MCPPuerTSBridgeService.cpp:519,574-575`). Across slices that becomes wall time including everything else the editor did, and the perf harness's compile scenario silently starts measuring the editor's mood. A job would need game-thread time accumulated per slice, which is a different field. |
| 2, separate job slot | The PIE guard is per-command (`MCPPuerTSBridgeService.cpp:495-502`). A job running across ticks can have PIE start under it, and that guard never re-runs. Either jobs must block PIE or the guard must move into the slice. |
| 2, separate job slot | The response contract gains a second shape. Every caller of `..._start` now gets `{job_id, state}` where it used to get a result, so `changed_assets` and `changed_actors` arrive later or not at all. This is a wire-contract version bump, not an additive field. |
| 3, job-owned transaction | Undo stops being one entry. Today `puerts_undo` undoes the exact last MCP transaction by id (`MCPPuerTSBridgeService.cpp:1214`). A transaction held open across ticks is visible in the editor's undo buffer while it is open, and anything the user does meanwhile nests inside it. The undo contract has to be redesigned before this ships, not after. |
| 3, job-owned transaction | Editor shutdown during a job. The bridge releases from `OnEnginePreExit` (`MCPPuerTSBridgeModule.cpp:64-65`), and an open `GEditor` transaction at that point is a new teardown path on the exact code that produced the unkillable-editor defect. |

Because of rows 5 and 6, the honest scope for job-based work until the undo
contract is redesigned is **read-only**: large scans, inspections and reports,
where no transaction is opened at all.

### 6.7 What is already true today, and is not progress

- Every native response carries `native_duration_ms`
  (`MCPPuerTSBridgeService.cpp:574-575`), and `puerts_blueprint_graph_patch`
  carries `elapsed_ms`. Both are after-the-fact durations.
- A tool can return a `next_offset` so a caller pages a large scan across
  several short commands, and then knows how far it has got because it is the
  one driving. No C++ change at all: a schema change per tool. This is the
  cheapest useful thing in the area.
- The handoff's target "progress for any task above 500 ms" is met by **no**
  tool today and cannot be met by any tool today.

### 6.8 Recommended order

1. Paging (`next_offset`) on the scanning tools. No C++, real benefit, no
   contract change beyond one optional field.
2. The status side channel of 6.3, stage-only, on the Blueprint builder. It is
   the only item here that improves a caller's experience without changing how
   commands run, and it is the one that proves whether stage-level reporting is
   worth anything before the expensive work starts.
3. Change 1 on one read-only operation, with the slice budget measured by
   `Scripts/perf-benchmark.mjs` before and after, to prove the editor stays
   responsive and the pipe stays serviceable mid-job.
4. Changes 2 and 3 only after 3 has a measured result, and only after the undo
   contract question in 6.6 has an answer. Do not add `job_start`, `job_status`
   or `job_cancel` to the native allowlist before then: registering them earlier
   advertises a capability that returns nothing.

None of steps 1 to 4 is a client-side change, and no client-side change can
substitute for any of them.

## 7. Running the pieces

```bash
npm run test:perf                # unit tests, no editor needed; also in npm run verify
node Scripts/perf-benchmark.mjs  # live run; options are in the file header and the runbook
```

`npm run test:perf` is `node Scripts/perf-stats.test.mjs` and is chained into
`npm run verify` in the root `package.json`, so the harness cannot rot silently.
It covers the order statistics against known inputs, the results-file schema
round trip and its rejections, all three scenario runners against a stub `call`
including every skip path, the editor-startup derivation, and both refusal paths
(no project root, and a project root no editor is serving).

**What it does not cover, and cannot:** a measured run. Everything above is a
stub or a mock. No number in this document was produced by a live editor, and
compilation and mocks are not live verification. `docs/evidence/PERF_RUNBOOK.md`
is the procedure that produces the first real one.
