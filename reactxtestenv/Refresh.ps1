<#
.SYNOPSIS
    Fill this folder from the ReactX build output: the runtime DLLs and every test tool.

.DESCRIPTION
    runtime\x64 and runtime\x86 get d3d11.dll and dxgi.dll. tools\x64 and tools\x86 get
    the test programs with the runtime in a reactx subfolder, which is where their
    -runtime reactx switch looks. Run it again after rebuilding.

.EXAMPLE
    .\Refresh.ps1
    .\Refresh.ps1 -Config Debug -Build
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Config = 'Release',

    [string] $Target = 'win11',

    # Build both architectures first
    [switch] $Build
)

$ErrorActionPreference = 'Stop'

$Here = $PSScriptRoot
$Root = Split-Path -Parent $Here
$Runtime = @('d3d11.dll', 'd3d11.pdb', 'dxgi.dll', 'dxgi.pdb', 'd3d10.dll', 'd3d10.pdb', 'd3d10_1.dll', 'd3d10_1.pdb', 'd3d10core.dll', 'd3d10core.pdb', 'd3d10_1core.dll', 'd3d10_1core.pdb', 'd3d12.dll', 'd3d12.pdb', 'd3d12core.dll', 'd3d12core.pdb')
$Tools = @('tinyd3d', 'apicheck', 'dxgicheck', 'd3d10check', 'd3d12check', 'rxprobe', 'NativeVideoDecode', 'maketestclips')

foreach ($Arch in 'x64', 'x86') {
    if ($Build) {
        & (Join-Path $Root 'scripts\build.ps1') -Target $Target -Arch $Arch -Config $Config
        if ($LASTEXITCODE -ne 0) { throw "build failed for $Arch" }
    }

    $Bin = Join-Path $Root "build\$Target-$Arch-$($Config.ToLower())\bin"
    if (-not (Test-Path (Join-Path $Bin 'reactx\d3d11.dll'))) {
        Write-Warning "no $Config build for $Arch at $Bin; run with -Build"
        continue
    }

    $RuntimeDir = Join-Path $Here "runtime\$Arch"
    $ToolsDir = Join-Path $Here "tools\$Arch"
    New-Item -ItemType Directory -Force -Path $RuntimeDir, (Join-Path $ToolsDir 'reactx') | Out-Null

    # Direct3D 12 only exists for the Windows 10 and later targets
    foreach ($File in $Runtime) {
        $Source = Join-Path $Bin "reactx\$File"
        if (-not (Test-Path $Source)) { continue }
        Copy-Item $Source $RuntimeDir -Force
        Copy-Item $Source (Join-Path $ToolsDir 'reactx') -Force
    }

    foreach ($Tool in $Tools) {
        foreach ($Extension in '.exe', '.pdb') {
            $Source = Join-Path $Bin "$Tool$Extension"
            if (Test-Path $Source) { Copy-Item $Source $ToolsDir -Force }
        }
    }

    Write-Output "$Arch : $Config build from $Bin"
}

# The comparison scripts drive the tools against System32 and ReactX
$Scripts = Join-Path $Here 'tools\scripts'
New-Item -ItemType Directory -Force -Path $Scripts | Out-Null
Copy-Item (Join-Path $Root 'tests\tinyd3d\compare.py') (Join-Path $Scripts 'compare_tinyd3d.py') -Force
Copy-Item (Join-Path $Root 'tests\apicheck\compare.py') (Join-Path $Scripts 'compare_checks.py') -Force

Set-Content -Path (Join-Path $Here 'BUILD.txt') -Encoding utf8 -Value @(
    "Refreshed $(Get-Date -Format 'yyyy-MM-dd HH:mm') from $Target $Config builds",
    "Source: $Root")
