/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Presenting to the primary through the desktop window manager's redirection surface
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

#include <dxgiformat.h>
#include <vector>

/* dwmapi hands out a window's redirection surface by ordinal only */
typedef HRESULT (WINAPI* PDD_GET_WINDOW_SURFACE)(HWND, LUID, HMONITOR, DWORD, DXGI_FORMAT*, HANDLE*, UINT64*);
typedef NTSTATUS (APIENTRY* PDD_OPEN_RESOURCE2)(D3DKMT_OPENRESOURCE*);

static constexpr WORD DdGetWindowSurfaceOrdinal = 100;
static constexpr DWORD DdWindowSurfaceForBlt = 0x10;

/* Success codes DWM returns for a window it redirects through GDI */
static constexpr HRESULT DdDwmGdiRedirected = 0x00263005;
static constexpr HRESULT DdDwmBltThroughGdi = 0x00263008;

/* Kernel answers that mean the present went nowhere but nothing failed */
static constexpr NTSTATUS DdStatusRedirectionOff = static_cast<NTSTATUS>(0xC01E0342);
static constexpr NTSTATUS DdStatusPresentOccluded = static_cast<NTSTATUS>(0xC01E000D);
static constexpr NTSTATUS DdStatusDeviceGone = static_cast<NTSTATUS>(0xC01E0106);

/* Driver interfaces older than this want an explicit flip interval on blits */
static constexpr UINT DdFlipIntervalVersion = 0x2000;
static constexpr UINT DdPresentExtrasVersion = 0x5002;

