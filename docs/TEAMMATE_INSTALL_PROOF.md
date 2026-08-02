# Teammate install proof

## This test has never been run

Nobody has taken this repository from clone to a working bridge without already
knowing how it works. `docs/PROJECT_FINISH_SCOREBOARD.json` records the same
thing about the P1 runbook row: its "fresh teammate completes install" test has
still never been run.

So this file is not a report. It is the script for the run, written so it can be
handed to someone and the failures recorded. Nothing below has an outcome
recorded next to it, because nothing below has happened.

## Who this is for

One person, no prior contact with this repository, who has:

- Windows 10 or 11
- UE4.27 installed, and a UE4.27 **C++** project to install into. A Blueprint-only
  project has no build pipeline and cannot compile the plugin
- Node.js 18 or newer
- Visual Studio with the C++ toolchain that UE4.27 requires
- write access to the machine's `Documents`, `Temp` and the project directory

The person running this should NOT be given help beyond what is written here. The
whole value of the exercise is the list of places they got stuck. If they ask a
question, write the question down and answer it, then record that the answer was
not in the documentation.

## How to record results

Fill in the table at the bottom as you go. One row per step. `PASS` means it did
what this file says it does. Anything else is `FAIL`, including "it worked but
only after I did something not written here". Paste the exact error text. Do not
summarise it, and do not clean it up.

A step that had to be repeated, or that took materially longer than the estimate,
is a finding too. Note it.

## The steps

### Step 0. Record the starting state

```powershell
node --version
git --version
$env:UE_ENGINE_ROOT
```

`UE_ENGINE_ROOT` must point at the directory that contains `Engine\Source`. If it
is empty, set it:

```powershell
[Environment]::SetEnvironmentVariable("UE_ENGINE_ROOT", "D:\UE\UE_4.27", "User")
```

Then open a new PowerShell window, because the old one will not see it.

Expected: three version strings and a path that exists. About 2 minutes.

### Step 1. Clone and install dependencies

```powershell
git clone <repository url> D:\Bridge
cd D:\Bridge
npm ci
```

Expected: `npm ci` finishes with no error. About 5 minutes on a cold npm cache.

### Step 2. Get the PuerTS bundle

**This is the step with no automation, and it is the one most likely to stop
you.** The bundle is 369 MB and is deliberately not in Git. Nothing in this
repository fetches it.

What you need at `D:\Bridge\Plugins\Puerts`:

| Field | Value |
|---|---|
| repository | `https://github.com/Tencent/puerts` |
| tag | `Unreal_v1.0.9` |
| commit | `838ab762d83021c0407f13120f4004dcaf70cffe` |
| subtree | `unreal/Puerts` |

Those are read from `Plugins\Puerts.lock.json`, which is the only tracked record
of what the bundle must contain. The lock file lists 1038 files with their
hashes.

The bundle also carries one deliberate local modification to a `Build.cs`, which
is why a clean upstream checkout may not verify. If the check in step 3 reports
a difference in a `Build.cs`, record it exactly and stop; that is a finding about
this document, not about your machine.

Ask whoever handed you this file where to get the bundle. Record their answer.
If the answer is "copy it off my machine", write that down: it means the
fresh-clone path does not exist yet.

Expected: unknown. This step is the reason for the exercise.

### Step 3. Verify the bundle, then build

```powershell
cd D:\Bridge
node Scripts\check-puerts-pin.mjs --strict
npm run build
npm run verify
```

Use `--strict`. Without it the check **skips** a missing bundle and exits 0, so
the plain `npm run check:puerts` will not tell you the bundle is absent.

Expected: `--strict` reports 1038 files verified. `npm run verify` runs build,
unit tests, the pin check, the tool inventory and a stdio smoke test, and ends
with no failure. About 3 minutes.

If `npm run build` prints `SKIP: pinned bundle support files not found`, step 2
did not work and the plugin you are about to install has an incomplete
JavaScript runtime. Do not continue. Record it.

### Step 4. Prove the editor-free acceptance passes before touching a project

```powershell
npm run test:editor-free
```

Expected: three suites, all green. Measured at 26 seconds on the development
machine on 2026-08-02, so budget under a minute. They install into a
throwaway directory under `Temp`, package a zip, and attack the security
boundary. If any of these fails, the problem is the checkout and not your
project, and installing into a real project will only add variables.

### Step 5. Install into your project

```powershell
.\Scripts\install-mcp-bridge.ps1 -Project "D:\Path\To\YourProject"
```

Expected: ends with `MCP Bridge install complete`. About 2 minutes.

