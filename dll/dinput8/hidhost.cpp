/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runs the HID game controller code shared with DirectInput 7 on this library's device layer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "../dinput/hid.hpp"
#include "input.hpp"
#include "shared.hpp"

#include <new>
#include <stdlib.h>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

/* The shared code loads its strings from the module it is linked into */
HINSTANCE DiModule = reinterpret_cast<HINSTANCE>(&__ImageBase);

/* The scan code tables keytables.cpp holds, in the order the shared code numbers them */
extern const BYTE DiKeyTranslations[9][256];

static_assert(sizeof(DI_OBJECT) == sizeof(DIOBJECTDATAFORMAT), "object layout");
static_assert(sizeof(DI_JOYSTICK_SLOT) == sizeof(RX_DI_JOY_SLOT), "joystick slot layout");
static_assert(DI_JOYSTICK_SLOTS == DI_JOY_SLOTS, "joystick slot count");

/* A HID device's view of the emulation layer, handed to the shared code as its sink */
struct DI_HID_SINK
{
    DI_EM_CLASS Class;
    RX_DI_SOURCE* Source;
    ULONG_PTR Extra;
    DI_EM_INSTANCE* Instance;
    DI_HID_SINK* Next;
};

static DI_HID_SINK* DiHidSinks;

static
DI_HID_SINK*
DiHidSink(
    _In_ PRX_DI_SINK Sink)
{
    return reinterpret_cast<DI_HID_SINK*>(Sink);
}

static
HRESULT
DiHidSwitch(
    _In_ VOID* Context,
    _In_ BOOLEAN On)
{
    DI_HID_SINK* Sink = static_cast<DI_HID_SINK*>(Context);
    RX_DI_SOURCE* Source = Sink->Source;
    HRESULT Result;

    if (!On)
    {
        InterlockedDecrement(&Source->AcquireCount);
        return Source->Acquire(reinterpret_cast<PRX_DI_SINK>(Sink), FALSE);
    }

    InterlockedIncrement(&Source->AcquireCount);
    Result = Source->Acquire(reinterpret_cast<PRX_DI_SINK>(Sink), TRUE);
    if (FAILED(Result))
    {
        InterlockedDecrement(&Source->AcquireCount);
        DiTrace(L"input capture start failed, 0x%08lx", Result);
    }
    return Result;
}

HRESULT
DiOpenSink(
    _In_ const RX_DI_SINK_PARAMS* Params,
    _In_ RX_DI_SOURCE* Source,
    _Outptr_ PRX_DI_SINK* Result)
{
    DI_HID_SINK* Sink;
    DI_FORMAT Format;
    HRESULT Status;

    *Result = nullptr;
    Sink = static_cast<DI_HID_SINK*>(calloc(1, sizeof(*Sink)));
    if (Sink == nullptr)
        return E_OUTOFMEMORY;
    Sink->Class.ObjectTypes = static_cast<DWORD*>(calloc(Params->StateSize + 1, sizeof(DWORD)));
    if (Sink->Class.ObjectTypes == nullptr)
    {
        free(Sink);
        return E_OUTOFMEMORY;
    }
    Sink->Class.DataSize = Params->StateSize;
    Sink->Class.State = Source->State;
    Sink->Class.Switch = DiHidSwitch;
    Sink->Class.Context = Sink;
    Sink->Source = Source;
    Sink->Extra = Params->Extra;

    Format.DataSize = Params->StateSize;
    Format.ObjectCount = Params->ObjectCount;
    Format.Objects = reinterpret_cast<DI_OBJECT*>(const_cast<DIOBJECTDATAFORMAT*>(Params->Objects));
    Status = DiEmCreate(&Sink->Class, &Format, Params->Mode << 16, &Sink->Instance);
    if (FAILED(Status))
    {
        free(Sink->Class.ObjectTypes);
        free(Sink);
        return Status;
    }
    InterlockedIncrement(&Source->RefCount);
    EnterCriticalSection(&DiLock);
    Sink->Next = DiHidSinks;
    DiHidSinks = Sink;
    LeaveCriticalSection(&DiLock);
    *Result = reinterpret_cast<PRX_DI_SINK>(Sink);
    return S_OK;
}

