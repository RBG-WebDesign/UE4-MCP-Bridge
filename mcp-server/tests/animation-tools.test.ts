/**
 * Unit tests for animation pose analysis and re-anchoring tools.
 * Mock server; no UE4 instance required.
 *
 * These cover the TS layer only: tool surface, command routing, parameter
 * passthrough, the divergent-clip write refusal, and error plumbing. The math
 * and the handler logic are tested in
 * Plugins/MCPBridge/Content/Python/tests/test_anim_math.py and
 * test_animation_handlers.py, neither of which needs an editor.
 */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createAnimationTools } from "../src/tools/animation.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18781;

let server: MockUnrealServer;
let client: UnrealClient;
let toolMap: Map<string, ToolDefinition>;

interface TestCase { name: string; fn: () => Promise<void>; }
const tests: TestCase[] = [];
let passed = 0;
let failed = 0;

function test(name: string, fn: () => Promise<void>): void {
  tests.push({ name, fn });
}

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(`Assertion failed: ${message}`);
}

async function callTool(name: string, params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const tool = toolMap.get(name);
  if (!tool) throw new Error(`Tool not found: ${name}`);
  const result = await tool.handler(params);
  return JSON.parse((result.content[0] as { text: string }).text) as Record<string, unknown>;
}

async function setup(): Promise<void> {
  server = new MockUnrealServer();
  await server.start(TEST_PORT);
  client = new UnrealClient({ port: TEST_PORT });
  toolMap = new Map(createAnimationTools(client).map((t) => [t.name, t]));

  server.setHandler("anim_pose_snapshot", (params) => ({
    success: true,
    data: {
      sequence: (params as Record<string, unknown>).sequence_path,
      frame: (params as Record<string, unknown>).frame ?? 0,
      bone_count: 2,
      bones: [
        { bone: "clavicle_l", rotation_quat: [0, 0, 0, 1], translation: [0, 0, 0] },
        { bone: "hand_l", rotation_quat: [0, 0, 0, 1], translation: [0, 0, 0] },
      ],
      skipped_no_track: [],
      received: params,
    },
  }));

  server.setHandler("anim_pose_delta", (params) => ({
    success: true,
    data: {
      reference: (params as Record<string, unknown>).reference_path,
      target: (params as Record<string, unknown>).target_path,
      bones_compared: 3,
      bones_over_threshold: 1,
      max_delta_degrees: 18.4,
      deltas: [{ bone: "clavicle_l", delta_degrees: 18.4 }],
      skipped_no_track: [],
      received: params,
    },
  }));

  server.setHandler("anim_root_motion_analyze", (params) => {
    const p = params as Record<string, unknown>;
    if (p.folder_path) {
      return {
        success: true,
        data: {
          folder: p.folder_path,
          count: 2,
          sequences: [
            { sequence: "/Game/A", root: { roughness: 1.4, classification: "noisy" } },
            { sequence: "/Game/B", root: { roughness: 0.07, classification: "smooth_drift" } },
          ],
          errors: [],
          received: params,
        },
      };
    }
    return {
      success: true,
      data: {
        sequence: p.sequence_path,
        num_frames: 556,
        length_seconds: 18.53,
        root: {
          range_cm: { x: 3.35, y: 2.34, z: 0.08 },
          roughness: 0.07,
          classification: "smooth_drift",
        },
        pelvis: { local_range_cm: { x: 0.07, y: 0.05, z: 0.02 } },
        flags: ["inverted_root_authoring"],
        compression: { codec: "DefaultAnimBoneCompressionSettings", error_threshold: null },
        received: params,
      },
    };
  });
}

const EXPECTED = [
  "anim_pose_snapshot", "anim_pose_delta", "anim_root_motion_analyze",
  "anim_reanchor", "anim_batch_reanchor",
];

test("exposes exactly the five animation tools", async () => {
  for (const name of EXPECTED) {
    assert(toolMap.has(name), `missing tool: ${name}`);
  }
  assert(toolMap.size === EXPECTED.length,
    `expected ${EXPECTED.length} tools, got ${toolMap.size}: ${[...toolMap.keys()].join(", ")}`);
});

