"""Tests for the Play In Editor guard. Plain Python, no UE4 required.

Run: python Plugins/MCPBridge/Content/Python/tests/test_pie_guard.py

The guard exists because UE4.27 editor scripting does not raise during PIE. It
logs "The Editor is currently in a play mode" and returns nothing, so
level_actors answered success with zero actors while a level full of actors was
being played. An empty success is worse than an error: the caller cannot tell it
apart from an empty level.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYTHON_ROOT = os.path.dirname(HERE)
if PYTHON_ROOT not in sys.path:
    sys.path.insert(0, PYTHON_ROOT)

from mcp_bridge.utils import editor_state  # noqa: E402

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


print("\nPIE guard\n")

# --- detection, with a stubbed unreal module ------------------------------

class _FakeLibrary(object):
    def __init__(self, editor_world, game_world):
        self._editor = editor_world
        self._game = game_world

    def get_editor_world(self):
        return self._editor

    def get_game_world(self):
        return self._game


class _FakeUnreal(object):
    def __init__(self, library):
        self.EditorLevelLibrary = library


def with_fake(editor_world, game_world):
    """Install a stub unreal module and report is_in_play_mode()."""
    fake = _FakeUnreal(_FakeLibrary(editor_world, game_world))
    real = sys.modules.get("unreal")
    sys.modules["unreal"] = fake
    try:
        return editor_state.is_in_play_mode()
    finally:
        if real is None:
            del sys.modules["unreal"]
        else:
            sys.modules["unreal"] = real


WORLD = object()

check(
    "not playing: editor world present, no game world",
    with_fake(WORLD, None) is False,
)
check(
    "playing: game world present, editor world gone",
    with_fake(None, WORLD) is True,
)
check(
    "playing: game world present even if an editor world is also reported",
    with_fake(WORLD, WORLD) is True,
)
check(
    "no editor world at all is treated as unavailable",
    with_fake(None, None) is True,
    "a missing editor world must not be reported as a usable editor",
)


class _Exploding(object):
    def get_editor_world(self):
        raise RuntimeError("boom")

    def get_game_world(self):
        raise RuntimeError("boom")


def with_exploding():
    real = sys.modules.get("unreal")
    sys.modules["unreal"] = _FakeUnreal(_Exploding())
    try:
        return editor_state.is_in_play_mode()
    finally:
        if real is None:
            del sys.modules["unreal"]
        else:
            sys.modules["unreal"] = real


check(
    "a detection failure does not block ordinary editor work",
    with_exploding() is False,
    "detection errors must fail open, not lock the bridge out of the editor",
)

# --- the error response ---------------------------------------------------

err = editor_state.play_mode_error("level_actors")
check("the guard reports failure, not empty success", err["success"] is False)
check("the response flags play mode in data", err["data"].get("in_play_mode") is True)
check("the error names the offending command", "level_actors" in err["error"])
check("the error says how to proceed", "gameplay_pie_stop" in err["error"])
check("the error points at the runtime tools", "pie_agent_" in err["error"])

# --- the allowlist --------------------------------------------------------

from mcp_bridge.utils.editor_state import is_pie_safe  # noqa: E402

for cmd in [
    "pie_agent_observe", "pie_agent_status",
    "gameplay_pie_stop", "gameplay_telemetry_snapshot",
    "python_proxy", "test_connection", "ue_logs",
    "job_status", "bridge_command_manifest",
    "optimization_stat_capture", "optimization_gpu_capture",
]:
    check("PIE-safe: {}".format(cmd), is_pie_safe(cmd) is True)

for cmd in [
    "level_actors", "level_outliner", "level_save",
    "actor_spawn", "actor_delete",
    "project_info", "material_create", "blueprint_create",
    "optimization_scene_audit", "optimization_asset_audit",
]:
    check("guarded during PIE: {}".format(cmd), is_pie_safe(cmd) is False)

check(
    "an unknown future command is guarded by default",
    is_pie_safe("some_tool_added_next_year") is False,
    "the allowlist must fail safe, or a new editor tool silently lies again",
)

print("\n{} passed, {} failed\n".format(_passed, _failed))
sys.exit(1 if _failed else 0)
