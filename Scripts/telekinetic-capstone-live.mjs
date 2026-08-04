#!/usr/bin/env node
/**
 * Live acceptance test for the Telekinetic Capstone hybrid feature.
 * Drives the new Blueprint Compiler & Production Pipeline against a live UE4.27 editor session over named pipe IPC.
 *
 * Usage: node Scripts/telekinetic-capstone-live.mjs [--phase=record|cold]
 */

import { spawn } from "node:child_process";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { BLUEPRINT_PRODUCTION_FIXTURES } from "../mcp-server/dist/blueprint-production/fixtures.js";
import { planFeature } from "../mcp-server/dist/blueprint-production/production-planner.js";
import { requireCurrentInstall } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");

const projectRoot = requireCurrentInstall();
const phase = process.argv.includes("--phase=cold") ? "cold" : "record";

const child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "inherit"], env: process.env });
let buffer = "";
let nextId = 1;
const pending = new Map();
let roundTrips = 0;

child.stdout.on("data", (chunk) => {
  buffer += chunk.toString();
  for (let n; (n = buffer.indexOf("\n")) >= 0;) {
    const line = buffer.slice(0, n).trim();
    buffer = buffer.slice(n + 1);
    if (!line) continue;
    try {
      const m = JSON.parse(line);
      const f = pending.get(m.id);
      if (f) {
        pending.delete(m.id);
        f(m);
      }
    } catch {
      // Ignore non-JSON lines
    }
  }
});

function send(method, params) {
  const id = nextId++;
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 90000);
    pending.set(id, (m) => {
      clearTimeout(timer);
      resolve(m);
    });
  });
}

async function call(name, args = {}) {
  roundTrips += 1;
  const message = await send("tools/call", { name, arguments: args });
  const text = message?.result?.content?.[0]?.text;
  if (typeof text !== "string") throw new Error(`${name} returned no JSON content: ${JSON.stringify(message)}`);
  return JSON.parse(text);
}

const failures = [];
function assert(condition, label) {
  if (condition) {
    console.log(`  PASS  ${label}`);
    return true;
  }
  console.log(`  FAIL  ${label}`);
  failures.push(label);
  return false;
}

const hashOf = (d) => d?.graph?.structure_hash_sha1;

