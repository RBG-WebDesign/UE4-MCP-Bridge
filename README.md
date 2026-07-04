# UE4 Bridge

UE4 Bridge is a local automation bridge for Unreal Engine 4.27. It lets Claude Code inspect, create, modify, and test Unreal project content through a local MCP server and a Python listener running inside the Unreal editor.

This repository contains:

- A TypeScript MCP server for Claude Code
- A UE4 Python listener that executes commands in the editor
- C++ UE4 plugin code for Blueprint, Widget Blueprint, Behavior Tree, animation, effects, and gameplay generation helpers
- PromptBrush, a prompt-driven gameplay generation workflow
- Agent and skill documents for repeatable Unreal automation work
- Setup, architecture, troubleshooting, and tool reference docs

## What You Can Do

With Unreal open and the bridge running, Claude Code can help with:

- Spawning, moving, duplicating, organizing, and deleting actors
- Listing, inspecting, creating, compiling, and documenting Blueprints
- Creating and applying materials
- Capturing viewport screenshots and moving the editor camera
- Creating maps and placing actors
- Running Python inside UE4
- Generating gameplay scaffolds with PromptBrush
- Saving levels and validating generated content

## Requirements

- Unreal Engine 4.27
- Node.js 18 or newer
- npm 9 or newer
- Python Editor Script Plugin enabled in UE4
- Claude Code or another MCP-compatible client

Optional, depending on the workflow:

- `uvx` for the `unreal-api` MCP server listed in `.mcp.json`
- Visual Studio with UE4 C++ build tools if you are compiling the C++ plugin

## Quick Start

From this repository root:

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\MyGame\MyGame.uproject"
```

The installer builds the MCP server, installs and enables the unified `MCPBridge` plugin, patches `DefaultEngine.ini`, and writes `.mcp.json` into the target project. Rerun the same command later to update that project. See `docs/MCP_BRIDGE_INSTALLER.md` for installer options and `docs/MCP_BRIDGE_RELEASE_WORKFLOW.md` for packaging.

Manual server build:

```bash
npm install
npm run build
```

Open your UE4 project, then make sure the Python listener is installed and active. Once the editor is running, test the listener:

```bash
curl -X POST http://localhost:8080 -H "Content-Type: application/json" -d "{\"command\":\"ping\"}"
```

Then open Claude Code in this repository folder. The `.mcp.json` file starts the bridge server automatically:

```json
{
  "mcpServers": {
    "unreal-bridge": {
      "command": "node",
      "args": ["mcp-server/dist/index.js"],
      "cwd": "."
    },
    "unreal-api": {
      "command": "uvx",
      "args": ["unreal-api-mcp"],
      "env": {
        "UNREAL_VERSION": "4.27"
      }
    }
  }
}
```

Ask Claude Code:

```text
Test the connection to Unreal Engine.
```

If the bridge is working, Claude should be able to report the engine version, project name, and project paths.

## Unreal Setup (manual, legacy layout)

The installer above is the recommended path; it uses the unified `MCPBridge` plugin. The steps below install the older loose-file layout by hand. The Python code is identical (`unreal-plugin/Content/Python/` mirrors `Plugins/MCPBridge/Content/Python/`), but do not mix the two layouts in one project.

### 1. Enable Python in UE4

1. Open the UE4 editor.
2. Go to `Edit > Plugins`.
3. Search for `Python Editor Script Plugin`.
4. Enable it.
5. Restart the editor.

### 2. Install the Python Listener

Copy this folder:

```text
unreal-plugin/Content/Python/
```

Into your UE4 project:

```text
YourProject/Content/Python/
```

The project should then contain:

```text
Content/
  Python/
    startup.py
    mcp_bridge/
      listener.py
      router.py
      handlers/
      generation/
      utils/
