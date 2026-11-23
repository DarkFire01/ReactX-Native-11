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

/* Transformed vertices skip the vertex converter; untransformed ones of a declaration feed it */
VOID
D9Device::SwitchDeclaration(
    _In_ HANDLE Handle,
    _In_ BOOLEAN Transformed)
{
    if (Transformed)
    {
        Switch.PixelFvf = 0;
        Switch.PixelDeclaration = Handle;
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
            D9_TRACE_ONCE(L"a draw needs the fixed function vertex pipeline, which is not converted yet");
            Acting = Switch.Pending & Switch.Allowed;
        }
        else if ((Acting & D9SwitchWantApiVertexShader) != 0)
        {
            UINT Floats;
            UINT Ints;

            Switch.VertexConverting = FALSE;
            Switch.Pending &= ~(D9SwitchWantVertexConverter | D9SwitchWantApiVertexShader);
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
            Switch.PixelMode = 2;
            Switch.Pending &= ~D9SwitchWantPixelConverter;
            Switch.Allowed = (Switch.Allowed | D9AllowPixelConverter) & ~D9SwitchWantPixelConverter;
            D9_TRACE_ONCE(L"a draw needs the fixed function pixel pipeline, which is not converted yet");
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
    if ((Acting & D9SwitchInputChanges) != 0)
        Switch.Pending &= ~(Acting & D9SwitchInputChanges);
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

    if (Bound->Transformed)
    {
        SwitchDeclaration(Bound->DriverHandle, TRUE);
        return S_OK;
    }
    SwitchDeclaration(Bound->DriverHandle, FALSE);
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
            DdiTexture(D9SlotStage(Slot), Resource != nullptr ? Resource->DriverResource : NULL);
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
