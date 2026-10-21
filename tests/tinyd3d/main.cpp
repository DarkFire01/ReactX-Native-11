/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runs one small Direct3D 11 scene in a window and can save its last frame
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * tinyd3d <scene> [-runtime system|reactx|<path>] [-frames N, 0 runs until closed] [-capture out.bmp]
 * tinyd3d -list
 */

#include "scenes.hpp"

#include <dxgi1_2.h>
#include <dbghelp.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

constexpr UINT TdWidth = 640;
constexpr UINT TdHeight = 480;
static const WCHAR TdWindowClass[] = L"ReactXTinyD3D";

struct TdOptions
{
    const SceneInfo* Scene = nullptr;
    std::wstring Runtime = L"system";
    std::wstring Capture;
    UINT Frames = 60;
    BOOLEAN Blt = FALSE;
};

static
std::wstring
TdRuntimePath(
    _In_ const std::wstring& Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime.c_str(), L"system") == 0)
        return L"d3d11.dll";

    if (_wcsicmp(Runtime.c_str(), L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\d3d11.dll";
    }

    return Runtime;
}

static
BOOLEAN
TdParseOptions(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv,
    _Out_ TdOptions& Options)
{
    int Index;

    Options = {};
    for (Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
            Options.Runtime = argv[++Index];
        else if (_wcsicmp(argv[Index], L"-frames") == 0 && Index + 1 < argc)
            Options.Frames = (UINT)std::max(0, _wtoi(argv[++Index]));
        else if (_wcsicmp(argv[Index], L"-capture") == 0 && Index + 1 < argc)
            Options.Capture = argv[++Index];
        else if (_wcsicmp(argv[Index], L"-blt") == 0)
            Options.Blt = TRUE;
        else if (argv[Index][0] != L'-' && Options.Scene == nullptr)
            Options.Scene = TdFindScene(argv[Index]);
        else
            return FALSE;
    }

    return Options.Scene != nullptr;
}

static
LRESULT
CALLBACK
TdWindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    if (Message == WM_CLOSE)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

static
HWND
TdCreateWindow(
    _In_z_ PCWSTR Title)
{
    WNDCLASSEXW Class = {};
    RECT Frame = { 0, 0, (LONG)TdWidth, (LONG)TdHeight };

    Class.cbSize = sizeof(Class);
    Class.lpfnWndProc = TdWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    Class.lpszClassName = TdWindowClass;
    RegisterClassExW(&Class);

    AdjustWindowRect(&Frame, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    return CreateWindowExW(0, TdWindowClass, Title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, Frame.right - Frame.left, Frame.bottom - Frame.top, NULL,
                           NULL, Class.hInstance, nullptr);
}

/* The swapchain comes from whatever DXGI the device answers to, so each runtime uses its own */
static
HRESULT
TdCreateSwapChain(
    _In_ ID3D11Device* Device,
    _In_ HWND Window,
    _In_ BOOLEAN Blt,
    _Outptr_ IDXGISwapChain1** SwapChain)
{
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    ComPtr<IDXGIDevice> DxgiDevice;
    ComPtr<IDXGIAdapter> Adapter;
    ComPtr<IDXGIFactory2> Factory;

    if (FAILED(TdCheck(Device->QueryInterface(IID_PPV_ARGS(&DxgiDevice)), "QueryInterface(IDXGIDevice)")) ||
        FAILED(TdCheck(DxgiDevice->GetAdapter(&Adapter), "GetAdapter")) ||
        FAILED(TdCheck(Adapter->GetParent(IID_PPV_ARGS(&Factory)), "GetParent(IDXGIFactory2)")))
    {
        return E_FAIL;
    }

    Desc.Width = TdWidth;
    Desc.Height = TdHeight;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 2;
    Desc.SwapEffect = Blt ? DXGI_SWAP_EFFECT_DISCARD : DXGI_SWAP_EFFECT_FLIP_DISCARD;
    return TdCheck(Factory->CreateSwapChainForHwnd(Device, Window, &Desc, nullptr, nullptr, SwapChain),
                   "CreateSwapChainForHwnd");
}

/* Reads the back buffer through a staging copy and writes a top down 32 bit BMP */
static
HRESULT
TdCapture(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context,
    _In_ ID3D11Texture2D* BackBuffer,
    _In_z_ PCWSTR Path)
{
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    ComPtr<ID3D11Texture2D> Staging;
    BITMAPFILEHEADER File = {};
    BITMAPINFOHEADER Info = {};
    FILE* Output;
    UINT Row;
    HRESULT hr;

    BackBuffer->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    hr = TdCheck(Device->CreateTexture2D(&Desc, nullptr, &Staging), "CreateTexture2D(staging)");
    if (FAILED(hr))
        return hr;

    Context->CopyResource(Staging.Get(), BackBuffer);
    hr = TdCheck(Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped), "Map(staging)");
    if (FAILED(hr))
        return hr;

    Info.biSize = sizeof(Info);
    Info.biWidth = Desc.Width;
    Info.biHeight = -(LONG)Desc.Height;
    Info.biPlanes = 1;
    Info.biBitCount = 32;
    Info.biCompression = BI_RGB;
    File.bfType = 0x4D42;
    File.bfOffBits = sizeof(File) + sizeof(Info);
    File.bfSize = File.bfOffBits + Desc.Width * Desc.Height * 4;

    if (_wfopen_s(&Output, Path, L"wb") != 0)
    {
        Context->Unmap(Staging.Get(), 0);
        return HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE);
    }

    fwrite(&File, sizeof(File), 1, Output);
    fwrite(&Info, sizeof(Info), 1, Output);
    for (Row = 0; Row < Desc.Height; Row++)
        fwrite((const BYTE*)Mapped.pData + (SIZE_T)Row * Mapped.RowPitch, 4, Desc.Width, Output);
    fclose(Output);

    Context->Unmap(Staging.Get(), 0);
    return S_OK;
}

