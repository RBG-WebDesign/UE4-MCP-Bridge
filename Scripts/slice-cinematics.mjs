#!/usr/bin/env node
// Vertical slice 7 of 7: CINEMATICS.
//
// One prompt: "make a three second intro: the camera pushes in on the beacon
// while it fades up, then cut to black."
//
// What a pass would prove: a LevelSequence with real tracks and keyframes, bound
// to real actors, placed in the level and playable. Nothing in the catalog does
// any part of this. The slice exists to say exactly which primitives are absent
// rather than to fail at an undefined tool name, and it is the one domain where
// no lane owns the work.
//
// What is in the repository today, and why none of it counts:
//   - Plugins/MCPBridge/Content/Python/mcp_bridge/generation/sequence_generator.py
//     creates an EMPTY LevelSequence and sets its playback range. No tracks, no
//     sections, no keys, no bindings. It is reachable only through the legacy
//     prompt_generate pipeline, which is behind MCP_ENABLE_LEGACY_HTTP=1.
//   - title_sequence_bind (legacy_http) writes a JSON scaffold file to disk and
//     says so itself: "full event-track binding needs a dedicated Sequencer
//     bridge tool" (handlers/titles.py:460).
//
//   node Scripts/slice-cinematics.mjs

import { runSlice } from "./slice-harness.mjs";

const SEQ = "/Game/MCPGenerated/LS_SliceIntro";

await runSlice({
  id: "cinematics",
  title: "a LevelSequence with tracks and keyframes",
  proves: "author a sequence with bindings, tracks and keys, read it back, place it in the level and play it",
}, async (h) => {
  h.request("puerts_sequence_build",
    "create or update a LevelSequence: frame rate, playback range, actor bindings, tracks and keyframes. "
    + "No registered tool touches Sequencer at all. The only LevelSequence code in the repository "
    + "(generation/sequence_generator.py) creates an empty asset and sets its playback range, is reachable only "
    + "through the legacy prompt_generate pipeline, and writes no track, no section and no key",
    {
      tool: "puerts_sequence_build",
      owner: "UNOWNED. No lane in this program covers Sequencer",
      params: {
        asset_path: "/Game/MCPGenerated/...",
        frame_rate: "number (default 30)",
        playback_range: "{ start_frame, end_frame }",
        bindings: "[{id, kind:'possessable'|'spawnable', actor_label?|actor_class?, name}]",
        tracks: "[{binding: id|'master', type:'Transform'|'Float'|'Visibility'|'Event'|'Camera'|'Audio', "
          + "property?: 'RelativeLocation'|..., sections: [{start_frame, end_frame, "
          + "keys: [{frame, value, interpolation:'Linear'|'Cubic'|'Constant'}]}]}]",
        save: "boolean",
      },
      returns: "{ compile_status: 'n/a', binding_count, track_count, key_count, structure_hash_sha1 }",
      note: "UE4.27 API surface for this is ULevelSequence, UMovieScene::AddTrack / AddPossessable, "
        + "UMovieScene3DTransformTrack and FMovieSceneDoubleChannel-era FMovieSceneFloatChannel. All editor C++, "
        + "so it belongs in MCPBridgeGraphBuilder next to the other builders.",
    });

  h.request("puerts_sequence_inspect",
    "read a LevelSequence back as machine-readable JSON: bindings, tracks, sections, keyframe times and values, "
    + "and a structure hash. Required by the builder-plus-inspector rule every other builder in this bridge "
    + "follows, and without it a keyframe claim cannot be verified without a human opening Sequencer",
    {
      tool: "puerts_sequence_inspect",
      owner: "UNOWNED",
      params: { asset_path: "string" },
      returns: "{ frame_rate, playback_range, bindings, tracks: [{binding, type, property, sections: [{start_frame, end_frame, keys}]}], "
        + "structure_hash_sha1, package_dirty_before, package_dirty_after }",
    });

  // Even with both of the above, the sequence has to exist in the level as a
  // LevelSequenceActor before anything can play it, and that is lane L's tool.
  h.need("puerts_scene_batch", {
    why: "place a LevelSequenceActor that points at the sequence. puerts_spawn_actor can spawn the actor class but "
      + "cannot set its LevelSequence property or label it",
    request: { tool: "puerts_scene_batch", owner: "lane L (settled, implemented_unverified)" },
  });

  // A one-prompt cinematic result the user can actually watch needs a render.
  h.request("puerts_sequence_render",
    "render a LevelSequence to image frames or a video so the result of a cinematic prompt can be looked at. "
    + "puerts_viewport_screenshot captures one editor viewport frame and cannot follow a sequence's camera cuts",
    {
      tool: "puerts_sequence_render",
      owner: "UNOWNED",
      params: { asset_path: "string", output_directory: "string", resolution: "{width, height}", format: "PNG | JPG", frame_range: "{start_frame, end_frame}" },
      returns: "{ frame_count, output_files, elapsed_ms }",
      note: "UE4.27 ships Movie Render Queue as a plugin; the fallback is the older FSequencerRenderer / "
        + "automated level sequence capture path. Which one is available is a project-settings question, so the "
        + "tool must report which it used.",
    });

  // The steps a working sequence slice would run, recorded so the shape of the
  // slice is reviewable now and executable the day the primitives land.
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
  }, { label: "1. build the intro sequence with two bound tracks and five keys" });

  await h.call("puerts_sequence_inspect", { asset_path: SEQ }, {
    label: "2. read the sequence back and count the keys that actually landed",
  });

  await h.call("puerts_scene_batch", {
    operations: [{
      op: "upsert_actor", label: "IntroSequencePlayer", class: "/Script/LevelSequence.LevelSequenceActor",
      properties: { LevelSequence: `${SEQ}.LS_SliceIntro` }, folder: "Courtyard/Cinematics",
    }],
  }, { label: "3. place a LevelSequenceActor that points at it" });

  await h.call("puerts_sequence_render", {
    asset_path: SEQ,
    output_directory: "Saved/MCPGenerated/slice-cinematics",
    resolution: { width: 1280, height: 720 },
    format: "PNG",
  }, { label: "4. render the three seconds so the result can be looked at" });

  h.note("5. what this slice is really reporting",
    "cinematics is the only one of the seven domains with no owner and no partial support. Four primitives are "
    + "missing and three of them are unowned. The nearest existing code, title_sequence_bind, writes a JSON "
    + "scaffold and its own runtime_note says a dedicated Sequencer bridge tool is required.");
});
