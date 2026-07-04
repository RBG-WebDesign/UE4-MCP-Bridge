# intent_extractor.py -- Parse a natural language prompt into an IntentMap.
# Uses keyword dictionaries and regex. No AI calls. Deterministic.
from __future__ import annotations

import re
from typing import Dict, List, Optional, Tuple

from mcp_bridge.generation.prompt_to_spec import GENRE_KEYWORDS, detect_genre as _detect_genre
from mcp_bridge.generation.spec_schema import (
    ActorIntent,
    IntentMap,
    MechanicIntent,
    RelationshipIntent,
)


# ---------------------------------------------------------------------------
# Actor keyword tables
# role: list of trigger words -> (canonical actor name, role string)
# ---------------------------------------------------------------------------

# Each entry: (keyword, canonical_name, role)
_ACTOR_KEYWORDS: List[Tuple[str, str, str]] = [
    # player
    ("player", "player", "player"),
    ("character", "player", "player"),
    ("hero", "player", "player"),
    # enemy
    ("monster", "monster", "enemy"),
    ("enemy", "enemy", "enemy"),
    ("zombie", "zombie", "enemy"),
    ("ghost", "ghost", "enemy"),
    ("creature", "creature", "enemy"),
    # interactable
    ("door", "door", "interactable"),
    ("gate", "gate", "interactable"),
    ("chest", "chest", "interactable"),
    ("lever", "lever", "interactable"),
    ("switch", "switch", "interactable"),
    ("button", "button", "interactable"),
    # collectible
    ("coin", "coin", "collectible"),
    ("gem", "gem", "collectible"),
    ("key", "key", "collectible"),
    ("collectible", "collectible", "collectible"),
    ("pickup", "pickup", "collectible"),
    ("star", "star", "collectible"),
    # equipment
    ("flashlight", "flashlight", "equipment"),
    ("weapon", "weapon", "equipment"),
    ("torch", "torch", "equipment"),
    # environment
    ("light", "light", "environment"),
    ("platform", "platform", "environment"),
    ("wall", "wall", "environment"),
    ("trap", "trap", "environment"),
    ("spike", "spike", "environment"),
]


# ---------------------------------------------------------------------------
# Mechanic keyword tables
# Each entry: (keyword_phrase, mechanic_name)
# Multi-word phrases must come before single-word ones so they match first.
# ---------------------------------------------------------------------------

_MECHANIC_KEYWORDS: List[Tuple[str, str]] = [
    # collect_item
    ("pick up", "collect_item"),
    ("collect", "collect_item"),
    ("coin", "collect_item"),
    ("gem", "collect_item"),
    ("pickup", "collect_item"),
    ("gather", "collect_item"),
    ("collectible", "collect_item"),
    ("star", "collect_item"),
    # door_trigger
    ("opens when", "door_trigger"),
    ("locked door", "door_trigger"),
    ("door", "door_trigger"),
    ("gate", "door_trigger"),
    ("unlock", "door_trigger"),
    # enemy_patrol
    ("patrolling", "enemy_patrol"),
    ("patrol", "enemy_patrol"),
    ("guard", "enemy_patrol"),
    ("wander", "enemy_patrol"),
    ("roam", "enemy_patrol"),
    ("chasing", "enemy_patrol"),
    ("chase", "enemy_patrol"),
    ("pursue", "enemy_patrol"),
    ("pursuit", "enemy_patrol"),
    # hide_from_enemy
    ("avoid detection", "hide_from_enemy"),
    ("hide from", "hide_from_enemy"),
    ("hiding", "hide_from_enemy"),
    ("stealth", "hide_from_enemy"),
    ("sneak", "hide_from_enemy"),
    ("hide", "hide_from_enemy"),
    ("crouch", "hide_from_enemy"),
    # main_menu
    ("title screen", "main_menu"),
    ("start screen", "main_menu"),
    ("main menu", "main_menu"),
    # game_over
    ("lose screen", "game_over"),
    ("death screen", "game_over"),
    ("game over", "game_over"),
    ("retry", "game_over"),
]


# ---------------------------------------------------------------------------
# UI request keyword tables
# Each entry: (keyword_phrase, ui_name)
# ---------------------------------------------------------------------------

_UI_KEYWORDS: List[Tuple[str, str]] = [
    ("title screen", "main_menu"),
    ("start screen", "main_menu"),
    ("main menu", "main_menu"),
    ("pause screen", "pause_menu"),
    ("pause menu", "pause_menu"),
    ("game over screen", "game_over"),
    ("death screen", "game_over"),
    ("lose screen", "game_over"),
    ("settings menu", "settings"),
    ("settings screen", "settings"),
    ("hud", "hud"),
    ("inventory", "inventory_ui"),
    ("score", "hud"),
]


# ---------------------------------------------------------------------------
# Genre defaults
# ---------------------------------------------------------------------------

_GENRE_MECHANIC_DEFAULTS: Dict[str, List[str]] = {
    "horror": ["player_movement", "enemy_patrol"],
    "platformer": ["player_movement"],
    "inventory": ["player_movement", "collect_item"],
    "menu_system": ["main_menu"],
    "generic": ["player_movement"],
    "puzzle_fighter": [],
}

_GENRE_UI_DEFAULTS: Dict[str, List[str]] = {
    "horror": ["hud"],
    "platformer": ["hud"],
    "inventory": ["hud", "inventory_ui"],
    "menu_system": ["main_menu", "pause_menu", "settings"],
    "generic": [],
    "puzzle_fighter": [],
}

