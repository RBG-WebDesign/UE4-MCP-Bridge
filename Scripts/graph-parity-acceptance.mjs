#!/usr/bin/env node
// Live acceptance: blueprint_build and graph_inspect describe the SAME graph.
//
// Every builder in this bridge verifies itself by reading back through its own
// inspector. That is circular: a builder and an inspector that share a wrong
// assumption agree with each other, pass read-back, and pass CI. Nothing in the
// suite catches it, because nothing compares the two vocabularies independently.
//
// This closes that. The round trip is:
//
//     build(spec) -> inspect -> NORMALIZE -> build(normalized) -> inspect
//                                                     |
//                          assert structure_hash_sha1 equal on both ends
//
// The two builds go to DIFFERENT asset paths on purpose. structure_hash_sha1
// covers node type, class, position, comment, enabled state, visible pin
// defaults and links, and excludes object names and NodeGuids, so two distinct
// assets holding the same graph hash identically. Rebuilding over the same path
// would let a no-op converge and pass while proving nothing.
//
// THE NORMALIZE STEP IS THE TEST. If an inspection could be fed straight back
// into the builder, normalize would be the identity function. Every adaptation
// it needs is a place where the two vocabularies disagree, so each one is
// declared in ADAPTATIONS below and counted. The count is asserted. A new
// disagreement fails this script rather than being absorbed silently, which is
// the whole point: the exception list is allowed to be non-empty, and is not
// allowed to grow without someone deciding it should.
//
// Requires MCP_UNREAL_PROJECT_ROOT and a live UE4.27 editor with the current
// plugin. Authoring only: no PIE, no play, per AGENTS.md.
//
//   node Scripts/graph-parity-acceptance.mjs
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(root, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
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
  child.stdin.write(`${JSON.stringify({ jsonrpc: "2.0", id, method, params })}\n`);
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 60000);
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

let passed = 0;
function assert(condition, label) {
  if (!condition) throw new Error(`FAIL  ${label}`);
  console.log(`  PASS  ${label}`);
  passed += 1;
}

const SOURCE = "/Game/MCPGenerated/BP_ParitySource";
const REBUILT = "/Game/MCPGenerated/BP_ParityRebuilt";

/**
 * Every place the inspector's output cannot be fed to the builder unchanged.
 *
 * `applied` is incremented by normalize() when the adaptation actually fires,
 * so an entry that stops being needed shows up as dead and can be deleted, and
 * an adaptation that fires without an entry is impossible: normalize() only
 * does what is listed here.
 *
 * Read this list as the honest answer to "do the builder and the inspector
 * speak the same language". Empty would mean yes. Today it is three, and each
 * one is a deliberate asymmetry rather than a defect.
 */
const ADAPTATIONS = [
  {
    id: "strip-implied-event-name",
    why: "BeginPlay, Tick and ActorBeginOverlap report params.event_name "
      + "(ReceiveBeginPlay and friends). The builder derives that from the node "
      + "TYPE and rejects the key, because two sources for one fact can "
      + "disagree. Reported for readability, not for round tripping.",
    applied: 0,
  },
  {
    id: "drop-inspector-only-node-fields",
    why: "The inspector adds asset_path, graph_name, node_guid, node_class, "
      + "title and enabled to each node. They describe the node as FOUND, not "
      + "as SPECIFIED, and the builder's node schema is strict.",
    applied: 0,
  },
  {
    id: "rename-object-name-ids",
    why: "Node ids come back as UObject names (K2Node_Event_2). They are valid "
      + "spec ids, but reusing them would hide a real difference: the id a spec "
      + "wrote is not persisted, so a round trip cannot preserve it. Renaming "
      + "to n0..nN makes that explicit instead of accidental.",
    applied: 0,
  },
];
const adaptation = (id) => {
  const entry = ADAPTATIONS.find((a) => a.id === id);
  if (!entry) throw new Error(`undeclared adaptation: ${id}`);
  entry.applied += 1;
  return entry;
};

/** Node-level keys the inspector adds that the builder's strict schema rejects. */
const INSPECTOR_ONLY_NODE_KEYS = [
  "asset_path", "graph_name", "node_guid", "node_class", "title", "enabled",
];
/** Node types whose params.event_name is implied by the type itself. */
const IMPLIED_EVENT_NAME = new Set(["BeginPlay", "Tick", "ActorBeginOverlap", "ActorEndOverlap"]);

/**
 * Turn an inspection into a build spec. Anything this needs to do beyond
 * copying is an adaptation and must be declared above.
 */
function normalize(graph) {
  const idMap = new Map();
  graph.nodes.forEach((node, index) => {
    adaptation("rename-object-name-ids");
    idMap.set(node.id, `n${index}`);
  });

  const nodes = graph.nodes.map((node) => {
    const out = { id: idMap.get(node.id), type: node.type };
    if (typeof node.x === "number") out.x = node.x;
    if (typeof node.y === "number") out.y = node.y;

    for (const key of INSPECTOR_ONLY_NODE_KEYS) {
      if (key in node) { adaptation("drop-inspector-only-node-fields"); break; }
    }

    const params = { ...(node.params ?? {}) };
    if (IMPLIED_EVENT_NAME.has(node.type) && "event_name" in params) {
      adaptation("strip-implied-event-name");
      delete params.event_name;
    }
    // A Comment node's text lives in params.text on both sides; the node-level
    // `comment` field is the bubble on an ordinary node and is not respecified.
    if (Object.keys(params).length) out.params = params;
    return out;
  });

  const connections = (graph.connections ?? []).map((link) => {
    const [fromId, ...fromPin] = link.from.split(".");
    const [toId, ...toPin] = link.to.split(".");
    return {
      from: `${idMap.get(fromId) ?? fromId}.${fromPin.join(".")}`,
      to: `${idMap.get(toId) ?? toId}.${toPin.join(".")}`,
    };
  });

  return { nodes, connections };
}

