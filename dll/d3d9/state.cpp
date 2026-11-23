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

/* The retired render state that carries the N patch segment count */
static constexpr UINT D9PatchSegmentsState = 164;

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

    /* Vertex fog turning on or off makes a device that keeps state bind its declaration again */
    if ((State == D3DRS_FOGENABLE || State == D3DRS_FOGVERTEXMODE) && (BehaviorFlags & D3DCREATE_PUREDEVICE) == 0)
    {
        BOOLEAN Fogged = RenderStates[D3DRS_FOGENABLE] != 0 && RenderStates[D3DRS_FOGVERTEXMODE] != 0;

        if (Fogged != VertexFogged)
        {
            VertexFogged = Fogged;
            DrawDirty |= D9DrawVertexInput;
        }
    }

    /* The vertex converter sees every state first; a wrap state also follows the stages that read its set */
    if (Switch.HasVertexConverter)
    {
        if (State >= D3DRS_WRAP0 && State <= D3DRS_WRAP7)
        {
            UINT Set = State - D3DRS_WRAP0;

            Switch.ApiWrap[Set] = Value;
            if (Switch.VertexConverting && (Switch.Allowed & 1) != 0)
            {
                for (UINT Stage = 0; Stage < D9VertexStages; Stage++)
                {
                    if ((Switch.RenumberedStages & (1u << Stage)) == 0 || (Switch.ApiCoordIndex[Stage] & 7) != Set ||
                        Switch.DriverWrap[Stage] == Value)
                    {
                        continue;
                    }
                    Args.State = static_cast<D3DDDIRENDERSTATETYPE>(D3DRS_WRAP0 + Stage);
                    Args.Value = Value;
                    DdiRenderState(&Args);
                    Switch.DriverWrap[Stage] = Value;
                }
            }
            Switch.DriverWrap[Set] = Value;
        }
        VertexShaders.SetRenderState(State, Value);
    }

    if (SoftwareVertices && D9ListHolds(D9SoftwareOnlyStates, _countof(D9SoftwareOnlyStates), State))
        return;
    if (!SoftwareVertices && D9ListHolds(D9ShaderPipelineStates, _countof(D9ShaderPipelineStates), State))
        return;

    /* Point sprites change the coordinates the converters read */
    if (State == D3DRS_POINTSPRITEENABLE)
        UpdateSwitch(D9SwitchPointSize, 0);

    /* The pixel shaders standing in for the fixed function stages take these two */
    if (State == D3DRS_SPECULARENABLE)
    {
        if (Switch.HasPixelConverter)
            PixelShaders.SetSpecular(Value != 0);
        return;
    }
    if (State == D3DRS_TEXTUREFACTOR)
    {
        if (Switch.HasPixelConverter)
            PixelShaders.SetFactor(Value);
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

    /* A block takes neither retired states nor the device's own copy */
    if (Recording != nullptr)
    {
        if (D9IsRetiredState(Slot))
            return D3DERR_INVALIDCALL;
        return Recording->Add(D9BlockOp::RenderState, Slot, 0, &Value, sizeof(Value));
    }

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

    /* The vertex converter may take the state for itself or hand the driver its own value */
    if (Switch.HasVertexConverter && Stage < D9VertexStages && FeedVertexStage(Stage, Type, &Value))
        return;

    /* A new count of active stages changes the coordinates the vertex converter writes */
    if (Switch.HasPixelConverter && Stage < D9FixedStages && PixelShaders.SetStage(Stage, Type, Value))
        UpdateSwitch(D9SwitchPixelShaderUse, 0);

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
    if (Pass && Switch.HasPixelConverter && Switch.PixelMode == 0)
        Pass = FALSE;
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

    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::StageState, Kept, Slot, &Value, sizeof(Value));
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
    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::SamplerState, Accepted, Type, &Value, sizeof(Value));
    if (!KeepsNoState())
    {
        if (SamplerStates[Slot][Type] == Value)
            return S_OK;
        SamplerStates[Slot][Type] = Value;
    }
    SendSamplerValue(Accepted, Type, Value);
    return S_OK;
}

/* Where a transform is kept, or -1 for a type the runtime does not keep */
static
INT
D9TransformSlot(
    _In_ DWORD Type)
{
    if (Type == D3DTS_VIEW)
        return 0;
    if (Type == D3DTS_PROJECTION)
        return 1;
    if (Type >= D3DTS_TEXTURE0 && Type <= D3DTS_TEXTURE7)
        return 2 + static_cast<INT>(Type - D3DTS_TEXTURE0);
    if (Type >= D3DTS_WORLDMATRIX(0) && Type <= D3DTS_WORLDMATRIX(255))
        return 10 + static_cast<INT>(Type - D3DTS_WORLDMATRIX(0));
    return -1;
}

