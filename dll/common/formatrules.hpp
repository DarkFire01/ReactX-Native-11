/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 11 format rules: what each feature level requires and what the driver decides
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* Driver interface generations whose format rules differ */
enum RxDdiGeneration
{
    RxDdiD3D11_1 = 2,
    RxDdiWddm1_3 = 3,
    RxDdiWddm2_0 = 4,
    RxDdiWddm2_3 = 7,
};

/* Format capture output; FORMAT_SUPPORT reports it, though d3d11.h has no name for it */
#define RX_FORMAT_SUPPORT_CAPTURE 0x80000000u

/* The format exists unless the driver answers D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED */
#define RX_RULE_DRIVER_MAY_REFUSE 0x1u
/* Output merger logic ops always work on the format */
#define RX_RULE_LOGIC_OP 0x2u
/* Logic ops work when the device supports them at all */
#define RX_RULE_LOGIC_OP_IF_CAPABLE 0x4u

struct RxFormatRule
{
    DXGI_FORMAT Format;

    /* FORMAT_SUPPORT bits every driver at this level has */
    UINT Required;

    /* FORMAT_SUPPORT bits the driver's own format caps decide */
    UINT Optional;

    UINT Flags;
};

/* The rule generation for the newest interface a driver lists */
RxDdiGeneration
RxGetDdiGeneration(
    _In_ UINT Interface);

/* The rule for a format at a feature level; nullptr when the format does not exist there */
const RxFormatRule*
RxFindFormatRule(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ RxDdiGeneration Generation,
    _In_ DXGI_FORMAT Format);

/* FORMAT_SUPPORT bits from a rule and the driver's caps for the format; 0 means unsupported */
UINT
RxApplyFormatRule(
    _In_ const RxFormatRule* Rule,
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ UINT Driver,
    _In_ BOOLEAN IsSoftware);
