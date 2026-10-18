<#
.SYNOPSIS
    Make a game or app load ReactX instead of the system d3d11.dll and dxgi.dll.

.DESCRIPTION
    Copies the ReactX d3d11.dll and dxgi.dll next to the program's exe. Windows loads DLLs
    from the exe's folder before System32 for these two, so the program picks ReactX up
    however it is started (Steam, a launcher, a shortcut). Whatever d3d11.dll or dxgi.dll
    was already there is renamed to *.reactx-backup and put back by Uninstall-ReactX.ps1.

    The architecture comes from the exe header, so a 32 bit game gets the x86 build.

.EXAMPLE
    .\Install-ReactX.ps1 "C:\Games\SomeGame\bin\game.exe"
    .\Install-ReactX.ps1 "C:\Games\SomeGame\bin\game.exe" -Log
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string] $Exe,

    # Turn on logging to reactx.log next to the game
    [switch] $Log,

    # Override the architecture read from the exe
    [ValidateSet('x64', 'x86')]
    [string] $Arch,

    # Also install d3d12.dll and d3d12core.dll; DXGI can not present Direct3D 12 work yet
    [switch] $D3D12
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'ReactXCommon.ps1')

$Exe = (Resolve-Path $Exe).Path
if (-not $Arch) { $Arch = Get-ReactXArch $Exe }

$GameDir = Split-Path -Parent $Exe
$RuntimeDir = Join-Path $PSScriptRoot "runtime\$Arch"
if (-not (Test-Path (Join-Path $RuntimeDir 'd3d11.dll'))) {
    throw "no $Arch runtime in $RuntimeDir; run Refresh.ps1 first"
}

$Record = Join-Path $GameDir $ReactXRecordName
if (Test-Path $Record) {
    Write-Output "ReactX is already installed here; updating the DLLs"
}

$Installed = [System.Collections.Generic.List[string]]::new()
foreach ($File in $ReactXFiles) {
    $Source = Join-Path $RuntimeDir $File
    if (-not (Test-Path $Source)) { continue }
    if (-not $D3D12 -and $File -like 'd3d12*') { continue }

    $Destination = Join-Path $GameDir $File
    $Backup = "$Destination.reactx-backup"

    # Only something that is not ours gets kept, and only once
    if ((Test-Path $Destination) -and -not (Test-Path $Backup) -and -not (Test-ReactXFile $Destination $RuntimeDir)) {
        Rename-Item $Destination $Backup
        Write-Output "kept the game's own $File as $File.reactx-backup"
    }

    Copy-Item $Source $Destination -Force
    $Installed.Add($File)
}

# Settings beside the DLLs; logging off unless asked for
$Settings = Join-Path $GameDir 'reactx.ini'
if ($Log -or -not (Test-Path $Settings)) {
    Set-Content -Path $Settings -Encoding ascii -Value @(
        '[reactx]',
        "log=$(if ($Log) { 1 } else { 0 })",
        'logfile=reactx.log')
    $Installed.Add('reactx.ini')
}

Set-Content -Path $Record -Encoding utf8 -Value (@("arch=$Arch", "exe=$Exe") + ($Installed | ForEach-Object { "file=$_" }))
Write-Output "ReactX ($Arch) installed for $Exe"
if ($Log) { Write-Output "logging to $(Join-Path $GameDir 'reactx.log')" }
