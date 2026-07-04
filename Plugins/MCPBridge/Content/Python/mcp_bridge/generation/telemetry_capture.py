"""PIE telemetry capture: log scraping, runtime state queries, class resolution cache.

All functions that call unreal.* must be called from the game thread.
This module is imported by pie_harness.py and (in Phase 4) by repair_engine.py.
"""
from __future__ import annotations
import os
from typing import Any, Dict, List, Optional

from mcp_bridge.generation.spec_schema import TelemetryFrame, ClassResolutionCache


# ---------------------------------------------------------------------------
# Log cursor
# ---------------------------------------------------------------------------

# Module-level cursor: byte offset into the log file after the last read.
_log_cursor: int = 0


def _get_log_path() -> str:
    """Return the absolute path to the current UE4 session log file.

    UE4 writes to <ProjectDir>/Saved/Logs/<ProjectName>.log.
    Returns empty string outside UE4 (unit test context).
    """
    try:
        import unreal
        log_dir = unreal.Paths.project_log_dir()
        project_name = unreal.SystemLibrary.get_game_name()
        return os.path.join(log_dir, f"{project_name}.log")
    except Exception:
        return ""


def reset_log_cursor() -> None:
    """Reset the cursor to the current end of the log file.

    Call this before launching PIE so assertions only see post-launch log lines.
    """
    global _log_cursor
    path = _get_log_path()
    if path and os.path.exists(path):
        _log_cursor = os.path.getsize(path)
    else:
        _log_cursor = 0


def read_new_log_lines() -> List[str]:
    """Return all log lines written to disk since the last call (or since reset_log_cursor).

    Uses a module-level byte-offset cursor. Thread-safe for single-threaded UE4 game thread use.
    """
    global _log_cursor
    path = _get_log_path()
    if not path or not os.path.exists(path):
        return []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            f.seek(_log_cursor)
            new_text = f.read()
            _log_cursor = f.tell()
        return [line for line in new_text.splitlines() if line.strip()]
    except Exception:
        return []


# ---------------------------------------------------------------------------
# PIE world helpers
# ---------------------------------------------------------------------------

def get_pie_world() -> Any:
    """Return the active PIE game world, or None if PIE is not running.

    Uses get_pie_worlds(False) (UE4.27 recommended) not get_editor_world(),
    which is documented as invalid during PIE mode.
    Falls back to get_game_world() for UE4.27 builds where get_pie_worlds
    is not available.
    """
    try:
        import unreal
        # Primary: get_pie_worlds returns list of active PIE worlds
        get_pie_worlds_fn = getattr(unreal.EditorLevelLibrary, "get_pie_worlds", None)
        if get_pie_worlds_fn is not None:
            worlds = get_pie_worlds_fn(False)
            if worlds:
                return worlds[0]
        # Fallback: get_game_world()
        get_game_world_fn = getattr(unreal.EditorLevelLibrary, "get_game_world", None)
        if get_game_world_fn is not None:
            return get_game_world_fn()
        return None
    except Exception:
        return None


# ---------------------------------------------------------------------------
# Runtime state queries (all require a live PIE world)
# ---------------------------------------------------------------------------

def get_possessed_pawn_class(pie_world: Any) -> Optional[str]:
    """Return the class name of the possessed pawn in PIE, or None."""
    try:
        import unreal
        pc = unreal.GameplayStatics.get_player_controller(pie_world, 0)
        if pc is None:
            return None
        pawn = pc.get_controlled_pawn()
        if pawn is None:
            return None
        return pawn.get_class().get_name()
    except Exception:
        return None


def get_visible_widget_classes(pie_world: Any) -> List[str]:
    """Return class names of all visible UUserWidget instances in PIE.

    Uses WidgetLibrary.get_all_widgets_of_class() (UE4.27 direct API).
    Positively includes only VISIBLE, HIT_TEST_INVISIBLE, and SELF_HIT_TEST_INVISIBLE
    states. HIDDEN and COLLAPSED are both excluded.
    Returns empty list if the API is unavailable.
    """
    try:
        import unreal
        widget_lib = getattr(unreal, "WidgetLibrary", None)
        if widget_lib is None:
            return []
        base_class = getattr(unreal, "UserWidget", None)
        if base_class is None:
            return []
        get_widgets_fn = getattr(widget_lib, "get_all_widgets_of_class", None)
        if get_widgets_fn is None:
            return []
        widgets = get_widgets_fn(pie_world, base_class, False)
        # Positively include only states that mean "visible" in UE4 SlateVisibility enum
        _VISIBLE_STATES = {
            unreal.SlateVisibility.VISIBLE,
            unreal.SlateVisibility.HIT_TEST_INVISIBLE,
            unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE,
        }
        visible: List[str] = []
        for w in widgets:
            try:
                if w.get_visibility() in _VISIBLE_STATES:
                    visible.append(w.get_class().get_name())
            except Exception:
                pass
        return visible
    except Exception:
        return []


def get_ai_controller_states(pie_world: Any) -> Dict[str, str]:
    """Return {actor_label: state_string} for AI controllers in PIE.

    Conservative Phase 1 probe: reports presence of controller, brain, and blackboard.
    Does NOT call get_active_task_name() as it is not verified available on
    BrainComponent in UE4.27 Python.
    """
    try:
        import unreal
        ai_cls = getattr(unreal, "AIController", None)
        if ai_cls is None:
            return {}
        controllers = unreal.GameplayStatics.get_all_actors_of_class(pie_world, ai_cls)
        states: Dict[str, str] = {}
        for ctrl in controllers:
            name = ctrl.get_actor_label()
            try:
                brain = getattr(ctrl, "get_editor_property", lambda p: None)("brain_component")
                if brain is None:
                    state = "no_brain"
                else:
                    bb = getattr(brain, "get_blackboard", None)
                    if bb is not None and bb() is not None:
                        state = "active_with_blackboard"
                    else:
                        state = "active"
            except Exception:
                state = "unknown"
            states[name] = state
        return states
    except Exception:
        return {}


# ---------------------------------------------------------------------------
# Snapshot
# ---------------------------------------------------------------------------

def _get_world_name(pie_world: Any) -> Optional[str]:
    """Return the world name string, or None on failure."""
    try:
        return pie_world.get_name()
    except Exception:
        return None


def snapshot(pie_world: Any) -> TelemetryFrame:
    """Capture current runtime state from the live PIE world."""
    return TelemetryFrame(
        log_lines_since_last=read_new_log_lines(),
        possessed_pawn_class=get_possessed_pawn_class(pie_world),
        ai_controller_states=get_ai_controller_states(pie_world),
        visible_widgets=get_visible_widget_classes(pie_world),
        fps=0.0,  # FPS via stats subsystem: deferred to Phase 1.1
        pie_world_name=_get_world_name(pie_world),
    )


# ---------------------------------------------------------------------------
# ClassResolutionCache
# ---------------------------------------------------------------------------

def build_class_cache(asset_paths: List[str]) -> ClassResolutionCache:
    """Build a ClassResolutionCache from a list of content paths.

    asset_paths: e.g. ["/Game/Generated/Gameplay/BP_Character"]
    """
    cache = ClassResolutionCache()
    for path in asset_paths:
        short_name = path.split("/")[-1]
        cache.class_paths[short_name] = path
    return cache
