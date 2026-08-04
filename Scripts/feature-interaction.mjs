#!/usr/bin/env node
// Feature acceptance 3: interaction state.

import { exerciseBlueprintState } from "./feature-blueprint-state.mjs";
import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const ASSET = `/Game/MCPGenerated/FeatureAcceptance/BP_Interaction_${runId}`;
const MARKER = `FEATURE_INTERACTION_USED_${runId}`;
const controlSpec = { asset_path: ASSET, parent_class: "Actor",
  variables: [{ name: "InteractionCount", type: "int", default: 0, category: "Interaction" }],
  graph: { nodes: [{ id: "begin", type: "BeginPlay" },
    { id: "control", type: "PrintString", params: { InString: `FEATURE_INTERACTION_CONTROL_${runId}` } }],
  connections: [{ from: "begin.then", to: "control.exec" }] } };
const finalSpec = { asset_path: ASSET, parent_class: "Actor",
  variables: [
    { name: "InteractionCount", type: "int", default: 0, category: "Interaction" },
    { name: "InteractionPrompt", type: "string", default: "Use", category: "Interaction" },
    { name: "InteractionRange", type: "float", default: 200, category: "Interaction" },
  ], graph: { nodes: [
    { id: "begin", type: "BeginPlay" },
    { id: "count", type: "VariableGet", x: 0, y: 240, params: { var_name: "InteractionCount" } },
    { id: "increment", type: "Operator", x: 280, y: 240, params: { op: "add_int", B: 1 } },
    { id: "set", type: "VariableSet", x: 520, y: 0, params: { var_name: "InteractionCount" } },
    { id: "marker", type: "PrintString", x: 800, y: 0, params: { InString: MARKER } },
  ], connections: [
    { from: "begin.then", to: "set.exec" }, { from: "count.InteractionCount", to: "increment.A" },
    { from: "increment.ReturnValue", to: "set.InteractionCount" }, { from: "set.then", to: "marker.exec" },
  ] } };

await runSlice({ id: "feature-interaction", title: "an inspectable interaction state transition",
  proves: "author, inspect, converge, cold-load and optionally observe interaction state" },
async (h) => exerciseBlueprintState(h, { assetPath: ASSET,
  actorClass: `${ASSET}.BP_Interaction_${runId}_C`, actorLabel: `FeatureInteraction_${runId}`,
  controlSpec, finalSpec, expectedVariables: ["InteractionCount", "InteractionPrompt", "InteractionRange"], marker: MARKER,
  includePie, runtimeProperty: "InteractionCount", runtimeValue: 1 }));
