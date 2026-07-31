/**
 * Material tools: material_list, material_info, material_create, material_apply.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";
const SimpleSurfaceSchema = z.object({
  base_color: z.tuple([z.number().min(0).max(1), z.number().min(0).max(1), z.number().min(0).max(1)]),
  roughness: z.number().min(0).max(1).optional(),
  metallic: z.number().min(0).max(1).optional(),
  emissive_strength: z.number().min(0).max(10).optional(),
}).strict();

export function createMaterialTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "material_list",
      description:
        "List materials in the project with optional filtering by path, name, and type.",
      inputSchema: z.object({
        path_filter: z
          .string()
          .optional()
          .describe("Content path prefix filter (default /Game/)"),
        name_filter: z
          .string()
          .optional()
          .describe("Filter by material name (supports fnmatch wildcards)"),
        type_filter: z
          .enum(["material", "instance", "all"])
          .default("all")
          .describe("Filter by material type"),
        limit: z
          .number()
          .int()
          .min(1)
          .max(2000)
          .default(200)
          .describe("Max results to return"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("material_list", params);
        return {
          content: [
            { type: "text" as const, text: JSON.stringify(result, null, 2) },
          ],
        };
      },
    },
    {
      name: "material_info",
      description:
        "Get detailed information about a material: parameters, textures, parent chain.",
      inputSchema: z.object({
        material_path: z
          .string()
          .startsWith("/")
          .describe("Asset path of the material"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("material_info", params);
        return {
          content: [
            { type: "text" as const, text: JSON.stringify(result, null, 2) },
          ],
        };
      },
    },
    {
      name: "material_create",
      description:
        "Create a new material, a simple opaque surface, or a material instance. Provide parent and type 'instance' for instances.",
      inputSchema: z
        .object({
          name: z.string().min(1).describe("Material name"),
          path: z
            .string()
            .startsWith("/")
            .default("/Game/Materials")
            .describe("Content directory to create in"),
          type: z
            .enum(["material", "instance"])
            .describe("Material type to create"),
          parent: z
            .string()
            .startsWith("/")
            .optional()
            .describe("Parent material path (required for instances)"),
          parameters: z
            .object({
              scalar: z
                .record(z.string(), z.number())
                .optional()
                .describe("Scalar parameter values"),
              vector: z
                .record(
                  z.string(),
                  z
                    .tuple([z.number(), z.number(), z.number(), z.number()])
                )
                .optional()
                .describe("Vector parameter values as [r, g, b, a]"),
              texture: z
                .record(z.string(), z.string())
                .optional()
                .describe("Texture parameter values as asset paths"),
            })
            .optional()
            .describe("Initial parameter values (instances only)"),
          simple: SimpleSurfaceSchema.optional().describe("Opaque base-material graph: base color, roughness, metallic, and optional emissive strength"),
        })
        .refine(
          (data) => data.type !== "instance" || data.parent !== undefined,
          { message: "parent is required when type is 'instance'" }
        )
        .refine(
          (data) => data.type === "material" || data.simple === undefined,
          { message: "simple is only valid when type is 'material'" }
        ),
      handler: async (params) => {
        const result = await client.sendCommand("material_create", params);
        return {
          content: [
            { type: "text" as const, text: JSON.stringify(result, null, 2) },
          ],
        };
      },
    },
    {
      name: "material_apply",
      description:
        "Apply a material to an actor's mesh component. Returns validation by default.",
      inputSchema: z.object({
        actor_name: z.string().min(1).describe("Label of the target actor"),
        material_path: z
          .string()
          .startsWith("/")
          .describe("Asset path of the material to apply"),
        slot_index: z
          .number()
          .int()
          .min(0)
          .optional()
          .describe("Material slot index (default 0)"),
        slot_name: z
          .string()
          .optional()
          .describe("Material slot name (overrides slot_index if both provided)"),
        component_name: z
          .string()
          .optional()
          .describe("Specific mesh component name (default: first mesh found)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("material_apply", params);
        return {
          content: [
            { type: "text" as const, text: JSON.stringify(result, null, 2) },
          ],
        };
      },
    },
  ];
}
