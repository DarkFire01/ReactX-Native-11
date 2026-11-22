/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Allocation, registry, string and window helpers the DirectInput libraries share
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput.hpp"

#include <aclapi.h>
#include <strsafe.h>

HRESULT
DiAllocate(
    _In_ SIZE_T Size,
    _Inout_ PVOID* Block)
{
    PVOID Grown;

    if (Size == 0)
    {
        DiFree(Block);
        return S_OK;
    }

    if (*Block == nullptr)
        Grown = LocalAlloc(LPTR, Size);
    else
        Grown = LocalReAlloc(*Block, Size, LMEM_MOVEABLE | LMEM_ZEROINIT);

    if (Grown == nullptr)
        return E_OUTOFMEMORY;

    *Block = Grown;
    return S_OK;
}

VOID
DiFree(
    _Inout_ PVOID* Block)
{
    if (*Block != nullptr)
        LocalFree(*Block);
    *Block = nullptr;
}

HRESULT
DiCheckString(
    _In_opt_z_ PCWSTR Text,
    _In_ ULONG Limit)
{
    ULONG Length = (Text != nullptr) ? lstrlenW(Text) : 0;

    if (Length != 0)
        return (Length < Limit) ? S_OK : E_INVALIDARG;
    if (Text == nullptr || *Text != L'\0')
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
DiCheckStringA(
    _In_opt_z_ PCSTR Text,
    _In_ ULONG Limit)
{
    ULONG Length = (Text != nullptr) ? lstrlenA(Text) : 0;

    if (Length != 0)
        return (Length < Limit) ? S_OK : E_INVALIDARG;
    if (Text == nullptr || *Text != '\0')
        return E_INVALIDARG;
    return S_OK;
}

static
HRESULT
DiWin32Result(
    _In_ LONG Error)
{
    if (Error == ERROR_SUCCESS)
        return S_OK;

    /* Missing keys read as a plain file not found */
    if (Error == ERROR_KEY_DELETED || Error == ERROR_BADKEY)
        Error = ERROR_FILE_NOT_FOUND;
    return HRESULT_FROM_WIN32(Error);
}

/* Creates a key that every user may write, the way DirectInput shares its settings */
static
LONG
DiCreateOpenKey(
    _In_ HKEY Root,
    _In_z_ PCWSTR Path,
    _In_ DWORD Options,
    _Out_ HKEY* Key)
{
    SID_IDENTIFIER_AUTHORITY WorldAuthority = SECURITY_WORLD_SID_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    EXPLICIT_ACCESSW Access[3] = {};
    SECURITY_ATTRIBUTES Attributes;
    SECURITY_DESCRIPTOR Descriptor;
    PSID Sids[3] = {};
    PACL Acl = nullptr;
    DWORD Disposition;
    LONG Error = ERROR_OUTOFMEMORY;

    if (!AllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &Sids[0]) ||
        !AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &Sids[1]) ||
        !AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &Sids[2]))
    {
        goto Done;
    }

    for (ULONG Index = 0; Index < _countof(Access); Index++)
    {
        Access[Index].grfAccessPermissions = KEY_ALL_ACCESS;
        Access[Index].grfAccessMode = SET_ACCESS;
        Access[Index].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        Access[Index].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        Access[Index].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
        Access[Index].Trustee.ptstrName = static_cast<LPWSTR>(Sids[Index]);
    }

    if (SetEntriesInAclW(_countof(Access), Access, nullptr, &Acl) != ERROR_SUCCESS)
        goto Done;

    if (InitializeSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorDacl(&Descriptor, TRUE, Acl, FALSE))
    {
        Attributes.nLength = sizeof(Attributes);
        Attributes.lpSecurityDescriptor = &Descriptor;
        Attributes.bInheritHandle = TRUE;
        Error = RegCreateKeyExW(Root, Path, 0, nullptr, Options, KEY_ALL_ACCESS, &Attributes, Key, &Disposition);
    }

Done:
    for (ULONG Index = 0; Index < _countof(Sids); Index++)
    {
        if (Sids[Index] != nullptr)
            FreeSid(Sids[Index]);
    }
    if (Acl != nullptr)
        LocalFree(Acl);
    return Error;
}

HRESULT
DiOpenKey(
    _In_ HKEY Root,
    _In_z_ PCWSTR Path,
    _In_ REGSAM Access,
    _In_ DWORD Options,
    _Out_ HKEY* Key)
{
    LONG Error;

    if ((Access & (KEY_SET_VALUE | KEY_CREATE_SUB_KEY | MAXIMUM_ALLOWED)) == 0)
        return DiWin32Result(RegOpenKeyExW(Root, Path, 0, Access, Key));

    Error = RegOpenKeyExW(Root, Path, 0, Access, Key);
    if (Error == ERROR_SUCCESS)
        return S_OK;

    /* Fall back to reading when writing is not allowed */
    if (Error == ERROR_ACCESS_DENIED)
        return DiWin32Result(RegOpenKeyExW(Root, Path, 0, KEY_READ, Key));

    return DiWin32Result(DiCreateOpenKey(Root, Path, Options, Key));
}

