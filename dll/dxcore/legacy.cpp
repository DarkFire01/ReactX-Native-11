/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The older D3DKMT entry points gdi32 reaches through dxcore
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

static constexpr NTSTATUS DcStatusNameTooLong = static_cast<NTSTATUS>(0x80000008);

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTCreateAllocation(
    _Inout_ D3DKMT_CREATEALLOCATION* Request)
{
    D3DDDI_ALLOCATIONINFO* Legacy;
    D3DDDI_ALLOCATIONINFO2 Single;
    D3DDDI_ALLOCATIONINFO2* Converted;
    D3DDDI_ALLOCATIONINFO2* Allocated = nullptr;
    UINT Count;
    NTSTATUS Status;

    if (DcKmt()->CreateAllocation == nullptr)
        return DcKmtCall(DcKmt()->LegacyCreateAllocation, Request);

    Count = Request->NumAllocations;
    Legacy = const_cast<D3DDDI_ALLOCATIONINFO*>(Request->pAllocationInfo);
    Converted = &Single;
    memset(&Single, 0, sizeof(Single));

    if (Count > 1)
    {
        Allocated = static_cast<D3DDDI_ALLOCATIONINFO2*>(LocalAlloc(LMEM_FIXED, sizeof(*Allocated) * Count));
        if (Allocated == nullptr)
            return STATUS_NO_MEMORY;
        Converted = Allocated;
    }

    for (UINT Slot = 0; Slot < Count; Slot++)
    {
        memset(&Converted[Slot], 0, sizeof(Converted[Slot]));
        Converted[Slot].hAllocation = Legacy[Slot].hAllocation;
        Converted[Slot].pSystemMem = Legacy[Slot].pSystemMem;
        Converted[Slot].pPrivateDriverData = Legacy[Slot].pPrivateDriverData;
        Converted[Slot].PrivateDriverDataSize = Legacy[Slot].PrivateDriverDataSize;
        Converted[Slot].VidPnSourceId = Legacy[Slot].VidPnSourceId;
        Converted[Slot].Flags.Primary = Legacy[Slot].Flags.Primary;
        Converted[Slot].Flags.Stereo = Legacy[Slot].Flags.Stereo;
    }

    Request->pAllocationInfo2 = Converted;
    Status = DcKmt()->CreateAllocation(Request);
    if (NT_SUCCESS(Status))
    {
        D3DDDI_ALLOCATIONINFO* Back = Legacy;

        for (UINT Slot = 0; Slot < Count; Slot++)
        {
            Back[Slot].hAllocation = Converted[Slot].hAllocation;
            Back[Slot].pSystemMem = Converted[Slot].pSystemMem;
            Back[Slot].pPrivateDriverData = Converted[Slot].pPrivateDriverData;
            Back[Slot].PrivateDriverDataSize = Converted[Slot].PrivateDriverDataSize;
            Back[Slot].VidPnSourceId = Converted[Slot].VidPnSourceId;
            Back[Slot].Flags.Value = 0;
            Back[Slot].Flags.Primary = Converted[Slot].Flags.Primary;
            Back[Slot].Flags.Stereo = Converted[Slot].Flags.Stereo;
        }
    }

    if (Allocated != nullptr)
        LocalFree(Allocated);

    Request->pAllocationInfo = Legacy;
    return Status;
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE* Request)
{
    D3DDDI_OPENALLOCATIONINFO* Legacy;
    D3DDDI_OPENALLOCATIONINFO2 Single;
    D3DDDI_OPENALLOCATIONINFO2* Converted;
    D3DDDI_OPENALLOCATIONINFO2* Allocated = nullptr;
    UINT Count;
    NTSTATUS Status;

    if (Request == nullptr)
        return STATUS_INVALID_PARAMETER;
    if (DcKmt()->OpenResource == nullptr)
        return DcKmtCall(DcKmt()->LegacyOpenResource, Request);

    Count = Request->NumAllocations;
    Legacy = Request->pOpenAllocationInfo;
    Converted = &Single;
    memset(&Single, 0, sizeof(Single));

    if (Count > 1)
    {
        Allocated = static_cast<D3DDDI_OPENALLOCATIONINFO2*>(LocalAlloc(LMEM_FIXED, sizeof(*Allocated) * Count));
        if (Allocated == nullptr)
            return STATUS_NO_MEMORY;
        Converted = Allocated;
    }

    for (UINT Slot = 0; Slot < Count; Slot++)
    {
        memset(&Converted[Slot], 0, sizeof(Converted[Slot]));
        Converted[Slot].hAllocation = Legacy[Slot].hAllocation;
        Converted[Slot].pPrivateDriverData = Legacy[Slot].pPrivateDriverData;
        Converted[Slot].PrivateDriverDataSize = Legacy[Slot].PrivateDriverDataSize;
    }

    Request->pOpenAllocationInfo2 = Converted;
    Status = DcKmt()->OpenResource(Request);
    if (NT_SUCCESS(Status))
    {
        for (UINT Slot = 0; Slot < Count; Slot++)
        {
            Legacy[Slot].hAllocation = Converted[Slot].hAllocation;
            Legacy[Slot].pPrivateDriverData = Converted[Slot].pPrivateDriverData;
            Legacy[Slot].PrivateDriverDataSize = Converted[Slot].PrivateDriverDataSize;
        }
    }

    if (Allocated != nullptr)
        LocalFree(Allocated);

    Request->pOpenAllocationInfo = Legacy;
    return Status;
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTCreateSynchronizationObject(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT* Request)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Converted = {};
    NTSTATUS Status;

    if (Request == nullptr)
        return STATUS_INVALID_PARAMETER;
    if (DcKmt()->CreateSynchronizationObject == nullptr)
        return DcKmtCall(DcKmt()->LegacyCreateSynchronizationObject, Request);

    Converted.hDevice = Request->hDevice;
    Converted.Info.Type = Request->Info.Type;
    if (Request->Info.Type == D3DDDI_SYNCHRONIZATION_MUTEX)
    {
        Converted.Info.SynchronizationMutex.InitialState = Request->Info.SynchronizationMutex.InitialState;
    }
    else if (Request->Info.Type == D3DDDI_SEMAPHORE)
    {
        Converted.Info.Semaphore.MaxCount = Request->Info.Semaphore.MaxCount;
        Converted.Info.Semaphore.InitialCount = Request->Info.Semaphore.InitialCount;
    }
    else
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DcKmt()->CreateSynchronizationObject(&Converted);
    if (NT_SUCCESS(Status))
        Request->hSyncObject = Converted.hSyncObject;

    return Status;
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTWaitForSynchronizationObject(
    _In_ const D3DKMT_WAITFORSYNCHRONIZATIONOBJECT* Request)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 Converted = {};
    UINT Copied;

    if (Request == nullptr)
        return STATUS_INVALID_PARAMETER;
    if (DcKmt()->WaitForSynchronizationObject == nullptr)
        return DcKmtCall(DcKmt()->LegacyWaitForSynchronizationObject, const_cast<D3DKMT_WAITFORSYNCHRONIZATIONOBJECT*>(Request));

    Converted.hContext = Request->hContext;
    Converted.ObjectCount = Request->ObjectCount;
    Copied = std::min<UINT>(Request->ObjectCount, D3DDDI_MAX_OBJECT_WAITED_ON);
    memcpy(Converted.ObjectHandleArray, Request->ObjectHandleArray, Copied * sizeof(D3DKMT_HANDLE));
    return DcKmt()->WaitForSynchronizationObject(&Converted);
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTSignalSynchronizationObject(
    _In_ const D3DKMT_SIGNALSYNCHRONIZATIONOBJECT* Request)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 Converted = {};
    UINT Copied;

    if (Request == nullptr)
        return STATUS_INVALID_PARAMETER;
    if (DcKmt()->SignalSynchronizationObject == nullptr)
        return DcKmtCall(DcKmt()->LegacySignalSynchronizationObject, const_cast<D3DKMT_SIGNALSYNCHRONIZATIONOBJECT*>(Request));

    Converted.hContext = Request->hContext;
    Converted.ObjectCount = Request->ObjectCount;
    Copied = std::min<UINT>(Request->ObjectCount, D3DDDI_MAX_OBJECT_SIGNALED);
    memcpy(Converted.ObjectHandleArray, Request->ObjectHandleArray, Copied * sizeof(D3DKMT_HANDLE));
    Converted.Flags = Request->Flags;
    return DcKmt()->SignalSynchronizationObject(&Converted);
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTSetVidPnSourceOwner(
    _In_ const D3DKMT_SETVIDPNSOURCEOWNER* Request)
{
    D3DKMT_SETVIDPNSOURCEOWNER2 Converted = {};

    if (DcKmt()->SetVidPnSourceOwner == nullptr)
        return DcKmtCall(DcKmt()->LegacySetVidPnSourceOwner, const_cast<D3DKMT_SETVIDPNSOURCEOWNER*>(Request));

    /* The old form always allowed output duplication */
    Converted.Version1.Version0 = *Request;
    Converted.Version1.Flags.AllowOutputDuplication = 1;
    return DcKmt()->SetVidPnSourceOwner(&Converted);
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTSetVidPnSourceOwner1(
    _In_ const D3DKMT_SETVIDPNSOURCEOWNER1* Request)
{
    D3DKMT_SETVIDPNSOURCEOWNER2 Converted = {};

    if (DcKmt()->SetVidPnSourceOwner == nullptr)
        return DcKmtCall(DcKmt()->LegacySetVidPnSourceOwner1, const_cast<D3DKMT_SETVIDPNSOURCEOWNER1*>(Request));

    Converted.Version1 = *Request;
    return DcKmt()->SetVidPnSourceOwner(&Converted);
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTSetDisplayPrivateDriverFormat(
    _In_ const D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT* Request)
{
    return STATUS_PROCEDURE_NOT_FOUND;
}

/* The block NtGdiDdDDINetDispStartMiracastDisplayDevice reads */
struct DC_MIRACAST_START
{
    UINT Size;
    WCHAR DeviceInterface[260];
    UINT Version;
    void* Context;
    BYTE Reserved[0x228 - 0x218];
};

static_assert(sizeof(DC_MIRACAST_START) == 0x228, "the kernel reads a 552 byte block");

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTNetDispStartMiracastDisplayDevice2(
    _In_z_ PCWSTR DeviceInterface,
    _In_ UINT Version,
    _In_opt_ void* Context)
{
    DC_MIRACAST_START Start = {};
    size_t Length;

    if (DcKmt()->NetDispStartMiracastDisplayDevice == nullptr)
    {
        typedef NTSTATUS (APIENTRY* PFN_DC_MIRACAST2)(PCWSTR, UINT, void*);
        PFN_DC_MIRACAST2 Legacy = reinterpret_cast<PFN_DC_MIRACAST2>(DcKmt()->LegacyNetDispStartMiracastDisplayDevice2);

        return (Legacy != nullptr) ? Legacy(DeviceInterface, Version, Context) : STATUS_NOT_IMPLEMENTED;
    }

    Start.Size = sizeof(Start);
    Start.Version = Version;
    Start.Context = Context;
    Length = wcslen(DeviceInterface);
    if (Length >= 0x105)
        return DcStatusNameTooLong;

    wcsncpy_s(Start.DeviceInterface, _countof(Start.DeviceInterface), DeviceInterface, Length);
    return DcKmt()->NetDispStartMiracastDisplayDevice(&Start);
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTNetDispStartMiracastDisplayDevice(
    _In_z_ PCWSTR DeviceInterface,
    _In_opt_ void* Context)
{
    return D3DKMTNetDispStartMiracastDisplayDevice2(DeviceInterface, 1, Context);
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTPinDirectFlipResources(
    _In_ const D3DKMT_PINDIRECTFLIPRESOURCES* Request)
{
    struct
    {
        D3DKMT_HANDLE Device;
        UINT Count;
        const D3DKMT_HANDLE* Resources;
        UINT Pin;
        UINT Reserved[3];
    } Converted = {};

    if (DcKmt()->PinResources == nullptr)
        return DcKmtCall(DcKmt()->LegacyPinDirectFlipResources, const_cast<D3DKMT_PINDIRECTFLIPRESOURCES*>(Request));

    Converted.Device = Request->hDevice;
    Converted.Count = Request->ResourceCount;
    Converted.Resources = Request->pResourceList;
    Converted.Pin = 1;
    return DcKmt()->PinResources(&Converted);
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTUnpinDirectFlipResources(
    _In_ const D3DKMT_UNPINDIRECTFLIPRESOURCES* Request)
{
    struct
    {
        D3DKMT_HANDLE Device;
        UINT Count;
        const D3DKMT_HANDLE* Resources;
        UINT64 Reserved;
    } Converted = {};

    if (DcKmt()->UnpinResources == nullptr)
        return DcKmtCall(DcKmt()->LegacyUnpinDirectFlipResources, const_cast<D3DKMT_UNPINDIRECTFLIPRESOURCES*>(Request));

    Converted.Device = Request->hDevice;
    Converted.Count = Request->ResourceCount;
    Converted.Resources = Request->pResourceList;
    return DcKmt()->UnpinResources(&Converted);
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTQueryFeatureInterface(
    _Inout_ D3DKMT_QUERYFEATUREINTERFACE* Request)
{
    D3DKMT_ISFEATUREENABLED Feature = {};
    NTSTATUS Status;

    Request->Result.Value = 0;
    Feature.hAdapter = Request->hAdapter;
    Feature.FeatureId = Request->FeatureId;
    Status = DcKmtCall(DcKmt()->IsFeatureEnabled, &Feature);
    if (!NT_SUCCESS(Status))
        return Status;

    Request->Result = Feature.Result;
    Request->InterfaceTableSize = 0;
    Request->InterfaceTable = nullptr;
    if (!Feature.Result.Enabled)
        return STATUS_SUCCESS;

    /* ReactX carries no user mode feature interfaces yet */
    return STATUS_UNSUCCESSFUL;
}

/* A trim callback waiting on the shared trim state name */
struct DcTrimEntry
{
    LIST_ENTRY Link;
    LUID AdapterLuid;
    D3DKMT_HANDLE Device;
    void* Context;
    PFND3DKMT_TRIMNOTIFICATIONCALLBACK Callback;
    BOOLEAN Started;
};

/* What the trim state name carries */
struct DcTrimState
{
    LUID AdapterLuid;
    UINT64 BytesToTrim;
    UINT Flags;
};

static SRWLOCK DcTrimLock = SRWLOCK_INIT;
static LIST_ENTRY DcTrimList = { &DcTrimList, &DcTrimList };
static void* DcTrimSubscription;
static constexpr ULONG64 DcTrimStateName = 0x41C61629A3BC30B5ull;

static
VOID
DcListRemove(
    _Inout_ LIST_ENTRY* Entry)
{
    Entry->Blink->Flink = Entry->Flink;
    Entry->Flink->Blink = Entry->Blink;
}

static
VOID
DcListAppend(
    _Inout_ LIST_ENTRY* Head,
    _Inout_ LIST_ENTRY* Entry)
{
    Entry->Flink = Head;
    Entry->Blink = Head->Blink;
    Head->Blink->Flink = Entry;
    Head->Blink = Entry;
}

typedef NTSTATUS (NTAPI* PFN_DC_TRIM_CALLBACK)(ULONG64, ULONG, void*, void*, const void*, ULONG);
typedef NTSTATUS (NTAPI* PFN_DC_TRIM_SUBSCRIBE)(void**, ULONG64, ULONG, PFN_DC_TRIM_CALLBACK, void*, void*, ULONG, ULONG);
typedef NTSTATUS (NTAPI* PFN_DC_TRIM_UNSUBSCRIBE)(void*);

static
NTSTATUS
NTAPI
DcTrimStateChanged(
    _In_ ULONG64 StateName,
    _In_ ULONG ChangeStamp,
    _In_opt_ void* TypeId,
    _In_opt_ void* Context,
    _In_reads_bytes_opt_(Size) const void* Data,
    _In_ ULONG Size)
{
    const DcTrimState* State = static_cast<const DcTrimState*>(Data);
    DcTrimEntry* Rotate = nullptr;
    UINT64 Budget = State->BytesToTrim;
    LIST_ENTRY* Link;

    AcquireSRWLockExclusive(&DcTrimLock);
    for (Link = DcTrimList.Flink; Link != &DcTrimList;)
    {
        DcTrimEntry* Entry = CONTAINING_RECORD(Link, DcTrimEntry, Link);
        D3DKMT_TRIMNOTIFICATION Note = {};

        Link = Link->Flink;
        if (Entry->AdapterLuid.LowPart != State->AdapterLuid.LowPart ||
            Entry->AdapterLuid.HighPart != State->AdapterLuid.HighPart)
        {
            continue;
        }

        Note.Context = Entry->Context;
        Note.Flags.Value = State->Flags;

        /* The first notice a callback sees turns a periodic trim into a restart */
        if (!Entry->Started)
        {
            if (Note.Flags.Value & 1)
                Note.Flags.Value = (Note.Flags.Value & ~3u) | 2;
            Entry->Started = TRUE;
        }

        if (Note.Flags.Value & 4)
        {
            if (Rotate == nullptr)
                Rotate = Entry;
            Note.NumBytesToTrim = Budget;
            if (Budget == 0)
                Note.Flags.Value &= ~4u;
        }

        if (Note.Flags.Value != 0)
        {
            Entry->Callback(&Note);
            if (Note.Flags.Value & 4)
                Budget = Note.NumBytesToTrim;
        }
    }

    /* The callback that trimmed first goes to the back next time */
    if (Rotate != nullptr)
    {
        DcListRemove(&Rotate->Link);
        DcListAppend(&DcTrimList, &Rotate->Link);
    }

    ReleaseSRWLockExclusive(&DcTrimLock);
    return STATUS_SUCCESS;
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTRegisterTrimNotification(
    _Inout_ D3DKMT_REGISTERTRIMNOTIFICATION* Request)
{
    static PFN_DC_TRIM_SUBSCRIBE Subscribe = reinterpret_cast<PFN_DC_TRIM_SUBSCRIBE>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlSubscribeWnfStateChangeNotification"));
    DcTrimEntry* Entry = static_cast<DcTrimEntry*>(HeapAlloc(GetProcessHeap(), 0, sizeof(DcTrimEntry)));
    NTSTATUS Status = STATUS_SUCCESS;

    if (Entry == nullptr)
        return STATUS_NO_MEMORY;

    Entry->AdapterLuid = Request->AdapterLuid;
    Entry->Device = Request->hDevice;
    Entry->Context = Request->Context;
    Entry->Callback = Request->Callback;
    Entry->Started = FALSE;

    AcquireSRWLockExclusive(&DcTrimLock);
    if (DcTrimSubscription == nullptr)
    {
        Status = (Subscribe != nullptr) ?
            Subscribe(&DcTrimSubscription, DcTrimStateName, 0, DcTrimStateChanged, nullptr, nullptr, 0, 0) :
            STATUS_NOT_IMPLEMENTED;
    }

    if (NT_SUCCESS(Status))
        DcListAppend(&DcTrimList, &Entry->Link);
    ReleaseSRWLockExclusive(&DcTrimLock);

    if (!NT_SUCCESS(Status))
    {
        HeapFree(GetProcessHeap(), 0, Entry);
        return Status;
    }

    Request->Handle = Entry;
    return STATUS_SUCCESS;
}

EXTERN_C
NTSTATUS
APIENTRY
D3DKMTUnregisterTrimNotification(
    _In_ D3DKMT_UNREGISTERTRIMNOTIFICATION* Request)
{
    static PFN_DC_TRIM_UNSUBSCRIBE Unsubscribe = reinterpret_cast<PFN_DC_TRIM_UNSUBSCRIBE>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlUnsubscribeWnfStateChangeNotification"));
    void* Subscription = nullptr;

    AcquireSRWLockExclusive(&DcTrimLock);
    if (Request->Handle != nullptr)
    {
        DcTrimEntry* Entry = static_cast<DcTrimEntry*>(Request->Handle);

        DcListRemove(&Entry->Link);
        HeapFree(GetProcessHeap(), 0, Entry);
    }
    else
    {
        for (LIST_ENTRY* Link = DcTrimList.Flink; Link != &DcTrimList;)
        {
            DcTrimEntry* Entry = CONTAINING_RECORD(Link, DcTrimEntry, Link);

            Link = Link->Flink;
            if (Entry->Callback == Request->Callback)
            {
                DcListRemove(&Entry->Link);
                HeapFree(GetProcessHeap(), 0, Entry);
            }
        }
    }

    if (DcTrimList.Flink == &DcTrimList && DcTrimSubscription != nullptr)
    {
        Subscription = DcTrimSubscription;
        DcTrimSubscription = nullptr;
    }
    ReleaseSRWLockExclusive(&DcTrimLock);

    if (Subscription != nullptr && Unsubscribe != nullptr)
        Unsubscribe(Subscription);

    return STATUS_SUCCESS;
}
