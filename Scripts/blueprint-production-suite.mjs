#!/usr/bin/env node
import { spawn } from "node:child_process";
import { existsSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
const plannerPath = join(repoRoot, "mcp-server", "dist", "blueprint-production", "production-planner.js");
const fixturesPath = join(repoRoot, "mcp-server", "dist", "blueprint-production", "fixtures.js");

if (process.env.MCP_BLUEPRINT_PRODUCTION_LIVE !== "1" || !process.argv.includes("--execute")) {
  console.error("REFUSED: live Blueprint production requires MCP_BLUEPRINT_PRODUCTION_LIVE=1 and --execute.");
  process.exit(2);
}
if (!process.env.MCP_UNREAL_PROJECT_ROOT) {
  console.error("REFUSED: MCP_UNREAL_PROJECT_ROOT must identify the explicitly approved UE4.27 test project.");
  process.exit(2);
}
for (const path of [serverPath, plannerPath, fixturesPath]) {
  if (!existsSync(path)) {
    console.error(`REFUSED: missing built file ${path}. Run the repository gate first.`);
    process.exit(2);
  }
}

const { assertInstallCurrent } = await import("./bridge-install.mjs");
assertInstallCurrent(process.env.MCP_UNREAL_PROJECT_ROOT);
const { planFeature } = await import(pathToFileURL(plannerPath));
const { BLUEPRINT_PRODUCTION_FIXTURES } = await import(pathToFileURL(fixturesPath));
const plans = BLUEPRINT_PRODUCTION_FIXTURES.map(planFeature);
const blocked = plans.filter((plan) => !plan.executable);
if (blocked.length > 0) {
  for (const plan of blocked) console.error(`${plan.feature_id}: ${plan.unsupported.map((item) => item.capability).join(", ")}`);
  console.error("REFUSED: unsupported capabilities must be implemented and independently provable before mutation.");
  process.exit(2);
}

const child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "pipe"], env: { ...process.env, MCP_ENABLE_LEGACY_HTTP: "" } });
let buffer = "";
let nextId = 1;
const pending = new Map();
child.stdout.on("data", (chunk) => {
  buffer += chunk.toString();
  let newline = buffer.indexOf("\n");
  while (newline >= 0) {
    const line = buffer.slice(0, newline).trim();
    buffer = buffer.slice(newline + 1);
    if (line) {
      const message = JSON.parse(line);
      const handler = pending.get(message.id);
      if (handler) { pending.delete(message.id); handler(message); }
    }
    newline = buffer.indexOf("\n");
  }
});

function send(method, params, timeoutMs = 60000) {
  const id = nextId++;
  child.stdin.write(`${JSON.stringify({ jsonrpc: "2.0", id, method, params })}\n`);
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { pending.delete(id); reject(new Error(`${method} timed out`)); }, timeoutMs);
    pending.set(id, (message) => { clearTimeout(timer); resolve(message); });
  });
}

function unwrap(message) {
  const text = message?.result?.content?.[0]?.text;
  if (typeof text !== "string") return { success: false, error: "Tool returned no JSON text", raw: message };
  try { return JSON.parse(text); } catch { return { success: false, error: "Tool returned invalid JSON", raw: text }; }
}

const evidence = { schema_version: 1, started_at: new Date().toISOString(), project: process.env.MCP_UNREAL_PROJECT_ROOT, fixtures: [] };
try {
  await send("initialize", { protocolVersion: "2024-11-05", capabilities: {}, clientInfo: { name: "blueprint-production-suite", version: "1.0.0" } });
  child.stdin.write(`${JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" })}\n`);
  for (const plan of plans) {
    const result = { feature_id: plan.feature_id, plan_hash_sha1: plan.plan_hash_sha1, commands: [], passed: true };
    for (const entry of plan.command_plan) {
      const payload = unwrap(await send("tools/call", { name: entry.command, arguments: entry.params }, 120000));
      result.commands.push({ order: entry.order, command: entry.command, asset_path: entry.asset_path, success: payload.success === true, payload });
      if (payload.success !== true) result.passed = false;
    }
    evidence.fixtures.push(result);
  }
} finally {
  child.stdin.end();
  child.kill();
  evidence.finished_at = new Date().toISOString();
  const artifact = join(tmpdir(), `ue4-blueprint-production-${Date.now()}.json`);
  writeFileSync(artifact, `${JSON.stringify(evidence, null, 2)}\n`, "utf8");
  console.log(`Proof artifact: ${artifact}`);
}

if (evidence.fixtures.some((fixture) => !fixture.passed)) process.exit(1);
