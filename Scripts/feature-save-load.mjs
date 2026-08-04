#!/usr/bin/env node
// Feature acceptance 6: save and load state data.

import { exerciseBlueprintState } from "./feature-blueprint-state.mjs";
import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const ASSET = `/Game/MCPGenerated/FeatureAcceptance/BP_SaveLoad_${runId}`;
const MARKER = `FEATURE_SAVE_LOAD_READY_${runId}`;
const controlSpec = { asset_path: ASSET, parent_class: "Actor",
  variables: [{ name: "SaveRevision", type: "int", default: 0, category: "Save" }],
  graph: { nodes: [{ id: "begin", type: "BeginPlay" },
    { id: "control", type: "PrintString", params: { InString: `FEATURE_SAVE_CONTROL_${runId}` } }],
  connections: [{ from: "begin.then", to: "control.exec" }] } };
const finalSpec = { asset_path: ASSET, parent_class: "Actor",
  variables: [
    { name: "SaveRevision", type: "int", default: 0, category: "Save" },
    { name: "SlotName", type: "string", default: `MCPFeature_${runId}`, category: "Save" },
    { name: "SavedHealth", type: "int", default: 75, category: "Save" },
  ], graph: { nodes: [
    { id: "begin", type: "BeginPlay" },
    { id: "revision", type: "VariableGet", x: 0, y: 240, params: { var_name: "SaveRevision" } },
    { id: "increment", type: "Operator", x: 280, y: 240, params: { op: "add_int", B: 1 } },
    { id: "set", type: "VariableSet", x: 520, y: 0, params: { var_name: "SaveRevision" } },
    { id: "exists", type: "CallFunction", x: 520, y: 320,
      params: { class: "GameplayStatics", function: "DoesSaveGameExist", SlotName: `MCPFeature_${runId}`, UserIndex: 0 } },
    { id: "marker", type: "PrintString", x: 800, y: 0, params: { InString: MARKER } },
  ], connections: [
    { from: "begin.then", to: "set.exec" }, { from: "revision.SaveRevision", to: "increment.A" },
    { from: "increment.ReturnValue", to: "set.SaveRevision" }, { from: "set.then", to: "marker.exec" },
  ] } };

await runSlice({ id: "feature-save-load", title: "save-slot data and load preflight state",
  proves: "author save data, resolve the engine save-slot query, inspect, converge, cold-load and optionally observe state" },
async (h) => exerciseBlueprintState(h, { assetPath: ASSET,
  actorClass: `${ASSET}.BP_SaveLoad_${runId}_C`, actorLabel: `FeatureSaveLoad_${runId}`,
  controlSpec, finalSpec, expectedVariables: ["SaveRevision", "SlotName", "SavedHealth"], marker: MARKER,
  includePie, runtimeProperty: "SaveRevision", runtimeValue: 1 }));
