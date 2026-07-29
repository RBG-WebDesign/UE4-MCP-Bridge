"""Gameplay handler: PIE control, telemetry, acceptance test runner.

Commands registered in router.py:
    gameplay_pie_start            -- launch PIE and wait until ready
    gameplay_pie_stop             -- end PIE session
    gameplay_pie_status           -- report current PIE session state
    gameplay_telemetry_snapshot   -- capture current runtime state
    gameplay_run_acceptance_tests -- run predicates against live PIE
"""
from __future__ import annotations
import traceback
from typing import Any, Dict, List

from mcp_bridge.generation import pie_harness
from mcp_bridge.generation import telemetry_capture as tc
from mcp_bridge.generation import gameplay_framework as gf


def handle_pie_start(params: Dict[str, Any]) -> Dict[str, Any]:
    """Request a PIE session. Returns as soon as the request is queued.

    This does NOT wait for PIE to be ready, and cannot.

    launch_pie defers the play request to the next engine tick precisely so this
    handler can return and release the game thread. But handlers run ON the game
    thread, so any wait here blocks the tick that starts PIE: the session cannot
    begin until this function returns. The previous version waited 30 seconds for
    a world that was structurally unable to appear, then reported failure, and
    PIE started the moment the handler gave the thread back.

    Poll gameplay_pie_status to find out when the session is live.

    Params:
        level_path (str, optional): Content path to load before PIE.
    """
    try:
        level_path = params.get("level_path")
        ok = pie_harness.launch_pie(level_path=level_path)
        if not ok:
            return {
                "success": False,
                "data": {},
                "error": "Failed to request PIE. Neither AutoPIEHelper.start_pie() nor editor_play_simulate() was available.",
            }
        already = tc.get_pie_world()
        return {
            "success": True,
            "data": {
                "status": "pie_running" if already is not None else "pie_starting",
                "is_running": already is not None,
                "pie_world": already.get_name() if already is not None else None,
                "next": "Poll gameplay_pie_status until is_running is true. PIE cannot start while this call is in flight, because handlers hold the game thread.",
            },
        }
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


