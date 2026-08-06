#!/usr/bin/env node
// Live acceptance for FP-5 native level create, load and save.
//
// Run only against a clean editor:
//   node Scripts/level-lifecycle-acceptance.mjs
//   restart the editor, then:
//   node Scripts/level-lifecycle-acceptance.mjs --phase=cold
//
// The record phase uses fresh map paths, proves a state-moving control, proves
// dirty-state refusal leaves the loaded map unchanged, exercises template_path
// and save_all, then writes the paths needed by the cold phase. It never starts
// PIE and never deletes assets.
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";
import { requireCurrentInstall } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
const projectRoot = requireCurrentInstall();
delete process.env.MCP_PUERTS_PIPE;
delete process.env.MCP_PUERTS_TOKEN_PATH;
delete process.env.MCP_PUERTS_TOKEN;

const phase = process.argv.includes("--phase=cold") ? "cold" : "record";
const evidenceDir = join(repoRoot, "docs", "evidence");
const fixturePath = join(evidenceDir, "level-lifecycle.json");
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
    const finish = pending.get(message.id);
    if (finish) {
      pending.delete(message.id);
      finish(message);
    }
  }
});
function send(method, params) {
  const id = nextId++;
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(method + " timed out")), 90000);
    pending.set(id, (message) => {
      clearTimeout(timer);
      resolve(message);
    });
  });
}
async function call(name, args = {}) {
  const message = await send("tools/call", { name, arguments: args });
  const text = message?.result?.content?.[0]?.text;
  if (typeof text !== "string") throw new Error(name + " returned no JSON content");
  return JSON.parse(text);
}
const failures = [];
function check(condition, label, detail = "") {
  if (condition) {
    console.log("  PASS  " + label);
    return;
  }
  failures.push(label);
  console.log("  FAIL  " + label + (detail ? ": " + detail : ""));
}
function mapFile(packagePath) {
  return join(projectRoot, "Content", packagePath.slice("/Game/".length).replaceAll("/", "\\")) + ".umap";
}
function sha(path) {
  return createHash("sha256").update(readFileSync(path)).digest("hex");
}
function labels(scene) {
  return (scene?.data?.actors ?? []).map((actor) => actor.label);
}

