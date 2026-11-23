/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Draws and the state they hand the driver first
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Masks the shader switch keeps the changes it acts on with, as each mode allows them */
static constexpr UINT D9AllowVertexConverter = 0x29BD;
static constexpr UINT D9AllowAfterVertexInit = 0xD64F;
static constexpr UINT D9AllowPixelConverter = 0x1642;
static constexpr UINT D9AllowWithoutPixelConverter = 0xE9BD;
static constexpr UINT D9AllowShaderInput = 0x29B5;
static constexpr UINT D9AllowTransformedInput = 0xD64A;
static constexpr UINT D9AllowConvertingVertices = 0x29B0;

/* Changes that pick the vertex and pixel modes, and those that feed the converters' inputs */
static constexpr UINT D9SwitchModeChanges = 0x063C;
static constexpr UINT D9SwitchInputChanges = 0x39C0;

/* The sampler a texture slot stands for: pixel samplers, the displacement map sampler, then vertex samplers */
static
UINT
D9SlotStage(
    _In_ UINT Slot)
{
    return Slot >= 16 ? Slot + (D3DDMAPSAMPLER - 16) : Slot;
}

/* What the fixed function stages and modes amount to for the switch, read as the switch keeps them */
static
UINT
D9SwitchModes(
    _In_ const D9ShaderSwitch* Switch)
{
    return (Switch->VertexConverting ? 1 : 0) | Switch->PixelMode | Switch->Pending;
}

/* The switch starts with the fixed function pipeline wanted on both sides */
VOID
D9Device::StartShaderSwitch(VOID)
{
    ZeroMemory(&Switch, sizeof(Switch));
    Switch.ApiPixelInputs = -1;

    if (Caps.VertexShaderVersion >= D3DVS_VERSION(2, 0))
    {
        Switch.HasVertexConverter = TRUE;
        Switch.ShaderInput = TRUE;
        Switch.Allowed = (D9AllowVertexConverter & D9AllowAfterVertexInit) | D9SwitchWantVertexConverter;
        Switch.Pending = D9SwitchWantVertexConverter;
    }
    Switch.Allowed &= D9AllowWithoutPixelConverter;

    if (Caps.PixelShaderVersion >= D3DPS_VERSION(2, 0))
    {
        Switch.HasPixelConverter = TRUE;
        Switch.PixelMode = 2;
        Switch.Allowed = (Switch.Allowed | D9AllowPixelConverter) & ~D9SwitchWantPixelConverter;
        Switch.Pending = (Switch.Pending | D9SwitchWantPixelConverter | D9SwitchPixelShaderUse) &
                         ~D9SwitchWantApiPixelShader;
    }
}

VOID
D9Device::UpdateSwitch(
    _In_ UINT Set,
    _In_ UINT Cleared)
{
    Switch.Pending = (Switch.Pending | Set) & ~Cleared;
}

/* The application's vertex shader reaches the driver unless the fixed function one is bound; a null one only
   matters to the driver when no converter stands in */
VOID
D9Device::SwitchVertexShader(
    _In_opt_ HANDLE Shader,
    _In_ UINT Inputs)
{
    BOOLEAN Send;

    if (Shader != NULL)
    {
        Switch.ApiVertexInputs = Inputs;
        Switch.ApiVertexShader = Shader;
        UpdateSwitch(D9SwitchWantApiVertexShader | D9SwitchPixelInput, D9SwitchWantVertexConverter);
        Send = !Switch.VertexConverting;
    }
    else
    {
        UpdateSwitch(D9SwitchWantVertexConverter, D9SwitchWantApiVertexShader);
        Send = !Switch.HasVertexConverter;
    }
    if (Send)
        DdiShaderConstants(D9BatchCall::VertexShaderFunc, 0, 0, &Shader, sizeof(Shader));
}

VOID
D9Device::SwitchPixelShader(
    _In_opt_ HANDLE Shader,
    _In_ INT Inputs)
{
    BOOLEAN Send;

    if (Shader != NULL)
    {
        Switch.ApiPixelShader = Shader;
        UpdateSwitch(D9SwitchWantApiPixelShader | D9SwitchPixelShaderUse, D9SwitchWantPixelConverter);
        Send = Switch.PixelMode == 0;
    }
    else
    {
        UpdateSwitch(D9SwitchWantPixelConverter | D9SwitchPixelShaderUse, D9SwitchWantApiPixelShader);
        Send = !Switch.HasPixelConverter;
        Inputs = -1;
    }
    Switch.ApiPixelInputs = Inputs;
    if (Send)
        DdiPixelShader(Shader);
}

/* Transformed vertices skip the vertex converter and feed the pixel converter directly; untransformed ones
   of a declaration feed the vertex converter */
VOID
D9Device::SwitchDeclaration(
    _In_ const D9Declaration* Bound)
{
    HANDLE Handle = Bound->DriverHandle;

    if (Bound->Transformed)
    {
        Switch.PixelFvf = Bound->Fvf;
        Switch.PixelDeclaration = Handle;
        Switch.PixelLayout = Bound->Fvf != 0 ? nullptr : Bound;
        UpdateSwitch(D9SwitchTransformedInput | D9SwitchPixelInput, D9SwitchShaderInput);
    }
    else
    {
        UpdateSwitch(D9SwitchShaderInput | D9SwitchVertexInput, D9SwitchTransformedInput);
    }
    Switch.InputFvf = 0;
    Switch.InputDeclaration = Handle;
    DdiVertexShaderDecl(Handle);
}

