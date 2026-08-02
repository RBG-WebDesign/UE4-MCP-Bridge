<#
.SYNOPSIS
Measures whether a UE4.27 editor with MCPBridge enabled closes normally and
releases its plugin DLLs so the project builds again.

.DESCRIPTION
One iteration is: build, launch, optionally exercise the bridge, close the
window normally, wait for the process to disappear, then build again. Each
iteration records the fields the shutdown investigation needs:

  build_before_seconds     the build that produced the binaries under test
  startup_complete_seconds wall time to the editor's startup-complete log line
  bridge_initialised       the plugin advertised a pipe and logged startup
  client_connected         a read-only command completed over the pipe
  shutdown_start           timestamp of the close request
  shutdown_callbacks       the MCPBridge lifecycle lines the editor logged
  subsystem_cleanup        the last LogExit line the editor reached
  close_seconds            wall time from close request to process exit
  process_exited           false means the hang reproduced
  pipe_remaining           a listening pipe left behind by a dead editor
  advertisement_remaining  Saved/MCPPuerTSBridge/pipe.txt left behind
  locked_binaries          plugin DLLs still held open
  build_after_ok           whether the project builds immediately afterwards

.PARAMETER Case
Which comparison case to run.

  plugin-off    MCPBridge disabled in the .uproject (restored afterwards)
  puerts-idle   plugin loaded, bridge inert via -MCPPuerTSBridgeDisabled,
                so PuerTS is present but no FJsEnv is ever created
  bridge-idle   default launch, no client ever connects
  read-only     default launch plus one puerts diagnostic command
  bt-smoke      default launch plus Scripts/behavior-tree-acceptance.mjs

