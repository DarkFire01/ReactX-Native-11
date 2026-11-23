/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The kernel adapter behind each display
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

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

/* The HRESULT the runtime reports for a kernel status */
HRESULT
D9HresultFromStatus(
    _In_ NTSTATUS Status)
{
    switch (static_cast<ULONG>(Status))
    {
    case STATUS_SUCCESS:
    case 0xC01E0006:
        return S_OK;
    case STATUS_PENDING:
        return E_PENDING;
    case STATUS_INVALID_PARAMETER:
    case STATUS_INVALID_PARAMETER_1:
    case STATUS_INVALID_PARAMETER_2:
    case STATUS_INVALID_PARAMETER_3:
    case STATUS_INVALID_PARAMETER_4:
    case STATUS_INVALID_PARAMETER_5:
    case STATUS_INVALID_PARAMETER_6:
    case STATUS_INVALID_PARAMETER_7:
    case STATUS_INVALID_PARAMETER_8:
    case STATUS_INVALID_PARAMETER_9:
    case STATUS_INVALID_PARAMETER_10:
    case STATUS_INVALID_PARAMETER_11:
    case STATUS_INVALID_PARAMETER_12:
        return E_INVALIDARG;
    case STATUS_NO_MEMORY:
    case STATUS_COMMITMENT_LIMIT:
        return E_OUTOFMEMORY;
    case STATUS_INVALID_USER_BUFFER:
        return 0x88760879;
    case STATUS_ACCESS_DENIED:
        return D3DERR_NOTAVAILABLE;
    case STATUS_PRIVILEGED_INSTRUCTION:
        return 0x88760871;
    case STATUS_ILLEGAL_INSTRUCTION:
        return 0x88760872;
    case STATUS_INVALID_HANDLE:
    case 0xC01E0112:
    case 0xC01E0113:
    case 0xC01E0114:
    case 0xC01E0115:
        return 0x88760873;
    case STATUS_DEVICE_REMOVED:
        return D3DERR_DEVICEREMOVED;
    case 0xC01E0102:
        return 0x8876021C;
    case 0xC01E0100:
        return 0x8876017C;
    case 0xC01E0109:
        return 0xC0262109;
    case 0xC01E0111:
        return 0xC0262111;
    case 0xC01E0009:
        OutputDebugStringW(L"User/kernel-mode driver mismatch");
        DebugBreak();
        return E_FAIL;
    default:
        return E_FAIL;
    }
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
