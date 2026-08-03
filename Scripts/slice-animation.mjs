#!/usr/bin/env node
// Vertical slice 4 of 7: ANIMATION.
//
// One prompt: "give the guard an Animation Blueprint that blends idle, walk and
// run off a Speed variable, and put it on the pawn's mesh."
//
// What a pass would prove: a state machine with real transitions can be
// authored against a real skeleton, read back, and attached to a
// SkeletalMeshComponent. Discovery of the skeleton and the animations is done
// with tools that exist, so a run against a live editor still reports whether
// the project even has assets to build this from.
//
//   node Scripts/slice-animation.mjs
//   node Scripts/slice-animation.mjs --phase=cold

import { runSlice } from "./slice-harness.mjs";

const ABP = "/Game/MCPGenerated/ABP_SliceGuard";
const PAWN = "/Game/MCPGenerated/BP_SliceAnimPawn";

await runSlice({
  id: "animation",
  title: "an Animation Blueprint with a state machine and transitions",
  proves: "author an AnimBP against a real skeleton, read the state machine back, and drive it from a pawn",
}, async (h) => {
  // Discovery first, with tools that are registered today. A slice that
  // hard-codes a skeleton path reports a fixture problem as a platform problem.
  let skeleton = null;
  let animations = [];
  const skeletons = await h.call("puerts_find_assets", { type: "Skeleton", limit: 10 }, {
    label: "1. find a Skeleton in the project to build against",
  });
  if (skeletons?.success === true) {
    const list = skeletons.data?.assets ?? [];
    skeleton = list[0]?.object_path ?? list[0]?.path ?? list[0]?.asset_path ?? null;
    h.check(skeleton !== null,
      "1. the project contains at least one Skeleton", `${list.length} found`);
  }
  const anims = await h.call("puerts_find_assets", { type: "AnimSequence", limit: 20 }, {
    label: "2. find AnimSequences the state machine can play",
  });
  if (anims?.success === true) {
    animations = (anims.data?.assets ?? []).map((a) => a.object_path ?? a.path ?? a.asset_path);
    h.check(animations.length >= 3,
      "2. at least three AnimSequences exist to blend between", `${animations.length} found`);
  }

  const [idle, walk, run] = [animations[0], animations[1], animations[2]];

  // Lane J owns these names and confirms both are uncompiled C++ today.
  const built = await h.call("puerts_anim_blueprint_build", {
    asset_path: ABP,
    skeleton_path: skeleton ?? "<no Skeleton found by step 1>",
    variables: [{ name: "Speed", type: "float", default: 0 }],
    states: [
      { name: "Idle", animation: idle, loop: true },
      { name: "Walk", animation: walk, loop: true },
      { name: "Run", animation: run, loop: true },
    ],
    transitions: [
      { from: "Idle", to: "Walk", condition: { variable: "Speed", op: "greater", value: 10 } },
      { from: "Walk", to: "Run", condition: { variable: "Speed", op: "greater", value: 300 } },
      { from: "Run", to: "Walk", condition: { variable: "Speed", op: "less_equal", value: 300 } },
      { from: "Walk", to: "Idle", condition: { variable: "Speed", op: "less_equal", value: 10 } },
    ],
    save: true,
  }, {
    label: "3. build the state machine from one spec",
    why: "an Animation Blueprint with states and transitions is the whole animation slice; "
      + "UAnimBlueprintBuilderLibrary::BuildAnimBlueprintFromJSON exists in C++ and anim_blueprint_build_from_json "
      + "exists only behind the legacy HTTP opt-in, so there is no registered path to it",
    legacy: ["anim_blueprint_build_from_json"],
    request: {
      tool: "puerts_anim_blueprint_build",
      owner: "lane J (settled, C++ uncompiled)",
      params: { asset_path: "string", skeleton_path: "string", variables: "array", anim_graph: "object", states: "array", transitions: "array", save: "boolean" },
      note: "CREATE ONLY by design: rerunning against an existing asset is a refusal, not a no-op",
    },
  });
  if (built?.success === true) {
    h.check(built.data?.compile_status === "UpToDate",
      "3. the Animation Blueprint compiled clean", String(built.data?.compile_status));
  }

  const read = await h.call("puerts_anim_blueprint_inspect", { asset_path: ABP }, {
    label: "4. read the state machine back with the independent inspector",
    why: "no inspector exists for Animation Blueprints today, which breaks the builder-plus-inspector rule "
      + "every other builder in this bridge follows",
    request: {
      tool: "puerts_anim_blueprint_inspect",
      owner: "lane J (settled, ships first, C++ uncompiled)",
      params: { asset_path: "string" },
      returns: "{ skeleton_path, variables, state_machines: [{name, states: [{name, animation, loop}], transitions: [{from, to, condition}]}], structure_hash_sha1, package_dirty_before, package_dirty_after }",
    },
  });
  let hash = null;
  if (read?.success === true) {
    hash = read.data?.structure_hash_sha1 ?? null;
    const states = (read.data?.state_machines ?? []).flatMap((m) => (m.states ?? []).map((s) => s.name));
    h.check(["Idle", "Walk", "Run"].every((s) => states.includes(s)),
      "4. all three states are in the asset", JSON.stringify(states));
    const transitions = (read.data?.state_machines ?? []).flatMap((m) => m.transitions ?? []);
    h.check(transitions.length >= 4, "4. all four transitions are in the asset", `${transitions.length} found`);
  }

  // Lane J is explicit that there will be no patch primitive, so a second prompt
  // that adjusts one transition has to delete and rebuild the whole asset. Every
  // other builder here has an incremental partner; this one will not.
  h.request("puerts_anim_blueprint_patch",
    "change one state, transition or variable on an existing Animation Blueprint. "
    + "puerts_anim_blueprint_build is create-only and refuses an existing asset, and lane J will not ship a patch "
    + "because the UAnimBlueprintBuilderLibrary rebuild path appends instead of replacing and cannot be made "
    + "failure-atomic yet. Until it exists, an animation slice is single-shot: the second prompt about the same "
    + "asset has nowhere to go",
    {
      tool: "puerts_anim_blueprint_patch",
      blocked_by: "UAnimBlueprintBuilderLibrary rebuild appends rather than replaces; needs the same failure-atomic "
        + "treatment BPMutatorHelpers is getting",
      params: {
        asset_path: "string",
        operations: "[{op:'add_state'|'remove_state'|'set_transition_condition'|'set_state_animation', ...}]",
        plan_only: "boolean", verify: "boolean", save: "boolean",
      },
    });

  // Attaching the AnimBP to a mesh is a component template property, which the
  // Blueprint builder already writes. This step is authorable today and is here
  // to prove the two halves compose once the AnimBP exists.
  const animClass = built?.data?.generated_class_path ?? `${ABP}.${ABP.split("/").pop()}_C`;
  await h.call("puerts_blueprint_build", {
    asset_path: PAWN,
    parent_class: "Actor",
    components: [{
      class: "SkeletalMeshComponent",
      name: "Mesh",
      properties: { AnimClass: animClass },
    }],
  }, {
    label: "5. attach the Animation Blueprint to a pawn's SkeletalMeshComponent",
    why: "AnimClass is a component template property, so this is the one step of the slice with a registered path",
  });

  h.policy("6. see the state machine blend",
    "an Animation Blueprint only evaluates in a running game or in the asset editor's preview viewport. "
    + "PIE is user-gated, and puerts_viewport_screenshot captures the level viewport, not an asset editor preview. "
    + "The legacy anim_pose_snapshot / anim_pose_delta pair is the closest thing to a pose read-back and is "
    + "behind the HTTP opt-in.");

  if (hash !== null) h.seal({ asset_path: ABP, inspect_tool: "puerts_anim_blueprint_inspect", structure_hash: hash });
});
