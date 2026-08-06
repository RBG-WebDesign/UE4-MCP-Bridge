#requires -Version 5.1

# Produces a release zip containing BOTH plugins a recipient needs: MCPBridge
# and the pinned PuerTS bundle it declares a dependency on. The bundle is
# verified against Plugins/Puerts.lock.json first, and so is the puerts support
# subtree staged inside MCPBridge/Content/JavaScript. Either check failing stops
# the packaging: a zip that cannot load is worse than no zip.

[CmdletBinding()]
param(
    [string]$PluginPath,

    [string]$OutputRoot,

    [string]$RunUAT,

    # Pinned PuerTS bundle to vendor. Defaults to <repo>/Plugins/Puerts, which
    # is .gitignored, so a worktree or clean clone has to point at one.
    [string]$PuertsPath,

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

    $excludedDirectories = @("Binaries", "Build", "Intermediate", "Saved", ".git", ".vs", "DerivedDataCache", "__pycache__")
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

    $badDirectories = @("Binaries", "Build", "Intermediate", "Saved", "__pycache__", ".vs")
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
# Gate 1: the bundle itself, vendored into the zip. MCPBridge.uplugin declares
# a dependency on Puerts and MCPBridgePuerTS.Build.cs links its JsEnv module, so
# a zip without it is a plugin UE4 refuses to load.
if (-not $PuertsPath) {
    $PuertsPath = Join-Path $repoRoot "Plugins/Puerts"
}
if (-not (Test-Path -LiteralPath $PuertsPath)) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
    throw "Refusing to package: pinned PuerTS bundle not found at $PuertsPath. MCPBridge.uplugin requires it and MCPBridgePuerTS links JsEnv, so a zip without it cannot load. Obtain the bundle (docs/PUERTS.md) or pass -PuertsPath."
}
$puertsRoot = (Resolve-Path -LiteralPath $PuertsPath).Path
& node (Join-Path $repoRoot "Scripts/check-puerts-pin.mjs") --strict --bundle $puertsRoot
if ($LASTEXITCODE -ne 0) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
    throw "Refusing to package: $puertsRoot does not match Plugins/Puerts.lock.json."
}

# Gate 2: the plugin's own generated PuerTS support files, checked against the
# lock file rather than against whatever happens to be on this disk. Without
# them the PuerTS lane has nothing to execute.
$stagePlugin = Join-Path $stageRoot "MCPBridge"
Copy-PluginClean -Source $pluginRoot -Destination $stagePlugin
Assert-PluginClean -Root $stagePlugin
& node (Join-Path $repoRoot "Scripts/stage-puerts-runtime.mjs") --check --plugin $stagePlugin
if ($LASTEXITCODE -ne 0) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
    throw "Refusing to package: MCPBridge/Content/JavaScript/puerts does not match Plugins/Puerts.lock.json. Run npm run build with the pinned bundle present."
}

$stagePuerts = Join-Path $stageRoot "Puerts"
Copy-PluginClean -Source $puertsRoot -Destination $stagePuerts
Assert-PluginClean -Root $stagePuerts

$lockSource = Join-Path $repoRoot "Plugins/Puerts.lock.json"
Copy-Item -LiteralPath $lockSource -Destination (Join-Path $stageRoot "Puerts.lock.json") -Force

$lock = Get-Content -LiteralPath $lockSource -Raw | ConvertFrom-Json
$readme = @"
MCP Bridge $versionName for Unreal Engine $engineVersion
=========================================================

This archive contains TWO plugins. Copy both into your project's Plugins
directory, side by side:

    <YourProject>/Plugins/MCPBridge/
    <YourProject>/Plugins/Puerts/

MCPBridge.uplugin declares a dependency on Puerts, and the MCPBridgePuerTS
module links the JsEnv module that lives in it. The editor refuses to load
MCPBridge if Puerts is missing, and UBT cannot compile it either. That is why
the bundle ships here instead of being a separate download.

Puerts is the upstream Tencent/puerts bundle, subtree $($lock.upstream.subtree),
tag $($lock.upstream.tag), commit $($lock.upstream.commit).
One deliberate local change: $($lock.local_modifications -join '; ').
Puerts.lock.json in this archive is the checksum manifest for all
$($lock.file_count) files. Verify an install with:

    node Scripts/check-puerts-pin.mjs --strict --bundle <YourProject>/Plugins/Puerts

