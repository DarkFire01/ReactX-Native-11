/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Copies and fills between surfaces: UpdateSurface, StretchRect, GetRenderTargetData and ColorFill
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Depth formats an application may lock, stencil only included */
static
BOOLEAN
D9IsLockableDepthFormat(
    _In_ D3DFORMAT Format)
{
    return Format == D3DFMT_D16_LOCKABLE || Format == D3DFMT_D32F_LOCKABLE || Format == D3DFMT_D32_LOCKABLE ||
           Format == D3DFMT_S8_LOCKABLE;
}

static
BOOLEAN
D9IsAnyDepthFormat(
    _In_ D3DFORMAT Format)
{
    return D9IsLockableDepthFormat(Format) || D9IsHiddenDepth(Format);
}

/* The format the driver resource was made in, which hides depth formats the driver spells its own way */
static
D3DFORMAT
D9DriverFormatOf(
    _In_ D9Surface* Surface)
{
    UINT Index;
    D9Resource* Resource = Surface->CopyResource(&Index);

    return Resource != nullptr ? static_cast<D3DFORMAT>(Resource->Format) : Surface->Describe().Format;
}

/* The whole surface as a rectangle */
static
VOID
D9WholeRect(
    _In_ const D3DSURFACE_DESC* Desc,
    _Out_ RECT* Rect)
{
    SetRect(Rect, 0, 0, static_cast<LONG>(Desc->Width), static_cast<LONG>(Desc->Height));
}

/* A rectangle that starts left of or above the surface, or ends past it */
static
BOOLEAN
D9RectLeavesSurface(
    _In_ const RECT* Rect,
    _In_ const D3DSURFACE_DESC* Desc)
{
    return Rect->left < 0 || Rect->top < 0 || Rect->right > static_cast<LONG>(Desc->Width) ||
           Rect->bottom > static_cast<LONG>(Desc->Height);
}

/* Copies and fills only touch default pool surfaces: render targets, offscreen plains and depth buffers on their own,
   and texture levels made as render targets */
static
BOOLEAN
D9CanBlitSurface(
    _In_ D9Surface* Surface,
    _In_ BOOLEAN IsTarget)
{
    const D3DSURFACE_DESC& Desc = Surface->Describe();

    if (Desc.Pool != D3DPOOL_DEFAULT)
    {
        D9_TRACE_ONCE(L"StretchRect and ColorFill take default pool surfaces only");
        return FALSE;
    }
    if (Surface->OwnerTexture() == nullptr)
        return TRUE;
    if (IsTarget && (Desc.Usage & D3DUSAGE_RENDERTARGET) == 0 && Desc.Format != D3DFMT_A1)
    {
        D9_TRACE_ONCE(L"a texture level written by StretchRect or ColorFill must be a render target");
        return FALSE;
    }
    if ((Desc.Usage & D3DUSAGE_DEPTHSTENCIL) != 0)
    {
        D9_TRACE_ONCE(L"depth textures cannot be copied or filled");
        return FALSE;
    }
    return TRUE;
}