Check by hand that these now exist in the project, because the installer is the
component with the least live evidence behind it:

- `Plugins\MCPBridge\MCPBridge.uplugin`
- `Plugins\MCPBridge\Content\JavaScript\registry.js`
- `Plugins\Puerts\Puerts.uplugin`
- `Config\DefaultEngine.ini` containing an `[MCPPuerTSBridge]` section with one
  `PipeName=` line
- `.mcp.json` whose `unreal-bridge` entry points at
  `D:/Bridge/mcp-server/dist/index.js`

`PythonScriptPlugin` should NOT be enabled in the `.uproject`. The legacy HTTP
listener is off unless somebody asks for it.

### Step 6. Build the editor target

```powershell
npm run install:sync -- --project "D:\Path\To\YourProject"
```

This copies the changed plugin files, then runs UBT, then writes
`Plugins\MCPBridge\MCPBridgeInstall.json`.

Close the Unreal editor first if it is open. UBT cannot replace a DLL the editor
has loaded, and the failure message is about a locked file rather than about the
editor.

Expected: a UBT build that succeeds, then `wrote ...MCPBridgeInstall.json`, then
a re-check that passes. First build from cold: 10 to 40 minutes. This is the
longest step by a wide margin.

### Step 7. Confirm the install is the checkout

```powershell
npm run install:check -- --project "D:\Path\To\YourProject"
```

Expected: every group `OK` and `install is current`.

This is the gate every live acceptance script calls before it connects. If it
fails here it will fail there, and a live run that gets past a stale install
proves nothing about the code under review.

### Step 8. Open the editor

Open the project in UE4.27. Accept the rebuild prompt if one appears.

Then confirm the editor advertised itself:

```powershell
Get-Content "D:\Path\To\YourProject\Saved\MCPPuerTSBridge\session.json"
```

Expected: JSON with `"schema_version": 1`, a `session_id`, a `pipe_name` of the
form `\\.\pipe\UE427PuerTSMCP_<project>_<hash>`, and
`"shutdown_state": "running"`. It appears within a few seconds of the editor
finishing startup.

If the file never appears, the plugin loaded but the bridge service did not
start, or the plugin did not load at all. Check `Window > MCP Bridge` for the
status panel. Record which.

### Step 9. Talk to it

```powershell
$env:MCP_UNREAL_PROJECT_ROOT = "D:\Path\To\YourProject"
npm run smoke:editor
```

`smoke:editor` FAILs rather than SKIPs when the editor is down, which is the
point of using it here instead of `npm run smoke`.

Expected: a green smoke run against the live editor. About 1 minute.

### Step 10. Point a client at it

Open your MCP client in the project folder, restart it so it picks up
`.mcp.json`, and ask it to test the Unreal connection. Expect the `puerts_*`
tools to be listed.

If the tools are absent, the usual cause is that the client was started before
`.mcp.json` existed. MCP servers connect at client startup.

## Results

Fill in during the run. Leave a row blank only if you did not reach it.

| Step | Result | Time | Exact error, or what was not in the docs |
|---|---|---|---|
| 0 record starting state | | | |
| 1 clone and npm ci | | | |
| 2 obtain the PuerTS bundle | | | |
| 3 verify pin and build | | | |
| 4 editor-free acceptance | | | |
| 5 install into the project | | | |
| 6 build the editor target | | | |
| 7 install:check | | | |
| 8 editor advertises a session | | | |
| 9 smoke:editor | | | |
| 10 client lists the tools | | | |

### Questions asked that this document did not answer

One line each. This list is the deliverable; the table above is just the
scaffolding for producing it.

### Verdict

- [ ] A person with no prior contact with this repository reached step 10
      without help.
- [ ] They reached step 10 but needed help. List the steps.
- [ ] They did not reach step 10. Name the step that stopped them.

## Predicted failures

Written before the run so the run can contradict them. If a prediction is wrong,
that is a result worth recording too.

1. **Step 2 stops them.** There is no automation and no documented source for the
   bundle. This is the most likely outcome.
2. **Step 6 takes far longer than expected, or fails on a locked DLL** because the
   editor was open.
3. **Step 3 passes with a warning nobody reads.** `npm run check:puerts` inside
   `npm run verify` skips a missing bundle and exits 0, so a checkout with no
   bundle still reports a green verify. The `--strict` line in step 3 exists to
   catch exactly this, and someone following the shorter path in
   `docs/SETUP.md` would not run it.
4. **Step 10 finds no tools** because the client was already running.
