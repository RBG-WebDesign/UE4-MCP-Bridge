"""Command router that maps command strings to handler functions."""

from typing import Any, Callable, Dict

# Import handlers
from mcp_bridge.handlers.system import (
    handle_ping,
    handle_test_connection,
    handle_python_proxy,
    handle_ue_logs,
    handle_bridge_status,
    handle_bridge_latest_notification,
    handle_bridge_copy_project_info,
    handle_bridge_clear_log,
    handle_bridge_command_manifest,
    handle_bridge_self_test,
    handle_restart_listener,
)
from mcp_bridge.handlers.project import (
    handle_project_info,
    handle_asset_list,
    handle_asset_info,
    handle_input_mapping_info,
    handle_asset_load_diagnostics,
    handle_asset_save_many,
    handle_project_enable_plugins,
)
from mcp_bridge.handlers.actors import (
    handle_actor_spawn,
    handle_actor_duplicate,
    handle_actor_delete,
    handle_actor_modify,
    handle_actor_organize,
    handle_actor_snap_to_socket,
    handle_batch_spawn,
    handle_placement_validate,
)
from mcp_bridge.handlers.level import (
    handle_level_actors,
    handle_level_save,
    handle_level_outliner,
)
from mcp_bridge.handlers.viewport import (
    handle_viewport_screenshot,
    handle_viewport_camera,
    handle_viewport_mode,
    handle_viewport_focus,
    handle_viewport_render_mode,
    handle_viewport_bounds,
    handle_viewport_fit,
    handle_viewport_look_at,
)
from mcp_bridge.handlers.materials import (
    handle_material_list,
    handle_material_info,
    handle_material_create,
    handle_material_apply,
)
from mcp_bridge.handlers.blueprints import (
    handle_blueprint_create,
    handle_blueprint_list,
    handle_blueprint_info,
    handle_blueprint_compile,
    handle_blueprint_document,
    handle_blueprint_build_from_json,
    handle_anim_blueprint_build_from_json,
    handle_widget_build_from_json,
)
from mcp_bridge.handlers.widgets import (
    handle_widget_lower_third_create,
    handle_widget_title_card_create,
    handle_widget_title_template,
)
from mcp_bridge.handlers.titles import (
    handle_title_controller_create,
    handle_title_manifest_adjust,
    handle_title_manifest_create,
    handle_title_manifest_from_reference,
    handle_title_manifest_validate,
    handle_title_render_compare,
    handle_title_sequence_bind,
    handle_title_widget_build_from_manifest,
)
from mcp_bridge.handlers.promptbrush import (
    handle_prompt_generate,
    handle_prompt_status,
    handle_prompt_spec_list,
)
from mcp_bridge.handlers.gameplay import (
    handle_pie_start,
    handle_pie_stop,
    handle_telemetry_snapshot,
    handle_run_acceptance_tests,
)
from mcp_bridge.handlers.effects import (
    handle_pp_volume_spawn,
    handle_pp_volume_modify,
    handle_pp_preset,
    handle_camera_shake_spawn,
    handle_camera_shake_play,
    handle_camera_shake_blueprint,
    handle_camera_shake_trigger,
    handle_console_effect,
)
from mcp_bridge.handlers.optimization import (
    handle_optimization_apply_low_risk_fixes,
    handle_optimization_asset_audit,
    handle_optimization_asset_cost_rank,
    handle_optimization_before_after_verify,
    handle_optimization_camera_path_profile,
    handle_optimization_cpu_capture,
    handle_optimization_fix_candidates,
    handle_optimization_generate_report,
    handle_optimization_gpu_capture,
    handle_optimization_insights_trace_start,
    handle_optimization_insights_trace_stop,
    handle_optimization_insights_trace_summary,
    handle_optimization_memory_streaming_capture,
    handle_optimization_profilegpu_capture,
    handle_optimization_quick_triage,
    handle_optimization_rendering_capture,
    handle_optimization_run_console_checklist,
    handle_optimization_scene_audit,
    handle_optimization_stat_capture,
    handle_optimization_texture_harm_rank,
    handle_optimization_tool_catalog,
    handle_optimization_viewmode_capture_set,
    handle_optimization_visual_logger_ingest,
    handle_optimization_vr_audit,
)
from mcp_bridge.handlers.project_index import (
    handle_project_index_rebuild,
    handle_project_index_query,
    handle_project_semantic_diff,
    handle_gameplay_pattern_search,
)
from mcp_bridge.handlers.cpp_tools import (
    handle_cpp_class_create,
    handle_cpp_build,
    handle_job_status,
    handle_job_list,
    handle_job_cancel,
)
from mcp_bridge.handlers.blueprint_graph import (
    handle_blueprint_inspect,
    handle_blueprint_add_variable,
    handle_blueprint_remove_variable,
    handle_blueprint_set_variable_default,
    handle_blueprint_add_function,
    handle_blueprint_remove_function,
    handle_blueprint_add_event_dispatcher,
    handle_blueprint_remove_event_dispatcher,
    handle_blueprint_add_interface,
    handle_blueprint_remove_interface,
    handle_blueprint_component_remove,
    handle_blueprint_component_rename,
    handle_blueprint_node_add,
    handle_blueprint_node_delete,
    handle_blueprint_node_move,
    handle_blueprint_node_set_enabled,
    handle_blueprint_pins_connect,
    handle_blueprint_pins_break,
)


