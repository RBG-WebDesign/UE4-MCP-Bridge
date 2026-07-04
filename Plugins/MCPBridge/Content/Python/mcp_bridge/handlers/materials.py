"""Material system handlers.

Handles listing, inspecting, creating, and applying materials
in the UE4 editor.

MaterialEditingLibrary availability in 4.27:
- set_material_instance_scalar_parameter_value: available
- set_material_instance_vector_parameter_value: available
- set_material_instance_texture_parameter_value: available
- get_scalar_parameter_names / get_vector_parameter_names / get_texture_parameter_names:
  may not be available; fall back to iterating parameter info structs on the material.
"""

import fnmatch
import os
from typing import Any, Dict, List, Optional
from mcp_bridge.utils.transactions import transactional


def _get_material_type(asset_class: str) -> Optional[str]:
    """Map UE4 asset class to our type string."""
    if asset_class == "Material":
        return "material"
    if asset_class == "MaterialInstanceConstant":
        return "instance"
    return None


def _get_parent_path(mat: Any) -> Optional[str]:
    """Get the parent material path for a MaterialInstanceConstant."""
    try:
        parent = mat.get_editor_property("parent")
        if parent:
            return str(parent.get_path_name())
    except Exception:
        pass
    return None


def _get_parent_chain(mat: Any) -> List[str]:
    """Walk the parent chain for a material instance."""
    chain: List[str] = []
    try:
        current = mat
        while True:
            parent = current.get_editor_property("parent")
            if parent is None:
                break
            chain.append(str(parent.get_path_name()))
            # If parent is a base Material, stop
            import unreal
            if isinstance(parent, unreal.Material):
                break
            current = parent
    except Exception:
        pass
    return chain


def _extract_instance_parameters(mat: Any) -> Dict[str, List[Dict[str, Any]]]:
    """Extract overridden parameters from a MaterialInstanceConstant."""
    params: Dict[str, List[Dict[str, Any]]] = {
        "scalar": [],
        "vector": [],
        "texture": [],
    }
    try:
        for p in mat.get_editor_property("scalar_parameter_values"):
            params["scalar"].append({
                "name": str(p.parameter_info.name),
                "value": p.parameter_value,
            })
    except Exception:
        pass
    try:
        for p in mat.get_editor_property("vector_parameter_values"):
            c = p.parameter_value
            params["vector"].append({
                "name": str(p.parameter_info.name),
                "value": [c.r, c.g, c.b, c.a],
            })
    except Exception:
        pass
    try:
        for p in mat.get_editor_property("texture_parameter_values"):
            tex = p.parameter_value
            params["texture"].append({
                "name": str(p.parameter_info.name),
                "value": str(tex.get_path_name()) if tex else None,
            })
    except Exception:
        pass
    return params


def _extract_base_material_parameters(mat: Any) -> Dict[str, List[Dict[str, Any]]]:
    """Extract parameter names from a base Material using MaterialEditingLibrary."""
    params: Dict[str, List[Dict[str, Any]]] = {
        "scalar": [],
        "vector": [],
        "texture": [],
    }
    try:
        import unreal
        lib = unreal.MaterialEditingLibrary
        try:
            for name in lib.get_scalar_parameter_names(mat):
                params["scalar"].append({"name": str(name)})
        except (AttributeError, Exception):
            pass
        try:
            for name in lib.get_vector_parameter_names(mat):
                params["vector"].append({"name": str(name)})
        except (AttributeError, Exception):
            pass
        try:
            for name in lib.get_texture_parameter_names(mat):
                params["texture"].append({"name": str(name)})
        except (AttributeError, Exception):
            pass
    except ImportError:
        pass
    return params


