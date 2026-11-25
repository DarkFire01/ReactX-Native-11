/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12GraphicsCommandList: work, clears, resolves, queries and buffer writes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "commandlist_internal.hpp"

/* Draws and dispatches go straight to the driver */

VOID
STDMETHODCALLTYPE
RxCommandList::DrawInstanced(
    _In_ UINT VertexCountPerInstance,
    _In_ UINT InstanceCount,
    _In_ UINT StartVertexLocation,
    _In_ UINT StartInstanceLocation)
{
    if (!Takes(RxListDoesGraphics) || !Records() || !PassAllows(TRUE))
        return;

    DriverTable->pfnDrawInstanced(Api(), VertexCountPerInstance, InstanceCount, StartVertexLocation,
                                  StartInstanceLocation);
}

VOID
STDMETHODCALLTYPE
RxCommandList::DrawIndexedInstanced(
    _In_ UINT IndexCountPerInstance,
    _In_ UINT InstanceCount,
    _In_ UINT StartIndexLocation,
    _In_ INT BaseVertexLocation,
    _In_ UINT StartInstanceLocation)
{
    if (!Takes(RxListDoesGraphics) || !Records() || !PassAllows(TRUE))
        return;

    DriverTable->pfnDrawIndexedInstanced(Api(), IndexCountPerInstance, InstanceCount, StartIndexLocation,
                                         BaseVertexLocation, StartInstanceLocation);
}

