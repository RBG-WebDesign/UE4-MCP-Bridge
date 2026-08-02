#!/usr/bin/env node
// Live acceptance for puerts_behavior_tree_build AND its independent read
// half, puerts_behavior_tree_inspect, against a running UE4.27 editor.
//
// Evidence policy: the builder's report is never the only proof. The
// inspector walks the saved runtime tree independently; asset existence and
// classes come from find_assets; failed-request cleanliness, file hashes and
// mtimes come from the filesystem; source-control quiescence from p4; the
// editor log is scanned for errors.
//
// Two phases, because the restart between them is orchestrated outside this
// script (the editor cannot restart itself over the pipe):
//   --phase=build  build, inspect, compare against the spec, rerun,
//                  convergence by independent hash; writes the hash to
//                  docs/evidence/bt-inspect-hash.json. Leaves the editor
//                  CLEAN (no dirty transients) so it can close gracefully.
//   --phase=cold   after an editor restart: cold-load inspect, hash equality
//                  with the saved hash, then the invalid-node test LAST (it
//                  leaves an in-memory transient; see findings 0c).
//
// Requires MCP_UNREAL_PROJECT_ROOT; pipe and token are discovered from it.
import { execSync, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(root, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
// Refuse to touch the editor unless the plugin in that project is the plugin
// in this checkout. A live run against a stale install proves nothing about the
// code under review and looks exactly like a passing run.
const projectRoot = requireCurrentInstall();
delete process.env.MCP_PUERTS_PIPE;
delete process.env.MCP_PUERTS_TOKEN_PATH;
delete process.env.MCP_PUERTS_TOKEN;
if (process.env.MCP_ENABLE_LEGACY_HTTP === "1") {
  throw new Error("Unset MCP_ENABLE_LEGACY_HTTP: this acceptance must prove the native lane alone");
}
const phase = process.argv.includes("--phase=cold") ? "cold" : "build";

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
  return { parsed: JSON.parse(text), bytes: text.length };
}

function assert(condition, label) {
  if (!condition) throw new Error(`FAIL  ${label}`);
  console.log(`  PASS  ${label}`);
}

function canonicalize(value) {
  if (Array.isArray(value)) return value.map(canonicalize);
  if (value !== null && typeof value === "object") {
    const out = {};
    for (const key of Object.keys(value).sort()) out[key] = canonicalize(value[key]);
    return out;
  }
  return value;
}
const jsonHash = (value) =>
  createHash("sha256").update(JSON.stringify(canonicalize(value))).digest("hex");
const fileSha = (path) => createHash("sha256").update(readFileSync(path)).digest("hex");

const TREE = "/Game/MCPGenerated/BT_AcceptancePatrol";
const BOARD = `${TREE}_BB`;
const BAD_TREE = "/Game/MCPGenerated/BT_AcceptanceBadType";
const contentFile = (packagePath) =>
  join(projectRoot, "Content", packagePath.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");
const evidenceDir = join(root, "docs", "evidence");
const hashFile = join(evidenceDir, "bt-inspect-hash.json");

function p4Opened() {
  try {
    return execSync("p4 opened", { cwd: join(projectRoot, "Content"), encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
  } catch {
    return null; // p4 unavailable; the check degrades to INFO
  }
}

const SPEC = {
  asset_path: TREE,
  keys: [
    { name: "TargetActor", type: "Object", base_class: "/Script/Engine.Actor" },
    { name: "PatrolPoint", type: "Vector" },
  ],
  root: {
    id: "root",
    type: "Selector",
    children: [
      {
        id: "chase",
        type: "Sequence",
        decorators: [
          { id: "hasTarget", type: "Blackboard", params: { blackboard_key: "TargetActor" } },
        ],
        children: [
          { id: "chaseMove", type: "MoveTo", params: { blackboard_key: "TargetActor" } },
          { id: "chaseWait", type: "Wait", params: { wait_time: "1.0" } },
        ],
      },
      {
        id: "patrol",
        type: "Sequence",
        children: [
          { id: "patrolMove", type: "MoveTo", params: { blackboard_key: "PatrolPoint" } },
          { id: "patrolWait", type: "Wait", params: { wait_time: "3.0" } },
        ],
      },
    ],
  },
};

/** Assert the inspected tree matches the SPEC structure independently:
    classes, child order, decorator attachment, key references. */
function assertStructureMatchesSpec(data) {
  const rootNode = data.root;
  assert(rootNode?.kind === "composite" && rootNode?.class_path?.includes("BTComposite_Selector"),
    "root is the requested Selector composite");
  const children = rootNode.children ?? [];
  assert(children.length === 2, "root has the two requested Sequence children");
  assert(children[0]?.child_index === 0 && children[1]?.child_index === 1,
    "child ordering matches the saved tree");
  assert(children.every((c) => c.class_path?.includes("BTComposite_Sequence")),
    "both children are Sequence composites");

  const chase = children[0];
  assert((chase.decorators ?? []).length === 1
    && chase.decorators[0].class_path?.includes("BTDecorator_Blackboard")
    && chase.decorators[0].blackboard_keys?.includes("TargetActor"),
    "the Blackboard decorator is attached to the chase child and references TargetActor");
  const chaseTasks = chase.children ?? [];
  assert(chaseTasks.length === 2
    && chaseTasks[0].class_path?.includes("BTTask_MoveTo")
    && chaseTasks[0].blackboard_keys?.includes("TargetActor")
    && chaseTasks[1].class_path?.includes("BTTask_Wait")
    && chaseTasks[1].properties?.WaitTime === 1,
    "chase sequence holds MoveTo(TargetActor) then Wait(1.0), in order");
  const patrolTasks = children[1].children ?? [];
  assert(patrolTasks.length === 2
    && patrolTasks[0].class_path?.includes("BTTask_MoveTo")
    && patrolTasks[0].blackboard_keys?.includes("PatrolPoint")
    && patrolTasks[1].class_path?.includes("BTTask_Wait")
    && patrolTasks[1].properties?.WaitTime === 3,
    "patrol sequence holds MoveTo(PatrolPoint) then Wait(3.0), in order");

  assert(data.composite_count === 3 && data.task_count === 4
    && data.decorator_count === 1 && data.service_count === 0,
    "node counts match the spec (3 composites, 4 tasks, 1 decorator, 0 services)");
  assert(JSON.stringify(data.referenced_blackboard_keys) === JSON.stringify(["PatrolPoint", "TargetActor"]),
    "referenced blackboard keys are exactly PatrolPoint and TargetActor");
  const keys = JSON.stringify(data.blackboard_keys ?? []);
  assert(keys.includes("TargetActor") && keys.includes("Object")
    && keys.includes("PatrolPoint") && keys.includes("Vector"),
    "blackboard key names and types match the spec independently");
  assert(String(data.blackboard_path).includes("BT_AcceptancePatrol_BB"),
    "the tree reports the expected Blackboard");
  assert(data.identity_kind === "derived", "node identity is labeled derived, not native");
  const everyNode = [];
  (function walk(n) {
    if (!n) return;
    everyNode.push(n);
    for (const d of n.decorators ?? []) everyNode.push(d);
    for (const s of n.services ?? []) everyNode.push(s);
    for (const c of n.children ?? []) walk(c);
  })(rootNode);
  assert(everyNode.every((n) => typeof n.class_path === "string" && typeof n.properties === "object"),
    "every node, known or unknown, reports structured class_path and properties");
}

/** The read-only proofs shared by both phases. */
async function inspectWithProofs(label) {
  const files = [contentFile(TREE), contentFile(BOARD)];
  const before = files.map((f) => ({ sha: fileSha(f), mtime: statSync(f).mtimeMs }));
  const p4Before = p4Opened();

  const first = await call("puerts_behavior_tree_inspect", { asset_path: TREE });
  const second = await call("puerts_behavior_tree_inspect", { asset_path: TREE });
  const data = first.parsed.data;

  assert(first.parsed.success === true, `${label}: inspection succeeded`);
  assert(first.parsed.transaction_id === "", `${label}: no transaction started`);
  assert(first.parsed.changed_assets.length === 0 && first.parsed.changed_actors.length === 0,
    `${label}: inspection reported nothing changed`);
  assert(data.package_dirty_before === data.package_dirty_after,
    `${label}: no package became dirty`);
  assert(first.bytes < 1024 * 1024,
    `${label}: payload ${first.bytes} bytes is within the 1 MiB pipe limit`);
  assert(jsonHash(first.parsed.data) === jsonHash(second.parsed.data)
    && data.structure_hash_sha1 === second.parsed.data.structure_hash_sha1,
    `${label}: two inspections produce identical canonical JSON and structure hash`);

  const after = files.map((f) => ({ sha: fileSha(f), mtime: statSync(f).mtimeMs }));
  assert(JSON.stringify(before) === JSON.stringify(after),
    `${label}: asset file hashes and modification times are unchanged`);
  const p4After = p4Opened();
  if (p4Before !== null && p4After !== null) {
    assert(p4Before === p4After, `${label}: no source-control operation started (p4 opened unchanged)`);
  } else {
    console.log(`  INFO  ${label}: p4 unavailable; source-control quiescence not measured`);
  }
  return data;
}

try {
  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "behavior-tree-acceptance", version: "2.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  const listed = await send("tools/list", {});
  const names = (listed?.result?.tools ?? []).map((tool) => tool.name);
  assert(names.length > 0
    && names.every((name) => name.startsWith("puerts_") || name.startsWith("engine_source_")),
    `the catalog is native-only (${names.length} tools, no legacy or HTTP names)`);

  if (phase === "build") {
    const built = await call("puerts_behavior_tree_build", SPEC);
    assert(built.parsed.success === true,
      `patrol tree built (${built.parsed.errors?.join("; ") || "no errors"})`);
    assert(built.parsed.transport === "named_pipe" && built.parsed.transaction_id.length > 0,
      "the build ran in a transaction over the native pipe");
    assert(built.parsed.data?.saved === true && built.parsed.changed_assets.length === 2,
      "both assets were saved and reported");

    const data = await inspectWithProofs("inspect");
    assertStructureMatchesSpec(data);

    const rerun = await call("puerts_behavior_tree_build", SPEC);
    assert(rerun.parsed.success === true
      && rerun.parsed.data?.created === false && rerun.parsed.data?.created_blackboard === false,
      "the same spec reruns and updates instead of duplicating");
    const dataAfterRerun = await inspectWithProofs("post-rerun inspect");
    assert(dataAfterRerun.structure_hash_sha1 === data.structure_hash_sha1,
      "the independent reader proves the rerun converged (identical structure hash)");

    mkdirSync(evidenceDir, { recursive: true });
    writeFileSync(hashFile, JSON.stringify({
      structure_hash_sha1: data.structure_hash_sha1,
      canonical_sha256: jsonHash(data),
      composite_count: data.composite_count,
      task_count: data.task_count,
    }, null, 2) + "\n");
    console.log("\nbehavior_tree build+inspect phase passed. Editor is clean; restart it,");
    console.log("then run with --phase=cold to finish.");
  } else {
    assert(existsSync(hashFile), "the build phase recorded a structure hash to compare against");
    const saved = JSON.parse(readFileSync(hashFile, "utf8"));
    const data = await inspectWithProofs("cold inspect");
    assertStructureMatchesSpec(data);
    // The cross-restart invariant is the STRUCTURE hash, not the full
    // canonical JSON: on reload UE4.27 adds the implicit SelfActor key to the
    // blackboard (engine behavior), so blackboard_keys legitimately gains an
    // entry the freshly-built asset did not report. Within-session canonical
    // equality is asserted by inspectWithProofs in both phases.
    assert(data.structure_hash_sha1 === saved.structure_hash_sha1
      && data.composite_count === saved.composite_count
      && data.task_count === saved.task_count,
      "the cold-loaded tree matches the pre-restart structure hash and counts");

    // Invalid inputs fail without mutation.
    const missing = await call("puerts_behavior_tree_inspect", { asset_path: "/Game/MCPGenerated/BT_DoesNotExist" });
    assert(missing.parsed.success === false, "a missing asset path fails cleanly");
    // The blackboard the build phase created: guaranteed to exist and
    // guaranteed to be the wrong class, in any project this runs in.
    const wrongClass = await call("puerts_behavior_tree_inspect", { asset_path: BOARD });
    assert(wrongClass.parsed.success === false
      && JSON.stringify(wrongClass.parsed.errors).includes("not a BehaviorTree"),
      "a non-BehaviorTree asset fails with a typed error");

    // The invalid-node build test runs LAST: it leaves an in-memory dirty
    // transient (findings 0c), which would hang a graceful editor close if it
    // ran before the restart.
    const bad = await call("puerts_behavior_tree_build", {
      asset_path: BAD_TREE,
      root: { id: "r", type: "NotARealNode" },
    });
    assert(bad.parsed.success === false && bad.parsed.data?.saved !== true,
      "an unknown node type is rejected with no save");
    assert(!existsSync(contentFile(BAD_TREE)) && !existsSync(contentFile(`${BAD_TREE}_BB`)),
      "the failed build wrote nothing to disk (filesystem check)");
    const badInspect = await call("puerts_behavior_tree_inspect", { asset_path: BAD_TREE });
    assert(badInspect.parsed.success === false || badInspect.parsed.data?.root === null,
      "no usable failed artifact survives for the inspector to find");

    const logs = await call("puerts_get_logs", { maximum_lines: 400 });
    const logLines = (logs.parsed.data?.lines ?? logs.parsed.log_output ?? []).map(String);
    const btErrors = logLines.filter((line) =>
      /error/i.test(line) && /(BehaviorTree|Blackboard|BTBuilder)/i.test(line));
    assert(btErrors.length === 0,
      `the editor log has no Behavior Tree errors (${btErrors[0] ?? "clean"})`);

    console.log("\nbehavior_tree build+inspect acceptance passed: independently verified.");
  }
} finally {
  child.kill();
}
