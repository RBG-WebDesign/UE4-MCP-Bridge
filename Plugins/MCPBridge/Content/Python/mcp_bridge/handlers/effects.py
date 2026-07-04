"""Post-processing, camera shake, and visual effects handlers.

Spawn and configure PostProcessVolumes, camera shake actors, and
apply glitch/distortion effects via post-process materials.

NOTE: PostProcessVolume changes ARE transactable (they create actors).
Camera shake playback during PIE is NOT transactable.
"""

from typing import Any, Dict, List


def _find_actor_by_name(name: str) -> Any:
    """Find an actor in the current level by its label."""
    import unreal
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if actor.get_actor_label() == name:
            return actor
    return None


def _set_pp_override(settings: Any, prop_name: str, value: Any) -> None:
    """Enable a post-process override flag and set the property value.

    UE4 requires override_<property> = True before the property takes effect.
    Uses setattr for the override flag and the value.
    """
    override_flag = f"override_{prop_name}"
    try:
        setattr(settings, override_flag, True)
    except AttributeError:
        pass  # Some properties don't have separate override flags
    setattr(settings, prop_name, value)


def _vector4_from_dict(d: Dict[str, float]) -> Any:
    """Create an unreal.Vector4 from a dict with r/g/b/a or x/y/z/w keys."""
    import unreal
    if "r" in d:
        return unreal.Vector4(d.get("r", 1.0), d.get("g", 1.0), d.get("b", 1.0), d.get("a", 1.0))
    return unreal.Vector4(d.get("x", 1.0), d.get("y", 1.0), d.get("z", 1.0), d.get("w", 1.0))


def _linear_color_from_dict(d: Dict[str, float]) -> Any:
    """Create an unreal.LinearColor from a dict with r/g/b/a keys."""
    import unreal
    return unreal.LinearColor(d.get("r", 1.0), d.get("g", 1.0), d.get("b", 1.0), d.get("a", 1.0))


