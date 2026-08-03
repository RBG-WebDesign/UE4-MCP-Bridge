/**
 * Command status: read the editor's in-flight command record off disk.
 *
 * This tool never touches the named pipe and never reaches the editor. It
 * reads `<project>/Saved/MCPPuerTSBridge/status.json`, which the PuerTS runtime
 * writes before it hands the game thread to a native call
 * (puerts-runtime/src/status.ts). That is the whole point: while a Blueprint
 * compile holds the game thread, the pipe cannot be serviced at all
 * (docs/PERF_AND_LONG_JOBS.md F3), so anything that asks the editor "what are
 * you doing" is answered only after the answer stopped mattering. A file was
 * written before the block and is served by the operating system during it.
 *
 * It reports a STAGE, never a percent. Between begin and end there is one
 * synchronous native call with no interior checkpoint, so "how far through" is
 * not knowable here. See status.ts in the runtime for the full limit.
 *
 * Classified server_local alongside engine_source_*: it performs no editor
 * operation, so the puerts_*-only protocol in AGENTS.md is not engaged.
 */

import { existsSync, readFileSync } from "fs";
import { join } from "path";
import { z } from "zod";
import type { ToolDefinition } from "../types.js";

export const STATUS_KIND = "bridge-command-status";

const StatusInputSchema = z.object({}).strict();

/** Same resolution the pipe client uses, so both tools address one project. */
export function statusRecordPath(env: NodeJS.ProcessEnv = process.env): string {
  const projectRoot = env.MCP_UNREAL_PROJECT_ROOT ?? process.cwd();
  return join(projectRoot, "Saved", "MCPPuerTSBridge", "status.json");
}

function envelope(
  success: boolean,
  data: Record<string, unknown>,
  errors: string[] = [],
): Record<string, unknown> {
  return { success, data, errors, transport: "server_local", tool_source: "status_record_file" };
}

/**
 * Turn the two files on disk into an answer. Pure, so every branch is testable
 * without an editor: the reader passes the raw text it found (or null) and the
 * clock it wants to measure age against.
 *
 * A record left behind by a previous editor session is the failure mode that
 * matters most here: it looks exactly like a live one and would report a
 * command that finished before this editor started. It is detected by
 * comparing the record against the session manifest's own creation time, and
 * reported rather than silently served.
 */
export function readStatus(
  recordText: string | null,
  sessionText: string | null,
  nowMs: number,
): Record<string, unknown> {
  if (recordText === null) {
    return envelope(false, { status_error_code: "status_record_absent" }, [
      "No status record exists for this project. The editor's PuerTS runtime writes one "
      + "only when MCP_PUERTS_STATUS=1 is set in its environment before launch. Without it "
      + "there is no way to observe a command in flight, because the command pipe cannot be "
      + "serviced while the game thread is busy.",
    ]);
  }

  let record: Record<string, unknown>;
  try {
    const parsed: unknown = JSON.parse(recordText);
    if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) {
      throw new Error("the record is not a JSON object");
    }
    record = parsed as Record<string, unknown>;
  } catch (error: unknown) {
    return envelope(false, { status_error_code: "status_record_unreadable" }, [
      `The status record is not readable JSON: ${error instanceof Error ? error.message : String(error)}. `
      + `First 200 characters: ${recordText.slice(0, 200)}`,
    ]);
  }

  if (record.kind !== STATUS_KIND) {
    return envelope(false, { status_error_code: "status_record_malformed", found_kind: record.kind ?? null }, [
      `The status record is not a ${STATUS_KIND}. Its "kind" is ${JSON.stringify(record.kind ?? null)}. `
      + "Something other than the PuerTS runtime wrote this file.",
    ]);
  }

  const stage = typeof record.stage === "string" ? record.stage : "";
  const updatedAt = typeof record.updated_at_ms === "number" ? record.updated_at_ms : 0;
  const startedAt = typeof record.started_at_ms === "number" ? record.started_at_ms : 0;

  // A record that predates the editor's own session is a leftover. Reported,
  // never served as current: it would name a command that finished before this
  // editor existed, which is worse than saying nothing.
  let sessionCreatedAtMs: number | null = null;
  try {
    if (sessionText !== null) {
      const session = JSON.parse(sessionText) as { created_at?: unknown };
      const parsed = Date.parse(String(session.created_at ?? ""));
      if (Number.isFinite(parsed)) sessionCreatedAtMs = parsed;
    }
  } catch {
    sessionCreatedAtMs = null;
  }
  const stale = sessionCreatedAtMs !== null && updatedAt > 0 && updatedAt < sessionCreatedAtMs;

  if (stale) {
    return envelope(false, {
      status_error_code: "status_record_from_previous_session",
      record_updated_at_ms: updatedAt,
      session_created_at_ms: sessionCreatedAtMs,
    }, [
      "The status record was last written before this editor session started, so it describes "
      + "a previous editor. Nothing is reported as in flight. Confirm MCP_PUERTS_STATUS=1 is set "
      + "for the running editor.",
    ]);
  }

  return envelope(true, {
    stage,
    running: stage === "native_call",
    tool: typeof record.tool === "string" ? record.tool : "",
    command_id: typeof record.command_id === "string" ? record.command_id : "",
    running_ms: stage === "native_call" && startedAt > 0 ? nowMs - startedAt : null,
    record_age_ms: updatedAt > 0 ? nowMs - updatedAt : null,
    last_completed: record.last_completed ?? null,
    reports: "stage",
    percent_available: false,
    percent_unavailable_reason:
      "A tool body is one synchronous native call. The runtime regains control only when it "
      + "returns, so no fraction of it is observable. Per-pass stages need C++ checkpoints "
      + "inside the builder (docs/PERF_AND_LONG_JOBS.md 6.3).",
  });
}

export function createStatusTools(): ToolDefinition[] {
  return [
    {
      name: "bridge_command_status",
      description:
        "[SERVER LOCAL] Report which native command the editor is executing right now, and for "
        + "how long, by reading the status record from disk. Does not contact the editor, so it "
        + "answers while the game thread is blocked and the command pipe cannot be serviced. "
        + "Reports a stage, never a percent: there is no observable progress inside a single "
        + "native call. Requires MCP_PUERTS_STATUS=1 in the editor's environment at launch; "
        + "without it this refuses with status_error_code status_record_absent rather than "
        + "reporting an idle editor.",
      inputSchema: StatusInputSchema,
      handler: async () => {
        const recordPath = statusRecordPath();
        const sessionPath = join(recordPath, "..", "session.json");
        const read = (path: string): string | null => {
          try {
            return existsSync(path) ? readFileSync(path, "utf-8") : null;
          } catch {
            return null;
          }
        };
        const result = readStatus(read(recordPath), read(sessionPath), Date.now());
        (result.data as Record<string, unknown>).record_path = recordPath;
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
  ];
}
