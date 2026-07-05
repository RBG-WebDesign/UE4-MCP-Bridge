"""Unit tests for gameplay mechanics -- no UE4 required.

Tests for all mechanics (Tasks 4-8). Only player_movement is implemented now.
Tests for other mechanics will fail with ImportError until those mechanics
are implemented in later tasks -- that is expected.

Run: python Plugins/MCPBridge/Content/Python/tests/test_mechanics.py
"""
import sys
import os

_PYTHON_ROOT = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, _PYTHON_ROOT)

from typing import Any, Dict
from mcp_bridge.generation.spec_schema import BehaviorTreeSpec, BuildSpec, IntentMap
from mcp_bridge.generation.intent_extractor import extract_intent


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_spec(prompt: str = "") -> BuildSpec:
    return BuildSpec(feature_name="Test", genre="generic", description=prompt)


def _make_intent(prompt: str = "test game") -> IntentMap:
    return extract_intent(prompt)


def _pass(name: str) -> None:
    print(f"  PASS  {name}")


# ---------------------------------------------------------------------------
# player_movement tests
# ---------------------------------------------------------------------------

def test_player_movement_adds_character_bp() -> None:
    from mcp_bridge.generation.mechanics.player_movement import apply_player_movement
    intent = _make_intent("test game")
    spec = apply_player_movement(intent, _make_spec())
    bp_names = [bp.name for bp in spec.blueprints]
    has_char = any("Character" in n or "Pawn" in n for n in bp_names)
    assert has_char, f"expected a Character/Pawn BP, got: {bp_names}"
    _pass("player_movement_adds_character_bp")


def test_player_movement_adds_controller() -> None:
    from mcp_bridge.generation.mechanics.player_movement import apply_player_movement
    intent = _make_intent("test game")
    spec = apply_player_movement(intent, _make_spec())
    bp_names = [bp.name for bp in spec.blueprints]
    has_controller = any("PlayerController" in n for n in bp_names)
    assert has_controller, f"expected a PlayerController BP, got: {bp_names}"
    _pass("player_movement_adds_controller")


def test_player_movement_adds_game_mode() -> None:
    from mcp_bridge.generation.mechanics.player_movement import apply_player_movement
    intent = _make_intent("test game")
    spec = apply_player_movement(intent, _make_spec())
    bp_names = [bp.name for bp in spec.blueprints]
    has_gm = any("GameMode" in n for n in bp_names)
    assert has_gm, f"expected a GameMode BP, got: {bp_names}"
    _pass("player_movement_adds_game_mode")


def test_player_movement_adds_input_mappings() -> None:
    from mcp_bridge.generation.mechanics.player_movement import apply_player_movement
    intent = _make_intent("test game")
    spec = apply_player_movement(intent, _make_spec())
    has_mappings = (
        len(spec.input_mappings.action_mappings) > 0
        or len(spec.input_mappings.axis_mappings) > 0
    )
    assert has_mappings, "expected action_mappings or axis_mappings to be non-empty"
    _pass("player_movement_adds_input_mappings")


# ---------------------------------------------------------------------------
# collect_item tests (will fail with ImportError until implemented)
# ---------------------------------------------------------------------------

def test_collect_item_adds_collectible_bp() -> None:
    from mcp_bridge.generation.mechanics.collect_item import apply_collect_item  # type: ignore[import]
    intent = _make_intent("collect coins")
    spec = apply_collect_item(intent, _make_spec())
    bp_names = [bp.name for bp in spec.blueprints]
    has_collectible = any(
        "Collectible" in n or "Coin" in n or "Item" in n or "Pickup" in n
        for n in bp_names
    )
    assert has_collectible, f"expected a collectible BP, got: {bp_names}"
    _pass("collect_item_adds_collectible_bp")


