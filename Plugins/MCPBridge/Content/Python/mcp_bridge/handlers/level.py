"""Level operation handlers.

Handles listing actors, saving levels, and querying the World Outliner
folder tree.
"""

import fnmatch
import time
from typing import Any, Dict, List


def handle_level_actors(params: Dict[str, Any]) -> Dict[str, Any]:
    """List actors in the current level with optional filters.

    Args:
        params:
            - class_filter (str): Filter by class name (supports * and ? wildcards)
            - folder_filter (str): Filter by World Outliner folder prefix
            - name_filter (str): Filter by actor label (supports * and ? wildcards)
            - include_transforms (bool): Include location/rotation/scale (default True)
            - include_components (bool): Include component list (default False)
            - limit (int): Max actors to return (default 500)
    """
    try:
        import unreal
        from mcp_bridge.utils.serialization import vector_to_dict, rotator_to_dict

        class_filter = params.get("class_filter", "")
        folder_filter = params.get("folder_filter", "")
        name_filter = params.get("name_filter", "")
        include_transforms = params.get("include_transforms", True)
        include_components = params.get("include_components", False)
        limit = params.get("limit", 500)

        # Validate limit
        if not isinstance(limit, (int, float)) or int(limit) < 1:
            limit = 500
        else:
            limit = int(limit)

        start_time = time.time()
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        results: List[Dict[str, Any]] = []

        for actor in actors:
            if len(results) >= limit:
                break

            actor_class = actor.get_class().get_name()
            label = actor.get_actor_label()
            folder = str(actor.get_folder_path())

            # Apply class filter (wildcard or exact, case-insensitive)
            if class_filter:
                if "*" in class_filter or "?" in class_filter:
                    if not fnmatch.fnmatch(actor_class.lower(), class_filter.lower()):
                        continue
                else:
                    if actor_class.lower() != class_filter.lower():
                        continue

            # Apply folder filter (prefix match)
            if folder_filter:
                if not folder.startswith(folder_filter):
                    continue

            # Apply name filter (wildcard or exact)
            if name_filter:
                if "*" in name_filter or "?" in name_filter:
                    if not fnmatch.fnmatch(label, name_filter):
                        continue
                else:
                    if label != name_filter:
                        continue

            entry: Dict[str, Any] = {
                "name": label,
                "class": actor_class,
                "folder": folder,
            }

            if include_transforms:
                loc = actor.get_actor_location()
                rot = actor.get_actor_rotation()
                scale = actor.get_actor_scale3d()
                entry["location"] = vector_to_dict(loc)
                entry["rotation"] = rotator_to_dict(rot)
                entry["scale"] = vector_to_dict(scale)

                # Include mesh path if it's a static mesh actor
                try:
                    mesh_comps = actor.get_components_by_class(unreal.StaticMeshComponent)
                    if mesh_comps:
                        mesh = mesh_comps[0].get_editor_property("static_mesh")
                        if mesh:
                            entry["mesh"] = mesh.get_path_name()
                except Exception:
                    pass

            if include_components:
                try:
                    all_comps = actor.get_components_by_class(unreal.ActorComponent)
                    entry["components"] = [
                        {"name": c.get_name(), "class": c.get_class().get_name()}
                        for c in all_comps
                    ]
                except Exception:
                    entry["components"] = []

            results.append(entry)

        # Sort by folder path then name
        results.sort(key=lambda a: (a.get("folder", ""), a.get("name", "")))

        elapsed = time.time() - start_time
        response_data: Dict[str, Any] = {
            "count": len(results),
            "total_in_level": len(actors),
            "actors": results,
        }

        if elapsed > 2.0:
            response_data["warning"] = "Large level, consider using filters"

        if len(results) >= limit:
            response_data["truncated"] = True
            response_data["limit"] = limit

        return {"success": True, "data": response_data}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_level_save(params: Dict[str, Any]) -> Dict[str, Any]:
    """Save the current level and optionally all dirty assets.

    Args:
        params:
            - save_all (bool): Also save all modified assets (default False)
    """
    try:
        import unreal

        save_all = params.get("save_all", False)

        # Save current level
        world = unreal.EditorLevelLibrary.get_editor_world()
        if not world:
            return {"success": False, "data": {}, "error": "No level is currently open"}

        level_name = world.get_name()

        try:
            unreal.EditorLevelLibrary.save_current_level()
        except Exception as save_err:
            return {
                "success": False,
                "data": {},
                "error": f"Failed to save level '{level_name}': {save_err}. Check if the file is read-only or locked by source control.",
            }

        assets_saved_count = 0
        if save_all:
            try:
                # Save all dirty packages
                dirty_assets = unreal.EditorAssetLibrary.list_assets(
                    "/Game/", recursive=True
                )
                saved = unreal.EditorAssetLibrary.save_loaded_assets(
                    dirty_assets, only_if_is_dirty=True
                )
                if saved:
                    assets_saved_count = len(dirty_assets)
            except Exception as asset_err:
                # Level saved OK but asset save failed -- partial success
                return {
                    "success": True,
                    "data": {
                        "level_saved": level_name,
                        "assets_saved_count": 0,
                        "warning": f"Level saved but asset save failed: {asset_err}",
                    },
                }

        return {
            "success": True,
            "data": {
                "level_saved": level_name,
                "assets_saved_count": assets_saved_count if save_all else 0,
                "save_all": save_all,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_level_outliner(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return the World Outliner folder tree structure.

    UE4 has no "get all folders" API. This builds the tree by iterating
    all actors and collecting unique get_folder_path() values.

    Args:
        params:
            - root_folder (str): Optional subtree root to start from
    """
    try:
        import unreal

        root_folder = params.get("root_folder", "")
        actors = unreal.EditorLevelLibrary.get_all_level_actors()

        # Collect folder paths and their actor counts
        folder_actors: Dict[str, List[str]] = {}
        unfoldered: List[str] = []

        for actor in actors:
            folder = str(actor.get_folder_path())
            label = actor.get_actor_label()

            if not folder or folder == "None":
                unfoldered.append(label)
                continue

            # If root_folder is specified, skip actors outside that subtree
            if root_folder and not folder.startswith(root_folder):
                continue

            if folder not in folder_actors:
                folder_actors[folder] = []
            folder_actors[folder].append(label)

        # Build a tree structure from flat folder paths
        tree: Dict[str, Any] = {}
        for folder_path in sorted(folder_actors.keys()):
            parts = folder_path.split("/")
            node = tree
            for part in parts:
                if part not in node:
                    node[part] = {"_children": {}, "_actor_count": 0}
                node = node[part]["_children"]
            # Walk back to set the actor count on the leaf
            node_ref = tree
            for part in parts:
                node_ref = node_ref[part]
            node_ref["_actor_count"] = len(folder_actors[folder_path])

        # Flatten tree into a list of folder entries
        folder_entries: List[Dict[str, Any]] = []
        for folder_path in sorted(folder_actors.keys()):
            children = [
                fp for fp in folder_actors.keys()
                if fp.startswith(folder_path + "/") and fp.count("/") == folder_path.count("/") + 1
            ]
            folder_entries.append({
                "path": folder_path,
                "actor_count": len(folder_actors[folder_path]),
                "children": children,
            })

        response_data: Dict[str, Any] = {
            "folders": folder_entries,
            "folder_count": len(folder_entries),
            "total_actors": len(actors),
            "unfoldered_actor_count": len(unfoldered),
        }

        if root_folder:
            response_data["root_folder"] = root_folder

        return {"success": True, "data": response_data}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}
