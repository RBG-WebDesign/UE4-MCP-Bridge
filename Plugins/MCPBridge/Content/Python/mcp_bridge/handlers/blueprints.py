"""Blueprint system handlers.

Handles creating, listing, inspecting, compiling, and documenting
Blueprint assets in the UE4 editor.

Blueprint variable creation limitations in 4.27:
- The new_variables array on Blueprint objects may be read-only
- FBPVariableDescription construction may not be fully exposed to Python
- The handler degrades gracefully: creates the Blueprint and components,
  reports variables as skipped if the API doesn't support it
"""

import fnmatch
import os
from typing import Any, Dict, List, Optional
from mcp_bridge.utils.transactions import transactional


# Common parent class name -> module path mapping
_PARENT_CLASS_MAP: Dict[str, str] = {
    "Actor": "Actor",
    "Pawn": "Pawn",
    "Character": "Character",
    "PlayerController": "PlayerController",
    "GameModeBase": "GameModeBase",
    "GameMode": "GameMode",
    "ActorComponent": "ActorComponent",
    "SceneComponent": "SceneComponent",
    "HUD": "HUD",
    "PlayerState": "PlayerState",
    "GameStateBase": "GameStateBase",
}


def _resolve_parent_class(class_name: str) -> Any:
    """Resolve a parent class name to a UClass object."""
    import unreal

    # Try direct attribute lookup on unreal module
    resolved = getattr(unreal, class_name, None)
    if resolved is not None:
        return resolved

    # Try the known mapping
    mapped = _PARENT_CLASS_MAP.get(class_name)
    if mapped and mapped != class_name:
        resolved = getattr(unreal, mapped, None)
        if resolved is not None:
            return resolved

    return None


def _extract_components(bp: Any) -> List[Dict[str, Any]]:
    """Extract component info from a Blueprint's SimpleConstructionScript."""
    components: List[Dict[str, Any]] = []
    try:
        scs = bp.get_editor_property("simple_construction_script")
        if scs is None:
            return components
        nodes = scs.get_all_nodes()
        # Find the root node
        root_nodes = scs.get_editor_property("root_nodes") if hasattr(scs, "get_editor_property") else []

        for node in nodes:
            template = node.get_editor_property("component_template")
            if template is None:
                continue

            parent_node = node.get_editor_property("parent_component_or_variable_name")
            is_root = False
            try:
                is_root = node in root_nodes
            except Exception:
                pass

            comp_info: Dict[str, Any] = {
                "name": template.get_name(),
                "class": template.get_class().get_name(),
                "parent": str(parent_node) if parent_node and str(parent_node) != "None" else None,
                "is_root": is_root,
            }
            components.append(comp_info)
    except Exception:
        pass
    return components


def _extract_variables(bp: Any) -> List[Dict[str, Any]]:
    """Extract variable info from a Blueprint."""
    variables: List[Dict[str, Any]] = []
    try:
        new_vars = bp.get_editor_property("new_variables")
        if new_vars is None:
            return variables
        for var_desc in new_vars:
            var_info: Dict[str, Any] = {
                "name": str(var_desc.get_editor_property("var_name")),
                "is_editable": bool(var_desc.get_editor_property("property_flags") & 0x0000000000000004) if hasattr(var_desc, "get_editor_property") else True,
            }
            # Try to get type info
            try:
                pin_type = var_desc.get_editor_property("var_type")
                if pin_type:
                    var_info["type"] = str(pin_type.get_editor_property("pin_category"))
            except Exception:
                var_info["type"] = "unknown"

            # Try to get category
            try:
                var_info["category"] = str(var_desc.get_editor_property("category"))
            except Exception:
                var_info["category"] = ""

            # Try to get tooltip
            try:
                tooltip = var_desc.get_editor_property("tool_tip")
                var_info["tooltip"] = str(tooltip) if tooltip else ""
            except Exception:
                var_info["tooltip"] = ""

            variables.append(var_info)
    except Exception:
        pass
    return variables


