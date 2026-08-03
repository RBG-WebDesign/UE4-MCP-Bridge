#!/usr/bin/env node
/**
 * Pure helpers for the bridge performance harness: order statistics, the
 * results-file shape, and the decision to refuse rather than report numbers.
 *
 * Nothing here spawns a process or touches the editor, which is the point:
 * Scripts/perf-stats.test.mjs covers all of it with no UE4 running, and
 * Scripts/perf-benchmark.mjs is left with only the measuring.
 */
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
export const SCHEMA_PATH = join(repoRoot, "docs", "evidence", "perf-run.schema.json");
export const SCHEMA_VERSION = 1;

/** The schema file is the only definition of the results format. Read it once
    rather than restating its required fields in JavaScript, where the two
    copies would drift the first time a field is added. */
export const runSchema = JSON.parse(readFileSync(SCHEMA_PATH, "utf-8"));

// ------------------------------------------------------------- statistics

/**
 * Nearest-rank percentile: the value at ceil(p * n) in ascending order. No
 * interpolation, so every reported number is a measurement that actually
 * happened rather than an average of two that did not. p is a fraction.
 */
export function percentile(samples, p) {
  if (!Array.isArray(samples) || samples.length === 0) return null;
  const sorted = [...samples].sort((a, b) => a - b);
  const rank = Math.max(1, Math.min(sorted.length, Math.ceil(p * sorted.length)));
  return sorted[rank - 1];
}

/** count/min/p50/p95/max/mean over one scenario's samples, or null for none.
    `samples` is carried through so a later run can re-derive any other
    statistic without re-running against the editor. */
export function summarize(samples, { keepSamples = true } = {}) {
  if (!Array.isArray(samples) || samples.length === 0) return null;
  const sorted = [...samples].sort((a, b) => a - b);
  const sum = sorted.reduce((total, value) => total + value, 0);
  const stats = {
    count: sorted.length,
    min: sorted[0],
    p50: percentile(sorted, 0.5),
    p95: percentile(sorted, 0.95),
    max: sorted[sorted.length - 1],
    mean: sum / sorted.length,
  };
  if (keepSamples) stats.samples = sorted;
  return stats;
}

// ------------------------------------------------------------- validation

/**
 * The subset of JSON Schema this format uses: type (single or union),
 * required, properties, items, enum, and local $ref into definitions.
 * Deliberately not a general validator - a general one is a dependency, and
 * this file is the only schema it will ever be asked about.
 */
function checkNode(value, node, path, schema, problems) {
  if (node.$ref) {
    const key = node.$ref.replace("#/definitions/", "");
    return checkNode(value, schema.definitions[key], path, schema, problems);
  }
  const types = node.type === undefined ? [] : (Array.isArray(node.type) ? node.type : [node.type]);
  if (types.length > 0 && !types.some((t) => matchesType(value, t))) {
    problems.push(`${path}: expected ${types.join(" or ")}, found ${describeType(value)}`);
    return;
  }
  if (Array.isArray(node.enum) && !node.enum.includes(value)) {
    problems.push(`${path}: ${JSON.stringify(value)} is not one of ${node.enum.join(", ")}`);
  }
  if (matchesType(value, "object")) {
    for (const key of node.required ?? []) {
      if (!(key in value)) problems.push(`${path}: missing required field "${key}"`);
    }
    for (const [key, childNode] of Object.entries(node.properties ?? {})) {
      if (key in value) checkNode(value[key], childNode, `${path}.${key}`, schema, problems);
    }
  }
  if (matchesType(value, "array") && node.items) {
    value.forEach((item, index) => checkNode(item, node.items, `${path}[${index}]`, schema, problems));
  }
}

function matchesType(value, type) {
  switch (type) {
    case "object": return value !== null && typeof value === "object" && !Array.isArray(value);
    case "array": return Array.isArray(value);
    case "string": return typeof value === "string";
    case "integer": return typeof value === "number" && Number.isInteger(value);
    case "number": return typeof value === "number" && Number.isFinite(value);
    case "boolean": return typeof value === "boolean";
    case "null": return value === null;
    default: return true;
  }
}

function describeType(value) {
  if (value === null) return "null";
  if (Array.isArray(value)) return "array";
  return typeof value;
}

/** Validate a results file against docs/evidence/perf-run.schema.json.
    Returns every problem, not the first, so one edit fixes one run. */
export function validateRunReport(report, schema = runSchema) {
  const problems = [];
  checkNode(report, schema, "$", schema, problems);
  return { ok: problems.length === 0, problems };
}