VOID
STDMETHODCALLTYPE
RxCommandList::Dispatch(
    _In_ UINT ThreadGroupCountX,
    _In_ UINT ThreadGroupCountY,
    _In_ UINT ThreadGroupCountZ)
{
    /* Dispatches run neither inside a pass nor while one is suspended */
    if (!Takes(RxListDoesCompute) || !Records() || !PassAllows(FALSE))
        return;

    DriverTable->pfnDispatch(Api(), ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

VOID
STDMETHODCALLTYPE
RxCommandList::DispatchMesh(
    _In_ UINT ThreadGroupCountX,
    _In_ UINT ThreadGroupCountY,
    _In_ UINT ThreadGroupCountZ)
{
    if (!Takes(RxListDoesGraphics) || !Records() || !PassAllows(TRUE) || DriverTable->pfnDispatchMesh == nullptr)
        return;

    DriverTable->pfnDispatchMesh(Api(), ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

/* An argument or count buffer: a buffer the GPU may read, holding Needed bytes from Offset */
static
BOOLEAN
RxIndirectBufferFits(
    _In_ const RxResourceFacts& Buffer,
    _In_ UINT64 Offset,
    _In_ UINT64 Needed)
{
    if (Buffer.Desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || Buffer.HeapType == D3D12_HEAP_TYPE_READBACK)
        return FALSE;

    return Offset + Needed >= Offset && Offset + Needed <= Buffer.Desc->Width;
}

VOID
STDMETHODCALLTYPE
RxCommandList::ExecuteIndirect(
    _In_ ID3D12CommandSignature* pCommandSignature,
    _In_ UINT MaxCommandCount,
    _In_ ID3D12Resource* pArgumentBuffer,
    _In_ UINT64 ArgumentBufferOffset,
    _In_opt_ ID3D12Resource* pCountBuffer,
    _In_ UINT64 CountBufferOffset)
{
    RxCommandSignature* Signature = RxUnwrap12<RxCommandSignature>(pCommandSignature);
    D3D12DDIARG_BUFFER_PLACEMENT Arguments = {};
    D3D12DDIARG_BUFFER_PLACEMENT Count = {};
    RxResourceFacts ArgumentFacts;
    RxResourceFacts CountFacts;
    D3D12_INDIRECT_ARGUMENT_TYPE Command;
    UINT64 Last;

    if (!Takes(RxListDoesCopy | RxListDoesCompute) || !Records() || !PassAllows(TRUE))
        return;

    if (Signature == nullptr || !RxReadResource(pArgumentBuffer, &ArgumentFacts))
    {
        Fail(E_INVALIDARG);
        return;
    }

    /* Bundles take one argument per command and no count buffer */
    if (Type == D3D12_COMMAND_LIST_TYPE_BUNDLE && (pCountBuffer != nullptr || Signature->GetArgumentCount() != 1))
        Fail(E_INVALIDARG);
    if (((ArgumentBufferOffset | CountBufferOffset) & 3) != 0)
        Fail(E_INVALIDARG);

    /* The last command must end inside the argument buffer */
    Last = (UINT64)Signature->GetByteStride() * (MaxCommandCount != 0 ? MaxCommandCount - 1 : 0);
    if (MaxCommandCount != 0 &&
        (Last + ArgumentBufferOffset < Last ||
         !RxIndirectBufferFits(ArgumentFacts, Last + ArgumentBufferOffset, Signature->GetCommandSize())))
    {
        Fail(E_INVALIDARG);
    }
    if (ArgumentFacts.Desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        ArgumentFacts.HeapType == D3D12_HEAP_TYPE_READBACK)
    {
        Fail(E_INVALIDARG);
    }

    if (pCountBuffer != nullptr)
    {
        if (!RxReadResource(pCountBuffer, &CountFacts) ||
            !RxIndirectBufferFits(CountFacts, CountBufferOffset, sizeof(UINT)))
        {
            Fail(E_INVALIDARG);
        }
        else
        {
            Count.BaseAddress.UMD.hResource = CountFacts.Resource->GetDriverHandle();
        }
    }
    else if (CountBufferOffset != 0)
    {
        Fail(E_INVALIDARG);
    }

    /* Compute lists only run dispatches */
    Command = Signature->GetCommandType();
    if (Type == D3D12_COMMAND_LIST_TYPE_COMPUTE && Command != D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH &&
        Command != D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS)
    {
        Fail(E_INVALIDARG);
    }

    if (!Records())
        return;

    Arguments.BaseAddress.UMD.hResource = ArgumentFacts.Resource->GetDriverHandle();
    Arguments.BaseAddress.UMD.Offset = ArgumentBufferOffset;
    Count.BaseAddress.UMD.Offset = CountBufferOffset;
    DriverTable->pfnExecuteIndirect(DriverList, Signature->GetDriverHandle(), MaxCommandCount, Arguments, Count);
}

VOID
STDMETHODCALLTYPE
RxCommandList::ExecuteBundle(
    _In_ ID3D12GraphicsCommandList* pCommandList)
{
    RxCommandList* Bundle = RxUnwrap12<RxCommandList>(pCommandList);
    UINT Index;

    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListDoesGraphics | RxListTopLevel) || !Records() ||
        !PassAllows(TRUE))
    {
        return;
    }

    /* A closed bundle whose descriptor heaps are the ones this list has bound */
    if (Bundle == nullptr || Bundle->ListId != UINT64_MAX || Bundle->Type != D3D12_COMMAND_LIST_TYPE_BUNDLE)
    {
        Fail(E_INVALIDARG);
        return;
    }

    for (Index = 0; Index < _countof(BoundHeaps); Index++)
    {
        if (Bundle->BoundHeaps[Index] != nullptr && Bundle->BoundHeaps[Index] != BoundHeaps[Index])
        {
            Fail(E_INVALIDARG);
            return;
        }
    }

    DriverTable->pfnExecuteBundle(DriverList, Bundle->DriverList);
}

/* Clears */

VOID
STDMETHODCALLTYPE
RxCommandList::ClearDepthStencilView(
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView,
    _In_ D3D12_CLEAR_FLAGS ClearFlags,
    _In_ FLOAT Depth,
    _In_ UINT8 Stencil,
    _In_ UINT NumRects,
    _In_reads_(NumRects) const D3D12_RECT* pRects)
{
    D3D12DDI_CPU_DESCRIPTOR_HANDLE View;

    if (!Takes(RxListDoesGraphics | RxListTopLevel) || !Records() || !PassAllows(FALSE))
        return;

    View.ptr = DepthStencilView.ptr;
    DriverTable->pfnClearDepthStencilView(DriverList, View, ClearFlags, RxZeroDenormal(Depth), Stencil, NumRects,
                                          pRects);
}

VOID
STDMETHODCALLTYPE
RxCommandList::ClearUnorderedAccessViewUint(
    _In_ D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
    _In_ ID3D12Resource* pResource,
    _In_ const UINT Values[4],
    _In_ UINT NumRects,
    _In_reads_(NumRects) const D3D12_RECT* pRects)
{
    D3D12DDI_GPU_DESCRIPTOR_HANDLE Gpu;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Cpu;

    if (!Takes(RxListDoesCompute | RxListTopLevel) || !Records() || !PassAllows(FALSE))
        return;

    Gpu.ptr = ViewGPUHandleInCurrentHeap.ptr;
    Cpu.ptr = ViewCPUHandle.ptr;
    DriverTable->pfnClearUnorderedAccessViewUint(DriverList, Gpu, Cpu, RxDriverResource(pResource), Values, NumRects,
                                                 pRects);
}

VOID
STDMETHODCALLTYPE
RxCommandList::ClearUnorderedAccessViewFloat(
    _In_ D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
    _In_ ID3D12Resource* pResource,
    _In_ const FLOAT Values[4],
    _In_ UINT NumRects,
    _In_reads_(NumRects) const D3D12_RECT* pRects)
{
    D3D12DDI_GPU_DESCRIPTOR_HANDLE Gpu;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Cpu;

    if (!Takes(RxListDoesCompute | RxListTopLevel) || !Records() || !PassAllows(FALSE))
        return;

    Gpu.ptr = ViewGPUHandleInCurrentHeap.ptr;
    Cpu.ptr = ViewCPUHandle.ptr;
    DriverTable->pfnClearUnorderedAccessViewFloat(DriverList, Gpu, Cpu, RxDriverResource(pResource), Values, NumRects,
                                                  pRects);
}

VOID
STDMETHODCALLTYPE
RxCommandList::DiscardResource(
    _In_ ID3D12Resource* pResource,
    _In_opt_ const D3D12_DISCARD_REGION* pRegion)
{
    static_assert(sizeof(D3D12_DISCARD_REGION) == sizeof(D3D12DDIARG_DISCARD_RESOURCE_0003));
    RxResourceFacts Facts;

    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListTopLevel) || !Records())
        return;

    if (Type == D3D12_COMMAND_LIST_TYPE_DIRECT && InPass)
    {
        Fail(E_FAIL);
        return;
    }

    if (!RxReadResource(pResource, &Facts) || !RxDiscardIsValid(Facts, pRegion))
    {
        Fail(E_INVALIDARG);
        return;
    }

    DriverTable->pfnDiscardResource(DriverList, Facts.Resource->GetDriverHandle(),
                                    reinterpret_cast<const D3D12DDIARG_DISCARD_RESOURCE_0003*>(pRegion));
}

