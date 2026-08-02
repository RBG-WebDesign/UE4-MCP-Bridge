#!/usr/bin/env node
/**
 * Bridge performance benchmark. Drives the built MCP server over stdio the way
 * a real client does, times repeated calls per command, and writes a results
 * file under docs/evidence/ that a later run can be compared against.
 *
 * RUN THIS AGAINST A LIVE EDITOR. It is not part of npm run verify and it is
 * not a unit test: the numbers it produces describe one machine, one project
 * and one editor session, and they are only worth anything when the run
 * conditions are recorded alongside them, which the results file does.
 *
 *   node Scripts/perf-benchmark.mjs [options]
 *
 *   --label <name>       run label, default "default"; goes in the filename
 *   --runs <n>           measured iterations per scenario, default 20
 *   --warmup <n>         discarded iterations per scenario, default 3
 *   --out <file>         explicit output path
 *   --baseline <file>    an earlier results file to compare against
 *   --include-save       add the save scenario (writes to disk)
 *   --include-screenshot add the screenshot scenario (writes a PNG)
 *   --no-write           print the report, do not write a file
 *
 * Exit codes: 0 measured and written, 2 refused (no editor addressed, or the
 * probe failed), 1 anything else. A refusal prints the structured refusal and
 * writes nothing. There is no mode in which this script invents a number.
 *
 * Requires MCP_UNREAL_PROJECT_ROOT, like every other live script here, and
 * gates on requireCurrentInstall() so a run against a stale install cannot
 * masquerade as evidence about this checkout.
 */
import { spawn } from "node:child_process";
import { execSync } from "node:child_process";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { hostname, cpus, platform, totalmem } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";
import { buildRunReport, compareRuns, describeRefusal, summarize, validateRunReport } from "./perf-stats.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(root, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");

const argv = process.argv.slice(2);
const flag = (name, fallback) => {
  const at = argv.indexOf(name);
  return at >= 0 && argv[at + 1] !== undefined ? argv[at + 1] : fallback;
};
const label = String(flag("--label", "default"));
const iterations = Number(flag("--runs", 20));
const warmup = Number(flag("--warmup", 3));
const includeSave = argv.includes("--include-save");
const includeScreenshot = argv.includes("--include-screenshot");
const writeFile = !argv.includes("--no-write");
if (!Number.isInteger(iterations) || iterations < 1) throw new Error("--runs must be a positive integer");
if (!Number.isInteger(warmup) || warmup < 0) throw new Error("--warmup must be zero or a positive integer");

const projectRoot = requireCurrentInstall();

const child = spawn(process.execPath, [serverPath], {
  stdio: ["pipe", "pipe", "inherit"],
  env: process.env,
});
let buffer = "";
let nextId = 1;
let roundTrips = 0;
const pending = new Map();
child.stdout.on("data", (chunk) => {
  buffer += chunk.toString();
  for (let newline; (newline = buffer.indexOf("\n")) >= 0;) {
    const line = buffer.slice(0, newline).trim();
    buffer = buffer.slice(newline + 1);
    if (!line) continue;
    const message = JSON.parse(line);
    const complete = pending.get(message.id);
    if (complete) { pending.delete(message.id); complete(message); }
  }
});

function send(method, params) {
  const id = nextId++;
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 60000);
    pending.set(id, (message) => { clearTimeout(timer); resolve(message); });
  });
}

/** One tool call, timed at the client boundary. Never throws on a failed
    command: the caller decides whether a failure is a refusal or a skip. */
async function call(name, args = {}) {
  const started = process.hrtime.bigint();
  const message = await send("tools/call", { name, arguments: args });
  const clientMs = Number(process.hrtime.bigint() - started) / 1e6;
  roundTrips += 1;
  const text = message?.result?.content?.[0]?.text;
  const result = typeof text === "string" ? JSON.parse(text) : { success: false, errors: ["no JSON content"] };
  return { result, clientMs, bytes: typeof text === "string" ? Buffer.byteLength(text, "utf-8") : 0 };
}

function refuse(refusal, extra = {}) {
  console.error(JSON.stringify({ ...refusal, ...extra }, null, 2));
  console.error("\nNo results file was written. Start the editor for "
    + `${projectRoot}, confirm it advertises a session, and run again.`);
  child.kill();
  process.exit(2);
}

function gitCommit() {
  try {
    return execSync("git rev-parse HEAD", { cwd: root, encoding: "utf-8" }).trim();
  } catch {
    return "unknown";
  }
}

