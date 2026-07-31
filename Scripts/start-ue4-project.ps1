[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Project,

    [string]$EditorPath = "$(if ($env:UE_ENGINE_ROOT) { Join-Path $env:UE_ENGINE_ROOT 'Engine/Binaries/Win64/UE4Editor.exe' } else { 'D:/UE/UE_4.27/Engine/Binaries/Win64/UE4Editor.exe' })",

    [switch]$CloseExisting,
    [switch]$AllowAdditional
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectPath = (Resolve-Path -LiteralPath $Project).Path
$editorExe = (Resolve-Path -LiteralPath $EditorPath).Path
$existing = @(Get-Process -Name UE4Editor -ErrorAction SilentlyContinue)

if ($existing.Count -gt 0 -and -not $AllowAdditional) {
    $details = $existing | ForEach-Object { "PID $($_.Id), started $($_.StartTime)" }
    if (-not $CloseExisting) {
        throw "Refusing to start another UE4 editor. Existing process(es): $($details -join '; '). Review them, then rerun with -CloseExisting to close them or -AllowAdditional to intentionally open another editor."
    }

    foreach ($process in $existing) {
        if ($PSCmdlet.ShouldProcess("UE4Editor PID $($process.Id)", 'force close before launching requested project')) {
            Stop-Process -Id $process.Id -Force
        }
    }
}

if ($PSCmdlet.ShouldProcess($projectPath, "launch UE4 editor using $editorExe")) {
    Start-Process -FilePath $editorExe -ArgumentList ('"{0}"' -f $projectPath)
    Write-Host "Started UE4 editor for $projectPath"
}
