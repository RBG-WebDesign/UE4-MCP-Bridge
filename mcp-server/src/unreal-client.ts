/**
 * HTTP client for communicating with the UE4 Python listener.
 * 
 * Sends JSON commands to localhost:8080 and returns parsed responses.
 * This module is the only point of contact between the MCP server and Unreal.
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
}

const DEFAULT_OPTIONS: UnrealClientOptions = {
  host: "localhost",
  port: 8080,
  timeout: 300000,
};

export class UnrealClient {
  private options: UnrealClientOptions;

  constructor(options: Partial<UnrealClientOptions> = {}) {
    this.options = { ...DEFAULT_OPTIONS, ...options };
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
          error: `Connection failed: ${err.message}. Is the UE4 editor running with the MCP Bridge listener?`,
        });
      });

      req.on("timeout", () => {
        req.destroy();
        resolve({
          success: false,
          data: {},
          error: `Request timed out after ${this.options.timeout}ms`,
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
