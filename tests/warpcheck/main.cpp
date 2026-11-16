/*
 * PROJECT:     ReactX Basic tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runs scenes on the system WARP and on ours and compares the results
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

/* What a scene produces: query results and pixels, compared byte for byte or within a tolerance */
struct TcResult
{
    std::vector<BYTE> Data;
    UINT Width = 0;
    UINT Height = 0;
    std::vector<BYTE> Pixels;
};

struct TcScene
{
    PCSTR Name;
    UINT Tolerance;
    HRESULT (*Run)(ID3D11Device *Device, ID3D11DeviceContext *Context, TcResult *Result);
};

static
VOID
TcAppend(
    _Inout_ TcResult *Result,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ SIZE_T Size)
{
    const BYTE *Bytes = static_cast<const BYTE *>(Data);

    Result->Data.insert(Result->Data.end(), Bytes, Bytes + Size);
}

static
HRESULT
TcReadBack(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _In_ ID3D11Texture2D *Texture,
    _Inout_ TcResult *Result)
{
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Map;
    ID3D11Texture2D *Staging = NULL;
    HRESULT hr;
    UINT Row;
    UINT Bytes;

    Texture->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    hr = Device->CreateTexture2D(&Desc, NULL, &Staging);
    if (FAILED(hr))
        return hr;

    Context->CopySubresourceRegion(Staging, 0, 0, 0, 0, Texture, 0, NULL);
    hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Map);
    if (SUCCEEDED(hr))
    {
        Bytes = Desc.Width * 4;
        Result->Width = Desc.Width;
        Result->Height = Desc.Height;
        Result->Pixels.resize((SIZE_T)Bytes * Desc.Height);
        for (Row = 0; Row < Desc.Height; Row++)
            memcpy(&Result->Pixels[(SIZE_T)Row * Bytes], static_cast<BYTE *>(Map.pData) + (SIZE_T)Row * Map.RowPitch, Bytes);
        Context->Unmap(Staging, 0);
    }
    Staging->Release();
    return hr;
}

/* Feature level, feature support blocks, format support and multisample levels */
static
HRESULT
SceneCaps(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const D3D11_FEATURE Features[] =
    {
        D3D11_FEATURE_THREADING, D3D11_FEATURE_DOUBLES, D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS,
        D3D11_FEATURE_D3D11_OPTIONS, D3D11_FEATURE_ARCHITECTURE_INFO, D3D11_FEATURE_D3D9_OPTIONS,
        D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, D3D11_FEATURE_D3D9_SHADOW_SUPPORT,
        D3D11_FEATURE_D3D11_OPTIONS1, D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT,
        D3D11_FEATURE_MARKER_SUPPORT, D3D11_FEATURE_D3D9_OPTIONS1, D3D11_FEATURE_D3D11_OPTIONS2,
        D3D11_FEATURE_D3D11_OPTIONS3, D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, D3D11_FEATURE_D3D11_OPTIONS4,
        D3D11_FEATURE_SHADER_CACHE, D3D11_FEATURE_D3D11_OPTIONS5,
    };
    BYTE Block[256];
    D3D_FEATURE_LEVEL Level = Device->GetFeatureLevel();
    UINT Support;
    UINT Quality;
    UINT Format;
    UINT Count;
    HRESULT hr;

    UNREFERENCED_PARAMETER(Context);
    TcAppend(Result, &Level, sizeof(Level));
    for (D3D11_FEATURE Feature : Features)
    {
        static const UINT Sizes[] = { 8, 4, 4, 56, 4, 4, 8, 4, 16, 4, 4, 12, 48, 4, 8, 4, 4, 4 };
        UINT Size = Sizes[&Feature - Features];

        ZeroMemory(Block, sizeof(Block));
        hr = Device->CheckFeatureSupport(Feature, Block, Size);
        TcAppend(Result, &hr, sizeof(hr));
        TcAppend(Result, Block, Size);
    }
    for (Format = 1; Format <= 132; Format++)
    {
        Support = 0;
        hr = Device->CheckFormatSupport(static_cast<DXGI_FORMAT>(Format), &Support);
        TcAppend(Result, &hr, sizeof(hr));
        TcAppend(Result, &Support, sizeof(Support));
        for (Count = 1; Count <= 32; Count *= 2)
        {
            Quality = 0;
            Device->CheckMultisampleQualityLevels(static_cast<DXGI_FORMAT>(Format), Count, &Quality);
            TcAppend(Result, &Quality, sizeof(Quality));
        }
    }
    return S_OK;
}

