#!/usr/bin/env node
// Feature acceptance 7: weapons and projectiles.
//
// Warm builds fresh projectile and weapon Blueprints, proves the weapon can
// decrement ammunition and spawn the projectile class, then checks a no-op
// rerun through graph_inspect and the package bytes. Cold re-reads the sealed
// assets after an editor restart. PIE is off unless --pie is passed.

import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const PROJECTILE = `/Game/MCPGenerated/FeatureAcceptance/BP_Projectile_${runId}`;
const WEAPON = `/Game/MCPGenerated/FeatureAcceptance/BP_Weapon_${runId}`;
const PROJECTILE_CLASS = `${PROJECTILE}.BP_Projectile_${runId}_C`;
const WEAPON_CLASS = `${WEAPON}.BP_Weapon_${runId}_C`;
const ACTOR_LABEL = `FeatureWeapon_${runId}`;

async function assertUnused(h, assetPath) {
  const name = assetPath.split("/").pop();
  const found = await h.call("puerts_find_assets", {
    path: "/Game/MCPGenerated/FeatureAcceptance",
    name,
    recursive: false,
    limit: 2,
  }, { label: `assert the fresh fixture is unused: ${name}` });
  const unused = found?.success === true && (found.data?.assets ?? []).length === 0;
  h.check(unused, `the fixture path ${assetPath} did not exist before the control`);
  return unused;
}

const PROJECTILE_SPEC = {
  asset_path: PROJECTILE,
  parent_class: "Actor",
  components: [
    {
      class: "SphereComponent",
      name: "Collision",
      properties: {
        SphereRadius: 12,
        BodyInstance: { CollisionProfileName: "BlockAllDynamic" },
      },
    },
    {
      class: "StaticMeshComponent",
      name: "Tracer",
      attach_to: "Collision",
      properties: {
        StaticMesh: "/Engine/BasicShapes/Sphere.Sphere",
        RelativeScale3D: { x: 0.12, y: 0.12, z: 0.12 },
      },
    },
    {
      class: "ProjectileMovementComponent",
      name: "Movement",
      properties: { InitialSpeed: 3000, MaxSpeed: 3000, ProjectileGravityScale: 0 },
    },
  ],
  variables: [
    { name: "Damage", type: "float", default: 25, category: "Weapon" },
  ],
  graph: {
    nodes: [
      { id: "begin", type: "BeginPlay", x: 0, y: 0 },
      { id: "alive", type: "PrintString", x: 320, y: 0, params: { InString: `FEATURE_PROJECTILE_ALIVE_${runId}` } },
    ],
    connections: [{ from: "begin.then", to: "alive.exec" }],
  },
};

const WEAPON_CONTROL = {
  asset_path: WEAPON,
  parent_class: "Actor",
  variables: [{ name: "Ammo", type: "int", default: 1, category: "Weapon" }],
  graph: {
    nodes: [
      { id: "begin", type: "BeginPlay", x: 0, y: 0 },
      { id: "control", type: "PrintString", x: 320, y: 0, params: { InString: `FEATURE_WEAPON_CONTROL_${runId}` } },
    ],
    connections: [{ from: "begin.then", to: "control.exec" }],
  },
};

const WEAPON_SPEC = {
  asset_path: WEAPON,
  parent_class: "Actor",
  components: [
    { class: "SceneComponent", name: "Root" },
    { class: "ArrowComponent", name: "Muzzle", attach_to: "Root" },
  ],
  variables: [
    { name: "Ammo", type: "int", default: 5, category: "Weapon" },
    { name: "ProjectileClass", type: "class:/Script/Engine.Actor", default: PROJECTILE_CLASS, category: "Weapon" },
    { name: "FireRate", type: "float", default: 0.2, category: "Weapon" },
  ],
  graph: {
    nodes: [
      { id: "fire", type: "InputKey", x: 0, y: 0, params: { fkey_name: "LeftMouseButton" } },
      { id: "getAmmo", type: "VariableGet", x: 0, y: 260, params: { var_name: "Ammo" } },
      { id: "hasAmmo", type: "Operator", x: 300, y: 260, params: { op: "greater_int", B: 0 } },
      { id: "branch", type: "Branch", x: 380, y: 0 },
      { id: "decrement", type: "Operator", x: 650, y: 260, params: { op: "subtract_int", B: 1 } },
      { id: "setAmmo", type: "VariableSet", x: 760, y: 0, params: { var_name: "Ammo" } },
      { id: "transform", type: "MakeStruct", x: 760, y: 360, params: { structType: "/Script/CoreUObject.Transform" } },
      { id: "spawn", type: "SpawnActor", x: 1080, y: 0, params: { actorClass: PROJECTILE_CLASS } },
      { id: "fired", type: "PrintString", x: 1430, y: 0, params: { InString: `FEATURE_WEAPON_FIRED_${runId}` } },
      { id: "empty", type: "PrintString", x: 760, y: 520, params: { InString: "FEATURE_WEAPON_EMPTY" } },
    ],
    connections: [
      { from: "fire.Pressed", to: "branch.exec" },
      { from: "getAmmo.Ammo", to: "hasAmmo.A" },
      { from: "hasAmmo.ReturnValue", to: "branch.Condition" },
      { from: "getAmmo.Ammo", to: "decrement.A" },
      { from: "branch.then", to: "setAmmo.exec" },
      { from: "decrement.ReturnValue", to: "setAmmo.Ammo" },
      { from: "setAmmo.then", to: "spawn.exec" },
      { from: "transform.Transform", to: "spawn.Spawn Transform" },
      { from: "spawn.then", to: "fired.exec" },
      { from: "branch.else", to: "empty.exec" },
    ],
  },
};