/* The application's vertex shader comes back: what the converter set on the driver goes back to the
   application's values first */
static
VOID
D9RestoreVertexConstants(
    _In_ UINT Floats,
    _In_ UINT Ints,
    _In_ UINT FloatLimit,
    _In_ UINT IntLimit,
    _Out_ UINT* FloatCount,
    _Out_ UINT* IntCount)
{
    *FloatCount = Floats > FloatLimit ? FloatLimit : Floats;
    *IntCount = Ints > IntLimit ? IntLimit : Ints;
}

/* Acts on the changes the current modes allow: which vertex and pixel shaders the driver gets, then the
   converters' inputs */
VOID
D9Device::ApplySwitch(VOID)
{
    UINT Acting = Switch.Pending & Switch.Allowed;

    if ((Acting & D9SwitchModeChanges) != 0)
    {
        if ((Acting & D9SwitchTransformedInput) != 0)
        {
            Switch.ShaderInput = FALSE;
            Switch.Allowed = (Switch.Allowed | D9SwitchShaderInput) & D9AllowTransformedInput;
            Switch.Pending &= ~D9SwitchTransformedInput;
            Acting = Switch.Pending & Switch.Allowed;
        }
        else if ((Acting & D9SwitchShaderInput) != 0)
        {
            Switch.ShaderInput = TRUE;
            Switch.Allowed = (Switch.Allowed | D9AllowShaderInput) & ~D9SwitchShaderInput;
            Switch.Pending &= ~D9SwitchShaderInput;
            Acting = Switch.Pending & Switch.Allowed;
        }

        if ((Acting & D9SwitchWantVertexConverter) != 0)
        {
            Switch.VertexConverting = TRUE;
            Switch.Allowed = (Switch.Allowed | D9AllowConvertingVertices) & ~D9SwitchWantVertexConverter;
            Switch.Pending &= ~D9SwitchWantVertexConverter;

            /* The converter's registers reach the driver again, then the stages read the outputs of their own
               numbers */
            if (VertexShaders.Mark != 0)
            {
                UINT Count = VertexShaders.Mark < Limits.DriverVertexFloats ? VertexShaders.Mark
                                                                             : Limits.DriverVertexFloats;

                DdiShaderConstants(D9BatchCall::VertexShaderConst, 0, Count, VertexShaders.Registers,
                                   Count * sizeof(VertexShaders.Registers[0]));
            }
            DdiShaderConstants(D9BatchCall::VertexShaderConstI, 0, 3, VertexShaders.Loops,
                               sizeof(VertexShaders.Loops));
            RenumberStages(TRUE);
            VertexShaders.Dirty = TRUE;
            Acting = Switch.Pending & Switch.Allowed;
        }
        else if ((Acting & D9SwitchWantApiVertexShader) != 0)
        {
            UINT Floats;
            UINT Ints;

            Switch.VertexConverting = FALSE;
            Switch.Pending &= ~(D9SwitchWantVertexConverter | D9SwitchWantApiVertexShader);
            RenumberStages(FALSE);
            Switch.Allowed = (Switch.Allowed & D9AllowAfterVertexInit) | D9SwitchWantVertexConverter;
            D9RestoreVertexConstants(Switch.VertexFloatsUsed, Switch.VertexIntsUsed, Limits.VertexFloats,
                                     Limits.VertexInts, &Floats, &Ints);
            if (Floats != 0)
                DdiShaderConstants(D9BatchCall::VertexShaderConst, 0, Floats, VertexFloats, Floats * sizeof(*VertexFloats));
            if (Ints != 0)
                DdiShaderConstants(D9BatchCall::VertexShaderConstI, 0, Ints, VertexInts, Ints * sizeof(*VertexInts));
            DdiShaderConstants(D9BatchCall::VertexShaderFunc, 0, 0, &Switch.ApiVertexShader,
                               sizeof(Switch.ApiVertexShader));
            Acting = Switch.Pending & Switch.Allowed;
        }

        if ((Acting & D9SwitchWantPixelConverter) != 0)
        {
            D3DDDIARG_SETPIXELSHADERCONST Restore = {};

            /* The converter's constants come back and its shader is bound again at the next draw */
            Switch.PixelMode = 2;
            Switch.Pending &= ~D9SwitchWantPixelConverter;
            Switch.Allowed = (Switch.Allowed | D9AllowPixelConverter) & ~D9SwitchWantPixelConverter;
            Restore.Count = PixelShaders.ConstantMark;
            DdiPixelShaderConst(&Restore, &PixelShaders.Constants[0][0]);
            Switch.ConverterBound = NULL;
            PixelShaders.Dirty = TRUE;
        }
        else if ((Acting & D9SwitchWantApiPixelShader) != 0)
        {
            D3DDDIARG_SETPIXELSHADERCONST Restore = {};

            Switch.PixelMode = 0;
            Switch.Pending &= ~D9SwitchWantApiPixelShader;
            Switch.Allowed = (Switch.Allowed & D9AllowWithoutPixelConverter) | D9SwitchWantPixelConverter;
            if (Switch.PixelConstantMark != 0)
            {
                Restore.Count = Switch.PixelConstantMark;
                DdiPixelShaderConst(&Restore, &PixelFloats[0][0]);
            }
            DdiPixelShader(Switch.ApiPixelShader);
        }
    }

    /* What the converters read changed; they take it when they next make a shader */
    Acting = Switch.Pending & Switch.Allowed;
    if ((Acting & D9SwitchInputChanges) == 0)
        return;
    if ((Acting & D9SwitchPointList) != 0 && Switch.HasVertexConverter)
        VertexShaders.SetPointList(Switch.DrewPoints);
    if ((Acting & D9SwitchPointSize) != 0)
        PixelShaders.SetPointSprite(Switch.DrewPoints && RenderStates[D3DRS_POINTSPRITEENABLE] != 0);
    if ((Acting & D9SwitchVertexInput) != 0 && Switch.HasVertexConverter && Declaration != nullptr)
    {
        if (Declaration->Fvf != 0)
            VertexShaders.SetFvf(Declaration->Fvf);
        else
            VertexShaders.SetElements(Declaration->Elements);
    }
    if ((Acting & D9SwitchPixelInput) != 0)
    {
        if (!Switch.ShaderInput)
        {
            if (Switch.PixelLayout != nullptr)
                PixelShaders.SetElements(Switch.PixelLayout->Elements);
            else
                PixelShaders.SetFvf(Switch.PixelFvf);
        }
        else if (!Switch.VertexConverting)
        {
            PixelShaders.SetFvf(Switch.ApiVertexInputs);
        }
        else
        {
            PixelShaders.SetFvf(VertexShaders.UsedKey.Outputs);
        }
    }
    Switch.Pending &= ~(Acting & D9SwitchInputChanges);
}