def _apply_post_process_settings(settings: Any, pp_params: Dict[str, Any]) -> List[str]:
    """Apply a dict of post-process settings to an FPostProcessSettings struct.

    Returns a list of property names that were successfully applied.
    """
    import unreal
    applied = []

    # ── Bloom ──
    if "bloom_intensity" in pp_params:
        _set_pp_override(settings, "bloom_intensity", float(pp_params["bloom_intensity"]))
        applied.append("bloom_intensity")
    if "bloom_threshold" in pp_params:
        _set_pp_override(settings, "bloom_threshold", float(pp_params["bloom_threshold"]))
        applied.append("bloom_threshold")
    if "bloom_size_scale" in pp_params:
        _set_pp_override(settings, "bloom_size_scale", float(pp_params["bloom_size_scale"]))
        applied.append("bloom_size_scale")
    if "bloom_convolution_scatter_dispersion" in pp_params:
        _set_pp_override(settings, "bloom_convolution_scatter_dispersion", float(pp_params["bloom_convolution_scatter_dispersion"]))
        applied.append("bloom_convolution_scatter_dispersion")

    # ── Exposure ──
    if "auto_exposure_bias" in pp_params:
        _set_pp_override(settings, "auto_exposure_bias", float(pp_params["auto_exposure_bias"]))
        applied.append("auto_exposure_bias")
    if "auto_exposure_min_brightness" in pp_params:
        _set_pp_override(settings, "auto_exposure_min_brightness", float(pp_params["auto_exposure_min_brightness"]))
        applied.append("auto_exposure_min_brightness")
    if "auto_exposure_max_brightness" in pp_params:
        _set_pp_override(settings, "auto_exposure_max_brightness", float(pp_params["auto_exposure_max_brightness"]))
        applied.append("auto_exposure_max_brightness")
    if "auto_exposure_speed_up" in pp_params:
        _set_pp_override(settings, "auto_exposure_speed_up", float(pp_params["auto_exposure_speed_up"]))
        applied.append("auto_exposure_speed_up")
    if "auto_exposure_speed_down" in pp_params:
        _set_pp_override(settings, "auto_exposure_speed_down", float(pp_params["auto_exposure_speed_down"]))
        applied.append("auto_exposure_speed_down")
    if "auto_exposure_method" in pp_params:
        # 0 = Histogram, 1 = Basic (Manual)
        method = int(pp_params["auto_exposure_method"])
        _set_pp_override(settings, "auto_exposure_method", method)
        applied.append("auto_exposure_method")

    # ── Color Grading ──
    if "color_saturation" in pp_params:
        _set_pp_override(settings, "color_saturation", _vector4_from_dict(pp_params["color_saturation"]))
        applied.append("color_saturation")
    if "color_contrast" in pp_params:
        _set_pp_override(settings, "color_contrast", _vector4_from_dict(pp_params["color_contrast"]))
        applied.append("color_contrast")
    if "color_gamma" in pp_params:
        _set_pp_override(settings, "color_gamma", _vector4_from_dict(pp_params["color_gamma"]))
        applied.append("color_gamma")
    if "color_gain" in pp_params:
        _set_pp_override(settings, "color_gain", _vector4_from_dict(pp_params["color_gain"]))
        applied.append("color_gain")
    if "color_offset" in pp_params:
        _set_pp_override(settings, "color_offset", _vector4_from_dict(pp_params["color_offset"]))
        applied.append("color_offset")

    # ── Color Grading - Shadows/Midtones/Highlights ──
    if "color_saturation_shadows" in pp_params:
        _set_pp_override(settings, "color_saturation_shadows", _vector4_from_dict(pp_params["color_saturation_shadows"]))
        applied.append("color_saturation_shadows")
    if "color_contrast_shadows" in pp_params:
        _set_pp_override(settings, "color_contrast_shadows", _vector4_from_dict(pp_params["color_contrast_shadows"]))
        applied.append("color_contrast_shadows")
    if "color_gamma_shadows" in pp_params:
        _set_pp_override(settings, "color_gamma_shadows", _vector4_from_dict(pp_params["color_gamma_shadows"]))
        applied.append("color_gamma_shadows")
    if "color_gain_shadows" in pp_params:
        _set_pp_override(settings, "color_gain_shadows", _vector4_from_dict(pp_params["color_gain_shadows"]))
        applied.append("color_gain_shadows")
    if "color_offset_shadows" in pp_params:
        _set_pp_override(settings, "color_offset_shadows", _vector4_from_dict(pp_params["color_offset_shadows"]))
        applied.append("color_offset_shadows")
    if "color_saturation_midtones" in pp_params:
        _set_pp_override(settings, "color_saturation_midtones", _vector4_from_dict(pp_params["color_saturation_midtones"]))
        applied.append("color_saturation_midtones")
    if "color_contrast_midtones" in pp_params:
        _set_pp_override(settings, "color_contrast_midtones", _vector4_from_dict(pp_params["color_contrast_midtones"]))
        applied.append("color_contrast_midtones")
    if "color_gamma_midtones" in pp_params:
        _set_pp_override(settings, "color_gamma_midtones", _vector4_from_dict(pp_params["color_gamma_midtones"]))
        applied.append("color_gamma_midtones")
    if "color_gain_midtones" in pp_params:
        _set_pp_override(settings, "color_gain_midtones", _vector4_from_dict(pp_params["color_gain_midtones"]))
        applied.append("color_gain_midtones")
    if "color_offset_midtones" in pp_params:
        _set_pp_override(settings, "color_offset_midtones", _vector4_from_dict(pp_params["color_offset_midtones"]))
        applied.append("color_offset_midtones")
    if "color_saturation_highlights" in pp_params:
        _set_pp_override(settings, "color_saturation_highlights", _vector4_from_dict(pp_params["color_saturation_highlights"]))
        applied.append("color_saturation_highlights")
    if "color_contrast_highlights" in pp_params:
        _set_pp_override(settings, "color_contrast_highlights", _vector4_from_dict(pp_params["color_contrast_highlights"]))
        applied.append("color_contrast_highlights")
    if "color_gamma_highlights" in pp_params:
        _set_pp_override(settings, "color_gamma_highlights", _vector4_from_dict(pp_params["color_gamma_highlights"]))
        applied.append("color_gamma_highlights")
    if "color_gain_highlights" in pp_params:
        _set_pp_override(settings, "color_gain_highlights", _vector4_from_dict(pp_params["color_gain_highlights"]))
        applied.append("color_gain_highlights")
    if "color_offset_highlights" in pp_params:
        _set_pp_override(settings, "color_offset_highlights", _vector4_from_dict(pp_params["color_offset_highlights"]))
        applied.append("color_offset_highlights")

    # ── White Balance ──
    if "white_temp" in pp_params:
        _set_pp_override(settings, "white_temp", float(pp_params["white_temp"]))
        applied.append("white_temp")
    if "white_tint" in pp_params:
        _set_pp_override(settings, "white_tint", float(pp_params["white_tint"]))
        applied.append("white_tint")

    # ── Film ──
    if "film_slope" in pp_params:
        _set_pp_override(settings, "film_slope", float(pp_params["film_slope"]))
        applied.append("film_slope")
    if "film_toe" in pp_params:
        _set_pp_override(settings, "film_toe", float(pp_params["film_toe"]))
        applied.append("film_toe")
    if "film_shoulder" in pp_params:
        _set_pp_override(settings, "film_shoulder", float(pp_params["film_shoulder"]))
        applied.append("film_shoulder")
    if "film_black_clip" in pp_params:
        _set_pp_override(settings, "film_black_clip", float(pp_params["film_black_clip"]))
        applied.append("film_black_clip")
    if "film_white_clip" in pp_params:
        _set_pp_override(settings, "film_white_clip", float(pp_params["film_white_clip"]))
        applied.append("film_white_clip")

    # ── Chromatic Aberration (glitch/distortion) ──
    if "scene_fringe_intensity" in pp_params:
        _set_pp_override(settings, "scene_fringe_intensity", float(pp_params["scene_fringe_intensity"]))
        applied.append("scene_fringe_intensity")

    # ── Vignette ──
    if "vignette_intensity" in pp_params:
        _set_pp_override(settings, "vignette_intensity", float(pp_params["vignette_intensity"]))
        applied.append("vignette_intensity")

    # ── Grain (film grain / noise glitch) ──
    if "grain_intensity" in pp_params:
        _set_pp_override(settings, "grain_intensity", float(pp_params["grain_intensity"]))
        applied.append("grain_intensity")
    if "grain_jitter" in pp_params:
        _set_pp_override(settings, "grain_jitter", float(pp_params["grain_jitter"]))
        applied.append("grain_jitter")

    # ── Ambient Occlusion ──
    if "ambient_occlusion_intensity" in pp_params:
        _set_pp_override(settings, "ambient_occlusion_intensity", float(pp_params["ambient_occlusion_intensity"]))
        applied.append("ambient_occlusion_intensity")
    if "ambient_occlusion_radius" in pp_params:
        _set_pp_override(settings, "ambient_occlusion_radius", float(pp_params["ambient_occlusion_radius"]))
        applied.append("ambient_occlusion_radius")
    if "ambient_occlusion_static_fraction" in pp_params:
        _set_pp_override(settings, "ambient_occlusion_static_fraction", float(pp_params["ambient_occlusion_static_fraction"]))
        applied.append("ambient_occlusion_static_fraction")
    if "ambient_occlusion_fade_distance" in pp_params:
        _set_pp_override(settings, "ambient_occlusion_fade_distance", float(pp_params["ambient_occlusion_fade_distance"]))
        applied.append("ambient_occlusion_fade_distance")
    if "ambient_occlusion_power" in pp_params:
        _set_pp_override(settings, "ambient_occlusion_power", float(pp_params["ambient_occlusion_power"]))
        applied.append("ambient_occlusion_power")
    if "ambient_occlusion_bias" in pp_params:
        _set_pp_override(settings, "ambient_occlusion_bias", float(pp_params["ambient_occlusion_bias"]))
        applied.append("ambient_occlusion_bias")
    if "ambient_occlusion_quality" in pp_params:
        _set_pp_override(settings, "ambient_occlusion_quality", float(pp_params["ambient_occlusion_quality"]))
        applied.append("ambient_occlusion_quality")

    # ── Depth of Field ──
    if "depth_of_field_focal_distance" in pp_params:
        _set_pp_override(settings, "depth_of_field_focal_distance", float(pp_params["depth_of_field_focal_distance"]))
        applied.append("depth_of_field_focal_distance")
    if "depth_of_field_fstop" in pp_params:
        _set_pp_override(settings, "depth_of_field_fstop", float(pp_params["depth_of_field_fstop"]))
        applied.append("depth_of_field_fstop")
    if "depth_of_field_sensor_width" in pp_params:
        _set_pp_override(settings, "depth_of_field_sensor_width", float(pp_params["depth_of_field_sensor_width"]))
        applied.append("depth_of_field_sensor_width")
    if "depth_of_field_min_fstop" in pp_params:
        _set_pp_override(settings, "depth_of_field_min_fstop", float(pp_params["depth_of_field_min_fstop"]))
        applied.append("depth_of_field_min_fstop")
    if "depth_of_field_blade_count" in pp_params:
        _set_pp_override(settings, "depth_of_field_blade_count", int(pp_params["depth_of_field_blade_count"]))
        applied.append("depth_of_field_blade_count")
    if "depth_of_field_depth_blur_amount" in pp_params:
        _set_pp_override(settings, "depth_of_field_depth_blur_amount", float(pp_params["depth_of_field_depth_blur_amount"]))
        applied.append("depth_of_field_depth_blur_amount")
    if "depth_of_field_depth_blur_radius" in pp_params:
        _set_pp_override(settings, "depth_of_field_depth_blur_radius", float(pp_params["depth_of_field_depth_blur_radius"]))
        applied.append("depth_of_field_depth_blur_radius")
    if "depth_of_field_focal_region" in pp_params:
        _set_pp_override(settings, "depth_of_field_focal_region", float(pp_params["depth_of_field_focal_region"]))
        applied.append("depth_of_field_focal_region")
    if "depth_of_field_near_transition_region" in pp_params:
        _set_pp_override(settings, "depth_of_field_near_transition_region", float(pp_params["depth_of_field_near_transition_region"]))
        applied.append("depth_of_field_near_transition_region")
    if "depth_of_field_far_transition_region" in pp_params:
        _set_pp_override(settings, "depth_of_field_far_transition_region", float(pp_params["depth_of_field_far_transition_region"]))
        applied.append("depth_of_field_far_transition_region")
    if "depth_of_field_near_blur_size" in pp_params:
        _set_pp_override(settings, "depth_of_field_near_blur_size", float(pp_params["depth_of_field_near_blur_size"]))
        applied.append("depth_of_field_near_blur_size")
    if "depth_of_field_far_blur_size" in pp_params:
        _set_pp_override(settings, "depth_of_field_far_blur_size", float(pp_params["depth_of_field_far_blur_size"]))
        applied.append("depth_of_field_far_blur_size")
    if "depth_of_field_occlusion" in pp_params:
        _set_pp_override(settings, "depth_of_field_occlusion", float(pp_params["depth_of_field_occlusion"]))
        applied.append("depth_of_field_occlusion")
    if "depth_of_field_sky_focus_distance" in pp_params:
        _set_pp_override(settings, "depth_of_field_sky_focus_distance", float(pp_params["depth_of_field_sky_focus_distance"]))
        applied.append("depth_of_field_sky_focus_distance")

    # ── Motion Blur ──
    if "motion_blur_amount" in pp_params:
        _set_pp_override(settings, "motion_blur_amount", float(pp_params["motion_blur_amount"]))
        applied.append("motion_blur_amount")
    if "motion_blur_max" in pp_params:
        _set_pp_override(settings, "motion_blur_max", float(pp_params["motion_blur_max"]))
        applied.append("motion_blur_max")
    if "motion_blur_per_object_size" in pp_params:
        _set_pp_override(settings, "motion_blur_per_object_size", float(pp_params["motion_blur_per_object_size"]))
        applied.append("motion_blur_per_object_size")
    if "motion_blur_target_fps" in pp_params:
        _set_pp_override(settings, "motion_blur_target_fps", int(pp_params["motion_blur_target_fps"]))
        applied.append("motion_blur_target_fps")

    # ── Screen Space Reflections ──
    if "screen_space_reflection_intensity" in pp_params:
        _set_pp_override(settings, "screen_space_reflection_intensity", float(pp_params["screen_space_reflection_intensity"]))
        applied.append("screen_space_reflection_intensity")
    if "screen_space_reflection_quality" in pp_params:
        _set_pp_override(settings, "screen_space_reflection_quality", float(pp_params["screen_space_reflection_quality"]))
        applied.append("screen_space_reflection_quality")
    if "screen_space_reflection_max_roughness" in pp_params:
        _set_pp_override(settings, "screen_space_reflection_max_roughness", float(pp_params["screen_space_reflection_max_roughness"]))
        applied.append("screen_space_reflection_max_roughness")

    # ── Global Illumination ──
    if "indirect_lighting_color" in pp_params:
        _set_pp_override(settings, "indirect_lighting_color", _linear_color_from_dict(pp_params["indirect_lighting_color"]))
        applied.append("indirect_lighting_color")
    if "indirect_lighting_intensity" in pp_params:
        _set_pp_override(settings, "indirect_lighting_intensity", float(pp_params["indirect_lighting_intensity"]))
        applied.append("indirect_lighting_intensity")

    # ── Screen Percentage (resolution scale) ──
    if "screen_percentage" in pp_params:
        _set_pp_override(settings, "screen_percentage", float(pp_params["screen_percentage"]))
        applied.append("screen_percentage")

    # ── Color Grading LUT ──
    if "color_grading_lut" in pp_params:
        try:
            lut_path = str(pp_params["color_grading_lut"])
            lut_texture = unreal.EditorAssetLibrary.load_asset(lut_path)
            if lut_texture:
                _set_pp_override(settings, "color_grading_lut", lut_texture)
                applied.append("color_grading_lut")
        except Exception:
            pass  # LUT not found, skip silently

    # ── Lens Flare ──
    if "lens_flare_intensity" in pp_params:
        _set_pp_override(settings, "lens_flare_intensity", float(pp_params["lens_flare_intensity"]))
        applied.append("lens_flare_intensity")
    if "lens_flare_tint" in pp_params:
        _set_pp_override(settings, "lens_flare_tint", _linear_color_from_dict(pp_params["lens_flare_tint"]))
        applied.append("lens_flare_tint")
    if "lens_flare_bokeh_size" in pp_params:
        _set_pp_override(settings, "lens_flare_bokeh_size", float(pp_params["lens_flare_bokeh_size"]))
        applied.append("lens_flare_bokeh_size")
    if "lens_flare_threshold" in pp_params:
        _set_pp_override(settings, "lens_flare_threshold", float(pp_params["lens_flare_threshold"]))
        applied.append("lens_flare_threshold")

    # ── Misc ──
    if "scene_color_tint" in pp_params:
        _set_pp_override(settings, "scene_color_tint", _linear_color_from_dict(pp_params["scene_color_tint"]))
        applied.append("scene_color_tint")

    return applied


