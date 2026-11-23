/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The Direct3D 9 user mode driver behind each display and what it reports
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"
#include "umdversions.hpp"

/* Direct3D 9 opens its user mode driver at the DirectX 9 interface */
static constexpr UINT D9UmdInterface = 9;

/* The newest runtime version is offered without its hybrid adapter extension */
static constexpr UINT D9UmdVersionWithoutHybrid = 0x11000;

/* An adapter property queried by number; bit 7 of its answer withdraws the overlay cap */
static constexpr KMTQUERYADAPTERINFOTYPE D9OverlayPropertyQuery = static_cast<KMTQUERYADAPTERINFOTYPE>(57);

/* Capability bits the runtime forces on or off in what the driver reports */
static constexpr DWORD D9CapsOverlay = 0x00000800;
static constexpr DWORD D9CapsRuntimeOwned = 0x80000000;
static constexpr DWORD D9Caps2Forced = 0x84008800;
static constexpr DWORD D9Caps2Cleared = 0x10200000;
static constexpr DWORD D9Caps2Presents = 0x00400000;
static constexpr DWORD D9Caps2QueuedFlips = 0x00200000;
static constexpr DWORD D9IntervalsImmediate = 0x80000001;
static constexpr DWORD D9IntervalsAll = 0x8000000F;

/* Fog coordinates in the vertex format; the runtime derives fog and specular alpha from it */
static constexpr DWORD D9MiscCapsFogInFvf = 0x00002000;

/* A Direct3D 8 vertex cap a Direct3D 9 driver must not report */
static constexpr DWORD D9VertexCapsNoUbyte4 = 0x00000080;

/* Depth formats the driver may list in either component order; the runtime keeps the first spelling */
static const D3DDDIFORMAT D9DepthSpellings[][2] =
{
    { D3DDDIFMT_S1D15, D3DDDIFMT_D15S1 },
    { D3DDDIFMT_X8D24, D3DDDIFMT_D24X8 },
    { D3DDDIFMT_X4S4D24, D3DDDIFMT_D24X4S4 },
    { D3DDDIFMT_S8D24, D3DDDIFMT_D24S8 },
};

static
HRESULT
APIENTRY
D9QueryAdapterInfoCb(
    _In_ HANDLE hAdapter,
    _In_ const D3DDDICB_QUERYADAPTERINFO* Args)
{
    D3DKMT_HANDLE Adapter = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(hAdapter));

    return D9HresultFromStatus(D9QueryKernel(Adapter, KMTQAITYPE_UMDRIVERPRIVATE, Args->pPrivateDriverData,
                                             Args->PrivateDriverDataSize));
}

static
HRESULT
APIENTRY
D9QueryAdapterInfoCb2(
    _In_ HANDLE hAdapter,
    _In_ const D3DDDICB_QUERYADAPTERINFO2* Args)
{
    D3DKMT_HANDLE Adapter = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(hAdapter));
    KMTQUERYADAPTERINFOTYPE Type;

    if (Args->QueryType == D3DDDI_QUERYADAPTERTYPE_DRIVERPRIVATE)
        Type = KMTQAITYPE_UMDRIVERPRIVATE;
    else if (Args->QueryType == D3DDDI_QUERYADAPTERTYPE_QUERYREGISTRY)
        Type = KMTQAITYPE_QUERYREGISTRY;
    else
        return D9HresultFromStatus(STATUS_INVALID_PARAMETER);

    return D9HresultFromStatus(D9QueryKernel(Adapter, Type, Args->pPrivateDriverData, Args->PrivateDriverDataSize));
}

