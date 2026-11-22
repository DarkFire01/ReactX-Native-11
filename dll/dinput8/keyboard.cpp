/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The system keyboard device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "keyboard.hpp"
#include "input.hpp"

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

/* Keys whose names come from the string table rather than the keyboard layout */
static const BYTE DiFixedNameKeys[] =
{
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x90, 0x99, 0xA0, 0xA1, 0xA2, 0xA4,
    0xAE, 0xB0, 0xB2, 0xDE, 0xDF, 0xE3, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED,
};

/* Keys left nameless when the layout has no name for them */
static
BOOLEAN
DiKeyMayBeNameless(
    _In_ BYTE Code)
{
    return Code == 0x56 || Code == 0x73 || Code == 0x7E;
}

/*
 * Names a key: digits and media keys from the string table, printable keys in the active
 * layout as their upper case character, the rest from the system key name.
 */
static
HRESULT
DiGetKeyName(
    _In_ DWORD Index,
    _In_ DWORD Type,
    _Out_writes_(MAX_PATH) PWSTR Name)
{
    BYTE Code = DiKeyHardwareCodes[Index];
    BOOLEAN Fixed = FALSE;

    Name[0] = L'\0';
    for (BYTE Key : DiFixedNameKeys)
    {
        if (Key == Code)
        {
            Fixed = TRUE;
            break;
        }
    }
    if (!Fixed)
    {
        DWORD Process = 0;
        HKL Layout = GetKeyboardLayout(GetWindowThreadProcessId(GetActiveWindow(), &Process));
        BYTE Keys[256] = {};
        UINT Virtual;

        if (Code <= 0x53)
        {
            Virtual = MapVirtualKeyExW(Code, MAPVK_VSC_TO_VK_EX, Layout);
            if (ToUnicodeEx(Virtual, Code, Keys, Name, MAX_PATH, 0, Layout) == 1 &&
                (iswctype(Name[0], _ALPHA) || iswctype(Name[0], _PUNCT)))
            {
                Name[0] = towupper(Name[0]);
                Name[1] = L'\0';
                goto Done;
            }
        }
        Name[0] = L'\0';
        GetKeyNameTextW(((Code & 0x7F) | ((Code & 0x80) << 1)) << 16, Name, MAX_PATH);
    }
    if (Name[0] == L'\0' && !DiKeyMayBeNameless(Code))
        LoadStringW(DiInstance, IDS_KEY_FIRST + DIDFT_GETINSTANCE(Type), Name, MAX_PATH);

Done:
    return Name[0] != L'\0' ? S_OK : DIERR_OBJECTNOTFOUND;
}

class DiKeyboard : public DiBackend
{
public:
    DI_FORMAT NativeFormat;
    DI_OBJECT Objects[256];
    DWORD SubType;
    DWORD Level;
    DI_EM_INSTANCE* Instance = nullptr;

    ~DiKeyboard() override
    {
        if (Instance != nullptr)
            DiEmRelease(Instance);
    }

    DI_EM_INSTANCE*
    Emulation(VOID) override
    {
        return Instance;
    }

    HRESULT
    GetObjectInfo(
        _In_ DWORD Index,
        _In_ DWORD Type,
        _Inout_ LPDIDEVICEOBJECTINSTANCEW Info) override
    {
        if (Index >= NativeFormat.ObjectCount)
            return E_INVALIDARG;
        ZeroMemory(Info->tszName, sizeof(Info->tszName));
        return DiGetKeyName(Index, Type, Info->tszName);
    }

    HRESULT
    GetProperty(
        _In_ const DI_PROPERTY* Property,
        _Inout_ LPDIPROPHEADER Header) override
    {
        if (Property->Id == 20)
        {
            LPDIPROPSTRING String = reinterpret_cast<LPDIPROPSTRING>(Header);

            ZeroMemory(String->wsz, sizeof(String->wsz));
            return DiGetKeyName(Property->Index, Property->Type, String->wsz);
        }
        if (Property->Id == 23)
        {
            DWORD Code = DiKeyHardwareCodes[Property->Index];

            if (Code == 0xC5)
                Code = 0x451DE1;
            else if (Code & 0x80)
                Code = ((Code & 0x7F) << 8) | 0xE0;
            reinterpret_cast<LPDIPROPDWORD>(Header)->dwData = Code;
            return DI_OK;
        }
        return E_NOTIMPL;
    }

    /* Exclusive use needs the foreground, and so does hiding the Windows keys */
    HRESULT
    SetCooperativeLevel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override
    {
        UNREFERENCED_PARAMETER(Window);
        if (Instance->Flags & DI_EM_KEYBOARD_HOOK)
        {
            if (Flags & DISCL_BACKGROUND)
                return E_NOTIMPL;
        }
        else if (Flags & DISCL_EXCLUSIVE)
        {
            if (!(Flags & DISCL_FOREGROUND))
                return E_NOTIMPL;
            Instance->Flags |= DI_EM_EXCLUSIVE | DI_EM_NOWINKEY;
            return DI_OK;
        }
        else
        {
            Instance->Flags &= ~DI_EM_EXCLUSIVE;
        }
        Instance->Flags &= ~DI_EM_NOWINKEY;
        if (Flags & DISCL_NOWINKEY)
        {
            if (!(Flags & DISCL_FOREGROUND))
                return E_NOTIMPL;
            Instance->Flags |= DI_EM_NOWINKEY;
        }
        return DI_OK;
    }

    /* Taking the keyboard exclusively lets go of every key the system still thinks is down */
    HRESULT
    Acquire(VOID) override
    {
        if (Instance->Flags & DI_EM_EXCLUSIVE)
        {
            for (UINT Key = VK_BACK; Key < 0xFE; Key++)
            {
                if ((Key == VK_CAPITAL || Key == VK_KANA || Key == VK_KANJI) || !(GetAsyncKeyState(Key) & 0x8000))
                    continue;
                keybd_event(static_cast<BYTE>(Key), 0, KEYEVENTF_KEYUP, 0);
            }
        }
        return S_FALSE;
    }

    HRESULT
    GetDeviceState(
        _Out_ VOID* State) override
    {
        if (!(Instance->Flags & DI_EM_ACQUIRED))
            return DIERR_INPUTLOST;
        EnterCriticalSection(&DiLock);
        CopyMemory(State, DiKeyboardClass.State, 256);
        LeaveCriticalSection(&DiLock);
        return DI_OK;
    }

    const DI_FORMAT*
    Format(VOID) override
    {
        return &NativeFormat;
    }

    HRESULT
    GetCapabilities(
        _Inout_ LPDIDEVCAPS Caps) override
    {
        Caps->dwFlags = Level != 0 ? DIDC_ATTACHED | DIDC_ALIAS : DIDC_ATTACHED;
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
        Level = DI_EMULATE_KEYBOARD;
    else if (IsEqualGUID(Guid, GUID_SysKeyboardEm2))
        Level = DI_EMULATE_KEYBOARD_HOOK;
    else
        Level = 0;

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
    return DiEmCreate(&DiKeyboardClass, &NativeFormat,
                      Level == DI_EMULATE_KEYBOARD_HOOK ? DI_EM_KEYBOARD_HOOK : DI_EM_KEYBOARD_RAW, &Instance);
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
