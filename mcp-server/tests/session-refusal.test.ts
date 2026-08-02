/**
 * Refusal tests for the named-pipe session client.
 *
 * Every other suite in this directory advertises a good session and checks that
 * a command works. This one is the opposite claim, and it is the one that
 * matters for safety: the client must REFUSE rather than reach an editor it
 * cannot identify. The behaviour being defended is concrete. Discovery used to
 * end in `return "\\\\.\\pipe\\UE427PuerTSMCP"`, so a missing advertisement sent
 * every request to whichever editor owned that name. With one editor open that
 * reads as a convenience. With two it is a client authoring assets in a project
 * nobody asked it to touch and reporting success.
 *
 * These are not mocks asserting on mocks. The code under test is the real
 * resolveSession / resolvePipeName / PuerTSClient.call. Session manifests are
 * real files in real temp directories, written in the shape the editor writes,
 * and the four transport cases run against a real listening pipe. The stand-in
 * editor is a counterparty, never the subject: every assertion is on what the
 * client did with what it got.
 *
 * WHAT THIS DOES NOT COVER, stated rather than implied. The editor's own half of
 * the nonce check is C++ in MCPPuerTSBridgeService.cpp (a missing nonce, a wrong
 * nonce, and a mismatched expect_session_id are refused around lines 467 to 490).
 * Nothing here compiles or runs that. The last case below covers the client side
 * of that exchange only, and says so.
 */

import { spawn } from "node:child_process";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { createServer, Server } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
  PuerTSClient, resolvePipeName, resolveSession, SessionError,
} from "../src/puerts-client.js";
import { TEST_SESSION_ID, TEST_SESSION_NONCE } from "./session-fixture.js";

let passed = 0;
const failures: string[] = [];

function check(condition: boolean, label: string): void {
  if (condition) { passed += 1; console.log(`  PASS  ${label}`); return; }
  failures.push(label);
  console.log(`  FAIL  ${label}`);
}

const ENV_KEYS = [
  "MCP_UNREAL_PROJECT_ROOT", "MCP_PUERTS_SESSION_ID", "MCP_PUERTS_PIPE",
  "MCP_PUERTS_TOKEN", "MCP_PUERTS_TOKEN_PATH",
];
const savedEnv: Record<string, string | undefined> = {};
for (const key of ENV_KEYS) savedEnv[key] = process.env[key];
function clearEnv(): void {
  for (const key of ENV_KEYS) delete process.env[key];
}

const scratch: string[] = [];

/** A project directory with no advertisement, or with exactly `body` as its
 *  session.json. Selects it with MCP_UNREAL_PROJECT_ROOT and returns the root. */
async function project(body: string | null): Promise<string> {
  const root = await mkdtemp(join(tmpdir(), "session-refusal-"));
  scratch.push(root);
  if (body !== null) {
    const dir = join(root, "Saved", "MCPPuerTSBridge");
    await mkdir(dir, { recursive: true });
    await writeFile(join(dir, "session.json"), body, "utf8");
  }
  process.env.MCP_UNREAL_PROJECT_ROOT = root;
  return root;
}

/** The manifest a healthy editor publishes for `root`, with `overrides`
 *  applied. editor_pid is this process, so liveness passes by construction
 *  rather than by guessing a number that happens to be running. */
function healthy(root: string, pipeName: string, overrides: Record<string, unknown> = {}) {
  return {
    schema_version: 1,
    session_id: TEST_SESSION_ID,
    session_nonce: TEST_SESSION_NONCE,
    editor_pid: process.pid,
    process_start_time: "2026-08-02T00:00:00.000Z",
    project_path: root,
    uproject_path: join(root, "Fixture.uproject"),
    pipe_name: pipeName,
    bridge_commit: "0000000",
    installed_manifest_hash: "0".repeat(40),
    created_at: "2026-08-02T00:00:00.000Z",
    last_heartbeat_at: new Date().toISOString(),
    shutdown_state: "running",
    ...overrides,
  };
}

/** A project advertising an otherwise healthy session for itself, sabotaged by
 *  `overrides`. One helper because every case below differs by exactly one
 *  field, and writing the manifest for a directory other than the one it lands
 *  in produces a project mismatch that masks the case under test. */