// ------------------------------------------------------------ shape checks

/**
 * Describe a value the way a diagnosis needs it: its type, and for an object
 * its keys with their types one level down. A truncated JSON dump alone is a
 * bad diagnosis for a 200 KB graph_inspect response, because the field that is
 * missing is exactly the one the truncation cut.
 */
export function describeShape(value, depth = 1) {
  if (value === null) return "null";
  if (Array.isArray(value)) {
    return value.length === 0 ? "array(0)" : `array(${value.length}) of ${describeShape(value[0], depth - 1)}`;
  }
  if (typeof value !== "object") return typeof value;
  const keys = Object.keys(value);
  if (depth <= 0) return `object{${keys.length} keys}`;
  const inner = keys.slice(0, 24).map((key) => `${key}: ${describeShape(value[key], depth - 1)}`);
  if (keys.length > 24) inner.push(`... ${keys.length - 24} more`);
  return `{ ${inner.join(", ")} }`;
}

/** Read "data.actors.0.name" out of a payload. Missing at any step is undefined. */
function atPath(payload, path) {
  let current = payload;
  for (const segment of path.split(".")) {
    if (current === null || typeof current !== "object") return undefined;
    current = Array.isArray(current) ? current[Number(segment)] : current[segment];
  }
  return current;
}

/**
 * Check that a response really has the fields the harness is about to read out
 * of it, and produce a diagnosis rather than a stack trace when it does not.
 *
 * This exists because every scenario runner in this file was only ever
 * exercised against a stub whose response shape was guessed from C++ field
 * names, and because the benchmark's spec-building code reads six different
 * payloads that nothing had ever checked. A wrong guess used to surface as
 * `undefined` flowing into a scenario argument, which produces either a
 * scenario that measures the wrong thing or a crash two hundred lines later.
 * Now it names the tool, the path, what was expected, what arrived, and the
 * shape of the payload it arrived in.
 *
 * `expectations` is a list of [path, kind] where kind is one of string,
 * number, boolean, array, object, with a trailing "+" meaning non-empty
 * (a non-empty string or an array with at least one element).
 *
 * Returns null when every expectation holds, otherwise one problem string
 * carrying all of them plus the payload description.
 */
export function checkShape(label, payload, expectations) {
  const problems = [];
  for (const [path, kind] of expectations) {
    const required = kind.endsWith("+");
    const baseKind = required ? kind.slice(0, -1) : kind;
    const value = atPath(payload, path);
    if (!matchesType(value, baseKind)) {
      problems.push(`${path}: expected ${baseKind}, found ${describeType(value)}`);
      continue;
    }
    if (required && baseKind === "string" && value.length === 0) {
      problems.push(`${path}: expected a non-empty string, found ""`);
    }
    if (required && baseKind === "array" && value.length === 0) {
      problems.push(`${path}: expected at least one element, found an empty array`);
    }
  }
  if (problems.length === 0) return null;
  return `${label}: the response does not have the shape this harness reads.\n`
    + problems.map((problem) => `    - ${problem}`).join("\n")
    + `\n    payload shape: ${describeShape(payload, 2)}`
    + `\n    payload head:  ${JSON.stringify(payload).slice(0, 400)}`;
}

// ----------------------------------------------------------------- report

/** Assemble one results file. Callers supply already-summarized scenarios. */
export function buildRunReport({ run, environment, scenarios, wallMs, roundTrips }) {
  return {
    schema_version: SCHEMA_VERSION,
    kind: "bridge-perf-run",
    run,
    environment,
    scenarios,
    totals: {
      round_trips: roundTrips,
      wall_ms: wallMs,
      scenarios_measured: scenarios.filter((s) => s.status === "measured").length,
      scenarios_skipped: scenarios.filter((s) => s.status === "skipped").length,
    },
  };
}

// --------------------------------------------------------------- scenario

/**
 * Run one scenario: `warmup` calls that are discarded, then `iterations` calls
 * that are timed. `call(tool, args)` returns `{result, clientMs, bytes}` and
 * must not throw on a failed command.
 *
 * A failure during the measured phase skips the whole scenario with the
 * editor's own error, rather than reporting a distribution over whichever
 * iterations happened to succeed. Lives here, apart from the stdio plumbing,
 * so the skip and target logic is testable with no editor.
 */
