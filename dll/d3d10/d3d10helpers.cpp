/*
 * PROJECT:     ReactX Direct3D 10 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     d3d10.dll helpers: blobs, shader tools, state block masks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d10runtime.hpp"

#include <d3dcompiler.h>
#include <new>

#include "blob.hpp"

/* What D3D10CreateBlob reports for a missing output pointer */
constexpr HRESULT RxInvalidCall = (HRESULT)0x8876086C;

/* Version and layers belong to d3d10core.dll */
extern "C" UINT64 WINAPI D3D10CoreGetVersion(VOID);
extern "C" HRESULT WINAPI D3D10CoreRegisterLayers(VOID);

extern "C"
HRESULT
WINAPI
D3D10CreateBlob(
    _In_ SIZE_T NumBytes,
    _Out_ LPD3D10BLOB* ppBuffer)
{
    RxBlob* Blob;
    HRESULT hr;

    if (ppBuffer == nullptr)
        return RxInvalidCall;

    hr = RxBlob::Create(NumBytes, &Blob);
    *ppBuffer = SUCCEEDED(hr) ? Blob : nullptr;
    return hr;
}

extern "C"
UINT64
WINAPI
D3D10GetVersion(VOID)
{
    return D3D10CoreGetVersion();
}

extern "C"
HRESULT
WINAPI
D3D10RegisterLayers(VOID)
{
    return D3D10CoreRegisterLayers();
}

/* Shader tools go to the HLSL compiler, found when first needed */

_Ret_maybenull_
static
HMODULE
RxGetCompiler(VOID)
{
    static const HMODULE Compiler = []()
    {
        HMODULE Found = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32 |
                                                                   LOAD_LIBRARY_SEARCH_APPLICATION_DIR);

        if (Found == NULL)
            Found = LoadLibraryW(L"d3dcompiler_47.dll");
        if (Found == NULL)
            Found = LoadLibraryW(L"d3dcompiler_43.dll");
        return Found;
    }();

    return Compiler;
}

template <typename Routine>
_Ret_maybenull_
static
Routine
RxGetCompilerRoutine(
    _In_z_ PCSTR Name)
{
    HMODULE Compiler = RxGetCompiler();

    return Compiler ? reinterpret_cast<Routine>(GetProcAddress(Compiler, Name)) : nullptr;
}

extern "C"
HRESULT
WINAPI
D3D10CompileShader(
    _In_reads_bytes_(SrcDataSize) LPCSTR pSrcData,
    _In_ SIZE_T SrcDataSize,
    _In_opt_ LPCSTR pFileName,
    _In_opt_ const D3D10_SHADER_MACRO* pDefines,
    _In_opt_ LPD3D10INCLUDE pInclude,
    _In_ LPCSTR pFunctionName,
    _In_ LPCSTR pProfile,
    _In_ UINT Flags,
    _Out_ ID3D10Blob** ppShader,
    _Out_opt_ ID3D10Blob** ppErrorMsgs)
{
    auto Compile = RxGetCompilerRoutine<pD3DCompile>("D3DCompile");

    if (ppShader != nullptr)
        *ppShader = nullptr;
    if (ppErrorMsgs != nullptr)
        *ppErrorMsgs = nullptr;
    if (Compile == nullptr)
        return E_NOTIMPL;

    /* Macros, includes and blobs are the same types under both names */
    return Compile(pSrcData, SrcDataSize, pFileName, reinterpret_cast<const D3D_SHADER_MACRO*>(pDefines),
                   reinterpret_cast<ID3DInclude*>(pInclude), pFunctionName, pProfile, Flags, 0, ppShader, ppErrorMsgs);
}

extern "C"
HRESULT
WINAPI
D3D10DisassembleShader(
    _In_reads_bytes_(BytecodeLength) const void* pShader,
    _In_ SIZE_T BytecodeLength,
    _In_ BOOL EnableColorCode,
    _In_opt_ LPCSTR pComments,
    _Out_ ID3D10Blob** ppDisassembly)
{
    auto Disassemble = RxGetCompilerRoutine<decltype(&D3DDisassemble)>("D3DDisassemble");

    if (ppDisassembly != nullptr)
        *ppDisassembly = nullptr;
    if (Disassemble == nullptr)
        return E_NOTIMPL;

    return Disassemble(pShader, BytecodeLength, EnableColorCode ? D3D_DISASM_ENABLE_COLOR_CODE : 0, pComments,
                       ppDisassembly);
}

