/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 command list state, root arguments, draws, dispatches, indirect work and queries
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device12.hpp"

static constexpr UINT RxWarp12DescriptorStride = 64;
static constexpr UINT RxWarp12RootBufferConstants = 4096;

BOOLEAN
RxWarp12Window::Open(
    _In_ RxWarp12Device *Device,
    _In_ D3D12DDI_GPU_VIRTUAL_ADDRESS Address,
    _In_ UINT64 Size)
{
    UINT64 Offset = 0;
    RxWarp12Resource *Base = (Address != 0) ? RxWarp12ResolveAddress(Device, Address, &Offset) : nullptr;
    UINT64 Left;

    Storage.Memory = nullptr;
    Storage.MemorySize = 0;
    Sub = {};
    if (Base == nullptr || Base->Storage.Memory == nullptr || Offset > Base->Width)
        return FALSE;
    Left = Base->Width - Offset;
    if (Size > Left)
        Size = Left;
    Storage.Dimension = D3D10DDIRESOURCE_BUFFER;
    Storage.Memory = Base->Storage.Memory + Offset;
    Storage.MemorySize = (SIZE_T)Size;
    Sub.Width = (UINT)Size;
    Sub.Height = 1;
    Sub.Depth = 1;
    Sub.RowPitch = (UINT)Size;
    Sub.DepthPitch = (UINT)Size;
    Sub.Size = (SIZE_T)Size;
    return TRUE;
}

BOOLEAN
RxWarp12Replay::Skipped(VOID) const
{
    UINT64 Value = 0;

    if (PredicateBuffer == nullptr || PredicateBuffer->Memory == nullptr || PredicateOffset + sizeof(Value) > PredicateBuffer->MemorySize)
        return FALSE;
    memcpy(&Value, PredicateBuffer->Memory + PredicateOffset, sizeof(Value));
    return (PredicateOp == D3D12DDI_PREDICATION_OP_EQUAL_ZERO) ? (Value == 0) : (Value != 0);
}

/* Newer tables pass the application's command list; its bypass header leads to ours */
template <typename Work>
static
VOID
RxWarp12Note(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ Work &&Command)
{
    RxWarp12Append(GetDriverCommandListHandle(List), RX_WARP12_COMMAND(std::forward<Work>(Command)));
}

template <typename Work>
static
VOID
RxWarp12Note(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ Work &&Command)
{
    RxWarp12Append(List, RX_WARP12_COMMAND(std::forward<Work>(Command)));
}

/* The stage a root parameter's visibility names; compute sees every parameter */
static
BOOLEAN
RxWarp12Visible(
    _In_ D3D12DDI_SHADER_VISIBILITY Visibility,
    _In_ RX_WARP_STAGE Stage)
{
    static const D3D12DDI_SHADER_VISIBILITY Of[RxWarpStageCount] =
    {
        D3D12DDI_SHADER_VISIBILITY_VERTEX,
        D3D12DDI_SHADER_VISIBILITY_PIXEL,
        D3D12DDI_SHADER_VISIBILITY_GEOMETRY,
        D3D12DDI_SHADER_VISIBILITY_HULL,
        D3D12DDI_SHADER_VISIBILITY_DOMAIN,
        D3D12DDI_SHADER_VISIBILITY_ALL,
    };

    return Stage == RxWarpStageCompute || Visibility == D3D12DDI_SHADER_VISIBILITY_ALL || Visibility == Of[Stage];
}

/* Which slots a stage declares, over all of a hull shader's phases */
typedef struct _RX_WARP12_STAGE_USE
{
    UINT Constants;
    UINT Samplers;
    BOOLEAN Views[128];
    BOOLEAN Uavs[64];

    /* Slots a lowered program fills from another space or register, as class, slot, space, register */
    std::vector<UINT> Sources;
    UINT MovedConstants;
    UINT MovedSamplers;
    BOOLEAN MovedViews[128];
    BOOLEAN MovedUavs[64];
} RX_WARP12_STAGE_USE;

static
VOID
RxWarp12Gather(
    _In_ const RxWarpShader *Shader,
    _Inout_ RX_WARP12_STAGE_USE *Use)
{
    Use->Constants |= Shader->ConstantMask;
    Use->Samplers |= Shader->SamplerMask;
    for (UINT Slot = 0; Slot < 128; Slot++)
        Use->Views[Slot] |= Shader->ResourceDimension[Slot] != 0;
    for (UINT Slot = 0; Slot < 64; Slot++)
        Use->Uavs[Slot] |= Shader->UavDimension[Slot] != 0;
    for (SIZE_T Index = 0; Index + 4 <= Shader->SlotSources.size(); Index += 4)
    {
        UINT Class = Shader->SlotSources[Index];
        UINT Slot = Shader->SlotSources[Index + 1];

        Use->Sources.insert(Use->Sources.end(), Shader->SlotSources.begin() + Index, Shader->SlotSources.begin() + Index + 4);
        if (Class == D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV && Slot < 128)
            Use->MovedViews[Slot] = TRUE;
        else if (Class == D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV && Slot < 64)
            Use->MovedUavs[Slot] = TRUE;
        else if (Class == D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV && Slot < RxWarpConstantBufferSlots)
            Use->MovedConstants |= 1u << Slot;
        else if (Slot < 16)
            Use->MovedSamplers |= 1u << Slot;
    }
    for (const RxWarpShader &Phase : Shader->Phases)
        RxWarp12Gather(&Phase, Use);
}

static
VOID
RxWarp12BindConstants(
    _Inout_ RX_WARP_STAGE_BINDINGS *Bindings,
    _In_ UINT Slot,
    _In_opt_ RxWarpResource *Resource,
    _In_ UINT64 Offset,
    _In_ UINT64 Size)
{
    UINT64 Left;

    if (Resource == nullptr || Resource->Memory == nullptr || Offset >= Resource->MemorySize)
        return;
    Left = Resource->MemorySize - Offset;
    if (Size > Left)
        Size = Left;
    Bindings->ConstantBuffer[Slot] = Resource->Memory + Offset;
    Bindings->ConstantCount[Slot] = (UINT)(Size / 16);
}

