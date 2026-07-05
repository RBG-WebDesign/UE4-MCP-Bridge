"""Shared response and finding helpers for optimization commands."""

from typing import Any, Dict, List, Optional


EVIDENCE_TYPES = {"measured", "inferred", "visual", "mixed"}


def normalize_evidence_type(value: Any, fallback: str = "inferred") -> str:
    text = str(value or "").lower()
    return text if text in EVIDENCE_TYPES else fallback


def measurement_status(measurements: Dict[str, Any], fallback: str = "visual_only") -> str:
    if not isinstance(measurements, dict) or not measurements:
        return fallback
    for value in measurements.values():
        if isinstance(value, dict) and value.get("parsed"):
            return "parsed"
        if isinstance(value, list) and value:
            return "parsed"
    return fallback


def enhanced_fields(
    evidence_type: str = "inferred",
    measurements: Optional[Dict[str, Any]] = None,
    rankings: Optional[Dict[str, Any]] = None,
    dependency_trails: Optional[Dict[str, Any]] = None,
    fix_candidates: Optional[List[Dict[str, Any]]] = None,
    verification: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    data: Dict[str, Any] = {
        "evidence_type": normalize_evidence_type(evidence_type),
        "measurements": measurements or {},
        "rankings": rankings or {},
        "dependency_trails": dependency_trails or {},
        "fix_candidates": fix_candidates or [],
        "verification": verification or {},
    }
    data["measurement_status"] = measurement_status(data["measurements"])
    return data

