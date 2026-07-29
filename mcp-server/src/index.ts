#!/usr/bin/env node

/**
 * Unreal MCP Bridge - MCP Server Entry Point
 * 
 * Registers all tools and starts the MCP server over stdio.
 * Communicates with the UE4 Python listener over HTTP on localhost:8080.
 */

import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";
import { zodToJsonSchema } from "zod-to-json-schema";

import { UnrealClient } from "./unreal-client.js";
import { OperationHistory } from "./history.js";
import { createSystemTools } from "./tools/system.js";
import { createProjectTools } from "./tools/project.js";
import { createActorTools } from "./tools/actors.js";
import { createLevelTools } from "./tools/level.js";
import { createViewportTools } from "./tools/viewport.js";
import { createMaterialTools } from "./tools/materials.js";
import { createBlueprintTools } from "./tools/blueprints.js";
import { createOperationsTools } from "./tools/operations.js";
import { createPromptBrushTools } from "./tools/promptbrush.js";
import { createGameplayTools } from "./tools/gameplay.js";
import { createEffectsTools } from "./tools/effects.js";
import { createIntelligenceTools } from "./tools/intelligence.js";
import { createTitleTools } from "./tools/titles.js";
import { createBlueprintGraphTools } from "./tools/blueprint-graph.js";
import { createCppTools } from "./tools/cpp.js";
import { createGamedevTools } from "./tools/gamedev.js";
import { createContentTools } from "./tools/content.js";
import { createAnimationTools } from "./tools/animation.js";
// NOTE: tools/inspection.ts, tools/locomotion.ts and tools/fixed-camera-locomotion.ts
// are present in the tree but deliberately NOT registered. They are TypeScript-only
// work in progress: no matching handlers exist in the Python listener, so registering
// them would advertise tools that can never succeed. Write the handlers in
// Plugins/MCPBridge/Content/Python/mcp_bridge/handlers/ and add routes to router.py,
// then import and register them here.
import { createPieAgentTools } from "./tools/pie-agent.js";
import { createClothTools } from "./tools/cloth.js";
import { createEngineSourceTools } from "./tools/engine-source.js";
import { toolAnnotations } from "./annotations.js";
import { loadExtensions } from "./extensions.js";
import type { ToolDefinition } from "./types.js";

