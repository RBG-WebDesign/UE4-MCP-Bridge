# PuerTS execution lane

The existing `unreal-bridge` MCP server uses one editor transport by default:

```text
MCP client -> stdio -> unreal-bridge -> authenticated Windows named pipe -> PuerTS Node.js -> native MCPBridgePuerTS module -> UE4 game thread
```

This is one MCP server and one Unreal plugin. HTTP/Python tools remain in the repository only for migration and are not advertised unless a human starts the server with `MCP_ENABLE_LEGACY_HTTP=1`. There is no automatic fallback.

## Version and runtime

- Unreal Engine 4.27 only
- PuerTS tag `Unreal_v1.0.9`, commit `838ab762d83021c0407f13120f4004dcaf70cffe`, subtree `unreal/Puerts` of https://github.com/Tencent/puerts
- Node.js backend
- No Unreal.js
- No React-UMG

### Provenance, verified 2026-07-31

The bundle was compared file by file against a sparse clone of the upstream tag
(CRLF-normalized). Results:

- 945 of 947 upstream files are byte-identical locally.
- One deliberate local modification: `Source/JsEnv/JsEnv.Build.cs` changes
  `UseNodejs = false` to `true` to select the Node.js backend.
- Two upstream files are absent locally: `Content/JavaScript/react-umg/index.js`
  (React-UMG deliberately not installed) and `ThirdParty/Libnode_APL.xml`.
- 93 local-only files, all under `ThirdParty/nodejs_16/`: the separately
  distributed PuerTS Node.js 16 dependency package (libuv 1.43 headers, V8
  headers, Win64 `libnode.dll` and `libnode.lib`).

Note that the `Puerts.uplugin` inside the bundle says `VersionName 1.0.5`. That
is upstream's own stale manifest: the file is byte-identical to the one in the
`Unreal_v1.0.9` tag. Do not treat it as evidence of the bundle version.

The pin is enforced by `Plugins/Puerts.lock.json` (tracked), a SHA256 manifest
of all 1038 bundle files excluding `Binaries`, `Intermediate`, and `Saved`.
`npm run verify` and the installer both run `Scripts/check-puerts-pin.mjs`
against it; the installer refuses to copy a bundle that does not match. After a
deliberate, reviewed bundle change, regenerate with
`node Scripts/check-puerts-pin.mjs --write`.

The local PuerTS bundle lives at `Plugins/Puerts`. It is ignored by Git because the Win64 Node libraries are large. The canonical installer requires that directory and copies it beside `MCPBridge` into the target Unreal project.

## Owned paths

| Path | Purpose |
|---|---|
| `Plugins/MCPBridge/Source/MCPBridgePuerTS/` | Native safety boundary, transactions, approved UE4.27 operations |
| `Plugins/MCPBridge/Content/JavaScript/` | Generated, not tracked: tsc output from `puerts-runtime` plus the `puerts/` support subtree staged from the pinned bundle by `Scripts/stage-puerts-runtime.mjs`. `npm run build` produces all of it. |
| `puerts-runtime/src/` | TypeScript source and tool registry |
| `puerts-runtime/typing/ue/ue.d.ts` | Generated declarations for the reflected Unreal API. Produced by the PuerTS editor toolbar button (DeclarationGenerator); regeneration steps and version stamp in `puerts-runtime/typing/ue/PROVENANCE.md` |
| `mcp-server/src/puerts-client.ts` | Authenticated named-pipe client used by the existing MCP server |
| `mcp-server/src/tools/puerts.ts` | MCP schemas for the PuerTS tools |

`npm run build` compiles the PuerTS runtime first, then the existing MCP server.

Operational notes from the official manual (full digest: `docs/PUERTS_MANUAL_DIGEST.md`):

- This bridge uses a manually started `FJsEnv`, not PuerTS automatic binding
  mode. The FAQ's TypeScript version ceiling applies only to automatic binding,
  which bundles its own compiler; our external TypeScript 5.5.4 is unaffected.
  Do not enable automatic binding without re-reading that constraint.
- If PuerTS extension methods ever come up missing after module load-order
  changes, call `IPuertsModule::Get().InitExtensionMethodsMap()` after all
  modules load.
- Declarations can be regenerated from the console with `Puerts.Gen FULL`
  (unverified here); see `puerts-runtime/typing/ue/PROVENANCE.md`.
- TypeScript-created delegates via `toManualReleaseDelegate` leak unless
  explicitly released. Delegate lifetime remains untested in this bridge; do
  not rely on delegates until add, callback, remove, and GC are covered.

## Connection configuration

The UE plugin writes a random bearer token to:

```text
<UnrealProject>/Saved/MCPPuerTSBridge/token.txt
```

Set one of these in the environment that launches the MCP server:

```text
MCP_UNREAL_PROJECT_ROOT=D:/Path/To/Project
MCP_PUERTS_TOKEN_PATH=D:/Path/To/Project/Saved/MCPPuerTSBridge/token.txt
MCP_PUERTS_TOKEN=<token value>
```

`MCP_PUERTS_TOKEN_PATH` takes precedence over the project-root default. The pipe defaults to `\\.\pipe\UE427PuerTSMCP` and can be overridden with `MCP_PUERTS_PIPE`.

## Safety boundary

- Only scripts under `Plugins/MCPBridge/Content/JavaScript` load by default.
- Script paths must remain inside the Unreal project.
- Node shell and worker modules are blocked.
- File-system operations are restricted to the Unreal project.
- Every request has a size limit and execution timeout.
- Every editor content mutation uses an Unreal transaction.
- Actor deletion requires `confirm=true`.
- Functions, writable properties, and tools use native allowlists.
- Editor-only operations are blocked during PIE. The physics observer and log capture are explicit exceptions.
- Every request and response uses structured JSON.

## Tools

The native catalog currently contributes 17 prefixed tools:

```text
puerts_diagnostic
puerts_find_assets
puerts_find_actors
puerts_read_property
puerts_set_property
puerts_call_function
puerts_spawn_actor
puerts_delete_actor
puerts_sky_shader_create
puerts_physics_build
puerts_physics_observe
puerts_viewport_screenshot
puerts_save
puerts_pie_start
puerts_pie_stop
puerts_get_logs
puerts_undo
```

The TypeScript registry inside Unreal defines each native tool's input schema, output schema, permissions, timeout, and execution function.

## Verification

With UE4 closed:

```powershell
npm run verify
```

With the isolated UE4.27 test editor open:

```powershell
$env:MCP_PUERTS_TOKEN_PATH = "D:\Path\To\TestProject\Saved\MCPPuerTSBridge\token.txt"
npm run smoke:puerts
npm run smoke:physics
```

`smoke:editor` proves the native transport and telemetry. `smoke:puerts` proves the first milestone. `smoke:physics` builds and saves a 35-actor scene, captures a fitted viewport PNG, starts PIE, observes rigid-body transforms, requires at least eight bodies to move, and stops PIE.
