"""Detect Play In Editor, so editor-only commands fail honestly instead of quietly.

UE4.27's editor scripting libraries refuse to work during PIE. They log
"LogEditorScripting: Error: The Editor is currently in a play mode." and then
hand back an empty result rather than raising. Handlers that trusted them
returned success with zero actors, so a client was told the level was empty when
it was simply being played.

That is worse than an error. An error is actionable; an empty success is a lie
the caller has no way to detect.

Detection, verified against a live 4.27.2 editor in and out of PIE:

    state        get_editor_world()   get_game_world()
    in PIE       None                 the PIE world
    not in PIE   the editor world     None

get_game_world() is the positive signal and is preferred. get_editor_world()
returning None is the corroborating one, and also catches the rarer case where
the editor has no world at all (during a map load, for instance).
"""

from typing import Any, Dict, Optional


def is_in_play_mode() -> bool:
    """True when a PIE or Simulate session is running.

    Never raises. If the state cannot be determined, returns False so a
    detection problem cannot block ordinary editor work.
    """
    try:
        import unreal
    except Exception:
        return False

    library = getattr(unreal, "EditorLevelLibrary", None)
    if library is None:
        return False

    # Positive signal: a game world only exists while playing.
    try:
        if hasattr(library, "get_game_world"):
            if library.get_game_world() is not None:
                return True
    except Exception:
        pass

    # Corroborating signal: the editor world disappears from scripting during PIE.
    try:
        if hasattr(library, "get_editor_world"):
            if library.get_editor_world() is None:
                return True
    except Exception:
        pass

    return False


def editor_world_available() -> bool:
    """True when the editor world can be reached by editor scripting."""
    try:
        import unreal
        library = getattr(unreal, "EditorLevelLibrary", None)
        if library is None or not hasattr(library, "get_editor_world"):
            return True
        return library.get_editor_world() is not None
    except Exception:
        return True


def play_mode_error(command: str) -> Dict[str, Any]:
    """The response an editor-only command returns while PIE is running."""
    return {
        "success": False,
        "data": {"in_play_mode": True},
        "error": (
            "'{cmd}' needs the editor world, and the editor is in play mode. "
            "UE4.27 editor scripting is unavailable during PIE, and would "
            "otherwise return empty results that look like an empty level. "
            "Stop PIE with gameplay_pie_stop, or use the pie_agent_* tools, "
            "which are built to run during play."
        ).format(cmd=command),
    }


# Commands that keep working while PIE is running.
#
# This is an allowlist rather than a list of editor-only commands on purpose. A
# new tool added later is guarded by default: it fails with a clear message
# during PIE instead of silently returning empty results, which is the failure
# this guard exists to prevent. If a new command genuinely works during play,
# add it here deliberately.
#
# It lives here rather than in router.py so it can be imported and tested
# without UE4: router.py imports handlers, and those import `unreal` at module
# level.
PIE_SAFE_PREFIXES = (
    "pie_agent_",        # built for runtime, meaningless outside PIE
    "job_",              # background job bookkeeping, no editor world
    "bridge_",           # listener diagnostics
    "project_index_",    # reads the on-disk index, not the world
)

PIE_SAFE_COMMANDS = frozenset({
    # Session control and observation
    "gameplay_pie_start", "gameplay_pie_stop", "gameplay_pie_status",
    "gameplay_telemetry_snapshot", "gameplay_run_acceptance_tests",
    # Diagnostics and transport
    "test_connection", "ping", "help", "ue_logs",
    "clear_output_log", "restart_listener", "refresh_tools",
    # The escape hatch must never be blocked: it is how you diagnose the guard.
    "python_proxy",
    # Runtime-only effects, which are the point during play
    "console_effect", "camera_shake_play",
    # Builds run out of process
    "cpp_build", "cpp_class_create",
    # Profiling that drives console commands rather than editor scripting.
    # Capturing while playing is the normal way to profile, so these stay live.
    "optimization_tool_catalog",
    "optimization_run_console_checklist",
    "optimization_viewmode_capture_set",
    "optimization_gpu_capture",
    "optimization_cpu_capture",
    "optimization_rendering_capture",
    "optimization_memory_streaming_capture",
    "optimization_stat_capture",
    "optimization_profilegpu_capture",
    "optimization_insights_trace_start",
    "optimization_insights_trace_stop",
    "optimization_insights_trace_summary",
    "optimization_camera_path_profile",
    "optimization_visual_logger_ingest",
})


def is_pie_safe(command: str) -> bool:
    """True when a command is expected to work during Play In Editor."""
    if command in PIE_SAFE_COMMANDS:
        return True
    return any(command.startswith(prefix) for prefix in PIE_SAFE_PREFIXES)


def guard_editor_command(command: str) -> Optional[Dict[str, Any]]:
    """Return an error response if this command cannot run now, else None.

    PIE-safe commands are never guarded.
    """
    if is_pie_safe(command):
        return None
    if is_in_play_mode():
        return play_mode_error(command)
    return None
