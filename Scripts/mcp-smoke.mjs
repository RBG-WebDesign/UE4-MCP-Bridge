#!/usr/bin/env node
/**
 * MCP smoke test for the Unreal bridge server.
 *
 * Speaks JSON-RPC over stdio directly to mcp-server/dist/index.js, the same way
 * Claude Code, Codex and Gemini do. No MCP SDK import, no Inspector arg parsing:
 * if this passes, a real client can drive the server.
 *
 * Checks, in order:
 *   1. initialize            - the server handshakes and reports its info
 *   2. tools/list            - it advertises tools, and every one has annotations
 *   3. engine_source_search  - server-local UE4.27 source access (no editor needed)
 *   4. native-only catalog   - no legacy HTTP tools are advertised by default
 *   5. puerts_diagnostic     - authenticated named-pipe editor link, if UE4 is up
 *
 * Step 5 is reported as SKIP, not FAIL, when the editor is closed, so this is
 * usable in CI and with Unreal shut down. No HTTP endpoint is contacted.
 *
 * Usage:
 *   node Scripts/mcp-smoke.mjs
 *   node Scripts/mcp-smoke.mjs --require-editor    (turn the SKIP into a FAIL)
 */

import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { existsSync } from "node:fs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
const requireEditor = process.argv.includes("--require-editor");

// --require-editor is the mode that actually talks to Unreal, so it is the mode
// that has to know which plugin Unreal is running. The default mode skips the
// editor check entirely and is part of npm run verify, where no test project is
// configured, so gating it there would fail the offline gate for no reason.
if (requireEditor && process.env.MCP_UNREAL_PROJECT_ROOT && process.env.MCP_SKIP_INSTALL_CHECK !== "1") {
  const { assertInstallCurrent } = await import("./bridge-install.mjs");
  assertInstallCurrent(process.env.MCP_UNREAL_PROJECT_ROOT);
}

const PROTOCOL_VERSION = "2024-11-05";
const results = [];
function record(name, ok, detail) {
  results.push({ name, ok, detail });
  const tag = ok === "skip" ? "SKIP" : ok ? "PASS" : "FAIL";
  console.log(`  ${tag}  ${name}${detail ? ` - ${detail}` : ""}`);
}

if (!existsSync(serverPath)) {
  console.error(`Server not built: ${serverPath}\nRun: npm run build`);
  process.exit(1);
}

// UE_ENGINE_ROOT lets engine_source_* find the installed engine. The server
// falls back to the .uproject EngineAssociation, which does not exist in a
// bridge-only clone, so pass it explicitly when the caller has it set.
const childEnv = { ...process.env };
delete childEnv.MCP_ENABLE_LEGACY_HTTP;

const child = spawn(process.execPath, [serverPath], {
  stdio: ["pipe", "pipe", "pipe"],
  env: childEnv,
});

let stderrBuf = "";
child.stderr.on("data", (d) => { stderrBuf += d.toString(); });

let buf = "";
const pending = new Map();
child.stdout.on("data", (chunk) => {
  buf += chunk.toString();
  let nl;
  while ((nl = buf.indexOf("\n")) >= 0) {
    const line = buf.slice(0, nl).trim();
    buf = buf.slice(nl + 1);
    if (!line) continue;
    let msg;
    try { msg = JSON.parse(line); } catch { continue; }
    const resolve = pending.get(msg.id);
    if (resolve) { pending.delete(msg.id); resolve(msg); }
  }
});

let nextId = 1;
function send(method, params, timeoutMs = 60000) {
  const id = nextId++;
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
  return new Promise((resolve, reject) => {
    const timer = setTimeout(
      () => { pending.delete(id); reject(new Error(`${method} timed out after ${timeoutMs}ms`)); },
      timeoutMs
    );
    pending.set(id, (msg) => { clearTimeout(timer); resolve(msg); });
  });
}

/** Tool results come back as a JSON string inside content[0].text. */
function unwrap(msg) {
  const text = msg?.result?.content?.[0]?.text;
  if (typeof text !== "string") return null;
  try { return JSON.parse(text); } catch { return null; }
}

