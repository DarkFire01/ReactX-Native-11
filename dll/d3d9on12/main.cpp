/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     d3d9on12.dll exports and the private table d3d9.dll opens adapters through
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9on12.hpp"

/* Without SetAppCompatData every limit is left open */
NineAppCompatInfo NineAppCompat = { 0, { ULONG_MAX, ULONG_MAX }, ULONG_MAX, ULONG_MAX, ULONG_MAX, 0 };

/* Private entries without an implementation yet report it once and answer zero */
template <UINT Slot>
static
UINT_PTR
APIENTRY
NineMissingEntry(VOID)
{
    NINE_TRACE_ONCE(L"private entry %u is not implemented", Slot);
    return 0;
}

/* Private entries that answer with an HRESULT say they are not done yet */
template <UINT Slot>
static
HRESULT
APIENTRY
NineMissingCall(VOID)
{
    NINE_TRACE_ONCE(L"private entry %u is not implemented", Slot);
    return E_NOTIMPL;
}

static const NinePrivateTable NinePrivate =
{
    NineOpenAdapter,
    reinterpret_cast<PVOID>(NineMissingEntry<1>),
    reinterpret_cast<PVOID>(NineMissingEntry<2>),
    reinterpret_cast<PVOID>(NineMissingEntry<3>),
    reinterpret_cast<PVOID>(NineMissingEntry<4>),
    reinterpret_cast<PVOID>(NineMissingEntry<5>),
    reinterpret_cast<PVOID>(NineMissingEntry<6>),
    reinterpret_cast<PVOID>(NineMissingEntry<7>),
    reinterpret_cast<PVOID>(NineMissingEntry<8>),
    reinterpret_cast<PVOID>(NineMissingEntry<9>),
    reinterpret_cast<PVOID>(NineMissingEntry<10>),
    reinterpret_cast<PVOID>(NineMissingEntry<11>),
    reinterpret_cast<PVOID>(NineMissingEntry<12>),
    reinterpret_cast<PVOID>(NineMissingCall<13>),
    reinterpret_cast<PVOID>(NineMissingEntry<14>),
    reinterpret_cast<PVOID>(NineMissingEntry<15>),
    reinterpret_cast<PVOID>(NineMissingEntry<16>),
    reinterpret_cast<PVOID>(NineMissingEntry<17>),
    reinterpret_cast<PVOID>(NineMissingEntry<18>),
    reinterpret_cast<PVOID>(NineMissingEntry<19>),
    reinterpret_cast<PVOID>(NineMissingEntry<20>),
    reinterpret_cast<PVOID>(NineMissingEntry<21>),
    reinterpret_cast<PVOID>(NineMissingEntry<22>),
    reinterpret_cast<PVOID>(NineMissingEntry<23>),
    reinterpret_cast<PVOID>(NineMissingEntry<24>),
    reinterpret_cast<PVOID>(NineMissingEntry<25>),
    reinterpret_cast<PVOID>(NineMissingEntry<26>),
    NineOpenAdapter2,
    reinterpret_cast<PVOID>(NineMissingEntry<28>),
    reinterpret_cast<PVOID>(NineMissingEntry<29>),
    reinterpret_cast<PVOID>(NineMissingEntry<30>),
};

/* DXGI codes the Direct3D 9 runtime knows under its own names */
HRESULT
NineDxgiResult(
    _In_ HRESULT Result)
{
    switch (Result)
    {
        case DXGI_ERROR_INVALID_CALL:
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
            return D3DERR_DRIVERINTERNALERROR;
        case DXGI_ERROR_DEVICE_REMOVED:
            return D3DERR_DEVICEREMOVED;
        case DXGI_ERROR_DEVICE_HUNG:
            return D3DERR_DEVICEHUNG;
        case DXGI_ERROR_DEVICE_RESET:
            return D3DERR_DEVICELOST;
        default:
            return Result;
    }
}

/* The regular entry; this driver is only reached through the private table */
EXTERN_C
HRESULT
APIENTRY
OpenAdapter(
    _Inout_ D3DDDIARG_OPENADAPTER* Args)
{
    UNREFERENCED_PARAMETER(Args);

    NineTrace(L"OpenAdapter called directly; only the private open is supported");
    return NineDxgiResult(E_NOTIMPL);
}

EXTERN_C
VOID
APIENTRY
GetPrivateDDITable(
    _Out_writes_bytes_all_(NineFirstTableSize) VOID* Table)
{
    CopyMemory(Table, &NinePrivate, NineFirstTableSize);
}

EXTERN_C
HRESULT
APIENTRY
GetPrivateDDITableVersioned(
    _Out_writes_bytes_(Size) VOID* Table,
    _In_ UINT Size)
{
    if (Size > sizeof(NinePrivate))
    {
        NineTrace(L"the runtime asks for a %u byte private table, more than the %u known", Size,
                  static_cast<UINT>(sizeof(NinePrivate)));
        return E_INVALIDARG;
    }

    CopyMemory(Table, &NinePrivate, Size);
    return NineDxgiResult(S_OK);
}

EXTERN_C
VOID
APIENTRY
SetAppCompatData(
    _In_reads_bytes_(sizeof(NineAppCompat)) const VOID* Data)
{
    CopyMemory(&NineAppCompat, Data, sizeof(NineAppCompat));
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ PVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);

    if (Reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(Instance);
    return TRUE;
}
