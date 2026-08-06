# Sequencer Tools

Detail for `puerts_sequence_build`, `puerts_sequence_inspect` and
`puerts_sequence_render_start`.

## `sequence_build`

Creates or updates a `ULevelSequence` from one desired-state spec: display rate,
playback range, actor bindings, tracks, sections and keyframes, in one
transaction. A three-second intro that would take a possessable, a spawnable,
three tracks and five keys as separate calls is one call.

### Frames are display-rate frames

Throughout the spec, converted to tick resolution by the engine's own
`FFrameRate::TransformTime`, so a spec written against what Sequencer shows lands
where it reads. `playback_range` is end-exclusive, the same convention Sequencer
uses.

`frame_rate` defaults to 30 on creation; omitted on an existing sequence it keeps
the current rate. Changing the rate of an existing sequence does **not** move
existing keys (UE4.27 stores key times in ticks), and the response warns when it
changed one.

### Bindings

- `id` is your own id for this binding, used by tracks in this same spec. It is
  not stored in the asset: the asset's identity is the FGuid, which the response
  maps back to this id under `binding_guids`.
- `name` is the binding's display name in Sequencer, and it **is** the
  convergence key together with `kind`. A rerun finds the binding by name rather
  than making a second one.
- `actor_label` is possessable only. A label matching no actor, or more than one,
  is a refusal that names the matches.
- `actor_class` is spawnable only, limited to `/Game/`, `/Script/Engine.` and
  `/Script/CinematicCamera.`. The object template is created as an inner of the
  MovieScene and a `UMovieSceneSpawnTrack` is added with its default true, which
  is what makes a spawnable actually spawn.

### Track types

| `type` | Class | Notes |
|---|---|---|
| `Transform` | `UMovieScene3DTransformTrack` | |
| `Float` | `UMovieSceneFloatTrack` | needs `property` |
| `Bool` | `UMovieSceneBoolTrack` | needs `property` |
| `Visibility` | `UMovieSceneVisibilityTrack` | animates `bHidden`, so value **true means hidden** |
| `Camera` | `UMovieSceneCameraCutTrack` | master track only |

Event and Audio tracks are refused by name: an event track needs a director
Blueprint this bridge does not author, and there is no tool that can produce a
`USoundBase` to put in an audio section.

`binding` is a binding id declared in this spec, or `"master"` for a master
track.

`property` (Float and Bool only) is the property path Sequencer animates, for
example `"Light.Intensity"`. The last segment is the property name and the whole
string is the path, matching `UMovieScenePropertyTrack::SetPropertyNameAndPath`.
It is part of the track's identity, so two Float tracks on one binding with
different properties are two tracks.

### Key values

- Float: a number.
- Bool and Visibility: true or false.
- Transform:
  `{location:{x,y,z}, rotation:{pitch,yaw,roll}, scale:{x,y,z}}`, any subset. An
  omitted component writes no key on those channels rather than writing a zero.
- Camera: the binding id of the camera to cut to, as a string.

`interpolation` defaults to `Cubic` on float channels, matching Sequencer's own
default. It is ignored on bool and camera-cut channels, which have no
interpolation, and the response warns rather than silently dropping it.

### Convergent

Every binding, track, section and key is an upsert keyed on its own identity: a
binding by name and kind, a track by binding plus type plus property, a section by
its frame range, a key by its channel and frame. Each one's satisfied-ness is
re-evaluated immediately before it is written rather than read from the plan,
because the spec is ordered and an earlier entry may have moved the state a later
one depends on.

A rerun of an identical spec writes nothing, reports `converged: true` and
`applied_operation_count: 0`, and leaves the structure hash where it was.

### Additive

It never removes a binding, track, section or key it did not write in this call.
There is no `remove_unlisted`: pruning a sequence someone hand-authored in
Sequencer is a different and more dangerous operation than converging on a spec,
and it is not in this tool. Deleting is a gap, stated rather than half-built.

### Failure-atomic

Any refusal cancels the transaction, runs the asset rollback boundary over the
package, and then decides whether the sequence actually came back by hashing it
again rather than trusting the undo; that answer is `rollback_succeeded`. A
sequence this call created is removed from the Asset Registry and leaves nothing
on disk.

`pre_structure_hash` and `post_structure_hash` are the same value
`sequence_inspect` returns as `structure_hash_sha1`.

## `sequence_render_start`

Renders a `ULevelSequence` to image files and returns a `job_id`.

**Nothing is rendered when this returns.** It spawns a second UE process and
answers. Poll `puerts_job_status` until `state` is no longer `"running"`, then
collect with `puerts_job_result`.

### Formats

The four capture protocols available in the installed build are `png`, `jpg`,
`bmp` and `exr`. AVI is unavailable because the installed engine omits its private
writer.

### Why the legacy path

This is the legacy MovieSceneCapture path, the same one the editor's own Render
Movie button takes in separate-process mode: a
`UAutomatedLevelSequenceCapture` is serialized to a manifest and a second editor
process is launched with `-MovieSceneCaptureManifest`. Movie Render Queue exists
in UE4.27 but ships disabled by default, so it is not used. A command that refused
on every project without an optional plugin turned on would be worse.

### It refuses on an unsaved level or an unsaved sequence

By name. The render process reads both from disk, so rendering with unsaved
changes would produce a correct-looking movie of the previous version and exit
successfully, which a caller cannot tell from a good render. Save with
`puerts_save` first. Also refused during Play In Editor.

### Output

`output_directory` must be inside the project; relative paths resolve against the
project directory. Default `Saved/MCPRenders/<sequence name>`.
`output_format` is the filename format string, for example `"{world}_{frame}"`;
the default is the engine's own. Resolution defaults to 1280x720, clamped to
16..7680. `warm_up_frames` (default 0) are played before the capture starts, to
let particles and post processing settle.

This is the one job in the bridge that can be cancelled immediately, because
`puerts_job_cancel` kills the render process.

## `sequence_inspect`

The independent read half of `sequence_build`, and read only: no transaction is
opened. It reports the same field names `sequence_build` takes, with the raw tick
values beside them under `start_tick` / `end_tick`, and
`structure_hash_sha1` for comparison against a build's `post_structure_hash`.
