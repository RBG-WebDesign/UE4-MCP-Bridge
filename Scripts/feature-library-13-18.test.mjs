#!/usr/bin/env node

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const contracts = {
  "feature-locomotion-animbp.mjs": ["puerts_anim_blueprint_build", "puerts_anim_blueprint_patch", "puerts_anim_blueprint_inspect"],
  "feature-montages-notifies.mjs": ["puerts_anim_blueprint_build", "puerts_anim_blueprint_inspect", "puerts_anim_montage_build"],
  "feature-material-effects.mjs": ["puerts_material_build", "puerts_material_inspect"],
  "feature-level-interaction.mjs": ["puerts_blueprint_build", "puerts_graph_inspect", "puerts_scene_batch", "puerts_scene_inspect"],
  "feature-sequencer-events.mjs": ["puerts_sequence_build", "puerts_sequence_inspect", "puerts_sequence_event_track_build"],
  "feature-packaged-demo.mjs": ["puerts_blueprint_build", "puerts_graph_inspect", "puerts_project_settings_maps", "puerts_project_package_start"],
};

for (const [file, required] of Object.entries(contracts)) {
  const source = readFileSync(join(root, "Scripts", file), "utf8");
  assert.match(source, /runSlice\(/, `${file} must use runSlice`);
  assert.match(source, /h\.assertFreshFixture\(/, `${file} must prove independent reader refusal before creation`);
  assert.match(source, /h\.seal\(/, `${file} must seal warm evidence for a cold read`);
  assert.match(source, /converg|rerun/i, `${file} must check desired-state convergence`);
  for (const tool of required) assert.ok(source.includes(tool), `${file} must name ${tool}`);
  if (source.includes("puerts_pie_start")) {
    assert.ok(source.includes('process.argv.includes("--pie")'), `${file} must require --pie`);
    assert.ok(source.includes("if (!includePie)"), `${file} must keep PIE off by default`);
  }
  assert.ok(!source.includes(String.fromCharCode(0x2014)), `${file} must not contain em dashes`);
}

const guardedReruns = {
  "feature-locomotion-animbp.mjs": "if (again?.success !== true) return;",
  "feature-montages-notifies.mjs": "if (again?.success !== true) return;",
  "feature-material-effects.mjs": "if (again?.success !== true) return;",
  "feature-sequencer-events.mjs": "if (again?.success !== true) return;",
  "feature-level-interaction.mjs": "if (placedAgain?.success !== true) return;",
  "feature-packaged-demo.mjs": "if (rebuilt?.success !== true) return;",
};
for (const [file, guard] of Object.entries(guardedReruns)) {
  assert.ok(readFileSync(join(root, "Scripts", file), "utf8").includes(guard), `${file} must stop after a failed dependent writer`);
}

const montage = readFileSync(join(root, "Scripts", "feature-montages-notifies.mjs"), "utf8");
assert.ok(montage.includes("FAnimLinkableElement") && montage.includes("next-section chain"), "montage blocker must name the UE4.27 atomicity boundary");
const sequence = readFileSync(join(root, "Scripts", "feature-sequencer-events.mjs"), "utf8");
assert.ok(sequence.includes("director Blueprint") && sequence.includes("never execute"), "event-track blocker must name the director endpoint gap");
const packaged = readFileSync(join(root, "Scripts", "feature-packaged-demo.mjs"), "utf8");
assert.ok(packaged.includes("async job API") && packaged.includes("project_settings_maps"), "packaged demo must name both release gates");

console.log("feature library 13-18 source contract: PASS");