/**
 * Run one scenario: warm-up calls that are discarded, then measured calls.
 * A failure during the measured phase skips the whole scenario with the
 * editor's own error rather than reporting a partial distribution.
 */
async function runScenario(spec) {
  const base = {
    name: spec.name,
    tool: spec.tool,
    layer: spec.layer ?? "client_round_trip",
    iterations: 0,
    round_trips_per_iteration: 1,
    target_ms: spec.targetMs ?? null,
    target_met: null,
  };
  if (spec.skip) {
    return { ...base, status: "skipped", skip_reason: spec.skip };
  }
  if (spec.targetBasis) base.target_basis = spec.targetBasis;

  for (let i = 0; i < warmup; i += 1) {
    const { result } = await call(spec.tool, spec.args);
    if (result.success !== true) {
      return { ...base, status: "skipped", skip_reason: `warm-up failed: ${(result.errors ?? []).join("; ") || result.message}` };
    }
  }

  const clientSamples = [];
  const nativeSamples = [];
  const byteSamples = [];
  for (let i = 0; i < iterations; i += 1) {
    const { result, clientMs, bytes } = await call(spec.tool, spec.args);
    if (result.success !== true) {
      return { ...base, status: "skipped", skip_reason: `iteration ${i} failed: ${(result.errors ?? []).join("; ") || result.message}` };
    }
    clientSamples.push(clientMs);
    byteSamples.push(bytes);
    if (typeof result.native_duration_ms === "number") nativeSamples.push(result.native_duration_ms);
    // blueprint_graph_patch and the planners report their own elapsed_ms.
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

const startedAt = new Date();
const wallStart = process.hrtime.bigint();
try {
  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "perf-benchmark", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  // Probe first. Every later measurement assumes an editor answered; if none
  // did, this is where the run stops.
  const probe = await call("puerts_diagnostic", { actor_limit: 1 });
  const refusal = describeRefusal(probe.result);
  if (refusal) refuse(refusal, { probe_tool: "puerts_diagnostic", project_root: projectRoot });

  const diagnostics = probe.result.data ?? {};
  const actorCount = Number(diagnostics.actor_count_total ?? 0);

  // Pick a live actor for the read and mutate scenarios. Actor.bHidden is on
  // the native writable allowlist and setting it to the value it already has
  // is the smallest honest "simple property mutation": it dirties the package
  // and opens a transaction without changing what the level looks like.
  const found = await call("puerts_find_actors", { limit: 500 });
  const actors = found.result.data?.actors ?? [];
  const actorName = actors.length > 0 ? (actors[0].name ?? actors[0].label ?? "") : "";
  let mutationSkip = actorName ? "" : "no actor in the editor level to read or mutate";
  let hiddenValue = false;
  if (actorName) {
    const read = await call("puerts_read_property", { actor: actorName, property: "bHidden" });
    if (read.result.success === true) hiddenValue = Boolean(read.result.data?.value ?? false);
    else mutationSkip = `bHidden is not readable on ${actorName}: ${(read.result.errors ?? []).join("; ")}`;
  }

  const specs = [
    {
      name: "diagnostic",
      tool: "puerts_diagnostic",
      args: { actor_limit: 1 },
      layer: "client_round_trip",
      targetBasis: "no target; this is the empty-payload round-trip floor that every other number sits on top of",
    },
    {
      name: "find_actors_500",
      tool: "puerts_find_actors",
      args: { limit: 500 },
      layer: "client_round_trip",
      targetMs: 50,
      targetBasis: "handoff section 9 suggested target: p95 query of 500 loaded actors below 50 ms",
    },
    {
      name: "find_assets_blueprints",
      tool: "puerts_find_assets",
      args: { path: "/Game", type: "Blueprint", recursive: true, limit: 100 },
      layer: "client_round_trip",
      targetBasis: "asset registry scan; no target set, recorded so a regression is visible",
    },
    {
      name: "read_property_bHidden",
      tool: "puerts_read_property",
      args: { actor: actorName, property: "bHidden" },
      layer: "client_round_trip",
      skip: mutationSkip || undefined,
    },
    {
      name: "set_property_bHidden",
      tool: "puerts_set_property",
      args: { actor: actorName, property: "bHidden", value: hiddenValue },
      layer: "client_round_trip",
      targetMs: 100,
      targetBasis: "handoff section 9 suggested target: p95 simple property mutation below 100 ms, excluding save and compile",
      skip: mutationSkip || undefined,
    },
    {
      name: "save_level",
      tool: "puerts_save",
      args: {},
      layer: "save",
      targetBasis: "no target; save is dominated by Unreal package writing and is measured separately on purpose",
      skip: includeSave ? undefined : "not requested; pass --include-save (it writes to disk)",
    },
    {
      name: "viewport_screenshot",
      tool: "puerts_viewport_screenshot",
      args: {},
      layer: "client_round_trip",
      targetBasis: "no target; capture is bounded by the render thread, not the bridge",
      skip: includeScreenshot ? undefined : "not requested; pass --include-screenshot (it writes a PNG)",
    },
  ];

  const scenarios = [];
  for (const spec of specs) {
    process.stderr.write(`  running ${spec.name}...`);
    const scenario = await runScenario(spec);
    scenarios.push(scenario);
    process.stderr.write(scenario.status === "measured"
      ? ` p50 ${scenario.client_round_trip_ms.p50.toFixed(2)} ms, p95 ${scenario.client_round_trip_ms.p95.toFixed(2)} ms\n`
      : ` skipped (${scenario.skip_reason})\n`);
  }

  const finishedAt = new Date();
  const report = buildRunReport({
    run: {
      run_id: `${label}-${startedAt.toISOString().replace(/[:.]/g, "-")}`,
      label,
      started_at: startedAt.toISOString(),
      finished_at: finishedAt.toISOString(),
      iterations,
      warmup,
      notes: "One machine, one editor session. Not a baseline until docs/PERFORMANCE.md's baseline conditions are met.",
    },
    environment: {
      host: hostname(),
      platform: platform(),
      cpu_model: cpus()[0]?.model ?? "unknown",
      cpu_count: cpus().length,
      total_memory_bytes: totalmem(),
      node_version: process.version,
      bridge_commit: gitCommit(),
      project_root: projectRoot,
      editor: {
        session_id: String(diagnostics.session_id ?? ""),
        editor_pid: Number(diagnostics.editor_pid ?? 0),
        actor_count_total: actorCount,
        is_game_thread: Boolean(diagnostics.is_game_thread),
      },
    },
    scenarios,
    wallMs: Number(process.hrtime.bigint() - wallStart) / 1e6,
    roundTrips,
  });

  const { ok, problems } = validateRunReport(report);
  if (!ok) {
    console.error("The results file does not match docs/evidence/perf-run.schema.json:");
    for (const problem of problems) console.error(`  - ${problem}`);
    child.kill();
    process.exit(1);
  }

  const baselinePath = flag("--baseline", "");
  if (baselinePath) {
    const rows = compareRuns(report, JSON.parse(readFileSync(baselinePath, "utf-8")));
    console.log(`\ncompared against ${baselinePath}`);
    for (const row of rows) {
      const pct = row.p95_delta_pct === null ? "n/a" : `${row.p95_delta_pct >= 0 ? "+" : ""}${row.p95_delta_pct.toFixed(1)}%`;
      console.log(`  ${row.name}: p95 ${row.p95_before.toFixed(2)} -> ${row.p95_after.toFixed(2)} ms (${pct})`);
    }
    if (rows.length === 0) console.log("  no scenario was measured in both runs");
  }

  console.log("");
  for (const scenario of report.scenarios) {
    if (scenario.status !== "measured") { console.log(`  SKIP  ${scenario.name}: ${scenario.skip_reason}`); continue; }
    const s = scenario.client_round_trip_ms;
    const verdict = scenario.target_met === null ? "" : (scenario.target_met ? "  TARGET MET" : "  TARGET MISSED");
    console.log(`  ${scenario.name}: p50 ${s.p50.toFixed(2)}  p95 ${s.p95.toFixed(2)}  max ${s.max.toFixed(2)} ms`
      + `  (n=${s.count})${verdict}`);
  }
  console.log(`\n  round trips: ${report.totals.round_trips}, wall ${report.totals.wall_ms.toFixed(0)} ms`);

  if (writeFile) {
    const day = startedAt.toISOString().slice(0, 10);
    const outPath = flag("--out", join(root, "docs", "evidence", `perf-run-${label}-${day}.json`));
    writeFileSync(outPath, JSON.stringify(report, null, 2) + "\n", "utf-8");
    console.log(`  wrote ${outPath}`);
  } else {
    console.log(JSON.stringify(report, null, 2));
  }
} finally {
  child.kill();
}
