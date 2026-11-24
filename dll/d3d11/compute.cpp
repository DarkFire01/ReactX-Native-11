/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compute dispatch, unordered access views and stream output on the immediate context
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "shaders.hpp"
#include "views.hpp"

#include <algorithm>
#include <cmath>

/* Sending */

VOID
RxImmediateContext::SendComputeUavs(
    _In_ UINT Start,
    _In_ UINT Count)
{
    RxContextGuard Guard(Device);
    if (Start >= RxUavSlotCount)
        return;
    if (Count == UINT_MAX)
        Count = (Pipeline.ComputeUavCount > Start) ? Pipeline.ComputeUavCount - Start : 0;
    Count = std::min(Count, RxUavSlotCount - Start);
    if (Count == 0)
        return;

    Device.GetDeviceFuncs().pfnCsSetUnorderedAccessViews(Device.GetDriverDevice(), Start, Count,
                                                         &Pipeline.ComputeUavHandles[Start], RxKeepCounters.data());
}

VOID
RxImmediateContext::SendStreamTargets(
    _In_ UINT ClearTargets)
{
    RxContextGuard Guard(Device);
    /* Offsets of -1 keep appending where the targets left off */
    Device.GetDeviceFuncs().pfnSoSetTargets(Device.GetDriverDevice(), Pipeline.StreamTargetCount, ClearTargets,
                                            Pipeline.StreamTargetHandles.data(), RxKeepCounters.data());
}

