/**
 * Project and asset tools: project_info, asset_list, asset_info, input_mapping_info.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

export function createProjectTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "project_info",
      description: "Return current UE project name, engine version, project path, content directory, and loaded level.",
      inputSchema: z.object({}),
      handler: async () => {
        const result = await client.sendCommand("project_info");
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "asset_list",
      description: "List assets with optional filters. Returns asset paths, types, and names.",
      inputSchema: z.object({
        path: z.string().optional().describe("Path prefix to search under (default: /Game/)"),
        asset_type: z.string().optional().describe("Filter by asset type class name"),
        name_pattern: z.string().optional().describe("Filter by name substring"),
        recursive: z.boolean().optional().describe("Search recursively (default: true)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("asset_list", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "folder_hide",
      description:
        "Hide one or more /Game subfolders in the Content Browser (display-only: assets stay on disk, " +
        "referenced, and cookable). Persists across editor restarts via Config/FolderVisibility.ini. " +
        "Refuses to hide /Game itself.",
      inputSchema: z.object({
        folder: z.string().optional().describe("Single folder path, e.g. /Game/HorrorEngine"),
        folders: z.array(z.string()).optional().describe("Multiple folder paths to hide at once"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("folder_hide", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "folder_show",
      description:
        "Unhide Content Browser folders hidden by folder_hide. Call with no arguments to unhide " +
        "EVERYTHING (escape hatch).",
      inputSchema: z.object({
        folder: z.string().optional().describe("Single folder path to unhide"),
        folders: z.array(z.string()).optional().describe("Multiple folder paths to unhide"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("folder_show", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "folder_hidden_list",
      description: "List Content Browser folders currently hidden by folder_hide.",
      inputSchema: z.object({}),
      handler: async (params) => {
        const result = await client.sendCommand("folder_hidden_list", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "asset_info",
      description: "Return detailed info for a single asset: type, bounds, material slots, LOD count.",
      inputSchema: z.object({
        path: z.string().describe("Asset path (e.g., /Game/Meshes/SM_Cube)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("asset_info", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "asset_load_diagnostics",
      description: "Diagnose asset loads, Blueprint generated classes, dependencies, referencers, and script class availability.",
      inputSchema: z.object({
        asset_paths: z.union([z.string(), z.array(z.string())]).optional().describe("Asset paths to inspect"),
        class_paths: z.union([z.string(), z.array(z.string())]).optional().describe("Class paths to load, such as /Script/Synthesis.AudioImpulseResponse"),
        include_dependencies: z.boolean().optional().describe("Include asset registry dependencies (default true)"),
        include_referencers: z.boolean().optional().describe("Include asset registry referencers (default true)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("asset_load_diagnostics", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "asset_save_many",
      description: "Save only the explicit asset paths passed in. Safer than project-wide save-all.",
      inputSchema: z.object({
        paths: z.union([z.string(), z.array(z.string())]).describe("Asset paths to save"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("asset_save_many", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "project_enable_plugins",
      description: "Enable one or more plugins in the current .uproject and report whether an editor restart is required.",
      inputSchema: z.object({
        plugins: z.union([z.string(), z.array(z.string())]).describe("Plugin names to enable"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("project_enable_plugins", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "input_mapping_info",
      description: "Inspect Unreal input action and axis mappings. Use this to validate bindings like PF_Pause to Escape when physical key injection is unavailable.",
      inputSchema: z.object({
        action_name: z.string().optional().describe("Exact action name to filter, such as PF_Pause"),
        axis_name: z.string().optional().describe("Exact axis name to filter"),
        key: z.string().optional().describe("Exact key name to filter, such as Escape"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("input_mapping_info", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
  ];
}