/* Clears of a few formats, read back through a staging copy */
static
HRESULT
SceneClear(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const DXGI_FORMAT Formats[] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB };
    const FLOAT Color[4] = { 1.0f, 0.5f, 0.25f, 0.75f };
    D3D11_TEXTURE2D_DESC Desc = { 64, 64, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    ID3D11Texture2D *Texture;
    ID3D11RenderTargetView *View;
    TcResult Image;
    D3D11_RECT Rect = { 8, 8, 40, 24 };
    HRESULT hr = S_OK;

    for (DXGI_FORMAT Format : Formats)
    {
        Desc.Format = Format;
        Texture = NULL;
        View = NULL;
        hr = Device->CreateTexture2D(&Desc, NULL, &Texture);
        if (SUCCEEDED(hr))
            hr = Device->CreateRenderTargetView(Texture, NULL, &View);
        if (SUCCEEDED(hr))
        {
            const FLOAT Second[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
            ID3D11DeviceContext1 *Context1 = NULL;

            Context->ClearRenderTargetView(View, Color);
            if (SUCCEEDED(Context->QueryInterface(IID_PPV_ARGS(&Context1))))
            {
                Context1->ClearView(View, Second, &Rect, 1);
                Context1->Release();
            }
            hr = TcReadBack(Device, Context, Texture, &Image);
            TcAppend(Result, Image.Pixels.data(), Image.Pixels.size());
            Result->Width = Image.Width;
            Result->Height = Image.Height;
            Result->Pixels = Image.Pixels;
        }
        if (View)
            View->Release();
        if (Texture)
            Texture->Release();
        if (FAILED(hr))
            break;
    }
    return hr;
}

/* Initial data, UpdateSubresource and region copies */
static
HRESULT
SceneCopy(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    D3D11_TEXTURE2D_DESC Desc = { 32, 32, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, 0, 0, 0 };
    std::vector<UINT> Source(32 * 32);
    D3D11_SUBRESOURCE_DATA Initial = { Source.data(), 32 * 4, 0 };
    ID3D11Texture2D *First = NULL;
    ID3D11Texture2D *Second = NULL;
    D3D11_BOX Box = { 4, 4, 0, 20, 12, 1 };
    UINT Patch[8 * 8];
    D3D11_BOX Target = { 10, 18, 0, 18, 26, 1 };
    HRESULT hr;
    UINT Index;

    for (Index = 0; Index < Source.size(); Index++)
        Source[Index] = 0xFF000000 | (Index * 2654435761u >> 8);
    for (Index = 0; Index < 64; Index++)
        Patch[Index] = 0xFF00FF00 | Index;

    hr = Device->CreateTexture2D(&Desc, &Initial, &First);
    if (SUCCEEDED(hr))
        hr = Device->CreateTexture2D(&Desc, NULL, &Second);
    if (SUCCEEDED(hr))
    {
        Context->CopyResource(Second, First);
        Context->CopySubresourceRegion(Second, 0, 12, 2, 0, First, 0, &Box);
        Context->UpdateSubresource(Second, 0, &Target, Patch, 8 * 4, 0);
        hr = TcReadBack(Device, Context, Second, Result);
        TcAppend(Result, Result->Pixels.data(), Result->Pixels.size());
    }
    if (Second)
        Second->Release();
    if (First)
        First->Release();
    return hr;
}

static const TcScene Scenes[] =
{
    { "caps", 0, SceneCaps },
    { "clear", 0, SceneClear },
    { "copy", 0, SceneCopy },
};

static
HRESULT
TcRun(
    _In_ HMODULE Module,
    _In_ const TcScene &Scene,
    _Out_ TcResult *Result)
{
    static const D3D_FEATURE_LEVEL Levels[] =
    {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_SOFTWARE, Module, 0, Levels, ARRAYSIZE(Levels),
                           D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(hr))
        return hr;

    hr = Scene.Run(Device, Context, Result);
    Context->Release();
    Device->Release();
    return hr;
}

static
VOID
TcWritePpm(
    _In_z_ PCSTR Path,
    _In_ const TcResult &Result)
{
    FILE *File;

    if (Result.Pixels.empty() || fopen_s(&File, Path, "wb") != 0)
        return;
    fprintf(File, "P6\n%u %u\n255\n", Result.Width, Result.Height);
    for (SIZE_T Index = 0; Index + 3 < Result.Pixels.size(); Index += 4)
        fwrite(&Result.Pixels[Index], 1, 3, File);
    fclose(File);
}

int
__cdecl
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t **argv)
{
    WCHAR SystemPath[MAX_PATH];
    HMODULE System;
    HMODULE Ours;
    UINT Failures = 0;

    if (argc < 2)
    {
        wprintf(L"usage: warpcheck <our d3d10warp.dll> [scene]\n");
        return 2;
    }

    GetSystemDirectoryW(SystemPath, ARRAYSIZE(SystemPath));
    wcscat_s(SystemPath, L"\\d3d10warp.dll");
    System = LoadLibraryExW(SystemPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    Ours = LoadLibraryExW(argv[1], NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (System == NULL || Ours == NULL || System == Ours)
    {
        wprintf(L"could not load both WARPs (%p, %p)\n", System, Ours);
        return 2;
    }

    for (const TcScene &Scene : Scenes)
    {
        TcResult Expected;
        TcResult Actual;
        HRESULT SystemHr;
        HRESULT OurHr;
        SIZE_T Differ = 0;
        UINT Worst = 0;
        char Name[96];

        if (argc > 2 && _stricmp(Scene.Name, std::string(argv[2], argv[2] + wcslen(argv[2])).c_str()) != 0)
            continue;

        SystemHr = TcRun(System, Scene, &Expected);
        OurHr = TcRun(Ours, Scene, &Actual);
        if (SystemHr != OurHr || Expected.Data.size() != Actual.Data.size())
        {
            printf("%-8s FAIL hr %08lX vs %08lX, %zu vs %zu bytes\n", Scene.Name, SystemHr, OurHr,
                   Expected.Data.size(), Actual.Data.size());
            Failures++;
            continue;
        }

        for (SIZE_T Index = 0; Index < Expected.Data.size(); Index++)
        {
            UINT Delta = (UINT)abs((int)Expected.Data[Index] - (int)Actual.Data[Index]);

            if (Delta > Scene.Tolerance)
            {
                if (Differ < 8)
                    printf("  %s byte %zu: %u vs %u\n", Scene.Name, Index, Expected.Data[Index], Actual.Data[Index]);
                Differ++;
            }
            if (Delta > Worst)
                Worst = Delta;
        }

        printf("%-8s %s (%zu bytes, %zu differ, worst %u)\n", Scene.Name, Differ ? "FAIL" : "ok",
               Expected.Data.size(), Differ, Worst);
        if (Differ)
            Failures++;

        sprintf_s(Name, "%s_system.ppm", Scene.Name);
        TcWritePpm(Name, Expected);
        sprintf_s(Name, "%s_ours.ppm", Scene.Name);
        TcWritePpm(Name, Actual);
    }
    return Failures ? 1 : 0;
}
