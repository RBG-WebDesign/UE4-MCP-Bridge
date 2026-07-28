# Animation Pose Re-Anchoring Design Spec

## Goal

Re-anchor existing AnimSequences to the current pose of a reference animation (normally
the idle's frame 0) so that editing the idle does not force re-authoring every clip that
blends into or out of it.

Target workflow: edit the idle, run a delta report across the animation folder, re-anchor
the clips that drifted past a threshold. The animator changes one asset instead of twenty.

## Problem

Locomotion and transition clips are authored against a specific idle start pose. When the
idle changes, every clip that blends from it pops at the boundary. The current fix is to
re-export or hand-fix each surrounding clip. The bones are correct in isolation; only the
anchor pose is wrong, so the difference is a per-bone constant rotation offset that can be
computed and applied mechanically.

This is a pose alignment tool. It does not fix motion quality, and it does not fix bad
retarget source data (see Non-Goals).

## Verified API Surface (UE4.27)

Confirmed against the UE4.27 C++ database via the `unreal-api` MCP server. Read side is
reachable from Python; the bone-track write path is not.

| Function | Module / Header | Exposed | Use |
|---|---|---|---|
| `UAnimationBlueprintLibrary::GetBonePosesForFrame` | AnimationModifiers / `AnimationBlueprintLibrary.h` | BlueprintPure | Read reference pose |
| `UAnimationBlueprintLibrary::GetRawTrackRotationData` | same | BlueprintPure | Read local rot keys |
| `UAnimationBlueprintLibrary::GetRawTrackPositionData` | same | BlueprintPure | Read local pos keys |
| `UAnimationBlueprintLibrary::GetAnimationTrackNames` | same | BlueprintPure | Enumerate tracks |
| `UAnimationBlueprintLibrary::GetNumFrames` | same | BlueprintPure | Frame count |
| `UAnimationBlueprintLibrary::GetAdditiveAnimationType` | same | BlueprintPure | Additive guard |
| `UAnimationBlueprintLibrary::GetBoneCompressionSettings` | same | BlueprintPure | Compression audit |
| `UAnimationBlueprintLibrary::FinalizeBoneAnimation` | same | BlueprintCallable | Commit + recompress |
| `UAnimationBlueprintLibrary::GetRawAnimationTrackByName` | same | **C++ only** | Mutable track reference |
| `UAnimSequence::BakeTrackCurvesToRawAnimation` | Engine / `Animation/AnimSequence.h` | **C++ only** | Bake layer curves |

Exact signatures for the two that matter most:

```cpp
// Returns a MUTABLE reference. This is the only bone-track write hook in 4.27.
static FRawAnimSequenceTrack& GetRawAnimationTrackByName(
    const UAnimSequence* AnimationSequence, const FName TrackName);

static void GetBonePosesForFrame(
    const UAnimSequence* AnimationSequence, TArray<FName> BoneNames, int32 Frame,
    bool bExtractRootMotion, TArray<FTransform>& Poses, const USkeletalMesh* PreviewMesh);
```

The key finding: **UE4.27 has no `AddBoneAnimation` or `SetRawTrack*`**. The library exposes
only `RemoveBoneAnimation`, `RemoveAllBoneAnimation`, and `FinalizeBoneAnimation` for bone
tracks. Mutation must go through the `GetRawAnimationTrackByName` reference in C++, which
puts the write path in the existing `MCPBridgeGraphBuilder` plugin rather than in Python.

`AddTransformationCurveKeys` is BlueprintCallable and writes additive layer tracks, but
baking those to raw still needs the C++ call, so it is useful only for prototyping.

## Math

Raw track `RotKeys` are local (parent-relative) rotations. For each bone in the mask:

```
Q_delta  = Q_ref * inverse(Q_target[anchor_frame])
Q_out[k] = slerp(Q_target[k], Q_delta * Q_target[k], w(k))
```

`Q_ref` is the reference animation's rotation for that bone at its anchor frame.
`w(k)` is the weight profile evaluated at key index `k`.

Rotation only by default. Bone lengths are fixed by the skeleton, so rebasing translation
on limb bones stretches the character. Position keys are only touched when
`include_translation` is explicitly set, and even then only for bones the caller names.

## Weight Profiles

| Profile | `w(k)` | Use |
|---|---|---|
| `constant` | 1.0 for all k | Idle variants, additive bases. Rigidly re-poses the whole clip. |
| `decay` | 1.0 at anchor, easing to 0.0 over `window_frames` | Transitions out of idle. Snaps the start, preserves the tail. |
| `both_ends` | 1.0 at first and last frame, 0.0 in the middle | Loops that must start and end on the idle pose. |

`decay` uses smoothstep rather than linear so the correction has no velocity discontinuity
at the point where it reaches zero.

## Safety Defaults

These are defaults, not options, because getting them wrong silently corrupts assets:

- **Rotation only.** `include_translation` defaults false.
- **Root and pelvis excluded** from the default bone mask. Rebasing them breaks root motion
  and foot planting.
- **Default mask is upper body** (`spine_01` and descendants). Legs re-anchored without a
  matching IK pass will float or sink.
- **Additive sequences refused.** If `GetAdditiveAnimationType != AAT_None` the raw data is
  already a delta and re-anchoring means something different. Return an error, do not guess.
- **`dry_run` defaults true** on every mutating command. `FinalizeBoneAnimation` triggers
  recompression and overwrites raw data.

## Implementation Details Worth Pinning Down

**Constant tracks.** `FRawAnimSequenceTrack::RotKeys` may hold a single key, meaning the
bone is static for the whole clip. A time-varying profile (`decay`, `both_ends`) cannot be
applied to a 1-key track as-is; the track must first be expanded to `NumFrames` keys by
repeating the constant value. `constant` profile does not need expansion.

**Bones without tracks.** Not every skeleton bone has a raw track. `GetAnimationTrackNames`
gives the real list. A masked bone with no track has no keys to modify and must be reported
in the response as `skipped_no_track`, never silently dropped.

**Finalize once.** `FinalizeBoneAnimation` recompresses the whole sequence and is expensive.
Call it once after all tracks are mutated, not per bone.

**Coordinate space: resolved, local.** `GetBonePosesForFrame` returns local
(parent-relative) transforms. Verified in-editor against `GetRawTrackRotationData` at frame
0 for `pelvis`, `spine_01`, `clavicle_l`, and `hand_l`: all four agree to four decimal
places, delta 0.0000 degrees. The delta math in this spec is therefore correct as written,
and `preview_mesh` has no bearing on the returned space, so leaving it `None` is safe.

## Root Motion Analysis

A separate diagnostic sharing the same read path. It answers "is this clip sliding,
jittering, or both, and is the root authored backwards" with numbers instead of a checklist.

Motivating case: `SK_Donathan_Idle_Final` reported a 3.35 cm X / 2.34 cm Y root range over
556 frames. Range alone cannot separate a smooth authored sway (~0.028 cm/frame, which reads
as sliding) from per-frame step noise (which reads as jiggle). Those have different causes
and different fixes, so the tool reports both.

### Metrics

Read root position keys `P[k]` and rotation keys `Q[k]` from the raw track.

**Range.** Per-axis max minus min. What a naive script reports. Kept for continuity, but it
is the weakest signal of the four.

**Step distribution.** First differences, `d[k] = |P[k+1] - P[k]|`. Report mean, max, and
95th percentile in cm/frame. Rotation equivalent: angle between consecutive quaternions in
degrees per frame.

**Roughness.** Second differences, `c[k] = |P[k+1] - 2*P[k] + P[k-1]|`, reported as
`roughness = mean(c) / mean(d)`. Dimensionless.

- Near 0: consecutive steps point the same way. Smooth authored sway, i.e. sliding.
- Near or above 1: direction reverses most frames. Quantization stepping or noise, i.e. jiggle.

The 1.0 boundary is empirical, not derived. It has to be calibrated against a clip that is
known to look correct before it can be trusted as a pass/fail gate. Until then it is a
ranking signal, not a verdict.

**Root vs pelvis split.** Compare root translation range against pelvis local translation
range. Normal authoring pins the root and carries weight shift on the pelvis, so the inverse
means motion was baked onto root at export. Flag `inverted_root_authoring` when root range
exceeds 1.0 cm and pelvis local range is under half of it. Heuristic thresholds, tunable.

**Compression audit.** `GetBoneCompressionSettings` returns the `UAnimBoneCompressionSettings`
asset. Report the codec class name at minimum. The per-codec error threshold lives on the
codec object inside the settings and may need reflection to reach, so treat it as best
effort and report null rather than guessing when it is not readable. This matters because
aggressive key reduction on a long clip can quantize a smooth sub-millimetre-per-frame sway
into visible steps at runtime, and no amount of raw-track inspection will reveal that.

### Response Contract (anim_root_motion_analyze)

```json
{
  "success": true,
  "data": {
    "sequence": "/Game/.../SK_Donathan_Idle_Final",
    "num_frames": 556,
    "length_seconds": 18.53,
    "root": {
      "range_cm": {"x": 3.35, "y": 2.34, "z": 0.08},
      "yaw_range_degrees": 0.92,
      "step_cm_per_frame": {"mean": 0.028, "max": 0.061, "p95": 0.049},
      "step_degrees_per_frame": {"mean": 0.004, "max": 0.011, "p95": 0.009},
      "roughness": 0.07,
      "classification": "smooth_drift"
    },
    "pelvis": {"local_range_cm": {"x": 0.07, "y": 0.05, "z": 0.02}},
    "flags": ["inverted_root_authoring"],
    "compression": {"codec": "UAnimCompress_PerTrackCompression", "error_threshold": null}
  },
  "error": null
}
```

`classification` is one of `static`, `smooth_drift`, `stepped`, `noisy`, derived from the
step distribution and roughness together. It summarizes the numbers above it; it is not an
independent measurement.

### What This Cannot See

It reads raw track data, which is what the asset stores at import. Runtime causes cannot
appear here by construction: foot IK reacting to a drifting root, and decompression
artifacts at evaluation time, both happen downstream of this data. A clip that reports
`smooth_drift` with low roughness and still looks jittery in PIE points at the AnimBP or the
codec rather than at the raw data. That negative result is most of the value of running it.

## Architecture

Split by direction, because the read and write paths have different requirements.

Every read the tool needs is BlueprintPure and therefore reachable from Python. Nothing in
the analysis or diff path requires C++ at all:

```
MCP tool (TS) --HTTP--> handlers/animation.py --> unreal.AnimationLibrary (Python)
```

Only the mutation path needs the plugin, because `GetRawAnimationTrackByName` is the sole
bone-track write hook and is not BlueprintCallable:

```
MCP tool (TS) --HTTP--> handlers/animation.py --> UAnimPoseLibrary (C++) --> raw tracks
```

**This means Passes 1 and 2 ship with no C++ and no plugin rebuild.** The read-only tools
can run against a live editor immediately, which matters because the whole point of
front-loading them is to validate the numbers before any code can write. The
`AnimationModifiers` dependency and the C++ library only land in Pass 3.

Doing the analysis math in Python costs nothing at this scale: a 556-frame clip across a
few dozen bones is trivial, and the per-key quaternion work in the write path is in C++
regardless.

### Module Dependencies

Pass 3 only. Add to `MCPBridgeGraphBuilder.Build.cs` `PrivateDependencyModuleNames`:

```csharp
"AnimationModifiers",   // UAnimationBlueprintLibrary, FRawAnimSequenceTrack access
```

`AnimGraph`, `AnimGraphRuntime`, and `Persona` are already present from the Anim Blueprint
Builder work. No new editor-only deps.

### File Structure

Python, all passes:

```
handlers/animation.py                    # All six commands
utils/anim_math.py                       # Delta math, weight profiles, statistics
```

C++, Pass 3 onward:

```
Public/
  AnimPoseLibrary.h                      # Public API (2 UFUNCTIONs)

Private/AnimPose/
  AnimPoseLibrary.cpp                    # Dispatcher
  APSpec.h                               # Parsed request structs
  APJsonParser.h / .cpp                  # JSON -> FAPReanchorSpec
  APValidator.h / .cpp                   # Additive check, mask check, track existence
  APReanchor.h / .cpp                    # Weight profiles, track expansion, mutation
```

### C++ Public API

Write path only. Everything read-only lives in Python.

```cpp
UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UAnimPoseLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Mutating unless bDryRun. Returns JSON report.
    UFUNCTION(BlueprintCallable, CallInEditor, Category="AnimPose")
    static FString ReanchorAnimation(UAnimSequence* Target, UAnimSequence* Reference,
                                     const FString& OptionsJSON, bool bDryRun);

    // Validate options without touching assets.
    UFUNCTION(BlueprintCallable, CallInEditor, Category="AnimPose")
    static FString ValidateReanchorJSON(const FString& OptionsJSON);
};
```

## MCP Tools

| Tool | Command | Mutating | Needs C++ |
|---|---|---|---|
| `anim_pose_snapshot` | `anim_pose_snapshot` | no | no |
| `anim_pose_delta` | `anim_pose_delta` | no | no |
| `anim_root_motion_analyze` | `anim_root_motion_analyze` | no | no |
| `anim_reanchor` | `anim_reanchor` | Pass 3 | Pass 3 |
| `anim_batch_reanchor` | `anim_batch_reanchor` | Pass 3 | Pass 3 |

`anim_root_motion_analyze` takes either `sequence_path` or `folder_path`. Folder mode
returns one entry per sequence sorted by roughness descending, so the clips most likely to
be jittering surface first without needing a separate batch tool.

All five are registered in `COMMAND_ROUTES` in `router.py`. None are in the
`modifyingCommands` set in `mcp-server/src/index.ts` and none carry `@transactional` yet:
every command is dry-run only until Pass 3, and recording a dry run in the undo history
would be wrong. Adding both is a Pass 3 step.

### Options Contract

```json
{
  "reference_path": "/Game/.../SK_Donathan_Idle",
  "reference_frame": 0,
  "target_path": "/Game/.../SK_Donathan_Possessed",
  "anchor_frame": 0,
  "bone_mask": {
    "include_subtrees": ["spine_01"],
    "include_bones": [],
    "exclude_bones": ["root", "pelvis"]
  },
  "profile": "decay",
  "window_frames": 12,
  "include_translation": false,
  "threshold_degrees": 1.5
}
```

### Response Contract (anim_reanchor)

```json
{
  "success": true,
  "data": {
    "target": "/Game/.../SK_Donathan_Possessed",
    "dry_run": true,
    "num_frames": 61,
    "bones_modified": 14,
    "skipped_no_track": ["ik_hand_gun"],
    "max_delta_degrees": 18.4,
    "deltas": [
      {"bone": "clavicle_l", "delta_degrees": 18.4, "keys_written": 61},
      {"bone": "upperarm_l", "delta_degrees": 11.2, "keys_written": 61}
    ]
  },
  "error": null
}
```

`threshold_degrees` filters both the report and the write: bones under it are left alone so
that noise does not cause needless recompression.

## Implementation Passes

Status: Passes 1 and 2 are written and their unit tests pass. Pass 1 has been run against a
live editor; see Live Verification below for what that did and did not establish. Pass 2 is
dry-run only and has not been run live. Pass 3 is not started.

**Pass 1 -- Read path. Python only, no plugin rebuild.** `anim_pose_snapshot`,
`anim_pose_delta`, and `anim_root_motion_analyze`. No mutating code exists anywhere in the
build yet. Acceptance, in order:

1. Run `anim_root_motion_analyze` on `SK_Donathan_Idle_Final` and confirm the reported
   range reproduces the known 3.35 / 2.34 cm figures. That validates the reader against a
   result obtained independently.
2. Confirm the step distribution and roughness classify it as `smooth_drift`, and that the
   `inverted_root_authoring` flag fires given the near-stationary pelvis.
3. Calibrate the roughness boundary against a clip that is known to look correct.
4. Confirm the reported degree deltas on the Donathan clips match what is visible in the
   editor.

The coordinate space item that used to sit here is resolved; see Resolved: Coordinate Space.

Item 1 is the reason this pass exists. Reproducing a number that was arrived at by other
means is the only cheap check available that the raw track reader is correct at all.

**Pass 2 -- Dry run.** Full delta math and weight profiles in Python, reporting what would
be written without writing. Still no C++. Acceptance: dry-run output at `w=1` matches the
Pass 1 delta report for the same clip and mask.

Pass 2 additions worth noting: bone mask resolution uses `find_bone_path_to_root`, so
`include_subtrees` works without the caller knowing the skeleton hierarchy, and it stays
inside the BlueprintPure read set. `anim_reanchor` refuses `dry_run=False` with an explicit
Pass 3 message rather than ignoring the flag, and refuses additive sequences on both sides.

**Pass 3 -- Mutation. First pass that needs C++.** Adds `UAnimPoseLibrary`, the
`AnimationModifiers` dependency, and a plugin rebuild. Track expansion for constant tracks,
key mutation through the `GetRawAnimationTrackByName` reference, a single
`FinalizeBoneAnimation`, then dirty and save. Acceptance: re-anchor a duplicated test clip,
confirm frame 0 matches the reference within tolerance, confirm the tail is unchanged under
`decay`, confirm undo restores the original.

Two registration steps belong to Pass 3 and are easy to miss, since Pass 2 deliberately
left them undone: add `anim_reanchor` and `anim_batch_reanchor` to the `modifyingCommands`
set in `mcp-server/src/index.ts`, and add `@transactional` to the two mutating handlers.
Neither was done earlier because a dry run is not a modification, and recording one in the
undo history would be wrong.

**Pass 4 -- Batch. Dry-run half landed early.** `anim_batch_reanchor` sweeps a folder or an
explicit list, ranks by drift, and reports per-clip verdicts. Pulled forward ahead of Pass 3
because the game project had already registered the command, and a route whose handler this
repo does not define blocks reconciliation entirely. The write half stays in Pass 3.
Acceptance: sweep the Blends folder, confirm the ranked drift report is stable across
repeat runs.

### Drift Verdicts

Batch mode triages each clip rather than only ranking it, because ranking alone invites the
wrong conclusion. A live Pass 2 dry run of `SK_Donathan_Run_180_Left` against
`SK_Donathan_Idle_Final` reported 80.65 degrees on `index_03_r` and 79.33 on `lowerarm_r`,
with `spine_01` at 2.68. That is not drift. A run turn legitimately starts with the arms
somewhere an idle never puts them, and re-anchoring it would drag the character into a pose
the animator never authored.

| Verdict | Condition | Meaning |
|---|---|---|
| `aligned` | max delta < `threshold_degrees` | Already matches; nothing to do |
| `drifted` | threshold <= max < `review_ceiling_degrees` | Normal re-anchor candidate |
| `divergent` | max >= `review_ceiling_degrees` | Probably a different start pose, not drift |

`review_ceiling_degrees` defaults to 30 and is configurable. Like the roughness boundary it
is empirical rather than derived. The `divergent` summary states the reason in full, since
it is the guard standing between a batch sweep and a bad apply once Pass 3 exists.

**Pass 5 -- Skill and playbook.** `.claude/skills/anim-pose-reanchoring/` with the workflow
and the safety defaults, plus `docs/playbooks/pose-anchored-animation-set.md` from
`docs/playbooks/_TEMPLATE.md`.

## Live Verification

`anim_root_motion_analyze` has been run against a live UE4.27 editor over HTTP. What that
established, and what it did not:

**Established.** The bindings work end to end: `unreal.AnimationLibrary` resolves, the raw
track reads return data, and the listener round trip is sound. On
`SK_Donathan_Run_180_Left` the tool reported `rotation_excursion_degrees` of 176.77 across
22 frames. An asset named "Run 180 Left" measuring ~177 degrees of rotation is strong
internal evidence that the rotation path is correct, since the expected answer was knowable
in advance from the asset name alone. A 76.65 cm Y range over a 22-frame run turn is
likewise plausible.

**Not established.** The planned first acceptance check was to reproduce the independently
obtained 3.35 / 2.34 cm figures on `SK_Donathan_Idle_Final`. That did not happen, because
the asset had already been stabilized before the tool ran on it: the live result reports
`has_root_track: false`, zero range, and `classification: static`. Reproducing a known
answer on a changed asset is not possible. If the pre-fix revision exists in source control,
running the tool against it is still the cheapest available check that the position reader
is numerically correct; without it, the translation path rests on plausibility rather than
on a reproduced measurement.

Note also that `yaw_range_degrees` and `rotation_excursion_degrees` came back identical
(176.7715). That is expected for a pure-yaw turn measured from an endpoint, so it is
consistent rather than suspicious, but it means the two did not act as independent checks
on this clip.

The reported `roughness` of 0.4205 sits just under the 0.5 `smooth_drift` boundary on a
22-frame clip with a 7.14 cm mean step. Roughness is noisy at that few keys and that large a
step, so this clip is not a good calibration anchor. The threshold calibration item remains
open.

**Schema divergence.** The live run returned a flat response shape (`sequence_path`,
`has_root_track`, `step_mean_cm`, `compression_settings`, `range_cm.xy_magnitude`) that does
not match this repo's nested shape (`sequence`, `root.step_cm_per_frame.{mean,max,p95}`,
`pelvis.local_range_cm`, `compression.codec`). Two implementations therefore exist: the one
tracked here and the one running in the game project.

