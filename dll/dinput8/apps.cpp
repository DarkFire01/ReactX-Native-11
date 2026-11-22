/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Records the applications that use DirectInput and reads their compatibility settings
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8.hpp"

#include <strsafe.h>

/* What the last notification told listeners about this process */
enum DI_APP_STATE
{
    DiAppNone = 0,
    DiAppNew = 1,
    DiAppKnown = 2,
    DiAppMapper = 3,
};

static DWORD DiLastNotice;
static DWORD DiAppDate;
static DWORD DiAppLength;

DI_APP_HACKS DiAppHacks = { 0, 0, MAX_PATH };

/* The link time stamp of the executable and its file size identify the build */
static
BOOLEAN
DiReadAppIdentity(
    _In_z_ PCWSTR Path)
{
    IMAGE_DOS_HEADER Dos;
    BYTE Nt[264];
    DWORD Read;
    HANDLE File;
    BOOLEAN Found = FALSE;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (File == INVALID_HANDLE_VALUE || File == nullptr)
        return FALSE;
    if (ReadFile(File, &Dos, sizeof(Dos), &Read, nullptr) && Read == sizeof(Dos) && Dos.e_magic == IMAGE_DOS_SIGNATURE &&
        SetFilePointer(File, Dos.e_lfanew, nullptr, FILE_BEGIN) == static_cast<DWORD>(Dos.e_lfanew) &&
        ReadFile(File, Nt, sizeof(Nt), &Read, nullptr) && Read == sizeof(Nt) &&
        reinterpret_cast<IMAGE_NT_HEADERS*>(Nt)->Signature == IMAGE_NT_SIGNATURE)
    {
        DiAppDate = reinterpret_cast<IMAGE_NT_HEADERS*>(Nt)->FileHeader.TimeDateStamp;
        DiAppLength = GetFileSize(File, nullptr);
        Found = DiAppLength != INVALID_FILE_SIZE;
    }
    CloseHandle(File);
    return Found;
}

static
HRESULT
DiOpenWritableKey(
    _In_ HKEY Parent,
    _In_z_ PCWSTR Name,
    _Out_ HKEY* Key)
{
    LONG Error = RegCreateKeyExW(Parent, Name, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, Key, nullptr);

    if (Error == ERROR_SUCCESS)
        return S_OK;
    return HRESULT_FROM_WIN32(Error);
}

static
VOID
DiSetString(
    _In_ HKEY Key,
    _In_z_ PCWSTR Name,
    _In_z_ PCWSTR Value)
{
    RegSetValueExW(Key, Name, 0, REG_SZ, reinterpret_cast<const BYTE*>(Value), (lstrlenW(Value) + 1) * sizeof(WCHAR));
}

/* Per application overrides live under Compatibility, keyed like the application record */
static
VOID
DiReadAppHacks(
    _In_z_ PCWSTR Id)
{
    static const PCWSTR Names[] = { L"ReAcquire", L"NoSubClass", L"MaxDeviceNameLength" };
    DWORD* Fields[] = { &DiAppHacks.ReAcquire, &DiAppHacks.NoSubClass, &DiAppHacks.MaxNameLength };
    HKEY Base;
    HKEY App = nullptr;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\DirectInput\\Compatibility", 0, KEY_READ, &Base) !=
        ERROR_SUCCESS)
    {
        return;
    }
    RegOpenKeyExW(Base, Id, 0, KEY_READ, &App);
    RegCloseKey(Base);
    if (App == nullptr)
        return;
    for (UINT Index = 0; Index < ARRAYSIZE(Names); Index++)
    {
        DWORD Value;
        DWORD Size = sizeof(Value);

        if (RegQueryValueExW(App, Names[Index], nullptr, nullptr, reinterpret_cast<BYTE*>(&Value), &Size) == ERROR_SUCCESS &&
            Size == sizeof(Value))
        {
            *Fields[Index] = Value;
        }
    }
    RegCloseKey(App);
}

