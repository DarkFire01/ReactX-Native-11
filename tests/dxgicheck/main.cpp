/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXGI behavior checks run against one dxgi.dll and d3d11.dll pair
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * dxgicheck [-runtime system|reactx|<dir>] [check ...]
 * dxgicheck -list
 */

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <windows.ui.core.h>
#include <corewindow.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

typedef HRESULT (WINAPI* PFN_DC_CREATE_FACTORY)(REFIID riid, void** ppFactory);
typedef HRESULT (WINAPI* PFN_DC_CREATE_FACTORY2)(UINT Flags, REFIID riid, void** ppFactory);
typedef HRESULT (WINAPI* PFN_DC_GET_DEBUG_INTERFACE1)(UINT Flags, REFIID riid, void** ppDebug);
typedef HRESULT (WINAPI* PFN_DC_DECLARE_REMOVAL_SUPPORT)(VOID);

/* One runtime pair and the place facts go */
class DxgiCheck
{
public:
    PFN_DC_CREATE_FACTORY CreateFactory = nullptr;
    PFN_DC_CREATE_FACTORY CreateFactory1 = nullptr;
    PFN_DC_CREATE_FACTORY2 CreateFactory2 = nullptr;
    PFN_DC_GET_DEBUG_INTERFACE1 GetDebugInterface1 = nullptr;
    PFN_DC_DECLARE_REMOVAL_SUPPORT DeclareRemovalSupport = nullptr;
    PFN_D3D11_CREATE_DEVICE CreateDevice = nullptr;
    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN CreateDeviceAndSwapChain = nullptr;
    PCSTR CheckName = "";
    UINT Failures = 0;

    VOID
    Report(
        _In_z_ PCSTR Key,
        _In_z_ _Printf_format_string_ PCSTR Format,
        ...)
    {
        va_list Arguments;

        printf("%s.%s = ", CheckName, Key);
        va_start(Arguments, Format);
        vprintf(Format, Arguments);
        va_end(Arguments);
        printf("\n");
    }

    VOID
    ReportResult(
        _In_z_ PCSTR Key,
        _In_ HRESULT Result)
    {
        Report(Key, "0x%08lX", Result);
    }

    BOOLEAN
    Expect(
        _In_ BOOLEAN Condition,
        _In_z_ PCSTR What)
    {
        if (!Condition)
        {
            printf("FAIL %s: %s\n", CheckName, What);
            Failures++;
        }

        return Condition;
    }

    HRESULT
    MakeDevice(
        _In_opt_ IDXGIAdapter* Adapter,
        _COM_Outptr_ ID3D11Device** Device)
    {
        static const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        return CreateDevice(Adapter, Adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL, 0, Levels,
                            _countof(Levels), D3D11_SDK_VERSION, Device, nullptr, nullptr);
    }
};

struct CheckInfo
{
    PCSTR Name;
    VOID (*Run)(DxgiCheck& Check);
};

/* Names a key with a number in it, for per adapter and per output facts */
static
std::string
DcKey(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    char Text[128];
    va_list Arguments;

    va_start(Arguments, Format);
    vsnprintf(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);
    return Text;
}

static
PCSTR
DcSupports(
    _In_ IUnknown* Object,
    _In_ REFIID Interface)
{
    ComPtr<IUnknown> Found;

    return SUCCEEDED(Object->QueryInterface(Interface, &Found)) ? "yes" : "no";
}

static
std::string
DcNarrow(
    _In_z_ PCWSTR Text)
{
    char Buffer[256];

    WideCharToMultiByte(CP_UTF8, 0, Text, -1, Buffer, sizeof(Buffer), nullptr, nullptr);
    return Buffer;
}

static
BOOLEAN
DcSameObject(
    _In_opt_ IUnknown* First,
    _In_opt_ IUnknown* Second)
{
    ComPtr<IUnknown> A;
    ComPtr<IUnknown> B;

    if (First == nullptr || Second == nullptr)
        return First == Second;

    First->QueryInterface(IID_PPV_ARGS(&A));
    Second->QueryInterface(IID_PPV_ARGS(&B));
    return A.Get() == B.Get();
}

