#!/usr/bin/env node
/**
 * Unit tests for the performance harness. No editor, no network, no build
 * artefacts: the statistics, the results-file schema round trip, and the
 * harness's refusal path.
 *
 *   node Scripts/perf-stats.test.mjs
 */
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  buildRunReport, checkShape, compareRuns, deriveEditorStartupScenario, describeRefusal,
  describeShape, percentile, runPieCycleScenario, runSchema, runScenario, runWorkflowScenario,
  summarize, validateRunReport,
} from "./perf-stats.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
let passed = 0;
function ok(label) { console.log(`  PASS  ${label}`); passed += 1; }

// ------------------------------------------------------------- statistics

// Nearest-rank over 1..10: p50 is the 5th value, p95 is the 10th. Stated as
// literals so a change to the percentile method fails here instead of being
// absorbed into a plausible-looking number.
const ten = [10, 3, 7, 1, 9, 4, 6, 2, 8, 5];
assert.equal(percentile(ten, 0.5), 5);
assert.equal(percentile(ten, 0.95), 10);
assert.equal(percentile(ten, 0), 1);
assert.equal(percentile(ten, 1), 10);
ok("nearest-rank percentiles over a known ten-sample set");

const stats = summarize(ten);
assert.deepEqual(
  { count: stats.count, min: stats.min, p50: stats.p50, p95: stats.p95, max: stats.max, mean: stats.mean },
  { count: 10, min: 1, p50: 5, p95: 10, max: 10, mean: 5.5 },
);
assert.deepEqual(stats.samples, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
ok("summarize reports count/min/p50/p95/max/mean and the sorted samples");

const single = summarize([42]);
assert.deepEqual(
  { count: single.count, p50: single.p50, p95: single.p95, max: single.max, mean: single.mean },
  { count: 1, p50: 42, p95: 42, max: 42, mean: 42 },
);
ok("a single sample makes every order statistic that sample");

assert.equal(summarize([]), null);
assert.equal(summarize(undefined), null);
assert.equal(percentile([], 0.5), null);
ok("no samples produces null, not zero: an unmeasured scenario is not a fast one");

assert.equal(summarize([1, 2], { keepSamples: false }).samples, undefined);
ok("keepSamples:false omits the raw samples");

// A distribution whose p95 is far from its mean: the point of reporting p95.
const skewed = [...Array(19).fill(5), 500];
const skew = summarize(skewed);
assert.equal(skew.p50, 5);
assert.equal(skew.p95, 5);
assert.equal(skew.max, 500);
ok("one outlier in twenty moves max but not p95, and p95 is not the max");

// ------------------------------------------------------ schema round trip

function sampleReport() {
  return buildRunReport({
    run: {
      run_id: "test-2026-08-02",
      label: "test",
      started_at: "2026-08-02T00:00:00.000Z",
      finished_at: "2026-08-02T00:00:10.000Z",
      iterations: 20,
      warmup: 3,
    },
    environment: {
      host: "testhost",
      platform: "win32",
      cpu_model: "Test CPU",
      cpu_count: 8,
      total_memory_bytes: 34359738368,
      node_version: "v20.0.0",
      bridge_commit: "0".repeat(40),
      project_root: "D:/Projects/Test",
      editor: { session_id: "s1", editor_pid: 1234, actor_count_total: 56, is_game_thread: true },
    },
    scenarios: [
      {
        name: "diagnostic",
        tool: "puerts_diagnostic",
        status: "measured",
        layer: "client_round_trip",
        iterations: 20,
        round_trips_per_iteration: 1,
        client_round_trip_ms: summarize(ten),
        native_duration_ms: summarize([1.2, 1.4, 1.6]),
        response_bytes: summarize([400, 402], { keepSamples: false }),
        target_ms: null,
        target_met: null,
      },
      {
        name: "save_level",
        tool: "puerts_save",
        status: "skipped",
        skip_reason: "not requested",
        iterations: 0,
        round_trips_per_iteration: 1,
        target_ms: null,
        target_met: null,
      },
    ],
    wallMs: 10000,
    roundTrips: 46,
  });
}

const report = sampleReport();
assert.deepEqual(validateRunReport(report), { ok: true, problems: [] });
ok("a freshly built report validates against docs/evidence/perf-run.schema.json");

const reparsed = JSON.parse(JSON.stringify(report));
assert.deepEqual(reparsed, report);
assert.deepEqual(validateRunReport(reparsed), { ok: true, problems: [] });
ok("the report survives a JSON write/read round trip unchanged and still validates");

assert.equal(report.totals.scenarios_measured, 1);
assert.equal(report.totals.scenarios_skipped, 1);
assert.equal(report.totals.round_trips, 46);
ok("totals count measured and skipped scenarios and carry the round-trip count");

// The validator has to actually reject. Each of these is a real way a results
// file goes wrong: a dropped section, a stringified number, an invented status.
function rejects(mutate, expectedFragment) {
  const broken = sampleReport();
  mutate(broken);
  const { ok: valid, problems } = validateRunReport(broken);
  assert.equal(valid, false, `expected a rejection mentioning ${expectedFragment}`);
  assert.ok(problems.some((p) => p.includes(expectedFragment)),
    `expected a problem mentioning "${expectedFragment}", got: ${problems.join(" | ")}`);
}

rejects((r) => { delete r.scenarios; }, "scenarios");
rejects((r) => { delete r.environment.bridge_commit; }, "bridge_commit");
rejects((r) => { r.totals.wall_ms = "10000"; }, "wall_ms");
rejects((r) => { r.scenarios[0].status = "probably_fine"; }, "probably_fine");
rejects((r) => { delete r.scenarios[0].client_round_trip_ms.p95; }, "p95");
rejects((r) => { r.scenarios[0].client_round_trip_ms.samples = ["fast"]; }, "samples[0]");
rejects((r) => { r.kind = "something-else"; }, "kind");
ok("the validator rejects a missing section, a missing field, a wrong type, a bad enum and a bad array element");

assert.equal(runSchema.$schema, "http://json-schema.org/draft-07/schema#");
assert.ok(runSchema.required.includes("scenarios"));
ok("the validator is driven by the committed schema file, not a second copy in code");

// -------------------------------------------------------------- compare

const older = sampleReport();
older.scenarios[0].client_round_trip_ms = summarize([2, 2, 2, 2, 2, 2, 2, 2, 2, 2]);
const rows = compareRuns(report, older);
assert.equal(rows.length, 1);
assert.equal(rows[0].name, "diagnostic");
assert.equal(rows[0].p95_before, 2);
assert.equal(rows[0].p95_after, 10);
assert.equal(rows[0].p95_delta_pct, 400);
ok("compareRuns matches scenarios by name and reports the p95 delta as a percentage");

assert.deepEqual(compareRuns(report, { scenarios: [] }), []);
ok("comparing against a run with no shared scenario yields no rows, not a fabricated zero");

// ------------------------------------------------------ scenario runner

/** A stub `call`: `plan` is one entry per call, `true` for success. */
function stubCall(plan, { nativeMs = 1.5, clientMs = 5 } = {}) {
  const calls = [];
  let index = 0;
  const call = async (tool, args) => {
    const succeeds = plan[index] ?? true;
    index += 1;
    calls.push({ tool, args });
    return {
      result: succeeds
        ? { success: true, native_duration_ms: nativeMs + index, data: {} }
        : { success: false, errors: ["Bridge is busy."] },
      clientMs: clientMs + index,
      bytes: 100,
    };
  };
  return { call, calls };
}

{
  const { call, calls } = stubCall([]);
  const scenario = await runScenario(
    { name: "probe", tool: "puerts_diagnostic", args: { actor_limit: 1 }, targetMs: 100 },
    { call, iterations: 5, warmup: 2 },
  );
  assert.equal(calls.length, 7, "warm-up calls were not made, or were counted as measured");
  assert.equal(scenario.status, "measured");
  assert.equal(scenario.iterations, 5);
  assert.equal(scenario.round_trips_per_iteration, 1);
  // clientMs is 6,7,8,... so the two warm-ups (6,7) must be absent.
  assert.deepEqual(scenario.client_round_trip_ms.samples, [8, 9, 10, 11, 12]);
  assert.equal(scenario.native_duration_ms.count, 5);
  assert.equal(scenario.response_bytes.p50, 100);
  assert.equal(scenario.target_met, true);
  assert.deepEqual(validateRunReport({ ...sampleReport(), scenarios: [scenario] }), { ok: true, problems: [] });
  ok("a measured scenario discards warm-up samples, keeps the measured ones, and validates");
}

{
  const { call } = stubCall([], { clientMs: 500 });
  const scenario = await runScenario(
    { name: "slow", tool: "puerts_find_actors", args: { limit: 500 }, targetMs: 50 },
    { call, iterations: 3, warmup: 0 },
  );
  assert.equal(scenario.target_met, false);
  assert.equal(scenario.target_ms, 50);
  ok("a scenario over its target reports target_met false rather than omitting the verdict");
}

{
  const { call } = stubCall([true, false]);
  const scenario = await runScenario(
    { name: "warmfail", tool: "puerts_save", args: {} },
    { call, iterations: 5, warmup: 2 },
  );
  assert.equal(scenario.status, "skipped");
  assert.ok(scenario.skip_reason.startsWith("warm-up failed"), scenario.skip_reason);
  assert.ok(scenario.skip_reason.includes("Bridge is busy."), "the editor's own error was discarded");
  assert.equal(scenario.client_round_trip_ms, undefined, "a skipped scenario reported statistics");
  ok("a failure during warm-up skips the scenario and carries the editor's error");
}

{
  const { call } = stubCall([true, true, false]);
  const scenario = await runScenario(
    { name: "midfail", tool: "puerts_set_property", args: {}, targetMs: 100 },
    { call, iterations: 5, warmup: 0 },
  );
  assert.equal(scenario.status, "skipped");
  assert.ok(scenario.skip_reason.startsWith("iteration 2 failed"), scenario.skip_reason);
  assert.equal(scenario.client_round_trip_ms, undefined);
  assert.equal(scenario.target_met, null, "a scenario that did not finish claimed a target verdict");
  ok("a failure mid-run skips the whole scenario rather than reporting a partial distribution");
}

{
  const { call, calls } = stubCall([]);
  const scenario = await runScenario(
    { name: "nolevel", tool: "puerts_read_property", args: {}, skip: "no actor in the editor level" },
    { call, iterations: 5, warmup: 2 },
  );
  assert.equal(scenario.status, "skipped");
  assert.equal(scenario.skip_reason, "no actor in the editor level");
  assert.equal(calls.length, 0, "a pre-skipped scenario still called the editor");
  ok("a scenario skipped before it starts calls nothing and records why");
}

{
  // A tool that reports elapsed_ms in data instead of native_duration_ms.
  const call = async () => ({ result: { success: true, data: { elapsed_ms: 72.3 } }, clientMs: 80, bytes: 10 });
  const scenario = await runScenario(
    { name: "patch", tool: "puerts_blueprint_graph_patch", args: {} },
    { call, iterations: 3, warmup: 0 },
  );
  assert.equal(scenario.native_duration_ms.p50, 72.3);
  ok("data.elapsed_ms is picked up when native_duration_ms is absent");
}

// ------------------------------------------------------ workflow runner

{
  const { call, calls } = stubCall([]);
  const scenario = await runWorkflowScenario(
    {
      name: "workflow_survey_project",
      steps: [
        { name: "identify_editor", tool: "puerts_diagnostic", args: { actor_limit: 1 } },
        { name: "list_actors", tool: "puerts_find_actors", args: { limit: 500 } },
        { name: "read_one_graph", tool: "puerts_graph_inspect", args: { asset_path: "/Game/BP_A" } },
      ],
    },
    { call, iterations: 2, warmup: 1 },
  );
  assert.equal(scenario.status, "measured");
  assert.equal(scenario.layer, "workflow");
  // Three steps, one warm-up iteration plus two measured: nine calls.
  assert.equal(calls.length, 9);
  assert.equal(scenario.round_trips_per_iteration, 3,
    "the round-trip count is the number AGENTS.md's product goal is about; it must be the step count");
  assert.equal(scenario.iterations, 2);
  assert.equal(scenario.steps.length, 3);
  assert.deepEqual(scenario.steps.map((s) => s.name), ["identify_editor", "list_actors", "read_one_graph"]);
  // clientMs is 5+index, so calls 4..9 are the measured ones: 9+10+11 and 12+13+14.
  assert.deepEqual(scenario.client_round_trip_ms.samples, [30, 39]);
  assert.deepEqual(scenario.steps.map((s) => s.client_round_trip_ms.samples), [[9, 12], [10, 13], [11, 14]]);
  assert.deepEqual(validateRunReport({ ...sampleReport(), scenarios: [scenario] }), { ok: true, problems: [] });
  ok("a workflow measures the whole sequence, declares its round-trip count, and breaks down by step");
}

{
  // Fail the fifth call: warm-up is three calls, so this is measured iteration
  // 0, step 2. The message has to name the step, not just the iteration.
  const { call } = stubCall([true, true, true, true, false]);
  const scenario = await runWorkflowScenario(
    {
      name: "workflow_author_and_verify",
      steps: [
        { name: "plan", tool: "puerts_blueprint_build", args: {} },
        { name: "apply_and_compile", tool: "puerts_blueprint_build", args: {} },
        { name: "verify", tool: "puerts_graph_inspect", args: {} },
      ],
    },
    { call, iterations: 3, warmup: 1 },
  );
  assert.equal(scenario.status, "skipped");
  assert.ok(scenario.skip_reason.includes("iteration 0 step 2/3 (apply_and_compile)"), scenario.skip_reason);
  assert.ok(scenario.skip_reason.includes("Bridge is busy."), "the editor's own error was discarded");
  assert.equal(scenario.client_round_trip_ms, undefined, "a skipped workflow reported statistics");
  ok("a workflow that breaks names the failing step and reports no distribution");
}

{
  const { call, calls } = stubCall([]);
  const skipped = await runWorkflowScenario(
    { name: "workflow_author_and_verify", skip: "not requested; pass --include-compile", steps: [{ name: "a", tool: "t" }] },
    { call, iterations: 3, warmup: 1 },
  );
  assert.equal(skipped.status, "skipped");
  assert.equal(calls.length, 0);
  const empty = await runWorkflowScenario({ name: "nothing", steps: [] }, { call, iterations: 1, warmup: 0 });
  assert.equal(empty.status, "skipped");
  assert.ok(empty.skip_reason.includes("no steps"));
  ok("a workflow that is not requested, or declares no steps, is skipped and calls nothing");
}

// ----------------------------------------------------------- PIE cycle

/** A `call` that walks a scripted list of `physics_observe` worlds. */
function pieStub(worlds, { startOk = true, stopOk = true } = {}) {
  const seen = [];
  let at = 0;
  const call = async (tool) => {
    seen.push(tool);
    if (tool === "puerts_pie_start") return { result: { success: startOk, errors: ["refused"] }, clientMs: 3, bytes: 1 };
    if (tool === "puerts_pie_stop") return { result: { success: stopOk, errors: ["refused"] }, clientMs: 4, bytes: 1 };
    const world = worlds[Math.min(at, worlds.length - 1)];
    at += 1;
    return { result: { success: true, data: { world } }, clientMs: 2, bytes: 1 };
  };
  return { call, seen };
}

{
  // editor (precondition), editor, pie (up), pie, editor (down).
  const { call, seen } = pieStub(["editor", "editor", "pie", "pie", "editor"]);
  const scenario = await runPieCycleScenario({ name: "pie_cycle" }, { call, sleep: async () => {}, pollIntervalMs: 0 });
  assert.equal(scenario.status, "measured");
  assert.equal(scenario.layer, "pie");
  assert.deepEqual(scenario.steps.map((s) => s.name),
    ["pie_start_request", "start_request_to_pie_world", "pie_stop_request", "stop_request_to_editor_world"]);
  assert.equal(scenario.round_trips_per_iteration, seen.length,
    "every readiness poll is a real round trip and must be counted");
  assert.ok(seen.includes("puerts_pie_start") && seen.includes("puerts_pie_stop"));
  assert.deepEqual(validateRunReport({ ...sampleReport(), scenarios: [scenario] }), { ok: true, problems: [] });
  ok("a PIE cycle polls for a real play world, then for the editor world back, and counts every poll");
}

{
  // The editor is already playing. Starting from there would measure nothing.
  const { call, seen } = pieStub(["pie"]);
  const scenario = await runPieCycleScenario({ name: "pie_cycle" }, { call, sleep: async () => {} });
  assert.equal(scenario.status, "skipped");
  assert.ok(scenario.skip_reason.includes('already in world "pie"'), scenario.skip_reason);
  assert.equal(seen.filter((t) => t === "puerts_pie_start").length, 0, "PIE was started from a world that was already playing");
  ok("a PIE cycle refuses to start from a world that is not the editor world");
}

{
  // PIE never comes up. This must skip with the timeout stated, not report the
  // poll loop's own duration as a PIE start time.
  const { call } = pieStub(["editor"]);
  const scenario = await runPieCycleScenario(
    { name: "pie_cycle" },
    { call, sleep: async () => {}, pollIntervalMs: 0, timeoutMs: 0 },
  );
  assert.equal(scenario.status, "skipped");
  assert.ok(scenario.skip_reason.includes("never became playable"), scenario.skip_reason);
  assert.equal(scenario.client_round_trip_ms, undefined);
  assert.ok(scenario.round_trips_per_iteration > 0, "the calls that were made were not counted");
  ok("PIE that never becomes playable is skipped with the timeout stated, not timed as if it had");
}

{
  const { call, seen } = pieStub(["editor"], { startOk: false });
  const refused = await runPieCycleScenario({ name: "pie_cycle" }, { call, sleep: async () => {} });
  assert.equal(refused.status, "skipped");
  assert.ok(refused.skip_reason.includes("pie_start refused"), refused.skip_reason);
  assert.equal(seen.filter((t) => t === "puerts_pie_stop").length, 0, "a stop was sent for a start that was refused");
  const notAsked = await runPieCycleScenario(
    { name: "pie_cycle", skip: "not requested; pass --include-pie" },
    { call, sleep: async () => {} },
  );
  assert.equal(notAsked.status, "skipped");
  assert.equal(notAsked.skip_reason, "not requested; pass --include-pie");
  ok("a refused pie_start does not go on to stop, and an unrequested cycle starts nothing");
}

{
  // physics_observe answers but its world field is gone. Polling for it for a
  // minute and then reporting "PIE never became playable" would name the wrong
  // problem: the editor is fine, the response contract moved.
  const call = async () => ({ result: { success: true, data: { actors: [] } }, clientMs: 2, bytes: 1 });
  const scenario = await runPieCycleScenario({ name: "pie_cycle" }, { call, sleep: async () => {} });
  assert.equal(scenario.status, "skipped");
  assert.ok(scenario.skip_reason.includes("data.world"), scenario.skip_reason);
  assert.ok(scenario.skip_reason.includes("payload shape"), "the payload that arrived is not described");
  assert.ok(!scenario.skip_reason.includes("never became playable"), "a shape failure was reported as a PIE timeout");
  ok("physics_observe with no world field stops at once and names the field, not a PIE timeout");
}

// ------------------------------------------------------------ shape checks

assert.equal(checkShape("x", { data: { a: 1 } }, [["data.a", "number"]]), null);
assert.equal(checkShape("x", { data: { a: [1, 2] } }, [["data.a", "array+"], ["data.a.0", "number"]]), null);
ok("a payload that has the fields a caller reads passes the shape check");

const wrongShape = checkShape("puerts_find_actors", { success: true, data: { actors: [] } }, [
  ["data.actors", "array+"],
  ["data.actors.0.name", "string+"],
]);
assert.ok(wrongShape.includes("puerts_find_actors"), "the tool is named");
assert.ok(wrongShape.includes("data.actors: expected at least one element"), wrongShape);
assert.ok(wrongShape.includes("data.actors.0.name: expected string, found undefined"), wrongShape);
assert.ok(wrongShape.includes("payload shape:"), "the shape of what arrived is described");
assert.ok(wrongShape.includes("payload head:"), "the payload itself is quoted");
ok("a bad shape names every failed path, what arrived, and describes the payload");

// The exact defect this check exists for: identity is on the response
// envelope under "session", not in "data". Reading it from data produced
// session_id "" and editor_pid 0 in every results file, which validates
// against the schema and is uncomparable to any other run.
const identity = { success: true, data: { actor_count_total: 56 }, session: { session_id: "abc", editor_pid: 4242 } };
assert.equal(checkShape("puerts_diagnostic", identity, [["session.session_id", "string+"], ["session.editor_pid", "number"]]), null);
assert.ok(checkShape("puerts_diagnostic", identity, [["data.session_id", "string+"]]).includes("found undefined"));
ok("session identity is read from the response envelope, and reading it from data is caught");

assert.ok(checkShape("t", { data: { value: null } }, [["data.value", "boolean"]]).includes("found null"));
assert.ok(checkShape("t", {}, [["data.value", "boolean"]]).includes("found undefined"));
assert.equal(checkShape("t", { data: { value: false } }, [["data.value", "boolean"]]), null);
ok("a boolean check accepts false and rejects null and absent: set_property writes this value back");

assert.equal(describeShape({ a: 1, b: "x", c: [1], d: null }), "{ a: number, b: string, c: array(1) of number, d: null }");
assert.equal(describeShape([]), "array(0)");
ok("describeShape names the keys and their types rather than truncating JSON");

// ------------------------------------------------- editor startup timing

{
  const startup = deriveEditorStartupScenario({
    process_start_time: "2026-08-02T10:00:00.000Z",
    created_at: "2026-08-02T10:00:42.500Z",
  });
  assert.equal(startup.status, "measured");
  assert.equal(startup.layer, "editor_startup");
  assert.equal(startup.round_trips_per_iteration, 0, "reading a file the editor already wrote is not a round trip");
  assert.equal(startup.client_round_trip_ms.p50, 42500);
  assert.deepEqual(validateRunReport({ ...sampleReport(), scenarios: [startup] }), { ok: true, problems: [] });
  ok("editor startup is derived from the session manifest timestamps, with no restart and no round trip");
}

{
  for (const manifest of [{}, { process_start_time: "", created_at: "2026-08-02T10:00:00.000Z" }, { unreadable: "ENOENT" }]) {
    const startup = deriveEditorStartupScenario(manifest);
    assert.equal(startup.status, "skipped");
    assert.equal(startup.client_round_trip_ms, undefined, "an unmeasurable startup reported a duration");
  }
  const backwards = deriveEditorStartupScenario({
    process_start_time: "2026-08-02T10:00:42.500Z",
    created_at: "2026-08-02T10:00:00.000Z",
  });
  assert.equal(backwards.status, "skipped");
  assert.ok(backwards.skip_reason.includes("precedes"), backwards.skip_reason);
  ok("a missing, unparseable or backwards manifest pair skips rather than reporting zero or a negative duration");
}

// -------------------------------------------------------- refusal path

assert.equal(describeRefusal({ success: true, data: {} }), null);
ok("a successful probe is not a refusal");

const missing = describeRefusal({
  success: false,
  errors: ["No editor session is advertised for D:/Projects/Test."],
  session_error_code: "session_missing",
  session_error_detail: { manifest: "Saved/MCPPuerTSBridge/session.json" },
});
assert.equal(missing.refused, true);
assert.equal(missing.measured, false);
assert.equal(missing.reason_code, "session_missing");
assert.deepEqual(missing.detail, { manifest: "Saved/MCPPuerTSBridge/session.json" });
assert.ok(missing.errors[0].includes("No editor session is advertised"));
ok("no advertised session refuses with the client's own session_missing code and detail");

for (const code of ["session_stale", "session_shut_down", "session_project_mismatch", "session_identity_mismatch"]) {
  assert.equal(describeRefusal({ success: false, session_error_code: code }).reason_code, code);
}
ok("every session refusal code is carried through rather than flattened to one message");

const failed = describeRefusal({ success: false, errors: ["Bridge is busy."] });
assert.equal(failed.reason_code, "command_failed");
assert.equal(failed.measured, false);
ok("a command that ran and failed is a distinct refusal code from an unreachable editor");

assert.equal(describeRefusal(undefined).refused, true);
assert.equal(describeRefusal(null).measured, false);
ok("a missing or null response is refused, not treated as a fast success");

// End to end, with no editor and no project root: the harness must exit
// non-zero and say what is missing rather than producing a results file.
const withoutRoot = { ...process.env };
delete withoutRoot.MCP_UNREAL_PROJECT_ROOT;
const run = spawnSync(process.execPath, [join(root, "Scripts", "perf-benchmark.mjs")], {
  env: withoutRoot, encoding: "utf-8", cwd: root,
});
assert.notEqual(run.status, 0);
const output = `${run.stdout ?? ""}${run.stderr ?? ""}`;
assert.ok(
  /MCP_UNREAL_PROJECT_ROOT|Run npm run build first/.test(output),
  `expected the harness to name what is missing, got: ${output.slice(0, 400)}`,
);
assert.ok(!/p50|p95/.test(run.stdout ?? ""), "the harness printed statistics without an editor");
ok("the harness with no project root exits non-zero, names the gap, and prints no statistics");

// A project root that no editor is serving. The install gate is bypassed on
// purpose: what is under test is the refusal after the gate, and this is the
// exact shape of a benchmark run started before the editor is up. Skipped
// when the server has not been built, because that failure would mask this one.
if (existsSync(join(root, "mcp-server", "dist", "index.js"))) {
  const emptyRoot = mkdtempSync(join(tmpdir(), "perf-no-editor-"));
  const refused = spawnSync(process.execPath, [join(root, "Scripts", "perf-benchmark.mjs"), "--runs", "2", "--warmup", "0"], {
    env: { ...process.env, MCP_UNREAL_PROJECT_ROOT: emptyRoot, MCP_SKIP_INSTALL_CHECK: "1" },
    encoding: "utf-8", cwd: root,
  });
  assert.equal(refused.status, 2, `expected exit 2, got ${refused.status}: ${refused.stderr?.slice(0, 400)}`);
  const printed = `${refused.stdout ?? ""}${refused.stderr ?? ""}`;
  assert.ok(/"refused": true/.test(printed), "the refusal was not printed as structured JSON");
  assert.ok(/"measured": false/.test(printed), "the refusal did not state that nothing was measured");
  assert.ok(/No results file was written/.test(printed), "the harness did not say it wrote nothing");
  assert.ok(!/p50|p95/.test(printed), "the harness printed latency statistics with no editor running");
  rmSync(emptyRoot, { recursive: true, force: true });
  ok("with a project root no editor is serving, the harness exits 2 with a structured refusal and no numbers");
} else {
  console.log("  SKIP  end-to-end refusal: mcp-server/dist is absent, run npm run build first");
}

console.log(`\nperf-stats: ${passed} checks passed.`);
