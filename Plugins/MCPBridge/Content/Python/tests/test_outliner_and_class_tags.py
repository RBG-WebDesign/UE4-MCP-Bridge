"""Regression tests for two UE4.27 bugs found by live integration testing.

Plain Python, no UE4 required.

Run: python Plugins/MCPBridge/Content/Python/tests/test_outliner_and_class_tags.py

Both bugs shipped for a long time because each degraded quietly:
  - level_outliner raised KeyError on any folder nested two deep, so the whole
    call failed on a real project. Reproduced with "Cinematics/Credits/Opening".
  - blueprint_info swallowed a property lookup failure in a bare except and
    reported every Blueprint as parentless.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYTHON_ROOT = os.path.dirname(HERE)
if PYTHON_ROOT not in sys.path:
    sys.path.insert(0, PYTHON_ROOT)

from mcp_bridge.handlers.level import build_folder_tree  # noqa: E402
from mcp_bridge.handlers.blueprints import _class_name_from_tag  # noqa: E402

_passed = 0
_failed = 0


def check(name, condition, detail=""):
    global _passed, _failed
    if condition:
        print("  PASS  {}".format(name))
        _passed += 1
    else:
        print("  FAIL  {}{}".format(name, ": " + detail if detail else ""))
        _failed += 1


print("\noutliner tree and class tag parsing\n")

# --- build_folder_tree ------------------------------------------------------

check(
    "an empty outliner produces an empty tree",
    build_folder_tree({}) == {},
)

single = build_folder_tree({"Lights": ["A", "B"]})
check(
    "a single-level folder carries its actor count",
    single.get("Lights", {}).get("_actor_count") == 2,
    repr(single),
)

# The exact shape that used to raise KeyError: 'Credits'.
nested = build_folder_tree({"Cinematics/Credits/Opening": ["TitleCard"]})
check(
    "a three-deep folder does not raise",
    "Cinematics" in nested,
    repr(nested),
)
check(
    "intermediate folders get a zero count, not the leaf's",
    nested["Cinematics"]["_actor_count"] == 0,
    repr(nested["Cinematics"]),
)
leaf = nested["Cinematics"]["_children"]["Credits"]["_children"]["Opening"]
check(
    "the leaf of a nested path carries the actor count",
    leaf["_actor_count"] == 1,
    repr(leaf),
)

# Siblings under a shared parent must not clobber each other.
siblings = build_folder_tree({
    "Cinematics/Credits/Opening": ["A"],
    "Cinematics/Credits/Closing": ["B", "C"],
    "Cinematics/Cutscenes": ["D"],
})
credits = siblings["Cinematics"]["_children"]["Credits"]["_children"]
check(
    "sibling leaves keep their own counts",
    credits["Opening"]["_actor_count"] == 1 and credits["Closing"]["_actor_count"] == 2,
    repr(credits),
)
check(
    "a parent with both children and its own siblings still resolves",
    set(siblings["Cinematics"]["_children"].keys()) == {"Credits", "Cutscenes"},
    repr(siblings["Cinematics"]["_children"].keys()),
)

# A folder that is both a leaf and a parent.
mixed = build_folder_tree({"Props": ["A"], "Props/Small": ["B"]})
check(
    "a folder that is both leaf and parent keeps its own count",
    mixed["Props"]["_actor_count"] == 1
    and mixed["Props"]["_children"]["Small"]["_actor_count"] == 1,
    repr(mixed),
)

check(
    "leading and doubled separators do not create empty nodes",
    "" not in build_folder_tree({"/Props//Small": ["A"]}),
)

# --- _class_name_from_tag ---------------------------------------------------

check(
    "an engine class tag reduces to the bare name",
    _class_name_from_tag("Class'/Script/Engine.Actor'") == "Actor",
    _class_name_from_tag("Class'/Script/Engine.Actor'"),
)
check(
    "a blueprint generated class tag reduces to the bare name",
    _class_name_from_tag(
        "BlueprintGeneratedClass'/Game/BP/BP_Thing.BP_Thing_C'"
    ) == "BP_Thing_C",
)
check(
    "a plain object path reduces to the bare name",
    _class_name_from_tag("/Script/Engine.Pawn") == "Pawn",
)
check(
    "an already-bare name is returned unchanged",
    _class_name_from_tag("Character") == "Character",
)
check(
    "surrounding whitespace is ignored",
    _class_name_from_tag("  Class'/Script/Engine.Actor'  ") == "Actor",
)

print("\n{} passed, {} failed\n".format(_passed, _failed))
sys.exit(1 if _failed else 0)