test("every tool has a non-empty description", async () => {
  for (const [name, tool] of toolMap) {
    assert(tool.description.length > 40, `description too thin for ${name}`);
  }
});

test("anim_pose_snapshot forwards sequence_path and frame", async () => {
  const res = await callTool("anim_pose_snapshot", {
    sequence_path: "/Game/Anims/SK_Donathan_Idle_Final",
    frame: 0,
  });
  assert(res.success === true, "expected success");
  const data = res.data as Record<string, unknown>;
  assert(data.sequence === "/Game/Anims/SK_Donathan_Idle_Final", "sequence not echoed");
  const received = data.received as Record<string, unknown>;
  assert(received.frame === 0, "frame not forwarded");
});

test("anim_pose_snapshot forwards an explicit bone list", async () => {
  const res = await callTool("anim_pose_snapshot", {
    sequence_path: "/Game/A",
    bones: ["clavicle_l", "upperarm_l", "lowerarm_l", "hand_l"],
  });
  const received = (res.data as Record<string, unknown>).received as Record<string, unknown>;
  const bones = received.bones as string[];
  assert(Array.isArray(bones) && bones.length === 4, "bone list not forwarded");
  assert(bones[0] === "clavicle_l", "bone order not preserved");
});

test("anim_pose_delta forwards both sequences and the threshold", async () => {
  const res = await callTool("anim_pose_delta", {
    reference_path: "/Game/Idle",
    target_path: "/Game/Possessed",
    threshold_degrees: 1.5,
  });
  assert(res.success === true, "expected success");
  const data = res.data as Record<string, unknown>;
  const received = data.received as Record<string, unknown>;
  assert(received.reference_path === "/Game/Idle", "reference_path not forwarded");
  assert(received.target_path === "/Game/Possessed", "target_path not forwarded");
  assert(received.threshold_degrees === 1.5, "threshold not forwarded");
  assert(data.max_delta_degrees === 18.4, "delta payload not returned");
});

test("anim_root_motion_analyze single-sequence mode returns the root block", async () => {
  const res = await callTool("anim_root_motion_analyze", {
    sequence_path: "/Game/Anims/SK_Donathan_Idle_Final",
  });
  assert(res.success === true, "expected success");
  const data = res.data as Record<string, unknown>;
  const root = data.root as Record<string, unknown>;
  assert(root.classification === "smooth_drift", "classification missing");
  const flags = data.flags as string[];
  assert(flags.includes("inverted_root_authoring"), "expected inverted root flag");
});

test("anim_root_motion_analyze folder mode returns a sorted list", async () => {
  const res = await callTool("anim_root_motion_analyze", {
    folder_path: "/Game/Anims/Blends",
    recursive: true,
  });
  assert(res.success === true, "expected success");
  const data = res.data as Record<string, unknown>;
  const sequences = data.sequences as Array<Record<string, unknown>>;
  assert(sequences.length === 2, "expected two sequences");
  const first = sequences[0].root as Record<string, unknown>;
  const second = sequences[1].root as Record<string, unknown>;
  assert((first.roughness as number) > (second.roughness as number),
    "folder results not sorted by roughness descending");
  const received = data.received as Record<string, unknown>;
  assert(received.recursive === true, "recursive not forwarded");
});

test("anim_root_motion_analyze passes bone name overrides through", async () => {
  const res = await callTool("anim_root_motion_analyze", {
    sequence_path: "/Game/A",
    root_bone: "Root",
    pelvis_bone: "Hips",
  });
  const received = (res.data as Record<string, unknown>).received as Record<string, unknown>;
  assert(received.root_bone === "Root", "root_bone not forwarded");
  assert(received.pelvis_bone === "Hips", "pelvis_bone not forwarded");
});

