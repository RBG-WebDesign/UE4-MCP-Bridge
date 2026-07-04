"""High-level UMG widget authoring helpers."""

from __future__ import annotations

import json
from typing import Any, Dict, List


TITLE_PRESETS = {
    "cinematic": {
        "title_color": {"r": 1.0, "g": 0.93, "b": 0.78, "a": 1.0},
        "subtitle_color": {"r": 0.82, "g": 0.86, "b": 0.9, "a": 1.0},
        "background_color": {"r": 0.02, "g": 0.018, "b": 0.016, "a": 0.72},
        "accent_color": {"r": 0.86, "g": 0.67, "b": 0.28, "a": 1.0},
    },
    "broadcast": {
        "title_color": {"r": 1.0, "g": 1.0, "b": 1.0, "a": 1.0},
        "subtitle_color": {"r": 0.78, "g": 0.88, "b": 1.0, "a": 1.0},
        "background_color": {"r": 0.015, "g": 0.03, "b": 0.06, "a": 0.78},
        "accent_color": {"r": 0.1, "g": 0.48, "b": 1.0, "a": 1.0},
    },
    "horror": {
        "title_color": {"r": 0.96, "g": 0.94, "b": 0.88, "a": 1.0},
        "subtitle_color": {"r": 0.65, "g": 0.69, "b": 0.7, "a": 1.0},
        "background_color": {"r": 0.015, "g": 0.012, "b": 0.011, "a": 0.82},
        "accent_color": {"r": 0.55, "g": 0.04, "b": 0.025, "a": 1.0},
    },
    "clean": {
        "title_color": {"r": 0.08, "g": 0.1, "b": 0.12, "a": 1.0},
        "subtitle_color": {"r": 0.26, "g": 0.3, "b": 0.34, "a": 1.0},
        "background_color": {"r": 0.96, "g": 0.97, "b": 0.98, "a": 0.88},
        "accent_color": {"r": 0.0, "g": 0.45, "b": 0.72, "a": 1.0},
    },
}


def _color(params: Dict[str, Any], key: str, fallback: Dict[str, float]) -> Dict[str, float]:
    value = params.get(key)
    if isinstance(value, dict):
        return {
            "r": float(value.get("r", fallback["r"])),
            "g": float(value.get("g", fallback["g"])),
            "b": float(value.get("b", fallback["b"])),
            "a": float(value.get("a", fallback["a"])),
        }
    return dict(fallback)


def _fade_animations(target_name: str, fade_in: float, hold: float, fade_out: float) -> List[Dict[str, Any]]:
    fade_in = max(float(fade_in), 0.01)
    fade_out = max(float(fade_out), 0.01)
    hold = max(float(hold), 0.0)
    return [
        {
            "name": "FadeIn",
            "target": target_name,
            "duration": fade_in,
            "tracks": [{"type": "opacity", "from": 0.0, "to": 1.0}],
        },
        {
            "name": "FadeOut",
            "target": target_name,
            "duration": fade_out,
            "tracks": [{"type": "opacity", "from": 1.0, "to": 0.0}],
        },
        {
            "name": "TitleSequence",
            "target": target_name,
            "duration": fade_in + hold + fade_out,
            "tracks": [{"type": "opacity", "from": 0.0, "to": 1.0}],
        },
    ]


