/**
 * Unit tests for animation pose analysis tools (Pass 1).
 * Mock server; no UE4 instance required.
 *
 * These cover the TS layer only: tool surface, command routing, parameter
 * passthrough, and error plumbing. The statistics themselves are tested in
 * Plugins/MCPBridge/Content/Python/tests/test_anim_math.py, which needs no
 * editor either.
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

const EXPECTED = ["anim_pose_snapshot", "anim_pose_delta", "anim_root_motion_analyze"];

test("exposes exactly the Pass 1 read-only tools", async () => {
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
