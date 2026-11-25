/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12GraphicsCommandList: pipeline, root arguments and fixed function state
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "commandlist_internal.hpp"
#include "session.hpp"

static_assert(sizeof(D3D12_VERTEX_BUFFER_VIEW) == sizeof(D3D12DDI_VERTEX_BUFFER_VIEW));
static_assert(sizeof(D3D12_INDEX_BUFFER_VIEW) == sizeof(D3D12DDI_INDEX_BUFFER_VIEW));
static_assert(sizeof(D3D12_STREAM_OUTPUT_BUFFER_VIEW) == sizeof(D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW));
static_assert(sizeof(D3D12_SAMPLE_POSITION) == sizeof(D3D12DDI_SAMPLE_POSITION));

VOID
STDMETHODCALLTYPE
RxCommandList::ClearState(
    _In_opt_ ID3D12PipelineState* pPipelineState)
{
    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListTopLevel) || !Records())
        return;

    if (Type == D3D12_COMMAND_LIST_TYPE_DIRECT && InPass)
    {
        Fail(E_FAIL);
        return;
    }

    ResetPipeline(pPipelineState);
    ResetState();
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetPipelineState(
    _In_ ID3D12PipelineState* pPipelineState)
{
    RxPipelineState* Pipeline = RxUnwrap12<RxPipelineState>(pPipelineState);

    if (!Takes(RxListDoesCompute) || !Records() || Pipeline == nullptr)
        return;

    DriverTable->pfnSetPipelineState(Api(), Pipeline->Api());
}