static
HWND
DcCreateWindow(
    _In_ UINT Width,
    _In_ UINT Height)
{
    static const WCHAR ClassName[] = L"ReactXDxgiCheck";
    static ATOM Registered = 0;
    WNDCLASSW Class = {};
    RECT Frame = { 0, 0, (LONG)Width, (LONG)Height };

    if (Registered == 0)
    {
        Class.lpfnWndProc = DefWindowProcW;
        Class.hInstance = GetModuleHandleW(nullptr);
        Class.lpszClassName = ClassName;
        Registered = RegisterClassW(&Class);
    }

    AdjustWindowRect(&Frame, WS_OVERLAPPEDWINDOW, FALSE);
    return CreateWindowW(ClassName, L"dxgicheck", WS_OVERLAPPEDWINDOW, 40, 40, Frame.right - Frame.left,
                         Frame.bottom - Frame.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

/* factory: the entry points, flags and the interfaces each factory answers */

static
VOID
DcCheckFactory(
    _In_ DxgiCheck& Check)
{
    static const struct
    {
        PCSTR Name;
        IID Interface;
    } Interfaces[] =
    {
        { "IDXGIFactory", __uuidof(IDXGIFactory) },
        { "IDXGIFactory1", __uuidof(IDXGIFactory1) },
        { "IDXGIFactory2", __uuidof(IDXGIFactory2) },
        { "IDXGIFactory3", __uuidof(IDXGIFactory3) },
        { "IDXGIFactory4", __uuidof(IDXGIFactory4) },
        { "IDXGIFactory5", __uuidof(IDXGIFactory5) },
        { "IDXGIFactory6", __uuidof(IDXGIFactory6) },
        { "IDXGIFactory7", __uuidof(IDXGIFactory7) },
    };
    ComPtr<IUnknown> Factories[3];
    ComPtr<IDXGIFactory1> Factory1;
    ComPtr<IUnknown> Parent;
    void* Unused = nullptr;
    UINT Index;
    UINT Kind;

    Check.ReportResult("create", Check.CreateFactory(IID_PPV_ARGS(&Factories[0])));
    Check.ReportResult("create1", Check.CreateFactory1(IID_PPV_ARGS(&Factories[1])));
    Check.ReportResult("create2", Check.CreateFactory2(0, IID_PPV_ARGS(&Factories[2])));

    for (Kind = 0; Kind < _countof(Factories); Kind++)
    {
        if (Factories[Kind] == nullptr)
            continue;

        for (Index = 0; Index < _countof(Interfaces); Index++)
        {
            Check.Report(DcKey("create%u_%s", Kind, Interfaces[Index].Name).c_str(), "%s",
                         DcSupports(Factories[Kind].Get(), Interfaces[Index].Interface));
        }
    }

    Check.ReportResult("create2_unknown_flag", Check.CreateFactory2(0x2, IID_PPV_ARGS(&Factory1)));
    Factory1.Reset();
    Check.ReportResult("create_adapter_iid", Check.CreateFactory1(__uuidof(IDXGIAdapter), &Unused));
    Check.Report("create_adapter_iid_out", "%s", Unused ? "set" : "null");
    Check.ReportResult("debug_interface_null", Check.GetDebugInterface1(0, __uuidof(IDXGIInfoQueue), nullptr));

    Check.Expect(SUCCEEDED(Check.CreateFactory1(IID_PPV_ARGS(&Factory1))), "CreateDXGIFactory1");
    if (Factory1 == nullptr)
        return;

    Check.Report("is_current", "%d", Factory1->IsCurrent());
    Check.ReportResult("get_parent", Factory1->GetParent(IID_PPV_ARGS(&Parent)));
    Check.Report("two_factories_same", "%s", DcSameObject(Factory1.Get(), Factories[1].Get()) ? "yes" : "no");

    Check.ReportResult("declare_removal", Check.DeclareRemovalSupport());
    Check.ReportResult("declare_removal_again", Check.DeclareRemovalSupport());
}

/* adapters: enumeration order and what each adapter says about itself */

static
VOID
DcCheckAdapters(
    _In_ DxgiCheck& Check)
{
    ComPtr<IDXGIFactory1> Factory;
    ComPtr<IDXGIFactory1> Other;
    ComPtr<IDXGIAdapter1> Adapter;
    ComPtr<IDXGIAdapter> Plain;
    ComPtr<IDXGIAdapter1> Again;
    ComPtr<IDXGIAdapter1> FromOther;
    ComPtr<IDXGIFactory1> Parent;
    DXGI_ADAPTER_DESC1 Desc;
    LARGE_INTEGER Version;
    UINT Index;
    HRESULT hr;

    if (!Check.Expect(SUCCEEDED(Check.CreateFactory1(IID_PPV_ARGS(&Factory))), "CreateDXGIFactory1") ||
        !Check.Expect(SUCCEEDED(Check.CreateFactory1(IID_PPV_ARGS(&Other))), "second factory"))
    {
        return;
    }

    for (Index = 0; SUCCEEDED(Factory->EnumAdapters1(Index, &Adapter)); Index++)
    {
        Adapter->GetDesc1(&Desc);
        Check.Report(DcKey("%u.description", Index).c_str(), "%s", DcNarrow(Desc.Description).c_str());
        Check.Report(DcKey("%u.ids", Index).c_str(), "%04X %04X %08X %u", Desc.VendorId, Desc.DeviceId, Desc.SubSysId,
                     Desc.Revision);
        Check.Report(DcKey("%u.memory", Index).c_str(), "%zu %zu %zu", Desc.DedicatedVideoMemory,
                     Desc.DedicatedSystemMemory, Desc.SharedSystemMemory);
        Check.Report(DcKey("%u.luid", Index).c_str(), "%08lX:%08lX", Desc.AdapterLuid.HighPart,
                     Desc.AdapterLuid.LowPart);
        Check.Report(DcKey("%u.flags", Index).c_str(), "%u", Desc.Flags);

        hr = Adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &Version);
        Check.Report(DcKey("%u.support_dxgidevice", Index).c_str(), "0x%08lX %08lX.%08lX", hr,
                     SUCCEEDED(hr) ? Version.HighPart : 0, SUCCEEDED(hr) ? Version.LowPart : 0);
        hr = Adapter->CheckInterfaceSupport(__uuidof(ID3D11Device), &Version);
        Check.Report(DcKey("%u.support_d3d11", Index).c_str(), "0x%08lX", hr);
        hr = Adapter->CheckInterfaceSupport(__uuidof(ID3D10Device), &Version);
        Check.Report(DcKey("%u.support_d3d10", Index).c_str(), "0x%08lX %08lX.%08lX", hr,
                     SUCCEEDED(hr) ? Version.HighPart : 0, SUCCEEDED(hr) ? Version.LowPart : 0);

        Check.Report(DcKey("%u.adapter2", Index).c_str(), "%s", DcSupports(Adapter.Get(), __uuidof(IDXGIAdapter2)));
        Check.Report(DcKey("%u.adapter4", Index).c_str(), "%s", DcSupports(Adapter.Get(), __uuidof(IDXGIAdapter4)));

        /* The same index from the same factory, from IDXGIFactory, and from another factory */
        Factory->EnumAdapters1(Index, &Again);
        Factory->EnumAdapters(Index, &Plain);
        Other->EnumAdapters1(Index, &FromOther);
        Check.Report(DcKey("%u.enum_twice_same", Index).c_str(), "%s", DcSameObject(Adapter.Get(), Again.Get()) ? "yes" : "no");
        Check.Report(DcKey("%u.enum_plain_same", Index).c_str(), "%s", DcSameObject(Adapter.Get(), Plain.Get()) ? "yes" : "no");
        Check.Report(DcKey("%u.other_factory_same", Index).c_str(), "%s",
                     DcSameObject(Adapter.Get(), FromOther.Get()) ? "yes" : "no");

        Check.ReportResult(DcKey("%u.get_parent", Index).c_str(), Adapter->GetParent(IID_PPV_ARGS(&Parent)));
        Check.Report(DcKey("%u.parent_is_factory", Index).c_str(), "%s",
                     DcSameObject(Parent.Get(), Factory.Get()) ? "yes" : "no");
        Parent.Reset();
    }

    Check.Report("count", "%u", Index);
    Check.ReportResult("past_end", Factory->EnumAdapters1(Index, &Adapter));
    Check.Report("past_end_out", "%s", Adapter ? "set" : "null");
    Check.ReportResult("null_out", Factory->EnumAdapters1(0, nullptr));
}

/* outputs: the displays each adapter drives */

static
VOID
DcCheckOutputs(
    _In_ DxgiCheck& Check)
{
    ComPtr<IDXGIFactory1> Factory;
    ComPtr<IDXGIAdapter1> Adapter;
    ComPtr<IDXGIOutput> Output;
    ComPtr<IDXGIAdapter> Parent;
    std::vector<DXGI_MODE_DESC> Modes;
    DXGI_MODE_DESC Wanted = {};
    DXGI_MODE_DESC Closest;
    DXGI_OUTPUT_DESC Desc;
    UINT AdapterIndex;
    UINT Index;
    UINT Count;
    HRESULT hr;

    if (!Check.Expect(SUCCEEDED(Check.CreateFactory1(IID_PPV_ARGS(&Factory))), "CreateDXGIFactory1"))
        return;

    for (AdapterIndex = 0; SUCCEEDED(Factory->EnumAdapters1(AdapterIndex, &Adapter)); AdapterIndex++)
    {
        for (Index = 0; SUCCEEDED(Adapter->EnumOutputs(Index, &Output)); Index++)
        {
            Output->GetDesc(&Desc);
            Check.Report(DcKey("%u.%u.name", AdapterIndex, Index).c_str(), "%s", DcNarrow(Desc.DeviceName).c_str());
            Check.Report(DcKey("%u.%u.rect", AdapterIndex, Index).c_str(), "%ld %ld %ld %ld",
                         Desc.DesktopCoordinates.left, Desc.DesktopCoordinates.top, Desc.DesktopCoordinates.right,
                         Desc.DesktopCoordinates.bottom);
            Check.Report(DcKey("%u.%u.attached", AdapterIndex, Index).c_str(), "%d %d %s", Desc.AttachedToDesktop,
                         Desc.Rotation, Desc.Monitor ? "monitor" : "none");
            Check.Report(DcKey("%u.%u.output1", AdapterIndex, Index).c_str(), "%s",
                         DcSupports(Output.Get(), __uuidof(IDXGIOutput1)));
            Check.Report(DcKey("%u.%u.output6", AdapterIndex, Index).c_str(), "%s",
                         DcSupports(Output.Get(), __uuidof(IDXGIOutput6)));

            {
                ComPtr<IDXGIOutput6> Output6;
                DXGI_OUTPUT_DESC1 Desc1 = {};

                if (SUCCEEDED(Output.As(&Output6)))
                {
                    hr = Output6->GetDesc1(&Desc1);
                    Check.Report(DcKey("%u.%u.desc1", AdapterIndex, Index).c_str(), "0x%08lX %s %d %d bpc %u space %u",
                                 hr, DcNarrow(Desc1.DeviceName).c_str(), Desc1.AttachedToDesktop, Desc1.Rotation,
                                 Desc1.BitsPerColor, Desc1.ColorSpace);
                    Check.Report(DcKey("%u.%u.desc1_color", AdapterIndex, Index).c_str(),
                                 "r %.4f %.4f g %.4f %.4f b %.4f %.4f w %.4f %.4f lum %.6f %.4f %.4f",
                                 Desc1.RedPrimary[0], Desc1.RedPrimary[1], Desc1.GreenPrimary[0], Desc1.GreenPrimary[1],
                                 Desc1.BluePrimary[0], Desc1.BluePrimary[1], Desc1.WhitePoint[0], Desc1.WhitePoint[1],
                                 Desc1.MinLuminance, Desc1.MaxLuminance, Desc1.MaxFullFrameLuminance);
                }
            }

            Check.ReportResult(DcKey("%u.%u.get_parent", AdapterIndex, Index).c_str(),
                               Output->GetParent(IID_PPV_ARGS(&Parent)));
            Check.Report(DcKey("%u.%u.parent_is_adapter", AdapterIndex, Index).c_str(), "%s",
                         DcSameObject(Parent.Get(), Adapter.Get()) ? "yes" : "no");
            Parent.Reset();

            Count = 0;
            hr = Output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &Count, nullptr);
            Check.Report(DcKey("%u.%u.mode_count", AdapterIndex, Index).c_str(), "0x%08lX %u", hr, Count);
            if (SUCCEEDED(hr) && Count != 0)
            {
                Modes.resize(Count);
                hr = Output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &Count, Modes.data());
                Check.Report(DcKey("%u.%u.first_mode", AdapterIndex, Index).c_str(), "0x%08lX %ux%u %u/%u %d %d", hr,
                             Modes[0].Width, Modes[0].Height, Modes[0].RefreshRate.Numerator,
                             Modes[0].RefreshRate.Denominator, Modes[0].ScanlineOrdering, Modes[0].Scaling);
                Check.Report(DcKey("%u.%u.last_mode", AdapterIndex, Index).c_str(), "%ux%u %u/%u %d %d",
                             Modes[Count - 1].Width, Modes[Count - 1].Height, Modes[Count - 1].RefreshRate.Numerator,
                             Modes[Count - 1].RefreshRate.Denominator, Modes[Count - 1].ScanlineOrdering,
                             Modes[Count - 1].Scaling);

                /* One fewer slot than modes: the list is cut short */
                Count--;
                hr = Output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &Count, Modes.data());
                Check.Report(DcKey("%u.%u.short_list", AdapterIndex, Index).c_str(), "0x%08lX %u", hr, Count);
            }

            Count = 0;
            hr = Output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_SCALING, &Count, nullptr);
            Check.Report(DcKey("%u.%u.scaling_count", AdapterIndex, Index).c_str(), "0x%08lX %u", hr, Count);
            Count = 0;
            hr = Output->GetDisplayModeList(DXGI_FORMAT_R16G16B16A16_FLOAT, 0, &Count, nullptr);
            Check.Report(DcKey("%u.%u.fp16_count", AdapterIndex, Index).c_str(), "0x%08lX %u", hr, Count);

            Wanted.Width = 1280;
            Wanted.Height = 720;
            Wanted.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            hr = Output->FindClosestMatchingMode(&Wanted, &Closest, nullptr);
            Check.Report(DcKey("%u.%u.closest", AdapterIndex, Index).c_str(), "0x%08lX %ux%u %u/%u %d %d", hr,
                         SUCCEEDED(hr) ? Closest.Width : 0, SUCCEEDED(hr) ? Closest.Height : 0,
                         SUCCEEDED(hr) ? Closest.RefreshRate.Numerator : 0,
                         SUCCEEDED(hr) ? Closest.RefreshRate.Denominator : 0,
                         SUCCEEDED(hr) ? Closest.ScanlineOrdering : 0, SUCCEEDED(hr) ? Closest.Scaling : 0);
        }

        Check.Report(DcKey("%u.count", AdapterIndex).c_str(), "%u", Index);
        Check.ReportResult(DcKey("%u.past_end", AdapterIndex).c_str(), Adapter->EnumOutputs(Index, &Output));
    }
}