STDMETHODIMP
D9Device::UpdateSurface(
    _In_opt_ IDirect3DSurface9* SourceSurface,
    _In_opt_ const RECT* SourceRect,
    _In_opt_ IDirect3DSurface9* DestinationSurface,
    _In_opt_ const POINT* DestPoint)
{
    D9_DEVICE_LOCK();

    D9Surface* Source = static_cast<D9Surface*>(SourceSurface);
    D9Surface* Target = static_cast<D9Surface*>(DestinationSurface);
    D3DDDIARG_BLT Blt = {};
    RECT Area;
    RECT Place;
    BOOLEAN DepthConversion;
    BOOLEAN DepthTransfer;

    if (NotReset || Source == nullptr || Target == nullptr || Source->Owner() != this || Target->Owner() != this)
        return D3DERR_INVALIDCALL;
    AcquireWriteAccess(Target);
    if (Source->IsBusy() || Target->IsBusy())
    {
        D9_TRACE_ONCE(L"UpdateSurface: a surface is locked");
        return D3DERR_INVALIDCALL;
    }

    const D3DSURFACE_DESC& From = Source->Describe();
    const D3DSURFACE_DESC& To = Target->Describe();
    BOOLEAN FromStencil = D9HasStencil(From.Format);
    BOOLEAN ToStencil = D9HasStencil(To.Format);
    BOOLEAN FromMemoryDepth = D9IsLockableDepthFormat(From.Format) && From.Pool == D3DPOOL_SYSTEMMEM;
    BOOLEAN ToMemoryDepth = D9IsLockableDepthFormat(To.Format) && To.Pool == D3DPOOL_SYSTEMMEM;
    BOOLEAN FromHidden = D9IsHiddenDepth(From.Format);
    BOOLEAN ToHidden = D9IsHiddenDepth(To.Format);

    /* Extended devices move depth between a lockable system memory copy and a hidden format, stencil matching */
    DepthConversion = IsEx && ((FromMemoryDepth && !FromStencil && ToHidden) ||
                               (FromHidden && ToMemoryDepth && !ToStencil) ||
                               (FromMemoryDepth && FromStencil && ToHidden && ToStencil) ||
                               (FromHidden && FromStencil && ToMemoryDepth && ToStencil));
    DepthTransfer = IsEx &&
                    (To.Pool == D3DPOOL_SYSTEMMEM ? ToMemoryDepth : (To.Pool == D3DPOOL_DEFAULT && ToHidden)) &&
                    (From.Pool == D3DPOOL_SYSTEMMEM ? FromMemoryDepth : (From.Pool == D3DPOOL_DEFAULT && FromHidden));

    if (!DepthConversion)
    {
        if (From.Format != To.Format)
        {
            D9_TRACE_ONCE(L"UpdateSurface: the formats differ");
            return D3DERR_INVALIDCALL;
        }
        if (D9IsAnyDepthFormat(From.Format) && D9BytesPerPixel(From.Format) != 0)
        {
            D9_TRACE_ONCE(L"UpdateSurface: depth surfaces need a lockable system memory side");
            return D3DERR_INVALIDCALL;
        }
    }
    if ((To.Pool != D3DPOOL_DEFAULT || From.Pool != D3DPOOL_SYSTEMMEM) && !DepthTransfer)
    {
        D9_TRACE_ONCE(L"UpdateSurface: copies go from system memory to the default pool");
        return D3DERR_INVALIDCALL;
    }
    if (From.MultiSampleType != D3DMULTISAMPLE_NONE || To.MultiSampleType != D3DMULTISAMPLE_NONE ||
        (From.Usage & D3DUSAGE_RENDERTARGET) != 0)
    {
        D9_TRACE_ONCE(L"UpdateSurface: multisampled surfaces and source render targets are not copied");
        return D3DERR_INVALIDCALL;
    }

    if (SourceRect != nullptr)
        Area = *SourceRect;
    else
        D9WholeRect(&From, &Area);
    Place.left = DestPoint != nullptr ? DestPoint->x : 0;
    Place.top = DestPoint != nullptr ? DestPoint->y : 0;
    Place.right = Place.left + (Area.right - Area.left);
    Place.bottom = Place.top + (Area.bottom - Area.top);
    if (!D9IsValidRect(From.Format, From.Width, From.Height, &Area) ||
        !D9IsValidRect(From.Format, To.Width, To.Height, &Place))
    {
        D9_TRACE_ONCE(L"UpdateSurface: the rectangle does not fit");
        return D3DERR_INVALIDCALL;
    }

    Blt.hSrcResource = Source->CopyResource(&Blt.SrcSubResourceIndex)->DriverResource;
    Blt.SrcRect = Area;
    Blt.hDstResource = Target->CopyResource(&Blt.DstSubResourceIndex)->DriverResource;
    Blt.DstRect = Place;
    return DriverBlt(&Blt);
}

