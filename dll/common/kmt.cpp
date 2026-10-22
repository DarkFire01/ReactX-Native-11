/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3DKMT thunks resolved from gdi32 at run time
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "rxcommon.hpp"

#include <d3d11_4.h>
#include <d3d10umddi.h>

static INIT_ONCE KmtInitOnce = INIT_ONCE_STATIC_INIT;
static KmtThunks Kmt;
static BOOLEAN KmtUsable;

template <typename Function>
static
VOID
RxResolve(
    _In_ HMODULE Module,
    _In_z_ PCSTR Name,
    _Out_ Function& Pointer)
{
    Pointer = reinterpret_cast<Function>(GetProcAddress(Module, Name));
}

static
BOOL
CALLBACK
RxLoadKmtThunks(
    _Inout_ PINIT_ONCE InitOnce,
    _Inout_opt_ PVOID Parameter,
    _Out_opt_ PVOID* Context)
{
    HMODULE Gdi32;

    Gdi32 = LoadLibraryExW(L"gdi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Gdi32 == NULL)
    {
        RX_TRACE(L"gdi32.dll failed to load (%lu)", GetLastError());
        return TRUE;
    }

    RxResolve(Gdi32, "D3DKMTOpenAdapterFromLuid", Kmt.OpenAdapterFromLuid);
    RxResolve(Gdi32, "D3DKMTOpenAdapterFromGdiDisplayName", Kmt.OpenAdapterFromGdiDisplayName);
    RxResolve(Gdi32, "D3DKMTEnumAdapters", Kmt.EnumAdapters);
    RxResolve(Gdi32, "D3DKMTEnumAdapters2", Kmt.EnumAdapters2);
    RxResolve(Gdi32, "D3DKMTCloseAdapter", Kmt.CloseAdapter);
    RxResolve(Gdi32, "D3DKMTQueryAdapterInfo", Kmt.QueryAdapterInfo);
    RxResolve(Gdi32, "D3DKMTEscape", Kmt.Escape);
    RxResolve(Gdi32, "D3DKMTGetDisplayModeList", Kmt.GetDisplayModeList);
    RxResolve(Gdi32, "D3DKMTWaitForVerticalBlankEvent", Kmt.WaitForVerticalBlankEvent);
    RxResolve(Gdi32, "D3DKMTSetDisplayMode", Kmt.SetDisplayMode);
    RxResolve(Gdi32, "D3DKMTSetVidPnSourceOwner", Kmt.SetVidPnSourceOwner);
    RxResolve(Gdi32, "D3DKMTSetVidPnSourceOwner1", Kmt.SetVidPnSourceOwner1);
    RxResolve(Gdi32, "D3DKMTQueryVideoMemoryInfo", Kmt.QueryVideoMemoryInfo);
    RxResolve(Gdi32, "D3DKMTChangeVideoMemoryReservation", Kmt.ChangeVideoMemoryReservation);
    RxResolve(Gdi32, "D3DKMTGetSharedResourceAdapterLuid", Kmt.GetSharedResourceAdapterLuid);
    RxResolve(Gdi32, "D3DKMTShareObjects", Kmt.ShareObjects);

    RxResolve(Gdi32, "D3DKMTCreateDevice", Kmt.CreateDevice);
    RxResolve(Gdi32, "D3DKMTDestroyDevice", Kmt.DestroyDevice);
    RxResolve(Gdi32, "D3DKMTCreateContext", Kmt.CreateContext);
    RxResolve(Gdi32, "D3DKMTDestroyContext", Kmt.DestroyContext);
    RxResolve(Gdi32, "D3DKMTSetContextSchedulingPriority", Kmt.SetContextSchedulingPriority);
    RxResolve(Gdi32, "D3DKMTGetContextSchedulingPriority", Kmt.GetContextSchedulingPriority);
    RxResolve(Gdi32, "D3DKMTRender", Kmt.Render);

    RxResolve(Gdi32, "D3DKMTCreateAllocation", Kmt.CreateAllocation);
    RxResolve(Gdi32, "D3DKMTCreateAllocation2", Kmt.CreateAllocation2);
    RxResolve(Gdi32, "D3DKMTDestroyAllocation", Kmt.DestroyAllocation);
    RxResolve(Gdi32, "D3DKMTQueryResourceInfo", Kmt.QueryResourceInfo);
    RxResolve(Gdi32, "D3DKMTOpenResource", Kmt.OpenResource);
    RxResolve(Gdi32, "D3DKMTQueryResourceInfoFromNtHandle", Kmt.QueryResourceInfoFromNtHandle);
    RxResolve(Gdi32, "D3DKMTOpenResourceFromNtHandle", Kmt.OpenResourceFromNtHandle);
    RxResolve(Gdi32, "D3DKMTCreateKeyedMutex2", Kmt.CreateKeyedMutex2);
    RxResolve(Gdi32, "D3DKMTOpenKeyedMutex2", Kmt.OpenKeyedMutex2);
    RxResolve(Gdi32, "D3DKMTDestroyKeyedMutex", Kmt.DestroyKeyedMutex);
    RxResolve(Gdi32, "D3DKMTAcquireKeyedMutex2", Kmt.AcquireKeyedMutex2);
    RxResolve(Gdi32, "D3DKMTReleaseKeyedMutex2", Kmt.ReleaseKeyedMutex2);
    RxResolve(Gdi32, "D3DKMTPresent", Kmt.Present);
    RxResolve(Gdi32, "D3DKMTLock", Kmt.Lock);
    RxResolve(Gdi32, "D3DKMTUnlock", Kmt.Unlock);
    RxResolve(Gdi32, "D3DKMTSetAllocationPriority", Kmt.SetAllocationPriority);
    RxResolve(Gdi32, "D3DKMTQueryAllocationResidency", Kmt.QueryAllocationResidency);
    RxResolve(Gdi32, "D3DKMTOfferAllocations", Kmt.OfferAllocations);
    RxResolve(Gdi32, "D3DKMTReclaimAllocations", Kmt.ReclaimAllocations);

    RxResolve(Gdi32, "D3DKMTCreateSynchronizationObject", Kmt.CreateSynchronizationObject);
    RxResolve(Gdi32, "D3DKMTCreateSynchronizationObject2", Kmt.CreateSynchronizationObject2);
    RxResolve(Gdi32, "D3DKMTOpenSynchronizationObject", Kmt.OpenSynchronizationObject);
    RxResolve(Gdi32, "D3DKMTDestroySynchronizationObject", Kmt.DestroySynchronizationObject);
    RxResolve(Gdi32, "D3DKMTWaitForSynchronizationObject", Kmt.WaitForSynchronizationObject);
    RxResolve(Gdi32, "D3DKMTWaitForSynchronizationObject2", Kmt.WaitForSynchronizationObject2);
    RxResolve(Gdi32, "D3DKMTSignalSynchronizationObject", Kmt.SignalSynchronizationObject);
    RxResolve(Gdi32, "D3DKMTSignalSynchronizationObject2", Kmt.SignalSynchronizationObject2);

    RxResolve(Gdi32, "D3DKMTCreatePagingQueue", Kmt.CreatePagingQueue);
    RxResolve(Gdi32, "D3DKMTDestroyPagingQueue", Kmt.DestroyPagingQueue);
    RxResolve(Gdi32, "D3DKMTMakeResident", Kmt.MakeResident);
    RxResolve(Gdi32, "D3DKMTEvict", Kmt.Evict);
    RxResolve(Gdi32, "D3DKMTReserveGpuVirtualAddress", Kmt.ReserveGpuVirtualAddress);
    RxResolve(Gdi32, "D3DKMTMapGpuVirtualAddress", Kmt.MapGpuVirtualAddress);
    RxResolve(Gdi32, "D3DKMTFreeGpuVirtualAddress", Kmt.FreeGpuVirtualAddress);
    RxResolve(Gdi32, "D3DKMTUpdateGpuVirtualAddress", Kmt.UpdateGpuVirtualAddress);
    RxResolve(Gdi32, "D3DKMTCreateContextVirtual", Kmt.CreateContextVirtual);
    RxResolve(Gdi32, "D3DKMTSubmitCommand", Kmt.SubmitCommand);
    RxResolve(Gdi32, "D3DKMTLock2", Kmt.Lock2);
    RxResolve(Gdi32, "D3DKMTUnlock2", Kmt.Unlock2);
    RxResolve(Gdi32, "D3DKMTInvalidateCache", Kmt.InvalidateCache);
    RxResolve(Gdi32, "D3DKMTDestroyAllocation2", Kmt.DestroyAllocation2);
    RxResolve(Gdi32, "D3DKMTReclaimAllocations2", Kmt.ReclaimAllocations2);
    RxResolve(Gdi32, "D3DKMTUpdateAllocationProperty", Kmt.UpdateAllocationProperty);
    RxResolve(Gdi32, "D3DKMTGetResourcePresentPrivateDriverData", Kmt.GetResourcePresentPrivateDriverData);
    RxResolve(Gdi32, "D3DKMTWaitForSynchronizationObjectFromCpu", Kmt.WaitForSynchronizationObjectFromCpu);
    RxResolve(Gdi32, "D3DKMTSignalSynchronizationObjectFromCpu", Kmt.SignalSynchronizationObjectFromCpu);
    RxResolve(Gdi32, "D3DKMTWaitForSynchronizationObjectFromGpu", Kmt.WaitForSynchronizationObjectFromGpu);
    RxResolve(Gdi32, "D3DKMTSignalSynchronizationObjectFromGpu", Kmt.SignalSynchronizationObjectFromGpu);
    RxResolve(Gdi32, "D3DKMTSignalSynchronizationObjectFromGpu2", Kmt.SignalSynchronizationObjectFromGpu2);

    RxResolve(Gdi32, "D3DKMTCreateHwContext", Kmt.CreateHwContext);
    RxResolve(Gdi32, "D3DKMTDestroyHwContext", Kmt.DestroyHwContext);
    RxResolve(Gdi32, "D3DKMTCreateHwQueue", Kmt.CreateHwQueue);
    RxResolve(Gdi32, "D3DKMTDestroyHwQueue", Kmt.DestroyHwQueue);
    RxResolve(Gdi32, "D3DKMTSubmitCommandToHwQueue", Kmt.SubmitCommandToHwQueue);
    RxResolve(Gdi32, "D3DKMTSubmitWaitForSyncObjectsToHwQueue", Kmt.SubmitWaitForSyncObjectsToHwQueue);
    RxResolve(Gdi32, "D3DKMTSubmitSignalSyncObjectsToHwQueue", Kmt.SubmitSignalSyncObjectsToHwQueue);
    RxResolve(Gdi32, "D3DKMTSubmitPresentToHwQueue", Kmt.SubmitPresentToHwQueue);
    RxResolve(Gdi32, "D3DKMTQueryClockCalibration", Kmt.QueryClockCalibration);
    RxResolve(Gdi32, "D3DKMTSetContextInProcessSchedulingPriority", Kmt.SetContextInProcessSchedulingPriority);

    /* Nothing works without these */
    KmtUsable = (Kmt.CloseAdapter != NULL &&
                 Kmt.QueryAdapterInfo != NULL &&
                 Kmt.CreateDevice != NULL &&
                 Kmt.DestroyDevice != NULL);
    return TRUE;
}

_Ret_maybenull_
const KmtThunks*
RxGetKmtThunks(VOID)
{
    InitOnceExecuteOnce(&KmtInitOnce, RxLoadKmtThunks, NULL, NULL);
    return KmtUsable ? &Kmt : NULL;
}

HRESULT
RxHresultFromStatus(
    _In_ NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_SUCCESS:
        case STATUS_GRAPHICS_PRESENT_OCCLUDED:
            return S_OK;

        /* Waits report these as they are */
        case STATUS_ABANDONED:
        case STATUS_TIMEOUT:
            return (HRESULT)Status;

        /* The UMD waits on the paging fence it was handed when it sees this */
        case STATUS_PENDING:
            return E_PENDING;

        case STATUS_NO_MEMORY:
        case STATUS_COMMITMENT_LIMIT:
            return E_OUTOFMEMORY;

        case STATUS_INSUFFICIENT_RESOURCES:
        case STATUS_GRAPHICS_NO_VIDEO_MEMORY:
            return D3DDDIERR_OUTOFVIDEOMEMORY;

        case STATUS_GRAPHICS_ALLOCATION_BUSY:
            return D3DDDIERR_WASSTILLDRAWING;

        case STATUS_INVALID_HANDLE:
        case STATUS_OBJECT_TYPE_MISMATCH:
        case STATUS_OBJECT_NAME_INVALID:
        case STATUS_OBJECT_NAME_NOT_FOUND:
        case STATUS_OBJECT_NAME_COLLISION:
        case STATUS_OBJECT_PATH_INVALID:
        case STATUS_OBJECT_PATH_NOT_FOUND:
        case STATUS_OBJECT_PATH_SYNTAX_BAD:
            return D3DDDIERR_INVALIDHANDLE;

        case STATUS_ACCESS_DENIED:
        case STATUS_PROCEDURE_NOT_FOUND:
            return D3DDDIERR_NOTAVAILABLE;

        case STATUS_ILLEGAL_INSTRUCTION:
            return D3DDDIERR_INVALIDINSTRUCTION;

        case STATUS_PRIVILEGED_INSTRUCTION:
            return D3DDDIERR_PRIVILEGEDINSTRUCTION;

        case STATUS_INVALID_USER_BUFFER:
            return D3DDDIERR_INVALIDUSERBUFFER;

        case STATUS_GRAPHICS_INCOMPATIBLE_PRIVATE_FORMAT:
            return D3DDDIERR_INCOMPATIBLEPRIVATEFORMAT;

        case STATUS_GRAPHICS_CANT_EVICT_PINNED_ALLOCATION:
            return D3DDDIERR_CANTEVICTPINNEDALLOCATION;

        case STATUS_GRAPHICS_CANT_RENDER_LOCKED_ALLOCATION:
            return D3DDDIERR_CANTRENDERLOCKEDALLOCATION;

        case STATUS_DEVICE_REMOVED:
        case STATUS_GRAPHICS_DRIVER_MISMATCH:
        case STATUS_GRAPHICS_ALLOCATION_CONTENT_LOST:
        case STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE:
            return D3DDDIERR_DEVICEREMOVED;

        case STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE:
        case STATUS_GRAPHICS_NO_AVAILABLE_VIDPN_TARGET:
            return DXGI_ERROR_NOT_FOUND;

        case STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE:
            return DXGI_STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;

        default:
            break;
    }

    /* Every numbered invalid parameter status is plain bad arguments */
    if (Status == STATUS_INVALID_PARAMETER ||
        (Status >= STATUS_INVALID_PARAMETER_1 && Status <= STATUS_INVALID_PARAMETER_12))
    {
        return E_INVALIDARG;
    }

    /* The allocation statuses just past the locked one all name a bad handle */
    if (Status > STATUS_GRAPHICS_CANT_RENDER_LOCKED_ALLOCATION && Status <= STATUS_GRAPHICS_WRONG_ALLOCATION_DEVICE)
        return D3DDDIERR_INVALIDHANDLE;

    return E_FAIL;
}
