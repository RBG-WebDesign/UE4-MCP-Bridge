/**
 * PromptBrush tools: prompt_generate, prompt_status, prompt_spec_list,
 * widget_build_from_json.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const colorSchema = z.object({
  r: z.number(),
  g: z.number(),
  b: z.number(),
  a: z.number().optional(),
});

const titleBaseSchema = {
  package_path: z.string().startsWith("/").optional().describe("Content directory path, default /Game/UI/Titles"),
  asset_name: z.string().optional().describe("Widget Blueprint asset name"),
  title: z.string().optional().describe("Primary title text"),
  subtitle: z.string().optional().describe("Secondary line text"),
  eyebrow: z.string().optional().describe("Small label above the title"),
  preset: z.enum(["cinematic", "broadcast", "horror", "clean"]).optional().describe("Visual preset"),
  fade_in: z.number().positive().optional().describe("Fade-in duration in seconds"),
  hold: z.number().min(0).optional().describe("Suggested hold duration in seconds"),
  fade_out: z.number().positive().optional().describe("Fade-out duration in seconds"),
  title_color: colorSchema.optional(),
  subtitle_color: colorSchema.optional(),
  background_color: colorSchema.optional(),
  accent_color: colorSchema.optional(),
  title_font_size: z.number().positive().optional(),
  subtitle_font_size: z.number().positive().optional(),
  eyebrow_font_size: z.number().positive().optional(),
};

export function createPromptBrushTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "prompt_generate",
      description:
        "Generate complete Unreal Engine 4.27 gameplay systems from a natural language prompt. " +
        "Creates Blueprints, Widget Blueprints, materials, data assets, maps, actors, and input " +
        "mappings. Supports genres: puzzle_fighter, menu_system, horror, platformer, inventory, generic. " +
        "Use dry_run=true to preview the build spec without creating any assets.",
      inputSchema: z.object({
        prompt: z.string().describe(
          "Natural language description of what to build. " +
          "Examples: 'Make me gameplay like Puzzle Fighter', " +
          "'Create a main menu HUD and pause screen', " +
          "'Build a horror hallway trigger sequence'"
        ),
        dry_run: z
          .boolean()
          .optional()
          .describe("If true, return the build spec JSON without creating any assets in UE4"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("prompt_generate", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "prompt_status",
      description:
        "Check PromptBrush system status: BlueprintGraphBuilder availability, " +
        "WidgetBlueprintBuilder availability, output directory path, and manifest count.",
      inputSchema: z.object({}),
      handler: async (_params) => {
        const result = await client.sendCommand("prompt_status", {});
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "prompt_spec_list",
      description:
        "List previously generated build specs from the PromptBrush output directory. " +
        "Each entry shows feature name, genre, and the original prompt.",
      inputSchema: z.object({}),
      handler: async (_params) => {
        const result = await client.sendCommand("prompt_spec_list", {});
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "widget_title_template",
      description:
        "Return a polished UMG title or lower-third widget JSON spec with named fade-in and fade-out animations. " +
        "Does not create an asset.",
      inputSchema: z.object({
        ...titleBaseSchema,
        style: z.enum(["title", "lower_third"]).optional().describe("Template style to generate"),
        lower_third: z.boolean().optional().describe("Alias for style=lower_third"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("widget_title_template", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "widget_title_card_create",
      description:
        "Create a centered cinematic title-card Widget Blueprint with styled text, background treatment, and FadeIn/FadeOut animation specs.",
      inputSchema: z.object({
        ...titleBaseSchema,
        asset_name: z.string().default("WBP_TitleCard"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("widget_title_card_create", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "widget_lower_third_create",
      description:
        "Create a broadcast-style lower-third Widget Blueprint with styled title/subtitle text and FadeIn/FadeOut animation specs.",
      inputSchema: z.object({
        ...titleBaseSchema,
        asset_name: z.string().default("WBP_LowerThird"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("widget_lower_third_create", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "widget_build_from_json",
      description:
        "Create a Widget Blueprint from a JSON widget tree using WidgetBlueprintBuilderLibrary. " +
        "Creates the asset at package_path/asset_name and populates the designer tree.",
      inputSchema: z.object({
        package_path: z
          .string()
          .startsWith("/")
          .describe("Content directory path e.g. /Game/UI/Widgets"),
        asset_name: z
          .string()
          .describe("Widget Blueprint asset name e.g. WBP_MyWidget"),
        widget_json: z
          .record(z.unknown())
          .describe("Widget tree object with type, name, properties, slot, and children fields"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("widget_build_from_json", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
  ];
}
