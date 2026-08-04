#!/usr/bin/env node
// Live acceptance for FP-6 native Sound Cue authoring.
//
// Warm phase, against an already-running UE4.27 editor:
//   node Scripts/audio-build-acceptance.mjs
// Restart that editor yourself, then run:
//   node Scripts/audio-build-acceptance.mjs --phase=cold
//
// This script never launches or closes Unreal and never starts PIE. The release
// plan's playback check remains user-gated: after warm and cold pass, a human
// may place the saved cue in an Audio Component and authorize PIE separately.
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
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

const phase = process.argv.includes("--phase=cold") ? "cold" : "warm";
const evidenceDir = join(repoRoot, "docs", "evidence");
const evidencePath = join(evidenceDir, "audio-build.json");
const wave = "/Engine/EditorSounds/Notifications/CompileSuccess.CompileSuccess";
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
function assetFile(packagePath) {
  return join(projectRoot, "Content", packagePath.slice("/Game/".length).replaceAll("/", "\\")) + ".uasset";
}
function sha(path) {
  return createHash("sha256").update(readFileSync(path)).digest("hex");
}
function nodeById(inspection, id) {
  return (inspection?.data?.audio?.sound_nodes ?? []).find((node) => node.id === id);
}

try {
  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "audio-build-acceptance", version: "1.0.0" },
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  if (phase === "cold") {
    const evidence = JSON.parse(readFileSync(evidencePath, "utf8"));
    const inspected = await call("puerts_audio_inspect", { asset_path: evidence.asset_path });
    check(inspected.success === true, "cold audio_inspect loads the saved cue");
    check(inspected.data?.structure_hash_sha1 === evidence.structure_hash_sha1,
      "cold structure hash matches the warm verified hash");
    check(inspected.data?.audio?.first_node === "root", "cold read preserves FirstNode");
    check(nodeById(inspected, "root")?.children?.[0] === "wave", "cold read preserves child order");
    check(nodeById(inspected, "wave")?.sound_wave === wave, "cold read preserves the Sound Wave reference");
  } else {
    const suffix = Date.now().toString(36);
    const assetPath = "/Game/MCPGenerated/FP6_Audio_" + suffix;
    const file = assetFile(assetPath);
    check(!existsSync(file), "fresh Sound Cue fixture file is unused");
    const preflight = await call("puerts_graph_inspect", { asset_path: assetPath });
    check(preflight.success === false, "graph_inspect fails before fixture creation");

    const controlSpec = {
      asset_path: assetPath,
      first_node: "wave",
      nodes: [{ id: "wave", type: "wave_player", sound_wave: wave }],
    };
    const control = await call("puerts_audio_build", controlSpec);
    check(control.success === true && control.data?.created === true,
      "state-moving control creates a Sound Cue");
    check(existsSync(file), "state-moving control writes a .uasset");
    const controlFileHash = sha(file);
    const controlRead = await call("puerts_audio_inspect", { asset_path: assetPath });
    check(controlRead.success === true && controlRead.data?.sound_node_count === 1,
      "independent inspector reads the control graph");
    check(nodeById(controlRead, "wave")?.sound_wave === wave,
      "independent inspector reads the control wave reference");

    const desired = {
      asset_path: assetPath,
      first_node: "root",
      nodes: [
        {
          id: "root",
          type: "modulator",
          children: ["wave"],
          properties: { PitchMin: 0.9, PitchMax: 1.1, VolumeMin: 0.8, VolumeMax: 1.0 },
        },
        { id: "wave", type: "wave_player", sound_wave: wave, properties: { bLooping: false } },
      ],
    };
    const beforePlanHash = sha(file);
    const planned = await call("puerts_audio_build", { ...desired, plan_only: true });
    check(planned.success === true && planned.data?.expected_change_count === 1,
      "plan_only reports one desired-state change");
    check(sha(file) === beforePlanHash, "plan_only does not change the saved asset");

    const built = await call("puerts_audio_build", desired);
    check(built.success === true && built.data?.verified === true && built.data?.saved === true,
      "desired-state update is independently verified before save");
    check(sha(file) !== controlFileHash, "desired-state update moves saved file state");
    const inspected = await call("puerts_audio_inspect", { asset_path: assetPath });
    check(inspected.success === true && inspected.data?.audio?.first_node === "root",
      "audio_inspect reads the updated FirstNode");
    check(nodeById(inspected, "root")?.children?.[0] === "wave",
      "audio_inspect reads the updated child order");
    check(nodeById(inspected, "wave")?.sound_wave === wave,
      "audio_inspect reads the updated wave reference");

    const convergedFileHash = sha(file);
    const converged = await call("puerts_audio_build", desired);
    check(converged.success === true && converged.data?.unchanged === true,
      "a repeated desired state converges without mutation");
    check(sha(file) === convergedFileHash, "a converged rerun does not rewrite the .uasset");

    const stableStructureHash = inspected.data?.structure_hash_sha1;
    const refused = await call("puerts_audio_build", {
      ...desired,
      nodes: [{ id: "root", type: "modulator", children: ["missing"] }],
    });
    check(refused.success === false, "an invalid graph is refused");
    const afterRefusal = await call("puerts_audio_inspect", { asset_path: assetPath });
    check(afterRefusal.data?.structure_hash_sha1 === stableStructureHash,
      "a refused graph leaves the independently read structure unchanged");
    check(sha(file) === convergedFileHash, "a refused graph leaves saved bytes unchanged");

    mkdirSync(evidenceDir, { recursive: true });
    writeFileSync(evidencePath, JSON.stringify({
      asset_path: assetPath,
      asset_file: file,
      structure_hash_sha1: stableStructureHash,
      file_sha256: convergedFileHash,
      node_count: inspected.data?.sound_node_count,
      playback_gate: "Not run. Requires explicit user authorization to start PIE.",
    }, null, 2));
  }

  requireCurrentInstall();
  if (failures.length > 0) throw new Error(failures.length + " acceptance check(s) failed");
  console.log("all audio build checks passed (" + phase + " phase)");
} catch (error) {
  console.error(error);
  process.exitCode = 1;
} finally {
  child.stdin.end();
}