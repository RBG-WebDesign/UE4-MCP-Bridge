/**
 * UE4.27 performance analysis and optimization tools.
 *
 * These forward to handlers in the listener's optimization module, which was
 * written for the in-editor panel and had no MCP surface at all: 24 working
 * handlers that no client could reach. The 2026-07-29 gap audit found the whole
 * area sitting at 4 tools out of a possible 32.
 *
 * Almost everything here reads or captures. Two mutate:
 * optimization_apply_low_risk_fixes writes asset changes, and
 * optimization_before_after_verify does too when apply_candidates is set.
 *
 * Capture tools drive the viewport and write PNG or report files under Saved/.
 * They are annotated mutatingIdempotent rather than readOnly for that reason:
 * they change no project content, but they are not free of side effects.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

function respond(result: unknown) {
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
}

/** Screenshot geometry, shared by every capture tool. */
const captureSize = {
  width: z.number().int().min(320).max(7680).default(1920).describe("Screenshot width in pixels."),
  height: z.number().int().min(240).max(4320).default(1080).describe("Screenshot height in pixels."),
};

/** Asset-scan scope, shared by the audit tools. */
const scanScope = {
  path: z.string().default("/Game/").describe("Content path to scan, e.g. /Game/Characters."),
  recursive: z.boolean().default(true).describe("Include subfolders."),
};

const TARGETS = ["desktop_60", "desktop_30", "vr_72", "vr_90", "vr_120", "custom"] as const;

