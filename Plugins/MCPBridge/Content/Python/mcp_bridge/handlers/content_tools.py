"""Content tools: material instances, data tables, audio components, map
creation, and the game template generator.

The template generator composes already-verified tools (framework creation,
camera rigs, input presets, project settings) into a playable skeleton.
"""

import json
from typing import Any, Dict, List, Optional, Tuple

from mcp_bridge.utils.transactions import transactional


def _fail(message: str, data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    return {"success": False, "data": data or {}, "error": message}


def _create_asset(name: str, path: str, asset_class: Any, factory: Any) -> Tuple[Optional[Any], Optional[str]]:
    import unreal

    full = f"{path.rstrip('/')}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        return None, f"Asset already exists: {full}"
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = tools.create_asset(name, path, asset_class, factory)
    if asset is None:
        return None, f"Failed to create asset {full}"
    return asset, None


def _save(path: str) -> bool:
    import unreal

    return bool(unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))


def _fill_data_table(table: Any, payload: str) -> Tuple[bool, List[str], List[str]]:
    """Fill a DataTable from JSON via the dialog-free C++ helper.

    unreal.DataTableFunctionLibrary.fill_data_table_from_json_string shows a
    modal summary dialog on the game thread, which hangs the bridge. The C++
    UMCPBridgeDataLibrary wraps UDataTable::CreateTableFromJSONString, which
    reports problems as strings and shows no dialog.

    Returns (success, problems, row_names).
    """
    import unreal

    lib = getattr(unreal, "MCPBridgeDataLibrary", None)
    if lib is None:
        return False, ["MCPBridgeDataLibrary not available. Rebuild the MCPBridge "
                       "plugin and restart the editor to load the C++ helpers."], []
    raw = str(lib.fill_data_table_from_json(table, payload))
    try:
        result = json.loads(raw)
    except json.JSONDecodeError:
        return False, [f"Fill helper returned non-JSON: {raw[:200]}"], []
    return bool(result.get("success")), result.get("problems", []), result.get("rows", [])


# ---------------------------------------------------------------------------
# Material instances
# ---------------------------------------------------------------------------

def _apply_instance_params(mic: Any, params: Dict[str, Any]) -> List[str]:
    """Apply scalar/vector/texture params to a material instance.
    Returns a list of error strings (empty on full success)."""
    import unreal

    mel = unreal.MaterialEditingLibrary
    errors: List[str] = []

    for name, value in (params.get("scalar_params") or {}).items():
        if not mel.set_material_instance_scalar_parameter_value(mic, name, float(value)):
            errors.append(f"scalar '{name}' not found on parent material")
    for name, value in (params.get("vector_params") or {}).items():
        try:
            color = unreal.LinearColor(
                float(value[0]), float(value[1]), float(value[2]),
                float(value[3]) if len(value) > 3 else 1.0)
        except (TypeError, IndexError):
            errors.append(f"vector '{name}' must be [r, g, b] or [r, g, b, a]")
            continue
        if not mel.set_material_instance_vector_parameter_value(mic, name, color):
            errors.append(f"vector '{name}' not found on parent material")
    for name, tex_path in (params.get("texture_params") or {}).items():
        tex = unreal.load_asset(tex_path)
        if tex is None:
            errors.append(f"texture asset not found: {tex_path}")
            continue
        if not mel.set_material_instance_texture_parameter_value(mic, name, tex):
            errors.append(f"texture '{name}' not found on parent material")
    return errors


