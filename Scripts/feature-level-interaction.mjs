#!/usr/bin/env node
// Feature acceptance 16: level interaction.

import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const INTERACTOR = `/Game/MCPGenerated/FeatureAcceptance/BP_LevelInteraction_${runId}`;
const INTERACTOR_CLASS = `${INTERACTOR}.BP_LevelInteraction_${runId}_C`;
const LABEL = `FeatureLevelInteraction_${runId}`;
const MARKER = `FEATURE_LEVEL_INTERACTION_${runId}`;
const CONTROL = {
  asset_path: INTERACTOR,
  parent_class: "Actor",
  variables: [{ name: "InteractionCount", type: "int", default: 0, category: "Interaction" }],
  graph: { nodes: [
    { id: "begin", type: "BeginPlay", x: 0, y: 0 },
    { id: "control", type: "PrintString", x: 320, y: 0, params: { InString: `FEATURE_LEVEL_CONTROL_${runId}` } },
  ], connections: [{ from: "begin.then", to: "control.exec" }] },
};
const SPEC = {
  asset_path: INTERACTOR,
  parent_class: "Actor",
  variables: [{ name: "InteractionCount", type: "int", default: 0, category: "Interaction" }],
  components: [{
    class: "BoxComponent",
    name: "InteractionVolume",
    properties: { BoxExtent: { x: 180, y: 180, z: 120 }, GenerateOverlapEvents: true },
  }],
  graph: { nodes: [
    { id: "overlap", type: "Event", x: 0, y: 0, params: { parent_class: "Actor", event_name: "ReceiveActorBeginOverlap" } },
    { id: "getCount", type: "VariableGet", x: 240, y: 220, params: { var_name: "InteractionCount" } },
    { id: "increment", type: "Operator", x: 480, y: 220, params: { op: "add_int", B: 1 } },
    { id: "setCount", type: "VariableSet", x: 560, y: 0, params: { var_name: "InteractionCount" } },
    { id: "marker", type: "PrintString", x: 860, y: 0, params: { InString: MARKER } },
  ], connections: [
    { from: "overlap.then", to: "setCount.exec" },
    { from: "getCount.InteractionCount", to: "increment.A" },
    { from: "increment.ReturnValue", to: "setCount.InteractionCount" },
    { from: "setCount.then", to: "marker.exec" },
  ] },
};
const SCENE = {
  operations: [{
    op: "upsert_actor",
    label: LABEL,
    class: INTERACTOR_CLASS,
    location: { x: 800, y: 0, z: 120 },
    folder: "FeatureAcceptance/Interaction",
  }],
  verify: true,
};

await runSlice({
  id: "feature-level-interaction",
  title: "a placed overlap interaction that moves persistent actor state",
  proves: "author the interaction, place it through level desired state, inspect both halves, converge, cold-load and optionally trigger it",
}, async (h) => {
  if (!await h.assertFreshFixture(INTERACTOR, "puerts_graph_inspect")) return;

  const controlBuild = await h.call("puerts_blueprint_build", CONTROL, { label: "1. build the non-interactive control" });
  if (controlBuild?.success !== true) return;
  const control = await h.call("puerts_graph_inspect", { asset_path: INTERACTOR, include_pins: true }, { label: "1. inspect the control" });
  const controlHash = control?.data?.graph?.structure_hash_sha1 ?? null;

  const built = await h.call("puerts_blueprint_build", SPEC, { label: "2. replace control with overlap interaction" });
  if (built?.success !== true) return;
  const read = await h.call("puerts_graph_inspect", { asset_path: INTERACTOR, include_pins: true }, { label: "3. inspect interaction independently" });
  const hash = read?.data?.graph?.structure_hash_sha1 ?? null;
  const graphJson = JSON.stringify(read?.data ?? {});
  h.check(built?.data?.compile_status === "UpToDate", "2. the interaction Blueprint compiles", String(built?.data?.compile_status));
  h.check(typeof hash === "string" && hash !== controlHash, "3. the writer moved the control", `${controlHash} -> ${hash}`);
  h.check(graphJson.includes("InteractionVolume") && graphJson.includes("ReceiveActorBeginOverlap"), "3. volume and overlap event read back");
  h.check(graphJson.includes("InteractionCount") && graphJson.includes(MARKER), "3. state movement and marker read back");

  const placed = await h.call("puerts_scene_batch", SCENE, { label: "4. place the interaction in the level" });
  if (placed?.success !== true) return;
  h.check(placed?.data?.applied_operation_count === 1, "4. scene control moved by one actor", String(placed?.data?.applied_operation_count));
  const scene = await h.call("puerts_scene_inspect", { actors: [LABEL], include_components: true }, { label: "5. inspect level placement independently" });
  const actor = (scene?.data?.actors ?? []).find((entry) => entry.label === LABEL);
  h.check(actor?.class?.includes(`BP_LevelInteraction_${runId}`) === true, "5. the generated interaction class is placed", String(actor?.class));
  h.check(actor?.folder === "FeatureAcceptance/Interaction", "5. the actor reads back in its intended folder", String(actor?.folder));

  const sha = h.fileSha(INTERACTOR);
  const rebuilt = await h.call("puerts_blueprint_build", SPEC, { label: "6. rerun interaction desired state" });
  if (rebuilt?.success !== true) return;
  const placedAgain = await h.call("puerts_scene_batch", SCENE, { label: "6. rerun level desired state" });
  if (placedAgain?.success !== true) return;
  const again = await h.call("puerts_graph_inspect", { asset_path: INTERACTOR }, { label: "6. inspect Blueprint rerun" });
  h.check(again?.data?.graph?.structure_hash_sha1 === hash, "6. interaction graph converged");
  h.check(placedAgain?.data?.converged === true || placedAgain?.data?.applied_operation_count === 0, "6. level placement converged");
  h.check(sha === null || h.fileSha(INTERACTOR) === sha, "6. Blueprint rerun wrote no new bytes");
  const saved = await h.call("puerts_save", {}, { label: "7. save the level fixture" });
  if (saved?.success !== true) return;

  if (!includePie) {
    h.policy("8. trigger the overlap", "PIE is off by default. Re-run with --pie only after explicit user authorization.");
  } else {
    let started = false;
    try {
      started = (await h.call("puerts_pie_start", {}, { label: "8. start explicitly authorized PIE" }))?.success === true;
      if (started) {
        await h.call("puerts_pie_agent_control", { op: "move_to", location: [800, 0, 120], timeout: 5, acceptance_radius: 80 }, { label: "8. enter the interaction volume" });
        const state = await h.call("puerts_pie_agent_query", { op: "read_property", actor: LABEL, property: "InteractionCount" }, { label: "8. read live interaction state" });
        h.check((state?.data?.value ?? 0) > 0, "8. overlap moved InteractionCount", String(state?.data?.value));
      }
    } finally { if (started) await h.call("puerts_pie_stop", {}, { label: "8. stop explicitly authorized PIE" }); }
  }

  h.seal({ asset_path: INTERACTOR, inspect_tool: "puerts_graph_inspect", structure_hash: hash });
});
