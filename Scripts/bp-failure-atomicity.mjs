#!/usr/bin/env node
// Failure atomicity for puerts_blueprint_build against a running UE4.27 editor.
//
// The Behavior Tree half of this is Scripts/bt-failure-atomicity.mjs; this is
// the same contract for the Blueprint builder. A failed build must leave no
// asset in the Asset Registry, no file on disk, no dirty package, no
// source-control entry, and nothing for the save-on-exit flow to write during
// close. See docs/CAPABILITY_FINDINGS.md defect 0c.
//
// Fixture: a connection whose endpoint names a pin role that does not exist on
// a known node. blueprint_build already validates node types, duplicate ids,
// unknown node ids, component classes and parent classes BEFORE it creates
// anything (findings 25), so those specs never reach the leak. An unresolved
// pin role does: it is only discovered once the graph has been built into a
// real asset, which is exactly the window where a failure used to leave the
// Blueprint behind.
//
// Two phases:
//   --phase=record  three failed builds in one session, recording observations
//   --phase=cold    after a restart, the failed asset must still be absent
//
// Requires MCP_UNREAL_PROJECT_ROOT; pipe and token are discovered from it.
import { execSync, spawn } from "node:child_process";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(root, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
const projectRoot = process.env.MCP_UNREAL_PROJECT_ROOT;
if (!projectRoot) throw new Error("Set MCP_UNREAL_PROJECT_ROOT to the test project root");
delete process.env.MCP_PUERTS_PIPE;
delete process.env.MCP_PUERTS_TOKEN_PATH;
delete process.env.MCP_PUERTS_TOKEN;
const phase = process.argv.includes("--phase=cold") ? "cold" : "record";
const observeOnly = process.argv.includes("--observe");

const child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "inherit"], env: process.env });
let buffer = "";
let nextId = 1;
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

async function call(name, args = {}) {
  const message = await send("tools/call", { name, arguments: args });
  const text = message?.result?.content?.[0]?.text;
  if (typeof text !== "string") throw new Error(`${name} returned no JSON content`);
  return JSON.parse(text);
}

const failures = [];
function assert(condition, label) {
  if (condition) { console.log(`  PASS  ${label}`); return true; }
  console.log(observeOnly ? `  OBSERVED-FAIL  ${label}` : `  FAIL  ${label}`);
  failures.push(label);
  return false;
}