/* The fixed function pixel shader for the stages as they stand, made the first time they come up */
VOID
D9Device::PreparePixelShader(VOID)
{
    HANDLE Shader;
    HANDLE Evicted;

    if (!PixelShaders.Find(&Shader))
    {
        DWORD* Tokens = new (std::nothrow) DWORD[D9FixedPixelTokenLimit];
        UINT Count = 0;

        if (Tokens == nullptr)
            return;
        if (!D9BuildFixedPixelShader(&PixelShaders.Key, PixelShaders.StageZeroTextured, Tokens, &Count) ||
            FAILED(CreateDriverShader(TRUE, Tokens, Count * sizeof(*Tokens), &Shader)))
        {
            D9_TRACE_ONCE(L"a fixed function pixel shader could not be made");
            delete[] Tokens;
            return;
        }
        delete[] Tokens;
        if (!PixelShaders.Keep(Shader, &Evicted))
        {
            DestroyDriverShader(TRUE, Shader);
            return;
        }
        if (Evicted != NULL)
            DestroyDriverShader(TRUE, Evicted);
    }
    PixelShaders.Dirty = FALSE;
    if (Shader != Switch.ConverterBound)
    {
        DdiPixelShader(Shader);
        Switch.ConverterBound = Shader;
    }
}

/* The registers waiting for the driver go in the order the converter keeps its ranges */
VOID
D9Device::SendConverterRanges(VOID)
{
    UINT First;
    UINT Count;

    while (VertexShaders.TakeRange(&First, &Count))
    {
        if (First >= Limits.DriverVertexFloats)
            continue;
        if (Count > Limits.DriverVertexFloats - First)
            Count = Limits.DriverVertexFloats - First;
        DdiShaderConstants(D9BatchCall::VertexShaderConst, First, Count, VertexShaders.Registers[First],
                           Count * sizeof(VertexShaders.Registers[0]));
    }
}

/* The fixed function vertex shader for the inputs and states as they stand, made the first time they come up,
   then the registers it reads */
VOID
D9Device::PrepareVertexShader(VOID)
{
    HANDLE Shader;
    HANDLE Evicted;

    if (!VertexShaders.Dirty)
    {
        if (VertexShaders.Updated != VertexShaders.Stale)
            VertexShaders.UpdateRegisters();
        SendConverterRanges();
        return;
    }

    /* The lights in use are counted again; one light needs no loop registers */
    if ((VertexShaders.Stale & 0x400000) != 0)
    {
        VertexShaders.Stale = (VertexShaders.Stale | 0x2C00000) & ~0x400000u;
        VertexShaders.Key.Modes &= ~0x8u;
        for (UINT Kind = 0; Kind < 3; Kind++)
        {
            VertexShaders.Loops[Kind][0] = 0;
            VertexShaders.Loops[Kind][1] = 0;
            VertexShaders.Loops[Kind][2] = 7;
            VertexShaders.Loops[Kind][3] = 0;
        }
        DdiShaderConstants(D9BatchCall::VertexShaderConstI, 0, 3, VertexShaders.Loops, sizeof(VertexShaders.Loops));
    }

    if (VertexShaders.RefreshKey())
    {
        if (!VertexShaders.Find(&Shader))
        {
            DWORD* Tokens = new (std::nothrow) DWORD[D9FixedVertexTokenLimit];
            UINT Count = 0;

            if (Tokens == nullptr)
                return;
            if (!D9BuildFixedVertexShader(&VertexShaders.Key, VertexShaders.FlowControl, VertexShaders.Projected,
                                          Tokens, &Count) ||
                FAILED(CreateDriverShader(FALSE, Tokens, Count * sizeof(*Tokens), &Shader)))
            {
                D9_TRACE_ONCE(L"a fixed function vertex shader could not be made");
                delete[] Tokens;
                VertexShaders.Dirty = TRUE;
                return;
            }
            delete[] Tokens;
            if (!VertexShaders.Keep(Shader, &Evicted))
            {
                DestroyDriverShader(FALSE, Shader);
                return;
            }
            if (Evicted != NULL)
                DestroyDriverShader(FALSE, Evicted);
        }
    }
    if (VertexShaders.Stale != 0)
        VertexShaders.UpdateRegisters();
    SendConverterRanges();

    /* The shader goes to the driver each time the converter looked again; the pixel converter reads what it
       writes now, or at the next change of its inputs */
    Shader = VertexShaders.UsedShader;
    DdiShaderConstants(D9BatchCall::VertexShaderFunc, 0, 0, &Shader, sizeof(Shader));
    if (Switch.PixelMode != 0)
        PixelShaders.SetFvf(VertexShaders.UsedKey.Outputs);
    else
        UpdateSwitch(D9SwitchPixelInput, 0);
}