def test_collect_item_adds_score_hud() -> None:
    from mcp_bridge.generation.mechanics.collect_item import apply_collect_item  # type: ignore[import]
    intent = _make_intent("collect coins")
    spec = apply_collect_item(intent, _make_spec())
    widget_names = [w.name for w in spec.widgets]
    has_hud = any("HUD" in n or "Score" in n for n in widget_names)
    assert has_hud, f"expected a HUD/Score widget, got: {widget_names}"
    _pass("collect_item_adds_score_hud")


# ---------------------------------------------------------------------------
# door_trigger tests (will fail with ImportError until implemented)
# ---------------------------------------------------------------------------

def test_door_trigger_adds_door_bp() -> None:
    from mcp_bridge.generation.mechanics.door_trigger import apply_door_trigger  # type: ignore[import]
    intent = _make_intent("a door that opens when all coins are collected")
    spec = apply_door_trigger(intent, _make_spec())
    bp_names = [bp.name for bp in spec.blueprints]
    has_door = any("Door" in n or "Gate" in n for n in bp_names)
    assert has_door, f"expected a Door/Gate BP, got: {bp_names}"
    _pass("door_trigger_adds_door_bp")


# ---------------------------------------------------------------------------
# enemy_patrol tests (will fail with ImportError until implemented)
# ---------------------------------------------------------------------------

def test_enemy_patrol_adds_enemy_bp() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol  # type: ignore[import]
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    bp_names = [bp.name for bp in spec.blueprints]
    has_enemy = any("Enemy" in n or "Monster" in n or "Guard" in n for n in bp_names)
    assert has_enemy, f"expected an enemy BP, got: {bp_names}"
    _pass("enemy_patrol_adds_enemy_bp")


def test_enemy_patrol_adds_ai_controller() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol  # type: ignore[import]
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    bp_names = [bp.name for bp in spec.blueprints]
    has_aic = any("AIController" in n or "AIC_" in n for n in bp_names)
    assert has_aic, f"expected an AIController BP, got: {bp_names}"
    _pass("enemy_patrol_adds_ai_controller")


def test_enemy_patrol_adds_blackboard() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol  # type: ignore[import]
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    has_bb = len(spec.blackboards) > 0
    assert has_bb, "expected at least one blackboard in spec"
    _pass("enemy_patrol_adds_blackboard")


def test_enemy_patrol_bt_root_has_id() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    bt = spec.behavior_trees[0]
    assert "id" in bt.root, f"expected 'id' in BT root, got keys: {list(bt.root.keys())}"
    assert bt.root["id"], "BT root id must not be empty"
    _pass("enemy_patrol_bt_root_has_id")


def test_enemy_patrol_bt_root_has_decorators() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    bt = spec.behavior_trees[0]
    # At least one child should have decorators
    has_decorators = any(
        len(child.get("decorators", [])) > 0
        for child in bt.root.get("children", [])
    )
    assert has_decorators, "expected at least one child with decorators in BT root"
    _pass("enemy_patrol_bt_root_has_decorators")


def test_enemy_patrol_bt_uses_correct_param_names() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    bt = spec.behavior_trees[0]
    # Walk tree to find a MoveTo node and check param names
    def find_nodes(node, target_type):
        found = []
        if node.get("type") == target_type:
            found.append(node)
        for child in node.get("children", []):
            found.extend(find_nodes(child, target_type))
        return found

    move_nodes = find_nodes(bt.root, "MoveTo")
    assert len(move_nodes) > 0, "expected at least one MoveTo node"
    for node in move_nodes:
        params = node.get("params", {})
        assert "blackboard_key" in params, f"MoveTo missing 'blackboard_key', got: {list(params.keys())}"
    _pass("enemy_patrol_bt_uses_correct_param_names")


def test_enemy_patrol_bt_all_nodes_have_unique_ids() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    bt = spec.behavior_trees[0]
    ids = set()
    def collect_ids(node):
        node_id = node.get("id", "")
        assert node_id, f"node type '{node.get('type')}' has empty/missing id"
        assert node_id not in ids, f"duplicate id: {node_id}"
        ids.add(node_id)
        for child in node.get("children", []):
            collect_ids(child)
        for dec in node.get("decorators", []):
            collect_ids(dec)
        for svc in node.get("services", []):
            collect_ids(svc)
    collect_ids(bt.root)
    _pass("enemy_patrol_bt_all_nodes_have_unique_ids")


