/**
 * Animation pose analysis tools (Passes 1 and 2).
 *
 * Bridges to unreal.AnimationLibrary via the Python listener. Every command
 * here reads raw animation data and mutates nothing, so none of them belong in
 * the modifyingCommands set in index.ts yet. anim_reanchor is dry-run only
 * until Pass 3 adds the C++ write path; registering it as modifying is a
 * Pass 3 step, listed in the design spec.
 *
 * Defaults are applied by the Python handler rather than by Zod, so that a
 * caller invoking the handler directly gets the same behavior as one going
 * through schema validation.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const sequencePath = z.string().describe(
  "AnimSequence content path, e.g. /Game/Anims/SK_Donathan_Idle_Final.");

const boneList = z.array(z.string()).optional().describe(
  "Bone names to include. Omit for every animated track.");

export function createAnimationTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "anim_pose_snapshot",
      description:
        "Capture the local (parent-relative) pose of an AnimSequence at one frame: " +
        "per-bone rotation quaternion, Rotator, translation, and scale. Use this to " +
        "record an idle's anchor pose before editing it, so later drift can be measured " +
        "against a known baseline. Read-only.",
      inputSchema: z.object({
        sequence_path: sequencePath,
        frame: z.number().int().min(0).optional()
          .describe("Frame index. Default 0."),
        bones: boneList,
      }),
      handler: async (params) => {
        const result = await client.sendCommand("anim_pose_snapshot", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "anim_pose_delta",
      description:
        "Compare two AnimSequences bone by bone at a given frame and report the angular " +
        "difference in degrees, sorted worst first. Use it to find which clips drifted " +
        "after an idle was edited, and which bones are responsible. Read-only.",
      inputSchema: z.object({
        reference_path: z.string().describe(
          "Reference AnimSequence, normally the idle."),
        target_path: z.string().describe(
          "Sequence being checked against the reference."),
        reference_frame: z.number().int().min(0).optional()
          .describe("Frame to read on the reference. Default 0."),
        target_frame: z.number().int().min(0).optional()
          .describe("Frame to read on the target. Default 0."),
        bones: z.array(z.string()).optional().describe(
          "Bone names to compare. Omit for every track present in both sequences."),
        threshold_degrees: z.number().min(0).optional().describe(
          "Omit bones whose delta is under this. Default 0, i.e. report everything."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("anim_pose_delta", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "anim_root_motion_analyze",
      description:
        "Measure root track drift on an AnimSequence: per-axis range, per-frame step " +
        "distribution (mean/max/p95), a roughness ratio that separates smooth authored " +
        "sway from per-frame step noise, a root-vs-pelvis comparison that flags motion " +
        "baked onto root at export, and the bone compression codec. Pass folder_path " +
        "instead of sequence_path to sweep a folder, sorted by roughness descending. " +
        "Reads import-time data, so runtime causes such as foot IK or decompression " +
        "artifacts cannot appear here. Read-only.",
      inputSchema: z.object({
        sequence_path: z.string().optional().describe(
          "Single AnimSequence to analyze. Provide this or folder_path, not both."),
        folder_path: z.string().optional().describe(
          "Content folder to sweep, e.g. /Game/Anims/Blends. Provide this or " +
          "sequence_path, not both."),
        root_bone: z.string().optional().describe("Root bone name. Default \"root\"."),
        pelvis_bone: z.string().optional().describe("Pelvis bone name. Default \"pelvis\"."),
        recursive: z.boolean().optional().describe(
          "Folder mode only: recurse into subfolders. Default true."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("anim_root_motion_analyze", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "anim_reanchor",
      description:
        "Report what re-anchoring a sequence to a reference pose would change: per-bone " +
        "angular delta, how many keys each weight profile would touch, and which tracks " +
        "would need key expansion first. Use it to preview a fix after editing an idle. " +
        "Dry run only for now; the write path arrives in Pass 3, and dry_run=false is " +
        "refused rather than ignored. Additive sequences are refused outright. Defaults " +
        "to the spine_01 subtree with root and pelvis excluded, because rebasing those " +
        "breaks root motion and foot planting.",
      inputSchema: z.object({
        target_path: z.string().describe("AnimSequence to re-anchor."),
        reference_path: z.string().describe(
          "AnimSequence supplying the anchor pose, normally the idle."),
        reference_frame: z.number().int().min(0).optional()
          .describe("Frame to read on the reference. Default 0."),
        anchor_frame: z.number().int().min(0).optional()
          .describe("Frame of the target to align to the reference. Default 0."),
        bone_mask: z.object({
          include_bones: z.array(z.string()).optional()
            .describe("Explicit bone names to include."),
          include_subtrees: z.array(z.string()).optional()
            .describe("Include these bones and everything beneath them, e.g. [\"spine_01\"]."),
          exclude_bones: z.array(z.string()).optional()
            .describe("Bone names to drop after the includes are resolved."),
        }).optional().describe(
          "Omit for the default upper-body mask: the spine_01 subtree, minus root and pelvis."),
        profile: z.enum(["constant", "decay", "both_ends"]).optional().describe(
          "constant re-poses the whole clip; decay snaps the start and preserves the " +
          "tail; both_ends corrects the first and last keys for loops. Default decay."),
        window_frames: z.number().int().min(0).optional()
          .describe("Frames over which decay falls to zero. Default 12."),
        threshold_degrees: z.number().min(0).optional()
          .describe("Skip bones whose delta is under this. Default 1.5."),
        include_translation: z.boolean().optional().describe(
          "Also report per-bone translation deltas. Default false; rebasing translation " +
          "on limb bones stretches the character."),
        dry_run: z.boolean().optional().describe(
          "Must be true until Pass 3 lands. Default true."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("anim_reanchor", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
  ];
}
