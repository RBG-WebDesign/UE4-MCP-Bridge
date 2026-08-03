#!/usr/bin/env node
/**
 * Build - inspect - compile - review - repair, driven end to end from one plan.
 *
 * This composes commands that already exist. It adds no transport, no second
 * inspection system, no rollback of its own and no registry: every stage is a
 * puerts_* call through the built MCP server on stdio, exactly as a client
 * makes it. What it adds is the loop AGENTS.md asks for and nothing in this
 * repository performed unattended: state the desired Blueprint, apply it, read
 * it back with the independent inspector, compare against what was asked for,
 * repair once, and say at every stage what was observed and what changed.
 *
 *   node Scripts/bridge-orchestrator.mjs --plan <plan.json> [options]
 *
 *   --plan <file>       the plan. Required. Shape documented below.
 *   --state <file>      run state, default docs/evidence/orchestrator-<name>.json
 *   --resume            skip stages the state file already records as ok
 *   --pie               start Play In Editor for the pie stage. OFF BY DEFAULT.
 *                       AGENTS.md requires the user to ask before PIE runs.
 *   --screenshot        capture the viewport in the review stage (writes a PNG)
 *   --max-repairs <n>   repair attempts before giving up, default 1
 *   --no-write          do not write the state file
 *
 * Exit codes: 0 every stage ok, 2 refused (no editor addressed), 1 a stage
 * failed or a repair did not converge. A refusal writes no state file.
 *
 * The plan:
 *
 * {
 *   "name": "probe_door",
 *   "blueprint": { <a puerts_blueprint_build spec: asset_path, parent_class,
 *                   components, variables, graph> },
 *   "expect": { "components": ["Mesh"], "variables": ["IsOpen"],
 *               "min_nodes": 3, "no_unmapped_nodes": true },
 *   "repair_operations": [ <puerts_blueprint_graph_patch operations> ]
 * }
 *
 * `expect` is checked against the INSPECTOR's answer, not the builder's. That
 * is the whole point of the stage: a builder that reports success and an
 * inspector that cannot find the node disagree, and only the inspector was not
 * involved in writing it.
 *
 * Round trips are counted per stage and in total, because AGENTS.md's product
 * goal is the fewest editor round trips and a loop like this is exactly where
 * they multiply without anyone noticing.
 */
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { checkShape, describeRefusal } from "./perf-stats.mjs";

export const STATE_SCHEMA_VERSION = 1;
export const STATE_KIND = "bridge-orchestrator-run";

// ------------------------------------------------------------------- pure

/**
 * A fingerprint of the parts of the plan that decide the outcome.
 *
 * Resume compares this. Resuming a changed plan into an old state file would
 * skip a build stage that already ran for a DIFFERENT desired state and then
 * report the stages that follow as verifying it, which is a lie that looks
 * exactly like a fast run. Cosmetic fields (name) are outside the fingerprint
 * on purpose; everything the editor sees is inside it.
 */
export function planFingerprint(plan) {
  const canonical = (value) => {
    if (Array.isArray(value)) return value.map(canonical);
    if (value !== null && typeof value === "object") {
      return Object.fromEntries(Object.keys(value).sort().map((key) => [key, canonical(value[key])]));
    }
    return value;
  };
  const material = { blueprint: plan.blueprint ?? null, expect: plan.expect ?? null, repair_operations: plan.repair_operations ?? null };
  return createHash("sha256").update(JSON.stringify(canonical(material))).digest("hex");
}

/**
 * Did the build land? Reads the builder's own report rather than trusting
 * `success`, because a build can succeed as a command and still have dropped
 * connections or failed to compile, and those are different repairs.
 */
