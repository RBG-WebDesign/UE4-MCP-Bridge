#!/usr/bin/env node
// Feature acceptance 9: main, pause and settings menus.

import { collectWidgetNames, runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const MENU = `/Game/MCPGenerated/FeatureAcceptance/WBP_Menus_${runId}`;
const HOST = `/Game/MCPGenerated/FeatureAcceptance/BP_MenuHost_${runId}`;
const MENU_CLASS = `${MENU}.WBP_Menus_${runId}_C`;
const HOST_CLASS = `${HOST}.BP_MenuHost_${runId}_C`;
const HOST_LABEL = `FeatureMenuHost_${runId}`;

const CONTROL = { root: { type: "CanvasPanel", name: "Root", children: [
  { type: "TextBlock", name: "ControlText", properties: { text: "menu control" } },
] } };

const TREE = { root: { type: "CanvasPanel", name: "Root", children: [
  { type: "VerticalBox", name: "MainMenu", children: [
    { type: "TextBlock", name: "GameTitle", properties: { text: "Feature Demo" } },
    { type: "Button", name: "PlayButton", children: [{ type: "TextBlock", name: "PlayLabel", properties: { text: "Play" } }] },
    { type: "Button", name: "SettingsButton", children: [{ type: "TextBlock", name: "SettingsLabel", properties: { text: "Settings" } }] },
    { type: "Button", name: "QuitButton", children: [{ type: "TextBlock", name: "QuitLabel", properties: { text: "Quit" } }] },
  ] },
  { type: "VerticalBox", name: "PauseMenu", properties: { visibility: "Collapsed" }, children: [
    { type: "TextBlock", name: "PausedTitle", properties: { text: "Paused" } },
    { type: "Button", name: "ResumeButton", children: [{ type: "TextBlock", name: "ResumeLabel", properties: { text: "Resume" } }] },
    { type: "Button", name: "PauseSettingsButton", children: [{ type: "TextBlock", name: "PauseSettingsLabel", properties: { text: "Settings" } }] },
  ] },
  { type: "VerticalBox", name: "SettingsMenu", properties: { visibility: "Collapsed" }, children: [
    { type: "TextBlock", name: "SettingsTitle", properties: { text: "Settings" } },
    { type: "Slider", name: "MasterVolume", properties: { value: 0.8 } },
    { type: "CheckBox", name: "FullscreenToggle" },
    { type: "Button", name: "SettingsBackButton", children: [{ type: "TextBlock", name: "BackLabel", properties: { text: "Back" } }] },
  ] },
] } };

const HOST_SPEC = {
  asset_path: HOST,
  parent_class: "Actor",
  graph: {
    nodes: [
      { id: "begin", type: "BeginPlay", x: 0, y: 0 },
      { id: "create", type: "CallFunction", x: 300, y: 0, params: { class: "WidgetBlueprintLibrary", function: "Create", WidgetType: MENU_CLASS } },
      { id: "show", type: "CallFunction", x: 650, y: 0, params: { class: "UserWidget", function: "AddToViewport" } },
      { id: "shown", type: "PrintString", x: 980, y: 0, params: { InString: `FEATURE_MAIN_MENU_VISIBLE_${runId}` } },
      { id: "escape", type: "InputKey", x: 0, y: 320, params: { fkey_name: "Escape" } },
      { id: "pause", type: "CallFunction", x: 350, y: 320, params: { class: "GameplayStatics", function: "SetGamePaused", bPaused: true } },
      { id: "paused", type: "PrintString", x: 700, y: 320, params: { InString: `FEATURE_PAUSE_MENU_${runId}` } },
    ],
    connections: [
      { from: "begin.then", to: "create.exec" }, { from: "create.then", to: "show.exec" },
      { from: "create.ReturnValue", to: "show.self" }, { from: "show.then", to: "shown.exec" },
      { from: "escape.Pressed", to: "pause.exec" }, { from: "pause.then", to: "paused.exec" },
    ],
  },
};

await runSlice({
  id: "feature-menus",
  title: "main, pause and settings menus with a pause input route",
  proves: "author and inspect all three menu states, compose a display and pause host, converge, cold-load, and optionally exercise Escape in PIE",
}, async (h) => {
  if (!await h.assertFreshFixture(MENU, "puerts_widget_inspect")
    || !await h.assertFreshFixture(HOST, "puerts_graph_inspect")) return;

  const control = await h.call("puerts_widget_build", { asset_path: MENU, tree: CONTROL }, { label: "1. build the menu control" });
  if (control?.success !== true) return;
  const controlRead = await h.call("puerts_widget_inspect", { asset_path: MENU }, { label: "1. inspect the menu control" });
  const controlHash = controlRead?.data?.structure_hash_sha1 ?? null;

  const built = await h.call("puerts_widget_build", { asset_path: MENU, tree: TREE }, { label: "2. replace the control with all menu states" });
  if (built?.success !== true) return;
  const read = await h.call("puerts_widget_inspect", { asset_path: MENU }, { label: "3. inspect all menu states independently" });
  const hash = read?.data?.structure_hash_sha1 ?? null;
  const names = collectWidgetNames(read?.data?.root);
  h.check(typeof hash === "string" && hash !== controlHash, "3. the control moved to the menu state", `${controlHash} -> ${hash}`);
  h.check(["MainMenu", "PlayButton", "PauseMenu", "ResumeButton", "SettingsMenu", "MasterVolume", "FullscreenToggle", "SettingsBackButton"]
    .every((name) => names.includes(name)), "3. main, pause and settings controls read back", JSON.stringify(names));

  const exposed = ["MainMenu", "PauseMenu", "SettingsMenu", "MasterVolume", "FullscreenToggle"];
  await h.call("puerts_widget_bind", { asset_path: MENU, expose_as_variable: exposed }, { label: "4. expose menu state controls" });
  const bound = await h.call("puerts_widget_inspect", { asset_path: MENU }, { label: "4. inspect exposed menu controls" });
  h.check(exposed.every((name) => (bound?.data?.variables ?? []).some((v) => (v.name ?? v) === name)),
    "4. every menu state control is addressable by gameplay");

  const host = await h.call("puerts_blueprint_build", HOST_SPEC, { label: "5. build the menu display and pause host" });
  const hostRead = await h.call("puerts_graph_inspect", { asset_path: HOST, include_pins: true }, { label: "5. inspect the menu host independently" });
  const hostHash = hostRead?.data?.graph?.structure_hash_sha1 ?? null;
  h.check(host?.data?.compile_status === "UpToDate", "5. the menu host compiles", String(host?.data?.compile_status));
  h.check(JSON.stringify(hostRead?.data?.graph?.nodes ?? []).includes("SetGamePaused"), "5. the host reads back the pause route");

  const menuSha = h.fileSha(MENU);
  const hostSha = h.fileSha(HOST);
  await h.call("puerts_widget_build", { asset_path: MENU, tree: TREE }, { label: "6. rerun the menu desired state" });
  await h.call("puerts_blueprint_build", HOST_SPEC, { label: "6. rerun the menu host desired state" });
  const menuAgain = await h.call("puerts_widget_inspect", { asset_path: MENU }, { label: "6. inspect the menu rerun" });
  const hostAgain = await h.call("puerts_graph_inspect", { asset_path: HOST }, { label: "6. inspect the host rerun" });
  h.check(menuAgain?.data?.structure_hash_sha1 === hash, "6. the menu tree converged");
  h.check(hostAgain?.data?.graph?.structure_hash_sha1 === hostHash, "6. the menu host converged");
  h.check(menuSha === null || h.fileSha(MENU) === menuSha, "6. the menu rerun wrote no new bytes");
  h.check(hostSha === null || h.fileSha(HOST) === hostSha, "6. the host rerun wrote no new bytes");

  if (!includePie) {
    h.policy("7. display and pause the menus", "PIE is off by default. Re-run with --pie only after explicit user authorization.");
  } else {
    const placed = await h.call("puerts_scene_batch", { operations: [{ op: "upsert_actor", label: HOST_LABEL, class: HOST_CLASS, location: { x: 0, y: 0, z: 150 } }] }, { label: "7. place the menu host" });
    let started = false;
    try {
      if (placed?.success === true) started = (await h.call("puerts_pie_start", {}, { label: "7. start explicitly authorized PIE" }))?.success === true;
      if (started) {
        await new Promise((resolve) => setTimeout(resolve, 1000));
        await h.call("puerts_pie_agent_control", { op: "press", key: "Escape" }, { label: "7. press Escape" });
        const observed = await h.call("puerts_pie_agent_query", { op: "observe", log_lines: 100 }, { label: "7. observe menu markers" });
        const payload = JSON.stringify(observed?.data ?? {});
        h.check(payload.includes(`FEATURE_MAIN_MENU_VISIBLE_${runId}`), "7. the main menu reached the viewport");
        h.check(payload.includes(`FEATURE_PAUSE_MENU_${runId}`), "7. Escape reached the pause route");
      }
    } finally { if (started) await h.call("puerts_pie_stop", {}, { label: "7. stop explicitly authorized PIE" }); }
  }

  h.seal({ asset_path: MENU, inspect_tool: "puerts_widget_inspect", structure_hash: hash });
  h.seal({ asset_path: HOST, inspect_tool: "puerts_graph_inspect", structure_hash: hostHash });
});
