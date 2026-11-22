/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What each format can do on this device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "formatrules.hpp"
#include "formatsupport.hpp"

/* Every UAV atomic operation FORMAT_SUPPORT2 reports */
#define RX_SUPPORT2_UAV_ATOMICS (D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS | \
                                 D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE | \
                                 D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE | \
                                 D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX | \
                                 D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX)

UINT
RxDevice::GetDriverFormatCaps(
    _In_ DXGI_FORMAT Format)
{
    UINT Driver = 0;

    AcquireSRWLockExclusive(&FormatLock);
    if (!(FormatAsked[Format / 64] & (1ull << (Format % 64))))
    {
        DeviceFuncs.pfnCheckFormatSupport(DriverDevice, Format, &Driver);
        FormatDriverCaps[Format] = Driver;
        FormatAsked[Format / 64] |= 1ull << (Format % 64);
    }
    Driver = FormatDriverCaps[Format];
    ReleaseSRWLockExclusive(&FormatLock);

    return Driver;
}

HRESULT
RxDevice::GetFormatSupport(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* Support)
{
    const RxFormatRule* Rule;
    UINT Driver = 0;

    *Support = 0;

    /* Formats the feature level does not have never reach the driver */
    Rule = RxFindFormatRule(FeatureLevel, RxGetDdiGeneration(Adapter->GetNewestDriverInterface()), Format);
    if (Rule == nullptr)
        return E_FAIL;

    /* The driver is only asked about what it decides */
    if (Rule->Optional != 0 || (Rule->Flags & RX_RULE_DRIVER_MAY_REFUSE))
    {
        Driver = GetDriverFormatCaps(Format);
        if ((Rule->Flags & RX_RULE_DRIVER_MAY_REFUSE) && Driver == D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED)
            return E_FAIL;
    }

    *Support = RxApplyFormatRule(Rule, FeatureLevel, Driver, Adapter->IsSoftware());
    return *Support != 0 ? S_OK : E_FAIL;
}

HRESULT
RxDevice::GetFormatSupport2(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* Support)
{
    D3D11_1DDI_D3D11_OPTIONS_DATA Options = {};
    const RxFormatRule* Rule;
    UINT First;
    UINT Driver = 0;
    UINT Bits;

    *Support = 0;

    /* The first query fails for formats this device does not have */
    if (FAILED(GetFormatSupport(Format, &First)))
        return E_FAIL;

    Rule = RxFindFormatRule(FeatureLevel, RxGetDdiGeneration(Adapter->GetNewestDriverInterface()), Format);
    if (Format != DXGI_FORMAT_UNKNOWN)
        Driver = GetDriverFormatCaps(Format);

    /* Atomics, typed loads, tiling, sharing, display and overlays come per format */
    RxDeriveFormatSupport2(Format, Driver, &Bits);
    if (FeatureLevel < D3D_FEATURE_LEVEL_11_0)
        Bits &= ~(RX_SUPPORT2_UAV_ATOMICS | D3D11_FORMAT_SUPPORT2_TILED);
    if (Format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP && FeatureLevel < D3D_FEATURE_LEVEL_11_1)
        Bits &= ~D3D11_FORMAT_SUPPORT2_SHAREABLE;

    /* Typed UAV access follows the first query, so the two never disagree */
    Bits &= ~(D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE | D3D11_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP);
    if (First & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)
        Bits |= D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    else
        Bits &= ~D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD;

    if (Rule->Flags & RX_RULE_LOGIC_OP)
    {
        Bits |= D3D11_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP;
    }
    else if ((Rule->Flags & RX_RULE_LOGIC_OP_IF_CAPABLE) && FeatureLevel >= D3D_FEATURE_LEVEL_10_0)
    {
        Adapter->GetCaps(D3D11_1DDICAPS_D3D11_OPTIONS, &Options, sizeof(Options));
        if (Options.OutputMergerLogicOp)
            Bits |= D3D11_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP;
    }

    *Support = Bits;
    return Bits != 0 ? S_OK : E_FAIL;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CheckFormatSupport(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* pFormatSupport)
{
    if (pFormatSupport == nullptr)
        return E_INVALIDARG;

    return GetFormatSupport(Format, pFormatSupport);
}