/* Left times right, each sum taken in the order the system's own product takes it */
static
VOID
D9MultiplyMatrices(
    _Out_ D3DMATRIX* Result,
    _In_ const D3DMATRIX* Left,
    _In_ const D3DMATRIX* Right)
{
    for (UINT Row = 0; Row < 4; Row++)
    {
        for (UINT Column = 0; Column < 4; Column++)
        {
            FLOAT Sum = Left->m[Row][1] * Right->m[1][Column];

            Sum += Left->m[Row][0] * Right->m[0][Column];
            Sum += Left->m[Row][2] * Right->m[2][Column];
            Sum += Left->m[Row][3] * Right->m[3][Column];
            Result->m[Row][Column] = Sum;
        }
    }
}

/* The range of W the projection gives, which the driver needs for W buffering */
VOID
D9Device::SendWInfo(
    _In_ const D3DMATRIX* Projection)
{
    D3DDDIARG_WINFO Info = {};
    FLOAT ZScale = Projection->_33;
    FLOAT WScale = Projection->_34;
    FLOAT ZOffset = Projection->_43;
    FLOAT WOffset = Projection->_44;

    if (ZScale == WScale || ZScale == 0.0f)
        return;
    Info.WNear = WOffset - (ZOffset / ZScale) * WScale;
    Info.WFar = ((WOffset - ZOffset) / (ZScale - WScale)) * WScale + WOffset;
    DdiWInfo(&Info);
}

STDMETHODIMP
D9Device::SetTransform(
    _In_ D3DTRANSFORMSTATETYPE State,
    _In_opt_ const D3DMATRIX* Matrix)
{
    D9_DEVICE_LOCK();
    DWORD Type = D9Clamp(State, D3DTS_WORLDMATRIX(255));
    INT Slot = D9TransformSlot(Type);

    if (Matrix == nullptr)
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::Transform, Type, 0, Matrix, sizeof(*Matrix));

    /* Types the runtime does not keep are accepted and dropped */
    if (Slot >= 0)
        Transforms[Slot] = *Matrix;
    if (Switch.HasVertexConverter)
        VertexShaders.SetTransform(Type, Matrix);
    if (Type == D3DTS_PROJECTION)
        SendWInfo(Matrix);
    return S_OK;
}

STDMETHODIMP
D9Device::GetTransform(
    _In_ D3DTRANSFORMSTATETYPE State,
    _Out_ D3DMATRIX* Matrix)
{
    D9_DEVICE_LOCK();
    INT Slot = D9TransformSlot(State);

    if (Slot < 0 || Matrix == nullptr)
        return D3DERR_INVALIDCALL;
    *Matrix = Transforms[Slot];
    return S_OK;
}

STDMETHODIMP
D9Device::MultiplyTransform(
    _In_ D3DTRANSFORMSTATETYPE State,
    _In_opt_ const D3DMATRIX* Matrix)
{
    D9_DEVICE_LOCK();
    INT Slot = D9TransformSlot(State);
    D3DMATRIX Product;

    if (Slot < 0 || Matrix == nullptr)
        return D3DERR_INVALIDCALL;

    /* The matrix given is applied before the one the device holds */
    D9MultiplyMatrices(&Product, Matrix, &Transforms[Slot]);
    Transforms[Slot] = Product;
    if (State == D3DTS_PROJECTION)
        SendWInfo(&Product);
    return S_OK;
}

STDMETHODIMP
D9Device::SetViewport(
    _In_opt_ const D3DVIEWPORT9* Viewport)
{
    D9_DEVICE_LOCK();
    D3DDDIARG_VIEWPORTINFO Area = {};
    D3DDDIARG_ZRANGE Range = {};
    D3DVIEWPORT9* Kept = &CurrentViewport;

    if (Viewport == nullptr)
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::Viewport, 0, 0, Viewport, sizeof(*Viewport));

    /* A depth range thinner than a thousandth is widened away from the nearer end of the unit range */
    *Kept = *Viewport;
    if (Switch.HasVertexConverter)
        VertexShaders.SetViewportSize(Kept->Width, Kept->Height);
    if (!KeepsNoState() && Kept->MaxZ - Kept->MinZ < 0.001f)
    {
        if (Kept->MaxZ < 0.5f)
            Kept->MaxZ = Kept->MinZ + 0.001f;
        else
            Kept->MinZ = Kept->MaxZ - 0.001f;
    }

    /* A new first target's viewport is still on its way; this one goes with it */
    if (ViewportPending)
        return S_OK;
    Area.X = Kept->X;
    Area.Y = Kept->Y;
    Area.Width = Kept->Width;
    Area.Height = Kept->Height;
    Range.MinZ = Kept->MinZ;
    Range.MaxZ = Kept->MaxZ;
    DdiViewport(&Area);
    DdiZRange(&Range);
    return S_OK;
}

