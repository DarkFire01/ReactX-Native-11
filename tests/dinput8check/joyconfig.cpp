/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Read only checks of IDirectInputJoyConfig8
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8check.hpp"

static
BOOL
CALLBACK
DcEnumJoyTypes(
    _In_ LPCWSTR Name,
    _In_opt_ VOID* Context)
{
    printf("    type '%s'\n", DcWide(Name).c_str());
    return ++*static_cast<ULONG*>(Context) < 64;
}

/* Nothing here changes the joystick settings: every call that writes is made where it fails first */
VOID
DcCheckJoyConfig(VOID)
{
    static const PCWSTR Types[] = { L"#2", L"#4", L"#9", L"#<", L"#=", L"VID_045E&PID_0000", L"NoSuchType", L"" };
    static const DWORD TypeSizes[] = { sizeof(DIJOYTYPEINFO_DX5), sizeof(DIJOYTYPEINFO_DX6), sizeof(DIJOYTYPEINFO), 100 };
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInputJoyConfig8* Config = nullptr;
    DIJOYTYPEINFO Type;
    DIJOYCONFIG Joy;
    DIJOYUSERVALUES User;
    HKEY Key = nullptr;
    ULONG Count = 0;
    HRESULT Result;

    if (Wide == nullptr)
        return;
    Result = Wide->QueryInterface(IID_IDirectInputJoyConfig8, reinterpret_cast<VOID**>(&Config));
    printf("joyconfig = %08lX\n", Result);
    if (FAILED(Result))
    {
        Wide->Release();
        return;
    }

    printf("  acquire uncoop = %08lX\n", Config->Acquire());
    printf("  unacquire = %08lX\n", Config->Unacquire());
    printf("  coop null = %08lX\n", Config->SetCooperativeLevel(nullptr, DISCL_EXCLUSIVE | DISCL_BACKGROUND));
    printf("  coop fg = %08lX\n", Config->SetCooperativeLevel(DcWindow, DISCL_EXCLUSIVE | DISCL_FOREGROUND));
    printf("  coop desktop = %08lX\n", Config->SetCooperativeLevel(GetDesktopWindow(), DISCL_EXCLUSIVE | DISCL_BACKGROUND));
    printf("  sendnotify = %08lX\n", Config->SendNotify());
    Result = Config->EnumTypes(DcEnumJoyTypes, &Count);
    printf("  enumtypes = %08lX count %lu\n", Result, Count);
    printf("  enumtypes null = %08lX\n", Config->EnumTypes(nullptr, nullptr));

    for (PCWSTR Name : Types)
    {
        for (DWORD Size : TypeSizes)
        {
            memset(&Type, 0xCC, sizeof(Type));
            Type.dwSize = Size;
            Result = Config->GetTypeInfo(Name, &Type, DITC_REGHWSETTINGS | DITC_DISPLAYNAME);
            printf("  typeinfo '%s' %lu = %08lX flags %08lX", DcWide(Name).c_str(), Size, Result, Type.hws.dwFlags);
            if (SUCCEEDED(Result))
                printf(" name '%s'", DcWide(Type.wszDisplayName).c_str());
            printf("\n");
        }
    }
    for (DWORD Size : TypeSizes)
    {
        for (DWORD Flags : { 0x0Ful, 0x10ul, 0x3Ful, 0x40ul, 0xFFul, 0x100ul })
        {
            memset(&Type, 0xCC, sizeof(Type));
            Type.dwSize = Size;
            Result = Config->GetTypeInfo(L"#2", &Type, Flags);
            printf("  typeinfo size %lu flags %#lx = %08lX", Size, Flags, Result);
            if (SUCCEEDED(Result))
            {
                printf(" hws %08lX %08lX callout %04X", Type.hws.dwFlags, Type.hws.dwNumButtons, Type.wszCallout[0]);
                if (Size >= sizeof(DIJOYTYPEINFO_DX6))
                    printf(" hwid '%s' flags1 %08lX", (Flags & DITC_HARDWAREID) ? DcWide(Type.wszHardwareId).c_str() : "-", Type.dwFlags1);
                if (Size >= sizeof(DIJOYTYPEINFO))
                    printf(" flags2 %08lX map %04X", Type.dwFlags2, Type.wszMapFile[0]);
            }
            printf("\n");
        }
    }
    memset(&Type, 0, sizeof(Type));
    Type.dwSize = sizeof(Type);
    printf("  typeinfo null = %08lX\n", Config->GetTypeInfo(L"#2", nullptr, 0));
    printf("  typeinfo null name = %08lX\n", Config->GetTypeInfo(nullptr, &Type, 0));
    printf("  settypeinfo unacquired = %08lX\n", Config->SetTypeInfo(L"Test", &Type, 0, nullptr));
    printf("  deletetype unacquired = %08lX\n", Config->DeleteType(L"Test"));

    for (UINT Id = 0; Id < 18; Id += (Id < 4) ? 1 : 6)
    {
        memset(&Joy, 0, sizeof(Joy));
        Joy.dwSize = sizeof(Joy);
        Result = Config->GetConfig(Id, &Joy, DIJC_REGHWCONFIGTYPE | DIJC_GUIDINSTANCE | DIJC_CALLOUT | DIJC_GAIN);
        printf("  getconfig %u = %08lX type '%s' gain %lu\n", Id, Result, DcWide(Joy.wszType).c_str(), Joy.dwGain);
    }
    memset(&Joy, 0, sizeof(Joy));
    Joy.dwSize = sizeof(DIJOYCONFIG_DX5);
    printf("  getconfig dx5 = %08lX\n", Config->GetConfig(0, &Joy, DIJC_REGHWCONFIGTYPE));
    printf("  getconfig dx5 badflags = %08lX\n", Config->GetConfig(0, &Joy, DIJC_WDMGAMEPORT));
    Joy.dwSize = sizeof(DIJOYCONFIG);
    printf("  getconfig badflags = %08lX\n", Config->GetConfig(0, &Joy, 0x20));
    Joy.dwSize = 12;
    printf("  getconfig badsize = %08lX\n", Config->GetConfig(0, &Joy, 0));
    printf("  getconfig null = %08lX\n", Config->GetConfig(0, nullptr, 0));
    printf("  setconfig unacquired = %08lX\n", Config->SetConfig(0, &Joy, 0));
    printf("  deleteconfig unacquired = %08lX\n", Config->DeleteConfig(0));

    memset(&User, 0, sizeof(User));
    User.dwSize = sizeof(User);
    printf("  uservalues = %08lX\n", Config->GetUserValues(&User, DIJU_USERVALUES | DIJU_GLOBALDRIVER | DIJU_GAMEPORTEMULATOR));
    printf("  uservalues badflags = %08lX\n", Config->GetUserValues(&User, 8));
    User.dwSize = 8;
    printf("  uservalues badsize = %08lX\n", Config->GetUserValues(&User, DIJU_USERVALUES));
    User.dwSize = sizeof(User);
    printf("  setuservalues unacquired = %08lX\n", Config->SetUserValues(&User, DIJU_USERVALUES));
    printf("  addnewhardware badwnd = %08lX\n", Config->AddNewHardware(reinterpret_cast<HWND>(0x1234), GUID_NULL));

    Result = Config->OpenTypeKey(L"#2", KEY_READ, &Key);
    printf("  opentypekey predef = %08lX\n", Result);
    Result = Config->OpenTypeKey(L"NoSuchType", KEY_ALL_ACCESS, &Key);
    printf("  opentypekey write unacquired = %08lX\n", Result);
    printf("  opentypekey null = %08lX\n", Config->OpenTypeKey(L"x", KEY_READ, nullptr));
    Key = nullptr;
    Result = Config->OpenAppStatusKey(&Key);
    printf("  openappstatuskey = %08lX %s\n", Result, Key != nullptr ? "key" : "null");
    if (SUCCEEDED(Result) && Key != nullptr)
        RegCloseKey(Key);
    printf("  openappstatuskey null = %08lX\n", Config->OpenAppStatusKey(nullptr));

    printf("  release = %lu\n", Config->Release());
    printf("  input release = %lu\n", Wide->Release());
}
