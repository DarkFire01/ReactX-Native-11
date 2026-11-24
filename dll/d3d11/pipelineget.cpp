/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pipeline getters and the instanced and indirect draws
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "shaders.hpp"
#include "states.hpp"
#include "views.hpp"

VOID
RxImmediateContext::GetShader(
    _In_ RxStage Stage,
    _In_ REFIID Interface,
    _Outptr_result_maybenull_ VOID** Shader,
    _Out_opt_ UINT* ClassInstanceCount)
{
    RxContextGuard Guard(Device);
    RxStageState& State = Pipeline.Stages[(UINT)Stage];

    /* Class instances are never bound, so the count is always zero */
    if (ClassInstanceCount != nullptr)
        *ClassInstanceCount = 0;
    if (Shader == nullptr)
        return;

    *Shader = nullptr;
    if (State.ShaderObject != nullptr)
        State.ShaderObject->QueryInterface(Interface, Shader);
}

VOID
RxImmediateContext::GetConstantBuffers(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count,
    _Out_writes_opt_(Count) ID3D11Buffer** Buffers)
{
    RxContextGuard Guard(Device);
    RxStageState& State = Pipeline.Stages[(UINT)Stage];
    UINT Index;

    if (Buffers == nullptr || Start >= State.ConstantBuffers.size() || Count > State.ConstantBuffers.size() - Start)
        return;

    for (Index = 0; Index < Count; Index++)
        Buffers[Index] = RxHandBack<ID3D11Buffer>(State.ConstantBuffers[Start + Index]);
}

VOID
RxImmediateContext::GetShaderResources(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count,
    _Out_writes_opt_(Count) ID3D11ShaderResourceView** Views)
{
    RxContextGuard Guard(Device);
    RxStageState& State = Pipeline.Stages[(UINT)Stage];
    UINT Index;

    if (Views == nullptr || Start >= State.ShaderResources.size() || Count > State.ShaderResources.size() - Start)
        return;

    for (Index = 0; Index < Count; Index++)
        Views[Index] = RxHandBack<ID3D11ShaderResourceView>(State.ShaderResources[Start + Index]);
}

