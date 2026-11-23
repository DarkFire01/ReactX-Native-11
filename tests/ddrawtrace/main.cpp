/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DirectDraw runtime traffic to the kernel, printed for comparing runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * ddrawtrace [-runtime system|reactx|<path>] [scenario ...]
 *
 * The runtime's imports of GetProcAddress and every D3DKMT thunk are pointed at wrappers inside this
 * process only, so the kernel calls the runtime and its driver callbacks make are printed with their
 * arguments. Kernel handles print as k ids and global share handles as s ids, in the order they first
 * appear. Calls from other threads are left out, since their timing is not the runtime's to decide.
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>
#include <ddraw.h>
#include <d3dkmthk.h>

#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

typedef NTSTATUS (APIENTRY* PDT_KERNEL)(VOID*);
typedef HRESULT (WINAPI* PDT_CREATE_EX)(GUID*, LPVOID*, REFIID, IUnknown*);

/* Handle kinds, each numbered on its own */
enum DtKind
{
    DtKindKernel,
    DtKindShare,
    DtKindCount,
};

static constexpr UINT DtSlotLimit = 192;

/* Everything the wrappers share */
struct DtState
{
    DWORD MainThread;
    BOOLEAN Wddm2;
    UINT SlotCount;
    PCSTR Names[DtSlotLimit];
    PDT_KERNEL Originals[DtSlotLimit];
    FARPROC (WINAPI* GetProcAddress)(HMODULE, LPCSTR);
    std::map<ULONG_PTR, UINT> Ids[DtKindCount];
    UINT NextId[DtKindCount];
};

static DtState Dt;

static
VOID
DtAppend(
    _Inout_ std::string* Text,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[512];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf_s(Buffer, sizeof(Buffer), _TRUNCATE, Format, Arguments);
    va_end(Arguments);
    Text->append(Buffer);
}

/* A handle as a stable id; zero stays zero */
static
std::string
DtId(
    _In_ DtKind Kind,
    _In_ ULONG_PTR Value)
{
    static const CHAR Prefix[] = { 'k', 's' };
    std::map<ULONG_PTR, UINT>& Ids = Dt.Ids[Kind];
    std::string Text;
    UINT Id;

    if (Value == 0)
        return "0";

    auto Found = Ids.find(Value);
    if (Found != Ids.end())
    {
        Id = Found->second;
    }
    else
    {
        Dt.NextId[Kind] += 1;
        Id = Dt.NextId[Kind];
        Ids.emplace(Value, Id);
    }
    DtAppend(&Text, "%c%u", Prefix[Kind], Id);
    return Text;
}

static
std::string
DtKernel(
    _In_ ULONG_PTR Handle)
{
    return DtId(DtKindKernel, Handle);
}

static
VOID
DtForget(
    _In_ ULONG_PTR Handle)
{
    Dt.Ids[DtKindKernel].erase(Handle);
}

/* FNV-1a over bytes the runtime passes, so blocks compare as one number */
static
UINT
DtHash(
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    UINT Hash = 2166136261u;

    if (Data == nullptr)
        return 0;
    for (SIZE_T Index = 0; Index < Size; Index++)
    {
        Hash ^= Bytes[Index];
        Hash *= 16777619u;
    }
    return Hash;
}

static
VOID
DtAppendRect(
    _Inout_ std::string* Text,
    _In_ const RECT* Rect)
{
    DtAppend(Text, "(%ld,%ld,%ld,%ld)", Rect->left, Rect->top, Rect->right, Rect->bottom);
}

static
VOID
DtAppendBytes(
    _Inout_ std::string* Text,
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ UINT Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);

    if (Data == nullptr)
    {
        Text->append("null");
        return;
    }
    for (UINT Index = 0; Index < Size && Index < 64; Index++)
        DtAppend(Text, "%02X", Bytes[Index]);
}

static
BOOLEAN
DtIs(
    _In_z_ PCSTR Name,
    _In_z_ PCSTR Wanted)
{
    return strcmp(Name, Wanted) == 0;
}

static
VOID
DtDescribeAllocations(
    _Inout_ std::string* Text,
    _In_opt_ const VOID* Info,
    _In_ UINT Count)
{
    for (UINT Index = 0; Info != nullptr && Index < Count && Index < 8; Index++)
    {
        if (Dt.Wddm2)
        {
            const D3DDDI_ALLOCATIONINFO2* Entry = static_cast<const D3DDDI_ALLOCATIONINFO2*>(Info) + Index;

            DtAppend(Text, " [%s drv %u %08X src %u flags %08X prio %08X]", Entry->pSystemMem != nullptr ? "mem" : "nomem",
                     Entry->PrivateDriverDataSize, DtHash(Entry->pPrivateDriverData, Entry->PrivateDriverDataSize),
                     Entry->VidPnSourceId, Entry->Flags.Value, Entry->Priority);
        }
        else
        {
            const D3DDDI_ALLOCATIONINFO* Entry = static_cast<const D3DDDI_ALLOCATIONINFO*>(Info) + Index;

            DtAppend(Text, " [%s drv %u %08X src %u flags %08X]", Entry->pSystemMem != nullptr ? "mem" : "nomem",
                     Entry->PrivateDriverDataSize, DtHash(Entry->pPrivateDriverData, Entry->PrivateDriverDataSize),
                     Entry->VidPnSourceId, Entry->Flags.Value);
        }
    }
}

