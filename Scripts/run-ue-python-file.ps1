#requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Project,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$Script,

    [string]$UE4EditorCmd = "D:\UE\UE_4.27\Engine\Binaries\Win64\UE4Editor-Cmd.exe",

    [string[]]$ExtraArgs = @()
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

$projectPath = Resolve-ExistingPath -Path $Project -Label "Project"
$scriptPath = Resolve-ExistingPath -Path $Script -Label "Python script"
$editorCmdPath = Resolve-ExistingPath -Path $UE4EditorCmd -Label "UE4Editor-Cmd"

$pythonScriptPath = $scriptPath.Replace("\", "/").Replace('"', '\"')
$pythonExpression = "exec(open(`"$pythonScriptPath`", encoding=`"utf-8`").read())"

& $editorCmdPath $projectPath -run=pythonscript "-script=$pythonExpression" @ExtraArgs
exit $LASTEXITCODE
