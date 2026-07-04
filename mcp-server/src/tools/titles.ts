/**
 * Data-driven Sequencer title tools.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const boxSchema = z.tuple([z.number(), z.number(), z.number(), z.number()]);
const resolutionSchema = z.tuple([z.number().positive(), z.number().positive()]);

const styleSchema = z.object({
  font: z.string().optional(),
  fillColor: z.string().optional(),
  shadowColor: z.string().optional(),
  shadowOffset: z.tuple([z.number(), z.number()]).optional(),
  fontSize: z.number().positive().optional(),
  roleFontSize: z.number().positive().optional(),
  nameFontSize: z.number().positive().optional(),
}).passthrough();

const cueSchema = z.object({
  id: z.string().optional(),
  type: z.enum(["rating_box", "main_title", "two_line_credit", "lower_third"]).optional(),
  text: z.string().optional(),
  role: z.string().optional(),
  name: z.string().optional(),
  startFrame: z.number().int().min(0).optional(),
  fadeInFrames: z.number().int().min(0).optional(),
  holdFrames: z.number().int().min(0).optional(),
  fadeOutFrames: z.number().int().min(0).optional(),
  box: boxSchema.optional(),
  pixel_box: z.tuple([z.number(), z.number(), z.number(), z.number()]).optional(),
  autoSchedule: z.boolean().optional(),
}).passthrough();

const manifestSchema = z.object({
  id: z.string().optional(),
  resolution: resolutionSchema.optional(),
  fps: z.number().positive().optional(),
  style: styleSchema.optional(),
  gapFrames: z.number().int().min(0).optional(),
  cues: z.array(cueSchema).optional(),
}).passthrough();

const manifestLocatorSchema = {
  manifest: manifestSchema.optional(),
  manifest_path: z.string().optional(),
  id: z.string().optional(),
};

function textResult(result: unknown) {
  return {
    content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
  };
}

export function createTitleTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "title_manifest_create",
      description:
        "Create and persist a JSON title manifest using normalized screen boxes, frame timing, and style metadata. " +
        "Stores output in Saved/MCP/titles by default.",
      inputSchema: z.object({
        id: z.string().optional(),
        resolution: resolutionSchema.optional().describe("Design resolution, default [1920, 1080]"),
        fps: z.number().positive().optional().describe("Sequence frame rate, default 29.97"),
        style: styleSchema.optional(),
        gapFrames: z.number().int().min(0).optional().describe("Gap between auto-scheduled cues"),
        cues: z.array(cueSchema).describe("Title cues with normalized box values"),
        output_path: z.string().optional(),
        manifest: manifestSchema.optional(),
      }),
      handler: async (params) => textResult(await client.sendCommand("title_manifest_create", params)),
    },
    {
      name: "title_manifest_validate",
      description:
        "Validate a title manifest for normalized boxes, valid cue types, positive frame rate, and frame timing.",
      inputSchema: z.object({
        ...manifestLocatorSchema,
      }),
      handler: async (params) => textResult(await client.sendCommand("title_manifest_validate", params)),
    },
    {
      name: "title_manifest_from_reference",
      description:
        "Create a manifest from measured reference-frame pixel boxes by converting them into normalized screen boxes.",
      inputSchema: z.object({
        id: z.string().optional(),
        frame_size: resolutionSchema.optional().describe("Reference frame size used by pixel_box measurements"),
        reference_size: resolutionSchema.optional(),
        resolution: resolutionSchema.optional().describe("Target design resolution, defaults to reference size"),
        fps: z.number().positive().optional(),
        style: styleSchema.optional(),
        gapFrames: z.number().int().min(0).optional(),
        cues: z.array(cueSchema).describe("Cues may include pixel_box instead of normalized box"),
        output_path: z.string().optional(),
      }),
      handler: async (params) => textResult(await client.sendCommand("title_manifest_from_reference", params)),
    },
    {
      name: "title_widget_build_from_manifest",
      description:
        "Build WBP_TitleRenderer from a title manifest, or return the generated widget spec with dry_run=true.",
      inputSchema: z.object({
        ...manifestLocatorSchema,
        package_path: z.string().startsWith("/").optional().describe("Default /Game/UI/Titles"),
        asset_name: z.string().optional().describe("Default WBP_TitleRenderer"),
        dry_run: z.boolean().optional().describe("Return the widget JSON spec without creating an asset"),
      }),
      handler: async (params) => textResult(await client.sendCommand("title_widget_build_from_manifest", params)),
    },
    {
      name: "title_controller_create",
      description:
        "Create a BP_TitleController implementation spec that owns the widget and updates cues from Sequencer frame numbers.",
      inputSchema: z.object({
        ...manifestLocatorSchema,
        package_path: z.string().startsWith("/").optional().describe("Default /Game/Cinematics/Titles"),
        asset_name: z.string().optional().describe("Default BP_TitleController"),
      }),
      handler: async (params) => textResult(await client.sendCommand("title_controller_create", params)),
    },
    {
      name: "title_sequence_bind",
      description:
        "Create a Level Sequence binding spec for driving BP_TitleController from frame numbers. " +
        "This is a v1 scaffold until the bridge has full Sequencer event-track authoring.",
      inputSchema: z.object({
        ...manifestLocatorSchema,
        sequence_path: z.string().startsWith("/").optional(),
        controller_path: z.string().startsWith("/").optional(),
      }),
      handler: async (params) => textResult(await client.sendCommand("title_sequence_bind", params)),
    },
    {
      name: "title_render_compare",
      description:
        "Compare a UE-rendered title frame against a reference still and report pixel error for the full image or region.",
      inputSchema: z.object({
        reference_path: z.string().describe("Reference image path"),
        render_path: z.string().describe("Rendered image path"),
        region: z.tuple([z.number().int(), z.number().int(), z.number().int(), z.number().int()]).optional(),
      }),
      handler: async (params) => textResult(await client.sendCommand("title_render_compare", params)),
    },
    {
      name: "title_manifest_adjust",
      description:
        "Apply measured correction passes to a manifest by changing cue boxes, frame timing, text, role, or name fields.",
      inputSchema: z.object({
        ...manifestLocatorSchema,
        adjustments: z.array(z.object({
          id: z.string(),
          box: boxSchema.optional(),
          box_delta: boxSchema.optional(),
          startFrame: z.number().int().min(0).optional(),
          fadeInFrames: z.number().int().min(0).optional(),
          holdFrames: z.number().int().min(0).optional(),
          fadeOutFrames: z.number().int().min(0).optional(),
          text: z.string().optional(),
          role: z.string().optional(),
          name: z.string().optional(),
        })).describe("Per-cue corrections from reference comparison"),
        output_path: z.string().optional(),
      }),
      handler: async (params) => textResult(await client.sendCommand("title_manifest_adjust", params)),
    },
  ];
}