## Reconciliation

`Scripts/reconcile_animation_schema.py` merges a game project's animation handlers to this
repo's canonical schema. Dry run by default; `--apply` writes, with `.bak` copies.

**The response schema is decided entirely by Python.** The TypeScript tools call
`client.sendCommand` and `JSON.stringify` the result without reshaping it, so reconciling the
schema means replacing the project's `handlers/animation.py`. The TS layer only affects tool
registration and parameter names.

**Why not `install-mcp-bridge.ps1`.** That script copies `Plugins/MCPBridge` wholesale with
`Copy-Item -Force`, which overwrites `router.py`. A project carrying routes this repo does
not have (locomotion, PIE agent, engine source search) would lose them: the handler files
survive, but their imports and routes do not, so those commands start returning "Unknown
command". The reconcile script patches only the four animation entries and leaves every
other route untouched.

**The failure mode the preflight exists to stop.** If the project router imports an animation
handler this repo does not define, copying `animation.py` over it makes the import raise, and
a router that cannot import is a bridge that answers nothing. That is not a degraded
animation feature; it is a dead listener. The script refuses to write in that state and names
both the missing handler and the commands that would break. `anim_batch_reanchor` is the live
example: it is Pass 4 here and not yet implemented, so a project that already registered it
must either drop the route or have the handler ported into this repo first.

