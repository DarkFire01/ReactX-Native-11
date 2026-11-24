/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The kernel device, the user mode driver device and the resources a device asks the driver for
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <vector>

/* What the kernel keeps with a shared resource for the runtimes that open it */
struct D9ShareRecord
{
    UINT Size;
    UINT Version;
    UINT Width;
    UINT Height;
    UINT DxgiFormat;
    UINT ArraySize;
    UINT64 ReadOnly;
    UINT Reserved[2];
    UINT NtShared;
    UINT Primary;
    UINT CrossAdapter;
    UINT Reserved2;

    /* Filled only by Direct3D 9, which marks it with version 1 */
    UINT Format;
    UINT Type;
    DWORD Usage;
    UINT Reserved3;
    UINT Shape[4];
};

static_assert(sizeof(D9ShareRecord) == 88);

/* The record version Direct3D 9 writes; others come from the DXGI runtimes */
static constexpr UINT D9ShareRecordVersion = 1;

/* A record shorter than this cannot describe a resource */
static constexpr UINT D9ShareRecordMinimum = 0x38;

/* Usage bits a record may carry that an opener need not ask for */
static constexpr DWORD D9ShareUsageIgnored = D3DUSAGE_RESTRICTED_CONTENT | D3DUSAGE_RESTRICT_SHARED_RESOURCE |
                                             D3DUSAGE_RESTRICT_SHARED_RESOURCE_DRIVER;

/* Usage bits that keep a resource of a DXGI runtime from opening as a Direct3D 9 one */
static constexpr DWORD D9ShareUsageForeign = 0x003F8DEA;

/* The DXGI format a shared resource records for a Direct3D 9 format, zero when there is none */
static
UINT
D9DxgiShareFormat(
    _In_ D3DDDIFORMAT Format)
{
    static const struct
    {
        UINT Nine;
        UINT Dxgi;
    } Map[] =
    {
        { D3DDDIFMT_A8R8G8B8, 87 }, { D3DDDIFMT_X8R8G8B8, 88 }, { D3DDDIFMT_R5G6B5, 85 },
        { D3DDDIFMT_A1R5G5B5, 86 }, { D3DDDIFMT_A8, 65 }, { D3DDDIFMT_A2B10G10R10, 24 },
        { D3DDDIFMT_A8B8G8R8, 28 }, { D3DDDIFMT_G16R16, 35 }, { D3DDDIFMT_A16B16G16R16, 11 },
        { D3DDDIFMT_L8, 61 }, { D3DDDIFMT_V8U8, 49 }, { D3DDDIFMT_D16_LOCKABLE, 55 }, { D3DDDIFMT_D24S8, 45 },
        { D3DDDIFMT_D16, 55 }, { D3DDDIFMT_L16, 56 }, { D3DDDIFMT_D32F_LOCKABLE, 40 },
        { D3DDDIFMT_INDEX16, 57 }, { D3DDDIFMT_INDEX32, 42 }, { D3DDDIFMT_Q16W16V16U16, 13 },
        { D3DDDIFMT_R16F, 54 }, { D3DDDIFMT_G16R16F, 34 }, { D3DDDIFMT_A16B16G16R16F, 10 },
        { D3DDDIFMT_R32F, 41 }, { D3DDDIFMT_G32R32F, 16 }, { D3DDDIFMT_A32B32G32R32F, 2 }, { 119, 89 },
        { D3DDDIFMT_DXT1, 71 }, { D3DDDIFMT_DXT3, 74 }, { D3DDDIFMT_DXT5, 77 }, { D3DDDIFMT_R8G8_B8G8, 68 },
        { D3DDDIFMT_G8R8_G8B8, 69 },
    };

    for (const auto& Entry : Map)
    {
        if (Entry.Nine == static_cast<UINT>(Format))
            return Entry.Dxgi;
    }
    return 0;
}

/* The Direct3D 9 format a DXGI runtime's shared resource opens as */
static
UINT
D9FormatOfDxgi(
    _In_ UINT Dxgi)
{
    switch (Dxgi)
    {
    case 10:
        return D3DDDIFMT_A16B16G16R16F;
    case 24:
        return D3DDDIFMT_A2B10G10R10;
    case 28:
    case 29:
        return D3DDDIFMT_A8B8G8R8;
    case 87:
        return D3DDDIFMT_A8R8G8B8;
    case 88:
        return D3DDDIFMT_X8R8G8B8;
    default:
        return 0;
    }
}

/* The record a resource made to share leaves with the kernel */
static
VOID
D9FillShareRecord(
    _In_ const D9Resource* Resource,
    _Out_ D9ShareRecord* Record)
{
    BOOLEAN Single = (Resource->Type == D3DRTYPE_SURFACE && Resource->MultisampleType == D3DDDIMULTISAMPLE_NONE &&
                      Resource->MultisampleQuality == 0) ||
                     (Resource->Type == D3DRTYPE_TEXTURE && Resource->LevelCount == 1);

    ZeroMemory(Record, sizeof(*Record));
    Record->Size = sizeof(*Record);
    Record->Version = D9ShareRecordVersion;
    Record->Width = Resource->Width;
    Record->Height = Resource->Height;
    Record->DxgiFormat = Single ? D9DxgiShareFormat(Resource->Format) : 0;
    Record->ArraySize = 1;
    Record->NtShared = Resource->Composed;
    Record->Primary = Resource->Flags.Primary || Resource->Composed;
    Record->Format = Resource->Format;
    Record->Type = Resource->Type;
    Record->Usage = Resource->ShareUsage;
    switch (Resource->Type)
    {
    case D3DRTYPE_SURFACE:
        Record->Shape[0] = Resource->MultisampleType;
        Record->Shape[1] = Resource->MultisampleQuality;
        Record->Shape[2] = Resource->Width;
        Record->Shape[3] = Resource->Height;
        break;
    case D3DRTYPE_VOLUME:
        Record->Shape[0] = Resource->Width;
        Record->Shape[1] = Resource->Height;
        Record->Shape[3] = Resource->Depth;
        break;
    case D3DRTYPE_TEXTURE:
    case D3DRTYPE_CUBETEXTURE:
        Record->Shape[0] = Resource->Width;
        Record->Shape[1] = Resource->Height;
        Record->Shape[2] = Resource->LevelCount;
        break;
    case D3DRTYPE_VOLUMETEXTURE:
        Record->Shape[0] = Resource->Width;
        Record->Shape[1] = Resource->Height;
        Record->Shape[2] = Resource->LevelCount;
        Record->Shape[3] = Resource->Depth;
        break;
    case D3DRTYPE_VERTEXBUFFER:
        Record->Shape[0] = Resource->Width;
        Record->Shape[1] = Resource->Fvf;
        break;
    case D3DRTYPE_INDEXBUFFER:
        Record->Shape[0] = Resource->Width;
        break;
    default:
        break;
    }
}

/* Whether the record of a resource another Direct3D 9 runtime made describes what this one asks for */
static
BOOLEAN
D9ShareRecordMatches(
    _In_ const D9ShareRecord* Record,
    _In_ const D9Resource* Wanted)
{
    DWORD Usage = Record->Usage & ~D9ShareUsageIgnored;
    UINT Width = Wanted->Width;
    UINT Height = Wanted->Height;
    UINT Depth = Wanted->Depth;
    UINT Levels = Wanted->LevelCount;
    UINT Fvf = Wanted->Fvf;
    UINT Samples = Wanted->MultisampleType;
    UINT Quality = Wanted->MultisampleQuality;

    if ((Wanted->ShareUsage & D3DUSAGE_NONSECURE) == 0)
        Usage &= ~D3DUSAGE_NONSECURE;

    /* Each type records its own part of the description; the rest stays as asked */
    switch (Record->Type)
    {
    case D3DRTYPE_SURFACE:
        Samples = Record->Shape[0];
        Quality = Record->Shape[1];
        Width = Record->Shape[2];
        Height = Record->Shape[3];
        break;
    case D3DRTYPE_VOLUME:
        Width = Record->Shape[0];
        Height = Record->Shape[1];
        Depth = Record->Shape[3];
        break;
    case D3DRTYPE_TEXTURE:
    case D3DRTYPE_CUBETEXTURE:
        Width = Record->Shape[0];
        Height = Record->Shape[1];
        Levels = Record->Shape[2];
        break;
    case D3DRTYPE_VOLUMETEXTURE:
        Width = Record->Shape[0];
        Height = Record->Shape[1];
        Levels = Record->Shape[2];
        Depth = Record->Shape[3];
        break;
    case D3DRTYPE_VERTEXBUFFER:
        Width = Record->Shape[0];
        Fvf = Record->Shape[1];
        break;
    case D3DRTYPE_INDEXBUFFER:
        Width = Record->Shape[0];
        break;
    default:
        break;
    }

    return Width == Wanted->Width && Height == Wanted->Height && Record->Format == static_cast<UINT>(Wanted->Format) &&
           Record->Type == static_cast<UINT>(Wanted->Type) && Usage == Wanted->ShareUsage && Depth == Wanted->Depth &&
           Levels == Wanted->LevelCount && Samples == static_cast<UINT>(Wanted->MultisampleType) &&
           Quality == Wanted->MultisampleQuality && Fvf == Wanted->Fvf;
}

/* Whether a resource a DXGI runtime made can open as the single surface or texture level asked for */
static
BOOLEAN
D9DxgiRecordMatches(
    _In_ const D9ShareRecord* Record,
    _In_ const D9Resource* Wanted)
{
    D3DRESOURCETYPE Shape = Wanted->Type == D3DRTYPE_TEXTURE ? D3DRTYPE_TEXTURE : D3DRTYPE_SURFACE;
    UINT Format = D9FormatOfDxgi(Record->DxgiFormat);

    if ((Record->ReadOnly >> 32) != 0 || Record->ArraySize > 1 || Format == 0)
        return FALSE;
    return Record->Width == Wanted->Width && Record->Height == Wanted->Height &&
           static_cast<UINT>(Wanted->Format) == Format &&
           Wanted->Type == Shape && (Shape != D3DRTYPE_TEXTURE || Wanted->MipLevels == 1) && Wanted->Depth == 0 &&
           (Wanted->ShareUsage & D9ShareUsageForeign) == 0 && Wanted->MultisampleType == D3DDDIMULTISAMPLE_NONE &&
           Wanted->MultisampleQuality == 0;
}

/* Direct3D 9 creates its driver device at the DirectX 9 interface */
static constexpr UINT D9DeviceInterface = 9;

/* Driver interfaces from these on must offer the newer entry points the runtime checks for */
static constexpr UINT D9NewEntryPointsVersion = 0x4002;
static constexpr UINT D9MarkerEntryPointsVersion = 0x4001;
static constexpr UINT D9AlwaysCreateResource2Version = 0x4000;
static constexpr UINT D9SyncTokenVersion = 0x6000;
static constexpr UINT D9FlipBatchingUpgradeVersion = 0x6001;

RxKernelDevice&
RxKernelDeviceFromHandle(
    _In_ HANDLE Handle)
{
    return *static_cast<D9Device*>(Handle);
}