/* The shaders the vertex converter made go with the device, oldest first */
VOID
D9Device::ReleaseVertexShaders(VOID)
{
    HANDLE Shader;

    while (VertexShaders.TakeOldest(&Shader))
        DestroyDriverShader(FALSE, Shader);
}

/* While the converter runs each stage reads the coordinates of its own number, with the wrap of the set it
   reads; switching back hands the driver what the application set */
VOID
D9Device::RenumberStages(
    _In_ BOOLEAN Converting)
{
    D3DDDIARG_RENDERSTATE Wrap = {};

    for (UINT Stage = 0; Stage < D9VertexStages; Stage++)
    {
        DWORD Index;
        DWORD Value;

        if ((Switch.RenumberedStages & (1u << Stage)) == 0)
            continue;
        Index = Converting ? Stage : Switch.ApiCoordIndex[Stage];
        SendTextureStageState(Stage, D3DTSS_TEXCOORDINDEX, Index);
        if (Switch.HasPixelConverter)
            PixelShaders.SetStage(Stage, D3DTSS_TEXCOORDINDEX, Index);
        Value = Switch.ApiWrap[Converting ? (Switch.ApiCoordIndex[Stage] & 7) : Stage];
        if (Value != Switch.DriverWrap[Stage])
        {
            Wrap.State = static_cast<D3DDDIRENDERSTATETYPE>(D3DRS_WRAP0 + Stage);
            Wrap.Value = Value;
            DdiRenderState(&Wrap);
            Switch.DriverWrap[Stage] = Value;
        }
    }
    for (UINT Stage = 0; Stage < D9VertexStages; Stage++)
    {
        if ((Switch.TransformedStages & (1u << Stage)) != 0)
        {
            SendTextureStageState(Stage, D3DTSS_TEXTURETRANSFORMFLAGS,
                                  Converting ? (Switch.ApiTransformFlags[Stage] & D3DTTFF_PROJECTED)
                                             : Switch.ApiTransformFlags[Stage]);
        }
    }
}

/* The vertex converter's part of a stage state; while it runs the driver gets the stage's own coordinates and
   only the projection of a transform */
BOOLEAN
D9Device::FeedVertexStage(
    _In_ UINT Stage,
    _In_ UINT Type,
    _Inout_ DWORD* Value)
{
    if (Type != D3DTSS_COLOROP && Type != D3DTSS_TEXCOORDINDEX && Type != D3DTSS_TEXTURETRANSFORMFLAGS)
        return FALSE;
    VertexShaders.SetStage(Stage, Type, *Value);
    if (Type == D3DTSS_TEXCOORDINDEX)
    {
        Switch.ApiCoordIndex[Stage] = *Value;
        Switch.RenumberedStages &= ~(1u << Stage);
        if (*Value != Stage)
            Switch.RenumberedStages |= 1u << Stage;
    }
    else if (Type == D3DTSS_TEXTURETRANSFORMFLAGS)
    {
        Switch.ApiTransformFlags[Stage] = *Value;
        Switch.TransformedStages &= ~(1u << Stage);
        if ((*Value & ~D3DTTFF_PROJECTED) != 0)
            Switch.TransformedStages |= 1u << Stage;
    }
    if (!Switch.VertexConverting || (Switch.Allowed & 1) == 0)
        return FALSE;
    if (Type == D3DTSS_TEXTURETRANSFORMFLAGS)
    {
        *Value &= D3DTTFF_PROJECTED;
        return FALSE;
    }
    if (Type == D3DTSS_TEXCOORDINDEX)
    {
        DWORD Wrap = Switch.ApiWrap[*Value & 7];

        if (Switch.DriverWrap[Stage] != Wrap)
        {
            D3DDDIARG_RENDERSTATE Args = {};

            Args.State = static_cast<D3DDDIRENDERSTATETYPE>(D3DRS_WRAP0 + Stage);
            Args.Value = Wrap;
            DdiRenderState(&Args);
            Switch.DriverWrap[Stage] = Wrap;
        }
        *Value = Stage;
        return TRUE;
    }
    return FALSE;
}

