/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     d3d11on12.dll, the Direct3D 11 driver that runs on a D3D12 device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"

extern const WCHAR RxTraceModule[] = L"d3d11on12";

/* The runtime opens the layer here instead of a vendor driver's OpenAdapter10_2 */
extern "C"
HRESULT
APIENTRY
OpenAdapter_D3D11On12(
    _Inout_ D3D10DDIARG_OPENADAPTER* Args,
    _Inout_ On12OpenArgs* On12)
{
    return O12Adapter::Open(Args, On12);
}