/* Compute */

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSSetUnorderedAccessViews(
    _In_range_(0, D3D11_1_UAV_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_1_UAV_SLOT_COUNT - StartSlot) UINT NumUAVs,
    _In_reads_opt_(NumUAVs) ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
    _In_reads_opt_(NumUAVs) const UINT* pUAVInitialCounts)
{
    RxContextGuard Guard(Device);
    RxUnorderedAccessView* View;
    UINT Index;

    if (StartSlot >= GetUavSlotCount() || NumUAVs > GetUavSlotCount() - StartSlot)
        return;

    for (Index = 0; Index < NumUAVs; Index++)
    {
        View = ppUnorderedAccessViews ? RxUnwrap<RxUnorderedAccessView>(ppUnorderedAccessViews[Index]) : nullptr;
        Pipeline.ComputeUavs[StartSlot + Index] = RxBound<RxUnorderedAccessView>(View);
        Pipeline.ComputeUavHandles[StartSlot + Index] = View ? View->GetHandle() : D3D11DDI_HUNORDEREDACCESSVIEW{};
    }

    /* Done after every slot is set, so slots of this call do not evict each other */
    for (Index = 0; Index < NumUAVs; Index++)
    {
        View = Pipeline.ComputeUavs[StartSlot + Index].Get();
        if (View == nullptr)
            continue;

        ReleaseComputeUavsOf(View->GetBacking(), StartSlot, NumUAVs);
        NoteOutput(View->GetBacking(), RxOutput::Compute);
    }

    Pipeline.ComputeUavCount = std::max(Pipeline.ComputeUavCount, StartSlot + NumUAVs);
    if (NumUAVs == 0)
        return;

    Device.GetDeviceFuncs().pfnCsSetUnorderedAccessViews(
        Device.GetDriverDevice(), StartSlot, NumUAVs, &Pipeline.ComputeUavHandles[StartSlot],
        pUAVInitialCounts ? pUAVInitialCounts : RxKeepCounters.data());
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSGetUnorderedAccessViews(
    _In_range_(0, D3D11_1_UAV_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_1_UAV_SLOT_COUNT - StartSlot) UINT NumUAVs,
    _Out_writes_opt_(NumUAVs) ID3D11UnorderedAccessView** ppUnorderedAccessViews)
{
    RxContextGuard Guard(Device);
    UINT Index;

    if (ppUnorderedAccessViews == nullptr || StartSlot >= RxUavSlotCount || NumUAVs > RxUavSlotCount - StartSlot)
        return;

    for (Index = 0; Index < NumUAVs; Index++)
        ppUnorderedAccessViews[Index] = RxHandBack<ID3D11UnorderedAccessView>(Pipeline.ComputeUavs[StartSlot + Index]);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::OMGetRenderTargetsAndUnorderedAccessViews(
    _In_range_(0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) UINT NumRTVs,
    _Out_writes_opt_(NumRTVs) ID3D11RenderTargetView** ppRenderTargetViews,
    _Outptr_opt_result_maybenull_ ID3D11DepthStencilView** ppDepthStencilView,
    _In_range_(0, D3D11_PS_CS_UAV_REGISTER_COUNT - 1) UINT UAVStartSlot,
    _In_range_(0, D3D11_PS_CS_UAV_REGISTER_COUNT - UAVStartSlot) UINT NumUAVs,
    _Out_writes_opt_(NumUAVs) ID3D11UnorderedAccessView** ppUnorderedAccessViews)
{
    RxContextGuard Guard(Device);
    UINT Index;

    OMGetRenderTargets(NumRTVs, ppRenderTargetViews, ppDepthStencilView);
    if (ppUnorderedAccessViews == nullptr || UAVStartSlot >= RxUavSlotCount || NumUAVs > RxUavSlotCount - UAVStartSlot)
        return;

    for (Index = 0; Index < NumUAVs; Index++)
        ppUnorderedAccessViews[Index] = RxHandBack<ID3D11UnorderedAccessView>(Pipeline.OutputUavs[UAVStartSlot + Index]);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::Dispatch(
    _In_ UINT ThreadGroupCountX,
    _In_ UINT ThreadGroupCountY,
    _In_ UINT ThreadGroupCountZ)
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnDispatch(Device.GetDriverDevice(), ThreadGroupCountX, ThreadGroupCountY,
                                        ThreadGroupCountZ);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::ClearUnorderedAccessViewUint(
    _In_ ID3D11UnorderedAccessView* pUnorderedAccessView,
    _In_ const UINT Values[4])
{
    RxContextGuard Guard(Device);
    RxUnorderedAccessView* View = RxUnwrap<RxUnorderedAccessView>(pUnorderedAccessView);

    if (View == nullptr || Values == nullptr)
        return;

    View->GetBacking()->NoteWrite();
    Device.GetDeviceFuncs().pfnClearUnorderedAccessViewUint(Device.GetDriverDevice(), View->GetHandle(), Values);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::ClearUnorderedAccessViewFloat(
    _In_ ID3D11UnorderedAccessView* pUnorderedAccessView,
    _In_ const FLOAT Values[4])
{
    RxContextGuard Guard(Device);
    RxUnorderedAccessView* View = RxUnwrap<RxUnorderedAccessView>(pUnorderedAccessView);
    FLOAT Flushed[4];
    UINT Index;

    if (View == nullptr || Values == nullptr)
        return;

    for (Index = 0; Index < _countof(Flushed); Index++)
        Flushed[Index] = (std::fpclassify(Values[Index]) == FP_SUBNORMAL) ? 0.0f : Values[Index];

    View->GetBacking()->NoteWrite();
    Device.GetDeviceFuncs().pfnClearUnorderedAccessViewFloat(Device.GetDriverDevice(), View->GetHandle(), Flushed);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CopyStructureCount(
    _In_ ID3D11Buffer* pDstBuffer,
    _In_ UINT DstAlignedByteOffset,
    _In_ ID3D11UnorderedAccessView* pSrcView)
{
    RxContextGuard Guard(Device);
    RxUnorderedAccessView* View = RxUnwrap<RxUnorderedAccessView>(pSrcView);
    RxBuffer* Destination = RxUnwrap<RxBuffer>(pDstBuffer);
    D3D11_UNORDERED_ACCESS_VIEW_DESC ViewDesc;
    D3D11_BUFFER_DESC BufferDesc;

    if (View == nullptr || Destination == nullptr || (DstAlignedByteOffset % sizeof(UINT)) != 0)
        return;

    /* Only append and counter buffers carry a count to copy */
    View->GetDesc(&ViewDesc);
    if (ViewDesc.ViewDimension != D3D11_UAV_DIMENSION_BUFFER ||
        (ViewDesc.Buffer.Flags & (D3D11_BUFFER_UAV_FLAG_APPEND | D3D11_BUFFER_UAV_FLAG_COUNTER)) == 0)
    {
        return;
    }

    Destination->GetDesc(&BufferDesc);
    if (BufferDesc.ByteWidth < sizeof(UINT) || DstAlignedByteOffset > BufferDesc.ByteWidth - sizeof(UINT))
        return;

    Destination->GetDdiResource().NoteWrite();
    Device.GetDeviceFuncs().pfnCopyStructureCount(Device.GetDriverDevice(), Destination->GetDdiResource().GetHandle(),
                                                  DstAlignedByteOffset, View->GetHandle());
}

/* Stream output */

VOID
STDMETHODCALLTYPE
RxImmediateContext::SOSetTargets(
    _In_range_(0, D3D11_SO_BUFFER_SLOT_COUNT) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppSOTargets,
    _In_reads_opt_(NumBuffers) const UINT* pOffsets)
{
    RxContextGuard Guard(Device);
    UINT Previous = Pipeline.StreamTargetCount;
    RxBuffer* Buffer;
    UINT Index;

    if (NumBuffers > Pipeline.StreamTargets.size())
        return;

    for (Index = 0; Index < Pipeline.StreamTargets.size(); Index++)
    {
        Buffer = (Index < NumBuffers && ppSOTargets) ? RxUnwrap<RxBuffer>(ppSOTargets[Index]) : nullptr;
        Pipeline.StreamTargets[Index] = RxBound<RxBuffer>(Buffer);
        Pipeline.StreamTargetHandles[Index] = Buffer ? Buffer->GetDdiResource().GetHandle() : D3D10DDI_HRESOURCE{};
        if (Buffer != nullptr)
            NoteOutput(&Buffer->GetDdiResource(), RxOutput::Stream);
    }

    Pipeline.StreamTargetCount = NumBuffers;
    Device.GetDeviceFuncs().pfnSoSetTargets(Device.GetDriverDevice(), NumBuffers, Previous - std::min(Previous, NumBuffers),
                                            Pipeline.StreamTargetHandles.data(),
                                            pOffsets ? pOffsets : RxKeepCounters.data());
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::SOGetTargets(
    _In_range_(0, D3D11_SO_BUFFER_SLOT_COUNT) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppSOTargets)
{
    RxContextGuard Guard(Device);
    UINT Index;

    if (ppSOTargets == nullptr || NumBuffers > Pipeline.StreamTargets.size())
        return;

    for (Index = 0; Index < NumBuffers; Index++)
        ppSOTargets[Index] = RxHandBack<ID3D11Buffer>(Pipeline.StreamTargets[Index]);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DrawAuto(VOID)
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnDrawAuto(Device.GetDriverDevice());
}
