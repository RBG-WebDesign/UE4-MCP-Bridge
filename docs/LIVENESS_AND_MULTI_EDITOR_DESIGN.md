# Liveness and multi-editor: design

Two open problems, specified and not implemented. Both are cases where the
bridge currently gives an answer that is safe but misdiagnosed, and a
misdiagnosis costs more than a refusal because it sends someone to fix the wrong
thing.

Facts below marked **confirmed** were read out of the shipped code at the paths
given. Facts marked **unknown** have not been measured and must not be treated as
findings.

---

## Part 1: a modal dialog is indistinguishable from a hang

### What happens today

A modal dialog in UE4 blocks the game thread. Every bridge command is executed on
the game thread, so nothing answers. The client's only timeout is a flat one:

`mcp-server/src/puerts-client.ts:214` starts a timer per call and rejects with

```
PuerTS command pipe timed out
```

That is a bare `Error`, not a `SessionError`, so it carries no code and no
detail. **Confirmed.** The caller cannot tell apart:

1. a modal dialog waiting for a human,
2. a long legitimate operation (a Blueprint compile, a shader recompile, a level
   load) that outran the tool's timeout budget,
3. the game thread deadlocked or stopped in a debugger,
4. the Node loop inside the editor wedged while the game thread is fine,
5. the editor crashed between the session read and the write.

Case 5 is already separable: the client re-reads nothing, but `resolveSession`
checks the PID before connecting, so a crash usually surfaces as
`session_stale` on the next call. The other four all present identically.

### The signal that already exists and is not used

`UMCPPuerTSBridgeService::BeginSession` registers a 5 second heartbeat on
`FTicker::GetCoreTicker()`
(`Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeService.cpp:299`)
and `TickHeartbeat` rewrites `session.json` with a fresh `last_heartbeat_at` on
every fire. **Confirmed.** The core ticker is pumped from the engine loop, which
is the game thread, and the code says so in its own comment: the heartbeat stalls
during a long compile while the editor is perfectly alive.

That is exactly what makes it useful here. `last_heartbeat_at` is a game-thread
liveness clock that the client never reads, and the PID is a process liveness
clock that it does. Two clocks, and the pair separates the cases the single
clock cannot:

| PID | `last_heartbeat_at` | Reply | Diagnosis |
|---|---|---|---|
| alive | fresh | none within budget | the command is running and outran its budget, or the Node loop is wedged |
| alive | stale by more than a few intervals | none | the game thread is not ticking: modal dialog, long blocking operation, or a debugger break |
| alive | stale | there was never an advertisement | already `session_missing` |
| dead | anything | none | already `session_stale` |

**Unknown, and the first thing to measure:** whether a UE4.27 modal window
freezes `FTicker::GetCoreTicker()`. A modal window in Slate pumps its own nested
loop; the core ticker is ticked from `FEngineLoop::Tick`, not from
`FSlateApplication::Tick`, which suggests the heartbeat does freeze. Suggests is
not measured. The experiment is small and needs one editor: open a modal (any
asset-save-confirm dialog), leave it open for 30 seconds, and read
`Saved/MCPPuerTSBridge/session.json` twice. If `last_heartbeat_at` does not
advance, the whole table above is real. If it does advance, the heartbeat is
useless for this and Part 1 needs the watchdog in the next section instead.

### Proposed change 1: turn the timeout into a diagnosis (client only)

On timeout, before rejecting, re-read `session.json` and branch:

```
editor_busy            pid alive, heartbeat fresh
                       "The editor is ticking and did not answer within Nms.
                        The command is probably still running. Retry with a
                        larger budget, or check the editor's log."

editor_not_ticking     pid alive, heartbeat older than 3 intervals (15s)
                       "The editor process is alive but its game thread has not
                        ticked for Ns. A modal dialog, a long blocking operation
                        or a debugger break will all do this. Look at the editor
                        window."

session_stale          pid dead                    (already implemented)
session_missing        no advertisement            (already implemented)
```

Cost: one file read on a path that is already failing, and two new
`session_error_code` values. No C++, no protocol change. This should be built
first because it is nearly free and it upgrades every existing timeout.

Its limit, stated: it cannot say *modal* rather than *blocked*. It says the game
thread is not ticking, which is the actionable half.

### Proposed change 2: a liveness clock that is not the game thread

If change 1 measures out, or if "modal" specifically needs naming, the editor
needs a heartbeat that a blocked game thread cannot stop. An `FRunnable` owned by
`MCPBridgePuerTS`, stamping a small file (`Saved/MCPPuerTSBridge/watchdog.json`)
once a second with a counter and a timestamp, is not on the game thread and keeps
running through a modal. Then:

- watchdog fresh + session heartbeat stale = the process lives, the game thread
  does not tick. Positive, not inferred from an absence.
- watchdog stale + PID alive = the process is wedged below the game thread, or
  suspended.

Naming the modal specifically needs one more thing, and the options are not
equal:

- `FSlateApplication::Get().GetActiveModalWindow()` returns the window and its
  title, but Slate is game-thread-only, so the watchdog thread cannot call it and
  the game thread cannot report it while it is blocked. Dead end unless something
  records it *before* the block starts.
- A Win32 read from the watchdog thread: `GetForegroundWindow` plus
  `GetWindowText`, filtered to windows owned by this PID. Off to the side of the
  engine entirely, so it works while the game thread is blocked, and it can name
  the dialog in the refusal ("the editor is showing: Save Content?"). Ugly,
  Windows-only, and the bridge is Windows-only anyway. Cheapest thing that
  actually names the modal.
- Hooking modal open/close: UE4.27 exposes no general delegate for
  `FSlateApplication::AddModalWindow`. Would need an engine change. Rejected.

