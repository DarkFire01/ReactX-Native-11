/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pipeline binding, drawing and clearing on the immediate context
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "queries.hpp"
#include "shaders.hpp"
#include "states.hpp"
#include "views.hpp"

#include <algorithm>
#include <cmath>

using RxFuncs = D3DWDDM1_3DDI_DEVICEFUNCS;

/* The per stage driver entries, in RxStage order */
struct RxStageEntries
{
    PFND3D10DDI_SETSHADER RxFuncs::* SetShader;
    PFND3D11_1DDI_SETCONSTANTBUFFERS RxFuncs::* SetConstantBuffers;
    PFND3D10DDI_SETSHADERRESOURCES RxFuncs::* SetShaderResources;
    PFND3D10DDI_SETSAMPLERS RxFuncs::* SetSamplers;
};

static const RxStageEntries StageEntries[RxStageCount] =
{
    { &RxFuncs::pfnVsSetShader, &RxFuncs::pfnVsSetConstantBuffers, &RxFuncs::pfnVsSetShaderResources, &RxFuncs::pfnVsSetSamplers },
    { &RxFuncs::pfnHsSetShader, &RxFuncs::pfnHsSetConstantBuffers, &RxFuncs::pfnHsSetShaderResources, &RxFuncs::pfnHsSetSamplers },
    { &RxFuncs::pfnDsSetShader, &RxFuncs::pfnDsSetConstantBuffers, &RxFuncs::pfnDsSetShaderResources, &RxFuncs::pfnDsSetSamplers },
    { &RxFuncs::pfnGsSetShader, &RxFuncs::pfnGsSetConstantBuffers, &RxFuncs::pfnGsSetShaderResources, &RxFuncs::pfnGsSetSamplers },
    { &RxFuncs::pfnPsSetShader, &RxFuncs::pfnPsSetConstantBuffers, &RxFuncs::pfnPsSetShaderResources, &RxFuncs::pfnPsSetSamplers },
    { &RxFuncs::pfnCsSetShader, &RxFuncs::pfnCsSetConstantBuffers, &RxFuncs::pfnCsSetShaderResources, &RxFuncs::pfnCsSetSamplers },
};

/* Clamps a resend range; UINT_MAX stretches it to the highest slot in use */
static
UINT
RxResendCount(
    _In_ UINT Start,
    _In_ UINT Count,
    _In_ UINT HighWater,
    _In_ UINT Slots)
{
    if (Start >= Slots)
        return 0;
    if (Count == UINT_MAX)
        return (HighWater > Start) ? HighWater - Start : 0;
    return std::min(Count, Slots - Start);
}

/* Unbinding past the old high water mark tells the driver to forget those slots too */
static
UINT
RxClearCount(
    _In_ UINT Previous,
    _In_ UINT Current)
{
    return Previous - std::min(Previous, Current);
}

static
FLOAT
RxFlushDenormal(
    _In_ FLOAT Value)
{
    return (std::fpclassify(Value) == FP_SUBNORMAL) ? 0.0f : Value;
}

RxPipelineState::RxPipelineState() = default;
RxPipelineState::~RxPipelineState() = default;

/* Sending */

VOID
RxImmediateContext::SendShader(
    _In_ RxStage Stage)
{
    RxContextGuard Guard(Device);
    const RxFuncs& Funcs = Device.GetDeviceFuncs();

    (Funcs.*StageEntries[(UINT)Stage].SetShader)(Device.GetDriverDevice(), Pipeline.Stages[(UINT)Stage].ShaderHandle);
}

VOID
RxImmediateContext::SendConstantBuffers(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count)
{
    RxContextGuard Guard(Device);
    const RxFuncs& Funcs = Device.GetDeviceFuncs();
    RxStageState& State = Pipeline.Stages[(UINT)Stage];

    BOOLEAN HasRanges = FALSE;
    UINT Index;

    Count = RxResendCount(Start, Count, State.ConstantBufferCount, (UINT)State.ConstantBufferHandles.size());
    if (Count == 0)
        return;

    for (Index = Start; Index < Start + Count; Index++)
        HasRanges |= State.ConstantCounts[Index] != 0;

    /* Whole buffers need no first constant or constant count */
    (Funcs.*StageEntries[(UINT)Stage].SetConstantBuffers)(Device.GetDriverDevice(), Start, Count,
                                                          &State.ConstantBufferHandles[Start],
                                                          HasRanges ? &State.FirstConstants[Start] : nullptr,
                                                          HasRanges ? &State.ConstantCounts[Start] : nullptr);
}

VOID
RxImmediateContext::SendShaderResources(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count)
{
    RxContextGuard Guard(Device);
    const RxFuncs& Funcs = Device.GetDeviceFuncs();
    RxStageState& State = Pipeline.Stages[(UINT)Stage];

    Count = RxResendCount(Start, Count, State.ShaderResourceCount, (UINT)State.ShaderResourceHandles.size());
    if (Count == 0)
        return;

    (Funcs.*StageEntries[(UINT)Stage].SetShaderResources)(Device.GetDriverDevice(), Start, Count,
                                                          &State.ShaderResourceHandles[Start]);
}

VOID
RxImmediateContext::SendSamplers(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count)
{
    RxContextGuard Guard(Device);
    const RxFuncs& Funcs = Device.GetDeviceFuncs();
    RxStageState& State = Pipeline.Stages[(UINT)Stage];

    Count = RxResendCount(Start, Count, State.SamplerCount, (UINT)State.SamplerHandles.size());
    if (Count == 0)
        return;

    (Funcs.*StageEntries[(UINT)Stage].SetSamplers)(Device.GetDriverDevice(), Start, Count, &State.SamplerHandles[Start]);
}

