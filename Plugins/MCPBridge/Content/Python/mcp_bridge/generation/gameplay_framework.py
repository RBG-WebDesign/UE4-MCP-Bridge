"""Reusable gameplay authoring layer for the MCP bridge.

This module keeps generated gameplay predictable by favoring a small set of
known Blueprint assets, reusable graph templates, and editor-side validators.
All Unreal imports stay inside functions so the module can be syntax checked
outside the editor.
"""
from __future__ import annotations

import datetime
import json
import math
import os
from typing import Any, Dict, Iterable, List, Optional, Tuple


FRAMEWORK_DIR = "/Game/Generated/GameplayFramework"
DESIGN_MEMORY_RELATIVE_PATH = os.path.join(
    "Content", "Python", "mcp_bridge", "data", "design_memory.json"
)


BLUEPRINT_ASSETS = {
    "interactable_base": "BP_InteractableBase",
    "key_item_pickup": "BP_KeyItemPickup",
    "locked_door": "BP_LockedDoor",
    "objective_trigger": "BP_ObjectiveTrigger",
    "finish_zone": "BP_FinishZone",
    "level_state_manager": "BP_LevelStateManager",
}


BLUEPRINT_COMPONENTS = {
    "BP_InteractableBase": [
        {"name": "Root", "class": "SceneComponent"},
        {"name": "VisualMesh", "class": "StaticMeshComponent", "attach": "Root"},
        {"name": "InteractionTrigger", "class": "BoxComponent", "attach": "Root"},
        {"name": "LabelText", "class": "TextRenderComponent", "attach": "Root"},
    ],
    "BP_KeyItemPickup": [
        {"name": "Root", "class": "SceneComponent"},
        {"name": "PickupMesh", "class": "StaticMeshComponent", "attach": "Root"},
        {"name": "PickupTrigger", "class": "BoxComponent", "attach": "Root"},
        {"name": "LabelText", "class": "TextRenderComponent", "attach": "Root"},
    ],
    "BP_LockedDoor": [
        {"name": "Root", "class": "SceneComponent"},
        {"name": "DoorMesh", "class": "StaticMeshComponent", "attach": "Root"},
        {"name": "LockPanel", "class": "StaticMeshComponent", "attach": "Root"},
        {"name": "UseTrigger", "class": "BoxComponent", "attach": "Root"},
        {"name": "LabelText", "class": "TextRenderComponent", "attach": "Root"},
    ],
    "BP_ObjectiveTrigger": [
        {"name": "Root", "class": "SceneComponent"},
        {"name": "ObjectiveVolume", "class": "BoxComponent", "attach": "Root"},
        {"name": "MarkerMesh", "class": "StaticMeshComponent", "attach": "Root"},
        {"name": "LabelText", "class": "TextRenderComponent", "attach": "Root"},
    ],
    "BP_FinishZone": [
        {"name": "Root", "class": "SceneComponent"},
        {"name": "FinishVolume", "class": "BoxComponent", "attach": "Root"},
        {"name": "FinishPad", "class": "StaticMeshComponent", "attach": "Root"},
        {"name": "LabelText", "class": "TextRenderComponent", "attach": "Root"},
    ],
    "BP_LevelStateManager": [
        {"name": "Root", "class": "SceneComponent"},
        {"name": "StateMarker", "class": "TextRenderComponent", "attach": "Root"},
    ],
}


GRAPH_NODE_CATALOG = {
    "event_actor_begin_overlap": {
        "json_type": "ActorBeginOverlap",
        "purpose": "Starts logic when any actor overlaps this actor.",
    },
    "event_component_begin_overlap": {
        "json_type": "ComponentBeginOverlap",
        "purpose": "Starts logic from a named trigger component overlap.",
    },
    "input_action": {
        "json_type": "InputAction",
        "purpose": "Starts logic from a project input action.",
    },
    "branch": {
        "json_type": "Branch",
        "purpose": "Executes true or false flow from a boolean condition.",
    },
    "cast": {
        "json_type": "CastTo",
        "purpose": "Casts an object reference before using class-specific calls.",
    },
    "delay": {
        "json_type": "Delay",
        "purpose": "Waits before continuing execution.",
    },
    "timeline": {
        "json_type": "Timeline",
        "purpose": "Drives gradual value changes such as door motion.",
    },
    "interface_call": {
        "json_type": "InterfaceCall",
        "purpose": "Calls a Blueprint interface function on a target.",
    },
    "set_material": {
        "json_type": "CallFunction",
        "function": "SetMaterial",
        "purpose": "Changes a mesh material at runtime.",
    },
    "set_visibility": {
        "json_type": "CallFunction",
        "function": "SetVisibility",
        "purpose": "Shows or hides a component.",
    },
    "move_component_to": {
        "json_type": "CallFunction",
        "function": "MoveComponentTo",
        "purpose": "Moves a scene component over time.",
    },
    "print_string": {
        "json_type": "PrintString",
        "purpose": "Logs and optionally prints a readable runtime marker.",
    },
}


