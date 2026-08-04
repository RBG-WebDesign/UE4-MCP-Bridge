#!/usr/bin/env node
// Feature acceptance 12: AI patrol, chase, search and perception.

import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const BT = `/Game/MCPGenerated/FeatureAcceptance/BT_Guard_${runId}`;
const BB = `/Game/MCPGenerated/FeatureAcceptance/BB_Guard_${runId}`;
const CTRL = `/Game/MCPGenerated/FeatureAcceptance/BP_GuardAI_${runId}`;
const PAWN = `/Game/MCPGenerated/FeatureAcceptance/BP_GuardPawn_${runId}`;
const BT_OBJECT = `${BT}.BT_Guard_${runId}`;
const CTRL_CLASS = `${CTRL}.BP_GuardAI_${runId}_C`;
const PAWN_CLASS = `${PAWN}.BP_GuardPawn_${runId}_C`;
const PAWN_LABEL = `FeatureGuard_${runId}`;

const CONTROL = {
  asset_path: BT, blackboard_path: BB, keys: [{ name: "PatrolPoint", type: "Vector" }],
  root: { id: "root", type: "Sequence", name: "Control", children: [{ id: "wait", type: "Wait", params: { wait_time: "0.1" } }] },
};
const TREE = {
  asset_path: BT, blackboard_path: BB,
  keys: [
    { name: "TargetActor", type: "Object", base_class: "/Script/Engine.Actor" },
    { name: "PatrolPoint", type: "Vector" }, { name: "LastKnownLocation", type: "Vector" },
    { name: "IsSearching", type: "Bool" },
  ],
  root: { id: "root", type: "Selector", name: "GuardRoot", children: [
    { id: "chase", type: "Sequence", name: "Chase", decorators: [
      { id: "hasTarget", type: "Blackboard", params: { blackboard_key: "TargetActor" } },
    ], children: [
      { id: "moveTarget", type: "MoveTo", params: { blackboard_key: "TargetActor", acceptable_radius: "120.0" } },
      { id: "chaseWait", type: "Wait", params: { wait_time: "0.25" } },
    ] },
    { id: "search", type: "Sequence", name: "Search", decorators: [
      { id: "isSearching", type: "Blackboard", params: { blackboard_key: "IsSearching" } },
    ], children: [
      { id: "moveLast", type: "MoveTo", params: { blackboard_key: "LastKnownLocation", acceptable_radius: "90.0" } },
      { id: "searchWait", type: "Wait", params: { wait_time: "3.0" } },
    ] },
    { id: "patrol", type: "Sequence", name: "Patrol", children: [
      { id: "movePatrol", type: "MoveTo", params: { blackboard_key: "PatrolPoint", acceptable_radius: "80.0" } },
      { id: "patrolWait", type: "Wait", params: { wait_time: "2.0" } },
    ] },
  ] },
};
const CTRL_SPEC = { asset_path: CTRL, parent_class: "AIController", graph: {
  nodes: [
    { id: "begin", type: "BeginPlay", x: 0, y: 0 },
    { id: "run", type: "CallFunction", x: 340, y: 0, params: { class: "AIController", function: "RunBehaviorTree", BTAsset: BT_OBJECT } },
    { id: "marker", type: "PrintString", x: 720, y: 0, params: { InString: `FEATURE_GUARD_AI_RUNNING_${runId}` } },
  ], connections: [{ from: "begin.then", to: "run.exec" }, { from: "run.then", to: "marker.exec" }],
} };
const PERCEPTION = {
  asset_path: CTRL,
  senses: [
    { sense: "Sight", SightRadius: 1800, LoseSightRadius: 2200, PeripheralVisionAngleDegrees: 70, MaxAge: 5,
      DetectionByAffiliation: { bDetectEnemies: true, bDetectNeutrals: true, bDetectFriendlies: false } },
    { sense: "Hearing", HearingRange: 1400, MaxAge: 4,
      DetectionByAffiliation: { bDetectEnemies: true, bDetectNeutrals: true, bDetectFriendlies: false } },
  ],
  dominant_sense: "Sight", remove_unlisted: true,
};
const PAWN_SPEC = { asset_path: PAWN, parent_class: "Character", variables: [
  { name: "PatrolRadius", type: "float", default: 800, category: "AI" },
] };

