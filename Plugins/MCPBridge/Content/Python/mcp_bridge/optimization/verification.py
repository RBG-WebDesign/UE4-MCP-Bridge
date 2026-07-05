"""Fix candidate and before/after verification helpers."""

from typing import Any, Dict, Iterable, List


def build_fix_candidates(findings: Iterable[Dict[str, Any]], rankings: Dict[str, Any]) -> List[Dict[str, Any]]:
    candidates: List[Dict[str, Any]] = []
    for texture in rankings.get("textures", []) or []:
        path = texture.get("path") or texture.get("item")
        if not path:
            continue
        if texture.get("mipmaps_disabled"):
            candidates.append({
                "id": f"enable_mips:{path}",
                "asset": path,
                "action": "enable_mipmaps",
                "risk_level": "low",
                "fix_type": "Low Risk Setting Review",
                "reason": "Texture has mipmaps disabled.",
                "dry_run_only": False,
            })
        if int(texture.get("width") or 0) >= 4096 or int(texture.get("height") or 0) >= 4096:
            candidates.append({
                "id": f"cap_texture_2048:{path}",
                "asset": path,
                "action": "cap_texture_size",
                "value": 2048,
                "risk_level": "low" if str(texture.get("importance_tag", "")).lower() != "hero" else "medium",
                "fix_type": "Low Risk Setting Review",
                "reason": "Large texture should be reviewed for a 2048 cap on desktop_60.",
                "dry_run_only": False,
            })
    for finding in findings:
        if str(finding.get("fix_type")) == "Low Risk Setting Review":
            candidates.append({
                "id": f"review:{finding.get('item')}",
                "asset": finding.get("item"),
                "action": "review_low_risk_setting",
                "risk_level": "low",
                "fix_type": "Low Risk Setting Review",
                "reason": finding.get("issue", ""),
                "dry_run_only": True,
            })
    deduped: List[Dict[str, Any]] = []
    seen = set()
    for candidate in candidates:
        key = candidate.get("id")
        if key in seen:
            continue
        seen.add(key)
        deduped.append(candidate)
    return deduped[:100]


def compare_metric_sets(baseline: Dict[str, Any], after: Dict[str, Any]) -> Dict[str, Any]:
    deltas: Dict[str, Any] = {}
    for key, before_value in baseline.items():
        if key not in after:
            continue
        try:
            before_float = float(before_value)
            after_float = float(after[key])
        except Exception:
            continue
        deltas[key] = {
            "before": before_float,
            "after": after_float,
            "delta": round(after_float - before_float, 4),
            "improved": after_float < before_float,
        }
    improved = [value for value in deltas.values() if value.get("improved")]
    worsened = [value for value in deltas.values() if not value.get("improved")]
    recommendation = "keep_change" if improved and not worsened else "review_manually"
    return {"deltas": deltas, "recommendation": recommendation}

