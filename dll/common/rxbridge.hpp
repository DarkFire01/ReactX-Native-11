/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The private contract between the ReactX dxgi.dll and d3d11.dll
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <dxgiddi.h>

/*
 * Everything else the two modules share goes through public DXGI and D3D11 calls.
 * Swapchains are the exception: they need driver resources and the driver's present.
 */

/* One present in flight: the kernel present the UMD's present callback is built from */
struct RxPresentContext
{
    D3DKMT_PRESENT Template;
    RECT SourceRect;

    /* What the kernel said about the present, kept because the driver only passes on success or failure */
    NTSTATUS KernelStatus;
};

/* A device that can back ReactX swapchains answers QueryInterface for this */
struct __declspec(uuid("3c8a5e71-94d2-4f06-b1e8-6d27a0c45f93")) IRxPresentDevice : public IUnknown
{
    /* The kernel adapter the device runs on */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    GetAdapterLuid(
        _Out_ LUID* Luid) = 0;

    /*
     * One swapchain buffer, bindable for present; the object is the device's 2D texture.
     * Parent is what the buffer reports as its DXGI parent, held without a reference.
     */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    CreatePresentBuffer(
        _In_ const DXGI_SWAP_CHAIN_DESC1* Desc,
        _In_ IUnknown* Parent,
        _COM_Outptr_ IUnknown** Buffer) = 0;

    /* The swapchain is letting go of a buffer the application may still hold */
    virtual
    VOID
    STDMETHODCALLTYPE
    DetachPresentBuffer(
        _In_ IUnknown* Buffer) = 0;

    /* Opens a surface another process shares by kernel handle, as a present destination */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    OpenPresentTarget(
        _In_ HANDLE SharedHandle,
        _COM_Outptr_ IUnknown** Target) = 0;

    /* An NT handle to a flip model buffer's kernel resource, for binding it to a composition surface */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    ShareBuffer(
        _In_ IUnknown* Buffer,
        _Out_ HANDLE* SharedHandle) = 0;

    /* The monitored fence a composition binding signals; Fence identifies it for DestroyFlipFence */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    CreateFlipFence(
        _Out_ UINT* Fence,
        _Out_ HANDLE* SharedHandle) = 0;

    virtual
    VOID
    STDMETHODCALLTYPE
    DestroyFlipFence(
        _In_ UINT Fence) = 0;

    /* The device's present limit semaphore: one count per queued flip, given back by the kernel */
    virtual
    HANDLE
    STDMETHODCALLTYPE
    GetPresentLimitSemaphore() = 0;

    /* Hands a flip model buffer to the driver; Context carries the redirected flip */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    PresentFlip(
        _In_ IUnknown* Buffer,
        _In_ RxPresentContext* Context) = 0;

    /* Shifts identities so Buffers[0] becomes what Buffers[1] was, and so on around */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    RotateBuffers(
        _In_reads_(Count) IUnknown* const* Buffers,
        _In_ UINT Count) = 0;

    /* Blts a buffer to a target, or to the window alone when Target is NULL */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    SubmitPresent(
        _In_ IUnknown* Buffer,
        _In_opt_ IUnknown* Target,
        _In_ RxPresentContext* Context) = 0;

    /* The kernel device, which is what owns a video present source */
    virtual
    D3DKMT_HANDLE
    STDMETHODCALLTYPE
    GetKernelDevice() = 0;

    /* One primary buffer the display can scan out from on VidPnSourceId */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    CreatePrimaryBuffer(
        _In_ const DXGI_SWAP_CHAIN_DESC1* Desc,
        _In_ const DXGI_DDI_PRIMARY_DESC* PrimaryDesc,
        _In_ IUnknown* Parent,
        _COM_Outptr_ IUnknown** Buffer) = 0;

    /* Makes a primary buffer the one the display scans out */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    SetDisplaySurface(
        _In_ IUnknown* Buffer) = 0;

    /* Flips a primary buffer onto the display at the next interval */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    PresentPrimary(
        _In_ IUnknown* Buffer,
        _In_ UINT FlipInterval,
        _In_ RxPresentContext* Context) = 0;

    /* Copies rectangles of Source into Destination, each moved by Offset */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    CopyBufferRegions(
        _In_ IUnknown* Destination,
        _In_ IUnknown* Source,
        _In_reads_(Count) const RECT* Rects,
        _In_ UINT Count,
        _In_ POINT Offset) = 0;

    /* The device's removal reason as GetDeviceRemovedReason reports it */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    GetRemovedReason() = 0;

    /*
     * Has the driver blt a buffer into a rectangle of a present target, resolving, converting
     * and stretching as needed, then flushes so a compositor reading the target sees it.
     */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    BltToTarget(
        _In_ IUnknown* Buffer,
        _In_ IUnknown* Target,
        _In_ const RECT* DestRect) = 0;

    /* The width and height a present target was opened with */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    GetTargetSize(
        _In_ IUnknown* Target,
        _Out_ UINT* Width,
        _Out_ UINT* Height) = 0;

    /* TRUE while a GDI DC from IDXGISurface1::GetDC on the buffer is not released */
    virtual
    BOOL
    STDMETHODCALLTYPE
    IsBufferDcHeld(
        _In_ IUnknown* Buffer) = 0;
};