/* devicelink: the adapter a device reports, with and without one given at creation */

static
VOID
DcCheckDeviceLink(
    _In_ DxgiCheck& Check)
{
    ComPtr<IDXGIFactory1> Factory;
    ComPtr<IDXGIAdapter1> First;
    ComPtr<ID3D11Device> Device;
    ComPtr<IDXGIDevice> DxgiDevice;
    ComPtr<IDXGIAdapter> Reported;
    ComPtr<IDXGIAdapter> Parent;
    ComPtr<IDXGIFactory1> AdapterParent;
    DXGI_ADAPTER_DESC FirstDesc;
    DXGI_ADAPTER_DESC ReportedDesc;

    if (!Check.Expect(SUCCEEDED(Check.CreateFactory1(IID_PPV_ARGS(&Factory))), "CreateDXGIFactory1") ||
        !Check.Expect(SUCCEEDED(Factory->EnumAdapters1(0, &First)), "first adapter"))
    {
        return;
    }
    First->GetDesc(&FirstDesc);

    /* No adapter given: the device lands on the first one */
    if (!Check.Expect(SUCCEEDED(Check.MakeDevice(nullptr, &Device)), "device without adapter") ||
        !Check.Expect(SUCCEEDED(Device.As(&DxgiDevice)), "IDXGIDevice"))
    {
        return;
    }

    Check.ReportResult("default_get_adapter", DxgiDevice->GetAdapter(&Reported));
    if (Reported != nullptr)
    {
        Reported->GetDesc(&ReportedDesc);
        Check.Report("default_is_first", "%s",
                     memcmp(&ReportedDesc.AdapterLuid, &FirstDesc.AdapterLuid, sizeof(LUID)) == 0 ? "yes" : "no");
        Check.ReportResult("default_adapter_parent", Reported->GetParent(IID_PPV_ARGS(&AdapterParent)));
    }
    Check.ReportResult("default_get_parent", DxgiDevice->GetParent(IID_PPV_ARGS(&Parent)));
    Check.Report("parent_matches_adapter", "%s", DcSameObject(Parent.Get(), Reported.Get()) ? "yes" : "no");

    /* An adapter given: the device hands that very object back */
    Device.Reset();
    DxgiDevice.Reset();
    Reported.Reset();
    if (!Check.Expect(SUCCEEDED(Check.MakeDevice(First.Get(), &Device)), "device on an adapter") ||
        !Check.Expect(SUCCEEDED(Device.As(&DxgiDevice)), "IDXGIDevice"))
    {
        return;
    }

    Check.ReportResult("given_get_adapter", DxgiDevice->GetAdapter(&Reported));
    Check.Report("given_is_same_object", "%s", DcSameObject(Reported.Get(), First.Get()) ? "yes" : "no");
}

static
VOID
DcReportSurface(
    _In_ DxgiCheck& Check,
    _In_z_ PCSTR Prefix,
    _In_ ID3D11Resource* Resource);

/* swapchain: creation rules and the state a windowed swapchain reports */

static
VOID
DcReportSwapChain(
    _In_ DxgiCheck& Check,
    _In_z_ PCSTR Prefix,
    _In_ IDXGISwapChain1* SwapChain)
{
    DXGI_SWAP_CHAIN_DESC1 Desc1;
    DXGI_SWAP_CHAIN_DESC Desc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC Fullscreen = {};

    SwapChain->GetDesc1(&Desc1);
    Check.Report(DcKey("%s.desc1", Prefix).c_str(), "%ux%u fmt %d stereo %d samples %u/%u usage %X buffers %u scaling %d effect %d alpha %d flags %X",
                 Desc1.Width, Desc1.Height, Desc1.Format, Desc1.Stereo, Desc1.SampleDesc.Count, Desc1.SampleDesc.Quality,
                 Desc1.BufferUsage, Desc1.BufferCount, Desc1.Scaling, Desc1.SwapEffect, Desc1.AlphaMode, Desc1.Flags);
    SwapChain->GetDesc(&Desc);
    Check.Report(DcKey("%s.desc", Prefix).c_str(), "%ux%u fmt %d refresh %u/%u scanline %d scaling %d usage %X buffers %u windowed %d effect %d flags %X",
                 Desc.BufferDesc.Width, Desc.BufferDesc.Height, Desc.BufferDesc.Format,
                 Desc.BufferDesc.RefreshRate.Numerator, Desc.BufferDesc.RefreshRate.Denominator,
                 Desc.BufferDesc.ScanlineOrdering, Desc.BufferDesc.Scaling, Desc.BufferUsage, Desc.BufferCount,
                 Desc.Windowed, Desc.SwapEffect, Desc.Flags);
    SwapChain->GetFullscreenDesc(&Fullscreen);
    Check.Report(DcKey("%s.fullscreen_desc", Prefix).c_str(), "%u/%u %d %d windowed %d", Fullscreen.RefreshRate.Numerator,
                 Fullscreen.RefreshRate.Denominator, Fullscreen.ScanlineOrdering, Fullscreen.Scaling, Fullscreen.Windowed);
}

