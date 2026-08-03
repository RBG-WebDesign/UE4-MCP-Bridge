#!/usr/bin/env node
// Vertical slice 3 of 7: AI.
//
// One prompt: "give me a guard that patrols, chases whatever is in its
// TargetActor blackboard key, and prove it is really wired up."
//
// What a pass would prove: a Behavior Tree, its Blackboard, an AIController
// that runs the tree, a pawn that uses that controller, and navigation the
// MoveTo tasks can actually path on. The tree and blackboard halves are
// live_verified today. The steps after them are where the slice stops.
//
//   node Scripts/slice-ai.mjs
//   node Scripts/slice-ai.mjs --phase=cold

import { runSlice } from "./slice-harness.mjs";

const BT = "/Game/MCPGenerated/BT_SliceGuard";
const BB = "/Game/MCPGenerated/BB_SliceGuard";
const CTRL = "/Game/MCPGenerated/BP_SliceGuardAI";
const PAWN = "/Game/MCPGenerated/BP_SliceGuardPawn";

const TREE = {
  asset_path: BT,
  blackboard_path: BB,
  keys: [
    { name: "TargetActor", type: "Object", base_class: "/Script/Engine.Actor" },
    { name: "PatrolPoint", type: "Vector" },
    { name: "IsAlert", type: "Bool" },
  ],
  root: {
    id: "root", type: "Selector", name: "GuardRoot",
    children: [
      {
        id: "chase", type: "Sequence", name: "Chase",
        decorators: [
          { id: "hasTarget", type: "Blackboard", name: "HasTarget", params: { blackboard_key: "TargetActor" } },
        ],
        children: [
          { id: "moveTarget", type: "MoveTo", name: "MoveToTarget", params: { blackboard_key: "TargetActor", acceptable_radius: "120.0" } },
          { id: "settle", type: "Wait", name: "Settle", params: { wait_time: "0.5", random_deviation: "0.2" } },
        ],
      },
      {
        id: "patrol", type: "Sequence", name: "Patrol",
        children: [
          { id: "movePatrol", type: "MoveTo", name: "MoveToPatrolPoint", params: { blackboard_key: "PatrolPoint", acceptable_radius: "80.0" } },
          { id: "pause", type: "Wait", name: "PauseAtPoint", params: { wait_time: "2.0" } },
        ],
      },
    ],
  },
};

