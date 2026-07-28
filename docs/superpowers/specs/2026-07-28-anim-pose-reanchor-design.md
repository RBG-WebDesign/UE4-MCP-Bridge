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
| `UAnimationBlueprintLibrary::GetAnimationTrackNames` | same | BlueprintPure | Enumerate tracks |
| `UAnimationBlueprintLibrary::GetNumFrames` | same | BlueprintPure | Frame count |
| `UAnimationBlueprintLibrary::GetAdditiveAnimationType` | same | BlueprintPure | Additive guard |
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

**Coordinate space check.** `GetBonePosesForFrame` returns `FTransform` per bone; whether
that is local or component space needs confirming in-editor before the math is trusted.
Pass 1 acceptance includes verifying it agrees with `GetRawTrackRotationData` for the same
bone and frame. If it turns out to be component space, the reference read switches to
`GetRawTrackRotationData` for both sides.

## Architecture

Same shape as the existing Inspector/Mutator split: a C++ `UBlueprintFunctionLibrary`
exposed to Python, a thin Python handler, a TS tool definition.

```
MCP tool (TS) --HTTP--> handlers/animation.py --> UAnimPoseLibrary (C++) --> UAnimSequence raw tracks
```

### Module Dependencies

Add to `MCPBridgeGraphBuilder.Build.cs` `PrivateDependencyModuleNames`:

```csharp
"AnimationModifiers",   // UAnimationBlueprintLibrary, FRawAnimSequenceTrack access
```

`AnimGraph`, `AnimGraphRuntime`, and `Persona` are already present from the Anim Blueprint
Builder work. No new editor-only deps.

### File Structure

```
Public/
  AnimPoseLibrary.h                      # Public API (4 UFUNCTIONs)

Private/AnimPose/
  AnimPoseLibrary.cpp                    # Dispatcher
  APSpec.h                               # Parsed request structs
  APJsonParser.h / .cpp                  # JSON -> FAPReanchorSpec
  APValidator.h / .cpp                   # Additive check, mask check, track existence
  APPoseReader.h / .cpp                  # Reference pose extraction
  APReanchor.h / .cpp                    # Delta math, weight profiles, track mutation
```

### C++ Public API

```cpp
UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UAnimPoseLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Read-only. Returns JSON pose snapshot.
    UFUNCTION(BlueprintCallable, CallInEditor, Category="AnimPose")
    static FString CapturePose(UAnimSequence* Sequence, int32 Frame, const FString& BoneMaskJSON);

    // Read-only. Returns JSON per-bone angle deltas, sorted worst first.
    UFUNCTION(BlueprintCallable, CallInEditor, Category="AnimPose")
    static FString AnalyzePoseDelta(UAnimSequence* Reference, int32 ReferenceFrame,
                                    UAnimSequence* Target, int32 TargetFrame,
                                    const FString& BoneMaskJSON);

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

| Tool | Command | Mutating |
|---|---|---|
| `anim_pose_snapshot` | `anim_pose_snapshot` | no |
| `anim_pose_delta` | `anim_pose_delta` | no |
| `anim_reanchor` | `anim_reanchor` | yes |
| `anim_batch_reanchor` | `anim_batch_reanchor` | yes |

Register `anim_reanchor` and `anim_batch_reanchor` in the `modifyingCommands` set in
`mcp-server/src/index.ts`. Register all four in `COMMAND_ROUTES` in `router.py`. Both
mutating handlers use `@transactional` from `utils/transactions.py`.

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

### Response Contract

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

**Pass 1 -- Read path.** `CapturePose` and `AnalyzePoseDelta` in C++, `anim_pose_snapshot`
and `anim_pose_delta` tools. No mutation anywhere in the build. Acceptance: point it at the
real Donathan clips and confirm the reported degree deltas match what is visible in the
editor, and confirm the coordinate space question above.

**Pass 2 -- Dry run.** `ReanchorAnimation` with `bDryRun` forced true. Full delta math and
weight profiles, reporting what it would write without writing. Acceptance: dry-run output
on a clip matches the Pass 1 delta report at `w=1`.

**Pass 3 -- Mutation.** Enable the write path: track expansion for constant tracks, key
mutation, single `FinalizeBoneAnimation`, dirty + save. Acceptance: re-anchor a duplicated
test clip, confirm frame 0 matches the reference within tolerance, confirm the tail is
unchanged under `decay`, confirm undo restores the original.

**Pass 4 -- Batch.** `anim_batch_reanchor` over a content folder with an include filter and
per-asset dry-run report before any write. Acceptance: sweep the Blends folder, confirm the
ranked drift report is stable across repeat runs.

**Pass 5 -- Skill and playbook.** `.claude/skills/anim-pose-reanchoring/` with the workflow
and the safety defaults, plus `docs/playbooks/pose-anchored-animation-set.md` from
`docs/playbooks/_TEMPLATE.md`.

## Testing

Unit tests in `mcp-server/tests/animation-tools.test.ts` against `mock-server.ts`, covering
schema validation, default values (`dry_run` true, `include_translation` false), and error
plumbing. Add the file to the `npm test` chain in the workspace `package.json`.

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

## Open Question

The Python binding names for `UAnimationBlueprintLibrary` follow its
`meta=(ScriptName="AnimationLibrary")` specifier, so calls should be
`unreal.AnimationLibrary.get_bone_poses_for_frame(...)`. This is inferred from the UCLASS
specifier and needs confirming through `python_proxy` against a live editor before the
handler is written, per the prototyping rule in CLAUDE.md.
