#!/usr/bin/env node
// Feature acceptance 5: inventory and equipment state.

import { exerciseBlueprintState } from "./feature-blueprint-state.mjs";
import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const ASSET = `/Game/MCPGenerated/FeatureAcceptance/BP_InventoryEquipment_${runId}`;
const MARKER = `FEATURE_ITEM_EQUIPPED_${runId}`;
const controlSpec = { asset_path: ASSET, parent_class: "Actor",
  variables: [{ name: "EquippedCount", type: "int", default: 0, category: "Inventory" }],
  graph: { nodes: [{ id: "begin", type: "BeginPlay" },
    { id: "control", type: "PrintString", params: { InString: `FEATURE_INVENTORY_CONTROL_${runId}` } }],
  connections: [{ from: "begin.then", to: "control.exec" }] } };
const finalSpec = { asset_path: ASSET, parent_class: "Actor",
  variables: [
    { name: "EquippedCount", type: "int", default: 0, category: "Inventory" },
    { name: "InventoryCapacity", type: "int", default: 16, category: "Inventory" },
    { name: "EquippedItem", type: "string", default: "BridgeKey", category: "Inventory" },
  ], graph: { nodes: [
    { id: "begin", type: "BeginPlay" },
    { id: "count", type: "VariableGet", x: 0, y: 240, params: { var_name: "EquippedCount" } },
    { id: "increment", type: "Operator", x: 280, y: 240, params: { op: "add_int", B: 1 } },
    { id: "set", type: "VariableSet", x: 520, y: 0, params: { var_name: "EquippedCount" } },
    { id: "marker", type: "PrintString", x: 800, y: 0, params: { InString: MARKER } },
  ], connections: [
    { from: "begin.then", to: "set.exec" }, { from: "count.EquippedCount", to: "increment.A" },
    { from: "increment.ReturnValue", to: "set.EquippedCount" }, { from: "set.then", to: "marker.exec" },
  ] } };

await runSlice({ id: "feature-inventory-equipment", title: "inventory capacity and equipped item state",
  proves: "author, inspect, converge, cold-load and optionally observe equipment state" },
async (h) => exerciseBlueprintState(h, { assetPath: ASSET,
  actorClass: `${ASSET}.BP_InventoryEquipment_${runId}_C`, actorLabel: `FeatureInventory_${runId}`,
  controlSpec, finalSpec, expectedVariables: ["EquippedCount", "InventoryCapacity", "EquippedItem"], marker: MARKER,
  includePie, runtimeProperty: "EquippedCount", runtimeValue: 1 }));
