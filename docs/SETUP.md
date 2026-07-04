# Setup Guide

## Prerequisites
- Unreal Engine 4.27 (Epic Games Launcher install)
- Node.js 18 or later
- npm 9 or later
- A UE4 project with the Python Editor Script Plugin enabled

## Recommended Install
Use the installer for new projects and updates:

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\MyGame\MyGame.uproject"
```

The installer builds the MCP server, copies the unified `MCPBridge` plugin, enables it in the `.uproject`, updates `DefaultEngine.ini`, and writes a project-local `.mcp.json`.

For update details and options, see `docs/MCP_BRIDGE_INSTALLER.md`. For release packaging, see `docs/MCP_BRIDGE_RELEASE_WORKFLOW.md`.

## Manual Install (legacy layout)
Use these steps only when you need to copy files by hand. This installs the older loose-file layout (`Content/Python/` in your project) instead of the unified `MCPBridge` plugin the installer uses. The Python code is identical; `unreal-plugin/Content/Python/` mirrors `Plugins/MCPBridge/Content/Python/`. Do not mix the two layouts in one project: if you later run the installer, it removes the legacy `StartupScripts` entry and switches the project to the plugin layout.

## Step 1: Enable Python in UE4
1. Open your UE4 project in the editor
2. Go to Edit > Plugins
3. Search for "Python Editor Script Plugin"
4. Enable it and restart the editor

## Step 2: Install the Python Listener
Copy the contents of `unreal-plugin/Content/Python/` into your UE4 project's `Content/Python/` folder.

Your project folder should look like:
```
YourProject/
  Content/
    Python/
      startup.py
      mcp_bridge/
        __init__.py
        listener.py
        router.py
        handlers/
        utils/
```

## Step 3: Configure Editor Startup
Add to your project's `DefaultEngine.ini` under `[/Script/PythonScriptPlugin.PythonScriptPluginSettings]`:
```ini
bDeveloperMode=True
bRemoteExecution=True
StartupScripts=/Game/Python/startup.py
```

Or see `unreal-plugin/Config/DefaultEngine.ini.example` for the full config block.

## Step 4: Build the MCP Server
```
cd unreal-mcp-bridge
npm install
npm run build
```

## Step 5: Test the Connection
1. Restart the UE4 editor (the Python listener starts automatically)
2. Verify the listener is running:
   ```
   curl -X POST http://localhost:8080 -H "Content-Type: application/json" -d "{\"command\":\"ping\"}"
   ```
3. Open Claude Code in the `unreal-mcp-bridge` directory
4. Ask Claude to "test connection to Unreal Engine"

## Step 6: Verify
Claude should be able to execute Python inside Unreal. Try asking:
"Run `unreal.SystemLibrary.get_game_name()` in Unreal"
