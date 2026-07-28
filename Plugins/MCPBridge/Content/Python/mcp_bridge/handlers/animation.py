"""Animation pose analysis handlers (Pass 1, read-only).

Thin Python bridge over unreal.AnimationLibrary (UAnimationBlueprintLibrary in
the AnimationModifiers module). Every call in this module is BlueprintPure on
the C++ side, so nothing here mutates an asset and nothing here needs the
MCPBridgeGraphBuilder C++ plugin. The mutating re-anchor path lands in Pass 3
and does need both.

Deliberately no @transactional decorator: wrapping a pure read in a UE4
transaction pollutes the undo stack with empty entries.

Binding note: get_bone_poses_for_frame is verified against a live 4.27 editor
and returns local (parent-relative) transforms, matching raw track data to four
decimal places. The other reads share the same single-out-parameter shape in
C++ but have not been individually observed, so _call reports the exact method
name when one is missing or throws.
"""

from typing import Any, Dict, List, Optional, Sequence, Tuple

from mcp_bridge.utils import anim_math as am

# A root track wandering more than this, with a pelvis that barely moves
# relative to it, means the motion was baked onto root at export instead of
# being carried by the hips. Centimetres.
INVERTED_ROOT_MIN_RANGE_CM = 1.0
INVERTED_ROOT_PELVIS_RATIO = 0.5


def _fail(message: str) -> Dict[str, Any]:
    return {"success": False, "data": {}, "error": message}


def _ok(data: Dict[str, Any]) -> Dict[str, Any]:
    return {"success": True, "data": data, "error": None}


def _anim_library() -> Any:
    """Return unreal.AnimationLibrary, or raise with a usable explanation."""
    import unreal

    lib = getattr(unreal, "AnimationLibrary", None)
    if lib is None:
        raise RuntimeError(
            "unreal.AnimationLibrary not found. UAnimationBlueprintLibrary lives in "
            "the AnimationModifiers module; confirm the Animation Modifiers plugin "
            "is enabled for this project."
        )
    return lib


def _call(lib: Any, method: str, *args: Any) -> Any:
    """Call a method on AnimationLibrary, naming it precisely on failure.

    The first live run of this module is also the confirmation that these
    bindings exist and marshal as expected, so a failure has to say which call
    broke rather than surfacing a bare AttributeError.
    """
    fn = getattr(lib, method, None)
    if fn is None:
        raise RuntimeError(
            f"unreal.AnimationLibrary.{method} not found in this build"
        )
    try:
        return fn(*args)
    except Exception as exc:
        raise RuntimeError(f"unreal.AnimationLibrary.{method} failed: {exc}") from exc


def _load_sequence(path: str) -> Tuple[Optional[Any], Optional[str]]:
    """Load an AnimSequence by content path. Returns (sequence, error)."""
    import unreal

    if not path:
        return None, "Missing sequence path"
    clean = path.split(".")[0]
    if not unreal.EditorAssetLibrary.does_asset_exist(clean):
        return None, f"Asset does not exist: {clean}"
    asset = unreal.load_asset(clean)
    if asset is None:
        return None, f"Failed to load asset: {clean}"
    if not isinstance(asset, unreal.AnimSequence):
        return None, (
            f"Asset is not an AnimSequence (got {type(asset).__name__}): {clean}"
        )
    return asset, None


def _is_anim_sequence_by_registry(path: str) -> bool:
    """Cheap class check that avoids loading the asset.

    Returns True when the registry cannot answer, so the caller falls back to
    loading and doing a real isinstance check rather than silently skipping a
    sequence because the metadata shape differed.
    """
    import unreal

    try:
        data = unreal.EditorAssetLibrary.find_asset_data(path)
        if data is None:
            return True
        asset_class = getattr(data, "asset_class", None)
        if asset_class is None:
            return True
        return str(asset_class) == "AnimSequence"
    except Exception:
        return True


def _quat_tuple(quat: Any) -> am.Quat:
    return (float(quat.x), float(quat.y), float(quat.z), float(quat.w))


def _vec_tuple(vec: Any) -> am.Vec3:
    return (float(vec.x), float(vec.y), float(vec.z))


def _euler_or_none(quat: Any) -> Optional[Dict[str, float]]:
    """Best-effort Rotator conversion for human-readable output.

    Returns None rather than raising if the binding is not available, because
    every number this module actually computes comes from the quaternion.
    """
    try:
        rot = quat.rotator()
        return {
            "pitch": float(rot.pitch),
            "yaw": float(rot.yaw),
            "roll": float(rot.roll),
        }
    except Exception:
        return None