LEVEL_GRAMMARS = {
    "hallway_with_side_room": {
        "description": "A direct route with a readable branch for a pickup or clue.",
        "segments": ["start", "main_hall", "side_room", "return_to_hall", "exit"],
        "required_actors": ["player_start", "objective_trigger"],
        "validation": ["route_reachable", "label_visibility", "lighting_readable"],
    },
    "lock_and_key_loop": {
        "description": "A key in an optional branch unlocks a blocking door on the critical route.",
        "segments": ["start", "key_branch", "locked_door", "reward_room", "finish"],
        "required_actors": ["player_start", "key_item_pickup", "locked_door", "finish_zone"],
        "validation": ["route_reachable", "objective_chain", "door_open_check"],
    },
    "hub_and_spoke": {
        "description": "A central hub connects several short objective spokes.",
        "segments": ["hub", "spoke_a", "spoke_b", "spoke_c", "hub_exit"],
        "required_actors": ["player_start", "level_state_manager", "objective_trigger", "finish_zone"],
        "validation": ["route_reachable", "objective_chain", "label_visibility"],
    },
    "combat_arena": {
        "description": "An entry gate, readable arena bounds, encounter space, and exit.",
        "segments": ["entry", "arena", "cover", "exit_gate"],
        "required_actors": ["player_start", "objective_trigger", "finish_zone"],
        "validation": ["route_reachable", "clearance_check", "lighting_readable"],
    },
    "stealth_route": {
        "description": "A readable route with optional bypasses and observation pockets.",
        "segments": ["start", "patrol_view", "bypass", "objective", "exit"],
        "required_actors": ["player_start", "objective_trigger", "finish_zone"],
        "validation": ["route_reachable", "label_visibility", "clearance_check"],
    },
    "escape_sequence": {
        "description": "A linear return route with timed-feeling gates and strong visual guidance.",
        "segments": ["trigger", "return_route", "blocker", "shortcut", "finish"],
        "required_actors": ["player_start", "objective_trigger", "finish_zone"],
        "validation": ["route_reachable", "objective_chain", "screenshot_pass"],
    },
    "tutorial_gauntlet": {
        "description": "A sequence of short skill checks with one visible objective at a time.",
        "segments": ["movement", "interaction", "key", "door", "finish"],
        "required_actors": ["player_start", "key_item_pickup", "locked_door", "finish_zone"],
        "validation": ["route_reachable", "objective_chain", "label_visibility"],
    },
}


def success(data: Dict[str, Any]) -> Dict[str, Any]:
    return {"success": True, "data": data}