# Command dispatch table
COMMAND_ROUTES: Dict[str, Callable[[Dict[str, Any]], Dict[str, Any]]] = {
    # System
    "ping": handle_ping,
    "test_connection": handle_test_connection,
    "python_proxy": handle_python_proxy,
    "ue_logs": handle_ue_logs,
    "bridge_status": handle_bridge_status,
    "bridge_latest_notification": handle_bridge_latest_notification,
    "bridge_copy_project_info": handle_bridge_copy_project_info,
    "bridge_clear_log": handle_bridge_clear_log,
    "clear_output_log": handle_bridge_clear_log,
    "bridge_command_manifest": handle_bridge_command_manifest,
    "bridge_self_test": handle_bridge_self_test,
    "restart_listener": handle_restart_listener,

    # Project & Assets
    "project_info": handle_project_info,
    "asset_list": handle_asset_list,
    "asset_info": handle_asset_info,
    "input_mapping_info": handle_input_mapping_info,
    "asset_load_diagnostics": handle_asset_load_diagnostics,
    "asset_save_many": handle_asset_save_many,
    "project_enable_plugins": handle_project_enable_plugins,

    # Project intelligence (index, semantic diff, pattern search)
    "project_index_rebuild": handle_project_index_rebuild,
    "project_index_query": handle_project_index_query,
    "project_semantic_diff": handle_project_semantic_diff,
    "gameplay_pattern_search": handle_gameplay_pattern_search,

    # C++ generation and build (job-based)
    "cpp_class_create": handle_cpp_class_create,
    "cpp_build": handle_cpp_build,
    "job_status": handle_job_status,
    "job_list": handle_job_list,
    "job_cancel": handle_job_cancel,

    # Blueprint member and graph editing (C++ Inspector/Mutator bridge)
    "blueprint_inspect": handle_blueprint_inspect,
    "blueprint_add_variable": handle_blueprint_add_variable,
    "blueprint_remove_variable": handle_blueprint_remove_variable,
    "blueprint_set_variable_default": handle_blueprint_set_variable_default,
    "blueprint_add_function": handle_blueprint_add_function,
    "blueprint_remove_function": handle_blueprint_remove_function,
    "blueprint_add_event_dispatcher": handle_blueprint_add_event_dispatcher,
    "blueprint_remove_event_dispatcher": handle_blueprint_remove_event_dispatcher,
    "blueprint_add_interface": handle_blueprint_add_interface,
    "blueprint_remove_interface": handle_blueprint_remove_interface,
    "blueprint_component_remove": handle_blueprint_component_remove,
    "blueprint_component_rename": handle_blueprint_component_rename,
    "blueprint_node_add": handle_blueprint_node_add,
    "blueprint_node_delete": handle_blueprint_node_delete,
    "blueprint_node_move": handle_blueprint_node_move,
    "blueprint_node_set_enabled": handle_blueprint_node_set_enabled,
    "blueprint_pins_connect": handle_blueprint_pins_connect,
    "blueprint_pins_break": handle_blueprint_pins_break,

    # Actors
    "actor_spawn": handle_actor_spawn,
    "actor_duplicate": handle_actor_duplicate,
    "actor_delete": handle_actor_delete,
    "actor_modify": handle_actor_modify,
    "actor_organize": handle_actor_organize,
    "actor_snap_to_socket": handle_actor_snap_to_socket,
    "batch_spawn": handle_batch_spawn,
    "placement_validate": handle_placement_validate,

    # Level
    "level_actors": handle_level_actors,
    "level_save": handle_level_save,
    "level_outliner": handle_level_outliner,

    # Viewport
    "viewport_screenshot": handle_viewport_screenshot,
    "viewport_camera": handle_viewport_camera,
    "viewport_mode": handle_viewport_mode,
    "viewport_focus": handle_viewport_focus,
    "viewport_render_mode": handle_viewport_render_mode,
    "viewport_bounds": handle_viewport_bounds,
    "viewport_fit": handle_viewport_fit,
    "viewport_look_at": handle_viewport_look_at,

    # Materials
    "material_list": handle_material_list,
    "material_info": handle_material_info,
    "material_create": handle_material_create,
    "material_apply": handle_material_apply,

    # Blueprints
    "blueprint_create": handle_blueprint_create,
    "blueprint_list": handle_blueprint_list,
    "blueprint_info": handle_blueprint_info,
    "blueprint_compile": handle_blueprint_compile,
    "blueprint_document": handle_blueprint_document,
    "blueprint_build_from_json": handle_blueprint_build_from_json,
    "anim_blueprint_build_from_json": handle_anim_blueprint_build_from_json,
    "widget_build_from_json": handle_widget_build_from_json,
    "widget_title_template": handle_widget_title_template,
    "widget_title_card_create": handle_widget_title_card_create,
    "widget_lower_third_create": handle_widget_lower_third_create,

    # Data-driven Sequencer titles
    "title_manifest_create": handle_title_manifest_create,
    "title_manifest_validate": handle_title_manifest_validate,
    "title_manifest_from_reference": handle_title_manifest_from_reference,
    "title_widget_build_from_manifest": handle_title_widget_build_from_manifest,
    "title_controller_create": handle_title_controller_create,
    "title_sequence_bind": handle_title_sequence_bind,
    "title_render_compare": handle_title_render_compare,
    "title_manifest_adjust": handle_title_manifest_adjust,

    # PromptBrush
    "prompt_generate": handle_prompt_generate,
    "prompt_status": handle_prompt_status,
    "prompt_spec_list": handle_prompt_spec_list,

    # Gameplay (PIE harness + telemetry)
    "gameplay_pie_start": handle_pie_start,
    "gameplay_pie_stop": handle_pie_stop,
    "gameplay_telemetry_snapshot": handle_telemetry_snapshot,
    "gameplay_run_acceptance_tests": handle_run_acceptance_tests,

    # Effects (PostProcess, Camera Shake, Visual Effects)
    "pp_volume_spawn": handle_pp_volume_spawn,
    "pp_volume_modify": handle_pp_volume_modify,
    "pp_preset": handle_pp_preset,
    "camera_shake_spawn": handle_camera_shake_spawn,
    "camera_shake_play": handle_camera_shake_play,
    "camera_shake_blueprint": handle_camera_shake_blueprint,
    "camera_shake_trigger": handle_camera_shake_trigger,
    "console_effect": handle_console_effect,

    # Optimization Assistant
    "optimization_tool_catalog": handle_optimization_tool_catalog,
    "optimization_quick_triage": handle_optimization_quick_triage,
    "optimization_run_console_checklist": handle_optimization_run_console_checklist,
    "optimization_gpu_capture": handle_optimization_gpu_capture,
    "optimization_cpu_capture": handle_optimization_cpu_capture,
    "optimization_rendering_capture": handle_optimization_rendering_capture,
    "optimization_memory_streaming_capture": handle_optimization_memory_streaming_capture,
    "optimization_asset_audit": handle_optimization_asset_audit,
    "optimization_scene_audit": handle_optimization_scene_audit,
    "optimization_vr_audit": handle_optimization_vr_audit,
    "optimization_viewmode_capture_set": handle_optimization_viewmode_capture_set,
    "optimization_generate_report": handle_optimization_generate_report,
    "optimization_texture_harm_rank": handle_optimization_texture_harm_rank,
    "optimization_asset_cost_rank": handle_optimization_asset_cost_rank,
    "optimization_stat_capture": handle_optimization_stat_capture,
    "optimization_insights_trace_start": handle_optimization_insights_trace_start,
    "optimization_insights_trace_stop": handle_optimization_insights_trace_stop,
    "optimization_insights_trace_summary": handle_optimization_insights_trace_summary,
    "optimization_profilegpu_capture": handle_optimization_profilegpu_capture,
    "optimization_camera_path_profile": handle_optimization_camera_path_profile,
    "optimization_visual_logger_ingest": handle_optimization_visual_logger_ingest,
    "optimization_fix_candidates": handle_optimization_fix_candidates,
    "optimization_apply_low_risk_fixes": handle_optimization_apply_low_risk_fixes,
    "optimization_before_after_verify": handle_optimization_before_after_verify,

    # Transaction support (called from MCP server for undo/redo)
    "begin_transaction": lambda params: _handle_transaction("begin", params),
    "end_transaction": lambda params: _handle_transaction("end", params),
    "undo": lambda params: _handle_undo(params),
    "redo": lambda params: _handle_redo(params),
}