# ── PostProcessVolume: spawn / modify ──

def handle_pp_volume_spawn(params: Dict[str, Any]) -> Dict[str, Any]:
    """Spawn a PostProcessVolume with full settings control.

    Args:
        params:
            - name (str): Actor label (default: "PP_Effect")
            - location (dict): {x, y, z} spawn location (default: origin)
            - unbound (bool): Affect entire level (default: true)
            - blend_radius (float): Blend radius when not unbound (default: 100)
            - blend_weight (float): Effect blend weight 0-1 (default: 1.0)
            - priority (float): Priority when volumes overlap (default: 0)
            - enabled (bool): Whether the volume is enabled (default: true)
            - settings (dict): PostProcess settings - see full list below
    """
    try:
        import unreal

        name = params.get("name", "PP_Effect")
        loc = params.get("location", {"x": 0, "y": 0, "z": 0})
        spawn_loc = unreal.Vector(float(loc.get("x", 0)), float(loc.get("y", 0)), float(loc.get("z", 0)))

        volume = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.PostProcessVolume, spawn_loc
        )
        if not volume:
            return {"success": False, "data": {}, "error": "Failed to spawn PostProcessVolume"}

        volume.set_actor_label(name)

        # Volume properties
        volume.unbound = params.get("unbound", True)
        volume.blend_radius = float(params.get("blend_radius", 100.0))
        volume.blend_weight = float(params.get("blend_weight", 1.0))
        volume.priority = float(params.get("priority", 0.0))
        volume.set_is_temporarily_hidden_in_editor(not params.get("enabled", True))

        # Apply post-process settings
        pp_settings = params.get("settings", {})
        applied = []
        if pp_settings:
            applied = _apply_post_process_settings(volume.settings, pp_settings)

        return {
            "success": True,
            "data": {
                "actor_name": name,
                "unbound": volume.unbound,
                "blend_weight": volume.blend_weight,
                "priority": volume.priority,
                "settings_applied": applied,
                "settings_count": len(applied),
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_pp_volume_modify(params: Dict[str, Any]) -> Dict[str, Any]:
    """Modify an existing PostProcessVolume's settings.

    Args:
        params:
            - actor_name (str): Label of the PostProcessVolume to modify
            - unbound (bool): Change unbound state (optional)
            - blend_weight (float): Change blend weight (optional)
            - blend_radius (float): Change blend radius (optional)
            - priority (float): Change priority (optional)
            - settings (dict): PostProcess settings to update (merged, not replaced)
    """
    try:
        import unreal

        actor_name = params.get("actor_name", "")
        if not actor_name:
            return {"success": False, "data": {}, "error": "Missing 'actor_name'"}

        actor = _find_actor_by_name(actor_name)
        if actor is None:
            return {"success": False, "data": {}, "error": f"Actor not found: {actor_name}"}

        if not isinstance(actor, unreal.PostProcessVolume):
            return {"success": False, "data": {}, "error": f"Actor '{actor_name}' is not a PostProcessVolume"}

        # Update volume properties
        if "unbound" in params:
            actor.unbound = bool(params["unbound"])
        if "blend_weight" in params:
            actor.blend_weight = float(params["blend_weight"])
        if "blend_radius" in params:
            actor.blend_radius = float(params["blend_radius"])
        if "priority" in params:
            actor.priority = float(params["priority"])

        # Update post-process settings
        pp_settings = params.get("settings", {})
        applied = []
        if pp_settings:
            applied = _apply_post_process_settings(actor.settings, pp_settings)

        return {
            "success": True,
            "data": {
                "actor_name": actor_name,
                "settings_applied": applied,
                "settings_count": len(applied),
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


# ── Presets: common effect combinations ──

def handle_pp_preset(params: Dict[str, Any]) -> Dict[str, Any]:
    """Apply a named post-processing preset. Creates or modifies a PP volume.

    Args:
        params:
            - preset (str): Preset name (see list below)
            - name (str): Actor label for the PP volume (default: "PP_<preset>")
            - intensity (float): Scale all effect values 0-2 (default: 1.0)

    Presets:
        - horror: Desaturated, high contrast, vignette, grain, chromatic aberration
        - cyberpunk: Teal/orange color grade, bloom, chromatic aberration, high saturation
        - noir: Full desaturation, high contrast, vignette, film grain
        - dream: Bloom glow, soft focus DOF, warm tint, low contrast
        - underwater: Blue-green tint, blur, low saturation, vignette
        - surveillance: Grain, desaturated, vignette, scanline feel
        - vhs_glitch: Heavy chromatic aberration, grain, color shift, bloom
        - damage: Red tint, high vignette, grain, chromatic aberration
        - drunk: Strong DOF blur, chromatic aberration, slight tilt, bloom
        - frozen: Blue tint, high contrast, desaturated warm colors
        - nuclear: Green tint, extreme bloom, grain, overexposure
        - sepia: Warm sepia tone, slight vignette, film grain
        - cinematic: Letterbox-feel vignette, film curve, subtle color grade
        - neon: Extreme bloom, high saturation, chromatic aberration
        - thermal: False-color thermal camera look via extreme color grading
    """
    try:
        preset_name = params.get("preset", "").lower()
        if not preset_name:
            return {"success": False, "data": {}, "error": "Missing 'preset'"}

        intensity = float(params.get("intensity", 1.0))
        actor_name = params.get("name", f"PP_{preset_name}")

        def scale(v: float) -> float:
            return v * intensity

        presets = {
            "horror": {
                "color_saturation": {"r": 0.3, "g": 0.3, "b": 0.35, "a": 1.0},
                "color_contrast": {"r": 1.4, "g": 1.4, "b": 1.5, "a": 1.0},
                "color_gamma": {"r": 0.85, "g": 0.85, "b": 0.9, "a": 1.0},
                "vignette_intensity": scale(0.8),
                "grain_intensity": scale(0.4),
                "grain_jitter": scale(0.5),
                "scene_fringe_intensity": scale(3.0),
                "bloom_intensity": scale(0.3),
                "auto_exposure_bias": -0.5 * intensity,
            },
            "cyberpunk": {
                "color_saturation": {"r": 1.3, "g": 1.1, "b": 1.4, "a": 1.0},
                "color_gain": {"r": 0.9, "g": 1.1, "b": 1.3, "a": 1.0},
                "color_gain_shadows": {"r": 0.2, "g": 0.8, "b": 1.0, "a": 1.0},
                "color_gain_highlights": {"r": 1.3, "g": 0.7, "b": 0.4, "a": 1.0},
                "bloom_intensity": scale(1.5),
                "bloom_threshold": 0.5,
                "scene_fringe_intensity": scale(2.0),
                "vignette_intensity": scale(0.3),
                "auto_exposure_bias": 0.5 * intensity,
            },
            "noir": {
                "color_saturation": {"r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0},
                "color_contrast": {"r": 1.6, "g": 1.6, "b": 1.6, "a": 1.0},
                "vignette_intensity": scale(0.9),
                "grain_intensity": scale(0.3),
                "grain_jitter": scale(0.3),
                "film_slope": 0.9,
                "film_toe": 0.6,
                "film_shoulder": 0.2,
                "auto_exposure_bias": -0.3 * intensity,
            },
            "dream": {
                "bloom_intensity": scale(3.0),
                "bloom_threshold": 0.1,
                "bloom_size_scale": 8.0,
                "color_saturation": {"r": 0.8, "g": 0.85, "b": 0.7, "a": 1.0},
                "color_contrast": {"r": 0.7, "g": 0.7, "b": 0.7, "a": 1.0},
                "color_gain": {"r": 1.1, "g": 1.05, "b": 0.9, "a": 1.0},
                "depth_of_field_focal_distance": 300.0,
                "depth_of_field_depth_blur_amount": scale(1.0),
                "depth_of_field_depth_blur_radius": scale(5.0),
                "scene_color_tint": {"r": 1.0, "g": 0.95, "b": 0.85, "a": 1.0},
                "vignette_intensity": scale(0.4),
            },
            "underwater": {
                "color_saturation": {"r": 0.4, "g": 0.8, "b": 0.9, "a": 1.0},
                "color_gain": {"r": 0.3, "g": 0.7, "b": 1.0, "a": 1.0},
                "color_gamma": {"r": 1.2, "g": 0.9, "b": 0.8, "a": 1.0},
                "vignette_intensity": scale(0.6),
                "depth_of_field_focal_distance": 200.0,
                "depth_of_field_depth_blur_amount": scale(0.5),
                "scene_fringe_intensity": scale(1.0),
                "bloom_intensity": scale(0.8),
                "scene_color_tint": {"r": 0.5, "g": 0.8, "b": 1.0, "a": 1.0},
            },
            "surveillance": {
                "color_saturation": {"r": 0.15, "g": 0.2, "b": 0.15, "a": 1.0},
                "color_contrast": {"r": 1.3, "g": 1.3, "b": 1.3, "a": 1.0},
                "grain_intensity": scale(0.6),
                "grain_jitter": scale(0.8),
                "vignette_intensity": scale(0.7),
                "scene_fringe_intensity": scale(1.5),
                "auto_exposure_bias": -0.2 * intensity,
                "bloom_intensity": scale(0.2),
            },
            "vhs_glitch": {
                "scene_fringe_intensity": scale(8.0),
                "grain_intensity": scale(0.7),
                "grain_jitter": scale(1.0),
                "color_saturation": {"r": 0.6, "g": 0.7, "b": 1.2, "a": 1.0},
                "color_offset": {"r": 0.02, "g": -0.01, "b": 0.01, "a": 0.0},
                "bloom_intensity": scale(1.2),
                "bloom_threshold": 0.3,
                "vignette_intensity": scale(0.5),
                "color_contrast": {"r": 1.2, "g": 1.1, "b": 0.9, "a": 1.0},
            },
            "damage": {
                "scene_color_tint": {"r": 1.0, "g": 0.2, "b": 0.1, "a": 1.0},
                "vignette_intensity": scale(1.2),
                "grain_intensity": scale(0.5),
                "scene_fringe_intensity": scale(5.0),
                "color_saturation": {"r": 1.5, "g": 0.3, "b": 0.3, "a": 1.0},
                "bloom_intensity": scale(0.5),
                "auto_exposure_bias": 0.3 * intensity,
            },
            "drunk": {
                "depth_of_field_focal_distance": 100.0,
                "depth_of_field_depth_blur_amount": scale(2.0),
                "depth_of_field_depth_blur_radius": scale(10.0),
                "scene_fringe_intensity": scale(5.0),
                "bloom_intensity": scale(2.0),
                "bloom_threshold": 0.3,
                "color_saturation": {"r": 1.1, "g": 0.9, "b": 1.1, "a": 1.0},
                "motion_blur_amount": scale(1.5),
                "motion_blur_max": scale(5.0),
                "vignette_intensity": scale(0.5),
            },
            "frozen": {
                "color_gain": {"r": 0.7, "g": 0.85, "b": 1.3, "a": 1.0},
                "color_saturation": {"r": 0.4, "g": 0.6, "b": 1.0, "a": 1.0},
                "color_contrast": {"r": 1.4, "g": 1.3, "b": 1.2, "a": 1.0},
                "white_temp": 4000.0,
                "bloom_intensity": scale(0.6),
                "vignette_intensity": scale(0.3),
                "scene_color_tint": {"r": 0.8, "g": 0.9, "b": 1.0, "a": 1.0},
            },
            "nuclear": {
                "color_gain": {"r": 0.5, "g": 1.5, "b": 0.3, "a": 1.0},
                "bloom_intensity": scale(5.0),
                "bloom_threshold": 0.05,
                "grain_intensity": scale(0.6),
                "grain_jitter": scale(0.8),
                "auto_exposure_bias": 2.0 * intensity,
                "scene_fringe_intensity": scale(3.0),
                "vignette_intensity": scale(0.4),
                "color_saturation": {"r": 0.5, "g": 1.2, "b": 0.3, "a": 1.0},
            },
            "sepia": {
                "color_saturation": {"r": 0.2, "g": 0.2, "b": 0.0, "a": 1.0},
                "color_gain": {"r": 1.2, "g": 1.0, "b": 0.7, "a": 1.0},
                "color_gamma": {"r": 0.9, "g": 0.95, "b": 1.1, "a": 1.0},
                "vignette_intensity": scale(0.5),
                "grain_intensity": scale(0.25),
                "film_slope": 0.85,
                "film_toe": 0.55,
            },
            "cinematic": {
                "vignette_intensity": scale(0.6),
                "film_slope": 0.88,
                "film_toe": 0.55,
                "film_shoulder": 0.26,
                "film_black_clip": 0.0,
                "film_white_clip": 0.04,
                "color_contrast": {"r": 1.15, "g": 1.15, "b": 1.15, "a": 1.0},
                "color_gain_shadows": {"r": 0.85, "g": 0.9, "b": 1.0, "a": 1.0},
                "color_gain_highlights": {"r": 1.1, "g": 1.05, "b": 0.95, "a": 1.0},
                "bloom_intensity": scale(0.5),
                "auto_exposure_bias": 0.0,
            },
            "neon": {
                "bloom_intensity": scale(4.0),
                "bloom_threshold": 0.1,
                "bloom_size_scale": 6.0,
                "color_saturation": {"r": 2.0, "g": 2.0, "b": 2.0, "a": 1.0},
                "scene_fringe_intensity": scale(3.0),
                "color_contrast": {"r": 1.3, "g": 1.3, "b": 1.3, "a": 1.0},
                "vignette_intensity": scale(0.2),
                "auto_exposure_bias": 0.5 * intensity,
            },
            "thermal": {
                "color_saturation": {"r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0},
                "color_gain": {"r": 2.0, "g": 0.5, "b": 0.0, "a": 1.0},
                "color_gain_shadows": {"r": 0.0, "g": 0.0, "b": 2.0, "a": 1.0},
                "color_gain_highlights": {"r": 2.0, "g": 1.5, "b": 0.0, "a": 1.0},
                "color_contrast": {"r": 2.0, "g": 2.0, "b": 2.0, "a": 1.0},
                "bloom_intensity": scale(0.3),
                "vignette_intensity": scale(0.2),
            },
        }

        if preset_name not in presets:
            return {
                "success": False,
                "data": {},
                "error": f"Unknown preset: '{preset_name}'. Available: {sorted(presets.keys())}",
            }

        # Spawn or modify existing volume
        existing = _find_actor_by_name(actor_name)
        if existing:
            return handle_pp_volume_modify({
                "actor_name": actor_name,
                "unbound": True,
                "blend_weight": 1.0,
                "settings": presets[preset_name],
            })
        else:
            return handle_pp_volume_spawn({
                "name": actor_name,
                "unbound": True,
                "blend_weight": 1.0,
                "settings": presets[preset_name],
            })

    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


# ── Camera Shake ──

def handle_camera_shake_spawn(params: Dict[str, Any]) -> Dict[str, Any]:
    """Spawn a CameraShakeSourceActor that emits camera shake in a radius.

    This places a persistent shake source in the level. The shake plays
    during PIE when a player camera is within the inner/outer radius.

    Args:
        params:
            - name (str): Actor label (default: "CameraShake_Source")
            - location (dict): {x, y, z} world position
            - inner_radius (float): Full-intensity shake radius (default: 500)
            - outer_radius (float): Fade-out radius (default: 1500)
            - shake_class (str): Content path to a CameraShake asset (optional)

    Note: UE4.27 camera shake requires a CameraShake Blueprint to define the
    actual oscillation parameters. This tool spawns the source actor that
    triggers it. For editor-time preview, use console command: PlayCameraShake.
    """
    try:
        import unreal

        name = params.get("name", "CameraShake_Source")
        loc = params.get("location", {"x": 0, "y": 0, "z": 0})
        spawn_loc = unreal.Vector(float(loc.get("x", 0)), float(loc.get("y", 0)), float(loc.get("z", 0)))

        # Try spawning CameraShakeSourceActor (UE4.26+)
        actor = None
        try:
            actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
                unreal.CameraShakeSourceActor, spawn_loc
            )
        except (AttributeError, Exception):
            pass

        if not actor:
            # Fallback: spawn generic actor with a note about manual setup
            return {
                "success": False,
                "data": {},
                "error": "CameraShakeSourceActor not available via Python in UE4.27. "
                         "Use python_proxy with console command 'PlayCameraShake <ShakeClass>' "
                         "during PIE instead, or create a CameraShake blueprint manually.",
            }

        actor.set_actor_label(name)

        # Configure shake source component
        source_comp = actor.get_component_by_class(unreal.CameraShakeSourceComponent)
        if not source_comp:
            return {"success": False, "data": {}, "error": "CameraShakeSourceActor spawned but has no CameraShakeSourceComponent"}

        inner_r = float(params.get("inner_radius", 500))
        outer_r = float(params.get("outer_radius", 1500))
        source_comp.set_editor_property("inner_attenuation_radius", inner_r)
        source_comp.set_editor_property("outer_attenuation_radius", outer_r)
        source_comp.set_editor_property("auto_start", True)

        # Assign the shake class (required for the source to do anything)
        shake_class_path = params.get("shake_class", "")
        shake_assigned = False
        if shake_class_path:
            class_name = shake_class_path.split("/")[-1]
            shake_cls = unreal.load_class(None, f"{shake_class_path}.{class_name}_C")
            if shake_cls:
                source_comp.set_editor_property("camera_shake", shake_cls)
                shake_assigned = True
            else:
                return {
                    "success": False,
                    "data": {},
                    "error": f"Failed to load CameraShake class at '{shake_class_path}'. "
                             f"Create it first with camera_shake_blueprint.",
                }

        return {
            "success": True,
            "data": {
                "actor_name": name,
                "location": loc,
                "inner_radius": inner_r,
                "outer_radius": outer_r,
                "auto_start": True,
                "shake_class": shake_class_path if shake_assigned else None,
                "shake_assigned": shake_assigned,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_camera_shake_play(params: Dict[str, Any]) -> Dict[str, Any]:
    """Play a camera shake on the PlayerCameraManager during PIE.

    Uses the correct UE4.27 path: PIE World -> PlayerController ->
    PlayerCameraManager -> play_camera_shake(). This is the only reliable
    way to trigger camera shake. Console commands and editor world calls
    do not work.

    Args:
        params:
            - shake_class (str): Content path to CameraShake BP (e.g. "/Game/CameraShakes/CS_Explosion")
            - scale (float): Shake intensity multiplier (default: 1.0)
            - play_space (str): "CameraLocal" or "World" (default: "CameraLocal")
    """
    try:
        import unreal

        shake_class_path = params.get("shake_class", "")
        scale = float(params.get("scale", 1.0))
        play_space = params.get("play_space", "CameraLocal")

        if not shake_class_path:
            return {"success": False, "data": {}, "error": "Missing 'shake_class'"}

        if scale <= 0:
            return {"success": False, "data": {}, "error": f"Scale must be > 0, got {scale}"}

        # Step 1: Get PIE world (not editor world)
        pie_worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not pie_worlds:
            return {
                "success": False,
                "data": {},
                "error": "No PIE session running. Start PIE first (gameplay_pie_start or press Play).",
            }

        pie_world = pie_worlds[0]

        # Step 2: Get PlayerController from PIE world
        pc = unreal.GameplayStatics.get_player_controller(pie_world, 0)
        if not pc:
            return {
                "success": False,
                "data": {},
                "error": "No PlayerController in PIE world. Player may not have spawned yet.",
            }

        # Step 3: Get PlayerCameraManager
        pcm = pc.player_camera_manager
        if not pcm:
            return {
                "success": False,
                "data": {},
                "error": "PlayerCameraManager is null. Controller may not be fully initialized.",
            }

        # Step 4: Load the shake class
        if "/" in shake_class_path:
            class_name = shake_class_path.split("/")[-1]
            full_class_path = f"{shake_class_path}.{class_name}_C"
        else:
            full_class_path = shake_class_path

        shake_cls = unreal.load_class(None, full_class_path)
        if not shake_cls:
            return {
                "success": False,
                "data": {},
                "error": f"Failed to load CameraShake class at '{full_class_path}'. "
                         f"Check that the asset exists and is compiled.",
            }

        # Step 5: Fire the shake
        pcm.play_camera_shake(shake_cls, scale)

        return {
            "success": True,
            "data": {
                "shake_class": shake_class_path,
                "scale": scale,
                "play_space": play_space,
                "player_controller": pc.get_name(),
                "camera_manager": pcm.get_name(),
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_camera_shake_blueprint(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a CameraShake Blueprint asset with oscillation parameters.

    This creates the actual shake definition (not the source actor).
    The resulting Blueprint can be referenced by camera shake sources
    or triggered via PlayCameraShake.

    Args:
        params:
            - name (str): Blueprint asset name (e.g. "CS_Explosion")
            - content_path (str): Content directory (default: "/Game/CameraShakes")
            - duration (float): Shake duration in seconds (default: 0.5, -1 for infinite)
            - blend_in_time (float): Fade-in time (default: 0.1)
            - blend_out_time (float): Fade-out time (default: 0.2)

            Rotational oscillation:
            - rot_pitch_amp (float): Pitch oscillation amplitude in degrees (default: 0)
            - rot_pitch_freq (float): Pitch oscillation frequency (default: 0)
            - rot_yaw_amp (float): Yaw oscillation amplitude (default: 0)
            - rot_yaw_freq (float): Yaw oscillation frequency (default: 0)
            - rot_roll_amp (float): Roll oscillation amplitude (default: 0)
            - rot_roll_freq (float): Roll oscillation frequency (default: 0)

            Locational oscillation:
            - loc_x_amp (float): X location oscillation amplitude (default: 0)
            - loc_x_freq (float): X location oscillation frequency (default: 0)
            - loc_y_amp (float): Y location oscillation amplitude (default: 0)
            - loc_y_freq (float): Y location oscillation frequency (default: 0)
            - loc_z_amp (float): Z location oscillation amplitude (default: 0)
            - loc_z_freq (float): Z location oscillation frequency (default: 0)

            FOV oscillation:
            - fov_amp (float): FOV oscillation amplitude (default: 0)
            - fov_freq (float): FOV oscillation frequency (default: 0)

            - shake_scale (float): Overall scale multiplier (default: 1.0)

    Presets (use preset instead of individual values):
            - preset (str): "explosion", "hit", "earthquake", "gunfire",
                           "footstep", "ambient", "electric_shock", "glitch_shake"
    """
    try:
        import unreal

        name = params.get("name", "CS_CameraShake")
        content_path = params.get("content_path", "/Game/CameraShakes")

        # Check for preset
        preset_name = params.get("preset", "").lower()
        if preset_name:
            shake_presets = {
                "explosion": {
                    "duration": 0.8, "blend_in_time": 0.05, "blend_out_time": 0.4,
                    "rot_pitch_amp": 5.0, "rot_pitch_freq": 25.0,
                    "rot_yaw_amp": 3.0, "rot_yaw_freq": 20.0,
                    "rot_roll_amp": 2.0, "rot_roll_freq": 30.0,
                    "loc_x_amp": 10.0, "loc_x_freq": 25.0,
                    "loc_y_amp": 8.0, "loc_y_freq": 22.0,
                    "loc_z_amp": 15.0, "loc_z_freq": 30.0,
                    "fov_amp": 3.0, "fov_freq": 15.0,
                },
                "hit": {
                    "duration": 0.3, "blend_in_time": 0.02, "blend_out_time": 0.15,
                    "rot_pitch_amp": 2.0, "rot_pitch_freq": 40.0,
                    "rot_yaw_amp": 1.5, "rot_yaw_freq": 35.0,
                    "loc_z_amp": 5.0, "loc_z_freq": 40.0,
                    "fov_amp": 1.0, "fov_freq": 20.0,
                },
                "earthquake": {
                    "duration": 5.0, "blend_in_time": 1.0, "blend_out_time": 2.0,
                    "rot_pitch_amp": 1.0, "rot_pitch_freq": 8.0,
                    "rot_roll_amp": 0.5, "rot_roll_freq": 6.0,
                    "loc_x_amp": 3.0, "loc_x_freq": 10.0,
                    "loc_y_amp": 3.0, "loc_y_freq": 8.0,
                    "loc_z_amp": 5.0, "loc_z_freq": 12.0,
                },
                "gunfire": {
                    "duration": 0.15, "blend_in_time": 0.01, "blend_out_time": 0.08,
                    "rot_pitch_amp": 0.8, "rot_pitch_freq": 50.0,
                    "rot_yaw_amp": 0.3, "rot_yaw_freq": 45.0,
                    "loc_z_amp": 1.0, "loc_z_freq": 50.0,
                },
                "footstep": {
                    "duration": 0.2, "blend_in_time": 0.02, "blend_out_time": 0.1,
                    "loc_z_amp": 0.5, "loc_z_freq": 15.0,
                    "rot_pitch_amp": 0.15, "rot_pitch_freq": 12.0,
                },
                "ambient": {
                    "duration": -1.0, "blend_in_time": 2.0, "blend_out_time": 2.0,
                    "rot_pitch_amp": 0.1, "rot_pitch_freq": 1.0,
                    "rot_yaw_amp": 0.1, "rot_yaw_freq": 0.8,
                    "loc_z_amp": 0.3, "loc_z_freq": 1.5,
                },
                "electric_shock": {
                    "duration": 0.4, "blend_in_time": 0.01, "blend_out_time": 0.1,
                    "rot_pitch_amp": 8.0, "rot_pitch_freq": 60.0,
                    "rot_yaw_amp": 6.0, "rot_yaw_freq": 55.0,
                    "rot_roll_amp": 4.0, "rot_roll_freq": 65.0,
                    "loc_x_amp": 3.0, "loc_x_freq": 60.0,
                    "loc_y_amp": 3.0, "loc_y_freq": 58.0,
                    "loc_z_amp": 2.0, "loc_z_freq": 62.0,
                    "fov_amp": 5.0, "fov_freq": 40.0,
                },
                "glitch_shake": {
                    "duration": 0.6, "blend_in_time": 0.01, "blend_out_time": 0.05,
                    "rot_pitch_amp": 3.0, "rot_pitch_freq": 80.0,
                    "rot_yaw_amp": 5.0, "rot_yaw_freq": 70.0,
                    "rot_roll_amp": 8.0, "rot_roll_freq": 90.0,
                    "loc_x_amp": 8.0, "loc_x_freq": 85.0,
                    "loc_y_amp": 6.0, "loc_y_freq": 75.0,
                    "loc_z_amp": 4.0, "loc_z_freq": 80.0,
                    "fov_amp": 8.0, "fov_freq": 50.0,
                },
            }
            if preset_name not in shake_presets:
                return {
                    "success": False,
                    "data": {},
                    "error": f"Unknown shake preset: '{preset_name}'. Available: {sorted(shake_presets.keys())}",
                }
            # Merge preset with any explicit overrides
            preset_vals = shake_presets[preset_name]
            for k, v in preset_vals.items():
                if k not in params:
                    params[k] = v

        # Build the Python code to create the CameraShake BP
        duration = float(params.get("duration", 0.5))
        blend_in = float(params.get("blend_in_time", 0.1))
        blend_out = float(params.get("blend_out_time", 0.2))
        shake_scale = float(params.get("shake_scale", 1.0))

        # Oscillation parameters
        rot_pitch_amp = float(params.get("rot_pitch_amp", 0.0))
        rot_pitch_freq = float(params.get("rot_pitch_freq", 0.0))
        rot_yaw_amp = float(params.get("rot_yaw_amp", 0.0))
        rot_yaw_freq = float(params.get("rot_yaw_freq", 0.0))
        rot_roll_amp = float(params.get("rot_roll_amp", 0.0))
        rot_roll_freq = float(params.get("rot_roll_freq", 0.0))
        loc_x_amp = float(params.get("loc_x_amp", 0.0))
        loc_x_freq = float(params.get("loc_x_freq", 0.0))
        loc_y_amp = float(params.get("loc_y_amp", 0.0))
        loc_y_freq = float(params.get("loc_y_freq", 0.0))
        loc_z_amp = float(params.get("loc_z_amp", 0.0))
        loc_z_freq = float(params.get("loc_z_freq", 0.0))
        fov_amp = float(params.get("fov_amp", 0.0))
        fov_freq = float(params.get("fov_freq", 0.0))

        # Create the CameraShake blueprint via the asset factory system
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.MatineeCameraShake)

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        full_path = f"{content_path}/{name}"

        # Check if asset already exists -- delete and recreate
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            unreal.EditorAssetLibrary.delete_asset(full_path)

        bp = asset_tools.create_asset(name, content_path, unreal.Blueprint, factory)
        if not bp:
            return {"success": False, "data": {}, "error": f"Failed to create CameraShake blueprint at {full_path}"}

        # UE4.27: Access CDO via load_class on the _C generated class path
        gen_class = unreal.load_class(None, f"{full_path}.{name}_C")
        if not gen_class:
            return {"success": False, "data": {}, "error": f"Failed to load generated class for {full_path}"}

        cdo = unreal.get_default_object(gen_class)
        if cdo:
            cdo.set_editor_property("oscillation_duration", duration)
            cdo.set_editor_property("oscillation_blend_in_time", blend_in)
            cdo.set_editor_property("oscillation_blend_out_time", blend_out)

            # Rotation oscillation -- access struct fields directly
            rot_osc = cdo.get_editor_property("rot_oscillation")
            if rot_osc:
                rot_osc.pitch.amplitude = rot_pitch_amp
                rot_osc.pitch.frequency = rot_pitch_freq
                rot_osc.yaw.amplitude = rot_yaw_amp
                rot_osc.yaw.frequency = rot_yaw_freq
                rot_osc.roll.amplitude = rot_roll_amp
                rot_osc.roll.frequency = rot_roll_freq

            # Location oscillation
            loc_osc = cdo.get_editor_property("loc_oscillation")
            if loc_osc:
                loc_osc.x.amplitude = loc_x_amp
                loc_osc.x.frequency = loc_x_freq
                loc_osc.y.amplitude = loc_y_amp
                loc_osc.y.frequency = loc_y_freq
                loc_osc.z.amplitude = loc_z_amp
                loc_osc.z.frequency = loc_z_freq

            # FOV oscillation
            fov_osc = cdo.get_editor_property("fov_oscillation")
            if fov_osc:
                fov_osc.amplitude = fov_amp
                fov_osc.frequency = fov_freq

        # Save the asset
        unreal.EditorAssetLibrary.save_asset(full_path)

        return {
            "success": True,
            "data": {
                "asset_path": full_path,
                "name": name,
                "duration": duration,
                "preset": preset_name if preset_name else None,
                "oscillation": {
                    "rot_pitch": {"amp": rot_pitch_amp, "freq": rot_pitch_freq},
                    "rot_yaw": {"amp": rot_yaw_amp, "freq": rot_yaw_freq},
                    "rot_roll": {"amp": rot_roll_amp, "freq": rot_roll_freq},
                    "loc_x": {"amp": loc_x_amp, "freq": loc_x_freq},
                    "loc_y": {"amp": loc_y_amp, "freq": loc_y_freq},
                    "loc_z": {"amp": loc_z_amp, "freq": loc_z_freq},
                    "fov": {"amp": fov_amp, "freq": fov_freq},
                },
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


# ── ShakeTriggerActor (C++ runtime actor, spawned by Python) ──

def handle_camera_shake_trigger(params: Dict[str, Any]) -> Dict[str, Any]:
    """Spawn a ShakeTriggerActor that plays camera shake when the player overlaps.

    Uses the C++ AShakeTriggerActor class from the BlueprintGraphBuilder plugin.
    Python handles spawning and configuration. C++ handles runtime overlap
    detection and the PlayerController -> CameraManager -> StartCameraShake chain.

    Args:
        params:
            - name (str): Actor label (default: "ShakeTrigger")
            - location (dict): {x, y, z} world position
            - shake_class (str): Content path to CameraShake BP (e.g. "/Game/CS_Earthquake")
            - shake_scale (float): Intensity multiplier (default: 1.0)
            - box_extent (dict): {x, y, z} trigger box half-extents (default: 200,200,200)
    """
    try:
        import unreal

        name = params.get("name", "ShakeTrigger")
        loc = params.get("location", {"x": 0, "y": 0, "z": 0})
        spawn_loc = unreal.Vector(float(loc.get("x", 0)), float(loc.get("y", 0)), float(loc.get("z", 0)))
        shake_scale = float(params.get("shake_scale", 1.0))

        # Load the C++ ShakeTriggerActor class
        actor_class = unreal.load_class(None, "/Script/BlueprintGraphBuilder.ShakeTriggerActor")
        if not actor_class:
            return {
                "success": False,
                "data": {},
                "error": "ShakeTriggerActor class not found. Make sure BlueprintGraphBuilder plugin is loaded.",
            }

        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(actor_class, spawn_loc)
        if not actor:
            return {"success": False, "data": {}, "error": "Failed to spawn ShakeTriggerActor"}

        actor.set_actor_label(name)

        # Assign camera shake class
        shake_class_path = params.get("shake_class", "")
        if shake_class_path:
            class_name = shake_class_path.split("/")[-1]
            full_path = f"{shake_class_path}.{class_name}_C"
            shake_cls = unreal.load_class(None, full_path)
            if shake_cls:
                actor.set_editor_property("ShakeClass", shake_cls)
            else:
                return {
                    "success": False,
                    "data": {},
                    "error": f"Failed to load CameraShake class at '{full_path}'. "
                             f"Create it first with camera_shake_blueprint.",
                }

        # Set shake scale
        actor.set_editor_property("ShakeScale", shake_scale)

        # Configure box extent
        box_extent = params.get("box_extent", None)
        if box_extent:
            trigger = actor.get_editor_property("Trigger")
            if trigger:
                extent = unreal.Vector(
                    float(box_extent.get("x", 200)),
                    float(box_extent.get("y", 200)),
                    float(box_extent.get("z", 200)),
                )
                trigger.set_box_extent(extent)

        return {
            "success": True,
            "data": {
                "actor_name": name,
                "location": loc,
                "shake_class": shake_class_path,
                "shake_scale": shake_scale,
                "note": "C++ ShakeTriggerActor spawned. Overlap triggers camera shake during PIE.",
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


# ── Console-based effects (immediate, editor-time) ──

def handle_console_effect(params: Dict[str, Any]) -> Dict[str, Any]:
    """Execute visual effect console commands. Immediate, editor-time.

    Args:
        params:
            - command (str): One of the supported effect commands
            - value (str|float): Command value (if applicable)

    Supported commands:
        - freeze_rendering: Freeze/unfreeze rendering (toggle)
        - show_fps: Show/hide FPS counter
        - screen_percentage <value>: Set render resolution scale (10-200)
        - temporal_aa <0|1>: Enable/disable temporal AA
        - fxaa <0|1>: Enable/disable FXAA
        - motion_blur <0|1>: Enable/disable motion blur
        - bloom <0|1>: Enable/disable bloom
        - eye_adaptation <0|1>: Enable/disable auto exposure
        - tonemapper <0|1>: Enable/disable tonemapper
        - ssr <0|1>: Enable/disable screen space reflections
        - ao <0|1>: Enable/disable ambient occlusion
        - dof <0|1>: Enable/disable depth of field
        - aa_quality <0-4>: Anti-aliasing quality level
        - shadow_quality <0-4>: Shadow quality level
        - view_distance_quality <0-4>: View distance quality
        - post_process_quality <0-4>: Post process quality level
    """
    try:
        import unreal

        command = params.get("command", "")
        value = params.get("value", "")

        if not command:
            return {"success": False, "data": {}, "error": "Missing 'command'"}

        console_map = {
            "freeze_rendering": "FreezeRendering",
            "show_fps": "stat fps",
            "screen_percentage": f"r.ScreenPercentage {value}",
            "temporal_aa": f"r.TemporalAACurrentFrameWeight {value}",
            "fxaa": f"r.DefaultFeature.AntiAliasing {value}",
            "motion_blur": f"ShowFlag.MotionBlur {value}",
            "bloom": f"ShowFlag.Bloom {value}",
            "eye_adaptation": f"ShowFlag.EyeAdaptation {value}",
            "tonemapper": f"ShowFlag.Tonemapper {value}",
            "ssr": f"ShowFlag.ScreenSpaceReflections {value}",
            "ao": f"ShowFlag.AmbientOcclusion {value}",
            "dof": f"ShowFlag.DepthOfField {value}",
            "aa_quality": f"sg.AntiAliasingQuality {value}",
            "shadow_quality": f"sg.ShadowQuality {value}",
            "view_distance_quality": f"sg.ViewDistanceQuality {value}",
            "post_process_quality": f"sg.PostProcessQuality {value}",
        }

        if command not in console_map:
            return {
                "success": False,
                "data": {},
                "error": f"Unknown effect command: '{command}'. Available: {sorted(console_map.keys())}",
            }

        console_cmd = console_map[command]
        world = unreal.EditorLevelLibrary.get_editor_world()
        unreal.SystemLibrary.execute_console_command(world, console_cmd)

        return {
            "success": True,
            "data": {
                "command": command,
                "console_command": console_cmd,
                "value": str(value),
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}
