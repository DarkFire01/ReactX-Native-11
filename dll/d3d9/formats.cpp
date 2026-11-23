/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The format, depth stencil, multisample and conversion checks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

/* Usage bits CheckDeviceFormat refuses outright */
static constexpr DWORD D9UsageRejected = 0xEF400004;

/* Formats with a texture of several planes or a raw buffer behind them */
static constexpr D3DFORMAT D9FormatBinaryBuffer = static_cast<D3DFORMAT>(199);

/* Display, then display with 3D acceleration, as one requirement */
static constexpr UINT D9DesktopOps = FORMATOP_DISPLAYMODE | FORMATOP_3DACCELERATION;

/* Bytes one pixel of a fixed size format takes; block compressed formats report minus their block size */
LONG
D9BytesPerPixel(
    _In_ D3DFORMAT Format)
{
    switch (static_cast<DWORD>(Format))
    {
    case D3DFMT_R8G8B8:
        return 3;
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_A2B10G10R10:
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
    case D3DFMT_G16R16:
    case D3DFMT_A2R10G10B10:
    case D3DFMT_X8L8V8U8:
    case D3DFMT_Q8W8V8U8:
    case D3DFMT_V16U16:
    case D3DFMT_A2W10V10U10:
    case D3DFMT_D32:
    case D3DFMT_D24S8:
    case D3DFMT_D24X8:
    case D3DFMT_D24X4S4:
    case 74:
    case 76:
    case 78:
    case D3DFMT_D32F_LOCKABLE:
    case D3DFMT_D24FS8:
    case D3DFMT_D32_LOCKABLE:
    case D3DFMT_G16R16F:
    case D3DFMT_R32F:
        return 4;
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_A8R3G3B2:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_A8P8:
    case D3DFMT_A8L8:
    case D3DFMT_V8U8:
    case D3DFMT_L6V5U5:
    case D3DFMT_D16_LOCKABLE:
    case 72:
    case D3DFMT_D15S1:
    case D3DFMT_D16:
    case D3DFMT_L16:
    case D3DFMT_R16F:
    case D3DFMT_CxV8U8:
    case D3DFMT_UYVY:
    case D3DFMT_YUY2:
    case D3DFMT_R8G8_B8G8:
    case D3DFMT_G8R8_G8B8:
        return 2;
    case D3DFMT_R3G3B2:
    case D3DFMT_A8:
    case D3DFMT_P8:
    case D3DFMT_L8:
    case D3DFMT_A4L4:
    case D3DFMT_S8_LOCKABLE:
    case D3DFMT_A1:
    case D9FormatBinaryBuffer:
        return 1;
    case D3DFMT_A16B16G16R16:
    case D3DFMT_Q16W16V16U16:
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_G32R32F:
    case D3DFMT_MULTI2_ARGB8:
        return 8;
    case D3DFMT_A32B32G32R32F:
        return 16;
    case D3DFMT_DXT1:
        return -8;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        return -16;
    default:
        if (static_cast<DWORD>(Format) >= 150 && static_cast<DWORD>(Format) <= 175)
            return 1;
        return 0;
    }
}

/* Depth formats applications may ask for under a test the driver lists in reversed component order */
static
BOOLEAN
D9IsEnumerableDepth(
    _In_ D3DFORMAT Format)
{
    static constexpr DWORD Listed = 0xF6AB;
    DWORD Offset = static_cast<DWORD>(Format) - D3DFMT_D16_LOCKABLE;

    if (Offset <= 15 && ((Listed >> Offset) & 1) != 0)
        return TRUE;
    return D9BytesPerPixel(Format) == 0;
}

static
D3DFORMAT
D9OpaqueTwin(
    _In_ D3DFORMAT Format)
{
    switch (Format)
    {
    case D3DFMT_A8R8G8B8:
        return D3DFMT_X8R8G8B8;
    case D3DFMT_A1R5G5B5:
        return D3DFMT_X1R5G5B5;
    case D3DFMT_A4R4G4B4:
        return D3DFMT_X4R4G4B4;
    case D3DFMT_A8B8G8R8:
        return D3DFMT_X8B8G8R8;
    default:
        return Format;
    }
}

