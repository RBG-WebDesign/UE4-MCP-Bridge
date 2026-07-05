"""door_trigger mechanic -- adds Door and TriggerZone blueprints to a BuildSpec.

Adds:
- BP_{feature_name}_Door (parent: Actor) with StaticMeshComponent + BoxComponent
  Variables: bIsOpen, bIsLocked, RequiredKeyCount
- BP_{feature_name}_TriggerZone (parent: Actor) with BoxComponent for trigger volume
"""
from __future__ import annotations

from mcp_bridge.generation.spec_schema import BlueprintSpec, BuildSpec, IntentMap
from mcp_bridge.generation.mechanics import register_mechanic


@register_mechanic("door_trigger")
def apply_door_trigger(intent: IntentMap, spec: BuildSpec) -> BuildSpec:
    """Add door trigger assets to spec.

    Adds:
    - BP_{feature_name}_Door (parent: Actor) with StaticMeshComponent("DoorMesh")
      and BoxComponent("TriggerVolume"). Variables: bIsOpen (Boolean, False),
      bIsLocked (Boolean, False), RequiredKeyCount (Integer, 0).
    - BP_{feature_name}_TriggerZone (parent: Actor) with BoxComponent("TriggerBox")
    """
    gameplay_path = f"/Game/Generated/{intent.feature_name}/Gameplay"

    # Door Blueprint with mesh and trigger volume.
    door_bp = BlueprintSpec(
        name=f"BP_{intent.feature_name}_Door",
        parent_class="Actor",
        content_path=gameplay_path,
        components=[
            {"type": "StaticMeshComponent", "name": "DoorMesh"},
            {"type": "BoxComponent", "name": "TriggerVolume"},
        ],
        variables=[
            {"name": "bIsOpen", "type": "Boolean", "default_value": False},
            {"name": "bIsLocked", "type": "Boolean", "default_value": False},
            {"name": "RequiredKeyCount", "type": "Integer", "default_value": 0},
        ],
    )

    # Trigger Zone Blueprint with collision box.
    trigger_zone_bp = BlueprintSpec(
        name=f"BP_{intent.feature_name}_TriggerZone",
        parent_class="Actor",
        content_path=gameplay_path,
        components=[
            {"type": "BoxComponent", "name": "TriggerBox"},
        ],
    )

    spec.blueprints.extend([door_bp, trigger_zone_bp])

    return spec