/* The vertex converter starts from the device's default states, matrices and material */
VOID
D9Device::StartVertexConverter(VOID)
{
    D3DMATRIX Identity = {};

    if (!Switch.HasVertexConverter)
        return;
    VertexShaders.Start(Limits.DriverVertexFloats, Caps.VS20Caps.DynamicFlowControlDepth > 1,
                        (Caps.TextureCaps & D3DPTEXTURECAPS_PROJECTED) != 0);
    for (UINT State = 0; State < D9RenderStateCount; State++)
    {
        if (RenderStates[State] != D9UnsetState)
            VertexShaders.SetRenderState(State, RenderStates[State]);
    }
    for (UINT Stage = 0; Stage < D9VertexStages; Stage++)
    {
        VertexShaders.SetStage(Stage, D3DTSS_COLOROP, StageStates[Stage][D3DTSS_COLOROP]);
        VertexShaders.SetStage(Stage, D3DTSS_TEXCOORDINDEX, StageStates[Stage][D3DTSS_TEXCOORDINDEX]);
        VertexShaders.SetStage(Stage, D3DTSS_TEXTURETRANSFORMFLAGS, StageStates[Stage][D3DTSS_TEXTURETRANSFORMFLAGS]);
        Switch.ApiCoordIndex[Stage] = StageStates[Stage][D3DTSS_TEXCOORDINDEX];
        Switch.ApiTransformFlags[Stage] = StageStates[Stage][D3DTSS_TEXTURETRANSFORMFLAGS];
        Switch.ApiWrap[Stage] = RenderStates[D3DRS_WRAP0 + Stage];
        Switch.DriverWrap[Stage] = RenderStates[D3DRS_WRAP0 + Stage];
    }
    Switch.RenumberedStages = 0;
    Switch.TransformedStages = 0;
    Identity._11 = Identity._22 = Identity._33 = Identity._44 = 1.0f;
    for (UINT Stage = 0; Stage < D9VertexStages; Stage++)
        VertexShaders.SetTransform(D3DTS_TEXTURE0 + Stage, &Identity);
    VertexShaders.SetMaterial(&CurrentMaterial);
    VertexShaders.SetViewportSize(CurrentViewport.Width, CurrentViewport.Height);
}

/* The shaders the converter made go with the device, oldest first */
VOID
D9Device::ReleasePixelShaders(VOID)
{
    HANDLE Shader;

    while (PixelShaders.TakeOldest(&Shader))
        DestroyDriverShader(TRUE, Shader);
}

/* A stage's texture kind and format pick the sampler its shader declares */
VOID
D9Device::FeedPixelTexture(
    _In_ UINT Stage,
    _In_opt_ IDirect3DBaseTexture9* Texture)
{
    DWORD Kind = D9FixedTexNone;
    DWORD Format = 0;

    if (!Switch.HasPixelConverter || Stage >= D9FixedStages)
        return;
    if (Texture != nullptr)
    {
        const D9TextureCore* Core = D9CoreOf(Texture);

        Kind = Core->KindOf() == D9TextureKind::Cube ? D9FixedTexCube
                                                     : Core->KindOf() == D9TextureKind::Volume ? D9FixedTexVolume
                                                                                               : D9FixedTex2D;
        Format = Core->FormatOf();
    }
    PixelShaders.SetTexture(Stage, Kind, Format);
}

/* Moving between points and other primitives changes what the converters make; then the switch acts */
VOID
D9Device::SettleSwitch(
    _In_ D3DPRIMITIVETYPE PrimitiveType,
    _In_ BOOLEAN Indexed)
{
    BOOLEAN Points = !Indexed && PrimitiveType == D3DPT_POINTLIST;

    if ((D9SwitchModes(&Switch) & Switch.Allowed) == 0)
        return;
    if (Points != Switch.DrewPoints)
    {
        Switch.DrewPoints = Points;
        Switch.Pending |= D9SwitchPointSize | D9SwitchPointList;
    }
    if ((Switch.Pending & Switch.Allowed) != 0)
        ApplySwitch();
    if (Switch.VertexConverting)
        PrepareVertexShader();
    if (Switch.PixelMode != 0 && PixelShaders.Dirty)
        PreparePixelShader();
}

/* BeginScene binds every sampler and stream again at the next draw */
VOID
D9Device::MarkSceneState(VOID)
{
    BOOLEAN Displacement = (Caps.DevCaps2 & (D3DDEVCAPS2_DMAPNPATCH | D3DDEVCAPS2_PRESAMPLEDDMAPNPATCH)) != 0;

    TextureDirty = (1u << 16) - 1;
    if (Displacement)
        TextureDirty |= 1u << 16;
    TextureDirty |= ((1u << 4) - 1) << 17;
    StreamDirty = ((1u << D9StreamCount) - 1) | D9IndexStreamBit;
    DrawDirty |= D9DrawTextures | D9DrawStreams;
}

/* Only draws until the first one that passes are checked */
HRESULT
D9Device::CheckFirstDraw(
    _In_ BOOLEAN Indexed,
    _In_ BOOLEAN FromMemory)
{
    if (DrawChecked)
        return S_OK;
    if (!InScene)
    {
        D9Trace(L"a draw came outside BeginScene and EndScene");
        return D3DERR_INVALIDCALL;
    }
    if (Declaration == nullptr)
    {
        D9Trace(L"a draw came with no vertex declaration set");
        return D3DERR_INVALIDCALL;
    }
    if (LastFvf != 0 && (LastFvf & 1) == 0 && !FromMemory && Streams[0].Buffer == nullptr)
    {
        D9Trace(L"a draw came with no buffer on stream 0 for a format");
        return D3DERR_INVALIDCALL;
    }
    if (Indexed && !FromMemory && Indices == nullptr)
    {
        D9Trace(L"an indexed draw came with no index buffer set");
        return D3DERR_INVALIDCALL;
    }
    DrawChecked = TRUE;
    return S_OK;
}

