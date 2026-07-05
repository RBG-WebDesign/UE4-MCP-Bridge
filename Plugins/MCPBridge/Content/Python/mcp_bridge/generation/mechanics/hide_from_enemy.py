"""hide_from_enemy mechanic -- adds hiding spot Blueprint."""
from __future__ import annotations

from mcp_bridge.generation.spec_schema import (
    BlueprintSpec,
    BuildSpec,
    IntentMap,
)
from mcp_bridge.generation.mechanics import register_mechanic


@register_mechanic("hide_from_enemy")
def apply_hide_from_enemy(intent: IntentMap, spec: BuildSpec) -> BuildSpec:
    """Add hide_from_enemy mechanic to spec.

    Adds:
    - BP_{feature_name}_HidingSpot (parent: Actor) with StaticMeshComponent("HideMesh")
      and BoxComponent("HideZone").
    - Variable: bIsOccupied (Boolean, False)
    """
    gameplay_path = f"/Game/Generated/{intent.feature_name}/Gameplay"

    # Create hiding spot Blueprint with StaticMesh + Box collision.
    hiding_bp = BlueprintSpec(
        name=f"BP_{intent.feature_name}_HidingSpot",
        parent_class="Actor",
        content_path=gameplay_path,
        components=[
            {"type": "StaticMeshComponent", "name": "HideMesh"},
            {"type": "BoxComponent", "name": "HideZone"},
        ],
        variables=[
            {
                "name": "bIsOccupied",
                "type": "Boolean",
                "default_value": False,
            }
        ],
    )

    spec.blueprints.append(hiding_bp)

    return spec
