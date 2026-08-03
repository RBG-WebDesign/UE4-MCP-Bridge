#!/usr/bin/env node
// Failure atomicity and convergence for puerts_anim_blueprint_patch.
//
// Modelled on Scripts/mutator-atomicity.mjs and importing its helpers rather
// than copying them: the CONTRACT a failed mutation must satisfy is the same for
// an Animation Blueprint as for a Blueprint (the .uasset is byte-identical, an
// independent read is unchanged, the package is not dirty, no source-control
// entry appeared). Only the inspector differs, which is why `inspector` is a
// parameter there now instead of a second harness here.
//
// What this proves that no other script does: patching an asset that ALREADY
// EXISTED. Every other atomicity script covers the CREATE contract - a failed
// build leaves no asset. Finding 0t recorded why the edit contract had no
// script: there was no command to point one at, because a failed rebuild over an
// existing AnimBlueprint left an emptied one and nothing could put it back.
// FBridgeContentSnapshot is what closed that, and this is its proof.
//
// The lever that matters is (2). It has to fail AFTER
// FAnimBPBuilder::ClearGeneratedGraph has already emptied the AnimGraph, or the
// restore is never exercised and every check below passes for the wrong reason.
// An unknown pipeline node type is exactly that shape: FAnimBPValidator has no
// rule for it (its Rule 6 only counts StateMachine nodes), so the request is
// accepted, the clear pass runs, and FAnimBPAnimGraphBuilder::Build then refuses
// it at ABPAnimGraphBuilder.cpp:63-67. Without the restore, that lever is
// precisely the "emptied AnimBlueprint" finding 0t describes.
//
// The control, lever (4), is not optional. Three of mutator-atomicity's levers
// once passed purely because everything failed and nothing could move; a control
// that must SUCCEED and must move the fingerprint is what caught it. Here the
// control asserts something stronger than a moved hash, because the hash can
// move for an uninteresting reason: anim_blueprint_inspect derives node identity
// from each node's UObject name, and a clear-and-rebuild reassigns those. So the
// control also requires the new state to be PRESENT in the read-back, which node
// renaming cannot fake.
//
// NOT RUN by the lane that wrote it: no editor was launched and the C++ it
// exercises is uncompiled. Running it is the integrator's step.
//
// Usage:
//   node Scripts/animbp-patch-atomicity.mjs

import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";
import {
  ANIM_BLUEPRINT_INSPECTOR,
  checkMutationIsAtomic,
  fingerprint,
  startBridge,
} from "./mutator-atomicity.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");

// A fresh path per run, finding 0n. The control ADDS a state, so on a fixed
// fixture the second run would find it already there, the patch would converge
// to the same shape, and the control would report that it did not move the
// fingerprint - a false red about the product caused entirely by the harness.
const ABP = `/Game/MCPGenerated/ABP_PatchAtomicity_${Date.now().toString(36)}`;

/** The two-state fixture, built fresh and saved so a restore source exists. */
function fixtureSpec({ skeleton, idle, walk }) {
  return {
    asset_path: ABP,
    skeleton_path: skeleton,
    variables: [{ name: "bIsMoving", type: "bool", default: "false" }],
    anim_graph: { pipeline: [{ id: "sm", type: "StateMachine", name: "Locomotion" }] },
    state_machine: {
      states: [
        { id: "idle", name: "Idle", animation: idle, looping: true, is_entry: true },
        { id: "walk", name: "Walk", animation: walk, looping: true },
      ],
      transitions: [
        { from: "idle", to: "walk", blend_time: 0.2, condition: { type: "bool_variable", variable: "bIsMoving", value: true } },
        { from: "walk", to: "idle", blend_time: 0.2, condition: { type: "bool_variable", variable: "bIsMoving", value: false } },
      ],
    },
    save: true,
  };
}

/** The same fixture plus a third state. The control, and the convergence probe. */
function threeStateSpec(assets) {
  const spec = fixtureSpec(assets);
  spec.state_machine.states.push(
    { id: "run", name: "Run", animation: assets.run, looping: true },
  );
  spec.state_machine.transitions.push(
    { from: "walk", to: "run", blend_time: 0.15, condition: { type: "bool_variable", variable: "bIsMoving", value: true } },
    { from: "run", to: "walk", blend_time: 0.15, condition: { type: "bool_variable", variable: "bIsMoving", value: false } },
  );
  return spec;
}