BOOLEAN
RxDiscardIsValid(
    _In_ const RxResourceFacts& Facts,
    _In_opt_ const D3D12_DISCARD_REGION* pRegion)
{
    if (pRegion == nullptr)
        return TRUE;

    /* Rectangles cover one mip at a time, and only buffers and 2D textures take them */
    if (pRegion->NumRects != 0)
    {
        if ((pRegion->NumSubresources > 1 && Facts.Desc->MipLevels > 1) ||
            (Facts.Desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER &&
             Facts.Desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D))
        {
            return FALSE;
        }
    }

    return pRegion->FirstSubresource <= Facts.Subresources &&
           pRegion->FirstSubresource + pRegion->NumSubresources >= pRegion->FirstSubresource &&
           pRegion->FirstSubresource + pRegion->NumSubresources <= Facts.Subresources;
}

/* Resolves */

static
UINT
RxArraySizeOf(
    _In_ const D3D12_RESOURCE_DESC1& Desc)
{
    return Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Desc.DepthOrArraySize;
}

/* Whether the format table lets a format be a target of this kind on the device's level */
static
BOOLEAN
RxFormatMayBe(
    _In_ RxDevice12& Device,
    _In_ DXGI_FORMAT Format,
    _In_ RxFormatCap12 Cap)
{
    RxFormatRule12 Rule = RxFormatCapRule12(Device.GetFeatureLevel(), Format, Cap);

    return Rule == RxRuleRequired || Rule == RxRuleDriver;
}

/* Sampler feedback transcodes between an R8_UINT texture and a feedback map */
static
BOOLEAN
RxFeedbackTranscodeIsValid(
    _In_ const D3D12_RESOURCE_DESC1& Dst,
    _In_ const D3D12_RESOURCE_DESC1& Src,
    _In_ BOOLEAN Encode)
{
    const D3D12_RESOURCE_DESC1& Plain = Encode ? Src : Dst;
    const D3D12_RESOURCE_DESC1& Map = Encode ? Dst : Src;
    BOOLEAN Valid = TRUE;

    if (Plain.Format != DXGI_FORMAT_R8_UINT && Plain.Format != DXGI_FORMAT_UNKNOWN)
        Valid = FALSE;
    if (Map.Format != DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE &&
        Map.Format != DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE)
    {
        Valid = FALSE;
    }

    /* A min mip map pairs with a single mip texture, a region used map with one of its own mip count */
    if (Map.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE && Plain.MipLevels != 1)
        Valid = FALSE;
    if (RxArraySizeOf(Dst) != RxArraySizeOf(Src))
        Valid = FALSE;
    if ((Src.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE ||
         Dst.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE) &&
        Src.MipLevels != Dst.MipLevels)
    {
        Valid = FALSE;
    }

    return Valid;
}

HRESULT
RxCheckResolve(
    _In_ RxDevice12& Device,
    _In_ const RxResourceFacts& Dst,
    _In_ UINT DstSubresource,
    _In_ UINT DstX,
    _In_ UINT DstY,
    _In_ const RxResourceFacts& Src,
    _In_ UINT SrcSubresource,
    _In_opt_ const D3D12_RECT* SrcRect,
    _In_ DXGI_FORMAT Format,
    _In_ D3D12_RESOLVE_MODE Mode)
{
    const D3D12_RESOURCE_DESC1& DstDesc = *Dst.Desc;
    const D3D12_RESOURCE_DESC1& SrcDesc = *Src.Desc;
    const UINT ResourceFlags = DstDesc.Flags | SrcDesc.Flags;
    const BOOLEAN Transcode = Mode == D3D12_RESOLVE_MODE_ENCODE_SAMPLER_FEEDBACK ||
                              Mode == D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK;
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 Options2 = {};
    BOOLEAN Valid = TRUE;
    UINT64 Width;
    UINT Height;

    /* The GPU reads no readback heap and writes no upload heap here */
    if (Src.HeapType == D3D12_HEAP_TYPE_READBACK || Dst.HeapType == D3D12_HEAP_TYPE_UPLOAD)
        return E_INVALIDARG;

    if (Transcode)
    {
        Valid = RxFeedbackTranscodeIsValid(DstDesc, SrcDesc, Mode == D3D12_RESOLVE_MODE_ENCODE_SAMPLER_FEEDBACK);
    }
    else
    {
        if (DstDesc.Dimension != SrcDesc.Dimension)
            Valid = FALSE;
        if (DstSubresource >= Dst.Subresources || SrcSubresource >= Src.Subresources)
            Valid = FALSE;

        Width = SrcDesc.Width;
        Height = SrcDesc.Height;
        if (SrcRect != nullptr)
        {
            if (SrcRect->left >= SrcRect->right || SrcRect->top >= SrcRect->bottom)
                Valid = FALSE;
            Width = (UINT)(SrcRect->right - SrcRect->left);
            Height = (UINT)(SrcRect->bottom - SrcRect->top);
        }

        /* The region is measured against the destination's top level, as the system runtime does */
        if ((UINT)Width + DstX > (UINT)DstDesc.Width || Height + DstY > DstDesc.Height)
            Valid = FALSE;
    }

    switch (Mode)
    {
        case D3D12_RESOLVE_MODE_DECOMPRESS:
            if (DstDesc.SampleDesc.Count != SrcDesc.SampleDesc.Count ||
                DstDesc.SampleDesc.Quality != SrcDesc.SampleDesc.Quality)
            {
                Valid = FALSE;
            }
            break;
        case D3D12_RESOLVE_MODE_ENCODE_SAMPLER_FEEDBACK:
            if (SrcDesc.SampleDesc.Count > 1 || SrcDesc.SampleDesc.Quality != 0)
                Valid = FALSE;
            break;
        case D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK:
            if (DstDesc.SampleDesc.Count > 1 || DstDesc.SampleDesc.Quality != 0)
                Valid = FALSE;
            break;
        default:
            if (DstDesc.SampleDesc.Count != 1 || SrcDesc.SampleDesc.Count < 2)
                Valid = FALSE;
            break;
    }

    if ((UINT)Format >= RxFormatRuleCount12 || !RxFormatInHeader(Format))
        return E_INVALIDARG;

    /* Decompress, min and max work on target formats; averaging needs a format that resolves */
    if ((UINT)Mode <= D3D12_RESOLVE_MODE_MAX)
    {
        if (!RxFormatMayBe(Device, Format, RxCapRenderTarget) && !RxFormatMayBe(Device, Format, RxCapDepthStencil) &&
            Format != DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS && Format != DXGI_FORMAT_X32_TYPELESS_G8X24_UINT &&
            Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS && Format != DXGI_FORMAT_X24_TYPELESS_G8_UINT)
        {
            Valid = FALSE;
        }
    }
    else if (Mode == D3D12_RESOLVE_MODE_AVERAGE)
    {
        if (!RxFormatMayBe(Device, Format, RxCapMultisampleResolve) &&
            Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS && Format != DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS)
        {
            Valid = FALSE;
        }
    }

    if (!Transcode &&
        (RxFormatFamily(Format) != RxFormatFamily(SrcDesc.Format) || RxFormatFamily(Format) != RxFormatFamily(DstDesc.Format)))
    {
        Valid = FALSE;
    }

    /* Depth and video reference resources resolve only with programmable sample positions */
    if (FAILED(Device.CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &Options2, sizeof(Options2))) ||
        Options2.ProgrammableSamplePositionsTier == D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_NOT_SUPPORTED)
    {
        if ((ResourceFlags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY |
                              D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY)) != 0)
        {
            Valid = FALSE;
        }
    }

    return Valid ? S_OK : E_INVALIDARG;
}