/* One descriptor into one executor slot of its kind */
static
VOID
RxWarp12BindSlot(
    _In_ UINT Kind,
    _In_ UINT Slot,
    _In_ RX_WARP12_DESCRIPTOR *Descriptor,
    _Inout_ RX_WARP_STAGE_BINDINGS *Bindings)
{
    switch (Kind)
    {
        case D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV:
            Bindings->Views[Slot] = (Descriptor->Kind == RxWarp12DescriptorShaderResource) ? &Descriptor->View : nullptr;
            break;
        case D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV:
            Bindings->Uavs[Slot] = (Descriptor->Kind == RxWarp12DescriptorUnorderedAccess) ? &Descriptor->View : nullptr;
            break;
        case D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV:
            if (Descriptor->Kind == RxWarp12DescriptorConstants)
                RxWarp12BindConstants(Bindings, Slot, Descriptor->Constants.Resource, Descriptor->Constants.Offset, Descriptor->Constants.Size);
            break;
        default:
            Bindings->Samplers[Slot] = (Descriptor->Kind == RxWarp12DescriptorSampler) ? &Descriptor->Sampler : nullptr;
            break;
    }
}

/* One range of a descriptor table, onto the slots the stage declares */
static
VOID
RxWarp12BindRange(
    _In_ const RX_WARP12_ROOT_RANGE &Range,
    _In_ UINT64 Table,
    _In_ const RX_WARP12_STAGE_USE &Use,
    _Inout_ RX_WARP_STAGE_BINDINGS *Bindings)
{
    UINT Limit;

    /* Slots moved by the lowering find their register in the range */
    for (SIZE_T Index = 0; Index + 4 <= Use.Sources.size(); Index += 4)
    {
        UINT Register = Use.Sources[Index + 3];

        if (Use.Sources[Index] != (UINT)Range.Kind || Use.Sources[Index + 2] != Range.Space || Register < Range.Base ||
            Register - Range.Base >= Range.Count)
        {
            continue;
        }
        RxWarp12BindSlot(Range.Kind, Use.Sources[Index + 1],
                         RxWarp12Descriptor((SIZE_T)(Table + (UINT64)(Range.Offset + Register - Range.Base) * RxWarp12DescriptorStride)),
                         Bindings);
    }

    if (Range.Space != 0)
        return;
    switch (Range.Kind)
    {
        case D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV:
            Limit = 128;
            break;
        case D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV:
            Limit = 64;
            break;
        case D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV:
            Limit = RxWarpConstantBufferSlots;
            break;
        default:
            Limit = 16;
            break;
    }
    for (UINT Index = 0; Index < Range.Count && Range.Base + Index < Limit; Index++)
    {
        UINT Slot = Range.Base + Index;

        switch (Range.Kind)
        {
            case D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV:
                if (!Use.Views[Slot] || Use.MovedViews[Slot])
                    continue;
                break;
            case D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV:
                if (!Use.Uavs[Slot] || Use.MovedUavs[Slot])
                    continue;
                break;
            case D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV:
                if (!(Use.Constants & (1u << Slot)) || (Use.MovedConstants & (1u << Slot)))
                    continue;
                break;
            default:
                if (!(Use.Samplers & (1u << Slot)) || (Use.MovedSamplers & (1u << Slot)))
                    continue;
                break;
        }
        RxWarp12BindSlot(Range.Kind, Slot, RxWarp12Descriptor((SIZE_T)(Table + (UINT64)(Range.Offset + Index) * RxWarp12DescriptorStride)),
                         Bindings);
    }
}

/* Root views read a buffer from its GPU address on; the shader's declaration decides the element size */
static
RxWarpView *
RxWarp12RootView(
    _Inout_ RxWarp12Replay *State,
    _In_ UINT Parameter,
    _In_ UINT64 Address)
{
    RxWarpView *View = &State->RootViews[Parameter];

    if (!State->RootWindows[Parameter].Open(State->Device, Address, UINT64_MAX))
        return nullptr;
    *View = RxWarpView();
    View->Resource = &State->RootWindows[Parameter].Storage;
    View->ElementCount = UINT_MAX;
    return View;
}

