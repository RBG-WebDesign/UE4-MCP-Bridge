# localization_generator.py -- Generate StringTable assets.
from __future__ import annotations
from typing import Any, Dict, List, Tuple

from mcp_bridge.generation.spec_schema import StringTableSpec


def generate_string_table(spec: StringTableSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a StringTable asset and populate entries."""
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        factory_cls = getattr(unreal, "StringTableFactory", None)
        if factory_cls is None:
            return False, "StringTableFactory not available", {}

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        # StringTable class is not directly accessible as unreal.StringTable in UE4.27;
        # pass None and let StringTableFactory determine the class.
        st = asset_tools.create_asset(spec.name, spec.content_path, None, factory_cls())
        if st is None:
            return False, f"Failed to create StringTable: {full_path}", {}

        # StringTable entries are not writable via Python API in UE4.27.
        # The asset is created as a stub; populate entries via CSV import or editor.
        unreal.EditorAssetLibrary.save_asset(full_path)
        return True, "", {
            "path": full_path,
            "namespace": spec.namespace,
            "entries_defined": len(spec.entries),
            "note": "Entries must be populated via CSV import or editor (Python API is read-only for StringTable entries in UE4.27)",
            "skipped": False,
        }

    except Exception as e:
        return False, str(e), {}


def generate_all_string_tables(specs: List[StringTableSpec]) -> Dict[str, Any]:
    results, errors = [], []
    for spec in specs:
        ok, err, data = generate_string_table(spec)
        entry: Dict[str, Any] = {"name": spec.name, "success": ok, "data": data}
        if err:
            entry["error"] = err
            errors.append(f"{spec.name}: {err}")
        results.append(entry)
    return {
        "results": results, "errors": errors,
        "total": len(specs), "succeeded": sum(1 for r in results if r["success"]),
    }