/* The declaration and vertex shader reach the driver; a declaration made for a format gets its driver copy
   the first time it draws */
HRESULT
D9Device::FlushVertexInput(VOID)
{
    D9Declaration* Bound = Declaration;
    HRESULT hr;

    DrawDirty &= ~D9DrawVertexInput;
    if (Bound == nullptr)
        return D3DERR_INVALIDCALL;
    if (Bound->DriverHandle == NULL)
    {
        hr = DriverDeclaration(Bound, &Bound->DriverHandle);
        if (FAILED(hr))
            return hr;
    }

    SwitchDeclaration(Bound);
    if (Bound->Transformed)
        return S_OK;
    if (VertexShader != nullptr)
        SwitchVertexShader(VertexShader->DriverHandle, 0);
    else
        SwitchVertexShader(NULL, 0);
    return S_OK;
}

/* Samplers that changed get their textures again; a managed texture refreshes its video copy whether or not
   its sampler changed, and one made now is bound again */
VOID
D9Device::FlushTextures(VOID)
{
    BOOLEAN Displacement = (Caps.DevCaps2 & (D3DDEVCAPS2_DMAPNPATCH | D3DDEVCAPS2_PRESAMPLEDDMAPNPATCH)) != 0;

    DrawDirty &= ~D9DrawTextures;
    for (UINT Slot = 0; Slot < D9TextureSlotCount; Slot++)
    {
        IDirect3DBaseTexture9* Bound = Textures[Slot];
        UINT Bit = 1u << Slot;
        BOOLEAN Changed = (TextureDirty & Bit) != 0;
        D9Resource* Resource = nullptr;
        D9TextureCore* Core;
        BOOLEAN Made = FALSE;

        if (Slot == 16 && !Displacement)
            continue;
        TextureDirty &= ~Bit;
        if (Bound != nullptr)
        {
            Core = D9CoreOf(Bound);
            if (Core->PoolOf() != D3DPOOL_SCRATCH)
            {
                Resource = Core->DrawResource(&Made);
                Changed = Changed || Made;
            }
        }
        if (Changed)
        {
            FeedPixelTexture(Slot, Bound);
            DdiTexture(D9SlotStage(Slot), Resource != nullptr ? Resource->DriverResource : NULL);
        }
    }
}

/* Streams and the index binding that changed reach the driver; managed buffers refresh their video copies
   first and one made now is bound again */
VOID
D9Device::FlushStreams(VOID)
{
    DrawDirty &= ~D9DrawStreams;
    for (UINT Stream = 0; Stream <= D9StreamCount; Stream++)
    {
        BOOLEAN IsIndex = Stream == D9StreamCount;
        UINT Bit = IsIndex ? D9IndexStreamBit : 1u << Stream;
        BOOLEAN Changed = (StreamDirty & Bit) != 0;
        D9Resource* Resource = nullptr;
        BOOLEAN Made = FALSE;

        StreamDirty &= ~Bit;
        if (IsIndex && Indices != nullptr)
            Resource = static_cast<D9IndexBuffer*>(Indices)->DrawResource(&Made);
        else if (!IsIndex && Streams[Stream].Buffer != nullptr)
            Resource = static_cast<D9VertexBuffer*>(Streams[Stream].Buffer)->DrawResource(&Made);
        if (!Changed && !Made)
            continue;

        if (IsIndex)
        {
            D3DDDIARG_SETINDICES Args = {};

            Args.hIndexBuffer = Resource != nullptr ? Resource->DriverResource : NULL;
            Args.Stride = IndexStride;
            DdiIndices(&Args);
        }
        else
        {
            D3DDDIARG_SETSTREAMSOURCE Args = {};

            Args.Stream = Stream;
            Args.hVertexBuffer = Resource != nullptr ? Resource->DriverResource : NULL;
            Args.Offset = Streams[Stream].Offset;
            Args.Stride = Streams[Stream].Stride;
            DdiStreamSource(&Args);
        }
    }
}

/* Float pixel constants reach the driver while the application's pixel shader is bound; the highest register
   set is remembered so the application's values come back when its shader does */
VOID
D9Device::SendPixelFloats(
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_(Count * 4) const FLOAT* Data)
{
    D3DDDIARG_SETPIXELSHADERCONST Args = {};

    if (Switch.HasPixelConverter)
    {
        if (Switch.PixelConstantMark < Start + Count)
            Switch.PixelConstantMark = Start + Count;
        if (Switch.PixelMode != 0)
            return;
    }
    Args.Register = Start;
    Args.Count = Count;
    DdiPixelShaderConst(&Args, Data);
}

static
BOOLEAN
D9Defines(
    _In_reads_(Count) const USHORT* Defs,
    _In_ UINT Count,
    _In_ UINT Register)
{
    for (UINT Index = 0; Index < Count; Index++)
    {
        if (Defs[Index] == Register)
            return TRUE;
    }
    return FALSE;
}

/* The registers the old shader defined and the new one does not go back to the application's values, each
   run of them in one call */