D3DKMT_HANDLE
D9Device::KernelDevice() const
{
    return DeviceHandle;
}

D3DKMT_HANDLE
D9Device::KernelAdapter() const
{
    return Adapter->KernelAdapter;
}

D3DKMT_CLIENTHINT
D9Device::KernelClientHint() const
{
    return D3DKMT_CLIENTHINT_DX9;
}

D3DKMT_HANDLE
D9Device::KernelResourceOf(
    _In_opt_ HANDLE RuntimeResource)
{
    if (RuntimeResource == NULL)
        return 0;
    return static_cast<D9Resource*>(RuntimeResource)->KernelResource;
}

VOID
D9Device::RetireKernelResource(
    _In_ HANDLE RuntimeResource)
{
    static_cast<D9Resource*>(RuntimeResource)->KernelResource = 0;
}

VOID
D9Device::NoteKernelContext(
    _In_ D3DKMT_HANDLE Context)
{
    if (KernelContextCount == D9MaxKernelContexts)
    {
        D9_TRACE_ONCE(L"the driver made more than %u contexts; later ones keep the default priority",
                      D9MaxKernelContexts);
        return;
    }
    KernelContexts[KernelContextCount] = Context;
    KernelContextCount += 1;
}

VOID
D9Device::ForgetKernelContext(
    _In_ D3DKMT_HANDLE Context)
{
    for (UINT Index = 0; Index < KernelContextCount; Index++)
    {
        if (KernelContexts[Index] != Context)
            continue;

        MoveMemory(&KernelContexts[Index], &KernelContexts[Index + 1],
                   (KernelContextCount - Index - 1) * sizeof(KernelContexts[0]));
        KernelContextCount -= 1;
        return;
    }
}

HRESULT
D9Device::AllocateCallback(
    _Inout_ D3DDDICB_ALLOCATE* Args)
{
    D9Resource* Resource = static_cast<D9Resource*>(Args->hResource);
    D3DKMT_CREATEALLOCATION Create = {};
    D9ShareRecord Record;
    NTSTATUS Status;

    Create.hDevice = DeviceHandle;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.NumAllocations = Args->NumAllocations;
    Create.pAllocationInfo = Args->pAllocationInfo;

    /* Only a resource made to share leaves a record, and only its first allocation makes the share */
    if (Resource != nullptr)
    {
        BOOLEAN Sharing = Resource->Share == D9ShareMode::Create;

        Create.hResource = Resource->KernelResource;
        Create.Flags.NonSecure = (Resource->ShareUsage & D3DUSAGE_NONSECURE) != 0;
        if (Sharing)
        {
            D9FillShareRecord(Resource, &Record);
            Create.pPrivateRuntimeData = &Record;
            Create.PrivateRuntimeDataSize = sizeof(Record);
            Create.Flags.SwapChainBackBuffer = Resource->Flags.Primary || Resource->Composed;
            Create.Flags.NtSecuritySharing = Resource->Composed;
        }
        if (Resource->KernelResource == 0)
        {
            Create.Flags.CreateResource = TRUE;
            Create.Flags.CreateShared = Sharing;
            Create.Flags.RestrictSharedAccess = Sharing &&
                                                (Resource->ShareUsage & D3DUSAGE_RESTRICT_SHARED_RESOURCE) != 0;
        }
    }

    Status = RxGetKmtThunks()->CreateAllocation(&Create);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTCreateAllocation failed 0x%08lX", Status);
        return D9HresultFromStatus(Status);
    }

    if (Resource != nullptr)
    {
        if (Resource->KernelResource == 0)
        {
            Resource->KernelResource = Create.hResource;
            if (Resource->Share == D9ShareMode::Create)
                Resource->ShareHandle = Create.hGlobalShare;
        }
        Args->hKMResource = Resource->KernelResource;
    }
    return S_OK;
}

HRESULT
D9Device::DeallocateCallback(
    _In_ const D3DDDICB_DEALLOCATE* Args)
{
    D9Resource* Resource = static_cast<D9Resource*>(Args->hResource);
    D3DKMT_DESTROYALLOCATION Destroy = {};
    NTSTATUS Status;

    Destroy.hDevice = DeviceHandle;
    if (Resource != nullptr)
    {
        Destroy.hResource = Resource->KernelResource;
    }
    else
    {
        Destroy.phAllocationList = Args->HandleList;
        Destroy.AllocationCount = Args->NumAllocations;
    }

    Status = RxGetKmtThunks()->DestroyAllocation(&Destroy);
    if (NT_SUCCESS(Status) || Status == STATUS_DEVICE_REMOVED)
    {
        if (Resource != nullptr)
            Resource->KernelResource = 0;
        return S_OK;
    }
    return D9HresultFromStatus(Status);
}

static
HRESULT
APIENTRY
D9AllocateCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_ALLOCATE* Args)
{
    return static_cast<D9Device*>(hDevice)->AllocateCallback(Args);
}

static
HRESULT
APIENTRY
D9DeallocateCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DEALLOCATE* Args)
{
    return static_cast<D9Device*>(hDevice)->DeallocateCallback(Args);
}

static
HRESULT
APIENTRY
D9PresentCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_PRESENT* Args)
{
    return static_cast<D9Device*>(hDevice)->PresentCallback(Args);
}

static
HRESULT
APIENTRY
D9SubmitPresentToHwQueueCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SUBMITPRESENTTOHWQUEUE* Args)
{
    return static_cast<D9Device*>(hDevice)->SubmitPresentCallback(Args);
}

static
HRESULT
APIENTRY
D9SetDisplayModeCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SETDISPLAYMODE* Args)
{
    return static_cast<D9Device*>(hDevice)->SetDisplayModeCallback(Args);
}

/* The runtime hands out no synchronization tokens yet, so every token a driver names is unknown */
/* Grows one of the sync token arrays to hold one more entry; it starts at eight and doubles */
template <class Entry>
static
BOOLEAN
D9GrowSyncArray(
    _Inout_ Entry** Items,
    _In_ UINT Count,
    _Inout_ UINT* Capacity)
{
    UINT Wanted = *Capacity * 2 > 8 ? *Capacity * 2 : 8;
    Entry* Grown;

    if (Count < *Capacity)
        return TRUE;
    Grown = new (std::nothrow) Entry[Wanted];
    if (Grown == nullptr)
        return FALSE;
    if (Count != 0)
        CopyMemory(Grown, *Items, sizeof(**Items) * Count);
    delete[] *Items;
    *Items = Grown;
    *Capacity = Wanted;
    return TRUE;
}

/* Stores a packet and names it by its slot plus one; slots freed by earlier tokens are used first */
HRESULT
D9Device::AcquireSyncToken(
    _In_ const D9SyncPacket* Packet,
    _Out_ UINT64* Token)
{
    UINT Slot;

    AcquireSRWLockExclusive(&SyncLock);
    if (SyncFreeCount != 0)
    {
        SyncFreeCount -= 1;
        Slot = SyncFreeSlots[SyncFreeCount];
    }
    else
    {
        if (!D9GrowSyncArray(&SyncPackets, SyncPacketCount, &SyncPacketCapacity))
        {
            ReleaseSRWLockExclusive(&SyncLock);
            return E_OUTOFMEMORY;
        }
        Slot = SyncPacketCount;
        SyncPacketCount += 1;
    }
    SyncPackets[Slot] = *Packet;
    *Token = static_cast<UINT64>(Slot) + 1;
    ReleaseSRWLockExclusive(&SyncLock);
    return S_OK;
}

/* Reads the packet a token names and frees its slot; a token the runtime never gave out is refused */
HRESULT
D9Device::TakeSyncPacket(
    _In_ UINT64 Token,
    _Out_ D9SyncPacket* Packet)
{
    UINT64 Slot = Token - 1;
    HRESULT hr = S_OK;

    ZeroMemory(Packet, sizeof(*Packet));
    AcquireSRWLockExclusive(&SyncLock);
    if (Slot >= SyncPacketCount)
    {
        hr = E_INVALIDARG;
    }
    else if (!D9GrowSyncArray(&SyncFreeSlots, SyncFreeCount, &SyncFreeCapacity))
    {
        *Packet = SyncPackets[Slot];
        hr = E_OUTOFMEMORY;
    }
    else
    {
        *Packet = SyncPackets[Slot];
        SyncFreeSlots[SyncFreeCount] = static_cast<UINT>(Slot);
        SyncFreeCount += 1;
        SyncPackets[Slot].Kind = D9SyncKind::None;
    }
    ReleaseSRWLockExclusive(&SyncLock);
    return hr;
}

/* Runs the packet behind a token on the contexts the driver names, or on every context the device has */
HRESULT
D9Device::RunSyncToken(
    _In_ const D3DDDICB_SYNCTOKEN* Args,
    _In_ D9SyncKind Expected)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_HANDLE Contexts[D9MaxKernelContexts];
    UINT Count = 0;
    D9SyncPacket Packet;
    HRESULT hr;

    hr = TakeSyncPacket(reinterpret_cast<UINT64>(Args->hSyncToken), &Packet);
    if (FAILED(hr) && hr != E_OUTOFMEMORY)
        return hr;
    if (Packet.Kind != Expected)
    {
        if (Packet.Tracked != nullptr)
            D9ReleaseTrackedFence(Packet.Tracked);
        return E_INVALIDARG;
    }

    if (Args->BroadcastContextArray != nullptr)
    {
        for (UINT Index = 0; Index < Args->BroadcastContextCount && Count < D9MaxKernelContexts; Index++)
        {
            Contexts[Count] = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(
                Args->BroadcastContextArray[Index]));
            Count += 1;
        }
    }
    else
    {
        for (UINT Remaining = KernelContextCount; Remaining > 0; Remaining -= 1)
        {
            Contexts[Count] = KernelContexts[Remaining - 1];
            Count += 1;
        }
    }

    if (Expected == D9SyncKind::Wait)
    {
        for (UINT Index = 0; Index < Count; Index++)
        {
            D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU Wait = {};
            NTSTATUS Status;

            Wait.hContext = Contexts[Index];
            Wait.ObjectCount = 1;
            Wait.ObjectHandleArray = &Packet.Fence;
            Wait.MonitoredFenceValueArray = &Packet.Value;
            Status = Kmt->WaitForSynchronizationObjectFromGpu(&Wait);
            if (!NT_SUCCESS(Status))
            {
                D9Trace(L"waiting for a sync token's fence failed 0x%08lX", Status);
                break;
            }
        }
    }
    else
    {
        D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 Signal = {};
        NTSTATUS Status;

        Signal.ObjectCount = 1;
        Signal.ObjectHandleArray = &Packet.Fence;
        Signal.BroadcastContextCount = Count;
        Signal.BroadcastContextArray = Contexts;
        Signal.MonitoredFenceValueArray = &Packet.Value;
        Status = Kmt->SignalSynchronizationObjectFromGpu2(&Signal);
        if (!NT_SUCCESS(Status))
            D9Trace(L"signaling a sync token's fence failed 0x%08lX", Status);
    }
    if (Packet.Tracked != nullptr)
        D9ReleaseTrackedFence(Packet.Tracked);
    return S_OK;
}

