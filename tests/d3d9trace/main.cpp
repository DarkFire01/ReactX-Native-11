/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 9 runtime traffic to its user mode driver and the kernel, printed for comparison
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * d3d9trace [-runtime system|reactx|<path>] [-threads] [-ddi] [scenario ...]
 *
 * The runtime's imports of GetProcAddress and the D3DKMT thunks are pointed at wrappers inside this
 * process only. The wrapper for the driver's OpenAdapter wraps every adapter and device table the
 * driver hands back, so each DDI and callback is printed with its arguments. Handles print as ids in
 * the order they first appear: r for runtime handles, d for driver handles, k for kernel handles.
 * With -ddi the driver's callbacks and what they call are left out, since drivers time those themselves.
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>
#include <d3d9.h>
#include <d3dkmthk.h>
#include <d3dumddi.h>

#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

typedef UINT_PTR (APIENTRY* PTR_FORWARD)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR,
                                         UINT_PTR);
typedef HRESULT (WINAPI* PFN_TR_CREATE_EX)(UINT, IDirect3D9Ex**);
typedef IDirect3D9* (WINAPI* PFN_TR_CREATE)(UINT);

/* The tables the tracer wraps */
enum TrFamily
{
    TrFamilyAdapter,
    TrFamilyAdapterCallback,
    TrFamilyDevice,
    TrFamilyDeviceCallback,
    TrFamilyKernel,
    TrFamilyCount,
};

/* Handle kinds, each numbered on its own */
enum TrKind
{
    TrKindRuntime,
    TrKindDriver,
    TrKindKernel,
    TrKindCount,
};

static const PCSTR TrAdapterNames[] = { "GetCaps", "CreateDevice", "CloseAdapter" };

static const PCSTR TrAdapterCallbackNames[] =
{
    "QueryAdapterInfoCb", "GetMultisampleMethodListCb", "QueryAdapterInfoCb2", "QueryAdapterInfoCb3",
};

static const PCSTR TrDeviceNames[] =
{
    "SetRenderState", "UpdateWInfo", "ValidateDevice", "SetTextureStageState", "SetTexture", "SetPixelShader",
    "SetPixelShaderConst", "SetStreamSourceUm", "SetIndices", "SetIndicesUm", "DrawPrimitive",
    "DrawIndexedPrimitive", "DrawRectPatch", "DrawTriPatch", "DrawPrimitive2", "DrawIndexedPrimitive2", "VolBlt",
    "BufBlt", "TexBlt", "StateSet", "SetPriority", "Clear", "UpdatePalette", "SetPalette", "SetVertexShaderConst",
    "MultiplyTransform", "SetTransform", "SetViewport", "SetZRange", "SetMaterial", "SetLight", "CreateLight",
    "DestroyLight", "SetClipPlane", "GetInfo", "Lock", "Unlock", "CreateResource", "DestroyResource",
    "SetDisplayMode", "Present", "Flush", "CreateVertexShaderFunc", "DeleteVertexShaderFunc",
    "SetVertexShaderFunc", "CreateVertexShaderDecl", "DeleteVertexShaderDecl", "SetVertexShaderDecl",
    "SetVertexShaderConstI", "SetVertexShaderConstB", "SetScissorRect", "SetStreamSource", "SetStreamSourceFreq",
    "SetConvolutionKernelMono", "ComposeRects", "Blt", "ColorFill", "DepthFill", "CreateQuery", "DestroyQuery",
    "IssueQuery", "GetQueryData", "SetRenderTarget", "SetDepthStencil", "GenerateMipSubLevels",
    "SetPixelShaderConstI", "SetPixelShaderConstB", "CreatePixelShader", "DeletePixelShader", "CreateDecodeDevice",
    "DestroyDecodeDevice", "SetDecodeRenderTarget", "DecodeBeginFrame", "DecodeEndFrame", "DecodeExecute",
    "DecodeExtensionExecute", "CreateVideoProcessDevice", "DestroyVideoProcessDevice", "VideoProcessBeginFrame",
    "VideoProcessEndFrame", "SetVideoProcessRenderTarget", "VideoProcessBlt", "CreateExtensionDevice",
    "DestroyExtensionDevice", "ExtensionExecute", "CreateOverlay", "UpdateOverlay", "FlipOverlay",
    "GetOverlayColorControls", "SetOverlayColorControls", "DestroyOverlay", "DestroyDevice",
    "QueryResourceResidency", "OpenResource", "GetCaptureAllocationHandle", "CaptureToSysMem", "LockAsync",
    "UnlockAsync", "Rename", "CreateVideoProcessor", "SetVideoProcessBltState", "GetVideoProcessBltStatePrivate",
    "SetVideoProcessStreamState", "GetVideoProcessStreamStatePrivate", "VideoProcessBltHD",
    "DestroyVideoProcessor", "CreateAuthenticatedChannel", "AuthenticatedChannelKeyExchange",
    "QueryAuthenticatedChannel", "ConfigureAuthenticatedChannel", "DestroyAuthenticatedChannel",
    "CreateCryptoSession", "CryptoSessionKeyExchange", "DestroyCryptoSession", "EncryptionBlt", "GetPitch",
    "StartSessionKeyRefresh", "FinishSessionKeyRefresh", "GetEncryptionBltKey", "DecryptionBlt",
    "ResolveSharedResource", "VolBlt1", "BufBlt1", "TexBlt1", "Discard", "OfferResources", "ReclaimResources",
    "CheckDirectFlipSupport", "CreateResource2", "CheckMultiPlaneOverlaySupport", "PresentMultiPlaneOverlay",
    "Reserved1", "Flush1", "CheckCounterInfo", "CheckCounter", "UpdateSubresourceUP", "Present1",
    "CheckPresentDurationSupport", "SetMarker", "SetMarkerMode", "TrimResidencySet", "AcquireResource",
    "ReleaseResource",
};

static const PCSTR TrDeviceCallbackNames[] =
{
    "AllocateCb", "DeallocateCb", "SetPriorityCb", "QueryResidencyCb", "SetDisplayModeCb", "PresentCb", "RenderCb",
    "LockCb", "UnlockCb", "EscapeCb", "CreateOverlayCb", "UpdateOverlayCb", "FlipOverlayCb", "DestroyOverlayCb",
    "CreateContextCb", "DestroyContextCb", "CreateSynchronizationObjectCb", "DestroySynchronizationObjectCb",
    "WaitForSynchronizationObjectCb", "SignalSynchronizationObjectCb", "SetAsyncCallbacksCb",
    "SetDisplayPrivateDriverFormatCb", "OfferAllocationsCb", "ReclaimAllocationsCb",
    "CreateSynchronizationObject2Cb", "WaitForSynchronizationObject2Cb", "SignalSynchronizationObject2Cb",
    "PresentMultiPlaneOverlayCb", "LogUMDMarkerCb", "MakeResidentCb", "EvictCb",
    "WaitForSynchronizationObjectFromCpuCb", "SignalSynchronizationObjectFromCpuCb",
    "WaitForSynchronizationObjectFromGpuCb", "SignalSynchronizationObjectFromGpuCb", "CreatePagingQueueCb",
    "DestroyPagingQueueCb", "Lock2Cb", "Unlock2Cb", "InvalidateCacheCb", "ReserveGpuVirtualAddressCb",
    "MapGpuVirtualAddressCb", "FreeGpuVirtualAddressCb", "UpdateGpuVirtualAddressCb", "CreateContextVirtualCb",
    "SubmitCommandCb", "Deallocate2Cb", "SignalSynchronizationObjectFromGpu2Cb", "ReclaimAllocations2Cb",
    "GetResourcePresentPrivateDriverDataCb", "UpdateAllocationPropertyCb", "OfferAllocations2Cb",
    "ReclaimAllocations3Cb", "AcquireResourceCb", "ReleaseResourceCb", "CreateHwContextCb", "DestroyHwContextCb",
    "CreateHwQueueCb", "DestroyHwQueueCb", "SubmitCommandToHwQueueCb", "SubmitWaitForSyncObjectsToHwQueueCb",
    "SubmitSignalSyncObjectsToHwQueueCb", "SubmitPresentBltToHwQueueCb", "SubmitPresentToHwQueueCb",
    "SubmitHistorySequenceCb", "CreateNativeFenceCb",
};

/* Kernel thunks the runtime may call; any other D3DKMT import is left alone */
static const PCSTR TrKernelNames[] =
{
    "D3DKMTCreateDevice", "D3DKMTDestroyDevice", "D3DKMTCreateContext", "D3DKMTCreateContextVirtual",
    "D3DKMTDestroyContext", "D3DKMTSetContextSchedulingPriority", "D3DKMTGetContextSchedulingPriority",
    "D3DKMTSetQueuedLimit", "D3DKMTCreateAllocation", "D3DKMTCreateAllocation2", "D3DKMTDestroyAllocation",
    "D3DKMTDestroyAllocation2", "D3DKMTSetAllocationPriority", "D3DKMTPresent", "D3DKMTSubmitPresentToHwQueue",
    "D3DKMTLock", "D3DKMTUnlock", "D3DKMTLock2", "D3DKMTUnlock2", "D3DKMTEscape", "D3DKMTQueryAdapterInfo",
    "D3DKMTRender", "D3DKMTSubmitCommand", "D3DKMTGetDeviceState", "D3DKMTSetDisplayPrivateDriverFormat",
    "D3DKMTOpenResource", "D3DKMTOpenResource2", "D3DKMTQueryResourceInfo", "D3DKMTCreateSynchronizationObject",
    "D3DKMTCreateSynchronizationObject2", "D3DKMTDestroySynchronizationObject", "D3DKMTCreatePagingQueue",
    "D3DKMTDestroyPagingQueue", "D3DKMTMakeResident", "D3DKMTEvict", "D3DKMTOfferAllocations",
    "D3DKMTReclaimAllocations", "D3DKMTReclaimAllocations2", "D3DKMTCreateHwQueue", "D3DKMTDestroyHwQueue",
    "D3DKMTSetProcessSchedulingPriorityClass", "D3DKMTGetProcessSchedulingPriorityClass",
    "D3DKMTWaitForVerticalBlankEvent", "D3DKMTGetScanLine", "D3DKMTCheckOcclusion", "D3DKMTGetSharedPrimaryHandle",
    "D3DKMTSetVidPnSourceOwner", "D3DKMTSetVidPnSourceOwner1", "D3DKMTSetGammaRamp", "D3DKMTSetDisplayMode",
    "D3DKMTOpenAdapterFromHdc", "D3DKMTOpenAdapterFromLuid", "D3DKMTOpenAdapterFromGdiDisplayName",
    "D3DKMTOpenAdapterFromDeviceName", "D3DKMTCloseAdapter", "D3DKMTEnumAdapters", "D3DKMTEnumAdapters2",
    "D3DKMTGetDisplayModeList", "D3DKMTGetMultisampleMethodList", "D3DKMTCheckMonitorPowerState",
    "D3DKMTCheckExclusiveOwnership", "D3DKMTSetProperties", "D3DKMTQueryStatistics",
};

enum TrKernelCall
{
    TrKmtCreateDevice,
    TrKmtDestroyDevice,
    TrKmtCreateContext,
    TrKmtCreateContextVirtual,
    TrKmtDestroyContext,
    TrKmtSetContextSchedulingPriority,
    TrKmtGetContextSchedulingPriority,
    TrKmtSetQueuedLimit,
    TrKmtCreateAllocation,
    TrKmtCreateAllocation2,
    TrKmtDestroyAllocation,
    TrKmtDestroyAllocation2,
    TrKmtSetAllocationPriority,
    TrKmtPresent,
    TrKmtSubmitPresentToHwQueue,
    TrKmtLock,
    TrKmtUnlock,
    TrKmtLock2,
    TrKmtUnlock2,
    TrKmtEscape,
    TrKmtQueryAdapterInfo,
};

static constexpr UINT TrSlotLimit = 160;

static_assert(_countof(TrAdapterNames) == sizeof(D3DDDI_ADAPTERFUNCS) / sizeof(VOID*));
static_assert(_countof(TrAdapterCallbackNames) == sizeof(D3DDDI_ADAPTERCALLBACKS) / sizeof(VOID*));
static_assert(_countof(TrDeviceNames) == sizeof(D3DDDI_DEVICEFUNCS) / sizeof(VOID*));
static_assert(_countof(TrDeviceCallbackNames) == sizeof(D3DDDI_DEVICECALLBACKS) / sizeof(VOID*));
static_assert(_countof(TrDeviceNames) <= TrSlotLimit && _countof(TrKernelNames) <= TrSlotLimit);

#define TR_DEVICE_SLOT(Member) (offsetof(D3DDDI_DEVICEFUNCS, Member) / sizeof(VOID*))
#define TR_CALLBACK_SLOT(Member) (offsetof(D3DDDI_DEVICECALLBACKS, Member) / sizeof(VOID*))