VOID
D9Device::RestoreShaderDefs(
    _In_ BOOLEAN Pixel,
    _In_ const D9ShaderLayout* Old,
    _In_opt_ const D9ShaderLayout* New)
{
    static const D9BatchCall VertexCalls[3] =
    {
        D9BatchCall::VertexShaderConst, D9BatchCall::VertexShaderConstI, D9BatchCall::VertexShaderConstB,
    };
    static const D9BatchCall PixelCalls[3] =
    {
        D9BatchCall::PixelShaderConst, D9BatchCall::PixelShaderConstI, D9BatchCall::PixelShaderConstB,
    };

    for (UINT Kind = 0; Kind < 3; Kind++)
    {
        const USHORT* Defs = Kind == 0 ? Old->FloatDefs : Kind == 1 ? Old->IntDefs : Old->BoolDefs;
        UINT Count = Kind == 0 ? Old->FloatDefCount : Kind == 1 ? Old->IntDefCount : Old->BoolDefCount;
        const USHORT* Kept = nullptr;
        UINT KeptCount = 0;
        UINT Limit;
        BYTE* Values;
        UINT Stride;
        UINT Index = 0;

        if (New != nullptr)
        {
            Kept = Kind == 0 ? New->FloatDefs : Kind == 1 ? New->IntDefs : New->BoolDefs;
            KeptCount = Kind == 0 ? New->FloatDefCount : Kind == 1 ? New->IntDefCount : New->BoolDefCount;
        }
        if (Pixel)
        {
            Limit = Kind == 0 ? Limits.PixelFloats : Kind == 1 ? Limits.PixelInts : Limits.PixelBools;
            Values = Kind == 0 ? reinterpret_cast<BYTE*>(PixelFloats)
                               : Kind == 1 ? reinterpret_cast<BYTE*>(PixelInts) : reinterpret_cast<BYTE*>(PixelBools);
        }
        else
        {
            Limit = Kind == 0 ? Limits.VertexFloats : Kind == 1 ? Limits.VertexInts : Limits.VertexBools;
            Values = Kind == 0 ? reinterpret_cast<BYTE*>(VertexFloats)
                               : Kind == 1 ? reinterpret_cast<BYTE*>(VertexInts) : reinterpret_cast<BYTE*>(VertexBools);
        }
        Stride = Kind == 2 ? sizeof(BOOL) : 4 * sizeof(DWORD);

        while (Index < Count)
        {
            UINT First = Defs[Index];
            UINT Run = 1;

            Index += 1;
            if (First >= Limit || D9Defines(Kept, KeptCount, First))
                continue;
            while (Index < Count && Defs[Index] == First + Run && First + Run < Limit &&
                   !D9Defines(Kept, KeptCount, First + Run))
            {
                Run += 1;
                Index += 1;
            }
            if (!Pixel)
                SendVertexConstants(VertexCalls[Kind], First, Run, Values + First * Stride, Stride);
            else if (Kind == 0)
                SendPixelFloats(First, Run, reinterpret_cast<const FLOAT*>(Values + First * Stride));
            else
                DdiShaderConstants(PixelCalls[Kind], First, Run, Values + First * Stride, Run * Stride);
        }
    }
}

STDMETHODIMP
D9Device::DrawPrimitive(
    _In_ D3DPRIMITIVETYPE PrimitiveType,
    _In_ UINT StartVertex,
    _In_ UINT PrimitiveCount)
{
    D9_DEVICE_LOCK();
    D3DDDIARG_DRAWPRIMITIVE Args = {};

    if (FAILED(CheckFirstDraw(FALSE, FALSE)))
        return D3DERR_INVALIDCALL;
    if ((DrawDirty & D9DrawVertexInput) != 0 && FAILED(FlushVertexInput()))
        return D3DERR_INVALIDCALL;
    if ((DrawDirty & D9DrawTextures) != 0)
        FlushTextures();
    if ((DrawDirty & D9DrawStreams) != 0)
        FlushStreams();
    SendTargets();
    SettleSwitch(PrimitiveType, FALSE);

    Args.PrimitiveType = static_cast<D3DPRIMITIVETYPE>(PrimitiveType);
    Args.VStart = StartVertex;
    Args.PrimitiveCount = PrimitiveCount;
    DdiDrawPrimitive(&Args);
    return S_OK;
}

STDMETHODIMP
D9Device::DrawIndexedPrimitive(
    _In_ D3DPRIMITIVETYPE PrimitiveType,
    _In_ INT BaseVertexIndex,
    _In_ UINT MinVertexIndex,
    _In_ UINT NumVertices,
    _In_ UINT StartIndex,
    _In_ UINT PrimCount)
{
    D9_DEVICE_LOCK();
    D3DDDIARG_DRAWINDEXEDPRIMITIVE Args = {};

    if (FAILED(CheckFirstDraw(TRUE, FALSE)))
        return D3DERR_INVALIDCALL;
    if ((DrawDirty & D9DrawVertexInput) != 0 && FAILED(FlushVertexInput()))
        return D3DERR_INVALIDCALL;
    if ((DrawDirty & D9DrawTextures) != 0)
        FlushTextures();
    if ((DrawDirty & D9DrawStreams) != 0)
        FlushStreams();
    SendTargets();
    SettleSwitch(PrimitiveType, TRUE);

    Args.PrimitiveType = static_cast<D3DPRIMITIVETYPE>(PrimitiveType);
    Args.BaseVertexIndex = BaseVertexIndex;
    Args.MinIndex = MinVertexIndex;
    Args.NumVertices = NumVertices;
    Args.StartIndex = StartIndex;
    Args.PrimitiveCount = PrimCount;
    DdiDrawIndexedPrimitive(&Args);
    return S_OK;
}

