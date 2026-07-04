"""HTTP listener that runs inside the UE4.27 editor process.

Starts an HTTP server on a background thread (localhost:8080) and marshals
incoming commands to the game thread via a thread-safe queue and
unreal.register_slate_post_tick_callback.
"""

import json
import threading
import time
import traceback
import queue
from http.server import HTTPServer, BaseHTTPRequestHandler
from typing import Any, Dict, Optional, Callable

# Globals
_server: Optional[HTTPServer] = None
_server_thread: Optional[threading.Thread] = None
_command_queue: queue.Queue = queue.Queue()
_response_map: Dict[int, threading.Event] = {}
_response_data: Dict[int, Dict[str, Any]] = {}
_request_counter: int = 0
_counter_lock: threading.Lock = threading.Lock()
_tick_handle: Optional[object] = None

_start_time: float = 0.0
_last_event_timestamp: float = 0.0
_last_event_command: str = ""
_last_event_result: str = ""
_last_event_duration_ms: float = 0.0

# Subsystem detection (cached at startup on game thread)
_blueprint_builder_loaded: bool = False
_blueprint_builder_version: str = ""
_widget_blueprint_builder_loaded: bool = False
_widget_blueprint_builder_version: str = ""
_shaderweave_registered: bool = False
_shaderweave_active_sessions: int = 0

HOST = "localhost"
PORT = 8080


def _get_next_request_id() -> int:
    """Thread-safe request ID generator."""
    global _request_counter
    with _counter_lock:
        _request_counter += 1
        return _request_counter


class BridgeRequestHandler(BaseHTTPRequestHandler):
    """Handles incoming HTTP requests from the MCP server."""

    def log_message(self, format: str, *args: Any) -> None:
        """Route HTTP logs to UE4's output log.

        Suppresses GET health-check spam (/status, /ping, /) to keep the
        output log readable. POST commands always log.
        """
        message = format % args
        # Skip logging GET health checks -- they fire constantly from the
        # MCP host and drown out useful command traffic.
        if '"GET ' in message:
            return
        try:
            import unreal
            unreal.log(f"[MCP Bridge HTTP] {message}")
        except ImportError:
            print(f"[MCP Bridge HTTP] {message}")

    def do_POST(self) -> None:
        """Handle POST requests containing JSON commands."""
        try:
            content_length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(content_length).decode("utf-8")

            try:
                payload = json.loads(body)
            except json.JSONDecodeError as e:
                self._send_error(400, f"Invalid JSON: {str(e)}")
                return

            command = payload.get("command")
            if not command:
                self._send_error(400, "Missing 'command' field")
                return

            params = payload.get("params", {})

            # Queue the command for game thread execution
            request_id = _get_next_request_id()
            event = threading.Event()
            _response_map[request_id] = event

            _command_queue.put({
                "id": request_id,
                "command": command,
                "params": params,
            })

            # Wait for the game thread to process and respond
            # Timeout after 60 seconds to prevent hanging
            if event.wait(timeout=60.0):
                result = _response_data.pop(request_id, {
                    "success": False,
                    "data": {},
                    "error": "Response data missing"
                })
                del _response_map[request_id]
            else:
                # Timeout
                _response_map.pop(request_id, None)
                _response_data.pop(request_id, None)
                result = {
                    "success": False,
                    "data": {},
                    "error": "Command timed out after 60 seconds"
                }

            self._send_json(200, result)

        except Exception as e:
            self._send_error(500, f"Internal server error: {str(e)}")

    def do_GET(self) -> None:
        """Handle GET requests: /ping, /status, / (health check)."""
        path = self.path.rstrip("/")

        if path == "/ping":
            self._send_json(200, {
                "success": True,
                "data": {"ok": True}
            })
        elif path == "/status":
            self._handle_status()
        else:
            # Backward-compatible health check
            self._send_json(200, {
                "success": True,
                "data": {"status": "ok", "message": "MCP Bridge listener is running"}
            })

    def _handle_status(self) -> None:
        """Return full bridge status. Reads thread-safe module-level vars only."""
        uptime = time.time() - _start_time if _start_time > 0 else 0.0

        last_event = None
        if _last_event_timestamp > 0:
            last_event = {
                "timestamp": _last_event_timestamp,
                "command": _last_event_command,
                "result": _last_event_result,
                "duration_ms": round(_last_event_duration_ms, 1),
            }

        self._send_json(200, {
            "success": True,
            "data": {
                "version": "0.1.0",
                "protocol_version": 1,
                "bridge": {
                    "running": True,
                    "port": PORT,
                    "uptime_sec": round(uptime, 1),
                    "total_requests": _request_counter,
                    "server_time": time.time(),
                },
                "last_event": last_event,
                "subsystems": {
                    "blueprint_builder": {
                        "loaded": _blueprint_builder_loaded,
                        "version": _blueprint_builder_version,
                    },
                    "widget_blueprint_builder": {
                        "loaded": _widget_blueprint_builder_loaded,
                        "version": _widget_blueprint_builder_version,
                    },
                    "shaderweave": {
                        "registered": _shaderweave_registered,
                        "active_sessions": _shaderweave_active_sessions,
                    },
                },
            }
        })

    def _send_json(self, status: int, data: Dict[str, Any]) -> None:
        """Send a JSON response."""
        response_body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response_body)))
        self.end_headers()
        self.wfile.write(response_body)

    def _send_error(self, status: int, message: str) -> None:
        """Send a JSON error response."""
        self._send_json(status, {
            "success": False,
            "data": {},
            "error": message
        })


