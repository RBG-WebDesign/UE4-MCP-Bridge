/**
 * Command status record: the only thing a caller can read about work in
 * flight while the Unreal game thread is blocked inside a native call.
 *
 * Why a file and not a second pipe. docs/PERF_AND_LONG_JOBS.md 6.3 proposes a
 * background FRunnable serving a second named pipe, because F3 (the Node event
 * loop turns on the game thread, JsEnvImpl.cpp:187-188,233) means this runtime
 * cannot answer a socket while a native call is running. A file needs no
 * runtime participation to be read: the record is written BEFORE the blocking
 * call returns to C++, and the operating system serves the read afterwards.
 * That removes the second ACL, the second auth decision and the FRunnable, and
 * delivers the same information. Every fact F1 to F6 still holds: this is not a
 * command, AcceptCommand is untouched, and nothing here is cancellable.
 *
 * What it can and cannot report, stated so nobody reads a promise into it:
 *
 *   CAN   which tool is executing, its command id, and since when.
 *   CAN   whether the previous command finished, and whether it succeeded.
 *   CANNOT report how far through that tool it is. Every registered tool body
 *         is one straight-line synchronous C++ call (registry.ts, F4), so
 *         between begin() and end() there is no point at which this runtime
 *         regains control. Per-pass stages inside the Blueprint builder need
 *         the C++ checkpoint instrumentation 6.3 describes; they are not here.
 *
 * The field is `stage`, never `percent` and never a fraction, for that reason.
 *
 * Off unless MCP_PUERTS_STATUS=1 is set in the editor's environment before
 * launch. Two synchronous file writes per command are a real cost on the
 * round-trip floor of the cheapest commands (diagnostic is 1.2-1.6 ms in
 * total), and this repository has no measurement of that cost yet. Measure it
 * with Scripts/perf-benchmark.mjs, then decide the default.
 */

export const STATUS_SCHEMA_VERSION = 1;
export const STATUS_KIND = "bridge-command-status";

interface FileSystem {
  mkdirSync(path: string, options: { recursive: boolean }): void;
  writeFileSync(path: string, data: string, encoding: "utf8"): void;
  renameSync(from: string, to: string): void;
}

interface PathModule {
  join(...parts: string[]): string;
}

export interface StatusRecorder {
  /** A command has been accepted and is about to enter native code. */
  begin(commandId: string, tool: string): void;
  /** That command returned. Records it as the last completed one. */
  end(commandId: string, tool: string, ok: boolean): void;
  /** True when this recorder actually writes a file. */
  readonly enabled: boolean;
}

interface Completed {
  tool: string;
  command_id: string;
  ok: boolean;
  duration_ms: number;
  finished_at_ms: number;
}

const inert: StatusRecorder = {
  enabled: false,
  begin(): void { /* disabled */ },
  end(): void { /* disabled */ },
};

/**
 * Build the record body. Exported so the shape is testable without a file
 * system, an editor, or a running command.
 */
export function statusRecord(
  stage: "idle" | "native_call",
  active: { tool: string; command_id: string; started_at_ms: number } | null,
  last: Completed | null,
  nowMs: number,
): Record<string, unknown> {
  return {
    schema_version: STATUS_SCHEMA_VERSION,
    kind: STATUS_KIND,
    stage,
    stage_note: "Stage only. There is no percent and no fraction: a tool body is one "
      + "synchronous native call with no interior checkpoint this runtime can observe.",
    tool: active === null ? "" : active.tool,
    command_id: active === null ? "" : active.command_id,
    started_at_ms: active === null ? 0 : active.started_at_ms,
    updated_at_ms: nowMs,
    last_completed: last,
  };
}

/**
 * A recorder that writes `<projectRoot>/Saved/MCPPuerTSBridge/status.json`.
 *
 * Returns the inert recorder unless `MCP_PUERTS_STATUS=1`. Writes go through a
 * staged file and a rename so a poller never parses a half-written record, the
 * same way the session manifest is published.
 *
 * A failed write NEVER fails the command. Status is a convenience; a command
 * that succeeded and could not be announced still succeeded.
 */
export function createStatusRecorder(projectRoot: string): StatusRecorder {
  if (process.env.MCP_PUERTS_STATUS !== "1") return inert;

  // Required lazily, so these go through the project-root guard that
  // safety.ts installs on module._load rather than around it.
  const fs = require("fs") as FileSystem;
  const path = require("path") as PathModule;
  const directory = path.join(projectRoot, "Saved", "MCPPuerTSBridge");
  const finalPath = path.join(directory, "status.json");
  const stagedPath = path.join(directory, "status.json.staged");

  let active: { tool: string; command_id: string; started_at_ms: number } | null = null;
  let last: Completed | null = null;

  const publish = (stage: "idle" | "native_call"): void => {
    try {
      fs.mkdirSync(directory, { recursive: true });
      fs.writeFileSync(stagedPath, JSON.stringify(statusRecord(stage, active, last, Date.now())), "utf8");
      fs.renameSync(stagedPath, finalPath);
    } catch (error: unknown) {
      console.error("MCP status record write failed: " + String(error));
    }
  };

  return {
    enabled: true,
    begin(commandId: string, tool: string): void {
      active = { tool, command_id: commandId, started_at_ms: Date.now() };
      publish("native_call");
    },
    end(commandId: string, tool: string, ok: boolean): void {
      const startedAt = active === null ? Date.now() : active.started_at_ms;
      const finishedAt = Date.now();
      last = {
        tool,
        command_id: commandId,
        ok,
        duration_ms: finishedAt - startedAt,
        finished_at_ms: finishedAt,
      };
      active = null;
      publish("idle");
    },
  };
}