VOID
STDMETHODCALLTYPE
RxCommandList::ResolveSubresource(
    _In_ ID3D12Resource* pDstResource,
    _In_ UINT DstSubresource,
    _In_ ID3D12Resource* pSrcResource,
    _In_ UINT SrcSubresource,
    _In_ DXGI_FORMAT Format)
{
    RxResourceFacts Dst;
    RxResourceFacts Src;
    HRESULT hr;

    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListDoesGraphics | RxListTopLevel) || !Records() ||
        !PassAllows(FALSE))
    {
        return;
    }

    if (!RxReadResource(pDstResource, &Dst) || !RxReadResource(pSrcResource, &Src))
    {
        Fail(E_INVALIDARG);
        return;
    }

    hr = RxCheckResolve(Device, Dst, DstSubresource, 0, 0, Src, SrcSubresource, nullptr, Format,
                        D3D12_RESOLVE_MODE_AVERAGE);
    if (FAILED(hr))
    {
        Fail(hr);
        return;
    }

    DriverTable->pfnResourceResolveSubresource(DriverList, Dst.Resource->GetDriverHandle(), DstSubresource,
                                               Src.Resource->GetDriverHandle(), SrcSubresource, Format);
}

VOID
STDMETHODCALLTYPE
RxCommandList::ResolveSubresourceRegion(
    _In_ ID3D12Resource* pDstResource,
    _In_ UINT DstSubresource,
    _In_ UINT DstX,
    _In_ UINT DstY,
    _In_ ID3D12Resource* pSrcResource,
    _In_ UINT SrcSubresource,
    _In_opt_ D3D12_RECT* pSrcRect,
    _In_ DXGI_FORMAT Format,
    _In_ D3D12_RESOLVE_MODE ResolveMode)
{
    RxResourceFacts Dst;
    RxResourceFacts Src;
    HRESULT hr;

    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListDoesGraphics | RxListTopLevel) || !Records() ||
        !PassAllows(FALSE))
    {
        return;
    }

    if (!RxReadResource(pDstResource, &Dst) || !RxReadResource(pSrcResource, &Src))
    {
        Fail(E_INVALIDARG);
        return;
    }

    hr = RxCheckResolve(Device, Dst, DstSubresource, DstX, DstY, Src, SrcSubresource, pSrcRect, Format, ResolveMode);
    if (FAILED(hr))
    {
        Fail(hr);
        return;
    }

    if (DriverTable->pfnResourceResolveSubresourceRegion == nullptr)
        return;

    DriverTable->pfnResourceResolveSubresourceRegion(DriverList, Dst.Resource->GetDriverHandle(), DstSubresource,
                                                     DstX, DstY, Src.Resource->GetDriverHandle(), SrcSubresource,
                                                     pSrcRect, Format, (D3D12DDI_RESOLVE_MODE)ResolveMode);
}

/* Queries */