/* The kernel returns its own method records; the driver wants the two field DDI form */
static
HRESULT
APIENTRY
D9GetMultisampleMethodListCb(
    _In_ HANDLE hAdapter,
    _Inout_ D3DDDICB_GETMULTISAMPLEMETHODLIST* Args)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_GETMULTISAMPLEMETHODLIST List = {};
    NTSTATUS Status;

    if (Kmt == nullptr || Kmt->GetMultisampleMethodList == nullptr)
        return E_FAIL;

    List.hAdapter = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(hAdapter));
    List.VidPnSourceId = Args->VidPnSourceId;
    List.Width = Args->Width;
    List.Height = Args->Height;
    List.Format = Args->Format;
    List.MethodCount = Args->MethodCount;
    if (Args->pMethodList != nullptr || Args->MethodCount == 0)
    {
        if (Args->MethodCount != 0)
        {
            List.pMethodList = new (std::nothrow) D3DKMT_MULTISAMPLEMETHOD[Args->MethodCount]();
            if (List.pMethodList == nullptr)
                return E_OUTOFMEMORY;
        }
    }

    Status = Kmt->GetMultisampleMethodList(&List);
    if (NT_SUCCESS(Status))
    {
        Args->MethodCount = List.MethodCount;
        for (UINT Index = 0; List.pMethodList != nullptr && Index < List.MethodCount; Index++)
        {
            Args->pMethodList[Index].NumSamples = List.pMethodList[Index].NumSamples;
            Args->pMethodList[Index].NumQualityLevels = List.pMethodList[Index].NumQualityLevels;
        }
    }
    else
    {
        D9Trace(L"D3DKMTGetMultisampleMethodList failed 0x%08lX", Status);
    }

    delete[] List.pMethodList;
    return D9HresultFromStatus(Status);
}

static
BOOLEAN
D9LoadDriverModule(
    _Inout_ D9Adapter* Adapter)
{
    D3DKMT_UMDFILENAMEINFO Name;

    if (!D9QueryUmdName(Adapter->KernelAdapter, KMTUMDVERSION_DX9, &Name))
    {
        D9Trace(L"%S has no Direct3D 9 user mode driver", Adapter->DeviceName);
        return FALSE;
    }

    Adapter->Module = LoadLibraryExW(Name.UmdFileName, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (Adapter->Module == NULL && GetLastError() == ERROR_INVALID_PARAMETER)
        Adapter->Module = LoadLibraryW(Name.UmdFileName);
    if (Adapter->Module == NULL)
    {
        D9Trace(L"cannot load user mode driver %s (%lu)", Name.UmdFileName, GetLastError());
        return FALSE;
    }
    return TRUE;
}

static
BOOLEAN
D9OpenDriverAdapter(
    _Inout_ D9Adapter* Adapter)
{
    RxAdapterIdentity* Identity = new (std::nothrow) RxAdapterIdentity;
    PFND3DDDI_OPENADAPTER OpenAdapter;
    D3DDDIARG_OPENADAPTER Args;
    UINT First = 0;
    HRESULT hr = E_FAIL;

    Adapter->DriverModel = RxDefaultDriverModel;
    if (Identity != nullptr)
    {
        if (RxReadAdapterIdentity(Adapter->AdapterLuid, RxAnySource, Identity))
        {
            Adapter->DriverModel = Identity->DriverModel;
            First = RxFirstD3D9UmdVersion(Identity->DriverModel, Identity->VendorId);
        }
        delete Identity;
    }

    OpenAdapter = reinterpret_cast<PFND3DDDI_OPENADAPTER>(GetProcAddress(Adapter->Module, "OpenAdapter"));
    if (OpenAdapter == nullptr)
    {
        D9Trace(L"the user mode driver of %S has no OpenAdapter", Adapter->DeviceName);
        return FALSE;
    }

    Adapter->Callbacks.pfnQueryAdapterInfoCb = D9QueryAdapterInfoCb;
    Adapter->Callbacks.pfnGetMultisampleMethodListCb = D9GetMultisampleMethodListCb;
    Adapter->Callbacks.pfnQueryAdapterInfoCb2 = D9QueryAdapterInfoCb2;

    for (UINT Index = First; Index < _countof(RxD3D9UmdVersions); Index++)
    {
        ZeroMemory(&Args, sizeof(Args));
        Args.hAdapter = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(Adapter->KernelAdapter));
        Args.Interface = D9UmdInterface;
        Args.Version = RxD3D9UmdVersions[Index];
        if (Args.Version == D3D_UMD_INTERFACE_VERSION_WDDM3_2_2)
            Args.Version = D9UmdVersionWithoutHybrid;
        Args.pAdapterCallbacks = &Adapter->Callbacks;
        Args.pAdapterFuncs = &Adapter->Functions;

        hr = OpenAdapter(&Args);
        if (SUCCEEDED(hr))
        {
            Adapter->DriverAdapter = Args.hAdapter;
            Adapter->RuntimeVersion = Args.Version;
            Adapter->DriverVersion = Args.DriverVersion;
            return TRUE;
        }
    }

    D9Trace(L"OpenAdapter on %S failed 0x%08lX for every runtime version", Adapter->DeviceName, hr);
    return FALSE;
}

