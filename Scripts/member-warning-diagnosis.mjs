#!/usr/bin/env node
// Finding 0q: blueprint_member_patch leaves the Blueprint UpToDateWithWarnings
// where a freshly built one is UpToDate. This script does not assert anything.
// It measures, so the fix has something to be a fix OF.
//
// Three questions, in the order that makes each one cheap:
//
//   1. Is the fixture already warning before any patch runs? If it is, the patch
//      is innocent and the acceptance is asserting about the fixture's own graph.
//   2. If not, WHICH operation first moves the status? The acceptance sends all
//      seven in one batch; this sends them one at a time, each in its own patch,
//      and reads the status and the compiler MESSAGES after every one.
//   3. Is a Blueprint built directly into the same final state also warning? If
//      the clean one is UpToDate and the patched one is not, the difference
//      between them is the defect. If both warn, the warning is a property of
//      the state, not of patching.
//
// Compiler messages are readable at all only because blueprint_member_patch now
// returns compile_warnings and compile_errors. CompileAndReport has always
// collected them off the FCompilerResultsLog; the command read two scalars out
// of that report and dropped the rest.
//
//   --phase=record   everything above, on a fixture path minted now
//   --phase=cold     after an editor restart, re-read both assets
//
// Fresh paths per run, asserted by requiring graph_inspect to FAIL first. Two
// earlier investigations in this program produced false reds off a reused
// fixture path (findings 0n, 0r).

import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
requireCurrentInstall();
const phase = process.argv.includes("--phase=cold") ? "cold" : "record";
const evidenceDir = join(repoRoot, "docs", "evidence");
const recordFile = join(evidenceDir, "member-warning-diagnosis.json");

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
function send(method, params) {
  const id = nextId++;
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 120000);
    pending.set(id, (m) => { clearTimeout(timer); resolve(m); });
  });
}
async function call(name, args = {}) {
  const message = await send("tools/call", { name, arguments: args });
  const text = message?.result?.content?.[0]?.text;
  if (typeof text !== "string") throw new Error(`${name} returned no JSON content`);
  return JSON.parse(text);
}

const runId = phase === "cold"
  ? JSON.parse(readFileSync(recordFile, "utf8")).run_id
  : Date.now().toString(36);
const PATCHED = `/Game/MCPGenerated/BP_WarnPatched_${runId}`;
const CLEAN = `/Game/MCPGenerated/BP_WarnClean_${runId}`;

// The acceptance's fixture, verbatim, because the point is to explain THAT red.
const components = [
  { class: "SceneComponent", name: "Root" },
  { class: "StaticMeshComponent", name: "Body", attach_to: "Root" },
  { class: "PointLightComponent", name: "Lamp", attach_to: "Root" },
  { class: "SceneComponent", name: "Muzzle", attach_to: "Body" },
];
const variables = [
  { name: "Alpha", type: "float", default: 1 },
  { name: "Beta", type: "float", default: 2 },
  { name: "Gamma", type: "float", default: 3 },
  { name: "Doomed", type: "float", default: 4 },
  { name: "Label", type: "string", default: "start" },
  { name: "Count", type: "int", default: 1 },
];
const graph = {
  nodes: [
    { id: "begin", type: "BeginPlay", x: 0, y: 0 },
    { id: "say", type: "PrintString", x: 300, y: 0, params: { InString: "member probe" } },
    { id: "getA", type: "VariableGet", x: 0, y: 300, params: { varName: "Alpha" } },
    { id: "getB", type: "VariableGet", x: 0, y: 400, params: { varName: "Beta" } },
  ],
  connections: [{ from: "begin.then", to: "say.exec" }],
};
const FIXTURE = { asset_path: PATCHED, parent_class: "Actor", components, variables, graph };

// The same asset the seven operations are supposed to produce, stated directly.
// Functions and event dispatchers are NOT here: blueprint_build cannot create
// them, so the control covers the variable and component state only. That is a
// stated limit of this control, not an oversight; if the culprit turns out to be
// add_function or add_event_dispatcher the control cannot speak to it.
const CLEAN_FIXTURE = {
  asset_path: CLEAN,
  parent_class: "Actor",
  components: [
    { class: "SceneComponent", name: "Root" },
    { class: "StaticMeshComponent", name: "Body", attach_to: "Root" },
    { class: "PointLightComponent", name: "Beacon", attach_to: "Root" },
  ],
  variables: [
    { name: "Alpha", type: "float", default: 1 },
    { name: "Beta", type: "float", default: 2 },
    { name: "Gamma", type: "float", default: 3 },
    { name: "Label", type: "string", default: "patched" },
    { name: "Count", type: "int", default: 1 },
    { name: "Delta", type: "float", default: 9 },
  ],
  graph,
};