/* The query types a heap of this type holds */
VOID
RxHeapQueryTypes(
    _In_ D3D12_QUERY_HEAP_TYPE HeapType,
    _Out_ INT* First,
    _Out_ INT* Last)
{
    switch (HeapType)
    {
        case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
            *First = D3D12_QUERY_TYPE_OCCLUSION;
            *Last = D3D12_QUERY_TYPE_BINARY_OCCLUSION;
            break;
        case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
        case D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP:
            *First = *Last = D3D12_QUERY_TYPE_TIMESTAMP;
            break;
        case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
            *First = *Last = D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
            break;
        case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
            *First = D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
            *Last = D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3;
            break;
        case D3D12_QUERY_HEAP_TYPE_VIDEO_DECODE_STATISTICS:
            *First = *Last = D3D12_QUERY_TYPE_VIDEO_DECODE_STATISTICS;
            break;
        default:
            *First = *Last = D3D12_QUERY_TYPE_PIPELINE_STATISTICS1;
            break;
    }
}

/* Heaps whose queries are begun and ended in pairs; timestamps and decode statistics are only ended */
INT
RxHeapPairs(
    _In_ D3D12_QUERY_HEAP_TYPE HeapType)
{
    return HeapType != D3D12_QUERY_HEAP_TYPE_TIMESTAMP && HeapType != D3D12_QUERY_HEAP_TYPE_VIDEO_DECODE_STATISTICS &&
           HeapType != D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP;
}

/* Query types a list type records, as bits by D3D12_QUERY_TYPE */
static
UINT
RxListQueryTypes(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    const UINT Timestamp = 1u << D3D12_QUERY_TYPE_TIMESTAMP;

    switch (Type)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            return 0xFFu | (1u << D3D12_QUERY_TYPE_PIPELINE_STATISTICS1);
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
        case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
            return Timestamp;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return Device.GetOptions().CopyQueueTimestampQueriesSupported ? Timestamp : 0;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
            return Timestamp | (1u << D3D12_QUERY_TYPE_VIDEO_DECODE_STATISTICS);
        default:
            return 0;
    }
}

/* Query heap types a list type resolves, as bits by D3D12_QUERY_HEAP_TYPE */
UINT
RxListQueryHeaps(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    const UINT Timestamp = 1u << D3D12_QUERY_HEAP_TYPE_TIMESTAMP;

    switch (Type)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            return (1u << D3D12_QUERY_HEAP_TYPE_OCCLUSION) | Timestamp | (1u << D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS) |
                   (1u << D3D12_QUERY_HEAP_TYPE_SO_STATISTICS) | (1u << D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1);
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
        case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
            return Timestamp;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return Device.GetOptions().CopyQueueTimestampQueriesSupported ? 1u << D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP
                                                                           : 0;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
            return Timestamp | (1u << D3D12_QUERY_HEAP_TYPE_VIDEO_DECODE_STATISTICS);
        default:
            return 0;
    }
}

/* The checks Begin and End share; each broken one fails the list */
BOOLEAN
RxQueryIsValid(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ const RxQueryHeap& Heap,
    _In_ D3D12_QUERY_TYPE QueryType,
    _In_ UINT Index)
{
    BOOLEAN Valid = TRUE;
    INT First;
    INT Last;

    RxHeapQueryTypes(Heap.GetType(), &First, &Last);

    if (Index >= Heap.GetCount())
        Valid = FALSE;
    if ((INT)QueryType < First || (INT)QueryType > Last)
        Valid = FALSE;
    if ((RxListQueryTypes(Device, ListType) & (1u << ((UINT)QueryType & 31))) == 0)
        Valid = FALSE;

    /* Copy queues time through their own heap type and nothing else does */
    if (Heap.GetType() == D3D12_QUERY_HEAP_TYPE_TIMESTAMP && ListType == D3D12_COMMAND_LIST_TYPE_COPY)
        Valid = FALSE;
    if (Heap.GetType() == D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP && ListType != D3D12_COMMAND_LIST_TYPE_COPY)
        Valid = FALSE;

    return Valid;
}

VOID
STDMETHODCALLTYPE
RxCommandList::BeginQuery(
    _In_ ID3D12QueryHeap* pQueryHeap,
    _In_ D3D12_QUERY_TYPE QueryType,
    _In_ UINT Index)
{
    RxQueryHeap* Heap = RxUnwrap12<RxQueryHeap>(pQueryHeap);

    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListDoesGraphics | RxListTopLevel) || !Records())
        return;

    if (Heap == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    if (!RxQueryIsValid(Device, Type, *Heap, QueryType, Index))
        Fail(E_INVALIDARG);

    /* Timestamps and decode statistics are only ever ended */
    if (QueryType == D3D12_QUERY_TYPE_TIMESTAMP || QueryType == D3D12_QUERY_TYPE_VIDEO_DECODE_STATISTICS)
        Fail(E_INVALIDARG);

    OpenQueries += RxHeapPairs(Heap->GetType());
    if (!Records())
        return;

    DriverTable->pfnBeginQuery(DriverList, Heap->GetDriverHandle(), (D3D12DDI_QUERY_TYPE)QueryType, Index);
}