static
HRESULT
APIENTRY
D9AcquireResourceCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SYNCTOKEN* Args)
{
    return static_cast<D9Device*>(hDevice)->RunSyncToken(Args, D9SyncKind::Wait);
}

static
HRESULT
APIENTRY
D9ReleaseResourceCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SYNCTOKEN* Args)
{
    return static_cast<D9Device*>(hDevice)->RunSyncToken(Args, D9SyncKind::Signal);
}

static
HRESULT
APIENTRY
D9EscapeCb(
    _In_ HANDLE hAdapter,
    _Inout_ const D3DDDICB_ESCAPE* Args)
{
    D9Device* Device = static_cast<D9Device*>(Args->hDevice);
    D3DKMT_ESCAPE Escape = {};

    Escape.hAdapter = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(hAdapter));
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.Flags = Args->Flags;
    Escape.pPrivateDriverData = Args->pPrivateDriverData;
    Escape.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    if (Device != nullptr)
    {
        Escape.hDevice = Device->KernelDevice();
        if (Device->DriverInterfaceVersion() > 1)
            Escape.hContext = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(Args->hContext));
    }
    return D9HresultFromStatus(RxGetKmtThunks()->Escape(&Escape));
}

/* Overlays, queued blits and native fences have no Direct3D 9 path to the kernel yet */
static
HRESULT
APIENTRY
D9CreateOverlayCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATEOVERLAY* Args)
{
    D9_TRACE_ONCE(L"the driver asked for an overlay, which is not implemented yet");
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
D9UpdateOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_UPDATEOVERLAY* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
D9FlipOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_FLIPOVERLAY* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
D9DestroyOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYOVERLAY* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
D9OfferAllocations2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_OFFERALLOCATIONS2* Args)
{
    D3DDDICB_OFFERALLOCATIONS Offer = {};

    Offer.pResources = Args->pResources;
    Offer.HandleList = Args->HandleList;
    Offer.NumAllocations = Args->NumAllocations;
    Offer.Priority = Args->Priority;
    return RxOfferAllocationsCb(hDevice, &Offer);
}

static
HRESULT
APIENTRY
D9SubmitPresentBltToHwQueueCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SUBMITPRESENTBLTTOHWQUEUE* Args)
{
    D9_TRACE_ONCE(L"the driver asked to queue a present blit, which is not implemented yet");
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
D9CreateNativeFenceCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DKMT_CREATENATIVEFENCE* Args)
{
    D9_TRACE_ONCE(L"the driver asked for a native fence, which is not implemented yet");
    return E_NOTIMPL;
}

static
VOID
D9FillDeviceCallbacks(
    _Out_ D3DDDI_DEVICECALLBACKS* Callbacks)
{
    ZeroMemory(Callbacks, sizeof(*Callbacks));
    Callbacks->pfnAllocateCb = D9AllocateCb;
    Callbacks->pfnDeallocateCb = D9DeallocateCb;
    Callbacks->pfnSetPriorityCb = RxSetPriorityCb;
    Callbacks->pfnQueryResidencyCb = RxQueryResidencyCb;
    Callbacks->pfnSetDisplayModeCb = D9SetDisplayModeCb;
    Callbacks->pfnPresentCb = D9PresentCb;
    Callbacks->pfnRenderCb = RxRenderCb;
    Callbacks->pfnLockCb = RxLockCb;
    Callbacks->pfnUnlockCb = RxUnlockCb;
    Callbacks->pfnEscapeCb = D9EscapeCb;
    Callbacks->pfnCreateOverlayCb = D9CreateOverlayCb;
    Callbacks->pfnUpdateOverlayCb = D9UpdateOverlayCb;
    Callbacks->pfnFlipOverlayCb = D9FlipOverlayCb;
    Callbacks->pfnDestroyOverlayCb = D9DestroyOverlayCb;
    Callbacks->pfnCreateContextCb = RxCreateContextCb;
    Callbacks->pfnDestroyContextCb = RxDestroyContextCb;
    Callbacks->pfnCreateSynchronizationObjectCb = RxCreateSynchronizationObjectCb;
    Callbacks->pfnDestroySynchronizationObjectCb = RxDestroySynchronizationObjectCb;
    Callbacks->pfnWaitForSynchronizationObjectCb = RxWaitForSynchronizationObjectCb;
    Callbacks->pfnSignalSynchronizationObjectCb = RxSignalSynchronizationObjectCb;
    Callbacks->pfnSetAsyncCallbacksCb = RxSetAsyncCallbacksCb;
    Callbacks->pfnSetDisplayPrivateDriverFormatCb = RxSetDisplayPrivateDriverFormatCb;
    Callbacks->pfnOfferAllocationsCb = RxOfferAllocationsCb;
    Callbacks->pfnReclaimAllocationsCb = RxReclaimAllocationsCb;
    Callbacks->pfnCreateSynchronizationObject2Cb = RxCreateSynchronizationObject2Cb;
    Callbacks->pfnWaitForSynchronizationObject2Cb = RxWaitForSynchronizationObject2Cb;
    Callbacks->pfnSignalSynchronizationObject2Cb = RxSignalSynchronizationObject2Cb;
    Callbacks->pfnMakeResidentCb = RxMakeResidentCb;
    Callbacks->pfnEvictCb = RxEvictCb;
    Callbacks->pfnWaitForSynchronizationObjectFromCpuCb = RxWaitForSynchronizationObjectFromCpuCb;
    Callbacks->pfnSignalSynchronizationObjectFromCpuCb = RxSignalSynchronizationObjectFromCpuCb;
    Callbacks->pfnWaitForSynchronizationObjectFromGpuCb = RxWaitForSynchronizationObjectFromGpuCb;
    Callbacks->pfnSignalSynchronizationObjectFromGpuCb = RxSignalSynchronizationObjectFromGpuCb;
    Callbacks->pfnCreatePagingQueueCb = RxCreatePagingQueueCb;
    Callbacks->pfnDestroyPagingQueueCb = RxDestroyPagingQueueCb;
    Callbacks->pfnLock2Cb = RxLock2Cb;
    Callbacks->pfnUnlock2Cb = RxUnlock2Cb;
    Callbacks->pfnInvalidateCacheCb = RxInvalidateCacheCb;
    Callbacks->pfnReserveGpuVirtualAddressCb = RxReserveGpuVirtualAddressCb;
    Callbacks->pfnMapGpuVirtualAddressCb = RxMapGpuVirtualAddressCb;
    Callbacks->pfnFreeGpuVirtualAddressCb = RxFreeGpuVirtualAddressCb;
    Callbacks->pfnUpdateGpuVirtualAddressCb = RxUpdateGpuVirtualAddressCb;
    Callbacks->pfnCreateContextVirtualCb = RxCreateContextVirtualCb;
    Callbacks->pfnSubmitCommandCb = RxSubmitCommandCb;
    Callbacks->pfnDeallocate2Cb = RxDeallocate2Cb;
    Callbacks->pfnSignalSynchronizationObjectFromGpu2Cb = RxSignalSynchronizationObjectFromGpu2Cb;
    Callbacks->pfnReclaimAllocations2Cb = RxReclaimAllocations2Cb;
    Callbacks->pfnGetResourcePresentPrivateDriverDataCb = RxGetResourcePresentPrivateDriverDataCb;
    Callbacks->pfnUpdateAllocationPropertyCb = RxUpdateAllocationPropertyCb;
    Callbacks->pfnOfferAllocations2Cb = D9OfferAllocations2Cb;
    Callbacks->pfnReclaimAllocations3Cb = RxReclaimAllocations3Cb;
    Callbacks->pfnAcquireResourceCb = D9AcquireResourceCb;
    Callbacks->pfnReleaseResourceCb = D9ReleaseResourceCb;
    Callbacks->pfnCreateHwQueueCb = RxCreateHwQueueCb;
    Callbacks->pfnDestroyHwQueueCb = RxDestroyHwQueueCb;
    Callbacks->pfnSubmitCommandToHwQueueCb = RxSubmitCommandToHwQueueCb;
    Callbacks->pfnSubmitWaitForSyncObjectsToHwQueueCb = RxSubmitWaitForSyncObjectsToHwQueueCb;
    Callbacks->pfnSubmitSignalSyncObjectsToHwQueueCb = RxSubmitSignalSyncObjectsToHwQueueCb;
    Callbacks->pfnSubmitPresentBltToHwQueueCb = D9SubmitPresentBltToHwQueueCb;
    Callbacks->pfnSubmitPresentToHwQueueCb = D9SubmitPresentToHwQueueCb;
    Callbacks->pfnCreateNativeFenceCb = D9CreateNativeFenceCb;
}

/* Threads may call the driver unless vertices go through the runtime or a fullscreen device has no focus */
BOOLEAN
D9DriverMayThread(
    _In_ DWORD BehaviorFlags,
    _In_opt_ HWND FocusWindow,
    _In_ BOOLEAN Windowed)
{
    if ((BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING |
                          D3DCREATE_DISABLE_PSGP_THREADING)) != 0)
    {
        return FALSE;
    }
    return FocusWindow != NULL || Windowed;
}

HRESULT
D9Device::CreateKernelDevice(VOID)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CREATEDEVICE Create = {};
    NTSTATUS Status;

    if (Kmt == nullptr || Kmt->CreateDevice == nullptr)
        return D3DERR_NOTAVAILABLE;

    /* Only the plain object's devices run in the kernel's legacy mode, unless their swap effect was upgraded */
    Create.hAdapter = Adapter->KernelAdapter;
    Create.Flags.LegacyMode = !IsEx && !SwapEffectUpgrade;
    Create.Flags.RequestVSync = (BehaviorFlags & D3DCREATE_ENABLE_PRESENTSTATS) != 0;
    Status = Kmt->CreateDevice(&Create);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTCreateDevice failed 0x%08lX", Status);
        return D9HresultFromStatus(Status);
    }

    DeviceHandle = Create.hDevice;
    return S_OK;
}

/* Each device opens the driver's adapter again for itself, looking the driver up first as the system does */
HRESULT
D9Device::OpenDriverAdapter(VOID)
{
    D3DKMT_UMDFILENAMEINFO Name;
    HRESULT hr;

    if (!D9QueryUmdName(Adapter->KernelAdapter, KMTUMDVERSION_DX9, &Name))
    {
        D9Trace(L"%S lost its Direct3D 9 user mode driver", Adapter->DeviceName);
        return E_FAIL;
    }

    AdapterCallbacks = Adapter->Callbacks;
    hr = D9CallOpenAdapter(Adapter, &AdapterCallbacks, &AdapterFunctions, &DriverAdapter, &RuntimeVersion,
                           &DriverVersion);
    if (FAILED(hr))
        D9Trace(L"OpenAdapter for a new device failed 0x%08lX", hr);
    return hr;
}

