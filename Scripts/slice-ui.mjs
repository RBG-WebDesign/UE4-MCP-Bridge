#!/usr/bin/env node
// Vertical slice 2 of 7: UI.
//
// One prompt: "build a beacon status HUD with a title, a charge bar and a
// toggle button, bind the bar to the beacon's charge, and show it on screen."
//
// What a pass would prove: a Widget Blueprint's layout, its bindings and its
// display path can all be authored from one prompt and read back. AGENTS.md
// still says widget_build has no inspector; puerts_widget_inspect is registered
// and live_verified, so the layout half of this slice is real. The binding half
// and the widget's own event graph are the parts with no primitive.
//
//   node Scripts/slice-ui.mjs
//   node Scripts/slice-ui.mjs --phase=cold

import { runSlice } from "./slice-harness.mjs";

const WBP = "/Game/MCPGenerated/WBP_SliceHUD";
const DRIVER = "/Game/MCPGenerated/BP_SliceHUDDriver";

const TREE = {
  asset_path: WBP,
  tree: {
    root: {
      type: "CanvasPanel",
      name: "Root",
      children: [
        {
          type: "VerticalBox",
          name: "Panel",
          slot: { position: { x: 80, y: 60 }, size: { x: 420, y: 260 }, alignment: { x: 0, y: 0 }, zOrder: 1 },
          children: [
            {
              type: "TextBlock",
              name: "TitleText",
              properties: { text: "Beacon Status", justification: "Center" },
              slot: { padding: { left: 8, top: 8, right: 8, bottom: 4 }, horizontalAlignment: "Fill" },
            },
            {
              type: "ProgressBar",
              name: "ChargeBar",
              properties: { percent: 0.35 },
              slot: { padding: { left: 8, top: 4, right: 8, bottom: 4 }, horizontalAlignment: "Fill" },
            },
            {
              type: "Button",
              name: "ToggleButton",
              slot: { padding: { left: 8, top: 4, right: 8, bottom: 8 }, horizontalAlignment: "Center" },
              children: [{ type: "TextBlock", name: "ToggleLabel", properties: { text: "Toggle" } }],
            },
          ],
        },
        {
          type: "TextBlock",
          name: "FooterText",
          properties: { text: "vertical slice: UI", visibility: "HitTestInvisible" },
          slot: { position: { x: 80, y: 340 }, size: { x: 420, y: 40 } },
        },
      ],
    },
  },
};

