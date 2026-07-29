/**
 * Live-editor integration test for animation pose re-anchoring.
 *
 * Set ANIM_POSE_REFERENCE and ANIM_POSE_TARGET to compatible, non-additive
 * AnimSequence paths. The target is duplicated before mutation and deleted after.
 */

import { UnrealClient } from "../../src/unreal-client.js";
import { createAnimationTools } from "../../src/tools/animation.js";
import type { ToolDefinition } from "../../src/types.js";

const referencePath = process.env.ANIM_POSE_REFERENCE;
const targetPath = process.env.ANIM_POSE_TARGET;
const client = new UnrealClient();
const toolMap = new Map<string, ToolDefinition>(
  createAnimationTools(client).map((tool) => [tool.name, tool])
);

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(`Assertion failed: ${message}`);
}

async function callTool(
  name: string,
  params: Record<string, unknown>
): Promise<Record<string, unknown>> {
  const tool = toolMap.get(name);
  if (!tool) throw new Error(`Tool not found: ${name}`);
  const result = await tool.handler(params);
  return JSON.parse(result.content[0].text) as Record<string, unknown>;
}

async function pythonProxy(code: string): Promise<Record<string, unknown>> {
  const result = await client.sendCommand("python_proxy", { code });
  return result as unknown as Record<string, unknown>;
}

function rotationMap(snapshot: Record<string, unknown>): Map<string, number[]> {
  const data = snapshot.data as Record<string, unknown>;
  const bones = data.bones as Array<Record<string, unknown>>;
  const result = new Map<string, number[]>();
  for (const bone of bones) {
    const rotation = bone.rotation as Record<string, number>;
    result.set(bone.bone as string, [rotation.x, rotation.y, rotation.z, rotation.w]);
  }
  return result;
}

function quatDistanceDegrees(left: number[], right: number[]): number {
  const leftLength = Math.hypot(left[0], left[1], left[2], left[3]);
  const rightLength = Math.hypot(right[0], right[1], right[2], right[3]);
  const rawDot =
    left[0] * right[0] + left[1] * right[1] +
    left[2] * right[2] + left[3] * right[3];
  const dot = Math.min(
    1,
    Math.abs(rawDot / (leftLength * rightLength))
  );
  return 2 * Math.acos(dot) * 180 / Math.PI;
}

