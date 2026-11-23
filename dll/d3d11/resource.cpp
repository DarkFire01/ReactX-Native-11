/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Resources: the UMD side of a resource and the API objects built on it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <initguid.h>

#include "resource.hpp"

#include <algorithm>
#include <vector>
#include <malloc.h>

UINT
RxDdiBindFlags(
    _In_ UINT BindFlags)
{
    UINT Ddi = 0;

    /* Everything but unordered access sits at the same bit in both encodings */
    Ddi |= BindFlags & (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER | D3D11_BIND_CONSTANT_BUFFER |
                        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_DECODER | D3D11_BIND_VIDEO_ENCODER);
    if (BindFlags & D3D11_BIND_UNORDERED_ACCESS)
        Ddi |= D3D11_DDI_BIND_UNORDERED_ACCESS;

    return Ddi;
}

UINT
RxDdiCpuAccessFlags(
    _In_ UINT CpuAccessFlags)
{
    UINT Ddi = 0;

    if (CpuAccessFlags & D3D11_CPU_ACCESS_WRITE)
        Ddi |= D3D10_DDI_CPU_ACCESS_WRITE;
    if (CpuAccessFlags & D3D11_CPU_ACCESS_READ)
        Ddi |= D3D10_DDI_CPU_ACCESS_READ;

    return Ddi;
}

/* The low misc bits mean the same in both encodings; cube maps travel as a dimension instead */
UINT
RxDdiMiscFlags(
    _In_ UINT MiscFlags)
{
    UINT Ddi = MiscFlags & (D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_SHARED |
                            D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS |
                            D3D11_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_RESOURCE_MISC_RESOURCE_CLAMP);

    /* The driver only knows one kind of sharing */
    if (MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)
        Ddi |= D3D10_DDI_RESOURCE_MISC_SHARED;

    return Ddi;
}

HRESULT
RxApiResult(
    _In_ HRESULT DriverResult)
{
    switch (DriverResult)
    {
        case DXGI_DDI_ERR_WASSTILLDRAWING:
            return DXGI_ERROR_WAS_STILL_DRAWING;
        case DXGI_DDI_ERR_UNSUPPORTED:
            return DXGI_ERROR_UNSUPPORTED;
        default:
            return DriverResult;
    }
}

VOID
RxCheckLayerTextureFormat(
    _In_ RxDevice& Device,
    _In_ DXGI_FORMAT Format)
{
    UINT Caps;
    UINT Pass;

    for (Pass = 0; Pass < 2; Pass++)
    {
        Caps = 0;
        Device.GetDeviceFuncs().pfnCheckFormatSupport(Device.GetDriverDevice(), Format, &Caps);
    }
}

/* The layer hands its resource handle back to the runtime, so a layer device puts its owner in front of it */
static constexpr SIZE_T RxLayerOwnerSize = 16;

BOOLEAN
DdiResource::AllocateDriverMemory(
    _In_ SIZE_T Size)
{
    SIZE_T Header = Device.GetAdapter().IsLayer() ? RxLayerOwnerSize : 0;

    Memory = _aligned_malloc(Header + Size, 16);
    if (Memory == nullptr)
        return FALSE;

    ZeroMemory(Memory, Header + Size);
    if (Header != 0)
        *static_cast<DdiResource**>(Memory) = this;

    Handle.pDrvPrivate = static_cast<BYTE*>(Memory) + Header;
    return TRUE;
}

DdiResource&
DdiResource::FromLayerHandle(
    _In_ D3D10DDI_HRESOURCE Resource)
{
    return **reinterpret_cast<DdiResource**>(static_cast<BYTE*>(Resource.pDrvPrivate) - RxLayerOwnerSize);
}

HRESULT
DdiResource::OpenWrapped(
    _In_ D3DKMT_HANDLE KernelHandle,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ UINT Size,
    _In_ const D3D11_RESOURCE_FLAGS& Flags)
{
    D3DDDI_OPENALLOCATIONINFO2 Allocation = {};
    D3D10DDIARG_OPENRESOURCE Args = {};

    if (Runtime == nullptr)
        return E_OUTOFMEMORY;

    Runtime->ApiFlags = Flags;

    Args.NumAllocations = 1;
    Args.pOpenAllocationInfo2 = &Allocation;
    Args.hKMResource.handle = KernelHandle;
    Args.pPrivateDriverData = const_cast<VOID*>(Data);
    Args.PrivateDriverDataSize = Size;
    return OpenDriverResource(Args);
}

