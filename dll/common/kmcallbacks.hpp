/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     WDDM 2 kernel callbacks every ReactX runtime hands to its user mode driver
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <d3d9types.h>
#include <d3dumddi.h>

/* What the shared callbacks need from the runtime device a UMD calls them for */
class RxKernelDevice
{
public:
    virtual
    D3DKMT_HANDLE
    KernelDevice() const = 0;

    virtual
    D3DKMT_HANDLE
    KernelAdapter() const = 0;

    /* A context the UMD made, for runtimes that keep a list of them */
    virtual
    VOID
    NoteKernelContext(
        _In_ D3DKMT_HANDLE Context)
    {
        UNREFERENCED_PARAMETER(Context);
    }

    /* The UMD destroyed a context it made */
    virtual
    VOID
    ForgetKernelContext(
        _In_ D3DKMT_HANDLE Context)
    {
        UNREFERENCED_PARAMETER(Context);
    }

    /* A hardware queue the UMD made on one of its contexts */
    virtual
    VOID
    NoteKernelQueue(
        _In_ D3DKMT_HANDLE Context,
        _In_ D3DKMT_HANDLE Queue)
    {
        UNREFERENCED_PARAMETER(Context);
        UNREFERENCED_PARAMETER(Queue);
    }

    /* The UMD destroyed a hardware queue it made */
    virtual
    VOID
    ForgetKernelQueue(
        _In_ D3DKMT_HANDLE Queue)
    {
        UNREFERENCED_PARAMETER(Queue);
    }

    /* Which API the kernel is told the contexts are for */
    virtual
    D3DKMT_CLIENTHINT
    KernelClientHint() const = 0;

    /* The kernel resource behind a runtime resource handle the UMD passes back, 0 for none */
    virtual
    D3DKMT_HANDLE
    KernelResourceOf(
        _In_opt_ HANDLE RuntimeResource) = 0;

    /* The driver destroyed the kernel resource behind a runtime resource handle */
    virtual
    VOID
    RetireKernelResource(
        _In_ HANDLE RuntimeResource) = 0;
};

/* Each runtime module says which of its objects a callback's device handle is */
RxKernelDevice&
RxKernelDeviceFromHandle(
    _In_ HANDLE Handle);

/* Kernel handles for an array of runtime resource handles; the caller frees the array */
_Success_(return != nullptr)
D3DKMT_HANDLE*
RxTranslateResources(
    _In_ RxKernelDevice& Device,
    _In_reads_(Count) const HANDLE* Resources,
    _In_ UINT Count);

HRESULT
APIENTRY
RxCreatePagingQueueCb(
    _In_ HANDLE hDevice,
    _Out_ D3DDDICB_CREATEPAGINGQUEUE* Args);

HRESULT
APIENTRY
RxDestroyPagingQueueCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDI_DESTROYPAGINGQUEUE* Args);

HRESULT
APIENTRY
RxMakeResidentCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDI_MAKERESIDENT* Args);

HRESULT
APIENTRY
RxEvictCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_EVICT* Args);

HRESULT
APIENTRY
RxReserveGpuVirtualAddressCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDI_RESERVEGPUVIRTUALADDRESS* Args);

HRESULT
APIENTRY
RxMapGpuVirtualAddressCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDI_MAPGPUVIRTUALADDRESS* Args);

HRESULT
APIENTRY
RxFreeGpuVirtualAddressCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_FREEGPUVIRTUALADDRESS* Args);

HRESULT
APIENTRY
RxUpdateGpuVirtualAddressCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_UPDATEGPUVIRTUALADDRESS* Args);

HRESULT
APIENTRY
RxCreateContextVirtualCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATECONTEXTVIRTUAL* Args);

HRESULT
APIENTRY
RxSubmitCommandCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SUBMITCOMMAND* Args);

HRESULT
APIENTRY
RxLock2Cb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_LOCK2* Args);

HRESULT
APIENTRY
RxUnlock2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_UNLOCK2* Args);

HRESULT
APIENTRY
RxInvalidateCacheCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_INVALIDATECACHE* Args);