def handle_pie_status(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return current PIE state and lightweight runtime context."""
    try:
        pie_world = tc.get_pie_world()
        if pie_world is None:
            return {
                "success": True,
                "data": {
                    "status": "stopped",
                    "is_running": False,
                    "pie_world_name": None,
                },
            }

        frame = tc.snapshot(pie_world)
        return {
            "success": True,
            "data": {
                "status": "running",
                "is_running": True,
                "possessed_pawn_class": frame.possessed_pawn_class,
                "visible_widgets": frame.visible_widgets,
                "ai_controller_count": len(frame.ai_controller_states),
                "fps": frame.fps,
                "pie_world_name": frame.pie_world_name,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


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


def handle_gameplay_framework_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create or refresh the reusable gameplay Blueprint framework.

    Params:
        asset_dir (str, optional): Content folder for generated assets.
        replace_existing (bool, optional): Recreate existing framework assets.
    """
    try:
        return gf.create_gameplay_framework(params)
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_gameplay_actor_place(params: Dict[str, Any]) -> Dict[str, Any]:
    """Place one configured gameplay actor from the framework.

    Params:
        type (str): interactable_base, key_item_pickup, locked_door,
            objective_trigger, finish_zone, or level_state_manager.
        location (dict): {x, y, z}
        rotation (dict, optional): {pitch, yaw, roll}
        label (str, optional): World Outliner label.
    """
    try:
        return gf.place_gameplay_actor(params)
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_gameplay_graph_template(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return or apply a purpose-built gameplay graph template.

    Params:
        template (str): overlap_print, pickup_hide, locked_door_open,
            objective_trigger, input_event_debug, or finish_zone.
        graph_params (dict, optional): Template parameters.
        blueprint_path (str, optional): Blueprint asset to populate.
        apply (bool, optional): Apply the graph to blueprint_path.
    """
    try:
        import json as json_mod

        template = params.get("template")
        if not template:
            return {
                "success": True,
                "data": {
                    "catalog": gf.GRAPH_NODE_CATALOG,
                    "templates": [
                        "overlap_print",
                        "pickup_hide",
                        "locked_door_open",
                        "objective_trigger",
                        "input_event_debug",
                        "finish_zone",
                    ],
                },
            }

        graph = gf.build_graph_template(template, params.get("graph_params", {}))
        result: Dict[str, Any] = {
            "template": template,
            "catalog": gf.GRAPH_NODE_CATALOG,
            "graph_json": graph,
            "applied": False,
        }

        if params.get("apply"):
            import unreal

            bp_path = params.get("blueprint_path", "")
            if not bp_path:
                return {"success": False, "data": result, "error": "Missing blueprint_path for apply=true"}
            bp = unreal.EditorAssetLibrary.load_asset(bp_path)
            if bp is None:
                return {"success": False, "data": result, "error": f"Blueprint not found: {bp_path}"}
            lib = getattr(unreal, "BlueprintGraphBuilderLibrary", None)
            if lib is None:
                return {"success": False, "data": result, "error": "BlueprintGraphBuilderLibrary not available"}
            lib.build_blueprint_from_json(bp, json_mod.dumps(graph), True)
            from mcp_bridge import state

            should_compile = bool(params.get("compile")) if "compile" in params else state.safety_enabled("auto_compile_blueprints")
            should_save = bool(params.get("save")) if "save" in params else state.safety_enabled("auto_save_after_operations")
            try:
                if should_compile:
                    compile_lib = getattr(unreal, "BlueprintGraphBuilderLibrary", None)
                    compile_and_report = getattr(compile_lib, "compile_and_report", None) if compile_lib else None
                    if compile_and_report is not None:
                        compile_and_report(bp)
                    else:
                        unreal.EditorAssetLibrary.save_asset(bp_path, only_if_is_dirty=False)
                if should_save:
                    unreal.EditorAssetLibrary.save_asset(bp_path)
            except Exception:
                pass
            result["applied"] = True
            result["blueprint_path"] = bp_path
            result["compiled"] = should_compile
            result["saved"] = should_save

        return {"success": True, "data": result}
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_gameplay_whitebox_lighting(params: Dict[str, Any]) -> Dict[str, Any]:
    """Apply readable whitebox lighting defaults and clamp extreme point lights."""
    try:
        return gf.apply_whitebox_lighting(params)
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_gameplay_route_analyze(params: Dict[str, Any]) -> Dict[str, Any]:
    """Analyze route reachability between actor labels or world points."""
    try:
        return gf.analyze_route(params)
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_gameplay_validate(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run editor-side gameplay validation checks."""
    try:
        return gf.run_gameplay_validation(params)
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_gameplay_level_grammar(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return reusable level grammar templates."""
    try:
        return gf.get_level_grammar(params)
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_gameplay_design_memory(params: Dict[str, Any]) -> Dict[str, Any]:
    """Read or update the local design memory used by gameplay generation.

    Params:
        update (dict, optional): Top-level keys to merge into memory.
    """
    try:
        memory = gf.load_design_memory()
        update = params.get("update")
        if update:
            if not isinstance(update, dict):
                return {"success": False, "data": {}, "error": "update must be a dict"}
            memory.update(update)
            path = gf.save_design_memory(memory)
        else:
            path = gf.project_file_path(gf.DESIGN_MEMORY_RELATIVE_PATH)
        return {"success": True, "data": {"path": path, "memory": memory}}
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_gameplay_multi_screenshot(params: Dict[str, Any]) -> Dict[str, Any]:
    """Capture multiple editor screenshots from named camera viewpoints.

    Params:
        viewpoints (list): Each item accepts location and rotation, or
            actor_name plus distance when using viewport_focus.
        resolution (dict, optional): {width, height}
        filename_prefix (str, optional): Prefix for generated PNGs.
    """
    try:
        from mcp_bridge.handlers.viewport import (
            handle_viewport_camera,
            handle_viewport_focus,
            handle_viewport_screenshot,
        )

        viewpoints = params.get("viewpoints", [])
        if not isinstance(viewpoints, list) or not viewpoints:
            return {"success": False, "data": {}, "error": "viewpoints must be a non-empty list"}

        prefix = params.get("filename_prefix", "gameplay_qa")
        resolution = params.get("resolution", {"width": 1920, "height": 1080})
        captures = []
        for index, viewpoint in enumerate(viewpoints):
            if viewpoint.get("actor_name"):
                camera_result = handle_viewport_focus(
                    {
                        "actor_name": viewpoint["actor_name"],
                        "distance": viewpoint.get("distance", 700),
                    }
                )
            else:
                camera_result = handle_viewport_camera(
                    {
                        "location": viewpoint.get("location"),
                        "rotation": viewpoint.get("rotation"),
                        "fov": viewpoint.get("fov"),
                    }
                )
            if not camera_result.get("success"):
                captures.append({"index": index, "success": False, "error": camera_result.get("error")})
                continue
            shot_result = handle_viewport_screenshot(
                {
                    "filename": f"{prefix}_{index + 1:02d}.png",
                    "resolution": resolution,
                    "show_ui": params.get("show_ui", False),
                }
            )
            captures.append(
                {
                    "index": index,
                    "success": bool(shot_result.get("success")),
                    "camera": camera_result.get("data", {}),
                    "screenshot": shot_result.get("data", {}),
                    "error": shot_result.get("error"),
                }
            )

        failed = [c for c in captures if not c["success"]]
        return {
            "success": len(failed) == 0,
            "data": {"captures": captures, "failed": len(failed), "succeeded": len(captures) - len(failed)},
            "error": None if not failed else "One or more screenshots failed",
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}