static
VOID
DtAllocationHandles(
    _Inout_ std::string* Text,
    _In_opt_ const VOID* Info,
    _In_ UINT Count)
{
    for (UINT Index = 0; Info != nullptr && Index < Count && Index < 8; Index++)
    {
        D3DKMT_HANDLE Handle = Dt.Wddm2 ? (static_cast<const D3DDDI_ALLOCATIONINFO2*>(Info) + Index)->hAllocation
                                        : (static_cast<const D3DDDI_ALLOCATIONINFO*>(Info) + Index)->hAllocation;

        DtAppend(Text, " %s", DtKernel(Handle).c_str());
    }
}

static
VOID
DtHandleList(
    _Inout_ std::string* Text,
    _In_opt_ const D3DKMT_HANDLE* Handles,
    _In_ UINT Count)
{
    for (UINT Index = 0; Handles != nullptr && Index < Count && Index < 8; Index++)
        DtAppend(Text, " %s", DtKernel(Handles[Index]).c_str());
}

/* What the runtime passes in, for the calls whose arguments say something */
static
VOID
DtInputs(
    _In_z_ PCSTR Name,
    _In_ VOID* Args,
    _Inout_ std::string* Text)
{
    if (DtIs(Name, "D3DKMTOpenAdapterFromHdc"))
    {
        Text->append(static_cast<D3DKMT_OPENADAPTERFROMHDC*>(Args)->hDc != NULL ? " dc set" : " dc null");
    }
    else if (DtIs(Name, "D3DKMTCloseAdapter"))
    {
        DtAppend(Text, " %s", DtKernel(static_cast<D3DKMT_CLOSEADAPTER*>(Args)->hAdapter).c_str());
    }
    else if (DtIs(Name, "D3DKMTQueryAdapterInfo"))
    {
        D3DKMT_QUERYADAPTERINFO* Query = static_cast<D3DKMT_QUERYADAPTERINFO*>(Args);

        DtAppend(Text, " %s type %u size %u", DtKernel(Query->hAdapter).c_str(), Query->Type,
                 Query->PrivateDriverDataSize);
    }
    else if (DtIs(Name, "D3DKMTCreateDevice"))
    {
        D3DKMT_CREATEDEVICE* Create = static_cast<D3DKMT_CREATEDEVICE*>(Args);

        DtAppend(Text, " adapter %s flags %08X", DtKernel(Create->hAdapter).c_str(),
                 *reinterpret_cast<const UINT*>(&Create->Flags));
    }
    else if (DtIs(Name, "D3DKMTDestroyDevice"))
    {
        DtAppend(Text, " %s", DtKernel(static_cast<D3DKMT_DESTROYDEVICE*>(Args)->hDevice).c_str());
    }
    else if (DtIs(Name, "D3DKMTCreateContext"))
    {
        D3DKMT_CREATECONTEXT* Create = static_cast<D3DKMT_CREATECONTEXT*>(Args);

        DtAppend(Text, " dev %s node %u engine %u flags %08X drv %u hint %u", DtKernel(Create->hDevice).c_str(),
                 Create->NodeOrdinal, Create->EngineAffinity, Create->Flags.Value, Create->PrivateDriverDataSize,
                 Create->ClientHint);
    }
    else if (DtIs(Name, "D3DKMTCreateContextVirtual"))
    {
        D3DKMT_CREATECONTEXTVIRTUAL* Create = static_cast<D3DKMT_CREATECONTEXTVIRTUAL*>(Args);

        DtAppend(Text, " dev %s node %u engine %u flags %08X drv %u hint %u", DtKernel(Create->hDevice).c_str(),
                 Create->NodeOrdinal, Create->EngineAffinity, Create->Flags.Value, Create->PrivateDriverDataSize,
                 Create->ClientHint);
    }
    else if (DtIs(Name, "D3DKMTDestroyContext"))
    {
        DtAppend(Text, " %s", DtKernel(static_cast<D3DKMT_DESTROYCONTEXT*>(Args)->hContext).c_str());
    }
    else if (DtIs(Name, "D3DKMTCreateAllocation") || DtIs(Name, "D3DKMTCreateAllocation2"))
    {
        D3DKMT_CREATEALLOCATION* Create = static_cast<D3DKMT_CREATEALLOCATION*>(Args);

        DtAppend(Text, " dev %s res %s flags %08X drv %u %08X count %u runtime ", DtKernel(Create->hDevice).c_str(),
                 DtKernel(Create->hResource).c_str(), *reinterpret_cast<const UINT*>(&Create->Flags),
                 Create->PrivateDriverDataSize, DtHash(Create->pPrivateDriverData, Create->PrivateDriverDataSize),
                 Create->NumAllocations);
        DtAppendBytes(Text, Create->pPrivateRuntimeData, Create->PrivateRuntimeDataSize);
        DtDescribeAllocations(Text, Create->pAllocationInfo2, Create->NumAllocations);
    }
    else if (DtIs(Name, "D3DKMTDestroyAllocation"))
    {
        D3DKMT_DESTROYALLOCATION* Destroy = static_cast<D3DKMT_DESTROYALLOCATION*>(Args);

        DtAppend(Text, " dev %s res %s count %u", DtKernel(Destroy->hDevice).c_str(),
                 DtKernel(Destroy->hResource).c_str(), Destroy->AllocationCount);
        DtHandleList(Text, Destroy->phAllocationList, Destroy->AllocationCount);
    }
    else if (DtIs(Name, "D3DKMTDestroyAllocation2"))
    {
        D3DKMT_DESTROYALLOCATION2* Destroy = static_cast<D3DKMT_DESTROYALLOCATION2*>(Args);

        DtAppend(Text, " dev %s res %s count %u flags %08X", DtKernel(Destroy->hDevice).c_str(),
                 DtKernel(Destroy->hResource).c_str(), Destroy->AllocationCount, Destroy->Flags.Value);
        DtHandleList(Text, Destroy->phAllocationList, Destroy->AllocationCount);
    }
    else if (DtIs(Name, "D3DKMTSetAllocationPriority"))
    {
        D3DKMT_SETALLOCATIONPRIORITY* Priority = static_cast<D3DKMT_SETALLOCATIONPRIORITY*>(Args);

        DtAppend(Text, " dev %s res %s count %u", DtKernel(Priority->hDevice).c_str(),
                 DtKernel(Priority->hResource).c_str(), Priority->AllocationCount);
        if (Priority->pPriorities != nullptr)
            DtAppend(Text, " prio %08X", Priority->pPriorities[0]);
    }
    else if (DtIs(Name, "D3DKMTLock"))
    {
        D3DKMT_LOCK* Lock = static_cast<D3DKMT_LOCK*>(Args);

        DtAppend(Text, " dev %s %s flags %08X pages %u", DtKernel(Lock->hDevice).c_str(),
                 DtKernel(Lock->hAllocation).c_str(), Lock->Flags.Value, Lock->NumPages);
    }
    else if (DtIs(Name, "D3DKMTUnlock"))
    {
        D3DKMT_UNLOCK* Unlock = static_cast<D3DKMT_UNLOCK*>(Args);

        DtAppend(Text, " dev %s count %u", DtKernel(Unlock->hDevice).c_str(), Unlock->NumAllocations);
        DtHandleList(Text, Unlock->phAllocations, Unlock->NumAllocations);
    }
    else if (DtIs(Name, "D3DKMTLock2"))
    {
        D3DKMT_LOCK2* Lock = static_cast<D3DKMT_LOCK2*>(Args);

        DtAppend(Text, " dev %s %s flags %08X", DtKernel(Lock->hDevice).c_str(), DtKernel(Lock->hAllocation).c_str(),
                 Lock->Flags.Value);
    }
    else if (DtIs(Name, "D3DKMTUnlock2"))
    {
        D3DKMT_UNLOCK2* Unlock = static_cast<D3DKMT_UNLOCK2*>(Args);

        DtAppend(Text, " dev %s %s", DtKernel(Unlock->hDevice).c_str(), DtKernel(Unlock->hAllocation).c_str());
    }
    else if (DtIs(Name, "D3DKMTRender"))
    {
        D3DKMT_RENDER* Render = static_cast<D3DKMT_RENDER*>(Args);

        DtAppend(Text, " ctx %s offset %u length %u allocs %u patches %u flags %08X broadcast %u drv %u",
                 DtKernel(Render->hContext).c_str(), Render->CommandOffset, Render->CommandLength,
                 Render->AllocationCount, Render->PatchLocationCount, *reinterpret_cast<const UINT*>(&Render->Flags),
                 Render->BroadcastContextCount, Render->PrivateDriverDataSize);
    }
    else if (DtIs(Name, "D3DKMTSubmitCommand"))
    {
        D3DKMT_SUBMITCOMMAND* Submit = static_cast<D3DKMT_SUBMITCOMMAND*>(Args);

        DtAppend(Text, " length %u flags %08X broadcast %u", Submit->CommandLength,
                 *reinterpret_cast<const UINT*>(&Submit->Flags), Submit->BroadcastContextCount);
        DtHandleList(Text, Submit->BroadcastContext, Submit->BroadcastContextCount);
        DtAppend(Text, " primaries %u history %u drv %u", Submit->NumPrimaries, Submit->NumHistoryBuffers,
                 Submit->PrivateDriverDataSize);
    }
    else if (DtIs(Name, "D3DKMTPresent"))
    {
        D3DKMT_PRESENT* Present = static_cast<D3DKMT_PRESENT*>(Args);

        DtAppend(Text, " ctx %s window %s src %u from %s to %s color %08X ", DtKernel(Present->hContext).c_str(),
                 Present->hWindow != NULL ? "set" : "null", Present->VidPnSourceId,
                 DtKernel(Present->hSource).c_str(), DtKernel(Present->hDestination).c_str(), Present->Color);
        DtAppendRect(Text, &Present->DstRect);
        Text->append(" ");
        DtAppendRect(Text, &Present->SrcRect);
        DtAppend(Text, " subrects %u interval %u flags %08X broadcast %u regions %s duration %u drv %u",
                 Present->SubRectCnt, Present->FlipInterval, Present->Flags.Value, Present->BroadcastContextCount,
                 Present->pPresentRegions != nullptr ? "set" : "null", Present->Duration,
                 Present->PrivateDriverDataSize);
    }
    else if (DtIs(Name, "D3DKMTEscape"))
    {
        D3DKMT_ESCAPE* Escape = static_cast<D3DKMT_ESCAPE*>(Args);

        DtAppend(Text, " adapter %s dev %s type %u flags %08X size %u ctx %s", DtKernel(Escape->hAdapter).c_str(),
                 DtKernel(Escape->hDevice).c_str(), Escape->Type, Escape->Flags.Value, Escape->PrivateDriverDataSize,
                 DtKernel(Escape->hContext).c_str());
    }
    else if (DtIs(Name, "D3DKMTGetSharedPrimaryHandle"))
    {
        D3DKMT_GETSHAREDPRIMARYHANDLE* Query = static_cast<D3DKMT_GETSHAREDPRIMARYHANDLE*>(Args);

        DtAppend(Text, " %s src %u", DtKernel(Query->hAdapter).c_str(), Query->VidPnSourceId);
    }
    else if (DtIs(Name, "D3DKMTQueryResourceInfo"))
    {
        D3DKMT_QUERYRESOURCEINFO* Query = static_cast<D3DKMT_QUERYRESOURCEINFO*>(Args);

        DtAppend(Text, " dev %s share %s", DtKernel(Query->hDevice).c_str(),
                 DtId(DtKindShare, Query->hGlobalShare).c_str());
    }
    else if (DtIs(Name, "D3DKMTOpenResource") || DtIs(Name, "D3DKMTOpenResource2"))
    {
        D3DKMT_OPENRESOURCE* Open = static_cast<D3DKMT_OPENRESOURCE*>(Args);

        DtAppend(Text, " dev %s share %s count %u runtime %u drv %u total %u", DtKernel(Open->hDevice).c_str(),
                 DtId(DtKindShare, Open->hGlobalShare).c_str(), Open->NumAllocations, Open->PrivateRuntimeDataSize,
                 Open->ResourcePrivateDriverDataSize, Open->TotalPrivateDriverDataBufferSize);
    }
    else if (DtIs(Name, "D3DKMTCreateDCFromMemory"))
    {
        D3DKMT_CREATEDCFROMMEMORY* Create = static_cast<D3DKMT_CREATEDCFROMMEMORY*>(Args);

        DtAppend(Text, " format %u %ux%u pitch %u device_dc %s colors %s", Create->Format, Create->Width,
                 Create->Height, Create->Pitch, Create->hDeviceDc != NULL ? "set" : "null",
                 Create->pColorTable != nullptr ? "set" : "null");
    }
    else if (DtIs(Name, "D3DKMTSetVidPnSourceOwner"))
    {
        D3DKMT_SETVIDPNSOURCEOWNER* Owner = static_cast<D3DKMT_SETVIDPNSOURCEOWNER*>(Args);

        DtAppend(Text, " dev %s count %u", DtKernel(Owner->hDevice).c_str(), Owner->VidPnSourceCount);
        if (Owner->VidPnSourceCount != 0 && Owner->pType != nullptr)
            DtAppend(Text, " type %u", Owner->pType[0]);
    }
    else if (DtIs(Name, "D3DKMTGetDeviceState"))
    {
        D3DKMT_GETDEVICESTATE* State = static_cast<D3DKMT_GETDEVICESTATE*>(Args);

        DtAppend(Text, " dev %s type %u", DtKernel(State->hDevice).c_str(), State->StateType);
    }
    else if (DtIs(Name, "D3DKMTCreateSynchronizationObject2"))
    {
        D3DKMT_CREATESYNCHRONIZATIONOBJECT2* Create = static_cast<D3DKMT_CREATESYNCHRONIZATIONOBJECT2*>(Args);

        DtAppend(Text, " dev %s type %u", DtKernel(Create->hDevice).c_str(), Create->Info.Type);
    }
    else if (DtIs(Name, "D3DKMTDestroySynchronizationObject"))
    {
        DtAppend(Text, " %s",
                 DtKernel(static_cast<D3DKMT_DESTROYSYNCHRONIZATIONOBJECT*>(Args)->hSyncObject).c_str());
    }
    else if (DtIs(Name, "D3DKMTWaitForSynchronizationObjectFromCpu"))
    {
        D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU* Wait =
            static_cast<D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU*>(Args);

        DtAppend(Text, " dev %s count %u", DtKernel(Wait->hDevice).c_str(), Wait->ObjectCount);
        DtHandleList(Text, Wait->ObjectHandleArray, Wait->ObjectCount);
        DtAppend(Text, " event %s flags %08X", Wait->hAsyncEvent != NULL ? "set" : "null", Wait->Flags.Value);
    }
    else if (DtIs(Name, "D3DKMTCreatePagingQueue"))
    {
        D3DKMT_CREATEPAGINGQUEUE* Create = static_cast<D3DKMT_CREATEPAGINGQUEUE*>(Args);

        DtAppend(Text, " dev %s prio %d node %u", DtKernel(Create->hDevice).c_str(), Create->Priority,
                 Create->PhysicalAdapterIndex);
    }
    else if (DtIs(Name, "D3DKMTDestroyPagingQueue"))
    {
        DtAppend(Text, " %s", DtKernel(static_cast<D3DDDI_DESTROYPAGINGQUEUE*>(Args)->hPagingQueue).c_str());
    }
    else if (DtIs(Name, "D3DKMTMakeResident"))
    {
        D3DDDI_MAKERESIDENT* Resident = static_cast<D3DDDI_MAKERESIDENT*>(Args);

        DtAppend(Text, " queue %s count %u flags %08X", DtKernel(Resident->hPagingQueue).c_str(),
                 Resident->NumAllocations, Resident->Flags.Value);
        DtHandleList(Text, Resident->AllocationList, Resident->NumAllocations);
    }
    else if (DtIs(Name, "D3DKMTEvict"))
    {
        D3DKMT_EVICT* Evict = static_cast<D3DKMT_EVICT*>(Args);

        DtAppend(Text, " dev %s count %u flags %08X", DtKernel(Evict->hDevice).c_str(), Evict->NumAllocations,
                 Evict->Flags.Value);
        DtHandleList(Text, Evict->AllocationList, Evict->NumAllocations);
    }
    else if (DtIs(Name, "D3DKMTInvalidateCache"))
    {
        D3DKMT_INVALIDATECACHE* Invalidate = static_cast<D3DKMT_INVALIDATECACHE*>(Args);

        DtAppend(Text, " dev %s %s offset %Iu length %Iu", DtKernel(Invalidate->hDevice).c_str(),
                 DtKernel(Invalidate->hAllocation).c_str(), Invalidate->Offset, Invalidate->Length);
    }
    else if (DtIs(Name, "D3DKMTGetScanLine"))
    {
        D3DKMT_GETSCANLINE* Scan = static_cast<D3DKMT_GETSCANLINE*>(Args);

        DtAppend(Text, " %s src %u", DtKernel(Scan->hAdapter).c_str(), Scan->VidPnSourceId);
    }
    else if (DtIs(Name, "D3DKMTWaitForVerticalBlankEvent"))
    {
        D3DKMT_WAITFORVERTICALBLANKEVENT* Wait = static_cast<D3DKMT_WAITFORVERTICALBLANKEVENT*>(Args);

        DtAppend(Text, " %s dev %s src %u", DtKernel(Wait->hAdapter).c_str(), DtKernel(Wait->hDevice).c_str(),
                 Wait->VidPnSourceId);
    }
    else if (DtIs(Name, "D3DKMTSharedPrimaryLockNotification") || DtIs(Name, "D3DKMTSharedPrimaryUnLockNotification"))
    {
        D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION* Notify = static_cast<D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION*>(Args);

        DtAppend(Text, " src %u ", Notify->VidPnSourceId);
        DtAppendRect(Text, reinterpret_cast<const RECT*>(&Notify->LockRect));
    }
}