def test_enemy_patrol_bt_has_services() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    bt = spec.behavior_trees[0]
    # At least one child should have services
    has_services = any(
        len(child.get("services", [])) > 0
        for child in bt.root.get("children", [])
    )
    assert has_services, "expected at least one child with services in BT root"
    _pass("enemy_patrol_bt_has_services")


def test_enemy_patrol_bt_has_arithmetic_condition() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    bt = spec.behavior_trees[0]
    # Find decorators with arithmetic conditions
    arithmetic_ops = {"Equal", "NotEqual", "Less", "LessOrEqual", "Greater", "GreaterOrEqual"}
    found = False
    def search(node):
        nonlocal found
        for dec in node.get("decorators", []):
            cond = dec.get("params", {}).get("condition", "")
            if cond in arithmetic_ops:
                found = True
                return
        for child in node.get("children", []):
            search(child)
    search(bt.root)
    assert found, "expected at least one decorator with an arithmetic condition"
    _pass("enemy_patrol_bt_has_arithmetic_condition")


def test_enemy_patrol_bt_has_loop_decorator() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    bt = spec.behavior_trees[0]
    found = False
    def search(node):
        nonlocal found
        for dec in node.get("decorators", []):
            if dec.get("type") == "Loop":
                found = True
                return
        for child in node.get("children", []):
            search(child)
    search(bt.root)
    assert found, "expected at least one Loop decorator in the BT"
    _pass("enemy_patrol_bt_has_loop_decorator")


def test_enemy_patrol_bt_has_rotate_to_face() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    bt = spec.behavior_trees[0]
    def find_nodes(node, target_type):
        found = []
        if node.get("type") == target_type:
            found.append(node)
        for child in node.get("children", []):
            found.extend(find_nodes(child, target_type))
        return found
    rotate_nodes = find_nodes(bt.root, "RotateToFaceBBEntry")
    assert len(rotate_nodes) > 0, "expected at least one RotateToFaceBBEntry node"
    _pass("enemy_patrol_bt_has_rotate_to_face")


# ---------------------------------------------------------------------------
# BT new task node specs (PlaySound, FinishWithResult, SetTagCooldown)
# ---------------------------------------------------------------------------

def _make_bt_spec(root: Dict[str, Any]) -> BehaviorTreeSpec:
    return BehaviorTreeSpec(
        name="BT_Test",
        content_path="/Game/Generated/AI",
        blackboard_path="/Game/Generated/AI/BB_Test",
        root=root,
    )


def test_bt_play_sound_task_spec() -> None:
    root = {
        "id": "root_sel",
        "type": "Selector",
        "children": [
            {
                "id": "play_sound_1",
                "type": "PlaySound",
                "params": {"sound_to_play": "/Game/Audio/SFX_Alert"},
            }
        ],
    }
    bt = _make_bt_spec(root)
    child = bt.root["children"][0]
    assert child["type"] == "PlaySound"
    assert child["id"] == "play_sound_1"
    assert "sound_to_play" in child["params"]
    _pass("bt_play_sound_task_spec")


def test_bt_finish_with_result_task_spec() -> None:
    root = {
        "id": "root_seq",
        "type": "Sequence",
        "children": [
            {
                "id": "finish_1",
                "type": "FinishWithResult",
                "params": {"result": "Succeeded"},
            }
        ],
    }
    bt = _make_bt_spec(root)
    child = bt.root["children"][0]
    assert child["type"] == "FinishWithResult"
    assert child["params"]["result"] in ("Succeeded", "Failed", "Aborted")
    _pass("bt_finish_with_result_task_spec")


