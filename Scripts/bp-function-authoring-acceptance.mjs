#!/usr/bin/env node
// Live acceptance for authoring and incrementally patching a Blueprint FUNCTION.
//
// The property under test is that a function's signature, metadata, local
// variables, graph body and call sites can each be changed on their own, that
// changing one does not disturb the others, and that a failure leaves the
// Blueprint exactly as the batch found it.
//
// A function is the hardest member to patch incrementally because it is not one
// thing. Its parameters live on two terminator nodes, its metadata lives in a
// struct on the entry node, its locals live in an array on that same node and
// nowhere in the Blueprint's variable list, its body is a graph, and its call
// sites are nodes in OTHER graphs that reconstruct themselves from the compiled
// signature. Rebuilding the function to change any one of those would take the
// other four with it, which is exactly what this script is here to disprove.
//
//   --phase=record  build the fixture, author the function, patch every facet
//   --phase=cold    after a restart, the patched function is the patched function
//
// Fresh path per run, for the reason bp-member-patch-acceptance.mjs records at
// length: a fixture inherited from an earlier run makes a red mean nothing. The
// asset path carries the record phase's start time in base 36, so it cannot have
// existed before this run, and the cold phase reads the path back out of the
// evidence file rather than minting a new one.

import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, readdirSync, unlinkSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";
import { requireCurrentInstall } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
const projectRoot = requireCurrentInstall();
const phase = process.argv.includes("--phase=cold") ? "cold" : "record";
const evidenceDir = join(repoRoot, "docs", "evidence");
const recordFile = join(evidenceDir, "bp-function-authoring.json");

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
const failures = [];
function assert(condition, label) {
  if (condition) { console.log(`  PASS  ${label}`); return true; }
  console.log(`  FAIL  ${label}`); failures.push(label); return false;
}
function note(label) { console.log(`  NOTE  ${label}`); }