STDMETHODIMP
D9Device::GetRenderTargetData(
    _In_ IDirect3DSurface9* RenderTarget,
    _In_ IDirect3DSurface9* DestSurface)
{
    D9_DEVICE_LOCK();

    D9Surface* Source = static_cast<D9Surface*>(RenderTarget);
    D9Surface* Target = static_cast<D9Surface*>(DestSurface);
    D3DDDIARG_BLT Blt = {};
    D3DLOCKED_BOX Box;
    D3DLOCKED_RECT Locked;
    BOOLEAN Asynchronous;
    D9Resource* Memory;
    UINT Index;

    if (NotReset || Target == nullptr || Target->Owner() != this)
        return D3DERR_INVALIDCALL;
    const D3DSURFACE_DESC& To = Target->Describe();
    if (To.Pool != D3DPOOL_SYSTEMMEM)
    {
        D9_TRACE_ONCE(L"GetRenderTargetData: the destination must be in system memory");
        return D3DERR_INVALIDCALL;
    }
    if (Source == nullptr || Source->Owner() != this)
        return D3DERR_INVALIDCALL;
    const D3DSURFACE_DESC& From = Source->Describe();
    if ((From.Usage & D3DUSAGE_RENDERTARGET) == 0 || From.Pool != D3DPOOL_DEFAULT || Source->IsBusy() ||
        Target->IsBusy())
    {
        D9_TRACE_ONCE(L"GetRenderTargetData: the source must be an unlocked default pool render target");
        return D3DERR_INVALIDCALL;
    }
    if (From.Format != To.Format || From.Width != To.Width || From.Height != To.Height ||
        From.MultiSampleType != D3DMULTISAMPLE_NONE)
    {
        D9_TRACE_ONCE(L"GetRenderTargetData: the surfaces do not match");
        return D3DERR_INVALIDCALL;
    }

    /* The driver hears of the system memory copy being touched before the copy lands in it */
    Memory = Target->CopyResource(&Index);
    if (Target->OwnerTexture() != nullptr)
    {
        if (SUCCEEDED(LockDriverSubresource(Memory, Index, nullptr, FALSE, 0, &Box, &Asynchronous)))
            UnlockDriverSubresource(Memory, Index, Asynchronous);
    }
    else if (SUCCEEDED(LockDriverSurface(Memory, nullptr, 0, &Locked, &Asynchronous)))
    {
        UnlockDriverSurface(Memory, Asynchronous);
    }

    Blt.hSrcResource = Source->CopyResource(&Blt.SrcSubResourceIndex)->DriverResource;
    D9WholeRect(&From, &Blt.SrcRect);
    Blt.hDstResource = Memory->DriverResource;
    Blt.DstSubResourceIndex = Index;
    Blt.DstRect = Blt.SrcRect;
    return DriverBlt(&Blt);
}

/* Depth buffers only copy whole to whole between two of their own, outside a scene */
static
HRESULT
D9CheckDepthStretch(
    _In_ D9Surface* Source,
    _In_ D9Surface* Target,
    _In_ const RECT* Area,
    _In_ const RECT* Place,
    _In_ BOOLEAN Resized,
    _In_ BOOLEAN InScene)
{
    const D3DSURFACE_DESC& From = Source->Describe();
    const D3DSURFACE_DESC& To = Target->Describe();
    BOOLEAN FromDepth = Source->OwnerTexture() == nullptr && (From.Usage & D3DUSAGE_DEPTHSTENCIL) != 0;
    BOOLEAN ToDepth = Target->OwnerTexture() == nullptr && (To.Usage & D3DUSAGE_DEPTHSTENCIL) != 0;
    RECT Whole;

    if (!FromDepth && !ToDepth)
        return S_OK;
    if (InScene || !FromDepth || !ToDepth || D9DriverFormatOf(Source) != D9DriverFormatOf(Target) || Resized)
        return D3DERR_INVALIDCALL;
    D9WholeRect(&From, &Whole);
    if (!EqualRect(Area, &Whole))
        return D3DERR_INVALIDCALL;
    D9WholeRect(&To, &Whole);
    if (!EqualRect(Place, &Whole))
        return D3DERR_INVALIDCALL;
    if (Source->IsDiscardable() || Target->IsDiscardable())
        return D3DERR_INVALIDCALL;
    return S_OK;
}