def test_bt_set_tag_cooldown_task_spec() -> None:
    root = {
        "id": "root_seq",
        "type": "Sequence",
        "children": [
            {
                "id": "set_cd_1",
                "type": "SetTagCooldown",
                "params": {
                    "cooldown_tag": "AttackCooldown",
                    "cooldown_duration": 3.0,
                    "add_to_existing": False,
                },
            }
        ],
    }
    bt = _make_bt_spec(root)
    child = bt.root["children"][0]
    assert child["type"] == "SetTagCooldown"
    assert "cooldown_tag" in child["params"]
    assert "cooldown_duration" in child["params"]
    assert "add_to_existing" in child["params"]
    _pass("bt_set_tag_cooldown_task_spec")


# ---------------------------------------------------------------------------
# BT new decorator node specs
# ---------------------------------------------------------------------------

def test_bt_is_at_location_decorator_spec() -> None:
    root = {
        "id": "root_sel",
        "type": "Selector",
        "children": [
            {
                "id": "wait_1",
                "type": "Wait",
                "params": {"wait_time": 1.0},
                "decorators": [
                    {
                        "id": "at_loc_1",
                        "type": "IsAtLocation",
                        "params": {
                            "blackboard_key": "TargetLocation",
                            "acceptable_radius": 100.0,
                            "inverse_condition": False,
                        },
                    }
                ],
            }
        ],
    }
    bt = _make_bt_spec(root)
    dec = bt.root["children"][0]["decorators"][0]
    assert dec["type"] == "IsAtLocation"
    assert "blackboard_key" in dec["params"]
    assert "acceptable_radius" in dec["params"]
    _pass("bt_is_at_location_decorator_spec")


def test_bt_does_path_exist_decorator_spec() -> None:
    root = {
        "id": "root_sel",
        "type": "Selector",
        "children": [
            {
                "id": "move_1",
                "type": "MoveTo",
                "params": {"blackboard_key": "Target"},
                "decorators": [
                    {
                        "id": "path_1",
                        "type": "DoesPathExist",
                        "params": {
                            "blackboard_key_a": "SelfActor",
                            "blackboard_key_b": "Target",
                            "path_query_type": "NavmeshRaycast2D",
                        },
                    }
                ],
            }
        ],
    }
    bt = _make_bt_spec(root)
    dec = bt.root["children"][0]["decorators"][0]
    assert dec["type"] == "DoesPathExist"
    assert "blackboard_key_a" in dec["params"]
    assert "blackboard_key_b" in dec["params"]
    assert "path_query_type" in dec["params"]
    _pass("bt_does_path_exist_decorator_spec")


def test_bt_tag_cooldown_decorator_spec() -> None:
    root = {
        "id": "root_seq",
        "type": "Sequence",
        "children": [
            {
                "id": "wait_1",
                "type": "Wait",
                "params": {"wait_time": 2.0},
                "decorators": [
                    {
                        "id": "tag_cd_1",
                        "type": "TagCooldown",
                        "params": {
                            "cooldown_tag": "AttackTag",
                            "cool_down_time": 5.0,
                            "add_to_existing": True,
                        },
                    }
                ],
            }
        ],
    }
    bt = _make_bt_spec(root)
    dec = bt.root["children"][0]["decorators"][0]
    assert dec["type"] == "TagCooldown"
    assert "cooldown_tag" in dec["params"]
    assert "cool_down_time" in dec["params"]
    _pass("bt_tag_cooldown_decorator_spec")


def test_bt_conditional_loop_decorator_spec() -> None:
    root = {
        "id": "root_seq",
        "type": "Sequence",
        "children": [
            {
                "id": "wait_1",
                "type": "Wait",
                "params": {"wait_time": 1.0},
                "decorators": [
                    {
                        "id": "cond_loop_1",
                        "type": "ConditionalLoop",
                        "params": {
                            "blackboard_key": "ShouldLoop",
                            "condition": "IsSet",
                        },
                    }
                ],
            }
        ],
    }
    bt = _make_bt_spec(root)
    dec = bt.root["children"][0]["decorators"][0]
    assert dec["type"] == "ConditionalLoop"
    assert "blackboard_key" in dec["params"]
    assert "condition" in dec["params"]
    _pass("bt_conditional_loop_decorator_spec")


