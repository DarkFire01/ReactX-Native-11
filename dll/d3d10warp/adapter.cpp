/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Adapter: interface versions, caps and device creation
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device.hpp"

#define RX_WARP_VERSION(Major, Minor, Build) \
    (((UINT64)(Major) << 48) | ((UINT64)(Minor) << 32) | ((UINT64)(Build) << 16))

/* Without a WDDM 2 kernel adapter only the WDDM 1.3 interface is offered */
static const UINT64 RxWarpVersionsWddm1[] =
{
    RX_WARP_VERSION(11, 16, 1),
};

static const UINT64 RxWarpVersionsWddm2[] =
{
    RX_WARP_VERSION(11, 32, 9), RX_WARP_VERSION(11, 34, 2), RX_WARP_VERSION(11, 35, 5), RX_WARP_VERSION(11, 36, 1),
    RX_WARP_VERSION(11, 37, 1), RX_WARP_VERSION(11, 38, 0), RX_WARP_VERSION(11, 39, 4), RX_WARP_VERSION(11, 40, 1),
    RX_WARP_VERSION(11, 41, 0), RX_WARP_VERSION(11, 42, 0), RX_WARP_VERSION(11, 43, 0), RX_WARP_VERSION(11, 44, 0),
    RX_WARP_VERSION(11, 45, 1),
    RX_WARP_VERSION(12, 10, 5),
    RX_WARP_VERSION(12, 20, 0), RX_WARP_VERSION(12, 20, 1), RX_WARP_VERSION(12, 20, 2), RX_WARP_VERSION(12, 20, 3),
    RX_WARP_VERSION(12, 20, 4), RX_WARP_VERSION(12, 20, 5), RX_WARP_VERSION(12, 20, 6), RX_WARP_VERSION(12, 20, 7),
    RX_WARP_VERSION(12, 20, 8),
    RX_WARP_VERSION(12, 30, 0), RX_WARP_VERSION(12, 30, 1), RX_WARP_VERSION(12, 30, 2), RX_WARP_VERSION(12, 30, 3),
    RX_WARP_VERSION(12, 30, 4),
    RX_WARP_VERSION(12, 40, 0), RX_WARP_VERSION(12, 40, 1), RX_WARP_VERSION(12, 40, 2), RX_WARP_VERSION(12, 40, 3),
    RX_WARP_VERSION(12, 50, 0), RX_WARP_VERSION(12, 50, 1), RX_WARP_VERSION(12, 50, 2), RX_WARP_VERSION(12, 50, 3),
    RX_WARP_VERSION(12, 50, 4),
    RX_WARP_VERSION(12, 60, 0), RX_WARP_VERSION(12, 60, 1), RX_WARP_VERSION(12, 60, 2), RX_WARP_VERSION(12, 60, 3),
    RX_WARP_VERSION(12, 60, 4), RX_WARP_VERSION(12, 60, 5),
    RX_WARP_VERSION(12, 70, 2), RX_WARP_VERSION(12, 70, 3), RX_WARP_VERSION(12, 70, 4), RX_WARP_VERSION(12, 70, 5),
    RX_WARP_VERSION(12, 70, 6),
    RX_WARP_VERSION(12, 80, 0), RX_WARP_VERSION(12, 80, 1), RX_WARP_VERSION(12, 80, 2), RX_WARP_VERSION(12, 80, 3),
    RX_WARP_VERSION(12, 80, 4), RX_WARP_VERSION(12, 80, 6), RX_WARP_VERSION(12, 80, 8), RX_WARP_VERSION(12, 80, 9),
    RX_WARP_VERSION(12, 80, 90), RX_WARP_VERSION(12, 80, 91), RX_WARP_VERSION(12, 80, 92), RX_WARP_VERSION(12, 80, 94),
    RX_WARP_VERSION(12, 80, 95), RX_WARP_VERSION(12, 80, 96), RX_WARP_VERSION(12, 80, 97), RX_WARP_VERSION(12, 80, 98),
    RX_WARP_VERSION(12, 80, 99), RX_WARP_VERSION(12, 80, 100), RX_WARP_VERSION(12, 80, 101), RX_WARP_VERSION(12, 80, 102),
    RX_WARP_VERSION(12, 80, 103), RX_WARP_VERSION(12, 80, 104), RX_WARP_VERSION(12, 80, 105), RX_WARP_VERSION(12, 80, 106),
    RX_WARP_VERSION(12, 80, 107), RX_WARP_VERSION(12, 80, 108), RX_WARP_VERSION(12, 80, 109), RX_WARP_VERSION(12, 80, 110),
    RX_WARP_VERSION(12, 80, 111), RX_WARP_VERSION(12, 80, 112), RX_WARP_VERSION(12, 80, 113), RX_WARP_VERSION(12, 80, 114),
    RX_WARP_VERSION(12, 80, 115), RX_WARP_VERSION(12, 80, 116), RX_WARP_VERSION(12, 80, 117), RX_WARP_VERSION(12, 80, 118),
};

