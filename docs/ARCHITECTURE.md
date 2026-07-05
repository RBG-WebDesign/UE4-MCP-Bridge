# Architecture

## System Overview

```
Claude Code (MCP Client)
    |
    |  stdio (JSON-RPC)
    |
MCP Server (TypeScript, local)
    |
    |  HTTP on localhost:8080
    |
Python Listener (inside UE4.27 Editor)
    |
    +-- Executes against UE4 Python API
```

## Components

### MCP Server (`mcp-server/`)
TypeScript process using `@modelcontextprotocol/sdk`. Registers tools with Zod input schemas, validates parameters, manages undo/redo/checkpoint history, and forwards execution requests to the Python listener over HTTP.

Does not know how Unreal works internally. Sends structured JSON, returns text results.

### Python Listener (`Plugins/MCPBridge/Content/Python/`)
Python script running inside the UE4.27 editor process. Starts an HTTP server on localhost:8080, receives JSON command payloads, and executes them using the `unreal` module.

Uses a background thread for HTTP with a thread-safe queue to marshal commands to UE4's game thread via `register_slate_post_tick_callback`.

### Configuration (`.mcp.json`)
Tells Claude Code how to start and connect to the MCP server via stdio.

## Data Flow

1. Claude Code calls an MCP tool (e.g., `actor_spawn`)
2. MCP server validates input against Zod schema
3. MCP server POSTs JSON to `http://localhost:8080`
4. Python listener receives the request on background thread
5. Command is queued and picked up on the game thread
6. Handler executes `unreal` API calls
7. Result flows back: handler -> listener -> MCP server -> Claude Code

## Threading Model
UE4's Python environment runs on the game thread. The HTTP server runs on a background thread. All `unreal` module calls must be marshaled to the game thread through a thread-safe queue pattern.

See `.claude/skills/bridge-http-protocol.md` for the full protocol spec.