async function run(): Promise<void> {
  if (!referencePath || !targetPath) {
    console.log("  SKIP  Set ANIM_POSE_REFERENCE and ANIM_POSE_TARGET to run.");
    return;
  }

  const snapshot = await callTool("anim_pose_snapshot", {
    sequence_path: referencePath,
    frame: 0,
  });
  assert(snapshot.success === true, `snapshot failed: ${snapshot.error}`);
  const snapshotData = snapshot.data as Record<string, unknown>;
  assert(
    snapshotData.coordinate_space === "local_parent_relative",
    "snapshot must report local parent-relative space"
  );

  const dryRunParams = {
    reference_path: referencePath,
    target_path: targetPath,
    dry_run: true,
    threshold_degrees: 0,
  };
  const dryRunA = await callTool("anim_reanchor", dryRunParams);
  const dryRunB = await callTool("anim_reanchor", dryRunParams);
  assert(dryRunA.success === true, `first dry run failed: ${dryRunA.error}`);
  assert(dryRunB.success === true, `second dry run failed: ${dryRunB.error}`);
  assert(
    JSON.stringify(dryRunA.data) === JSON.stringify(dryRunB.data),
    "dry-run output must be deterministic"
  );

  const throwawayPath = `/Game/MCPBridgeTests/AnimPoseReanchor_IT_${Date.now()}`;
  const duplicateCode = [
    "import unreal",
    `source = unreal.load_asset(${JSON.stringify(targetPath)})`,
    `duplicate = unreal.EditorAssetLibrary.duplicate_asset(${JSON.stringify(targetPath)}, ${JSON.stringify(throwawayPath)})`,
    "print(duplicate.get_path_name() if duplicate else '')",
  ].join("\n");
  const duplicate = await pythonProxy(duplicateCode);
  assert(duplicate.success === true, `duplicate failed: ${duplicate.error}`);

  try {
    const beforeAnchor = await callTool("anim_pose_delta", {
      reference_path: referencePath,
      target_path: throwawayPath,
      reference_frame: 0,
      target_frame: 0,
    });
    assert(beforeAnchor.success === true, `initial delta failed: ${beforeAnchor.error}`);

    const firstSnapshot = await callTool("anim_pose_snapshot", {
      sequence_path: throwawayPath,
      frame: 0,
    });
    assert(firstSnapshot.success === true, `target snapshot failed: ${firstSnapshot.error}`);
    const firstData = firstSnapshot.data as Record<string, unknown>;
    const numFrames = firstData.num_frames as number;
    const lastFrame = numFrames - 1;
    const tailBefore = await callTool("anim_pose_snapshot", {
      sequence_path: throwawayPath,
      frame: lastFrame,
    });
    assert(tailBefore.success === true, `tail snapshot failed: ${tailBefore.error}`);

    const mutation = await callTool("anim_reanchor", {
      reference_path: referencePath,
      target_path: throwawayPath,
      dry_run: false,
      profile: "decay",
      window_frames: Math.min(12, Math.max(1, numFrames - 2)),
      threshold_degrees: 0,
    });
    assert(mutation.success === true, `mutation failed: ${mutation.error}`);
    const mutationData = mutation.data as Record<string, unknown>;
    assert(mutationData.saved === true, "throwaway mutation must save");

    const afterAnchor = await callTool("anim_pose_delta", {
      reference_path: referencePath,
      target_path: throwawayPath,
      reference_frame: 0,
      target_frame: 0,
    });
    assert(afterAnchor.success === true, `post-mutation delta failed: ${afterAnchor.error}`);
    const afterData = afterAnchor.data as Record<string, unknown>;
    assert(
      (afterData.max_delta_degrees as number) <= 0.1,
      `anchor should match within 0.1 degrees, got ${afterData.max_delta_degrees}`
    );

    const tailAfter = await callTool("anim_pose_snapshot", {
      sequence_path: throwawayPath,
      frame: lastFrame,
    });
    assert(tailAfter.success === true, `post-mutation tail snapshot failed: ${tailAfter.error}`);
    const beforeRotations = rotationMap(tailBefore);
    const afterRotations = rotationMap(tailAfter);
    let maxTailChange = 0;
    for (const [bone, beforeRotation] of beforeRotations) {
      const afterRotation = afterRotations.get(bone);
      if (afterRotation) {
        maxTailChange = Math.max(
          maxTailChange,
          quatDistanceDegrees(beforeRotation, afterRotation)
        );
      }
    }
    assert(
      maxTailChange <= 0.1,
      `decay tail should remain unchanged, got ${maxTailChange} degrees`
    );
    const undoResult = await client.sendCommand("undo", { count: 1 });
    assert(
      undoResult.success === true,
      `undo failed: ${undoResult.error}`
    );
    const undoSnapshot = await callTool("anim_pose_snapshot", {
      sequence_path: throwawayPath,
      frame: 0,
    });
    assert(
      undoSnapshot.success === true,
      `post-undo snapshot failed: ${undoSnapshot.error}`
    );
    const originalRotations = rotationMap(firstSnapshot);
    const restoredRotations = rotationMap(undoSnapshot);
    let maxUndoChange = 0;
    for (const [bone, originalRotation] of originalRotations) {
      const restoredRotation = restoredRotations.get(bone);
      if (restoredRotation) {
        maxUndoChange = Math.max(
          maxUndoChange,
          quatDistanceDegrees(originalRotation, restoredRotation)
        );
      }
    }
    assert(
      maxUndoChange <= 0.1,
      `undo should restore frame 0, got ${maxUndoChange} degrees`
    );
    console.log(
      "  PASS  snapshot, dry run, throwaway mutation, decay tail, undo, and cleanup"
    );
  } finally {
    const cleanupCode = [
      "import unreal",
      `print(unreal.EditorAssetLibrary.delete_asset(${JSON.stringify(throwawayPath)}))`,
    ].join("\n");
    await pythonProxy(cleanupCode);
  }
}

run().catch((error) => {
  console.error(`  FAIL  ${(error as Error).message}`);
  process.exit(1);
});
