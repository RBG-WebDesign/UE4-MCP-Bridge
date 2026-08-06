#!/usr/bin/env node
// Vertical slice 7 of 7: CINEMATICS.
//
// One prompt: "make a three second intro: the camera pushes in on the beacon
// while it fades up, then cut to black."
//
// What a pass proves: a LevelSequence with real tracks and keyframes can bind
// real and spawnable actors, survive independent inspection, be placed and
// saved in the level, then launch through the asynchronous render job API.
//
//   node Scripts/slice-cinematics.mjs

import { runSlice } from "./slice-harness.mjs";

const SEQ = "/Game/MCPGenerated/LS_SliceIntro";

await runSlice({
  id: "cinematics",
  title: "a LevelSequence with tracks and keyframes",
  proves: "author a sequence with bindings, tracks and keys, read it back, place it in the level and play it",
}, async (h) => {
  // Even with both of the above, the sequence has to exist in the level as a
  // LevelSequenceActor before anything can play it, and that is lane L's tool.
  h.need("puerts_scene_batch", {
    why: "place a LevelSequenceActor that points at the sequence. puerts_spawn_actor can spawn the actor class but "
      + "cannot set its LevelSequence property or label it",
    legacy: ["actor_spawn", "batch_spawn", "actor_organize"],
    request: { tool: "puerts_scene_batch", owner: "lane L (settled, implemented_unverified)" },
  });

  h.need("puerts_sequence_render_start", {
    why: "start an asynchronous UE4.27 image-sequence render and return a job id",
  });
  await h.call("puerts_scene_batch", {
    operations: [{
      op: "upsert_actor", label: "SliceBeacon", class: "/Script/Engine.PointLight",
      location: { x: 0, y: 0, z: 200 }, folder: "Courtyard/Cinematics",
      components: { LightComponent0: { Intensity: 0 } },
    }],
  }, { label: "1. place the beacon that the sequence will possess" });

  await h.call("puerts_sequence_build", {
    asset_path: SEQ,
    frame_rate: 30,
    playback_range: { start_frame: 0, end_frame: 90 },
    bindings: [
      { id: "cam", kind: "spawnable", actor_class: "/Script/CinematicCamera.CineCameraActor", name: "IntroCamera" },
      { id: "beacon", kind: "possessable", actor_label: "SliceBeacon", name: "Beacon" },
    ],
    tracks: [
      {
        binding: "cam", type: "Transform",
        sections: [{
          start_frame: 0, end_frame: 90,
          keys: [
            { frame: 0, value: { location: { x: 1200, y: 0, z: 300 }, rotation: { pitch: -10, yaw: 180, roll: 0 } }, interpolation: "Cubic" },
            { frame: 90, value: { location: { x: 400, y: 0, z: 180 }, rotation: { pitch: -5, yaw: 180, roll: 0 } }, interpolation: "Cubic" },
          ],
        }],
      },
      {
        binding: "beacon", type: "Float", property: "Glow.Intensity",
        sections: [{
          start_frame: 0, end_frame: 60,
          keys: [
            { frame: 0, value: 0, interpolation: "Linear" },
            { frame: 60, value: 8000, interpolation: "Linear" },
          ],
        }],
      },
      { binding: "master", type: "Camera", sections: [{ start_frame: 0, end_frame: 90, keys: [{ frame: 0, value: "cam" }] }] },
    ],
    save: true,
  }, { label: "2. build the intro sequence with two bound tracks and five keys" });

  await h.call("puerts_sequence_inspect", { asset_path: SEQ }, {
    label: "3. read the sequence back and count the keys that actually landed",
  });

  await h.call("puerts_scene_batch", {
    operations: [{
      op: "upsert_actor", label: "IntroSequencePlayer", class: "/Script/LevelSequence.LevelSequenceActor",
      properties: { LevelSequence: `${SEQ}.LS_SliceIntro` }, folder: "Courtyard/Cinematics",
    }],
  }, { label: "4. place a LevelSequenceActor that points at it" });

  await h.call("puerts_save", {}, { label: "5. save the level before the child render process reads it" });

  const render = await h.call("puerts_sequence_render_start", {
    asset_path: SEQ,
    output_directory: "Saved/MCPGenerated/slice-cinematics",
    resolution_x: 1280,
    resolution_y: 720,
    format: "png",
  }, { label: "6. start rendering the three seconds in a separate UE4 process" });
  const jobId = render?.data?.job_id ?? null;
  if (render?.success === true) {
    h.check(typeof jobId === "string" && jobId.length > 0,
      "6. the render returned a trackable job id", String(jobId));
  }
  if (jobId !== null) {
    const status = await h.call("puerts_job_status", { job_id: jobId }, {
      label: "7. read the asynchronous render state back",
    });
    if (status?.success === true) {
      const state = status.data?.job?.state;
      h.check(["running", "succeeded"].includes(state),
        "7. the render process is running or already completed", String(state));
    }
  }

  h.note("8. render completion is asynchronous",
    "puerts_sequence_render_start returns immediately. The release run polls job_status until terminal and then calls job_result; this slice proves the sequence, placement, save, child-process launch and job tracking compose.");
});