/* The kernel facts and capabilities are read again through the device's own driver adapter */
HRESULT
D9Device::ReadDriverAnswers(VOID)
{
    D9Adapter* Answers = D9CopyAdapter(Adapter);
    BOOLEAN Read;

    if (Answers == nullptr)
        return E_OUTOFMEMORY;

    Answers->Functions = AdapterFunctions;
    Answers->DriverAdapter = DriverAdapter;
    Read = D9ReadDriverAnswers(Answers, IsEx);
    D9FreeDriverAnswers(Answers);
    delete Answers;
    return Read ? S_OK : D3DERR_NOTAVAILABLE;
}

/* Interfaces from WDDM 1.2 on drop the old blit, create, flush and present entry points for newer ones */
static
BOOLEAN
D9DriverEntryPointsValid(
    _In_ const D3DDDI_DEVICEFUNCS* Funcs,
    _In_ UINT DriverVersion,
    _In_ BOOLEAN UsesFlush1)
{
    if (DriverVersion < D9NewEntryPointsVersion)
        return TRUE;

    if (Funcs->pfnVolBlt != nullptr || Funcs->pfnVolBlt1 == nullptr)
        return FALSE;
    if (Funcs->pfnBufBlt != nullptr || Funcs->pfnBufBlt1 == nullptr)
        return FALSE;
    if (Funcs->pfnTexBlt != nullptr || Funcs->pfnTexBlt1 == nullptr)
        return FALSE;
    if (Funcs->pfnCreateResource != nullptr || Funcs->pfnCreateResource2 == nullptr)
        return FALSE;

    if (UsesFlush1)
    {
        if (Funcs->pfnFlush != nullptr || Funcs->pfnFlush1 == nullptr)
            return FALSE;
        if (Funcs->pfnPresent != nullptr || Funcs->pfnPresent1 == nullptr)
            return FALSE;
    }
    return TRUE;
}

HRESULT
D9Device::CreateDriverDevice(
    _In_ BOOLEAN Windowed)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    const D3DDDI_DEVICEFUNCS& Funcs = DeviceFunctions;
    D3DDDIARG_CREATEDEVICE Args = {};
    D3DKMT_CREATECONTEXT Context = {};
    BOOLEAN MayThread;
    NTSTATUS Status;
    HRESULT hr;

    /* Drivers from before the WDDM 1.1 interface get their first command buffer from the runtime */
    if (DriverVersion <= 1)
    {
        Context.hDevice = DeviceHandle;
        Context.ClientHint = D3DKMT_CLIENTHINT_DX9;
        Status = Kmt->CreateContext(&Context);
        if (!NT_SUCCESS(Status))
        {
            D9Trace(L"D3DKMTCreateContext failed 0x%08lX", Status);
            return D9HresultFromStatus(Status);
        }
        ContextHandle = Context.hContext;
        Args.pCommandBuffer = Context.pCommandBuffer;
        Args.CommandBufferSize = Context.CommandBufferSize;
        Args.pAllocationList = Context.pAllocationList;
        Args.AllocationListSize = Context.AllocationListSize;
        Args.pPatchLocationList = Context.pPatchLocationList;
        Args.PatchLocationListSize = Context.PatchLocationListSize;
    }

    D9FillDeviceCallbacks(&DeviceCallbacks);
    MayThread = D9DriverMayThread(BehaviorFlags, FocusWindow, Windowed);
    Args.hDevice = this;
    Args.Interface = D9DeviceInterface;
    Args.Version = RuntimeVersion;
    Args.pCallbacks = &DeviceCallbacks;
    Args.pDeviceFuncs = &DeviceFunctions;
    Args.Flags.AllowMultithreading = MayThread;
    Args.Flags.AllowFlipBatching = MayThread && !IsEx;

    hr = AdapterFunctions.pfnCreateDevice(DriverAdapter, &Args);
    if (FAILED(hr))
    {
        D9Trace(L"UMD CreateDevice failed 0x%08lX", hr);
        return hr == D3DERR_DEVICEREMOVED ? E_FAIL : hr;
    }
    DriverDevice = Args.hDevice;

    UsesFlush1 = DriverVersion > D9AlwaysCreateResource2Version && Funcs.pfnFlush1 != nullptr &&
                 Funcs.pfnCheckCounterInfo != nullptr && Funcs.pfnCheckCounter != nullptr &&
                 Funcs.pfnUpdateSubresourceUP != nullptr && Funcs.pfnPresent1 != nullptr &&
                 (DriverVersion <= D9MarkerEntryPointsVersion ||
                  (Funcs.pfnSetMarker != nullptr && Funcs.pfnSetMarkerMode != nullptr));

    if (DriverVersion > D9SyncTokenVersion &&
        (Funcs.pfnAcquireResource == nullptr || Funcs.pfnReleaseResource == nullptr))
    {
        D9Trace(L"the driver lacks the WDDM 2.1 resource synchronization entry points");
        return E_FAIL;
    }
    if (!D9DriverEntryPointsValid(&Funcs, DriverVersion, UsesFlush1))
    {
        D9Trace(L"the driver offers entry points that do not fit interface 0x%X", DriverVersion);
        return E_FAIL;
    }
    return S_OK;
}

VOID
D9Device::DestroyHardware(VOID)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_DESTROYCONTEXT Context = {};
    D3DKMT_DESTROYDEVICE Device = {};

    if (DriverDevice != NULL)
    {
        ReleaseWindowSurface();
        DeviceFunctions.pfnDestroyDevice(DriverDevice);
        DriverDevice = NULL;
    }
    if (DriverAdapter != NULL && AdapterFunctions.pfnCloseAdapter != nullptr)
    {
        AdapterFunctions.pfnCloseAdapter(DriverAdapter);
        DriverAdapter = NULL;
    }
    if (ContextHandle != 0)
    {
        Context.hContext = ContextHandle;
        Kmt->DestroyContext(&Context);
        ContextHandle = 0;
    }
    if (DeviceHandle != 0)
    {
        Device.hDevice = DeviceHandle;
        Kmt->DestroyDevice(&Device);
        DeviceHandle = 0;
    }
}

/* Driver flags for a surface: back buffers and render targets are render targets, depth formats are Z buffers */
static
VOID
D9SurfaceFlags(
    _In_ const D3DSURFACE_DESC* Desc,
    _In_ BOOLEAN Lockable,
    _In_ BOOLEAN Discard,
    _Out_ D3DDDI_RESOURCEFLAGS* Flags)
{
    Flags->Value = 0;
    if ((Desc->Usage & D3DUSAGE_RENDERTARGET) != 0)
        Flags->RenderTarget = TRUE;
    if ((Desc->Usage & D3DUSAGE_DEPTHSTENCIL) != 0)
        Flags->ZBuffer = TRUE;
    if (!Lockable)
        Flags->NotLockable = TRUE;
    if (Discard)
        Flags->DiscardRenderTarget = TRUE;
}

/* Hands the driver one resource and keeps what it returns; textures name their levels and faces */
HRESULT
D9Device::CreateDriverResource(
    _Inout_ D9Resource* Resource,
    _In_ const D3DDDI_SURFACEINFO* Surface,
    _In_ D3DDDIMULTISAMPLE_TYPE MultisampleType,
    _In_ UINT MultisampleQuality)
{
    UINT Count = Resource->SurfaceCount != 0 ? Resource->SurfaceCount : 1;

    D3DDDIARG_CREATERESOURCE2 Create2 = {};
    D3DDDIARG_CREATERESOURCE Create = {};
    HRESULT hr;

    Resource->MultisampleType = MultisampleType;
    Resource->MultisampleQuality = MultisampleQuality;
    if (Resource->Share == D9ShareMode::Open)
        return OpenSharedResource(Resource);

    if (DeviceFunctions.pfnCreateResource2 != nullptr)
    {
        Create2.Format = Resource->Format;
        Create2.Pool = Resource->Pool;
        Create2.Fvf = Resource->Fvf;
        Create2.MultisampleType = MultisampleType;
        Create2.MultisampleQuality = MultisampleQuality;
        Create2.MipLevels = Resource->MipLevels;
        Create2.pSurfList = Surface;
        Create2.SurfCount = Count;
        Create2.VidPnSourceId = Adapter->SourceId;
        Create2.hResource = Resource;
        Create2.Flags = Resource->Flags;
        Create2.Flags2 = Resource->Flags2;
        Create2.Rotation = D3DDDI_ROTATION_IDENTITY;
        hr = DeviceFunctions.pfnCreateResource2(DriverDevice, &Create2);
        Resource->DriverResource = Create2.hResource;
    }
    else
    {
        Create.Format = Resource->Format;
        Create.Pool = Resource->Pool;
        Create.Fvf = Resource->Fvf;
        Create.MultisampleType = MultisampleType;
        Create.MultisampleQuality = MultisampleQuality;
        Create.MipLevels = Resource->MipLevels;
        Create.pSurfList = Surface;
        Create.SurfCount = Count;
        Create.VidPnSourceId = Adapter->SourceId;
        Create.hResource = Resource;
        Create.Flags = Resource->Flags;
        Create.Rotation = D3DDDI_ROTATION_IDENTITY;
        hr = DeviceFunctions.pfnCreateResource(DriverDevice, &Create);
        Resource->DriverResource = Create.hResource;
    }

    if (FAILED(hr))
    {
        D9Trace(L"UMD CreateResource failed 0x%08lX for format %u", hr, Resource->Format);
        Resource->DriverResource = NULL;
    }
    return hr;
}

/* Marks a resource the driver is about to make as shared, or as the one a handle names */
VOID
D9ApplyShare(
    _Inout_ D9Resource* Resource,
    _In_opt_ const D9ShareRequest* Share,
    _In_ D3DRESOURCETYPE Type)
{
    Resource->Type = Type;
    Resource->LevelCount = 1;
    if (Share == nullptr || Share->Mode == D9ShareMode::None)
        return;
    Resource->Share = Share->Mode;
    Resource->ShareHandle = Share->Handle;
    Resource->ShareUsage = Share->Usage;
    Resource->Flags.SharedResource = TRUE;
}

