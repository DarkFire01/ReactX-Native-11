/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 10 runtime behavior, printed as comparable facts
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * d3d10check [-runtime system|reactx|<dir>] [check ...]
 */

#include <windows.h>
#include <d3d10_1.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

typedef HRESULT (WINAPI* PFN_TC_CREATE_DEVICE)(IDXGIAdapter*, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT, ID3D10Device**);
typedef HRESULT (WINAPI* PFN_TC_CREATE_DEVICE1)(IDXGIAdapter*, D3D10_DRIVER_TYPE, HMODULE, UINT, D3D10_FEATURE_LEVEL1,
                                               UINT, ID3D10Device1**);
typedef HRESULT (WINAPI* PFN_TC_CREATE_BLOB)(SIZE_T, ID3D10Blob**);
typedef LPCSTR (WINAPI* PFN_TC_GET_PROFILE)(ID3D10Device*);
typedef UINT64 (WINAPI* PFN_TC_GET_VERSION)(VOID);
typedef HRESULT (WINAPI* PFN_TC_MASK_ALL)(D3D10_STATE_BLOCK_MASK*);
typedef HRESULT (WINAPI* PFN_TC_MASK_CAPTURE)(D3D10_STATE_BLOCK_MASK*, D3D10_DEVICE_STATE_TYPES, UINT, UINT);
typedef BOOL (WINAPI* PFN_TC_MASK_GET)(D3D10_STATE_BLOCK_MASK*, D3D10_DEVICE_STATE_TYPES, UINT);
typedef HRESULT (WINAPI* PFN_TC_CORE_CREATE)(IDXGIFactory*, IDXGIAdapter*, UINT, UINT, void**);

struct TenCheck
{
    HMODULE D3d10 = NULL;
    HMODULE D3d10_1 = NULL;
    HMODULE Core = NULL;
    HMODULE Core1 = NULL;
    PFN_TC_CREATE_DEVICE CreateDevice = nullptr;
    PFN_TC_CREATE_DEVICE1 CreateDevice1 = nullptr;
    PCSTR CheckName = "";

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

    HRESULT
    MakeDevice(
        _In_ UINT Flags,
        _Outptr_ ID3D10Device1** Device)
    {
        return CreateDevice1(nullptr, D3D10_DRIVER_TYPE_HARDWARE, NULL, Flags, D3D10_FEATURE_LEVEL_10_1,
                             D3D10_1_SDK_VERSION, Device);
    }
};

/* create: argument checks of both creation entries, and what a device reports about itself */
static
VOID
TcCheckCreate(
    _In_ TenCheck& Check)
{
    ComPtr<ID3D10Device1> Device1;
    ComPtr<ID3D10Device> Device;
    HRESULT hr;

    Check.Report("bad_sdk", "0x%08lX", Check.CreateDevice1(nullptr, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
                                                            D3D10_FEATURE_LEVEL_10_1, 29, &Device1));
    Check.Report("bad_level", "0x%08lX", Check.CreateDevice1(nullptr, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
                                                              (D3D10_FEATURE_LEVEL1)0xB000, D3D10_1_SDK_VERSION,
                                                              &Device1));
    Check.Report("bad_type", "0x%08lX", Check.CreateDevice1(nullptr, (D3D10_DRIVER_TYPE)7, NULL, 0,
                                                             D3D10_FEATURE_LEVEL_10_1, D3D10_1_SDK_VERSION, &Device1));
    Check.Report("null_out", "0x%08lX", Check.CreateDevice1(nullptr, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
                                                             D3D10_FEATURE_LEVEL_10_1, D3D10_1_SDK_VERSION, nullptr));

    hr = Check.MakeDevice(0, &Device1);
    Check.Report("device1", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Check.Report("device1_level", "0x%X", Device1->GetFeatureLevel());
        Check.Report("device1_flags", "0x%X", Device1->GetCreationFlags());
    }

    hr = Check.MakeDevice(D3D10_CREATE_DEVICE_SINGLETHREADED | D3D10_CREATE_DEVICE_BGRA_SUPPORT, &Device1);
    Check.Report("device1_single", "0x%08lX %X", hr, SUCCEEDED(hr) ? Device1->GetCreationFlags() : 0);

    hr = Check.CreateDevice1(nullptr, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_FEATURE_LEVEL_10_0,
                             D3D10_1_SDK_VERSION, &Device1);
    Check.Report("device1_10_0", "0x%08lX %X", hr, SUCCEEDED(hr) ? Device1->GetFeatureLevel() : 0);

    Check.Report("d3d10_bad_sdk", "0x%08lX", Check.CreateDevice(nullptr, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, 30,
                                                                 &Device));
    hr = Check.CreateDevice(nullptr, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_SDK_VERSION, &Device);
    Check.Report("d3d10_device", "0x%08lX %X", hr, SUCCEEDED(hr) ? Device->GetCreationFlags() : 0);
}

