/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Opening the adapter and creating the immediate device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "internal.hpp"

SIZE_T
APIENTRY
On12CalcPrivateDeviceSize(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ const D3D10DDIARG_CALCPRIVATEDEVICESIZE* Args)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Args);

    return sizeof(On12Device);
}

/* The device presents through D3D12 itself, so DXGI never needs to redirect it */
HRESULT
APIENTRY
On12CreateDevice(
    _In_ D3D10DDI_HADAPTER Adapter,
    _Inout_ D3D10DDIARG_CREATEDEVICE* Args)
{
    D3D11On12::Adapter* Owner = D3D11On12::Adapter::CastFrom(Adapter);

    try
    {
        new (Args->hDrvDevice.pDrvPrivate) On12Device(Owner, Args);
        ThrowFailure(Owner->m_pUnderlyingDevice->QueryInterface(&Owner->m_pCompatDevice));
    }
    catch (_com_error& Error)
    {
        On12Trace(L"device creation failed with 0x%08lX", Error.Error());
        return Error.Error();
    }
    catch (std::bad_alloc&)
    {
        On12Trace(L"device creation ran out of memory");
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

/* Exported as OpenAdapter_D3D11On12 */
extern "C"
HRESULT
WINAPI
On12OpenAdapter(
    _Inout_ D3D10DDIARG_OPENADAPTER* Args,
    _Inout_ D3D11On12::SOpenAdapterArgs* On12Args)
{
    D3D11On12::Adapter* Adapter;

    try
    {
        Adapter = new D3D11On12::Adapter(Args, *On12Args);
    }
    catch (_com_error& Error)
    {
        On12Trace(L"adapter open failed with 0x%08lX", Error.Error());
        return Error.Error();
    }
    catch (std::bad_alloc&)
    {
        On12Trace(L"adapter open ran out of memory");
        return E_OUTOFMEMORY;
    }

    Args->pAdapterFuncs_2->pfnCalcPrivateDeviceSize = On12CalcPrivateDeviceSize;
    Args->pAdapterFuncs_2->pfnCreateDevice = On12CreateDevice;
    Args->hAdapter = Adapter->GetHandle();
    return S_OK;
}
