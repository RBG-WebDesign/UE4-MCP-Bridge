"""PIE gameplay agent handlers.

Thin adapters over unreal.PIEAgentLibrary. The listener runs these handlers on
the game thread, so they never sleep or wait for future frames. The Node MCP
server polls pie_agent_status for long-running commands.
"""
from __future__ import annotations

import json
from typing import Any, Callable, Dict

import unreal


def _call(function: Callable[..., str], *args: object) -> Dict[str, Any]:
    """Invoke a PIEAgentLibrary method and return the bridge response shape."""
    try:
        raw = function(*args)
        parsed = json.loads(raw)
        return {
            "success": bool(parsed.get("success")),
            "data": parsed.get("data") or {},
            "error": parsed.get("error"),
        }
    except Exception as exc:
        return {"success": False, "data": {}, "error": f"pie_agent handler error: {exc}"}


def handle_pie_agent_move_to(params: Dict[str, Any]) -> Dict[str, Any]:
    """Start walking the possessed pawn to a location or actor."""
    return _call(unreal.PIEAgentLibrary.start_move_to, json.dumps(params))


def handle_pie_agent_look_at(params: Dict[str, Any]) -> Dict[str, Any]:
    """Point the player camera at a location or actor."""
    return _call(unreal.PIEAgentLibrary.look_at, json.dumps(params))


def handle_pie_agent_press(params: Dict[str, Any]) -> Dict[str, Any]:
    """Press an action mapping or raw key with a hold duration."""
    return _call(unreal.PIEAgentLibrary.start_press, json.dumps(params))


def handle_pie_agent_observe(params: Dict[str, Any]) -> Dict[str, Any]:
    """Capture the current PIE world snapshot."""
    return _call(unreal.PIEAgentLibrary.observe, json.dumps(params))


def handle_pie_agent_record_start(params: Dict[str, Any]) -> Dict[str, Any]:
    """Start recording runtime actor events."""
    return _call(unreal.PIEAgentLibrary.record_start, json.dumps(params))


def handle_pie_agent_record_stop(params: Dict[str, Any]) -> Dict[str, Any]:
    """Stop recording and return its events."""
    return _call(unreal.PIEAgentLibrary.record_stop)


def handle_pie_agent_expect(params: Dict[str, Any]) -> Dict[str, Any]:
    """Start in-engine declarative condition polling."""
    return _call(unreal.PIEAgentLibrary.start_expect, json.dumps(params))


def handle_pie_agent_replay(params: Dict[str, Any]) -> Dict[str, Any]:
    """Replay a session script, or export the current one."""
    return _call(unreal.PIEAgentLibrary.start_replay, json.dumps(params))


def handle_pie_agent_status(params: Dict[str, Any]) -> Dict[str, Any]:
    """Poll the status of a PIE agent operation."""
    return _call(unreal.PIEAgentLibrary.get_operation_status, int(params.get("operation_id", 0)))