/* interfaces: what the device and its first texture answer, and multithread protection */
static
VOID
TcCheckInterfaces(
    _In_ TenCheck& Check)
{
    static const struct
    {
        PCSTR Name;
        GUID Id;
    } Probes[] =
    {
        { "d3d10device", __uuidof(ID3D10Device) },
        { "d3d10device1", __uuidof(ID3D10Device1) },
        { "dxgidevice", __uuidof(IDXGIDevice) },
        { "d3d11device", __uuidof(ID3D11Device) },
        { "multithread", __uuidof(ID3D10Multithread) },
    };
    ComPtr<ID3D10Device1> Device;
    ComPtr<ID3D10Device1> Single;
    ComPtr<ID3D10Multithread> Multithread;
    ComPtr<IUnknown> Found;
    char Key[64];

    if (FAILED(Check.MakeDevice(0, &Device)) ||
        FAILED(Check.MakeDevice(D3D10_CREATE_DEVICE_SINGLETHREADED, &Single)))
    {
        return;
    }

    for (const auto& Probe : Probes)
    {
        sprintf_s(Key, "qi_%s", Probe.Name);
        Check.Report(Key, "0x%08lX", Device->QueryInterface(Probe.Id, &Found));
        Found.Reset();
    }

    if (SUCCEEDED(Device.As(&Multithread)))
        Check.Report("protected", "%d", Multithread->GetMultithreadProtected());
    if (SUCCEEDED(Single.As(&Multithread)))
        Check.Report("protected_single", "%d", Multithread->GetMultithreadProtected());
}

/* textures: create, map, copy and read back, with descs both ways */
static
VOID
TcCheckTextures(
    _In_ TenCheck& Check)
{
    ComPtr<ID3D10Device1> Device;
    ComPtr<ID3D10Device> Owner;
    ComPtr<ID3D10Texture2D> Staging;
    ComPtr<ID3D10Texture2D> Source;
    ComPtr<ID3D10Texture2D> Gdi;
    ComPtr<ID3D11Texture2D> As11;
    D3D10_TEXTURE2D_DESC Desc = {};
    D3D10_TEXTURE2D_DESC Back;
    D3D11_TEXTURE2D_DESC Desc11;
    D3D10_SUBRESOURCE_DATA Initial;
    D3D10_MAPPED_TEXTURE2D Mapped;
    UINT Pixels[16 * 16];
    UINT Sum = 0;
    UINT Index;
    UINT Row;
    HRESULT hr;

    if (FAILED(Check.MakeDevice(D3D10_CREATE_DEVICE_BGRA_SUPPORT, &Device)))
        return;

    for (Index = 0; Index < _countof(Pixels); Index++)
        Pixels[Index] = Index * 0x01020305u;

    Desc.Width = 16;
    Desc.Height = 16;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D10_USAGE_DEFAULT;
    Desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    Initial = { Pixels, 16 * 4, 0 };
    Check.Report("create_default", "0x%08lX", Device->CreateTexture2D(&Desc, &Initial, &Source));

    Desc.Usage = D3D10_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ | D3D10_CPU_ACCESS_WRITE;
    Check.Report("create_staging", "0x%08lX", Device->CreateTexture2D(&Desc, nullptr, &Staging));
    if (Source == nullptr || Staging == nullptr)
        return;

    Device->CopyResource(Staging.Get(), Source.Get());
    hr = Staging->Map(0, D3D10_MAP_READ, 0, &Mapped);
    if (SUCCEEDED(hr))
    {
        for (Row = 0; Row < 16; Row++)
        {
            for (Index = 0; Index < 16; Index++)
                Sum = Sum * 31 + static_cast<const UINT*>(Mapped.pData)[Row * (Mapped.RowPitch / 4) + Index];
        }
        Staging->Unmap(0);
    }
    Check.Report("readback", "0x%08lX %08X", hr, Sum);

    Staging->GetDesc(&Back);
    Check.Report("desc", "%u %u %u %u %u %X %X", Back.Width, Back.Height, Back.Format, Back.Usage, Back.BindFlags,
                 Back.CPUAccessFlags, Back.MiscFlags);

    Staging->GetDevice(&Owner);
    Check.Report("same_device", "%d", Owner.Get() != nullptr && Owner.Get() == static_cast<ID3D10Device*>(Device.Get()));
    Check.Report("qi_texture11", "0x%08lX", Staging.As(&As11));

    /* Misc flags move between the two encodings */
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.Usage = D3D10_USAGE_DEFAULT;
    Desc.CPUAccessFlags = 0;
    Desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;
    Desc.MiscFlags = D3D10_RESOURCE_MISC_GDI_COMPATIBLE;
    hr = Device->CreateTexture2D(&Desc, nullptr, &Gdi);
    Check.Report("create_gdi", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Gdi->GetDesc(&Back);
        Gdi.As(&As11);
        As11->GetDesc(&Desc11);
        Check.Report("gdi_misc", "%X %X", Back.MiscFlags, Desc11.MiscFlags);
    }
}