/* Everything the wrappers share */
struct TrState
{
    DWORD MainThread;
    BOOLEAN AllThreads;
    BOOLEAN DdiOnly;
    BOOLEAN LineOpen;
    UINT Depth;
    UINT DriverVersion;
    PTR_FORWARD Originals[TrFamilyCount][TrSlotLimit];
    PFND3DDDI_OPENADAPTER OpenAdapter;
    FARPROC (WINAPI* GetProcAddress)(HMODULE, LPCSTR);
    std::map<ULONG_PTR, UINT> Ids[TrKindCount];
    UINT NextId[TrKindCount];
    std::map<ULONG_PTR, ULONG_PTR> RuntimeOfDriver;
    ULONG_PTR PendingRuntime;
    SRWLOCK IdLock;
};

static TrState Tr;

/* Set while a call runs that is not printed */
static thread_local BOOLEAN TrQuiet;

/* How deep the driver is in runtime callbacks on this thread */
static thread_local UINT TrCallbackDepth;

static
VOID
TrAppend(
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
TrId(
    _In_ TrKind Kind,
    _In_ ULONG_PTR Value)
{
    static const CHAR Prefix[] = { 'r', 'd', 'k' };
    std::map<ULONG_PTR, UINT>& Ids = Tr.Ids[Kind];
    std::string Text;
    UINT Id;

    if (Value == 0)
        return "0";

    /* Calls that are not printed must not use up ids */
    AcquireSRWLockExclusive(&Tr.IdLock);
    auto Found = Ids.find(Value);
    if (Found != Ids.end())
    {
        Id = Found->second;
    }
    else if (TrQuiet)
    {
        Id = 0;
    }
    else
    {
        Tr.NextId[Kind] += 1;
        Id = Tr.NextId[Kind];
        Ids.emplace(Value, Id);
    }
    ReleaseSRWLockExclusive(&Tr.IdLock);

    TrAppend(&Text, "%c%u", Prefix[Kind], Id);
    return Text;
}

static
VOID
TrForget(
    _In_ TrKind Kind,
    _In_ ULONG_PTR Value)
{
    AcquireSRWLockExclusive(&Tr.IdLock);
    Tr.Ids[Kind].erase(Value);
    ReleaseSRWLockExclusive(&Tr.IdLock);
}

static
std::string
TrRuntime(
    _In_opt_ const VOID* Handle)
{
    return TrId(TrKindRuntime, reinterpret_cast<ULONG_PTR>(Handle));
}

static
std::string
TrDriver(
    _In_opt_ const VOID* Handle)
{
    return TrId(TrKindDriver, reinterpret_cast<ULONG_PTR>(Handle));
}

static
std::string
TrKernel(
    _In_ ULONG_PTR Handle)
{
    return TrId(TrKindKernel, Handle);
}

static
UINT
TrBits(
    _In_ FLOAT Value)
{
    UINT Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

/* FNV-1a over bytes the runtime passes, so large blocks compare as one number */
static
UINT
TrHash(
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
TrAppendRect(
    _Inout_ std::string* Text,
    _In_ const RECT* Rect)
{
    TrAppend(Text, "(%ld,%ld,%ld,%ld)", Rect->left, Rect->top, Rect->right, Rect->bottom);
}

static
VOID
TrAppendBytes(
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
        TrAppend(Text, "%02X", Bytes[Index]);
}

static
BOOLEAN
TrShouldLog(VOID)
{
    if (Tr.DdiOnly && TrCallbackDepth != 0)
        return FALSE;
    return Tr.AllThreads || GetCurrentThreadId() == Tr.MainThread;
}

static
VOID
TrCloseLine(VOID)
{
    if (Tr.LineOpen)
    {
        fputc('\n', stdout);
        Tr.LineOpen = FALSE;
    }
}

static
VOID
TrPhase(
    _In_z_ PCSTR Name)
{
    TrCloseLine();
    printf("== %s\n", Name);
}

/* Opens a call line; nested calls start their own line under it */
static
VOID
TrBegin(
    _In_ const std::string& Text)
{
    if (GetCurrentThreadId() != Tr.MainThread)
        return;
    TrCloseLine();
    printf("%*s%s", static_cast<int>(Tr.Depth * 2), "", Text.c_str());
    Tr.LineOpen = TRUE;
    Tr.Depth += 1;
}

static
VOID
TrEnd(
    _In_ const std::string& Prefix,
    _In_ const std::string& Text)
{
    if (GetCurrentThreadId() != Tr.MainThread)
    {
        printf("[worker] %s = %s\n", Prefix.c_str(), Text.c_str());
        return;
    }
    Tr.Depth -= 1;
    if (Tr.LineOpen)
        printf(" = %s\n", Text.c_str());
    else
        printf("%*s= %s\n", static_cast<int>(Tr.Depth * 2), "", Text.c_str());
    Tr.LineOpen = FALSE;
}

static
UINT_PTR
TrDispatch(
    _In_ TrFamily Family,
    _In_ UINT Slot,
    _In_reads_(8) UINT_PTR* Arguments);

template <UINT Family, UINT Slot>
static
UINT_PTR
APIENTRY
TrThunk(
    _In_ UINT_PTR A0,
    _In_ UINT_PTR A1,
    _In_ UINT_PTR A2,
    _In_ UINT_PTR A3,
    _In_ UINT_PTR A4,
    _In_ UINT_PTR A5,
    _In_ UINT_PTR A6,
    _In_ UINT_PTR A7)
{
    UINT_PTR Arguments[8] = { A0, A1, A2, A3, A4, A5, A6, A7 };

    return TrDispatch(static_cast<TrFamily>(Family), Slot, Arguments);
}

template <UINT Family, UINT... Slots>
static
const PTR_FORWARD*
TrThunkTable(
    _In_ std::integer_sequence<UINT, Slots...>)
{
    static const PTR_FORWARD Table[] = { &TrThunk<Family, Slots>... };
    return Table;
}

static
const PTR_FORWARD*
TrThunks(
    _In_ TrFamily Family)
{
    static const PTR_FORWARD* Tables[TrFamilyCount] =
    {
        TrThunkTable<TrFamilyAdapter>(std::make_integer_sequence<UINT, TrSlotLimit>()),
        TrThunkTable<TrFamilyAdapterCallback>(std::make_integer_sequence<UINT, TrSlotLimit>()),
        TrThunkTable<TrFamilyDevice>(std::make_integer_sequence<UINT, TrSlotLimit>()),
        TrThunkTable<TrFamilyDeviceCallback>(std::make_integer_sequence<UINT, TrSlotLimit>()),
        TrThunkTable<TrFamilyKernel>(std::make_integer_sequence<UINT, TrSlotLimit>()),
    };
    return Tables[Family];
}

/* Saves each filled entry of a table and points it at the wrapper for its slot */
static
VOID
TrWrapTable(
    _In_ TrFamily Family,
    _Inout_updates_(Count) PTR_FORWARD* Table,
    _In_ UINT Count)
{
    const PTR_FORWARD* Thunks = TrThunks(Family);

    for (UINT Slot = 0; Slot < Count; Slot++)
    {
        if (Table[Slot] == nullptr || Table[Slot] == Thunks[Slot])
            continue;
        Tr.Originals[Family][Slot] = Table[Slot];
        Table[Slot] = Thunks[Slot];
    }
}

/* Names the slots a table leaves empty */
static
std::string
TrEmptySlots(
    _In_reads_(Count) const PTR_FORWARD* Table,
    _In_reads_(Count) const PCSTR* Names,
    _In_ UINT Count)
{
    std::string Text;

    for (UINT Slot = 0; Slot < Count; Slot++)
    {
        if (Table[Slot] != nullptr)
            continue;
        if (!Text.empty())
            Text.append(",");
        Text.append(Names[Slot]);
    }
    return Text.empty() ? "none" : Text;
}

static
VOID
TrDescribeResource(
    _Inout_ std::string* Text,
    _In_ const D3DDDIARG_CREATERESOURCE2* Args,
    _In_ BOOLEAN HasFlags2)
{
    Tr.PendingRuntime = reinterpret_cast<ULONG_PTR>(Args->hResource);
    TrAppend(Text, "fmt %u pool %u ms %u:%u mips %u fvf %08X src %u rate %u/%u res %s flags %08X rot %u",
             Args->Format, Args->Pool, Args->MultisampleType, Args->MultisampleQuality, Args->MipLevels, Args->Fvf,
             Args->VidPnSourceId, Args->RefreshRate.Numerator, Args->RefreshRate.Denominator,
             TrRuntime(Args->hResource).c_str(), Args->Flags.Value, Args->Rotation);
    if (HasFlags2)
        TrAppend(Text, " flags2 %08X", Args->Flags2.Value);
    TrAppend(Text, " surfs %u", Args->SurfCount);
    for (UINT Index = 0; Index < Args->SurfCount && Index < 16; Index++)
    {
        const D3DDDI_SURFACEINFO* Surface = &Args->pSurfList[Index];

        TrAppend(Text, " [%ux%ux%u %s %u %u]", Surface->Width, Surface->Height, Surface->Depth,
                 Surface->pSysMem != nullptr ? "mem" : "nomem", Surface->SysMemPitch, Surface->SysMemSlicePitch);
    }
}

/* Inputs of a device DDI */
static
VOID
TrDeviceInputs(
    _In_ UINT Slot,
    _In_reads_(8) const UINT_PTR* A,
    _Inout_ std::string* Text)
{
    const VOID* Args = reinterpret_cast<const VOID*>(A[1]);

    switch (Slot)
    {
    case TR_DEVICE_SLOT(pfnSetRenderState):
    {
        const D3DDDIARG_RENDERSTATE* State = static_cast<const D3DDDIARG_RENDERSTATE*>(Args);
        TrAppend(Text, " %u %08X", State->State, State->Value);
        break;
    }
    case TR_DEVICE_SLOT(pfnSetTextureStageState):
    {
        const D3DDDIARG_TEXTURESTAGESTATE* State = static_cast<const D3DDDIARG_TEXTURESTAGESTATE*>(Args);
        TrAppend(Text, " %u %u %08X", State->Stage, State->State, State->Value);
        break;
    }
    case TR_DEVICE_SLOT(pfnSetTexture):
        TrAppend(Text, " %u %s", static_cast<UINT>(A[1]), TrDriver(reinterpret_cast<VOID*>(A[2])).c_str());
        break;
    case TR_DEVICE_SLOT(pfnSetPixelShader):
    case TR_DEVICE_SLOT(pfnSetVertexShaderFunc):
    case TR_DEVICE_SLOT(pfnSetVertexShaderDecl):
    case TR_DEVICE_SLOT(pfnDeletePixelShader):
    case TR_DEVICE_SLOT(pfnDeleteVertexShaderFunc):
    case TR_DEVICE_SLOT(pfnDeleteVertexShaderDecl):
    case TR_DEVICE_SLOT(pfnDestroyResource):
    case TR_DEVICE_SLOT(pfnDestroyQuery):
        TrAppend(Text, " %s", TrDriver(Args).c_str());
        break;
    case TR_DEVICE_SLOT(pfnSetPixelShaderConst):
    case TR_DEVICE_SLOT(pfnSetVertexShaderConst):
    case TR_DEVICE_SLOT(pfnSetPixelShaderConstI):
    case TR_DEVICE_SLOT(pfnSetVertexShaderConstI):
    {
        const D3DDDIARG_SETVERTEXSHADERCONST* Constants = static_cast<const D3DDDIARG_SETVERTEXSHADERCONST*>(Args);
        const UINT* Values = reinterpret_cast<const UINT*>(A[2]);

        /* A few registers print in full; larger blocks as a hash */
        TrAppend(Text, " %u %u", Constants->Register, Constants->Count);
        if (Values != nullptr && Constants->Count <= 2)
        {
            for (UINT Index = 0; Index < Constants->Count * 4; Index++)
                TrAppend(Text, " %08X", Values[Index]);
        }
        else
        {
            TrAppend(Text, " %08X", TrHash(Values, Constants->Count * 16));
        }
        break;
    }
    case TR_DEVICE_SLOT(pfnSetPixelShaderConstB):
    case TR_DEVICE_SLOT(pfnSetVertexShaderConstB):
    {
        const D3DDDIARG_SETVERTEXSHADERCONST* Constants = static_cast<const D3DDDIARG_SETVERTEXSHADERCONST*>(Args);
        TrAppend(Text, " %u %u %08X", Constants->Register, Constants->Count,
                 TrHash(reinterpret_cast<VOID*>(A[2]), Constants->Count * sizeof(BOOL)));
        break;
    }
    case TR_DEVICE_SLOT(pfnSetStreamSource):
    {
        const D3DDDIARG_SETSTREAMSOURCE* Stream = static_cast<const D3DDDIARG_SETSTREAMSOURCE*>(Args);
        TrAppend(Text, " %u %s %u %u", Stream->Stream, TrDriver(Stream->hVertexBuffer).c_str(), Stream->Offset,
                 Stream->Stride);
        break;
    }
    case TR_DEVICE_SLOT(pfnSetStreamSourceUm):
    {
        const D3DDDIARG_SETSTREAMSOURCEUM* Stream = static_cast<const D3DDDIARG_SETSTREAMSOURCEUM*>(Args);
        TrAppend(Text, " %u %u", Stream->Stream, Stream->Stride);
        break;
    }
    case TR_DEVICE_SLOT(pfnSetIndices):
    {
        const D3DDDIARG_SETINDICES* Indices = static_cast<const D3DDDIARG_SETINDICES*>(Args);
        TrAppend(Text, " %s %u", TrDriver(Indices->hIndexBuffer).c_str(), Indices->Stride);
        break;
    }
    case TR_DEVICE_SLOT(pfnSetIndicesUm):
        TrAppend(Text, " %u", static_cast<UINT>(A[1]));
        break;
    case TR_DEVICE_SLOT(pfnDrawPrimitive):
    case TR_DEVICE_SLOT(pfnDrawPrimitive2):
    {
        const D3DDDIARG_DRAWPRIMITIVE* Draw = static_cast<const D3DDDIARG_DRAWPRIMITIVE*>(Args);
        TrAppend(Text, " %u %u %u", Draw->PrimitiveType, Draw->VStart, Draw->PrimitiveCount);
        break;
    }
    case TR_DEVICE_SLOT(pfnDrawIndexedPrimitive):
    case TR_DEVICE_SLOT(pfnDrawIndexedPrimitive2):
    {
        const D3DDDIARG_DRAWINDEXEDPRIMITIVE* Draw = static_cast<const D3DDDIARG_DRAWINDEXEDPRIMITIVE*>(Args);
        TrAppend(Text, " %u %d %u %u %u %u", Draw->PrimitiveType, Draw->BaseVertexIndex, Draw->MinIndex,
                 Draw->NumVertices, Draw->StartIndex, Draw->PrimitiveCount);
        break;
    }
    case TR_DEVICE_SLOT(pfnClear):
    {
        const D3DDDIARG_CLEAR* Clear = static_cast<const D3DDDIARG_CLEAR*>(Args);
        const RECT* Rects = reinterpret_cast<const RECT*>(A[3]);
        UINT Count = static_cast<UINT>(A[2]);

        TrAppend(Text, " flags %08X color %08X depth %08X stencil %u rects %u", Clear->Flags, Clear->FillColor,
                 TrBits(Clear->FillDepth), Clear->FillStencil, Count);
        for (UINT Index = 0; Rects != nullptr && Index < Count && Index < 8; Index++)
        {
            Text->append(" ");
            TrAppendRect(Text, &Rects[Index]);
        }
        break;
    }
    case TR_DEVICE_SLOT(pfnSetViewport):
    {
        const D3DDDIARG_VIEWPORTINFO* Viewport = static_cast<const D3DDDIARG_VIEWPORTINFO*>(Args);
        TrAppend(Text, " %u %u %u %u", Viewport->X, Viewport->Y, Viewport->Width, Viewport->Height);
        break;
    }
    case TR_DEVICE_SLOT(pfnSetZRange):
    case TR_DEVICE_SLOT(pfnUpdateWInfo):
    {
        const FLOAT* Range = static_cast<const FLOAT*>(Args);
        TrAppend(Text, " %08X %08X", TrBits(Range[0]), TrBits(Range[1]));
        break;
    }
    case TR_DEVICE_SLOT(pfnSetTransform):
    case TR_DEVICE_SLOT(pfnMultiplyTransform):
    {
        const D3DDDIARG_SETTRANSFORM* Transform = static_cast<const D3DDDIARG_SETTRANSFORM*>(Args);
        TrAppend(Text, " %u %08X", Transform->TransformType, TrHash(&Transform->Matrix, sizeof(Transform->Matrix)));
        break;
    }
    case TR_DEVICE_SLOT(pfnSetMaterial):
        TrAppend(Text, " %08X", TrHash(Args, sizeof(D3DDDIARG_SETMATERIAL)));
        break;
    case TR_DEVICE_SLOT(pfnSetLight):
    {
        const D3DDDIARG_SETLIGHT* Light = static_cast<const D3DDDIARG_SETLIGHT*>(Args);
        TrAppend(Text, " %u %u", Light->Index, Light->DataType);
        break;
    }
    case TR_DEVICE_SLOT(pfnCreateLight):
    case TR_DEVICE_SLOT(pfnDestroyLight):
        TrAppend(Text, " %u", *static_cast<const UINT*>(Args));
        break;
    case TR_DEVICE_SLOT(pfnSetClipPlane):
    {
        const D3DDDIARG_SETCLIPPLANE* Plane = static_cast<const D3DDDIARG_SETCLIPPLANE*>(Args);
        TrAppend(Text, " %u %08X", Plane->Index, TrHash(Plane->Plane, sizeof(Plane->Plane)));
        break;
    }
    case TR_DEVICE_SLOT(pfnLock):
    {
        const D3DDDIARG_LOCK* Lock = static_cast<const D3DDDIARG_LOCK*>(Args);
        TrAppend(Text, " %s sub %u flags %08X box %u %u %u %u %u %u", TrDriver(Lock->hResource).c_str(),
                 Lock->SubResourceIndex, Lock->Flags.Value, Lock->Box.Left, Lock->Box.Top, Lock->Box.Right,
                 Lock->Box.Bottom, Lock->Box.Front, Lock->Box.Back);
        break;
    }
    case TR_DEVICE_SLOT(pfnUnlock):
    {
        const D3DDDIARG_UNLOCK* Unlock = static_cast<const D3DDDIARG_UNLOCK*>(Args);
        TrAppend(Text, " %s sub %u flags %08X", TrDriver(Unlock->hResource).c_str(), Unlock->SubResourceIndex,
                 Unlock->Flags.Value);
        break;
    }
    case TR_DEVICE_SLOT(pfnCreateResource):
    {
        D3DDDIARG_CREATERESOURCE2 Wide = {};

        memcpy(&Wide, Args, sizeof(D3DDDIARG_CREATERESOURCE));
        Text->append(" ");
        TrDescribeResource(Text, &Wide, FALSE);
        break;
    }
    case TR_DEVICE_SLOT(pfnCreateResource2):
        Text->append(" ");
        TrDescribeResource(Text, static_cast<const D3DDDIARG_CREATERESOURCE2*>(Args), TRUE);
        break;
    case TR_DEVICE_SLOT(pfnPresent):
    {
        const D3DDDIARG_PRESENT* Present = static_cast<const D3DDDIARG_PRESENT*>(Args);
        TrAppend(Text, " src %s:%u dst %s:%u flags %08X interval %u", TrDriver(Present->hSrcResource).c_str(),
                 Present->SrcSubResourceIndex, TrDriver(Present->hDstResource).c_str(), Present->DstSubResourceIndex,
                 Present->Flags.Value, Present->FlipInterval);
        break;
    }
    case TR_DEVICE_SLOT(pfnPresent1):
    {
        const D3DDDIARG_PRESENT1* Present = static_cast<const D3DDDIARG_PRESENT1*>(Args);

        TrAppend(Text, " srcs %u", Present->SrcResources);
        for (UINT Index = 0; Index < Present->SrcResources && Index < 4; Index++)
        {
            TrAppend(Text, " %s:%u", TrDriver(Present->phSrcResources[Index].hResource).c_str(),
                     Present->phSrcResources[Index].SubResourceIndex);
        }
        TrAppend(Text, " dst %s:%u flags %08X interval %u dirty %u", TrDriver(Present->hDstResource).c_str(),
                 Present->DstSubResourceIndex, Present->Flags.Value, Present->FlipInterval, Present->DirtyRects);
        break;
    }
    case TR_DEVICE_SLOT(pfnFlush1):
        TrAppend(Text, " %08X", static_cast<UINT>(A[1]));
        break;
    case TR_DEVICE_SLOT(pfnCreateVertexShaderFunc):
    {
        const D3DDDIARG_CREATEVERTEXSHADERFUNC* Shader = static_cast<const D3DDDIARG_CREATEVERTEXSHADERFUNC*>(Args);
        TrAppend(Text, " %u %08X", Shader->Size, TrHash(reinterpret_cast<VOID*>(A[2]), Shader->Size));
        break;
    }
    case TR_DEVICE_SLOT(pfnCreateVertexShaderDecl):
    {
        const D3DDDIARG_CREATEVERTEXSHADERDECL* Decl = static_cast<const D3DDDIARG_CREATEVERTEXSHADERDECL*>(Args);
        TrAppend(Text, " %u %08X", Decl->NumVertexElements,
                 TrHash(reinterpret_cast<VOID*>(A[2]), Decl->NumVertexElements * sizeof(D3DDDIVERTEXELEMENT)));
        break;
    }
    case TR_DEVICE_SLOT(pfnCreatePixelShader):
    {
        const D3DDDIARG_CREATEPIXELSHADER* Shader = static_cast<const D3DDDIARG_CREATEPIXELSHADER*>(Args);
        TrAppend(Text, " %u %08X", Shader->CodeSize, TrHash(reinterpret_cast<VOID*>(A[2]), Shader->CodeSize));
        break;
    }
    case TR_DEVICE_SLOT(pfnSetScissorRect):
        Text->append(" ");
        TrAppendRect(Text, static_cast<const RECT*>(Args));
        break;
    case TR_DEVICE_SLOT(pfnSetStreamSourceFreq):
    {
        const D3DDDIARG_SETSTREAMSOURCEFREQ* Freq = static_cast<const D3DDDIARG_SETSTREAMSOURCEFREQ*>(Args);
        TrAppend(Text, " %u %08X", Freq->Stream, Freq->Divider);
        break;
    }
    case TR_DEVICE_SLOT(pfnBlt):
    {
        const D3DDDIARG_BLT* Blt = static_cast<const D3DDDIARG_BLT*>(Args);

        TrAppend(Text, " src %s:%u ", TrDriver(Blt->hSrcResource).c_str(), Blt->SrcSubResourceIndex);
        TrAppendRect(Text, &Blt->SrcRect);
        TrAppend(Text, " dst %s:%u ", TrDriver(Blt->hDstResource).c_str(), Blt->DstSubResourceIndex);
        TrAppendRect(Text, &Blt->DstRect);
        TrAppend(Text, " key %08X flags %08X", Blt->ColorKey, Blt->Flags.Value);
        break;
    }
    case TR_DEVICE_SLOT(pfnColorFill):
    {
        const D3DDDIARG_COLORFILL* Fill = static_cast<const D3DDDIARG_COLORFILL*>(Args);

        TrAppend(Text, " %s:%u ", TrDriver(Fill->hResource).c_str(), Fill->SubResourceIndex);
        TrAppendRect(Text, &Fill->DstRect);
        TrAppend(Text, " color %08X flags %08X", Fill->Color, Fill->Flags.Value);
        break;
    }
    case TR_DEVICE_SLOT(pfnDepthFill):
    {
        const D3DDDIARG_DEPTHFILL* Fill = static_cast<const D3DDDIARG_DEPTHFILL*>(Args);

        TrAppend(Text, " %s:%u ", TrDriver(Fill->hResource).c_str(), Fill->SubResourceIndex);
        TrAppendRect(Text, &Fill->DstRect);
        TrAppend(Text, " depth %08X", Fill->Depth);
        break;
    }
    case TR_DEVICE_SLOT(pfnCreateQuery):
        TrAppend(Text, " %u", static_cast<const D3DDDIARG_CREATEQUERY*>(Args)->QueryType);
        break;
    case TR_DEVICE_SLOT(pfnIssueQuery):
    {
        const D3DDDIARG_ISSUEQUERY* Issue = static_cast<const D3DDDIARG_ISSUEQUERY*>(Args);
        TrAppend(Text, " %s %08X", TrDriver(Issue->hQuery).c_str(), Issue->Flags.Value);
        break;
    }
    case TR_DEVICE_SLOT(pfnGetQueryData):
        TrAppend(Text, " %s", TrDriver(static_cast<const D3DDDIARG_GETQUERYDATA*>(Args)->hQuery).c_str());
        break;
    case TR_DEVICE_SLOT(pfnSetRenderTarget):
    {
        const D3DDDIARG_SETRENDERTARGET* Target = static_cast<const D3DDDIARG_SETRENDERTARGET*>(Args);
        TrAppend(Text, " %u %s:%u", Target->RenderTargetIndex, TrDriver(Target->hRenderTarget).c_str(),
                 Target->SubResourceIndex);
        break;
    }
    case TR_DEVICE_SLOT(pfnSetDepthStencil):
        TrAppend(Text, " %s", TrDriver(static_cast<const D3DDDIARG_SETDEPTHSTENCIL*>(Args)->hZBuffer).c_str());
        break;
    case TR_DEVICE_SLOT(pfnGenerateMipSubLevels):
    {
        const D3DDDIARG_GENERATEMIPSUBLEVELS* Mips = static_cast<const D3DDDIARG_GENERATEMIPSUBLEVELS*>(Args);
        TrAppend(Text, " %s %u", TrDriver(Mips->hResource).c_str(), Mips->Filter);
        break;
    }
    case TR_DEVICE_SLOT(pfnStateSet):
    {
        const D3DDDIARG_STATESET* Set = static_cast<const D3DDDIARG_STATESET*>(Args);
        TrAppend(Text, " %u %u %s", Set->Operation, Set->StateBlockType, TrDriver(Set->hStateSet).c_str());
        break;
    }
    case TR_DEVICE_SLOT(pfnSetPriority):
    {
        const D3DDDIARG_SETPRIORITY* Priority = static_cast<const D3DDDIARG_SETPRIORITY*>(Args);
        TrAppend(Text, " %s %08X", TrDriver(Priority->hResource).c_str(), Priority->Priority);
        break;
    }
    case TR_DEVICE_SLOT(pfnTexBlt1):
    {
        const D3DDDIARG_TEXBLT1* Blt = static_cast<const D3DDDIARG_TEXBLT1*>(Args);

        TrAppend(Text, " dst %s src %s face %u at %ld,%ld ", TrDriver(Blt->hDstResource).c_str(),
                 TrDriver(Blt->hSrcResource).c_str(), Blt->CubeMapFace, Blt->DstPoint.x, Blt->DstPoint.y);
        TrAppendRect(Text, &Blt->SrcRect);
        TrAppend(Text, " copy %08X", Blt->CopyFlags);
        break;
    }
    case TR_DEVICE_SLOT(pfnDiscard):
    {
        const D3DDDIARG_DISCARD* Discard = static_cast<const D3DDDIARG_DISCARD*>(Args);
        TrAppend(Text, " %s %u %u rects %u", TrDriver(Discard->hResource).c_str(), Discard->FirstSubResource,
                 Discard->NumSubResources, Discard->NumRects);
        break;
    }
    case TR_DEVICE_SLOT(pfnUpdateSubresourceUP):
    {
        const D3DDDIARG_UPDATESUBRESOURCEUP* Update = static_cast<const D3DDDIARG_UPDATESUBRESOURCEUP*>(Args);
        TrAppend(Text, " %s:%u box %u %u %u %u %u %u pitch %u %u flags %08X", TrDriver(Update->hResource).c_str(),
                 Update->SubResourceIndex, Update->DstBox.Left, Update->DstBox.Top, Update->DstBox.Right,
                 Update->DstBox.Bottom, Update->DstBox.Front, Update->DstBox.Back, Update->RowPitch,
                 Update->DepthPitch, Update->Flags.Value);
        break;
    }
    case TR_DEVICE_SLOT(pfnSetPalette):
    {
        const D3DDDIARG_SETPALETTE* Palette = static_cast<const D3DDDIARG_SETPALETTE*>(Args);
        TrAppend(Text, " %u %08X %s", Palette->PaletteHandle, Palette->PaletteFlags,
                 TrDriver(Palette->hResource).c_str());
        break;
    }
    case TR_DEVICE_SLOT(pfnUpdatePalette):
    {
        const D3DDDIARG_UPDATEPALETTE* Palette = static_cast<const D3DDDIARG_UPDATEPALETTE*>(Args);
        TrAppend(Text, " %u %u %u", Palette->PaletteHandle, Palette->StartIndex, Palette->NumEntries);
        break;
    }
    default:
        break;
    }
}

/* Outputs of a device DDI after it returned */
static
VOID
TrDeviceOutputs(
    _In_ UINT Slot,
    _In_reads_(8) const UINT_PTR* A,
    _In_ HRESULT Result,
    _Inout_ std::string* Text)
{
    VOID* Args = reinterpret_cast<VOID*>(A[1]);

    TrAppend(Text, "%08lX", Result);
    switch (Slot)
    {
    case TR_DEVICE_SLOT(pfnLock):
    {
        const D3DDDIARG_LOCK* Lock = static_cast<const D3DDDIARG_LOCK*>(Args);
        if (SUCCEEDED(Result))
            TrAppend(Text, " %s pitch %u %u", Lock->pSurfData != nullptr ? "mem" : "nomem", Lock->Pitch,
                     Lock->SlicePitch);
        break;
    }
    case TR_DEVICE_SLOT(pfnCreateResource):
    case TR_DEVICE_SLOT(pfnCreateResource2):
    {
        HANDLE Driver = Slot == TR_DEVICE_SLOT(pfnCreateResource) ?
                        static_cast<D3DDDIARG_CREATERESOURCE*>(Args)->hResource :
                        static_cast<D3DDDIARG_CREATERESOURCE2*>(Args)->hResource;
        if (SUCCEEDED(Result))
        {
            TrAppend(Text, " %s", TrDriver(Driver).c_str());
            Tr.RuntimeOfDriver[reinterpret_cast<ULONG_PTR>(Driver)] = Tr.PendingRuntime;
        }
        break;
    }
    case TR_DEVICE_SLOT(pfnDestroyResource):
    {
        auto Found = Tr.RuntimeOfDriver.find(A[1]);

        if (Found != Tr.RuntimeOfDriver.end())
        {
            TrForget(TrKindRuntime, Found->second);
            Tr.RuntimeOfDriver.erase(Found);
        }
        TrForget(TrKindDriver, A[1]);
        break;
    }
    case TR_DEVICE_SLOT(pfnDeletePixelShader):
    case TR_DEVICE_SLOT(pfnDeleteVertexShaderFunc):
    case TR_DEVICE_SLOT(pfnDeleteVertexShaderDecl):
    case TR_DEVICE_SLOT(pfnDestroyQuery):
        TrForget(TrKindDriver, A[1]);
        break;
    case TR_DEVICE_SLOT(pfnDestroyDevice):
        TrForget(TrKindDriver, A[0]);
        break;
    case TR_DEVICE_SLOT(pfnCreateVertexShaderFunc):
        TrAppend(Text, " %s", TrDriver(static_cast<D3DDDIARG_CREATEVERTEXSHADERFUNC*>(Args)->ShaderHandle).c_str());
        break;
    case TR_DEVICE_SLOT(pfnCreateVertexShaderDecl):
        TrAppend(Text, " %s", TrDriver(static_cast<D3DDDIARG_CREATEVERTEXSHADERDECL*>(Args)->ShaderHandle).c_str());
        break;
    case TR_DEVICE_SLOT(pfnCreatePixelShader):
        TrAppend(Text, " %s", TrDriver(static_cast<D3DDDIARG_CREATEPIXELSHADER*>(Args)->ShaderHandle).c_str());
        break;
    case TR_DEVICE_SLOT(pfnCreateQuery):
        TrAppend(Text, " %s", TrDriver(static_cast<D3DDDIARG_CREATEQUERY*>(Args)->hQuery).c_str());
        break;
    case TR_DEVICE_SLOT(pfnStateSet):
        TrAppend(Text, " %s", TrDriver(static_cast<D3DDDIARG_STATESET*>(Args)->hStateSet).c_str());
        break;
    case TR_DEVICE_SLOT(pfnPresent1):
        TrAppend(Text, " multiplicity %u", static_cast<D3DDDIARG_PRESENT1*>(Args)->BackBufferMultiplicity);
        break;
    default:
        break;
    }
}

static
VOID
TrDescribeAllocations(
    _Inout_ std::string* Text,
    _In_opt_ const VOID* Info,
    _In_ UINT Count)
{
    for (UINT Index = 0; Info != nullptr && Index < Count && Index < 8; Index++)
    {
        if (Tr.DriverVersion >= D3D_UMD_INTERFACE_VERSION_WDDM2_0)
        {
            const D3DDDI_ALLOCATIONINFO2* Entry = static_cast<const D3DDDI_ALLOCATIONINFO2*>(Info) + Index;
            TrAppend(Text, " [%s drv %u src %u flags %08X prio %08X]", Entry->pSystemMem != nullptr ? "mem" : "nomem",
                     Entry->PrivateDriverDataSize, Entry->VidPnSourceId, Entry->Flags.Value, Entry->Priority);
        }
        else
        {
            const D3DDDI_ALLOCATIONINFO* Entry = static_cast<const D3DDDI_ALLOCATIONINFO*>(Info) + Index;
            TrAppend(Text, " [%s drv %u src %u flags %08X]", Entry->pSystemMem != nullptr ? "mem" : "nomem",
                     Entry->PrivateDriverDataSize, Entry->VidPnSourceId, Entry->Flags.Value);
        }
    }
}

static
VOID
TrAllocationHandles(
    _Inout_ std::string* Text,
    _In_opt_ const VOID* Info,
    _In_ UINT Count)
{
    for (UINT Index = 0; Info != nullptr && Index < Count && Index < 8; Index++)
    {
        D3DKMT_HANDLE Handle = Tr.DriverVersion >= D3D_UMD_INTERFACE_VERSION_WDDM2_0 ?
                               (static_cast<const D3DDDI_ALLOCATIONINFO2*>(Info) + Index)->hAllocation :
                               (static_cast<const D3DDDI_ALLOCATIONINFO*>(Info) + Index)->hAllocation;
        TrAppend(Text, " %s", TrKernel(Handle).c_str());
    }
}

static
VOID
TrCallbackInputs(
    _In_ UINT Slot,
    _In_reads_(8) const UINT_PTR* A,
    _Inout_ std::string* Text)
{
    const VOID* Args = reinterpret_cast<const VOID*>(A[1]);

    switch (Slot)
    {
    case TR_CALLBACK_SLOT(pfnAllocateCb):
    {
        const D3DDDICB_ALLOCATE* Allocate = static_cast<const D3DDDICB_ALLOCATE*>(Args);
        TrAppend(Text, " res %s drv %u count %u", TrRuntime(Allocate->hResource).c_str(),
                 Allocate->PrivateDriverDataSize, Allocate->NumAllocations);
        TrDescribeAllocations(Text, Allocate->pAllocationInfo2, Allocate->NumAllocations);
        break;
    }
    case TR_CALLBACK_SLOT(pfnDeallocateCb):
    case TR_CALLBACK_SLOT(pfnDeallocate2Cb):
    {
        const D3DDDICB_DEALLOCATE* Free = static_cast<const D3DDDICB_DEALLOCATE*>(Args);
        TrAppend(Text, " res %s count %u", TrRuntime(Free->hResource).c_str(), Free->NumAllocations);
        for (UINT Index = 0; Free->HandleList != nullptr && Index < Free->NumAllocations && Index < 8; Index++)
            TrAppend(Text, " %s", TrKernel(Free->HandleList[Index]).c_str());
        if (Slot == TR_CALLBACK_SLOT(pfnDeallocate2Cb))
            TrAppend(Text, " flags %08X", static_cast<const D3DDDICB_DEALLOCATE2*>(Args)->Flags.Value);
        break;
    }
    case TR_CALLBACK_SLOT(pfnSetPriorityCb):
    {
        const D3DDDICB_SETPRIORITY* Priority = static_cast<const D3DDDICB_SETPRIORITY*>(Args);
        TrAppend(Text, " res %s count %u", TrRuntime(Priority->hResource).c_str(), Priority->NumAllocations);
        if (Priority->pPriorities != nullptr)
            TrAppend(Text, " prio %08X", Priority->pPriorities[0]);
        break;
    }
    case TR_CALLBACK_SLOT(pfnPresentCb):
    {
        const D3DDDICB_PRESENT* Present = static_cast<const D3DDDICB_PRESENT*>(Args);
        TrAppend(Text, " src %s dst %s ctx %s broadcast %u drv %u override %d:%u",
                 TrKernel(Present->hSrcAllocation).c_str(), TrKernel(Present->hDstAllocation).c_str(),
                 TrKernel(reinterpret_cast<ULONG_PTR>(Present->hContext)).c_str(), Present->BroadcastContextCount,
                 Present->PrivateDriverDataSize, Present->SyncIntervalOverrideValid, Present->SyncIntervalOverride);
        break;
    }
    case TR_CALLBACK_SLOT(pfnRenderCb):
    {
        const D3DDDICB_RENDER* Render = static_cast<const D3DDDICB_RENDER*>(Args);
        TrAppend(Text, " ctx %s flags %08X", TrKernel(reinterpret_cast<ULONG_PTR>(Render->hContext)).c_str(),
                 Render->Flags.Value);
        break;
    }
    case TR_CALLBACK_SLOT(pfnLockCb):
    {
        const D3DDDICB_LOCK* Lock = static_cast<const D3DDDICB_LOCK*>(Args);
        TrAppend(Text, " %s flags %08X", TrKernel(Lock->hAllocation).c_str(), Lock->Flags.Value);
        break;
    }
    case TR_CALLBACK_SLOT(pfnUnlockCb):
    {
        const D3DDDICB_UNLOCK* Unlock = static_cast<const D3DDDICB_UNLOCK*>(Args);
        TrAppend(Text, " count %u", Unlock->NumAllocations);
        for (UINT Index = 0; Unlock->phAllocations != nullptr && Index < Unlock->NumAllocations && Index < 8; Index++)
            TrAppend(Text, " %s", TrKernel(Unlock->phAllocations[Index]).c_str());
        break;
    }
    case TR_CALLBACK_SLOT(pfnLock2Cb):
    {
        const D3DDDICB_LOCK2* Lock = static_cast<const D3DDDICB_LOCK2*>(Args);
        TrAppend(Text, " %s flags %08X", TrKernel(Lock->hAllocation).c_str(), Lock->Flags.Value);
        break;
    }
    case TR_CALLBACK_SLOT(pfnUnlock2Cb):
        TrAppend(Text, " %s", TrKernel(static_cast<const D3DDDICB_UNLOCK2*>(Args)->hAllocation).c_str());
        break;
    case TR_CALLBACK_SLOT(pfnEscapeCb):
    {
        const D3DDDICB_ESCAPE* Escape = static_cast<const D3DDDICB_ESCAPE*>(Args);
        TrAppend(Text, " dev %s flags %08X size %u", TrRuntime(Escape->hDevice).c_str(), Escape->Flags.Value,
                 Escape->PrivateDriverDataSize);
        break;
    }
    case TR_CALLBACK_SLOT(pfnCreateContextCb):
    case TR_CALLBACK_SLOT(pfnCreateContextVirtualCb):
    {
        const D3DDDICB_CREATECONTEXTVIRTUAL* Context = static_cast<const D3DDDICB_CREATECONTEXTVIRTUAL*>(Args);
        TrAppend(Text, " node %u engine %u flags %08X drv %u", Context->NodeOrdinal, Context->EngineAffinity,
                 Context->Flags.Value, Context->PrivateDriverDataSize);
        break;
    }
    case TR_CALLBACK_SLOT(pfnSetDisplayPrivateDriverFormatCb):
    {
        const D3DDDICB_SETDISPLAYPRIVATEDRIVERFORMAT* Format =
            static_cast<const D3DDDICB_SETDISPLAYPRIVATEDRIVERFORMAT*>(Args);
        TrAppend(Text, " src %u attr %08X", Format->VidPnSourceId, Format->PrivateDriverFormatAttribute);
        break;
    }
    case TR_CALLBACK_SLOT(pfnSubmitCommandCb):
    {
        const D3DDDICB_SUBMITCOMMAND* Submit = static_cast<const D3DDDICB_SUBMITCOMMAND*>(Args);
        TrAppend(Text, " flags %08X broadcast %u primaries %u", Submit->Flags.Value, Submit->BroadcastContextCount,
                 Submit->NumPrimaries);
        break;
    }
    case TR_CALLBACK_SLOT(pfnSubmitPresentToHwQueueCb):
    {
        const D3DDDICB_SUBMITPRESENTTOHWQUEUE* Present = static_cast<const D3DDDICB_SUBMITPRESENTTOHWQUEUE*>(Args);
        TrAppend(Text, " queues %u drv %u override %d:%u", Present->BroadcastHwQueueCount,
                 Present->PrivateDriverDataSize, Present->SyncIntervalOverrideValid, Present->SyncIntervalOverride);
        if (Present->BroadcastSrcAllocations != nullptr && Present->BroadcastHwQueueCount != 0)
            TrAppend(Text, " src %s", TrKernel(Present->BroadcastSrcAllocations[0]).c_str());
        if (Present->BroadcastDstAllocations != nullptr && Present->BroadcastHwQueueCount != 0)
            TrAppend(Text, " dst %s", TrKernel(Present->BroadcastDstAllocations[0]).c_str());
        break;
    }
    default:
        break;
    }
}

static
VOID
TrCallbackOutputs(
    _In_ UINT Slot,
    _In_reads_(8) const UINT_PTR* A,
    _In_ HRESULT Result,
    _Inout_ std::string* Text)
{
    VOID* Args = reinterpret_cast<VOID*>(A[1]);

    TrAppend(Text, "%08lX", Result);
    if (FAILED(Result))
        return;

    switch (Slot)
    {
    case TR_CALLBACK_SLOT(pfnAllocateCb):
    {
        const D3DDDICB_ALLOCATE* Allocate = static_cast<const D3DDDICB_ALLOCATE*>(Args);
        TrAppend(Text, " kres %s alloc", TrKernel(Allocate->hKMResource).c_str());
        TrAllocationHandles(Text, Allocate->pAllocationInfo2, Allocate->NumAllocations);
        break;
    }
    case TR_CALLBACK_SLOT(pfnCreateContextCb):
        TrAppend(Text, " ctx %s",
                 TrKernel(reinterpret_cast<ULONG_PTR>(static_cast<D3DDDICB_CREATECONTEXT*>(Args)->hContext)).c_str());
        break;
    case TR_CALLBACK_SLOT(pfnCreateContextVirtualCb):
        TrAppend(Text, " ctx %s", TrKernel(reinterpret_cast<ULONG_PTR>(
                                      static_cast<D3DDDICB_CREATECONTEXTVIRTUAL*>(Args)->hContext)).c_str());
        break;
    default:
        break;
    }
}

static
VOID
TrKernelInputs(
    _In_ UINT Slot,
    _In_ VOID* Args,
    _Inout_ std::string* Text)
{
    switch (Slot)
    {
    case TrKmtCreateDevice:
    {
        const D3DKMT_CREATEDEVICE* Create = static_cast<const D3DKMT_CREATEDEVICE*>(Args);
        TrAppend(Text, " adapter %s flags %08X", TrKernel(Create->hAdapter).c_str(), *reinterpret_cast<const UINT*>(
                                                                                           &Create->Flags));
        break;
    }
    case TrKmtDestroyDevice:
        TrAppend(Text, " %s", TrKernel(static_cast<const D3DKMT_DESTROYDEVICE*>(Args)->hDevice).c_str());
        break;
    case TrKmtCreateContext:
    {
        const D3DKMT_CREATECONTEXT* Create = static_cast<const D3DKMT_CREATECONTEXT*>(Args);
        TrAppend(Text, " dev %s node %u engine %u flags %08X drv %u hint %u", TrKernel(Create->hDevice).c_str(),
                 Create->NodeOrdinal, Create->EngineAffinity, Create->Flags.Value, Create->PrivateDriverDataSize,
                 Create->ClientHint);
        break;
    }
    case TrKmtCreateContextVirtual:
    {
        const D3DKMT_CREATECONTEXTVIRTUAL* Create = static_cast<const D3DKMT_CREATECONTEXTVIRTUAL*>(Args);
        TrAppend(Text, " dev %s node %u engine %u flags %08X drv %u hint %u", TrKernel(Create->hDevice).c_str(),
                 Create->NodeOrdinal, Create->EngineAffinity, Create->Flags.Value, Create->PrivateDriverDataSize,
                 Create->ClientHint);
        break;
    }
    case TrKmtDestroyContext:
        TrAppend(Text, " %s", TrKernel(static_cast<const D3DKMT_DESTROYCONTEXT*>(Args)->hContext).c_str());
        break;
    case TrKmtSetContextSchedulingPriority:
    {
        const D3DKMT_SETCONTEXTSCHEDULINGPRIORITY* Priority =
            static_cast<const D3DKMT_SETCONTEXTSCHEDULINGPRIORITY*>(Args);
        TrAppend(Text, " %s %d", TrKernel(Priority->hContext).c_str(), Priority->Priority);
        break;
    }
    case TrKmtGetContextSchedulingPriority:
        TrAppend(Text, " %s",
                 TrKernel(static_cast<const D3DKMT_GETCONTEXTSCHEDULINGPRIORITY*>(Args)->hContext).c_str());
        break;
    case TrKmtSetQueuedLimit:
    {
        const D3DKMT_SETQUEUEDLIMIT* Limit = static_cast<const D3DKMT_SETQUEUEDLIMIT*>(Args);
        TrAppend(Text, " %s type %u", TrKernel(Limit->hDevice).c_str(), Limit->Type);
        if (Limit->Type == D3DKMT_SET_QUEUEDLIMIT_PRESENT)
            TrAppend(Text, " limit %u", Limit->QueuedPresentLimit);
        else
            TrAppend(Text, " src %u limit %u", Limit->VidPnSourceId, Limit->QueuedPendingFlipLimit);
        break;
    }
    case TrKmtCreateAllocation:
    case TrKmtCreateAllocation2:
    {
        const D3DKMT_CREATEALLOCATION* Create = static_cast<const D3DKMT_CREATEALLOCATION*>(Args);
        TrAppend(Text, " dev %s res %s flags %08X drv %u count %u runtime ", TrKernel(Create->hDevice).c_str(),
                 TrKernel(Create->hResource).c_str(), *reinterpret_cast<const UINT*>(&Create->Flags),
                 Create->PrivateDriverDataSize, Create->NumAllocations);
        TrAppendBytes(Text, Create->pPrivateRuntimeData, Create->PrivateRuntimeDataSize);
        TrDescribeAllocations(Text, Create->pAllocationInfo2, Create->NumAllocations);
        break;
    }
    case TrKmtDestroyAllocation:
    {
        const D3DKMT_DESTROYALLOCATION* Destroy = static_cast<const D3DKMT_DESTROYALLOCATION*>(Args);
        TrAppend(Text, " dev %s res %s count %u", TrKernel(Destroy->hDevice).c_str(),
                 TrKernel(Destroy->hResource).c_str(), Destroy->AllocationCount);
        break;
    }
    case TrKmtDestroyAllocation2:
    {
        const D3DKMT_DESTROYALLOCATION2* Destroy = static_cast<const D3DKMT_DESTROYALLOCATION2*>(Args);
        TrAppend(Text, " dev %s res %s count %u flags %08X", TrKernel(Destroy->hDevice).c_str(),
                 TrKernel(Destroy->hResource).c_str(), Destroy->AllocationCount, Destroy->Flags.Value);
        break;
    }
    case TrKmtSetAllocationPriority:
    {
        const D3DKMT_SETALLOCATIONPRIORITY* Priority = static_cast<const D3DKMT_SETALLOCATIONPRIORITY*>(Args);
        TrAppend(Text, " dev %s res %s count %u", TrKernel(Priority->hDevice).c_str(),
                 TrKernel(Priority->hResource).c_str(), Priority->AllocationCount);
        if (Priority->pPriorities != nullptr)
            TrAppend(Text, " prio %08X", Priority->pPriorities[0]);
        break;
    }
    case TrKmtPresent:
    {
        const D3DKMT_PRESENT* Present = static_cast<const D3DKMT_PRESENT*>(Args);

        TrAppend(Text, " ctx %s window %s src %u from %s to %s color %08X ", TrKernel(Present->hContext).c_str(),
                 Present->hWindow != NULL ? "set" : "null", Present->VidPnSourceId,
                 TrKernel(Present->hSource).c_str(), TrKernel(Present->hDestination).c_str(), Present->Color);
        TrAppendRect(Text, &Present->DstRect);
        Text->append(" ");
        TrAppendRect(Text, &Present->SrcRect);
        TrAppend(Text, " subrects %u interval %u flags %08X broadcast %u regions %s duration %u drv %u",
                 Present->SubRectCnt, Present->FlipInterval, Present->Flags.Value, Present->BroadcastContextCount,
                 Present->pPresentRegions != nullptr ? "set" : "null", Present->Duration,
                 Present->PrivateDriverDataSize);
        break;
    }
    case TrKmtLock:
    {
        const D3DKMT_LOCK* Lock = static_cast<const D3DKMT_LOCK*>(Args);
        TrAppend(Text, " dev %s %s flags %08X", TrKernel(Lock->hDevice).c_str(), TrKernel(Lock->hAllocation).c_str(),
                 Lock->Flags.Value);
        break;
    }
    case TrKmtUnlock:
    {
        const D3DKMT_UNLOCK* Unlock = static_cast<const D3DKMT_UNLOCK*>(Args);
        TrAppend(Text, " dev %s count %u", TrKernel(Unlock->hDevice).c_str(), Unlock->NumAllocations);
        break;
    }
    case TrKmtLock2:
    {
        const D3DKMT_LOCK2* Lock = static_cast<const D3DKMT_LOCK2*>(Args);
        TrAppend(Text, " dev %s %s flags %08X", TrKernel(Lock->hDevice).c_str(), TrKernel(Lock->hAllocation).c_str(),
                 Lock->Flags.Value);
        break;
    }
    case TrKmtUnlock2:
    {
        const D3DKMT_UNLOCK2* Unlock = static_cast<const D3DKMT_UNLOCK2*>(Args);
        TrAppend(Text, " dev %s %s", TrKernel(Unlock->hDevice).c_str(), TrKernel(Unlock->hAllocation).c_str());
        break;
    }
    case TrKmtEscape:
    {
        const D3DKMT_ESCAPE* Escape = static_cast<const D3DKMT_ESCAPE*>(Args);
        TrAppend(Text, " dev %s type %u flags %08X size %u ctx %s", TrKernel(Escape->hDevice).c_str(), Escape->Type,
                 Escape->Flags.Value, Escape->PrivateDriverDataSize, TrKernel(Escape->hContext).c_str());
        break;
    }
    case TrKmtQueryAdapterInfo:
    {
        const D3DKMT_QUERYADAPTERINFO* Query = static_cast<const D3DKMT_QUERYADAPTERINFO*>(Args);
        TrAppend(Text, " type %u size %u", Query->Type, Query->PrivateDriverDataSize);
        break;
    }
    default:
        if (strcmp(TrKernelNames[Slot], "D3DKMTSetVidPnSourceOwner1") == 0 ||
            strcmp(TrKernelNames[Slot], "D3DKMTSetVidPnSourceOwner") == 0)
        {
            const D3DKMT_SETVIDPNSOURCEOWNER* Owner = static_cast<const D3DKMT_SETVIDPNSOURCEOWNER*>(Args);

            TrAppend(Text, " dev %s count %u", TrKernel(Owner->hDevice).c_str(), Owner->VidPnSourceCount);
            if (Owner->VidPnSourceCount != 0 && Owner->pType != nullptr)
                TrAppend(Text, " type %u", Owner->pType[0]);
        }
        else if (strcmp(TrKernelNames[Slot], "D3DKMTGetDeviceState") == 0)
        {
            const D3DKMT_GETDEVICESTATE* State = static_cast<const D3DKMT_GETDEVICESTATE*>(Args);
            TrAppend(Text, " dev %s type %u", TrKernel(State->hDevice).c_str(), State->StateType);
        }
        break;
    }
}

static
VOID
TrKernelOutputs(
    _In_ UINT Slot,
    _In_ VOID* Args,
    _In_ NTSTATUS Status,
    _Inout_ std::string* Text)
{
    TrAppend(Text, "%08lX", Status);
    switch (Slot)
    {
    case TrKmtCreateDevice:
        if (NT_SUCCESS(Status))
            TrAppend(Text, " %s", TrKernel(static_cast<D3DKMT_CREATEDEVICE*>(Args)->hDevice).c_str());
        break;
    case TrKmtDestroyDevice:
        TrForget(TrKindKernel, static_cast<D3DKMT_DESTROYDEVICE*>(Args)->hDevice);
        break;
    case TrKmtCreateContext:
        if (NT_SUCCESS(Status))
            TrAppend(Text, " %s", TrKernel(static_cast<D3DKMT_CREATECONTEXT*>(Args)->hContext).c_str());
        break;
    case TrKmtCreateContextVirtual:
        if (NT_SUCCESS(Status))
            TrAppend(Text, " %s", TrKernel(static_cast<D3DKMT_CREATECONTEXTVIRTUAL*>(Args)->hContext).c_str());
        break;
    case TrKmtDestroyContext:
        TrForget(TrKindKernel, static_cast<D3DKMT_DESTROYCONTEXT*>(Args)->hContext);
        break;
    case TrKmtGetContextSchedulingPriority:
        if (NT_SUCCESS(Status))
            TrAppend(Text, " %d", static_cast<D3DKMT_GETCONTEXTSCHEDULINGPRIORITY*>(Args)->Priority);
        break;
    case TrKmtCreateAllocation:
    case TrKmtCreateAllocation2:
    {
        D3DKMT_CREATEALLOCATION* Create = static_cast<D3DKMT_CREATEALLOCATION*>(Args);
        if (NT_SUCCESS(Status))
        {
            TrAppend(Text, " res %s alloc", TrKernel(Create->hResource).c_str());
            TrAllocationHandles(Text, Create->pAllocationInfo2, Create->NumAllocations);
        }
        break;
    }
    case TrKmtDestroyAllocation:
    {
        D3DKMT_DESTROYALLOCATION* Destroy = static_cast<D3DKMT_DESTROYALLOCATION*>(Args);
        TrForget(TrKindKernel, Destroy->hResource);
        for (UINT Index = 0; Destroy->phAllocationList != nullptr && Index < Destroy->AllocationCount; Index++)
            TrForget(TrKindKernel, Destroy->phAllocationList[Index]);
        break;
    }
    case TrKmtDestroyAllocation2:
    {
        D3DKMT_DESTROYALLOCATION2* Destroy = static_cast<D3DKMT_DESTROYALLOCATION2*>(Args);
        TrForget(TrKindKernel, Destroy->hResource);
        for (UINT Index = 0; Destroy->phAllocationList != nullptr && Index < Destroy->AllocationCount; Index++)
            TrForget(TrKindKernel, Destroy->phAllocationList[Index]);
        break;
    }
    default:
        break;
    }
}

static
HRESULT
APIENTRY
TrOpenAdapter(
    _Inout_ D3DDDIARG_OPENADAPTER* Args);

static
UINT_PTR
TrCallOriginal(
    _In_ TrFamily Family,
    _In_ UINT Slot,
    _In_reads_(8) UINT_PTR* A)
{
    return Tr.Originals[Family][Slot](A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7]);
}

/* Adapter CreateDevice: the callbacks go in wrapped, the device table comes back wrapped */
static
HRESULT
TrCreateDevice(
    _In_reads_(8) UINT_PTR* A)
{
    D3DDDIARG_CREATEDEVICE* Args = reinterpret_cast<D3DDDIARG_CREATEDEVICE*>(A[1]);
    const D3DDDI_DEVICECALLBACKS* RuntimeCallbacks = Args->pCallbacks;
    D3DDDI_DEVICECALLBACKS* Callbacks = new D3DDDI_DEVICECALLBACKS(*RuntimeCallbacks);
    BOOLEAN Log = TrShouldLog();
    std::string Text;
    HRESULT hr;

    TrAppend(&Text, "CreateDevice %s dev %s interface %u version %08X flags %08X cmdbuf %u alloc %u patch %u",
             TrDriver(reinterpret_cast<VOID*>(A[0])).c_str(), TrRuntime(Args->hDevice).c_str(), Args->Interface,
             Args->Version, Args->Flags.Value, Args->CommandBufferSize, Args->AllocationListSize,
             Args->PatchLocationListSize);
    if (Log)
    {
        TrBegin(Text);
        TrCloseLine();
        printf("%*scallbacks_empty = %s\n", static_cast<int>(Tr.Depth * 2), "",
               TrEmptySlots(reinterpret_cast<const PTR_FORWARD*>(RuntimeCallbacks), TrDeviceCallbackNames,
                            _countof(TrDeviceCallbackNames)).c_str());
    }

    /* The driver may keep the table pointer, so the wrapped copy lives for the process */
    TrWrapTable(TrFamilyDeviceCallback, reinterpret_cast<PTR_FORWARD*>(Callbacks), _countof(TrDeviceCallbackNames));
    Args->pCallbacks = Callbacks;
    hr = static_cast<HRESULT>(TrCallOriginal(TrFamilyAdapter, 1, A));
    Args->pCallbacks = RuntimeCallbacks;
    TrQuiet = !Log;

    Text.clear();
    TrAppend(&Text, "%08lX", hr);
    if (SUCCEEDED(hr))
    {
        TrAppend(&Text, " %s", TrDriver(Args->hDevice).c_str());
        if (Log)
        {
            TrCloseLine();
            printf("%*sfuncs_empty = %s\n", static_cast<int>(Tr.Depth * 2), "",
                   TrEmptySlots(reinterpret_cast<const PTR_FORWARD*>(Args->pDeviceFuncs), TrDeviceNames,
                                _countof(TrDeviceNames)).c_str());
        }
        TrWrapTable(TrFamilyDevice, reinterpret_cast<PTR_FORWARD*>(Args->pDeviceFuncs), _countof(TrDeviceNames));
    }
    if (Log)
        TrEnd("CreateDevice", Text);
    return hr;
}

static
UINT_PTR
TrDispatch(
    _In_ TrFamily Family,
    _In_ UINT Slot,
    _In_reads_(8) UINT_PTR* A)
{
    BOOLEAN Log = TrShouldLog();
    BOOLEAN WasQuiet;
    std::string Name;
    std::string Inputs;
    std::string Text;
    UINT_PTR Result;

    if (Family == TrFamilyDeviceCallback || Family == TrFamilyAdapterCallback)
    {
        TrCallbackDepth += 1;
        Log = TrShouldLog();
    }
    WasQuiet = TrQuiet;
    TrQuiet = !Log;
    if (Family == TrFamilyAdapter && Slot == 1)
    {
        Result = static_cast<UINT_PTR>(TrCreateDevice(A));
        TrQuiet = WasQuiet;
        return Result;
    }

    switch (Family)
    {
    case TrFamilyAdapter:
        Name = TrAdapterNames[Slot];
        TrAppend(&Text, "%s %s", Name.c_str(), TrDriver(reinterpret_cast<VOID*>(A[0])).c_str());
        if (Slot == 0)
        {
            const D3DDDIARG_GETCAPS* Caps = reinterpret_cast<const D3DDDIARG_GETCAPS*>(A[1]);

            TrAppend(&Text, " type %u size %u info %s", Caps->Type, Caps->DataSize,
                     Caps->pInfo != nullptr ? "set" : "null");
            if (Caps->Type == D3DDDICAPS_GETMULTISAMPLEQUALITYLEVELS && Caps->pData != nullptr)
            {
                const DDIMULTISAMPLEQUALITYLEVELSDATA* Levels =
                    static_cast<const DDIMULTISAMPLEQUALITYLEVELSDATA*>(Caps->pData);
                TrAppend(&Text, " fmt %u flip %d ms %u", Levels->Format, Levels->Flip, Levels->MsType);
            }
        }
        break;
    case TrFamilyAdapterCallback:
        Name = TrAdapterCallbackNames[Slot];
        Text = Name;
        if (Slot == 0)
            TrAppend(&Text, " size %u",
                     reinterpret_cast<const D3DDDICB_QUERYADAPTERINFO*>(A[1])->PrivateDriverDataSize);
        else if (Slot == 2)
            TrAppend(&Text, " type %u size %u", reinterpret_cast<const D3DDDICB_QUERYADAPTERINFO2*>(A[1])->QueryType,
                     reinterpret_cast<const D3DDDICB_QUERYADAPTERINFO2*>(A[1])->PrivateDriverDataSize);
        break;
    case TrFamilyDevice:
        Name = TrDeviceNames[Slot];
        TrAppend(&Text, "%s %s", Name.c_str(), TrDriver(reinterpret_cast<VOID*>(A[0])).c_str());
        TrDeviceInputs(Slot, A, &Text);
        break;
    case TrFamilyDeviceCallback:
        Name = TrDeviceCallbackNames[Slot];
        TrAppend(&Text, "%s %s", Name.c_str(), TrRuntime(reinterpret_cast<VOID*>(A[0])).c_str());
        TrCallbackInputs(Slot, A, &Text);
        break;
    case TrFamilyKernel:
        Name = TrKernelNames[Slot] + 6;
        Text = "KMT" + Name;
        TrKernelInputs(Slot, reinterpret_cast<VOID*>(A[0]), &Text);
        break;
    default:
        break;
    }

    if (Log)
        TrBegin(Text);
    Result = TrCallOriginal(Family, Slot, A);
    if (Family == TrFamilyDeviceCallback || Family == TrFamilyAdapterCallback)
        TrCallbackDepth -= 1;
    TrQuiet = !Log;

    Inputs.swap(Text);
    switch (Family)
    {
    case TrFamilyAdapter:
        TrAppend(&Text, "%08lX", static_cast<HRESULT>(Result));
        if (Slot == 2)
            TrForget(TrKindDriver, A[0]);
        if (Slot == 0)
        {
            const D3DDDIARG_GETCAPS* Caps = reinterpret_cast<const D3DDDIARG_GETCAPS*>(A[1]);
            TrAppend(&Text, " data %08X", TrHash(Caps->pData, SUCCEEDED(static_cast<HRESULT>(Result)) ?
                                                              Caps->DataSize : 0));
        }
        break;
    case TrFamilyDevice:
        TrDeviceOutputs(Slot, A, static_cast<HRESULT>(Result), &Text);
        break;
    case TrFamilyDeviceCallback:
        TrCallbackOutputs(Slot, A, static_cast<HRESULT>(Result), &Text);
        break;
    case TrFamilyKernel:
        TrKernelOutputs(Slot, reinterpret_cast<VOID*>(A[0]), static_cast<NTSTATUS>(Result), &Text);
        break;
    default:
        TrAppend(&Text, "%08lX", static_cast<HRESULT>(Result));
        break;
    }
    if (Log)
        TrEnd(Inputs, Text);
    TrQuiet = WasQuiet;
    return Result;
}

/* The driver's OpenAdapter: its callbacks and functions are wrapped around the call */
static
HRESULT
APIENTRY
TrOpenAdapter(
    _Inout_ D3DDDIARG_OPENADAPTER* Args)
{
    const D3DDDI_ADAPTERCALLBACKS* RuntimeCallbacks = Args->pAdapterCallbacks;
    D3DDDI_ADAPTERCALLBACKS* Callbacks = new D3DDDI_ADAPTERCALLBACKS(*RuntimeCallbacks);
    BOOLEAN Log = TrShouldLog();
    BOOLEAN WasQuiet = TrQuiet;
    std::string Text;
    HRESULT hr;

    TrQuiet = !Log;
    TrAppend(&Text, "OpenAdapter adapter %s interface %u version %08X",
             TrKernel(reinterpret_cast<ULONG_PTR>(Args->hAdapter)).c_str(), Args->Interface, Args->Version);
    if (Log)
    {
        TrBegin(Text);
        TrCloseLine();
        printf("%*sadapter_callbacks_empty = %s\n", static_cast<int>(Tr.Depth * 2), "",
               TrEmptySlots(reinterpret_cast<const PTR_FORWARD*>(RuntimeCallbacks), TrAdapterCallbackNames,
                            _countof(TrAdapterCallbackNames)).c_str());
    }

    TrWrapTable(TrFamilyAdapterCallback, reinterpret_cast<PTR_FORWARD*>(Callbacks), _countof(TrAdapterCallbackNames));
    Args->pAdapterCallbacks = Callbacks;
    hr = Tr.OpenAdapter(Args);
    Args->pAdapterCallbacks = RuntimeCallbacks;
    TrQuiet = !Log;

    Text.clear();
    TrAppend(&Text, "%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Tr.DriverVersion = Args->DriverVersion;
        TrAppend(&Text, " %s driver %08X", TrDriver(Args->hAdapter).c_str(), Args->DriverVersion);
        TrWrapTable(TrFamilyAdapter, reinterpret_cast<PTR_FORWARD*>(Args->pAdapterFuncs), _countof(TrAdapterNames));
    }
    if (Log)
        TrEnd("OpenAdapter", Text);
    TrQuiet = WasQuiet;
    return hr;
}

static
INT
TrKernelSlot(
    _In_z_ PCSTR Name)
{
    for (UINT Slot = 0; Slot < _countof(TrKernelNames); Slot++)
    {
        if (strcmp(TrKernelNames[Slot], Name) == 0)
            return static_cast<INT>(Slot);
    }
    return -1;
}

static
FARPROC
WINAPI
TrGetProcAddress(
    _In_ HMODULE Module,
    _In_ LPCSTR Name)
{
    FARPROC Found = Tr.GetProcAddress(Module, Name);
    INT Slot;

    if (Found == nullptr || IS_INTRESOURCE(Name))
        return Found;

    if (strcmp(Name, "OpenAdapter") == 0)
    {
        Tr.OpenAdapter = reinterpret_cast<PFND3DDDI_OPENADAPTER>(Found);
        return reinterpret_cast<FARPROC>(TrOpenAdapter);
    }

    Slot = TrKernelSlot(Name);
    if (Slot < 0)
        return Found;
    Tr.Originals[TrFamilyKernel][Slot] = reinterpret_cast<PTR_FORWARD>(Found);
    return reinterpret_cast<FARPROC>(TrThunks(TrFamilyKernel)[Slot]);
}

static
VOID
TrWriteImport(
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
TrPatchImports(
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
            INT Slot;

            if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
                continue;
            ByName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(Base + Names->u1.AddressOfData);
            if (strcmp(ByName->Name, "GetProcAddress") == 0)
            {
                Tr.GetProcAddress = reinterpret_cast<decltype(Tr.GetProcAddress)>(Addresses->u1.Function);
                TrWriteImport(&Addresses->u1.Function, reinterpret_cast<ULONG_PTR>(TrGetProcAddress));
                Patched += 1;
                continue;
            }

            Slot = TrKernelSlot(ByName->Name);
            if (Slot < 0)
                continue;
            Tr.Originals[TrFamilyKernel][Slot] = reinterpret_cast<PTR_FORWARD>(Addresses->u1.Function);
            TrWriteImport(&Addresses->u1.Function, reinterpret_cast<ULONG_PTR>(TrThunks(TrFamilyKernel)[Slot]));
            Patched += 1;
        }
    }
    return Patched;
}

struct TrContext
{
    PFN_TR_CREATE Create;
    IDirect3D9Ex* Ex;
};

/* A small top level window the scenarios present to */
static
HWND
TrCreateWindow(VOID)
{
    static ATOM Class;
    WNDCLASSW Info = {};
    RECT Rect = { 0, 0, 64, 48 };
    HWND Window;

    if (Class == 0)
    {
        Info.lpfnWndProc = DefWindowProcW;
        Info.hInstance = GetModuleHandleW(nullptr);
        Info.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        Info.lpszClassName = L"TrWindow";
        Class = RegisterClassW(&Info);
    }

    AdjustWindowRect(&Rect, WS_OVERLAPPEDWINDOW, FALSE);
    Window = CreateWindowExW(0, L"TrWindow", L"d3d9trace", WS_OVERLAPPEDWINDOW, 40, 40, Rect.right - Rect.left,
                             Rect.bottom - Rect.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (Window != NULL)
    {
        ShowWindow(Window, SW_SHOWNOACTIVATE);
        UpdateWindow(Window);
    }
    return Window;
}

static
VOID
TrApi(
    _In_z_ PCSTR Name,
    _In_ HRESULT Result)
{
    TrCloseLine();
    printf("api %s = %08lX\n", Name, Result);
}

/* The windowed extended device d3d9check uses: creation, clears, presents, Ex settings and resets */
static
VOID
TrScenarioDevice(
    _In_ TrContext* Context)
{
    HWND Window = TrCreateWindow();
    D3DPRESENT_PARAMETERS Parameters = {};
    D3DPRESENT_PARAMETERS Again;
    IDirect3DDevice9Ex* Device = nullptr;
    D3DRECT Area = { 8, 8, 24, 16 };
    RECT Part = { 8, 8, 24, 16 };
    INT Priority = 99;
    UINT Latency = 99;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    Parameters.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    Parameters.EnableAutoDepthStencil = TRUE;
    Parameters.AutoDepthStencilFormat = D3DFMT_D24S8;

    TrPhase("device create");
    hr = Context->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters,
                                     nullptr, &Device);
    TrApi("CreateDeviceEx", hr);
    if (Device == nullptr)
    {
        DestroyWindow(Window);
        return;
    }

    TrPhase("device clear");
    TrApi("Clear", Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF336699, 1.0f, 0));
    TrApi("Clear depth", Device->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 0.5f, 7));
    TrApi("Clear rect", Device->Clear(1, &Area, D3DCLEAR_TARGET, 0xFFCC0000, 1.0f, 0));

    TrPhase("device present");
    TrApi("Present", Device->Present(nullptr, nullptr, nullptr, nullptr));
    TrApi("Present rect", Device->Present(&Part, &Part, nullptr, nullptr));
    TrApi("PresentEx donotwait", Device->PresentEx(nullptr, nullptr, nullptr, nullptr, D3DPRESENT_DONOTWAIT));

    TrPhase("device settings");
    TrApi("SetGPUThreadPriority 3", Device->SetGPUThreadPriority(3));
    TrApi("SetGPUThreadPriority -7", Device->SetGPUThreadPriority(-7));
    TrApi("GetGPUThreadPriority", Device->GetGPUThreadPriority(&Priority));
    TrCloseLine();
    printf("api priority = %d\n", Priority);
    TrApi("SetMaximumFrameLatency 5", Device->SetMaximumFrameLatency(5));
    TrApi("SetMaximumFrameLatency 30", Device->SetMaximumFrameLatency(30));
    TrApi("SetMaximumFrameLatency 31", Device->SetMaximumFrameLatency(31));
    TrApi("SetMaximumFrameLatency 0", Device->SetMaximumFrameLatency(0));
    TrApi("GetMaximumFrameLatency", Device->GetMaximumFrameLatency(&Latency));
    TrCloseLine();
    printf("api latency = %u\n", Latency);
    TrApi("Present", Device->Present(nullptr, nullptr, nullptr, nullptr));

    TrPhase("device reset");
    Again = Parameters;
    Again.BackBufferWidth = 32;
    Again.BackBufferHeight = 24;
    TrApi("Reset", Device->Reset(&Again));
    Again = Parameters;
    Again.BackBufferWidth = 0;
    Again.BackBufferHeight = 0;
    Again.EnableAutoDepthStencil = FALSE;
    TrApi("ResetEx", Device->ResetEx(&Again, nullptr));
    TrApi("Clear", Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF00FF00, 1.0f, 0));
    TrApi("Present", Device->Present(nullptr, nullptr, nullptr, nullptr));

    TrPhase("device release");
    TrCloseLine();
    printf("api Release = %lu\n", Device->Release());
    DestroyWindow(Window);
}