def _track_names(lib: Any, seq: Any) -> List[str]:
    return [str(n) for n in _call(lib, "get_animation_track_names", seq)]


def _sequence_meta(lib: Any, seq: Any) -> Dict[str, Any]:
    """Frame count, length, and additive status for a sequence."""
    additive_raw = _call(lib, "get_additive_animation_type", seq)
    additive_name = str(additive_raw)
    return {
        "num_frames": int(_call(lib, "get_num_frames", seq)),
        "length_seconds": float(_call(lib, "get_sequence_length", seq)),
        "additive_type": additive_name,
        # Reported so callers can see the raw enum too; the re-anchor pass in
        # Pass 3 refuses additive clips outright.
        "is_additive": "NONE" not in additive_name.upper(),
    }


def _bone_rotations_at_frame(
    lib: Any, seq: Any, bones: Sequence[str], frame: int
) -> Dict[str, am.Quat]:
    """Local rotation per bone at a frame, keyed by bone name.

    The returned array is documented as matching bone_names one to one. If it
    ever does not, zip would silently truncate and every downstream delta would
    be attributed to the wrong bone, so the lengths are checked explicitly.
    """
    poses = _call(lib, "get_bone_poses_for_frame", seq, list(bones), frame, False, None)
    if len(poses) != len(bones):
        raise RuntimeError(
            f"get_bone_poses_for_frame returned {len(poses)} transforms for "
            f"{len(bones)} bones; cannot align results to bone names"
        )
    return {name: _quat_tuple(t.rotation) for name, t in zip(bones, poses)}


def handle_anim_pose_snapshot(params: Dict[str, Any]) -> Dict[str, Any]:
    """Capture the local pose of an AnimSequence at one frame.

    Args:
        params:
            - sequence_path (str): AnimSequence content path. Required.
            - frame (int): Frame index. Default 0.
            - bones (list[str]): Bone names. Default: every animated track.

    Returns:
        Per-bone local rotation (quaternion, plus Rotator when available),
        translation, and scale, with sequence metadata.
    """
    try:
        seq, err = _load_sequence(params.get("sequence_path", ""))
        if err:
            return _fail(err)

        lib = _anim_library()
        frame = int(params.get("frame", 0))
        meta = _sequence_meta(lib, seq)

        if frame < 0 or frame >= max(meta["num_frames"], 1):
            return _fail(
                f"Frame {frame} out of range; sequence has {meta['num_frames']} frames"
            )

        available = _track_names(lib, seq)
        requested = params.get("bones") or available
        bones = [b for b in requested if b in available]
        missing = [b for b in requested if b not in available]
        if not bones:
            return _fail(
                "No requested bone has an animated track. Available tracks: "
                + ", ".join(available[:40])
            )

        poses = _call(lib, "get_bone_poses_for_frame", seq, bones, frame, False, None)
        entries: List[Dict[str, Any]] = []
        for name, transform in zip(bones, poses):
            quat = transform.rotation
            entries.append({
                "bone": name,
                "rotation_quat": list(_quat_tuple(quat)),
                "rotation_euler": _euler_or_none(quat),
                "translation": list(_vec_tuple(transform.translation)),
                "scale": list(_vec_tuple(transform.scale3d)),
            })

        data = {
            "sequence": params.get("sequence_path"),
            "frame": frame,
            "bone_count": len(entries),
            "bones": entries,
            "skipped_no_track": missing,
        }
        data.update(meta)
        return _ok(data)
    except Exception as exc:
        return _fail(str(exc))