/* Gathers what one stage sees from the root signature and its arguments */
static
VOID
RxWarp12BindStage(
    _Inout_ RxWarp12Replay *State,
    _In_ const RX_WARP12_ROOT_ARGUMENTS *Root,
    _In_ RX_WARP_STAGE Stage,
    _In_ const RxWarpShader *Shader,
    _Out_ RX_WARP_STAGE_BINDINGS *Bindings)
{
    const RxWarp12RootSignature *Signature = Root->Signature;
    RX_WARP12_STAGE_USE Use = {};

    ZeroMemory(Bindings, sizeof(*Bindings));
    if (Signature == nullptr)
        return;
    RxWarp12Gather(Shader, &Use);

    for (UINT Index = 0; Index < Signature->Parameters.size(); Index++)
    {
        const RX_WARP12_ROOT_PARAMETER &Parameter = Signature->Parameters[Index];
        UINT64 Value = Root->Values[Index];

        if (!RxWarp12Visible(Parameter.Visibility, Stage))
            continue;
        switch (Parameter.Type)
        {
            case D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                if (Value == 0)
                    break;
                for (const RX_WARP12_ROOT_RANGE &Range : Parameter.Ranges)
                    RxWarp12BindRange(Range, Value, Use, Bindings);
                break;
            case D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                if (Parameter.Space != 0 || Parameter.Register >= RxWarpConstantBufferSlots)
                    break;
                Bindings->ConstantBuffer[Parameter.Register] = reinterpret_cast<const BYTE *>(Root->Constants[Index]);
                Bindings->ConstantCount[Parameter.Register] = (Parameter.Values + 3) / 4;
                break;
            case D3D12DDI_ROOT_PARAMETER_TYPE_CBV:
            {
                UINT64 Offset = 0;
                RxWarp12Resource *Resource = (Value != 0) ? RxWarp12ResolveAddress(State->Device, Value, &Offset) : nullptr;

                if (Resource == nullptr || Parameter.Space != 0 || Parameter.Register >= RxWarpConstantBufferSlots)
                    break;
                RxWarp12BindConstants(Bindings, Parameter.Register, &Resource->Storage, Offset,
                                      (UINT64)RxWarp12RootBufferConstants * 16);
                break;
            }
            case D3D12DDI_ROOT_PARAMETER_TYPE_SRV:
                if (Parameter.Space == 0 && Parameter.Register < 128 && Use.Views[Parameter.Register] && !Use.MovedViews[Parameter.Register])
                    Bindings->Views[Parameter.Register] = RxWarp12RootView(State, Index, Value);
                break;
            case D3D12DDI_ROOT_PARAMETER_TYPE_UAV:
                if (Parameter.Space == 0 && Parameter.Register < 64 && Use.Uavs[Parameter.Register] && !Use.MovedUavs[Parameter.Register])
                    Bindings->Uavs[Parameter.Register] = RxWarp12RootView(State, Index, Value);
                break;
            default:
                break;
        }

        /* Root arguments for slots the lowering moved */
        for (SIZE_T Source = 0; Source + 4 <= Use.Sources.size(); Source += 4)
        {
            UINT Class = Use.Sources[Source];
            UINT Slot = Use.Sources[Source + 1];

            if (Use.Sources[Source + 2] != Parameter.Space || Use.Sources[Source + 3] != Parameter.Register)
                continue;
            if (Parameter.Type == D3D12DDI_ROOT_PARAMETER_TYPE_SRV && Class == D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV)
                Bindings->Views[Slot] = RxWarp12RootView(State, Index, Value);
            else if (Parameter.Type == D3D12DDI_ROOT_PARAMETER_TYPE_UAV && Class == D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV)
                Bindings->Uavs[Slot] = RxWarp12RootView(State, Index, Value);
            else if (Parameter.Type == D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS && Class == D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV)
            {
                Bindings->ConstantBuffer[Slot] = reinterpret_cast<const BYTE *>(Root->Constants[Index]);
                Bindings->ConstantCount[Slot] = (Parameter.Values + 3) / 4;
            }
            else if (Parameter.Type == D3D12DDI_ROOT_PARAMETER_TYPE_CBV && Class == D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV && Value != 0)
            {
                UINT64 Offset = 0;
                RxWarp12Resource *Resource = RxWarp12ResolveAddress(State->Device, Value, &Offset);

                if (Resource != nullptr)
                    RxWarp12BindConstants(Bindings, Slot, &Resource->Storage, Offset, (UINT64)RxWarp12RootBufferConstants * 16);
            }
        }
    }
    for (const RX_WARP12_STATIC_SAMPLER &Sampler : Signature->Samplers)
    {
        if (!RxWarp12Visible(Sampler.Visibility, Stage))
            continue;
        if (Sampler.Space == 0 && Sampler.Register < 16 && !(Use.MovedSamplers & (1u << Sampler.Register)))
            Bindings->Samplers[Sampler.Register] = &Sampler.Sampler;
        for (SIZE_T Source = 0; Source + 4 <= Use.Sources.size(); Source += 4)
        {
            if (Use.Sources[Source] == D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER && Use.Sources[Source + 2] == Sampler.Space &&
                Use.Sources[Source + 3] == Sampler.Register)
            {
                Bindings->Samplers[Use.Sources[Source + 1]] = &Sampler.Sampler;
            }
        }
    }
}

/* Points the executor's pipeline at everything a draw reads; FALSE without a graphics pipeline */
static
BOOLEAN
RxWarp12LoadGraphics(
    _Inout_ RxWarp12Replay *State)
{
    RxWarpDevice *Executor = State->Device->Executor;
    RX_WARP_PIPELINE *Pipeline = &Executor->Pipeline;
    RxWarp12PipelineState *Pso = State->Pipeline;

    if (Pso == nullptr || Pso->Compute)
        return FALSE;
    ZeroMemory(Pipeline, sizeof(*Pipeline));
    Pipeline->Layout = &Pso->Layout;
    for (UINT Slot = 0; Slot < 32; Slot++)
    {
        const RX_WARP12_VERTEX_BUFFER &Buffer = State->Vertices[Slot];

        if (State->VertexWindows[Slot].Open(State->Device, Buffer.Address, Buffer.Size))
        {
            Pipeline->VertexBuffers[Slot].Resource = &State->VertexWindows[Slot].Storage;
            Pipeline->VertexBuffers[Slot].Stride = Buffer.Stride;
        }
    }
    if (State->IndexWindow.Open(State->Device, State->IndexAddress, State->IndexSize))
        Pipeline->IndexBuffer = &State->IndexWindow.Storage;
    Pipeline->IndexFormat = State->IndexFormat;
    Pipeline->Topology = (D3D10_DDI_PRIMITIVE_TOPOLOGY)State->Topology;
    for (UINT Stage = 0; Stage < RxWarpStageCompute; Stage++)
    {
        RxWarpShader *Shader = Pso->Shaders[Stage];

        Pipeline->Stages[Stage].Shader = Shader;
        if (Shader == nullptr)
            continue;
        RxWarp12BindStage(State, &State->Graphics, (RX_WARP_STAGE)Stage, Shader, &State->Bindings[Stage]);
        Executor->Bound[Stage] = &State->Bindings[Stage];
    }
    Pipeline->Blend = &Pso->Blend;
    memcpy(Pipeline->BlendFactor, State->BlendFactor, sizeof(Pipeline->BlendFactor));
    Pipeline->SampleMask = Pso->SampleMask;
    Pipeline->DepthStencil = &Pso->DepthStencil;
    Pipeline->StencilRef = State->StencilRef;
    Pipeline->Rasterizer = &Pso->Rasterizer;
    memcpy(Pipeline->Viewports, State->Viewports, sizeof(Pipeline->Viewports));
    Pipeline->ViewportCount = State->ViewportCount;
    memcpy(Pipeline->Scissors, State->Scissors, sizeof(Pipeline->Scissors));
    Pipeline->ScissorCount = State->ScissorCount;
    for (UINT Index = 0; Index < State->TargetCount; Index++)
        Pipeline->RenderTargets[Index] = State->TargetBound[Index] ? &State->Targets[Index] : nullptr;
    Pipeline->RenderTargetCount = State->TargetCount;
    Pipeline->DepthStencilView = State->DepthBound ? &State->Depth : nullptr;
    Pipeline->NoStripCut = Pso->StripCut == D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

    /* Stream output appends after what the filled size location says is already there */
    for (UINT Slot = 0; Slot < 4; Slot++)
    {
        const RX_WARP12_STREAM_TARGET &Target = State->Streams[Slot];
        RxWarp12Window *Window = &State->StreamWindows[Slot];
        RxWarp12Window Filled;

        if (!Window->Open(State->Device, Target.Address, Target.Size))
            continue;
        Window->Storage.StreamFilled = 0;
        if (Filled.Open(State->Device, Target.Filled, sizeof(UINT)) && Filled.Storage.MemorySize >= sizeof(UINT))
            memcpy(&Window->Storage.StreamFilled, Filled.Storage.Memory, sizeof(UINT));
        Pipeline->StreamTargets[Slot] = &Window->Storage;
    }
    return TRUE;
}

