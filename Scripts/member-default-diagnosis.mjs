#!/usr/bin/env node
// Finding 0p: a float variable's default reads back "" after a successful set.
//
// This script does not fix anything. It measures the SAME variable through five
// independent readings so the next session knows which of four things is wrong:
//
//   a. the writer          the value never reaches storage
//   b. the description     FBPVariableDescription::DefaultValue
//   c. compile             a compile clears or rewrites it
//   d. the reader          stored fine, reported wrong
//
// The readings, per phase:
//
//   1  requested        what this script sent
//   2  description      FBPVariableDescription::DefaultValue
//   3  cdo              the generated class default, read with puerts_read_property
//                       against /Game/.../BP_X.Default__BP_X_C
//   4  inspector        graph_inspect's variables[].default_value
//   5  uasset           is the requested value present as ASCII in the saved
//                       package? The description is a serialized FString and a
//                       float CDO value is four raw bytes, so an ASCII hit means
//                       the DESCRIPTION carried the value to disk. This is the
//                       closest available proxy for what a cold load restores.
//
// (2) and (4) are printed as separate columns and they are the SAME FIELD:
// BPMemberReader.cpp:140 serializes V.DefaultValue straight into default_value,
// and nothing else in the catalog exposes the description. That is a finding in
// itself, not an oversight in this script.
//
// Three phases, one fixture, one float variable, one value:
//
//   build              variable created, no default        -> baseline
//   patch-nocompile    set default, compile:false          -> the writer alone
//   compile            identical patch, compile:true       -> the writer plus a
//                      full compile; the ops converge and are skipped, so the
//                      ONLY difference from the previous phase is the compile
//
// A phase-to-phase difference between those last two isolates candidate (c)
// with nothing else moving.
//
// Usage (BRIDGE_REPO must be the checkout whose mcp-server/dist is built and
// whose plugin is installed in the target project; a worktree is usually not):
//
//   MCP_UNREAL_PROJECT_ROOT="D:/Unreal Projects/BridgeInstallTest" \
//   BRIDGE_REPO="D:/Unreal Projects/UE4_Bridge" \
//   node Scripts/member-default-diagnosis.mjs
//
//   node Scripts/member-default-diagnosis.mjs --phase=cold
//     Re-reads the fixture named in docs/evidence/member-default-diagnosis.json
//     without mutating anything. Meaningful ONLY after the editor has been
//     restarted. NOT RUN by lane Q: no editor restart was permitted.

import { execFileSync, spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const scriptRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const bridgeRepo = process.env.BRIDGE_REPO || scriptRoot;
const projectRoot = process.env.MCP_UNREAL_PROJECT_ROOT;
const evidenceFile = join(scriptRoot, "docs", "evidence", "member-default-diagnosis.json");

const VALUE = 0.75;
const VAR = "Delta";

/** The install gate, run as the shipped command so its output is the output a
    reader is asked to compare against. Never skipped: a stale plugin in the
    target project makes every reading below meaningless. */
function installCheck(label) {
  console.log(`\n== install:check (${label}) ==`);
  // The gate's own script, not `npm run install:check`: with shell:true the
  // project path is re-parsed by cmd and "D:/Unreal Projects/..." splits at the
  // space into a path that does not exist, which the gate then reports as a
  // MISSING plugin. A missing gate that looks like a failing gate is the worst
  // of both.
  const out = execFileSync(process.execPath,
    [join(bridgeRepo, "Scripts", "bridge-install.mjs"), "--check", "--project", projectRoot],
    { cwd: bridgeRepo, encoding: "utf8" });
  console.log(out.trim());
  return out;
}

function startBridge() {
  const serverPath = join(bridgeRepo, "mcp-server", "dist", "index.js");
  if (!existsSync(serverPath)) throw new Error(`No built server at ${serverPath}; run npm run build in BRIDGE_REPO`);
  const child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "inherit"], env: process.env });
  let buffer = ""; let nextId = 1; const pending = new Map();
  child.stdout.on("data", (chunk) => {
    buffer += chunk.toString();
    for (let n; (n = buffer.indexOf("\n")) >= 0;) {
      const line = buffer.slice(0, n).trim(); buffer = buffer.slice(n + 1);
      if (!line) continue;
      const m = JSON.parse(line); const f = pending.get(m.id);
      if (f) { pending.delete(m.id); f(m); }
    }
  });
  const send = (method, params) => {
    const id = nextId++;
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 120000);
      pending.set(id, (m) => { clearTimeout(timer); resolve(m); });
    });
  };
  const call = async (name, args = {}) => {
    const message = await send("tools/call", { name, arguments: args });
    const text = message?.result?.content?.[0]?.text;
    if (typeof text !== "string") throw new Error(`${name} returned no JSON content`);
    return JSON.parse(text);
  };
  const ready = async () => {
    await send("initialize", {
      protocolVersion: "2024-11-05", capabilities: {},
      clientInfo: { name: "member-default-diagnosis", version: "1.0.0" },
    });
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");
  };
  return { call, ready, close: () => child.stdin.end() };
}