def _extract_functions(bp: Any) -> List[Dict[str, Any]]:
    """Extract function graph info from a Blueprint."""
    functions: List[Dict[str, Any]] = []
    try:
        func_graphs = bp.get_editor_property("function_graphs")
        if func_graphs is None:
            return functions
        for graph in func_graphs:
            func_info: Dict[str, Any] = {
                "name": graph.get_name(),
                "inputs": [],
                "outputs": [],
                "is_pure": False,
            }
            functions.append(func_info)
    except Exception:
        pass
    return functions


def _extract_event_graphs(bp: Any) -> List[str]:
    """Extract event graph names from a Blueprint."""
    graphs: List[str] = []
    try:
        uber_graphs = bp.get_editor_property("uber_graphs")
        if uber_graphs:
            for graph in uber_graphs:
                graphs.append(graph.get_name())
    except Exception:
        pass
    return graphs


def _get_parent_chain(bp: Any) -> List[str]:
    """Walk the parent class chain."""
    chain: List[str] = []
    try:
        current = bp.get_editor_property("parent_class")
        while current is not None:
            name = current.get_name()
            if name == "Object":
                break
            chain.append(name)
            current = current.get_editor_property("super_class") if hasattr(current, "get_editor_property") else None
    except Exception:
        pass
    return chain


def _is_blueprint_compiled(bp: Any) -> bool:
    """Check if a Blueprint is compiled (has a valid generated class)."""
    try:
        gen_class = bp.get_editor_property("generated_class")
        return gen_class is not None
    except Exception:
        return False


def _compile_blueprint_ue427_safe(bp: Any, bp_path: str = "") -> Dict[str, Any]:
    """Compile or save a Blueprint using APIs that exist in UE4.27 Python."""
    try:
        import unreal
        import json

        errors: List[str] = []
        method = "save_asset"
        report: Dict[str, Any] = {}

        builder_lib = getattr(unreal, "BlueprintGraphBuilderLibrary", None)
        compile_and_report = getattr(builder_lib, "compile_and_report", None) if builder_lib else None
        if compile_and_report is not None:
            try:
                raw_report = compile_and_report(bp)
                method = "BlueprintGraphBuilderLibrary.compile_and_report"
                if raw_report:
                    try:
                        report = json.loads(str(raw_report))
                    except Exception:
                        report = {"raw": str(raw_report)}
                path_to_save = bp_path or bp.get_path_name()
                unreal.EditorAssetLibrary.save_asset(path_to_save, only_if_is_dirty=False)
                return {
                    "compiled": bool(report.get("success", True)),
                    "had_errors": bool(report.get("errors")),
                    "errors": list(report.get("errors", [])),
                    "warnings": list(report.get("warnings", [])),
                    "status": report.get("status", "unknown"),
                    "method": method,
                    "report": report,
                }
            except Exception as e:
                errors.append(str(e))

        try:
            compile_fn = getattr(getattr(unreal, "KismetSystemLibrary", None), "compile_blueprint", None)
            if compile_fn is not None:
                compile_fn(bp)
                method = "KismetSystemLibrary.compile_blueprint"
                path_to_save = bp_path or bp.get_path_name()
                unreal.EditorAssetLibrary.save_asset(path_to_save, only_if_is_dirty=False)
            else:
                path_to_save = bp_path or bp.get_path_name()
                unreal.EditorAssetLibrary.save_asset(path_to_save, only_if_is_dirty=False)
        except Exception as e:
            errors.append(str(e))
            try:
                path_to_save = bp_path or bp.get_path_name()
                unreal.EditorAssetLibrary.save_asset(path_to_save, only_if_is_dirty=False)
                method = "save_asset_after_compile_fallback"
            except Exception as save_error:
                errors.append(str(save_error))

        compiled = _is_blueprint_compiled(bp)
        return {
            "compiled": compiled,
            "had_errors": len(errors) > 0,
            "errors": errors,
            "warnings": [],
            "status": "compiled" if compiled else "unknown",
            "method": method,
            "report": report,
        }
    except Exception as e:
        return {
            "compiled": False,
            "had_errors": True,
            "errors": [str(e)],
            "warnings": [],
            "status": "failed",
            "method": "failed",
            "report": {},
        }


