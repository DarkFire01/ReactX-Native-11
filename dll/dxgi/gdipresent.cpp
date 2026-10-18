/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Presenting through GDI when no compositor takes the frame
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "swapchain.hpp"

#include <d3d11.h>
#include <vector>

/* present= in reactx.ini picks a path for every present, to try the ones DWM normally hides */
RxPresentOverride
RxGetPresentOverride(VOID)
{
    static const RxPresentOverride Override = []()
    {
        WCHAR Path[MAX_PATH];
        WCHAR Value[16] = {};
        HMODULE Module = NULL;
        PWSTR Slash;

        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&RxGetPresentOverride), &Module) ||
            GetModuleFileNameW(Module, Path, _countof(Path)) == 0 || (Slash = wcsrchr(Path, L'\\')) == nullptr)
        {
            return RxPresentOverride::None;
        }

        wcscpy_s(Slash + 1, _countof(Path) - (Slash + 1 - Path), L"reactx.ini");
        GetPrivateProfileStringW(L"reactx", L"present", L"", Value, _countof(Value), Path);
        if (_wcsicmp(Value, L"gdi") == 0)
            return RxPresentOverride::Gdi;
        if (_wcsicmp(Value, L"window") == 0)
            return RxPresentOverride::Window;
        return RxPresentOverride::None;
    }();

    return Override;
}

/* Half float to a byte, clamped to [0, 1] */
static
BYTE
RxHalfToByte(
    _In_ USHORT Half)
{
    UINT Exponent = (Half >> 10) & 0x1F;
    UINT Mantissa = Half & 0x3FF;
    FLOAT Value;

    if (Half & 0x8000)
        return 0;
    if (Exponent == 0x1F)
        return Mantissa ? 0 : 255;

    Value = (Exponent == 0) ? (Mantissa / 1024.0f) / 16384.0f
                            : (1.0f + Mantissa / 1024.0f) * (FLOAT)(1u << Exponent) / 32768.0f;
    return (Value >= 1.0f) ? 255 : (BYTE)(Value * 255.0f + 0.5f);
}

BOOLEAN
RxConvertRowToBgra(
    _In_ DXGI_FORMAT Format,
    _In_reads_bytes_(Width * 8) const BYTE* Source,
    _Out_writes_(Width) UINT* Destination,
    _In_ UINT Width)
{
    const UINT* Texels = reinterpret_cast<const UINT*>(Source);
    const USHORT* Halves = reinterpret_cast<const USHORT*>(Source);
    UINT X;
    UINT Value;

    switch (Format)
    {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            memcpy(Destination, Source, Width * sizeof(*Destination));
            return TRUE;

        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            for (X = 0; X < Width; X++)
            {
                Value = Texels[X];
                Destination[X] = (Value & 0xFF00FF00) | ((Value & 0xFF) << 16) | ((Value >> 16) & 0xFF);
            }
            return TRUE;

        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            for (X = 0; X < Width; X++)
            {
                Value = Texels[X];
                Destination[X] = 0xFF000000 | (((Value >> 2) & 0xFF) << 16) | (((Value >> 12) & 0xFF) << 8) |
                                 ((Value >> 22) & 0xFF);
            }
            return TRUE;

        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            for (X = 0; X < Width; X++)
            {
                Destination[X] = 0xFF000000 | ((UINT)RxHalfToByte(Halves[X * 4]) << 16) |
                                 ((UINT)RxHalfToByte(Halves[X * 4 + 1]) << 8) | RxHalfToByte(Halves[X * 4 + 2]);
            }
            return TRUE;

        default:
            return FALSE;
    }
}

VOID
RxSwapChain::ReleaseGdiCopies()
{
    if (GdiStaging != nullptr)
        GdiStaging->Release();
    if (GdiResolved != nullptr)
        GdiResolved->Release();
    GdiStaging = nullptr;
    GdiResolved = nullptr;
}

VOID
RxWaitForInterval(
    _In_ IDXGISwapChain* SwapChain,
    _In_ UINT SyncInterval)
{
    IDXGIOutput* Output = nullptr;
    UINT Count;

    if (SyncInterval == 0)
        return;

    if (FAILED(SwapChain->GetContainingOutput(&Output)))
    {
        Sleep(16 * SyncInterval);
        return;
    }

    for (Count = 0; Count < SyncInterval; Count++)
    {
        if (FAILED(Output->WaitForVBlank()))
        {
            Sleep(16 * (SyncInterval - Count));
            break;
        }
    }

    Output->Release();
}

