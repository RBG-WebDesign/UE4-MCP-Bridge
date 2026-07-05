/**
 * System tools: test_connection, python_proxy, restart_listener, ue_logs, help.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const TOOL_DOCS: Record<string, { description: string; params: string }> = {
  test_connection: { description: "Ping the Python listener and return connection status", params: "none" },
  python_proxy: { description: "Execute arbitrary Python code inside UE4 editor", params: "code (string)" },
  project_info: { description: "Return current project name, engine version, paths, loaded level", params: "none" },
  asset_list: { description: "List assets with optional path/type/name filters", params: "path?, asset_type?, name_pattern?, recursive?" },
  asset_info: { description: "Return detailed info for a single asset", params: "path (string)" },
  asset_load_diagnostics: { description: "Diagnose asset loads, Blueprint generated classes, dependencies, referencers, and script classes", params: "asset_paths?, class_paths?, include_dependencies?, include_referencers?" },
  asset_save_many: { description: "Save only explicit asset paths", params: "paths" },
  project_enable_plugins: { description: "Enable plugins in the current .uproject", params: "plugins" },
  input_mapping_info: { description: "Inspect action and axis input mappings, useful for validating bindings without physical key injection", params: "action_name?, axis_name?, key?" },
  project_index_rebuild: { description: "Rebuild the lightweight project intelligence index under Saved/MCP/index", params: "categories?, path_prefix?, limit?, blueprint_detail_limit?, material_detail_limit?, actor_limit?" },
  project_index_query: { description: "Search cached indexed assets, Blueprint structures, dependencies, and gameplay patterns", params: "query?, category?, path_prefix?, limit?, include_raw?" },
  project_semantic_diff: { description: "Compare indexed before/after snapshots or cached index data against a fresh read", params: "before_snapshot?, after_snapshot?, asset_paths?, categories?, path_prefix?, limit?" },
  gameplay_pattern_search: { description: "Find reusable gameplay patterns in indexed Blueprints, widgets, and level actors", params: "pattern?, query?, limit?, fresh?, path_prefix?" },
  actor_spawn: { description: "Spawn an actor from an asset path", params: "asset_path, location, rotation?, scale?, name?, folder?" },
  actor_modify: { description: "Change actor location, rotation, scale, visibility, or mesh", params: "actor_name, location?, rotation?, scale?, visible?, mesh?" },
  actor_delete: { description: "Delete actors by name or pattern", params: "actor_name (supports * wildcard)" },
  actor_duplicate: { description: "Duplicate an actor with optional offset", params: "actor_name, offset?, new_name?" },
  actor_organize: { description: "Move actors into World Outliner folders", params: "actors (list), folder (string)" },
  batch_spawn: { description: "Spawn multiple actors in one call", params: "spawns (array of spawn definitions)" },
  level_actors: { description: "List all actors in current level", params: "class_filter?, folder_filter?" },
  level_save: { description: "Save current level and optionally all dirty assets", params: "save_all?" },
  level_outliner: { description: "Return World Outliner folder tree", params: "none" },
  viewport_screenshot: { description: "Capture viewport to image file", params: "filename?, resolution_x?, resolution_y?" },
  viewport_camera: { description: "Set viewport camera position/rotation", params: "location?, rotation?" },
  viewport_focus: { description: "Focus camera on named actor", params: "actor_name, distance?" },
  material_list: { description: "List materials with filters", params: "path?, name_pattern?" },
  material_create: { description: "Create material or material instance", params: "name, path?, parent?, scalar_params?, vector_params?" },
  material_apply: { description: "Apply material to actor mesh", params: "material_path, actor_name, slot_index?" },
  blueprint_create: { description: "Create Blueprint class", params: "name, path?, parent_class?" },
  blueprint_compile: { description: "Compile a Blueprint", params: "path (string)" },
  blueprint_build_from_json: {
    description: "Build a Blueprint event graph from a JSON node/connection description",
    params: "blueprint_path (string), graph (object with nodes[] and connections[]), clear_existing? (bool)"
  },
  bridge_clear_log: { description: "Reset the bridge output-log cursor marker", params: "none" },
  clear_output_log: { description: "Alias for bridge_clear_log", params: "none" },
  bridge_command_manifest: { description: "Return live listener command names", params: "none" },
  widget_build_from_json: { description: "Create a Widget Blueprint from JSON, including optional top-level animations", params: "package_path, asset_name, widget_json" },
  widget_title_template: { description: "Return a polished title or lower-third widget spec without creating an asset", params: "title?, subtitle?, eyebrow?, preset?, style?, fade_in?, hold?, fade_out?" },
  widget_title_card_create: { description: "Create a centered title-card Widget Blueprint with FadeIn/FadeOut animation specs", params: "package_path?, asset_name?, title?, subtitle?, eyebrow?, preset?, fade_in?, hold?, fade_out?" },
  widget_lower_third_create: { description: "Create a lower-third Widget Blueprint with FadeIn/FadeOut animation specs", params: "package_path?, asset_name?, title?, subtitle?, eyebrow?, preset?, fade_in?, hold?, fade_out?" },
  title_manifest_create: { description: "Create a JSON title manifest with normalized boxes, frame timing, and style metadata", params: "id?, resolution?, fps?, style?, gapFrames?, cues[], output_path?" },
  title_manifest_validate: { description: "Validate a title manifest loaded from manifest, manifest_path, or id", params: "manifest?, manifest_path?, id?" },
  title_manifest_from_reference: { description: "Convert measured reference-frame pixel boxes into normalized manifest cue boxes", params: "id?, frame_size?, resolution?, fps?, style?, cues[]" },
  title_widget_build_from_manifest: { description: "Build WBP_TitleRenderer from a manifest or return its widget JSON spec with dry_run=true", params: "manifest?, manifest_path?, id?, package_path?, asset_name?, dry_run?" },
  title_controller_create: { description: "Create a BP_TitleController spec for loading a manifest and updating title cues by frame", params: "manifest?, manifest_path?, id?, package_path?, asset_name?" },
  title_sequence_bind: { description: "Create a Level Sequence binding spec for driving the title controller from frame numbers", params: "manifest?, manifest_path?, id?, sequence_path?, controller_path?" },
  title_render_compare: { description: "Compare a rendered title frame against a reference image and report pixel error", params: "reference_path, render_path, region?" },
  title_manifest_adjust: { description: "Apply cue box, timing, and text corrections to a title manifest", params: "manifest?, manifest_path?, id?, adjustments[], output_path?" },
  undo: { description: "Undo last N operations", params: "count?" },
  redo: { description: "Redo undone operations", params: "count?" },
  help: { description: "Show tool documentation", params: "tool_name?" },
};

export function createSystemTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "test_connection",
      description: "Ping the UE4 Python listener. Returns connection status, engine version, and project info.",
      inputSchema: z.object({}),
      handler: async () => {
        const result = await client.sendCommand("test_connection");
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "python_proxy",
      description: "Execute arbitrary Python code inside the UE4 editor. Has full access to the unreal module. Use for prototyping or operations not covered by dedicated tools.",
      inputSchema: z.object({
        code: z.string().describe("Python code to execute in the UE4 editor"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("python_proxy", { code: params.code });
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "restart_listener",
      description: "Restart the Python listener without restarting Unreal. Use after modifying listener code.",
      inputSchema: z.object({
        host: z.string().optional().describe("Hostname (default: localhost)"),
        port: z.number().optional().describe("Port (default: 8080)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("restart_listener", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "ue_logs",
      description: "Fetch recent UE4 log entries.",
      inputSchema: z.object({
        category: z.string().optional().describe("Log category filter"),
        severity: z.string().optional().describe("Severity filter"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("ue_logs", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "bridge_clear_log",
      description: "Reset the MCP output-log cursor marker. Does not delete or truncate the UE log.",
      inputSchema: z.object({}),
      handler: async () => {
        const result = await client.sendCommand("bridge_clear_log");
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "clear_output_log",
      description: "Alias for bridge_clear_log.",
      inputSchema: z.object({}),
      handler: async () => {
        const result = await client.sendCommand("clear_output_log");
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "bridge_command_manifest",
      description: "Return command names currently registered in the live Unreal listener.",
      inputSchema: z.object({}),
      handler: async () => {
        const result = await client.sendCommand("bridge_command_manifest");
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "help",
      description: "Show documentation for available tools.",
      inputSchema: z.object({
        tool_name: z.string().optional().describe("Specific tool to get help for. Omit for all tools."),
      }),
      handler: async (params) => {
        const toolName = params.tool_name as string | undefined;
        if (toolName) {
          const doc = TOOL_DOCS[toolName];
          if (!doc) {
            return {
              content: [{ type: "text" as const, text: JSON.stringify({
                success: false, error: `Unknown tool: ${toolName}. Available: ${Object.keys(TOOL_DOCS).join(", ")}`,
              }, null, 2) }],
            };
          }
          return {
            content: [{ type: "text" as const, text: JSON.stringify({
              success: true, data: { tool: toolName, ...doc },
            }, null, 2) }],
          };
        }
        return {
          content: [{ type: "text" as const, text: JSON.stringify({
            success: true, data: { tools: TOOL_DOCS },
          }, null, 2) }],
        };
      },
    },
  ];
}
