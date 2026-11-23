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

/* Without single adapter hybrid mode the newest runtime version is offered without its hybrid extension */
static constexpr UINT D9UmdVersionWithoutHybrid = 0x11000;

/* The feature switch for single adapter hybrid mode; it is on unless configured off */
static constexpr ULONG D9HybridModeFeature = 56684010;
static constexpr ULONG D9FeatureRuntimeConfiguration = 1;
static constexpr ULONG D9FeatureStateDisabled = 1;

/* What ntdll reports for a configured feature */
struct D9FeatureConfiguration
{
    ULONG FeatureId;
    ULONG Flags;
    ULONG VariantPayload;
};

typedef NTSTATUS (NTAPI* PD9_QUERY_FEATURE)(ULONG, ULONG, ULONGLONG*, D9FeatureConfiguration*);

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

static
BOOLEAN
D9HybridModeEnabled(VOID)
{
    static PD9_QUERY_FEATURE Query = reinterpret_cast<PD9_QUERY_FEATURE>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlQueryFeatureConfiguration"));
    D9FeatureConfiguration Configuration = {};
    ULONGLONG ChangeStamp = 0;

    if (Query == nullptr ||
        !NT_SUCCESS(Query(D9HybridModeFeature, D9FeatureRuntimeConfiguration, &ChangeStamp, &Configuration)))
    {
        return TRUE;
    }
    return ((Configuration.Flags >> 4) & 3) != D9FeatureStateDisabled;
}

/* Like the second query, but the driver passes back the adapter it was opened for */
static
HRESULT
APIENTRY
D9QueryAdapterInfoCb3(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_QUERYADAPTERINFO2* Args)
{
    const D9Adapter* Adapter = static_cast<const D9Adapter*>(hDevice);

    if (!D9HybridModeEnabled())
        return E_NOTIMPL;

    if (Args->QueryType == D3DDDI_QUERYADAPTERTYPE_SINGLEADAPTERHYBRIDMODE)
    {
        if (Args->PrivateDriverDataSize < sizeof(DWORD))
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

        /* Only Direct3D 9 on 12 runs an adapter in hybrid mode */
        *static_cast<DWORD*>(Args->pPrivateDriverData) = 0;
        return S_OK;
    }
    return D9QueryAdapterInfoCb2(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(Adapter->KernelAdapter)), Args);
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

/* Opens the driver's adapter at the newest runtime version it takes, starting where the identity says to */
HRESULT
D9CallOpenAdapter(
    _In_ const D9Adapter* Adapter,
    _In_ D3DDDI_ADAPTERCALLBACKS* Callbacks,
    _Out_ D3DDDI_ADAPTERFUNCS* Functions,
    _Out_ HANDLE* DriverAdapter,
    _Out_ UINT* RuntimeVersion,
    _Out_ UINT* DriverVersion)
{
    PFND3DDDI_OPENADAPTER OpenAdapter;
    D3DDDIARG_OPENADAPTER Args;
    BOOLEAN Hybrid = D9HybridModeEnabled();
    HRESULT hr = E_FAIL;

    *DriverAdapter = NULL;
    OpenAdapter = reinterpret_cast<PFND3DDDI_OPENADAPTER>(GetProcAddress(Adapter->Module, "OpenAdapter"));
    if (OpenAdapter == nullptr)
        return E_FAIL;

    for (UINT Index = Adapter->FirstUmdVersion; Index < _countof(RxD3D9UmdVersions); Index++)
    {
        ZeroMemory(&Args, sizeof(Args));
        ZeroMemory(Functions, sizeof(*Functions));
        Args.hAdapter = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(Adapter->KernelAdapter));
        Args.Interface = D9UmdInterface;
        Args.Version = RxD3D9UmdVersions[Index];
        if (Hybrid)
            Args.hDevice = const_cast<D9Adapter*>(Adapter);
        else if (Args.Version == D3D_UMD_INTERFACE_VERSION_WDDM3_2_2)
            Args.Version = D9UmdVersionWithoutHybrid;
        Args.pAdapterCallbacks = Callbacks;
        Args.pAdapterFuncs = Functions;

        hr = OpenAdapter(&Args);
        if (SUCCEEDED(hr))
        {
            *DriverAdapter = Args.hAdapter;
            *RuntimeVersion = Args.Version;
            *DriverVersion = Args.DriverVersion;
            return S_OK;
        }
    }
    return hr;
}

