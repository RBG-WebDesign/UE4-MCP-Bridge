#!/usr/bin/env node
import { spawn } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const child = spawn(process.execPath, [join(repoRoot, "mcp-server", "dist", "index.js")], {
  stdio: ["pipe", "pipe", "pipe"],
  env: process.env,
});
let buffer = "";
let nextId = 1;
const pending = new Map();
child.stdout.on("data", (chunk) => {
  buffer += chunk.toString();
  let newline;
  while ((newline = buffer.indexOf("\n")) >= 0) {
    const line = buffer.slice(0, newline).trim();
    buffer = buffer.slice(newline + 1);
    if (!line) continue;
    let message;
    try { message = JSON.parse(line); } catch { continue; }
    const resolve = pending.get(message.id);
    if (resolve) { pending.delete(message.id); resolve(message); }
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
  if (typeof text !== "string") return null;
  try { return JSON.parse(text); } catch { return null; }
}
async function call(name, args = {}) {
  const result = unwrap(await send("tools/call", { name, arguments: args }));
  if (!result?.success) throw new Error(`${name}: ${result?.error ?? result?.errors?.join("; ") ?? "failed"}`);
  return result;
}
async function optional(name, args = {}) {
  return unwrap(await send("tools/call", { name, arguments: args }));
}
const v = (x, y, z) => ({ x, y, z });
const r = (pitch = 0, yaw = 0, roll = 0) => ({ pitch, yaw, roll });
const spawns = [];
function cube(name, x, y, z, sx, sy, sz, material = "stone") {
  spawns.push({ asset_path: "/Engine/BasicShapes/Cube.Cube", location: v(x, y, z), scale: v(sx, sy, sz), rotation: r(), name, folder: "MCP Castle" });
  materials[material].push(name);
}
function cylinder(name, x, y, z, sx, sy, sz, material = "stone") {
  spawns.push({ asset_path: "/Engine/BasicShapes/Cylinder.Cylinder", location: v(x, y, z), scale: v(sx, sy, sz), rotation: r(), name, folder: "MCP Castle" });
  materials[material].push(name);
}
function cone(name, x, y, z, sx, sy, sz, material = "stone") {
  spawns.push({ asset_path: "/Engine/BasicShapes/Cone.Cone", location: v(x, y, z), scale: v(sx, sy, sz), rotation: r(), name, folder: "MCP Castle" });
  materials[material].push(name);
}
const materials = { stone: [], water: [], grass: [], wood: [] };

// Ground, water, bridge, and gate.
cube("MCP_Castle_Ground", 0, 0, -70, 18, 14, 0.2, "grass");
cube("MCP_Castle_MoatNorth", 0, 700, -20, 10, 1.2, 0.15, "water");
cube("MCP_Castle_MoatSouth", 0, -700, -20, 10, 1.2, 0.15, "water");
cube("MCP_Castle_MoatWest", -850, 0, -20, 1.2, 5.8, 0.15, "water");
cube("MCP_Castle_MoatEast", 850, 0, -20, 1.2, 5.8, 0.15, "water");
cube("MCP_Castle_Drawbridge", 0, -560, 8, 2.0, 1.4, 0.15, "wood");
cube("MCP_Castle_Gate", 0, -430, 120, 1.5, 0.2, 2.0, "wood");

// Keep, curtain walls, and towers.
cube("MCP_Castle_Keep", 0, 0, 220, 3.2, 2.8, 4.4);
cone("MCP_Castle_KeepRoof", 0, 0, 500, 3.8, 3.4, 1.5);
cube("MCP_Castle_WallNorth", 0, 420, 180, 8, 0.7, 3.2);
cube("MCP_Castle_WallWest", -400, 0, 180, 0.7, 8, 3.2);
cube("MCP_Castle_WallEast", 400, 0, 180, 0.7, 8, 3.2);
cube("MCP_Castle_WallSouthWest", -280, -420, 180, 2.4, 0.7, 3.2);
cube("MCP_Castle_WallSouthEast", 280, -420, 180, 2.4, 0.7, 3.2);
for (const [x, y, label] of [[-400, -420, "SW"], [400, -420, "SE"], [-400, 420, "NW"], [400, 420, "NE"]]) {
  cylinder(`MCP_Castle_Tower${label}`, x, y, 220, 1.4, 1.4, 4.4);
  cone(`MCP_Castle_TowerRoof${label}`, x, y, 480, 1.7, 1.7, 1.8);
}

// Gatehouse and battlements.
cube("MCP_Castle_GatePillarL", -100, -420, 170, 0.8, 0.8, 3.0);
cube("MCP_Castle_GatePillarR", 100, -420, 170, 0.8, 0.8, 3.0);
cube("MCP_Castle_GateLintel", 0, -420, 315, 2.4, 0.8, 0.6);
for (const [i, x] of [-300, -150, 0, 150, 300].entries()) cube(`MCP_Castle_BattlementN${i}`, x, 420, 370, 0.5, 0.9, 0.5);
for (const [i, x] of [-520, -370, 370, 520].entries()) cube(`MCP_Castle_BattlementS${i}`, x, -420, 370, 0.5, 0.9, 0.5);
for (const [i, y] of [-300, -150, 0, 150, 300].entries()) {
  cube(`MCP_Castle_BattlementW${i}`, -400, y, 370, 0.9, 0.5, 0.5);
  cube(`MCP_Castle_BattlementE${i}`, 400, y, 370, 0.9, 0.5, 0.5);
}

async function main() {
  await send("initialize", { protocolVersion: "2024-11-05", capabilities: {}, clientInfo: { name: "castle-builder", version: "1.0.0" } });
  child.stdin.write(`${JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" })}\n`);
  await optional("actor_delete", { actor_name: "MCP_Castle_*" });
  const defs = ["MI_CastleStone", "MI_CastleWater", "MI_CastleGrass", "MI_CastleWood"];
  for (const name of defs) {
    const path = `/Game/MCPTests/${name}`;
    if (!(await optional("asset_info", { path }))?.success) {
      await call("material_instance_create", { path: "/Game/MCPTests", name, parent_material: "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial" });
    }
  }
  await call("batch_spawn", { spawns });
  const materialPaths = { stone: "/Game/MCPTests/MI_CastleStone", water: "/Game/MCPTests/MI_CastleWater", grass: "/Game/MCPTests/MI_CastleGrass", wood: "/Game/MCPTests/MI_CastleWood" };
  for (const [kind, names] of Object.entries(materials)) {
    for (const actor_name of names) await call("material_apply", { actor_name, material_path: materialPaths[kind], slot_index: 0 });
  }
  await call("puerts_save", { assets: Object.values(materialPaths), level_path: "/Game/MCPTests/FullAcceptance" });
  await call("viewport_fit", { actor_names: ["MCP_Castle_Keep", "MCP_Castle_TowerSW", "MCP_Castle_TowerNE", "MCP_Castle_MoatNorth", "MCP_Castle_MoatSouth"], padding: 1.25 });
  await call("viewport_screenshot", { filename: "mcp_castle_moat.png" });
  const result = await call("level_actors", { folder_filter: "MCP Castle", include_transforms: false, limit: 200 });
  console.log(JSON.stringify({ success: true, spawned: spawns.length, castle_actors: result.data?.count ?? 0, materials: materialPaths }, null, 2));
}
main().catch((error) => { console.error(JSON.stringify({ success: false, error: error.message }, null, 2)); process.exitCode = 1; }).finally(() => { child.stdin.end(); child.kill(); });
