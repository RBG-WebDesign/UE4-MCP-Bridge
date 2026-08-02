# MCP Bridge Installer

Use `Scripts/install-mcp-bridge.ps1` to install or update the MCP Bridge in any UE4.27 project.

The installer keeps one shared bridge checkout as the source of truth, then updates the target project with the files Unreal needs locally.

## What It Installs

Into the target Unreal project:

```text
Plugins/MCPBridge/
.mcp.json
Config/DefaultEngine.ini Python startup settings
The project `.uproject` plugin list
```

The MCP server stays in this bridge repository. The generated `.mcp.json` points back to the shared `mcp-server/dist/index.js` with an absolute path, so updating the shared bridge updates the MCP server for every installed project. The installer also enables `MCPBridge`, `PythonScriptPlugin`, and `EditorScriptingUtilities` in the target `.uproject`.

The installer registers `startup.py` through the copied plugin Python directory, for example:

```ini
[/Script/PythonScriptPlugin.PythonScriptPluginSettings]
bDeveloperMode=True
bRemoteExecution=True
+StartupScripts=startup.py
+AdditionalPaths=(Path="D:/Unreal Projects/MyGame/Plugins/MCPBridge/Content/Python")
```

## Install

From this repository root:

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\MyGame\MyGame.uproject"
```

You can also pass the project directory:

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\MyGame"
```

The script runs `npm install` if `node_modules` is missing, then runs `npm run build`.

## Update Later

Pull or copy the latest bridge source, then rerun the same command:

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\MyGame\MyGame.uproject"
```

For a cleaner update of managed bridge folders:

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\MyGame\MyGame.uproject" -CleanManaged
```

`-CleanManaged` replaces the managed `Plugins/MCPBridge` folder before copying fresh files. It does not delete unrelated project folders.

## Useful Options

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\MyGame\MyGame.uproject" -SkipBuild
```

Use `-SkipBuild` if the MCP server was already built.

`-SkipCppPlugin` and `-SkipPanelPlugin` are legacy options from the older split-plugin layout. The current installer uses one product plugin, `Plugins/MCPBridge`.

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\MyGame\MyGame.uproject" -IncludeUnrealApi
```

