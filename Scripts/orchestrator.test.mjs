#!/usr/bin/env node
/**
 * Unit tests for the orchestrator's decisions. No editor, no server, no state
 * on disk: what is under test is which stages run, whether a build landed,
 * whether the read-back matches the plan, and whether a resume is allowed.
 *
 *   node Scripts/orchestrator.test.mjs
 */
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { evaluateBuild, evaluateInspection, planFingerprint, stagesToRun } from "./bridge-orchestrator.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
let passed = 0;
const ok = (label) => { console.log(`  PASS  ${label}`); passed += 1; };

// ---------------------------------------------------------- fingerprint

const planA = { name: "one", blueprint: { asset_path: "/Game/MCPGenerated/BP_A", parent_class: "Actor" }, expect: { min_nodes: 2 } };
const planB = { name: "a different name", blueprint: { parent_class: "Actor", asset_path: "/Game/MCPGenerated/BP_A" }, expect: { min_nodes: 2 } };
assert.equal(planFingerprint(planA), planFingerprint(planB));
ok("the fingerprint ignores key order and the plan's cosmetic name");

const planC = { ...planA, blueprint: { ...planA.blueprint, parent_class: "Pawn" } };
assert.notEqual(planFingerprint(planA), planFingerprint(planC));
ok("a changed parent class changes the fingerprint");

// ----------------------------------------------------------------- build

const goodBuild = {
  success: true,
  data: {
    asset_path: "/Game/MCPGenerated/BP_A", created: true, compiled: true, compile_status: "UpToDate",
    saved: true, graph: { node_count: 4, connection_count: 3, unresolved_connections: [] },
    convergence: { converged: true }, warnings: [],
  },
};
assert.equal(evaluateBuild(goodBuild).ok, true);
assert.equal(evaluateBuild(goodBuild).observed.node_count, 4);
ok("a compiled build with no dropped connections is ok");

const droppedLinks = JSON.parse(JSON.stringify(goodBuild));
droppedLinks.data.graph.unresolved_connections = ["Branch.Then -> PrintString.Exec"];
const dropped = evaluateBuild(droppedLinks);
assert.equal(dropped.ok, false);
assert.ok(dropped.problems.join(" ").includes("Branch.Then"), "the dropped pair is named");
ok("a build with an unresolved connection fails and names the pair");

const failedCompile = JSON.parse(JSON.stringify(goodBuild));
failedCompile.data.compiled = false;
failedCompile.data.compile_status = "Error";
assert.equal(evaluateBuild(failedCompile).ok, false);
ok("a build whose Blueprint did not compile fails");

const skippedCompile = JSON.parse(JSON.stringify(goodBuild));
skippedCompile.data.compiled = false;
skippedCompile.data.compile_status = "Skipped";
assert.equal(evaluateBuild(skippedCompile).ok, true);
ok("compile_status Skipped is not a failure: that is what compile:false reports");

const refused = evaluateBuild({ success: false, errors: ["Bridge is busy."] });
assert.equal(refused.ok, false);
assert.ok(refused.problems[0].includes("Bridge is busy."), "the editor's own error is carried through");
ok("a refused build reports the editor's error, not a generic one");

// ------------------------------------------------------------- inspection

const inspected = {
  components: [{ name: "Mesh" }, { name: "Trigger" }],
  variables: [{ name: "IsOpen" }],
  package_dirty_after: false,
  graph: { name: "EventGraph", node_count: 5, connection_count: 4, unmapped_nodes: [], structure_hash_sha1: "abc" },
};
const match = evaluateInspection(inspected, { components: ["Mesh"], variables: ["IsOpen"], min_nodes: 5, no_unmapped_nodes: true });
assert.equal(match.ok, true);
assert.deepEqual(match.observed.components, ["Mesh", "Trigger"]);
ok("a read-back that satisfies the plan reports ok and what it observed");

const missing = evaluateInspection(inspected, { components: ["Door"], variables: ["Speed"], min_nodes: 9 });
assert.equal(missing.ok, false);
assert.equal(missing.gaps.length, 3);
assert.ok(missing.gaps[0].includes("Mesh, Trigger"), "the gap names what the asset does have");
assert.ok(missing.gaps[2].includes("5 nodes"), "the node gap names the real count");
ok("every gap is named individually, with what the asset actually has");

