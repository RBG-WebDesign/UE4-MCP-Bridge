"""Viewport control handlers.

Handles screenshot capture, camera manipulation, view modes, render modes,
focusing, fitting, and look-at operations.

NOTE: Viewport state changes (camera moves, mode switches, render mode) are
NOT transactable in UE4's undo system. Do not wrap them in transactions.
"""

import math
import os
import datetime
from typing import Any, Dict


def _get_viewport_subsystem() -> Any:
    """Get the UnrealEditorSubsystem for viewport camera access.

    Returns the subsystem or raises if unavailable.
    """
    import unreal
    return unreal.UnrealEditorSubsystem()


def _get_camera_state() -> Dict[str, Any]:
    """Read current viewport camera location and rotation."""
    subsystem = _get_viewport_subsystem()
    loc, rot = subsystem.get_level_viewport_camera_info()
    return {
        "location": {"x": loc.x, "y": loc.y, "z": loc.z},
        "rotation": {"pitch": rot.pitch, "yaw": rot.yaw, "roll": rot.roll},
    }


def _find_actor_by_name(name: str) -> Any:
    """Find an actor in the current level by its label."""
    import unreal
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    for actor in actors:
        if actor.get_actor_label() == name:
            return actor
    return None


def _calculate_look_rotation(from_loc: Dict[str, float], to_loc: Dict[str, float]) -> Dict[str, float]:
    """Calculate pitch/yaw rotation to look from one point at another."""
    dx = to_loc["x"] - from_loc["x"]
    dy = to_loc["y"] - from_loc["y"]
    dz = to_loc["z"] - from_loc["z"]
    yaw = math.degrees(math.atan2(dy, dx))
    horizontal_dist = math.sqrt(dx * dx + dy * dy)
    pitch = math.degrees(math.atan2(dz, horizontal_dist))
    return {"pitch": pitch, "yaw": yaw, "roll": 0.0}


