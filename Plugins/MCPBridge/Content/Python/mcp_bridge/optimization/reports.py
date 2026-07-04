"""Report formatting helpers for enhanced optimization results."""

from typing import Any, Dict, List


def append_enhanced_sections(lines: List[str], data: Dict[str, Any]) -> None:
    evidence_type = data.get("evidence_type")
    measurement_status = data.get("measurement_status")
    if evidence_type or measurement_status:
        lines.extend(["", "## Evidence Type"])
        if evidence_type:
            lines.append(f"- Evidence type: {evidence_type}")
        if measurement_status:
            lines.append(f"- Measurement status: {measurement_status}")

    rankings = data.get("rankings") or {}
    if rankings:
        lines.extend(["", "## Rankings"])
        for name, rows in rankings.items():
            if not isinstance(rows, list) or not rows:
                continue
            lines.append(f"### {str(name).replace('_', ' ').title()}")
            for row in rows[:10]:
                label = row.get("path") or row.get("asset") or row.get("item") or row.get("name")
                score = row.get("rank_score")
                risk = row.get("risk_level")
                lines.append(f"- {label}: score {score}, risk {risk}")

    fix_candidates = data.get("fix_candidates") or []
    if fix_candidates:
        lines.extend(["", "## Fix Candidates"])
        for candidate in fix_candidates[:20]:
            lines.append(f"- [{candidate.get('risk_level')}] {candidate.get('action')} on {candidate.get('asset')}: {candidate.get('reason')}")

