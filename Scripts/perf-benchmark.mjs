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
 *   --include-compile    add the compile pair and the authoring workflow. Runs
 *                        puerts_blueprint_build against --blueprint, which
 *                        CREATES that asset if it does not exist.
 *   --include-pie        add the Play In Editor cycle. AGENTS.md requires the
 *                        user to ask for PIE, so this is never on by default.
 *   --blueprint <path>   the Blueprint the compile and authoring scenarios use,
 *                        default /Game/MCPGenerated/BP_BridgePerfFixture
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
import { execSync, spawn } from "node:child_process";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { hostname, cpus, platform, totalmem } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";
import {
  buildRunReport, checkShape, compareRuns, deriveEditorStartupScenario, describeRefusal,
  runPieCycleScenario, runScenario, runWorkflowScenario, validateRunReport,
} from "./perf-stats.mjs";

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
const includeCompile = argv.includes("--include-compile");
const includePie = argv.includes("--include-pie");
const fixtureBlueprint = String(flag("--blueprint", "/Game/MCPGenerated/BP_BridgePerfFixture"));
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
  if (typeof text !== "string") {
    // A JSON-RPC level failure: an unknown tool, a schema rejection at the
    // transport, a server crash. Carrying the protocol error through is the
    // difference between "the editor is slow" and "this tool does not exist".
    return {
      result: {
        success: false,
        errors: [message?.error
          ? `MCP protocol error calling ${name}: ${JSON.stringify(message.error)}`
          : `MCP response for ${name} carried no text content: ${JSON.stringify(message).slice(0, 300)}`],
      },
      clientMs,
      bytes: 0,
    };
  }
  let result;
  try {
    result = JSON.parse(text);
  } catch (error) {
    result = { success: false, errors: [`${name} returned text that is not JSON (${String(error)}): ${text.slice(0, 300)}`] };
  }
  return { result, clientMs, bytes: Buffer.byteLength(text, "utf-8") };
}

function refuse(refusal, extra = {}) {
  console.error(JSON.stringify({ ...refusal, ...extra }, null, 2));
  console.error("\nNo results file was written. Start the editor for "
    + `${projectRoot}, confirm it advertises a session, and run again.`);
  child.kill();
  process.exit(2);
}

/**
 * Stop on a response that is not shaped the way this harness reads it.
 *
 * Separate from refuse() on purpose: a session refusal means "no editor", and
 * a shape mismatch means "an editor answered and the contract moved". They are
 * fixed by different people and must not print the same reason code. Both
 * write no results file, because a run that guessed at a field it could not
 * find would report numbers for the wrong work.
 */
