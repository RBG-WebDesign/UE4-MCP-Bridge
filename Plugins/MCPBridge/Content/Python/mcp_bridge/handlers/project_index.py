"""Project index, semantic diff, and gameplay pattern handlers.

This module builds a lightweight, file-backed intelligence layer from existing
read-only bridge handlers. The index is disposable cache data under
Saved/MCP/index and does not mutate Unreal assets.
"""

from __future__ import annotations

import datetime
import json
import os
from typing import Any, Dict, Iterable, List, Optional, Tuple

from mcp_bridge.handlers.project import handle_asset_list
from mcp_bridge.handlers.blueprints import handle_blueprint_info, handle_blueprint_list
from mcp_bridge.handlers.level import handle_level_actors
from mcp_bridge.handlers.materials import handle_material_info, handle_material_list


SCHEMA_VERSION = 1
INDEX_RELATIVE_DIR = os.path.join("Saved", "MCP", "index")
INDEX_CATEGORIES = ("assets", "blueprints", "materials", "widgets", "level", "design_memory")


def _now_iso() -> str:
    return datetime.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"


def _project_dir() -> str:
    import unreal

    return unreal.SystemLibrary.get_project_directory()


def _index_dir() -> str:
    return os.path.join(_project_dir(), INDEX_RELATIVE_DIR)


def _index_path(category: str) -> str:
    return os.path.join(_index_dir(), f"{category}.json")


def _ensure_index_dir() -> str:
    path = _index_dir()
    if not os.path.isdir(path):
        os.makedirs(path)
    return path