DdiResource::~DdiResource()
{
    D3DKMT_DESTROYALLOCATION Destroy = {};

    if (IsCreated)
        Device.GetDeviceFuncs().pfnDestroyResource(Device.GetDriverDevice(), Handle);

    if (Memory != nullptr)
        _aligned_free(Memory);

    if (Runtime == nullptr)
        return;

    /* The driver deallocates what it opened, maybe later on a thread of its own; one it never took is ours to close */
    if (Runtime->IsOpened && !IsCreated && Runtime->KmResource != 0)
    {
        Destroy.hDevice = Device.GetKmDevice();
        Destroy.hResource = Runtime->KmResource;
        RxGetKmtThunks()->DestroyAllocation(&Destroy);
        Runtime->KmResource = 0;
    }

    /* The driver may still deallocate later, so the device keeps the record until then */
    Device.RetireRuntimeResource(Runtime);
}

HRESULT
DdiResource::Create(
    _In_ const D3D11DDIARG_CREATERESOURCE& Args)
{
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = Device.GetDeviceFuncs();
    D3D10DDI_HRTRESOURCE RuntimeHandle;
    SIZE_T Size;
    HRESULT hr;

    if (Runtime == nullptr)
        return E_OUTOFMEMORY;

    Size = Funcs.pfnCalcPrivateResourceSize(Device.GetDriverDevice(), &Args);
    if (!AllocateDriverMemory(Size))
        return E_OUTOFMEMORY;
    RuntimeHandle.handle = Runtime;
    Funcs.pfnCreateResource(Device.GetDriverDevice(), &Args, Handle, RuntimeHandle);

    hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateResource failed 0x%08lX: dimension %u format %u %ux%u mips %u array %u samples %u usage %u bind %X cpu %X misc %X",
                 hr, Args.ResourceDimension, Args.Format, Args.pMipInfoList ? Args.pMipInfoList[0].TexelWidth : 0,
                 Args.pMipInfoList ? Args.pMipInfoList[0].TexelHeight : 0, Args.MipLevels, Args.ArraySize,
                 Args.SampleDesc.Count, Args.Usage, Args.BindFlags, Args.MapFlags, Args.MiscFlags);
        return hr;
    }

    if (Args.Usage == D3D10_DDI_USAGE_STAGING)
        Route = MapRoute::Staging;
    else if (Args.Usage != D3D10_DDI_USAGE_DYNAMIC)
        Route = MapRoute::Default;
    else if (Args.BindFlags & (D3D10_DDI_BIND_VERTEX_BUFFER | D3D10_DDI_BIND_INDEX_BUFFER))
        Route = MapRoute::DynamicInputAssembler;
    else if (Args.BindFlags & D3D10_DDI_BIND_CONSTANT_BUFFER)
        Route = MapRoute::DynamicConstant;
    else
        Route = MapRoute::DynamicOther;

    IsCreated = TRUE;
    return S_OK;
}

HRESULT
DdiResource::CreateTexture(
    _In_ const RxTextureShape& Shape,
    _In_reads_opt_(Shape.MipLevels * Shape.ArraySize) const D3D11_SUBRESOURCE_DATA* InitialData,
    _In_ UINT ExtraDdiBindFlags,
    _In_ UINT ExtraDdiMiscFlags)
{
    D3D11DDIARG_CREATERESOURCE Args = {};
    D3D10DDI_MIPINFO Mips[D3D11_REQ_MIP_LEVELS];
    UINT Level;

    if (Shape.MipLevels == 0 || Shape.MipLevels > D3D11_REQ_MIP_LEVELS)
        return E_INVALIDARG;

    for (Level = 0; Level < Shape.MipLevels; Level++)
    {
        Mips[Level].TexelWidth = std::max(Shape.Width >> Level, 1u);
        Mips[Level].TexelHeight = std::max(Shape.Height >> Level, 1u);
        Mips[Level].TexelDepth = std::max(Shape.Depth >> Level, 1u);
        Mips[Level].PhysicalWidth = Mips[Level].TexelWidth;
        Mips[Level].PhysicalHeight = Mips[Level].TexelHeight;
        Mips[Level].PhysicalDepth = Mips[Level].TexelDepth;
    }

    /* D3D11_SUBRESOURCE_DATA and the DDI's upload description share one layout */
    static_assert(sizeof(D3D11_SUBRESOURCE_DATA) == sizeof(D3D10_DDIARG_SUBRESOURCE_UP));

    Args.pMipInfoList = Mips;
    Args.pInitialDataUP = reinterpret_cast<const D3D10_DDIARG_SUBRESOURCE_UP*>(InitialData);
    Args.ResourceDimension = Shape.Dimension;
    Args.Usage = Shape.Usage;
    Args.BindFlags = RxDdiBindFlags(Shape.BindFlags) | ExtraDdiBindFlags;
    Args.MapFlags = RxDdiCpuAccessFlags(Shape.CPUAccessFlags);
    Args.MiscFlags = RxDdiMiscFlags(Shape.MiscFlags) | ExtraDdiMiscFlags;
    Args.Format = Shape.Format;
    Args.SampleDesc = Shape.SampleDesc;
    Args.MipLevels = Shape.MipLevels;
    Args.ArraySize = Shape.ArraySize;
    Args.DecoderBufferType = D3D11_1DDI_VIDEO_DECODER_BUFFER_UNKNOWN;
    Args.TextureLayout = D3DWDDM2_0DDI_TL_UNDEFINED;
    if (IsPrimary)
        Args.pPrimaryDesc = &PrimaryDesc;

    if (Runtime != nullptr)
        Runtime->ApiFlags = { Shape.BindFlags, Shape.MiscFlags, Shape.CPUAccessFlags, 0 };

    if (Device.GetLayerDevice() != nullptr)
        RxCheckLayerTextureFormat(Device, Shape.Format);

    return Create(Args);
}

