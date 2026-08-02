// A fake advertised editor session, shared by every suite that exercises the
// named-pipe client.
//
// It exists because the client no longer guesses. Discovery used to end in
// `return "\\\\.\\pipe\\UE427PuerTSMCP"`, so a test needed nothing but a pipe
// name; now the client resolves a session manifest and refuses when there is
// none, which is the behaviour that keeps two open editors apart. Every suite
// therefore has to advertise one, and they should all advertise the same shape.

import { mkdir, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

export const TEST_SESSION_ID = "11111111-2222-3333-4444-555555555555";
export const TEST_SESSION_NONCE = "test-session-nonce";

export interface SessionFixture {
  projectRoot: string;
  /** What the editor writes to Saved/MCPPuerTSBridge/session.json. */
  manifest: Record<string, unknown>;
  /** The same identity seen from the other direction: what every reply stamps. */
  responseSession: Record<string, unknown>;
}

/** Advertise a session for `pipeName` and point MCP_UNREAL_PROJECT_ROOT at it.
 *  editor_pid is this process's own pid, so the client's liveness check passes
 *  by construction rather than by the test picking a number that happens to be
 *  running. */
export async function advertiseSession(pipeName: string): Promise<SessionFixture> {
  const projectRoot = await mkdtemp(join(tmpdir(), "ue4-puerts-project-"));
  const bridgeDir = join(projectRoot, "Saved", "MCPPuerTSBridge");
  await mkdir(bridgeDir, { recursive: true });
  const manifest = {
    schema_version: 1,
    session_id: TEST_SESSION_ID,
    session_nonce: TEST_SESSION_NONCE,
    editor_pid: process.pid,
    process_start_time: "2026-08-02T00:00:00.000Z",
    project_path: projectRoot,
    uproject_path: join(projectRoot, "Fixture.uproject"),
    pipe_name: pipeName,
    bridge_commit: "0000000",
    installed_manifest_hash: "0".repeat(40),
    created_at: "2026-08-02T00:00:00.000Z",
    last_heartbeat_at: new Date().toISOString(),
    shutdown_state: "running",
  };
  await writeFile(join(bridgeDir, "session.json"), JSON.stringify(manifest), "utf8");
  process.env.MCP_UNREAL_PROJECT_ROOT = projectRoot;
  return {
    projectRoot,
    manifest,
    responseSession: {
      session_id: TEST_SESSION_ID,
      editor_pid: process.pid,
      process_start_time: manifest.process_start_time,
      project_path: projectRoot,
      uproject_path: manifest.uproject_path,
      pipe_name: pipeName,
    },
  };
}
