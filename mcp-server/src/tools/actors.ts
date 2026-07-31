/**
 * Actor tools: spawn, modify, delete, duplicate, organize, snap, batch, validate.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const VectorSchema = z.object({
  x: z.number().describe("X coordinate"),
  y: z.number().describe("Y coordinate"),
  z: z.number().describe("Z coordinate"),
});

const RotationSchema = z.object({
  pitch: z.number().describe("Pitch in degrees"),
  yaw: z.number().describe("Yaw in degrees"),
  roll: z.number().describe("Roll in degrees"),
});

export function createActorTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "actor_selection",
      description: "Return the currently selected level actors and their world transforms.",
      inputSchema: z.object({}),
      handler: async () => {
        const result = await client.sendCommand("actor_selection", {});
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "actor_spawn",
      description: "Spawn an actor from an asset path at a given location/rotation/scale. Returns validation results by default.",
      inputSchema: z.object({
        asset_path: z.string().describe("Asset path (e.g., /Game/Meshes/SM_Cube)"),
        location: VectorSchema.optional().describe("World location"),
        rotation: RotationSchema.optional().describe("Rotation"),
        scale: VectorSchema.optional().describe("Scale (default 1,1,1)"),
        name: z.string().optional().describe("Actor label"),
        folder: z.string().optional().describe("World Outliner folder"),
        validate: z.boolean().optional().describe("Validate resulting transform (default true)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("actor_spawn", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "actor_duplicate",
      description: "Duplicate an existing actor with an optional offset. Returns validation results by default.",
      inputSchema: z.object({
        actor_name: z.string().describe("Label of actor to duplicate"),
        offset: VectorSchema.optional().describe("Offset from original position"),
        new_name: z.string().optional().describe("Label for the duplicate"),
        validate: z.boolean().optional().describe("Validate resulting transform (default true)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("actor_duplicate", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "actor_delete",
      description: "Delete actors by name or name pattern (supports * and ? wildcards).",
      inputSchema: z.object({
        actor_name: z.string().describe("Exact name or pattern with * or ? wildcards"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("actor_delete", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "actor_modify",
      description: "Change an actor's location, rotation, scale, mesh, or visibility. Returns validation results by default.",
      inputSchema: z.object({
        actor_name: z.string().describe("Label of actor to modify"),
        location: VectorSchema.optional().describe("New location"),
        rotation: RotationSchema.optional().describe("New rotation"),
        scale: VectorSchema.optional().describe("New scale"),
        visible: z.boolean().optional().describe("Set visibility"),
        mesh: z.string().optional().describe("Asset path for new mesh"),
        validate: z.boolean().optional().describe("Validate resulting transform (default true)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("actor_modify", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "actor_organize",
      description: "Move actors into World Outliner folders.",
      inputSchema: z.object({
        actors: z.array(z.string()).describe("Actor labels to move"),
        folder: z.string().describe("Target folder path"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("actor_organize", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "actor_snap_to_socket",
      description: "Snap one actor to another actor's named socket.",
      inputSchema: z.object({
        actor_name: z.string().describe("Actor to move"),
        target_actor: z.string().describe("Actor with the socket"),
        socket_name: z.string().describe("Socket name"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("actor_snap_to_socket", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "batch_spawn",
      description: "Spawn multiple actors in one call. Each spawn definition uses the same format as actor_spawn.",
      inputSchema: z.object({
        spawns: z.array(z.object({
          asset_path: z.string(),
          location: VectorSchema.optional(),
          rotation: RotationSchema.optional(),
          scale: VectorSchema.optional(),
          name: z.string().optional(),
          folder: z.string().optional(),
          validate: z.boolean().optional(),
        })).describe("Array of spawn definitions"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("batch_spawn", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "placement_validate",
      description: "Check actors for gaps, overlaps, and alignment issues.",
      inputSchema: z.object({
        actors: z.array(z.string()).describe("Actor labels to validate"),
        check_gaps: z.boolean().optional().describe("Check for gaps (default true)"),
        check_overlaps: z.boolean().optional().describe("Check for overlaps (default true)"),
        gap_threshold: z.number().optional().describe("Max gap in units (default 1.0)"),
        overlap_threshold: z.number().optional().describe("Min distance for overlap (default 1.0)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("placement_validate", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
  ];
}