STDMETHODIMP
D9Device::GetViewport(
    _Out_ D3DVIEWPORT9* Viewport)
{
    D9_DEVICE_LOCK();

    if (Viewport == nullptr)
        return D3DERR_INVALIDCALL;
    *Viewport = CurrentViewport;
    return S_OK;
}

STDMETHODIMP
D9Device::SetScissorRect(
    _In_opt_ const RECT* Rect)
{
    D9_DEVICE_LOCK();

    if (Rect == nullptr)
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::Scissor, 0, 0, Rect, sizeof(*Rect));
    ScissorRect = *Rect;
    if (!ViewportPending)
        DdiScissorRect(&ScissorRect);
    return S_OK;
}

STDMETHODIMP
D9Device::GetScissorRect(
    _Out_ RECT* Rect)
{
    D9_DEVICE_LOCK();

    if (Rect == nullptr)
        return D3DERR_INVALIDCALL;
    *Rect = ScissorRect;
    return S_OK;
}

/* The fixed function vertex pipeline takes the material; the driver never sees it */
STDMETHODIMP
D9Device::SetMaterial(
    _In_opt_ const D3DMATERIAL9* Material)
{
    D9_DEVICE_LOCK();

    if (Material == nullptr)
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::Material, 0, 0, Material, sizeof(*Material));
    CurrentMaterial = *Material;
    if (Switch.HasVertexConverter)
        VertexShaders.SetMaterial(Material);
    return S_OK;
}

STDMETHODIMP
D9Device::GetMaterial(
    _Out_ D3DMATERIAL9* Material)
{
    D9_DEVICE_LOCK();

    if (Material == nullptr)
        return D3DERR_INVALIDCALL;
    *Material = CurrentMaterial;
    return S_OK;
}

/* A light the application has set, or null when the index never was */
static
D9Light*
D9FindLight(
    _In_reads_opt_(Capacity) D9Light* Lights,
    _In_ UINT Capacity,
    _In_ DWORD Index)
{
    if (Index >= Capacity || !Lights[Index].Made)
        return nullptr;
    return &Lights[Index];
}

STDMETHODIMP
D9Device::SetLight(
    _In_ DWORD Index,
    _In_opt_ const D3DLIGHT9* Light)
{
    D9_DEVICE_LOCK();

    if (Light == nullptr)
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::Light, Index, 0, Light, sizeof(*Light));

    /* The table grows past the index so that nearby lights fit without growing it again */
    if (Index >= LightCapacity)
    {
        UINT Capacity = Index + 32;
        D9Light* Grown;

        if (Capacity < Index)
            return E_OUTOFMEMORY;
        Grown = new D9Light[Capacity];
        if (Grown == nullptr)
        {
            D9Trace(L"SetLight: no memory for %u lights", Capacity);
            return E_OUTOFMEMORY;
        }
        ZeroMemory(Grown, Capacity * sizeof(*Grown));
        if (Lights != nullptr)
            CopyMemory(Grown, Lights, LightCapacity * sizeof(*Lights));
        delete[] Lights;
        Lights = Grown;
        LightCapacity = Capacity;
    }

    Lights[Index].Made = TRUE;
    Lights[Index].Light = *Light;
    if (Switch.HasVertexConverter && !VertexShaders.SetLight(Index, Light))
        D9_TRACE_ONCE(L"SetLight: the vertex converter has no room for another light");
    return S_OK;
}

STDMETHODIMP
D9Device::GetLight(
    _In_ DWORD Index,
    _Out_ D3DLIGHT9* Light)
{
    D9_DEVICE_LOCK();
    const D9Light* Found = D9FindLight(Lights, LightCapacity, Index);

    if (Found == nullptr || Light == nullptr)
        return D3DERR_INVALIDCALL;
    *Light = Found->Light;
    return S_OK;
}