static
VOID
RxWarp12Unload(
    _Inout_ RxWarp12Replay *State)
{
    RxWarpDevice *Executor = State->Device->Executor;

    for (UINT Slot = 0; Slot < 4; Slot++)
    {
        RxWarp12Window Filled;

        if (Executor->Pipeline.StreamTargets[Slot] == nullptr)
            continue;
        if (Filled.Open(State->Device, State->Streams[Slot].Filled, sizeof(UINT64)) && Filled.Storage.MemorySize >= sizeof(UINT64))
        {
            UINT64 Value = State->StreamWindows[Slot].Storage.StreamFilled;

            memcpy(Filled.Storage.Memory, &Value, sizeof(Value));
        }
    }
    for (UINT Stage = 0; Stage < RxWarpStageCount; Stage++)
        Executor->Bound[Stage] = nullptr;
    ZeroMemory(&Executor->Pipeline, sizeof(Executor->Pipeline));
}

static
VOID
RxWarp12RunDraw(
    _Inout_ RxWarp12Replay *State,
    _In_ UINT Count,
    _In_ UINT Instances,
    _In_ UINT Start,
    _In_ BOOLEAN Indexed,
    _In_ INT BaseVertex,
    _In_ UINT StartInstance)
{
    if (State->Skipped() || !RxWarp12LoadGraphics(State))
        return;
    RxWarpRunDraw(State->Device->Executor, Count, Start, Indexed, BaseVertex, Instances, StartInstance);
    RxWarp12Unload(State);
}

static
VOID
RxWarp12RunDispatch(
    _Inout_ RxWarp12Replay *State,
    _In_ UINT X,
    _In_ UINT Y,
    _In_ UINT Z)
{
    RxWarpDevice *Executor = State->Device->Executor;
    RxWarp12PipelineState *Pso = State->Pipeline;

    if (State->Skipped() || Pso == nullptr || !Pso->Compute)
        return;
    ZeroMemory(&Executor->Pipeline, sizeof(Executor->Pipeline));
    Executor->Pipeline.Stages[RxWarpStageCompute].Shader = Pso->Shaders[RxWarpStageCompute];
    RxWarp12BindStage(State, &State->Compute, RxWarpStageCompute, Pso->Shaders[RxWarpStageCompute], &State->Bindings[RxWarpStageCompute]);
    Executor->Bound[RxWarpStageCompute] = &State->Bindings[RxWarpStageCompute];
    RxWarpDispatch(Executor, X, Y, Z);
    RxWarp12Unload(State);
}

static
VOID
APIENTRY
RxWarp12DrawInstanced(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Count,
    _In_ UINT Instances,
    _In_ UINT Start,
    _In_ UINT StartInstance)
{
    RxWarpLock Lock;

    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        RxWarp12RunDraw(State, Count, Instances, Start, FALSE, 0, StartInstance);
    });
}

static
VOID
APIENTRY
RxWarp12DrawIndexedInstanced(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Count,
    _In_ UINT Instances,
    _In_ UINT Start,
    _In_ INT BaseVertex,
    _In_ UINT StartInstance)
{
    RxWarpLock Lock;

    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        RxWarp12RunDraw(State, Count, Instances, Start, TRUE, BaseVertex, StartInstance);
    });
}

static
VOID
APIENTRY
RxWarp12Dispatch(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT X,
    _In_ UINT Y,
    _In_ UINT Z)
{
    RxWarpLock Lock;

    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        RxWarp12RunDispatch(State, X, Y, Z);
    });
}

static
VOID
APIENTRY
RxWarp12SetTopology(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ D3D12DDI_PRIMITIVE_TOPOLOGY Topology)
{
    RxWarpLock Lock;

    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        State->Topology = Topology;
    });
}