/* A root signature as list entries name it; none unbinds */
static
D3D12DDI_API_HROOTSIGNATURE
RxRootHandle(
    _In_opt_ ID3D12RootSignature* RootSignature)
{
    RxRootSignature* Root = RxUnwrap12<RxRootSignature>(RootSignature);
    D3D12DDI_API_HROOTSIGNATURE None = {};

    return Root != nullptr ? Root->Api() : None;
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetComputeRootSignature(
    _In_opt_ ID3D12RootSignature* pRootSignature)
{
    if (!Takes(RxListDoesCompute) || !Records())
        return;

    DriverTable->pfnSetComputeRootSignature(Api(), RxRootHandle(pRootSignature));
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetGraphicsRootSignature(
    _In_opt_ ID3D12RootSignature* pRootSignature)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnSetGraphicsRootSignature(Api(), RxRootHandle(pRootSignature));
}

/* Root arguments go to the driver as given: shader visible handles and addresses are the driver's own */

VOID
STDMETHODCALLTYPE
RxCommandList::SetComputeRootDescriptorTable(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    D3D12DDI_GPU_DESCRIPTOR_HANDLE Base;

    if (!Takes(RxListDoesCompute) || !Records())
        return;

    Base.ptr = BaseDescriptor.ptr;
    DriverTable->pfnSetComputeRootDescriptorTable(Api(), RootParameterIndex, Base);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetGraphicsRootDescriptorTable(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    D3D12DDI_GPU_DESCRIPTOR_HANDLE Base;

    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    Base.ptr = BaseDescriptor.ptr;
    DriverTable->pfnSetGraphicsRootDescriptorTable(Api(), RootParameterIndex, Base);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetComputeRoot32BitConstant(
    _In_ UINT RootParameterIndex,
    _In_ UINT SrcData,
    _In_ UINT DestOffsetIn32BitValues)
{
    if (!Takes(RxListDoesCompute) || !Records())
        return;

    DriverTable->pfnSetComputeRoot32BitConstant(Api(), RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetGraphicsRoot32BitConstant(
    _In_ UINT RootParameterIndex,
    _In_ UINT SrcData,
    _In_ UINT DestOffsetIn32BitValues)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnSetGraphicsRoot32BitConstant(Api(), RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetComputeRoot32BitConstants(
    _In_ UINT RootParameterIndex,
    _In_ UINT Num32BitValuesToSet,
    _In_reads_(Num32BitValuesToSet* sizeof(UINT)) const void* pSrcData,
    _In_ UINT DestOffsetIn32BitValues)
{
    if (!Takes(RxListDoesCompute) || !Records())
        return;

    DriverTable->pfnSetComputeRoot32BitConstants(Api(), RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                                 DestOffsetIn32BitValues);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetGraphicsRoot32BitConstants(
    _In_ UINT RootParameterIndex,
    _In_ UINT Num32BitValuesToSet,
    _In_reads_(Num32BitValuesToSet* sizeof(UINT)) const void* pSrcData,
    _In_ UINT DestOffsetIn32BitValues)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnSetGraphicsRoot32BitConstants(Api(), RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                                  DestOffsetIn32BitValues);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetComputeRootConstantBufferView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (!Takes(RxListDoesCompute) || !Records())
        return;

    DriverTable->pfnSetComputeRootConstantBufferView(Api(), RootParameterIndex, BufferLocation);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetGraphicsRootConstantBufferView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnSetGraphicsRootConstantBufferView(Api(), RootParameterIndex, BufferLocation);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetComputeRootShaderResourceView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (!Takes(RxListDoesCompute) || !Records())
        return;

    DriverTable->pfnSetComputeRootShaderResourceView(Api(), RootParameterIndex, BufferLocation);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetGraphicsRootShaderResourceView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnSetGraphicsRootShaderResourceView(Api(), RootParameterIndex, BufferLocation);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetComputeRootUnorderedAccessView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (!Takes(RxListDoesCompute) || !Records())
        return;

    DriverTable->pfnSetComputeRootUnorderedAccessView(Api(), RootParameterIndex, BufferLocation);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetGraphicsRootUnorderedAccessView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnSetGraphicsRootUnorderedAccessView(Api(), RootParameterIndex, BufferLocation);
}

/* Input assembly and stream output */

VOID
STDMETHODCALLTYPE
RxCommandList::IASetIndexBuffer(
    _In_opt_ const D3D12_INDEX_BUFFER_VIEW* pView)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnIASetIndexBuffer(Api(), reinterpret_cast<const D3D12DDI_INDEX_BUFFER_VIEW*>(pView));
}

VOID
STDMETHODCALLTYPE
RxCommandList::IASetVertexBuffers(
    _In_ UINT StartSlot,
    _In_ UINT NumViews,
    _In_reads_opt_(NumViews) const D3D12_VERTEX_BUFFER_VIEW* pViews)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnIASetVertexBuffers(Api(), StartSlot, NumViews,
                                       reinterpret_cast<const D3D12DDI_VERTEX_BUFFER_VIEW*>(pViews));
}

VOID
STDMETHODCALLTYPE
RxCommandList::IASetIndexBufferStripCutValue(
    _In_ D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnIASetIndexBufferStripCutValue(Api(), (D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE)IBStripCutValue);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SOSetTargets(
    _In_ UINT StartSlot,
    _In_ UINT NumViews,
    _In_reads_opt_(NumViews) const D3D12_STREAM_OUTPUT_BUFFER_VIEW* pViews)
{
    if (!Takes(RxListDoesGraphics | RxListTopLevel) || !Records())
        return;

    DriverTable->pfnSOSetTargets(Api(), StartSlot, NumViews,
                                 reinterpret_cast<const D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW*>(pViews));
}

/* Output merger and rasterizer */

VOID
STDMETHODCALLTYPE
RxCommandList::OMSetDepthBounds(
    _In_ FLOAT Min,
    _In_ FLOAT Max)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    /* A NaN bound reaches the driver as 0 */
    if (Min != Min)
        Min = 0.0f;
    if (Max != Max)
        Max = 0.0f;

    DriverTable->pfnOMSetDepthBounds(Api(), Min, Max);
}

VOID
STDMETHODCALLTYPE
RxCommandList::OMSetFrontAndBackStencilRef(
    _In_ UINT FrontStencilRef,
    _In_ UINT BackStencilRef)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    if (DriverTable->pfnOmSetFrontAndBackStencilRef != nullptr)
        DriverTable->pfnOmSetFrontAndBackStencilRef(Api(), FrontStencilRef, BackStencilRef);
    else
        DriverTable->pfnOmSetStencilRef(Api(), FrontStencilRef);
}

VOID
STDMETHODCALLTYPE
RxCommandList::RSSetDepthBias(
    _In_ FLOAT DepthBias,
    _In_ FLOAT DepthBiasClamp,
    _In_ FLOAT SlopeScaledDepthBias)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnRSSetDepthBias(Api(), DepthBias, DepthBiasClamp, SlopeScaledDepthBias);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetSamplePositions(
    _In_ UINT NumSamplesPerPixel,
    _In_ UINT NumPixels,
    _In_reads_(NumSamplesPerPixel* NumPixels) D3D12_SAMPLE_POSITION* pSamplePositions)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnSetSamplePositions(Api(), NumSamplesPerPixel, NumPixels,
                                       reinterpret_cast<D3D12DDI_SAMPLE_POSITION*>(pSamplePositions));
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetViewInstanceMask(
    _In_ UINT Mask)
{
    if (!Takes(RxListDoesGraphics) || !Records())
        return;

    DriverTable->pfnSetViewInstanceMask(Api(), Mask);
}

/* Variable rate shading */

static
BOOLEAN
RxShadingRateKnown(
    _In_ D3D12_SHADING_RATE Rate,
    _In_ BOOLEAN Additional)
{
    switch (Rate)
    {
        case D3D12_SHADING_RATE_1X1:
        case D3D12_SHADING_RATE_1X2:
        case D3D12_SHADING_RATE_2X1:
        case D3D12_SHADING_RATE_2X2:
            return TRUE;
        case D3D12_SHADING_RATE_2X4:
        case D3D12_SHADING_RATE_4X2:
        case D3D12_SHADING_RATE_4X4:
            return Additional;
        default:
            return FALSE;
    }
}

static
BOOLEAN
RxCombinerKnown(
    _In_ D3D12_SHADING_RATE_COMBINER Combiner)
{
    return (UINT)Combiner <= D3D12_SHADING_RATE_COMBINER_SUM;
}

VOID
STDMETHODCALLTYPE
RxCommandList::RSSetShadingRate(
    _In_ D3D12_SHADING_RATE baseShadingRate,
    _In_reads_opt_(D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT) const D3D12_SHADING_RATE_COMBINER* combiners)
{
    static_assert(sizeof(D3D12_SHADING_RATE_COMBINER) == sizeof(D3D12DDI_SHADING_RATE_COMBINER_0062));

    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListDoesGraphics) || !Records())
        return;

    if (!RxShadingRateKnown(baseShadingRate, Device.GetOptions().AdditionalShadingRatesSupported != FALSE) ||
        (combiners != nullptr && (!RxCombinerKnown(combiners[0]) || !RxCombinerKnown(combiners[1]))))
    {
        Fail(E_INVALIDARG);
        return;
    }

    if (DriverTable->pfnRSSetShadingRate == nullptr)
        return;

    DriverTable->pfnRSSetShadingRate(Api(), (D3D12DDI_SHADING_RATE_0062)baseShadingRate,
                                     reinterpret_cast<const D3D12DDI_SHADING_RATE_COMBINER_0062*>(combiners));
}

VOID
STDMETHODCALLTYPE
RxCommandList::RSSetShadingRateImage(
    _In_opt_ ID3D12Resource* shadingRateImage)
{
    RxResourceFacts Image;
    D3D12DDI_HRESOURCE Handle = {};

    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListDoesGraphics) || !Records())
        return;

    /* The image is a single R8_UINT texture without mips, arrays or samples */
    if (shadingRateImage != nullptr)
    {
        if (!RxReadResource(shadingRateImage, &Image) || Image.Desc->Format != DXGI_FORMAT_R8_UINT ||
            (Image.Desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D && Image.Desc->DepthOrArraySize != 1) ||
            Image.Desc->MipLevels != 1 || Image.Desc->SampleDesc.Count != 1 || Image.Desc->SampleDesc.Quality != 0)
        {
            Fail(E_INVALIDARG);
            return;
        }

        Handle = Image.Resource->GetDriverHandle();
    }

    if (DriverTable->pfnRSSetShadingRateImage == nullptr)
        return;

    DriverTable->pfnRSSetShadingRateImage(DriverList, Handle);
}