export async function runScenario(spec, { call, iterations, warmup }) {
  const base = {
    name: spec.name,
    tool: spec.tool,
    layer: spec.layer ?? "client_round_trip",
    iterations: 0,
    round_trips_per_iteration: spec.roundTripsPerIteration ?? 1,
    target_ms: spec.targetMs ?? null,
    target_met: null,
  };
  if (spec.skip) return { ...base, status: "skipped", skip_reason: spec.skip };
  if (spec.targetBasis) base.target_basis = spec.targetBasis;

  const why = (result) => (result?.errors ?? []).join("; ") || String(result?.message ?? "no error reported");

  for (let i = 0; i < warmup; i += 1) {
    const { result } = await call(spec.tool, spec.args);
    if (result?.success !== true) {
      return { ...base, status: "skipped", skip_reason: `warm-up failed: ${why(result)}` };
    }
  }

  const clientSamples = [];
  const nativeSamples = [];
  const byteSamples = [];
  for (let i = 0; i < iterations; i += 1) {
    const { result, clientMs, bytes } = await call(spec.tool, spec.args);
    if (result?.success !== true) {
      return { ...base, status: "skipped", skip_reason: `iteration ${i} failed: ${why(result)}` };
    }
    clientSamples.push(clientMs);
    byteSamples.push(bytes);
    // Every native response carries native_duration_ms. The planners and
    // blueprint_graph_patch report their own elapsed_ms inside data instead.
    if (typeof result.native_duration_ms === "number") nativeSamples.push(result.native_duration_ms);
    else if (typeof result.data?.elapsed_ms === "number") nativeSamples.push(result.data.elapsed_ms);
  }

  const client = summarize(clientSamples);
  const scenario = {
    ...base,
    status: "measured",
    iterations,
    client_round_trip_ms: client,
    response_bytes: summarize(byteSamples, { keepSamples: false }),
  };
  const native = summarize(nativeSamples);
  if (native) scenario.native_duration_ms = native;
  if (typeof spec.targetMs === "number") scenario.target_met = client.p95 <= spec.targetMs;
  return scenario;
}

/**
 * Run one workflow: an ordered list of steps that together do a job a caller
 * would actually ask for. One iteration is the whole list.
 *
 * This exists because AGENTS.md's product goal is the fewest editor round
 * trips, not the fewest milliseconds. A change that halves per-call latency
 * and doubles the call count is a regression, and only a scenario that counts
 * steps can see it. `round_trips_per_iteration` is the headline number here;
 * the milliseconds are secondary.
 *
 * Per-step statistics are kept so a workflow that got slower says which step
 * did it, without a second editor session to find out.
 */
export async function runWorkflowScenario(spec, { call, iterations, warmup }) {
  const steps = spec.steps ?? [];
  const base = {
    name: spec.name,
    tool: "(workflow)",
    layer: "workflow",
    iterations: 0,
    round_trips_per_iteration: steps.length,
    target_ms: spec.targetMs ?? null,
    target_met: null,
  };
  if (spec.skip) return { ...base, status: "skipped", skip_reason: spec.skip };
  if (steps.length === 0) {
    return { ...base, status: "skipped", skip_reason: "the workflow declares no steps" };
  }
  if (spec.targetBasis) base.target_basis = spec.targetBasis;

  const why = (result) => (result?.errors ?? []).join("; ") || String(result?.message ?? "no error reported");
  const totals = [];
  const perStep = steps.map(() => []);

  for (let i = 0; i < warmup + iterations; i += 1) {
    const measured = i >= warmup;
    let total = 0;
    for (let s = 0; s < steps.length; s += 1) {
      const { result, clientMs } = await call(steps[s].tool, steps[s].args ?? {});
      if (result?.success !== true) {
        return {
          ...base,
          status: "skipped",
          skip_reason: `${measured ? `iteration ${i - warmup}` : "warm-up"} step `
            + `${s + 1}/${steps.length} (${steps[s].name}) failed: ${why(result)}`,
        };
      }
      total += clientMs;
      if (measured) perStep[s].push(clientMs);
    }
    if (measured) totals.push(total);
  }

  const client = summarize(totals);
  const scenario = {
    ...base,
    status: "measured",
    iterations,
    client_round_trip_ms: client,
    steps: steps.map((step, index) => ({
      name: step.name,
      tool: step.tool,
      client_round_trip_ms: summarize(perStep[index]),
    })),
  };
  if (typeof spec.targetMs === "number") scenario.target_met = client.p95 <= spec.targetMs;
  return scenario;
}

