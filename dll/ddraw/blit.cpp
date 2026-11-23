/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Emulation layer blits, fills and presenting to the primary through GDI
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

/* Blt flags the emulation layer knows; anything else is refused */
static constexpr DWORD DdBltKnownFlags = DDBLT_ASYNC | DDBLT_COLORFILL | DDBLT_DDFX | DDBLT_DDROPS | DDBLT_KEYDEST |
                                         DDBLT_KEYDESTOVERRIDE | DDBLT_KEYSRC | DDBLT_KEYSRCOVERRIDE | DDBLT_ROP |
                                         DDBLT_ROTATIONANGLE | DDBLT_WAIT | DDBLT_DONOTWAIT | DDBLT_DEPTHFILL |
                                         DDBLT_ALPHADEST | DDBLT_ALPHASRC | DDBLT_ZBUFFER;

struct DdBlitJob
{
    DdSurface* Target;
    BYTE* TargetBits;
    RECT To;
    DdSurface* Source;
    BYTE* SourceBits;
    RECT From;
    BOOLEAN Keyed;
    DDCOLORKEY Key;
    DWORD KeyMask;
    BOOLEAN MirrorX;
    BOOLEAN MirrorY;
};

static
DWORD
DdBytesPerPixel(
    _In_ const DdSurface* Surface)
{
    return Surface->Shared->Format.dwRGBBitCount / 8;
}

static
DWORD
DdReadPixel(
    _In_reads_bytes_(Size) const BYTE* Pixel,
    _In_ DWORD Size)
{
    DWORD Value = 0;

    memcpy(&Value, Pixel, Size);
    return Value;
}

static
VOID
DdWritePixel(
    _Out_writes_bytes_(Size) BYTE* Pixel,
    _In_ DWORD Size,
    _In_ DWORD Value)
{
    memcpy(Pixel, &Value, Size);
}

static
BOOLEAN
DdRectInside(
    _In_ const RECT* Rect,
    _In_ const DdSurface* Surface)
{
    return Rect->left >= 0 && Rect->top >= 0 && Rect->right <= static_cast<LONG>(Surface->Shared->Width) &&
           Rect->bottom <= static_cast<LONG>(Surface->Shared->Height) && Rect->left < Rect->right &&
           Rect->top < Rect->bottom;
}

/* Bits a color key compares: the color channels, or the whole index for palettized pixels */
static
DWORD
DdKeyMask(
    _In_ const DdSurface* Surface)
{
    const DDPIXELFORMAT* Format = &Surface->Shared->Format;
    DWORD Mask = Format->dwRBitMask | Format->dwGBitMask | Format->dwBBitMask;

    if (Mask == 0)
        Mask = Format->dwRGBBitCount >= 32 ? 0xFFFFFFFF : (1u << Format->dwRGBBitCount) - 1;
    return Mask;
}

/* WHITENESS sets every bit of a pixel, alpha included, except the top byte of 32 bit pixels */
static
DWORD
DdWhiteFor(
    _In_ const DdSurface* Surface)
{
    DWORD Bits = Surface->Shared->Format.dwRGBBitCount;

    return Bits >= 32 ? 0x00FFFFFF : (1u << Bits) - 1;
}

static
VOID
DdFillRect(
    _In_ DdSurface* Target,
    _In_ BYTE* Bits,
    _In_ const RECT* To,
    _In_ DWORD Value)
{
    DWORD Size = DdBytesPerPixel(Target);
    BYTE* Row;
    LONG X;
    LONG Y;

    for (Y = To->top; Y < To->bottom; Y++)
    {
        Row = Bits + Y * Target->Shared->Pitch + To->left * Size;
        for (X = To->left; X < To->right; X++, Row += Size)
            DdWritePixel(Row, Size, Value);
    }
}

/*
 * After SetSurfaceDesc changes the pixel size, fills still spread the color as the surface was
 * created and write it four bytes at a time, as the system does.
 */
static
VOID
DdFillRectStale(
    _In_ DdSurface* Target,
    _In_ BYTE* Bits,
    _In_ const RECT* To,
    _In_ DWORD Value)
{
    DWORD Size = DdBytesPerPixel(Target);
    DWORD Masked = Value & ((1u << Target->Shared->Format.dwRGBBitCount) - 1);
    DWORD Pattern = Masked;
    BYTE* Row;
    DWORD Count;
    LONG Y;

    if (Target->Shared->FillBitCount == 8)
        Pattern = Masked * 0x01010101;
    else if (Target->Shared->FillBitCount == 16)
        Pattern = Masked | (Masked << 16);

    for (Y = To->top; Y < To->bottom; Y++)
    {
        Row = Bits + Y * Target->Shared->Pitch + To->left * Size;
        Count = (To->right - To->left) * Size;
        for (DWORD Index = 0; Index < Count; Index++)
            Row[Index] = static_cast<BYTE>(Pattern >> ((Index % 4) * 8));
    }
}

