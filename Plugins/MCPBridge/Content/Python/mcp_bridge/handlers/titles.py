"""Data-driven title manifest and Sequencer title helpers."""

from __future__ import annotations

import json
import math
import os
from typing import Any, Dict, Iterable, List, Optional, Tuple


DEFAULT_RESOLUTION = [1920, 1080]
DEFAULT_FPS = 29.97
TITLE_DIR_RELATIVE = os.path.join("Saved", "MCP", "titles")
VALID_CUE_TYPES = {"rating_box", "main_title", "two_line_credit", "lower_third"}


def _project_dir() -> str:
    import unreal

    return unreal.SystemLibrary.get_project_directory()


def _title_dir() -> str:
    return os.path.join(_project_dir(), TITLE_DIR_RELATIVE)


def _ensure_title_dir() -> str:
    path = _title_dir()
    if not os.path.isdir(path):
        os.makedirs(path)
    return path


def _safe_id(value: Any, fallback: str = "title_manifest") -> str:
    text = str(value or fallback).strip()
    cleaned = []
    for char in text:
        if char.isalnum() or char in {"_", "-"}:
            cleaned.append(char)
        elif char.isspace():
            cleaned.append("_")
    return "".join(cleaned).strip("_") or fallback


def _manifest_path(manifest_id: str) -> str:
    return os.path.join(_ensure_title_dir(), f"{_safe_id(manifest_id)}.json")


