/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     HID devices, their list and the HID device backend
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "hid.hpp"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <strsafe.h>

#define DI_HID_INTERFACE_LIMIT  64
#define DI_HID_REBUILD_TICKS    20000
#define DI_HID_NO_JOYSTICK      17
#define DI_HID_AXIS_MAX         0xFFFF
#define DI_IOCTL_POLL_FREQUENCY 0x000B019C

#if DIRECTINPUT_VERSION >= 0x0800
/* DirectInput 8 has a type per kind of game controller */
#define DI_HID_TYPE_MOUSE       DI8DEVTYPE_MOUSE
#define DI_HID_TYPE_KEYBOARD    DI8DEVTYPE_KEYBOARD
#define DI_DEVTYPE_HID_DEVICE   (DIDEVTYPE_HID | DI8DEVTYPE_DEVICE)
#define DI_DEVTYPE_HID_MOUSE    (DIDEVTYPE_HID | MAKEWORD(DI8DEVTYPE_MOUSE, DI8DEVTYPEMOUSE_UNKNOWN))
#define DI_DEVTYPE_HID_JOYSTICK (DIDEVTYPE_HID | MAKEWORD(DI8DEVTYPE_JOYSTICK, DI8DEVTYPEJOYSTICK_STANDARD))
#define DI_DEVTYPE_HID_GAMEPAD  (DIDEVTYPE_HID | MAKEWORD(DI8DEVTYPE_GAMEPAD, DI8DEVTYPEGAMEPAD_STANDARD))
#define DI_DEVTYPE_HID_KEYBOARD (DIDEVTYPE_HID | DI8DEVTYPE_KEYBOARD)
#else
#define DI_HID_TYPE_MOUSE       DIDEVTYPE_MOUSE
#define DI_HID_TYPE_KEYBOARD    DIDEVTYPE_KEYBOARD
#define DI_DEVTYPE_HID_DEVICE   (DIDEVTYPE_HID | DIDEVTYPE_DEVICE)
#define DI_DEVTYPE_HID_MOUSE    (DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_MOUSE, DIDEVTYPEMOUSE_UNKNOWN))
#define DI_DEVTYPE_HID_JOYSTICK (DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_JOYSTICK, DIDEVTYPEJOYSTICK_UNKNOWN))
#define DI_DEVTYPE_HID_GAMEPAD  (DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_JOYSTICK, DIDEVTYPEJOYSTICK_GAMEPAD))
#define DI_DEVTYPE_HID_KEYBOARD (DIDEVTYPE_HID | DIDEVTYPE_KEYBOARD)
#define DI_DEVTYPE_HID_STICK    (DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_JOYSTICK, DIDEVTYPEJOYSTICK_TRADITIONAL))
#endif

/* Microsoft gameport adapters, which have no type key of their own */
#define DI_HID_MS_VENDOR        0x045E
#define DI_HID_MS_GAMEPORT_LOW  0x0102
#define DI_HID_MS_GAMEPORT_HIGH 0x010C

/* Physical interface device page, which describes force feedback effects */
#define DI_HID_PAGE_PID         0x0F

/* Head tracker flag in a joystick type's hardware settings, newer than the headers */
#define DI_JOY_HWS_HEADTRACKER  0x02000000

#define DI_PRIVATE_ROOT L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\DirectInput"
#define DI_JOY_TYPE_ROOT L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM"

/* Device instance flags remembered under DeviceInstances */
#define DI_HID_SEEN_DESCRIBED   2

/* Read, write, create and delete, without changing security */
#define DI_HID_KEY_ACCESS       0x0003003F

/* One HID collection DirectInput knows about */
typedef struct _RX_DI_HID_ENTRY
{
    ULONG MouseButtons;
    ULONG DevType;
    ULONG MouseAxes;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    HKEY Calibration;
    PWSTR InstanceId;
    GUID Instance;
    GUID Product;
    ULONG JoystickId;
    USHORT ProductId;
    USHORT VendorId;
    BOOL Present;
} RX_DI_HID_ENTRY;

static RX_DI_HID_ENTRY* DiHidEntries;
static ULONG DiHidCount;
static ULONG DiHidCapacity;
static DWORD DiHidBuilt;
static WCHAR DiHidLastUnknown[MAX_PATH];
static DWORD DiHidUnknownTick;

