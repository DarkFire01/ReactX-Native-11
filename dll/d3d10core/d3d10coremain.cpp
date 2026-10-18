/*
 * PROJECT:     ReactX Direct3D 10 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     d3d10core.dll and d3d10_1core.dll: what is left of the Direct3D 10 core layer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "rxcommon.hpp"

#include <d3d10umddi.h>

#ifdef RX_D3D10_1
extern const WCHAR RxTraceModule[] = L"d3d10_1core";
#else
extern const WCHAR RxTraceModule[] = L"d3d10core";
#endif

/*
 * Devices come from d3d10.dll and d3d10_1.dll through d3d11.dll now, so the old core
 * entry points (create, supported versions, register layers) all answer not implemented.
 */
extern "C"
HRESULT
WINAPI
RxCoreNotImplemented(VOID)
{
    return E_NOTIMPL;
}

/* 10.1, build 6000 in the low half: what Windows reports */
extern "C"
UINT64
WINAPI
D3D10CoreGetVersion(VOID)
{
    return 0x000A000100041770ull;
}

/*
 * The kernel thunks of an old software rasterizer driver. They describe a device that
 * never draws, which is all a caller still loading this as a driver can get.
 */

extern "C"
NTSTATUS
APIENTRY
RxCoreAcceptKernelCall(
    _In_opt_ void* Arguments)
{
    UNREFERENCED_PARAMETER(Arguments);
    return STATUS_SUCCESS;
}

/* The user mode driver name is this module; adapter type queries report nothing */
extern "C"
NTSTATUS
APIENTRY
RxCoreQueryAdapterInfo(
    _Inout_ D3DKMT_QUERYADAPTERINFO* Query)
{
    D3DKMT_UMDFILENAMEINFO* Name;
    HMODULE Self = NULL;

    if (Query->Type == KMTQAITYPE_UMDRIVERNAME)
    {
        Name = static_cast<D3DKMT_UMDFILENAMEINFO*>(Query->pPrivateDriverData);
        if (Name->Version != KMTUMDVERSION_DX10)
            return STATUS_NOT_IMPLEMENTED;

        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&RxCoreQueryAdapterInfo), &Self);
        if (GetModuleFileNameW(Self, Name->UmdFileName, MAX_PATH) == 0)
            return STATUS_UNSUCCESSFUL;
        return STATUS_SUCCESS;
    }

    if (Query->Type == KMTQAITYPE_ADAPTERTYPE)
        static_cast<D3DKMT_ADAPTERTYPE*>(Query->pPrivateDriverData)->Value = 0;

    return STATUS_SUCCESS;
}

/* The device is always fine and never presents */
extern "C"
NTSTATUS
APIENTRY
RxCoreGetDeviceState(
    _Inout_ D3DKMT_GETDEVICESTATE* State)
{
    switch (State->StateType)
    {
        case D3DKMT_DEVICESTATE_EXECUTION:
            State->ExecutionState = D3DKMT_DEVICEEXECUTION_ACTIVE;
            return STATUS_SUCCESS;
        case D3DKMT_DEVICESTATE_PRESENT:
            ZeroMemory(&State->PresentState, sizeof(State->PresentState));
            return STATUS_SUCCESS;
        default:
            return STATUS_UNSUCCESSFUL;
    }
}

/* Any DC opens one made up adapter, under a fresh LUID; the handle only has to be nonzero */
extern "C"
NTSTATUS
APIENTRY
RxCoreOpenAdapterFromHdc(
    _Inout_ D3DKMT_OPENADAPTERFROMHDC* Open)
{
    Open->hAdapter = 1;
    AllocateLocallyUniqueId(&Open->AdapterLuid);
    return STATUS_SUCCESS;
}

/* No shared resources exist on it, for either open or query */
extern "C"
NTSTATUS
APIENTRY
RxCoreRefuseResource(
    _Inout_opt_ void* Arguments)
{
    UNREFERENCED_PARAMETER(Arguments);
    return STATUS_INVALID_PARAMETER;
}

/* Opening it as a driver is handed to d3d11.dll when that offers it */
static
HRESULT
RxOpenAdapterThroughD3D11(
    _In_z_ PCSTR Name,
    _Inout_ D3D10DDIARG_OPENADAPTER* Args)
{
    HMODULE D3d11 = LoadLibraryExW(L"d3d11.dll", NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                                        LOAD_LIBRARY_SEARCH_SYSTEM32);
    PFND3D10DDI_OPENADAPTER Open;

    if (D3d11 == NULL)
        return E_NOTIMPL;

    Open = reinterpret_cast<PFND3D10DDI_OPENADAPTER>(GetProcAddress(D3d11, Name));
    return Open ? Open(Args) : E_NOTIMPL;
}

extern "C"
HRESULT
APIENTRY
RxCoreOpenAdapter10(
    _Inout_ D3D10DDIARG_OPENADAPTER* Args)
{
    return RxOpenAdapterThroughD3D11("OpenAdapter10", Args);
}

extern "C"
HRESULT
APIENTRY
RxCoreOpenAdapter10_2(
    _Inout_ D3D10DDIARG_OPENADAPTER* Args)
{
    return RxOpenAdapterThroughD3D11("OpenAdapter10_2", Args);
}