/* clear: a render target cleared through the device and read back */
static
VOID
TcCheckClear(
    _In_ TenCheck& Check)
{
    static const FLOAT Color[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    ComPtr<ID3D10Device1> Device;
    ComPtr<ID3D10Texture2D> Target;
    ComPtr<ID3D10Texture2D> Staging;
    ComPtr<ID3D10RenderTargetView> View;
    D3D10_TEXTURE2D_DESC Desc = {};
    D3D10_RENDER_TARGET_VIEW_DESC ViewDesc;
    D3D10_MAPPED_TEXTURE2D Mapped;
    D3D10_VIEWPORT Viewport = { 1, 2, 3, 4, 0.0f, 1.0f };
    D3D10_VIEWPORT Back = {};
    UINT Count = 1;
    HRESULT hr;

    if (FAILED(Check.MakeDevice(0, &Device)))
        return;

    Desc.Width = 8;
    Desc.Height = 8;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D10_USAGE_DEFAULT;
    Desc.BindFlags = D3D10_BIND_RENDER_TARGET;
    if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &Target)))
        return;

    Desc.Usage = D3D10_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &Staging)))
        return;

    hr = Device->CreateRenderTargetView(Target.Get(), nullptr, &View);
    Check.Report("rtv", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    View->GetDesc(&ViewDesc);
    Check.Report("rtv_desc", "%u %u %u", ViewDesc.Format, ViewDesc.ViewDimension, ViewDesc.Texture2D.MipSlice);

    Device->ClearRenderTargetView(View.Get(), Color);
    Device->CopyResource(Staging.Get(), Target.Get());
    hr = Staging->Map(0, D3D10_MAP_READ, 0, &Mapped);
    Check.Report("pixel", "0x%08lX %08X", hr, SUCCEEDED(hr) ? *static_cast<const UINT*>(Mapped.pData) : 0);
    if (SUCCEEDED(hr))
        Staging->Unmap(0);

    Device->RSSetViewports(1, &Viewport);
    Device->RSGetViewports(&Count, &Back);
    Check.Report("viewport", "%u %d %d %u %u", Count, Back.TopLeftX, Back.TopLeftY, Back.Width, Back.Height);
}