const fileSha = (p) => createHash("sha256").update(readFileSync(p)).digest("hex");
const contentFile = (p) =>
  join(projectRoot, "Content", p.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");

const PREFIX = "BP_FunctionAuthoringFixture_";
const runId = phase === "cold"
  ? JSON.parse(readFileSync(recordFile, "utf8")).run_id
  : Date.now().toString(36);
if (typeof runId !== "string" || !/^[a-z0-9]+$/.test(runId)) {
  throw new Error(`no usable run id (${runId}); run --phase=record first`);
}
const BP = `/Game/MCPGenerated/${PREFIX}${runId}`;
const BP_CLASS = `${BP}.${PREFIX}${runId}_C`;
const FN = "ComputeDamage";

function sweepEarlierFixtures() {
  const dir = join(projectRoot, "Content", "MCPGenerated");
  const keep = `${PREFIX}${runId}.uasset`;
  const removed = [];
  try {
    for (const name of readdirSync(dir)) {
      if (!name.startsWith(PREFIX) || !name.endsWith(".uasset") || name === keep) continue;
      try { unlinkSync(join(dir, name)); removed.push(name); } catch { /* in use; leave it */ }
    }
  } catch { /* directory not there yet */ }
  return removed;
}

// Unrelated state on purpose. The claim is about what does NOT move when a
// function is patched, and a fixture with nothing else in it cannot make that
// claim: components, member variables, a second function and an event graph
// that references a variable are all here to be checked for stillness.
const FIXTURE = {
  asset_path: BP,
  parent_class: "Actor",
  components: [
    { class: "SceneComponent", name: "Root" },
    { class: "StaticMeshComponent", name: "Body", attach_to: "Root" },
  ],
  variables: [
    { name: "Health", type: "float", default: 100 },
    { name: "Armor", type: "float", default: 5 },
    { name: "Tag", type: "string", default: "fixture" },
  ],
  graph: {
    nodes: [
      { id: "begin", type: "BeginPlay", x: 0, y: 0 },
      { id: "say", type: "PrintString", x: 320, y: 0, params: { InString: "function fixture" } },
      { id: "getHealth", type: "VariableGet", x: 0, y: 260, params: { varName: "Health" } },
    ],
    connections: [{ from: "begin.then", to: "say.exec" }],
  },
};

// The function this whole script is about, as one desired state.
const DESIRED_SIGNATURE = {
  inputs: [
    { name: "BaseDamage", type: { category: "float" } },
    { name: "Multiplier", type: { category: "float" } },
  ],
  outputs: [{ name: "Damage", type: { category: "float" } }],
  metadata: { category: "Combat", tooltip: "Calculates final damage", pure: true },
};
const setFunctionOp = (extra = {}) => ({ op: "set_function", name: FN, ...DESIRED_SIGNATURE, ...extra });

const evidence = { phase, run_id: runId, asset_path: BP, function: FN, steps: {} };
const memberHash = (d) => d?.member_structure_hash_sha1;
const namesOf = (list) => (list ?? []).map((e) => e.name).sort();
const fnOf = (d) => (d?.functions ?? []).find((f) => f.name === FN);

/** Everything an independent reader can see right now: the members the editor
    holds, the function as the inspector reports it, and the bytes on disk. */
async function snapshot() {
  const read = await call("puerts_graph_inspect", { asset_path: BP });
  return {
    hash: memberHash(read.data),
    sha: fileSha(contentFile(BP)),
    data: read.data,
    fn: fnOf(read.data),
  };
}

/** Read one graph's nodes, independently of whatever wrote them. */
async function graphNodes(graphName) {
  const read = await call("puerts_graph_inspect", { asset_path: BP, graph_name: graphName });
  return { ok: read.success === true, data: read.data, nodes: read.data?.graph?.nodes ?? [] };
}

/** A refusal check that owns its own baseline, so a red here is about this
    operation rather than about something earlier in the script. */
async function refusal(tag, operations, pattern) {
  const before = await snapshot();
  const result = await call("puerts_blueprint_member_patch", { asset_path: BP, operations });
  const after = await snapshot();
  const errorText = JSON.stringify(result.errors ?? []);
  assert(result.success === false, `${tag}: the request is refused`);
  if (pattern) {
    assert(pattern.test(errorText), `${tag}: the refusal names the rule (got: ${errorText.slice(0, 240)})`);
  }
  assert(after.hash === before.hash, `${tag}: the member hash is unmoved`);
  assert(after.sha === before.sha, `${tag}: nothing reached disk`);
  assert(!/Treat this asset as damaged/.test(errorText), `${tag}: no unrecovered rollback was reported`);
  return { result, before, after, errorText };
}

try {
  await send("initialize", {
    protocolVersion: "2024-11-05", capabilities: {},
    clientInfo: { name: "bp-function-authoring", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    console.log(`\ncold restart survival: ${BP}::${FN}`);
    const recorded = JSON.parse(readFileSync(recordFile, "utf8"));
    const read = await call("puerts_graph_inspect", { asset_path: BP });
    assert(read.success === true, "cold: the cold-loaded Blueprint inspects");
    const fn = fnOf(read.data);
    assert(fn !== undefined, "cold: the function survived the restart");
    assert(JSON.stringify(fn) === JSON.stringify(recorded.final_function),
      "cold: the function the warm phase left is the function that loaded, field for field");
    assert(memberHash(read.data) === recorded.final_member_hash,
      `cold: the member hash is the warm phase's (${memberHash(read.data)} vs ${recorded.final_member_hash})`);
    assert(fileSha(contentFile(BP)) === recorded.final_file_sha256,
      "cold: the .uasset on disk is byte-identical to what the warm phase saved");
    const body = await graphNodes(FN);
    assert(body.ok === true, "cold: the function graph still inspects");
    assert(body.nodes.length === recorded.final_body_node_count,
      `cold: the body still has ${recorded.final_body_node_count} nodes (got ${body.nodes.length})`);
  } else {
    console.log("\n== reseed: a fixture no earlier run can have touched");
    const swept = sweepEarlierFixtures();
    console.log(`  INFO  asset ${BP}; ${swept.length} earlier fixture file(s) unlinked`);
    const preexisting = await call("puerts_graph_inspect", { asset_path: BP });
    assert(preexisting.success === false,
      `reseed: the fixture path did not exist before this run ${preexisting.success ? "(IT DID: the reseed is not a reseed)" : ""}`);
    const built = await call("puerts_blueprint_build", FIXTURE);
    assert(built.success === true,
      `reseed: the fixture builds from nothing ${built.success ? "" : JSON.stringify(built.errors)}`);

    // A second function, authored the old way, that nothing below ever names.
    // Its job is to still be there, unchanged, at the end.
    const helper = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{ op: "add_function", name: "UnrelatedHelper" }],
    });
    assert(helper.success === true,
      `reseed: an unrelated second function exists to be left alone ${helper.success ? "" : JSON.stringify(helper.errors)}`);

    const baseline = await snapshot();
    const baselineEventGraph = await graphNodes("EventGraph");
    assert(baseline.fn === undefined, "reseed: the fixture has no ComputeDamage yet");
    evidence.steps.fixture = {
      member_hash: baseline.hash,
      file_sha256: baseline.sha,
      variables: namesOf(baseline.data?.variables),
      components: namesOf(baseline.data?.components),
      functions: namesOf(baseline.data?.functions),
      event_graph_node_guids: baselineEventGraph.nodes.map((n) => n.node_guid).sort(),
    };

    console.log("\n== 1. plan_only reads the change before it is made");
    const plan = await call("puerts_blueprint_member_patch", {
      asset_path: BP, operations: [setFunctionOp()], plan_only: true,
    });
    assert(plan.success === true, `plan: succeeds ${plan.success ? "" : JSON.stringify(plan.errors)}`);
    assert(plan.data?.plan_only === true, "plan: the response says it was a plan");
    assert(plan.data?.expected_change_count === 1, "plan: expected_change_count is 1");
    const fnPlan = (plan.data?.function_plans ?? [])[0];
    assert(fnPlan !== undefined, "plan: a function_plans entry describes the function");
    assert(fnPlan?.function_exists === false, "plan: function_exists is false before it is created");
    const changes = JSON.stringify(fnPlan?.signature_changes ?? []);
    assert(/BaseDamage/.test(changes) && /Multiplier/.test(changes) && /Damage/.test(changes),
      `plan: signature_changes names all three parameters (${changes})`);
    assert((fnPlan?.metadata_changes ?? []).includes("is_pure")
      && (fnPlan?.metadata_changes ?? []).includes("category")
      && (fnPlan?.metadata_changes ?? []).includes("tooltip"),
      `plan: metadata_changes names category, tooltip and is_pure (${JSON.stringify(fnPlan?.metadata_changes)})`);
    assert((fnPlan?.call_sites_affected ?? []).length === 0,
      "plan: a function that does not exist yet has no call sites");
    const afterPlan = await snapshot();
    assert(afterPlan.hash === baseline.hash, "plan: the members are unmoved");
    assert(afterPlan.sha === baseline.sha, "on-disk file hash: plan_only wrote nothing");
    evidence.steps.plan = plan.data;

    console.log("\n== 2. create the function from desired state, and compile it");
    const created = await call("puerts_blueprint_member_patch", {
      asset_path: BP, operations: [setFunctionOp()],
    });
    assert(created.success === true,
      `create: the function is created ${created.success ? "" : JSON.stringify(created.errors)}`);
    assert(created.data?.applied_operation_count === 1, "create: exactly one operation applied");
    assert(created.data?.compile_status !== "Skipped",
      `create: the Blueprint was compiled (${created.data?.compile_status})`);
    assert((created.data?.compile_errors ?? []).length === 0,
      `create: the compile reported no errors ${JSON.stringify(created.data?.compile_errors)}`);
    assert(created.data?.saved === true, "create: the asset was saved");
    assert((created.data?.verification_mismatches ?? []).length === 0,
      "create: the independent read-back found no mismatch");

    console.log("\n== 3. read the signature back with the independent inspector");
    const afterCreate = await snapshot();
    const fn = afterCreate.fn;
    assert(fn !== undefined, "signature: the inspector reports the function");
    assert(JSON.stringify((fn?.inputs ?? []).map((p) => p.name)) === JSON.stringify(["BaseDamage", "Multiplier"]),
      `signature: the inputs are the ones asked for, in order (${JSON.stringify((fn?.inputs ?? []).map((p) => p.name))})`);
    assert((fn?.inputs ?? []).every((p) => p.type?.category === "float"),
      "signature: both inputs are floats");
    assert(JSON.stringify((fn?.outputs ?? []).map((p) => p.name)) === JSON.stringify(["Damage"]),
      `signature: the output is Damage (${JSON.stringify((fn?.outputs ?? []).map((p) => p.name))})`);
    assert(fn?.category === "Combat", `metadata: category is Combat (${fn?.category})`);
    assert(fn?.tooltip === "Calculates final damage", `metadata: tooltip is the one asked for (${fn?.tooltip})`);
    assert(fn?.is_pure === true, `metadata: the function is pure (${fn?.is_pure})`);
    assert(fn?.access === "public", `metadata: access is reported (${fn?.access})`);
    assert(Array.isArray(fn?.locals) && fn.locals.length === 0,
      "signature: the function starts with no locals");

    console.log("\n== 4. author the body inside the function graph, not by rebuilding it");
    const emptyBody = await graphNodes(FN);
    assert(emptyBody.ok === true, "body: the function graph inspects by name");
    const entryNode = emptyBody.nodes.find((n) => /FunctionEntry/.test(n.node_class ?? ""));
    const resultNode = emptyBody.nodes.find((n) => /FunctionResult/.test(n.node_class ?? ""));
    assert(entryNode !== undefined && resultNode !== undefined,
      `body: the graph has an entry and a result node (${emptyBody.nodes.map((n) => n.node_class).join(", ")})`);

    const bodyPatch = await call("puerts_blueprint_graph_patch", {
      asset_path: BP, graph: FN,
      operations: [
        { op: "add_node", new_id: "mul", node: { type: "Operator", x: 300, y: 120, params: { op: "multiply_float" } } },
        { op: "add_node", new_id: "clamp", node: { type: "CallFunction", x: 620, y: 120, params: { class: "KismetMathLibrary", function: "FMax", B: 0 } } },
        { op: "connect_pins", from: { target: { node_guid: entryNode?.node_guid }, pin: "BaseDamage" }, to: { target: { new_id: "mul" }, pin: "A" } },
        { op: "connect_pins", from: { target: { node_guid: entryNode?.node_guid }, pin: "Multiplier" }, to: { target: { new_id: "mul" }, pin: "B" } },
        { op: "connect_pins", from: { target: { new_id: "mul" }, pin: "ReturnValue" }, to: { target: { new_id: "clamp" }, pin: "A" } },
        { op: "connect_pins", from: { target: { new_id: "clamp" }, pin: "ReturnValue" }, to: { target: { node_guid: resultNode?.node_guid }, pin: "Damage" } },
      ],
    });
    assert(bodyPatch.success === true,
      `body: the multiply-and-clamp chain is authored into the function graph ${bodyPatch.success ? "" : JSON.stringify(bodyPatch.errors)}`);
    const body = await graphNodes(FN);
    assert(body.nodes.length === 4,
      `body: the graph holds entry, multiply, clamp and result (${body.nodes.length} nodes)`);
    evidence.steps.body_node_count = body.nodes.length;

    console.log("\n== 5. add a call site in the event graph");
    const callSitePatch = await call("puerts_blueprint_graph_patch", {
      asset_path: BP, graph: "EventGraph",
      operations: [
        { op: "add_node", new_id: "callsite", node: { type: "CallFunction", x: 320, y: 420, params: { class: BP_CLASS, function: FN } } },
      ],
    });
    assert(callSitePatch.success === true,
      `call site: a call to ${FN} is added to the event graph ${callSitePatch.success ? "" : JSON.stringify(callSitePatch.errors)}`);
    const withCallSite = await graphNodes("EventGraph");
    const callSite = withCallSite.nodes.find((n) => n.params?.function === FN);
    assert(callSite !== undefined,
      `call site: the inspector finds the call node (${withCallSite.nodes.map((n) => n.params?.function ?? n.type).join(", ")})`);
    const callSiteGuid = callSite?.node_guid;

    // Wire something INTO the parameter that is about to be renamed. Without
    // this the rename check below only proves the node survived, and a node
    // surviving with its connections dropped is the exact failure a rename is
    // supposed to avoid: reconstruction after the recompile matches pins by
    // name, so an unrenamed call-site pin loses its link silently.
    const healthGetter = withCallSite.nodes.find((n) => n.params?.var_name === "Health");
    assert(healthGetter !== undefined, "call site: the fixture's Health getter is addressable");
    const wired = await call("puerts_blueprint_graph_patch", {
      asset_path: BP, graph: "EventGraph",
      operations: [{
        op: "connect_pins",
        from: { target: { node_guid: healthGetter?.node_guid }, pin: "Health" },
        to: { target: { node_guid: callSiteGuid }, pin: "Multiplier" },
      }],
    });
    assert(wired.success === true,
      `call site: a value is wired into the parameter about to be renamed ${wired.success ? "" : JSON.stringify(wired.errors)}`);

    const planWithCallSite = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{ op: "set_function", name: FN, metadata: { tooltip: "probe" } }],
      plan_only: true,
    });
    assert(((planWithCallSite.data?.function_plans ?? [])[0]?.call_sites_affected ?? []).length === 1,
      `plan: the plan now reports the one call site ${JSON.stringify((planWithCallSite.data?.function_plans ?? [])[0]?.call_sites_affected)}`);

    console.log("\n== 6. patch metadata alone");
    const beforeMeta = await snapshot();
    const metaPatch = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{ op: "set_function", name: FN, metadata: { category: "Combat|Damage", tooltip: "Final damage after the multiplier" } }],
    });
    assert(metaPatch.success === true,
      `metadata patch: succeeds ${metaPatch.success ? "" : JSON.stringify(metaPatch.errors)}`);
    const afterMeta = await snapshot();
    assert(afterMeta.fn?.category === "Combat|Damage", `metadata patch: the category moved (${afterMeta.fn?.category})`);
    assert(afterMeta.fn?.tooltip === "Final damage after the multiplier",
      `metadata patch: the tooltip moved (${afterMeta.fn?.tooltip})`);
    assert(JSON.stringify(afterMeta.fn?.inputs) === JSON.stringify(beforeMeta.fn?.inputs),
      "metadata patch: the parameters did not move with it");
    assert(afterMeta.fn?.is_pure === true, "metadata patch: pure did not move with it");

    console.log("\n== 7. rename a parameter without replacing it");
    const renamed = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{
        op: "set_function", name: FN,
        inputs: [
          { name: "BaseDamage", type: { category: "float" } },
          { name: "Scale", rename_from: "Multiplier", type: { category: "float" } },
        ],
      }],
    });
    assert(renamed.success === true,
      `rename: succeeds ${renamed.success ? "" : JSON.stringify(renamed.errors)}`);
    const afterRename = await snapshot();
    assert(JSON.stringify((afterRename.fn?.inputs ?? []).map((p) => p.name)) === JSON.stringify(["BaseDamage", "Scale"]),
      `rename: the parameter is Scale and BaseDamage is untouched (${JSON.stringify((afterRename.fn?.inputs ?? []).map((p) => p.name))})`);
    const eventGraphAfterRename = await graphNodes("EventGraph");
    const survivingCallSite = eventGraphAfterRename.nodes.find((n) => n.node_guid === callSiteGuid);
    assert(survivingCallSite !== undefined,
      "rename: the call site node still exists, with the same identity");
    const wiredRead = await call("puerts_graph_inspect", {
      asset_path: BP, graph_name: "EventGraph", include_pins: true,
    });
    const wiredCallSite = (wiredRead.data?.graph?.nodes ?? []).find((n) => n.node_guid === callSiteGuid);
    const renamedPin = (wiredCallSite?.pins ?? []).find((p) => p.pin_name === "Scale");
    const strandedPin = (wiredCallSite?.pins ?? []).find((p) => p.pin_name === "Multiplier");
    assert(strandedPin === undefined,
      "rename: the call site no longer carries a pin under the old name");
    assert(renamedPin !== undefined,
      `rename: the call site carries the renamed pin (${(wiredCallSite?.pins ?? []).map((p) => p.pin_name).join(", ")})`);
    assert((renamedPin?.links ?? []).length === 1,
      `rename: the connection into that parameter survived the rename (${JSON.stringify(renamedPin?.links ?? [])})`);
    const bodyAfterRename = await graphNodes(FN);
    assert(bodyAfterRename.nodes.length === 4,
      `rename: the body is still four nodes (${bodyAfterRename.nodes.length})`);
    const renamedEntry = bodyAfterRename.nodes.find((n) => /FunctionEntry/.test(n.node_class ?? ""));
    evidence.steps.rename = {
      inputs: (afterRename.fn?.inputs ?? []).map((p) => p.name),
      call_site_survived: survivingCallSite !== undefined,
      entry_guid_stable: renamedEntry?.node_guid === entryNode?.node_guid,
    };

    console.log("\n== 8. add a local variable, which lives nowhere the variable list can reach");
    const withLocal = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{
        op: "set_function", name: FN,
        locals: [{ name: "Working", type: { category: "float" }, default: "0.0" }],
      }],
    });
    assert(withLocal.success === true,
      `locals: succeeds ${withLocal.success ? "" : JSON.stringify(withLocal.errors)}`);
    const afterLocal = await snapshot();
    assert(JSON.stringify((afterLocal.fn?.locals ?? []).map((l) => l.name)) === JSON.stringify(["Working"]),
      `locals: the inspector reports the local (${JSON.stringify(afterLocal.fn?.locals)})`);
    assert(!namesOf(afterLocal.data?.variables).includes("Working"),
      "locals: the local did NOT become a member variable");

    console.log("\n== 9. one pin default, one node position, nothing else");
    const clampNode = bodyAfterRename.nodes.find((n) => n.params?.function === "FMax");
    const mulNode = bodyAfterRename.nodes.find((n) => /multiply/i.test(JSON.stringify(n.params ?? {})));
    assert(clampNode !== undefined && mulNode !== undefined,
      `body edit: the clamp and multiply nodes are addressable (${bodyAfterRename.nodes.map((n) => n.type).join(", ")})`);
    const beforeEdit = await snapshot();
    const bodyEdit = await call("puerts_blueprint_graph_patch", {
      asset_path: BP, graph: FN,
      operations: [
        { op: "set_pin_default", target: { node_guid: clampNode?.node_guid }, pin: "B", value: 0.25 },
        { op: "move_node", target: { node_guid: mulNode?.node_guid }, x: 340, y: 200 },
      ],
    });
    assert(bodyEdit.success === true,
      `body edit: one pin default and one move apply ${bodyEdit.success ? "" : JSON.stringify(bodyEdit.errors)}`);
    const afterEdit = await graphNodes(FN);
    const movedNode = afterEdit.nodes.find((n) => n.node_guid === mulNode?.node_guid);
    assert(movedNode?.x === 340 && movedNode?.y === 200,
      `body edit: the node moved and kept its identity (${movedNode?.x},${movedNode?.y})`);
    assert(afterEdit.nodes.length === 4, "body edit: no node was added or lost");
    const afterEditMembers = await snapshot();
    assert(JSON.stringify(afterEditMembers.fn?.inputs) === JSON.stringify(beforeEdit.fn?.inputs),
      "body edit: editing the body did not touch the signature");

    console.log("\n== 10. everything unrelated is still exactly where it was");
    const now = await snapshot();
    const nowEventGraph = await graphNodes("EventGraph");
    assert(JSON.stringify(namesOf(now.data?.variables)) === JSON.stringify(evidence.steps.fixture.variables),
      `unrelated: the member variables are unchanged (${namesOf(now.data?.variables).join(", ")})`);
    assert(JSON.stringify(namesOf(now.data?.components)) === JSON.stringify(evidence.steps.fixture.components),
      `unrelated: the components are unchanged (${namesOf(now.data?.components).join(", ")})`);
    assert((now.data?.functions ?? []).some((f) => f.name === "UnrelatedHelper"),
      "unrelated: the second function is still there");
    const helperNow = (now.data?.functions ?? []).find((f) => f.name === "UnrelatedHelper");
    assert((helperNow?.inputs ?? []).length === 0 && (helperNow?.locals ?? []).length === 0,
      "unrelated: the second function's own signature is untouched");
    const originalGuids = new Set(evidence.steps.fixture.event_graph_node_guids);
    const survivors = nowEventGraph.nodes.filter((n) => originalGuids.has(n.node_guid));
    assert(survivors.length === originalGuids.size,
      `unrelated: every event graph node the fixture built still exists (${survivors.length} of ${originalGuids.size})`);
    assert(nowEventGraph.nodes.length === originalGuids.size + 1,
      `unrelated: the event graph grew by exactly the one call site (${nowEventGraph.nodes.length})`);

    console.log("\n== 11. the identical request again changes nothing at all");
    const beforeRerun = await snapshot();
    const rerun = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{
        op: "set_function", name: FN,
        inputs: [
          { name: "BaseDamage", type: { category: "float" } },
          { name: "Scale", type: { category: "float" } },
        ],
        outputs: [{ name: "Damage", type: { category: "float" } }],
        locals: [{ name: "Working", type: { category: "float" }, default: "0.0" }],
        metadata: { category: "Combat|Damage", tooltip: "Final damage after the multiplier", pure: true },
      }],
    });
    const afterRerun = await snapshot();
    assert(rerun.success === true, `converge: the rerun succeeds ${rerun.success ? "" : JSON.stringify(rerun.errors)}`);
    assert(rerun.data?.applied_operation_count === 0, "converge: nothing was applied");
    assert(rerun.data?.converged === true, "converge: the response says converged");
    assert(rerun.data?.compile_status === "Skipped",
      `converge: a converged batch does not compile (${rerun.data?.compile_status})`);
    assert(rerun.data?.saved === false, "converge: a converged batch does not save");
    assert(afterRerun.hash === beforeRerun.hash, "converge: the member hash is unmoved");
    assert(afterRerun.sha === beforeRerun.sha, "on-disk file hash: the converged rerun left the bytes alone");
    evidence.steps.converged = {
      applied: rerun.data?.applied_operation_count,
      compile_status: rerun.data?.compile_status,
      saved: rerun.data?.saved,
      file_sha256: afterRerun.sha,
    };

    console.log("\n== 12. an invalid parameter type is refused, and nothing moves");
    await refusal("invalid type", [{
      op: "set_function", name: FN,
      inputs: [
        { name: "BaseDamage", type: { category: "float" } },
        { name: "Scale", type: { category: "not_a_real_category" } },
      ],
    }], /not_a_real_category|cannot resolve|type/i);

    console.log("\n== 13. a shape error is refused before anything is attempted");
    await refusal("missing type descriptor", [{
      op: "set_function", name: FN, inputs: [{ name: "Nameless" }],
    }], /type/i);

    console.log("\n== 14. a failure later in the batch rolls the earlier work back");
    const beforeRollback = await snapshot();
    const rollbackAttempt = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [
        // Applies cleanly.
        { op: "set_function", name: FN, metadata: { tooltip: "this tooltip must not survive" } },
        // Fails inside the mutator, after the operation above has already been
        // written and compiled. A rename whose source does not exist is the
        // cheapest apply-time failure that is not a validation failure.
        {
          op: "set_function", name: "RollbackProbeFn",
          inputs: [{ name: "Renamed", rename_from: "NeverExisted", type: { category: "float" } }],
        },
      ],
    });
    const afterRollback = await snapshot();
    const rollbackErrors = JSON.stringify(rollbackAttempt.errors ?? []);
    assert(rollbackAttempt.success === false, "rollback: the batch fails");
    assert(/NeverExisted/.test(rollbackErrors),
      `rollback: the error names the operation that failed (${rollbackErrors.slice(0, 240)})`);
    assert(afterRollback.fn?.tooltip === beforeRollback.fn?.tooltip,
      `rollback: the first operation's tooltip was taken back (${afterRollback.fn?.tooltip})`);
    assert(JSON.stringify(afterRollback.fn?.inputs) === JSON.stringify(beforeRollback.fn?.inputs),
      "rollback: the signature is the one the batch found");
    assert(JSON.stringify(afterRollback.fn?.locals) === JSON.stringify(beforeRollback.fn?.locals),
      "rollback: the locals are the ones the batch found");
    assert(!(afterRollback.data?.functions ?? []).some((f) => f.name === "RollbackProbeFn"),
      "rollback: the half-created probe function is gone");
    assert(afterRollback.hash === beforeRollback.hash,
      `rollback: the member hash is back where it started (${afterRollback.hash} vs ${beforeRollback.hash})`);
    assert(afterRollback.sha === beforeRollback.sha,
      "on-disk file hash: the failed batch left the bytes alone");
    assert(!/Treat this asset as damaged/.test(rollbackErrors),
      "rollback: the command did not report an unrecovered rollback");
    const bodyAfterRollback = await graphNodes(FN);
    assert(bodyAfterRollback.nodes.length === 4,
      `rollback: the function body is intact (${bodyAfterRollback.nodes.length} nodes)`);

    const final = await snapshot();
    const finalBody = await graphNodes(FN);
    evidence.final_function = final.fn;
    evidence.final_member_hash = final.hash;
    evidence.final_file_sha256 = final.sha;
    evidence.final_body_node_count = finalBody.nodes.length;
  }

  if (!existsSync(evidenceDir)) mkdirSync(evidenceDir, { recursive: true });
  if (phase === "record") {
    writeFileSync(recordFile, JSON.stringify(evidence, null, 2));
  }
  console.log(`\n${failures.length === 0 ? "PASS" : "FAIL"}: ${phase} phase, ${failures.length} failure(s)`);
  if (failures.length > 0) {
    for (const f of failures) console.log(`  - ${f}`);
  }
  console.log(`evidence: ${recordFile}`);
} finally {
  child.stdin.end();
  child.kill();
}
process.exit(failures.length === 0 ? 0 : 1);