/** The fixture. One node of every type whose configuration needs nothing from
    the project, wired so the graph has real exec and data edges to compare. */
const SPEC = {
  asset_path: SOURCE,
  parent_class: "Actor",
  variables: [
    { name: "Lit", type: "bool", default: false },
    { name: "Count", type: "int", default: 0 },
  ],
  graph: {
    nodes: [
      { id: "note", type: "Comment", x: -200, y: -200, params: { text: "parity fixture", width: 800, height: 200 } },
      { id: "begin", type: "BeginPlay", x: 0, y: 0 },
      { id: "say", type: "PrintString", x: 320, y: 0, params: { InString: "parity" } },
      { id: "wait", type: "Delay", x: 640, y: 0, params: { Duration: 0.25 } },
      { id: "setLit", type: "VariableSet", x: 960, y: 0, params: { var_name: "Lit", Lit: true } },
      { id: "overlap", type: "ActorBeginOverlap", x: 0, y: 400 },
      { id: "getLit", type: "VariableGet", x: 0, y: 600, params: { var_name: "Lit" } },
      { id: "gate", type: "Branch", x: 320, y: 400 },
      { id: "getCount", type: "VariableGet", x: 320, y: 700, params: { var_name: "Count" } },
      { id: "bump", type: "Operator", x: 640, y: 680, params: { op: "add_int", B: 1 } },
      { id: "store", type: "VariableSet", x: 960, y: 400, params: { var_name: "Count" } },
    ],
    connections: [
      { from: "begin.exec", to: "say.exec" },
      { from: "say.exec", to: "wait.exec" },
      { from: "wait.exec", to: "setLit.exec" },
      { from: "overlap.exec", to: "gate.exec" },
      { from: "getLit.Lit", to: "gate.Condition" },
      { from: "gate.then", to: "store.exec" },
      { from: "getCount.Count", to: "bump.A" },
      { from: "bump.ReturnValue", to: "store.Count" },
    ],
  },
};

try {
  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "graph-parity-acceptance", version: "1" },
  });

  // ------------------------------------------------------------------ build
  const built = await call("puerts_blueprint_build", SPEC);
  assert(built.data?.graph?.connection_count === SPEC.graph.connections.length,
    `source built with all ${SPEC.graph.connections.length} connections resolved`);

  // ---------------------------------------------------------------- inspect
  // include_pins is required: without it the response carries no pin defaults,
  // and a rebuild from it would silently drop every literal the spec set. That
  // is a flag, not an adaptation, so it is not on the list.
  const first = await call("puerts_graph_inspect", { asset_path: SOURCE, include_pins: true });
  const sourceGraph = first.data.graph;
  assert(sourceGraph.unmapped_nodes.length === 0,
    "every node in the fixture maps to a builder node type");
  assert(sourceGraph.lossy_pin_defaults.length === 0,
    "no pin default was reported as raw text the builder cannot respecify");

  // -------------------------------------------------------------- normalize
  const normalized = normalize(sourceGraph);
  assert(normalized.nodes.length === SPEC.graph.nodes.length,
    `normalized spec keeps all ${SPEC.graph.nodes.length} nodes`);
  assert(normalized.connections.length === sourceGraph.connections.length,
    `normalized spec keeps all ${sourceGraph.connections.length} connections`);

  // ---------------------------------------------------------------- rebuild
  const rebuilt = await call("puerts_blueprint_build", {
    asset_path: REBUILT,
    parent_class: SPEC.parent_class,
    variables: SPEC.variables,
    graph: normalized,
  });
  assert(rebuilt.data.graph.connection_count === sourceGraph.connections.length,
    "every connection from the inspection resolved on the rebuild");
  assert((rebuilt.data.errors ?? []).length === 0,
    "the normalized spec was accepted without error");

  // ------------------------------------------------------------------ prove
  const second = await call("puerts_graph_inspect", { asset_path: REBUILT, include_pins: true });
  const rebuiltGraph = second.data.graph;

  assert(rebuiltGraph.node_count === sourceGraph.node_count,
    `node count survives the round trip (${sourceGraph.node_count})`);
  assert(rebuiltGraph.connection_count === sourceGraph.connection_count,
    `connection count survives the round trip (${sourceGraph.connection_count})`);

  // THE assertion. Two different assets, same hash, so the inspector described
  // the graph completely enough for the builder to reproduce it.
  assert(rebuiltGraph.structure_hash_sha1 === sourceGraph.structure_hash_sha1,
    `structure_hash_sha1 matches across the round trip (${sourceGraph.structure_hash_sha1})`);

  // ------------------------------------------------- the exception list itself
  const fired = ADAPTATIONS.filter((a) => a.applied > 0);
  const dead = ADAPTATIONS.filter((a) => a.applied === 0);
  console.log("\n  normalization adaptations, i.e. where the vocabularies differ:");
  for (const entry of ADAPTATIONS) {
    console.log(`    ${entry.applied > 0 ? "USED" : "dead"}  ${entry.id}  (x${entry.applied})`);
  }
  console.log("");

  assert(dead.length === 0,
    "no declared adaptation is dead; the list describes what the code needs");
  assert(fired.length === 3,
    `the vocabularies differ in exactly 3 declared ways (found ${fired.length}); `
    + "a new one means builder and inspector are drifting and needs a decision, not a bigger list");

  console.log(`\n${passed} check(s) passed, 0 failed`);
  console.log(`source ${SOURCE}\nrebuilt ${REBUILT}\nhash ${sourceGraph.structure_hash_sha1}`);
} finally {
  child.kill();
}