static const GUID DiJoystickBase = { 0x6F1D2B70, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

/* Whether a HID device type is one the joystick numbering covers */
static
BOOLEAN
DiHidIsJoystick(
    _In_ ULONG DevType)
{
#if DIRECTINPUT_VERSION >= 0x0800
    return (DevType & DIDEVTYPE_HID) != 0 && LOBYTE(DevType) >= DI8DEVTYPE_JOYSTICK;
#else
    return (DevType & (DIDEVTYPE_HID | DIDEVTYPE_JOYSTICK)) == (DIDEVTYPE_HID | DIDEVTYPE_JOYSTICK);
#endif
}

static
VOID
DiHidFreeEntry(
    _Inout_ RX_DI_HID_ENTRY* Entry)
{
    DiFree(reinterpret_cast<PVOID*>(&Entry->Detail));
    DiFree(reinterpret_cast<PVOID*>(&Entry->InstanceId));
    if (Entry->Calibration != nullptr)
        RegCloseKey(Entry->Calibration);
    ZeroMemory(Entry, sizeof(*Entry));
}

static
VOID
DiHidEmpty(VOID)
{
    for (ULONG Index = 0; Index < DiHidCount; Index++)
        DiHidFreeEntry(&DiHidEntries[Index]);
    DiHidCount = 0;
}

static
BOOLEAN
DiHidGetPath(
    _In_ HDEVINFO Set,
    _In_ PSP_DEVICE_INTERFACE_DATA Interface,
    _Out_ PSP_DEVICE_INTERFACE_DETAIL_DATA_W* Detail,
    _Out_opt_ PSP_DEVINFO_DATA Device)
{
    DWORD Size = 0;

    *Detail = nullptr;
    if (!SetupDiGetDeviceInterfaceDetailW(Set, Interface, nullptr, 0, &Size, nullptr) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return FALSE;
    }

    if (FAILED(DiAllocate(Size, reinterpret_cast<PVOID*>(Detail))))
        return FALSE;

    (*Detail)->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (SetupDiGetDeviceInterfaceDetailW(Set, Interface, *Detail, Size, &Size, Device))
        return TRUE;

    DiFree(reinterpret_cast<PVOID*>(Detail));
    return FALSE;
}

static
BOOLEAN
DiHidGetInstanceId(
    _In_ HDEVINFO Set,
    _In_ PSP_DEVINFO_DATA Device,
    _Out_ PWSTR* Id)
{
    DWORD Length = 0;

    *Id = nullptr;
    if (SetupDiGetDeviceInstanceIdW(Set, Device, nullptr, 0, &Length) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;
    if (FAILED(DiAllocate(Length * sizeof(WCHAR), reinterpret_cast<PVOID*>(Id))))
        return FALSE;
    return SetupDiGetDeviceInstanceIdW(Set, Device, *Id, Length, nullptr) != FALSE;
}

BOOLEAN
DiHidParseIds(
    _In_z_ PCWSTR Text,
    _Out_ PUSHORT VendorId,
    _Out_ PUSHORT ProductId)
{
    ULONG Vendor = 0;
    ULONG Product = 0;

    if (_wcsnicmp(Text, L"VID_", 4) != 0 || _wcsnicmp(Text + 8, L"&PID_", 5) != 0)
        return FALSE;

    for (ULONG Index = 0; Index < 4; Index++)
    {
        WCHAR V = Text[4 + Index];
        WCHAR P = Text[13 + Index];

        if (!iswxdigit(V) || !iswxdigit(P))
            return FALSE;
        Vendor = (Vendor << 4) | (iswdigit(V) ? V - L'0' : (towupper(V) - L'A' + 10));
        Product = (Product << 4) | (iswdigit(P) ? P - L'0' : (towupper(P) - L'A' + 10));
    }

    *VendorId = static_cast<USHORT>(Vendor);
    *ProductId = static_cast<USHORT>(Product);
    return TRUE;
}

/* Reads the instance GUID of a calibration key, making one up the first time */
static
VOID
DiHidReadInstanceGuid(
    _In_ HKEY Key,
    _Out_ GUID* Guid)
{
    DWORD Size = sizeof(*Guid);
    SYSTEMTIME Now;
    ULARGE_INTEGER Time;
    ULONG Sequence;

    if (RegQueryValueExW(Key, L"GUID", nullptr, nullptr, reinterpret_cast<PBYTE>(Guid), &Size) == ERROR_SUCCESS)
        return;

    /* A time based GUID counted from the Gregorian calendar reform */
    GetLocalTime(&Now);
    SystemTimeToFileTime(&Now, reinterpret_cast<FILETIME*>(&Time));
    Time.QuadPart += 5748191990000000ULL;
    Guid->Data1 = Time.LowPart;
    Guid->Data2 = static_cast<USHORT>(Time.HighPart);
    Guid->Data3 = static_cast<USHORT>(((Time.HighPart >> 16) & 0x0FFF) | 0x1000);

    Sequence = DiNextGuidSequence();
    Guid->Data4[0] = static_cast<BYTE>(((Sequence >> 8) & 0x3F) | 0x80);
    Guid->Data4[1] = static_cast<BYTE>(Sequence);
    Guid->Data4[2] = 'D';
    Guid->Data4[3] = 'E';
    Guid->Data4[4] = 'S';
    Guid->Data4[5] = 'T';
    Guid->Data4[6] = 0;
    Guid->Data4[7] = 0;

    if (RegSetValueExW(Key, L"GUID", 0, REG_BINARY, reinterpret_cast<const BYTE*>(Guid), sizeof(*Guid)) != ERROR_SUCCESS)
        DiTrace(L"cannot store a HID instance GUID");
}

/* Opens the per device keys and reads or writes the device instance flag */
static
HRESULT
DiHidOpenKeys(
    _Inout_ RX_DI_HID_ENTRY* Entry,
    _Inout_ PBYTE Flag)
{
    WCHAR Name[MAX_PATH] = {};
    PCWSTR Suffix = nullptr;
    PCWSTR Id = Entry->InstanceId;
    HKEY Root = nullptr;
    HKEY Device = nullptr;
    HKEY Calibration = nullptr;
    HKEY Instances = nullptr;
    WCHAR Index[3];
    ULONG Same = 0;
    HRESULT hr;

    if (Id[0] == L'\0' || Id[1] == L'\0' || Id[2] == L'\0' || Id[3] != L'\\')
        return E_INVALIDARG;
    Id += 4;

    if (Entry->ProductId != 0 && Entry->VendorId != 0)
    {
        PCWSTR Slash;
        SIZE_T Length;

        StringCchPrintfW(Name, _countof(Name), L"VID_%04X&PID_%04X", Entry->VendorId, Entry->ProductId);
        Length = wcslen(Name);
        Slash = wcschr(Id, L'\\');
        if (*Id == L'\\' || Slash == nullptr || Slash == Id)
            return E_INVALIDARG;

        /* The instance part goes past a gap after the key name */
        StringCchCopyW(Name + Length + 2, _countof(Name) - Length - 2, Slash + 1);
        Suffix = Name + Length + 2;
    }
    else
    {
        PWSTR Out = Name;
        BOOLEAN Ampersand = FALSE;

        if (*Id == L'\0')
            return E_INVALIDARG;

        /* The hardware part up to its second ampersand names the key, the rest names the instance */
        for (PCWSTR In = Id; *In != L'\0'; In++)
        {
            WCHAR Char = *In;

            if (Char >= L'a' && Char <= L'z')
            {
                Char -= 32;
            }
            else if (Char == L'&')
            {
                if (Suffix == nullptr && Ampersand)
                    Char = L'\0';
                else
                    Ampersand = TRUE;
            }
            else if (Char == L'\\')
            {
                if (Suffix != nullptr || Out == Name)
                    return E_INVALIDARG;
                Suffix = Out + 1;
                Char = L'\0';
            }

            if (Out >= Name + _countof(Name) - 1)
                return E_INVALIDARG;
            *Out++ = Char;
        }

        if (Suffix == nullptr)
            return E_INVALIDARG;

        if (Suffix - Name == 18)
            DiHidParseIds(Id, &Entry->VendorId, &Entry->ProductId);
        *Out = L'\0';
    }

    hr = DiOpenKey(HKEY_CURRENT_USER, DI_PRIVATE_ROOT, DI_HID_KEY_ACCESS, 0, &Root);
    if (FAILED(hr))
        return hr;

    hr = DiOpenKey(Root, Name, DI_HID_KEY_ACCESS, 0, &Device);
    if (SUCCEEDED(hr))
    {
        hr = DiOpenKey(Device, L"Calibration", DI_HID_KEY_ACCESS, 0, &Calibration);
        if (SUCCEEDED(hr))
        {
            /* Devices sharing an id get their own numbered calibration slot */
            for (ULONG Other = 0; Other < DiHidCount; Other++)
            {
                if (DiHidEntries[Other].VendorId == Entry->VendorId && DiHidEntries[Other].ProductId == Entry->ProductId)
                    Same++;
            }

            StringCchPrintfW(Index, _countof(Index), L"%u", Same);
            hr = DiOpenKey(Calibration, Index, DI_HID_KEY_ACCESS, 0, &Entry->Calibration);
            if (SUCCEEDED(hr))
                DiHidReadInstanceGuid(Entry->Calibration, &Entry->Instance);
            RegCloseKey(Calibration);
        }

        if (SUCCEEDED(DiOpenKey(Device, L"DeviceInstances", DI_HID_KEY_ACCESS, 0, &Instances)))
        {
            if (*Flag != 0)
            {
                RegSetValueExW(Instances, Suffix, 0, REG_BINARY, Flag, 1);
            }
            else
            {
                DWORD Size = 1;

                if (RegQueryValueExW(Instances, Suffix, nullptr, nullptr, Flag, &Size) != ERROR_SUCCESS)
                    *Flag = 0;
            }
            RegCloseKey(Instances);
        }
        RegCloseKey(Device);
    }

    RegCloseKey(Root);
    return hr;
}

static
VOID
DiHidReadUsages(
    _Inout_ RX_DI_HID_ENTRY* Entry,
    _In_ const HIDP_CAPS* Caps,
    _In_ PHIDP_PREPARSED_DATA Data)
{
    if (Caps->UsagePage != HID_USAGE_PAGE_GENERIC)
    {
        Entry->DevType = DI_DEVTYPE_HID_DEVICE;
        return;
    }

    switch (Caps->Usage)
    {
        case HID_USAGE_GENERIC_POINTER:
        case HID_USAGE_GENERIC_MOUSE:
        {
            HIDP_VALUE_CAPS* Wheel = nullptr;
            USHORT Count = Caps->NumberInputValueCaps;

            Entry->MouseButtons = HidP_MaxUsageListLength(HidP_Input, HID_USAGE_PAGE_BUTTON, Data);
            if (SUCCEEDED(DiAllocate(Count, reinterpret_cast<PVOID*>(&Wheel))))
            {
                USHORT Found = 0;

                if (HidP_GetSpecificValueCaps(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, HID_USAGE_GENERIC_WHEEL,
                                              Wheel, &Found, Data) == HIDP_STATUS_SUCCESS)
                {
                    Entry->MouseAxes = 3;
                }
                DiFree(reinterpret_cast<PVOID*>(&Wheel));
            }
            Entry->DevType = DI_DEVTYPE_HID_MOUSE;
            break;
        }

        case HID_USAGE_GENERIC_JOYSTICK:
            Entry->DevType = DI_DEVTYPE_HID_JOYSTICK;
            break;
        case HID_USAGE_GENERIC_GAMEPAD:
            Entry->DevType = DI_DEVTYPE_HID_GAMEPAD;
            break;
        case HID_USAGE_GENERIC_KEYBOARD:
            Entry->DevType = DI_DEVTYPE_HID_KEYBOARD;
            break;
        default:
            Entry->DevType = DI_DEVTYPE_HID_DEVICE;
            break;
    }
}

static
BOOLEAN
DiHidDescribe(
    _In_ HDEVINFO Set,
    _In_ PSP_DEVICE_INTERFACE_DATA Interface,
    _Out_ RX_DI_HID_ENTRY* Entry)
{
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR Description[260];
    ULONG Length = sizeof(Description);
    BYTE Stored = 0;
    BYTE Seen = 0;
    BOOLEAN Usable = FALSE;
    BOOLEAN Open = FALSE;
    HANDLE File;

    ZeroMemory(Entry, sizeof(*Entry));
    if (!DiHidGetPath(Set, Interface, &Entry->Detail, &Device) || !DiHidGetInstanceId(Set, &Device, &Entry->InstanceId))
        goto Done;

    DiHidOpenKeys(Entry, &Stored);

    /* Devices without a description are only taken once they have been seen for a while */
    if (CM_Get_DevNode_Registry_PropertyW(Device.DevInst, CM_DRP_DEVICEDESC, nullptr, Description, &Length, 0) == CR_SUCCESS)
    {
        Seen = DI_HID_SEEN_DESCRIBED;
        Open = TRUE;
    }
    else if (Stored == 1)
    {
        Seen = 1;
        Open = TRUE;
    }
    else if (lstrcmpiW(Entry->InstanceId, DiHidLastUnknown) != 0)
    {
        StringCchCopyW(DiHidLastUnknown, _countof(DiHidLastUnknown), Entry->InstanceId);
        DiHidUnknownTick = GetTickCount();
    }
    else if (GetTickCount() - DiHidUnknownTick >= DI_HID_REBUILD_TICKS)
    {
        Seen = 1;
        DiHidLastUnknown[0] = L'0';
        Open = TRUE;
    }

    if (Open)
    {
        File = CreateFileW(Entry->Detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
        if (File != INVALID_HANDLE_VALUE)
        {
            PHIDP_PREPARSED_DATA Data = nullptr;
            HIDD_ATTRIBUTES Attributes = { sizeof(Attributes) };

            if (HidD_GetPreparsedData(File, &Data))
            {
                HIDP_CAPS Caps = {};

                if (HidP_GetCaps(Data, &Caps) == HIDP_STATUS_SUCCESS)
                {
                    DiHidReadUsages(Entry, &Caps, Data);
                    Usable = TRUE;
                }
                HidD_FreePreparsedData(Data);
            }

            if (HidD_GetAttributes(File, &Attributes))
            {
                BYTE Known = Stored;

                if (Entry->VendorId != Attributes.VendorID || Entry->ProductId != Attributes.ProductID)
                {
                    Entry->VendorId = Attributes.VendorID;
                    Entry->ProductId = Attributes.ProductID;
                    Known = 0;
                }
                if (Known != Seen)
                    DiHidOpenKeys(Entry, &Seen);
            }

            Entry->Product = { static_cast<ULONG>(Entry->VendorId | (Entry->ProductId << 16)), 0, 0,
                               { 0, 0, 'P', 'I', 'D', 'V', 'I', 'D' } };
            CloseHandle(File);
        }
    }

Done:
    if (Entry->Calibration != nullptr)
    {
        DWORD Size = sizeof(Entry->JoystickId);

        if (RegQueryValueExW(Entry->Calibration, L"Joystick Id", nullptr, nullptr,
                             reinterpret_cast<PBYTE>(&Entry->JoystickId), &Size) != ERROR_SUCCESS)
        {
            Entry->JoystickId = DI_HID_NO_JOYSTICK;
        }
    }

    if (!Usable)
        DiHidFreeEntry(Entry);
    return Usable;
}

/* Gives every HID joystick a distinct id below 16, keeping the ids they already hold */
static
VOID
DiHidAssignJoystickIds(VOID)
{
    BOOLEAN Used[16] = {};
    BOOLEAN Pending = FALSE;

    for (ULONG Index = 0; Index < DiHidCount && DiHidEntries[Index].Present; Index++)
    {
        RX_DI_HID_ENTRY* Entry = &DiHidEntries[Index];

        if (!DiHidIsJoystick(Entry->DevType))
            continue;

        if (Entry->JoystickId >= _countof(Used) || Used[Entry->JoystickId])
        {
            Entry->JoystickId = DI_HID_NO_JOYSTICK;
            Pending = TRUE;
        }
        else
        {
            Used[Entry->JoystickId] = TRUE;
        }
    }

    if (!Pending)
        return;

    for (ULONG Index = 0; Index < DiHidCount; Index++)
    {
        RX_DI_HID_ENTRY* Entry = &DiHidEntries[Index];
        ULONG Free = 0;

        if (!DiHidIsJoystick(Entry->DevType) || Entry->JoystickId != DI_HID_NO_JOYSTICK)
        {
            continue;
        }

        while (Free < _countof(Used) && Used[Free])
            Free++;
        if (Free == _countof(Used))
            continue;

        Used[Free] = TRUE;
        Entry->JoystickId = Free;
        if (RegSetValueExW(Entry->Calibration, L"Joystick Id", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&Free),
                           sizeof(Free)) != ERROR_SUCCESS)
        {
            DiTrace(L"cannot store joystick id %lu for %ls", Free, Entry->InstanceId);
        }
        else
        {
            DiTrace(L"HID device %ls is joystick %lu", Entry->InstanceId, Free);
        }
    }
}

VOID
DiRefreshHidList(
    _In_ BOOLEAN Force)
{
    SP_DEVICE_INTERFACE_DATA Interface;
    HDEVINFO Set;
    GUID HidClass;
    DWORD Now;

    EnterCriticalSection(&DiLock);
    if (Force)
        DiHidEmpty();

    Now = GetTickCount();
    if (static_cast<LONG>(DiEmulation) < 0 || (!Force && DiHidBuilt != 0 && Now - DiHidBuilt <= DI_HID_REBUILD_TICKS))
    {
        LeaveCriticalSection(&DiLock);
        return;
    }

    HidD_GetHidGuid(&HidClass);
    Set = SetupDiGetClassDevsW(&HidClass, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Set == INVALID_HANDLE_VALUE)
    {
        DiTrace(L"HID device list unavailable, error %lu", GetLastError());
        if (DiHidCount != 0)
            DiHidAssignJoystickIds();
        DiHidBuilt = GetTickCount();
        LeaveCriticalSection(&DiLock);
        return;
    }

    /* Drop entries for devices that went away */
    for (ULONG Index = 0; Index < DiHidCount;)
    {
        HANDLE File = CreateFileW(DiHidEntries[Index].Detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

        if (File != INVALID_HANDLE_VALUE)
        {
            CloseHandle(File);
            Index++;
            continue;
        }

        DiTrace(L"HID device %ls is gone", DiHidEntries[Index].InstanceId);
        DiHidFreeEntry(&DiHidEntries[Index]);
        MoveMemory(&DiHidEntries[Index], &DiHidEntries[Index + 1], sizeof(*DiHidEntries) * (DiHidCount - Index - 1));
        DiHidCount -= 1;
    }

    for (DWORD Index = 0; Index < DI_HID_INTERFACE_LIMIT; Index++)
    {
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail = nullptr;
        RX_DI_HID_ENTRY Entry;
        BOOLEAN Known = FALSE;

        Interface.cbSize = sizeof(Interface);
        if (!SetupDiEnumDeviceInterfaces(Set, nullptr, &HidClass, Index, &Interface))
        {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                break;
            continue;
        }

        if (DiHidGetPath(Set, &Interface, &Detail, nullptr))
        {
            for (ULONG Other = 0; Other < DiHidCount; Other++)
            {
                if (lstrcmpW(Detail->DevicePath, DiHidEntries[Other].Detail->DevicePath) == 0)
                {
                    Known = TRUE;
                    break;
                }
            }
            DiFree(reinterpret_cast<PVOID*>(&Detail));
        }

        if (Known)
            continue;

        if (DiHidCount >= DiHidCapacity)
        {
            ULONG Grown = (DiHidCapacity == 0) ? 16 : DiHidCapacity * 2;

            if (FAILED(DiAllocate(sizeof(*DiHidEntries) * Grown, reinterpret_cast<PVOID*>(&DiHidEntries))))
                break;
            DiHidCapacity = Grown;
        }

        if (DiHidDescribe(Set, &Interface, &Entry))
        {
            Entry.Present = TRUE;
            DiHidEntries[DiHidCount++] = Entry;
        }
    }

    SetupDiDestroyDeviceInfoList(Set);
    if (DiHidCount != 0)
        DiHidAssignJoystickIds();
    DiHidBuilt = GetTickCount();
    LeaveCriticalSection(&DiLock);
}

HRESULT
DiSnapshotHidList(
    _Outptr_result_maybenull_ RX_DI_HID_SNAPSHOT** Snapshot)
{
    RX_DI_HID_SNAPSHOT* Copy = nullptr;
    HRESULT hr;

    *Snapshot = nullptr;
    EnterCriticalSection(&DiLock);
    hr = DiAllocate(FIELD_OFFSET(RX_DI_HID_SNAPSHOT, Entries) + sizeof(RX_DI_HID_SUMMARY) * (DiHidCount + 1),
                    reinterpret_cast<PVOID*>(&Copy));
    if (SUCCEEDED(hr))
    {
        Copy->Count = DiHidCount;
        for (ULONG Index = 0; Index < DiHidCount; Index++)
        {
            Copy->Entries[Index].Instance = DiHidEntries[Index].Instance;
            Copy->Entries[Index].DevType = static_cast<UCHAR>(DiHidEntries[Index].DevType);
        }
        *Snapshot = Copy;
    }
    LeaveCriticalSection(&DiLock);
    return hr;
}

static
RX_DI_HID_ENTRY*
DiHidFindByGuid(
    _In_ REFGUID Guid)
{
    for (ULONG Index = 0; Index < DiHidCount; Index++)
    {
        if (IsEqualGUID(Guid, DiHidEntries[Index].Instance))
            return &DiHidEntries[Index];
    }

    for (ULONG Index = 0; Index < DiHidCount; Index++)
    {
        if (IsEqualGUID(Guid, DiHidEntries[Index].Product))
            return &DiHidEntries[Index];
    }

    /* GUID_Joystick plus a joystick number */
    if ((Guid.Data1 & ~0xF) == DiJoystickBase.Data1 && memcmp(&Guid.Data2, &DiJoystickBase.Data2, 12) == 0)
    {
        for (ULONG Index = 0; Index < DiHidCount; Index++)
        {
            if (DiHidEntries[Index].JoystickId == (Guid.Data1 & 0xF))
                return &DiHidEntries[Index];
        }
    }
    return nullptr;
}

HRESULT
DiFindHidByPath(
    _In_z_ PCWSTR Path,
    _Out_ LPGUID Instance)
{
    for (ULONG Pass = 0; Pass < 2; Pass++)
    {
        if (Pass != 0)
            DiRefreshHidList(TRUE);

        EnterCriticalSection(&DiLock);
        for (ULONG Index = 0; Index < DiHidCount; Index++)
        {
            if (lstrcmpiW(DiHidEntries[Index].Detail->DevicePath, Path) == 0)
            {
                *Instance = DiHidEntries[Index].Instance;
                LeaveCriticalSection(&DiLock);
                return S_OK;
            }
        }
        LeaveCriticalSection(&DiLock);
    }

    return DIERR_DEVICENOTREG;
}

ULONG
DiHidMouseAxes(VOID)
{
    ULONG Axes = 2;

    DiRefreshHidList(FALSE);
    EnterCriticalSection(&DiLock);
    for (ULONG Index = 0; Index < DiHidCount; Index++)
    {
        if (DiHidEntries[Index].MouseAxes > Axes)
            Axes = DiHidEntries[Index].MouseAxes;
    }
    LeaveCriticalSection(&DiLock);
    return Axes;
}

/* Everything the HID backend copies out of the list when it starts */
typedef struct _RX_DI_HID_START
{
    ULONG DevType;
    ULONG JoystickId;
    WCHAR Path[MAX_PATH * 2];
    WCHAR InstanceId[MAX_PATH];
    USHORT VendorId;
    USHORT ProductId;
    HKEY CalTypeKey;
} RX_DI_HID_START;

/* Copies the values of one key that the target does not have yet */
static
LONG
DiCopyMissingValues(
    _In_ HKEY From,
    _In_ HKEY To)
{
    DWORD Count = 0;
    DWORD Longest = 0;
    PWSTR Name = nullptr;
    PBYTE Data = nullptr;
    DWORD Room = 0;
    LONG Error;

    Error = RegQueryInfoKeyW(From, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &Count, &Longest, nullptr, nullptr, nullptr);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (Longest >= 0x4000 || FAILED(DiAllocate(sizeof(WCHAR) * (Longest + 1), reinterpret_cast<PVOID*>(&Name))))
        return ERROR_NOT_ENOUGH_MEMORY;

    for (DWORD Index = 0; Index < Count; Index++)
    {
        DWORD NameLength = Longest + 1;
        DWORD Present = 0;
        DWORD Type = 0;
        DWORD Size = 0;

        Error = RegEnumValueW(From, Index, Name, &NameLength, nullptr, nullptr, nullptr, nullptr);
        if (Error != ERROR_SUCCESS)
            break;

        /* Values the user already has win */
        Error = RegQueryValueExW(To, Name, nullptr, nullptr, nullptr, &Present);
        if (Error == ERROR_SUCCESS || Error == ERROR_MORE_DATA)
        {
            Error = ERROR_SUCCESS;
            continue;
        }

        Error = RegQueryValueExW(From, Name, nullptr, &Type, nullptr, &Size);
        if (Error != ERROR_SUCCESS)
            break;
        if (Size > Room)
        {
            DiFree(reinterpret_cast<PVOID*>(&Data));
            if (FAILED(DiAllocate(Size, reinterpret_cast<PVOID*>(&Data))))
            {
                Error = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            Room = Size;
        }

        Error = RegQueryValueExW(From, Name, nullptr, &Type, Data, &Size);
        if (Error == ERROR_SUCCESS)
            Error = RegSetValueExW(To, Name, 0, Type, Data, Size);
        if (Error != ERROR_SUCCESS)
            break;
    }

    DiFree(reinterpret_cast<PVOID*>(&Name));
    DiFree(reinterpret_cast<PVOID*>(&Data));
    return Error;
}

/* Copies a key's subkeys, then its own values, stopping at the first failure */
static
LONG
DiCopyKeyTree(
    _In_ HKEY From,
    _In_ HKEY To)
{
    WCHAR Name[256];
    DWORD Count = 0;
    LONG Error;

    Error = RegQueryInfoKeyW(From, nullptr, nullptr, nullptr, &Count, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (Error != ERROR_SUCCESS)
        return Error;

    for (DWORD Index = 0; Index < Count; Index++)
    {
        HKEY Target = nullptr;
        HKEY Source = nullptr;

        Error = RegEnumKeyW(From, Index, Name, _countof(Name));
        if (Error == ERROR_SUCCESS)
            Error = RegCreateKeyExW(To, Name, 0, nullptr, 0, MAXIMUM_ALLOWED, nullptr, &Target, nullptr);
        if (Error != ERROR_SUCCESS)
            return Error;

        Error = RegOpenKeyExW(From, Name, 0, MAXIMUM_ALLOWED, &Source);
        if (Error == ERROR_SUCCESS)
        {
            Error = DiCopyKeyTree(Source, Target);
            RegCloseKey(Source);
        }
        RegCloseKey(Target);
        if (Error != ERROR_SUCCESS)
            return Error;
    }

    return DiCopyMissingValues(From, To);
}

/* Settings an installer put under HKLM for a device type follow the user into HKCU */
static
VOID
DiCopyMachineType(
    _In_ USHORT VendorId,
    _In_ USHORT ProductId)
{
    WCHAR Path[MAX_PATH];
    HKEY Machine = nullptr;
    HKEY User = nullptr;

    if (FAILED(StringCchPrintfW(Path, _countof(Path), DI_JOY_TYPE_ROOT L"\\VID_%04X&PID_%04X", VendorId, ProductId)))
        return;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, MAXIMUM_ALLOWED, &Machine) == ERROR_SUCCESS &&
        RegCreateKeyExW(HKEY_CURRENT_USER, Path, 0, nullptr, 0, MAXIMUM_ALLOWED, nullptr, &User, nullptr) == ERROR_SUCCESS)
    {
        if (DiCopyKeyTree(Machine, User) == ERROR_SUCCESS)
            DiTrace(L"copied joystick type VID_%04X&PID_%04X to the user", VendorId, ProductId);
    }

    if (User != nullptr)
        RegCloseKey(User);
    if (Machine != nullptr)
        RegCloseKey(Machine);
}

static
BOOLEAN
DiHidLookup(
    _In_ REFGUID Guid,
    _Out_ RX_DI_HID_START* Start)
{
    RX_DI_HID_ENTRY* Entry;
    BOOLEAN Found = FALSE;

    EnterCriticalSection(&DiLock);
    Entry = DiHidFindByGuid(Guid);
    if (Entry != nullptr)
    {
        Start->DevType = Entry->DevType;
        Start->JoystickId = Entry->JoystickId;
        Start->VendorId = Entry->VendorId;
        Start->ProductId = Entry->ProductId;
        StringCchCopyW(Start->Path, _countof(Start->Path), Entry->Detail->DevicePath);
        StringCchCopyW(Start->InstanceId, _countof(Start->InstanceId), Entry->InstanceId);

        /* Saved calibrations live in the Type key under the device's calibration slot */
        Start->CalTypeKey = nullptr;
        if (Entry->Calibration == nullptr ||
            FAILED(DiOpenKey(Entry->Calibration, L"Type", DI_HID_KEY_ACCESS, 0, &Start->CalTypeKey)))
        {
            Start->CalTypeKey = nullptr;
        }
        Found = TRUE;
    }
    LeaveCriticalSection(&DiLock);
    return Found;
}

/* Link collections are filed after the three HID report kinds */
#define DI_HID_REPORT_COLLECTION 3
#define DI_HID_REPORT_KINDS      4

/* Size of the point list form of the calibration property */
#define DI_HID_PROPCALPOV_BYTES  56

/* HID usage page whose usages are keyboard keys */
#define DI_HID_PAGE_KEYBOARD     0x07

/* Table that turns keyboard usages into DIK_ codes */
#define DI_HID_KEY_TABLE         9

/* A run of usages that came from one HID caps entry */
typedef struct _RX_DI_HID_GROUP
{
    ULONG Report;
    ULONG Count;
    USHORT UsagePage;
    USHORT UsageMin;
    USHORT StringMin;
    USHORT StringMax;
    USHORT DesignatorMin;
    USHORT DesignatorMax;
    USHORT DataIndexMin;
    USHORT Granularity;
    LONG SignMask;
    USHORT LinkCollection;
    UCHAR ReportId;
    LONG LogicalMin;
    LONG LogicalMax;
    LONG PhysicalMin;
    LONG PhysicalMax;
    ULONG Units;
    USHORT Exponent;
    BOOLEAN Absolute;
    BOOLEAN Value;
    BOOLEAN Alias;
    BOOLEAN Signed;
    BOOLEAN PovPoints;
    BOOLEAN Disabled;
} RX_DI_HID_GROUP;

/* Calibration of one input value, the Low and High fields are derived from the rest */
typedef struct _RX_DI_HID_CAL
{
    ULONG Raw;
    LONG LowStart;
    LONG LowBase;
    LONG LowSpan;
    LONG LowOut;
    LONG HighStart;
    LONG HighBase;
    LONG HighSpan;
    LONG HighOut;
    LONG Min;
    LONG Max;
    LONG Center;
    LONG RangeMin;
    LONG RangeMax;
    LONG RangeCenter;
    ULONG DeadZone;
    ULONG Saturation;
    ULONG LegacyRaw;
    BOOLEAN PovPoints;
    LONG PovLow[5];
    LONG PovHigh[5];
} RX_DI_HID_CAL;

typedef struct _RX_DI_HID_SLOT
{
    RX_DI_HID_GROUP* Group;
    RX_DI_HID_CAL* Cal;
} RX_DI_HID_SLOT;

/* Usages that DirectInput gives a fixed axis slot, searched in this order */
typedef struct _RX_DI_USAGE_SLOT
{
    ULONG Usage;
    ULONG Slot;
    const GUID* Guid;
} RX_DI_USAGE_SLOT;

static const RX_DI_USAGE_SLOT DiUsageSlots[] =
{
    { 0x00010030, 0, &GUID_XAxis },
    { 0x00010031, 1, &GUID_YAxis },
    { 0x00010032, 2, &GUID_ZAxis },
    { 0x00010038, 2, &GUID_ZAxis },
    { 0x00010033, 3, &GUID_RxAxis },
    { 0x00010034, 4, &GUID_RyAxis },
    { 0x00010035, 5, &GUID_RzAxis },
    { 0x00010039, 7, &GUID_POV },
    { 0x00010036, 6, &GUID_Slider },
    { 0x00010037, 6, &GUID_Slider },
    { 0x000200C8, 0, &GUID_XAxis },
    { 0x000200C4, 1, &GUID_YAxis },
    { 0x000200C5, 5, &GUID_RzAxis },
    { 0x000200BA, 5, &GUID_RzAxis },
    { 0x000200BB, 6, &GUID_Slider },
    { 0x00050020, 7, &GUID_POV },
};

/* First string of each usage page's names, zero for pages without names */
static const USHORT DiUsageNameBase[16] =
{
    0, 1536, 2048, 2560, 3072, 3584, 0, 6144, 4096, 0, 0, 4608, 5120, 5632, 0, 6656
};

static
const RX_DI_USAGE_SLOT*
DiHidFindUsageSlot(
    _In_ ULONG Usage)
{
    for (ULONG Index = 0; Index < _countof(DiUsageSlots); Index++)
    {
        if (DiUsageSlots[Index].Usage == Usage)
            return &DiUsageSlots[Index];
    }
    return nullptr;
}

/* Joins a page and a usage the way the usage table stores them */
static
ULONG
DiHidUsageKey(
    _In_ USHORT Page,
    _In_ ULONG Usage)
{
    return static_cast<USHORT>(Usage) | (static_cast<ULONG>(Page) << 16);
}

/* Orders caps by data index, keeping the real usage ahead of its aliases */
template <typename CAPS>
static
VOID
DiHidSortCaps(
    _Inout_updates_(Count) CAPS* List,
    _In_ ULONG Count)
{
    for (ULONG Next = 1; Next < Count; Next++)
    {
        CAPS Held = List[Next];
        ULONG Hole = Next;

        while (Hole > 0)
        {
            const CAPS* Before = &List[Hole - 1];

            if (Before->Range.DataIndexMin < Held.Range.DataIndexMin)
                break;
            if (Before->Range.DataIndexMin == Held.Range.DataIndexMin && !Before->IsAlias)
                break;
            List[Hole] = *Before;
            Hole -= 1;
        }
        List[Hole] = Held;
    }
}

/* What a value's units say about it, or what its usages imply when it has none */
static
ULONG
DiHidValueAspect(
    _In_ const HIDP_VALUE_CAPS* Caps)
{
    const RX_DI_USAGE_SLOT* First;
    const RX_DI_USAGE_SLOT* Last;
    ULONG Units = Caps->Units;

    if (Units == 0)
    {
        First = DiHidFindUsageSlot(DiHidUsageKey(Caps->UsagePage, Caps->Range.UsageMin));
        if (First == nullptr || First->Slot > 6)
            return 0;
        Last = DiHidFindUsageSlot(DiHidUsageKey(Caps->UsagePage, Caps->Range.UsageMax));
        if (Last == nullptr)
            return 0;
        return (Last->Slot > 6) ? 0 : DIDOI_ASPECTPOSITION;
    }

    /* Only first powers of a length or an angle are understood */
    if ((Units & 0xFFFF0000) != 0 || static_cast<UCHAR>(Units) < 0x11 || static_cast<UCHAR>(Units) > 0x14)
        return 0;

    switch (Units & 0xFF00)
    {
        case 0x0000:
            return DIDOI_ASPECTPOSITION;
        case 0xF000:
            return DIDOI_ASPECTVELOCITY;
        case 0xE000:
            return DIDOI_ASPECTACCEL;
        case 0xE100:
            return DIDOI_ASPECTFORCE;
    }
    return 0;
}

/* Splits the calibrated range into the parts below and above the center */
static
VOID
DiHidRecalc(
    _Inout_ RX_DI_HID_CAL* Cal)
{
    ULONG Edge = (Cal->Saturation > Cal->DeadZone) ? Cal->Saturation : Cal->DeadZone;
    LONG Below = Cal->Center - Cal->Min;
    LONG Above = Cal->Max - Cal->Center;

    Cal->LowStart = Cal->Center - MulDiv(Below, Edge, 10000);
    Cal->LowBase = Cal->RangeMin;
    Cal->LowSpan = Cal->Center - Cal->LowStart - MulDiv(Below, Cal->DeadZone, 10000);
    Cal->LowOut = Cal->RangeCenter - Cal->RangeMin;

    Cal->HighStart = Cal->Center + MulDiv(Above, Cal->DeadZone, 10000);
    if (static_cast<ULONG>(Cal->Max) > static_cast<ULONG>(Cal->Center + 1))
        Cal->HighStart += 1;
    Cal->HighSpan = Cal->Center - Cal->HighStart + MulDiv(Above, Edge, 10000);

    Cal->HighBase = Cal->RangeCenter;
    if (Cal->RangeMax > Cal->RangeCenter && static_cast<ULONG>(Cal->Max) > static_cast<ULONG>(Cal->Center + 1))
        Cal->HighBase += 1;
    Cal->HighOut = Cal->RangeMax - Cal->HighBase;
}

/* Maps a raw reading through the calibration, a no op in raw mode */
static
VOID
DiHidCook(
    _In_ const RX_DI_HID_CAL* Cal,
    _Inout_ PLONG Value)
{
    LONG Reading = *Value;

    if (Cal->Raw != 0)
        return;

    if (Cal->LegacyRaw != 0)
    {
        if (Cal->Min >= 0)
            return;
        *Value = Reading - Cal->Min;
        return;
    }

    if (Cal->PovPoints)
    {
        static const LONG Directions[4] = { 0, 18000, 27000, 9000 };

        *Value = -1;
        for (ULONG Index = 0; Index < _countof(Directions); Index++)
        {
            if (Reading > Cal->PovLow[Index] && Reading < Cal->PovHigh[Index])
            {
                *Value = Directions[Index];
                break;
            }
        }
        return;
    }

    {
        BOOLEAN Upper = Reading >= Cal->HighStart;
        LONG Start = Upper ? Cal->HighStart : Cal->LowStart;
        LONG Span = Upper ? Cal->HighSpan : Cal->LowSpan;
        LONG Out = Upper ? Cal->HighOut : Cal->LowOut;
        LONG Moved = 0;

        if (Reading > Start)
        {
            ULONG Past = static_cast<ULONG>(Reading - Start);

            Moved = Out;
            if (Past < static_cast<ULONG>(Span))
                Moved = MulDiv(static_cast<LONG>(Past), Out, Span);
        }
        *Value = (Upper ? Cal->HighBase : Cal->LowBase) + Moved;
    }
}

static
BOOLEAN
DiHidModernVersion(
    _In_ DWORD Version)
{
    return Version >= 0x0700 || Version == 0x05B2;
}

/* Opens the type key branch of one object, as Axes, Buttons, POVs or Actuators */
static
HRESULT
DiHidOpenObjectKey(
    _In_opt_ HKEY Root,
    _In_ ULONG Type,
    _In_ REGSAM Access,
    _Out_ HKEY* Key)
{
    WCHAR Path[32];
    PCWSTR Kind;

    *Key = nullptr;
    if (Root == nullptr)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    if (Type & DIDFT_AXIS)
        Kind = L"Axes";
    else if (Type & DIDFT_BUTTON)
        Kind = L"Buttons";
    else if (Type & DIDFT_POV)
        Kind = L"POVs";
    else if (Type & DIDFT_NODATA)
        Kind = L"Actuators";
    else
        return E_NOTIMPL;

    StringCchPrintfW(Path, _countof(Path), L"%s\\%u", Kind, DIDFT_GETINSTANCE(Type));
    return DiOpenKey(Root, Path, Access, 0, Key);
}

/* Reads a string value, reporting a missing terminator the way the registry does */
static
LONG
DiHidReadString(
    _In_ HKEY Key,
    _In_opt_z_ PCWSTR Name,
    _Out_writes_(Length) PWSTR Text,
    _In_ ULONG Length)
{
    DWORD Size = Length * sizeof(WCHAR);
    DWORD Type = 0;
    ULONG Count;
    LONG Error;

    Error = RegQueryValueExW(Key, Name, nullptr, &Type, reinterpret_cast<PBYTE>(Text), &Size);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (Type != REG_SZ)
        return ERROR_INVALID_DATA;

    Count = Size / sizeof(WCHAR);
    if (Count == 0)
    {
        Text[0] = L'\0';
        return ERROR_SUCCESS;
    }
    if (Text[Count - 1] != L'\0')
    {
        if (Count >= Length)
            return ERROR_MORE_DATA;
        Text[Count] = L'\0';
    }
    return ERROR_SUCCESS;
}

static
HRESULT
DiHidSetCalibration(
    _Inout_ RX_DI_HID_CAL* Cal,
    _In_ ULONG Type,
    _In_ LPCDIPROPHEADER Header,
    _In_opt_ HKEY Key)
{
    HKEY Object = nullptr;
    HRESULT hr = S_FALSE;

    if (Type != DIDFT_POV)
    {
        const DIPROPCAL* Points = reinterpret_cast<const DIPROPCAL*>(Header);

        if (Key != nullptr)
        {
            hr = DiHidOpenObjectKey(Key, Type, DI_HID_KEY_ACCESS, &Object);
            if (FAILED(hr))
                return hr;

            /* An all zero calibration means none */
            if (Points->lMin == Points->lMax && Points->lCenter == Points->lMax && Points->lMax == 0)
                RegDeleteValueW(Object, L"Calibration");
            else if (RegSetValueExW(Object, L"Calibration", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&Points->lMin),
                                    3 * sizeof(LONG)) != ERROR_SUCCESS)
                hr = E_FAIL;
            RegCloseKey(Object);
            if (FAILED(hr))
                return hr;
        }

        Cal->Min = Points->lMin;
        Cal->Max = Points->lMax;
        Cal->Center = Points->lCenter;
        DiHidRecalc(Cal);
        return hr;
    }

    {
        const LONG* Points = reinterpret_cast<const LONG*>(reinterpret_cast<const BYTE*>(Header) + sizeof(DIPROPHEADER));

        if (!Cal->PovPoints)
            return E_NOTIMPL;

        if (Key != nullptr)
        {
            LONG Chain;

            hr = DiHidOpenObjectKey(Key, DIDFT_POV, DI_HID_KEY_ACCESS, &Object);
            if (FAILED(hr))
                return hr;

            /* Each point is compared against the truth of the comparisons before it */
            Chain = (Points[0] == Points[1]);
            for (ULONG Index = 2; Index < 10; Index++)
                Chain = (Chain == Points[Index]);

            if (Chain != 0)
                hr = (RegSetValueExW(Object, L"Calibration", 0, REG_BINARY, reinterpret_cast<const BYTE*>(Points),
                                     10 * sizeof(LONG)) != ERROR_SUCCESS) ? E_FAIL : S_OK;
            else
                RegDeleteValueW(Object, L"Calibration");
            RegCloseKey(Object);
            if (FAILED(hr))
                return hr;
        }

        CopyMemory(Cal->PovLow, Points, sizeof(Cal->PovLow));
        CopyMemory(Cal->PovHigh, Points + 5, sizeof(Cal->PovHigh));
        return hr;
    }
}

static
HRESULT
DiHidSetCalProperty(
    _Inout_ RX_DI_HID_CAL* Cal,
    _In_ const RX_DI_PROP_TARGET* Target,
    _In_ LPCDIPROPHEADER Header,
    _In_opt_ HKEY Key,
    _In_ DWORD Version)
{
    const DIPROPDWORD* Value = reinterpret_cast<const DIPROPDWORD*>(Header);

    switch (Target->Property)
    {
        case 4:
        {
            const DIPROPRANGE* Range = reinterpret_cast<const DIPROPRANGE*>(Header);

            if (Range->lMin > Range->lMax)
                return E_INVALIDARG;
            Cal->RangeMin = Range->lMin;
            Cal->RangeMax = Range->lMax;
            Cal->RangeCenter = Range->lMin + static_cast<LONG>(static_cast<ULONG>(Range->lMax - Range->lMin + 1) >> 1);
            DiHidRecalc(Cal);
            return S_OK;
        }

        case 5:
        case 6:
            if (Value->dwData > 10000)
                return E_INVALIDARG;
            if (Target->Property == 5)
                Cal->DeadZone = Value->dwData;
            else
                Cal->Saturation = Value->dwData;
            DiHidRecalc(Cal);
            return S_OK;

        case 10:
            if (Value->dwData & ~1UL)
                return E_INVALIDARG;
            if (DiHidModernVersion(Version) || (Cal->Min & 0x8000) == 0)
                Cal->Raw = Value->dwData;
            else
                Cal->LegacyRaw = Value->dwData;
            return S_OK;

        case 11:
            return DiHidSetCalibration(Cal, Target->Type, Header, Key);
    }
    return E_NOTIMPL;
}

static
HRESULT
DiHidGetCalProperty(
    _In_ const RX_DI_HID_CAL* Cal,
    _In_ ULONG_PTR Property,
    _Inout_ LPDIPROPHEADER Header,
    _In_ DWORD Version)
{
    LPDIPROPDWORD Value = reinterpret_cast<LPDIPROPDWORD>(Header);
    LPDIPROPRANGE Range = reinterpret_cast<LPDIPROPRANGE>(Header);
    LPDIPROPCAL Points = reinterpret_cast<LPDIPROPCAL>(Header);

    switch (Property)
    {
        case 4:
            Range->lMin = Cal->RangeMin;
            Range->lMax = Cal->RangeMax;
            return S_OK;
        case 5:
            Value->dwData = Cal->DeadZone;
            return S_OK;
        case 6:
            Value->dwData = Cal->Saturation;
            return S_OK;
        case 10:
            Value->dwData = DiHidModernVersion(Version) ? Cal->Raw : Cal->LegacyRaw;
            return S_OK;
        case 11:
            /* Old applications saw calibrations that start below zero moved up to it */
            if (DiHidModernVersion(Version) || (Cal->Min & 0x8000) == 0)
            {
                Points->lMin = Cal->Min;
                Points->lMax = Cal->Max;
                Points->lCenter = Cal->Center;
            }
            else
            {
                Points->lMin = 0;
                Points->lMax = Cal->Max - Cal->Min;
                Points->lCenter = static_cast<LONG>(static_cast<ULONG>(Points->lMax + 1) >> 1);
            }
            return S_OK;
    }
    return E_NOTIMPL;
}

/* Signed and unsigned limits of a field, with shifts wrapping like the hardware does */
static
VOID
DiHidFieldLimits(
    _In_ USHORT Bits,
    _Out_ PLONG SignMask,
    _Out_ PLONG SignedMax,
    _Out_ PLONG UnsignedMax)
{
    ULONG Half = 1UL << ((Bits - 1) & 31);
    LONG Full = static_cast<LONG>((1UL << (Bits & 31)) - 1);

    *SignMask = static_cast<LONG>(0 - Half);
    *SignedMax = static_cast<LONG>(Half - 1);
    *UnsignedMax = (Full < 1) ? 1 : Full;
}

class RxDiHid final : public RxDiBackend
{
public:
    HRESULT
    Init(
        _In_ REFGUID Instance)
    {
        RX_DI_SINK_PARAMS Params = {};
        HIDD_ATTRIBUTES Attributes = { sizeof(Attributes) };
        HANDLE File;
        HRESULT hr;

        if (!DiHidLookup(Instance, &Start))
            return DIERR_DEVICENOTREG;
        DiCopyMachineType(Start.VendorId, Start.ProductId);

        DevType = Start.DevType;
        File = OpenDevice(FILE_FLAG_OVERLAPPED);
        if (File == INVALID_HANDLE_VALUE)
            return E_FAIL;

        if (!HidD_GetAttributes(File, &Attributes) || !HidD_GetPreparsedData(File, &Data) ||
            HidP_GetCaps(Data, &Caps) != HIDP_STATUS_SUCCESS)
        {
            CloseHandle(File);
            DiTrace(L"HID device %ls could not be parsed", Start.InstanceId);
            return E_FAIL;
        }
        CloseHandle(File);

        VendorId = Attributes.VendorID;
        ProductId = Attributes.ProductID;
        OpenTypeKey();

        hr = BuildObjects();
        if (FAILED(hr))
        {
            DiTrace(L"HID device %ls has objects DirectInput cannot describe, 0x%08lx", Start.InstanceId, hr);
            return hr;
        }

        Source.State = State;
        Source.Types = nullptr;
        Source.Acquire = AcquireSource;
        Source.AcquireCount = -1;
        Source.StateSize = Format.dwDataSize;
        Source.RefCount = 0;

        Params.StateSize = Format.dwDataSize;
        Params.ObjectCount = Format.dwNumObjs;
        Params.Objects = Objects;
        Params.Extra = reinterpret_cast<ULONG_PTR>(this);
        return DiOpenSink(&Params, &Source, &Sink);
    }

    ~RxDiHid() override
    {
        StopReader();
        if (TypeKey != nullptr)
            RegCloseKey(TypeKey);
        if (Start.CalTypeKey != nullptr)
            RegCloseKey(Start.CalTypeKey);
        DiCloseSink(Sink);
        if (Data != nullptr)
            HidD_FreePreparsedData(Data);
        for (ULONG Kind = 0; Kind < _countof(Enabled); Kind++)
            DiFree(reinterpret_cast<PVOID*>(&Enabled[Kind]));
        DiFree(reinterpret_cast<PVOID*>(&Objects));
        DiFree(reinterpret_cast<PVOID*>(&Slots));
        DiFree(reinterpret_cast<PVOID*>(&Groups));
        DiFree(reinterpret_cast<PVOID*>(&Cals));
        DiFree(reinterpret_cast<PVOID*>(&MaskRows));
        DiFree(reinterpret_cast<PVOID*>(&MaskData));
        DiFree(reinterpret_cast<PVOID*>(&DataList));
        DiFree(reinterpret_cast<PVOID*>(&State));
        DiFree(reinterpret_cast<PVOID*>(&Report));
    }

    PRX_DI_SINK GetSink(VOID) override { return Sink; }

    HRESULT
    SetDIData(
        _In_ DWORD AppVersion,
        _In_ RX_DI_APP_HACKS* Hacks) override
    {
        Version = AppVersion;
        return S_OK;
    }

    HRESULT
    GetDataFormat(
        _Outptr_ const DIDATAFORMAT** Result) override
    {
        *Result = &Format;
        return S_OK;
    }

    HRESULT
    GetObjectInfo(
        _In_ const RX_DI_PROP_TARGET* Target,
        _Inout_ LPDIDEVICEOBJECTINSTANCEW Object) override
    {
        const RX_DI_HID_GROUP* Group;
        ULONG Offset;
        UINT Id;

        if (Target->Index >= ObjectCount || Slots[Target->Index].Group == nullptr)
            return E_INVALIDARG;

        Group = Slots[Target->Index].Group;
        ReadOemObjectInfo(Target->Type, Object);

        if (Target->Type & DIDFT_COLLECTION)
        {
            Id = 31;
            Offset = 0;
        }
        else
        {
            if (Target->Type & DIDFT_BUTTON)
                Id = 28;
            else if (Target->Type & DIDFT_AXIS)
                Id = 29;
            else if (Target->Type & DIDFT_POV)
                Id = 30;
            else
                Id = 35;
            Offset = Target->Index - Base[Group->Report] - Group->DataIndexMin;
        }

        if (Object->tszName[0] == L'\0')
            NameObject(Target->Type, Group, Offset, Id, Object->tszName);

        if (Object->dwSize >= sizeof(DIDEVICEOBJECTINSTANCEW))
        {
            USHORT Designator = static_cast<USHORT>(Group->DesignatorMin + Offset);

            Object->wCollectionNumber = Group->LinkCollection;
            Object->wDesignatorIndex = (Designator > Group->DesignatorMax) ? Group->DesignatorMax : Designator;
            Object->wUsagePage = Group->UsagePage;
            Object->wUsage = static_cast<WORD>(Group->UsageMin + Offset);
            Object->dwDimension = Group->Units;
            Object->wExponent = Group->Exponent;
            Object->wReportId = Group->ReportId;
        }
        return S_OK;
    }

    HRESULT
    GetCapabilities(
        _Inout_ LPDIDEVCAPS Result) override
    {
        HANDLE File = OpenDevice(FILE_FLAG_OVERLAPPED);

        if (File != INVALID_HANDLE_VALUE)
        {
            CloseHandle(File);
            Result->dwFlags |= DIDC_ATTACHED;
        }
        if (Polled)
            Result->dwFlags |= DIDC_POLLEDDEVICE;

        Result->dwDevType = DevType;
        Result->dwAxes = AxisCount;
        Result->dwButtons = ButtonCount;
        Result->dwPOVs = PovCount;
        return S_OK;
    }

    HRESULT
    Acquire(VOID) override
    {
        return S_FALSE;
    }

    HRESULT
    GetDeviceState(
        _Out_ PVOID Target) override
    {
        if ((DiSinkFlags(Sink) & DI_SINK_ACQUIRED) == 0)
            return DIERR_INPUTLOST;

        CopyMemory(Target, State, Format.dwDataSize);
        return S_OK;
    }

    HRESULT
    GetDeviceInfo(
        _Inout_ LPDIDEVICEINSTANCEW Result) override
    {
        Result->guidProduct = { static_cast<ULONG>(VendorId | (ProductId << 16)), 0, 0, { 0, 0, 'P', 'I', 'D', 'V', 'I', 'D' } };
        Result->dwDevType = DevType;
        if (Result->dwSize >= sizeof(DIDEVICEINSTANCEW))
        {
            Result->wUsagePage = Caps.UsagePage;
            Result->wUsage = Caps.Usage;
        }

        ReadName(Result->tszProductName);
        ReadName(Result->tszInstanceName);
        return S_OK;
    }

    HRESULT
    GetProperty(
        _In_ const RX_DI_PROP_TARGET* Target,
        _Inout_ LPDIPROPHEADER Header) override
    {
        LPDIPROPDWORD Value = reinterpret_cast<LPDIPROPDWORD>(Header);

        if (Target->Index != MAXULONG)
        {
            if (Target->Index >= ObjectCount)
                return E_NOTIMPL;
            return GetObjectProperty(Target, Header);
        }

        switch (Target->Property)
        {
            case 13:
            case 14:
                ReadName(reinterpret_cast<LPDIPROPSTRING>(Header)->wsz);
                return S_OK;

            case 15:
                if (!DiHidIsJoystick(DevType))
                    return E_NOTIMPL;
                Value->dwData = Start.JoystickId;
                return S_OK;

            case 16:
            {
                BOOLEAN Friendly;

                if (!ReadPortName(reinterpret_cast<LPDIPROPSTRING>(Header)->wsz, &Friendly))
                    return E_FAIL;
                return Friendly ? S_OK : S_FALSE;
            }

            case 12:
            {
                LPDIPROPGUIDANDPATH Path = reinterpret_cast<LPDIPROPGUIDANDPATH>(Header);

                Path->guidClass = GUID_HIDClass;
                StringCchCopyW(Path->wszPath, _countof(Path->wszPath), Start.Path);
                return S_OK;
            }
        }
        return E_NOTIMPL;
    }

    HRESULT
    SetProperty(
        _In_ const RX_DI_PROP_TARGET* Target,
        _In_ LPCDIPROPHEADER Header) override
    {
        if (Target->Index != MAXULONG)
        {
            if (Target->Index >= ObjectCount)
                return E_NOTIMPL;
            return SetObjectProperty(Target, Header);
        }

        switch (Target->Property)
        {
            case 4:
            case 5:
            case 6:
            case 10:
            case 11:
                return SetEveryAxis(Target, Header);

            case DI_PROP_ENABLE_OBJECT:
            {
                BYTE Fill = (reinterpret_cast<const DIPROPDWORD*>(Header)->dwData != 0) ? 1 : 0;

                for (ULONG Kind = 0; Kind < _countof(Enabled); Kind++)
                    FillMemory(Enabled[Kind], ReportIds[Kind], Fill);
                return S_OK;
            }
        }
        return E_NOTIMPL;
    }

    HRESULT
    SetCooperativeLevel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override
    {
        DiSetSinkExclusive(Sink, (Flags & DISCL_EXCLUSIVE) != 0);
        return S_OK;
    }

    HRESULT
    RunControlPanel(
        _In_opt_ HWND Owner,
        _In_ DWORD Flags) override
    {
        return DiRunControlPanel(L"joy.cpl");
    }

    HRESULT
    CreateEffectDriver(
        _Outptr_ RxDiShepherd** Driver) override
    {
        DIHIDFFINITINFO Init = {};
        HKEY Key = nullptr;
        HRESULT hr;

        *Driver = nullptr;
        if (FAILED(GetFFConfigKey(KEY_QUERY_VALUE, &Key)))
            return E_NOTIMPL;

        hr = DiCreateShepherd(Key, Driver);
        if (SUCCEEDED(hr))
        {
            Init.dwSize = sizeof(Init);
            Init.pwszDeviceInterface = Start.Path;
            EnterCriticalSection(&DiLock);
            for (ULONG Index = 0; Index < DiHidCount; Index++)
            {
                if (lstrcmpiW(DiHidEntries[Index].Detail->DevicePath, Start.Path) == 0)
                {
                    Init.GuidInstance = DiHidEntries[Index].Instance;
                    break;
                }
            }
            LeaveCriticalSection(&DiLock);

            hr = (*Driver)->DeviceID(Start.JoystickId, TRUE, &Init);
            if (FAILED(hr))
            {
                DiTrace(L"effect driver refused HID device %ls, 0x%08lx", Start.InstanceId, hr);
                delete *Driver;
                *Driver = nullptr;
            }
        }

        if (Key != nullptr)
            RegCloseKey(Key);
        return hr;
    }

    /* S_FALSE means the PID driver has to be created before its key exists */
    HRESULT
    GetFFConfigKey(
        _In_ REGSAM Access,
        _Out_ HKEY* Key) override
    {
        HRESULT hr;

        *Key = nullptr;
        if (TypeKey == nullptr)
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        else if (RegOpenKeyExW(TypeKey, L"OEMForceFeedback", 0, Access, Key) == ERROR_SUCCESS)
            return S_OK;
        else
            hr = E_FAIL;

        if (PidDevice)
        {
            *Key = nullptr;
            hr = S_FALSE;
        }
        return hr;
    }

    HRESULT
    Poll(VOID) override
    {
        return Polled ? S_OK : S_FALSE;
    }

    ULONG
    GetUsage(
        _In_ ULONG Index) override
    {
        const RX_DI_HID_GROUP* Group;
        ULONG Offset = 0;

        if (Index >= ObjectCount || Slots[Index].Group == nullptr)
            return 0;

        Group = Slots[Index].Group;
        if ((Objects[Index].dwType & DIDFT_COLLECTION) == 0)
            Offset = Index - Base[Group->Report] - Group->DataIndexMin;
        return DiHidUsageKey(Group->UsagePage, Group->UsageMin + Offset);
    }

    /* Looks objects up through the GUID they were given, so only the first usage of each slot matches */
    HRESULT
    MapUsage(
        _In_ ULONG Usage,
        _Out_ PULONG Index) override
    {
        for (ULONG Kind = 0; Kind < GroupCount; Kind++)
        {
            const RX_DI_HID_GROUP* Group = &Groups[Kind];

            for (ULONG Step = 0; Step < Group->Count; Step++)
            {
                ULONG Object = Base[HidP_Input] + Group->DataIndexMin + Step;
                ULONG Found = 0;

                if (Object >= ObjectCount || Objects[Object].pguid == nullptr)
                    continue;

                for (ULONG Entry = 0; Entry < _countof(DiUsageSlots); Entry++)
                {
                    if (IsEqualGUID(*DiUsageSlots[Entry].Guid, *Objects[Object].pguid))
                    {
                        Found = DiUsageSlots[Entry].Usage;
                        break;
                    }
                }

                if (Found == Usage)
                {
                    *Index = Object;
                    return S_OK;
                }
            }
        }
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

private:
    VOID
    OpenTypeKey(VOID)
    {
        WCHAR Type[20];

        /* The gameport adapters stand for whatever stick is plugged into them */
        if (VendorId == DI_HID_MS_VENDOR && ProductId >= DI_HID_MS_GAMEPORT_LOW && ProductId <= DI_HID_MS_GAMEPORT_HIGH)
        {
#if DIRECTINPUT_VERSION < 0x0800
            if (DevType == DI_DEVTYPE_HID_JOYSTICK)
                DevType = DI_DEVTYPE_HID_STICK;
#endif
            return;
        }

        StringCchPrintfW(Type, _countof(Type), L"VID_%04X&PID_%04X", VendorId, ProductId);
        DiOpenJoyTypeKey(Type, MAXIMUM_ALLOWED, &TypeKey);
    }

    /* The joystick type settings say what kind of stick a HID joystick is */
    VOID
    ApplyTypeKind(VOID)
    {
#if DIRECTINPUT_VERSION < 0x0800
        JOYREGHWSETTINGS Settings = {};
        UCHAR Kind;

        if (LOBYTE(DevType) != DIDEVTYPE_JOYSTICK)
            return;
        if (TypeKey != nullptr)
            DiReadValue(TypeKey, L"OEMData", &Settings, sizeof(Settings));

        if (Settings.dwFlags & JOY_HWS_ISYOKE)
            Kind = DIDEVTYPEJOYSTICK_FLIGHTSTICK;
        else if (Settings.dwFlags & JOY_HWS_ISGAMEPAD)
            Kind = DIDEVTYPEJOYSTICK_GAMEPAD;
        else if (Settings.dwFlags & JOY_HWS_ISCARCTRL)
            Kind = DIDEVTYPEJOYSTICK_WHEEL;
        else if (Settings.dwFlags & DI_JOY_HWS_HEADTRACKER)
            Kind = DIDEVTYPEJOYSTICK_HEADTRACKER;
        else
            return;

        DevType = DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_JOYSTICK, Kind);
#endif
    }

    /* The type key can mark objects as actuators or triggers and override their aspect */
    VOID
    ApplyTypeAttributes(VOID)
    {
        if (TypeKey == nullptr)
            return;

        for (ULONG Index = 0; Index < Format.dwNumObjs; Index++)
        {
            DIOBJECTDATAFORMAT* Object = &Objects[Index];
            WCHAR Path[32];
            PCWSTR Kind;
            DWORD Attributes = 0;
            HKEY Key;

            if (Object->dwType & DIDFT_AXIS)
                Kind = L"Axes";
            else if (Object->dwType & DIDFT_BUTTON)
                Kind = L"Buttons";
            else if (Object->dwType & DIDFT_POV)
                Kind = L"POVs";
            else if (Object->dwType & DIDFT_NODATA)
                Kind = L"Actuators";
            else
                continue;

            StringCchPrintfW(Path, _countof(Path), L"%s\\%u", Kind, DIDFT_GETINSTANCE(Object->dwType));
            if (RegOpenKeyExW(TypeKey, Path, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
                continue;

            if (SUCCEEDED(DiReadValue(Key, L"Attributes", &Attributes, sizeof(Attributes))))
            {
                Object->dwType |= Attributes << 24;
                Object->dwFlags |= Attributes & ~DIDOI_ASPECTMASK;
                if (Attributes & DIDOI_ASPECTMASK)
                    Object->dwFlags = (Object->dwFlags & ~DIDOI_ASPECTMASK) | (Attributes & DIDOI_ASPECTMASK);
            }
            RegCloseKey(Key);
        }
    }

    HANDLE
    OpenDevice(
        _In_ DWORD Flags)
    {
        HANDLE File = CreateFileW(Start.Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, Flags, nullptr);
        ULONG Frequency = 0;
        DWORD Returned = 0;

        /* A device with a poll interval has to be asked for its reports */
        Polled = DeviceIoControl(File, DI_IOCTL_POLL_FREQUENCY, &Frequency, sizeof(Frequency), &Frequency,
                                 sizeof(Frequency), &Returned, nullptr) != FALSE;
        return File;
    }

    /* Product names come from the parent device, the device strings or the device description */
    VOID
    ReadName(
        _Out_writes_(MAX_PATH) PWSTR Name)
    {
        HANDLE File;

        Name[0] = L'\0';
        if (ReadParentDescription(Name))
            return;

        File = OpenDevice(FILE_FLAG_OVERLAPPED);
        if (File != INVALID_HANDLE_VALUE)
        {
            BOOLEAN Got = HidD_GetProductString(File, Name, MAX_PATH * sizeof(WCHAR)) && Name[0] != L'\0';

            CloseHandle(File);
            if (Got)
                return;
        }

        if (ReadOwnDescription(Name))
            return;

        LoadStringW(DiModule, (LOBYTE(DevType) == DI_HID_TYPE_MOUSE) ? IDS_MOUSE :
                              (LOBYTE(DevType) == DI_HID_TYPE_KEYBOARD) ? IDS_KEYBOARD : 0x2008, Name, MAX_PATH);
    }

    BOOLEAN
    ReadParentDescription(
        _Out_writes_(MAX_PATH) PWSTR Name)
    {
        SP_DEVINFO_DATA Device = { sizeof(Device) };
        WCHAR ParentId[MAX_PATH];
        DEVINST Parent = 0;
        BOOLEAN Matched = FALSE;
        HDEVINFO Set;
        HKEY Driver;

        Set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
        if (Set == INVALID_HANDLE_VALUE)
            return FALSE;

        /* Only parents whose driver matched one of their own hardware ids name the device */
        if (SetupDiOpenDeviceInfoW(Set, Start.InstanceId, nullptr, 0, &Device) &&
            CM_Get_Parent(&Parent, Device.DevInst, 0) == CR_SUCCESS &&
            CM_Get_Device_IDW(Parent, ParentId, _countof(ParentId), 0) == CR_SUCCESS &&
            SetupDiOpenDeviceInfoW(Set, ParentId, nullptr, 0, &Device))
        {
            Driver = SetupDiOpenDevRegKey(Set, &Device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, MAXIMUM_ALLOWED);
            if (Driver != INVALID_HANDLE_VALUE)
            {
                ULONG Size = 0;
                CONFIGRET Result = CM_Get_DevNode_Registry_PropertyW(Parent, CM_DRP_HARDWAREID, nullptr, nullptr, &Size, 0);
                PWSTR Ids = nullptr;

                if ((Result == CR_BUFFER_SMALL || Result == CR_SUCCESS) && Size != 0 &&
                    SUCCEEDED(DiAllocate(Size + MAX_PATH * sizeof(WCHAR), reinterpret_cast<PVOID*>(&Ids))))
                {
                    PWSTR Matching = Ids;
                    PWSTR List = Ids + MAX_PATH;
                    DWORD Length = MAX_PATH * sizeof(WCHAR);

                    if (CM_Get_DevNode_Registry_PropertyW(Parent, CM_DRP_HARDWAREID, nullptr, List, &Size, 0) == CR_SUCCESS &&
                        RegQueryValueExW(Driver, L"MatchingDeviceId", nullptr, nullptr, reinterpret_cast<PBYTE>(Matching), &Length) == ERROR_SUCCESS)
                    {
                        for (PWSTR Id = List; *Id != L'\0'; Id += lstrlenW(Id) + 1)
                        {
                            if (lstrcmpiW(Id, Matching) == 0)
                            {
                                Matched = TRUE;
                                break;
                            }
                        }
                    }
                    DiFree(reinterpret_cast<PVOID*>(&Ids));
                }
                RegCloseKey(Driver);
            }
        }

        if (Matched)
        {
            ULONG Size = MAX_PATH * sizeof(WCHAR);

            Matched = CM_Get_DevNode_Registry_PropertyW(Parent, CM_DRP_DEVICEDESC, nullptr, Name, &Size, 0) == CR_SUCCESS;
        }

        SetupDiDestroyDeviceInfoList(Set);
        return Matched;
    }

    BOOLEAN
    ReadOwnDescription(
        _Out_writes_(MAX_PATH) PWSTR Name)
    {
        SP_DEVINFO_DATA Device = { sizeof(Device) };
        BOOLEAN Got = FALSE;
        HDEVINFO Set;

        Set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
        if (Set == INVALID_HANDLE_VALUE)
            return FALSE;

        /* The description buffer is counted in bytes, which caps it at half a path */
        if (SetupDiOpenDeviceInfoW(Set, Start.InstanceId, nullptr, 0, &Device))
            Got = SetupDiGetDeviceRegistryPropertyW(Set, &Device, SPDRP_DEVICEDESC, nullptr,
                                                    reinterpret_cast<PBYTE>(Name), MAX_PATH, nullptr) != FALSE;

        SetupDiDestroyDeviceInfoList(Set);
        return Got;
    }

    /* Object names, attributes and force feedback limits an OEM put in the type key */
    VOID
    ReadOemObjectInfo(
        _In_ ULONG Type,
        _Inout_ LPDIDEVICEOBJECTINSTANCEW Object)
    {
        struct
        {
            ULONG Flags;
            USHORT Page;
            USHORT Usage;
        } Attributes = {};
        HRESULT Read;
        HKEY Key;
        LONG Error;

        if (FAILED(DiHidOpenObjectKey(TypeKey, Type, KEY_QUERY_VALUE, &Key)))
            return;

        Read = DiReadValue(Key, L"Attributes", &Attributes, sizeof(Attributes));
        if (SUCCEEDED(Read))
        {
            Object->dwFlags |= Attributes.Flags & ~DIDOI_ASPECTMASK;
            if (Attributes.Flags & DIDOI_ASPECTMASK)
                Object->dwFlags = (Object->dwFlags & ~DIDOI_ASPECTMASK) | (Attributes.Flags & DIDOI_ASPECTMASK);
        }

        if (Object->dwSize >= sizeof(DIDEVICEOBJECTINSTANCEW))
        {
            DWORD Force[2] = {};

            if (SUCCEEDED(Read) && Attributes.Page != 0 && Attributes.Usage != 0)
            {
                Object->wUsagePage = Attributes.Page;
                Object->wUsage = Attributes.Usage;
            }
            if (FAILED(DiReadValue(Key, L"FFAttributes", Force, sizeof(Force))))
                ZeroMemory(Force, sizeof(Force));
            Object->dwFFMaxForce = Force[0];
            Object->dwFFForceResolution = Force[1];
        }

        Object->tszName[MAX_PATH - 1] = L'\0';
        Error = DiHidReadString(Key, nullptr, Object->tszName, MAX_PATH - 1);
        if (Error == ERROR_MORE_DATA)
            Object->tszName[MAX_PATH - 2] = L'\0';
        else if (Error != ERROR_SUCCESS)
            Object->tszName[0] = L'\0';
        RegCloseKey(Key);
    }

    /* Usage names where the page has them, numbered kind names otherwise */
    VOID
    NameObject(
        _In_ ULONG Type,
        _In_ const RX_DI_HID_GROUP* Group,
        _In_ ULONG Offset,
        _In_ UINT Id,
        _Out_writes_(MAX_PATH) PWSTR Name)
    {
        WCHAR Pattern[64];
        WCHAR Numbered[256];
        ULONG Usage = Group->UsageMin + Offset;

        if (Group->UsagePage < _countof(DiUsageNameBase) && DiUsageNameBase[Group->UsagePage] != 0 && Usage < 0x200 &&
            LoadStringW(DiModule, DiUsageNameBase[Group->UsagePage] + Usage, Name, MAX_PATH) != 0)
        {
            if (Type & DIDFT_COLLECTION)
            {
                WCHAR Text[MAX_PATH];

                StringCchCopyW(Text, _countof(Text), Name);
                LoadStringW(DiModule, 32, Pattern, _countof(Pattern));
                StringCchPrintfW(Name, MAX_PATH, Pattern, DIDFT_GETINSTANCE(Type), Text);
            }
            return;
        }

        LoadStringW(DiModule, Id, Numbered, _countof(Numbered));
        StringCchPrintfW(Name, MAX_PATH, Numbered, DIDFT_GETINSTANCE(Type));
    }

    HRESULT
    GetObjectProperty(
        _In_ const RX_DI_PROP_TARGET* Target,
        _Inout_ LPDIPROPHEADER Header)
    {
        const RX_DI_HID_SLOT* Slot = &Slots[Target->Index];
        LPDIPROPDWORD Value = reinterpret_cast<LPDIPROPDWORD>(Header);

        if (Target->Property == DI_PROP_ENABLE_OBJECT)
        {
            Value->dwData = 0;
            if (Slot->Group != nullptr && Slot->Group->Report < _countof(Enabled))
                Value->dwData = Enabled[Slot->Group->Report][Slot->Group->ReportId];
            return S_OK;
        }

        if (Target->Type & DIDFT_POV)
        {
            if (Slot->Group == nullptr)
                return E_NOTIMPL;
            if (Slot->Group->PovPoints && Target->Property == 10)
                return (Slot->Cal != nullptr) ? DiHidGetCalProperty(Slot->Cal, 10, Header, Version) : E_NOTIMPL;
            if (Target->Property != 3)
                return E_NOTIMPL;
            Value->dwData = Slot->Group->Granularity;
            return S_OK;
        }

        if ((Target->Type & DIDFT_RELAXIS) == 0 && (Target->Type & DIDFT_ABSAXIS) != 0 && Slot->Cal != nullptr)
            return DiHidGetCalProperty(Slot->Cal, Target->Property, Header, Version);
        return E_NOTIMPL;
    }

    HRESULT
    SetObjectProperty(
        _In_ const RX_DI_PROP_TARGET* Target,
        _In_ LPCDIPROPHEADER Header)
    {
        RX_DI_HID_SLOT* Slot = &Slots[Target->Index];
        HRESULT hr;

        if (Slot->Group == nullptr)
            return E_NOTIMPL;

        if (Target->Property == DI_PROP_ENABLE_OBJECT)
        {
            BOOLEAN On = reinterpret_cast<const DIPROPDWORD*>(Header)->dwData == 1;

            if (Slot->Group->Report < _countof(Enabled))
                Enabled[Slot->Group->Report][Slot->Group->ReportId] = On ? 1 : 0;
            Slot->Group->Disabled = !On;
            return S_OK;
        }

        if (Slot->Cal == nullptr)
            return E_NOTIMPL;

        if (Target->Type == DIDFT_POV)
        {
            /* Only the DirectX 5 betas could calibrate the points of a POV */
            if (Version != 0x05B2 || !Slot->Group->PovPoints)
                return E_NOTIMPL;
            hr = DiHidSetCalProperty(Slot->Cal, Target, Header, Start.CalTypeKey, Version);
            if (FAILED(hr))
                return hr;
            LoadCalibrations();
        }
        else
        {
            if ((Target->Type & DIDFT_RELAXIS) != 0 || (Target->Type & DIDFT_ABSAXIS) == 0)
                return E_NOTIMPL;
            hr = DiHidSetCalProperty(Slot->Cal, Target, Header, Start.CalTypeKey, Version);
            LoadCalibrations();
            if (FAILED(hr))
                return hr;
        }

        ResetState();
        return S_OK;
    }

    /* The whole device means each plain input axis and POV, last object first */
    HRESULT
    SetEveryAxis(
        _In_ const RX_DI_PROP_TARGET* Target,
        _In_ LPCDIPROPHEADER Header)
    {
        BYTE Copy[DI_HID_PROPCALPOV_BYTES] = {};
        LPDIPROPHEADER Each = reinterpret_cast<LPDIPROPHEADER>(Copy);
        RX_DI_PROP_TARGET One = *Target;

        CopyMemory(Copy, Header, (Header->dwSize < sizeof(Copy)) ? Header->dwSize : sizeof(Copy));
        Each->dwHow = DIPH_BYID;

        for (ULONG Remaining = ObjectCount; Remaining > 0; Remaining -= 1)
        {
            ULONG Index = Remaining - 1;
            ULONG Type = Objects[Index].dwType;
            HRESULT hr;

            if ((Type & (DIDFT_OUTPUT | DIDFT_ALIAS | DIDFT_VENDORDEFINED | DIDFT_ABSAXIS)) != DIDFT_ABSAXIS &&
                (Type & (DIDFT_OUTPUT | DIDFT_ALIAS | DIDFT_VENDORDEFINED | DIDFT_POV)) != DIDFT_POV)
            {
                continue;
            }

            Each->dwObj = Type;
            One.Index = Index;
            One.Type = Type;
            hr = SetObjectProperty(&One, Each);
            if (FAILED(hr) && hr != E_NOTIMPL)
                return hr;
        }
        return S_OK;
    }

    /* Calibrations come from the device's own Type key, defaults from the logical range */
    VOID
    LoadCalibrations(VOID)
    {
        for (ULONG Index = 0; Index < ObjectCount; Index++)
        {
            RX_DI_HID_CAL* Cal = Slots[Index].Cal;
            const RX_DI_HID_GROUP* Group = Slots[Index].Group;
            ULONG Type = Objects[Index].dwType;
            LONG Saved[3] = {};
            HKEY Key;

            if (Cal == nullptr || Group == nullptr)
                continue;

            if (Type & DIDFT_POV)
            {
                LONG Points[10] = {};

                if (!Group->PovPoints)
                    continue;
                if (SUCCEEDED(DiHidOpenObjectKey(Start.CalTypeKey, Type, KEY_QUERY_VALUE, &Key)))
                {
                    DiReadValue(Key, L"Calibration", Points, sizeof(Points));
                    RegCloseKey(Key);
                }
                CopyMemory(Cal->PovLow, Points, sizeof(Cal->PovLow));
                CopyMemory(Cal->PovHigh, Points + 5, sizeof(Cal->PovHigh));
                continue;
            }

            if (FAILED(DiHidOpenObjectKey(Start.CalTypeKey, Type, KEY_QUERY_VALUE, &Key)) ||
                FAILED(ReadCalibration(Key, Saved)))
            {
                LONG Low = Group->LogicalMin;
                LONG High = Group->LogicalMax;

                if (Low >= High)
                {
                    Low = 0;
                    High = 655;
                }
                else if (VendorId == DI_HID_MS_VENDOR && (ProductId & 0xFF00) == 0x0100)
                {
                    High = (High - Low) / 11 + Low;
                }
                Saved[0] = Low;
                Saved[1] = Low + static_cast<LONG>(static_cast<ULONG>(High - Low + 1) >> 1);
                Saved[2] = High;
            }

            Cal->Min = Saved[0];
            Cal->Center = Saved[1];
            Cal->Max = Saved[2];
            DiHidRecalc(Cal);
        }
    }

    static
    HRESULT
    ReadCalibration(
        _In_ HKEY Key,
        _Out_writes_(3) PLONG Saved)
    {
        HRESULT hr = DiReadValue(Key, L"Calibration", Saved, 3 * sizeof(LONG));

        RegCloseKey(Key);
        return hr;
    }

    /* Values take a dword and buttons a byte, objects in output reports carry no data */
    VOID
    PlaceObject(
        _Inout_ DIOBJECTDATAFORMAT* Object,
        _In_ const RX_DI_HID_GROUP* Group,
        _Inout_ PULONG Type,
        _In_ ULONG Size)
    {
        if (Group->Report == HidP_Output)
        {
            *Type |= DIDFT_NODATA | DIDFT_OUTPUT;
            return;
        }

        Object->dwOfs = DataSize;
        if (!Group->Alias)
            DataSize += Size;
        if (Group->Report == HidP_Feature)
            *Type |= DIDFT_OUTPUT;
    }

    /* Input values on these pages are axes, except two generic desktop usages and button page values */
    ULONG
    ClassifyInputValue(
        _In_ const RX_DI_HID_GROUP* Group,
        _In_ ULONG Usage)
    {
        switch (Group->UsagePage)
        {
            case HID_USAGE_PAGE_GENERIC:
                if (Usage == 0x3A || Usage == 0x3B)
                    return 0;
                return Group->Absolute ? DIDFT_ABSAXIS : DIDFT_RELAXIS;

            case HID_USAGE_PAGE_SIMULATION:
            case HID_USAGE_PAGE_VR:
            case HID_USAGE_PAGE_SPORT:
            case HID_USAGE_PAGE_GAME:
            case HID_USAGE_PAGE_KEYBOARD:
            case HID_USAGE_PAGE_CONSUMER:
            case HID_USAGE_PAGE_DIGITIZER:
                return Group->Absolute ? DIDFT_ABSAXIS : DIDFT_RELAXIS;

            case HID_USAGE_PAGE_BUTTON:
                if (!Group->Absolute)
                    return DIDFT_RELAXIS;
                if (!Group->Alias)
                    ButtonCount++;
                return DIDFT_PSHBUTTON;
        }
        return 0;
    }

    HRESULT
    AddValueGroup(
        _In_ HIDP_REPORT_TYPE Kind,
        _Inout_ HIDP_VALUE_CAPS* Entry)
    {
        RX_DI_HID_GROUP* Group = &Groups[GroupCount++];
        LONG SignedMax;
        LONG UnsignedMax;
        ULONG Aspect = 0;
        BOOLEAN Pov;

        if (Entry->IsRange)
        {
            if (Entry->Range.DataIndexMax - Entry->Range.DataIndexMin != Entry->Range.UsageMax - Entry->Range.UsageMin)
                return E_FAIL;
        }
        else
        {
            Entry->Range.UsageMax = Entry->Range.UsageMin;
        }

        if (Kind == HidP_Input && !Entry->IsAlias)
            Aspect = DiHidValueAspect(Entry);

        Group->ReportId = Entry->ReportID;
        if (Entry->ReportID > ReportIds[Kind])
            ReportIds[Kind] = Entry->ReportID;
        Group->UsagePage = Entry->UsagePage;
        Group->UsageMin = Entry->Range.UsageMin;
        Group->DataIndexMin = Entry->Range.DataIndexMin;
        Group->Count = Entry->Range.UsageMax - Entry->Range.UsageMin + 1;
        Group->LinkCollection = Entry->LinkCollection;
        Group->Units = Entry->Units;
        Group->Exponent = static_cast<USHORT>(Entry->UnitsExp);
        Group->LogicalMin = Entry->LogicalMin;
        Group->LogicalMax = Entry->LogicalMax;
        Group->PhysicalMin = Entry->PhysicalMin;
        Group->PhysicalMax = Entry->PhysicalMax;
        DiHidFieldLimits(Entry->BitSize, &Group->SignMask, &SignedMax, &UnsignedMax);

        /* Without a usable logical range the physical one or the field size stands in */
        Group->Signed = TRUE;
        if (Group->LogicalMin < Group->LogicalMax || Group->PhysicalMin >= Group->PhysicalMax)
        {
            if (Group->LogicalMin >= Group->LogicalMax)
            {
                Group->LogicalMin = Group->SignMask;
                Group->LogicalMax = SignedMax;
            }

            if (Group->PhysicalMin > Group->PhysicalMax)
            {
                Group->LogicalMin = 0;
                Group->LogicalMax = UnsignedMax;
                Group->PhysicalMin = 0;
                Group->PhysicalMax = UnsignedMax;
                Group->Signed = FALSE;
            }
            else if (Group->PhysicalMin == Group->PhysicalMax)
            {
                Group->PhysicalMin = Group->LogicalMin;
                Group->PhysicalMax = Group->LogicalMax;
            }
        }
        else
        {
            Group->LogicalMin = Group->PhysicalMin;
            Group->LogicalMax = Group->PhysicalMax;
        }

        if (Group->Signed && (Group->LogicalMin < Group->SignMask || Group->LogicalMax > SignedMax))
        {
            if (Group->LogicalMin >= 0 && Group->LogicalMax <= UnsignedMax)
            {
                Group->Signed = FALSE;
            }
            else
            {
                if (Group->UsagePage < 0xFF00)
                    return E_FAIL;
                Group->LogicalMin = Group->SignMask;
                Group->LogicalMax = SignedMax;
            }
        }
        if (!Group->Signed)
            Group->SignMask = UnsignedMax;

        Group->StringMin = Entry->Range.StringMin;
        Group->StringMax = Entry->IsStringRange ? Entry->Range.StringMax : Entry->Range.StringMin;
        Group->DesignatorMin = Entry->Range.DesignatorMin;
        Group->DesignatorMax = Entry->IsDesignatorRange ? Entry->Range.DesignatorMax : Entry->Range.DesignatorMin;
        Group->Absolute = Entry->IsAbsolute;
        Group->Value = TRUE;
        Group->Report = Kind;
        Group->Alias = Entry->IsAlias;

        /* Steering and rudder controls say what kind of joystick this is */
#if DIRECTINPUT_VERSION < 0x0800
        if (Group->UsagePage == HID_USAGE_PAGE_SIMULATION)
        {
            if (Group->UsageMin == 0xC8)
                DevType = DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_JOYSTICK, DIDEVTYPEJOYSTICK_WHEEL);
            else if (Group->UsageMin == 0xBA)
                DevType = DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_JOYSTICK, DIDEVTYPEJOYSTICK_RUDDER);
        }
#endif

        Pov = Group->Count == 1 &&
              ((Group->UsagePage == HID_USAGE_PAGE_GENERIC && Group->UsageMin == HID_USAGE_GENERIC_HATSWITCH) ||
               (Group->UsagePage == HID_USAGE_PAGE_GAME && Group->UsageMin == 0x20));
        if (Pov && Group->LogicalMax - Group->LogicalMin != -1)
            Group->Granularity = static_cast<USHORT>(36000UL / static_cast<ULONG>(Group->LogicalMax - Group->LogicalMin + 1));

        /* Microsoft pads without a null state report their hat as points to calibrate */
        if (!Entry->HasNull && Pov && VendorId == DI_HID_MS_VENDOR && (ProductId & 0xFF00) == 0x0100)
        {
            if (Group->LogicalMax - Group->LogicalMin != -1)
                Group->Granularity = 9000;
            Group->PovPoints = TRUE;
        }

        for (ULONG Step = 0; Step < Group->Count; Step++)
        {
            ULONG Index = Base[Kind] + Group->DataIndexMin + Step;
            USHORT Usage = static_cast<USHORT>(Group->UsageMin + Step);
            DIOBJECTDATAFORMAT* Object;
            ULONG AliasBit = Group->Alias ? DIDFT_ALIAS : 0;
            ULONG Type;
            ULONG Flags;

            if (Index >= ObjectCount)
                break;
            if (Slots[Index].Group != nullptr)
                return E_FAIL;
            Slots[Index].Group = Group;
            Object = &Objects[Index];

            if (LOBYTE(DevType) == DI_HID_TYPE_MOUSE && Usage == HID_USAGE_GENERIC_WHEEL && Group->UsagePage == HID_USAGE_PAGE_GENERIC)
            {
                Object->pguid = &GUID_ZAxis;
                Group->Granularity = 0;
            }
            else if (Kind != HidP_Input)
            {
                Object->pguid = &GUID_Unknown;
            }
            else
            {
                const RX_DI_USAGE_SLOT* Known = DiHidFindUsageSlot(DiHidUsageKey(Group->UsagePage, Usage));

                Object->pguid = (Known != nullptr) ? Known->Guid : &GUID_Unknown;
            }

            if (Group->UsagePage >= 0xFF00)
            {
                Type = DIDFT_VENDORDEFINED | AliasBit;
            }
            else if (Object->pguid == &GUID_POV)
            {
                Type = DIDFT_POV | AliasBit;
                if (!Group->Alias)
                    PovCount++;
            }
            else if (Kind == HidP_Input)
            {
                Type = ClassifyInputValue(Group, Usage) | AliasBit;
                if ((Type & DIDFT_AXIS) && !Group->Alias)
                    AxisCount++;
            }
            else
            {
                Type = AliasBit;
            }

            Type |= DIDFT_MAKEINSTANCE(static_cast<USHORT>(Index));
            PlaceObject(Object, Group, &Type, sizeof(DWORD));

            if (Kind != HidP_Input)
                Flags = 0x80000000 | DIDOI_POLLED;
            else
                Flags = Aspect | (Polled ? DIDOI_POLLED : 0);

            /* A PID device can push on any usage its output reports name */
            if (PidDevice && !IsEqualGUID(*Object->pguid, GUID_Unknown))
            {
                USHORT Length = 0;
                NTSTATUS Status = HidP_GetSpecificButtonCaps(HidP_Output, Group->UsagePage, 0, Usage, nullptr, &Length, Data);

                if (Status >= 0 || Status == HIDP_STATUS_BUFFER_TOO_SMALL)
                {
                    Type |= DIDFT_FFACTUATOR;
                    Flags |= DIDOI_FFACTUATOR;
                }
            }

            Object->dwType = Type;
            Object->dwFlags = Flags;

            if (Kind == HidP_Input)
            {
                RX_DI_HID_CAL* Cal = &Cals[CalCount++];

                Slots[Index].Cal = Cal;
                Cal->Saturation = 10000;
                if (Type & DIDFT_ABSAXIS)
                {
                    Cal->RangeMin = 0;
                    Cal->RangeMax = DI_HID_AXIS_MAX;
                    Cal->RangeCenter = DI_HID_AXIS_MAX / 2;
                }
                else if ((Type & DIDFT_POV) && Group->PovPoints)
                {
                    Cal->PovPoints = TRUE;
                }
            }
        }
        return S_OK;
    }

    HRESULT
    AddButtonGroup(
        _In_ HIDP_REPORT_TYPE Kind,
        _Inout_ HIDP_BUTTON_CAPS* Entry,
        _Inout_ PBYTE ButtonReports)
    {
        RX_DI_HID_GROUP* Group = &Groups[GroupCount++];

        if (Entry->IsRange)
        {
            if (Entry->Range.DataIndexMax - Entry->Range.DataIndexMin != Entry->Range.UsageMax - Entry->Range.UsageMin)
                return E_FAIL;
        }
        else
        {
            Entry->Range.UsageMax = Entry->Range.UsageMin;
        }

        Group->ReportId = Entry->ReportID;
        if (Entry->ReportID > ReportIds[Kind])
            ReportIds[Kind] = Entry->ReportID;
        Group->UsagePage = Entry->UsagePage;
        Group->UsageMin = Entry->Range.UsageMin;
        Group->DataIndexMin = Entry->Range.DataIndexMin;
        Group->Count = Entry->Range.UsageMax - Entry->Range.UsageMin + 1;
        Group->SignMask = -1;
        Group->LinkCollection = Entry->LinkCollection;
        Group->StringMin = Entry->Range.StringMin;
        Group->StringMax = Entry->IsStringRange ? Entry->Range.StringMax : Entry->Range.StringMin;
        Group->DesignatorMin = Entry->Range.DesignatorMin;
        Group->DesignatorMax = Entry->IsDesignatorRange ? Entry->Range.DesignatorMax : Entry->Range.DesignatorMin;
        Group->Report = Kind;
        Group->Absolute = Entry->IsAbsolute;
        Group->Alias = Entry->IsAlias;

        for (ULONG Step = 0; Step < Group->Count; Step++)
        {
            ULONG Index = Base[Kind] + Group->DataIndexMin + Step;
            DIOBJECTDATAFORMAT* Object;
            ULONG Type;
            ULONG Flags;

            if (Index >= ObjectCount)
                break;
            if (Slots[Index].Group != nullptr)
                return E_FAIL;
            if (Kind != HidP_Output)
                ButtonReports[Index] = Entry->ReportID;
            Slots[Index].Group = Group;
            Object = &Objects[Index];
            Object->pguid = &GUID_Unknown;

            if (Group->UsagePage >= 0xFF00)
            {
                Type = DIDFT_VENDORDEFINED | (Group->Alias ? DIDFT_ALIAS : 0);
            }
            else
            {
                Type = Group->Alias ? DIDFT_ALIAS : 0;
                if (Kind == HidP_Input)
                {
                    switch (Group->UsagePage)
                    {
                        case HID_USAGE_PAGE_GENERIC:
                        case HID_USAGE_PAGE_SIMULATION:
                        case HID_USAGE_PAGE_VR:
                        case HID_USAGE_PAGE_SPORT:
                        case HID_USAGE_PAGE_GAME:
                        case HID_USAGE_PAGE_BUTTON:
                        case HID_USAGE_PAGE_CONSUMER:
                        case HID_USAGE_PAGE_DIGITIZER:
                            Object->pguid = &GUID_Button;
                            break;
                        case HID_USAGE_PAGE_KEYBOARD:
                            Object->pguid = &GUID_Key;
                            break;
                    }
                    if (Object->pguid != &GUID_Unknown)
                    {
                        Type |= DIDFT_PSHBUTTON;
                        if (!Group->Alias)
                            ButtonCount++;
                    }
                }
            }

            Type |= DIDFT_MAKEINSTANCE(static_cast<USHORT>(Index));
            PlaceObject(Object, Group, &Type, sizeof(BYTE));

            Flags = (Kind != HidP_Input) ? (0x80000000 | DIDOI_POLLED) : (Polled ? DIDOI_POLLED : 0);
            if (PidDevice && (Type & DIDFT_PSHBUTTON))
            {
                Type |= DIDFT_FFEFFECTTRIGGER;
                Flags |= DIDOI_FFEFFECTTRIGGER;
            }

            Object->dwType = Type;
            Object->dwFlags = Flags;
        }
        return S_OK;
    }

    HRESULT
    AddValueGroups(
        _In_ HIDP_REPORT_TYPE Kind,
        _In_ USHORT Expected)
    {
        HIDP_VALUE_CAPS* List = nullptr;
        USHORT Count = Expected;
        HRESULT hr;

        if (Expected == 0)
            return S_OK;

        hr = DiAllocate(sizeof(*List) * Expected, reinterpret_cast<PVOID*>(&List));
        if (FAILED(hr))
            return hr;

        if (HidP_GetValueCaps(Kind, List, &Count, Data) < 0 || Count != Expected)
        {
            hr = E_FAIL;
        }
        else
        {
            DiHidSortCaps(List, Count);
            for (USHORT Index = 0; Index < Count && SUCCEEDED(hr); Index++)
                hr = AddValueGroup(Kind, &List[Index]);
        }

        DiFree(reinterpret_cast<PVOID*>(&List));
        return hr;
    }

    HRESULT
    AddButtonGroups(
        _In_ HIDP_REPORT_TYPE Kind,
        _In_ USHORT Expected,
        _Inout_ PBYTE ButtonReports)
    {
        HIDP_BUTTON_CAPS* List = nullptr;
        USHORT Count = Expected;
        HRESULT hr;

        if (Expected == 0)
            return S_OK;

        hr = DiAllocate(sizeof(*List) * Expected, reinterpret_cast<PVOID*>(&List));
        if (FAILED(hr))
            return hr;

        if (HidP_GetButtonCaps(Kind, List, &Count, Data) < 0 || Count != Expected)
        {
            hr = E_FAIL;
        }
        else
        {
            DiHidSortCaps(List, Count);
            for (USHORT Index = 0; Index < Count && SUCCEEDED(hr); Index++)
                hr = AddButtonGroup(Kind, &List[Index], ButtonReports);
        }

        DiFree(reinterpret_cast<PVOID*>(&List));
        return hr;
    }

    /* Each report id gets a mask that clears only the buttons it carries */
    HRESULT
    BuildButtonMasks(
        _In_reads_(ObjectCount) const BYTE* ButtonReports)
    {
        ULONG Rows = ReportIds[HidP_Input];
        ULONG Next = 0;
        HRESULT hr;

        if (ReportIds[HidP_Output] > Rows)
            Rows = ReportIds[HidP_Output];
        if (ReportIds[HidP_Feature] > Rows)
            Rows = ReportIds[HidP_Feature];
        if (Rows == 0 || ButtonBytes == 0)
            return S_OK;

        hr = DiAllocate(sizeof(*MaskRows) * Rows, reinterpret_cast<PVOID*>(&MaskRows));
        if (SUCCEEDED(hr))
            hr = DiAllocate(Rows * ButtonBytes, reinterpret_cast<PVOID*>(&MaskData));
        if (FAILED(hr))
            return hr;

        MaskRowCount = Rows;
        FillMemory(MaskData, Rows * ButtonBytes, 0xFF);
        for (ULONG Row = 0; Row < Rows; Row++)
        {
            ULONG Seen = 0;

            for (ULONG Index = 0; Index < ObjectCount; Index++)
            {
                if (ButtonReports[Index] == Row + 1)
                {
                    MaskRows[Row] = MaskData + Next;
                    if (Next + Seen < Rows * ButtonBytes)
                        MaskData[Next + Seen] = 0;
                }
                if (ButtonReports[Index] != 0)
                    Seen++;
            }
            if (MaskRows[Row] == MaskData + Next)
                Next += ButtonBytes;
        }
        return S_OK;
    }

    HRESULT
    AddCollections(VOID)
    {
        HIDP_LINK_COLLECTION_NODE* Nodes = nullptr;
        ULONG Count = Caps.NumberLinkCollectionNodes;
        HRESULT hr;

        if (Count == 0)
            return S_OK;

        hr = DiAllocate(sizeof(*Nodes) * Count, reinterpret_cast<PVOID*>(&Nodes));
        if (FAILED(hr))
            return hr;

        if (HidP_GetLinkCollectionNodes(Nodes, &Count, Data) < 0 || Count != Caps.NumberLinkCollectionNodes)
        {
            DiFree(reinterpret_cast<PVOID*>(&Nodes));
            return E_FAIL;
        }

        for (ULONG Node = 0; Node < Count; Node++)
        {
            RX_DI_HID_GROUP* Group = &Groups[GroupCount++];
            ULONG Index = Base[DI_HID_REPORT_COLLECTION] + Node;
            ULONG Type = DIDFT_COLLECTION | DIDFT_NODATA | DIDFT_MAKEINSTANCE(static_cast<USHORT>(Index));

            Group->UsagePage = Nodes[Node].LinkUsagePage;
            Group->UsageMin = Nodes[Node].LinkUsage;
            Group->Count = 1;
            Group->Alias = Nodes[Node].IsAlias;
            Group->DataIndexMin = static_cast<USHORT>(Node);
            Group->LinkCollection = Nodes[Node].Parent;
            Group->Report = DI_HID_REPORT_COLLECTION;

            if (Group->UsagePage >= 0xFF00)
                Type |= DIDFT_VENDORDEFINED;
            Slots[Index].Group = Group;
            Objects[Index].pguid = &GUID_Unknown;
            Objects[Index].dwType = Type;
            Objects[Index].dwFlags = 0;
        }

        DiFree(reinterpret_cast<PVOID*>(&Nodes));
        return S_OK;
    }

    HRESULT
    BuildObjects(VOID)
    {
        ULONG GroupTotal = Caps.NumberLinkCollectionNodes + Caps.NumberInputValueCaps + Caps.NumberInputButtonCaps +
                           Caps.NumberOutputValueCaps + Caps.NumberOutputButtonCaps + Caps.NumberFeatureValueCaps +
                           Caps.NumberFeatureButtonCaps;
        PBYTE ButtonReports = nullptr;
        HRESULT hr;

        ObjectCount = Caps.NumberInputDataIndices + Caps.NumberOutputDataIndices + Caps.NumberFeatureDataIndices +
                      Caps.NumberLinkCollectionNodes;
        if (ObjectCount >= 0xFFFF)
            return E_FAIL;

        /* Objects sit at their data index, inputs first, then features, outputs and collections */
        Base[HidP_Input] = 0;
        Base[HidP_Feature] = Caps.NumberInputDataIndices;
        Base[HidP_Output] = Caps.NumberInputDataIndices + Caps.NumberFeatureDataIndices;
        Base[DI_HID_REPORT_COLLECTION] = Base[HidP_Output] + Caps.NumberOutputDataIndices;

        /* Devices that report effects in output reports get the PID driver */
        if (Caps.NumberOutputValueCaps != 0 && Caps.NumberOutputButtonCaps != 0)
        {
            USHORT Length = 0;

            PidDevice = HidP_GetSpecificButtonCaps(HidP_Output, DI_HID_PAGE_PID, 0, 0, nullptr, &Length, Data) ==
                        HIDP_STATUS_BUFFER_TOO_SMALL && Length > 2;
        }

        hr = DiAllocate(sizeof(*Objects) * (ObjectCount + 1), reinterpret_cast<PVOID*>(&Objects));
        if (SUCCEEDED(hr))
            hr = DiAllocate(sizeof(*Slots) * (ObjectCount + 1), reinterpret_cast<PVOID*>(&Slots));
        if (SUCCEEDED(hr))
            hr = DiAllocate(sizeof(*Groups) * (GroupTotal + 1), reinterpret_cast<PVOID*>(&Groups));
        if (SUCCEEDED(hr))
            hr = DiAllocate(sizeof(*Cals) * (Caps.NumberInputDataIndices + 1), reinterpret_cast<PVOID*>(&Cals));
        if (SUCCEEDED(hr))
            hr = DiAllocate(ObjectCount + 1, reinterpret_cast<PVOID*>(&ButtonReports));
        if (FAILED(hr))
            return hr;

        hr = AddValueGroups(HidP_Input, Caps.NumberInputValueCaps);
        if (SUCCEEDED(hr))
            hr = AddValueGroups(HidP_Feature, Caps.NumberFeatureValueCaps);
        if (SUCCEEDED(hr))
            hr = AddValueGroups(HidP_Output, Caps.NumberOutputValueCaps);

        ButtonStart = DataSize;
        if (SUCCEEDED(hr))
            hr = AddButtonGroups(HidP_Input, Caps.NumberInputButtonCaps, ButtonReports);
        if (SUCCEEDED(hr))
            hr = AddButtonGroups(HidP_Feature, Caps.NumberFeatureButtonCaps, ButtonReports);
        if (SUCCEEDED(hr))
            hr = AddButtonGroups(HidP_Output, Caps.NumberOutputButtonCaps, ButtonReports);
        ButtonBytes = DataSize - ButtonStart;
        if (SUCCEEDED(hr))
            hr = BuildButtonMasks(ButtonReports);
        DiFree(reinterpret_cast<PVOID*>(&ButtonReports));

        if (SUCCEEDED(hr))
            hr = AddCollections();
        if (FAILED(hr))
            return hr;

        DataSize = (DataSize + 3) & ~3UL;
        for (ULONG Kind = 0; Kind < _countof(Enabled); Kind++)
        {
            ReportIds[Kind] += 1;
            hr = DiAllocate(ReportIds[Kind], reinterpret_cast<PVOID*>(&Enabled[Kind]));
            if (FAILED(hr))
                return hr;
        }

        if (LOBYTE(DevType) == DI_HID_TYPE_KEYBOARD)
            MungeKeyboard();
        else
            MungeOther();
        ApplyTypeAttributes();

        Format.dwSize = sizeof(Format);
        Format.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
        Format.dwDataSize = DataSize;
        Format.dwNumObjs = ObjectCount;
        Format.rgodf = Objects;

        DataListLength = HidP_MaxDataListLength(HidP_Input, Data);
        hr = DiAllocate(sizeof(*DataList) * (DataListLength + 1), reinterpret_cast<PVOID*>(&DataList));
        if (SUCCEEDED(hr))
            hr = DiAllocate(Format.dwDataSize + sizeof(DWORD), reinterpret_cast<PVOID*>(&State));
        if (SUCCEEDED(hr))
            hr = DiAllocate(Caps.InputReportByteLength + 1, reinterpret_cast<PVOID*>(&Report));
        if (FAILED(hr))
            return hr;

        ResetState();
        LoadCalibrations();
        return S_OK;
    }

    /* Keys take their DIK_ code as instance, everything else is numbered from 256 */
    VOID
    MungeKeyboard(VOID)
    {
        const BYTE* Table = static_cast<const BYTE*>(DiFindTable(DI_HID_KEY_TABLE));
        BOOLEAN Taken[256] = {};
        ULONG Next = 256;

        if (Table == nullptr)
            return;

        for (ULONG Index = 0; Index < ObjectCount; Index++)
        {
            const RX_DI_HID_GROUP* Group = Slots[Index].Group;
            ULONG Instance = MAXULONG;

            if (Group != nullptr && Group->UsagePage == DI_HID_PAGE_KEYBOARD)
            {
                ULONG Usage = Group->UsageMin - Base[Group->Report] - Group->DataIndexMin + Index;

                if (Usage < 256 && Table[Usage] != 0 && !Taken[Table[Usage]])
                {
                    Taken[Table[Usage]] = TRUE;
                    Instance = Table[Usage];
                }
            }
            if (Instance == MAXULONG)
                Instance = Next++;

            Objects[Index].dwType = (Objects[Index].dwType & 0xFF0000FF) | DIDFT_MAKEINSTANCE(static_cast<USHORT>(Instance));
        }
    }

    ULONG
    ObjectUsage(
        _In_ ULONG Index,
        _In_ const RX_DI_HID_GROUP* Group,
        _In_ ULONG Kind)
    {
        return DiHidUsageKey(Group->UsagePage, Group->UsageMin - Group->DataIndexMin - Base[Kind] + Index);
    }

    /*
     * Axes take their slot from the type key's Axes branch first, then from the usage table,
     * then from 6 up. Buttons, POVs and collections are numbered in object order.
     */
    VOID
    MungeOther(VOID)
    {
        ULONG Axes = 0;
        ULONG Povs = 0;
        ULONG Buttons = 0;
        ULONG Collections = 0;
        ULONG Total;
        ULONG ScratchCount;
        PULONG Scratch = nullptr;
        PULONG Map = nullptr;
        PULONG Wanted;
        ULONG PovsAt;
        ULONG AxesAt;
        ULONG CollectionsAt;
        BOOLEAN Matched = TRUE;
        ULONG Slot = 0;
        ULONG Missing = 0;
        ULONG Hits = 0;

        for (ULONG Index = 0; Index < ObjectCount; Index++)
        {
            const RX_DI_HID_GROUP* Group = Slots[Index].Group;
            ULONG Type = Objects[Index].dwType;

            if (Group == nullptr || Group->Alias)
                continue;
            if (Type & DIDFT_COLLECTION)
                Collections++;
            else if (Type & DIDFT_NODATA)
                continue;
            else if (Type & DIDFT_BUTTON)
                Buttons++;
            else if (Type & DIDFT_AXIS)
                Axes++;
            else if (Type & DIDFT_POV)
                Povs++;
        }

        /* Claims per object, then the usages the type key asks for, then one list per kind */
        Total = Axes + Povs + Buttons + Collections + 6;
        ScratchCount = Total + Axes + 6;
        if (FAILED(DiAllocate(sizeof(ULONG) * ScratchCount, reinterpret_cast<PVOID*>(&Scratch))) ||
            FAILED(DiAllocate(sizeof(ULONG) * Total, reinterpret_cast<PVOID*>(&Map))))
        {
            DiFree(reinterpret_cast<PVOID*>(&Scratch));
            return;
        }
        FillMemory(Scratch, sizeof(ULONG) * Total, 0xFF);
        FillMemory(Map, sizeof(ULONG) * Total, 0xFF);
        Wanted = Scratch + Total;
        PovsAt = Buttons;
        AxesAt = Buttons + Povs;
        CollectionsAt = Buttons + Axes + 6;

        do
        {
            HKEY Key;

            if (SUCCEEDED(DiHidOpenObjectKey(TypeKey, DIDFT_MAKEINSTANCE(Slot) | DIDFT_AXIS, KEY_QUERY_VALUE, &Key)))
            {
                struct
                {
                    ULONG Flags;
                    USHORT Page;
                    USHORT Usage;
                } Attributes = {};

                if (FAILED(DiReadValue(Key, L"Attributes", &Attributes, sizeof(Attributes))) ||
                    (Attributes.Page == 0 && Attributes.Usage == 0))
                {
                    Missing++;
                }
                else
                {
                    BOOLEAN Present = FALSE;

                    Wanted[Slot] = DiHidUsageKey(Attributes.Page, Attributes.Usage);
                    for (ULONG Index = 0; Index < ObjectCount && !Present; Index++)
                    {
                        const RX_DI_HID_GROUP* Group = Slots[Index].Group;

                        Present = (Objects[Index].dwType & DIDFT_AXIS) && Group != nullptr && Group->Report == HidP_Input &&
                                  ObjectUsage(Index, Group, HidP_Input) == Wanted[Slot];
                    }

                    /* A type key that names an axis the device lacks is ignored entirely */
                    if (!Present)
                    {
                        RegCloseKey(Key);
                        FillMemory(Scratch, sizeof(ULONG) * Total, 0xFF);
                        Matched = FALSE;
                        break;
                    }
                    Hits++;
                }

                RegCloseKey(Key);
                if (Missing + Hits == Axes)
                    break;
            }
            Slot++;
        }
        while (Slot < Axes + 5);

        for (ULONG Index = 0; Matched && Index < ObjectCount; Index++)
        {
            const RX_DI_HID_GROUP* Group = Slots[Index].Group;
            ULONG Usage;
            ULONG Claim;
            ULONG Target;
            ULONG Wish;
            const GUID* Guid;

            if ((Objects[Index].dwType & DIDFT_AXIS) == 0 || Group == nullptr || Group->Report != HidP_Input)
                continue;

            Claim = Group->Alias ? 0 : Index;
            Usage = ObjectUsage(Index, Group, HidP_Input);
            for (Wish = 0; Wish <= Slot; Wish++)
            {
                if (Wanted[Wish] == Usage)
                    break;
            }
            if (Wish > Slot)
                continue;

            if (Claim < ScratchCount)
                Scratch[Claim] = Wish;
            Wanted[Wish] = 0;

            /* The GUID lands on the object as far past this one as this one is into its group */
            if (Wish < 6)
                Guid = DiHidFindUsageSlot(DiHidUsageKey(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_X + Wish))->Guid;
            else
                Guid = (Wish < 8) ? &GUID_Slider : &GUID_Unknown;
            Target = 2 * Index - Group->DataIndexMin - Base[HidP_Input];
            if (Target < ObjectCount)
                Objects[Target].pguid = Guid;

            if (AxesAt + Wish < Total)
                Map[AxesAt + Wish] = Claim;
            Objects[Claim].dwType = (Objects[Claim].dwType & 0xFF0000FF) | DIDFT_MAKEINSTANCE(static_cast<USHORT>(Wish));
        }

        {
            ULONG NextCollection = 0;
            ULONG NextButton = 0;
            ULONG NextPov = 0;
            ULONG NextAxis = 6;

            for (ULONG Index = 0; Index < ObjectCount; Index++)
            {
                const RX_DI_HID_GROUP* Group = Slots[Index].Group;
                ULONG Original = Index;
                ULONG Type;
                ULONG Kept;

                if (Group == nullptr || Group->Alias)
                    continue;

                Type = Objects[Index].dwType;
                if ((Type & (DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV | DIDFT_COLLECTION)) == 0)
                    continue;

                if (Type & DIDFT_COLLECTION)
                {
                    Objects[Index].dwType = (Type & 0xFF0000FF) | DIDFT_MAKEINSTANCE(static_cast<USHORT>(NextCollection));
                    if (CollectionsAt + NextCollection < Total)
                        Map[CollectionsAt + NextCollection] = Index;
                    NextCollection++;
                    continue;
                }

                /* Objects beyond the claim list read whatever follows it, which is never unclaimed */
                if ((Type & DIDFT_NODATA) || Index >= ScratchCount || Scratch[Index] != MAXULONG)
                    continue;

                Kept = Type & 0xFF0000FF;
                Objects[Index].dwType = Kept;

                if (Type & DIDFT_BUTTON)
                {
                    Objects[Index].dwType = Kept | DIDFT_MAKEINSTANCE(static_cast<USHORT>(NextButton));
                    Map[NextButton] = Index;
                    NextButton++;
                }
                else if ((Type & DIDFT_AXIS) == 0)
                {
                    Objects[Index].dwType = Kept | DIDFT_MAKEINSTANCE(static_cast<USHORT>(NextPov));
                    Map[PovsAt + NextPov] = Index;
                    NextPov++;
                }
                else
                {
                    const RX_DI_USAGE_SLOT* Known = DiHidFindUsageSlot(ObjectUsage(Index, Group, Group->Report));
                    BOOLEAN Placed = FALSE;

                    /* An axis without a slot of its own may borrow the slot of the aliases after it */
                    while (Known == nullptr && Index + 1 != ObjectCount)
                    {
                        const RX_DI_HID_GROUP* Next = Slots[Index + 1].Group;

                        if (Next == nullptr || !Next->Alias)
                            break;
                        Index += 1;
                        Known = DiHidFindUsageSlot(ObjectUsage(Index, Next, Next->Report));
                    }

                    if (Known != nullptr)
                    {
                        ULONG Wish = (Known->Slot == 6) ? 2 : Known->Slot;

                        if (Known->Slot <= 6 && Map[AxesAt + Wish] == MAXULONG)
                        {
                            Map[AxesAt + Wish] = Original;
                            Objects[Original].dwType = Kept | DIDFT_MAKEINSTANCE(static_cast<USHORT>(Wish));
                            Placed = TRUE;
                        }
                    }

                    if (!Placed)
                    {
                        Objects[Original].dwType = Kept | DIDFT_MAKEINSTANCE(static_cast<USHORT>(NextAxis));
                        Map[AxesAt + NextAxis] = Original;
                        NextAxis++;
                    }
                }
            }
        }

        ApplyTypeKind();

        /* Aliases keep only their alias and vendor bits */
        for (ULONG Index = 0; Index < ObjectCount; Index++)
        {
            if (Slots[Index].Group != nullptr && Slots[Index].Group->Alias)
                Objects[Index].dwType &= DIDFT_ALIAS | DIDFT_VENDORDEFINED;
        }

        DiFree(reinterpret_cast<PVOID*>(&Scratch));
        DiFree(reinterpret_cast<PVOID*>(&Map));
    }

    /* The port a device hangs off is named by the grandparent device */
    BOOLEAN
    ReadPortName(
        _Out_writes_(MAX_PATH) PWSTR Name,
        _Out_ PBOOLEAN Friendly)
    {
        SP_DEVINFO_DATA Device = { sizeof(Device) };
        DEVINST Parent = 0;
        BOOLEAN Got = FALSE;
        HDEVINFO Set;

        *Friendly = FALSE;
        Set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
        if (Set == INVALID_HANDLE_VALUE)
            return FALSE;

        if (SetupDiOpenDeviceInfoW(Set, Start.InstanceId, nullptr, 0, &Device) &&
            CM_Get_Parent(&Parent, Device.DevInst, 0) == CR_SUCCESS &&
            CM_Get_Parent(&Parent, Parent, 0) == CR_SUCCESS)
        {
            ULONG Size = MAX_PATH * sizeof(WCHAR);

            if (CM_Get_DevNode_Registry_PropertyW(Parent, CM_DRP_FRIENDLYNAME, nullptr, Name, &Size, 0) == CR_SUCCESS)
            {
                *Friendly = TRUE;
                Got = TRUE;
            }
            else
            {
                Size = MAX_PATH * sizeof(WCHAR);
                Got = CM_Get_DevNode_Registry_PropertyW(Parent, CM_DRP_DEVICEDESC, nullptr, Name, &Size, 0) == CR_SUCCESS;
            }
        }

        SetupDiDestroyDeviceInfoList(Set);
        return Got;
    }

    /* Input axes rest at their calibrated center and POVs point nowhere */
    VOID
    ResetState(VOID)
    {
        for (ULONG Index = 0; Index < ObjectCount; Index++)
        {
            const RX_DI_HID_GROUP* Group = Slots[Index].Group;
            LONG* Slot = reinterpret_cast<LONG*>(State + Objects[Index].dwOfs);

            if (Group == nullptr || Group->Report != HidP_Input)
                continue;

            if (Objects[Index].dwType & DIDFT_ABSAXIS)
                *Slot = (Slots[Index].Cal->Raw != 0) ? (Group->LogicalMin + Group->LogicalMax) / 2 : Slots[Index].Cal->RangeCenter;
            else if (Objects[Index].dwType & DIDFT_POV)
                *Slot = -1;
        }
    }

    /* Applies one input report to a copy of the state and posts it */
    VOID
    ParseReport(
        _In_ ULONG Length)
    {
        ULONG Count = DataListLength;
        UCHAR Id = Report[0];
        PBYTE Next = nullptr;

        if (DataListLength == 0 || Length == 0)
            return;
        if (Id >= ReportIds[HidP_Input] || Enabled[HidP_Input][Id] == 0)
            return;
        if (HidP_GetData(HidP_Input, DataList, &Count, Data, reinterpret_cast<PCHAR>(Report), Length) < 0)
            return;
        if (FAILED(DiAllocate(Format.dwDataSize + sizeof(DWORD), reinterpret_cast<PVOID*>(&Next))))
            return;
        CopyMemory(Next, State, Format.dwDataSize);

        /* Buttons not in the data list are up, but only those this report carries */
        if (MaskRows == nullptr)
        {
            ZeroMemory(Next + ButtonStart, ButtonBytes);
        }
        else if (Id != 0 && Id <= MaskRowCount && MaskRows[Id - 1] != nullptr)
        {
            for (ULONG Byte = 0; Byte < ButtonBytes; Byte++)
                Next[ButtonStart + Byte] &= MaskRows[Id - 1][Byte];
        }

        for (ULONG Item = 0; Item < Count; Item++)
        {
            ULONG Index = DataList[Item].DataIndex + Base[HidP_Input];
            const RX_DI_HID_GROUP* Group;
            ULONG Raw = DataList[Item].RawValue;
            ULONG Type;
            LONG Reading;
            PBYTE Slot;

            if (Index >= ObjectCount)
                continue;
            Group = Slots[Index].Group;
            if (Group == nullptr || Group->Report != HidP_Input)
                continue;

            /* Sign extend negative readings, mask positive ones to the field */
            Reading = static_cast<LONG>(Raw & static_cast<ULONG>(Group->SignMask));
            if (Reading == 0)
                Reading = static_cast<LONG>(Raw);
            else if (Group->Signed)
                Reading = static_cast<LONG>(Raw | static_cast<ULONG>(Group->SignMask));

            Slot = Next + Objects[Index].dwOfs;
            if (!Group->Value)
            {
                *Slot = 0x80;
                continue;
            }

            Type = Objects[Index].dwType;
            if (Type & DIDFT_RELAXIS)
            {
                if (Group->Granularity != 0)
                    Reading = -static_cast<LONG>(Group->Granularity * Reading);
                *reinterpret_cast<LONG*>(Slot) += Reading;
            }
            else if ((Type & DIDFT_ABSAXIS) || ((Type & DIDFT_POV) && Group->PovPoints))
            {
                *reinterpret_cast<LONG*>(Slot) = Reading;
                if (Slots[Index].Cal != nullptr && (DiSinkFlags(Sink) & DI_SINK_RELATIVE) == 0)
                    DiHidCook(Slots[Index].Cal, reinterpret_cast<LONG*>(Slot));
            }
            else if (Type & DIDFT_BUTTON)
            {
                LONG Half = (Group->LogicalMax - Group->LogicalMin + 1) / 2;

                *Slot = (Reading > Group->LogicalMax || Reading - Group->LogicalMin < Half) ? 0 : 0x80;
            }
            else if (Type & DIDFT_POV)
            {
                if (Reading < Group->LogicalMin || Reading > Group->LogicalMax)
                    *reinterpret_cast<LONG*>(Slot) = -1;
                else
                    *reinterpret_cast<LONG*>(Slot) = (Reading - Group->LogicalMin) * Group->Granularity;
            }
        }

        DiPostState(&Source, Next, GetTickCount());
        DiFree(reinterpret_cast<PVOID*>(&Next));
    }

    static
    DWORD
    WINAPI
    ReaderProc(
        _In_ PVOID Parameter)
    {
        RxDiHid* Self = static_cast<RxDiHid*>(Parameter);
        OVERLAPPED Overlapped = {};
        HANDLE Waits[2];

        Overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Waits[0] = Self->StopEvent;
        Waits[1] = Overlapped.hEvent;

        while (Overlapped.hEvent != nullptr)
        {
            DWORD Read = 0;

            ResetEvent(Overlapped.hEvent);
            if (!ReadFile(Self->Reader, Self->Report, Self->Caps.InputReportByteLength, nullptr, &Overlapped) &&
                GetLastError() != ERROR_IO_PENDING)
            {
                DiTrace(L"HID read failed, error %lu", GetLastError());
                break;
            }

            if (WaitForMultipleObjects(2, Waits, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
            {
                CancelIo(Self->Reader);
                GetOverlappedResult(Self->Reader, &Overlapped, &Read, TRUE);
                break;
            }

            if (!GetOverlappedResult(Self->Reader, &Overlapped, &Read, FALSE))
            {
                DiTrace(L"HID device %ls lost, error %lu", Self->Start.InstanceId, GetLastError());
                break;
            }
            if (Read != Self->Caps.InputReportByteLength)
            {
                DiTrace(L"HID device %ls sent a short report", Self->Start.InstanceId);
                break;
            }
            Self->ParseReport(Read);
        }

        if (Overlapped.hEvent != nullptr)
            CloseHandle(Overlapped.hEvent);
        return 0;
    }

    HRESULT
    StartReader(VOID)
    {
        Reader = OpenDevice(FILE_FLAG_OVERLAPPED);
        if (Reader == INVALID_HANDLE_VALUE)
        {
            Reader = nullptr;
            DiTrace(L"HID device %ls cannot be opened, error %lu", Start.InstanceId, GetLastError());
            return DIERR_UNPLUGGED;
        }

        StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ReaderThread = (StopEvent != nullptr) ? CreateThread(nullptr, 0, ReaderProc, this, 0, nullptr) : nullptr;
        if (ReaderThread == nullptr)
        {
            StopReader();
            return E_FAIL;
        }
        return S_OK;
    }

    VOID
    StopReader(VOID)
    {
        if (ReaderThread != nullptr)
        {
            SetEvent(StopEvent);
            WaitForSingleObject(ReaderThread, INFINITE);
            CloseHandle(ReaderThread);
            ReaderThread = nullptr;
        }
        if (StopEvent != nullptr)
        {
            CloseHandle(StopEvent);
            StopEvent = nullptr;
        }
        if (Reader != nullptr)
        {
            CloseHandle(Reader);
            Reader = nullptr;
        }
    }

    static
    HRESULT
    AcquireSource(
        _Inout_ PRX_DI_SINK Sink,
        _In_ BOOL Acquire)
    {
        RxDiHid* Self = reinterpret_cast<RxDiHid*>(DiSinkContext(Sink));

        if (!Acquire)
        {
            Self->StopReader();
            return S_OK;
        }
        return Self->StartReader();
    }

    RX_DI_HID_START Start = {};
    RX_DI_SOURCE Source = {};
    PRX_DI_SINK Sink = nullptr;
    PHIDP_PREPARSED_DATA Data = nullptr;
    HIDP_CAPS Caps = {};
    ULONG DevType = 0;
    DWORD Version = 0;
    HKEY TypeKey = nullptr;
    BOOLEAN PidDevice = FALSE;
    USHORT VendorId = 0;
    USHORT ProductId = 0;
    BOOL Polled = FALSE;
    ULONG AxisCount = 0;
    ULONG ButtonCount = 0;
    ULONG PovCount = 0;
    DIDATAFORMAT Format = {};
    DIOBJECTDATAFORMAT* Objects = nullptr;
    ULONG ObjectCount = 0;
    RX_DI_HID_SLOT* Slots = nullptr;
    RX_DI_HID_GROUP* Groups = nullptr;
    ULONG GroupCount = 0;
    RX_DI_HID_CAL* Cals = nullptr;
    ULONG CalCount = 0;
    ULONG Base[DI_HID_REPORT_KINDS] = {};
    ULONG ReportIds[3] = {};
    PBYTE Enabled[3] = {};
    ULONG DataSize = 0;
    ULONG ButtonStart = 0;
    ULONG ButtonBytes = 0;
    PBYTE* MaskRows = nullptr;
    PBYTE MaskData = nullptr;
    ULONG MaskRowCount = 0;
    HIDP_DATA* DataList = nullptr;
    ULONG DataListLength = 0;
    PBYTE State = nullptr;
    PBYTE Report = nullptr;
    HANDLE Reader = nullptr;
    HANDLE ReaderThread = nullptr;
    HANDLE StopEvent = nullptr;
};

static
HRESULT
DiCreateHid(
    _In_ REFGUID Instance,
    _Outptr_ RxDiBackend** Backend)
{
    RxDiHid* Created = new RxDiHid();
    HRESULT hr;

    *Backend = nullptr;
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Init(Instance);
    if (FAILED(hr))
    {
        delete Created;
        return hr;
    }

    *Backend = Created;
    return S_OK;
}

HRESULT
DiFindHidBackend(
    _In_ REFGUID Instance,
    _Out_ PRX_DI_BACKEND_CREATE* Create)
{
    BOOLEAN Found;

    *Create = nullptr;
    for (ULONG Pass = 0; Pass < 2; Pass++)
    {
        if (Pass != 0)
            DiRefreshHidList(TRUE);

        EnterCriticalSection(&DiLock);
        Found = DiHidFindByGuid(Instance) != nullptr;
        LeaveCriticalSection(&DiLock);
        if (Found)
        {
            *Create = DiCreateHid;
            return S_OK;
        }
    }

    return DIERR_DEVICENOTREG;
}
