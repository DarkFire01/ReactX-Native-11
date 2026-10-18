<#
.SYNOPSIS
    Show which d3d11.dll and dxgi.dll a running program actually loaded.

.DESCRIPTION
    Run it while the game is up. A path inside the game folder means ReactX; a System32
    path means the program found the Windows runtime instead.

.EXAMPLE
    .\Check-ReactX.ps1 game
    .\Check-ReactX.ps1 -Id 12345
#>
[CmdletBinding(DefaultParameterSetName = 'Name')]
param(
    [Parameter(Mandatory, Position = 0, ParameterSetName = 'Name')]
    [string] $Name,

    [Parameter(Mandatory, ParameterSetName = 'Id')]
    [int] $Id
)

$ErrorActionPreference = 'Stop'

$Processes = if ($PSCmdlet.ParameterSetName -eq 'Id') { Get-Process -Id $Id } else { Get-Process -Name $Name }

foreach ($Process in $Processes) {
    Write-Output "$($Process.ProcessName) ($($Process.Id))"
    try {
        $Modules = $Process.Modules | Where-Object { $_.ModuleName -in 'd3d11.dll', 'dxgi.dll', 'd3d10.dll', 'd3d10_1.dll', 'd3d10core.dll', 'd3d10_1core.dll', 'd3d12.dll', 'd3d12core.dll', 'd3d9.dll' }
    }
    catch {
        Write-Output "  cannot read its modules: $($_.Exception.Message) (run this shell as the same user, or elevated)"
        continue
    }

    if (-not $Modules) {
        Write-Output '  no Direct3D runtime loaded yet'
        continue
    }

    foreach ($Module in $Modules) {
        $Source = if ($Module.FileName -like "$env:SystemRoot\*") { 'Windows' } else { 'ReactX' }
        Write-Output ("  {0,-10} {1,-8} {2}" -f $Module.ModuleName, $Source, $Module.FileName)
    }
}