def route_command(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Route a command string to its handler function.
    
    Args:
        command: The command name to execute.
        params: Parameters to pass to the handler.
    
    Returns:
        Standard response dict: {success: bool, data: dict, error?: str}
    """
    handler = COMMAND_ROUTES.get(command)
    if handler is None:
        return {
            "success": False,
            "data": {},
            "error": f"Unknown command: '{command}'. Available: {sorted(COMMAND_ROUTES.keys())}"
        }
    return handler(params)


def _handle_transaction(action: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Handle begin/end transaction commands."""
    try:
        import unreal
        if action == "begin":
            description = params.get("description", "MCP Bridge Operation")
            unreal.SystemLibrary.begin_transaction(description)
        else:
            unreal.SystemLibrary.end_transaction()
        return {"success": True, "data": {"action": action}}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def _handle_undo(params: Dict[str, Any]) -> Dict[str, Any]:
    """Handle undo command."""
    try:
        import unreal
        count = params.get("count", 1)
        for _ in range(count):
            unreal.EditorLevelLibrary.editor_undo()
        return {"success": True, "data": {"undone": count}}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def _handle_redo(params: Dict[str, Any]) -> Dict[str, Any]:
    """Handle redo command."""
    try:
        import unreal
        count = params.get("count", 1)
        for _ in range(count):
            unreal.EditorLevelLibrary.editor_redo()
        return {"success": True, "data": {"redone": count}}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}
