/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The system keyboard device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "keyboard.hpp"

#include <stdlib.h>

BYTE DiKeyHardwareCodes[256];
const BYTE* DiKeyTranslation;

/* The first keyboard raw input reports with a type DirectInput knows, else the system answer */
DWORD
DiGetKeyboardType(
    _In_ INT Which)
{
    RAWINPUTDEVICELIST* List;
    UINT Count = 0;
    DWORD Found = 0;

    if (GetRawInputDeviceList(nullptr, &Count, sizeof(*List)) == static_cast<UINT>(-1) || Count == 0)
        return GetKeyboardType(Which);
    List = static_cast<RAWINPUTDEVICELIST*>(malloc(Count * sizeof(*List)));
    if (List == nullptr)
        return GetKeyboardType(Which);
    if (GetRawInputDeviceList(List, &Count, sizeof(*List)) != static_cast<UINT>(-1))
    {
        for (UINT Index = 0; Index < Count; Index++)
        {
            RID_DEVICE_INFO Info;
            UINT NameSize = 0;
            UINT Size = sizeof(Info);

            if (List[Index].dwType != RIM_TYPEKEYBOARD)
                continue;
            if (GetRawInputDeviceInfoW(List[Index].hDevice, RIDI_DEVICENAME, nullptr, &NameSize) != 0)
                continue;
            ZeroMemory(&Info, sizeof(Info));
            Info.cbSize = sizeof(Info);
            if (GetRawInputDeviceInfoW(List[Index].hDevice, RIDI_DEVICEINFO, &Info, &Size) == static_cast<UINT>(-1) ||
                Which > 1)
            {
                continue;
            }
            if (Info.keyboard.dwType == 4 || Info.keyboard.dwType == 7 || Info.keyboard.dwType == 8)
            {
                Found = Which == 0 ? Info.keyboard.dwType : Info.keyboard.dwSubType;
                break;
            }
        }
    }
    free(List);
    if (Found == 0)
        return GetKeyboardType(Which);
    return Found;
}

class DiKeyboard : public DiBackend
{
public:
    DI_FORMAT NativeFormat;
    DI_OBJECT Objects[256];
    DWORD SubType;
    DWORD Emulation;

    const DI_FORMAT*
    Format(VOID) override
    {
        return &NativeFormat;
    }

    BOOLEAN
    Emulated(VOID) override
    {
        return TRUE;
    }

    HRESULT
    GetCapabilities(
        _Inout_ LPDIDEVCAPS Caps) override
    {
        Caps->dwFlags = Emulation != 0 ? DIDC_ATTACHED | DIDC_ALIAS : DIDC_ATTACHED;
        Caps->dwDevType = (LOBYTE(SubType) << 8) | DI8DEVTYPE_KEYBOARD;
        Caps->dwButtons = NativeFormat.ObjectCount;
        return S_OK;
    }

    HRESULT
    GetDeviceInfo(
        _Inout_ LPDIDEVICEINSTANCEW Info) override
    {
        Info->guidProduct = GUID_SysKeyboard;
        Info->dwDevType = (LOBYTE(SubType) << 8) | DI8DEVTYPE_KEYBOARD;
        LoadStringW(DiInstance, IDS_KEYBOARD, Info->tszProductName, MAX_PATH);
        LoadStringW(DiInstance, IDS_KEYBOARD, Info->tszInstanceName, MAX_PATH);
        return S_OK;
    }

    HRESULT
    RunControlPanel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override
    {
        UNREFERENCED_PARAMETER(Window);
        UNREFERENCED_PARAMETER(Flags);
        return DiLaunchControlPanel(L"keyboard");
    }

    HRESULT
    Build(
        _In_ REFGUID Guid);
};

/*
 * Picks the layout family and the hardware translation for a Japanese keyboard, which
 * reports its maker in the high byte of the subtype.
 */
