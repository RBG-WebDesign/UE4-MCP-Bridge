# Fab Listing Materials - MCP Bridge (UE 4.27)

Ready-to-paste copy for the Fab publisher portal. Keep the Technical Details
section verbatim unless the architecture changes; it preempts Epic QA
questions about the local listener and companion server.

## Product Title

MCP Bridge - AI Editor Automation for UE 4.27

## Short Description

Drive the Unreal Editor with AI coding agents (Claude Code, Codex, Cursor)
over the Model Context Protocol. 150+ tools: Blueprint graph editing with
schema-validated pin connections, gameplay framework generation, Behavior
Trees from JSON, C++ class generation with build feedback, input mappings,
camera rigs, material instances, DataTables, and project intelligence search.

## Long Description

MCP Bridge turns the UE 4.27 editor into a workspace an AI coding agent can
operate safely. An in-editor Python listener executes commands on the game
thread; C++ modules handle everything Python reflection cannot reach
(Blueprint graph mutation, Behavior Tree internals, input mapping structs).

Every mutation is transactional (Ctrl+Z works), compiles and saves the
affected asset, and reports failure explicitly - a broken Blueprint is never
reported as success. Pin connections are validated through
UEdGraphSchema_K2::CanCreateConnection before linking, so incompatible
connections fail cleanly with no graph change.

Highlights:

- Blueprint editing: variables, functions, event dispatchers, interfaces,
  components, 37 registered graph node types, pin connect/break
- Game skeleton generation: GameMode/Character/PlayerController/HUD with
  class defaults wired, camera rig presets, input control schemes
- AI: Blackboards with typed keys, Behavior Trees built from JSON
  (26 node types), navigation rebuild
- C++: class generation (17 parent classes) and UnrealBuildTool compilation
  as a background job with structured file/line/code error reporting
- Content: materials and material instances with parameter overrides,
  DataTables (dialog-free JSON row import), audio components, map creation
- Project intelligence: searchable asset/Blueprint/material index and
  gameplay pattern search
- Dockable status panel (Window > MCP Bridge) with connection health,
  self-test, and copy-paste agent handoff prompts

## Technical Details & Network Disclosure

- **Module Type:** Editor only. Both C++ modules are Type "Editor" and are
  compiled out of shipped retail builds entirely. Nothing in this plugin
  runs in a packaged game.
- **Local Networking:** The plugin starts a local-only HTTP listener bound
  to localhost:8080 inside the editor process. It accepts connections only
  from the local machine. No project data, source code, or telemetry is
  transmitted to any external server; all communication stays on the user's
  machine. The listener can be stopped/restarted from the MCP Bridge panel.
- **External Dependencies:** Driving the editor from an AI agent requires a
  companion MCP server (Node.js/TypeScript) that runs locally and translates
  MCP tool calls into listener commands. It is MIT-licensed with full source
  and setup instructions at https://github.com/RBG-WebDesign/MCPBridge-Server.
  The plugin itself is fully functional for panel-based use without it.
- **Engine APIs:** UE 4.27 only. No UE5-only APIs are used (enforced by an
  automated token scan in CI).
- **Python:** Requires the built-in Python Editor Script Plugin (enabled
  automatically as a plugin dependency).

## Requirements (listing fields)

- Engine version: 4.27
- Platforms: Win64 (editor)
- Network: localhost only, port 8080 (configurable at start())
- Companion tooling: Node.js 18+ for the MCP server (separate download)

## Suggested Category / Tags

Category: Editor Tools
Tags: automation, AI, MCP, blueprint, editor scripting, pipeline, tools

## Submission Checklist (per release)

1. Bump Version/VersionName in MCPBridge.uplugin.
2. Run: Scripts/package-mcp-bridge.ps1 -OutputRoot <dir> -SourceZipOnly
3. Sanity: zip has single MCPBridge/ root, no Binaries/Intermediate/Saved/
   __pycache__/.vs, Icon128.png present (the script asserts this).
4. Optional full check: add -RunUAT <path to RunUAT.bat> for a Rocket build.
5. Upload zip to cloud storage with "anyone with the link" download access.
6. Paste the link + this document's copy into the publisher portal, marked
   as the UE 4.27 engine-specific submission.

Note on source: Fab code plugins are distributed to buyers with full Source/
included - Epic's build farm compiles from it. Repository visibility is a
separate decision from plugin source visibility.
