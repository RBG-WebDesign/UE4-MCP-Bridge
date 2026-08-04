#!/usr/bin/env node
// Feature acceptance 11: dialogue state and advance input.

import { runSlice } from "./slice-harness.mjs";

const includePie = process.argv.includes("--pie");
const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const DIALOGUE = `/Game/MCPGenerated/FeatureAcceptance/BP_Dialogue_${runId}`;
const DIALOGUE_CLASS = `${DIALOGUE}.BP_Dialogue_${runId}_C`;
const ACTOR_LABEL = `FeatureDialogue_${runId}`;

const CONTROL = {
  asset_path: DIALOGUE,
  parent_class: "Actor",
  variables: [{ name: "CurrentLine", type: "int", default: 0, category: "Dialogue" }],
  graph: { nodes: [
    { id: "begin", type: "BeginPlay", x: 0, y: 0 },
    { id: "control", type: "PrintString", x: 320, y: 0, params: { InString: `FEATURE_DIALOGUE_CONTROL_${runId}` } },
  ], connections: [{ from: "begin.then", to: "control.exec" }] },
};
const SPEC = {
  asset_path: DIALOGUE,
  parent_class: "Actor",
  variables: [
    { name: "Speaker", type: "string", default: "Guide", category: "Dialogue" },
    { name: "LineOne", type: "string", default: "The bridge is open.", category: "Dialogue" },
    { name: "LineTwo", type: "string", default: "Continue when ready.", category: "Dialogue" },
    { name: "CurrentLine", type: "int", default: 0, category: "Dialogue" },
    { name: "IsActive", type: "bool", default: true, category: "Dialogue" },
  ],
  graph: { nodes: [
    { id: "begin", type: "BeginPlay", x: 0, y: 0 },
    { id: "started", type: "PrintString", x: 320, y: 0, params: { InString: `FEATURE_DIALOGUE_STARTED_${runId}` } },
    { id: "advance", type: "InputKey", x: 0, y: 300, params: { fkey_name: "E" } },
    { id: "getLine", type: "VariableGet", x: 260, y: 520, params: { var_name: "CurrentLine" } },
    { id: "increment", type: "Operator", x: 520, y: 520, params: { op: "add_int", B: 1 } },
    { id: "setLine", type: "VariableSet", x: 600, y: 300, params: { var_name: "CurrentLine" } },
    { id: "advanced", type: "PrintString", x: 900, y: 300, params: { InString: `FEATURE_DIALOGUE_ADVANCED_${runId}` } },
  ], connections: [
    { from: "begin.then", to: "started.exec" }, { from: "advance.Pressed", to: "setLine.exec" },
    { from: "getLine.CurrentLine", to: "increment.A" }, { from: "increment.ReturnValue", to: "setLine.CurrentLine" },
    { from: "setLine.then", to: "advanced.exec" },
  ] },
};

await runSlice({
  id: "feature-dialogue",
  title: "an inspectable dialogue actor with authored lines and advance state",
  proves: "author dialogue data and an input-driven line transition, inspect it independently, converge, cold-load, and optionally prove runtime state movement",
}, async (h) => {
  if (!await h.assertFreshFixture(DIALOGUE, "puerts_graph_inspect")) return;

  const controlBuild = await h.call("puerts_blueprint_build", CONTROL, { label: "1. build the one-variable dialogue control" });
  if (controlBuild?.success !== true) return;
  const control = await h.call("puerts_graph_inspect", { asset_path: DIALOGUE, include_pins: true }, { label: "1. inspect dialogue control" });
  const controlHash = control?.data?.graph?.structure_hash_sha1 ?? null;

  const built = await h.call("puerts_blueprint_build", SPEC, { label: "2. replace control with dialogue data and advance graph" });
  if (built?.success !== true) return;
  const read = await h.call("puerts_graph_inspect", { asset_path: DIALOGUE, include_pins: true }, { label: "3. inspect dialogue independently" });
  const hash = read?.data?.graph?.structure_hash_sha1 ?? null;
  const variables = (read?.data?.variables ?? []).map((v) => v.name);
  const graph = JSON.stringify(read?.data?.graph?.nodes ?? []);
  h.check(built?.data?.compile_status === "UpToDate", "2. the dialogue Blueprint compiles", String(built?.data?.compile_status));
  h.check(typeof hash === "string" && hash !== controlHash, "3. the control moved to complete dialogue state", `${controlHash} -> ${hash}`);
  h.check(["Speaker", "LineOne", "LineTwo", "CurrentLine", "IsActive"].every((name) => variables.includes(name)),
    "3. every dialogue datum reads back", JSON.stringify(variables));
  h.check(graph.includes("CurrentLine") && graph.includes("FEATURE_DIALOGUE_ADVANCED"), "3. the line advance graph reads back independently");

  const sha = h.fileSha(DIALOGUE);
  await h.call("puerts_blueprint_build", SPEC, { label: "4. rerun the dialogue desired state" });
  const again = await h.call("puerts_graph_inspect", { asset_path: DIALOGUE }, { label: "4. inspect dialogue rerun" });
  h.check(again?.data?.graph?.structure_hash_sha1 === hash, "4. dialogue graph converged");
  h.check(sha === null || h.fileSha(DIALOGUE) === sha, "4. dialogue rerun wrote no new bytes");

  if (!includePie) {
    h.policy("5. advance live dialogue", "PIE is off by default. Re-run with --pie only after explicit user authorization.");
  } else {
    const placed = await h.call("puerts_scene_batch", { operations: [{ op: "upsert_actor", label: ACTOR_LABEL, class: DIALOGUE_CLASS, location: { x: 0, y: 0, z: 150 } }] }, { label: "5. place dialogue fixture" });
    let started = false;
    try {
      if (placed?.success === true) started = (await h.call("puerts_pie_start", {}, { label: "5. start explicitly authorized PIE" }))?.success === true;
      if (started) {
        await h.call("puerts_pie_agent_control", { op: "press", key: "E" }, { label: "5. advance dialogue" });
        const state = await h.call("puerts_pie_agent_query", { op: "read_property", actor: ACTOR_LABEL, property: "CurrentLine" }, { label: "5. read live dialogue state" });
        h.check(state?.data?.value === 1, "5. advance moved CurrentLine from 0 to 1", String(state?.data?.value));
        const observed = await h.call("puerts_pie_agent_query", { op: "observe", log_lines: 100 }, { label: "5. observe dialogue marker" });
        h.check(JSON.stringify(observed?.data ?? {}).includes(`FEATURE_DIALOGUE_ADVANCED_${runId}`), "5. runtime emitted the advance marker");
      }
    } finally { if (started) await h.call("puerts_pie_stop", {}, { label: "5. stop explicitly authorized PIE" }); }
  }

  h.seal({ asset_path: DIALOGUE, inspect_tool: "puerts_graph_inspect", structure_hash: hash });
});