def test_bt_keep_in_cone_decorator_spec() -> None:
    root = {
        "id": "root_sel",
        "type": "Selector",
        "children": [
            {
                "id": "move_1",
                "type": "MoveTo",
                "params": {"blackboard_key": "Target"},
                "decorators": [
                    {
                        "id": "cone_1",
                        "type": "KeepInCone",
                        "params": {
                            "cone_half_angle": 45.0,
                            "cone_origin": "SelfActor",
                            "observed": "Target",
                        },
                    }
                ],
            }
        ],
    }
    bt = _make_bt_spec(root)
    dec = bt.root["children"][0]["decorators"][0]
    assert dec["type"] == "KeepInCone"
    assert "cone_half_angle" in dec["params"]
    assert "cone_origin" in dec["params"]
    assert "observed" in dec["params"]
    _pass("bt_keep_in_cone_decorator_spec")


def test_bt_is_bb_entry_of_class_decorator_spec() -> None:
    root = {
        "id": "root_sel",
        "type": "Selector",
        "children": [
            {
                "id": "wait_1",
                "type": "Wait",
                "params": {"wait_time": 1.0},
                "decorators": [
                    {
                        "id": "class_check_1",
                        "type": "IsBBEntryOfClass",
                        "params": {
                            "blackboard_key": "TargetActor",
                            "test_class": "Character",
                        },
                    }
                ],
            }
        ],
    }
    bt = _make_bt_spec(root)
    dec = bt.root["children"][0]["decorators"][0]
    assert dec["type"] == "IsBBEntryOfClass"
    assert "blackboard_key" in dec["params"]
    assert "test_class" in dec["params"]
    _pass("bt_is_bb_entry_of_class_decorator_spec")


# ---------------------------------------------------------------------------
# enemy_patrol regression test
# ---------------------------------------------------------------------------

def test_enemy_patrol_regression_produces_valid_structure() -> None:
    from mcp_bridge.generation.mechanics.enemy_patrol import apply_enemy_patrol
    intent = _make_intent("enemies that patrol the level")
    spec = apply_enemy_patrol(intent, _make_spec())
    # Has blueprints
    assert len(spec.blueprints) > 0, "expected at least one blueprint"
    # Has behavior trees
    assert len(spec.behavior_trees) > 0, "expected at least one behavior tree"
    # Has blackboards
    assert len(spec.blackboards) > 0, "expected at least one blackboard"
    # BT root is well-formed
    bt = spec.behavior_trees[0]
    assert "id" in bt.root, "BT root missing 'id'"
    assert "type" in bt.root, "BT root missing 'type'"
    assert "children" in bt.root, "BT root missing 'children'"
    assert len(bt.root["children"]) > 0, "BT root has no children"
    # Each child has id and type
    for child in bt.root["children"]:
        assert "id" in child, f"child missing 'id': {child}"
        assert "type" in child, f"child missing 'type': {child}"
    _pass("enemy_patrol_regression_produces_valid_structure")


# ---------------------------------------------------------------------------
# hide_from_enemy tests (will fail with ImportError until implemented)
# ---------------------------------------------------------------------------

def test_hide_adds_hiding_spot() -> None:
    from mcp_bridge.generation.mechanics.hide_from_enemy import apply_hide_from_enemy  # type: ignore[import]
    intent = _make_intent("hide from the monster in hiding spots")
    spec = apply_hide_from_enemy(intent, _make_spec())
    bp_names = [bp.name for bp in spec.blueprints]
    has_hiding = any("Hiding" in n or "HideSpot" in n or "Cover" in n for n in bp_names)
    assert has_hiding, f"expected a hiding spot BP, got: {bp_names}"
    _pass("hide_adds_hiding_spot")