HRESULT
D9DriverGetCaps(
    _In_ const D9Adapter* Adapter,
    _In_ D3DDDICAPS_TYPE Type,
    _In_opt_ VOID* Info,
    _Out_writes_bytes_(Size) VOID* Data,
    _In_ UINT Size)
{
    D3DDDIARG_GETCAPS Caps;

    Caps.Type = Type;
    Caps.pInfo = Info;
    Caps.pData = Data;
    Caps.DataSize = Size;
    return Adapter->Functions.pfnGetCaps(Adapter->DriverAdapter, &Caps);
}

/* The kernel facts the capability corrections depend on; any failure leaves the adapter without a driver */
static
BOOLEAN
D9QueryKernelFacts(
    _Inout_ D9Adapter* Adapter)
{
    DWORD Overlay = 0;

    if (!NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_FLIPQUEUEINFO, &Adapter->FlipQueue,
                                  sizeof(Adapter->FlipQueue))) ||
        !NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_ADAPTERTYPE, &Adapter->AdapterType,
                                  sizeof(Adapter->AdapterType))) ||
        !NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_WDDM_1_2_CAPS, &Adapter->Wddm12Caps,
                                  sizeof(Adapter->Wddm12Caps))))
    {
        D9Trace(L"kernel adapter queries failed for %S", Adapter->DeviceName);
        return FALSE;
    }

    if (NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, D9OverlayPropertyQuery, &Overlay, sizeof(Overlay))))
        Adapter->NoOverlayCap = (Overlay & 0x80) != 0;
    return TRUE;
}

/* Gives a depth format the spelling an earlier entry of the same pair used */
static
VOID
D9UnifyDepthSpellings(
    _Inout_updates_(Count) FORMATOP* Formats,
    _In_ UINT Count)
{
    for (UINT Index = 0; Index < Count; Index++)
    {
        for (const auto& Pair : D9DepthSpellings)
        {
            if (Formats[Index].Format != Pair[0] && Formats[Index].Format != Pair[1])
                continue;

            for (UINT Earlier = 0; Earlier < Index; Earlier++)
            {
                if (Formats[Earlier].Format == Pair[0] || Formats[Earlier].Format == Pair[1])
                    Formats[Index].Format = Formats[Earlier].Format;
            }
            break;
        }
    }
}

/* Keeps the 10 bit and float desktop modes; the plain runtime copies them from 32 bit modes */
static
BOOLEAN
D9CollectWideModes(
    _Inout_ D9Adapter* Adapter)
{
    D3DKMT_DISPLAYMODE* Modes;
    UINT Count;
    UINT Wide = 0;

    if (FAILED(D9ReadKernelModes(Adapter, &Modes, &Count)))
        return FALSE;

    for (UINT Index = 0; Index < Count; Index++)
    {
        if (Modes[Index].Format == D3DDDIFMT_A2R10G10B10 || Modes[Index].Format == D3DDDIFMT_A16B16G16R16F)
            Wide += 1;
    }

    if (Wide != 0)
    {
        Adapter->WideModes = new (std::nothrow) D3DDISPLAYMODE[Wide];
        if (Adapter->WideModes == nullptr)
        {
            delete[] Modes;
            return FALSE;
        }
    }

    for (UINT Index = 0; Index < Count; Index++)
    {
        D3DDISPLAYMODE* Mode;

        if (Modes[Index].Format != D3DDDIFMT_A2R10G10B10 && Modes[Index].Format != D3DDDIFMT_A16B16G16R16F)
            continue;

        Mode = &Adapter->WideModes[Adapter->WideModeCount];
        Mode->Width = Modes[Index].Width;
        Mode->Height = Modes[Index].Height;
        Mode->RefreshRate = Modes[Index].IntegerRefreshRate;
        Mode->Format = static_cast<D3DFORMAT>(Modes[Index].Format);
        Adapter->WideModeCount += 1;
    }

    delete[] Modes;
    return TRUE;
}

