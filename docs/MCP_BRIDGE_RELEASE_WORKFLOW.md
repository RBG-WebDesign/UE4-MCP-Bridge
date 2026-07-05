# MCP Bridge Release Workflow

MCP Bridge now has a canonical product plugin at:

```text
Plugins/MCPBridge/
```

This folder is the plugin that should be developed, installed into test projects, packaged, and eventually uploaded to Fab.

## Development Loop

1. Edit code under `Plugins/MCPBridge/`.
2. Install into a test project:

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\CodePlayground\CodePlayground.uproject"
```

3. Restart Unreal and accept the rebuild prompt.
4. Verify `Window > MCP Bridge`.
5. Verify `localhost:8080` responds to `ping`.

## Versioning

Before packaging a release, update `Plugins/MCPBridge/MCPBridge.uplugin`:

```json
"Version": 4,
"VersionName": "0.3.1"
```

Use `Version` as a monotonically increasing integer and `VersionName` as the human-readable release number.

## Package A Release

Create a clean source plugin zip:

```powershell
.\Scripts\package-mcp-bridge.ps1
```

Output goes to:

```text
Releases/
  MCPBridge_UE4.27.0_v0.3.0.zip
```

If you want Unreal Automation Tool to package the plugin, pass the UE4.27 RunUAT path:

```powershell
.\Scripts\package-mcp-bridge.ps1 -RunUAT "C:\Program Files\Epic Games\UE_4.27\Engine\Build\BatchFiles\RunUAT.bat"
```

The source zip intentionally excludes:

```text
Binaries/
Build/
Intermediate/
Saved/
DerivedDataCache/
```

## Fab Notes

For Fab-style distribution, keep the uploaded plugin folder clean:

```text
MCPBridge/
  Config/
  Content/
  Docs/
  Source/
  MCPBridge.uplugin
```

Do not upload a whole game project for the plugin listing unless Fab specifically requests a project wrapper. For code plugins, test packaging through Unreal before upload.
