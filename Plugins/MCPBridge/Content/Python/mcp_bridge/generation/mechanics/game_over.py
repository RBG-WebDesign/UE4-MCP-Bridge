"""game_over mechanic -- adds game over widget."""
from __future__ import annotations

from mcp_bridge.generation.spec_schema import (
    BuildSpec,
    IntentMap,
    WidgetSpec,
)
from mcp_bridge.generation.mechanics import register_mechanic


@register_mechanic("game_over")
def apply_game_over(intent: IntentMap, spec: BuildSpec) -> BuildSpec:
    """Add game_over mechanic to spec.

    Adds:
    - WBP_{feature_name}_GameOver widget with CanvasPanel root, "GAME OVER" title (red),
      RetryButton, QuitButton
    """
    ui_path = f"/Game/Generated/{intent.feature_name}/UI"

    # Create game over widget with CanvasPanel root, title, and buttons.
    game_over_widget = WidgetSpec(
        name=f"WBP_{intent.feature_name}_GameOver",
        content_path=ui_path,
        root_widget={
            "type": "CanvasPanel",
            "name": "RootCanvas",
            "children": [
                {
                    "type": "TextBlock",
                    "name": "GameOverTitle",
                    "text": "GAME OVER",
                    "color": [1.0, 0.0, 0.0, 1.0],  # Red
                },
                {
                    "type": "VerticalBox",
                    "name": "ButtonBox",
                    "children": [
                        {
                            "type": "Button",
                            "name": "RetryButton",
                            "text": "Retry",
                        },
                        {
                            "type": "Button",
                            "name": "QuitButton",
                            "text": "Quit",
                        },
                    ],
                },
            ],
        },
    )

    spec.widgets.append(game_over_widget)

    return spec
