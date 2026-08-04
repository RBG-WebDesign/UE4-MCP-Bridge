#!/usr/bin/env node
// Feature acceptance 2: sprint and stamina state.

import { exerciseBlueprintState } from "./feature-blueprint-state.mjs";
import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const ASSET = `/Game/MCPGenerated/FeatureAcceptance/BP_SprintStamina_${runId}`;
const MARKER = `FEATURE_SPRINT_DRAIN_${runId}`;
const controlSpec = { asset_path: ASSET, parent_class: "Actor",
  variables: [{ name: "Stamina", type: "int", default: 100, category: "Movement" }],
  graph: { nodes: [{ id: "begin", type: "BeginPlay" },
    { id: "control", type: "PrintString", params: { InString: `FEATURE_SPRINT_CONTROL_${runId}` } }],
  connections: [{ from: "begin.then", to: "control.exec" }] } };
const finalSpec = { asset_path: ASSET, parent_class: "Actor",
  variables: [
    { name: "Stamina", type: "int", default: 100, category: "Movement" },
    { name: "SprintCost", type: "int", default: 15, category: "Movement" },
    { name: "SprintSpeed", type: "float", default: 900, category: "Movement" },
  ], graph: { nodes: [
    { id: "begin", type: "BeginPlay" },
    { id: "stamina", type: "VariableGet", x: 0, y: 240, params: { var_name: "Stamina" } },
    { id: "drain", type: "Operator", x: 280, y: 240, params: { op: "subtract_int", B: 15 } },
    { id: "set", type: "VariableSet", x: 520, y: 0, params: { var_name: "Stamina" } },
    { id: "marker", type: "PrintString", x: 800, y: 0, params: { InString: MARKER } },
  ], connections: [
    { from: "begin.then", to: "set.exec" }, { from: "stamina.Stamina", to: "drain.A" },
    { from: "drain.ReturnValue", to: "set.Stamina" }, { from: "set.then", to: "marker.exec" },
  ] } };

await runSlice({ id: "feature-sprint-stamina", title: "sprint stamina consumption",
  proves: "author, inspect, converge, cold-load and optionally observe a stamina transition" },
async (h) => exerciseBlueprintState(h, { assetPath: ASSET,
  actorClass: `${ASSET}.BP_SprintStamina_${runId}_C`, actorLabel: `FeatureSprint_${runId}`,
  controlSpec, finalSpec, expectedVariables: ["Stamina", "SprintCost", "SprintSpeed"], marker: MARKER,
  includePie, runtimeProperty: "Stamina", runtimeValue: 85 }));