Use `-IncludeUnrealApi` to add the optional `unreal-api` MCP server entry.

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\MyGame\MyGame.uproject" -WhatIf
```

Use `-WhatIf` to preview file changes.

## Running Python Files In Commandlets

UE4.27 treats `-script=` as Python source text, not as a file path. Use the helper when you want to run a `.py` file through `UE4Editor-Cmd.exe`:

```powershell
.\Scripts\run-ue-python-file.ps1 "D:\Unreal Projects\MyGame\MyGame.uproject" ".\Scripts\some_editor_task.py"
```

## Keeping a test project in step with this checkout

The installer copies the plugin. Nothing used to check that the copy stayed the
copy, and it did not: two UE4.27 projects carried an MCPBridge install, only one
was current, and the stale one was a day behind with no sign of it. A live
acceptance run against that project would have passed and proved nothing about
the code under review.

`Scripts/bridge-install.mjs` closes that. It writes an install manifest,
`Plugins/MCPBridge/MCPBridgeInstall.json`, into the target project recording the
bridge commit, whether the tree was dirty, the PuerTS pin, per-group file hashes,
the compiled DLL hashes, and both paths.

```bash
npm run install:check -- --project "D:\Unreal Projects\MyGame"
```

```bash
npm run install:sync -- --project "D:\Unreal Projects\MyGame"
```

`install:check` is read-only and never writes to the target project. That is the
reason the two verbs are separate: a verification run that quietly repairs what
it was asked to measure cannot fail, and a gate that cannot fail is not a gate.
`install:sync` copies the declared plugin files whose content actually differs,
builds the target editor, then regenerates the manifest and re-checks.

The comparison is by content, not by timestamp. UBT reported `Target is up to
date` for a project whose plugin sources were a day old, and it was right from
where it stood: UBT compares each project's own sources against its own object
files and has no idea a repository exists.

What is checked, and why each is separate:

| Group | Rejected when |
|---|---|
| `uplugin`, `build_cs`, `native_source`, `resources`, `plugin_misc` | any file differs from this checkout |
| `content_javascript` | any file differs; separate from native source because tsc and UBT are different producers and either can be stale while the other is current |
| `content_other` | any file differs |
| `puerts_pin` | a pinned file is missing or altered, or an unpinned file appears under the bundle's `Content/` or `Binaries/` |
| dll | a DLL is not the binary built at install time, or the sources are newer than every DLL (synced but never rebuilt) |
| catalog | the installed `Content/JavaScript/registry.js` does not expose the native commands the MCP server advertises |

Extra files elsewhere in the PuerTS bundle are counted and named, not treated as
a violation. `Tests\UE427PuerTSMCP` carries 63 of them - `ThirdParty/Libnode_APL.xml`
and Android static libraries under `ThirdParty/nodejs_16/lib` - which are
upstream's own files from the same pinned tag; that bundle is simply a more
complete extraction. All 1038 pinned files there are byte-identical. They cannot
change what the pinned files do on a Win64 editor, and the only way to remove
them is deleting files from someone's project directory, which a verification
gate has no business doing on its own.

Every live acceptance script calls the gate before it opens a connection, so a
stale install fails with a named group instead of producing a green run.
`MCP_SKIP_INSTALL_CHECK=1` turns it off for debugging the gate itself and says
so loudly on stdout.

Known targets:

| Project | Status |
|---|---|
| `D:\Unreal Projects\BridgeInstallTest` | active, verified, synchronised |
| `D:\Unreal Projects\MASTER_PROJECT\SF_Repository\Sinfeld_240301\Tests\UE427PuerTSMCP` | active, verified, synchronised |

A project with no `MCPBridgeInstall.json` is rejected as an unmanaged copy of
unknown provenance rather than treated as current. To retire a target instead of
maintaining it, set `"status": "deprecated"` and a `"deprecated_reason"` in its
manifest; the gate then blocks it from acceptance even when its files are current.

The gate's own acceptance is `Scripts/bridge-install-acceptance.mjs`. It clones a
passing install and breaks one thing per case, because a gate that has only been
run against a correct install has not been shown to reject anything:

```bash
node Scripts/bridge-install-acceptance.mjs --from "D:\Unreal Projects\BridgeInstallTest"
```

## Running two editors at once

Supported and tested as of 2026-08-02. Each editor is addressed by session, and a
client that cannot tell which editor it is addressing refuses rather than picking
one. Discovery used to end in a compiled-in default pipe name, so a missing
advertisement silently routed to whichever editor owned it; with two open that is
a command authoring assets in the wrong project and reporting success.

Sync both targets first. A live result from either editor means nothing if it is
running a plugin that is not this checkout:

```bash
npm run install:check -- --project "D:\Unreal Projects\BridgeInstallTest"
```

Launch the first editor normally. The launcher refuses a second UE4Editor unless
a human says so in as many words, which is the point of the flag:

```powershell
.\Scripts\start-ue4-project.ps1 -Project 'D:\Unreal Projects\BridgeInstallTest\BridgeInstallTest.uproject'
```

```powershell
.\Scripts\start-ue4-project.ps1 -Project 'D:\Path\To\Second\Second.uproject' -AllowAdditional
```

Wait for `Saved/MCPPuerTSBridge/session.json` to appear under each project. Pipes
are project-hashed, so they do not collide:
`\\.\pipe\UE427PuerTSMCP_<project>_<hash>`.

### Selecting a target

`MCP_UNREAL_PROJECT_ROOT` selects which editor a client addresses. It is the only
thing that has to be set, and one client per project is the normal arrangement:

```bash
MCP_UNREAL_PROJECT_ROOT="D:/Unreal Projects/BridgeInstallTest" npm run smoke:inspect
```

`MCP_PUERTS_SESSION_ID` pins one exact session. An editor gets a new session id
on every start, so a pinned id that no longer matches is refused rather than
silently retargeted. Use it when a restart mid-run would be a problem.

`MCP_PUERTS_PIPE` still overrides the pipe name, because the installer writes one
into `.mcp.json`, but it no longer decides identity: the session that answers is
checked against the manifest on every response whatever chose the pipe.

### What the editor advertises

`Saved/MCPPuerTSBridge/session.json`, schema version 1, written by staging a temp
file and moving it over the target so a reader can never see a partial manifest:
session id, session nonce, editor PID, OS process creation time, project and
uproject paths, pipe name, bridge commit, install-manifest hash, creation time, a
5-second heartbeat, and shutdown state. It is retracted on shutdown, and only
that editor's own advertisement is touched.

Two checks, in opposite directions, and both are needed:

- The **nonce** goes out with every request; the editor refuses a mismatch before
  anything runs. It stops a request reaching the wrong editor.
- The **identity stamp** comes back on every response including rejections; the
  client refuses a reply from an editor it did not address. It stops an answer
  arriving from the wrong one.

PID is the liveness test, not the heartbeat. The heartbeat runs on the game
thread, so a long Blueprint compile stalls it while the editor is perfectly
alive; it is context for an error message, not evidence of death. Process
creation time is recorded because Windows reuses process ids.

### When it refuses

Failures carry `session_error_code` on the response, so a caller branches on the
code instead of matching prose:

| Code | Means |
|---|---|
| `session_missing` | no editor is open for that project. Not a reason to try another editor |
| `session_unreadable` | the manifest is corrupt |
| `session_schema_unsupported` | a newer manifest than this client understands |
| `session_shut_down` | the editor recorded its own shutdown |
| `session_stale` | the manifest names a PID that is not running |
| `session_not_selected` | `MCP_PUERTS_SESSION_ID` does not match the live session |
| `session_project_mismatch` | the manifest describes a different project than the root it was found under |
| `session_identity_absent` | the reply carried no identity |
| `session_identity_mismatch` | the reply came from an editor other than the one addressed |

### Regression test

```bash
node Scripts/session-isolation-acceptance.mjs --phase=both
```

Four phases, run against editor lifecycle rather than inside one process:
`both` (identity, isolation, refusals), `a-closed` (one editor closed, the other
still serving and the closed one refused without falling through), `a-restarted`
(new session id, same project; pass the old id as `MCP_PREVIOUS_SESSION_ID`), and
`none` (nothing left advertising). Evidence lands in
`docs/evidence/session-isolation-*.json`.

## After Install

1. Open the project in UE4.27.
2. Enable `Python Editor Script Plugin` if it is not already enabled.
3. Restart the editor.
4. Accept the rebuild prompt if the `MCPBridge` plugin was installed or updated.
5. Open `Window > MCP Bridge`.
6. Test the listener:

```powershell
curl -X POST http://localhost:8080 -H "Content-Type: application/json" -d "{\"command\":\"ping\"}"
```

Then open Claude Code in the project folder and ask:

```text
Test the Unreal connection.
```
