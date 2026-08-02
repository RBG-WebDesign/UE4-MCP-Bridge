# Clean post-reboot validation of the shutdown fix, 2026-08-01

Validates commit `9675f6c` ("Release MCPBridge editor modules at OnEnginePreExit
so the editor can exit") on a rebooted machine carrying no leftover state. The
pre-reboot case matrix is in `reports/shutdown-acceptance-*.json`; the
investigation that found the cause is in `reports/shutdown-reliability-2026-08-01.md`
and `docs/CAPABILITY_FINDINGS.md` defect 0f.

Everything below was measured after the reboot, not carried over.

The root cause, per the corrected 0f, is `MCPBridgePIEAgent`: its core ticker and
its `GLog` output device were removed only in `ShutdownModule`, which runs after
`StaticExit` has destroyed every UObject, leaving `GLog` holding a freed sink
owned by a dead `UPIEAgentRuntime`. The earlier PuerTS `FJsEnv` theory was wrong
and is recorded as such in 0f - `puerts-idle` never creates an `FJsEnv` and hung
anyway, and the still-listening pipes were a consequence of a process that never
finishes exiting, not the cause. The shipped fix applies the same
`OnEnginePreExit` release to both modules, which is why both appear in the
shutdown log below.

## Starting state

| Check | Result |
|---|---|
| `UE4Editor` processes | none |
| MCPBridge named pipes | none |
| `pipe.txt` in BridgeInstallTest | absent |
| `pipe.txt` in UE427PuerTSMCP | present, advertising the dead `..._skyshader6` session - removed |
| `*.dll.zombielocked*` files | 25 present, all removed |
| BridgeInstallTest `PipeName` | `..._eb10ef4f_fix1` - restored to the canonical `..._eb10ef4f` |

The canonical name was computed with the installer's own rule
(`Get-ProjectPipeName`: SHA256 of the lowercased project root, first four bytes)
rather than assumed.

## BridgeInstallTest

| Step | Result |
|---|---|
| Build | ok, 28.7 s |
| Binary carries the fix | verified by reading the DLL's string table: `shutdown begin`, `OnEnginePreExit`, `pipe advertisement`, `startup complete` all present, DLL newer than the source |
| Launch | startup complete 51.5 s, pipe `..._eb10ef4f`, 22 approved tools |
| Log file name | `BridgeInstallTest.log`, not `_2` - nothing held the previous log |
| Read-only diagnostic | `success true`, `transaction_id ""` (a read-only tool must not transact), 12 actors, `is_game_thread true` |
| `npm run smoke:bt` | exit 0, build+inspect phase, 27 assertions passed |
| Normal close | **exited in 4.1 s** |
| Processes afterwards | none |
| Pipes afterwards | none |
| `pipe.txt` afterwards | retracted |
| Forced relink | `[3/4] UE4Editor-MCPBridgePuerTS.dll` linked, 18.5 s |

The relink is the pointed one: that exact step was
`LINK : fatal error LNK1104: cannot open file ...UE4Editor-MCPBridgePuerTS.dll`
before the fix.

## The shutdown log, which is the actual proof

```
MCPBridge lifecycle: shutdown begin (trigger: OnEnginePreExit).
MCPBridge lifecycle: releasing the PuerTS script environment.
MCPBridge lifecycle: PuerTS script environment released after 0.004 s.
MCPBridge lifecycle: service shutdown, pending command none, pipe advertisement retracted.
MCPBridge lifecycle: bridge service released.
MCPBridge lifecycle: shutdown complete in 0.004 s.
MCPBridge lifecycle: PIE agent shutdown begin (trigger: OnEnginePreExit).
MCPBridge lifecycle: PIE agent runtime released, ticker and log sink unregistered.
LogExit: Preparing to exit.
LogExit: Transaction tracking system shut down
LogExit: Editor shut down
LogExit: Object subsystem successfully closed.
LogExit: Exiting.
Log file closed, 08/01/26 20:12:57
```

Before the fix, **every** graceful close on this machine stopped dead at
`LogExit: Object subsystem successfully closed.` and never reached
`LogExit: Exiting.` through the normal path. The whole bridge teardown costs
4 ms when it runs at the right time.

## UE427PuerTSMCP, the main test project

Its plugin copy was still pre-fix, so the three changed files were synced into it
first (`MCPPuerTSBridgeModule.cpp`, `MCPPuerTSBridgeService.cpp`,
`PIEAgentModule.cpp`). Its `[MCPPuerTSBridge] PipeName` was deliberately left at
`..._skyshader6`; see Open items.

| Step | Result |
|---|---|
| Build | ok, 21.9 s, 11 actions including all three plugin DLLs |
| Launch | startup complete 45.5 s |
| Normal close | **exited in 3.78 s** (500 ms polling, so a tighter bound than the harness's 4.1 s) |
| Processes / pipes / `pipe.txt` afterwards | none / none / retracted |
| Log | reached `LogExit: Exiting.` and `Log file closed` |

This project produced two of the four original unkillable processes. It now
closes clean.

## Close measurements

| # | Project | Close | Notes |
|---|---|---|---|
| 1 | BridgeInstallTest | 4.1 s | after a read-only command and the BT smoke |
| 2 | UE427PuerTSMCP | 3.78 s | |
| 3 | BridgeInstallTest | 3.74 s | measured from when the modal prompt was answered; see below |

Plus the five consecutive `bridge-idle` iterations recorded pre-reboot in
`reports/shutdown-acceptance-bridge-idle.json`, all `process_exited true` with
`build_after_ok true`.

`npm run verify`: exit 0. 13 unit suites, PuerTS pin
(`Unreal_v1.0.9 @ 838ab762d830`, 1038 files), tool inventory (206 tools frozen),
smoke 8 passed 0 failed 2 skipped - the same two skips the handoff records.

## The one failure, and why it is not a regression

`behavior-tree-acceptance.mjs --phase=cold` (an extra phase beyond
`npm run smoke:bt`, which passed) failed its 13th assertion,
`the failed build wrote nothing to disk (filesystem check)`. The first twelve
passed, including `an unknown node type is rejected with no save` - so the build
correctly reported that it saved nothing.

This is **defect 0c, not the shutdown fix**, and the timestamps prove it twice:

- The offending `BT_AcceptanceBadType.uasset` / `_BB.uasset` were dated
  **18:04:03**, the exact second of the pre-fix hung close performed during the
  investigation. The cold run was at 20:12. They pre-dated the run.
- After clearing them, the next normal close **rewrote both at 20:22:13**, the
  exact second of that close, *despite* the `Save Content` prompt being answered
  `Don't Save`. So the unattended save-on-exit flow writes the failed transient
  regardless of the answer.

That is a sharper reproduction of 0c than the original entry had, and it is
builder-side work: purge the transient package when a create-path build fails.
Left alone deliberately - it is a separate open defect, not a regression, and
fixing it is a different capability.

**It also caused close #3 to look slow at first.** The editor sat for 105.9 s
after the close request. It was parked on the `Save Content` modal waiting for a
human: 266 threads, main window alive, and **no `LogExit` or
`MCPBridge lifecycle: shutdown begin` line in the log at all**, so teardown had
not started. Once the prompt was answered it exited in 3.74 s. Under the old
code the same prompt was answered the same way and the editor still never
exited. A modal waiting for input and a process that cannot die are different
failures, and the log distinguishes them.

## Open items, not acted on

- `UE427PuerTSMCP`'s `[MCPPuerTSBridge] PipeName` is still the temporary
  `..._skyshader6` suffix. The handoff already calls this a test artifact rather
  than portable configuration. Restoring it to the canonical
  `..._81d778e7` means re-running the installer, which also rewrites that
  project's MCP client config, so it was left for a deliberate decision rather
  than changed underneath the user.
- Both test projects carry their own copies of the plugin, so a bridge-repo edit
  is invisible to them until the installer runs or the files are copied. That
  bit this validation once and is worth a line in the runbook.
- Defect 0c, above.
