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

export class PuerTSClient {
  readonly pipeName = process.env.MCP_PUERTS_PIPE ?? "\\\\.\\pipe\\UE427PuerTSMCP";

  async call(command: string, params: Record<string, unknown>): Promise<PuerTSResponse> {
    const token = await readToken();
    return new Promise<PuerTSResponse>((resolve, reject) => {
      const socket = createConnection(this.pipeName);
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
        7000,
      );
      socket.on("connect", () => socket.write(JSON.stringify({
        id: randomUUID(), command, tool: command, params, timeout_ms: 5000, auth: token,
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
