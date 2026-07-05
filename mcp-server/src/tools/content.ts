/**
 * Content tools: material instances, data tables, audio components, map
 * creation, and the game template generator (M13/M14).
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const scalarParams = z.record(z.number()).optional()
  .describe("Scalar parameter overrides: {ParamName: value}.");
const vectorParams = z.record(z.array(z.number()).min(3).max(4)).optional()
  .describe("Vector parameter overrides: {ParamName: [r, g, b, a?]}.");
const textureParams = z.record(z.string()).optional()
  .describe("Texture parameter overrides: {ParamName: texture asset path}.");

export function createContentTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "material_instance_create",
      description:
        "Create a MaterialInstanceConstant from a parent material with optional " +
        "scalar/vector/texture parameter overrides. Saves the instance.",
      inputSchema: z.object({
        path: z.string().describe("Content directory."),
        name: z.string().describe("Asset name, e.g. MI_Rock_Mossy."),
        parent_material: z.string().describe("Parent material asset path."),
        scalar_params: scalarParams,
        vector_params: vectorParams,
        texture_params: textureParams,
      }),
      handler: async (params) => {
        const result = await client.sendCommand("material_instance_create", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "material_instance_set_params",
      description: "Set scalar/vector/texture parameters on an existing material instance, then save.",
      inputSchema: z.object({
        material_instance: z.string().describe("MaterialInstanceConstant asset path."),
        scalar_params: scalarParams,
        vector_params: vectorParams,
        texture_params: textureParams,
      }),
      handler: async (params) => {
        const result = await client.sendCommand("material_instance_set_params", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "data_table_create",
      description:
        "Create a DataTable for a row struct, optionally filling rows from DataTable " +
        "JSON ([{\"Name\": \"Row1\", ...fields}]). Saves the table.",
      inputSchema: z.object({
        path: z.string().describe("Content directory."),
        name: z.string().describe("Asset name, e.g. DT_Items."),
        row_struct: z.string().describe("Row struct path: /Script/Module.Struct or /Game/... user struct."),
        rows_json: z.union([z.string(), z.array(z.record(z.unknown()))]).optional()
          .describe("Rows as DataTable JSON (array with a Name field per row)."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("data_table_create", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "data_table_fill_from_json",
      description: "Replace an existing DataTable's rows from DataTable JSON, then save. Returns the resulting row names.",
      inputSchema: z.object({
        data_table: z.string().describe("DataTable asset path."),
        rows_json: z.union([z.string(), z.array(z.record(z.unknown()))]),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("data_table_fill_from_json", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "audio_component_add",
      description:
        "Add an AudioComponent to a Blueprint, optionally assigning a SoundBase asset " +
        "and auto-activate. Compiles and saves.",
      inputSchema: z.object({
        blueprint_path: z.string(),
        component_name: z.string().optional().describe("Default 'Audio'."),
        sound: z.string().optional().describe("SoundBase asset path (SoundWave/SoundCue)."),
        attach_to: z.string().optional().describe("Parent component name."),
        auto_activate: z.boolean().optional().describe("Default true."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("audio_component_add", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "level_new",
      description:
        "Create and load a new level (optionally from a template map), then save it. " +
        "WARNING: switches the editor's loaded level; refuses if unsaved changes exist.",
      inputSchema: z.object({
        path: z.string().describe("New level path, e.g. /Game/Maps/MyMap."),
        template: z.string().optional().describe("Template map asset path to copy from."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("level_new", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "game_template_create",
      description:
        "Generate a playable game skeleton: gameplay framework (GameMode/Character/" +
        "PlayerController/HUD with class defaults wired), a camera rig matching the " +
        "template, an input preset, and optionally a new map and project default " +
        "game mode. Templates: first_person, third_person, top_down, side_scroller. " +
        "Returns a manifest of everything created.",
      inputSchema: z.object({
        path: z.string().describe("Content root for the game, e.g. /Game/MyGame."),
        base_name: z.string().describe("Base name for assets, e.g. 'MyGame'."),
        template: z.enum(["first_person", "third_person", "top_down", "side_scroller"]),
        create_map: z.boolean().optional()
          .describe("Also create <path>/Maps/<base_name>Map and load it. Default false."),
        set_as_default: z.boolean().optional()
          .describe("Write global default game mode (and default map if created) to project settings. Default false."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("game_template_create", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
  ];
}
