/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What each format can do, from what the driver reports for it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* FORMAT_SUPPORT bits for a format given its driver bits; FALSE for a format the runtime does not know */
BOOLEAN
RxDeriveFormatSupport(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Driver,
    _Out_ UINT* Support);

/* FORMAT_SUPPORT2 bits; FALSE when there are none to report */
BOOLEAN
RxDeriveFormatSupport2(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Driver,
    _Out_ UINT* Support);