async function main(): Promise<void> {
  const client = new UnrealClient();
  const history = new OperationHistory();

  const server = new Server(
    { name: "unreal-bridge", version: "1.0.0" },
    {
      capabilities: { tools: {} },
      instructions: [
        "Before executing any structural engine or blueprint generation task",
        "(gameplay systems, blueprint/widget generation, editor C++, UI",
        "materials), read the contents of docs/playbooks/ in the project",
        "repository. Playbooks are verified recipes for pre-solved UE4.27",
        "architecture patterns: design intent, dependencies, graph logic,",
        "replication steps, and engine-specific gotchas. Follow an existing",
        "playbook instead of re-deriving a solution, and when you materially",
        "change a documented system, update its playbook as part of the work.",
        "If docs/playbooks/ does not exist in this project, proceed normally.",
      ].join(" "),
    }
  );

  // Collect all tool definitions
  const allTools: ToolDefinition[] = [
    ...createSystemTools(client),
    ...createProjectTools(client),
    ...createActorTools(client),
    ...createLevelTools(client),
    ...createViewportTools(client),
    ...createMaterialTools(client),
    ...createBlueprintTools(client),
    ...createOperationsTools(client, history),
    ...createPromptBrushTools(client),
    ...createGameplayTools(client),
    ...createEffectsTools(client),
    ...createIntelligenceTools(client),
    ...createTitleTools(client),
    ...createBlueprintGraphTools(client),
    ...createCppTools(client),
    ...createGamedevTools(client),
    ...createContentTools(client),
    ...createAnimationTools(client),
    ...createPieAgentTools(client),
    ...createClothTools(client),
    // Server-local tools: no HTTP to the listener, work with the editor closed.
    ...createEngineSourceTools(),
  ];

  // Project-local extensions. Namespaced, so they cannot shadow a core tool,
  // and they carry their own annotations rather than relying on the central map.
  const extensions = loadExtensions(client);
  for (const ext of extensions) {
    const clash = ext.tools.filter((t) => allTools.some((c) => c.name === t.name));
    if (clash.length > 0) {
      console.error(
        `[Unreal MCP Bridge] extension "${ext.manifest.name}" skipped: its tools collide with core tools (${clash
          .map((t) => t.name)
          .join(", ")}). Change its toolPrefix.`
      );
      continue;
    }
    allTools.push(...ext.tools);
    console.error(
      `[Unreal MCP Bridge] extension "${ext.manifest.name}" v${ext.manifest.version}: ${ext.tools.length} tool(s) from ${ext.manifestPath}`
    );
  }

  // Build a lookup map
  const toolMap = new Map<string, ToolDefinition>();
  for (const tool of allTools) {
    toolMap.set(tool.name, tool);
  }

  // Modifying commands that get recorded in history
  const modifyingCommands = new Set([
    "actor_spawn", "actor_modify", "actor_delete", "actor_duplicate",
    "actor_organize", "actor_snap_to_socket", "batch_spawn",
    "material_create", "material_apply",
    "asset_save_many", "project_enable_plugins",
    "blueprint_create", "blueprint_compile", "blueprint_build_from_json", "blueprint_build_from_description",
    "anim_blueprint_build_from_json",
    "widget_build_from_json",
    "widget_title_card_create", "widget_lower_third_create",
    "title_manifest_create", "title_manifest_from_reference",
    "title_widget_build_from_manifest", "title_controller_create",
    "title_sequence_bind", "title_manifest_adjust",
    "prompt_generate",
    "level_save",
    "pp_volume_spawn", "pp_volume_modify", "pp_preset",
    "camera_shake_blueprint", "camera_shake_spawn", "camera_shake_trigger",
    "blueprint_add_variable", "blueprint_remove_variable", "blueprint_set_variable_default",
    "blueprint_add_function", "blueprint_remove_function",
    "blueprint_add_event_dispatcher", "blueprint_remove_event_dispatcher",
    "blueprint_add_interface", "blueprint_remove_interface",
    "blueprint_component_remove", "blueprint_component_rename",
    "blueprint_node_add", "blueprint_node_delete", "blueprint_node_move",
    "blueprint_node_set_enabled", "blueprint_pins_connect", "blueprint_pins_break",
    "anim_reanchor", "anim_batch_reanchor",
    "cpp_class_create", "cpp_build",
    "input_mapping_add", "input_mapping_remove", "input_preset_apply",
    "gameplay_framework_create", "project_settings_maps", "camera_rig_create",
    "blackboard_create", "behavior_tree_create", "ai_nav_rebuild",
    "material_instance_create", "material_instance_set_params",
    "data_table_create", "data_table_fill_from_json",
    "audio_component_add", "level_new", "game_template_create",
    "cloth_apply_fabric_profile",
    "cloth_smooth_max_distance",
    "cloth_apply_lower_leg_gradient",
  ]);

  // Every tool must carry annotations (per-tool field or the central map)
  // so clients can distinguish read-only from destructive calls.
  const unannotated = allTools
    .filter((t) => !t.annotations && !toolAnnotations[t.name])
    .map((t) => t.name);
  if (unannotated.length > 0) {
    console.error(
      `[Unreal MCP Bridge] WARNING: tools missing annotations (add them to src/annotations.ts): ${unannotated.join(", ")}`
    );
  }

  // Handle tools/list
  server.setRequestHandler(ListToolsRequestSchema, async () => {
    return {
      tools: allTools.map((tool) => ({
        name: tool.name,
        description: tool.description,
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        inputSchema: zodToJsonSchema(tool.inputSchema as any) as Record<string, unknown>,
        annotations: tool.annotations ?? toolAnnotations[tool.name],
      })),
    };
  });

  // Handle tools/call
  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: params } = request.params;
    const tool = toolMap.get(name);

    if (!tool) {
      return {
        content: [{ type: "text" as const, text: JSON.stringify({
          success: false,
          error: `Unknown tool: ${name}`,
        }, null, 2) }],
      };
    }

    try {
      if (modifyingCommands.has(name)) {
        history.record(name, params || {}, tool.description);
      }
      return await tool.handler(params || {});
    } catch (error) {
      const errorMessage = error instanceof Error ? error.message : String(error);
      return {
        content: [{ type: "text" as const, text: JSON.stringify({
          success: false,
          data: {},
          error: `Tool error: ${errorMessage}`,
        }, null, 2) }],
      };
    }
  });

  // Start the server
  const transport = new StdioServerTransport();
  await server.connect(transport);

  console.error(`[Unreal MCP Bridge] Server started with ${allTools.length} tools`);
  console.error(`[Unreal MCP Bridge] Listener endpoint: ${client.endpoint}`);
  console.error(`[Unreal MCP Bridge] Tools: ${allTools.map((t) => t.name).join(", ")}`);
}

main().catch((error) => {
  console.error("[Unreal MCP Bridge] Fatal error:", error);
  process.exit(1);
});