/* Opens a resource another device shared: the kernel's record must describe what was asked for */
HRESULT
D9Device::OpenSharedResource(
    _Inout_ D9Resource* Resource)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_QUERYRESOURCEINFO Query = {};
    D3DKMT_OPENRESOURCE Open = {};
    D3DKMT_DESTROYALLOCATION Destroy = {};
    D3DDDIARG_OPENRESOURCE Args = {};
    std::vector<BYTE> Runtime;
    std::vector<BYTE> Driver;
    std::vector<BYTE> Total;
    std::vector<D3DDDI_OPENALLOCATIONINFO> Infos;
    const D9ShareRecord* Record;
    BOOLEAN Matches;
    NTSTATUS Status;
    HRESULT hr;

    Query.hDevice = DeviceHandle;
    Query.hGlobalShare = Resource->ShareHandle;
    Status = Kmt->QueryResourceInfo(&Query);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTQueryResourceInfo failed 0x%08lX for a shared resource", Status);
        return D9HresultFromStatus(Status);
    }
    if (Query.PrivateRuntimeDataSize < D9ShareRecordMinimum &&
        (Query.PrivateRuntimeDataSize != 0 || !Resource->Flags.Primary))
    {
        D9Trace(L"a shared resource carries %u bytes of runtime data", Query.PrivateRuntimeDataSize);
        return E_FAIL;
    }

    Runtime.resize(Query.PrivateRuntimeDataSize);
    Driver.resize(Query.ResourcePrivateDriverDataSize);
    Total.resize(Query.TotalPrivateDriverDataSize);
    Infos.resize(Query.NumAllocations);

    Open.hDevice = DeviceHandle;
    Open.hGlobalShare = Resource->ShareHandle;
    Open.NumAllocations = Query.NumAllocations;
    Open.pOpenAllocationInfo = Infos.data();
    Open.pPrivateRuntimeData = Runtime.data();
    Open.PrivateRuntimeDataSize = Query.PrivateRuntimeDataSize;
    Open.pResourcePrivateDriverData = Driver.data();
    Open.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Open.pTotalPrivateDriverDataBuffer = Total.data();
    Open.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Status = Kmt->OpenResource(&Open);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTOpenResource failed 0x%08lX for a shared resource", Status);
        return D9HresultFromStatus(Status);
    }

    Destroy.hDevice = DeviceHandle;
    Destroy.hResource = Open.hResource;
    if (Query.PrivateRuntimeDataSize != 0)
    {
        Record = reinterpret_cast<const D9ShareRecord*>(Runtime.data());
        Matches = Record->Width == Resource->Width && Record->Height == Resource->Height &&
                  Resource->Pool != D3DDDIPOOL_SYSTEMMEM;
        if (Matches)
        {
            Matches = Record->Version == D9ShareRecordVersion ? D9ShareRecordMatches(Record, Resource) :
                                                                D9DxgiRecordMatches(Record, Resource);
        }
        if (!Matches)
        {
            D9_TRACE_ONCE(L"a shared resource does not match the description it was opened with");
            Kmt->DestroyAllocation(&Destroy);
            return E_INVALIDARG;
        }
    }

    Args.NumAllocations = Open.NumAllocations;
    Args.pOpenAllocationInfo = Infos.data();
    Args.hKMResource = Open.hResource;
    Args.pPrivateDriverData = Driver.data();
    Args.PrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Args.hResource = Resource;
    Args.Rotation = D3DDDI_ROTATION_IDENTITY;
    DrainBatch();
    hr = DeviceFunctions.pfnOpenResource(DriverDevice, &Args);
    if (FAILED(hr))
    {
        D9Trace(L"UMD OpenResource failed 0x%08lX for a shared resource", hr);
        Kmt->DestroyAllocation(&Destroy);
        return hr;
    }

    Resource->DriverResource = Args.hResource;
    Resource->KernelResource = Open.hResource;
    return S_OK;
}

/* A linear vertex or index buffer of the runtime's own */
HRESULT
D9Device::CreateDriverBuffer(
    _In_ D3DDDIFORMAT Format,
    _In_ UINT Size,
    _In_ UINT Flags,
    _Out_ D9Resource* Resource)
{
    D3DDDI_SURFACEINFO Surface = {};

    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = Format;
    Resource->Pool = D3DDDIPOOL_VIDEOMEMORY;
    Resource->Flags.Value = Flags;
    Resource->Width = Size;
    Resource->Height = 1;

    Surface.Width = Size;
    Surface.Height = 1;
    return CreateDriverResource(Resource, &Surface, D3DDDIMULTISAMPLE_NONE, 0);
}

/* A vertex or index buffer the application made, in video memory or in memory of the runtime's own */
HRESULT
D9Device::CreateBufferResource(
    _In_ D3DFORMAT Format,
    _In_ UINT Length,
    _In_ UINT Flags,
    _In_ DWORD Fvf,
    _In_ D3DDDI_POOL Placement,
    _In_opt_ VOID* Memory,
    _In_opt_ const D9ShareRequest* Share,
    _Out_ D9Resource* Resource)
{
    D3DDDI_SURFACEINFO Surface = {};

    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = static_cast<D3DDDIFORMAT>(Format);
    Resource->Pool = Placement;
    Resource->Flags.Value = Flags;
    Resource->Width = Length;
    Resource->Height = 1;
    Resource->Fvf = Fvf;
    D9ApplyShare(Resource, Share, Format == D3DFMT_VERTEXDATA ? D3DRTYPE_VERTEXBUFFER : D3DRTYPE_INDEXBUFFER);

    Surface.Width = Length;
    Surface.Height = 1;
    Surface.pSysMem = Memory;
    Surface.SysMemPitch = Memory != nullptr ? Length : 0;
    return CreateDriverResource(Resource, &Surface, D3DDDIMULTISAMPLE_NONE, 0);
}

/* System memory surfaces hand the driver the memory the runtime keeps their pixels in */
HRESULT
D9Device::CreateDriverSurface(
    _In_ const D3DSURFACE_DESC* Desc,
    _In_ BOOLEAN Lockable,
    _In_ BOOLEAN Discard,
    _In_ D3DDDI_POOL DriverPool,
    _In_opt_ VOID* Memory,
    _In_ UINT MemoryPitch,
    _In_opt_ const D9ShareRequest* Share,
    _Out_ D9Resource* Resource,
    _In_ D9SurfaceRole Role)
{
    D3DDDI_SURFACEINFO Surface = {};
    D9ShareRequest Composed = {};

    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = static_cast<D3DDDIFORMAT>(D9DriverDepthFormat(Adapter, Desc->Format));
    Resource->Pool = DriverPool;
    Resource->Width = Desc->Width;
    Resource->Height = Desc->Height;
    D9SurfaceFlags(Desc, Lockable, Discard, &Resource->Flags);

    /* Flip model surfaces are single level textures; the buffers the compositor shows are shared by NT handle */
    if (Role == D9SurfaceRole::Plain)
    {
        D9ApplyShare(Resource, Share, D3DRTYPE_SURFACE);
    }
    else
    {
        Resource->Flags.Texture = TRUE;
        Resource->MipLevels = 1;
        if (Role == D9SurfaceRole::CompositionBuffer)
        {
            Composed.Mode = D9ShareMode::Create;
            Composed.Usage = D3DUSAGE_RENDERTARGET | D3DUSAGE_RESTRICT_SHARED_RESOURCE | D3DUSAGE_NONSECURE |
                             D9ShareUsageModern;
            Resource->Flags2.IsDisplayable = TRUE;
            Resource->Composed = TRUE;
            D9ApplyShare(Resource, &Composed, D3DRTYPE_TEXTURE);
        }
        else
        {
            D9ApplyShare(Resource, nullptr, D3DRTYPE_TEXTURE);
        }
    }

    Surface.Width = Desc->Width;
    Surface.Height = Desc->Height;
    Surface.pSysMem = Memory;
    Surface.SysMemPitch = MemoryPitch;
    return CreateDriverResource(Resource, &Surface, static_cast<D3DDDIMULTISAMPLE_TYPE>(Desc->MultiSampleType),
                                Desc->MultiSampleQuality);
}

VOID
D9Device::DestroyDriverSurface(
    _Inout_ D9Resource* Resource)
{
    if (Resource->DriverResource == NULL)
        return;

    /* Only a resource the batch still uses waits for the batch to run; any other waits for the worker alone */
    if (Batch != nullptr)
        Batch->Settle(Resource->DriverResource);
    DeviceFunctions.pfnDestroyResource(DriverDevice, Resource->DriverResource);
    Resource->DriverResource = NULL;
}

/* Copies a call into the batch; FALSE leaves it to the caller to make */
static
BOOLEAN
D9RecordCall(
    _In_opt_ D9Batch* Batch,
    _In_ D9BatchCall Call,
    _In_reads_bytes_(Size) const VOID* Args,
    _In_ UINT Size)
{
    VOID* Slot;

    if (Batch == nullptr)
        return FALSE;
    Slot = Batch->Record(Call, Size);
    if (Slot == nullptr)
        return FALSE;
    CopyMemory(Slot, Args, Size);
    return TRUE;
}

/* The lock flags the driver sees for an application lock */
static
VOID
D9DriverLockFlags(
    _In_ const D9Resource* Resource,
    _In_ DWORD Flags,
    _Out_ D3DDDI_LOCKFLAGS* Driver)
{
    /* A read only lock never discards; system memory locks only tell the driver */
    Driver->Value = 0;
    Driver->ReadOnly = (Flags & D3DLOCK_READONLY) != 0;
    Driver->Discard = !Driver->ReadOnly && (Flags & D3DLOCK_DISCARD) != 0;
    Driver->DoNotWait = (Flags & D3DLOCK_DONOTWAIT) != 0;
    Driver->NoOverwrite = (Flags & D3DLOCK_NOOVERWRITE) != 0;
    Driver->NotifyOnly = Resource->Pool == D3DDDIPOOL_SYSTEMMEM;
}

/* A batching device locks dynamic and system memory resources asynchronously when the lock discards, does not
   overwrite or only tells the driver; a discard renames the resource in the batch */
HRESULT
D9Device::LockAsyncResource(
    _Inout_ D3DDDIARG_LOCK* Request,
    _In_ UINT LockCount)
{
    D3DDDIARG_LOCKASYNC Args = {};
    D3DDDIARG_RENAME Rename = {};
    HRESULT hr;

    if (!Request->Flags.NotifyOnly && LockCount == 0 && Request->Flags.Discard == Request->Flags.NoOverwrite)
        return E_FAIL;

    Args.hResource = Request->hResource;
    Args.SubResourceIndex = Request->SubResourceIndex;
    Args.Flags.NoOverwrite = Request->Flags.NoOverwrite;
    Args.Flags.Discard = Request->Flags.Discard && !Request->Flags.NotifyOnly;
    Args.Flags.NotifyOnly = Request->Flags.NotifyOnly;
    if (Request->Flags.RangeValid)
    {
        Args.Flags.RangeValid = TRUE;
        Args.Range = Request->Range;
    }
    if (Request->Flags.AreaValid)
    {
        Args.Flags.AreaValid = TRUE;
        Args.Area = Request->Area;
    }
    if (Request->Flags.BoxValid)
    {
        Args.Flags.BoxValid = TRUE;
        Args.Box = Request->Box;
    }

    /* Notifying the driver of a resource the batch still uses waits for the batch */
    if (!Request->Flags.NotifyOnly || Request->Flags.NoOverwrite)
    {
        Args.Flags.NoExistingReferences = !Batch->IsReferenced(Request->hResource);
    }
    else
    {
        if (Batch->IsReferenced(Request->hResource))
            Batch->Drain();
        Args.Flags.NoExistingReferences = TRUE;
    }

    hr = DeviceFunctions.pfnLockAsync(DriverDevice, &Args);
    if (FAILED(hr))
        return hr;
    Request->pSurfData = Args.pSurfData;
    Request->Pitch = Args.Pitch;
    Request->SlicePitch = Args.SlicePitch;

    if (Args.Flags.Discard && LockCount == 0)
    {
        Rename.hResource = Request->hResource;
        Rename.SubResourceIndex = Request->SubResourceIndex;
        Rename.hCookie = Args.hCookie;
        if (!D9RecordCall(Batch, D9BatchCall::Rename, &Rename, sizeof(Rename)))
            DeviceFunctions.pfnRename(DriverDevice, &Rename);
        Batch->Reference(Request->hResource);
    }
    return S_OK;
}

