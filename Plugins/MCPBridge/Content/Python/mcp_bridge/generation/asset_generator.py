# asset_generator.py -- Create materials, data assets, enums, structs, curves from spec.
from __future__ import annotations
from typing import Any, Dict, List, Tuple

from mcp_bridge.generation.spec_schema import MaterialSpec, DataAssetSpec


def generate_material(spec: MaterialSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a simple flat-color material asset.

    Uses MaterialFactoryNew. Attempts to set a constant base color expression.
    If MaterialEditingLibrary is not available, the blank material is still created.
    """
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"

        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        factory = unreal.MaterialFactoryNew()
        mat = asset_tools.create_asset(spec.name, spec.content_path, unreal.Material, factory)

        if mat is None:
            return False, f"Failed to create material: {full_path}", {}

        # Try to set base color
        if spec.base_color:
            r, g, b, a = spec.base_color
            try:
                mel = getattr(unreal, "MaterialEditingLibrary", None)
                if mel:
                    expr = mel.create_material_expression(
                        mat, unreal.MaterialExpressionConstant4Vector, -400, 0
                    )
                    if expr:
                        expr.set_editor_property(
                            "constant", unreal.LinearColor(r, g, b, a)
                        )
                        mel.connect_material_property(
                            expr, "rgba", unreal.MaterialProperty.MP_BASE_COLOR
                        )
                        mel.recompile_material(mat)
            except Exception:
                pass  # material still exists, just without the color node

        try:
            unreal.EditorAssetLibrary.save_asset(full_path)
        except Exception:
            pass

        return True, "", {"path": full_path, "skipped": False}

    except Exception as e:
        return False, str(e), {}


def generate_data_asset(spec: DataAssetSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a stub data asset, curve, or DataAsset-fallback for Enum/Struct/DataTable."""
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"

        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

        if spec.asset_type == "CurveFloat":
            factory = unreal.CurveFloatFactory()
            asset = asset_tools.create_asset(
                spec.name, spec.content_path, unreal.CurveFloat, factory
            )
        elif spec.asset_type == "DataAsset":
            factory = unreal.DataAssetFactory()
            asset = asset_tools.create_asset(
                spec.name, spec.content_path, unreal.DataAsset, factory
            )
        else:
            # Enum, Struct, DataTable -- use DataAsset stub
            # These types require Python-inaccessible factories in 4.27.
            # A DataAsset placeholder preserves the content path slot.
            factory = unreal.DataAssetFactory()
            asset = asset_tools.create_asset(
                spec.name, spec.content_path, unreal.DataAsset, factory
            )

        if asset is None:
            return False, f"Failed to create {spec.asset_type}: {full_path}", {}

        try:
            unreal.EditorAssetLibrary.save_asset(full_path)
        except Exception:
            pass

        return True, "", {
            "path": full_path,
            "type": spec.asset_type,
            "skipped": False,
        }

    except Exception as e:
        return False, str(e), {}


def generate_all_materials(specs: List[MaterialSpec]) -> Dict[str, Any]:
    results = []
    errors: List[str] = []
    for spec in specs:
        ok, err, data = generate_material(spec)
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


def generate_all_data_assets(specs: List[DataAssetSpec]) -> Dict[str, Any]:
    results = []
    errors: List[str] = []
    for spec in specs:
        ok, err, data = generate_data_asset(spec)
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