STDMETHODIMP
D9Device::LightEnable(
    _In_ DWORD Index,
    _In_ BOOL Enable)
{
    D9_DEVICE_LOCK();
    D9Light* Found = D9FindLight(Lights, LightCapacity, Index);
    BOOL Enabled = Enable != FALSE;

    /* Turning on a light never set gives the device a white directional light pointing along Z first, even
       while a block records */
    if (Found == nullptr)
    {
        D9StateBlock* Block = Recording;
        D3DLIGHT9 White = {};
        HRESULT hr;

        White.Type = D3DLIGHT_DIRECTIONAL;
        White.Diffuse.r = 1.0f;
        White.Diffuse.g = 1.0f;
        White.Diffuse.b = 1.0f;
        White.Direction.z = 1.0f;
        Recording = nullptr;
        hr = SetLight(Index, &White);
        Recording = Block;
        if (FAILED(hr))
            return hr;
        Found = &Lights[Index];
    }
    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::LightEnable, Index, 0, &Enabled, sizeof(Enabled));
    Found->Enabled = Enable != FALSE;
    if (Switch.HasVertexConverter && !VertexShaders.EnableLight(Index, Found->Enabled))
        D9_TRACE_ONCE(L"LightEnable: the vertex converter has no room for another light");
    return S_OK;
}

/* An enabled light reads back as the runtime's own flag for it rather than TRUE */
STDMETHODIMP
D9Device::GetLightEnable(
    _In_ DWORD Index,
    _Out_ BOOL* Enable)
{
    D9_DEVICE_LOCK();
    static constexpr BOOL D9LightOnFlag = 0x80;
    const D9Light* Found = D9FindLight(Lights, LightCapacity, Index);

    if (Found == nullptr || Enable == nullptr)
        return D3DERR_INVALIDCALL;
    *Enable = Found->Enabled ? D9LightOnFlag : 0;
    return S_OK;
}

/* A clip plane reaches the driver more than once while the runtime's vertex shaders stand in for the fixed
   function pipeline, and not at all while the runtime processes vertices itself */
VOID
D9Device::SendClipPlane(
    _In_ UINT Index,
    _In_ UINT Repeats)
{
    D3DDDIARG_SETCLIPPLANE Args = {};

    if (SoftwareVertices)
        return;
    Args.Index = Index;
    CopyMemory(Args.Plane, ClipPlanes[Index], sizeof(Args.Plane));
    for (UINT Sent = 0; Sent < Repeats; Sent++)
        DdiClipPlane(&Args);
}

/* Indices past the device's planes land on its last one */
static
UINT
D9ClipPlaneSlot(
    _In_ DWORD Index,
    _In_ DWORD Planes)
{
    return D9Clamp(Index, Planes != 0 ? Planes - 1 : 0);
}

STDMETHODIMP
D9Device::SetClipPlane(
    _In_ DWORD Index,
    _In_opt_ const float* Plane)
{
    D9_DEVICE_LOCK();
    UINT Slot = D9ClipPlaneSlot(Index, Caps.MaxUserClipPlanes);

    if (Plane == nullptr)
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::ClipPlane, Slot, 0, Plane, sizeof(ClipPlanes[Slot]));
    /* A mixed device's driver sees the plane from both of the runtime's vertex pipelines */
    CopyMemory(ClipPlanes[Slot], Plane, sizeof(ClipPlanes[Slot]));
    SendClipPlane(Slot, (BehaviorFlags & D3DCREATE_MIXED_VERTEXPROCESSING) != 0 ? 4 : 2);
    return S_OK;
}

STDMETHODIMP
D9Device::GetClipPlane(
    _In_ DWORD Index,
    _Out_ float* Plane)
{
    D9_DEVICE_LOCK();
    UINT Slot = D9ClipPlaneSlot(Index, Caps.MaxUserClipPlanes);

    if (Plane == nullptr)
        return D3DERR_INVALIDCALL;
    CopyMemory(Plane, ClipPlanes[Slot], sizeof(ClipPlanes[Slot]));
    return S_OK;
}

STDMETHODIMP
D9Device::SetClipStatus(
    _In_opt_ const D3DCLIPSTATUS9* ClipStatus)
{
    D9_DEVICE_LOCK();

    if (ClipStatus == nullptr)
        return D3DERR_INVALIDCALL;
    CurrentClipStatus = *ClipStatus;
    return S_OK;
}

STDMETHODIMP
D9Device::GetClipStatus(
    _Out_ D3DCLIPSTATUS9* ClipStatus)
{
    D9_DEVICE_LOCK();

    if (ClipStatus == nullptr)
        return D3DERR_INVALIDCALL;
    *ClipStatus = CurrentClipStatus;
    return S_OK;
}

