# D3D11On12

Microsoft's Direct3D 11 user mode driver over D3D12, built into d3d11on12.dll.

- Upstream: https://github.com/microsoft/D3D11On12
- Commit: 2a71203bdca4119e7de12f214c8f6678d150cc28 (2026-03-09)
- License: MIT, see LICENSE

The commit pairs with the D3D12TranslationLayer commit in ../D3D12TranslationLayer.
src/main.cpp is built as objects of d3d11on12.dll so DllMain is always linked; the
rest of src/ is the d3d11on12core static library. The upstream CMakeLists.txt files and
src/d3d11on12.def are unused; dll/d3d11on12 has the export list.
.github and README.md are not imported.

## Patches

None.