await runSlice({
  id: "ui",
  title: "a Widget Blueprint with a layout, a binding and an independent read-back",
  proves: "author a UMG tree, bind a widget property to data, read the asset back, and get it on screen",
}, async (h) => {
  const built = await h.call("puerts_widget_build", TREE, {
    label: "1. build the HUD widget tree from one spec",
    why: "the layout half of the UI slice is one desired-state widget build",
  });
  if (built?.success === true) {
    h.check(typeof built.data?.generated_class_path === "string",
      "1. the response names the generated widget class a graph can create",
      String(built.data?.generated_class_path));
  }

  const read = await h.call("puerts_widget_inspect", { asset_path: WBP }, {
    label: "2. read the widget back with the independent inspector",
    why: "AGENTS.md records widget_build as having no inspector; this step measures whether that is still true",
  });
  let hash = null;
  const found = [];
  if (read?.success === true) {
    hash = read.data?.structure_hash_sha1 ?? null;
    const walk = (node) => {
      if (!node) return;
      found.push(node.name);
      for (const c of node.children ?? []) walk(c);
    };
    walk(read.data?.root);
    h.check(["Root", "Panel", "TitleText", "ChargeBar", "ToggleButton", "ToggleLabel", "FooterText"]
      .every((n) => found.includes(n)),
      "2. every widget in the spec is in the asset", JSON.stringify(found));
    h.check(typeof hash === "string" && hash.length === 40,
      "2. the inspector reports a structure hash", String(hash));
    h.check(read.data?.package_dirty_after === false
      || read.data?.package_dirty_after === read.data?.package_dirty_before,
      "2. inspecting did not dirty the package");
    h.note("2. AGENTS.md is stale on this point",
      "\"widget_build has no inspector yet\" in AGENTS.md and in the product-goal section is wrong: "
      + "puerts_widget_inspect is registered and carries live evidence (docs/evidence/widget-inspect-2026-08-02.json)");
  }

  const shaAfterBuild = h.fileSha(WBP);
  const rerun = await h.call("puerts_widget_build", TREE, {
    label: "3. rerunning the identical tree converges",
    why: "a widget tree has no per-widget identity to merge against, so convergence is the only rerun guarantee",
  });
  if (rerun?.success === true) {
    const again = await h.call("puerts_widget_inspect", { asset_path: WBP }, { label: "3. re-read after the rerun" });
    h.check(again?.data?.structure_hash_sha1 === hash,
      "3. the structure hash is unmoved by an identical rebuild",
      `${again?.data?.structure_hash_sha1} vs ${hash}`);
    h.check(shaAfterBuild === null || h.fileSha(WBP) === shaAfterBuild,
      "3. a converged rerun wrote no new bytes to disk");
  }

  // The write side of bindings and is_variable is puerts_widget_bind. Only the
  // expose half runs here: a property binding needs a source function or
  // variable that already exists on this Widget Blueprint, and whether the
  // Blueprint tooling can author one inside a WidgetBlueprint is exactly what
  // step 4 below is still measuring. Exposing needs no prerequisite and is the
  // half that decides whether a graph can reach these widgets by name at all.
  const EXPOSED = ["ChargeBar", "ToggleButton", "TitleText"];
  const exposed = await h.call("puerts_widget_bind", {
    asset_path: WBP,
    expose_as_variable: EXPOSED,
  }, {
    label: "4a. expose the driveable widgets as members of the generated class",
    why: "widget_inspect reports is_variable and widget_build cannot write it, so without this every widget "
      + "the bridge authors is unreachable from a graph by name and no BindWidget can resolve",
  });
  if (exposed?.success === true) {
    const after = await h.call("puerts_widget_inspect", { asset_path: WBP }, {
      label: "4a. read the exposed variables back with the independent inspector",
    });
    const names = (after?.data?.variables ?? []).map((v) => v.name ?? v);
    h.check(EXPOSED.every((n) => names.includes(n)),
      "4a. every widget asked for is a variable on the asset, read back independently",
      JSON.stringify(names));

    const again = await h.call("puerts_widget_bind", {
      asset_path: WBP,
      expose_as_variable: EXPOSED,
    }, { label: "4a. rerunning the identical bind spec converges" });
    h.check(again?.data?.applied_change_count === 0 && again?.data?.converged === true,
      "4a. a converged rerun applied nothing and said so",
      `applied=${again?.data?.applied_change_count} converged=${again?.data?.converged}`);
    h.check(again?.data?.saved === false,
      "4a. a converged rerun saved nothing", String(again?.data?.saved));
  }

  h.note("4a. the property-binding half of this slice is unrun, not unsupported",
    "puerts_widget_bind takes bindings of {widget, property, source:{kind, name}}, but a source has to "
    + "already exist on WBP_SliceHUD. Authoring one is the widget event graph question step 4 measures, "
    + "so this slice exposes the widgets and stops rather than binding to a function it invented.");

  // A Widget Blueprint has an event graph. Whether the Blueprint tooling can
  // reach it is unmeasured, and it decides whether OnClicked is authorable at
  // all, so the slice probes it rather than assuming either answer.
  const probe = await h.call("puerts_graph_inspect", { asset_path: WBP }, {
    label: "4. probe whether the Blueprint inspector can read a Widget Blueprint's event graph",
    why: "if graph_inspect and blueprint_graph_patch accept a WidgetBlueprint, button handlers are already authorable; if not, that is the missing primitive",
  });
  if (probe !== null) {
    h.check(probe.success === true,
      "4. the Blueprint graph inspector accepts a Widget Blueprint",
      probe.success === true
        ? `graphs: ${JSON.stringify((probe.data?.graphs ?? []).map((g) => g.name ?? g))}`
        : "if this refuses, a widget event graph needs its own patch primitive (proposed: puerts_widget_graph_patch)");
  }

  // Getting the HUD on screen is a Blueprint graph job: create the widget and
  // add it to the viewport. This is authorable with the tools that exist, and
  // it is the step that proves the two builders compose.
  const cls = built?.data?.generated_class_path;
  if (cls) {
    const driver = await h.call("puerts_blueprint_build", {
      asset_path: DRIVER,
      parent_class: "Actor",
      graph: {
        nodes: [
          { id: "begin", type: "BeginPlay", x: 0, y: 0 },
          {
            id: "create", type: "CallFunction", x: 340, y: 0,
            params: { class: "WidgetBlueprintLibrary", function: "Create", WidgetType: cls },
          },
          { id: "show", type: "CallFunction", x: 720, y: 0, params: { class: "UserWidget", function: "AddToViewport" } },
        ],
        connections: [
          { from: "begin.then", to: "create.exec" },
          { from: "create.then", to: "show.exec" },
          { from: "create.ReturnValue", to: "show.self" },
        ],
      },
    }, {
      label: "5. author a driver Blueprint that creates the HUD and adds it to the viewport",
      why: "a widget nobody displays is not a UI slice",
    });
    if (driver?.success === true) {
      h.check((driver.data?.graph?.unresolved_connections ?? []).length === 0,
        "5. the create-and-show chain resolved to real pins",
        JSON.stringify(driver.data?.graph?.unresolved_connections ?? []).slice(0, 300));
    }
  }

  h.policy("6. see the HUD rendered",
    "the widget only draws in a running game. PIE is reserved for the user to ask for, and "
    + "puerts_viewport_screenshot captures the EDITOR viewport, which never contains a UMG widget. "
    + "There is no primitive that renders a widget to an image without entering play.");

  h.seal({ asset_path: WBP, inspect_tool: "puerts_widget_inspect", structure_hash: hash });
});
