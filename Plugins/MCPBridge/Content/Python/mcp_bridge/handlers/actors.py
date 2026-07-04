"""Actor command handlers.

Handles spawning, modifying, deleting, duplicating, and organizing actors
in the current level.
"""

import re
from typing import Any, Dict, List, Optional
from mcp_bridge.utils.serialization import actor_to_dict, vector_from_dict, rotator_from_dict
from mcp_bridge.utils.transactions import transactional
from mcp_bridge.utils.validation import validate_actor_transform


# Characters that UE4 rejects in actor labels
_INVALID_LABEL_CHARS = re.compile(r'[/\\:*?"<>|]')


def _sanitize_actor_label(name: str) -> str:
    """Strip characters that UE4 rejects in actor labels."""
    return _INVALID_LABEL_CHARS.sub("_", name).strip()


def _validate_scale(scale: Dict[str, float]) -> Optional[str]:
    """Return an error string if scale has any zero component, else None."""
    for axis in ("x", "y", "z"):
        val = float(scale.get(axis, 1))
        if val == 0.0:
            return f"Scale {axis} cannot be zero (would collapse the actor)"
    return None


@transactional("Spawn Actor")
def handle_actor_spawn(params: Dict[str, Any]) -> Dict[str, Any]:
    """Spawn an actor from an asset path.

    Args:
        params:
            - asset_path (str): Asset to spawn (e.g., /Game/Meshes/SM_Cube)
            - location (dict): {x, y, z} world location
            - rotation (dict): {pitch, yaw, roll} rotation (optional)
            - scale (dict): {x, y, z} scale (optional, default 1,1,1)
            - name (str): Desired actor label (optional)
            - folder (str): World Outliner folder (optional)
            - validate (bool): Validate resulting transform (default True)
    """
    try:
        import unreal

        asset_path = params.get("asset_path", "")
        if not asset_path:
            return {"success": False, "data": {}, "error": "Missing 'asset_path'"}

        if not asset_path.startswith("/"):
            return {"success": False, "data": {}, "error": f"Invalid asset_path: must start with '/' (got '{asset_path}')"}

        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if asset is None:
            return {"success": False, "data": {}, "error": f"Asset not found: {asset_path}"}

        # Validate scale before spawning
        scale_dict = params.get("scale")
        if scale_dict is not None:
            scale_err = _validate_scale(scale_dict)
            if scale_err:
                return {"success": False, "data": {}, "error": scale_err}

        loc_dict = params.get("location", {"x": 0, "y": 0, "z": 0})
        rot_dict = params.get("rotation", {"pitch": 0, "yaw": 0, "roll": 0})
        loc = vector_from_dict(loc_dict)
        rot = rotator_from_dict(rot_dict)

        actor = unreal.EditorLevelLibrary.spawn_actor_from_object(asset, loc, rot)
        if actor is None:
            return {"success": False, "data": {}, "error": "Failed to spawn actor"}

        # Set scale if provided
        if scale_dict is not None:
            actor.set_actor_scale3d(vector_from_dict(scale_dict))

        # Set label if provided (sanitize special characters)
        name = params.get("name")
        if name:
            actor.set_actor_label(_sanitize_actor_label(str(name)))

        # Set folder if provided
        folder = params.get("folder")
        if folder:
            actor.set_folder_path(str(folder))

        result_data = actor_to_dict(actor)

        # Post-operation validation
        if params.get("validate", True):
            validation = validate_actor_transform(
                actor,
                expected_location=loc_dict,
                expected_rotation=rot_dict,
                expected_scale=scale_dict,
            )
            result_data["validation"] = validation

        return {"success": True, "data": result_data}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Duplicate Actor")
