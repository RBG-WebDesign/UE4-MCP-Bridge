#!/usr/bin/env node
// Feature acceptance 15: time-driven material effects.

import { runSlice } from "./slice-harness.mjs";

const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const MATERIAL = `/Game/MCPGenerated/FeatureAcceptance/M_Pulse_${runId}`;
const CONTROL = {
  asset_path: MATERIAL,
  domain: "Surface",
  shading_model: "Unlit",
  parameters: [{ kind: "vector", name: "EffectColor", default: { r: 0.1, g: 0.6, b: 1, a: 1 } }],
  outputs: { EmissiveColor: "EffectColor" },
  save: true,
};
const EFFECT = {
  asset_path: MATERIAL,
  domain: "Surface",
  shading_model: "Unlit",
  parameters: [
    { kind: "vector", name: "EffectColor", default: { r: 0.1, g: 0.6, b: 1, a: 1 }, group: "Effect" },
    { kind: "scalar", name: "Intensity", default: 8, group: "Effect" },
    { kind: "scalar", name: "PulseSpeed", default: 2, group: "Effect" },
  ],
  expressions: [
    { id: "Time", type: "Time" },
    { id: "ScaledTime", type: "Multiply" },
    { id: "Wave", type: "Sine" },
    { id: "Strength", type: "Multiply" },
    { id: "Glow", type: "Multiply" },
  ],
  connections: [
    { from: "Time", to: "ScaledTime.A" },
    { from: "PulseSpeed", to: "ScaledTime.B" },
    { from: "ScaledTime", to: "Wave.Input" },
    { from: "Wave", to: "Strength.A" },
    { from: "Intensity", to: "Strength.B" },
    { from: "EffectColor", to: "Glow.A" },
    { from: "Strength", to: "Glow.B" },
  ],
  outputs: { EmissiveColor: "Glow" },
  save: true,
};

await runSlice({
  id: "feature-material-effects",
  title: "a tunable time-driven emissive pulse material",
  proves: "replace a static control with a dynamic material graph, read it independently, converge and cold-load it",
}, async (h) => {
  if (!await h.assertFreshFixture(MATERIAL, "puerts_material_inspect")) return;

  const controlBuild = await h.call("puerts_material_build", CONTROL, { label: "1. build the static emissive control" });
  if (controlBuild?.success !== true) return;
  const control = await h.call("puerts_material_inspect", { asset_path: MATERIAL }, { label: "1. inspect the control independently" });
  const controlHash = control?.data?.structure_hash_sha1 ?? null;

  const built = await h.call("puerts_material_build", EFFECT, { label: "2. replace the control with the pulse graph" });
  if (built?.success !== true) return;
  const read = await h.call("puerts_material_inspect", { asset_path: MATERIAL }, { label: "3. inspect the effect independently" });
  const hash = read?.data?.structure_hash_sha1 ?? null;
  const json = JSON.stringify(read?.data ?? {});
  const parameters = (read?.data?.parameters ?? []).map((parameter) => parameter.name);
  h.check(built?.data?.compile?.succeeded === true || built?.data?.compile_status === "UpToDate", "2. the material compiles", JSON.stringify(built?.data?.compile ?? built?.data?.compile_status));
  h.check(typeof hash === "string" && hash !== controlHash, "3. the writer moved static control to dynamic effect", `${controlHash} -> ${hash}`);
  h.check(["EffectColor", "Intensity", "PulseSpeed"].every((name) => parameters.includes(name)), "3. all effect controls read back", JSON.stringify(parameters));
  h.check(["Time", "Sine", "Multiply"].every((type) => json.includes(type)), "3. the time and pulse expressions read back");

  const sha = h.fileSha(MATERIAL);
  const again = await h.call("puerts_material_build", EFFECT, { label: "4. rerun the effect desired state" });
  if (again?.success !== true) return;
  const reread = await h.call("puerts_material_inspect", { asset_path: MATERIAL }, { label: "4. inspect the rerun" });
  h.check(again?.data?.converged === true || again?.data?.applied_operation_count === 0, "4. the rerun reports convergence");
  h.check(reread?.data?.structure_hash_sha1 === hash, "4. the structure hash stayed fixed");
  h.check(sha === null || h.fileSha(MATERIAL) === sha, "4. the converged rerun wrote no new bytes");

  h.note("5. visual pulse proof remains live", "The graph is inspectable offline from the editor viewport, but shader evaluation over time requires a live render batch.");
  h.seal({ asset_path: MATERIAL, inspect_tool: "puerts_material_inspect", structure_hash: hash });
});