/* What the kernel hands back, and which handles stop meaning anything */
static
VOID
DtOutputs(
    _In_z_ PCSTR Name,
    _In_ VOID* Args,
    _In_ NTSTATUS Status,
    _Inout_ std::string* Text)
{
    DtAppend(Text, "%08lX", Status);
    if (DtIs(Name, "D3DKMTDestroyAllocation"))
    {
        D3DKMT_DESTROYALLOCATION* Destroy = static_cast<D3DKMT_DESTROYALLOCATION*>(Args);

        DtForget(Destroy->hResource);
        for (UINT Index = 0; Destroy->phAllocationList != nullptr && Index < Destroy->AllocationCount; Index++)
            DtForget(Destroy->phAllocationList[Index]);
        return;
    }
    if (DtIs(Name, "D3DKMTDestroyAllocation2"))
    {
        D3DKMT_DESTROYALLOCATION2* Destroy = static_cast<D3DKMT_DESTROYALLOCATION2*>(Args);

        DtForget(Destroy->hResource);
        for (UINT Index = 0; Destroy->phAllocationList != nullptr && Index < Destroy->AllocationCount; Index++)
            DtForget(Destroy->phAllocationList[Index]);
        return;
    }
    if (DtIs(Name, "D3DKMTDestroyDevice"))
    {
        DtForget(static_cast<D3DKMT_DESTROYDEVICE*>(Args)->hDevice);
        return;
    }
    if (DtIs(Name, "D3DKMTDestroyContext"))
    {
        DtForget(static_cast<D3DKMT_DESTROYCONTEXT*>(Args)->hContext);
        return;
    }
    if (DtIs(Name, "D3DKMTCloseAdapter"))
    {
        DtForget(static_cast<D3DKMT_CLOSEADAPTER*>(Args)->hAdapter);
        return;
    }
    if (!NT_SUCCESS(Status))
        return;

    if (DtIs(Name, "D3DKMTOpenAdapterFromHdc"))
    {
        D3DKMT_OPENADAPTERFROMHDC* Open = static_cast<D3DKMT_OPENADAPTERFROMHDC*>(Args);

        DtAppend(Text, " %s src %u", DtKernel(Open->hAdapter).c_str(), Open->VidPnSourceId);
    }
    else if (DtIs(Name, "D3DKMTCreateDevice"))
    {
        D3DKMT_CREATEDEVICE* Create = static_cast<D3DKMT_CREATEDEVICE*>(Args);

        DtAppend(Text, " %s commands %u allocs %u patches %u", DtKernel(Create->hDevice).c_str(),
                 Create->CommandBufferSize, Create->AllocationListSize, Create->PatchLocationListSize);
    }
    else if (DtIs(Name, "D3DKMTCreateContext"))
    {
        D3DKMT_CREATECONTEXT* Create = static_cast<D3DKMT_CREATECONTEXT*>(Args);

        DtAppend(Text, " %s commands %u allocs %u patches %u", DtKernel(Create->hContext).c_str(),
                 Create->CommandBufferSize, Create->AllocationListSize, Create->PatchLocationListSize);
    }
    else if (DtIs(Name, "D3DKMTCreateContextVirtual"))
    {
        DtAppend(Text, " %s", DtKernel(static_cast<D3DKMT_CREATECONTEXTVIRTUAL*>(Args)->hContext).c_str());
    }
    else if (DtIs(Name, "D3DKMTCreateAllocation") || DtIs(Name, "D3DKMTCreateAllocation2"))
    {
        D3DKMT_CREATEALLOCATION* Create = static_cast<D3DKMT_CREATEALLOCATION*>(Args);

        DtAppend(Text, " res %s alloc", DtKernel(Create->hResource).c_str());
        DtAllocationHandles(Text, Create->pAllocationInfo2, Create->NumAllocations);
    }
    else if (DtIs(Name, "D3DKMTLock"))
    {
        Text->append(static_cast<D3DKMT_LOCK*>(Args)->pData != nullptr ? " data" : " nodata");
    }
    else if (DtIs(Name, "D3DKMTRender"))
    {
        D3DKMT_RENDER* Render = static_cast<D3DKMT_RENDER*>(Args);

        DtAppend(Text, " commands %u allocs %u patches %u", Render->NewCommandBufferSize,
                 Render->NewAllocationListSize, Render->NewPatchLocationListSize);
    }
    else if (DtIs(Name, "D3DKMTGetSharedPrimaryHandle"))
    {
        DtAppend(Text, " %s", DtId(DtKindShare, static_cast<D3DKMT_GETSHAREDPRIMARYHANDLE*>(Args)->hSharedPrimary)
                                  .c_str());
    }
    else if (DtIs(Name, "D3DKMTQueryResourceInfo"))
    {
        D3DKMT_QUERYRESOURCEINFO* Query = static_cast<D3DKMT_QUERYRESOURCEINFO*>(Args);

        DtAppend(Text, " runtime %u drv %u total %u count %u", Query->PrivateRuntimeDataSize,
                 Query->ResourcePrivateDriverDataSize, Query->TotalPrivateDriverDataSize, Query->NumAllocations);
    }
    else if (DtIs(Name, "D3DKMTOpenResource") || DtIs(Name, "D3DKMTOpenResource2"))
    {
        DtAppend(Text, " res %s", DtKernel(static_cast<D3DKMT_OPENRESOURCE*>(Args)->hResource).c_str());
    }
    else if (DtIs(Name, "D3DKMTCreateDCFromMemory"))
    {
        Text->append(static_cast<D3DKMT_CREATEDCFROMMEMORY*>(Args)->hDc != NULL ? " dc" : " nodc");
    }
    else if (DtIs(Name, "D3DKMTCreateSynchronizationObject2"))
    {
        DtAppend(Text, " %s",
                 DtKernel(static_cast<D3DKMT_CREATESYNCHRONIZATIONOBJECT2*>(Args)->hSyncObject).c_str());
    }
    else if (DtIs(Name, "D3DKMTCreatePagingQueue"))
    {
        D3DKMT_CREATEPAGINGQUEUE* Create = static_cast<D3DKMT_CREATEPAGINGQUEUE*>(Args);

        DtAppend(Text, " %s sync %s", DtKernel(Create->hPagingQueue).c_str(), DtKernel(Create->hSyncObject).c_str());
    }
}

