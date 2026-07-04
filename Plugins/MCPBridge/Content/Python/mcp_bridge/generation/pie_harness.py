"""PIE session lifecycle and acceptance test runner.

launch_pie() / stop_pie() manage the session.
wait_for_pie_ready() polls the log for the UE4.27 PIE-ready marker.
run_assertions() evaluates PIETestSpec predicates against live runtime state.

All unreal.* calls must be on the game thread.
"""
from __future__ import annotations
import time
from typing import List, Optional

from mcp_bridge.generation.spec_schema import PIETestSpec, AssertionResult, TelemetryFrame
from mcp_bridge.generation import telemetry_capture as tc

# The exact string UE4.27 emits to the output log when PIE is ready.
_PIE_READY_MARKER = "PIE: play in editor start"
_PIE_READY_TIMEOUT_S = 30.0
_PIE_POLL_INTERVAL_S = 0.5


def launch_pie(level_path: Optional[str] = None) -> bool:
    """Launch a PIE session. Resets log cursor. Does NOT block until ready.

    If level_path is provided, loads that level before starting PIE.
    """
    try:
        import unreal
        if level_path:
            unreal.EditorLevelLibrary.load_level(level_path)
        tc.reset_log_cursor()

        # Primary: AutoPIEHelper (C++ deferred tick, real PIE with PlayerController).
        # Defers RequestPlaySession to next engine tick so this handler can return
        # and the game thread unblocks before PIE initializes.
        try:
            unreal.AutoPIEHelper.start_pie()
            return True
        except (AttributeError, Exception):
            pass

        # Fallback 1: LevelEditorSubsystem (UE5+)
        try:
            subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
            if subsystem is not None:
                subsystem.play_in_editor(in_editor=True)
                return True
        except (AttributeError, Exception):
            pass

        # Fallback 2: editor_play_simulate (SIE, no PlayerController)
        try:
            unreal.EditorLevelLibrary.editor_play_simulate()
            return True
        except (AttributeError, Exception):
            pass

        return False
    except Exception:
        return False


def stop_pie() -> bool:
    """End the current PIE session."""
    try:
        import unreal

        # Primary: AutoPIEHelper (C++ deferred tick)
        try:
            unreal.AutoPIEHelper.stop_pie()
            return True
        except (AttributeError, Exception):
            pass

        # Fallback 1: LevelEditorSubsystem (UE5+)
        try:
            subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
            if subsystem is not None:
                subsystem.request_end_play_map()
                return True
        except (AttributeError, Exception):
            pass

        # Fallback 2: editor_end_play (UE4.27)
        try:
            unreal.EditorLevelLibrary.editor_end_play()
            return True
        except (AttributeError, Exception):
            pass

        return False
    except Exception:
        return False


def wait_for_pie_ready(timeout_s: float = _PIE_READY_TIMEOUT_S) -> bool:
    """Poll the log file for the PIE-ready marker.

    UE4.27 emits "PIE: play in editor start" when PIE is ready.
    Returns True if found within timeout_s, False otherwise.

    Note: time.sleep() is used here. Since this function is called from a
    handler that executes on the game thread via slate post-tick callback,
    the sleep will block the editor UI. This is acceptable for Phase 1
    where PIE start is a synchronous command. A non-blocking tick-based
    approach should be considered if editor responsiveness becomes an issue.
    """
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        lines = tc.read_new_log_lines()
        for line in lines:
            if _PIE_READY_MARKER in line:
                return True
        time.sleep(_PIE_POLL_INTERVAL_S)
    return False


def _check_predicate(
    spec: PIETestSpec,
    frame: TelemetryFrame,
    all_lines: List[str],
) -> AssertionResult:
    """Evaluate one PIETestSpec against telemetry and accumulated log lines."""
    p = spec.predicate
    target = spec.target or ""

    if p == "log_contains":
        passed = any(target in line for line in all_lines)
        return AssertionResult(predicate=p, passed=passed, observed=f"found={passed}", target=target, raw=f"log_contains:{target}")

    if p == "pawn_possessed":
        pawn_class = frame.possessed_pawn_class or ""
        passed = target in pawn_class
        return AssertionResult(predicate=p, passed=passed, observed=pawn_class or "None", target=target, raw=f"pawn_possessed:{target}")

    if p == "widget_visible":
        # Primary: runtime widget list
        passed = any(target in w for w in frame.visible_widgets)
        # Fallback: log convention [WidgetVisible:<name>]
        if not passed:
            marker = f"[WidgetVisible:{target}]"
            passed = any(marker in line for line in all_lines)
        return AssertionResult(
            predicate=p, passed=passed,
            observed=f"visible_widgets={frame.visible_widgets}",
            target=target, raw=f"widget_visible:{target}",
        )

    if p == "ai_state":
        expected = spec.expected or ""
        matching = {k: v for k, v in frame.ai_controller_states.items() if target in k}
        passed = any(expected in v for v in matching.values())
        observed = str(matching) if matching else "no_matching_actors"
        return AssertionResult(predicate=p, passed=passed, observed=observed, target=target, raw=f"ai_state:{target}:{expected}")

    if p == "survive":
        fatal_markers = ["Fatal:", "Error: (Assertion failed)"]
        crash_lines = [l for l in all_lines if any(m in l for m in fatal_markers)]
        passed = len(crash_lines) == 0
        observed = crash_lines[0] if crash_lines else "no_crash"
        return AssertionResult(predicate=p, passed=passed, observed=observed, target=None, raw=f"survive:{spec.timeout_seconds}")

    return AssertionResult(predicate=p, passed=False, observed=f"unknown_predicate:{p}", target=target, raw=str(spec))


def run_assertions(tests: List[PIETestSpec]) -> List[AssertionResult]:
    """Evaluate predicates against the live PIE session.

    Must be called after wait_for_pie_ready() returns True.
    """
    pie_world = tc.get_pie_world()
    if pie_world is None:
        return [
            AssertionResult(predicate=t.predicate, passed=False, observed="PIE not running", target=t.target, raw="")
            for t in tests
        ]

    all_log_lines: List[str] = tc.read_new_log_lines()
    results: List[AssertionResult] = []

    # Initialize frame before the loop so static analysis does not flag it as
    # potentially unbound (both branches always assign it, but Python cannot prove this).
    frame = tc.snapshot(pie_world)
    all_log_lines += frame.log_lines_since_last

    for spec in tests:
        if spec.predicate == "survive":
            time.sleep(spec.timeout_seconds)
            # snapshot() calls read_new_log_lines() internally; do not call it again
            # separately or the cursor will advance twice with no lines on the second read.
            frame = tc.snapshot(pie_world)
            all_log_lines += frame.log_lines_since_last
        else:
            frame = tc.snapshot(pie_world)
            all_log_lines += frame.log_lines_since_last

        results.append(_check_predicate(spec, frame, all_log_lines))

    return results


def parse_test_specs(tests: List[str]) -> List[PIETestSpec]:
    """Convert shorthand strings into PIETestSpec objects."""
    return [PIETestSpec.from_string(t) for t in tests]
