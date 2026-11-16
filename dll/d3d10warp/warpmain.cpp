/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DLL entry and OpenAdapter10_2
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "format.hpp"

static CRITICAL_SECTION RxWarpDdiLock;
VOID *RxWarpLastDevice;

RxWarpLock::RxWarpLock()
{
    EnterCriticalSection(&RxWarpDdiLock);
}

RxWarpLock::~RxWarpLock()
{
    LeaveCriticalSection(&RxWarpDdiLock);
}

#ifdef REACTX_WARP_TRACE
VOID
RxWarpTrace(
    _In_z_ PCSTR Format,
    ...)
{
    CHAR Buffer[512];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf_s(Buffer, _TRUNCATE, Format, Arguments);
    va_end(Arguments);
    OutputDebugStringA(Buffer);
}
#endif

extern "C"
HRESULT
APIENTRY
OpenAdapter10_2(
    _Inout_ D3D10DDIARG_OPENADAPTER *Args)
{
    RxWarpLock Lock;

    return RxWarpAdapter::Open(Args);
}

/*
 * Private commands used by Direct2D and the compositor, exported by ordinal only. The
 * Direct3D runtime looks this ordinal up to know a software module is WARP, which then runs
 * on the Basic Render adapter instead of needing its own kernel thunks. No command is
 * implemented yet.
 */
extern "C"
VOID
APIENTRY
RxWarpPrivateApi(
    _In_opt_ VOID *Device,
    _Inout_ VOID *Command)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Command);
}

/* The software adapter path asks for the device it just created */
extern "C"
HRESULT
APIENTRY
D3D11RefGetLastCreation(
    _Out_ VOID **Device)
{
    RxWarpLock Lock;

    if (Device == nullptr)
        return E_INVALIDARG;
    *Device = RxWarpLastDevice;
    return S_OK;
}

/* Direct3D 9 DDI entry; dxgi looks it up when it opens a software adapter. Not implemented yet */
extern "C"
HRESULT
APIENTRY
OpenAdapter(
    _Inout_ VOID *Args)
{
    UNREFERENCED_PARAMETER(Args);
    return E_NOTIMPL;
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_ LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);

    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        InitializeCriticalSection(&RxWarpDdiLock);
        RxWarpInitFormats();
    }
    else if (Reason == DLL_PROCESS_DETACH && Reserved == nullptr)
    {
        DeleteCriticalSection(&RxWarpDdiLock);
    }
    return TRUE;
}
