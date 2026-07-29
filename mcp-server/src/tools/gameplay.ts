/**
 * Gameplay tools: PIE control, telemetry, acceptance tests.
 *
 * None of these commands are modifying in the undo-stack sense (PIE
 * sessions are transient). gameplay_pie_start is NOT added to modifyingCommands.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

export function createGameplayTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "gameplay_pie_start",
      description:
        "Launch a Play In Editor (PIE) session and wait until the game world is ready " +
        "(up to 30s). Returns success once the PIE-ready log marker is found. " +
        "Call this before gameplay_run_acceptance_tests or gameplay_telemetry_snapshot.",
      inputSchema: z.object({
        level_path: z
          .string()
          .optional()
          .describe(
            "Content path of the level to load before PIE (e.g. /Game/Maps/Gameplay). " +
            "If omitted, uses the current level."
          ),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_pie_start", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "gameplay_pie_stop",
      description: "End the current PIE session.",
      inputSchema: z.object({}),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_pie_stop", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "gameplay_pie_status",
      description:
        "Report whether a PIE session is live, and if so the possessed pawn class, visible widgets, AI controller " +
        "count and FPS. Poll this after gameplay_pie_start: that call returns as soon as the request is queued, " +
        "because PIE cannot initialise while a handler holds the game thread.",
      inputSchema: z.object({}),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_pie_status", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "gameplay_telemetry_snapshot",
      description:
        "Capture the current PIE runtime state: new log lines since last snapshot, " +
        "possessed pawn class, visible widget class names, AI controller states, and PIE world name. " +
        "PIE must be running.",
      inputSchema: z.object({}),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_telemetry_snapshot", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "gameplay_run_acceptance_tests",
      description:
        "Run structured acceptance test predicates against a live PIE session. " +
        "PIE must already be running (call gameplay_pie_start first). " +
        "Returns pass/fail per predicate with observed value. " +
        "Predicates: 'pawn_possessed:ClassName', 'widget_visible:WidgetName', " +
        "'log_contains:String', 'ai_state:ActorName:StateName', 'survive:N' (N seconds without crash).",
      inputSchema: z.object({
        tests: z
          .array(z.string())
          .min(1)
          .describe(
            "List of predicate strings. Examples: " +
            "'pawn_possessed:BP_Character', 'widget_visible:WBP_HUD', " +
            "'log_contains:GameStarted', 'ai_state:BP_Enemy:Patrol', 'survive:5'"
          ),
        timeout_seconds: z
          .number()
          .positive()
          .optional()
          .describe("Override timeout_seconds for all predicates (default: 5.0 per predicate)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_run_acceptance_tests", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
  ];
}
