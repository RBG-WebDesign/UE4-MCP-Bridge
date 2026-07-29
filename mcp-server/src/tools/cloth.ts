/**
 * UE4.27 NvCloth inspection and optimization tools.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

export function createClothTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "cloth_inspect_asset",
      description:
        "Inspect a UE4.27 skeletal mesh and return render-section cloth bindings, physical cloth LOD counts, authoring masks, applied weight maps, NvCloth config, topology hash, and Physics Asset coverage.",
      inputSchema: z.object({
        skeletal_mesh: z
          .string()
          .describe("Skeletal mesh asset path, such as /Game/Characters/SK_Coat.SK_Coat"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("cloth_inspect_asset", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "cloth_apply_fabric_profile",
      description:
        "Apply a bounded UE4.27 NvCloth fabric profile to exactly one embedded clothing asset, with a JSON backup, transaction, and package save. This command does not change cloth paint masks or the Physics Asset.",
      inputSchema: z.object({
        skeletal_mesh: z.string().describe("Skeletal mesh asset path"),
        clothing_asset: z.string().describe("Exact embedded clothing asset name"),
        profile: z
          .enum(["heavy_denim", "heavy_denim_close_fit"])
          .describe("Fabric profile to apply"),
        confirm: z.literal(true).describe("Explicit confirmation immediately before Apply"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("cloth_apply_fabric_profile", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "cloth_smooth_max_distance",
      description:
        "Apply topology-aware, non-expanding smoothing to one embedded clothing asset's UE4.27 Max Distance mask, preserving fixed anchors and writing a full backup before saving.",
      inputSchema: z.object({
        skeletal_mesh: z.string().describe("Skeletal mesh asset path"),
        clothing_asset: z.string().describe("Exact embedded clothing asset name"),
        iterations: z.number().int().min(1).max(12).default(6),
        strength: z.number().min(0).max(1).default(0.6),
        confirm: z.literal(true).describe("Explicit confirmation immediately before Apply"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("cloth_smooth_max_distance", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "cloth_apply_lower_leg_gradient",
      description:
        "Author a smooth UE4.27 Max Distance gradient from the trouser hem to the knee on one clothing asset. This intentionally activates formerly fixed lower-leg vertices, writes a full backup, and preserves the fabric profile and Physics Asset.",
      inputSchema: z.object({
        skeletal_mesh: z.string().describe("Skeletal mesh asset path"),
        clothing_asset: z.string().describe("Exact embedded clothing asset name"),
        knee_height_fraction: z.number().min(0.35).max(0.65).default(0.48),
        confirm: z.literal(true).describe("Explicit approval to increase lower-leg cloth freedom"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("cloth_apply_lower_leg_gradient", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
  ];
}
