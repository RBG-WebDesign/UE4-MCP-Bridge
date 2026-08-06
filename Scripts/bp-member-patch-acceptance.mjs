#!/usr/bin/env node
// Live acceptance for blueprint_member_patch.
//
// The property under test is that changing a Blueprint's MEMBERS changes what
// the batch names and nothing else, and that a failure leaves the Blueprint the
// batch found. That second half matters more here than it does for the graph
// patch: every UBlueprintMutatorLibrary entry point recompiles the Blueprint,
// so a batch that half-applies has already written and compiled a state nobody
// asked for. The command's answer is to classify the whole batch before the
// first mutation; this script is what decides whether that answer is true.
//
//   --phase=record  build the fixture, patch it, verify, converge, refuse
//   --phase=cold    after a restart, the patched members are still the patched members
//
// -- Why this run cannot be contaminated by the last one -----------------------
//
// The first two live executions of this script produced two different reds, and
// neither meant anything, because the fixture was one fixed asset path that each
// run inherited from the run before. A patch that renames Lamp to Beacon and
// removes Muzzle leaves an asset whose component set no rebuild can restore:
//
//   * blueprint_build converges on what the spec NAMES. A stale "Beacon" is
//     named by nothing, so the rebuild adds "Lamp" back and leaves "Beacon"
//     standing. The rename under test then collides with its own residue.
//   * At the time of the two failed runs, component downward convergence and
//     asset deletion did not exist. Both now have native primitives, but a
//     fresh path remains the stronger isolation because it proves the fixture
//     was unused before the test begins.
//
// So the reseed is a fresh asset path per run: BP_MemberProbe_<run id>, where
// the run id is the record phase's start time in base 36. The asset cannot have
// existed before this run, which is a stronger guarantee than deleting one, and
// it is the only guarantee obtainable from the primitives that ship today. The
// path is written into the evidence file so --phase=cold reads back the same
// asset after a restart, and the record phase unlinks the .uasset of every
// EARLIER probe so the fixture does not accumulate.
//
// NOT RUN by the lane that wrote it: no editor was launched. Running it is the
// integrator's step, and puerts_blueprint_member_patch stays at verification
// "implemented" with an empty live_evidence array until it passes.