static
VOID
DcCheckSwapChain(
    _In_ DxgiCheck& Check)
{
    HWND Window = DcCreateWindow(320, 200);
    ComPtr<ID3D11Device> Device;
    ComPtr<IDXGIDevice> DxgiDevice;
    ComPtr<IDXGIAdapter> Adapter;
    ComPtr<IDXGIFactory2> Factory;
    ComPtr<IDXGISwapChain1> SwapChain;
    ComPtr<IDXGISwapChain1> Rejected;
    ComPtr<ID3D11Texture2D> Buffer;
    ComPtr<IDXGISurface> Surface;
    ComPtr<IDXGIOutput> Output;
    ComPtr<IUnknown> Parent;
    ComPtr<ID3D11Device> Owner;
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    D3D11_TEXTURE2D_DESC TextureDesc;
    BOOL IsFullscreen = TRUE;
    HWND Hwnd = NULL;
    UINT Count = 0;

    if (!Check.Expect(Window != NULL, "window") ||
        !Check.Expect(SUCCEEDED(Check.MakeDevice(nullptr, &Device)), "device") ||
        !Check.Expect(SUCCEEDED(Device.As(&DxgiDevice)), "IDXGIDevice") ||
        !Check.Expect(SUCCEEDED(DxgiDevice->GetAdapter(&Adapter)), "GetAdapter") ||
        !Check.Expect(SUCCEEDED(Adapter->GetParent(IID_PPV_ARGS(&Factory))), "factory"))
    {
        return;
    }

    /* Invalid descriptions first */
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 1;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Check.ReportResult("flip_one_buffer", Factory->CreateSwapChainForHwnd(Device.Get(), Window, &Desc, nullptr, nullptr, &Rejected));
    Desc.BufferCount = 2;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    Check.ReportResult("flip_srgb", Factory->CreateSwapChainForHwnd(Device.Get(), Window, &Desc, nullptr, nullptr, &Rejected));
    Desc.Format = DXGI_FORMAT_R32_FLOAT;
    Check.ReportResult("bad_format", Factory->CreateSwapChainForHwnd(Device.Get(), Window, &Desc, nullptr, nullptr, &Rejected));
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Check.ReportResult("null_window", Factory->CreateSwapChainForHwnd(Device.Get(), NULL, &Desc, nullptr, nullptr, &Rejected));
    Check.ReportResult("null_device", Factory->CreateSwapChainForHwnd(nullptr, Window, &Desc, nullptr, nullptr, &Rejected));

    /* A blt swapchain through the DXGI 1.2 entry */
    Desc.BufferCount = 1;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    Check.ReportResult("create_blt", Factory->CreateSwapChainForHwnd(Device.Get(), Window, &Desc, nullptr, nullptr, &SwapChain));
    if (SwapChain != nullptr)
    {
        DcReportSwapChain(Check, "blt", SwapChain.Get());
        ShowWindow(Window, SW_SHOWNOACTIVATE);
        Check.ReportResult("blt.present", SwapChain->Present(0, 0));
        Check.ReportResult("blt.present_again", SwapChain->Present(0, 0));
        Check.ReportResult("blt.present_synced", SwapChain->Present(1, 0));
        ShowWindow(Window, SW_HIDE);
    }
    SwapChain.Reset();
    Desc.BufferCount = 2;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    /* Frame latency needs the waitable flag */
    {
        ComPtr<IDXGISwapChain2> Waitable;
        ComPtr<IDXGISwapChain1> Plain;
        UINT Latency = 99;
        HANDLE Event;

        Check.ReportResult("plain_create", Factory->CreateSwapChainForHwnd(Device.Get(), Window, &Desc, nullptr, nullptr, &Plain));
        if (Plain != nullptr && SUCCEEDED(Plain.As(&Waitable)))
        {
            Check.ReportResult("plain.set_latency", Waitable->SetMaximumFrameLatency(2));
            Check.ReportResult("plain.get_latency", Waitable->GetMaximumFrameLatency(&Latency));
            Event = Waitable->GetFrameLatencyWaitableObject();
            Check.Report("plain.waitable", "%s", Event ? "handle" : "null");
            if (Event != NULL)
                CloseHandle(Event);
        }
        Waitable.Reset();
        Plain.Reset();

        Desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        Check.ReportResult("waitable_create", Factory->CreateSwapChainForHwnd(Device.Get(), Window, &Desc, nullptr, nullptr, &Plain));
        if (Plain != nullptr && SUCCEEDED(Plain.As(&Waitable)))
        {
            Latency = 99;
            Check.ReportResult("waitable.get_default", Waitable->GetMaximumFrameLatency(&Latency));
            Check.Report("waitable.default", "%u", Latency);
            Check.ReportResult("waitable.set_0", Waitable->SetMaximumFrameLatency(0));
            Check.ReportResult("waitable.set_40", Waitable->SetMaximumFrameLatency(40));
            Check.ReportResult("waitable.set_41", Waitable->SetMaximumFrameLatency(41));
            Check.ReportResult("waitable.set_3", Waitable->SetMaximumFrameLatency(3));
            Waitable->GetMaximumFrameLatency(&Latency);
            Check.Report("waitable.latency", "%u", Latency);
            Event = Waitable->GetFrameLatencyWaitableObject();
            Check.Report("waitable.waitable", "%s", Event ? "handle" : "null");
            if (Event != NULL)
            {
                /* The semaphore starts at one and grew by two when the latency went from one to three */
                Check.Report("waitable.wait1", "%lu", WaitForSingleObject(Event, 0));
                Check.Report("waitable.wait2", "%lu", WaitForSingleObject(Event, 0));
                Check.Report("waitable.wait3", "%lu", WaitForSingleObject(Event, 0));
                Check.Report("waitable.wait4", "%lu", WaitForSingleObject(Event, 0));
                CloseHandle(Event);
            }
        }
        Desc.Flags = 0;
    }

    /* A flip discard swapchain sized from the window */
    Check.ReportResult("create_flip", Factory->CreateSwapChainForHwnd(Device.Get(), Window, &Desc, nullptr, nullptr, &SwapChain));
    if (SwapChain == nullptr)
        return;

    DcReportSwapChain(Check, "flip", SwapChain.Get());
    Check.Report("flip.swapchain2", "%s", DcSupports(SwapChain.Get(), __uuidof(IDXGISwapChain2)));
    Check.Report("flip.swapchain3", "%s", DcSupports(SwapChain.Get(), __uuidof(IDXGISwapChain3)));

    Check.ReportResult("flip.get_buffer0", SwapChain->GetBuffer(0, IID_PPV_ARGS(&Buffer)));
    Check.ReportResult("flip.get_buffer1", SwapChain->GetBuffer(1, IID_PPV_ARGS(&Surface)));
    Surface.Reset();
    Check.ReportResult("flip.get_buffer9", SwapChain->GetBuffer(9, IID_PPV_ARGS(&Surface)));
    if (Buffer != nullptr)
    {
        Buffer->GetDesc(&TextureDesc);
        Check.Report("flip.buffer_desc", "%ux%u mips %u array %u fmt %d samples %u usage %d bind %X cpu %X misc %X",
                     TextureDesc.Width, TextureDesc.Height, TextureDesc.MipLevels, TextureDesc.ArraySize,
                     TextureDesc.Format, TextureDesc.SampleDesc.Count, TextureDesc.Usage, TextureDesc.BindFlags,
                     TextureDesc.CPUAccessFlags, TextureDesc.MiscFlags);
        DcReportSurface(Check, "flip.back_buffer", Buffer.Get());
    }

    Check.ReportResult("flip.get_parent", SwapChain->GetParent(IID_PPV_ARGS(&Parent)));
    Check.Report("flip.parent_is_factory", "%s", DcSameObject(Parent.Get(), Factory.Get()) ? "yes" : "no");
    Check.ReportResult("flip.get_device", SwapChain->GetDevice(IID_PPV_ARGS(&Owner)));
    Check.Report("flip.device_is_device", "%s", DcSameObject(Owner.Get(), Device.Get()) ? "yes" : "no");
    SwapChain->GetHwnd(&Hwnd);
    Check.Report("flip.hwnd", "%s", Hwnd == Window ? "same" : "other");
    Check.ReportResult("flip.fullscreen_state", SwapChain->GetFullscreenState(&IsFullscreen, nullptr));
    Check.Report("flip.is_fullscreen", "%d", IsFullscreen);
    Check.ReportResult("flip.containing_output", SwapChain->GetContainingOutput(&Output));

    /* A buffer still held keeps ResizeBuffers from working */
    Check.ReportResult("flip.resize_while_held", SwapChain->ResizeBuffers(0, 400, 300, DXGI_FORMAT_UNKNOWN, 0));
    Buffer.Reset();
    Check.ReportResult("flip.resize", SwapChain->ResizeBuffers(0, 400, 300, DXGI_FORMAT_UNKNOWN, 0));
    DcReportSwapChain(Check, "resized", SwapChain.Get());
    Check.ReportResult("flip.resize_count", SwapChain->ResizeBuffers(3, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
    DcReportSwapChain(Check, "resized_window", SwapChain.Get());

    Check.ReportResult("flip.present_test", SwapChain->Present(0, DXGI_PRESENT_TEST));
    Check.ReportResult("flip.present_bad_interval", SwapChain->Present(5, 0));
    ShowWindow(Window, SW_SHOWNOACTIVATE);
    {
        ComPtr<IDXGISwapChain3> SwapChain3;

        if (SUCCEEDED(SwapChain.As(&SwapChain3)))
            Check.Report("flip.index_before", "%u", SwapChain3->GetCurrentBackBufferIndex());
        Check.ReportResult("flip.present", SwapChain->Present(0, 0));
        if (SwapChain3 != nullptr)
            Check.Report("flip.index_after1", "%u", SwapChain3->GetCurrentBackBufferIndex());
        Check.ReportResult("flip.present_again", SwapChain->Present(0, 0));
        if (SwapChain3 != nullptr)
            Check.Report("flip.index_after2", "%u", SwapChain3->GetCurrentBackBufferIndex());
        Check.ReportResult("flip.present_third", SwapChain->Present(1, 0));
        if (SwapChain3 != nullptr)
            Check.Report("flip.index_after3", "%u", SwapChain3->GetCurrentBackBufferIndex());
    }
    SwapChain->GetLastPresentCount(&Count);
    Check.Report("flip.present_count", "%u", Count);

    SwapChain.Reset();
    DestroyWindow(Window);
}

/* surface: the DXGI face of D3D11 resources */

static
VOID
DcReportSurface(
    _In_ DxgiCheck& Check,
    _In_z_ PCSTR Prefix,
    _In_ ID3D11Resource* Resource)
{
    ComPtr<IDXGISurface2> Surface;
    ComPtr<IDXGIResource1> DxgiResource;
    ComPtr<IUnknown> Identity;
    ComPtr<IUnknown> SurfaceIdentity;
    ComPtr<ID3D11Resource> Back;
    ComPtr<IDXGIDevice> Parent;
    DXGI_SURFACE_DESC Desc = {};
    DXGI_USAGE Usage = 0;
    HANDLE Shared = (HANDLE)1;
    UINT Priority = 0;
    UINT Subresource = 7;
    HRESULT hr;

    Check.Report(DcKey("%s.surface", Prefix).c_str(), "%s", DcSupports(Resource, __uuidof(IDXGISurface)));
    Check.Report(DcKey("%s.surface2", Prefix).c_str(), "%s", DcSupports(Resource, __uuidof(IDXGISurface2)));
    Check.Report(DcKey("%s.resource1", Prefix).c_str(), "%s", DcSupports(Resource, __uuidof(IDXGIResource1)));
    if (FAILED(Resource->QueryInterface(IID_PPV_ARGS(&DxgiResource))))
        return;

    DxgiResource->GetUsage(&Usage);
    Check.Report(DcKey("%s.usage", Prefix).c_str(), "%X", (UINT)Usage);
    hr = DxgiResource->GetSharedHandle(&Shared);
    Check.Report(DcKey("%s.shared_handle", Prefix).c_str(), "0x%08lX %s", hr, Shared ? "set" : "null");
    DxgiResource->GetEvictionPriority(&Priority);
    Check.Report(DcKey("%s.priority", Prefix).c_str(), "%X", Priority);
    Check.ReportResult(DcKey("%s.get_parent", Prefix).c_str(), DxgiResource->GetParent(IID_PPV_ARGS(&Parent)));
    Resource->QueryInterface(IID_PPV_ARGS(&Identity));
    DxgiResource->QueryInterface(IID_PPV_ARGS(&SurfaceIdentity));
    Check.Report(DcKey("%s.same_identity", Prefix).c_str(), "%s", Identity.Get() == SurfaceIdentity.Get() ? "yes" : "no");

    if (FAILED(Resource->QueryInterface(IID_PPV_ARGS(&Surface))))
        return;

    hr = Surface->GetDesc(&Desc);
    Check.Report(DcKey("%s.desc", Prefix).c_str(), "0x%08lX %ux%u fmt %d samples %u/%u", hr, Desc.Width, Desc.Height,
                 Desc.Format, Desc.SampleDesc.Count, Desc.SampleDesc.Quality);
    hr = Surface->GetResource(IID_PPV_ARGS(&Back), &Subresource);
    Check.Report(DcKey("%s.get_resource", Prefix).c_str(), "0x%08lX %u %s", hr, Subresource,
                 Back.Get() == Resource ? "same" : "other");
}

static
VOID
DcCheckSurface(
    _In_ DxgiCheck& Check)
{
    static const UINT MapFlags[] = { 0, DXGI_MAP_READ, DXGI_MAP_WRITE, DXGI_MAP_READ | DXGI_MAP_WRITE,
                                     DXGI_MAP_WRITE | DXGI_MAP_DISCARD, DXGI_MAP_DISCARD,
                                     DXGI_MAP_READ | DXGI_MAP_DISCARD };
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<ID3D11Texture2D> Mipped;
    ComPtr<ID3D11Texture2D> Dynamic;
    ComPtr<ID3D11Texture2D> Staging;
    ComPtr<ID3D11Buffer> Buffer;
    ComPtr<IDXGISurface> Surface;
    D3D11_TEXTURE2D_DESC Desc = {};
    D3D11_BUFFER_DESC BufferDesc = {};
    DXGI_MAPPED_RECT Mapped;
    UINT Index;
    HRESULT hr;

    if (!Check.Expect(SUCCEEDED(Check.MakeDevice(nullptr, &Device)), "device"))
        return;

    Desc.Width = 64;
    Desc.Height = 32;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    Check.Expect(SUCCEEDED(Device->CreateTexture2D(&Desc, nullptr, &Texture)), "texture");
    DcReportSurface(Check, "texture", Texture.Get());

    Desc.MipLevels = 0;
    Check.Expect(SUCCEEDED(Device->CreateTexture2D(&Desc, nullptr, &Mipped)), "mipped texture");
    DcReportSurface(Check, "mipped", Mipped.Get());

    Desc.MipLevels = 1;
    Desc.Usage = D3D11_USAGE_DYNAMIC;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Check.Expect(SUCCEEDED(Device->CreateTexture2D(&Desc, nullptr, &Dynamic)), "dynamic texture");
    DcReportSurface(Check, "dynamic", Dynamic.Get());

    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    Check.Expect(SUCCEEDED(Device->CreateTexture2D(&Desc, nullptr, &Staging)), "staging texture");
    DcReportSurface(Check, "staging", Staging.Get());

    BufferDesc.ByteWidth = 256;
    BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    Check.Expect(SUCCEEDED(Device->CreateBuffer(&BufferDesc, nullptr, &Buffer)), "buffer");
    DcReportSurface(Check, "buffer", Buffer.Get());

    /* Map flags against default, dynamic and staging resources */
    for (Index = 0; Index < _countof(MapFlags); Index++)
    {
        ID3D11Texture2D* Targets[] = { Texture.Get(), Dynamic.Get(), Staging.Get() };
        PCSTR Names[] = { "default", "dynamic", "staging" };
        UINT Target;

        for (Target = 0; Target < _countof(Targets); Target++)
        {
            Surface.Reset();
            if (FAILED(Targets[Target]->QueryInterface(IID_PPV_ARGS(&Surface))))
                continue;

            Mapped = {};
            hr = Surface->Map(&Mapped, MapFlags[Index]);
            Check.Report(DcKey("map.%s.%u", Names[Target], MapFlags[Index]).c_str(), "0x%08lX %s", hr,
                         (SUCCEEDED(hr) && Mapped.pBits && Mapped.Pitch >= 256) ? "mapped" : "-");
            if (SUCCEEDED(hr))
                Surface->Unmap();
        }
    }
}

/* methods: the DXGI 1.3 to 1.6 factory and adapter additions */

static
VOID
DcCheckMethods(
    _In_ DxgiCheck& Check)
{
    ComPtr<IDXGIFactory7> Factory;
    ComPtr<IDXGIFactory3> Legacy;
    ComPtr<IDXGIFactory3> Debug;
    ComPtr<IDXGIAdapter4> Adapter;
    ComPtr<IDXGIAdapter1> Warp;
    ComPtr<ID3D11Device> Device;
    ComPtr<IDXGISwapChain1> SwapChain;
    DXGI_ADAPTER_DESC1 Desc1;
    DXGI_ADAPTER_DESC2 Desc2;
    DXGI_ADAPTER_DESC3 Desc3;
    DXGI_QUERY_VIDEO_MEMORY_INFO Memory;
    DXGI_SWAP_CHAIN_DESC1 SwapDesc = {};
    HANDLE Event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE Other = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HWND Window = DcCreateWindow(64, 64);
    LUID Missing = { 0x12345, 0x7 };
    DWORD Cookie = 0;
    DWORD Second = 0;
    BOOL Tearing = FALSE;
    UINT64 Big = 0;
    UINT Preference;
    UINT Index;
    HRESULT hr;

    if (!Check.Expect(SUCCEEDED(Check.CreateFactory2(0, IID_PPV_ARGS(&Factory))), "factory 7"))
        return;

    Check.Report("flags_create2", "%X", Factory->GetCreationFlags());
    if (SUCCEEDED(Check.CreateFactory(IID_PPV_ARGS(&Legacy))))
        Check.Report("flags_create", "%X", Legacy->GetCreationFlags());
    hr = Check.CreateFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&Debug));
    Check.Report("flags_debug", "0x%08lX %X", hr, Debug ? Debug->GetCreationFlags() : 0);

    hr = Factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &Tearing, sizeof(Tearing));
    Check.Report("tearing", "0x%08lX %d", hr, Tearing);
    Check.ReportResult("tearing_size8", Factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &Big, sizeof(Big)));
    Check.ReportResult("feature_1", Factory->CheckFeatureSupport((DXGI_FEATURE)1, &Tearing, sizeof(Tearing)));

    hr = Factory->EnumWarpAdapter(IID_PPV_ARGS(&Warp));
    Check.ReportResult("warp", hr);
    if (Warp != nullptr)
    {
        Warp->GetDesc1(&Desc1);
        Check.Report("warp_desc", "%s %X", DcNarrow(Desc1.Description).c_str(), Desc1.Flags);
    }
    Check.ReportResult("by_luid_missing", Factory->EnumAdapterByLuid(Missing, IID_PPV_ARGS(&Adapter)));

    for (Preference = 0; Preference < 4; Preference++)
    {
        for (Index = 0; Index < 4; Index++)
        {
            Adapter.Reset();
            hr = Factory->EnumAdapterByGpuPreference(Index, (DXGI_GPU_PREFERENCE)Preference, IID_PPV_ARGS(&Adapter));
            if (FAILED(hr))
            {
                Check.ReportResult(DcKey("preference%u.%u", Preference, Index).c_str(), hr);
                break;
            }
            Adapter->GetDesc1(&Desc1);
            Check.Report(DcKey("preference%u.%u", Preference, Index).c_str(), "%s", DcNarrow(Desc1.Description).c_str());
        }
    }

    for (Index = 0; SUCCEEDED(Factory->EnumAdapterByGpuPreference(Index, DXGI_GPU_PREFERENCE_UNSPECIFIED,
                                                                   IID_PPV_ARGS(&Adapter))); Index++)
    {
        Adapter->GetDesc2(&Desc2);
        Adapter->GetDesc3(&Desc3);
        Check.Report(DcKey("%u.desc2", Index).c_str(), "flags %X preempt %d %d", Desc2.Flags,
                     Desc2.GraphicsPreemptionGranularity, Desc2.ComputePreemptionGranularity);
        Check.Report(DcKey("%u.desc3", Index).c_str(), "flags %X preempt %d %d", Desc3.Flags,
                     Desc3.GraphicsPreemptionGranularity, Desc3.ComputePreemptionGranularity);

        Memory = {};
        hr = Adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Memory);
        Check.Report(DcKey("%u.memory_local", Index).c_str(), "0x%08lX %s", hr, Memory.Budget ? "budget" : "none");
        Memory = {};
        hr = Adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &Memory);
        Check.Report(DcKey("%u.memory_nonlocal", Index).c_str(), "0x%08lX %s", hr, Memory.Budget ? "budget" : "none");
        Check.ReportResult(DcKey("%u.memory_node9", Index).c_str(),
                           Adapter->QueryVideoMemoryInfo(9, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Memory));
        Check.ReportResult(DcKey("%u.reserve0", Index).c_str(),
                           Adapter->SetVideoMemoryReservation(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, 0));

        ResetEvent(Event);
        hr = Adapter->RegisterVideoMemoryBudgetChangeNotificationEvent(Event, &Cookie);
        Check.Report(DcKey("%u.budget_event", Index).c_str(), "0x%08lX %lu signaled %d", hr, Cookie,
                     WaitForSingleObject(Event, 0) == WAIT_OBJECT_0);
        Adapter->UnregisterVideoMemoryBudgetChangeNotification(Cookie);
        ResetEvent(Event);
        hr = Adapter->RegisterHardwareContentProtectionTeardownStatusEvent(Event, &Cookie);
        Check.Report(DcKey("%u.teardown_event", Index).c_str(), "0x%08lX %lu signaled %d", hr, Cookie,
                     WaitForSingleObject(Event, 0) == WAIT_OBJECT_0);
        Adapter->UnregisterHardwareContentProtectionTeardownStatus(Cookie);
        Adapter.Reset();
    }

    /* Event registrations: cookies, duplicates, signaling and unknown cookies */
    ResetEvent(Event);
    hr = Factory->RegisterAdaptersChangedEvent(Event, &Cookie);
    Check.Report("adapters_event", "0x%08lX %lu signaled %d", hr, Cookie, WaitForSingleObject(Event, 0) == WAIT_OBJECT_0);
    Check.ReportResult("adapters_event_again", Factory->RegisterAdaptersChangedEvent(Event, &Second));
    hr = Factory->RegisterAdaptersChangedEvent(Other, &Second);
    Check.Report("adapters_event_other", "0x%08lX %lu", hr, Second);
    Check.ReportResult("adapters_event_null", Factory->RegisterAdaptersChangedEvent(NULL, &Second));
    Check.ReportResult("adapters_unregister", Factory->UnregisterAdaptersChangedEvent(Cookie));
    Check.ReportResult("adapters_unregister_again", Factory->UnregisterAdaptersChangedEvent(Cookie));
    hr = Factory->RegisterAdaptersChangedEvent(Event, &Cookie);
    Check.Report("adapters_reuse_cookie", "0x%08lX %lu", hr, Cookie);
    Factory->UnregisterAdaptersChangedEvent(Cookie);
    Factory->UnregisterAdaptersChangedEvent(Second);

    ResetEvent(Event);
    hr = Factory->RegisterOcclusionStatusEvent(Event, &Cookie);
    Check.Report("occlusion_event", "0x%08lX %lu signaled %d", hr, Cookie, WaitForSingleObject(Event, 0) == WAIT_OBJECT_0);
    Factory->UnregisterOcclusionStatus(Cookie);
    Check.ReportResult("occlusion_window_msg0", Factory->RegisterOcclusionStatusWindow(Window, 0, &Cookie));
    Check.ReportResult("occlusion_window_broadcast", Factory->RegisterOcclusionStatusWindow(HWND_BROADCAST, WM_USER, &Cookie));
    hr = Factory->RegisterOcclusionStatusWindow(Window, WM_USER, &Cookie);
    Check.Report("occlusion_window", "0x%08lX %lu", hr, Cookie);
    Factory->UnregisterOcclusionStatus(Cookie);
    ResetEvent(Event);
    hr = Factory->RegisterStereoStatusEvent(Event, &Cookie);
    Check.Report("stereo_event", "0x%08lX %lu signaled %d", hr, Cookie, WaitForSingleObject(Event, 0) == WAIT_OBJECT_0);
    Factory->UnregisterStereoStatus(Cookie);

    Check.ReportResult("shared_luid_null", Factory->GetSharedResourceAdapterLuid(NULL, &Missing));
    Check.ReportResult("software_null", Factory->CreateSoftwareAdapter(NULL, nullptr));

    /* Composition swapchain validation */
    if (SUCCEEDED(Check.MakeDevice(nullptr, &Device)))
    {
        SwapDesc.Width = 64;
        SwapDesc.Height = 64;
        SwapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        SwapDesc.SampleDesc.Count = 1;
        SwapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        SwapDesc.BufferCount = 2;
        SwapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        Check.ReportResult("composition_blt", Factory->CreateSwapChainForComposition(Device.Get(), &SwapDesc, nullptr, &SwapChain));
        SwapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        SwapDesc.Scaling = DXGI_SCALING_NONE;
        Check.ReportResult("composition_scaling", Factory->CreateSwapChainForComposition(Device.Get(), &SwapDesc, nullptr, &SwapChain));
        SwapDesc.Scaling = DXGI_SCALING_STRETCH;
        SwapDesc.Width = 0;
        Check.ReportResult("composition_zero", Factory->CreateSwapChainForComposition(Device.Get(), &SwapDesc, nullptr, &SwapChain));
        SwapDesc.Width = 64;

        /* A valid one: no window, but a surface DirectComposition can read through its private face */
        if (SUCCEEDED(Factory->CreateSwapChainForComposition(Device.Get(), &SwapDesc, nullptr, &SwapChain)))
        {
            const GUID CompositionFace = { 0x7652c85a, 0x0df5, 0x4567, { 0xbc, 0xdd, 0x81, 0x76, 0xa3, 0x32, 0xb9, 0x9b } };
            ComPtr<IUnknown> Face;
            HWND Owner = (HWND)1;

            Check.Report("composition_valid", "created");
            Check.Report("composition_hwnd", "0x%08lX %s", SwapChain->GetHwnd(&Owner), Owner ? "set" : "null");
            Check.ReportResult("composition_face", SwapChain->QueryInterface(CompositionFace, &Face));
            Check.ReportResult("composition_present", SwapChain->Present(0, 0));
            SwapChain.Reset();
        }

        Check.ReportResult("corewindow_hwnd", Factory->CreateSwapChainForCoreWindow(Device.Get(), Device.Get(), &SwapDesc, nullptr, &SwapChain));
    }

    CloseHandle(Event);
    CloseHandle(Other);
    DestroyWindow(Window);
}