/* The segment count reaches the driver as the retired patch segments render state, every time */
STDMETHODIMP
D9Device::SetNPatchMode(
    _In_ FLOAT NSegments)
{
    D9_DEVICE_LOCK();
    D3DDDIARG_RENDERSTATE Args = {};

    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::NPatchMode, 0, 0, &NSegments, sizeof(NSegments));
    PatchSegments = NSegments;
    Args.State = static_cast<D3DDDIRENDERSTATETYPE>(D9PatchSegmentsState);
    CopyMemory(&Args.Value, &NSegments, sizeof(Args.Value));
    DdiRenderState(&Args);
    return S_OK;
}

STDMETHODIMP_(float)
D9Device::GetNPatchMode()
{
    D9_DEVICE_LOCK();

    return PatchSegments;
}

/* Back on the hardware pipeline the driver learns what the software pipeline kept from it */
VOID
D9Device::SwitchVertexProcessing(
    _In_ BOOLEAN Software)
{
    SoftwareVertices = Software;
    if (Software)
        return;

    if (FrequenciesPending)
    {
        for (UINT Stream = 0; Stream < Caps.MaxStreams; Stream++)
            SendStreamFrequency(Stream);
        FrequenciesPending = FALSE;
    }
    for (BYTE State : D9SoftwareOnlyStates)
        SendStateValue(State, RenderStates[State]);
    for (UINT Plane = 0; Plane < Caps.MaxUserClipPlanes; Plane++)
        SendClipPlane(Plane, 2);
}

STDMETHODIMP
D9Device::SetSoftwareVertexProcessing(
    _In_ BOOL Software)
{
    D9_DEVICE_LOCK();

    if ((BehaviorFlags & D3DCREATE_HARDWARE_VERTEXPROCESSING) != 0)
        return Software ? D3DERR_INVALIDCALL : S_OK;
    if ((BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) != 0)
        return Software ? S_OK : D3DERR_INVALIDCALL;
    if ((Software != FALSE) != (SoftwareVertices != FALSE))
        SwitchVertexProcessing(Software != FALSE);
    return S_OK;
}

STDMETHODIMP_(BOOL)
D9Device::GetSoftwareVertexProcessing()
{
    D9_DEVICE_LOCK();

    return SoftwareVertices;
}

/* A format binds a declaration the device makes for it; the driver sees it when something draws */
STDMETHODIMP
D9Device::SetFVF(
    _In_ DWORD FVF)
{
    D9_DEVICE_LOCK();

    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::Fvf, 0, 0, &FVF, sizeof(FVF));
    CurrentFvf = FVF;
    if (FVF == ApiFvf)
        return S_OK;
    ApiFvf = FVF;
    LastFvf = FVF;
    DrawDirty |= D9DrawVertexInput;
    return BindFvfDeclaration(FVF);
}

STDMETHODIMP
D9Device::GetFVF(
    _Out_ DWORD* FVF)
{
    D9_DEVICE_LOCK();

    if (FVF == nullptr)
        return D3DERR_INVALIDCALL;
    *FVF = CurrentFvf;
    return S_OK;
}

/* Buffers reach the driver when something draws from them */
STDMETHODIMP
D9Device::SetStreamSource(
    _In_ UINT StreamNumber,
    _In_opt_ IDirect3DVertexBuffer9* StreamData,
    _In_ UINT OffsetInBytes,
    _In_ UINT Stride)
{
    D9_DEVICE_LOCK();
    D9Stream* Stream;

    if (StreamNumber >= D9StreamCount)
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
    {
        D9BlockStream Bound = { StreamData, OffsetInBytes, Stride };

        return Recording->Add(D9BlockOp::StreamSource, StreamNumber, 0, &Bound, sizeof(Bound));
    }

    Stream = &Streams[StreamNumber];
    if (Stream->Buffer == StreamData && Stream->Offset == OffsetInBytes && Stream->Stride == Stride)
        return S_OK;

    /* Taking a buffer away leaves the offset and stride it had, and the driver keeps what it was given */
    if (StreamData != nullptr)
    {
        static_cast<D9VertexBuffer*>(StreamData)->Bind();
        Stream->Offset = OffsetInBytes;
        Stream->Stride = Stride;
        StreamDirty |= 1u << StreamNumber;
        DrawDirty |= D9DrawStreams;
    }
    if (Stream->Buffer != nullptr)
        static_cast<D9VertexBuffer*>(Stream->Buffer)->Unbind();
    Stream->Buffer = StreamData;
    return S_OK;
}