// The acceptance's batch, split. Order preserved.
const OPS = [
  { op: "add_variable", name: "Delta", type: { category: "float" }, default: 9, category: "Probe" },
  { op: "set_variable_default", name: "Label", default: "patched" },
  { op: "remove_variable", name: "Doomed" },
  { op: "add_function", name: "ProbeStep", inputs: [{ name: "Amount", type: { category: "float" } }], outputs: [{ name: "Result", type: { category: "float" } }] },
  { op: "add_event_dispatcher", name: "OnProbed", parameters: [{ name: "Value", type: { category: "float" } }] },
  { op: "rename_component", from: "Lamp", to: "Beacon" },
  { op: "remove_component", name: "Muzzle" },
];

const evidence = { phase, run_id: runId, patched: PATCHED, clean: CLEAN, readings: [] };
const line = (label, status, warnings) => {
  console.log(`  ${String(status).padEnd(22)} ${label}`);
  for (const w of warnings ?? []) console.log(`      warn: ${w}`);
};

/** A converged member_patch is a recompile that reports its own messages. The
    operation offered is one that is already true, so nothing is applied, and the
    command compiles regardless of how many operations applied. */
async function recompileAndRead(assetPath, satisfiedOp) {
  const r = await call("puerts_blueprint_member_patch", { asset_path: assetPath, operations: [satisfiedOp] });
  return {
    ok: r.success,
    applied: r.data?.applied_operation_count,
    status: r.data?.compile_status,
    warnings: r.data?.compile_warnings ?? null,
    errors: r.data?.compile_errors ?? null,
    errorText: r.success ? undefined : JSON.stringify(r.errors ?? []),
  };
}

async function inspect(assetPath) {
  const r = await call("puerts_graph_inspect", { asset_path: assetPath });
  return r;
}