/* Keeps only the capability bits Direct3D 9 defines, field by field */
static
VOID
D9MaskDriverCaps(
    _Inout_ D3DCAPS9* Caps)
{
    DWORD PrimitiveMisc = Caps->PrimitiveMiscCaps;

    if (Caps->MaxPointSize == 1.0f)
        Caps->MaxPointSize = 0.0f;
    if ((PrimitiveMisc & D3DPMISCCAPS_CULLNONE) == 0)
        Caps->StencilCaps &= ~D3DSTENCILCAPS_TWOSIDED;

    Caps->PresentationIntervals &= 0x8000000F;
    Caps->CursorCaps &= 0x00000003;
    Caps->DevCaps &= 0x07FBFFF0;
    Caps->PrimitiveMiscCaps = PrimitiveMisc & 0x003FFFF6;
    if ((PrimitiveMisc & D9MiscCapsFogInFvf) != 0)
        Caps->PrimitiveMiscCaps |= D3DPMISCCAPS_FOGANDSPECULARALPHA;
    Caps->DevCaps2 = (Caps->DevCaps2 | D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES) & 0x0000007F;
    Caps->RasterCaps &= 0x0F77E191;
    Caps->ZCmpCaps &= 0x000000FF;
    Caps->SrcBlendCaps &= 0x0000FFFF;
    Caps->DestBlendCaps &= 0x0000FFFF;
    Caps->AlphaCmpCaps &= 0x000000FF;
    Caps->ShadeCaps &= 0x00084208;
    Caps->TextureCaps &= 0x0027EDE7;
    Caps->TextureFilterCaps &= 0x1F071F00;
    Caps->CubeTextureFilterCaps &= 0x1F071F00;
    Caps->VolumeTextureFilterCaps &= 0x1F071F00;
    Caps->TextureAddressCaps &= 0x0000003F;
    Caps->VolumeTextureAddressCaps &= 0x0000003F;
    Caps->LineCaps &= 0x0000003F;
    Caps->StencilCaps &= 0x000001FF;
    Caps->FVFCaps &= 0x0018FFFF;
    Caps->TextureOpCaps &= 0x03FFFFFF;
    Caps->StretchRectFilterCaps &= 0x1F071F00;
    Caps->VertexTextureFilterCaps &= 0x1F071F00;
    Caps->VS20Caps.Caps &= 0x00000001;
    Caps->PS20Caps.Caps &= 0x0000001F;

    /* Copies between video and system memory are always offered on hardware */
    Caps->Caps3 |= D3DCAPS3_COPY_TO_VIDMEM | D3DCAPS3_COPY_TO_SYSTEMMEM;

    if ((Caps->Caps2 & 0x00000200) == 0)
        Caps->DevCaps &= ~D3DDEVCAPS_TEXTURENONLOCALVIDMEM;
    if (Caps->MaxAnisotropy == 0)
        Caps->MaxAnisotropy = 1;
    if ((Caps->DevCaps2 & D3DDEVCAPS2_STREAMOFFSET) == 0)
        Caps->NumSimultaneousRTs = 1;
    Caps->Caps2 &= ~0x00800000;
    if ((Caps->DevCaps & D3DDEVCAPS_SEPARATETEXTUREMEMORIES) != 0)
    {
        Caps->MaxSimultaneousTextures = 1;
        Caps->DevCaps &= ~D3DDEVCAPS_SEPARATETEXTUREMEMORIES;
    }
}