@transactional("MCP Create Material Instance")
def handle_material_instance_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a MaterialInstanceConstant from a parent material with
    optional scalar/vector/texture parameter overrides.

    Args:
        params: path (str), name (str), parent_material (str asset path),
            scalar_params ({name: float}), vector_params ({name: [r,g,b,a?]}),
            texture_params ({name: texture asset path}).
    """
    import unreal

    name = params.get("name", "")
    path = params.get("path", "")
    parent_path = params.get("parent_material", "")
    if not name or not path or not parent_path:
        return _fail("Requires 'name', 'path', and 'parent_material'")

    parent = unreal.load_asset(parent_path)
    if parent is None or not isinstance(parent, unreal.MaterialInterface):
        return _fail(f"Parent material not found or not a material: {parent_path}")

    mic, err = _create_asset(name, path, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
    if err:
        return _fail(err)

    unreal.MaterialEditingLibrary.set_material_instance_parent(mic, parent)
    param_errors = _apply_instance_params(mic, params)
    unreal.MaterialEditingLibrary.update_material_instance(mic)

    full = f"{path.rstrip('/')}/{name}"
    saved = _save(full)
    if param_errors:
        return _fail(f"Instance created but {len(param_errors)} parameter(s) failed: {param_errors}",
                     {"material_instance": full, "saved": saved})
    if not saved:
        return _fail("Instance created but save failed", {"material_instance": full})
    return {"success": True, "data": {"material_instance": full, "parent": parent_path, "saved": True}}


@transactional("MCP Set Material Instance Params")
def handle_material_instance_set_params(params: Dict[str, Any]) -> Dict[str, Any]:
    """Set scalar/vector/texture parameters on an existing material instance."""
    import unreal

    mic_path = params.get("material_instance", "")
    if not mic_path:
        return _fail("Requires 'material_instance'")
    mic = unreal.load_asset(mic_path)
    if mic is None or not isinstance(mic, unreal.MaterialInstanceConstant):
        return _fail(f"Not a MaterialInstanceConstant: {mic_path}")

    param_errors = _apply_instance_params(mic, params)
    unreal.MaterialEditingLibrary.update_material_instance(mic)
    saved = _save(mic_path)
    if param_errors:
        return _fail(f"{len(param_errors)} parameter(s) failed: {param_errors}",
                     {"material_instance": mic_path, "saved": saved})
    if not saved:
        return _fail("Parameters set but save failed", {"material_instance": mic_path})
    return {"success": True, "data": {"material_instance": mic_path, "saved": True}}


# ---------------------------------------------------------------------------
# Data tables
# ---------------------------------------------------------------------------

@transactional("MCP Create DataTable")
def handle_data_table_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a DataTable for a row struct, optionally filling rows from JSON.

    Args:
        params: path (str), name (str), row_struct (str: /Script/Module.Struct
            or /Game/... user struct path), rows_json (optional: DataTable
            JSON array [{"Name": "Row1", ...fields}]).
    """
    import unreal

    name = params.get("name", "")
    path = params.get("path", "")
    struct_path = params.get("row_struct", "")
    if not name or not path or not struct_path:
        return _fail("Requires 'name', 'path', and 'row_struct'")

    row_struct = unreal.load_object(None, struct_path)
    if row_struct is None or not isinstance(row_struct, unreal.ScriptStruct):
        return _fail(f"Row struct not found or not a struct: {struct_path}")

    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", row_struct)
    table, err = _create_asset(name, path, unreal.DataTable, factory)
    if err:
        return _fail(err)

    full = f"{path.rstrip('/')}/{name}"
    rows_json = params.get("rows_json")
    rows: List[str] = []
    if rows_json is not None:
        payload = rows_json if isinstance(rows_json, str) else json.dumps(rows_json)
        ok, problems, rows = _fill_data_table(table, payload)
        if not ok:
            return _fail(f"DataTable created but JSON fill reported problems: {problems}",
                         {"data_table": full, "problems": problems})

    saved = _save(full)
    if not saved:
        return _fail("DataTable created but save failed", {"data_table": full})
    return {"success": True, "data": {"data_table": full, "row_struct": struct_path,
                                      "rows": rows, "row_count": len(rows), "saved": True}}


