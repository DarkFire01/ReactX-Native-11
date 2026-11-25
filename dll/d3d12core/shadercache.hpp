/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The shader cache the runtime keeps for drivers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* Turns on the driver's shader caching feature and hands it the runtime's store */
HRESULT
RxEnableShaderCache12(
    _In_ const D3D12DDI_EXTENDED_FEATURES_FUNCS_0096& Functions,
    _In_ D3D12DDI_HDEVICE Device);
