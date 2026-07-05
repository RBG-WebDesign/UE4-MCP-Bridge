"""Optimization assistant handlers for UE4.27 projects.

These commands turn the bridge into a guided profiler: run the useful
console overlays, capture evidence, inspect assets and level state where
Python can return structured data, then produce concrete next steps.
"""

import datetime
import json
import os
import time
from typing import Any, Dict, Iterable, List, Optional, Tuple

from mcp_bridge.optimization import dependencies as opt_dependencies
from mcp_bridge.optimization import profiler_ingestion
from mcp_bridge.optimization import ranking as opt_ranking
from mcp_bridge.optimization import reports as opt_reports
from mcp_bridge.optimization import schema as opt_schema
from mcp_bridge.optimization import verification as opt_verification


TARGET_FRAME_BUDGETS_MS: Dict[str, float] = {
    "desktop_30": 33.33,
    "desktop_60": 16.67,
    "vr_72": 13.89,
    "vr_80": 12.50,
    "vr_90": 11.11,
    "vr_120": 8.33,
}


CONSOLE_CATALOG: List[Dict[str, Any]] = [
    {
        "command": "stat unit",
        "category": "Frame Triage",
        "answers": "Is frame time dominated by Game, Draw, GPU, or total Frame time?",
        "use_when": "Run first on any slow scene.",
        "bad_signs": ["Game, Draw, or GPU is over the selected frame budget", "Frame time spikes"],
        "next_actions": ["Branch into CPU, render thread, or GPU capture based on the highest number"],
    },
    {
        "command": "stat fps",
        "category": "Frame Triage",
        "answers": "What is the approximate current FPS?",
        "use_when": "Pair with stat unit for a quick sanity check.",
        "bad_signs": ["FPS below target", "Large FPS swings while standing still"],
        "next_actions": ["Use milliseconds from stat unit for diagnosis"],
    },
    {
        "command": "stat scenerendering",
        "category": "Scene Optimization",
        "answers": "How many primitives, draw calls, lights, and scene rendering costs are active?",
        "use_when": "Draw thread is high, many objects are visible, or scene setup is suspected.",
        "bad_signs": ["High visible primitive count", "High dynamic shadow or draw cost"],
        "next_actions": ["Use culling, HLOD, instancing, and light reduction"],
    },
    {
        "command": "stat rhi",
        "category": "Rendering",
        "answers": "What are the RHI draw primitive and draw call counts?",
        "use_when": "Draw thread or GPU cost may be caused by too many render submissions.",
        "bad_signs": ["High draw calls", "High triangle count for the target platform"],
        "next_actions": ["Merge props, use instancing, simplify meshes, reduce material slots"],
    },
    {
        "command": "stat gpu",
        "category": "GPU Profiling",
        "answers": "Which major GPU categories are expensive?",
        "use_when": "GPU time is highest in stat unit.",
        "bad_signs": ["Expensive shadows", "Expensive translucency", "Expensive post processing"],
        "next_actions": ["Run profilegpu and capture shader, quad overdraw, and light complexity views"],
    },
    {
        "command": "profilegpu",
        "category": "GPU Profiling",
        "answers": "Which GPU passes are most expensive?",
        "use_when": "GPU-bound scenes need pass-level evidence.",
        "bad_signs": ["High Shadow Depths", "High BasePass", "High Translucency", "High PostProcessing"],
        "next_actions": ["Reduce the pass that dominates before retesting stat unit"],
    },
    {
        "command": "stat game",
        "category": "CPU Profiling",
        "answers": "Which game thread systems are expensive?",
        "use_when": "Game is highest in stat unit.",
        "bad_signs": ["High tick, AI, movement, component, or world update cost"],
        "next_actions": ["Audit ticking actors, Blueprint cost, AI, animation, and physics"],
    },
    {
        "command": "stat initviews",
        "category": "Scene Optimization",
        "answers": "What is the visibility, culling, and occlusion setup cost?",
        "use_when": "Draw thread is high or long sightlines show many actors.",
        "bad_signs": ["High visible primitives", "High visibility setup cost", "Weak culling"],
        "next_actions": ["Add occluders, cull distance volumes, HLOD, and shorter sightlines"],
    },
    {
        "command": "stat streaming",
        "category": "Streaming and Memory",
        "answers": "Is texture streaming over budget or constantly reacting?",
        "use_when": "Texture memory, hitching, or blurry textures are suspected.",
        "bad_signs": ["Texture pool over budget", "Large streaming changes during gameplay"],
        "next_actions": ["Resize textures, improve mips, split content, check pool size only after asset fixes"],
    },
    {
        "command": "stat memory",
        "category": "Streaming and Memory",
        "answers": "Where is memory going at a high level?",
        "use_when": "Memory pressure, crashes, or streaming trouble are suspected.",
        "bad_signs": ["High texture memory", "High static mesh memory", "High UObject count"],
        "next_actions": ["Use asset audit, Size Map, and Reference Viewer"],
    },
    {
        "command": "stat blueprint",
        "category": "CPU Profiling",
        "answers": "Are Blueprints spending meaningful time per frame?",
        "use_when": "Game thread is high or many Blueprint actors tick.",
        "bad_signs": ["High Blueprint time", "Many ticking Blueprint actors"],
        "next_actions": ["Replace Tick with events or timers, pool spawned actors, simplify hot graphs"],
    },
    {
        "command": "stat anim",
        "category": "CPU Profiling",
        "answers": "Are animation updates expensive?",
        "use_when": "Many skeletal meshes or Animation Blueprints are active.",
        "bad_signs": ["High animation evaluation", "Many visible skeletal meshes"],
        "next_actions": ["Enable update-rate optimization, reduce distant updates, use impostors"],
    },
    {
        "command": "stat physics",
        "category": "CPU Profiling",
        "answers": "Are collision and simulation expensive?",
        "use_when": "Physics bodies, constraints, ragdolls, or overlap-heavy actors are active.",
        "bad_signs": ["High physics time", "Many simulating bodies", "Complex decorative collision"],
        "next_actions": ["Disable decorative simulation, simplify collision, reduce overlap queries"],
    },
    {
        "command": "stat particles",
        "category": "VFX",
        "answers": "Are particles a CPU or GPU risk?",
        "use_when": "Smoke, sparks, fog cards, or many emitters are active.",
        "bad_signs": ["High particle counts", "Expensive translucent particles"],
        "next_actions": ["Cull emitters, reduce overdraw, simplify materials, shorten effect lifetime"],
    },
    {
        "command": "stat levels",
        "category": "Streaming and Memory",
        "answers": "Which levels are loaded and visible?",
        "use_when": "Too much content may be loaded at once.",
        "bad_signs": ["Too many loaded sublevels", "Hidden content still loaded"],
        "next_actions": ["Adjust streaming volumes, unload far interiors, split heavy areas"],
    },
]

DEFAULT_TRIAGE_COMMANDS = [
    "stat unit",
    "stat fps",
    "stat scenerendering",
    "stat rhi",
    "stat gpu",
    "stat game",
    "stat initviews",
    "stat streaming",
    "stat memory",
    "stat blueprint",
    "stat anim",
    "stat physics",
    "stat particles",
    "stat levels",
]

VIEWMODE_COMMANDS = {
    "lit": "viewmode lit",
    "shader_complexity": "viewmode shadercomplexity",
    "quad_overdraw": "viewmode quadoverdraw",
    "light_complexity": "viewmode lightcomplexity",
    "stationary_light_overlap": "viewmode stationarylightoverlap",
    "lighting_only": "viewmode lightingonly",
    "detail_lighting": "viewmode detaillighting",
    "wireframe": "viewmode wireframe",
    "collision": "viewmode CollisionVisibility",
}


def _ok(data: Dict[str, Any]) -> Dict[str, Any]:
    return {"success": True, "data": data}


def _err(message: str) -> Dict[str, Any]:
    return {"success": False, "data": {}, "error": message}


def _project_dir(unreal_module: Any) -> str:
    return unreal_module.SystemLibrary.get_project_directory()


def _optimization_dir(unreal_module: Any) -> str:
    path = os.path.join(_project_dir(unreal_module), "Saved", "MCP", "Optimization")
    os.makedirs(path, exist_ok=True)
    return path


def _timestamp() -> str:
    return datetime.datetime.now().strftime("%Y%m%d_%H%M%S")


def _target_budget_ms(target: str, custom_ms: Optional[Any] = None) -> float:
    if custom_ms is not None:
        try:
            value = float(custom_ms)
            if value > 0:
                return round(value, 2)
        except Exception:
            pass
    return TARGET_FRAME_BUDGETS_MS.get(str(target or "desktop_60"), TARGET_FRAME_BUDGETS_MS["desktop_60"])


def _execute_console(unreal_module: Any, command: str) -> Dict[str, Any]:
    world = unreal_module.EditorLevelLibrary.get_editor_world()
    if not world:
        return {"command": command, "success": False, "error": "No level is currently open"}
    from mcp_bridge.utils.console_safety import execute_console_command

    result = execute_console_command(unreal_module, world, command, "optimization_console")
    data = result.get("data", {}) if isinstance(result.get("data"), dict) else {}
    response = {
        "command": command,
        "success": bool(result.get("success", False)),
    }
    if not response["success"]:
        response["error"] = str(result.get("error", "Console command failed"))
        if data.get("error_code"):
            response["error_code"] = data.get("error_code")
    return response


def _capture_viewport(filename: str, width: int = 1920, height: int = 1080) -> Dict[str, Any]:
    from mcp_bridge.handlers.viewport import handle_viewport_screenshot

    return handle_viewport_screenshot({
        "filename": filename,
        "resolution": {"width": width, "height": height},
    })


def _issue(
    severity: str,
    category: str,
    item: str,
    issue: str,
    why: str,
    fix: str,
    evidence: Optional[Dict[str, Any]] = None,
    confidence: str = "Medium",
    fix_type: str = "",
) -> Dict[str, Any]:
    if not fix_type:
        issue_lower = issue.lower()
        category_lower = category.lower()
        if "never stream" in issue_lower or "mipmaps disabled" in issue_lower:
            fix_type = "Low Risk Setting Review"
        elif "movable shadow" in issue_lower or "attenuation radius" in issue_lower or category_lower == "lighting":
            fix_type = "Manual Lighting Review"
        elif "instances of the same static mesh" in issue_lower:
            fix_type = "Content Refactor"
        elif "complex collision" in issue_lower:
            fix_type = "Collision Review"
        elif "no authored lods" in issue_lower:
            fix_type = "Asset LOD Work"
        elif category_lower in {"design around", "scene setup"}:
            fix_type = "Level Design Review"
        else:
            fix_type = "Review Required"
    return {
        "severity": severity,
        "confidence": confidence,
        "fix_type": fix_type,
        "category": category,
        "item": item,
        "issue": issue,
        "why": why,
        "suggested_fix": fix,
        "evidence": evidence or {},
    }


def _normalize_finding(finding: Dict[str, Any]) -> Dict[str, Any]:
    normalized = dict(finding)
    normalized.setdefault("severity", "Info")
    normalized.setdefault("confidence", "Medium")
    normalized.setdefault("fix_type", "Review Required")
    normalized.setdefault("category", "Optimization")
    normalized.setdefault("item", "Unknown")
    normalized.setdefault("issue", "")
    normalized.setdefault("why", "")
    normalized.setdefault("suggested_fix", "")
    normalized.setdefault("evidence", {})
    return normalized


