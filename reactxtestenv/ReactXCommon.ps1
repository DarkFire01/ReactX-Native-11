# Shared by the ReactX test environment scripts

$ReactXFiles = @('d3d11.dll', 'd3d11.pdb', 'dxgi.dll', 'dxgi.pdb', 'd3d10.dll', 'd3d10.pdb', 'd3d10_1.dll', 'd3d10_1.pdb', 'd3d10core.dll', 'd3d10core.pdb', 'd3d10_1core.dll', 'd3d10_1core.pdb', 'd3d12.dll', 'd3d12.pdb', 'd3d12core.dll', 'd3d12core.pdb')
$ReactXRecordName = 'reactx-installed.txt'

# x64 or x86, from the machine field of the exe's PE header
function Get-ReactXArch([string] $Path) {
    $Stream = [System.IO.File]::OpenRead($Path)
    try {
        $Reader = [System.IO.BinaryReader]::new($Stream)
        $Stream.Position = 0x3C
        $Stream.Position = $Reader.ReadInt32() + 4
        switch ($Reader.ReadUInt16()) {
            0x8664 { return 'x64' }
            0x014C { return 'x86' }
            0xAA64 { throw "$Path is an ARM64 program; ReactX has no ARM64 build here" }
            default { throw "$Path is not a Windows x64 or x86 program" }
        }
    }
    finally {
        $Stream.Dispose()
    }
}

# True when a file is the same as the ReactX copy of it
function Test-ReactXFile([string] $Path, [string] $RuntimeDir) {
    $Ours = Join-Path $RuntimeDir (Split-Path -Leaf $Path)
    if ((Test-Path $Ours) -and (Get-FileHash $Path).Hash -eq (Get-FileHash $Ours).Hash) { return $true }

    # Older ReactX builds: the DLLs read reactx.ini, the PDBs name the ReactX sources
    $Bytes = [IO.File]::ReadAllBytes($Path)
    return [Text.Encoding]::Unicode.GetString($Bytes).Contains('reactx.ini') -or
           [Text.Encoding]::ASCII.GetString($Bytes).Contains('\ReactX')
}
