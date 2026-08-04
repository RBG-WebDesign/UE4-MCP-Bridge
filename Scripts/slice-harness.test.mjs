#!/usr/bin/env node
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { isExpectedRefusal } from "./slice-harness.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
assert.equal(isExpectedRefusal({ success: false, errors: ["absent"] }), true);
assert.equal(isExpectedRefusal({ success: true }), false);
assert.equal(isExpectedRefusal(null), false);

const weapons = readFileSync(join(root, "Scripts", "feature-weapons-projectiles.mjs"), "utf8")
  .replaceAll("\r\n", "\n");
const projectileRead = weapons.indexOf('h.expectFailure("puerts_graph_inspect", {\n    asset_path: PROJECTILE');
const weaponRead = weapons.indexOf('h.expectFailure("puerts_graph_inspect", {\n    asset_path: WEAPON');
const weaponBuild = weapons.indexOf('h.call("puerts_blueprint_build"');
assert.ok(projectileRead >= 0 && projectileRead < weaponBuild);
assert.ok(weaponRead >= 0 && weaponRead < weaponBuild);

const hud = readFileSync(join(root, "Scripts", "feature-hud.mjs"), "utf8")
  .replaceAll("\r\n", "\n");
const hudRead = hud.indexOf('h.expectFailure("puerts_widget_inspect", {\n    asset_path: HUD');
const hostRead = hud.indexOf('h.expectFailure("puerts_graph_inspect", {\n    asset_path: HOST');
const hudBuild = hud.indexOf('h.call("puerts_widget_build"');
const hostBuild = hud.indexOf('h.call("puerts_blueprint_build"');
assert.ok(hudRead >= 0 && hudRead < hudBuild);
assert.ok(hostRead >= 0 && hostRead < hostBuild);

console.log("slice harness expected-refusal contract: PASS");
