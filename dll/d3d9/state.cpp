/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Fixed function and pipeline state the application sets and reads back
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Render states past the last one land on it */
static constexpr UINT D9LastRenderState = D9RenderStateCount - 1;

/* Vertex pipeline states the driver never sees while the runtime processes vertices itself */
static const BYTE D9SoftwareOnlyStates[] =
{
    48, 137, 139, 140, 141, 142, 143, 145, 146, 147, 148, 151, 152, 157, 158, 159, 160, 167, 170,
};

/* Vertex pipeline states the runtime turns into vertex shaders instead of handing on */
static const BYTE D9ShaderPipelineStates[] =
{
    137, 139, 140, 141, 142, 143, 145, 146, 147, 148, 151, 157, 158, 159, 160, 167, 170,
};

/* Where the fixed function pixel shaders keep their values among the pixel constants: the texture factor,
   each stage's constant, the luminance scale and offset of two stages per register, and two registers of
   bump matrix per stage */
static constexpr UINT D9FactorRegister = 2;
static constexpr UINT D9StageConstantRegister = 3;
static constexpr UINT D9LuminanceRegister = 11;
static constexpr UINT D9BumpRegister = 15;

/* The displacement map and vertex samplers are kept right after the sixteen pixel samplers */
static constexpr UINT D9SamplerSlotShift = D3DDMAPSAMPLER - 16;

/* The driver's stage state for each sampler state type */
static const BYTE D9SamplerDriverTypes[D9SamplerStateCount] =
{
    0, D3DDDITSS_ADDRESSU, D3DDDITSS_ADDRESSV, D3DDDITSS_ADDRESSW, D3DDDITSS_BORDERCOLOR, D3DDDITSS_MAGFILTER,
    D3DDDITSS_MINFILTER, D3DDDITSS_MIPFILTER, D3DDDITSS_MIPMAPLODBIAS, D3DDDITSS_MAXMIPLEVEL,
    D3DDDITSS_MAXANISOTROPY, D3DDDITSS_SRGBTEXTURE, D3DDDITSS_ELEMENTINDEX, D3DDDITSS_DMAPOFFSET,
};

/* Filters as the driver numbers them, by the application's filter; the mip filter has its own list */
static const BYTE D9DriverFilters[] = { 1, 1, 2, 3, 2, 2, 6, 7, 8 };
static const BYTE D9DriverMipFilters[] = { 0, 1, 2, 2, 2, 2, 2, 2 };

static
BOOLEAN
D9ListHolds(
    _In_reads_(Count) const BYTE* List,
    _In_ SIZE_T Count,
    _In_ UINT State)
{
    for (SIZE_T Index = 0; Index < Count; Index++)
    {
        if (List[Index] == State)
            return TRUE;
    }
    return FALSE;
}

/* Values past the end of a range land on its last entry */
static
UINT
D9Clamp(
    _In_ DWORD Value,
    _In_ UINT Last)
{
    return Value > Last ? Last : Value;
}

/* One color channel as the fixed function pixel constants hold it */
static
FLOAT
D9Channel(
    _In_ DWORD Color,
    _In_ UINT Shift)
{
    return static_cast<FLOAT>(((Color >> Shift) & 0xFF) / 255.0);
}

/* A pure device keeps no state only when its driver lacks version 2 shaders; otherwise it runs as any other */
BOOLEAN
D9Device::KeepsNoState(VOID) const
{
    if ((BehaviorFlags & D3DCREATE_PUREDEVICE) == 0)
        return FALSE;
    return Caps.VertexShaderVersion < D3DVS_VERSION(2, 0) && Caps.PixelShaderVersion < D3DPS_VERSION(2, 0);
}

