<#
.SYNOPSIS
Points this bridge at a UE4.27 project and makes both MCP servers work for it.

.DESCRIPTION
One command per project:

    .\Scripts\setup-unreal-project.ps1 -Project "D:\Unreal Projects\MyGame\MyGame.uproject"

then restart the MCP client. No committed file changes: the machine-local paths
land in bridge.local.json, which is gitignored.

This sequences work that already exists rather than reimplementing it:
verify_project_version.py refuses anything that is not 4.27, bridge-install.mjs
installs the plugin and builds the target editor, and ue427.py doctor is the
validation pass. Nothing here duplicates those; fix them there.

.PARAMETER Project
Path to the .uproject, or the directory containing it.

.PARAMETER EngineRoot
Engine root (the directory containing Engine/Source). Defaults to
UE_ENGINE_ROOT, then whatever bridge.local.json already records.

.PARAMETER SkipPluginInstall
Record the project and skip copying and compiling the plugin. Use when the
target already has a current install, since the build is the slow step.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Project,

    [string]$EngineRoot,
    [switch]$SkipPluginInstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$localConfigPath = Join-Path $repoRoot 'bridge.local.json'

function Step($text) { Write-Host "`n== $text" }

# --- 1. Resolve the project ------------------------------------------------
$resolved = (Resolve-Path -LiteralPath $Project -ErrorAction SilentlyContinue)
if (-not $resolved) { throw "Project path not found: $Project" }
$projectPath = $resolved.Path

if (Test-Path -LiteralPath $projectPath -PathType Container) {
    $found = @(Get-ChildItem -LiteralPath $projectPath -Filter '*.uproject' -File)
    if ($found.Count -ne 1) {
        throw "Expected exactly one .uproject in $projectPath, found $($found.Count). Pass the .uproject directly."
    }
    $uproject = $found[0].FullName
} else {
    if (-not $projectPath.EndsWith('.uproject')) { throw "Not a .uproject: $projectPath" }
    $uproject = $projectPath
}
$projectRoot = Split-Path -Parent $uproject
Step "Project: $uproject"

# --- 2. Refuse anything that is not 4.27 -----------------------------------
Step "Verifying engine version"
& python (Join-Path $repoRoot 'skills\unreal-engine-4-27\scripts\verify_project_version.py') $projectRoot
if ($LASTEXITCODE -ne 0) { throw "This bridge supports Unreal Engine 4.27 only. Nothing was changed." }

# --- 3. Resolve and validate the engine root -------------------------------
Step "Resolving engine root"
if (-not $EngineRoot) { $EngineRoot = $env:UE_ENGINE_ROOT }
if (-not $EngineRoot -and (Test-Path -LiteralPath $localConfigPath)) {
    $existing = Get-Content -LiteralPath $localConfigPath -Raw | ConvertFrom-Json
    if ($existing.PSObject.Properties.Name -contains 'UE_ENGINE_ROOT') { $EngineRoot = $existing.UE_ENGINE_ROOT }
}
if (-not $EngineRoot) {
    throw "No engine root. Pass -EngineRoot <path>, or set UE_ENGINE_ROOT. It is the directory containing Engine\Source."
}
$EngineRoot = $EngineRoot -replace '\\', '/'
if (-not (Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Source'))) {
    throw "No Engine/Source under '$EngineRoot'. That is not an engine root."
}
Write-Host "  $EngineRoot"

# --- 4. Python environment for unreal-api ----------------------------------
Step "Preparing the unreal-api environment"
& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'setup-unreal-api-mcp.ps1')
if ($LASTEXITCODE -ne 0) { throw "unreal-api environment setup failed." }

# --- 5. Build the bridge server if it is missing ----------------------------
Step "Building the MCP server"
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'mcp-server\dist\index.js'))) {
    Push-Location $repoRoot
    try { & npm run build; if ($LASTEXITCODE -ne 0) { throw "npm run build failed." } }
    finally { Pop-Location }
} else {
    Write-Host "  mcp-server/dist already present, leaving it alone. Run 'npm run build' after changing src."
}

# --- 6. Record the machine-local paths -------------------------------------
# Written before the plugin install so a failed build still leaves a usable
# config; the install is re-runnable, the path record does not need to be.
Step "Recording bridge.local.json"
$config = [ordered]@{
    UE_ENGINE_ROOT          = $EngineRoot
    MCP_UNREAL_PROJECT_ROOT = ($projectRoot -replace '\\', '/')
}
if (Test-Path -LiteralPath $localConfigPath) {
    $previous = Get-Content -LiteralPath $localConfigPath -Raw | ConvertFrom-Json
    foreach ($name in @('MCP_PUERTS_SESSION_ID', 'UNREAL_API_MCP_EXE')) {
        if ($previous.PSObject.Properties.Name -contains $name) { $config[$name] = $previous.$name }
    }
}
# WriteAllText, not Set-Content -Encoding utf8: on Windows PowerShell 5.1 that
# switch writes a UTF-8 BOM, and JSON.parse in Node rejects a leading BOM, so
# every reader on the JavaScript side silently saw no config at all.
[System.IO.File]::WriteAllText($localConfigPath, ($config | ConvertTo-Json))
Write-Host "  $localConfigPath -> $($config.MCP_UNREAL_PROJECT_ROOT)"

# --- 7. Install the plugin and build the target editor ---------------------
if ($SkipPluginInstall) {
    Step "Skipping plugin install (-SkipPluginInstall)"
} else {
    Step "Installing the MCPBridge plugin and building the editor"
    Push-Location $repoRoot
    try {
        & node (Join-Path $repoRoot 'Scripts\bridge-install.mjs') --sync --project $projectRoot
        if ($LASTEXITCODE -ne 0) { throw "Plugin install or editor build failed." }
    } finally { Pop-Location }
}

# --- 8. Validate ------------------------------------------------------------
Step "Validating"
Push-Location $repoRoot
try {
    $env:UE_ENGINE_ROOT = $EngineRoot
    $env:MCP_UNREAL_PROJECT_ROOT = $config.MCP_UNREAL_PROJECT_ROOT
    & python (Join-Path $repoRoot 'Scripts\ue427.py') doctor
} finally { Pop-Location }

Write-Host @"

Done. Next:
  1. Launch the editor:  .\Scripts\start-ue4-project.ps1 "$uproject"
  2. Restart your MCP client so it reconnects (MCP servers register at startup).
  3. Both servers now resolve their paths from bridge.local.json. Nothing
     machine-specific was written to a tracked file.
"@
