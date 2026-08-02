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
// NOT RUN by the lane that wrote it: no editor was launched. Running it is the
// integrator's step, and puerts_blueprint_member_patch stays at verification
// "implemented" with an empty live_evidence array until it passes.

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
const BP = "/Game/MCPGenerated/BP_MemberProbe";
const contentFile = (p) =>
  join(projectRoot, "Content", p.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");
function p4Opened() {
  try {
    return execSync("p4 opened", { cwd: join(projectRoot, "Content"), encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
  } catch { return null; }
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

const evidence = { phase, steps: {} };
const memberHash = (d) => d?.member_structure_hash_sha1;
const namesOf = (list) => (list ?? []).map((e) => e.name).sort();
const varNamed = (d, name) => (d?.variables ?? []).find((v) => v.name === name);

try {
  await send("initialize", {
    protocolVersion: "2024-11-05", capabilities: {},
    clientInfo: { name: "bp-member-patch", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    console.log("\ncold phase: the patched members survive a restart");
    const recorded = JSON.parse(readFileSync(join(repoRoot, "docs", "evidence", "bp-member-patch.json"), "utf8"));
    const read = await call("puerts_graph_inspect", { asset_path: BP });
    assert(read.success === true, "(13) the cold-loaded Blueprint inspects");
    assert(memberHash(read.data) === recorded.post_patch_member_hash,
      `(13) the member hash is the one the patch left (${memberHash(read.data)} vs ${recorded.post_patch_member_hash})`);
    assert(JSON.stringify(namesOf(read.data?.variables)) === JSON.stringify(recorded.post_patch_variables),
      "(13) the variable set is the one the patch left");
    assert(JSON.stringify(namesOf(read.data?.components)) === JSON.stringify(recorded.post_patch_components),
      "(13) the component set is the one the patch left");
    assert(JSON.stringify(namesOf(read.data?.functions)) === JSON.stringify(recorded.post_patch_functions),
      "(13) the function set is the one the patch left");
    assert(JSON.stringify(namesOf(read.data?.event_dispatchers)) === JSON.stringify(recorded.post_patch_dispatchers),
      "(13) the event dispatcher set is the one the patch left");
  } else {
    console.log("\n-- 1. a fixture with more members than the patch touches");
    const built = await call("puerts_blueprint_build", FIXTURE);
    assert(built.success === true, `(1) the fixture builds ${built.success ? "" : JSON.stringify(built.errors)}`);
    const before = await call("puerts_graph_inspect", { asset_path: BP });
    assert(before.success === true, "(1) the fixture inspects");
    const hashBefore = memberHash(before.data);
    assert(typeof hashBefore === "string" && hashBefore.length === 40,
      `(1) graph_inspect reports a member structure hash (${hashBefore})`);
    assert((before.data?.variables ?? []).length >= 5, "(1) the fixture has at least five variables");
    assert((before.data?.components ?? []).length >= 4, "(1) the fixture has at least four components");
    assert(Array.isArray(before.data?.event_dispatchers),
      "(1) graph_inspect reports event_dispatchers");
    const shaBefore = fileSha(contentFile(BP));
    evidence.steps.fixture = {
      member_hash: hashBefore,
      variables: namesOf(before.data?.variables),
      components: namesOf(before.data?.components),
      file_sha256: shaBefore,
    };

    console.log("\n-- 2. plan_only classifies and touches nothing");
    const planOps = [
      { op: "add_variable", name: "Delta", type: { category: "float" }, default: 9 },
      { op: "add_variable", name: "Alpha", type: { category: "float" }, default: 1 },
    ];
    const plan = await call("puerts_blueprint_member_patch", { asset_path: BP, operations: planOps, plan_only: true });
    assert(plan.success === true, `(2) plan_only succeeds ${plan.success ? "" : JSON.stringify(plan.errors)}`);
    assert(plan.data?.plan_only === true, "(2) the response says it was a plan");
    assert((plan.data?.operations_to_apply ?? []).length === 1, "(2) exactly one operation would apply");
    assert((plan.data?.unchanged_operations ?? []).length === 1,
      "(2) the operation that is already true is classified unchanged, not applied");
    assert(plan.data?.expected_change_count === 1, "(2) expected_change_count is 1");
    assert(plan.data?.pre_member_hash === hashBefore,
      "(2) the plan's pre hash is the hash graph_inspect measured independently");
    assert(plan.data?.predicted_member_hash === undefined,
      "(2) no hash is predicted for a batch that changes something");
    const afterPlan = await call("puerts_graph_inspect", { asset_path: BP });
    assert(memberHash(afterPlan.data) === hashBefore, "(2) plan_only changed nothing");
    assert(fileSha(contentFile(BP)) === shaBefore, "(2) plan_only wrote nothing to disk");
    evidence.steps.plan = plan.data;

    console.log("\n-- 3. one batch across all five member kinds");
    const p4Before = p4Opened();
    const patch = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [
        { op: "add_variable", name: "Delta", type: { category: "float" }, default: 9, category: "Probe" },
        { op: "set_variable_default", name: "Label", default: "patched" },
        { op: "remove_variable", name: "Doomed" },
        { op: "add_function", name: "ProbeStep", inputs: [{ name: "Amount", type: { category: "float" } }], outputs: [{ name: "Result", type: { category: "float" } }] },
        { op: "add_event_dispatcher", name: "OnProbed", parameters: [{ name: "Value", type: { category: "float" } }] },
        { op: "rename_component", from: "Lamp", to: "Beacon" },
        { op: "remove_component", name: "Muzzle" },
      ],
    });
    assert(patch.success === true, `(3) the batch applies ${patch.success ? "" : JSON.stringify(patch.errors)}`);
    assert(patch.data?.applied_operation_count === 7, `(3) all seven operations applied (${patch.data?.applied_operation_count})`);
    assert(patch.data?.compile_status === "UpToDate", `(3) the patched Blueprint compiles (${patch.data?.compile_status})`);
    assert((patch.data?.verification_mismatches ?? []).length === 0, "(3) no verification mismatches");
    assert(patch.data?.pre_member_hash === hashBefore, "(3) the reported pre hash is the hash measured before");
    assert(patch.data?.post_member_hash !== hashBefore, "(3) the members actually changed");
    assert(patch.data?.saved === true, "(3) a batch that applied something saves");
    assert(typeof patch.data?.elapsed_ms === "number", `(14) the command reports its own elapsed time (${patch.data?.elapsed_ms} ms)`);
    evidence.steps.patch = {
      elapsed_ms: patch.data?.elapsed_ms,
      applied_operation_count: patch.data?.applied_operation_count,
      pre_member_hash: patch.data?.pre_member_hash,
      post_member_hash: patch.data?.post_member_hash,
    };

    console.log("\n-- 4. independent verification, and what did NOT move");
    const after = await call("puerts_graph_inspect", { asset_path: BP });
    assert(memberHash(after.data) === patch.data?.post_member_hash,
      "(4) an independent read agrees with the hash the patch reported");
    assert(varNamed(after.data, "Delta") !== undefined, "(4) the added variable is present");
    assert(varNamed(after.data, "Doomed") === undefined, "(4) the removed variable is gone");
    assert((after.data?.functions ?? []).some((f) => f.name === "ProbeStep"), "(4) the added function is present");
    assert((after.data?.event_dispatchers ?? []).some((d) => d.name === "OnProbed"),
      "(4) the added event dispatcher is present");
    assert((after.data?.components ?? []).some((c) => c.name === "Beacon"), "(4) the renamed component is present");
    assert(!(after.data?.components ?? []).some((c) => c.name === "Lamp"), "(4) the old component name is gone");
    assert(!(after.data?.components ?? []).some((c) => c.name === "Muzzle"), "(4) the removed component is gone");

    // The claim the whole command rests on. Every member the batch did not name
    // is compared field for field, not merely counted.
    const untouched = ["Alpha", "Beta", "Gamma"];
    const drifted = untouched.filter((name) =>
      JSON.stringify(varNamed(before.data, name)) !== JSON.stringify(varNamed(after.data, name)));
    assert(drifted.length === 0,
      `(5) every variable the patch did not name is identical afterwards (${drifted.join(", ") || "none drifted"})`);
    const bodyBefore = (before.data?.components ?? []).find((c) => c.name === "Body");
    const bodyAfter = (after.data?.components ?? []).find((c) => c.name === "Body");
    assert(JSON.stringify(bodyBefore) === JSON.stringify(bodyAfter),
      "(5) a component the patch did not name is identical afterwards");
    assert((after.data?.graph?.nodes ?? []).length === (before.data?.graph?.nodes ?? []).length,
      "(5) the event graph node count is unchanged: a member patch is not a graph patch");
    evidence.steps.untouched_members_compared = untouched.length + 1;

    console.log("\n-- 5. rerunning the same batch is a no-op");
    const shaAfterPatch = fileSha(contentFile(BP));
    const rerun = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [
        { op: "add_variable", name: "Delta", type: { category: "float" }, default: 9, category: "Probe" },
        { op: "set_variable_default", name: "Label", default: "patched" },
        { op: "remove_variable", name: "Doomed" },
        { op: "add_function", name: "ProbeStep" },
        { op: "add_event_dispatcher", name: "OnProbed" },
        { op: "rename_component", from: "Lamp", to: "Beacon" },
        { op: "remove_component", name: "Muzzle" },
      ],
    });
    assert(rerun.success === true, `(6) the rerun succeeds ${rerun.success ? "" : JSON.stringify(rerun.errors)}`);
    assert(rerun.data?.applied_operation_count === 0, `(6) the rerun applied nothing (${rerun.data?.applied_operation_count})`);
    assert((rerun.data?.unchanged_operations ?? []).length === 7, "(6) all seven operations reported already satisfied");
    assert(rerun.data?.converged === true, "(6) the rerun reports converged");
    assert(rerun.data?.saved === false, "(6) a converged rerun does not save");
    assert(fileSha(contentFile(BP)) === shaAfterPatch, "(6) the file on disk is untouched by the rerun");
    assert((rerun.data?.cleanup?.dirty_packages_after ?? []).length === 0,
      "(6) a converged rerun leaves no dirty package: nothing was written to be saved later");
    evidence.steps.rerun = { applied: rerun.data?.applied_operation_count, converged: rerun.data?.converged };

    console.log("\n-- 6. a retype is refused by name rather than silently accepted");
    const retype = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{ op: "add_variable", name: "Alpha", type: { category: "int" } }],
    });
    assert(retype.success === false, "(7) add_variable onto a different type fails the request");
    assert(/change-variable-type/.test(JSON.stringify(retype.errors ?? [])),
      `(7) the refusal names the missing primitive (got: ${JSON.stringify(retype.errors ?? []).slice(0, 200)})`);
    const afterRetype = await call("puerts_graph_inspect", { asset_path: BP });
    assert(memberHash(afterRetype.data) === memberHash(after.data), "(7) nothing changed");
    assert(fileSha(contentFile(BP)) === shaAfterPatch, "(7) nothing reached disk");

    console.log("\n-- 7. an inherited property is not this command's to remove");
    const inherited = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{ op: "remove_variable", name: "bHidden" }],
    });
    assert(inherited.success === false, "(8) removing an inherited property fails the request");
    assert(/not declared on this Blueprint/.test(JSON.stringify(inherited.errors ?? [])),
      "(8) the refusal says why");
    assert(fileSha(contentFile(BP)) === shaAfterPatch, "(8) nothing reached disk");

    console.log("\n-- 8. a batch that names one member twice has no desired state");
    const doubled = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [
        { op: "set_variable_default", name: "Label", default: "one" },
        { op: "set_variable_default", name: "Label", default: "two" },
      ],
    });
    assert(doubled.success === false, "(9) two operations on one member fail the request");
    assert(fileSha(contentFile(BP)) === shaAfterPatch, "(9) neither was applied");

    console.log("\n-- 9. a batch that fails validation leaves the Blueprint exactly as it was");
    // The first operation is valid and the second is not. A batch that applied
    // the first and then stopped would be the half-applied state this command
    // exists to make impossible, and here it is expensive: the first operation
    // would already have recompiled the Blueprint.
    const partial = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [
        { op: "add_variable", name: "Epsilon", type: { category: "float" }, default: 5 },
        { op: "add_interface", path: "/Script/Engine.NoSuchInterfaceAnywhere" },
      ],
    });
    assert(partial.success === false, "(10) a batch with an unresolvable target fails");
    const afterPartial = await call("puerts_graph_inspect", { asset_path: BP });
    assert(memberHash(afterPartial.data) === memberHash(after.data),
      "(10) the valid operation in the failed batch was NOT applied");
    assert(varNamed(afterPartial.data, "Epsilon") === undefined, "(10) the variable it would have added is absent");
    assert(fileSha(contentFile(BP)) === shaAfterPatch, "(10) the file on disk is unchanged");

    console.log("\n-- 10. a value the variable's type cannot hold is refused, not written");
    // Validation is the primary guard, and this is the case it exists for: the
    // desired value is imported into a scratch value of the property's own type
    // before anything is mutated, so an unparseable default is a refusal rather
    // than a failed write halfway through a batch.
    const badValue = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [
        { op: "add_variable", name: "Zeta", type: { category: "float" }, default: 1 },
        { op: "set_variable_default", name: "Alpha", default: "not a number" },
      ],
    });
    assert(badValue.success === false, "(11) a default the type cannot hold fails the request");
    const afterBad = await call("puerts_graph_inspect", { asset_path: BP });
    assert(memberHash(afterBad.data) === memberHash(after.data),
      "(11) the members are the ones the batch found");
    assert(varNamed(afterBad.data, "Zeta") === undefined, "(11) the valid operation beside it was not applied");
    assert(fileSha(contentFile(BP)) === shaAfterPatch, "(11) nothing reached disk");
    // Honest reporting, not just a refusal: a rollback that did not restore the
    // asset says so in the error text. This asserts the batch never got that far.
    assert(!/Treat this asset as damaged/.test(JSON.stringify(badValue.errors ?? [])),
      "(11) the refusal happened during validation, so no rollback was needed");

    console.log("\n-- 11. nothing dirty, nothing checked out");
    const quiescence = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [{ op: "set_variable_default", name: "Label", default: "patched" }],
    });
    assert(quiescence.success === true, "(12) a converged single-operation batch succeeds");
    assert((quiescence.data?.cleanup?.dirty_packages_after ?? []).length === 0,
      "(12) no dirty package remains");
    const p4After = p4Opened();
    if (p4Before !== null && p4After !== null) {
      assert(p4Before === p4After, "(12) no source-control entry appeared");
      evidence.steps.source_control_quiescent = true;
    } else {
      console.log("  INFO  p4 unavailable; source-control quiescence not measured");
      evidence.steps.source_control_quiescent = null;
    }

    evidence.post_patch_member_hash = memberHash(after.data);
    evidence.post_patch_variables = namesOf(after.data?.variables);
    evidence.post_patch_components = namesOf(after.data?.components);
    evidence.post_patch_functions = namesOf(after.data?.functions);
    evidence.post_patch_dispatchers = namesOf(after.data?.event_dispatchers);
  }

  evidence.mcp_round_trips = roundTrips;

  mkdirSync(join(repoRoot, "docs", "evidence"), { recursive: true });
  writeFileSync(join(repoRoot, "docs", "evidence", `bp-member-patch${phase === "cold" ? "-cold" : ""}.json`),
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
