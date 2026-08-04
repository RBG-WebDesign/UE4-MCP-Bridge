#!/usr/bin/env node
// Feature acceptance 1: health and damage.

import { exerciseBlueprintState } from "./feature-blueprint-state.mjs";
import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const ASSET = `/Game/MCPGenerated/FeatureAcceptance/BP_HealthDamage_${runId}`;
const MARKER = `FEATURE_HEALTH_DAMAGED_${runId}`;
const controlSpec = {
  asset_path: ASSET, parent_class: "Actor",
  variables: [{ name: "Health", type: "int", default: 100, category: "Combat" }],
  graph: { nodes: [{ id: "begin", type: "BeginPlay" },
    { id: "control", type: "PrintString", params: { InString: `FEATURE_HEALTH_CONTROL_${runId}` } }],
  connections: [{ from: "begin.then", to: "control.exec" }] },
};
const finalSpec = {
  asset_path: ASSET, parent_class: "Actor",
  variables: [
    { name: "Health", type: "int", default: 100, category: "Combat" },
    { name: "DamageAmount", type: "int", default: 25, category: "Combat" },
  ],
  graph: { nodes: [
    { id: "begin", type: "BeginPlay", x: 0, y: 0 },
    { id: "health", type: "VariableGet", x: 0, y: 240, params: { var_name: "Health" } },
    { id: "subtract", type: "Operator", x: 280, y: 240, params: { op: "subtract_int", B: 25 } },
    { id: "set", type: "VariableSet", x: 520, y: 0, params: { var_name: "Health" } },
    { id: "marker", type: "PrintString", x: 800, y: 0, params: { InString: MARKER } },
  ], connections: [
    { from: "begin.then", to: "set.exec" }, { from: "health.Health", to: "subtract.A" },
    { from: "subtract.ReturnValue", to: "set.Health" }, { from: "set.then", to: "marker.exec" },
  ] },
};

await runSlice({ id: "feature-health-damage", title: "health reduced by authored damage",
  proves: "author, inspect, converge, cold-load and optionally observe a damage transition" },
async (h) => exerciseBlueprintState(h, { assetPath: ASSET,
  actorClass: `${ASSET}.BP_HealthDamage_${runId}_C`, actorLabel: `FeatureHealth_${runId}`,
  controlSpec, finalSpec, expectedVariables: ["Health", "DamageAmount"], marker: MARKER,
  includePie, runtimeProperty: "Health", runtimeValue: 75 }));