/* Formats DWM and the driver both name, in DXGI and Direct3D terms */
static const struct
{
    DXGI_FORMAT Dxgi;
    D3DDDIFORMAT Direct3D;
} DdFormatPairs[] =
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
DdDxgiFromDirect3D(
    _In_ D3DDDIFORMAT Format)
{
    for (const auto& Pair : DdFormatPairs)
    {
        if (Pair.Direct3D == Format)
            return Pair.Dxgi;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static
D3DDDIFORMAT
DdDirect3DFromDxgi(
    _In_ DXGI_FORMAT Format)
{
    for (const auto& Pair : DdFormatPairs)
    {
        if (Pair.Dxgi == Format)
            return Pair.Direct3D;
    }
    return D3DDDIFMT_UNKNOWN;
}

static
D3DKMT_HANDLE
DdKernelContext(
    _In_opt_ HANDLE Context)
{
    return static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(Context));
}

static
PDD_GET_WINDOW_SURFACE
DdWindowSurfaceExport(VOID)
{
    static PDD_GET_WINDOW_SURFACE Export = nullptr;
    static BOOLEAN Resolved = FALSE;
    HMODULE Dwm;

    if (!Resolved)
    {
        Resolved = TRUE;
        Dwm = LoadLibraryW(L"dwmapi.dll");
        if (Dwm != NULL)
        {
            Export = reinterpret_cast<PDD_GET_WINDOW_SURFACE>(
                GetProcAddress(Dwm, MAKEINTRESOURCEA(DdGetWindowSurfaceOrdinal)));
        }
        if (Export == nullptr)
            DdTrace(L"dwmapi has no window surface export, primary blits go through GDI");
    }
    return Export;
}

/* One channel of a pixel scaled to eight bits, the way the runtime widens colors for the kernel */
static
DWORD
DdWidenChannel(
    _In_ DWORD Pixel,
    _In_ DWORD Mask)
{
    DWORD Shift = 0;

    if (Mask == 0)
        return 0;
    while (((Mask >> Shift) & 1) == 0)
        Shift++;
    return (((Pixel & Mask) >> Shift) * 255) / (Mask >> Shift);
}

static
DWORD
DdPixelToArgb(
    _In_ const DDPIXELFORMAT* Format,
    _In_ DWORD Pixel)
{
    DWORD Alpha = (Format->dwFlags & DDPF_ALPHAPIXELS) != 0 ? DdWidenChannel(Pixel, Format->dwRGBAlphaBitMask) : 0;

    return (Alpha << 24) | (DdWidenChannel(Pixel, Format->dwRBitMask) << 16) |
           (DdWidenChannel(Pixel, Format->dwGBitMask) << 8) | DdWidenChannel(Pixel, Format->dwBBitMask);
}

VOID
DdReleaseWindowSurface(
    _Inout_ DdHardware* Hardware)
{
    if (Hardware->WindowSurface == nullptr)
        return;

    if (Hardware->WindowSurface->DriverResource != NULL)
        Hardware->DeviceFunctions.pfnDestroyResource(Hardware->DriverDevice, Hardware->WindowSurface->DriverResource);
    delete Hardware->WindowSurface;
    Hardware->WindowSurface = nullptr;
    Hardware->WindowShare = 0;
}

/* Opens a resource another process shared by its global handle and hands it to the driver */
static
HRESULT
DdOpenSharedResource(
    _In_ DdHardware* Hardware,
    _In_ D3DKMT_HANDLE Share,
    _In_ BOOLEAN NeedsRuntimeData,
    _Inout_ DdResource* Resource)
{
    static PDD_OPEN_RESOURCE2 OpenResource2 =
        reinterpret_cast<PDD_OPEN_RESOURCE2>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTOpenResource2"));
    const KmtThunks* Kmt = RxGetKmtThunks();
    BOOLEAN Virtual = Hardware->RuntimeVersion >= D3D_UMD_INTERFACE_VERSION_WDDM2_0 && OpenResource2 != nullptr;
    D3DKMT_QUERYRESOURCEINFO Query = {};
    D3DKMT_OPENRESOURCE Open = {};
    D3DKMT_DESTROYALLOCATION Destroy = {};
    D3DDDIARG_OPENRESOURCE Args = {};
    std::vector<BYTE> Runtime;
    std::vector<BYTE> Driver;
    std::vector<BYTE> Total;
    std::vector<BYTE> Infos;
    NTSTATUS Status;
    HRESULT hr;

    Query.hDevice = Hardware->DeviceHandle;
    Query.hGlobalShare = Share;
    Status = Kmt->QueryResourceInfo(&Query);
    if (!NT_SUCCESS(Status))
    {
        DdTrace(L"D3DKMTQueryResourceInfo failed 0x%08lX for share 0x%X", Status, Share);
        return DdHresultFromStatus(Status);
    }

    /* Only a resource some runtime described can be adopted; the display allocation carries no such data */
    if (NeedsRuntimeData && Query.PrivateRuntimeDataSize < 56)
        return E_FAIL;

    Runtime.resize(Query.PrivateRuntimeDataSize);
    Driver.resize(Query.ResourcePrivateDriverDataSize);
    Total.resize(Query.TotalPrivateDriverDataSize);
    Infos.resize(Query.NumAllocations *
                 (Virtual ? sizeof(D3DDDI_OPENALLOCATIONINFO2) : sizeof(D3DDDI_OPENALLOCATIONINFO)));

    Open.hDevice = Hardware->DeviceHandle;
    Open.hGlobalShare = Share;
    Open.NumAllocations = Query.NumAllocations;
    Open.pOpenAllocationInfo = reinterpret_cast<D3DDDI_OPENALLOCATIONINFO*>(Infos.data());
    Open.pPrivateRuntimeData = Runtime.data();
    Open.PrivateRuntimeDataSize = Query.PrivateRuntimeDataSize;
    Open.pResourcePrivateDriverData = Driver.data();
    Open.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Open.pTotalPrivateDriverDataBuffer = Total.data();
    Open.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Status = Virtual ? OpenResource2(&Open) : Kmt->OpenResource(&Open);
    if (!NT_SUCCESS(Status))
    {
        DdTrace(L"D3DKMTOpenResource failed 0x%08lX for share 0x%X", Status, Share);
        return Status == DdStatusDeviceGone ? E_INVALIDARG : DdHresultFromStatus(Status);
    }

    Args.NumAllocations = Open.NumAllocations;
    Args.pOpenAllocationInfo = Open.pOpenAllocationInfo;
    Args.hKMResource = Open.hResource;
    Args.pPrivateDriverData = Driver.data();
    Args.PrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Args.hResource = Resource;
    hr = Hardware->DeviceFunctions.pfnOpenResource(Hardware->DriverDevice, &Args);
    if (FAILED(hr))
    {
        DdTrace(L"UMD OpenResource failed 0x%08lX for share 0x%X", hr, Share);
        Destroy.hDevice = Hardware->DeviceHandle;
        Destroy.hResource = Open.hResource;
        Kmt->DestroyAllocation(&Destroy);
        return hr;
    }

    Resource->DriverResource = Args.hResource;
    Resource->KernelResource = Open.hResource;
    return S_OK;
}

/* The window's redirection surface, opened once per share handle DWM gives out */
static
HRESULT
DdOpenWindowSurface(
    _Inout_ DdHardware* Hardware,
    _In_ D3DKMT_HANDLE Share,
    _In_ D3DDDIFORMAT Format)
{
    DdResource* Resource;
    HRESULT hr;

    if (Hardware->WindowSurface != nullptr && Hardware->WindowShare == Share)
        return S_OK;
    DdReleaseWindowSurface(Hardware);
    if (Share == 0)
        return S_OK;

    Resource = new (std::nothrow) DdResource;
    if (Resource == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = Format;
    Resource->Pool = D3DDDIPOOL_VIDEOMEMORY;
    Resource->Flags.RenderTarget = TRUE;
    Resource->Flags.NotLockable = TRUE;
    Resource->Flags.SharedResource = TRUE;
    Resource->Flags.Texture = TRUE;
    Resource->SurfaceCount = 1;
    Resource->MipLevels = 1;

    hr = DdOpenSharedResource(Hardware, Share, TRUE, Resource);
    if (FAILED(hr))
    {
        delete Resource;
        return hr;
    }

    Hardware->WindowSurface = Resource;
    Hardware->WindowShare = Share;
    return S_OK;
}

/* Opens the display allocation when the kernel shares one; a composed desktop usually has none and the primary waits */
HRESULT
DdOpenDisplayPrimary(
    _Inout_ DdSurface* Primary)
{
    static PFND3DKMT_GETSHAREDPRIMARYHANDLE GetShared = reinterpret_cast<PFND3DKMT_GETSHAREDPRIMARYHANDLE>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTGetSharedPrimaryHandle"));
    DdHardware* Hardware = Primary->Driver->Hardware;
    D3DKMT_GETSHAREDPRIMARYHANDLE Query = {};
    DdResource* Resource;
    HRESULT hr;

    if (Hardware == nullptr || GetShared == nullptr || Primary->SharedPrimary != nullptr)
        return S_OK;

    Query.hAdapter = Hardware->AdapterHandle;
    Query.VidPnSourceId = Hardware->VidPnSourceId;
    if (!NT_SUCCESS(GetShared(&Query)) || Query.hSharedPrimary == 0)
        return S_OK;

    Resource = new (std::nothrow) DdResource;
    if (Resource == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = DdDriverFormat(&Primary->Shared->Format);
    Resource->Pool = D3DDDIPOOL_VIDEOMEMORY;
    Resource->Flags.Primary = TRUE;
    Resource->Flags.SharedResource = TRUE;
    Resource->SurfaceCount = 1;
    Resource->MipLevels = 1;

    hr = DdOpenSharedResource(Hardware, Query.hSharedPrimary, FALSE, Resource);
    if (FAILED(hr))
    {
        delete Resource;
        return hr;
    }
    Primary->SharedPrimary = Resource;
    return S_OK;
}

VOID
DdCloseDisplayPrimary(
    _Inout_ DdSurface* Primary)
{
    DdHardware* Hardware = Primary->Driver->Hardware;

    if (Primary->SharedPrimary == nullptr)
        return;
    if (Primary->SharedPrimary->DriverResource != NULL)
        Hardware->DeviceFunctions.pfnDestroyResource(Hardware->DriverDevice, Primary->SharedPrimary->DriverResource);
    delete Primary->SharedPrimary;
    Primary->SharedPrimary = nullptr;
}

static
VOID
DdFlushDriver(
    _In_ DdHardware* Hardware)
{
    if (Hardware->UsesFlush1)
        Hardware->DeviceFunctions.pfnFlush1(Hardware->DriverDevice, 0);
    else
        Hardware->DeviceFunctions.pfnFlush(Hardware->DriverDevice);
}

/* Copies the source rectangle into a video memory resource the size of the target rectangle */
static
HRESULT
DdStageInVideoMemory(
    _In_ DdHardware* Hardware,
    _In_ DdSurface* Source,
    _In_ const RECT* From,
    _In_ const RECT* To,
    _Out_ DdResource* Staging)
{
    D3DDDIARG_BLT Blt = {};
    HRESULT hr;

    hr = DdCreateStagingResource(Hardware, Source->Shared->SystemResource->Format, To->right - To->left,
                                 To->bottom - To->top, Staging);
    if (FAILED(hr))
        return hr;

    Blt.hSrcResource = Source->Shared->SystemResource->DriverResource;
    Blt.SrcRect = *From;
    Blt.hDstResource = Staging->DriverResource;
    SetRect(&Blt.DstRect, 0, 0, To->right - To->left, To->bottom - To->top);
    hr = Hardware->DeviceFunctions.pfnBlt(Hardware->DriverDevice, &Blt);
    if (SUCCEEDED(hr))
        DdFlushDriver(Hardware);
    if (FAILED(hr))
    {
        DdTrace(L"UMD Blt into a staging copy failed 0x%08lX", hr);
        Hardware->DeviceFunctions.pfnDestroyResource(Hardware->DriverDevice, Staging->DriverResource);
        Staging->DriverResource = NULL;
    }
    return hr;
}

/* Lets the driver present; it calls back into DdPresentCb with the allocations it chose */
static
HRESULT
DdDriverPresent(
    _Inout_ DdHardware* Hardware,
    _In_opt_ HANDLE SourceResource,
    _In_opt_ HANDLE TargetResource,
    _In_ UINT FlipInterval)
{
    D3DDDIARG_PRESENTSURFACE Surface = {};
    D3DDDIARG_PRESENT1 Present1 = {};
    D3DDDIARG_PRESENT Present = {};
    HRESULT hr;

    if (Hardware->UsesFlush1)
    {
        Surface.hResource = SourceResource;
        Present1.phSrcResources = SourceResource != NULL ? &Surface : nullptr;
        Present1.SrcResources = SourceResource != NULL ? 1 : 0;
        Present1.hDstResource = TargetResource;
        Present1.Flags.Value = Hardware->Present.Flags.Value & 7;
        Present1.FlipInterval = static_cast<D3DDDI_FLIPINTERVAL_TYPE>(FlipInterval);
        hr = Hardware->DeviceFunctions.pfnPresent1(Hardware->DriverDevice, &Present1);
    }
    else
    {
        Present.hSrcResource = SourceResource;
        Present.hDstResource = TargetResource;
        Present.Flags.Value = Hardware->Present.Flags.Value & 7;
        Present.FlipInterval = static_cast<D3DDDI_FLIPINTERVAL_TYPE>(FlipInterval);
        hr = Hardware->DeviceFunctions.pfnPresent(Hardware->DriverDevice, &Present);
    }

    ZeroMemory(&Hardware->Present, sizeof(Hardware->Present));
    if (FAILED(hr))
        DdTrace(L"UMD Present failed 0x%08lX", hr);
    return hr;
}

HRESULT
APIENTRY
DdPresentCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_PRESENT* Args)
{
    DdHardware* Hardware = static_cast<DdHardware*>(hDevice);
    D3DKMT_PRESENT* Present = &Hardware->Present;
    NTSTATUS Status;
    UINT Index;

    if (Hardware->Lost)
        return S_OK;
    if ((Present->Flags.Value & 7) == 0)
        return E_INVALIDARG;

    Present->hContext = Hardware->DriverVersion <= 1 ? Hardware->ContextHandle : DdKernelContext(Args->hContext);
    Present->hSource = Args->hSrcAllocation;
    Present->hDestination = Args->hDstAllocation;
    Present->BroadcastContextCount = 0;
    if (Hardware->DriverVersion > 4 && Args->BroadcastContextCount <= D3DDDI_MAX_BROADCAST_CONTEXT)
    {
        Present->BroadcastContextCount = Args->BroadcastContextCount;
        for (Index = 0; Index < Args->BroadcastContextCount; Index++)
            Present->BroadcastContext[Index] = DdKernelContext(Args->BroadcastContext[Index]);
    }
    if (Hardware->DriverVersion >= DdPresentExtrasVersion)
    {
        Present->BroadcastSrcAllocation = Args->BroadcastSrcAllocation;
        Present->BroadcastDstAllocation = Args->BroadcastDstAllocation;
        Present->PrivateDriverDataSize = Args->PrivateDriverDataSize;
        Present->pPrivateDriverData = Args->pPrivateDriverData;
    }

    Status = RxGetKmtThunks()->Present(Present);
    if (Hardware->DriverVersion >= DdPresentExtrasVersion)
        Args->bOptimizeForComposition = Present->bOptimizeForComposition;
    if (NT_SUCCESS(Status) || Status == DdStatusRedirectionOff || Status == DdStatusPresentOccluded)
        return S_OK;

    DdTrace(L"D3DKMTPresent failed 0x%08lX", Status);
    if (Status == DdStatusDeviceGone || Status == STATUS_DEVICE_REMOVED)
    {
        DdMarkDeviceLost(Hardware);
        return S_OK;
    }
    return DdHresultFromStatus(Status);
}

/* The hardware scheduling form of DdPresentCb: one source per queue, all handed to the kernel at once */
HRESULT
APIENTRY
DdSubmitPresentToHwQueueCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SUBMITPRESENTTOHWQUEUE* Args)
{
    DdHardware* Hardware = static_cast<DdHardware*>(hDevice);
    D3DKMT_HANDLE Queues[D3DDDI_MAX_BROADCAST_CONTEXT + 1];
    D3DKMT_SUBMITPRESENTTOHWQUEUE Submit = {};
    D3DKMT_PRESENT* Present = &Hardware->Present;
    NTSTATUS Status;
    UINT Index;

    if (Hardware->Lost)
        return S_OK;
    if ((Present->Flags.Value & 7) == 0 || Args->BroadcastHwQueueCount == 0 ||
        Args->BroadcastHwQueueCount > D3DDDI_MAX_BROADCAST_CONTEXT + 1)
    {
        return E_INVALIDARG;
    }
    if (RxGetKmtThunks()->SubmitPresentToHwQueue == nullptr)
        return E_NOTIMPL;

    for (Index = 0; Index < Args->BroadcastHwQueueCount; Index++)
        Queues[Index] = DdKernelContext(Args->hHwQueues[Index]);

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
    if (NT_SUCCESS(Status) || Status == DdStatusRedirectionOff || Status == DdStatusPresentOccluded)
        return S_OK;

    DdTrace(L"D3DKMTSubmitPresentToHwQueue failed 0x%08lX", Status);
    if (Status == DdStatusDeviceGone || Status == STATUS_DEVICE_REMOVED)
    {
        DdMarkDeviceLost(Hardware);
        return S_OK;
    }
    return DdHresultFromStatus(Status);
}

/* Blits and fills to the primary land in the window's redirection surface as a redirected blt present */
HRESULT
DdPresentThroughDwm(
    _In_ DdSurface* Primary,
    _In_ HWND Window,
    _In_ const RECT* To,
    _In_opt_ DdSurface* Source,
    _In_opt_ const RECT* From,
    _In_ BOOLEAN Fill,
    _In_ DWORD FillValue,
    _Out_ BOOLEAN* Handled)
{
    PDD_GET_WINDOW_SURFACE GetWindowSurface = DdWindowSurfaceExport();
    DdHardware* Hardware = Primary->Driver->Hardware;
    D3DKMT_PRESENT* Present = &Hardware->Present;
    DXGI_FORMAT Format = DdDxgiFromDirect3D(DdDriverFormat(&Primary->Shared->Format));
    WINDOWINFO Info = {};
    HANDLE Share = NULL;
    UINT64 UpdateId = 0;
    HANDLE SourceResource = NULL;
    HANDLE TargetResource = NULL;
    DdResource Staging = {};
    BOOLEAN ThroughGdi;
    BOOLEAN Stretched;
    HRESULT hr;

    *Handled = FALSE;
    if (GetWindowSurface == nullptr || Hardware->DriverDevice == NULL)
        return S_OK;

    hr = GetWindowSurface(Window, Hardware->AdapterLuid, NULL, DdWindowSurfaceForBlt, &Format, &Share, &UpdateId);
    if (FAILED(hr))
    {
        DD_TRACE_ONCE(L"DWM gave no redirection surface (0x%08lX), primary blits go through GDI", hr);
        return S_OK;
    }
    if (hr != DdDwmGdiRedirected && hr != DdDwmBltThroughGdi)
    {
        DD_TRACE_ONCE(L"DWM answered 0x%08lX for the window surface, primary blits go through GDI", hr);
        return S_OK;
    }

    /* A window DWM has not given a surface yet takes nothing; the blit still succeeds */
    *Handled = TRUE;
    ThroughGdi = hr == DdDwmBltThroughGdi;
    if (ThroughGdi)
        Share = NULL;
    else if (Share == NULL)
        return S_OK;

    hr = DdOpenWindowSurface(Hardware, static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(Share)),
                             DdDirect3DFromDxgi(Format));
    if (FAILED(hr))
        return hr;
    if (Hardware->WindowSurface != nullptr)
        TargetResource = Hardware->WindowSurface->DriverResource;

    if (!Fill && Source->Resource == nullptr)
    {
        hr = DdCreateSystemResource(Hardware, Source);
        if (FAILED(hr))
            return E_FAIL;
    }

    DdFlushDriver(Hardware);

    Info.cbSize = sizeof(Info);
    if (!GetWindowInfo(Window, &Info))
        return GetLastError() == ERROR_INVALID_WINDOW_HANDLE ? HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE) : S_OK;

    /* The kernel wants the rectangle relative to the window's client area */
    ZeroMemory(Present, sizeof(*Present));
    Present->hWindow = Window;
    Present->DstRect = *To;
    OffsetRect(&Present->DstRect, Primary->Driver->DeviceRect.left - Info.rcClient.left,
               Primary->Driver->DeviceRect.top - Info.rcClient.top);
    Present->Flags.DstRectValid = TRUE;
    Present->Flags.RedirectedBlt = TRUE;
    Present->SubRectCnt = 1;
    Present->PresentHistoryToken.Model = D3DKMT_PM_REDIRECTED_BLT;
    Present->PresentHistoryToken.Token.Blt.hPhysicalSurface = reinterpret_cast<ULONG_PTR>(Share);
    Present->PresentHistoryToken.Token.Blt.EventId = UpdateId;

    if (Fill)
    {
        Present->Flags.ColorFill = TRUE;
        Present->Color = DdPixelToArgb(&Primary->Shared->Format, FillValue);
        Present->pSrcSubRects = &Present->DstRect;
        hr = DdDriverPresent(Hardware, NULL, TargetResource, 0);
    }
    else
    {
        SourceResource = Source->Resource != nullptr ? Source->Resource->DriverResource
                                                     : Source->Shared->SystemResource->DriverResource;
        Present->Flags.Blt = TRUE;
        Present->Flags.SrcRectValid = TRUE;
        Present->SrcRect = *From;
        Present->pSrcSubRects = &Present->SrcRect;

        /* The driver cannot stretch out of system memory while presenting, so it first copies to video memory */
        Stretched = To->right - To->left != From->right - From->left || To->bottom - To->top != From->bottom - From->top;
        if (Source->Resource == nullptr && Stretched)
        {
            hr = DdStageInVideoMemory(Hardware, Source, From, To, &Staging);
            if (FAILED(hr))
            {
                ZeroMemory(Present, sizeof(*Present));
                return hr;
            }
            SetRect(&Present->SrcRect, 0, 0, To->right - To->left, To->bottom - To->top);
            SourceResource = Staging.DriverResource;
        }

        hr = DdDriverPresent(Hardware, SourceResource, TargetResource,
                             Hardware->DriverVersion < DdFlipIntervalVersion ? 1 : 0);
        if (Staging.DriverResource != NULL)
            Hardware->DeviceFunctions.pfnDestroyResource(Hardware->DriverDevice, Staging.DriverResource);
    }

    if (Hardware->Lost)
        return DDERR_SURFACELOST;
    return hr;
}