/* Dynamic and system memory resources of a batching device may lock without waiting for the batch */
BOOLEAN
D9Device::LocksAsync(
    _In_ const D9Resource* Resource) const
{
    return Batch != nullptr && (Resource->Flags.Dynamic || Resource->Pool == D3DDDIPOOL_SYSTEMMEM);
}

/* Locks a surface or texture resource, asynchronously when it may */
HRESULT
D9Device::LockDriverResource(
    _In_ D9Resource* Resource,
    _Inout_ D3DDDIARG_LOCK* Args,
    _Out_ BOOLEAN* Asynchronous)
{
    HRESULT hr;

    *Asynchronous = FALSE;
    if (LocksAsync(Resource) && SUCCEEDED(LockAsyncResource(Args, 0)))
    {
        *Asynchronous = TRUE;
        return S_OK;
    }

    if (Batch != nullptr)
        Batch->Settle(Args->hResource);
    hr = DeviceFunctions.pfnLock(DriverDevice, Args);
    if (FAILED(hr) && hr != D3DERR_WASSTILLDRAWING)
        D9Trace(L"UMD Lock failed 0x%08lX", hr);
    return hr;
}

/* Unlocks what LockDriverResource locked, the same way it was locked */
HRESULT
D9Device::UnlockDriverResource(
    _In_ D9Resource* Resource,
    _In_ UINT Index,
    _In_ BOOLEAN Asynchronous)
{
    D3DDDIARG_UNLOCKASYNC Quick = {};
    D3DDDIARG_UNLOCK Unlock = {};

    if (Asynchronous)
    {
        Quick.hResource = Resource->DriverResource;
        Quick.SubResourceIndex = Index;
        Quick.Flags.NotifyOnly = Resource->Pool == D3DDDIPOOL_SYSTEMMEM;
        return DeviceFunctions.pfnUnlockAsync(DriverDevice, &Quick);
    }
    Unlock.hResource = Resource->DriverResource;
    Unlock.SubResourceIndex = Index;
    Unlock.Flags.NotifyOnly = Resource->Pool == D3DDDIPOOL_SYSTEMMEM;
    if (Batch != nullptr)
        Batch->Settle(Unlock.hResource);
    return DeviceFunctions.pfnUnlock(DriverDevice, &Unlock);
}

HRESULT
D9Device::LockDriverBuffer(
    _In_ D9Resource* Resource,
    _In_ UINT Offset,
    _In_ UINT Size,
    _In_ BOOLEAN Ranged,
    _In_ DWORD Flags,
    _In_ UINT LockCount,
    _Inout_ BOOLEAN* Asynchronous,
    _Outptr_ VOID** Data)
{
    D3DDDIARG_LOCK Args = {};
    HRESULT hr;

    Args.hResource = Resource->DriverResource;
    D9DriverLockFlags(Resource, Flags, &Args.Flags);
    if (Ranged)
    {
        Args.Flags.RangeValid = TRUE;
        Args.Range.Offset = Offset;
        Args.Range.Size = Size;
    }

    if (*Asynchronous && Batch != nullptr)
    {
        hr = LockAsyncResource(&Args, LockCount);
        if (SUCCEEDED(hr))
        {
            *Data = Args.pSurfData;
            return S_OK;
        }
    }
    *Asynchronous = FALSE;

    if (Batch != nullptr)
        Batch->Settle(Args.hResource);
    hr = DeviceFunctions.pfnLock(DriverDevice, &Args);
    if (FAILED(hr))
    {
        if (hr != D3DERR_WASSTILLDRAWING)
            D9Trace(L"UMD Lock of a buffer failed 0x%08lX", hr);
        return hr;
    }
    *Data = Args.pSurfData;
    return S_OK;
}

HRESULT
D9Device::LockDriverSurface(
    _In_ D9Resource* Resource,
    _In_opt_ const RECT* Rect,
    _In_ DWORD Flags,
    _Out_ D3DLOCKED_RECT* Locked,
    _Out_ BOOLEAN* Asynchronous)
{
    D3DDDIARG_LOCK Args = {};
    HRESULT hr;

    Args.hResource = Resource->DriverResource;
    D9DriverLockFlags(Resource, Flags, &Args.Flags);
    if (Rect != nullptr)
    {
        Args.Flags.AreaValid = TRUE;
        Args.Area = *Rect;
    }

    hr = LockDriverResource(Resource, &Args, Asynchronous);
    if (FAILED(hr))
        return hr;

    Locked->pBits = Args.pSurfData;
    Locked->Pitch = static_cast<INT>(Args.Pitch);
    return S_OK;
}

HRESULT
D9Device::UnlockDriverBuffer(
    _In_ D9Resource* Resource,
    _In_ BOOLEAN Asynchronous)
{
    D3DDDIARG_UNLOCKASYNC Unlock = {};

    if (!Asynchronous)
        return UnlockDriverSurface(Resource, FALSE);

    Unlock.hResource = Resource->DriverResource;
    Unlock.Flags.NotifyOnly = Resource->Pool == D3DDDIPOOL_SYSTEMMEM;
    return DeviceFunctions.pfnUnlockAsync(DriverDevice, &Unlock);
}

HRESULT
D9Device::UnlockDriverSurface(
    _In_ D9Resource* Resource,
    _In_ BOOLEAN Asynchronous)
{
    return UnlockDriverResource(Resource, 0, Asynchronous);
}

HRESULT
D9Device::CreateTextureResource(
    _In_reads_(Count) const D3DDDI_SURFACEINFO* Surfaces,
    _In_ UINT Count,
    _In_ UINT MipLevels,
    _Inout_ D9Resource* Resource)
{
    Resource->SurfaceCount = Count;
    Resource->MipLevels = MipLevels;
    Resource->Width = Surfaces[0].Width;
    Resource->Height = Surfaces[0].Height;
    return CreateDriverResource(Resource, Surfaces, D3DDDIMULTISAMPLE_NONE, 0);
}

/* System memory locks only tell the driver, and always name the first subresource */
HRESULT
D9Device::LockDriverSubresource(
    _In_ D9Resource* Resource,
    _In_ UINT Index,
    _In_opt_ const D3DBOX* Box,
    _In_ BOOLEAN IsVolume,
    _In_ DWORD Flags,
    _Out_ D3DLOCKED_BOX* Locked,
    _Out_ BOOLEAN* Asynchronous)
{
    D3DDDIARG_LOCK Args = {};
    HRESULT hr;

    Args.hResource = Resource->DriverResource;
    D9DriverLockFlags(Resource, Flags, &Args.Flags);
    if (!Args.Flags.NotifyOnly)
    {
        Args.SubResourceIndex = Index;
        if (Box != nullptr && IsVolume)
        {
            Args.Flags.BoxValid = TRUE;
            Args.Box.Left = Box->Left;
            Args.Box.Top = Box->Top;
            Args.Box.Right = Box->Right;
            Args.Box.Bottom = Box->Bottom;
            Args.Box.Front = Box->Front;
            Args.Box.Back = Box->Back;
        }
        else if (Box != nullptr)
        {
            Args.Flags.AreaValid = TRUE;
            Args.Area.left = static_cast<LONG>(Box->Left);
            Args.Area.top = static_cast<LONG>(Box->Top);
            Args.Area.right = static_cast<LONG>(Box->Right);
            Args.Area.bottom = static_cast<LONG>(Box->Bottom);
        }
    }

    hr = LockDriverResource(Resource, &Args, Asynchronous);
    if (FAILED(hr))
        return hr;
    Locked->pBits = Args.pSurfData;
    Locked->RowPitch = static_cast<INT>(Args.Pitch);
    Locked->SlicePitch = static_cast<INT>(Args.SlicePitch);
    return S_OK;
}

HRESULT
D9Device::UnlockDriverSubresource(
    _In_ D9Resource* Resource,
    _In_ UINT Index,
    _In_ BOOLEAN Asynchronous)
{
    return UnlockDriverResource(Resource, Resource->Pool == D3DDDIPOOL_SYSTEMMEM ? 0 : Index, Asynchronous);
}

/* Copies a region of one face's top level, which the driver repeats down the levels; a batch records the copy */
VOID
D9Device::DriverTexBlt(
    _In_ D9Resource* Destination,
    _In_ D9Resource* Source,
    _In_ UINT Face,
    _In_ const D3DBOX* Box)
{
    D3DDDIARG_TEXBLT1 Blt = {};
    HRESULT hr;

    Blt.hDstResource = Destination->DriverResource;
    Blt.hSrcResource = Source->DriverResource;
    Blt.CubeMapFace = Face;
    Blt.DstPoint.x = static_cast<LONG>(Box->Left);
    Blt.DstPoint.y = static_cast<LONG>(Box->Top);
    Blt.SrcRect.left = static_cast<LONG>(Box->Left);
    Blt.SrcRect.top = static_cast<LONG>(Box->Top);
    Blt.SrcRect.right = static_cast<LONG>(Box->Right);
    Blt.SrcRect.bottom = static_cast<LONG>(Box->Bottom);

    if (D9RecordCall(Batch, D9BatchCall::TexBlt, &Blt, sizeof(Blt)))
    {
        Batch->Reference(Blt.hSrcResource);
        Batch->Reference(Blt.hDstResource);
        return;
    }
    hr = D9RunTexBlt(&DeviceFunctions, DriverDevice, &Blt);
    if (FAILED(hr))
        D9Trace(L"UMD TexBlt failed 0x%08lX", hr);
}

VOID
D9Device::DriverVolBlt(
    _In_ D9Resource* Destination,
    _In_ D9Resource* Source,
    _In_ const D3DBOX* Box)
{
    D3DDDIARG_VOLUMEBLT1 Blt = {};
    HRESULT hr;

    Blt.hDstResource = Destination->DriverResource;
    Blt.hSrcResource = Source->DriverResource;
    Blt.DstX = Box->Left;
    Blt.DstY = Box->Top;
    Blt.DstZ = Box->Front;
    Blt.SrcBox.Left = Box->Left;
    Blt.SrcBox.Top = Box->Top;
    Blt.SrcBox.Right = Box->Right;
    Blt.SrcBox.Bottom = Box->Bottom;
    Blt.SrcBox.Front = Box->Front;
    Blt.SrcBox.Back = Box->Back;

    if (D9RecordCall(Batch, D9BatchCall::VolBlt, &Blt, sizeof(Blt)))
    {
        Batch->Reference(Blt.hSrcResource);
        Batch->Reference(Blt.hDstResource);
        return;
    }
    hr = D9RunVolBlt(&DeviceFunctions, DriverDevice, &Blt);
    if (FAILED(hr))
        D9Trace(L"UMD VolBlt failed 0x%08lX", hr);
}