/* A windowed device of the plain object with software vertex processing and a copy swap chain */
static
VOID
TrScenarioDevicePlain(
    _In_ TrContext* Context)
{
    IDirect3D9* Plain = Context->Create(D3D_SDK_VERSION);
    HWND Window = TrCreateWindow();
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9* Device = nullptr;
    IDirect3DSurface9* Back = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_COPY;
    Parameters.hDeviceWindow = Window;
    Parameters.BackBufferWidth = 40;
    Parameters.BackBufferHeight = 30;
    Parameters.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

    TrPhase("plain create");
    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                             &Parameters, &Device);
    TrApi("CreateDevice", hr);
    if (Device != nullptr)
    {
        TrPhase("plain clear");
        TrApi("Clear", Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF00FF00, 1.0f, 0));
        TrApi("SetRenderState cull", Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));

        TrPhase("plain lock");
        hr = Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &Back);
        TrApi("GetBackBuffer", hr);
        if (Back != nullptr)
        {
            D3DLOCKED_RECT Locked = {};
            RECT Part = { 2, 3, 10, 9 };

            TrApi("LockRect", Back->LockRect(&Locked, nullptr, D3DLOCK_READONLY));
            TrApi("UnlockRect", Back->UnlockRect());
            TrApi("LockRect part", Back->LockRect(&Locked, &Part, 0));
            TrApi("UnlockRect", Back->UnlockRect());
            Back->Release();
        }

        TrPhase("plain present");
        TrApi("Present", Device->Present(nullptr, nullptr, nullptr, nullptr));

        TrPhase("plain reset");
        Parameters.BackBufferWidth = 0;
        Parameters.BackBufferHeight = 0;
        Parameters.EnableAutoDepthStencil = TRUE;
        Parameters.AutoDepthStencilFormat = D3DFMT_D16;
        TrApi("Reset", Device->Reset(&Parameters));
        TrApi("Clear", Device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0));

        TrPhase("plain release");
        TrCloseLine();
        printf("api Release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* A hardware vertex processing device of the plain object, for its caps and driver flags */
static
VOID
TrScenarioDeviceHardware(
    _In_ TrContext* Context)
{
    IDirect3D9* Plain = Context->Create(D3D_SDK_VERSION);
    HWND Window = TrCreateWindow();
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9* Device = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_FLIP;
    Parameters.BackBufferCount = 2;
    Parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    Parameters.EnableAutoDepthStencil = TRUE;
    Parameters.AutoDepthStencilFormat = D3DFMT_D24X8;
    Parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    TrPhase("hardware create");
    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
                             &Parameters, &Device);
    TrApi("CreateDevice", hr);
    if (Device != nullptr)
    {
        TrPhase("hardware present");
        TrApi("Clear", Device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF102030, 1.0f, 0));
        TrApi("Present", Device->Present(nullptr, nullptr, nullptr, nullptr));
        TrPhase("hardware release");
        TrCloseLine();
        printf("api Release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* A flip swap chain presented twice, so the second frame renders into the buffer the first flip moved in */
static
VOID
TrScenarioDeviceFlip(
    _In_ TrContext* Context)
{
    IDirect3D9* Plain = Context->Create(D3D_SDK_VERSION);
    HWND Window = TrCreateWindow();
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9* Device = nullptr;
    IDirect3DSurface9* Before = nullptr;
    IDirect3DSurface9* After = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_FLIP;
    Parameters.BackBufferCount = 2;
    Parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    Parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    TrPhase("flip create");
    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters, &Device);
    TrApi("CreateDevice", hr);
    if (Device != nullptr)
    {
        TrPhase("flip frames");
        TrApi("GetBackBuffer 0", Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &Before));
        TrApi("GetBackBuffer 2", Device->GetBackBuffer(0, 2, D3DBACKBUFFER_TYPE_MONO, &After));
        TrApi("Clear", Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030, 1.0f, 0));
        TrApi("Present", Device->Present(nullptr, nullptr, nullptr, nullptr));
        TrApi("Clear", Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF304050, 1.0f, 0));
        TrApi("Present", Device->Present(nullptr, nullptr, nullptr, nullptr));
        TrApi("GetBackBuffer 0", Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &After));
        TrCloseLine();
        printf("api same back buffer = %d\n", Before != nullptr && Before == After);
        if (After != nullptr)
            After->Release();
        if (Before != nullptr)
            Before->Release();

        TrPhase("flip release");
        TrCloseLine();
        printf("api Release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Locks a surface, writes a pattern when it may, and prints what the lock answered */
static
VOID
TrLockSurface(
    _In_z_ PCSTR Name,
    _In_ IDirect3DSurface9* Surface,
    _In_opt_ const RECT* Rect,
    _In_ DWORD Flags)
{
    D3DLOCKED_RECT Locked = {};
    HRESULT hr = Surface->LockRect(&Locked, Rect, Flags);

    TrCloseLine();
    printf("api LockRect %s = %08lX pitch %d %s\n", Name, hr, Locked.Pitch, Locked.pBits != nullptr ? "mem" : "nomem");
    if (FAILED(hr))
        return;
    if ((Flags & D3DLOCK_READONLY) == 0 && Locked.pBits != nullptr)
        *static_cast<DWORD*>(Locked.pBits) = 0x11223344;
    TrApi("UnlockRect", Surface->UnlockRect());
}

/* Prints a surface's description, or that the call made none */
static
VOID
TrDescribeSurface(
    _In_z_ PCSTR Name,
    _In_ HRESULT Result,
    _In_opt_ IDirect3DSurface9* Surface)
{
    D3DSURFACE_DESC Desc = {};

    TrCloseLine();
    if (Surface == nullptr)
    {
        printf("api %s = %08lX null\n", Name, Result);
        return;
    }
    Surface->GetDesc(&Desc);
    printf("api %s = %08lX fmt %u usage %08lX pool %u ms %u:%lu size %ux%u\n", Name, Result, Desc.Format, Desc.Usage,
           Desc.Pool, Desc.MultiSampleType, Desc.MultiSampleQuality, Desc.Width, Desc.Height);
}

/* Standalone surfaces of every kind, the calls that refuse them, and their locks */
static
VOID
TrSurfaceWork(
    _In_ IDirect3DDevice9* Device,
    _In_opt_ IDirect3DDevice9Ex* DeviceEx)
{
    static const struct
    {
        PCSTR Name;
        D3DFORMAT Format;
        D3DPOOL Pool;
        UINT Width;
        UINT Height;
    } Plains[] =
    {
        { "plain sysmem", D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, 16, 8 },
        { "plain default", D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, 16, 8 },
        { "plain scratch", D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, 16, 8 },
        { "plain managed", D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, 16, 8 },
        { "plain unknown", D3DFMT_UNKNOWN, D3DPOOL_SYSTEMMEM, 16, 8 },
        { "plain dxt1", D3DFMT_DXT1, D3DPOOL_SYSTEMMEM, 16, 8 },
        { "plain dxt1 odd", D3DFMT_DXT1, D3DPOOL_SYSTEMMEM, 10, 8 },
        { "plain r5g6b5 default", D3DFMT_R5G6B5, D3DPOOL_DEFAULT, 7, 5 },
        { "plain zero", D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 0, 8 },
        { "plain d16", D3DFMT_D16, D3DPOOL_DEFAULT, 16, 8 },
        { "plain yuy2", D3DFMT_YUY2, D3DPOOL_DEFAULT, 16, 8 },
    };
    IDirect3DSurface9* Surface = nullptr;
    RECT Part = { 2, 1, 10, 5 };
    HRESULT hr;

    TrPhase("surfaces plain");
    for (const auto& Case : Plains)
    {
        Surface = nullptr;
        hr = Device->CreateOffscreenPlainSurface(Case.Width, Case.Height, Case.Format, Case.Pool, &Surface, nullptr);
        TrDescribeSurface(Case.Name, hr, Surface);
        if (Surface == nullptr)
            continue;
        TrLockSurface("full", Surface, nullptr, 0);
        TrLockSurface("part", Surface, &Part, D3DLOCK_READONLY);
        TrCloseLine();
        printf("api Release = %lu\n", Surface->Release());
    }

    TrPhase("surfaces target");
    Surface = nullptr;
    hr = Device->CreateRenderTarget(32, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &Surface, nullptr);
    TrDescribeSurface("target lockable", hr, Surface);
    if (Surface != nullptr)
    {
        TrLockSurface("full", Surface, nullptr, 0);
        Surface->Release();
    }
    Surface = nullptr;
    hr = Device->CreateRenderTarget(32, 16, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr);
    TrDescribeSurface("target", hr, Surface);
    if (Surface != nullptr)
    {
        TrLockSurface("full", Surface, nullptr, 0);
        Surface->Release();
    }
    Surface = nullptr;
    hr = Device->CreateRenderTarget(32, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE, &Surface, nullptr);
    TrDescribeSurface("target ms4", hr, Surface);
    if (Surface != nullptr)
        Surface->Release();
    Surface = nullptr;
    hr = Device->CreateRenderTarget(32, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_4_SAMPLES, 0, TRUE, &Surface, nullptr);
    TrDescribeSurface("target ms4 lockable", hr, Surface);
    if (Surface != nullptr)
        Surface->Release();
    Surface = nullptr;
    hr = Device->CreateRenderTarget(32, 16, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr);
    TrDescribeSurface("target depth format", hr, Surface);
    if (Surface != nullptr)
        Surface->Release();

    TrPhase("surfaces depth");
    Surface = nullptr;
    hr = Device->CreateDepthStencilSurface(32, 16, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr);
    TrDescribeSurface("depth", hr, Surface);
    if (Surface != nullptr)
    {
        TrLockSurface("full", Surface, nullptr, 0);
        Surface->Release();
    }
    Surface = nullptr;
    hr = Device->CreateDepthStencilSurface(32, 16, D3DFMT_D16, D3DMULTISAMPLE_NONE, 0, TRUE, &Surface, nullptr);
    TrDescribeSurface("depth discard", hr, Surface);
    if (Surface != nullptr)
        Surface->Release();
    Surface = nullptr;
    hr = Device->CreateDepthStencilSurface(32, 16, D3DFMT_D16_LOCKABLE, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface,
                                           nullptr);
    TrDescribeSurface("depth lockable", hr, Surface);
    if (Surface != nullptr)
    {
        TrLockSurface("full", Surface, nullptr, 0);
        Surface->Release();
    }
    Surface = nullptr;
    hr = Device->CreateDepthStencilSurface(32, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr);
    TrDescribeSurface("depth color format", hr, Surface);
    if (Surface != nullptr)
        Surface->Release();

    if (DeviceEx == nullptr)
        return;

    TrPhase("surfaces ex");
    Surface = nullptr;
    hr = DeviceEx->CreateRenderTargetEx(32, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr, 0);
    TrDescribeSurface("target ex", hr, Surface);
    if (Surface != nullptr)
        Surface->Release();
    Surface = nullptr;
    hr = DeviceEx->CreateRenderTargetEx(32, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr,
                                        D3DUSAGE_RESTRICTED_CONTENT);
    TrDescribeSurface("target ex restricted", hr, Surface);
    if (Surface != nullptr)
        Surface->Release();
    Surface = nullptr;
    hr = DeviceEx->CreateRenderTargetEx(32, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr,
                                        D3DUSAGE_DYNAMIC);
    TrDescribeSurface("target ex dynamic", hr, Surface);
    if (Surface != nullptr)
        Surface->Release();
    Surface = nullptr;
    hr = DeviceEx->CreateOffscreenPlainSurfaceEx(16, 8, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Surface, nullptr, 0);
    TrDescribeSurface("plain ex", hr, Surface);
    if (Surface != nullptr)
        Surface->Release();
    Surface = nullptr;
    hr = DeviceEx->CreateDepthStencilSurfaceEx(32, 16, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr,
                                               0);
    TrDescribeSurface("depth ex", hr, Surface);
    if (Surface != nullptr)
        Surface->Release();
}

/* Standalone surfaces on a windowed extended device */
static
VOID
TrScenarioSurfaces(
    _In_ TrContext* Context)
{
    HWND Window = TrCreateWindow();
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9Ex* Device = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;

    TrPhase("surfaces create");
    hr = Context->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters,
                                     nullptr, &Device);
    TrApi("CreateDeviceEx", hr);
    if (Device != nullptr)
    {
        TrSurfaceWork(Device, Device);
        TrPhase("surfaces release");
        TrCloseLine();
        printf("api Release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
}

/* The same surfaces on a device of the plain object, whose driver calls go through the batch */
static
VOID
TrScenarioSurfacesPlain(
    _In_ TrContext* Context)
{
    IDirect3D9* Plain = Context->Create(D3D_SDK_VERSION);
    HWND Window = TrCreateWindow();
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9* Device = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;

    TrPhase("surfaces create");
    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters, &Device);
    TrApi("CreateDevice", hr);
    if (Device != nullptr)
    {
        TrSurfaceWork(Device, nullptr);
        TrPhase("surfaces release");
        TrCloseLine();
        printf("api Release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

struct TrScenario
{
    PCSTR Name;
    VOID (*Run)(_In_ TrContext* Context);
};

static const TrScenario TrScenarios[] =
{
    { "device", TrScenarioDevice },
    { "device_plain", TrScenarioDevicePlain },
    { "device_hardware", TrScenarioDeviceHardware },
    { "device_flip", TrScenarioDeviceFlip },
    { "surfaces", TrScenarioSurfaces },
    { "surfaces_plain", TrScenarioSurfacesPlain },
};

static
std::wstring
TrRuntimePath(
    _In_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"d3d9.dll";

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\d3d9.dll";
    }

    return Runtime;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    std::wstring Runtime = TrRuntimePath(L"system");
    std::vector<std::string> Wanted;
    PFN_TR_CREATE_EX CreateEx;
    TrContext Context = {};
    WCHAR Loaded[MAX_PATH] = {};
    HMODULE Module;
    char Name[64];
    HRESULT hr;

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Index += 1;
            Runtime = TrRuntimePath(argv[Index]);
            continue;
        }
        if (_wcsicmp(argv[Index], L"-threads") == 0)
        {
            Tr.AllThreads = TRUE;
            continue;
        }
        if (_wcsicmp(argv[Index], L"-ddi") == 0)
        {
            Tr.DdiOnly = TRUE;
            continue;
        }
        WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
        Wanted.push_back(Name);
    }

    Tr.MainThread = GetCurrentThreadId();
    Module = LoadLibraryW(Runtime.c_str());
    if (Module == NULL)
    {
        fprintf(stderr, "error: cannot load %ls (%lu)\n", Runtime.c_str(), GetLastError());
        return 1;
    }
    GetModuleFileNameW(Module, Loaded, _countof(Loaded) - 1);
    fprintf(stderr, "runtime: %ls\n", Loaded);
    fprintf(stderr, "patched imports: %u\n", TrPatchImports(Module));

    Context.Create = reinterpret_cast<PFN_TR_CREATE>(GetProcAddress(Module, "Direct3DCreate9"));
    CreateEx = reinterpret_cast<PFN_TR_CREATE_EX>(GetProcAddress(Module, "Direct3DCreate9Ex"));
    if (Context.Create == nullptr || CreateEx == nullptr)
    {
        fprintf(stderr, "error: the runtime lacks its creation exports\n");
        return 1;
    }

    TrPhase("create9ex");
    hr = CreateEx(D3D_SDK_VERSION, &Context.Ex);
    TrApi("Direct3DCreate9Ex", hr);
    if (FAILED(hr))
        return 1;

    for (const TrScenario& Scenario : TrScenarios)
    {
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Item : Wanted)
            IsWanted = IsWanted || Item == Scenario.Name;
        if (IsWanted)
            Scenario.Run(&Context);
    }

    TrPhase("release");
    TrCloseLine();
    printf("api Release9Ex = %lu\n", Context.Ex->Release());
    fflush(stdout);
    return 0;
}
