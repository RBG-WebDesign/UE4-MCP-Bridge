#!/usr/bin/env node
// Live acceptance for puerts_behavior_tree_build against a running UE4.27
// editor. Evidence policy: the builder's own report is never the only proof.
// Asset existence and classes come from find_assets, the blackboard reference
// from read_property, failed-request cleanliness from the filesystem, and the
// editor log is checked for new errors. The one thing that has no independent
// reader is the node graph itself, so a full pass here is native_live_partial
// until a Behavior Tree inspector exists (tracked as its own capability).
//
// Requires MCP_UNREAL_PROJECT_ROOT; pipe and token are discovered from it.
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(root, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
const projectRoot = process.env.MCP_UNREAL_PROJECT_ROOT;
if (!projectRoot) throw new Error("Set MCP_UNREAL_PROJECT_ROOT to the test project root");
delete process.env.MCP_PUERTS_PIPE;
delete process.env.MCP_PUERTS_TOKEN_PATH;
delete process.env.MCP_PUERTS_TOKEN;
if (process.env.MCP_ENABLE_LEGACY_HTTP === "1") {
  throw new Error("Unset MCP_ENABLE_LEGACY_HTTP: this acceptance must prove the native lane alone");
}

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
  return JSON.parse(text);
}

function assert(condition, label) {
  if (!condition) throw new Error(`FAIL  ${label}`);
  console.log(`  PASS  ${label}`);
}

const TREE = "/Game/MCPGenerated/BT_AcceptancePatrol";
const BOARD = `${TREE}_BB`;
const BAD_TREE = "/Game/MCPGenerated/BT_AcceptanceBadType";
const contentFile = (packagePath) =>
  join(projectRoot, "Content", packagePath.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");

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
          { id: "hasTarget", type: "Blackboard", params: { BlackboardKey: "TargetActor" } },
        ],
        children: [
          { id: "chaseMove", type: "MoveTo", params: { BlackboardKey: "TargetActor" } },
          { id: "chaseWait", type: "Wait", params: { WaitTime: "1.0" } },
        ],
      },
      {
        id: "patrol",
        type: "Sequence",
        children: [
          { id: "patrolMove", type: "MoveTo", params: { BlackboardKey: "PatrolPoint" } },
          { id: "patrolWait", type: "Wait", params: { WaitTime: "3.0" } },
        ],
      },
    ],
  },
};

try {
  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "behavior-tree-acceptance", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  // The catalog itself must be native-only: no legacy name may be registered.
  const listed = await send("tools/list", {});
  const names = (listed?.result?.tools ?? []).map((tool) => tool.name);
  assert(names.length > 0
    && names.every((name) => name.startsWith("puerts_") || name.startsWith("engine_source_")),
    `the catalog is native-only (${names.length} tools, no legacy or HTTP names)`);

  // --- Build. The response is the builder's claim; everything after this
  // block is independent evidence. ---
  const built = await call("puerts_behavior_tree_build", SPEC);
  assert(built.success === true, `patrol tree built (${built.errors?.join("; ") || "no errors"})`);
  assert(built.transport === "named_pipe", "the response names the native transport");
  assert(typeof built.transaction_id === "string" && built.transaction_id.length > 0,
    "the build ran in a transaction");
  assert(built.data?.has_root === true && built.data?.saved === true,
    "the builder reports a root and a save");
  assert(built.changed_assets.length === 2, "both changed assets are reported");
  const keys1 = JSON.stringify(built.data?.blackboard_keys ?? []);
  assert(keys1.includes("TargetActor") && keys1.includes("PatrolPoint"),
    "both key names read back from the blackboard asset");
  assert(keys1.includes("Object") && keys1.includes("Vector"),
    "both key types read back from the blackboard asset");

  // --- Independent evidence. ---
  const found = await call("puerts_find_assets", { path: "/Game/MCPGenerated", recursive: true, limit: 200 });
  const assets = found.data?.assets ?? [];
  const entryFor = (packagePath) =>
    assets.find((a) => JSON.stringify(a).includes(packagePath));
  const treeEntry = entryFor(TREE);
  const boardEntry = entryFor(BOARD);
  assert(treeEntry !== undefined, "the BehaviorTree asset exists at its expected path (asset registry)");
  assert(boardEntry !== undefined, "the Blackboard asset exists at its expected path (asset registry)");
  assert(JSON.stringify(treeEntry).includes("BehaviorTree"), "the tree asset has the BehaviorTree class");
  assert(JSON.stringify(boardEntry).includes("Blackboard"), "the blackboard asset has the BlackboardData class");
  assert(existsSync(contentFile(TREE)) && existsSync(contentFile(BOARD)),
    "both .uasset files exist on disk");

  const reference = await call("puerts_read_property", {
    object_path: `${TREE}.BT_AcceptancePatrol`,
    property: "BlackboardAsset",
  });
  assert(reference.success === true
    && JSON.stringify(reference.data ?? {}).includes("BT_AcceptancePatrol_BB"),
    "the tree references its blackboard (independent reflection read)");

  // --- Convergence: same spec, nothing duplicated. ---
  const rerun = await call("puerts_behavior_tree_build", SPEC);
  assert(rerun.success === true, "the same spec reruns cleanly");
  assert(rerun.data?.created === false && rerun.data?.created_blackboard === false,
    "a rerun updates instead of duplicating assets");
  assert(rerun.data?.has_root === true, "the rerun tree still has its root");
  const keyCount = (json) => ((json.match(/"name"/g) ?? []).length);
  assert(keyCount(JSON.stringify(rerun.data?.blackboard_keys ?? [])) === keyCount(keys1),
    "a rerun adds no duplicate blackboard keys");

  // --- Failure atomicity: a bad node type on a fresh path must reach disk
  // nowhere. The in-memory transient asset is undone with the transaction;
  // what this proves independently is that no package was written. ---
  const bad = await call("puerts_behavior_tree_build", {
    asset_path: BAD_TREE,
    root: { id: "r", type: "NotARealNode" },
  });
  assert(bad.success === false, "an unknown node type is rejected");
  assert(bad.data?.saved !== true, "the failed build reports no save");
  assert(!existsSync(contentFile(BAD_TREE)) && !existsSync(contentFile(`${BAD_TREE}_BB`)),
    "the failed build wrote nothing to disk (filesystem check)");

  // --- The editor log gained no unresolved builder errors. The expected
  // rejection above logs through the command response, not as an engine
  // error, so any Behavior Tree error line here is new and real. ---
  const logs = await call("puerts_get_logs", { maximum_lines: 400 });
  const logLines = (logs.data?.lines ?? logs.log_output ?? []).map(String);
  const btErrors = logLines.filter((line) =>
    /error/i.test(line) && /(BehaviorTree|Blackboard|BTBuilder)/i.test(line));
  assert(btErrors.length === 0,
    `the editor log has no Behavior Tree errors (${btErrors[0] ?? "clean"})`);

  console.log("\nbehavior_tree_build acceptance passed: native_live_partial.");
  console.log("The node graph itself is still the builder's own report; a Behavior Tree");
  console.log("inspector (the graph_inspect pattern) is the tracked follow-on capability.");
} finally {
  child.kill();
}