/* Stream 0 lets go of its buffer and reads application memory for one draw; afterwards it holds nothing.
   The driver reads the memory where it lies, so a batch runs what it holds first */
STDMETHODIMP
D9Device::DrawPrimitiveUP(
    _In_ D3DPRIMITIVETYPE PrimitiveType,
    _In_ UINT PrimitiveCount,
    _In_opt_ const VOID* VertexStreamZeroData,
    _In_ UINT VertexStreamZeroStride)
{
    D9_DEVICE_LOCK();
    D9Stream* Zero = &Streams[0];
    IDirect3DVertexBuffer9* Held = Zero->Buffer;
    D3DDDIARG_SETSTREAMSOURCEUM Memory = {};
    D3DDDIARG_DRAWPRIMITIVE Args = {};

    if (VertexStreamZeroStride == 0)
    {
        Zero->Offset = 0;
        Zero->Stride = 0;
        return D3DERR_INVALIDCALL;
    }
    Zero->Buffer = nullptr;
    if (Held != nullptr)
        static_cast<D9VertexBuffer*>(Held)->Unbind();
    Zero->Offset = 0;
    Zero->Stride = VertexStreamZeroStride;

    if (FAILED(CheckFirstDraw(FALSE, TRUE)) ||
        ((DrawDirty & D9DrawVertexInput) != 0 && FAILED(FlushVertexInput())))
    {
        Zero->Stride = 0;
        return D3DERR_INVALIDCALL;
    }
    if ((DrawDirty & D9DrawTextures) != 0)
        FlushTextures();
    SendTargets();
    SettleSwitch(PrimitiveType, FALSE);

    DrainBatch();
    Memory.Stream = 0;
    Memory.Stride = VertexStreamZeroStride;
    DeviceFunctions.pfnSetStreamSourceUm(DriverDevice, &Memory, VertexStreamZeroData);
    Args.PrimitiveType = static_cast<D3DPRIMITIVETYPE>(PrimitiveType);
    Args.PrimitiveCount = PrimitiveCount;
    DeviceFunctions.pfnDrawPrimitive(DriverDevice, &Args, nullptr);
    Zero->Stride = 0;
    return S_OK;
}

/* Stream 0 and the index binding let go of their buffers for one draw from application memory */
STDMETHODIMP
D9Device::DrawIndexedPrimitiveUP(
    _In_ D3DPRIMITIVETYPE PrimitiveType,
    _In_ UINT MinVertexIndex,
    _In_ UINT NumVertices,
    _In_ UINT PrimitiveCount,
    _In_opt_ const VOID* IndexData,
    _In_ D3DFORMAT IndexDataFormat,
    _In_opt_ const VOID* VertexStreamZeroData,
    _In_ UINT VertexStreamZeroStride)
{
    D9_DEVICE_LOCK();
    D9Stream* Zero = &Streams[0];
    IDirect3DVertexBuffer9* Held = Zero->Buffer;
    IDirect3DIndexBuffer9* HeldIndices = Indices;
    D3DDDIARG_SETSTREAMSOURCEUM Memory = {};
    D3DDDIARG_DRAWINDEXEDPRIMITIVE Args = {};

    if (VertexStreamZeroStride == 0)
    {
        Zero->Stride = 0;
        IndexStride = 0;
        return D3DERR_INVALIDCALL;
    }
    Zero->Buffer = nullptr;
    if (Held != nullptr)
        static_cast<D9VertexBuffer*>(Held)->Unbind();
    Indices = nullptr;
    if (HeldIndices != nullptr)
        static_cast<D9IndexBuffer*>(HeldIndices)->Unbind();
    Zero->Offset = 0;
    Zero->Stride = VertexStreamZeroStride;
    if (IndexDataFormat == D3DFMT_INDEX16)
        IndexStride = 2;
    else if (IndexDataFormat == D3DFMT_INDEX32)
        IndexStride = 4;

    if (FAILED(CheckFirstDraw(TRUE, TRUE)) ||
        ((DrawDirty & D9DrawVertexInput) != 0 && FAILED(FlushVertexInput())))
    {
        Zero->Stride = 0;
        IndexStride = 0;
        return D3DERR_INVALIDCALL;
    }
    if ((DrawDirty & D9DrawTextures) != 0)
        FlushTextures();
    SendTargets();
    SettleSwitch(PrimitiveType, TRUE);

    DrainBatch();
    Memory.Stream = 0;
    Memory.Stride = VertexStreamZeroStride;
    DeviceFunctions.pfnSetStreamSourceUm(DriverDevice, &Memory, VertexStreamZeroData);
    DeviceFunctions.pfnSetIndicesUm(DriverDevice, IndexStride, IndexData);
    Args.PrimitiveType = static_cast<D3DPRIMITIVETYPE>(PrimitiveType);
    Args.MinIndex = MinVertexIndex;
    Args.NumVertices = NumVertices;
    Args.PrimitiveCount = PrimitiveCount;
    DeviceFunctions.pfnDrawIndexedPrimitive(DriverDevice, &Args);
    Zero->Stride = 0;
    IndexStride = 0;
    return S_OK;
}