await runSlice({
  id: "feature-ai-behavior",
  title: "a guard with patrol, chase, search, sight and hearing",
  proves: "author and independently inspect the complete tree, controller wiring and perception config, converge, cold-load, and optionally observe the possessed guard",
}, async (h) => {
  if (!await h.assertFreshFixture(BT, "puerts_behavior_tree_inspect")
    || !await h.assertFreshFixture(CTRL, "puerts_ai_controller_inspect")
    || !await h.assertFreshFixture(PAWN, "puerts_graph_inspect")) return;

  const controlBuild = await h.call("puerts_behavior_tree_build", CONTROL, { label: "1. build the one-task AI control" });
  if (controlBuild?.success !== true) return;
  const control = await h.call("puerts_behavior_tree_inspect", { asset_path: BT }, { label: "1. inspect AI control" });
  const controlHash = control?.data?.structure_hash_sha1 ?? null;
  const treeBuild = await h.call("puerts_behavior_tree_build", TREE, { label: "2. replace control with patrol, chase and search tree" });
  if (treeBuild?.success !== true) return;
  const treeRead = await h.call("puerts_behavior_tree_inspect", { asset_path: BT }, { label: "3. inspect AI tree independently" });
  const treeHash = treeRead?.data?.structure_hash_sha1 ?? null;
  const treeJson = JSON.stringify(treeRead?.data ?? {});
  h.check(typeof treeHash === "string" && treeHash !== controlHash, "3. the control moved to complete AI behavior", `${controlHash} -> ${treeHash}`);
  h.check(["TargetActor", "PatrolPoint", "LastKnownLocation", "IsSearching"].every((key) => treeJson.includes(key)),
    "3. all behavior keys read back");
  h.check(["Chase", "Search", "Patrol"].every((branch) => treeJson.includes(branch)), "3. all behavior branches read back");

  const controllerBuild = await h.call("puerts_blueprint_build", CTRL_SPEC, { label: "4. build the AIController tree runner" });
  if (controllerBuild?.success !== true) return;
  const perception = await h.call("puerts_ai_perception_build", PERCEPTION, { label: "5. add sight and hearing desired state" });
  if (perception?.success !== true) return;
  const controllerRead = await h.call("puerts_ai_controller_inspect", { asset_path: CTRL }, { label: "6. inspect controller wiring and perception independently" });
  const controllerJson = JSON.stringify(controllerRead?.data ?? {});
  h.check(controllerJson.includes("Sight") && controllerJson.includes("Hearing"), "6. sight and hearing read back independently");
  h.check(controllerJson.includes(`BT_Guard_${runId}`), "6. the controller call site names the generated tree");
  h.check(perception?.data?.saved === true || perception?.data?.applied_change_count > 0, "5. perception moved controller state");

  const pawnBuild = await h.call("puerts_blueprint_build", PAWN_SPEC, { label: "7. build the guard pawn" });
  if (pawnBuild?.success !== true) return;
  const defaults = await h.call("puerts_class_defaults_patch", {
    asset_path: PAWN, properties: { AIControllerClass: CTRL_CLASS, AutoPossessAI: "PlacedInWorldOrSpawned" },
  }, { label: "7. wire guard pawn class defaults" });
  if (defaults?.data?.class_default_object) {
    const back = await h.call("puerts_read_property", { object_path: defaults.data.class_default_object, property: "AIControllerClass" },
      { label: "7. read the controller class independently from the CDO" });
    h.check(JSON.stringify(back?.data?.value ?? "").includes(`BP_GuardAI_${runId}`), "7. the pawn names the generated controller");
  }

  const treeSha = h.fileSha(BT);
  const ctrlSha = h.fileSha(CTRL);
  await h.call("puerts_behavior_tree_build", TREE, { label: "8. rerun AI tree" });
  const perceptionAgain = await h.call("puerts_ai_perception_build", PERCEPTION, { label: "8. rerun perception config" });
  await h.call("puerts_blueprint_build", CTRL_SPEC, { label: "8. rerun controller graph" });
  const treeAgain = await h.call("puerts_behavior_tree_inspect", { asset_path: BT }, { label: "8. inspect tree rerun" });
  const ctrlAgain = await h.call("puerts_ai_controller_inspect", { asset_path: CTRL }, { label: "8. inspect controller rerun" });
  h.check(treeAgain?.data?.structure_hash_sha1 === treeHash, "8. AI tree converged");
  h.check(perceptionAgain?.data?.applied_change_count === 0, "8. perception rerun applied no changes", String(perceptionAgain?.data?.applied_change_count));
  h.check(JSON.stringify(ctrlAgain?.data ?? {}).includes(`BT_Guard_${runId}`), "8. controller wiring survived the rerun");
  h.check(treeSha === null || h.fileSha(BT) === treeSha, "8. tree rerun wrote no new bytes");
  h.check(ctrlSha === null || h.fileSha(CTRL) === ctrlSha, "8. controller rerun wrote no new bytes");

  if (!includePie) {
    h.policy("9. observe patrol, chase and search", "PIE is off by default. Re-run with --pie only after explicit user authorization; navigation must already be built.");
  } else {
    const placed = await h.call("puerts_scene_batch", { operations: [{ op: "upsert_actor", label: PAWN_LABEL, class: PAWN_CLASS, location: { x: 0, y: 0, z: 150 } }] }, { label: "9. place guard pawn" });
    let started = false;
    try {
      if (placed?.success === true) started = (await h.call("puerts_pie_start", {}, { label: "9. start explicitly authorized PIE" }))?.success === true;
      if (started) {
        await new Promise((resolve) => setTimeout(resolve, 1500));
        const observed = await h.call("puerts_pie_agent_query", { op: "observe", class_filter: `BP_GuardPawn_${runId}_C`, log_lines: 100 }, { label: "9. observe possessed guard" });
        h.check(JSON.stringify(observed?.data ?? {}).includes(`BP_GuardPawn_${runId}`), "9. runtime contains the generated guard");
      }
    } finally { if (started) await h.call("puerts_pie_stop", {}, { label: "9. stop explicitly authorized PIE" }); }
  }

  h.seal({ asset_path: BT, inspect_tool: "puerts_behavior_tree_inspect", structure_hash: treeHash });
  h.seal({ asset_path: CTRL, inspect_tool: "puerts_ai_controller_inspect" });
  h.seal({ asset_path: PAWN, inspect_tool: "puerts_graph_inspect" });
});
