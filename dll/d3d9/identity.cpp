/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     GetAdapterIdentifier
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

/* The identifier GUID starts from the DirectDraw class and is mixed with the adapter's numbers */
static const GUID D9IdentifierBase = { 0xD7B70EE0, 0x4340, 0x11CF, { 0xB0, 0x63, 0x00, 0x20, 0xAF, 0xC2, 0xCD, 0x35 } };

/* Every user mode driver family the kernel may know a file for, asked in this order */
static constexpr UINT D9DriverNameFamilies = 6;

static constexpr DWORD D9IdentifierFlags = D3DENUM_WHQL_LEVEL | D3DENUM_NO_DRIVERVERSION;

/* Finds the first user mode driver the kernel names and puts its full path in Driver */
static
BOOLEAN
D9DriverPath(
    _In_ D3DKMT_HANDLE KernelAdapter,
    _Out_ D3DKMT_UMDFILENAMEINFO* Name,
    _Out_writes_z_(Size) PSTR Driver,
    _In_ int Size)
{
    for (UINT Family = 0; Family < D9DriverNameFamilies; Family++)
    {
        if (!D9QueryUmdName(KernelAdapter, static_cast<KMTUMDVERSION>(Family), Name))
            continue;

        if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, Name->UmdFileName, -1, Driver, Size, nullptr,
                                nullptr) != 0)
        {
            return TRUE;
        }
        D9Trace(L"cannot convert the user mode driver name");
        return FALSE;
    }

    D9Trace(L"the kernel names no user mode driver for this adapter");
    return FALSE;
}

/* Reads the file version of the driver into DriverVersion */
static
VOID
D9DriverFileVersion(
    _In_z_ PCWSTR Path,
    _Inout_ D3DADAPTER_IDENTIFIER9* Identifier)
{
    VS_FIXEDFILEINFO* Fixed = nullptr;
    UINT FixedSize = 0;
    DWORD Size;
    VOID* Block;

    Size = GetFileVersionInfoSizeExW(FILE_VER_GET_LOCALISED, Path, nullptr);
    if (Size == 0)
        return;

    Block = LocalAlloc(LPTR, Size);
    if (Block == nullptr)
        return;

    if (GetFileVersionInfoExW(FILE_VER_GET_LOCALISED | FILE_VER_GET_NEUTRAL, Path, 0, Size, Block) &&
        VerQueryValueW(Block, L"\\", reinterpret_cast<VOID**>(&Fixed), &FixedSize))
    {
        Identifier->DriverVersion.HighPart = static_cast<LONG>(Fixed->dwFileVersionMS);
        Identifier->DriverVersion.LowPart = Fixed->dwFileVersionLS;
    }
    LocalFree(Block);
}

static
VOID
D9DescribeAdapter(
    _In_ const D9Adapter* Adapter,
    _In_ DWORD Flags,
    _Out_ D3DADAPTER_IDENTIFIER9* Identifier)
{
    RxAdapterIdentity* Identity = new (std::nothrow) RxAdapterIdentity;
    D3DKMT_UMDFILENAMEINFO Name;
    BOOLEAN Known = FALSE;
    CHAR BaseName[sizeof(Identifier->Driver)];
    PCSTR Slash;
    DWORD Mix[2];

    ZeroMemory(Identifier, sizeof(*Identifier));

    if (Identity != nullptr && RxReadAdapterIdentity(Adapter->AdapterLuid, Adapter->SourceId, Identity))
    {
        Identifier->VendorId = Identity->VendorId;
        Identifier->DeviceId = Identity->DeviceId;
        Identifier->SubSysId = Identity->SubSysId;
        Identifier->Revision = Identity->Revision;
        Known = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, Identity->Description, -1, Identifier->Description,
                                    sizeof(Identifier->Description), nullptr, nullptr) != 0 &&
                WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, Identity->DeviceName, -1, Identifier->DeviceName,
                                    sizeof(Identifier->DeviceName), nullptr, nullptr) != 0;
    }
    delete Identity;

    if (Known)
    {
        if (D9DriverPath(Adapter->KernelAdapter, &Name, Identifier->Driver, sizeof(Identifier->Driver)) &&
            (Flags & D3DENUM_NO_DRIVERVERSION) == 0)
        {
            D9DriverFileVersion(Name.UmdFileName, Identifier);
        }

        Slash = strrchr(Identifier->Driver, '\\');
        if (Slash != nullptr)
        {
            strcpy_s(BaseName, Slash + 1);
            strcpy_s(Identifier->Driver, BaseName);
        }
    }

    Identifier->DeviceIdentifier = D9IdentifierBase;
    Identifier->DeviceIdentifier.Data1 ^= Identifier->VendorId;
    Identifier->DeviceIdentifier.Data2 ^= LOWORD(Identifier->DeviceId);
    Identifier->DeviceIdentifier.Data3 ^= HIWORD(Identifier->DeviceId);
    memcpy(Mix, Identifier->DeviceIdentifier.Data4, sizeof(Mix));
    Mix[0] ^= Identifier->DriverVersion.LowPart ^ Identifier->SubSysId;
    Mix[1] ^= static_cast<DWORD>(Identifier->DriverVersion.HighPart) ^ Identifier->Revision;
    memcpy(Identifier->DeviceIdentifier.Data4, Mix, sizeof(Mix));

    /* No WHQL lookup is made any more; asking for the level reports a signed driver */
    Identifier->WHQLLevel = (Flags & D3DENUM_WHQL_LEVEL) != 0;
}

STDMETHODIMP
D9Enum::GetAdapterIdentifier(
    _In_ UINT Adapter,
    _In_ DWORD Flags,
    _Out_ D3DADAPTER_IDENTIFIER9* Identifier)
{
    D9LockScope Scope(&Lock);

    if ((Flags & ~D9IdentifierFlags) != 0)
    {
        D9_TRACE_ONCE(L"GetAdapterIdentifier: invalid flags 0x%08lX", Flags);
        return D3DERR_INVALIDCALL;
    }
    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"GetAdapterIdentifier: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }

    D9DescribeAdapter(&Adapters[Adapter], Flags, Identifier);
    return S_OK;
}