VOID
RxImmediateContext::SendInputLayout()
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnIaSetInputLayout(Device.GetDriverDevice(), Pipeline.InputLayoutHandle);
}

VOID
RxImmediateContext::SendVertexBuffers(
    _In_ UINT Start,
    _In_ UINT Count)
{
    RxContextGuard Guard(Device);
    Count = RxResendCount(Start, Count, Pipeline.VertexBufferCount, (UINT)Pipeline.VertexBufferHandles.size());
    if (Count == 0)
        return;

    Device.GetDeviceFuncs().pfnIaSetVertexBuffers(Device.GetDriverDevice(), Start, Count,
                                                  &Pipeline.VertexBufferHandles[Start], &Pipeline.Strides[Start],
                                                  &Pipeline.Offsets[Start]);
}

VOID
RxImmediateContext::SendIndexBuffer()
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnIaSetIndexBuffer(Device.GetDriverDevice(), Pipeline.IndexBufferHandle,
                                                Pipeline.IndexFormat, Pipeline.IndexOffset);
}

VOID
RxImmediateContext::SendTopology()
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnIaSetTopology(Device.GetDriverDevice(), (D3D10_DDI_PRIMITIVE_TOPOLOGY)Pipeline.Topology);
}

VOID
RxImmediateContext::SendRenderTargets(
    _In_ UINT ClearSlots)
{
    RxContextGuard Guard(Device);
    SendOutputs(ClearSlots, Pipeline.OutputUavStart, Pipeline.OutputUavEnd - Pipeline.OutputUavStart, nullptr);
}

VOID
RxImmediateContext::SendOutputs(
    _In_ UINT ClearSlots,
    _In_ UINT UavRangeStart,
    _In_ UINT UavRangeSize,
    _In_reads_opt_(RxUavSlotCount) const UINT* Counts)
{
    RxContextGuard Guard(Device);
    UINT UavStart = Pipeline.OutputUavStart;
    UINT UavCount = Pipeline.OutputUavEnd - UavStart;

    Device.GetDeviceFuncs().pfnSetRenderTargets(Device.GetDriverDevice(), Pipeline.RenderTargetHandles.data(),
                                                Pipeline.RenderTargetCount, ClearSlots,
                                                Pipeline.DepthStencilViewHandle,
                                                UavCount ? &Pipeline.OutputUavHandles[UavStart] : nullptr,
                                                UavCount ? (Counts ? Counts : RxKeepCounters.data()) : nullptr,
                                                UavStart, UavCount, UavRangeStart, UavRangeSize);
}

VOID
RxImmediateContext::SendBlendState()
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnSetBlendState(Device.GetDriverDevice(), Pipeline.BlendHandle,
                                             Pipeline.BlendFactor.data(), Pipeline.SampleMask);
}

VOID
RxImmediateContext::SendDepthStencilState()
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnSetDepthStencilState(Device.GetDriverDevice(), Pipeline.DepthStencilHandle,
                                                    Pipeline.StencilRef);
}

VOID
RxImmediateContext::SendRasterizerState()
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnSetRasterizerState(Device.GetDriverDevice(), Pipeline.RasterizerHandle);
}

VOID
RxImmediateContext::SendViewports(
    _In_ UINT ClearViewports)
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnSetViewports(Device.GetDriverDevice(), Pipeline.ViewportCount, ClearViewports,
                                            Pipeline.Viewports.data());
}

VOID
RxImmediateContext::SendScissorRects(
    _In_ UINT ClearRects)
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnSetScissorRects(Device.GetDriverDevice(), Pipeline.ScissorCount, ClearRects,
                                               Pipeline.Scissors.data());
}

/* Defaults and teardown */

VOID
RxImmediateContext::LoadPipelineDefaults()
{
    RxContextGuard Guard(Device);
    ID3D11SamplerState* NoSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    UINT Stage;

    OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    OMSetDepthStencilState(nullptr, 0);
    RSSetState(nullptr);
    for (Stage = 0; Stage < RxStageCount; Stage++)
        SetSamplers((RxStage)Stage, 0, _countof(NoSamplers), NoSamplers);
}

VOID
RxImmediateContext::EmptyDriverStates()
{
    RxContextGuard Guard(Device);
    static const FLOAT DefaultFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = Device.GetDeviceFuncs();
    D3D10DDI_HDEVICE Driver = Device.GetDriverDevice();

    Funcs.pfnSetBlendState(Driver, D3D10DDI_HBLENDSTATE{}, DefaultFactor, 0xFFFFFFFF);
    Funcs.pfnSetDepthStencilState(Driver, D3D10DDI_HDEPTHSTENCILSTATE{}, 0);
    Funcs.pfnSetRasterizerState(Driver, D3D10DDI_HRASTERIZERSTATE{});
}

VOID
RxImmediateContext::ReleaseBindings()
{
    RxContextGuard Guard(Device);
    Pipeline = {};
    ReleaseContextState();
}

/* Hazards */

BOOLEAN
RxImmediateContext::IsBoundForOutput(
    _In_ const DdiResource* Resource) const
{
    RxContextGuard Guard(Device);
    UINT Index;

    for (Index = 0; Index < Pipeline.RenderTargetCount; Index++)
    {
        if (Pipeline.RenderTargets[Index] && Pipeline.RenderTargets[Index]->GetBacking() == Resource)
            return TRUE;
    }

    for (Index = Pipeline.OutputUavStart; Index < Pipeline.OutputUavEnd; Index++)
    {
        if (Pipeline.OutputUavs[Index] && Pipeline.OutputUavs[Index]->GetBacking() == Resource)
            return TRUE;
    }

    for (Index = 0; Index < Pipeline.ComputeUavCount; Index++)
    {
        if (Pipeline.ComputeUavs[Index] && Pipeline.ComputeUavs[Index]->GetBacking() == Resource)
            return TRUE;
    }

    for (Index = 0; Index < Pipeline.StreamTargetCount; Index++)
    {
        if (Pipeline.StreamTargets[Index] && &Pipeline.StreamTargets[Index]->GetDdiResource() == Resource)
            return TRUE;
    }

    return Pipeline.DepthStencilView && Pipeline.DepthStencilView->GetBacking() == Resource;
}

