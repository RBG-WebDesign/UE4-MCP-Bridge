#!/usr/bin/env node
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { collectWidgetNames, isExpectedRefusal } from "./slice-harness.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
assert.equal(isExpectedRefusal({ success: false, errors: ["absent"] }), true);
assert.equal(isExpectedRefusal({ success: true }), false);
assert.equal(isExpectedRefusal(null), false);
assert.deepEqual(collectWidgetNames({ name: "Root", children: [
  { name: "First" },
  { name: "Panel", children: [{ name: "Nested" }] },
] }), ["Root", "First", "Panel", "Nested"]);

const harness = readFileSync(join(root, "Scripts", "slice-harness.mjs"), "utf8");
assert.ok(harness.includes('async assertFreshFixture(assetPath, inspectTool)'));
assert.ok(harness.includes('await h.expectFailure(inspectTool, { asset_path: assetPath }'));

const weapons = readFileSync(join(root, "Scripts", "feature-weapons-projectiles.mjs"), "utf8")
  .replaceAll("\r\n", "\n");
const projectileRead = weapons.indexOf('h.assertFreshFixture(PROJECTILE, "puerts_graph_inspect")');
const weaponRead = weapons.indexOf('h.assertFreshFixture(WEAPON, "puerts_graph_inspect")');
const weaponBuild = weapons.indexOf('h.call("puerts_blueprint_build"');
assert.ok(projectileRead >= 0 && projectileRead < weaponBuild);
assert.ok(weaponRead >= 0 && weaponRead < weaponBuild);

const hud = readFileSync(join(root, "Scripts", "feature-hud.mjs"), "utf8")
  .replaceAll("\r\n", "\n");
const hudRead = hud.indexOf('h.assertFreshFixture(HUD, "puerts_widget_inspect")');
const hostRead = hud.indexOf('h.assertFreshFixture(HOST, "puerts_graph_inspect")');
const hudBuild = hud.indexOf('h.call("puerts_widget_build"');
const hostBuild = hud.indexOf('h.call("puerts_blueprint_build"');
assert.ok(hudRead >= 0 && hudRead < hudBuild);
assert.ok(hostRead >= 0 && hostRead < hostBuild);

for (const file of [
  "feature-menus.mjs",
  "feature-controller-navigation.mjs",
  "feature-dialogue.mjs",
  "feature-ai-behavior.mjs",
]) {
  const source = readFileSync(join(root, "Scripts", file), "utf8")
    .replaceAll("\r\n", "\n");
  const fresh = source.indexOf("h.assertFreshFixture(");
  const build = source.search(/h\.call\("puerts_(?:widget|blueprint|behavior_tree)_build"/);
  assert.ok(fresh >= 0 && fresh < build, `${file} proves fixture freshness before construction`);
  assert.match(source,
    /const (\w+) = await h\.call\("puerts_(?:widget|blueprint|behavior_tree)_build",[\s\S]{0,220}\);\n  if \(\1\?\.success !== true\) return;/,
    `${file} requires the state-moving control writer to succeed`);
  assert.ok(source.includes('process.argv.includes("--pie")'), `${file} requires an explicit PIE flag`);
  assert.ok(source.includes("h.seal({"), `${file} seals warm evidence for cold verification`);
}

console.log("slice harness expected-refusal contract: PASS");
