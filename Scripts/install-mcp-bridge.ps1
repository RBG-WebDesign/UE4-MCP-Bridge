#requires -Version 5.1

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Project,

    [string]$BridgeRoot,

    [switch]$SkipBuild,

    [switch]$SkipPython,

    [switch]$EnableLegacyHttp,

    [switch]$SkipCppPlugin,

    [switch]$SkipPanelPlugin,

    [switch]$SkipMcpConfig,

    [switch]$IncludeUnrealApi,

    [string]$UnrealVersion = "4.27",

    [switch]$CleanManaged
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-ExistingPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    try {
        return (Resolve-Path -LiteralPath $Path).Path
    }
    catch {
        throw "$Label does not exist: $Path"
    }
}

function Get-RepoRoot {
    if ($BridgeRoot) {
        return Resolve-ExistingPath -Path $BridgeRoot -Label "Bridge root"
    }

    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
}

function Get-ProjectRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath
    )

    $resolved = Resolve-ExistingPath -Path $ProjectPath -Label "Project path"
    $item = Get-Item -LiteralPath $resolved

    if (-not $item.PSIsContainer) {
        if ($item.Extension -ne ".uproject") {
            throw "Project file must be a .uproject file: $resolved"
        }

        return [PSCustomObject]@{
            Root = $item.Directory.FullName
            UProject = $item.FullName
        }
    }

    $uprojectFiles = @(Get-ChildItem -LiteralPath $item.FullName -Filter "*.uproject" -File)
    if ($uprojectFiles.Count -gt 1) {
        $names = ($uprojectFiles | ForEach-Object { $_.Name }) -join ", "
        throw "Project directory contains multiple .uproject files. Pass one explicitly: $names"
    }

    return [PSCustomObject]@{
        Root = $item.FullName
        UProject = if ($uprojectFiles.Count -eq 1) { $uprojectFiles[0].FullName } else { $null }
    }
}

function ConvertTo-JsonPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return $Path.Replace("\", "/")
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    [System.IO.File]::WriteAllText($Path, $Content, (New-Object System.Text.UTF8Encoding($false)))
}

function Get-ProjectPipeName {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [string]$UProjectPath
    )

    $name = if ([string]::IsNullOrWhiteSpace($UProjectPath)) {
        Split-Path -Leaf $ProjectRoot
    }
    else {
        [System.IO.Path]::GetFileNameWithoutExtension($UProjectPath)
    }
    $slug = [regex]::Replace($name, "[^A-Za-z0-9_]", "_").Trim("_")
    if ([string]::IsNullOrWhiteSpace($slug)) { $slug = "Project" }
    if ($slug.Length -gt 32) { $slug = $slug.Substring(0, 32) }

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($ProjectRoot.ToLowerInvariant())
        $hashBytes = $sha.ComputeHash($bytes)
    }
    finally {
        $sha.Dispose()
    }
    $hash = (($hashBytes[0..3] | ForEach-Object { $_.ToString("x2") }) -join "")
    return "\\.\pipe\UE427PuerTSMCP_${slug}_${hash}"
}

function Set-ManagedIniSection {
    param(
        [string[]]$Lines,
        [Parameter(Mandatory = $true)]
        [string]$SectionName,
        [string[]]$ManagedPatterns,
        [string[]]$RequiredLines
    )

    $sectionStart = -1
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i].Trim() -eq $SectionName) {
            $sectionStart = $i
            break
        }
    }

    if ($sectionStart -lt 0) {
        $result = @($Lines)
        if ($result.Count -gt 0 -and $result[-1].Trim() -ne "") { $result += "" }
        $result += $SectionName
        $result += $RequiredLines
        return $result
    }

    $sectionEnd = $Lines.Count
    for ($i = $sectionStart + 1; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -match "^\s*\[.+\]\s*$") {
            $sectionEnd = $i
            break
        }
    }

    $result = @()
    if ($sectionStart -gt 0) { $result += $Lines[0..($sectionStart - 1)] }
    $result += $SectionName
    $result += $RequiredLines
    for ($i = $sectionStart + 1; $i -lt $sectionEnd; $i++) {
        $managed = $false
        foreach ($pattern in $ManagedPatterns) {
            if ($Lines[$i] -match $pattern) {
                $managed = $true
                break
            }
        }
        if (-not $managed) { $result += $Lines[$i] }
    }
    if ($sectionEnd -lt $Lines.Count) { $result += $Lines[$sectionEnd..($Lines.Count - 1)] }
    return $result
}
function Assert-BridgeLayout {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $requiredPaths = @(
        "package.json",
        "mcp-server/package.json",
        "Plugins/MCPBridge/MCPBridge.uplugin",
        "Plugins/MCPBridge/Content/Python/startup.py",
        "Plugins/MCPBridge/Content/Python/mcp_bridge",
        "Plugins/MCPBridge/Source/MCPBridgeEditorPanel/MCPBridgeEditorPanel.Build.cs",
        "Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/MCPBridgeGraphBuilder.Build.cs"
    )

    foreach ($relativePath in $requiredPaths) {
        $candidate = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $candidate)) {
            throw "Bridge root is missing required path: $candidate"
        }
    }
}