static
VOID
APIENTRY
RxWarp12SetViewports(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Count,
    _In_reads_(Count) const D3D12DDI_VIEWPORT *Viewports)
{
    RxWarpLock Lock;
    D3D10_DDI_VIEWPORT Copy[16] = {};
    UINT Taken = (Count < 16) ? Count : 16;

    for (UINT Index = 0; Index < Taken; Index++)
    {
        Copy[Index].TopLeftX = Viewports[Index].TopLeftX;
        Copy[Index].TopLeftY = Viewports[Index].TopLeftY;
        Copy[Index].Width = Viewports[Index].Width;
        Copy[Index].Height = Viewports[Index].Height;
        Copy[Index].MinDepth = Viewports[Index].MinDepth;
        Copy[Index].MaxDepth = Viewports[Index].MaxDepth;
    }
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        memcpy(State->Viewports, Copy, sizeof(State->Viewports));
        State->ViewportCount = Taken;
    });
}

static
VOID
APIENTRY
RxWarp12SetScissorRects(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Count,
    _In_reads_(Count) const D3D12DDI_RECT *Rects)
{
    RxWarpLock Lock;
    D3D10_DDI_RECT Copy[16] = {};
    UINT Taken = (Count < 16) ? Count : 16;

    for (UINT Index = 0; Index < Taken; Index++)
    {
        Copy[Index].left = Rects[Index].left;
        Copy[Index].top = Rects[Index].top;
        Copy[Index].right = Rects[Index].right;
        Copy[Index].bottom = Rects[Index].bottom;
    }
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        memcpy(State->Scissors, Copy, sizeof(State->Scissors));
        State->ScissorCount = Taken;
    });
}

static
VOID
APIENTRY
RxWarp12SetBlendFactor(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ const FLOAT Factor[4])
{
    RxWarpLock Lock;
    FLOAT Copy[4];

    memcpy(Copy, Factor, sizeof(Copy));
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        memcpy(State->BlendFactor, Copy, sizeof(State->BlendFactor));
    });
}

static
VOID
APIENTRY
RxWarp12SetStencilRef(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Reference)
{
    RxWarpLock Lock;

    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        State->StencilRef = Reference;
    });
}

static
VOID
APIENTRY
RxWarp12SetFrontAndBackStencilRef(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Front,
    _In_ UINT Back)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Back);
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        State->StencilRef = Front;
    });
}

static
VOID
APIENTRY
RxWarp12SetPipelineState(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ D3D12DDI_API_HPIPELINESTATE Pipeline)
{
    RxWarpLock Lock;
    RxWarp12PipelineState *Object = Pipeline.pDrvPrivate ? RxWarpObject<RxWarp12PipelineState>(GetDriverPipelineStateHandle(Pipeline)) : nullptr;

    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        State->Pipeline = Object;
    });
}

/* Descriptor handles are addresses, so the bound heaps change nothing */
static
VOID
APIENTRY
RxWarp12SetDescriptorHeaps(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ UINT Count,
    _In_reads_(Count) D3D12DDI_HDESCRIPTORHEAP *Heaps)
{
    UNREFERENCED_PARAMETER(List);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Heaps);
}

static
RxWarp12RootSignature *
RxWarp12SignatureOf(
    _In_ D3D12DDI_API_HROOTSIGNATURE Signature)
{
    D3D12DDI_HROOTSIGNATURE Handle = Signature.pDrvPrivate ? GetDriverRootSignatureHandle(Signature) : D3D12DDI_HROOTSIGNATURE{};

    return RxWarpObject<RxWarp12RootSignature>(Handle);
}

static
VOID
APIENTRY
RxWarp12SetComputeRootSignature(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ D3D12DDI_API_HROOTSIGNATURE Signature)
{
    RxWarpLock Lock;
    RxWarp12RootSignature *Object = RxWarp12SignatureOf(Signature);

    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        State->Compute.Signature = Object;
    });
}

static
VOID
APIENTRY
RxWarp12SetGraphicsRootSignature(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ D3D12DDI_API_HROOTSIGNATURE Signature)
{
    RxWarpLock Lock;
    RxWarp12RootSignature *Object = RxWarp12SignatureOf(Signature);

    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        State->Graphics.Signature = Object;
    });
}

/* Root argument setters, one template for both bind points */
template <BOOLEAN Compute>
static
RX_WARP12_ROOT_ARGUMENTS *
RxWarp12RootOf(
    _In_ RxWarp12Replay *State)
{
    return Compute ? &State->Compute : &State->Graphics;
}

template <BOOLEAN Compute>
static
VOID
APIENTRY
RxWarp12SetRootTable(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Parameter,
    _In_ D3D12DDI_GPU_DESCRIPTOR_HANDLE Base)
{
    RxWarpLock Lock;
    UINT64 Value = Base.ptr;

    if (Parameter >= RxWarp12RootSlots)
        return;
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        RxWarp12RootOf<Compute>(State)->Values[Parameter] = Value;
    });
}

template <BOOLEAN Compute>
static
VOID
APIENTRY
RxWarp12SetRootView(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Parameter,
    _In_ D3D12DDI_GPU_VIRTUAL_ADDRESS Address)
{
    RxWarpLock Lock;

    if (Parameter >= RxWarp12RootSlots)
        return;
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        RxWarp12RootOf<Compute>(State)->Values[Parameter] = Address;
    });
}

template <BOOLEAN Compute>
static
VOID
APIENTRY
RxWarp12SetRootConstant(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Parameter,
    _In_ UINT Value,
    _In_ UINT Offset)
{
    RxWarpLock Lock;

    if (Parameter >= RxWarp12RootSlots || Offset >= 64)
        return;
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        RxWarp12RootOf<Compute>(State)->Constants[Parameter][Offset] = Value;
    });
}

template <BOOLEAN Compute>
static
VOID
APIENTRY
RxWarp12SetRootConstants(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Parameter,
    _In_ UINT Count,
    _In_reads_(Count) const VOID *Values,
    _In_ UINT Offset)
{
    RxWarpLock Lock;
    std::vector<UINT> Copy;

    if (Parameter >= RxWarp12RootSlots || Offset >= 64)
        return;
    if (Count > 64 - Offset)
        Count = 64 - Offset;
    Copy.assign(static_cast<const UINT *>(Values), static_cast<const UINT *>(Values) + Count);
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        memcpy(&RxWarp12RootOf<Compute>(State)->Constants[Parameter][Offset], Copy.data(), Copy.size() * sizeof(UINT));
    });
}

