/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Windowed presents through the desktop window manager's redirection surface
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <dxgiformat.h>
#include <vector>

/* dwmapi hands out a window's redirection surface by ordinal only */
typedef HRESULT (WINAPI* PD9_GET_WINDOW_SURFACE)(HWND, LUID, HMONITOR, DWORD, DXGI_FORMAT*, HANDLE*, UINT64*);
typedef NTSTATUS (APIENTRY* PD9_CHECK_OCCLUSION)(const D3DKMT_CHECKOCCLUSION*);

static constexpr WORD D9GetWindowSurfaceOrdinal = 100;
static constexpr DWORD D9WindowSurfaceForBlt = 0x10;

/* Success codes DWM returns for a window it redirects */
static constexpr HRESULT D9DwmRedirected = 0x00263005;
static constexpr HRESULT D9DwmBltThroughGdi = 0x00263008;

/* Kernel answers that mean the present went nowhere but nothing failed */
static constexpr NTSTATUS D9StatusRedirectionOff = static_cast<NTSTATUS>(0xC01E0342);
static constexpr NTSTATUS D9StatusPresentOccluded = static_cast<NTSTATUS>(0xC01E0006);
static constexpr NTSTATUS D9StatusPresentDenied = static_cast<NTSTATUS>(0xC01E000D);
static constexpr NTSTATUS D9StatusDeviceGone = static_cast<NTSTATUS>(0xC01E0106);

/* Driver interfaces older than this want an explicit flip interval on blits */
static constexpr UINT D9FlipIntervalVersion = 0x2000;
static constexpr UINT D9PresentExtrasVersion = 0x5002;

/* Formats DWM and the driver both name, in DXGI and Direct3D terms */
static const struct
{
    DXGI_FORMAT Dxgi;
    D3DDDIFORMAT Direct3D;
} D9FormatPairs[] =
{
    { DXGI_FORMAT_B8G8R8A8_UNORM, D3DDDIFMT_A8R8G8B8 },
    { DXGI_FORMAT_B8G8R8X8_UNORM, D3DDDIFMT_X8R8G8B8 },
    { DXGI_FORMAT_R8G8B8A8_UNORM, D3DDDIFMT_A8B8G8R8 },
    { DXGI_FORMAT_B5G6R5_UNORM, D3DDDIFMT_R5G6B5 },
    { DXGI_FORMAT_B5G5R5A1_UNORM, D3DDDIFMT_A1R5G5B5 },
    { DXGI_FORMAT_R10G10B10A2_UNORM, D3DDDIFMT_A2B10G10R10 },
    { DXGI_FORMAT_R16G16B16A16_FLOAT, D3DDDIFMT_A16B16G16R16F },
};