function Invoke-BridgeBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    if ($SkipBuild) {
        Write-Host "Skipping MCP server build."
        return
    }

    if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
        throw "npm was not found on PATH. Install Node.js 18 or newer, or rerun with -SkipBuild after building manually."
    }

    Push-Location -LiteralPath $Root
    try {
        if (-not (Test-Path -LiteralPath (Join-Path $Root "node_modules"))) {
            if ($PSCmdlet.ShouldProcess($Root, "npm install")) {
                & npm install
            }
        }

        if ($PSCmdlet.ShouldProcess($Root, "npm run build")) {
            & npm run build
        }
    }
    finally {
        Pop-Location
    }
}

function Copy-ManagedDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination,

        [Parameter(Mandatory = $true)]
        [string]$Name,

        [string[]]$ExcludeDirectoryNames = @()
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Source directory does not exist: $Source"
    }

    if ($CleanManaged -and (Test-Path -LiteralPath $Destination)) {
        if ($PSCmdlet.ShouldProcess($Destination, "remove existing managed $Name")) {
            Remove-Item -LiteralPath $Destination -Recurse -Force
        }
    }

    if (-not (Test-Path -LiteralPath $Destination)) {
        if ($PSCmdlet.ShouldProcess($Destination, "create $Name directory")) {
            New-Item -ItemType Directory -Path $Destination | Out-Null
        }
    }

    if ($PSCmdlet.ShouldProcess($Destination, "copy $Name files")) {
        $sourceRoot = (Resolve-Path -LiteralPath $Source).Path.TrimEnd("\", "/")
        $items = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force)

        foreach ($item in $items) {
            $relativePath = $item.FullName.Substring($sourceRoot.Length).TrimStart("\", "/")
            if ([string]::IsNullOrWhiteSpace($relativePath)) {
                continue
            }

            $pathParts = @($relativePath -split "[\\/]")
            $isExcluded = $false
            foreach ($part in $pathParts) {
                if ($ExcludeDirectoryNames -contains $part) {
                    $isExcluded = $true
                    break
                }
            }

            if ($isExcluded) {
                continue
            }

            $targetPath = Join-Path $Destination $relativePath
            if ($item.PSIsContainer) {
                if (-not (Test-Path -LiteralPath $targetPath)) {
                    New-Item -ItemType Directory -Path $targetPath | Out-Null
                }
            }
            else {
                $targetParent = Split-Path -Parent $targetPath
                if (-not (Test-Path -LiteralPath $targetParent)) {
                    New-Item -ItemType Directory -Path $targetParent | Out-Null
                }

                Copy-Item -LiteralPath $item.FullName -Destination $targetPath -Force
            }
        }
    }
}

function Copy-ManagedFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent)) {
        if ($PSCmdlet.ShouldProcess($parent, "create directory for $Name")) {
            New-Item -ItemType Directory -Path $parent | Out-Null
        }
    }

    if ($CleanManaged -and (Test-Path -LiteralPath $Destination)) {
        if ($PSCmdlet.ShouldProcess($Destination, "remove existing managed $Name")) {
            Remove-Item -LiteralPath $Destination -Force
        }
    }

    if ($PSCmdlet.ShouldProcess($Destination, "copy $Name")) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
    }
}

