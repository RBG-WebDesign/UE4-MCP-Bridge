"""Game development workflow handlers: input mappings, gameplay framework,
camera rigs, and AI assets (Blackboard + Behavior Tree).

Input mutation and blackboard wiring require the MCPBridgeInputLibrary and
MCPBridgeAILibrary C++ helpers (UE4.27 Python cannot construct FKey and
cannot touch UBehaviorTree::BlackboardAsset). Handlers fail with a clear
message if the plugin C++ is not loaded.
"""

import json
import os
from typing import Any, Dict, List, Optional, Tuple

from mcp_bridge.utils.transactions import transactional


def _fail(message: str) -> Dict[str, Any]:
    return {"success": False, "data": {}, "error": message}


def _lib(name: str) -> Tuple[Optional[Any], Optional[str]]:
    import unreal

    lib = getattr(unreal, name, None)
    if lib is None:
        return None, (f"{name} not available. Rebuild the MCPBridge plugin and "
                      "restart the editor to load the C++ helpers.")
    return lib, None


def _bp_generated_class(asset_path: str) -> Tuple[Optional[Any], Optional[str]]:
    """Load a Blueprint's generated class. UE4.27 Python does not expose
    UBlueprint::GeneratedClass, so load the _C class object directly."""
    import unreal

    asset_name = asset_path.rstrip("/").rsplit("/", 1)[-1]
    class_path = f"{asset_path}.{asset_name}_C"
    cls = unreal.load_class(None, class_path)
    if cls is None:
        return None, f"Could not load generated class: {class_path}"
    return cls, None


# ---------------------------------------------------------------------------
# Input mappings
# ---------------------------------------------------------------------------

def handle_input_mapping_add(params: Dict[str, Any]) -> Dict[str, Any]:
    """Add an action or axis mapping to project input settings.

    Args:
        params: kind ('action'|'axis'), name (str), key (FKey name, e.g.
            'SpaceBar', 'W', 'MouseX', 'Gamepad_LeftTrigger'), scale (float,
            axis only, default 1.0), shift/ctrl/alt/cmd (bool, action only).
    """
    lib, err = _lib("MCPBridgeInputLibrary")
    if err:
        return _fail(err)
    kind = params.get("kind", "action")
    name = params.get("name", "")
    key = params.get("key", "")
    if not name or not key:
        return _fail("Requires 'name' and 'key'")

    if kind == "action":
        result = lib.add_action_mapping(
            name, key,
            bool(params.get("shift", False)), bool(params.get("ctrl", False)),
            bool(params.get("alt", False)), bool(params.get("cmd", False)),
        )
    elif kind == "axis":
        result = lib.add_axis_mapping(name, key, float(params.get("scale", 1.0)))
    else:
        return _fail(f"Unknown kind '{kind}': use 'action' or 'axis'")

    if result:
        return _fail(str(result))
    return {"success": True, "data": {"kind": kind, "name": name, "key": key,
                                      "saved_to": "Config/DefaultInput.ini"}}


def handle_input_mapping_remove(params: Dict[str, Any]) -> Dict[str, Any]:
    """Remove action/axis mappings. Empty 'key' removes all keys for the name."""
    lib, err = _lib("MCPBridgeInputLibrary")
    if err:
        return _fail(err)
    kind = params.get("kind", "action")
    name = params.get("name", "")
    if not name:
        return _fail("Requires 'name'")
    key = params.get("key", "")

    if kind == "action":
        result = lib.remove_action_mapping(name, key)
    elif kind == "axis":
        result = lib.remove_axis_mapping(name, key)
    else:
        return _fail(f"Unknown kind '{kind}': use 'action' or 'axis'")

    if result:
        return _fail(str(result))
    return {"success": True, "data": {"kind": kind, "name": name, "key": key or "(all)"}}


