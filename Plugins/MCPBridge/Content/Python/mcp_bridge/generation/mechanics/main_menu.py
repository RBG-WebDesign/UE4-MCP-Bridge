"""main_menu mechanic -- adds menu GameMode, MainMenu widget, and menu level."""
from __future__ import annotations

from mcp_bridge.generation.spec_schema import (
    BlueprintSpec,
    BuildSpec,
    IntentMap,
    LevelSpec,
    WidgetSpec,
)
from mcp_bridge.generation.mechanics import register_mechanic


@register_mechanic("main_menu")
def apply_main_menu(intent: IntentMap, spec: BuildSpec) -> BuildSpec:
    """Add main_menu mechanic to spec.

    Adds:
    - BP_{feature_name}_MenuGameMode (parent: GameModeBase)
    - WBP_{feature_name}_MainMenu widget with CanvasPanel root, GameTitle TextBlock
      (yellow, intent.feature_name.upper()), VerticalBox with PlayButton and QuitButton
    - LevelSpec for Map_{feature_name}_MainMenu with DirectionalLight
    """
    gameplay_path = f"/Game/Generated/{intent.feature_name}/Gameplay"
    ui_path = f"/Game/Generated/{intent.feature_name}/UI"
    maps_path = f"/Game/Generated/{intent.feature_name}/Maps"

    # Create menu GameMode Blueprint.
    menu_gm = BlueprintSpec(
        name=f"BP_{intent.feature_name}_MenuGameMode",
        parent_class="GameModeBase",
        content_path=gameplay_path,
    )

    spec.blueprints.append(menu_gm)

    # Create main menu widget with CanvasPanel root, title, and buttons.
    main_menu_widget = WidgetSpec(
        name=f"WBP_{intent.feature_name}_MainMenu",
        content_path=ui_path,
        root_widget={
            "type": "CanvasPanel",
            "name": "RootCanvas",
            "children": [
                {
                    "type": "TextBlock",
                    "name": "GameTitle",
                    "text": intent.feature_name.upper(),
                    "color": [1.0, 1.0, 0.0, 1.0],  # Yellow
                },
                {
                    "type": "VerticalBox",
                    "name": "ButtonBox",
                    "children": [
                        {
                            "type": "Button",
                            "name": "PlayButton",
                            "text": "Play",
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

    spec.widgets.append(main_menu_widget)

    # Create main menu level with DirectionalLight.
    menu_level = LevelSpec(
        name=f"Map_{intent.feature_name}_MainMenu",
        content_path=maps_path,
        actors=[
            {"type": "DirectionalLight", "location": [0.0, 0.0, 500.0]},
        ],
    )

    spec.levels.append(menu_level)

    return spec