def _normalize_findings(findings: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    return [_normalize_finding(finding) for finding in findings]


def _safe_prop(obj: Any, prop: str, default: Any = None) -> Any:
    try:
        return obj.get_editor_property(prop)
    except Exception:
        return default


def _asset_class_name(asset_data: Any) -> str:
    try:
        return str(asset_data.asset_class)
    except Exception:
        return "Unknown"


def _list_assets(unreal_module: Any, root: str, recursive: bool) -> List[str]:
    try:
        return list(unreal_module.EditorAssetLibrary.list_assets(root, recursive=recursive, include_folder=False))
    except Exception:
        return []


def _class_matches(class_name: str, candidates: Iterable[str]) -> bool:
    lower = class_name.lower()
    return any(candidate.lower() in lower for candidate in candidates)


def _material_name(material: Any) -> str:
    try:
        return material.get_path_name()
    except Exception:
        try:
            return material.get_name()
        except Exception:
            return ""


def _static_mesh_findings(unreal_module: Any, asset: Any, path: str, max_material_slots: int) -> List[Dict[str, Any]]:
    findings: List[Dict[str, Any]] = []
    name = asset.get_name()

    lod_count = 0
    try:
        lod_count = int(asset.get_num_lods())
    except Exception:
        pass
    if lod_count <= 1:
        findings.append(_issue(
            "Medium",
            "Static Mesh",
            path,
            "Static mesh has no authored LODs",
            "Meshes with no LODs keep full detail even when small on screen.",
            "Generate or import LODs, especially for repeated props and background set dressing.",
            {"lod_count": lod_count},
        ))

    material_slots = 0
    try:
        material_slots = int(asset.get_num_sections(0))
    except Exception:
        try:
            material_slots = len(_safe_prop(asset, "static_materials", []) or [])
        except Exception:
            material_slots = 0
    if material_slots > max_material_slots:
        findings.append(_issue(
            "Medium",
            "Static Mesh",
            path,
            f"Static mesh uses {material_slots} material slots",
            "Each material slot can add draw overhead and makes batching harder.",
            "Merge material slots where practical or split the mesh by real material needs.",
            {"material_slots": material_slots},
        ))

    lightmap_resolution = _safe_prop(asset, "light_map_resolution")
    try:
        lightmap_value = int(lightmap_resolution)
    except Exception:
        lightmap_value = 0
    if lightmap_value >= 1024:
        findings.append(_issue(
            "High",
            "Lightmaps",
            path,
            f"Lightmap resolution is {lightmap_value}",
            "Very high lightmaps increase memory, bake time, and streaming pressure.",
            "Test 256 or 512, rebuild lighting, then compare visual loss against memory savings.",
            {"light_map_resolution": lightmap_value},
        ))

    body_setup = _safe_prop(asset, "body_setup")
    collision_trace = str(_safe_prop(body_setup, "collision_trace_flag", "")) if body_setup else ""
    if "USE_COMPLEX_AS_SIMPLE" in collision_trace.upper():
        findings.append(_issue(
            "Medium",
            "Static Mesh",
            path,
            "Mesh appears to use complex collision as simple",
            "Complex collision on many props can increase physics and query cost.",
            "Use simplified collision primitives for props that do not need exact per-triangle collision.",
            {"collision_trace_flag": collision_trace},
        ))

    bounds = None
    try:
        raw_bounds = asset.get_bounds()
        bounds = {
            "extent": {
                "x": raw_bounds.box_extent.x,
                "y": raw_bounds.box_extent.y,
                "z": raw_bounds.box_extent.z,
            }
        }
    except Exception:
        pass

    if bounds and max(bounds["extent"].values()) < 60 and lod_count <= 1:
        findings.append(_issue(
            "Low",
            "Static Mesh",
            path,
            f"Small prop {name} has no LODs",
            "Small props are often repeated many times and become death by a thousand cuts.",
            "Use instancing, merge nearby non-interactive copies, or add a cheap distant LOD.",
            {"bounds": bounds, "lod_count": lod_count},
        ))

    return findings


def _skeletal_mesh_findings(asset: Any, path: str) -> List[Dict[str, Any]]:
    findings: List[Dict[str, Any]] = []

    lod_count = 0
    try:
        lod_count = int(asset.get_num_lods())
    except Exception:
        pass
    if lod_count <= 1:
        findings.append(_issue(
            "Medium",
            "Skeletal Mesh",
            path,
            "Skeletal mesh has no authored LODs",
            "Characters and animated meshes need cheaper distant versions to reduce CPU and GPU cost.",
            "Add skeletal mesh LODs and pair them with update-rate optimization for distance.",
            {"lod_count": lod_count},
        ))

    morph_targets = _safe_prop(asset, "morph_targets", []) or []
    try:
        morph_count = len(morph_targets)
    except Exception:
        morph_count = 0
    if morph_count > 0:
        findings.append(_issue(
            "Low",
            "Skeletal Mesh",
            path,
            f"Skeletal mesh has {morph_count} morph targets",
            "Morph targets can add animation and memory cost if used widely.",
            "Keep morph targets on hero meshes and avoid them on background crowd variants.",
            {"morph_targets": morph_count},
        ))

    return findings


def _texture_findings(asset: Any, path: str, large_texture_px: int) -> List[Dict[str, Any]]:
    findings: List[Dict[str, Any]] = []
    size_x = 0
    size_y = 0
    for method_name in ("blueprint_get_size_x", "get_size_x"):
        try:
            size_x = int(getattr(asset, method_name)())
            break
        except Exception:
            pass
    for method_name in ("blueprint_get_size_y", "get_size_y"):
        try:
            size_y = int(getattr(asset, method_name)())
            break
        except Exception:
            pass

    if max(size_x, size_y) >= large_texture_px:
        findings.append(_issue(
            "Medium",
            "Texture",
            path,
            f"Large texture detected: {size_x}x{size_y}",
            "Large textures on props can push streaming memory over budget.",
            "Check screen size in game and test 2K or 1K versions for small props.",
            {"width": size_x, "height": size_y},
        ))

    never_stream = bool(_safe_prop(asset, "never_stream", False))
    if never_stream:
        findings.append(_issue(
            "High",
            "Texture",
            path,
            "Texture is marked Never Stream",
            "Never Stream textures stay resident and can quickly pressure memory.",
            "Allow streaming unless this texture truly must stay fully resident.",
            {"never_stream": True},
        ))

    mip_setting = str(_safe_prop(asset, "mip_gen_settings", ""))
    if "NO_MIPMAPS" in mip_setting.upper():
        findings.append(_issue(
            "Medium",
            "Texture",
            path,
            "Texture appears to have mipmaps disabled",
            "No mipmaps can shimmer, waste bandwidth, and hurt distant rendering.",
            "Enable mipmaps for world textures unless this is UI or a special lookup texture.",
            {"mip_gen_settings": mip_setting},
        ))

    return findings


def _material_findings(asset: Any, path: str) -> List[Dict[str, Any]]:
    findings: List[Dict[str, Any]] = []
    blend_mode = str(_safe_prop(asset, "blend_mode", ""))
    two_sided = bool(_safe_prop(asset, "two_sided", False))

    if "TRANSLUCENT" in blend_mode.upper():
        findings.append(_issue(
            "High",
            "Material",
            path,
            "Material is translucent",
            "Translucency is often expensive because of sorting, overdraw, and limited depth rejection.",
            "Use fewer overlapping cards, replace distant versions with opaque fakes, and inspect Quad Overdraw.",
            {"blend_mode": blend_mode},
        ))
    elif "MASKED" in blend_mode.upper():
        findings.append(_issue(
            "Medium",
            "Material",
            path,
            "Material is masked",
            "Masked foliage, hair, cards, and grates can make BasePass and overdraw expensive.",
            "Inspect Shader Complexity and Quad Overdraw, then reduce card overlap or simplify masks.",
            {"blend_mode": blend_mode},
        ))

    if two_sided:
        findings.append(_issue(
            "Medium",
            "Material",
            path,
            "Material is two-sided",
            "Two-sided materials can double shaded surface work and are risky in VR.",
            "Use one-sided geometry where possible or reserve two-sided shading for close hero assets.",
            {"two_sided": True},
        ))

    return findings


def _actor_label(actor: Any) -> str:
    try:
        return actor.get_actor_label()
    except Exception:
        try:
            return actor.get_name()
        except Exception:
            return "UnknownActor"


def _actor_class(actor: Any) -> str:
    try:
        return actor.get_class().get_name()
    except Exception:
        return type(actor).__name__


def _component_class(component: Any) -> str:
    try:
        return component.get_class().get_name()
    except Exception:
        return type(component).__name__


def _actor_mobility(component: Any) -> str:
    value = _safe_prop(component, "mobility", "")
    return str(value)


def _iter_actor_components(actor: Any) -> List[Any]:
    try:
        import unreal
        if hasattr(unreal, "ActorComponent"):
            return list(actor.get_components_by_class(unreal.ActorComponent))
    except Exception:
        pass
    try:
        return list(actor.get_components())
    except Exception:
        try:
            return list(_safe_prop(actor, "components", []) or [])
        except Exception:
            return []


def _find_static_mesh_component_mesh(component: Any) -> str:
    mesh = _safe_prop(component, "static_mesh")
    return _material_name(mesh) if mesh else ""


def _scene_findings(unreal_module: Any, include_design_advice: bool = True) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    findings: List[Dict[str, Any]] = []
    summary: Dict[str, Any] = {
        "actor_count": 0,
        "class_counts": {},
        "movable_lights": 0,
        "shadow_casting_movable_lights": 0,
        "large_radius_lights": 0,
        "ticking_actors": 0,
        "static_mesh_components": 0,
        "skeletal_mesh_components": 0,
        "particle_components": 0,
        "repeated_static_meshes": [],
        "streaming_levels": [],
    }

    world = unreal_module.EditorLevelLibrary.get_editor_world()
    if not world:
        return findings, summary

    try:
        actors = list(unreal_module.EditorLevelLibrary.get_all_level_actors())
    except Exception:
        actors = []
    summary["actor_count"] = len(actors)

    mesh_instances: Dict[str, int] = {}
    high_radius_light_labels: List[str] = []
    shadow_light_labels: List[str] = []
    ticking_labels: List[str] = []

    for actor in actors:
        cls = _actor_class(actor)
        summary["class_counts"][cls] = summary["class_counts"].get(cls, 0) + 1
        label = _actor_label(actor)

        can_tick = False
        try:
            can_tick = bool(actor.get_actor_tick_enabled())
        except Exception:
            can_tick = bool(_safe_prop(actor, "allow_tick_before_begin_play", False))
        if can_tick:
            summary["ticking_actors"] += 1
            if len(ticking_labels) < 20:
                ticking_labels.append(label)

        components = _iter_actor_components(actor)
        for component in components:
            ccls = _component_class(component)
            ccls_lower = ccls.lower()
            if "staticmeshcomponent" in ccls_lower:
                summary["static_mesh_components"] += 1
                mesh_path = _find_static_mesh_component_mesh(component)
                if mesh_path:
                    mesh_instances[mesh_path] = mesh_instances.get(mesh_path, 0) + 1
            elif "skeletalmeshcomponent" in ccls_lower:
                summary["skeletal_mesh_components"] += 1
            elif "particle" in ccls_lower or "niagara" in ccls_lower:
                summary["particle_components"] += 1

            if "lightcomponent" in ccls_lower or ccls_lower.endswith("lightcomponent"):
                mobility = _actor_mobility(component).upper()
                is_movable = "MOVABLE" in mobility
                casts_shadow = bool(_safe_prop(component, "cast_shadows", False))
                radius = _safe_prop(component, "attenuation_radius", 0.0)
                try:
                    radius_value = float(radius or 0.0)
                except Exception:
                    radius_value = 0.0
                if is_movable:
                    summary["movable_lights"] += 1
                if is_movable and casts_shadow:
                    summary["shadow_casting_movable_lights"] += 1
                    if len(shadow_light_labels) < 20:
                        shadow_light_labels.append(label)
                if radius_value >= 1800:
                    summary["large_radius_lights"] += 1
                    if len(high_radius_light_labels) < 20:
                        high_radius_light_labels.append(label)

    repeated = sorted(mesh_instances.items(), key=lambda item: item[1], reverse=True)
    summary["repeated_static_meshes"] = [
        {"mesh": mesh, "instances": count}
        for mesh, count in repeated[:20]
        if count >= 25
    ]

    try:
        levels = unreal_module.EditorLevelUtils.get_levels(world)
        for level in levels:
            summary["streaming_levels"].append(str(level.get_name()))
    except Exception:
        try:
            streaming_levels = world.get_streaming_levels()
            summary["streaming_levels"] = [str(level.get_name()) for level in streaming_levels]
        except Exception:
            pass

    if summary["actor_count"] >= 1200:
        findings.append(_issue(
            "High",
            "Scene Setup",
            "Current level",
            f"High actor count: {summary['actor_count']}",
            "Large actor counts can increase editor overhead, visibility cost, and per-frame update pressure.",
            "Merge non-interactive detail props, use HLODs, instance repeated objects, and split far areas into streaming levels.",
            {"actor_count": summary["actor_count"]},
        ))

    if summary["shadow_casting_movable_lights"] > 0:
        findings.append(_issue(
            "High" if summary["shadow_casting_movable_lights"] >= 4 else "Medium",
            "Lighting",
            "Current level",
            f"{summary['shadow_casting_movable_lights']} movable shadow-casting lights found",
            "Movable shadows often show up in Shadow Depths and can dominate GPU time.",
            "Bake or make lights stationary where possible, disable shadows on small accent lights, reduce radius, or fake distant highlights with emissive materials.",
            {"examples": shadow_light_labels},
        ))

    if summary["large_radius_lights"] > 0:
        findings.append(_issue(
            "Medium",
            "Lighting",
            "Current level",
            f"{summary['large_radius_lights']} lights have large attenuation radius",
            "Large lights affect more primitives and can inflate lighting and shadow cost.",
            "Shrink radius to the area that must visibly react, split lighting into baked fill plus small non-shadowing accents.",
            {"examples": high_radius_light_labels},
        ))

    if summary["ticking_actors"] >= 100:
        findings.append(_issue(
            "High",
            "CPU Profiling",
            "Current level",
            f"{summary['ticking_actors']} actors have ticking enabled",
            "Per-frame actor Tick work is a common Game thread bottleneck.",
            "Replace Tick with events or timers, disable tick when idle, and pool actors that spawn frequently.",
            {"examples": ticking_labels},
        ))

    for repeated_mesh in summary["repeated_static_meshes"][:5]:
        findings.append(_issue(
            "Medium" if repeated_mesh["instances"] < 200 else "High",
            "Scene Setup",
            repeated_mesh["mesh"],
            f"{repeated_mesh['instances']} instances of the same static mesh found",
            "Many separate repeated actors can increase draw, visibility, and actor management cost.",
            "Use InstancedStaticMesh, HierarchicalInstancedStaticMesh, merged prop groups, or HLOD where interaction is not needed.",
            repeated_mesh,
        ))

    if include_design_advice and summary["actor_count"] >= 800:
        findings.append(_issue(
            "Medium",
            "Design Around",
            "Current camera path",
            "Scene may expose too much content at once",
            "Long sightlines force the renderer to consider more actors, lights, shadows, and materials.",
            "Use corners, doors, curtains, fog, darkness, vehicles, debris piles, or layout bends to block expensive views while improving composition.",
            {"actor_count": summary["actor_count"]},
        ))

    return findings, summary


def _structured_asset_audit(
    unreal_module: Any,
    root: str,
    recursive: bool,
    max_assets: int,
    max_findings: int,
    max_material_slots: int,
    large_texture_px: int,
) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    findings: List[Dict[str, Any]] = []
    summary: Dict[str, Any] = {
        "root": root,
        "scanned_assets": 0,
        "static_meshes": 0,
        "skeletal_meshes": 0,
        "materials": 0,
        "textures": 0,
        "skipped_after_limit": False,
    }

    asset_paths = _list_assets(unreal_module, root, recursive)
    if max_assets > 0 and len(asset_paths) > max_assets:
        asset_paths = asset_paths[:max_assets]
        summary["skipped_after_limit"] = True

    for path in asset_paths:
        if len(findings) >= max_findings:
            break
        try:
            asset_data = unreal_module.EditorAssetLibrary.find_asset_data(path)
            class_name = _asset_class_name(asset_data)
        except Exception:
            class_name = ""

        if not _class_matches(class_name, ["StaticMesh", "SkeletalMesh", "Texture2D", "Material", "MaterialInstance"]):
            continue

        try:
            asset = unreal_module.EditorAssetLibrary.load_asset(path)
        except Exception:
            asset = None
        if asset is None:
            continue

        summary["scanned_assets"] += 1
        if _class_matches(class_name, ["StaticMesh"]) and not _class_matches(class_name, ["SkeletalMesh"]):
            summary["static_meshes"] += 1
            findings.extend(_static_mesh_findings(unreal_module, asset, path, max_material_slots))
        elif _class_matches(class_name, ["SkeletalMesh"]):
            summary["skeletal_meshes"] += 1
            findings.extend(_skeletal_mesh_findings(asset, path))
        elif _class_matches(class_name, ["Texture2D"]):
            summary["textures"] += 1
            findings.extend(_texture_findings(asset, path, large_texture_px))
        elif _class_matches(class_name, ["Material"]):
            summary["materials"] += 1
            findings.extend(_material_findings(asset, path))

    return findings[:max_findings], summary


def _classify_from_findings(findings: List[Dict[str, Any]], include_vr: bool = False) -> Tuple[str, List[str], List[str]]:
    findings = _normalize_findings(findings)
    category_counts: Dict[str, int] = {}
    high_counts: Dict[str, int] = {}
    for finding in findings:
        category = str(finding.get("category", "Unknown"))
        category_counts[category] = category_counts.get(category, 0) + 1
        if str(finding.get("severity", "")).lower() == "high":
            high_counts[category] = high_counts.get(category, 0) + 1

    if high_counts:
        likely_category = sorted(high_counts.items(), key=lambda item: item[1], reverse=True)[0][0]
    elif category_counts:
        likely_category = sorted(category_counts.items(), key=lambda item: item[1], reverse=True)[0][0]
    else:
        likely_category = "Unknown"

    mapping = {
        "Lighting": "GPU",
        "Material": "GPU",
        "Texture": "Streaming/Memory",
        "Lightmaps": "Streaming/Memory",
        "Static Mesh": "Draw",
        "Scene Setup": "Draw",
        "Skeletal Mesh": "Game",
        "CPU Profiling": "Game",
        "Design Around": "Scene Design",
    }
    bottleneck = mapping.get(likely_category, "Needs evidence")
    evidence = []
    next_steps = []

    for finding in findings[:8]:
        evidence.append(f"{finding.get('severity')}: {finding.get('issue')} ({finding.get('item')})")

    if bottleneck == "GPU":
        next_steps = [
            "Run profilegpu and inspect Shadow Depths, BasePass, Translucency, and PostProcessing.",
            "Capture Shader Complexity, Quad Overdraw, Light Complexity, and Stationary Light Overlap.",
            "Disable or reduce small movable shadows, then re-test stat unit.",
        ]
    elif bottleneck == "Draw":
        next_steps = [
            "Check stat scenerendering, stat rhi, and stat initviews.",
            "Instance repeated props, merge non-interactive clusters, and enable HLOD or culling.",
            "Break up long sightlines so fewer primitives are visible at once.",
        ]
    elif bottleneck == "Game":
        next_steps = [
            "Run stat game, stat blueprint, stat anim, stat physics, and stat particles.",
            "Disable unnecessary Tick, replace polling with events or timers, and reduce distant animation updates.",
            "Pool frequently spawned actors and simplify hot Blueprint graphs.",
        ]
    elif bottleneck == "Streaming/Memory":
        next_steps = [
            "Run stat streaming, stat memory, and stat levels.",
            "Reduce 4K textures on small props and remove unnecessary Never Stream settings.",
            "Use Size Map and Reference Viewer to find dependency chains.",
        ]
    elif bottleneck == "Scene Design":
        next_steps = [
            "Add occluders, layout bends, doors, curtains, darkness, fog, or props to block expensive views.",
            "Keep hero expensive effects close and short-lived.",
            "Retest from player sightlines after each layout change.",
        ]
    else:
        next_steps = [
            "Start with stat unit and stat fps.",
            "Branch based on the highest value: Game, Draw, or GPU.",
            "Run asset and scene audits to add structured evidence.",
        ]

    if include_vr:
        next_steps.insert(0, "For VR, compare all fixes against the selected millisecond budget, not average FPS.")

    return bottleneck, evidence, next_steps


def _filtered_asset_findings(
    unreal_module: Any,
    categories: Iterable[str],
    path: str = "/Game/",
    max_assets: int = 500,
    max_findings: int = 80,
    large_texture_px: int = 2048,
) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    category_set = set(categories)
    asset_findings, asset_summary = _structured_asset_audit(
        unreal_module,
        path,
        True,
        max_assets,
        max_findings,
        4,
        large_texture_px,
    )
    return [finding for finding in asset_findings if finding.get("category") in category_set], asset_summary


def _gpu_structured_findings(unreal_module: Any, params: Dict[str, Any]) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    findings: List[Dict[str, Any]] = []
    summaries: Dict[str, Any] = {}

    scene_findings, scene_summary = _scene_findings(unreal_module, include_design_advice=True)
    summaries["scene"] = scene_summary
    findings.extend([
        finding for finding in scene_findings
        if finding.get("category") in {"Lighting", "Scene Setup", "Design Around"}
    ])

    material_findings, asset_summary = _filtered_asset_findings(
        unreal_module,
        {"Material", "Texture"},
        path=str(params.get("path", "/Game/") or "/Game/"),
        max_assets=int(params.get("max_assets", 500)),
        max_findings=int(params.get("max_findings", 80)),
        large_texture_px=int(params.get("large_texture_px", 2048)),
    )
    summaries["asset"] = asset_summary
    findings.extend(material_findings)

    particle_components = int(scene_summary.get("particle_components", 0) or 0)
    if particle_components >= 40:
        findings.append(_issue(
            "Medium",
            "GPU Profiling",
            "Current level",
            f"{particle_components} particle components may contribute to overdraw",
            "Particles often use translucent materials that increase GPU cost and fill rate.",
            "Cull distant emitters, reduce overlapping cards, simplify particle materials, and inspect Quad Overdraw.",
            {"particle_components": particle_components},
        ))

    return findings, summaries


def _cpu_structured_findings(unreal_module: Any, params: Dict[str, Any]) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    findings: List[Dict[str, Any]] = []
    summaries: Dict[str, Any] = {}

    scene_findings, scene_summary = _scene_findings(unreal_module, include_design_advice=False)
    summaries["scene"] = scene_summary
    findings.extend([finding for finding in scene_findings if finding.get("category") == "CPU Profiling"])

    skeletal_components = int(scene_summary.get("skeletal_mesh_components", 0) or 0)
    if skeletal_components >= 10:
        findings.append(_issue(
            "Medium",
            "CPU Profiling",
            "Current level",
            f"{skeletal_components} skeletal mesh components found",
            "Skeletal meshes can add Game thread and animation evaluation cost, especially if many are visible.",
            "Enable update-rate optimization, reduce tick rates for distant characters, and consider impostors or pose loops for background characters.",
            {"skeletal_mesh_components": skeletal_components},
        ))

    particle_components = int(scene_summary.get("particle_components", 0) or 0)
    if particle_components >= 40:
        findings.append(_issue(
            "Medium",
            "CPU Profiling",
            "Current level",
            f"{particle_components} particle components found",
            "Many active emitters can add CPU management cost before their GPU overdraw cost is even considered.",
            "Cull inactive emitters, pool effects, shorten lifetimes, and avoid always-on ambience where a baked/faked version works.",
            {"particle_components": particle_components},
        ))

    class_counts = scene_summary.get("class_counts", {}) or {}
    blueprint_actor_count = 0
    if isinstance(class_counts, dict):
        for class_name, count in class_counts.items():
            if str(class_name).endswith("_C"):
                try:
                    blueprint_actor_count += int(count)
                except Exception:
                    pass
    if blueprint_actor_count >= 50:
        findings.append(_issue(
            "Medium",
            "CPU Profiling",
            "Blueprint actors",
            f"{blueprint_actor_count} Blueprint actor instances found",
            "Blueprint actors are fine, but many instances make Tick, construction logic, and component updates worth checking.",
            "Use stat blueprint, disable unnecessary Tick, replace polling with events, and pool actors that spawn repeatedly.",
            {"blueprint_actor_count": blueprint_actor_count},
        ))

    collision_findings, asset_summary = _filtered_asset_findings(
        unreal_module,
        {"Static Mesh", "Skeletal Mesh"},
        path=str(params.get("path", "/Game/") or "/Game/"),
        max_assets=int(params.get("max_assets", 500)),
        max_findings=int(params.get("max_findings", 80)),
        large_texture_px=4096,
    )
    summaries["asset"] = asset_summary
    for finding in collision_findings:
        issue = str(finding.get("issue", "")).lower()
        if "collision" in issue or "skeletal" in str(finding.get("category", "")).lower():
            findings.append(finding)

    return findings, summaries


def _rendering_structured_findings(unreal_module: Any, params: Dict[str, Any]) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    findings: List[Dict[str, Any]] = []
    summaries: Dict[str, Any] = {}

    scene_findings, scene_summary = _scene_findings(unreal_module, include_design_advice=True)
    summaries["scene"] = scene_summary
    findings.extend([
        finding for finding in scene_findings
        if finding.get("category") in {"Scene Setup", "Lighting", "Design Around"}
    ])

    static_mesh_components = int(scene_summary.get("static_mesh_components", 0) or 0)
    if static_mesh_components >= 800:
        findings.append(_issue(
            "Medium",
            "Rendering",
            "Current level",
            f"{static_mesh_components} static mesh components found",
            "Many mesh components can increase visibility work, draw submissions, and render thread pressure.",
            "Use HLOD, merge static prop clusters, instance repeated meshes, and block long sightlines.",
            {"static_mesh_components": static_mesh_components},
        ))

    asset_findings, asset_summary = _filtered_asset_findings(
        unreal_module,
        {"Static Mesh", "Material"},
        path=str(params.get("path", "/Game/") or "/Game/"),
        max_assets=int(params.get("max_assets", 500)),
        max_findings=int(params.get("max_findings", 80)),
        large_texture_px=4096,
    )
    summaries["asset"] = asset_summary
    findings.extend([
        finding for finding in asset_findings
        if "material slots" in str(finding.get("issue", "")).lower()
        or finding.get("category") == "Material"
        or "no authored lods" in str(finding.get("issue", "")).lower()
    ])

    return findings, summaries


def _evidence_from_findings(findings: List[Dict[str, Any]], fallback: str) -> List[str]:
    findings = _normalize_findings(findings)
    evidence = [
        f"{finding.get('severity')} severity, {finding.get('confidence')} confidence: {finding.get('issue')} ({finding.get('item')})"
        for finding in findings[:8]
    ]
    return evidence or [fallback]


def _data_with_findings(
    likely_bottleneck: str,
    findings: List[Dict[str, Any]],
    summaries: Dict[str, Any],
    screenshots: Optional[List[Dict[str, Any]]] = None,
    captures: Optional[List[Dict[str, Any]]] = None,
    commands: Optional[List[str]] = None,
    console_results: Optional[List[Dict[str, Any]]] = None,
    recommended_next_steps: Optional[List[str]] = None,
    fallback_evidence: str = "No structured findings were generated. Review captured overlays manually.",
    max_output_findings: int = 50,
    evidence_type: str = "inferred",
    measurements: Optional[Dict[str, Any]] = None,
    rankings: Optional[Dict[str, Any]] = None,
    dependency_trails: Optional[Dict[str, Any]] = None,
    fix_candidates: Optional[List[Dict[str, Any]]] = None,
    verification: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    findings = _normalize_findings(findings)
    data = {
        "commands": commands or [],
        "console_results": console_results or [],
        "screenshots": screenshots or [],
        "captures": captures or [],
        "likely_bottleneck": likely_bottleneck if findings else "Needs screenshot review",
        "evidence": _evidence_from_findings(findings, fallback_evidence),
        "recommended_next_steps": recommended_next_steps or [],
        "findings": findings[:max_output_findings],
        "finding_count": len(findings),
        "summaries": summaries,
    }
    data.update(opt_schema.enhanced_fields(
        evidence_type=evidence_type,
        measurements=measurements,
        rankings=rankings,
        dependency_trails=dependency_trails,
        fix_candidates=fix_candidates,
        verification=verification,
    ))
    return data


def _write_report(unreal_module: Any, payload: Dict[str, Any], basename: str = "optimization_report") -> str:
    out_dir = _optimization_dir(unreal_module)
    report_path = os.path.join(out_dir, f"{basename}_{_timestamp()}.md")
    data = payload.get("data", payload)
    lines = [
        "# Optimization Report",
        "",
        f"Generated: {datetime.datetime.now().isoformat(timespec='seconds')}",
        f"Target: {data.get('target', 'desktop_60')}",
        f"Frame budget: {data.get('target_frame_ms', 'unknown')} ms",
        f"Likely bottleneck: {data.get('likely_bottleneck', 'Needs evidence')}",
        "",
        "## Evidence",
    ]
    for item in data.get("evidence", []) or []:
        lines.append(f"- {item}")
    if not data.get("evidence"):
        lines.append("- No structured evidence yet. Use stat overlays and captures next.")

    lines.extend(["", "## Recommended Next Steps"])
    for item in data.get("recommended_next_steps", []) or []:
        lines.append(f"- {item}")

    findings = data.get("findings", []) or []
    lines.extend(["", "## Findings"])
    if findings:
        for finding in findings:
            lines.extend([
                f"### [{finding.get('severity', 'Info')}] {finding.get('item', 'Unknown')}",
                f"Confidence: {finding.get('confidence', 'Medium')}",
                f"Fix type: {finding.get('fix_type', 'Review Required')}",
                f"Issue: {finding.get('issue', '')}",
                f"Why this matters: {finding.get('why', '')}",
                f"Suggested fix: {finding.get('suggested_fix', '')}",
                "",
            ])
    else:
        lines.append("No findings were generated.")

    screenshots = data.get("screenshots", []) or []
    if screenshots:
        lines.extend(["", "## Screenshots"])
        for screenshot in screenshots:
            if isinstance(screenshot, dict):
                lines.append(f"- {screenshot.get('label', 'Screenshot')}: {screenshot.get('filepath')}")
            else:
                lines.append(f"- {screenshot}")

    opt_reports.append_enhanced_sections(lines, data)

    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines).rstrip() + "\n")
    return report_path


