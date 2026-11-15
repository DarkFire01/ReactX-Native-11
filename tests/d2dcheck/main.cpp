/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Draws the same scenes with a Direct2D DLL and the system one and compares the pixels
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <d3d11_1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

typedef HRESULT (WINAPI *DcCreateFactory)(D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS*, void**);

constexpr UINT32 DcSize = 160;

struct DcSide
{
    PCWSTR Name;
    ComPtr<ID2D1Factory1> Factory;
};

static ComPtr<ID3D11Device> DcDevice;
static ComPtr<ID3D11DeviceContext> DcContext;
static ComPtr<IDWriteFactory> DcWrite;
static WCHAR DcDumpDirectory[MAX_PATH];

static
BOOLEAN
DcLoad(
    _Inout_ DcSide& Side,
    _In_z_ PCWSTR Path)
{
    HMODULE Module = LoadLibraryExW(Path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    DcCreateFactory Create;

    if (Module == nullptr)
    {
        printf("cannot load %ls (%lu)\n", Path, GetLastError());
        return FALSE;
    }
    Create = reinterpret_cast<DcCreateFactory>(GetProcAddress(Module, "D2D1CreateFactory"));
    if (Create == nullptr || FAILED(Create(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr,
                                           reinterpret_cast<void**>(Side.Factory.GetAddressOf()))))
    {
        printf("%ls: D2D1CreateFactory failed\n", Side.Name);
        return FALSE;
    }
    return TRUE;
}

typedef VOID (*DcScene)(ID2D1DeviceContext* Context);

static
VOID
DcSceneRects(
    _In_ ID2D1DeviceContext* Context)
{
    ComPtr<ID2D1SolidColorBrush> Red;
    ComPtr<ID2D1SolidColorBrush> Blue;
    D2D1_RECT_F A = { 10.25f, 12.5f, 70.75f, 40.4f };
    D2D1_RECT_F B = { 30.0f, 50.0f, 90.0f, 80.0f };
    D2D1_RECT_F Thin = { 20.0f, 100.0f, 20.5f, 140.0f };
    D2D1_ROUNDED_RECT Round = { { 95.3f, 20.2f, 150.6f, 70.1f }, 8.0f, 8.0f };

    Context->Clear(D2D1_COLOR_F{ 1.0f, 1.0f, 1.0f, 1.0f });
    Context->CreateSolidColorBrush(D2D1_COLOR_F{ 0.8f, 0.1f, 0.1f, 1.0f }, &Red);
    Context->CreateSolidColorBrush(D2D1_COLOR_F{ 0.1f, 0.2f, 0.9f, 0.6f }, &Blue);
    Context->FillRectangle(&A, Red.Get());
    Context->FillRectangle(&B, Blue.Get());
    Context->FillRectangle(&Thin, Red.Get());
    Context->FillRoundedRectangle(&Round, Blue.Get());
}

static
VOID
DcSceneGradient(
    _In_ ID2D1DeviceContext* Context)
{
    D2D1_GRADIENT_STOP Stops[] = { { 0.0f, { 0.2f, 0.6f, 0.2f, 1.0f } }, { 1.0f, { 0.9f, 0.9f, 0.3f, 0.8f } } };
    ComPtr<ID2D1GradientStopCollection> Collection;
    ComPtr<ID2D1LinearGradientBrush> Brush;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES Line = { { 0.0f, 0.0f }, { 0.0f, 60.0f } };
    D2D1_RECT_F Bar = { 0.0f, 0.0f, (FLOAT)DcSize, 60.0f };
    D2D1_RECT_F Clip = { 20.4f, 70.6f, 140.5f, 150.5f };
    D2D1_RECT_F Inside = { 0.0f, 60.0f, (FLOAT)DcSize, (FLOAT)DcSize };

    Context->Clear(D2D1_COLOR_F{ 0.0f, 0.0f, 0.0f, 0.0f });
    Context->CreateGradientStopCollection(Stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &Collection);
    Context->CreateLinearGradientBrush(&Line, nullptr, Collection.Get(), &Brush);
    Context->FillRectangle(&Bar, Brush.Get());
    Context->PushAxisAlignedClip(&Clip, D2D1_ANTIALIAS_MODE_ALIASED);
    Context->FillRectangle(&Inside, Brush.Get());
    Context->PopAxisAlignedClip();
}

static
VOID
DcSceneText(
    _In_ ID2D1DeviceContext* Context)
{
    ComPtr<ID2D1SolidColorBrush> Black;
    ComPtr<ID2D1SolidColorBrush> White;
    ComPtr<IDWriteTextFormat> Format;
    ComPtr<IDWriteTextLayout> Layout;
    static const WCHAR Text[] = L"Settings 12:45 Wi-Fi";
    D2D1_RECT_F Band = { 0.0f, 80.0f, (FLOAT)DcSize, (FLOAT)DcSize };

    Context->Clear(D2D1_COLOR_F{ 1.0f, 1.0f, 1.0f, 1.0f });
    Context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    Context->CreateSolidColorBrush(D2D1_COLOR_F{ 0.0f, 0.0f, 0.0f, 1.0f }, &Black);
    Context->CreateSolidColorBrush(D2D1_COLOR_F{ 1.0f, 1.0f, 1.0f, 1.0f }, &White);
    DcWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &Format);
    DcWrite->CreateTextLayout(Text, (UINT32)wcslen(Text), Format.Get(), 150.0f, 60.0f, &Layout);
    Layout->SetUnderline(TRUE, { 0, 8 });
    Context->DrawTextLayout(D2D1_POINT_2F{ 4.0f, 10.0f }, Layout.Get(), Black.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
    Context->FillRectangle(&Band, Black.Get());
    Context->DrawTextLayout(D2D1_POINT_2F{ 4.0f, 90.0f }, Layout.Get(), White.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

static
VOID
DcSceneBitmap(
    _In_ ID2D1DeviceContext* Context)
{
    std::vector<UINT32> Pixels(16 * 16);
    ComPtr<ID2D1Bitmap1> Bitmap;
    D2D1_BITMAP_PROPERTIES1 Properties = {};
    D2D1_RECT_F Big = { 10.0f, 10.0f, 74.0f, 74.0f };
    D2D1_RECT_F Shifted = { 80.5f, 20.25f, 150.5f, 90.25f };
    D2D1_MATRIX_3X2_F Rotate;

    for (UINT32 Y = 0; Y < 16; Y++)
    {
        for (UINT32 X = 0; X < 16; X++)
            Pixels[Y * 16 + X] = ((X ^ Y) & 4) ? 0xFF2050C0 : 0x80804020;
    }
    Properties.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
    Properties.dpiX = Properties.dpiY = 96.0f;
    Context->Clear(D2D1_COLOR_F{ 0.9f, 0.9f, 0.9f, 1.0f });
    Context->CreateBitmap({ 16, 16 }, Pixels.data(), 64, &Properties, &Bitmap);
    Context->DrawBitmap(Bitmap.Get(), &Big, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
    Context->DrawBitmap(Bitmap.Get(), &Shifted, 0.7f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr);
    {
        FLOAT Sin = 0.258819f;
        FLOAT Cos = 0.965926f;

        Rotate = { Cos, Sin, -Sin, Cos, 80.0f - 80.0f * Cos + 120.0f * Sin, 120.0f - 80.0f * Sin - 120.0f * Cos };
    }
    Context->SetTransform(&Rotate);
    Context->DrawBitmap(Bitmap.Get(), D2D1_RECT_F{ 50.0f, 100.0f, 110.0f, 140.0f }, 1.0f,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
    Context->SetTransform(D2D1_MATRIX_3X2_F{ 1, 0, 0, 1, 0, 0 });
}

static
BOOLEAN
DcRender(
    _In_ DcSide& Side,
    _In_ DcScene Scene,
    _Out_ std::vector<UINT32>& Pixels)
{
    ComPtr<IDXGIDevice> Dxgi;
    ComPtr<ID2D1Device> Device;
    ComPtr<ID2D1DeviceContext> Context;
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<ID3D11Texture2D> Staging;
    ComPtr<IDXGISurface> Surface;
    ComPtr<ID2D1Bitmap1> Target;
    D3D11_TEXTURE2D_DESC Desc = {};
    D2D1_BITMAP_PROPERTIES1 Properties = {};
    D3D11_MAPPED_SUBRESOURCE Mapped;
    HRESULT hr;

    DcDevice.As(&Dxgi);
    if (FAILED(Side.Factory->CreateDevice(Dxgi.Get(), &Device)) ||
        FAILED(Device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &Context)))
    {
        printf("%ls: device creation failed\n", Side.Name);
        return FALSE;
    }

    Desc.Width = Desc.Height = DcSize;
    Desc.MipLevels = Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    DcDevice->CreateTexture2D(&Desc, nullptr, &Texture);
    Texture.As(&Surface);

    Properties.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
    Properties.dpiX = Properties.dpiY = 96.0f;
    Properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    hr = Context->CreateBitmapFromDxgiSurface(Surface.Get(), &Properties, &Target);
    if (FAILED(hr))
    {
        printf("%ls: CreateBitmapFromDxgiSurface failed 0x%08lX\n", Side.Name, hr);
        return FALSE;
    }

    Context->SetTarget(Target.Get());
    Context->BeginDraw();
    Scene(Context.Get());
    hr = Context->EndDraw();
    if (FAILED(hr))
        printf("%ls: EndDraw 0x%08lX\n", Side.Name, hr);

    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    DcDevice->CreateTexture2D(&Desc, nullptr, &Staging);
    DcContext->CopyResource(Staging.Get(), Texture.Get());
    if (FAILED(DcContext->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped)))
        return FALSE;
    Pixels.resize(DcSize * DcSize);
    for (UINT32 Y = 0; Y < DcSize; Y++)
        memcpy(&Pixels[Y * DcSize], static_cast<BYTE*>(Mapped.pData) + Y * Mapped.RowPitch, DcSize * 4);
    DcContext->Unmap(Staging.Get(), 0);
    return TRUE;
}

/* Expected, actual and the difference side by side, as a PPM */
static
VOID
DcDump(
    _In_z_ PCWSTR Name,
    _In_ const std::vector<UINT32>& Want,
    _In_ const std::vector<UINT32>& Got)
{
    WCHAR Path[MAX_PATH];
    FILE* File;

    if (DcDumpDirectory[0] == UNICODE_NULL)
        return;
    swprintf_s(Path, L"%s\\%s.ppm", DcDumpDirectory, Name);
    if (_wfopen_s(&File, Path, L"wb") != 0)
        return;
    fprintf(File, "P6\n%u %u\n255\n", DcSize * 3, DcSize);
    for (UINT32 Y = 0; Y < DcSize; Y++)
    {
        for (UINT32 Panel = 0; Panel < 3; Panel++)
        {
            for (UINT32 X = 0; X < DcSize; X++)
            {
                UINT32 A = Want[Y * DcSize + X];
                UINT32 B = Got[Y * DcSize + X];
                BYTE Rgb[3];

                for (UINT32 Channel = 0; Channel < 3; Channel++)
                {
                    INT Shift = 16 - Channel * 8;
                    INT Va = (A >> Shift) & 0xFF;
                    INT Vb = (B >> Shift) & 0xFF;

                    Rgb[Channel] = (BYTE)(Panel == 0 ? Va : Panel == 1 ? Vb : std::min(255, abs(Va - Vb) * 8));
                }
                fwrite(Rgb, 1, 3, File);
            }
        }
    }
    fclose(File);
}

int
__cdecl
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    static const struct
    {
        PCWSTR Name;
        DcScene Scene;
    } Scenes[] =
    {
        { L"rects", DcSceneRects },
        { L"gradient", DcSceneGradient },
        { L"text", DcSceneText },
        { L"bitmap", DcSceneBitmap },
    };
    DcSide System = { L"system" };
    DcSide ReactX = { L"reactx" };
    WCHAR SystemPath[MAX_PATH];
    ULONG Failures = 0;

    if (argc < 2)
    {
        printf("d2dcheck <d2d1.dll to test> [--dump folder]\n");
        return 2;
    }
    if (argc >= 4 && _wcsicmp(argv[2], L"--dump") == 0)
    {
        wcscpy_s(DcDumpDirectory, argv[3]);
        CreateDirectoryW(DcDumpDirectory, nullptr);
    }

    /* Text in both sides goes through the system DirectWrite, so only Direct2D differs */
    {
        typedef HRESULT (WINAPI *DcWriteCreate)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
        WCHAR WritePath[MAX_PATH];
        DcWriteCreate CreateWrite;

        GetSystemDirectoryW(WritePath, MAX_PATH);
        wcscat_s(WritePath, L"\\dwrite.dll");
        CreateWrite = reinterpret_cast<DcWriteCreate>(GetProcAddress(LoadLibraryW(WritePath), "DWriteCreateFactory"));
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                     nullptr, 0, D3D11_SDK_VERSION, &DcDevice, nullptr, &DcContext)) ||
            CreateWrite == nullptr ||
            FAILED(CreateWrite(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                               reinterpret_cast<IUnknown**>(DcWrite.GetAddressOf()))))
        {
            printf("no device\n");
            return 1;
        }
    }

    GetSystemDirectoryW(SystemPath, MAX_PATH);
    wcscat_s(SystemPath, L"\\d2d1.dll");
    if (!DcLoad(System, SystemPath) || !DcLoad(ReactX, argv[1]))
        return 1;

    for (const auto& Scene : Scenes)
    {
        std::vector<UINT32> Want;
        std::vector<UINT32> Got;
        ULONG Different = 0;
        ULONG Large = 0;
        INT Largest = 0;

        if (!DcRender(System, Scene.Scene, Want) || !DcRender(ReactX, Scene.Scene, Got))
        {
            Failures++;
            continue;
        }

        for (SIZE_T Index = 0; Index < Want.size(); Index++)
        {
            INT Worst = 0;

            for (UINT32 Shift = 0; Shift < 32; Shift += 8)
                Worst = std::max(Worst, abs((INT)((Want[Index] >> Shift) & 0xFF) - (INT)((Got[Index] >> Shift) & 0xFF)));
            if (Worst != 0)
                Different++;
            if (Worst > 8)
                Large++;
            Largest = std::max(Largest, Worst);
        }

        printf("%-9ls %5lu pixels differ, %5lu by more than 8, largest %d\n", Scene.Name, Different, Large, Largest);
        if (Large != 0)
            Failures++;
        DcDump(Scene.Name, Want, Got);
    }

    printf("%lu scenes with large differences\n", Failures);
    return Failures == 0 ? 0 : 1;
}