/* The filter caps a stretch or a shrink needs */
static
BOOLEAN
D9FilterAllowed(
    _In_ DWORD Caps,
    _In_ D3DTEXTUREFILTERTYPE Filter,
    _In_ BOOLEAN Stretches,
    _In_ BOOLEAN Shrinks)
{
    DWORD Magnify;
    DWORD Minify;

    if (Filter == D3DTEXF_POINT)
    {
        Magnify = D3DPTFILTERCAPS_MAGFPOINT;
        Minify = D3DPTFILTERCAPS_MINFPOINT;
    }
    else if (Filter == D3DTEXF_LINEAR)
    {
        Magnify = D3DPTFILTERCAPS_MAGFLINEAR;
        Minify = D3DPTFILTERCAPS_MINFLINEAR;
    }
    else
    {
        return TRUE;
    }
    if (Stretches && (Caps & Magnify) == 0)
        return FALSE;
    if (Shrinks && (Caps & Minify) == 0)
        return FALSE;
    return (Caps & (Magnify | Minify)) != 0;
}

STDMETHODIMP
D9Device::StretchRect(
    _In_opt_ IDirect3DSurface9* SourceSurface,
    _In_opt_ const RECT* SourceRect,
    _In_opt_ IDirect3DSurface9* DestSurface,
    _In_opt_ const RECT* DestRect,
    _In_ D3DTEXTUREFILTERTYPE Filter)
{
    D9_DEVICE_LOCK();

    D9Surface* Source = static_cast<D9Surface*>(SourceSurface);
    D9Surface* Target = static_cast<D9Surface*>(DestSurface);
    D3DDDIARG_BLT Blt = {};
    BOOLEAN Matching;
    BOOLEAN Stretches;
    BOOLEAN Shrinks;
    RECT Area;
    RECT Place;

    if (NotReset || Source == nullptr || Filter > D3DTEXF_LINEAR || Source->Owner() != this || Target == nullptr ||
        Target->Owner() != this)
    {
        return D3DERR_INVALIDCALL;
    }
    AcquireWriteAccess(Target);
    if (Source->IsBusy() || Target->IsBusy() || Source == Target)
    {
        D9_TRACE_ONCE(L"StretchRect: a surface is locked or both are the same");
        return D3DERR_INVALIDCALL;
    }

    const D3DSURFACE_DESC& From = Source->Describe();
    const D3DSURFACE_DESC& To = Target->Describe();
    D3DFORMAT FromFormat = D9DriverFormatOf(Source);
    D3DFORMAT ToFormat = D9DriverFormatOf(Target);

    /* Extended devices copy any two matching default pool surfaces whole without further checks */
    Matching = IsEx && FromFormat == ToFormat && SourceRect == nullptr && DestRect == nullptr &&
               Filter == D3DTEXF_NONE && From.Width == To.Width && From.Height == To.Height &&
               From.Pool == D3DPOOL_DEFAULT && To.Pool == D3DPOOL_DEFAULT;
    if (!Matching)
    {
        if (!D9CanBlitSurface(Source, FALSE) || !D9CanBlitSurface(Target, TRUE))
            return D3DERR_INVALIDCALL;
        if (FromFormat != ToFormat &&
            FAILED(Enum->CheckDeviceFormatConversion(Adapter->Ordinal, DeviceType, FromFormat, ToFormat)))
        {
            D9_TRACE_ONCE(L"StretchRect: format %u does not convert to %u", FromFormat, ToFormat);
            return D3DERR_INVALIDCALL;
        }
    }

    if (SourceRect != nullptr && !Matching)
    {
        if (D9RectLeavesSurface(SourceRect, &From))
            return D3DERR_INVALIDCALL;
        Area = *SourceRect;
    }
    else
    {
        D9WholeRect(&From, &Area);
    }
    if (DestRect != nullptr)
    {
        if (D9RectLeavesSurface(DestRect, &To))
            return D3DERR_INVALIDCALL;
        Place = *DestRect;
    }
    else
    {
        D9WholeRect(&To, &Place);
    }
    if (!D9IsValidRect(FromFormat, From.Width, From.Height, &Area) ||
        !D9IsValidRect(ToFormat, To.Width, To.Height, &Place))
    {
        D9_TRACE_ONCE(L"StretchRect: a rectangle does not fit its surface");
        return D3DERR_INVALIDCALL;
    }

    Stretches = Place.right - Place.left > Area.right - Area.left || Place.bottom - Place.top > Area.bottom - Area.top;
    Shrinks = Place.right - Place.left < Area.right - Area.left || Place.bottom - Place.top < Area.bottom - Area.top;
    if (!D9FilterAllowed(Caps.StretchRectFilterCaps, Filter, Stretches, Shrinks))
    {
        D9_TRACE_ONCE(L"StretchRect: the driver cannot filter with %u", Filter);
        return D3DERR_INVALIDCALL;
    }

    if (Matching)
    {
        BOOLEAN FromDepth = Source->OwnerTexture() == nullptr && (From.Usage & D3DUSAGE_DEPTHSTENCIL) != 0;
        BOOLEAN ToDepth = Target->OwnerTexture() == nullptr && (To.Usage & D3DUSAGE_DEPTHSTENCIL) != 0;

        if ((FromDepth || ToDepth) && (Source->IsDiscardable() || Target->IsDiscardable()))
            return D3DERR_INVALIDCALL;
    }
    else if (FAILED(D9CheckDepthStretch(Source, Target, &Area, &Place, Stretches || Shrinks, InScene)))
    {
        D9_TRACE_ONCE(L"StretchRect: depth buffers copy whole, unscaled, to another depth buffer");
        return D3DERR_INVALIDCALL;
    }

    if ((Stretches || Shrinks) && (D9IsDxtFormat(FromFormat) || D9IsDxtFormat(ToFormat)))
        return D3DERR_INVALIDCALL;

    /* An offscreen plain destination takes an unscaled copy of another offscreen plain only */
    if (!Matching && Target->OwnerTexture() == nullptr && (To.Usage & D3DUSAGE_RENDERTARGET) == 0)
    {
        if (Source->OwnerTexture() != nullptr || (From.Usage & D3DUSAGE_RENDERTARGET) != 0 || Stretches || Shrinks)
        {
            D9_TRACE_ONCE(L"StretchRect: an offscreen plain destination needs an unscaled offscreen plain source");
            return D3DERR_INVALIDCALL;
        }
    }

    Blt.hSrcResource = Source->CopyResource(&Blt.SrcSubResourceIndex)->DriverResource;
    Blt.SrcRect = Area;
    Blt.hDstResource = Target->CopyResource(&Blt.DstSubResourceIndex)->DriverResource;
    Blt.DstRect = Place;
    Blt.Flags.Point = Filter == D3DTEXF_POINT;
    Blt.Flags.Linear = Filter == D3DTEXF_LINEAR;
    return DriverBlt(&Blt);
}