export function evaluateBuild(result) {
  const problems = [];
  const data = result?.data ?? {};
  if (result?.success !== true) {
    problems.push(`the build command failed: ${(result?.errors ?? []).join("; ") || "no error reported"}`);
    return { ok: false, problems, observed: {} };
  }
  const unresolved = Array.isArray(data.graph?.unresolved_connections) ? data.graph.unresolved_connections : [];
  if (unresolved.length > 0) {
    problems.push(`${unresolved.length} connection(s) did not resolve to real pins: ${unresolved.slice(0, 5).join("; ")}`);
  }
  // compile_status "Skipped" is what a compile:false build reports and is not
  // a failure. Anything with Error in it is.
  const status = String(data.compile_status ?? "");
  if (status !== "Skipped" && data.compiled !== true) {
    problems.push(`the Blueprint did not compile: status ${status || "unknown"}`);
  }
  if (/error/i.test(status)) problems.push(`compile status is ${status}`);
  return {
    ok: problems.length === 0,
    problems,
    observed: {
      asset_path: data.asset_path ?? null,
      created: data.created ?? null,
      compiled: data.compiled ?? null,
      compile_status: status || null,
      saved: data.saved ?? null,
      node_count: data.graph?.node_count ?? null,
      connection_count: data.graph?.connection_count ?? null,
      converged: data.convergence?.converged ?? null,
      warnings: (data.warnings ?? []).slice(0, 10),
    },
  };
}

/**
 * Compare the inspector's answer against what the plan asked for.
 *
 * Every gap names the thing that is missing, so the repair stage and the human
 * reading the state file are looking at the same list.
 */
export function evaluateInspection(data, expect = {}) {
  const gaps = [];
  const shape = checkShape("puerts_graph_inspect", { data }, [["data.graph", "object"]]);
  if (shape) return { ok: false, gaps: [shape], observed: {} };

  const componentNames = (data.components ?? []).map((c) => c?.name).filter((n) => typeof n === "string");
  const variableNames = (data.variables ?? []).map((v) => v?.name).filter((n) => typeof n === "string");
  const nodeCount = Number(data.graph.node_count ?? 0);
  const unmapped = Array.isArray(data.graph.unmapped_nodes) ? data.graph.unmapped_nodes : [];

  for (const wanted of expect.components ?? []) {
    if (!componentNames.includes(wanted)) gaps.push(`component "${wanted}" is not in the asset; it has [${componentNames.join(", ")}]`);
  }
  for (const wanted of expect.variables ?? []) {
    if (!variableNames.includes(wanted)) gaps.push(`variable "${wanted}" is not in the asset; it has [${variableNames.join(", ")}]`);
  }
  if (typeof expect.min_nodes === "number" && nodeCount < expect.min_nodes) {
    gaps.push(`the graph has ${nodeCount} nodes, the plan expects at least ${expect.min_nodes}`);
  }
  if (expect.no_unmapped_nodes === true && unmapped.length > 0) {
    gaps.push(`${unmapped.length} node(s) have no builder node type: ${unmapped.slice(0, 5).map((n) => JSON.stringify(n)).join("; ")}`);
  }
  return {
    ok: gaps.length === 0,
    gaps,
    observed: {
      graph: data.graph.name ?? null,
      node_count: nodeCount,
      connection_count: data.graph.connection_count ?? null,
      structure_hash_sha1: data.graph.structure_hash_sha1 ?? null,
      components: componentNames,
      variables: variableNames,
      unmapped_node_count: unmapped.length,
      package_dirty_after: data.package_dirty_after ?? null,
    },
  };
}

/**
 * Decide which stages to run.
 *
 * A stage is skipped only when the state file says it finished ok AND the plan
 * fingerprint matches AND --resume was asked for. The first stage that is not
 * ok, and everything after it, is re-run: a later stage's result depends on the
 * earlier one, so resuming past a failure would verify a state that was never
 * reached.
 */