/* reservation: when the kernel accepts a video memory reservation for this process */

static
VOID
DcCheckReservation(
    _In_ DxgiCheck& Check)
{
    ComPtr<IDXGIFactory4> Factory;
    ComPtr<IDXGIAdapter3> Adapter;
    ComPtr<IDXGIAdapter3> Other;
    ComPtr<ID3D11Device> Device;

    if (!Check.Expect(SUCCEEDED(Check.CreateFactory1(IID_PPV_ARGS(&Factory))), "factory") ||
        !Check.Expect(SUCCEEDED(Factory->EnumAdapters1(0, reinterpret_cast<IDXGIAdapter1**>(Adapter.GetAddressOf()))),
                      "adapter"))
    {
        return;
    }

    Check.ReportResult("no_device", Adapter->SetVideoMemoryReservation(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, 0));

    /* A device on this very adapter object */
    Check.Expect(SUCCEEDED(Check.MakeDevice(Adapter.Get(), &Device)), "device on adapter");
    Check.ReportResult("own_device", Adapter->SetVideoMemoryReservation(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, 0));
    Check.ReportResult("own_device_1mb",
                       Adapter->SetVideoMemoryReservation(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, 1024 * 1024));
    Factory->EnumAdapters1(0, reinterpret_cast<IDXGIAdapter1**>(Other.GetAddressOf()));
    Check.ReportResult("other_object", Other->SetVideoMemoryReservation(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, 0));
    Device.Reset();
    Check.ReportResult("after_release", Adapter->SetVideoMemoryReservation(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, 0));

    /* A device made without an adapter, on its own factory */
    Check.Expect(SUCCEEDED(Check.MakeDevice(nullptr, &Device)), "device without adapter");
    Check.ReportResult("default_device", Other->SetVideoMemoryReservation(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, 0));
    Device.Reset();
}