# preset name -> (axis mappings, action mappings)
_INPUT_PRESETS: Dict[str, Dict[str, List[Dict[str, Any]]]] = {
    "first_person": {
        "axes": [
            {"name": "MoveForward", "key": "W", "scale": 1.0},
            {"name": "MoveForward", "key": "S", "scale": -1.0},
            {"name": "MoveRight", "key": "D", "scale": 1.0},
            {"name": "MoveRight", "key": "A", "scale": -1.0},
            {"name": "Turn", "key": "MouseX", "scale": 1.0},
            {"name": "LookUp", "key": "MouseY", "scale": -1.0},
        ],
        "actions": [
            {"name": "Jump", "key": "SpaceBar"},
            {"name": "Fire", "key": "LeftMouseButton"},
            {"name": "Interact", "key": "E"},
            {"name": "Crouch", "key": "LeftControl"},
            {"name": "Sprint", "key": "LeftShift"},
        ],
    },
    "third_person": {
        "axes": [
            {"name": "MoveForward", "key": "W", "scale": 1.0},
            {"name": "MoveForward", "key": "S", "scale": -1.0},
            {"name": "MoveRight", "key": "D", "scale": 1.0},
            {"name": "MoveRight", "key": "A", "scale": -1.0},
            {"name": "Turn", "key": "MouseX", "scale": 1.0},
            {"name": "LookUp", "key": "MouseY", "scale": -1.0},
        ],
        "actions": [
            {"name": "Jump", "key": "SpaceBar"},
            {"name": "Interact", "key": "E"},
            {"name": "Sprint", "key": "LeftShift"},
        ],
    },
    "top_down": {
        "axes": [
            {"name": "MoveForward", "key": "W", "scale": 1.0},
            {"name": "MoveForward", "key": "S", "scale": -1.0},
            {"name": "MoveRight", "key": "D", "scale": 1.0},
            {"name": "MoveRight", "key": "A", "scale": -1.0},
        ],
        "actions": [
            {"name": "Interact", "key": "E"},
            {"name": "PrimaryAction", "key": "LeftMouseButton"},
        ],
    },
    "tank": {
        "axes": [
            {"name": "MoveForward", "key": "W", "scale": 1.0},
            {"name": "MoveForward", "key": "S", "scale": -1.0},
            {"name": "TurnBody", "key": "D", "scale": 1.0},
            {"name": "TurnBody", "key": "A", "scale": -1.0},
        ],
        "actions": [
            {"name": "Fire", "key": "SpaceBar"},
            {"name": "Interact", "key": "E"},
        ],
    },
}


def handle_input_preset_apply(params: Dict[str, Any]) -> Dict[str, Any]:
    """Apply a named input preset (first_person, third_person, top_down, tank)."""
    preset_name = params.get("preset", "")
    preset = _INPUT_PRESETS.get(preset_name)
    if preset is None:
        return _fail(f"Unknown preset '{preset_name}'. Available: {sorted(_INPUT_PRESETS)}")
    lib, err = _lib("MCPBridgeInputLibrary")
    if err:
        return _fail(err)

    applied = []
    errors = []
    for axis in preset["axes"]:
        result = lib.add_axis_mapping(axis["name"], axis["key"], axis["scale"])
        (errors if result else applied).append(f"axis {axis['name']}={axis['key']}" + (f" ({result})" if result else ""))
    for action in preset["actions"]:
        result = lib.add_action_mapping(action["name"], action["key"], False, False, False, False)
        (errors if result else applied).append(f"action {action['name']}={action['key']}" + (f" ({result})" if result else ""))

    if errors:
        return {"success": False, "data": {"applied": applied, "errors": errors},
                "error": f"{len(errors)} mapping(s) failed"}
    return {"success": True, "data": {"preset": preset_name, "applied": applied,
                                      "saved_to": "Config/DefaultInput.ini"}}


# ---------------------------------------------------------------------------
# Gameplay framework
# ---------------------------------------------------------------------------

_FRAMEWORK_PIECES = {
    "game_mode": ("GameModeBase", "GM"),
    "pawn": ("Pawn", "Pawn"),
    "character": ("Character", "Char"),
    "player_controller": ("PlayerController", "PC"),
    "game_instance": ("GameInstance", "GI"),
    "hud": ("HUD", "HUD"),
    "game_state": ("GameStateBase", "GS"),
    "player_state": ("PlayerState", "PS"),
}

# GameMode CDO property per piece
_GM_WIRING = {
    "pawn": "default_pawn_class",
    "character": "default_pawn_class",
    "player_controller": "player_controller_class",
    "hud": "hud_class",
    "game_state": "game_state_class",
    "player_state": "player_state_class",
}


