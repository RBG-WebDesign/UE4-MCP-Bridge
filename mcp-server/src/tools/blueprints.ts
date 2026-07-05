/**
 * Blueprint tools: blueprint_list, blueprint_info, blueprint_create, blueprint_compile, blueprint_document.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";
import { resolveSteps, listPatterns, type Step } from "../patterns/index.js";
import "../patterns/common.js"; // side-effect import: registers all patterns

export function createBlueprintTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "blueprint_list",
      description: "List Blueprint assets with optional filtering by path, name, and parent class.",
      inputSchema: z.object({
        path_filter: z
          .string()
          .optional()
          .describe("Content path prefix (default /Game/)"),
        name_filter: z
          .string()
          .optional()
          .describe("Filter by name (supports fnmatch wildcards)"),
        parent_class_filter: z
          .string()
          .optional()
          .describe("Filter by parent class name (e.g., Actor, Pawn, Character)"),
        limit: z
          .number()
          .int()
          .min(1)
          .max(2000)
          .default(200)
          .describe("Max results to return"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_list", params);
        return {
          content: [
            { type: "text" as const, text: JSON.stringify(result, null, 2) },
          ],
        };
      },
    },
    {
      name: "blueprint_info",
      description:
        "Get detailed Blueprint structure: components, variables, functions, event graphs, parent chain.",
      inputSchema: z.object({
        blueprint_path: z
          .string()
          .startsWith("/")
          .describe("Asset path of the Blueprint"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_info", params);
        return {
          content: [
            { type: "text" as const, text: JSON.stringify(result, null, 2) },
          ],
        };
      },
    },
    {
      name: "blueprint_create",
      description:
        "Create a new Blueprint class with optional components and variables.",
      inputSchema: z.object({
        name: z.string().min(1).describe("Blueprint name"),
        path: z
          .string()
          .startsWith("/")
          .default("/Game/Blueprints")
          .describe("Content directory to create in"),
        parent_class: z
          .string()
          .min(1)
          .default("Actor")
          .describe(
            "Parent class name (Actor, Pawn, Character, PlayerController, GameModeBase, ActorComponent, SceneComponent)"
          ),
        components: z
          .array(
            z.object({
              name: z.string().min(1).describe("Component name"),
              class: z
                .string()
                .min(1)
                .describe("Component class (e.g., BoxComponent, StaticMeshComponent)"),
              attach_to: z
                .string()
                .optional()
                .describe("Parent component to attach to"),
            })
          )
          .optional()
          .describe("Components to add to the Blueprint"),
        variables: z
          .array(
            z.object({
              name: z.string().min(1).describe("Variable name"),
              type: z
                .string()
                .min(1)
                .describe("Variable type (Boolean, Integer, Float, String, Vector, Rotator, Transform, Actor)"),
              default_value: z.any().optional().describe("Default value"),
              category: z.string().optional().describe("Variable category for grouping"),
              editable: z
                .boolean()
                .default(true)
                .describe("Whether editable in details panel"),
              tooltip: z.string().optional().describe("Tooltip text"),
            })
          )
          .optional()
          .describe("Variables to add (may be skipped in 4.27 if API is limited)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_create", params);
        return {
          content: [
            { type: "text" as const, text: JSON.stringify(result, null, 2) },
          ],
        };
      },
    },
    {
      name: "blueprint_compile",
      description:
        "Compile a Blueprint and return success/failure with error details. Success means the compile ran, check 'compiled' and 'had_errors' for actual result.",
      inputSchema: z.object({
        blueprint_path: z
          .string()
          .startsWith("/")
          .describe("Asset path of the Blueprint to compile"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_compile", params);
        return {
          content: [
            { type: "text" as const, text: JSON.stringify(result, null, 2) },
          ],
        };
      },
    },
    {
      name: "blueprint_document",
      description:
        "Generate a human-readable text summary of a Blueprint's structure, variables, and components.",
      inputSchema: z.object({
        blueprint_path: z
          .string()
          .startsWith("/")
          .describe("Asset path of the Blueprint"),
        detail_level: z
          .enum(["minimal", "standard", "detailed"])
          .default("standard")
          .describe("Level of detail: minimal, standard, or detailed"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_document", params);
        return {
          content: [
            { type: "text" as const, text: JSON.stringify(result, null, 2) },
          ],
        };
      },
    },
    {
      name: "blueprint_build_from_json",
      description:
        "Builds a Blueprint event graph from a JSON node/connection description. " +
        "Supported node types: BeginPlay, PrintString, Delay, CallFunction. " +
        "Node objects accept an optional 'params' key to set pin default values by PinName. " +
        "Connections use 'nodeId.exec' format for exec pin wiring.",
      inputSchema: z.object({
        blueprint_path: z
          .string()
          .startsWith("/")
          .describe("Content path of the Blueprint, e.g. /Game/BP_TestGraph.BP_TestGraph"),
        graph: z.object({
          nodes: z.array(
            z.object({
              id: z.string().describe("Unique node identifier"),
              type: z.string().describe("Node type: BeginPlay | PrintString | Delay | CallFunction"),
              function: z.string().optional().describe("Required when type is CallFunction -- function name on UKismetSystemLibrary"),
              params: z.record(z.union([z.string(), z.number(), z.boolean()]))
                .optional()
                .describe("Pin default values keyed by PinName. Supports string, number, bool only."),
            })
          ),
          connections: z.array(
            z.object({
              from: z.string().describe("Source node and pin role: nodeId.exec"),
              to: z.string().describe("Target node and pin role: nodeId.exec"),
            })
          ),
        }),
        clear_existing: z.boolean().default(true).describe("Clear existing graph before building"),
      }),
      handler: async (params) => {
        const { blueprint_path, graph, clear_existing } = params as {
          blueprint_path: string;
          graph: { nodes: Array<{ id: string; type: string; function?: string; params?: Record<string, string | number | boolean> }>; connections: Array<{ from: string; to: string }> };
          clear_existing: boolean;
        };

        const result = await client.sendCommand("blueprint_build_from_json", {
          blueprint_path,
          graph_json: graph,
          clear_existing,
        });
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "anim_blueprint_build_from_json",
      description:
        "Build an Animation Blueprint from a JSON specification. " +
        "Creates the AnimBP asset with the given skeleton and populates the anim graph " +
        "from the JSON spec (state machines, blend spaces, slots, etc.).",
      inputSchema: z.object({
        package_path: z
          .string()
          .startsWith("/")
          .describe("Content directory path, e.g. /Game/AnimBP"),
        asset_name: z
          .string()
          .min(1)
          .describe("Animation Blueprint asset name, e.g. ABP_Enemy"),
        skeleton_path: z
          .string()
          .startsWith("/")
          .describe("Full path to Skeleton asset, e.g. /Game/Mannequin/Mesh/UE4_Mannequin_Skeleton"),
        json_spec: z
          .string()
          .min(1)
          .describe("JSON specification string for the AnimBP graph"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("anim_blueprint_build_from_json", params);
        return {
          content: [
            { type: "text" as const, text: JSON.stringify(result, null, 2) },
          ],
        };
      },
    },
    {
      name: "blueprint_build_from_description",
      description:
        "Build a Blueprint event graph from high-level pattern templates. " +
        "Each step references a named pattern with optional parameters. " +
        "Exec flow is auto-wired between steps. " +
        `Available patterns: ${listPatterns().join(", ")}`,
      inputSchema: z.object({
        blueprint_path: z
          .string()
          .startsWith("/")
          .describe(
            "Content path of the Blueprint, e.g. /Game/BP_TestGraph.BP_TestGraph"
          ),
        steps: z
          .array(
            z.object({
              pattern: z
                .string()
                .describe("Pattern name (e.g., 'on_begin_play', 'print_string')"),
              params: z
                .record(z.union([z.string(), z.number(), z.boolean()]))
                .optional()
                .describe("Pattern-specific parameters"),
            })
          )
          .describe("Ordered list of pattern steps"),
        clear_existing: z
          .boolean()
          .default(true)
          .describe("Clear existing graph before building"),
      }),
      handler: async (params) => {
        const {
          blueprint_path,
          steps,
          clear_existing,
        } = params as {
          blueprint_path: string;
          steps: Step[];
          clear_existing: boolean;
        };

        // Resolve patterns into a single graph JSON
        let graph;
        try {
          graph = resolveSteps(steps);
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err);
          return {
            content: [
              {
                type: "text" as const,
                text: JSON.stringify({ success: false, error: msg }, null, 2),
              },
            ],
          };
        }

        const result = await client.sendCommand("blueprint_build_from_json", {
          blueprint_path,
          graph_json: graph,
          clear_existing,
        });
        return {
          content: [
            {
              type: "text" as const,
              text: JSON.stringify(
                {
                  ...((result as unknown as Record<string, unknown>) ?? {}),
                  generated_graph: graph,
                },
                null,
                2
              ),
            },
          ],
        };
      },
    },
  ];
}
