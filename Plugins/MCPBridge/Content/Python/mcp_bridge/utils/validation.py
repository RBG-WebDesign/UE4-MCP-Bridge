"""Validation helpers for checking actor state after operations."""

from typing import Any, Dict, List, Optional, Tuple


# Tolerance thresholds
LOCATION_TOLERANCE = 0.1    # units
ROTATION_TOLERANCE = 0.1    # degrees
SCALE_TOLERANCE = 0.001


def validate_actor_location(
    actor: Any,
    expected: Dict[str, float]
) -> Tuple[bool, Optional[str]]:
    """Check if an actor's location matches expected values within tolerance.
    
    Returns:
        (is_valid, error_message_or_none)
    """
    loc = actor.get_actor_location()
    
    dx = abs(loc.x - expected.get("x", 0))
    dy = abs(loc.y - expected.get("y", 0))
    dz = abs(loc.z - expected.get("z", 0))
    
    if dx > LOCATION_TOLERANCE or dy > LOCATION_TOLERANCE or dz > LOCATION_TOLERANCE:
        return False, (
            f"Location mismatch: expected ({expected.get('x',0)}, {expected.get('y',0)}, {expected.get('z',0)}), "
            f"got ({loc.x}, {loc.y}, {loc.z}), delta ({dx}, {dy}, {dz})"
        )
    return True, None


def validate_actor_rotation(
    actor: Any,
    expected: Dict[str, float]
) -> Tuple[bool, Optional[str]]:
    """Check if an actor's rotation matches expected values within tolerance."""
    rot = actor.get_actor_rotation()
    
    dp = abs(rot.pitch - expected.get("pitch", 0))
    dy = abs(rot.yaw - expected.get("yaw", 0))
    dr = abs(rot.roll - expected.get("roll", 0))
    
    if dp > ROTATION_TOLERANCE or dy > ROTATION_TOLERANCE or dr > ROTATION_TOLERANCE:
        return False, (
            f"Rotation mismatch: expected ({expected.get('pitch',0)}, {expected.get('yaw',0)}, {expected.get('roll',0)}), "
            f"got ({rot.pitch}, {rot.yaw}, {rot.roll})"
        )
    return True, None


def validate_actor_scale(
    actor: Any,
    expected: Dict[str, float]
) -> Tuple[bool, Optional[str]]:
    """Check if an actor's scale matches expected values within tolerance."""
    scale = actor.get_actor_scale3d()
    
    dx = abs(scale.x - expected.get("x", 1))
    dy = abs(scale.y - expected.get("y", 1))
    dz = abs(scale.z - expected.get("z", 1))
    
    if dx > SCALE_TOLERANCE or dy > SCALE_TOLERANCE or dz > SCALE_TOLERANCE:
        return False, (
            f"Scale mismatch: expected ({expected.get('x',1)}, {expected.get('y',1)}, {expected.get('z',1)}), "
            f"got ({scale.x}, {scale.y}, {scale.z})"
        )
    return True, None


def validate_actor_transform(
    actor: Any,
    expected_location: Optional[Dict[str, float]] = None,
    expected_rotation: Optional[Dict[str, float]] = None,
    expected_scale: Optional[Dict[str, float]] = None,
) -> Dict[str, Any]:
    """Validate an actor's full transform against expected values.
    
    Returns:
        Dict with 'valid' (bool) and 'errors' (list of error strings).
    """
    errors: List[str] = []
    
    if expected_location:
        valid, error = validate_actor_location(actor, expected_location)
        if not valid and error:
            errors.append(error)
    
    if expected_rotation:
        valid, error = validate_actor_rotation(actor, expected_rotation)
        if not valid and error:
            errors.append(error)
    
    if expected_scale:
        valid, error = validate_actor_scale(actor, expected_scale)
        if not valid and error:
            errors.append(error)
    
    return {
        "valid": len(errors) == 0,
        "errors": errors,
    }
