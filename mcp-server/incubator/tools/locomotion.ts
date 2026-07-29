/** UE4.27 motion-matching and KAI locomotion debugger tools. */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

type Parameters = Record<string, unknown>;

function response(result: unknown) {
  return {
    content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
  };
}

async function send(client: UnrealClient, operation: string, params: Parameters) {
  return response(await client.sendCommand("locomotion_debug", { ...params, operation }));
}

export function createLocomotionTools(client: UnrealClient): ToolDefinition[] {
  const empty = z.object({});
  const captureFile = z
    .string()
    .optional()
    .describe("Capture filename inside Saved/LocomotionTests. Omit to use the newest capture.");
  return [
    {
      name: "locomotion_debug_snapshot",
      description:
        "Read one live PIE locomotion snapshot. KAI targets return live state, direction, mesh, skeleton, and active AnimGraph players; native matching targets return state, eligibility gates, query, selected animation, " +
        "authoritative candidates, costs, trajectory, pose contacts, AnimGraph evaluators, and search timing. " +
        "This never starts PIE.",
      inputSchema: z.object({
        include_candidates: z.boolean().default(true).describe("Re-score the cached query through the real selector and include its top five."),
      }),
      handler: async (params) => send(client, "snapshot", params),
    },
    {
      name: "locomotion_capture_start",
      description: "Start the top-right PIE JSONL recorder for a native motion-matching or KAI pawn after an optional countdown. Does not start PIE.",
      inputSchema: z.object({
        countdown_seconds: z.number().min(0).max(10).default(0),
      }),
      handler: async (params) => send(client, "capture_start", params),
    },
    {
      name: "locomotion_capture_mark",
      description: "Write a segment_start marker into the active locomotion capture.",
      inputSchema: empty,
      handler: async (params) => send(client, "capture_mark", params),
    },
    {
      name: "locomotion_capture_flag",
      description:
        "Add a repeatable visual_glitch marker to the active motion-matching or KAI capture without stopping it.",
      inputSchema: z.object({
        note: z.string().max(160).default("").describe("Optional short description of the visible hiccup."),
      }),
      handler: async (params) => send(client, "capture_flag", params),
    },
    {
      name: "locomotion_capture_stop",
      description: "Finalize the active locomotion capture, or cancel it while preserving the partial JSONL file.",
      inputSchema: z.object({ cancel: z.boolean().default(false) }),
      handler: async (params) => send(client, "capture_stop", params),
    },
    {
      name: "locomotion_capture_status",
      description: "Read recorder state, elapsed time, row count, detector count, and output path for live PIE.",
      inputSchema: z.object({
        flush: z.boolean().default(false).describe("Flush buffered rows before returning status."),
      }),
      handler: async (params) => send(client, "capture_status", params),
    },
    {
      name: "locomotion_capture_list",
      description: "List JSONL locomotion captures in Saved/LocomotionTests, newest first.",
      inputSchema: empty,
      handler: async (params) => send(client, "capture_list", params),
    },
    {
      name: "locomotion_capture_get",
      description:
        "Read and summarize a bounded JSONL locomotion capture. File access is restricted to Saved/LocomotionTests.",
      inputSchema: z.object({
        file: captureFile,
        limit: z.number().int().min(1).max(500).default(200),
        include_rows: z.boolean().default(true),
      }),
      handler: async (params) => send(client, "capture_get", params),
    },
    {
      name: "motion_query_inspect",
      description:
        "Inspect the exact cached motion query used by the runtime selector, including current and desired motion, " +
        "phase, gait, direction, trajectory, and pose features.",
      inputSchema: empty,
      handler: async (params) => send(client, "query_inspect", params),
    },
    {
      name: "motion_candidates_inspect",
      description:
        "Re-run the cached query through the authoritative selector and return its top five candidates with every cost term and filter count.",
      inputSchema: empty,
      handler: async (params) => send(client, "candidates_inspect", params),
    },
    {
      name: "motion_database_validate",
      description:
        "Validate a locomotion database using the native builder checks plus missing animation, range, finite-feature, velocity-outlier, and contact checks.",
      inputSchema: z.object({
        database_path: z
          .string()
          .default("")
          .describe("UE object path. Leave blank to validate the database on the live PIE pawn."),
      }),
      handler: async (params) => send(client, "database_validate", params),
    },
    {
      name: "trajectory_debug",
      description: "Inspect the selector's predicted trajectory beside current CharacterMovement velocity.",
      inputSchema: empty,
      handler: async (params) => send(client, "trajectory_debug", params),
    },
    {
      name: "pose_history_inspect",
      description: "Inspect the current pose-query features used for matching: pelvis and foot positions, velocities, and contacts.",
      inputSchema: empty,
      handler: async (params) => send(client, "pose_history_inspect", params),
    },
    {
      name: "animation_state_inspect",
      description:
        "Inspect the component playback request and AnimGraph node evaluators, blend state, generation, and request application path.",
      inputSchema: empty,
      handler: async (params) => send(client, "animation_state_inspect", params),
    },
    {
      name: "root_motion_inspect",
      description:
        "Compare baked root-motion metadata at the selected sample with actual capsule velocity. Playback clips are intentionally in-place.",
      inputSchema: empty,
      handler: async (params) => send(client, "root_motion_inspect", params),
    },
    {
      name: "contact_debug",
      description: "Inspect current left and right foot contacts and the pose features feeding the matcher.",
      inputSchema: empty,
      handler: async (params) => send(client, "contact_debug", params),
    },
    {
      name: "locomotion_overlay_set",
      description: "Configure live PIE trajectory, candidate, and freeze-selection debug overlays.",
      inputSchema: z.object({
        draw_debug: z.boolean().default(true),
        show_candidates: z.boolean().default(true),
        freeze_selection: z.boolean().default(false),
      }),
      handler: async (params) => send(client, "overlay_set", params),
    },
    {
      name: "locomotion_force_candidate",
      description:
        "Temporarily force one database sample on the live PIE pawn, or clear the override. The override is component-local and never saved.",
      inputSchema: z.object({
        sample_index: z.number().int().min(0).optional(),
        clear: z.boolean().default(false),
        allow_incompatible: z.boolean().default(false),
      }),
      handler: async (params) => {
        if (params.clear || params.sample_index === undefined) {
          return send(client, "force_candidate_clear", params);
        }
        return send(client, "force_candidate", params);
      },
    },
    {
      name: "locomotion_weights_set",
      description:
        "Temporarily replace matcher scoring weights on the live PIE component, or clear the override. Project settings are never modified or saved.",
      inputSchema: z.object({
        reset: z.boolean().default(false),
        trajectory: z.number().min(0).optional(),
        velocity: z.number().min(0).optional(),
        facing: z.number().min(0).optional(),
        pelvis: z.number().min(0).optional(),
        feet: z.number().min(0).optional(),
        continuity: z.number().min(0).optional(),
      }),
      handler: async (params) => {
        if (params.reset) {
          return send(client, "weights_clear", params);
        }
        return send(client, "weights_set", params);
      },
    },
    {
      name: "locomotion_capture_compare",
      description:
        "Compare two bounded JSONL captures, summarize detector and performance differences, and report the first phase, animation, or generation divergence.",
      inputSchema: z.object({
        left_file: z.string().min(1),
        right_file: z.string().min(1),
        limit: z.number().int().min(1).max(500).default(500),
      }),
      handler: async (params) => send(client, "capture_compare", params),
    },
    {
      name: "locomotion_performance_get",
      description:
        "Read live selector time, or summarize search-time percentiles, hitches, fallback frames, and switch rate from a capture.",
      inputSchema: z.object({
        file: captureFile,
        limit: z.number().int().min(1).max(500).default(500),
      }),
      handler: async (params) => send(client, "performance_get", params),
    },
  ];
}
