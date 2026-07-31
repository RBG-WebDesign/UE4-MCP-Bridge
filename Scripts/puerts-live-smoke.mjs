#!/usr/bin/env node
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(root, "mcp-server", "dist", "index.js");
const tokenPath = process.env.MCP_PUERTS_TOKEN_PATH;
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
if (!tokenPath || !existsSync(tokenPath)) throw new Error("Set MCP_PUERTS_TOKEN_PATH to the test project's token.txt");

const child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "inherit"], env: process.env });
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
    const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 15000);
    pending.set(id, (message) => { clearTimeout(timer); resolve(message); });
  });
}

async function call(name, args = {}) {
  const message = await send("tools/call", { name, arguments: args });
  const text = message?.result?.content?.[0]?.text;
  if (typeof text !== "string") throw new Error(`${name} returned no JSON content`);
  const result = JSON.parse(text);
  if (!result.success) throw new Error(`${name}: ${result.errors?.join("; ") || result.message}`);
  console.log(`  PASS  ${name}: ${result.message}`);
  return result;
}

try {
  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "puerts-live-smoke", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");
  const listed = await send("tools/list", {});
  const names = new Set((listed?.result?.tools ?? []).map((tool) => tool.name));
  const required = ["puerts_find_actors", "puerts_read_property", "puerts_set_property", "puerts_save", "puerts_undo"];
  if (!required.every((name) => names.has(name))) throw new Error("Consolidated MCP server is missing PuerTS tools");
  console.log("  PASS  one MCP server advertises the PuerTS tools");

  const found = await call("puerts_find_actors", { limit: 50 });
  const actor = found.data.actors.find((candidate) => candidate.class_name !== "WorldSettings") ?? found.data.actors[0];
  if (!actor?.name) throw new Error("The open level has no actor suitable for the milestone");
  const before = await call("puerts_read_property", { actor: actor.name, property: "bHidden" });
  const changed = await call("puerts_set_property", { actor: actor.name, property: "bHidden", value: !before.data.value });
  if (!changed.transaction_id) throw new Error("Property change returned no transaction_id");
  await call("puerts_save", { level_path: "/Game/MCPTests/ConsolidatedMilestone" });
  await call("puerts_undo", { transaction_id: changed.transaction_id });
  const after = await call("puerts_read_property", { actor: actor.name, property: "bHidden" });
  if (after.data.value !== before.data.value) throw new Error("Undo did not restore the actor property");
  await call("puerts_save");
  console.log("\nConsolidated UE4.27 PuerTS milestone passed.");

  if (process.argv.includes("--physics")) {
    const actors = [
      { name: "MCP_Physics_Floor", mesh: "/Engine/BasicShapes/Cube.Cube", location: { x: 0, y: 0, z: -50 }, scale: { x: 12, y: 8, z: 0.5 } },
      { name: "MCP_Physics_Ramp", mesh: "/Engine/BasicShapes/Cube.Cube", location: { x: -450, y: -250, z: 125 }, rotation: { pitch: -18 }, scale: { x: 4, y: 2, z: 0.2 } },
      { name: "MCP_Physics_Backstop", mesh: "/Engine/BasicShapes/Cube.Cube", location: { x: 700, y: 0, z: 100 }, scale: { x: 0.25, y: 8, z: 1.5 } },
    ];
    for (let index = 0; index < 24; index += 1) {
      actors.push({
        name: "MCP_Physics_Domino_" + String(index).padStart(2, "0"),
        mesh: "/Engine/BasicShapes/Cube.Cube",
        location: { x: -150 + index * 28, y: 0, z: 75 },
        rotation: { pitch: index === 0 ? 18 : 0 },
        scale: { x: 0.14, y: 0.6, z: 1.5 },
        simulate_physics: true,
        mass_kg: 2,
        linear_damping: 0.05,
        angular_damping: 0.05,
      });
    }
    for (let index = 0; index < 4; index += 1) {
      actors.push({
        name: "MCP_Physics_Sphere_" + index,
        mesh: "/Engine/BasicShapes/Sphere.Sphere",
        location: { x: -650 - index * 90, y: -250 + index * 80, z: 350 + index * 80 },
        scale: { x: 0.7, y: 0.7, z: 0.7 },
        simulate_physics: true,
        mass_kg: 8,
      });
      actors.push({
        name: "MCP_Physics_Stack_" + index,
        mesh: "/Engine/BasicShapes/Cube.Cube",
        location: { x: 350, y: 260, z: 30 + index * 65 },
        scale: { x: 0.6, y: 0.6, z: 0.6 },
        simulate_physics: true,
        mass_kg: 5,
      });
    }

    const built = await call("puerts_physics_build", { actors });
    if (built.data.count !== actors.length) throw new Error("Physics builder returned the wrong actor count");
    await call("puerts_save", { level_path: "/Game/MCPTests/PuerTSPhysicsStress" });
    const labels = actors.filter((actor) => actor.simulate_physics).map((actor) => actor.name);
    const screenshot = await call("puerts_viewport_screenshot", {
      actors: actors.map((actor) => actor.name),
      filename: "puerts_physics_stress.png",
    });
    for (let attempt = 0; attempt < 30 && !existsSync(screenshot.data.filepath); attempt += 1) {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    if (!existsSync(screenshot.data.filepath)) throw new Error("Viewport screenshot was not written");
    console.log("  PASS  viewport PNG: " + screenshot.data.filepath);
    const beforePhysics = await call("puerts_physics_observe", { actors: labels });
    const beforeByLabel = new Map(beforePhysics.data.actors.map((actor) => [actor.label, actor]));
    await call("puerts_pie_start");
    await new Promise((resolve) => setTimeout(resolve, 4000));
    let duringPhysics;
    try {
      duringPhysics = await call("puerts_physics_observe", { actors: labels });
    } finally {
      await call("puerts_pie_stop");
    }
    const moved = duringPhysics.data.actors.filter((actor) => {
      const beforeActor = beforeByLabel.get(actor.label);
      if (!beforeActor) return false;
      const dx = actor.location.x - beforeActor.location.x;
      const dy = actor.location.y - beforeActor.location.y;
      const dz = actor.location.z - beforeActor.location.z;
      return Math.hypot(dx, dy, dz) > 5;
    });
    if (duringPhysics.data.world !== "pie") throw new Error("Physics observer did not read the PIE world");
    if (moved.length < 8) throw new Error("Only " + moved.length + " rigid bodies moved");
    console.log("\nPhysics stress passed: " + actors.length + " actors, " + moved.length + " moving rigid bodies observed in PIE.");
  }
} finally {
  child.stdin.end();
  child.kill();
}
