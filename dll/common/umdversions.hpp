/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runtime versions the Direct3D 9 family offers a user mode driver's OpenAdapter
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* Newest first; OpenAdapter takes the first one the driver accepts */
static const UINT RxD3D9UmdVersions[] =
{
    D3D_UMD_INTERFACE_VERSION_WDDM3_2_2, D3D_UMD_INTERFACE_VERSION_WDDM3_1_1, D3D_UMD_INTERFACE_VERSION_WDDM3_0_1,
    D3D_UMD_INTERFACE_VERSION_WDDM2_9_1, D3D_UMD_INTERFACE_VERSION_WDDM2_8_1, D3D_UMD_INTERFACE_VERSION_WDDM2_7_2,
    D3D_UMD_INTERFACE_VERSION_WDDM2_6_4, D3D_UMD_INTERFACE_VERSION_WDDM2_5_3, D3D_UMD_INTERFACE_VERSION_WDDM2_4_2,
    D3D_UMD_INTERFACE_VERSION_WDDM2_3_2, D3D_UMD_INTERFACE_VERSION_WDDM2_2_2, D3D_UMD_INTERFACE_VERSION_WDDM2_1_4,
    D3D_UMD_INTERFACE_VERSION_WDDM2_0_M2_2, D3D_UMD_INTERFACE_VERSION_WDDM2_0_M1_3, D3D_UMD_INTERFACE_VERSION_WDDM1_3,
    D3D_UMD_INTERFACE_VERSION_WIN8, D3D_UMD_INTERFACE_VERSION_WIN7,
};

/* WDDM 1.2 drivers from AMD and Qualcomm are only offered the Windows 8 and Windows 7 versions */
static
inline
UINT
RxFirstD3D9UmdVersion(
    _In_ UINT DriverModel,
    _In_ DWORD VendorId)
{
    if (DriverModel == 1200 && (VendorId == 0x1002 || VendorId == 0x4D4F4351))
        return _countof(RxD3D9UmdVersions) - 2;
    return 0;
}