VOID
DiCloseSink(
    _In_opt_ PRX_DI_SINK Handle)
{
    DI_HID_SINK* Sink;

    if (Handle == nullptr)
        return;
    Sink = DiHidSink(Handle);
    EnterCriticalSection(&DiLock);
    for (DI_HID_SINK** Link = &DiHidSinks; *Link != nullptr; Link = &(*Link)->Next)
    {
        if (*Link == Sink)
        {
            *Link = Sink->Next;
            break;
        }
    }
    LeaveCriticalSection(&DiLock);
    DiEmRelease(Sink->Instance);
    InterlockedDecrement(&Sink->Source->RefCount);
    free(Sink->Class.ObjectTypes);
    free(Sink);
}

VOID
DiPostState(
    _In_ RX_DI_SOURCE* Source,
    _In_reads_bytes_(Source->StateSize) const BYTE* Values,
    _In_ DWORD Time)
{
    DI_EM_CLASS* Class = nullptr;

    EnterCriticalSection(&DiLock);
    for (DI_HID_SINK* Sink = DiHidSinks; Sink != nullptr; Sink = Sink->Next)
    {
        if (Sink->Source == Source)
        {
            Class = &Sink->Class;
            break;
        }
    }
    LeaveCriticalSection(&DiLock);
    if (Class != nullptr)
        DiEmPostState(Class, Values, Time);
}

ULONG
DiSinkFlags(
    _In_ PRX_DI_SINK Sink)
{
    return DiHidSink(Sink)->Instance->Flags;
}

VOID
DiSetSinkExclusive(
    _Inout_ PRX_DI_SINK Sink,
    _In_ BOOLEAN Exclusive)
{
    DI_EM_INSTANCE* Instance = DiHidSink(Sink)->Instance;

    if (Exclusive)
        Instance->Flags |= DI_EM_EXCLUSIVE;
    else
        Instance->Flags &= ~DI_EM_EXCLUSIVE;
}

ULONG_PTR
DiSinkContext(
    _In_ PRX_DI_SINK Sink)
{
    return DiHidSink(Sink)->Extra;
}

ULONG
DiNextGuidSequence(VOID)
{
    ULONG Value;

    WaitForSingleObject(DiGlobalMutex, INFINITE);
    DiShared->UuidCounter += 1;
    Value = DiShared->UuidCounter;
    ReleaseMutex(DiGlobalMutex);
    return Value;
}

VOID
DiLockJoysticks(VOID)
{
    WaitForSingleObject(DiJoystickMutex, INFINITE);
}

VOID
DiUnlockJoysticks(VOID)
{
    ReleaseMutex(DiJoystickMutex);
}

RX_DI_JOY_SLOT*
DiJoystickSlot(
    _In_ ULONG Id)
{
    return reinterpret_cast<RX_DI_JOY_SLOT*>(&DiShared->Joysticks[Id]);
}

PVOID
DiFindTable(
    _In_ ULONG Id)
{
    if (Id == 0 || Id > ARRAYSIZE(DiKeyTranslations))
        return nullptr;
    return const_cast<BYTE*>(DiKeyTranslations[Id - 1]);
}

/* The DirectInput 8 face of a shared HID backend */
class DiHidBackend : public DiBackend
{
public:
    RxDiBackend* Inner;
    DI_FORMAT Native;
    RX_DI_APP_HACKS Hacks;

    DiHidBackend(
        _In_ RxDiBackend* Shared) : Inner(Shared), Native(), Hacks()
    {
        const DIDATAFORMAT* Format = nullptr;

        Inner->GetDataFormat(&Format);
        Native.DataSize = Format->dwDataSize;
        Native.ObjectCount = Format->dwNumObjs;
        Native.Objects = reinterpret_cast<DI_OBJECT*>(Format->rgodf);
        Inner->SetDIData(DIRECTINPUT_VERSION, &Hacks);
    }

    ~DiHidBackend() override
    {
        delete Inner;
    }

    const DI_FORMAT*
    Format(VOID) override
    {
        return &Native;
    }

    HRESULT
    GetCapabilities(
        _Inout_ LPDIDEVCAPS Caps) override
    {
        return Inner->GetCapabilities(Caps);
    }

    HRESULT
    GetDeviceInfo(
        _Inout_ LPDIDEVICEINSTANCEW Info) override
    {
        return Inner->GetDeviceInfo(Info);
    }

    HRESULT
    GetVersions(
        _Out_ LPDIDRIVERVERSIONS Versions) override
    {
        return Inner->GetVersions(Versions);
    }

    DI_EM_INSTANCE*
    Emulation(VOID) override
    {
        PRX_DI_SINK Sink = Inner->GetSink();

        return Sink != nullptr ? DiHidSink(Sink)->Instance : nullptr;
    }

