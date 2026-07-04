"""System command handlers: ping, test_connection, python_proxy, ue_logs, restart."""

import sys
import io
import traceback
import datetime
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
    """Restart the MCP Bridge listener.
    
    Args:
        params: Optional 'host' (str) and 'port' (int).
    
    Returns:
        Restart status.
    """
    try:
        from mcp_bridge.listener import restart
        host = params.get("host", "localhost")
        port = params.get("port", 8080)
        success = restart(host, port)
        return {
            "success": success,
            "data": {
                "status": "restarted" if success else "restart_failed",
                "host": host,
                "port": port,
            }
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}
