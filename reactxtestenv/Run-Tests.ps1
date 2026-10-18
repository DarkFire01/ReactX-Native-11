<#
.SYNOPSIS
    Run the ReactX test suites, each comparing ReactX with the Windows runtime.

.DESCRIPTION
    tinyd3d renders scenes on both runtimes and compares the pixels; apicheck and dxgicheck
    print facts about API behavior on both and list every difference. NativeVideoDecode
    decodes a clip on both and compares checksums. Needs Python for the comparisons.

.EXAMPLE
    .\Run-Tests.ps1
    .\Run-Tests.ps1 -Arch x86 -Suite dxgicheck
    .\Run-Tests.ps1 -Video C:\DevEnv\PRs\SampleVideos\sample-10s-360p.mp4
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86')]
    [string] $Arch = 'x64',

    [ValidateSet('all', 'tinyd3d', 'apicheck', 'dxgicheck', 'video')]
    [string[]] $Suite = 'all',

    # A clip for the decode comparison; skipped when not given
    [string] $Video
)

$ErrorActionPreference = 'Stop'

$Tools = Join-Path $PSScriptRoot "tools\$Arch"
$Scripts = Join-Path $PSScriptRoot 'tools\scripts'
$Captures = Join-Path $PSScriptRoot "results\$Arch"
$Failed = 0

function Test-Wanted([string] $Name) { return ($Suite -contains 'all') -or ($Suite -contains $Name) }

if (-not (Test-Path (Join-Path $Tools 'reactx\d3d11.dll'))) { throw "no $Arch tools; run Refresh.ps1 first" }
New-Item -ItemType Directory -Force -Path $Captures | Out-Null

if (Test-Wanted 'tinyd3d') {
    Write-Output '== tinyd3d: rendered pixels, Windows against ReactX'
    python (Join-Path $Scripts 'compare_tinyd3d.py') $Tools $Captures
    if ($LASTEXITCODE -ne 0) { $Failed++ }
}

foreach ($Tool in 'apicheck', 'dxgicheck') {
    if (-not (Test-Wanted $Tool)) { continue }
    Write-Output "== ${Tool}: API behavior, Windows against ReactX"
    python (Join-Path $Scripts 'compare_checks.py') $Tools --tool $Tool
    if ($LASTEXITCODE -ne 0) { $Failed++ }
}

if ((Test-Wanted 'video') -and $Video) {
    Write-Output '== NativeVideoDecode: decoded frame checksums'
    $Sums = foreach ($Runtime in 'system', 'reactx') {
        $Line = & (Join-Path $Tools 'NativeVideoDecode.exe') $Video -headless -fast -crc -runtime $Runtime 2>&1 |
            Select-String 'stream checksum'
        Write-Host "   $Runtime $Line"
        "$Line"
    }
    if ($Sums[0] -ne $Sums[1]) { Write-Output '   checksums differ'; $Failed++ }
}

if ($Failed) { Write-Output "$Failed suite(s) reported differences"; exit 1 }
Write-Output 'all suites match Windows'
