#!/usr/bin/env node
// Live acceptance for session isolation with two UE4.27 editors open at once.
//
// The property under test is not "the bridge works with two editors". It is that
// a request can never reach an editor it was not addressed to, and that when the
// client cannot tell which editor it is addressing it refuses instead of picking
// one. The old client ended discovery with a compiled-in default pipe name, so
// "I do not know" and "use whichever editor owns the default name" were the same
// code path. With one editor that reads as a convenience. With two it is a
// command landing in a project nobody asked it to touch, reported as a success.
//
//   --phase=both         both editors up: identity, isolation, and refusals
//   --phase=a-closed     A closed: B still serves, A is refused as stale
//   --phase=a-restarted  A back up: a new session id, same project
//   --phase=none         both closed: no advertisement survives
//
// Each phase is a separate run because editor lifecycle is the thing being
// measured; driving it from inside one process would test the script instead.

import { spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { checkInstall } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");

const PROJECT_A = process.env.MCP_PROJECT_A ?? "D:/Unreal Projects/BridgeInstallTest";
const PROJECT_B = process.env.MCP_PROJECT_B
  ?? "D:/Unreal Projects/MASTER_PROJECT/SF_Repository/Sinfeld_240301/Tests/UE427PuerTSMCP";
const phase = (process.argv.find((a) => a.startsWith("--phase=")) ?? "--phase=both").slice(8);

const failures = [];
function assert(condition, label) {
  if (condition) { console.log(`  PASS  ${label}`); return true; }
  console.log(`  FAIL  ${label}`); failures.push(label); return false;
}

const sessionPath = (root) => join(root, "Saved", "MCPPuerTSBridge", "session.json");
const readSession = (root) => {
  try { return JSON.parse(readFileSync(sessionPath(root), "utf8")); } catch { return null; }
};

/** One MCP server process bound to one project. Two of these run at once and
    must never answer for each other, which is the entire point of the file. */
class Client {
  constructor(label, projectRoot, extraEnv = {}) {
    this.label = label;
    this.projectRoot = projectRoot;
    const env = { ...process.env, MCP_UNREAL_PROJECT_ROOT: projectRoot, ...extraEnv };
    // The installer writes a pipe into .mcp.json; inheriting it here would let a
    // stale value decide the target and quietly defeat what is being measured.
    delete env.MCP_PUERTS_PIPE;
    delete env.MCP_PUERTS_TOKEN_PATH;
    delete env.MCP_PUERTS_TOKEN;
    delete env.MCP_PUERTS_SESSION_ID;
    if (extraEnv.MCP_PUERTS_SESSION_ID) env.MCP_PUERTS_SESSION_ID = extraEnv.MCP_PUERTS_SESSION_ID;
    this.child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "pipe"], env });
    this.stderr = "";
    this.child.stderr.on("data", (c) => { this.stderr += c.toString(); });
    this.buffer = "";
    this.nextId = 1;
    this.pending = new Map();
    this.child.stdout.on("data", (chunk) => {
      this.buffer += chunk.toString();
      for (let n; (n = this.buffer.indexOf("\n")) >= 0;) {
        const line = this.buffer.slice(0, n).trim();
        this.buffer = this.buffer.slice(n + 1);
        if (!line) continue;
        const message = JSON.parse(line);
        const settle = this.pending.get(message.id);
        if (settle) { this.pending.delete(message.id); settle(message); }
      }
    });
  }

  send(method, params) {
    const id = this.nextId++;
    this.child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`${this.label}: ${method} timed out`)), 90000);
      this.pending.set(id, (m) => { clearTimeout(timer); resolve(m); });
    });
  }

  async ready() {
    await this.send("initialize", {
      protocolVersion: "2024-11-05", capabilities: {},
      clientInfo: { name: `session-isolation-${this.label}`, version: "1.0.0" },
    });
    this.child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");
    return this;
  }

  /** Returns the parsed tool payload, or { __error } when the tool refused.
      A refusal is a result here, not an exception: half these cases exist to
      prove the bridge says no. */
  async call(name, args = {}) {
    const message = await this.send("tools/call", { name, arguments: args });
    const text = message?.result?.content?.[0]?.text;
    if (typeof text !== "string") return { __error: JSON.stringify(message?.error ?? message) };
    try { return JSON.parse(text); } catch { return { __error: text }; }
  }

  close() { this.child.kill(); }
}