def handle_anim_pose_delta(params: Dict[str, Any]) -> Dict[str, Any]:
    """Compare two sequences' local poses bone by bone.

    Args:
        params:
            - reference_path (str): Reference AnimSequence, normally the idle.
            - target_path (str): Sequence being checked against it.
            - reference_frame (int): Default 0.
            - target_frame (int): Default 0.
            - bones (list[str]): Default: tracks present in both sequences.
            - threshold_degrees (float): Omit bones under this. Default 0.

    Returns:
        Per-bone angular delta in degrees, sorted worst first.
    """
    try:
        reference, err = _load_sequence(params.get("reference_path", ""))
        if err:
            return _fail(f"reference_path: {err}")
        target, err = _load_sequence(params.get("target_path", ""))
        if err:
            return _fail(f"target_path: {err}")

        lib = _anim_library()
        reference_frame = int(params.get("reference_frame", 0))
        target_frame = int(params.get("target_frame", 0))
        threshold = float(params.get("threshold_degrees", 0.0))

        reference_tracks = _track_names(lib, reference)
        target_tracks = _track_names(lib, target)
        shared = [b for b in reference_tracks if b in target_tracks]

        requested = params.get("bones") or shared
        bones = [b for b in requested if b in shared]
        missing = [b for b in requested if b not in shared]
        if not bones:
            return _fail(
                "No requested bone has a track in both sequences. Shared tracks: "
                + ", ".join(shared[:40])
            )

        reference_meta = _sequence_meta(lib, reference)
        target_meta = _sequence_meta(lib, target)
        if reference_frame < 0 or reference_frame >= max(reference_meta["num_frames"], 1):
            return _fail(
                f"reference_frame {reference_frame} out of range; reference has "
                f"{reference_meta['num_frames']} frames"
            )
        if target_frame < 0 or target_frame >= max(target_meta["num_frames"], 1):
            return _fail(
                f"target_frame {target_frame} out of range; target has "
                f"{target_meta['num_frames']} frames"
            )

        reference_rot = _bone_rotations_at_frame(lib, reference, bones, reference_frame)
        target_rot = _bone_rotations_at_frame(lib, target, bones, target_frame)

        deltas: List[Dict[str, Any]] = []
        for name in bones:
            degrees = am.quat_angle_degrees(reference_rot[name], target_rot[name])
            if degrees >= threshold:
                deltas.append({"bone": name, "delta_degrees": round(degrees, 4)})
        deltas.sort(key=lambda entry: entry["delta_degrees"], reverse=True)

        return _ok({
            "reference": params.get("reference_path"),
            "reference_frame": reference_frame,
            "target": params.get("target_path"),
            "target_frame": target_frame,
            "threshold_degrees": threshold,
            "bones_compared": len(bones),
            "bones_over_threshold": len(deltas),
            "max_delta_degrees": deltas[0]["delta_degrees"] if deltas else 0.0,
            "deltas": deltas,
            "skipped_no_track": missing,
            "reference_is_additive": reference_meta["is_additive"],
            "target_is_additive": target_meta["is_additive"],
        })
    except Exception as exc:
        return _fail(str(exc))


def _compression_info(lib: Any, seq: Any) -> Dict[str, Any]:
    """Codec name for the sequence's bone compression settings.

    Best effort. The per-codec error threshold sits on the codec object inside
    the settings asset and is not reliably reachable from Python in 4.27, so it
    is reported as None rather than guessed.
    """
    try:
        settings = _call(lib, "get_bone_compression_settings", seq)
        if settings is None:
            return {"codec": None, "error_threshold": None}
        return {
            "codec": settings.get_name(),
            "codec_class": type(settings).__name__,
            "error_threshold": None,
        }
    except Exception as exc:
        return {"codec": None, "error_threshold": None, "error": str(exc)}