NTSTATUS
RxCreateKeyedMutex(
    _In_ RxDevice& Device,
    _Inout_ RxRuntimeResource& Resource)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CREATEKEYEDMUTEX2 Mutex = {};
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Sync = {};
    D3DKMT_DESTROYKEYEDMUTEX DestroyMutex = {};
    BOOLEAN IsNtShared = Resource.ShareKind != RxShareKind::GlobalHandle;
    UINT PrivateData = 0;
    NTSTATUS Status;

    if (Kmt->CreateKeyedMutex2 == nullptr || Kmt->CreateSynchronizationObject2 == nullptr)
        return STATUS_NOT_SUPPORTED;

    /* The first acquirer asks for key 0 */
    Mutex.InitialValue = 0;
    Mutex.pPrivateRuntimeData = &PrivateData;
    Mutex.PrivateRuntimeDataSize = sizeof(PrivateData);
    Mutex.Flags.NtSecuritySharing = IsNtShared;
    Status = Kmt->CreateKeyedMutex2(&Mutex);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateKeyedMutex2 failed 0x%08lX", Status);
        return Status;
    }

    Sync.hDevice = Device.GetKmDevice();
    Sync.Info.Type = D3DDDI_FENCE;
    Sync.Info.Flags.Shared = TRUE;
    Sync.Info.Flags.NtSecuritySharing = IsNtShared;
    Status = Kmt->CreateSynchronizationObject2(&Sync);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateSynchronizationObject2 for a keyed mutex failed 0x%08lX", Status);
        DestroyMutex.hKeyedMutex = Mutex.hKeyedMutex;
        Kmt->DestroyKeyedMutex(&DestroyMutex);
        return Status;
    }

    /* Openers find both through the record; NT handle sharing has no global handles to write */
    Resource.KeyedMutex = Mutex.hKeyedMutex;
    Resource.SyncObject = Sync.hSyncObject;
    Resource.Shared.Constants[RxSharedRecordKeyedMutexShareIndex] = Mutex.hSharedHandle;
    Resource.Shared.Constants[RxSharedRecordSyncObjectShareIndex] = Sync.Info.SharedHandle;
    return STATUS_SUCCESS;
}

VOID
RxCloseKeyedMutex(
    _Inout_ RxRuntimeResource& Resource)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync = {};
    D3DKMT_DESTROYKEYEDMUTEX DestroyMutex = {};

    if (Resource.SyncObject != 0)
    {
        DestroySync.hSyncObject = Resource.SyncObject;
        Kmt->DestroySynchronizationObject(&DestroySync);
        Resource.SyncObject = 0;
    }

    if (Resource.KeyedMutex != 0 && Kmt->DestroyKeyedMutex != nullptr)
    {
        DestroyMutex.hKeyedMutex = Resource.KeyedMutex;
        Kmt->DestroyKeyedMutex(&DestroyMutex);
    }
    Resource.KeyedMutex = 0;
}

/* Opens the creator's keyed mutex and fence by the global handles in its record; without both the texture has none */
static
VOID
RxOpenKeyedMutex(
    _Inout_ RxRuntimeResource& Resource,
    _In_ const RxSharedTextureRecord& Record)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_OPENKEYEDMUTEX2 Mutex = {};
    D3DKMT_OPENSYNCHRONIZATIONOBJECT Sync = {};
    D3DKMT_DESTROYKEYEDMUTEX DestroyMutex = {};
    UINT PrivateData = 0;

    if (Record.Constants[RxSharedRecordKeyedMutexIndex] == 0 || Kmt->OpenKeyedMutex2 == nullptr ||
        Kmt->OpenSynchronizationObject == nullptr)
    {
        return;
    }

    Mutex.hSharedHandle = Record.Constants[RxSharedRecordKeyedMutexShareIndex];
    Mutex.pPrivateRuntimeData = &PrivateData;
    Mutex.PrivateRuntimeDataSize = sizeof(PrivateData);
    if (!NT_SUCCESS(Kmt->OpenKeyedMutex2(&Mutex)))
        return;

    Sync.hSharedHandle = Record.Constants[RxSharedRecordSyncObjectShareIndex];
    if (!NT_SUCCESS(Kmt->OpenSynchronizationObject(&Sync)))
    {
        DestroyMutex.hKeyedMutex = Mutex.hKeyedMutex;
        Kmt->DestroyKeyedMutex(&DestroyMutex);
        return;
    }

    Resource.KeyedMutex = Mutex.hKeyedMutex;
    Resource.SyncObject = Sync.hSyncObject;
}