static
NTSTATUS
DtDispatch(
    _In_ UINT Slot,
    _Inout_ VOID* Args)
{
    BOOLEAN Log = GetCurrentThreadId() == Dt.MainThread;
    PCSTR Name = Dt.Names[Slot];
    std::string Inputs;
    std::string Outputs;
    NTSTATUS Status;

    if (Log)
        DtInputs(Name, Args, &Inputs);
    Status = Dt.Originals[Slot](Args);
    if (!Log)
        return Status;
    DtOutputs(Name, Args, Status, &Outputs);
    printf("%s%s = %s\n", Name, Inputs.c_str(), Outputs.c_str());
    return Status;
}

template <UINT Slot>
static
NTSTATUS
APIENTRY
DtThunk(
    _Inout_ VOID* Args)
{
    return DtDispatch(Slot, Args);
}

template <UINT... Slots>
static
const PDT_KERNEL*
DtThunkTable(
    _In_ std::integer_sequence<UINT, Slots...>)
{
    static const PDT_KERNEL Table[] = { &DtThunk<Slots>... };
    return Table;
}

/* The wrapper standing in for one kernel thunk, given a slot the first time the name shows up */
static
PDT_KERNEL
DtWrapperFor(
    _In_z_ PCSTR Name,
    _In_ PDT_KERNEL Original)
{
    static const PDT_KERNEL* Thunks = DtThunkTable(std::make_integer_sequence<UINT, DtSlotLimit>());
    UINT Slot;

    for (Slot = 0; Slot < Dt.SlotCount; Slot++)
    {
        if (strcmp(Dt.Names[Slot], Name) == 0)
            return Thunks[Slot];
    }
    if (Dt.SlotCount == DtSlotLimit)
        return Original;

    Dt.Names[Slot] = _strdup(Name);
    Dt.Originals[Slot] = Original;
    Dt.SlotCount += 1;
    return Thunks[Slot];
}