Recommendation: change 1 now, change 2 only if measurement shows change 1 is not
enough. Do not build the watchdog speculatively; it is a thread and a file that
have to be right at shutdown, and a wrong watchdog invents a liveness problem
rather than reporting one.

---

## Part 2: two editors on the SAME project

### What actually happens, read out of the code

`session.json` is one path per project:
`FPaths::ProjectSavedDir() / MCPPuerTSBridge / session.json`
(`MCPPuerTSBridgeService.cpp:47-50`). **Confirmed.**

`BeginSession` writes it unconditionally. There is no read of an existing
manifest, no check for a live PID already advertising, no refusal path.
**Confirmed** (`MCPPuerTSBridgeService.cpp:274-305`).

`TickHeartbeat` rewrites it unconditionally every 5 seconds. **Confirmed.**

`Shutdown` writes `shutdown_state: shut_down` and then deletes the file, with no
check that the file still describes this session. **Confirmed**
(`MCPPuerTSBridgeService.cpp:367-375`). The comment there says retracting
"touches only this editor's advertisement and another editor's session is
untouched" - which is true for two editors on two projects, and false for two
editors on one.

The pipe name comes from `[MCPPuerTSBridge] PipeName` in `GEngineIni`
(`MCPPuerTSBridgeService.cpp:172`), and the installer derives one name per
project (`Scripts/install-mcp-bridge.ps1`, `Get-ProjectPipeName`). Two editors on
the same project therefore compute the same pipe name. The listener is
`net.createServer(...).listen(bridge.GetPipeName())` in
`puerts-runtime/src/bootstrap.ts:97`, so the second editor's listen fails, the
error handler calls `SetRuntimeReady(0)` and logs, and that editor keeps running
without a bridge. **Confirmed** by reading the code; **not measured live.**

So the sequence for a second editor on the same project is:

1. Second editor publishes `session.json`, overwriting the first editor's
   advertisement with its own session id, nonce and PID.
2. Its pipe listen fails; it serves nothing.
3. A client reads the advertisement, gets the second editor's nonce, and connects
   to the pipe name - which is owned by the FIRST editor.
4. The first editor receives a request whose `session_nonce` is not its own and
   refuses: "session_nonce does not match this editor".
5. The client refuses the reply as well, with `session_identity_mismatch`.

The isolation work holds: **nothing lands in the wrong world.** The result is a
refusal. But the refusal describes a forged or stale nonce, and the actual cause
is "you have two editors open on this project" - which appears nowhere. Worse,
when the second editor is closed it deletes `session.json`, retracting the FIRST
editor's advertisement while that editor is still running and still serving. The
first editor then looks like `session_missing` until it is restarted.

### What should happen instead

Three changes, in order of value.

**2a. Do not publish over a live advertisement.** In `BeginSession`, read any
existing `session.json` first. If it parses, its `shutdown_state` is `running`,
its `editor_pid` is alive, its `process_start_time` matches that PID, and its
`session_id` is not this one, then this editor must not publish. It cannot serve
anyway: the pipe is single-owner and its listen is about to fail. It should log,
loudly and once, naming the other PID:

```
MCPBridge did not start for this editor: pid 1234 is already serving this
project on \\.\pipe\UE427PuerTSMCP_MyGame_ab12cd34. Only one editor per project
can own the bridge pipe. Close the other editor, or open this project in only
one editor.
```

and leave the first editor's advertisement untouched. The stale-advertisement
case (a previous editor that died without retracting) is already distinguishable
by the PID check, and must still be overwritten - that is the current recovery
path and it works.

**2b. Retract only what you own.** In `Shutdown`, re-read the manifest before
deleting and delete only if its `session_id` equals this session's. Otherwise log
that the advertisement belongs to another session and leave it. This is four
lines and it removes the worst symptom: closing the second editor should not be
able to unpublish the first.

**2c. Only if two editors on one project must actually work.** Then the
one-file-one-pipe-per-project model is the constraint, and both have to go:

- Advertise a directory, `Saved/MCPPuerTSBridge/sessions/<session_id>.json`, one
  file per editor, each retracted by its owner. A reader that finds more than one
  live file has an ambiguity it can report by name, and `MCP_PUERTS_SESSION_ID`
  already exists to resolve it.
- Derive the pipe name from the session id rather than from the project, so two
  editors never contend. `[MCPPuerTSBridge] PipeName` becomes a prefix, not a
  name. `MCP_PUERTS_PIPE` in `.mcp.json` stops being meaningful as an override
  and should be dropped from what the installer writes.
- The client's ambiguity refusal becomes: "two editors are running this project
  (pids 1234, 5678). Set MCP_PUERTS_SESSION_ID to one of ...". Refusing on
  ambiguity rather than picking is the rule the isolation work already
  established.

That is a protocol change with a `session.json` schema bump and a matching bump
of `SUPPORTED_SCHEMA_VERSION` in `mcp-server/src/puerts-client.ts`. It should not
be done until somebody has a reason to run two editors on one project. **2a and
2b should be done regardless**, because they are small and because the current
behaviour lets one editor unpublish another.

### What to write down as an acceptance when this is built

`Scripts/session-isolation-acceptance.mjs` covers two editors on two projects. A
same-project phase would be:

- open editor A, capture its session id
- open editor B on the SAME project
- assert A's advertisement survives (2a)
- assert B logs the refusal naming A's pid
- assert a client call still reaches A, with A's session id in the response
- close B
- assert A is still advertised and still answers (2b)

Every step needs a live editor, so it belongs with the editor-required list in
`.github/workflows/ci.yml` and not in CI.
