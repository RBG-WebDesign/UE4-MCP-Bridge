"""Semantic adapters for the UE4.27 Cloth Optimizer editor module."""

import datetime
import json
import os
from typing import Any, Dict

from mcp_bridge.utils.transactions import transactional


def handle_cloth_inspect_asset(params: Dict[str, Any]) -> Dict[str, Any]:
    """Inspect one skeletal mesh, its NvCloth data, masks, and Physics Asset.

    Args:
        params: Requires ``skeletal_mesh`` as a /Game asset path.

    Returns:
        A versioned cloth snapshot produced by the C++ editor module.
    """
    try:
        import unreal

        skeletal_mesh = str(params.get("skeletal_mesh", "")).strip()
        if not skeletal_mesh:
            return {
                "success": False,
                "data": {},
                "error": "Missing 'skeletal_mesh' parameter",
            }

        library = getattr(unreal, "ClothOptimizerLibrary", None)
        if library is None:
            return {
                "success": False,
                "data": {},
                "error": "MCPBridgeClothOptimizer is not loaded. Build the editor target and restart Unreal.",
            }

        snapshot_text = library.inspect_cloth_asset(skeletal_mesh)
        snapshot = json.loads(str(snapshot_text))
        if not bool(snapshot.get("success", False)):
            return {
                "success": False,
                "data": snapshot,
                "error": str(snapshot.get("error", "Cloth inspection failed")),
            }

        return {"success": True, "data": snapshot}
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def _vector_dict(value: Any) -> Dict[str, float]:
    """Serialize an Unreal vector-like cloth property."""
    return {"x": float(value.x), "y": float(value.y), "z": float(value.z)}


def handle_cloth_smooth_max_distance(params: Dict[str, Any]) -> Dict[str, Any]:
    """Smooth one clothing asset's Max Distance transition and save it.

    Args:
        params: Requires exact mesh and clothing asset identifiers, bounded
            iteration and strength values, and explicit confirmation.

    Returns:
        Candidate summary and backup path from the C++ cloth service.
    """
    try:
        import unreal

        skeletal_mesh = str(params.get("skeletal_mesh", "")).strip()
        clothing_asset = str(params.get("clothing_asset", "")).strip()
        iterations = max(1, min(12, int(params.get("iterations", 6))))
        strength = max(0.0, min(1.0, float(params.get("strength", 0.6))))
        if not bool(params.get("confirm", False)):
            return {
                "success": False,
                "data": {},
                "error": "Set 'confirm' to true immediately before applying mask smoothing",
            }
        library = getattr(unreal, "ClothOptimizerLibrary", None)
        if library is None or not hasattr(library, "smooth_max_distance_transition"):
            return {
                "success": False,
                "data": {},
                "error": "The semantic smoothing function requires a closed-editor rebuild of MCPBridgeClothOptimizer",
            }
        result = library.smooth_max_distance_transition(
            skeletal_mesh,
            clothing_asset,
            iterations,
            strength,
        )
        if not isinstance(result, tuple) or len(result) not in (3, 4):
            return {
                "success": False,
                "data": {},
                "error": f"Unexpected Cloth Optimizer smoothing result: {result}",
            }
        if len(result) == 4:
            applied, summary, backup_path, error = result
        else:
            summary, backup_path, error = result
            applied = not bool(str(error))
        if not bool(applied):
            return {
                "success": False,
                "data": {"summary": str(summary), "backup": str(backup_path)},
                "error": str(error),
            }
        return {
            "success": True,
            "data": {
                "skeletal_mesh": skeletal_mesh,
                "clothing_asset": clothing_asset,
                "iterations": iterations,
                "strength": strength,
                "summary": str(summary),
                "backup": str(backup_path),
                "saved": True,
            },
        }
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def handle_cloth_apply_lower_leg_gradient(params: Dict[str, Any]) -> Dict[str, Any]:
    """Author and save a smooth hem-to-knee Max Distance gradient."""
    try:
        import unreal

        skeletal_mesh = str(params.get("skeletal_mesh", "")).strip()
        clothing_asset = str(params.get("clothing_asset", "")).strip()
        knee_fraction = max(0.35, min(0.65, float(params.get("knee_height_fraction", 0.48))))
        if not bool(params.get("confirm", False)):
            return {
                "success": False,
                "data": {},
                "error": "Set 'confirm' to true immediately before expanding lower-leg cloth freedom",
            }
        library = getattr(unreal, "ClothOptimizerLibrary", None)
        if library is None or not hasattr(library, "apply_lower_leg_gradient"):
            return {
                "success": False,
                "data": {},
                "error": "The lower-leg gradient function requires a closed-editor rebuild of MCPBridgeClothOptimizer",
            }
        result = library.apply_lower_leg_gradient(
            skeletal_mesh,
            clothing_asset,
            knee_fraction,
        )
        if not isinstance(result, tuple) or len(result) not in (3, 4):
            return {
                "success": False,
                "data": {},
                "error": f"Unexpected Cloth Optimizer gradient result: {result}",
            }
        if len(result) == 4:
            applied, summary, backup_path, error = result
        else:
            summary, backup_path, error = result
            applied = not bool(str(error))
        if not bool(applied):
            return {
                "success": False,
                "data": {"summary": str(summary), "backup": str(backup_path)},
                "error": str(error),
            }
        return {
            "success": True,
            "data": {
                "skeletal_mesh": skeletal_mesh,
                "clothing_asset": clothing_asset,
                "knee_height_fraction": knee_fraction,
                "summary": str(summary),
                "backup": str(backup_path),
                "saved": True,
            },
        }
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