static
FARPROC
WINAPI
DtGetProcAddress(
    _In_ HMODULE Module,
    _In_ LPCSTR Name)
{
    FARPROC Found = Dt.GetProcAddress(Module, Name);

    if (Found == nullptr || IS_INTRESOURCE(Name) || strncmp(Name, "D3DKMT", 6) != 0)
        return Found;
    return reinterpret_cast<FARPROC>(DtWrapperFor(Name, reinterpret_cast<PDT_KERNEL>(Found)));
}

static
VOID
DtWriteImport(
    _Inout_ ULONG_PTR* Entry,
    _In_ ULONG_PTR Value)
{
    DWORD Old;

    if (!VirtualProtect(Entry, sizeof(*Entry), PAGE_READWRITE, &Old))
        return;
    *Entry = Value;
    VirtualProtect(Entry, sizeof(*Entry), Old, &Old);
}

/* Points the runtime's GetProcAddress and D3DKMT imports at the wrappers */
static
UINT
DtPatchImports(
    _In_ HMODULE Module)
{
    BYTE* Base = reinterpret_cast<BYTE*>(Module);
    const IMAGE_DOS_HEADER* Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Base);
    const IMAGE_NT_HEADERS* Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(Base + Dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY* Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const IMAGE_IMPORT_DESCRIPTOR* Import;
    UINT Patched = 0;

    if (Directory->VirtualAddress == 0)
        return 0;

    for (Import = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(Base + Directory->VirtualAddress);
         Import->Name != 0; Import++)
    {
        const IMAGE_THUNK_DATA* Names;
        IMAGE_THUNK_DATA* Addresses;

        if (Import->OriginalFirstThunk == 0)
            continue;
        Names = reinterpret_cast<const IMAGE_THUNK_DATA*>(Base + Import->OriginalFirstThunk);
        Addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Import->FirstThunk);
        for (; Names->u1.AddressOfData != 0; Names++, Addresses++)
        {
            const IMAGE_IMPORT_BY_NAME* ByName;
            PDT_KERNEL Wrapper;

            if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
                continue;
            ByName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(Base + Names->u1.AddressOfData);
            if (strcmp(ByName->Name, "GetProcAddress") == 0)
            {
                Dt.GetProcAddress = reinterpret_cast<decltype(Dt.GetProcAddress)>(Addresses->u1.Function);
                DtWriteImport(&Addresses->u1.Function, reinterpret_cast<ULONG_PTR>(DtGetProcAddress));
                Patched += 1;
                continue;
            }
            if (strncmp(ByName->Name, "D3DKMT", 6) != 0)
                continue;
            Wrapper = DtWrapperFor(ByName->Name, reinterpret_cast<PDT_KERNEL>(Addresses->u1.Function));
            DtWriteImport(&Addresses->u1.Function, reinterpret_cast<ULONG_PTR>(Wrapper));
            Patched += 1;
        }
    }
    return Patched;
}

