#!/usr/bin/env node
// Vertical slice 6 of 7: LEVELS.
//
// One prompt: "lay out a lit courtyard: four pillars in a square, a directional
// light, a sky light, a warm point light in the middle and a trigger volume by
// the entrance, all filed under a Courtyard folder, then save the level."
//
// What a pass would prove: a scene can be described as desired state, placed in
// one transactional call, read back by an independent inspector, and saved. The
// AGENTS.md trigger-volume rule is part of the slice, not an afterthought: a
// volume placed on the PlayerStart never fires and the harness measures it.
//
//   node Scripts/slice-level.mjs
//   node Scripts/slice-level.mjs --phase=cold

import { runSlice } from "./slice-harness.mjs";

const PILLARS = [
  { label: "Pillar_NE", location: { x: 600, y: 600, z: 0 } },
  { label: "Pillar_NW", location: { x: 600, y: -600, z: 0 } },
  { label: "Pillar_SE", location: { x: -600, y: 600, z: 0 } },
  { label: "Pillar_SW", location: { x: -600, y: -600, z: 0 } },
];
const TRIGGER_LOCATION = { x: 1400, y: 0, z: 100 };
const TRIGGER_EXTENT = { x: 200, y: 200, z: 200 };

await runSlice({
  id: "level",
  title: "a scene of several actors with lighting and a volume, placed and verified",
  proves: "describe a scene as desired state, place it in one call, read it back independently, and save the level",
}, async (h) => {
  const operations = [
    ...PILLARS.map((p) => ({
      op: "upsert_actor",
      label: p.label,
      class: "/Script/Engine.StaticMeshActor",
      location: p.location,
      scale: { x: 1, y: 1, z: 4 },
      folder: "Courtyard/Structure",
      properties: { "StaticMeshComponent.StaticMesh": "/Engine/BasicShapes/Cube.Cube" },
    })),
    {
      op: "upsert_actor", label: "CourtyardSun", class: "/Script/Engine.DirectionalLight",
      rotation: { pitch: -42, yaw: 25, roll: 0 }, folder: "Courtyard/Lighting",
      properties: { "LightComponent.Intensity": 6 },
    },
    {
      op: "upsert_actor", label: "CourtyardSky", class: "/Script/Engine.SkyLight",
      location: { x: 0, y: 0, z: 400 }, folder: "Courtyard/Lighting",
    },
    {
      op: "upsert_actor", label: "CourtyardLamp", class: "/Script/Engine.PointLight",
      location: { x: 0, y: 0, z: 300 }, folder: "Courtyard/Lighting",
      properties: { "LightComponent.Intensity": 8000 },
    },
    {
      op: "upsert_actor", label: "CourtyardEntryTrigger", class: "/Script/Engine.TriggerBox",
      location: TRIGGER_LOCATION, folder: "Courtyard/Gameplay",
      properties: { "CollisionComponent.BoxExtent": TRIGGER_EXTENT },
    },
  ];

  // The AGENTS.md rule is checkable before anything is placed, with tools that
  // exist today. A trigger volume on top of a PlayerStart never fires
  // OnBeginOverlap, and the whole scene would still report success.
  const starts = await h.call("puerts_find_actors", { type: "PlayerStart" }, {
    label: "1. find the PlayerStart the trigger volume must stay away from",
  });
  const start = (starts?.data?.actors ?? [])[0];
  if (start) {
    const where = await h.call("puerts_read_property", {
      actor: start.name ?? start.label ?? start,
      property: "ActorLocation",
    }, { label: "1. read the PlayerStart location" });
    const p = where?.data?.value ?? where?.data ?? null;
    if (p && typeof p.x === "number") {
      const d = Math.hypot(p.x - TRIGGER_LOCATION.x, p.y - TRIGGER_LOCATION.y, p.z - TRIGGER_LOCATION.z);
      const clearance = 1.5 * Math.max(TRIGGER_EXTENT.x, TRIGGER_EXTENT.y, TRIGGER_EXTENT.z);
      h.check(d >= clearance,
        "1. the planned trigger volume clears the PlayerStart by 1.5x its extent",
        `${Math.round(d)} uu vs the ${clearance} uu the rule requires`);
    } else {
      h.note("1. PlayerStart clearance not measured", "the location read did not return a vector");
    }
  } else if (starts?.success === true) {
    h.note("1. no PlayerStart in this level", "the clearance rule is vacuously satisfied");
  }

  const placed = await h.call("puerts_scene_batch", { operations, verify: true }, {
    label: "2. place the whole courtyard in one desired-state call",
    why: "puerts_spawn_actor takes a class path and nothing else: it cannot label an actor, cannot scale it, "
      + "cannot file it in an outliner folder, cannot set a volume's extent and cannot batch. Seven separate "
      + "spawns with no way to name what they produced is not a scene-authoring primitive",
    // Every one of those four abilities shipped in the legacy catalog and did
    // not survive the native migration. The compat alias says so in its own
    // description: scale, name and folder "are refused rather than dropped".
    // This is a capability regression to undo, not a feature to invent.
    legacy: ["actor_spawn", "batch_spawn", "actor_organize"],
    request: {
      tool: "puerts_scene_batch",
      owner: "lane L (settled, implemented_unverified)",
      params: { operations: "[{op:'upsert_actor'|'delete_actor', select?, label?, class?, location?, rotation?, scale?, folder?, tags?, attach_to?, properties?, components?}]", plan_only: "boolean", verify: "boolean" },
      note: "does NOT save the level; it reports level_package_dirty and puerts_save persists it",
    },
  });
  if (placed?.success === true) {
    h.check((placed.data?.verification_mismatches ?? []).length === 0,
      "2. the batch verified every operation it applied",
      JSON.stringify(placed.data?.verification_mismatches ?? []).slice(0, 300));
    h.check(placed.data?.applied_operation_count === operations.length,
      "2. every operation in the batch applied",
      `${placed.data?.applied_operation_count} of ${operations.length}`);
  }

  const read = await h.call("puerts_scene_inspect", { include_components: true }, {
    label: "3. read the level back with the independent scene inspector",
    why: "puerts_find_actors returns names and classes only. It reports no transform, no folder, no bounds and no "
      + "structure hash, so there is no way to verify a placement without one",
    // The legacy level_actors already takes include_transforms and
    // folder_filter, and level_outliner returns the folder tree. The native
    // find_actors dropped both. Bounds and a structure hash are the genuinely
    // new parts.
    legacy: ["level_actors", "level_outliner"],
    request: {
      tool: "puerts_scene_inspect",
      owner: "lane L (settled, implemented_unverified)",
      params: { level_path: "optional assertion; refuses if a different level is loaded", actors: "optional selectors", include_components: "boolean", include_properties: "boolean" },
      returns: "{ level_path, level_name, actor_count, actors: [{id, path, label, class, folder, tags, transform, bounds, attach_parent, components}], structure_hash_sha1 }",
    },
  });
  if (read?.success === true) {
    const labels = (read.data?.actors ?? []).map((a) => a.label);
    const wanted = [...PILLARS.map((p) => p.label), "CourtyardSun", "CourtyardSky", "CourtyardLamp", "CourtyardEntryTrigger"];
    h.check(wanted.every((w) => labels.includes(w)),
      "3. every actor the prompt asked for is in the level", JSON.stringify(labels).slice(0, 400));
    const pillar = (read.data?.actors ?? []).find((a) => a.label === "Pillar_NE");
    h.check(pillar?.folder?.startsWith("Courtyard") === true,
      "3. the actors landed in the outliner folder the prompt named", String(pillar?.folder));
    h.check(Math.abs((pillar?.transform?.scale?.z ?? 0) - 4) < 1e-3,
      "3. the pillar scale survived, which puerts_spawn_actor cannot express at all",
      JSON.stringify(pillar?.transform?.scale));
    h.check(read.data?.package_dirty_after === read.data?.package_dirty_before,
      "3. inspecting did not dirty the level package");
  }

  // Degraded read-back, so a run with no scene tools still says something about
  // what the registered catalog can and cannot see.
  if (read === null) {
    const seen = await h.call("puerts_find_actors", { type: "StaticMeshActor" }, {
      label: "3b. degraded read-back: puerts_find_actors is the only registered level query",
    });
    if (seen?.success === true) {
      h.note("3b. what the degraded read cannot answer",
        `${(seen.data?.actors ?? []).length} StaticMeshActors found, with no transform, no folder, no bounds and `
        + "no structure hash, so no placement claim can be verified from it");
    }
  }

  h.request("puerts_lighting_build",
    "build lighting so the placed lights are baked rather than preview-only. Nothing in the catalog, native or "
    + "legacy, triggers a lighting build, so a screenshot of an authored scene shows unbuilt lighting and the "
    + "Unreal 'Lighting needs to be rebuilt' banner",
    {
      tool: "puerts_lighting_build",
      params: { quality: "Preview | Medium | High | Production", wait_for_completion: "boolean", timeout_ms: "number" },
      returns: "{ built: boolean, quality, mapped_lights, elapsed_ms, warnings }",
    });

  await h.call("puerts_save", {}, {
    label: "4. persist the level, which scene_batch deliberately does not do",
  });

  await h.call("puerts_viewport_screenshot", {}, {
    label: "5. capture the courtyard",
  });

  h.note("6. this slice seals no artifact for the cold phase",
    "the generic cold check re-reads by asset_path, and puerts_scene_inspect addresses the loaded level rather "
    + "than an asset path. Proving a level survives a restart needs a cold check written against level_path.");
});
