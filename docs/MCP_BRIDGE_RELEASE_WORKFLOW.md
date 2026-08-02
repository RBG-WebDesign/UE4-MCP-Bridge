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

### What the source zip is, and what it is not

Validate it with:

```bash
npm run test:package
```

That runs the packager into a temporary directory, opens the zip, and checks it.
What it proves: a single `MCPBridge/` root with the descriptor at the top, no
`Binaries`/`Build`/`Intermediate`/`Saved`/`__pycache__`/`.vs`, every module the
descriptor declares shipping its `Build.cs`, the generated `Content/JavaScript`
present, plugin-relative paths inside the 170 character budget, and the version
in the file name coming from the descriptor.

What it is not: **the zip is not a self-sufficient installable artefact**, and
the same script records why, as cases that fail if any of it changes.

1. **`MCPBridge.uplugin` requires the `Puerts` plugin and the zip does not
   contain it.** UE4 refuses to load MCPBridge without it. The recipient has to
   install the pinned PuerTS Unreal_v1.0.9 bundle separately, and nothing inside
   the zip tells them so - `TEAM_SETUP.md`, which is in the zip, never mentions
   PuerTS.
2. **The packager does not build, and does not check that anyone else did.**
   `Content/JavaScript` is produced by `npm run build` and is not in Git. Packaging
   a tree without it produces a zip with no warning, and that release ships a
   plugin whose PuerTS lane has nothing to execute. **Run `npm run build` before
   `package-mcp-bridge.ps1`.**
3. **No `MCPBridgeInstall.json`**, so a project installed from the zip is an
   unmanaged copy of unknown provenance as far as `npm run install:check` is
   concerned.
4. **No binaries.** The target must be a C++ project with UE4.27 and UBT
   available. `-RunUAT` produces a binary package and needs an engine; nothing
   in `npm run test:package` exercises it.

Closing 1 and 2 is what would make the zip droppable. Neither is done. The long
form of this verdict, with what would change it, is in `docs/RELEASE.md`.

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
