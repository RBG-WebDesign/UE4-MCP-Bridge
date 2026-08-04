#!/usr/bin/env node

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const contracts = {
  "feature-health-damage.mjs": ["Health", "DamageAmount", "subtract_int"],
  "feature-sprint-stamina.mjs": ["Stamina", "SprintCost", "SprintSpeed"],
  "feature-interaction.mjs": ["InteractionCount", "InteractionPrompt", "InteractionRange"],
  "feature-doors-locks.mjs": ["OpenCount", "IsLocked", "RequiredKey"],
  "feature-inventory-equipment.mjs": ["EquippedCount", "InventoryCapacity", "EquippedItem"],
  "feature-save-load.mjs": ["SaveRevision", "SlotName", "DoesSaveGameExist"],
};

for (const [file, required] of Object.entries(contracts)) {
  const source = readFileSync(join(root, "Scripts", file), "utf8");
  assert.match(source, /runSlice\(/, `${file} must call runSlice`);
  assert.ok(source.includes("exerciseBlueprintState"), `${file} must use the shared evidence protocol`);
  assert.ok(source.includes('process.argv.includes("--pie")'), `${file} must gate PIE explicitly`);
  for (const marker of required) assert.ok(source.includes(marker), `${file} must name ${marker}`);
  assert.ok(!source.includes("puerts_class_defaults_patch"), `${file} must not leave an unrestored CDO write`);
  assert.ok(!source.includes(String.fromCharCode(0x2014)), `${file} must not contain em dashes`);
}

const helper = readFileSync(join(root, "Scripts", "feature-blueprint-state.mjs"), "utf8");
for (const required of [
  "assertFreshFixture", "minimal state-moving control", "puerts_graph_inspect",
  "graph hash converged", "wrote no new bytes", "puerts_pie_agent_query", "h.seal",
]) assert.ok(helper.includes(required), `shared feature harness must include ${required}`);
assert.ok(helper.indexOf("if (!includePie)") < helper.indexOf('h.call("puerts_pie_start"'),
  "shared feature harness must refuse PIE before the start call");
assert.ok(!helper.includes(String.fromCharCode(0x2014)), "shared feature harness must not contain em dashes");

console.log("feature library 1-6 source contract: PASS");
