# Release

Three questions, answered from what was actually run rather than from what the
scripts are supposed to do:

1. Can the plugin be packaged for distribution today?
2. How does an existing install move to a newer manifest version?
3. What does a fresh project install look like end to end?

Where a claim below has no evidence behind it, it says so. Nothing in this
document was proven by a live editor; see "What none of this proves" at the end.

## 1. Packaging verdict: NO

`Scripts/package-mcp-bridge.ps1` produces a well-formed UE4 plugin **source
tree**. It does not produce an artefact a recipient can drop into a project and
use. The zip is a build input, not a release.

Evidence, from `node Scripts/package-acceptance.mjs`, which runs the real
packaging script and opens what it produced. All 27 cases pass: 22 are PROVEN
properties and 5 are GAP cases that assert the gap exactly as it stands today,
so closing one breaks the test and forces this document to be updated.

What the package does have:

- one `MCPBridge/` root with `MCPBridge.uplugin` at the top, which is where UE4
  looks for it
- no `Binaries/`, `Build/`, `Intermediate/`, `Saved/`, `__pycache__/` or `.vs/`
- longest plugin-relative path 89 characters, inside the 170 budget
- a `Build.cs` for every one of the five modules the descriptor declares
- `Content/JavaScript/{bootstrap,registry,runtime,safety}.js`

### Blocker 1: the descriptor requires a plugin the zip does not contain

`MCPBridge.uplugin` declares a dependency on `Puerts`, and
`MCPBridgePuerTS.Build.cs` lists `JsEnv` in `PrivateDependencyModuleNames`.
`JsEnv` is a module of the PuerTS plugin. So the dependency is not advisory:
UE4 refuses to load MCPBridge without Puerts present, and UBT cannot compile
`MCPBridgePuerTS` without it either.

The zip contains no PuerTS bundle, and no text inside the zip mentions the
dependency. A recipient gets a plugin that fails to load and no artefact-local
explanation of why. The bundle is 369 MB, is excluded from Git by `.gitignore`,
and has to be obtained separately from `Tencent/puerts`, subtree
`unreal/Puerts`, tag `Unreal_v1.0.9`, commit `838ab762d830`, as recorded in
`Plugins/Puerts.lock.json`.

This is the blocker that decides the verdict. The other four are fixable in an
afternoon; this one is a distribution decision about a third-party bundle.

### Blocker 2: packaging a clone silently ships an empty PuerTS lane

`Plugins/MCPBridge/Content/JavaScript/` is generated and is not tracked. It is
produced by `npm run build`, whose middle step is
`node Scripts/stage-puerts-runtime.mjs`, which copies the `puerts/` support
subtree out of `Plugins/Puerts`. With no bundle present that script prints
`SKIP` and exits 0 (`Scripts/stage-puerts-runtime.mjs:24`), and the build
continues.

So a clean clone with no bundle produces a `Content/JavaScript` containing the
five compiled runtime files and none of the PuerTS support files. The packaging
script then packages whatever is on disk without checking, which the acceptance
records as a GAP: packaging a tree with no `Content/JavaScript` at all still
produces a zip and prints no warning.

This is observable in this checkout right now. `npm run install:check` against
`D:\Unreal Projects\BridgeInstallTest` reports 16 differing files in
`content_javascript`. The five it prints are all `extra:` entries under
`Content/JavaScript/puerts/`: the target has the support subtree and this
worktree, which has no bundle, cannot generate it.

Fix before shipping: make the packaging script refuse a tree whose
`Content/JavaScript/puerts/` is missing, the same way `install:check` refuses a
stale target. A silent partial artefact is the failure this repository keeps
having.

### Blocker 3: the zip carries no install manifest

Nothing in the zip writes `MCPBridgeInstall.json`, so a project installed from
the zip is what `checkInstall` calls an unmanaged plugin copy of unknown
provenance, and it is refused for acceptance. That is correct behaviour and it
means the packaged path and the `install:sync` path produce projects the tooling
treats differently.

### Blocker 4: no binaries, so the recipient needs a compiler

The zip is source only. The recipient must have a C++ project, UE4.27 and UBT.
`package-mcp-bridge.ps1 -RunUAT` exists to build binaries and needs an engine.

**It has never been run.** There is no `Releases/` directory in this checkout
and no evidence file recording a UAT build. `docs/MCP_BRIDGE_RELEASE_WORKFLOW.md`
says the compile is "validated locally per release"; no such validation is
recorded anywhere in the repository. Treat `-RunUAT` as untested.

There is also a reason to expect it to fail on first attempt, stated as a
prediction rather than a finding: `BuildPlugin -Rocket` compiles the plugin
against the engine on its own, and `MCPBridgePuerTS` depends on `JsEnv`, which
lives in a project plugin. Unless Puerts is installed into the engine's own
`Engine/Plugins`, that dependency has nowhere to resolve from. Nobody has run it,
so this is reasoning from the Build.cs, not a measurement.

### Blocker 5: the plugin is half the bridge

The MCP server is Node and lives in `mcp-server/`, outside the plugin. The
installer writes a `.mcp.json` pointing at an absolute path into a shared bridge
checkout on that machine. A packaged plugin therefore cannot be a complete
product by itself, whatever is done about the four blockers above. A real
distribution has to decide whether the server ships with the plugin, is fetched
from npm, or stays a developer-checkout arrangement. That decision has not been
made.

