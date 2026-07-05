"""Lightweight in-editor MCP Bridge panel helpers.

UE4.27 Python can register editor menu actions reliably. The first version
uses Window menu entries plus status dialogs, backed by the persistent state
store in mcp_bridge.state.
"""

import json
from typing import Any, Dict, Optional

from mcp_bridge import state


MENU_NAME = "LevelEditor.MainMenu.Window"
SECTION_NAME = "MCPBridge"
OWNER_NAME = "MCPBridgePanel"
ENTRY_NAMES = [
    "MCPBridge_ShowStatus",
    "MCPBridge_TestConnection",
    "MCPBridge_RestartListener",
    "MCPBridge_SelfTest",
    "MCPBridge_CopyProjectInfo",
    "MCPBridge_OpenOutputLog",
    "MCPBridge_ClearLog",
    "MCPBridge_SaveLevel",
    "MCPBridge_Screenshot",
    "MCPBridge_OptimizationCatalog",
    "MCPBridge_OptimizationQuickTriage",
    "MCPBridge_OptimizationGPU",
    "MCPBridge_OptimizationCPU",
    "MCPBridge_OptimizationMemory",
    "MCPBridge_OptimizationAssetAudit",
    "MCPBridge_OptimizationSceneAudit",
    "MCPBridge_OptimizationVRAudit",
    "MCPBridge_OptimizationReport",
    "MCPBridge_TogglePythonProxy",
    "MCPBridge_ToggleConsole",
    "MCPBridge_ToggleActorDelete",
    "MCPBridge_ToggleAssetDelete",
    "MCPBridge_ToggleAutoCompile",
    "MCPBridge_ToggleAutoSave",
]


def _dialog(title: str, message: str) -> None:
    try:
        import unreal
        unreal.EditorDialog.show_message(title, message, unreal.AppMsgType.OK)
    except Exception:
        print(f"{title}\n{message}")


