/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     State objects, shaders and pipeline bindings
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"

static
RxWarpDevice *
RxWarpGetDevice(
    _In_ D3D10DDI_HDEVICE Device)
{
    return RxWarpObject<RxWarpDevice>(Device);
}

/* Creates a driver object holding a copy of Desc, or reports out of memory */
template <typename Object, typename Handle>
static
Object *
RxWarpNewObject(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ Handle Value)
{
    Object *Instance = new (std::nothrow) Object();

    if (Instance == nullptr)
    {
        RxWarpGetDevice(Device)->SetError(E_OUTOFMEMORY);
        return nullptr;
    }
    RxWarpBind(Value, Instance);
    return Instance;
}

template <typename Object, typename Handle>
static
VOID
RxWarpDeleteObject(
    _In_ Handle Value)
{
    delete RxWarpObject<Object>(Value);
    RxWarpBind<Object>(Value, nullptr);
}

VOID
APIENTRY
RxWarpDdi::CreateBlendState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D11_1_DDI_BLEND_DESC *Desc,
    _In_ D3D10DDI_HBLENDSTATE State,
    _In_ D3D10DDI_HRTBLENDSTATE RuntimeState)
{
    RxWarpLock Lock;
    RxWarpBlendState *Object;

    UNREFERENCED_PARAMETER(RuntimeState);
    Object = RxWarpNewObject<RxWarpBlendState>(Device, State);
    if (Object == nullptr)
        return;
    Object->Desc = *Desc;

    /* Without independent blending every target follows the first */
    if (!Object->Desc.IndependentBlendEnable)
    {
        for (UINT Index = 1; Index < 8; Index++)
            Object->Desc.RenderTarget[Index] = Object->Desc.RenderTarget[0];
    }
}

VOID
APIENTRY
RxWarpDdi::DestroyBlendState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HBLENDSTATE State)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    RxWarpDeleteObject<RxWarpBlendState>(State);
}

VOID
APIENTRY
RxWarpDdi::CreateDepthStencilState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D10_DDI_DEPTH_STENCIL_DESC *Desc,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE State,
    _In_ D3D10DDI_HRTDEPTHSTENCILSTATE RuntimeState)
{
    RxWarpLock Lock;
    RxWarpDepthStencilState *Object;

    UNREFERENCED_PARAMETER(RuntimeState);
    Object = RxWarpNewObject<RxWarpDepthStencilState>(Device, State);
    if (Object != nullptr)
        Object->Desc = *Desc;
}

VOID
APIENTRY
RxWarpDdi::DestroyDepthStencilState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE State)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    RxWarpDeleteObject<RxWarpDepthStencilState>(State);
}

VOID
APIENTRY
RxWarpDdi::CreateRasterizerState2(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3DWDDM2_0DDI_RASTERIZER_DESC *Desc,
    _In_ D3D10DDI_HRASTERIZERSTATE State,
    _In_ D3D10DDI_HRTRASTERIZERSTATE RuntimeState)
{
    RxWarpLock Lock;
    RxWarpRasterizerState *Object;

    UNREFERENCED_PARAMETER(RuntimeState);
    Object = RxWarpNewObject<RxWarpRasterizerState>(Device, State);
    if (Object != nullptr)
        Object->Desc = *Desc;
}

/* The 11.1 description is the 2.0 one without conservative rasterization */
VOID
APIENTRY
RxWarpDdi::CreateRasterizerState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D11_1_DDI_RASTERIZER_DESC *Desc,
    _In_ D3D10DDI_HRASTERIZERSTATE State,
    _In_ D3D10DDI_HRTRASTERIZERSTATE RuntimeState)
{
    D3DWDDM2_0DDI_RASTERIZER_DESC Wide = {};

    memcpy(&Wide, Desc, sizeof(*Desc));
    CreateRasterizerState2(Device, &Wide, State, RuntimeState);
}

VOID
APIENTRY
RxWarpDdi::DestroyRasterizerState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRASTERIZERSTATE State)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    RxWarpDeleteObject<RxWarpRasterizerState>(State);
}

VOID
APIENTRY
RxWarpDdi::CreateSampler(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D10_DDI_SAMPLER_DESC *Desc,
    _In_ D3D10DDI_HSAMPLER Sampler,
    _In_ D3D10DDI_HRTSAMPLER RuntimeSampler)
{
    RxWarpLock Lock;
    RxWarpSampler *Object;

    UNREFERENCED_PARAMETER(RuntimeSampler);
    Object = RxWarpNewObject<RxWarpSampler>(Device, Sampler);
    if (Object != nullptr)
        Object->State.Desc = *Desc;
}