await runSlice({
  id: "feature-weapons-projectiles",
  title: "a weapon that consumes ammunition and spawns a configured projectile",
  proves: "author projectile movement and collision, wire weapon input to ammunition and spawning, inspect both assets, converge, and optionally prove the runtime join",
}, async (h) => {
  if (!await assertUnused(h, PROJECTILE) || !await assertUnused(h, WEAPON)) return;

  const projectile = await h.call("puerts_blueprint_build", PROJECTILE_SPEC, {
    label: "1. build the projectile as the state-moving positive control",
  });
  if (projectile?.success !== true) return;
  const projectileRead = await h.call("puerts_graph_inspect", { asset_path: PROJECTILE, include_pins: true }, {
    label: "2. inspect projectile collision, movement and damage independently",
  });
  const projectileComponents = (projectileRead?.data?.components ?? []).map((c) => c.name);
  h.check(["Collision", "Tracer", "Movement"].every((name) => projectileComponents.includes(name)),
    "2. the projectile has collision, visual and movement components", JSON.stringify(projectileComponents));
  h.check((projectileRead?.data?.variables ?? []).some((v) => v.name === "Damage"),
    "2. the projectile exposes its damage value");
  const projectileHash = projectileRead?.data?.graph?.structure_hash_sha1 ?? null;

  const control = await h.call("puerts_blueprint_build", WEAPON_CONTROL, {
    label: "3. build the minimal weapon control",
  });
  if (control?.success !== true) return;
  const controlRead = await h.call("puerts_graph_inspect", { asset_path: WEAPON, include_pins: true }, {
    label: "3. inspect the control independently before moving state",
  });
  const controlHash = controlRead?.data?.graph?.structure_hash_sha1 ?? null;

  const weapon = await h.call("puerts_blueprint_build", WEAPON_SPEC, {
    label: "4. replace the control with the complete weapon graph",
  });
  if (weapon?.success !== true) return;
  h.check(weapon.data?.compile_status === "UpToDate", "4. the weapon compiles clean", String(weapon.data?.compile_status));
  h.check((weapon.data?.graph?.unresolved_connections ?? []).length === 0,
    "4. every fire, ammo and projectile-spawn connection resolved",
    JSON.stringify(weapon.data?.graph?.unresolved_connections ?? []));

  const weaponRead = await h.call("puerts_graph_inspect", { asset_path: WEAPON, include_pins: true }, {
    label: "5. inspect the complete weapon independently",
  });
  const weaponHash = weaponRead?.data?.graph?.structure_hash_sha1 ?? null;
  const weaponVariables = (weaponRead?.data?.variables ?? []).map((v) => v.name);
  h.check(typeof weaponHash === "string" && weaponHash !== controlHash,
    "5. the control moved to the requested weapon state", `${controlHash} -> ${weaponHash}`);
  h.check(["Ammo", "ProjectileClass", "FireRate"].every((name) => weaponVariables.includes(name)),
    "5. the weapon exposes ammo, projectile class and fire rate", JSON.stringify(weaponVariables));
  h.check(JSON.stringify(weaponRead?.data?.graph?.nodes ?? []).includes(PROJECTILE_CLASS),
    "5. independent pin read-back names the generated projectile class");

  const weaponSha = h.fileSha(WEAPON);
  const projectileSha = h.fileSha(PROJECTILE);
  const weaponRerun = await h.call("puerts_blueprint_build", WEAPON_SPEC, {
    label: "6. rerun the identical weapon spec",
  });
  const projectileRerun = await h.call("puerts_blueprint_build", PROJECTILE_SPEC, {
    label: "6. rerun the identical projectile spec",
  });
  if (weaponRerun?.success === true && projectileRerun?.success === true) {
    const weaponAgain = await h.call("puerts_graph_inspect", { asset_path: WEAPON }, { label: "6. inspect the weapon rerun" });
    const projectileAgain = await h.call("puerts_graph_inspect", { asset_path: PROJECTILE }, { label: "6. inspect the projectile rerun" });
    h.check(weaponAgain?.data?.graph?.structure_hash_sha1 === weaponHash,
      "6. the weapon graph hash converged");
    h.check(projectileAgain?.data?.graph?.structure_hash_sha1 === projectileHash,
      "6. the projectile graph hash converged");
    h.check(weaponSha === null || h.fileSha(WEAPON) === weaponSha,
      "6. the weapon rerun wrote no new bytes");
    h.check(projectileSha === null || h.fileSha(PROJECTILE) === projectileSha,
      "6. the projectile rerun wrote no new bytes");
  }

  const defaults = await h.call("puerts_class_defaults_patch", {
    asset_path: WEAPON,
    properties: { AutoReceiveInput: "Player0" },
  }, { label: "7. enable Player 0 input on the fresh weapon fixture" });
  if (defaults?.success === true) {
    const cdo = defaults.data?.class_default_object;
    if (cdo) {
      const input = await h.call("puerts_read_property", { object_path: cdo, property: "AutoReceiveInput" }, {
        label: "7. read AutoReceiveInput independently from the CDO",
      });
      h.check(JSON.stringify(input?.data?.value).includes("Player0") || input?.data?.value === 1,
        "7. the weapon CDO independently reports Player 0 input");
    }
    const defaultsRerun = await h.call("puerts_class_defaults_patch", {
      asset_path: WEAPON,
      properties: { AutoReceiveInput: "Player0" },
    }, { label: "7. rerun the identical weapon class defaults" });
    h.check(defaultsRerun?.data?.applied_property_count === 0 && defaultsRerun?.data?.saved === false,
      "7. the class-default rerun converged without saving",
      `applied=${defaultsRerun?.data?.applied_property_count} saved=${defaultsRerun?.data?.saved}`);
  }

  if (!includePie) {
    h.policy("8. fire the weapon and observe the projectile",
      "PIE is off by default. Re-run with --pie only after explicit user authorization.");
  } else {
    const placed = await h.call("puerts_scene_batch", {
      operations: [{ op: "upsert_actor", label: ACTOR_LABEL, class: WEAPON_CLASS, location: { x: 0, y: 0, z: 150 } }],
    }, { label: "8. place the fresh weapon fixture" });
    let started = false;
    try {
      if (placed?.success === true) {
        const start = await h.call("puerts_pie_start", {}, { label: "8. start explicitly authorized PIE" });
        started = start?.success === true;
      }
      if (started) {
        await new Promise((resolve) => setTimeout(resolve, 1200));
        await h.call("puerts_pie_agent_control", { op: "press", key: "LeftMouseButton" }, {
          label: "8. press fire through the PIE input agent",
        });
        await new Promise((resolve) => setTimeout(resolve, 250));
        const ammo = await h.call("puerts_pie_agent_query", {
          op: "read_property", actor: ACTOR_LABEL, property: "Ammo",
        }, { label: "8. read live ammunition independently" });
        h.check(ammo?.data?.value === 4, "8. firing moved live ammunition from 5 to 4", String(ammo?.data?.value));
        const observed = await h.call("puerts_pie_agent_query", {
          op: "observe", class_filter: `BP_Projectile_${runId}_C`, log_lines: 50,
        }, { label: "8. observe the spawned projectile independently" });
        h.check(JSON.stringify(observed?.data ?? {}).includes(`BP_Projectile_${runId}`),
          "8. the runtime observation contains the spawned projectile class");
      }
    } finally {
      if (started) await h.call("puerts_pie_stop", {}, { label: "8. stop the explicitly authorized PIE session" });
    }
  }

  h.seal({ asset_path: PROJECTILE, inspect_tool: "puerts_graph_inspect", structure_hash: projectileHash });
  h.seal({ asset_path: WEAPON, inspect_tool: "puerts_graph_inspect", structure_hash: weaponHash });
});