const PROBE = "/Game/MCPGenerated/BP_AtomicityProbe";
const GOOD = "/Game/MCPGenerated/BP_AtomicityGood";
const contentFile = (packagePath) =>
  join(projectRoot, "Content", packagePath.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");

function p4Opened() {
  try {
    return execSync("p4 opened", { cwd: join(projectRoot, "Content"), encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
  } catch {
    return null;
  }
}

async function registryCount(name) {
  const found = await call("puerts_find_assets", {
    path: "/Game/MCPGenerated", name, recursive: true, limit: 50,
  });
  return (found.data?.assets ?? []).length;
}

/** Passes validate-before-mutate, fails once the graph is built into the asset. */
const badSpec = () => ({
  asset_path: PROBE,
  parent_class: "Actor",
  graph: {
    nodes: [
      { id: "begin", type: "BeginPlay" },
      { id: "print", type: "PrintString" },
    ],
    // "begin" has no pin called "nosuchpin". The graph editor discards a
    // connection whose role names no pin of that direction, so the build fails.
    connections: [{ from: "begin.nosuchpin", to: "print.exec" }],
  },
});

const evidence = { phase, iterations: [], observations: {} };

try {
  await send("initialize", {
    protocolVersion: "2024-11-05", capabilities: {},
    clientInfo: { name: "bp-failure-atomicity", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    console.log("\ncold phase: the failed Blueprint must still be absent after a restart");
    const count = await registryCount("BP_AtomicityProbe");
    const onDisk = existsSync(contentFile(PROBE));
    evidence.observations = { cold_registry_count: count, cold_file_on_disk: onDisk };
    assert(count === 0, "(8) restart: the failed Blueprint is absent from the Asset Registry");
    assert(onDisk === false, "(8) restart: no failed Blueprint file exists on disk");
    const inspect = await call("puerts_graph_inspect", { asset_path: PROBE });
    assert(inspect.success === false, "(8) restart: the inspector finds no failed artifact");
    // (10) The successful asset from the record phase survives and reads back.
    const good = await call("puerts_graph_inspect", { asset_path: GOOD });
    assert(good.success === true, "(10) the successful Blueprint still reads back after a restart");
    assert((good.data?.graph?.node_count ?? 0) >= 2,
      "(10) the successful Blueprint's graph is intact after a restart");
  } else {
    console.log("\nrecord phase: three failed Blueprint builds in one editor session");

    // (1)-(4) Starting state.
    const p4Before = p4Opened();
    assert(await registryCount("BP_AtomicityProbe") === 0, "(1) no failed Blueprint in the Asset Registry");
    assert(existsSync(contentFile(PROBE)) === false, "(3) no failed Blueprint file on disk");
    evidence.observations.before_p4_opened = p4Before;

    for (let attempt = 1; attempt <= 3; attempt++) {
      console.log(`\n-- failed build ${attempt} of 3`);
      const bad = await call("puerts_blueprint_build", badSpec());
      const cleanup = bad.data?.cleanup ?? {};
      evidence.iterations.push({ attempt, success: bad.success, errors: bad.errors, cleanup });

      assert(bad.success === false, `(5) build ${attempt}: the invalid request fails`);
      assert(bad.data?.saved !== true, `(5) build ${attempt}: nothing was saved`);
      assert(await registryCount("BP_AtomicityProbe") === 0,
        `(2) build ${attempt}: no failed Blueprint in the Asset Registry`);
      assert(existsSync(contentFile(PROBE)) === false,
        `(4) build ${attempt}: no failed Blueprint file on disk`);

      if (Object.keys(cleanup).length > 0) {
        assert(cleanup.rollback_attempted === true, `build ${attempt}: rollback_attempted`);
        assert(cleanup.rollback_succeeded === true, `build ${attempt}: rollback_succeeded`);
        assert((cleanup.dirty_packages_after ?? []).length === 0,
          `(3) build ${attempt}: no related package is dirty afterwards`);
        assert((cleanup.cleanup_errors ?? []).length === 0, `build ${attempt}: no cleanup errors`);
        assert((cleanup.files_created ?? []).length === 0, `build ${attempt}: no file was written`);
      } else {
        console.log(`  INFO  build ${attempt}: no cleanup evidence in the response (pre-fix build)`);
      }

      const inspect = await call("puerts_graph_inspect", { asset_path: PROBE });
      assert(inspect.success === false,
        `build ${attempt}: the inspector finds no failed artifact (independent of the builder's report)`);
    }

    const p4After = p4Opened();
    evidence.observations.after_p4_opened = p4After;
    if (p4Before !== null && p4After !== null) {
      assert(p4Before === p4After, "(5) no source-control entry appeared (p4 opened unchanged)");
    } else {
      console.log("  INFO  p4 unavailable; source-control quiescence not measured");
    }

    // (9) A successful build in the same session must still compile and save.
    console.log("\n-- a successful build in the same session");
    const good = await call("puerts_blueprint_build", {
      asset_path: GOOD,
      parent_class: "Actor",
      components: [{ class: "StaticMeshComponent", name: "ProbeMesh" }],
      graph: {
        nodes: [{ id: "begin", type: "BeginPlay" }, { id: "print", type: "PrintString" }],
        connections: [{ from: "begin.then", to: "print.exec" }],
      },
    });
    evidence.observations.good_build = {
      success: good.success, compile_status: good.data?.compile_status,
      saved: good.data?.saved, cleanup: good.data?.cleanup,
    };
    assert(good.success === true, "(9) the successful build succeeds");
    assert(good.data?.compile_status === "UpToDate", "(9) the successful build compiles UpToDate");
    assert(good.data?.saved === true, "(9) the successful build saves");
    assert(existsSync(contentFile(GOOD)), "(9) the successful Blueprint exists on disk");
    if (good.data?.cleanup) {
      assert(good.data.cleanup.rollback_attempted === false,
        "(9) no rollback was attempted on the successful build");
      assert((good.data.cleanup.removed_assets ?? []).length === 0,
        "(9) the successful build removed nothing");
    }

    // (10) Graph inspection still reads the successful asset correctly.
    const read = await call("puerts_graph_inspect", { asset_path: GOOD });
    assert(read.success === true, "(10) graph inspection reads the successful asset");
    assert((read.data?.graph?.node_count ?? 0) === 2, "(10) inspection reports both spec nodes");
    assert(read.transaction_id === "", "(10) inspection opened no transaction");
  }

  const evidenceDir = join(root, "docs", "evidence");
  mkdirSync(evidenceDir, { recursive: true });
  const out = join(evidenceDir, `bp-failure-atomicity-${phase}.json`);
  writeFileSync(out, JSON.stringify(evidence, null, 2));
  console.log(`\nevidence: ${out}`);

  if (failures.length > 0) {
    console.log(`\n${failures.length} check(s) did not hold:`);
    for (const f of failures) console.log(`  - ${f}`);
    if (!observeOnly) process.exitCode = 1;
  } else {
    console.log(`\nall checks passed (${phase} phase)`);
  }
} catch (error) {
  console.error(error);
  process.exitCode = 1;
} finally {
  child.stdin.end();
}
