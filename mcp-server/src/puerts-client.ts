import { randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";
import { createConnection } from "node:net";
import { join } from "node:path";

export type JsonValue = string | number | boolean | null | JsonObject | JsonValue[];
export interface JsonObject { [key: string]: JsonValue; }

export interface PuerTSResponse extends JsonObject {
  success: boolean;
  message: string;
  changed_assets: JsonValue[];
  changed_actors: JsonValue[];
  warnings: JsonValue[];
  errors: JsonValue[];
  log_output: JsonValue[];
  transaction_id: string;
}

function isObject(value: unknown): value is JsonObject {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function isResponse(value: unknown): value is PuerTSResponse {
  return isObject(value)
    && typeof value.success === "boolean"
    && typeof value.message === "string"
    && Array.isArray(value.changed_assets)
    && Array.isArray(value.changed_actors)
    && Array.isArray(value.warnings)
    && Array.isArray(value.errors)
    && Array.isArray(value.log_output)
    && typeof value.transaction_id === "string";
}

async function readToken(): Promise<string> {
  const configured = process.env.MCP_PUERTS_TOKEN?.trim();
  if (configured) return configured;
  const projectRoot = process.env.MCP_UNREAL_PROJECT_ROOT ?? process.cwd();
  const tokenPath = process.env.MCP_PUERTS_TOKEN_PATH
    ?? join(projectRoot, "Saved", "MCPPuerTSBridge", "token.txt");
  const token = (await readFile(tokenPath, "utf8")).trim();
  if (!token) throw new Error(`PuerTS bearer token is empty: ${tokenPath}`);
  return token;
}

/** The session manifest an editor publishes under its own project's Saved
    directory. This replaces pipe.txt as the thing a client resolves against.
    pipe.txt held a pipe name, which is enough to reach an editor and says
    nothing about which editor answered; with two editors open that difference
    is the whole problem. */
export interface BridgeSession extends JsonObject {
  schema_version: number;
  session_id: string;
  session_nonce: string;
  editor_pid: number;
  process_start_time: string;
  project_path: string;
  uproject_path: string;
  pipe_name: string;
  bridge_commit: string;
  installed_manifest_hash: string;
  created_at: string;
  last_heartbeat_at: string;
  shutdown_state: string;
}

/** A refusal to connect, with a code a caller can branch on. Every one of these
    is a case where the old client would have connected to SOMETHING - the
    compiled-in default pipe, a dead editor's advertised name, another project's
    editor - and reported success for work that landed in the wrong world. */
export class SessionError extends Error {
  constructor(readonly code: string, message: string, readonly detail: Record<string, unknown> = {}) {
    super(message);
    this.name = "SessionError";
  }
}

const SUPPORTED_SCHEMA_VERSION = 1;

/** Trailing separators and casing differ between what Unreal reports and what a
    caller sets in the environment; neither difference means a different project. */
function samePath(a: string, b: string): boolean {
  const norm = (p: string) => p.replace(/[\\/]+$/, "").replace(/\\/g, "/").toLowerCase();
  return norm(a) === norm(b);
}

/** Is that process still there? Signal 0 performs the permission and existence
    checks without delivering anything. EPERM means it exists and belongs to
    someone else, which for this purpose is alive. */
function processIsAlive(pid: number): boolean {
  if (!Number.isInteger(pid) || pid <= 0) return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch (error: unknown) {
    return (error as NodeJS.ErrnoException).code === "EPERM";
  }
}

/** Resolve the editor this client is addressing, or refuse.
 *
 *  There is deliberately no fallback anywhere in here. The previous version
 *  ended with `return "\\\\.\\pipe\\UE427PuerTSMCP"`, so a missing or stale
 *  advertisement silently sent every request to whichever editor happened to own
 *  the default pipe name. With one editor that reads as a convenience. With two
 *  it is a client writing into a project nobody asked it to touch, and reporting
 *  success. Refusing is the only safe answer to "I do not know which editor this
 *  is". */
export async function resolveSession(): Promise<BridgeSession> {
  const projectRoot = process.env.MCP_UNREAL_PROJECT_ROOT ?? process.cwd();
  const manifestPath = join(projectRoot, "Saved", "MCPPuerTSBridge", "session.json");

  let raw: string;
  try {
    raw = await readFile(manifestPath, "utf8");
  } catch {
    throw new SessionError("session_missing",
      `No editor session is advertised for ${projectRoot}. Expected ${manifestPath}. ` +
      "Either no editor is open for that project, or it is an older build that only writes pipe.txt. " +
      "This client will not guess a pipe name.",
      { project_root: projectRoot, manifest_path: manifestPath });
  }

  let session: BridgeSession;
  try {
    session = JSON.parse(raw) as BridgeSession;
  } catch {
    throw new SessionError("session_unreadable",
      `The session manifest at ${manifestPath} is not valid JSON. The editor writes it by moving a ` +
      "staged file over the target, so a partial read should be impossible; treat this as corruption.",
      { manifest_path: manifestPath });
  }

  if (session.schema_version !== SUPPORTED_SCHEMA_VERSION) {
    throw new SessionError("session_schema_unsupported",
      `The session manifest at ${manifestPath} is schema version ${session.schema_version}; ` +
      `this client understands ${SUPPORTED_SCHEMA_VERSION}. Refusing rather than reading it optimistically.`,
      { found: session.schema_version, supported: SUPPORTED_SCHEMA_VERSION });
  }
  if (session.shutdown_state !== "running") {
    throw new SessionError("session_shut_down",
      `The editor for ${session.project_path} recorded shutdown_state "${session.shutdown_state}" ` +
      `and is no longer serving. Session ${session.session_id}, pid ${session.editor_pid}.`,
      { session_id: session.session_id, shutdown_state: session.shutdown_state });
  }
  if (!processIsAlive(session.editor_pid)) {
    throw new SessionError("session_stale",
      `The session manifest at ${manifestPath} names pid ${session.editor_pid}, which is not running. ` +
      `The editor died without retracting its advertisement. Last heartbeat ${session.last_heartbeat_at}.`,
      { session_id: session.session_id, editor_pid: session.editor_pid, last_heartbeat_at: session.last_heartbeat_at });
  }

  // Explicit targeting. Both are opt-in: without them the project root selects
  // the editor, which is what makes a reconnection return to the same project
  // rather than to whichever editor came up most recently.
  const wantedSessionId = process.env.MCP_PUERTS_SESSION_ID?.trim();
  if (wantedSessionId && wantedSessionId !== session.session_id) {
    throw new SessionError("session_not_selected",
      `MCP_PUERTS_SESSION_ID requests session ${wantedSessionId}, but ${session.project_path} is ` +
      `currently running session ${session.session_id} (pid ${session.editor_pid}). An editor gets a new ` +
      "session id every start, so this usually means the editor restarted since the id was captured.",
      { requested: wantedSessionId, actual: session.session_id });
  }
  if (process.env.MCP_UNREAL_PROJECT_ROOT && !samePath(session.project_path, projectRoot)) {
    throw new SessionError("session_project_mismatch",
      `The manifest under ${projectRoot} advertises project ${session.project_path}. A session file ` +
      "describing a different project means the Saved directory was copied between projects, and " +
      "connecting would address the wrong editor.",
      { expected: projectRoot, advertised: session.project_path });
  }

  return session;
}

/** The pipe to connect to. MCP_PUERTS_PIPE still overrides the name, because the
    installer writes one into .mcp.json, but it can no longer decide WHICH editor
    the client believes it is talking to: the session identity is checked against
    the manifest on every response regardless of how the pipe was chosen. */
export async function resolvePipeName(): Promise<string> {
  const configured = process.env.MCP_PUERTS_PIPE?.trim();
  if (configured) return configured;
  return (await resolveSession()).pipe_name;
}

export class PuerTSClient {

  /** Send one command. `timeoutMs` is the budget for the whole round trip:
      the editor runs these on the game thread, so a command that authors and
      compiles an asset legitimately outlasts one that reads a property, and a
      shared 7 second ceiling would report a timeout for work Unreal is still
      finishing. Callers pass the tool's own budget; the default matches the
      inspection tools. */
  async call(
    command: string,
    params: Record<string, unknown>,
    timeoutMs = 7000,
  ): Promise<PuerTSResponse> {
    const token = await readToken();
    const session = await resolveSession();
    const pipeName = process.env.MCP_PUERTS_PIPE?.trim() || session.pipe_name;
    return new Promise<PuerTSResponse>((resolve, reject) => {
      const socket = createConnection(pipeName);
      socket.setEncoding("utf8");
      let buffer = "";
      let settled = false;
      const finish = (callback: () => void): void => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        socket.destroy();
        callback();
      };
      const timer = setTimeout(
        () => finish(() => reject(new Error("PuerTS command pipe timed out"))),
        timeoutMs,
      );
      socket.on("connect", () => socket.write(JSON.stringify({
        id: randomUUID(),
        command,
        tool: command,
        params,
        timeout_ms: Math.max(1000, timeoutMs - 2000),
        auth: token,
        // Addressed, not merely sent. The nonce is what the editor checks to
        // confirm the request meant it; expect_session_id closes the window
        // between reading the manifest and this write, in which the editor could
        // have restarted and published a new session on the same pipe name.
        session_nonce: session.session_nonce,
        expect_session_id: session.session_id,
      }) + "\n"));
      socket.on("data", (chunk: string) => {
        buffer += chunk;
        if (buffer.length > 1024 * 1024) {
          finish(() => reject(new Error("PuerTS response exceeded the 1 MiB limit")));
          return;
        }
        const newline = buffer.indexOf("\n");
        if (newline < 0) return;
        try {
          const parsed = JSON.parse(buffer.slice(0, newline)) as unknown;
          if (!isResponse(parsed)) throw new Error("PuerTS returned a non-conforming JSON response");
          // Who actually answered. The nonce stops a request reaching the wrong
          // editor; this stops the reverse, an answer arriving from one. Both
          // directions are needed because the two are different failures: a
          // wrong pipe name, a restarted editor reusing a name, a mixed-up
          // response. Checked on every response, not only on diagnostics.
          const answered = parsed.session;
          if (!isObject(answered)) {
            throw new SessionError("session_identity_absent",
              "The reply carries no session identity, so there is no way to tell which editor ran the " +
              "command. Every response from this bridge stamps one. Refusing rather than assuming it " +
              `was session ${session.session_id}.`,
              { addressed: session.session_id });
          }
          const mismatch =
            answered.session_id !== session.session_id
            || answered.editor_pid !== session.editor_pid
            || !samePath(String(answered.project_path ?? ""), session.project_path);
          if (mismatch) {
            throw new SessionError("session_identity_mismatch",
              `The reply came from session ${String(answered.session_id)} (pid ${String(answered.editor_pid)}, ` +
              `project ${String(answered.project_path)}), but this client addressed session ` +
              `${session.session_id} (pid ${session.editor_pid}, project ${session.project_path}). ` +
              "The command may have run in the wrong editor; treat any reported change as suspect.",
              // The responder's own errors travel with this. When the editor
              // refused the request on its side - a bad nonce, an unknown
              // session - that refusal is the useful diagnosis, and throwing on
              // the identity check would otherwise discard it and report only
              // the symptom.
              {
                addressed: session.session_id,
                answered: answered.session_id,
                answered_errors: parsed.errors,
                answered_message: parsed.message,
              });
          }
          finish(() => resolve(parsed));
        } catch (error: unknown) {
          finish(() => reject(error));
        }
      });
      socket.on("error", (error: Error) => finish(() => reject(error)));
    });
  }
}
