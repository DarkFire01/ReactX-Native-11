/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compares the WinRT Direct3D 11 interop objects and window capture on one runtime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * capturecheck [-runtime system|reactx]
 */

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <inspectable.h>
#include <weakreference.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace WinCapture = winrt::Windows::Graphics::Capture;
namespace WinDirectX = winrt::Windows::Graphics::DirectX;

using Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;

typedef HRESULT (WINAPI* PFN_CREATE_FROM_DXGI)(IUnknown*, IInspectable**);

static PFN_D3D11_CREATE_DEVICE CcCreateDevice;
static PFN_CREATE_FROM_DXGI CcDeviceFromDxgi;
static PFN_CREATE_FROM_DXGI CcSurfaceFromDxgi;

/* The client color painted into the captured window, as BGRA */
static constexpr COLORREF CcPaintColor = RGB(0x33, 0x66, 0x99);

static
VOID
CcReport(
    _In_z_ PCSTR Key,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    va_list Arguments;

    printf("%s = ", Key);
    va_start(Arguments, Format);
    vprintf(Format, Arguments);
    va_end(Arguments);
    printf("\n");
}

static
std::string
CcGuidText(
    _In_ REFGUID Guid)
{
    WCHAR Wide[40];
    CHAR Narrow[40];

    StringFromGUID2(Guid, Wide, _countof(Wide));
    WideCharToMultiByte(CP_UTF8, 0, Wide, -1, Narrow, sizeof(Narrow), nullptr, nullptr);
    return Narrow;
}

/* Reports which interfaces an object answers, and whether a QI of IUnknown is stable */
static
VOID
CcReportInterfaces(
    _In_z_ PCSTR Prefix,
    _In_ IInspectable* Object)
{
    static const struct
    {
        PCSTR Name;
        IID Iid;
    } Asked[] =
    {
        { "IInspectable", __uuidof(IInspectable) },
        { "IClosable", __uuidof(ABI::Windows::Foundation::IClosable) },
        { "IDirect3DDevice", __uuidof(ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice) },
        { "IDirect3DSurface", __uuidof(ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface) },
        { "IDirect3DDxgiInterfaceAccess", __uuidof(IDirect3DDxgiInterfaceAccess) },
        { "IWeakReferenceSource", __uuidof(IWeakReferenceSource) },
        { "IAgileObject", __uuidof(IAgileObject) },
        { "IMarshal", __uuidof(IMarshal) },
        { "IDXGIDevice", __uuidof(IDXGIDevice) },
        { "IDXGISurface", __uuidof(IDXGISurface) },
        { "ID3D11Device", __uuidof(ID3D11Device) },
    };
    HSTRING Name = nullptr;
    TrustLevel Trust = FullTrust;
    ULONG Count = 0;
    IID* Iids = nullptr;
    IUnknown* Found;
    CHAR Key[160];
    HRESULT hr;

    for (const auto& Each : Asked)
    {
        Found = nullptr;
        hr = Object->QueryInterface(Each.Iid, reinterpret_cast<void**>(&Found));
        sprintf_s(Key, "%s.qi.%s", Prefix, Each.Name);
        CcReport(Key, "0x%08lX", hr);
        if (Found != nullptr)
            Found->Release();
    }

    hr = Object->GetRuntimeClassName(&Name);
    sprintf_s(Key, "%s.class", Prefix);
    CcReport(Key, "0x%08lX %ls", hr, Name != nullptr ? WindowsGetStringRawBuffer(Name, nullptr) : L"");
    if (Name != nullptr)
        WindowsDeleteString(Name);

    hr = Object->GetTrustLevel(&Trust);
    sprintf_s(Key, "%s.trust", Prefix);
    CcReport(Key, "0x%08lX %d", hr, Trust);

    hr = Object->GetIids(&Count, &Iids);
    sprintf_s(Key, "%s.iids", Prefix);
    CcReport(Key, "0x%08lX %lu", hr, Count);
    for (ULONG Index = 0; Index < Count; Index++)
    {
        sprintf_s(Key, "%s.iid%lu", Prefix, Index);
        CcReport(Key, "%s", CcGuidText(Iids[Index]).c_str());
    }
    CoTaskMemFree(Iids);
}