static
BOOLEAN
D9OpenDriverAdapter(
    _Inout_ D9Adapter* Adapter)
{
    RxAdapterIdentity* Identity = new (std::nothrow) RxAdapterIdentity;
    HRESULT hr;

    Adapter->DriverModel = RxDefaultDriverModel;
    Adapter->FirstUmdVersion = 0;
    if (Identity != nullptr)
    {
        if (RxReadAdapterIdentity(Adapter->AdapterLuid, RxAnySource, Identity))
        {
            Adapter->DriverModel = Identity->DriverModel;
            Adapter->FirstUmdVersion = RxFirstD3D9UmdVersion(Identity->DriverModel, Identity->VendorId);
        }
        delete Identity;
    }

    if (GetProcAddress(Adapter->Module, "OpenAdapter") == nullptr)
    {
        D9Trace(L"the user mode driver of %S has no OpenAdapter", Adapter->DeviceName);
        return FALSE;
    }

    Adapter->Callbacks.pfnQueryAdapterInfoCb = D9QueryAdapterInfoCb;
    Adapter->Callbacks.pfnGetMultisampleMethodListCb = D9GetMultisampleMethodListCb;
    Adapter->Callbacks.pfnQueryAdapterInfoCb2 = D9QueryAdapterInfoCb2;
    if (D9HybridModeEnabled())
        Adapter->Callbacks.pfnQueryAdapterInfoCb3 = D9QueryAdapterInfoCb3;

    hr = D9CallOpenAdapter(Adapter, &Adapter->Callbacks, &Adapter->Functions, &Adapter->DriverAdapter,
                           &Adapter->RuntimeVersion, &Adapter->DriverVersion);
    if (FAILED(hr))
    {
        D9Trace(L"OpenAdapter on %S failed 0x%08lX for every runtime version", Adapter->DeviceName, hr);
        return FALSE;
    }
    return TRUE;
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
    D3DKMT_SEGMENTSIZEINFO Segments = {};
    UINT PhysicalCount = 0;
    DWORD Overlay = 0;

    if (!NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_GETSEGMENTSIZE, &Segments, sizeof(Segments))))
    {
        D9Trace(L"the segment sizes of %S are unknown", Adapter->DeviceName);
        return FALSE;
    }
    Adapter->VideoMemory = Segments.DedicatedVideoMemorySize + Segments.DedicatedSystemMemorySize +
                           Segments.SharedSystemMemorySize;

    if (!NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_FLIPQUEUEINFO, &Adapter->FlipQueue,
                                  sizeof(Adapter->FlipQueue))))
    {
        D9Trace(L"the flip queue of %S is unknown", Adapter->DeviceName);
        return FALSE;
    }

    /* The first count is optional and defaults to one; the system asks a second time and needs that answer */
    Adapter->PhysicalAdapterCount = 1;
    if (NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_PHYSICALADAPTERCOUNT, &PhysicalCount,
                                 sizeof(PhysicalCount))))
    {
        Adapter->PhysicalAdapterCount = PhysicalCount;
    }

    if (!NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_PHYSICALADAPTERCOUNT, &PhysicalCount,
                                  sizeof(PhysicalCount))) ||
        !NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_ADAPTERTYPE, &Adapter->AdapterType,
                                  sizeof(Adapter->AdapterType))) ||
        !NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_WDDM_1_2_CAPS, &Adapter->Wddm12Caps,
                                  sizeof(Adapter->Wddm12Caps))) ||
        !NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_WDDM_1_3_CAPS, &Adapter->Wddm13Caps,
                                  sizeof(Adapter->Wddm13Caps))))
    {
        D9Trace(L"kernel adapter queries failed for %S", Adapter->DeviceName);
        return FALSE;
    }

    Adapter->NoOverlayCap = FALSE;
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

    delete[] Adapter->WideModes;
    Adapter->WideModes = nullptr;
    Adapter->WideModeCount = 0;
    if (FAILED(D9ReadKernelModes(Adapter, FALSE, &Modes, &Count)))
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