### What would change the verdict

In order:

1. Decide how PuerTS reaches the recipient, and say so inside the artefact.
2. Make packaging refuse an incomplete `Content/JavaScript`.
3. Run `-RunUAT` once against a real UE4.27 and record the output.
4. Decide how the MCP server ships.

Items 1, 2 and 4 are decisions. Item 3 is the only one that needs an engine.

## 2. Upgrading an install from an older manifest version

`MCPBridgeInstall.json` carries `schema_version`, and `checkInstall` reads the
version before it reads any other field. A manifest whose version this checkout
does not recognise is refused whole: `status`, `deprecated_reason` and
`dll_hashes` are not consulted. That is deliberate. Both `status` and
`dll_hashes` are fields where guessing means passing an install that should have
been refused.

Current version: `MANIFEST_SCHEMA_VERSION = 1` in `Scripts/bridge-install.mjs`.

Three refusals, and the fix differs for each:

| Manifest | `install:check` says | Do this |
|---|---|---|
| no integer `schema_version` | written before the manifest was versioned | `npm run install:sync -- --project <path>` |
| version below this checkout | names both versions | `npm run install:sync -- --project <path>` |
| version above this checkout | the target came from a newer bridge | update this checkout. Do **not** sync |

The newer case is the one worth reading twice. Syncing from an older checkout
into a target installed from a newer one moves the target backwards while
reporting success, so the refusal deliberately does not suggest `install:sync`.
`Scripts/fresh-install-acceptance.mjs` case 10 asserts that the newer-version
message never contains the sync instruction.

`install:sync` rewrites the manifest at the current version as its last step.
It also builds the target editor, so it needs UE4.27 and it is not something to
run while an editor has the target open.

### This is not hypothetical

Run today against a real target, read-only:

```
npm run install:check -- --project "D:\Unreal Projects\BridgeInstallTest"
```

```
FAIL  manifest_schema: MCPBridgeInstall.json in D:/Unreal Projects/BridgeInstallTest
      has no integer schema_version, so it was written before the manifest was
      versioned. Its fields are not read. Re-run
      npm run install:sync -- --project <path> to rewrite it.
```

Both known targets were installed before versioning existed and both need one
`install:sync` to become checkable again. Until they do, `install:check` refuses
them, which is the intended behaviour and not a regression.

## 3. Fresh-project install, end to end

`docs/TEAMMATE_INSTALL_PROOF.md` is the same procedure written as a script for
somebody who has never used this repository, with a place to record what broke.
This section is the short form for somebody who has.

```powershell
git clone <this repository> D:\Bridge
cd D:\Bridge
npm ci
```

Obtain the pinned PuerTS bundle and put it at `Plugins\Puerts`. There is no
script for this and it is a real gap in the fresh-clone path: the lock file
records what the bundle must be, and nothing fetches it.

```powershell
npm run check:puerts        # SKIPs with a warning if the bundle is absent
node Scripts\check-puerts-pin.mjs --strict   # fails instead of skipping
npm run build
npm run verify
```

Run the strict form. `npm run check:puerts` skips a missing bundle by design so
that a server-only checkout can still build, which also means the default form
will not tell you the bundle is missing.

```powershell
.\Scripts\install-mcp-bridge.ps1 -Project "D:\Unreal Projects\MyGame"
```

That copies `Plugins\MCPBridge` and `Plugins\Puerts` into the project, writes the
`[MCPPuerTSBridge]` pipe section into `Config\DefaultEngine.ini`, generates
`.mcp.json` and `.codex\config.toml`, and enables `MCPBridge` and `Puerts` in the
`.uproject`. It does not enable `PythonScriptPlugin`: the legacy HTTP listener is
off unless `-EnableLegacyHttp` is passed.

Everything in that paragraph is asserted by `npm run test:fresh-install`, which
installs into a directory that did not exist a moment earlier and reads back
every file, including launching exactly what the generated `.mcp.json` says to
launch and asking it for `tools/list`. It answered with 26 tools.

Then, and only then, the parts that need Unreal:

```powershell
npm run install:sync -- --project "D:\Unreal Projects\MyGame"   # builds the editor target
```

Open the project, accept the rebuild prompt if it appears, wait for
`Saved\MCPPuerTSBridge\session.json`, then:

```powershell
npm run install:check -- --project "D:\Unreal Projects\MyGame"
npm run smoke:editor
```

`install:check` must pass before any live result means anything.
`Scripts/fresh-install-acceptance.mjs` case 9 marks exactly where the editor-free
proof stops: the gate must fail with the missing DLL as its **only** complaint.
Everything past that line needs an engine.

## What none of this proves

- No editor was launched and no UBT build was run for this document. Every
  number above comes from a script that runs with Unreal closed.
- `package-mcp-bridge.ps1 -RunUAT` has never been run. The binary release path
  is unevaluated, not merely unrecorded.
- The teammate procedure in `docs/TEAMMATE_INSTALL_PROOF.md` has never been
  performed by anyone who did not already know this repository.
- Green CI is not a release gate. `.github/workflows/ci.yml` lists what it
  cannot run; the live acceptance suites and the UE4.27 compile are on that
  list.
