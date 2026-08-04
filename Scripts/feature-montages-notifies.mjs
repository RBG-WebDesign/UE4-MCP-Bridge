#!/usr/bin/env node
// Feature acceptance 14: montage sections and notifies.

import { runSlice } from "./slice-harness.mjs";

const runId = `${new Date().toISOString().replace(/\D/g, "").slice(0, 14)}_${process.pid}`;
const HOST = `/Game/MCPGenerated/FeatureAcceptance/ABP_MontageHost_${runId}`;
const MONTAGE = `/Game/MCPGenerated/FeatureAcceptance/AM_Action_${runId}`;

await runSlice({
  id: "feature-montages-notifies",
  title: "an action montage with ordered sections and gameplay notifies",
  proves: "prove the slot host and name the engine-safe montage writer boundary without corrupting montage links",
}, async (h) => {
  if (!await h.assertFreshFixture(MONTAGE, "puerts_anim_montage_inspect")
    || !await h.assertFreshFixture(HOST, "puerts_anim_blueprint_inspect")) return;

  const skeletons = await h.call("puerts_find_assets", { type: "Skeleton", limit: 10 }, { label: "1. find a project Skeleton" });
  const animations = await h.call("puerts_find_assets", { type: "AnimSequence", limit: 10 }, { label: "1. find a source animation" });
  const skeleton = (skeletons?.data?.assets ?? [])[0]?.object_path
    ?? (skeletons?.data?.assets ?? [])[0]?.asset_path ?? null;
  const animation = (animations?.data?.assets ?? [])[0]?.object_path
    ?? (animations?.data?.assets ?? [])[0]?.asset_path ?? null;
  h.check(skeleton !== null && animation !== null, "1. montage host inputs are available", `${skeleton} | ${animation}`);
  if (skeleton === null || animation === null) return;

  const hostSpec = {
    asset_path: HOST,
    skeleton_path: skeleton,
    variables: [{ name: "ActionActive", type: "bool", default: "false" }],
    anim_graph: { pipeline: [
      { id: "base", type: "StateMachine", name: "BasePose" },
      { id: "action", type: "Slot", name: "DefaultSlot" },
    ] },
    state_machine: {
      states: [{ id: "idle", name: "Idle", animation, looping: true, is_entry: true }],
      transitions: [],
    },
    save: true,
  };
  const built = await h.call("puerts_anim_blueprint_build", hostSpec, { label: "2. build the montage slot host control" });
  if (built?.success !== true) return;
  const read = await h.call("puerts_anim_blueprint_inspect", { asset_path: HOST }, { label: "3. inspect the slot host independently" });
  const hostJson = JSON.stringify(read?.data ?? {});
  h.check(built?.data?.compile_status === "UpToDate", "2. the slot host compiles", String(built?.data?.compile_status));
  h.check(hostJson.includes("DefaultSlot"), "3. the montage slot reads back independently");

  const again = await h.call("puerts_anim_blueprint_patch", hostSpec, { label: "3. rerun the slot host desired state" });
  if (again?.success !== true) return;
  const reread = await h.call("puerts_anim_blueprint_inspect", { asset_path: HOST }, { label: "3. inspect the slot host rerun" });
  h.check((again?.data?.verification?.actual_states ?? []).length === 1, "3. slot host rerun replaced rather than appended state");
  h.check(JSON.stringify(reread?.data ?? {}).includes("DefaultSlot"), "3. slot host semantic state converged");

  h.request(
    "puerts_anim_montage_build",
    "create or replace ordered montage sections, slot segments and notifies atomically. The shipped inspector explicitly refuses a writer because UE4.27 FAnimLinkableElement values must be re-linked against segments and a partial next-section chain can silently play the wrong action",
    {
      asset_path: MONTAGE,
      skeleton_path: skeleton,
      slots: [{ name: "DefaultSlot", segments: [{ animation, start_time: 0, play_rate: 1 }] }],
      sections: [{ name: "Start", time: 0, next: "Loop" }, { name: "Loop", time: 0.2, next: "End" }, { name: "End", time: 0.8 }],
      notifies: [{ name: "ApplyDamage", trigger_time: 0.35, notify_class: "/Script/Engine.AnimNotify" }],
      save: true,
    },
  );
  h.note("4. montage mutation remains read-only by design", "puerts_anim_montage_inspect can verify hand-authored assets, but there is no non-corrupting native writer to call for the fresh montage path.");
  h.policy("5. play the montage", "PIE is off and the montage asset cannot be authored safely by the current bridge.");
  h.seal({ asset_path: HOST, inspect_tool: "puerts_anim_blueprint_inspect", structure_hash: reread?.data?.structure_hash_sha1 ?? read?.data?.structure_hash_sha1 ?? null });
});