/* objects: buffers, states and queries through their Direct3D 10 faces */
static
VOID
TcCheckObjects(
    _In_ TenCheck& Check)
{
    ComPtr<ID3D10Device1> Device;
    ComPtr<ID3D10Buffer> Buffer;
    ComPtr<ID3D10BlendState> Blend;
    ComPtr<ID3D10Query> Query;
    D3D10_BUFFER_DESC BufferDesc = { 256, D3D10_USAGE_DYNAMIC, D3D10_BIND_VERTEX_BUFFER, D3D10_CPU_ACCESS_WRITE, 0 };
    D3D10_BUFFER_DESC BufferBack;
    D3D10_BLEND_DESC BlendDesc = {};
    D3D10_BLEND_DESC BlendBack;
    D3D10_QUERY_DESC QueryDesc = { D3D10_QUERY_EVENT, 0 };
    void* Data = nullptr;
    BOOL Done = FALSE;
    UINT Spins = 0;
    HRESULT hr;

    if (FAILED(Check.MakeDevice(0, &Device)))
        return;

    hr = Device->CreateBuffer(&BufferDesc, nullptr, &Buffer);
    Check.Report("buffer", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        hr = Buffer->Map(D3D10_MAP_WRITE_DISCARD, 0, &Data);
        Check.Report("buffer_map", "0x%08lX %s", hr, Data ? "data" : "null");
        if (SUCCEEDED(hr))
            Buffer->Unmap();
        Buffer->GetDesc(&BufferBack);
        Check.Report("buffer_desc", "%u %u %X %X %X", BufferBack.ByteWidth, BufferBack.Usage, BufferBack.BindFlags,
                     BufferBack.CPUAccessFlags, BufferBack.MiscFlags);
    }

    BlendDesc.BlendEnable[1] = TRUE;
    BlendDesc.SrcBlend = D3D10_BLEND_SRC_ALPHA;
    BlendDesc.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
    BlendDesc.BlendOp = D3D10_BLEND_OP_ADD;
    BlendDesc.SrcBlendAlpha = D3D10_BLEND_ONE;
    BlendDesc.DestBlendAlpha = D3D10_BLEND_ZERO;
    BlendDesc.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    BlendDesc.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
    BlendDesc.RenderTargetWriteMask[1] = D3D10_COLOR_WRITE_ENABLE_RED;
    hr = Device->CreateBlendState(&BlendDesc, &Blend);
    Check.Report("blend", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Blend->GetDesc(&BlendBack);
        Check.Report("blend_desc", "%d %d %d %u %u %X %X", BlendBack.BlendEnable[0], BlendBack.BlendEnable[1],
                     BlendBack.BlendEnable[2], BlendBack.SrcBlend, BlendBack.DestBlend,
                     BlendBack.RenderTargetWriteMask[0], BlendBack.RenderTargetWriteMask[1]);
    }

    hr = Device->CreateQuery(&QueryDesc, &Query);
    Check.Report("query", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Query->End();
        while ((hr = Query->GetData(&Done, sizeof(Done), 0)) == S_FALSE && Spins < 1000000)
            Spins++;
        Check.Report("query_done", "0x%08lX %d %u", hr, Done, Query->GetDataSize());
    }
}

/* helpers: blobs, profiles, versions and state block masks */
static
VOID
TcCheckHelpers(
    _In_ TenCheck& Check)
{
    auto CreateBlob = reinterpret_cast<PFN_TC_CREATE_BLOB>(GetProcAddress(Check.D3d10, "D3D10CreateBlob"));
    auto Profile = reinterpret_cast<PFN_TC_GET_PROFILE>(GetProcAddress(Check.D3d10, "D3D10GetVertexShaderProfile"));
    auto Profile1 = reinterpret_cast<PFN_TC_GET_PROFILE>(GetProcAddress(Check.D3d10_1, "D3D10GetPixelShaderProfile"));
    auto Version = reinterpret_cast<PFN_TC_GET_VERSION>(GetProcAddress(Check.D3d10, "D3D10GetVersion"));
    auto CoreVersion = reinterpret_cast<PFN_TC_GET_VERSION>(GetProcAddress(Check.Core, "D3D10CoreGetVersion"));
    auto CoreCreate = reinterpret_cast<PFN_TC_CORE_CREATE>(GetProcAddress(Check.Core, "D3D10CoreCreateDevice"));
    auto EnableAll = reinterpret_cast<PFN_TC_MASK_ALL>(GetProcAddress(Check.D3d10, "D3D10StateBlockMaskEnableAll"));
    auto Enable = reinterpret_cast<PFN_TC_MASK_CAPTURE>(GetProcAddress(Check.D3d10, "D3D10StateBlockMaskEnableCapture"));
    auto Disable = reinterpret_cast<PFN_TC_MASK_ALL>(GetProcAddress(Check.D3d10_1, "D3D10StateBlockMaskDisableAll"));
    auto Get = reinterpret_cast<PFN_TC_MASK_GET>(GetProcAddress(Check.D3d10, "D3D10StateBlockMaskGetSetting"));
    ComPtr<ID3D10Blob> Blob;
    D3D10_STATE_BLOCK_MASK Mask;
    const BYTE* Bytes = reinterpret_cast<const BYTE*>(&Mask);
    UINT Sum = 0;
    SIZE_T Index;
    HRESULT hr;

    hr = CreateBlob(24, &Blob);
    Check.Report("blob", "0x%08lX %Iu", hr, SUCCEEDED(hr) ? Blob->GetBufferSize() : 0);
    Check.Report("blob_null", "0x%08lX", CreateBlob(24, nullptr));
    Check.Report("profiles", "%s %s", Profile(nullptr) ? Profile(nullptr) : "null", Profile1(nullptr) ? Profile1(nullptr) : "null");
    Check.Report("version", "%I64X %I64X", Version(), CoreVersion());
    Check.Report("core_create", "0x%08lX", CoreCreate(nullptr, nullptr, 0, 0, nullptr));

    EnableAll(&Mask);
    for (Index = 0; Index < sizeof(Mask); Index++)
        Sum = Sum * 31 + Bytes[Index];
    Check.Report("mask_all", "%08X", Sum);
    {
        char Text[sizeof(Mask) * 2 + 1];

        for (Index = 0; Index < sizeof(Mask); Index++)
            sprintf_s(Text + Index * 2, 3, "%02X", Bytes[Index]);
        Check.Report("mask_bytes", "%s", Text);
    }

    Disable(&Mask);
    Check.Report("mask_enable", "0x%08lX 0x%08lX 0x%08lX", Enable(&Mask, D3D10_DST_PS_SHADER_RESOURCES, 3, 2),
                 Enable(&Mask, D3D10_DST_PS_SHADER_RESOURCES, 127, 2), Enable(&Mask, D3D10_DST_VS, 0, 1));
    Check.Report("mask_get", "%d %d %d %d", Get(&Mask, D3D10_DST_PS_SHADER_RESOURCES, 3),
                 Get(&Mask, D3D10_DST_PS_SHADER_RESOURCES, 5), Get(&Mask, D3D10_DST_VS, 0),
                 Get(&Mask, D3D10_DST_PS_SHADER_RESOURCES, 200));
}