@transactional("MCP Create Gameplay Framework")
def handle_gameplay_framework_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a linked set of gameplay framework Blueprints.

    Creates the requested pieces (default: game_mode, character,
    player_controller) as Blueprints named BP_<base_name><suffix> under
    'path', wires the GameMode's class defaults to the created pieces,
    compiles, and saves.

    Args:
        params: path (str content dir), base_name (str), pieces (list of
            game_mode/pawn/character/player_controller/game_instance/hud/
            game_state/player_state).
    """
    import unreal
    from mcp_bridge.handlers.blueprints import handle_blueprint_create

    path = params.get("path", "")
    base_name = params.get("base_name", "")
    pieces = params.get("pieces") or ["game_mode", "character", "player_controller"]
    if not path or not base_name:
        return _fail("Requires 'path' and 'base_name'")
    bad = [p for p in pieces if p not in _FRAMEWORK_PIECES]
    if bad:
        return _fail(f"Unknown pieces {bad}. Available: {sorted(_FRAMEWORK_PIECES)}")
    if "pawn" in pieces and "character" in pieces:
        return _fail("Choose either 'pawn' or 'character', not both (both wire default_pawn_class)")

    created: Dict[str, str] = {}
    for piece in pieces:
        parent, suffix = _FRAMEWORK_PIECES[piece]
        bp_name = f"BP_{base_name}{suffix}"
        result = handle_blueprint_create({"name": bp_name, "path": path, "parent_class": parent})
        if not result.get("success"):
            return {"success": False, "data": {"created": created},
                    "error": f"Failed to create {piece} ({bp_name}): {result.get('error')}"}
        created[piece] = f"{path.rstrip('/')}/{bp_name}"

    wiring: Dict[str, str] = {}
    if "game_mode" in created:
        gm_class, err = _bp_generated_class(created["game_mode"])
        if err:
            return {"success": False, "data": {"created": created}, "error": err}
        gm_cdo = unreal.get_default_object(gm_class)
        for piece, prop in _GM_WIRING.items():
            if piece in created:
                piece_class, err = _bp_generated_class(created[piece])
                if err:
                    return {"success": False, "data": {"created": created, "wiring": wiring}, "error": err}
                try:
                    gm_cdo.set_editor_property(prop, piece_class)
                    wiring[prop] = created[piece]
                except Exception as exc:
                    return {"success": False, "data": {"created": created, "wiring": wiring},
                            "error": f"Failed to wire GameMode.{prop}: {exc}"}
        unreal.EditorAssetLibrary.save_asset(created["game_mode"], only_if_is_dirty=False)

    for asset_path in created.values():
        unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)

    return {"success": True, "data": {"created": created, "game_mode_wiring": wiring}}


def handle_project_settings_maps(params: Dict[str, Any]) -> Dict[str, Any]:
    """Set default maps and game mode in project settings.

    Updates the GameMapsSettings CDO (live) and persists the values to
    Config/DefaultEngine.ini (UE4.27 Python cannot call SaveConfig).

    Args:
        params: game_default_map (str, optional), editor_startup_map (str,
            optional), global_default_game_mode (str class path, optional).
    """
    import unreal

    updates = {}
    for param_key, ini_key in (
        ("game_default_map", "GameDefaultMap"),
        ("editor_startup_map", "EditorStartupMap"),
        ("global_default_game_mode", "GlobalDefaultGameMode"),
    ):
        value = params.get(param_key)
        if value:
            updates[ini_key] = value
    if not updates:
        return _fail("Provide at least one of: game_default_map, editor_startup_map, global_default_game_mode")

    # Live CDO update. Map properties are SoftObjectPath (constructible from
    # Python); GlobalDefaultGameMode is a SoftClassPath, which 4.27 Python
    # cannot construct at all - that key is ini-only and takes effect on the
    # next editor start (verified live 2026-07-05).
    settings = unreal.get_default_object(unreal.GameMapsSettings)
    cdo_errors = []
    for param_key in ("game_default_map", "editor_startup_map"):
        if params.get(param_key):
            try:
                settings.set_editor_property(param_key, unreal.SoftObjectPath(params[param_key]))
            except Exception as exc:
                cdo_errors.append(f"{param_key}: {exc}")
    if params.get("global_default_game_mode"):
        cdo_errors.append(
            "global_default_game_mode: persisted to ini only (SoftClassPath is "
            "not constructible from UE4.27 Python); takes effect on editor restart"
        )

    # Persist to DefaultEngine.ini
    project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    ini_path = os.path.join(project_dir, "Config", "DefaultEngine.ini")
    section = "[/Script/EngineSettings.GameMapsSettings]"
    try:
        with open(ini_path, "r", encoding="utf-8") as fh:
            lines = fh.readlines()
    except FileNotFoundError:
        lines = []

    out_lines: List[str] = []
    in_section = False
    section_found = False
    pending = dict(updates)
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("["):
            if in_section and pending:
                for key, value in pending.items():
                    out_lines.append(f"{key}={value}\n")
                pending = {}
            in_section = stripped == section
            if in_section:
                section_found = True
            out_lines.append(line)
            continue
        if in_section:
            key = stripped.split("=", 1)[0] if "=" in stripped else ""
            if key in pending:
                out_lines.append(f"{key}={pending.pop(key)}\n")
                continue
        out_lines.append(line)
    if pending:
        if not section_found:
            if out_lines and not out_lines[-1].endswith("\n"):
                out_lines[-1] += "\n"
            out_lines.append(f"\n{section}\n")
        for key, value in pending.items():
            out_lines.append(f"{key}={value}\n")

    with open(ini_path, "w", encoding="utf-8") as fh:
        fh.writelines(out_lines)

    data = {"updated": updates, "ini": ini_path}
    if cdo_errors:
        data["cdo_warnings"] = cdo_errors
        data["note"] = "Persisted to ini; live CDO update partially failed (takes effect on restart)."
    return {"success": True, "data": data}


# ---------------------------------------------------------------------------
# Camera rigs
# ---------------------------------------------------------------------------

_CAMERA_PRESETS: Dict[str, Dict[str, Any]] = {
    "first_person": {
        "use_arm": False,
        "camera_props": {
            "RelativeLocation": {"X": 0, "Y": 0, "Z": 64},
            "bUsePawnControlRotation": True,
        },
    },
    "third_person": {
        "use_arm": True,
        "arm_props": {
            "TargetArmLength": 300.0,
            "bUsePawnControlRotation": True,
            "bEnableCameraLag": True,
            "CameraLagSpeed": 10.0,
        },
        "camera_props": {"bUsePawnControlRotation": False},
    },
    "top_down": {
        "use_arm": True,
        "arm_props": {
            "TargetArmLength": 800.0,
            "RelativeRotation": {"Pitch": -60.0, "Yaw": 0.0, "Roll": 0.0},
            "bUsePawnControlRotation": False,
            "bInheritPitch": False, "bInheritYaw": False, "bInheritRoll": False,
            "bDoCollisionTest": False,
        },
        "camera_props": {"bUsePawnControlRotation": False},
    },
    "side_scroller": {
        "use_arm": True,
        "arm_props": {
            "TargetArmLength": 500.0,
            "RelativeRotation": {"Pitch": 0.0, "Yaw": -90.0, "Roll": 0.0},
            "bUsePawnControlRotation": False,
            "bInheritPitch": False, "bInheritYaw": False, "bInheritRoll": False,
            "bDoCollisionTest": False,
        },
        "camera_props": {"bUsePawnControlRotation": False},
    },
}


@transactional("MCP Create Camera Rig")
def handle_camera_rig_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Add a camera rig (SpringArm + Camera per preset) to a Pawn/Character Blueprint.

    Args:
        params: blueprint_path (str), preset (first_person/third_person/
            top_down/side_scroller), camera_name (default 'Camera'),
            arm_name (default 'CameraBoom').
    """
    import unreal
    from mcp_bridge.handlers.blueprints import _compile_blueprint_ue427_safe

    preset_name = params.get("preset", "")
    preset = _CAMERA_PRESETS.get(preset_name)
    if preset is None:
        return _fail(f"Unknown preset '{preset_name}'. Available: {sorted(_CAMERA_PRESETS)}")

    bp_path = params.get("blueprint_path", "")
    if not bp_path or not unreal.EditorAssetLibrary.does_asset_exist(bp_path):
        return _fail(f"Blueprint not found: {bp_path}")
    bp = unreal.load_asset(bp_path)
    if not isinstance(bp, unreal.Blueprint):
        return _fail(f"Asset is not a Blueprint: {bp_path}")

    lib, err = _lib("BlueprintGraphBuilderLibrary")
    if err:
        return _fail(err)

    camera_name = params.get("camera_name", "Camera")
    arm_name = params.get("arm_name", "CameraBoom")
    added = []

    if preset["use_arm"]:
        if not lib.add_component_to_blueprint(bp, unreal.SpringArmComponent, arm_name, ""):
            return _fail(f"Failed to add SpringArmComponent '{arm_name}'")
        added.append(arm_name)
        for prop, value in preset.get("arm_props", {}).items():
            if not lib.set_component_property(bp, arm_name, prop, json.dumps(value)):
                return _fail(f"Failed to set {arm_name}.{prop}")
        camera_parent = arm_name
    else:
        camera_parent = ""

    if not lib.add_component_to_blueprint(bp, unreal.CameraComponent, camera_name, camera_parent):
        return _fail(f"Failed to add CameraComponent '{camera_name}'")
    added.append(camera_name)
    for prop, value in preset.get("camera_props", {}).items():
        if not lib.set_component_property(bp, camera_name, prop, json.dumps(value)):
            return _fail(f"Failed to set {camera_name}.{prop}")

    compile_report = _compile_blueprint_ue427_safe(bp, bp_path)
    saved = bool(unreal.EditorAssetLibrary.save_asset(bp_path, only_if_is_dirty=False))
    ok = bool(compile_report.get("compiled")) and saved
    return {
        "success": ok,
        "data": {"preset": preset_name, "components": added,
                 "compile": compile_report, "saved": saved},
        **({} if ok else {"error": "Camera rig added but compile or save failed; see data"}),
    }