@transactional("MCP Fill DataTable")
def handle_data_table_fill_from_json(params: Dict[str, Any]) -> Dict[str, Any]:
    """Replace an existing DataTable's rows from a DataTable JSON array."""
    import unreal

    table_path = params.get("data_table", "")
    rows_json = params.get("rows_json")
    if not table_path or rows_json is None:
        return _fail("Requires 'data_table' and 'rows_json'")
    table = unreal.load_asset(table_path)
    if table is None or not isinstance(table, unreal.DataTable):
        return _fail(f"Not a DataTable: {table_path}")

    payload = rows_json if isinstance(rows_json, str) else json.dumps(rows_json)
    ok, problems, rows = _fill_data_table(table, payload)
    if not ok:
        return _fail(f"JSON fill reported problems: {problems}",
                     {"data_table": table_path, "problems": problems})
    saved = _save(table_path)
    if not saved:
        return _fail("Rows filled but save failed", {"data_table": table_path, "rows": rows})
    return {"success": True, "data": {"data_table": table_path, "rows": rows,
                                      "row_count": len(rows), "saved": True}}


# ---------------------------------------------------------------------------
# Audio
# ---------------------------------------------------------------------------

@transactional("MCP Add Audio Component")
def handle_audio_component_add(params: Dict[str, Any]) -> Dict[str, Any]:
    """Add an AudioComponent to a Blueprint, optionally assigning a sound.

    Args:
        params: blueprint_path (str), component_name (default 'Audio'),
            sound (optional SoundBase asset path), attach_to (optional parent
            component name), auto_activate (bool, default True).
    """
    import unreal
    from mcp_bridge.handlers.blueprints import _compile_blueprint_ue427_safe

    bp_path = params.get("blueprint_path", "")
    if not bp_path or not unreal.EditorAssetLibrary.does_asset_exist(bp_path):
        return _fail(f"Blueprint not found: {bp_path}")
    bp = unreal.load_asset(bp_path)
    if not isinstance(bp, unreal.Blueprint):
        return _fail(f"Asset is not a Blueprint: {bp_path}")

    lib = getattr(unreal, "BlueprintGraphBuilderLibrary", None)
    if lib is None:
        return _fail("BlueprintGraphBuilderLibrary not available (plugin C++ not loaded)")

    component_name = params.get("component_name", "Audio")
    if not lib.add_component_to_blueprint(bp, unreal.AudioComponent, component_name,
                                          params.get("attach_to", "")):
        return _fail(f"Failed to add AudioComponent '{component_name}'")

    warnings = []
    sound_path = params.get("sound", "")
    if sound_path:
        sound = unreal.load_asset(sound_path)
        if sound is None or not isinstance(sound, unreal.SoundBase):
            return _fail(f"Sound asset not found or not a SoundBase: {sound_path}")
        if not lib.set_component_property(bp, component_name, "Sound", json.dumps(sound_path)):
            warnings.append("Sound property could not be set via the property applier; "
                            "assign it manually or via blueprint defaults")
    if not lib.set_component_property(bp, component_name, "bAutoActivate",
                                      json.dumps(bool(params.get("auto_activate", True)))):
        warnings.append("bAutoActivate could not be set")

    compile_report = _compile_blueprint_ue427_safe(bp, bp_path)
    saved = _save(bp_path)
    ok = bool(compile_report.get("compiled")) and saved
    data = {"component": component_name, "compile": compile_report, "saved": saved}
    if warnings:
        data["warnings"] = warnings
    if not ok:
        return _fail("Audio component added but compile or save failed", data)
    return {"success": True, "data": data}


# ---------------------------------------------------------------------------
# Maps
# ---------------------------------------------------------------------------