static
VOID
APIENTRY
RxWarp12SetIndexBuffer(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ const D3D12DDI_INDEX_BUFFER_VIEW *View)
{
    RxWarpLock Lock;
    D3D12DDI_INDEX_BUFFER_VIEW Copy = {};

    if (View != nullptr)
        Copy = *View;
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        State->IndexAddress = Copy.BufferLocation;
        State->IndexSize = Copy.SizeInBytes;
        State->IndexFormat = Copy.Format;
    });
}

static
VOID
APIENTRY
RxWarp12SetVertexBuffers(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_opt_(Count) const D3D12DDI_VERTEX_BUFFER_VIEW *Views)
{
    RxWarpLock Lock;
    std::vector<RX_WARP12_VERTEX_BUFFER> Copy;

    for (UINT Index = 0; Index < Count && StartSlot + Index < 32; Index++)
    {
        RX_WARP12_VERTEX_BUFFER Buffer = {};

        if (Views != nullptr)
        {
            Buffer.Address = Views[Index].BufferLocation;
            Buffer.Size = Views[Index].SizeInBytes;
            Buffer.Stride = Views[Index].StrideInBytes;
        }
        Copy.push_back(Buffer);
    }
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        for (SIZE_T Index = 0; Index < Copy.size(); Index++)
            State->Vertices[StartSlot + Index] = Copy[Index];
    });
}

static
VOID
APIENTRY
RxWarp12SetStreamTargets(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_opt_(Count) const D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW *Views)
{
    RxWarpLock Lock;
    std::vector<RX_WARP12_STREAM_TARGET> Copy;

    for (UINT Index = 0; Index < Count && StartSlot + Index < 4; Index++)
    {
        RX_WARP12_STREAM_TARGET Target = {};

        if (Views != nullptr)
        {
            Target.Address = Views[Index].BufferLocation;
            Target.Size = Views[Index].SizeInBytes;
            Target.Filled = Views[Index].BufferFilledSizeLocation;
        }
        Copy.push_back(Target);
    }
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        for (SIZE_T Index = 0; Index < Copy.size(); Index++)
            State->Streams[StartSlot + Index] = Copy[Index];
    });
}

/* Target descriptors are read now, as the API asks */
static
VOID
APIENTRY
RxWarp12SetRenderTargets(
    _In_ D3D12DDI_API_HCOMMANDLIST List,
    _In_ UINT Count,
    _In_reads_(Count) const D3D12DDI_CPU_DESCRIPTOR_HANDLE *Targets,
    _In_ BOOL SingleRange,
    _In_opt_ const D3D12DDI_CPU_DESCRIPTOR_HANDLE *DepthStencil)
{
    RxWarpLock Lock;
    RxWarpView Views[8];
    BOOLEAN Bound[8] = {};
    RxWarpView Depth;
    BOOLEAN DepthBound = FALSE;
    UINT Taken = (Count < 8) ? Count : 8;

    for (UINT Index = 0; Index < Taken; Index++)
    {
        D3D12DDI_CPU_DESCRIPTOR_HANDLE Handle = SingleRange ? D3D12DDI_CPU_DESCRIPTOR_HANDLE{ Targets[0].ptr + (SIZE_T)Index * sizeof(VOID *) } :
                                                              Targets[Index];
        RxWarpView *View = RxWarp12TargetView(Handle);

        if (View != nullptr && View->Resource != nullptr)
        {
            Views[Index] = *View;
            Bound[Index] = TRUE;
        }
    }
    if (DepthStencil != nullptr)
    {
        RxWarpView *View = RxWarp12TargetView(*DepthStencil);

        if (View != nullptr && View->Resource != nullptr)
        {
            Depth = *View;
            DepthBound = TRUE;
        }
    }
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        for (UINT Index = 0; Index < 8; Index++)
        {
            State->Targets[Index] = Views[Index];
            State->TargetBound[Index] = Bound[Index];
        }
        State->TargetCount = Taken;
        State->Depth = Depth;
        State->DepthBound = DepthBound;
    });
}

static
VOID
APIENTRY
RxWarp12SetPredication(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HRESOURCE Buffer,
    _In_ UINT64 Offset,
    _In_ D3D12DDI_PREDICATION_OP Operation)
{
    RxWarpLock Lock;
    RxWarp12Resource *Resource = Buffer.pDrvPrivate ? RxWarp12GetResource(Buffer) : nullptr;

    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        State->PredicateBuffer = Resource ? &Resource->Storage : nullptr;
        State->PredicateOffset = Offset;
        State->PredicateOp = Operation;
    });
}

/* Bundles run on the caller's state and leave their changes in it */
static
VOID
APIENTRY
RxWarp12ExecuteBundle(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HCOMMANDLIST Bundle)
{
    RxWarpLock Lock;
    RxWarp12CommandList *Object = RxWarpObject<RxWarp12CommandList>(Bundle);

    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        RxWarp12RunList(Object, State);
    });
}

