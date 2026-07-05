"""Persistent state and safety settings for the MCP Bridge."""

import datetime
import json
import os
import threading
import time
from typing import Any, Dict, List, Optional, Tuple


HISTORY_LIMIT = 50
DEFAULT_HOST = "localhost"
DEFAULT_PORT = 8080

DEFAULT_SAFETY: Dict[str, bool] = {
    "allow_python_proxy": True,
    "allow_console_command": True,
    "allow_actor_delete": False,
    "allow_asset_delete": False,
    "auto_compile_blueprints": True,
    "auto_save_after_operations": False,
    "pie_commands_enabled": True,
}

COMMAND_SAFETY_KEYS: Dict[str, str] = {
    "python_proxy": "allow_python_proxy",
    "actor_delete": "allow_actor_delete",
    "console_effect": "allow_console_command",
    "viewport_render_mode": "allow_console_command",
    "optimization_quick_triage": "allow_console_command",
    "optimization_run_console_checklist": "allow_console_command",
    "optimization_gpu_capture": "allow_console_command",
    "optimization_cpu_capture": "allow_console_command",
    "optimization_rendering_capture": "allow_console_command",
    "optimization_memory_streaming_capture": "allow_console_command",
    "optimization_viewmode_capture_set": "allow_console_command",
    "optimization_generate_report": "allow_console_command",
    "optimization_stat_capture": "allow_console_command",
    "optimization_insights_trace_start": "allow_console_command",
    "optimization_insights_trace_stop": "allow_console_command",
    "optimization_insights_trace_summary": "allow_console_command",
    "optimization_profilegpu_capture": "allow_console_command",
    "optimization_camera_path_profile": "allow_console_command",
    "optimization_before_after_verify": "allow_console_command",
    "gameplay_pie_start": "pie_commands_enabled",
    "gameplay_pie_stop": "pie_commands_enabled",
    "gameplay_pie_status": "pie_commands_enabled",
    "pie_start": "pie_commands_enabled",
    "pie_stop": "pie_commands_enabled",
    "pie_status": "pie_commands_enabled",
}

_lock = threading.RLock()


def _utc_now() -> str:
    return datetime.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"


def _project_dir() -> str:
    try:
        import unreal
        return unreal.SystemLibrary.get_project_directory()
    except Exception:
        return os.getcwd()


def _mcp_dir() -> str:
    path = os.path.join(_project_dir(), "Saved", "MCP")
    os.makedirs(path, exist_ok=True)
    return path


def status_path() -> str:
    return os.path.join(_mcp_dir(), "bridge_status.json")


def config_path() -> str:
    return os.path.join(_mcp_dir(), "bridge_config.json")


def last_result_path() -> str:
    return os.path.join(_mcp_dir(), "last_command_result.json")


def latest_notification_path() -> str:
    return os.path.join(_mcp_dir(), "latest_notification.json")


def notification_log_path() -> str:
    return os.path.join(_mcp_dir(), "bridge_notifications.jsonl")


def _write_json(path: str, payload: Dict[str, Any]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp_path = path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True, default=str)
    os.replace(tmp_path, path)


def _read_json(path: str, fallback: Dict[str, Any]) -> Dict[str, Any]:
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict):
            return data
    except Exception:
        pass
    return dict(fallback)


def _base_status() -> Dict[str, Any]:
    return {
        "connected": False,
        "listener_running": False,
        "listener_host": DEFAULT_HOST,
        "listener_port": DEFAULT_PORT,
        "engine_version": "",
        "project_name": "",
        "project_dir": _project_dir(),
        "content_dir": "",
        "current_level": "",
        "mcp_client_name": "",
        "last_ping_at": "",
        "last_command": "",
        "last_command_at": "",
        "last_command_status": "",
        "last_command_duration_ms": 0.0,
        "last_result_path": "",
        "latest_notification_path": "",
        "notification_log_path": "",
        "last_error_code": "",
        "last_error_message": "",
        "commands_handled": 0,
        "failures": 0,
        "started_at": "",
        "updated_at": _utc_now(),
        "history": [],
    }


def _load_status() -> Dict[str, Any]:
    status = _base_status()
    status.update(_read_json(status_path(), status))
    if not isinstance(status.get("history"), list):
        status["history"] = []
    return status


def load_safety() -> Dict[str, bool]:
    with _lock:
        raw = _read_json(config_path(), {"safety": DEFAULT_SAFETY}).get("safety", {})
        safety = dict(DEFAULT_SAFETY)
        if isinstance(raw, dict):
            for key, value in raw.items():
                if key in safety:
                    safety[key] = bool(value)
        return safety


def save_safety(safety: Dict[str, bool]) -> Dict[str, bool]:
    with _lock:
        clean = dict(DEFAULT_SAFETY)
        for key, value in safety.items():
            if key in clean:
                clean[key] = bool(value)
        _write_json(config_path(), {"safety": clean, "updated_at": _utc_now()})
        return clean