.EXAMPLE
./Scripts/editor-shutdown-acceptance.ps1 -Case bridge-idle
./Scripts/editor-shutdown-acceptance.ps1 -Case bt-smoke -Iterations 5
#>
[CmdletBinding()]
param(
    [string]$Project = 'D:\Unreal Projects\BridgeInstallTest\BridgeInstallTest.uproject',

    [ValidateSet('bare', 'plugin-off', 'puerts-idle', 'bridge-idle', 'read-only', 'bt-smoke')]
    [string]$Case = 'bridge-idle',

    [int]$Iterations = 1,
    [int]$StartupTimeoutSeconds = 600,
    [int]$CloseTimeoutSeconds = 240,

    [string]$EngineRoot = "$(if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'D:\UE\UE_4.27' })",
    [string]$ReportPath,
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectPath = (Resolve-Path -LiteralPath $Project).Path
$projectDir = Split-Path -Parent $projectPath
$projectName = [IO.Path]::GetFileNameWithoutExtension($projectPath)
$editorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UE4Editor.exe'
$buildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$repoRoot = Split-Path -Parent $PSScriptRoot
$advertisePath = Join-Path $projectDir 'Saved\MCPPuerTSBridge\pipe.txt'
$tokenPath = Join-Path $projectDir 'Saved\MCPPuerTSBridge\token.txt'
$pluginBinaries = Join-Path $projectDir 'Plugins\MCPBridge\Binaries\Win64'

foreach ($required in @($editorExe, $buildBat)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Not found: $required. Set -EngineRoot or UE_ENGINE_ROOT." }
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class ShutdownAcceptanceWin32 {
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    /// <summary>Visible top-level windows owned by a process, as "handle|title".</summary>
    public static List<string> WindowsOf(uint target) {
        var found = new List<string>();
        EnumWindows((h, l) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            if (owner == target && IsWindowVisible(h)) {
                var text = new StringBuilder(512);
                GetWindowText(h, text, 512);
                if (text.Length > 0) found.Add(h.ToInt64() + "|" + text.ToString());
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

function Get-EditorWindows([int]$ProcessId) {
    [ShutdownAcceptanceWin32]::WindowsOf([uint32]$ProcessId) | ForEach-Object {
        $parts = $_ -split '\|', 2
        [pscustomobject]@{ Handle = [IntPtr][int64]$parts[0]; Title = $parts[1] }
    }
}

# ponytail: the Save Content prompt is a Slate-drawn window with no child controls,
# so its buttons cannot be found by handle. "Don't Save" sits at a fixed inset from
# the dialog's bottom-right corner. Fragile by nature; it is only reached when a case
# leaves dirty transients behind, and it logs whenever it fires so a mis-click is
# visible in the report rather than silent.
function Dismiss-SaveContentPrompt([IntPtr]$Handle) {
    $rect = New-Object ShutdownAcceptanceWin32+RECT
    if (-not [ShutdownAcceptanceWin32]::GetWindowRect($Handle, [ref]$rect)) { return $false }
    $null = [ShutdownAcceptanceWin32]::SetForegroundWindow($Handle)
    Start-Sleep -Milliseconds 400
    $null = [ShutdownAcceptanceWin32]::SetCursorPos($rect.Right - 171, $rect.Bottom - 31)
    Start-Sleep -Milliseconds 200
    [ShutdownAcceptanceWin32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [ShutdownAcceptanceWin32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    return $true
}

function Test-FileLocked([string]$Path) {
    try { $stream = [IO.File]::Open($Path, 'Open', 'ReadWrite', 'None'); $stream.Close(); return $false }
    catch { return $true }
}

function Get-LockedPluginBinaries {
    if (-not (Test-Path -LiteralPath $pluginBinaries)) { return @() }
    @(Get-ChildItem -LiteralPath $pluginBinaries -Filter *.dll |
        Where-Object { Test-FileLocked $_.FullName } |
        ForEach-Object { $_.Name })
}

function Get-BridgePipes {
    # Split-Path returns nothing useful for a \\.\pipe\ path, so trim the prefix directly.
    @([IO.Directory]::GetFiles('\\.\pipe\') |
        Where-Object { $_ -match 'UE427PuerTSMCP' } |
        ForEach-Object { $_ -replace '^\\\\\.\\pipe\\', '' })
}

# A process left behind by the shutdown bug keeps its DLLs open, and Windows refuses
# to overwrite them. It does allow them to be RENAMED, which is enough for the linker
# to write a fresh file, so a machine already carrying zombies can still build without
# a reboot. Only ever used before an iteration's own build. build_after is never
# cleared, because a locked binary there is exactly the failure being measured.
function Clear-LockedBuildOutputs {
    $cleared = @()
    foreach ($dir in @((Join-Path $projectDir 'Binaries\Win64'), $pluginBinaries)) {
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        foreach ($dll in Get-ChildItem -LiteralPath $dir -Filter *.dll) {
            if (-not (Test-FileLocked $dll.FullName)) { continue }
            try {
                $aside = '{0}.zombielocked-{1}' -f $dll.Name, [guid]::NewGuid().ToString('N').Substring(0, 8)
                Rename-Item -LiteralPath $dll.FullName -NewName $aside -ErrorAction Stop
                $cleared += $dll.Name
            }
            catch { Write-Warning "Could not move aside $($dll.Name): $($_.Exception.Message)" }
        }
    }
    if ($cleared.Count -gt 0) { Write-Host ("  cleared      : {0} locked binaries moved aside" -f $cleared.Count) }
    return $cleared
}

function Invoke-ProjectBuild {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $output = & $buildBat "${projectName}Editor" Win64 Development $projectPath -WaitMutex -NoHotReload 2>&1
    $timer.Stop()
    [pscustomobject]@{
        ok      = ($LASTEXITCODE -eq 0)
        seconds = [math]::Round($timer.Elapsed.TotalSeconds, 1)
        tail    = ($output | Select-Object -Last 6) -join "`n"
    }
}

function Get-CurrentEditorLog {
    $logDir = Join-Path $projectDir 'Saved\Logs'
    if (-not (Test-Path -LiteralPath $logDir)) { return $null }
    Get-ChildItem -LiteralPath $logDir -Filter "$projectName*.log" |
        Where-Object { $_.Name -notmatch 'backup' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}

function Read-LogSnapshot([string]$Path) {
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return @() }
    # The editor holds the log open, so read it shared.
    $stream = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
    try { $reader = New-Object IO.StreamReader($stream); return $reader.ReadToEnd() -split "`r?`n" }
    finally { $stream.Dispose() }
}

# One read-only command over the named pipe, in the wire shape mcp-server/src/puerts-client.ts uses.
function Invoke-ReadOnlyProbe {
    if (-not (Test-Path -LiteralPath $tokenPath) -or -not (Test-Path -LiteralPath $advertisePath)) {
        return [pscustomobject]@{ connected = $false; detail = 'no token or pipe advertisement' }
    }
    $token = (Get-Content -LiteralPath $tokenPath -Raw).Trim()
    $pipe = (Get-Content -LiteralPath $advertisePath -Raw).Trim() -replace '^\\\\\.\\pipe\\', ''
    try {
        $client = New-Object IO.Pipes.NamedPipeClientStream('.', $pipe, 'InOut')
        $client.Connect(10000)
        $request = @{
            id         = [guid]::NewGuid().ToString()
            command    = 'diagnostic'
            tool       = 'diagnostic'
            params     = @{}
            timeout_ms = 10000
            auth       = $token
        } | ConvertTo-Json -Compress
        $writer = New-Object IO.StreamWriter($client); $writer.AutoFlush = $true
        $writer.WriteLine($request)
        # PipeStream ignores ReadTimeout, and a pipe instance owned by a dead editor
        # accepts the connection and then never answers, so a bare ReadLine can block
        # forever. Bound it explicitly.
        $reader = New-Object IO.StreamReader($client)
        $read = $reader.ReadLineAsync()
        if (-not $read.Wait(20000)) {
            $client.Dispose()
            return [pscustomobject]@{ connected = $false; detail = 'pipe accepted the connection but never answered' }
        }
        $reply = $read.Result
        $client.Dispose()
        $parsed = $reply | ConvertFrom-Json
        return [pscustomobject]@{ connected = [bool]$parsed.success; detail = $parsed.message }
    }
    catch { return [pscustomobject]@{ connected = $false; detail = $_.Exception.Message } }
}

function Set-PluginsEnabled([string[]]$Names, [bool]$Enabled) {
    $json = Get-Content -LiteralPath $projectPath -Raw | ConvertFrom-Json
    foreach ($plugin in $json.Plugins) { if ($Names -contains $plugin.Name) { $plugin.Enabled = $Enabled } }
    ($json | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath $projectPath -Encoding utf8
}

$launchArgs = @('"{0}"' -f $projectPath)
$expectBridge = $true
$disablePlugins = @()
switch ($Case) {
    'bare' { $expectBridge = $false; $disablePlugins = @('MCPBridge', 'Puerts') }
    'plugin-off' { $expectBridge = $false; $disablePlugins = @('MCPBridge') }
    'puerts-idle' { $launchArgs += '-MCPPuerTSBridgeDisabled'; $expectBridge = $false }
}

$originalUproject = Get-Content -LiteralPath $projectPath -Raw
if ($disablePlugins.Count -gt 0) { Set-PluginsEnabled $disablePlugins $false }

$results = @()
try {
    for ($iteration = 1; $iteration -le $Iterations; $iteration++) {
        Write-Host "`n=== $Case iteration $iteration of $Iterations ===" -ForegroundColor Cyan

        $clearedBefore = Clear-LockedBuildOutputs
        $buildBefore = if ($SkipBuild -and $iteration -gt 1) {
            [pscustomobject]@{ ok = $true; seconds = 0; tail = 'skipped' }
        } else { Invoke-ProjectBuild }
        Write-Host ("  build before : {0} ({1}s)" -f $(if ($buildBefore.ok) { 'ok' } else { 'FAILED' }), $buildBefore.seconds)
        if (-not $buildBefore.ok) {
            $results += [pscustomobject]@{ case = $Case; iteration = $iteration; build_before_ok = $false; build_before_tail = $buildBefore.tail }
            break
        }

        # Remove the previous advertisement so a stale file cannot be mistaken for a live one.
        Remove-Item -LiteralPath $advertisePath -ErrorAction SilentlyContinue

        $launchAt = Get-Date
        $editor = Start-Process -FilePath $editorExe -ArgumentList $launchArgs -PassThru
        $logPath = $null
        $startupSeconds = $null
        $deadline = $launchAt.AddSeconds($StartupTimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 3
            if ($editor.HasExited) { break }
            if (-not $logPath) { $logPath = Get-CurrentEditorLog }
            $lines = Read-LogSnapshot $logPath
            # Readiness is the engine's own completion line, not the appearance of a
            # window and not the bridge's module-startup line. Both of those fire while
            # the editor is still initialising, and closing it then measures a close
            # during startup rather than a close of a loaded editor.
            $ready = @($lines | Where-Object { $_ -match 'LogLoad: \(Engine Initialization\) Total time:' }).Count -gt 0
            if ($ready -and $expectBridge) {
                $ready = @($lines | Where-Object { $_ -match 'MCPBridge lifecycle: startup complete' }).Count -gt 0
            }
            if ($ready) {
                # The asset registry keeps working past that line; give the editor a
                # moment to go idle so the close is a genuinely quiet one.
                Start-Sleep -Seconds 15
                $startupSeconds = [math]::Round(((Get-Date) - $launchAt).TotalSeconds, 1)
                break
            }
        }
        Write-Host ("  startup      : {0}" -f $(if ($startupSeconds) { "$startupSeconds s" } else { 'TIMED OUT' }))

        $bridgeInitialised = Test-Path -LiteralPath $advertisePath
        $probe = if ($Case -in @('read-only', 'bt-smoke')) { Invoke-ReadOnlyProbe } else {
            [pscustomobject]@{ connected = $false; detail = 'no client for this case' }
        }
        Write-Host ("  bridge init  : {0}   client: {1}" -f $bridgeInitialised, $probe.connected)

        $btSmoke = $null
        if ($Case -eq 'bt-smoke') {
            Push-Location $repoRoot
            try {
                $env:MCP_UNREAL_PROJECT_ROOT = $projectDir
                $env:MCP_PUERTS_TOKEN_PATH = $tokenPath
                # Windows PowerShell 5.1 wraps a native executable's stderr in
                # ErrorRecords, which $ErrorActionPreference = 'Stop' turns into a
                # terminating error. The bridge server writes its banner to stderr, so
                # without this the harness aborts before it can close the editor.
                $previousPreference = $ErrorActionPreference
                $ErrorActionPreference = 'Continue'
                try { $btOutput = & npm run smoke:bt 2>&1 }
                finally { $ErrorActionPreference = $previousPreference }
                $btSmoke = [pscustomobject]@{ ok = ($LASTEXITCODE -eq 0); tail = ($btOutput | Select-Object -Last 8) -join "`n" }
            }
            finally { Pop-Location }
            Write-Host ("  bt smoke     : {0}" -f $btSmoke.ok)
        }

        $preCloseLength = (Read-LogSnapshot $logPath).Count
        $shutdownStart = Get-Date
        $null = $editor.CloseMainWindow()
        Write-Host ("  close sent   : {0:HH:mm:ss}" -f $shutdownStart)

        $closeDeadline = $shutdownStart.AddSeconds($CloseTimeoutSeconds)
        $promptDismissed = $false
        while ((Get-Date) -lt $closeDeadline) {
            Start-Sleep -Seconds 2
            $editor.Refresh()
            if ($editor.HasExited -and -not (Get-Process -Id $editor.Id -ErrorAction SilentlyContinue)) { break }
            $prompt = Get-EditorWindows $editor.Id | Where-Object { $_.Title -match 'Save Content' } | Select-Object -First 1
            if ($prompt -and -not $promptDismissed) {
                Write-Host '  save prompt  : dismissing with Don''t Save'
                $promptDismissed = Dismiss-SaveContentPrompt $prompt.Handle
                $closeDeadline = (Get-Date).AddSeconds($CloseTimeoutSeconds)
            }
        }
        $stillPresent = [bool](Get-Process -Id $editor.Id -ErrorAction SilentlyContinue)
        $closeSeconds = [math]::Round(((Get-Date) - $shutdownStart).TotalSeconds, 1)
        Write-Host ("  close        : {0} after {1}s" -f $(if ($stillPresent) { 'HUNG' } else { 'exited' }), $closeSeconds)

        $lines = Read-LogSnapshot $logPath
        $shutdownLines = @($lines | Select-Object -Skip $preCloseLength | Where-Object { $_ -match 'MCPBridge lifecycle' })
        $lastExitLine = @($lines | Where-Object { $_ -match 'LogExit:' }) | Select-Object -Last 1

        $buildAfter = Invoke-ProjectBuild
        Write-Host ("  build after  : {0} ({1}s)" -f $(if ($buildAfter.ok) { 'ok' } else { 'FAILED' }), $buildAfter.seconds)

        # Counted here rather than derived from the JSON later: ConvertTo-Json renders an
        # empty collection as {}, which reads back as an object and counts as one item.
        $pipesLeft = @(Get-BridgePipes)
        $lockedLeft = @(Get-LockedPluginBinaries)

        $results += [pscustomobject]@{
            case                     = $Case
            iteration                = $iteration
            build_before_ok          = $buildBefore.ok
            build_before_seconds     = $buildBefore.seconds
            cleared_before_build     = $clearedBefore
            startup_complete_seconds = $startupSeconds
            bridge_initialised       = $bridgeInitialised
            client_connected         = $probe.connected
            client_detail            = $probe.detail
            bt_smoke_ok              = $(if ($btSmoke) { $btSmoke.ok } else { $null })
            shutdown_start           = $shutdownStart.ToString('o')
            save_prompt_dismissed    = $promptDismissed
            shutdown_callbacks       = $shutdownLines
            subsystem_cleanup        = $lastExitLine
            close_seconds            = $closeSeconds
            process_exited           = (-not $stillPresent)
            pipe_remaining           = ($pipesLeft -join ', ')
            pipe_remaining_count     = $pipesLeft.Count
            advertisement_remaining  = (Test-Path -LiteralPath $advertisePath)
            locked_binaries          = ($lockedLeft -join ', ')
            locked_binary_count      = $lockedLeft.Count
            build_after_ok           = $buildAfter.ok
            build_after_seconds      = $buildAfter.seconds
            build_after_tail         = $(if ($buildAfter.ok) { '' } else { $buildAfter.tail })
            log_path                 = $logPath
        }

        if ($stillPresent) {
            Write-Warning "Iteration $iteration left PID $($editor.Id) behind. Later iterations cannot build; stopping."
            break
        }
    }
}
finally {
    if ($disablePlugins.Count -gt 0) { Set-Content -LiteralPath $projectPath -Value $originalUproject -NoNewline }
}

$passed = @($results | Where-Object {
        $_.process_exited -and $_.build_after_ok -and
        -not $_.advertisement_remaining -and $_.locked_binary_count -eq 0
    }).Count
Write-Host ("`n{0}: {1} of {2} iterations closed cleanly and rebuilt." -f $Case, $passed, $results.Count) -ForegroundColor $(if ($passed -eq $Iterations) { 'Green' } else { 'Red' })

if (-not $ReportPath) {
    $ReportPath = Join-Path $repoRoot ("reports\shutdown-acceptance-{0}.json" -f $Case)
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ReportPath) | Out-Null
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding utf8
Write-Host "Report: $ReportPath"

if ($passed -ne $Iterations) { exit 1 }