Source only: no binaries are included. The target must be a C++ project with
UE4.27 and UnrealBuildTool available. Build the editor target before opening
the project:

    <UE4.27>\Engine\Build\BatchFiles\Build.bat <YourProject>Editor Win64 ``
        Development "<YourProject>\<YourProject>.uproject" -WaitMutex -NoHotReload

-NoHotReload is not optional. Without it UBT gives PuerTS's
ParamDefaultValueMetas module a hot-reload-suffixed name, and JsEnv then fails
to link against it:

    LINK : fatal error LNK1181: cannot open input file
    '...\ParamDefaultValueMetas\UE4Editor-ParamDefaultValueMetas.lib'

The bridge runs without project configuration. When PipeName is absent, it
derives a stable project-specific pipe from the .uproject name and canonical
project path, so two zip installs do not contend for one shared default. To use
an explicit override, add to Config\DefaultEngine.ini:

    [MCPPuerTSBridge]
    PipeName=\\.\pipe\UE427PuerTSMCP_<YourProjectName>

MCPBridge\Config\DefaultEngine.ini.example in this archive has the long form.
The MCP server is NOT in this archive. It is Node and lives in the bridge
repository under mcp-server/. Point your MCP client at that checkout; see
docs/MCP_BRIDGE_INSTALLER.md.
"@
Set-Content -LiteralPath (Join-Path $stageRoot "README_INSTALL.txt") -Value $readme -Encoding UTF8

$zipPath = Join-Path $OutputRoot "MCPBridge_UE$engineVersion`_v$versionName.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
# Entries are written one at a time with forward slashes. Both Compress-Archive
# and ZipFile.CreateFromDirectory on .NET Framework write backslash separators,
# which are outside the zip spec and become literal filenames on any reader that
# follows it.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stageFull = (Resolve-Path -LiteralPath $stageRoot).Path.TrimEnd("\", "/")
$archive = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in @(Get-ChildItem -LiteralPath $stageRoot -Recurse -File -Force)) {
        $entryName = $file.FullName.Substring($stageFull.Length).TrimStart("\", "/").Replace("\", "/")
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file.FullName, $entryName, [System.IO.Compression.CompressionLevel]::Optimal)
    }
}
finally {
    $archive.Dispose()
}

Write-Host "Source plugin zip: $zipPath"

if ($RunUAT -and -not $SourceZipOnly) {
    $runUATPath = (Resolve-Path -LiteralPath $RunUAT).Path
    $uatPackagePath = Join-Path $OutputRoot "MCPBridge_UAT_UE$engineVersion`_v$versionName"
    if (Test-Path -LiteralPath $uatPackagePath) {
        Remove-Item -LiteralPath $uatPackagePath -Recurse -Force
    }

    # RunUAT.bat pushd's into Engine\Binaries\DotNET and then runs
    # AutomationTool.exe by bare name. With NoDefaultCurrentDirectoryInExePath
    # set (Git Bash and several CI harnesses set it) cmd refuses to look in the
    # current directory and the run dies with 9009 "not recognized", which reads
    # like a missing engine rather than an environment variable.
    if ($env:NoDefaultCurrentDirectoryInExePath) {
        Write-Host "Clearing NoDefaultCurrentDirectoryInExePath for the UAT run."
        Remove-Item Env:\NoDefaultCurrentDirectoryInExePath
    }

    & $runUATPath BuildPlugin -Plugin="$upluginPath" -Package="$uatPackagePath" -Rocket
    if ($LASTEXITCODE -ne 0) {
        # Expected, and not a defect in this plugin. BuildPlugin writes its own
        # host project enabling exactly one plugin and copies only that plugin
        # into it (BuildPluginCommand.Automation.cs CreateHostProject), and the
        # descriptor it writes has no AdditionalPluginDirectories. MCPBridge
        # requires Puerts, so UBT has nowhere to resolve it from and stops with
        # "Unable to find plugin 'Puerts'" (UEBuildTarget.cs). It also wipes the
        # package directory first, so the dependency cannot be staged in
        # beforehand. There is no argument that fixes this in 4.27.
        throw ("RunUAT BuildPlugin failed with exit code $LASTEXITCODE. If the log says " +
            "`"Unable to find plugin 'Puerts'`", that is the known 4.27 limitation, not a " +
            "regression: BuildPlugin builds one plugin in a host project it generates itself " +
            "and cannot be given a plugin dependency. Build in a real host project that has " +
            "both plugins instead. See docs/RELEASE.md.")
    }

    Write-Host "UAT package: $uatPackagePath"
}

Remove-Item -LiteralPath $stageRoot -Recurse -Force