const errorTextOf = (result) =>
  JSON.stringify(result?.__error ?? result?.errors ?? result?.message ?? result ?? "");

const evidence = { phase, cases: {} };

try {
  console.log(`session isolation acceptance, phase ${phase}`);
  console.log(`  A: ${PROJECT_A}`);
  console.log(`  B: ${PROJECT_B}\n`);

  // The install gate first, for both. A live isolation result is meaningless if
  // either editor is running a plugin that is not this checkout.
  if (phase === "both") {
    for (const [label, root] of [["A", PROJECT_A], ["B", PROJECT_B]]) {
      const { ok, problems } = checkInstall(root);
      assert(ok, `install:check passes for ${label}${ok ? "" : ` [${problems.join("; ")}]`}`);
    }
  }

  if (phase === "both") {
    const a = readSession(PROJECT_A);
    const b = readSession(PROJECT_B);
    assert(a !== null, "editor A advertises a session manifest");
    assert(b !== null, "editor B advertises a session manifest");
    if (!a || !b) throw new Error("both editors must be running for this phase");

    console.log("\n-- 1. the two advertisements are distinct in every identifying field");
    assert(a.session_id !== b.session_id, `session ids differ (${a.session_id} vs ${b.session_id})`);
    assert(a.editor_pid !== b.editor_pid, `editor pids differ (${a.editor_pid} vs ${b.editor_pid})`);
    assert(a.pipe_name !== b.pipe_name, `pipe names differ (${a.pipe_name} vs ${b.pipe_name})`);
    assert(a.session_nonce !== b.session_nonce, "session nonces differ");
    assert(a.project_path !== b.project_path, "project paths differ");
    assert(a.shutdown_state === "running" && b.shutdown_state === "running",
      "both advertise shutdown_state running");
    assert(a.schema_version === 1 && b.schema_version === 1, "both use schema version 1");
    evidence.cases.advertisements = {
      a: { session_id: a.session_id, editor_pid: a.editor_pid, pipe_name: a.pipe_name, project_path: a.project_path },
      b: { session_id: b.session_id, editor_pid: b.editor_pid, pipe_name: b.pipe_name, project_path: b.project_path },
    };

    const clientA = await new Client("A", PROJECT_A).ready();
    const clientB = await new Client("B", PROJECT_B).ready();
    try {
      console.log("\n-- 2. concurrent diagnostics resolve to different editors");
      const [diagA, diagB] = await Promise.all([
        clientA.call("puerts_diagnostic", {}),
        clientB.call("puerts_diagnostic", {}),
      ]);
      assert(diagA.success === true, `A diagnostic succeeded ${diagA.success ? "" : errorTextOf(diagA)}`);
      assert(diagB.success === true, `B diagnostic succeeded ${diagB.success ? "" : errorTextOf(diagB)}`);
      assert(diagA.session?.session_id === a.session_id, "A answered with A's session id");
      assert(diagB.session?.session_id === b.session_id, "B answered with B's session id");
      assert(diagA.session?.session_id !== diagB.session?.session_id,
        "the two concurrent diagnostics came from different sessions");
      assert(diagA.session?.editor_pid !== diagB.session?.editor_pid,
        `the two answers came from different processes (${diagA.session?.editor_pid} vs ${diagB.session?.editor_pid})`);
      assert(diagA.session?.pipe_name !== diagB.session?.pipe_name, "the two answers name different pipes");
      evidence.cases.concurrent_diagnostics = { a: diagA.session, b: diagB.session };

      console.log("\n-- 3. every response carries identity, not only diagnostics");
      const logsA = await clientA.call("puerts_get_logs", { maximum_lines: 1 });
      assert(logsA.session?.session_id === a.session_id,
        "a non-diagnostic response also reports its session id");
      assert(typeof logsA.session?.project_path === "string" && logsA.session.project_path.length > 0,
        "a non-diagnostic response reports its project path");

      console.log("\n-- 4. simultaneous read-only requests do not mix");
      // Interleaved on purpose, several rounds, both directions. A response
      // arriving on the wrong client is the failure this is looking for, and one
      // round of two calls is not enough to catch an ordering bug.
      const rounds = [];
      for (let i = 0; i < 6; i++) {
        rounds.push(clientA.call("puerts_find_actors", { limit: 1 }).then((r) => ["A", r]));
        rounds.push(clientB.call("puerts_find_actors", { limit: 1 }).then((r) => ["B", r]));
      }
      const mixed = (await Promise.all(rounds)).filter(([who, r]) =>
        r.session?.session_id !== (who === "A" ? a.session_id : b.session_id));
      assert(mixed.length === 0,
        `all ${rounds.length} interleaved read-only responses came back on the right client (${mixed.length} mixed)`);

      console.log("\n-- 5. a probe actor spawned in one world never appears in the other");
      const stamp = `${a.editor_pid}_${b.editor_pid}`;
      const probeA = `MCPIsolationProbe_A_${stamp}`;
      const probeB = `MCPIsolationProbe_B_${stamp}`;
      const spawnA = await clientA.call("puerts_spawn_actor", { class_path: "/Script/Engine.StaticMeshActor" });
      const spawnB = await clientB.call("puerts_spawn_actor", { class_path: "/Script/Engine.StaticMeshActor" });
      assert(spawnA.success === true, `A spawned a probe actor ${spawnA.success ? "" : errorTextOf(spawnA)}`);
      assert(spawnB.success === true, `B spawned a probe actor ${spawnB.success ? "" : errorTextOf(spawnB)}`);
      const nameOf = (r) => r.changed_actors?.[0] ?? r.data?.name ?? r.data?.actor ?? null;
      const spawnedA = nameOf(spawnA);
      const spawnedB = nameOf(spawnB);
      assert(spawnedA !== null && spawnedB !== null,
        `both spawns reported an actor (${JSON.stringify(spawnedA)}, ${JSON.stringify(spawnedB)})`);
      assert(spawnA.session?.session_id === a.session_id, "A's spawn was executed by A");
      assert(spawnB.session?.session_id === b.session_id, "B's spawn was executed by B");

      // Label each probe uniquely, then read both worlds back independently.
      await clientA.call("puerts_call_function",
        { actor: String(spawnedA), function: "Actor.SetActorLabel", arguments: [probeA] });
      await clientB.call("puerts_call_function",
        { actor: String(spawnedB), function: "Actor.SetActorLabel", arguments: [probeB] });

      const worldA = await clientA.call("puerts_find_actors", { limit: 500 });
      const worldB = await clientB.call("puerts_find_actors", { limit: 500 });
      const textA = JSON.stringify(worldA.data ?? {});
      const textB = JSON.stringify(worldB.data ?? {});
      assert(!textA.includes(probeB), "B's probe is absent from A's world");
      assert(!textB.includes(probeA), "A's probe is absent from B's world");
      assert(worldA.session?.session_id === a.session_id && worldB.session?.session_id === b.session_id,
        "each world was read by the editor that owns it");
      evidence.cases.probe_isolation = {
        probe_a: probeA, probe_b: probeB,
        a_actor_count: worldA.data?.count ?? null, b_actor_count: worldB.data?.count ?? null,
      };

      console.log("\n-- 6. a request carrying the wrong session nonce is refused by the editor");
      // A doctored advertisement: A's live pipe and pid, a nonce that is not A's.
      // Everything the client can check locally passes, so the refusal has to
      // come from the editor, which is exactly the claim.
      const forged = mkdtempSync(join(tmpdir(), "mcp-forged-session-"));
      mkdirSync(join(forged, "Saved", "MCPPuerTSBridge"), { recursive: true });
      writeFileSync(join(forged, "Saved", "MCPPuerTSBridge", "session.json"), JSON.stringify({
        ...a,
        project_path: forged,
        session_nonce: "00000000000000000000000000000000not-the-live-nonce",
      }), "utf8");
      writeFileSync(join(forged, "Saved", "MCPPuerTSBridge", "token.txt"),
        readFileSync(join(PROJECT_A, "Saved", "MCPPuerTSBridge", "token.txt"), "utf8"), "utf8");
      const forgedClient = await new Client("forged", forged).ready();
      const forgedResult = await forgedClient.call("puerts_find_actors", { limit: 1 });
      forgedClient.close();
      const forgedText = errorTextOf(forgedResult);
      const forgedCode = forgedResult.session_error_code;
      // Two refusals fire here and both are wanted. The EDITOR refuses first, on
      // the nonce, and its words come back in session_error_detail. The CLIENT
      // then refuses to accept the reply at all, because the identity that
      // answered is not the identity it addressed. Asserting only the client
      // side would leave the editor's own check unproven, which is the half that
      // matters when a client is the thing that is wrong.
      const editorSaid = JSON.stringify(forgedResult.session_error_detail?.answered_errors ?? []);
      assert(forgedResult.success !== true, "the forged-nonce request did not succeed");
      assert(forgedCode === "session_identity_mismatch",
        `the client reports a structured code rather than prose (got: ${String(forgedCode)})`);
      assert(/session_nonce does not match this editor/i.test(editorSaid),
        `the EDITOR refused on the nonce, in its own words (got: ${editorSaid.slice(0, 400)})`);
      evidence.cases.forged_nonce = {
        refused: forgedResult.success !== true,
        client_code: forgedCode,
        editor_refusal: JSON.parse(editorSaid)[0] ?? null,
      };
      rmSync(forged, { recursive: true, force: true });

      console.log("\n-- 7. a stale advertisement naming a dead process is refused before connecting");
      const staleRoot = mkdtempSync(join(tmpdir(), "mcp-stale-session-"));
      mkdirSync(join(staleRoot, "Saved", "MCPPuerTSBridge"), { recursive: true });
      // PID 0x7FFFFFFE: a value Windows will not have assigned, standing in for
      // an editor that died without retracting.
      writeFileSync(join(staleRoot, "Saved", "MCPPuerTSBridge", "session.json"), JSON.stringify({
        ...a, project_path: staleRoot, editor_pid: 2147483646,
      }), "utf8");
      writeFileSync(join(staleRoot, "Saved", "MCPPuerTSBridge", "token.txt"), "irrelevant", "utf8");
      const staleClient = await new Client("stale", staleRoot).ready();
      const staleResult = await staleClient.call("puerts_find_actors", { limit: 1 });
      staleClient.close();
      const staleText = errorTextOf(staleResult);
      assert(staleResult.success !== true, "the stale-advertisement request did not succeed");
      assert(staleResult.session_error_code === "session_stale",
        `the refusal carries the structured stale code (got: ${String(staleResult.session_error_code)})`);
      assert(/not running/i.test(staleText), "the refusal names the dead process");
      evidence.cases.stale_advertisement = {
        refused: staleResult.success !== true,
        client_code: staleResult.session_error_code,
        detail: staleText.slice(0, 400),
      };
      rmSync(staleRoot, { recursive: true, force: true });

      console.log("\n-- 8. explicit session selection, and refusal when it does not match");
      const pinnedRight = await new Client("pinA", PROJECT_A,
        { MCP_PUERTS_SESSION_ID: a.session_id }).ready();
      const pinnedRightResult = await pinnedRight.call("puerts_diagnostic", {});
      pinnedRight.close();
      assert(pinnedRightResult.success === true && pinnedRightResult.session?.session_id === a.session_id,
        "pinning MCP_PUERTS_SESSION_ID to A's live session reaches A");
      const pinnedWrong = await new Client("pinB-on-A", PROJECT_A,
        { MCP_PUERTS_SESSION_ID: b.session_id }).ready();
      const pinnedWrongResult = await pinnedWrong.call("puerts_diagnostic", {});
      pinnedWrong.close();
      assert(pinnedWrongResult.success !== true,
        "asking project A for B's session id is refused rather than silently served by A");
      assert(pinnedWrongResult.session_error_code === "session_not_selected",
        `the refusal carries the structured selection code (got: ${String(pinnedWrongResult.session_error_code)})`);
    } finally {
      clientA.close();
      clientB.close();
    }
  }

  if (phase === "a-closed") {
    console.log("-- 9. closing A leaves B serving and A refused");
    const a = readSession(PROJECT_A);
    assert(a === null, `A's advertisement was retracted on shutdown (found: ${a ? a.session_id : "none"})`);
    const b = readSession(PROJECT_B);
    assert(b !== null, "B is still advertising");
    const clientB = await new Client("B", PROJECT_B).ready();
    try {
      const diagB = await clientB.call("puerts_diagnostic", {});
      assert(diagB.success === true, `B still serves with A closed ${diagB.success ? "" : errorTextOf(diagB)}`);
      assert(diagB.session?.session_id === b.session_id, "B's identity is unchanged by A's shutdown");
      evidence.cases.a_closed = { b_session_id: diagB.session?.session_id, b_pid: diagB.session?.editor_pid };
    } finally { clientB.close(); }

    const clientA = await new Client("A", PROJECT_A).ready();
    try {
      const result = await clientA.call("puerts_diagnostic", {});
      assert(result.success !== true, "a request to the closed editor is refused");
      assert(result.session_error_code === "session_missing",
        `the refusal is the structured session_missing (got: ${String(result.session_error_code)})`);
      // The decisive one. B is up and owns a live pipe; the old client would have
      // fallen through to a default pipe name here rather than refusing.
      assert(!errorTextOf(result).includes(String(b.session_id)),
        "the refusal did NOT fall through to the other running editor");
    } finally { clientA.close(); }
  }

  if (phase === "a-restarted") {
    console.log("-- 10. a restarted editor is a new session for the same project");
    const previous = process.env.MCP_PREVIOUS_SESSION_ID;
    const a = readSession(PROJECT_A);
    assert(a !== null, "A advertises again after restarting");
    assert(previous !== undefined, "the previous session id was passed in for comparison");
    assert(a.session_id !== previous, `the session id is new (${previous} -> ${a?.session_id})`);
    const clientA = await new Client("A", PROJECT_A).ready();
    try {
      const diag = await clientA.call("puerts_diagnostic", {});
      assert(diag.success === true, "the restarted editor serves");
      assert(diag.session?.session_id === a.session_id, "it answers with the new session id");
      // Requirement 12: reconnecting returns to the same PROJECT. A new session
      // id for the same project is correct; a different project would not be.
      assert(diag.session?.project_path?.toLowerCase().replace(/\\/g, "/")
        === PROJECT_A.toLowerCase().replace(/\\/g, "/"),
        `reconnection returned to the same project (${diag.session?.project_path})`);
      evidence.cases.a_restarted = { previous_session_id: previous, new_session_id: a.session_id };
    } finally { clientA.close(); }

    const pinnedOld = await new Client("A-old-id", PROJECT_A,
      { MCP_PUERTS_SESSION_ID: previous }).ready();
    const oldResult = await pinnedOld.call("puerts_diagnostic", {});
    pinnedOld.close();
    assert(oldResult.success !== true,
      "a client pinned to the previous session id is refused rather than retargeted silently");
  }

  if (phase === "none") {
    console.log("-- 11. with both editors closed, nothing is left advertising");
    for (const [label, root] of [["A", PROJECT_A], ["B", PROJECT_B]]) {
      const session = readSession(root);
      assert(session === null,
        `${label} left no session advertisement (found: ${session ? JSON.stringify(session.shutdown_state) : "none"})`);
      const client = await new Client(label, root).ready();
      try {
        const result = await client.call("puerts_diagnostic", {});
        assert(result.success !== true, `${label} refuses a request with no editor running`);
        assert(result.session_error_code === "session_missing",
          `${label}'s refusal is the structured session_missing, not a connection timeout`);
      } finally { client.close(); }
    }
  }

  const evidenceDir = join(repoRoot, "docs", "evidence");
  mkdirSync(evidenceDir, { recursive: true });
  const evidencePath = join(evidenceDir, `session-isolation-${phase}.json`);
  writeFileSync(evidencePath, JSON.stringify(evidence, null, 2) + "\n");
  console.log(`\nevidence: ${evidencePath}`);
  console.log(failures.length === 0
    ? `all checks passed (${phase} phase)`
    : `${failures.length} check(s) did not hold:`);
  for (const f of failures) console.log(`  - ${f}`);
  process.exit(failures.length === 0 ? 0 : 1);
} catch (error) {
  console.error(`\nsession isolation acceptance: ${error.message}`);
  process.exit(1);
}