Verified against fixtures covering the abort path, a project with extra unrelated routes, a
fresh project needing the import block inserted, and a repeat run proving idempotency.

## Testing

Unit tests in `mcp-server/tests/animation-tools.test.ts` against `mock-server.ts`, covering
schema validation, default values (`dry_run` true, `include_translation` false), and error
plumbing. Add the file to the `npm test` chain in the workspace `package.json`.

The statistics in `utils/anim_math.py` are pure functions over key arrays and need no editor
at all, so they get direct tests against synthetic input: a linear ramp should produce
roughness near 0, an alternating sawtooth near 2, a constant track near 0 with zero step
mean. Those cases pin the classification boundaries without a UE4 round trip and are worth
writing before the handler that calls them.

Integration tests in `mcp-server/tests/integration/` against a live editor: snapshot round
trip, dry-run determinism, and a mutation test on a duplicated throwaway asset. Never
against a source asset.

## Risks

**Recompression cost.** `FinalizeBoneAnimation` is expensive per sequence. A batch over a
large folder is slow and holds the game thread. Batch must process sequentially with
progress reporting, and the 60s `UnrealClient` timeout will need a job-style async path if
folders are large. `cpp_build` already established that pattern in `cpp.ts`.

**Destructive to raw data.** There is no built-in raw backup other than what
`BakeTrackCurvesToRawAnimation` does for curves. Assets must be under source control before
a non-dry run. The playbook should state this as a precondition.