def failure(message: str, data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    return {"success": False, "data": data or {}, "error": message}


def project_file_path(relative_path: str) -> str:
    import unreal

    return os.path.join(unreal.SystemLibrary.get_project_directory(), relative_path)


def load_design_memory() -> Dict[str, Any]:
    path = project_file_path(DESIGN_MEMORY_RELATIVE_PATH)
    if not os.path.exists(path):
        return {}
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def save_design_memory(memory: Dict[str, Any]) -> str:
    path = project_file_path(DESIGN_MEMORY_RELATIVE_PATH)
    parent = os.path.dirname(path)
    if not os.path.isdir(parent):
        os.makedirs(parent)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(memory, fh, indent=2, sort_keys=True)
    return path


def vector(value: Any, default: Tuple[float, float, float] = (0, 0, 0)) -> Any:
    import unreal

    if isinstance(value, dict):
        return unreal.Vector(
            float(value.get("x", default[0])),
            float(value.get("y", default[1])),
            float(value.get("z", default[2])),
        )
    if isinstance(value, (list, tuple)) and len(value) >= 3:
        return unreal.Vector(float(value[0]), float(value[1]), float(value[2]))
    return unreal.Vector(float(default[0]), float(default[1]), float(default[2]))


def rotator(value: Any, default: Tuple[float, float, float] = (0, 0, 0)) -> Any:
    import unreal

    if isinstance(value, dict):
        return unreal.Rotator(
            float(value.get("pitch", default[0])),
            float(value.get("yaw", default[1])),
            float(value.get("roll", default[2])),
        )
    if isinstance(value, (list, tuple)) and len(value) >= 3:
        return unreal.Rotator(float(value[0]), float(value[1]), float(value[2]))
    return unreal.Rotator(float(default[0]), float(default[1]), float(default[2]))


def vector_dict(value: Any) -> Dict[str, float]:
    return {"x": float(value.x), "y": float(value.y), "z": float(value.z)}


def rotator_dict(value: Any) -> Dict[str, float]:
    return {"pitch": float(value.pitch), "yaw": float(value.yaw), "roll": float(value.roll)}


def ensure_dir(path: str) -> None:
    import unreal

    unreal.EditorAssetLibrary.make_directory(path)


def _load_basic_cube() -> Any:
    import unreal

    return unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube")


def create_material(asset_dir: str, name: str, color: Iterable[float], emissive: float = 0.0, save_enabled: bool = True) -> Any:
    import unreal

    materials_dir = asset_dir + "/Materials"
    ensure_dir(materials_dir)
    path = materials_dir + "/" + name
    existing = unreal.EditorAssetLibrary.load_asset(path)
    if existing:
        mat = existing
        try:
            unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
        except Exception:
            pass
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        mat = tools.create_asset(name, materials_dir, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        raise RuntimeError("Failed to create material {}".format(path))

    values = list(color)
    base = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -400, 0
    )
    base.set_editor_property("constant", unreal.LinearColor(values[0], values[1], values[2], 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rough = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionConstant, -400, 200
    )
    rough.set_editor_property("r", 0.72)
    unreal.MaterialEditingLibrary.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    if emissive > 0.0:
        em = unreal.MaterialEditingLibrary.create_material_expression(
            mat, unreal.MaterialExpressionConstant3Vector, -400, 400
        )
        em.set_editor_property(
            "constant",
            unreal.LinearColor(values[0] * emissive, values[1] * emissive, values[2] * emissive, 1.0),
        )
        unreal.MaterialEditingLibrary.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    if save_enabled:
        unreal.EditorAssetLibrary.save_asset(path)
    return mat


def create_whitebox_materials(asset_dir: str = FRAMEWORK_DIR, save_enabled: bool = True) -> Dict[str, str]:
    specs = {
        "floor": ("M_GF_Neutral_Floor", (0.22, 0.23, 0.24), 0.0),
        "wall": ("M_GF_Neutral_Wall", (0.38, 0.39, 0.40), 0.0),
        "dark": ("M_GF_Dark_Obstacle", (0.08, 0.09, 0.10), 0.0),
        "key": ("M_GF_Key_Yellow", (1.0, 0.62, 0.08), 0.18),
        "locked": ("M_GF_Locked_Red", (0.72, 0.05, 0.03), 0.08),
        "powered": ("M_GF_Powered_Green", (0.05, 0.55, 0.18), 0.12),
        "finish": ("M_GF_Finish_Blue", (0.04, 0.26, 0.78), 0.10),
    }
    paths: Dict[str, str] = {}
    for key, spec in specs.items():
        name, color, emissive = spec
        mat = create_material(asset_dir, name, color, emissive, save_enabled=save_enabled)
        paths[key] = mat.get_path_name().split(".")[0]
    return paths


def _component_class(name: str) -> Any:
    import unreal

    cls = getattr(unreal, name, None)
    if cls is None:
        raise RuntimeError("Unknown component class {}".format(name))
    return cls


def _add_component_with_scs(bp: Any, comp_def: Dict[str, Any]) -> Optional[str]:
    try:
        scs = bp.get_editor_property("simple_construction_script")
        if scs is None:
            return "Blueprint has no simple construction script"
        node = scs.create_node(_component_class(comp_def["class"]))
        if node is None:
            return "create_node returned None for {}".format(comp_def["name"])
        template = node.get_editor_property("component_template")
        if template:
            template.rename(comp_def["name"])
        scs.add_node(node)
    except Exception as exc:
        return str(exc)
    return None


def _set_component_property(lib: Any, bp: Any, component_name: str, prop_name: str, value: str) -> Optional[str]:
    if lib is None:
        return "BlueprintGraphBuilderLibrary unavailable for {}.{}".format(component_name, prop_name)
    try:
        ok = lib.set_component_property(bp, component_name, prop_name, value)
        if not ok:
            return "set_component_property returned false for {}.{}".format(component_name, prop_name)
    except Exception as exc:
        return str(exc)
    return None


def _compile_blueprint(bp: Any) -> Dict[str, Any]:
    import unreal

    lib = getattr(unreal, "BlueprintGraphBuilderLibrary", None)
    if lib and hasattr(lib, "compile_and_report"):
        try:
            return json.loads(lib.compile_and_report(bp))
        except Exception as exc:
            return {"success": False, "error": str(exc)}
    try:
        unreal.EditorAssetLibrary.save_asset(bp.get_path_name(), only_if_is_dirty=False)
        gen_class = bp.get_editor_property("generated_class")
        return {"success": gen_class is not None, "generated_class": gen_class.get_name() if gen_class else None}
    except Exception as exc:
        return {"success": False, "error": str(exc)}


def build_graph_template(template: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    params = params or {}
    message = params.get("message", "Gameplay event fired")
    trigger_component = params.get("trigger_component", "InteractionTrigger")
    door_component = params.get("door_component", "DoorMesh")
    move_offset = params.get("move_offset", {"x": 0, "y": 0, "z": 220})
    delay_seconds = float(params.get("delay_seconds", 0.0))

    if template in ("overlap_print", "objective_trigger"):
        return {
            "nodes": [
                {"id": "overlap", "type": "ActorBeginOverlap", "x": 0, "y": 0},
                {
                    "id": "print",
                    "type": "PrintString",
                    "x": 300,
                    "y": 0,
                    "params": {"InString": message, "bPrintToScreen": True, "bPrintToLog": True, "Duration": 3.0},
                },
            ],
            "connections": [{"from": "overlap.exec", "to": "print.exec"}],
        }

    if template == "pickup_hide":
        return {
            "nodes": [
                {"id": "overlap", "type": "ActorBeginOverlap", "x": 0, "y": 0},
                {
                    "id": "print",
                    "type": "PrintString",
                    "x": 280,
                    "y": 0,
                    "params": {"InString": message, "bPrintToScreen": True, "bPrintToLog": True, "Duration": 3.0},
                },
                {
                    "id": "hide",
                    "type": "CallFunction",
                    "x": 560,
                    "y": 0,
                    "params": {"class": "Actor", "function": "SetActorHiddenInGame", "bNewHidden": True},
                },
                {
                    "id": "collision",
                    "type": "CallFunction",
                    "x": 840,
                    "y": 0,
                    "params": {"class": "Actor", "function": "SetActorEnableCollision", "bNewActorEnableCollision": False},
                },
            ],
            "connections": [
                {"from": "overlap.exec", "to": "print.exec"},
                {"from": "print.then", "to": "hide.exec"},
                {"from": "hide.then", "to": "collision.exec"},
            ],
        }

    if template == "locked_door_open":
        nodes = [
            {"id": "overlap", "type": "ComponentBeginOverlap", "x": 0, "y": 0, "params": {"component": trigger_component}},
            {
                "id": "print",
                "type": "PrintString",
                "x": 300,
                "y": 0,
                "params": {"InString": message, "bPrintToScreen": True, "bPrintToLog": True, "Duration": 3.0},
            },
        ]
        connections = [{"from": "overlap.exec", "to": "print.exec"}]
        previous = "print"
        if delay_seconds > 0:
            nodes.append({"id": "delay", "type": "Delay", "x": 580, "y": 0, "params": {"Duration": delay_seconds}})
            connections.append({"from": "print.then", "to": "delay.exec"})
            previous = "delay"
        nodes.extend(
            [
                {
                    "id": "move",
                    "type": "CallFunction",
                    "x": 860,
                    "y": 0,
                    "params": {
                        "class": "KismetSystemLibrary",
                        "function": "MoveComponentTo",
                        "component": door_component,
                        "target_relative_location": move_offset,
                        "over_time": float(params.get("open_time", 0.65)),
                    },
                },
                {
                    "id": "collision",
                    "type": "CallFunction",
                    "x": 1160,
                    "y": 0,
                    "params": {"class": "Actor", "function": "SetActorEnableCollision", "bNewActorEnableCollision": False},
                },
            ]
        )
        connections.extend(
            [
                {"from": previous + ".then", "to": "move.exec"},
                {"from": "move.then", "to": "collision.exec"},
            ]
        )
        return {"nodes": nodes, "connections": connections}

    if template == "input_event_debug":
        action_name = params.get("action_name", "Interact")
        return {
            "nodes": [
                {"id": "input", "type": "InputAction", "x": 0, "y": 0, "params": {"ActionName": action_name}},
                {
                    "id": "print",
                    "type": "PrintString",
                    "x": 300,
                    "y": 0,
                    "params": {"InString": message, "bPrintToScreen": True, "bPrintToLog": True, "Duration": 2.0},
                },
            ],
            "connections": [{"from": "input.pressed", "to": "print.exec"}],
        }

    if template == "finish_zone":
        return build_graph_template("overlap_print", {"message": message or "Objective chain complete"})

    raise ValueError("Unknown graph template {}".format(template))


def create_actor_blueprint(
    name: str,
    asset_dir: str,
    graph_template: Optional[str],
    graph_params: Optional[Dict[str, Any]],
    replace_existing: bool,
    compile_enabled: bool = True,
    save_enabled: bool = True,
) -> Dict[str, Any]:
    import unreal

    ensure_dir(asset_dir)
    path = asset_dir + "/" + name
    warnings: List[str] = []
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if replace_existing:
            from mcp_bridge import state
            blocked = state.blocked_response_for_key("allow_asset_delete", "gameplay_framework_create")
            if blocked is not None:
                return blocked
            unreal.EditorAssetLibrary.delete_asset(path)
        else:
            bp = unreal.EditorAssetLibrary.load_asset(path)
            compile_report = _compile_blueprint(bp) if compile_enabled else {"skipped": True}
            return {"name": name, "path": path, "created": False, "warnings": [], "compile": compile_report}

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, asset_dir, unreal.Blueprint, factory)
    if not bp:
        raise RuntimeError("Failed to create Blueprint {}".format(path))

    lib = getattr(unreal, "BlueprintGraphBuilderLibrary", None)
    components = BLUEPRINT_COMPONENTS.get(name, [])
    for comp_def in components:
        if lib:
            try:
                lib.add_component_to_blueprint(
                    bp,
                    _component_class(comp_def["class"]),
                    comp_def["name"],
                    comp_def.get("attach", ""),
                )
                continue
            except Exception as exc:
                warnings.append("GraphBuilder add component failed for {}: {}".format(comp_def["name"], exc))
        err = _add_component_with_scs(bp, comp_def)
        if err:
            warnings.append("SCS add component failed for {}: {}".format(comp_def["name"], err))

    cube_path = "StaticMesh'/Engine/BasicShapes/Cube.Cube'"
    component_defaults = {
        "VisualMesh": {"StaticMesh": cube_path, "RelativeScale3D": "(X=1,Y=1,Z=1)"},
        "PickupMesh": {"StaticMesh": cube_path, "RelativeLocation": "(X=0,Y=0,Z=80)", "RelativeScale3D": "(X=0.55,Y=0.55,Z=0.55)"},
        "DoorMesh": {"StaticMesh": cube_path, "RelativeLocation": "(X=0,Y=0,Z=140)", "RelativeScale3D": "(X=0.45,Y=3.2,Z=2.8)"},
        "LockPanel": {"StaticMesh": cube_path, "RelativeLocation": "(X=-80,Y=-210,Z=150)", "RelativeScale3D": "(X=0.18,Y=0.65,Z=0.9)"},
        "MarkerMesh": {"StaticMesh": cube_path, "RelativeLocation": "(X=0,Y=0,Z=15)", "RelativeScale3D": "(X=2,Y=2,Z=0.2)"},
        "FinishPad": {"StaticMesh": cube_path, "RelativeLocation": "(X=0,Y=0,Z=10)", "RelativeScale3D": "(X=2.4,Y=2.4,Z=0.2)"},
        "InteractionTrigger": {"BoxExtent": "(X=160,Y=160,Z=160)"},
        "PickupTrigger": {"BoxExtent": "(X=140,Y=140,Z=140)", "RelativeLocation": "(X=0,Y=0,Z=80)"},
        "UseTrigger": {"BoxExtent": "(X=180,Y=180,Z=180)", "RelativeLocation": "(X=-80,Y=-210,Z=150)"},
        "ObjectiveVolume": {"BoxExtent": "(X=220,Y=220,Z=160)"},
        "FinishVolume": {"BoxExtent": "(X=260,Y=260,Z=180)"},
        "LabelText": {"RelativeLocation": "(X=0,Y=0,Z=230)", "WorldSize": "34"},
        "StateMarker": {"RelativeLocation": "(X=0,Y=0,Z=120)", "WorldSize": "30"},
    }
    blueprint_component_names = set(c["name"] for c in components)
    for comp_name, props in component_defaults.items():
        if comp_name not in blueprint_component_names:
            continue
        for prop_name, value in props.items():
            err = _set_component_property(lib, bp, comp_name, prop_name, value)
            if err and lib:
                warnings.append(err)

    graph = None
    if graph_template:
        graph = build_graph_template(graph_template, graph_params)
        if lib:
            try:
                lib.build_blueprint_from_json(bp, json.dumps(graph), True)
            except Exception as exc:
                warnings.append("Graph build failed for {}: {}".format(name, exc))
        else:
            warnings.append("Graph template recorded but not applied because BlueprintGraphBuilderLibrary is unavailable")

    compile_report = _compile_blueprint(bp) if compile_enabled else {"skipped": True}
    if save_enabled:
        unreal.EditorAssetLibrary.save_asset(path)
    return {
        "name": name,
        "path": path,
        "created": True,
        "components": [c["name"] for c in components],
        "graph_template": graph_template,
        "graph_json": graph,
        "warnings": warnings,
        "compile": compile_report,
        "saved": save_enabled,
    }


def create_gameplay_framework(params: Dict[str, Any]) -> Dict[str, Any]:
    import unreal

    asset_dir = params.get("asset_dir", FRAMEWORK_DIR)
    replace_existing = bool(params.get("replace_existing", False))
    from mcp_bridge import state

    compile_enabled = bool(params.get("compile")) if "compile" in params else state.safety_enabled("auto_compile_blueprints")
    save_enabled = bool(params.get("save")) if "save" in params else state.safety_enabled("auto_save_after_operations")
    ensure_dir(asset_dir)
    blueprint_specs = [
        ("BP_InteractableBase", "overlap_print", {"message": "Interactable overlap"}),
        ("BP_KeyItemPickup", "pickup_hide", {"message": "Key item collected"}),
        ("BP_LockedDoor", "locked_door_open", {"message": "Door unlocked"}),
        ("BP_ObjectiveTrigger", "objective_trigger", {"message": "Objective advanced"}),
        ("BP_FinishZone", "finish_zone", {"message": "Finish zone reached"}),
        ("BP_LevelStateManager", None, {}),
    ]
    if replace_existing:
        existing_paths = [
            asset_dir + "/Blueprints/" + name
            for name, _template, _graph_params in blueprint_specs
            if unreal.EditorAssetLibrary.does_asset_exist(asset_dir + "/Blueprints/" + name)
        ]
        if existing_paths:
            from mcp_bridge import state
            blocked = state.blocked_response_for_key("allow_asset_delete", "gameplay_framework_create")
            if blocked is not None:
                blocked["data"]["asset_paths"] = existing_paths
                return blocked

    materials = create_whitebox_materials(asset_dir, save_enabled=save_enabled)
    blueprints = [
        create_actor_blueprint(name, asset_dir + "/Blueprints", template, graph_params, replace_existing, compile_enabled, save_enabled)
        for name, template, graph_params in blueprint_specs
    ]
    memory = load_design_memory()
    memory.setdefault("unreal_version", "4.27")
    memory.setdefault("scale", {"uu_per_meter": 100, "default_corridor_width": 500, "default_player_eye_height": 160})
    memory["gameplay_framework"] = {
        "asset_dir": asset_dir,
        "blueprints": {key: asset_dir + "/Blueprints/" + value for key, value in BLUEPRINT_ASSETS.items()},
        "materials": materials,
        "updated_utc": datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    memory_path = save_design_memory(memory)
    return success({"asset_dir": asset_dir, "materials": materials, "blueprints": blueprints, "design_memory": memory_path, "compiled": compile_enabled, "saved": save_enabled})


def _load_blueprint_class(asset_path: str) -> Any:
    import unreal

    class_name = asset_path.split("/")[-1]
    full_path = "{}.{}_C".format(asset_path, class_name)
    cls = unreal.load_class(None, full_path)
    if cls:
        return cls
    return unreal.EditorAssetLibrary.load_blueprint_class(asset_path)


def _find_actor(label: str) -> Optional[Any]:
    import unreal

    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        try:
            if actor.get_actor_label() == label:
                return actor
        except Exception:
            pass
    return None


def place_gameplay_actor(params: Dict[str, Any]) -> Dict[str, Any]:
    import unreal

    actor_type = params.get("type", "")
    if actor_type not in BLUEPRINT_ASSETS:
        return failure("Unknown gameplay actor type: {}. Available: {}".format(actor_type, sorted(BLUEPRINT_ASSETS)))

    asset_dir = params.get("asset_dir", FRAMEWORK_DIR)
    bp_path = params.get("blueprint_path") or asset_dir + "/Blueprints/" + BLUEPRINT_ASSETS[actor_type]
    cls = _load_blueprint_class(bp_path)
    if cls is None:
        return failure("Could not load Blueprint class {}. Run gameplay_framework_create first.".format(bp_path))

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        cls,
        vector(params.get("location"), (0, 0, 0)),
        rotator(params.get("rotation"), (0, 0, 0)),
    )
    if actor is None:
        return failure("Failed to spawn gameplay actor {}".format(actor_type))

    label = params.get("label") or BLUEPRINT_ASSETS[actor_type]
    actor.set_actor_label(str(label))
    folder = params.get("folder", "00_Gameplay")
    actor.set_folder_path(str(folder))
    if params.get("scale"):
        actor.set_actor_scale3d(vector(params.get("scale"), (1, 1, 1)))

    tags = list(params.get("tags", []))
    tags.append("MCPGameplay")
    tags.append(actor_type)
    try:
        actor.tags = [unreal.Name(str(t)) for t in tags]
    except Exception:
        pass

    return success(
        {
            "type": actor_type,
            "label": actor.get_actor_label(),
            "blueprint_path": bp_path,
            "location": vector_dict(actor.get_actor_location()),
            "rotation": rotator_dict(actor.get_actor_rotation()),
            "folder": str(actor.get_folder_path()),
        }
    )


def apply_whitebox_lighting(params: Dict[str, Any]) -> Dict[str, Any]:
    import unreal

    max_point_intensity = float(params.get("max_point_intensity", 1400.0))
    max_radius = float(params.get("max_attenuation_radius", 900.0))
    clamp_existing = bool(params.get("clamp_existing", True))
    changed_lights: List[Dict[str, Any]] = []

    if clamp_existing:
        for actor in unreal.EditorLevelLibrary.get_all_level_actors():
            for comp in actor.get_components_by_class(unreal.PointLightComponent):
                changed: Dict[str, Any] = {"actor": actor.get_actor_label()}
                try:
                    intensity = float(comp.get_editor_property("intensity"))
                    if intensity > max_point_intensity:
                        comp.set_editor_property("intensity", max_point_intensity)
                        changed["intensity"] = {"from": intensity, "to": max_point_intensity}
                except Exception:
                    pass
                try:
                    radius = float(comp.get_editor_property("attenuation_radius"))
                    if radius > max_radius:
                        comp.set_editor_property("attenuation_radius", max_radius)
                        changed["attenuation_radius"] = {"from": radius, "to": max_radius}
                except Exception:
                    pass
                if len(changed) > 1:
                    changed_lights.append(changed)

    label = params.get("post_process_label", "PP_MCP_Whitebox_Exposure")
    pp = _find_actor(label)
    if pp is None:
        pp = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.PostProcessVolume, vector(params.get("location"), (0, 0, 160)))
        pp.set_actor_label(label)
        pp.set_folder_path("03_Lighting")
    pp.set_editor_property("unbound", True)
    settings = pp.get_editor_property("settings")
    values = {
        "override_auto_exposure_min_brightness": True,
        "override_auto_exposure_max_brightness": True,
        "auto_exposure_min_brightness": float(params.get("auto_exposure_min_brightness", 0.85)),
        "auto_exposure_max_brightness": float(params.get("auto_exposure_max_brightness", 1.05)),
        "override_bloom_intensity": True,
        "bloom_intensity": float(params.get("bloom_intensity", 0.12)),
    }
    applied = []
    for prop, value in values.items():
        try:
            settings.set_editor_property(prop, value)
            applied.append(prop)
        except Exception:
            pass
    pp.set_editor_property("settings", settings)
    mats = create_whitebox_materials(params.get("asset_dir", FRAMEWORK_DIR))
    return success({"post_process": label, "applied_settings": applied, "clamped_lights": changed_lights, "materials": mats})


def _point_from_param(value: Any) -> Optional[Any]:
    if isinstance(value, str):
        actor = _find_actor(value)
        if actor:
            return actor.get_actor_location()
        return None
    return vector(value, (0, 0, 0))


def analyze_route(params: Dict[str, Any]) -> Dict[str, Any]:
    import unreal

    points = params.get("points", [])
    if len(points) < 2:
        return failure("Route analysis needs at least two points or actor labels.")

    resolved = []
    missing = []
    for point in points:
        loc = _point_from_param(point)
        if loc is None:
            missing.append(point)
        else:
            resolved.append(loc)
    if missing:
        return failure("Could not resolve route points: {}".format(missing), {"missing": missing})

    world = unreal.EditorLevelLibrary.get_editor_world()
    segments = []
    for index in range(len(resolved) - 1):
        start = resolved[index]
        end = resolved[index + 1]
        direct_distance = math.sqrt((start.x - end.x) ** 2 + (start.y - end.y) ** 2 + (start.z - end.z) ** 2)
        nav_status = "unknown"
        nav_length = None
        try:
            nav_path = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, start, end)
            if nav_path and nav_path.is_valid():
                points_on_path = nav_path.path_points
                nav_status = "reachable"
                nav_length = 0.0
                for path_index in range(len(points_on_path) - 1):
                    a = points_on_path[path_index]
                    b = points_on_path[path_index + 1]
                    nav_length += math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)
            else:
                nav_status = "unreachable"
        except Exception as exc:
            nav_status = "nav_query_failed: {}".format(exc)

        segments.append(
            {
                "from": vector_dict(start),
                "to": vector_dict(end),
                "direct_distance": round(direct_distance, 2),
                "nav_status": nav_status,
                "nav_length": round(nav_length, 2) if nav_length is not None else None,
            }
        )

    issues = [s for s in segments if s["nav_status"] not in ("reachable", "unknown")]
    return success({"segments": segments, "issue_count": len(issues), "issues": issues})