/* Hands a render state on, unless the runtime's own pipelines consume it */
VOID
D9Device::SendStateValue(
    _In_ UINT State,
    _In_ DWORD Value)
{
    D3DDDIARG_RENDERSTATE Args = {};

    if (SoftwareVertices && D9ListHolds(D9SoftwareOnlyStates, _countof(D9SoftwareOnlyStates), State))
        return;
    if (!SoftwareVertices && D9ListHolds(D9ShaderPipelineStates, _countof(D9ShaderPipelineStates), State))
        return;

    /* The pixel shaders standing in for the fixed function stages take these two */
    if (State == D3DRS_SPECULARENABLE)
        return;
    if (State == D3DRS_TEXTUREFACTOR)
    {
        SendPixelConstant(D9FactorRegister, D9Channel(Value, 16), D9Channel(Value, 8), D9Channel(Value, 0),
                          D9Channel(Value, 24));
        return;
    }

    Args.State = static_cast<D3DDDIRENDERSTATETYPE>(State);
    Args.Value = Value;
    DdiRenderState(&Args);
}

STDMETHODIMP
D9Device::SetRenderState(
    _In_ D3DRENDERSTATETYPE State,
    _In_ DWORD Value)
{
    D9_DEVICE_LOCK();
    UINT Slot = D9Clamp(State, D9LastRenderState);

    /* Undefined and retired states reach the driver like any other */
    if (!KeepsNoState())
    {
        if (RenderStates[Slot] == Value)
            return S_OK;
        RenderStates[Slot] = Value;
    }
    SendStateValue(Slot, Value);
    return S_OK;
}

STDMETHODIMP
D9Device::GetRenderState(
    _In_ D3DRENDERSTATETYPE State,
    _Out_ DWORD* Value)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState() || static_cast<DWORD>(State) >= D9RenderStateCount || D9IsRetiredState(State))
        return D3DERR_INVALIDCALL;

    *Value = RenderStates[State];
    return S_OK;
}

/* The stage states the fixed function pixel shaders consume, and the pixel constants some of them feed */
VOID
D9Device::SendStageValue(
    _In_ UINT Stage,
    _In_ UINT Type,
    _In_ DWORD Value)
{
    const DWORD* Types = StageStates[Stage];
    D3DDDIARG_SETPIXELSHADERCONST Constant = {};
    DWORD Bits[4] = {};
    BOOLEAN Pass = TRUE;

    switch (Type)
    {
    case D3DTSS_COLOROP:
    case D3DTSS_COLORARG1:
    case D3DTSS_COLORARG2:
    case D3DTSS_ALPHAOP:
    case D3DTSS_ALPHAARG1:
    case D3DTSS_ALPHAARG2:
    case D3DTSS_COLORARG0:
    case D3DTSS_ALPHAARG0:
    case D3DTSS_RESULTARG:
        return;
    case D3DTSS_CONSTANT:
        SendPixelConstant(D9StageConstantRegister + Stage, D9Channel(Value, 16), D9Channel(Value, 8),
                          D9Channel(Value, 0), D9Channel(Value, 24));
        return;
    case D3DTSS_BUMPENVMAT00:
    case D3DTSS_BUMPENVMAT10:
        Constant.Register = D9BumpRegister + Stage * 2;
        Bits[0] = Types[D3DTSS_BUMPENVMAT00];
        Bits[1] = Types[D3DTSS_BUMPENVMAT10];
        break;
    case D3DTSS_BUMPENVMAT01:
    case D3DTSS_BUMPENVMAT11:
        Constant.Register = D9BumpRegister + Stage * 2 + 1;
        Bits[0] = Types[D3DTSS_BUMPENVMAT01];
        Bits[1] = Types[D3DTSS_BUMPENVMAT11];
        break;
    case D3DTSS_BUMPENVLSCALE:
    case D3DTSS_BUMPENVLOFFSET:
    {
        UINT Even = Stage & ~1u;

        Constant.Register = D9LuminanceRegister + Stage / 2;
        Bits[0] = StageStates[Even][D3DTSS_BUMPENVLSCALE];
        Bits[1] = StageStates[Even][D3DTSS_BUMPENVLOFFSET];
        Bits[2] = StageStates[Even + 1][D3DTSS_BUMPENVLSCALE];
        Bits[3] = StageStates[Even + 1][D3DTSS_BUMPENVLOFFSET];
        break;
    }
    default:
        Pass = FALSE;
        break;
    }

    /* These keep their raw bits in a constant and still reach the driver */
    if (Pass)
    {
        Constant.Count = 1;
        DdiPixelShaderConst(&Constant, reinterpret_cast<const FLOAT*>(Bits));
    }
    SendTextureStageState(Stage, Type, Value);
}