const unmapped = evaluateInspection(
  { ...inspected, graph: { ...inspected.graph, unmapped_nodes: [{ id: "K2Node_Foo" }] } },
  { no_unmapped_nodes: true },
);
assert.equal(unmapped.ok, false);
assert.ok(unmapped.gaps[0].includes("K2Node_Foo"));
ok("an unmapped node is a gap and is named");

// A response with no graph at all is the shape failure that used to become
// `undefined` inside a comparison and pass.
const noGraph = evaluateInspection({ components: [] }, { min_nodes: 1 });
assert.equal(noGraph.ok, false);
assert.ok(noGraph.gaps[0].includes("data.graph"), "the missing field is named");
assert.ok(noGraph.gaps[0].includes("payload shape"), "the payload it actually got is described");
ok("an inspection with no graph fails with the payload described, not with undefined");

// ------------------------------------------------------------------ resume

const names = ["probe", "build", "inspect", "repair", "review", "pie"];
const fingerprint = planFingerprint(planA);
const state = {
  plan_fingerprint: fingerprint,
  stages: [{ name: "probe", status: "ok" }, { name: "build", status: "ok" }, { name: "inspect", status: "failed" }],
};

assert.deepEqual(
  stagesToRun(names, state, { resume: false, fingerprint }).map((s) => s.run),
  [true, true, true, true, true, true],
);
ok("without --resume every stage runs");

const resumed = stagesToRun(names, state, { resume: true, fingerprint });
assert.deepEqual(resumed.map((s) => s.run), [false, false, true, true, true, true]);
ok("resume skips the ok stages and re-runs from the first that was not");

// Everything after a failure re-runs even if the state file recorded it ok:
// a later stage's result describes a state that was never reached.
const outOfOrder = {
  plan_fingerprint: fingerprint,
  stages: [{ name: "probe", status: "ok" }, { name: "build", status: "failed" }, { name: "inspect", status: "ok" }],
};
assert.deepEqual(
  stagesToRun(names, outOfOrder, { resume: true, fingerprint }).map((s) => s.run),
  [false, true, true, true, true, true],
);
ok("a stage after a failure re-runs even when the state file called it ok");

const changed = stagesToRun(names, state, { resume: true, fingerprint: "different" });
assert.deepEqual(changed.map((s) => s.run), [true, true, true, true, true, true]);
assert.ok(changed[0].reason.includes("the plan changed"), "the reason says why nothing was reused");
ok("a resume against a changed plan reuses nothing and says so");

assert.deepEqual(stagesToRun(names, null, { resume: true, fingerprint }).map((s) => s.run), [true, true, true, true, true, true]);
ok("resume with no state file runs everything");

// ------------------------------------------------------------- end to end

// PIE must be opt-in. Proven by the source, not by starting one: this test is
// not allowed to touch an editor and AGENTS.md is not allowed to be satisfied
// by a comment. The flag name and the default are what is under test.
const source = spawnSync(process.execPath, ["-e",
  `const fs=require('fs');const s=fs.readFileSync(${JSON.stringify(join(root, "Scripts", "bridge-orchestrator.mjs"))},'utf8');`
  + `if(!/const includePie = argv\\.includes\\("--pie"\\)/.test(s))throw new Error('PIE is not gated on an explicit --pie flag');`
  + `if(!/if \\(!includePie\\)/.test(s))throw new Error('the pie stage does not check the flag before starting');`
], { encoding: "utf-8" });
assert.equal(source.status, 0, source.stderr);
ok("Play In Editor is behind an explicit --pie flag and is off by default");

// No plan, no run. The failure has to name the missing argument rather than
// spawning a server and refusing later.
const noPlan = spawnSync(process.execPath, [join(root, "Scripts", "bridge-orchestrator.mjs")], { encoding: "utf-8", cwd: root });
assert.notEqual(noPlan.status, 0);
assert.ok(/--plan/.test(`${noPlan.stdout}${noPlan.stderr}`), "the missing --plan argument is named");
ok("the orchestrator with no plan exits non-zero and names the missing argument");

console.log(`\norchestrator: ${passed} checks passed.`);