static
VOID
CcCheckDevice(
    _In_ ID3D11Device* Device)
{
    IDXGIDevice* Dxgi = nullptr;
    IInspectable* Graphics = nullptr;
    IInspectable* Again = (IInspectable*)(ULONG_PTR)1;
    IDirect3DDxgiInterfaceAccess* Access = nullptr;
    ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice* Face = nullptr;
    ABI::Windows::Foundation::IClosable* Closable = nullptr;
    IWeakReferenceSource* Source = nullptr;
    IWeakReference* Weak = nullptr;
    IInspectable* Resolved = nullptr;
    ID3D11Device* Inner = nullptr;
    HRESULT hr;

    hr = CcDeviceFromDxgi(nullptr, &Again);
    CcReport("device.null", "0x%08lX %s", hr, Again == (IInspectable*)(ULONG_PTR)1 ? "untouched" : "written");

    Device->QueryInterface(IID_PPV_ARGS(&Dxgi));
    hr = CcDeviceFromDxgi(Dxgi, &Graphics);
    CcReport("device.create", "0x%08lX", hr);
    if (FAILED(hr))
    {
        Dxgi->Release();
        return;
    }

    CcReportInterfaces("device", Graphics);

    Graphics->QueryInterface(IID_PPV_ARGS(&Access));
    hr = Access->GetInterface(IID_PPV_ARGS(&Inner));
    CcReport("device.access", "0x%08lX %s", hr, Inner == Device ? "same" : "different");
    if (Inner != nullptr)
        Inner->Release();

    Graphics->QueryInterface(IID_PPV_ARGS(&Face));
    CcReport("device.trim", "0x%08lX", Face->Trim());

    /* A weak reference resolves while the object lives */
    Graphics->QueryInterface(IID_PPV_ARGS(&Source));
    hr = Source->GetWeakReference(&Weak);
    CcReport("device.weak", "0x%08lX", hr);
    Source->Release();
    if (Weak != nullptr)
    {
        hr = Weak->Resolve(__uuidof(IInspectable), &Resolved);
        CcReport("device.weak.resolve", "0x%08lX %s", hr, Resolved != nullptr ? "object" : "null");
        if (Resolved != nullptr)
            Resolved->Release();
    }

    Graphics->QueryInterface(IID_PPV_ARGS(&Closable));
    CcReport("device.close", "0x%08lX", Closable->Close());
    CcReport("device.close.again", "0x%08lX", Closable->Close());
    CcReport("device.closed.trim", "0x%08lX", Face->Trim());
    Inner = nullptr;
    hr = Access->GetInterface(IID_PPV_ARGS(&Inner));
    CcReport("device.closed.access", "0x%08lX %s", hr, Inner != nullptr ? "object" : "null");
    if (Inner != nullptr)
        Inner->Release();

    Closable->Release();
    Face->Release();
    Access->Release();
    Graphics->Release();

    /* Every strong reference is gone now */
    if (Weak != nullptr)
    {
        Resolved = (IInspectable*)(ULONG_PTR)1;
        hr = Weak->Resolve(__uuidof(IInspectable), &Resolved);
        CcReport("device.weak.after", "0x%08lX %s", hr, Resolved == nullptr ? "null" : "object");
        Weak->Release();
    }

    Dxgi->Release();
}