/* Drops format operations Direct3D 9 does not define and fills in the ones others imply */
static
VOID
D9NormalizeFormatOps(
    _Inout_updates_(Count) FORMATOP* Formats,
    _In_ UINT Count)
{
    for (UINT Index = 0; Index < Count; Index++)
    {
        UINT Ops = Formats[Index].Operations & 0x01FFFFDF;

        if ((Ops & FORMATOP_OFFSCREEN_RENDERTARGET) != 0)
            Ops |= FORMATOP_SAME_FORMAT_RENDERTARGET | FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET;
        if ((Ops & FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET) != 0)
            Ops |= FORMATOP_SAME_FORMAT_RENDERTARGET;
        if ((Ops & FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH) != 0)
            Ops |= FORMATOP_ZSTENCIL;
        Formats[Index].Operations = Ops;
    }
}

/* Reads the capabilities, formats and queries and applies the runtime's corrections */
static
BOOLEAN
D9QueryDriverCaps(
    _Inout_ D9Adapter* Adapter)
{
    D3DCAPS9* Caps = &Adapter->DriverCaps;
    DWORD GammaRamp = 0;
    BOOLEAN QueuedFlips;
    HRESULT hr;

    if (FAILED(D9ReadDesktopMode(Adapter->DeviceName, &Adapter->DesktopMode, &Adapter->Rotation)))
        return FALSE;

    hr = D9DriverGetCaps(Adapter, D3DDDICAPS_GETD3D9CAPS, nullptr, Caps, sizeof(*Caps));
    if (FAILED(hr))
    {
        D9Trace(L"GetCaps(GETD3D9CAPS) failed 0x%08lX", hr);
        return FALSE;
    }

    if (Adapter->NoOverlayCap)
        Caps->Caps &= ~D9CapsOverlay;
    Caps->Caps |= D9CapsRuntimeOwned;

    QueuedFlips = Adapter->FlipQueue.MaxSoftwareFlipQueueLength != 0 || Adapter->FlipQueue.FlipFlags.FlipInterval;
    Caps->Caps2 = ((Caps->Caps2 | D9Caps2Forced) & ~D9Caps2Cleared) | D9Caps2Presents;
    if (QueuedFlips)
        Caps->Caps2 |= D9Caps2QueuedFlips;
    Caps->PresentationIntervals = QueuedFlips ? D9IntervalsAll : D9IntervalsImmediate;
    if (!Adapter->AdapterType.RenderSupported)
        Caps->CursorCaps = Adapter->Wddm12Caps.SupportHWCursor;

    hr = D9DriverGetCaps(Adapter, D3DDDICAPS_GETFORMATCOUNT, nullptr, &Adapter->FormatCount,
                         sizeof(Adapter->FormatCount));
    if (FAILED(hr))
    {
        D9Trace(L"GetCaps(GETFORMATCOUNT) failed 0x%08lX", hr);
        return FALSE;
    }

    if (!D9CollectWideModes(Adapter))
        return FALSE;

    if (FAILED(D9DriverGetCaps(Adapter, D3DDDICAPS_GETD3DQUERYCOUNT, nullptr, &Adapter->QueryCount,
                               sizeof(Adapter->QueryCount))))
    {
        Adapter->QueryCount = 0;
    }

    /* Display only adapters take the gamma ramp from the kernel, render adapters from the driver */
    if (Adapter->AdapterType.RenderSupported)
    {
        if (FAILED(D9DriverGetCaps(Adapter, D3DDDICAPS_GETGAMMARAMPCAPS, nullptr, &GammaRamp, sizeof(GammaRamp))))
            GammaRamp = 0;
    }
    else
    {
        GammaRamp = Adapter->Wddm12Caps.SupportGammaRamp;
    }
    if ((GammaRamp & 1) != 0)
        Caps->Caps2 |= D3DCAPS2_FULLSCREENGAMMA;
    else
        Caps->Caps2 &= ~D3DCAPS2_FULLSCREENGAMMA;

    Adapter->Formats = new (std::nothrow) FORMATOP[Adapter->FormatCount]();
    if (Adapter->Formats == nullptr)
        return FALSE;
    hr = D9DriverGetCaps(Adapter, D3DDDICAPS_GETFORMATDATA, nullptr, Adapter->Formats,
                         Adapter->FormatCount * sizeof(FORMATOP));
    if (FAILED(hr))
    {
        D9Trace(L"GetCaps(GETFORMATDATA) failed 0x%08lX", hr);
        return FALSE;
    }
    D9UnifyDepthSpellings(Adapter->Formats, Adapter->FormatCount);

    if (Adapter->QueryCount != 0)
    {
        Adapter->Queries = new (std::nothrow) D3DDDIQUERYTYPE[Adapter->QueryCount]();
        if (Adapter->Queries == nullptr)
            return FALSE;
        hr = D9DriverGetCaps(Adapter, D3DDDICAPS_GETD3DQUERYDATA, nullptr, Adapter->Queries,
                             Adapter->QueryCount * sizeof(D3DDDIQUERYTYPE));
        if (FAILED(hr))
        {
            D9Trace(L"GetCaps(GETD3DQUERYDATA) failed 0x%08lX", hr);
            return FALSE;
        }
    }

    if ((Caps->VertexProcessingCaps & D9VertexCapsNoUbyte4) != 0)
    {
        D9Trace(L"the driver sets the retired D3DVTXPCAPS_NO_VSDT_UBYTE4 cap; its HAL is not used");
        return FALSE;
    }

    D9MaskDriverCaps(Caps);
    D9NormalizeFormatOps(Adapter->Formats, Adapter->FormatCount);
    return TRUE;
}