/* Copies what the creator stored with a shared resource; whatever it did not write stays zero */
static
VOID
RxCopySharedRecord(
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ UINT Size,
    _Out_opt_ RxSharedTextureRecord* Record)
{
    if (Record == nullptr)
        return;

    ZeroMemory(Record, sizeof(*Record));
    memcpy(Record, Data, std::min<SIZE_T>(Size, sizeof(*Record)));
}

HRESULT
DdiResource::OpenShared(
    _In_ D3DKMT_HANDLE GlobalShare,
    _Out_opt_ RxSharedTextureRecord* Record)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_QUERYRESOURCEINFO Query = {};
    D3DKMT_OPENRESOURCE Open = {};
    D3D10DDIARG_OPENRESOURCE Args = {};
    RxSharedTextureRecord Opened;
    std::vector<D3DDDI_OPENALLOCATIONINFO> Allocations;
    std::vector<BYTE> RuntimeData;
    std::vector<BYTE> ResourceData;
    std::vector<BYTE> AllocationData;
    NTSTATUS Status;

    if (Runtime == nullptr)
        return E_OUTOFMEMORY;
    if (Kmt->QueryResourceInfo == nullptr || Kmt->OpenResource == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    /* The first query only reports how big everything is */
    Query.hDevice = Device.GetKmDevice();
    Query.hGlobalShare = GlobalShare;
    Status = Kmt->QueryResourceInfo(&Query);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    Allocations.resize(Query.NumAllocations);
    RuntimeData.resize(std::max(Query.PrivateRuntimeDataSize, 1u));
    ResourceData.resize(std::max(Query.ResourcePrivateDriverDataSize, 1u));
    AllocationData.resize(std::max(Query.TotalPrivateDriverDataSize, 1u));

    Open.hDevice = Device.GetKmDevice();
    Open.hGlobalShare = GlobalShare;
    Open.NumAllocations = Query.NumAllocations;
    Open.pOpenAllocationInfo = Allocations.data();
    Open.pPrivateRuntimeData = RuntimeData.data();
    Open.PrivateRuntimeDataSize = Query.PrivateRuntimeDataSize;
    Open.pResourcePrivateDriverData = ResourceData.data();
    Open.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Open.pTotalPrivateDriverDataBuffer = AllocationData.data();
    Open.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Status = Kmt->OpenResource(&Open);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    Runtime->KmResource = Open.hResource;
    Runtime->IsOpened = TRUE;
    Runtime->ShareKind = RxShareKind::GlobalHandle;
    Runtime->GlobalShare = GlobalShare;
    RxCopySharedRecord(RuntimeData.data(), Query.PrivateRuntimeDataSize, &Opened);
    RxOpenKeyedMutex(*Runtime, Opened);
    if (Record != nullptr)
        *Record = Opened;

    Args.NumAllocations = Open.NumAllocations;
    Args.pOpenAllocationInfo = Allocations.data();
    Args.hKMResource.handle = Open.hResource;
    Args.pPrivateDriverData = ResourceData.data();
    Args.PrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    return OpenDriverResource(Args);
}

