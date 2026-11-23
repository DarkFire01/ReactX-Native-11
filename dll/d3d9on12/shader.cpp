/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 9 shader objects, shared between creates of identical code
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

static
UINT64
NineHashCode(
    _In_reads_bytes_(Size) const BYTE* Code,
    _In_ SIZE_T Size)
{
    UINT64 Hash = 0xCBF29CE484222325ull;

    for (SIZE_T Index = 0; Index < Size; Index += 1)
    {
        Hash ^= Code[Index];
        Hash *= 0x100000001B3ull;
    }
    return Hash;
}

/* Hands out the shader object for this code, making it on the first create */
static
NineShader*
NineFindOrMakeShader(
    _Inout_ NineDevice* Device,
    _In_ NineShaderStage Stage,
    _In_reads_bytes_(Size) const BYTE* Code,
    _In_ UINT Size)
{
    std::unordered_multimap<UINT64, std::unique_ptr<NineShader>>& Cache = Device->ShaderCache[Stage];
    UINT64 Hash = NineHashCode(Code, Size);
    std::unique_ptr<NineShader> Shader;
    NineShader* Made;
    auto Range = Cache.equal_range(Hash);

    for (auto Entry = Range.first; Entry != Range.second; ++Entry)
    {
        NineShader* Known = Entry->second.get();

        if (Known->Code.size() == Size && memcmp(Known->Code.data(), Code, Size) == 0)
        {
            Known->References += 1;
            return Known;
        }
    }

    Shader = std::make_unique<NineShader>();
    Shader->Stage = Stage;
    Shader->References = 1;
    Shader->Hash = Hash;
    Shader->Code.assign(Code, Code + Size);
    Made = Shader.get();
    Cache.emplace(Hash, std::move(Shader));
    return Made;
}

/* Drops one create of a shader and frees it with the last */
static
VOID
NineReleaseShader(
    _Inout_ NineDevice* Device,
    _Inout_ NineShader* Shader)
{
    std::unordered_multimap<UINT64, std::unique_ptr<NineShader>>& Cache = Device->ShaderCache[Shader->Stage];
    auto Range = Cache.equal_range(Shader->Hash);

    Shader->References -= 1;
    if (Shader->References != 0)
        return;

    if (Device->State.Shaders[Shader->Stage] == Shader)
        Device->State.Shaders[Shader->Stage] = nullptr;
    NineDropConverted(Device, Shader);
    for (auto Entry = Range.first; Entry != Range.second; ++Entry)
    {
        if (Entry->second.get() == Shader)
        {
            Cache.erase(Entry);
            break;
        }
    }
}

static
HRESULT
NineCreateShader(
    _In_ HANDLE hDevice,
    _In_ NineShaderStage Stage,
    _In_ UINT Size,
    _In_opt_ const UINT* Code,
    _Out_ HANDLE* Handle)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr || Code == nullptr)
        return E_INVALIDARG;

    *Handle = NineFindOrMakeShader(Device, Stage, reinterpret_cast<const BYTE*>(Code), Size);
    return NineDxgiResult(S_OK);
}

static
HRESULT
NineDeleteShader(
    _In_ HANDLE hDevice,
    _In_ HANDLE hShader)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr || hShader == nullptr)
        return E_INVALIDARG;

    NineReleaseShader(Device, static_cast<NineShader*>(hShader));
    return NineDxgiResult(S_OK);
}

static
HRESULT
NineBindShader(
    _In_ HANDLE hDevice,
    _In_ NineShaderStage Stage,
    _In_ HANDLE hShader)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    /* The runtime always has a shader to set; the fixed function pipeline comes as one too */
    if (Device == nullptr || hShader == nullptr)
        return E_INVALIDARG;

    if (Device->State.Shaders[Stage] != hShader && Stage == NineVertexStage)
        Device->State.VertexKeyDirty = TRUE;
    Device->State.Shaders[Stage] = static_cast<NineShader*>(hShader);
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineCreateVertexShaderFunc(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_CREATEVERTEXSHADERFUNC* Args,
    _In_ const UINT* Code)
{
    if (Args == nullptr)
        return E_INVALIDARG;
    return NineCreateShader(hDevice, NineVertexStage, Args->Size, Code, &Args->ShaderHandle);
}

HRESULT
APIENTRY
NineDeleteVertexShaderFunc(
    _In_ HANDLE hDevice,
    _In_ HANDLE hShader)
{
    return NineDeleteShader(hDevice, hShader);
}

HRESULT
APIENTRY
NineSetVertexShaderFunc(
    _In_ HANDLE hDevice,
    _In_ HANDLE hShader)
{
    return NineBindShader(hDevice, NineVertexStage, hShader);
}

HRESULT
APIENTRY
NineCreatePixelShader(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_CREATEPIXELSHADER* Args,
    _In_ const UINT* Code)
{
    if (Args == nullptr)
        return E_INVALIDARG;
    return NineCreateShader(hDevice, NinePixelStage, Args->CodeSize, Code, &Args->ShaderHandle);
}

HRESULT
APIENTRY
NineDeletePixelShader(
    _In_ HANDLE hDevice,
    _In_ HANDLE hShader)
{
    return NineDeleteShader(hDevice, hShader);
}

HRESULT
APIENTRY
NineSetPixelShader(
    _In_ HANDLE hDevice,
    _In_ HANDLE hShader)
{
    return NineBindShader(hDevice, NinePixelStage, hShader);
}
