#!/usr/bin/env node
// Feature acceptance 10: controller navigation.

import { collectWidgetNames, runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const MENU = `/Game/MCPGenerated/FeatureAcceptance/WBP_ControllerNav_${runId}`;
const HOST = `/Game/MCPGenerated/FeatureAcceptance/BP_ControllerNav_${runId}`;
const MENU_CLASS = `${MENU}.WBP_ControllerNav_${runId}_C`;
const HOST_CLASS = `${HOST}.BP_ControllerNav_${runId}_C`;
const HOST_LABEL = `FeatureControllerNav_${runId}`;

const CONTROL = { root: { type: "VerticalBox", name: "Root", children: [
  { type: "Button", name: "ControlButton", children: [{ type: "TextBlock", name: "ControlLabel", properties: { text: "Control" } }] },
] } };
const TREE = { root: { type: "VerticalBox", name: "Root", children: [
  { type: "TextBlock", name: "Prompt", properties: { text: "Use D-pad, A to select, B to return" } },
  { type: "Button", name: "FirstButton", children: [{ type: "TextBlock", name: "FirstLabel", properties: { text: "First" } }] },
  { type: "Button", name: "SecondButton", children: [{ type: "TextBlock", name: "SecondLabel", properties: { text: "Second" } }] },
  { type: "Button", name: "BackButton", children: [{ type: "TextBlock", name: "BackLabel", properties: { text: "Back" } }] },
] } };
const HOST_SPEC = { asset_path: HOST, parent_class: "Actor", graph: {
  nodes: [
    { id: "begin", type: "BeginPlay", x: 0, y: 0 },
    { id: "create", type: "CallFunction", x: 300, y: 0, params: { class: "WidgetBlueprintLibrary", function: "Create", WidgetType: MENU_CLASS } },
    { id: "show", type: "CallFunction", x: 650, y: 0, params: { class: "UserWidget", function: "AddToViewport" } },
    { id: "down", type: "InputKey", x: 0, y: 300, params: { fkey_name: "Gamepad_DPad_Down" } },
    { id: "downMarker", type: "PrintString", x: 360, y: 300, params: { InString: `FEATURE_NAV_DOWN_${runId}` } },
    { id: "accept", type: "InputKey", x: 0, y: 520, params: { fkey_name: "Gamepad_FaceButton_Bottom" } },
    { id: "acceptMarker", type: "PrintString", x: 360, y: 520, params: { InString: `FEATURE_NAV_ACCEPT_${runId}` } },
    { id: "back", type: "InputKey", x: 0, y: 740, params: { fkey_name: "Gamepad_FaceButton_Right" } },
    { id: "backMarker", type: "PrintString", x: 360, y: 740, params: { InString: `FEATURE_NAV_BACK_${runId}` } },
  ], connections: [
    { from: "begin.then", to: "create.exec" }, { from: "create.then", to: "show.exec" }, { from: "create.ReturnValue", to: "show.self" },
    { from: "down.Pressed", to: "downMarker.exec" }, { from: "accept.Pressed", to: "acceptMarker.exec" }, { from: "back.Pressed", to: "backMarker.exec" },
  ],
} };

await runSlice({
  id: "feature-controller-navigation",
  title: "a focusable menu with D-pad, accept and back controller routes",
  proves: "author inspectable focus targets and controller input routes, converge, cold-load, and optionally inject controller keys in PIE",
}, async (h) => {
  if (!await h.assertFreshFixture(MENU, "puerts_widget_inspect")
    || !await h.assertFreshFixture(HOST, "puerts_graph_inspect")) return;

  const controlBuild = await h.call("puerts_widget_build", { asset_path: MENU, tree: CONTROL }, { label: "1. build one-button navigation control" });
  if (controlBuild?.success !== true) return;
  const control = await h.call("puerts_widget_inspect", { asset_path: MENU }, { label: "1. inspect navigation control" });
  const controlHash = control?.data?.structure_hash_sha1 ?? null;
  const menuBuild = await h.call("puerts_widget_build", { asset_path: MENU, tree: TREE }, { label: "2. replace control with controller menu" });
  if (menuBuild?.success !== true) return;
  const read = await h.call("puerts_widget_inspect", { asset_path: MENU }, { label: "2. inspect controller menu independently" });
  const hash = read?.data?.structure_hash_sha1 ?? null;
  const names = collectWidgetNames(read?.data?.root);
  h.check(typeof hash === "string" && hash !== controlHash, "2. the one-button control moved to three focus targets", `${controlHash} -> ${hash}`);
  h.check(["FirstButton", "SecondButton", "BackButton"].every((name) => names.includes(name)), "2. all focus targets read back", JSON.stringify(names));

  const exposed = ["FirstButton", "SecondButton", "BackButton"];
  await h.call("puerts_widget_bind", { asset_path: MENU, expose_as_variable: exposed }, { label: "3. expose controller focus targets" });
  const bound = await h.call("puerts_widget_inspect", { asset_path: MENU }, { label: "3. inspect focus target variables" });
  h.check(exposed.every((name) => (bound?.data?.variables ?? []).some((v) => (v.name ?? v) === name)), "3. every focus target is addressable");

  const built = await h.call("puerts_blueprint_build", HOST_SPEC, { label: "4. build D-pad, accept and back routes" });
  if (built?.success !== true) return;
  const hostRead = await h.call("puerts_graph_inspect", { asset_path: HOST, include_pins: true }, { label: "4. inspect controller routes independently" });
  const hostHash = hostRead?.data?.graph?.structure_hash_sha1 ?? null;
  const graph = JSON.stringify(hostRead?.data?.graph?.nodes ?? []);
  h.check(built?.data?.compile_status === "UpToDate", "4. controller host compiles", String(built?.data?.compile_status));
  h.check(["Gamepad_DPad_Down", "Gamepad_FaceButton_Bottom", "Gamepad_FaceButton_Right"].every((key) => graph.includes(key)),
    "4. every controller key reads back from the graph");

  const menuSha = h.fileSha(MENU);
  const hostSha = h.fileSha(HOST);
  await h.call("puerts_widget_build", { asset_path: MENU, tree: TREE }, { label: "5. rerun controller menu" });
  await h.call("puerts_blueprint_build", HOST_SPEC, { label: "5. rerun controller host" });
  const menuAgain = await h.call("puerts_widget_inspect", { asset_path: MENU }, { label: "5. inspect menu rerun" });
  const hostAgain = await h.call("puerts_graph_inspect", { asset_path: HOST }, { label: "5. inspect host rerun" });
  h.check(menuAgain?.data?.structure_hash_sha1 === hash, "5. controller menu converged");
  h.check(hostAgain?.data?.graph?.structure_hash_sha1 === hostHash, "5. controller routes converged");
  h.check(menuSha === null || h.fileSha(MENU) === menuSha, "5. controller menu rerun wrote no new bytes");
  h.check(hostSha === null || h.fileSha(HOST) === hostSha, "5. controller host rerun wrote no new bytes");

  if (!includePie) {
    h.policy("6. navigate with a controller", "PIE is off by default. Re-run with --pie only after explicit user authorization.");
  } else {
    const placed = await h.call("puerts_scene_batch", { operations: [{ op: "upsert_actor", label: HOST_LABEL, class: HOST_CLASS, location: { x: 0, y: 0, z: 150 } }] }, { label: "6. place controller host" });
    let started = false;
    try {
      if (placed?.success === true) started = (await h.call("puerts_pie_start", {}, { label: "6. start explicitly authorized PIE" }))?.success === true;
      if (started) {
        for (const key of ["Gamepad_DPad_Down", "Gamepad_FaceButton_Bottom", "Gamepad_FaceButton_Right"])
          await h.call("puerts_pie_agent_control", { op: "press", key }, { label: `6. press ${key}` });
        const observed = await h.call("puerts_pie_agent_query", { op: "observe", log_lines: 100 }, { label: "6. observe controller markers" });
        const payload = JSON.stringify(observed?.data ?? {});
        h.check([`FEATURE_NAV_DOWN_${runId}`, `FEATURE_NAV_ACCEPT_${runId}`, `FEATURE_NAV_BACK_${runId}`].every((marker) => payload.includes(marker)),
          "6. all controller inputs reached their routes");
      }
    } finally { if (started) await h.call("puerts_pie_stop", {}, { label: "6. stop explicitly authorized PIE" }); }
  }

  h.seal({ asset_path: MENU, inspect_tool: "puerts_widget_inspect", structure_hash: hash });
  h.seal({ asset_path: HOST, inspect_tool: "puerts_graph_inspect", structure_hash: hostHash });
});
