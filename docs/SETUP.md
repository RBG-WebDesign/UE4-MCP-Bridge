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

## Manual Install
Use these steps only when you need to copy files by hand. They install the same unified `MCPBridge` plugin the installer uses.

## Step 1: Enable Python in UE4
1. Open your UE4 project in the editor
2. Go to Edit > Plugins
3. Search for "Python Editor Script Plugin"
4. Enable it and restart the editor

## Step 2: Install the MCPBridge Plugin
Copy `Plugins/MCPBridge/` into your UE4 project's `Plugins/` folder.

Your project folder should look like:
```
YourProject/
  Plugins/
    MCPBridge/
      MCPBridge.uplugin
      Content/
        Python/
          startup.py
          mcp_bridge/
      Source/
        MCPBridgePanel/
        BlueprintGraphBuilder/
```

Enable the `MCPBridge` plugin in Edit > Plugins (or add it to your `.uproject`). The C++ modules compile the next time you build the project.

## Step 3: Configure Editor Startup
Add to your project's `DefaultEngine.ini` under `[/Script/PythonScriptPlugin.PythonScriptPluginSettings]`, replacing `<ProjectRoot>` with your project's absolute path:
```ini
bDeveloperMode=True
bRemoteExecution=True
+StartupScripts=startup.py
+AdditionalPaths=(Path="<ProjectRoot>/Plugins/MCPBridge/Content/Python")
```

Or see `Plugins/MCPBridge/Config/DefaultEngine.ini.example` for the full config block.

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
