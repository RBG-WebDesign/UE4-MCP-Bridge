#!/usr/bin/env node
// Vertical slice 1 of 7: GAMEPLAY.
//
// One prompt: "make a beacon actor that counts how many times something walks
// into it, lights up on begin play after a delay, and prove it works."
//
// What a pass would prove: a Blueprint actor with components, member variables
// and an event graph can be authored, compiled, read back independently,
// converged on a rerun and placed in the level without a human opening the
// editor. This is the slice closest to green today, so its failures are the
// ones that say what the OTHER six are missing structurally.
//
//   node Scripts/slice-gameplay.mjs
//   node Scripts/slice-gameplay.mjs --phase=cold

import { runSlice } from "./slice-harness.mjs";

const BP = "/Game/MCPGenerated/BP_SliceBeacon";

const SPEC = {
  asset_path: BP,
  parent_class: "Actor",
  components: [
    {
      class: "StaticMeshComponent",
      name: "Body",
      properties: {
        StaticMesh: "/Engine/BasicShapes/Cube.Cube",
        RelativeScale3D: { x: 1.5, y: 1.5, z: 3 },
      },
    },
    {
      class: "PointLightComponent",
      name: "Glow",
      attach_to: "Body",
      properties: { Intensity: 5000, LightColor: { r: 255, g: 180, b: 60, a: 255 } },
    },
  ],
  variables: [
    { name: "IsLit", type: "bool", default: false, category: "Beacon" },
    { name: "Pulses", type: "int", default: 0, category: "Beacon" },
    { name: "LightDelay", type: "float", default: 1.5, category: "Beacon" },
  ],
  graph: {
    nodes: [
      { id: "note", type: "Comment", x: -200, y: -260, params: { text: "vertical slice: gameplay", width: 900, height: 220 } },
      { id: "begin", type: "BeginPlay", x: 0, y: 0 },
      { id: "announce", type: "PrintString", x: 300, y: 0, params: { InString: "beacon warming up" } },
      { id: "getDelay", type: "VariableGet", x: 300, y: 200, params: { var_name: "LightDelay" } },
      { id: "wait", type: "Delay", x: 640, y: 0 },
      { id: "light", type: "VariableSet", x: 980, y: 0, params: { var_name: "IsLit", value: true } },
      { id: "lit", type: "PrintString", x: 1320, y: 0, params: { InString: "beacon lit" } },

      { id: "overlap", type: "ActorBeginOverlap", x: 0, y: 600 },
      { id: "getLit", type: "VariableGet", x: 0, y: 820, params: { var_name: "IsLit" } },
      { id: "gate", type: "Branch", x: 340, y: 600 },
      { id: "getPulses", type: "VariableGet", x: 340, y: 900, params: { var_name: "Pulses" } },
      { id: "bump", type: "Operator", x: 680, y: 880, params: { op: "add_int", B: 1 } },
      { id: "store", type: "VariableSet", x: 1020, y: 600, params: { var_name: "Pulses" } },
      { id: "report", type: "PrintString", x: 1360, y: 600, params: { InString: "beacon touched" } },
      { id: "ignored", type: "PrintString", x: 680, y: 1120, params: { InString: "beacon is dark, ignoring" } },
    ],
    connections: [
      { from: "begin.then", to: "announce.exec" },
      { from: "announce.then", to: "wait.exec" },
      { from: "getDelay.LightDelay", to: "wait.Duration" },
      // Delay's output exec pin role is the one thing this graph is not certain
      // of: the builder vocabulary documents "then" as always Then, and a latent
      // Delay names its output Completed. If exactly this pair turns up in
      // graph.unresolved_connections the gap is the vocabulary, not the fixture.
      { from: "wait.Completed", to: "light.exec" },
      { from: "light.then", to: "lit.exec" },

      { from: "overlap.then", to: "gate.exec" },
      { from: "getLit.IsLit", to: "gate.Condition" },
      { from: "gate.then", to: "store.exec" },
      { from: "gate.else", to: "ignored.exec" },
      { from: "getPulses.Pulses", to: "bump.A" },
      { from: "bump.ReturnValue", to: "store.Pulses" },
      { from: "store.then", to: "report.exec" },
    ],
  },
};