static
DXGI_FORMAT
D9DxgiFromDirect3D(
    _In_ D3DDDIFORMAT Format)
{
    for (const auto& Pair : D9FormatPairs)
    {
        if (Pair.Direct3D == Format)
            return Pair.Dxgi;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static
D3DDDIFORMAT
D9Direct3DFromDxgi(
    _In_ DXGI_FORMAT Format)
{
    for (const auto& Pair : D9FormatPairs)
    {
        if (Pair.Dxgi == Format)
            return Pair.Direct3D;
    }
    return D3DDDIFMT_UNKNOWN;
}

static
D3DKMT_HANDLE
D9KernelContext(
    _In_opt_ HANDLE Context)
{
    return static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(Context));
}

static
PD9_GET_WINDOW_SURFACE
D9WindowSurfaceExport(VOID)
{
    static PD9_GET_WINDOW_SURFACE Export = nullptr;
    static BOOLEAN Resolved = FALSE;
    HMODULE Dwm;

    if (!Resolved)
    {
        Resolved = TRUE;
        Dwm = LoadLibraryW(L"dwmapi.dll");
        if (Dwm != NULL)
            Export = reinterpret_cast<PD9_GET_WINDOW_SURFACE>(GetProcAddress(Dwm, MAKEINTRESOURCEA(D9GetWindowSurfaceOrdinal)));
        if (Export == nullptr)
            D9Trace(L"dwmapi has no window surface export, windowed presents go nowhere");
    }
    return Export;
}

/* Whether the kernel says nothing of the window can be seen */
HRESULT
D9CheckOcclusion(
    _In_ HWND Window)
{
    static PD9_CHECK_OCCLUSION CheckOcclusion = reinterpret_cast<PD9_CHECK_OCCLUSION>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTCheckOcclusion"));
    D3DKMT_CHECKOCCLUSION Check = {};
    NTSTATUS Status;

    if (CheckOcclusion == nullptr)
        return S_OK;

    Check.hWindow = Window;
    Status = CheckOcclusion(&Check);
    if (Status == D9StatusPresentOccluded)
        return S_PRESENT_OCCLUDED;
    if (Status == STATUS_INVALID_PARAMETER)
        return D3DERR_INVALIDCALL;
    return NT_SUCCESS(Status) ? S_OK : D9HresultFromStatus(Status);
}

VOID
D9Device::ReleaseWindowSurface(VOID)
{
    if (WindowSurface == nullptr)
        return;

    if (WindowSurface->DriverResource != NULL)
        DrainBatch();
        DeviceFunctions.pfnDestroyResource(DriverDevice, WindowSurface->DriverResource);
    delete WindowSurface;
    WindowSurface = nullptr;
    WindowShare = 0;
}

/* The window's redirection surface, opened once for each share handle DWM gives out; native drivers take
   the first open with its first allocation records */
HRESULT
D9Device::OpenWindowSurface(
    _In_ D3DKMT_HANDLE Share,
    _In_ D3DDDIFORMAT Format)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_QUERYRESOURCEINFO Query = {};
    D3DKMT_OPENRESOURCE Open = {};
    D3DKMT_DESTROYALLOCATION Destroy = {};
    D3DDDIARG_OPENRESOURCE Args = {};
    std::vector<BYTE> Runtime;
    std::vector<BYTE> Driver;
    std::vector<BYTE> Total;
    std::vector<BYTE> Infos;
    D9Resource* Resource;
    NTSTATUS Status;
    HRESULT hr;

    if (WindowSurface != nullptr && WindowShare == Share)
        return S_OK;
    ReleaseWindowSurface();
    if (Share == 0)
        return S_OK;

    Query.hDevice = DeviceHandle;
    Query.hGlobalShare = Share;
    Status = Kmt->QueryResourceInfo(&Query);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTQueryResourceInfo failed 0x%08lX for the window surface", Status);
        return D9HresultFromStatus(Status);
    }

    Runtime.resize(Query.PrivateRuntimeDataSize);
    Driver.resize(Query.ResourcePrivateDriverDataSize);
    Total.resize(Query.TotalPrivateDriverDataSize);
    Infos.resize(Query.NumAllocations * sizeof(D3DDDI_OPENALLOCATIONINFO));

    Open.hDevice = DeviceHandle;
    Open.hGlobalShare = Share;
    Open.NumAllocations = Query.NumAllocations;
    Open.pOpenAllocationInfo = reinterpret_cast<D3DDDI_OPENALLOCATIONINFO*>(Infos.data());
    Open.pPrivateRuntimeData = Runtime.data();
    Open.PrivateRuntimeDataSize = Query.PrivateRuntimeDataSize;
    Open.pResourcePrivateDriverData = Driver.data();
    Open.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Open.pTotalPrivateDriverDataBuffer = Total.data();
    Open.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Status = Kmt->OpenResource(&Open);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTOpenResource failed 0x%08lX for the window surface", Status);
        return D9HresultFromStatus(Status);
    }

    Resource = new (std::nothrow) D9Resource;
    if (Resource == nullptr)
    {
        Destroy.hDevice = DeviceHandle;
        Destroy.hResource = Open.hResource;
        Kmt->DestroyAllocation(&Destroy);
        return E_OUTOFMEMORY;
    }
    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = Format;
    Resource->Pool = D3DDDIPOOL_VIDEOMEMORY;
    Resource->Flags.RenderTarget = TRUE;
    Resource->Flags.NotLockable = TRUE;
    Resource->Flags.SharedResource = TRUE;
    Resource->Flags.Texture = TRUE;
    Resource->KernelResource = Open.hResource;

    Args.NumAllocations = Open.NumAllocations;
    Args.pOpenAllocationInfo = Open.pOpenAllocationInfo;
    Args.hKMResource = Open.hResource;
    Args.pPrivateDriverData = Driver.data();
    Args.PrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Args.hResource = Resource;
    Args.Rotation = D3DDDI_ROTATION_IDENTITY;
    DrainBatch();
    hr = DeviceFunctions.pfnOpenResource(DriverDevice, &Args);
    if (FAILED(hr))
    {
        D9Trace(L"UMD OpenResource failed 0x%08lX for the window surface", hr);
        Destroy.hDevice = DeviceHandle;
        Destroy.hResource = Open.hResource;
        Kmt->DestroyAllocation(&Destroy);
        delete Resource;
        return hr;
    }

    Resource->DriverResource = Args.hResource;
    WindowSurface = Resource;
    WindowShare = Share;
    return S_OK;
}