def _process_command_queue(delta_time: float) -> None:
    """Tick callback that processes queued commands on the game thread.
    
    This function is registered with unreal.register_slate_post_tick_callback
    and runs every editor tick on the game thread, where unreal API calls are safe.
    """
    from mcp_bridge.router import route_command
    global _last_event_timestamp, _last_event_command, _last_event_result, _last_event_duration_ms

    # Process up to 10 commands per tick to avoid blocking the editor
    processed = 0
    while not _command_queue.empty() and processed < 10:
        try:
            item = _command_queue.get_nowait()
        except queue.Empty:
            break

        request_id = item["id"]
        command = item["command"]
        params = item["params"]

        cmd_start = time.time()

        try:
            result = route_command(command, params)
        except Exception as e:
            result = {
                "success": False,
                "data": {},
                "error": f"Handler error: {str(e)}\n{traceback.format_exc()}"
            }

        cmd_duration = (time.time() - cmd_start) * 1000.0  # ms
        _last_event_timestamp = time.time()
        _last_event_command = command
        _last_event_result = "success" if result.get("success", False) else "error"
        _last_event_duration_ms = cmd_duration

        # Store result and signal the waiting HTTP thread
        _response_data[request_id] = result
        event = _response_map.get(request_id)
        if event:
            event.set()

        processed += 1


def _detect_subsystems() -> None:
    """Detect available subsystems. Runs once on game thread at startup."""
    global _blueprint_builder_loaded, _blueprint_builder_version
    global _widget_blueprint_builder_loaded, _widget_blueprint_builder_version

    try:
        import unreal
        try:
            bp_class = getattr(unreal, 'BlueprintGraphBuilderLibrary', None)
            _blueprint_builder_loaded = bp_class is not None
            if _blueprint_builder_loaded:
                _blueprint_builder_version = "0.1.0"
        except Exception:
            _blueprint_builder_loaded = False

        try:
            wb_class = getattr(unreal, 'WidgetBlueprintBuilderLibrary', None)
            _widget_blueprint_builder_loaded = wb_class is not None
            if _widget_blueprint_builder_loaded:
                _widget_blueprint_builder_version = "0.1.0"
        except Exception:
            _widget_blueprint_builder_loaded = False

        unreal.log(f"[MCP Bridge] Subsystem detection: BlueprintBuilder={_blueprint_builder_loaded}, WidgetBuilder={_widget_blueprint_builder_loaded}")
    except ImportError:
        pass


def start(host: str = HOST, port: int = PORT) -> bool:
    """Start the MCP Bridge listener.
    
    Args:
        host: Hostname to bind to. Default: localhost
        port: Port to bind to. Default: 8080
    
    Returns:
        True if the server started successfully, False if already running.
    """
    global _server, _server_thread, _tick_handle, _start_time

    if _server is not None:
        try:
            import unreal
            unreal.log_warning("[MCP Bridge] Listener already running")
        except ImportError:
            print("[MCP Bridge] Listener already running")
        return False

    try:
        _start_time = time.time()
        _server = HTTPServer((host, port), BridgeRequestHandler)
        _server_thread = threading.Thread(
            target=_server.serve_forever,
            name="MCPBridgeListener",
            daemon=True,
        )
        _server_thread.start()

        # Register tick callback for game thread command processing
        try:
            import unreal
            _tick_handle = unreal.register_slate_post_tick_callback(_process_command_queue)
            _detect_subsystems()
            unreal.log(f"[MCP Bridge] Listener started on {host}:{port}")
        except ImportError:
            print(f"[MCP Bridge] Listener started on {host}:{port} (no UE4 tick callback)")

        return True

    except OSError as e:
        try:
            import unreal
            unreal.log_error(f"[MCP Bridge] Failed to start listener: {str(e)}")
        except ImportError:
            print(f"[MCP Bridge] Failed to start listener: {str(e)}")
        _server = None
        _server_thread = None
        return False


def stop() -> bool:
    """Stop the MCP Bridge listener.
    
    Returns:
        True if stopped successfully, False if not running.
    """
    global _server, _server_thread, _tick_handle

    if _server is None:
        return False

    # Unregister tick callback
    if _tick_handle is not None:
        try:
            import unreal
            unreal.unregister_slate_post_tick_callback(_tick_handle)
        except (ImportError, Exception):
            pass
        _tick_handle = None

    # Shutdown HTTP server
    _server.shutdown()
    if _server_thread is not None:
        _server_thread.join(timeout=5.0)

    _server = None
    _server_thread = None

    try:
        import unreal
        unreal.log("[MCP Bridge] Listener stopped")
    except ImportError:
        print("[MCP Bridge] Listener stopped")

    return True


def restart(host: str = HOST, port: int = PORT) -> bool:
    """Restart the MCP Bridge listener.
    
    Returns:
        True if restarted successfully.
    """
    stop()
    return start(host, port)


def is_running() -> bool:
    """Check if the listener is currently running."""
    return _server is not None
