#!/usr/bin/env node
// Feature acceptance 18: packaged feature demo.

import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const includePackage = process.argv.includes("--package");
const packageTimeoutMs = 45 * 60 * 1000;
const packagePollMs = 3000;
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const DEMO = `/Game/MCPGenerated/FeatureAcceptance/BP_PackagedDemo_${runId}`;
const DEMO_CLASS = `${DEMO}.BP_PackagedDemo_${runId}_C`;
const LABEL = `FeaturePackagedDemo_${runId}`;
const MARKER = `FEATURE_PACKAGED_DEMO_${runId}`;
const CONTROL = {
  asset_path: DEMO,
  parent_class: "Actor",
  variables: [{ name: "ActivationCount", type: "int", default: 0, category: "Demo" }],
  graph: { nodes: [
    { id: "begin", type: "BeginPlay", x: 0, y: 0 },
    { id: "control", type: "PrintString", x: 320, y: 0, params: { InString: `FEATURE_PACKAGE_CONTROL_${runId}` } },
  ], connections: [{ from: "begin.then", to: "control.exec" }] },
};
const SPEC = {
  asset_path: DEMO,
  parent_class: "Actor",
  variables: [{ name: "ActivationCount", type: "int", default: 0, category: "Demo" }],
  components: [{
    class: "StaticMeshComponent",
    name: "DemoMesh",
    properties: { StaticMesh: "/Engine/BasicShapes/Cube.Cube" },
  }],
  graph: { nodes: [
    { id: "begin", type: "BeginPlay", x: 0, y: 0 },
    { id: "ready", type: "PrintString", x: 320, y: 0, params: { InString: `${MARKER}_READY` } },
    { id: "activate", type: "InputKey", x: 0, y: 300, params: { fkey_name: "E" } },
    { id: "getCount", type: "VariableGet", x: 260, y: 520, params: { var_name: "ActivationCount" } },
    { id: "increment", type: "Operator", x: 520, y: 520, params: { op: "add_int", B: 1 } },
    { id: "setCount", type: "VariableSet", x: 600, y: 300, params: { var_name: "ActivationCount" } },
    { id: "activated", type: "PrintString", x: 900, y: 300, params: { InString: `${MARKER}_ACTIVATED` } },
  ], connections: [
    { from: "begin.then", to: "ready.exec" },
    { from: "activate.Pressed", to: "setCount.exec" },
    { from: "getCount.ActivationCount", to: "increment.A" },
    { from: "increment.ReturnValue", to: "setCount.ActivationCount" },
    { from: "setCount.then", to: "activated.exec" },
  ] },
};

