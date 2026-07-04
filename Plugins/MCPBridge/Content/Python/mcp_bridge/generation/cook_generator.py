# cook_generator.py -- Generate PrimaryAssetLabel assets for cook/chunk rules.
from __future__ import annotations
from typing import Any, Dict, List, Tuple

from mcp_bridge.generation.spec_schema import PrimaryAssetLabelSpec


def generate_primary_asset_label(spec: PrimaryAssetLabelSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a PrimaryAssetLabel asset for Asset Manager cook rules."""
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        label_cls = getattr(unreal, "PrimaryAssetLabel", None)
        factory_cls = getattr(unreal, "BlueprintFactory", None)
        if label_cls is None:
            return False, "PrimaryAssetLabel not available", {}

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

        # PrimaryAssetLabel is a DataAsset subclass, use DataAssetFactory
        da_factory_cls = getattr(unreal, "DataAssetFactory", None)
        if da_factory_cls is None:
            return False, "DataAssetFactory not available", {}

        factory = da_factory_cls()
        factory.set_editor_property("data_asset_class", label_cls)
        label = asset_tools.create_asset(spec.name, spec.content_path, label_cls, factory)

        if label is None:
            return False, f"Failed to create PrimaryAssetLabel: {full_path}", {}

        try:
            label.set_editor_property("priority", spec.priority)
            label.set_editor_property("chunk_id", spec.chunk_id)
        except Exception:
            pass

        unreal.EditorAssetLibrary.save_asset(full_path)
        return True, "", {
            "path": full_path,
            "priority": spec.priority,
            "chunk_id": spec.chunk_id,
            "skipped": False,
        }

    except Exception as e:
        return False, str(e), {}


def generate_all_primary_asset_labels(specs: List[PrimaryAssetLabelSpec]) -> Dict[str, Any]:
    results, errors = [], []
    for spec in specs:
        ok, err, data = generate_primary_asset_label(spec)
        entry: Dict[str, Any] = {"name": spec.name, "success": ok, "data": data}
        if err:
            entry["error"] = err
            errors.append(f"{spec.name}: {err}")
        results.append(entry)
    return {
        "results": results, "errors": errors,
        "total": len(specs), "succeeded": sum(1 for r in results if r["success"]),
    }