VOID
STDMETHODCALLTYPE
RxCommandList::EndQuery(
    _In_ ID3D12QueryHeap* pQueryHeap,
    _In_ D3D12_QUERY_TYPE QueryType,
    _In_ UINT Index)
{
    RxQueryHeap* Heap = RxUnwrap12<RxQueryHeap>(pQueryHeap);

    if (!Takes(RxListDoesCopy | RxListTopLevel) || !Records())
        return;

    if (Heap == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    if (!RxQueryIsValid(Device, Type, *Heap, QueryType, Index))
        Fail(E_INVALIDARG);

    OpenQueries -= RxHeapPairs(Heap->GetType());
    if (!Records())
        return;

    DriverTable->pfnEndQuery(DriverList, Heap->GetDriverHandle(), (D3D12DDI_QUERY_TYPE)QueryType, Index);
}

VOID
STDMETHODCALLTYPE
RxCommandList::ResolveQueryData(
    _In_ ID3D12QueryHeap* pQueryHeap,
    _In_ D3D12_QUERY_TYPE QueryType,
    _In_ UINT StartIndex,
    _In_ UINT NumQueries,
    _In_ ID3D12Resource* pDestinationBuffer,
    _In_ UINT64 AlignedDestinationBufferOffset)
{
    RxQueryHeap* Heap = RxUnwrap12<RxQueryHeap>(pQueryHeap);
    RxResourceFacts Destination;

    if (!Takes(RxListDoesCopy | RxListTopLevel) || !Records() || !PassAllows(FALSE))
        return;

    if (Heap == nullptr || !RxReadResource(pDestinationBuffer, &Destination) ||
        !RxResolveIsValid(Device, Type, *Heap, QueryType, StartIndex, NumQueries, Destination,
                          AlignedDestinationBufferOffset))
    {
        Fail(E_INVALIDARG);
        return;
    }

    DriverTable->pfnResolveQueryData(DriverList, Heap->GetDriverHandle(), (D3D12DDI_QUERY_TYPE)QueryType, StartIndex,
                                     NumQueries, Destination.Resource->GetDriverHandle(), AlignedDestinationBufferOffset);
}

BOOLEAN
RxResolveIsValid(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ const RxQueryHeap& Heap,
    _In_ D3D12_QUERY_TYPE QueryType,
    _In_ UINT StartIndex,
    _In_ UINT NumQueries,
    _In_ const RxResourceFacts& Destination,
    _In_ UINT64 Offset)
{
    BOOLEAN Valid = TRUE;
    UINT64 Start;
    INT First;
    INT Last;

    if (Destination.Desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || Destination.HeapType == D3D12_HEAP_TYPE_UPLOAD)
        Valid = FALSE;
    if (StartIndex + NumQueries < NumQueries || StartIndex + NumQueries > Heap.GetCount())
        Valid = FALSE;

    /* The results land whole inside the buffer, on 8 byte boundaries */
    if (NumQueries != 0)
    {
        Start = (UINT64)Heap.GetResultSize() * (NumQueries - 1);
        if (Start + Offset < Start || Start + Offset + Heap.GetResultSize() < Start + Offset ||
            Start + Offset + Heap.GetResultSize() > Destination.Desc->Width)
        {
            Valid = FALSE;
        }
    }
    if ((Offset & 7) != 0)
        Valid = FALSE;

    RxHeapQueryTypes(Heap.GetType(), &First, &Last);
    if ((INT)QueryType < First || (INT)QueryType > Last)
        Valid = FALSE;
    if ((RxListQueryHeaps(Device, ListType) & (1u << ((UINT)Heap.GetType() & 31))) == 0)
        Valid = FALSE;

    return Valid;
}

/* Immediate and atomic buffer writes */

/* The engine flag a list type's work runs on */
UINT
RxEngineOf(
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    switch (Type)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
        case D3D12_COMMAND_LIST_TYPE_BUNDLE:
            return D3D12DDI_COMMAND_QUEUE_FLAG_3D;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            return D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return D3D12DDI_COMMAND_QUEUE_FLAG_COPY;
        default:
            return RxQueueFlagsOf(Type);
    }
}

VOID
STDMETHODCALLTYPE
RxCommandList::WriteBufferImmediate(
    UINT Count,
    _In_reads_(Count) const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* pParams,
    _In_reads_opt_(Count) const D3D12_WRITEBUFFERIMMEDIATE_MODE* pModes)
{
    static_assert(sizeof(D3D12_WRITEBUFFERIMMEDIATE_PARAMETER) == sizeof(D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032));
    UINT Index;

    if (!Records())
        return;

    if ((Device.GetOptions().WriteBufferImmediateQueueFlags & RxEngineOf(Type)) == 0)
    {
        Fail(E_INVALIDARG);
        return;
    }

    /* Addresses and modes are only checked when modes are given */
    if (pModes != nullptr)
    {
        for (Index = 0; Index < Count; Index++)
        {
            if ((pParams[Index].Dest & 3) != 0 || (UINT)pModes[Index] > D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT)
            {
                Fail(E_INVALIDARG);
                return;
            }
        }
    }

    DriverTable->pfnWriteBufferImmediate(DriverList, Count,
                                         reinterpret_cast<const D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032*>(pParams),
                                         reinterpret_cast<const D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_0032*>(pModes));
}

