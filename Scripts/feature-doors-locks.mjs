#!/usr/bin/env node
// Feature acceptance 4: doors and locks.

import { exerciseBlueprintState } from "./feature-blueprint-state.mjs";
import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const ASSET = `/Game/MCPGenerated/FeatureAcceptance/BP_DoorLock_${runId}`;
const MARKER = `FEATURE_DOOR_OPENED_${runId}`;
const controlSpec = { asset_path: ASSET, parent_class: "Actor",
  variables: [{ name: "OpenCount", type: "int", default: 0, category: "Door" }],
  graph: { nodes: [{ id: "begin", type: "BeginPlay" },
    { id: "control", type: "PrintString", params: { InString: `FEATURE_DOOR_CONTROL_${runId}` } }],
  connections: [{ from: "begin.then", to: "control.exec" }] } };
const finalSpec = { asset_path: ASSET, parent_class: "Actor",
  components: [{ class: "SceneComponent", name: "DoorRoot" },
    { class: "StaticMeshComponent", name: "DoorMesh", attach_to: "DoorRoot",
      properties: { StaticMesh: "/Engine/BasicShapes/Cube.Cube" } }],
  variables: [
    { name: "OpenCount", type: "int", default: 0, category: "Door" },
    { name: "IsLocked", type: "bool", default: false, category: "Door" },
    { name: "RequiredKey", type: "string", default: "BridgeKey", category: "Door" },
  ], graph: { nodes: [
    { id: "begin", type: "BeginPlay" },
    { id: "count", type: "VariableGet", x: 0, y: 240, params: { var_name: "OpenCount" } },
    { id: "increment", type: "Operator", x: 280, y: 240, params: { op: "add_int", B: 1 } },
    { id: "set", type: "VariableSet", x: 520, y: 0, params: { var_name: "OpenCount" } },
    { id: "marker", type: "PrintString", x: 800, y: 0, params: { InString: MARKER } },
  ], connections: [
    { from: "begin.then", to: "set.exec" }, { from: "count.OpenCount", to: "increment.A" },
    { from: "increment.ReturnValue", to: "set.OpenCount" }, { from: "set.then", to: "marker.exec" },
  ] } };

await runSlice({ id: "feature-doors-locks", title: "a keyed door with inspectable open state",
  proves: "author door structure and lock data, inspect, converge, cold-load and optionally observe opening" },
async (h) => exerciseBlueprintState(h, { assetPath: ASSET,
  actorClass: `${ASSET}.BP_DoorLock_${runId}_C`, actorLabel: `FeatureDoor_${runId}`,
  controlSpec, finalSpec, expectedVariables: ["OpenCount", "IsLocked", "RequiredKey"], marker: MARKER,
  includePie, runtimeProperty: "OpenCount", runtimeValue: 1 }));