def handle_material_list(params: Dict[str, Any]) -> Dict[str, Any]:
    """List materials with optional filters.

    Args:
        params:
            - path_filter (str): Content path prefix (default /Game/)
            - name_filter (str): Name filter, supports fnmatch wildcards
            - type_filter (str): "material", "instance", or "all" (default "all")
            - limit (int): Max results (default 200)
    """
    try:
        import unreal

        path_filter = params.get("path_filter", "/Game/")
        name_filter = params.get("name_filter", "")
        type_filter = params.get("type_filter", "all")
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
            if not asset_data:
                continue

            asset_class = str(asset_data.asset_class)
            mat_type = _get_material_type(asset_class)
            if mat_type is None:
                continue

            if type_filter != "all" and mat_type != type_filter:
                continue

            asset_name = os.path.basename(asset_path).split(".")[0]

            if name_filter:
                if not fnmatch.fnmatch(asset_name.lower(), name_filter.lower()):
                    continue

            info: Dict[str, Any] = {
                "name": asset_name,
                "path": asset_path,
                "type": mat_type,
                "parent": None,
            }

            if mat_type == "instance":
                try:
                    mat = unreal.EditorAssetLibrary.load_asset(asset_path)
                    if mat:
                        info["parent"] = _get_parent_path(mat)
                except Exception:
                    pass

            results.append(info)

        truncated = len(results) >= limit

        return {
            "success": True,
            "data": {
                "count": len(results),
                "truncated": truncated,
                "materials": results,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_material_info(params: Dict[str, Any]) -> Dict[str, Any]:
    """Get detailed information about a single material or material instance.

    Args:
        params:
            - material_path (str): Asset path of the material (must start with /)
    """
    try:
        import unreal

        mat_path = params.get("material_path", "")
        if not mat_path:
            return {"success": False, "data": {}, "error": "Missing 'material_path'"}
        if not mat_path.startswith("/"):
            return {
                "success": False,
                "data": {},
                "error": f"material_path must start with '/' (got '{mat_path}')",
            }

        mat = unreal.EditorAssetLibrary.load_asset(mat_path)
        if mat is None:
            return {
                "success": False,
                "data": {},
                "error": f"Material not found: {mat_path}",
            }

        is_instance = isinstance(mat, unreal.MaterialInstanceConstant)
        is_material = isinstance(mat, unreal.Material) if not is_instance else False

        if not is_instance and not is_material:
            return {
                "success": False,
                "data": {},
                "error": f"Asset is not a Material or MaterialInstanceConstant: {mat_path}",
            }

        info: Dict[str, Any] = {
            "name": mat.get_name(),
            "path": mat_path,
            "type": "instance" if is_instance else "material",
        }

        if is_instance:
            info["parent"] = _get_parent_path(mat)
            info["parent_chain"] = _get_parent_chain(mat)
            info["parameters"] = _extract_instance_parameters(mat)
        else:
            info["parent"] = None
            info["parent_chain"] = []
            info["parameters"] = _extract_base_material_parameters(mat)

        return {"success": True, "data": info}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Create Material")
def handle_material_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a new material or material instance.

    Args:
        params:
            - name (str): Material name
            - path (str): Content directory (default /Game/Materials)
            - type (str): "material" or "instance"
            - parent (str): Parent material path (required for instances)
            - parameters (dict): Initial parameter values for instances:
                - scalar: {name: value}
                - vector: {name: [r, g, b, a]}
                - texture: {name: asset_path}
    """
    try:
        import unreal

        name = params.get("name", "")
        if not name:
            return {"success": False, "data": {}, "error": "Missing 'name'"}

        path = params.get("path", "/Game/Materials")
        if not path.startswith("/"):
            return {
                "success": False,
                "data": {},
                "error": f"path must start with '/' (got '{path}')",
            }

        mat_type = params.get("type", "instance" if params.get("parent") else "material")
        parent_path = params.get("parent", "")

        # Check if asset already exists
        full_path = f"{path}/{name}"
        existing = unreal.EditorAssetLibrary.find_asset_data(full_path)
        if existing and existing.is_valid():
            return {
                "success": False,
                "data": {},
                "error": f"Asset already exists at: {full_path}",
            }

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        parameters_set: Dict[str, List[str]] = {"scalar": [], "vector": [], "texture": []}

        if mat_type == "instance":
            if not parent_path:
                return {
                    "success": False,
                    "data": {},
                    "error": "type is 'instance' but no 'parent' material path provided",
                }

            parent_mat = unreal.EditorAssetLibrary.load_asset(parent_path)
            if parent_mat is None:
                return {
                    "success": False,
                    "data": {},
                    "error": f"Parent material not found: {parent_path}",
                }

            if not isinstance(parent_mat, (unreal.Material, unreal.MaterialInstanceConstant)):
                return {
                    "success": False,
                    "data": {},
                    "error": f"Parent is not a Material or MaterialInstanceConstant: {parent_path}",
                }

            factory = unreal.MaterialInstanceConstantFactoryNew()
            factory.set_editor_property("initial_parent", parent_mat)
            mat = asset_tools.create_asset(
                name, path, unreal.MaterialInstanceConstant, factory
            )

            if mat is None:
                return {
                    "success": False,
                    "data": {},
                    "error": "Failed to create material instance",
                }

            # Set initial parameters if provided
            init_params = params.get("parameters", {})

            for param_name, value in init_params.get("scalar", {}).items():
                try:
                    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                        mat, param_name, float(value)
                    )
                    parameters_set["scalar"].append(param_name)
                except Exception:
                    pass

            for param_name, color_val in init_params.get("vector", {}).items():
                try:
                    if isinstance(color_val, (list, tuple)) and len(color_val) >= 3:
                        color = unreal.LinearColor(
                            r=float(color_val[0]),
                            g=float(color_val[1]),
                            b=float(color_val[2]),
                            a=float(color_val[3]) if len(color_val) > 3 else 1.0,
                        )
                    else:
                        continue
                    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
                        mat, param_name, color
                    )
                    parameters_set["vector"].append(param_name)
                except Exception:
                    pass

            for param_name, tex_path in init_params.get("texture", {}).items():
                try:
                    tex = unreal.EditorAssetLibrary.load_asset(tex_path)
                    if tex:
                        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                            mat, param_name, tex
                        )
                        parameters_set["texture"].append(param_name)
                except Exception:
                    pass

        else:
            factory = unreal.MaterialFactoryNew()
            mat = asset_tools.create_asset(name, path, unreal.Material, factory)
            if mat is None:
                return {
                    "success": False,
                    "data": {},
                    "error": "Failed to create base material",
                }

        # Save the asset
        try:
            unreal.EditorAssetLibrary.save_asset(mat.get_path_name())
        except Exception:
            pass

        return {
            "success": True,
            "data": {
                "name": mat.get_name(),
                "path": mat.get_path_name(),
                "type": mat_type,
                "parent": parent_path if mat_type == "instance" else None,
                "parameters_set": parameters_set,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


@transactional("Apply Material")
def handle_material_apply(params: Dict[str, Any]) -> Dict[str, Any]:
    """Apply a material to an actor's mesh component.

    Args:
        params:
            - actor_name (str): Label of the target actor
            - material_path (str): Asset path of the material
            - slot_index (int): Material slot index (default 0)
            - slot_name (str): Material slot name (overrides slot_index)
            - component_name (str): Specific mesh component name
    """
    try:
        import unreal

        actor_name = params.get("actor_name", "")
        mat_path = params.get("material_path", "")

        if not actor_name:
            return {"success": False, "data": {}, "error": "Missing 'actor_name'"}
        if not mat_path:
            return {"success": False, "data": {}, "error": "Missing 'material_path'"}

        # Load material
        mat = unreal.EditorAssetLibrary.load_asset(mat_path)
        if mat is None:
            return {
                "success": False,
                "data": {},
                "error": f"Material not found: {mat_path}",
            }
        if not isinstance(mat, (unreal.Material, unreal.MaterialInstanceConstant)):
            return {
                "success": False,
                "data": {},
                "error": f"Asset is not a Material or MaterialInstanceConstant: {mat_path}",
            }

        # Find actor
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target = None
        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target = actor
                break
        if target is None:
            return {
                "success": False,
                "data": {},
                "error": f"Actor not found: {actor_name}",
            }

        # Find mesh component
        component_name = params.get("component_name", "")
        mesh_comp = None
        comp_name_used = ""

        if component_name:
            all_components = target.get_components_by_class(unreal.ActorComponent)
            for comp in all_components:
                if comp.get_name() == component_name:
                    if isinstance(comp, (unreal.StaticMeshComponent, unreal.SkeletalMeshComponent)):
                        mesh_comp = comp
                        comp_name_used = comp.get_name()
                        break
            if mesh_comp is None:
                return {
                    "success": False,
                    "data": {},
                    "error": f"Mesh component not found: {component_name}",
                }
        else:
            static_comps = target.get_components_by_class(unreal.StaticMeshComponent)
            if static_comps:
                mesh_comp = static_comps[0]
                comp_name_used = mesh_comp.get_name()
            else:
                skel_comps = target.get_components_by_class(unreal.SkeletalMeshComponent)
                if skel_comps:
                    mesh_comp = skel_comps[0]
                    comp_name_used = mesh_comp.get_name()

        if mesh_comp is None:
            return {
                "success": False,
                "data": {},
                "error": f"Actor '{actor_name}' has no mesh components",
            }

        # Determine total slots
        total_slots = mesh_comp.get_num_materials()

        # Resolve slot
        slot_index = int(params.get("slot_index", 0))
        slot_name_param = params.get("slot_name", "")
        resolved_slot_name = ""

        if slot_name_param:
            # Find slot by name
            found = False
            for i in range(total_slots):
                sname = str(mesh_comp.get_material_slot_names()[i]) if hasattr(mesh_comp, "get_material_slot_names") else ""
                if sname == slot_name_param:
                    slot_index = i
                    resolved_slot_name = sname
                    found = True
                    break
            if not found:
                # Try alternative method
                try:
                    slot_names = [str(n) for n in mesh_comp.get_material_slot_names()]
                    return {
                        "success": False,
                        "data": {},
                        "error": f"Slot name '{slot_name_param}' not found. Available: {slot_names}",
                    }
                except Exception:
                    return {
                        "success": False,
                        "data": {},
                        "error": f"Slot name '{slot_name_param}' not found",
                    }

        if slot_index < 0 or slot_index >= total_slots:
            return {
                "success": False,
                "data": {},
                "error": f"Slot index {slot_index} out of range (actor has {total_slots} material slots)",
            }

        # Get previous material
        prev_mat = mesh_comp.get_material(slot_index)
        prev_mat_path = str(prev_mat.get_path_name()) if prev_mat else None

        # Apply material
        mesh_comp.set_material(slot_index, mat)

        # Verify application
        applied_mat = mesh_comp.get_material(slot_index)
        applied_path = str(applied_mat.get_path_name()) if applied_mat else None

        return {
            "success": True,
            "data": {
                "actor": actor_name,
                "component": comp_name_used,
                "slot_index": slot_index,
                "slot_name": resolved_slot_name or None,
                "material_applied": applied_path,
                "previous_material": prev_mat_path,
                "total_slots": total_slots,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}