const LEVERS = [
  {
    label: "(1) two entry states",
    // Refused by FAnimBPValidator Rule 1 before the builder touches the asset.
    // Proves a refusal costs nothing; it does NOT exercise the restore.
    caught_by: "validate-before-mutate",
    spec: (assets) => {
      const spec = fixtureSpec(assets);
      spec.state_machine.states[1].is_entry = true;
      return spec;
    },
  },
  {
    label: "(2) an unknown pipeline node type, which fails AFTER the AnimGraph is cleared",
    // THE lever. FAnimBPValidator has no rule for an unknown pipeline type, so
    // this reaches FAnimBPBuilder::Rebuild, the clear pass empties the AnimGraph,
    // and FAnimBPAnimGraphBuilder::Build then refuses at ABPAnimGraphBuilder.cpp:66.
    // Everything the harness asserts afterwards is a measurement of
    // FBridgeContentSnapshot::Restore. Without it this leaves the emptied
    // AnimBlueprint finding 0t describes.
    caught_by: "the AnimGraph builder, after the clear pass emptied the graph",
    spec: (assets) => {
      const spec = fixtureSpec(assets);
      spec.anim_graph.pipeline.unshift({ id: "bogus", type: "NotANodeType", name: "Bogus" });
      return spec;
    },
  },
  {
    label: "(3) a transition condition on a variable that does not exist",
    caught_by: "validate-before-mutate",
    spec: (assets) => {
      const spec = fixtureSpec(assets);
      spec.state_machine.transitions[0].condition = {
        type: "bool_variable", variable: "bNoSuchVariable", value: true,
      };
      return spec;
    },
  },
];

