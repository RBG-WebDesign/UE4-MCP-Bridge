/**
 * PIE gameplay agent tools: runtime hands and eyes for play-testing.
 *
 * Long-running calls start a C++ operation and poll its status outside the
 * editor process, so the Python game-thread handler never blocks a frame.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const POLL_MS = 300;

interface BridgeResult {
  success: boolean;
  data: Record<string, unknown>;
  error?: string | null;
}

function textResult(payload: unknown): { content: Array<{ type: "text"; text: string }> } {
  return { content: [{ type: "text", text: JSON.stringify(payload, null, 2) }] };
}

async function runAsyncOperation(
  client: UnrealClient,
  command: string,
  params: Record<string, unknown>,
  budgetSeconds: number,
): Promise<BridgeResult> {
  const start = await client.sendCommand(command, params) as BridgeResult;
  if (!start.success) return start;

  const operationId = Number(start.data.operation_id ?? 0);
  const deadline = Date.now() + budgetSeconds * 1000;
  for (;;) {
    if (Date.now() > deadline) {
      return {
        success: false,
        data: start.data,
        error: `${command} did not reach a terminal state within ${budgetSeconds}s (operation ${operationId} may still be running)`,
      };
    }
    await new Promise<void>((resolve) => setTimeout(resolve, POLL_MS));
    const status = await client.sendCommand("pie_agent_status", { operation_id: operationId }) as BridgeResult;
    if (!status.success) return status;
    const state = String(status.data.status ?? "");
    if (state === "succeeded" || state === "failed") {
      const result = (status.data.result as Record<string, unknown> | undefined) ?? {};
      const error = status.data.error as string | undefined;
      return {
        success: state === "succeeded",
        data: { operation_id: operationId, ...result },
        error: error ?? null,
      };
    }
  }
}

export function createPieAgentTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "pie_agent_move_to",
      description: "Walk the PIE player pawn to a location or named actor along a nav path, with a straight-line fallback. Resolves when arrived or timed out.",
      inputSchema: z.object({
        location: z.array(z.number()).length(3).optional().describe("Target [x,y,z] in world units"),
        actor: z.string().optional().describe("Target actor by name or class substring"),
        acceptance_radius: z.number().positive().optional().describe("Arrival distance in uu (default 75)"),
        timeout: z.number().positive().optional().describe("Seconds before the move fails (default 20)"),
      }),
      handler: async (params) => textResult(
        await runAsyncOperation(client, "pie_agent_move_to", params, Number(params.timeout ?? 20) + 5),
      ),
    },
    {
      name: "pie_agent_look_at",
      description: "Point the PIE player camera at a location or named actor.",
      inputSchema: z.object({
        location: z.array(z.number()).length(3).optional(),
        actor: z.string().optional(),
      }),
      handler: async (params) => textResult(await client.sendCommand("pie_agent_look_at", params)),
    },
    {
      name: "pie_agent_press",
      description: "Press an action mapping through live InputComponent bindings, or a raw key through the PlayerController, then release it after the requested hold.",
      inputSchema: z.object({
        action: z.string().optional().describe("Action mapping name, such as UI_Interact"),
        key: z.string().optional().describe("Raw key name, such as E"),
        hold_seconds: z.number().min(0).optional().describe("Hold duration before release (default 0.1)"),
      }),
      handler: async (params) => textResult(
        await runAsyncOperation(client, "pie_agent_press", params, Number(params.hold_seconds ?? 0.1) + 8),
      ),
    },
    {
      name: "pie_agent_observe",
      description: "Capture a PIE snapshot: pawn transform and velocity, nearby actors with physics and collision state, widgets, player counters, and Output Log tail.",
      inputSchema: z.object({
        radius: z.number().positive().optional().describe("Actor scan radius around the pawn in uu (default 1500)"),
        class_filter: z.string().optional().describe("Only include actors whose class contains this text"),
        log_lines: z.number().int().min(0).optional().describe("Output Log tail length (default 20)"),
      }),
      handler: async (params) => textResult(await client.sendCommand("pie_agent_observe", params)),
    },
    {
      name: "pie_agent_record_start",
      description: "Start frame-accurate spawn, destroy, and overlap event recording for filtered actor classes.",
      inputSchema: z.object({
        events: z.array(z.enum(["spawn", "destroy", "overlap"])).optional().describe("Event types to record (default all)"),
        class_filters: z.array(z.string()).optional().describe("Actor class substrings to watch (default all)"),
        log_categories: z.array(z.string()).optional().describe("Reserved Output Log category filters"),
        max_events: z.number().int().positive().optional().describe("Ring buffer cap (default 4096)"),
      }),
      handler: async (params) => textResult(await client.sendCommand("pie_agent_record_start", params)),
    },
    {
      name: "pie_agent_record_stop",
      description: "Stop PIE event recording and return the timestamped event list.",
      inputSchema: z.object({}),
      handler: async (params) => textResult(await client.sendCommand("pie_agent_record_stop", params)),
    },
    {
      name: "pie_agent_expect",
      description: "Poll declarative actor_count, player counter, and log regex assertions in-engine until they pass or the deadline expires.",
      inputSchema: z.object({
        conditions: z.array(z.string()).min(1),
        within_seconds: z.number().positive().optional().describe("Deadline in seconds (default 5)"),
      }),
      handler: async (params) => textResult(
        await runAsyncOperation(client, "pie_agent_expect", params, Number(params.within_seconds ?? 5) + 5),
      ),
    },
    {
      name: "pie_agent_replay",
      description: "Replay a PIE agent script with a fixed random seed for regression testing, or export the current session script.",
      inputSchema: z.object({
        script: z.array(z.object({ command: z.string(), params: z.record(z.unknown()) })).optional(),
        seed: z.number().int().optional().describe("FMath::RandInit seed (default 1337)"),
        export: z.boolean().optional().describe("Return the current session script without running it"),
        budget_seconds: z.number().positive().optional().describe("Whole-replay wall-clock budget (default 120)"),
      }),
      handler: async (params) => {
        if (params.export) return textResult(await client.sendCommand("pie_agent_replay", params));
        return textResult(await runAsyncOperation(client, "pie_agent_replay", params, Number(params.budget_seconds ?? 120)));
      },
    },
    {
      name: "pie_agent_status",
      description: "Poll the current PIE agent asynchronous operation. Operation id 0 means the latest operation.",
      inputSchema: z.object({
        operation_id: z.number().int().nonnegative().optional(),
      }),
      handler: async (params) => textResult(await client.sendCommand("pie_agent_status", params)),
    },
  ];
}
