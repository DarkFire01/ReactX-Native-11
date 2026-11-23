/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     System GPU preference through the shared DirectX settings helper
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

/* One adapter as the settings helper wants to see it */
struct DC_PREFERENCE_ADAPTER
{
    UINT Index;
    UINT Adapter;
    LUID AdapterLuid;
    UINT64 VendorId;
    UINT64 DriverVersion;
    UINT64 Paravirtualized;
};

static_assert(sizeof(DC_PREFERENCE_ADAPTER) == 40, "the helper reads 40 byte records");

/* The escape request the helper passes, laid out like D3DDDICB_ESCAPE */
struct DC_HELPER_ESCAPE
{
    HANDLE Device;
    D3DDDI_ESCAPEFLAGS Flags;
    void* PrivateData;
    UINT PrivateDataSize;
    HANDLE Context;
};

typedef HRESULT (APIENTRY* PFN_DC_ESCAPE_CALLBACK)(void*, const DC_HELPER_ESCAPE*);
typedef HRESULT (WINAPI* PFN_DC_FINAL_PREFERENCE)(const DC_PREFERENCE_ADAPTER*, size_t, PFN_DC_ESCAPE_CALLBACK,
                                                  void*, void*, UINT*, UINT*, UINT*);
typedef HRESULT (WINAPI* PFN_DC_USER_SETTINGS)(UINT, void*, void*, void*, UINT);

static INIT_ONCE DcHelperOnce = INIT_ONCE_STATIC_INIT;
static PFN_DC_FINAL_PREFERENCE DcFinalPreference;
static PFN_DC_USER_SETTINGS DcUserSettings;

static
BOOL
CALLBACK
DcLoadHelper(
    _Inout_ PINIT_ONCE InitOnce,
    _Inout_opt_ PVOID Parameter,
    _Out_opt_ PVOID* Context)
{
    HMODULE Helper = LoadLibraryExW(L"ext-ms-win-dx-dxdbhelper-l1-1-0.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

    if (Helper == NULL)
    {
        DcTrace(L"the DirectX settings helper is not present (%lu)", GetLastError());
        return TRUE;
    }

    DcUserSettings = reinterpret_cast<PFN_DC_USER_SETTINGS>(GetProcAddress(Helper, MAKEINTRESOURCEA(101)));
    DcFinalPreference = reinterpret_cast<PFN_DC_FINAL_PREFERENCE>(GetProcAddress(Helper, MAKEINTRESOURCEA(102)));
    return TRUE;
}

/* Lets the helper ask a driver about its hybrid state */
static
HRESULT
APIENTRY
DcPreferenceEscape(
    _In_ void* Adapter,
    _In_ const DC_HELPER_ESCAPE* Escape)
{
    D3DKMT_ESCAPE Request = {};

    Request.hAdapter = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(Adapter));
    Request.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Request.Flags = Escape->Flags;
    Request.pPrivateDriverData = Escape->PrivateData;
    Request.PrivateDriverDataSize = Escape->PrivateDataSize;
    return DcStatusHresult(DcKmtCall(DcKmt()->Escape, &Request));
}

HRESULT
DcQueryGpuPreference(
    _In_reads_(Count) DcAdapter* const* Adapters,
    _In_ UINT Count,
    _Out_ UINT* Preference,
    _Out_ UINT* Index,
    _Out_ UINT* Factor)
{
    std::vector<DC_PREFERENCE_ADAPTER> Records(Count);

    InitOnceExecuteOnce(&DcHelperOnce, DcLoadHelper, nullptr, nullptr);
    if (DcFinalPreference == nullptr)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);

    for (UINT Slot = 0; Slot < Count; Slot++)
    {
        Records[Slot].Index = Slot;
        Records[Slot].Adapter = Adapters[Slot]->Handle;
        Records[Slot].AdapterLuid = Adapters[Slot]->Luid;
        Records[Slot].VendorId = Adapters[Slot]->HardwareId.vendorID;
        Records[Slot].DriverVersion = Adapters[Slot]->DriverVersion;
        Records[Slot].Paravirtualized = Adapters[Slot]->Traits.IsParavirtualized;
    }

    return DcFinalPreference(Records.data(), Records.size(), DcPreferenceEscape, nullptr, nullptr, Preference, Index,
                             Factor);
}

HRESULT
DcQueryUserSetting(
    _In_ UINT Setting,
    _Out_writes_bytes_(Size) void* Buffer,
    _In_ UINT Size)
{
    InitOnceExecuteOnce(&DcHelperOnce, DcLoadHelper, nullptr, nullptr);
    if (DcUserSettings == nullptr)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);

    return DcUserSettings(Setting, nullptr, nullptr, Buffer, Size);
}