def _load_json_file(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _write_json_file(path: str, data: Dict[str, Any]) -> str:
    parent = os.path.dirname(path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, sort_keys=True)
    return path


def _number(value: Any, fallback: float = 0.0) -> float:
    try:
        return float(value)
    except Exception:
        return fallback


def _int(value: Any, fallback: int = 0) -> int:
    try:
        return int(round(float(value)))
    except Exception:
        return fallback


def normalize_pixel_box(pixel_box: Iterable[Any], frame_size: Iterable[Any]) -> List[float]:
    values = list(pixel_box)
    size = list(frame_size)
    if len(values) != 4 or len(size) != 2:
        raise ValueError("pixel_box must have 4 values and frame_size must have 2 values")
    width = max(_number(size[0], 1.0), 1.0)
    height = max(_number(size[1], 1.0), 1.0)
    return [
        _number(values[0]) / width,
        _number(values[1]) / height,
        _number(values[2]) / width,
        _number(values[3]) / height,
    ]


def denormalize_box(box: Iterable[Any], resolution: Iterable[Any]) -> List[int]:
    values = list(box)
    size = list(resolution)
    if len(values) != 4 or len(size) != 2:
        raise ValueError("box must have 4 values and resolution must have 2 values")
    width = max(_number(size[0], 1.0), 1.0)
    height = max(_number(size[1], 1.0), 1.0)
    return [
        _int(_number(values[0]) * width),
        _int(_number(values[1]) * height),
        _int(_number(values[2]) * width),
        _int(_number(values[3]) * height),
    ]


def cue_end_frame(cue: Dict[str, Any]) -> int:
    return (
        _int(cue.get("startFrame"))
        + _int(cue.get("fadeInFrames"))
        + _int(cue.get("holdFrames"))
        + _int(cue.get("fadeOutFrames"))
    )


def cue_opacity_at_frame(cue: Dict[str, Any], frame: Any) -> float:
    current = _number(frame)
    start = _number(cue.get("startFrame"))
    fade_in = max(_number(cue.get("fadeInFrames")), 0.0)
    hold = max(_number(cue.get("holdFrames")), 0.0)
    fade_out = max(_number(cue.get("fadeOutFrames")), 0.0)
    fade_in_end = start + fade_in
    hold_end = fade_in_end + hold
    end = hold_end + fade_out

    if current < start:
        return 0.0
    if fade_in > 0 and current < fade_in_end:
        return max(0.0, min(1.0, (current - start) / fade_in))
    if current < hold_end:
        return 1.0
    if fade_out > 0 and current < end:
        return max(0.0, min(1.0, 1.0 - ((current - hold_end) / fade_out)))
    return 0.0


def schedule_cues(cues: List[Dict[str, Any]], fps: float, gap_frames: Optional[int] = None) -> List[Dict[str, Any]]:
    scheduled: List[Dict[str, Any]] = []
    next_start = 0
    gap = _int(gap_frames if gap_frames is not None else round(fps))
    for index, cue in enumerate(cues):
        updated = dict(cue)
        if "startFrame" not in updated or updated.get("autoSchedule"):
            updated["startFrame"] = next_start
        updated.setdefault("id", f"cue_{index + 1:02d}")
        updated.setdefault("fadeInFrames", 6)
        updated.setdefault("holdFrames", 90)
        updated.setdefault("fadeOutFrames", 6)
        updated.setdefault("type", "main_title")
        scheduled.append(updated)
        next_start = cue_end_frame(updated) + gap
    return scheduled


def default_style() -> Dict[str, Any]:
    return {
        "font": "CondensedBold",
        "fillColor": "#FFFFFF",
        "shadowColor": "#000000",
        "shadowOffset": [5, 5],
        "fontSize": 96,
        "roleFontSize": 34,
        "nameFontSize": 54,
    }


def build_manifest(params: Dict[str, Any]) -> Dict[str, Any]:
    manifest = dict(params.get("manifest") or {})
    manifest_id = _safe_id(params.get("id") or manifest.get("id") or "title_manifest")
    fps = _number(params.get("fps", manifest.get("fps", DEFAULT_FPS)), DEFAULT_FPS)
    resolution = list(params.get("resolution") or manifest.get("resolution") or DEFAULT_RESOLUTION)
    style = dict(default_style())
    style.update(manifest.get("style") or {})
    style.update(params.get("style") or {})
    raw_cues = list(params.get("cues") or manifest.get("cues") or [])
    gap_frames = params.get("gapFrames", manifest.get("gapFrames"))
    cues = schedule_cues(raw_cues, fps=fps, gap_frames=gap_frames)
    return {
        "id": manifest_id,
        "resolution": [_int(resolution[0], DEFAULT_RESOLUTION[0]), _int(resolution[1], DEFAULT_RESOLUTION[1])],
        "fps": fps,
        "style": style,
        "gapFrames": _int(gap_frames if gap_frames is not None else round(fps)),
        "cues": cues,
    }


def validate_manifest(manifest: Dict[str, Any]) -> Dict[str, Any]:
    errors: List[str] = []
    warnings: List[str] = []
    resolution = manifest.get("resolution")
    if not isinstance(resolution, list) or len(resolution) != 2:
        errors.append("resolution must be [width, height]")
    elif _number(resolution[0]) <= 0 or _number(resolution[1]) <= 0:
        errors.append("resolution values must be positive")
    if _number(manifest.get("fps"), 0.0) <= 0:
        errors.append("fps must be positive")

    previous_end: Optional[int] = None
    cues = manifest.get("cues")
    if not isinstance(cues, list) or not cues:
        errors.append("cues must be a non-empty array")
        cues = []
    for index, cue in enumerate(cues):
        cue_id = cue.get("id", f"cues[{index}]")
        cue_type = cue.get("type")
        if cue_type not in VALID_CUE_TYPES:
            errors.append(f"{cue_id}: unsupported type '{cue_type}'")
        box = cue.get("box")
        if not isinstance(box, list) or len(box) != 4:
            errors.append(f"{cue_id}: box must be [x, y, width, height]")
        else:
            x, y, w, h = [_number(v) for v in box]
            if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > 1.0 or y + h > 1.0:
                errors.append(f"{cue_id}: box must be normalized and fit inside the screen")
        for key in ("startFrame", "fadeInFrames", "holdFrames", "fadeOutFrames"):
            if _int(cue.get(key), -1) < 0:
                errors.append(f"{cue_id}: {key} must be >= 0")
        if _int(cue.get("holdFrames"), 0) == 0:
            warnings.append(f"{cue_id}: holdFrames is 0")
        current_end = cue_end_frame(cue)
        if previous_end is not None and _int(cue.get("startFrame")) < previous_end:
            warnings.append(f"{cue_id}: overlaps previous cue")
        previous_end = current_end
    return {
        "valid": not errors,
        "errors": errors,
        "warnings": warnings,
        "cue_count": len(cues),
    }


def _hex_to_color(hex_value: Any, alpha: float = 1.0) -> Dict[str, float]:
    text = str(hex_value or "#FFFFFF").strip().lstrip("#")
    if len(text) == 3:
        text = "".join(ch * 2 for ch in text)
    if len(text) != 6:
        text = "FFFFFF"
    return {
        "r": int(text[0:2], 16) / 255.0,
        "g": int(text[2:4], 16) / 255.0,
        "b": int(text[4:6], 16) / 255.0,
        "a": alpha,
    }


def _text_props(text: str, style: Dict[str, Any], size_key: str = "fontSize", align: str = "Center") -> Dict[str, Any]:
    shadow = style.get("shadowOffset") or [5, 5]
    return {
        "text": text,
        "justification": align,
        "color": _hex_to_color(style.get("fillColor", "#FFFFFF")),
        "fontSize": _number(style.get(size_key), 72),
        "autoWrapText": True,
        "shadowOffset": {"x": _number(shadow[0] if len(shadow) > 0 else 5), "y": _number(shadow[1] if len(shadow) > 1 else 5)},
        "shadowColor": _hex_to_color(style.get("shadowColor", "#000000"), 0.9),
    }


def build_widget_spec_from_manifest(manifest: Dict[str, Any]) -> Dict[str, Any]:
    resolution = manifest.get("resolution") or DEFAULT_RESOLUTION
    style = manifest.get("style") or default_style()
    children: List[Dict[str, Any]] = []
    for cue in manifest.get("cues", []):
        cue_id = _safe_id(cue.get("id"), "cue")
        box_px = denormalize_box(cue.get("box", [0, 0, 0.25, 0.1]), resolution)
        cue_type = cue.get("type", "main_title")
        group_children: List[Dict[str, Any]] = []
        if cue_type == "rating_box":
            group_children.append({
                "type": "TextBlock",
                "name": f"{cue_id}_Text",
                "properties": _text_props(str(cue.get("text", "")), style, "fontSize", "Center"),
            })
        elif cue_type == "two_line_credit":
            group_children.extend([
                {
                    "type": "TextBlock",
                    "name": f"{cue_id}_Role",
                    "properties": _text_props(str(cue.get("role", "")), style, "roleFontSize", "Center"),
                    "slot": {"padding": {"left": 0, "top": 0, "right": 0, "bottom": 8}},
                },
                {
                    "type": "TextBlock",
                    "name": f"{cue_id}_Name",
                    "properties": _text_props(str(cue.get("name", cue.get("text", ""))), style, "nameFontSize", "Center"),
                },
            ])
        else:
            group_children.append({
                "type": "TextBlock",
                "name": f"{cue_id}_Text",
                "properties": _text_props(str(cue.get("text", "")), style, "fontSize", "Center"),
            })
        children.append({
            "type": "Border",
            "name": f"{cue_id}_Group",
            "properties": {
                "renderOpacity": 0.0,
                "color": {"r": 0, "g": 0, "b": 0, "a": 0.0},
                "padding": {"left": 0, "top": 0, "right": 0, "bottom": 0},
            },
            "slot": {
                "position": {"x": box_px[0], "y": box_px[1]},
                "size": {"x": box_px[2], "y": box_px[3]},
                "alignment": {"x": 0, "y": 0},
                "zOrder": 100,
            },
            "children": [{
                "type": "VerticalBox",
                "name": f"{cue_id}_Stack",
                "slot": {"alignment": {"x": 0.5, "y": 0.5}},
                "children": group_children,
            }],
        })
    return {
        "root": {
            "type": "CanvasPanel",
            "name": "RootCanvas",
            "properties": {"renderOpacity": 1.0},
            "children": children,
        },
        "animations": [],
        "title_manifest": manifest,
    }


def manifest_from_reference(params: Dict[str, Any]) -> Dict[str, Any]:
    frame_size = params.get("frame_size") or params.get("reference_size") or DEFAULT_RESOLUTION
    cues: List[Dict[str, Any]] = []
    for index, raw in enumerate(params.get("cues") or []):
        cue = dict(raw)
        if "pixel_box" in cue:
            cue["box"] = normalize_pixel_box(cue.pop("pixel_box"), frame_size)
        cue.setdefault("id", f"cue_{index + 1:02d}")
        cue.setdefault("type", "main_title")
        cue.setdefault("fadeInFrames", 6)
        cue.setdefault("holdFrames", 90)
        cue.setdefault("fadeOutFrames", 6)
        cues.append(cue)
    return build_manifest({
        "id": params.get("id", "reference_titles"),
        "resolution": params.get("resolution") or frame_size,
        "fps": params.get("fps", DEFAULT_FPS),
        "style": params.get("style") or {},
        "gapFrames": params.get("gapFrames"),
        "cues": cues,
    })


def _load_manifest_from_params(params: Dict[str, Any]) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
    if isinstance(params.get("manifest"), dict):
        return build_manifest({"manifest": params["manifest"]}), None
    path = params.get("manifest_path")
    if path:
        try:
            return _load_json_file(str(path)), None
        except Exception as exc:
            return None, str(exc)
    manifest_id = params.get("id")
    if manifest_id:
        path = _manifest_path(str(manifest_id))
        if os.path.exists(path):
            return _load_json_file(path), None
    return None, "Provide manifest, manifest_path, or id"


def handle_title_manifest_create(params: Dict[str, Any]) -> Dict[str, Any]:
    try:
        manifest = build_manifest(params)
        validation = validate_manifest(manifest)
        path = str(params.get("output_path") or _manifest_path(manifest["id"]))
        _write_json_file(path, manifest)
        return {"success": validation["valid"], "data": {"path": path, "manifest": manifest, "validation": validation}, "error": None if validation["valid"] else "; ".join(validation["errors"])}
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def handle_title_manifest_validate(params: Dict[str, Any]) -> Dict[str, Any]:
    try:
        manifest, error = _load_manifest_from_params(params)
        if error:
            return {"success": False, "data": {}, "error": error}
        validation = validate_manifest(manifest or {})
        return {"success": validation["valid"], "data": {"manifest": manifest, "validation": validation}, "error": None if validation["valid"] else "; ".join(validation["errors"])}
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def handle_title_manifest_from_reference(params: Dict[str, Any]) -> Dict[str, Any]:
    try:
        manifest = manifest_from_reference(params)
        validation = validate_manifest(manifest)
        path = str(params.get("output_path") or _manifest_path(manifest["id"]))
        _write_json_file(path, manifest)
        return {"success": validation["valid"], "data": {"path": path, "manifest": manifest, "validation": validation}, "error": None if validation["valid"] else "; ".join(validation["errors"])}
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def handle_title_widget_build_from_manifest(params: Dict[str, Any]) -> Dict[str, Any]:
    try:
        manifest, error = _load_manifest_from_params(params)
        if error:
            return {"success": False, "data": {}, "error": error}
        validation = validate_manifest(manifest or {})
        if not validation["valid"]:
            return {"success": False, "data": {"validation": validation}, "error": "; ".join(validation["errors"])}
        widget_spec = build_widget_spec_from_manifest(manifest or {})
        if params.get("dry_run", False):
            return {"success": True, "data": {"widget_spec": widget_spec, "manifest": manifest}}

        import unreal

        package_path = str(params.get("package_path", "/Game/UI/Titles"))
        asset_name = str(params.get("asset_name", "WBP_TitleRenderer"))
        lib = getattr(unreal, "WidgetBlueprintBuilderLibrary", None)
        if lib is None:
            return {"success": False, "data": {"widget_spec": widget_spec}, "error": "WidgetBlueprintBuilderLibrary not available"}
        unreal.EditorAssetLibrary.make_directory(package_path)
        result = lib.build_widget_from_json(package_path, asset_name, json.dumps(widget_spec))
        ok = result == ""
        return {"success": ok, "data": {"path": f"{package_path}/{asset_name}", "widget_spec": widget_spec}, "error": str(result) if not ok else None}
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def handle_title_controller_create(params: Dict[str, Any]) -> Dict[str, Any]:
    try:
        manifest, error = _load_manifest_from_params(params)
        if error:
            return {"success": False, "data": {}, "error": error}
        controller_spec = {
            "name": str(params.get("asset_name", "BP_TitleController")),
            "package_path": str(params.get("package_path", "/Game/Cinematics/Titles")),
            "manifest_id": (manifest or {}).get("id"),
            "manifest": manifest,
            "functions": ["LoadTitleManifest", "CreateTitleWidget", "UpdateTitlesAtFrame", "HideAllTitles"],
            "runtime_note": "v1 scaffold: use this spec to implement Blueprint/C++ frame update logic; current UE4 Python bridge cannot author the full runtime graph safely.",
        }
        path = os.path.join(_ensure_title_dir(), f"{_safe_id(controller_spec['name'])}_controller_spec.json")
        _write_json_file(path, controller_spec)
        return {"success": True, "data": {"controller_spec_path": path, "controller_spec": controller_spec}}
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def handle_title_sequence_bind(params: Dict[str, Any]) -> Dict[str, Any]:
    try:
        manifest, error = _load_manifest_from_params(params)
        if error:
            return {"success": False, "data": {}, "error": error}
        sequence_spec = {
            "sequence_path": params.get("sequence_path", "/Game/Cinematics/LS_TitleSequence"),
            "controller_path": params.get("controller_path", "/Game/Cinematics/Titles/BP_TitleController"),
            "manifest_id": (manifest or {}).get("id"),
            "fps": (manifest or {}).get("fps", DEFAULT_FPS),
            "durationFrames": max([cue_end_frame(cue) for cue in (manifest or {}).get("cues", [])] or [0]),
            "binding_mode": "frame_event_scaffold",
            "runtime_note": "v1 scaffold: create the Level Sequence asset separately or via PromptBrush; full event-track binding needs a dedicated Sequencer bridge tool.",
        }
        path = os.path.join(_ensure_title_dir(), f"{_safe_id((manifest or {}).get('id', 'title'))}_sequence_bind.json")
        _write_json_file(path, sequence_spec)
        return {"success": True, "data": {"sequence_spec_path": path, "sequence_spec": sequence_spec}}
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def _image_compare(reference_path: str, render_path: str, region: Optional[List[int]] = None) -> Dict[str, Any]:
    try:
        from PIL import Image, ImageChops, ImageStat
    except Exception as exc:
        return {"available": False, "error": f"Pillow not available: {exc}"}
    ref = Image.open(reference_path).convert("RGB")
    rnd = Image.open(render_path).convert("RGB")
    if ref.size != rnd.size:
        rnd = rnd.resize(ref.size)
    if region:
        x, y, w, h = region
        box = (x, y, x + w, y + h)
        ref = ref.crop(box)
        rnd = rnd.crop(box)
    diff = ImageChops.difference(ref, rnd)
    stat = ImageStat.Stat(diff)
    mae = sum(stat.mean) / len(stat.mean)
    extrema = diff.getextrema()
    max_error = max(channel[1] for channel in extrema)
    return {"available": True, "mean_absolute_error": mae, "max_channel_error": max_error, "size": list(ref.size)}


def handle_title_render_compare(params: Dict[str, Any]) -> Dict[str, Any]:
    try:
        reference_path = str(params.get("reference_path", ""))
        render_path = str(params.get("render_path", ""))
        if not reference_path or not render_path:
            return {"success": False, "data": {}, "error": "reference_path and render_path are required"}
        region = params.get("region")
        result = _image_compare(reference_path, render_path, region if isinstance(region, list) else None)
        return {"success": bool(result.get("available")), "data": result, "error": result.get("error")}
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def handle_title_manifest_adjust(params: Dict[str, Any]) -> Dict[str, Any]:
    try:
        manifest, error = _load_manifest_from_params(params)
        if error:
            return {"success": False, "data": {}, "error": error}
        adjusted = dict(manifest or {})
        cues = [dict(cue) for cue in adjusted.get("cues", [])]
        for patch in params.get("adjustments") or []:
            cue_id = patch.get("id")
            for cue in cues:
                if cue.get("id") == cue_id:
                    if "box_delta" in patch:
                        delta = patch["box_delta"]
                        cue["box"] = [_number(cue["box"][i]) + _number(delta[i]) for i in range(4)]
                    if "box" in patch:
                        cue["box"] = patch["box"]
                    for key in ("startFrame", "fadeInFrames", "holdFrames", "fadeOutFrames", "text", "role", "name"):
                        if key in patch:
                            cue[key] = patch[key]
        adjusted["cues"] = cues
        validation = validate_manifest(adjusted)
        path = str(params.get("output_path") or _manifest_path(adjusted.get("id", "title_manifest")))
        _write_json_file(path, adjusted)
        return {"success": validation["valid"], "data": {"path": path, "manifest": adjusted, "validation": validation}, "error": None if validation["valid"] else "; ".join(validation["errors"])}
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}
