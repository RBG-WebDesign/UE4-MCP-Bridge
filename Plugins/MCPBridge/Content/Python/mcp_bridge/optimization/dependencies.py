"""Unreal-aware dependency and active-level attribution helpers."""

from typing import Any, Dict, Iterable, List, Optional


def safe_prop(obj: Any, prop: str, default: Any = None) -> Any:
    try:
        return obj.get_editor_property(prop)
    except Exception:
        return default


def object_path(obj: Any) -> str:
    if obj is None:
        return ""
    try:
        return str(obj.get_path_name())
    except Exception:
        try:
            return str(obj.get_name())
        except Exception:
            return ""


def actor_label(actor: Any) -> str:
    try:
        return actor.get_actor_label()
    except Exception:
        try:
            return actor.get_name()
        except Exception:
            return "UnknownActor"


def class_name(obj: Any) -> str:
    try:
        return obj.get_class().get_name()
    except Exception:
        return type(obj).__name__


def iter_components(actor: Any) -> List[Any]:
    try:
        import unreal
        return list(actor.get_components_by_class(unreal.ActorComponent))
    except Exception:
        pass
    try:
        return list(actor.get_components())
    except Exception:
        return []


def material_texture_paths(material: Any) -> List[str]:
    paths: List[str] = []
    if material is None:
        return paths
    try:
        values = material.get_editor_property("texture_parameter_values")
        for value in values:
            tex = getattr(value, "parameter_value", None)
            path = object_path(tex)
            if path:
                paths.append(path)
    except Exception:
        pass
    return sorted(set(paths))


def material_parent_chain(material: Any) -> List[str]:
    chain: List[str] = []
    current = material
    for _ in range(12):
        parent = safe_prop(current, "parent")
        if parent is None:
            break
        path = object_path(parent)
        if path:
            chain.append(path)
        current = parent
    return chain


def collect_active_level_usage(unreal_module: Any) -> Dict[str, Any]:
    usage: Dict[str, Any] = {
        "meshes": {},
        "materials": {},
        "textures": {},
        "actors": 0,
    }
    try:
        actors = list(unreal_module.EditorLevelLibrary.get_all_level_actors())
    except Exception:
        actors = []
    usage["actors"] = len(actors)

    for actor in actors:
        label = actor_label(actor)
        for component in iter_components(actor):
            c_name = class_name(component).lower()
            if "meshcomponent" not in c_name:
                continue
            mesh = safe_prop(component, "static_mesh") or safe_prop(component, "skeletal_mesh")
            mesh_path = object_path(mesh)
            casts_shadow = bool(safe_prop(component, "cast_shadow", safe_prop(component, "cast_shadows", False)))
            if mesh_path:
                mesh_row = usage["meshes"].setdefault(mesh_path, {
                    "path": mesh_path,
                    "component_count": 0,
                    "actors": [],
                    "shadow_casters": 0,
                    "materials": {},
                })
                mesh_row["component_count"] += 1
                if len(mesh_row["actors"]) < 20:
                    mesh_row["actors"].append(label)
                if casts_shadow:
                    mesh_row["shadow_casters"] += 1

            material_count = 0
            try:
                material_count = int(component.get_num_materials())
            except Exception:
                material_count = 0
            for index in range(material_count):
                try:
                    material = component.get_material(index)
                except Exception:
                    material = None
                material_path = object_path(material)
                if not material_path:
                    continue
                mat_row = usage["materials"].setdefault(material_path, {
                    "path": material_path,
                    "component_count": 0,
                    "actors": [],
                    "meshes": {},
                    "texture_paths": [],
                    "parent_chain": [],
                    "material_risk": 0.0,
                })
                mat_row["component_count"] += 1
                if len(mat_row["actors"]) < 20:
                    mat_row["actors"].append(label)
                if mesh_path:
                    mat_row["meshes"][mesh_path] = mat_row["meshes"].get(mesh_path, 0) + 1
                    usage["meshes"][mesh_path]["materials"][material_path] = usage["meshes"][mesh_path]["materials"].get(material_path, 0) + 1
                mat_row["parent_chain"] = material_parent_chain(material)
                blend_mode = str(safe_prop(material, "blend_mode", ""))
                two_sided = bool(safe_prop(material, "two_sided", False))
                material_risk = 0.0
                if "TRANSLUCENT" in blend_mode.upper():
                    material_risk += 12.0
                if "MASKED" in blend_mode.upper():
                    material_risk += 8.0
                if two_sided:
                    material_risk += 5.0
                mat_row["material_risk"] = max(float(mat_row.get("material_risk") or 0.0), material_risk)
                textures = material_texture_paths(material)
                mat_row["texture_paths"] = sorted(set(mat_row["texture_paths"] + textures))
                for texture_path in textures:
                    tex_row = usage["textures"].setdefault(texture_path, {
                        "path": texture_path,
                        "component_count": 0,
                        "material_count": 0,
                        "materials": {},
                        "meshes": {},
                        "actors": [],
                        "material_risk": 0.0,
                    })
                    tex_row["component_count"] += 1
                    tex_row["materials"][material_path] = tex_row["materials"].get(material_path, 0) + 1
                    if mesh_path:
                        tex_row["meshes"][mesh_path] = tex_row["meshes"].get(mesh_path, 0) + 1
                    if len(tex_row["actors"]) < 20:
                        tex_row["actors"].append(label)
                    tex_row["material_risk"] = max(float(tex_row.get("material_risk") or 0.0), material_risk)

    for tex_row in usage["textures"].values():
        tex_row["material_count"] = len(tex_row.get("materials", {}))
        tex_row["top_materials"] = _top_counts(tex_row.get("materials", {}))
        tex_row["top_meshes"] = _top_counts(tex_row.get("meshes", {}))
    for mat_row in usage["materials"].values():
        mat_row["top_meshes"] = _top_counts(mat_row.get("meshes", {}))
    return usage


def dependency_trail_for_texture(texture_path: str, usage: Dict[str, Any]) -> Dict[str, Any]:
    row = (usage.get("textures") or {}).get(texture_path, {})
    materials = row.get("top_materials", [])
    meshes = row.get("top_meshes", [])
    trail = []
    for material in materials[:5]:
        trail.append({
            "texture": texture_path,
            "material": material.get("path"),
            "mesh_examples": meshes[:5],
            "actor_examples": row.get("actors", [])[:10],
        })
    return {
        "texture": texture_path,
        "used_by": {
            "component_count": int(row.get("component_count") or 0),
            "material_count": int(row.get("material_count") or 0),
            "materials": materials,
            "meshes": meshes,
            "actors": row.get("actors", [])[:10],
        },
        "dependency_trail": trail,
    }


def _top_counts(values: Dict[str, int], limit: int = 10) -> List[Dict[str, Any]]:
    rows = [{"path": key, "count": int(value or 0)} for key, value in values.items()]
    rows.sort(key=lambda row: row["count"], reverse=True)
    return rows[:limit]

