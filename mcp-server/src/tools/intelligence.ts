/**
 * Project intelligence tools: index rebuild/query, semantic diff, pattern search.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const categorySchema = z.enum(["assets", "blueprints", "materials", "widgets", "level", "design_memory"]);

const snapshotSchema: z.ZodType<Record<string, unknown>> = z.record(z.unknown());

export function createIntelligenceTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "project_index_rebuild",
      description:
        "Rebuild the lightweight project intelligence index under Saved/MCP/index. " +
        "Scans assets, Blueprints, materials, widgets, level actors, and design memory using existing read-only bridge handlers.",
      inputSchema: z.object({
        categories: z.union([categorySchema, z.array(categorySchema)]).optional()
          .describe("Optional category or categories to rebuild. Defaults to all categories."),
        path_prefix: z.string().optional().describe("Content path prefix to scan, default /Game/."),
        limit: z.number().int().positive().optional()
          .describe("General per-category detail cap for Blueprint/material scans."),
        blueprint_detail_limit: z.number().int().positive().optional()
          .describe("Max Blueprint assets to inspect in detail."),
        material_detail_limit: z.number().int().positive().optional()
          .describe("Max material assets to inspect in detail."),
        actor_limit: z.number().int().positive().optional()
          .describe("Max level actors to include in the level index."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("project_index_rebuild", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "project_index_query",
      description:
        "Search the cached project intelligence index. Use this before editing to find existing assets, Blueprint structures, dependencies, and patterns.",
      inputSchema: z.object({
        query: z.string().optional().describe("Whitespace-separated search terms. All terms must match."),
        category: categorySchema.optional().describe("Optional category to search."),
        path_prefix: z.string().optional().describe("Optional asset path prefix filter."),
        limit: z.number().int().positive().max(500).optional().describe("Max results, default 50."),
        include_raw: z.boolean().optional().describe("Include full indexed records in results."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("project_index_query", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "project_semantic_diff",
      description:
        "Compare indexed before/after snapshots for semantic changes in Blueprints, widgets, materials, level actors, and dependencies. " +
        "If snapshots are omitted, compares cached index data against a fresh read.",
      inputSchema: z.object({
        before_snapshot: snapshotSchema.optional().describe("Optional explicit before snapshot."),
        after_snapshot: snapshotSchema.optional().describe("Optional explicit after snapshot."),
        asset_paths: z.union([z.string(), z.array(z.string())]).optional()
          .describe("Optional asset paths, actor names, or record names to narrow the diff."),
        categories: z.union([categorySchema, z.array(categorySchema)]).optional()
          .describe("Optional category or categories to compare."),
        path_prefix: z.string().optional().describe("Content path prefix to use when taking a fresh snapshot."),
        limit: z.number().int().positive().optional().describe("General fresh snapshot cap."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("project_semantic_diff", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "gameplay_pattern_search",
      description:
        "Find reusable gameplay patterns in indexed Blueprints, widgets, and level actors using deterministic heuristics. " +
        "Patterns include overlap logic, key-door flows, prompt widgets, objective chains, and interactables.",
      inputSchema: z.object({
        pattern: z.enum(["overlap_logic", "key_door", "prompt_widget", "objective_chain", "interactable"]).optional()
          .describe("Optional specific pattern to search for."),
        query: z.string().optional().describe("Optional extra text filter over matches."),
        limit: z.number().int().positive().max(500).optional().describe("Max matches, default 50."),
        fresh: z.boolean().optional().describe("Read fresh project data instead of using the cached index."),
        path_prefix: z.string().optional().describe("Content path prefix used only when fresh is true."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_pattern_search", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
  ];
}
