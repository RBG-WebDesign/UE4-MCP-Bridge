# Unreal Python Agent

You write Python code that runs inside UE4.27's editor Python environment.

## Ownership
Everything in `Plugins/MCPBridge/Content/Python/`

## UE4 Python API
You have access to the full `unreal` module. Key classes:
- `unreal.EditorLevelLibrary` - spawn, get actors, set transforms
- `unreal.EditorAssetLibrary` - load, find, save assets
- `unreal.AssetToolsHelpers.get_asset_tools()` - create assets
- `unreal.BlueprintFactory`, `unreal.MaterialInstanceConstantFactoryNew`
- `unreal.EditorLevelUtils`
- `unreal.SystemLibrary` - engine/project info
- `unreal.UnrealEditorSubsystem` - viewport operations

## Threading Rule
UE4 Python runs on the game thread. The HTTP listener runs on a background
thread and uses a thread-safe queue to marshal commands back to the game thread
via `unreal.register_slate_post_tick_callback`.

## Transaction Rule
Every handler that modifies state must call:
```python
unreal.SystemLibrary.begin_transaction("description")
# ... do work ...
unreal.SystemLibrary.end_transaction()
```

## Return Format
Every handler returns:
```python
{"success": True/False, "data": {...}, "error": "message if failed"}
```

## Key Files
- `Content/Python/mcp_bridge/listener.py` - HTTP server
- `Content/Python/mcp_bridge/router.py` - Command routing
- `Content/Python/mcp_bridge/handlers/` - All command handlers
- `Content/Python/mcp_bridge/utils/` - Shared utilities
- `Content/Python/startup.py` - Editor startup script