async function advertise(tag: string, overrides: Record<string, unknown> = {}): Promise<{ root: string; pipeName: string }> {
  const root = await mkdtemp(join(tmpdir(), "session-refusal-"));
  scratch.push(root);
  const pipeName = `\\\\.\\pipe\\session-refusal-${tag}-${process.pid}-${Date.now()}`;
  const dir = join(root, "Saved", "MCPPuerTSBridge");
  await mkdir(dir, { recursive: true });
  await writeFile(join(dir, "session.json"),
    JSON.stringify(healthy(root, pipeName, overrides)), "utf8");
  process.env.MCP_UNREAL_PROJECT_ROOT = root;
  return { root, pipeName };
}

/** The refusal code, or a description of whatever happened instead. A resolve
 *  that SUCCEEDS is the failure this suite is looking for, so it has to be
 *  reported as loudly as a wrong code. */
async function refusalCode(run: () => Promise<unknown>): Promise<string> {
  try {
    await run();
    return "no refusal: the call succeeded";
  } catch (error: unknown) {
    if (error instanceof SessionError) return error.code;
    return `not a SessionError: ${(error as Error).message}`;
  }
}

/** A pid that is definitely not running: spawn a process that exits at once and
 *  reuse its number the moment it is gone. */
async function deadPid(): Promise<number> {
  const child = spawn(process.execPath, ["-e", ""], { stdio: "ignore" });
  const pid = child.pid ?? 0;
  await new Promise<void>((resolve) => child.once("exit", () => resolve()));
  return pid;
}

/** A stand-in editor on a real pipe. `reply` receives the parsed request and
 *  returns the line to answer with, so a case can answer with the wrong
 *  identity, no identity, or a refusal. Requests are recorded for inspection. */
async function standInEditor(
  pipeName: string,
  reply: (request: Record<string, unknown>) => Record<string, unknown>,
): Promise<{ server: Server; requests: Record<string, unknown>[] }> {
  const requests: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as Record<string, unknown>;
    requests.push(request);
    socket.end(JSON.stringify(reply(request)) + "\n");
  }));
  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(pipeName, resolve);
  });
  return { server, requests };
}

const envelope = (extra: Record<string, unknown>) => ({
  success: true,
  message: "ok",
  changed_assets: [],
  changed_actors: [],
  warnings: [],
  errors: [],
  log_output: [],
  transaction_id: "",
  ...extra,
});