HRESULT
DdiResource::OpenSharedNt(
    _In_ HANDLE NtHandle,
    _Out_opt_ RxSharedTextureRecord* Record)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE Query = {};
    D3DKMT_OPENRESOURCEFROMNTHANDLE Open = {};
    D3D10DDIARG_OPENRESOURCE Args = {};
    UINT MutexPrivateData = 0;
    std::vector<D3DDDI_OPENALLOCATIONINFO2> Opened;
    std::vector<D3DDDI_OPENALLOCATIONINFO> Allocations;
    std::vector<BYTE> RuntimeData;
    std::vector<BYTE> ResourceData;
    std::vector<BYTE> AllocationData;
    NTSTATUS Status;
    UINT Index;

    if (Runtime == nullptr)
        return E_OUTOFMEMORY;
    if (Kmt->QueryResourceInfoFromNtHandle == nullptr || Kmt->OpenResourceFromNtHandle == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Query.hDevice = Device.GetKmDevice();
    Query.hNtHandle = NtHandle;
    Status = Kmt->QueryResourceInfoFromNtHandle(&Query);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    Opened.resize(Query.NumAllocations);
    RuntimeData.resize(std::max(Query.PrivateRuntimeDataSize, 1u));
    ResourceData.resize(std::max(Query.ResourcePrivateDriverDataSize, 1u));
    AllocationData.resize(std::max(Query.TotalPrivateDriverDataSize, 1u));

    Open.hDevice = Device.GetKmDevice();
    Open.hNtHandle = NtHandle;
    Open.NumAllocations = Query.NumAllocations;
    Open.pOpenAllocationInfo2 = Opened.data();
    Open.pPrivateRuntimeData = RuntimeData.data();
    Open.PrivateRuntimeDataSize = Query.PrivateRuntimeDataSize;
    Open.pResourcePrivateDriverData = ResourceData.data();
    Open.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Open.pTotalPrivateDriverDataBuffer = AllocationData.data();
    Open.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Open.pKeyedMutexPrivateRuntimeData = &MutexPrivateData;
    Open.KeyedMutexPrivateRuntimeDataSize = sizeof(MutexPrivateData);
    Status = Kmt->OpenResourceFromNtHandle(&Open);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    /* A keyed mutex and its fence travel in the same NT handle as the resource */
    Runtime->KmResource = Open.hResource;
    Runtime->IsOpened = TRUE;
    Runtime->ShareKind = RxShareKind::NtHandle;
    Runtime->KeyedMutex = Open.hKeyedMutex;
    Runtime->SyncObject = Open.hSyncObject;
    RxCopySharedRecord(RuntimeData.data(), Query.PrivateRuntimeDataSize, Record);

    /* Before WDDM 2.0 the UMD takes the older allocation list, whose fields lead the newer one */
    Args.NumAllocations = Open.NumAllocations;
    if (Device.GetAdapter().GetRevision()->Interface >= D3DWDDM2_0_DDI_INTERFACE_VERSION)
    {
        Args.pOpenAllocationInfo2 = Opened.data();
    }
    else
    {
        Allocations.resize(Open.NumAllocations);
        for (Index = 0; Index < Open.NumAllocations; Index++)
        {
            Allocations[Index].hAllocation = Opened[Index].hAllocation;
            Allocations[Index].pPrivateDriverData = Opened[Index].pPrivateDriverData;
            Allocations[Index].PrivateDriverDataSize = Opened[Index].PrivateDriverDataSize;
        }
        Args.pOpenAllocationInfo = Allocations.data();
    }

    Args.hKMResource.handle = Open.hResource;
    Args.pPrivateDriverData = ResourceData.data();
    Args.PrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    return OpenDriverResource(Args);
}

HRESULT
DdiResource::OpenDriverResource(
    _In_ const D3D10DDIARG_OPENRESOURCE& Args)
{
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = Device.GetDeviceFuncs();
    D3D10DDI_HRTRESOURCE RuntimeHandle;
    SIZE_T Size;
    HRESULT hr;

    /* The runtime data belongs to whoever created the surface; the UMD only needs its own */
    Size = Funcs.pfnCalcPrivateOpenedResourceSize(Device.GetDriverDevice(), &Args);
    if (!AllocateDriverMemory(Size ? Size : 1))
        return E_OUTOFMEMORY;

    RuntimeHandle.handle = Runtime;
    Funcs.pfnOpenResource(Device.GetDriverDevice(), &Args, Handle, RuntimeHandle);

    hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD OpenResource failed 0x%08lX", hr);
        return hr;
    }

    IsCreated = TRUE;
    return S_OK;
}

HRESULT
DdiResource::CreateBuffer(
    _In_ const D3D11_BUFFER_DESC& Desc,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData)
{
    D3D11DDIARG_CREATERESOURCE Args = {};
    D3D10DDI_MIPINFO Mip = {};

    Mip.TexelWidth = Desc.ByteWidth;
    Mip.TexelHeight = 1;
    Mip.TexelDepth = 1;
    Mip.PhysicalWidth = Desc.ByteWidth;
    Mip.PhysicalHeight = 1;
    Mip.PhysicalDepth = 1;

    Args.pMipInfoList = &Mip;
    Args.pInitialDataUP = reinterpret_cast<const D3D10_DDIARG_SUBRESOURCE_UP*>(InitialData);
    Args.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
    Args.Usage = Desc.Usage;
    Args.BindFlags = RxDdiBindFlags(Desc.BindFlags);
    Args.MapFlags = RxDdiCpuAccessFlags(Desc.CPUAccessFlags);
    Args.MiscFlags = RxDdiMiscFlags(Desc.MiscFlags);
    Args.Format = DXGI_FORMAT_UNKNOWN;
    Args.SampleDesc.Count = 1;
    Args.MipLevels = 1;
    Args.ArraySize = 1;
    Args.ByteStride = (Desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) ? Desc.StructureByteStride : 0;
    Args.DecoderBufferType = D3D11_1DDI_VIDEO_DECODER_BUFFER_UNKNOWN;
    Args.TextureLayout = D3DWDDM2_0DDI_TL_UNDEFINED;

    if (Runtime != nullptr)
        Runtime->ApiFlags = { Desc.BindFlags, Desc.MiscFlags, Desc.CPUAccessFlags, Desc.StructureByteStride };

    return Create(Args);
}

