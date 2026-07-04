# level_generator.py -- Create maps and place actors from LevelSpec.
from __future__ import annotations
from typing import Any, Dict, List, Tuple

from mcp_bridge.generation.spec_schema import LevelSpec


# Map type name strings to spawn-able actor classes
_ACTOR_TYPE_MAP = {
    "DirectionalLight": "DirectionalLight",
    "PointLight": "PointLight",
    "SpotLight": "SpotLight",
    "PlayerStart": "PlayerStart",
    "PostProcessVolume": "PostProcessVolume",
    "TriggerBox": "TriggerBox",
    "StaticMeshActor": "StaticMeshActor",
    "Actor": "Actor",
}


def generate_level(spec: LevelSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a map and place actors from a LevelSpec.

    Creates the level if it does not exist, loads it, places actors, saves.
    """
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        # Create map if it does not exist
        if not unreal.EditorAssetLibrary.does_asset_exist(full_path):
            unreal.EditorLevelLibrary.new_level(full_path)

        # Load the level
        unreal.EditorLevelLibrary.load_level(full_path)

        placed: List[str] = []
        failed: List[str] = []

        for actor_def in spec.actors:
            try:
                actor_type = actor_def.get("type", "Actor")
                actor_name = actor_def.get("name", "Actor")
                loc_raw = actor_def.get("location", {"x": 0, "y": 0, "z": 0})
                location = unreal.Vector(
                    float(loc_raw.get("x", 0)),
                    float(loc_raw.get("y", 0)),
                    float(loc_raw.get("z", 0)),
                )

                # Resolve actor class
                class_attr = _ACTOR_TYPE_MAP.get(actor_type, "Actor")
                actor_class = getattr(unreal, class_attr, unreal.Actor)

                actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
                    actor_class, location, unreal.Rotator(0, 0, 0)
                )
                if actor:
                    try:
                        actor.set_actor_label(actor_name)
                    except Exception:
                        pass
                    placed.append(actor_name)
                else:
                    failed.append(actor_name)

            except Exception as e:
                failed.append(f"{actor_def.get('name', '?')}: {e}")

        # Save level
        try:
            unreal.EditorLevelLibrary.save_current_level()
        except Exception:
            pass

        return True, "", {"path": full_path, "placed": placed, "failed": failed}

    except Exception as e:
        return False, str(e), {}


def generate_all_levels(specs: List[LevelSpec]) -> Dict[str, Any]:
    results = []
    errors: List[str] = []
    for spec in specs:
        ok, err, data = generate_level(spec)
        entry: Dict[str, Any] = {"name": spec.name, "success": ok, "data": data}
        if err:
            entry["error"] = err
            errors.append(f"{spec.name}: {err}")
        results.append(entry)
    return {
        "results": results,
        "errors": errors,
        "total": len(specs),
        "succeeded": sum(1 for r in results if r["success"]),
    }
