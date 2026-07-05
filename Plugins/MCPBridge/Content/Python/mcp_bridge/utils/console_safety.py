"""Safety helpers for Unreal console command execution."""

from typing import Any, Dict, Optional

from mcp_bridge import state


_EXACT_COMMANDS = {
    "freezerendering",
    "profilegpu",
    "showlog",
}

_PREFIXES = (
    "stat ",
    "viewmode ",
    "highresshot ",
    "fov ",
    "r.screenpercentage ",
    "r.temporalaacurrentframeweight ",
    "r.defaultfeature.antialiasing ",
    "showflag.",
    "sg.",
)


def is_allowlisted(command: str) -> bool:
    normalized = str(command or "").strip()
    if not normalized:
        return False
    if "\n" in normalized or "\r" in normalized or ";" in normalized:
        return False
    lowered = normalized.lower()
    return lowered in _EXACT_COMMANDS or any(lowered.startswith(prefix) for prefix in _PREFIXES)


def validate_console_command(command: str, bridge_command: str) -> Optional[Dict[str, Any]]:
    blocked = state.blocked_response_for_key("allow_console_command", bridge_command)
    if blocked is not None:
        return blocked
    if not is_allowlisted(command):
        return {
            "success": False,
            "data": {
                "error_code": "CONSOLE_COMMAND_NOT_ALLOWLISTED",
                "command": bridge_command,
                "console_command": command,
            },
            "error": "Console command is not allowlisted",
        }
    return None


def execute_console_command(
    unreal_module: Any,
    world: Any,
    command: str,
    bridge_command: str,
) -> Dict[str, Any]:
    blocked = validate_console_command(command, bridge_command)
    if blocked is not None:
        return blocked
    try:
        unreal_module.SystemLibrary.execute_console_command(world, command)
        return {"success": True, "data": {"console_command": command}}
    except Exception as exc:
        return {"success": False, "data": {"console_command": command}, "error": str(exc)}