STDMETHODIMP
D9Device::GetStreamSource(
    _In_ UINT StreamNumber,
    _Out_ IDirect3DVertexBuffer9** StreamData,
    _Out_ UINT* OffsetInBytes,
    _Out_ UINT* Stride)
{
    D9_DEVICE_LOCK();
    const D9Stream* Stream = &Streams[D9Clamp(StreamNumber, D9StreamCount - 1)];

    if (StreamData == nullptr || OffsetInBytes == nullptr || Stride == nullptr)
        return D3DERR_INVALIDCALL;
    *StreamData = Stream->Buffer;
    if (Stream->Buffer != nullptr)
        Stream->Buffer->AddRef();
    *OffsetInBytes = Stream->Offset;
    *Stride = Stream->Stride;
    return S_OK;
}

VOID
D9Device::SendStreamFrequency(
    _In_ UINT Stream)
{
    D3DDDIARG_SETSTREAMSOURCEFREQ Args = {};

    Args.Stream = Stream;
    Args.Divider = Streams[Stream].Frequency;
    DdiStreamSourceFreq(&Args);
}

/* The software pipeline keeps frequencies to itself until the device goes back to hardware */
STDMETHODIMP
D9Device::SetStreamSourceFreq(
    _In_ UINT StreamNumber,
    _In_ UINT Setting)
{
    D9_DEVICE_LOCK();
    static constexpr UINT Indexed = D3DSTREAMSOURCE_INDEXEDDATA;
    static constexpr UINT Instanced = static_cast<UINT>(D3DSTREAMSOURCE_INSTANCEDATA);
    UINT Stream = D9Clamp(StreamNumber, D9StreamCount - 1);
    UINT Kind = Setting & (Indexed | Instanced);

    if (Setting == 0 || Kind == (Indexed | Instanced) || (Kind == Instanced && Stream == 0))
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::StreamFrequency, Stream, 0, &Setting, sizeof(Setting));

    Streams[Stream].Frequency = Setting;
    if (SoftwareVertices)
    {
        FrequenciesPending = TRUE;
        return S_OK;
    }
    SendStreamFrequency(Stream);
    return S_OK;
}

STDMETHODIMP
D9Device::GetStreamSourceFreq(
    _In_ UINT StreamNumber,
    _Out_ UINT* Setting)
{
    D9_DEVICE_LOCK();

    if (Setting == nullptr)
        return D3DERR_INVALIDCALL;
    *Setting = Streams[D9Clamp(StreamNumber, D9StreamCount - 1)].Frequency;
    return S_OK;
}

STDMETHODIMP
D9Device::SetIndices(
    _In_opt_ IDirect3DIndexBuffer9* IndexData)
{
    D9_DEVICE_LOCK();

    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::Indices, 0, 0, &IndexData, sizeof(IndexData));
    if (Indices == IndexData)
        return S_OK;

    /* As with streams, the driver keeps an index buffer taken away */
    if (IndexData != nullptr)
    {
        static_cast<D9IndexBuffer*>(IndexData)->Bind();
        IndexStride = static_cast<D9IndexBuffer*>(IndexData)->IndexSize();
        StreamDirty |= D9IndexStreamBit;
        DrawDirty |= D9DrawStreams;
    }
    if (Indices != nullptr)
        static_cast<D9IndexBuffer*>(Indices)->Unbind();
    Indices = IndexData;
    return S_OK;
}

STDMETHODIMP
D9Device::GetIndices(
    _Out_ IDirect3DIndexBuffer9** IndexData)
{
    D9_DEVICE_LOCK();

    if (IndexData == nullptr)
        return D3DERR_INVALIDCALL;
    *IndexData = Indices;
    if (Indices != nullptr)
        Indices->AddRef();
    return S_OK;
}

/* Lets go of every bound vertex and index buffer, destroying those the application already released */
VOID
D9Device::UnbindStreams(VOID)
{
    IDirect3DIndexBuffer9* Bound = Indices;

    for (D9Stream& Stream : Streams)
    {
        IDirect3DVertexBuffer9* Buffer = Stream.Buffer;

        Stream.Buffer = nullptr;
        if (Buffer != nullptr)
            static_cast<D9VertexBuffer*>(Buffer)->Unbind();
    }
    Indices = nullptr;
    if (Bound != nullptr)
        static_cast<D9IndexBuffer*>(Bound)->Unbind();
}