def handle_actor_duplicate(params: Dict[str, Any]) -> Dict[str, Any]:
    """Duplicate an existing actor.

    Args:
        params:
            - actor_name (str): Label of the actor to duplicate
            - offset (dict): {x, y, z} offset from original position (optional)
            - new_name (str): Label for the duplicate (optional)
            - validate (bool): Validate resulting transform (default True)
    """
    try:
        import unreal

        actor_name = params.get("actor_name", "")
        if not actor_name:
            return {"success": False, "data": {}, "error": "Missing 'actor_name'"}

        source = _find_actor_by_name(actor_name)
        if source is None:
            return {"success": False, "data": {}, "error": f"Actor not found: {actor_name}"}

        # Select and duplicate
        unreal.EditorLevelLibrary.set_selected_level_actors([source])

        # Get source transform for offset calculation
        source_loc = source.get_actor_location()
        offset_dict = params.get("offset")

        # Duplicate via editor command (handles all component copying)
        unreal.EditorLevelLibrary.editor_duplicate_actors(True)

        # Get the new actor (should be selected after duplicate)
        selected = unreal.EditorLevelLibrary.get_selected_level_actors()
        if not selected:
            return {"success": False, "data": {}, "error": "Duplicate failed, no actor selected"}

        new_actor = selected[0]

        # Apply offset only when explicitly provided
        expected_location = None
        if offset_dict is not None:
            offset_vec = vector_from_dict(offset_dict)
            new_loc = unreal.Vector(
                source_loc.x + offset_vec.x,
                source_loc.y + offset_vec.y,
                source_loc.z + offset_vec.z,
            )
            new_actor.set_actor_location(new_loc, False, False)
            expected_location = {
                "x": source_loc.x + offset_vec.x,
                "y": source_loc.y + offset_vec.y,
                "z": source_loc.z + offset_vec.z,
            }

        # Set name if provided (sanitize)
        new_name = params.get("new_name")
        if new_name:
            new_actor.set_actor_label(_sanitize_actor_label(str(new_name)))

        result_data = actor_to_dict(new_actor)

        # Post-operation validation
        if params.get("validate", True) and expected_location is not None:
            validation = validate_actor_transform(
                new_actor,
                expected_location=expected_location,
            )
            result_data["validation"] = validation

        return {"success": True, "data": result_data}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Delete Actor")