VOID
D9Device::FlushDriver(VOID)
{
    DrainBatch();
    if (UsesFlush1)
        DeviceFunctions.pfnFlush1(DriverDevice, 0);
    else
        DeviceFunctions.pfnFlush(DriverDevice);
}

/* Keeps a copy of the present being handed to the driver; the oldest is dropped when the queue is full */
VOID
D9Device::QueuePendingPresent(VOID)
{
    D3DKMT_PRESENT* Slot;

    AcquireSRWLockExclusive(&PresentQueueLock);
    if (QueuedPresentCount == D9QueuedPresentLimit)
    {
        QueuedPresentFirst = (QueuedPresentFirst + 1) % D9QueuedPresentLimit;
        QueuedPresentCount -= 1;
    }
    Slot = &QueuedPresents[(QueuedPresentFirst + QueuedPresentCount) % D9QueuedPresentLimit];
    *Slot = PendingPresent;
    QueuedPresentCount += 1;
    ReleaseSRWLockExclusive(&PresentQueueLock);
}

/* The oldest present the driver has not reported yet; its sub rectangle is its own source rectangle */
BOOLEAN
D9Device::TakeQueuedPresent(
    _Out_ D3DKMT_PRESENT* Present)
{
    BOOLEAN Found = FALSE;

    AcquireSRWLockExclusive(&PresentQueueLock);
    if (QueuedPresentCount != 0)
    {
        *Present = QueuedPresents[QueuedPresentFirst];
        QueuedPresentFirst = (QueuedPresentFirst + 1) % D9QueuedPresentLimit;
        QueuedPresentCount -= 1;
        Found = TRUE;
    }
    ReleaseSRWLockExclusive(&PresentQueueLock);

    if (!Found)
    {
        ZeroMemory(Present, sizeof(*Present));
        return FALSE;
    }
    if (Present->SubRectCnt != 0)
        Present->pSrcSubRects = &Present->SrcRect;
    return TRUE;
}

/* Lets the driver present; it calls back into PresentCallback with the allocations it chose */
HRESULT
D9Device::DriverPresent(
    _In_opt_ HANDLE SourceResource,
    _In_opt_ HANDLE TargetResource)
{
    UINT Interval = DriverVersion < D9FlipIntervalVersion ? 1 : 0;
    D3DDDIARG_PRESENTSURFACE Surface = {};
    D3DDDIARG_PRESENT1 Present1 = {};
    D3DDDIARG_PRESENT Present = {};
    HRESULT hr;

    /* A driver that threads reports the present from its worker after this call returns */
    PresentResult = S_OK;
    QueuePendingPresent();
    if (UsesFlush1)
    {
        Surface.hResource = SourceResource;
        Present1.phSrcResources = &Surface;
        Present1.SrcResources = 1;
        Present1.hDstResource = TargetResource;
        Present1.Flags.Value = PendingPresent.Flags.Value & 7;
        Present1.FlipInterval = static_cast<D3DDDI_FLIPINTERVAL_TYPE>(Interval);
        Present1.BackBufferMultiplicity = 1;
        hr = DdiPresent1(&Present1) ? S_OK : DeviceFunctions.pfnPresent1(DriverDevice, &Present1);
    }
    else
    {
        Present.hSrcResource = SourceResource;
        Present.hDstResource = TargetResource;
        Present.Flags.Value = PendingPresent.Flags.Value & 7;
        Present.FlipInterval = static_cast<D3DDDI_FLIPINTERVAL_TYPE>(Interval);
        hr = DeviceFunctions.pfnPresent(DriverDevice, &Present);
    }

    ZeroMemory(&PendingPresent, sizeof(PendingPresent));
    if (FAILED(hr))
    {
        D9Trace(L"UMD Present failed 0x%08lX", hr);
        return hr;
    }
    return PresentResult;
}

/* What a failed kernel present means for the application; the plain object passes the status through */
static
HRESULT
D9PresentResult(
    _In_ NTSTATUS Status,
    _In_ BOOLEAN IsEx)
{
    if (!IsEx)
        return D9HresultFromStatus(Status);

    switch (static_cast<ULONG>(Status))
    {
    case 0xC01E0000:
    case 0xC01E0006:
    case 0xC01E0007:
    case 0xC01E000B:
    case 0xC01E000F:
        return S_PRESENT_OCCLUDED;
    case 0xC01E0005:
        return S_PRESENT_MODE_CHANGED;
    default:
        D9Trace(L"D3DKMTPresent failed 0x%08lX", Status);
        return D9HresultFromStatus(Status);
    }
}

