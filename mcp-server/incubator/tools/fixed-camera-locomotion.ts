/** UE4.27 fixed-camera locomotion runtime debugger tools. */

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
  return response(await client.sendCommand("fixed_camera_locomotion_debug", { ...params, operation }));
}

const transitionSection = z.enum([
  "Start_F",
  "Start_R",
  "Start_B",
  "Start_L",
  "Stop_F",
  "Stop_R",
  "Stop_B",
  "Stop_L",
  "Turn90_L",
  "Turn90_R",
  "Spin180_L",
  "Spin180_R",
]);

export function createFixedCameraLocomotionTools(client: UnrealClient): ToolDefinition[] {
  const empty = z.object({});
  return [
    {
      name: "fixed_camera_locomotion_snapshot",
      description:
        "Read the live fixed-camera PIE pawn state, input basis, camera revision, direction, plant foot, montage section, transition locks, and debug overlay status. This never starts PIE.",
      inputSchema: empty,
      handler: async (params) => send(client, "snapshot", params),
    },
    {
      name: "fixed_camera_locomotion_overlay_set",
      description:
        "Enable or disable the live fixed-camera locomotion world overlay and direction arrows. The setting is runtime-only and is never saved.",
      inputSchema: z.object({ enabled: z.boolean().default(true) }),
      handler: async (params) => send(client, "overlay_set", params),
    },
    {
      name: "fixed_camera_locomotion_transition_force",
      description:
        "Force one known start, stop, 90-degree turn, or 180-degree reversal montage section on the live fixed-camera PIE pawn. This never changes animation assets.",
      inputSchema: z.object({ section: transitionSection }),
      handler: async (params) => send(client, "transition_force", params),
    },
    {
      name: "fixed_camera_locomotion_reset",
      description:
        "Stop any active fixed-camera transition and clear transient redirect, stop-hold, and transition-lock state on the live PIE pawn.",
      inputSchema: empty,
      handler: async (params) => send(client, "reset", params),
    },
  ];
}