    HRESULT
    GetObjectInfo(
        _In_ DWORD Index,
        _In_ DWORD Type,
        _Inout_ LPDIDEVICEOBJECTINSTANCEW Info) override
    {
        RX_DI_PROP_TARGET Target = { 0, Index, Type };

        return Inner->GetObjectInfo(&Target, Info);
    }

    HRESULT
    GetProperty(
        _In_ const DI_PROPERTY* Property,
        _Inout_ LPDIPROPHEADER Header) override
    {
        RX_DI_PROP_TARGET Target = { Property->Id, Property->Index, Property->Type };

        return Inner->GetProperty(&Target, Header);
    }

    HRESULT
    SetProperty(
        _In_ const DI_PROPERTY* Property,
        _In_ LPCDIPROPHEADER Header) override
    {
        RX_DI_PROP_TARGET Target = { Property->Id, Property->Index, Property->Type };

        return Inner->SetProperty(&Target, Header);
    }

    HRESULT
    SetCooperativeLevel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override
    {
        return Inner->SetCooperativeLevel(Window, Flags);
    }

    HRESULT
    Acquire(VOID) override
    {
        return Inner->Acquire();
    }

    HRESULT
    Unacquire(VOID) override
    {
        return Inner->Unacquire();
    }

    HRESULT
    GetDeviceState(
        _Out_ VOID* State) override
    {
        return Inner->GetDeviceState(State);
    }

    HRESULT
    Poll(VOID) override
    {
        return Inner->Poll();
    }

    HRESULT
    MapUsage(
        _In_ DWORD Usage,
        _Out_ DWORD* Index) override
    {
        return Inner->MapUsage(Usage, Index);
    }

    DWORD
    GetUsage(
        _In_ DWORD Index) override
    {
        return Inner->GetUsage(Index);
    }

    HRESULT
    RunControlPanel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override
    {
        return Inner->RunControlPanel(Window, Flags);
    }
};

static
HRESULT
DiCreateHidBackend(
    _In_ REFGUID Guid,
    _Outptr_ DiBackend** Backend)
{
    PRX_DI_BACKEND_CREATE Create = nullptr;
    RxDiBackend* Shared = nullptr;
    DiHidBackend* Created;
    HRESULT Result;

    *Backend = nullptr;
    Result = DiFindHidBackend(Guid, &Create);
    if (FAILED(Result))
        return Result;
    Result = Create(Guid, &Shared);
    if (FAILED(Result))
        return Result;
    Created = new (std::nothrow) DiHidBackend(Shared);
    if (Created == nullptr)
    {
        delete Shared;
        return E_OUTOFMEMORY;
    }
    *Backend = Created;
    return S_OK;
}

HRESULT
DiFindHidDevice(
    _In_ REFGUID Guid,
    _Out_ PDI_BACKEND_CREATE* Create)
{
    PRX_DI_BACKEND_CREATE Shared = nullptr;
    HRESULT Result;

    *Create = nullptr;
    Result = DiFindHidBackend(Guid, &Shared);
    if (SUCCEEDED(Result))
        *Create = DiCreateHidBackend;
    return Result;
}

HRESULT
DiListHidDevices(
    _Outptr_result_buffer_maybenull_(*Count) GUID** Guids,
    _Out_ DWORD* Count)
{
    RX_DI_HID_SNAPSHOT* Snapshot = nullptr;
    GUID* List;
    HRESULT Result;

    *Guids = nullptr;
    *Count = 0;
    DiRefreshHidList(TRUE);
    Result = DiSnapshotHidList(&Snapshot);
    if (FAILED(Result))
        return Result;
    if (Snapshot == nullptr || Snapshot->Count == 0)
    {
        DiFree(reinterpret_cast<PVOID*>(&Snapshot));
        return S_OK;
    }
    List = static_cast<GUID*>(malloc(Snapshot->Count * sizeof(*List)));
    if (List == nullptr)
    {
        DiFree(reinterpret_cast<PVOID*>(&Snapshot));
        return E_OUTOFMEMORY;
    }
    for (ULONG Index = 0; Index < Snapshot->Count; Index++)
        List[Index] = Snapshot->Entries[Index].Instance;
    *Guids = List;
    *Count = Snapshot->Count;
    DiFree(reinterpret_cast<PVOID*>(&Snapshot));
    return S_OK;
}