function Install-MCPBridgePlugin {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )

    $source = Join-Path $Root "Plugins/MCPBridge"
    $destination = Join-Path $ProjectRoot "Plugins/MCPBridge"
    Copy-ManagedDirectory -Source $source -Destination $destination -Name "MCPBridge plugin" -ExcludeDirectoryNames @("Binaries", "Intermediate", "Saved")

    $puertsSource = Join-Path $Root "Plugins/Puerts"
    if (-not (Test-Path -LiteralPath (Join-Path $puertsSource "Puerts.uplugin"))) {
        throw "PuerTS Unreal_v1.0.9 is not installed at $puertsSource. See docs/PUERTS.md."
    }
    $pinCheck = Join-Path $Root "Scripts/check-puerts-pin.mjs"
    if (Test-Path -LiteralPath $pinCheck) {
        & node $pinCheck --strict
        if ($LASTEXITCODE -ne 0) {
            throw "PuerTS bundle at $puertsSource does not match Plugins/Puerts.lock.json (pinned Unreal_v1.0.9, commit 838ab762d830). Refusing to install an unverified bundle. See docs/PUERTS.md."
        }
    }
    $puertsDestination = Join-Path $ProjectRoot "Plugins/Puerts"
    Copy-ManagedDirectory -Source $puertsSource -Destination $puertsDestination -Name "PuerTS plugin" -ExcludeDirectoryNames @("Binaries", "Intermediate", "Saved")
}