/* Reads one command's worth of arguments and runs it */
static
VOID
RxWarp12RunIndirect(
    _Inout_ RxWarp12Replay *State,
    _In_ const RxWarp12CommandSignature *Signature,
    _In_reads_bytes_(Signature->Stride) const BYTE *Data,
    _In_ BOOLEAN Compute)
{
    RX_WARP12_ROOT_ARGUMENTS *Root = Compute ? &State->Compute : &State->Graphics;
    SIZE_T Position = 0;
    UINT Values[5];

    for (const D3D12DDI_INDIRECT_ARGUMENT_DESC &Argument : Signature->Arguments)
    {
        switch (Argument.Type)
        {
            case D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW:
                memcpy(Values, Data + Position, 16);
                Position += 16;
                RxWarp12RunDraw(State, Values[0], Values[1], Values[2], FALSE, 0, Values[3]);
                break;
            case D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                memcpy(Values, Data + Position, 20);
                Position += 20;
                RxWarp12RunDraw(State, Values[0], Values[1], Values[2], TRUE, (INT)Values[3], Values[4]);
                break;
            case D3D12DDI_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                memcpy(Values, Data + Position, 12);
                Position += 12;
                RxWarp12RunDispatch(State, Values[0], Values[1], Values[2]);
                break;
            case D3D12DDI_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
                if (Argument.VertexBuffer.Slot < 32)
                {
                    D3D12DDI_VERTEX_BUFFER_VIEW View;

                    memcpy(&View, Data + Position, sizeof(View));
                    State->Vertices[Argument.VertexBuffer.Slot].Address = View.BufferLocation;
                    State->Vertices[Argument.VertexBuffer.Slot].Size = View.SizeInBytes;
                    State->Vertices[Argument.VertexBuffer.Slot].Stride = View.StrideInBytes;
                }
                Position += sizeof(D3D12DDI_VERTEX_BUFFER_VIEW);
                break;
            case D3D12DDI_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
            {
                D3D12DDI_INDEX_BUFFER_VIEW View;

                memcpy(&View, Data + Position, sizeof(View));
                State->IndexAddress = View.BufferLocation;
                State->IndexSize = View.SizeInBytes;
                State->IndexFormat = View.Format;
                Position += sizeof(View);
                break;
            }
            case D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT:
            {
                UINT Count = Argument.Constant.Num32BitValuesToSet;
                UINT Offset = Argument.Constant.DestOffsetIn32BitValues;

                if (Argument.Constant.RootParameterIndex < RxWarp12RootSlots && Offset < 64)
                    memcpy(&Root->Constants[Argument.Constant.RootParameterIndex][Offset], Data + Position,
                           (SIZE_T)((Count < 64 - Offset) ? Count : 64 - Offset) * sizeof(UINT));
                Position += (SIZE_T)Count * sizeof(UINT);
                break;
            }
            case D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
            case D3D12DDI_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            case D3D12DDI_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
                if (Argument.ConstantBufferView.RootParameterIndex < RxWarp12RootSlots)
                    memcpy(&Root->Values[Argument.ConstantBufferView.RootParameterIndex], Data + Position, sizeof(UINT64));
                Position += sizeof(UINT64);
                break;
            default:
                RX_WARP_TRACE("D3D12 indirect argument %u not handled\n", Argument.Type);
                return;
        }
    }
}

static
VOID
APIENTRY
RxWarp12ExecuteIndirect(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HCOMMANDSIGNATURE Signature,
    _In_ UINT MaxCount,
    _In_ D3D12DDIARG_BUFFER_PLACEMENT Arguments,
    _In_ D3D12DDIARG_BUFFER_PLACEMENT Counts)
{
    RxWarpLock Lock;
    RxWarp12CommandSignature *Object = RxWarpObject<RxWarp12CommandSignature>(Signature);
    RxWarp12Resource *ArgumentBuffer = Arguments.BaseAddress.UMD.hResource.pDrvPrivate ? RxWarp12GetResource(Arguments.BaseAddress.UMD.hResource) : nullptr;
    RxWarp12Resource *CountBuffer = Counts.BaseAddress.UMD.hResource.pDrvPrivate ? RxWarp12GetResource(Counts.BaseAddress.UMD.hResource) : nullptr;
    UINT64 ArgumentOffset = Arguments.BaseAddress.UMD.Offset;
    UINT64 CountOffset = Counts.BaseAddress.UMD.Offset;

    if (Object == nullptr || ArgumentBuffer == nullptr)
        return;
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        UINT Count = MaxCount;
        BOOLEAN Compute = FALSE;

        if (State->Skipped())
            return;
        if (CountBuffer != nullptr && CountBuffer->Storage.Memory != nullptr && CountOffset + sizeof(UINT) <= CountBuffer->Width)
        {
            UINT Stored;

            memcpy(&Stored, CountBuffer->Storage.Memory + CountOffset, sizeof(Stored));
            if (Stored < Count)
                Count = Stored;
        }
        for (const D3D12DDI_INDIRECT_ARGUMENT_DESC &Argument : Object->Arguments)
            Compute |= Argument.Type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        for (UINT Index = 0; Index < Count; Index++)
        {
            UINT64 Offset = ArgumentOffset + (UINT64)Index * Object->Stride;

            if (ArgumentBuffer->Storage.Memory == nullptr || Offset + Object->Stride > ArgumentBuffer->Width)
                break;
            RxWarp12RunIndirect(State, Object, ArgumentBuffer->Storage.Memory + Offset, Compute);
        }
    });
}

/* Queries sample the executor's running totals at begin and end */
static
VOID
APIENTRY
RxWarp12BeginQuery(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HQUERYHEAP Heap,
    _In_ D3D12DDI_QUERY_TYPE Type,
    _In_ UINT Index)
{
    RxWarpLock Lock;
    RxWarp12QueryHeap *Object = RxWarpObject<RxWarp12QueryHeap>(Heap);

    UNREFERENCED_PARAMETER(Type);
    if (Object == nullptr || Index >= Object->Count)
        return;
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        memcpy(&Object->Values[(SIZE_T)Index * RxWarpCountTotal], State->Device->Executor->Counters, sizeof(UINT64) * RxWarpCountTotal);
    });
}

