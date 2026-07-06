# Viewport Deadlock Prevention (game-thread blocking rules)

Status: Verified 2026-07-05
Engine: UE4.27
Origin: Sinfeld_Demo MCP bridge, viewport_screenshot fix + PIE harness audit

## 1. System Design Intent

A rule set for any bridge handler that produces frame-dependent output
(screenshots, PIE startup, render targets): never block the game thread
waiting for work that requires the game thread to advance. In this bridge,
Python handlers execute inside a slate post-tick callback ON the game
thread. Any `time.sleep` loop there prevents the current frame from ending,
so deferred engine work (end-of-frame captures, next-tick play sessions)
can never complete while the handler waits for it. The pattern that
resolves it: request the deferred work, return immediately with a pending
status, and let a component OFF the game thread (the Node MCP server, or a
follow-up bridge call one tick later) do the waiting.

## 2. Dependencies

- Bridge architecture: HTTP listener thread -> thread-safe queue ->
  `unreal.register_slate_post_tick_callback` -> handler runs on game thread
  (Plugins/MCPBridge/Content/Python/mcp_bridge/listener.py)
- Node side: `mcp-server/src/tools/viewport.ts` `waitForScreenshotFile`
  (fs polling, 100 x 100ms)
- Engine facts (4.27):
  - `unreal.AutomationLibrary.take_high_res_screenshot` is deferred; the
    PNG is written at end of frame, after the handler returns
  - `FScreenshotRequest::RequestScreenshot` is fulfilled on the next
    viewport draw; force one with `GEditor->RedrawAllViewports()`
  - PIE via `GEditor->RequestPlaySession` starts on a deferred tick
    (the bridge's AutoPIEHelper exists precisely to defer it)

## 3. How-To Graph Logic

The deadlock (what NOT to build):

```
handler (game thread):
  request deferred capture
  loop: sleep(0.1); check file        <- frame can never end
  after 10s timeout: give up, return  <- editor frozen the whole time
frame finally ends AFTER return       <- file written when nobody waits
```

The fix, two halves:

```
python handler (game thread):
  request capture
  return {status: "pending", filepath, file_size_bytes: 0} immediately

node tool handler (own process, harmless to block):
  if result.status == "pending":
    poll fs.stat(filepath) up to 10s
    on size > 0: status = "complete", fill file_size_bytes
  return result   <- caller contract preserved: file ready when tool returns
```

Same rule applied to PIE: request via a deferred-tick helper and return;
confirm readiness from a SEPARATE bridge call (get_pie_world) instead of
sleeping in the requesting handler.

## 4. Replication Steps

1. Audit every handler for `time.sleep` / busy-wait between requesting
   engine work and observing its result. Any such wait on the game thread
   that depends on a frame advancing is a deadlock.
2. Split the operation: game-thread half requests and returns `pending`
   plus enough data for an external waiter (absolute path, expected
   marker).
3. Put the wait where blocking is free: the Node MCP tool handler for
   file outputs; a follow-up bridge call (next tick) for engine state.
4. Keep the tool's external contract unchanged so callers never see the
   pending state unless they connect to the raw listener.
5. Update the mock server to carry the new status field with a value that
   does not trigger polling in unit tests ("complete").

## 5. UE4.27 Legacy Gotchas

- The Python listener queue processes commands inside
  `register_slate_post_tick_callback`; EVERYTHING a handler does is on the
  game thread, including innocuous-looking waits deep in helpers.
- `HighResShot`/`take_high_res_screenshot` exclude Slate/UMG. To capture
  UI, use console `Shot SHOWUI` (files land in
  Saved/Screenshots/Windows/ScreenShotNNNNN.png with engine numbering).
- The PIE ready-marker wait in the harness suffered the same disease
  twice: it slept on the game thread (so PIE started only after its 30s
  timeout returned) AND its log marker string was case-wrong
  ("PIE: play in editor start" vs the actual "PIE: Play in editor").
  Symptom: gameplay_pie_start reports timeout while PIE starts fine
  immediately after.
- `Invoke-RestMethod` on Windows can fail against the listener while curl
  and Node connect fine; never conclude the listener is down from one
  client.
- A blocked game thread also blocks log flushing, editor input, and the
  very tick callbacks that would process a cancel - the freeze is total,
  not just slow.

## 6. Verification

- Call viewport_screenshot with the editor visible: the call must return
  in well under a second (not ~10s), the editor must not hitch, and the
  PNG must appear on disk within ~1-2s of the response
  (Saved/Screenshots/MCPBridge/, `viewport_` prefix; the native panel
  button writes `panel_` prefixed files).
- Raw listener response carries `status: pending` with
  `file_size_bytes: 0`; the MCP tool response carries `status: complete`
  with a real size.
- Failure signatures: 10s editor freeze on screenshot = a sleep crept
  back into the game-thread path; tool returns pending with size 0 =
  Node-side poller not running (stale dist or the mock's status field
  set to pending).
