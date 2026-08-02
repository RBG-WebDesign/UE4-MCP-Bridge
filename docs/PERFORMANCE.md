# Performance and long-running work

How the bridge is measured, what a benchmark run consists of, how runs are
compared, what the targets are, and what would have to change before a job that
outlives a command timeout can report progress or be cancelled.

Two things this document is careful about:

- The numbers in `docs/UE427_PUERST_MCP_HANDOFF_2026-07-31.md` section 9 are
  **observations**, not baselines. Section "Observations are not baselines"
  below says exactly what would make them baselines.
- Progress and cancellation for long jobs **do not exist**, and cannot be built
  inside the current command model. The last section says why, with the code
  that makes it true, and specifies the change that would be required.

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

| Scenario | Tool | Layer under test | Target |
|---|---|---|---|
| `diagnostic` | `puerts_diagnostic` | round-trip floor with a near-empty payload | none |
| `find_actors_500` | `puerts_find_actors` limit 500 | level query | p95 < 50 ms |
| `find_assets_blueprints` | `puerts_find_assets` | asset registry scan | none, recorded for regressions |
| `read_property_bHidden` | `puerts_read_property` | reflected read | none |
| `set_property_bHidden` | `puerts_set_property` | transacted mutation | p95 < 100 ms |
| `save_level` | `puerts_save` | package write | none; `--include-save` |
| `viewport_screenshot` | `puerts_viewport_screenshot` | render thread capture | none; `--include-screenshot` |

`set_property_bHidden` writes `bHidden` back to the value it already holds.
`Actor.bHidden` is on the native writable allowlist, so this exercises the full
mutation path (transaction open, `Modify`, `PostEditChangeProperty`,
`MarkPackageDirty`, transaction close) without changing what the level looks
like. **It does dirty the level package.** The default run does not save; the
save scenario is opt-in because it writes to disk.

Scenarios that need a live actor are skipped, with the reason recorded, when
the level has none.

### Round trips

The results file records `totals.round_trips`: every `tools/call` the run made,
including probes and warm-ups. Round-trip count is the number the product goal
in `AGENTS.md` is actually about. A change that halves per-call latency but
doubles the number of calls has made the bridge slower, and only this number
shows it.

Per-scenario `round_trips_per_iteration` is 1 for every scenario shipped here.
It exists so a composed workflow scenario, added later, can honestly declare
that one iteration cost five round trips.

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

Native command timeouts are clamped between 100 ms and 30 s
(`MCPPuerTSBridgeService.cpp`, `RequestTimeoutMilliseconds` clamp). The client's
default round-trip budget is 7 s, raised per tool to 15 s for the inspectors and
30 s for the builders. Anything that cannot finish inside 30 s cannot be a
single native command at all; that is the subject of section 6.

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

**Answer: not achievable within the current command model. Progress and
cancellation require a change to the command queue. This section specifies it
and deliberately ships no API, because an API that reports no progress is worse
than no API.**

### The constraint, from the code

Three facts, each read from the source rather than assumed:

1. **One command at a time, enforced natively.**
   `UMCPPuerTSBridgeService::AcceptCommand` opens with:

   ```cpp
   if (!ActiveCommandId.IsEmpty())
   {
       ... BuildErrorResponse(TEXT("Bridge is busy."),
           TEXT("Commands are serialized on the Unreal game thread."));
   }
   ```

   Any second command while one is active is refused. A `job_status` call is a
   second command.

2. **The runtime is single-threaded and pumped from the game thread.**
   `JsEnv.Build.cs` declares `NOT_THREAD_SAFE`. `FJsEnvImpl::UvRunOnce` runs
   `uv_run(&NodeUVLoop, UV_RUN_NOWAIT)` and is dispatched with
   `ENamedThreads::GameThread` (`JsEnvImpl.cpp:188`). The Node event loop
   therefore only turns when the game thread is free to turn it.

   This is the fact that kills naive progress. `puerts-runtime/src/bootstrap.ts`
   uses `net.createServer`, so a second pipe connection is accepted at the
   socket layer, and `commandQueue` chains the work. But while a long native
   call is executing, the game thread is inside that call, `uv_run` is not
   being reached, and the second socket's `data` event never fires. The status
   query is not merely refused; it is not even read off the pipe until the work
   it wants to report on has finished.

3. **The transaction is scoped to the command.**
   `AcceptCommand` constructs `ActiveTransaction` as an `FScopedTransaction`;
   `CompleteCommand` calls `EndActiveCommand`, which does
   `ActiveTransaction.Reset()`. A command that returns early to free the pipe
   also closes its transaction. Whatever the job does afterwards is outside it,
   and outside the rollback and undo guarantees `AGENTS.md` requires of every
   mutating tool.

Together: **a long job cannot report progress or accept a cancel while it holds
the game thread, no matter what is added on the client side.** Nothing in the
MCP server or the TypeScript runtime can work around a blocked game thread.

### What a client-side timeout does today, and why that is not cancellation

`PuerTSClient.call` has a per-tool budget (7 s default, 30 s for builders). When
it elapses, the client stops waiting. The editor does not stop working: the
native command keeps running, keeps its transaction open, and eventually calls
`CompleteCommand` into a socket nobody is reading. The caller sees a failure for
work that may have succeeded. This is the failure mode the raised per-tool
timeouts in `mcp-server/src/tools/puerts.ts` exist to avoid, and it is the exact
thing a real cancel API would have to replace.

### The change required

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

### What can be delivered without changing the command queue

Only this, and it should not be called progress:

- Every native response already carries `native_duration_ms`, and
  `puerts_blueprint_graph_patch` carries `elapsed_ms`. That is *after the fact*
  duration, not progress.
- A tool can return a `next_offset` so a caller can page a large scan across
  several short commands. The caller then knows how far it has got, because the
  caller is the one driving. This is the cheapest useful thing in the area and
  it needs no C++ change at all: it is a schema change per tool.
- The handoff target "progress for any task above 500 ms" is met for **no**
  tool today and cannot be met by any tool today.

### Recommended order

1. Paging (`next_offset`) on the scanning tools. No C++, real benefit.
2. Change 1 on one read-only operation, with the slice budget measured by
   `Scripts/perf-benchmark.mjs` before and after, to prove the editor stays
   responsive and the pipe stays serviceable mid-job.
3. Changes 2 and 3 only after 2 has a measured result. Do not add `job_start`,
   `job_status` or `job_cancel` to the native allowlist before then: registering
   them earlier advertises a capability that returns nothing.

## 7. Running the pieces

```bash
npm run test:perf                # unit tests, no editor needed; also in npm run verify
node Scripts/perf-benchmark.mjs  # live run; options are documented in the file header
```

`Scripts/perf-stats.test.mjs` covers the order statistics against known inputs,
the results-file schema round trip and its rejections, and both refusal paths
(no project root, and a project root no editor is serving). It does not and
cannot cover a measured run: that requires an editor.