```

### 3. Configure Startup

Add this to your project `Config/DefaultEngine.ini`:

```ini
[/Script/PythonScriptPlugin.PythonScriptPluginSettings]
bDeveloperMode=True
bRemoteExecution=True
+StartupScripts=/Game/Python/startup.py
+AdditionalPaths=(Path="/Game/Python")
```

There is also an example file here:

```text
unreal-plugin/Config/DefaultEngine.ini.example
```

### 4. Restart Unreal

After restart, the listener should start automatically on:

```text
http://localhost:8080
```

## Claude Code Setup

Claude Code reads `.mcp.json` from the repository root. After running `npm run build`, open Claude Code in this folder and use the bridge tools directly.

Default UE Bridge workflow for authoring tasks:

- Make the requested editor change.
- Run only lightweight editor-side sanity checks when useful.
- Let the user test the result in Unreal before starting PIE.
- Do not ask what the user wants next unless there is a blocker or required choice.

Good first requests:

```text
Test the Unreal connection.
List actors in the current level.
Take a viewport screenshot.
List Blueprints under /Game.
Run unreal.SystemLibrary.get_engine_version() in Unreal.
```

If Claude cannot find the MCP server, rebuild it:

```bash
npm run build
```

If Claude can find the MCP server but Unreal commands fail, make sure the UE4 editor is open and the listener is responding on `localhost:8080`.

## PromptBrush

PromptBrush generates Unreal gameplay scaffolding from natural language prompts.

Example prompts:

```text
Make me gameplay like Puzzle Fighter.
Create a main menu, HUD, pause screen, and game over flow.
Generate a simple enemy patrol system with triggers and UI feedback.
```

PromptBrush can create:

- Blueprint classes
- Widget Blueprints
- Materials
- Data assets
- Curves
- Maps
- Input mappings
- JSON build specs and manifests

Generated output is written to:

```text
/Game/Generated/<FeatureName>/
PromptBrushOutput/
```

For the full PromptBrush guide, see:

```text
README_PROMPTBRUSH.md
```

## Common Commands

Install dependencies:

```bash
npm install
```

Build the MCP server:

```bash
npm run build
```

Run the server in development mode:

```bash
npm run dev
```

Run tests:

```bash
npm test
```

Run integration tests:

```bash
npm run test:integration
```

## Repository Layout

```text
mcp-server/
  TypeScript MCP server used by Claude Code.

unreal-plugin/
  Python listener and command handlers that run inside UE4.

ue4-plugin/
  C++ UE4 plugin code for advanced Blueprint, Widget Blueprint,
  Behavior Tree, animation, effects, and gameplay generation tools.

docs/
  Setup, architecture, troubleshooting, tool reference, specs, and plans.

agents/
  Agent role documents for Unreal automation workflows.

skills/
  Reusable procedure files for generating, validating, and repairing content.

tests/
  Integration and workflow tests.

PromptBrushOutput/
  Generated PromptBrush specs and manifests.
```

In this local project workspace you may also see Unreal project folders such as `Content`, `Config`, `Source`, `Plugins`, `Saved`, `Intermediate`, and `Binaries`. Those are the live UE project folders and may not all be tracked by Git.

## How It Works

```text
Claude Code
  |
  | MCP over stdio
  v
mcp-server
  |
  | HTTP POST to localhost:8080
  v
UE4 Python listener
  |
  | UE4 Python API on the editor game thread
  v
Unreal Editor
```

The MCP server validates tool calls and forwards structured requests to the Python listener. The listener queues work safely onto the UE4 editor thread, executes the command, and sends results back to Claude Code.

## Useful Workflows

### Inspect a Level

1. Open the level in UE4.
2. Ask Claude to list actors.
3. Ask for a viewport screenshot.
4. Ask Claude to summarize the current layout.

### Create or Modify Actors

1. Ask Claude to spawn or move actors.
2. Use viewport focus or screenshot tools to inspect the result.
3. Ask Claude to adjust placement, scale, rotation, folder organization, or materials.
4. Test the result in Unreal when the edit is complete.
5. Save the level when the result is correct.

### Build Gameplay Content

1. Describe the gameplay feature.
2. Ask Claude to generate a build spec or use PromptBrush.
3. Create Blueprints, widgets, maps, and supporting assets.
4. Compile and validate.
5. Capture screenshots or inspect assets.
6. Test the result in Unreal.
7. Save the level and generated assets.

## Troubleshooting

### Claude cannot see the bridge tools

Run:

```bash
npm run build
```

Then reopen Claude Code in the repository root.

### Bridge tools exist but Unreal commands fail

Check that:

- UE4 editor is open
- Python Editor Script Plugin is enabled
- `Content/Python/startup.py` exists in the UE project
- The listener responds on `localhost:8080`

Test:

```bash
curl -X POST http://localhost:8080 -H "Content-Type: application/json" -d "{\"command\":\"ping\"}"
```

### Listener does not start

Check `Config/DefaultEngine.ini` and confirm this setting exists:

```ini
StartupScripts=/Game/Python/startup.py
```

Restart the editor after changing Python plugin settings or startup scripts.

### C++ plugin tools are missing

Make sure the plugin in `ue4-plugin/` has been copied or linked into your UE project `Plugins` folder, compiled, enabled in the editor, and loaded after restart.

## More Documentation

- `docs/SETUP.md`
- `docs/ARCHITECTURE.md`
- `docs/TOOL_REFERENCE.md`
- `docs/TROUBLESHOOTING.md`
- `README_PROMPTBRUSH.md`
- `ue4-plugin/README.md`
- `unreal-plugin/README.md`

## Current Project Notes

This repository is configured to use:

```text
https://github.com/RBG-WebDesign/UE4_Bridge.git
```

The default branch is:

```text
main
```
