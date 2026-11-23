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

    /* The DdiResource a layer device's driver handle belongs to */
    static
    DdiResource&
    FromLayerHandle(
        _In_ D3D10DDI_HRESOURCE Resource);

    /* Opens a D3D12 resource the layer wrapped under a kernel handle */
    HRESULT
    OpenWrapped(
        _In_ D3DKMT_HANDLE KernelHandle,
        _In_reads_bytes_(Size) const VOID* Data,
        _In_ UINT Size,
        _In_ const D3D11_RESOURCE_FLAGS& Flags);

    /* The flags the layer is told the resource was created with */
    const D3D11_RESOURCE_FLAGS&
    GetApiFlags() const
    {
        return Runtime->ApiFlags;
    }

    /* The layer's own interface on its resource */
    IOn12DdiResource*
    GetLayerResource() const
    {
        return static_cast<IOn12DdiResource*>(Handle.pDrvPrivate);
    }

    /* Who may use a resource of a layer device right now */
    enum class On12Owner : UCHAR
    {
        Direct3D11,
        Released,
        Unwrapped,
    };

    On12Owner LayerOwner = On12Owner::Direct3D11;
    BOOLEAN IsLayerWrapped = FALSE;
    D3D12_RESOURCE_STATES LayerInState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES LayerOutState = D3D12_RESOURCE_STATE_COMMON;

    /* Extra DDI bits cover what only the runtime asks for, such as present targets */
    HRESULT
    CreateTexture(
        _In_ const RxTextureShape& Shape,
        _In_reads_opt_(Shape.MipLevels * Shape.ArraySize) const D3D11_SUBRESOURCE_DATA* InitialData,
        _In_ UINT ExtraDdiBindFlags = 0,
        _In_ UINT ExtraDdiMiscFlags = 0);

    /* Makes the next texture shared, with the record an opener rebuilds it from */
    VOID
    SetSharedRecord(
        _In_ const RxSharedTextureRecord& Record,
        _In_ RxShareKind Kind)
    {
        if (Runtime == nullptr)
            return;

        Runtime->Shared = Record;
        Runtime->SharedRecord = &Runtime->Shared;
        Runtime->SharedRecordSize = sizeof(Runtime->Shared);
        Runtime->ShareKind = Kind;
    }

    RxShareKind
    GetShareKind() const
    {
        return Runtime ? Runtime->ShareKind : RxShareKind::None;
    }

    /* The global share handle of a texture shared or opened without NT security, else 0 */
    D3DKMT_HANDLE
    GetGlobalShare() const
    {
        return Runtime ? Runtime->GlobalShare : 0;
    }

    /* Makes the next texture a primary the display can scan out from */
    VOID
    SetPrimaryDesc(
        _In_ const DXGI_DDI_PRIMARY_DESC& Desc)
    {
        PrimaryDesc = Desc;
        IsPrimary = TRUE;
    }

    BOOLEAN
    GetIsPrimary() const
    {
        return IsPrimary;
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

    /* Created with a keyed mutex, or opened along with the creator's */
    BOOLEAN
    HasKeyedMutex() const
    {
        return Runtime != nullptr && Runtime->KeyedMutex != 0;
    }

    /* IDXGIKeyedMutex::AcquireSync: WAIT_TIMEOUT and WAIT_ABANDONED come back as they are */
    HRESULT
    AcquireSync(
        _In_ UINT64 Key,
        _In_ DWORD Milliseconds);

    HRESULT
    ReleaseSync(
        _In_ UINT64 Key);

    /* Opens a resource another process shared through a global share handle */
    HRESULT
    OpenShared(
        _In_ D3DKMT_HANDLE GlobalShare,
        _Out_opt_ RxSharedTextureRecord* Record = nullptr);

    /* Opens a resource another process shared through an NT handle */
    HRESULT
    OpenSharedNt(
        _In_ HANDLE NtHandle,
        _Out_opt_ RxSharedTextureRecord* Record);

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
    BOOLEAN
    AllocateDriverMemory(
        _In_ SIZE_T Size);

    HRESULT
    Create(
        _In_ const D3D11DDIARG_CREATERESOURCE& Args);

    /* Hands a kernel resource this device just opened to the UMD */
    HRESULT
    OpenDriverResource(
        _In_ const D3D10DDIARG_OPENRESOURCE& Args);

    RxDevice& Device;
    RxRuntimeResource* Runtime;
    PVOID Memory = nullptr;
    D3D10DDI_HRESOURCE Handle = {};
    BOOLEAN IsCreated = FALSE;
    UINT64 WriteCount = 0;
    DXGI_DDI_PRIMARY_DESC PrimaryDesc = {};
    BOOLEAN IsPrimary = FALSE;

    /* This device holds the keyed mutex; the fence value is what the GPU waited for or signals next */
    BOOLEAN IsSyncAcquired = FALSE;
    UINT64 SyncFenceValue = 0;

    /* The DDI map entries the usage and binding of the resource call for */
    enum class MapRoute : UCHAR
    {
        Default,
        Staging,
        DynamicInputAssembler,
        DynamicConstant,
        DynamicOther,
    };

    MapRoute Route = MapRoute::Default;

    /* A dynamic map that went through the default entry is undone through it too */
    BOOLEAN IsDefaultMapped = FALSE;
};

/* Creates the keyed mutex and fence of a texture whose record asks for one, before its allocations */
NTSTATUS
RxCreateKeyedMutex(
    _In_ RxDevice& Device,
    _Inout_ RxRuntimeResource& Resource);

/* Closes whatever keyed mutex and fence a resource created or opened */
VOID
RxCloseKeyedMutex(
    _Inout_ RxRuntimeResource& Resource);

/* A layer device checks a texture's format with the driver twice, uncached, before the driver makes it */
VOID
RxCheckLayerTextureFormat(
    _In_ RxDevice& Device,
    _In_ DXGI_FORMAT Format);

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

    /* Wraps a D3D12 buffer the D3D11 on 12 layer opened under a kernel handle */
    static
    HRESULT
    Wrap(
        _In_ RxDevice& Device,
        _In_ const D3D11_BUFFER_DESC& Desc,
        _In_ D3DKMT_HANDLE KernelHandle,
        _In_reads_bytes_(Size) const VOID* Data,
        _In_ UINT Size,
        _In_ const D3D11_RESOURCE_FLAGS& Flags,
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
