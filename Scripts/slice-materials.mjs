#!/usr/bin/env node
// Vertical slice 5 of 7: MATERIALS.
//
// One prompt: "make me an amber emissive beacon material with tunable colour and
// glow strength, and put it on the beacon mesh."
//
// What a pass would prove: a master material with named parameters, an instance
// that overrides them, a read-back that agrees, and the material actually on a
// mesh. Lane I is building the instance half and the inspector, and is explicit
// that the master material graph will NOT be authorable in that lane, so this
// slice is the record of a gap that is not merely unbuilt but unowned.
//
//   node Scripts/slice-materials.mjs
//   node Scripts/slice-materials.mjs --phase=cold

import { runSlice } from "./slice-harness.mjs";

const MASTER = "/Game/MCPGenerated/M_SliceBeacon";
const INSTANCE = "/Game/MCPGenerated/MI_SliceBeacon";
const BP = "/Game/MCPGenerated/BP_SliceMaterialProbe";

await runSlice({
  id: "materials",
  title: "a master material with parameters, an instance that overrides them, compiled and applied",
  proves: "author a material graph, instance it, read the parameter values back, and see it on a mesh",
}, async (h) => {
  // The master material. Nothing anywhere authors a material graph: the legacy
  // material_create is behind the HTTP opt-in and builds from a fixed template,
  // puerts_sky_shader_create is one hard-coded HLSL sky, and lane B already
  // recorded material graph structure as a platform gap with no native command.
  h.request("puerts_material_build",
    "author a UMaterial graph: expression nodes, named scalar/vector/texture parameters, and the base-colour, "
    + "emissive, roughness and normal connections. Without it there is no parent material to instance, so the "
    + "material slice cannot start from a prompt. Lane I states this will not exist in that lane, and lane B "
    + "recorded material graph structure as a platform gap. The nearest registered tool, puerts_sky_shader_create, "
    + "builds one specific hard-coded sky material",
    {
      tool: "puerts_material_build",
      owner: "UNOWNED. Lane I ships the instance and the inspector only",
      // Deliberately NOT declared as a legacy equivalent. material_create builds
      // from a fixed template and the inventory's own note on it says a generic
      // material graph builder is a known gap. Claiming it as a port would
      // understate this by a whole primitive.
      nearest_existing: "material_create (legacy_http), which creates a simple opaque surface from a fixed template and authors no graph",
      params: {
        asset_path: "/Game/MCPGenerated/...",
        domain: "Surface | PostProcess | UI",
        blend_mode: "Opaque | Masked | Translucent",
        shading_model: "DefaultLit | Unlit",
        parameters: "[{kind:'scalar'|'vector'|'texture'|'switch', name, default, group?}]",
        expressions: "[{id, type, params}] and connections: [{from:'id.output', to:'id.input'}], "
          + "the same node-and-connection shape puerts_blueprint_build already uses",
        outputs: "{BaseColor:'id.output', EmissiveColor:'id.output', Roughness:..., Normal:...}",
        save: "boolean",
      },
      returns: "{ compile_status, parameters_read_back, structure_hash_sha1 }",
    });

  // Without an authoring primitive the slice needs an existing parent. It looks
  // for one rather than assuming, so the evidence says whether the fallback was
  // even available.
  let parent = null;
  const found = await h.call("puerts_find_assets", { type: "Material", limit: 20 }, {
    label: "1. look for an existing master material to instance instead",
  });
  if (found?.success === true) {
    const list = found.data?.assets ?? [];
    parent = list[0]?.object_path ?? list[0]?.path ?? list[0]?.asset_path ?? null;
    h.check(parent !== null,
      "1. some material exists to act as a parent", `${list.length} found`);
    h.note("1. this is a degraded path, not the slice",
      "instancing whatever material happens to exist proves the instance tool, not the prompt. The prompt asked "
      + "for a material with named parameters, and a parent nobody authored has no guarantee of carrying any.");
  }

  const instance = await h.call("puerts_material_instance_build", {
    asset_path: INSTANCE,
    parent_path: parent ?? MASTER,
    scalars: { GlowStrength: 12.5, Roughness: 0.35 },
    vectors: { BeaconColor: { r: 1.0, g: 0.62, b: 0.16, a: 1.0 } },
    switches: { UseEmissive: true },
    save: true,
  }, {
    label: "2. build the material instance with the prompt's parameter values",
    why: "the instance is what a designer actually tunes, and it is the only material write lane I will ship",
    legacy: ["material_instance_create", "material_instance_set_params"],
    request: {
      tool: "puerts_material_instance_build",
      owner: "lane I (settled, implementation in progress)",
      params: { asset_path: "string", parent_path: "string", scalars: "{name: number}", vectors: "{name: {r,g,b,a}}", textures: "{name: path}", switches: "{name: boolean}", clear_unlisted: "boolean", save: "boolean" },
    },
  });

  const read = await h.call("puerts_material_inspect", { asset_path: INSTANCE }, {
    label: "3. read the instance back and compare the overrides field for field",
    why: "a material builder with no inspector cannot be verified without a human opening the asset",
    legacy: ["material_info"],
    request: {
      tool: "puerts_material_inspect",
      owner: "lane I (settled, implementation in progress)",
      params: { asset_path: "string" },
      returns: "{ asset_kind: 'material'|'material_instance', parent_path, scalars, vectors, textures, switches, structure_hash_sha1 }",
    },
  });
  let hash = null;
  if (read?.success === true) {
    hash = read.data?.structure_hash_sha1 ?? null;
    h.check(read.data?.asset_kind === "material_instance",
      "3. the inspector identifies it as an instance", String(read.data?.asset_kind));
    h.check(Math.abs((read.data?.scalars?.GlowStrength ?? 0) - 12.5) < 1e-4,
      "3. the scalar override is the one the prompt asked for",
      String(read.data?.scalars?.GlowStrength));
    const c = read.data?.vectors?.BeaconColor;
    h.check(c !== undefined && Math.abs((c.r ?? 0) - 1.0) < 1e-3 && Math.abs((c.g ?? 0) - 0.62) < 1e-3,
      "3. the vector override is the colour the prompt asked for", JSON.stringify(c));
  }

  // A texture parameter has nowhere to get a texture from. The 2026-07-29 gap
  // audit already recorded "no texture tools at all"; the instance tool's
  // textures map makes that gap reachable from a prompt.
  h.request("puerts_texture_import",
    "import or generate a UTexture2D so a texture parameter can be set. puerts_material_instance_build accepts a "
    + "textures map and nothing in the catalog can produce a texture to put in it; docs/GAP_AUDIT-2026-07-29.md "
    + "records no texture tools at all",
    {
      tool: "puerts_texture_import",
      params: { asset_path: "/Game/MCPGenerated/...", source_file: "absolute path on disk", srgb: "boolean", compression: "Default | Normalmap | Masks | Grayscale", save: "boolean" },
      returns: "{ asset_path, width, height, format }",
    });

  // Application is authorable today: OverrideMaterials is a component template
  // property and blueprint_build writes those, including asset references.
  await h.call("puerts_blueprint_build", {
    asset_path: BP,
    parent_class: "Actor",
    components: [{
      class: "StaticMeshComponent",
      name: "Body",
      properties: {
        StaticMesh: "/Engine/BasicShapes/Sphere.Sphere",
        OverrideMaterials: [`${INSTANCE}.${INSTANCE.split("/").pop()}`],
      },
    }],
  }, {
    label: "4. put the instance on a mesh through a component template property",
    why: "this is the one step of the material slice with a registered path today",
  });

  await h.call("puerts_viewport_screenshot", { filename: "slice-materials.png" }, {
    label: "5. capture the viewport so the material can be judged by eye",
  });

  h.note("6. shader compilation is not observable",
    "no registered tool reports whether the shader compiler has finished, so a screenshot taken immediately after a "
    + "material change can legitimately show the default checkerboard. Lane I reports compile result inside the "
    + "instance build response, which covers the instance but not the shader map.");

  if (hash !== null) h.seal({ asset_path: INSTANCE, inspect_tool: "puerts_material_inspect", structure_hash: hash });
});
