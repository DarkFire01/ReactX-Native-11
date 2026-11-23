# DirectXShaderCompiler (dxilconv subset)

Source: https://github.com/microsoft/DirectXShaderCompiler
Commit: cdfa0cee3cd1bc20c960008bab8e0170f493a154 (branch release-1.9.2602, 2026-06-30)
License: University of Illinois/NCSA, see LICENSE.TXT, ThirdPartyNotices.txt and lib/Support/COPYRIGHT.regex.

## Why this commit

The system dxilconv.dll reports `101.9.2602.1360 (release/github-release-1.9.2602, a9caaf7fb)`.
a9caaf7fb is not in the public history, so the tip of the public release-1.9.2602 branch is used.
Converted output from this commit matches the system DLL byte for byte on the dxilconvcheck corpus.

## What is here

Only the files dxilconv.dll links and includes, found from the upstream build:
the objects the linker pulled into dxilconv.dll (`link /VERBOSE`) and the headers
those objects include (`ninja -t deps`). Files keep their upstream paths.

- `projects/dxilconv`: hlsl::DxbcConverter, the DXBC parser and the DxilCleanup pass
- `lib/DXIL`, `lib/DxilContainer`, `lib/DxilRootSignature`: DxilModule, containers, root signatures
- `lib/Support`, `lib/MSSupport`, `lib/DxcSupport`, `lib/IR`, `lib/Bitcode`, `lib/Analysis`,
  `lib/Transforms`: the LLVM 3.7 fork subset

`generated/` holds files the upstream build produces, copied from a configured upstream
build (Ninja, MSVC ARM64, `cmake/caches/PredefinedParams.cmake`, Release):

- `include/llvm/Config/*`, `include/llvm/Support/DataTypes.h`, `include/dxc/config.h`: configure_file output
- `include/llvm/IR/Intrinsics.gen`: llvm-tblgen output for `include/llvm/IR/Intrinsics.td` (not vendored)
- `projects/include/Tracing/DxcRuntimeEtw.h`: mc.exe output for `projects/include/Tracing/DxcRuntime.man` (not vendored)

## CMake targets

- `dxc_dxil`: the LLVM subset plus DxilModule, container and root signature code
- `dxc_dxbcconverter`: hlsl::DxbcConverter on top of `dxc_dxil`

All are static libraries built with exceptions and RTTI; those flags stay private to these targets.

## Patches

None. Upstream sources are unchanged.

Where the system DLL differs from upstream, ReactX's dll/dxilconv implements the system
behavior in front of the upstream converter instead of patching it:

- Convert checks the input container digest (retail or debug MD5 variant) unless
  `-disableHashCheck` is given, and writes the same kind of digest into the output container.
  Upstream does neither and leaves the output digest zero.
- Convert and ConvertInDriver argument checks follow the system DLL.
