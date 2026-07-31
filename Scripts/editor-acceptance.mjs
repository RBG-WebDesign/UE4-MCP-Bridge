#!/usr/bin/env node
import { spawn } from "node:child_process";
import { existsSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
const reportPath = process.env.MCP_ACCEPTANCE_REPORT;
if (!existsSync(serverPath)) throw new Error(`Server not built: ${serverPath}`);

const child = spawn(process.execPath, [serverPath], {
  stdio: ["pipe", "pipe", "pipe"],
  env: process.env,
});
let stderr = "";
child.stderr.on("data", (chunk) => { stderr += chunk.toString(); });

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
    if (resolve) {
      pending.delete(message.id);
      resolve(message);
    }
  }
});

function send(method, params, timeoutMs = 60000) {
  const id = nextId++;
  child.stdin.write(`${JSON.stringify({ jsonrpc: "2.0", id, method, params })}\n`);
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`${method} timed out after ${timeoutMs}ms`));
    }, timeoutMs);
    pending.set(id, (message) => {
      clearTimeout(timer);
      resolve(message);
    });
  });
}

function unwrap(message) {
  const text = message?.result?.content?.[0]?.text;
  if (typeof text !== "string") return null;
  try { return JSON.parse(text); } catch { return null; }
}

const steps = [];
function pass(name, detail = "") {
  steps.push({ name, success: true, detail });
  console.log(`  PASS  ${name}${detail ? ` - ${detail}` : ""}`);
}

async function tool(name, args = {}, timeoutMs = 60000) {
  const payload = unwrap(await send("tools/call", { name, arguments: args }, timeoutMs));
  if (!payload?.success) {
    const error = payload?.error ?? payload?.errors?.join("; ") ?? "missing response payload";
    throw new Error(`${name}: ${error}`);
  }
  return payload;
}

async function optionalTool(name, args = {}) {
  return unwrap(await send("tools/call", { name, arguments: args }));
}

function pythonString(payload) {
  const value = payload?.data?.result;
  if (typeof value !== "string" || value.length < 2) throw new Error("python_proxy did not return a string");
  return value.slice(1, -1).replaceAll("\\\\", "\\").replaceAll("\\'", "'");
}

const actorNames = [
  "MCPAcceptance_Cube",
  "MCPAcceptance_PointLight",
  "MCPAcceptance_Sphere",
  "MCPAcceptance_CineCamera",
];

async function cleanupActors() {
  for (const actorName of actorNames) {
    const found = await optionalTool("level_actors", { name_filter: actorName, limit: 2 });
    if ((found?.data?.count ?? 0) > 0) {
      await optionalTool("actor_delete", { actor_name: actorName });
    }
  }
}