/* Notes which desktop formats the driver can scan out, and which 16 bit format stands for 16 bit modes */
static
VOID
D9NoteDisplayFormats(
    _Inout_ D9Adapter* Adapter)
{
    Adapter->Display16Format = D3DFMT_UNKNOWN;
    for (UINT Index = 0; Index < Adapter->FormatCount; Index++)
    {
        const FORMATOP* Op = &Adapter->Formats[Index];

        if ((Op->Operations & FORMATOP_DISPLAYMODE) == 0)
            continue;

        if (Op->Format == D3DDDIFMT_X8R8G8B8)
            Adapter->Displays32 = TRUE;
        else if (Op->Format == D3DDDIFMT_R5G6B5 || Op->Format == D3DDDIFMT_X1R5G5B5)
        {
            Adapter->Displays16 = TRUE;
            Adapter->Display16Format = static_cast<D3DFORMAT>(Op->Format);
        }
    }

    if (Adapter->Display16Format == D3DFMT_UNKNOWN)
        Adapter->Display16Format = Adapter->DesktopMode.Format == D3DFMT_X1R5G5B5 ? D3DFMT_X1R5G5B5 : D3DFMT_R5G6B5;
}

BOOLEAN
D9OpenDriver(
    _Inout_ D9Adapter* Adapter,
    _In_ BOOLEAN IsEx)
{
    if (Adapter->KernelAdapter == 0)
        return FALSE;

    if (!D9LoadDriverModule(Adapter) || !D9OpenDriverAdapter(Adapter) || !D9QueryKernelFacts(Adapter) ||
        !D9QueryDriverCaps(Adapter))
    {
        D9CloseDriver(Adapter);
        return FALSE;
    }

    D9NoteDisplayFormats(Adapter);
    Adapter->HasDriver = TRUE;
    return TRUE;
}

VOID
D9CloseDriver(
    _Inout_ D9Adapter* Adapter)
{
    if (Adapter->DriverAdapter != NULL && Adapter->Functions.pfnCloseAdapter != nullptr)
        Adapter->Functions.pfnCloseAdapter(Adapter->DriverAdapter);
    Adapter->DriverAdapter = NULL;

    /* The module stays mapped: drivers start worker threads that outlive the adapter */
    Adapter->Module = NULL;

    delete[] Adapter->Formats;
    delete[] Adapter->Queries;
    delete[] Adapter->WideModes;
    Adapter->Formats = nullptr;
    Adapter->Queries = nullptr;
    Adapter->WideModes = nullptr;
    Adapter->FormatCount = 0;
    Adapter->QueryCount = 0;
    Adapter->WideModeCount = 0;
    Adapter->HasDriver = FALSE;
    D9FreeModeTable(Adapter);
}