const contentFile = (assetPath) =>
  join(projectRoot, "Content", assetPath.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");

/** The three live readings plus the disk one, for ONE variable, at one moment.
    Nothing here mutates: graph_inspect and read_property are both read-only and
    the file is opened for reading. */
async function readAll(call, assetPath, varName) {
  const assetName = assetPath.split("/").pop();
  const cdoPath = `${assetPath}.Default__${assetName}_C`;

  const inspect = await call("puerts_graph_inspect", { asset_path: assetPath });
  const variable = (inspect.data?.variables ?? []).find((v) => v.name === varName);

  let cdo;
  try {
    const read = await call("puerts_read_property", { object_path: cdoPath, property: varName });
    cdo = read.success ? read.data?.value : `ERROR: ${read.error ?? "unknown"}`;
  } catch (error) {
    cdo = `ERROR: ${error.message}`;
  }

  const file = contentFile(assetPath);
  const ascii = existsSync(file)
    ? readFileSync(file, "latin1").includes(VALUE.toFixed(6))
    : null;

  return {
    description: variable?.default_value ?? "(variable not reported)",
    inspector: variable?.default_value ?? "(variable not reported)",
    cdo,
    uasset_has_ascii_value: ascii,
    member_hash: inspect.data?.member_structure_hash_sha1 ?? null,
  };
}

function printTable(rows) {
  const columns = [
    ["phase", (r) => r.phase],
    ["1 requested", (r) => r.requested],
    ["2 description", (r) => JSON.stringify(r.description)],
    ["3 cdo", (r) => JSON.stringify(r.cdo)],
    ["4 inspector", (r) => JSON.stringify(r.inspector)],
    [`5 uasset has "${VALUE.toFixed(6)}"`, (r) => String(r.uasset_has_ascii_value)],
  ];
  const cells = [columns.map(([h]) => h), ...rows.map((r) => columns.map(([, f]) => f(r)))];
  const widths = columns.map((_, i) => Math.max(...cells.map((row) => row[i].length)));
  for (const [index, row] of cells.entries()) {
    console.log("  " + row.map((cell, i) => cell.padEnd(widths[i])).join("  "));
    if (index === 0) console.log("  " + widths.map((w) => "-".repeat(w)).join("  "));
  }
}

async function record() {
  const runId = Date.now().toString(36);
  const assetPath = `/Game/MCPGenerated/BP_DefaultProbe_${runId}`;
  const bridge = startBridge();
  const rows = [];
  try {
    await bridge.ready();

    // Baseline: the variable exists and has never been given a default.
    const build = await bridge.call("puerts_blueprint_build", {
      asset_path: assetPath,
      parent_class: "Actor",
      variables: [{ name: VAR, type: "float", category: "Probe" }],
      graph: { nodes: [] },
      compile: true, save: true,
    });
    if (!build.success) throw new Error(`fixture build failed: ${build.error}`);
    rows.push({ phase: "build (no default)", requested: "-", ...await readAll(bridge.call, assetPath, VAR) });

    // The writer alone. No compile after the batch, so whatever the mutator's
    // own compile-then-sync left in the description is what is read.
    const operations = [{ op: "set_variable_default", name: VAR, default: VALUE }];
    const noCompile = await bridge.call("puerts_blueprint_member_patch", {
      asset_path: assetPath, operations, compile: false, save: true, verify: true,
    });
    console.log(`\n  patch compile:false -> success=${noCompile.success} applied=${JSON.stringify(noCompile.data?.applied_operations)} `
      + `verification_mismatches=${JSON.stringify(noCompile.data?.verification_mismatches)} saved=${noCompile.data?.saved}`);
    if (!noCompile.success) throw new Error(`patch (compile:false) failed: ${JSON.stringify(noCompile)}`);
    rows.push({ phase: "patch compile:false", requested: String(VALUE), ...await readAll(bridge.call, assetPath, VAR) });

    // The identical batch. Every operation is already satisfied, so it applies
    // nothing and skips everything; the compile is the only thing that happens.
    const compiled = await bridge.call("puerts_blueprint_member_patch", {
      asset_path: assetPath, operations, compile: true, save: true, verify: true,
    });
    console.log(`\n  patch compile:true  -> success=${compiled.success} applied=${JSON.stringify(compiled.data?.applied_operations)} `
      + `unchanged=${JSON.stringify(compiled.data?.unchanged_operations)} compile_status=${compiled.data?.compile_status} `
      + `converged=${compiled.data?.converged} saved=${compiled.data?.saved}`);
    // NOT a throw. This patch failing IS a reading, and the phases after it are
    // the ones that say what the failure did to the asset. An abort here would
    // discard exactly the evidence the run was launched for.
    if (!compiled.success) console.log(`  errors: ${JSON.stringify(compiled.errors ?? compiled.data?.errors)}`);
    rows.push({ phase: "after compile", requested: String(VALUE), ...await readAll(bridge.call, assetPath, VAR) });

    // A converged batch does not save, so the disk column above is still the
    // previous phase's. Save explicitly and read the file again, because what
    // reaches the package is what a cold load gets back.
    const saved = await bridge.call("puerts_save", { assets: [assetPath] });
    console.log(`\n  save after compile  -> success=${saved.success}`);
    rows.push({ phase: "after compile+save", requested: String(VALUE), ...await readAll(bridge.call, assetPath, VAR) });

    mkdirSync(dirname(evidenceFile), { recursive: true });
    writeFileSync(evidenceFile, JSON.stringify({ asset_path: assetPath, variable: VAR, value: VALUE, rows }, null, 2));
    console.log(`\n  fixture: ${assetPath}\n  evidence: ${evidenceFile}`);
    console.log("  --phase=cold re-reads this fixture; it is only meaningful after an editor restart.");
  } finally {
    // In the finally, so a phase that fails still shows every phase that ran.
    // The last two runs of this investigation each failed at a later phase and
    // threw away the readings that had already been taken.
    if (rows.length > 0) printTable(rows);
    bridge.close();
  }
}

async function cold() {
  if (!existsSync(evidenceFile)) throw new Error(`No ${evidenceFile}; run the record phase first`);
  const { asset_path, variable } = JSON.parse(readFileSync(evidenceFile, "utf8"));
  const bridge = startBridge();
  try {
    await bridge.ready();
    printTable([{ phase: "cold reload", requested: String(VALUE), ...await readAll(bridge.call, asset_path, variable) }]);
    console.log(`\n  fixture: ${asset_path}`);
    console.log("  This is a cold reading ONLY if the editor was restarted since the record phase.");
  } finally {
    bridge.close();
  }
}

// pathToFileURL, not "file://" + path: the hand-built form never matches
// import.meta.url on Windows, and a script that exits 0 having run nothing is
// worse than one that fails.
if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  if (!projectRoot) throw new Error("Set MCP_UNREAL_PROJECT_ROOT to the test project root");
  installCheck("before");
  const run = process.argv.includes("--phase=cold") ? cold : record;
  run()
    .then(() => { installCheck("after"); })
    .catch((error) => { console.error(`\nFAILED: ${error.message}`); process.exitCode = 1; });
}
