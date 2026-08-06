#!/usr/bin/env node
// Live acceptance for capability quarantine, against a running UE4.27 editor.
//
// The property under test is not "the tool is hidden". A hidden tool is still
// callable: the pipe takes a tool name, not a tools/list entry, and a compat
// alias, a stale client or a hand-written request never reads discovery at all.
// The property is that a quarantined capability cannot execute by ANY route,
// and that every route refuses with the same stable code instead of a
// version-mismatch-shaped "unknown tool".
//
// The subject is puerts_level_load / puerts_level_create, which crashed the
// editor 3/3 on 2026-08-05 (docs/CAPABILITY_FINDINGS.md, section Unknown). That
// is why condition 6 exists: the refusal is only worth anything if the editor
// is still alive afterwards, and this script is the one place that calls a
// known-crashing tool on purpose.
//
//   node Scripts/quarantine-acceptance.mjs
//   node Scripts/quarantine-acceptance.mjs --project "D:/Unreal Projects/Other"
//
// Requires a live editor for the project. Run npm run build first.

import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("mcp-server/dist is missing. Run npm run build first.");

const projectArg = process.argv.indexOf("--project");
const project = projectArg >= 0
  ? process.argv[projectArg + 1]
  : process.env.MCP_UNREAL_PROJECT_ROOT ?? "D:/Unreal Projects/BridgeInstallTest";
process.env.MCP_UNREAL_PROJECT_ROOT = project;

const dist = (p) => import(pathToFileURL(join(repoRoot, "mcp-server", "dist", p)).href);
const { PuerTSClient } = await dist("puerts-client.js");
const { QUARANTINED_TOOLS, nativeToolSpec } = await dist("tools/puerts.js");
const { compatAliasTargets } = await dist("tools/compat.js");

const failures = [];
function assert(condition, label, detail = "") {
  if (condition) { console.log(`  PASS  ${label}`); return true; }
  console.log(`  FAIL  ${label}${detail ? `\n        ${detail}` : ""}`);
  failures.push(label);
  return false;
}

/** tools/list off the real stdio server, which is the only honest answer to
    "what would a client see". Reading the factory in-process would test the
    factory; a client reads this. */
async function listTools(extraEnv = {}) {
  const child = spawn(process.execPath, [serverPath], {
    stdio: ["pipe", "pipe", "pipe"],
    env: { ...process.env, ...extraEnv },
  });
  let buffer = "";
  const pending = new Map();
  child.stdout.on("data", (chunk) => {
    buffer += chunk.toString();
    for (let n; (n = buffer.indexOf("\n")) >= 0;) {
      const line = buffer.slice(0, n).trim();
      buffer = buffer.slice(n + 1);
      if (!line) continue;
      const message = JSON.parse(line);
      const settle = pending.get(message.id);
      if (settle) { pending.delete(message.id); settle(message); }
    }
  });
  let nextId = 1;
  const send = (method, params) => {
    const id = nextId++;
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 60000);
      pending.set(id, (m) => { clearTimeout(timer); resolve(m); });
    });
  };
  try {
    await send("initialize", {
      protocolVersion: "2024-11-05", capabilities: {},
      clientInfo: { name: "quarantine-acceptance", version: "1.0.0" },
    });
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");
    const listed = await send("tools/list", {});
    return (listed?.result?.tools ?? []).map((t) => t.name);
  } finally {
    child.kill();
  }
}

console.log(`quarantine acceptance against ${project}`);
console.log(`  quarantined: ${[...QUARANTINED_TOOLS].join(", ")}\n`);

const client = new PuerTSClient();

// --- 1, 2, 7: the capability report -----------------------------------------
const before = await client.call("diagnostic", { actor_limit: 1 }, 15000);
assert(before.success === true, "the editor answers puerts_diagnostic");
const capabilities = before.data?.capabilities;
assert(capabilities !== undefined,
  "diagnostic carries a capabilities report",
  "the editor is serving a runtime built before the quarantine; restart it after npm run install:sync");