/* A range a dependent resource names: all of it, or a nonempty range inside one subresource */
static
BOOLEAN
RxDependencyIsValid(
    _In_ const RxResourceFacts& Facts,
    _In_ const D3D12_SUBRESOURCE_RANGE_UINT64& Range)
{
    if (Range.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        return Range.Range.Begin == 0 && Range.Range.End == UINT64_MAX;
    if (Range.Range.Begin >= Range.Range.End)
        return FALSE;

    if (Facts.Desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return Range.Subresource == 0 && (Range.Range.End <= Facts.Desc->Width || Range.Range.End == UINT64_MAX);

    return Range.Subresource < Facts.Subresources;
}

/* Copies one value, ordered after the dependent ranges are written and before anything reads it */
static
BOOLEAN
RxAtomicCopyIsValid(
    _In_ const RxResourceFacts& Dst,
    _In_ UINT64 DstOffset,
    _In_ const RxResourceFacts& Src,
    _In_ UINT64 SrcOffset,
    _In_ UINT Size,
    _In_ UINT Dependencies,
    _In_reads_(Dependencies) ID3D12Resource* const* ppDependentResources,
    _In_reads_(Dependencies) const D3D12_SUBRESOURCE_RANGE_UINT64* pDependentSubresourceRanges)
{
    RxResourceFacts Dependent;
    UINT Index;

    if (Dst.Desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || Src.Desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
        return FALSE;
    if (DstOffset + Size < DstOffset || DstOffset + Size > Dst.Desc->Width)
        return FALSE;
    if (SrcOffset + Size < SrcOffset || SrcOffset + Size > Src.Desc->Width)
        return FALSE;

    /* Copies of 64 bit values are only atomic on nodes sharing memory with the CPU */
    if (Size == sizeof(UINT64) && !Dst.Resource->GetOwner().IsUma())
        return FALSE;
    if (((DstOffset | SrcOffset) & (Size - 1)) != 0)
        return FALSE;

    for (Index = 0; Index < Dependencies; Index++)
    {
        if (!RxReadResource(ppDependentResources[Index], &Dependent) ||
            !RxDependencyIsValid(Dependent, pDependentSubresourceRanges[Index]))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static
VOID
RxAtomicCopy(
    _In_ RxDevice12& Device,
    _In_ const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114& Table,
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HRESOURCE Dst,
    _In_ UINT64 DstOffset,
    _In_ D3D12DDI_HRESOURCE Src,
    _In_ UINT64 SrcOffset,
    _In_ UINT Size,
    _In_ UINT Dependencies,
    _In_reads_(Dependencies) ID3D12Resource* const* ppDependentResources,
    _In_reads_(Dependencies) const D3D12_SUBRESOURCE_RANGE_UINT64* pDependentSubresourceRanges)
{
    const BOOLEAN Enhanced = Device.GetOptions().EnhancedBarriersSupported && Table.pfnBarrier != nullptr;
    D3D12DDIARG_BUFFER_PLACEMENT Destination = {};
    D3D12DDIARG_BUFFER_PLACEMENT Source = {};
    D3D12DDIARG_BARRIER_0094* Barriers = nullptr;
    D3D12DDIARG_RESOURCE_BARRIER_0022* Legacy = nullptr;
    UINT Index;

    /* The dependent ranges and the source are acquired together, then the destination is released */
    if (Enhanced)
        Barriers = new (std::nothrow) D3D12DDIARG_BARRIER_0094[Dependencies + 1]();
    else
        Legacy = new (std::nothrow) D3D12DDIARG_RESOURCE_BARRIER_0022[Dependencies + 1]();
    if (Barriers == nullptr && Legacy == nullptr)
        return;

    for (Index = 0; Index <= Dependencies; Index++)
    {
        BOOLEAN IsSource = Index == Dependencies;
        D3D12DDI_HRESOURCE Resource = IsSource ? Src : RxDriverResource(ppDependentResources[Index]);
        UINT Subresource = IsSource ? 0 : pDependentSubresourceRanges[Index].Subresource;
        UINT64 Begin = IsSource ? SrcOffset : pDependentSubresourceRanges[Index].Range.Begin;
        UINT64 End = IsSource ? SrcOffset + Size : pDependentSubresourceRanges[Index].Range.End;

        if (Enhanced)
        {
            Barriers[Index].Type = D3D12DDI_BARRIER_TYPE_RANGED;
            Barriers[Index].RangedBarrier.Flags = IsSource ? D3D12DDI_RANGED_BARRIER_FLAG_ATOMIC_COPY_0094 |
                                                                 D3D12DDI_RANGED_BARRIER_FLAG_ACQUIRE_0094
                                                           : D3D12DDI_RANGED_BARRIER_FLAG_ACQUIRE_0094;
            Barriers[Index].RangedBarrier.hResource = Resource;
            Barriers[Index].RangedBarrier.Subresource = Subresource;
            Barriers[Index].RangedBarrier.Range.Begin = Begin;
            Barriers[Index].RangedBarrier.Range.End = End;
        }
        else
        {
            Legacy[Index].Type = D3D12DDI_RESOURCE_BARRIER_TYPE_0022_RANGED;
            Legacy[Index].Flags = IsSource ? (D3D12DDI_RESOURCE_BARRIER_FLAGS)(D3D12DDI_RESOURCE_BARRIER_FLAG_END_ONLY |
                                                                               D3D12DDI_RESOURCE_BARRIER_FLAG_0022_ATOMIC_COPY)
                                           : D3D12DDI_RESOURCE_BARRIER_FLAG_END_ONLY;
            Legacy[Index].Ranged.hResource = Resource;
            Legacy[Index].Ranged.Subresource = Subresource;
            Legacy[Index].Ranged.Range.Begin = Begin;
            Legacy[Index].Ranged.Range.End = End;
        }
    }

    if (Enhanced)
        Table.pfnBarrier(List, Dependencies + 1, Barriers);
    else
        Table.pfnResourceBarrier(List, Dependencies + 1, Legacy);

    Destination.BaseAddress.UMD.hResource = Dst;
    Destination.BaseAddress.UMD.Offset = DstOffset;
    Source.BaseAddress.UMD.hResource = Src;
    Source.BaseAddress.UMD.Offset = SrcOffset;
    Table.pfnAtomicCopyBufferRegion(List, Destination, Source, Size);

    if (Enhanced)
    {
        ZeroMemory(&Barriers[0], sizeof(Barriers[0]));
        Barriers[0].Type = D3D12DDI_BARRIER_TYPE_RANGED;
        Barriers[0].RangedBarrier.Flags = D3D12DDI_RANGED_BARRIER_FLAG_ATOMIC_COPY_0094 |
                                          D3D12DDI_RANGED_BARRIER_FLAG_RELEASE_0094;
        Barriers[0].RangedBarrier.hResource = Dst;
        Barriers[0].RangedBarrier.Range.Begin = DstOffset;
        Barriers[0].RangedBarrier.Range.End = DstOffset + Size;
        Table.pfnBarrier(List, 1, Barriers);
    }
    else
    {
        ZeroMemory(&Legacy[0], sizeof(Legacy[0]));
        Legacy[0].Type = D3D12DDI_RESOURCE_BARRIER_TYPE_0022_RANGED;
        Legacy[0].Flags = (D3D12DDI_RESOURCE_BARRIER_FLAGS)(D3D12DDI_RESOURCE_BARRIER_FLAG_BEGIN_ONLY |
                                                            D3D12DDI_RESOURCE_BARRIER_FLAG_0022_ATOMIC_COPY);
        Legacy[0].Ranged.hResource = Dst;
        Legacy[0].Ranged.Range.Begin = DstOffset;
        Legacy[0].Ranged.Range.End = DstOffset + Size;
        Table.pfnResourceBarrier(List, 1, Legacy);
    }

    delete[] Barriers;
    delete[] Legacy;
}

VOID
STDMETHODCALLTYPE
RxCommandList::AtomicCopyBufferUINT(
    _In_ ID3D12Resource* pDstBuffer,
    UINT64 DstOffset,
    _In_ ID3D12Resource* pSrcBuffer,
    UINT64 SrcOffset,
    UINT Dependencies,
    _In_reads_(Dependencies) ID3D12Resource* const* ppDependentResources,
    _In_reads_(Dependencies) const D3D12_SUBRESOURCE_RANGE_UINT64* pDependentSubresourceRanges)
{
    RxResourceFacts Dst;
    RxResourceFacts Src;

    if (!Takes(RxListDoesCopy | RxListTopLevel) || !Records() || !PassAllows(FALSE))
        return;

    if (!RxReadResource(pDstBuffer, &Dst) || !RxReadResource(pSrcBuffer, &Src) ||
        (Dependencies != 0 && (ppDependentResources == nullptr || pDependentSubresourceRanges == nullptr)) ||
        !RxAtomicCopyIsValid(Dst, DstOffset, Src, SrcOffset, sizeof(UINT), Dependencies, ppDependentResources,
                             pDependentSubresourceRanges))
    {
        Fail(E_INVALIDARG);
        return;
    }

    if (DriverTable->pfnAtomicCopyBufferRegion == nullptr)
        return;

    RxAtomicCopy(Device, *DriverTable, DriverList, Dst.Resource->GetDriverHandle(), DstOffset,
                 Src.Resource->GetDriverHandle(), SrcOffset, sizeof(UINT), Dependencies, ppDependentResources,
                 pDependentSubresourceRanges);
}

VOID
STDMETHODCALLTYPE
RxCommandList::AtomicCopyBufferUINT64(
    _In_ ID3D12Resource* pDstBuffer,
    UINT64 DstOffset,
    _In_ ID3D12Resource* pSrcBuffer,
    UINT64 SrcOffset,
    UINT Dependencies,
    _In_reads_(Dependencies) ID3D12Resource* const* ppDependentResources,
    _In_reads_(Dependencies) const D3D12_SUBRESOURCE_RANGE_UINT64* pDependentSubresourceRanges)
{
    RxResourceFacts Dst;
    RxResourceFacts Src;

    if (!Takes(RxListDoesCopy | RxListTopLevel) || !Records() || !PassAllows(FALSE))
        return;

    if (!RxReadResource(pDstBuffer, &Dst) || !RxReadResource(pSrcBuffer, &Src) ||
        (Dependencies != 0 && (ppDependentResources == nullptr || pDependentSubresourceRanges == nullptr)) ||
        !RxAtomicCopyIsValid(Dst, DstOffset, Src, SrcOffset, sizeof(UINT64), Dependencies, ppDependentResources,
                             pDependentSubresourceRanges))
    {
        Fail(E_INVALIDARG);
        return;
    }

    if (DriverTable->pfnAtomicCopyBufferRegion == nullptr)
        return;

    RxAtomicCopy(Device, *DriverTable, DriverList, Dst.Resource->GetDriverHandle(), DstOffset,
                 Src.Resource->GetDriverHandle(), SrcOffset, sizeof(UINT64), Dependencies, ppDependentResources,
                 pDependentSubresourceRanges);
}