def build_title_widget_spec(params: Dict[str, Any], lower_third: bool = False) -> Dict[str, Any]:
    preset = TITLE_PRESETS.get(str(params.get("preset", "cinematic")), TITLE_PRESETS["cinematic"])
    title = str(params.get("title", "TITLE"))
    subtitle = str(params.get("subtitle", ""))
    eyebrow = str(params.get("eyebrow", ""))
    target_name = "TitleGroup"

    title_color = _color(params, "title_color", preset["title_color"])
    subtitle_color = _color(params, "subtitle_color", preset["subtitle_color"])
    background_color = _color(params, "background_color", preset["background_color"])
    accent_color = _color(params, "accent_color", preset["accent_color"])

    width = float(params.get("width", 1440 if lower_third else 1280))
    height = float(params.get("height", 220 if lower_third else 360))
    x = float(params.get("x", 80 if lower_third else 320))
    y = float(params.get("y", 760 if lower_third else 360))

    text_children: List[Dict[str, Any]] = []
    if eyebrow:
        text_children.append({
            "type": "TextBlock",
            "name": "EyebrowText",
            "properties": {
                "text": eyebrow,
                "justification": "Left" if lower_third else "Center",
                "color": accent_color,
                "fontSize": float(params.get("eyebrow_font_size", 28 if lower_third else 34)),
                "shadowOffset": {"x": 1, "y": 1},
                "shadowColor": {"r": 0, "g": 0, "b": 0, "a": 0.65},
            },
            "slot": {"padding": {"left": 0, "top": 0, "right": 0, "bottom": 10}},
        })
    text_children.append({
        "type": "TextBlock",
        "name": "TitleText",
        "properties": {
            "text": title,
            "justification": "Left" if lower_third else "Center",
            "color": title_color,
            "fontSize": float(params.get("title_font_size", 58 if lower_third else 86)),
            "autoWrapText": True,
            "shadowOffset": {"x": 2, "y": 2},
            "shadowColor": {"r": 0, "g": 0, "b": 0, "a": 0.72},
        },
        "slot": {"padding": {"left": 0, "top": 0, "right": 0, "bottom": 8}},
    })
    if subtitle:
        text_children.append({
            "type": "TextBlock",
            "name": "SubtitleText",
            "properties": {
                "text": subtitle,
                "justification": "Left" if lower_third else "Center",
                "color": subtitle_color,
                "fontSize": float(params.get("subtitle_font_size", 34 if lower_third else 42)),
                "autoWrapText": True,
                "shadowOffset": {"x": 1, "y": 1},
                "shadowColor": {"r": 0, "g": 0, "b": 0, "a": 0.62},
            },
        })

    root = {
        "type": "CanvasPanel",
        "name": "RootCanvas",
        "children": [
            {
                "type": "Border",
                "name": target_name,
                "properties": {
                    "renderOpacity": 0.0,
                    "color": background_color,
                    "padding": {"left": 44, "top": 30, "right": 44, "bottom": 30},
                },
                "slot": {
                    "position": {"x": x, "y": y},
                    "size": {"x": width, "y": height},
                    "alignment": {"x": 0.0, "y": 0.5 if lower_third else 0.5},
                    "zOrder": 100,
                },
                "children": [
                    {
                        "type": "VerticalBox",
                        "name": "TextStack",
                        "slot": {
                            "alignment": {"x": 0.0 if lower_third else 0.5, "y": 0.5},
                        },
                        "children": text_children,
                    }
                ],
            }
        ],
    }

    return {
        "root": root,
        "animations": _fade_animations(
            target_name,
            float(params.get("fade_in", 0.35)),
            float(params.get("hold", 2.2)),
            float(params.get("fade_out", 0.45)),
        ),
    }


def _build_widget_asset(package_path: str, asset_name: str, spec: Dict[str, Any]) -> Dict[str, Any]:
    try:
        import unreal

        lib = getattr(unreal, "WidgetBlueprintBuilderLibrary", None)
        if lib is None:
            return {"success": False, "data": {}, "error": "WidgetBlueprintBuilderLibrary not available"}
        if not package_path or not asset_name:
            return {"success": False, "data": {}, "error": "Missing package_path or asset_name"}
        unreal.EditorAssetLibrary.make_directory(package_path)
        result = lib.build_widget_from_json(package_path, asset_name, json.dumps(spec))
        ok = result == ""
        return {
            "success": ok,
            "data": {
                "path": f"{package_path}/{asset_name}",
                "spec": spec,
                "animations": [anim.get("name") for anim in spec.get("animations", [])],
            },
            "error": str(result) if not ok else None,
        }
    except Exception as exc:
        return {"success": False, "data": {}, "error": str(exc)}


def handle_widget_title_template(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return a title or lower-third widget spec without creating an asset."""
    lower_third = str(params.get("style", "title")) == "lower_third" or bool(params.get("lower_third", False))
    spec = build_title_widget_spec(params, lower_third=lower_third)
    return {"success": True, "data": {"spec": spec, "preset_names": sorted(TITLE_PRESETS.keys())}}


def handle_widget_title_card_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a full-screen centered title card with fade animations."""
    package_path = str(params.get("package_path", "/Game/UI/Titles"))
    asset_name = str(params.get("asset_name", "WBP_TitleCard"))
    spec = build_title_widget_spec(params, lower_third=False)
    return _build_widget_asset(package_path, asset_name, spec)


def handle_widget_lower_third_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a broadcast-style lower third with fade animations."""
    package_path = str(params.get("package_path", "/Game/UI/Titles"))
    asset_name = str(params.get("asset_name", "WBP_LowerThird"))
    spec = build_title_widget_spec(params, lower_third=True)
    return _build_widget_asset(package_path, asset_name, spec)