/* Predication and protected content */

VOID
STDMETHODCALLTYPE
RxCommandList::SetPredication(
    _In_opt_ ID3D12Resource* pBuffer,
    _In_ UINT64 AlignedBufferOffset,
    _In_ D3D12_PREDICATION_OP Operation)
{
    D3D12DDI_HRESOURCE Handle;

    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListTopLevel) || !Records())
        return;

    if (!RxPredicationIsValid(pBuffer, AlignedBufferOffset, Operation, &Handle))
    {
        Fail(E_INVALIDARG);
        return;
    }

    DriverTable->pfnSetPredication(DriverList, Handle, AlignedBufferOffset, (D3D12DDI_PREDICATION_OP)Operation);
}

BOOLEAN
RxPredicationIsValid(
    _In_opt_ ID3D12Resource* pBuffer,
    _In_ UINT64 AlignedBufferOffset,
    _In_ D3D12_PREDICATION_OP Operation,
    _Out_ D3D12DDI_HRESOURCE* Handle)
{
    RxResourceFacts Buffer;

    ZeroMemory(Handle, sizeof(*Handle));

    /* No buffer turns predication off and takes nothing else */
    if (pBuffer == nullptr)
        return AlignedBufferOffset == 0 && Operation == D3D12_PREDICATION_OP_EQUAL_ZERO;

    if (!RxReadResource(pBuffer, &Buffer))
        return FALSE;

    *Handle = Buffer.Resource->GetDriverHandle();
    return Buffer.Desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && Buffer.HeapType != D3D12_HEAP_TYPE_READBACK &&
           (AlignedBufferOffset & 7) == 0 && (UINT)Operation <= D3D12_PREDICATION_OP_NOT_EQUAL_ZERO &&
           AlignedBufferOffset + sizeof(UINT64) >= AlignedBufferOffset &&
           AlignedBufferOffset + sizeof(UINT64) <= Buffer.Desc->Width;
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetProtectedResourceSession(
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession)
{
    RxProtectedResourceSession* Session = RxUnwrap12<RxProtectedResourceSession>(pProtectedResourceSession);
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 Handle = {};

    if (!Takes(RxListDoesCopy | RxListTopLevel) || !Records())
        return;

    if (Type == D3D12_COMMAND_LIST_TYPE_DIRECT && InPass)
    {
        Fail(E_FAIL);
        return;
    }

    /* A list on a node without sessions only takes none, which then changes nothing */
    if (!TakesSessions || (pProtectedResourceSession != nullptr && Session == nullptr))
    {
        if (pProtectedResourceSession != nullptr)
            Fail(E_INVALIDARG);
        return;
    }

    if (OpenQueries != 0)
    {
        Fail(E_FAIL);
        return;
    }

    if (Session != nullptr)
        Handle = Session->GetDriverHandle();
    DriverTable->pfnSetProtectedResourceSession(DriverList, Handle);
}