def handle_actor_delete(params: Dict[str, Any]) -> Dict[str, Any]:
    """Delete actors by name or pattern.

    Args:
        params:
            - actor_name (str): Exact name or pattern (with * wildcard)
    """
    try:
        import unreal

        actor_name = params.get("actor_name", "")
        if not actor_name:
            return {"success": False, "data": {}, "error": "Missing 'actor_name'"}

        actors = _find_actors_by_pattern(actor_name)
        if not actors:
            return {"success": False, "data": {}, "error": f"No actors found matching: {actor_name}"}

        deleted_names = [a.get_actor_label() for a in actors]
        for actor in actors:
            unreal.EditorLevelLibrary.destroy_actor(actor)

        return {
            "success": True,
            "data": {
                "deleted_count": len(deleted_names),
                "deleted_actors": deleted_names,
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Modify Actor")
def handle_actor_modify(params: Dict[str, Any]) -> Dict[str, Any]:
    """Modify an actor's transform or properties.

    Args:
        params:
            - actor_name (str): Label of the actor to modify
            - location (dict): {x, y, z} new location (optional)
            - rotation (dict): {pitch, yaw, roll} new rotation (optional)
            - scale (dict): {x, y, z} new scale (optional)
            - visible (bool): Set visibility (optional)
            - mesh (str): Asset path for new mesh (optional)
            - validate (bool): Validate resulting transform (default True)
    """
    try:
        import unreal

        actor_name = params.get("actor_name", "")
        if not actor_name:
            return {"success": False, "data": {}, "error": "Missing 'actor_name'"}

        actor = _find_actor_by_name(actor_name)
        if actor is None:
            return {"success": False, "data": {}, "error": f"Actor not found: {actor_name}"}

        modified = []

        location = params.get("location")
        if location is not None:
            actor.set_actor_location(vector_from_dict(location), False, False)
            modified.append("location")

        rotation = params.get("rotation")
        if rotation is not None:
            actor.set_actor_rotation(rotator_from_dict(rotation), False)
            modified.append("rotation")

        scale = params.get("scale")
        if scale is not None:
            scale_err = _validate_scale(scale)
            if scale_err:
                return {"success": False, "data": {}, "error": scale_err}
            actor.set_actor_scale3d(vector_from_dict(scale))
            modified.append("scale")

        visible = params.get("visible")
        if visible is not None:
            actor.set_is_temporarily_hidden_in_editor(not visible)
            modified.append("visibility")

        mesh_path = params.get("mesh")
        if mesh_path:
            if not mesh_path.startswith("/"):
                return {"success": False, "data": {}, "error": f"Invalid mesh path: must start with '/' (got '{mesh_path}')"}
            mesh_asset = unreal.EditorAssetLibrary.load_asset(mesh_path)
            if mesh_asset is None:
                return {"success": False, "data": {}, "error": f"Mesh asset not found: {mesh_path}"}
            if not isinstance(mesh_asset, unreal.StaticMesh):
                return {"success": False, "data": {}, "error": f"Asset is not a StaticMesh: {mesh_path}"}
            components = actor.get_components_by_class(unreal.StaticMeshComponent)
            if not components:
                return {"success": False, "data": {}, "error": f"Actor '{actor_name}' has no StaticMeshComponent"}
            components[0].set_static_mesh(mesh_asset)
            modified.append("mesh")

        if not modified:
            return {"success": False, "data": {}, "error": "No properties to modify (provide location, rotation, scale, visible, or mesh)"}

        result_data = {
            "modified_properties": modified,
            "actor": actor_to_dict(actor),
        }

        # Post-operation validation
        if params.get("validate", True):
            validation = validate_actor_transform(
                actor,
                expected_location=location,
                expected_rotation=rotation,
                expected_scale=scale,
            )
            result_data["validation"] = validation

        return {"success": True, "data": result_data}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Organize Actors")
def handle_actor_organize(params: Dict[str, Any]) -> Dict[str, Any]:
    """Move actors into World Outliner folders.

    Args:
        params:
            - actors (list[str]): Actor labels to move
            - folder (str): Target folder path
    """
    try:
        import unreal

        actor_names = params.get("actors", [])
        folder = params.get("folder", "")

        if not isinstance(actor_names, list):
            return {"success": False, "data": {}, "error": "'actors' must be a list of strings"}
        if not actor_names:
            return {"success": False, "data": {}, "error": "Missing 'actors' list"}
        if not folder:
            return {"success": False, "data": {}, "error": "Missing 'folder' path"}

        moved = []
        not_found = []
        for name in actor_names:
            actor = _find_actor_by_name(str(name))
            if actor:
                actor.set_folder_path(str(folder))
                moved.append(str(name))
            else:
                not_found.append(str(name))

        return {
            "success": True,
            "data": {
                "moved": moved,
                "not_found": not_found,
                "folder": folder,
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Snap to Socket")
def handle_actor_snap_to_socket(params: Dict[str, Any]) -> Dict[str, Any]:
    """Snap one actor to another actor's named socket.

    Args:
        params:
            - actor_name (str): Actor to move
            - target_actor (str): Actor with the socket
            - socket_name (str): Name of the socket to snap to
    """
    try:
        import unreal

        actor_name = params.get("actor_name", "")
        target_name = params.get("target_actor", "")
        socket_name = params.get("socket_name", "")

        if not actor_name:
            return {"success": False, "data": {}, "error": "Missing 'actor_name'"}
        if not target_name:
            return {"success": False, "data": {}, "error": "Missing 'target_actor'"}
        if not socket_name:
            return {"success": False, "data": {}, "error": "Missing 'socket_name'"}

        actor = _find_actor_by_name(actor_name)
        target = _find_actor_by_name(target_name)

        if not actor:
            return {"success": False, "data": {}, "error": f"Actor not found: {actor_name}"}
        if not target:
            return {"success": False, "data": {}, "error": f"Target actor not found: {target_name}"}

        # Find socket on target's mesh component
        mesh_components = target.get_components_by_class(unreal.StaticMeshComponent)
        if not mesh_components:
            mesh_components = target.get_components_by_class(unreal.SkeletalMeshComponent)

        if not mesh_components:
            return {"success": False, "data": {}, "error": "Target has no mesh components"}

        # Verify socket exists before snapping
        mesh_comp = mesh_components[0]
        if not mesh_comp.does_socket_exist(unreal.Name(socket_name)):
            return {"success": False, "data": {}, "error": f"Socket '{socket_name}' not found on target mesh"}

        socket_transform = mesh_comp.get_socket_transform(
            unreal.Name(socket_name),
            unreal.RelativeTransformSpace.RTS_WORLD
        )

        actor.set_actor_location(socket_transform.translation, False, False)
        actor.set_actor_rotation(socket_transform.rotation.rotator(), False)

        return {"success": True, "data": actor_to_dict(actor)}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Batch Spawn")
def handle_batch_spawn(params: Dict[str, Any]) -> Dict[str, Any]:
    """Spawn multiple actors in one call.

    Args:
        params:
            - spawns (list[dict]): Array of spawn definitions, each with the
              same format as actor_spawn params.
    """
    try:
        spawns = params.get("spawns", [])

        if not isinstance(spawns, list):
            return {"success": False, "data": {}, "error": "'spawns' must be a list"}
        if not spawns:
            return {"success": False, "data": {}, "error": "'spawns' array is empty"}

        results = []
        for i, spawn_def in enumerate(spawns):
            if not isinstance(spawn_def, dict):
                results.append({
                    "index": i,
                    "success": False,
                    "data": {},
                    "error": f"Spawn definition at index {i} must be a dict",
                })
                continue
            result = handle_actor_spawn.__wrapped__(spawn_def)
            results.append({
                "index": i,
                "success": result["success"],
                "data": result.get("data", {}),
                "error": result.get("error"),
            })

        success_count = sum(1 for r in results if r["success"])
        return {
            "success": True,
            "data": {
                "total": len(spawns),
                "succeeded": success_count,
                "failed": len(spawns) - success_count,
                "results": results,
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_placement_validate(params: Dict[str, Any]) -> Dict[str, Any]:
    """Check actors for placement issues (gaps, overlaps, alignment).

    Args:
        params:
            - actors (list[str]): Actor labels to validate
            - check_gaps (bool): Check for gaps (default True)
            - check_overlaps (bool): Check for overlaps (default True)
            - gap_threshold (float): Max acceptable gap in units (default 1.0)
            - overlap_threshold (float): Min distance for overlap (default 1.0)
    """
    try:
        import unreal

        actor_names = params.get("actors", [])
        if not isinstance(actor_names, list):
            return {"success": False, "data": {}, "error": "'actors' must be a list"}
        if not actor_names:
            return {"success": False, "data": {}, "error": "Missing 'actors' list"}

        check_gaps = params.get("check_gaps", True)
        check_overlaps = params.get("check_overlaps", True)
        gap_threshold = float(params.get("gap_threshold", 1.0))
        overlap_threshold = float(params.get("overlap_threshold", 1.0))

        actors = []
        not_found = []
        for name in actor_names:
            actor = _find_actor_by_name(str(name))
            if actor:
                actors.append(actor)
            else:
                not_found.append(str(name))

        if len(actors) < 2:
            return {
                "success": True,
                "data": {
                    "actors_checked": len(actors),
                    "not_found": not_found,
                    "issues": [],
                    "issue_count": 0,
                    "message": "Need at least 2 actors for pairwise checks",
                }
            }

        issues = []

        # Check for overlaps and gaps between pairs
        for i in range(len(actors)):
            for j in range(i + 1, len(actors)):
                a = actors[i]
                b = actors[j]

                a_loc = a.get_actor_location()
                b_loc = b.get_actor_location()

                distance = (unreal.Vector(
                    a_loc.x - b_loc.x,
                    a_loc.y - b_loc.y,
                    a_loc.z - b_loc.z
                )).length()

                a_label = a.get_actor_label()
                b_label = b.get_actor_label()

                if check_overlaps and distance < overlap_threshold:
                    issues.append({
                        "type": "overlap",
                        "actors": [a_label, b_label],
                        "distance": round(distance, 3),
                    })

                # Use bounds for gap detection when available
                if check_gaps:
                    try:
                        a_origin, a_extent = a.get_actor_bounds(False)
                        b_origin, b_extent = b.get_actor_bounds(False)

                        # Distance between bounding box edges
                        edge_gap = distance - a_extent.length() - b_extent.length()
                        if edge_gap > gap_threshold:
                            issues.append({
                                "type": "gap",
                                "actors": [a_label, b_label],
                                "gap": round(edge_gap, 3),
                                "threshold": gap_threshold,
                            })
                    except Exception:
                        pass

        return {
            "success": True,
            "data": {
                "actors_checked": len(actors),
                "not_found": not_found,
                "issues": issues,
                "issue_count": len(issues),
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


# Helper functions

def _find_actor_by_name(name: str) -> Optional[Any]:
    """Find an actor in the current level by its label."""
    try:
        import unreal
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        for actor in actors:
            if actor.get_actor_label() == name:
                return actor
        return None
    except Exception:
        return None


def _find_actors_by_pattern(pattern: str) -> List[Any]:
    """Find actors matching a name pattern (supports * and ? wildcards via fnmatch)."""
    try:
        import unreal
        import fnmatch
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        if "*" in pattern or "?" in pattern or "[" in pattern:
            return [a for a in actors if fnmatch.fnmatch(a.get_actor_label(), pattern)]
        else:
            return [a for a in actors if a.get_actor_label() == pattern]
    except Exception:
        return []
