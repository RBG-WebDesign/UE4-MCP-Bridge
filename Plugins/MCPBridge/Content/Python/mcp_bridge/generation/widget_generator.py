# widget_generator.py -- Drive WidgetBlueprintBuilderLibrary from WidgetSpec.
from __future__ import annotations
import json
from typing import Any, Dict, List, Tuple

from mcp_bridge.generation.spec_schema import WidgetSpec


def generate_widget(spec: WidgetSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a Widget Blueprint from a WidgetSpec.

    Calls WidgetBlueprintBuilderLibrary.build_widget_from_json.
    Must run on the UE4 game thread.
    """
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"

        # Skip if already exists
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        # Ensure directory exists
        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        lib = getattr(unreal, "WidgetBlueprintBuilderLibrary", None)
        if lib is None:
            return False, "WidgetBlueprintBuilderLibrary not available -- is BlueprintGraphBuilder plugin loaded?", {}

        json_str = json.dumps({"root": spec.root_widget})

        # Returns empty string on success, error message on failure
        err_out = lib.build_widget_from_json(
            spec.content_path,
            spec.name,
            json_str,
        )

        if err_out == "":
            return True, "", {"path": full_path, "skipped": False}
        else:
            return False, str(err_out) or "build_widget_from_json returned error", {}

    except Exception as e:
        return False, str(e), {}


def generate_all_widgets(specs: List[WidgetSpec]) -> Dict[str, Any]:
    """Generate all widgets from a list of specs. Returns summary."""
    results = []
    errors: List[str] = []
    for spec in specs:
        ok, err, data = generate_widget(spec)
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
