/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Probe D3D11 device and video support on every adapter
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>

#include <stdio.h>
#include <wchar.h>

/*
 * rxprobe [-system] [-v]
 *   -system  load C:\Windows\System32\d3d11.dll instead of reactx\d3d11.dll next to rxprobe
 *   -v       set REACTX_LOG so the ReactX runtime explains itself
 */

static constexpr D3D_FEATURE_LEVEL AllFeatureLevels[] =
{
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1,
};

static
PCWSTR
RxFeatureLevelName(
    _In_ D3D_FEATURE_LEVEL FeatureLevel)
{
    switch (FeatureLevel)
    {
        case D3D_FEATURE_LEVEL_12_1: return L"12_1";
        case D3D_FEATURE_LEVEL_12_0: return L"12_0";
        case D3D_FEATURE_LEVEL_11_1: return L"11_1";
        case D3D_FEATURE_LEVEL_11_0: return L"11_0";
        case D3D_FEATURE_LEVEL_10_1: return L"10_1";
        case D3D_FEATURE_LEVEL_10_0: return L"10_0";
        case D3D_FEATURE_LEVEL_9_3: return L"9_3";
        case D3D_FEATURE_LEVEL_9_2: return L"9_2";
        case D3D_FEATURE_LEVEL_9_1: return L"9_1";
        default: return L"none";
    }
}

static
VOID
RxPrintVideoProfiles(
    _In_ ID3D11Device* Device)
{
    ID3D11VideoDevice* Video = nullptr;
    D3D11_VIDEO_DECODER_CONFIG Config;
    D3D11_VIDEO_DECODER_DESC Desc;
    BOOL Supported = FALSE;
    UINT ConfigCount;
    GUID Profile;
    WCHAR Text[64];
    UINT Count;
    UINT Index;
    HRESULT hr;

    hr = Device->QueryInterface(IID_PPV_ARGS(&Video));
    if (FAILED(hr))
    {
        wprintf(L"      ID3D11VideoDevice: 0x%08lX\n", hr);
        return;
    }

    Count = Video->GetVideoDecoderProfileCount();
    wprintf(L"      decoder profiles: %u\n", Count);
    for (Index = 0; Index < Count; Index++)
    {
        if (FAILED(Video->GetVideoDecoderProfile(Index, &Profile)))
            continue;

        StringFromGUID2(Profile, Text, _countof(Text));
        wprintf(L"        %s\n", Text);
    }

    hr = Video->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_H264_VLD_NOFGT, DXGI_FORMAT_NV12, &Supported);
    wprintf(L"      H.264 NV12 output: %s (0x%08lX)\n", Supported ? L"yes" : L"no", hr);

    Desc.Guid = D3D11_DECODER_PROFILE_H264_VLD_NOFGT;
    Desc.SampleWidth = 1920;
    Desc.SampleHeight = 1080;
    Desc.OutputFormat = DXGI_FORMAT_NV12;
    ConfigCount = 0;
    hr = Video->GetVideoDecoderConfigCount(&Desc, &ConfigCount);
    wprintf(L"      H.264 1080p configs: %u (0x%08lX)\n", ConfigCount, hr);
    for (Index = 0; Index < ConfigCount; Index++)
    {
        if (FAILED(Video->GetVideoDecoderConfig(&Desc, Index, &Config)))
            continue;

        wprintf(L"        config %u: bitstream raw %u, decoder specific 0x%X, min buffers %u\n",
                Index, Config.ConfigBitstreamRaw, Config.ConfigDecoderSpecific,
                Config.ConfigMinRenderTargetBuffCount);
    }

    Video->Release();
}