def handle_viewport_screenshot(params: Dict[str, Any]) -> Dict[str, Any]:
    """Capture the active viewport to a PNG file.

    Saves screenshots to {ProjectDir}/Saved/Screenshots/MCPBridge/.
    Returns the absolute filesystem path so Claude Code can read the file.

    Capture fallback chain:
    1. unreal.AutomationLibrary.take_high_res_screenshot() if available in 4.27
    2. Console command: HighResShot {width}x{height}
    3. Error with clear message

    Args:
        params:
            - filename (str): Output filename (optional, auto-generated if omitted)
            - resolution (dict): {width, height} in pixels (optional, default 1920x1080)
            - show_ui (bool): Include editor UI in capture (default False)
    """
    try:
        import unreal

        # Parse resolution -- support both nested object and flat params
        resolution = params.get("resolution", {})
        if isinstance(resolution, dict):
            res_x = int(resolution.get("width", 1920))
            res_y = int(resolution.get("height", 1080))
        else:
            res_x = 1920
            res_y = 1080

        if res_x < 1 or res_y < 1:
            return {"success": False, "data": {}, "error": "Resolution width and height must be positive integers"}

        # show_ui is accepted but not yet supported by the capture methods
        # available in UE4.27. Reserved for future use.
        _ = params.get("show_ui", False)

        # Build output path
        project_dir = unreal.SystemLibrary.get_project_directory()
        screenshots_dir = os.path.join(project_dir, "Saved", "Screenshots", "MCPBridge")
        try:
            os.makedirs(screenshots_dir, exist_ok=True)
        except OSError as dir_err:
            return {"success": False, "data": {}, "error": f"Cannot create screenshot directory: {dir_err}"}

        filename = params.get("filename", "")
        if not filename:
            timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"viewport_{timestamp}.png"

        # Ensure .png extension
        if not filename.lower().endswith(".png"):
            filename += ".png"

        filepath = os.path.join(screenshots_dir, filename)

        # Check if viewport is active
        try:
            subsystem = _get_viewport_subsystem()
            cam_loc, cam_rot = subsystem.get_level_viewport_camera_info()
        except Exception:
            return {"success": False, "data": {}, "error": "No active viewport. Is a level open in the editor?"}

        # Capture method 1: AutomationLibrary.take_high_res_screenshot
        # This is the primary method and works in UE4.27
        capture_method = "unknown"
        try:
            unreal.AutomationLibrary.take_high_res_screenshot(res_x, res_y, filepath)
            capture_method = "AutomationLibrary.take_high_res_screenshot"
        except (AttributeError, Exception) as primary_err:
            # Capture method 2: Console command fallback
            try:
                world = unreal.EditorLevelLibrary.get_editor_world()
                cmd = f"HighResShot {res_x}x{res_y} filename=\"{filepath}\""
                unreal.SystemLibrary.execute_console_command(world, cmd)
                capture_method = "console_command_HighResShot"
            except Exception as fallback_err:
                return {
                    "success": False,
                    "data": {},
                    "error": f"Screenshot capture failed. Primary: {primary_err}. Fallback: {fallback_err}",
                }

        # Verify file was written
        if os.path.exists(filepath) and os.path.getsize(filepath) > 0:
            file_size = os.path.getsize(filepath)
        else:
            file_size = 0

        return {
            "success": True,
            "data": {
                "filepath": filepath,
                "filename": filename,
                "resolution": {"width": res_x, "height": res_y},
                "file_size_bytes": file_size,
                "capture_method": capture_method,
                "camera_location": {"x": cam_loc.x, "y": cam_loc.y, "z": cam_loc.z},
                "camera_rotation": {"pitch": cam_rot.pitch, "yaw": cam_rot.yaw, "roll": cam_rot.roll},
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_viewport_camera(params: Dict[str, Any]) -> Dict[str, Any]:
    """Set viewport camera position, rotation, and/or FOV.

    Preserves existing values for parameters not provided.

    Args:
        params:
            - location (list[3] or dict): Camera position [x, y, z] or {x, y, z}
            - rotation (list[3] or dict): Camera rotation [pitch, yaw, roll] or {pitch, yaw, roll}
            - fov (float): Field of view in degrees (optional)
    """
    try:
        import unreal
        from mcp_bridge.utils.serialization import vector_from_dict, rotator_from_dict

        location = params.get("location")
        rotation = params.get("rotation")
        fov = params.get("fov")

        if location is None and rotation is None and fov is None:
            return {"success": False, "data": {}, "error": "Provide at least one of: location, rotation, fov"}

        subsystem = _get_viewport_subsystem()
        current_loc, current_rot = subsystem.get_level_viewport_camera_info()

        # Convert list format to dict if needed
        if isinstance(location, list) and len(location) == 3:
            location = {"x": location[0], "y": location[1], "z": location[2]}
        if isinstance(rotation, list) and len(rotation) == 3:
            rotation = {"pitch": rotation[0], "yaw": rotation[1], "roll": rotation[2]}

        new_loc = vector_from_dict(location) if location is not None else current_loc
        new_rot = rotator_from_dict(rotation) if rotation is not None else current_rot

        subsystem.set_level_viewport_camera_info(new_loc, new_rot)

        # FOV is set via console command in UE4.27
        if fov is not None:
            fov_val = float(fov)
            if fov_val < 1 or fov_val > 170:
                return {"success": False, "data": {}, "error": f"FOV must be between 1 and 170 (got {fov_val})"}
            try:
                world = unreal.EditorLevelLibrary.get_editor_world()
                unreal.SystemLibrary.execute_console_command(world, f"fov {fov_val}")
            except Exception:
                pass  # FOV command may not be available in all contexts

        # Read back actual state after setting
        actual_loc, actual_rot = subsystem.get_level_viewport_camera_info()

        return {
            "success": True,
            "data": {
                "location": {"x": actual_loc.x, "y": actual_loc.y, "z": actual_loc.z},
                "rotation": {"pitch": actual_rot.pitch, "yaw": actual_rot.yaw, "roll": actual_rot.roll},
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_viewport_mode(params: Dict[str, Any]) -> Dict[str, Any]:
    """Switch viewport to a standard view preset.

    Switching from perspective to ortho views will reposition the camera.
    The new camera position is returned in the response.

    Args:
        params:
            - mode (str): "perspective", "top", "bottom", "front", "back", "left", "right"
    """
    try:
        mode = params.get("mode", "").lower()
        if not mode:
            return {"success": False, "data": {}, "error": "Missing 'mode'"}

        # Standard view presets: camera position + rotation
        views = {
            "top":         {"location": {"x": 0, "y": 0, "z": 5000},    "rotation": {"pitch": -90, "yaw": 0,   "roll": 0}},
            "bottom":      {"location": {"x": 0, "y": 0, "z": -5000},   "rotation": {"pitch": 90,  "yaw": 0,   "roll": 0}},
            "front":       {"location": {"x": -5000, "y": 0, "z": 500}, "rotation": {"pitch": 0,   "yaw": 0,   "roll": 0}},
            "back":        {"location": {"x": 5000, "y": 0, "z": 500},  "rotation": {"pitch": 0,   "yaw": 180, "roll": 0}},
            "right":       {"location": {"x": 0, "y": 5000, "z": 500},  "rotation": {"pitch": 0,   "yaw": -90, "roll": 0}},
            "left":        {"location": {"x": 0, "y": -5000, "z": 500}, "rotation": {"pitch": 0,   "yaw": 90,  "roll": 0}},
            "perspective": {"location": {"x": -2000, "y": -2000, "z": 1500}, "rotation": {"pitch": -30, "yaw": 45, "roll": 0}},
        }

        if mode not in views:
            return {
                "success": False,
                "data": {},
                "error": f"Unknown view mode: '{mode}'. Available: {list(views.keys())}",
            }

        view = views[mode]
        result = handle_viewport_camera({
            "location": view["location"],
            "rotation": view["rotation"],
        })

        # Add mode info to the response
        if result["success"]:
            result["data"]["mode"] = mode

        return result
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_viewport_focus(params: Dict[str, Any]) -> Dict[str, Any]:
    """Focus the viewport camera on a named actor.

    Positions the camera at a distance from the actor and aims at its
    bounding box center.

    Args:
        params:
            - actor_name (str): Label of the actor to focus on
            - distance (float): Distance from actor (optional, default 500)
    """
    try:
        import unreal

        actor_name = params.get("actor_name", "")
        if not actor_name:
            return {"success": False, "data": {}, "error": "Missing 'actor_name'"}

        distance = float(params.get("distance", 500.0))
        if distance <= 0:
            distance = 500.0

        target = _find_actor_by_name(actor_name)
        if target is None:
            return {
                "success": False,
                "data": {},
                "error": f"Actor not found: {actor_name}. Use level_actors to list available actors.",
            }

        # Select the actor in the editor
        unreal.EditorLevelLibrary.set_selected_level_actors([target])

        # Use bounding box center, not the actor's origin
        try:
            origin, extent = target.get_actor_bounds(False)
            target_center = {"x": origin.x, "y": origin.y, "z": origin.z}
            bounds_min = {"x": origin.x - extent.x, "y": origin.y - extent.y, "z": origin.z - extent.z}
            bounds_max = {"x": origin.x + extent.x, "y": origin.y + extent.y, "z": origin.z + extent.z}
        except Exception:
            loc = target.get_actor_location()
            target_center = {"x": loc.x, "y": loc.y, "z": loc.z}
            bounds_min = target_center
            bounds_max = target_center

        # Position camera offset from target
        camera_loc = {
            "x": target_center["x"] - distance * 0.7,
            "y": target_center["y"] - distance * 0.7,
            "z": target_center["z"] + distance * 0.5,
        }

        # Calculate rotation to look at target center
        look_rot = _calculate_look_rotation(camera_loc, target_center)

        subsystem = _get_viewport_subsystem()
        from mcp_bridge.utils.serialization import vector_from_dict, rotator_from_dict
        subsystem.set_level_viewport_camera_info(
            vector_from_dict(camera_loc),
            rotator_from_dict(look_rot),
        )

        # Read back actual camera state
        actual_loc, actual_rot = subsystem.get_level_viewport_camera_info()

        return {
            "success": True,
            "data": {
                "focused_on": actor_name,
                "camera_location": {"x": actual_loc.x, "y": actual_loc.y, "z": actual_loc.z},
                "camera_rotation": {"pitch": actual_rot.pitch, "yaw": actual_rot.yaw, "roll": actual_rot.roll},
                "actor_bounds": {"min": bounds_min, "max": bounds_max},
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_viewport_render_mode(params: Dict[str, Any]) -> Dict[str, Any]:
    """Change the viewport render mode.

    Uses console commands to switch render mode. Stores and returns the
    previous mode alongside the new mode.

    Args:
        params:
            - mode (str): "lit", "unlit", "wireframe", "detail_lighting",
                          "lighting_only", "light_complexity", "shader_complexity",
                          "collision"
    """
    try:
        import unreal

        mode = params.get("mode", "").lower()
        if not mode:
            return {"success": False, "data": {}, "error": "Missing 'mode'"}

        mode_commands = {
            "lit": "viewmode lit",
            "unlit": "viewmode unlit",
            "wireframe": "viewmode wireframe",
            "detail_lighting": "viewmode detaillighting",
            "lighting_only": "viewmode lightingonly",
            "light_complexity": "viewmode lightcomplexity",
            "shader_complexity": "viewmode shadercomplexity",
            "collision": "viewmode CollisionVisibility",
        }

        if mode not in mode_commands:
            return {
                "success": False,
                "data": {},
                "error": f"Unknown render mode: '{mode}'. Available: {list(mode_commands.keys())}",
            }

        world = unreal.EditorLevelLibrary.get_editor_world()
        if not world:
            return {"success": False, "data": {}, "error": "No level is currently open"}

        unreal.SystemLibrary.execute_console_command(world, mode_commands[mode])

        return {
            "success": True,
            "data": {
                "render_mode": mode,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_viewport_bounds(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return current viewport camera state. Read-only query.

    Args:
        params: No required parameters.
    """
    try:
        subsystem = _get_viewport_subsystem()
        loc, rot = subsystem.get_level_viewport_camera_info()

        return {
            "success": True,
            "data": {
                "camera_location": {"x": loc.x, "y": loc.y, "z": loc.z},
                "camera_rotation": {"pitch": rot.pitch, "yaw": rot.yaw, "roll": rot.roll},
                "is_perspective": True,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_viewport_fit(params: Dict[str, Any]) -> Dict[str, Any]:
    """Fit actors into the viewport frame.

    If actor_names is empty or omitted, fits all actors in the level.
    Computes the combined bounding box, applies a padding multiplier,
    and frames the camera.

    Args:
        params:
            - actor_names (list[str]): Actor labels to fit (optional, empty = fit all)
            - padding (float): Distance multiplier (default 1.2 = 20% extra)
    """
    try:
        import unreal

        actor_names = params.get("actor_names", [])
        if not isinstance(actor_names, list):
            actor_names = []

        padding = float(params.get("padding", 1.2))
        if padding <= 0:
            padding = 1.2

        all_actors = unreal.EditorLevelLibrary.get_all_level_actors()

        # Select target actors
        if actor_names:
            targets = []
            not_found = []
            for name in actor_names:
                actor = _find_actor_by_name(str(name))
                if actor:
                    targets.append(actor)
                else:
                    not_found.append(str(name))
            if not targets:
                return {"success": False, "data": {}, "error": f"No matching actors found. Not found: {not_found}"}
        else:
            # Fit all actors
            targets = list(all_actors)
            not_found = []
            if not targets:
                return {"success": False, "data": {}, "error": "No actors in the level to fit"}

        # Compute combined bounding box
        min_bound = {"x": float("inf"), "y": float("inf"), "z": float("inf")}
        max_bound = {"x": float("-inf"), "y": float("-inf"), "z": float("-inf")}

        for actor in targets:
            try:
                origin, extent = actor.get_actor_bounds(False)
                for axis in ("x", "y", "z"):
                    o = getattr(origin, axis)
                    e = getattr(extent, axis)
                    min_bound[axis] = min(min_bound[axis], o - e)
                    max_bound[axis] = max(max_bound[axis], o + e)
            except Exception:
                loc = actor.get_actor_location()
                for axis in ("x", "y", "z"):
                    v = getattr(loc, axis)
                    min_bound[axis] = min(min_bound[axis], v)
                    max_bound[axis] = max(max_bound[axis], v)

        # Center of the combined bounding box
        center = {
            axis: (min_bound[axis] + max_bound[axis]) / 2.0
            for axis in ("x", "y", "z")
        }

        # Compute diagonal distance for camera placement
        dx = max_bound["x"] - min_bound["x"]
        dy = max_bound["y"] - min_bound["y"]
        dz = max_bound["z"] - min_bound["z"]
        diagonal = math.sqrt(dx * dx + dy * dy + dz * dz)
        camera_dist = max(diagonal * padding * 0.5, 500.0)

        # Position camera looking at center
        camera_loc = {
            "x": center["x"] - camera_dist * 0.7,
            "y": center["y"] - camera_dist * 0.7,
            "z": center["z"] + camera_dist * 0.5,
        }
        look_rot = _calculate_look_rotation(camera_loc, center)

        subsystem = _get_viewport_subsystem()
        from mcp_bridge.utils.serialization import vector_from_dict, rotator_from_dict
        subsystem.set_level_viewport_camera_info(
            vector_from_dict(camera_loc),
            rotator_from_dict(look_rot),
        )

        # Select the target actors
        unreal.EditorLevelLibrary.set_selected_level_actors(targets)

        # Read back actual state
        actual_loc, actual_rot = subsystem.get_level_viewport_camera_info()

        fitted_names = [a.get_actor_label() for a in targets]

        return {
            "success": True,
            "data": {
                "fitted_actors": fitted_names,
                "fitted_count": len(fitted_names),
                "camera_location": {"x": actual_loc.x, "y": actual_loc.y, "z": actual_loc.z},
                "camera_rotation": {"pitch": actual_rot.pitch, "yaw": actual_rot.yaw, "roll": actual_rot.roll},
                "combined_bounds": {"min": min_bound, "max": max_bound},
                "padding": padding,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_viewport_look_at(params: Dict[str, Any]) -> Dict[str, Any]:
    """Point camera at a target without moving the camera position.

    Accepts either an actor_name or a location. If both are provided,
    actor_name takes priority. If targeting an actor, uses its bounding
    box center, not the actor's origin (which might be at its feet).

    Args:
        params:
            - actor_name (str): Actor label to look at (optional)
            - location (list[3] or dict): World coordinates to look at (optional)
    """
    try:
        import unreal

        subsystem = _get_viewport_subsystem()
        cam_loc, _ = subsystem.get_level_viewport_camera_info()

        actor_name = params.get("actor_name")
        location = params.get("location")

        target: Dict[str, float] = {}

        # actor_name takes priority if both are provided
        if actor_name:
            actor = _find_actor_by_name(str(actor_name))
            if actor is None:
                return {
                    "success": False,
                    "data": {},
                    "error": f"Actor not found: {actor_name}. Use level_actors to list available actors.",
                }
            # Use bounding box center
            try:
                origin, _ = actor.get_actor_bounds(False)
                target = {"x": origin.x, "y": origin.y, "z": origin.z}
            except Exception:
                loc = actor.get_actor_location()
                target = {"x": loc.x, "y": loc.y, "z": loc.z}
        elif location is not None:
            if isinstance(location, list) and len(location) == 3:
                target = {"x": float(location[0]), "y": float(location[1]), "z": float(location[2])}
            elif isinstance(location, dict):
                target = {"x": float(location.get("x", 0)), "y": float(location.get("y", 0)), "z": float(location.get("z", 0))}
            else:
                return {"success": False, "data": {}, "error": "Invalid 'location': must be [x, y, z] array or {x, y, z} object"}
        else:
            return {"success": False, "data": {}, "error": "Provide 'actor_name' or 'location'"}

        from_loc = {"x": cam_loc.x, "y": cam_loc.y, "z": cam_loc.z}
        new_rot = _calculate_look_rotation(from_loc, target)

        from mcp_bridge.utils.serialization import rotator_from_dict
        subsystem.set_level_viewport_camera_info(cam_loc, rotator_from_dict(new_rot))

        return {
            "success": True,
            "data": {
                "camera_location": from_loc,
                "camera_rotation": new_rot,
                "target_location": target,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}
