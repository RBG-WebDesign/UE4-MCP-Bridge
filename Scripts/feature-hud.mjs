#!/usr/bin/env node
// Feature acceptance 8: HUD.
//
// Warm builds a fresh Widget Blueprint from a smaller state-moving control,
// exposes the live-data widgets, independently inspects the result, and proves
// a no-op rerun. Cold re-reads the sealed widget after an editor restart.
// PIE is off unless --pie is passed.

import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const HUD = `/Game/MCPGenerated/FeatureAcceptance/WBP_HUD_${runId}`;
const HOST = `/Game/MCPGenerated/FeatureAcceptance/BP_HUDHost_${runId}`;
const HUD_CLASS = `${HUD}.WBP_HUD_${runId}_C`;
const HOST_CLASS = `${HOST}.BP_HUDHost_${runId}_C`;
const HOST_LABEL = `FeatureHUDHost_${runId}`;

async function assertUnused(h, assetPath) {
  const name = assetPath.split("/").pop();
  const found = await h.call("puerts_find_assets", {
    path: "/Game/MCPGenerated/FeatureAcceptance",
    name,
    recursive: false,
    limit: 2,
  }, { label: `assert the fresh fixture is unused: ${name}` });
  const unused = found?.success === true && (found.data?.assets ?? []).length === 0;
  h.check(unused, `the fixture path ${assetPath} did not exist before the control`);
  return unused;
}

function collectNames(root) {
  const names = [];
  const visit = (node) => {
    if (!node) return;
    names.push(node.name);
    for (const child of node.children ?? []) visit(child);
  };
  visit(root);
  return names;
}

const CONTROL_TREE = {
  root: {
    type: "CanvasPanel",
    name: "Root",
    children: [{
      type: "TextBlock",
      name: "ControlText",
      properties: { text: "HUD control" },
      slot: { position: { x: 40, y: 40 }, size: { x: 240, y: 40 } },
    }],
  },
};

const HUD_TREE = {
  root: {
    type: "CanvasPanel",
    name: "Root",
    children: [
      {
        type: "VerticalBox",
        name: "VitalsPanel",
        slot: { position: { x: 48, y: 48 }, size: { x: 420, y: 150 }, zOrder: 2 },
        children: [
          { type: "TextBlock", name: "PlayerName", properties: { text: "Player", justification: "Left" } },
          { type: "ProgressBar", name: "HealthBar", properties: { percent: 1, fillColorAndOpacity: { r: 0.8, g: 0.05, b: 0.05, a: 1 } } },
          { type: "ProgressBar", name: "ShieldBar", properties: { percent: 0.65, fillColorAndOpacity: { r: 0.05, g: 0.35, b: 0.9, a: 1 } } },
        ],
      },
      {
        type: "TextBlock",
        name: "Crosshair",
        properties: { text: "+", justification: "Center" },
        slot: { position: { x: 930, y: 500 }, size: { x: 60, y: 60 }, zOrder: 5 },
      },
      {
        type: "TextBlock",
        name: "AmmoText",
        properties: { text: "30 / 120", justification: "Right" },
        slot: { position: { x: 1540, y: 900 }, size: { x: 300, y: 60 }, zOrder: 2 },
      },
      {
        type: "TextBlock",
        name: "ObjectiveText",
        properties: { text: "Reach the checkpoint", justification: "Center" },
        slot: { position: { x: 660, y: 40 }, size: { x: 600, y: 50 }, zOrder: 2 },
      },
    ],
  },
};
const HOST_SPEC = {
  asset_path: HOST,
  parent_class: "Actor",
  graph: {
    nodes: [
      { id: "begin", type: "BeginPlay", x: 0, y: 0 },
      {
        id: "create", type: "CallFunction", x: 340, y: 0,
        params: { class: "WidgetBlueprintLibrary", function: "Create", WidgetType: HUD_CLASS },
      },
      { id: "show", type: "CallFunction", x: 720, y: 0, params: { class: "UserWidget", function: "AddToViewport" } },
      { id: "marker", type: "PrintString", x: 1060, y: 0, params: { InString: `FEATURE_HUD_VISIBLE_${runId}` } },
    ],
    connections: [
      { from: "begin.then", to: "create.exec" },
      { from: "create.then", to: "show.exec" },
      { from: "create.ReturnValue", to: "show.self" },
      { from: "show.then", to: "marker.exec" },
    ],
  },
};