**Blend-space and montage references.** Re-anchoring a clip does not update montage section
timings or blend space sample positions. Those stay valid because frame count and length are
unchanged, but this should be verified in Pass 3 rather than assumed.

## Non-Goals

**Fixing bad retargets.** If a clip has wrong bone rotations from a bad retarget base pose,
re-anchoring bakes that error into a different error. The retarget must be fixed upstream in
the Retarget Manager first, then re-anchoring keeps the set consistent afterward. Running
these in the wrong order costs the set twice.

**Motion quality.** A rigid delta blended over a short window reads as the character being
dragged into position if the new idle moved a lot. Window length is an art call per clip.

**IK chain correction.** Foot and hand IK bones are not solved. They are excluded from the
default mask and left to the existing IK setup.

## Verified Python Bindings

Confirmed against a live UE4.27 editor, not inferred:

```python
unreal.AnimationLibrary.get_bone_poses_for_frame(
    animation_sequence, bone_names, frame, extract_root_motion, preview_mesh=None
) -> Array(Transform)
```

Three things this settles:

- The `meta=(ScriptName="AnimationLibrary")` specifier does produce `unreal.AnimationLibrary`,
  so the class name in every call site is correct.
- The `TArray<FTransform>&` out-parameter marshals as the **direct return value** rather than
  requiring a pre-allocated argument. Results come back as a Python list matching `bone_names`
  one-to-one.
