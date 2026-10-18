/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Resources: the UMD side of a resource and the API objects built on it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "devicechild.hpp"
#include "dxgisurface.hpp"

/* Private interface id: QueryInterface with it yields the DdiResource behind an API resource */
DEFINE_GUID(IID_RxDdiResource, 0xf5c24dbd, 0x0949, 0x4fd0, 0xac, 0xb7, 0x7e, 0xb9, 0x9b, 0xd9, 0xbc, 0x22);

/* Private interface id: yields the lifetime of a resource made here, without a reference */
DEFINE_GUID(IID_RxChildLifetime, 0x3a6d81c4, 0x5e27, 0x4b90, 0x9f, 0x13, 0xc8, 0x42, 0x0d, 0x7b, 0xe5, 0x6a);

/* What the driver needs to know to create any texture */
struct RxTextureShape
{
    D3D10DDIRESOURCE_TYPE Dimension;
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D11_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
};

/* How DirectComposition limits drawing into a texture it handed out */
enum class RxGuard
{
    None,
    Empty,
    Rect
};

/* One resource as the UMD knows it; its record is the hRTResource the allocation callbacks receive */
class DdiResource final
{
public:
    explicit DdiResource(
        _In_ RxDevice& Owner)
        : Device(Owner),
          Runtime(new (std::nothrow) RxRuntimeResource())
    {
    }

    ~DdiResource();

    DdiResource(const DdiResource&) = delete;
    DdiResource& operator=(const DdiResource&) = delete;

    /* Extra DDI bits cover what only the runtime asks for, such as present targets */
    HRESULT
    CreateTexture(
        _In_ const RxTextureShape& Shape,
        _In_reads_opt_(Shape.MipLevels * Shape.ArraySize) const D3D11_SUBRESOURCE_DATA* InitialData,
        _In_ UINT ExtraDdiBindFlags = 0,
        _In_ UINT ExtraDdiMiscFlags = 0);

    /* Makes the next texture a swapchain buffer the compositor opens by NT handle */
    VOID
    SetSharedRecord(
        _In_ const RxSharedTextureRecord& Record)
    {
        if (Runtime == nullptr)
            return;

        Runtime->Shared = Record;
        Runtime->SharedRecord = &Runtime->Shared;
        Runtime->SharedRecordSize = sizeof(Runtime->Shared);
    }

    /* The kernel resource behind the driver's allocations, 0 before the first one */
    D3DKMT_HANDLE
    GetKmResource() const
    {
        return Runtime ? Runtime->KmResource : 0;
    }

    _Ret_maybenull_
    RxRuntimeResource*
    GetRuntimeResource() const
    {
        return Runtime;
    }

    /* Opens a resource another process shared through a global share handle */
    HRESULT
    OpenShared(
        _In_ D3DKMT_HANDLE GlobalShare);

    HRESULT
    CreateBuffer(
        _In_ const D3D11_BUFFER_DESC& Desc,
        _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData);

    /* A decoder buffer of the type and size the driver asked for */
    HRESULT
    CreateDecoderBuffer(
        _In_ const D3D11_1DDI_VIDEO_DECODER_BUFFER_INFO& Info);

    HRESULT
    Map(
        _In_ UINT Subresource,
        _In_ D3D11_MAP MapType,
        _In_ UINT MapFlags,
        _Out_ D3D11_MAPPED_SUBRESOURCE* Mapped);

    VOID
    Unmap(
        _In_ UINT Subresource);

    D3D10DDI_HRESOURCE
    GetHandle() const
    {
        return Handle;
    }

    /* Counts GPU writes so readers can tell the driver about read after write hazards */
    VOID
    NoteWrite()
    {
        WriteCount++;
    }

    UINT64
    GetWriteCount() const
    {
        return WriteCount;
    }

    /* The clamp SetResourceMinLOD last set, which GetResourceMinLOD reads back */
    FLOAT MinLod = 0.0f;

    /* Guard state set through the composition partner interface */
    RxGuard Guard = RxGuard::None;
    RECT GuardRect = {};

private:
    HRESULT
    Create(
        _In_ const D3D11DDIARG_CREATERESOURCE& Args);

    RxDevice& Device;
    RxRuntimeResource* Runtime;
    PVOID Memory = nullptr;
    D3D10DDI_HRESOURCE Handle = {};
    BOOLEAN IsCreated = FALSE;
    UINT64 WriteCount = 0;
};

/* Finds the lifetime of an API resource created by this runtime */
_Ret_maybenull_
RxChildLifetime*
RxGetLifetime(
    _In_opt_ ID3D11Resource* Resource);

/* Finds the DdiResource behind an API resource created by this runtime */
_Ret_maybenull_
DdiResource*
RxGetDdiResource(
    _In_opt_ ID3D11Resource* Resource);

/* Description of an API resource when it is a 2D texture; E_INVALIDARG for anything else */
HRESULT
RxGetTexture2DDesc(
    _In_ ID3D11Resource* Resource,
    _Out_ D3D11_TEXTURE2D_DESC* Desc);

/* Hands out the DXGI interfaces a resource answers; TRUE when riid was one of them */
BOOLEAN
RxQueryDxgiSurface(
    _In_ RxDxgiSurface& Surface,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject);

class RxBuffer final : public RxDeviceChild<ID3D11Buffer>
{
public:
    static constexpr GUID PrivateId = { 0xc85f0e3a, 0x7d21, 0x4f96, { 0xb2, 0x48, 0x1e, 0x6a, 0xd3, 0x95, 0x0c, 0x7b } };

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ const D3D11_BUFFER_DESC& Desc,
        _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData,
        _Outptr_ RxBuffer** Buffer);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "buffer.inl"

    DdiResource&
    GetDdiResource()
    {
        return Resource;
    }

private:
    explicit RxBuffer(
        _In_ RxDevice& Device)
        : RxDeviceChild(Device),
          Resource(Device),
          DxgiSurface(this, Device)
    {
    }

    DdiResource Resource;
    RxDxgiSurface DxgiSurface;
    D3D11_BUFFER_DESC Desc = {};
    UINT Priority = DXGI_RESOURCE_PRIORITY_NORMAL;
};