# ---------------------------------------------------------------------------
# main_menu tests (will fail with ImportError until implemented)
# ---------------------------------------------------------------------------

def test_main_menu_adds_widget() -> None:
    from mcp_bridge.generation.mechanics.main_menu import apply_main_menu  # type: ignore[import]
    intent = _make_intent("game with a main menu")
    spec = apply_main_menu(intent, _make_spec())
    widget_names = [w.name for w in spec.widgets]
    has_menu = any("MainMenu" in n or "Menu" in n or "Title" in n for n in widget_names)
    assert has_menu, f"expected a main menu widget, got: {widget_names}"
    _pass("main_menu_adds_widget")


# ---------------------------------------------------------------------------
# game_over tests (will fail with ImportError until implemented)
# ---------------------------------------------------------------------------

def test_game_over_adds_widget() -> None:
    from mcp_bridge.generation.mechanics.game_over import apply_game_over  # type: ignore[import]
    intent = _make_intent("game over screen when player dies")
    spec = apply_game_over(intent, _make_spec())
    widget_names = [w.name for w in spec.widgets]
    has_gameover = any("GameOver" in n or "Death" in n or "Retry" in n for n in widget_names)
    assert has_gameover, f"expected a game over widget, got: {widget_names}"
    _pass("game_over_adds_widget")


# ---------------------------------------------------------------------------
# Main -- runs all tests (unimplemented ones will fail, that's expected)
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    player_movement_tests = [
        test_player_movement_adds_character_bp,
        test_player_movement_adds_controller,
        test_player_movement_adds_game_mode,
        test_player_movement_adds_input_mappings,
    ]

    bt_node_spec_tests = [
        test_bt_play_sound_task_spec,
        test_bt_finish_with_result_task_spec,
        test_bt_set_tag_cooldown_task_spec,
        test_bt_is_at_location_decorator_spec,
        test_bt_does_path_exist_decorator_spec,
        test_bt_tag_cooldown_decorator_spec,
        test_bt_conditional_loop_decorator_spec,
        test_bt_keep_in_cone_decorator_spec,
        test_bt_is_bb_entry_of_class_decorator_spec,
        test_enemy_patrol_regression_produces_valid_structure,
    ]

    other_mechanic_tests = [
        test_collect_item_adds_collectible_bp,
        test_collect_item_adds_score_hud,
        test_door_trigger_adds_door_bp,
        test_enemy_patrol_adds_enemy_bp,
        test_enemy_patrol_adds_ai_controller,
        test_enemy_patrol_adds_blackboard,
        test_enemy_patrol_bt_root_has_id,
        test_enemy_patrol_bt_root_has_decorators,
        test_enemy_patrol_bt_uses_correct_param_names,
        test_enemy_patrol_bt_all_nodes_have_unique_ids,
        test_enemy_patrol_bt_has_services,
        test_enemy_patrol_bt_has_arithmetic_condition,
        test_enemy_patrol_bt_has_loop_decorator,
        test_enemy_patrol_bt_has_rotate_to_face,
        test_hide_adds_hiding_spot,
        test_main_menu_adds_widget,
        test_game_over_adds_widget,
    ]

    print("Running player_movement tests...")
    for fn in player_movement_tests:
        fn()
    print("All player_movement tests passed.\n")

    print("Running BT node spec tests...")
    for fn in bt_node_spec_tests:
        try:
            fn()
        except ImportError as e:
            print(f"  SKIP  {fn.__name__} (ImportError: {e})")
        except Exception as e:
            print(f"  FAIL  {fn.__name__}: {e}")
    print("BT node spec tests done.\n")

    print("Running other mechanic tests (expected to fail with ImportError)...")
    for fn in other_mechanic_tests:
        try:
            fn()
        except ImportError as e:
            print(f"  SKIP  {fn.__name__} (ImportError: {e})")
        except Exception as e:
            print(f"  FAIL  {fn.__name__}: {e}")
    print("Done.")