_GENRE_FEATURE_NAME: Dict[str, str] = {
    "horror": "Horror",
    "platformer": "Platformer",
    "inventory": "Inventory",
    "menu_system": "MenuSystem",
    "generic": "GeneratedFeature",
    "puzzle_fighter": "PuzzleFighter",
}

# Genres that do NOT get player_movement injected automatically.
_NO_AUTO_PLAYER_MOVEMENT = {"menu_system", "puzzle_fighter"}


# ---------------------------------------------------------------------------
# Relationship regex patterns
# ---------------------------------------------------------------------------

_VERBS = r"(opens|closes|unlocks|activates|spawns|destroys|appears)"

_REL_PATTERNS = [
    # "a door that opens when all coins are collected"
    # "the gate unlocks when the key is picked up"
    re.compile(
        r"(?:the\s+|a\s+)?(\w+)\s+(?:that\s+)?" + _VERBS + r"\s+when\s+(.+?)(?:\.|$)",
        re.IGNORECASE,
    ),
    # "when all coins are collected, the door opens"
    re.compile(
        r"when\s+(.+?),\s+(?:the\s+|a\s+)?(\w+)\s+" + _VERBS,
        re.IGNORECASE,
    ),
]


# ---------------------------------------------------------------------------
# Extraction helpers
# ---------------------------------------------------------------------------

def _extract_actors(lower: str) -> List[ActorIntent]:
    """Return ActorIntent list from keywords found in the prompt."""
    seen_names: Dict[str, ActorIntent] = {}
    for keyword, canonical, role in _ACTOR_KEYWORDS:
        if keyword in lower:
            if canonical not in seen_names:
                seen_names[canonical] = ActorIntent(name=canonical, role=role)
    return list(seen_names.values())


def _ensure_player(actors: List[ActorIntent]) -> None:
    """Add player actor if no player-role actor exists."""
    has_player = any(a.role == "player" for a in actors)
    if not has_player:
        actors.insert(0, ActorIntent(name="player", role="player"))


def _extract_mechanics(lower: str, genre: str) -> List[MechanicIntent]:
    """Return MechanicIntent list from keyword matches plus genre defaults."""
    seen: Dict[str, MechanicIntent] = {}

    # Add genre defaults first.
    for mechanic_name in _GENRE_MECHANIC_DEFAULTS.get(genre, ["player_movement"]):
        if mechanic_name not in seen:
            seen[mechanic_name] = MechanicIntent(name=mechanic_name)

    # Add prompt-matched mechanics.
    for keyword, mechanic_name in _MECHANIC_KEYWORDS:
        if keyword in lower and mechanic_name not in seen:
            seen[mechanic_name] = MechanicIntent(name=mechanic_name)

    # Always add player_movement unless genre opts out.
    if genre not in _NO_AUTO_PLAYER_MOVEMENT and "player_movement" not in seen:
        seen["player_movement"] = MechanicIntent(name="player_movement")

    return list(seen.values())


def _extract_relationships(prompt: str) -> List[RelationshipIntent]:
    """Return RelationshipIntent list from regex pattern matches."""
    relationships: List[RelationshipIntent] = []
    lower = prompt.lower()

    # Pattern 1: "<subject> <verb> when <trigger>"
    pat1 = _REL_PATTERNS[0]
    for m in pat1.finditer(lower):
        subject = m.group(1).strip()
        verb = m.group(2).strip()
        trigger = m.group(3).strip()
        relationships.append(RelationshipIntent(subject=subject, verb=verb, trigger=trigger))

    # Pattern 2: "when <trigger>, <subject> <verb>"
    pat2 = _REL_PATTERNS[1]
    for m in pat2.finditer(lower):
        trigger = m.group(1).strip()
        subject = m.group(2).strip()
        verb = m.group(3).strip()
        relationships.append(RelationshipIntent(subject=subject, verb=verb, trigger=trigger))

    return relationships


def _extract_ui_requests(lower: str, genre: str) -> List[str]:
    """Return UI request names from keyword matches plus genre defaults."""
    seen: Dict[str, bool] = {}

    # Add genre UI defaults first.
    for ui_name in _GENRE_UI_DEFAULTS.get(genre, []):
        seen[ui_name] = True

    # Add prompt-matched UI requests.
    for keyword, ui_name in _UI_KEYWORDS:
        if keyword in lower and ui_name not in seen:
            seen[ui_name] = True

    return list(seen.keys())


def _derive_feature_name(genre: str, prompt: str) -> str:
    """Derive a PascalCase feature name from genre, with prompt fallback."""
    name = _GENRE_FEATURE_NAME.get(genre)
    if name:
        return name
    # Fallback: title-case the first two words of the prompt.
    words = prompt.strip().split()[:2]
    return "".join(w.capitalize() for w in words) or "GeneratedFeature"


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def extract_intent(prompt: str) -> IntentMap:
    """Parse a natural language prompt into an IntentMap.

    Uses keyword matching and regex only. No external AI calls.
    """
    lower = prompt.lower()
    genre = _detect_genre(prompt)

    actors = _extract_actors(lower)
    _ensure_player(actors)

    mechanics = _extract_mechanics(lower, genre)
    relationships = _extract_relationships(prompt)
    ui_requests = _extract_ui_requests(lower, genre)
    feature_name = _derive_feature_name(genre, prompt)

    return IntentMap(
        genre=genre,
        feature_name=feature_name,
        description=prompt,
        actors=actors,
        mechanics=mechanics,
        relationships=relationships,
        ui_requests=ui_requests,
    )