def handle_optimization_tool_catalog(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return the searchable UE4.27 optimization command catalog."""
    query = str(params.get("query", "") or "").lower()
    category = str(params.get("category", "") or "").lower()
    tools = []
    for item in CONSOLE_CATALOG:
        haystack = " ".join([
            str(item.get("command", "")),
            str(item.get("category", "")),
            str(item.get("answers", "")),
            str(item.get("use_when", "")),
            " ".join(item.get("bad_signs", [])),
            " ".join(item.get("next_actions", [])),
        ]).lower()
        if query and query not in haystack:
            continue
        if category and category not in str(item.get("category", "")).lower():
            continue
        tools.append(item)
    return _ok({
        "count": len(tools),
        "tools": tools,
        "targets": TARGET_FRAME_BUDGETS_MS,
        "viewmodes": sorted(VIEWMODE_COMMANDS.keys()),
    })


def handle_optimization_run_console_checklist(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run a safe optimization console command checklist and return inferred findings."""
    try:
        import unreal

        requested = params.get("commands") or DEFAULT_TRIAGE_COMMANDS
        commands = [str(cmd) for cmd in requested if str(cmd) in {item["command"] for item in CONSOLE_CATALOG}]
        if not commands:
            return _err("No supported optimization commands were requested")

        delay = float(params.get("delay_seconds", 0.15))
        capture_screenshots = bool(params.get("capture_screenshots", False))
        width = int(params.get("width", 1920))
        height = int(params.get("height", 1080))

        results = []
        for command in commands:
            results.append(_execute_console(unreal, command))
            if delay > 0:
                time.sleep(delay)

        screenshots = []
        if capture_screenshots:
            shot = _capture_viewport(f"optimization_console_{_timestamp()}.png", width, height)
            if shot.get("success"):
                data = shot.get("data", {})
                screenshots.append({"label": "console_overlays", "filepath": data.get("filepath")})

        include_structured = bool(params.get("include_structured_findings", True))
        command_text = " ".join(commands).lower()
        findings: List[Dict[str, Any]] = []
        summaries: Dict[str, Any] = {}
        likely_bottleneck = "Needs screenshot review"
        next_steps = [
            "Review the captured stat overlay screenshot for the actual millisecond numbers.",
            "Use the structured findings below to decide which specialized capture to run next.",
        ]

        if include_structured and ("gpu" in command_text or "profilegpu" in command_text):
            gpu_findings, gpu_summaries = _gpu_structured_findings(unreal, params)
            findings.extend(gpu_findings)
            summaries.update(gpu_summaries)
            likely_bottleneck = "GPU"
            next_steps.append("Run GPU Capture if lighting, material, or overdraw findings appear near the top.")
        if include_structured and ("game" in command_text or "blueprint" in command_text or "anim" in command_text or "physics" in command_text or "particles" in command_text):
            cpu_findings, cpu_summaries = _cpu_structured_findings(unreal, params)
            findings.extend(cpu_findings)
            summaries.update({f"cpu_{key}": value for key, value in cpu_summaries.items()})
            if likely_bottleneck == "Needs screenshot review":
                likely_bottleneck = "Game"
            next_steps.append("Run CPU Capture if Game thread, Blueprint, animation, physics, or particles look high.")
        if include_structured and ("scenerendering" in command_text or "rhi" in command_text or "initviews" in command_text):
            render_findings, render_summaries = _rendering_structured_findings(unreal, params)
            findings.extend(render_findings)
            summaries.update({f"render_{key}": value for key, value in render_summaries.items()})
            if likely_bottleneck == "Needs screenshot review":
                likely_bottleneck = "Draw"
            next_steps.append("Use HLOD, culling, instancing, or layout occlusion if Draw or InitViews looks high.")

        return _ok(_data_with_findings(
            likely_bottleneck,
            findings,
            summaries,
            screenshots=screenshots,
            commands=commands,
            console_results=results,
            recommended_next_steps=next_steps,
            fallback_evidence="Console commands ran, but UE4.27 returns stat values visually. Review the screenshot for exact numbers.",
            max_output_findings=int(params.get("max_output_findings", 50)),
        ))
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_viewmode_capture_set(params: Dict[str, Any]) -> Dict[str, Any]:
    """Capture selected optimization viewmodes as screenshots."""
    try:
        import unreal

        viewmodes = params.get("viewmodes") or [
            "shader_complexity",
            "quad_overdraw",
            "light_complexity",
            "stationary_light_overlap",
        ]
        width = int(params.get("width", 1920))
        height = int(params.get("height", 1080))
        delay = float(params.get("delay_seconds", 0.35))
        captures = []

        for mode in viewmodes:
            mode_key = str(mode).lower()
            command = VIEWMODE_COMMANDS.get(mode_key)
            if not command:
                captures.append({"viewmode": mode_key, "success": False, "error": "Unsupported viewmode"})
                continue
            command_result = _execute_console(unreal, command)
            if delay > 0:
                time.sleep(delay)
            shot = _capture_viewport(f"optimization_{mode_key}_{_timestamp()}.png", width, height)
            captures.append({
                "viewmode": mode_key,
                "console_command": command,
                "command_result": command_result,
                "success": bool(shot.get("success")),
                "filepath": shot.get("data", {}).get("filepath"),
                "error": shot.get("error", ""),
            })

        if bool(params.get("restore_lit", True)):
            _execute_console(unreal, VIEWMODE_COMMANDS["lit"])

        findings: List[Dict[str, Any]] = []
        summaries: Dict[str, Any] = {}
        viewmode_text = " ".join(str(mode).lower() for mode in viewmodes)
        if any(key in viewmode_text for key in ("shader", "quad", "light", "stationary")):
            findings, summaries = _gpu_structured_findings(unreal, params)

        return _ok(_data_with_findings(
            "GPU",
            findings,
            summaries,
            captures=captures,
            recommended_next_steps=[
                "Open the captured viewmode screenshots and compare the worst colors near the player camera.",
                "If Shader Complexity or Quad Overdraw is hot, simplify masked/translucent materials and reduce overlapping cards.",
                "If Light Complexity or Stationary Light Overlap is hot, reduce overlapping lights and disable small dynamic shadows.",
            ],
            fallback_evidence="Viewmode screenshots captured. Review image colors to confirm material, overdraw, or light overlap cost.",
            max_output_findings=int(params.get("max_output_findings", 50)),
        ))
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_gpu_capture(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run GPU-focused commands and capture useful render viewmodes."""
    try:
        import unreal

        commands = ["stat unit", "stat gpu", "profilegpu"]
        results = [_execute_console(unreal, command) for command in commands]
        capture_result = handle_optimization_viewmode_capture_set({
            "viewmodes": params.get("viewmodes") or [
                "shader_complexity",
                "quad_overdraw",
                "light_complexity",
                "stationary_light_overlap",
            ],
            "width": params.get("width", 1920),
            "height": params.get("height", 1080),
            "restore_lit": params.get("restore_lit", True),
        })
        findings, summaries = _gpu_structured_findings(unreal, params)
        data = _data_with_findings(
            "GPU",
            findings,
            summaries,
            captures=capture_result.get("data", {}).get("captures", []),
            commands=commands,
            console_results=results,
            recommended_next_steps=[
                "Inspect profilegpu for Shadow Depths, BasePass, Translucency, and PostProcessing.",
                "Compare Shader Complexity and Quad Overdraw screenshots near the player camera.",
                "Retest stat unit after disabling or reducing one suspected GPU cost.",
            ],
            fallback_evidence="GPU overlays and viewmodes were captured. Review profilegpu and screenshots for exact pass cost.",
            max_output_findings=int(params.get("max_output_findings", 50)),
        )
        if bool(params.get("generate_report", True)):
            data["report_path"] = _write_report(unreal, {"data": data}, "gpu_capture")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_cpu_capture(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run CPU, game thread, animation, physics, Blueprint, and particles overlays."""
    try:
        import unreal

        commands = params.get("commands") or ["stat unit", "stat game", "stat blueprint", "stat anim", "stat physics", "stat particles"]
        console = handle_optimization_run_console_checklist({
            "commands": commands,
            "capture_screenshots": params.get("capture_screenshots", True),
            "width": params.get("width", 1920),
            "height": params.get("height", 1080),
            "include_structured_findings": False,
        })
        findings, summaries = _cpu_structured_findings(unreal, params)
        data = _data_with_findings(
            "Game",
            findings,
            summaries,
            screenshots=console.get("data", {}).get("screenshots", []),
            commands=commands,
            console_results=console.get("data", {}).get("console_results", console.get("data", {}).get("results", [])),
            recommended_next_steps=[
                "Use the stat game screenshot to identify the expensive system bucket.",
                "If Blueprint is high, disable unnecessary Tick and replace polling with events or timers.",
                "If Anim is high, enable update-rate optimization and reduce distant skeletal mesh updates.",
                "If Physics is high, simplify collision and disable simulation on decorative objects.",
                "If Particles are high, cull emitters and pool short-lived effects.",
            ],
            fallback_evidence="CPU overlays were captured. Review the screenshot for exact stat game, Blueprint, animation, physics, and particle costs.",
            max_output_findings=int(params.get("max_output_findings", 50)),
        )
        if bool(params.get("generate_report", True)):
            data["report_path"] = _write_report(unreal, {"data": data}, "cpu_capture")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_rendering_capture(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run render thread and scene rendering overlays."""
    try:
        import unreal

        commands = params.get("commands") or ["stat unit", "stat scenerendering", "stat rhi", "stat initviews"]
        console = handle_optimization_run_console_checklist({
            "commands": commands,
            "capture_screenshots": params.get("capture_screenshots", True),
            "width": params.get("width", 1920),
            "height": params.get("height", 1080),
            "include_structured_findings": False,
        })
        findings, summaries = _rendering_structured_findings(unreal, params)
        data = _data_with_findings(
            "Draw",
            findings,
            summaries,
            screenshots=console.get("data", {}).get("screenshots", []),
            commands=commands,
            console_results=console.get("data", {}).get("console_results", console.get("data", {}).get("results", [])),
            recommended_next_steps=[
                "Use stat scenerendering, stat rhi, and stat initviews screenshots to confirm visible primitives and draw cost.",
                "Instance or merge repeated non-interactive props.",
                "Add HLOD and cull distance rules for background city and repeated detail meshes.",
                "Use layout occluders or bends to reduce how much of the level is visible from key camera positions.",
            ],
            fallback_evidence="Rendering overlays were captured. Review screenshots for exact primitive, draw, and InitViews numbers.",
            max_output_findings=int(params.get("max_output_findings", 50)),
        )
        if bool(params.get("generate_report", True)):
            data["report_path"] = _write_report(unreal, {"data": data}, "rendering_capture")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_memory_streaming_capture(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run memory overlays and return structured memory/streaming findings."""
    try:
        import unreal

        commands = params.get("commands") or ["stat streaming", "stat memory", "stat levels"]
        console = handle_optimization_run_console_checklist({
            "commands": commands,
            "capture_screenshots": params.get("capture_screenshots", True),
            "width": params.get("width", 1920),
            "height": params.get("height", 1080),
        })

        findings: List[Dict[str, Any]] = []
        summaries: Dict[str, Any] = {}

        include_asset_audit = bool(params.get("include_asset_audit", True))
        if include_asset_audit:
            asset_findings, asset_summary = _structured_asset_audit(
                unreal,
                str(params.get("path", "/Game/") or "/Game/"),
                bool(params.get("recursive", True)),
                int(params.get("max_assets", 900)),
                int(params.get("max_findings", 120)),
                int(params.get("max_material_slots", 4)),
                int(params.get("large_texture_px", 2048)),
            )
            memory_categories = {"Texture", "Lightmaps", "Material"}
            findings.extend([finding for finding in asset_findings if finding.get("category") in memory_categories])
            summaries["asset"] = asset_summary

        scene_findings, scene_summary = _scene_findings(unreal, include_design_advice=False)
        summaries["scene"] = scene_summary
        streaming_levels = scene_summary.get("streaming_levels", [])
        actor_count = int(scene_summary.get("actor_count", 0) or 0)

        if streaming_levels == ["PersistentLevel"] and actor_count >= 800:
            findings.append(_issue(
                "Medium",
                "Streaming and Memory",
                "Current level",
                "Only PersistentLevel is loaded for a large scene",
                "A large single loaded level can keep distant content resident and make memory problems harder to isolate.",
                "Split far interiors, background city sections, or combat set pieces into streaming levels where it fits the design.",
                {"actor_count": actor_count, "streaming_levels": streaming_levels},
            ))

        particle_components = int(scene_summary.get("particle_components", 0) or 0)
        if particle_components >= 50:
            findings.append(_issue(
                "Medium",
                "Streaming and Memory",
                "Current level",
                f"{particle_components} particle components found",
                "Particle systems can hold textures, materials, meshes, and buffers resident while also adding overdraw.",
                "Cull distant emitters, shorten effect lifetime, and check whether background effects can be baked or faked.",
                {"particle_components": particle_components},
            ))

        texture_findings = [finding for finding in findings if finding.get("category") == "Texture"]
        lightmap_findings = [finding for finding in findings if finding.get("category") == "Lightmaps"]
        evidence: List[str] = []
        if texture_findings:
            evidence.append(f"{len(texture_findings)} texture memory risks found")
        if lightmap_findings:
            evidence.append(f"{len(lightmap_findings)} high lightmap memory risks found")
        if streaming_levels:
            evidence.append(f"Loaded levels: {', '.join(str(level) for level in streaming_levels[:6])}")
        if not evidence:
            evidence.append("No structured memory risk was found, but stat overlay screenshots should still be reviewed.")

        recommended_next_steps = [
            "Open the captured stat streaming screenshot and check texture pool budget and wanted/current mips.",
            "Prioritize large textures on small props and textures marked Never Stream.",
            "Use Size Map and Reference Viewer on the top flagged assets to find heavy dependency chains.",
            "If the level is mostly PersistentLevel, consider streaming far interiors or background city chunks.",
            "Re-run Memory after changing one asset group so the overlay is comparable.",
        ]

        data = {
            "commands": commands,
            "console_results": console.get("data", {}).get("results", []),
            "screenshots": console.get("data", {}).get("screenshots", []),
            "likely_bottleneck": "Streaming/Memory" if findings else "Needs screenshot review",
            "evidence": evidence,
            "recommended_next_steps": recommended_next_steps,
            "findings": findings[: int(params.get("max_output_findings", 50))],
            "finding_count": len(findings),
            "summaries": summaries,
            "note": "UE4.27 memory stat values are visual overlays, so this combines screenshots with structured asset and level checks.",
        }
        data.update(opt_schema.enhanced_fields(evidence_type="mixed"))
        if bool(params.get("generate_report", True)):
            data["report_path"] = _write_report(unreal, {"data": data}, "memory_streaming")

        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_asset_audit(params: Dict[str, Any]) -> Dict[str, Any]:
    """Inspect project assets for common optimization risks."""
    try:
        import unreal

        root = str(params.get("path", "/Game/") or "/Game/")
        recursive = bool(params.get("recursive", True))
        max_assets = int(params.get("max_assets", 800))
        max_findings = int(params.get("max_findings", 100))
        max_material_slots = int(params.get("max_material_slots", 4))
        large_texture_px = int(params.get("large_texture_px", 4096))
        findings, summary = _structured_asset_audit(
            unreal,
            root,
            recursive,
            max_assets,
            max_findings,
            max_material_slots,
            large_texture_px,
        )
        likely_bottleneck, evidence, recommended_next_steps = _classify_from_findings(findings)
        data = {
            "likely_bottleneck": likely_bottleneck,
            "evidence": evidence,
            "recommended_next_steps": recommended_next_steps,
            "summary": summary,
            "findings": findings,
            "finding_count": len(findings),
        }
        data.update(opt_schema.enhanced_fields(evidence_type="inferred"))
        if bool(params.get("generate_report", False)):
            data["report_path"] = _write_report(unreal, {"data": data}, "asset_audit")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_scene_audit(params: Dict[str, Any]) -> Dict[str, Any]:
    """Inspect the active level for lighting, culling, tick, and setup risks."""
    try:
        import unreal

        findings, summary = _scene_findings(unreal, include_design_advice=bool(params.get("include_design_advice", True)))
        likely_bottleneck, evidence, recommended_next_steps = _classify_from_findings(findings)
        data = {
            "likely_bottleneck": likely_bottleneck,
            "evidence": evidence,
            "recommended_next_steps": recommended_next_steps,
            "summary": summary,
            "findings": findings,
            "finding_count": len(findings),
        }
        data.update(opt_schema.enhanced_fields(evidence_type="inferred"))
        if bool(params.get("generate_report", False)):
            data["report_path"] = _write_report(unreal, {"data": data}, "scene_audit")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_vr_audit(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run a VR-focused audit against a selected refresh-rate budget."""
    try:
        target = str(params.get("target", "vr_90") or "vr_90")
        target_frame_ms = _target_budget_ms(target, params.get("custom_frame_ms"))
        scene = handle_optimization_scene_audit({"include_design_advice": True})
        asset = handle_optimization_asset_audit({
            "path": params.get("path", "/Game/"),
            "recursive": params.get("recursive", True),
            "max_assets": params.get("max_assets", 500),
            "max_findings": params.get("max_findings", 80),
            "large_texture_px": params.get("large_texture_px", 2048),
        }) if bool(params.get("include_asset_audit", True)) else _ok({"findings": [], "summary": {}})

        findings = []
        findings.extend(scene.get("data", {}).get("findings", []))
        findings.extend(asset.get("data", {}).get("findings", []))

        bottleneck, evidence, next_steps = _classify_from_findings(findings, include_vr=True)
        vr_steps = [
            "Disable expensive post-process features during VR testing: SSR, heavy AO, motion blur, and high bloom.",
            "Prefer baked lighting, one hero movable light near the player, and non-shadowing accents.",
            "Use doors, corners, curtains, darkness, fog, or props to prevent seeing too many rooms at once.",
            "Move smoke, glass, and translucent cards away from the camera and avoid layered overdraw.",
        ]
        data = {
            "target": target,
            "target_frame_ms": target_frame_ms,
            "likely_bottleneck": bottleneck,
            "evidence": evidence,
            "recommended_next_steps": next_steps + vr_steps,
            "findings": findings,
            "scene_summary": scene.get("data", {}).get("summary", {}),
            "asset_summary": asset.get("data", {}).get("summary", {}),
        }
        data.update(opt_schema.enhanced_fields(evidence_type="inferred"))
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_quick_triage(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run first-pass optimization evidence capture, audits, classification, and optional report."""
    try:
        import unreal

        target = str(params.get("target", "desktop_60") or "desktop_60")
        target_frame_ms = _target_budget_ms(target, params.get("custom_frame_ms"))
        capture_screenshots = bool(params.get("capture_screenshots", True))
        include_asset_audit = bool(params.get("include_asset_audit", True))
        include_scene_audit = bool(params.get("include_scene_audit", True))
        include_vr_checks = bool(params.get("include_vr_checks", False))

        console = handle_optimization_run_console_checklist({
            "commands": params.get("commands") or DEFAULT_TRIAGE_COMMANDS,
            "capture_screenshots": capture_screenshots,
            "width": params.get("width", 1920),
            "height": params.get("height", 1080),
        })

        findings: List[Dict[str, Any]] = []
        summaries: Dict[str, Any] = {}
        if include_asset_audit:
            asset = handle_optimization_asset_audit({
                "path": params.get("path", "/Game/"),
                "recursive": params.get("recursive", True),
                "max_assets": params.get("max_assets", 600),
                "max_findings": params.get("max_findings", 100),
            })
            findings.extend(asset.get("data", {}).get("findings", []))
            summaries["asset"] = asset.get("data", {}).get("summary", {})
        if include_scene_audit:
            scene = handle_optimization_scene_audit({"include_design_advice": True})
            findings.extend(scene.get("data", {}).get("findings", []))
            summaries["scene"] = scene.get("data", {}).get("summary", {})

        if include_vr_checks:
            vr = handle_optimization_vr_audit({
                "target": target if target.startswith("vr_") else "vr_90",
                "include_asset_audit": False,
            })
            findings.extend(vr.get("data", {}).get("findings", []))

        likely_bottleneck, evidence, recommended_next_steps = _classify_from_findings(findings, include_vr=include_vr_checks)
        screenshots = console.get("data", {}).get("screenshots", [])
        data = {
            "target": target,
            "target_frame_ms": target_frame_ms,
            "likely_bottleneck": likely_bottleneck,
            "evidence": evidence,
            "recommended_next_steps": recommended_next_steps,
            "screenshots": screenshots,
            "findings": findings[: int(params.get("max_output_findings", 50))],
            "finding_count": len(findings),
            "summaries": summaries,
            "console_results": console.get("data", {}).get("console_results", console.get("data", {}).get("results", [])),
            "note": "Stat overlays are visual in UE4.27. Use screenshots and structured audits together.",
        }
        data.update(opt_schema.enhanced_fields(evidence_type="mixed"))

        if bool(params.get("generate_report", False)):
            data["report_path"] = _write_report(unreal, {"data": data}, "quick_triage")

        # Persist an agent-ready context payload so the MCP server (or any
        # agent session) can pull the bundled triage evidence in one read
        # instead of re-running the audits. Overwritten on every triage run.
        try:
            context_payload = {
                "generated_at": datetime.datetime.utcnow().replace(microsecond=0).isoformat() + "Z",
                "source": "optimization_quick_triage",
                "target": target,
                "target_frame_ms": target_frame_ms,
                "likely_bottleneck": likely_bottleneck,
                "evidence": evidence,
                "recommended_next_steps": recommended_next_steps,
                "summaries": summaries,
                "findings": findings[: int(params.get("max_output_findings", 50))],
                "finding_count": len(findings),
                "report_path": data.get("report_path"),
            }
            context_dir = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "MCP")
            os.makedirs(context_dir, exist_ok=True)
            context_path = os.path.join(context_dir, "optimization_context.json")
            with open(context_path, "w", encoding="utf-8") as fh:
                json.dump(context_payload, fh, indent=2, sort_keys=True, default=str)
            data["context_path"] = context_path
        except Exception as context_exc:
            data["context_error"] = f"Context payload not written: {context_exc}"

        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def _canonical_asset_path(path: Any) -> str:
    text = str(path or "")
    if "." in text:
        left, right = text.rsplit(".", 1)
        if "/" in left and right:
            return left
    return text


def _texture_info(unreal_module: Any, path: str, usage: Dict[str, Any]) -> Dict[str, Any]:
    asset = unreal_module.EditorAssetLibrary.load_asset(path)
    size_x = 0
    size_y = 0
    for method_name in ("blueprint_get_size_x", "get_size_x"):
        try:
            size_x = int(getattr(asset, method_name)())
            break
        except Exception:
            pass
    for method_name in ("blueprint_get_size_y", "get_size_y"):
        try:
            size_y = int(getattr(asset, method_name)())
            break
        except Exception:
            pass
    mip_setting = str(_safe_prop(asset, "mip_gen_settings", ""))
    format_name = str(_safe_prop(asset, "compression_settings", ""))
    importance_tag = ""
    try:
        importance_tag = str(unreal_module.EditorAssetLibrary.get_metadata_tag(asset, "OptimizationImportance") or "")
    except Exception:
        pass
    row = {
        "path": path,
        "width": size_x,
        "height": size_y,
        "format": format_name,
        "never_stream": bool(_safe_prop(asset, "never_stream", False)),
        "mipmaps_disabled": "NO_MIPMAPS" in mip_setting.upper(),
        "mip_gen_settings": mip_setting,
        "importance_tag": importance_tag,
    }
    row["memory_bytes"] = opt_ranking.estimate_texture_memory_bytes(size_x, size_y, format_name)
    row["usage"] = usage
    row["component_count"] = int(usage.get("component_count") or 0)
    row["material_count"] = int(usage.get("material_count") or 0)
    row["material_risk"] = float(usage.get("material_risk") or 0.0)
    row["visibility_weight"] = 1.0 if row["component_count"] > 0 else 0.25
    return row


def _collect_texture_rankings(unreal_module: Any, params: Dict[str, Any]) -> Tuple[List[Dict[str, Any]], Dict[str, Any], Dict[str, Any]]:
    root = str(params.get("path", "/Game/") or "/Game/")
    recursive = bool(params.get("recursive", True))
    max_assets = int(params.get("max_assets", 1200))
    max_items = int(params.get("max_items", 50))
    active_usage = opt_dependencies.collect_active_level_usage(unreal_module)
    texture_usage = {}
    for path, row in (active_usage.get("textures") or {}).items():
        texture_usage[_canonical_asset_path(path)] = row

    texture_rows: List[Dict[str, Any]] = []
    for index, path in enumerate(_list_assets(unreal_module, root, recursive)):
        if max_assets > 0 and index >= max_assets:
            break
        try:
            asset_data = unreal_module.EditorAssetLibrary.find_asset_data(path)
            class_name = _asset_class_name(asset_data)
        except Exception:
            class_name = ""
        if not _class_matches(class_name, ["Texture2D"]):
            continue
        canonical = _canonical_asset_path(path)
        usage = texture_usage.get(canonical, {})
        try:
            texture_rows.append(_texture_info(unreal_module, canonical, usage))
        except Exception:
            continue

    ranked = opt_ranking.rank_textures(texture_rows, max_items=max_items)
    trails: Dict[str, Any] = {}
    for row in ranked:
        path = str(row.get("path") or "")
        trails[path] = opt_dependencies.dependency_trail_for_texture(path, {"textures": texture_usage})
        row["used_by"] = trails[path].get("used_by", {})
        row["dependency_trail"] = trails[path].get("dependency_trail", [])
        row["fix_candidate"] = row.get("risk_level") in {"low", "medium"} and (
            bool(row.get("mipmaps_disabled")) or int(row.get("width") or 0) >= 4096 or int(row.get("height") or 0) >= 4096
        )
    return ranked, trails, active_usage


def _finding_from_texture_rank(row: Dict[str, Any]) -> Dict[str, Any]:
    path = str(row.get("path") or "Unknown texture")
    evidence = {
        "rank_score": row.get("rank_score"),
        "score_components": row.get("score_components"),
        "estimated_memory_mb": row.get("estimated_memory_mb"),
        "width": row.get("width"),
        "height": row.get("height"),
        "used_by": row.get("used_by", {}),
        "dependency_trail": row.get("dependency_trail", []),
    }
    why_parts = [
        f"{row.get('width')}x{row.get('height')}",
        f"{row.get('estimated_memory_mb')} MB estimated",
        f"{row.get('component_count')} active scene uses",
    ]
    if row.get("never_stream"):
        why_parts.append("Never Stream")
    if row.get("mipmaps_disabled"):
        why_parts.append("mipmaps disabled")
    return _issue(
        row.get("severity", "Medium"),
        "Texture",
        path,
        f"Texture harm rank #{row.get('rank')}: score {row.get('rank_score')}",
        "Texture risk combines memory, streaming settings, scene usage, material risk, and active-level relevance: " + ", ".join(why_parts),
        "Review dependency trail first. Enable mipmaps, allow streaming, or cap max texture size for non-hero desktop_60 content where visual loss is acceptable.",
        evidence,
        confidence="High" if int(row.get("component_count") or 0) > 0 else "Medium",
        fix_type="Low Risk Setting Review" if row.get("fix_candidate") else "Review Required",
    )


def handle_optimization_texture_harm_rank(params: Dict[str, Any]) -> Dict[str, Any]:
    """Rank textures by likely harm, with active-level dependency trails."""
    try:
        import unreal

        ranked, trails, active_usage = _collect_texture_rankings(unreal, params)
        findings = [_finding_from_texture_rank(row) for row in ranked[: int(params.get("max_findings", 30))]]
        fix_candidates = opt_verification.build_fix_candidates(findings, {"textures": ranked})
        data = _data_with_findings(
            "Streaming/Memory" if ranked else "Needs evidence",
            findings,
            {"active_level_usage": {"actors": active_usage.get("actors"), "texture_count": len(active_usage.get("textures", {}))}},
            recommended_next_steps=[
                "Start with the highest ranked texture that is used in the active level.",
                "Open the top dependency trail before changing texture settings.",
                "Apply only low-risk candidates, then run before/after verification.",
            ],
            fallback_evidence="No texture ranking could be generated.",
            max_output_findings=int(params.get("max_output_findings", 50)),
            evidence_type="inferred",
            rankings={"textures": ranked},
            dependency_trails=trails,
            fix_candidates=fix_candidates,
        )
        if bool(params.get("generate_report", False)):
            data["report_path"] = _write_report(unreal, {"data": data}, "texture_harm_rank")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def _collect_asset_rankings(unreal_module: Any, params: Dict[str, Any]) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    active_usage = opt_dependencies.collect_active_level_usage(unreal_module)
    rows: List[Dict[str, Any]] = []
    for mesh_path, usage in (active_usage.get("meshes") or {}).items():
        canonical = _canonical_asset_path(mesh_path)
        asset = None
        try:
            asset = unreal_module.EditorAssetLibrary.load_asset(canonical)
        except Exception:
            pass
        lod_count = 0
        material_slots = len(usage.get("materials", {}) or {})
        triangle_count = 0
        complex_collision = False
        if asset:
            try:
                lod_count = int(asset.get_num_lods())
            except Exception:
                pass
            try:
                material_slots = max(material_slots, len(_safe_prop(asset, "static_materials", []) or []))
            except Exception:
                pass
            body_setup = _safe_prop(asset, "body_setup")
            collision_trace = str(_safe_prop(body_setup, "collision_trace_flag", "")) if body_setup else ""
            complex_collision = "USE_COMPLEX_AS_SIMPLE" in collision_trace.upper()
        rows.append({
            "path": canonical,
            "component_count": int(usage.get("component_count") or 0),
            "actor_examples": usage.get("actors", [])[:10],
            "material_slots": material_slots,
            "lod_count": lod_count,
            "triangle_count": triangle_count,
            "shadow_casters": int(usage.get("shadow_casters") or 0),
            "complex_collision": complex_collision,
            "visibility_weight": 1.0,
            "materials": usage.get("materials", {}),
        })
    return opt_ranking.rank_assets(rows, max_items=int(params.get("max_items", 50))), active_usage


def handle_optimization_asset_cost_rank(params: Dict[str, Any]) -> Dict[str, Any]:
    """Rank active-level mesh assets by likely scene cost."""
    try:
        import unreal

        ranked, active_usage = _collect_asset_rankings(unreal, params)
        findings = []
        for row in ranked[: int(params.get("max_findings", 30))]:
            findings.append(_issue(
                row.get("severity", "Medium"),
                "Scene Setup",
                row.get("path", "Unknown asset"),
                f"Asset cost rank #{row.get('rank')}: score {row.get('rank_score')}",
                "Scene cost combines active placement count, material slots, LOD state, shadow casting, collision risk, and visibility relevance.",
                "Prioritize instancing, LODs, material-slot reduction, or shadow review for high-repeat active-level assets.",
                {
                    "rank_score": row.get("rank_score"),
                    "score_components": row.get("score_components"),
                    "actor_examples": row.get("actor_examples", []),
                    "component_count": row.get("component_count"),
                },
                confidence="High",
                fix_type="Content Refactor",
            ))
        data = _data_with_findings(
            "Draw" if ranked else "Needs evidence",
            findings,
            {"active_level_usage": {"actors": active_usage.get("actors"), "mesh_count": len(active_usage.get("meshes", {}))}},
            recommended_next_steps=[
                "Start with high-repeat meshes that also lack LODs or cast shadows.",
                "Use instancing or HLOD for repeated non-interactive props.",
                "Retest stat scenerendering and stat rhi after changing one asset group.",
            ],
            evidence_type="inferred",
            rankings={"assets": ranked},
        )
        if bool(params.get("generate_report", False)):
            data["report_path"] = _write_report(unreal, {"data": data}, "asset_cost_rank")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def _project_saved_dir(unreal_module: Any) -> str:
    return os.path.join(_project_dir(unreal_module), "Saved")


def _project_log_path(unreal_module: Any) -> str:
    try:
        return os.path.join(unreal_module.Paths.project_log_dir(), f"{unreal_module.SystemLibrary.get_game_name()}.log")
    except Exception:
        return ""


def handle_optimization_stat_capture(params: Dict[str, Any]) -> Dict[str, Any]:
    """Capture stat and CSV profiler files where UE4.27 exposes them."""
    try:
        import unreal

        duration = float(params.get("duration_seconds", 3.0))
        before = time.time()
        commands = params.get("commands") or ["stat startfile", "csvprofile start"]
        stop_commands = params.get("stop_commands") or ["csvprofile stop", "stat stopfile"]
        console_results = []
        for command in commands:
            console_results.append(_execute_console(unreal, str(command)))
        if duration > 0:
            time.sleep(duration)
        for command in stop_commands:
            console_results.append(_execute_console(unreal, str(command)))

        saved = _project_saved_dir(unreal)
        recent_csv = profiler_ingestion.find_recent_files(saved, [".csv"], since_time=before)
        recent_stats = profiler_ingestion.find_recent_files(saved, [".ue4stats"], since_time=before)
        csv_results = [profiler_ingestion.parse_csv_file(path) for path in recent_csv[:5]]
        measurements = {
            "csv": {"parsed": any(item.get("parsed") for item in csv_results), "files": csv_results},
            "ue4stats": {"parsed": False, "files": [{"path": path, "parsed": False} for path in recent_stats]},
        }
        evidence_type = "measured" if measurements["csv"]["parsed"] else "visual"
        screenshots = []
        if bool(params.get("capture_screenshot", True)):
            shot = _capture_viewport(f"optimization_stat_capture_{_timestamp()}.png", int(params.get("width", 1920)), int(params.get("height", 1080)))
            if shot.get("success"):
                screenshots.append({"label": "stat_capture_overlay", "filepath": shot.get("data", {}).get("filepath")})
        data = _data_with_findings(
            "Needs evidence",
            [],
            {"recent_csv_count": len(recent_csv), "recent_ue4stats_count": len(recent_stats)},
            screenshots=screenshots,
            commands=list(commands) + list(stop_commands),
            console_results=console_results,
            recommended_next_steps=[
                "Use parsed CSV metrics when present.",
                "If only .ue4stats was produced, open it in Unreal Frontend or Insights and use the visual report path.",
                "Use the overlay screenshot when UE4.27 keeps stat values visual-only.",
            ],
            fallback_evidence="Stat capture ran, but no parseable CSV metrics were found.",
            evidence_type=evidence_type,
            measurements=measurements,
        )
        if bool(params.get("generate_report", False)):
            data["report_path"] = _write_report(unreal, {"data": data}, "stat_capture")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def _insights_state_path(unreal_module: Any) -> str:
    return os.path.join(_optimization_dir(unreal_module), "insights_capture_state.json")


def _summarize_trace_with_helper(unreal_module: Any, trace_path: str) -> Dict[str, Any]:
    summary = profiler_ingestion.summarize_trace_file(trace_path)
    try:
        helper = getattr(unreal_module, "MCPBridgeProfilerLibrary", None)
        if helper and hasattr(helper, "summarize_trace_file"):
            raw = helper.summarize_trace_file(trace_path)
            parsed = json.loads(raw)
            if isinstance(parsed, dict):
                summary.update(parsed)
    except Exception as exc:
        summary["helper_error"] = str(exc)
    return summary


def handle_optimization_insights_trace_start(params: Dict[str, Any]) -> Dict[str, Any]:
    """Start an Unreal Insights trace capture."""
    try:
        import unreal

        channels = str(params.get("channels", "cpu,gpu,frame,bookmark,loadtime,file,counters") or "")
        command = f"trace.start {channels}".strip()
        result = _execute_console(unreal, command)
        state = {"started_at": time.time(), "channels": channels, "command": command}
        with open(_insights_state_path(unreal), "w", encoding="utf-8") as handle:
            json.dump(state, handle, indent=2, sort_keys=True)
        return _ok({
            "commands": [command],
            "console_results": [result],
            "evidence_type": "measured",
            "measurements": {"trace": {"parsed": False, "capture_started": bool(result.get("success"))}},
            "recommended_next_steps": ["Run optimization_insights_trace_stop after reproducing the hitch or slow view."],
        })
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_insights_trace_stop(params: Dict[str, Any]) -> Dict[str, Any]:
    """Stop Unreal Insights trace capture and summarize the newest trace if possible."""
    try:
        import unreal

        result = _execute_console(unreal, "trace.stop")
        time.sleep(float(params.get("settle_seconds", 0.5)))
        state = {}
        try:
            with open(_insights_state_path(unreal), "r", encoding="utf-8") as handle:
                state = json.load(handle)
        except Exception:
            pass
        since = float(state.get("started_at") or (time.time() - 3600.0))
        trace_files = profiler_ingestion.find_recent_files(_project_saved_dir(unreal), [".utrace"], since_time=since, limit=10)
        summary = _summarize_trace_with_helper(unreal, trace_files[0] if trace_files else "")
        return _ok({
            "commands": ["trace.stop"],
            "console_results": [result],
            "evidence_type": "measured" if trace_files else "visual",
            "measurements": {"trace": summary, "trace_files": trace_files},
            "measurement_status": "parsed" if summary.get("parsed") else "capture_only",
            "likely_bottleneck": "Game",
            "recommended_next_steps": [
                "If a .utrace file was saved, open it in Unreal Insights for exact thread timings.",
                "Use optimization_insights_trace_summary when the compiled TraceServices helper is available.",
            ],
        })
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_insights_trace_summary(params: Dict[str, Any]) -> Dict[str, Any]:
    """Summarize an Unreal Insights trace file when parser support is available."""
    try:
        import unreal

        trace_path = str(params.get("trace_path", "") or "")
        if not trace_path:
            files = profiler_ingestion.find_recent_files(_project_saved_dir(unreal), [".utrace"], limit=1)
            trace_path = files[0] if files else ""
        summary = _summarize_trace_with_helper(unreal, trace_path)
        return _ok({
            "likely_bottleneck": "Game",
            "evidence_type": "measured" if summary.get("exists") else "visual",
            "measurements": {"trace": summary},
            "measurement_status": "parsed" if summary.get("parsed") else "capture_only",
            "recommended_next_steps": [
                "Use the .utrace file in Unreal Insights for exact CPU scopes until the TraceServices helper is compiled.",
                "Correlate trace timing with texture and asset rankings to explain bad frames.",
            ],
        })
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_profilegpu_capture(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run ProfileGPU and parse any available text or log output."""
    try:
        import unreal

        before = time.time()
        commands = ["stat unit", "stat gpu", "profilegpu"]
        console_results = [_execute_console(unreal, command) for command in commands]
        time.sleep(float(params.get("settle_seconds", 1.0)))
        screenshots = []
        shot = _capture_viewport(f"optimization_profilegpu_{_timestamp()}.png", int(params.get("width", 1920)), int(params.get("height", 1080)))
        if shot.get("success"):
            screenshots.append({"label": "profilegpu", "filepath": shot.get("data", {}).get("filepath")})
        saved = _project_saved_dir(unreal)
        text_files = profiler_ingestion.find_recent_files(saved, [".txt", ".log"], since_time=before, limit=10)
        log_path = _project_log_path(unreal)
        if log_path and os.path.exists(log_path):
            text_files.insert(0, log_path)
        parsed_gpu = profiler_ingestion.parse_profilegpu_files(text_files[:10])
        findings, summaries = _gpu_structured_findings(unreal, params)
        measurements = {"profilegpu": parsed_gpu}
        data = _data_with_findings(
            "GPU",
            findings,
            summaries,
            screenshots=screenshots,
            commands=commands,
            console_results=console_results,
            recommended_next_steps=[
                "Use parsed ProfileGPU pass timings when present.",
                "If ProfileGPU is visual-only, compare the screenshot with inferred lighting, material, and overdraw risks.",
                "Retest after reducing the highest-cost pass or the highest-ranked inferred risk.",
            ],
            fallback_evidence="ProfileGPU ran, but no parseable pass timings were found.",
            evidence_type="measured" if parsed_gpu.get("parsed") else "mixed",
            measurements=measurements,
        )
        if not parsed_gpu.get("parsed"):
            data["visual_only_gpu_profile"] = {
                "reason": "UE4.27 did not expose parseable ProfileGPU text to the bridge.",
                "screenshots": screenshots,
                "inferred_risks": data.get("evidence", []),
            }
        if bool(params.get("generate_report", True)):
            data["report_path"] = _write_report(unreal, {"data": data}, "profilegpu_capture")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_camera_path_profile(params: Dict[str, Any]) -> Dict[str, Any]:
    """Capture and rank named camera viewpoints."""
    try:
        from mcp_bridge.handlers.viewport import handle_viewport_camera, handle_viewport_focus, handle_viewport_screenshot
        import unreal

        viewpoints = params.get("viewpoints") or []
        if not viewpoints:
            viewpoints = [{"name": "current_view"}]
        captures = []
        view_rankings = []
        asset_ranked, active_usage = _collect_asset_rankings(unreal, params)
        for index, viewpoint in enumerate(viewpoints):
            name = str(viewpoint.get("name") or f"view_{index + 1}")
            if viewpoint.get("actor_name"):
                camera_result = handle_viewport_focus({"actor_name": viewpoint.get("actor_name"), "distance": viewpoint.get("distance", 700)})
            elif viewpoint.get("location") or viewpoint.get("rotation"):
                camera_result = handle_viewport_camera({"location": viewpoint.get("location"), "rotation": viewpoint.get("rotation"), "fov": viewpoint.get("fov")})
            else:
                camera_result = {"success": True, "data": {"note": "Used current editor viewport"}}
            shot = handle_viewport_screenshot({
                "filename": f"optimization_camera_path_{name}_{_timestamp()}.png",
                "resolution": params.get("resolution", {"width": 1920, "height": 1080}),
                "show_ui": params.get("show_ui", False),
            }) if camera_result.get("success") else {"success": False, "error": camera_result.get("error")}
            scene_findings, scene_summary = _scene_findings(unreal, include_design_advice=True)
            score = (
                int(scene_summary.get("static_mesh_components", 0) or 0) * 0.02
                + int(scene_summary.get("shadow_casting_movable_lights", 0) or 0) * 8.0
                + int(scene_summary.get("particle_components", 0) or 0) * 0.5
                + len(scene_findings) * 4.0
            )
            view_rankings.append({
                "rank_score": round(score, 2),
                "name": name,
                "scene_summary": scene_summary,
                "top_assets": asset_ranked[:10],
            })
            captures.append({
                "name": name,
                "camera": camera_result.get("data", {}),
                "screenshot": shot.get("data", {}),
                "success": bool(shot.get("success")),
                "error": shot.get("error", ""),
            })
        view_rankings.sort(key=lambda row: row.get("rank_score", 0), reverse=True)
        for rank, row in enumerate(view_rankings, 1):
            row["rank"] = rank
        findings = []
        if view_rankings:
            worst = view_rankings[0]
            findings.append(_issue(
                "Medium",
                "Scene Setup",
                worst.get("name", "Camera path"),
                f"Worst camera path view score: {worst.get('rank_score')}",
                "View score combines active scene size, movable shadow lights, particles, and structured scene findings.",
                "Use occluders, layout bends, culling, HLOD, and light-radius reductions around the worst view first.",
                {"view": worst},
                confidence="Medium",
                fix_type="Level Design Review",
            ))
        data = _data_with_findings(
            "Draw" if view_rankings else "Needs evidence",
            findings,
            {"active_level_usage": {"actors": active_usage.get("actors")}},
            captures=captures,
            recommended_next_steps=[
                "Start with the highest ranked view and inspect its screenshot.",
                "Compare top assets and lighting risks for that view against the active level.",
                "After layout or culling changes, recapture the same camera path.",
            ],
            evidence_type="mixed",
            rankings={"views": view_rankings, "assets": asset_ranked[:20]},
        )
        if bool(params.get("generate_report", True)):
            data["report_path"] = _write_report(unreal, {"data": data}, "camera_path_profile")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_visual_logger_ingest(params: Dict[str, Any]) -> Dict[str, Any]:
    """Parse Visual Logger or log text for gameplay-heavy CPU clues."""
    try:
        import unreal

        saved = _project_saved_dir(unreal)
        paths = params.get("paths")
        if not isinstance(paths, list) or not paths:
            paths = profiler_ingestion.find_recent_files(saved, [".vlog", ".txt", ".log"], limit=int(params.get("limit", 20)))
            log_path = _project_log_path(unreal)
            if log_path and os.path.exists(log_path):
                paths.insert(0, log_path)
        parsed = profiler_ingestion.parse_visual_logger_files([str(path) for path in paths])
        findings = []
        counts = parsed.get("counts", {})
        for category, count in counts.items():
            if int(count or 0) <= 0:
                continue
            findings.append(_issue(
                "Medium" if int(count) < 50 else "High",
                "CPU Profiling",
                f"Visual Logger {category}",
                f"{count} {category} gameplay log signals found",
                "Visual Logger evidence can explain Game thread cost from AI, movement, pathing, perception, or state churn.",
                "Inspect repeated actors and replace per-tick requests with event, timer, or cooldown-driven updates.",
                {"examples": (parsed.get("examples") or {}).get(category, [])},
                confidence="Medium",
                fix_type="Content Refactor",
            ))
        data = _data_with_findings(
            "Game" if findings else "Needs evidence",
            findings,
            {"visual_logger": parsed},
            recommended_next_steps=[
                "Use Visual Logger findings to explain CPU-heavy gameplay scenes.",
                "Correlate AI and pathing signals with stat game, stat blueprint, and Insights CPU scopes.",
            ],
            evidence_type="measured" if parsed.get("parsed") else "visual",
            measurements={"visual_logger": parsed},
        )
        if bool(params.get("generate_report", False)):
            data["report_path"] = _write_report(unreal, {"data": data}, "visual_logger_ingest")
        return _ok(data)
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_fix_candidates(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return low-risk fix candidates separate from manual recommendations."""
    try:
        texture = handle_optimization_texture_harm_rank({
            "path": params.get("path", "/Game/"),
            "recursive": params.get("recursive", True),
            "max_assets": params.get("max_assets", 1000),
            "max_items": params.get("max_items", 50),
        })
        data = texture.get("data", {})
        findings = data.get("findings", []) or []
        rankings = data.get("rankings", {}) or {}
        candidates = opt_verification.build_fix_candidates(findings, rankings)
        return _ok({
            "likely_bottleneck": data.get("likely_bottleneck", "Streaming/Memory"),
            "evidence": data.get("evidence", []),
            "recommended_next_steps": [
                "Apply only low-risk candidates after reviewing dependency trails.",
                "Run before/after verification after each candidate group.",
            ],
            "findings": findings,
            "finding_count": len(findings),
            "evidence_type": "inferred",
            "rankings": rankings,
            "dependency_trails": data.get("dependency_trails", {}),
            "fix_candidates": candidates,
        })
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_apply_low_risk_fixes(params: Dict[str, Any]) -> Dict[str, Any]:
    """Apply explicit low-risk candidates, dry-run by default."""
    try:
        import unreal

        dry_run = bool(params.get("dry_run", True))
        save_assets = bool(params.get("save_assets", False))
        candidates = params.get("candidates")
        if not isinstance(candidates, list) or not candidates:
            candidates = handle_optimization_fix_candidates(params).get("data", {}).get("fix_candidates", [])
        applied = []
        skipped = []
        for candidate in candidates:
            if str(candidate.get("risk_level")) != "low":
                skipped.append({"candidate": candidate, "reason": "Only low-risk candidates are eligible"})
                continue
            action = str(candidate.get("action") or "")
            asset_path = str(candidate.get("asset") or "")
            if not asset_path:
                skipped.append({"candidate": candidate, "reason": "Missing asset path"})
                continue
            asset = unreal.EditorAssetLibrary.load_asset(asset_path)
            if asset is None:
                skipped.append({"candidate": candidate, "reason": "Asset not found"})
                continue
            if dry_run:
                applied.append({"candidate": candidate, "dry_run": True})
                continue
            if action == "enable_mipmaps":
                try:
                    asset.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
                    asset.modify()
                    applied.append({"candidate": candidate, "dry_run": False, "changed": "mip_gen_settings"})
                except Exception as exc:
                    skipped.append({"candidate": candidate, "reason": str(exc)})
            elif action == "cap_texture_size":
                try:
                    asset.set_editor_property("max_texture_size", int(candidate.get("value") or 2048))
                    asset.modify()
                    applied.append({"candidate": candidate, "dry_run": False, "changed": "max_texture_size"})
                except Exception as exc:
                    skipped.append({"candidate": candidate, "reason": str(exc)})
            else:
                skipped.append({"candidate": candidate, "reason": "Action is review-only or unsupported"})
        if save_assets and not dry_run:
            for item in applied:
                asset_path = str((item.get("candidate") or {}).get("asset") or "")
                if asset_path:
                    try:
                        unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=True)
                    except Exception:
                        pass
        return _ok({
            "dry_run": dry_run,
            "applied": applied,
            "skipped": skipped,
            "changed_count": 0 if dry_run else len(applied),
            "recommended_next_steps": ["Run optimization_before_after_verify before keeping applied changes."],
        })
    except Exception as exc:
        return _err(str(exc))


def _flatten_csv_measurements(data: Dict[str, Any]) -> Dict[str, float]:
    metrics: Dict[str, float] = {}
    csv_data = (((data.get("measurements") or {}).get("csv") or {}).get("files") or [])
    for file_data in csv_data:
        for key, value in (file_data.get("metrics") or {}).items():
            if isinstance(value, dict) and "avg" in value:
                metrics[f"{key}.avg"] = float(value.get("avg") or 0.0)
            if isinstance(value, dict) and "max" in value:
                metrics[f"{key}.max"] = float(value.get("max") or 0.0)
    return metrics


def handle_optimization_before_after_verify(params: Dict[str, Any]) -> Dict[str, Any]:
    """Compare baseline and after measurements for an optimization candidate group."""
    try:
        baseline = params.get("baseline")
        after = params.get("after")
        if not isinstance(baseline, dict) or not isinstance(after, dict):
            baseline_result = handle_optimization_stat_capture({
                "duration_seconds": params.get("duration_seconds", 2.0),
                "capture_screenshot": params.get("capture_screenshot", True),
            })
            if bool(params.get("apply_candidates", False)):
                handle_optimization_apply_low_risk_fixes({
                    "candidates": params.get("candidates", []),
                    "dry_run": False,
                    "save_assets": params.get("save_assets", False),
                })
            after_result = handle_optimization_stat_capture({
                "duration_seconds": params.get("duration_seconds", 2.0),
                "capture_screenshot": params.get("capture_screenshot", True),
            })
            baseline = _flatten_csv_measurements(baseline_result.get("data", {}))
            after = _flatten_csv_measurements(after_result.get("data", {}))
            screenshots = {
                "baseline": baseline_result.get("data", {}).get("screenshots", []),
                "after": after_result.get("data", {}).get("screenshots", []),
            }
        else:
            screenshots = params.get("screenshots", {})
        verification = opt_verification.compare_metric_sets(baseline, after)
        verification["screenshots"] = screenshots
        return _ok({
            "likely_bottleneck": "Verification",
            "evidence": [f"Compared {len(verification.get('deltas', {}))} numeric metrics"],
            "recommended_next_steps": [
                "Keep the change if measured deltas improve and screenshot differences are acceptable.",
                "Review manually if metrics are missing or mixed.",
            ],
            "evidence_type": "measured" if verification.get("deltas") else "visual",
            "verification": verification,
        })
    except Exception as exc:
        return _err(str(exc))


def handle_optimization_generate_report(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a Markdown optimization report from supplied data or a fresh quick triage."""
    try:
        import unreal

        payload = params.get("payload")
        if not isinstance(payload, dict):
            payload = handle_optimization_quick_triage({
                "target": params.get("target", "desktop_60"),
                "capture_screenshots": params.get("capture_screenshots", True),
                "include_asset_audit": params.get("include_asset_audit", True),
                "include_scene_audit": params.get("include_scene_audit", True),
                "include_vr_checks": params.get("include_vr_checks", False),
            })
        report_path = _write_report(unreal, payload, str(params.get("basename", "optimization_report")))
        data = payload.get("data", {}) if isinstance(payload.get("data"), dict) else {}
        result = {
            "report_path": report_path,
            "likely_bottleneck": data.get("likely_bottleneck", "Needs evidence"),
            "evidence": data.get("evidence", []),
            "recommended_next_steps": data.get("recommended_next_steps", []),
            "findings": data.get("findings", []),
            "finding_count": data.get("finding_count", len(data.get("findings", []) or [])),
            "screenshots": data.get("screenshots", []),
            "evidence_type": data.get("evidence_type", "mixed"),
            "measurements": data.get("measurements", {}),
            "rankings": data.get("rankings", {}),
            "dependency_trails": data.get("dependency_trails", {}),
            "fix_candidates": data.get("fix_candidates", []),
            "verification": data.get("verification", {}),
        }
        result.setdefault("measurement_status", opt_schema.measurement_status(result.get("measurements", {})))
        return _ok(result)
    except Exception as exc:
        return _err(str(exc))
