<#
.SYNOPSIS
    Take ReactX back out of a game folder and restore what was there before.

.EXAMPLE
    .\Uninstall-ReactX.ps1 "C:\Games\SomeGame\bin\game.exe"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string] $Exe,

    # Also delete reactx.log
    [switch] $RemoveLog
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'ReactXCommon.ps1')

$GameDir = Split-Path -Parent (Resolve-Path $Exe).Path
$Record = Join-Path $GameDir $ReactXRecordName
if (-not (Test-Path $Record)) {
    throw "ReactX is not installed next to $Exe"
}

foreach ($Line in Get-Content $Record) {
    if ($Line -notlike 'file=*') { continue }

    $Path = Join-Path $GameDir $Line.Substring(5)
    if (Test-Path $Path) { Remove-Item $Path -Force }

    # Put the game's own copy back
    $Backup = "$Path.reactx-backup"
    if (Test-Path $Backup) {
        Rename-Item $Backup (Split-Path -Leaf $Path)
        Write-Output "restored the game's own $(Split-Path -Leaf $Path)"
    }
}

if ($RemoveLog -and (Test-Path (Join-Path $GameDir 'reactx.log'))) {
    Remove-Item (Join-Path $GameDir 'reactx.log') -Force
}

Remove-Item $Record -Force
Write-Output "ReactX removed from $GameDir"
