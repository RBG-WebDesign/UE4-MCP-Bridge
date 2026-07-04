"""player_movement mechanic -- adds Character BP, PlayerController, GameMode,
a starter level, and input mappings to a BuildSpec.
"""
from __future__ import annotations

from mcp_bridge.generation.spec_schema import (
    BlueprintSpec,
    BuildSpec,
    InputMappingSpec,
    IntentMap,
    LevelSpec,
)
from mcp_bridge.generation.mechanics import register_mechanic


@register_mechanic("player_movement")
def apply_player_movement(intent: IntentMap, spec: BuildSpec) -> BuildSpec:
    """Add player movement assets to spec.

    Adds:
    - BP_{feature_name}_Character (parent: Character) with SpringArm + Camera
    - BP_{feature_name}_PlayerController (parent: PlayerController)
    - BP_{feature_name}_GameMode (parent: GameModeBase)
    - A starter LevelSpec with PlayerStart and DirectionalLight
    - InputMappingSpec -- platformer gets Jump + MoveRight; everything else gets
      WASD + mouse look + Jump + Interact + Sprint
    """
    gameplay_path = f"/Game/Generated/{intent.feature_name}/Gameplay"
    maps_path = f"/Game/Generated/{intent.feature_name}/Maps"

    # Character Blueprint with third-person camera rig components.
    char_bp = BlueprintSpec(
        name=f"BP_{intent.feature_name}_Character",
        parent_class="Character",
        content_path=gameplay_path,
        components=[
            {"type": "SpringArmComponent", "name": "CameraBoom"},
            {"type": "CameraComponent", "name": "FollowCamera", "attach_to": "CameraBoom"},
        ],
    )

    # PlayerController Blueprint.
    controller_bp = BlueprintSpec(
        name=f"BP_{intent.feature_name}_PlayerController",
        parent_class="PlayerController",
        content_path=gameplay_path,
    )

    # GameMode Blueprint.
    game_mode_bp = BlueprintSpec(
        name=f"BP_{intent.feature_name}_GameMode",
        parent_class="GameModeBase",
        content_path=gameplay_path,
    )

    spec.blueprints.extend([char_bp, controller_bp, game_mode_bp])

    # Starter level.
    level = LevelSpec(
        name=f"Map_{intent.feature_name}_Gameplay",
        content_path=maps_path,
        actors=[
            {"type": "PlayerStart", "location": [0.0, 0.0, 100.0]},
            {"type": "DirectionalLight", "location": [0.0, 0.0, 500.0]},
        ],
    )
    spec.levels.append(level)

    # Input mappings -- platformer uses side-scroller bindings, others use WASD.
    is_platformer = intent.genre == "platformer"
    if is_platformer:
        spec.input_mappings = InputMappingSpec(
            action_mappings=[
                {"name": "Jump", "keys": ["SpaceBar"]},
            ],
            axis_mappings=[
                {"name": "MoveRight", "keys": [{"key": "D", "scale": 1.0}, {"key": "A", "scale": -1.0}]},
            ],
        )
    else:
        spec.input_mappings = InputMappingSpec(
            action_mappings=[
                {"name": "Jump", "keys": ["SpaceBar"]},
                {"name": "Interact", "keys": ["E"]},
                {"name": "Sprint", "keys": ["LeftShift"]},
            ],
            axis_mappings=[
                {"name": "MoveForward", "keys": [{"key": "W", "scale": 1.0}, {"key": "S", "scale": -1.0}]},
                {"name": "MoveRight", "keys": [{"key": "D", "scale": 1.0}, {"key": "A", "scale": -1.0}]},
                {"name": "LookUp", "keys": [{"key": "MouseY", "scale": -1.0}]},
                {"name": "Turn", "keys": [{"key": "MouseX", "scale": 1.0}]},
            ],
        )

    return spec
