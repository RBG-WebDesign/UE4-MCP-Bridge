<#
.SYNOPSIS
Launches (or stops) the UE4.27 editor for a project, refusing to pile up
duplicate UE4Editor.exe processes by accident.

.DESCRIPTION
The -CloseExisting / -AllowAdditional switches ARE the confirmation gate:
each requires the caller to deliberately opt in, so this script does not
also layer PowerShell's own ShouldProcess/-Confirm prompt on top - that
prompt has no console to read from under `-NonInteractive` and just throws.

After launch, waits for Saved\MCPPuerTSBridge\session.json to appear so the
caller knows the bridge is actually up, not just that a process started.

.PARAMETER StopOnly
Close any running UE4Editor process(es) for this project's engine and
return without launching a new one.

.PARAMETER Unattended
Launch with -unattended, which UE4.27 checks via FApp::IsUnattended() to
suppress interactive prompts that only a human can answer - notably the
"Restore Packages" dialog FPackageAutoSaver::OfferToRestorePackages shows on
next launch after this same script's -CloseExisting/-StopOnly force-killed a
prior editor (PackageAutoSaver.cpp:300), which blocks Slate/viewport
initialization until dismissed. Not the default: a human's own interactive
session may want other prompts -unattended also suppresses (e.g. source
control checkout dialogs) to actually ask. Use this for bridge-driven
automated rebuild/relaunch/test cycles, which never want any UI interaction.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Project,

    [string]$EditorPath = "$(if ($env:UE_ENGINE_ROOT) { Join-Path $env:UE_ENGINE_ROOT 'Engine/Binaries/Win64/UE4Editor.exe' } else { 'D:/UE/UE_4.27/Engine/Binaries/Win64/UE4Editor.exe' })",

    [switch]$CloseExisting,
    [switch]$AllowAdditional,
    [switch]$StopOnly,
    [int]$ReadyTimeoutSeconds = 120,
    [switch]$Unattended
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectPath = (Resolve-Path -LiteralPath $Project).Path
$projectDir = Split-Path -Parent $projectPath
$sessionFile = Join-Path $projectDir 'Saved\MCPPuerTSBridge\session.json'
$existing = @(Get-Process -Name UE4Editor -ErrorAction SilentlyContinue)

if ($existing.Count -gt 0 -and -not $AllowAdditional) {
    $details = $existing | ForEach-Object { "PID $($_.Id), started $($_.StartTime)" }
    if (-not $CloseExisting -and -not $StopOnly) {
        throw "Refusing to start another UE4 editor. Existing process(es): $($details -join '; '). Review them, then rerun with -CloseExisting to close them or -AllowAdditional to intentionally open another editor."
    }

    foreach ($process in $existing) {
        Write-Host "Closing UE4Editor PID $($process.Id)..."
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit(15000) | Out-Null
    }
}

if ($StopOnly) {
    Write-Host "Stopped $($existing.Count) UE4Editor process(es). -StopOnly set, not launching."
    return
}

$editorExe = (Resolve-Path -LiteralPath $EditorPath).Path

# A crashed prior editor can leave a stale session.json behind; remove it so
# readiness below can't be fooled by yesterday's session.
if (Test-Path -LiteralPath $sessionFile) {
    Remove-Item -LiteralPath $sessionFile -Force
}

$launchArgs = @('"{0}"' -f $projectPath)
if ($Unattended) { $launchArgs += '-unattended' }
$proc = Start-Process -FilePath $editorExe -ArgumentList $launchArgs -PassThru
Write-Host "Launched UE4 editor (PID $($proc.Id)) for $projectPath. Waiting up to ${ReadyTimeoutSeconds}s for the bridge session..."

$deadline = (Get-Date).AddSeconds($ReadyTimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $sessionFile) {
        Write-Host "Bridge session ready: $sessionFile"
        return
    }
    if ($proc.HasExited) {
        throw "UE4Editor (PID $($proc.Id)) exited before the bridge session appeared (exit code $($proc.ExitCode)). Check $projectDir\Saved\Logs for the crash."
    }
    Start-Sleep -Seconds 2
}
throw "Timed out after ${ReadyTimeoutSeconds}s waiting for $sessionFile. Editor PID $($proc.Id) is still running; check $projectDir\Saved\Logs for what it's stuck on."
