"""Unit tests for intent_extractor -- no UE4 required.

Run: python Plugins/MCPBridge/Content/Python/tests/test_intent_extractor.py
"""
import sys
import os

_PYTHON_ROOT = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, _PYTHON_ROOT)

from mcp_bridge.generation.intent_extractor import extract_intent


def _pass(name: str) -> None:
    print(f"  PASS  {name}")


def test_horror_genre_detection() -> None:
    intent = extract_intent("make a horror game with a monster")
    assert intent.genre == "horror", f"expected horror, got {intent.genre}"
    _pass("horror genre detection")


def test_platformer_genre_detection() -> None:
    intent = extract_intent("build a platformer with coins")
    assert intent.genre == "platformer", f"expected platformer, got {intent.genre}"
    _pass("platformer genre detection")


def test_generic_fallback() -> None:
    intent = extract_intent("make something cool")
    assert intent.genre == "generic", f"expected generic, got {intent.genre}"
    _pass("generic fallback")


def test_enemy_actor_extracted() -> None:
    intent = extract_intent("horror game with a monster chasing the player")
    actor_names = [a.name for a in intent.actors]
    assert "enemy" in actor_names or "monster" in actor_names, f"expected enemy/monster in {actor_names}"
    assert "player" in actor_names, f"expected player in {actor_names}"
    _pass("enemy actor extracted")


def test_collectible_detected() -> None:
    intent = extract_intent("platformer with coins to collect")
    mechanic_names = [m.name for m in intent.mechanics]
    assert "collect_item" in mechanic_names, f"expected collect_item in {mechanic_names}"
    _pass("collectible detected")


def test_door_relationship() -> None:
    intent = extract_intent("a door that opens when all coins are collected")
    assert len(intent.relationships) >= 1, f"expected at least 1 relationship, got {len(intent.relationships)}"
    rel = intent.relationships[0]
    assert "door" in rel.subject.lower(), f"expected door in subject, got {rel.subject}"
    assert "open" in rel.verb.lower(), f"expected open in verb, got {rel.verb}"
    _pass("door relationship")


def test_enemy_patrol_mechanic() -> None:
    intent = extract_intent("enemies that patrol around the level")
    mechanic_names = [m.name for m in intent.mechanics]
    assert "enemy_patrol" in mechanic_names, f"expected enemy_patrol in {mechanic_names}"
    _pass("enemy patrol mechanic")


def test_menu_ui_request() -> None:
    intent = extract_intent("game with a main menu and pause screen")
    assert "main_menu" in intent.ui_requests, f"expected main_menu in {intent.ui_requests}"
    assert "pause_menu" in intent.ui_requests, f"expected pause_menu in {intent.ui_requests}"
    _pass("menu UI request")


def test_hide_mechanic() -> None:
    intent = extract_intent("horror game where you hide from the monster")
    mechanic_names = [m.name for m in intent.mechanics]
    assert "hide_from_enemy" in mechanic_names, f"expected hide_from_enemy in {mechanic_names}"
    _pass("hide mechanic")


def test_feature_name_derived() -> None:
    intent = extract_intent("make a horror game")
    assert intent.feature_name != "", f"feature_name should not be empty"
    assert "horror" in intent.feature_name.lower() or "Horror" in intent.feature_name, \
        f"expected horror in feature_name, got {intent.feature_name}"
    _pass("feature name derived")


def test_player_movement_always_present() -> None:
    intent = extract_intent("make a platformer")
    mechanic_names = [m.name for m in intent.mechanics]
    assert "player_movement" in mechanic_names, f"expected player_movement in {mechanic_names}"
    _pass("player_movement always present")


def test_description_preserved() -> None:
    prompt = "build a cool platformer with coins"
    intent = extract_intent(prompt)
    assert intent.description == prompt, f"description should be original prompt"
    _pass("description preserved")


if __name__ == "__main__":
    print("Running intent_extractor tests...")
    test_horror_genre_detection()
    test_platformer_genre_detection()
    test_generic_fallback()
    test_enemy_actor_extracted()
    test_collectible_detected()
    test_door_relationship()
    test_enemy_patrol_mechanic()
    test_menu_ui_request()
    test_hide_mechanic()
    test_feature_name_derived()
    test_player_movement_always_present()
    test_description_preserved()
    print("All intent_extractor tests passed.")
