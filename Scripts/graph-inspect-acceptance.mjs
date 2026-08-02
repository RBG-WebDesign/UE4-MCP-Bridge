#!/usr/bin/env node
// Live acceptance for puerts_graph_inspect against a running UE4.27 editor.
//
// Proves, in order: zero-config pipe and token discovery, that inspection is
// read-only and leaves the Blueprint clean, that two reads produce identical
// canonical JSON (compared by hash), that a known fixture reads back field by
// field, that the builder's node-type vocabulary and the inspector's inverse
// map have not drifted, and that a large real Blueprint inspects within its
// timeout with its payload size reported.
//
// Requires MCP_UNREAL_PROJECT_ROOT. Deliberately ignores MCP_PUERTS_PIPE and
// MCP_PUERTS_TOKEN_PATH: discovery from the project root is part of what this
// script exists to prove.
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(root, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
// Refuse to touch the editor unless the plugin in that project is the plugin
// in this checkout. A live run against a stale install proves nothing about the
// code under review and looks exactly like a passing run.
requireCurrentInstall();
delete process.env.MCP_PUERTS_PIPE;
delete process.env.MCP_PUERTS_TOKEN_PATH;
delete process.env.MCP_PUERTS_TOKEN;

const child = spawn(process.execPath, [serverPath], {
  stdio: ["pipe", "pipe", "inherit"],
  env: process.env,
});
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
  const result = JSON.parse(text);
  if (!result.success) throw new Error(`${name}: ${result.errors?.join("; ") || result.message}`);
  return result;
}

function assert(condition, label) {
  if (!condition) throw new Error(`FAIL  ${label}`);
  console.log(`  PASS  ${label}`);
}

/** Key-sorted deep copy, so two responses hash equally when their content is
    equal. Array order is the inspector's canonical order and is preserved. */
function canonicalize(value) {
  if (Array.isArray(value)) return value.map(canonicalize);
  if (value !== null && typeof value === "object") {
    const out = {};
    for (const key of Object.keys(value).sort()) out[key] = canonicalize(value[key]);
    return out;
  }
  return value;
}
const hash = (value) =>
  createHash("sha256").update(JSON.stringify(canonicalize(value))).digest("hex");

const FIXTURE = "/Game/MCPGenerated/BP_InspectFixture";
// One node of every builder vocabulary type whose config needs nothing from
// the project. ponytail: drift detection covers the types this fixture uses;
// a type added to the builder later must also be added here to be guarded.
const FIXTURE_NODES = [
  { id: "begin", type: "BeginPlay" },
  { id: "tick", type: "Tick" },
  { id: "evt", type: "CustomEvent", params: { event_name: "OnFixtureProbe" } },
  { id: "print", type: "PrintString", params: { InString: "fixture probe" } },
  { id: "delay", type: "Delay", params: { Duration: 2.5 } },
  { id: "vsize", type: "CallFunction", params: { class: "KismetMathLibrary", function: "VSize", A: { x: 1, y: 2, z: 3 } } },
  { id: "add", type: "Operator", params: { op: "add_float" } },
  { id: "br", type: "Branch" },
  { id: "seq", type: "Sequence", params: { num_outputs: 3 } },
  { id: "note", type: "Comment", params: { text: "fixture comment", width: 300, height: 200 } },
  { id: "setv", type: "VariableSet", params: { var_name: "FixtureCount" } },
  { id: "getv", type: "VariableGet", params: { var_name: "FixtureCount" } },
  // Full path on purpose: a short target_class silently spawns no node
  // (docs/CAPABILITY_FINDINGS.md, Phase L defect 0). The Object pin must be
  // wired or the Blueprint does not compile.
  { id: "getvT", type: "VariableGet", params: { var_name: "FixtureTarget" } },
  { id: "cast", type: "Cast", params: { target_class: "/Script/Engine.StaticMeshActor" } },
  { id: "knot", type: "Knot" },
  // No num_outputs: the MultiGate factory ignores it (findings defect 0b),
  // so the fixture asks for nothing and asserts the default it gets.
  { id: "gate", type: "MultiGate" },
  { id: "key", type: "InputKey", params: { fkey_name: "LeftShift" } },
  { id: "swi", type: "SwitchInt", params: { start_index: 1, num_cases: 2, has_default: true } },
];