async function main() {
  const projectRoot = requireCurrentInstall();
  const bridge = startBridge("animbp-patch-atomicity");
  const failures = [];
  const assert = (condition, label) => {
    if (condition) { console.log(`  PASS  ${label}`); return true; }
    console.log(`  FAIL  ${label}`);
    failures.push(label);
    return false;
  };
  const evidence = { asset: ABP, levers: [] };

  try {
    await bridge.ready();

    // Discovery, not hard-coded paths. A script that hard-codes a skeleton
    // reports a fixture problem as a platform problem.
    console.log("\n-- find a Skeleton and three AnimSequences to build against");
    const skeletons = await bridge.call("puerts_find_assets", { type: "Skeleton", limit: 10 });
    const anims = await bridge.call("puerts_find_assets", { type: "AnimSequence", limit: 20 });
    const pathOf = (a) => a?.object_path ?? a?.path ?? a?.asset_path ?? null;
    const skeleton = pathOf((skeletons.data?.assets ?? [])[0]);
    const animations = (anims.data?.assets ?? []).map(pathOf).filter(Boolean);

    if (skeleton === null || animations.length < 3) {
      // Exit 1, not 0. This proved nothing, and a green run that proved nothing
      // is the one result a test must never produce.
      console.log(
        `\nCANNOT RUN: this project has ${skeleton === null ? "no Skeleton" : "a Skeleton"} and `
        + `${animations.length} AnimSequence(s); three are needed. Import animation content into `
        + `${projectRoot} and run again.`);
      process.exitCode = 1;
      return;
    }
    const assets = { skeleton, idle: animations[0], walk: animations[1], run: animations[2] };
    console.log(`  skeleton   ${skeleton}`);
    console.log(`  animations ${assets.idle}, ${assets.walk}, ${assets.run}`);

    console.log("\n-- seed the fixture, saved, so a restore source exists on disk");
    const seeded = await bridge.call("puerts_anim_blueprint_build", fixtureSpec(assets));
    assert(seeded.success === true,
      `(0) the fixture builds ${seeded.success ? "" : JSON.stringify(seeded.errors)}`);
    assert(seeded.data?.saved === true,
      "(0) the fixture is saved, so there is a file to compare against and to restore from");

    console.log("\n-- the create/edit split: patch refuses a path with nothing on it");
    let missing;
    try {
      missing = await bridge.call("puerts_anim_blueprint_patch",
        { ...fixtureSpec(assets), asset_path: `${ABP}_NoSuchAsset` });
    } catch (error) {
      // A refusal that surfaces as an MCP-layer error is still a refusal.
      missing = { success: false, errors: [String(error?.message ?? error)] };
    }
    assert(missing.success === false,
      "(0) patching a path that holds no asset is refused, not a create");
    assert(JSON.stringify(missing).includes("anim_blueprint_build"),
      "(0) the refusal names anim_blueprint_build as the command that creates");

    for (const lever of LEVERS) {
      console.log(`\n-- ${lever.label}`);
      const result = await checkMutationIsAtomic({
        call: bridge.call, assert, projectRoot, assetPath: ABP, label: lever.label,
        inspector: ANIM_BLUEPRINT_INSPECTOR,
        mutate: () => bridge.call("puerts_anim_blueprint_patch", lever.spec(assets)),
      });
      // The command's own account of the rollback, checked against the harness's
      // independent measurement above rather than trusted in place of it. A
      // command that says rollback_succeeded while the hash moved is a worse
      // defect than one that fails honestly.
      if (result.response?.data !== undefined) {
        assert(result.response.data.rollback_succeeded === true,
          `${lever.label}: the command reports rollback_succeeded`);
        assert(result.response.data.saved !== true,
          `${lever.label}: the failed patch did not save`);
      }
      evidence.levers.push({ label: lever.label, caught_by: lever.caught_by, ...result });
    }

    console.log("\n-- control: a patch that must succeed and must move the asset");
    const control = await checkMutationIsAtomic({
      call: bridge.call, assert, projectRoot, assetPath: ABP, label: "(4) control",
      expectFailure: false,
      inspector: ANIM_BLUEPRINT_INSPECTOR,
      mutate: () => bridge.call("puerts_anim_blueprint_patch", threeStateSpec(assets)),
    });
    // Stronger than "the hash moved". structure_hash_sha1 can move for a reason
    // that proves nothing - a clear-and-rebuild reassigns node UObject names, and
    // node identity is derived from those - so the control also requires the new
    // state to be readable back by NAME.
    const controlStates = control.response?.data?.verification?.actual_states ?? [];
    assert(controlStates.includes("Run"),
      "(4) control: the state the patch added is present in the independent read-back");
    assert(control.response?.data?.verification?.state_machine_count === 1,
      "(4) control: exactly one state machine, so the patch replaced rather than appended");
    evidence.levers.push({ label: "(4) control", ...control });

    // Convergence, which is the other half of what the clear pass exists for and
    // the check that would have caught the duplication defect finding 0t
    // describes. The same spec twice must leave the same shape, not two of it.
    console.log("\n-- convergence: the same patch twice leaves the same shape");
    const again = await bridge.call("puerts_anim_blueprint_patch", threeStateSpec(assets));
    assert(again.success === true, "(5) the same spec applies a second time");
    const secondStates = again.data?.verification?.actual_states ?? [];
    const secondTransitions = again.data?.verification?.actual_transitions ?? [];
    assert(again.data?.verification?.state_machine_count === 1,
      `(5) still exactly one state machine (${again.data?.verification?.state_machine_count})`);
    assert([...secondStates].sort().join(",") === [...controlStates].sort().join(","),
      `(5) the same states, not a second copy (${secondStates.join(",")})`);
    assert(secondStates.length === new Set(secondStates).size,
      `(5) no duplicated state names (${secondStates.join(",")})`);
    evidence.convergence = {
      first_states: controlStates,
      second_states: secondStates,
      second_transitions: secondTransitions,
      // Recorded, deliberately not asserted. The command documents that this
      // hash is not stable across a rerun; recording it is how the next reader
      // finds out whether that caveat is real on a live editor.
      first_hash: control.after?.member_hash ?? null,
      second_hash: again.data?.structure_hash_sha1 ?? null,
    };

    // Whatever else happened, the asset has to end this run readable and clean.
    const final = await fingerprint(bridge.call, projectRoot, ABP, ANIM_BLUEPRINT_INSPECTOR);
    assert(final.inspect_ok === true, "(6) the fixture is still readable at the end of the run");
    assert(final.package_dirty === false, "(6) the fixture's package is clean at the end of the run");
    evidence.final = final;

    const evidenceDir = join(repoRoot, "docs", "evidence");
    mkdirSync(evidenceDir, { recursive: true });
    const out = join(evidenceDir, "animbp-patch-atomicity.json");
    writeFileSync(out, JSON.stringify(evidence, null, 2));
    console.log(`\nevidence: ${out}`);

    if (failures.length > 0) {
      console.log(`\n${failures.length} check(s) did not hold:`);
      for (const f of failures) console.log(`  - ${f}`);
      process.exitCode = 1;
    } else {
      console.log("\nall checks passed");
    }
  } catch (error) {
    console.error(error);
    process.exitCode = 1;
  } finally {
    bridge.close();
  }
}

// Importing this file must not launch an editor session; only running it does.
// pathToFileURL, not a hand-built "file://" + path: on Windows the hand-built
// form never matches import.meta.url, so main() never runs and the script exits
// 0 having tested nothing.
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  await main();
}