def set_safety_toggle(key: str, enabled: bool) -> Dict[str, bool]:
    if key not in DEFAULT_SAFETY:
        raise KeyError(f"Unknown safety toggle: {key}")
    safety = load_safety()
    safety[key] = bool(enabled)
    return save_safety(safety)


def _runtime_info() -> Dict[str, str]:
    info = {
        "engine_version": "",
        "project_name": "",
        "project_dir": _project_dir(),
        "content_dir": "",
        "current_level": "",
    }
    try:
        import unreal
        info["engine_version"] = unreal.SystemLibrary.get_engine_version()
        info["project_name"] = unreal.SystemLibrary.get_game_name()
        info["project_dir"] = unreal.SystemLibrary.get_project_directory()
        info["content_dir"] = unreal.SystemLibrary.get_project_content_directory()
        try:
            world = unreal.EditorLevelLibrary.get_editor_world()
            if world:
                info["current_level"] = world.get_path_name()
        except Exception:
            pass
    except Exception:
        pass
    return info


def refresh_runtime_status(
    connected: Optional[bool] = None,
    listener_running: Optional[bool] = None,
    host: Optional[str] = None,
    port: Optional[int] = None,
) -> Dict[str, Any]:
    with _lock:
        status = _load_status()
        status.update(_runtime_info())
        if connected is not None:
            status["connected"] = bool(connected)
        if listener_running is not None:
            status["listener_running"] = bool(listener_running)
        if host:
            status["listener_host"] = str(host)
        if port is not None:
            status["listener_port"] = int(port)
        if not status.get("started_at") and status.get("listener_running"):
            status["started_at"] = _utc_now()
        status["updated_at"] = _utc_now()
        _write_json(status_path(), status)
        return status


def mark_listener_started(host: str, port: int) -> Dict[str, Any]:
    with _lock:
        status = refresh_runtime_status(
            connected=True,
            listener_running=True,
            host=host,
            port=port,
        )
        if not status.get("started_at"):
            status["started_at"] = _utc_now()
            _write_json(status_path(), status)
        load_safety()
        return status


def mark_listener_stopped() -> Dict[str, Any]:
    return refresh_runtime_status(connected=False, listener_running=False)


def _error_details(result: Dict[str, Any]) -> Tuple[str, str]:
    data = result.get("data") if isinstance(result.get("data"), dict) else {}
    code = ""
    if isinstance(data, dict):
        code = str(data.get("error_code", "") or data.get("code", ""))
    message = str(result.get("error", "") or "")
    if not code and message:
        code = message.split(":", 1)[0].strip().upper().replace(" ", "_")[:48]
    return code, message


def _compact_value(value: Any, max_items: int = 8) -> Any:
    if isinstance(value, dict):
        compact: Dict[str, Any] = {}
        for index, (key, item) in enumerate(value.items()):
            if index >= max_items:
                compact["truncated"] = True
                break
            compact[str(key)] = _compact_value(item, max_items=max_items)
        return compact
    if isinstance(value, list):
        items = [_compact_value(item, max_items=max_items) for item in value[:max_items]]
        if len(value) > max_items:
            items.append({"truncated_count": len(value) - max_items})
        return items
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return str(value)


def _result_key_data(result: Dict[str, Any]) -> Dict[str, Any]:
    data = result.get("data") if isinstance(result.get("data"), dict) else {}
    key_data: Dict[str, Any] = {}
    for key in (
        "target",
        "target_frame_ms",
        "likely_bottleneck",
        "report_path",
        "finding_count",
        "screenshots",
        "recommended_next_steps",
        "evidence",
        "summary",
    ):
        if isinstance(data, dict) and key in data:
            key_data[key] = _compact_value(data.get(key), max_items=6)
    if isinstance(data, dict) and "findings" in data:
        findings = data.get("findings") or []
        if isinstance(findings, list):
            key_data["finding_count"] = key_data.get("finding_count", len(findings))
            key_data["top_findings"] = _compact_value(findings[:6], max_items=6)
    return key_data


def _result_summary(command: str, result: Dict[str, Any]) -> str:
    if not result.get("success", False):
        return str(result.get("error", "Command failed"))
    data = result.get("data") if isinstance(result.get("data"), dict) else {}
    if command.startswith("optimization_") and isinstance(data, dict):
        pieces = []
        bottleneck = data.get("likely_bottleneck")
        if bottleneck:
            pieces.append(f"likely bottleneck: {bottleneck}")
        findings = data.get("findings")
        if isinstance(findings, list):
            pieces.append(f"{len(findings)} findings")
        elif data.get("finding_count") is not None:
            pieces.append(f"{data.get('finding_count')} findings")
        report_path_value = data.get("report_path") or data.get("report")
        if report_path_value:
            pieces.append(f"report: {report_path_value}")
        if pieces:
            return "; ".join(pieces)
    return "Command completed successfully"


