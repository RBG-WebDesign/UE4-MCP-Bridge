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

/** The pipe to connect to, resolved the same way the token is: an explicit
    MCP_PUERTS_PIPE wins, otherwise the pipe name the editor advertised beside
    its token (so a [MCPPuerTSBridge] PipeName override in the project needs no
    matching client config), otherwise the plugin's compiled-in default. */
export async function resolvePipeName(): Promise<string> {
  const configured = process.env.MCP_PUERTS_PIPE?.trim();
  if (configured) return configured;
  const projectRoot = process.env.MCP_UNREAL_PROJECT_ROOT ?? process.cwd();
  try {
    const advertised =
      (await readFile(join(projectRoot, "Saved", "MCPPuerTSBridge", "pipe.txt"), "utf8")).trim();
    if (advertised) return advertised;
  } catch {
    // No advertised pipe; the editor may predate pipe.txt. Use the default.
  }
  return "\\\\.\\pipe\\UE427PuerTSMCP";
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
    const pipeName = await resolvePipeName();
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
          finish(() => resolve(parsed));
        } catch (error: unknown) {
          finish(() => reject(error));
        }
      });
      socket.on("error", (error: Error) => finish(() => reject(error)));
    });
  }
}
