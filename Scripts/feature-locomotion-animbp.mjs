#!/usr/bin/env node
// Feature acceptance 13: locomotion Animation Blueprint.

import { runSlice } from "./slice-harness.mjs";

const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const ABP = `/Game/MCPGenerated/FeatureAcceptance/ABP_Locomotion_${runId}`;

function spec(skeleton, animations, complete) {
  const states = [
    { id: "idle", name: "Idle", animation: animations[0], looping: true, is_entry: true },
    { id: "walk", name: "Walk", animation: animations[1], looping: true },
  ];
  const transitions = [
    { from: "idle", to: "walk", condition: { type: "bool_variable", variable: "IsMoving", value: true } },
    { from: "walk", to: "idle", condition: { type: "bool_variable", variable: "IsMoving", value: false } },
  ];
  if (complete) {
    states.push({ id: "run", name: "Run", animation: animations[2], looping: true });
    transitions.push(
      { from: "walk", to: "run", condition: { type: "bool_variable", variable: "IsSprinting", value: true } },
      { from: "run", to: "walk", condition: { type: "bool_variable", variable: "IsSprinting", value: false } },
    );
  }
  return {
    asset_path: ABP,
    skeleton_path: skeleton,
    variables: [
      { name: "IsMoving", type: "bool", default: "false" },
      { name: "IsSprinting", type: "bool", default: "false" },
    ],
    anim_graph: { pipeline: [{ id: "locomotion", type: "StateMachine", name: "Locomotion" }] },
    state_machine: { states, transitions },
    save: true,
  };
}

await runSlice({
  id: "feature-locomotion-animbp",
  title: "an idle, walk and run locomotion Animation Blueprint",
  proves: "build, patch and independently inspect locomotion desired state, then seal it for a cold read",
}, async (h) => {
  if (!await h.assertFreshFixture(ABP, "puerts_anim_blueprint_inspect")) return;

  const skeletons = await h.call("puerts_find_assets", { type: "Skeleton", limit: 10 }, { label: "1. find a project Skeleton" });
  const animations = await h.call("puerts_find_assets", { type: "AnimSequence", limit: 20 }, { label: "1. find locomotion AnimSequences" });
  const skeleton = (skeletons?.data?.assets ?? [])[0]?.object_path
    ?? (skeletons?.data?.assets ?? [])[0]?.asset_path ?? null;
  const clips = (animations?.data?.assets ?? []).map((asset) => asset.object_path ?? asset.asset_path ?? asset.path);
  h.check(skeleton !== null, "1. a Skeleton is available", String(skeleton));
  h.check(clips.length >= 3, "1. idle, walk and run source clips are available", `${clips.length} found`);
  if (skeleton === null || clips.length < 3) return;

  const controlBuild = await h.call("puerts_anim_blueprint_build", spec(skeleton, clips, false), {
    label: "2. build the two-state locomotion control",
  });
  if (controlBuild?.success !== true) return;
  const control = await h.call("puerts_anim_blueprint_inspect", { asset_path: ABP }, { label: "2. inspect the control independently" });
  const controlHash = control?.data?.structure_hash_sha1 ?? null;

  const patched = await h.call("puerts_anim_blueprint_patch", spec(skeleton, clips, true), {
    label: "3. replace the control with idle, walk and run locomotion",
  });
  if (patched?.success !== true) return;
  const read = await h.call("puerts_anim_blueprint_inspect", { asset_path: ABP }, { label: "4. inspect complete locomotion independently" });
  const hash = read?.data?.structure_hash_sha1 ?? null;
  const machines = read?.data?.state_machines ?? [];
  const states = machines.flatMap((machine) => (machine.states ?? []).map((state) => state.name));
  const transitions = machines.flatMap((machine) => machine.transitions ?? []);
  h.check(patched?.data?.compile_status === "UpToDate", "3. the patched Animation Blueprint compiles", String(patched?.data?.compile_status));
  h.check(typeof hash === "string" && hash !== controlHash, "4. the writer moved the two-state control", `${controlHash} -> ${hash}`);
  h.check(["Idle", "Walk", "Run"].every((name) => states.includes(name)), "4. all locomotion states read back", JSON.stringify(states));
  h.check(transitions.length >= 4, "4. all locomotion transitions read back", `${transitions.length} found`);

  const again = await h.call("puerts_anim_blueprint_patch", spec(skeleton, clips, true), { label: "5. rerun locomotion desired state" });
  if (again?.success !== true) return;
  const reread = await h.call("puerts_anim_blueprint_inspect", { asset_path: ABP }, { label: "5. inspect the rerun" });
  const rerunStates = (reread?.data?.state_machines ?? []).flatMap((machine) => (machine.states ?? []).map((state) => state.name));
  h.check((again?.data?.verification?.actual_states ?? []).length === 3, "5. rerun replaced rather than appended states");
  h.check(rerunStates.filter((name) => ["Idle", "Walk", "Run"].includes(name)).length === 3, "5. semantic locomotion state converged", JSON.stringify(rerunStates));

  h.policy("6. observe live locomotion blending", "Runtime animation evaluation needs a compatible skeletal pawn and user-authorized PIE; authoring proof does not start PIE.");
  h.seal({ asset_path: ABP, inspect_tool: "puerts_anim_blueprint_inspect", structure_hash: reread?.data?.structure_hash_sha1 ?? hash });
});