# ---------------------------------------------------------------------------
# AI: Blackboard + Behavior Tree
# ---------------------------------------------------------------------------

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


@transactional("MCP Create Blackboard")
def handle_blackboard_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a BlackboardData asset with typed keys.

    Args:
        params: path (str), name (str), keys (list of {name, type, base_class?};
            types: Bool, Int, Float, String, Name, Vector, Rotator, Object, Class).
    """
    import unreal

    ai_lib, err = _lib("MCPBridgeAILibrary")
    if err:
        return _fail(err)
    name = params.get("name", "")
    path = params.get("path", "")
    if not name or not path:
        return _fail("Requires 'name' and 'path'")

    bb, err = _create_asset(name, path, unreal.BlackboardData, unreal.BlackboardDataFactory())
    if err:
        return _fail(err)

    keys = params.get("keys", [])
    if keys:
        result = ai_lib.add_blackboard_keys(bb, json.dumps(keys))
        if result:
            return _fail(f"Blackboard created but key setup failed: {result}")

    full = f"{path.rstrip('/')}/{name}"
    saved = bool(unreal.EditorAssetLibrary.save_asset(full, only_if_is_dirty=False))
    key_list = json.loads(str(ai_lib.list_blackboard_keys(bb)) or "[]")
    return {"success": saved, "data": {"blackboard": full, "keys": key_list, "saved": saved},
            **({} if saved else {"error": "Blackboard created but save failed"})}


@transactional("MCP Create Behavior Tree")
def handle_behavior_tree_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a BehaviorTree asset bound to a Blackboard, optionally building
    its node graph from JSON via the C++ BehaviorTreeBuilder (26 node types).

    Args:
        params: path (str), name (str), blackboard_path (str, existing asset),
            keys (optional list, added to the blackboard first),
            tree (optional dict, BehaviorTreeBuilder JSON spec).
    """
    import unreal

    ai_lib, err = _lib("MCPBridgeAILibrary")
    if err:
        return _fail(err)
    bt_lib, err = _lib("BehaviorTreeBuilderLibrary")
    if err:
        return _fail(err)

    name = params.get("name", "")
    path = params.get("path", "")
    bb_path = params.get("blackboard_path", "")
    if not name or not path or not bb_path:
        return _fail("Requires 'name', 'path', and 'blackboard_path'")
    if not unreal.EditorAssetLibrary.does_asset_exist(bb_path):
        return _fail(f"Blackboard asset not found: {bb_path} (create it with blackboard_create)")
    bb = unreal.load_asset(bb_path)

    keys = params.get("keys", [])
    if keys:
        result = ai_lib.add_blackboard_keys(bb, json.dumps(keys))
        if result:
            return _fail(f"Blackboard key setup failed: {result}")
        unreal.EditorAssetLibrary.save_asset(bb_path, only_if_is_dirty=False)

    bt, err = _create_asset(name, path, unreal.BehaviorTree, unreal.BehaviorTreeFactory())
    if err:
        return _fail(err)

    result = ai_lib.set_blackboard_asset(bt, bb)
    if result:
        return _fail(f"Failed to assign blackboard: {result}")

    full = f"{path.rstrip('/')}/{name}"
    build_result = None
    tree_spec = params.get("tree")
    if tree_spec:
        build_error = str(bt_lib.build_behavior_tree_from_json(bt, json.dumps(tree_spec)))
        if build_error:
            return {"success": False,
                    "data": {"behavior_tree": full, "blackboard": bb_path},
                    "error": f"BT asset created but graph build failed: {build_error}"}
        build_result = "built"

    saved = bool(unreal.EditorAssetLibrary.save_asset(full, only_if_is_dirty=False))
    return {"success": saved,
            "data": {"behavior_tree": full, "blackboard": bb_path,
                     "graph": build_result or "empty", "saved": saved},
            **({} if saved else {"error": "BehaviorTree created but save failed"})}


def handle_ai_nav_rebuild(params: Dict[str, Any]) -> Dict[str, Any]:
    """Rebuild navigation data in the loaded level (console: RebuildNavigation)."""
    import unreal

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        return _fail("No editor world loaded")
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    return {"success": True, "data": {"command": "RebuildNavigation",
                                      "note": "Navigation rebuild triggered; check Output Log for results."}}
