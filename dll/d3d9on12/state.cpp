/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device DDI entries that record pipeline state for the next draw
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* The displacement map sampler and the vertex samplers follow the 16 pixel samplers */
static constexpr UINT NineDisplacementSampler = 256;
static constexpr UINT NineFirstVertexSampler = 257;
static constexpr UINT NineVertexSamplerCount = 4;

/* The legacy address state that stands for both the U and the V address */
static constexpr UINT NineAddressBoth = 12;
static constexpr UINT NineAddressU = 13;
static constexpr UINT NineAddressV = 14;

static
NineDevice*
NineDeviceFrom(
    _In_ HANDLE hDevice)
{
    return static_cast<NineDevice*>(hDevice);
}

/* Copies registers into a stage's shadow constants, as many as the application passed */
static
HRESULT
NineStoreConstants(
    _In_ HANDLE hDevice,
    _In_ BOOLEAN Vertex,
    _In_ NineConstantKind Kind,
    _In_ UINT Register,
    _In_ UINT Count,
    _In_reads_opt_(Count) const VOID* Values)
{
    NineStageConstants* Stage;
    BYTE* Shadow;

    if (hDevice == NULL || (Count != 0 && Values == nullptr))
        return E_INVALIDARG;

    if (static_cast<UINT64>(Register) + Count > NineConstantRegisters[Kind])
    {
        NineTrace(L"constants %u to %u are past the %u registers of kind %u", Register, Register + Count,
                  NineConstantRegisters[Kind], Kind);
        return E_INVALIDARG;
    }

    Stage = Vertex ? &NineDeviceFrom(hDevice)->State.VertexConstants : &NineDeviceFrom(hDevice)->State.PixelConstants;
    if (Kind == NineConstantFloat)
        Shadow = Stage->Float;
    else if (Kind == NineConstantInteger)
        Shadow = Stage->Integer;
    else
        Shadow = Stage->Boolean;

    CopyMemory(Shadow + Register * NineConstantStride[Kind], Values, Count * NineConstantStride[Kind]);
    Stage->Dirty[Kind] = TRUE;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetRenderState(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_RENDERSTATE* Args)
{
    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    if (static_cast<UINT>(Args->State) < NineRenderStateCount)
        NineDeviceFrom(hDevice)->State.RenderStates[Args->State] = Args->Value;
    else
        NINE_TRACE_ONCE(L"render state %u is out of range", Args->State);
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineUpdateWInfo(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_WINFO* Args)
{
    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    NineDeviceFrom(hDevice)->State.WInfo = *Args;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetTextureStageState(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_TEXTURESTAGESTATE* Args)
{
    DWORD (*States)[NineStageStateCount];
    UINT Stage;

    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    if (Args->Stage == NineDisplacementSampler)
        Stage = 16;
    else if (Args->Stage >= NineFirstVertexSampler && Args->Stage < NineFirstVertexSampler + NineVertexSamplerCount)
        Stage = 17 + (Args->Stage - NineFirstVertexSampler);
    else
        Stage = Args->Stage;

    if (Stage >= NineStageCount || static_cast<UINT>(Args->State) >= NineStageStateCount)
        return E_INVALIDARG;

    /* The combined address state sets both the U and the V address */
    States = NineDeviceFrom(hDevice)->State.StageStates;
    if (static_cast<UINT>(Args->State) == NineAddressBoth)
    {
        States[Stage][NineAddressU] = Args->Value;
        States[Stage][NineAddressV] = Args->Value;
    }
    else
    {
        States[Stage][Args->State] = Args->Value;
    }
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetPixelShaderConst(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETPIXELSHADERCONST* Args,
    _In_ const FLOAT* Values)
{
    if (Args == nullptr)
        return E_INVALIDARG;
    return NineStoreConstants(hDevice, FALSE, NineConstantFloat, Args->Register, Args->Count, Values);
}

HRESULT
APIENTRY
NineSetPixelShaderConstI(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETPIXELSHADERCONSTI* Args,
    _In_ const INT* Values)
{
    if (Args == nullptr)
        return E_INVALIDARG;
    return NineStoreConstants(hDevice, FALSE, NineConstantInteger, Args->Register, Args->Count, Values);
}

HRESULT
APIENTRY
NineSetPixelShaderConstB(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETPIXELSHADERCONSTB* Args,
    _In_ const BOOL* Values)
{
    if (Args == nullptr)
        return E_INVALIDARG;
    return NineStoreConstants(hDevice, FALSE, NineConstantBoolean, Args->Register, Args->Count, Values);
}

HRESULT
APIENTRY
NineSetVertexShaderConst(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETVERTEXSHADERCONST* Args,
    _In_ const VOID* Values)
{
    if (Args == nullptr)
        return E_INVALIDARG;
    return NineStoreConstants(hDevice, TRUE, NineConstantFloat, Args->Register, Args->Count, Values);
}

HRESULT
APIENTRY
NineSetVertexShaderConstI(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETVERTEXSHADERCONSTI* Args,
    _In_ const INT* Values)
{
    if (Args == nullptr)
        return E_INVALIDARG;
    return NineStoreConstants(hDevice, TRUE, NineConstantInteger, Args->Register, Args->Count, Values);
}

HRESULT
APIENTRY
NineSetVertexShaderConstB(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETVERTEXSHADERCONSTB* Args,
    _In_ const BOOL* Values)
{
    if (Args == nullptr)
        return E_INVALIDARG;
    return NineStoreConstants(hDevice, TRUE, NineConstantBoolean, Args->Register, Args->Count, Values);
}

HRESULT
APIENTRY
NineSetViewport(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_VIEWPORTINFO* Args)
{
    NineDeviceState* State;

    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    State = &NineDeviceFrom(hDevice)->State;
    State->Viewport[0] = static_cast<FLOAT>(Args->X);
    State->Viewport[1] = static_cast<FLOAT>(Args->Y);
    State->Viewport[2] = static_cast<FLOAT>(Args->Width);
    State->Viewport[3] = static_cast<FLOAT>(Args->Height);
    State->ViewportDirty = TRUE;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetZRange(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_ZRANGE* Args)
{
    NineDeviceState* State;

    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    State = &NineDeviceFrom(hDevice)->State;
    State->ZRange = *Args;
    State->ViewportDirty = TRUE;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetScissorRect(
    _In_ HANDLE hDevice,
    _In_ const RECT* Rect)
{
    if (hDevice == NULL || Rect == nullptr)
        return E_INVALIDARG;

    NineDeviceFrom(hDevice)->State.Scissor = *Rect;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetClipPlane(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETCLIPPLANE* Args)
{
    NineDeviceState* State;

    if (hDevice == NULL || Args == nullptr || Args->Index >= NineClipPlaneCount)
        return E_INVALIDARG;

    State = &NineDeviceFrom(hDevice)->State;
    if (memcmp(State->ClipPlanes[Args->Index], Args->Plane, sizeof(State->ClipPlanes[Args->Index])) != 0)
    {
        CopyMemory(State->ClipPlanes[Args->Index], Args->Plane, sizeof(State->ClipPlanes[Args->Index]));
        State->ClipPlanesDirty = TRUE;
    }
    return NineDxgiResult(S_OK);
}

/* Submits recorded work; the translation layer skips an empty command list */
HRESULT
APIENTRY
NineFlush1(
    _In_ HANDLE hDevice,
    _In_ UINT FlushFlags)
{
    UNREFERENCED_PARAMETER(FlushFlags);

    if (hDevice == NULL)
        return E_INVALIDARG;

    try
    {
        NineDeviceFrom(hDevice)->Context->Flush(TL::COMMAND_LIST_TYPE_ALL_MASK);
    }
    catch (_com_error& Error)
    {
        return NineDxgiResult(Error.Error());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    return NineDxgiResult(S_OK);
}