HRESULT
DdiResource::CreateDecoderBuffer(
    _In_ const D3D11_1DDI_VIDEO_DECODER_BUFFER_INFO& Info)
{
    D3D11DDIARG_CREATERESOURCE Args = {};
    D3D10DDI_MIPINFO Mip = {};

    Mip.TexelWidth = Info.Size;
    Mip.TexelHeight = 1;
    Mip.TexelDepth = 1;
    Mip.PhysicalWidth = Info.Size;
    Mip.PhysicalHeight = 1;
    Mip.PhysicalDepth = 1;

    /* Dynamic buffers only need writing; others are read back by some drivers too */
    Args.pMipInfoList = &Mip;
    Args.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
    Args.Usage = Info.Usage;
    Args.BindFlags = D3D11_DDI_BIND_DECODER;
    Args.MapFlags = (Info.Usage == D3D10_DDI_USAGE_DYNAMIC) ? D3D10_DDI_CPU_ACCESS_WRITE
                                                            : (D3D10_DDI_CPU_ACCESS_WRITE | D3D10_DDI_CPU_ACCESS_READ);
    Args.Format = DXGI_FORMAT_UNKNOWN;
    Args.SampleDesc.Count = 1;
    Args.MipLevels = 1;
    Args.ArraySize = 1;
    Args.DecoderBufferType = Info.Type;
    Args.TextureLayout = D3DWDDM2_0DDI_TL_UNDEFINED;

    return Create(Args);
}

HRESULT
DdiResource::Map(
    _In_ UINT Subresource,
    _In_ D3D11_MAP MapType,
    _In_ UINT MapFlags,
    _Out_ D3D11_MAPPED_SUBRESOURCE* Mapped)
{
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = Device.GetDeviceFuncs();
    D3D10DDI_MAPPED_SUBRESOURCE Result = {};
    BOOLEAN IsDiscard = (MapType == D3D11_MAP_WRITE_DISCARD);
    decltype(Funcs.pfnResourceMap) Entry = Funcs.pfnResourceMap;
    HRESULT hr;

    switch (Route)
    {
        case MapRoute::Staging:
            Entry = Funcs.pfnStagingResourceMap;
            break;

        case MapRoute::DynamicInputAssembler:
            Entry = IsDiscard ? Funcs.pfnDynamicIABufferMapDiscard : Funcs.pfnDynamicIABufferMapNoOverwrite;
            break;

        case MapRoute::DynamicConstant:
            Entry = IsDiscard ? Funcs.pfnDynamicConstantBufferMapDiscard : Funcs.pfnDynamicConstantBufferMapNoOverwrite;
            break;

        case MapRoute::DynamicOther:
            if (IsDiscard)
                Entry = Funcs.pfnDynamicResourceMapDiscard;
            break;

        default:
            break;
    }

    /* D3D11_MAP and the DO_NOT_WAIT flag share their values with the DDI */
    Entry(Device.GetDriverDevice(), Handle, Subresource, (D3D10_DDI_MAP)MapType, MapFlags, &Result);
    IsDefaultMapped = (Route == MapRoute::DynamicOther && !IsDiscard);
    hr = RxApiResult(Device.TakeDriverError());
    if (FAILED(hr))
    {
        Mapped->pData = nullptr;
        return hr;
    }

    Mapped->pData = Result.pData;
    Mapped->RowPitch = Result.RowPitch;
    Mapped->DepthPitch = Result.DepthPitch;
    return S_OK;
}

VOID
DdiResource::Unmap(
    _In_ UINT Subresource)
{
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = Device.GetDeviceFuncs();
    decltype(Funcs.pfnResourceUnmap) Entry = Funcs.pfnResourceUnmap;

    if (!IsDefaultMapped)
    {
        switch (Route)
        {
            case MapRoute::Staging:
                Entry = Funcs.pfnStagingResourceUnmap;
                break;

            case MapRoute::DynamicInputAssembler:
                Entry = Funcs.pfnDynamicIABufferUnmap;
                break;

            case MapRoute::DynamicConstant:
                Entry = Funcs.pfnDynamicConstantBufferUnmap;
                break;

            case MapRoute::DynamicOther:
                Entry = Funcs.pfnDynamicResourceUnmap;
                break;

            default:
                break;
        }
    }

    Entry(Device.GetDriverDevice(), Handle, Subresource);
}

