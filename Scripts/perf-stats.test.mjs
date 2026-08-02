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
  buildRunReport, compareRuns, describeRefusal, percentile,
  runSchema, runScenario, summarize, validateRunReport,
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