static
VOID
CcCheckSurface(
    _In_ ID3D11Device* Device)
{
    D3D11_TEXTURE2D_DESC Desc = {};
    ID3D11Texture2D* Texture = nullptr;
    IDXGISurface* Surface = nullptr;
    IInspectable* Graphics = nullptr;
    ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface* Face = nullptr;
    ABI::Windows::Graphics::DirectX::Direct3D11::Direct3DSurfaceDescription Info = {};
    ABI::Windows::Foundation::IClosable* Closable = nullptr;
    IDirect3DDxgiInterfaceAccess* Access = nullptr;
    ID3D11Texture2D* Inner = nullptr;
    HRESULT hr;

    Desc.Width = 96;
    Desc.Height = 40;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    hr = Device->CreateTexture2D(&Desc, nullptr, &Texture);
    CcReport("surface.texture", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Texture->QueryInterface(IID_PPV_ARGS(&Surface));
    CcReport("surface.null", "0x%08lX", CcSurfaceFromDxgi(nullptr, &Graphics));
    hr = CcSurfaceFromDxgi(Surface, &Graphics);
    CcReport("surface.create", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        CcReportInterfaces("surface", Graphics);

        Graphics->QueryInterface(IID_PPV_ARGS(&Face));
        hr = Face->get_Description(&Info);
        CcReport("surface.desc", "0x%08lX %dx%d format %d samples %d/%d", hr, Info.Width, Info.Height, Info.Format,
                 Info.MultisampleDescription.Count, Info.MultisampleDescription.Quality);

        Graphics->QueryInterface(IID_PPV_ARGS(&Access));
        hr = Access->GetInterface(IID_PPV_ARGS(&Inner));
        CcReport("surface.access", "0x%08lX %s", hr, Inner == Texture ? "same" : "different");
        if (Inner != nullptr)
            Inner->Release();

        Graphics->QueryInterface(IID_PPV_ARGS(&Closable));
        CcReport("surface.close", "0x%08lX", Closable->Close());
        CcReport("surface.closed.desc", "0x%08lX", Face->get_Description(&Info));
        Inner = nullptr;
        CcReport("surface.closed.access", "0x%08lX", Access->GetInterface(IID_PPV_ARGS(&Inner)));

        Closable->Release();
        Access->Release();
        Face->Release();
        Graphics->Release();
    }

    Surface->Release();
    Texture->Release();
}

static
LRESULT
CALLBACK
CcWindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    PAINTSTRUCT Paint;
    HBRUSH Brush;
    HDC Dc;

    if (Message == WM_PAINT)
    {
        Dc = BeginPaint(Window, &Paint);
        Brush = CreateSolidBrush(CcPaintColor);
        FillRect(Dc, &Paint.rcPaint, Brush);
        DeleteObject(Brush);
        EndPaint(Window, &Paint);
        return 0;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

static
VOID
CcPump(VOID)
{
    MSG Message;

    while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
}

/* Captures one frame of a window this process owns and reads its center pixel back */
static
VOID
CcCheckCapture(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context)
{
    WNDCLASSW Class = {};
    IDXGIDevice* Dxgi = nullptr;
    IInspectable* Graphics = nullptr;
    HWND Window;
    HRESULT hr;

    Class.lpfnWndProc = CcWindowProc;
    Class.hInstance = GetModuleHandleW(nullptr);
    Class.lpszClassName = L"ReactXCaptureCheck";
    Class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&Class);

    Window = CreateWindowExW(0, Class.lpszClassName, L"capturecheck", WS_POPUP | WS_VISIBLE, 100, 100, 256, 128,
                             nullptr, nullptr, Class.hInstance, nullptr);
    UpdateWindow(Window);
    CcPump();

    CcReport("capture.supported", "%d", WinCapture::GraphicsCaptureSession::IsSupported() ? 1 : 0);

    Device->QueryInterface(IID_PPV_ARGS(&Dxgi));
    hr = CcDeviceFromDxgi(Dxgi, &Graphics);
    Dxgi->Release();
    if (FAILED(hr))
    {
        DestroyWindow(Window);
        return;
    }

    try
    {
        auto Interop = winrt::get_activation_factory<WinCapture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        WinCapture::GraphicsCaptureItem Item{ nullptr };
        WinDirectX::Direct3D11::IDirect3DDevice WinDevice{ nullptr };
        WinCapture::Direct3D11CaptureFrame Frame{ nullptr };
        ULONGLONG Deadline;

        winrt::check_hresult(Interop->CreateForWindow(Window, winrt::guid_of<WinCapture::GraphicsCaptureItem>(),
                                                      winrt::put_abi(Item)));
        CcReport("capture.item", "%dx%d", Item.Size().Width, Item.Size().Height);

        winrt::copy_from_abi(WinDevice, Graphics);
        auto Pool = WinCapture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            WinDevice, WinDirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, Item.Size());
        auto Session = Pool.CreateCaptureSession(Item);
        Session.StartCapture();
        CcReport("capture.started", "1");

        /* Frames arrive on the pool's own thread; poll briefly for the first one */
        Deadline = GetTickCount64() + 5000;
        while (!Frame && GetTickCount64() < Deadline)
        {
            CcPump();
            Frame = Pool.TryGetNextFrame();
            if (!Frame)
                Sleep(10);
        }

        CcReport("capture.frame", "%d", Frame ? 1 : 0);
        if (Frame)
        {
            auto Access = Frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
            winrt::com_ptr<ID3D11Texture2D> Texture;
            D3D11_TEXTURE2D_DESC Desc;
            D3D11_MAPPED_SUBRESOURCE Mapped;
            ID3D11Texture2D* Staging = nullptr;
            ID3D11Device* Owner = nullptr;

            CcReport("capture.content", "%dx%d", Frame.ContentSize().Width, Frame.ContentSize().Height);
            hr = Access->GetInterface(IID_PPV_ARGS(Texture.put()));
            CcReport("capture.texture", "0x%08lX", hr);
            if (SUCCEEDED(hr))
            {
                Texture->GetDevice(&Owner);
                CcReport("capture.texture.device", "%s", Owner == Device ? "ours" : "other");
                if (Owner != nullptr)
                    Owner->Release();

                Texture->GetDesc(&Desc);
                CcReport("capture.texture.desc", "%ux%u format %d bind 0x%X misc 0x%X usage %d", Desc.Width, Desc.Height,
                         Desc.Format, Desc.BindFlags, Desc.MiscFlags, Desc.Usage);

                Desc.BindFlags = 0;
                Desc.MiscFlags = 0;
                Desc.Usage = D3D11_USAGE_STAGING;
                Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                hr = Device->CreateTexture2D(&Desc, nullptr, &Staging);
                if (SUCCEEDED(hr))
                {
                    Context->CopyResource(Staging, Texture.get());
                    hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped);
                    CcReport("capture.map", "0x%08lX", hr);
                    if (SUCCEEDED(hr))
                    {
                        const BYTE* Row = static_cast<const BYTE*>(Mapped.pData) + (Desc.Height / 2) * Mapped.RowPitch;
                        CcReport("capture.center", "0x%08X", *reinterpret_cast<const UINT*>(Row + (Desc.Width / 2) * 4));
                        Context->Unmap(Staging, 0);
                    }
                    Staging->Release();
                }
            }
            Frame.Close();
        }

        Session.Close();
        Pool.Close();
    }
    catch (const winrt::hresult_error& Error)
    {
        CcReport("capture.error", "0x%08lX", Error.code().value);
    }

    Graphics->Release();
    DestroyWindow(Window);
}

