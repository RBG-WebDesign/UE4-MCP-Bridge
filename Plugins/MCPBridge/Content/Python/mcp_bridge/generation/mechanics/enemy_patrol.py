"""enemy_patrol mechanic -- adds enemy character, AI controller, blackboard, behavior tree,
and red material to a BuildSpec.

The BT builder supports expanded node types beyond what this mechanic uses:
Tasks: PlaySound, FinishWithResult, SetTagCooldown
Decorators: IsAtLocation, DoesPathExist, TagCooldown, ConditionalLoop, KeepInCone, IsBBEntryOfClass
"""
from __future__ import annotations

from mcp_bridge.generation.spec_schema import (
    BehaviorTreeSpec,
    BlackboardSpec,
    BlueprintSpec,
    BuildSpec,
    IntentMap,
    MaterialSpec,
)
from mcp_bridge.generation.mechanics import register_mechanic


@register_mechanic("enemy_patrol")
def apply_enemy_patrol(intent: IntentMap, spec: BuildSpec) -> BuildSpec:
    """Add enemy patrol assets to spec.

    Adds:
    - BP_{feature_name}_Enemy (parent: Character) with StaticMeshComponent("EnemyMesh")
      + SphereComponent("DetectionSphere")
    - BP_{feature_name}_AIController (parent: AIController)
    - BB_{feature_name}_Enemy (Blackboard) with keys: TargetActor (Object),
      PatrolLocation (Vector), bIsAlerted (Bool), AlertLevel (Float)
    - BT_{feature_name}_Enemy (BehaviorTree) with Selector root containing
      two Sequences (MoveTo + Wait)
    - M_{feature_name}_Enemy (Material) with red base_color [0.8, 0.1, 0.1, 1.0]
    """
    gameplay_path = f"/Game/Generated/{intent.feature_name}/Gameplay"
    ai_path = f"/Game/Generated/{intent.feature_name}/AI"
    art_path = f"/Game/Generated/{intent.feature_name}/Art"

    # Enemy Blueprint with mesh and detection sphere.
    enemy_bp = BlueprintSpec(
        name=f"BP_{intent.feature_name}_Enemy",
        parent_class="Character",
        content_path=gameplay_path,
        components=[
            {"type": "StaticMeshComponent", "name": "EnemyMesh"},
            {"type": "SphereComponent", "name": "DetectionSphere", "attach_to": "RootComponent"},
        ],
    )

    # AIController Blueprint.
    ai_controller_bp = BlueprintSpec(
        name=f"BP_{intent.feature_name}_AIController",
        parent_class="AIController",
        content_path=gameplay_path,
    )

    spec.blueprints.extend([enemy_bp, ai_controller_bp])

    # Blackboard with patrol and alert state keys.
    blackboard = BlackboardSpec(
        name=f"BB_{intent.feature_name}_Enemy",
        content_path=ai_path,
        keys=[
            {"name": "TargetActor", "type": "Object"},
            {"name": "PatrolLocation", "type": "Vector"},
            {"name": "bIsAlerted", "type": "Bool"},
            {"name": "AlertLevel", "type": "Float"},
        ],
    )
    spec.blackboards.append(blackboard)

    # Behavior Tree: Selector root with three branches.
    # 1. High alert chase (AlertLevel >= 0.5, with DefaultFocus service)
    # 2. Investigate (target set but low alert, with cooldown)
    # 3. Patrol loop (no target, looping patrol points)
    behavior_tree = BehaviorTreeSpec(
        name=f"BT_{intent.feature_name}_Enemy",
        content_path=ai_path,
        blackboard_path=f"{ai_path}/BB_{intent.feature_name}_Enemy",
        root={
            "id": "root_selector",
            "type": "Selector",
            "name": "EnemyBehavior",
            "children": [
                # Branch 1: High-alert chase -- uses arithmetic condition + service
                {
                    "id": "chase_sequence",
                    "type": "Sequence",
                    "name": "ChasePlayer",
                    "decorators": [
                        {
                            "id": "has_target",
                            "type": "Blackboard",
                            "name": "HasTarget",
                            "params": {
                                "blackboard_key": "TargetActor",
                                "condition": "IsSet",
                            },
                        },
                        {
                            "id": "high_alert",
                            "type": "Blackboard",
                            "name": "HighAlert",
                            "params": {
                                "blackboard_key": "AlertLevel",
                                "condition": "GreaterOrEqual",
                                "float_value": 0.5,
                            },
                        },
                    ],
                    "services": [
                        {
                            "id": "focus_target",
                            "type": "DefaultFocus",
                            "name": "FocusOnTarget",
                            "params": {
                                "blackboard_key": "TargetActor",
                            },
                        },
                    ],
                    "children": [
                        {
                            "id": "rotate_to_target",
                            "type": "RotateToFaceBBEntry",
                            "name": "FaceTarget",
                            "params": {
                                "blackboard_key": "TargetActor",
                            },
                        },
                        {
                            "id": "move_to_target",
                            "type": "MoveTo",
                            "name": "ChaseTarget",
                            "params": {
                                "blackboard_key": "TargetActor",
                                "acceptable_radius": 100.0,
                            },
                        },
                    ],
                },
                # Branch 2: Investigate -- target set but low alert, with cooldown
                {
                    "id": "investigate_sequence",
                    "type": "Sequence",
                    "name": "Investigate",
                    "decorators": [
                        {
                            "id": "has_target_investigate",
                            "type": "Blackboard",
                            "name": "HasTargetInvestigate",
                            "params": {
                                "blackboard_key": "TargetActor",
                                "condition": "IsSet",
                            },
                        },
                        {
                            "id": "low_alert",
                            "type": "Blackboard",
                            "name": "LowAlert",
                            "params": {
                                "blackboard_key": "AlertLevel",
                                "condition": "Less",
                                "float_value": 0.5,
                            },
                        },
                        {
                            "id": "investigate_cooldown",
                            "type": "Cooldown",
                            "name": "InvestigateCooldown",
                            "params": {
                                "cool_down_time": 3.0,
                            },
                        },
                    ],
                    "children": [
                        {
                            "id": "move_to_investigate",
                            "type": "MoveTo",
                            "name": "ApproachTarget",
                            "params": {
                                "blackboard_key": "TargetActor",
                                "acceptable_radius": 200.0,
                            },
                        },
                        {
                            "id": "investigate_wait",
                            "type": "Wait",
                            "name": "LookAround",
                            "params": {
                                "wait_time": 3.0,
                                "random_deviation": 1.0,
                            },
                        },
                    ],
                },
                # Branch 3: Patrol loop -- no target, loops forever
                {
                    "id": "patrol_sequence",
                    "type": "Sequence",
                    "name": "Patrol",
                    "decorators": [
                        {
                            "id": "no_target",
                            "type": "Blackboard",
                            "name": "NoTarget",
                            "params": {
                                "blackboard_key": "TargetActor",
                                "condition": "IsNotSet",
                            },
                        },
                        {
                            "id": "patrol_loop",
                            "type": "Loop",
                            "name": "LoopForever",
                            "params": {
                                "infinite_loop": "true",
                            },
                        },
                    ],
                    "children": [
                        {
                            "id": "move_to_patrol",
                            "type": "MoveTo",
                            "name": "GoToPatrolPoint",
                            "params": {
                                "blackboard_key": "PatrolLocation",
                                "acceptable_radius": 50.0,
                            },
                        },
                        {
                            "id": "patrol_wait",
                            "type": "Wait",
                            "name": "WaitAtPoint",
                            "params": {
                                "wait_time": 2.0,
                                "random_deviation": 1.0,
                            },
                        },
                    ],
                },
            ],
        },
    )
    spec.behavior_trees.append(behavior_tree)

    # Red material for the enemy.
    material = MaterialSpec(
        name=f"M_{intent.feature_name}_Enemy",
        content_path=art_path,
        base_color=[0.8, 0.1, 0.1, 1.0],
    )
    spec.materials.append(material)

    return spec
