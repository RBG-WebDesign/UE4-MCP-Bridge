# sequence_generator.py -- Generate LevelSequence assets.
from __future__ import annotations
from typing import Any, Dict, List, Tuple

from mcp_bridge.generation.spec_schema import LevelSequenceSpec


def generate_level_sequence(spec: LevelSequenceSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a LevelSequence asset."""
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        factory_cls = getattr(unreal, "LevelSequenceFactoryNew", None)
        if factory_cls is None:
            return False, "LevelSequenceFactoryNew not available", {}

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        seq = asset_tools.create_asset(spec.name, spec.content_path, unreal.LevelSequence, factory_cls())
        if seq is None:
            return False, f"Failed to create LevelSequence: {full_path}", {}

        # Set duration via movie scene display rate
        try:
            movie_scene = seq.get_movie_scene()
            if movie_scene:
                # Set playback range end to duration in frames (assuming 30fps)
                fps = 30
                end_frame = int(spec.duration_seconds * fps)
                movie_scene.set_playback_start(0)
                movie_scene.set_playback_end(end_frame)
        except Exception:
            pass  # duration is best-effort; asset is valid without it

        unreal.EditorAssetLibrary.save_asset(full_path)
        return True, "", {
            "path": full_path, "duration_seconds": spec.duration_seconds, "skipped": False
        }

    except Exception as e:
        return False, str(e), {}


def generate_all_level_sequences(specs: List[LevelSequenceSpec]) -> Dict[str, Any]:
    results, errors = [], []
    for spec in specs:
        ok, err, data = generate_level_sequence(spec)
        entry: Dict[str, Any] = {"name": spec.name, "success": ok, "data": data}
        if err:
            entry["error"] = err
            errors.append(f"{spec.name}: {err}")
        results.append(entry)
    return {
        "results": results, "errors": errors,
        "total": len(specs), "succeeded": sum(1 for r in results if r["success"]),
    }