def validate_objective_chain(params: Dict[str, Any]) -> Dict[str, Any]:
    import unreal

    chain = params.get("chain", [])
    if not chain:
        chain = [
            {"type": "key_item_pickup"},
            {"type": "locked_door"},
            {"type": "finish_zone"},
        ]
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    actor_rows = []
    issues = []
    for step in chain:
        label = step.get("label")
        step_type = step.get("type")
        candidates = []
        for actor in actors:
            actor_label = actor.get_actor_label()
            actor_class = actor.get_class().get_name()
            actor_tags = [str(t) for t in getattr(actor, "tags", [])]
            if label and actor_label == label:
                candidates.append(actor)
            elif step_type and (step_type in actor_tags or BLUEPRINT_ASSETS.get(step_type, "") in actor_class):
                candidates.append(actor)
        if not candidates:
            issues.append({"type": "missing_step", "step": step})
            actor_rows.append({"step": step, "found": False})
            continue
        actor = candidates[0]
        trigger_count = len(actor.get_components_by_class(unreal.BoxComponent))
        mesh_count = len(actor.get_components_by_class(unreal.StaticMeshComponent))
        actor_rows.append(
            {
                "step": step,
                "found": True,
                "actor": actor.get_actor_label(),
                "class": actor.get_class().get_name(),
                "trigger_count": trigger_count,
                "mesh_count": mesh_count,
            }
        )
        if step_type != "level_state_manager" and trigger_count == 0:
            issues.append({"type": "missing_trigger", "actor": actor.get_actor_label()})

    return success({"steps": actor_rows, "issue_count": len(issues), "issues": issues, "passed": len(issues) == 0})


