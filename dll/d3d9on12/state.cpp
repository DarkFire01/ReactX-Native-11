/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device DDI entries that record pipeline state for the next draw
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <algorithm>

/* The displacement map sampler and the vertex samplers follow the 16 pixel samplers */
static constexpr UINT NineDisplacementSampler = 256;
static constexpr UINT NineFirstVertexSampler = 257;
static constexpr UINT NineVertexSamplerCount = 4;

/* The legacy address state that stands for both the U and the V address */
static constexpr UINT NineAddressBoth = 12;
static constexpr UINT NineAddressU = 13;
static constexpr UINT NineAddressV = 14;

/* Render state values that ask for something other than the state */
static constexpr DWORD NineResolveDepthCode = 0x7FA05000;
static constexpr DWORD NineAtocCode = 0x434F5441;
static constexpr DWORD NineAtocPointOff = 0x304D3241;
static constexpr DWORD NineAtocPointOn = 0x314D3241;

/* Bump mapping stage states and where they go in the bump constants */
static constexpr UINT NineBumpMatrixFirst = 7;
static constexpr UINT NineBumpMatrixLast = 10;
static constexpr UINT NineBumpScale = 22;
static constexpr UINT NineBumpOffset = 23;

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

/* Copies a float out of a render state value */
static
FLOAT
NineStateFloat(
    _In_ DWORD Value)
{
    FLOAT Float;

    memcpy(&Float, &Value, sizeof(Float));
    return Float;
}

/* Stores a render state and marks what the next draw has to build again */
static
VOID
NineNoteRenderState(
    _Inout_ NineDevice* Device,
    _In_ UINT State,
    _In_ DWORD Value)
{
    NineDeviceState* Draw = &Device->State;
    FLOAT* Point = reinterpret_cast<FLOAT*>(Device->Extension + 16);
    DWORD Previous = Draw->RenderStates[State];

    switch (State)
    {
        case D3DDDIRS_POINTSIZE:
            /* The depth resolve request is not a size */
            if (Value == NineResolveDepthCode)
            {
                NINE_TRACE_ONCE(L"the depth resolve render state is not handled yet");
                return;
            }

            /* The alpha to coverage codes switch it and still land as the size */
            if (Value == NineAtocPointOff || Value == NineAtocPointOn)
            {
                Draw->AlphaToCoverage = Value == NineAtocPointOn;
                Draw->BlendDirty = TRUE;
            }
            Point[0] = NineStateFloat(Value);
            Draw->PointSizeDirty = TRUE;
            Draw->ExtensionDirty = TRUE;
            break;
        case D3DDDIRS_POINTSIZE_MIN:
            Point[1] = std::max(NineStateFloat(Value), 1.0f);
            Draw->PointSizeDirty = TRUE;
            Draw->ExtensionDirty = TRUE;
            break;
        case D3DDDIRS_POINTSIZE_MAX:
            Point[2] = std::min(NineStateFloat(Value), 8192.0f);
            Draw->PointSizeDirty = TRUE;
            Draw->ExtensionDirty = TRUE;
            break;
        case D3DDDIRS_ADAPTIVETESS_Y:
            /* Only the alpha to coverage codes are kept */
            if (Value != NineAtocCode && Value != 0)
                return;
            Draw->AlphaToCoverage = Value == NineAtocCode;
            Draw->BlendDirty = TRUE;
            break;
        case D3DDDIRS_SRCBLEND:
        case D3DDDIRS_DESTBLEND:
        case D3DDDIRS_ALPHABLENDENABLE:
        case D3DDDIRS_COLORWRITEENABLE:
        case D3DDDIRS_BLENDOP:
        case D3DDDIRS_COLORWRITEENABLE1:
        case D3DDDIRS_COLORWRITEENABLE2:
        case D3DDDIRS_COLORWRITEENABLE3:
        case D3DDDIRS_BLENDFACTOR:
        case D3DDDIRS_SEPARATEALPHABLENDENABLE:
        case D3DDDIRS_SRCBLENDALPHA:
        case D3DDDIRS_DESTBLENDALPHA:
        case D3DDDIRS_BLENDOPALPHA:
        case D3DDDIRS_MULTISAMPLEMASK:
            Draw->BlendDirty = TRUE;
            break;
        case D3DDDIRS_CLIPPLANEENABLE:
            if (((Previous ^ Value) & 0x3F) != 0)
                Draw->VertexKeyDirty = TRUE;
            break;
        case D3DDDIRS_FOGCOLOR:
        case D3DDDIRS_FOGTABLEMODE:
        case D3DDDIRS_FOGSTART:
        case D3DDDIRS_FOGEND:
        case D3DDDIRS_FOGDENSITY:
        case D3DDDIRS_ALPHAREF:
            Draw->FogDirty = TRUE;
            break;
        default:
            break;
    }
    Draw->RenderStates[State] = Value;
    NineNotePixelKey(Device);
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
        NineNoteRenderState(NineDeviceFrom(hDevice), Args->State, Args->Value);
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
    NineNotePixelKey(NineDeviceFrom(hDevice));
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

    /* Bump mapping states of the first 8 stages are pixel shader constants */
    if (Stage < 8)
    {
        NineDevice* Device = NineDeviceFrom(hDevice);
        UINT State = static_cast<UINT>(Args->State);
        UINT At = ~0u;

        if (State >= NineBumpMatrixFirst && State <= NineBumpMatrixLast)
            At = 16 * Stage + 4 * (State - NineBumpMatrixFirst);
        else if (State == NineBumpScale || State == NineBumpOffset)
            At = 128 + 16 * Stage + 4 * (State - NineBumpScale);
        if (At != ~0u)
        {
            memcpy(Device->Bump + At, &Args->Value, sizeof(Args->Value));
            Device->State.BumpDirty = TRUE;
        }
    }

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
    NineNotePixelKey(NineDeviceFrom(hDevice));
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
        CopyMemory(NineDeviceFrom(hDevice)->Extension + 16 * (2 + Args->Index), Args->Plane,
                   sizeof(State->ClipPlanes[Args->Index]));
        State->ClipPlanesDirty = TRUE;
        State->ExtensionDirty = TRUE;
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