VOID
RxImmediateContext::NoteOutput(
    _In_ DdiResource* Resource,
    _In_ RxOutput Kind)
{
    RxContextGuard Guard(Device);
    UnbindInputsOf(Resource);

    /* The newer output wins over the others */
    if (Kind == RxOutput::Merger)
    {
        ReleaseComputeUavsOf(Resource, 0, 0);
        ReleaseStreamTargetsOf(Resource);
    }
    else
    {
        ReleaseMergerOutputsOf(Resource);
    }

    Resource->NoteWrite();
}

VOID
RxImmediateContext::ReleaseMergerOutputsOf(
    _In_ const DdiResource* Resource)
{
    RxContextGuard Guard(Device);
    BOOLEAN Changed = FALSE;
    UINT Index;

    for (Index = 0; Index < Pipeline.RenderTargetCount; Index++)
    {
        if (Pipeline.RenderTargets[Index] && Pipeline.RenderTargets[Index]->GetBacking() == Resource)
        {
            Pipeline.RenderTargets[Index].Reset();
            Pipeline.RenderTargetHandles[Index] = {};
            Changed = TRUE;
        }
    }

    for (Index = Pipeline.OutputUavStart; Index < Pipeline.OutputUavEnd; Index++)
    {
        if (Pipeline.OutputUavs[Index] && Pipeline.OutputUavs[Index]->GetBacking() == Resource)
        {
            Pipeline.OutputUavs[Index].Reset();
            Pipeline.OutputUavHandles[Index] = {};
            Changed = TRUE;
        }
    }

    if (Changed)
        SendRenderTargets(0);
}

VOID
RxImmediateContext::ReleaseComputeUavsOf(
    _In_ const DdiResource* Resource,
    _In_ UINT KeepStart,
    _In_ UINT KeepCount)
{
    RxContextGuard Guard(Device);
    UINT First = UINT_MAX;
    UINT Last = 0;
    UINT Index;

    for (Index = 0; Index < Pipeline.ComputeUavCount; Index++)
    {
        if (Index >= KeepStart && Index - KeepStart < KeepCount)
            continue;
        if (!Pipeline.ComputeUavs[Index] || Pipeline.ComputeUavs[Index]->GetBacking() != Resource)
            continue;

        Pipeline.ComputeUavs[Index].Reset();
        Pipeline.ComputeUavHandles[Index] = {};
        First = std::min(First, Index);
        Last = Index;
    }

    if (First != UINT_MAX)
        SendComputeUavs(First, Last - First + 1);
}

VOID
RxImmediateContext::ReleaseStreamTargetsOf(
    _In_ const DdiResource* Resource)
{
    RxContextGuard Guard(Device);
    BOOLEAN Changed = FALSE;
    UINT Index;

    for (Index = 0; Index < Pipeline.StreamTargetCount; Index++)
    {
        if (Pipeline.StreamTargets[Index] && &Pipeline.StreamTargets[Index]->GetDdiResource() == Resource)
        {
            Pipeline.StreamTargets[Index].Reset();
            Pipeline.StreamTargetHandles[Index] = {};
            Changed = TRUE;
        }
    }

    if (Changed)
        SendStreamTargets(0);
}

UINT
RxImmediateContext::GetUavSlotCount() const
{
    RxContextGuard Guard(Device);
    return (Device.GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1) ? D3D11_1_UAV_SLOT_COUNT
                                                                : D3D11_PS_CS_UAV_REGISTER_COUNT;
}

VOID
RxImmediateContext::UnbindInputsOf(
    _In_ const DdiResource* Resource)
{
    RxContextGuard Guard(Device);
    UINT Stage;
    UINT Slot;

    for (Stage = 0; Stage < RxStageCount; Stage++)
    {
        RxStageState& State = Pipeline.Stages[Stage];

        for (Slot = 0; Slot < State.ShaderResourceCount; Slot++)
        {
            if (!State.ShaderResources[Slot] || State.ShaderResources[Slot]->GetBacking() != Resource)
                continue;

            State.ShaderResources[Slot].Reset();
            State.ShaderResourceHandles[Slot] = {};
            SendShaderResources((RxStage)Stage, Slot, 1);
        }
    }

    for (Slot = 0; Slot < Pipeline.VertexBufferCount; Slot++)
    {
        if (!Pipeline.VertexBuffers[Slot] || &Pipeline.VertexBuffers[Slot]->GetDdiResource() != Resource)
            continue;

        Pipeline.VertexBuffers[Slot].Reset();
        Pipeline.VertexBufferHandles[Slot] = {};
        Pipeline.Strides[Slot] = 0;
        Pipeline.Offsets[Slot] = 0;
        SendVertexBuffers(Slot, 1);
    }

    if (Pipeline.IndexBuffer && &Pipeline.IndexBuffer->GetDdiResource() == Resource)
    {
        Pipeline.IndexBuffer.Reset();
        Pipeline.IndexBufferHandle = {};
        SendIndexBuffer();
    }
}

/* Shared stage setters */