static
VOID
RxProbeAdapter(
    _In_ PFN_D3D11_CREATE_DEVICE CreateDevice,
    _In_ IDXGIAdapter1* Adapter)
{
    static constexpr UINT FlagSets[] = { 0, D3D11_CREATE_DEVICE_VIDEO_SUPPORT };
    ID3D11Device* Device;
    ID3D11DeviceContext* Context;
    D3D_FEATURE_LEVEL FeatureLevel;
    UINT QualityLevels;
    HRESULT hr;

    for (UINT Flags : FlagSets)
    {
        wprintf(L"    flags 0x%04X\n", Flags);

        FeatureLevel = (D3D_FEATURE_LEVEL)0;
        hr = CreateDevice(Adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, Flags,
                          AllFeatureLevels, _countof(AllFeatureLevels), D3D11_SDK_VERSION,
                          nullptr, &FeatureLevel, nullptr);
        wprintf(L"      support check: 0x%08lX, level %s\n", hr, RxFeatureLevelName(FeatureLevel));

        Device = nullptr;
        Context = nullptr;
        hr = CreateDevice(Adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, Flags,
                          AllFeatureLevels, _countof(AllFeatureLevels), D3D11_SDK_VERSION,
                          &Device, &FeatureLevel, &Context);
        wprintf(L"      create device: 0x%08lX\n", hr);
        if (FAILED(hr))
            continue;

        hr = Device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, 4, &QualityLevels);
        wprintf(L"      device level %s, 4x MSAA BGRA quality levels %u (0x%08lX)\n",
                RxFeatureLevelName(Device->GetFeatureLevel()), QualityLevels, hr);
        Context->Flush();

        if (Flags & D3D11_CREATE_DEVICE_VIDEO_SUPPORT)
            RxPrintVideoProfiles(Device);

        Context->Release();
        Device->Release();
    }
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    PFN_D3D11_CREATE_DEVICE CreateDevice;
    IDXGIFactory1* Factory = nullptr;
    IDXGIAdapter1* Adapter;
    DXGI_ADAPTER_DESC1 Desc;
    WCHAR Path[MAX_PATH];
    PWSTR Slash;
    HMODULE Runtime;
    BOOLEAN UseSystem = FALSE;
    UINT Index;
    int Argument;

    for (Argument = 1; Argument < argc; Argument++)
    {
        if (_wcsicmp(argv[Argument], L"-system") == 0)
        {
            UseSystem = TRUE;
        }
        else if (_wcsicmp(argv[Argument], L"-v") == 0)
        {
            SetEnvironmentVariableW(L"REACTX_LOG", L"1");
        }
        else
        {
            fwprintf(stderr, L"usage: rxprobe [-system] [-v]\n");
            return 2;
        }
    }

    if (UseSystem)
    {
        GetSystemDirectoryW(Path, _countof(Path));
        wcscat_s(Path, L"\\d3d11.dll");
    }
    else
    {
        GetModuleFileNameW(NULL, Path, _countof(Path));
        Slash = wcsrchr(Path, L'\\');
        if (Slash != nullptr)
            Slash[1] = UNICODE_NULL;
        wcscat_s(Path, L"reactx\\d3d11.dll");
    }

    Runtime = LoadLibraryW(Path);
    if (Runtime == NULL)
    {
        fwprintf(stderr, L"cannot load %s (%lu)\n", Path, GetLastError());
        return 1;
    }

    CreateDevice = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(
        GetProcAddress(Runtime, "D3D11CreateDevice"));
    if (CreateDevice == nullptr)
    {
        fwprintf(stderr, L"%s has no D3D11CreateDevice\n", Path);
        return 1;
    }

    wprintf(L"runtime: %s\n", Path);

    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&Factory))))
    {
        fwprintf(stderr, L"CreateDXGIFactory1 failed\n");
        return 1;
    }

    for (Index = 0; Factory->EnumAdapters1(Index, &Adapter) != DXGI_ERROR_NOT_FOUND; Index++)
    {
        Adapter->GetDesc1(&Desc);
        wprintf(L"adapter %u: %s (LUID %08lX:%08lX)%s\n",
                Index,
                Desc.Description,
                Desc.AdapterLuid.HighPart,
                Desc.AdapterLuid.LowPart,
                (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? L" [software]" : L"");

        if ((Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
            RxProbeAdapter(CreateDevice, Adapter);

        Adapter->Release();
    }

    Factory->Release();
    return 0;
}
