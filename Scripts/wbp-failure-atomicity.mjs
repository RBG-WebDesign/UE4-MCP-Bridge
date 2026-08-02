#!/usr/bin/env node
// Failure atomicity for puerts_widget_build. Third of three; see
// Scripts/bt-failure-atomicity.mjs and Scripts/bp-failure-atomicity.mjs.
//
// Widget differs from the other two in a way that raises the stakes:
// BuildWidgetFromJSON creates, compiles AND SAVES inside the library, so on the
// create path the .uasset is on disk before the command can judge the result. A
// failure after that point leaves a saved file, not merely a dirty package.
//
// Honest limitation, stated because it changes what this proves: every widget
// failure reachable from the current spec vocabulary is rejected BEFORE
// mutation (findings 50 lists eight), so this script cannot demonstrate a
// pre-fix leak the way the BT and Blueprint scripts do. What it proves is that
// the rejections still leave nothing, that the create path now carries a
// rollback boundary for the failures only building can find, and that
// successful output is unchanged.
//
//   --phase=record  rejected specs and a successful build in one session
//   --phase=cold    after a restart, rejects absent and the good widget intact
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
    pending.set(id, (m) => { clearTimeout(timer); resolve(m); });
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
  console.log(`  FAIL  ${label}`); failures.push(label); return false;
}

const PROBE = "/Game/MCPGenerated/WBP_AtomicityProbe";
const GOOD = "/Game/MCPGenerated/WBP_AtomicityGood";
const contentFile = (p) =>
  join(projectRoot, "Content", p.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");
function p4Opened() {
  try {
    return execSync("p4 opened", { cwd: join(projectRoot, "Content"), encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
  } catch { return null; }
}
async function registryCount(name) {
  const found = await call("puerts_find_assets", { path: "/Game/MCPGenerated", name, recursive: true, limit: 50 });
  return (found.data?.assets ?? []).length;
}

// Each of these must fail and leave nothing. The first three are rejected
// before mutation; the fourth asks for a leaf with children, which the
// validator also catches. All are run against the same unused path so any
// residue is attributable.
const REJECTED = [
  { label: "unsupported property", tree: { root: { type: "CanvasPanel", name: "R", children: [{ type: "TextBlock", name: "T", properties: { percent: 0.5 } }] } } },
  { label: "duplicate widget name", tree: { root: { type: "CanvasPanel", name: "R", children: [{ type: "TextBlock", name: "Same" }, { type: "TextBlock", name: "Same" }] } } },
  { label: "root is not a panel", tree: { root: { type: "TextBlock", name: "R" } } },
  { label: "leaf with children", tree: { root: { type: "CanvasPanel", name: "R", children: [{ type: "TextBlock", name: "T", children: [{ type: "TextBlock", name: "C" }] }] } } },
];

const evidence = { phase, rejected: [], observations: {} };

try {
  await send("initialize", { protocolVersion: "2024-11-05", capabilities: {}, clientInfo: { name: "wbp-failure-atomicity", version: "1.0.0" } });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    console.log("\ncold phase: rejects absent, the good widget intact");
    assert(await registryCount("WBP_AtomicityProbe") === 0, "restart: no failed widget in the Asset Registry");
    assert(existsSync(contentFile(PROBE)) === false, "restart: no failed widget file on disk");
    const good = await call("puerts_find_assets", { path: "/Game/MCPGenerated", name: "WBP_AtomicityGood", recursive: true, limit: 10 });
    assert((good.data?.assets ?? []).length === 1, "restart: the successful widget survives");
    assert(existsSync(contentFile(GOOD)), "restart: the successful widget file is on disk");
  } else {
    const p4Before = p4Opened();
    assert(await registryCount("WBP_AtomicityProbe") === 0, "no failed widget in the Asset Registry to begin with");
    assert(existsSync(contentFile(PROBE)) === false, "no failed widget file on disk to begin with");

    for (const spec of REJECTED) {
      console.log(`\n-- rejected spec: ${spec.label}`);
      const bad = await call("puerts_widget_build", { asset_path: PROBE, tree: spec.tree });
      evidence.rejected.push({ label: spec.label, success: bad.success, errors: bad.errors, cleanup: bad.data?.cleanup });
      assert(bad.success === false, `${spec.label}: the request fails`);
      assert(await registryCount("WBP_AtomicityProbe") === 0, `${spec.label}: no widget in the Asset Registry`);
      assert(existsSync(contentFile(PROBE)) === false, `${spec.label}: no widget file on disk`);
    }

    const p4After = p4Opened();
    if (p4Before !== null && p4After !== null) {
      assert(p4Before === p4After, "no source-control entry appeared (p4 opened unchanged)");
    } else {
      console.log("  INFO  p4 unavailable; source-control quiescence not measured");
    }

    console.log("\n-- a successful widget build in the same session");
    const good = await call("puerts_widget_build", {
      asset_path: GOOD,
      tree: {
        root: {
          type: "CanvasPanel", name: "Root",
          children: [
            { type: "TextBlock", name: "Title", properties: { Text: "WBP_ATOMICITY" }, slot: { position: { x: 40, y: 24 }, size: { x: 320, y: 40 } } },
            { type: "ProgressBar", name: "Bar", properties: { Percent: 0.5 } },
          ],
        },
      },
    });
    evidence.observations.good_build = {
      success: good.success, compile_status: good.data?.compile_status,
      saved: good.data?.saved, widget_count: good.data?.widget_count, cleanup: good.data?.cleanup,
    };
    assert(good.success === true, "the successful widget build succeeds");
    assert(good.data?.compile_status === "UpToDate", "the successful widget compiles UpToDate");
    assert(good.data?.widget_count === 3, "the successful widget reports its three widgets");
    assert(existsSync(contentFile(GOOD)), "the successful widget exists on disk");
    if (good.data?.cleanup) {
      assert(good.data.cleanup.rollback_attempted === false, "no rollback on the successful build");
      assert((good.data.cleanup.removed_assets ?? []).length === 0, "the successful build removed nothing");
    }

    // Convergence: the same spec twice must not create a second asset.
    const again = await call("puerts_widget_build", { asset_path: GOOD, tree: { root: { type: "CanvasPanel", name: "Root", children: [{ type: "TextBlock", name: "Title" }, { type: "ProgressBar", name: "Bar" }] } } });
    assert(again.success === true, "the rerun succeeds");
    assert(again.data?.created === false, "the rerun updates rather than creating a second asset");
    assert(await registryCount("WBP_AtomicityGood") === 1, "still exactly one successful widget asset");
  }

  const evidenceDir = join(root, "docs", "evidence");
  mkdirSync(evidenceDir, { recursive: true });
  const out = join(evidenceDir, `wbp-failure-atomicity-${phase}.json`);
  writeFileSync(out, JSON.stringify(evidence, null, 2));
  console.log(`\nevidence: ${out}`);
  if (failures.length > 0) {
    console.log(`\n${failures.length} check(s) did not hold:`);
    for (const f of failures) console.log(`  - ${f}`);
    process.exitCode = 1;
  } else {
    console.log(`\nall checks passed (${phase} phase)`);
  }
} catch (error) {
  console.error(error);
  process.exitCode = 1;
} finally {
  child.stdin.end();
}
