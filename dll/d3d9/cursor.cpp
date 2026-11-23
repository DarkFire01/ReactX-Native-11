/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The device cursor, shown through a color cursor of the window manager
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <vector>

/* Mouse trails fight a cursor the runtime moves itself, so fullscreen chains turn them off while they hold one */
static constexpr UINT D9TrailsGet = SPI_GETMOUSETRAILS;
static constexpr UINT D9TrailsSet = SPI_SETMOUSETRAILS;

/**
 * @brief Builds a color cursor of the system cursor size from an A8R8G8B8 surface.
 *
 * Pixels that are not fully opaque become transparent; the image is scaled by nearest sampling.
 */
static
HCURSOR
D9MakeColorCursor(
    _In_ UINT HotX,
    _In_ UINT HotY,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ IDirect3DSurface9* Bitmap)
{
    UINT CursorWidth = static_cast<UINT>(GetSystemMetrics(SM_CXCURSOR));
    UINT CursorHeight = static_cast<UINT>(GetSystemMetrics(SM_CYCURSOR));
    UINT64 Pixels = static_cast<UINT64>(CursorWidth) * CursorHeight;
    struct
    {
        BITMAPINFOHEADER Header;
        RGBQUAD Colors[2];
    } MaskInfo = {};
    BITMAPINFOHEADER ColorInfo = {};
    ICONINFO Icon = {};
    D3DLOCKED_RECT Locked = {};
    std::vector<BYTE> Mask;
    std::vector<DWORD> Color;
    HCURSOR Made;
    HWND Desktop;
    HDC Screen;
    HDC Memory;

    if (Pixels > MAXUINT || Pixels * 4 > MAXUINT)
        return NULL;
    Icon.fIcon = FALSE;
    Icon.xHotspot = CursorWidth * HotX / Width;
    Icon.yHotspot = CursorHeight * HotY / Height;
    Mask.assign(static_cast<SIZE_T>(Pixels / 8), 0);
    Color.resize(static_cast<SIZE_T>(Pixels));
    if (FAILED(Bitmap->LockRect(&Locked, nullptr, 0)))
    {
        D9Trace(L"the cursor bitmap could not be locked");
        return NULL;
    }

    /* The color bitmap is bottom up, so its last row samples the surface's first */
    for (UINT Remaining = CursorHeight; Remaining > 0; Remaining -= 1)
    {
        UINT Row = Remaining - 1;
        INT SourceRow = static_cast<INT>((CursorHeight - Row - 1) * Height / CursorHeight);
        const DWORD* Source = reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(Locked.pBits) +
                                                             SourceRow * Locked.Pitch);

        for (UINT Column = 0; Column < CursorWidth; Column++)
        {
            UINT Index = Row * CursorWidth + Column;
            DWORD Pixel = Source[Width * Column / CursorWidth];

            if ((Pixel & 0xFF000000) != 0xFF000000)
            {
                Mask[Index >> 3] |= static_cast<BYTE>(1 << (7 - (Index & 7)));
                Pixel = 0;
            }
            Color[Index] = Pixel;
        }
    }
    Bitmap->UnlockRect();

    ColorInfo.biSize = sizeof(ColorInfo);
    ColorInfo.biWidth = static_cast<LONG>(CursorWidth);
    ColorInfo.biHeight = static_cast<LONG>(CursorHeight);
    ColorInfo.biPlanes = 1;
    ColorInfo.biBitCount = 32;
    ColorInfo.biCompression = BI_RGB;
    ColorInfo.biSizeImage = static_cast<DWORD>(Pixels * 4);
    Desktop = GetDesktopWindow();
    Screen = GetWindowDC(Desktop);
    if (Screen == NULL)
    {
        D9Trace(L"no screen device context for the cursor");
        return NULL;
    }
    Icon.hbmColor = CreateDIBitmap(Screen, &ColorInfo, CBM_INIT, Color.data(),
                                   reinterpret_cast<const BITMAPINFO*>(&ColorInfo), DIB_RGB_COLORS);
    ReleaseDC(Desktop, Screen);
    if (Icon.hbmColor == NULL)
        return NULL;

    Memory = CreateCompatibleDC(NULL);
    if (Memory == NULL)
    {
        DeleteObject(Icon.hbmColor);
        return NULL;
    }
    MaskInfo.Header.biSize = sizeof(MaskInfo.Header);
    MaskInfo.Header.biWidth = static_cast<LONG>(CursorWidth);
    MaskInfo.Header.biHeight = static_cast<LONG>(CursorHeight);
    MaskInfo.Header.biPlanes = 1;
    MaskInfo.Header.biBitCount = 1;
    MaskInfo.Header.biCompression = BI_RGB;
    MaskInfo.Header.biSizeImage = static_cast<DWORD>(Pixels / 8);
    MaskInfo.Header.biClrUsed = 2;
    MaskInfo.Colors[1].rgbRed = 0xFF;
    MaskInfo.Colors[1].rgbGreen = 0xFF;
    MaskInfo.Colors[1].rgbBlue = 0xFF;
    Icon.hbmMask = CreateDIBitmap(Memory, &MaskInfo.Header, CBM_INIT, Mask.data(),
                                  reinterpret_cast<const BITMAPINFO*>(&MaskInfo), DIB_RGB_COLORS);
    DeleteDC(Memory);
    if (Icon.hbmMask == NULL)
    {
        DeleteObject(Icon.hbmColor);
        return NULL;
    }

    Made = CreateIconIndirect(&Icon);
    DeleteObject(Icon.hbmMask);
    DeleteObject(Icon.hbmColor);
    return Made;
}