VOID
APIENTRY
RxWarpDdi::DestroySampler(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HSAMPLER Sampler)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    RxWarpDeleteObject<RxWarpSampler>(Sampler);
}

VOID
APIENTRY
RxWarpDdi::CreateElementLayout(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D10DDIARG_CREATEELEMENTLAYOUT *Args,
    _In_ D3D10DDI_HELEMENTLAYOUT Layout,
    _In_ D3D10DDI_HRTELEMENTLAYOUT RuntimeLayout)
{
    RxWarpLock Lock;
    RxWarpElementLayout *Object;

    UNREFERENCED_PARAMETER(RuntimeLayout);
    Object = RxWarpNewObject<RxWarpElementLayout>(Device, Layout);
    if (Object != nullptr && Args->NumElements != 0)
        Object->Elements.assign(Args->pVertexElements, Args->pVertexElements + Args->NumElements);
}

VOID
APIENTRY
RxWarpDdi::DestroyElementLayout(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HELEMENTLAYOUT Layout)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    RxWarpDeleteObject<RxWarpElementLayout>(Layout);
}

static
VOID
RxWarpCreateShader(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const UINT *Code,
    _In_ D3D10DDI_HSHADER Shader)
{
    RxWarpShader *Object = RxWarpNewObject<RxWarpShader>(Device, Shader);
    HRESULT Result;

    if (Object == nullptr)
        return;
    Result = Object->Initialize(Code);
    if (FAILED(Result))
        RX_WARP_TRACE("WARP: shader decode failed %08lx\n", Result);
}

VOID
APIENTRY
RxWarpDdi::CreateVertexShader(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const UINT *Code,
    _In_ D3D10DDI_HSHADER Shader,
    _In_ D3D10DDI_HRTSHADER RuntimeShader,
    _In_ const D3D11_1DDIARG_STAGE_IO_SIGNATURES *Signatures)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(RuntimeShader);
    UNREFERENCED_PARAMETER(Signatures);
    RxWarpCreateShader(Device, Code, Shader);
}

VOID
APIENTRY
RxWarpDdi::CreatePixelShader(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const UINT *Code,
    _In_ D3D10DDI_HSHADER Shader,
    _In_ D3D10DDI_HRTSHADER RuntimeShader,
    _In_ const D3D11_1DDIARG_STAGE_IO_SIGNATURES *Signatures)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(RuntimeShader);
    UNREFERENCED_PARAMETER(Signatures);
    RxWarpCreateShader(Device, Code, Shader);
}

VOID
APIENTRY
RxWarpDdi::DestroyShader(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HSHADER Shader)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    RxWarpDeleteObject<RxWarpShader>(Shader);
}

VOID
APIENTRY
RxWarpDdi::IaSetInputLayout(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HELEMENTLAYOUT Layout)
{
    RxWarpLock Lock;

    RxWarpGetDevice(Device)->Pipeline.Layout = RxWarpObject<RxWarpElementLayout>(Layout);
}

VOID
APIENTRY
RxWarpDdi::IaSetVertexBuffers(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HRESOURCE *Buffers,
    _In_reads_(Count) const UINT *Strides,
    _In_reads_(Count) const UINT *Offsets)
{
    RxWarpLock Lock;
    RX_WARP_PIPELINE *Pipeline = &RxWarpGetDevice(Device)->Pipeline;

    for (UINT Index = 0; Index < Count && StartSlot + Index < 32; Index++)
    {
        RX_WARP_BUFFER_BINDING *Binding = &Pipeline->VertexBuffers[StartSlot + Index];

        Binding->Resource = RxWarpObject<RxWarpResource>(Buffers[Index]);
        Binding->Stride = Strides[Index];
        Binding->Offset = Offsets[Index];
    }
}

VOID
APIENTRY
RxWarpDdi::IaSetIndexBuffer(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Buffer,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Offset)
{
    RxWarpLock Lock;
    RX_WARP_PIPELINE *Pipeline = &RxWarpGetDevice(Device)->Pipeline;

    Pipeline->IndexBuffer = RxWarpObject<RxWarpResource>(Buffer);
    Pipeline->IndexFormat = Format;
    Pipeline->IndexOffset = Offset;
}

VOID
APIENTRY
RxWarpDdi::IaSetTopology(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10_DDI_PRIMITIVE_TOPOLOGY Topology)
{
    RxWarpLock Lock;

    RxWarpGetDevice(Device)->Pipeline.Topology = Topology;
}

static
VOID
RxWarpSetShader(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ RX_WARP_STAGE Stage,
    _In_ D3D10DDI_HSHADER Shader)
{
    RxWarpLock Lock;

    RxWarpGetDevice(Device)->Pipeline.Stages[Stage].Shader = RxWarpObject<RxWarpShader>(Shader);
}