extern "C"
HRESULT
WINAPI
D3D10PreprocessShader(
    _In_reads_bytes_(SrcDataSize) LPCSTR pSrcData,
    _In_ SIZE_T SrcDataSize,
    _In_opt_ LPCSTR pFileName,
    _In_opt_ const D3D10_SHADER_MACRO* pDefines,
    _In_opt_ LPD3D10INCLUDE pInclude,
    _Out_ ID3D10Blob** ppShaderText,
    _Out_opt_ ID3D10Blob** ppErrorMsgs)
{
    auto Preprocess = RxGetCompilerRoutine<decltype(&D3DPreprocess)>("D3DPreprocess");

    if (ppShaderText != nullptr)
        *ppShaderText = nullptr;
    if (ppErrorMsgs != nullptr)
        *ppErrorMsgs = nullptr;
    if (Preprocess == nullptr)
        return E_NOTIMPL;

    return Preprocess(pSrcData, SrcDataSize, pFileName, reinterpret_cast<const D3D_SHADER_MACRO*>(pDefines),
                      reinterpret_cast<ID3DInclude*>(pInclude), ppShaderText, ppErrorMsgs);
}

/* The signature chunks of a shader, in a container of their own */
static
HRESULT
RxGetSignature(
    _In_z_ PCSTR RoutineName,
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10Blob** ppSignatureBlob)
{
    auto Get = RxGetCompilerRoutine<decltype(&D3DGetInputSignatureBlob)>(RoutineName);

    if (ppSignatureBlob == nullptr)
        return E_INVALIDARG;
    *ppSignatureBlob = nullptr;
    if (Get == nullptr)
        return E_NOTIMPL;

    return Get(pShaderBytecode, BytecodeLength, ppSignatureBlob);
}

extern "C"
HRESULT
WINAPI
D3D10GetInputSignatureBlob(
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10Blob** ppSignatureBlob)
{
    return RxGetSignature("D3DGetInputSignatureBlob", pShaderBytecode, BytecodeLength, ppSignatureBlob);
}

extern "C"
HRESULT
WINAPI
D3D10GetOutputSignatureBlob(
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10Blob** ppSignatureBlob)
{
    return RxGetSignature("D3DGetOutputSignatureBlob", pShaderBytecode, BytecodeLength, ppSignatureBlob);
}

extern "C"
HRESULT
WINAPI
D3D10GetInputAndOutputSignatureBlob(
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10Blob** ppSignatureBlob)
{
    return RxGetSignature("D3DGetInputAndOutputSignatureBlob", pShaderBytecode, BytecodeLength, ppSignatureBlob);
}

extern "C"
HRESULT
WINAPI
D3D10GetShaderDebugInfo(
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10Blob** ppDebugInfo)
{
    UNREFERENCED_PARAMETER(pShaderBytecode);
    UNREFERENCED_PARAMETER(BytecodeLength);

    RX_TRACE(L"unimplemented D3D10GetShaderDebugInfo");
    if (ppDebugInfo != nullptr)
        *ppDebugInfo = nullptr;
    return E_NOTIMPL;
}

extern "C"
HRESULT
WINAPI
D3D10ReflectShader(
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10ShaderReflection** ppReflector)
{
    UNREFERENCED_PARAMETER(pShaderBytecode);
    UNREFERENCED_PARAMETER(BytecodeLength);

    RX_TRACE(L"unimplemented D3D10ReflectShader");
    if (ppReflector != nullptr)
        *ppReflector = nullptr;
    return E_NOTIMPL;
}

/* Profiles for the one shader model a Direct3D 10.0 device runs */

extern "C"
LPCSTR
WINAPI
D3D10GetVertexShaderProfile(
    _In_ ID3D10Device* pDevice)
{
    UNREFERENCED_PARAMETER(pDevice);
    return "vs_4_0";
}

extern "C"
LPCSTR
WINAPI
D3D10GetGeometryShaderProfile(
    _In_ ID3D10Device* pDevice)
{
    UNREFERENCED_PARAMETER(pDevice);
    return "gs_4_0";
}