/* Copies a byte range of a managed buffer's system memory copy into its video memory copy */
VOID
D9Device::DriverBufBlt(
    _In_ D9Resource* Destination,
    _In_ D9Resource* Source,
    _In_ UINT Offset,
    _In_ UINT Size)
{
    D3DDDIARG_BUFFERBLT1 Blt = {};
    HRESULT hr;

    Blt.hDstResource = Destination->DriverResource;
    Blt.hSrcResource = Source->DriverResource;
    Blt.Offset = Offset;
    Blt.SrcRange.Offset = Offset;
    Blt.SrcRange.Size = Size;

    if (D9RecordCall(Batch, D9BatchCall::BufBlt, &Blt, sizeof(Blt)))
    {
        Batch->Reference(Blt.hSrcResource);
        Batch->Reference(Blt.hDstResource);
        return;
    }
    hr = D9RunBufBlt(&DeviceFunctions, DriverDevice, &Blt);
    if (FAILED(hr))
        D9Trace(L"UMD BufBlt failed 0x%08lX", hr);
}

VOID
D9Device::DriverGenerateMips(
    _In_ D9Resource* Resource,
    _In_ D3DTEXTUREFILTERTYPE Filter)
{
    D3DDDIARG_GENERATEMIPSUBLEVELS Args = {};

    Args.hResource = Resource->DriverResource;
    Args.Filter = static_cast<D3DDDITEXTUREFILTERTYPE>(Filter);
    if (D9RecordCall(Batch, D9BatchCall::GenerateMips, &Args, sizeof(Args)))
    {
        Batch->Reference(Args.hResource);
        return;
    }
    DeviceFunctions.pfnGenerateMipSubLevels(DriverDevice, &Args);
}

/* The managed pool may be spelled the extended object's way */
static
BOOLEAN
D9IsManagedPool(
    _In_ D3DPOOL Pool)
{
    return Pool == D3DPOOL_MANAGED || Pool == static_cast<D3DPOOL>(6);
}

DWORD
D9Device::ChangePriority(
    _In_opt_ D9Resource* Resource,
    _In_ D3DPOOL Pool,
    _Inout_ DWORD* Stored,
    _In_ DWORD Priority)
{
    DWORD Previous = *Stored;

    if (D9IsManagedPool(Pool))
    {
        *Stored = Priority;
        return Previous;
    }
    if (!IsEx || Pool != D3DPOOL_DEFAULT)
        return 0;

    *Stored = Priority;
    if (Resource != nullptr && Resource->DriverResource != NULL)
        DriverSetPriority(Resource, Priority);
    return Previous;
}

DWORD
D9Device::ReadPriority(
    _In_ D3DPOOL Pool,
    _In_ DWORD Stored) const
{
    if (D9IsManagedPool(Pool) || (IsEx && Pool == D3DPOOL_DEFAULT))
        return Stored;
    return 0;
}

VOID
D9Device::DriverSetPriority(
    _In_ D9Resource* Resource,
    _In_ DWORD Priority)
{
    D3DDDIARG_SETPRIORITY Args = {};

    Args.hResource = Resource->DriverResource;
    Args.Priority = Priority;
    DrainBatch();
    DeviceFunctions.pfnSetPriority(DriverDevice, &Args);
}

/* A runtime owned system memory buffer the driver reads from */
HRESULT
D9Device::CreateSystemBuffer(
    _In_ D3DDDIFORMAT Format,
    _In_ UINT Size,
    _In_ UINT Flags,
    _In_ VOID* Memory,
    _Out_ D9Resource* Resource)
{
    D3DDDI_SURFACEINFO Surface = {};

    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = Format;
    Resource->Pool = D3DDDIPOOL_SYSTEMMEM;
    Resource->Flags.Value = Flags;
    Resource->Width = Size;
    Resource->Height = 1;

    Surface.Width = Size;
    Surface.Height = 1;
    Surface.pSysMem = Memory;
    Surface.SysMemPitch = Size;
    return CreateDriverResource(Resource, &Surface, D3DDDIMULTISAMPLE_NONE, 0);
}

/* Whether the machine has more than one processor core */
static
BOOLEAN
D9HasSeveralCores(VOID)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* Entries;
    DWORD Size = 0;
    UINT Cores = 0;

    if (GetLogicalProcessorInformation(nullptr, &Size) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;

    Entries = static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(HeapAlloc(GetProcessHeap(), 0, Size));
    if (Entries == nullptr)
        return FALSE;
    if (GetLogicalProcessorInformation(Entries, &Size))
    {
        for (DWORD Index = 0; Index < Size / sizeof(*Entries) && Cores < 2; Index++)
        {
            if (Entries[Index].Relationship == RelationProcessorCore)
                Cores += 1;
        }
    }
    HeapFree(GetProcessHeap(), 0, Entries);
    return Cores > 1;
}

/* Drivers that offer asynchronous locks on a hardware device leave threading to the runtime */
BOOLEAN
D9Device::WantsBatch(VOID) const
{
    static LONG SeveralCores = -1;

    if (DeviceType != D3DDEVTYPE_HAL || (BehaviorFlags & D3DCREATE_DISABLE_PSGP_THREADING) != 0)
        return FALSE;
    if (DeviceFunctions.pfnLockAsync == nullptr || DeviceFunctions.pfnUnlockAsync == nullptr ||
        DeviceFunctions.pfnRename == nullptr)
    {
        return FALSE;
    }
    if (SeveralCores < 0)
        InterlockedExchange(&SeveralCores, D9HasSeveralCores() ? 1 : 0);
    return SeveralCores == 1;
}

VOID
D9Device::StartBatch(VOID)
{
    HRESULT hr;

    if (Batch != nullptr || !WantsBatch())
        return;

    Batch = PausedBatch;
    PausedBatch = nullptr;
    if (Batch == nullptr)
        Batch = new (std::nothrow) D9Batch(this, &DeviceFunctions, DriverDevice);
    if (Batch == nullptr)
        return;
    hr = Batch->Start();
    if (FAILED(hr))
    {
        D9Trace(L"driver calls run on the calling thread; the batch could not start (0x%08lX)", hr);
        StopBatch();
    }
}

VOID
D9Device::StopBatch(VOID)
{
    D9Batch* Stopping = Batch;

    if (Stopping == nullptr)
        return;
    Batch = nullptr;
    Stopping->Stop();
    delete Stopping;
}

/* Stops the batch but keeps it, with the buffer it records into, for the next start */
VOID
D9Device::PauseBatch(VOID)
{
    D9Batch* Stopping = Batch;

    if (Stopping == nullptr)
        return;
    Batch = nullptr;
    Stopping->Stop();
    delete PausedBatch;
    PausedBatch = Stopping;
}

VOID
D9Device::DrainBatch(VOID)
{
    if (Batch != nullptr)
        Batch->Drain();
}

VOID
D9Device::DdiRenderState(
    _In_ const D3DDDIARG_RENDERSTATE* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::RenderState, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetRenderState(DriverDevice, Args);
}

VOID
D9Device::DdiTextureStageState(
    _In_ const D3DDDIARG_TEXTURESTAGESTATE* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::TextureStageState, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetTextureStageState(DriverDevice, Args);
}

VOID
D9Device::DdiPixelShaderConst(
    _In_ const D3DDDIARG_SETPIXELSHADERCONST* Args,
    _In_reads_(Args->Count * 4) const FLOAT* Data)
{
    UINT DataSize = Args->Count * 4 * sizeof(*Data);
    BYTE* Slot = Batch != nullptr ? static_cast<BYTE*>(Batch->Record(D9BatchCall::PixelShaderConst,
                                                                      sizeof(*Args) + DataSize))
                                  : nullptr;

    if (Slot == nullptr)
    {
        DeviceFunctions.pfnSetPixelShaderConst(DriverDevice, Args, Data);
        return;
    }
    CopyMemory(Slot, Args, sizeof(*Args));
    CopyMemory(Slot + sizeof(*Args), Data, DataSize);
}

/* Vertex constants of every kind, integer and boolean pixel constants and the vertex shader; a recorded call
   hands the driver the batch's copy of the data */
VOID
D9Device::DdiShaderConstants(
    _In_ D9BatchCall Call,
    _In_ UINT Register,
    _In_ UINT Count,
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ UINT Size)
{
    D3DDDIARG_SETVERTEXSHADERCONST Range = {};
    UINT* Slot = nullptr;

    Range.Register = Register;
    Range.Count = Count;
    if (Batch != nullptr)
        Slot = static_cast<UINT*>(Batch->Record(Call, sizeof(Range) + Size));
    if (Slot != nullptr)
    {
        Slot[0] = Register;
        Slot[1] = Count;
        if (Data != nullptr)
            CopyMemory(Slot + 2, Data, Size);
        return;
    }

    switch (Call)
    {
    case D9BatchCall::VertexShaderFunc:
        DeviceFunctions.pfnSetVertexShaderFunc(DriverDevice, *static_cast<const HANDLE*>(Data));
        break;
    case D9BatchCall::VertexShaderConst:
        DeviceFunctions.pfnSetVertexShaderConst(DriverDevice, &Range, Data);
        break;
    case D9BatchCall::VertexShaderConstI:
        DeviceFunctions.pfnSetVertexShaderConstI(DriverDevice,
                                                 reinterpret_cast<D3DDDIARG_SETVERTEXSHADERCONSTI*>(&Range),
                                                 static_cast<const INT*>(Data));
        break;
    case D9BatchCall::VertexShaderConstB:
        DeviceFunctions.pfnSetVertexShaderConstB(DriverDevice,
                                                 reinterpret_cast<D3DDDIARG_SETVERTEXSHADERCONSTB*>(&Range),
                                                 static_cast<const BOOL*>(Data));
        break;
    case D9BatchCall::PixelShaderConstI:
        DeviceFunctions.pfnSetPixelShaderConstI(DriverDevice,
                                                reinterpret_cast<D3DDDIARG_SETPIXELSHADERCONSTI*>(&Range),
                                                static_cast<const INT*>(Data));
        break;
    case D9BatchCall::PixelShaderConstB:
        DeviceFunctions.pfnSetPixelShaderConstB(DriverDevice,
                                                reinterpret_cast<D3DDDIARG_SETPIXELSHADERCONSTB*>(&Range),
                                                static_cast<const BOOL*>(Data));
        break;
    default:
        break;
    }
}

VOID
D9Device::DdiTexture(
    _In_ UINT Stage,
    _In_opt_ HANDLE Resource)
{
    D9TextureRecord Record = { Stage, Resource };

    if (Batch != nullptr)
        Batch->NoteBound(D9BoundTextures + (Stage < 256 ? Stage : Stage - 240), Resource);
    if (!D9RecordCall(Batch, D9BatchCall::Texture, &Record, sizeof(Record)))
        DeviceFunctions.pfnSetTexture(DriverDevice, Stage, Resource);
}