def validate_lighting(params: Dict[str, Any]) -> Dict[str, Any]:
    import unreal

    max_point_intensity = float(params.get("max_point_intensity", 1800.0))
    max_radius = float(params.get("max_attenuation_radius", 1000.0))
    issues = []
    point_lights = []
    post_process_volumes = []
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        for comp in actor.get_components_by_class(unreal.PointLightComponent):
            try:
                intensity = float(comp.get_editor_property("intensity"))
                radius = float(comp.get_editor_property("attenuation_radius"))
            except Exception:
                intensity = 0.0
                radius = 0.0
            row = {"actor": actor.get_actor_label(), "intensity": intensity, "attenuation_radius": radius}
            point_lights.append(row)
            if intensity > max_point_intensity:
                issues.append({"type": "overbright_point_light", "actor": actor.get_actor_label(), "intensity": intensity})
            if radius > max_radius:
                issues.append({"type": "wide_point_light", "actor": actor.get_actor_label(), "attenuation_radius": radius})
        if actor.get_class().get_name() == "PostProcessVolume":
            post_process_volumes.append(actor.get_actor_label())

    if not post_process_volumes:
        issues.append({"type": "missing_exposure_clamp", "message": "No PostProcessVolume found"})

    return success(
        {
            "point_lights": point_lights,
            "post_process_volumes": post_process_volumes,
            "issue_count": len(issues),
            "issues": issues,
            "passed": len(issues) == 0,
        }
    )


