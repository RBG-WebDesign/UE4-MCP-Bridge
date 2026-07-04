"""Editor startup script that launches the MCP Bridge listener.

This file should be placed at: YourProject/Content/Python/startup.py
Configure it in DefaultEngine.ini as a startup script for the Python plugin.
"""

import unreal


def _start_mcp_bridge():
    """Start the MCP Bridge listener after a short delay."""
    try:
        from mcp_bridge.listener import start, is_running
        
        if is_running():
            unreal.log("[MCP Bridge] Listener already running, skipping startup")
            try:
                from mcp_bridge.panel import register_menu
                register_menu()
            except Exception as panel_err:
                unreal.log_warning(f"[MCP Bridge] Panel menu registration failed: {panel_err}")
            return
        
        success = start()
        if success:
            unreal.log("[MCP Bridge] Listener started successfully on localhost:8080")
        else:
            unreal.log_error("[MCP Bridge] Failed to start listener")

        try:
            from mcp_bridge.panel import register_menu
            register_menu()
        except Exception as panel_err:
            unreal.log_warning(f"[MCP Bridge] Panel menu registration failed: {panel_err}")
    except Exception as e:
        unreal.log_error(f"[MCP Bridge] Startup error: {str(e)}")


# Start the bridge when this script is loaded
unreal.log("[MCP Bridge] Startup script loaded, initializing...")
_start_mcp_bridge()