await runSlice({
  id: "gameplay",
  title: "a Blueprint actor with components, variables and a working event graph",
  proves: "author, compile, read back independently, converge on rerun, place in the level, and observe it run",
}, async (h) => {
  const built = await h.call("puerts_blueprint_build", SPEC, {
    label: "1. build the beacon Blueprint from one spec",
    why: "the whole gameplay slice is one desired-state Blueprint build",
  });
  if (built?.success === true) {
    h.check(built.data?.compile_status === "UpToDate",
      "1. the Blueprint compiled clean", String(built.data?.compile_status));
    h.check((built.data?.graph?.unresolved_connections ?? []).length === 0,
      "1. every connection in the spec resolved to real pins",
      JSON.stringify(built.data?.graph?.unresolved_connections ?? []).slice(0, 300));
    h.check(typeof built.data?.generated_class_path === "string",
      "1. the response names the generated class a level can spawn",
      String(built.data?.generated_class_path));
  }

  const read = await h.call("puerts_graph_inspect", { asset_path: BP, include_pins: true }, {
    label: "2. read the asset back with the independent inspector",
    why: "a builder's own report is not evidence; the inspector is the second opinion",
  });
  let hash = null;
  if (read?.success === true) {
    const data = read.data ?? {};
    hash = data.graph?.structure_hash_sha1 ?? null;
    const names = (list) => (list ?? []).map((x) => x.name);
    h.check(["Body", "Glow"].every((n) => names(data.components).includes(n)),
      "2. both components are on the asset", JSON.stringify(names(data.components)));
    h.check(["IsLit", "Pulses", "LightDelay"].every((n) => names(data.variables).includes(n)),
      "2. all three member variables are on the asset", JSON.stringify(names(data.variables)));
    h.check((data.graph?.node_count ?? 0) === SPEC.graph.nodes.length,
      "2. the graph has exactly the nodes the spec asked for",
      `${data.graph?.node_count} vs ${SPEC.graph.nodes.length}`);
    h.check((data.graph?.unmapped_nodes ?? []).length === 0,
      "2. no node came back outside the builder vocabulary");
    h.check(data.package_dirty_after === false || data.package_dirty_after === data.package_dirty_before,
      "2. inspecting did not dirty the package");
    h.check(typeof hash === "string" && hash.length === 40,
      "2. the inspector reports a structure hash", String(hash));
  }

  const shaAfterBuild = h.fileSha(BP);
  const p4Before = h.sourceControl();

  const rerun = await h.call("puerts_blueprint_build", SPEC, {
    label: "3. rerunning the identical spec converges",
    why: "a desired-state builder that is not convergent cannot be composed into a workflow",
  });
  if (rerun?.success === true) {
    const readAgain = await h.call("puerts_graph_inspect", { asset_path: BP }, {
      label: "3. re-read after the rerun",
    });
    h.check(readAgain?.data?.graph?.structure_hash_sha1 === hash,
      "3. the structure hash is unmoved by an identical rebuild",
      `${readAgain?.data?.graph?.structure_hash_sha1} vs ${hash}`);
    h.check(shaAfterBuild === null || h.fileSha(BP) === shaAfterBuild,
      "3. a converged rerun wrote no new bytes to disk",
      "a failure here is a real finding: blueprint_build re-saves an unchanged asset");
  }

  // Placement. spawn_actor takes a class path and nothing else: it cannot label
  // the actor, cannot scale it, and cannot put it in an outliner folder, which
  // is the whole reason lane L's scene_batch exists. The slice asks for the
  // tool that can express the intent, then degrades so the rest still runs.
  const canScene = h.need("puerts_scene_batch", {
    why: "place the beacon with a stable label and a transform in one desired-state call; puerts_spawn_actor cannot label, scale, or fold an actor",
    request: {
      tool: "puerts_scene_batch",
      owner: "lane L (settled, implemented_unverified)",
      params: { operations: "[{op:'upsert_actor', select?, label?, class?, location?, rotation?, scale?, folder?, tags?, properties?}]", plan_only: "boolean", verify: "boolean" },
    },
  });
  const cls = built?.data?.generated_class_path;
  if (canScene && cls) {
    await h.call("puerts_scene_batch", {
      operations: [{
        op: "upsert_actor", label: "SliceBeacon", class: cls,
        location: { x: 400, y: 0, z: 100 },
      }],
    }, { label: "4. place the beacon in the level with a stable label" });
  } else if (cls) {
    h.note("4. degraded placement path",
      "scene_batch is unavailable, so the slice falls back to puerts_spawn_actor and accepts an unlabelled actor");
    const spawned = await h.call("puerts_spawn_actor", { class_path: cls, location: { x: 400, y: 0, z: 100 } }, {
      label: "4. spawn the beacon (no label, no scale, no folder)",
    });
    if (spawned?.success === true) {
      const found = await h.call("puerts_find_actors", { type: "BP_SliceBeacon_C" }, {
        label: "4. an independent query finds the spawned beacon",
      });
      h.check((found?.data?.actors ?? []).length >= 1,
        "4. the level really contains the beacon", `${(found?.data?.actors ?? []).length} found`);
    }
  }

  await h.call("puerts_viewport_screenshot", { filename: "slice-gameplay.png" }, {
    label: "5. capture the viewport as the visual half of the feedback loop",
  });

  // The event graph does something observable only while the game runs, and
  // AGENTS.md reserves PIE for the user to ask for. Nothing in the catalog can
  // observe a Blueprint executing without entering play.
  h.policy("6. observe the graph actually running",
    "PIE is reserved for the user to request (AGENTS.md testing etiquette). puerts_pie_start exists but is live_partial, "
    + "and there is no primitive that reads a variable off a running actor: puerts_read_property addresses editor objects. "
    + "A gameplay slice therefore proves authoring, not behavior.");

  h.seal({ asset_path: BP, inspect_tool: "puerts_graph_inspect", structure_hash: hash });
  const p4After = h.sourceControl();
  if (p4Before !== null && p4After !== null) {
    h.check(p4Before === p4After, "7. no unexpected source-control entry appeared");
  } else {
    h.note("7. source-control quiescence not measured", "p4 is unavailable in this environment");
  }
});