VOID
RxImmediateContext::SetShader(
    _In_ RxStage Stage,
    _In_opt_ RxChildLifetime* Shader,
    _In_opt_ ID3D11DeviceChild* Object,
    _In_ D3D10DDI_HSHADER Handle)
{
    RxContextGuard Guard(Device);
    RxStageState& State = Pipeline.Stages[(UINT)Stage];

    State.Shader = RxBound<RxChildLifetime>(Shader);
    State.ShaderObject = Object;
    if (State.ShaderHandle.pDrvPrivate == Handle.pDrvPrivate)
        return;

    State.ShaderHandle = Handle;
    SendShader(Stage);
}

VOID
RxImmediateContext::SetConstantBuffers(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_opt_(Count) ID3D11Buffer* const* Buffers)
{
    RxContextGuard Guard(Device);
    RxStageState& State = Pipeline.Stages[(UINT)Stage];
    RxBuffer* Buffer;
    UINT Index;

    if (Start >= State.ConstantBuffers.size() || Count > State.ConstantBuffers.size() - Start)
        return;

    for (Index = 0; Index < Count; Index++)
    {
        Buffer = Buffers ? RxUnwrap<RxBuffer>(Buffers[Index]) : nullptr;
        if (Buffer != nullptr && IsBoundForOutput(&Buffer->GetDdiResource()))
            Buffer = nullptr;
        State.ConstantBuffers[Start + Index] = RxBound<RxBuffer>(Buffer);
        State.ConstantBufferHandles[Start + Index] = Buffer ? Buffer->GetDdiResource().GetHandle() : D3D10DDI_HRESOURCE{};
        State.FirstConstants[Start + Index] = 0;
        State.ConstantCounts[Start + Index] = 0;
    }

    State.ConstantBufferCount = std::max(State.ConstantBufferCount, Start + Count);
    SendConstantBuffers(Stage, Start, Count);
}

VOID
RxImmediateContext::SetShaderResources(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_opt_(Count) ID3D11ShaderResourceView* const* Views)
{
    RxContextGuard Guard(Device);
    RxStageState& State = Pipeline.Stages[(UINT)Stage];
    RxShaderResourceView* View;
    UINT Index;

    if (Start >= State.ShaderResources.size() || Count > State.ShaderResources.size() - Start)
        return;

    for (Index = 0; Index < Count; Index++)
    {
        View = Views ? RxUnwrap<RxShaderResourceView>(Views[Index]) : nullptr;

        /* What the output merger is writing can not be read at the same time */
        if (View != nullptr && IsBoundForOutput(View->GetBacking()))
            View = nullptr;
        if (View != nullptr)
            View->NoteRead();

        State.ShaderResources[Start + Index] = RxBound<RxShaderResourceView>(View);
        State.ShaderResourceHandles[Start + Index] = View ? View->GetHandle() : D3D10DDI_HSHADERRESOURCEVIEW{};
    }

    State.ShaderResourceCount = std::max(State.ShaderResourceCount, Start + Count);
    SendShaderResources(Stage, Start, Count);
}

VOID
RxImmediateContext::SetSamplers(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_opt_(Count) ID3D11SamplerState* const* Samplers)
{
    RxContextGuard Guard(Device);
    RxStageState& State = Pipeline.Stages[(UINT)Stage];
    RxSamplerState* Sampler;
    UINT Index;

    if (Start >= State.Samplers.size() || Count > State.Samplers.size() - Start)
        return;

    for (Index = 0; Index < Count; Index++)
    {
        Sampler = Samplers ? RxUnwrap<RxSamplerState>(Samplers[Index]) : nullptr;
        State.Samplers[Start + Index] = RxBound<RxSamplerState>(Sampler);

        /* An empty slot samples with the device default */
        State.SamplerHandles[Start + Index] = Sampler ? Sampler->GetHandle() : Device.GetDefaults().Sampler->GetHandle();
    }

    State.SamplerCount = std::max(State.SamplerCount, Start + Count);
    SendSamplers(Stage, Start, Count);
}

/* Vertex and pixel shader stages */

