"""collect_item mechanic -- adds collectible item BP and score counter widget."""
from __future__ import annotations

from mcp_bridge.generation.spec_schema import (
    BlueprintSpec,
    BuildSpec,
    IntentMap,
    WidgetSpec,
)
from mcp_bridge.generation.mechanics import register_mechanic


@register_mechanic("collect_item")
def apply_collect_item(intent: IntentMap, spec: BuildSpec) -> BuildSpec:
    """Add collect_item mechanic to spec.

    Adds:
    - BP_{feature_name}_{item_name} (parent: Actor) with StaticMeshComponent + SphereComponent.
      Item name is derived from intent.actors with role "collectible" (capitalized).
      Defaults to "Collectible" if no matching actor found.
    - WBP_{feature_name}_ScoreCounter widget with CanvasPanel root containing
      TextBlock showing "{item_name}s: 0"
    """
    gameplay_path = f"/Game/Generated/{intent.feature_name}/Gameplay"
    ui_path = f"/Game/Generated/{intent.feature_name}/UI"

    # Extract collectible item name from intent.actors.
    # Look for actors with role "collectible", capitalize the first letter.
    # Default to "Collectible" if none found.
    item_name = "Collectible"
    for actor in intent.actors:
        if actor.role == "collectible":
            # Capitalize first letter of actor name.
            item_name = actor.name.capitalize()
            break

    # Create collectible item Blueprint with StaticMesh + Sphere collision.
    item_bp = BlueprintSpec(
        name=f"BP_{intent.feature_name}_{item_name}",
        parent_class="Actor",
        content_path=gameplay_path,
        components=[
            {"type": "StaticMeshComponent", "name": "Mesh"},
            {"type": "SphereComponent", "name": "Collision"},
        ],
    )

    spec.blueprints.append(item_bp)

    # Create score counter widget with CanvasPanel root and TextBlock.
    score_widget = WidgetSpec(
        name=f"WBP_{intent.feature_name}_ScoreCounter",
        content_path=ui_path,
        root_widget={
            "type": "CanvasPanel",
            "name": "RootCanvas",
            "children": [
                {
                    "type": "TextBlock",
                    "name": "ScoreText",
                    "text": f"{item_name}s: 0",
                }
            ],
        },
    )

    spec.widgets.append(score_widget)

    return spec