import { execSync, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, readdirSync, unlinkSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
const projectRoot = requireCurrentInstall();
const phase = process.argv.includes("--phase=cold") ? "cold" : "record";
const evidenceDir = join(repoRoot, "docs", "evidence");
const recordFile = join(evidenceDir, "bp-member-patch.json");

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
    const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 120000);
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
const contentFile = (p) =>
  join(projectRoot, "Content", p.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");
function p4Opened() {
  try {
    return execSync("p4 opened", { cwd: join(projectRoot, "Content"), encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
  } catch { return null; }
}

// The reseed. In the record phase the run id is minted now, so the asset path
// has never been used; in the cold phase it is read back from the record phase's
// evidence, because the cold check is about THAT asset surviving a restart.
const PREFIX = "BP_MemberProbe_";
const runId = phase === "cold"
  ? JSON.parse(readFileSync(recordFile, "utf8")).run_id
  : Date.now().toString(36);
if (typeof runId !== "string" || !/^[a-z0-9]+$/.test(runId)) {
  throw new Error(`no usable run id (${runId}); run --phase=record first`);
}
const BP = `/Game/MCPGenerated/${PREFIX}${runId}`;

/** Unlink the .uasset of every probe from an earlier run. Hygiene only: the
    determinism comes from the fresh path, not from this. A probe still loaded
    in the editor keeps its in-memory package, which is harmless because nothing
    ever addresses it again. */
function sweepEarlierProbes() {
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

// A fixture with more members than the patch touches, and a graph that USES
// two of them. The claim is about what does not move, and members nothing
// references are the easy case: a variable with live reference nodes is where a
// removal takes things with it.
const FIXTURE = {
  asset_path: BP,
  parent_class: "Actor",
  components: [
    { class: "SceneComponent", name: "Root" },
    { class: "StaticMeshComponent", name: "Body", attach_to: "Root" },
    { class: "PointLightComponent", name: "Lamp", attach_to: "Root" },
    { class: "SceneComponent", name: "Muzzle", attach_to: "Body" },
  ],
  variables: [
    { name: "Alpha", type: "float", default: 1 },
    { name: "Beta", type: "float", default: 2 },
    { name: "Gamma", type: "float", default: 3 },
    { name: "Doomed", type: "float", default: 4 },
    { name: "Label", type: "string", default: "start" },
    // Count exists for one check: the same nonsense default offered to an int
    // and to a float, so a red on the float is a statement about the float path
    // rather than about validation in general.
    { name: "Count", type: "int", default: 1 },
  ],
  graph: {
    nodes: [
      { id: "begin", type: "BeginPlay", x: 0, y: 0 },
      { id: "say", type: "PrintString", x: 300, y: 0, params: { InString: "member probe" } },
      { id: "getA", type: "VariableGet", x: 0, y: 300, params: { varName: "Alpha" } },
      { id: "getB", type: "VariableGet", x: 0, y: 400, params: { varName: "Beta" } },
    ],
    connections: [{ from: "begin.then", to: "say.exec" }],
  },
};

const evidence = { phase, run_id: runId, asset_path: BP, steps: {} };
const memberHash = (d) => d?.member_structure_hash_sha1;
const namesOf = (list) => (list ?? []).map((e) => e.name).sort();
const varNamed = (d, name) => (d?.variables ?? []).find((v) => v.name === name);

/** Everything an independent reader can see about the asset right now: the
    members the editor holds and the bytes on disk. */
async function snapshot() {
  const read = await call("puerts_graph_inspect", { asset_path: BP });
  return { hash: memberHash(read.data), sha: fileSha(contentFile(BP)), data: read.data };
}

/** A refusal check that owns its own baseline.
 *
 *  The old script measured once, after the big batch, and compared every later
 *  refusal against that one snapshot. One red early therefore made every later
 *  check report a difference it had not caused, which is how a single fixture
 *  collision turned into seventeen failures that said nothing. Reading
 *  immediately before and immediately after makes each of these independent:
 *  a red here is about this operation. */
async function refusal(tag, operations, pattern) {
  const before = await snapshot();
  const result = await call("puerts_blueprint_member_patch", { asset_path: BP, operations });
  const after = await snapshot();
  const errorText = JSON.stringify(result.errors ?? []);
  assert(result.success === false, `${tag}: the request is refused`);
  if (pattern) {
    assert(pattern.test(errorText), `${tag}: the refusal names the rule (got: ${errorText.slice(0, 200)})`);
  }
  assert(after.hash === before.hash, `${tag}: the member hash is unmoved`);
  assert(after.sha === before.sha, `${tag}: nothing reached disk`);
  assert(!/Treat this asset as damaged/.test(errorText), `${tag}: no unrecovered rollback was reported`);
  return { result, before, after };
}

try {
  await send("initialize", {
    protocolVersion: "2024-11-05", capabilities: {},
    clientInfo: { name: "bp-member-patch", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    console.log(`\ncold restart survival: ${BP}`);
    const recorded = JSON.parse(readFileSync(recordFile, "utf8"));
    const read = await call("puerts_graph_inspect", { asset_path: BP });
    assert(read.success === true, "cold restart survival: the cold-loaded Blueprint inspects");
    assert(memberHash(read.data) === recorded.post_patch_member_hash,
      `cold restart survival: the member hash is the one the warm phase left (${memberHash(read.data)} vs ${recorded.post_patch_member_hash})`);
    assert(JSON.stringify(namesOf(read.data?.variables)) === JSON.stringify(recorded.post_patch_variables),
      "cold restart survival: the variable set is the one the warm phase left");
    assert(JSON.stringify(namesOf(read.data?.components)) === JSON.stringify(recorded.post_patch_components),
      "cold restart survival: the component set is the one the warm phase left");
    assert(JSON.stringify(namesOf(read.data?.functions)) === JSON.stringify(recorded.post_patch_functions),
      "cold restart survival: the function set is the one the warm phase left");
    assert(JSON.stringify(namesOf(read.data?.event_dispatchers)) === JSON.stringify(recorded.post_patch_dispatchers),
      "cold restart survival: the event dispatcher set is the one the warm phase left");
    assert(fileSha(contentFile(BP)) === recorded.post_patch_file_sha256,
      "on-disk file hash: the .uasset on disk is byte-identical to what the warm phase saved");
  } else {
    console.log("\n== reseed: a fixture that no earlier run can have touched");
    const swept = sweepEarlierProbes();
    console.log(`  INFO  asset ${BP}; ${swept.length} earlier probe file(s) unlinked`);
    const preexisting = await call("puerts_graph_inspect", { asset_path: BP });
    assert(preexisting.success === false,
      `reseed: the fixture path did not exist before this run ${preexisting.success ? "(IT DID: the reseed is not a reseed)" : ""}`);
    const built = await call("puerts_blueprint_build", FIXTURE);
    assert(built.success === true, `reseed: the fixture builds from nothing ${built.success ? "" : JSON.stringify(built.errors)}`);

    const before = await snapshot();
    const hashBefore = before.hash;
    assert(typeof hashBefore === "string" && hashBefore.length === 40,
      `reseed: graph_inspect reports a member structure hash (${hashBefore})`);
    assert(JSON.stringify(namesOf(before.data?.variables))
      === JSON.stringify(namesOf(FIXTURE.variables)),
      `reseed: the variable set is exactly the spec's (${namesOf(before.data?.variables).join(", ")})`);
    assert(JSON.stringify(namesOf(before.data?.components))
      === JSON.stringify(namesOf(FIXTURE.components)),
      `reseed: the component set is exactly the spec's (${namesOf(before.data?.components).join(", ")})`);
    assert((before.data?.functions ?? []).every((f) => f.name !== "ProbeStep"),
      "reseed: no function from an earlier run survived");
    assert((before.data?.event_dispatchers ?? []).every((d) => d.name !== "OnProbed"),
      "reseed: no event dispatcher from an earlier run survived");
    assert(Array.isArray(before.data?.event_dispatchers),
      "reseed: graph_inspect reports event_dispatchers");
    evidence.steps.fixture = {
      member_hash: hashBefore,
      variables: namesOf(before.data?.variables),
      components: namesOf(before.data?.components),
      file_sha256: before.sha,
      earlier_probe_files_unlinked: swept.length,
    };

    console.log("\n== plan_only classifies and touches nothing");
    const plan = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [
        { op: "add_variable", name: "Delta", type: { category: "float" }, default: 9 },
        { op: "add_variable", name: "Alpha", type: { category: "float" }, default: 1 },
      ],
      plan_only: true,
    });
    assert(plan.success === true, `plan_only: succeeds ${plan.success ? "" : JSON.stringify(plan.errors)}`);
    assert(plan.data?.plan_only === true, "plan_only: the response says it was a plan");
    assert((plan.data?.operations_to_apply ?? []).length === 1, "plan_only: exactly one operation would apply");
    assert((plan.data?.unchanged_operations ?? []).length === 1,
      "plan_only: the operation that is already true is classified unchanged, not applied");
    assert(plan.data?.expected_change_count === 1, "plan_only: expected_change_count is 1");
    assert(plan.data?.pre_member_hash === hashBefore,
      "plan_only: the plan's pre hash is the hash graph_inspect measured independently");
    assert(plan.data?.predicted_member_hash === undefined,
      "plan_only: no hash is predicted for a batch that changes something");
    const afterPlan = await snapshot();
    assert(afterPlan.hash === hashBefore, "plan_only: the members are unmoved");
    assert(afterPlan.sha === before.sha, "on-disk file hash: plan_only wrote nothing to disk");
    evidence.steps.plan = plan.data;

    console.log("\n== one batch across all five member kinds");
    const p4Before = p4Opened();
    const patchOps = [
      { op: "add_variable", name: "Delta", type: { category: "float" }, default: 9, category: "Probe" },
      { op: "set_variable_default", name: "Label", default: "patched" },
      { op: "remove_variable", name: "Doomed" },
      { op: "add_function", name: "ProbeStep", inputs: [{ name: "Amount", type: { category: "float" } }], outputs: [{ name: "Result", type: { category: "float" } }] },
      { op: "add_event_dispatcher", name: "OnProbed", parameters: [{ name: "Value", type: { category: "float" } }] },
      { op: "rename_component", from: "Lamp", to: "Beacon" },
      { op: "remove_component", name: "Muzzle" },
    ];
    const patch = await call("puerts_blueprint_member_patch", { asset_path: BP, operations: patchOps });
    assert(patch.success === true, `batch apply: the batch applies ${patch.success ? "" : JSON.stringify(patch.errors)}`);
    assert(patch.data?.applied_operation_count === 7, `batch apply: all seven operations applied (${patch.data?.applied_operation_count})`);
    assert(patch.data?.compile_status === "UpToDate", `batch apply: the patched Blueprint compiles (${patch.data?.compile_status})`);
    assert((patch.data?.verification_mismatches ?? []).length === 0, "batch apply: no verification mismatches");
    assert(patch.data?.pre_member_hash === hashBefore, "batch apply: the reported pre hash is the hash measured before");
    assert(patch.data?.post_member_hash !== hashBefore, "batch apply: the members actually changed");
    assert(patch.data?.saved === true, "batch apply: a batch that applied something saves");
    assert(typeof patch.data?.elapsed_ms === "number", `batch apply: the command reports its own elapsed time (${patch.data?.elapsed_ms} ms)`);
    evidence.steps.patch = {
      elapsed_ms: patch.data?.elapsed_ms,
      applied_operation_count: patch.data?.applied_operation_count,
      pre_member_hash: patch.data?.pre_member_hash,
      post_member_hash: patch.data?.post_member_hash,
    };

    console.log("\n== independent verification, and what did NOT move");
    const after = await snapshot();
    assert(after.hash === patch.data?.post_member_hash,
      "independent verification: a second reader agrees with the hash the patch reported");
    assert(varNamed(after.data, "Delta") !== undefined, "independent verification: the added variable is present");
    assert(varNamed(after.data, "Doomed") === undefined, "independent verification: the removed variable is gone");
    assert((after.data?.functions ?? []).some((f) => f.name === "ProbeStep"), "independent verification: the added function is present");
    assert((after.data?.event_dispatchers ?? []).some((d) => d.name === "OnProbed"),
      "independent verification: the added event dispatcher is present");
    assert((after.data?.components ?? []).some((c) => c.name === "Beacon"), "independent verification: the renamed component is present");
    assert(!(after.data?.components ?? []).some((c) => c.name === "Lamp"), "independent verification: the old component name is gone");
    assert(!(after.data?.components ?? []).some((c) => c.name === "Muzzle"), "independent verification: the removed component is gone");
    // default_value is the variable DESCRIPTION string, which is import text: a
    // string variable's is quoted. Substring, not equality, for that reason.
    assert(String(varNamed(after.data, "Label")?.default_value ?? "").includes("patched"),
      `independent verification: the changed default is the requested one (${JSON.stringify(varNamed(after.data, "Label")?.default_value)})`);

    // The claim the whole command rests on. Every member the batch did not name
    // is compared field for field, not merely counted.
    const untouched = ["Alpha", "Beta", "Gamma", "Count"];
    const drifted = untouched.filter((name) =>
      JSON.stringify(varNamed(before.data, name)) !== JSON.stringify(varNamed(after.data, name)));
    assert(drifted.length === 0,
      `unnamed members untouched: every variable the patch did not name is identical afterwards (${drifted.join(", ") || "none drifted"})`);
    const bodyBefore = (before.data?.components ?? []).find((c) => c.name === "Body");
    const bodyAfter = (after.data?.components ?? []).find((c) => c.name === "Body");
    assert(JSON.stringify(bodyBefore) === JSON.stringify(bodyAfter),
      "unnamed members untouched: a component the patch did not name is identical afterwards");
    assert((after.data?.graph?.nodes ?? []).length === (before.data?.graph?.nodes ?? []).length,
      "unnamed members untouched: the event graph node count is unchanged; a member patch is not a graph patch");
    evidence.steps.untouched_members_compared = untouched.length + 1;

    console.log("\n== repeated apply converges and does not save");
    const rerun = await call("puerts_blueprint_member_patch", { asset_path: BP, operations: patchOps });
    const afterRerun = await snapshot();
    assert(rerun.success === true, `repeated apply: the rerun succeeds ${rerun.success ? "" : JSON.stringify(rerun.errors)}`);
    assert(rerun.data?.applied_operation_count === 0, `repeated apply: the rerun applied nothing (${rerun.data?.applied_operation_count})`);
    assert((rerun.data?.unchanged_operations ?? []).length === 7, "repeated apply: all seven operations reported already satisfied");
    assert(rerun.data?.converged === true, "repeated apply: the rerun reports converged");
    assert(rerun.data?.saved === false, "repeated apply: a converged rerun does not save");
    assert(afterRerun.sha === after.sha, "on-disk file hash: the .uasset is byte-identical after the converged rerun");
    assert((rerun.data?.cleanup?.dirty_packages_after ?? []).length === 0,
      "dirty-package state: a converged rerun leaves no dirty package, so nothing was written to be saved later");
    evidence.steps.rerun = { applied: rerun.data?.applied_operation_count, converged: rerun.data?.converged };

    // Split out of the batch above deliberately. The second live run read
    // "rename_component Lamp -> Beacon was refused" as the command failing to
    // converge; it was the fixture, because a rebuild had put Lamp back beside
    // the Beacon a previous run created and BOTH names were then present, which
    // is a genuine collision. On a reseeded fixture only Beacon exists, and the
    // satisfied predicate for a rename is exactly "from absent and to present".
    // A red HERE, on a reseeded asset, would be the product defect that run
    // could not distinguish.
    console.log("\n== rename convergence: a rename whose result is already true");
    const renameAgain = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{ op: "rename_component", from: "Lamp", to: "Beacon" }],
    });
    assert(renameAgain.success === true,
      `rename convergence: a rename already carried out is not an error ${renameAgain.success ? "" : JSON.stringify(renameAgain.errors)}`);
    assert(renameAgain.data?.applied_operation_count === 0,
      `rename convergence: it applies nothing (${renameAgain.data?.applied_operation_count})`);
    assert(renameAgain.data?.converged === true, "rename convergence: it reports converged");
    assert(renameAgain.data?.saved === false, "rename convergence: it does not save");
    assert((await snapshot()).sha === after.sha, "on-disk file hash: the converged rename wrote nothing");

    console.log("\n== rename collision: a rename onto a name already taken");
    await refusal("rename collision", [{ op: "rename_component", from: "Body", to: "Root" }],
      /already taken by a different component/);

    console.log("\n== rename onto nothing: neither name is a component");
    await refusal("rename onto nothing", [{ op: "rename_component", from: "NoSuchPart", to: "AlsoNotAPart" }],
      /neither name is a component/);

    console.log("\n== type-incompatible default: an int given a word");
    // The control for the check below. FNumericProperty::ImportText_Internal
    // rejects a non-numeric, non-enum string for an INTEGER property, so if
    // validation is reached at all, this one is refused.
    await refusal("type-incompatible default (int)",
      [{ op: "set_variable_default", name: "Count", default: "not a number" }],
      /not a value|cannot hold/);

    console.log("\n== type-incompatible default: a float given a word");
    // PRODUCT DEFECT SUSPICION, and the one the first live run reported.
    // CompareVariableDefault decides "can this type hold this value" by whether
    // FProperty::ImportText returns non-null. For a FLOATING-POINT property that
    // test always passes: FNumericProperty::ImportText_Internal advances past
    // [+-.0-9] (zero characters here), calls SetNumericPropertyValueFromString
    // and returns a non-null pointer, so "not a number" imports as 0.0 and is
    // classified Different rather than Unavailable. The applier in
    // BPVariableOps::SetVariableDefault uses the same non-test, and the verifier
    // re-reads through the same comparator and agrees, so 0.0 is written,
    // verified and SAVED. If this check is red, it is not the fixture.
    const badFloat = await refusal("type-incompatible default (float)",
      [
        { op: "add_variable", name: "Zeta", type: { category: "float" }, default: 1 },
        { op: "set_variable_default", name: "Alpha", default: "not a number" },
      ],
      /not a value|cannot hold/);
    assert(varNamed(badFloat.after.data, "Zeta") === undefined,
      "rollback after a failed batch: the valid operation beside the bad default was not applied");
    assert(JSON.stringify(varNamed(badFloat.after.data, "Alpha"))
      === JSON.stringify(varNamed(before.data, "Alpha")),
      `type-incompatible default (float): Alpha still holds its original default (${JSON.stringify(varNamed(badFloat.after.data, "Alpha")?.default_value)})`);

    console.log("\n== retype: add_variable onto an existing variable of another type");
    await refusal("retype", [{ op: "add_variable", name: "Alpha", type: { category: "int" } }],
      /change-variable-type/);

    console.log("\n== inherited property: not this command's to remove");
    await refusal("inherited property", [{ op: "remove_variable", name: "bHidden" }],
      /not declared on this Blueprint/);

    console.log("\n== one member named twice in one batch has no desired state");
    await refusal("duplicate member", [
      { op: "set_variable_default", name: "Label", default: "one" },
      { op: "set_variable_default", name: "Label", default: "two" },
    ], /second operation in this batch/);

    console.log("\n== rollback after a failed batch: refused during validation");
    // The first operation is valid and the second is not. A batch that applied
    // the first and then stopped would be the half-applied state this command
    // exists to make impossible, and here it is expensive: the first operation
    // would already have recompiled the Blueprint. This pair is caught in
    // validation, so no rollback should be needed at all.
    const refusedEarly = await refusal("rollback after a failed batch (validation)", [
      { op: "add_variable", name: "Epsilon", type: { category: "float" }, default: 5 },
      { op: "add_interface", path: "/Script/Engine.NoSuchInterfaceAnywhere" },
    ], /no class loaded/);
    assert(varNamed(refusedEarly.after.data, "Epsilon") === undefined,
      "rollback after a failed batch (validation): the variable the valid operation would have added is absent");
    assert(!/failed inside the mutator|rolled back/.test(JSON.stringify(refusedEarly.result.errors ?? [])),
      "rollback after a failed batch (validation): the refusal came before any mutation, so nothing was rolled back");

    console.log("\n== rollback after a failed batch: failed during apply");
    // Validation does not check add_variable against inherited names, but
    // FBlueprintEditorUtils::AddMemberVariable does: GetClassVariableList
    // includes the parent's properties and AActor declares bHidden, so this
    // batch passes classification and fails at the mutation. That is the only
    // shape that exercises the rollback path rather than the refusal path, and
    // the operation before it has already recompiled the Blueprint by then.
    const rolledBack = await refusal("rollback after a failed batch (apply)", [
      { op: "add_variable", name: "Eta", type: { category: "float" }, default: 6 },
      { op: "add_variable", name: "bHidden", type: { category: "bool" } },
    ], /failed inside the mutator/);
    assert(varNamed(rolledBack.after.data, "Eta") === undefined,
      "rollback after a failed batch (apply): the operation that DID apply was rolled back out");
    assert(varNamed(rolledBack.after.data, "bHidden") === undefined,
      "rollback after a failed batch (apply): the operation that failed left nothing behind");
    // A failed request returns the failure envelope, whose data is empty, so
    // rollback_attempted and rollback_succeeded are unreadable from here. What
    // IS readable is that the error text does not say "Treat this asset as
    // damaged" (asserted in refusal()) and that the members came back, above.
    evidence.steps.rollback = { errors: rolledBack.result.errors ?? [] };

    console.log("\n== dirty-package state and source-control quiescence");
    // Last, deliberately: this converged call reports the dirty-package set the
    // whole run of refusals above left behind. A refusal that dirtied the
    // package without saving it shows up here and nowhere else, because a failed
    // request returns an empty data and carries no cleanup evidence of its own.
    const quiescence = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{ op: "set_variable_default", name: "Label", default: "patched" }],
    });
    assert(quiescence.success === true, "dirty-package state: a converged single-operation batch succeeds");
    assert(quiescence.data?.converged === true, "dirty-package state: it reports converged");
    assert((quiescence.data?.cleanup?.dirty_packages_after ?? []).length === 0,
      `dirty-package state: no dirty package remains after every refusal above (${JSON.stringify(quiescence.data?.cleanup?.dirty_packages_after ?? [])})`);
    const p4After = p4Opened();
    if (p4Before !== null && p4After !== null) {
      assert(p4Before === p4After, "source-control quiescence: no source-control entry appeared");
      evidence.steps.source_control_quiescent = true;
    } else {
      console.log("  INFO  p4 unavailable; source-control quiescence not measured");
      evidence.steps.source_control_quiescent = null;
    }

    console.log("\n== the asset the refusals leave behind is the asset the patch left");
    const final = await snapshot();
    assert(final.hash === patch.data?.post_member_hash,
      `no residue: the member hash is still the patch's (${final.hash} vs ${patch.data?.post_member_hash})`);
    assert(final.sha === after.sha,
      "on-disk file hash: the .uasset is byte-identical to the one the patch saved");

    // Recorded from the FINAL read, not a mid-script one, so the cold phase
    // checks what is actually on disk when the warm phase ends.
    evidence.post_patch_member_hash = final.hash;
    evidence.post_patch_file_sha256 = final.sha;
    evidence.post_patch_variables = namesOf(final.data?.variables);
    evidence.post_patch_components = namesOf(final.data?.components);
    evidence.post_patch_functions = namesOf(final.data?.functions);
    evidence.post_patch_dispatchers = namesOf(final.data?.event_dispatchers);
  }

  evidence.mcp_round_trips = roundTrips;

  mkdirSync(evidenceDir, { recursive: true });
  writeFileSync(join(evidenceDir, `bp-member-patch${phase === "cold" ? "-cold" : ""}.json`),
    JSON.stringify(evidence, null, 2) + "\n");
  console.log(`\nevidence written; ${roundTrips} MCP round trips this phase`);
  console.log(failures.length === 0 ? `all checks passed (${phase} phase)` : `${failures.length} check(s) did not hold:`);
  for (const f of failures) console.log(`  - ${f}`);
  process.exit(failures.length === 0 ? 0 : 1);
} catch (error) {
  console.error(`\nbp-member-patch: ${error.message}`);
  process.exit(1);
} finally {
  child.kill();
}
