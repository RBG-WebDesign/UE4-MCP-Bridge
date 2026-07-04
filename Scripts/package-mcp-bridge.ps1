#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$PluginPath,

    [string]$OutputRoot,

    [string]$RunUAT,

    [switch]$SourceZipOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
}

function ConvertTo-SafeName {
    param([string]$Value)
    return ($Value -replace "[^A-Za-z0-9_.-]", "_")
}

function Copy-PluginClean {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    $excludedDirectories = @("Binaries", "Build", "Intermediate", "Saved", ".git", ".vs", "DerivedDataCache")
    $sourceRoot = (Resolve-Path -LiteralPath $Source).Path.TrimEnd("\", "/")

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Destination | Out-Null

    $items = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force)
    foreach ($item in $items) {
        $relativePath = $item.FullName.Substring($sourceRoot.Length).TrimStart("\", "/")
        if ([string]::IsNullOrWhiteSpace($relativePath)) {
            continue
        }

        $parts = @($relativePath -split "[\\/]")
        $isExcluded = $false
        foreach ($part in $parts) {
            if ($excludedDirectories -contains $part) {
                $isExcluded = $true
                break
            }
        }

        if ($isExcluded) {
            continue
        }

        $target = Join-Path $Destination $relativePath
        if ($item.PSIsContainer) {
            if (-not (Test-Path -LiteralPath $target)) {
                New-Item -ItemType Directory -Path $target | Out-Null
            }
        }
        else {
            $parent = Split-Path -Parent $target
            if (-not (Test-Path -LiteralPath $parent)) {
                New-Item -ItemType Directory -Path $parent | Out-Null
            }
            Copy-Item -LiteralPath $item.FullName -Destination $target -Force
        }
    }
}

function Assert-PluginClean {
    param([string]$Root)

    $badDirectories = @("Binaries", "Build", "Intermediate", "Saved")
    foreach ($badDirectory in $badDirectories) {
        $matches = @(Get-ChildItem -LiteralPath $Root -Directory -Recurse -Force | Where-Object { $_.Name -eq $badDirectory })
        if ($matches.Count -gt 0) {
            throw "Package contains non-distributable folder: $($matches[0].FullName)"
        }
    }

    $rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd("\", "/")
    $tooLong = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Where-Object {
        $relativePath = $_.FullName.Substring($rootPath.Length).TrimStart("\", "/")
        $relativePath.Length -gt 170
    })
    if ($tooLong.Count -gt 0) {
        $relativePath = $tooLong[0].FullName.Substring($rootPath.Length).TrimStart("\", "/")
        throw "Package contains plugin-relative path longer than 170 characters: $relativePath"
    }
}

$repoRoot = Resolve-RepoRoot
if (-not $PluginPath) {
    $PluginPath = Join-Path $repoRoot "Plugins/MCPBridge"
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot "Releases"
}

$pluginRoot = (Resolve-Path -LiteralPath $PluginPath).Path
$upluginPath = Join-Path $pluginRoot "MCPBridge.uplugin"
if (-not (Test-Path -LiteralPath $upluginPath)) {
    throw "MCPBridge.uplugin was not found at: $upluginPath"
}

$descriptor = Get-Content -LiteralPath $upluginPath -Raw | ConvertFrom-Json
$versionName = ConvertTo-SafeName -Value $descriptor.VersionName
$engineVersion = ConvertTo-SafeName -Value $descriptor.EngineVersion

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$stageRoot = Join-Path $OutputRoot "_stage"
$stagePlugin = Join-Path $stageRoot "MCPBridge"
Copy-PluginClean -Source $pluginRoot -Destination $stagePlugin
Assert-PluginClean -Root $stagePlugin

$zipPath = Join-Path $OutputRoot "MCPBridge_UE$engineVersion`_v$versionName.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path $stagePlugin -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Source plugin zip: $zipPath"

if ($RunUAT -and -not $SourceZipOnly) {
    $runUATPath = (Resolve-Path -LiteralPath $RunUAT).Path
    $uatPackagePath = Join-Path $OutputRoot "MCPBridge_UAT_UE$engineVersion`_v$versionName"
    if (Test-Path -LiteralPath $uatPackagePath) {
        Remove-Item -LiteralPath $uatPackagePath -Recurse -Force
    }

    & $runUATPath BuildPlugin -Plugin="$upluginPath" -Package="$uatPackagePath" -Rocket
    if ($LASTEXITCODE -ne 0) {
        throw "RunUAT BuildPlugin failed with exit code $LASTEXITCODE"
    }

    Write-Host "UAT package: $uatPackagePath"
}

Remove-Item -LiteralPath $stageRoot -Recurse -Force
