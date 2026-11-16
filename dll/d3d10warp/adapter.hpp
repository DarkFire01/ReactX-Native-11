/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The adapter object behind OpenAdapter10_2
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* What the Basic Render kernel adapter hands its user mode driver as private data */
typedef struct _RX_WARP_KERNEL_INFO
{
    UINT Reserved;
    UINT DdiVersion;
    UINT Flags;
} RX_WARP_KERNEL_INFO;

/* Kernel DDI versions from WDDM 2.0 on unlock the newer interfaces and feature level 12 */
static constexpr UINT RxWarpKernelWddm2 = 0x2000;

class RxWarpAdapter
{
public:
    static
    HRESULT
    Open(
        _Inout_ D3D10DDIARG_OPENADAPTER *Args);

    BOOLEAN
    HasWddm2Kernel() const
    {
        return Kernel.DdiVersion >= RxWarpKernelWddm2;
    }

    D3D10DDI_HRTADAPTER RuntimeAdapter = {};
    D3DDDI_ADAPTERCALLBACKS Callbacks = {};
    UINT Interface = 0;
    UINT Version = 0;
    UINT ProcessorCount = 1;
    RX_WARP_KERNEL_INFO Kernel = {};

private:
    static
    SIZE_T
    APIENTRY
    CalcPrivateDeviceSize(
        _In_ D3D10DDI_HADAPTER Adapter,
        _In_ const D3D10DDIARG_CALCPRIVATEDEVICESIZE *Args);

    static
    HRESULT
    APIENTRY
    CreateDevice(
        _In_ D3D10DDI_HADAPTER Adapter,
        _In_ D3D10DDIARG_CREATEDEVICE *Args);

    static
    HRESULT
    APIENTRY
    CloseAdapter(
        _In_ D3D10DDI_HADAPTER Adapter);

    static
    HRESULT
    APIENTRY
    GetSupportedVersions(
        _In_ D3D10DDI_HADAPTER Adapter,
        _Inout_ UINT32 *Entries,
        _Out_writes_opt_(*Entries) UINT64 *Versions);

    static
    HRESULT
    APIENTRY
    GetCaps(
        _In_ D3D10DDI_HADAPTER Adapter,
        _In_ const D3D10_2DDIARG_GETCAPS *Args);
};
