"""Machine-readable profiler ingestion helpers with graceful fallbacks."""

import csv
import json
import os
import re
import time
from typing import Any, Dict, Iterable, List, Optional


PROFILEGPU_PATTERNS = [
    "ShadowDepths",
    "Shadow Depths",
    "BasePass",
    "Translucency",
    "PostProcessing",
    "Post Process",
    "SSR",
    "Lighting",
    "SlateUI",
    "Slate",
]


def find_recent_files(root: str, extensions: Iterable[str], since_time: Optional[float] = None, limit: int = 20) -> List[str]:
    if not root or not os.path.isdir(root):
        return []
    wanted = {ext.lower() for ext in extensions}
    found: List[str] = []
    for base, _, names in os.walk(root):
        for name in names:
            ext = os.path.splitext(name)[1].lower()
            if ext not in wanted:
                continue
            path = os.path.join(base, name)
            try:
                if since_time is not None and os.path.getmtime(path) < since_time:
                    continue
            except Exception:
                pass
            found.append(path)
    found.sort(key=lambda path: os.path.getmtime(path) if os.path.exists(path) else 0, reverse=True)
    return found[:limit]


def parse_csv_file(path: str, max_rows: int = 2000) -> Dict[str, Any]:
    result: Dict[str, Any] = {"path": path, "parsed": False, "columns": [], "rows": 0, "metrics": {}, "error": ""}
    try:
        with open(path, "r", encoding="utf-8-sig", errors="replace", newline="") as handle:
            reader = csv.DictReader(handle)
            columns = list(reader.fieldnames or [])
            numeric: Dict[str, List[float]] = {column: [] for column in columns}
            rows = 0
            for row in reader:
                rows += 1
                if rows > max_rows:
                    break
                for column in columns:
                    value = row.get(column)
                    try:
                        numeric[column].append(float(str(value).strip()))
                    except Exception:
                        pass
            metrics = {}
            for column, values in numeric.items():
                if not values:
                    continue
                metrics[column] = {
                    "avg": round(sum(values) / len(values), 4),
                    "max": round(max(values), 4),
                    "samples": len(values),
                }
            result.update({"parsed": bool(metrics), "columns": columns, "rows": rows, "metrics": metrics})
    except Exception as exc:
        result["error"] = str(exc)
    return result


def parse_profilegpu_text(text: str) -> Dict[str, Any]:
    passes: List[Dict[str, Any]] = []
    for raw_line in str(text or "").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        has_pass_name = any(pattern.lower() in line.lower() for pattern in PROFILEGPU_PATTERNS)
        if not has_pass_name:
            continue
        match = re.search(r"([0-9]+(?:\.[0-9]+)?)\s*ms", line, re.IGNORECASE)
        passes.append({
            "line": line[:300],
            "name": _best_profilegpu_name(line),
            "time_ms": float(match.group(1)) if match else None,
        })
    passes.sort(key=lambda row: row.get("time_ms") if row.get("time_ms") is not None else -1.0, reverse=True)
    return {"parsed": bool(passes), "passes": passes}


def parse_profilegpu_files(paths: Iterable[str], max_bytes: int = 1024 * 1024) -> Dict[str, Any]:
    parsed_files = []
    all_passes = []
    for path in paths:
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read(max_bytes)
            parsed = parse_profilegpu_text(text)
            parsed_files.append({"path": path, "parsed": parsed.get("parsed", False), "pass_count": len(parsed.get("passes", []))})
            all_passes.extend(parsed.get("passes", []))
        except Exception as exc:
            parsed_files.append({"path": path, "parsed": False, "error": str(exc)})
    all_passes.sort(key=lambda row: row.get("time_ms") if row.get("time_ms") is not None else -1.0, reverse=True)
    return {"parsed": bool(all_passes), "files": parsed_files, "passes": all_passes[:50]}


def summarize_trace_file(path: str) -> Dict[str, Any]:
    exists = bool(path and os.path.exists(path))
    return {
        "path": path,
        "exists": exists,
        "parsed": False,
        "parser": "unavailable",
        "summary": {},
        "note": "Trace capture is available, but structured .utrace parsing requires the compiled TraceServices helper.",
    }


def parse_visual_logger_text(text: str) -> Dict[str, Any]:
    categories = {
        "ai": ["AI", "BehaviorTree", "Perception"],
        "movement": ["Movement", "MoveTo", "PathFollowing"],
        "pathing": ["Path", "NavMesh", "Recast"],
        "state": ["State", "Blackboard"],
    }
    counts = {key: 0 for key in categories}
    examples = {key: [] for key in categories}
    for line in str(text or "").splitlines():
        for key, needles in categories.items():
            if any(needle.lower() in line.lower() for needle in needles):
                counts[key] += 1
                if len(examples[key]) < 8:
                    examples[key].append(line.strip()[:240])
    return {"parsed": any(counts.values()), "counts": counts, "examples": examples}


def parse_visual_logger_files(paths: Iterable[str], max_bytes: int = 2 * 1024 * 1024) -> Dict[str, Any]:
    combined = {"ai": 0, "movement": 0, "pathing": 0, "state": 0}
    examples = {"ai": [], "movement": [], "pathing": [], "state": []}
    files = []
    for path in paths:
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                parsed = parse_visual_logger_text(handle.read(max_bytes))
            files.append({"path": path, "parsed": parsed.get("parsed", False)})
            for key, value in parsed.get("counts", {}).items():
                combined[key] = combined.get(key, 0) + int(value or 0)
            for key, value in parsed.get("examples", {}).items():
                examples[key].extend(value)
                examples[key] = examples[key][:8]
        except Exception as exc:
            files.append({"path": path, "parsed": False, "error": str(exc)})
    return {"parsed": any(combined.values()), "files": files, "counts": combined, "examples": examples}


def _best_profilegpu_name(line: str) -> str:
    for pattern in PROFILEGPU_PATTERNS:
        if pattern.lower() in line.lower():
            return pattern
    return line.split()[0] if line.split() else "Unknown"