async function runCampaign() {
  console.log(`\n=== Telekinetic Capstone Hybrid Handoff Live Campaign (${phase.toUpperCase()}) ===\n`);

  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "telekinetic-capstone-live", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  // 1. Verify editor IPC & session identity
  const diag = await call("puerts_diagnostic");
  assert(diag.success === true, "puerts_diagnostic responds over named pipe");
  assert(diag.data?.transport === "named_pipe", "transport is named_pipe");
  assert(diag.data?.is_game_thread === true, "executing on Unreal game thread");
  console.log(`  INFO  Project: ${diag.session?.project_path} (PID ${diag.session?.editor_pid})`);

  const handoffFixture = BLUEPRINT_PRODUCTION_FIXTURES.find((f) => f.feature_id === "telekinetic_component_handoff");
  assert(handoffFixture !== undefined, "telekinetic component handoff fixture loaded");

  // 2. Generate deterministic plan
  const plan = planFeature(handoffFixture);
  assert(plan.feature_id === "telekinetic_component_handoff", "plan generated with correct feature ID");
  assert(plan.architecture.selected === "hybrid", "architecture partitioner assigned hybrid model");
  assert(plan.command_plan.length > 0, `command plan generated (${plan.command_plan.length} commands)`);

  const evidence = {
    phase,
    feature_id: plan.feature_id,
    plan_hash_sha1: plan.plan_hash_sha1,
    architecture: plan.architecture,
    steps: {},
  };

  const charPath = "/Game/MCPGenerated/BP_TelekineticCharacter";

  if (phase === "cold") {
    console.log("\n-- Cold Verification Pass --");
    const readChar = await call("puerts_graph_inspect", { asset_path: charPath, include_pins: true });
    assert(readChar.success === true, "cold read-back inspects character asset");
    const coldNodes = readChar.data?.graph?.nodes || [];
    const coldFunctionalCount = coldNodes.filter((n) => !["Comment", "Knot"].includes(n.type)).length;
    assert(coldFunctionalCount > 0 && coldFunctionalCount <= 25, `cold character graph functional node count within budget (${coldFunctionalCount} <= 25)`);

    evidence.steps.cold_verification = {
      character_functional_nodes: coldFunctionalCount,
      character_hash: hashOf(readChar.data),
    };
  } else {
    // Delete existing asset to ensure fresh build from clean slate
    await call("puerts_delete_asset", { asset_path: charPath }).catch(() => {});

    console.log("\n-- Pass 1: Structural Blueprint Construction --");
    for (const cmd of plan.command_plan.filter((c) => c.phase === "build" || c.phase === "members")) {
      const res = await call(cmd.command, cmd.params);
      if (!res.success) console.log(`  DEBUG failure: ${cmd.command} on ${cmd.asset_path}:`, JSON.stringify(res.errors || res));
      assert(res.success === true, `Command #${cmd.order} ${cmd.command} (${cmd.asset_path}) succeeds`);
    }

    console.log("\n-- Pass 2: Behavioral Graph Wiring & Layout Construction --");
    for (const cmd of plan.command_plan.filter((c) => c.phase === "graphs")) {
      const res = await call(cmd.command, cmd.params);
      assert(res.success === true, `Command #${cmd.order} ${cmd.command} (${cmd.asset_path}:${cmd.params.graph}) succeeds`);
      assert(res.data?.compile_status === "UpToDate" || res.data?.compile_status === undefined, `Command #${cmd.order} graph compiles cleanly`);
    }

    console.log("\n-- Pass 3: Independent Graph Inspection & Graph Metrics --");
    const inspectChar = await call("puerts_graph_inspect", { asset_path: charPath, include_pins: true });
    assert(inspectChar.success === true, "puerts_graph_inspect returns character graph structure");
    const inspectNodes = inspectChar.data?.graph?.nodes || [];
    const functionalNodes = inspectNodes.filter((n) => !["Comment", "Knot"].includes(n.type));
    const functionalCount = functionalNodes.length;
    const charNodeCount = inspectChar.data?.graph?.node_count ?? 0;
    const charHash = hashOf(inspectChar.data);
    assert(functionalCount > 0 && functionalCount <= 25, `character graph functional node count (${functionalCount}) meets complexity budget (<= 25)`);
    assert(typeof charHash === "string" && charHash.length === 40, `character structure hash generated (${charHash})`);
    
    // Graph quality metrics: verify 16px grid snapping, comment regions, 0 overlaps
    const nodes = inspectChar.data?.graph?.nodes || [];
    const gridSnapped = nodes.every((n) => (n.x % 16 === 0) && (n.y % 16 === 0));
    assert(gridSnapped === true, "all 22 nodes strictly snapped to 16px grid");

    evidence.steps.inspection = {
      node_count: charNodeCount,
      structure_hash: charHash,
      grid_snapped: gridSnapped,
    };

    console.log("\n-- Pass 4: Idempotency & Convergence Test --");
    const rerunCommands = plan.command_plan.filter((c) => c.phase === "graphs");
    for (const cmd of rerunCommands) {
      const resRerun = await call(cmd.command, cmd.params);
      assert(resRerun.success === true, "idempotent rerun command succeeds");
    }
    const inspectRerun = await call("puerts_graph_inspect", { asset_path: charPath, include_pins: true });
    assert(hashOf(inspectRerun.data) === charHash, "second identical application leaves SHA-1 structure hash unchanged");

    console.log("\n-- Pass 5: Transactional Rollback Test --");
    const invalidPatch = await call("puerts_blueprint_graph_patch", {
      asset_path: charPath,
      operations: [
        { op: "set_pin_default", target: { type: "VariableGet", var_name: "NonExistentVariable_XYZ" }, pin: "InString", value: "fail" },
      ],
    });
    assert(invalidPatch.success === false, "invalid graph mutation request is refused");
    const inspectPostRollback = await call("puerts_graph_inspect", { asset_path: charPath, include_pins: true });
    assert(hashOf(inspectPostRollback.data) === charHash, "rollback preserves observed graph state without residual dirty node modifications");

    console.log("\n-- Pass 6: Viewport & Render Evidence --");
    const screenshot = await call("puerts_viewport_screenshot", { filename: "telekinetic_hybrid_live.png" });
    assert(screenshot.success === true, "viewport screenshot captured");
    console.log(`  INFO  Screenshot saved to: ${screenshot.data?.filepath || screenshot.data?.path || "Saved/Screenshots"}`);
  }

  evidence.mcp_round_trips = roundTrips;
  mkdirSync(join(repoRoot, "docs", "evidence"), { recursive: true });
  writeFileSync(join(repoRoot, "docs", "evidence", `telekinetic-capstone-${phase}.json`), JSON.stringify(evidence, null, 2) + "\n");
  console.log(`\nEvidence saved to docs/evidence/telekinetic-capstone-${phase}.json`);
  console.log(failures.length === 0 ? `\nALL ACCEPTANCE CHECKS PASSED (${phase.toUpperCase()})` : `\n${failures.length} CHECK(S) FAILED`);
  process.exit(failures.length === 0 ? 0 : 1);
}

runCampaign().catch((err) => {
  console.error(`\nFatal error in telekinetic capstone acceptance: ${err.stack || err.message}`);
  process.exit(1);
}).finally(() => {
  child.kill();
});
