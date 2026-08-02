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
