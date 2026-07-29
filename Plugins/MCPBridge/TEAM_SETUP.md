# MCPBridge Team Setup

The plugin is project-local and must be submitted as one unit. Submit these
paths together whenever a module is added or changed:

- `Plugins/MCPBridge/MCPBridge.uplugin`
- `Plugins/MCPBridge/Source/...`
- `Plugins/MCPBridge/Content/Python/...`
- `Plugins/MCPBridge/Binaries/Win64/UE4Editor.modules`
- `Plugins/MCPBridge/Binaries/Win64/UE4Editor-*.dll`

PDB files and plugin `Intermediate` files remain ignored.

After syncing, close Unreal Editor and run `Build-MCPBridge.bat` if Unreal
reports a missing or incompatible module. The script discovers the registered
UE4.27 installation and builds the full `Sinfeld_DemoEditor` target. Teammates
who use the same UE4.27 launcher build can normally use the submitted DLLs
without rebuilding.

Do not submit only `MCPBridge.uplugin`. A manifest that names a module without
its matching source or DLL causes Unreal's missing-modules dialog.
