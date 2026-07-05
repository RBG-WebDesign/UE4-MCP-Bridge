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

## Connect an MCP Client (Claude Code, Codex, Cursor)

The plugin hosts the in-editor listener on `http://localhost:8080`. To drive it
from an AI coding agent you also need the companion MCP server (TypeScript),
which translates MCP tool calls into listener commands. It is not part of this
plugin package. Get it from the project repository:

```text
https://github.com/RBG-WebDesign/UE4_Bridge
```

From that repository root:

```bash
npm install
npm run build
```

Then point your MCP client at `mcp-server/dist/index.js` (the repository's
`.mcp.json` shows the exact configuration), or run
`Scripts/install-mcp-bridge.ps1 <YourProject.uproject>` to install the plugin,
patch your project settings, and write `.mcp.json` in one step.

The full tool reference (150+ tools) is in the repository under
`docs/TOOL_REFERENCE.md`.