VOID
DiRegisterApplication(
    _In_ DWORD Version,
    _In_ BOOLEAN UsesMapper)
{
    WCHAR Module[MAX_PATH];
    WCHAR FullPath[MAX_PATH];
    WCHAR Id[MAX_PATH + 16];
    PWSTR FileName = nullptr;
    HKEY Base = nullptr;
    HKEY Key = nullptr;
    DWORD State = DiAppNone;
    DWORD Mapper = UsesMapper ? 1 : 0;
    DWORD Size;

    if (DiLastNotice == DiAppMapper)
        return;

    EnterCriticalSection(&DiLock);
    if (GetModuleFileNameW(GetModuleHandleW(nullptr), Module, _countof(Module)) == 0 ||
        GetFullPathNameW(Module, _countof(FullPath), FullPath, &FileName) == 0 || FileName == nullptr)
    {
        goto Exit;
    }
    if (DiLastNotice == DiAppNone && !DiReadAppIdentity(FullPath))
        goto Exit;
    if (FAILED(DiOpenWritableKey(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectInput", &Base)))
        goto Exit;

    {
        DWORD Date = DiAppDate;
        DWORD Length = DiAppLength;
        DWORD IdFlags = 0;

        Size = sizeof(IdFlags);
        if (RegQueryValueExW(Base, L"AppIdFlag", nullptr, nullptr, reinterpret_cast<BYTE*>(&IdFlags), &Size) ==
            ERROR_SUCCESS)
        {
            if (IdFlags & 1)
                Date = 0;
            if (IdFlags & 2)
                Length = 0;
        }
        CharUpperW(FileName);
        StringCbPrintfW(Id, sizeof(Id), L"%s%8.8lX%8.8lX", FileName, Date, Length);
    }
    if (DiLastNotice == DiAppNone)
        DiReadAppHacks(Id);

    if (RegOpenKeyExW(Base, Id, 0, KEY_READ, &Key) != ERROR_SUCCESS)
    {
        State = DiAppNew;
    }
    else
    {
        DWORD Stored = 0;

        Size = sizeof(Stored);
        if (RegQueryValueExW(Key, L"UsesMapper", nullptr, nullptr, reinterpret_cast<BYTE*>(&Stored), &Size) !=
            ERROR_SUCCESS)
        {
            State = DiAppNew;
        }
        else if (Stored != 0)
        {
            Mapper = 1;
            State = DiAppMapper;
        }
        else
        {
            State = DiAppKnown;
        }
        RegCloseKey(Key);
        Key = nullptr;
    }

    if (State == DiAppNew || (State == DiAppKnown && Mapper != 0))
    {
        if (FAILED(DiOpenWritableKey(Base, Id, &Key)))
        {
            State = DiAppNone;
            goto Close;
        }
        DiSetString(Key, L"Name", FileName);
        RegSetValueExW(Key, L"UsesMapper", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&Mapper), sizeof(Mapper));
        RegCloseKey(Key);
        Key = nullptr;
        if (Mapper != 0)
            State = DiAppMapper;
    }

    if (DiLastNotice == State ||
        FAILED(DiOpenWritableKey(Base, Mapper ? L"MostRecentMapperApplication" : L"MostRecentApplication", &Key)))
    {
        State = DiAppNone;
    }
    else
    {
        FILETIME Now;

        GetSystemTimeAsFileTime(&Now);
        DiLastNotice = State;
        DiSetString(Key, L"Name", FileName);
        DiSetString(Key, L"Id", Id);
        RegSetValueExW(Key, L"Version", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&Version), sizeof(Version));
        RegSetValueExW(Key, L"MostRecentStart", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&Now), sizeof(Now));
        RegCloseKey(Key);
    }

Close:
    RegCloseKey(Base);
Exit:
    LeaveCriticalSection(&DiLock);
    if (State != DiAppNone)
        PostMessageW(HWND_BROADCAST, DiNotifyMessage, State, 0);
}