await runSlice({
  id: "feature-hud",
  title: "a game HUD with independently inspectable vitals, aiming, ammunition and objective widgets",
  proves: "author a complete HUD tree from a small control, expose live-data widgets, converge, cold-load, and optionally prove it reaches the game viewport",
}, async (h) => {
  if (!await assertUnused(h, HUD) || !await assertUnused(h, HOST)) return;

  const hudAbsent = await h.expectFailure("puerts_widget_inspect", {
    asset_path: HUD,
  }, { label: "the independent widget reader refuses the unused HUD path" });
  const hostAbsent = await h.expectFailure("puerts_graph_inspect", {
    asset_path: HOST,
  }, { label: "the independent graph reader refuses the unused HUD host path" });
  if (hudAbsent === null || hostAbsent === null) return;

  const control = await h.call("puerts_widget_build", { asset_path: HUD, tree: CONTROL_TREE }, {
    label: "1. build the minimal HUD as the state-moving positive control",
  });
  if (control?.success !== true) return;
  const controlRead = await h.call("puerts_widget_inspect", { asset_path: HUD }, {
    label: "1. inspect the control independently",
  });
  const controlHash = controlRead?.data?.structure_hash_sha1 ?? null;

  const built = await h.call("puerts_widget_build", { asset_path: HUD, tree: HUD_TREE }, {
    label: "2. replace the control with the complete HUD tree",
  });
  if (built?.success !== true) return;
  const read = await h.call("puerts_widget_inspect", { asset_path: HUD }, {
    label: "3. inspect the complete HUD independently",
  });
  const hash = read?.data?.structure_hash_sha1 ?? null;
  const names = collectNames(read?.data?.root);
  h.check(typeof hash === "string" && hash !== controlHash,
    "3. the positive control moved to the requested HUD state", `${controlHash} -> ${hash}`);
  h.check(["Root", "VitalsPanel", "PlayerName", "HealthBar", "ShieldBar", "Crosshair", "AmmoText", "ObjectiveText"]
    .every((name) => names.includes(name)),
  "3. every requested HUD widget exists", JSON.stringify(names));
  h.check(read?.data?.package_dirty_after === false
    || read?.data?.package_dirty_after === read?.data?.package_dirty_before,
  "3. the independent inspector did not dirty the HUD package");

  const fileSha = h.fileSha(HUD);
  const rerun = await h.call("puerts_widget_build", { asset_path: HUD, tree: HUD_TREE }, {
    label: "4. rerun the identical HUD tree",
  });
  if (rerun?.success === true) {
    const again = await h.call("puerts_widget_inspect", { asset_path: HUD }, {
      label: "4. inspect the converged HUD rerun",
    });
    h.check(again?.data?.structure_hash_sha1 === hash,
      "4. the HUD structure hash converged");
    h.check(fileSha === null || h.fileSha(HUD) === fileSha,
      "4. the converged HUD rerun wrote no new bytes");
  }

  let sealedHash = hash;
  const exposed = ["HealthBar", "ShieldBar", "AmmoText", "ObjectiveText"];
  const bound = await h.call("puerts_widget_bind", {
    asset_path: HUD,
    expose_as_variable: exposed,
  }, { label: "5. expose every widget gameplay needs to update" });
  if (bound?.success === true) {
    const afterBind = await h.call("puerts_widget_inspect", { asset_path: HUD }, {
      label: "5. inspect exposed widget variables independently",
    });
    sealedHash = afterBind?.data?.structure_hash_sha1 ?? hash;
    const variables = (afterBind?.data?.variables ?? []).map((v) => v.name ?? v);
    h.check(exposed.every((name) => variables.includes(name)),
      "5. every live-data widget is exposed", JSON.stringify(variables));
    const bindSha = h.fileSha(HUD);
    const bindRerun = await h.call("puerts_widget_bind", {
      asset_path: HUD,
      expose_as_variable: exposed,
    }, { label: "5. rerun the widget exposure desired state" });
    h.check(bindRerun?.data?.applied_change_count === 0 && bindRerun?.data?.saved === false,
      "5. the exposure rerun converged without saving",
      `applied=${bindRerun?.data?.applied_change_count} saved=${bindRerun?.data?.saved}`);
    h.check(bindSha === null || h.fileSha(HUD) === bindSha,
      "5. the converged exposure rerun wrote no new bytes");
  }

  const host = await h.call("puerts_blueprint_build", HOST_SPEC,
    { label: "6. build a host that creates the HUD and adds it to the viewport" });
  let hostHash = null;
  if (host?.success === true) {
    h.check((host.data?.graph?.unresolved_connections ?? []).length === 0,
      "6. the create-and-show graph has no unresolved connection");
    const hostRead = await h.call("puerts_graph_inspect", { asset_path: HOST, include_pins: true }, {
      label: "6. inspect the HUD host independently",
    });
    hostHash = hostRead?.data?.graph?.structure_hash_sha1 ?? null;
    h.check(JSON.stringify(hostRead?.data?.graph?.nodes ?? []).includes(`WBP_HUD_${runId}`),
      "6. independent pin read-back names the generated HUD class");
    const hostSha = h.fileSha(HOST);
    const hostRerun = await h.call("puerts_blueprint_build", HOST_SPEC,
      { label: "6. rerun the identical HUD host" });
    if (hostRerun?.success === true) {
      const hostAgain = await h.call("puerts_graph_inspect", { asset_path: HOST }, {
        label: "6. inspect the converged HUD host",
      });
      h.check(hostAgain?.data?.graph?.structure_hash_sha1 === hostHash,
        "6. the HUD host graph hash converged");
      h.check(hostSha === null || h.fileSha(HOST) === hostSha,
        "6. the converged HUD host wrote no new bytes");
    }
  }

  if (!includePie) {
    h.policy("7. prove the HUD reaches the game viewport",
      "PIE is off by default. Re-run with --pie only after explicit user authorization.");
  } else if (host?.success === true) {
    const placed = await h.call("puerts_scene_batch", {
      operations: [{ op: "upsert_actor", label: HOST_LABEL, class: HOST_CLASS, location: { x: 0, y: 0, z: 150 } }],
    }, { label: "7. place the fresh HUD host" });
    let started = false;
    try {
      if (placed?.success === true) {
        const start = await h.call("puerts_pie_start", {}, { label: "7. start explicitly authorized PIE" });
        started = start?.success === true;
      }
      if (started) {
        await new Promise((resolve) => setTimeout(resolve, 1500));
        const observed = await h.call("puerts_pie_agent_query", {
          op: "observe", class_filter: `BP_HUDHost_${runId}_C`, log_lines: 100,
        }, { label: "7. observe the running HUD independently" });
        const payload = JSON.stringify(observed?.data ?? {});
        h.check(payload.includes(`FEATURE_HUD_VISIBLE_${runId}`),
          "7. the runtime log contains the host marker");
        h.check(payload.includes(`WBP_HUD_${runId}`) || /HealthBar|AmmoText/.test(payload),
          "7. the runtime observation contains the authored HUD");
      }
    } finally {
      if (started) await h.call("puerts_pie_stop", {}, { label: "7. stop the explicitly authorized PIE session" });
    }
  }

  h.seal({ asset_path: HUD, inspect_tool: "puerts_widget_inspect", structure_hash: sealedHash });
  if (hostHash) h.seal({ asset_path: HOST, inspect_tool: "puerts_graph_inspect", structure_hash: hostHash });
});
