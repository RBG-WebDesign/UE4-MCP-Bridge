# MCP Bridge Quick Start

MCP Bridge is an Unreal Engine 4.27 editor plugin that starts a local Python listener, exposes editor automation commands to an MCP server, and provides a dockable `Window > MCP Bridge` status panel.

## Install In A Project

Copy the plugin folder into your project:

```text
YourProject/
  Plugins/
    MCPBridge/
```

Enable these plugins in the editor or in your `.uproject`:

```text
MCPBridge
PythonScriptPlugin
EditorScriptingUtilities
```

Add these settings to `Config/DefaultEngine.ini`:

```ini
[/Script/PythonScriptPlugin.PythonScriptPluginSettings]
bDeveloperMode=True
bRemoteExecution=True
+StartupScripts=startup.py
+AdditionalPaths=(Path="YourProject/Plugins/MCPBridge/Content/Python")
```

Restart Unreal. If prompted, rebuild the plugin.

## Verify

After Unreal restarts, open:

```text
Window > MCP Bridge
```

The Connection card shows the bridge channel, such as
`MCP Bridge HTTP on http://localhost:8080`. Click `Copy Chat Prompt` to copy a
ready-to-paste prompt with the listener URL, project, level, and panel status.

Then test the listener:

```powershell
curl -X POST http://localhost:8080 -H "Content-Type: application/json" -d "{\"command\":\"ping\"}"
```

The MCP server is distributed separately in this repository under `mcp-server/`. For local development, use `Scripts/install-mcp-bridge.ps1` from the repository root to install the plugin and generate `.mcp.json`.