/* Sampler states reach the driver as stage states of the sampler's own number */
VOID
D9Device::SendSamplerValue(
    _In_ UINT Sampler,
    _In_ UINT Type,
    _In_ DWORD Value)
{
    SendTextureStageState(Sampler, D9SamplerDriverTypes[Type], Value);
}

/* Filters are renumbered for the driver on every stage state it gets */
DWORD
D9DriverStageValue(
    _In_ UINT Type,
    _In_ DWORD Value)
{
    if (Type == D3DDDITSS_MAGFILTER || Type == D3DDDITSS_MINFILTER)
        return D9DriverFilters[D9Clamp(Value, _countof(D9DriverFilters) - 1)];
    if (Type == D3DDDITSS_MIPFILTER)
        return D9DriverMipFilters[D9Clamp(Value, _countof(D9DriverMipFilters) - 1)];
    return Value;
}

STDMETHODIMP
D9Device::GetTextureStageState(
    _In_ DWORD Stage,
    _In_ D3DTEXTURESTAGESTATETYPE Type,
    _Out_ DWORD* Value)
{
    D9_DEVICE_LOCK();
    UINT Kept = D9Clamp(Stage, D9StageCount - 1);
    UINT Slot = D9Clamp(Type, D9StageStateCount - 1);

    *Value = StageStates[Kept][Slot];
    return S_OK;
}

STDMETHODIMP
D9Device::SetTextureStageState(
    _In_ DWORD Stage,
    _In_ D3DTEXTURESTAGESTATETYPE Type,
    _In_ DWORD Value)
{
    D9_DEVICE_LOCK();
    UINT Kept = D9Clamp(Stage, D9StageCount - 1);
    UINT Slot = D9Clamp(Type, D9StageStateCount - 1);

    if (!KeepsNoState())
    {
        if (StageStates[Kept][Slot] == Value)
            return S_OK;
        StageStates[Kept][Slot] = Value;
    }
    SendStageValue(Kept, Slot, Value);
    return S_OK;
}

/* A sampler number the runtime accepts, or the first sampler for one it does not */
static
UINT
D9AcceptedSampler(
    _In_ DWORD Sampler)
{
    DWORD Slot = Sampler >= D3DDMAPSAMPLER ? Sampler - D9SamplerSlotShift : Sampler;

    return Slot < D9TextureSlotCount ? Sampler : 0;
}

static
UINT
D9SamplerSlot(
    _In_ UINT Sampler)
{
    return Sampler >= D3DDMAPSAMPLER ? Sampler - D9SamplerSlotShift : Sampler;
}

STDMETHODIMP
D9Device::GetSamplerState(
    _In_ DWORD Sampler,
    _In_ D3DSAMPLERSTATETYPE Type,
    _Out_ DWORD* Value)
{
    D9_DEVICE_LOCK();
    UINT Slot = D9SamplerSlot(D9AcceptedSampler(Sampler));

    if (static_cast<UINT>(Type) >= D9SamplerStateCount)
        return D3DERR_INVALIDCALL;
    *Value = SamplerStates[Slot][Type];
    return S_OK;
}

STDMETHODIMP
D9Device::SetSamplerState(
    _In_ DWORD Sampler,
    _In_ D3DSAMPLERSTATETYPE Type,
    _In_ DWORD Value)
{
    D9_DEVICE_LOCK();
    UINT Accepted = D9AcceptedSampler(Sampler);
    UINT Slot = D9SamplerSlot(Accepted);

    if (static_cast<UINT>(Type) >= D9SamplerStateCount)
        return D3DERR_INVALIDCALL;
    if (!KeepsNoState())
    {
        if (SamplerStates[Slot][Type] == Value)
            return S_OK;
        SamplerStates[Slot][Type] = Value;
    }
    SendSamplerValue(Accepted, Type, Value);
    return S_OK;
}