test("anim_reanchor forwards profile, window, threshold, and mask", async () => {
  server.setHandler("anim_reanchor", (params) => ({
    success: true,
    data: {
      target: (params as Record<string, unknown>).target_path,
      dry_run: true,
      bones_modified: 2,
      max_delta_degrees: 18.4,
      deltas: [
        { bone: "clavicle_l", delta_degrees: 18.4, key_count: 61, keys_written: 12 },
        { bone: "upperarm_l", delta_degrees: 11.2, key_count: 61, keys_written: 12 },
      ],
      needs_key_expansion: [],
      received: params,
    },
  }));
  const res = await callTool("anim_reanchor", {
    target_path: "/Game/Possessed",
    reference_path: "/Game/Idle",
    profile: "decay",
    window_frames: 12,
    threshold_degrees: 1.5,
    bone_mask: { include_subtrees: ["spine_01"], exclude_bones: ["root", "pelvis"] },
  });
  assert(res.success === true, "expected success");
  const data = res.data as Record<string, unknown>;
  assert(data.dry_run === true, "dry_run should come back true");
  const received = data.received as Record<string, unknown>;
  assert(received.profile === "decay", "profile not forwarded");
  assert(received.window_frames === 12, "window_frames not forwarded");
  assert(received.threshold_degrees === 1.5, "threshold not forwarded");
  const mask = received.bone_mask as Record<string, unknown>;
  const subtrees = mask.include_subtrees as string[];
  assert(subtrees[0] === "spine_01", "bone mask subtrees not forwarded");
});

test("anim_reanchor rejects an unknown profile at the schema layer", async () => {
  const tool = toolMap.get("anim_reanchor");
  if (!tool) throw new Error("anim_reanchor missing");
  const parsed = tool.inputSchema.safeParse({
    target_path: "/Game/A",
    reference_path: "/Game/B",
    profile: "ease_in_out_quart",
  });
  assert(parsed.success === false, "expected schema rejection for an unknown profile");
});

test("anim_reanchor accepts the three documented profiles", async () => {
  const tool = toolMap.get("anim_reanchor");
  if (!tool) throw new Error("anim_reanchor missing");
  for (const profile of ["constant", "decay", "both_ends"]) {
    const parsed = tool.inputSchema.safeParse({
      target_path: "/Game/A", reference_path: "/Game/B", profile,
    });
    assert(parsed.success === true, `profile ${profile} should be accepted`);
  }
});

test("anim_reanchor refuses to write a divergent clip without force", async () => {
  server.setHandler("anim_reanchor", (params) => {
    const p = params as Record<string, unknown>;
    if (p.dry_run === false && !p.force) {
      return {
        success: false,
        data: { written: false, verdict: { code: "divergent" } },
        error: "Refusing to write /Game/Run180: max 80.65 deg exceeds the 30 deg review "
          + "ceiling. Re-run with force=true if this is genuinely what you want.",
      };
    }
    return { success: true, data: { written: true, forced: true, received: params } };
  });
  const refused = await callTool("anim_reanchor", {
    target_path: "/Game/Run180", reference_path: "/Game/Idle", dry_run: false,
  });
  assert(refused.success === false, "expected refusal for a divergent clip");
  assert(String(refused.error).includes("force=true"), `unexpected error: ${refused.error}`);

  const forced = await callTool("anim_reanchor", {
    target_path: "/Game/Run180", reference_path: "/Game/Idle", dry_run: false, force: true,
  });
  assert(forced.success === true, "force should let the write through");
  assert((forced.data as Record<string, unknown>).forced === true, "forced flag not set");
});

test("anim_reanchor forwards create_backup", async () => {
  server.setHandler("anim_reanchor", (params) => ({
    success: true,
    data: {
      written: true,
      write_report: { bones_written: 14, keys_written: 168, backup: "/Game/X_PreReanchor" },
      received: params,
    },
  }));
  const res = await callTool("anim_reanchor", {
    target_path: "/Game/X", reference_path: "/Game/Idle",
    dry_run: false, create_backup: true,
  });
  const data = res.data as Record<string, unknown>;
  const received = data.received as Record<string, unknown>;
  assert(received.create_backup === true, "create_backup not forwarded");
  const report = data.write_report as Record<string, unknown>;
  assert(String(report.backup).endsWith("_PreReanchor"), "backup path not reported");
});