async function main() {
  console.log("UE4.27 MCP editor acceptance");
  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "editor-acceptance", version: "1.0.0" },
  });
  child.stdin.write(`${JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" })}\n`);

  const listed = await send("tools/list", {});
  const tools = listed?.result?.tools ?? [];
  if (tools.length < 187) throw new Error(`Expected at least 187 tools, got ${tools.length}`);
  pass("tool discovery", `${tools.length} tools`);

  const project = await tool("project_info");
  const actors = await tool("level_actors", { include_transforms: true });
  pass("level query", `${project.data?.current_level ?? project.data?.level ?? "open level"}, ${actors.data?.count ?? 0} actors`);

  await cleanupActors();

  const cube = await tool("actor_spawn", {
    asset_path: "/Engine/BasicShapes/Cube.Cube",
    location: { x: 0, y: 0, z: 100 },
    name: "MCPAcceptance_Cube",
  });
  pass("spawn cube", cube.data?.name ?? "MCPAcceptance_Cube");
  await tool("viewport_focus", { actor_name: "MCPAcceptance_Cube" });
  const selection = await tool("actor_selection");
  if (selection.data?.count !== 1 || selection.data?.actors?.[0]?.name !== "MCPAcceptance_Cube") {
    throw new Error("actor_selection did not return the focused cube");
  }
  pass("selection state", JSON.stringify(selection.data.actors[0].location));

  const modifiedCube = await tool("actor_modify", {
    actor_name: "MCPAcceptance_Cube",
    scale: { x: 2, y: 2, z: 2 },
    rotation: { pitch: 45, yaw: 0, roll: 0 },
  });
  if (!modifiedCube.data?.validation?.valid) throw new Error("cube transform validation failed");
  pass("modify cube transform");

  const light = await tool("puerts_spawn_actor", {
    class_path: "/Script/Engine.PointLight",
    location: { x: 0, y: 0, z: 300 },
  });
  const lightPath = light.data?.actor?.path;
  await tool("puerts_call_function", {
    actor: lightPath,
    function: "Actor.SetActorLabel",
    arguments: ["MCPAcceptance_PointLight"],
  });
  const componentResult = await tool("python_proxy", {
    code: "[c.get_path_name() for a in unreal.EditorLevelLibrary.get_all_level_actors() if a.get_actor_label() == 'MCPAcceptance_PointLight' for c in a.get_components_by_class(unreal.PointLightComponent)][0]",
  });
  const lightComponentPath = pythonString(componentResult);
  await tool("puerts_set_property", { object_path: lightComponentPath, property: "Intensity", value: 5000 });
  await tool("puerts_set_property", {
    object_path: lightComponentPath,
    property: "LightColor",
    value: { r: 255, g: 0, b: 0, a: 255 },
  });
  const intensity = await tool("puerts_read_property", { object_path: lightComponentPath, property: "Intensity" });
  const color = await tool("puerts_read_property", { object_path: lightComponentPath, property: "LightColor" });
  if (intensity.data?.value !== 5000) throw new Error(`light intensity was ${intensity.data?.value}`);
  pass("reflected point light properties", JSON.stringify(color.data?.value));

  const assets = await tool("asset_list", { path: "/Game/", asset_type: "StaticMesh", recursive: true });
  pass("asset query", `${assets.data?.count ?? assets.data?.assets?.length ?? 0} /Game StaticMesh assets`);

  const materialPath = "/Game/MCPAcceptance/MI_TestMaterial";
  const existingMaterial = await optionalTool("asset_info", { path: materialPath });
  if (!existingMaterial?.success) {
    await tool("material_instance_create", {
      path: "/Game/MCPAcceptance",
      name: "MI_TestMaterial",
      parent_material: "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial",
    });
    pass("create material instance", materialPath);
  } else {
    pass("material instance available", materialPath);
  }

  await tool("actor_spawn", {
    asset_path: "/Engine/BasicShapes/Sphere.Sphere",
    location: { x: 100, y: 0, z: 0 },
    name: "MCPAcceptance_Sphere",
  });
  await tool("material_apply", {
    actor_name: "MCPAcceptance_Sphere",
    material_path: materialPath,
    slot_index: 0,
  });
  pass("assign material to sphere");

  await tool("python_proxy", { code: "unreal.log('MCP Test Connected Successfully')" });
  const logs = await tool("puerts_get_logs", { maximum_lines: 200 });
  if (!logs.log_output?.some((line) => line.includes("MCP Test Connected Successfully"))) {
    throw new Error("PuerTS log capture did not contain the Python test marker");
  }
  pass("Python execution and PuerTS log capture");

  const blueprintPath = "/Game/MCPAcceptance/BP_TestActor";
  const existingBlueprint = await optionalTool("asset_info", { path: blueprintPath });
  if (!existingBlueprint?.success) {
    await tool("blueprint_create", { name: "BP_TestActor", path: "/Game/MCPAcceptance", parent_class: "Actor" });
    pass("create Blueprint", blueprintPath);
  } else {
    pass("Blueprint available", blueprintPath);
  }
  const compiled = await tool("blueprint_compile", { blueprint_path: blueprintPath });
  if (compiled.data?.had_errors) throw new Error("BP_TestActor compiled with errors");
  pass("compile Blueprint");

  const camera = await tool("puerts_spawn_actor", {
    class_path: "/Script/CinematicCamera.CineCameraActor",
    location: { x: 600, y: 600, z: 400 },
    rotation: { pitch: -25.24, yaw: -135, roll: 0 },
  });
  await tool("puerts_call_function", {
    actor: camera.data?.actor?.path,
    function: "Actor.SetActorLabel",
    arguments: ["MCPAcceptance_CineCamera"],
  });
  await tool("viewport_fit", { actor_names: actorNames, padding: 1.4 });
  await tool("viewport_look_at", { location: [0, 0, 0] });
  const screenshot = await tool("viewport_screenshot", { filename: "mcp_acceptance.png" });
  pass("CineCamera and viewport screenshot", screenshot.data?.filepath ?? "captured");

  await tool("gameplay_pie_start", {});
  await new Promise((resolve) => setTimeout(resolve, 3000));
  const pie = await tool("gameplay_pie_status", {});
  await tool("gameplay_pie_stop", {});
  let stopped = false;
  for (let attempt = 0; attempt < 20; attempt++) {
    await new Promise((resolve) => setTimeout(resolve, 250));
    const status = await tool("gameplay_pie_status", {});
    if (!status.data?.is_running) {
      stopped = true;
      break;
    }
  }
  if (!stopped) throw new Error("PIE did not stop within 5 seconds");
  pass("PIE start, observe, stop", JSON.stringify(pie.data?.status ?? pie.data?.is_playing ?? true));

  await tool("puerts_save", { level_path: "/Game/MCPTests/FullAcceptance" });
  pass("save level and dirty assets");

  const hiddenBefore = await tool("puerts_read_property", { actor: "MCPAcceptance_Cube", property: "bHidden" });
  const hiddenTarget = !Boolean(hiddenBefore.data?.value);
  const change = await tool("puerts_set_property", {
    actor: "MCPAcceptance_Cube",
    property: "bHidden",
    value: hiddenTarget,
  });
  await tool("puerts_save", {});
  await tool("puerts_undo", { transaction_id: change.transaction_id });
  const hiddenAfter = await tool("puerts_read_property", { actor: "MCPAcceptance_Cube", property: "bHidden" });
  if (Boolean(hiddenAfter.data?.value) !== Boolean(hiddenBefore.data?.value)) {
    throw new Error("PuerTS undo did not restore bHidden");
  }
  await tool("puerts_save", {});
  pass("save and exact transaction undo", change.transaction_id);

  await tool("actor_delete", { actor_name: "MCPAcceptance_Cube" });
  await tool("actor_delete", { actor_name: "MCPAcceptance_PointLight" });
  await tool("actor_delete", { actor_name: "MCPAcceptance_Sphere" });
  await tool("actor_delete", { actor_name: "MCPAcceptance_CineCamera" });
  await tool("level_save", { save_all: false });
  pass("actor cleanup", actorNames.join(", "));

  const report = { success: true, tool_count: tools.length, steps, stderr: stderr.trim() };
  if (reportPath) writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
  console.log(`\n${steps.length} passed, 0 failed`);
}

main().catch(async (error) => {
  try { await optionalTool("gameplay_pie_stop", {}); } catch {}
  const report = { success: false, error: error.message, steps, stderr: stderr.trim() };
  if (reportPath) writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
  console.error(`\nFAIL: ${error.message}`);
  process.exitCode = 1;
}).finally(() => {
  child.stdin.end();
  child.kill();
});