STDMETHODIMP
D9Device::ColorFill(
    _In_opt_ IDirect3DSurface9* Surface,
    _In_opt_ const RECT* Rect,
    _In_ D3DCOLOR Color)
{
    D9_DEVICE_LOCK();

    D9Surface* Target = static_cast<D9Surface*>(Surface);
    D3DDDIARG_COLORFILL Fill = {};
    RECT Area;

    if (NotReset || Target == nullptr || Target->Owner() != this)
        return D3DERR_INVALIDCALL;
    AcquireWriteAccess(Target);
    if (Target->IsBusy())
    {
        D9_TRACE_ONCE(L"ColorFill: the surface is locked");
        return D3DERR_INVALIDCALL;
    }
    if (!D9CanBlitSurface(Target, TRUE))
        return D3DERR_INVALIDCALL;

    const D3DSURFACE_DESC& Desc = Target->Describe();
    if (Rect != nullptr)
    {
        if (D9RectLeavesSurface(Rect, &Desc))
            return D3DERR_INVALIDCALL;
        Area = *Rect;
    }
    else
    {
        D9WholeRect(&Desc, &Area);
    }
    if (!D9IsValidRect(Desc.Format, Desc.Width, Desc.Height, &Area))
        return D3DERR_INVALIDCALL;
    if (D9IsDxtFormat(Desc.Format) && ((Area.left & 3) != 0 || (Area.top & 3) != 0))
        return D3DERR_INVALIDCALL;
    if ((Desc.Usage & D3DUSAGE_DEPTHSTENCIL) != 0)
    {
        D9_TRACE_ONCE(L"ColorFill: depth buffers are not filled");
        return D3DERR_INVALIDCALL;
    }

    Fill.hResource = Target->CopyResource(&Fill.SubResourceIndex)->DriverResource;
    Fill.DstRect = Area;
    Fill.Color = Color;
    return DriverColorFill(&Fill);
}