HRESULT
APIENTRY
RxDeallocate2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DEALLOCATE2* Args);

HRESULT
APIENTRY
RxReclaimAllocations2Cb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_RECLAIMALLOCATIONS2* Args);

HRESULT
APIENTRY
RxReclaimAllocations3Cb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_RECLAIMALLOCATIONS3* Args);

HRESULT
APIENTRY
RxUpdateAllocationPropertyCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDI_UPDATEALLOCPROPERTY* Args);

HRESULT
APIENTRY
RxGetResourcePresentPrivateDriverDataCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDI_GETRESOURCEPRESENTPRIVATEDRIVERDATA* Args);

HRESULT
APIENTRY
RxWaitForSynchronizationObjectFromCpuCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU* Args);

HRESULT
APIENTRY
RxSignalSynchronizationObjectFromCpuCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU* Args);

HRESULT
APIENTRY
RxWaitForSynchronizationObjectFromGpuCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU* Args);

HRESULT
APIENTRY
RxSignalSynchronizationObjectFromGpuCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU* Args);

HRESULT
APIENTRY
RxSignalSynchronizationObjectFromGpu2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2* Args);

HRESULT
APIENTRY
RxCreateHwContextCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATEHWCONTEXT* Args);

HRESULT
APIENTRY
RxDestroyHwContextCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYHWCONTEXT* Args);

HRESULT
APIENTRY
RxCreateHwQueueCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATEHWQUEUE* Args);

HRESULT
APIENTRY
RxDestroyHwQueueCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYHWQUEUE* Args);

HRESULT
APIENTRY
RxSubmitCommandToHwQueueCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SUBMITCOMMANDTOHWQUEUE* Args);

HRESULT
APIENTRY
RxSubmitWaitForSyncObjectsToHwQueueCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE* Args);

HRESULT
APIENTRY
RxSubmitSignalSyncObjectsToHwQueueCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE* Args);

HRESULT
APIENTRY
RxSetPriorityCb(
    _In_ HANDLE hDevice,
    _In_ D3DDDICB_SETPRIORITY* Args);

HRESULT
APIENTRY
RxQueryResidencyCb(
    _In_ HANDLE hDevice,
    _Inout_ const D3DDDICB_QUERYRESIDENCY* Args);

HRESULT
APIENTRY
RxRenderCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_RENDER* Args);

HRESULT
APIENTRY
RxLockCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_LOCK* Args);

HRESULT
APIENTRY
RxUnlockCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_UNLOCK* Args);

HRESULT
APIENTRY
RxCreateContextCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATECONTEXT* Args);

HRESULT
APIENTRY
RxDestroyContextCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYCONTEXT* Args);

HRESULT
APIENTRY
RxCreateSynchronizationObjectCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATESYNCHRONIZATIONOBJECT* Args);

HRESULT
APIENTRY
RxCreateSynchronizationObject2Cb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATESYNCHRONIZATIONOBJECT2* Args);

HRESULT
APIENTRY
RxDestroySynchronizationObjectCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT* Args);

HRESULT
APIENTRY
RxWaitForSynchronizationObjectCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECT* Args);

HRESULT
APIENTRY
RxWaitForSynchronizationObject2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECT2* Args);

HRESULT
APIENTRY
RxSignalSynchronizationObjectCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT* Args);

HRESULT
APIENTRY
RxSignalSynchronizationObject2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2* Args);

HRESULT
APIENTRY
RxSetAsyncCallbacksCb(
    _In_ HANDLE hDevice,
    _In_ BOOL Enable);

HRESULT
APIENTRY
RxOfferAllocationsCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_OFFERALLOCATIONS* Args);

HRESULT
APIENTRY
RxReclaimAllocationsCb(
    _In_ HANDLE hDevice,
    _Inout_ const D3DDDICB_RECLAIMALLOCATIONS* Args);

HRESULT
APIENTRY
RxLogUmdMarkerCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_LOGUMDMARKER* Args);

HRESULT
APIENTRY
RxSetDisplayPrivateDriverFormatCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SETDISPLAYPRIVATEDRIVERFORMAT* Args);