- `preview_mesh` is optional and defaults to `None`.

Directly verified for `get_bone_poses_for_frame` only. The other reads
(`get_raw_track_rotation_data`, `get_raw_track_position_data`,
`get_bone_compression_settings`) use the same out-parameter pattern in C++, so the same
marshaling is expected but has not been observed. Confirm each on first use rather than
assuming; the cost is one `python_proxy` call apiece.

## Resolved: Coordinate Space

`get_bone_poses_for_frame` returns **local (parent-relative)** transforms. Measured against
`get_raw_track_rotation_data` at frame 0:

| Bone | `get_bone_poses_for_frame` | `get_raw_track_rotation_data` | Delta |
|---|---|---|---|
| `pelvis` | (89.6534, -51.4840, -51.8454) | (89.6534, -51.4840, -51.8454) | 0.0000 deg |
| `spine_01` | (0.4449, 3.4751, 0.0976) | (0.4449, 3.4751, 0.0976) | 0.0000 deg |
| `clavicle_l` | (-76.4938, -96.9320, 110.6357) | (-76.4938, -96.9320, 110.6357) | 0.0000 deg |
| `hand_l` | (1.3926, 2.9340, -87.4916) | (1.3926, 2.9340, -87.4916) | 0.0000 deg |

Consequences: the delta math is correct as written, `preview_mesh` has no bearing on the
returned space so leaving it `None` is safe, and the fallback plan of dropping
`get_bone_poses_for_frame` in favor of reading raw tracks on both sides is not needed.

No open questions remain blocking Pass 1.