function refuseShape(problem) {
  console.error(JSON.stringify({
    refused: true,
    measured: false,
    reason_code: "response_shape_mismatch",
    reason: "An editor answered, but a response does not carry the fields this harness reads. "
      + "The benchmark cannot build its scenarios from it. This is a contract change or a bug, "
      + "not a slow bridge.",
    problem,
  }, null, 2));
  console.error("\nNo results file was written.");
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

  // Everything below reads fields out of live responses. Each read is checked
  // before it is used: a missing field used to become `undefined` in a scenario
  // argument, which measures the wrong thing or crashes far from the cause.
  //
  // The environment block is checked hardest, because bridge_commit, the
  // session id and actor_count_total are the fields that decide whether one
  // results file is comparable to another. A run that records session_id "" is
  // a file nobody can compare later, and it looks exactly like a good one.
  const probeShape = checkShape("puerts_diagnostic", probe.result, [
    ["data.actor_count_total", "number"],
    ["data.is_game_thread", "boolean"],
    // Identity lives on the response envelope, not in data: BuildBaseResponse
    // puts it under "session" for EVERY response including refusals
    // (MCPPuerTSBridgeService.cpp:1322-1336). Reading it from data silently
    // produced session_id "" and editor_pid 0 in every results file.
    ["session.session_id", "string+"],
    ["session.editor_pid", "number"],
  ]);
  if (probeShape) refuseShape(probeShape);

  const diagnostics = probe.result.data;
  const probeSession = probe.result.session;
  const actorCount = Number(diagnostics.actor_count_total);

  // Pick a live actor for the read and mutate scenarios. Actor.bHidden is on
  // the native writable allowlist and setting it to the value it already has
  // is the smallest honest "simple property mutation": it dirties the package
  // and opens a transaction without changing what the level looks like.
  const found = await call("puerts_find_actors", { limit: 500 });
  const actorsShape = checkShape("puerts_find_actors", found.result, [["data.actors", "array"]]);
  if (actorsShape) refuseShape(actorsShape);
  const actors = found.result.data.actors;
  let mutationSkip = "";
  let actorName = "";
  if (actors.length === 0) {
    mutationSkip = "no actor in the editor level to read or mutate";
  } else {
    const nameShape = checkShape("puerts_find_actors", found.result, [["data.actors.0.name", "string+"]]);
    if (nameShape) refuseShape(nameShape);
    actorName = actors[0].name;
  }
  let hiddenValue = false;
  if (actorName) {
    const read = await call("puerts_read_property", { actor: actorName, property: "bHidden" });
    if (read.result.success !== true) {
      mutationSkip = `bHidden is not readable on ${actorName}: ${(read.result.errors ?? []).join("; ")}`;
    } else {
      // This one is not cosmetic. set_property_bHidden writes this value back.
      // The old code did Boolean(data?.value ?? false), so a missing or
      // non-boolean field became false and the "no-op mutation" scenario
      // silently UNHID an actor twenty-three times. A property whose read did
      // not produce a boolean is not something to write back.
      const valueShape = checkShape("puerts_read_property", read.result, [["data.value", "boolean"]]);
      if (valueShape) mutationSkip = `bHidden did not read back as a boolean, so writing it back `
        + `would not be a no-op. ${valueShape}`;
      else hiddenValue = read.result.data.value;
    }
  }

  // A Blueprint to inspect. Discovery rather than a hardcoded fixture, because
  // graph_inspect is read-only and any project Blueprint measures the same path;
  // requiring a fixture would skip this scenario on every project that has not
  // run the acceptance scripts.
  const blueprints = await call("puerts_find_assets", { path: "/Game", type: "Blueprint", recursive: true, limit: 1 });
  const assetsShape = checkShape("puerts_find_assets", blueprints.result, [["data.assets", "array"]]);
  if (assetsShape) refuseShape(assetsShape);
  let firstBlueprint = "";
  let inspectSkip = "";
  if (blueprints.result.data.assets.length === 0) {
    inspectSkip = "no Blueprint under /Game to inspect";
  } else {
    const pathShape = checkShape("puerts_find_assets", blueprints.result, [["data.assets.0.path", "string+"]]);
    if (pathShape) refuseShape(pathShape);
    firstBlueprint = blueprints.result.data.assets[0].path;
  }

  // The compile and authoring scenarios write. blueprint_build is convergent, so
  // re-running the same spec against an existing asset changes nothing, but the
  // first run CREATES the fixture. That is a mutation, so it is opt-in.
  const compileSkip = includeCompile
    ? ""
    : `not requested; pass --include-compile (it runs puerts_blueprint_build against ${fixtureBlueprint} and creates it if absent)`;
  const fixtureSpec = (compile) => ({
    asset_path: fixtureBlueprint,
    parent_class: "Actor",
    compile,
    save: false,
    clear_existing_graph: false,
  });

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
      name: "graph_inspect",
      tool: "puerts_graph_inspect",
      args: { asset_path: firstBlueprint, include_pins: false },
      layer: "client_round_trip",
      targetBasis: "the read half of the authoring loop. Every desired-state build is verified with this, "
        + "so its cost is paid on every authoring job, not only on reads",
      skip: inspectSkip || undefined,
    },
    {
      name: "graph_inspect_with_pins",
      tool: "puerts_graph_inspect",
      args: { asset_path: firstBlueprint, include_pins: true },
      layer: "serialization",
      targetBasis: "the same query with pin data. The difference against graph_inspect is what pin "
        + "serialization costs, which is the largest payload the inspectors produce",
      skip: inspectSkip || undefined,
    },
    // The compile pair. Neither number means much alone; the difference between
    // them is the Blueprint compile, which is otherwise buried inside the build
    // command's total and is the single largest cost in an authoring job.
    {
      name: "blueprint_build_no_compile",
      tool: "puerts_blueprint_build",
      args: fixtureSpec(false),
      layer: "client_round_trip",
      targetBasis: "convergent no-op build with compile off. The control for blueprint_build_compile",
      skip: compileSkip || undefined,
    },
    {
      name: "blueprint_build_compile",
      tool: "puerts_blueprint_build",
      args: fixtureSpec(true),
      layer: "compile",
      targetBasis: "the same no-op build with compile on. blueprint_build_compile minus "
        + "blueprint_build_no_compile is FKismetEditorUtilities::CompileBlueprint "
        + "(BlueprintGraphBuilderLibrary.cpp:1950), which the bridge cannot time separately today",
      skip: compileSkip || undefined,
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

  // Workflows. The number that matters here is round_trips_per_iteration, not
  // the milliseconds: AGENTS.md's product goal is the fewest editor round trips,
  // and a change that halves per-call latency while doubling the call count is a
  // regression that only this count can see.
  const workflowSpecs = [
    {
      name: "workflow_survey_project",
      targetBasis: "what it costs an agent to orient itself in a project it has not seen: which editor, "
        + "what is in the level, what Blueprints exist, and what one of them contains. Four round trips",
      skip: inspectSkip || undefined,
      steps: [
        { name: "identify_editor", tool: "puerts_diagnostic", args: { actor_limit: 1 } },
        { name: "list_actors", tool: "puerts_find_actors", args: { limit: 500 } },
        { name: "list_blueprints", tool: "puerts_find_assets", args: { path: "/Game", type: "Blueprint", recursive: true, limit: 100 } },
        { name: "read_one_graph", tool: "puerts_graph_inspect", args: { asset_path: firstBlueprint, include_pins: false } },
      ],
    },
    {
      name: "workflow_author_and_verify",
      targetBasis: "the fast path AGENTS.md describes: state the desired Blueprint, compile it, and read it "
        + "back with an independent inspector. Three round trips. The slow path it replaces is "
        + "create-node, inspect, connect-pin, inspect, set-property, inspect",
      skip: compileSkip || undefined,
      steps: [
        { name: "plan", tool: "puerts_blueprint_build", args: { ...fixtureSpec(false), plan_only: true } },
        { name: "apply_and_compile", tool: "puerts_blueprint_build", args: fixtureSpec(true) },
        { name: "verify", tool: "puerts_graph_inspect", args: { asset_path: fixtureBlueprint, include_pins: true } },
      ],
    },
  ];

  const scenarios = [];
  const record = (scenario) => {
    scenarios.push(scenario);
    process.stderr.write(scenario.status === "measured"
      ? ` p50 ${scenario.client_round_trip_ms.p50.toFixed(2)} ms, p95 ${scenario.client_round_trip_ms.p95.toFixed(2)} ms\n`
      : ` skipped (${scenario.skip_reason})\n`);
  };

  for (const spec of specs) {
    process.stderr.write(`  running ${spec.name}...`);
    record(await runScenario(spec, { call, iterations, warmup }));
  }
  for (const spec of workflowSpecs) {
    process.stderr.write(`  running ${spec.name}...`);
    record(await runWorkflowScenario(spec, { call, iterations, warmup }));
  }

  // Read from the manifest the editor already publishes rather than restarting
  // anything: this harness is not allowed to launch an editor, and the manifest
  // carries the process creation time and the moment the bridge went live.
  process.stderr.write("  deriving editor_startup_to_bridge_ready...");
  let session = {};
  try {
    session = JSON.parse(readFileSync(join(projectRoot, "Saved", "MCPPuerTSBridge", "session.json"), "utf-8"));
  } catch (error) {
    session = { unreadable: String(error) };
  }
  record(deriveEditorStartupScenario(session));

  // Last, because it changes what world the editor is in and everything above
  // is editor-only. AGENTS.md requires the user to ask before PIE starts.
  process.stderr.write("  running pie_cycle...");
  record(await runPieCycleScenario(
    {
      name: "pie_cycle",
      targetBasis: "start request, time until a PIE world really exists, stop request, time until the "
        + "editor world is back. The request round trips alone would be meaningless: RequestPlaySession "
        + "only queues, so pie_start returns in the time it takes to set a flag",
      skip: includePie ? undefined : "not requested; pass --include-pie, and AGENTS.md requires the user to ask for PIE first",
    },
    { call, sleep: (ms) => new Promise((resolve) => setTimeout(resolve, ms)) },
  ));

  const finishedAt = new Date();
  const report = buildRunReport({
    run: {
      run_id: `${label}-${startedAt.toISOString().replace(/[:.]/g, "-")}`,
      label,
      started_at: startedAt.toISOString(),
      finished_at: finishedAt.toISOString(),
      iterations,
      warmup,
      notes: "One machine, one editor session. Not a baseline until docs/PERF_AND_LONG_JOBS.md's baseline conditions are met.",
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
        session_id: String(probeSession.session_id),
        editor_pid: Number(probeSession.editor_pid),
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
    const trips = scenario.round_trips_per_iteration === 1 ? "" : `  ${scenario.round_trips_per_iteration} round trips`;
    console.log(`  ${scenario.name}: p50 ${s.p50.toFixed(2)}  p95 ${s.p95.toFixed(2)}  max ${s.max.toFixed(2)} ms`
      + `  (n=${s.count})${trips}${verdict}`);
    for (const step of scenario.steps ?? []) {
      console.log(`      ${step.name}: p50 ${step.client_round_trip_ms.p50.toFixed(2)} ms`);
    }
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
