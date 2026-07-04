# audio_generator.py -- Generate SoundAttenuation, SoundClass, and SoundMix assets.
from __future__ import annotations
from typing import Any, Dict, List, Tuple

from mcp_bridge.generation.spec_schema import SoundAttenuationSpec, SoundClassSpec, SoundMixSpec


_FALLOFF_MAP: Dict[str, str] = {
    "Logarithmic": "ATTENUATION_Logarithmic",
    "Linear":      "ATTENUATION_Linear",
    "Inverse":     "ATTENUATION_Inverse",
    "LogReverse":  "ATTENUATION_LogReverse",
    "NaturalSound":"ATTENUATION_NaturalSound",
}


def generate_sound_attenuation(spec: SoundAttenuationSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a SoundAttenuation asset."""
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        factory_cls = getattr(unreal, "SoundAttenuationFactory", None)
        if factory_cls is None:
            return False, "SoundAttenuationFactory not available", {}

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        attn = asset_tools.create_asset(spec.name, spec.content_path, unreal.SoundAttenuation, factory_cls())
        if attn is None:
            return False, f"Failed to create SoundAttenuation: {full_path}", {}

        try:
            settings = attn.get_editor_property("attenuation")
            settings.set_editor_property("inner_radius", spec.radius_min)
            settings.set_editor_property("falloff_distance", spec.radius_max)
            settings.set_editor_property("b_spatialize", spec.spatialization_enabled)
            attn.set_editor_property("attenuation", settings)
        except Exception:
            pass  # property layout may vary; asset created is still valid

        unreal.EditorAssetLibrary.save_asset(full_path)
        return True, "", {"path": full_path, "skipped": False}

    except Exception as e:
        return False, str(e), {}


def generate_sound_class(spec: SoundClassSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a SoundClass asset."""
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        factory_cls = getattr(unreal, "SoundClassFactory", None)
        if factory_cls is None:
            return False, "SoundClassFactory not available", {}

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        sc = asset_tools.create_asset(spec.name, spec.content_path, unreal.SoundClass, factory_cls())
        if sc is None:
            return False, f"Failed to create SoundClass: {full_path}", {}

        try:
            props = sc.get_editor_property("properties")
            props.set_editor_property("volume", spec.volume)
            props.set_editor_property("pitch", spec.pitch)
            sc.set_editor_property("properties", props)
        except Exception:
            pass

        unreal.EditorAssetLibrary.save_asset(full_path)
        return True, "", {"path": full_path, "skipped": False}

    except Exception as e:
        return False, str(e), {}


def generate_sound_mix(spec: SoundMixSpec) -> Tuple[bool, str, Dict[str, Any]]:
    """Create a SoundMix asset."""
    try:
        import unreal

        full_path = f"{spec.content_path}/{spec.name}"
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return True, "", {"path": full_path, "skipped": True}

        unreal.EditorAssetLibrary.make_directory(spec.content_path)

        factory_cls = getattr(unreal, "SoundMixFactory", None)
        if factory_cls is None:
            return False, "SoundMixFactory not available", {}

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        mix = asset_tools.create_asset(spec.name, spec.content_path, unreal.SoundMix, factory_cls())
        if mix is None:
            return False, f"Failed to create SoundMix: {full_path}", {}

        try:
            mix.set_editor_property("fade_in_time", spec.fade_in_time)
            mix.set_editor_property("fade_out_time", spec.fade_out_time)
        except Exception:
            pass

        unreal.EditorAssetLibrary.save_asset(full_path)
        return True, "", {"path": full_path, "skipped": False}

    except Exception as e:
        return False, str(e), {}


def _batch(items: list, fn) -> Dict[str, Any]:
    results, errors = [], []
    for item in items:
        ok, err, data = fn(item)
        entry: Dict[str, Any] = {"name": item.name, "success": ok, "data": data}
        if err:
            entry["error"] = err
            errors.append(f"{item.name}: {err}")
        results.append(entry)
    return {
        "results": results, "errors": errors,
        "total": len(items), "succeeded": sum(1 for r in results if r["success"]),
    }


def generate_all_sound_attenuations(specs: List[SoundAttenuationSpec]) -> Dict[str, Any]:
    return _batch(specs, generate_sound_attenuation)


def generate_all_sound_classes(specs: List[SoundClassSpec]) -> Dict[str, Any]:
    return _batch(specs, generate_sound_class)


def generate_all_sound_mixes(specs: List[SoundMixSpec]) -> Dict[str, Any]:
    return _batch(specs, generate_sound_mix)