VOID
APIENTRY
RxWarpDdi::VsSetShader(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HSHADER Shader)
{
    RxWarpSetShader(Device, RxWarpStageVertex, Shader);
}

VOID
APIENTRY
RxWarpDdi::PsSetShader(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HSHADER Shader)
{
    RxWarpSetShader(Device, RxWarpStagePixel, Shader);
}

static
VOID
RxWarpSetConstantBuffers(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ RX_WARP_STAGE Stage,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HRESOURCE *Buffers,
    _In_reads_opt_(Count) const UINT *First,
    _In_reads_opt_(Count) const UINT *Constants)
{
    RxWarpLock Lock;
    RX_WARP_STAGE_STATE *State = &RxWarpGetDevice(Device)->Pipeline.Stages[Stage];

    for (UINT Index = 0; Index < Count && StartSlot + Index < RxWarpConstantBufferSlots; Index++)
    {
        RX_WARP_CONSTANT_BINDING *Binding = &State->ConstantBuffers[StartSlot + Index];

        Binding->Resource = RxWarpObject<RxWarpResource>(Buffers[Index]);
        Binding->First = (First != nullptr) ? First[Index] : 0;
        Binding->Count = (Constants != nullptr) ? Constants[Index] : UINT_MAX;
    }
}

VOID
APIENTRY
RxWarpDdi::VsSetConstantBuffers(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HRESOURCE *Buffers,
    _In_reads_opt_(Count) const UINT *First,
    _In_reads_opt_(Count) const UINT *Constants)
{
    RxWarpSetConstantBuffers(Device, RxWarpStageVertex, StartSlot, Count, Buffers, First, Constants);
}

VOID
APIENTRY
RxWarpDdi::PsSetConstantBuffers(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HRESOURCE *Buffers,
    _In_reads_opt_(Count) const UINT *First,
    _In_reads_opt_(Count) const UINT *Constants)
{
    RxWarpSetConstantBuffers(Device, RxWarpStagePixel, StartSlot, Count, Buffers, First, Constants);
}

static
VOID
RxWarpSetViews(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ RX_WARP_STAGE Stage,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HSHADERRESOURCEVIEW *Views)
{
    RxWarpLock Lock;
    RX_WARP_STAGE_STATE *State = &RxWarpGetDevice(Device)->Pipeline.Stages[Stage];

    for (UINT Index = 0; Index < Count && StartSlot + Index < 128; Index++)
        State->Views[StartSlot + Index] = RxWarpObject<RxWarpView>(Views[Index]);
}

VOID
APIENTRY
RxWarpDdi::VsSetShaderResources(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HSHADERRESOURCEVIEW *Views)
{
    RxWarpSetViews(Device, RxWarpStageVertex, StartSlot, Count, Views);
}

VOID
APIENTRY
RxWarpDdi::PsSetShaderResources(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HSHADERRESOURCEVIEW *Views)
{
    RxWarpSetViews(Device, RxWarpStagePixel, StartSlot, Count, Views);
}

static
VOID
RxWarpSetSamplers(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ RX_WARP_STAGE Stage,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HSAMPLER *Samplers)
{
    RxWarpLock Lock;
    RX_WARP_STAGE_STATE *State = &RxWarpGetDevice(Device)->Pipeline.Stages[Stage];

    for (UINT Index = 0; Index < Count && StartSlot + Index < 16; Index++)
        State->Samplers[StartSlot + Index] = RxWarpObject<RxWarpSampler>(Samplers[Index]);
}

VOID
APIENTRY
RxWarpDdi::VsSetSamplers(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HSAMPLER *Samplers)
{
    RxWarpSetSamplers(Device, RxWarpStageVertex, StartSlot, Count, Samplers);
}

VOID
APIENTRY
RxWarpDdi::PsSetSamplers(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HSAMPLER *Samplers)
{
    RxWarpSetSamplers(Device, RxWarpStagePixel, StartSlot, Count, Samplers);
}

VOID
APIENTRY
RxWarpDdi::SetBlendState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HBLENDSTATE State,
    _In_ const FLOAT Factor[4],
    _In_ UINT SampleMask)
{
    RxWarpLock Lock;
    RX_WARP_PIPELINE *Pipeline = &RxWarpGetDevice(Device)->Pipeline;

    Pipeline->Blend = RxWarpObject<RxWarpBlendState>(State);
    memcpy(Pipeline->BlendFactor, Factor, sizeof(Pipeline->BlendFactor));
    Pipeline->SampleMask = SampleMask;
}

