<#
Detects other Claude Code / Codex / Gemini CLI sessions on this machine, so a
native rebuild (install:sync, UBT compile) doesn't race a concurrent agent
editing or building the same repo. Mirrors the refuse-by-default pattern in
start-ue4-project.ps1: report only unless -CloseOthers is passed.

Excludes this script's own process ancestry so it can never kill its own
session, and excludes the Claude desktop app / its crashpad handler, which
match the same name but aren't CLI sessions.
#>
param(
    [switch]$CloseOthers
)

function Get-AgentCliSession {
    $patterns = @(
        '@anthropic-ai[\\/]claude-code'
        'claude-code'
        '(^|[\\/])claude(\.exe|\.cmd|\.ps1)?(\s|$)'
        '@openai[\\/]codex'
        '(^|[\\/])codex(\.exe|\.cmd|\.ps1)?(\s|$)'
        '@google[\\/]gemini-cli'
        '[\\/]gemini-cli[\\/]'
        '(^|[\\/])gemini(\.exe|\.cmd|\.ps1)?(\s|$)'
    )
    $regex = $patterns -join '|'

    Get-CimInstance Win32_Process | Where-Object {
        $processText = "$($_.Name) $($_.ExecutablePath) $($_.CommandLine)"
        $isClaudeDesktop = $processText -match '\\WindowsApps\\Claude_' -or $processText -match '--type=crashpad-handler'
        $processText -match $regex -and -not $isClaudeDesktop
    } | Sort-Object ProcessId | Select-Object ProcessId, ParentProcessId, Name, ExecutablePath, CommandLine
}

function Get-SelfAncestry {
    $ids = @()
    $currentId = $PID
    while ($currentId) {
        $ids += $currentId
        $proc = Get-CimInstance Win32_Process -Filter "ProcessId=$currentId" -ErrorAction SilentlyContinue
        if (-not $proc -or -not $proc.ParentProcessId -or $proc.ParentProcessId -eq $currentId) { break }
        $currentId = $proc.ParentProcessId
    }
    $ids
}

$self = Get-SelfAncestry
$sessions = @(Get-AgentCliSession | Where-Object { $_.ProcessId -notin $self })

if ($sessions.Count -eq 0) {
    Write-Host "No other Claude Code, Codex, or Gemini CLI sessions found."
    exit 0
}

$sessions | Format-Table ProcessId, ParentProcessId, Name, CommandLine -AutoSize

if (-not $CloseOthers) {
    Write-Host "Rerun with -CloseOthers to stop the sessions above before rebuilding."
    exit 1
}

$sessionIds = @($sessions.ProcessId)
$rootSessions = $sessions | Where-Object { $_.ParentProcessId -notin $sessionIds }
foreach ($session in $rootSessions) {
    Write-Host "Stopping $($session.Name) PID $($session.ProcessId)"
    taskkill.exe /PID $session.ProcessId /T /F | Out-Null
}

Write-Host "Remaining sessions:"
Get-AgentCliSession | Where-Object { $_.ProcessId -notin $self }