def validate_label_visibility(params: Dict[str, Any]) -> Dict[str, Any]:
    import unreal

    eye = vector(params.get("eye_location"), (0, 0, 160))
    max_distance = float(params.get("max_distance", 1800.0))
    labels = []
    issues = []
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        components = actor.get_components_by_class(unreal.TextRenderComponent)
        if actor.get_class().get_name() == "TextRenderActor":
            text_comp = actor.get_component_by_class(unreal.TextRenderComponent)
            components = components or ([text_comp] if text_comp else [])
        for comp in components:
            try:
                loc = comp.get_world_location()
            except Exception:
                loc = actor.get_actor_location()
            distance = math.sqrt((loc.x - eye.x) ** 2 + (loc.y - eye.y) ** 2 + (loc.z - eye.z) ** 2)
            row = {"actor": actor.get_actor_label(), "distance": round(distance, 2), "location": vector_dict(loc)}
            labels.append(row)
            if distance > max_distance:
                issues.append({"type": "label_too_far", "actor": actor.get_actor_label(), "distance": round(distance, 2)})
    if not labels:
        issues.append({"type": "no_labels_found"})
    return success({"labels": labels, "issue_count": len(issues), "issues": issues, "passed": len(issues) == 0})


def run_gameplay_validation(params: Dict[str, Any]) -> Dict[str, Any]:
    checks = params.get("checks", ["objective_chain", "lighting", "labels"])
    data: Dict[str, Any] = {}
    if "objective_chain" in checks:
        data["objective_chain"] = validate_objective_chain(params.get("objective_chain", {}))["data"]
    if "lighting" in checks:
        data["lighting"] = validate_lighting(params.get("lighting", {}))["data"]
    if "labels" in checks:
        data["labels"] = validate_label_visibility(params.get("labels", {}))["data"]
    if "route" in checks:
        data["route"] = analyze_route(params.get("route", {}))["data"]
    total_issues = sum(v.get("issue_count", 0) for v in data.values())
    data["passed"] = total_issues == 0
    data["issue_count"] = total_issues
    return success(data)


def get_level_grammar(params: Dict[str, Any]) -> Dict[str, Any]:
    name = params.get("name")
    if name:
        grammar = LEVEL_GRAMMARS.get(name)
        if not grammar:
            return failure("Unknown level grammar {}. Available: {}".format(name, sorted(LEVEL_GRAMMARS)))
        return success({"name": name, "grammar": grammar})
    return success({"grammars": LEVEL_GRAMMARS})