/* flipflags: present flags and HDR metadata on a flip swapchain that allows tearing */

static
VOID
DcCheckFlipFlags(
    _In_ DxgiCheck& Check)
{
    HWND Window = DcCreateWindow(160, 120);
    ComPtr<ID3D11Device> Device;
    ComPtr<IDXGIDevice> DxgiDevice;
    ComPtr<IDXGIAdapter> Adapter;
    ComPtr<IDXGIFactory2> Factory;
    ComPtr<IDXGISwapChain1> SwapChain;
    ComPtr<IDXGISwapChain4> SwapChain4;
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    DXGI_HDR_METADATA_HDR10 Metadata = {};

    if (!Check.Expect(Window != NULL, "window") ||
        !Check.Expect(SUCCEEDED(Check.MakeDevice(nullptr, &Device)), "device") ||
        !Check.Expect(SUCCEEDED(Device.As(&DxgiDevice)), "IDXGIDevice") ||
        !Check.Expect(SUCCEEDED(DxgiDevice->GetAdapter(&Adapter)), "GetAdapter") ||
        !Check.Expect(SUCCEEDED(Adapter->GetParent(IID_PPV_ARGS(&Factory))), "factory"))
    {
        return;
    }

    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 3;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    Desc.Stereo = TRUE;
    Check.ReportResult("create_stereo", Factory->CreateSwapChainForHwnd(Device.Get(), Window, &Desc, nullptr, nullptr, &SwapChain));
    Check.Report("stereo_enabled", "%d", Factory->IsWindowedStereoEnabled());
    if (SwapChain != nullptr)
    {
        ComPtr<ID3D11Texture2D> Eyes;
        D3D11_TEXTURE2D_DESC EyesDesc = {};

        DcReportSwapChain(Check, "stereo", SwapChain.Get());
        Check.Report("stereo.temporary_mono", "%d", SwapChain->IsTemporaryMonoSupported());
        if (SUCCEEDED(SwapChain->GetBuffer(0, IID_PPV_ARGS(&Eyes))))
        {
            Eyes->GetDesc(&EyesDesc);
            Check.Report("stereo.buffer", "%ux%u array %u bind %X misc %X", EyesDesc.Width, EyesDesc.Height,
                         EyesDesc.ArraySize, EyesDesc.BindFlags, EyesDesc.MiscFlags);
            Eyes.Reset();
        }
        ShowWindow(Window, SW_SHOWNOACTIVATE);
        Check.ReportResult("stereo.present", SwapChain->Present(1, 0));
        Check.ReportResult("stereo.present_mono", SwapChain->Present(1, DXGI_PRESENT_STEREO_TEMPORARY_MONO));
        Check.ReportResult("stereo.present_right", SwapChain->Present(1, DXGI_PRESENT_STEREO_PREFER_RIGHT));
        ShowWindow(Window, SW_HIDE);
    }
    SwapChain.Reset();
    Desc.Stereo = FALSE;
    Check.ReportResult("create_tearing", Factory->CreateSwapChainForHwnd(Device.Get(), Window, &Desc, nullptr, nullptr, &SwapChain));
    if (SwapChain == nullptr || FAILED(SwapChain.As(&SwapChain4)))
        return;

    ShowWindow(Window, SW_SHOWNOACTIVATE);
    Check.ReportResult("tearing_present", SwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING));
    Check.ReportResult("tearing_synced", SwapChain->Present(1, DXGI_PRESENT_ALLOW_TEARING));
    Check.ReportResult("plain_present", SwapChain->Present(0, 0));

    Metadata.MaxMasteringLuminance = 1000;
    Metadata.MaxContentLightLevel = 800;
    Check.ReportResult("hdr_set", SwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(Metadata), &Metadata));
    Check.ReportResult("hdr_present", SwapChain->Present(1, 0));
    Check.ReportResult("hdr_present_again", SwapChain->Present(1, 0));

    Check.ReportResult("do_not_sequence", SwapChain->Present(0, DXGI_PRESENT_DO_NOT_SEQUENCE));
    Check.ReportResult("restart", SwapChain->Present(0, DXGI_PRESENT_RESTART));
    Check.ReportResult("do_not_wait", SwapChain->Present(0, DXGI_PRESENT_DO_NOT_WAIT));

    SwapChain4.Reset();
    SwapChain.Reset();
    DestroyWindow(Window);
}