def _persist_command_result(
    command: str,
    result: Dict[str, Any],
    duration_ms: float,
    params: Optional[Dict[str, Any]],
    timestamp: str,
) -> Dict[str, Any]:
    payload = {
        "timestamp": timestamp,
        "command": command,
        "status": "success" if result.get("success", False) else "failed",
        "duration_ms": round(float(duration_ms or 0.0), 1),
        "params": _compact_value(params or {}, max_items=12),
        "result": _compact_value(result, max_items=60),
    }
    _write_json(last_result_path(), payload)

    notification = {
        "timestamp": timestamp,
        "command": command,
        "status": payload["status"],
        "duration_ms": payload["duration_ms"],
        "summary": _result_summary(command, result),
        "key_data": _result_key_data(result),
        "result_path": last_result_path(),
    }
    _write_json(latest_notification_path(), notification)
    os.makedirs(os.path.dirname(notification_log_path()), exist_ok=True)
    with open(notification_log_path(), "a", encoding="utf-8") as f:
        f.write(json.dumps(notification, sort_keys=True, default=str) + "\n")
    return notification


def record_command(
    command: str,
    result: Dict[str, Any],
    duration_ms: Optional[float] = None,
    params: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    with _lock:
        status = _load_status()
        status.update(_runtime_info())

        ok = bool(result.get("success", False))
        now = _utc_now()
        status["connected"] = True
        status["listener_running"] = True
        status["last_command"] = command
        status["last_command_at"] = now
        status["last_command_status"] = "success" if ok else "failed"
        status["last_command_duration_ms"] = round(float(duration_ms or 0.0), 1)
        status["last_result_path"] = last_result_path()
        status["latest_notification_path"] = latest_notification_path()
        status["notification_log_path"] = notification_log_path()
        status["commands_handled"] = int(status.get("commands_handled", 0)) + 1
        status["updated_at"] = now

        if command in ("ping", "test_connection"):
            status["last_ping_at"] = now

        if params and isinstance(params, dict):
            client_name = params.get("mcp_client_name") or params.get("client_name")
            if client_name:
                status["mcp_client_name"] = str(client_name)

        error_code, error_message = _error_details(result)
        if ok:
            status["last_error_code"] = ""
            status["last_error_message"] = ""
        else:
            status["failures"] = int(status.get("failures", 0)) + 1
            status["last_error_code"] = error_code
            status["last_error_message"] = error_message

        entry = {
            "timestamp": now,
            "command": command,
            "status": status["last_command_status"],
            "duration_ms": status["last_command_duration_ms"],
        }
        try:
            notification = _persist_command_result(command, result, duration_ms or 0.0, params, now)
            entry["summary"] = notification.get("summary", "")
        except Exception:
            pass
        if not ok:
            entry["error_code"] = error_code
            entry["error"] = error_message

        history: List[Dict[str, Any]] = status.get("history", [])
        history.append(entry)
        status["history"] = history[-HISTORY_LIMIT:]

        _write_json(status_path(), status)
        return status


def clear_history() -> Dict[str, Any]:
    with _lock:
        status = _load_status()
        status["history"] = []
        status["last_command"] = ""
        status["last_command_at"] = ""
        status["last_command_status"] = ""
        status["last_result_path"] = last_result_path()
        status["latest_notification_path"] = latest_notification_path()
        status["notification_log_path"] = notification_log_path()
        status["last_error_code"] = ""
        status["last_error_message"] = ""
        status["updated_at"] = _utc_now()
        _write_json(status_path(), status)
        return status


def get_status() -> Dict[str, Any]:
    with _lock:
        status = refresh_runtime_status()
        status["safety"] = load_safety()
        status["status_path"] = status_path()
        status["config_path"] = config_path()
        status["last_result_path"] = last_result_path()
        status["latest_notification_path"] = latest_notification_path()
        status["notification_log_path"] = notification_log_path()
        return status


def blocked_response_for(command: str) -> Optional[Dict[str, Any]]:
    safety_key = COMMAND_SAFETY_KEYS.get(command)
    if not safety_key:
        return None
    return blocked_response_for_key(safety_key, command)


def safety_enabled(key: str, default: bool = True) -> bool:
    return bool(load_safety().get(key, default))


def blocked_response_for_key(safety_key: str, command: str) -> Optional[Dict[str, Any]]:
    if load_safety().get(safety_key, True):
        return None
    return {
        "success": False,
        "data": {
            "error_code": "SAFETY_TOGGLE_DISABLED",
            "command": command,
            "safety_toggle": safety_key,
        },
        "error": f"Command '{command}' is blocked because safety toggle '{safety_key}' is disabled",
    }
