/**
 * HTTP client for communicating with the UE4 Python listener.
 *
 * Sends JSON commands to the listener and returns parsed responses. This
 * module is the only point of contact between the MCP server and Unreal.
 *
 * Defaults to localhost:8080. Override with environment variables when the
 * MCP server and the editor are not on the same machine, e.g. when the server
 * runs in a container and reaches the editor over a tunnel:
 *
 *   UNREAL_BRIDGE_HOST=100.x.y.z      hostname or IP of the listener
 *   UNREAL_BRIDGE_PORT=8080           port
 *   UNREAL_BRIDGE_TIMEOUT_MS=320000   request timeout
 *   UNREAL_BRIDGE_TOKEN=<secret>      shared secret, sent as X-MCP-Bridge-Token
 *
 * SECURITY: the listener has no authentication and python_proxy executes
 * arbitrary Python inside the editor. Only point this at a host reachable
 * over a private network (Tailscale, WireGuard, an SSH tunnel bound to
 * loopback). Never expose the listener port to the public internet.
 */

import http from "http";

export interface UnrealResponse {
  success: boolean;
  data: Record<string, unknown>;
  error?: string;
}

export interface UnrealClientOptions {
  host: string;
  port: number;
  timeout: number;
  /** Shared secret. Empty means send no auth header. */
  token: string;
}

// Client timeout must exceed the listener's COMMAND_TIMEOUT_SECONDS (300s)
// so the server's structured timeout error reaches the caller instead of
// the client cutting the connection first.
const FALLBACK_HOST = "localhost";
const FALLBACK_PORT = 8080;
const FALLBACK_TIMEOUT = 320000;

/**
 * Parse an integer environment variable, ignoring values that are absent,
 * malformed, or out of range. A typo in a tunnel port should fall back to the
 * default rather than silently producing NaN and an unroutable request.
 */
function envInt(name: string, fallback: number, min: number, max: number): number {
  const raw = process.env[name];
  if (raw === undefined || raw.trim() === "") return fallback;
  const text = raw.trim();
  // Reject anything that is not purely an integer before parsing. parseInt
  // stops at the first non-digit, so "80.5" would otherwise become port 80 --
  // a different, silently wrong endpoint rather than an error.
  const parsed = /^-?\d+$/.test(text) ? Number.parseInt(text, 10) : Number.NaN;
  if (!Number.isFinite(parsed) || parsed < min || parsed > max) {
    console.error(
      `[Unreal MCP Bridge] Ignoring ${name}="${raw}": expected an integer ` +
      `between ${min} and ${max}. Using ${fallback}.`
    );
    return fallback;
  }
  return parsed;
}

function envHost(): string {
  const raw = process.env.UNREAL_BRIDGE_HOST;
  if (raw === undefined || raw.trim() === "") return FALLBACK_HOST;
  const value = raw.trim();
  // A literal "${...}" means a config file used shell-style interpolation that
  // the MCP client did not expand. Treating that as a hostname produces a
  // baffling DNS error, so fall back and say why.
  if (value.includes("${")) {
    console.error(
      `[Unreal MCP Bridge] UNREAL_BRIDGE_HOST="${raw}" looks like an unexpanded ` +
      `placeholder. Set the variable in the environment that launches the MCP ` +
      `server rather than relying on interpolation in .mcp.json. Using ${FALLBACK_HOST}.`
    );
    return FALLBACK_HOST;
  }
  return value;
}

export function resolveClientDefaults(): UnrealClientOptions {
  return {
    host: envHost(),
    port: envInt("UNREAL_BRIDGE_PORT", FALLBACK_PORT, 1, 65535),
    timeout: envInt("UNREAL_BRIDGE_TIMEOUT_MS", FALLBACK_TIMEOUT, 1000, 3600000),
    token: (process.env.UNREAL_BRIDGE_TOKEN ?? "").trim(),
  };
}

export class UnrealClient {
  private options: UnrealClientOptions;

  constructor(options: Partial<UnrealClientOptions> = {}) {
    // Env vars are read per instance rather than once at module load so tests
    // can vary them, and so a wrapper process that sets them late still works.
    this.options = { ...resolveClientDefaults(), ...options };
  }

  /** The endpoint this client will talk to. Used for startup diagnostics. */
  get endpoint(): string {
    return `http://${this.options.host}:${this.options.port}`;
  }

  /**
   * Send a command to the UE4 Python listener.
   */
  async sendCommand(
    command: string,
    params: Record<string, unknown> = {}
  ): Promise<UnrealResponse> {
    const payload = JSON.stringify({ command, params });

    return new Promise<UnrealResponse>((resolve, reject) => {
      const requestOptions: http.RequestOptions = {
        hostname: this.options.host,
        port: this.options.port,
        path: "/",
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Content-Length": Buffer.byteLength(payload),
          // Omitted entirely when unset, so a listener with no token
          // configured sees exactly the requests it saw before.
          ...(this.options.token ? { "X-MCP-Bridge-Token": this.options.token } : {}),
        },
        timeout: this.options.timeout,
      };

      const req = http.request(requestOptions, (res) => {
        let data = "";

        res.on("data", (chunk: Buffer) => {
          data += chunk.toString();
        });

        res.on("end", () => {
          try {
            const parsed = JSON.parse(data) as UnrealResponse;
            resolve(parsed);
          } catch {
            if (res.statusCode === 401) {
              resolve({
                success: false,
                data: {},
                error:
                  `Listener at ${this.endpoint} rejected the request as unauthorized. ` +
                  `It has MCP_BRIDGE_TOKEN set; set UNREAL_BRIDGE_TOKEN to the same ` +
                  `value in the environment that launches this MCP server.`,
              });
              return;
            }
            resolve({
              success: false,
              data: {},
              error: `Failed to parse response: ${data.substring(0, 200)}`,
            });
          }
        });
      });

      req.on("error", (err: Error) => {
        resolve({
          success: false,
          data: {},
          error:
            `Connection failed talking to ${this.endpoint}: ${err.message}. ` +
            `Is the UE4 editor running with the MCP Bridge listener, and is ` +
            `that endpoint reachable from here? Set UNREAL_BRIDGE_HOST / ` +
            `UNREAL_BRIDGE_PORT if the editor is on another machine.`,
        });
      });

      req.on("timeout", () => {
        req.destroy();
        resolve({
          success: false,
          data: {},
          error:
            `Request to ${this.endpoint} timed out after ${this.options.timeout}ms. ` +
            `Raise UNREAL_BRIDGE_TIMEOUT_MS if a slow link or a long editor ` +
            `operation needs more headroom.`,
        });
      });

      req.write(payload);
      req.end();
    });
  }

  /**
   * Quick connection check.
   */
  async ping(): Promise<boolean> {
    const response = await this.sendCommand("ping");
    return response.success;
  }
}