struct CheckInfo
{
    PCSTR Name;
    VOID (*Run)(TenCheck& Check);
};

static const CheckInfo Checks[] =
{
    { "create", TcCheckCreate },
    { "interfaces", TcCheckInterfaces },
    { "textures", TcCheckTextures },
    { "clear", TcCheckClear },
    { "objects", TcCheckObjects },
    { "helpers", TcCheckHelpers },
};

/* System32 for "system", the ReactX build next to this exe for "reactx", else a directory */
static
std::wstring
TcRuntimeDirectory(
    _In_z_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
    {
        GetSystemDirectoryW(Module, _countof(Module));
        return std::wstring(Module) + L"\\";
    }

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
    static const PCWSTR Order[] = { L"dxgi.dll", L"d3d11.dll", L"d3d10core.dll", L"d3d10_1core.dll", L"d3d10.dll",
                                    L"d3d10_1.dll" };
    std::vector<std::string> Wanted;
    PCWSTR Runtime = L"system";
    std::wstring Directory;
    TenCheck Check;
    HMODULE Loaded[_countof(Order)];
    int Index;
    char Name[128];

    setvbuf(stdout, nullptr, _IONBF, 0);

    for (Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Runtime = argv[++Index];
            continue;
        }

        WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
        Wanted.push_back(Name);
    }

    /* The whole stack from one place, lowest first, so each finds the others already loaded */
    Directory = TcRuntimeDirectory(Runtime);
    for (Index = 0; Index < (int)_countof(Order); Index++)
    {
        Loaded[Index] = LoadLibraryExW((Directory + Order[Index]).c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (Loaded[Index] == NULL)
        {
            fwprintf(stderr, L"error: cannot load %s\n", Order[Index]);
            return 1;
        }
    }

    Check.Core = Loaded[2];
    Check.Core1 = Loaded[3];
    Check.D3d10 = Loaded[4];
    Check.D3d10_1 = Loaded[5];
    Check.CreateDevice = reinterpret_cast<PFN_TC_CREATE_DEVICE>(GetProcAddress(Check.D3d10, "D3D10CreateDevice"));
    Check.CreateDevice1 = reinterpret_cast<PFN_TC_CREATE_DEVICE1>(GetProcAddress(Check.D3d10_1, "D3D10CreateDevice1"));
    if (Check.CreateDevice == nullptr || Check.CreateDevice1 == nullptr)
    {
        fprintf(stderr, "error: missing creation exports\n");
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
        Info.Run(Check);
    }

    printf("failures = 0\n");
    return 0;
}
