/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What the d3d12.dll loader files share
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* The d3d12core.dll beside this module and the table it hands back */
_Ret_maybenull_
const RxD3D12CoreExports*
RxGetCore(VOID);

/* A fresh ID3D12SDKConfiguration object; no output only checks */
HRESULT
RxGetSdkConfiguration(
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvObject);