def handle_level_new(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create and load a new level, then save it.

    WARNING: this switches the editor's loaded level. Unsaved changes in the
    current level block the operation.

    Args:
        params: path (str, e.g. /Game/Maps/MyMap), template (optional map
            asset path to copy from).
    """
    import unreal

    path = params.get("path", "")
    if not path:
        return _fail("Requires 'path' (e.g. /Game/Maps/MyMap)")
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return _fail(f"Level already exists: {path}")

    dirty = ([p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()] +
             [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    if dirty:
        return _fail(f"Refusing to switch levels with unsaved changes: {dirty[:10]}. "
                     "Save first (level_save / asset_save_many).")

    template = params.get("template", "")
    if template:
        ok = unreal.EditorLevelLibrary.new_level_from_template(path, template)
    else:
        ok = unreal.EditorLevelLibrary.new_level(path)
    if not ok:
        return _fail(f"Level creation failed for {path}")
    unreal.EditorLevelLibrary.save_current_level()
    return {"success": True, "data": {"level": path, "template": template or None,
                                      "note": "New level is now the loaded level."}}


# ---------------------------------------------------------------------------
# M14: game template generator
# ---------------------------------------------------------------------------

_TEMPLATE_PRESETS = {
    "first_person": {"camera": "first_person", "input": "first_person"},
    "third_person": {"camera": "third_person", "input": "third_person"},
    "top_down": {"camera": "top_down", "input": "top_down"},
    "side_scroller": {"camera": "side_scroller", "input": "third_person"},
}


def handle_game_template_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Generate a playable game skeleton by composing verified tools:
    optional new map, gameplay framework (GameMode/Character/PC/HUD), camera
    rig on the character, input preset, and default game mode wiring.

    Args:
        params: path (str content root, e.g. /Game/MyGame), base_name (str),
            template (first_person/third_person/top_down/side_scroller),
            create_map (bool, default False - switches the loaded level!),
            set_as_default (bool, default False - writes project settings).

    Returns:
        Manifest of everything created. Fails fast on the first error with
        the partial manifest.
    """
    from mcp_bridge.handlers.gamedev import (
        handle_gameplay_framework_create,
        handle_camera_rig_create,
        handle_input_preset_apply,
        handle_project_settings_maps,
    )

    template_name = params.get("template", "")
    preset = _TEMPLATE_PRESETS.get(template_name)
    if preset is None:
        return _fail(f"Unknown template '{template_name}'. Available: {sorted(_TEMPLATE_PRESETS)}")
    path = params.get("path", "")
    base_name = params.get("base_name", "")
    if not path or not base_name:
        return _fail("Requires 'path' and 'base_name'")

    manifest: Dict[str, Any] = {"template": template_name}

    map_path = None
    if params.get("create_map", False):
        map_path = f"{path.rstrip('/')}/Maps/{base_name}Map"
        r = handle_level_new({"path": map_path})
        if not r.get("success"):
            return _fail(f"Map creation failed: {r.get('error')}", manifest)
        manifest["map"] = map_path

    fw_path = f"{path.rstrip('/')}/Framework"
    r = handle_gameplay_framework_create({
        "path": fw_path, "base_name": base_name,
        "pieces": ["game_mode", "character", "player_controller", "hud"],
    })
    if not r.get("success"):
        return _fail(f"Framework creation failed: {r.get('error')}", manifest)
    manifest["framework"] = r.get("data", {}).get("created", {})
    manifest["game_mode_wiring"] = r.get("data", {}).get("game_mode_wiring", {})

    char_path = manifest["framework"].get("character")
    r = handle_camera_rig_create({"blueprint_path": char_path, "preset": preset["camera"]})
    if not r.get("success"):
        return _fail(f"Camera rig failed: {r.get('error')}", manifest)
    manifest["camera_rig"] = {"preset": preset["camera"],
                              "components": r.get("data", {}).get("components")}

    r = handle_input_preset_apply({"preset": preset["input"]})
    if not r.get("success"):
        return _fail(f"Input preset failed: {r.get('error')}", manifest)
    manifest["input_preset"] = preset["input"]

    if params.get("set_as_default", False):
        gm_path = manifest["framework"].get("game_mode", "")
        gm_name = gm_path.rsplit("/", 1)[-1]
        settings: Dict[str, Any] = {
            "global_default_game_mode": f"{gm_path}.{gm_name}_C",
        }
        if map_path:
            map_name = map_path.rsplit("/", 1)[-1]
            settings["game_default_map"] = f"{map_path}.{map_name}"
        r = handle_project_settings_maps(settings)
        if not r.get("success"):
            return _fail(f"Project settings failed: {r.get('error')}", manifest)
        manifest["project_settings"] = settings

    manifest["next_steps"] = [
        "Add gameplay logic with blueprint_node_add / blueprint_pins_connect",
        "Wire movement input to the character (MoveForward/MoveRight axis events)",
        "Place a PlayerStart in the map and test with PIE when ready",
    ]
    return {"success": True, "data": manifest}