/* Reads the capabilities and, on the second pass, the format and query tables */
static
BOOLEAN
D9QueryDriverCaps(
    _Inout_ D9Adapter* Adapter,
    _In_ BOOLEAN WithTables)
{
    D3DCAPS9* Caps = &Adapter->DriverCaps;
    DWORD GammaRamp = 0;
    BOOLEAN QueuedFlips;
    HRESULT hr;

    delete[] Adapter->Formats;
    delete[] Adapter->Queries;
    Adapter->Formats = nullptr;
    Adapter->Queries = nullptr;

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

    if (!WithTables)
    {
        if ((Caps->VertexProcessingCaps & D9VertexCapsNoUbyte4) != 0)
        {
            D9Trace(L"the driver sets the retired D3DVTXPCAPS_NO_VSDT_UBYTE4 cap; its HAL is not used");
            return FALSE;
        }
        return TRUE;
    }

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
    for (UINT Index = 0; Index < Adapter->FormatCount; Index++)
    {
        const FORMATOP* Op = &Adapter->Formats[Index];

        if ((Op->Operations & FORMATOP_PIXELSIZE) != 0 && Op->PrivateFormatBitCount != 0)
            D9RegisterFormatBits(static_cast<D3DFORMAT>(Op->Format), Op->PrivateFormatBitCount);
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

/* A driver that is being updated is treated as gone */
BOOLEAN
D9DriverUpdating(
    _In_ D3DKMT_HANDLE Adapter)
{
    BOOL Updating = FALSE;

    if (!NT_SUCCESS(D9QueryKernel(Adapter, KMTQAITYPE_CHECKDRIVERUPDATESTATUS, &Updating, sizeof(Updating))))
        return FALSE;
    return Updating != FALSE;
}

/* The plain runtime does not open a driver while another desktop has the display */
static
BOOLEAN
D9DesktopAccessible(VOID)
{
    typedef NTSTATUS (APIENTRY* PD9_GET_DEVICE_STATE)(D3DKMT_GETDEVICESTATE*);
    static PD9_GET_DEVICE_STATE GetState = reinterpret_cast<PD9_GET_DEVICE_STATE>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTGetDeviceState"));
    D3DKMT_GETDEVICESTATE State = {};

    if (GetState == nullptr)
        return TRUE;

    State.StateType = D3DKMT_DEVICESTATE_RESET;
    GetState(&State);
    return !State.ResetState.DesktopSwitched;
}

/* The two capability passes; the plain runtime first makes sure the desktop is its own */
BOOLEAN
D9RereadDriverCaps(
    _Inout_ D9Adapter* Adapter,
    _In_ BOOLEAN IsEx)
{
    if (!IsEx && !D9DesktopAccessible())
    {
        D9Trace(L"the desktop of %S is not accessible", Adapter->DeviceName);
        return FALSE;
    }
    return D9QueryDriverCaps(Adapter, FALSE) && D9QueryDriverCaps(Adapter, TRUE);
}

/* Everything read through a freshly opened driver adapter: kernel facts, then two capability passes */
BOOLEAN
D9ReadDriverAnswers(
    _Inout_ D9Adapter* Adapter,
    _In_ BOOLEAN IsEx)
{
    D3DKMT_DRIVERVERSION Version = KMT_DRIVERVERSION_WDDM_1_0;

    if (!D9QueryKernelFacts(Adapter) || !D9RereadDriverCaps(Adapter, IsEx))
        return FALSE;

    Adapter->Wddm11Plus = NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_DRIVERVERSION, &Version,
                                                   sizeof(Version))) &&
                          Version > KMT_DRIVERVERSION_WDDM_1_0;
    return TRUE;
}

