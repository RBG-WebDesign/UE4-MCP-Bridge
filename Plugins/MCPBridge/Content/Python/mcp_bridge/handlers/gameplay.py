"""Gameplay handler: PIE control, telemetry, acceptance test runner.

Commands registered in router.py:
    gameplay_pie_start            -- launch PIE and wait until ready
    gameplay_pie_stop             -- end PIE session
    gameplay_telemetry_snapshot   -- capture current runtime state
    gameplay_run_acceptance_tests -- run predicates against live PIE
"""
from __future__ import annotations
import traceback
from typing import Any, Dict, List

from mcp_bridge.generation import pie_harness
from mcp_bridge.generation import telemetry_capture as tc


def handle_pie_start(params: Dict[str, Any]) -> Dict[str, Any]:
    """Launch PIE and wait until the session is ready.

    Params:
        level_path (str, optional): Content path to load before PIE.
    """
    try:
        level_path = params.get("level_path")
        ok = pie_harness.launch_pie(level_path=level_path)
        if not ok:
            return {"success": False, "data": {}, "error": "Failed to call play_in_editor()"}
        ready = pie_harness.wait_for_pie_ready()
        if not ready:
            return {
                "success": False,
                "data": {},
                "error": "PIE did not become ready within 30s",
            }
        return {"success": True, "data": {"status": "pie_ready"}}
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_pie_stop(params: Dict[str, Any]) -> Dict[str, Any]:
    """End the current PIE session."""
    try:
        ok = pie_harness.stop_pie()
        if ok:
            return {"success": True, "data": {"status": "pie_stopped"}}
        return {"success": False, "data": {"status": "stop_failed"}, "error": "stop_pie() returned False"}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_telemetry_snapshot(params: Dict[str, Any]) -> Dict[str, Any]:
    """Capture current PIE runtime state."""
    try:
        pie_world = tc.get_pie_world()
        if pie_world is None:
            return {"success": False, "data": {}, "error": "PIE is not running"}
        frame = tc.snapshot(pie_world)
        return {
            "success": True,
            "data": {
                "log_lines": frame.log_lines_since_last,
                "possessed_pawn_class": frame.possessed_pawn_class,
                "ai_controller_states": frame.ai_controller_states,
                "visible_widgets": frame.visible_widgets,
                "fps": frame.fps,
                "pie_world_name": frame.pie_world_name,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_run_acceptance_tests(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run acceptance test predicates against the live PIE session.

    Params:
        tests (List[str]): Predicate strings.
        timeout_seconds (float, optional): Per-predicate timeout override.
    """
    try:
        raw_tests: List[str] = params.get("tests", [])
        if not raw_tests:
            return {"success": False, "data": {}, "error": "No tests provided"}
        timeout_override = params.get("timeout_seconds")
        specs = pie_harness.parse_test_specs(raw_tests)
        if timeout_override is not None:
            for spec in specs:
                spec.timeout_seconds = float(timeout_override)
        results = pie_harness.run_assertions(specs)
        total = len(results)
        passed = sum(1 for r in results if r.passed)
        return {
            "success": True,
            "data": {
                "total": total,
                "passed": passed,
                "failed": total - passed,
                "results": [
                    {"predicate": r.predicate, "target": r.target, "passed": r.passed, "observed": r.observed}
                    for r in results
                ],
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}
