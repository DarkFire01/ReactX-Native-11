/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDXGIFactory: adapter enumeration and swapchain creation
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxgiobjects.hpp"
#include "swapchain.hpp"
#include "dwmswapchain.hpp"

#include <corewindow.h>
#include <algorithm>

/* An adapter's place in the factory's list; lower comes first and ties keep kernel order */
static
UINT
RxAdapterRank(
    _In_ BOOLEAN OwnsPrimary,
    _In_ BOOLEAN HasOutputs,
    _In_ BOOLEAN IsBasicRender)
{
    if (OwnsPrimary)
        return 0;
    if (HasOutputs)
        return IsBasicRender ? 2 : 1;
    return IsBasicRender ? 4 : 3;
}

/*
 * Without the display configuration calls, each attached GDI display is asked for its
 * adapter and source through the kernel thunks; the primary display comes first.
 */
static
VOID
RxCollectOutputsFromGdi(
    _Inout_ std::vector<RxAdapterInfo>& Adapters,
    _Inout_ std::vector<BOOLEAN>& OwnsPrimary)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
    D3DKMT_CLOSEADAPTER Close;
    DISPLAY_DEVICEW Display;
    RxOutputInfo Output;
    DWORD DeviceIndex;
    SIZE_T Index;

    if (Kmt->OpenAdapterFromGdiDisplayName == nullptr || Kmt->CloseAdapter == nullptr)
        return;

    for (DeviceIndex = 0;; DeviceIndex++)
    {
        Display = {};
        Display.cb = sizeof(Display);
        if (!EnumDisplayDevicesW(nullptr, DeviceIndex, &Display, 0))
            break;
        if (!(Display.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            continue;

        Open = {};
        wcscpy_s(Open.DeviceName, Display.DeviceName);
        if (!NT_SUCCESS(Kmt->OpenAdapterFromGdiDisplayName(&Open)))
            continue;
        Close.hAdapter = Open.hAdapter;
        Kmt->CloseAdapter(&Close);

        for (Index = 0; Index < Adapters.size(); Index++)
        {
            if (Adapters[Index].Luid.LowPart == Open.AdapterLuid.LowPart &&
                Adapters[Index].Luid.HighPart == Open.AdapterLuid.HighPart)
            {
                break;
            }
        }
        if (Index == Adapters.size())
            continue;

        Output = {};
        wcscpy_s(Output.DeviceName, Display.DeviceName);
        Output.VidPnSourceId = Open.VidPnSourceId;
        if (Display.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            Adapters[Index].Outputs.insert(Adapters[Index].Outputs.begin(), Output);
            OwnsPrimary[Index] = TRUE;
        }
        else
        {
            Adapters[Index].Outputs.push_back(Output);
        }
    }
}

/* Hands each active display source to the adapter that scans it out */
static
VOID
RxCollectOutputs(
    _Inout_ std::vector<RxAdapterInfo>& Adapters,
    _Inout_ std::vector<BOOLEAN>& OwnsPrimary)
{
    const RxDisplayConfigRoutines* Config = RxGetDisplayConfig();
    std::vector<DISPLAYCONFIG_PATH_INFO> Paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> Modes;
    DISPLAYCONFIG_SOURCE_DEVICE_NAME Name;
    RxOutputInfo Output;
    UINT32 PathCount;
    UINT32 ModeCount;
    BOOLEAN IsPrimary;
    BOOLEAN IsKnown;
    SIZE_T Index;

    if (Config == nullptr)
    {
        RxCollectOutputsFromGdi(Adapters, OwnsPrimary);
        return;
    }

    /* Systems that export the calls without implementing them still answer through GDI */
    if (Config->GetBufferSizes(QDC_ONLY_ACTIVE_PATHS, &PathCount, &ModeCount) != ERROR_SUCCESS)
    {
        RX_TRACE(L"no display configuration, finding outputs through GDI");
        RxCollectOutputsFromGdi(Adapters, OwnsPrimary);
        return;
    }

    Paths.resize(PathCount);
    Modes.resize(ModeCount);
    if (Config->Query(QDC_ONLY_ACTIVE_PATHS, &PathCount, Paths.data(), &ModeCount, Modes.data(), nullptr) !=
        ERROR_SUCCESS)
    {
        RX_TRACE(L"no display configuration, finding outputs through GDI");
        RxCollectOutputsFromGdi(Adapters, OwnsPrimary);
        return;
    }
    Paths.resize(PathCount);

    for (const DISPLAYCONFIG_PATH_INFO& Path : Paths)
    {
        for (Index = 0; Index < Adapters.size(); Index++)
        {
            if (Adapters[Index].Luid.LowPart == Path.sourceInfo.adapterId.LowPart &&
                Adapters[Index].Luid.HighPart == Path.sourceInfo.adapterId.HighPart)
            {
                break;
            }
        }
        if (Index == Adapters.size())
            continue;

        /* A cloned source shows up once per target; it is still one output */
        IsKnown = FALSE;
        for (const RxOutputInfo& Existing : Adapters[Index].Outputs)
            IsKnown = IsKnown || Existing.VidPnSourceId == Path.sourceInfo.id;
        if (IsKnown)
            continue;

        Name = {};
        Name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        Name.header.size = sizeof(Name);
        Name.header.adapterId = Path.sourceInfo.adapterId;
        Name.header.id = Path.sourceInfo.id;
        if (Config->GetDeviceInfo(&Name.header) != ERROR_SUCCESS)
            continue;

        Output = {};
        wcscpy_s(Output.DeviceName, Name.viewGdiDeviceName);
        Output.VidPnSourceId = Path.sourceInfo.id;

        /* The primary desktop is the source placed at the origin */
        IsPrimary = Path.sourceInfo.modeInfoIdx < ModeCount &&
                    Modes[Path.sourceInfo.modeInfoIdx].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE &&
                    Modes[Path.sourceInfo.modeInfoIdx].sourceMode.position.x == 0 &&
                    Modes[Path.sourceInfo.modeInfoIdx].sourceMode.position.y == 0;
        if (IsPrimary)
        {
            Adapters[Index].Outputs.insert(Adapters[Index].Outputs.begin(), Output);
            OwnsPrimary[Index] = TRUE;
        }
        else
        {
            Adapters[Index].Outputs.push_back(Output);
        }
    }

    /* After the primary, outputs go in source order */
    for (Index = 0; Index < Adapters.size(); Index++)
    {
        std::vector<RxOutputInfo>& Outputs = Adapters[Index].Outputs;

        std::stable_sort(Outputs.begin() + (OwnsPrimary[Index] ? 1 : 0), Outputs.end(),
                         [](const RxOutputInfo& First, const RxOutputInfo& Second)
                         {
                             return First.VidPnSourceId < Second.VidPnSourceId;
                         });
    }
}

HRESULT
RxDxgiFactory::Create(
    _In_ UINT Flags,
    _Outptr_ RxDxgiFactory** Factory)
{
    RxDxgiFactory* NewFactory;
    HRESULT hr;

    *Factory = nullptr;

    NewFactory = new (std::nothrow) RxDxgiFactory();
    if (NewFactory == nullptr)
        return E_OUTOFMEMORY;

    NewFactory->CreationFlags = Flags;
    hr = NewFactory->RefreshAdapterList();
    if (FAILED(hr))
    {
        NewFactory->Release();
        return hr;
    }

    *Factory = NewFactory;
    return S_OK;
}

HRESULT
RxDxgiFactory::RefreshAdapterList()
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_ENUMADAPTERS2 Enum = {};
    std::vector<D3DKMT_ADAPTERINFO> Infos;
    std::vector<BOOLEAN> OwnsPrimary;
    std::vector<BOOLEAN> IsBasicRender;
    std::vector<UINT> Order;
    std::vector<RxAdapterInfo> Found;
    D3DKMT_QUERY_DEVICE_IDS Ids;
    D3DKMT_QUERYADAPTERINFO Query;
    D3DKMT_CLOSEADAPTER Close;
    NTSTATUS Status;
    UINT Index;

    if (Kmt == nullptr || Kmt->EnumAdapters2 == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    /* The first call only counts */
    Status = Kmt->EnumAdapters2(&Enum);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    Infos.resize(Enum.NumAdapters);
    Enum.pAdapters = Infos.data();
    Status = Kmt->EnumAdapters2(&Enum);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);
    Infos.resize(Enum.NumAdapters);

    /* Enumeration opens every adapter; keep the LUID and whether it is Basic Render */
    for (const D3DKMT_ADAPTERINFO& Info : Infos)
    {
        Ids = {};
        Query = {};
        Query.hAdapter = Info.hAdapter;
        Query.Type = KMTQAITYPE_PHYSICALADAPTERDEVICEIDS;
        Query.pPrivateDriverData = &Ids;
        Query.PrivateDriverDataSize = sizeof(Ids);
        IsBasicRender.push_back(NT_SUCCESS(Kmt->QueryAdapterInfo(&Query)) &&
                                Ids.DeviceIds.VendorID == RxBasicRenderVendor &&
                                Ids.DeviceIds.DeviceID == RxBasicRenderDevice);

        Found.push_back({ Info.AdapterLuid, {}, {}, IsBasicRender.back() });
        Query.Type = KMTQAITYPE_ADAPTERTYPE;
        Query.pPrivateDriverData = &Found.back().Type;
        Query.PrivateDriverDataSize = sizeof(Found.back().Type);
        Kmt->QueryAdapterInfo(&Query);
        Close.hAdapter = Info.hAdapter;
        Kmt->CloseAdapter(&Close);
    }

    OwnsPrimary.assign(Found.size(), FALSE);
    RxCollectOutputs(Found, OwnsPrimary);

    for (Index = 0; Index < Found.size(); Index++)
        Order.push_back(Index);
    std::stable_sort(Order.begin(), Order.end(),
                     [&](UINT First, UINT Second)
                     {
                         return RxAdapterRank(OwnsPrimary[First], !Found[First].Outputs.empty(), IsBasicRender[First]) <
                                RxAdapterRank(OwnsPrimary[Second], !Found[Second].Outputs.empty(), IsBasicRender[Second]);
                     });

    Adapters.clear();
    for (UINT Position : Order)
        Adapters.push_back(Found[Position]);

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    /* A DXGI 1.0 factory answers every factory interface except the one that came after it */
    if (riid == __uuidof(IDXGIFactory1) && (CreationFlags & LegacyFactory))
    {
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIFactory) ||
        riid == __uuidof(IDXGIFactory1) || riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory3) ||
        riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) || riid == __uuidof(IDXGIFactory6) ||
        riid == __uuidof(IDXGIFactory7))
    {
        *ppvObject = static_cast<IDXGIFactory7*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IDXGIFactoryDWM))
    {
        *ppvObject = static_cast<IDXGIFactoryDWM*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::CreateSwapChain(
    _In_ IUnknown* pDevice,
    _In_ DXGI_SWAP_CHAIN_DESC* pDesc,
    _In_ IDXGIOutput* pOutput,
    _COM_Outptr_ IDXGISwapChainDWM1** ppSwapChain)
{
    IRxPresentDevice* Device;
    IDXGIOutputDWM* Dwm;
    RxDxgiOutput* Output;
    RxDwmSwapChain* SwapChain;
    HRESULT hr;

    if (ppSwapChain == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppSwapChain = nullptr;
    if (pDevice == nullptr || pDesc == nullptr || pOutput == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    /* Only outputs from this runtime know their video present source */
    if (FAILED(pOutput->QueryInterface(IID_PPV_ARGS(&Dwm))))
        return DXGI_ERROR_INVALID_CALL;
    Output = static_cast<RxDxgiOutput*>(pOutput);
    hr = (Dwm == static_cast<IDXGIOutputDWM*>(Output)) ? S_OK : DXGI_ERROR_INVALID_CALL;
    Dwm->Release();
    if (FAILED(hr))
        return hr;

    hr = pDevice->QueryInterface(IID_PPV_ARGS(&Device));
    if (FAILED(hr))
        return DXGI_ERROR_INVALID_CALL;

    hr = RxDwmSwapChain::Create(*this, Device, Output, *pDesc, &SwapChain);
    Device->Release();
    if (FAILED(hr))
        return hr;

    *ppSwapChain = SwapChain;
    return S_OK;
}

ULONG
STDMETHODCALLTYPE
RxDxgiFactory::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

ULONG
STDMETHODCALLTYPE
RxDxgiFactory::Release(VOID)
{
    ULONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        delete this;
    return Count;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::SetPrivateData(
    _In_ REFGUID Name,
    _In_ UINT DataSize,
    _In_reads_bytes_(DataSize) const void* pData)
{
    return PrivateData.Set(Name, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::SetPrivateDataInterface(
    _In_ REFGUID Name,
    _In_opt_ const IUnknown* pUnknown)
{
    return PrivateData.Set(Name, 0, nullptr, pUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::GetPrivateData(
    _In_ REFGUID Name,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_(*pDataSize) void* pData)
{
    return PrivateData.Get(Name, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::GetParent(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppParent)
{
    UNREFERENCED_PARAMETER(riid);

    /* A factory is the root of the DXGI object tree */
    if (ppParent != nullptr)
        *ppParent = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::EnumAdapters(
    _In_ UINT Adapter,
    _COM_Outptr_ IDXGIAdapter** ppAdapter)
{
    IDXGIAdapter1* Found;
    HRESULT hr;

    if (ppAdapter == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    hr = EnumAdapters1(Adapter, &Found);
    *ppAdapter = SUCCEEDED(hr) ? Found : nullptr;
    return hr;
}

VOID
RxDxgiFactory::TrackSwapChain(
    _In_ RxSwapChain* SwapChain)
{
    AcquireSRWLockExclusive(&SwapChainLock);
    SwapChains.push_back(SwapChain);
    ReleaseSRWLockExclusive(&SwapChainLock);
}

VOID
RxDxgiFactory::UntrackSwapChain(
    _In_ RxSwapChain* SwapChain)
{
    AcquireSRWLockExclusive(&SwapChainLock);
    std::erase(SwapChains, SwapChain);
    ReleaseSRWLockExclusive(&SwapChainLock);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::MakeWindowAssociation(
    _In_ HWND WindowHandle,
    _In_ UINT Flags)
{
    if ((Flags & ~DXGI_MWA_VALID) != 0)
        return DXGI_ERROR_INVALID_CALL;

    /* The flags land on this factory's swapchains for that window, or on all of them for NULL */
    AcquireSRWLockShared(&SwapChainLock);
    for (RxSwapChain* SwapChain : SwapChains)
    {
        if (WindowHandle == NULL || SwapChain->GetWindow() == WindowHandle)
            SwapChain->SetWindowAssociation(Flags);
    }
    ReleaseSRWLockShared(&SwapChainLock);

    /* Flags for every window also apply to swapchains made later, except on a legacy factory */
    if (WindowHandle == NULL && !(CreationFlags & LegacyFactory))
        AssociationFlags = Flags;

    return S_OK;
}

VOID
RxDxgiFactory::ToggleFullscreen()
{
    std::vector<RxSwapChain*> Alive;
    BOOL AnyFullscreen = FALSE;
    BOOL Fullscreen;

    /* A swapchain whose last reference is already gone is being destroyed and is skipped */
    AcquireSRWLockShared(&SwapChainLock);
    for (RxSwapChain* SwapChain : SwapChains)
    {
        if (SwapChain->AddRef() != 1)
            Alive.push_back(SwapChain);
    }
    ReleaseSRWLockShared(&SwapChainLock);

    for (RxSwapChain* SwapChain : Alive)
    {
        SwapChain->GetFullscreenState(&Fullscreen, nullptr);
        AnyFullscreen |= Fullscreen;
    }

    RX_TRACE(L"Alt+Enter: %Iu swapchains %s fullscreen", Alive.size(), AnyFullscreen ? L"leave" : L"enter");
    for (RxSwapChain* SwapChain : Alive)
    {
        SwapChain->SetFullscreenState(!AnyFullscreen, nullptr);
        SwapChain->Release();
    }
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::GetWindowAssociation(
    _Out_ HWND* pWindowHandle)
{
    if (pWindowHandle == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    /* Association lives on swapchains, so the factory never reports a window */
    *pWindowHandle = NULL;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::CreateSwapChain(
    _In_ IUnknown* pDevice,
    _In_ DXGI_SWAP_CHAIN_DESC* pDesc,
    _COM_Outptr_ IDXGISwapChain** ppSwapChain)
{
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC Fullscreen = {};
    IDXGISwapChain1* SwapChain;
    HRESULT hr;

    if (ppSwapChain == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppSwapChain = nullptr;
    if (pDesc == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    /* The original description splits into the window part and the fullscreen part */
    Desc.Width = pDesc->BufferDesc.Width;
    Desc.Height = pDesc->BufferDesc.Height;
    Desc.Format = pDesc->BufferDesc.Format;
    Desc.SampleDesc = pDesc->SampleDesc;
    Desc.BufferUsage = pDesc->BufferUsage;
    Desc.BufferCount = pDesc->BufferCount;
    Desc.Scaling = DXGI_SCALING_STRETCH;
    Desc.SwapEffect = pDesc->SwapEffect;
    Desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    Desc.Flags = pDesc->Flags;

    Fullscreen.RefreshRate = pDesc->BufferDesc.RefreshRate;
    Fullscreen.ScanlineOrdering = pDesc->BufferDesc.ScanlineOrdering;
    Fullscreen.Scaling = pDesc->BufferDesc.Scaling;
    Fullscreen.Windowed = pDesc->Windowed;

    hr = CreateSwapChainForHwnd(pDevice, pDesc->OutputWindow, &Desc, &Fullscreen, nullptr, &SwapChain);
    if (SUCCEEDED(hr))
        *ppSwapChain = SwapChain;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::EnumAdapters1(
    _In_ UINT Adapter,
    _COM_Outptr_ IDXGIAdapter1** ppAdapter)
{
    RxDxgiAdapter* Created;
    HRESULT hr;

    if (ppAdapter == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppAdapter = nullptr;

    if (Adapter >= Adapters.size())
        return DXGI_ERROR_NOT_FOUND;

    /* Each call hands out a new adapter object */
    hr = RxDxgiAdapter::Create(this, Adapters[Adapter], &Created);
    if (SUCCEEDED(hr))
        *ppAdapter = Created;
    return hr;
}

UINT
STDMETHODCALLTYPE
RxDxgiFactory::GetCreationFlags(VOID)
{
    return CreationFlags;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::EnumAdapterByLuid(
    _In_ LUID AdapterLuid,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvAdapter)
{
    if (ppvAdapter == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppvAdapter = nullptr;

    for (const RxAdapterInfo& Info : Adapters)
    {
        if (Info.Luid.LowPart == AdapterLuid.LowPart && Info.Luid.HighPart == AdapterLuid.HighPart)
            return CreateAdapter(Info, riid, ppvAdapter);
    }

    return DXGI_ERROR_NOT_FOUND;
}

BOOL
STDMETHODCALLTYPE
RxDxgiFactory::IsCurrent(VOID)
{
    /* Adapter and display changes are not watched yet, so the snapshot stays current */
    return TRUE;
}

BOOL
STDMETHODCALLTYPE
RxDxgiFactory::IsWindowedStereoEnabled(VOID)
{
    return FALSE;
}

/*
 * Devices ReactX can not present for, such as a Direct3D 12 queue from the system runtime,
 * get their swapchains from the system DXGI, loaded by full path beside this one.
 */
_Ret_maybenull_
static
IDXGIFactory2*
RxGetSystemFactory(VOID)
{
    static IDXGIFactory2* const Factory = []()
    {
        WCHAR Path[MAX_PATH];
        HMODULE System;
        HMODULE Self = NULL;
        decltype(&CreateDXGIFactory2) Create;
        IDXGIFactory2* Created = nullptr;
        UINT Length;

        Length = GetSystemDirectoryW(Path, _countof(Path));
        if (Length == 0 || Length >= _countof(Path) || wcscat_s(Path, L"\\dxgi.dll") != 0)
            return (IDXGIFactory2*)nullptr;

        System = LoadLibraryExW(Path, NULL, 0);
        if (System == NULL)
            return (IDXGIFactory2*)nullptr;

        /* Where this runtime is the system DXGI, as on ReactOS, forwarding would only come back here */
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&RxGetSystemFactory), &Self);
        if (System == Self)
        {
            RX_TRACE(L"the system DXGI is this runtime; devices from other runtimes get no swapchain");
            FreeLibrary(System);
            return (IDXGIFactory2*)nullptr;
        }

        Create = reinterpret_cast<decltype(&CreateDXGIFactory2)>(GetProcAddress(System, "CreateDXGIFactory2"));
        if (Create == nullptr || FAILED(Create(0, IID_PPV_ARGS(&Created))))
            return (IDXGIFactory2*)nullptr;

        RX_TRACE(L"system DXGI loaded for swapchains of devices from other runtimes");
        return Created;
    }();

    return Factory;
}

static
BOOLEAN
RxIsReactXDevice(
    _In_ IUnknown* Device)
{
    IRxPresentDevice* Present;

    if (FAILED(Device->QueryInterface(IID_PPV_ARGS(&Present))))
        return FALSE;

    Present->Release();
    return TRUE;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::CreateSwapChainForHwnd(
    _In_ IUnknown* pDevice,
    _In_ HWND hWnd,
    _In_ const DXGI_SWAP_CHAIN_DESC1* pDesc,
    _In_opt_ const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    _In_opt_ IDXGIOutput* pRestrictToOutput,
    _COM_Outptr_ IDXGISwapChain1** ppSwapChain)
{
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC Windowed;
    IRxPresentDevice* Device;
    RxSwapChain* SwapChain;
    BOOL WantFullscreen;
    HRESULT hr;

    UNREFERENCED_PARAMETER(pRestrictToOutput);

    if (ppSwapChain == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppSwapChain = nullptr;
    if (pDevice == nullptr || pDesc == nullptr || !IsWindow(hWnd))
        return DXGI_ERROR_INVALID_CALL;

    if (!RxIsReactXDevice(pDevice))
    {
        IDXGIFactory2* System = RxGetSystemFactory();

        RX_TRACE(L"swapchain for a device from another runtime goes to the system DXGI");
        if (System == nullptr)
            return DXGI_ERROR_INVALID_CALL;

        /* Outputs from this factory mean nothing to that one */
        return System->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, nullptr, ppSwapChain);
    }

    /* Aspect ratio stretching is only for CoreWindow and composition swapchains */
    if (pDesc->Scaling == DXGI_SCALING_ASPECT_RATIO_STRETCH)
        return DXGI_ERROR_INVALID_CALL;

    RX_TRACE(L"swapchain for window %p (%s): %ux%u format %u effect %u buffers %u usage %X flags %X alpha %u",
             hWnd, (GetWindowLongW(hWnd, GWL_STYLE) & WS_CHILD) ? L"child" : L"top level", pDesc->Width,
             pDesc->Height, pDesc->Format, pDesc->SwapEffect, pDesc->BufferCount, pDesc->BufferUsage, pDesc->Flags,
             pDesc->AlphaMode);

    /* Only a ReactX device can back a swapchain made here */
    if (FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&Device))))
        return DXGI_ERROR_INVALID_CALL;

    /* A swapchain asked for fullscreen starts windowed and then enters fullscreen, as SetFullscreenState would */
    WantFullscreen = (pFullscreenDesc != nullptr && !pFullscreenDesc->Windowed);
    if (WantFullscreen)
    {
        Windowed = *pFullscreenDesc;
        Windowed.Windowed = TRUE;
        pFullscreenDesc = &Windowed;
    }

    hr = RxSwapChain::Create(*this, Device, hWnd, *pDesc, pFullscreenDesc, &SwapChain);
    Device->Release();
    if (FAILED(hr))
        return hr;

    /* When fullscreen cannot be had now the swapchain stays windowed and says it is occluded */
    if (WantFullscreen && FAILED(SwapChain->SetFullscreenState(TRUE, nullptr)))
        hr = DXGI_STATUS_OCCLUDED;

    *ppSwapChain = SwapChain;
    return hr;
}

HRESULT
RxDxgiFactory::CreateAdapter(
    _In_ const RxAdapterInfo& Info,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvAdapter)
{
    RxDxgiAdapter* Created;
    HRESULT hr;

    hr = RxDxgiAdapter::Create(this, Info, &Created);
    if (FAILED(hr))
        return hr;

    hr = Created->QueryInterface(riid, ppvAdapter);
    Created->Release();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::EnumWarpAdapter(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvAdapter)
{
    if (ppvAdapter == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppvAdapter = nullptr;

    /* WARP is the software adapter already in the list, not a separate one */
    for (const RxAdapterInfo& Info : Adapters)
    {
        if (Info.IsBasicRender && Info.Type.RenderSupported && !Info.Type.DisplaySupported)
            return CreateAdapter(Info, riid, ppvAdapter);
    }

    return DXGI_ERROR_NOT_FOUND;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::CheckFeatureSupport(
    _In_ DXGI_FEATURE Feature,
    _Inout_updates_bytes_(FeatureSupportDataSize) void* pFeatureSupportData,
    _In_ UINT FeatureSupportDataSize)
{
    if (Feature != DXGI_FEATURE_PRESENT_ALLOW_TEARING || FeatureSupportDataSize != sizeof(BOOL))
        return DXGI_ERROR_INVALID_CALL;

    *static_cast<BOOL*>(pFeatureSupportData) = TRUE;
    return S_OK;
}

/* The per application choice from Graphics settings, or UNSPECIFIED when there is none */
static
DXGI_GPU_PREFERENCE
RxUserGpuPreference(VOID)
{
    WCHAR Executable[MAX_PATH];
    WCHAR Value[128] = {};
    DWORD Size = sizeof(Value) - sizeof(WCHAR);
    PCWSTR Setting;

    if (GetModuleFileNameW(NULL, Executable, _countof(Executable)) == 0 ||
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", Executable,
                     RRF_RT_REG_SZ, nullptr, Value, &Size) != ERROR_SUCCESS)
    {
        return DXGI_GPU_PREFERENCE_UNSPECIFIED;
    }

    Setting = wcsstr(Value, L"GpuPreference=");
    if (Setting == nullptr)
        return DXGI_GPU_PREFERENCE_UNSPECIFIED;

    switch (Setting[wcslen(L"GpuPreference=")])
    {
        case L'1':
            return DXGI_GPU_PREFERENCE_MINIMUM_POWER;
        case L'2':
            return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
        default:
            return DXGI_GPU_PREFERENCE_UNSPECIFIED;
    }
}

/* What kind of GPU an adapter is, listed fastest first */
enum class RxGpuKind
{
    Detachable,
    Discrete,
    Integrated,
};

static
RxGpuKind
RxKindOf(
    _In_ const RxAdapterInfo& Info)
{
    if (Info.Type.Detachable)
        return RxGpuKind::Detachable;
    if (Info.Type.HybridIntegrated)
        return RxGpuKind::Integrated;
    return RxGpuKind::Discrete;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::EnumAdapterByGpuPreference(
    _In_ UINT Adapter,
    _In_ DXGI_GPU_PREFERENCE GpuPreference,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvAdapter)
{
    std::vector<SIZE_T> Order;
    std::vector<SIZE_T>::iterator Hardware;
    DXGI_GPU_PREFERENCE UserChoice;
    BOOLEAN IsFastestFirst;
    UINT Preference;
    SIZE_T Index;

    if (ppvAdapter == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppvAdapter = nullptr;
    if (Adapter >= Adapters.size())
        return DXGI_ERROR_NOT_FOUND;

    /* Unspecified keeps the plain order; the other two can be overridden by the user */
    Preference = (UINT)GpuPreference & 3;
    if (Preference == DXGI_GPU_PREFERENCE_UNSPECIFIED)
        return CreateAdapter(Adapters[Adapter], riid, ppvAdapter);

    UserChoice = RxUserGpuPreference();
    if (UserChoice != DXGI_GPU_PREFERENCE_UNSPECIFIED)
        Preference = UserChoice;

    for (Index = 0; Index < Adapters.size(); Index++)
        Order.push_back(Index);

    /* Software adapters stay at the end; the hardware ones are ordered by the preference */
    Hardware = std::stable_partition(Order.begin(), Order.end(),
                                     [&](SIZE_T Index)
                                     {
                                         return !Adapters[Index].IsBasicRender;
                                     });

    IsFastestFirst = Preference == DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
    std::stable_sort(Order.begin(), Hardware,
                     [&](SIZE_T First, SIZE_T Second)
                     {
                         RxGpuKind A = RxKindOf(Adapters[First]);
                         RxGpuKind B = RxKindOf(Adapters[Second]);

                         return IsFastestFirst ? A < B : B < A;
                     });

    return CreateAdapter(Adapters[Order[Adapter]], riid, ppvAdapter);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::RegisterAdaptersChangedEvent(
    _In_ HANDLE hEvent,
    _Out_ DWORD* pdwCookie)
{
    /* The snapshot never goes stale yet, so there is nothing to signal now */
    return AdapterEvents.AddEvent(hEvent, pdwCookie, FALSE);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::UnregisterAdaptersChangedEvent(
    _In_ DWORD dwCookie)
{
    return AdapterEvents.Remove(dwCookie) ? S_OK : DXGI_ERROR_NOT_FOUND;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::RegisterOcclusionStatusWindow(
    _In_ HWND WindowHandle,
    _In_ UINT wMsg,
    _Out_ DWORD* pdwCookie)
{
    return OcclusionEvents.AddWindow(WindowHandle, wMsg, pdwCookie);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::RegisterOcclusionStatusEvent(
    _In_ HANDLE hEvent,
    _Out_ DWORD* pdwCookie)
{
    return OcclusionEvents.AddEvent(hEvent, pdwCookie, TRUE);
}

VOID
STDMETHODCALLTYPE
RxDxgiFactory::UnregisterOcclusionStatus(
    _In_ DWORD dwCookie)
{
    OcclusionEvents.Remove(dwCookie);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::RegisterStereoStatusWindow(
    _In_ HWND WindowHandle,
    _In_ UINT wMsg,
    _Out_ DWORD* pdwCookie)
{
    return StereoEvents.AddWindow(WindowHandle, wMsg, pdwCookie);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::RegisterStereoStatusEvent(
    _In_ HANDLE hEvent,
    _Out_ DWORD* pdwCookie)
{
    return StereoEvents.AddEvent(hEvent, pdwCookie, TRUE);
}

VOID
STDMETHODCALLTYPE
RxDxgiFactory::UnregisterStereoStatus(
    _In_ DWORD dwCookie)
{
    StereoEvents.Remove(dwCookie);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::GetSharedResourceAdapterLuid(
    _In_ HANDLE hResource,
    _Out_ LUID* pLuid)
{
    D3DKMT_GETSHAREDRESOURCEADAPTERLUID Query = {};
    const KmtThunks* Kmt = RxGetKmtThunks();
    NTSTATUS Status;

    if (hResource == NULL || pLuid == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    if (Kmt->GetSharedResourceAdapterLuid == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    /* Kernel global share handles have bit one set; anything else is an NT handle */
    if ((ULONG_PTR)hResource & 2)
        Query.hGlobalShare = (D3DKMT_HANDLE)(ULONG_PTR)hResource;
    else
        Query.hNtHandle = hResource;

    Status = Kmt->GetSharedResourceAdapterLuid(&Query);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    *pLuid = Query.AdapterLuid;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::CreateSoftwareAdapter(
    HMODULE Module,
    _COM_Outptr_ IDXGIAdapter** ppAdapter)
{
    WCHAR Name[MAX_PATH];
    PCWSTR File;

    if (ppAdapter == nullptr || Module == NULL)
        return DXGI_ERROR_INVALID_CALL;
    *ppAdapter = nullptr;

    /* WARP and the reference rasterizer are the Basic Render adapter; other modules have no host here */
    if (GetModuleFileNameW(Module, Name, _countof(Name)) == 0)
        return DXGI_ERROR_INVALID_CALL;
    File = wcsrchr(Name, L'\\');
    File = File ? File + 1 : Name;
    if (_wcsicmp(File, L"d3d10warp.dll") != 0 && _wcsicmp(File, L"d3d11ref.dll") != 0)
        return DXGI_ERROR_UNSUPPORTED;

    return EnumWarpAdapter(IID_PPV_ARGS(ppAdapter));
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::CreateSwapChainForComposition(
    _In_ IUnknown* pDevice,
    _In_ const DXGI_SWAP_CHAIN_DESC1* pDesc,
    _In_opt_ IDXGIOutput* pRestrictToOutput,
    _COM_Outptr_ IDXGISwapChain1** ppSwapChain)
{
    UNREFERENCED_PARAMETER(pRestrictToOutput);

    if (ppSwapChain == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppSwapChain = nullptr;

    /* Composition swapchains are flip only, sized up front, stretched and never straight alpha */
    if (pDevice == nullptr || pDesc == nullptr || pDesc->Width == 0 || pDesc->Height == 0 ||
        (pDesc->SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL && pDesc->SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD) ||
        pDesc->Scaling != DXGI_SCALING_STRETCH || pDesc->AlphaMode == DXGI_ALPHA_MODE_STRAIGHT)
    {
        return DXGI_ERROR_INVALID_CALL;
    }

    IRxPresentDevice* Device;
    RxSwapChain* SwapChain;
    HRESULT hr;

    if (!RxIsReactXDevice(pDevice))
    {
        IDXGIFactory2* System = RxGetSystemFactory();

        RX_TRACE(L"composition swapchain for a device from another runtime goes to the system DXGI");
        if (System == nullptr)
            return DXGI_ERROR_INVALID_CALL;
        return System->CreateSwapChainForComposition(pDevice, pDesc, nullptr, ppSwapChain);
    }

    /* Only a ReactX device can back a swapchain made here */
    if (FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&Device))))
        return DXGI_ERROR_INVALID_CALL;

    hr = RxSwapChain::Create(*this, Device, NULL, *pDesc, nullptr, &SwapChain);
    Device->Release();
    RX_TRACE(L"composition swapchain %ux%u format %u effect %u buffers %u alpha %u: 0x%08lX", pDesc->Width,
             pDesc->Height, pDesc->Format, pDesc->SwapEffect, pDesc->BufferCount, pDesc->AlphaMode, hr);
    if (SUCCEEDED(hr))
        *ppSwapChain = SwapChain;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiFactory::CreateSwapChainForCoreWindow(
    _In_ IUnknown* pDevice,
    _In_ IUnknown* pWindow,
    _In_ const DXGI_SWAP_CHAIN_DESC1* pDesc,
    _In_opt_ IDXGIOutput* pRestrictToOutput,
    _COM_Outptr_ IDXGISwapChain1** ppSwapChain)
{
    ICoreWindowInterop* Interop;
    IRxPresentDevice* Device;
    IUnknown* Identity;
    RxSwapChain* SwapChain = nullptr;
    BOOLEAN IsTaken = FALSE;
    HWND Window = NULL;
    HRESULT hr;

    UNREFERENCED_PARAMETER(pRestrictToOutput);

    if (ppSwapChain == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppSwapChain = nullptr;
    if (pDevice == nullptr || pDesc == nullptr || pWindow == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    if (FAILED(pWindow->QueryInterface(IID_PPV_ARGS(&Interop))))
        return DXGI_ERROR_INVALID_CALL;
    hr = Interop->get_WindowHandle(&Window);
    Interop->Release();
    if (FAILED(hr) || !IsWindow(Window))
        return DXGI_ERROR_INVALID_CALL;

    if (pDesc->SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL && pDesc->SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD)
        return DXGI_ERROR_INVALID_CALL;

    if (!RxIsReactXDevice(pDevice))
    {
        IDXGIFactory2* System = RxGetSystemFactory();

        RX_TRACE(L"CoreWindow swapchain for a device from another runtime goes to the system DXGI");
        if (System == nullptr)
            return DXGI_ERROR_INVALID_CALL;
        return System->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, nullptr, ppSwapChain);
    }

    if (FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&Device))))
        return DXGI_ERROR_INVALID_CALL;
    pWindow->QueryInterface(IID_PPV_ARGS(&Identity));

    /* A CoreWindow takes one swapchain at a time */
    AcquireSRWLockShared(&SwapChainLock);
    for (RxSwapChain* Each : SwapChains)
        IsTaken = IsTaken || Each->GetCoreWindowIdentity() == Identity;
    ReleaseSRWLockShared(&SwapChainLock);

    /* A Windows.UI.Composition target already owns the window's root */
    if (GetPropW(Window, RxCoreWindowRootProperty) != NULL)
        IsTaken = TRUE;

    RX_TRACE(L"swapchain for CoreWindow %p (window %p): %ux%u format %u effect %u buffers %u scaling %u", Identity, Window,
             pDesc->Width, pDesc->Height, pDesc->Format, pDesc->SwapEffect, pDesc->BufferCount, pDesc->Scaling);

    /* ReactOS has no CoreWindow visual, so the buffers flip into the window behind it */
    hr = IsTaken ? E_ACCESSDENIED : RxSwapChain::Create(*this, Device, Window, *pDesc, nullptr, &SwapChain);
    Device->Release();
    if (SUCCEEDED(hr))
    {
        SwapChain->AttachCoreWindow(Identity);
        *ppSwapChain = SwapChain;
    }

    Identity->Release();
    return hr;
}