/* Whether the display's driver takes the newer allocation descriptions, asked before anything is traced */
static
BOOLEAN
DtDisplayIsWddm2(VOID)
{
    D3DKMT_OPENADAPTERFROMHDC Open = {};
    D3DKMT_QUERYADAPTERINFO Query = {};
    D3DKMT_CLOSEADAPTER Close = {};
    D3DKMT_DRIVERVERSION Version = KMT_DRIVERVERSION_WDDM_1_0;

    Open.hDc = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
    if (Open.hDc == NULL)
        return FALSE;
    if (NT_SUCCESS(D3DKMTOpenAdapterFromHdc(&Open)))
    {
        Query.hAdapter = Open.hAdapter;
        Query.Type = KMTQAITYPE_DRIVERVERSION;
        Query.pPrivateDriverData = &Version;
        Query.PrivateDriverDataSize = sizeof(Version);
        D3DKMTQueryAdapterInfo(&Query);
        Close.hAdapter = Open.hAdapter;
        D3DKMTCloseAdapter(&Close);
    }
    DeleteDC(Open.hDc);
    return Version >= KMT_DRIVERVERSION_WDDM_2_0;
}

static
VOID
DtPhase(
    _In_z_ PCSTR Name)
{
    printf("== %s\n", Name);
}