try {
  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "level-lifecycle-acceptance", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    const fixture = JSON.parse(readFileSync(fixturePath, "utf8"));
    const loadedBase = await call("puerts_level_load", { level_path: fixture.base });
    check(loadedBase.success === true, "cold load restores the base map");
    const baseScene = await call("puerts_scene_inspect", { level_path: fixture.base });
    check(baseScene.success === true && labels(baseScene).includes("FP5_BaseMarker"),
      "cold read-back finds the saved base marker");

    const loadedCopy = await call("puerts_level_load", { level_path: fixture.copy });
    check(loadedCopy.success === true, "cold load restores the template copy");
    const copyScene = await call("puerts_scene_inspect", { level_path: fixture.copy });
    check(copyScene.success === true
      && labels(copyScene).includes("FP5_BaseMarker")
      && labels(copyScene).includes("FP5_CopyMarker"),
      "cold read-back proves template content and later edits both persisted");
  } else {
    const suffix = Date.now().toString(36);
    const base = "/Game/MCPGenerated/FP5_Level_" + suffix;
    const guard = base + "_Guard";
    const copy = base + "_Copy";
    const paths = [base, guard, copy];

    for (const path of paths) {
      check(!existsSync(mapFile(path)), "fresh fixture file is unused: " + path);
      const preflight = await call("puerts_graph_inspect", { asset_path: path });
      check(preflight.success === false, "graph_inspect fails before fixture creation: " + path);
    }

    const created = await call("puerts_level_create", { level_path: base });
    check(created.success === true && created.data?.level === base,
      "level_create creates, saves and loads a fresh map");
    check(existsSync(mapFile(base)), "the created .umap exists on disk");

    const blankHash = sha(mapFile(base));
    const placed = await call("puerts_scene_batch", {
      level_path: base,
      operations: [{
        op: "upsert_actor",
        label: "FP5_BaseMarker",
        class: "/Script/Engine.PointLight",
        location: { x: 120, y: 240, z: 360 },
      }],
      verify: true,
    });
    check(placed.success === true && placed.data?.applied_operation_count === 1,
      "state-moving control adds one actor to the new map");

    const saved = await call("puerts_level_save", { save_all: false });
    check(saved.success === true && saved.data?.save_all === false,
      "level_save persists the current map without broad asset saving");
    check(sha(mapFile(base)) !== blankHash,
      "the state-moving control changed the saved map bytes");

    const createdGuard = await call("puerts_level_create", { level_path: guard });
    check(createdGuard.success === true, "a second clean map is created for dirty refusal");
    const dirtiedGuard = await call("puerts_scene_batch", {
      level_path: guard,
      operations: [{
        op: "upsert_actor",
        label: "FP5_DirtyGuard",
        class: "/Script/Engine.TargetPoint",
        location: { x: 10, y: 20, z: 30 },
      }],
      verify: true,
    });
    check(dirtiedGuard.success === true, "the refusal control dirties the loaded guard map");

    const refused = await call("puerts_level_load", { level_path: base });
    check(refused.success === false
      && JSON.stringify(refused.errors).includes("unsaved"),
      "level_load refuses to discard unsaved packages");
    const stillGuard = await call("puerts_scene_inspect", { level_path: guard });
    check(stillGuard.success === true && labels(stillGuard).includes("FP5_DirtyGuard"),
      "a refused load leaves the dirty map and its state intact");

    const guardSaved = await call("puerts_level_save", { save_all: false });
    check(guardSaved.success === true, "saving the guard clears the load refusal");
    const loadedBase = await call("puerts_level_load", { level_path: base });
    check(loadedBase.success === true && loadedBase.data?.already_loaded === false,
      "level_load switches to the saved base map");
    const baseScene = await call("puerts_scene_inspect", { level_path: base });
    check(baseScene.success === true && labels(baseScene).includes("FP5_BaseMarker"),
      "independent scene inspection reads the saved base marker");

    const copied = await call("puerts_level_create", {
      level_path: copy,
      template_path: base,
    });
    check(copied.success === true && copied.data?.template === base,
      "level_create preserves template_path and loads the copy");
    const copiedScene = await call("puerts_scene_inspect", { level_path: copy });
    check(copiedScene.success === true && labels(copiedScene).includes("FP5_BaseMarker"),
      "the template copy contains the base map marker");

    const copyHash = sha(mapFile(copy));
    const editedCopy = await call("puerts_scene_batch", {
      level_path: copy,
      operations: [{
        op: "upsert_actor",
        label: "FP5_CopyMarker",
        class: "/Script/Engine.TargetPoint",
        location: { x: 40, y: 50, z: 60 },
      }],
      verify: true,
    });
    check(editedCopy.success === true, "the save_all control dirties the template copy");
    const savedAll = await call("puerts_level_save", { save_all: true });
    check(savedAll.success === true && savedAll.data?.save_all === true,
      "level_save preserves save_all=true");
    check(sha(mapFile(copy)) !== copyHash, "save_all persisted the copy edit");

    mkdirSync(evidenceDir, { recursive: true });
    writeFileSync(fixturePath, JSON.stringify({
      base,
      guard,
      copy,
      base_file: mapFile(base),
      copy_file: mapFile(copy),
      base_size: statSync(mapFile(base)).size,
      copy_size: statSync(mapFile(copy)).size,
    }, null, 2));
  }

  requireCurrentInstall();
  if (failures.length > 0) {
    throw new Error(failures.length + " acceptance check(s) failed");
  }
  console.log("all level lifecycle checks passed (" + phase + " phase)");
} catch (error) {
  console.error(error);
  process.exitCode = 1;
} finally {
  child.stdin.end();
}