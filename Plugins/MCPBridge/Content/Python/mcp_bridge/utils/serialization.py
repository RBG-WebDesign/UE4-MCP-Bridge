"""Serialization utilities for converting UE4 objects to JSON-safe dicts."""

from typing import Any, Dict, Optional


def vector_from_dict(d: Dict[str, float]) -> Any:
    """Create an unreal.Vector from a dict with x, y, z keys."""
    import unreal
    return unreal.Vector(
        x=float(d.get("x", 0)),
        y=float(d.get("y", 0)),
        z=float(d.get("z", 0)),
    )


def rotator_from_dict(d: Dict[str, float]) -> Any:
    """Create an unreal.Rotator from a dict with pitch, yaw, roll keys."""
    import unreal
    return unreal.Rotator(
        pitch=float(d.get("pitch", 0)),
        yaw=float(d.get("yaw", 0)),
        roll=float(d.get("roll", 0)),
    )


def vector_to_dict(v: Any) -> Dict[str, float]:
    """Convert an unreal.Vector to a JSON-safe dict."""
    return {"x": v.x, "y": v.y, "z": v.z}


def rotator_to_dict(r: Any) -> Dict[str, float]:
    """Convert an unreal.Rotator to a JSON-safe dict."""
    return {"pitch": r.pitch, "yaw": r.yaw, "roll": r.roll}


def actor_to_dict(actor: Any) -> Dict[str, Any]:
    """Convert an actor to a JSON-safe summary dict.
    
    Args:
        actor: An unreal.Actor instance.
    
    Returns:
        Dict with name, class, location, rotation, scale, folder.
    """
    loc = actor.get_actor_location()
    rot = actor.get_actor_rotation()
    scale = actor.get_actor_scale3d()
    
    return {
        "name": actor.get_actor_label(),
        "class": actor.get_class().get_name(),
        "location": vector_to_dict(loc),
        "rotation": rotator_to_dict(rot),
        "scale": vector_to_dict(scale),
        "folder": str(actor.get_folder_path()),
    }
