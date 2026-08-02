#!/usr/bin/env node
// Live acceptance for blueprint_build remove_unlisted (variables scope).
//
// remove_unlisted deletes work, so the safety properties are the point and are
// asserted before the happy path: a variable this builder did not declare is
// never removable, and a referenced variable is blocked unless the caller says
// otherwise in as many words.
//
// Ownership is an explicit mark, not a heuristic. blueprint_build stamps
// MCPManaged=1 on every variable it declares; removal only considers stamped
// variables. Inherited, native C++, engine-generated and human-authored
// variables carry no stamp, so the protection list is satisfied by
// construction rather than by enumeration.
//
//   --phase=record  build the fixture, plan, block, force, converge, rerun
//   --phase=cold    after a restart, the converged set is still converged
import { execSync, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
const projectRoot = process.env.MCP_UNREAL_PROJECT_ROOT;
if (!projectRoot) throw new Error("Set MCP_UNREAL_PROJECT_ROOT to the test project root");
delete process.env.MCP_PUERTS_PIPE;
delete process.env.MCP_PUERTS_TOKEN_PATH;
delete process.env.MCP_PUERTS_TOKEN;
const phase = process.argv.includes("--phase=cold") ? "cold" : "record";

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
function canonicalize(v) {
  if (Array.isArray(v)) return v.map(canonicalize);
  if (v !== null && typeof v === "object") {
    const o = {}; for (const k of Object.keys(v).sort()) o[k] = canonicalize(v[k]); return o;
  }
  return v;
}
const fileSha = (p) => createHash("sha256").update(readFileSync(p)).digest("hex");
const jsonHash = (v) => createHash("sha256").update(JSON.stringify(canonicalize(v))).digest("hex");

const BP = "/Game/MCPGenerated/BP_ConvergeProbe";
const contentFile = (p) =>
  join(projectRoot, "Content", p.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");
function p4Opened() {
  try {
    return execSync("p4 opened", { cwd: join(projectRoot, "Content"), encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
  } catch { return null; }
}

// Six managed variables. KeptA and KeptB are referenced by graph nodes, so the
// fixture exercises both the blocked path and the forced path.
const SIX = [
  { name: "KeptA", type: "float", default: 1 },
  { name: "KeptB", type: "int", default: 2 },
  { name: "KeptC", type: "bool", default: false },
  { name: "DropPlain", type: "float", default: 4 },
  { name: "DropAlsoPlain", type: "int", default: 5 },
  { name: "DropReferenced", type: "float", default: 6 },
];
// The graph references KeptA (kept) and DropReferenced (removal must be blocked).
// The factory config key is varName, not "variable". A wrong key makes the
// factory return null while the build report still counts the spec entry
// (findings defect 0), so a fixture built on the wrong key silently has no
// reference nodes at all and every reference assertion below passes vacuously.
// This was caught by comparing the build's node_types against graph_inspect.
const GRAPH = {
  nodes: [
    { id: "begin", type: "BeginPlay" },
    { id: "getA", type: "VariableGet", params: { varName: "KeptA" } },
    { id: "getRef", type: "VariableGet", params: { varName: "DropReferenced" } },
    { id: "say", type: "PrintString" },
  ],
  connections: [{ from: "begin.then", to: "say.exec" }],
};
// Applied with the three-variable spec. Removal runs before the graph is
// rebuilt, so a post-convergence spec must not re-declare a node that reads a
// variable it just asked to remove.
const GRAPH_AFTER = {
  nodes: [
    { id: "begin", type: "BeginPlay" },
    { id: "getA", type: "VariableGet", params: { varName: "KeptA" } },
    { id: "say", type: "PrintString" },
  ],
  connections: [{ from: "begin.then", to: "say.exec" }],
};
const DESIRED_THREE = [
  { name: "KeptA", type: "float", default: 1 },
  { name: "KeptB", type: "int", default: 2 },
  { name: "KeptC", type: "bool", default: false },
];

const evidence = { phase, steps: {} };
const names = (a) => (a ?? []).slice().sort();

try {
  await send("initialize", { protocolVersion: "2024-11-05", capabilities: {}, clientInfo: { name: "bp-remove-unlisted", version: "1.0.0" } });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    console.log("\ncold phase: the converged Blueprint is still converged after a restart");
    const recorded = JSON.parse(readFileSync(join(repoRoot, "docs", "evidence", "bp-remove-unlisted.json"), "utf8"));
    const read = await call("puerts_graph_inspect", { asset_path: BP });
    assert(read.success === true, "(14) the cold-loaded Blueprint inspects");
    const vars = names((read.data?.variables ?? []).map((v) => v.name));
    assert(JSON.stringify(vars) === JSON.stringify(recorded.final_variables),
      "(14) the cold-loaded variable set matches the converged set");
    const rerun = await call("puerts_blueprint_build", {
      asset_path: BP, parent_class: "Actor", variables: DESIRED_THREE, graph: GRAPH_AFTER,
      remove_unlisted: { variables: true },
    });
    assert(rerun.success === true, "(14) the same spec still applies after a restart");
    assert(rerun.data?.convergence?.removed_variables?.length === 0,
      "(14) a cold rerun removes nothing further");
    assert(rerun.data?.convergence?.converged === true, "(14) still converged");
  } else {
    console.log("\n-- 1. build the fixture: six managed variables plus a graph");
    const built = await call("puerts_blueprint_build", {
      asset_path: BP, parent_class: "Actor", variables: SIX, graph: GRAPH,
    });
    assert(built.success === true, "(1) the fixture builds");
    assert(built.data?.compile_status === "UpToDate", "(1) the fixture compiles");
    const afterBuild = await call("puerts_graph_inspect", { asset_path: BP });
    const builtVars = names((afterBuild.data?.variables ?? []).map((v) => v.name));
    assert(builtVars.length >= 6, `(1) at least six variables exist (${builtVars.length})`);
    assert(SIX.every((v) => builtVars.includes(v.name)), "(1) all six managed variables are present");

    console.log("\n-- 2. unsupported scopes are rejected, not ignored");
    for (const scope of ["components", "functions", "macros", "graph_nodes", "interfaces"]) {
      const rejected = await call("puerts_blueprint_build", {
        asset_path: BP, parent_class: "Actor", variables: DESIRED_THREE,
        remove_unlisted: { [scope]: true },
      });
      assert(rejected.success === false
        && JSON.stringify(rejected.errors).includes("unsupported_scope"),
        `(2) remove_unlisted.${scope}=true is rejected with unsupported_scope`);
    }
    const falseIsFine = await call("puerts_blueprint_build", {
      asset_path: BP, parent_class: "Actor", variables: SIX, graph: GRAPH,
      remove_unlisted: { components: false, functions: false }, plan_only: true,
    });
    assert(falseIsFine.success === true, "(2) an unsupported scope set false is accepted");

    console.log("\n-- 3. plan_only returns the exact plan and changes nothing");
    const beforePlan = await call("puerts_graph_inspect", { asset_path: BP });
    const plan = await call("puerts_blueprint_build", {
      asset_path: BP, parent_class: "Actor", variables: DESIRED_THREE, graph: GRAPH_AFTER,
      remove_unlisted: { variables: true }, plan_only: true,
    });
    evidence.steps.plan = plan.data;
    assert(plan.success === true, "(4) plan_only succeeds");
    assert(names(plan.data?.variables_to_remove).join() === "DropAlsoPlain,DropPlain",
      `(4) the plan removes exactly the two unreferenced managed variables (got ${names(plan.data?.variables_to_remove)})`);
    assert((plan.data?.blocked_removals ?? []).some((b) => b.startsWith("DropReferenced")),
      "(4) DropReferenced is blocked, not planned for removal");
    assert(Object.keys(plan.data?.referenced_variables ?? {}).includes("DropReferenced"),
      "(4) the plan reports where DropReferenced is referenced");
    assert(names(plan.data?.variables_to_update).join() === "KeptA,KeptB,KeptC",
      "(4) the three desired variables are updates, not adds");
    assert(typeof plan.data?.expected_change_count === "number",
      "(4) expected_change_count is reported");
    const afterPlan = await call("puerts_graph_inspect", { asset_path: BP });
    assert(jsonHash(beforePlan.data) === jsonHash(afterPlan.data),
      "(4) plan_only changed nothing: inspection is byte-stable across it");
    assert(plan.transaction_id === "" || plan.data?.plan_only === true,
      "(4) plan mode is read-only");

    console.log("\n-- 4. apply without force: referenced removals are blocked");
    const p4Before = p4Opened();
    const noForce = await call("puerts_blueprint_build", {
      asset_path: BP, parent_class: "Actor", variables: DESIRED_THREE, graph: GRAPH,
      remove_unlisted: { variables: true },
    });
    evidence.steps.no_force = noForce.data?.convergence;
    assert(noForce.success === true, "(5) the unforced apply succeeds");
    assert(names(noForce.data?.convergence?.removed_variables).join() === "DropAlsoPlain,DropPlain",
      "(5) only the unreferenced managed variables were removed");
    const midVars = names(((await call("puerts_graph_inspect", { asset_path: BP })).data?.variables ?? []).map((v) => v.name));
    assert(midVars.includes("DropReferenced"),
      "(5) the referenced variable survived the unforced apply");
    assert((noForce.data?.convergence?.removed_reference_nodes ?? []).length === 0,
      "(5) no graph node was deleted without force");

    console.log("\n-- 5. apply with force: the referenced variable and its nodes go");
    const forced = await call("puerts_blueprint_build", {
      asset_path: BP, parent_class: "Actor", variables: DESIRED_THREE,
      remove_unlisted: { variables: true }, force_remove_referenced: true,
    });
    evidence.steps.forced = forced.data?.convergence;
    assert(forced.success === true, "(6) the forced apply succeeds");
    assert(names(forced.data?.convergence?.removed_variables).includes("DropReferenced"),
      "(6) the referenced managed variable was removed under force");
    assert((forced.data?.convergence?.removed_reference_nodes ?? []).length > 0,
      "(6) every deleted reference node is reported");
    assert(forced.data?.compile_status === "UpToDate", "(6) the Blueprint still compiles after removal");

    console.log("\n-- 6. independent inspection proves the final set");
    const finalRead = await call("puerts_graph_inspect", { asset_path: BP });
    const finalVars = names((finalRead.data?.variables ?? []).map((v) => v.name));
    evidence.steps.final_variables = finalVars;
    assert(JSON.stringify(finalVars) === JSON.stringify(["KeptA", "KeptB", "KeptC"]),
      `(7)(8) the final variable set is exactly the desired managed set (got ${finalVars})`);
    assert(forced.data?.convergence?.converged === true, "(8) the command reports converged");
    // KeptA's reference node must survive: it is still in the desired set.
    const nodeTypes = (finalRead.data?.graph?.nodes ?? []).map((n) => n.type);
    assert(nodeTypes.filter((t) => t === "VariableGet").length === 1,
      "(11) only the reference to the removed variable was deleted; KeptA's node survives");
    assert(nodeTypes.includes("BeginPlay") && nodeTypes.includes("PrintString"),
      "(11) unrelated graph nodes are untouched");

    console.log("\n-- 7. convergence: the same spec twice changes nothing");
    const beforeRerun = await call("puerts_graph_inspect", { asset_path: BP });
    const rerun = await call("puerts_blueprint_build", {
      asset_path: BP, parent_class: "Actor", variables: DESIRED_THREE, graph: GRAPH_AFTER,
      remove_unlisted: { variables: true },
    });
    assert(rerun.success === true, "(9) the rerun succeeds");
    assert((rerun.data?.convergence?.removed_variables ?? []).length === 0,
      "(10) the rerun removes nothing");
    const afterRerun = await call("puerts_graph_inspect", { asset_path: BP });
    assert(jsonHash(beforeRerun.data?.variables) === jsonHash(afterRerun.data?.variables),
      "(10) the inspected variable set is byte-stable across the rerun");

    // ---- 8. rollback restores removals when the build fails after them ----
    //
    // The failure lever is an unresolved connection, not a compile error. A
    // compile error cannot be reached from the current spec vocabulary: the
    // validator, the node factories and the connection resolver all catch
    // first, and probes that tried to force one (a node reading a
    // just-removed variable, a Cast with nothing wired to its Object pin)
    // both compiled UpToDate. That is worth knowing on its own.
    //
    // It proves the same thing regardless. Removal happens at step 3 of the
    // command; compile failure and unresolved connections both land in the
    // same Errors array and both exit through the same FailRolledBack, which
    // cancels the transaction and then rolls back. Proving the path with the
    // reachable trigger proves it for the unreachable one.
    console.log("\n-- 8. a failure after removal rolls the removals back");
    const seedForRollback = await call("puerts_blueprint_build", {
      asset_path: BP, parent_class: "Actor",
      variables: [...DESIRED_THREE, { name: "RollbackVictim", type: "float", default: 9 }],
      graph: {
        nodes: [
          { id: "begin", type: "BeginPlay" },
          { id: "getA", type: "VariableGet", params: { varName: "KeptA" } },
          { id: "getVictim", type: "VariableGet", params: { varName: "RollbackVictim" } },
          { id: "say", type: "PrintString" },
        ],
        connections: [{ from: "begin.then", to: "say.exec" }],
      },
    });
    assert(seedForRollback.success === true, "(12) the rollback fixture seeds");
    const shaBeforeFailure = fileSha(contentFile(BP));
    const beforeFailure = await call("puerts_graph_inspect", { asset_path: BP });
    const beforeVars = names((beforeFailure.data?.variables ?? []).map((v) => v.name));
    assert(beforeVars.includes("RollbackVictim"), "(12) the victim variable exists before the failed build");

    // The spec below omits RollbackVictim AND omits the node that reads it.
    // Restoration must therefore come from the ledger, not from the request:
    // rebuilding the graph from this spec would bring the variable back and
    // silently drop its reader.
    const failed = await call("puerts_blueprint_build", {
      asset_path: BP, parent_class: "Actor", variables: DESIRED_THREE,
      remove_unlisted: { variables: true }, force_remove_referenced: true,
      // begin has no pin called nosuchpin: the connection cannot be resolved,
      // so the build fails AFTER the removal pass has already run.
      graph: {
        nodes: [{ id: "begin", type: "BeginPlay" }, { id: "say", type: "PrintString" }],
        connections: [{ from: "begin.nosuchpin", to: "say.exec" }],
      },
    });
    evidence.steps.failed_build = { success: failed.success, cleanup: failed.data?.cleanup };
    assert(failed.success === false, "(12) the build fails after the removal pass");

    const afterFailure = await call("puerts_graph_inspect", { asset_path: BP });
    const afterVars = names((afterFailure.data?.variables ?? []).map((v) => v.name));
    assert(afterVars.includes("RollbackVictim"),
      `(12) the removed variable was RESTORED by rollback (got ${afterVars})`);
    assert(JSON.stringify(afterVars) === JSON.stringify(beforeVars),
      "(12) the whole variable set is exactly what it was before the failed build");
    // The variable set is the claim under test. The full inspect payload also
    // covers graph node object names, which the failed build regenerated when
    // it rebuilt the graph from the spec; that is not damage, and the file on
    // disk is checked separately below.
    assert(jsonHash(beforeFailure.data?.variables) === jsonHash(afterFailure.data?.variables),
      "(12) the variable set is byte-identical to before the failed build");
    assert(fileSha(contentFile(BP)) === shaBeforeFailure,
      "(8) the asset file on disk is byte-identical after the failed build");
    const ledger = failed.data?.cleanup?.removal_ledger ?? {};
    assert(ledger.reference_nodes_captured > 0,
      `(8) the reference node was captured before deletion (${ledger.reference_nodes_captured})`);
    assert(ledger.reference_nodes_recreated === ledger.reference_nodes_captured,
      `(8) every captured reference node was recreated (${ledger.reference_nodes_recreated}/${ledger.reference_nodes_captured})`);
    assert(ledger.links_restored === ledger.links_captured,
      `(9) every captured link was restored (${ledger.links_restored}/${ledger.links_captured})`);
    assert((ledger.graph_restoration_mismatches ?? []).length === 0,
      "(10) no graph restoration mismatches");
    // The decisive one: the node reading RollbackVictim is back even though the
    // failing spec never declared it.
    // Scope note. The ledger restores the nodes IT removed. The failing build
    // also rebuilt the whole graph from its own spec (clear_existing_graph
    // defaults true), which dropped getA - a node reading a variable that was
    // never removed and so was never in the ledger. That is the failing build
    // replacing the graph, not the removal rollback losing it; tracked as
    // findings 0k. Asserted here is the ledger's own contract: every node it
    // removed is back, matched by structural identity rather than object name.
    // (8)(11) The WHOLE pre-request graph must be back, not only the nodes the
    // ledger removed. getA reads KeptA, which was never removed and so is in no
    // ledger; it survives because a failing build no longer destroys the
    // existing graph at all - the replacement is built alongside it and
    // discarded when it turns out not to be whole (findings 0k).
    const varGetAfter = (afterFailure.data?.graph?.nodes ?? []).filter((n) => n.type === "VariableGet").length;
    const varGetBefore = (beforeFailure.data?.graph?.nodes ?? []).filter((n) => n.type === "VariableGet").length;
    assert(varGetBefore === 2, `(1) the pre-request graph had two VariableGet nodes (${varGetBefore})`);
    assert(varGetAfter === varGetBefore,
      `(8) every VariableGet returned, including the one no ledger saw (${varGetAfter} vs ${varGetBefore})`);
    const typesOf = (d) => (d?.graph?.nodes ?? []).map((n) => n.type).sort();
    assert(JSON.stringify(typesOf(afterFailure.data)) === JSON.stringify(typesOf(beforeFailure.data)),
      "(11) the post-rollback graph node types match the pre-request graph exactly");
    assert((afterFailure.data?.graph?.node_count ?? -1) === (beforeFailure.data?.graph?.node_count ?? -2),
      `(8) graph_inspect reports the original node count (${afterFailure.data?.graph?.node_count} vs ${beforeFailure.data?.graph?.node_count})`);
    assert(failed.data?.cleanup?.rollback_succeeded === true,
      "(13) the failed build reports rollback_succeeded");
    assert((failed.data?.cleanup?.dirty_packages_after ?? []).length === 0,
      "(13) no dirty package remains after the failed build");
    assert((failed.data?.cleanup?.cleanup_errors ?? []).length === 0,
      "(13) no cleanup errors after the failed build");

    // Put the fixture back to the converged three for the cold phase.
    await call("puerts_blueprint_build", {
      asset_path: BP, parent_class: "Actor", variables: DESIRED_THREE, graph: GRAPH_AFTER,
      remove_unlisted: { variables: true }, force_remove_referenced: true,
    });

    const p4After = p4Opened();
    if (p4Before !== null && p4After !== null) {
      assert(p4Before === p4After, "(13) no source-control entry appeared");
    } else {
      console.log("  INFO  p4 unavailable; source-control quiescence not measured");
    }

    mkdirSync(join(repoRoot, "docs", "evidence"), { recursive: true });
    writeFileSync(join(repoRoot, "docs", "evidence", "bp-remove-unlisted.json"),
      JSON.stringify({ final_variables: finalVars, plan: plan.data, forced: forced.data?.convergence }, null, 2));
  }

  const out = join(repoRoot, "docs", "evidence", `bp-remove-unlisted-${phase}.json`);
  mkdirSync(join(repoRoot, "docs", "evidence"), { recursive: true });
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