VOID
RxImmediateContext::GetSamplers(
    _In_ RxStage Stage,
    _In_ UINT Start,
    _In_ UINT Count,
    _Out_writes_opt_(Count) ID3D11SamplerState** Samplers)
{
    RxContextGuard Guard(Device);
    RxStageState& State = Pipeline.Stages[(UINT)Stage];
    UINT Index;

    if (Samplers == nullptr || Start >= State.Samplers.size() || Count > State.Samplers.size() - Start)
        return;

    for (Index = 0; Index < Count; Index++)
        Samplers[Index] = RxHandBack<ID3D11SamplerState>(State.Samplers[Start + Index]);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::IAGetInputLayout(
    _Outptr_result_maybenull_ ID3D11InputLayout** ppInputLayout)
{
    RxContextGuard Guard(Device);
    if (ppInputLayout != nullptr)
        *ppInputLayout = RxHandBack<ID3D11InputLayout>(Pipeline.InputLayout);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::IAGetVertexBuffers(
    _In_range_(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppVertexBuffers,
    _Out_writes_opt_(NumBuffers) UINT* pStrides,
    _Out_writes_opt_(NumBuffers) UINT* pOffsets)
{
    RxContextGuard Guard(Device);
    UINT Index;

    if (StartSlot >= Pipeline.VertexBuffers.size() || NumBuffers > Pipeline.VertexBuffers.size() - StartSlot)
        return;

    for (Index = 0; Index < NumBuffers; Index++)
    {
        if (ppVertexBuffers != nullptr)
            ppVertexBuffers[Index] = RxHandBack<ID3D11Buffer>(Pipeline.VertexBuffers[StartSlot + Index]);
        if (pStrides != nullptr)
            pStrides[Index] = Pipeline.Strides[StartSlot + Index];
        if (pOffsets != nullptr)
            pOffsets[Index] = Pipeline.Offsets[StartSlot + Index];
    }
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::IAGetIndexBuffer(
    _Outptr_opt_result_maybenull_ ID3D11Buffer** pIndexBuffer,
    _Out_opt_ DXGI_FORMAT* Format,
    _Out_opt_ UINT* Offset)
{
    RxContextGuard Guard(Device);
    if (pIndexBuffer != nullptr)
        *pIndexBuffer = RxHandBack<ID3D11Buffer>(Pipeline.IndexBuffer);
    if (Format != nullptr)
        *Format = Pipeline.IndexFormat;
    if (Offset != nullptr)
        *Offset = Pipeline.IndexOffset;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::IAGetPrimitiveTopology(
    _Out_ D3D11_PRIMITIVE_TOPOLOGY* pTopology)
{
    RxContextGuard Guard(Device);
    if (pTopology != nullptr)
        *pTopology = Pipeline.Topology;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::RSGetState(
    _Outptr_result_maybenull_ ID3D11RasterizerState** ppRasterizerState)
{
    RxContextGuard Guard(Device);
    if (ppRasterizerState != nullptr)
        *ppRasterizerState = RxHandBack<ID3D11RasterizerState>(Pipeline.Rasterizer);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::RSGetViewports(
    _Inout_ UINT* pNumViewports,
    _Out_writes_opt_(*pNumViewports) D3D11_VIEWPORT* pViewports)
{
    RxContextGuard Guard(Device);
    UINT Index;

    if (pNumViewports == nullptr)
        return;

    /* Without an array the caller only asks for the count */
    if (pViewports == nullptr)
    {
        *pNumViewports = Pipeline.ViewportCount;
        return;
    }

    for (Index = 0; Index < *pNumViewports; Index++)
    {
        if (Index < Pipeline.ViewportCount)
        {
            const D3D10_DDI_VIEWPORT& Source = Pipeline.Viewports[Index];

            pViewports[Index] = { Source.TopLeftX, Source.TopLeftY, Source.Width, Source.Height, Source.MinDepth,
                                  Source.MaxDepth };
        }
        else
        {
            pViewports[Index] = {};
        }
    }
    *pNumViewports = Pipeline.ViewportCount;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::RSGetScissorRects(
    _Inout_ UINT* pNumRects,
    _Out_writes_opt_(*pNumRects) D3D11_RECT* pRects)
{
    RxContextGuard Guard(Device);
    UINT Index;

    if (pNumRects == nullptr)
        return;

    if (pRects == nullptr)
    {
        *pNumRects = Pipeline.ScissorCount;
        return;
    }

    for (Index = 0; Index < *pNumRects; Index++)
    {
        if (Index < Pipeline.ScissorCount)
            memcpy(&pRects[Index], &Pipeline.Scissors[Index], sizeof(D3D11_RECT));
        else
            pRects[Index] = {};
    }
    *pNumRects = Pipeline.ScissorCount;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::OMGetRenderTargets(
    _In_range_(0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D11RenderTargetView** ppRenderTargetViews,
    _Outptr_opt_result_maybenull_ ID3D11DepthStencilView** ppDepthStencilView)
{
    RxContextGuard Guard(Device);
    UINT Index;

    if (ppRenderTargetViews != nullptr && NumViews <= Pipeline.RenderTargets.size())
    {
        for (Index = 0; Index < NumViews; Index++)
            ppRenderTargetViews[Index] = RxHandBack<ID3D11RenderTargetView>(Pipeline.RenderTargets[Index]);
    }

    if (ppDepthStencilView != nullptr)
        *ppDepthStencilView = RxHandBack<ID3D11DepthStencilView>(Pipeline.DepthStencilView);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::OMGetBlendState(
    _Outptr_opt_result_maybenull_ ID3D11BlendState** ppBlendState,
    _Out_opt_ FLOAT BlendFactor[4],
    _Out_opt_ UINT* pSampleMask)
{
    RxContextGuard Guard(Device);
    if (ppBlendState != nullptr)
        *ppBlendState = RxHandBack<ID3D11BlendState>(Pipeline.Blend);
    if (BlendFactor != nullptr)
        memcpy(BlendFactor, Pipeline.BlendFactor.data(), sizeof(FLOAT) * 4);
    if (pSampleMask != nullptr)
        *pSampleMask = Pipeline.SampleMask;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::OMGetDepthStencilState(
    _Outptr_opt_result_maybenull_ ID3D11DepthStencilState** ppDepthStencilState,
    _Out_opt_ UINT* pStencilRef)
{
    RxContextGuard Guard(Device);
    if (ppDepthStencilState != nullptr)
        *ppDepthStencilState = RxHandBack<ID3D11DepthStencilState>(Pipeline.DepthStencil);
    if (pStencilRef != nullptr)
        *pStencilRef = Pipeline.StencilRef;
}

/* Instanced and indirect draws go straight to the driver */

VOID
STDMETHODCALLTYPE
RxImmediateContext::DrawInstanced(
    _In_ UINT VertexCountPerInstance,
    _In_ UINT InstanceCount,
    _In_ UINT StartVertexLocation,
    _In_ UINT StartInstanceLocation)
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnDrawInstanced(Device.GetDriverDevice(), VertexCountPerInstance, InstanceCount,
                                             StartVertexLocation, StartInstanceLocation);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DrawIndexedInstanced(
    _In_ UINT IndexCountPerInstance,
    _In_ UINT InstanceCount,
    _In_ UINT StartIndexLocation,
    _In_ INT BaseVertexLocation,
    _In_ UINT StartInstanceLocation)
{
    RxContextGuard Guard(Device);
    Device.GetDeviceFuncs().pfnDrawIndexedInstanced(Device.GetDriverDevice(), IndexCountPerInstance, InstanceCount,
                                                    StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

/* Argument buffers must have been created for indirect use; anything else is dropped */
static
RxBuffer*
RxIndirectArguments(
    _In_opt_ ID3D11Buffer* Buffer,
    _In_ UINT Offset,
    _In_ UINT Size)
{
    RxBuffer* Arguments = RxUnwrap<RxBuffer>(Buffer);
    D3D11_BUFFER_DESC Desc;

    if (Arguments == nullptr || (Offset % 4) != 0)
        return nullptr;

    Arguments->GetDesc(&Desc);
    if ((Desc.MiscFlags & D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS) == 0 || Offset > Desc.ByteWidth ||
        Size > Desc.ByteWidth - Offset)
    {
        return nullptr;
    }

    return Arguments;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DrawInstancedIndirect(
    _In_ ID3D11Buffer* pBufferForArgs,
    _In_ UINT AlignedByteOffsetForArgs)
{
    RxContextGuard Guard(Device);
    RxBuffer* Arguments = RxIndirectArguments(pBufferForArgs, AlignedByteOffsetForArgs, 4 * sizeof(UINT));

    if (Arguments != nullptr)
    {
        Device.GetDeviceFuncs().pfnDrawInstancedIndirect(Device.GetDriverDevice(),
                                                         Arguments->GetDdiResource().GetHandle(),
                                                         AlignedByteOffsetForArgs);
    }
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DrawIndexedInstancedIndirect(
    _In_ ID3D11Buffer* pBufferForArgs,
    _In_ UINT AlignedByteOffsetForArgs)
{
    RxContextGuard Guard(Device);
    RxBuffer* Arguments = RxIndirectArguments(pBufferForArgs, AlignedByteOffsetForArgs, 5 * sizeof(UINT));

    if (Arguments != nullptr)
    {
        Device.GetDeviceFuncs().pfnDrawIndexedInstancedIndirect(Device.GetDriverDevice(),
                                                                Arguments->GetDdiResource().GetHandle(),
                                                                AlignedByteOffsetForArgs);
    }
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DispatchIndirect(
    _In_ ID3D11Buffer* pBufferForArgs,
    _In_ UINT AlignedByteOffsetForArgs)
{
    RxContextGuard Guard(Device);
    RxBuffer* Arguments = RxIndirectArguments(pBufferForArgs, AlignedByteOffsetForArgs, 3 * sizeof(UINT));

    if (Arguments != nullptr)
    {
        Device.GetDeviceFuncs().pfnDispatchIndirect(Device.GetDriverDevice(), Arguments->GetDdiResource().GetHandle(),
                                                    AlignedByteOffsetForArgs);
    }
}