static
BOOLEAN
TdPumpMessages(VOID)
{
    MSG Message;

    while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
    {
        if (Message.message == WM_QUIT)
            return FALSE;
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }

    return TRUE;
}

/* A crash leaves tinyd3d-<pid>.dmp in the temp folder, for runs a debugger would hide */
static
LONG
WINAPI
TdWriteCrashDump(
    _In_ EXCEPTION_POINTERS* Exception)
{
    MINIDUMP_EXCEPTION_INFORMATION Information = {};
    WCHAR Path[MAX_PATH];
    WCHAR Folder[MAX_PATH];
    HANDLE File;

    GetTempPathW(_countof(Folder), Folder);
    swprintf_s(Path, L"%stinyd3d-%lu.dmp", Folder, GetCurrentProcessId());
    File = CreateFileW(Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File != INVALID_HANDLE_VALUE)
    {
        Information.ThreadId = GetCurrentThreadId();
        Information.ExceptionPointers = Exception;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), File,
                          (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo), &Information,
                          nullptr, nullptr);
        CloseHandle(File);
        fwprintf(stderr, L"crashed; dump written to %s\n", Path);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    static const D3D_FEATURE_LEVEL Levels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    PFN_D3D11_CREATE_DEVICE CreateDevice;
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<IDXGISwapChain1> SwapChain;
    ComPtr<ID3D11Texture2D> BackBuffer;
    ComPtr<ID3D11RenderTargetView> BackBufferView;
    std::unique_ptr<Scene> Current;
    D3D_FEATURE_LEVEL FeatureLevel;
    std::wstring RuntimePath;
    SceneTarget Target;
    TdOptions Options;
    HMODULE Runtime;
    HWND Window;
    UINT Frame;
    HRESULT hr;

    if (argc == 2 && _wcsicmp(argv[1], L"-list") == 0)
    {
        TdListScenes();
        return 0;
    }

    SetUnhandledExceptionFilter(TdWriteCrashDump);

    if (!TdParseOptions(argc, argv, Options))
    {
        fwprintf(stderr, L"usage: tinyd3d <scene> [-runtime system|reactx|<path>] [-frames N] [-capture out.bmp] [-blt]\n");
        fwprintf(stderr, L"       tinyd3d -list\n");
        return 2;
    }

    RuntimePath = TdRuntimePath(Options.Runtime);
    Runtime = LoadLibraryW(RuntimePath.c_str());
    CreateDevice = Runtime ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(Runtime, "D3D11CreateDevice"))
                           : nullptr;
    if (CreateDevice == nullptr)
    {
        fwprintf(stderr, L"error: cannot use %s (%lu)\n", RuntimePath.c_str(), GetLastError());
        return 1;
    }

    wprintf(L"scene %s on %s\n", Options.Scene->Name, RuntimePath.c_str());

    hr = CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, Levels, _countof(Levels), D3D11_SDK_VERSION,
                      &Device, &FeatureLevel, &Context);
    if (FAILED(TdCheck(hr, "D3D11CreateDevice")))
        return 1;

    Window = TdCreateWindow(Options.Scene->Name);
    if (Window == NULL || FAILED(TdCreateSwapChain(Device.Get(), Window, Options.Blt, &SwapChain)))
        return 1;

    if (FAILED(TdCheck(SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer)), "GetBuffer")) ||
        FAILED(TdCheck(Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, &BackBufferView),
                       "CreateRenderTargetView")))
    {
        return 1;
    }

    Target = { Device.Get(), Context.Get(), BackBufferView.Get(), BackBuffer.Get(), TdWidth, TdHeight };
    Current = Options.Scene->Create();
    if (Current == nullptr || FAILED(Current->Initialize(Target)))
        return 1;

    /* Zero frames means run until the window is closed */
    for (Frame = 0; (Options.Frames == 0 || Frame < Options.Frames) && TdPumpMessages(); Frame++)
    {
        Current->Render(Target, Frame);

        if (Frame + 1 == Options.Frames && !Options.Capture.empty() &&
            FAILED(TdCapture(Device.Get(), Context.Get(), BackBuffer.Get(), Options.Capture.c_str())))
        {
            return 1;
        }

        hr = SwapChain->Present(1, 0);
        if (FAILED(hr))
        {
            TdCheck(hr, "Present");
            return 1;
        }
    }

    wprintf(L"rendered %u frames\n", Frame);
    return 0;
}