VOID
D9Device::DdiStreamSource(
    _In_ const D3DDDIARG_SETSTREAMSOURCE* Args)
{
    if (Args->Stream == 0)
    {
        DriverStreamZero = Args->hVertexBuffer;
        DriverStrideZero = Args->Stride;
    }
    if (Batch != nullptr)
        Batch->NoteBound(D9BoundStreams + Args->Stream, Args->hVertexBuffer);
    if (!D9RecordCall(Batch, D9BatchCall::StreamSource, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetStreamSource(DriverDevice, Args);
}

VOID
D9Device::DdiIndices(
    _In_ const D3DDDIARG_SETINDICES* Args)
{
    if (Batch != nullptr)
        Batch->NoteBound(D9BoundIndices, Args->hIndexBuffer);
    if (!D9RecordCall(Batch, D9BatchCall::Indices, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetIndices(DriverDevice, Args);
}

VOID
D9Device::DdiVertexShaderDecl(
    _In_ HANDLE Handle)
{
    if (!D9RecordCall(Batch, D9BatchCall::VertexShaderDecl, &Handle, sizeof(Handle)))
        DeviceFunctions.pfnSetVertexShaderDecl(DriverDevice, Handle);
}

VOID
D9Device::DdiPixelShader(
    _In_opt_ HANDLE Handle)
{
    if (!D9RecordCall(Batch, D9BatchCall::PixelShader, &Handle, sizeof(Handle)))
        DeviceFunctions.pfnSetPixelShader(DriverDevice, Handle);
}

HRESULT
D9Device::DdiDrawPrimitive(
    _In_ const D3DDDIARG_DRAWPRIMITIVE* Args)
{
    HRESULT hr;

    if (D9RecordCall(Batch, D9BatchCall::DrawPrimitive, Args, sizeof(*Args)))
        return S_OK;
    hr = DeviceFunctions.pfnDrawPrimitive(DriverDevice, Args, nullptr);
    if (FAILED(hr))
        D9_TRACE_ONCE(L"the driver failed a draw (0x%08lX)", hr);
    return hr;
}

HRESULT
D9Device::DdiDrawIndexedPrimitive(
    _In_ const D3DDDIARG_DRAWINDEXEDPRIMITIVE* Args)
{
    HRESULT hr;

    if (D9RecordCall(Batch, D9BatchCall::DrawIndexedPrimitive, Args, sizeof(*Args)))
        return S_OK;
    hr = DeviceFunctions.pfnDrawIndexedPrimitive(DriverDevice, Args);
    if (FAILED(hr))
        D9_TRACE_ONCE(L"the driver failed an indexed draw (0x%08lX)", hr);
    return hr;
}

/* Draws from the transformed vertex buffer; the batch records the plain draw and hands one with indices
   from memory to the driver at once, after what it holds */
HRESULT
D9Device::DdiDrawPrimitive2(
    _In_ const D3DDDIARG_DRAWPRIMITIVE2* Args)
{
    HRESULT hr;

    if (D9RecordCall(Batch, D9BatchCall::DrawPrimitive2, Args, sizeof(*Args)))
        return S_OK;
    hr = DeviceFunctions.pfnDrawPrimitive2(DriverDevice, Args);
    if (FAILED(hr))
        D9_TRACE_ONCE(L"the driver failed a transformed draw (0x%08lX)", hr);
    return hr;
}

HRESULT
D9Device::DdiDrawIndexedPrimitive2(
    _In_ const D3DDDIARG_DRAWINDEXEDPRIMITIVE2* Args,
    _In_ UINT IndexSize,
    _In_ const VOID* IndexData)
{
    HRESULT hr;

    DrainBatch();
    hr = DeviceFunctions.pfnDrawIndexedPrimitive2(DriverDevice, Args, IndexSize, IndexData, nullptr);
    if (FAILED(hr))
        D9_TRACE_ONCE(L"the driver failed a transformed indexed draw (0x%08lX)", hr);
    return hr;
}

VOID
D9Device::DdiViewport(
    _In_ const D3DDDIARG_VIEWPORTINFO* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::Viewport, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetViewport(DriverDevice, Args);
}

VOID
D9Device::DdiZRange(
    _In_ const D3DDDIARG_ZRANGE* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::ZRange, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetZRange(DriverDevice, Args);
}

VOID
D9Device::DdiScissorRect(
    _In_ const RECT* Rect)
{
    if (!D9RecordCall(Batch, D9BatchCall::ScissorRect, Rect, sizeof(*Rect)))
        DeviceFunctions.pfnSetScissorRect(DriverDevice, Rect);
}

VOID
D9Device::DdiClipPlane(
    _In_ const D3DDDIARG_SETCLIPPLANE* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::ClipPlane, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetClipPlane(DriverDevice, Args);
}

VOID
D9Device::DdiStreamSourceFreq(
    _In_ const D3DDDIARG_SETSTREAMSOURCEFREQ* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::StreamSourceFreq, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetStreamSourceFreq(DriverDevice, Args);
}

VOID
D9Device::DdiWInfo(
    _In_ const D3DDDIARG_WINFO* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::WInfo, Args, sizeof(*Args)))
        DeviceFunctions.pfnUpdateWInfo(DriverDevice, Args);
}

/* Weightless kernels join the batch; weights would have to outlive it, so those go straight to the driver */
VOID
D9Device::DdiConvolutionKernel(
    _In_ const D3DDDIARG_SETCONVOLUTIONKERNELMONO* Args)
{
    if (Args->pKernelRow == nullptr && Args->pKernelCol == nullptr &&
        D9RecordCall(Batch, D9BatchCall::ConvolutionKernel, Args, sizeof(*Args)))
    {
        return;
    }
    DrainBatch();
    DeviceFunctions.pfnSetConvolutionKernelMono(DriverDevice, Args);
}

/* The query joins the calls the batch holds, so reading or destroying it waits for them */
VOID
D9Device::DdiIssueQuery(
    _In_ const D3DDDIARG_ISSUEQUERY* Args)
{
    if (D9RecordCall(Batch, D9BatchCall::IssueQuery, Args, sizeof(*Args)))
        Batch->Reference(Args->hQuery);
    else
        DeviceFunctions.pfnIssueQuery(DriverDevice, Args);
}

VOID
D9Device::DdiRenderTarget(
    _In_ const D3DDDIARG_SETRENDERTARGET* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::RenderTarget, Args, sizeof(*Args)))
    {
        DeviceFunctions.pfnSetRenderTarget(DriverDevice, Args);
        return;
    }
    Batch->NoteBound(Args->RenderTargetIndex, Args->hRenderTarget);
}

VOID
D9Device::DdiDepthStencil(
    _In_ const D3DDDIARG_SETDEPTHSTENCIL* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::DepthStencil, Args, sizeof(*Args)))
    {
        DeviceFunctions.pfnSetDepthStencil(DriverDevice, Args);
        return;
    }
    Batch->NoteBound(D9BoundDepth, Args->hZBuffer);
}

/* A recorded clear keeps its rectangle count and then the rectangles after the arguments */
HRESULT
D9Device::DdiClear(
    _In_ const D3DDDIARG_CLEAR* Args,
    _In_ UINT Count,
    _In_reads_(Count) const RECT* Rects)
{
    UINT Size = sizeof(*Args) + 2 * sizeof(UINT) + Count * sizeof(*Rects);
    BYTE* Slot = Batch != nullptr ? static_cast<BYTE*>(Batch->Record(D9BatchCall::Clear, Size)) : nullptr;
    UINT* Kept;

    if (Slot == nullptr)
        return DeviceFunctions.pfnClear(DriverDevice, Args, Count, Rects);

    CopyMemory(Slot, Args, sizeof(*Args));
    Kept = reinterpret_cast<UINT*>(Slot + sizeof(*Args));
    Kept[0] = Count;
    Kept[1] = 0;
    CopyMemory(Kept + 2, Rects, Count * sizeof(*Rects));
    return S_OK;
}

/* A batch records the copy and remembers both resources; a driver failure fails the call */
HRESULT
D9Device::DriverBlt(
    _In_ const D3DDDIARG_BLT* Args)
{
    HRESULT hr;

    if (D9RecordCall(Batch, D9BatchCall::Blt, Args, sizeof(*Args)))
    {
        Batch->Reference(Args->hSrcResource);
        Batch->Reference(Args->hDstResource);
        return S_OK;
    }
    hr = DeviceFunctions.pfnBlt(DriverDevice, Args);
    if (FAILED(hr))
    {
        D9Trace(L"UMD Blt failed 0x%08lX", hr);
        return E_FAIL;
    }
    return S_OK;
}

HRESULT
D9Device::DriverColorFill(
    _In_ const D3DDDIARG_COLORFILL* Args)
{
    HRESULT hr;

    if (D9RecordCall(Batch, D9BatchCall::ColorFill, Args, sizeof(*Args)))
    {
        Batch->Reference(Args->hResource);
        return S_OK;
    }
    hr = DeviceFunctions.pfnColorFill(DriverDevice, Args);
    if (FAILED(hr))
    {
        D9Trace(L"UMD ColorFill failed 0x%08lX", hr);
        return E_FAIL;
    }
    return S_OK;
}

/* Hands a single source present to the worker; FALSE leaves it to the caller */
BOOLEAN
D9Device::DdiPresent1(
    _In_ const D3DDDIARG_PRESENT1* Args)
{
    BYTE* Slot;

    if (Batch == nullptr || Args->SrcResources != 1)
        return FALSE;

    Slot = static_cast<BYTE*>(Batch->Record(D9BatchCall::Present1,
                                            sizeof(*Args) + sizeof(D3DDDIARG_PRESENTSURFACE)));
    if (Slot == nullptr)
        return FALSE;
    CopyMemory(Slot, Args, sizeof(*Args));
    CopyMemory(Slot + sizeof(*Args), Args->phSrcResources, sizeof(D3DDDIARG_PRESENTSURFACE));
    Batch->Submit();
    return TRUE;
}

HRESULT
D9Device::CreateDriverShader(
    _In_ BOOLEAN Pixel,
    _In_reads_bytes_(Size) const DWORD* Code,
    _In_ UINT Size,
    _Out_ HANDLE* Handle)
{
    HRESULT hr;

    *Handle = NULL;
    if (Pixel)
    {
        D3DDDIARG_CREATEPIXELSHADER Args = {};

        Args.CodeSize = Size;
        hr = DeviceFunctions.pfnCreatePixelShader(DriverDevice, &Args, reinterpret_cast<const UINT*>(Code));
        *Handle = Args.ShaderHandle;
    }
    else
    {
        D3DDDIARG_CREATEVERTEXSHADERFUNC Args = {};

        Args.Size = Size;
        hr = DeviceFunctions.pfnCreateVertexShaderFunc(DriverDevice, &Args, reinterpret_cast<const UINT*>(Code));
        *Handle = Args.ShaderHandle;
    }
    if (FAILED(hr))
        D9Trace(L"CreateDriverShader: the driver failed with %08lX", hr);
    return hr;
}

/* A batch may still bind the shader, so it runs first */
VOID
D9Device::DestroyDriverShader(
    _In_ BOOLEAN Pixel,
    _In_ HANDLE Handle)
{
    DrainBatch();
    if (Pixel)
        DeviceFunctions.pfnDeletePixelShader(DriverDevice, Handle);
    else
        DeviceFunctions.pfnDeleteVertexShaderFunc(DriverDevice, Handle);
}
