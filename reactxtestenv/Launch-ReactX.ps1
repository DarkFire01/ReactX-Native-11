<#
.SYNOPSIS
    Start a program on ReactX with logging turned on for that one run.

.DESCRIPTION
    For programs you start directly, not through Steam or a launcher that starts another
    exe. Sets REACTX_LOG and REACTX_LOG_FILE for the child process only and starts it.
    ReactX must already be installed next to the exe with Install-ReactX.ps1, because
    Windows only picks the DLLs up from the exe's folder. For games started from Steam,
    use Install-ReactX.ps1 -Log instead, which turns logging on through reactx.ini.

.EXAMPLE
    .\Launch-ReactX.ps1 "C:\Games\SomeGame\bin\game.exe"
    .\Launch-ReactX.ps1 "C:\Games\SomeGame\bin\game.exe" -Arguments '-windowed'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string] $Exe,

    [string] $Arguments = '',

    # Where the log goes; defaults to reactx.log next to the game
    [string] $LogFile
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'ReactXCommon.ps1')

$Exe = (Resolve-Path $Exe).Path
$GameDir = Split-Path -Parent $Exe
if (-not (Test-Path (Join-Path $GameDir $ReactXRecordName))) {
    throw "ReactX is not installed next to $Exe; run Install-ReactX.ps1 first"
}
if (-not $LogFile) { $LogFile = Join-Path $GameDir 'reactx.log' }

$env:REACTX_LOG = '1'
$env:REACTX_LOG_FILE = $LogFile
try {
    $Process = Start-Process -FilePath $Exe -ArgumentList $Arguments -WorkingDirectory $GameDir -PassThru
}
finally {
    Remove-Item Env:REACTX_LOG, Env:REACTX_LOG_FILE -ErrorAction SilentlyContinue
}

Write-Output "started $($Process.ProcessName) ($($Process.Id)); log: $LogFile"