async function main(): Promise<void> {
  clearEnv();
  process.env.MCP_PUERTS_TOKEN = "test-token";

  console.log("session refusal acceptance\n");
  console.log("-- discovery: an editor that cannot be identified is never guessed at");

  await project(null);
  check(await refusalCode(resolveSession) === "session_missing",
    "no session.json refuses with session_missing");
  // The point of the code is the absence of a fallback. resolvePipeName is the
  // function that used to end in a compiled-in default, so it gets its own case:
  // a client that refuses to resolve a session but then happily returns a pipe
  // name would have the same bug with better error messages.
  check(await refusalCode(resolvePipeName) === "session_missing",
    "and resolvePipeName refuses too, rather than returning a default pipe name");

  await project("{ this is not json");
  check(await refusalCode(resolveSession) === "session_unreadable",
    "a corrupt manifest refuses with session_unreadable");

  await advertise("schema", { schema_version: 2 });
  check(await refusalCode(resolveSession) === "session_schema_unsupported",
    "a manifest newer than this client refuses with session_schema_unsupported, not a best-effort read");

  await advertise("shutdown", { shutdown_state: "shutting_down" });
  check(await refusalCode(resolveSession) === "session_shut_down",
    "an editor that recorded its own shutdown refuses with session_shut_down");

  {
    const pid = await deadPid();
    await advertise("stale", { editor_pid: pid });
    check(await refusalCode(resolveSession) === "session_stale",
      `a manifest left behind by a dead editor refuses with session_stale (pid ${pid})`);
  }

  await advertise("pinned");
  process.env.MCP_PUERTS_SESSION_ID = "99999999-9999-9999-9999-999999999999";
  check(await refusalCode(resolveSession) === "session_not_selected",
    "a pinned session id that no longer matches refuses with session_not_selected");
  delete process.env.MCP_PUERTS_SESSION_ID;

  {
    // A Saved directory copied from one project into another: the manifest
    // describes a project that is not the one it was found under.
    const other = join(tmpdir(), "session-refusal-somewhere-else");
    await advertise("mismatch", { project_path: other });
    check(await refusalCode(resolveSession) === "session_project_mismatch",
      "a manifest advertising a different project refuses with session_project_mismatch");
  }

  console.log("\n-- transport: an answer from the wrong editor is refused as hard as a request to one");

  {
    const { pipeName } = await advertise("noidentity");
    const editor = await standInEditor(pipeName, () => envelope({}));
    try {
      check(await refusalCode(() => new PuerTSClient().call("find_actors", {}, 4000))
        === "session_identity_absent",
        "a reply with no session stamp refuses with session_identity_absent");
    } finally { editor.server.close(); }
  }

  {
    const { root, pipeName } = await advertise("otheredi");
    const editor = await standInEditor(pipeName, () => envelope({
      session: {
        session_id: "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
        editor_pid: process.pid,
        project_path: root,
      },
    }));
    try {
      check(await refusalCode(() => new PuerTSClient().call("find_actors", {}, 4000))
        === "session_identity_mismatch",
        "a reply stamped by another session refuses with session_identity_mismatch");
    } finally { editor.server.close(); }
  }

  {
    // Same session id, different project. A response is only trusted when all
    // three of id, pid and project agree, because a copied Saved directory
    // reproduces the id while the project is exactly what has gone wrong.
    const { pipeName } = await advertise("otherproj");
    const editor = await standInEditor(pipeName, () => envelope({
      session: {
        session_id: TEST_SESSION_ID,
        editor_pid: process.pid,
        project_path: join(tmpdir(), "some-other-project"),
      },
    }));
    try {
      check(await refusalCode(() => new PuerTSClient().call("find_actors", {}, 4000))
        === "session_identity_mismatch",
        "a reply carrying the right session id from the wrong project is refused too");
    } finally { editor.server.close(); }
  }

  {
    const { root, pipeName } = await advertise("addressed");
    const editor = await standInEditor(pipeName, () => envelope({
      session: { session_id: TEST_SESSION_ID, editor_pid: process.pid, project_path: root },
    }));
    try {
      await new PuerTSClient().call("find_actors", {}, 4000);
      const sent = editor.requests[0] ?? {};
      check(sent.session_nonce === TEST_SESSION_NONCE,
        "the request carries the advertised nonce, which is what lets the editor refuse a request meant for another");
      check(sent.expect_session_id === TEST_SESSION_ID,
        "and expect_session_id, which closes the window where the editor restarts between the read and the write");
    } finally { editor.server.close(); }
  }

  {
    // The client half of the wrong-nonce exchange, in the shape it actually
    // occurs: the editor restarted and rotated its nonce while this client was
    // still holding the manifest from before, so the nonce it sends is stale
    // and the editor refuses. The editor's refusal itself is C++ and is not run
    // here; what is asserted is that the client surfaces the editor's own words
    // instead of a bare transport error. Losing that text is not cosmetic.
    // "pipe closed" sends someone to debug the pipe, and the problem was the
    // addressing.
    const { root, pipeName } = await advertise("badnonce");
    const rotatedNonce = "the-nonce-this-editor-has-now";
    const editor = await standInEditor(pipeName, (request) => {
      const good = request.session_nonce === rotatedNonce;
      return envelope({
        success: good,
        message: good ? "ok" : "session_nonce does not match this editor.",
        errors: good ? [] : ["session_nonce does not match this editor."],
        // A refusal still stamps identity, so the client can tell a refusal from
        // the right editor apart from an answer from the wrong one.
        session: { session_id: "zzzzzzzz-0000-0000-0000-000000000000", editor_pid: process.pid, project_path: root },
      });
    });
    try {
      let detail: Record<string, unknown> = {};
      try {
        await new PuerTSClient().call("find_actors", {}, 4000);
      } catch (error: unknown) {
        if (error instanceof SessionError) detail = error.detail;
      }
      check(JSON.stringify(detail.answered_errors ?? []).includes("session_nonce"),
        "an editor's own refusal travels with the identity mismatch instead of being discarded");
    } finally { editor.server.close(); }
  }

  console.log(`\n${passed} check(s) passed, ${failures.length} failed`);
  for (const f of failures) console.log(`  - ${f}`);
  if (failures.length > 0) process.exitCode = 1;
}

main()
  .catch((error: unknown) => {
    console.error(error);
    process.exitCode = 1;
  })
  .finally(async () => {
    clearEnv();
    for (const key of ENV_KEYS) {
      if (savedEnv[key] !== undefined) process.env[key] = savedEnv[key];
    }
    for (const dir of scratch) await rm(dir, { recursive: true, force: true });
  });