/* The driver always hears success; what the kernel said becomes the result of Present */
HRESULT
D9Device::NotePresentStatus(
    _In_ NTSTATUS Status)
{
    if (NT_SUCCESS(Status) || Status == D9StatusRedirectionOff || Status == D9StatusPresentDenied)
        return S_OK;

    if (Status == D9StatusDeviceGone || Status == STATUS_DEVICE_REMOVED)
    {
        Lost = TRUE;
        PresentResult = D3DERR_DEVICELOST;
        return S_OK;
    }
    PresentResult = D9PresentResult(Status, IsEx);
    return S_OK;
}

/* Scans a primary out through the kernel; windowed devices never set a mode, so the mode fields stay zero */
HRESULT
D9Device::SetDisplayModeCallback(
    _Inout_ D3DDDICB_SETDISPLAYMODE* Args)
{
    D3DKMT_SETDISPLAYMODE Mode = {};
    NTSTATUS Status;

    if (Lost)
        return S_OK;

    Mode.hDevice = DeviceHandle;
    Mode.hPrimaryAllocation = Args->hPrimaryAllocation;
    Status = RxGetKmtThunks()->SetDisplayMode(&Mode);
    if (NT_SUCCESS(Status))
        return S_OK;

    D9Trace(L"D3DKMTSetDisplayMode failed 0x%08lX", Status);
    if (Status == STATUS_GRAPHICS_INCOMPATIBLE_PRIVATE_FORMAT && DriverVersion >= 6)
    {
        Args->PrivateDriverFormatAttribute = Mode.PrivateDriverFormatAttribute;
        return D3DDDIERR_INCOMPATIBLEPRIVATEFORMAT;
    }
    return S_OK;
}

HRESULT
D9Device::PresentCallback(
    _Inout_ D3DDDICB_PRESENT* Args)
{
    D3DKMT_PRESENT Queued;
    D3DKMT_PRESENT* Present = &Queued;
    NTSTATUS Status;

    if (Lost)
        return S_OK;
    if (!TakeQueuedPresent(&Queued) || (Present->Flags.Value & 7) == 0)
        return E_INVALIDARG;

    Present->hContext = DriverVersion <= 1 ? ContextHandle : D9KernelContext(Args->hContext);
    Present->hSource = Args->hSrcAllocation;
    Present->hDestination = Args->hDstAllocation;
    Present->BroadcastContextCount = 0;
    if (DriverVersion > 4 && Args->BroadcastContextCount <= D3DDDI_MAX_BROADCAST_CONTEXT)
    {
        Present->BroadcastContextCount = Args->BroadcastContextCount;
        for (UINT Index = 0; Index < Args->BroadcastContextCount; Index++)
            Present->BroadcastContext[Index] = D9KernelContext(Args->BroadcastContext[Index]);
    }
    if (DriverVersion >= D9PresentExtrasVersion)
    {
        Present->BroadcastSrcAllocation = Args->BroadcastSrcAllocation;
        Present->BroadcastDstAllocation = Args->BroadcastDstAllocation;
        Present->PrivateDriverDataSize = Args->PrivateDriverDataSize;
        Present->pPrivateDriverData = Args->pPrivateDriverData;
    }

    Status = RxGetKmtThunks()->Present(Present);
    if (DriverVersion >= D9PresentExtrasVersion)
        Args->bOptimizeForComposition = Present->bOptimizeForComposition;
    return NotePresentStatus(Status);
}