export function stagesToRun(stageNames, priorState, { resume, fingerprint }) {
  if (!resume || priorState === null) return stageNames.map((name) => ({ name, run: true, reason: "" }));
  if (priorState.plan_fingerprint !== fingerprint) {
    return stageNames.map((name) => ({
      name,
      run: true,
      reason: "the plan changed since that state file was written, so nothing in it can be reused",
    }));
  }
  const done = new Map((priorState.stages ?? []).map((stage) => [stage.name, stage.status]));
  let reached = false;
  return stageNames.map((name) => {
    if (reached || done.get(name) !== "ok") {
      reached = true;
      return { name, run: true, reason: "" };
    }
    return { name, run: false, reason: "already ok in the state file" };
  });
}

// -------------------------------------------------------------------- CLI

async function main() {
  const root = join(dirname(fileURLToPath(import.meta.url)), "..");
  const argv = process.argv.slice(2);
  const flag = (name, fallback) => {
    const at = argv.indexOf(name);
    return at >= 0 && argv[at + 1] !== undefined ? argv[at + 1] : fallback;
  };
  const planPath = flag("--plan", "");
  if (!planPath) throw new Error("--plan <file> is required");
  const plan = JSON.parse(readFileSync(planPath, "utf-8"));
  if (plan.blueprint === undefined || typeof plan.blueprint?.asset_path !== "string") {
    throw new Error("the plan needs a blueprint object with an asset_path");
  }
  const planName = String(plan.name ?? "run").replace(/[^A-Za-z0-9_-]/g, "_");
  const resume = argv.includes("--resume");
  const includePie = argv.includes("--pie");
  const includeScreenshot = argv.includes("--screenshot");
  const writeState = !argv.includes("--no-write");
  const maxRepairs = Number(flag("--max-repairs", 1));
  if (!Number.isInteger(maxRepairs) || maxRepairs < 0) throw new Error("--max-repairs must be zero or a positive integer");
  const statePath = flag("--state", join(root, "docs", "evidence", `orchestrator-${planName}.json`));

  const serverPath = join(root, "mcp-server", "dist", "index.js");
  if (!existsSync(serverPath)) throw new Error("Run npm run build first");

  const fingerprint = planFingerprint(plan);
  let priorState = null;
  if (resume && existsSync(statePath)) {
    try {
      priorState = JSON.parse(readFileSync(statePath, "utf-8"));
    } catch (error) {
      console.error(`  the state file at ${statePath} is not readable JSON (${String(error)}); running every stage`);
    }
  }

  // ---- transport: the built server on stdio, like any other client

  const child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "inherit"], env: process.env });
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
  const send = (method, params) => {
    const id = nextId++;
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 120000);
      pending.set(id, (message) => { clearTimeout(timer); resolve(message); });
    });
  };
  const call = async (name, args = {}) => {
    const message = await send("tools/call", { name, arguments: args });
    roundTrips += 1;
    const text = message?.result?.content?.[0]?.text;
    if (typeof text !== "string") {
      return { success: false, errors: [`MCP response for ${name} carried no text: ${JSON.stringify(message).slice(0, 300)}`] };
    }
    try {
      return JSON.parse(text);
    } catch (error) {
      return { success: false, errors: [`${name} returned text that is not JSON (${String(error)}): ${text.slice(0, 300)}`] };
    }
  };
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

  // ---- state

  const stages = [];
  const startedAt = new Date();
  const persist = () => {
    if (!writeState) return;
    mkdirSync(dirname(statePath), { recursive: true });
    writeFileSync(statePath, JSON.stringify({
      schema_version: STATE_SCHEMA_VERSION,
      kind: STATE_KIND,
      plan_name: plan.name ?? planName,
      plan_path: planPath,
      plan_fingerprint: fingerprint,
      started_at: startedAt.toISOString(),
      updated_at: new Date().toISOString(),
      asset_path: plan.blueprint.asset_path,
      stages,
      totals: {
        round_trips: roundTrips,
        stages_ok: stages.filter((s) => s.status === "ok").length,
        stages_failed: stages.filter((s) => s.status === "failed").length,
        stages_skipped: stages.filter((s) => s.status === "skipped").length,
      },
    }, null, 2) + "\n", "utf-8");
  };

  /** Record one stage. `body` returns {ok, observed, changed, errors}. */
  const runStage = async (name, decision, body) => {
    if (!decision.run) {
      const carried = (priorState?.stages ?? []).find((stage) => stage.name === name) ?? {};
      stages.push({ ...carried, name, status: "ok", resumed: true, resume_reason: decision.reason });
      console.log(`  RESUMED ${name}: ${decision.reason}`);
      persist();
      return { ok: true, resumed: true, observed: carried.observed ?? {} };
    }
    const before = roundTrips;
    const started = process.hrtime.bigint();
    process.stdout.write(`  ${name}...`);
    const outcome = await body();
    const record = {
      name,
      status: outcome.skipped ? "skipped" : (outcome.ok ? "ok" : "failed"),
      skip_reason: outcome.skipped ? outcome.reason : undefined,
      round_trips: roundTrips - before,
      ms: Number(process.hrtime.bigint() - started) / 1e6,
      observed: outcome.observed ?? {},
      changed: outcome.changed ?? [],
      errors: outcome.errors ?? [],
    };
    stages.push(record);
    console.log(` ${record.status} (${record.round_trips} round trips, ${record.ms.toFixed(0)} ms)`);
    for (const error of record.errors) console.log(`      ${error}`);
    persist();
    return { ...outcome, observed: record.observed };
  };

  /** changed_assets and changed_actors are on every native response; this is
      the "what did it change" half of every stage report. */
  const changesOf = (result) => [
    ...(result?.changed_assets ?? []).map((path) => `asset ${path}`),
    ...(result?.changed_actors ?? []).map((name) => `actor ${name}`),
  ];

  const stageNames = ["probe", "build", "inspect", "repair", "review", "pie"];
  const decisions = new Map(stagesToRun(stageNames, priorState, { resume, fingerprint }).map((d) => [d.name, d]));
  let failed = false;

  try {
    await send("initialize", {
      protocolVersion: "2024-11-05",
      capabilities: {},
      clientInfo: { name: "bridge-orchestrator", version: "1.0.0" },
    });
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

    // 1. probe. Never resumed: an editor that answered an hour ago proves
    //    nothing about the one that has to answer the stages below.
    const probe = await call("puerts_diagnostic", { actor_limit: 1 });
    const refusal = describeRefusal(probe);
    if (refusal) {
      console.error(JSON.stringify({ ...refusal, stage: "probe" }, null, 2));
      console.error("\nNo state file was written. Start the editor for this project and run again.");
      child.kill();
      process.exit(2);
    }
    stages.push({
      name: "probe",
      status: "ok",
      round_trips: 1,
      observed: {
        session_id: probe.session?.session_id ?? null,
        editor_pid: probe.session?.editor_pid ?? null,
        actor_count_total: probe.data?.actor_count_total ?? null,
      },
      changed: [],
      errors: [],
    });
    console.log(`  probe... ok (editor ${probe.session?.editor_pid ?? "?"}, session ${probe.session?.session_id ?? "?"})`);
    persist();

    // 2. build. One round trip in the good case: compile and save on. A
    //    failure buys a second call with compile off, which is the cheapest
    //    way to answer "did the graph fail to build, or only to compile", and
    //    it is paid only when something is already wrong.
    const buildSpec = { ...plan.blueprint, compile: true, save: true };
    const build = await runStage("build", decisions.get("build"), async () => {
      const result = await call("puerts_blueprint_build", buildSpec);
      const verdict = evaluateBuild(result);
      if (verdict.ok) return { ok: true, observed: verdict.observed, changed: changesOf(result) };
      const structural = await call("puerts_blueprint_build", { ...plan.blueprint, compile: false, save: false });
      const structuralVerdict = evaluateBuild(structural);
      return {
        ok: false,
        observed: { ...verdict.observed, structure_builds_without_compile: structuralVerdict.ok },
        changed: [...changesOf(result), ...changesOf(structural)],
        errors: [
          ...verdict.problems,
          ...(result.errors ?? []),
          structuralVerdict.ok
            ? "the graph builds when compile is off, so this is a compile failure, not a structural one"
            : `the graph does not build even with compile off: ${structuralVerdict.problems.join("; ")}`,
        ],
      };
    });

    // 3. inspect. The independent read-back. Runs even when the build failed,
    //    because what is actually in the asset is the thing a repair needs.
    const inspectArgs = { asset_path: plan.blueprint.asset_path, include_pins: false };
    if (typeof plan.expect?.graph === "string") inspectArgs.graph_name = plan.expect.graph;
    let inspection = null;
    const inspect = await runStage("inspect", decisions.get("inspect"), async () => {
      const result = await call("puerts_graph_inspect", inspectArgs);
      if (result?.success !== true) {
        return { ok: false, errors: [`graph_inspect failed: ${(result?.errors ?? []).join("; ") || "no error reported"}`] };
      }
      inspection = evaluateInspection(result.data ?? {}, plan.expect ?? {});
      return { ok: inspection.ok, observed: inspection.observed, changed: changesOf(result), errors: inspection.gaps };
    });

    // 4. repair. Only when something is wrong, bounded, and it re-inspects
    //    after every attempt: a repair that is not verified by the inspector
    //    is a claim, not a repair.
    const needsRepair = !build.ok || !inspect.ok;
    await runStage("repair", decisions.get("repair"), async () => {
      if (!needsRepair) return { skipped: true, reason: "the build and the read-back both matched the plan" };
      if (maxRepairs === 0) return { skipped: true, reason: "--max-repairs 0" };

      const attempts = [];
      const changed = [];
      for (let attempt = 1; attempt <= maxRepairs; attempt += 1) {
        // Two repairs, in this order. The plan's own patch operations when it
        // has them, because they are the specific fix; otherwise re-applying
        // the desired state, which converges and is the general one. No repair
        // is invented here: a gap this cannot close is reported, not guessed at.
        let result;
        if (Array.isArray(plan.repair_operations) && plan.repair_operations.length > 0) {
          result = await call("puerts_blueprint_graph_patch", {
            asset_path: plan.blueprint.asset_path,
            operations: plan.repair_operations,
            verify: true,
            compile: true,
            save: true,
          });
        } else {
          result = await call("puerts_blueprint_build", buildSpec);
        }
        changed.push(...changesOf(result));
        const reread = await call("puerts_graph_inspect", inspectArgs);
        if (reread?.success !== true) {
          attempts.push({ attempt, applied: result?.success === true, reinspect_failed: (reread?.errors ?? []).join("; ") });
          continue;
        }
        inspection = evaluateInspection(reread.data ?? {}, plan.expect ?? {});
        attempts.push({
          attempt,
          applied: result?.success === true,
          apply_errors: (result?.errors ?? []).slice(0, 5),
          gaps_after: inspection.gaps,
        });
        if (inspection.ok && result?.success === true) {
          return { ok: true, observed: { attempts, ...inspection.observed }, changed };
        }
      }
      return {
        ok: false,
        observed: { attempts, ...(inspection?.observed ?? {}) },
        changed,
        errors: [
          `${maxRepairs} repair attempt(s) did not close the gaps. This is not self-repairable from the plan.`,
          ...(inspection?.gaps ?? []),
        ],
      };
    });

    // 5. review. Logs always, the viewport only when asked: a screenshot
    //    writes a PNG.
    await runStage("review", decisions.get("review"), async () => {
      const logs = await call("puerts_get_logs", { maximum_lines: 100 });
      const lines = logs?.log_output ?? [];
      const errorLines = lines.filter((line) => /error|warning/i.test(String(line))).slice(-20);
      const observed = { log_lines: lines.length, error_or_warning_lines: errorLines };
      const changed = changesOf(logs);
      if (!includeScreenshot) {
        return { ok: logs?.success === true, observed: { ...observed, screenshot: "not requested; pass --screenshot" }, changed };
      }
      const shot = await call("puerts_viewport_screenshot", {});
      return {
        ok: logs?.success === true && shot?.success === true,
        observed: { ...observed, screenshot: shot?.data?.path ?? shot?.data?.filename ?? null },
        changed: [...changed, ...changesOf(shot)],
        errors: shot?.success === true ? [] : (shot?.errors ?? ["the screenshot failed"]),
      };
    });

    // 6. pie. AGENTS.md: the user asks for PIE, the agent does not start it.
    await runStage("pie", decisions.get("pie"), async () => {
      if (!includePie) {
        return {
          skipped: true,
          reason: "not requested. AGENTS.md requires the user to ask before Play In Editor starts; pass --pie",
        };
      }
      const worldNow = async () => {
        const observed = await call("puerts_physics_observe", {});
        if (observed?.success !== true) return { error: (observed.errors ?? []).join("; ") || "physics_observe failed" };
        const shape = checkShape("puerts_physics_observe", observed, [["data.world", "string+"]]);
        if (shape) return { error: shape };
        return { world: observed.data.world };
      };
      const waitFor = async (wanted, timeoutMs = 60000) => {
        const started = Date.now();
        for (;;) {
          const seen = await worldNow();
          if (seen.error) return seen;
          if (seen.world === wanted) return seen;
          if (Date.now() - started > timeoutMs) return { error: `world stayed "${seen.world}" for ${timeoutMs} ms` };
          await sleep(250);
        }
      };

      const before = await worldNow();
      if (before.error) return { ok: false, errors: [before.error] };
      if (before.world !== "editor") return { ok: false, errors: [`the editor is already in world "${before.world}"`] };

      const start = await call("puerts_pie_start", {});
      if (start?.success !== true) return { ok: false, errors: [`pie_start refused: ${(start.errors ?? []).join("; ")}`] };
      const up = await waitFor("pie");
      if (up.error) return { ok: false, errors: [`PIE never became playable: ${up.error}`] };

      // Observe the running session, then always try to stop it. A play
      // session left running by a failed observation is worse than the
      // failure: every editor-only command after it is refused.
      const logs = await call("puerts_get_logs", { maximum_lines: 100 });
      const shot = includeScreenshot ? await call("puerts_viewport_screenshot", {}) : null;
      const stop = await call("puerts_pie_stop", {});
      const down = stop?.success === true ? await waitFor("editor") : { error: "pie_stop refused" };
      return {
        ok: stop?.success === true && !down.error,
        observed: {
          play_log_lines: (logs?.log_output ?? []).length,
          screenshot: shot?.data?.path ?? null,
          returned_to_editor_world: !down.error,
        },
        changed: changesOf(stop),
        errors: down.error ? [down.error] : [],
      };
    });
  } finally {
    child.kill();
  }

  failed = stages.some((stage) => stage.status === "failed");
  persist();
  console.log(`\n  ${stages.filter((s) => s.status === "ok").length}/${stages.length} stages ok, `
    + `${roundTrips} round trips total`);
  for (const stage of stages) {
    if (stage.status === "failed") console.log(`  FAILED ${stage.name}: ${stage.errors.join(" | ")}`);
  }
  if (writeState) console.log(`  state: ${statePath}`);
  process.exit(failed ? 1 : 0);
}

// Only run when invoked directly, so the pure helpers above are importable by
// the test without spawning a server. resolve() on both sides because Windows
// hands argv[1] back with the separators it feels like using.
if (process.argv[1] && resolve(process.argv[1]) === resolve(fileURLToPath(import.meta.url))) {
  await main();
}
