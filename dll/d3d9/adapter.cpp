/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The kernel adapter behind each display
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

static
NTSTATUS
D9QueryKernel(
    _In_ D3DKMT_HANDLE Adapter,
    _In_ KMTQUERYADAPTERINFOTYPE Type,
    _Inout_updates_bytes_(Size) VOID* Data,
    _In_ UINT Size)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_QUERYADAPTERINFO Query = {};

    if (Kmt == nullptr || Kmt->QueryAdapterInfo == nullptr)
        return STATUS_NOT_SUPPORTED;

    Query.hAdapter = Adapter;
    Query.Type = Type;
    Query.pPrivateDriverData = Data;
    Query.PrivateDriverDataSize = Size;
    return Kmt->QueryAdapterInfo(&Query);
}

/* Asks for the user mode driver of one runtime family; only success matters to the callers */
BOOLEAN
D9QueryUmdName(
    _In_ D3DKMT_HANDLE Adapter,
    _In_ KMTUMDVERSION Version,
    _Out_ D3DKMT_UMDFILENAMEINFO* Name)
{
    ZeroMemory(Name, sizeof(*Name));
    Name->Version = Version;
    if (!NT_SUCCESS(D9QueryKernel(Adapter, KMTQAITYPE_UMDRIVERNAME, Name, sizeof(*Name))))
        return FALSE;

    Name->UmdFileName[_countof(Name->UmdFileName) - 1] = UNICODE_NULL;
    return TRUE;
}

VOID
D9OpenAdapter(
    _Inout_ D9Adapter* Adapter)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_OPENADAPTERFROMHDC Open = {};
    D3DKMT_UMDFILENAMEINFO Name;
    D3DKMT_CLOSEADAPTER Close;
    BOOLEAN IsDisplayName;
    NTSTATUS Status;

    Adapter->KernelAdapter = 0;
    Adapter->SourceId = 0;
    if (Kmt == nullptr || Kmt->OpenAdapterFromHdc == nullptr || Kmt->CloseAdapter == nullptr)
        return;

    /* "\\.\DISPLAYn" names a display device; anything else names a driver */
    IsDisplayName = Adapter->DeviceName[0] == '\\' && Adapter->DeviceName[1] == '\\' &&
                    Adapter->DeviceName[2] == '.';
    Open.hDc = CreateDCA(IsDisplayName ? nullptr : Adapter->DeviceName,
                         IsDisplayName ? Adapter->DeviceName : nullptr, nullptr, nullptr);

    Status = Kmt->OpenAdapterFromHdc(&Open);
    if (Open.hDc != NULL)
        DeleteDC(Open.hDc);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTOpenAdapterFromHdc on %S failed 0x%08lX", Adapter->DeviceName, Status);
        return;
    }

    Adapter->KernelAdapter = Open.hAdapter;
    Adapter->AdapterLuid = Open.AdapterLuid;
    Adapter->SourceId = Open.VidPnSourceId;

    /* An adapter without a Direct3D 9 or Direct3D 12 driver is not a WDDM display to this runtime */
    if (!D9QueryUmdName(Open.hAdapter, KMTUMDVERSION_DX9, &Name) &&
        !D9QueryUmdName(Open.hAdapter, KMTUMDVERSION_DX12, &Name))
    {
        D9Trace(L"%S has no user mode driver", Adapter->DeviceName);
        Close.hAdapter = Open.hAdapter;
        Kmt->CloseAdapter(&Close);
        Adapter->KernelAdapter = 0;
        Adapter->SourceId = 0;
        return;
    }

    D9State.HasWddmDriver = TRUE;
}

VOID
D9CloseAdapter(
    _Inout_ D9Adapter* Adapter)
{
    D3DKMT_CLOSEADAPTER Close;

    if (Adapter->KernelAdapter == 0)
        return;

    Close.hAdapter = Adapter->KernelAdapter;
    RxGetKmtThunks()->CloseAdapter(&Close);
    Adapter->KernelAdapter = 0;
}