test("anim_batch_reanchor reports written and refused separately", async () => {
  server.setHandler("anim_batch_reanchor", () => ({
    success: true,
    data: {
      dry_run: false,
      assets_written: ["/Game/Walk"],
      refused: [{ sequence: "/Game/Run180", reason: "divergent; max 80.65 deg" }],
      verdict_counts: { aligned: 0, drifted: 1, divergent: 1 },
    },
  }));
  const res = await callTool("anim_batch_reanchor", {
    reference_path: "/Game/Idle", folder_path: "/Game/Blends", dry_run: false,
  });
  const data = res.data as Record<string, unknown>;
  const written = data.assets_written as string[];
  const refused = data.refused as Array<Record<string, unknown>>;
  assert(written.length === 1 && written[0] === "/Game/Walk", "written list wrong");
  assert(refused.length === 1, "refused list wrong");
  assert(String(refused[0].reason).includes("divergent"), "refusal reason not carried");
});

test("anim_batch_reanchor ranks sequences and reports verdict counts", async () => {
  server.setHandler("anim_batch_reanchor", (params) => ({
    success: true,
    data: {
      reference: (params as Record<string, unknown>).reference_path,
      dry_run: true,
      analyzed: 3,
      verdict_counts: { aligned: 1, drifted: 1, divergent: 1 },
      truncated_by_limit: 0,
      sequences: [
        { sequence: "/Game/Run180", max_delta_degrees: 80.65, verdict: "divergent" },
        { sequence: "/Game/Walk", max_delta_degrees: 6.2, verdict: "drifted" },
        { sequence: "/Game/IdleVar", max_delta_degrees: 0.4, verdict: "aligned" },
      ],
      skipped: [{ sequence: "/Game/Additive", reason: "additive sequence (AAT_LocalSpaceBase)" }],
      received: params,
    },
  }));
  const res = await callTool("anim_batch_reanchor", {
    reference_path: "/Game/Idle",
    folder_path: "/Game/Anims/Blends",
  });
  assert(res.success === true, "expected success");
  const data = res.data as Record<string, unknown>;
  const sequences = data.sequences as Array<Record<string, unknown>>;
  assert(sequences.length === 3, "expected three rows");
  assert((sequences[0].max_delta_degrees as number) > (sequences[1].max_delta_degrees as number),
    "rows not sorted by drift descending");
  assert(sequences[0].verdict === "divergent", "worst clip should be divergent");
  const skipped = data.skipped as Array<Record<string, unknown>>;
  assert(String(skipped[0].reason).includes("additive"), "skip reason not carried");
});

test("anim_batch_reanchor forwards the review ceiling and limit", async () => {
  const res = await callTool("anim_batch_reanchor", {
    reference_path: "/Game/Idle",
    target_paths: ["/Game/A", "/Game/B"],
    review_ceiling_degrees: 45,
    limit: 10,
  });
  const received = (res.data as Record<string, unknown>).received as Record<string, unknown>;
  assert(received.review_ceiling_degrees === 45, "review ceiling not forwarded");
  assert(received.limit === 10, "limit not forwarded");
  const targets = received.target_paths as string[];
  assert(targets.length === 2, "target_paths not forwarded");
});

test("anim_batch_reanchor requires a reference path at the schema layer", async () => {
  const tool = toolMap.get("anim_batch_reanchor");
  if (!tool) throw new Error("anim_batch_reanchor missing");
  const parsed = tool.inputSchema.safeParse({ folder_path: "/Game/Anims" });
  assert(parsed.success === false, "expected rejection without reference_path");
});

test("listener errors surface as success=false with the message", async () => {
  server.setHandler("anim_pose_delta", () => ({
    success: false,
    data: {},
    error: "target_path: Asset does not exist: /Game/Nope",
  }));
  const res = await callTool("anim_pose_delta", {
    reference_path: "/Game/Idle",
    target_path: "/Game/Nope",
  });
  assert(res.success === false, "expected failure");
  assert(String(res.error).includes("/Game/Nope"), `unexpected error: ${res.error}`);
});

async function main(): Promise<void> {
  await setup();
  for (const t of tests) {
    try {
      await t.fn();
      passed += 1;
      console.log(`  PASS  ${t.name}`);
    } catch (err) {
      failed += 1;
      console.error(`  FAIL  ${t.name}`);
      console.error(`        ${(err as Error).message}`);
    }
  }
  await server.stop();
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
