"""Pure ranking models for optimization attribution."""

import math
from typing import Any, Dict, Iterable, List, Optional


BYTES_PER_PIXEL_BY_FORMAT = {
    "DXT1": 0.5,
    "BC1": 0.5,
    "DXT5": 1.0,
    "BC3": 1.0,
    "BC5": 1.0,
    "BC7": 1.0,
    "RGBA": 4.0,
    "BGRA": 4.0,
    "HDR": 8.0,
}


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def estimate_texture_memory_bytes(width: int, height: int, format_name: str = "") -> int:
    format_upper = str(format_name or "").upper()
    bytes_per_pixel = 4.0
    for key, value in BYTES_PER_PIXEL_BY_FORMAT.items():
        if key in format_upper:
            bytes_per_pixel = value
            break
    mip_factor = 1.33
    return int(max(0, width) * max(0, height) * bytes_per_pixel * mip_factor)


def risk_level_from_score(score: float) -> str:
    if score >= 80:
        return "high"
    if score >= 50:
        return "medium"
    return "low"


def severity_from_score(score: float) -> str:
    if score >= 80:
        return "High"
    if score >= 50:
        return "Medium"
    return "Low"


def texture_harm_score(texture: Dict[str, Any], usage: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    usage = usage or {}
    width = int(texture.get("width") or 0)
    height = int(texture.get("height") or 0)
    max_dim = max(width, height)
    memory_bytes = int(texture.get("memory_bytes") or estimate_texture_memory_bytes(width, height, texture.get("format", "")))
    memory_mb = memory_bytes / (1024.0 * 1024.0) if memory_bytes else 0.0
    component_count = int(usage.get("component_count") or texture.get("component_count") or 0)
    material_count = int(usage.get("material_count") or texture.get("material_count") or 0)
    visibility_weight = float(texture.get("visibility_weight", usage.get("visibility_weight", 1.0)) or 1.0)
    material_risk = float(texture.get("material_risk", usage.get("material_risk", 0.0)) or 0.0)
    importance = str(texture.get("importance_tag") or usage.get("importance_tag") or "").lower()

    components = {
        "memory": clamp((memory_mb / 64.0) * 30.0, 0.0, 30.0),
        "dimension": clamp((max_dim / 4096.0) * 18.0, 0.0, 18.0),
        "streaming": 0.0,
        "usage": clamp(math.log(max(component_count, 0) + 1, 2) * 4.0, 0.0, 18.0),
        "visibility": clamp(visibility_weight * 10.0, 0.0, 12.0),
        "material": clamp(material_risk, 0.0, 12.0),
        "importance": 0.0,
    }
    if texture.get("never_stream"):
        components["streaming"] += 12.0
    if texture.get("mipmaps_disabled"):
        components["streaming"] += 10.0
    if str(texture.get("streaming_status") or "").lower() in {"over_budget", "pool_over_budget"}:
        components["streaming"] += 8.0
    components["streaming"] = clamp(components["streaming"], 0.0, 22.0)

    if "hero" in importance:
        components["importance"] = -8.0
    elif "background" in importance or "setdress" in importance:
        components["importance"] = 6.0

    score = round(clamp(sum(components.values()), 0.0, 100.0), 2)
    return {
        "rank_score": score,
        "risk_level": risk_level_from_score(score),
        "severity": severity_from_score(score),
        "score_components": {key: round(value, 2) for key, value in components.items()},
        "estimated_memory_mb": round(memory_mb, 2),
        "component_count": component_count,
        "material_count": material_count,
    }


def rank_textures(items: Iterable[Dict[str, Any]], max_items: int = 50) -> List[Dict[str, Any]]:
    ranked: List[Dict[str, Any]] = []
    for item in items:
        enriched = dict(item)
        usage = enriched.get("usage") if isinstance(enriched.get("usage"), dict) else {}
        enriched.update(texture_harm_score(enriched, usage))
        ranked.append(enriched)
    ranked.sort(key=lambda row: (float(row.get("rank_score") or 0), int(row.get("component_count") or 0)), reverse=True)
    for index, row in enumerate(ranked, 1):
        row["rank"] = index
    return ranked[:max_items]


def asset_cost_score(asset: Dict[str, Any]) -> Dict[str, Any]:
    component_count = int(asset.get("component_count") or 0)
    material_slots = int(asset.get("material_slots") or 0)
    lod_count = int(asset.get("lod_count") or 0)
    triangle_count = int(asset.get("triangle_count") or 0)
    visibility_weight = float(asset.get("visibility_weight") or 1.0)
    shadow_casters = int(asset.get("shadow_casters") or 0)

    components = {
        "placement": clamp(math.log(component_count + 1, 2) * 8.0, 0.0, 28.0),
        "materials": clamp(max(0, material_slots - 1) * 4.0, 0.0, 16.0),
        "lod": 16.0 if lod_count <= 1 else 0.0,
        "triangles": clamp((triangle_count / 100000.0) * 18.0, 0.0, 18.0),
        "visibility": clamp(visibility_weight * 12.0, 0.0, 12.0),
        "shadows": clamp(shadow_casters * 2.0, 0.0, 10.0),
    }
    if asset.get("complex_collision"):
        components["collision"] = 8.0
    else:
        components["collision"] = 0.0
    score = round(clamp(sum(components.values()), 0.0, 100.0), 2)
    return {
        "rank_score": score,
        "risk_level": risk_level_from_score(score),
        "severity": severity_from_score(score),
        "score_components": {key: round(value, 2) for key, value in components.items()},
    }


def rank_assets(items: Iterable[Dict[str, Any]], max_items: int = 50) -> List[Dict[str, Any]]:
    ranked: List[Dict[str, Any]] = []
    for item in items:
        enriched = dict(item)
        enriched.update(asset_cost_score(enriched))
        ranked.append(enriched)
    ranked.sort(key=lambda row: (float(row.get("rank_score") or 0), int(row.get("component_count") or 0)), reverse=True)
    for index, row in enumerate(ranked, 1):
        row["rank"] = index
    return ranked[:max_items]

