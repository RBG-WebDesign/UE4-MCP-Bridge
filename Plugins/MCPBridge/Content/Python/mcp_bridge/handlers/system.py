"""System command handlers: ping, test_connection, python_proxy, ue_logs, restart."""

import sys
import io
import traceback
import datetime
import json
import os
from typing import Any, Dict


def handle_ping(params: Dict[str, Any]) -> Dict[str, Any]:
    """Health check. Returns engine version and project name.
    
    Args:
        params: Unused.
    
    Returns:
        Engine version, project name, status.
    """
    try:
        import unreal
        return {
            "success": True,
            "data": {
                "status": "ok",
                "engine_version": unreal.SystemLibrary.get_engine_version(),
                "project": unreal.SystemLibrary.get_game_name(),
                "project_dir": unreal.SystemLibrary.get_project_directory(),
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_test_connection(params: Dict[str, Any]) -> Dict[str, Any]:
    """Extended connection test. Same as ping but with more metadata.
    
    Args:
        params: Unused.
    
    Returns:
        Connection status with engine and project details.
    """
    try:
        import unreal
        return {
            "success": True,
            "data": {
                "status": "connected",
                "engine_version": unreal.SystemLibrary.get_engine_version(),
                "project": unreal.SystemLibrary.get_game_name(),
                "project_dir": unreal.SystemLibrary.get_project_directory(),
                "content_dir": unreal.SystemLibrary.get_project_content_directory(),
                "platform": unreal.SystemLibrary.get_platform_user_name(),
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_python_proxy(params: Dict[str, Any]) -> Dict[str, Any]:
    """Execute arbitrary Python code inside the UE4 editor.
    
    Args:
        params: Dict with 'code' (str) - the Python code to execute.
    
    Returns:
        The result of the code execution and any stdout output.
    """
    code = params.get("code", "")
    if not code:
        return {"success": False, "data": {}, "error": "Missing 'code' parameter"}

    # Log execution for safety audit
    timestamp = datetime.datetime.now().isoformat()
    try:
        import unreal
        unreal.log(f"[MCP Bridge] python_proxy [{timestamp}]: {code[:200]}...")
    except ImportError:
        print(f"[MCP Bridge] python_proxy [{timestamp}]: {code[:200]}...")

    # Capture stdout
    old_stdout = sys.stdout
    captured_output = io.StringIO()
    sys.stdout = captured_output

    result_value = None
    try:
        # Try exec first (for statements), then eval (for expressions)
        try:
            # Create a namespace with unreal module available
            exec_globals = {"__builtins__": __builtins__}
            try:
                import unreal
                exec_globals["unreal"] = unreal
            except ImportError:
                pass

            # Try as expression first (returns a value)
            try:
                result_value = eval(code, exec_globals)
            except SyntaxError:
                # Not an expression, execute as statements
                exec(code, exec_globals)
                result_value = exec_globals.get("result", None)

        except Exception as e:
            sys.stdout = old_stdout
            return {
                "success": False,
                "data": {
                    "stdout": captured_output.getvalue(),
                },
                "error": f"Execution error: {str(e)}\n{traceback.format_exc()}"
            }

        sys.stdout = old_stdout
        stdout_text = captured_output.getvalue()

        # Convert result to string representation
        result_str = repr(result_value) if result_value is not None else None

        return {
            "success": True,
            "data": {
                "result": result_str,
                "stdout": stdout_text,
            }
        }

    finally:
        sys.stdout = old_stdout


def handle_ue_logs(params: Dict[str, Any]) -> Dict[str, Any]:
    """Fetch recent UE4 log entries.
    
    Args:
        params: Optional 'category' (str) and 'severity' (str) filters.
    
    Returns:
        Recent log entries. Note: UE4.27 Python API has limited log access,
        so this returns what is available through the Python interface.
    """
    try:
        import unreal
        # UE4.27 does not have a direct Python API for reading the output log.
        # We return a message explaining this and suggest alternatives.
        return {
            "success": True,
            "data": {
                "message": "Direct log reading is not available in UE4.27 Python API. "
                           "Use the Output Log window in the editor (Window > Developer Tools > Output Log). "
                           "For programmatic log capture, consider redirecting Python logging through python_proxy.",
                "suggestion": "You can use python_proxy to run: "
                              "import unreal; unreal.log('your message') to write to the log."
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_bridge_status(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return the persisted bridge status plus runtime metadata."""
    try:
        from mcp_bridge import state

        return {
            "success": True,
            "data": state.get_status(),
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_bridge_latest_notification(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return the latest compact command notification written by the bridge."""
    try:
        from mcp_bridge import state

        path = state.latest_notification_path()
        if not os.path.exists(path):
            return {
                "success": True,
                "data": {
                    "notification": None,
                    "path": path,
                    "message": "No bridge notification has been written yet.",
                },
            }

        with open(path, "r", encoding="utf-8") as f:
            notification = json.load(f)

        return {
            "success": True,
            "data": {
                "notification": notification,
                "path": path,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_bridge_copy_project_info(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return compact project info for external clients.

    Clipboard copying is handled by the C++ panel. The command is still useful
    for MCP clients and prevents the panel from pointing at a missing backend
    command.
    """
    try:
        import unreal
        from mcp_bridge import state

        status = state.get_status()
        data = {
            "listener_url": "http://{}:{}".format(
                status.get("listener_host", "localhost"),
                status.get("listener_port", 8080),
            ),
            "project": unreal.SystemLibrary.get_game_name(),
            "project_dir": unreal.SystemLibrary.get_project_directory(),
            "content_dir": unreal.SystemLibrary.get_project_content_directory(),
            "engine_version": unreal.SystemLibrary.get_engine_version(),
            "current_level": status.get("current_level", ""),
            "last_ping_at": status.get("last_ping_at", ""),
            "last_command": status.get("last_command", ""),
            "last_result": status.get("last_command_status", ""),
            "latest_notification_path": status.get("latest_notification_path", ""),
            "last_result_path": status.get("last_result_path", ""),
        }
        return {
            "success": True,
            "data": data,
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_bridge_self_test(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run quick readiness checks for panel actions and chat handoff."""
    try:
        from mcp_bridge import router, state

        commands = sorted(router.COMMAND_ROUTES.keys())
        safety = state.load_safety()
        required = [
            "ping",
            "test_connection",
            "bridge_status",
            "bridge_latest_notification",
            "bridge_copy_project_info",
            "bridge_command_manifest",
        ]
        missing = [name for name in required if name not in router.COMMAND_ROUTES]
        optimization_commands = [name for name in commands if name.startswith("optimization_")]
        checks = {
            "listener": "ok",
            "required_commands": "ok" if not missing else "missing",
            "optimization_commands": len(optimization_commands),
            "safety": safety,
        }
        return {
            "success": not missing,
            "data": {
                "status": "ready" if not missing else "needs_repair",
                "checks": checks,
                "missing_commands": missing,
                "command_count": len(commands),
                "recommended_chat_handoff": (
                    "Paste the copied MCP Bridge handoff prompt, then ask the chat "
                    "to run test_connection against the listener URL."
                ),
            },
            "error": "" if not missing else "Missing required bridge commands: {}".format(", ".join(missing)),
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_bridge_clear_log(params: Dict[str, Any]) -> Dict[str, Any]:
    """Record a logical output-log cursor for later diagnostics."""
    try:
        import unreal

        timestamp = datetime.datetime.now().isoformat()
        unreal.log(f"[MCP Bridge] Log cursor reset at {timestamp}")
        return {
            "success": True,
            "data": {
                "status": "cursor_reset",
                "timestamp": timestamp,
                "note": "UE4.27 Python cannot truncate or read the Output Log directly, so this records a logical cursor marker.",
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_bridge_command_manifest(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return the command names currently registered in the live listener."""
    try:
        from mcp_bridge import router

        commands = sorted(router.COMMAND_ROUTES.keys())
        return {
            "success": True,
            "data": {
                "count": len(commands),
                "commands": commands,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_restart_listener(params: Dict[str, Any]) -> Dict[str, Any]:
    """Restart the MCP Bridge listener on the next editor tick.

    The restart must not run inline: this handler executes on the game
    thread while the HTTP thread is blocked waiting for this very response.
    Calling listener.stop() here would deadlock until the command timeout
    (HTTPServer.shutdown() waits for the request loop, which waits for us).
    Instead, schedule a one-shot tick callback and return immediately.

    Args:
        params: Optional 'host' (str), 'port' (int), and 'reload' (bool) to
            importlib.reload the listener module before restarting (picks up
            code changes without an editor restart).

    Returns:
        Scheduling status. The actual restart happens within a tick or two;
        callers should re-poll test_connection.
    """
    try:
        import unreal

        host = params.get("host", "localhost")
        port = params.get("port", 8080)
        do_reload = bool(params.get("reload", False))

        state: Dict[str, Any] = {"done": False, "handle": None}

        def _deferred_restart(delta_time: float) -> None:
            if state["done"]:
                return
            state["done"] = True
            try:
                import importlib
                from mcp_bridge import listener
                listener.stop()
                if do_reload:
                    listener = importlib.reload(listener)
                listener.start(host, port)
                unreal.log(f"[MCP Bridge] Listener restarted on {host}:{port} (reload={do_reload})")
            except Exception as exc:
                unreal.log_error(f"[MCP Bridge] Deferred listener restart failed: {exc}")
            finally:
                handle = state.get("handle")
                if handle is not None:
                    try:
                        unreal.unregister_slate_post_tick_callback(handle)
                    except Exception:
                        pass

        state["handle"] = unreal.register_slate_post_tick_callback(_deferred_restart)
        return {
            "success": True,
            "data": {
                "status": "restart_scheduled",
                "host": host,
                "port": port,
                "reload": do_reload,
                "note": "Restart runs on the next editor tick; re-poll test_connection.",
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}
