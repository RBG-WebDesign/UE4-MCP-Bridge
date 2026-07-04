# blueprint_generator.py -- Drive blueprint_create and BlueprintGraphBuilderLibrary from BlueprintSpec.
from __future__ import annotations
import json
from typing import Any, Dict, List, Tuple

from mcp_bridge.generation.spec_schema import BlueprintSpec


# Map simple parent class names to the unreal module attribute name
_PARENT_ALIASES: Dict[str, str] = {
    "Actor": "Actor",
    "Pawn": "Pawn",
    "Character": "Character",
    "PlayerController": "PlayerController",
    "GameModeBase": "GameModeBase",
    "GameMode": "GameMode",
    "GameStateBase": "GameStateBase",
    "GameState": "GameState",
    "HUD": "HUD",
    "AIController": "AIController",
    "GameInstance": "GameInstance",
    "ActorComponent": "ActorComponent",
    "SceneComponent": "SceneComponent",
    "SaveGame": "SaveGame",
    "PlayerState": "PlayerState",
}


def _resolve_parent_class(parent_name: str) -> Any:
    """Resolve a parent class name to a UClass. Falls back to Actor."""
    try:
        import unreal
        cls = getattr(unreal, parent_name, None)
        if cls is not None:
            return cls
        mapped = _PARENT_ALIASES.get(parent_name)
        if mapped:
            cls = getattr(unreal, mapped, None)
            if cls is not None:
                return cls
        return unreal.Actor
    except Exception:
        return None


def generate_blueprint(spec: BlueprintSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a Blueprint asset from a BlueprintSpec.

    Returns (success, error_message, result_data).
    Must run on the UE4 game thread.
    """
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"

        # Skip if already exists
        existing = unreal.EditorAssetLibrary.find_asset_data(full_path)
        if existing and existing.is_valid():
            return True, "", {"path": full_path, "skipped": True}

        # Ensure content directory exists
        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        # Resolve parent class
        parent_class = _resolve_parent_class(spec.parent_class)
        if parent_class is None:
            return False, f"Cannot resolve parent class: {spec.parent_class}", {}

        # Create asset
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent_class)
        bp = asset_tools.create_asset(spec.name, spec.content_path, unreal.Blueprint, factory)

        if bp is None:
            return False, f"Failed to create Blueprint: {full_path}", {}

        # Add components
        components_added: List[str] = []
        if spec.components:
            try:
                scs = bp.get_editor_property("simple_construction_script")
                if scs:
                    for comp_def in spec.components:
                        comp_class_name = comp_def.get("class", "")
                        comp_name = comp_def.get("name", "")
                        if not comp_class_name:
                            continue
                        comp_class = getattr(unreal, comp_class_name, None)
                        if comp_class is None:
                            continue
                        node = scs.create_node(comp_class)
                        if node:
                            template = node.get_editor_property("component_template")
                            if template and comp_name:
                                template.rename(comp_name)
                            components_added.append(comp_name or comp_class_name)
            except Exception:
                pass

        # Build graph if spec has one
        if spec.graph_json:
            try:
                lib = getattr(unreal, "BlueprintGraphBuilderLibrary", None)
                if lib:
                    json_str = json.dumps(spec.graph_json)
                    lib.build_blueprint_from_json(bp, json_str, True)
            except Exception:
                pass  # graph failure is non-fatal

        # Save (triggers BP compilation on the game thread; KismetSystemLibrary
        # is not exposed in UE4.27 Python bindings)
        try:
            unreal.EditorAssetLibrary.save_asset(bp.get_path_name())
        except Exception:
            pass

        return True, "", {
            "path": full_path,
            "components_added": components_added,
            "skipped": False,
        }

    except Exception as e:
        return False, str(e), {}


def generate_all_blueprints(specs: List[BlueprintSpec]) -> Dict[str, Any]:
    """Generate all blueprints from a list of specs. Returns summary."""
    results = []
    errors: List[str] = []
    for spec in specs:
        ok, err, data = generate_blueprint(spec)
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