async function main() {
  console.log("MCP smoke test");
  console.log(`  server: ${serverPath}`);
  console.log(`  engine: ${process.env.UE_ENGINE_ROOT ?? "(UE_ENGINE_ROOT not set)"}`);
  console.log("");

  // 1. initialize
  const init = await send("initialize", {
    protocolVersion: PROTOCOL_VERSION,
    capabilities: {},
    clientInfo: { name: "mcp-smoke", version: "1.0.0" },
  });
  const serverName = init?.result?.serverInfo?.name;
  record("initialize handshake", serverName === "unreal-bridge", serverName ? `serverInfo.name=${serverName}` : "no serverInfo");
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

  // 2. tools/list
  const listed = await send("tools/list", {});
  const tools = listed?.result?.tools ?? [];
  record("tools/list returns tools", tools.length > 0, `${tools.length} tools`);

  const noSchema = tools.filter((t) => !t.inputSchema).map((t) => t.name);
  record("every tool has an inputSchema", noSchema.length === 0, noSchema.length ? noSchema.join(", ") : undefined);

  const noAnnotations = tools.filter((t) => !t.annotations).map((t) => t.name);
  record("every tool has annotations", noAnnotations.length === 0,
    noAnnotations.length ? `missing: ${noAnnotations.slice(0, 8).join(", ")}` : undefined);

  const startupWarning = stderrBuf.includes("missing annotations");
  record("no annotation warning at startup", !startupWarning);

  // Named one by one rather than by prefix: a server-local tool contacts no
  // editor, so it is not part of the native lane and must not be waved through
  // by a prefix somebody picks later. Adding one here is a deliberate act.
  const SERVER_LOCAL_TOOLS = new Set(["bridge_command_status"]);
  const unexpectedTools = tools
    .map((tool) => tool.name)
    .filter((name) => !name.startsWith("puerts_") && !name.startsWith("engine_source_")
      && !SERVER_LOCAL_TOOLS.has(name));
  record("default catalog is native-only or declared server-local", unexpectedTools.length === 0,
    unexpectedTools.length ? `unexpected: ${unexpectedTools.join(", ")}` : undefined);
  record("legacy HTTP tools are hidden", !tools.some((tool) => tool.name === "test_connection"));
  record("native diagnostic is registered", tools.some((tool) => tool.name === "puerts_diagnostic"));

  // 3. engine_source_search - server-local, works with the editor closed
  if (tools.some((t) => t.name === "engine_source_search")) {
    const r = await send("tools/call", {
      name: "engine_source_search",
      arguments: { symbol: "FRunnable", module: "Core" },
    });
    const payload = unwrap(r);
    if (payload?.success) {
      const n = payload.data?.results?.length ?? payload.data?.matches?.length ?? "?";
      record("engine_source_search finds FRunnable", true, `${n} result(s)`);
    } else {
      const err = payload?.error ?? "no payload";
      const isConfig = /UE_ENGINE_ROOT|locate the UE engine/i.test(err);
      record("engine_source_search finds FRunnable", isConfig ? "skip" : false,
        isConfig ? "UE_ENGINE_ROOT not set for this process" : err);
    }
  } else {
    record("engine_source_search registered", false, "tool not present");
  }

  // 4. engine_source_read - the other server-local reader, and its path guard.
  // It was marked live_verified carrying an empty evidence array, which is the
  // accounting this repo exists to stop. The traversal refusal is checked here
  // rather than in a security suite because it is the same one round trip.
  if (tools.some((t) => t.name === "engine_source_read")) {
    const r = await send("tools/call", {
      name: "engine_source_read",
      arguments: { file: "Source/Runtime/Core/Public/HAL/Runnable.h", start_line: 1, end_line: 40 },
    });
    const payload = unwrap(r);
    if (payload?.success) {
      const text = JSON.stringify(payload.data);
      record("engine_source_read returns real engine source", /FRunnable/.test(text),
        /FRunnable/.test(text) ? "Runnable.h names FRunnable" : "read succeeded but FRunnable is absent");
    } else {
      const err = payload?.error ?? "no payload";
      const isConfig = /UE_ENGINE_ROOT|locate the UE engine/i.test(err);
      record("engine_source_read returns real engine source", isConfig ? "skip" : false,
        isConfig ? "UE_ENGINE_ROOT not set for this process" : err);
    }

    const escape = unwrap(await send("tools/call", {
      name: "engine_source_read",
      arguments: { file: "../../../Windows/System32/drivers/etc/hosts", start_line: 1 },
    }));
    // Success here means the reader can be walked out of the engine directory,
    // which is worse than the tool not existing. But "it refused" is not enough
    // on its own: with no UE_ENGINE_ROOT it refuses because it cannot find the
    // engine at all, and that refusal would still arrive with the guard deleted.
    // Only the guard's own refusal counts as a pass.
    const escapeErr = escape?.error ?? "";
    const guardRefused = /resolves outside the engine directory/i.test(escapeErr);
    const noEngine = /UE_ENGINE_ROOT|locate the UE engine/i.test(escapeErr);
    record("engine_source_read refuses a path outside the engine directory",
      escape?.success === true ? false : guardRefused ? true : noEngine ? "skip" : false,
      escape?.success === true
        ? "READ SUCCEEDED OUTSIDE THE ENGINE ROOT"
        : guardRefused
          ? escapeErr
          : noEngine
            ? "UE_ENGINE_ROOT not set; this refusal does not exercise the guard"
            : `refused, but not by the path guard: ${escapeErr}`);
  } else {
    record("engine_source_read registered", false, "tool not present");
  }

  // 5. puerts_diagnostic - the authenticated native named-pipe link
  const diagnostic = await send("tools/call", {
    name: "puerts_diagnostic",
    arguments: { actor_limit: 500 },
  });
  const payload = unwrap(diagnostic);
  if (payload?.success) {
    const data = payload.data ?? {};
    const native = payload.transport === "named_pipe"
      && payload.is_game_thread === true
      && data.transport === "named_pipe"
      && data.execution_context === "puerts_node_v8_in_process";
    record("puerts_diagnostic proves native IPC", native,
      native ? `${data.actor_count_measured} actor(s), ${data.native_actor_query_ms} ms` : "missing native telemetry");
  } else {
    record("puerts_diagnostic proves native IPC", requireEditor ? false : "skip",
      payload?.error ?? payload?.errors?.join(", ") ?? "named pipe unavailable (is UE4 open?)");
  }

  child.stdin.end();
  child.kill();

  const failed = results.filter((r) => r.ok === false);
  const skipped = results.filter((r) => r.ok === "skip");
  console.log("");
  console.log(`${results.length - failed.length - skipped.length} passed, ${failed.length} failed, ${skipped.length} skipped`);
  process.exit(failed.length === 0 ? 0 : 1);
}

main().catch((err) => {
  console.error("smoke test error:", err.message);
  if (stderrBuf) console.error("--- server stderr ---\n" + stderrBuf.slice(0, 2000));
  child.kill();
  process.exit(1);
});