await runSlice({
  id: "ai",
  title: "a Behavior Tree plus Blackboard driving an AIController on a pawn",
  proves: "author the tree, the blackboard, the controller wiring and the pawn, then prove the wiring by reading it back",
}, async (h) => {
  const built = await h.call("puerts_behavior_tree_build", TREE, {
    label: "1. build the guard tree and its blackboard in one call",
    why: "the AI slice starts as one desired-state Behavior Tree build",
  });
  if (built?.success === true) {
    const keys = (built.data?.blackboard_keys ?? built.data?.keys ?? []).map((k) => k.name ?? k);
    h.check(["TargetActor", "PatrolPoint", "IsAlert"].every((k) => keys.includes(k)),
      "1. the blackboard carries all three keys, read back off the asset", JSON.stringify(keys));
  }

  const read = await h.call("puerts_behavior_tree_inspect", { asset_path: BT }, {
    label: "2. read the tree back with the independent inspector",
  });
  let hash = null;
  if (read?.success === true) {
    hash = read.data?.structure_hash_sha1 ?? null;
    h.check((read.data?.counts?.composite ?? read.data?.composite_count ?? 0) >= 3,
      "2. all three composites are in the asset",
      JSON.stringify(read.data?.counts ?? {}));
    h.check((read.data?.counts?.task ?? read.data?.task_count ?? 0) >= 4,
      "2. all four tasks are in the asset");
    h.check(read.data?.blackboard_path?.includes("BB_SliceGuard") === true,
      "2. the tree names the blackboard the spec asked for", String(read.data?.blackboard_path));
    h.check(typeof hash === "string" && hash.length === 40,
      "2. the inspector reports a structure hash", String(hash));
    h.check(read.data?.package_dirty_after === false
      || read.data?.package_dirty_after === read.data?.package_dirty_before,
      "2. inspecting did not dirty the package");
  }

  const rerun = await h.call("puerts_behavior_tree_build", TREE, {
    label: "3. rerunning the identical spec converges",
  });
  if (rerun?.success === true) {
    const again = await h.call("puerts_behavior_tree_inspect", { asset_path: BT }, { label: "3. re-read after the rerun" });
    h.check(again?.data?.structure_hash_sha1 === hash,
      "3. the structure hash is unmoved by an identical rebuild",
      `${again?.data?.structure_hash_sha1} vs ${hash}`);
  }

  // AAIController::RunBehaviorTree is a plain graph call, so the controller is
  // authorable with the Blueprint builder that already exists. Lane K confirmed
  // nothing in the AI lane authors this; blueprint_build is the path.
  const ctrl = await h.call("puerts_blueprint_build", {
    asset_path: CTRL,
    parent_class: "AIController",
    graph: {
      nodes: [
        { id: "begin", type: "BeginPlay", x: 0, y: 0 },
        {
          id: "run", type: "CallFunction", x: 360, y: 0,
          params: { class: "AIController", function: "RunBehaviorTree", BTAsset: `${BT}.BT_SliceGuard` },
        },
        { id: "say", type: "PrintString", x: 760, y: 0, params: { InString: "guard AI running its tree" } },
      ],
      connections: [
        { from: "begin.then", to: "run.exec" },
        { from: "run.then", to: "say.exec" },
      ],
    },
  }, {
    label: "4. author the AIController that runs the tree",
    why: "the tree does nothing until a controller runs it",
  });
  if (ctrl?.success === true) {
    h.check((ctrl.data?.graph?.unresolved_connections ?? []).length === 0,
      "4. the begin-and-run chain resolved to real pins",
      JSON.stringify(ctrl.data?.graph?.unresolved_connections ?? []).slice(0, 300));
    const back = await h.call("puerts_graph_inspect", { asset_path: CTRL, include_pins: true }, {
      label: "5. prove the wiring by reading the BTAsset pin default back off the controller",
      why: "the claim under test is that the controller really names this tree, not that the build said so",
    });
    const nodes = back?.data?.graph?.nodes ?? [];
    const wired = nodes.some((n) => JSON.stringify(n.params ?? {}).includes("BT_SliceGuard"));
    h.check(wired, "5. a RunBehaviorTree node in the controller names BT_SliceGuard",
      wired ? "" : "the pin default did not survive the build, which is a builder defect, not a missing tool");
    h.note("5. a purpose-built read exists on paper",
      "lane K's puerts_ai_controller_inspect reports RunBehaviorTree call sites and perception senses directly. "
      + "It is not registered yet; graph_inspect covers the same claim today, so this slice does not block on it.");
  }

  // The pawn. AIControllerClass and AutoPossessAI are class defaults on the
  // actor CDO, not component template properties, and no primitive writes them:
  // blueprint_build's schema has components, variables and a graph and nothing
  // for the CDO. Without this the controller never possesses anything.
  await h.call("puerts_blueprint_build", {
    asset_path: PAWN,
    parent_class: "Character",
    variables: [{ name: "PatrolRadius", type: "float", default: 800 }],
  }, { label: "6. author the pawn the controller will possess" });

  h.request("puerts_class_defaults_patch",
    "set class-default (CDO) properties on a generated Blueprint. The AI slice needs AIControllerClass and "
    + "AutoPossessAI on the pawn; puerts_blueprint_build writes component template properties and member variable "
    + "defaults, and has no section for the actor's own class defaults, so an authored pawn can never be possessed "
    + "by an authored controller",
    {
      tool: "puerts_class_defaults_patch",
      alternative: "add a `defaults: {PropertyName: value}` section to puerts_blueprint_build",
      params: {
        asset_path: "/Game/MCPGenerated/...",
        properties: "{PropertyName: reflected value} applied to the generated class default object",
        plan_only: "boolean",
        verify: "boolean",
        save: "boolean",
      },
    });

  // Navigation. MoveTo needs a NavMeshBoundsVolume in the level and a built
  // navmesh. Lane K ships nav_inspect and nav_query, both read-only, and lane L
  // places the volume; nothing builds the navmesh.
  h.need("puerts_scene_batch", {
    why: "place a NavMeshBoundsVolume and the guard pawn in the level; puerts_spawn_actor cannot set a volume's brush extent or label the actor",
    request: { tool: "puerts_scene_batch", owner: "lane L (settled, implemented_unverified)" },
  });
  h.request("puerts_nav_build",
    "rebuild navigation so MoveTo can path. The legacy catalog has ai_nav_rebuild behind the HTTP opt-in; "
    + "lane K's puerts_nav_inspect and puerts_nav_query are read-only by design, so no registered tool builds a navmesh",
    {
      tool: "puerts_nav_build",
      params: { wait_for_completion: "boolean (default true)", timeout_ms: "number" },
      returns: "{ built: boolean, navmesh_tile_count: number, bounds_volume_count: number, elapsed_ms: number }",
    });

  h.policy("7. watch the guard patrol",
    "a Behavior Tree only ticks in a running game. PIE is reserved for the user to ask for, and the only runtime "
    + "observation surface is the pie_agent_* family, which is legacy-only and also user-gated.");

  h.seal({ asset_path: BT, inspect_tool: "puerts_behavior_tree_inspect", structure_hash: hash });
});