static
VOID
DtApi(
    _In_z_ PCSTR Name,
    _In_ HRESULT Result)
{
    printf("api %s = %08lX\n", Name, Result);
}

/* A small top level window the presents go to */
static
HWND
DtCreateWindow(VOID)
{
    WNDCLASSW Info = {};
    RECT Rect = { 0, 0, 64, 48 };
    HWND Window;

    Info.lpfnWndProc = DefWindowProcW;
    Info.hInstance = GetModuleHandleW(nullptr);
    Info.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    Info.lpszClassName = L"DtWindow";
    RegisterClassW(&Info);

    AdjustWindowRect(&Rect, WS_OVERLAPPEDWINDOW, FALSE);
    Window = CreateWindowExW(0, L"DtWindow", L"ddrawtrace", WS_OVERLAPPEDWINDOW, 40, 40, Rect.right - Rect.left,
                             Rect.bottom - Rect.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (Window != NULL)
    {
        ShowWindow(Window, SW_SHOWNOACTIVATE);
        UpdateWindow(Window);
    }
    return Window;
}

static
LPDIRECTDRAWSURFACE7
DtMakeSurface(
    _In_ LPDIRECTDRAW7 Draw7,
    _In_ DWORD Caps,
    _In_ DWORD Width,
    _In_ DWORD Height)
{
    LPDIRECTDRAWSURFACE7 Surface = nullptr;
    DDSURFACEDESC2 Desc = {};
    HRESULT hr;

    Desc.dwSize = sizeof(Desc);
    Desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    Desc.ddsCaps.dwCaps = Caps;
    Desc.dwWidth = Width;
    Desc.dwHeight = Height;
    Desc.ddpfPixelFormat.dwSize = sizeof(Desc.ddpfPixelFormat);
    Desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    Desc.ddpfPixelFormat.dwRGBBitCount = 32;
    Desc.ddpfPixelFormat.dwRBitMask = 0xFF0000;
    Desc.ddpfPixelFormat.dwGBitMask = 0xFF00;
    Desc.ddpfPixelFormat.dwBBitMask = 0xFF;
    hr = Draw7->CreateSurface(&Desc, &Surface, nullptr);
    DtApi("CreateSurface", hr);
    return SUCCEEDED(hr) ? Surface : nullptr;
}

static
VOID
DtLockCycle(
    _In_ LPDIRECTDRAWSURFACE7 Surface,
    _In_ DWORD Flags)
{
    DDSURFACEDESC2 Desc = {};
    HRESULT hr;

    Desc.dwSize = sizeof(Desc);
    hr = Surface->Lock(nullptr, &Desc, Flags, NULL);
    DtApi("Lock", hr);
    if (SUCCEEDED(hr))
    {
        memset(Desc.lpSurface, 0x5A, static_cast<SIZE_T>(Desc.lPitch) * Desc.dwHeight);
        DtApi("Unlock", Surface->Unlock(nullptr));
    }
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    std::wstring Runtime = L"ddraw.dll";
    LPDIRECTDRAWSURFACE7 Primary = nullptr;
    LPDIRECTDRAWSURFACE7 Video;
    LPDIRECTDRAWSURFACE7 Other;
    LPDIRECTDRAWSURFACE7 System;
    LPDIRECTDRAWCLIPPER Clipper = nullptr;
    LPDIRECTDRAW7 Draw7 = nullptr;
    PDT_CREATE_EX CreateEx;
    WCHAR Loaded[MAX_PATH] = {};
    DDSURFACEDESC2 Desc;
    HMODULE Module;
    DDBLTFX Fx;
    HWND Window;
    RECT Rect;
    HRESULT hr;
    HDC Dc;

    setvbuf(stdout, nullptr, _IONBF, 0);
    for (int Index = 1; Index + 1 < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") != 0)
            continue;
        Index += 1;
        if (_wcsicmp(argv[Index], L"reactx") == 0)
        {
            PWSTR Slash;

            GetModuleFileNameW(NULL, Loaded, _countof(Loaded));
            Slash = wcsrchr(Loaded, L'\\');
            if (Slash != nullptr)
                *(Slash + 1) = L'\0';
            Runtime = std::wstring(Loaded) + L"reactx\\ddraw.dll";
        }
        else if (_wcsicmp(argv[Index], L"system") != 0)
        {
            Runtime = argv[Index];
        }
    }

    Dt.MainThread = GetCurrentThreadId();
    Dt.Wddm2 = DtDisplayIsWddm2();
    Window = DtCreateWindow();
    Module = LoadLibraryW(Runtime.c_str());
    if (Module == NULL)
    {
        fprintf(stderr, "error: cannot load %ls (%lu)\n", Runtime.c_str(), GetLastError());
        return 1;
    }
    GetModuleFileNameW(Module, Loaded, _countof(Loaded) - 1);
    fprintf(stderr, "loaded %ls\n", Loaded);
    fprintf(stderr, "patched imports: %u\n", DtPatchImports(Module));
    CreateEx = reinterpret_cast<PDT_CREATE_EX>(GetProcAddress(Module, "DirectDrawCreateEx"));
    if (CreateEx == nullptr)
    {
        fprintf(stderr, "error: no DirectDrawCreateEx\n");
        return 1;
    }

    DtPhase("create");
    hr = CreateEx(nullptr, reinterpret_cast<LPVOID*>(&Draw7), IID_IDirectDraw7, nullptr);
    DtApi("DirectDrawCreateEx", hr);
    if (FAILED(hr))
        return 1;
    DtApi("SetCooperativeLevel", Draw7->SetCooperativeLevel(Window, DDSCL_NORMAL));

    DtPhase("surfaces");
    Video = DtMakeSurface(Draw7, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY, 64, 32);
    Other = DtMakeSurface(Draw7, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY, 64, 32);
    System = DtMakeSurface(Draw7, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 64, 32);
    if (Video == nullptr || Other == nullptr || System == nullptr)
        return 1;
    memset(&Desc, 0, sizeof(Desc));
    Desc.dwSize = sizeof(Desc);
    DtApi("GetSurfaceDesc", Video->GetSurfaceDesc(&Desc));
    DtLockCycle(Video, DDLOCK_WAIT);
    DtLockCycle(Video, DDLOCK_WAIT | DDLOCK_READONLY);
    DtLockCycle(System, DDLOCK_WAIT);
    hr = Video->GetDC(&Dc);
    DtApi("GetDC", hr);
    if (SUCCEEDED(hr))
        DtApi("ReleaseDC", Video->ReleaseDC(Dc));

    DtPhase("blits");
    memset(&Fx, 0, sizeof(Fx));
    Fx.dwSize = sizeof(Fx);
    Fx.dwFillColor = 0x00336699;
    DtApi("Blt fill", Other->Blt(nullptr, nullptr, nullptr, DDBLT_COLORFILL | DDBLT_WAIT, &Fx));
    DtApi("Blt video", Video->Blt(nullptr, Other, nullptr, DDBLT_WAIT, nullptr));
    SetRect(&Rect, 0, 0, 32, 16);
    DtApi("Blt stretch", Video->Blt(&Rect, Other, nullptr, DDBLT_WAIT, nullptr));
    DtApi("Blt upload", Video->Blt(nullptr, System, nullptr, DDBLT_WAIT, nullptr));
    DtApi("Blt download", System->Blt(nullptr, Video, nullptr, DDBLT_WAIT, nullptr));
    DtApi("BltFast", Video->BltFast(8, 8, Other, &Rect, DDBLTFAST_WAIT));
    DtLockCycle(Video, DDLOCK_WAIT | DDLOCK_READONLY);

    DtPhase("present");
    memset(&Desc, 0, sizeof(Desc));
    Desc.dwSize = sizeof(Desc);
    Desc.dwFlags = DDSD_CAPS;
    Desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = Draw7->CreateSurface(&Desc, &Primary, nullptr);
    DtApi("CreateSurface primary", hr);
    if (SUCCEEDED(hr))
    {
        hr = Draw7->CreateClipper(0, &Clipper, nullptr);
        DtApi("CreateClipper", hr);
        if (SUCCEEDED(hr))
        {
            DtApi("SetHWnd", Clipper->SetHWnd(0, Window));
            DtApi("SetClipper", Primary->SetClipper(Clipper));
        }
        GetClientRect(Window, &Rect);
        MapWindowPoints(Window, NULL, reinterpret_cast<POINT*>(&Rect), 2);
        DtApi("Blt to primary", Primary->Blt(&Rect, Video, nullptr, DDBLT_WAIT, nullptr));
        DtApi("Blt fill primary", Primary->Blt(&Rect, nullptr, nullptr, DDBLT_COLORFILL | DDBLT_WAIT, &Fx));
        DtApi("Blt system to primary", Primary->Blt(&Rect, System, nullptr, DDBLT_WAIT, nullptr));
        printf("api Release primary = %lu\n", Primary->Release());
        if (Clipper != nullptr)
            printf("api Release clipper = %lu\n", Clipper->Release());
    }

    DtPhase("release");
    printf("api Release system = %lu\n", System->Release());
    printf("api Release other = %lu\n", Other->Release());
    printf("api Release video = %lu\n", Video->Release());
    printf("api Release draw = %lu\n", Draw7->Release());
    DestroyWindow(Window);
    return 0;
}
