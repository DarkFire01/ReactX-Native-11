/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Helpers the command list recording files share
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "pipeline.hpp"
#include "formatlayout12.hpp"
#include "formatsupport12.hpp"

/* What command checks read from a resource */
struct RxResourceFacts
{
    RxResource* Resource;
    const D3D12_RESOURCE_DESC1* Desc;
    D3D12_HEAP_TYPE HeapType;
    UINT Subresources;
};

/* FALSE for a pointer that is not a ReactX resource */
inline
BOOLEAN
RxReadResource(
    _In_opt_ ID3D12Resource* Resource,
    _Out_ RxResourceFacts* Facts)
{
    D3D12_HEAP_PROPERTIES Properties = {};

    ZeroMemory(Facts, sizeof(*Facts));
    Facts->Resource = RxUnwrap12<RxResource>(Resource);
    if (Facts->Resource == nullptr)
        return FALSE;

    Facts->Desc = &Facts->Resource->GetApiDesc();
    if (SUCCEEDED(Facts->Resource->GetHeapProperties(&Properties, nullptr)))
        Facts->HeapType = Properties.Type;

    if (Facts->Desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        Facts->Subresources = 1;
    }
    else
    {
        Facts->Subresources = Facts->Desc->MipLevels * RxFormatPlaneCount(Facts->Desc->Format);
        if (Facts->Desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            Facts->Subresources *= Facts->Desc->DepthOrArraySize;
    }

    return TRUE;
}

inline
D3D12DDI_HRESOURCE
RxDriverResource(
    _In_opt_ ID3D12Resource* Resource)
{
    RxResource* Found = RxUnwrap12<RxResource>(Resource);
    D3D12DDI_HRESOURCE None = {};

    return Found != nullptr ? Found->GetDriverHandle() : None;
}

/* A render target slot holds the driver's descriptor inside it */
inline
D3D12DDI_CPU_DESCRIPTOR_HANDLE
RxDriverTarget(
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE Handle)
{
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Driver;

    Driver.ptr = reinterpret_cast<const RxRtvEntry*>(Handle.ptr)->DriverHandle;
    return Driver;
}

/* Denormals become signed zero before clear values reach the driver */
inline
FLOAT
RxZeroDenormal(
    _In_ FLOAT Value)
{
    UINT Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    if ((Bits & 0x7F800000) == 0)
        Bits &= 0x80000000;
    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

/* Enhanced barrier rules shared by Barrier and the passes that end with barriers */
BOOLEAN
RxCheckGlobalBarrier(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ const D3D12_GLOBAL_BARRIER& Barrier);

BOOLEAN
RxCheckTextureBarrier(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ const D3D12_TEXTURE_BARRIER& Barrier);

/* Buffers take resolve accesses only on devices with sampler feedback */
BOOLEAN
RxCheckBufferBarrier(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ const D3D12_BUFFER_BARRIER& Barrier,
    _In_ BOOLEAN BufferResolves);

/* The rules a resolve meets; DstX, DstY and the rect only count outside the sampler feedback modes */
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
    _In_ D3D12_RESOLVE_MODE Mode);
