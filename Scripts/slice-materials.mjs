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
  // The master material, authored from the prompt. This step used to be a
  // h.request() for a primitive that did not exist, with a degraded fallback
  // that instanced whatever material happened to be in the project. That
  // fallback proved the instance tool and not the prompt: a parent nobody
  // authored has no guarantee of carrying any parameter to override, and the
  // 2026-08-03 run failed on exactly that, refused for four parameter names the
  // accidental parent had never heard of. puerts_material_build is registered
  // now, so the slice authors its own parent and the refusal becomes a real
  // measurement of the builder.
  const master = await h.call("puerts_material_build", {
    asset_path: MASTER,
    domain: "Surface",
    blend_mode: "Opaque",
    shading_model: "DefaultLit",
    parameters: [
      { kind: "vector", name: "BeaconColor", default: { r: 1.0, g: 0.62, b: 0.16, a: 1.0 }, group: "Beacon", x: -600, y: -200 },
      { kind: "scalar", name: "GlowStrength", default: 5.0, group: "Beacon", x: -600, y: 0 },
      { kind: "scalar", name: "Roughness", default: 0.5, group: "Beacon", x: -600, y: 200 },
      { kind: "scalar", name: "EmissiveOff", default: 0.0, group: "Beacon", x: -400, y: 100 },
      { kind: "switch", name: "UseEmissive", default: true, group: "Beacon", x: -200, y: 0 },
    ],
    expressions: [
      { id: "Glow", type: "Multiply", x: -400, y: -100 },
    ],
    connections: [
      { from: "BeaconColor", to: "Glow.A" },
      { from: "GlowStrength", to: "Glow.B" },
      { from: "Glow", to: "UseEmissive.A" },
      { from: "EmissiveOff", to: "UseEmissive.B" },
    ],
    outputs: {
      BaseColor: "BeaconColor",
      EmissiveColor: "UseEmissive",
      Roughness: "Roughness",
    },
    save: true,
  }, {
    label: "1. author the amber emissive master material the prompt asked for",
    why: "the prompt asks for tunable colour and glow strength, which means named parameters on a parent "
      + "material. Without authoring one there is nothing to instance and nothing to tune",
  });
  if (master?.success === true) {
    h.check(master.data?.compile?.succeeded === true || master.data?.compile_status === "UpToDate",
      "1. the master material compiled",
      JSON.stringify(master.data?.compile ?? master.data?.compile_status ?? null).slice(0, 300));
    // A converged rerun against an already-saved, unchanged material
    // deliberately reports saved:false (MCPPuerTSBridgeMaterialBuild.cpp's
    // converged branch) because there is nothing to save, not because a save
    // was attempted and skipped. Same shape as the level slice's converged
    // scene_batch: a correct no-op is not a failed assertion.
    h.check(master.data?.saved === true || master.data?.converged === true,
      "1. the master material was saved after its read-back agreed, or converged",
      `saved=${master.data?.saved} converged=${master.data?.converged}`);
  }

  // The independent read of the PARENT, before anything instances it. A builder
  // reporting its own success is not evidence that the parameters a designer
  // will tune are actually published by the material.
  const masterRead = await h.call("puerts_material_inspect", { asset_path: MASTER }, {
    label: "1. read the master back with the independent material inspector",
    why: "an instance can only override parameters the parent publishes, so the parameter list is the "
      + "contract between the two halves of this slice",
  });
  let masterHash = null;
  if (masterRead?.success === true) {
    masterHash = masterRead.data?.structure_hash_sha1 ?? null;
    h.check(masterRead.data?.asset_kind === "material",
      "1. the inspector identifies the parent as a material", String(masterRead.data?.asset_kind));
    const published = (masterRead.data?.parameters ?? []).map((parameter) => parameter.name);
    h.check(["BeaconColor", "GlowStrength", "Roughness", "UseEmissive"].every((n) => published.includes(n)),
      "1. every parameter the prompt asked for is published by the parent",
      JSON.stringify(published));
    h.check(typeof masterHash === "string" && masterHash.length >= 8,
      "1. the inspector reports a structure hash for the master", String(masterHash));
  }

  // Convergence, the property every builder here is judged on: an identical
  // rerun must leave the same asset and must not rewrite the file.
  const masterSha = h.fileSha(MASTER);

  const instance = await h.call("puerts_material_instance_build", {
    asset_path: INSTANCE,
    parent_path: MASTER,
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
    const parameters = new Map((read.data?.parameters ?? []).map((parameter) => [parameter.name, parameter]));
    h.check(Math.abs((parameters.get("GlowStrength")?.value ?? 0) - 12.5) < 1e-4,
      "3. the scalar override is the one the prompt asked for",
      String(parameters.get("GlowStrength")?.value));
    const c = parameters.get("BeaconColor")?.value;
    h.check(c !== undefined && Math.abs((c.r ?? 0) - 1.0) < 1e-3 && Math.abs((c.g ?? 0) - 0.62) < 1e-3,
      "3. the vector override is the colour the prompt asked for", JSON.stringify(c));
    h.check(String(read.data?.parent_path ?? "").includes("M_SliceBeacon"),
      "3. the instance names the master this slice authored",
      String(read.data?.parent_path));
  }

  // The master, rebuilt from the identical spec. A desired-state builder that
  // rewrites the file every time it is asked for a state the asset is already
  // in cannot be composed into a workflow: the caller cannot tell "already
  // right" from "changed".
  const masterAgain = await h.call("puerts_material_build", {
    asset_path: MASTER,
    domain: "Surface",
    blend_mode: "Opaque",
    shading_model: "DefaultLit",
    parameters: [
      { kind: "vector", name: "BeaconColor", default: { r: 1.0, g: 0.62, b: 0.16, a: 1.0 }, group: "Beacon", x: -600, y: -200 },
      { kind: "scalar", name: "GlowStrength", default: 5.0, group: "Beacon", x: -600, y: 0 },
      { kind: "scalar", name: "Roughness", default: 0.5, group: "Beacon", x: -600, y: 200 },
      { kind: "scalar", name: "EmissiveOff", default: 0.0, group: "Beacon", x: -400, y: 100 },
      { kind: "switch", name: "UseEmissive", default: true, group: "Beacon", x: -200, y: 0 },
    ],
    expressions: [
      { id: "Glow", type: "Multiply", x: -400, y: -100 },
    ],
    connections: [
      { from: "BeaconColor", to: "Glow.A" },
      { from: "GlowStrength", to: "Glow.B" },
      { from: "Glow", to: "UseEmissive.A" },
      { from: "EmissiveOff", to: "UseEmissive.B" },
    ],
    outputs: {
      BaseColor: "BeaconColor",
      EmissiveColor: "UseEmissive",
      Roughness: "Roughness",
    },
    save: true,
  }, { label: "3b. rerunning the identical master spec converges" });
  if (masterAgain?.success === true && masterHash !== null) {
    const masterReread = await h.call("puerts_material_inspect", { asset_path: MASTER }, {
      label: "3b. re-read the master after the rerun",
    });
    h.check(masterReread?.data?.structure_hash_sha1 === masterHash,
      "3b. the master's structure hash is unmoved by an identical rebuild",
      `${masterReread?.data?.structure_hash_sha1} vs ${masterHash}`);
    h.check(masterSha === null || h.fileSha(MASTER) === masterSha,
      "3b. a converged rerun of the master wrote no new bytes to disk");
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