/* Releases the tables D9ReadDriverAnswers filled */
VOID
D9FreeDriverAnswers(
    _Inout_ D9Adapter* Adapter)
{
    delete[] Adapter->Formats;
    delete[] Adapter->Queries;
    delete[] Adapter->WideModes;
    Adapter->Formats = nullptr;
    Adapter->Queries = nullptr;
    Adapter->WideModes = nullptr;
    Adapter->FormatCount = 0;
    Adapter->QueryCount = 0;
    Adapter->WideModeCount = 0;
}

/* A copy of the adapter that shares nothing it frees with the original */
D9Adapter*
D9CopyAdapter(
    _In_ const D9Adapter* Adapter)
{
    D9Adapter* Copy = new (std::nothrow) D9Adapter(*Adapter);

    if (Copy == nullptr)
        return nullptr;

    Copy->DriverAdapter = NULL;
    Copy->Formats = nullptr;
    Copy->Queries = nullptr;
    Copy->WideModes = nullptr;
    Copy->Modes = nullptr;
    Copy->FormatCount = 0;
    Copy->QueryCount = 0;
    Copy->WideModeCount = 0;
    Copy->ModeCount = 0;
    Copy->ExtendedModeCount = 0;
    Copy->UnvalidatedModeCount = 0;
    return Copy;
}

BOOLEAN
D9OpenDriver(
    _Inout_ D9Adapter* Adapter,
    _In_ BOOLEAN IsEx)
{
    if (Adapter->KernelAdapter == 0)
        return FALSE;

    if (D9DriverUpdating(Adapter->KernelAdapter))
    {
        D9Trace(L"the driver of %S is being updated", Adapter->DeviceName);
        return FALSE;
    }

    if (!D9LoadDriverModule(Adapter) || !D9OpenDriverAdapter(Adapter) || !D9ReadDriverAnswers(Adapter, IsEx))
    {
        D9CloseDriver(Adapter);
        return FALSE;
    }

    D9NoteDisplayFormats(Adapter);
    Adapter->HasDriver = TRUE;
    return TRUE;
}

/* Another driver adapter beside the object's own, with everything read through it again */
_Ret_maybenull_
D9Adapter*
D9OpenSecondDriver(
    _In_ const D9Adapter* Adapter,
    _In_ BOOLEAN IsEx)
{
    D9Adapter* Second;

    if (D9DriverUpdating(Adapter->KernelAdapter))
        return nullptr;

    Second = D9CopyAdapter(Adapter);
    if (Second == nullptr)
        return nullptr;

    if (!D9LoadDriverModule(Second) || !D9OpenDriverAdapter(Second) || !D9ReadDriverAnswers(Second, IsEx))
    {
        D9CloseSecondDriver(Second);
        return nullptr;
    }
    Second->HasDriver = TRUE;
    return Second;
}

VOID
D9CloseSecondDriver(
    _In_opt_ D9Adapter* Second)
{
    if (Second == nullptr)
        return;
    D9CloseDriver(Second);
    delete Second;
}

/* Opens a second driver adapter to check a device creation against, as the system does, and closes it again */
HRESULT
D9ProbeDriver(
    _In_ const D9Adapter* Adapter,
    _In_ BOOLEAN IsEx,
    _Out_ D3DCAPS9* Caps)
{
    D9Adapter* Probe = D9OpenSecondDriver(Adapter, IsEx);

    ZeroMemory(Caps, sizeof(*Caps));
    if (Probe == nullptr)
        return D3DERR_NOTAVAILABLE;

    *Caps = Probe->DriverCaps;
    D9CloseSecondDriver(Probe);
    return S_OK;
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

    D9FreeDriverAnswers(Adapter);
    Adapter->HasDriver = FALSE;
    D9FreeModeTable(Adapter);
}