def _analyze_one(lib: Any, seq: Any, path: str, root_bone: str, pelvis_bone: str) -> Dict[str, Any]:
    """Root motion statistics for a single sequence."""
    meta = _sequence_meta(lib, seq)
    tracks = _track_names(lib, seq)

    if root_bone not in tracks:
        return {
            "sequence": path,
            "error": (
                f"No animated track named '{root_bone}'. Available: "
                + ", ".join(tracks[:40])
            ),
        }

    positions = [_vec_tuple(v) for v in _call(lib, "get_raw_track_position_data", seq, root_bone)]
    raw_rotations = _call(lib, "get_raw_track_rotation_data", seq, root_bone)
    rotations = [_quat_tuple(q) for q in raw_rotations]

    steps = am.position_steps(positions)
    step_distribution = am.distribution(steps)
    rough = am.roughness(positions)
    root_range = am.axis_range(positions)

    # Convention-free rotational excursion: the largest angle any key reaches
    # from the first key. Always available, and unlike yaw it cannot be fooled
    # by the Rotator's wrap at +/-180 degrees.
    excursion = 0.0
    if rotations:
        excursion = max(am.quat_angle_degrees(rotations[0], q) for q in rotations)

    # Reported because it is what an eyeball inspection of the track shows, but
    # it is naive about yaw wrapping and depends on the Rotator binding being
    # available. Prefer rotation_excursion_degrees when the two disagree.
    yaw_range: Optional[float] = None
    yaws: List[float] = []
    for quat in raw_rotations:
        euler = _euler_or_none(quat)
        if euler is None:
            yaws = []
            break
        yaws.append(euler["yaw"])
    if yaws:
        yaw_range = round(max(yaws) - min(yaws), 4)

    root_block: Dict[str, Any] = {
        "key_count": len(positions),
        "range_cm": root_range,
        "yaw_range_degrees": yaw_range,
        "rotation_excursion_degrees": round(excursion, 4),
        "step_cm_per_frame": step_distribution,
        "step_degrees_per_frame": am.distribution(am.rotation_steps(rotations)),
        "roughness": round(rough, 4),
        "classification": am.classify(step_distribution["mean"], rough),
    }

    pelvis_block: Dict[str, Any] = {"local_range_cm": None}
    flags: List[str] = []
    if pelvis_bone in tracks:
        pelvis_positions = [
            _vec_tuple(v)
            for v in _call(lib, "get_raw_track_position_data", seq, pelvis_bone)
        ]
        pelvis_range = am.axis_range(pelvis_positions)
        pelvis_block = {"local_range_cm": pelvis_range, "key_count": len(pelvis_positions)}

        root_max = max(root_range.values())
        pelvis_max = max(pelvis_range.values())
        if (root_max > INVERTED_ROOT_MIN_RANGE_CM
                and pelvis_max < INVERTED_ROOT_PELVIS_RATIO * root_max):
            flags.append("inverted_root_authoring")
    else:
        flags.append("pelvis_track_missing")

    if meta["is_additive"]:
        flags.append("additive_sequence")

    result: Dict[str, Any] = {
        "sequence": path,
        "root": root_block,
        "pelvis": pelvis_block,
        "flags": flags,
        "compression": _compression_info(lib, seq),
    }
    result.update(meta)
    return result


def handle_anim_root_motion_analyze(params: Dict[str, Any]) -> Dict[str, Any]:
    """Measure root track drift, step distribution, and roughness.

    Separates smooth authored sway (reads as sliding) from per-frame step noise
    (reads as jiggle), which a min-to-max range alone cannot do. Reads
    import-time raw data, so runtime causes such as foot IK or decompression
    artifacts cannot appear here by construction.

    Args:
        params:
            - sequence_path (str): Single AnimSequence. Either this or
              folder_path is required.
            - folder_path (str): Content folder; analyzes every AnimSequence
              under it, sorted by roughness descending.
            - root_bone (str): Default "root".
            - pelvis_bone (str): Default "pelvis".
            - recursive (bool): Folder mode only. Default True.

    Returns:
        One analysis block, or a sorted list of them in folder mode.
    """
    try:
        import unreal

        sequence_path = params.get("sequence_path", "")
        folder_path = params.get("folder_path", "")
        if bool(sequence_path) == bool(folder_path):
            return _fail("Provide exactly one of 'sequence_path' or 'folder_path'")

        root_bone = params.get("root_bone", "root")
        pelvis_bone = params.get("pelvis_bone", "pelvis")
        lib = _anim_library()

        if sequence_path:
            seq, err = _load_sequence(sequence_path)
            if err:
                return _fail(err)
            return _ok(_analyze_one(lib, seq, sequence_path, root_bone, pelvis_bone))

        recursive = bool(params.get("recursive", True))
        asset_paths = unreal.EditorAssetLibrary.list_assets(
            folder_path, recursive=recursive, include_folder=False
        )

        analyzed: List[Dict[str, Any]] = []
        errors: List[Dict[str, str]] = []
        for asset_path in asset_paths:
            clean = asset_path.split(".")[0]
            # Filter on registry metadata before loading. A Blends folder holds
            # far more than AnimSequences, and load_asset on every entry would
            # pull textures and materials into memory for nothing.
            if not _is_anim_sequence_by_registry(clean):
                continue
            asset = unreal.load_asset(clean)
            if not isinstance(asset, unreal.AnimSequence):
                continue
            try:
                entry = _analyze_one(lib, asset, clean, root_bone, pelvis_bone)
                if "error" in entry:
                    errors.append({"sequence": clean, "error": entry["error"]})
                else:
                    analyzed.append(entry)
            except Exception as exc:
                errors.append({"sequence": clean, "error": str(exc)})

        analyzed.sort(key=lambda e: e["root"]["roughness"], reverse=True)
        return _ok({
            "folder": folder_path,
            "recursive": recursive,
            "count": len(analyzed),
            "sequences": analyzed,
            "errors": errors,
        })
    except Exception as exc:
        return _fail(str(exc))