try {
  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "graph-inspect-acceptance", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  // 1. Discovery: no pipe or token configuration was passed to this process.
  await call("puerts_diagnostic", {});
  assert(true, "pipe and token discovered from MCP_UNREAL_PROJECT_ROOT alone");

  // 2. Build the fixture.
  const built = await call("puerts_blueprint_build", {
    asset_path: FIXTURE,
    components: [{ class: "StaticMeshComponent", name: "FixtureMesh" }],
    variables: [
      { name: "FixtureCount", type: "int", default: 3 },
      { name: "FixtureTarget", type: "object:Actor" },
    ],
    graph: {
      nodes: FIXTURE_NODES,
      connections: [
        { from: "begin.then", to: "print.exec" },
        { from: "print.then", to: "br.exec" },
        { from: "br.then", to: "setv.exec" },
        { from: "br.else", to: "cast.exec" },
        { from: "getvT.FixtureTarget", to: "cast.Object" },
        { from: "evt.then", to: "gate.exec" },
      ],
    },
  });
  assert(built.data?.compile_status === "UpToDate", "fixture Blueprint built and compiled");
  // The build's own count, checked before anything is read back: a builder
  // that silently drops a spec node must fail here, not surface as a
  // confusing inspection mismatch. See docs/CAPABILITY_FINDINGS.md.
  assert(built.data?.graph?.node_count === FIXTURE_NODES.length,
    `the build spawned all ${FIXTURE_NODES.length} spec nodes`);

  // 3. Read-only contract on a real read.
  const read = await call("puerts_graph_inspect", { asset_path: FIXTURE });
  assert(read.transaction_id === "", "inspection opened no transaction");
  assert(read.changed_assets.length === 0 && read.changed_actors.length === 0,
    "inspection reported nothing changed");
  assert(read.data.package_dirty_after === read.data.package_dirty_before,
    "inspection did not dirty the package");
  assert(read.data.package_dirty_after === false, "the fixture package is clean after the read");

  // 4. Fixture fields.
  const d = read.data;
  assert(d.parent_class?.endsWith("Actor") === true, "parent class reads back");
  assert((d.components ?? []).some((c) => c.name === "FixtureMesh"), "SCS component reads back");
  assert((d.variables ?? []).some((v) => v.name === "FixtureCount"), "member variable reads back");
  assert((d.graphs ?? []).some((g) => g.name === d.graph?.name), "the described graph is in the graph list");
  const nodes = d.graph?.nodes ?? [];
  assert(nodes.length === FIXTURE_NODES.length, "the read reports every spec node");
  const byType = (type) => nodes.filter((n) => n.type === type);
  const one = (type) => {
    const found = byType(type);
    if (found.length !== 1) throw new Error(`FAIL  expected exactly one ${type} node, saw ${found.length}`);
    return found[0];
  };
  assert(one("PrintString").params?.InString === "fixture probe", "PrintString pin default reads back");
  assert(one("Delay").params?.Duration === 2.5, "Delay duration reads back");
  const vsize = one("CallFunction");
  assert(vsize.params?.function === "VSize" && String(vsize.params?.class).includes("KismetMathLibrary"),
    "CallFunction routing reads back");
  const vec = vsize.params?.A;
  assert(vec && vec.x === 1 && vec.y === 2 && vec.z === 3,
    "vector pin default round-trips through FDefaultValueHelper");
  assert(one("Operator").params?.op === "add_float", "Operator maps back to its op");
  assert(one("CustomEvent").params?.event_name === "OnFixtureProbe", "CustomEvent name reads back");
  const note = one("Comment");
  assert(note.params?.text === "fixture comment" && note.params?.width === 300 && note.params?.height === 200,
    "Comment text and size read back");
  assert(one("VariableSet").params?.var_name === "FixtureCount"
    && new Set(byType("VariableGet").map((n) => n.params?.var_name)).size === 2,
    "variable nodes read back");
  assert(String(one("Cast").params?.target_class).includes("StaticMeshActor"), "Cast target reads back");
  assert(one("Sequence").params?.num_outputs === 3, "Sequence output count reads back");
  assert(one("MultiGate").params?.num_outputs === 2, "MultiGate reports its real output count");
  const key = one("InputKey");
  assert(key.params?.fkey_name === "LeftShift", "InputKey key reads back");
  const swi = one("SwitchInt");
  assert(swi.params?.start_index === 1 && swi.params?.has_default === true, "SwitchInt config reads back");
  assert(d.graph.connection_count >= 6, "fixture connections read back");

  // 5. Node-type drift detection: every type the builder accepted must map
  // back to itself, and no node in a builder-authored graph may be unmapped.
  const specTypes = new Set(FIXTURE_NODES.map((n) => n.type));
  const readTypes = new Set(nodes.map((n) => n.type));
  const missing = [...specTypes].filter((t) => !readTypes.has(t));
  assert(missing.length === 0, `no builder node type is missing from the read (${missing.join(", ") || "none"})`);
  assert((d.graph.unmapped_nodes ?? []).length === 0, "no node in a builder-authored graph is unmapped");

  // 6. Determinism: two full reads hash identically in key-sorted form.
  const first = await call("puerts_graph_inspect", { asset_path: FIXTURE, include_pins: true });
  const second = await call("puerts_graph_inspect", { asset_path: FIXTURE, include_pins: true });
  assert(hash(first.data) === hash(second.data), "two reads produce the same canonical hash");

  // 6b. The native structure hash. Distinct from the client-side hash above:
  // that one covers the whole payload including NodeGuids and object names, so
  // it answers "is this the same read". This one deliberately excludes both, so
  // it answers "is this the same GRAPH" - the question a convergence check and
  // a post-rollback comparison actually ask, and the one findings 0j got wrong
  // by comparing object names.
  const structureHash = first.data?.graph?.structure_hash_sha1;
  assert(typeof structureHash === "string" && /^[0-9a-f]{40}$/.test(structureHash),
    `the graph reports a native structure hash (${String(structureHash)})`);
  assert(second.data?.graph?.structure_hash_sha1 === structureHash,
    "the native structure hash is stable across two reads");
  assert(typeof first.data?.graph?.structure_hash_basis === "string",
    "the structure hash states what it covers, so a caller is not guessing");

  // A structurally different graph must hash differently, or the hash is
  // decorative. BP_InspectFixture and BP_ConvergeProbe are both builder-made
  // and are not the same graph.
  const otherGraph = await call("puerts_graph_inspect",
    { asset_path: "/Game/MCPGenerated/BP_ConvergeProbe", include_pins: true });
  if (otherGraph.success === true) {
    assert(otherGraph.data?.graph?.structure_hash_sha1 !== structureHash,
      "a structurally different graph produces a different structure hash");
  } else {
    console.log("  INFO  BP_ConvergeProbe absent; cross-graph hash separation not measured");
  }

  // 7. Stress and payload measurement on the largest builder-made Blueprints.
  const assets = await call("puerts_find_assets", { path: "/Game/MCPGenerated", type: "Blueprint", recursive: true, limit: 50 });
  const paths = (assets.data?.assets ?? []).map((a) => a.path ?? a.object_path ?? a.asset_path).filter(Boolean);
  assert(paths.length > 0, `found ${paths.length} generated Blueprints to stress`);
  let largest = { path: "", bytes: 0, ms: 0, nodes: 0 };
  for (const path of paths) {
    const started = Date.now();
    const result = await call("puerts_graph_inspect", { asset_path: path, include_pins: true });
    const ms = Date.now() - started;
    const bytes = JSON.stringify(result.data).length;
    const unmapped = result.data.graph?.unmapped_nodes ?? [];
    if (unmapped.length > 0) {
      throw new Error(`FAIL  ${path} has unmapped builder nodes: ${JSON.stringify(unmapped)}`);
    }
    if (bytes > largest.bytes) {
      largest = { path, bytes, ms, nodes: result.data.graph?.node_count ?? 0 };
    }
    console.log(`  INFO  ${path}: ${result.data.graph?.node_count ?? 0} nodes, ${bytes} bytes, ${ms} ms`);
  }
  assert(largest.bytes < 1024 * 1024,
    `largest payload (${largest.path}: ${largest.bytes} bytes, ${largest.nodes} nodes, ${largest.ms} ms) is under the 1 MiB pipe cap`);

  console.log("\ngraph_inspect acceptance passed.");
} finally {
  child.kill();
}