/**
 * Time a full Play In Editor cycle: request start, wait until a PIE world is
 * really there, request stop, wait until the editor world is back.
 *
 * The request round trips on their own would be a lie. `StartPlayInEditor`
 * calls `GEditor->RequestPlaySession` (MCPPuerTSBridgeService.cpp:1177), which
 * queues a request the editor honours on a later tick, so `pie_start` returns
 * in the time it takes to set a flag. Worse, `StopPlayInEditor` cancels a
 * still-queued request rather than ending a session
 * (MCPPuerTSBridgeService.cpp:1193-1195), so a stop sent straight after a start
 * measures a PIE session that never happened.
 *
 * `physics_observe` is one of the three tools AcceptCommand allows during play
 * (MCPPuerTSBridgeService.cpp:495-499) and it stamps its result with
 * `world: "pie"` or `"editor"` (MCPBridgePuerTSPhysics.cpp:298). That is the
 * readiness signal, so it is what this polls. Every poll is a real round trip
 * and is counted as one.
 *
 * One iteration only: a PIE cycle costs seconds, and twenty of them is a
 * different kind of test.
 */
export async function runPieCycleScenario(spec, { call, sleep, pollIntervalMs = 250, timeoutMs = 60000 }) {
  const base = {
    name: spec.name,
    tool: "puerts_pie_start",
    layer: "pie",
    iterations: 0,
    round_trips_per_iteration: 0,
    target_ms: spec.targetMs ?? null,
    target_met: null,
  };
  if (spec.skip) return { ...base, status: "skipped", skip_reason: spec.skip };
  if (spec.targetBasis) base.target_basis = spec.targetBasis;

  const why = (result) => (result?.errors ?? []).join("; ") || String(result?.message ?? "no error reported");
  let roundTrips = 0;
  const phases = [];

  /** Poll physics_observe until it reports `wanted`, or give up and say so. */
  const waitForWorld = async (wanted) => {
    const started = process.hrtime.bigint();
    for (;;) {
      const { result } = await call("puerts_physics_observe", {});
      roundTrips += 1;
      const elapsed = Number(process.hrtime.bigint() - started) / 1e6;
      // A refusal during play is expected for other tools, not this one; if
      // physics_observe itself fails there is nothing left to poll with.
      if (result?.success !== true) return { ok: false, elapsed, reason: why(result) };
      // A missing world field is a contract change, not a slow PIE start.
      // Polling for it for a minute and then reporting a timeout would name
      // the wrong problem, so it stops here with the payload.
      const shapeProblem = checkShape("puerts_physics_observe", result, [["data.world", "string+"]]);
      if (shapeProblem) return { ok: false, elapsed, reason: shapeProblem };
      if (result.data.world === wanted) return { ok: true, elapsed };
      if (elapsed > timeoutMs) {
        return { ok: false, elapsed, reason: `world stayed "${String(result.data?.world)}" for ${timeoutMs} ms` };
      }
      await sleep(pollIntervalMs);
    }
  };

  const fail = (reason) => ({ ...base, status: "skipped", skip_reason: reason, round_trips_per_iteration: roundTrips });

  const before = await call("puerts_physics_observe", {});
  roundTrips += 1;
  if (before.result?.success !== true) return fail(`physics_observe is unavailable, so PIE readiness cannot be observed: ${why(before.result)}`);
  const worldShape = checkShape("puerts_physics_observe", before.result, [["data.world", "string+"]]);
  if (worldShape) return fail(worldShape);
  if (before.result.data.world !== "editor") return fail(`the editor is already in world "${String(before.result.data.world)}"; a PIE cycle must start from the editor world`);

  const start = await call("puerts_pie_start", {});
  roundTrips += 1;
  if (start.result?.success !== true) return fail(`pie_start refused: ${why(start.result)}`);
  phases.push({ name: "pie_start_request", ms: start.clientMs });

  const up = await waitForWorld("pie");
  if (!up.ok) return fail(`PIE never became playable: ${up.reason}`);
  phases.push({ name: "start_request_to_pie_world", ms: up.elapsed });

  const stop = await call("puerts_pie_stop", {});
  roundTrips += 1;
  if (stop.result?.success !== true) return fail(`pie_stop refused while PIE was running: ${why(stop.result)}`);
  phases.push({ name: "pie_stop_request", ms: stop.clientMs });

  const down = await waitForWorld("editor");
  if (!down.ok) return fail(`the editor world never came back: ${down.reason}`);
  phases.push({ name: "stop_request_to_editor_world", ms: down.elapsed });

  const total = phases.reduce((sum, phase) => sum + phase.ms, 0);
  return {
    ...base,
    status: "measured",
    iterations: 1,
    round_trips_per_iteration: roundTrips,
    client_round_trip_ms: summarize([total]),
    steps: phases.map((phase) => ({
      name: phase.name,
      tool: "puerts_physics_observe",
      client_round_trip_ms: summarize([phase.ms]),
    })),
    target_met: typeof spec.targetMs === "number" ? total <= spec.targetMs : null,
  };
}

