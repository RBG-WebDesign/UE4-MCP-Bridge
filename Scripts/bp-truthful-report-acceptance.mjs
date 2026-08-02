#!/usr/bin/env node
// Live acceptance: blueprint_build must never report a node it did not create.
//
// The defect this closes (findings defect 0, reproduced on Cast and again on
// VariableGet): node_count came from the SPEC, and a factory returning null
// left the requested node in the count. A build could report node_count 2 while
// graph_inspect saw one node. A count derived from the request is not a report,
// it is an echo.
//
// The gate every case runs: created_node_count must equal the node count an
// INDEPENDENT reader finds in the asset. The builder never grades its own work.
import { execSync, spawn } from "node:child_process";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
// Refuse to touch the editor unless the plugin in that project is the plugin
// in this checkout. A live run against a stale install proves nothing about the
// code under review and looks exactly like a passing run.
const projectRoot = requireCurrentInstall();
delete process.env.MCP_PUERTS_PIPE;
delete process.env.MCP_PUERTS_TOKEN_PATH;
delete process.env.MCP_PUERTS_TOKEN;

const child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "ignore"], env: process.env });
let buffer = ""; let nextId = 1; const pending = new Map();
child.stdout.on("data", (d) => {
  buffer += d.toString();
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
    const t = setTimeout(() => reject(new Error(`${method} timed out`)), 60000);
    pending.set(id, (m) => { clearTimeout(t); resolve(m); });
  });
}
const call = async (n, a) =>
  JSON.parse((await send("tools/call", { name: n, arguments: a }))?.result?.content?.[0]?.text ?? "{}");