static_assert(ARRAYSIZE(RxWarpVersionsWddm2) == 84, "the WDDM 2 version list has 84 entries");

HRESULT
RxWarpAdapter::Open(
    _Inout_ D3D10DDIARG_OPENADAPTER *Args)
{
    SYSTEM_INFO System;
    RxWarpAdapter *Adapter;

    Adapter = new (std::nothrow) RxWarpAdapter();
    if (Adapter == nullptr)
        return E_OUTOFMEMORY;

    Adapter->RuntimeAdapter = Args->hRTAdapter;
    Adapter->Interface = Args->Interface;
    Adapter->Version = Args->Version;
    if (Args->pAdapterCallbacks != nullptr)
        Adapter->Callbacks = *Args->pAdapterCallbacks;

    GetSystemInfo(&System);
    Adapter->ProcessorCount = System.dwNumberOfProcessors;
    Adapter->QueryKernel();

    Args->hAdapter.pDrvPrivate = Adapter;
    Args->pAdapterFuncs_2->pfnCalcPrivateDeviceSize = CalcPrivateDeviceSize;
    Args->pAdapterFuncs_2->pfnCreateDevice = CreateDevice;
    Args->pAdapterFuncs_2->pfnCloseAdapter = CloseAdapter;
    Args->pAdapterFuncs_2->pfnGetSupportedVersions = GetSupportedVersions;
    Args->pAdapterFuncs_2->pfnGetCaps = GetCaps;
    return S_OK;
}

/* Older kernel adapters hand out only the first two DWORDs */
VOID
RxWarpAdapter::QueryKernel(VOID)
{
    D3DDDICB_QUERYADAPTERINFO Query;

    if (Callbacks.pfnQueryAdapterInfoCb == nullptr)
        return;

    Query.pPrivateDriverData = &Kernel;
    Query.PrivateDriverDataSize = sizeof(Kernel);
    if (FAILED(Callbacks.pfnQueryAdapterInfoCb(RuntimeAdapter.handle, &Query)))
    {
        Query.PrivateDriverDataSize = 2 * sizeof(UINT);
        if (FAILED(Callbacks.pfnQueryAdapterInfoCb(RuntimeAdapter.handle, &Query)))
            Kernel = {};
    }
}

SIZE_T
APIENTRY
RxWarpAdapter::CalcPrivateDeviceSize(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ const D3D10DDIARG_CALCPRIVATEDEVICESIZE *Args)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Args);
    return 4000;
}

HRESULT
APIENTRY
RxWarpAdapter::CreateDevice(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ D3D10DDIARG_CREATEDEVICE *Args)
{
    RxWarpLock Lock;

    return RxWarpDevice::Create(static_cast<RxWarpAdapter *>(Adapter.pDrvPrivate), Args);
}

HRESULT
APIENTRY
RxWarpAdapter::CloseAdapter(
    _In_ D3D10DDI_HADAPTER Adapter)
{
    RxWarpLock Lock;

    delete static_cast<RxWarpAdapter *>(Adapter.pDrvPrivate);
    return S_OK;
}

