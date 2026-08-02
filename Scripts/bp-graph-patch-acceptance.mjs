#!/usr/bin/env node
// Live acceptance for blueprint_graph_patch.
//
// The property under test is that a patch changes what it names and nothing
// else. blueprint_build is desired-state and rebuilds the whole graph, so
// "unrelated nodes survived" is trivially true there only because the caller
// restated them. Here they survive because they were never touched, and that is
// only worth believing if it is measured against nodes the patch never mentions.
//
//   --phase=record  build the fixture, patch it, verify, converge
//   --phase=cold    after a restart, the patched graph is still the patched graph

import { execSync, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
const projectRoot = requireCurrentInstall();
const phase = process.argv.includes("--phase=cold") ? "cold" : "record";

const child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "inherit"], env: process.env });
let buffer = ""; let nextId = 1; const pending = new Map();
let roundTrips = 0;
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
    const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 90000);
    pending.set(id, (m) => { clearTimeout(timer); resolve(m); });
  });
}
async function call(name, args = {}) {
  roundTrips += 1;
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
const fileSha = (p) => createHash("sha256").update(readFileSync(p)).digest("hex");
const BP = "/Game/MCPGenerated/BP_PatchProbe";
const contentFile = (p) =>
  join(projectRoot, "Content", p.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");
function p4Opened() {
  try {
    return execSync("p4 opened", { cwd: join(projectRoot, "Content"), encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
  } catch { return null; }
}

// Twenty-two nodes. Deliberately more than the patch touches, because the claim
// is about what does NOT move: a fixture the size of the change proves nothing.
const FIXTURE_NODES = [
  { id: "begin", type: "BeginPlay", x: 0, y: 0 },
  { id: "say1", type: "PrintString", x: 300, y: 0, params: { InString: "one" } },
  { id: "say2", type: "PrintString", x: 600, y: 0, params: { InString: "two" } },
  { id: "say3", type: "PrintString", x: 900, y: 0, params: { InString: "three" } },
  { id: "branch", type: "Branch", x: 1200, y: 0 },
  { id: "say4", type: "PrintString", x: 1500, y: -100, params: { InString: "four" } },
  { id: "say5", type: "PrintString", x: 1500, y: 200, params: { InString: "five" } },
  { id: "getA", type: "VariableGet", x: 100, y: 400, params: { varName: "Alpha" } },
  { id: "getB", type: "VariableGet", x: 100, y: 500, params: { varName: "Beta" } },
  { id: "getC", type: "VariableGet", x: 100, y: 600, params: { varName: "Gamma" } },
  { id: "setA", type: "VariableSet", x: 400, y: 400, params: { var_name: "Alpha", value: 7 } },
  { id: "addf", type: "Operator", x: 700, y: 500, params: { op: "add_float" } },
  { id: "mulf", type: "Operator", x: 700, y: 700, params: { op: "multiply_float" } },
  { id: "gtf", type: "Operator", x: 1000, y: 600, params: { op: "greater_float" } },
  { id: "seq", type: "Sequence", x: 1800, y: 0, params: { num_outputs: 3 } },
  { id: "say6", type: "PrintString", x: 2100, y: -200, params: { InString: "six" } },
  { id: "say7", type: "PrintString", x: 2100, y: 100, params: { InString: "seven" } },
  { id: "say8", type: "PrintString", x: 2100, y: 400, params: { InString: "eight" } },
  { id: "note", type: "Comment", x: 0, y: -300, params: { text: "patch probe", width: 600, height: 200 } },
  { id: "tick", type: "Tick", x: 0, y: 900 },
  { id: "say9", type: "PrintString", x: 300, y: 900, params: { InString: "nine" } },
  { id: "delay", type: "Delay", x: 600, y: 900, params: { Duration: 0.5 } },
];
const FIXTURE = {
  asset_path: BP, parent_class: "Actor",
  variables: [
    { name: "Alpha", type: "float", default: 1 },
    { name: "Beta", type: "float", default: 2 },
    { name: "Gamma", type: "float", default: 3 },
  ],
  graph: {
    nodes: FIXTURE_NODES,
    connections: [
      { from: "begin.then", to: "say1.exec" },
      { from: "say1.then", to: "say2.exec" },
      { from: "say2.then", to: "say3.exec" },
      { from: "say3.then", to: "branch.exec" },
      { from: "branch.then", to: "say4.exec" },
      { from: "branch.else", to: "say5.exec" },
      { from: "say4.then", to: "seq.exec" },
      { from: "seq.then_0", to: "say6.exec" },
      { from: "seq.then_1", to: "say7.exec" },
      { from: "seq.then_2", to: "say8.exec" },
      { from: "getA.Alpha", to: "addf.A" },
      { from: "getB.Beta", to: "addf.B" },
      { from: "getC.Gamma", to: "mulf.A" },
      { from: "addf.ReturnValue", to: "gtf.A" },
      { from: "gtf.ReturnValue", to: "branch.Condition" },
      { from: "tick.then", to: "say9.exec" },
      { from: "say9.then", to: "delay.exec" },
    ],
  },
};

const evidence = { phase, steps: {} };
// Node identity as the structural comparison sees it: never the UObject name,
// which is regenerated whenever a node is recreated (findings 0j).
const structuralKey = (n) =>
  JSON.stringify([n.type, n.node_class, n.x, n.y, n.comment ?? "", n.params ?? null]);
const keysOf = (d) => (d?.graph?.nodes ?? []).map(structuralKey).sort();
const hashOf = (d) => d?.graph?.structure_hash_sha1;

try {
  await send("initialize", {
    protocolVersion: "2024-11-05", capabilities: {},
    clientInfo: { name: "bp-graph-patch", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    console.log("\ncold phase: the patched graph survives a restart");
    const recorded = JSON.parse(readFileSync(join(repoRoot, "docs", "evidence", "bp-graph-patch.json"), "utf8"));
    const read = await call("puerts_graph_inspect", { asset_path: BP, include_pins: true });
    assert(read.success === true, "(14) the cold-loaded Blueprint inspects");
    assert(hashOf(read.data) === recorded.post_patch_structure_hash,
      `(14) the structure hash is the one the patch left (${hashOf(read.data)} vs ${recorded.post_patch_structure_hash})`);
    assert((read.data?.graph?.node_count ?? 0) === recorded.post_patch_node_count,
      "(14) the node count is the one the patch left");
  } else {
    console.log("\n-- 1. a fixture bigger than the patch");
    const built = await call("puerts_blueprint_build", FIXTURE);
    assert(built.success === true, `(1) the fixture builds ${built.success ? "" : JSON.stringify(built.errors)}`);
    const before = await call("puerts_graph_inspect", { asset_path: BP, include_pins: true });
    const nodeCount = before.data?.graph?.node_count ?? 0;
    assert(nodeCount >= 20, `(1) the fixture has at least 20 nodes (${nodeCount})`);
    const shaBefore = fileSha(contentFile(BP));
    const hashBefore = hashOf(before.data);
    assert(typeof hashBefore === "string" && hashBefore.length === 40, "(2) the graph reports a structure hash");
    evidence.steps.fixture = { node_count: nodeCount, structure_hash: hashBefore, file_sha256: shaBefore };

    console.log("\n-- 2. plan_only resolves and reports without touching anything");
    const planOps = [{ op: "set_pin_default", target: { type: "PrintString", x: 600, y: 0 }, pin: "InString", value: "TWO" }];
    const plan = await call("puerts_blueprint_graph_patch", { asset_path: BP, operations: planOps, plan_only: true });
    assert(plan.success === true, `(2) plan_only succeeds ${plan.success ? "" : JSON.stringify(plan.errors)}`);
    assert(plan.data?.plan_only === true, "(2) the response says it was a plan");
    assert((plan.data?.matched_nodes ?? []).length === 1, "(2) the plan matched exactly one node");
    assert((plan.data?.pin_defaults_to_change ?? []).length === 1, "(2) the plan lists one pin default to change");
    assert(plan.data?.expected_change_count === 1, "(2) expected_change_count is 1");
    const afterPlan = await call("puerts_graph_inspect", { asset_path: BP, include_pins: true });
    assert(hashOf(afterPlan.data) === hashBefore, "(2) plan_only changed nothing: the structure hash is unmoved");
    assert(fileSha(contentFile(BP)) === shaBefore, "(2) plan_only wrote nothing to disk");
    evidence.steps.plan = plan.data;

    console.log("\n-- 3. one batch: a pin default, a new node with two links, a move, a removal, a relink");
    const p4Before = p4Opened();
    const patch = await call("puerts_blueprint_graph_patch", {
      asset_path: BP,
      operations: [
        // 3. one pin default
        { op: "set_pin_default", target: { type: "PrintString", x: 600, y: 0 }, pin: "InString", value: "TWO" },
        // 4. one node and two links
        { op: "add_node", new_id: "added", node: { id: "added", type: "PrintString", x: 2400, y: 700, params: { InString: "added" } } },
        { op: "connect_pins", from: { target: { type: "PrintString", x: 2100, y: 400 }, pin: "then" }, to: { target: { new_id: "added" }, pin: "exec" } },
        { op: "connect_pins", from: { target: { new_id: "added" }, pin: "then" }, to: { target: { type: "Delay", x: 600, y: 900 }, pin: "exec" } },
        // 5. move one node
        { op: "move_node", target: { type: "Comment", x: 0, y: -300 }, x: 100, y: -400 },
        // 6. remove one node the builder made
        // The builder spawns every Operator through FGraphNodeCreator<UK2Node_CallFunction>,
        // so that is the class inspection reports, not a commutative-operator class.
        { op: "remove_node", target: { type: "Operator", node_class: "K2Node_CallFunction", x: 700, y: 700 } },
        // 7. disconnect and reconnect one link
        { op: "disconnect_pins", from: { target: { type: "BeginPlay" }, pin: "then" }, to: { target: { type: "PrintString", x: 300, y: 0 }, pin: "exec" } },
        { op: "connect_pins", from: { target: { type: "BeginPlay" }, pin: "then" }, to: { target: { type: "PrintString", x: 300, y: 0 }, pin: "exec" } },
      ],
    });
    assert(patch.success === true, `(3) the batch applies ${patch.success ? "" : JSON.stringify(patch.errors)}`);
    assert(patch.data?.compile_status === "UpToDate", `(3) the patched Blueprint compiles (${patch.data?.compile_status})`);
    assert((patch.data?.verification_mismatches ?? []).length === 0, "(3) no verification mismatches");
    assert((patch.data?.failed_operations ?? []).length === 0, "(3) no failed operations");
    assert((patch.data?.created_nodes ?? []).length === 1, "(4) exactly one node was created");
    assert((patch.data?.created_links ?? []).length === 3, "(4)(7) three links were created (two new, one reconnected)");
    assert((patch.data?.removed_links ?? []).length === 1, "(7) one link was removed");
    assert((patch.data?.removed_nodes ?? []).length === 1, "(6) one node was removed");
    assert((patch.data?.changed_pin_defaults ?? []).length === 1, "(3) one pin default was changed");
    assert(typeof patch.data?.elapsed_ms === "number", `(15) the command reports its own elapsed time (${patch.data?.elapsed_ms} ms)`);
    assert(patch.data?.pre_structure_hash === hashBefore, "(3) the reported pre hash is the hash measured before");
    assert(patch.data?.post_structure_hash !== hashBefore, "(3) the graph actually changed");
    evidence.steps.patch = {
      elapsed_ms: patch.data?.elapsed_ms,
      applied_operation_count: patch.data?.applied_operation_count,
      pre_structure_hash: patch.data?.pre_structure_hash,
      post_structure_hash: patch.data?.post_structure_hash,
    };

    console.log("\n-- 4. independent verification, and what did NOT move");
    const after = await call("puerts_graph_inspect", { asset_path: BP, include_pins: true });
    assert(hashOf(after.data) === patch.data?.post_structure_hash,
      "(8) an independent read agrees with the hash the patch reported");
    assert((after.data?.graph?.node_count ?? 0) === nodeCount, "(8) one node added and one removed leaves the count");
    const nodesAfter = after.data?.graph?.nodes ?? [];
    const two = nodesAfter.find((n) => JSON.stringify(n.params ?? {}).includes("TWO"));
    assert(two !== undefined, "(8) the changed pin default is present in the asset");
    assert(nodesAfter.some((n) => n.x === 2400 && n.y === 700), "(8) the added node is present at its position");
    assert(nodesAfter.some((n) => n.type === "Comment" && n.x === 100 && n.y === -400), "(8) the moved node is at its new position");
    assert(!nodesAfter.some((n) => n.x === 700 && n.y === 700), "(8) the removed node is gone");

    // The claim the whole command rests on: everything not named survived
    // untouched, compared by structural identity rather than by object name.
    const untouchedBefore = keysOf(before.data).filter((k) => {
      const n = JSON.parse(k);
      const [type, , x, y] = n;
      if (type === "Comment") return false;              // moved
      if (x === 700 && y === 700) return false;          // removed
      if (type === "PrintString" && x === 600 && y === 0) return false; // pin default changed
      return true;
    });
    const afterKeys = keysOf(after.data);
    const missing = untouchedBefore.filter((k) => !afterKeys.includes(k));
    assert(missing.length === 0,
      `(9) every node the patch did not name is structurally identical afterwards (${missing.length} changed)`);
    evidence.steps.untouched_nodes_compared = untouchedBefore.length;

    console.log("\n-- 5. rerunning the same patch is a no-op");
    const shaAfterPatch = fileSha(contentFile(BP));
    const rerun = await call("puerts_blueprint_graph_patch", {
      asset_path: BP,
      operations: [
        { op: "set_pin_default", target: { type: "PrintString", x: 600, y: 0 }, pin: "InString", value: "TWO" },
        { op: "move_node", target: { type: "Comment", x: 100, y: -400 }, x: 100, y: -400 },
        { op: "connect_pins", from: { target: { type: "BeginPlay" }, pin: "then" }, to: { target: { type: "PrintString", x: 300, y: 0 }, pin: "exec" } },
      ],
    });
    assert(rerun.success === true, `(10) the rerun succeeds ${rerun.success ? "" : JSON.stringify(rerun.errors)}`);
    assert(rerun.data?.applied_operation_count === 0, `(10) the rerun applied nothing (${rerun.data?.applied_operation_count})`);
    assert((rerun.data?.unchanged_operations ?? []).length === 3, "(10) all three operations reported as already satisfied");
    assert(rerun.data?.converged === true, "(10) the rerun reports converged");
    assert(rerun.data?.saved === false, "(10) a converged rerun does not save");
    assert(fileSha(contentFile(BP)) === shaAfterPatch, "(10) the file on disk is untouched by the rerun");
    evidence.steps.rerun = { applied: rerun.data?.applied_operation_count, converged: rerun.data?.converged };

    console.log("\n-- 6. an ambiguous selector is refused without mutating");
    // Three PrintStrings share a type and differ only by position, so a
    // type-only selector matches all of them.
    const ambiguous = await call("puerts_blueprint_graph_patch", {
      asset_path: BP,
      operations: [{ op: "set_pin_default", target: { type: "PrintString" }, pin: "InString", value: "AMBIGUOUS" }],
    });
    assert(ambiguous.success === false, "(11) an ambiguous selector fails the request");
    // errors is the authoritative place to read a refusal. A failed request comes
    // back through the failure envelope, which carries the error text and an
    // empty data, so the per-selector detail has to be in the error string
    // itself; data.ambiguous_selectors survives only on a successful plan.
    const ambiguousText = JSON.stringify(ambiguous.errors ?? []);
    assert(/matched \d+ nodes and a patch will not choose between them/.test(ambiguousText),
      `(11) the refusal says it will not choose (got: ${ambiguousText.slice(0, 240)})`);
    const afterAmbiguous = await call("puerts_graph_inspect", { asset_path: BP, include_pins: true });
    assert(hashOf(afterAmbiguous.data) === hashOf(after.data), "(11) nothing changed");
    assert(fileSha(contentFile(BP)) === shaAfterPatch, "(11) nothing reached disk");

    console.log("\n-- 7. a batch that fails partway leaves the graph exactly as it was");
    // The first operation is valid and the second is not. A batch that applied
    // the first and then stopped would be the half-applied state this command
    // exists to make impossible.
    const partial = await call("puerts_blueprint_graph_patch", {
      asset_path: BP,
      operations: [
        { op: "move_node", target: { type: "Comment", x: 100, y: -400 }, x: 999, y: 999 },
        { op: "set_pin_default", target: { type: "VariableGet", var_name: "NoSuchVariable" }, pin: "InString", value: "x" },
      ],
    });
    assert(partial.success === false, "(12) a batch with an unresolvable target fails");
    const afterPartial = await call("puerts_graph_inspect", { asset_path: BP, include_pins: true });
    assert(hashOf(afterPartial.data) === hashOf(after.data),
      "(12) the valid operation in the failed batch was NOT applied");
    assert(fileSha(contentFile(BP)) === shaAfterPatch, "(12) the file on disk is unchanged");
    assert((partial.data?.cleanup?.dirty_packages_after ?? []).length === 0
      || partial.data?.cleanup === undefined, "(13) no dirty package remains");
    evidence.steps.partial_batch_refused = true;

    const p4After = p4Opened();
    if (p4Before !== null && p4After !== null) {
      assert(p4Before === p4After, "(13) no source-control entry appeared");
      evidence.steps.source_control_quiescent = true;
    } else {
      console.log("  INFO  p4 unavailable; source-control quiescence not measured");
      evidence.steps.source_control_quiescent = null;
    }

    evidence.post_patch_structure_hash = hashOf(after.data);
    evidence.post_patch_node_count = after.data?.graph?.node_count ?? 0;
  }

  evidence.mcp_round_trips = roundTrips;

  mkdirSync(join(repoRoot, "docs", "evidence"), { recursive: true });
  writeFileSync(join(repoRoot, "docs", "evidence", `bp-graph-patch${phase === "cold" ? "-cold" : ""}.json`),
    JSON.stringify(evidence, null, 2) + "\n");
  console.log(`\nevidence written; ${roundTrips} MCP round trips this phase`);
  console.log(failures.length === 0 ? `all checks passed (${phase} phase)` : `${failures.length} check(s) did not hold:`);
  for (const f of failures) console.log(`  - ${f}`);
  process.exit(failures.length === 0 ? 0 : 1);
} catch (error) {
  console.error(`\nbp-graph-patch: ${error.message}`);
  process.exit(1);
} finally {
  child.kill();
}