HRESULT
DdiResource::AcquireSync(
    _In_ UINT64 Key,
    _In_ DWORD Milliseconds)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_ACQUIREKEYEDMUTEX2 Acquire = {};
    LARGE_INTEGER Timeout;
    UINT PrivateData = 0;
    NTSTATUS Status;
    HRESULT hr;

    if (!HasKeyedMutex() || IsSyncAcquired || Kmt->AcquireKeyedMutex2 == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    /* Work recorded before the acquire goes out first, so it does not end up behind the GPU wait */
    Device.FlushDriver(0);

    Acquire.hKeyedMutex = Runtime->KeyedMutex;
    Acquire.Key = Key;
    if (Milliseconds != INFINITE)
    {
        Timeout.QuadPart = -10000LL * Milliseconds;
        Acquire.pTimeout = &Timeout;
    }
    Acquire.pPrivateRuntimeData = &PrivateData;
    Acquire.PrivateRuntimeDataSize = sizeof(PrivateData);
    Status = Kmt->AcquireKeyedMutex2(&Acquire);

    /* A timeout or an abandoned mutex is reported as it is, without taking ownership */
    if (Status != STATUS_SUCCESS)
        return RxHresultFromStatus(Status);

    IsSyncAcquired = TRUE;
    SyncFenceValue = Acquire.FenceValue;

    /* Our GPU work starts after the last owner's has finished; a lost device leaves the contents abandoned */
    if (SyncFenceValue != 0 && Runtime->SyncObject != 0)
    {
        hr = Device.WaitForFenceOnGpu(Runtime->SyncObject, SyncFenceValue);
        if (hr == D3DDDIERR_DEVICEREMOVED)
            return (HRESULT)WAIT_ABANDONED;
        if (FAILED(hr))
            return hr;
    }

    return S_OK;
}

HRESULT
DdiResource::ReleaseSync(
    _In_ UINT64 Key)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    const DXGI1_6_1_DDI_BASE_FUNCTIONS& DxgiFuncs = Device.GetDxgiFuncs();
    DXGI_DDI_ARG_RESOLVESHAREDRESOURCE Resolve;
    D3DKMT_RELEASEKEYEDMUTEX2 Release = {};
    UINT PrivateData = 0;
    NTSTATUS Status;
    HRESULT hr;

    if (!HasKeyedMutex() || !IsSyncAcquired || Kmt->ReleaseKeyedMutex2 == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    /* The driver finishes anything it keeps private to this device, such as compression, first */
    if (DxgiFuncs.pfnResolveSharedResource != nullptr)
    {
        Resolve.hDevice = (DXGI_DDI_HDEVICE)Device.GetDriverDevice().pDrvPrivate;
        Resolve.hResource = (DXGI_DDI_HRESOURCE)Handle.pDrvPrivate;
        hr = DxgiFuncs.pfnResolveSharedResource(&Resolve);
        if (FAILED(hr))
            RX_TRACE(L"UMD ResolveSharedResource failed 0x%08lX", hr);
    }

    /* The next owner waits for the fence value signaled after all of our work */
    Device.FlushDriver(0);
    Release.FenceValue = 0;
    if (Runtime->SyncObject != 0)
    {
        hr = Device.SignalFenceOnGpu(Runtime->SyncObject, SyncFenceValue + 1);
        if (FAILED(hr))
            return hr;
        if (hr == S_OK)
        {
            SyncFenceValue++;
            Release.FenceValue = SyncFenceValue;
        }
    }

    Release.hKeyedMutex = Runtime->KeyedMutex;
    Release.Key = Key;
    Release.pPrivateRuntimeData = &PrivateData;
    Release.PrivateRuntimeDataSize = sizeof(PrivateData);
    Status = Kmt->ReleaseKeyedMutex2(&Release);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTReleaseKeyedMutex2 failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    IsSyncAcquired = FALSE;
    return S_OK;
}

_Ret_maybenull_
DdiResource*
RxGetDdiResource(
    _In_opt_ ID3D11Resource* Resource)
{
    DdiResource* Found = nullptr;

    /* The private id hands back a raw pointer and takes no reference */
    if (Resource == nullptr || FAILED(Resource->QueryInterface(IID_RxDdiResource, reinterpret_cast<void**>(&Found))))
        return nullptr;

    return Found;
}