VOID
APIENTRY
RxWarpDdi::SetDepthStencilState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE State,
    _In_ UINT StencilRef)
{
    RxWarpLock Lock;
    RX_WARP_PIPELINE *Pipeline = &RxWarpGetDevice(Device)->Pipeline;

    Pipeline->DepthStencil = RxWarpObject<RxWarpDepthStencilState>(State);
    Pipeline->StencilRef = StencilRef;
}

VOID
APIENTRY
RxWarpDdi::SetRasterizerState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRASTERIZERSTATE State)
{
    RxWarpLock Lock;

    RxWarpGetDevice(Device)->Pipeline.Rasterizer = RxWarpObject<RxWarpRasterizerState>(State);
}

VOID
APIENTRY
RxWarpDdi::SetViewports(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT Count,
    _In_ UINT ClearCount,
    _In_reads_(Count) const D3D10_DDI_VIEWPORT *Viewports)
{
    RxWarpLock Lock;
    RX_WARP_PIPELINE *Pipeline = &RxWarpGetDevice(Device)->Pipeline;

    UNREFERENCED_PARAMETER(ClearCount);
    if (Count > 16)
        Count = 16;
    memcpy(Pipeline->Viewports, Viewports, Count * sizeof(*Viewports));
    Pipeline->ViewportCount = Count;
}

VOID
APIENTRY
RxWarpDdi::SetScissorRects(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT Count,
    _In_ UINT ClearCount,
    _In_reads_(Count) const D3D10_DDI_RECT *Rects)
{
    RxWarpLock Lock;
    RX_WARP_PIPELINE *Pipeline = &RxWarpGetDevice(Device)->Pipeline;

    UNREFERENCED_PARAMETER(ClearCount);
    if (Count > 16)
        Count = 16;
    memcpy(Pipeline->Scissors, Rects, Count * sizeof(*Rects));
    Pipeline->ScissorCount = Count;
}

VOID
APIENTRY
RxWarpDdi::SetRenderTargets(
    _In_ D3D10DDI_HDEVICE Device,
    _In_reads_(Count) const D3D10DDI_HRENDERTARGETVIEW *Targets,
    _In_ UINT Count,
    _In_ UINT ClearCount,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW DepthStencil,
    _In_opt_ const D3D11DDI_HUNORDEREDACCESSVIEW *Uavs,
    _In_opt_ const UINT *InitialCounts,
    _In_ UINT UavStart,
    _In_ UINT UavCount,
    _In_ UINT UavRangeStart,
    _In_ UINT UavRangeSize)
{
    RxWarpLock Lock;
    RX_WARP_PIPELINE *Pipeline = &RxWarpGetDevice(Device)->Pipeline;

    UNREFERENCED_PARAMETER(ClearCount);
    UNREFERENCED_PARAMETER(UavRangeStart);
    UNREFERENCED_PARAMETER(UavRangeSize);

    /* Pixel shader UAVs share the output slots after the render targets */
    RxWarpBindUavs(&Pipeline->Stages[RxWarpStagePixel], UavStart, UavCount, Uavs, InitialCounts);

    ZeroMemory(Pipeline->RenderTargets, sizeof(Pipeline->RenderTargets));
    for (UINT Index = 0; Index < Count && Index < 8; Index++)
        Pipeline->RenderTargets[Index] = RxWarpObject<RxWarpView>(Targets[Index]);
    Pipeline->RenderTargetCount = (Count < 8) ? Count : 8;
    Pipeline->DepthStencilView = RxWarpObject<RxWarpView>(DepthStencil);
}

VOID
RxWarpBindStage(
    _In_ const RX_WARP_STAGE_STATE *State,
    _Out_ RX_WARP_STAGE_BINDINGS *Bindings)
{
    ZeroMemory(Bindings, sizeof(*Bindings));
    for (UINT Slot = 0; Slot < RxWarpConstantBufferSlots; Slot++)
    {
        const RX_WARP_CONSTANT_BINDING *Binding = &State->ConstantBuffers[Slot];
        UINT Available;

        if (Binding->Resource == nullptr || Binding->Resource->Memory == nullptr)
            continue;
        Available = (UINT)(Binding->Resource->MemorySize / 16);
        if (Binding->First >= Available)
            continue;
        Available -= Binding->First;
        Bindings->ConstantBuffer[Slot] = Binding->Resource->Memory + (SIZE_T)Binding->First * 16;
        Bindings->ConstantCount[Slot] = (Binding->Count < Available) ? Binding->Count : Available;
    }
    memcpy(Bindings->Views, State->Views, sizeof(Bindings->Views));
    memcpy(Bindings->Uavs, State->Uavs, sizeof(Bindings->Uavs));
    for (UINT Slot = 0; Slot < 16; Slot++)
        Bindings->Samplers[Slot] = State->Samplers[Slot] ? &State->Samplers[Slot]->State : nullptr;
}
