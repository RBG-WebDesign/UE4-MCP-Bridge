# ai_generator.py -- Generate Blackboard, BehaviorTree, and EQS Query assets.
from __future__ import annotations
from typing import Any, Dict, List, Tuple

from mcp_bridge.generation.spec_schema import BlackboardSpec, BehaviorTreeSpec, EQSQuerySpec


# Map spec key type strings to UE4 Python Blackboard key class names
_BB_KEY_TYPES: Dict[str, str] = {
    "Bool":   "BlackboardKeyType_Bool",
    "Float":  "BlackboardKeyType_Float",
    "Int":    "BlackboardKeyType_Int",
    "Name":   "BlackboardKeyType_Name",
    "Object": "BlackboardKeyType_Object",
    "Vector": "BlackboardKeyType_Vector",
    "Enum":   "BlackboardKeyType_Enum",
    "String": "BlackboardKeyType_String",
}


def generate_blackboard(spec: BlackboardSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a Blackboard asset from a BlackboardSpec."""
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        factory_cls = getattr(unreal, "BlackboardDataFactory", None)
        if factory_cls is None:
            return False, "BlackboardDataFactory not available", {}

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        bb = asset_tools.create_asset(spec.name, spec.content_path, unreal.BlackboardData, factory_cls())
        if bb is None:
            return False, f"Failed to create BlackboardData: {full_path}", {}

        keys_added: List[str] = []
        for key_def in spec.keys:
            key_name = key_def.get("name", "")
            key_type = key_def.get("type", "Object")
            if not key_name:
                continue
            key_type_cls = getattr(unreal, _BB_KEY_TYPES.get(key_type, "BlackboardKeyType_Object"), None)
            if key_type_cls is None:
                continue
            try:
                # Add key via editor property path on BlackboardData
                existing_keys = bb.get_editor_property("keys")
                new_key = unreal.BlackboardEntry()
                new_key.set_editor_property("entry_name", key_name)
                new_key.set_editor_property("key_type", key_type_cls())
                existing_keys.append(new_key)
                bb.set_editor_property("keys", existing_keys)
                keys_added.append(key_name)
            except Exception:
                pass  # key add is best-effort

        unreal.EditorAssetLibrary.save_asset(full_path)
        return True, "", {"path": full_path, "keys_added": keys_added, "skipped": False}

    except Exception as e:
        return False, str(e), {}


def generate_behavior_tree(spec: BehaviorTreeSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a BehaviorTree asset, assign its Blackboard, and build node graph."""
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        factory_cls = getattr(unreal, "BehaviorTreeFactory", None)
        if factory_cls is None:
            return False, "BehaviorTreeFactory not available", {}

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        bt = asset_tools.create_asset(spec.name, spec.content_path, unreal.BehaviorTree, factory_cls())
        if bt is None:
            return False, f"Failed to create BehaviorTree: {full_path}", {}

        # Assign blackboard BEFORE building nodes (builder needs it for key validation)
        blackboard_assigned = False
        if spec.blackboard_path:
            bb = unreal.EditorAssetLibrary.load_asset(spec.blackboard_path)
            if bb:
                try:
                    bt.set_editor_property("blackboard_asset", bb)
                    blackboard_assigned = True
                except Exception:
                    pass

        # Build node graph via C++ plugin
        graph_built = False
        builder_available = False
        build_error = ""

        if isinstance(spec.root, dict) and "type" in spec.root:
            import json as json_mod
            lib = getattr(unreal, "BehaviorTreeBuilderLibrary", None)
            if lib and hasattr(lib, "build_behavior_tree_from_json"):
                builder_available = True
                try:
                    json_str = json_mod.dumps({"root": spec.root})
                    build_error = lib.build_behavior_tree_from_json(bt, json_str)
                    graph_built = (build_error == "")
                except Exception as e:
                    build_error = str(e)

        success = graph_built or not builder_available
        if build_error and builder_available:
            success = False

        unreal.EditorAssetLibrary.save_asset(full_path)
        return success, build_error, {
            "path": full_path,
            "blackboard": spec.blackboard_path,
            "blackboard_assigned": blackboard_assigned,
            "builder_available": builder_available,
            "graph_built": graph_built,
            "skipped": False,
        }

    except Exception as e:
        return False, str(e), {}


def generate_eqs_query(spec: EQSQuerySpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create an EnvQuery (EQS) asset."""
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        # EnvQuery has no dedicated factory in UE4.27 Python -- pass None
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        eq = asset_tools.create_asset(spec.name, spec.content_path, unreal.EnvQuery, None)
        if eq is None:
            return False, f"Failed to create EnvQuery: {full_path}", {}

        unreal.EditorAssetLibrary.save_asset(full_path)
        return True, "", {"path": full_path, "generator_type": spec.generator_type, "skipped": False}

    except Exception as e:
        return False, str(e), {}


def _batch(items: list, fn) -> Dict[str, Any]:
    results, errors = [], []
    for item in items:
        ok, err, data = fn(item)
        entry: Dict[str, Any] = {"name": item.name, "success": ok, "data": data}
        if err:
            entry["error"] = err
            errors.append(f"{item.name}: {err}")
        results.append(entry)
    return {
        "results": results, "errors": errors,
        "total": len(items), "succeeded": sum(1 for r in results if r["success"]),
    }


def generate_all_blackboards(specs: List[BlackboardSpec]) -> Dict[str, Any]:
    return _batch(specs, generate_blackboard)


def generate_all_behavior_trees(specs: List[BehaviorTreeSpec]) -> Dict[str, Any]:
    return _batch(specs, generate_behavior_tree)


def generate_all_eqs_queries(specs: List[EQSQuerySpec]) -> Dict[str, Any]:
    return _batch(specs, generate_eqs_query)
