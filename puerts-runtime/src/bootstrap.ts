import * as net from "net";
import * as UE from "ue";
import * as puerts from "puerts";
import { ToolRegistry, toolDefinitions } from "./registry";
import { installRuntimeGuards } from "./safety";
import { createStatusRecorder } from "./status";
import { CommandRequest, JsonObject, response } from "./types";

interface NativeEnvelope {
  readonly accepted: boolean;
  readonly request?: JsonObject;
  readonly response?: JsonObject;
}

function isJsonObject(value: unknown): value is JsonObject {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function parseRequest(value: unknown): CommandRequest {
  if (!isJsonObject(value)) throw new Error("Native request envelope is invalid");
  if (typeof value.id !== "string" || value.id.length === 0) throw new Error("Request id is required");
  if (typeof value.tool !== "string" || value.tool.length === 0) throw new Error("Tool name is required");
  const params = value.params === undefined ? {} : value.params;
  if (!isJsonObject(params)) throw new Error("params must be a JSON object");
  return {
    id: value.id,
    tool: value.tool,
    params,
    timeout_ms: typeof value.timeout_ms === "number" ? value.timeout_ms : undefined,
    transaction_id: typeof value.transaction_id === "string" ? value.transaction_id : "",
  };
}

function failureJson(message: string, error: unknown): string {
  const result = response(false, message);
  result.errors.push(error instanceof Error ? error.message : String(error));
  return JSON.stringify(result);
}

const bridge = puerts.argv.getByName("bridge") as UE.MCPPuerTSBridgeService;
if (bridge === undefined || bridge === null) throw new Error("Native MCP bridge argument is unavailable");
installRuntimeGuards(bridge.GetProjectRoot(), bridge.AreShellCommandsAllowed());
process.chdir(bridge.GetProjectRoot());
const registry = new ToolRegistry(toolDefinitions);
// Written before the runtime hands the game thread to native code, so a client
// can tell "working" from "hung" while nothing can be read off this pipe. See
// status.ts for what it can and cannot report.
const status = createStatusRecorder(bridge.GetProjectRoot());
let commandQueue: Promise<void> = Promise.resolve();

/** Milliseconds for measuring durations inside this process only. Never
    reported as a timestamp and never compared against the server's clock.
    Date.now rather than process.hrtime because PuerTS's Node typing exposes
    only chdir and env on process; a wall clock can step under NTP, which at the
    millisecond scale these commands run at is not worth a native binding. */
function nowMs(): number {
  return Date.now();
}

async function executeLine(line: string, socket: net.Socket, queuedAtMs: number): Promise<void> {
  let commandId = "";
  let commandTool = "";
  // How long this request waited behind another command. Commands are
  // serialized on the game thread on purpose, so this is the number that says
  // whether a slow call was slow itself or was stuck behind one that was.
  const runtimeQueueMs = nowMs() - queuedAtMs;
  try {
    const envelope = JSON.parse(bridge.AcceptCommand(line)) as NativeEnvelope;
    if (!envelope.accepted) {
      socket.end(JSON.stringify(envelope.response ?? JSON.parse(failureJson("Command rejected.", "Native bridge rejected the request"))) + "\n");
      return;
    }
    const request = parseRequest(envelope.request);
    commandId = request.id;
    commandTool = request.tool;
    status.begin(commandId, commandTool);
    const result = await registry.execute(
      { bridge, transactionId: request.transaction_id },
      request.tool,
      request.params,
    );
    status.end(commandId, commandTool, result.success === true);
    result.transaction_id = request.transaction_id;
    result.timings_ms = {
      runtime_queue: runtimeQueueMs,
      runtime_total: nowMs() - queuedAtMs,
    };
    socket.end(bridge.CompleteCommand(commandId, JSON.stringify(result)) + "\n");
  } catch (error: unknown) {
    if (commandId.length > 0) status.end(commandId, commandTool, false);
    const failed = failureJson("Command failed before completion.", error);
    socket.end((commandId.length > 0 ? bridge.CompleteCommand(commandId, failed) : failed) + "\n");
  }
}

const server = net.createServer((socket: net.Socket) => {
  socket.setEncoding("utf8");
  socket.setTimeout(bridge.GetRequestTimeoutMilliseconds() + 2000, () => socket.destroy(new Error("Command pipe timed out")));
  let buffer = "";
  let queued = false;
  socket.on("data", (chunk: string) => {
    if (queued) return;
    buffer += chunk;
    if (buffer.length > bridge.GetMaximumRequestBytes()) {
      queued = true;
      socket.end(failureJson("Request rejected.", "JSON request exceeds the configured size limit") + "\n");
      return;
    }
    const newline = buffer.indexOf("\n");
    if (newline < 0) return;
    queued = true;
    const line = buffer.slice(0, newline).trim();
    // Stamped where the request ARRIVES, not where it runs, so the wait behind
    // the serial queue is inside the measurement rather than invisible to it.
    const queuedAtMs = nowMs();
    commandQueue = commandQueue.then(() => executeLine(line, socket, queuedAtMs)).catch((error: unknown) => {
      socket.end(failureJson("Command queue failed.", error) + "\n");
    });
  });
  socket.on("error", (error: Error) => console.error("MCP pipe socket error: " + error.message));
});

server.on("error", (error: Error) => {
  bridge.SetRuntimeReady(0);
  console.error("MCP named-pipe server error: " + error.message);
});
server.listen(bridge.GetPipeName(), () => {
  bridge.SetRuntimeReady(toolDefinitions.length);
  console.log("MCP PuerTS named pipe ready with " + toolDefinitions.length + " approved tools");
});