HRESULT
APIENTRY
RxWarpAdapter::GetSupportedVersions(
    _In_ D3D10DDI_HADAPTER Adapter,
    _Inout_ UINT32 *Entries,
    _Out_writes_opt_(*Entries) UINT64 *Versions)
{
    RxWarpLock Lock;
    RxWarpAdapter *This = static_cast<RxWarpAdapter *>(Adapter.pDrvPrivate);
    const UINT64 *List = This->HasWddm2Kernel() ? RxWarpVersionsWddm2 : RxWarpVersionsWddm1;
    UINT32 Count = This->HasWddm2Kernel() ? ARRAYSIZE(RxWarpVersionsWddm2) : ARRAYSIZE(RxWarpVersionsWddm1);

    if (Versions != nullptr)
    {
        if (*Entries < Count)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        memcpy(Versions, List, Count * sizeof(*Versions));
    }
    *Entries = Count;
    return S_OK;
}

/* Writes a fixed size answer when the caller asked for exactly that size */
template <typename Value>
static
HRESULT
RxWarpAnswer(
    _In_ const D3D10_2DDIARG_GETCAPS *Args,
    _In_ Value Answer)
{
    if (Args->DataSize != sizeof(Answer))
        return E_UNEXPECTED;
    memcpy(Args->pData, &Answer, sizeof(Answer));
    return S_OK;
}

HRESULT
APIENTRY
RxWarpAdapter::GetCaps(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ const D3D10_2DDIARG_GETCAPS *Args)
{
    RxWarpLock Lock;
    RxWarpAdapter *This = static_cast<RxWarpAdapter *>(Adapter.pDrvPrivate);
    const UINT64 Layout[2] = { 0, 1 };
    const UINT LayoutSets[3] = { 0, 0, 1 };

    switch ((UINT)Args->Type)
    {
        case D3D11DDICAPS_THREADING:
        case 151:
        case D3DWDDM2_0DDICAPS_D3D11_OPTIONS3:
            return RxWarpAnswer(Args, (UINT)1);

        case D3D11DDICAPS_SHADER:
            return RxWarpAnswer(Args, (UINT)119);

        case D3D11DDICAPS_3DPIPELINESUPPORT:
            /* Feature levels 12_0 and 12_1 need the WDDM 2 kernel adapter */
            return RxWarpAnswer(Args, (UINT)(This->HasWddm2Kernel() ? 511 : 127));

        case D3D11_1DDICAPS_D3D11_OPTIONS:
        case D3DWDDM2_0DDICAPS_MEMORY_ARCHITECTURE:
            return RxWarpAnswer(Args, (UINT64)0x0000000100000001ull);

        case D3D11_1DDICAPS_ARCHITECTURE_INFO:
        case D3DWDDM2_2DDICAPS_SHADERCACHE:
            return RxWarpAnswer(Args, (UINT)0);

        case D3D11_1DDICAPS_SHADER_MIN_PRECISION_SUPPORT:
            return RxWarpAnswer(Args, (UINT64)0x0000000300000003ull);

        case D3DWDDM1_3DDICAPS_D3D11_OPTIONS1:
            return RxWarpAnswer(Args, (UINT)7);

        case D3DWDDM1_3DDICAPS_MARKER:
            return RxWarpAnswer(Args, (UINT)2);

        case D3DWDDM2_0DDICAPS_D3D11_OPTIONS2:
            if (Args->DataSize == sizeof(UINT64))
                return RxWarpAnswer(Args, (UINT64)3);
            return RxWarpAnswer(Args, (UINT)3);

        case D3DWDDM2_0DDICAPS_TEXTURE_LAYOUT:
            if (Args->pInfo != nullptr || Args->DataSize != sizeof(LayoutSets))
                return E_UNEXPECTED;
            memcpy(Args->pData, LayoutSets, sizeof(LayoutSets));
            return S_OK;

        case D3DWDDM2_0DDICAPS_GPUVA_CAPS:
            return RxWarpAnswer(Args, (UINT)32);

        case D3DWDDM2_2DDICAPS_TEXTURE_LAYOUT:
            if (Args->pInfo != nullptr || Args->DataSize != sizeof(Layout))
                return E_UNEXPECTED;
            memcpy(Args->pData, Layout, sizeof(Layout));
            return S_OK;

        default:
            return E_UNEXPECTED;
    }
}