def _run(command: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    from mcp_bridge.router import route_command

    params = params or {}
    return route_command(command, params)


def _format_status(payload: Dict[str, Any]) -> str:
    safety = payload.get("safety", {})
    history = payload.get("history", [])[-12:]

    lines = [
        "MCP Bridge Panel",
        "",
        f"Connected: {payload.get('connected')}",
        f"Listener: {payload.get('listener_host')}:{payload.get('listener_port')}",
        f"Engine: {payload.get('engine_version')}",
        f"Project: {payload.get('project_name')}",
        f"Current level: {payload.get('current_level')}",
        f"Project dir: {payload.get('project_dir')}",
        f"Last ping: {payload.get('last_ping_at') or '(none)'}",
        f"Last command: {payload.get('last_command') or '(none)'}",
        f"Last result: {payload.get('last_command_status') or '(none)'}",
        f"Last error: {payload.get('last_error_code') or '(none)'} {payload.get('last_error_message') or ''}".rstrip(),
        f"Commands handled: {payload.get('commands_handled', 0)}",
        f"Failures: {payload.get('failures', 0)}",
        "",
        "Safety",
    ]

    for key in sorted(safety.keys()):
        lines.append(f"- {key}: {safety[key]}")

    lines.extend(["", "Recent commands"])
    if history:
        for item in history:
            error = item.get("error_code") or item.get("error") or ""
            suffix = f"  {error}" if error else ""
            lines.append(
                f"{item.get('timestamp')}  {item.get('command')}  {item.get('status')}  "
                f"{item.get('duration_ms')} ms{suffix}"
            )
    else:
        lines.append("(none)")

    lines.extend([
        "",
        f"State file: {payload.get('status_path')}",
        f"Config file: {payload.get('config_path')}",
    ])
    return "\n".join(lines)


def show_status() -> None:
    _dialog("MCP Bridge Panel", _format_status(state.get_status()))


def run_self_test() -> None:
    result = _run("bridge_self_test")
    text = json.dumps(result, indent=2, sort_keys=True)
    _dialog("MCP Bridge Self-Test", text)


def test_connection() -> None:
    result = _run("test_connection")
    _dialog("MCP Bridge Test Connection", json.dumps(result, indent=2, sort_keys=True))


def restart_listener() -> None:
    result = _run("restart_listener")
    _dialog("MCP Bridge Restart Listener", json.dumps(result, indent=2, sort_keys=True))


def copy_project_info() -> None:
    result = _run("bridge_copy_project_info")
    copied = result.get("data", {}).get("copied", False)
    message = "Project info copied to clipboard." if copied else result.get("data", {}).get("message", "Project info prepared.")
    _dialog("MCP Bridge Copy Project Info", message)


def open_output_log() -> None:
    result = _run("bridge_open_output_log")
    _dialog("MCP Bridge Output Log", json.dumps(result, indent=2, sort_keys=True))


def clear_log() -> None:
    _run("bridge_clear_log")
    _dialog("MCP Bridge Log", "Bridge command history cleared.")


def save_current_level() -> None:
    result = _run("level_save", {"save_all": False})
    _dialog("MCP Bridge Save Current Level", json.dumps(result, indent=2, sort_keys=True))


def take_viewport_screenshot() -> None:
    result = _run("viewport_screenshot", {"resolution": {"width": 1920, "height": 1080}})
    _dialog("MCP Bridge Viewport Screenshot", json.dumps(result, indent=2, sort_keys=True))


def optimization_catalog() -> None:
    result = _run("optimization_tool_catalog")
    tools = result.get("data", {}).get("tools", [])
    lines = ["Optimization Tool Catalog", ""]
    for tool in tools:
        lines.append(f"{tool.get('command')} [{tool.get('category')}]")
        lines.append(f"  Answers: {tool.get('answers')}")
        lines.append(f"  Use when: {tool.get('use_when')}")
        lines.append("")
    _dialog("MCP Bridge Optimization Catalog", "\n".join(lines).rstrip())


def optimization_quick_triage() -> None:
    result = _run("optimization_quick_triage", {
        "target": "desktop_60",
        "capture_screenshots": True,
        "include_asset_audit": True,
        "include_scene_audit": True,
        "include_vr_checks": False,
        "generate_report": True,
    })
    _dialog("MCP Bridge Optimization Triage", json.dumps(result, indent=2, sort_keys=True))


def optimization_gpu_capture() -> None:
    result = _run("optimization_gpu_capture")
    _dialog("MCP Bridge GPU Capture", json.dumps(result, indent=2, sort_keys=True))


def optimization_cpu_capture() -> None:
    result = _run("optimization_cpu_capture", {"capture_screenshots": True})
    _dialog("MCP Bridge CPU Capture", json.dumps(result, indent=2, sort_keys=True))


def optimization_memory_capture() -> None:
    result = _run("optimization_memory_streaming_capture", {"capture_screenshots": True})
    _dialog("MCP Bridge Memory and Streaming", json.dumps(result, indent=2, sort_keys=True))


def optimization_asset_audit() -> None:
    result = _run("optimization_asset_audit", {"path": "/Game/", "recursive": True})
    _dialog("MCP Bridge Asset Audit", json.dumps(result, indent=2, sort_keys=True))


def optimization_scene_audit() -> None:
    result = _run("optimization_scene_audit", {"include_design_advice": True})
    _dialog("MCP Bridge Scene Audit", json.dumps(result, indent=2, sort_keys=True))


def optimization_vr_audit() -> None:
    result = _run("optimization_vr_audit", {"target": "vr_90", "include_asset_audit": True})
    _dialog("MCP Bridge VR Audit", json.dumps(result, indent=2, sort_keys=True))


def optimization_generate_report() -> None:
    result = _run("optimization_generate_report", {
        "target": "desktop_60",
        "capture_screenshots": True,
        "include_asset_audit": True,
        "include_scene_audit": True,
    })
    _dialog("MCP Bridge Optimization Report", json.dumps(result, indent=2, sort_keys=True))


def toggle_safety(key: str) -> None:
    current = state.load_safety()
    if key not in current:
        _dialog("MCP Bridge Safety", f"Unknown safety toggle: {key}")
        return
    enabled = not bool(current[key])
    result = _run("bridge_safety_set", {"updates": {key: enabled}})
    _dialog("MCP Bridge Safety", json.dumps(result, indent=2, sort_keys=True))


def _add_entry(menu: Any, name: str, label: str, tooltip: str, python_command: str) -> None:
    import unreal

    entry = unreal.ToolMenuEntry(
        name=name,
        type=unreal.MultiBlockType.MENU_ENTRY,
        user_interface_action_type=unreal.UserInterfaceActionType.BUTTON,
    )
    entry.set_label(label)
    entry.set_tool_tip(tooltip)
    entry.set_string_command(
        unreal.ToolMenuStringCommandType.PYTHON,
        "",
        python_command,
    )
    menu.add_menu_entry(SECTION_NAME, entry)


def register_menu() -> None:
    """Register Window > MCP Bridge menu entries."""
    try:
        import unreal

        menus = unreal.ToolMenus.get()
        menu = menus.extend_menu(MENU_NAME)

        try:
            menu.add_section(SECTION_NAME, "MCP Bridge")
        except Exception:
            pass

        for entry_name in ENTRY_NAMES:
            try:
                menus.remove_entry(MENU_NAME, SECTION_NAME, entry_name)
            except Exception:
                pass

        _add_entry(menu, "MCPBridge_ShowStatus", "MCP Bridge Status", "Show bridge status and recent commands.", "import mcp_bridge.panel as p; p.show_status()")
        _add_entry(menu, "MCPBridge_TestConnection", "Test Connection", "Run the test_connection handler.", "import mcp_bridge.panel as p; p.test_connection()")
        _add_entry(menu, "MCPBridge_RestartListener", "Restart Listener", "Restart the Python HTTP listener.", "import mcp_bridge.panel as p; p.restart_listener()")
        _add_entry(menu, "MCPBridge_SelfTest", "Run Self-Test", "Run bridge health checks.", "import mcp_bridge.panel as p; p.run_self_test()")
        _add_entry(menu, "MCPBridge_CopyProjectInfo", "Copy Project Info", "Copy bridge status JSON to the clipboard.", "import mcp_bridge.panel as p; p.copy_project_info()")
        _add_entry(menu, "MCPBridge_OpenOutputLog", "Open Output Log", "Open the Unreal Output Log if available.", "import mcp_bridge.panel as p; p.open_output_log()")
        _add_entry(menu, "MCPBridge_ClearLog", "Clear Bridge Log", "Clear the rolling MCP command history.", "import mcp_bridge.panel as p; p.clear_log()")
        _add_entry(menu, "MCPBridge_SaveLevel", "Save Current Level", "Save the active level.", "import mcp_bridge.panel as p; p.save_current_level()")
        _add_entry(menu, "MCPBridge_Screenshot", "Take Viewport Screenshot", "Capture the active viewport.", "import mcp_bridge.panel as p; p.take_viewport_screenshot()")

        _add_entry(menu, "MCPBridge_OptimizationCatalog", "Optimization Tool Catalog", "Show UE4.27 optimization commands and what they answer.", "import mcp_bridge.panel as p; p.optimization_catalog()")
        _add_entry(menu, "MCPBridge_OptimizationQuickTriage", "Optimization: Run Quick Triage", "Run stat overlays, capture evidence, audit assets and scene setup, then create a report.", "import mcp_bridge.panel as p; p.optimization_quick_triage()")
        _add_entry(menu, "MCPBridge_OptimizationGPU", "Optimization: GPU Capture", "Run GPU profiling overlays and capture shader, overdraw, and light complexity views.", "import mcp_bridge.panel as p; p.optimization_gpu_capture()")
        _add_entry(menu, "MCPBridge_OptimizationCPU", "Optimization: CPU Capture", "Run Game, Blueprint, animation, physics, and particle stat overlays.", "import mcp_bridge.panel as p; p.optimization_cpu_capture()")
        _add_entry(menu, "MCPBridge_OptimizationMemory", "Optimization: Memory and Streaming", "Run streaming, memory, and level stat overlays.", "import mcp_bridge.panel as p; p.optimization_memory_capture()")
        _add_entry(menu, "MCPBridge_OptimizationAssetAudit", "Optimization: Asset Audit", "Inspect meshes, textures, materials, and skeletal assets for optimization risks.", "import mcp_bridge.panel as p; p.optimization_asset_audit()")
        _add_entry(menu, "MCPBridge_OptimizationSceneAudit", "Optimization: Scene Audit", "Inspect the current level for lights, ticks, repeated props, and design-around opportunities.", "import mcp_bridge.panel as p; p.optimization_scene_audit()")
        _add_entry(menu, "MCPBridge_OptimizationVRAudit", "Optimization: VR Audit", "Check the level against a VR 90 Hz frame budget.", "import mcp_bridge.panel as p; p.optimization_vr_audit()")
        _add_entry(menu, "MCPBridge_OptimizationReport", "Optimization: Export Report", "Generate a Markdown optimization report under Saved/MCP/Optimization.", "import mcp_bridge.panel as p; p.optimization_generate_report()")

        _add_entry(menu, "MCPBridge_TogglePythonProxy", "Toggle Allow python_proxy", "Enable or disable python_proxy MCP calls.", "import mcp_bridge.panel as p; p.toggle_safety('allow_python_proxy')")
        _add_entry(menu, "MCPBridge_ToggleConsole", "Toggle Allow console_command", "Enable or disable console command MCP calls.", "import mcp_bridge.panel as p; p.toggle_safety('allow_console_command')")
        _add_entry(menu, "MCPBridge_ToggleActorDelete", "Toggle Allow actor_delete", "Enable or disable actor delete MCP calls.", "import mcp_bridge.panel as p; p.toggle_safety('allow_actor_delete')")
        _add_entry(menu, "MCPBridge_ToggleAssetDelete", "Toggle Allow asset_delete", "Enable or disable future asset delete MCP calls.", "import mcp_bridge.panel as p; p.toggle_safety('allow_asset_delete')")
        _add_entry(menu, "MCPBridge_ToggleAutoCompile", "Toggle Auto-compile Blueprints", "Persist auto-compile preference.", "import mcp_bridge.panel as p; p.toggle_safety('auto_compile_blueprints')")
        _add_entry(menu, "MCPBridge_ToggleAutoSave", "Toggle Auto-save after operations", "Persist auto-save preference.", "import mcp_bridge.panel as p; p.toggle_safety('auto_save_after_operations')")

        menus.refresh_all_widgets()
        unreal.log("[MCP Bridge] Window menu registered")
    except Exception as e:
        try:
            import unreal
            unreal.log_warning(f"[MCP Bridge] Could not register panel menu: {e}")
        except Exception:
            print(f"[MCP Bridge] Could not register panel menu: {e}")