HRESULT
RxSwapChain::PresentGdi(
    _In_ UINT SyncInterval)
{
    ID3D11Texture2D* Source = nullptr;
    ID3D11Device* D3dDevice = nullptr;
    ID3D11DeviceContext* Context = nullptr;
    ID3D11Resource* Readable;
    D3D11_TEXTURE2D_DESC TextureDesc;
    D3D11_TEXTURE2D_DESC CopyDesc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    BITMAPINFO Info = {};
    std::vector<UINT> Pixels;
    RECT Client;
    HRESULT hr;
    HDC Dc;
    UINT Y;

    if (IsIconic(Window) || !IsWindowVisible(Window))
        return DXGI_STATUS_OCCLUDED;

    hr = Buffers[0]->QueryInterface(IID_PPV_ARGS(&Source));
    if (FAILED(hr))
        return hr;

    Source->GetDevice(&D3dDevice);
    D3dDevice->GetImmediateContext(&Context);
    Source->GetDesc(&TextureDesc);

    /* One staging copy, plus a resolve target for multisampled buffers, kept until the buffers change */
    CopyDesc = TextureDesc;
    CopyDesc.MipLevels = 1;
    CopyDesc.ArraySize = 1;
    CopyDesc.SampleDesc = { 1, 0 };
    CopyDesc.MiscFlags = 0;
    if (TextureDesc.SampleDesc.Count > 1 && GdiResolved == nullptr)
    {
        CopyDesc.Usage = D3D11_USAGE_DEFAULT;
        CopyDesc.BindFlags = 0;
        CopyDesc.CPUAccessFlags = 0;
        hr = D3dDevice->CreateTexture2D(&CopyDesc, nullptr, &GdiResolved);
    }

    if (SUCCEEDED(hr) && GdiStaging == nullptr)
    {
        CopyDesc.Usage = D3D11_USAGE_STAGING;
        CopyDesc.BindFlags = 0;
        CopyDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = D3dDevice->CreateTexture2D(&CopyDesc, nullptr, &GdiStaging);
    }

    if (SUCCEEDED(hr))
    {
        Readable = Source;
        if (GdiResolved != nullptr)
        {
            Context->ResolveSubresource(GdiResolved, 0, Source, 0, TextureDesc.Format);
            Readable = GdiResolved;
        }

        Context->CopySubresourceRegion(GdiStaging, 0, 0, 0, 0, Readable, 0, nullptr);
        hr = Context->Map(GdiStaging, 0, D3D11_MAP_READ, 0, &Mapped);
    }

    if (SUCCEEDED(hr))
    {
        Pixels.resize((SIZE_T)TextureDesc.Width * TextureDesc.Height);
        for (Y = 0; Y < TextureDesc.Height; Y++)
        {
            if (!RxConvertRowToBgra(TextureDesc.Format, static_cast<const BYTE*>(Mapped.pData) + (SIZE_T)Y * Mapped.RowPitch,
                                    &Pixels[(SIZE_T)Y * TextureDesc.Width], TextureDesc.Width))
            {
                hr = DXGI_ERROR_UNSUPPORTED;
                break;
            }
        }
        Context->Unmap(GdiStaging, 0);
    }

    Context->Release();
    D3dDevice->Release();
    Source->Release();
    if (FAILED(hr))
    {
        RX_TRACE(L"GDI present failed 0x%08lX (format %u)", hr, TextureDesc.Format);
        return hr;
    }

    /* The window's client area takes the frame, stretched as the swapchain's scaling would */
    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = TextureDesc.Width;
    Info.bmiHeader.biHeight = -(LONG)TextureDesc.Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;

    GetClientRect(Window, &Client);
    Dc = GetDC(Window);
    if (Dc == NULL)
        return DXGI_STATUS_OCCLUDED;

    RxWaitForInterval(this, SyncInterval);
    if (Desc.Scaling == DXGI_SCALING_NONE)
    {
        SetDIBitsToDevice(Dc, 0, 0, TextureDesc.Width, TextureDesc.Height, 0, 0, 0, TextureDesc.Height, Pixels.data(),
                          &Info, DIB_RGB_COLORS);
    }
    else
    {
        SetStretchBltMode(Dc, COLORONCOLOR);
        StretchDIBits(Dc, 0, 0, Client.right - Client.left, Client.bottom - Client.top, 0, 0, TextureDesc.Width,
                      TextureDesc.Height, Pixels.data(), &Info, DIB_RGB_COLORS, SRCCOPY);
    }
    ReleaseDC(Window, Dc);

    PresentCount++;
    if (LatencySemaphore != NULL)
        ReleaseSemaphore(LatencySemaphore, 1, nullptr);

    return S_OK;
}
