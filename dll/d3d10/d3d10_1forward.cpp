/*
 * PROJECT:     ReactX Direct3D 10 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     d3d10_1.dll helpers, carried out by d3d10.dll
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d10runtime.hpp"

/* The d3d10.dll beside this module, loaded the first time a helper is called */
_Ret_maybenull_
static
FARPROC
RxGetD3D10Routine(
    _In_z_ PCSTR Name)
{
    static const HMODULE D3d10 = []()
    {
        WCHAR Path[MAX_PATH];
        HMODULE Self = NULL;
        PWSTR Slash;

        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&RxGetD3D10Routine), &Self) ||
            GetModuleFileNameW(Self, Path, _countof(Path)) == 0 || (Slash = wcsrchr(Path, L'\\')) == nullptr ||
            wcscpy_s(Slash + 1, _countof(Path) - (Slash + 1 - Path), L"d3d10.dll") != 0)
        {
            return (HMODULE)NULL;
        }

        return LoadLibraryExW(Path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    }();

    return D3d10 ? GetProcAddress(D3d10, Name) : nullptr;
}

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
    auto Routine = reinterpret_cast<decltype(&D3D10CompileEffectFromMemory)>(RxGetD3D10Routine("D3D10CompileEffectFromMemory"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pData, DataLength, pSrcFileName, pDefines, pInclude, HLSLFlags, FXFlags, ppCompiledEffect, ppErrors);
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
    auto Routine = reinterpret_cast<decltype(&D3D10CompileShader)>(RxGetD3D10Routine("D3D10CompileShader"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pSrcData, SrcDataSize, pFileName, pDefines, pInclude, pFunctionName, pProfile, Flags, ppShader, ppErrorMsgs);
}

extern "C"
HRESULT
WINAPI
D3D10CreateBlob(
    _In_ SIZE_T NumBytes,
    _Out_ LPD3D10BLOB* ppBuffer)
{
    auto Routine = reinterpret_cast<decltype(&D3D10CreateBlob)>(RxGetD3D10Routine("D3D10CreateBlob"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(NumBytes, ppBuffer);
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
    auto Routine = reinterpret_cast<decltype(&D3D10CreateEffectFromMemory)>(RxGetD3D10Routine("D3D10CreateEffectFromMemory"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pData, DataLength, FXFlags, pDevice, pEffectPool, ppEffect);
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
    auto Routine = reinterpret_cast<decltype(&D3D10CreateEffectPoolFromMemory)>(RxGetD3D10Routine("D3D10CreateEffectPoolFromMemory"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pData, DataLength, FXFlags, pDevice, ppEffectPool);
}

extern "C"
HRESULT
WINAPI
D3D10CreateStateBlock(
    _In_ ID3D10Device* pDevice,
    _In_ D3D10_STATE_BLOCK_MASK* pStateBlockMask,
    _Out_ ID3D10StateBlock** ppStateBlock)
{
    auto Routine = reinterpret_cast<decltype(&D3D10CreateStateBlock)>(RxGetD3D10Routine("D3D10CreateStateBlock"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pDevice, pStateBlockMask, ppStateBlock);
}

extern "C"
HRESULT
WINAPI
D3D10DisassembleEffect(
    _In_ ID3D10Effect* pEffect,
    _In_ BOOL EnableColorCode,
    _Out_ ID3D10Blob** ppDisassembly)
{
    auto Routine = reinterpret_cast<decltype(&D3D10DisassembleEffect)>(RxGetD3D10Routine("D3D10DisassembleEffect"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pEffect, EnableColorCode, ppDisassembly);
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
    auto Routine = reinterpret_cast<decltype(&D3D10DisassembleShader)>(RxGetD3D10Routine("D3D10DisassembleShader"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pShader, BytecodeLength, EnableColorCode, pComments, ppDisassembly);
}

extern "C"
LPCSTR
WINAPI
D3D10GetGeometryShaderProfile(
    _In_ ID3D10Device* pDevice)
{
    auto Routine = reinterpret_cast<decltype(&D3D10GetGeometryShaderProfile)>(RxGetD3D10Routine("D3D10GetGeometryShaderProfile"));

    if (Routine == nullptr)
        return nullptr;

    return Routine(pDevice);
}

extern "C"
HRESULT
WINAPI
D3D10GetInputAndOutputSignatureBlob(
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10Blob** ppSignatureBlob)
{
    auto Routine = reinterpret_cast<decltype(&D3D10GetInputAndOutputSignatureBlob)>(RxGetD3D10Routine("D3D10GetInputAndOutputSignatureBlob"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pShaderBytecode, BytecodeLength, ppSignatureBlob);
}

extern "C"
HRESULT
WINAPI
D3D10GetInputSignatureBlob(
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10Blob** ppSignatureBlob)
{
    auto Routine = reinterpret_cast<decltype(&D3D10GetInputSignatureBlob)>(RxGetD3D10Routine("D3D10GetInputSignatureBlob"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pShaderBytecode, BytecodeLength, ppSignatureBlob);
}

extern "C"
HRESULT
WINAPI
D3D10GetOutputSignatureBlob(
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10Blob** ppSignatureBlob)
{
    auto Routine = reinterpret_cast<decltype(&D3D10GetOutputSignatureBlob)>(RxGetD3D10Routine("D3D10GetOutputSignatureBlob"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pShaderBytecode, BytecodeLength, ppSignatureBlob);
}

extern "C"
LPCSTR
WINAPI
D3D10GetPixelShaderProfile(
    _In_ ID3D10Device* pDevice)
{
    auto Routine = reinterpret_cast<decltype(&D3D10GetPixelShaderProfile)>(RxGetD3D10Routine("D3D10GetPixelShaderProfile"));

    if (Routine == nullptr)
        return nullptr;

    return Routine(pDevice);
}

extern "C"
HRESULT
WINAPI
D3D10GetShaderDebugInfo(
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10Blob** ppDebugInfo)
{
    auto Routine = reinterpret_cast<decltype(&D3D10GetShaderDebugInfo)>(RxGetD3D10Routine("D3D10GetShaderDebugInfo"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pShaderBytecode, BytecodeLength, ppDebugInfo);
}

extern "C"
UINT64
WINAPI
D3D10GetVersion(VOID)
{
    auto Routine = reinterpret_cast<decltype(&D3D10GetVersion)>(RxGetD3D10Routine("D3D10GetVersion"));

    if (Routine == nullptr)
        return 0;

    return Routine();
}

extern "C"
LPCSTR
WINAPI
D3D10GetVertexShaderProfile(
    _In_ ID3D10Device* pDevice)
{
    auto Routine = reinterpret_cast<decltype(&D3D10GetVertexShaderProfile)>(RxGetD3D10Routine("D3D10GetVertexShaderProfile"));

    if (Routine == nullptr)
        return nullptr;

    return Routine(pDevice);
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
    auto Routine = reinterpret_cast<decltype(&D3D10PreprocessShader)>(RxGetD3D10Routine("D3D10PreprocessShader"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pSrcData, SrcDataSize, pFileName, pDefines, pInclude, ppShaderText, ppErrorMsgs);
}

extern "C"
HRESULT
WINAPI
D3D10ReflectShader(
    _In_reads_bytes_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_ ID3D10ShaderReflection** ppReflector)
{
    auto Routine = reinterpret_cast<decltype(&D3D10ReflectShader)>(RxGetD3D10Routine("D3D10ReflectShader"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pShaderBytecode, BytecodeLength, ppReflector);
}

extern "C"
HRESULT
WINAPI
D3D10RegisterLayers(VOID)
{
    auto Routine = reinterpret_cast<decltype(&D3D10RegisterLayers)>(RxGetD3D10Routine("D3D10RegisterLayers"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine();
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskDifference(
    _In_ D3D10_STATE_BLOCK_MASK* pA,
    _In_ D3D10_STATE_BLOCK_MASK* pB,
    _Out_ D3D10_STATE_BLOCK_MASK* pResult)
{
    auto Routine = reinterpret_cast<decltype(&D3D10StateBlockMaskDifference)>(RxGetD3D10Routine("D3D10StateBlockMaskDifference"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pA, pB, pResult);
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskDisableAll(
    _Out_ D3D10_STATE_BLOCK_MASK* pMask)
{
    auto Routine = reinterpret_cast<decltype(&D3D10StateBlockMaskDisableAll)>(RxGetD3D10Routine("D3D10StateBlockMaskDisableAll"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pMask);
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskDisableCapture(
    _Inout_ D3D10_STATE_BLOCK_MASK* pMask,
    _In_ D3D10_DEVICE_STATE_TYPES StateType,
    _In_ UINT RangeStart,
    _In_ UINT RangeLength)
{
    auto Routine = reinterpret_cast<decltype(&D3D10StateBlockMaskDisableCapture)>(RxGetD3D10Routine("D3D10StateBlockMaskDisableCapture"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pMask, StateType, RangeStart, RangeLength);
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskEnableAll(
    _Out_ D3D10_STATE_BLOCK_MASK* pMask)
{
    auto Routine = reinterpret_cast<decltype(&D3D10StateBlockMaskEnableAll)>(RxGetD3D10Routine("D3D10StateBlockMaskEnableAll"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pMask);
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskEnableCapture(
    _Inout_ D3D10_STATE_BLOCK_MASK* pMask,
    _In_ D3D10_DEVICE_STATE_TYPES StateType,
    _In_ UINT RangeStart,
    _In_ UINT RangeLength)
{
    auto Routine = reinterpret_cast<decltype(&D3D10StateBlockMaskEnableCapture)>(RxGetD3D10Routine("D3D10StateBlockMaskEnableCapture"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pMask, StateType, RangeStart, RangeLength);
}

extern "C"
BOOL
WINAPI
D3D10StateBlockMaskGetSetting(
    _In_ D3D10_STATE_BLOCK_MASK* pMask,
    _In_ D3D10_DEVICE_STATE_TYPES StateType,
    _In_ UINT Entry)
{
    auto Routine = reinterpret_cast<decltype(&D3D10StateBlockMaskGetSetting)>(RxGetD3D10Routine("D3D10StateBlockMaskGetSetting"));

    if (Routine == nullptr)
        return FALSE;

    return Routine(pMask, StateType, Entry);
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskIntersect(
    _In_ D3D10_STATE_BLOCK_MASK* pA,
    _In_ D3D10_STATE_BLOCK_MASK* pB,
    _Out_ D3D10_STATE_BLOCK_MASK* pResult)
{
    auto Routine = reinterpret_cast<decltype(&D3D10StateBlockMaskIntersect)>(RxGetD3D10Routine("D3D10StateBlockMaskIntersect"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pA, pB, pResult);
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskUnion(
    _In_ D3D10_STATE_BLOCK_MASK* pA,
    _In_ D3D10_STATE_BLOCK_MASK* pB,
    _Out_ D3D10_STATE_BLOCK_MASK* pResult)
{
    auto Routine = reinterpret_cast<decltype(&D3D10StateBlockMaskUnion)>(RxGetD3D10Routine("D3D10StateBlockMaskUnion"));

    if (Routine == nullptr)
        return E_NOTIMPL;

    return Routine(pA, pB, pResult);
}