static
HRESULT
DiPickJapaneseLayout(
    _Inout_ DWORD* SubType,
    _Out_ BYTE* Families,
    _Out_ const BYTE** Translation)
{
    DWORD Detail = DiGetKeyboardType(1);
    UINT Table;

    *Families = DI_KEYS_JAPANESE;
    if (HIBYTE(Detail) == 13)
    {
        UINT Plain = 1;
        UINT Alternate = 7;

        *SubType = 7;
        if (LOBYTE(Detail) == 4)
        {
            *SubType = 8;
        }
        else if (LOBYTE(Detail) == 5)
        {
            Plain = 2;
            Alternate = 8;
            *SubType = 9;
        }
        Table = MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC) == 1 ? Alternate : Plain;
    }
    else if (Detail == 0)
    {
        *SubType = 4;
        *Families = DI_KEYS_ENHANCED;
        *Translation = nullptr;
        return S_OK;
    }
    else if (Detail == 1)
    {
        Table = 4;
        *SubType = 11;
    }
    else if (Detail >= 13 && Detail <= 15)
    {
        Table = 5;
        *SubType = 12;
    }
    else
    {
        Table = 3;
        *SubType = 10;
    }
    *Translation = DiKeyTranslations[Table - 1];
    return S_OK;
}

HRESULT
DiKeyboard::Build(
    _In_ REFGUID Guid)
{
    const BYTE* Translation = nullptr;
    BOOLEAN Used[256] = {};
    BYTE Families;
    DWORD Count = 0;

    SubType = DiGetKeyboardType(0);
    switch (SubType)
    {
        case 1:
            Families = DI_KEYS_XT;
            break;
        case 3:
            Families = DI_KEYS_AT;
            break;
        case 7:
            DiPickJapaneseLayout(&SubType, &Families, &Translation);
            break;
        default:
            Families = DI_KEYS_ENHANCED;
            break;
    }

    if (IsEqualGUID(Guid, GUID_SysKeyboardEm))
        Emulation = DI_EMULATE_KEYBOARD;
    else if (IsEqualGUID(Guid, GUID_SysKeyboardEm2))
        Emulation = DI_EMULATE_KEYBOARD_HOOK;
    else
        Emulation = 0;

    for (UINT Hardware = 0; Hardware < 256; Hardware++)
    {
        BYTE Code = Translation != nullptr ? Translation[Hardware] : static_cast<BYTE>(Hardware);
        BYTE Family = DiKeyFamilies[Code] & Families;

        if (Family == 0 || Used[Code])
            continue;
        Used[Code] = TRUE;
        Objects[Count].Guid = &GUID_Key;
        Objects[Count].Offset = Code;
        Objects[Count].Type = DIDFT_MAKEINSTANCE(Code) | ((Family & DI_KEYS_TOGGLE) ? DIDFT_TGLBUTTON : DIDFT_PSHBUTTON);
        Objects[Count].Flags = 0;

        /* Pause and Num Lock trade places on enhanced keyboards */
        if (Families == DI_KEYS_ENHANCED && (Hardware & 0x7F) == 0x45)
            DiKeyHardwareCodes[Count] = static_cast<BYTE>(Hardware ^ 0x80);
        else
            DiKeyHardwareCodes[Count] = static_cast<BYTE>(Hardware);
        Count++;
    }

    NativeFormat.DataSize = 256;
    NativeFormat.ObjectCount = Count;
    NativeFormat.Objects = Objects;
    if (DiKeyTranslation == nullptr)
        DiKeyTranslation = Translation != nullptr ? Translation : DiKeyTranslations[DI_KEY_TABLE_IDENTITY - 1];
    return S_OK;
}

HRESULT
DiCreateKeyboard(
    _In_ REFGUID Guid,
    _Outptr_ DiBackend** Backend)
{
    DiKeyboard* Keyboard = new (std::nothrow) DiKeyboard();
    HRESULT Result;

    *Backend = nullptr;
    if (Keyboard == nullptr)
        return E_OUTOFMEMORY;
    Result = Keyboard->Build(Guid);
    if (FAILED(Result))
    {
        delete Keyboard;
        return Result;
    }
    *Backend = Keyboard;
    return S_OK;
}
