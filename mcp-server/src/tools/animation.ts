/**
 * Animation pose analysis and re-anchoring tools.
 *
 * Bridges to unreal.AnimationLibrary via the Python listener. The three
 * analysis tools are read-only. anim_reanchor and anim_batch_reanchor default
 * to dry_run and only write when it is explicitly false, so both are in the
 * modifyingCommands set in index.ts.
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
        "baked onto root at export, a rotation_roughness that detects wobble " +
        "independently of amplitude, and the bone compression codec. A missing root " +
        "track is reported as a flag rather than an error, so clips whose root was " +
        "stripped are still analyzed. Pass folder_path " +
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
        bones: z.array(z.string()).optional().describe(
          "Additional bones to analyze alongside root and pelvis, e.g. " +
          "[\"spine_01\", \"clavicle_l\"]. Pass [\"*\"] for every animated track. " +
          "Each gets the same translation and rotation statistics, and the response " +
          "includes wobble_ranking: bones sorted by oscillating motion per frame, " +
          "which is roughness weighted by amplitude so float noise on a barely-moving " +
          "bone does not outrank a visible defect."),
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
        "Defaults to dry_run; set dry_run=false to write, which is " +
        "refused when the clip is judged divergent. Additive sequences are refused " +
        "outright. Defaults " +
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
          "Default true. Set false to actually write raw rotation keys."),
        force: z.boolean().optional().describe(
          "Write even when the clip is judged divergent. Default false. A divergent " +
          "clip usually has a legitimately different start pose rather than drift, so " +
          "forcing it drags the character toward the reference."),
        create_backup: z.boolean().optional().describe(
          "Duplicate the asset to <name>_PreReanchor before writing. Default true. " +
          "UE4.27 undo of raw animation data is not reliable, so this is the real " +
          "safety net rather than Ctrl+Z."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("anim_reanchor", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "anim_batch_reanchor",
      description:
        "Dry-run a re-anchor across a folder or an explicit list of sequences and rank " +
        "them by drift. This is the tool for the core workflow: edit the idle, then find " +
        "which clips no longer line up with it. Each clip gets a verdict of aligned, " +
        "drifted, or divergent. Divergent means the clip legitimately starts in a " +
        "different pose rather than having drifted, so re-anchoring it would drag the " +
        "character somewhere the animator never intended. A sequence that cannot be " +
        "re-anchored is reported in skipped with a reason instead of aborting the sweep. " +
        "Clips judged divergent are refused unless force is set.",
      inputSchema: z.object({
        reference_path: z.string().describe(
          "AnimSequence supplying the anchor pose, normally the idle."),
        folder_path: z.string().optional().describe(
          "Content folder to sweep. Provide this or target_paths, not both."),
        target_paths: z.array(z.string()).optional().describe(
          "Explicit sequences to check. Provide this or folder_path, not both."),
        recursive: z.boolean().optional()
          .describe("Folder mode only: recurse into subfolders. Default true."),
        limit: z.number().int().min(0).optional().describe(
          "Stop after this many sequences. Truncation is reported in " +
          "truncated_by_limit rather than applied silently."),
        reference_frame: z.number().int().min(0).optional()
          .describe("Frame to read on the reference. Default 0."),
        anchor_frame: z.number().int().min(0).optional()
          .describe("Frame of each target to align. Default 0."),
        bone_mask: z.object({
          include_bones: z.array(z.string()).optional(),
          include_subtrees: z.array(z.string()).optional(),
          exclude_bones: z.array(z.string()).optional(),
        }).optional().describe(
          "Omit for the default upper-body mask: the spine_01 subtree, minus root and pelvis."),
        profile: z.enum(["constant", "decay", "both_ends"]).optional()
          .describe("Weight profile. Default decay."),
        window_frames: z.number().int().min(0).optional()
          .describe("Frames over which decay falls to zero. Default 12."),
        threshold_degrees: z.number().min(0).optional()
          .describe("Skip bones whose delta is under this. Default 1.5."),
        review_ceiling_degrees: z.number().min(0).optional().describe(
          "Above this worst-bone delta a clip is called divergent rather than drifted. " +
          "Default 30."),
        include_translation: z.boolean().optional()
          .describe("Also compute translation deltas. Default false."),
        dry_run: z.boolean().optional()
          .describe("Default true. Set false to write every non-refused clip."),
        force: z.boolean().optional()
          .describe("Write divergent clips too. Default false."),
        create_backup: z.boolean().optional()
          .describe("Back each asset up to <name>_PreReanchor before writing. Default true."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("anim_batch_reanchor", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
  ];
}