const unavailable = capabilities?.unavailable ?? [];
const available = capabilities?.available ?? [];
for (const name of QUARANTINED_TOOLS) {
  const command = nativeToolSpec(name).command;
  const entry = unavailable.find((u) => u.capability === command);
  assert(entry !== undefined, `1. ${command} is reported unavailable`);
  assert(entry?.reason_code === "capability_quarantined",
    `2. ${command} names capability_quarantined`, `got ${JSON.stringify(entry?.reason_code)}`);
  assert(!available.includes(command), `${command} is not also reported available`);
}
assert(available.length + unavailable.length === 73,
  "7. available plus unavailable equals the registry definition count",
  `got ${available.length} + ${unavailable.length} = ${available.length + unavailable.length}, expected 73`);

// --- 3, 4: discovery, with aliases both off and on --------------------------
const plain = await listTools();
const withAliases = await listTools({ MCP_COMPAT_ALIASES: "1" });
assert(withAliases.length > plain.length,
  "the alias-enabled run really registered aliases",
  `plain ${plain.length}, with aliases ${withAliases.length}; if equal, condition 4 proves nothing`);
for (const name of QUARANTINED_TOOLS) {
  assert(!plain.includes(name), `3. ${name} is absent from tools/list`);
  assert(!withAliases.includes(name), `3. ${name} is absent with aliases enabled`);
}
const quarantinedAliases = Object.entries(compatAliasTargets)
  .filter(([, canonical]) => QUARANTINED_TOOLS.has(canonical))
  .map(([alias]) => alias);
assert(quarantinedAliases.length > 0, "at least one alias fronts a quarantined tool");
for (const alias of quarantinedAliases) {
  assert(!withAliases.includes(alias),
    `4. the ${alias} alias is absent even with MCP_COMPAT_ALIASES=1`);
}

// --- 5: the runtime refuses a direct request --------------------------------
// This is the call discovery cannot prevent: the command name goes straight
// down the pipe. Everything below asserts it stopped in the JavaScript registry
// and never reached the permission check or the native operation.
for (const name of QUARANTINED_TOOLS) {
  const command = nativeToolSpec(name).command;
  const refused = await client.call(command, { level_path: "/Game/Maps/QuarantineProbe" }, 15000);
  const errors = (refused.errors ?? []).join(" ");
  assert(refused.success === false, `5. a direct ${command} request is refused`);
  assert(refused.error_code === "capability_quarantined",
    `5. the refusal carries capability_quarantined`, `got ${JSON.stringify(refused.error_code)}`);
  assert(!errors.includes("Missing permission"),
    `5. ${command} did not reach the permission check`);
  assert(!errors.includes("Tool is not registered"),
    `5. ${command} refused as quarantined, not as unknown`);
  assert(refused.transaction_id === "",
    `5. ${command} opened no transaction`, `got ${JSON.stringify(refused.transaction_id)}`);
  assert((refused.changed_assets ?? []).length === 0 && (refused.changed_actors ?? []).length === 0,
    `5. ${command} changed nothing`);
  assert(errors.includes("docs/CAPABILITY_FINDINGS.md"),
    `5. the refusal points at the finding rather than only saying no`);
}

// --- 6: the editor survived it ----------------------------------------------
// The whole reason these tools are quarantined is that calling them killed the
// editor. If the refusal had fallen through to the native call, this is where
// the run would end with a dead pipe instead of a matching session.
const after = await client.call("diagnostic", { actor_limit: 1 }, 15000);
assert(after.success === true, "6. the editor still answers after the refusal test");
assert(after.session?.session_id === before.session?.session_id
  && after.session?.editor_pid === before.session?.editor_pid,
  "6. the same editor process served both diagnostics",
  `before ${before.session?.editor_pid}, after ${after.session?.editor_pid}`);

console.log(`\n${failures.length === 0 ? "OK" : "FAIL"}: quarantine acceptance, ${failures.length} failure(s)`);
if (failures.length > 0) {
  for (const failure of failures) console.log(`  - ${failure}`);
  process.exit(1);
}