HRESULT
DiReadValue(
    _In_ HKEY Key,
    _In_z_ PCWSTR Name,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    DWORD Length = Size;
    PVOID Whole = nullptr;
    LONG Error;
    HRESULT hr;

    Error = RegQueryValueExW(Key, Name, nullptr, nullptr, static_cast<PBYTE>(Buffer), &Length);
    if (Error == ERROR_SUCCESS)
    {
        if (Length == Size)
            return S_OK;
        ZeroMemory(static_cast<PBYTE>(Buffer) + Length, Size - Length);
        return S_FALSE;
    }

    if (Error != ERROR_MORE_DATA)
    {
        ZeroMemory(Buffer, Size);
        return DiWin32Result(Error);
    }

    /* Longer than the caller wants, keep the leading part */
    hr = DiAllocate(Length, &Whole);
    if (FAILED(hr))
        return hr;

    Error = RegQueryValueExW(Key, Name, nullptr, nullptr, static_cast<PBYTE>(Whole), &Length);
    if (Error == ERROR_SUCCESS)
    {
        CopyMemory(Buffer, Whole, Size);
        hr = S_OK;
    }
    else
    {
        ZeroMemory(Buffer, Size);
        hr = HRESULT_FROM_WIN32(Error);
    }
    DiFree(&Whole);
    return hr;
}

BOOLEAN
DiReadDword(
    _In_ HKEY Key,
    _In_z_ PCWSTR Name,
    _Out_ PULONG Value)
{
    return DiReadValue(Key, Name, Value, sizeof(*Value)) == S_OK;
}

LONG
DiReadString(
    _In_ HKEY Key,
    _In_opt_z_ PCWSTR Name,
    _Out_writes_(Count) PWSTR Buffer,
    _In_ ULONG Count)
{
    DWORD Size = Count * sizeof(WCHAR);
    DWORD Type = 0;
    ULONG Length;
    LONG Error;

    Error = RegQueryValueExW(Key, Name, nullptr, &Type, reinterpret_cast<PBYTE>(Buffer), &Size);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (Type != REG_SZ)
        return ERROR_INVALID_DATA;

    /* Terminate strings stored without their null, when there is room */
    Length = Size / sizeof(WCHAR);
    if (Length == 0)
    {
        Buffer[0] = L'\0';
    }
    else if (Buffer[Length - 1] != L'\0')
    {
        if (Length >= Count)
            return ERROR_MORE_DATA;
        Buffer[Length] = L'\0';
    }
    return ERROR_SUCCESS;
}

static
PCWSTR
DiTakeHex(
    _In_ PCWSTR Text,
    _In_ ULONG Digits,
    _Out_ PULONG Value)
{
    *Value = 0;
    for (ULONG Index = 0; Index < Digits; Index++)
    {
        WCHAR Char = Text[Index];
        ULONG Nibble;

        if (Char >= L'0' && Char <= L'9')
            Nibble = Char - L'0';
        else if ((Char | 0x20) >= L'a' && (Char | 0x20) <= L'f')
            Nibble = (Char | 0x20) - L'a' + 10;
        else
            return nullptr;
        *Value = (*Value << 4) | Nibble;
    }
    return Text + Digits;
}

BOOLEAN
DiParseGuid(
    _In_z_ PCWSTR Text,
    _Out_ LPGUID Guid)
{
    /* Digits per field and the character that has to follow it */
    static const UCHAR Digits[] = { 8, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2 };
    static const WCHAR After[] = { L'-', L'-', L'-', 0, L'-', 0, 0, 0, 0, 0, L'}' };
    ULONG Fields[_countof(Digits)];
    PCWSTR Cursor;

    if (lstrlenW(Text) != 38 || Text[0] != L'{')
        return FALSE;

    Cursor = Text + 1;
    for (ULONG Index = 0; Index < _countof(Digits); Index++)
    {
        Cursor = DiTakeHex(Cursor, Digits[Index], &Fields[Index]);
        if (Cursor == nullptr)
            return FALSE;
        if (After[Index] != 0)
        {
            if (*Cursor != After[Index])
                return FALSE;
            Cursor++;
        }
    }

    Guid->Data1 = Fields[0];
    Guid->Data2 = static_cast<USHORT>(Fields[1]);
    Guid->Data3 = static_cast<USHORT>(Fields[2]);
    for (ULONG Index = 0; Index < 8; Index++)
        Guid->Data4[Index] = static_cast<UCHAR>(Fields[3 + Index]);
    return TRUE;
}

HRESULT
DiRunControlPanel(
    _In_z_ PCWSTR Arguments)
{
    STARTUPINFOW Startup = {};
    PROCESS_INFORMATION Process = {};
    WCHAR Command[MAX_PATH];

    Startup.cb = sizeof(Startup);
    StringCchPrintfW(Command, _countof(Command), L"control.exe %s", Arguments);
    if (!CreateProcessW(nullptr, Command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Startup, &Process))
    {
        DWORD Error = GetLastError();

        DiTrace(L"control panel launch failed, error %lu", Error);
        return HRESULT_FROM_WIN32(Error);
    }

    CloseHandle(Process.hProcess);
    CloseHandle(Process.hThread);
    return S_OK;
}

DWORD
DiWindowProcess(
    _In_opt_ HWND Window)
{
    DWORD Process = 0;

    if (!IsWindow(Window) || GetWindowThreadProcessId(Window, &Process) == 0)
        return 0;
    return Process;
}

BOOL
DiIsWindowOurs(
    _In_opt_ HWND Window)
{
    return DiWindowProcess(Window) == GetCurrentProcessId();
}