try {
  await send("initialize", {
    protocolVersion: "2024-11-05", capabilities: {},
    clientInfo: { name: "member-warning-diagnosis", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    console.log(`\n== cold: both assets after an editor restart`);
    for (const path of [PATCHED, CLEAN]) {
      const read = await inspect(path);
      console.log(`  ${String(read.data?.compile_status).padEnd(22)} ${path}`);
      evidence.readings.push({ step: "cold inspect", asset: path, status: read.data?.compile_status });
    }
    // A recompile in the cold session, so the messages are this session's.
    const cold = await recompileAndRead(PATCHED, { op: "set_variable_default", name: "Label", default: "patched" });
    line("cold recompile of the patched asset", cold.status, cold.warnings);
    evidence.readings.push({ step: "cold recompile", asset: PATCHED, ...cold });
  } else {
    console.log(`\n== reseed: two paths that cannot have existed`);
    for (const path of [PATCHED, CLEAN]) {
      const pre = await inspect(path);
      console.log(`  ${pre.success === false ? "absent (good)" : "PRESENT - NOT A RESEED"}  ${path}`);
      if (pre.success !== false) throw new Error(`${path} already exists; this run would prove nothing`);
    }

    console.log(`\n== 1. the full fixture, before any patch`);
    const built = await call("puerts_blueprint_build", FIXTURE);
    if (!built.success) throw new Error(`fixture build failed: ${JSON.stringify(built.errors)}`);
    line("blueprint_build reports", built.data?.compile_status, built.data?.warnings);
    const builtInspect = await inspect(PATCHED);
    line("graph_inspect reports", builtInspect.data?.compile_status, null);
    // Independent recompile: build's own compile and a later one can differ.
    const rebuilt = await recompileAndRead(PATCHED, { op: "set_variable_default", name: "Label", default: "start" });
    line("converged member_patch recompile reports", rebuilt.status, rebuilt.warnings);
    evidence.readings.push({
      step: "fixture built, unpatched",
      build_status: built.data?.compile_status,
      build_warnings: built.data?.warnings ?? [],
      inspect_status: builtInspect.data?.compile_status,
      recompile_status: rebuilt.status,
      recompile_warnings: rebuilt.warnings,
      recompile_applied: rebuilt.applied,
    });

    console.log(`\n== 2. one operation at a time`);
    let firstWarningOp = null;
    for (let i = 0; i < OPS.length; i++) {
      const op = OPS[i];
      const label = `${i}: ${op.op} ${op.name ?? `${op.from} -> ${op.to}`}`;
      const r = await call("puerts_blueprint_member_patch", { asset_path: PATCHED, operations: [op] });
      const after = await inspect(PATCHED);
      line(label, r.success ? r.data?.compile_status : `FAILED ${JSON.stringify(r.errors)}`, r.data?.compile_warnings);
      evidence.readings.push({
        step: `op ${i}`,
        label,
        success: r.success,
        applied: r.data?.applied_operation_count,
        status: r.data?.compile_status,
        warnings: r.data?.compile_warnings ?? [],
        errors: r.data?.compile_errors ?? [],
        inspect_status: after.data?.compile_status,
        member_hash: after.data?.member_structure_hash_sha1,
      });
      if (firstWarningOp === null && r.data?.compile_status === "UpToDateWithWarnings") {
        firstWarningOp = label;
        console.log(`      ^^ FIRST warning-producing operation`);
      }
    }
    evidence.first_warning_operation = firstWarningOp;

    console.log(`\n== 3. the same final state, built directly, never patched`);
    const clean = await call("puerts_blueprint_build", CLEAN_FIXTURE);
    if (!clean.success) throw new Error(`clean build failed: ${JSON.stringify(clean.errors)}`);
    line("blueprint_build reports", clean.data?.compile_status, clean.data?.warnings);
    const cleanInspect = await inspect(CLEAN);
    line("graph_inspect reports", cleanInspect.data?.compile_status, null);
    const cleanRecompile = await recompileAndRead(CLEAN, { op: "set_variable_default", name: "Label", default: "patched" });
    line("converged member_patch recompile reports", cleanRecompile.status, cleanRecompile.warnings);
    evidence.readings.push({
      step: "clean control",
      build_status: clean.data?.compile_status,
      build_warnings: clean.data?.warnings ?? [],
      inspect_status: cleanInspect.data?.compile_status,
      recompile_status: cleanRecompile.status,
      recompile_warnings: cleanRecompile.warnings,
    });

    console.log(`\n== 4. what the two assets look like to an independent reader`);
    const patchedFinal = await inspect(PATCHED);
    evidence.patched_inspect = patchedFinal.data;
    evidence.clean_inspect = cleanInspect.data;
    const shape = (d) => ({
      components: (d?.components ?? []).map((c) => `${c.name}:${c.class ?? c.component_class ?? "?"}@${c.attach_to ?? c.parent ?? "-"}`).sort(),
      variables: (d?.variables ?? []).map((v) => `${v.name}:${JSON.stringify(v.type)}=${v.default_value}`).sort(),
      functions: (d?.functions ?? []).map((f) => f.name).sort(),
      dispatchers: (d?.event_dispatchers ?? []).map((d2) => d2.name).sort(),
      graph_nodes: (d?.graph?.nodes ?? []).map((n) => `${n.type ?? n.class}:${n.title ?? ""}`).sort(),
    });
    const a = shape(patchedFinal.data); const b = shape(cleanInspect.data);
    for (const key of Object.keys(a)) {
      const onlyPatched = a[key].filter((x) => !b[key].includes(x));
      const onlyClean = b[key].filter((x) => !a[key].includes(x));
      console.log(`  ${key}: ${onlyPatched.length + onlyClean.length === 0 ? "identical" : ""}`);
      for (const x of onlyPatched) console.log(`      only in PATCHED: ${x}`);
      for (const x of onlyClean) console.log(`      only in CLEAN:   ${x}`);
    }
    evidence.patched_shape = a;
    evidence.clean_shape = b;
  }

  mkdirSync(evidenceDir, { recursive: true });
  writeFileSync(join(evidenceDir, `member-warning-diagnosis${phase === "cold" ? "-cold" : ""}.json`),
    JSON.stringify(evidence, null, 2) + "\n");
  console.log(`\nevidence written for ${PATCHED}`);
  process.exit(0);
} catch (error) {
  console.error(`\nmember-warning-diagnosis: ${error.message}`);
  process.exit(1);
} finally {
  child.kill();
}