/* Which runtime modules ended up in the process */
static
VOID
CcReportModule(
    _In_z_ PCSTR Key,
    _In_z_ PCWSTR Name)
{
    WCHAR Path[MAX_PATH] = L"";
    HMODULE Module = GetModuleHandleW(Name);

    if (Module != nullptr)
        GetModuleFileNameW(Module, Path, _countof(Path));
    fprintf(stderr, "%s: %ls\n", Key, Path);
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* Context = nullptr;
    PCWSTR Runtime = L"system";
    std::wstring Folder;
    WCHAR Module[MAX_PATH];
    HMODULE D3D11;
    HRESULT hr;

    setvbuf(stdout, nullptr, _IONBF, 0);
    for (int Index = 1; Index + 1 < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0)
            Runtime = argv[Index + 1];
    }

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(nullptr, Module, _countof(Module));
        *(wcsrchr(Module, L'\\') + 1) = L'\0';
        Folder = std::wstring(Module) + L"reactx\\";
        LoadLibraryW((Folder + L"dxgi.dll").c_str());
    }

    D3D11 = LoadLibraryW((Folder + L"d3d11.dll").c_str());
    if (D3D11 == nullptr)
    {
        fprintf(stderr, "error: cannot load the runtime\n");
        return 1;
    }

    CcCreateDevice = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(D3D11, "D3D11CreateDevice"));
    CcDeviceFromDxgi = reinterpret_cast<PFN_CREATE_FROM_DXGI>(GetProcAddress(D3D11, "CreateDirect3D11DeviceFromDXGIDevice"));
    CcSurfaceFromDxgi = reinterpret_cast<PFN_CREATE_FROM_DXGI>(GetProcAddress(D3D11, "CreateDirect3D11SurfaceFromDXGISurface"));
    CcReport("exports", "%d %d", CcDeviceFromDxgi != nullptr, CcSurfaceFromDxgi != nullptr);
    if (CcCreateDevice == nullptr || CcDeviceFromDxgi == nullptr || CcSurfaceFromDxgi == nullptr)
        return 1;

    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    /* Made as libobs-d3d11 makes its device */
    hr = CcCreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, Levels,
                        _countof(Levels), D3D11_SDK_VERSION, &Device, nullptr, &Context);
    CcReport("d3d11.create", "0x%08lX", hr);
    if (FAILED(hr))
        return 1;

    CcCheckDevice(Device);
    CcCheckSurface(Device);
    CcCheckCapture(Device, Context);

    Context->Release();
    Device->Release();

    CcReportModule("loaded d3d11", L"d3d11.dll");
    CcReportModule("loaded dxgi", L"dxgi.dll");
    return 0;
}
