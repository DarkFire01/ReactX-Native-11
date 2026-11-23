/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The immediate device and its frame latency
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "internal.hpp"

On12Device::On12Device(
    _In_ D3D11On12::Adapter* Adapter,
    _Inout_ D3D10DDIARG_CREATEDEVICE* Args)
    : D3D11On12::Device(Adapter, Args)
    , m_PresentContext(m_ImmediateContext)
{
    m_pDXGITable->pfnPresent1 = Present1;
    m_pDDITable->pfnDestroyDevice = DestroyDevice;
}

void
APIENTRY
On12Device::DestroyDevice(
    _In_ D3D10DDI_HDEVICE Device)
{
    On12Device* Self = static_cast<On12Device*>(D3D11On12::Device::CastFrom(Device));

    try
    {
        Self->~On12Device();
    }
    catch (_com_error& Error)
    {
        On12Trace(L"device destruction failed with 0x%08lX", Error.Error());
    }
    catch (std::bad_alloc&)
    {
        On12Trace(L"device destruction ran out of memory");
    }
}

STDMETHODIMP_(void)
On12Device::SetMaximumFrameLatency(
    _In_ UINT MaxFrameLatency) noexcept
{
    m_PresentContext.Latency.SetMaximumFrameLatency(MaxFrameLatency);
}

STDMETHODIMP_(bool)
On12Device::IsMaximumFrameLatencyReached() noexcept
{
    try
    {
        return m_PresentContext.Latency.IsMaximumFrameLatencyReached();
    }
    catch (_com_error&)
    {
        return false;
    }
    catch (std::bad_alloc&)
    {
        return false;
    }
}