VOID
STDMETHODCALLTYPE
RxImmediateContext::VSSetShader(
    _In_opt_ ID3D11VertexShader* pVertexShader,
    _In_reads_opt_(NumClassInstances) ID3D11ClassInstance* const* ppClassInstances,
    _In_ UINT NumClassInstances)
{
    RxContextGuard Guard(Device);
    RxVertexShader* Shader = RxUnwrap<RxVertexShader>(pVertexShader);

    UNREFERENCED_PARAMETER(ppClassInstances);
    UNREFERENCED_PARAMETER(NumClassInstances);

    SetShader(RxStage::Vertex, Shader, Shader, Shader ? Shader->GetHandle() : D3D10DDI_HSHADER{});
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::PSSetShader(
    _In_opt_ ID3D11PixelShader* pPixelShader,
    _In_reads_opt_(NumClassInstances) ID3D11ClassInstance* const* ppClassInstances,
    _In_ UINT NumClassInstances)
{
    RxContextGuard Guard(Device);
    RxPixelShader* Shader = RxUnwrap<RxPixelShader>(pPixelShader);

    UNREFERENCED_PARAMETER(ppClassInstances);
    UNREFERENCED_PARAMETER(NumClassInstances);

    SetShader(RxStage::Pixel, Shader, Shader, Shader ? Shader->GetHandle() : D3D10DDI_HSHADER{});
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::VSSetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers)
{
    RxContextGuard Guard(Device);
    SetConstantBuffers(RxStage::Vertex, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::PSSetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers)
{
    RxContextGuard Guard(Device);
    SetConstantBuffers(RxStage::Pixel, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::VSSetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    RxContextGuard Guard(Device);
    SetShaderResources(RxStage::Vertex, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::PSSetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    RxContextGuard Guard(Device);
    SetShaderResources(RxStage::Pixel, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::VSSetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _In_reads_opt_(NumSamplers) ID3D11SamplerState* const* ppSamplers)
{
    RxContextGuard Guard(Device);
    SetSamplers(RxStage::Vertex, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::PSSetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _In_reads_opt_(NumSamplers) ID3D11SamplerState* const* ppSamplers)
{
    RxContextGuard Guard(Device);
    SetSamplers(RxStage::Pixel, StartSlot, NumSamplers, ppSamplers);
}

/* Input assembler */

VOID
STDMETHODCALLTYPE
RxImmediateContext::IASetInputLayout(
    _In_opt_ ID3D11InputLayout* pInputLayout)
{
    RxContextGuard Guard(Device);
    RxInputLayout* Layout = RxUnwrap<RxInputLayout>(pInputLayout);
    D3D10DDI_HELEMENTLAYOUT Handle = Layout ? Layout->GetHandle() : D3D10DDI_HELEMENTLAYOUT{};

    Pipeline.InputLayout = RxBound<RxInputLayout>(Layout);
    if (Handle.pDrvPrivate == Pipeline.InputLayoutHandle.pDrvPrivate)
        return;

    Pipeline.InputLayoutHandle = Handle;
    SendInputLayout();
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::IASetVertexBuffers(
    _In_range_(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppVertexBuffers,
    _In_reads_opt_(NumBuffers) const UINT* pStrides,
    _In_reads_opt_(NumBuffers) const UINT* pOffsets)
{
    RxContextGuard Guard(Device);
    RxBuffer* Buffer;
    UINT Index;

    if (StartSlot >= Pipeline.VertexBuffers.size())
        return;
    NumBuffers = std::min(NumBuffers, (UINT)Pipeline.VertexBuffers.size() - StartSlot);

    for (Index = 0; Index < NumBuffers; Index++)
    {
        Buffer = ppVertexBuffers ? RxUnwrap<RxBuffer>(ppVertexBuffers[Index]) : nullptr;
        if (Buffer != nullptr && IsBoundForOutput(&Buffer->GetDdiResource()))
            Buffer = nullptr;
        Pipeline.VertexBuffers[StartSlot + Index] = RxBound<RxBuffer>(Buffer);
        Pipeline.VertexBufferHandles[StartSlot + Index] = Buffer ? Buffer->GetDdiResource().GetHandle()
                                                                 : D3D10DDI_HRESOURCE{};
        Pipeline.Strides[StartSlot + Index] = pStrides ? pStrides[Index] : 0;
        Pipeline.Offsets[StartSlot + Index] = pOffsets ? pOffsets[Index] : 0;
    }

    Pipeline.VertexBufferCount = std::max(Pipeline.VertexBufferCount, StartSlot + NumBuffers);
    SendVertexBuffers(StartSlot, NumBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::IASetIndexBuffer(
    _In_opt_ ID3D11Buffer* pIndexBuffer,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Offset)
{
    RxContextGuard Guard(Device);
    RxBuffer* Buffer = RxUnwrap<RxBuffer>(pIndexBuffer);
    D3D10DDI_HRESOURCE Handle;

    if (Buffer != nullptr && IsBoundForOutput(&Buffer->GetDdiResource()))
        Buffer = nullptr;
    Handle = Buffer ? Buffer->GetDdiResource().GetHandle() : D3D10DDI_HRESOURCE{};

    Pipeline.IndexBuffer = RxBound<RxBuffer>(Buffer);
    if (Handle.pDrvPrivate == Pipeline.IndexBufferHandle.pDrvPrivate && Format == Pipeline.IndexFormat &&
        Offset == Pipeline.IndexOffset)
    {
        return;
    }

    Pipeline.IndexBufferHandle = Handle;
    Pipeline.IndexFormat = Format;
    Pipeline.IndexOffset = Offset;
    SendIndexBuffer();
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::IASetPrimitiveTopology(
    _In_ D3D11_PRIMITIVE_TOPOLOGY Topology)
{
    RxContextGuard Guard(Device);
    if (Topology == Pipeline.Topology)
        return;

    Pipeline.Topology = Topology;
    SendTopology();
}

/* Rasterizer */

VOID
STDMETHODCALLTYPE
RxImmediateContext::RSSetState(
    _In_opt_ ID3D11RasterizerState* pRasterizerState)
{
    RxContextGuard Guard(Device);
    RxRasterizerState* State = RxUnwrap<RxRasterizerState>(pRasterizerState);
    D3D10DDI_HRASTERIZERSTATE Handle = State ? State->GetHandle() : Device.GetDefaults().Rasterizer->GetHandle();

    Pipeline.Rasterizer = RxBound<RxRasterizerState>(State);
    if (Handle.pDrvPrivate == Pipeline.RasterizerHandle.pDrvPrivate)
        return;

    Pipeline.RasterizerHandle = Handle;
    SendRasterizerState();
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::RSSetViewports(
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumViewports,
    _In_reads_opt_(NumViewports) const D3D11_VIEWPORT* pViewports)
{
    RxContextGuard Guard(Device);
    constexpr FLOAT Bound = (FLOAT)D3D11_VIEWPORT_BOUNDS_MAX;
    UINT Previous = Pipeline.ViewportCount;
    UINT Index;

    if (NumViewports > Pipeline.Viewports.size() || (NumViewports != 0 && pViewports == nullptr))
        return;

    /* One bad viewport drops the whole call */
    for (Index = 0; Index < NumViewports; Index++)
    {
        const D3D11_VIEWPORT& Viewport = pViewports[Index];

        if (Viewport.TopLeftX < -Bound || Viewport.TopLeftY < -Bound || Viewport.Width < 0.0f || Viewport.Height < 0.0f ||
            Viewport.TopLeftX + Viewport.Width > Bound || Viewport.TopLeftY + Viewport.Height > Bound ||
            Viewport.MinDepth < 0.0f || Viewport.MaxDepth > 1.0f || Viewport.MinDepth > Viewport.MaxDepth)
        {
            return;
        }
    }

    for (Index = 0; Index < NumViewports; Index++)
    {
        D3D10_DDI_VIEWPORT& Out = Pipeline.Viewports[Index];

        Out.TopLeftX = RxFlushDenormal(pViewports[Index].TopLeftX);
        Out.TopLeftY = RxFlushDenormal(pViewports[Index].TopLeftY);
        Out.Width = RxFlushDenormal(pViewports[Index].Width);
        Out.Height = RxFlushDenormal(pViewports[Index].Height);
        Out.MinDepth = RxFlushDenormal(pViewports[Index].MinDepth);
        Out.MaxDepth = RxFlushDenormal(pViewports[Index].MaxDepth);
    }

    Pipeline.ViewportCount = NumViewports;
    SendViewports(RxClearCount(Previous, NumViewports));
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::RSSetScissorRects(
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumRects,
    _In_reads_opt_(NumRects) const D3D11_RECT* pRects)
{
    RxContextGuard Guard(Device);
    UINT Previous = Pipeline.ScissorCount;
    UINT Index;

    static_assert(sizeof(D3D11_RECT) == sizeof(D3D10_DDI_RECT));

    if (NumRects > Pipeline.Scissors.size() || (NumRects != 0 && pRects == nullptr))
        return;

    for (Index = 0; Index < NumRects; Index++)
        memcpy(&Pipeline.Scissors[Index], &pRects[Index], sizeof(D3D10_DDI_RECT));

    Pipeline.ScissorCount = NumRects;
    SendScissorRects(RxClearCount(Previous, NumRects));
}

/* Output merger */

VOID
RxImmediateContext::SetOutputs(
    _In_ BOOLEAN KeepTargets,
    _In_ UINT TargetCount,
    _In_reads_opt_(TargetCount) ID3D11RenderTargetView* const* Targets,
    _In_opt_ ID3D11DepthStencilView* DepthStencil,
    _In_ BOOLEAN KeepUavs,
    _In_ UINT UavStart,
    _In_ UINT UavCount,
    _In_reads_opt_(UavCount) ID3D11UnorderedAccessView* const* Uavs,
    _In_reads_opt_(UavCount) const UINT* UavCounts)
{
    RxContextGuard Guard(Device);
    UINT PreviousTargets = Pipeline.RenderTargetCount;
    UINT PreviousLow = Pipeline.OutputUavStart;
    UINT PreviousHigh = Pipeline.OutputUavEnd;
    RxDepthStencilView* DepthView;
    RxUnorderedAccessView* Uav;
    RxRenderTargetView* View;
    UINT RangeStart;
    UINT RangeSize = 0;
    UINT Index;

    if (KeepTargets)
    {
        /* Kept targets in slots the UAVs now take are let go */
        for (Index = TargetCount; Index < PreviousTargets; Index++)
        {
            Pipeline.RenderTargets[Index].Reset();
            Pipeline.RenderTargetHandles[Index] = {};
        }
        Pipeline.RenderTargetCount = TargetCount;
    }
    else
    {
        DepthView = RxUnwrap<RxDepthStencilView>(DepthStencil);
        for (Index = 0; Index < Pipeline.RenderTargets.size(); Index++)
        {
            View = (Index < TargetCount && Targets) ? RxUnwrap<RxRenderTargetView>(Targets[Index]) : nullptr;
            Pipeline.RenderTargets[Index] = RxBound<RxRenderTargetView>(View);
            Pipeline.RenderTargetHandles[Index] = View ? View->GetHandle() : D3D10DDI_HRENDERTARGETVIEW{};
        }

        Pipeline.DepthStencilView = RxBound<RxDepthStencilView>(DepthView);
        Pipeline.DepthStencilViewHandle = DepthView ? DepthView->GetHandle() : D3D10DDI_HDEPTHSTENCILVIEW{};
        Pipeline.RenderTargetCount = TargetCount;
    }

    if (KeepUavs)
    {
        UavStart = PreviousLow;
        RangeStart = PreviousLow;
    }
    else
    {
        for (Index = 0; Index < RxUavSlotCount; Index++)
        {
            Uav = (Index >= UavStart && Index - UavStart < UavCount && Uavs)
                      ? RxUnwrap<RxUnorderedAccessView>(Uavs[Index - UavStart])
                      : nullptr;
            Pipeline.OutputUavs[Index] = RxBound<RxUnorderedAccessView>(Uav);
            Pipeline.OutputUavHandles[Index] = Uav ? Uav->GetHandle() : D3D11DDI_HUNORDEREDACCESSVIEW{};
        }

        /* The updated range covers both the old and the new UAVs, never the targets */
        RangeStart = std::min(UavStart, std::max(PreviousLow, TargetCount));
        RangeSize = std::max(UavStart + UavCount, PreviousHigh) - RangeStart;
        Pipeline.OutputUavStart = UavStart;
        Pipeline.OutputUavEnd = UavStart + UavCount;
    }

    /* Outputs are writes: drop them from every read binding and count them for later readers */
    for (Index = 0; Index < TargetCount; Index++)
    {
        if (Pipeline.RenderTargets[Index])
            NoteOutput(Pipeline.RenderTargets[Index]->GetBacking(), RxOutput::Merger);
    }
    if (Pipeline.DepthStencilView)
        NoteOutput(Pipeline.DepthStencilView->GetBacking(), RxOutput::Merger);
    for (Index = Pipeline.OutputUavStart; Index < Pipeline.OutputUavEnd; Index++)
    {
        if (Pipeline.OutputUavs[Index])
            NoteOutput(Pipeline.OutputUavs[Index]->GetBacking(), RxOutput::Merger);
    }

    /* Targets past the new count are cleared, unless UAVs took those slots over */
    SendOutputs(RxClearCount(std::min(PreviousTargets, UavStart), TargetCount), RangeStart, RangeSize,
                KeepUavs ? nullptr : UavCounts);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::OMSetRenderTargets(
    _In_range_(0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D11RenderTargetView* const* ppRenderTargetViews,
    _In_opt_ ID3D11DepthStencilView* pDepthStencilView)
{
    RxContextGuard Guard(Device);
    if (NumViews > Pipeline.RenderTargets.size())
        return;

    /* Setting targets alone lets go of the output merger UAVs */
    SetOutputs(FALSE, NumViews, ppRenderTargetViews, pDepthStencilView, FALSE,
               std::max(Pipeline.OutputUavStart, NumViews), 0, nullptr, nullptr);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::OMSetRenderTargetsAndUnorderedAccessViews(
    _In_ UINT NumRTVs,
    _In_reads_opt_(NumRTVs) ID3D11RenderTargetView* const* ppRenderTargetViews,
    _In_opt_ ID3D11DepthStencilView* pDepthStencilView,
    _In_range_(0, D3D11_1_UAV_SLOT_COUNT - 1) UINT UAVStartSlot,
    _In_ UINT NumUAVs,
    _In_reads_opt_(NumUAVs) ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
    _In_reads_opt_(NumUAVs) const UINT* pUAVInitialCounts)
{
    RxContextGuard Guard(Device);
    BOOLEAN KeepTargets = (NumRTVs == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL);
    BOOLEAN KeepUavs = (NumUAVs == D3D11_KEEP_UNORDERED_ACCESS_VIEWS);
    UINT Targets = KeepTargets ? Pipeline.RenderTargetCount : NumRTVs;

    if (Targets > Pipeline.RenderTargets.size())
        return;

    if (!KeepUavs)
    {
        if (NumUAVs == 0)
            UAVStartSlot = Targets;
        if (UAVStartSlot > GetUavSlotCount() || NumUAVs > GetUavSlotCount() - UAVStartSlot)
            return;

        /* Targets only reach up to the first UAV slot */
        Targets = std::min(Targets, UAVStartSlot);
    }

    SetOutputs(KeepTargets, Targets, ppRenderTargetViews, pDepthStencilView, KeepUavs, UAVStartSlot, NumUAVs,
               ppUnorderedAccessViews, pUAVInitialCounts);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::OMSetBlendState(
    _In_opt_ ID3D11BlendState* pBlendState,
    _In_opt_ const FLOAT BlendFactor[4],
    _In_ UINT SampleMask)
{
    RxContextGuard Guard(Device);
    static const FLOAT DefaultFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    RxBlendState* State = RxUnwrap<RxBlendState>(pBlendState);
    D3D10DDI_HBLENDSTATE Handle = State ? State->GetHandle() : Device.GetDefaults().Blend->GetHandle();
    const FLOAT* Factor = BlendFactor ? BlendFactor : DefaultFactor;

    Pipeline.Blend = RxBound<RxBlendState>(State);
    if (Handle.pDrvPrivate == Pipeline.BlendHandle.pDrvPrivate && SampleMask == Pipeline.SampleMask &&
        memcmp(Factor, Pipeline.BlendFactor.data(), sizeof(DefaultFactor)) == 0)
    {
        return;
    }

    Pipeline.BlendHandle = Handle;
    Pipeline.SampleMask = SampleMask;
    memcpy(Pipeline.BlendFactor.data(), Factor, sizeof(DefaultFactor));
    SendBlendState();
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::OMSetDepthStencilState(
    _In_opt_ ID3D11DepthStencilState* pDepthStencilState,
    _In_ UINT StencilRef)
{
    RxContextGuard Guard(Device);
    RxDepthStencilState* State = RxUnwrap<RxDepthStencilState>(pDepthStencilState);
    D3D10DDI_HDEPTHSTENCILSTATE Handle = State ? State->GetHandle() : Device.GetDefaults().DepthStencil->GetHandle();

    /* Stencil values are eight bits */
    StencilRef &= 0xFF;
    Pipeline.DepthStencil = RxBound<RxDepthStencilState>(State);
    if (Handle.pDrvPrivate == Pipeline.DepthStencilHandle.pDrvPrivate && StencilRef == Pipeline.StencilRef)
        return;

    Pipeline.DepthStencilHandle = Handle;
    Pipeline.StencilRef = StencilRef;
    SendDepthStencilState();
}

/* Drawing, updating and clearing */

VOID
STDMETHODCALLTYPE
RxImmediateContext::Draw(
    _In_ UINT VertexCount,
    _In_ UINT StartVertexLocation)
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnDraw(Device.GetDriverDevice(), VertexCount, StartVertexLocation);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DrawIndexed(
    _In_ UINT IndexCount,
    _In_ UINT StartIndexLocation,
    _In_ INT BaseVertexLocation)
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnDrawIndexed(Device.GetDriverDevice(), IndexCount, StartIndexLocation, BaseVertexLocation);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::UpdateSubresource(
    _In_ ID3D11Resource* pDstResource,
    _In_ UINT DstSubresource,
    _In_opt_ const D3D11_BOX* pDstBox,
    _In_ const void* pSrcData,
    _In_ UINT SrcRowPitch,
    _In_ UINT SrcDepthPitch)
{
    RxContextGuard Guard(Device);
    UpdateSubresource1(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch, 0);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::UpdateSubresource1(
    _In_ ID3D11Resource* pDstResource,
    _In_ UINT DstSubresource,
    _In_opt_ const D3D11_BOX* pDstBox,
    _In_ const void* pSrcData,
    _In_ UINT SrcRowPitch,
    _In_ UINT SrcDepthPitch,
    _In_ UINT CopyFlags)
{
    RxContextGuard Guard(Device);
    DdiResource* Resource = RxGetDdiResource(pDstResource);
    ID3D11Buffer* Buffer;
    D3D11_BUFFER_DESC BufferDesc = {};
    BOOLEAN IsConstantBuffer = FALSE;

    static_assert(sizeof(D3D11_BOX) == sizeof(D3D10_DDI_BOX));

    if (Resource == nullptr || pSrcData == nullptr)
        return;

    /* An empty box updates nothing */
    if (pDstBox != nullptr &&
        (pDstBox->left >= pDstBox->right || pDstBox->top >= pDstBox->bottom || pDstBox->front >= pDstBox->back))
    {
        return;
    }

    if (SUCCEEDED(pDstResource->QueryInterface(IID_PPV_ARGS(&Buffer))))
    {
        Buffer->GetDesc(&BufferDesc);
        Buffer->Release();
        IsConstantBuffer = (BufferDesc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
    }

    /* Constant buffers have their own entry so drivers can rename them cheaply */
    (IsConstantBuffer ? Device.GetDeviceFuncs().pfnDefaultConstantBufferUpdateSubresourceUP
                      : Device.GetDeviceFuncs().pfnResourceUpdateSubresourceUP)(
        Device.GetDriverDevice(), Resource->GetHandle(), DstSubresource,
        reinterpret_cast<const D3D10_DDI_BOX*>(pDstBox), pSrcData, SrcRowPitch, SrcDepthPitch, CopyFlags);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::ClearDepthStencilView(
    _In_ ID3D11DepthStencilView* pDepthStencilView,
    _In_ UINT ClearFlags,
    _In_ FLOAT Depth,
    _In_ UINT8 Stencil)
{
    RxContextGuard Guard(Device);
    RxDepthStencilView* View = RxUnwrap<RxDepthStencilView>(pDepthStencilView);

    if (View == nullptr || (ClearFlags & (D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL)) == 0)
        return;

    Device.GetDeviceFuncs().pfnClearDepthStencilView(Device.GetDriverDevice(), View->GetHandle(), ClearFlags, Depth,
                                                     Stencil);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::ClearState(VOID)
{
    RxContextGuard Guard(Device);
    ID3D11ShaderResourceView* NoViews[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11Buffer* NoBuffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11SamplerState* NoSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* NoVertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11UnorderedAccessView* NoUavs[RxUavSlotCount] = {};
    UINT Stage;

    /* Everything goes back through the public setters, up to what was ever bound */
    IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
    IASetInputLayout(nullptr);
    if (Pipeline.VertexBufferCount != 0)
        IASetVertexBuffers(0, Pipeline.VertexBufferCount, NoVertexBuffers, nullptr, nullptr);
    IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

    for (Stage = 0; Stage < RxStageCount; Stage++)
    {
        RxStageState& State = Pipeline.Stages[Stage];

        SetShader((RxStage)Stage, nullptr, nullptr, D3D10DDI_HSHADER{});
        if (State.ShaderResourceCount != 0)
            SetShaderResources((RxStage)Stage, 0, State.ShaderResourceCount, NoViews);
        if (State.ConstantBufferCount != 0)
            SetConstantBuffers((RxStage)Stage, 0, State.ConstantBufferCount, NoBuffers);
        if (State.SamplerCount != 0)
            SetSamplers((RxStage)Stage, 0, State.SamplerCount, NoSamplers);
    }

    RSSetState(nullptr);
    if (Pipeline.ViewportCount != 0)
        RSSetViewports(0, nullptr);
    if (Pipeline.ScissorCount != 0)
        RSSetScissorRects(0, nullptr);
    OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    OMSetDepthStencilState(nullptr, 0);
    OMSetRenderTargets(0, nullptr, nullptr);
    if (Pipeline.ComputeUavCount != 0)
        CSSetUnorderedAccessViews(0, Pipeline.ComputeUavCount, NoUavs, nullptr);
    if (Pipeline.StreamTargetCount != 0)
        SOSetTargets(0, nullptr, nullptr);
    if (Pipeline.Predicate)
        SetPredication(nullptr, FALSE);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::ResolveSubresource(
    _In_ ID3D11Resource* pDstResource,
    _In_ UINT DstSubresource,
    _In_ ID3D11Resource* pSrcResource,
    _In_ UINT SrcSubresource,
    _In_ DXGI_FORMAT Format)
{
    RxContextGuard Guard(Device);
    DdiResource* Destination = RxGetDdiResource(pDstResource);
    DdiResource* Source = RxGetDdiResource(pSrcResource);

    if (Destination == nullptr || Source == nullptr || Destination == Source)
        return;

    /* The resolved samples are a write that later readers must see */
    Destination->NoteWrite();
    Device.GetDeviceFuncs().pfnResourceResolveSubresource(Device.GetDriverDevice(), Destination->GetHandle(),
                                                          DstSubresource, Source->GetHandle(), SrcSubresource, Format);
}