extern "C"
LPCSTR
WINAPI
D3D10GetPixelShaderProfile(
    _In_ ID3D10Device* pDevice)
{
    UNREFERENCED_PARAMETER(pDevice);
    return "ps_4_0";
}

/* Effects are not part of this runtime yet */

extern "C"
HRESULT
WINAPI
D3D10CompileEffectFromMemory(
    _In_reads_bytes_(DataLength) void* pData,
    _In_ SIZE_T DataLength,
    _In_opt_ LPCSTR pSrcFileName,
    _In_opt_ const D3D10_SHADER_MACRO* pDefines,
    _In_opt_ ID3D10Include* pInclude,
    _In_ UINT HLSLFlags,
    _In_ UINT FXFlags,
    _Out_ ID3D10Blob** ppCompiledEffect,
    _Out_opt_ ID3D10Blob** ppErrors)
{
    UNREFERENCED_PARAMETER(pData);
    UNREFERENCED_PARAMETER(DataLength);
    UNREFERENCED_PARAMETER(pSrcFileName);
    UNREFERENCED_PARAMETER(pDefines);
    UNREFERENCED_PARAMETER(pInclude);
    UNREFERENCED_PARAMETER(HLSLFlags);
    UNREFERENCED_PARAMETER(FXFlags);

    RX_TRACE(L"unimplemented D3D10CompileEffectFromMemory");
    if (ppCompiledEffect != nullptr)
        *ppCompiledEffect = nullptr;
    if (ppErrors != nullptr)
        *ppErrors = nullptr;
    return E_NOTIMPL;
}

extern "C"
HRESULT
WINAPI
D3D10CreateEffectFromMemory(
    _In_reads_bytes_(DataLength) void* pData,
    _In_ SIZE_T DataLength,
    _In_ UINT FXFlags,
    _In_ ID3D10Device* pDevice,
    _In_opt_ ID3D10EffectPool* pEffectPool,
    _Out_ ID3D10Effect** ppEffect)
{
    UNREFERENCED_PARAMETER(pData);
    UNREFERENCED_PARAMETER(DataLength);
    UNREFERENCED_PARAMETER(FXFlags);
    UNREFERENCED_PARAMETER(pDevice);
    UNREFERENCED_PARAMETER(pEffectPool);

    RX_TRACE(L"unimplemented D3D10CreateEffectFromMemory");
    if (ppEffect != nullptr)
        *ppEffect = nullptr;
    return E_NOTIMPL;
}

extern "C"
HRESULT
WINAPI
D3D10CreateEffectPoolFromMemory(
    _In_reads_bytes_(DataLength) void* pData,
    _In_ SIZE_T DataLength,
    _In_ UINT FXFlags,
    _In_ ID3D10Device* pDevice,
    _Out_ ID3D10EffectPool** ppEffectPool)
{
    UNREFERENCED_PARAMETER(pData);
    UNREFERENCED_PARAMETER(DataLength);
    UNREFERENCED_PARAMETER(FXFlags);
    UNREFERENCED_PARAMETER(pDevice);

    RX_TRACE(L"unimplemented D3D10CreateEffectPoolFromMemory");
    if (ppEffectPool != nullptr)
        *ppEffectPool = nullptr;
    return E_NOTIMPL;
}

extern "C"
HRESULT
WINAPI
D3D10DisassembleEffect(
    _In_ ID3D10Effect* pEffect,
    _In_ BOOL EnableColorCode,
    _Out_ ID3D10Blob** ppDisassembly)
{
    UNREFERENCED_PARAMETER(pEffect);
    UNREFERENCED_PARAMETER(EnableColorCode);

    RX_TRACE(L"unimplemented D3D10DisassembleEffect");
    if (ppDisassembly != nullptr)
        *ppDisassembly = nullptr;
    return E_NOTIMPL;
}

extern "C"
HRESULT
WINAPI
D3D10CreateStateBlock(
    _In_ ID3D10Device* pDevice,
    _In_ D3D10_STATE_BLOCK_MASK* pStateBlockMask,
    _Out_ ID3D10StateBlock** ppStateBlock)
{
    UNREFERENCED_PARAMETER(pDevice);
    UNREFERENCED_PARAMETER(pStateBlockMask);

    RX_TRACE(L"unimplemented D3D10CreateStateBlock");
    if (ppStateBlock != nullptr)
        *ppStateBlock = nullptr;
    return E_NOTIMPL;
}