function Update-DefaultEngineIni {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [Parameter(Mandatory = $true)]
        [string]$PipeName
    )

    $configDir = Join-Path $ProjectRoot "Config"
    $iniPath = Join-Path $configDir "DefaultEngine.ini"
    if (-not (Test-Path -LiteralPath $configDir)) {
        if ($PSCmdlet.ShouldProcess($configDir, "create Config directory")) {
            New-Item -ItemType Directory -Path $configDir | Out-Null
        }
    }

    $lines = if (Test-Path -LiteralPath $iniPath) { @(Get-Content -LiteralPath $iniPath) } else { @() }
    $bridgePatterns = @("^\s*PipeName\s*=")
    $lines = @(Set-ManagedIniSection -Lines $lines -SectionName "[MCPPuerTSBridge]" -ManagedPatterns $bridgePatterns -RequiredLines @("PipeName=$PipeName"))

    if (-not $SkipPython) {
        $pythonPath = ConvertTo-JsonPath -Path (Join-Path $ProjectRoot "Plugins/MCPBridge/Content/Python")
        $pythonLines = @("bRemoteExecution=False")
        if ($EnableLegacyHttp) {
            $pythonLines += "bDeveloperMode=True"
            $pythonLines += "+StartupScripts=startup.py"
            $pythonLines += "+AdditionalPaths=(Path=`"$pythonPath`")"
        }
        $pythonPatterns = @(
            "^\s*bDeveloperMode\s*=",
            "^\s*bRemoteExecution\s*=",
            "^\s*\+?StartupScripts\s*=\s*/Game/Python/startup\.py\s*$",
            "^\s*\+?StartupScripts\s*=\s*/MCPBridge/Python/startup\.py\s*$",
            "^\s*\+?StartupScripts\s*=\s*startup\.py\s*$",
            '^\s*\+?AdditionalPaths\s*=\s*\(Path=".*?/Plugins/MCPBridge/Content/Python"\)\s*$'
        )
        $lines = @(Set-ManagedIniSection -Lines $lines -SectionName "[/Script/PythonScriptPlugin.PythonScriptPluginSettings]" -ManagedPatterns $pythonPatterns -RequiredLines $pythonLines)
    }

    if ($PSCmdlet.ShouldProcess($iniPath, "configure native MCP pipe and legacy Python policy")) {
        Write-Utf8NoBom -Path $iniPath -Content (($lines -join "`r`n") + "`r`n")
    }
}
function Update-McpConfig {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [Parameter(Mandatory = $true)]
        [string]$PipeName
    )

    if ($SkipMcpConfig) {
        Write-Host "Skipping MCP client config updates."
        return
    }

    $serverEnv = [PSCustomObject]@{
        MCP_UNREAL_PROJECT_ROOT = ConvertTo-JsonPath -Path $ProjectRoot
        MCP_PUERTS_PIPE = $PipeName
    }
    if ($EnableLegacyHttp) {
        $serverEnv | Add-Member -MemberType NoteProperty -Name MCP_ENABLE_LEGACY_HTTP -Value "1"
    }
    $serverEntry = [PSCustomObject]@{
        command = "node"
        args = @((ConvertTo-JsonPath -Path (Join-Path $Root "mcp-server/dist/index.js")))
        cwd = ConvertTo-JsonPath -Path $Root
        env = $serverEnv
    }

    $mcpPath = Join-Path $ProjectRoot ".mcp.json"
    if (Test-Path -LiteralPath $mcpPath) {
        try {
            $config = Get-Content -LiteralPath $mcpPath -Raw | ConvertFrom-Json
        }
        catch {
            $backupPath = "$mcpPath.bak-$(Get-Date -Format yyyyMMddHHmmss)"
            if ($PSCmdlet.ShouldProcess($mcpPath, "backup invalid MCP config to $backupPath")) {
                Copy-Item -LiteralPath $mcpPath -Destination $backupPath -Force
            }
            $config = [PSCustomObject]@{}
        }
    }
    else {
        $config = [PSCustomObject]@{}
    }

    $propertyNames = @($config.PSObject.Properties | ForEach-Object { $_.Name })
    if (-not ($propertyNames -contains "mcpServers")) {
        $config | Add-Member -MemberType NoteProperty -Name "mcpServers" -Value ([PSCustomObject]@{})
    }
    $config.mcpServers | Add-Member -MemberType NoteProperty -Name "unreal-bridge" -Value $serverEntry -Force

    if ($IncludeUnrealApi) {
        $unrealApiEntry = [PSCustomObject]@{
            command = "uvx"
            args = @("unreal-api-mcp")
            env = [PSCustomObject]@{ UNREAL_VERSION = $UnrealVersion }
        }
        $config.mcpServers | Add-Member -MemberType NoteProperty -Name "unreal-api" -Value $unrealApiEntry -Force
    }

    if ($PSCmdlet.ShouldProcess($mcpPath, "write MCP config")) {
        Write-Utf8NoBom -Path $mcpPath -Content (($config | ConvertTo-Json -Depth 12) + "`r`n")
    }

    $codexDir = Join-Path $ProjectRoot ".codex"
    $codexPath = Join-Path $codexDir "config.toml"
    $legacyLine = if ($EnableLegacyHttp) { "MCP_ENABLE_LEGACY_HTTP = '1'`r`n" } else { "" }
    $engineLine = if ([string]::IsNullOrWhiteSpace($env:UE_ENGINE_ROOT)) { "" } else {
        "UE_ENGINE_ROOT = '$(ConvertTo-JsonPath -Path $env:UE_ENGINE_ROOT)'`r`n"
    }
    $managedBlock = @"
# BEGIN MCPBRIDGE MANAGED
[mcp_servers.unreal-bridge]
command = "node"
args = ["$(ConvertTo-JsonPath -Path (Join-Path $Root "mcp-server/dist/index.js"))"]
cwd = "$(ConvertTo-JsonPath -Path $Root)"

[mcp_servers.unreal-bridge.env]
MCP_UNREAL_PROJECT_ROOT = "$(ConvertTo-JsonPath -Path $ProjectRoot)"
MCP_PUERTS_PIPE = '$PipeName'
$engineLine$legacyLine# END MCPBRIDGE MANAGED
"@

    if (-not (Test-Path -LiteralPath $codexDir)) {
        if ($PSCmdlet.ShouldProcess($codexDir, "create project-local Codex config directory")) {
            New-Item -ItemType Directory -Path $codexDir | Out-Null
        }
    }
    $codexText = if (Test-Path -LiteralPath $codexPath) { [System.IO.File]::ReadAllText($codexPath) } else { "" }
    if ($codexText -match '(?s)# BEGIN MCPBRIDGE MANAGED.*?# END MCPBRIDGE MANAGED') {
        $codexText = [regex]::Replace($codexText, '(?s)# BEGIN MCPBRIDGE MANAGED.*?# END MCPBRIDGE MANAGED', $managedBlock.Trim())
    }
    elseif ($codexText -match '(?m)^\[mcp_servers\.unreal-bridge\]') {
        Write-Warning "Codex config already defines mcp_servers.unreal-bridge without MCPBridge markers. Left it unchanged: $codexPath"
        return
    }
    else {
        if (-not [string]::IsNullOrWhiteSpace($codexText)) { $codexText = $codexText.TrimEnd() + "`r`n`r`n" }
        $codexText += $managedBlock.Trim() + "`r`n"
    }
    if ($PSCmdlet.ShouldProcess($codexPath, "write project-local Codex MCP config")) {
        Write-Utf8NoBom -Path $codexPath -Content $codexText
    }
}
function Update-UProjectPlugins {
    param(
        [string]$UProjectPath
    )

    if ([string]::IsNullOrWhiteSpace($UProjectPath)) {
        Write-Host "Skipping .uproject plugin update because no .uproject file was found."
        return
    }

    $pluginsToEnable = @("Puerts", "MCPBridge")
    if ($EnableLegacyHttp) { $pluginsToEnable += @("PythonScriptPlugin", "EditorScriptingUtilities") }

    if ($pluginsToEnable.Count -eq 0) {
        return
    }

    try {
        $projectJson = Get-Content -LiteralPath $UProjectPath -Raw | ConvertFrom-Json
    }
    catch {
        throw "Unable to parse .uproject JSON: $UProjectPath"
    }

    $propertyNames = @($projectJson.PSObject.Properties | ForEach-Object { $_.Name })
    if ($propertyNames -contains "Plugins") {
        $plugins = @($projectJson.Plugins)
    }
    else {
        $plugins = @()
    }

    foreach ($pluginName in $pluginsToEnable) {
        $existing = $plugins | Where-Object { $_.Name -eq $pluginName } | Select-Object -First 1
        if ($existing) {
            $existingPropertyNames = @($existing.PSObject.Properties | ForEach-Object { $_.Name })
            if ($existingPropertyNames -contains "Enabled") {
                $existing.Enabled = $true
            }
            else {
                $existing | Add-Member -MemberType NoteProperty -Name "Enabled" -Value $true
            }
        }
        else {
            $plugins += [PSCustomObject]@{
                Name = $pluginName
                Enabled = $true
            }
        }
    }

    if ($propertyNames -contains "Plugins") {
        $projectJson.Plugins = $plugins
    }
    else {
        $projectJson | Add-Member -MemberType NoteProperty -Name "Plugins" -Value $plugins
    }

    $json = $projectJson | ConvertTo-Json -Depth 12
    if ($PSCmdlet.ShouldProcess($UProjectPath, "enable MCP Bridge project plugins")) {
        Write-Utf8NoBom -Path $UProjectPath -Content ($json + "`r`n")
    }
}

$repoRoot = Get-RepoRoot
Assert-BridgeLayout -Root $repoRoot

$projectInfo = Get-ProjectRoot -ProjectPath $Project
$projectRoot = $projectInfo.Root
$pipeName = Get-ProjectPipeName -ProjectRoot $projectRoot -UProjectPath $projectInfo.UProject

Write-Host "MCP Bridge root: $repoRoot"
Write-Host "Target project:  $projectRoot"
if ($projectInfo.UProject) {
    Write-Host "UProject file:   $($projectInfo.UProject)"
}
else {
    Write-Host "UProject file:   not found in target directory"
}

Invoke-BridgeBuild -Root $repoRoot
Install-MCPBridgePlugin -Root $repoRoot -ProjectRoot $projectRoot
Update-DefaultEngineIni -ProjectRoot $projectRoot -PipeName $pipeName
Update-McpConfig -Root $repoRoot -ProjectRoot $projectRoot -PipeName $pipeName
Update-UProjectPlugins -UProjectPath $projectInfo.UProject

Write-Host ""
Write-Host "MCP Bridge install complete."
Write-Host "Next steps:"
Write-Host "1. Confirm Plugins/Puerts uses the Node.js backend and do not install Unreal.js."
Write-Host "2. Native pipe: $pipeName"
Write-Host "3. Restart Unreal Editor and Codex so both load the project-local native config."
Write-Host "4. If the MCPBridge plugin was installed or updated, accept the UE4 rebuild prompt."
Write-Host "5. Legacy HTTP/Python remains disabled unless -EnableLegacyHttp was specified."