VOID
D9Cursor::Start(
    _In_ D9Device* Owner,
    _In_ INT X,
    _In_ INT Y,
    _In_ BOOLEAN IsWindowed)
{
    Release();
    Device = Owner;
    PositionX = X;
    PositionY = Y;
    Windowed = IsWindowed;
}

VOID
D9Cursor::Release(VOID)
{
    if (Held != nullptr)
    {
        Held->Unpin();
        Held = nullptr;
    }
    Destroy();
    SurfaceWidth = 0;
    SurfaceHeight = 0;
}

/* The surface, the background under it and a copy to draw with, all in the display format */
HRESULT
D9Cursor::MakeDriverSurfaces(
    _In_ const D3DSURFACE_DESC* Desc)
{
    D3DSURFACE_DESC Made = {};
    D3DDISPLAYMODE Mode = {};

    Device->Owner()->GetAdapterDisplayMode(Device->AdapterInfo()->Ordinal, &Mode);
    Made.Format = Mode.Format;
    Made.Type = D3DRTYPE_SURFACE;
    Made.Pool = D3DPOOL_DEFAULT;
    Made.Width = Desc->Width;
    Made.Height = Desc->Height;
    for (D9Resource& Surface : Surfaces)
    {
        if (FAILED(Device->CreateDriverSurface(&Made, TRUE, FALSE, D3DDDIPOOL_LOCALVIDMEM, nullptr, 0, nullptr,
                                               &Surface)))
        {
            Device->CreateDriverSurface(&Made, TRUE, FALSE, D3DDDIPOOL_SYSTEMMEM, nullptr, 0, nullptr, &Surface);
        }
    }
    for (const D9Resource& Surface : Surfaces)
    {
        if (Surface.DriverResource == NULL)
        {
            D9_TRACE_ONCE(L"the driver could not make the surfaces a drawn cursor needs");
            return D3DERR_NOTAVAILABLE;
        }
    }
    SurfaceWidth = Desc->Width;
    SurfaceHeight = Desc->Height;
    return S_OK;
}

VOID
D9Cursor::Destroy(VOID)
{
    for (D9Resource& Surface : Surfaces)
    {
        if (Surface.DriverResource != NULL)
            Device->DestroyDriverSurface(&Surface);
    }
    if (Icon != NULL)
    {
        if (GetCursor() == Icon)
            SetCursor(NULL);
        if (!DestroyIcon(Icon))
            D9Trace(L"the device cursor could not be destroyed");
        Icon = NULL;
        if (Trails > 1)
        {
            SystemParametersInfoW(D9TrailsSet, Trails, nullptr, 0);
            Trails = 0;
        }
    }
    Hardware = FALSE;
    Visible = FALSE;
}

HRESULT
D9Cursor::SetProperties(
    _In_ UINT HotX,
    _In_ UINT HotY,
    _In_ D9Surface* Bitmap)
{
    D3DSURFACE_DESC Desc = {};
    HCURSOR Made;

    Hardware = FALSE;
    Bitmap->GetDesc(&Desc);
    if (!Windowed && Icon == NULL)
    {
        SystemParametersInfoW(D9TrailsGet, 0, &Trails, 0);
        if (Trails > 1)
            SystemParametersInfoW(D9TrailsSet, 0, nullptr, 0);
    }

    Made = D9MakeColorCursor(HotX, HotY, Desc.Width, Desc.Height, Bitmap);
    if (Made != NULL)
    {
        if (Visible)
            SetCursor(Made);
        if (Icon != NULL && !DestroyIcon(Icon))
            D9Trace(L"the previous device cursor could not be destroyed");
        Icon = Made;
        Hardware = TRUE;
        Bitmap->Pin();
        if (Held != nullptr)
            Held->Unpin();
        Held = Bitmap;
        return S_OK;
    }

    /* Without a color cursor the runtime draws one itself, from surfaces of the driver */
    if (Trails > 1)
    {
        SystemParametersInfoW(D9TrailsSet, Trails, nullptr, 0);
        Trails = 0;
    }
    SetCursor(NULL);
    if (Icon != NULL)
    {
        DestroyIcon(Icon);
        Icon = NULL;
    }
    if (Desc.Width != SurfaceWidth || Desc.Height != SurfaceHeight)
    {
        HRESULT hr;

        Destroy();
        hr = MakeDriverSurfaces(&Desc);
        if (FAILED(hr))
            return hr;
    }
    {
        D3DLOCKED_RECT Locked = {};

        if (FAILED(Bitmap->LockRect(&Locked, nullptr, 0)))
        {
            D9_TRACE_ONCE(L"the cursor bitmap could not be locked");
            return D3DERR_INVALIDCALL;
        }
        Bitmap->UnlockRect();
    }
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

VOID
D9Cursor::SetPosition(
    _In_ INT X,
    _In_ INT Y)
{
    POINT Current = {};

    /* The window manager draws the cursor, so moving it moves the real one when it is not already there */
    if (Hardware && Icon != NULL)
    {
        if (X == PositionX && Y == PositionY && GetCursorPos(&Current) && Current.x == X && Current.y == Y)
            return;
        PositionX = X;
        PositionY = Y;
        SetCursorPos(X, Y);
        return;
    }
    PositionX = X;
    PositionY = Y;
}

BOOL
D9Cursor::SetVisibility(
    _In_ BOOL Show)
{
    BOOL Previous = Visible;
    HCURSOR Current;

    /* Until the device has a cursor image the call only reports */
    if (!Hardware)
        return Previous;

    Current = GetCursor();
    if (Show)
    {
        if (Icon != Current)
            SetCursor(Icon);
    }
    else if (Current != NULL)
    {
        SetCursor(NULL);
    }
    Visible = Show != FALSE;
    return Previous;
}
