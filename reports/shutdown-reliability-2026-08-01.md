# MCPBridge editor shutdown reliability, 2026-08-01

Branch `bridge/native-consolidation-2026-07-31`. Root cause found, fixed and
verified on BridgeInstallTest. Full diagnosis in `docs/CAPABILITY_FINDINGS.md`
defect 0f.

## Root cause

`FMCPBridgePIEAgentModule::StartupModule` creates a `UPIEAgentRuntime`, roots
it, and `Initialize` registers a core ticker bound to it with
`FTickerDelegate::CreateUObject` and an `FOutputDevice` handed to `GLog` whose
memory that UObject owns (`PIEAgentRuntime.cpp:97-106`). Both were removed only
in `ShutdownModule`, which UE4.27 calls from `UnloadModulesAtShutdown`
(`LaunchEngineLoop.cpp:4294`) **after** `StaticExit` has destroyed every
UObject. From `StaticExit` onward `GLog` holds a freed log sink. The editor
stops on the line `StaticExit` itself emits and never finishes exiting, keeping
its plugin DLLs locked so the project cannot be rebuilt, and it cannot be
killed.

## The case matrix

Recorded by `Scripts/editor-shutdown-acceptance.ps1`. Readiness is
`LogLoad: (Engine Initialization) Total time:` plus a settle, so every close is
a close of a fully loaded editor.

### Before the fix

| Case | MCPBridge | Puerts | FJsEnv | Client | Close | Builds after |
|---|---|---|---|---|---|---|
| `bare` | off | off | no | no | 4.1 s | yes |
| `plugin-off` | off | **on** | no | no | 4.1 s | yes |
| `puerts-idle` | on, inert | on | **no** | no | never exits | no |
| `bridge-idle` | on | on | yes | no | never exits | no |

Smallest reproducing case: **`puerts-idle`** - the plugin loaded with the bridge
inert via `-MCPPuerTSBridgeDisabled`, no `FJsEnv` ever created, no client. That
rules out PuerTS and the named pipe. Removing only `MCPBridgePIEAgent` from
`MCPBridge.uplugin`, with the whole rest of the bridge running and its pipe up,
closed in 4.1 s.

### After the fix

| Case | Startup | Bridge init | Client | Close | Exited | Advert left | Locked DLLs | Builds after |
|---|---|---|---|---|---|---|---|---|
| `bare` | 24.2 s | no | no | 4.1 s | yes | no | 0 | yes |
| `plugin-off` | 24.2 s | no | no | 4.1 s | yes | no | 0 | yes |
| `puerts-idle` | 27.2 s | no | no | 4.1 s | yes | no | 0 | yes |
| `bridge-idle` x5 | 27.1-27.3 s | yes | no | 4.1 s each | yes | no | 0 | yes |
| `read-only` | 27.2 s | yes | **yes** | 4.1 s | yes | no | 0 | yes |
| `bt-smoke` | 24.2 s | yes | yes, `npm run smoke:bt` passed | 4.1 s | yes | no | 0 | yes |

Shutdown callbacks reached, every run:

```
MCPBridge lifecycle: shutdown begin (trigger: OnEnginePreExit).
MCPBridge lifecycle: releasing the PuerTS script environment.
MCPBridge lifecycle: PuerTS script environment released after 0.003 s.
MCPBridge lifecycle: service shutdown, pending command none, pipe advertisement retracted.
MCPBridge lifecycle: bridge service released.
MCPBridge lifecycle: shutdown complete in 0.003 s.
MCPBridge lifecycle: PIE agent shutdown begin (trigger: OnEnginePreExit).
MCPBridge lifecycle: PIE agent runtime released, ticker and log sink unregistered.
```

Subsystem cleanup then proceeds normally through `LogExit: Preparing to exit.`,
`Editor shut down`, `Object subsystem successfully closed.` and the process
exits.

## The wrong first answer

The first fix moved the *PuerTS* teardown to `OnEnginePreExit`, on the theory
that `FJsEnvImpl::~FJsEnvImpl` was blocking. The lifecycle logging added at the
same time disproved it in one run: the script environment released in
**0.003 s** and the editor hung anyway. The evidence that had pointed at
PuerTS - three exited editors whose named pipes still accepted connections -
was a consequence of the hang, not its cause: a process that never finishes
exiting keeps every handle it owns.

The `MCPBridgePuerTS` change was kept. It was releasing `FJsEnv` and calling
`Service->RemoveFromRoot()` after `StaticExit` had closed the object subsystem,
which is wrong for the same reason even though it was not what hung, and the
pipe advertisement retraction is required by the acceptance.

Two measurement mistakes, both of which produced confident wrong answers, are
recorded in defect 0f: a readiness check that fired during startup (so three
early runs closed editors mid-initialisation, and one made a bridge-free editor
look like it hung), and an unbounded pipe read that blocked on a dead editor's
pipe instance.

## Changed files

| File | Change |
|---|---|
| `Plugins/MCPBridge/Source/MCPBridgePIEAgent/Private/PIEAgentModule.cpp` | **The fix.** Release the runtime from `OnEnginePreExit`; idempotent `ShutdownModule` fallback; lifecycle logging |
| `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeModule.cpp` | Same pattern for `FJsEnv` and the service; lifecycle logging |
| `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeService.cpp` | `Shutdown` retracts `Saved/MCPPuerTSBridge/pipe.txt`; one `GetPipeAdvertisePath` for write and delete |
| `Scripts/editor-shutdown-acceptance.ps1` | New. The case matrix and the five-consecutive-closes regression test |
| `docs/CAPABILITY_FINDINGS.md` | Defect 0f rewritten with the real cause and both wrong turns |
| `docs/UE427_PUERST_MCP_HANDOFF_2026-07-31.md` | Status row and a teardown rule in Debug and recovery |

`npm run verify` passes: 13 unit suites, PuerTS pin (`Unreal_v1.0.9 @
838ab762d830`, 1038 files), tool inventory (206 tools), smoke 8 passed 0 failed
2 skipped - the same two skips the handoff records.

## Machine state left behind

Four editors that hung *before* the fix are still present and still hold their
named pipes; they survive until reboot. No editor running the fix has left one.
Their locked DLLs were moved aside as `*.dll.zombielocked*` in
`D:\Unreal Projects\BridgeInstallTest\Plugins\MCPBridge\Binaries\Win64\` - safe
to delete after a reboot.

`BridgeInstallTest\Config\DefaultEngine.ini` `PipeName` was bumped to
`..._eb10ef4f_fix1` because the previous name is still owned by one of those
zombies. It can go back to a plain project-hashed name after a reboot.

Windows refuses to overwrite a zombie's locked DLL but does allow it to be
**renamed**, which frees the path for the linker. That is how this session built
and tested without rebooting; the harness does it before its own build and never
before the build it measures.

## Not addressed

Defect 0c (a failed `behavior_tree_build` leaves a dirty transient that the
close prompt offers to save) is a separate builder-side bug and was out of
scope. It is not the hang: a close answered with `Don't Save`, writing nothing
and running no source-control flow, hung exactly the same way.