await runSlice({
  id: "feature-packaged-demo",
  title: "a package-ready interactive feature demo",
  proves: "author and inspect a standalone mechanic, then distinguish runtime proof, map configuration and packaging gates",
}, async (h) => {
  if (!await h.assertFreshFixture(DEMO, "puerts_graph_inspect")) return;

  const controlBuild = await h.call("puerts_blueprint_build", CONTROL, { label: "1. build the non-interactive package control" });
  if (controlBuild?.success !== true) return;
  const control = await h.call("puerts_graph_inspect", { asset_path: DEMO }, { label: "1. inspect package control" });
  const controlHash = control?.data?.graph?.structure_hash_sha1 ?? null;

  const built = await h.call("puerts_blueprint_build", SPEC, { label: "2. replace control with the standalone demo mechanic" });
  if (built?.success !== true) return;
  const defaults = await h.call("puerts_class_defaults_patch", { asset_path: DEMO, properties: { AutoReceiveInput: "Player0" } }, { label: "2. enable packaged player input" });
  if (defaults?.success !== true) return;
  const read = await h.call("puerts_graph_inspect", { asset_path: DEMO, include_pins: true }, { label: "3. inspect the demo independently" });
  const hash = read?.data?.graph?.structure_hash_sha1 ?? null;
  const json = JSON.stringify(read?.data ?? {});
  h.check(built?.data?.compile_status === "UpToDate", "2. the demo compiles", String(built?.data?.compile_status));
  h.check(typeof hash === "string" && hash !== controlHash, "3. the writer moved the package control", `${controlHash} -> ${hash}`);
  h.check(json.includes("ActivationCount") && json.includes(`${MARKER}_ACTIVATED`), "3. mechanic state and marker read back");
  if (defaults?.data?.class_default_object) {
    const input = await h.call("puerts_read_property", { object_path: defaults.data.class_default_object, property: "AutoReceiveInput" }, { label: "3. read packaged input default independently" });
    h.check(JSON.stringify(input?.data?.value ?? "").includes("Player0"), "3. packaged input is enabled", JSON.stringify(input?.data?.value));
  }

  const sha = h.fileSha(DEMO);
  const rebuilt = await h.call("puerts_blueprint_build", SPEC, { label: "4. rerun demo desired state" });
  if (rebuilt?.success !== true) return;
  const again = await h.call("puerts_graph_inspect", { asset_path: DEMO }, { label: "4. inspect demo rerun" });
  h.check(again?.data?.graph?.structure_hash_sha1 === hash, "4. demo graph converged");
  h.check(sha === null || h.fileSha(DEMO) === sha, "4. demo rerun wrote no new bytes");

  if (!includePie) {
    h.policy("5. exercise the standalone mechanic", "PIE is off by default. Re-run with --pie only after explicit user authorization.");
  } else {
    const placed = await h.call("puerts_scene_batch", { operations: [{ op: "upsert_actor", label: LABEL, class: DEMO_CLASS, location: { x: 0, y: 0, z: 100 } }] }, { label: "5. place packaged demo actor" });
    let started = false;
    try {
      if (placed?.success === true) started = (await h.call("puerts_pie_start", {}, { label: "5. start explicitly authorized PIE" }))?.success === true;
      if (started) {
        await h.call("puerts_pie_agent_control", { op: "press", key: "E" }, { label: "5. activate packaged demo" });
        const state = await h.call("puerts_pie_agent_query", { op: "read_property", actor: LABEL, property: "ActivationCount" }, { label: "5. read packaged demo state" });
        h.check(state?.data?.value === 1, "5. packaged mechanic moved state from 0 to 1", String(state?.data?.value));
      }
    } finally { if (started) await h.call("puerts_pie_stop", {}, { label: "5. stop explicitly authorized PIE" }); }
  }

  // project_settings_maps persists the startup contract before package preflight.
  // A real async job API run is opt-in because it writes a long-lived archive.
  const scene = await h.call("puerts_scene_inspect", {}, { label: "6. read the saved map selected for packaging" });
  const mapPath = scene?.data?.level_path ?? scene?.data?.world?.package_name ?? null;
  if (typeof mapPath === "string" && mapPath.startsWith("/Game/")) {
    const configured = await h.call("puerts_project_settings_maps", {
      game_default_map: mapPath,
      global_default_game_mode: "/Script/Engine.GameModeBase",
    }, { label: "6. persist package defaults" });
    if (configured?.success === true) {
      const packageSpec = {
        target: "Win64", configuration: "Development",
        output_directory: "Saved/MCPGenerated/PackagedDemo", maps: [mapPath],
      };
      if (!includePackage) {
        const packaged = await h.call("puerts_project_package_start", {
          ...packageSpec, plan_only: true,
        }, { label: "6. validate the asynchronous package plan" });
        h.check(packaged?.data?.status === "planned", "6. package preflight completed without starting UAT");
        h.policy("6. run the real package job",
          "Packaging is off by default. Re-run with --package only when a long UAT archive build is intended.");
      } else {
        const started = await h.call("puerts_project_package_start", {
          ...packageSpec, plan_only: false,
        }, { label: "6. start the explicitly requested asynchronous package job" });
        const jobId = started?.data?.job_id;
        h.check(typeof jobId === "string" && jobId.length > 0, "6. package start returned an owned job id", String(jobId));
        if (typeof jobId === "string" && jobId.length > 0) {
          const deadline = Date.now() + packageTimeoutMs;
          let terminal = null;
          while (Date.now() < deadline) {
            const status = await h.call("puerts_job_status", { job_id: jobId }, {
              label: "6. poll the owned package job",
            });
            const state = status?.data?.job?.state;
            if (["succeeded", "failed", "cancelled"].includes(state)) {
              terminal = status;
              break;
            }
            await new Promise((resolve) => setTimeout(resolve, packagePollMs));
          }
          if (terminal === null) {
            const cancelled = await h.call("puerts_job_cancel", { job_id: jobId }, {
              label: "6. cancel the owned package job after the bounded timeout",
            });
            h.check(cancelled?.success === true, "6. the timed-out owned package job accepted cancellation");
            h.check(false, "6. package job completed inside the bounded timeout", `${packageTimeoutMs}ms`);
          } else {
            const result = await h.call("puerts_job_result", { job_id: jobId }, {
              label: "6. collect the completed package result exactly once",
            });
            const job = result?.data?.job ?? terminal?.data?.job ?? {};
            h.check(job.state === "succeeded" && job.return_code === 0,
              "6. the package process completed successfully", `state=${job.state} return_code=${job.return_code}`);
            h.check(typeof job.output_directory === "string" && job.output_directory.length > 0
              && job.output_file_count > 0,
            "6. the package result reports a non-empty archive output",
            `directory=${job.output_directory} files=${job.output_file_count}`);
          }
        }
      }
    }
  } else {
    h.check(false, "6. loaded level is a saved /Game map", String(mapPath));
  }
  h.seal({ asset_path: DEMO, inspect_tool: "puerts_graph_inspect", structure_hash: hash });
});
