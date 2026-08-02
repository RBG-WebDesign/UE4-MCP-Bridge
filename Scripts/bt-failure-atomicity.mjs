#!/usr/bin/env node
// Failure atomicity for puerts_behavior_tree_build against a running UE4.27 editor.
//
// A failed build must leave nothing: no asset in the Asset Registry, no file on
// disk, no dirty package, no source-control entry, and nothing for the
// save-on-exit flow to find. See docs/CAPABILITY_FINDINGS.md defect 0c.
//
// The fixture is the existing invalid-node request: a root whose type is not a
// registered node. It passes path validation, so it reaches the point where the
// command has already created both assets, and fails inside the builder.
//
// Two phases, because the restart between them is orchestrated outside this
// script and the close itself is measured by Scripts/editor-shutdown-acceptance.ps1:
//   --phase=record  run the failed request three times in one session and
//                   record observations 1-7; writes the evidence JSON.
//   --phase=cold    after a restart, confirm the probe is still absent (10).
//
// Requires MCP_UNREAL_PROJECT_ROOT; pipe and token are discovered from it.
import { execSync, spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(root, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
// Refuse to touch the editor unless the plugin in that project is the plugin
// in this checkout. A live run against a stale install proves nothing about the
// code under review and looks exactly like a passing run.
const projectRoot = requireCurrentInstall();
delete process.env.MCP_PUERTS_PIPE;
delete process.env.MCP_PUERTS_TOKEN_PATH;
delete process.env.MCP_PUERTS_TOKEN;
const phase = process.argv.includes("--phase=cold") ? "cold" : "record";
// Records observations without failing on them, for the pre-fix run.
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
    const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 40000);
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
  if (observeOnly) { console.log(`  OBSERVED-FAIL  ${label}`); failures.push(label); return false; }
  failures.push(label);
  console.log(`  FAIL  ${label}`);
  return false;
}

const PROBE = "/Game/MCPGenerated/BT_AtomicityProbe";
const PROBE_BB = `${PROBE}_BB`;
const contentFile = (packagePath) =>
  join(projectRoot, "Content", packagePath.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");

function p4Opened() {
  try {
    return execSync("p4 opened", { cwd: join(projectRoot, "Content"), encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
  } catch {
    return null; // p4 unavailable; degrades to INFO
  }
}

/** Is the probe visible to the Asset Registry? */
async function registryCount() {
  const found = await call("puerts_find_assets", {
    path: "/Game/MCPGenerated", name: "BT_AtomicityProbe", recursive: true, limit: 50,
  });
  return (found.data?.assets ?? []).length;
}

const filesOnDisk = () => [PROBE, PROBE_BB].filter((p) => existsSync(contentFile(p)));

/** The invalid-node fixture: a root whose type is not a registered node. */
const badSpec = () => ({
  asset_path: PROBE,
  root: { id: "r", type: "NotARealNode" },
});

async function recentLogs(maxLines = 400) {
  const logs = await call("puerts_get_logs", { maximum_lines: maxLines });
  return (logs.data?.lines ?? logs.log_output ?? []).map(String);
}

const evidence = { phase, iterations: [], observations: {} };

try {
  await send("initialize", {
    protocolVersion: "2024-11-05", capabilities: {},
    clientInfo: { name: "bt-failure-atomicity", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    // (10) Restart must not show the failed assets or files.
    console.log("\ncold phase: the probe must still be absent after a restart");
    const count = await registryCount();
    const files = filesOnDisk();
    evidence.observations.cold_registry_count = count;
    evidence.observations.cold_files_on_disk = files;
    assert(count === 0, "restart: the failed probe is absent from the Asset Registry");
    assert(files.length === 0, "restart: no probe file exists on disk");
    const inspect = await call("puerts_behavior_tree_inspect", { asset_path: PROBE });
    evidence.observations.cold_inspect_success = inspect.success;
    assert(inspect.success === false, "restart: the inspector finds no usable failed artifact");
  } else {
    console.log("\nrecord phase: three failed builds in one editor session");

    // (1)-(4) Starting state, before any request.
    const beforeCount = await registryCount();
    const beforeFiles = filesOnDisk();
    const p4Before = p4Opened();
    evidence.observations.before_registry_count = beforeCount;
    evidence.observations.before_files_on_disk = beforeFiles;
    evidence.observations.before_p4_opened = p4Before;
    assert(beforeCount === 0, "(1) no probe asset exists in the Asset Registry");
    assert(beforeFiles.length === 0, "(2) no probe file exists on disk");

    for (let attempt = 1; attempt <= 3; attempt++) {
      console.log(`\n-- failed request ${attempt} of 3`);
      const logsBefore = await recentLogs();
      const savesBefore = logsBefore.filter((l) => /LogSavePackage/.test(l) && /AtomicityProbe/.test(l)).length;

      const bad = await call("puerts_behavior_tree_build", badSpec());
      const cleanup = bad.data?.cleanup ?? {};
      evidence.iterations.push({ attempt, success: bad.success, errors: bad.errors, cleanup });

      // (5) The invalid request fails.
      assert(bad.success === false, `(5) request ${attempt}: the invalid request fails`);
      // (6) No save occurs during the request.
      const logsAfter = await recentLogs();
      const savesAfter = logsAfter.filter((l) => /LogSavePackage/.test(l) && /AtomicityProbe/.test(l)).length;
      assert(bad.data?.saved !== true && savesAfter === savesBefore,
        `(6) request ${attempt}: no save occurred during the request`);

      // (7) Nothing survives the failure. Pre-fix these are the leak.
      const afterCount = await registryCount();
      const afterFiles = filesOnDisk();
      assert(afterCount === 0, `(2/7) request ${attempt}: no asset is visible in the Asset Registry`);
      assert(afterFiles.length === 0, `(4/7) request ${attempt}: no file exists on disk`);

      // The command's own structured cleanup evidence.
      if (Object.keys(cleanup).length > 0) {
        assert(cleanup.rollback_attempted === true, `request ${attempt}: rollback_attempted`);
        assert(cleanup.rollback_succeeded === true, `request ${attempt}: rollback_succeeded`);
        assert((cleanup.dirty_packages_after ?? []).length === 0,
          `(3) request ${attempt}: no related package is dirty afterwards`);
        assert((cleanup.cleanup_errors ?? []).length === 0, `request ${attempt}: no cleanup errors`);
        assert((cleanup.files_created ?? []).length === 0, `request ${attempt}: no file was written`);
      } else {
        console.log(`  INFO  request ${attempt}: the response carries no cleanup evidence (pre-fix build)`);
      }

      // The inspector is an independent reader: post-fix it must find nothing.
      const inspect = await call("puerts_behavior_tree_inspect", { asset_path: PROBE });
      assert(inspect.success === false,
        `request ${attempt}: the inspector finds no failed artifact (independent of the builder's report)`);
    }

    // (5) Source control must be untouched by the whole sequence.
    const p4After = p4Opened();
    evidence.observations.after_p4_opened = p4After;
    if (p4Before !== null && p4After !== null) {
      assert(p4Before === p4After, "no source-control entry was created (p4 opened unchanged)");
    } else {
      console.log("  INFO  p4 unavailable; source-control quiescence not measured");
    }
  }

  const evidenceDir = join(root, "docs", "evidence");
  mkdirSync(evidenceDir, { recursive: true });
  const out = join(evidenceDir, `bt-failure-atomicity-${phase}.json`);
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