export function createOptimizationTools(client: UnrealClient): ToolDefinition[] {
  return [
    // ---------------------------------------------------------------- triage
    {
      name: "optimization_tool_catalog",
      description:
        "Search the UE4.27 profiling command catalog. Each entry names a console command, the question it answers, " +
        "when to reach for it, what a bad reading looks like, and what to do next. Start here when you do not yet " +
        "know which capture to run. Filter by category or free-text query.",
      inputSchema: z.object({
        category: z.string().default("").describe("Category filter, e.g. 'Frame Triage', 'Scene Optimization'."),
        query: z.string().default("").describe("Free-text search across commands and their descriptions."),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_tool_catalog", p)),
    },
    {
      name: "optimization_quick_triage",
      description:
        "First-pass performance investigation: runs the triage console commands, captures overlays, optionally audits " +
        "assets and the scene, then classifies findings against a frame budget. Run this before any targeted capture " +
        "so the follow-up is aimed at the thread that is actually slow.",
      inputSchema: z.object({
        target: z.enum(TARGETS).default("desktop_60").describe("Frame budget preset."),
        custom_frame_ms: z.number().positive().optional().describe("Budget in milliseconds when target is 'custom'."),
        commands: z.array(z.string()).optional().describe("Override the default triage command set."),
        include_asset_audit: z.boolean().default(true),
        include_scene_audit: z.boolean().default(true),
        include_vr_checks: z.boolean().default(false),
        capture_screenshots: z.boolean().default(true),
        generate_report: z.boolean().default(false).describe("Also write a Markdown report under Saved/."),
        ...scanScope,
        ...captureSize,
        max_assets: z.number().int().positive().default(600),
        max_findings: z.number().int().positive().default(100),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_quick_triage", p)),
    },
    {
      name: "optimization_run_console_checklist",
      description:
        "Run a list of safe profiling console commands in order and return the parsed findings. Defaults to the " +
        "standard triage set when no commands are given. Use this when you know which stat commands you want rather " +
        "than running a full capture.",
      inputSchema: z.object({
        commands: z.array(z.string()).optional().describe("Console commands, e.g. ['stat unit', 'stat scenerendering']."),
        delay_seconds: z.number().min(0).max(10).default(0.15).describe("Pause between commands so overlays settle."),
        capture_screenshots: z.boolean().default(false),
        include_structured_findings: z.boolean().default(true),
        max_output_findings: z.number().int().positive().default(50),
        ...captureSize,
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_run_console_checklist", p)),
    },

    // ------------------------------------------------------------------ GPU
    {
      name: "optimization_gpu_capture",
      description:
        "Run GPU-focused console commands and capture the render viewmodes that explain GPU cost (shader complexity, " +
        "overdraw, lighting). Use when quick triage shows GPU as the dominant frame cost.",
      inputSchema: z.object({
        viewmodes: z.array(z.string()).optional().describe("Viewmodes to capture. Defaults to the standard GPU set."),
        restore_lit: z.boolean().default(true).describe("Return the viewport to Lit when finished."),
        generate_report: z.boolean().default(true),
        max_output_findings: z.number().int().positive().default(50),
        ...captureSize,
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_gpu_capture", p)),
    },
    {
      name: "optimization_profilegpu_capture",
      description:
        "Run ProfileGPU and parse the per-pass GPU timing breakdown from its text or log output. This is the finest " +
        "grained GPU measurement UE4.27 exposes without an external tool.",
      inputSchema: z.object({
        settle_seconds: z.number().min(0).max(30).default(1.0).describe("Wait before capturing so the frame stabilises."),
        generate_report: z.boolean().default(true),
        ...captureSize,
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_profilegpu_capture", p)),
    },
    {
      name: "optimization_rendering_capture",
      description:
        "Run render-thread and scene-rendering overlays (draw calls, visible primitives, init views) and return parsed " +
        "findings. Use when the Draw thread rather than the GPU is the bottleneck.",
      inputSchema: z.object({
        commands: z.array(z.string()).optional(),
        capture_screenshots: z.boolean().default(true),
        generate_report: z.boolean().default(true),
        max_output_findings: z.number().int().positive().default(50),
        ...captureSize,
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_rendering_capture", p)),
    },
    {
      name: "optimization_viewmode_capture_set",
      description:
        "Capture an explicit set of optimization viewmodes as screenshots and restore the viewport afterwards. Use " +
        "when you want specific views rather than the curated GPU set.",
      inputSchema: z.object({
        viewmodes: z.array(z.string()).optional().describe("Viewmode names, e.g. ['ShaderComplexity', 'LightComplexity']."),
        restore_lit: z.boolean().default(true),
        delay_seconds: z.number().min(0).max(10).default(0.35),
        max_output_findings: z.number().int().positive().default(50),
        ...captureSize,
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_viewmode_capture_set", p)),
    },

    // ------------------------------------------------------------------ CPU
    {
      name: "optimization_cpu_capture",
      description:
        "Run CPU-side overlays covering the game thread, animation, physics, Blueprint and particles, and return parsed " +
        "findings. Use when quick triage shows Game rather than Draw or GPU as the dominant cost.",
      inputSchema: z.object({
        commands: z.array(z.string()).optional(),
        capture_screenshots: z.boolean().default(true),
        generate_report: z.boolean().default(true),
        max_output_findings: z.number().int().positive().default(50),
        ...captureSize,
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_cpu_capture", p)),
    },
    {
      name: "optimization_insights_trace_start",
      description:
        "Start an Unreal Insights trace. Pair with optimization_insights_trace_stop. Use for a timeline of CPU work " +
        "across frames, which the stat overlays cannot show.",
      inputSchema: z.object({
        channels: z
          .string()
          .default("cpu,gpu,frame,bookmark,loadtime,file,counters")
          .describe("Comma-separated Insights channels."),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_insights_trace_start", p)),
    },
    {
      name: "optimization_insights_trace_stop",
      description:
        "Stop the active Unreal Insights trace and summarise the newest trace file when parser support is available. " +
        "Returns the trace path either way.",
      inputSchema: z.object({
        settle_seconds: z.number().min(0).max(30).default(0.5),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_insights_trace_stop", p)),
    },
    {
      name: "optimization_insights_trace_summary",
      description:
        "Summarise an Unreal Insights trace file already on disk. Give a path, or omit it to summarise the newest trace.",
      inputSchema: z.object({
        trace_path: z.string().default("").describe("Absolute path to a .utrace file. Empty means newest."),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_insights_trace_summary", p)),
    },
    {
      name: "optimization_camera_path_profile",
      description:
        "Move the viewport through named viewpoints, measure each, and rank them by cost. Use to find which part of a " +
        "level is expensive rather than which subsystem.",
      inputSchema: z.object({
        viewpoints: z
          .array(z.record(z.unknown()))
          .optional()
          .describe("Viewpoints to visit, each with a name and camera transform."),
        resolution: z
          .object({ width: z.number().int().default(1920), height: z.number().int().default(1080) })
          .optional(),
        show_ui: z.boolean().default(false),
        generate_report: z.boolean().default(true),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_camera_path_profile", p)),
    },

    // --------------------------------------------------------------- memory
    {
      name: "optimization_memory_streaming_capture",
      description:
        "Run memory and texture-streaming overlays and return structured memory findings, optionally with an asset " +
        "audit in the same pass. Use for hitching, streaming pops, or memory ceilings rather than frame cost.",
      inputSchema: z.object({
        commands: z.array(z.string()).optional(),
        capture_screenshots: z.boolean().default(true),
        generate_report: z.boolean().default(true),
        include_asset_audit: z.boolean().default(true),
        large_texture_px: z.number().int().positive().default(2048).describe("Textures at or above this size are flagged."),
        ...scanScope,
        ...captureSize,
        max_assets: z.number().int().positive().default(900),
        max_findings: z.number().int().positive().default(120),
        max_material_slots: z.number().int().positive().default(4),
        max_output_findings: z.number().int().positive().default(50),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_memory_streaming_capture", p)),
    },

    // --------------------------------------------------------------- audits
    {
      name: "optimization_asset_audit",
      description:
        "Scan project assets for common optimization risks: oversized textures, excessive material slots, missing LODs. " +
        "Static analysis, so it needs no PIE session and no capture.",
      inputSchema: z.object({
        ...scanScope,
        large_texture_px: z.number().int().positive().default(4096),
        max_assets: z.number().int().positive().default(800),
        max_findings: z.number().int().positive().default(100),
        max_material_slots: z.number().int().positive().default(4),
        generate_report: z.boolean().default(false),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_asset_audit", p)),
    },
    {
      name: "optimization_scene_audit",
      description:
        "Inspect the open level for lighting, culling, tick and setup risks: dynamic lights, actors ticking every frame, " +
        "missing culling distances. Complements the asset audit, which cannot see level composition.",
      inputSchema: z.object({
        include_design_advice: z.boolean().default(true).describe("Include design-level suggestions, not just measurements."),
        generate_report: z.boolean().default(false),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_scene_audit", p)),
    },
    {
      name: "optimization_vr_audit",
      description:
        "Audit against a VR refresh-rate budget, where the frame budget is far tighter than desktop and forward " +
        "rendering constraints apply. Use for any VR target rather than the desktop audits.",
      inputSchema: z.object({
        target: z.enum(TARGETS).default("vr_90"),
        custom_frame_ms: z.number().positive().optional(),
        include_asset_audit: z.boolean().default(true),
        large_texture_px: z.number().int().positive().default(2048),
        ...scanScope,
        max_assets: z.number().int().positive().default(500),
        max_findings: z.number().int().positive().default(80),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_vr_audit", p)),
    },
    {
      name: "optimization_texture_harm_rank",
      description:
        "Rank textures by likely harm, following dependency trails from the active level so the ranking reflects what " +
        "is actually loaded rather than everything on disk.",
      inputSchema: z.object({
        max_findings: z.number().int().positive().default(30),
        max_output_findings: z.number().int().positive().default(50),
        generate_report: z.boolean().default(false),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_texture_harm_rank", p)),
    },
    {
      name: "optimization_asset_cost_rank",
      description:
        "Rank the meshes in the active level by likely scene cost, combining triangle counts, material slots and " +
        "instance counts. Use to decide what to optimise first.",
      inputSchema: z.object({
        max_findings: z.number().int().positive().default(30),
        generate_report: z.boolean().default(false),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_asset_cost_rank", p)),
    },

    // ---------------------------------------------------------------- stats
    {
      name: "optimization_stat_capture",
      description:
        "Capture stat and CSV profiler files over a duration, where UE4.27 exposes them. Use when you need a recording " +
        "across time rather than a single-frame reading.",
      inputSchema: z.object({
        commands: z.array(z.string()).optional().describe("Start commands, e.g. ['csvprofile start']."),
        stop_commands: z.array(z.string()).optional().describe("Stop commands. Defaults to the matching stops."),
        duration_seconds: z.number().min(0.1).max(600).default(3.0),
        capture_screenshot: z.boolean().default(true),
        generate_report: z.boolean().default(false),
        ...captureSize,
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_stat_capture", p)),
    },
    {
      name: "optimization_visual_logger_ingest",
      description:
        "Parse Visual Logger output or log text for gameplay-heavy CPU clues. Use when the game thread is slow but the " +
        "stat overlays do not say which gameplay system is responsible.",
      inputSchema: z.object({
        paths: z.array(z.string()).min(1).describe("Log or Visual Logger files to parse."),
        limit: z.number().int().positive().default(20),
        generate_report: z.boolean().default(false),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_visual_logger_ingest", p)),
    },

    // ----------------------------------------------------------------- fixes
    {
      name: "optimization_fix_candidates",
      description:
        "Return low-risk fix candidates, kept separate from manual recommendations so an automated pass never touches " +
        "anything needing judgement. Read-only: it proposes, it does not apply. Feed the result to " +
        "optimization_apply_low_risk_fixes.",
      inputSchema: z.object({
        ...scanScope,
        max_assets: z.number().int().positive().default(1000),
        max_items: z.number().int().positive().default(50),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_fix_candidates", p)),
    },
    {
      name: "optimization_apply_low_risk_fixes",
      description:
        "Apply explicit candidates from optimization_fix_candidates. Dry run by default: it reports what it would do " +
        "and changes nothing until dry_run is false. Setting save_assets writes the modified assets to disk, which a " +
        "plain editor undo will not recover.",
      inputSchema: z.object({
        candidates: z.array(z.record(z.unknown())).min(1).describe("Candidates from optimization_fix_candidates."),
        dry_run: z.boolean().default(true).describe("Leave true to preview. False actually modifies assets."),
        save_assets: z.boolean().default(false).describe("Write changes to disk. Not recoverable by editor undo."),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_apply_low_risk_fixes", p)),
    },
    {
      name: "optimization_before_after_verify",
      description:
        "Compare a baseline measurement against an after measurement for a candidate group, so an optimization can be " +
        "shown to have worked rather than assumed to have. Modifies assets only when apply_candidates is true.",
      inputSchema: z.object({
        baseline: z.record(z.unknown()).describe("Baseline measurement payload."),
        after: z.record(z.unknown()).describe("After measurement payload."),
        candidates: z.array(z.record(z.unknown())).default([]),
        apply_candidates: z.boolean().default(false).describe("Apply the candidates as part of this call."),
        save_assets: z.boolean().default(false),
        capture_screenshot: z.boolean().default(true),
        duration_seconds: z.number().min(0.1).max(600).default(2.0),
        screenshots: z.record(z.unknown()).default({}),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_before_after_verify", p)),
    },

    // -------------------------------------------------------------- reports
    {
      name: "optimization_generate_report",
      description:
        "Write a Markdown optimization report under Saved/. Give it a payload from an earlier capture, or omit the " +
        "payload to run a fresh quick triage and report on that.",
      inputSchema: z.object({
        payload: z.record(z.unknown()).optional().describe("Result from an earlier optimization call. Omit to capture fresh."),
        basename: z.string().default("optimization_report").describe("Report filename stem."),
        target: z.enum(TARGETS).default("desktop_60"),
        include_asset_audit: z.boolean().default(true),
        include_scene_audit: z.boolean().default(true),
        include_vr_checks: z.boolean().default(false),
        capture_screenshots: z.boolean().default(true),
      }),
      handler: async (p) => respond(await client.sendCommand("optimization_generate_report", p)),
    },
  ];
}
