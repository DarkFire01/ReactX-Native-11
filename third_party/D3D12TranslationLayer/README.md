# D3D12TranslationLayer

Microsoft's D3D12 translation layer, used by d3d11on12.dll and d3d9on12.dll.

- Upstream: https://github.com/microsoft/D3D12TranslationLayer
- Commit: 90ef2fdbd556768d4ee6f7563dec5ca72031e52a (2026-03-16)
- License: MIT, see LICENSE

The commit is the one closest to the Windows 11 28000 d3d11on12.dll: it fetches the
D3D12_OPTIONS13 caps the system layer asks for, and it is the last one before the residency
trim notifications, which query ID3D12Device15 where the system layer does not.

The upstream CMakeLists.txt files are kept but unused; ../CMakeLists.txt builds the
d3d12translationlayer and dxbcparser static libraries against ../DirectX-Headers.
.github and README.md are not imported.

## Patches

None.