/* Formats a swap chain may use for its back buffers */
BOOLEAN
D9IsBackBufferFormat(
    _In_ D3DFORMAT Format,
    _In_ BOOLEAN IsEx)
{
    switch (Format)
    {
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_A2B10G10R10_XR_BIAS:
        return IsEx;
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A2R10G10B10:
        return TRUE;
    default:
        return FALSE;
    }
}

/* Desktop formats that cannot back a windowed swap chain */
static
BOOLEAN
D9IsExtendedDesktopFormat(
    _In_ D3DFORMAT Format,
    _In_ BOOLEAN IsEx)
{
    if (IsEx)
        return Format == D3DFMT_A16B16G16R16F || Format == D3DFMT_A2R10G10B10;
    return Format == D3DFMT_A2R10G10B10;
}

/* Whether any listed entry of Format has every operation in Ops */
BOOLEAN
D9DriverHasOps(
    _In_ const D9Adapter* Adapter,
    _In_ D3DFORMAT Format,
    _In_ UINT Ops)
{
    for (UINT Index = 0; Index < Adapter->FormatCount; Index++)
    {
        const FORMATOP* Op = &Adapter->Formats[Index];

        if (Op->Format == static_cast<D3DDDIFORMAT>(Format) && (Ops & ~Op->Operations) == 0)
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
D9DriverLists(
    _In_ const D9Adapter* Adapter,
    _In_ D3DDDIFORMAT Format)
{
    for (UINT Index = 0; Index < Adapter->FormatCount; Index++)
    {
        if (Adapter->Formats[Index].Format == Format)
            return TRUE;
    }
    return FALSE;
}

/* Asks about a depth format in the component order the driver listed it */
static
D3DFORMAT
D9DriverDepthFormat(
    _In_ const D9Adapter* Adapter,
    _In_ D3DFORMAT Format)
{
    switch (Format)
    {
    case D3DFMT_D15S1:
        return D9DriverLists(Adapter, D3DDDIFMT_S1D15) ? static_cast<D3DFORMAT>(D3DDDIFMT_S1D15) : Format;
    case D3DFMT_D24S8:
        return D9DriverLists(Adapter, D3DDDIFMT_S8D24) ? static_cast<D3DFORMAT>(D3DDDIFMT_S8D24) : Format;
    case D3DFMT_D24X8:
        return D9DriverLists(Adapter, D3DDDIFMT_X8D24) ? static_cast<D3DFORMAT>(D3DDDIFMT_X8D24) : Format;
    case D3DFMT_D24X4S4:
        return D9DriverLists(Adapter, D3DDDIFMT_X4S4D24) ? static_cast<D3DFORMAT>(D3DDDIFMT_X4S4D24) : Format;
    case D3DFMT_D16:
        return D9DriverLists(Adapter, D3DDDIFMT_D16) ? D3DFMT_D16 : D3DFMT_D16_LOCKABLE;
    default:
        return Format;
    }
}

/* The format table a device type answers from; only the hardware device has one so far */
HRESULT
D9Enum::DeviceTable(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _Outptr_ const D9Adapter** Table)
{
    *Table = &Adapters[Adapter];
    switch (DeviceType)
    {
    case D3DDEVTYPE_HAL:
        return SoftwareOnly ? D3DERR_NOTAVAILABLE : S_OK;
    case D3DDEVTYPE_REF:
    case D3DDEVTYPE_NULLREF:
        D9_TRACE_ONCE(L"the reference rasterizer is not implemented yet");
        return D3DERR_NOTAVAILABLE;
    case D3DDEVTYPE_SW:
        return D3DERR_NOTAVAILABLE;
    default:
        return D3DERR_INVALIDDEVICE;
    }
}

STDMETHODIMP
D9Enum::CheckDeviceType(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ D3DFORMAT DisplayFormat,
    _In_ D3DFORMAT BackBufferFormat,
    _In_ BOOL Windowed)
{
    D9LockScope Scope(&Lock);
    const D9Adapter* Table;
    BOOLEAN NotDisplayable = FALSE;
    D3DFORMAT AlphaTwin;
    HRESULT hr;

    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"CheckDeviceType: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }
    if (DeviceType < D3DDEVTYPE_HAL || DeviceType > D3DDEVTYPE_SW)
    {
        D9_TRACE_ONCE(L"CheckDeviceType: invalid device type %u", DeviceType);
        return D3DERR_INVALIDCALL;
    }

    if (BackBufferFormat == D3DFMT_UNKNOWN && Windowed)
        BackBufferFormat = DisplayFormat;
    if (DisplayFormat == D3DFMT_UNKNOWN || BackBufferFormat == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;

    if (!D9IsBackBufferFormat(BackBufferFormat, IsEx) || !D9IsDesktopFormat(DisplayFormat, IsEx) ||
        (Windowed && D9IsExtendedDesktopFormat(DisplayFormat, IsEx)))
    {
        return D3DERR_NOTAVAILABLE;
    }

    hr = DeviceTable(Adapter, DeviceType, &Table);
    if (FAILED(hr))
        return hr;

    if (!D9DriverHasOps(Table, DisplayFormat, D9DesktopOps))
    {
        if (!Windowed)
            return D3DERR_NOTAVAILABLE;
        NotDisplayable = TRUE;
        DisplayFormat = D3DFMT_UNKNOWN;
    }
    else if (DisplayFormat == BackBufferFormat)
    {
        return D9DriverHasOps(Table, DisplayFormat, FORMATOP_SAME_FORMAT_RENDERTARGET) ? S_OK : D3DERR_NOTAVAILABLE;
    }
    else
    {
        AlphaTwin = DisplayFormat == D3DFMT_X8R8G8B8 ? D3DFMT_A8R8G8B8 :
                    DisplayFormat == D3DFMT_X1R5G5B5 ? D3DFMT_A1R5G5B5 : D3DFMT_UNKNOWN;
        if (AlphaTwin != D3DFMT_UNKNOWN && AlphaTwin == BackBufferFormat)
        {
            return D9DriverHasOps(Table, AlphaTwin, FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET) ? S_OK
                                                                                                  : D3DERR_NOTAVAILABLE;
        }
        if (!Windowed)
            return D3DERR_NOTAVAILABLE;
    }

    /* A windowed back buffer only has to be rendered to and converted for the desktop */
    if (!D9IsBackBufferFormat(BackBufferFormat, IsEx) ||
        !D9DriverHasOps(Table, BackBufferFormat, FORMATOP_OFFSCREEN_RENDERTARGET))
    {
        return D3DERR_NOTAVAILABLE;
    }
    if ((D9DriverHasOps(Table, BackBufferFormat, FORMATOP_CONVERT_TO_ARGB) &&
         D9DriverHasOps(Table, DisplayFormat, FORMATOP_MEMBEROFGROUP_ARGB)) ||
        NotDisplayable)
    {
        return S_OK;
    }
    return D3DERR_NOTAVAILABLE;
}

/* The operations a resource type and usage need from the format */
static
UINT
D9RequiredOps(
    _In_ DWORD Usage,
    _In_ D3DRESOURCETYPE ResourceType)
{
    UINT Ops = 0;

    switch (ResourceType)
    {
    case D3DRTYPE_VOLUME:
    case D3DRTYPE_VOLUMETEXTURE:
        Ops = FORMATOP_VOLUMETEXTURE;
        break;
    case D3DRTYPE_TEXTURE:
        Ops = FORMATOP_TEXTURE;
        break;
    case D3DRTYPE_CUBETEXTURE:
        Ops = FORMATOP_CUBETEXTURE;
        break;
    default:
        break;
    }

    if ((Usage & D3DUSAGE_QUERY_LEGACYBUMPMAP) != 0)
        Ops |= FORMATOP_BUMPMAP;
    if ((Usage & D3DUSAGE_DMAP) != 0)
        Ops |= FORMATOP_DMAP;
    if ((Usage & D3DUSAGE_QUERY_SRGBREAD) != 0)
        Ops |= FORMATOP_SRGBREAD;
    if ((Usage & D3DUSAGE_QUERY_SRGBWRITE) != 0)
        Ops |= FORMATOP_SRGBWRITE;
    if ((Usage & D3DUSAGE_QUERY_VERTEXTEXTURE) != 0)
        Ops |= FORMATOP_VERTEXTEXTURE;
    if ((Usage & D3DUSAGE_DEPTHSTENCIL) != 0)
        Ops |= FORMATOP_ZSTENCIL;
    if (ResourceType == D3DRTYPE_SURFACE && Usage == 0)
        Ops |= FORMATOP_OFFSCREENPLAIN;
    return Ops;
}

/* Operations whose presence means the format cannot do what the usage asks */
static
UINT
D9ExcludedOps(
    _In_ DWORD Usage)
{
    UINT Ops = 0;

    if ((Usage & D3DUSAGE_QUERY_FILTER) != 0)
        Ops |= FORMATOP_NOFILTER;
    if ((Usage & D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING) != 0)
        Ops |= FORMATOP_NOALPHABLEND;
    if ((Usage & D3DUSAGE_QUERY_WRAPANDMIP) != 0)
        Ops |= FORMATOP_NOTEXCOORDWRAPNORMIP;
    return Ops;
}

STDMETHODIMP
D9Enum::CheckDeviceFormat(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ D3DFORMAT AdapterFormat,
    _In_ DWORD Usage,
    _In_ D3DRESOURCETYPE ResourceType,
    _In_ D3DFORMAT CheckFormat)
{
    D9LockScope Scope(&Lock);
    BOOLEAN AutoGen = (Usage & D3DUSAGE_AUTOGENMIPMAP) != 0;
    BOOLEAN IsTexture;
    BOOLEAN MissingOnlyAutoGen = FALSE;
    const D9Adapter* Table;
    UINT Required;
    UINT Excluded;
    HRESULT hr;

    if (Adapter >= AdapterCount)
    {
        if (!(AdapterCount == 0 && DeviceType == D3DDEVTYPE_NULLREF && Adapter == 0))
        {
            D9_TRACE_ONCE(L"CheckDeviceFormat: adapter %u does not exist", Adapter);
            return D3DERR_INVALIDCALL;
        }
    }
    else if (DeviceType < D3DDEVTYPE_HAL || DeviceType > D3DDEVTYPE_NULLREF)
    {
        D9_TRACE_ONCE(L"CheckDeviceFormat: invalid device type %u", DeviceType);
        return D3DERR_INVALIDCALL;
    }

    if ((AdapterFormat == D3DFMT_UNKNOWN && DeviceType != D3DDEVTYPE_NULLREF) || CheckFormat == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    if (!D9IsDesktopFormat(AdapterFormat, IsEx) && DeviceType != D3DDEVTYPE_NULLREF)
        return D3DERR_NOTAVAILABLE;

    IsTexture = ResourceType == D3DRTYPE_TEXTURE || ResourceType == D3DRTYPE_VOLUMETEXTURE ||
                ResourceType == D3DRTYPE_CUBETEXTURE;
    if (ResourceType == D3DRTYPE_SURFACE && Usage != 0 &&
        (Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) == 0)
    {
        D9_TRACE_ONCE(L"CheckDeviceFormat: surfaces take render target, depth stencil or no usage");
        return D3DERR_INVALIDCALL;
    }
    if ((Usage & D3DUSAGE_DEPTHSTENCIL) != 0 && (!D9IsEnumerableDepth(CheckFormat) || AutoGen))
    {
        D9_TRACE_ONCE(L"CheckDeviceFormat: format %u cannot be a depth stencil this way", CheckFormat);
        return D3DERR_INVALIDCALL;
    }

    switch (ResourceType)
    {
    case D3DRTYPE_SURFACE:
    case D3DRTYPE_TEXTURE:
    case D3DRTYPE_CUBETEXTURE:
        break;
    case D3DRTYPE_VOLUME:
    case D3DRTYPE_VOLUMETEXTURE:
        if ((Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) != 0 || AutoGen)
        {
            D9_TRACE_ONCE(L"CheckDeviceFormat: volumes cannot be render targets, depth stencils or mipmapped");
            return D3DERR_INVALIDCALL;
        }
        break;
    default:
        D9_TRACE_ONCE(L"CheckDeviceFormat: invalid resource type %u", ResourceType);
        return D3DERR_INVALIDCALL;
    }

    if ((Usage & D9UsageRejected) != 0 || ((Usage & D3DUSAGE_QUERY_VERTEXTEXTURE) != 0 && !IsTexture) ||
        (CheckFormat == D9FormatBinaryBuffer && (ResourceType != D3DRTYPE_SURFACE || Usage != 0)))
    {
        D9_TRACE_ONCE(L"CheckDeviceFormat: invalid usage 0x%08lX", Usage);
        return D3DERR_INVALIDCALL;
    }

    hr = DeviceTable(Adapter, DeviceType, &Table);
    if (FAILED(hr))
        return hr;

    if (CheckFormat == D3DFMT_MULTI2_ARGB8 && AutoGen)
        return D3DERR_INVALIDCALL;
    if ((Usage & D3DUSAGE_DYNAMIC) != 0 && IsTexture &&
        ((Table->DriverCaps.Caps2 & D3DCAPS2_DYNAMICTEXTURES) == 0 ||
         (Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) != 0))
    {
        return D3DERR_INVALIDCALL;
    }
    if ((Usage & D3DUSAGE_DMAP) != 0 &&
        ((Table->DriverCaps.DevCaps2 & (D3DDEVCAPS2_DMAPNPATCH | D3DDEVCAPS2_PRESAMPLEDDMAPNPATCH)) == 0 ||
         ResourceType != D3DRTYPE_TEXTURE))
    {
        return D3DERR_INVALIDCALL;
    }

    /* On WDDM an adapter format the driver cannot scan out still lets windowed formats be asked about */
    if (DeviceType != D3DDEVTYPE_NULLREF && !D9DriverHasOps(Table, AdapterFormat, D9DesktopOps))
    {
        if (!D9State.HasWddmDriver && DeviceType != D3DDEVTYPE_REF && DeviceType != D3DDEVTYPE_SW)
            return D3DERR_NOTAVAILABLE;
        AdapterFormat = D3DFMT_UNKNOWN;
    }

    Required = D9RequiredOps(Usage, ResourceType);
    if (AutoGen)
    {
        DWORD MipCap = ResourceType == D3DRTYPE_TEXTURE ? D3DPTEXTURECAPS_MIPMAP :
                       ResourceType == D3DRTYPE_CUBETEXTURE ? D3DPTEXTURECAPS_MIPCUBEMAP : 0;

        if (MipCap != 0 && (Table->DriverCaps.TextureCaps & MipCap) == 0)
            return D3DERR_NOTAVAILABLE;
        Required |= FORMATOP_AUTOGENMIPMAP;
    }

    CheckFormat = D9DriverDepthFormat(Table, CheckFormat);
    if ((Usage & D3DUSAGE_RENDERTARGET) != 0)
    {
        if (AdapterFormat == CheckFormat)
            Required |= FORMATOP_SAME_FORMAT_RENDERTARGET;
        else if (D9OpaqueTwin(CheckFormat) != CheckFormat && D9OpaqueTwin(CheckFormat) == AdapterFormat)
            Required |= FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET;
        else
            Required |= FORMATOP_OFFSCREEN_RENDERTARGET;
    }

    Excluded = D9ExcludedOps(Usage);
    for (UINT Index = 0; Index < Table->FormatCount; Index++)
    {
        const FORMATOP* Op = &Table->Formats[Index];

        if (Op->Format != static_cast<D3DDDIFORMAT>(CheckFormat) || (Op->Operations & Excluded) != 0)
            continue;
        if ((Op->Operations & Required) == Required)
            return S_OK;
        if (((Op->Operations & Required) | FORMATOP_AUTOGENMIPMAP) == Required)
            MissingOnlyAutoGen = TRUE;
    }
    return MissingOnlyAutoGen ? D3DOK_NOAUTOGEN : D3DERR_NOTAVAILABLE;
}

STDMETHODIMP
D9Enum::CheckDeviceMultiSampleType(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ D3DFORMAT SurfaceFormat,
    _In_ BOOL Windowed,
    _In_ D3DMULTISAMPLE_TYPE MultiSampleType,
    _Out_opt_ DWORD* QualityLevels)
{
    D9LockScope Scope(&Lock);
    const D9Adapter* Table;
    D3DFORMAT Format;
    HRESULT hr;

    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"CheckDeviceMultiSampleType: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }
    if (DeviceType < D3DDEVTYPE_HAL || DeviceType > D3DDEVTYPE_NULLREF)
    {
        D9_TRACE_ONCE(L"CheckDeviceMultiSampleType: invalid device type %u", DeviceType);
        return D3DERR_INVALIDCALL;
    }
    if (SurfaceFormat == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;

    hr = DeviceTable(Adapter, DeviceType, &Table);
    if (FAILED(hr))
        return hr;

    if (QualityLevels != nullptr)
        *QualityLevels = 1;
    if (MultiSampleType == D3DMULTISAMPLE_NONE)
        return S_OK;
    if (static_cast<UINT>(MultiSampleType) > D3DMULTISAMPLE_16_SAMPLES)
    {
        D9_TRACE_ONCE(L"CheckDeviceMultiSampleType: invalid sample type %u", MultiSampleType);
        return D3DERR_INVALIDCALL;
    }
    if (DeviceType != D3DDEVTYPE_HAL)
        Windowed = TRUE;

    Format = D9DriverDepthFormat(Table, SurfaceFormat);
    for (UINT Index = 0; Index < Table->FormatCount; Index++)
    {
        const FORMATOP* Op = &Table->Formats[Index];
        UINT Types;

        if (Op->Format != static_cast<D3DDDIFORMAT>(Format))
            continue;

        /* Bit n stands for n + 1 samples, the nonmaskable type being bit 0 */
        Types = Windowed ? (Op->BltMsTypes & 0xFFFF) : (Op->FlipMsTypes & 0xFFFF);
        if (Table->DriverCaps.DevCaps2 == 0 || DeviceType != D3DDEVTYPE_HAL)
        {
            if (MultiSampleType == D3DMULTISAMPLE_NONMASKABLE)
            {
                UINT Count = 0;

                for (UINT Bits = Types; Bits != 0; Bits >>= 1)
                    Count += Bits & 1;
                if (Count != 0)
                {
                    if (QualityLevels != nullptr)
                        *QualityLevels = Count;
                    return S_OK;
                }
            }
            else if ((Table->DriverCaps.RasterCaps & 0x00800000) != 0)
            {
                return D3DERR_NOTAVAILABLE;
            }
        }

        if ((Types & (1U << (MultiSampleType - 1))) == 0)
            continue;

        if (QualityLevels != nullptr && DeviceType == D3DDEVTYPE_HAL && Table->HasDriver)
        {
            DDIMULTISAMPLEQUALITYLEVELSDATA Levels = {};

            Levels.Format = static_cast<D3DDDIFORMAT>(Format);
            Levels.Flip = !Windowed;
            Levels.MsType = static_cast<D3DDDIMULTISAMPLE_TYPE>(MultiSampleType);
            if (SUCCEEDED(D9DriverGetCaps(Table, D3DDDICAPS_GETMULTISAMPLEQUALITYLEVELS, &Levels, &Levels,
                                          sizeof(Levels))))
            {
                *QualityLevels = Levels.QualityLevels;
            }
        }
        return S_OK;
    }
    return D3DERR_NOTAVAILABLE;
}

STDMETHODIMP
D9Enum::CheckDepthStencilMatch(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ D3DFORMAT AdapterFormat,
    _In_ D3DFORMAT RenderTargetFormat,
    _In_ D3DFORMAT DepthStencilFormat)
{
    D9LockScope Scope(&Lock);
    BOOLEAN TargetFound = FALSE;
    BOOLEAN DepthFound = FALSE;
    BOOLEAN SameDepthNeeded = TRUE;
    const D9Adapter* Table;
    D3DFORMAT Depth;
    HRESULT hr;

    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"CheckDepthStencilMatch: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }
    if (DeviceType < D3DDEVTYPE_HAL || DeviceType > D3DDEVTYPE_SW)
    {
        D9_TRACE_ONCE(L"CheckDepthStencilMatch: invalid device type %u", DeviceType);
        return D3DERR_INVALIDCALL;
    }
    if (AdapterFormat == D3DFMT_UNKNOWN || RenderTargetFormat == D3DFMT_UNKNOWN ||
        DepthStencilFormat == D3DFMT_UNKNOWN)
    {
        return D3DERR_INVALIDCALL;
    }

    hr = DeviceTable(Adapter, DeviceType, &Table);
    if (FAILED(hr))
        return hr;
    if (!D9State.HasWddmDriver && DeviceType == D3DDEVTYPE_HAL && !D9DriverHasOps(Table, AdapterFormat, D9DesktopOps))
        return D3DERR_NOTAVAILABLE;

    Depth = D9DriverDepthFormat(Table, DepthStencilFormat);
    for (UINT Index = 0; Index < Table->FormatCount; Index++)
    {
        const FORMATOP* Op = &Table->Formats[Index];

        if (Op->Format == static_cast<D3DDDIFORMAT>(RenderTargetFormat) &&
            (Op->Operations & FORMATOP_SAME_FORMAT_RENDERTARGET) != 0)
        {
            TargetFound = TRUE;
        }
        if (Op->Format == static_cast<D3DDDIFORMAT>(Depth) && (Op->Operations & FORMATOP_ZSTENCIL) != 0)
        {
            if ((Op->Operations & FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH) != 0)
                SameDepthNeeded = FALSE;
            DepthFound = TRUE;
        }
    }

    if (!TargetFound || !DepthFound)
    {
        D9_TRACE_ONCE(L"CheckDepthStencilMatch: format %u or %u is not supported", RenderTargetFormat,
                      DepthStencilFormat);
        return D3DERR_INVALIDCALL;
    }
    if (SameDepthNeeded && D9BytesPerPixel(RenderTargetFormat) != D9BytesPerPixel(DepthStencilFormat))
        return D3DERR_NOTAVAILABLE;
    return S_OK;
}

STDMETHODIMP
D9Enum::CheckDeviceFormatConversion(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ D3DFORMAT SourceFormat,
    _In_ D3DFORMAT TargetFormat)
{
    D9LockScope Scope(&Lock);
    const D9Adapter* Table;
    BOOLEAN FourCcBlt;
    HRESULT hr;

    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"CheckDeviceFormatConversion: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }
    if (SourceFormat == D3DFMT_UNKNOWN || TargetFormat == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;

    hr = DeviceTable(Adapter, DeviceType, &Table);
    if (FAILED(hr))
        return hr;

    if (D9OpaqueTwin(SourceFormat) == D9OpaqueTwin(TargetFormat))
        return S_OK;

    switch (TargetFormat)
    {
    case D3DFMT_R8G8B8:
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A2B10G10R10:
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
    case D3DFMT_A2R10G10B10:
    case D3DFMT_A16B16G16R16:
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_A32B32G32R32F:
        break;
    default:
        return D3DERR_NOTAVAILABLE;
    }

    /* Old drivers that blit FOURCC surfaces convert them without listing the conversion */
    FourCcBlt = (static_cast<DWORD>(SourceFormat) & 0xFF00) != 0 && Table->DriverCaps.DevCaps2 == 0 &&
                (Table->DriverCaps.Caps & 0x100) != 0;
    if (!FourCcBlt && (DeviceType != D3DDEVTYPE_HAL ||
                       !D9DriverHasOps(Table, TargetFormat, FORMATOP_MEMBEROFGROUP_ARGB) ||
                       !D9DriverHasOps(Table, SourceFormat, FORMATOP_CONVERT_TO_ARGB)))
    {
        return D3DERR_NOTAVAILABLE;
    }
    return S_OK;
}