static
VOID
APIENTRY
RxWarp12EndQuery(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HQUERYHEAP Heap,
    _In_ D3D12DDI_QUERY_TYPE Type,
    _In_ UINT Index)
{
    RxWarpLock Lock;
    RxWarp12QueryHeap *Object = RxWarpObject<RxWarp12QueryHeap>(Heap);

    if (Object == nullptr || Index >= Object->Count)
        return;
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        const UINT64 *Counters = State->Device->Executor->Counters;
        UINT64 *Begin = &Object->Values[(SIZE_T)Index * RxWarpCountTotal];
        UINT64 *Result = &Object->Results[(SIZE_T)Index * RxWarpCountTotal];
        LARGE_INTEGER Now;

        if (Type == D3D12DDI_QUERY_TYPE_TIMESTAMP)
        {
            QueryPerformanceCounter(&Now);
            Result[0] = (UINT64)Now.QuadPart;
            return;
        }
        for (UINT Counter = 0; Counter < RxWarpCountTotal; Counter++)
            Result[Counter] = Counters[Counter] - Begin[Counter];
    });
}

static
VOID
APIENTRY
RxWarp12ResolveQueryData(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HQUERYHEAP Heap,
    _In_ D3D12DDI_QUERY_TYPE Type,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_ D3D12DDI_HRESOURCE Buffer,
    _In_ UINT64 Offset)
{
    RxWarpLock Lock;
    RxWarp12QueryHeap *Object = RxWarpObject<RxWarp12QueryHeap>(Heap);
    RxWarp12Resource *Target = RxWarp12GetResource(Buffer);

    if (Object == nullptr || Target == nullptr)
        return;
    RxWarp12Note(List, [=](RxWarp12Replay *State)
    {
        UINT64 Position = Offset;

        UNREFERENCED_PARAMETER(State);
        for (UINT Index = Start; Index < Start + Count && Index < Object->Count; Index++)
        {
            const UINT64 *Result = &Object->Results[(SIZE_T)Index * RxWarpCountTotal];
            UINT64 Values[11];
            UINT Taken;

            switch (Type)
            {
                case D3D12DDI_QUERY_TYPE_OCCLUSION:
                case D3D12DDI_QUERY_TYPE_TIMESTAMP:
                    Values[0] = (Type == D3D12DDI_QUERY_TYPE_OCCLUSION) ? Result[RxWarpCountSamplesPassed] : Result[0];
                    Taken = 1;
                    break;
                case D3D12DDI_QUERY_TYPE_BINARY_OCCLUSION:
                    Values[0] = Result[RxWarpCountSamplesPassed] != 0;
                    Taken = 1;
                    break;
                case D3D12DDI_QUERY_TYPE_PIPELINE_STATISTICS:
                    memcpy(Values, Result, sizeof(Values));
                    Taken = 11;
                    break;
                default:
                    Values[0] = Result[RxWarpCountSoWritten];
                    Values[1] = Result[RxWarpCountSoNeeded];
                    Taken = 2;
                    break;
            }
            if (Target->Storage.Memory == nullptr || Position + Taken * sizeof(UINT64) > Target->Width)
                return;
            memcpy(Target->Storage.Memory + Position, Values, Taken * sizeof(UINT64));
            Position += Taken * sizeof(UINT64);
        }
    });
}

VOID
RxWarp12FillDrawTable(
    _Inout_ D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 *List)
{
    List->pfnDrawInstanced = RxWarp12DrawInstanced;
    List->pfnDrawIndexedInstanced = RxWarp12DrawIndexedInstanced;
    List->pfnDispatch = RxWarp12Dispatch;
    List->pfnIaSetTopology = RxWarp12SetTopology;
    List->pfnRsSetViewports = RxWarp12SetViewports;
    List->pfnRsSetScissorRects = RxWarp12SetScissorRects;
    List->pfnOmSetBlendFactor = RxWarp12SetBlendFactor;
    List->pfnOmSetStencilRef = RxWarp12SetStencilRef;
    List->pfnOmSetFrontAndBackStencilRef = RxWarp12SetFrontAndBackStencilRef;
    List->pfnSetPipelineState = RxWarp12SetPipelineState;
    List->pfnSetDescriptorHeaps = RxWarp12SetDescriptorHeaps;
    List->pfnSetComputeRootSignature = RxWarp12SetComputeRootSignature;
    List->pfnSetGraphicsRootSignature = RxWarp12SetGraphicsRootSignature;
    List->pfnSetComputeRootDescriptorTable = RxWarp12SetRootTable<TRUE>;
    List->pfnSetGraphicsRootDescriptorTable = RxWarp12SetRootTable<FALSE>;
    List->pfnSetComputeRoot32BitConstant = RxWarp12SetRootConstant<TRUE>;
    List->pfnSetGraphicsRoot32BitConstant = RxWarp12SetRootConstant<FALSE>;
    List->pfnSetComputeRoot32BitConstants = RxWarp12SetRootConstants<TRUE>;
    List->pfnSetGraphicsRoot32BitConstants = RxWarp12SetRootConstants<FALSE>;
    List->pfnSetComputeRootConstantBufferView = RxWarp12SetRootView<TRUE>;
    List->pfnSetGraphicsRootConstantBufferView = RxWarp12SetRootView<FALSE>;
    List->pfnSetComputeRootShaderResourceView = RxWarp12SetRootView<TRUE>;
    List->pfnSetGraphicsRootShaderResourceView = RxWarp12SetRootView<FALSE>;
    List->pfnSetComputeRootUnorderedAccessView = RxWarp12SetRootView<TRUE>;
    List->pfnSetGraphicsRootUnorderedAccessView = RxWarp12SetRootView<FALSE>;
    List->pfnIASetIndexBuffer = RxWarp12SetIndexBuffer;
    List->pfnIASetVertexBuffers = RxWarp12SetVertexBuffers;
    List->pfnSOSetTargets = RxWarp12SetStreamTargets;
    List->pfnOMSetRenderTargets = RxWarp12SetRenderTargets;
    List->pfnSetPredication = RxWarp12SetPredication;
    List->pfnExecuteBundle = RxWarp12ExecuteBundle;
    List->pfnExecuteIndirect = RxWarp12ExecuteIndirect;
    List->pfnBeginQuery = RxWarp12BeginQuery;
    List->pfnEndQuery = RxWarp12EndQuery;
    List->pfnResolveQueryData = RxWarp12ResolveQueryData;
}
