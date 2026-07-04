"""Project and asset command handlers."""

import os
import json
import re
from typing import Any, Dict, List


def handle_project_info(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return current UE project info.
    
    Args:
        params: Unused.
    
    Returns:
        Project name, engine version, paths, loaded level.
    """
    try:
        import unreal
        
        # Get the current level name
        world = unreal.EditorLevelLibrary.get_editor_world()
        level_name = world.get_name() if world else "Unknown"
        
        return {
            "success": True,
            "data": {
                "project_name": unreal.SystemLibrary.get_game_name(),
                "engine_version": unreal.SystemLibrary.get_engine_version(),
                "project_dir": unreal.SystemLibrary.get_project_directory(),
                "content_dir": unreal.SystemLibrary.get_project_content_directory(),
                "current_level": level_name,
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_asset_list(params: Dict[str, Any]) -> Dict[str, Any]:
    """List assets with optional filters.
    
    Args:
        params: Optional filters:
            - path (str): Path prefix to search under (default: /Game/)
            - asset_type (str): Filter by asset type class name
            - name_pattern (str): Filter by name substring
            - recursive (bool): Search recursively (default: True)
    
    Returns:
        List of asset paths with types.
    """
    try:
        import unreal
        
        search_path = params.get("path", "/Game/")
        asset_type = params.get("asset_type", "")
        name_pattern = params.get("name_pattern", "")
        recursive = params.get("recursive", True)
        
        # Get asset registry
        asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
        
        # List assets
        asset_paths = unreal.EditorAssetLibrary.list_assets(
            search_path,
            recursive=recursive,
            include_folder=False
        )
        
        results: List[Dict[str, Any]] = []
        for asset_path in asset_paths:
            # Apply name filter
            if name_pattern and name_pattern.lower() not in asset_path.lower():
                continue
            
            # Get asset data
            asset_data = unreal.EditorAssetLibrary.find_asset_data(asset_path)
            if not asset_data:
                continue
            
            asset_class = str(asset_data.asset_class)
            
            # Apply type filter
            if asset_type and asset_type.lower() != asset_class.lower():
                continue
            
            results.append({
                "path": asset_path,
                "type": asset_class,
                "name": os.path.basename(asset_path),
            })
        
        return {
            "success": True,
            "data": {
                "count": len(results),
                "assets": results,
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_asset_info(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return detailed info for a single asset.
    
    Args:
        params: Required:
            - path (str): Asset path (e.g., /Game/Meshes/SM_Cube)
    
    Returns:
        Detailed asset information including type, size, and type-specific metadata.
    """
    try:
        import unreal
        
        asset_path = params.get("path", "")
        if not asset_path:
            return {"success": False, "data": {}, "error": "Missing 'path' parameter"}
        
        if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            return {"success": False, "data": {}, "error": f"Asset not found: {asset_path}"}
        
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if asset is None:
            return {"success": False, "data": {}, "error": f"Failed to load asset: {asset_path}"}
        
        asset_data = unreal.EditorAssetLibrary.find_asset_data(asset_path)
        
        info: Dict[str, Any] = {
            "path": asset_path,
            "type": str(asset_data.asset_class) if asset_data else "Unknown",
            "name": str(asset.get_name()),
        }
        
        # Static mesh specific info
        if isinstance(asset, unreal.StaticMesh):
            info["lod_count"] = asset.get_num_lods()
            info["material_slots"] = []
            for i in range(asset.get_num_sections(0)):
                mat = asset.get_material(i)
                info["material_slots"].append({
                    "index": i,
                    "material": str(mat.get_name()) if mat else None,
                })
            # Bounds
            bounds = asset.get_bounds()
            if bounds:
                info["bounds"] = {
                    "origin": {"x": bounds.origin.x, "y": bounds.origin.y, "z": bounds.origin.z},
                    "extent": {"x": bounds.box_extent.x, "y": bounds.box_extent.y, "z": bounds.box_extent.z},
                }
        
        # Skeletal mesh specific info
        elif isinstance(asset, unreal.SkeletalMesh):
            info["lod_count"] = asset.get_num_lods() if hasattr(asset, 'get_num_lods') else 0
            info["skeleton"] = str(asset.skeleton.get_name()) if asset.skeleton else None
        
        return {
            "success": True,
            "data": info,
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def _normalize_filter(value: Any) -> str:
    return str(value or "").strip().lower()


def _key_to_string(key: Any) -> str:
    try:
        if hasattr(key, "get_name"):
            return str(key.get_name())
    except Exception:
        pass
    try:
        key_name = key.get_editor_property("key_name")
        return str(key_name)
    except Exception:
        return str(key)


def _bool_property(obj: Any, name: str) -> bool:
    try:
        return bool(obj.get_editor_property(name))
    except Exception:
        return False


def _mapping_property(obj: Any, name: str, default: Any = "") -> Any:
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def _mapping_matches(mapping: Dict[str, Any], name_filter: str, key_filter: str, name_field: str) -> bool:
    if name_filter and str(mapping.get(name_field, "")).lower() != name_filter:
        return False
    if key_filter and str(mapping.get("key", "")).lower() != key_filter:
        return False
    return True


def _parse_input_line_fields(line: str) -> Dict[str, str]:
    fields: Dict[str, str] = {}
    for match in re.finditer(r'(\w+)=("[^"]*"|[^,\)]+)', line):
        key = match.group(1)
        value = match.group(2).strip()
        if value.startswith('"') and value.endswith('"'):
            value = value[1:-1]
        fields[key] = value
    return fields


def _parse_float(value: Any, default: float = 1.0) -> float:
    try:
        return float(value)
    except Exception:
        return default


def _read_input_mappings_from_ini(project_dir: str) -> Dict[str, Any]:
    ini_path = os.path.join(project_dir, "Config", "DefaultInput.ini")
    action_mappings: List[Dict[str, Any]] = []
    axis_mappings: List[Dict[str, Any]] = []

    if not os.path.exists(ini_path):
        return {
            "source": "DefaultInput.ini",
            "ini_path": ini_path,
            "action_mappings": action_mappings,
            "axis_mappings": axis_mappings,
        }

    with open(ini_path, encoding="utf-8-sig") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if "ActionMappings=(" in line:
                fields = _parse_input_line_fields(line)
                action_mappings.append({
                    "action_name": fields.get("ActionName", ""),
                    "key": fields.get("Key", ""),
                    "shift": fields.get("bShift", "False").lower() == "true",
                    "ctrl": fields.get("bCtrl", "False").lower() == "true",
                    "alt": fields.get("bAlt", "False").lower() == "true",
                    "cmd": fields.get("bCmd", "False").lower() == "true",
                })
            elif "AxisMappings=(" in line:
                fields = _parse_input_line_fields(line)
                axis_mappings.append({
                    "axis_name": fields.get("AxisName", ""),
                    "key": fields.get("Key", ""),
                    "scale": _parse_float(fields.get("Scale", "1.0"), 1.0),
                })

    return {
        "source": "DefaultInput.ini",
        "ini_path": ini_path,
        "action_mappings": action_mappings,
        "axis_mappings": axis_mappings,
    }


def _read_input_mappings_from_unreal() -> Dict[str, Any]:
    import unreal

    settings = unreal.InputSettings.get_input_settings()
    action_mappings: List[Dict[str, Any]] = []
    axis_mappings: List[Dict[str, Any]] = []

    for mapping in settings.get_action_mappings():
        action_mappings.append({
            "action_name": str(_mapping_property(mapping, "action_name", "")),
            "key": _key_to_string(_mapping_property(mapping, "key", "")),
            "shift": _bool_property(mapping, "shift"),
            "ctrl": _bool_property(mapping, "ctrl"),
            "alt": _bool_property(mapping, "alt"),
            "cmd": _bool_property(mapping, "cmd"),
        })

    for mapping in settings.get_axis_mappings():
        axis_mappings.append({
            "axis_name": str(_mapping_property(mapping, "axis_name", "")),
            "key": _key_to_string(_mapping_property(mapping, "key", "")),
            "scale": _parse_float(_mapping_property(mapping, "scale", 1.0), 1.0),
        })

    return {
        "source": "unreal.InputSettings",
        "action_mappings": action_mappings,
        "axis_mappings": axis_mappings,
    }


def handle_input_mapping_info(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return input action and axis mappings, with optional filters.

    Args:
        params:
            - action_name (str): Exact action name to filter, such as PF_Pause.
            - axis_name (str): Exact axis name to filter.
            - key (str): Exact key name to filter, such as Escape.

    Returns:
        Matching input mappings and booleans useful for validation.
    """
    try:
        import unreal

        action_filter = _normalize_filter(params.get("action_name", ""))
        axis_filter = _normalize_filter(params.get("axis_name", ""))
        key_filter = _normalize_filter(params.get("key", ""))

        try:
            mapping_data = _read_input_mappings_from_unreal()
        except Exception:
            mapping_data = _read_input_mappings_from_ini(unreal.SystemLibrary.get_project_directory())

        action_mappings = [
            mapping for mapping in mapping_data.get("action_mappings", [])
            if _mapping_matches(mapping, action_filter, key_filter, "action_name")
        ]
        axis_mappings = [
            mapping for mapping in mapping_data.get("axis_mappings", [])
            if _mapping_matches(mapping, axis_filter, key_filter, "axis_name")
        ]

        return {
            "success": True,
            "data": {
                "source": mapping_data.get("source", "unknown"),
                "ini_path": mapping_data.get("ini_path"),
                "filters": {
                    "action_name": params.get("action_name", ""),
                    "axis_name": params.get("axis_name", ""),
                    "key": params.get("key", ""),
                },
                "action_count": len(action_mappings),
                "axis_count": len(axis_mappings),
                "action_mappings": action_mappings,
                "axis_mappings": axis_mappings,
                "matches": {
                    "action_name": bool(action_filter and action_mappings),
                    "axis_name": bool(axis_filter and axis_mappings),
                    "key": bool(key_filter and (action_mappings or axis_mappings)),
                    "action_key": bool(action_filter and key_filter and action_mappings),
                    "axis_key": bool(axis_filter and key_filter and axis_mappings),
                },
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def _asset_package_path(path: str) -> str:
    if "." in path:
        return path.split(".", 1)[0]
    return path


def _asset_name_from_path(path: str) -> str:
    package_path = _asset_package_path(path)
    return package_path.rsplit("/", 1)[-1]


def _object_path_for_asset(path: str) -> str:
    if "." in path:
        return path
    return f"{path}.{_asset_name_from_path(path)}"


def _dependency_options() -> Any:
    import unreal

    try:
        return unreal.AssetRegistryDependencyOptions(True, True, True, True)
    except Exception:
        return None


def _try_load_path(path: str) -> Dict[str, Any]:
    import unreal

    result: Dict[str, Any] = {
        "path": path,
        "loads": False,
        "load_kind": "object",
    }
    try:
        if path.startswith("/Script/") or path.endswith("_C"):
            loaded = unreal.load_class(None, path)
            result["load_kind"] = "class"
        else:
            loaded = unreal.load_object(None, _object_path_for_asset(path))
        result["loads"] = bool(loaded)
        if loaded:
            result["resolved_type"] = loaded.get_class().get_name()
    except Exception as exc:
        result["error"] = str(exc)
    return result


def handle_asset_load_diagnostics(params: Dict[str, Any]) -> Dict[str, Any]:
    """Diagnose asset, generated class, dependency, and script class load state."""
    try:
        import unreal

        asset_paths = params.get("asset_paths", [])
        if isinstance(asset_paths, str):
            asset_paths = [asset_paths]
        class_paths = params.get("class_paths", [])
        if isinstance(class_paths, str):
            class_paths = [class_paths]
        include_dependencies = bool(params.get("include_dependencies", True))
        include_referencers = bool(params.get("include_referencers", True))

        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        try:
            registry.scan_paths_synchronous(["/Game"], True)
        except Exception:
            pass
        dep_options = _dependency_options()

        diagnostics: List[Dict[str, Any]] = []
        for path in asset_paths:
            package_path = _asset_package_path(str(path))
            object_path = _object_path_for_asset(str(path))
            row: Dict[str, Any] = {
                "path": str(path),
                "package_path": package_path,
                "object_path": object_path,
                "asset_exists": bool(unreal.EditorAssetLibrary.does_asset_exist(package_path)),
            }
            load_result = _try_load_path(str(path))
            row.update({
                "loads": load_result.get("loads", False),
                "resolved_type": load_result.get("resolved_type"),
                "load_error": load_result.get("error"),
            })

            asset_data = unreal.EditorAssetLibrary.find_asset_data(package_path)
            if asset_data and asset_data.is_valid():
                row["asset_class"] = str(asset_data.asset_class)
                row["asset_name"] = str(asset_data.asset_name)

            if row.get("asset_class") == "Blueprint":
                generated_class_path = f"{package_path}.{_asset_name_from_path(package_path)}_C"
                row["generated_class_path"] = generated_class_path
                row["generated_class_load"] = _try_load_path(generated_class_path)

            if dep_options is not None:
                if include_dependencies:
                    try:
                        dependencies = [str(item) for item in registry.get_dependencies(package_path, dep_options)]
                    except Exception:
                        dependencies = []
                    row["dependencies"] = dependencies
                    row["missing_script_dependencies"] = [
                        dep for dep in dependencies
                        if dep.startswith("/Script/") and not _try_load_path(dep).get("loads", False)
                    ]
                if include_referencers:
                    try:
                        row["referencers"] = [str(item) for item in registry.get_referencers(package_path, dep_options)]
                    except Exception:
                        row["referencers"] = []

            diagnostics.append(row)

        class_results = [_try_load_path(str(path)) for path in class_paths]

        return {
            "success": True,
            "data": {
                "asset_count": len(diagnostics),
                "class_count": len(class_results),
                "assets": diagnostics,
                "classes": class_results,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_asset_save_many(params: Dict[str, Any]) -> Dict[str, Any]:
    """Save only the explicit assets passed in paths."""
    try:
        import unreal

        paths = params.get("paths", [])
        if isinstance(paths, str):
            paths = [paths]
        if not paths:
            return {"success": False, "data": {}, "error": "Missing 'paths' parameter"}

        results: List[Dict[str, Any]] = []
        for raw_path in paths:
            path = _asset_package_path(str(raw_path))
            row: Dict[str, Any] = {"path": path, "saved": False}
            try:
                asset = unreal.EditorAssetLibrary.load_asset(path)
                if asset is None:
                    row["error"] = "Asset not found or failed to load"
                else:
                    row["saved"] = bool(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False))
            except Exception as exc:
                row["error"] = str(exc)
            results.append(row)

        failed = [row for row in results if not row.get("saved")]
        return {
            "success": len(failed) == 0,
            "data": {
                "saved_count": len(results) - len(failed),
                "failed_count": len(failed),
                "results": results,
            },
            "error": "One or more assets failed to save" if failed else None,
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def _find_plugin_descriptor(project_dir: str, plugin_name: str) -> str:
    import unreal

    try:
        engine_dir = unreal.Paths.engine_dir()
    except Exception:
        engine_dir = ""

    roots = [
        os.path.join(project_dir, "Plugins"),
        os.path.join(engine_dir, "Plugins") if engine_dir else "",
    ]
    target = f"{plugin_name}.uplugin".lower()
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, _, filenames in os.walk(root):
            for filename in filenames:
                if filename.lower() == target:
                    return os.path.join(dirpath, filename)
    return ""


def handle_project_enable_plugins(params: Dict[str, Any]) -> Dict[str, Any]:
    """Enable plugin entries in the current project's .uproject file."""
    try:
        import unreal

        names = params.get("plugins", [])
        if isinstance(names, str):
            names = [names]
        names = [str(name).strip() for name in names if str(name).strip()]
        if not names:
            return {"success": False, "data": {}, "error": "Missing 'plugins' parameter"}

        project_file = unreal.Paths.get_project_file_path()
        project_dir = unreal.SystemLibrary.get_project_directory()
        with open(project_file, encoding="utf-8-sig") as handle:
            project_data = json.load(handle)

        plugin_entries = project_data.setdefault("Plugins", [])
        changed = False
        results: List[Dict[str, Any]] = []

        for plugin_name in names:
            descriptor = _find_plugin_descriptor(project_dir, plugin_name)
            existing = None
            for entry in plugin_entries:
                if str(entry.get("Name", "")).lower() == plugin_name.lower():
                    existing = entry
                    break

            row = {
                "name": plugin_name,
                "descriptor": descriptor,
                "available": bool(descriptor),
                "changed": False,
                "enabled": True,
            }

            if existing is None:
                plugin_entries.append({"Name": plugin_name, "Enabled": True})
                row["changed"] = True
                changed = True
            elif not bool(existing.get("Enabled", False)):
                existing["Enabled"] = True
                row["changed"] = True
                changed = True

            results.append(row)

        if changed:
            with open(project_file, "w", encoding="utf-8") as handle:
                json.dump(project_data, handle, indent="\t")
                handle.write("\n")

        unavailable = [row["name"] for row in results if not row["available"]]
        return {
            "success": True,
            "data": {
                "project_file": project_file,
                "changed": changed,
                "requires_editor_restart": changed,
                "plugins": results,
                "unavailable_plugins": unavailable,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}
