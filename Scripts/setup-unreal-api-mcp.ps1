<#
.SYNOPSIS
Creates the dedicated virtual environment the unreal-api MCP server runs from.

.DESCRIPTION
unreal-api-mcp does "from mcp.server.fastmcp import FastMCP". The newest mcp is
2.0.0, where fastmcp was removed in the rewrite, so an unpinned install crashes
on import every launch. This pins mcp to a 1.x that still ships fastmcp.

A plain venv is used rather than "uv tool install" on purpose. uv hardlinks
packages from a shared cache, and on Windows a pywin32 DLL already mapped by
another uv-managed Python process cannot be replaced through any hardlink to
it, so the install fails whenever such a process is running. venv plus pip
copies, and never touches that cache.

Run once per machine. Idempotent: an existing environment is reused unless
-Recreate is passed.
#>
param(
    [string]$Python = "python",
    [string]$McpPin = "mcp==1.29.0",
    [switch]$Recreate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$venv = Join-Path $repoRoot '.venv-unreal-api'
$exe = Join-Path $venv 'Scripts\unreal-api-mcp.exe'
$venvPython = Join-Path $venv 'Scripts\python.exe'

if ($Recreate -and (Test-Path -LiteralPath $venv)) {
    Write-Host "Removing existing environment at $venv"
    Remove-Item -LiteralPath $venv -Recurse -Force
}

if (-not (Test-Path -LiteralPath $venvPython)) {
    Write-Host "Creating virtual environment: $venv"
    & $Python -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw "Could not create a virtual environment with '$Python'. Install Python 3.10+ or pass -Python <path>." }
}

Write-Host "Installing unreal-api-mcp with $McpPin"
& $venvPython -m pip install --disable-pip-version-check --quiet --upgrade pip
& $venvPython -m pip install --disable-pip-version-check --quiet "unreal-api-mcp" $McpPin
if ($LASTEXITCODE -ne 0) { throw "pip install failed." }

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Install finished but $exe is missing. The package may have changed its entry point."
}

# Prove the import that actually breaks, rather than trusting that pip exited 0.
& $venvPython -c "import mcp, mcp.server.fastmcp; print('mcp', mcp.__version__ if hasattr(mcp,'__version__') else '(version unknown)', 'fastmcp OK')"
if ($LASTEXITCODE -ne 0) { throw "mcp.server.fastmcp still does not import. The pin '$McpPin' may be wrong." }

Write-Host "unreal-api ready: $exe"
Write-Host "The committed .mcp.json reaches it through Scripts/start-unreal-api-mcp.mjs, so no path needs editing."