_Ret_maybenull_
RxChildLifetime*
RxGetLifetime(
    _In_opt_ ID3D11Resource* Resource)
{
    RxChildLifetime* Found = nullptr;

    if (Resource == nullptr || FAILED(Resource->QueryInterface(IID_RxChildLifetime, reinterpret_cast<void**>(&Found))))
        return nullptr;

    return Found;
}

HRESULT
RxGetTexture2DDesc(
    _In_ ID3D11Resource* Resource,
    _Out_ D3D11_TEXTURE2D_DESC* Desc)
{
    ID3D11Texture2D* Texture;

    if (Resource == nullptr || FAILED(Resource->QueryInterface(IID_PPV_ARGS(&Texture))))
        return E_INVALIDARG;

    Texture->GetDesc(Desc);
    Texture->Release();
    return S_OK;
}

HRESULT
RxBuffer::Create(
    _In_ RxDevice& Device,
    _In_ const D3D11_BUFFER_DESC& Desc,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData,
    _Outptr_ RxBuffer** Buffer)
{
    RxBuffer* NewBuffer;
    HRESULT hr;

    *Buffer = nullptr;

    NewBuffer = new (std::nothrow) RxBuffer(Device);
    if (NewBuffer == nullptr)
        return E_OUTOFMEMORY;

    NewBuffer->Desc = Desc;
    hr = NewBuffer->Resource.CreateBuffer(Desc, InitialData);
    if (FAILED(hr))
    {
        NewBuffer->Release();
        return hr;
    }

    *Buffer = NewBuffer;
    return S_OK;
}

HRESULT
RxBuffer::Wrap(
    _In_ RxDevice& Device,
    _In_ const D3D11_BUFFER_DESC& Desc,
    _In_ D3DKMT_HANDLE KernelHandle,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ UINT Size,
    _In_ const D3D11_RESOURCE_FLAGS& Flags,
    _Outptr_ RxBuffer** Buffer)
{
    RxBuffer* NewBuffer;
    HRESULT hr;

    *Buffer = nullptr;

    NewBuffer = new (std::nothrow) RxBuffer(Device);
    if (NewBuffer == nullptr)
        return E_OUTOFMEMORY;

    NewBuffer->Desc = Desc;
    hr = NewBuffer->Resource.OpenWrapped(KernelHandle, Data, Size, Flags);
    if (FAILED(hr))
    {
        NewBuffer->Release();
        return hr;
    }

    *Buffer = NewBuffer;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxBuffer::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == IID_RxDdiResource)
    {
        *ppvObject = &Resource;
        return S_OK;
    }

    if (riid == IID_RxChildLifetime)
    {
        *ppvObject = static_cast<RxChildLifetime*>(this);
        return S_OK;
    }

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11Resource) || riid == __uuidof(ID3D11Buffer))
    {
        *ppvObject = static_cast<ID3D11Buffer*>(this);
        AddRef();
        return S_OK;
    }

    if (RxQueryDxgiSurface(DxgiSurface, riid, ppvObject))
        return S_OK;

    return RxQueryD3D10Face(*this, this, this->Device, riid, ppvObject);
}

VOID
STDMETHODCALLTYPE
RxBuffer::GetType(
    _Out_ D3D11_RESOURCE_DIMENSION* pResourceDimension)
{
    *pResourceDimension = D3D11_RESOURCE_DIMENSION_BUFFER;
}

VOID
STDMETHODCALLTYPE
RxBuffer::SetEvictionPriority(
    _In_ UINT EvictionPriority)
{
    Priority = EvictionPriority;
}

UINT
STDMETHODCALLTYPE
RxBuffer::GetEvictionPriority(VOID)
{
    return Priority;
}

VOID
STDMETHODCALLTYPE
RxBuffer::GetDesc(
    _Out_ D3D11_BUFFER_DESC* pDesc)
{
    *pDesc = Desc;
}

BOOLEAN
RxQueryDxgiSurface(
    _In_ RxDxgiSurface& Surface,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGIResource) ||
        riid == __uuidof(IDXGIResource1))
    {
        *ppvObject = static_cast<IDXGIResource1*>(&Surface);
        Surface.AddRef();
        return TRUE;
    }

    if ((riid == __uuidof(IDXGISurface) || riid == __uuidof(IDXGISurface1) || riid == __uuidof(IDXGISurface2)) &&
        Surface.IsSurface())
    {
        *ppvObject = static_cast<IDXGISurface2*>(&Surface);
        Surface.AddRef();
        return TRUE;
    }

    if (riid == __uuidof(IDXGIKeyedMutex) && Surface.HasKeyedMutex())
    {
        *ppvObject = static_cast<IDXGIKeyedMutex*>(&Surface);
        Surface.AddRef();
        return TRUE;
    }

    return FALSE;
}