/* Nearest pixel sampling from the middle of each target pixel, mirrored when asked */
static
VOID
DdCopyRect(
    _In_ const DdBlitJob& Job,
    _In_ const RECT* Clip)
{
    DWORD Size = DdBytesPerPixel(Job.Target);
    LONG TargetWidth = Job.To.right - Job.To.left;
    LONG TargetHeight = Job.To.bottom - Job.To.top;
    LONG SourceWidth = Job.From.right - Job.From.left;
    LONG SourceHeight = Job.From.bottom - Job.From.top;
    BYTE* Copy = nullptr;
    const BYTE* SourceBits = Job.SourceBits;
    LONG SourcePitch = Job.Source->Shared->Pitch;
    const BYTE* SourceRow;
    BYTE* TargetRow;
    DWORD Pixel;
    LONG SourceX;
    LONG SourceY;
    LONG X;
    LONG Y;

    /* Blits within one surface read from a copy so overlapping rectangles behave like a move */
    if (Job.Source == Job.Target)
    {
        Copy = static_cast<BYTE*>(LocalAlloc(LMEM_FIXED,
                                             static_cast<SIZE_T>(Job.Source->Shared->Pitch) * Job.Source->Shared->Height));
        if (Copy == nullptr)
            return;
        memcpy(Copy, Job.SourceBits, static_cast<SIZE_T>(Job.Source->Shared->Pitch) * Job.Source->Shared->Height);
        SourceBits = Copy;
    }

    for (Y = Clip->top - Job.To.top; Y < Clip->bottom - Job.To.top; Y++)
    {
        SourceY = static_cast<LONG>((static_cast<LONGLONG>(2 * Y + 1) * SourceHeight) / (2 * TargetHeight));
        if (Job.MirrorY)
            SourceY = SourceHeight - 1 - SourceY;
        SourceRow = SourceBits + (Job.From.top + SourceY) * SourcePitch;
        TargetRow = Job.TargetBits + (Job.To.top + Y) * Job.Target->Shared->Pitch;

        for (X = Clip->left - Job.To.left; X < Clip->right - Job.To.left; X++)
        {
            SourceX = static_cast<LONG>((static_cast<LONGLONG>(2 * X + 1) * SourceWidth) / (2 * TargetWidth));
            if (Job.MirrorX)
                SourceX = SourceWidth - 1 - SourceX;

            Pixel = DdReadPixel(SourceRow + (Job.From.left + SourceX) * Size, Size);
            if (Job.Keyed && (Pixel & Job.KeyMask) >= (Job.Key.dwColorSpaceLowValue & Job.KeyMask) &&
                (Pixel & Job.KeyMask) <= (Job.Key.dwColorSpaceHighValue & Job.KeyMask))
            {
                continue;
            }
            DdWritePixel(TargetRow + (Job.To.left + X) * Size, Size, Pixel);
        }
    }

    if (Copy != nullptr)
        LocalFree(Copy);
}

/* A color in the primary's format, as GDI wants it */
static
COLORREF
DdPixelToColor(
    _In_ const DDPIXELFORMAT* Format,
    _In_ DWORD Pixel)
{
    DWORD Channel[3] = { Format->dwRBitMask, Format->dwGBitMask, Format->dwBBitMask };
    BYTE Value[3];
    DWORD Index;
    DWORD Shift;
    DWORD Bits;

    for (Index = 0; Index < 3; Index++)
    {
        if (Channel[Index] == 0)
        {
            Value[Index] = 0;
            continue;
        }
        for (Shift = 0; ((Channel[Index] >> Shift) & 1) == 0; Shift++)
            ;
        Bits = Channel[Index] >> Shift;
        Value[Index] = static_cast<BYTE>((((Pixel & Channel[Index]) >> Shift) * 255) / Bits);
    }

    return RGB(Value[0], Value[1], Value[2]);
}