/* legacy: CreateSwapChain and D3D11CreateDeviceAndSwapChain with the DXGI 1.0 description */

static
VOID
DcCheckLegacySwapChain(
    _In_ DxgiCheck& Check)
{
    static const D3D_FEATURE_LEVEL Level = D3D_FEATURE_LEVEL_11_0;
    HWND Window = DcCreateWindow(256, 128);
    DXGI_SWAP_CHAIN_DESC Desc = {};
    ComPtr<IDXGISwapChain> SwapChain;
    ComPtr<IDXGISwapChain1> SwapChain1;
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    D3D_FEATURE_LEVEL Created = (D3D_FEATURE_LEVEL)0;
    HRESULT hr;

    if (!Check.Expect(Window != NULL, "window"))
        return;

    Desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 1;
    Desc.OutputWindow = Window;
    Desc.Windowed = TRUE;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = Check.CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &Level, 1, D3D11_SDK_VERSION, &Desc,
                                        &SwapChain, &Device, &Created, &Context);
    Check.ReportResult("create_with_device", hr);
    Check.Report("created_level", "0x%X", Created);
    if (SwapChain != nullptr && SUCCEEDED(SwapChain.As(&SwapChain1)))
        DcReportSwapChain(Check, "blt", SwapChain1.Get());

    SwapChain.Reset();
    SwapChain1.Reset();
    Device.Reset();
    Context.Reset();
    hr = Check.CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &Level, 1, D3D11_SDK_VERSION, nullptr,
                                        &SwapChain, &Device, nullptr, nullptr);
    Check.ReportResult("null_desc", hr);
    Check.Report("null_desc_device", "%s", Device ? "set" : "null");

    DestroyWindow(Window);
}

/* corewindow: swapchains on a CoreWindow made through the Windows.UI creation export (ordinal 1500) */

typedef HRESULT (WINAPI* PFN_DC_CREATE_CORE_WINDOW)(INT32 Type, PCWSTR Title, INT32 X, INT32 Y, UINT32 Width, UINT32 Height,
                                                    DWORD Attributes, HWND Owner, REFIID riid, void** ppWindow);