def _safe_str(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    return str(value)


def _stable_json(value: Any) -> str:
    try:
        return json.dumps(value, sort_keys=True, default=str)
    except Exception:
        return _safe_str(value)


def _search_text(*parts: Any) -> str:
    flattened: List[str] = []
    for part in parts:
        if part is None:
            continue
        if isinstance(part, (dict, list, tuple)):
            flattened.append(_stable_json(part))
        else:
            flattened.append(_safe_str(part))
    return " ".join(flattened).lower()


def _record_id(category: str, record: Dict[str, Any]) -> str:
    for key in ("path", "actor_name", "name", "id"):
        value = record.get(key)
        if value:
            return f"{category}:{value}"
    return f"{category}:{_stable_json(record)[:80]}"


def _write_category(category: str, records: List[Dict[str, Any]]) -> Dict[str, Any]:
    _ensure_index_dir()
    payload = {
        "schema_version": SCHEMA_VERSION,
        "category": category,
        "generated_at": _now_iso(),
        "count": len(records),
        "records": records,
    }
    path = _index_path(category)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
    return {"path": path, "count": len(records)}


def _read_category(category: str) -> Dict[str, Any]:
    path = _index_path(category)
    if not os.path.exists(path):
        return {
            "schema_version": SCHEMA_VERSION,
            "category": category,
            "generated_at": None,
            "count": 0,
            "records": [],
        }
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _asset_dependencies(asset_path: str) -> List[str]:
    try:
        import unreal

        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        deps = registry.get_dependencies(asset_path, unreal.AssetRegistryDependencyOptions(True, True, True, True))
        return sorted([_safe_str(dep) for dep in deps])
    except Exception:
        return []


def _asset_record(asset: Dict[str, Any]) -> Dict[str, Any]:
    path = _safe_str(asset.get("path"))
    name = _safe_str(asset.get("name") or os.path.basename(path))
    asset_type = _safe_str(asset.get("type"))
    dependencies = _asset_dependencies(path)
    return {
        "id": path,
        "name": name,
        "path": path,
        "type": asset_type,
        "dependencies": dependencies,
        "searchable_text": _search_text(name, path, asset_type, dependencies),
    }


def _collect_assets(params: Dict[str, Any]) -> List[Dict[str, Any]]:
    path_prefix = params.get("path_prefix") or params.get("path") or "/Game/"
    result = handle_asset_list({"path": path_prefix, "recursive": True})
    if not result.get("success"):
        return []
    return [_asset_record(asset) for asset in result.get("data", {}).get("assets", [])]


def _blueprint_record(info: Dict[str, Any]) -> Dict[str, Any]:
    path = _safe_str(info.get("path"))
    name = _safe_str(info.get("name") or os.path.basename(path))
    components = info.get("components", [])
    variables = info.get("variables", [])
    functions = info.get("functions", [])
    event_graphs = info.get("event_graphs", [])
    return {
        "id": path,
        "name": name,
        "path": path,
        "type": "Blueprint",
        "parent_class": _safe_str(info.get("parent_class")),
        "parent_chain": info.get("parent_chain", []),
        "is_compiled": bool(info.get("is_compiled")),
        "components": components,
        "variables": variables,
        "functions": functions,
        "event_graphs": event_graphs,
        "component_count": len(components),
        "variable_count": len(variables),
        "function_count": len(functions),
        "dependencies": _asset_dependencies(path),
        "searchable_text": _search_text(name, path, info.get("parent_class"), components, variables, functions, event_graphs),
    }


def _collect_blueprints(params: Dict[str, Any]) -> List[Dict[str, Any]]:
    path_prefix = params.get("path_prefix") or "/Game/"
    limit = int(params.get("blueprint_detail_limit", params.get("limit", 300)) or 300)
    listed = handle_blueprint_list({"path_filter": path_prefix, "limit": min(max(limit, 1), 2000)})
    if not listed.get("success"):
        return []
    records: List[Dict[str, Any]] = []
    for item in listed.get("data", {}).get("blueprints", [])[:limit]:
        info = handle_blueprint_info({"blueprint_path": item.get("path")})
        if info.get("success"):
            records.append(_blueprint_record(info.get("data", {})))
        else:
            path = _safe_str(item.get("path"))
            records.append({
                "id": path,
                "name": _safe_str(item.get("name")),
                "path": path,
                "type": "Blueprint",
                "parent_class": _safe_str(item.get("parent_class")),
                "error": info.get("error"),
                "searchable_text": _search_text(item),
            })
    return records


def _material_record(info: Dict[str, Any]) -> Dict[str, Any]:
    path = _safe_str(info.get("path"))
    name = _safe_str(info.get("name") or os.path.basename(path))
    parameters = info.get("parameters", {})
    return {
        "id": path,
        "name": name,
        "path": path,
        "type": _safe_str(info.get("type") or "material"),
        "parent": info.get("parent"),
        "parent_chain": info.get("parent_chain", []),
        "parameters": parameters,
        "dependencies": _asset_dependencies(path),
        "searchable_text": _search_text(name, path, info.get("type"), info.get("parent"), parameters),
    }


def _collect_materials(params: Dict[str, Any]) -> List[Dict[str, Any]]:
    path_prefix = params.get("path_prefix") or "/Game/"
    limit = int(params.get("material_detail_limit", params.get("limit", 300)) or 300)
    listed = handle_material_list({"path_filter": path_prefix, "limit": min(max(limit, 1), 2000)})
    if not listed.get("success"):
        return []
    records: List[Dict[str, Any]] = []
    for item in listed.get("data", {}).get("materials", [])[:limit]:
        info = handle_material_info({"material_path": item.get("path")})
        if info.get("success"):
            records.append(_material_record(info.get("data", {})))
        else:
            path = _safe_str(item.get("path"))
            records.append({
                "id": path,
                "name": _safe_str(item.get("name")),
                "path": path,
                "type": _safe_str(item.get("type")),
                "error": info.get("error"),
                "searchable_text": _search_text(item),
            })
    return records


def _collect_widgets(params: Dict[str, Any]) -> List[Dict[str, Any]]:
    assets = _collect_assets({**params, "path_prefix": params.get("path_prefix") or "/Game/"})
    widget_types = ("WidgetBlueprint", "UserWidgetBlueprint")
    widgets = [asset for asset in assets if asset.get("type") in widget_types or "widget" in _safe_str(asset.get("type")).lower()]
    for widget in widgets:
        widget["category_hint"] = "widget"
        widget["searchable_text"] = _search_text(widget.get("searchable_text"), "widget umg user interface hud menu prompt")
    return widgets


def _collect_level(params: Dict[str, Any]) -> List[Dict[str, Any]]:
    limit = int(params.get("actor_limit", params.get("limit", 1000)) or 1000)
    result = handle_level_actors({
        "include_transforms": True,
        "include_components": True,
        "limit": min(max(limit, 1), 5000),
    })
    if not result.get("success"):
        return []
    records: List[Dict[str, Any]] = []
    for actor in result.get("data", {}).get("actors", []):
        name = _safe_str(actor.get("name"))
        actor_class = _safe_str(actor.get("class"))
        records.append({
            "id": name,
            "name": name,
            "actor_name": name,
            "type": "Actor",
            "class": actor_class,
            "folder": _safe_str(actor.get("folder")),
            "location": actor.get("location"),
            "rotation": actor.get("rotation"),
            "scale": actor.get("scale"),
            "components": actor.get("components", []),
            "searchable_text": _search_text(name, actor_class, actor.get("folder"), actor.get("components")),
        })
    return records


def _collect_design_memory(params: Dict[str, Any]) -> List[Dict[str, Any]]:
    try:
        from mcp_bridge.generation import gameplay_framework as gf

        memory = gf.load_design_memory()
        return [{
            "id": "design_memory",
            "name": "design_memory",
            "type": "DesignMemory",
            "path": gf.project_file_path(gf.DESIGN_MEMORY_RELATIVE_PATH),
            "memory": memory,
            "searchable_text": _search_text(memory),
        }]
    except Exception as exc:
        return [{
            "id": "design_memory",
            "name": "design_memory",
            "type": "DesignMemory",
            "error": str(exc),
            "searchable_text": "",
        }]


def build_index_snapshot(params: Dict[str, Any]) -> Dict[str, List[Dict[str, Any]]]:
    categories = params.get("categories") or INDEX_CATEGORIES
    if isinstance(categories, str):
        categories = [categories]
    snapshot: Dict[str, List[Dict[str, Any]]] = {}
    collectors = {
        "assets": _collect_assets,
        "blueprints": _collect_blueprints,
        "materials": _collect_materials,
        "widgets": _collect_widgets,
        "level": _collect_level,
        "design_memory": _collect_design_memory,
    }
    for category in categories:
        collector = collectors.get(category)
        if collector:
            snapshot[category] = collector(params)
    return snapshot


def _normalize_snapshot(snapshot: Any) -> Dict[str, List[Dict[str, Any]]]:
    if not snapshot:
        return {}
    if isinstance(snapshot, dict) and "records" in snapshot and "category" in snapshot:
        return {_safe_str(snapshot["category"]): snapshot.get("records", [])}
    if isinstance(snapshot, dict):
        normalized: Dict[str, List[Dict[str, Any]]] = {}
        for key, value in snapshot.items():
            if isinstance(value, dict) and "records" in value:
                normalized[key] = value.get("records", [])
            elif isinstance(value, list):
                normalized[key] = value
        return normalized
    return {}


def _load_cached_snapshot(categories: Optional[Iterable[str]] = None) -> Dict[str, List[Dict[str, Any]]]:
    selected = list(categories or INDEX_CATEGORIES)
    return {category: _read_category(category).get("records", []) for category in selected}


def handle_project_index_rebuild(params: Dict[str, Any]) -> Dict[str, Any]:
    """Rebuild the lightweight project index cache."""
    try:
        snapshot = build_index_snapshot(params)
        written = {category: _write_category(category, records) for category, records in snapshot.items()}
        return {
            "success": True,
            "data": {
                "schema_version": SCHEMA_VERSION,
                "index_dir": _index_dir(),
                "generated_at": _now_iso(),
                "categories": {category: len(records) for category, records in snapshot.items()},
                "files": written,
            },
        }
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def _query_match(record: Dict[str, Any], terms: List[str]) -> bool:
    if not terms:
        return True
    haystack = _safe_str(record.get("searchable_text") or _stable_json(record)).lower()
    return all(term in haystack for term in terms)


def _summarize_record(category: str, record: Dict[str, Any], include_raw: bool) -> Dict[str, Any]:
    summary = {
        "category": category,
        "id": _record_id(category, record),
        "name": record.get("name") or record.get("actor_name"),
        "path": record.get("path"),
        "type": record.get("type") or record.get("class"),
        "parent_class": record.get("parent_class"),
        "matches": [],
    }
    if include_raw:
        summary["raw"] = record
    return summary


def handle_project_index_query(params: Dict[str, Any]) -> Dict[str, Any]:
    """Search the cached project index."""
    try:
        category_filter = params.get("category")
        categories = [category_filter] if category_filter else INDEX_CATEGORIES
        query = _safe_str(params.get("query", "")).lower().strip()
        terms = [term for term in query.split() if term]
        path_prefix = _safe_str(params.get("path_prefix", "")).lower()
        limit = min(max(int(params.get("limit", 50) or 50), 1), 500)
        include_raw = bool(params.get("include_raw", False))

        results: List[Dict[str, Any]] = []
        for category in categories:
            payload = _read_category(_safe_str(category))
            for record in payload.get("records", []):
                path = _safe_str(record.get("path")).lower()
                if path_prefix and not path.startswith(path_prefix):
                    continue
                if not _query_match(record, terms):
                    continue
                results.append(_summarize_record(_safe_str(category), record, include_raw))
                if len(results) >= limit:
                    break
            if len(results) >= limit:
                break

        return {
            "success": True,
            "data": {
                "query": query,
                "category": category_filter,
                "path_prefix": params.get("path_prefix", ""),
                "count": len(results),
                "truncated": len(results) >= limit,
                "results": results,
            },
        }
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def _records_by_id(records: List[Dict[str, Any]]) -> Dict[str, Dict[str, Any]]:
    return {_safe_str(record.get("path") or record.get("actor_name") or record.get("id") or record.get("name")): record for record in records}


def _name_set(records: Any, key: str = "name") -> List[str]:
    if not isinstance(records, list):
        return []
    return sorted([_safe_str(item.get(key)) for item in records if isinstance(item, dict) and item.get(key)])


def _changed_fields(before: Dict[str, Any], after: Dict[str, Any], fields: Iterable[str]) -> Dict[str, Any]:
    changes: Dict[str, Any] = {}
    for field in fields:
        if before.get(field) != after.get(field):
            changes[field] = {"before": before.get(field), "after": after.get(field)}
    return changes


def diff_snapshots(before_snapshot: Any, after_snapshot: Any) -> Dict[str, Any]:
    before = _normalize_snapshot(before_snapshot)
    after = _normalize_snapshot(after_snapshot)
    categories = sorted(set(before.keys()) | set(after.keys()))
    result: Dict[str, Any] = {"categories": {}, "summary": {"added": 0, "removed": 0, "changed": 0}}

    for category in categories:
        before_map = _records_by_id(before.get(category, []))
        after_map = _records_by_id(after.get(category, []))
        before_ids = set(before_map.keys())
        after_ids = set(after_map.keys())
        added = sorted(after_ids - before_ids)
        removed = sorted(before_ids - after_ids)
        changed: List[Dict[str, Any]] = []

        for record_id in sorted(before_ids & after_ids):
            old = before_map[record_id]
            new = after_map[record_id]
            detail = _changed_fields(old, new, ("type", "class", "parent_class", "parent", "folder", "location", "rotation", "scale", "dependencies"))

            if category == "blueprints":
                for label, key in (("components", "components"), ("variables", "variables"), ("functions", "functions")):
                    old_names = _name_set(old.get(key))
                    new_names = _name_set(new.get(key))
                    if old_names != new_names:
                        detail[f"{label}_changed"] = {"before": old_names, "after": new_names}
            elif category == "materials":
                if old.get("parameters") != new.get("parameters"):
                    detail["material_parameters_changed"] = {
                        "before": old.get("parameters"),
                        "after": new.get("parameters"),
                    }
            elif category == "level":
                old_comps = _name_set(old.get("components"))
                new_comps = _name_set(new.get("components"))
                if old_comps != new_comps:
                    detail["components_changed"] = {"before": old_comps, "after": new_comps}

            if detail:
                changed.append({"id": record_id, "name": new.get("name") or old.get("name"), "changes": detail})

        renamed_candidates = []
        for old_id in removed:
            old_name = _safe_str(before_map[old_id].get("name")).lower()
            for new_id in added:
                new_name = _safe_str(after_map[new_id].get("name")).lower()
                if old_name and new_name and (old_name == new_name or old_name in new_name or new_name in old_name):
                    renamed_candidates.append({"before": old_id, "after": new_id, "name_similarity": True})

        result["categories"][category] = {
            "added": added,
            "removed": removed,
            "renamed_candidates": renamed_candidates,
            "changed": changed,
        }
        result["summary"]["added"] += len(added)
        result["summary"]["removed"] += len(removed)
        result["summary"]["changed"] += len(changed)

    return result


def _filter_snapshot_by_paths(snapshot: Dict[str, List[Dict[str, Any]]], asset_paths: List[str]) -> Dict[str, List[Dict[str, Any]]]:
    if not asset_paths:
        return snapshot
    wanted = set(asset_paths)
    filtered: Dict[str, List[Dict[str, Any]]] = {}
    for category, records in snapshot.items():
        filtered[category] = [
            record for record in records
            if record.get("path") in wanted or record.get("actor_name") in wanted or record.get("name") in wanted
        ]
    return filtered


def handle_project_semantic_diff(params: Dict[str, Any]) -> Dict[str, Any]:
    """Compare two snapshots or compare cached index data against a fresh read."""
    try:
        categories_param = params.get("categories")
        if isinstance(categories_param, str):
            categories = [categories_param]
        elif isinstance(categories_param, list):
            categories = categories_param
        else:
            categories = list(INDEX_CATEGORIES)

        before = params.get("before_snapshot") or _load_cached_snapshot(categories)
        after = params.get("after_snapshot") or build_index_snapshot({**params, "categories": categories})

        asset_paths = params.get("asset_paths") or []
        if isinstance(asset_paths, str):
            asset_paths = [asset_paths]
        before = _filter_snapshot_by_paths(_normalize_snapshot(before), asset_paths)
        after = _filter_snapshot_by_paths(_normalize_snapshot(after), asset_paths)

        diff = diff_snapshots(before, after)
        return {
            "success": True,
            "data": {
                "generated_at": _now_iso(),
                "asset_paths": asset_paths,
                "diff": diff,
            },
        }
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


PATTERN_RULES = {
    "overlap_logic": ("overlap", "trigger", "boxcomponent", "begin overlap", "end overlap"),
    "key_door": ("key", "door", "locked", "unlock", "pickup"),
    "prompt_widget": ("prompt", "widget", "hud", "textblock", "viewport"),
    "objective_chain": ("objective", "finish", "checkpoint", "levelstatemanager", "state"),
    "interactable": ("interact", "usable", "use", "pickup", "trigger"),
}


def _pattern_hits(text: str, tokens: Tuple[str, ...]) -> List[str]:
    return [token for token in tokens if token in text]


def search_gameplay_patterns(snapshot: Dict[str, List[Dict[str, Any]]], pattern: str = "", limit: int = 50) -> List[Dict[str, Any]]:
    wanted_patterns = [pattern] if pattern else sorted(PATTERN_RULES.keys())
    matches: List[Dict[str, Any]] = []
    for category in ("blueprints", "level", "widgets"):
        for record in snapshot.get(category, []):
            text = _safe_str(record.get("searchable_text") or _stable_json(record)).lower()
            for pattern_name in wanted_patterns:
                tokens = PATTERN_RULES.get(pattern_name)
                if not tokens:
                    continue
                hits = _pattern_hits(text, tokens)
                required_hits = 2 if pattern_name in ("key_door", "objective_chain") else 1
                if len(hits) >= required_hits:
                    matches.append({
                        "pattern": pattern_name,
                        "category": category,
                        "name": record.get("name") or record.get("actor_name"),
                        "path": record.get("path"),
                        "actor_name": record.get("actor_name"),
                        "type": record.get("type") or record.get("class"),
                        "why": [f"matched token '{hit}'" for hit in hits[:6]],
                    })
                    break
            if len(matches) >= limit:
                return matches
    return matches


def handle_gameplay_pattern_search(params: Dict[str, Any]) -> Dict[str, Any]:
    """Find reusable gameplay patterns in indexed Blueprints and level actors."""
    try:
        pattern = _safe_str(params.get("pattern", "")).strip()
        query = _safe_str(params.get("query", "")).strip().lower()
        limit = min(max(int(params.get("limit", 50) or 50), 1), 500)
        use_fresh = bool(params.get("fresh", False))
        if use_fresh:
            snapshot = build_index_snapshot({"categories": ["blueprints", "level", "widgets"], **params})
        else:
            snapshot = _load_cached_snapshot(["blueprints", "level", "widgets"])
        matches = search_gameplay_patterns(snapshot, pattern=pattern, limit=limit)
        if query:
            matches = [
                match for match in matches
                if query in _search_text(match.get("name"), match.get("path"), match.get("actor_name"), match.get("type"), match.get("why"))
            ][:limit]
        return {
            "success": True,
            "data": {
                "pattern": pattern,
                "query": query,
                "count": len(matches),
                "patterns": sorted(PATTERN_RULES.keys()),
                "matches": matches,
            },
        }
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}