const failures = [];
function assert(condition, label) {
  if (condition) { console.log(`  PASS  ${label}`); return true; }
  console.log(`  FAIL  ${label}`); failures.push(label); return false;
}
function p4Opened() {
  try {
    return execSync("p4 opened", { cwd: join(projectRoot, "Content"), encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
  } catch { return null; }
}
const contentFile = (p) =>
  join(projectRoot, "Content", p.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");

const P = "/Game/MCPGenerated/BP_TruthProbe";
const VARS = [{ name: "V1", type: "float", default: 1 }];
const evidence = { cases: [] };

/** The gate: the builder's created_node_count must equal what an independent
    reader finds. Only meaningful when the build succeeded and saved. */
async function gate(label, built) {
  const read = await call("puerts_graph_inspect", { asset_path: P });
  const inspected = read.data?.graph?.node_count;
  const created = built.data?.graph?.created_node_count;
  evidence.cases.push({ label, created, inspected, success: built.success });
  assert(created === inspected,
    `(4)(11) ${label}: created_node_count ${created} equals independently inspected ${inspected}`);
  return read;
}

try {
  await send("initialize", { protocolVersion: "2024-11-05", capabilities: {}, clientInfo: { name: "bp-truthful", version: "1.0.0" } });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");
  const p4Before = p4Opened();

  console.log("\n-- 10. a valid multi-type graph is the baseline");
  const good = await call("puerts_blueprint_build", {
    asset_path: P, parent_class: "Actor", variables: VARS,
    graph: {
      nodes: [
        { id: "begin", type: "BeginPlay" },
        { id: "g", type: "VariableGet", params: { varName: "V1" } },
        { id: "say", type: "PrintString" },
        { id: "br", type: "Branch" },
      ],
      connections: [{ from: "begin.then", to: "say.exec" }, { from: "say.then", to: "br.exec" }],
    },
  });
  assert(good.success === true, "(10) the valid graph builds");
  assert(good.data?.graph?.created_node_count === 4, "(5)(10) all four nodes were created");
  assert(good.data?.graph?.failed_node_count === 0, "(10) no node failed");
  assert(good.data?.graph?.created_connection_count === 2, "(10) both connections were made");
  await gate("valid graph", good);

  console.log("\n-- 13. rerunning the valid graph stays consistent");
  const rerun = await call("puerts_blueprint_build", {
    asset_path: P, parent_class: "Actor", variables: VARS,
    graph: {
      nodes: [
        { id: "begin", type: "BeginPlay" },
        { id: "g", type: "VariableGet", params: { varName: "V1" } },
        { id: "say", type: "PrintString" },
        { id: "br", type: "Branch" },
      ],
      connections: [{ from: "begin.then", to: "say.exec" }, { from: "say.then", to: "br.exec" }],
    },
  });
  assert(rerun.success === true, "(13) the rerun succeeds");
  assert(rerun.data?.graph?.created_node_count === 4, "(13) the rerun reports the same created count");
  await gate("rerun", rerun);

  // Every failing case below must leave the asset exactly as the valid build
  // left it, so the gate after each one is also a no-damage check.
  const failing = [
    {
      n: "(1)(2)(3) VariableGet with 'variable' instead of 'varName'",
      spec: { nodes: [{ id: "begin", type: "BeginPlay" }, { id: "g", type: "VariableGet", params: { variable: "V1" } }], connections: [] },
      expectFailedNode: "g",
    },
    {
      n: "(6) Cast with an unresolvable target class",
      spec: { nodes: [{ id: "begin", type: "BeginPlay" }, { id: "c", type: "Cast", params: { target_class: "/Script/Engine.NoSuchClassAtAll" } }], connections: [] },
      expectFailedNode: "c",
    },
    {
      n: "(8) a connection naming a pin role that does not exist",
      spec: { nodes: [{ id: "begin", type: "BeginPlay" }, { id: "say", type: "PrintString" }], connections: [{ from: "begin.nosuchpin", to: "say.exec" }] },
      expectFailedNode: null,
    },
    {
      // Rejected BEFORE mutation, so there is deliberately no graph payload:
      // nothing was created, so there is nothing to count. The earlier reading
      // of this as a reporting gap was wrong.
      n: "(9) a connection referencing an unknown node id",
      spec: { nodes: [{ id: "begin", type: "BeginPlay" }, { id: "say", type: "PrintString" }], connections: [{ from: "begin.then", to: "ghost.exec" }] },
      expectFailedNode: null,
      preMutation: "unknown node id",
    },
  ];

  for (const c of failing) {
    console.log(`\n-- ${c.n}`);
    const bad = await call("puerts_blueprint_build", {
      asset_path: P, parent_class: "Actor", variables: VARS, graph: c.spec,
    });
    evidence.cases.push({ label: c.n, success: bad.success, graph: bad.data?.graph, cleanup: bad.data?.cleanup });
    assert(bad.success === false, `${c.n}: the request FAILS rather than reporting partial success`);
    assert(bad.data?.saved !== true, `${c.n}: nothing was saved`);
    if (c.expectFailedNode) {
      const failedNodes = bad.data?.graph?.failed_nodes ?? [];
      assert(failedNodes.some((f) => f.startsWith(`${c.expectFailedNode}:`)),
        `${c.n}: the refused node is named in failed_nodes`);
      assert(bad.data?.graph?.failed_node_count > 0, `${c.n}: failed_node_count is non-zero`);
      assert(bad.data?.graph?.created_node_count < bad.data?.graph?.requested_node_count,
        `${c.n}: created_node_count excludes the phantom (created ${bad.data?.graph?.created_node_count} < requested ${bad.data?.graph?.requested_node_count})`);
    } else if (c.preMutation) {
      // Rejected before anything was created. The correct report is a named
      // error and NO graph payload: counting a graph that was never built
      // would be the same lie in the other direction.
      assert(JSON.stringify(bad.errors).includes("unknown node id"),
        `${c.n}: rejected before mutation with the unknown id named`);
      assert(bad.data?.graph === undefined,
        `${c.n}: no graph counts are reported for a build that never started`);
    } else {
      // A connection can be refused in two shapes: it resolved to fewer links
      // than requested (failed_connection_count), or the resolver rejected the
      // endpoint outright and named it (unresolved_connections). Either is a
      // truthful report of a refusal; what must never happen is silence.
      const failedConnections = bad.data?.graph?.failed_connection_count ?? 0;
      const unresolved = bad.data?.graph?.unresolved_connections ?? [];
      assert(failedConnections > 0 || unresolved.length > 0,
        `${c.n}: the refused connection is reported (failed_connection_count ${failedConnections}, unresolved ${unresolved.length})`);
    }
    // (12) no residue from a failed case.
    assert((bad.data?.cleanup?.dirty_packages_after ?? []).length === 0, `(12) ${c.n}: no dirty package`);
    assert((bad.data?.cleanup?.cleanup_errors ?? []).length === 0, `(12) ${c.n}: no cleanup errors`);
    assert(existsSync(contentFile(P)), `(12) ${c.n}: the pre-existing asset is untouched on disk`);
  }

  console.log("\n-- 7. MultiGate output count is reported from the node, not the request");
  const mg = await call("puerts_blueprint_build", {
    asset_path: P, parent_class: "Actor", variables: VARS,
    graph: { nodes: [{ id: "begin", type: "BeginPlay" }, { id: "gate", type: "MultiGate", params: { num_outputs: 4 } }], connections: [] },
  });
  const mgRead = await gate("MultiGate", mg);
  if (mg.success === true) {
    // findings 0b: num_outputs is ignored by the factory. The point here is
    // that whatever it built is what gets reported, and the inspector agrees.
    assert(mg.data?.graph?.created_node_count === 2, "(7) both MultiGate-case nodes were created");
    const gateNode = (mgRead.data?.graph?.nodes ?? []).find((n) => n.type === "MultiGate");
    assert(gateNode !== undefined, "(7) the MultiGate node exists in the asset");
  }

  const p4After = p4Opened();
  if (p4Before !== null && p4After !== null) {
    assert(p4Before === p4After, "(12) no source-control entry appeared across every case");
  } else {
    console.log("  INFO  p4 unavailable; source-control quiescence not measured");
  }

  mkdirSync(join(repoRoot, "docs", "evidence"), { recursive: true });
  writeFileSync(join(repoRoot, "docs", "evidence", "bp-truthful-report.json"), JSON.stringify(evidence, null, 2));
  console.log(`\nevidence: docs/evidence/bp-truthful-report.json`);
  if (failures.length > 0) {
    console.log(`\n${failures.length} check(s) did not hold:`);
    for (const f of failures) console.log(`  - ${f}`);
    process.exitCode = 1;
  } else {
    console.log("\nall checks passed");
  }
} catch (error) {
  console.error(error);
  process.exitCode = 1;
} finally {
  child.stdin.end();
}