static
VOID
DcCheckCoreWindow(
    _In_ DxgiCheck& Check)
{
    HMODULE WindowsUi = LoadLibraryW(L"Windows.UI.dll");
    PFN_DC_CREATE_CORE_WINDOW CreateCoreWindow;
    ComPtr<ABI::Windows::UI::Core::ICoreWindow> Window;
    ComPtr<ICoreWindowInterop> Interop;
    ComPtr<ID3D11Device> Device;
    ComPtr<IDXGIDevice> DxgiDevice;
    ComPtr<IDXGIAdapter> Adapter;
    ComPtr<IDXGIFactory2> Factory;
    ComPtr<IDXGISwapChain1> SwapChain;
    ComPtr<IDXGISwapChain1> Rejected;
    ComPtr<IUnknown> Returned;
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    HWND Handle = NULL;
    HWND Hwnd = (HWND)1;
    RECT Client = {};
    BOOL IsFullscreen = TRUE;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC FullscreenDesc;
    HRESULT hr;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CreateCoreWindow = WindowsUi ? reinterpret_cast<PFN_DC_CREATE_CORE_WINDOW>(GetProcAddress(WindowsUi, MAKEINTRESOURCEA(1500))) : nullptr;
    if (!Check.Expect(CreateCoreWindow != nullptr, "Windows.UI creation export") ||
        !Check.Expect(SUCCEEDED(CreateCoreWindow(6, L"dxgicheck", 40, 40, 320, 200, 0, NULL,
                                                 __uuidof(ABI::Windows::UI::Core::ICoreWindow),
                                                 reinterpret_cast<void**>(Window.GetAddressOf()))), "CoreWindow") ||
        !Check.Expect(SUCCEEDED(Window.As(&Interop)) && SUCCEEDED(Interop->get_WindowHandle(&Handle)), "window handle") ||
        !Check.Expect(SUCCEEDED(Check.MakeDevice(nullptr, &Device)), "device") ||
        !Check.Expect(SUCCEEDED(Device.As(&DxgiDevice)), "IDXGIDevice") ||
        !Check.Expect(SUCCEEDED(DxgiDevice->GetAdapter(&Adapter)), "GetAdapter") ||
        !Check.Expect(SUCCEEDED(Adapter->GetParent(IID_PPV_ARGS(&Factory))), "factory"))
    {
        Interop.Reset();
        Window.Reset();
        CoUninitialize();
        return;
    }

    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 2;
    Desc.Scaling = DXGI_SCALING_STRETCH;

    /* Creation rules */
    Desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    Check.ReportResult("blt_discard", Factory->CreateSwapChainForCoreWindow(Device.Get(), Window.Get(), &Desc, nullptr, &Rejected));
    Desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    Check.ReportResult("blt_sequential", Factory->CreateSwapChainForCoreWindow(Device.Get(), Window.Get(), &Desc, nullptr, &Rejected));
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    Check.ReportResult("null_window", Factory->CreateSwapChainForCoreWindow(Device.Get(), nullptr, &Desc, nullptr, &Rejected));
    Check.ReportResult("null_device", Factory->CreateSwapChainForCoreWindow(nullptr, Window.Get(), &Desc, nullptr, &Rejected));
    Check.ReportResult("null_desc", Factory->CreateSwapChainForCoreWindow(Device.Get(), Window.Get(), nullptr, nullptr, &Rejected));
    Desc.BufferCount = 1;
    Check.ReportResult("one_buffer", Factory->CreateSwapChainForCoreWindow(Device.Get(), Window.Get(), &Desc, nullptr, &Rejected));
    Desc.BufferCount = 2;

    /* Scaling modes, each released before the next */
    Desc.Scaling = DXGI_SCALING_NONE;
    Check.ReportResult("scaling_none", Factory->CreateSwapChainForCoreWindow(Device.Get(), Window.Get(), &Desc, nullptr, &Rejected));
    Rejected.Reset();
    Desc.Scaling = DXGI_SCALING_ASPECT_RATIO_STRETCH;
    Check.ReportResult("scaling_aspect", Factory->CreateSwapChainForCoreWindow(Device.Get(), Window.Get(), &Desc, nullptr, &Rejected));
    Rejected.Reset();
    Check.ReportResult("hwnd_aspect", Factory->CreateSwapChainForHwnd(Device.Get(), Handle, &Desc, nullptr, nullptr, &Rejected));
    Rejected.Reset();
    Desc.Scaling = DXGI_SCALING_STRETCH;

    /* A zero size takes the window's */
    GetClientRect(Handle, &Client);
    Check.Report("client", "%ldx%ld", Client.right - Client.left, Client.bottom - Client.top);
    Desc.Width = 0;
    Desc.Height = 0;
    hr = Factory->CreateSwapChainForCoreWindow(Device.Get(), Window.Get(), &Desc, nullptr, &SwapChain);
    Check.ReportResult("create", hr);
    if (FAILED(hr))
    {
        Interop.Reset();
        Window.Reset();
        CoUninitialize();
        return;
    }

    DcReportSwapChain(Check, "valid", SwapChain.Get());
    Check.ReportResult("get_fullscreen_desc", SwapChain->GetFullscreenDesc(&FullscreenDesc));
    Check.ReportResult("get_hwnd", SwapChain->GetHwnd(&Hwnd));
    Check.Report("get_hwnd_value", "%s", Hwnd == Handle ? "window" : (Hwnd ? "other" : "null"));
    hr = SwapChain->GetCoreWindow(__uuidof(ABI::Windows::UI::Core::ICoreWindow), reinterpret_cast<void**>(Returned.GetAddressOf()));
    Check.Report("get_core_window", "0x%08lX %s", hr, DcSameObject(Returned.Get(), Window.Get()) ? "same" : "other");
    Returned.Reset();
    hr = SwapChain->GetCoreWindow(IID_PPV_ARGS(&Returned));
    Check.Report("get_core_window_unknown", "0x%08lX %s", hr, DcSameObject(Returned.Get(), Window.Get()) ? "same" : "other");
    Returned.Reset();
    Check.ReportResult("get_core_window_device", SwapChain->GetCoreWindow(__uuidof(ID3D11Device), reinterpret_cast<void**>(Returned.GetAddressOf())));
    Returned.Reset();
    Check.ReportResult("get_core_window_null", SwapChain->GetCoreWindow(__uuidof(IUnknown), nullptr));

    /* One swapchain per window */
    Check.ReportResult("second", Factory->CreateSwapChainForCoreWindow(Device.Get(), Window.Get(), &Desc, nullptr, &Rejected));
    Rejected.Reset();
    Check.ReportResult("second_hwnd", Factory->CreateSwapChainForHwnd(Device.Get(), Handle, &Desc, nullptr, nullptr, &Rejected));
    Rejected.Reset();

    Check.ReportResult("set_fullscreen", SwapChain->SetFullscreenState(TRUE, nullptr));
    Check.ReportResult("get_fullscreen", SwapChain->GetFullscreenState(&IsFullscreen, nullptr));
    Check.Report("fullscreen", "%d", IsFullscreen);
    Check.ReportResult("present", SwapChain->Present(0, 0));
    Check.ReportResult("resize", SwapChain->ResizeBuffers(0, 128, 96, DXGI_FORMAT_UNKNOWN, 0));
    DcReportSwapChain(Check, "sized", SwapChain.Get());
    Check.ReportResult("resize_zero", SwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0));
    DcReportSwapChain(Check, "resized", SwapChain.Get());

    SwapChain.Reset();
    Check.ReportResult("after_release", Factory->CreateSwapChainForHwnd(Device.Get(), Handle, &Desc, nullptr, nullptr, &Rejected));
    Rejected.Reset();

    Interop.Reset();
    Window->Close();
    Window.Reset();
    CoUninitialize();
}

/* association: MakeWindowAssociation and what GetWindowAssociation reports back */

static
VOID
DcCheckAssociation(
    _In_ DxgiCheck& Check)
{
    HWND Window = DcCreateWindow(64, 64);
    ComPtr<IDXGIFactory1> Factory;
    HWND Associated = (HWND)1;

    if (!Check.Expect(Window != NULL, "window") ||
        !Check.Expect(SUCCEEDED(Check.CreateFactory1(IID_PPV_ARGS(&Factory))), "factory"))
    {
        return;
    }

    Check.ReportResult("initial", Factory->GetWindowAssociation(&Associated));
    Check.Report("initial_window", "%s", Associated ? "set" : "null");
    Check.ReportResult("make", Factory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER));
    Check.ReportResult("get", Factory->GetWindowAssociation(&Associated));
    Check.Report("get_window", "%s", Associated == Window ? "same" : (Associated ? "other" : "null"));
    Check.ReportResult("bad_flags", Factory->MakeWindowAssociation(Window, 0x10));
    Check.ReportResult("null_window", Factory->MakeWindowAssociation(NULL, 0));
    Check.ReportResult("get_null", Factory->GetWindowAssociation(nullptr));

    DestroyWindow(Window);
}

static const CheckInfo Checks[] =
{
    { "factory", DcCheckFactory },
    { "adapters", DcCheckAdapters },
    { "outputs", DcCheckOutputs },
    { "devicelink", DcCheckDeviceLink },
    { "swapchain", DcCheckSwapChain },
    { "legacy", DcCheckLegacySwapChain },
    { "association", DcCheckAssociation },
    { "surface", DcCheckSurface },
    { "methods", DcCheckMethods },
    { "reservation", DcCheckReservation },
    { "flipflags", DcCheckFlipFlags },
    { "corewindow", DcCheckCoreWindow },
};

/* System32 for "system", the ReactX build next to this exe for "reactx", else a directory */
static
std::wstring
DcRuntimeDirectory(
    _In_z_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"";

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\";
    }

    return std::wstring(Runtime) + L"\\";
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    std::vector<std::string> Wanted;
    PCWSTR Runtime = L"system";
    std::wstring Directory;
    DxgiCheck Check;
    HMODULE Dxgi;
    HMODULE D3D11;
    UINT Failures = 0;
    int Index;
    char Name[128];

    /* A crash in a check must not take the facts already printed with it */
    setvbuf(stdout, nullptr, _IONBF, 0);

    for (Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-list") == 0)
        {
            for (const CheckInfo& Info : Checks)
                printf("%s\n", Info.Name);
            return 0;
        }

        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Runtime = argv[++Index];
            continue;
        }

        WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
        Wanted.push_back(Name);
    }

    /* dxgi first, so the d3d11 of the same pair finds it already loaded */
    Directory = DcRuntimeDirectory(Runtime);
    Dxgi = LoadLibraryW((Directory + L"dxgi.dll").c_str());
    D3D11 = LoadLibraryW((Directory + L"d3d11.dll").c_str());
    if (Dxgi == NULL || D3D11 == NULL)
    {
        fprintf(stderr, "error: cannot load the runtime pair\n");
        return 1;
    }

    Check.CreateFactory = reinterpret_cast<PFN_DC_CREATE_FACTORY>(GetProcAddress(Dxgi, "CreateDXGIFactory"));
    Check.CreateFactory1 = reinterpret_cast<PFN_DC_CREATE_FACTORY>(GetProcAddress(Dxgi, "CreateDXGIFactory1"));
    Check.CreateFactory2 = reinterpret_cast<PFN_DC_CREATE_FACTORY2>(GetProcAddress(Dxgi, "CreateDXGIFactory2"));
    Check.GetDebugInterface1 = reinterpret_cast<PFN_DC_GET_DEBUG_INTERFACE1>(GetProcAddress(Dxgi, "DXGIGetDebugInterface1"));
    Check.DeclareRemovalSupport =
        reinterpret_cast<PFN_DC_DECLARE_REMOVAL_SUPPORT>(GetProcAddress(Dxgi, "DXGIDeclareAdapterRemovalSupport"));
    Check.CreateDevice = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(D3D11, "D3D11CreateDevice"));
    Check.CreateDeviceAndSwapChain =
        reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(GetProcAddress(D3D11, "D3D11CreateDeviceAndSwapChain"));
    if (Check.CreateFactory == nullptr || Check.CreateFactory1 == nullptr || Check.CreateFactory2 == nullptr ||
        Check.GetDebugInterface1 == nullptr || Check.DeclareRemovalSupport == nullptr || Check.CreateDevice == nullptr ||
        Check.CreateDeviceAndSwapChain == nullptr)
    {
        fprintf(stderr, "error: the runtime pair is missing exports\n");
        return 1;
    }

    for (const CheckInfo& Info : Checks)
    {
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Each : Wanted)
            IsWanted = IsWanted || Each == Info.Name;
        if (!IsWanted)
            continue;

        Check.CheckName = Info.Name;
        Check.Failures = 0;
        Info.Run(Check);
        Failures += Check.Failures;
    }

    printf("failures = %u\n", Failures);
    return Failures ? 1 : 0;
}