/* The primary belongs to the desktop compositor, so drawing on it goes through GDI */
static
HRESULT
DdPresentToPrimary(
    _In_ DdSurface* Primary,
    _In_ const RECT* To,
    _In_opt_ DdSurface* Source,
    _In_opt_ const RECT* From,
    _In_ BOOLEAN Fill,
    _In_ DWORD FillValue)
{
    HWND Window = Primary->Clipper != nullptr ? Primary->Clipper->Window : NULL;
    struct
    {
        BITMAPINFOHEADER Header;
        DWORD Masks[3];
    } Info = {};
    POINT Origin = { 0, 0 };
    HBRUSH Brush;
    RECT Local = *To;
    BYTE* Bits = nullptr;
    BOOLEAN Handled;
    HDC Dc;
    int Lines;
    HRESULT hr;

    /* A window clipper sends the pixels to the window's redirection surface, as the system does */
    if (Window != NULL && Primary->Driver->Hardware != nullptr && Source != Primary)
    {
        hr = DdPresentThroughDwm(Primary, Window, To, Source, From, Fill, FillValue, &Handled);
        if (Handled)
            return hr;
    }

    if (!Fill)
    {
        hr = DdMapSurface(Source, TRUE, &Bits);
        if (FAILED(hr))
            return hr;
    }

    Dc = GetDC(Window);
    if (Dc == NULL)
    {
        if (!Fill)
            DdUnmapSurface(Source);
        return DDERR_CANTCREATEDC;
    }

    /* The rectangle is in screen space; a window clipper draws through that window's DC */
    if (Window != NULL)
    {
        ClientToScreen(Window, &Origin);
        OffsetRect(&Local, -Origin.x, -Origin.y);
    }

    if (Fill)
    {
        Brush = CreateSolidBrush(DdPixelToColor(&Primary->Shared->Format, FillValue));
        FillRect(Dc, &Local, Brush);
        DeleteObject(Brush);
        ReleaseDC(Window, Dc);
        return S_OK;
    }

    Info.Header.biSize = sizeof(Info.Header);
    Info.Header.biWidth = static_cast<LONG>(Source->Shared->Pitch * 8 / Source->Shared->Format.dwRGBBitCount);
    Info.Header.biHeight = -static_cast<LONG>(Source->Shared->Height);
    Info.Header.biPlanes = 1;
    Info.Header.biBitCount = static_cast<WORD>(Source->Shared->Format.dwRGBBitCount);
    Info.Header.biCompression = BI_RGB;
    if (Source->Shared->Format.dwRGBBitCount == 16 || Source->Shared->Format.dwRGBBitCount == 32)
    {
        Info.Header.biCompression = BI_BITFIELDS;
        Info.Masks[0] = Source->Shared->Format.dwRBitMask;
        Info.Masks[1] = Source->Shared->Format.dwGBitMask;
        Info.Masks[2] = Source->Shared->Format.dwBBitMask;
    }

    SetStretchBltMode(Dc, COLORONCOLOR);
    Lines = StretchDIBits(Dc, Local.left, Local.top, Local.right - Local.left, Local.bottom - Local.top, From->left,
                          From->top, From->right - From->left, From->bottom - From->top, Bits,
                          reinterpret_cast<BITMAPINFO*>(&Info), DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(Window, Dc);
    DdUnmapSurface(Source);
    if (Lines == 0)
    {
        DD_TRACE_ONCE(L"StretchDIBits to the primary failed (%lu)", GetLastError());
        return DDERR_GENERIC;
    }
    return S_OK;
}

/* The parts of a target rectangle an off screen clipper's list lets through */
struct DdClipPieces
{
    RECT* Rects;
    DWORD Count;
    RECT Whole;
};

static
BOOLEAN
DdClipTarget(
    _In_ const DdSurface* Target,
    _In_ const RECT* To,
    _Out_ DdClipPieces* Pieces)
{
    const RGNDATA* List = Target->Clipper != nullptr ? Target->Clipper->ClipList : nullptr;
    const RECT* Rects;
    DWORD Index;

    Pieces->Rects = &Pieces->Whole;
    Pieces->Count = 1;
    Pieces->Whole = *To;
    if (List == nullptr || Target->Clipper->Window != NULL)
        return TRUE;

    Pieces->Rects = new (std::nothrow) RECT[List->rdh.nCount != 0 ? List->rdh.nCount : 1];
    if (Pieces->Rects == nullptr)
        return FALSE;

    Rects = reinterpret_cast<const RECT*>(List->Buffer);
    Pieces->Count = 0;
    for (Index = 0; Index < List->rdh.nCount; Index++)
    {
        if (IntersectRect(&Pieces->Rects[Pieces->Count], &Rects[Index], To))
            Pieces->Count += 1;
    }
    return TRUE;
}

static
VOID
DdFreeClipPieces(
    _Inout_ DdClipPieces* Pieces)
{
    if (Pieces->Rects != &Pieces->Whole)
        delete[] Pieces->Rects;
    Pieces->Rects = nullptr;
}

/* The source rectangle that lands on one piece of the target rectangle */
static
VOID
DdSourceForPiece(
    _In_ const RECT* To,
    _In_ const RECT* From,
    _In_ const RECT* Piece,
    _Out_ RECT* Part)
{
    LONGLONG TargetWidth = To->right - To->left;
    LONGLONG TargetHeight = To->bottom - To->top;
    LONGLONG SourceWidth = From->right - From->left;
    LONGLONG SourceHeight = From->bottom - From->top;

    Part->left = From->left + static_cast<LONG>((Piece->left - To->left) * SourceWidth / TargetWidth);
    Part->right = From->left + static_cast<LONG>((Piece->right - To->left) * SourceWidth / TargetWidth);
    Part->top = From->top + static_cast<LONG>((Piece->top - To->top) * SourceHeight / TargetHeight);
    Part->bottom = From->top + static_cast<LONG>((Piece->bottom - To->top) * SourceHeight / TargetHeight);
}

static
HANDLE
DdDriverHandle(
    _In_ const DdSurface* Surface)
{
    if (Surface->Resource != nullptr)
        return Surface->Resource->DriverResource;
    return Surface->Shared->SystemResource->DriverResource;
}

/* Hands a copy between two driver resources to the driver, keys and mirroring included */
static
HRESULT
DdDriverCopy(
    _In_ DdSurface* Target,
    _In_ const RECT* To,
    _In_ DdSurface* Source,
    _In_ const RECT* From,
    _In_ DWORD Flags,
    _In_opt_ const DDBLTFX* Fx)
{
    DdHardware* Hardware = Target->Driver->Hardware;
    D3DDDIARG_BLT Blt = {};
    DdClipPieces Pieces;
    DWORD Index;
    HRESULT hr;

    Blt.hSrcResource = DdDriverHandle(Source);
    Blt.SrcRect = *From;
    Blt.hDstResource = DdDriverHandle(Target);
    Blt.DstRect = *To;

    if ((Flags & DDBLT_KEYSRCOVERRIDE) != 0)
    {
        Blt.Flags.SrcColorKey = TRUE;
        Blt.ColorKey = Fx->ddckSrcColorkey.dwColorSpaceLowValue;
    }
    else if ((Flags & DDBLT_KEYSRC) != 0)
    {
        if ((Source->KeyFlags & DDCKEY_SRCBLT) == 0)
            return DDERR_NOCOLORKEY;
        Blt.Flags.SrcColorKey = TRUE;
        Blt.ColorKey = Source->SourceKey.dwColorSpaceLowValue;
    }

    if ((Flags & DDBLT_DDFX) != 0)
    {
        if ((Fx->dwDDFX & (DDBLTFX_ROTATE90 | DDBLTFX_ROTATE180 | DDBLTFX_ROTATE270 | DDBLTFX_ZBUFFERRANGE |
                           DDBLTFX_ZBUFFERBASEDEST)) != 0)
        {
            return DDERR_UNSUPPORTED;
        }
        Blt.Flags.MirrorLeftRight = (Fx->dwDDFX & DDBLTFX_MIRRORLEFTRIGHT) != 0;
        Blt.Flags.MirrorUpDown = (Fx->dwDDFX & DDBLTFX_MIRRORUPDOWN) != 0;
    }
    if ((Flags & (DDBLT_DDROPS | DDBLT_ROTATIONANGLE)) != 0)
        return DDERR_UNSUPPORTED;

    if (!DdClipTarget(Target, To, &Pieces))
        return E_OUTOFMEMORY;

    hr = S_OK;
    for (Index = 0; Index < Pieces.Count && SUCCEEDED(hr); Index++)
    {
        Blt.DstRect = Pieces.Rects[Index];
        DdSourceForPiece(To, From, &Pieces.Rects[Index], &Blt.SrcRect);
        hr = Hardware->DeviceFunctions.pfnBlt(Hardware->DriverDevice, &Blt);
    }
    DdFreeClipPieces(&Pieces);

    if (FAILED(hr))
    {
        DdTrace(L"UMD Blt failed 0x%08lX", hr);
        return hr;
    }

    if (Target->Shared->SystemResource != nullptr)
        Target->Shared->DriverPending = TRUE;
    if (Source->Shared->SystemResource != nullptr)
        Source->Shared->DriverPending = TRUE;
    return S_OK;
}

/* The driver only promises plain same size copies across the bus; anything more is emulated */
static
BOOLEAN
DdDriverTakesBusCopy(
    _In_ DWORD Flags,
    _In_opt_ const DDBLTFX* Fx,
    _In_ const RECT* To,
    _In_ const RECT* From)
{
    if ((Flags & (DDBLT_KEYSRC | DDBLT_KEYSRCOVERRIDE | DDBLT_DDFX | DDBLT_DDROPS | DDBLT_ROTATIONANGLE)) != 0)
        return FALSE;
    if ((Flags & DDBLT_ROP) != 0 && Fx->dwROP != SRCCOPY)
        return FALSE;
    return To->right - To->left == From->right - From->left && To->bottom - To->top == From->bottom - From->top;
}

/* A copy between video and system memory, after the system memory side gets its driver resource */
static
HRESULT
DdBusCopy(
    _In_ DdSurface* Target,
    _In_ const RECT* To,
    _In_ DdSurface* Source,
    _In_ const RECT* From,
    _In_ DWORD Flags,
    _In_opt_ const DDBLTFX* Fx)
{
    DdSurface* System = Target->Resource == nullptr ? Target : Source;
    HRESULT hr;

    hr = DdCreateSystemResource(Target->Driver->Hardware, System);
    if (FAILED(hr))
        return E_FAIL;
    return DdDriverCopy(Target, To, Source, From, Flags, Fx);
}

/* Widens one channel of Bits bits to eight */
static
DWORD
DdWiden(
    _In_ DWORD Value,
    _In_ DWORD Shift,
    _In_ DWORD Bits)
{
    DWORD Top = (1u << Bits) - 1;

    return 255 * ((Value >> Shift) & Top) / Top;
}

/* The driver fills with a D3DCOLOR, so the surface's own pixel value is spread out to ARGB first */
static
D3DCOLOR
DdFillColorFor(
    _In_ D3DDDIFORMAT Format,
    _In_ DWORD Value)
{
    DWORD Alpha = 0;
    DWORD Red;
    DWORD Green;
    DWORD Blue;

    switch (Format)
    {
    case D3DDDIFMT_A8R8G8B8:
        Alpha = (Value >> 24) & 0xFF;
        __fallthrough;
    case D3DDDIFMT_R8G8B8:
    case D3DDDIFMT_X8R8G8B8:
        Red = (Value >> 16) & 0xFF;
        Green = (Value >> 8) & 0xFF;
        Blue = Value & 0xFF;
        break;
    case D3DDDIFMT_A8B8G8R8:
        Alpha = (Value >> 24) & 0xFF;
        __fallthrough;
    case D3DDDIFMT_X8B8G8R8:
        Red = Value & 0xFF;
        Green = (Value >> 8) & 0xFF;
        Blue = (Value >> 16) & 0xFF;
        break;
    case D3DDDIFMT_R5G6B5:
        Red = DdWiden(Value, 11, 5);
        Green = DdWiden(Value, 5, 6);
        Blue = DdWiden(Value, 0, 5);
        break;
    case D3DDDIFMT_A1R5G5B5:
        Alpha = DdWiden(Value, 15, 1);
        __fallthrough;
    case D3DDDIFMT_X1R5G5B5:
        Red = DdWiden(Value, 10, 5);
        Green = DdWiden(Value, 5, 5);
        Blue = DdWiden(Value, 0, 5);
        break;
    case D3DDDIFMT_A4R4G4B4:
        Alpha = DdWiden(Value, 12, 4);
        __fallthrough;
    case D3DDDIFMT_X4R4G4B4:
        Red = DdWiden(Value, 8, 4);
        Green = DdWiden(Value, 4, 4);
        Blue = DdWiden(Value, 0, 4);
        break;
    case D3DDDIFMT_A8R3G3B2:
        Alpha = DdWiden(Value, 8, 8);
        __fallthrough;
    case D3DDDIFMT_R3G3B2:
        Red = DdWiden(Value, 5, 3);
        Green = DdWiden(Value, 2, 3);
        Blue = DdWiden(Value, 0, 2);
        break;
    default:
        return Value;
    }
    return (Alpha << 24) | (Red << 16) | (Green << 8) | Blue;
}

/* Video memory fills are the driver's ColorFill, one call per visible piece */
static
HRESULT
DdDriverFill(
    _In_ DdSurface* Target,
    _In_ const DdClipPieces* Pieces,
    _In_ DWORD Value)
{
    DdHardware* Hardware = Target->Driver->Hardware;
    D3DDDIARG_COLORFILL Fill = {};
    DWORD Index;
    HRESULT hr = S_OK;

    Fill.hResource = Target->Resource->DriverResource;
    Fill.Color = DdFillColorFor(Target->Resource->Format, Value);
    for (Index = 0; Index < Pieces->Count && SUCCEEDED(hr); Index++)
    {
        Fill.DstRect = Pieces->Rects[Index];
        hr = Hardware->DeviceFunctions.pfnColorFill(Hardware->DriverDevice, &Fill);
    }
    if (FAILED(hr))
        DdTrace(L"UMD ColorFill failed 0x%08lX", hr);
    return hr;
}

/* Color fills of video memory go to the driver; raster operation fills are always written by hand */
static
HRESULT
DdFillSurface(
    _In_ DdSurface* Target,
    _In_ const RECT* To,
    _In_ DWORD Value,
    _In_ BOOLEAN ColorFill)
{
    DWORD BitCount = Target->Shared->Format.dwRGBBitCount;
    BOOLEAN Stale = Target->Shared->FillBitCount != BitCount && BitCount < 24;
    DdClipPieces Pieces;
    BYTE* Bits;
    DWORD Index;
    HRESULT hr;

    if (BitCount < 8)
        return DDERR_UNSUPPORTED;
    if (!DdClipTarget(Target, To, &Pieces))
        return E_OUTOFMEMORY;

    if (ColorFill && Target->Resource != nullptr && Target->Driver->Hardware->DeviceFunctions.pfnColorFill != nullptr)
    {
        hr = DdDriverFill(Target, &Pieces, Value);
        DdFreeClipPieces(&Pieces);
        return hr;
    }

    hr = DdMapSurface(Target, FALSE, &Bits);
    if (SUCCEEDED(hr))
    {
        for (Index = 0; Index < Pieces.Count; Index++)
        {
            if (Stale)
                DdFillRectStale(Target, Bits, &Pieces.Rects[Index], Value);
            else
                DdFillRect(Target, Bits, &Pieces.Rects[Index], Value);
        }
        DdUnmapSurface(Target);
    }
    DdFreeClipPieces(&Pieces);
    return hr;
}

/* The driver takes depth fills of its own resources only when it reports them */
static
BOOLEAN
DdDriverDepthFills(
    _In_ const DdSurface* Target)
{
    const DdHardware* Hardware = Target->Driver->Hardware;

    return Target->Resource != nullptr && (Target->Driver->HalCaps.dwCaps & DDCAPS_BLTDEPTHFILL) != 0 &&
           Hardware != nullptr && Hardware->DeviceFunctions.pfnDepthFill != nullptr;
}

/* Depth fills write the value untouched, without the conversion color fills get */
static
HRESULT
DdDepthFillSurface(
    _In_ DdSurface* Target,
    _In_ const RECT* To,
    _In_ DWORD Value)
{
    DdHardware* Hardware = Target->Driver->Hardware;
    D3DDDIARG_DEPTHFILL Fill = {};
    DdClipPieces Pieces;
    BYTE* Bits;
    DWORD Index;
    HRESULT hr = S_OK;

    if (!DdClipTarget(Target, To, &Pieces))
        return E_OUTOFMEMORY;

    if (DdDriverDepthFills(Target))
    {
        Fill.hResource = Target->Resource->DriverResource;
        Fill.Depth = Value;
        for (Index = 0; Index < Pieces.Count && SUCCEEDED(hr); Index++)
        {
            Fill.DstRect = Pieces.Rects[Index];
            hr = Hardware->DeviceFunctions.pfnDepthFill(Hardware->DriverDevice, &Fill);
        }
        if (FAILED(hr))
            DdTrace(L"UMD DepthFill failed 0x%08lX", hr);
    }
    else
    {
        hr = DdMapSurface(Target, FALSE, &Bits);
        if (SUCCEEDED(hr))
        {
            for (Index = 0; Index < Pieces.Count; Index++)
                DdFillRect(Target, Bits, &Pieces.Rects[Index], Value);
            DdUnmapSurface(Target);
        }
    }
    DdFreeClipPieces(&Pieces);
    return hr;
}

HRESULT
DdBlt(
    _In_ DdSurface* Target,
    _In_opt_ const RECT* TargetRect,
    _In_opt_ DdSurface* Source,
    _In_opt_ const RECT* SourceRect,
    _In_ DWORD Flags,
    _In_opt_ const DDBLTFX* Fx)
{
    BOOLEAN Primary = (Target->Caps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
    DdBlitJob Job = {};
    DdClipPieces Pieces;
    DWORD Rop = SRCCOPY;
    DWORD Index;
    HRESULT hr;

    if (Target == nullptr || (Flags & ~DdBltKnownFlags) != 0)
        return E_INVALIDARG;
    if ((Flags & (DDBLT_COLORFILL | DDBLT_DDFX | DDBLT_DDROPS | DDBLT_KEYDESTOVERRIDE | DDBLT_KEYSRCOVERRIDE | DDBLT_ROP |
                  DDBLT_ROTATIONANGLE | DDBLT_DEPTHFILL)) != 0 &&
        (Fx == nullptr || Fx->dwSize != sizeof(*Fx)))
    {
        return E_INVALIDARG;
    }

    /* Neither the driver nor the emulation layer offers destination color keys */
    if ((Flags & DDBLT_KEYDEST) != 0)
        return E_INVALIDARG;
    if ((Flags & DDBLT_KEYDESTOVERRIDE) != 0)
        return DDERR_NOCOLORKEYHW;

    /* Fills take no source; color fills skip Z buffers and depth fills want one */
    if ((Flags & DDBLT_COLORFILL) != 0)
    {
        if (Source != nullptr || (Target->Caps.dwCaps & DDSCAPS_ZBUFFER) != 0)
            return E_INVALIDARG;
    }
    else if ((Flags & DDBLT_DEPTHFILL) != 0)
    {
        if (Source != nullptr || (Target->Caps.dwCaps & DDSCAPS_ZBUFFER) == 0)
            return E_INVALIDARG;
        if (!DdDriverDepthFills(Target) && (Target->Driver->HelCaps.dwCaps & DDCAPS_BLTDEPTHFILL) == 0)
            return E_NOTIMPL;
    }

    Job.Target = Target;
    if (TargetRect != nullptr)
        Job.To = *TargetRect;
    else
        SetRect(&Job.To, 0, 0, Target->Shared->Width, Target->Shared->Height);
    if (!DdRectInside(&Job.To, Target))
        return DDERR_INVALIDRECT;

    if (DdSurfaceBusy(Target) || (Source != nullptr && DdSurfaceBusy(Source)))
        return DDERR_SURFACEBUSY;

    /* A clipper with neither a window nor a list has nothing to clip against */
    if (Target->Clipper != nullptr && Target->Clipper->Window == NULL && Target->Clipper->ClipList == nullptr)
        return DDERR_NOCLIPLIST;
    if (DdSurfaceUnbacked(Target) || (Source != nullptr && DdSurfaceUnbacked(Source)))
        return E_FAIL;
    DdTouchSurface(Target);

    if ((Flags & DDBLT_COLORFILL) != 0)
    {
        if (Primary)
            return DdPresentToPrimary(Target, &Job.To, nullptr, nullptr, TRUE, Fx->dwFillColor);
        return DdFillSurface(Target, &Job.To, Fx->dwFillColor, TRUE);
    }
    if ((Flags & DDBLT_DEPTHFILL) != 0)
        return DdDepthFillSurface(Target, &Job.To, Fx->dwFillDepth);

    if ((Flags & DDBLT_ROP) != 0)
    {
        Rop = Fx->dwROP;
        if (Rop == BLACKNESS || Rop == WHITENESS)
        {
            if (Primary)
                return DdPresentToPrimary(Target, &Job.To, nullptr, nullptr, TRUE, Rop == BLACKNESS ? 0 : 0xFFFFFFFF);
            return DdFillSurface(Target, &Job.To, Rop == BLACKNESS ? 0 : DdWhiteFor(Target), FALSE);
        }
        if (Rop != SRCCOPY)
            return DDERR_NORASTEROPHW;
    }

    if (Source == nullptr)
        return E_INVALIDARG;

    Job.Source = Source;
    if (SourceRect != nullptr)
        Job.From = *SourceRect;
    else
        SetRect(&Job.From, 0, 0, Source->Shared->Width, Source->Shared->Height);
    if (!DdRectInside(&Job.From, Source))
        return DDERR_INVALIDRECT;

    if (Primary)
        return DdPresentToPrimary(Target, &Job.To, Source, &Job.From, FALSE, 0);

    /* Copies between driver resources are the driver's work */
    if (Target->Resource != nullptr && Source->Resource != nullptr)
        return DdDriverCopy(Target, &Job.To, Source, &Job.From, Flags, Fx);
    if ((Target->Resource != nullptr) != (Source->Resource != nullptr) &&
        DdDriverTakesBusCopy(Flags, Fx, &Job.To, &Job.From))
    {
        return DdBusCopy(Target, &Job.To, Source, &Job.From, Flags, Fx);
    }

    if (Source->Shared->Format.dwRGBBitCount != Target->Shared->Format.dwRGBBitCount ||
        Target->Shared->Format.dwRGBBitCount < 8)
    {
        DD_TRACE_ONCE(L"blits that convert pixel formats are not implemented yet");
        return DDERR_UNSUPPORTED;
    }

    if ((Flags & DDBLT_KEYSRCOVERRIDE) != 0)
    {
        Job.Keyed = TRUE;
        Job.Key = Fx->ddckSrcColorkey;
    }
    else if ((Flags & DDBLT_KEYSRC) != 0)
    {
        if ((Source->KeyFlags & DDCKEY_SRCBLT) == 0)
            return DDERR_NOCOLORKEY;
        Job.Keyed = TRUE;
        Job.Key = Source->SourceKey;
    }
    Job.KeyMask = DdKeyMask(Source);

    if ((Flags & DDBLT_DDFX) != 0)
    {
        Job.MirrorX = (Fx->dwDDFX & DDBLTFX_MIRRORLEFTRIGHT) != 0;
        Job.MirrorY = (Fx->dwDDFX & DDBLTFX_MIRRORUPDOWN) != 0;
    }

    hr = DdMapSurface(Target, FALSE, &Job.TargetBits);
    if (FAILED(hr))
        return hr;
    if (Source == Target)
    {
        Job.SourceBits = Job.TargetBits;
    }
    else
    {
        hr = DdMapSurface(Source, TRUE, &Job.SourceBits);
        if (FAILED(hr))
        {
            DdUnmapSurface(Target);
            return hr;
        }
    }

    if (DdClipTarget(Target, &Job.To, &Pieces))
    {
        for (Index = 0; Index < Pieces.Count; Index++)
            DdCopyRect(Job, &Pieces.Rects[Index]);
        DdFreeClipPieces(&Pieces);
    }

    if (Source != Target)
        DdUnmapSurface(Source);
    DdUnmapSurface(Target);
    return S_OK;
}

HRESULT
DdBltFast(
    _In_ DdSurface* Target,
    _In_ DWORD X,
    _In_ DWORD Y,
    _In_ DdSurface* Source,
    _In_opt_ const RECT* SourceRect,
    _In_ DWORD Flags)
{
    DWORD BltFlags = DDBLT_WAIT;
    RECT From;
    RECT To;

    if ((Flags & ~(DDBLTFAST_SRCCOLORKEY | DDBLTFAST_DESTCOLORKEY | DDBLTFAST_WAIT | DDBLTFAST_DONOTWAIT)) != 0 ||
        (Flags & (DDBLTFAST_SRCCOLORKEY | DDBLTFAST_DESTCOLORKEY)) == (DDBLTFAST_SRCCOLORKEY | DDBLTFAST_DESTCOLORKEY))
    {
        return E_INVALIDARG;
    }

    if (Target->Clipper != nullptr)
        return DDERR_BLTFASTCANTCLIP;

    if (SourceRect != nullptr)
        From = *SourceRect;
    else
        SetRect(&From, 0, 0, Source->Shared->Width, Source->Shared->Height);
    SetRect(&To, X, Y, X + (From.right - From.left), Y + (From.bottom - From.top));

    if ((Flags & DDBLTFAST_SRCCOLORKEY) != 0)
        BltFlags |= DDBLT_KEYSRC;
    if ((Flags & DDBLTFAST_DESTCOLORKEY) != 0)
        BltFlags |= DDBLT_KEYDEST;

    return DdBlt(Target, &To, Source, &From, BltFlags, nullptr);
}