def handle_blueprint_list(params: Dict[str, Any]) -> Dict[str, Any]:
    """List Blueprint assets in the project.

    Args:
        params:
            - path_filter (str): Content path prefix (default /Game/)
            - name_filter (str): Name filter, supports fnmatch wildcards
            - parent_class_filter (str): Filter by parent class name
            - limit (int): Max results (default 200)
    """
    try:
        import unreal

        path_filter = params.get("path_filter", "/Game/")
        name_filter = params.get("name_filter", "")
        parent_class_filter = params.get("parent_class_filter", "")
        limit = min(int(params.get("limit", 200)), 2000)
        if limit < 1:
            limit = 200

        assets = unreal.EditorAssetLibrary.list_assets(
            path_filter, recursive=True, include_folder=False
        )

        results: List[Dict[str, Any]] = []
        for asset_path in assets:
            if len(results) >= limit:
                break

            asset_data = unreal.EditorAssetLibrary.find_asset_data(asset_path)
            if not asset_data or str(asset_data.asset_class) != "Blueprint":
                continue

            asset_name = os.path.basename(asset_path).split(".")[0]

            if name_filter:
                if not fnmatch.fnmatch(asset_name.lower(), name_filter.lower()):
                    continue

            # Get parent class for filtering and reporting
            parent_class_name = ""
            is_compiled = False
            try:
                bp = unreal.EditorAssetLibrary.load_asset(asset_path)
                if bp:
                    parent_cls = bp.get_editor_property("parent_class")
                    if parent_cls:
                        parent_class_name = parent_cls.get_name()
                    is_compiled = _is_blueprint_compiled(bp)
            except Exception:
                pass

            if parent_class_filter:
                if parent_class_name.lower() != parent_class_filter.lower():
                    continue

            results.append({
                "name": asset_name,
                "path": asset_path,
                "parent_class": parent_class_name,
                "is_compiled": is_compiled,
            })

        truncated = len(results) >= limit

        return {
            "success": True,
            "data": {
                "count": len(results),
                "truncated": truncated,
                "blueprints": results,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_blueprint_info(params: Dict[str, Any]) -> Dict[str, Any]:
    """Get detailed Blueprint structure: components, variables, functions.

    Args:
        params:
            - blueprint_path (str): Asset path of the Blueprint
    """
    try:
        import unreal

        bp_path = params.get("blueprint_path", "")
        if not bp_path:
            return {"success": False, "data": {}, "error": "Missing 'blueprint_path'"}
        if not bp_path.startswith("/"):
            return {
                "success": False,
                "data": {},
                "error": f"blueprint_path must start with '/' (got '{bp_path}')",
            }

        bp = unreal.EditorAssetLibrary.load_asset(bp_path)
        if bp is None:
            return {
                "success": False,
                "data": {},
                "error": f"Blueprint not found: {bp_path}",
            }

        # Verify it's a Blueprint
        if not isinstance(bp, unreal.Blueprint):
            return {
                "success": False,
                "data": {},
                "error": f"Asset is not a Blueprint: {bp_path}",
            }

        parent_class_name = ""
        try:
            parent_cls = bp.get_editor_property("parent_class")
            if parent_cls:
                parent_class_name = parent_cls.get_name()
        except Exception:
            pass

        components = _extract_components(bp)
        variables = _extract_variables(bp)
        functions = _extract_functions(bp)
        event_graphs = _extract_event_graphs(bp)
        parent_chain = _get_parent_chain(bp)
        is_compiled = _is_blueprint_compiled(bp)

        return {
            "success": True,
            "data": {
                "name": bp.get_name(),
                "path": bp_path,
                "parent_class": parent_class_name,
                "parent_chain": parent_chain,
                "is_compiled": is_compiled,
                "components": components,
                "variables": variables,
                "functions": functions,
                "event_graphs": event_graphs,
                "component_count": len(components),
                "variable_count": len(variables),
                "function_count": len(functions),
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Create Blueprint")
def handle_blueprint_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a new Blueprint class.

    Args:
        params:
            - name (str): Blueprint name
            - path (str): Content directory (default /Game/Blueprints)
            - parent_class (str): Parent class name (default Actor)
            - components (list[dict]): Components to add, each with name, class, attach_to
            - variables (list[dict]): Variables to add, each with name, type, default_value, category, editable, tooltip
    """
    try:
        import unreal

        name = params.get("name", "")
        if not name:
            return {"success": False, "data": {}, "error": "Missing 'name'"}

        path = params.get("path", "/Game/Blueprints")
        if not path.startswith("/"):
            return {
                "success": False,
                "data": {},
                "error": f"path must start with '/' (got '{path}')",
            }

        parent_class_name = params.get("parent_class", "Actor")

        # Check if asset already exists
        full_path = f"{path}/{name}"
        existing = unreal.EditorAssetLibrary.find_asset_data(full_path)
        if existing and existing.is_valid():
            return {
                "success": False,
                "data": {},
                "error": f"Asset already exists at: {full_path}",
            }

        # Resolve parent class
        parent_class = _resolve_parent_class(parent_class_name)
        if parent_class is None:
            common_classes = sorted(_PARENT_CLASS_MAP.keys())
            return {
                "success": False,
                "data": {},
                "error": f"Unknown parent class: '{parent_class_name}'. Common classes: {common_classes}",
            }

        # Create the Blueprint
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent_class)

        bp = asset_tools.create_asset(name, path, unreal.Blueprint, factory)
        if bp is None:
            return {
                "success": False,
                "data": {},
                "error": "Failed to create Blueprint",
            }

        # Add components
        components_added: List[str] = []
        components_failed: List[str] = []
        component_defs = params.get("components", [])

        if component_defs:
            try:
                scs = bp.get_editor_property("simple_construction_script")
                if scs:
                    for comp_def in component_defs:
                        comp_name = comp_def.get("name", "")
                        comp_class_name = comp_def.get("class", "")
                        if not comp_name or not comp_class_name:
                            components_failed.append(comp_name or "unnamed")
                            continue
                        try:
                            comp_class = getattr(unreal, comp_class_name, None)
                            if comp_class is None:
                                components_failed.append(comp_name)
                                continue
                            node = scs.create_node(comp_class)
                            if node:
                                template = node.get_editor_property("component_template")
                                if template:
                                    template.rename(comp_name)
                                components_added.append(comp_name)
                            else:
                                components_failed.append(comp_name)
                        except Exception:
                            components_failed.append(comp_name)
            except Exception:
                for comp_def in component_defs:
                    components_failed.append(comp_def.get("name", "unnamed"))

        # Add variables (may not work in 4.27 Python)
        variables_added: List[str] = []
        variables_failed: List[str] = []
        variables_skipped_reason: Optional[str] = None
        variable_defs = params.get("variables", [])

        if variable_defs:
            try:
                # Try to add variables through the Blueprint's new_variables array
                new_vars = bp.get_editor_property("new_variables")
                if new_vars is not None:
                    for var_def in variable_defs:
                        var_name = var_def.get("name", "")
                        if not var_name:
                            variables_failed.append("unnamed")
                            continue
                        try:
                            # Create FBPVariableDescription - may not be constructable in 4.27
                            var_desc = unreal.BPVariableDescription()
                            var_desc.set_editor_property("var_name", var_name)

                            # Set category if provided
                            category = var_def.get("category", "")
                            if category:
                                var_desc.set_editor_property("category", category)

                            # Set tooltip if provided
                            tooltip = var_def.get("tooltip", "")
                            if tooltip:
                                var_desc.set_editor_property("tool_tip", tooltip)

                            new_vars.append(var_desc)
                            variables_added.append(var_name)
                        except Exception:
                            variables_failed.append(var_name)
                else:
                    variables_skipped_reason = "Blueprint new_variables array not accessible in 4.27 Python"
                    for var_def in variable_defs:
                        variables_failed.append(var_def.get("name", "unnamed"))
            except Exception as e:
                variables_skipped_reason = f"Variable creation not supported: {str(e)}"
                for var_def in variable_defs:
                    vname = var_def.get("name", "unnamed")
                    if vname not in variables_added:
                        variables_failed.append(vname)

        compile_result = _compile_blueprint_ue427_safe(bp, bp.get_path_name())
        compiled = bool(compile_result["compiled"])
        compile_errors = list(compile_result["errors"])

        # Save
        try:
            unreal.EditorAssetLibrary.save_asset(bp.get_path_name())
        except Exception:
            pass

        return {
            "success": True,
            "data": {
                "name": bp.get_name(),
                "path": bp.get_path_name(),
                "parent_class": parent_class_name,
                "components_added": components_added,
                "components_failed": components_failed,
                "variables_added": variables_added,
                "variables_failed": variables_failed,
                "variables_skipped_reason": variables_skipped_reason,
                "compiled": compiled,
                "compile_errors": compile_errors,
                "compile_method": compile_result["method"],
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Compile Blueprint")
def handle_blueprint_compile(params: Dict[str, Any]) -> Dict[str, Any]:
    """Compile a Blueprint and return success/failure with error details.

    Args:
        params:
            - blueprint_path (str): Asset path of the Blueprint
    """
    try:
        import unreal

        bp_path = params.get("blueprint_path", "")
        if not bp_path:
            return {"success": False, "data": {}, "error": "Missing 'blueprint_path'"}
        if not bp_path.startswith("/"):
            return {
                "success": False,
                "data": {},
                "error": f"blueprint_path must start with '/' (got '{bp_path}')",
            }

        bp = unreal.EditorAssetLibrary.load_asset(bp_path)
        if bp is None:
            return {
                "success": False,
                "data": {},
                "error": f"Blueprint not found: {bp_path}",
            }

        if not isinstance(bp, unreal.Blueprint):
            return {
                "success": False,
                "data": {},
                "error": f"Asset is not a Blueprint: {bp_path}",
            }

        warnings: List[str] = []
        compile_result = _compile_blueprint_ue427_safe(bp, bp_path)
        errors = list(compile_result["errors"])
        warnings = list(compile_result.get("warnings", []))
        had_errors = bool(compile_result["had_errors"])
        compiled = bool(compile_result["compiled"])

        # Save after successful compile
        if compiled and not had_errors:
            try:
                unreal.EditorAssetLibrary.save_asset(bp.get_path_name())
            except Exception:
                pass

        return {
            "success": True,
            "data": {
                "name": bp.get_name(),
                "path": bp_path,
                "compiled": compiled,
                "had_errors": had_errors,
                "errors": errors,
                "warnings": warnings,
                "method": compile_result["method"],
                "status": compile_result.get("status"),
                "report": compile_result.get("report", {}),
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_blueprint_document(params: Dict[str, Any]) -> Dict[str, Any]:
    """Generate a human-readable text summary of a Blueprint's structure.

    Args:
        params:
            - blueprint_path (str): Asset path of the Blueprint
            - detail_level (str): "minimal", "standard", or "detailed" (default "standard")
    """
    try:
        # Reuse blueprint_info for data extraction
        info_params = {"blueprint_path": params.get("blueprint_path", "")}
        result = handle_blueprint_info(info_params)
        if not result["success"]:
            return result

        info = result["data"]
        detail_level = params.get("detail_level", "standard")

        lines: List[str] = []

        # Header (all levels)
        lines.append(f"# {info['name']}")
        lines.append(f"Parent: {info.get('parent_class', 'Unknown')}")
        lines.append(f"Compiled: {'Yes' if info.get('is_compiled') else 'No'}")

        if detail_level == "minimal":
            lines.append(f"Components: {info.get('component_count', 0)}")
            lines.append(f"Variables: {info.get('variable_count', 0)}")
            lines.append(f"Functions: {info.get('function_count', 0)}")
        else:
            # Components section
            components = info.get("components", [])
            if components:
                lines.append("")
                lines.append(f"## Components ({len(components)})")
                for comp in components:
                    prefix = "  " if not comp.get("is_root") else ""
                    root_tag = " [ROOT]" if comp.get("is_root") else ""
                    parent_tag = f" -> {comp['parent']}" if comp.get("parent") else ""
                    lines.append(f"{prefix}- {comp['name']} ({comp['class']}){root_tag}{parent_tag}")

            # Variables section
            variables = info.get("variables", [])
            if variables:
                lines.append("")
                lines.append(f"## Variables ({len(variables)})")
                for var in variables:
                    var_type = var.get("type", "unknown")
                    category = f" [Category: {var['category']}]" if var.get("category") else ""
                    lines.append(f"- {var['name']} : {var_type}{category}")
                    if detail_level == "detailed" and var.get("tooltip"):
                        lines.append(f"  {var['tooltip']}")

            # Functions section
            functions = info.get("functions", [])
            if functions:
                lines.append("")
                lines.append(f"## Functions ({len(functions)})")
                for func in functions:
                    if detail_level == "detailed":
                        inputs = ", ".join(
                            f"{p.get('name', '?')}: {p.get('type', '?')}"
                            for p in func.get("inputs", [])
                        )
                        outputs = ", ".join(
                            p.get("type", "?") for p in func.get("outputs", [])
                        )
                        ret = f" -> {outputs}" if outputs else ""
                        lines.append(f"- {func['name']}({inputs}){ret}")
                    else:
                        lines.append(f"- {func['name']}")

            # Event graphs section
            event_graphs = info.get("event_graphs", [])
            if event_graphs:
                lines.append("")
                lines.append("## Event Graphs")
                for graph in event_graphs:
                    lines.append(f"- {graph}")

            # Parent chain (detailed only)
            if detail_level == "detailed":
                parent_chain = info.get("parent_chain", [])
                if parent_chain:
                    lines.append("")
                    lines.append(f"## Inheritance")
                    lines.append(f"{info['name']} -> {' -> '.join(parent_chain)}")

        documentation = "\n".join(lines)

        return {
            "success": True,
            "data": {
                "name": info["name"],
                "path": info["path"],
                "documentation": documentation,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_blueprint_build_from_json(params: Dict[str, Any]) -> Dict[str, Any]:
    """Build a Blueprint event graph from JSON using BlueprintGraphBuilderLibrary.

    Args:
        params:
            - blueprint_path (str): Asset path of the Blueprint to populate.
            - graph_json (dict): Graph specification for BlueprintGraphBuilderLibrary.
    """
    try:
        import unreal
        import json as json_mod

        bp_path = params.get("blueprint_path", "")
        graph_json = params.get("graph_json", {})

        if not bp_path:
            return {"success": False, "data": {}, "error": "Missing 'blueprint_path'"}

        bp = unreal.EditorAssetLibrary.load_asset(bp_path)
        if bp is None:
            return {"success": False, "data": {}, "error": f"Blueprint not found: {bp_path}"}
        if not isinstance(bp, unreal.Blueprint):
            return {"success": False, "data": {}, "error": f"Asset is not a Blueprint: {bp_path}"}

        lib = getattr(unreal, "BlueprintGraphBuilderLibrary", None)
        if lib is None:
            return {"success": False, "data": {}, "error": "BlueprintGraphBuilderLibrary not available"}

        json_str = json_mod.dumps(graph_json)
        lib.build_blueprint_from_json(bp, json_str, bool(params.get("clear_existing", True)))
        compile_result = _compile_blueprint_ue427_safe(bp, bp_path)

        return {
            "success": not compile_result["had_errors"],
            "data": {
                "path": bp_path,
                "compiled": compile_result["compiled"],
                "compile_method": compile_result["method"],
            },
            "error": "; ".join(compile_result["errors"]) if compile_result["had_errors"] else None,
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Build AnimBlueprint from JSON")
def handle_anim_blueprint_build_from_json(params: Dict[str, Any]) -> Dict[str, Any]:
    """Build an Animation Blueprint from JSON using AnimBlueprintBuilderLibrary.

    Args:
        params:
            - package_path (str): Content directory path e.g. /Game/AnimBP
            - asset_name (str): AnimBlueprint asset name e.g. ABP_Enemy
            - skeleton_path (str): Full path to Skeleton asset e.g. /Game/Mannequin/Mesh/UE4_Mannequin_Skeleton
            - json_spec (str): JSON specification for the AnimBP graph
    """
    try:
        import unreal

        package_path = params.get("package_path", "")
        asset_name = params.get("asset_name", "")
        skeleton_path = params.get("skeleton_path", "")
        json_spec = params.get("json_spec", "")

        if not package_path or not asset_name:
            return {"success": False, "data": {}, "error": "Missing 'package_path' or 'asset_name'"}
        if not skeleton_path:
            return {"success": False, "data": {}, "error": "Missing 'skeleton_path'"}
        if not json_spec:
            return {"success": False, "data": {}, "error": "Missing 'json_spec'"}

        lib = getattr(unreal, "AnimBlueprintBuilderLibrary", None)
        if lib is None:
            return {"success": False, "data": {}, "error": "AnimBlueprintBuilderLibrary not available"}

        result = lib.build_anim_blueprint_from_json(
            package_path, asset_name, skeleton_path, json_spec
        )

        full_path = f"{package_path}/{asset_name}"
        if result == "":
            return {"success": True, "data": {"path": full_path}, "error": None}
        else:
            return {"success": False, "data": {"path": full_path}, "error": result}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_widget_build_from_json(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a Widget Blueprint from JSON using WidgetBlueprintBuilderLibrary.

    Args:
        params:
            - package_path (str): Content directory path e.g. /Game/UI
            - asset_name (str): Widget Blueprint asset name e.g. WBP_MyWidget
            - widget_json (dict): Widget tree specification matching WidgetBlueprintBuilderLibrary schema
    """
    try:
        import unreal
        import json as json_mod

        package_path = params.get("package_path", "")
        asset_name = params.get("asset_name", "")
        widget_json = params.get("widget_json", {})

        if not package_path or not asset_name:
            return {"success": False, "data": {}, "error": "Missing 'package_path' or 'asset_name'"}

        lib = getattr(unreal, "WidgetBlueprintBuilderLibrary", None)
        if lib is None:
            return {"success": False, "data": {}, "error": "WidgetBlueprintBuilderLibrary not available"}

        unreal.EditorAssetLibrary.make_directory(package_path)

        if isinstance(widget_json, dict) and "root" in widget_json:
            spec_json = widget_json
        else:
            spec_json = {"root": widget_json}

        json_str = json_mod.dumps(spec_json)
        err_out = lib.build_widget_from_json(package_path, asset_name, json_str)

        full_path = f"{package_path}/{asset_name}"
        ok = str(err_out) == ""
        return {
            "success": ok,
            "data": {"path": full_path, "animations": spec_json.get("animations", [])},
            "error": str(err_out) if not ok else None,
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}