/**
 * Editor start to bridge ready, derived from the session manifest rather than
 * measured by restarting anything.
 *
 * `process_start_time` is the OS process creation time
 * (`GetProcessStartTimeUtc`, MCPPuerTSBridgeService.cpp:62-76) and `created_at`
 * is stamped in `Initialize` immediately before the manifest is first published
 * (MCPPuerTSBridgeService.cpp:290-292). The difference is exactly "how long
 * after launch could a client have addressed this editor", which is the number
 * an editor-restart benchmark is after. No restart is required to read it, and
 * a harness that is forbidden from launching editors can still report it.
 *
 * The cost of that convenience, stated because it is not obvious: it is one
 * sample per editor session and it is not repeatable within a run.
 */
export function deriveEditorStartupScenario(session) {
  const base = {
    name: "editor_startup_to_bridge_ready",
    tool: "(session manifest)",
    layer: "editor_startup",
    iterations: 0,
    round_trips_per_iteration: 0,
    target_ms: null,
    target_met: null,
    target_basis: "process creation to session.json first published. One sample per editor session, "
      + "derived from the manifest, not from a restart the harness performed.",
  };
  const started = Date.parse(String(session?.process_start_time ?? ""));
  const ready = Date.parse(String(session?.created_at ?? ""));
  if (!Number.isFinite(started) || !Number.isFinite(ready)) {
    return {
      ...base,
      status: "skipped",
      skip_reason: "the session manifest has no parseable process_start_time and created_at pair; "
        + "GetProcessStartTimeUtc returns an empty string off Windows",
    };
  }
  if (ready < started) {
    return {
      ...base,
      status: "skipped",
      skip_reason: `created_at ${session.created_at} precedes process_start_time ${session.process_start_time}; `
        + "the clocks disagree and the difference would not be a duration",
    };
  }
  return { ...base, status: "measured", iterations: 1, client_round_trip_ms: summarize([ready - started]) };
}

// ---------------------------------------------------------------- refusal

/**
 * Decide whether a tool result means "no editor answered" rather than "the
 * command ran and failed". A benchmark that cannot reach an editor must say
 * so with the structured code the client already produced; timing a refusal
 * and reporting it as a latency is how a harness lies.
 *
 * Returns null when the result is a real success.
 */
export function describeRefusal(result) {
  if (result !== null && typeof result === "object" && result.success === true) return null;
  const envelope = (result !== null && typeof result === "object") ? result : {};
  const code = typeof envelope.session_error_code === "string"
    ? envelope.session_error_code
    : "command_failed";
  return {
    refused: true,
    measured: false,
    reason_code: code,
    reason: code === "command_failed"
      ? "The probe command did not succeed and carried no session error code. No numbers were recorded."
      : "No editor session could be addressed. No numbers were recorded.",
    detail: (envelope.session_error_detail ?? {}),
    errors: Array.isArray(envelope.errors) ? envelope.errors : [],
  };
}

/** Per-scenario p50/p95 delta against an earlier run, matched by name.
    Returns one row per scenario present in both and measured in both. */
export function compareRuns(current, baseline) {
  const before = new Map((baseline?.scenarios ?? []).map((s) => [s.name, s]));
  const rows = [];
  for (const scenario of current.scenarios ?? []) {
    const previous = before.get(scenario.name);
    if (!previous) continue;
    if (scenario.status !== "measured" || previous.status !== "measured") continue;
    const now = scenario.client_round_trip_ms;
    const then = previous.client_round_trip_ms;
    if (!now || !then) continue;
    rows.push({
      name: scenario.name,
      p50_before: then.p50,
      p50_after: now.p50,
      p50_delta_pct: then.p50 === 0 ? null : ((now.p50 - then.p50) / then.p50) * 100,
      p95_before: then.p95,
      p95_after: now.p95,
      p95_delta_pct: then.p95 === 0 ? null : ((now.p95 - then.p95) / then.p95) * 100,
    });
  }
  return rows;
}