@transactional("Apply Cloth Fabric Profile")
def handle_cloth_apply_fabric_profile(params: Dict[str, Any]) -> Dict[str, Any]:
    """Apply a bounded NvCloth fabric profile to one embedded clothing asset.

    This command intentionally leaves cloth paint masks and the Physics Asset
    unchanged. The close-fitting denim profile uses reduced particle collision
    thickness to avoid hard capsule-shaped bulges on fitted trousers.

    Args:
        params: Requires ``skeletal_mesh``, ``clothing_asset``, ``profile``,
            and explicit ``confirm`` set to true.

    Returns:
        The applied values, backup path, and package save status.
    """
    try:
        import unreal

        skeletal_mesh_path = str(params.get("skeletal_mesh", "")).strip()
        clothing_asset_name = str(params.get("clothing_asset", "")).strip()
        profile = str(params.get("profile", "")).strip().lower()
        confirmed = bool(params.get("confirm", False))
        if not skeletal_mesh_path or not clothing_asset_name:
            return {
                "success": False,
                "data": {},
                "error": "Both 'skeletal_mesh' and 'clothing_asset' are required",
            }
        if not confirmed:
            return {
                "success": False,
                "data": {},
                "error": "Set 'confirm' to true immediately before applying cloth settings",
            }

        library = getattr(unreal, "ClothOptimizerLibrary", None)
        if library is None or not hasattr(library, "apply_fabric_profile"):
            return {
                "success": False,
                "data": {},
                "error": "The semantic cloth apply function requires a closed-editor rebuild of MCPBridgeClothOptimizer",
            }
        apply_result = library.apply_fabric_profile(
            skeletal_mesh_path,
            clothing_asset_name,
            profile,
        )
        if not isinstance(apply_result, tuple) or len(apply_result) not in (2, 3):
            return {
                "success": False,
                "data": {},
                "error": f"Unexpected Cloth Optimizer apply result: {apply_result}",
            }
        if len(apply_result) == 3:
            applied, backup_path, apply_error = apply_result
        else:
            backup_path, apply_error = apply_result
            applied = not bool(str(apply_error))
        if not bool(applied):
            return {
                "success": False,
                "data": {"backup": str(backup_path)},
                "error": str(apply_error),
            }
        return {
            "success": True,
            "data": {
                "skeletal_mesh": skeletal_mesh_path,
                "clothing_asset": clothing_asset_name,
                "profile": profile,
                "mask_changed": False,
                "physics_asset_changed": False,
                "backup": str(backup_path),
                "saved": True,
            },
        }

        profiles: Dict[str, Dict[str, float]] = {
            "heavy_denim_close_fit": {
                "solver_frequency": 120.0,
                "stiffness_frequency": 100.0,
                "damping": 0.50,
                "linear_drag": 0.40,
                "angular_drag": 0.40,
                "friction": 0.40,
                "tether_stiffness": 1.0,
                "tether_limit": 0.90,
                "collision_thickness": 0.50,
            },
            "heavy_denim": {
                "solver_frequency": 120.0,
                "stiffness_frequency": 100.0,
                "damping": 0.50,
                "linear_drag": 0.40,
                "angular_drag": 0.40,
                "friction": 0.40,
                "tether_stiffness": 1.0,
                "tether_limit": 0.90,
                "collision_thickness": 2.0,
            },
        }
        values = profiles.get(profile)
        if values is None:
            return {
                "success": False,
                "data": {"profiles": sorted(profiles.keys())},
                "error": f"Unsupported fabric profile: {profile}",
            }

        mesh = unreal.EditorAssetLibrary.load_asset(skeletal_mesh_path)
        if mesh is None or not isinstance(mesh, unreal.SkeletalMesh):
            return {
                "success": False,
                "data": {},
                "error": f"Skeletal mesh not found: {skeletal_mesh_path}",
            }

        selected_asset = None
        for asset in mesh.get_editor_property("mesh_clothing_assets"):
            if asset.get_name() == clothing_asset_name:
                selected_asset = asset
                break
        if selected_asset is None:
            return {
                "success": False,
                "data": {},
                "error": f"Embedded clothing asset not found: {clothing_asset_name}",
            }

        config_map = selected_asset.get_editor_property("cloth_configs")
        config = config_map.get("ClothConfigNv")
        if config is None:
            return {
                "success": False,
                "data": {},
                "error": "The selected clothing asset has no ClothConfigNv",
            }

        previous = {
            "solver_frequency": float(config.get_editor_property("solver_frequency")),
            "stiffness_frequency": float(config.get_editor_property("stiffness_frequency")),
            "damping": _vector_dict(config.get_editor_property("damping")),
            "linear_drag": _vector_dict(config.get_editor_property("linear_drag")),
            "angular_drag": _vector_dict(config.get_editor_property("angular_drag")),
            "friction": float(config.get_editor_property("friction")),
            "tether_stiffness": float(config.get_editor_property("tether_stiffness")),
            "tether_limit": float(config.get_editor_property("tether_limit")),
            "collision_thickness": float(config.get_editor_property("collision_thickness")),
            "self_collision_radius": float(config.get_editor_property("self_collision_radius")),
            "self_collision_stiffness": float(config.get_editor_property("self_collision_stiffness")),
        }

        backup_dir = os.path.join(
            unreal.SystemLibrary.get_project_directory(),
            "Saved",
            "ClothMCP",
            "Backups",
        )
        os.makedirs(backup_dir, exist_ok=True)
        timestamp = datetime.datetime.utcnow().strftime("%Y%m%dT%H%M%SZ")
        backup_path = os.path.join(
            backup_dir,
            f"{mesh.get_name()}_{clothing_asset_name}_config_{timestamp}.json",
        )
        with open(backup_path, "w", encoding="utf-8") as backup_file:
            json.dump(
                {
                    "schemaVersion": 1,
                    "skeletalMesh": skeletal_mesh_path,
                    "clothingAsset": clothing_asset_name,
                    "profileBeforeApply": previous,
                },
                backup_file,
                indent=2,
                sort_keys=True,
            )

        mesh.modify()
        selected_asset.modify()
        config.modify()
        config.set_editor_property("solver_frequency", values["solver_frequency"])
        config.set_editor_property("stiffness_frequency", values["stiffness_frequency"])
        config.set_editor_property("damping", unreal.Vector(values["damping"]))
        config.set_editor_property("linear_drag", unreal.Vector(values["linear_drag"]))
        config.set_editor_property("angular_drag", unreal.Vector(values["angular_drag"]))
        config.set_editor_property("friction", values["friction"])
        config.set_editor_property("tether_stiffness", values["tether_stiffness"])
        config.set_editor_property("tether_limit", values["tether_limit"])
        config.set_editor_property("collision_thickness", values["collision_thickness"])
        saved = bool(
            unreal.EditorAssetLibrary.save_asset(
                skeletal_mesh_path,
                only_if_is_dirty=False,
            )
        )
        if not saved:
            return {
                "success": False,
                "data": {"backup": backup_path},
                "error": "The cloth profile changed in memory but the skeletal mesh package did not save",
            }

        return {
            "success": True,
            "data": {
                "skeletal_mesh": skeletal_mesh_path,
                "clothing_asset": clothing_asset_name,
                "profile": profile,
                "previous": previous,
                "applied": values,
                "mask_changed": False,
                "physics_asset_changed": False,
                "backup": backup_path,
                "saved": saved,
            },
        }
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}