/* The hardware scheduling form of PresentCallback: one source per queue, all handed to the kernel at once */
HRESULT
D9Device::SubmitPresentCallback(
    _Inout_ D3DDDICB_SUBMITPRESENTTOHWQUEUE* Args)
{
    D3DKMT_HANDLE Queues[D3DDDI_MAX_BROADCAST_CONTEXT + 1];
    D3DKMT_SUBMITPRESENTTOHWQUEUE Submit = {};
    D3DKMT_PRESENT Queued;
    D3DKMT_PRESENT* Present = &Queued;
    NTSTATUS Status;

    if (Lost)
        return S_OK;
    if (!TakeQueuedPresent(&Queued) || (Present->Flags.Value & 7) == 0 || Args->BroadcastHwQueueCount == 0 ||
        Args->BroadcastHwQueueCount > D3DDDI_MAX_BROADCAST_CONTEXT + 1)
    {
        return E_INVALIDARG;
    }
    if (RxGetKmtThunks()->SubmitPresentToHwQueue == nullptr)
        return E_NOTIMPL;

    for (UINT Index = 0; Index < Args->BroadcastHwQueueCount; Index++)
        Queues[Index] = D9KernelContext(Args->hHwQueues[Index]);

    Present->PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Present->pPrivateDriverData = Args->pPrivateDriverData;
    Present->BroadcastContextCount = Args->BroadcastHwQueueCount - 1;
    Present->hSource = Args->BroadcastSrcAllocations[0];
    if (Args->BroadcastDstAllocations != nullptr)
        Present->hDestination = Args->BroadcastDstAllocations[0];
    if (Args->BroadcastHwQueueCount > 1)
    {
        Present->BroadcastSrcAllocation = Args->BroadcastSrcAllocations + 1;
        if (Args->BroadcastDstAllocations != nullptr)
            Present->BroadcastDstAllocation = Args->BroadcastDstAllocations + 1;
    }

    Submit.hHwQueues = Queues;
    Submit.PrivatePresentData = *Present;
    Status = RxGetKmtThunks()->SubmitPresentToHwQueue(&Submit);
    Args->bOptimizeForComposition = Submit.PrivatePresentData.bOptimizeForComposition;
    return NotePresentStatus(Status);
}

/* A windowed present: the back buffer rectangle is blitted into the window's redirection surface */
HRESULT
D9Device::PresentToWindow(
    _In_ D9Resource* Source,
    _In_ HWND Window,
    _In_ const RECT* From,
    _In_ const RECT* To)
{
    PD9_GET_WINDOW_SURFACE GetWindowSurface = D9WindowSurfaceExport();
    DXGI_FORMAT Format = D9DxgiFromDirect3D(Source->Format);
    D3DKMT_PRESENT* Present = &PendingPresent;
    WINDOWINFO Info = {};
    HANDLE Share = NULL;
    UINT64 UpdateId = 0;
    HRESULT hr;

    if (GetWindowSurface == nullptr)
        return S_OK;

    hr = GetWindowSurface(Window, Adapter->AdapterLuid, NULL, D9WindowSurfaceForBlt, &Format, &Share, &UpdateId);
    if (FAILED(hr))
    {
        D9_TRACE_ONCE(L"DWM gave no redirection surface (0x%08lX)", hr);
        return S_OK;
    }

    /* Anything but a redirected window, or a redirected one without a surface, only learns whether it is seen */
    if ((Share != NULL && hr != D9DwmRedirected && hr != D9DwmBltThroughGdi) ||
        (Share == NULL && hr != D9DwmBltThroughGdi))
    {
        return D9CheckOcclusion(Window) == S_PRESENT_OCCLUDED ? S_PRESENT_OCCLUDED : S_OK;
    }
    if (hr == D9DwmBltThroughGdi)
        Share = NULL;

    hr = OpenWindowSurface(static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(Share)), D9Direct3DFromDxgi(Format));
    if (FAILED(hr))
        return hr;

    FlushDriver();

    Info.cbSize = sizeof(Info);
    if (!GetWindowInfo(Window, &Info))
        return GetLastError() == ERROR_INVALID_WINDOW_HANDLE ? D3DERR_INVALIDCALL : S_OK;

    ZeroMemory(Present, sizeof(*Present));
    Present->hWindow = Window;
    Present->DstRect = *To;
    Present->SrcRect = *From;
    Present->pSrcSubRects = &Present->SrcRect;
    Present->SubRectCnt = 1;
    Present->Flags.Blt = TRUE;
    Present->Flags.SrcRectValid = TRUE;
    Present->Flags.DstRectValid = TRUE;
    Present->Flags.RedirectedBlt = TRUE;
    Present->PresentHistoryToken.Model = D3DKMT_PM_REDIRECTED_BLT;
    Present->PresentHistoryToken.Token.Blt.hPhysicalSurface = reinterpret_cast<ULONG_PTR>(Share);
    Present->PresentHistoryToken.Token.Blt.EventId = UpdateId;

    /* The driver is flushed on both sides of the present */
    hr = DriverPresent(Source->DriverResource, WindowSurface != nullptr ? WindowSurface->DriverResource : NULL);
    FlushDriver();
    if (Lost)
        return D3DERR_DEVICELOST;
    return hr;
}
