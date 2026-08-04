#!/usr/bin/env node
// Feature acceptance 17: Sequencer events.

import { runSlice } from "./slice-harness.mjs";

const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const SEQUENCE = `/Game/MCPGenerated/FeatureAcceptance/LS_Event_${runId}`;
const CONTROL = {
  asset_path: SEQUENCE,
  frame_rate: 30,
  playback_range: { start_frame: 0, end_frame: 60 },
  bindings: [{ id: "light", kind: "spawnable", name: "EventLight", actor_class: "/Script/Engine.PointLight" }],
  tracks: [{
    binding: "light",
    type: "Float",
    property: "Light.Intensity",
    sections: [{ start_frame: 0, end_frame: 60, keys: [
      { frame: 0, value: 0, interpolation: "Linear" },
      { frame: 30, value: 5000, interpolation: "Linear" },
    ] }],
  }],
  save: true,
};

await runSlice({
  id: "feature-sequencer-events",
  title: "a Level Sequence that fires authored gameplay events",
  proves: "prove normal Sequencer desired state and name the missing director-Blueprint event boundary exactly",
}, async (h) => {
  if (!await h.assertFreshFixture(SEQUENCE, "puerts_sequence_inspect")) return;

  const built = await h.call("puerts_sequence_build", CONTROL, { label: "1. build the keyed Sequencer control" });
  if (built?.success !== true) return;
  const read = await h.call("puerts_sequence_inspect", { asset_path: SEQUENCE, include_keys: true }, { label: "2. inspect the control independently" });
  const hash = read?.data?.structure_hash_sha1 ?? null;
  const json = JSON.stringify(read?.data ?? {});
  h.check(typeof hash === "string", "2. the control has a structure hash", String(hash));
  h.check(json.includes("EventLight") && json.includes("5000"), "2. binding and intensity key read back");

  const sha = h.fileSha(SEQUENCE);
  const again = await h.call("puerts_sequence_build", CONTROL, { label: "3. rerun Sequencer control" });
  if (again?.success !== true) return;
  const reread = await h.call("puerts_sequence_inspect", { asset_path: SEQUENCE }, { label: "3. inspect the rerun" });
  h.check(again?.data?.converged === true || again?.data?.applied_operation_count === 0, "3. sequence rerun converged");
  h.check(reread?.data?.structure_hash_sha1 === hash, "3. sequence hash stayed fixed");
  h.check(sha === null || h.fileSha(SEQUENCE) === sha, "3. converged rerun wrote no bytes");

  h.request(
    "puerts_sequence_event_track_build",
    "author a Sequencer Event track and its director Blueprint endpoint atomically. puerts_sequence_build explicitly refuses Event tracks because a key without a generated director endpoint would be stored but never execute",
    {
      asset_path: SEQUENCE,
      events: [
        { frame: 15, endpoint: "OnChargeStarted", parameters: [] },
        { frame: 45, endpoint: "OnChargeReleased", parameters: [{ name: "Strength", type: "float", value: 1 }] },
      ],
      save: true,
    },
  );
  h.note("4. event authoring is blocked at the director Blueprint", "Transform, property and camera tracks are writable and inspectable. Event tracks are refused rather than saved without executable endpoints.");
  h.policy("5. play and observe events", "PIE is off and there is no authored event endpoint to execute.");
  h.seal({ asset_path: SEQUENCE, inspect_tool: "puerts_sequence_inspect", structure_hash: hash });
});
