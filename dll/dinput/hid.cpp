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

#define DI_DEVTYPE_HID_DEVICE   (DIDEVTYPE_HID | DIDEVTYPE_DEVICE)
#define DI_DEVTYPE_HID_MOUSE    (DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_MOUSE, DIDEVTYPEMOUSE_UNKNOWN))
#define DI_DEVTYPE_HID_JOYSTICK (DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_JOYSTICK, DIDEVTYPEJOYSTICK_UNKNOWN))
#define DI_DEVTYPE_HID_GAMEPAD  (DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_JOYSTICK, DIDEVTYPEJOYSTICK_GAMEPAD))
#define DI_DEVTYPE_HID_KEYBOARD (DIDEVTYPE_HID | DIDEVTYPE_KEYBOARD)
#define DI_DEVTYPE_HID_STICK    (DIDEVTYPE_HID | MAKEWORD(DIDEVTYPE_JOYSTICK, DIDEVTYPEJOYSTICK_TRADITIONAL))

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
        Found = TRUE;
    }
    LeaveCriticalSection(&DiLock);
    return Found;
}

/* One object of a HID device and where its data comes from */
typedef struct _RX_DI_HID_OBJECT
{
    USHORT UsagePage;
    USHORT Usage;
    USHORT Collection;
    BOOLEAN Value;
    BOOLEAN Input;
    USHORT Number;
    UCHAR ReportId;
    ULONG Units;
    USHORT Exponent;
    LONG LogicalMin;
    LONG LogicalMax;
    LONG RangeMin;
    LONG RangeMax;
    ULONG DeadZone;
    ULONG Saturation;
} RX_DI_HID_OBJECT;

/* First string of each usage page's names, zero for pages without names */
static const USHORT DiUsageNameBase[16] =
{
    0, 1536, 2048, 2560, 3072, 3584, 0, 6144, 4096, 0, 0, 4608, 5120, 5632, 0, 6656
};

static
const GUID*
DiHidUsageGuid(
    _In_ USHORT Page,
    _In_ USHORT Usage)
{
    if (Page == HID_USAGE_PAGE_GENERIC)
    {
        switch (Usage)
        {
            case HID_USAGE_GENERIC_X: return &GUID_XAxis;
            case HID_USAGE_GENERIC_Y: return &GUID_YAxis;
            case HID_USAGE_GENERIC_Z: return &GUID_ZAxis;
            case HID_USAGE_GENERIC_RX: return &GUID_RxAxis;
            case HID_USAGE_GENERIC_RY: return &GUID_RyAxis;
            case HID_USAGE_GENERIC_RZ: return &GUID_RzAxis;
            case HID_USAGE_GENERIC_SLIDER:
            case HID_USAGE_GENERIC_DIAL:
            case HID_USAGE_GENERIC_WHEEL:
                return &GUID_Slider;
            case HID_USAGE_GENERIC_HATSWITCH:
                return &GUID_POV;
        }
    }
    return &GUID_Unknown;
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

        /* Devices that report effects in output reports get the PID driver */
        if (Caps.NumberOutputValueCaps != 0 && Caps.NumberOutputButtonCaps != 0)
        {
            USHORT Length = 0;

            PidDevice = HidP_GetSpecificButtonCaps(HidP_Output, DI_HID_PAGE_PID, 0, 0, nullptr, &Length, Data) ==
                        HIDP_STATUS_BUFFER_TOO_SMALL && Length > 2;
        }

        hr = BuildObjects();
        if (FAILED(hr))
            return hr;
        ApplyTypeAttributes();
        ApplyTypeKind();

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
        DiCloseSink(Sink);
        if (Data != nullptr)
            HidD_FreePreparsedData(Data);
        DiFree(reinterpret_cast<PVOID*>(&Objects));
        DiFree(reinterpret_cast<PVOID*>(&Info));
        DiFree(reinterpret_cast<PVOID*>(&State));
        DiFree(reinterpret_cast<PVOID*>(&Report));
    }

    PRX_DI_SINK GetSink(VOID) override { return Sink; }

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
        const RX_DI_HID_OBJECT* Hid;
        WCHAR Pattern[64];
        UINT Id;

        if (Target->Index >= Format.dwNumObjs)
            return E_INVALIDARG;

        Hid = &Info[Target->Index];
        if (Object->dwSize >= sizeof(DIDEVICEOBJECTINSTANCEW))
        {
            Object->wCollectionNumber = Hid->Number;
            Object->wUsagePage = Hid->UsagePage;
            Object->wUsage = Hid->Usage;
            Object->dwDimension = Hid->Units;
            Object->wExponent = Hid->Exponent;
            Object->wReportId = Hid->ReportId;
        }

        if (Target->Type & DIDFT_COLLECTION)
            Id = 31;
        else if (Target->Type & DIDFT_BUTTON)
            Id = 28;
        else if (Target->Type & DIDFT_AXIS)
            Id = 29;
        else if (Target->Type & DIDFT_POV)
            Id = 30;
        else
            Id = 35;

        /* Known usage pages have a name for each usage */
        if (Hid->UsagePage < _countof(DiUsageNameBase) && DiUsageNameBase[Hid->UsagePage] != 0 && Hid->Usage < 0x200 &&
            LoadStringW(DiModule, DiUsageNameBase[Hid->UsagePage] + Hid->Usage, Object->tszName, MAX_PATH) != 0)
        {
            if (Target->Type & DIDFT_COLLECTION)
            {
                WCHAR Usage[MAX_PATH];

                StringCchCopyW(Usage, _countof(Usage), Object->tszName);
                LoadStringW(DiModule, 32, Pattern, _countof(Pattern));
                StringCchPrintfW(Object->tszName, _countof(Object->tszName), Pattern, DIDFT_GETINSTANCE(Target->Type), Usage);
            }
            return S_OK;
        }

        LoadStringW(DiModule, Id, Pattern, _countof(Pattern));
        StringCchPrintfW(Object->tszName, _countof(Object->tszName), Pattern, DIDFT_GETINSTANCE(Target->Type));
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
        if ((Sink->Flags & DI_SINK_ACQUIRED) == 0)
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
        const RX_DI_HID_OBJECT* Hid = (Target->Index < Format.dwNumObjs) ? &Info[Target->Index] : nullptr;

        switch (Target->Property)
        {
            case 4:
                if (Hid == nullptr || (Target->Type & DIDFT_ABSAXIS) == 0)
                    return E_NOTIMPL;
                reinterpret_cast<LPDIPROPRANGE>(Header)->lMin = Hid->RangeMin;
                reinterpret_cast<LPDIPROPRANGE>(Header)->lMax = Hid->RangeMax;
                return S_OK;

            case 5:
            case 6:
                if (Hid == nullptr || (Target->Type & DIDFT_ABSAXIS) == 0)
                    return E_NOTIMPL;
                Value->dwData = (Target->Property == 5) ? Hid->DeadZone : Hid->Saturation;
                return S_OK;

            case DI_PROP_ENABLE_OBJECT:
                if (Hid == nullptr)
                    return E_NOTIMPL;
                Value->dwData = 0;
                return S_OK;

            case 13:
            case 14:
                if (Hid != nullptr)
                    return E_NOTIMPL;
                ReadName(reinterpret_cast<LPDIPROPSTRING>(Header)->wsz);
                return S_OK;

            case 15:
                if ((DevType & (DIDEVTYPE_HID | DIDEVTYPE_JOYSTICK)) != (DIDEVTYPE_HID | DIDEVTYPE_JOYSTICK))
                    return E_NOTIMPL;
                Value->dwData = Start.JoystickId;
                return S_OK;

            case 16:
            {
                BOOLEAN Friendly;

                if (Hid != nullptr)
                    return E_NOTIMPL;
                if (!ReadPortName(reinterpret_cast<LPDIPROPSTRING>(Header)->wsz, &Friendly))
                    return E_FAIL;
                return Friendly ? S_OK : S_FALSE;
            }

            case 12:
            {
                LPDIPROPGUIDANDPATH Path = reinterpret_cast<LPDIPROPGUIDANDPATH>(Header);

                if (Hid != nullptr)
                    return E_NOTIMPL;
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
        HRESULT hr;

        /* Every object is read anyway, turning one on or off needs nothing */
        if (Target->Property == DI_PROP_ENABLE_OBJECT)
            return S_OK;

        if (Target->Property != 4 && Target->Property != 5 && Target->Property != 6)
            return E_NOTIMPL;

        if (Target->Index != MAXULONG)
        {
            if (Target->Index >= Format.dwNumObjs || (Target->Type & DIDFT_ABSAXIS) == 0)
                return E_NOTIMPL;
            return SetAxisProperty(&Info[Target->Index], Target->Property, Header);
        }

        /* The whole device means every absolute axis */
        for (ULONG Index = 0; Index < Format.dwNumObjs; Index++)
        {
            if ((Objects[Index].dwType & DIDFT_ABSAXIS) == 0)
                continue;
            hr = SetAxisProperty(&Info[Index], Target->Property, Header);
            if (FAILED(hr))
                return hr;
        }
        return S_OK;
    }

    HRESULT
    SetCooperativeLevel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override
    {
        if (Flags & DISCL_EXCLUSIVE)
            Sink->Flags |= DI_SINK_EXCLUSIVE;
        else
            Sink->Flags &= ~DI_SINK_EXCLUSIVE;
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
        if (Index >= Format.dwNumObjs || (Objects[Index].dwType & DIDFT_COLLECTION))
            return 0;
        return MAKELONG(Info[Index].Usage, Info[Index].UsagePage);
    }

    HRESULT
    MapUsage(
        _In_ ULONG Usage,
        _Out_ PULONG Index) override
    {
        for (ULONG Object = 0; Object < Format.dwNumObjs; Object++)
        {
            if (GetUsage(Object) == Usage)
            {
                *Index = Object;
                return S_OK;
            }
        }
        return DIERR_NOTFOUND;
    }

private:
    VOID
    OpenTypeKey(VOID)
    {
        WCHAR Type[20];

        /* The gameport adapters stand for whatever stick is plugged into them */
        if (VendorId == DI_HID_MS_VENDOR && ProductId >= DI_HID_MS_GAMEPORT_LOW && ProductId <= DI_HID_MS_GAMEPORT_HIGH)
        {
            if (DevType == DI_DEVTYPE_HID_JOYSTICK)
                DevType = DI_DEVTYPE_HID_STICK;
            return;
        }

        StringCchPrintfW(Type, _countof(Type), L"VID_%04X&PID_%04X", VendorId, ProductId);
        DiOpenJoyTypeKey(Type, MAXIMUM_ALLOWED, &TypeKey);
    }

    /* The joystick type settings say what kind of stick a HID joystick is */
    VOID
    ApplyTypeKind(VOID)
    {
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

        LoadStringW(DiModule, (LOBYTE(DevType) == DIDEVTYPE_MOUSE) ? IDS_MOUSE :
                              (LOBYTE(DevType) == DIDEVTYPE_KEYBOARD) ? IDS_KEYBOARD : 0x2008, Name, MAX_PATH);
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

    /* Extra type bits for objects that live in feature and output reports */
    static
    ULONG
    ReportTypeBits(
        _In_ HIDP_REPORT_TYPE Kind)
    {
        if (Kind == HidP_Feature)
            return DIDFT_OUTPUT;
        if (Kind == HidP_Output)
            return DIDFT_OUTPUT | DIDFT_NODATA;
        return 0;
    }

    ULONG
    CountObjects(
        _In_ HIDP_REPORT_TYPE Kind)
    {
        USHORT ValueCount = (Kind == HidP_Input) ? Caps.NumberInputValueCaps :
                            (Kind == HidP_Feature) ? Caps.NumberFeatureValueCaps : Caps.NumberOutputValueCaps;
        USHORT ButtonCapsCount = (Kind == HidP_Input) ? Caps.NumberInputButtonCaps :
                                 (Kind == HidP_Feature) ? Caps.NumberFeatureButtonCaps : Caps.NumberOutputButtonCaps;
        HIDP_BUTTON_CAPS* Buttons = nullptr;
        ULONG Count = ValueCount;

        if (ButtonCapsCount != 0 && SUCCEEDED(DiAllocate(sizeof(*Buttons) * ButtonCapsCount, reinterpret_cast<PVOID*>(&Buttons))))
        {
            if (HidP_GetButtonCaps(Kind, Buttons, &ButtonCapsCount, Data) == HIDP_STATUS_SUCCESS)
            {
                for (USHORT Index = 0; Index < ButtonCapsCount; Index++)
                    Count += Buttons[Index].IsRange ? (Buttons[Index].Range.UsageMax - Buttons[Index].Range.UsageMin + 1) : 1;
            }
            DiFree(reinterpret_cast<PVOID*>(&Buttons));
        }
        return Count;
    }

    /* Values take a dword each, aliases share the slot of the value before them */
    HRESULT
    AddValues(
        _In_ HIDP_REPORT_TYPE Kind,
        _Inout_ PULONG Offset)
    {
        USHORT ValueCount = (Kind == HidP_Input) ? Caps.NumberInputValueCaps :
                            (Kind == HidP_Feature) ? Caps.NumberFeatureValueCaps : Caps.NumberOutputValueCaps;
        HIDP_VALUE_CAPS* Values = nullptr;
        HRESULT hr;

        if (ValueCount == 0)
            return S_OK;

        hr = DiAllocate(sizeof(*Values) * ValueCount, reinterpret_cast<PVOID*>(&Values));
        if (FAILED(hr))
            return hr;

        if (HidP_GetValueCaps(Kind, Values, &ValueCount, Data) != HIDP_STATUS_SUCCESS)
            ValueCount = 0;

        for (USHORT Index = 0; Index < ValueCount; Index++)
        {
            const HIDP_VALUE_CAPS* Value = &Values[Index];
            USHORT Usage = Value->IsRange ? Value->Range.UsageMin : Value->NotRange.Usage;
            BOOLEAN Alias = Value->IsAlias != FALSE;
            ULONG Type = ClassifyValue(Value, Usage);
            const GUID* Guid = (Type & (DIDFT_AXIS | DIDFT_POV)) ? DiHidUsageGuid(Value->UsagePage, Usage) : &GUID_Unknown;
            ULONG Slot = (Alias && *Offset >= sizeof(DWORD)) ? *Offset - sizeof(DWORD) : *Offset;
            ULONG Flags;

            /* Only input POVs keep their kind outside the input report, the rest just carry data */
            if (Kind != HidP_Input && (Type & (DIDFT_VENDORDEFINED | DIDFT_POV)) == 0)
                Type = 0;

            if (Alias)
                Type |= DIDFT_ALIAS;
            else if (Type & DIDFT_POV)
                PovCount++;
            else if (Kind == HidP_Input && (Type & DIDFT_AXIS))
                AxisCount++;
            else if (Kind == HidP_Input && (Type & DIDFT_BUTTON))
                ButtonCount++;

            if (Kind != HidP_Input)
                Flags = 0x80000000 | DIDOI_POLLED;
            else
                Flags = ((Type & DIDFT_AXIS) ? DIDOI_ASPECTPOSITION : 0) | (Polled ? DIDOI_POLLED : 0);

            /* A PID device can push on any usage it names in an output report */
            if (PidDevice && Guid != &GUID_Unknown)
            {
                USHORT Length = 0;
                NTSTATUS Status = HidP_GetSpecificButtonCaps(HidP_Output, Value->UsagePage, 0, Usage, nullptr, &Length, Data);

                if (Status >= 0 || Status == HIDP_STATUS_BUFFER_TOO_SMALL)
                {
                    Type |= DIDFT_FFACTUATOR;
                    Flags |= DIDOI_FFACTUATOR;
                }
            }

            AddObject(Guid, Slot, Type | ReportTypeBits(Kind) | DIDFT_MAKEINSTANCE(ValueInstance++), Flags,
                      Value->UsagePage, Usage, Value->LinkCollection, TRUE, Value->LogicalMin, Value->LogicalMax);
            Info[Format.dwNumObjs - 1].ReportId = Value->ReportID;
            Info[Format.dwNumObjs - 1].Units = Value->Units;
            Info[Format.dwNumObjs - 1].Exponent = static_cast<USHORT>(Value->UnitsExp);
            Info[Format.dwNumObjs - 1].Input = (Kind == HidP_Input);
            if (!Alias)
                *Offset += sizeof(DWORD);
        }

        DiFree(reinterpret_cast<PVOID*>(&Values));
        return S_OK;
    }

    /* Buttons take a byte each, only pages that hold buttons make buttons and keyboard pages make keys */
    HRESULT
    AddButtons(
        _In_ HIDP_REPORT_TYPE Kind,
        _Inout_ PULONG Offset)
    {
        USHORT ButtonCapsCount = (Kind == HidP_Input) ? Caps.NumberInputButtonCaps :
                                 (Kind == HidP_Feature) ? Caps.NumberFeatureButtonCaps : Caps.NumberOutputButtonCaps;
        HIDP_BUTTON_CAPS* Buttons = nullptr;
        ULONG First = *Offset;
        HRESULT hr;

        if (ButtonCapsCount == 0)
            return S_OK;

        hr = DiAllocate(sizeof(*Buttons) * ButtonCapsCount, reinterpret_cast<PVOID*>(&Buttons));
        if (FAILED(hr))
            return hr;

        if (HidP_GetButtonCaps(Kind, Buttons, &ButtonCapsCount, Data) != HIDP_STATUS_SUCCESS)
            ButtonCapsCount = 0;

        for (USHORT Index = 0; Index < ButtonCapsCount; Index++)
        {
            const HIDP_BUTTON_CAPS* Button = &Buttons[Index];
            USHORT Low = Button->IsRange ? Button->Range.UsageMin : Button->NotRange.Usage;
            USHORT High = Button->IsRange ? Button->Range.UsageMax : Low;

            for (ULONG Usage = Low; Usage <= High; Usage++)
            {
                const GUID* Guid = &GUID_Unknown;
                BOOLEAN Alias = Button->IsAlias != FALSE;
                ULONG Type = 0;

                if (Button->UsagePage >= 0xFF00)
                {
                    Type = DIDFT_VENDORDEFINED;
                }
                else if (Kind == HidP_Input)
                {
                    switch (Button->UsagePage)
                    {
                        case HID_USAGE_PAGE_GENERIC:
                        case HID_USAGE_PAGE_SIMULATION:
                        case HID_USAGE_PAGE_VR:
                        case HID_USAGE_PAGE_SPORT:
                        case HID_USAGE_PAGE_GAME:
                        case HID_USAGE_PAGE_BUTTON:
                        case HID_USAGE_PAGE_CONSUMER:
                        case HID_USAGE_PAGE_DIGITIZER:
                            Guid = &GUID_Button;
                            Type = DIDFT_PSHBUTTON;
                            break;
                        case HID_USAGE_PAGE_KEYBOARD:
                            Guid = &GUID_Key;
                            Type = DIDFT_PSHBUTTON;
                            break;
                    }
                }

                Type |= ReportTypeBits(Kind) | DIDFT_MAKEINSTANCE(ButtonInstance++);
                if (PidDevice && (Type & DIDFT_PSHBUTTON))
                    Type |= DIDFT_FFEFFECTTRIGGER;
                if (Alias)
                    Type |= DIDFT_ALIAS;
                else if (Type & DIDFT_BUTTON)
                    ButtonCount++;

                AddObject(Guid, (Alias && *Offset > First) ? *Offset - 1 : *Offset, Type,
                          ((Kind != HidP_Input) ? (0x80000000 | DIDOI_POLLED) : (Polled ? DIDOI_POLLED : 0)) |
                          ((Type & DIDFT_FFEFFECTTRIGGER) ? DIDOI_FFEFFECTTRIGGER : 0),
                          Button->UsagePage, static_cast<USHORT>(Usage), Button->LinkCollection, FALSE, 0, 1);
                Info[Format.dwNumObjs - 1].ReportId = Button->ReportID;
                Info[Format.dwNumObjs - 1].Input = (Kind == HidP_Input);
                if (!Alias)
                    (*Offset)++;
            }
        }

        DiFree(reinterpret_cast<PVOID*>(&Buttons));
        return S_OK;
    }

    HRESULT
    BuildObjects(VOID)
    {
        static const HIDP_REPORT_TYPE Reports[] = { HidP_Input, HidP_Feature, HidP_Output };
        ULONG Capacity = Caps.NumberLinkCollectionNodes;
        ULONG Offset = 0;
        HRESULT hr;

        for (ULONG Index = 0; Index < _countof(Reports); Index++)
            Capacity += CountObjects(Reports[Index]);

        hr = DiAllocate(sizeof(*Objects) * (Capacity + 1), reinterpret_cast<PVOID*>(&Objects));
        if (SUCCEEDED(hr))
            hr = DiAllocate(sizeof(*Info) * (Capacity + 1), reinterpret_cast<PVOID*>(&Info));

        /* Input, feature and output values, then the buttons in the same order */
        for (ULONG Index = 0; SUCCEEDED(hr) && Index < _countof(Reports); Index++)
            hr = AddValues(Reports[Index], &Offset);
        for (ULONG Index = 0; SUCCEEDED(hr) && Index < _countof(Reports); Index++)
            hr = AddButtons(Reports[Index], &Offset);
        if (FAILED(hr))
            return hr;

        /* Link collections carry no data */
        if (Caps.NumberLinkCollectionNodes != 0)
        {
            HIDP_LINK_COLLECTION_NODE* Nodes = nullptr;
            ULONG NodeCount = Caps.NumberLinkCollectionNodes;

            if (SUCCEEDED(DiAllocate(sizeof(*Nodes) * NodeCount, reinterpret_cast<PVOID*>(&Nodes))))
            {
                if (HidP_GetLinkCollectionNodes(Nodes, &NodeCount, Data) == HIDP_STATUS_SUCCESS)
                {
                    for (ULONG Index = 0; Index < NodeCount; Index++)
                    {
                        ULONG Type = DIDFT_COLLECTION | DIDFT_NODATA | DIDFT_MAKEINSTANCE(Index);

                        if (Nodes[Index].LinkUsagePage >= 0xFF00)
                            Type |= DIDFT_VENDORDEFINED;
                        AddObject(&GUID_Unknown, 0, Type, 0, Nodes[Index].LinkUsagePage, Nodes[Index].LinkUsage,
                                  static_cast<USHORT>(Index), FALSE, 0, 0);
                        Info[Format.dwNumObjs - 1].Number = Nodes[Index].Parent;
                    }
                }
                DiFree(reinterpret_cast<PVOID*>(&Nodes));
            }
        }

        Format.dwSize = sizeof(Format);
        Format.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
        Format.dwDataSize = (Offset + 3) & ~3UL;
        Format.rgodf = Objects;

        hr = DiAllocate(Format.dwDataSize + sizeof(DWORD), reinterpret_cast<PVOID*>(&State));
        if (SUCCEEDED(hr))
            hr = DiAllocate(Caps.InputReportByteLength + 1, reinterpret_cast<PVOID*>(&Report));
        if (SUCCEEDED(hr))
            ResetState();
        return hr;
    }

    VOID
    AddObject(
        _In_ const GUID* Guid,
        _In_ ULONG Offset,
        _In_ ULONG Type,
        _In_ ULONG Flags,
        _In_ USHORT Page,
        _In_ USHORT Usage,
        _In_ USHORT Collection,
        _In_ BOOLEAN Value,
        _In_ LONG Minimum,
        _In_ LONG Maximum)
    {
        DIOBJECTDATAFORMAT* Object = &Objects[Format.dwNumObjs];
        RX_DI_HID_OBJECT* Hid = &Info[Format.dwNumObjs];

        Object->pguid = Guid;
        Object->dwOfs = Offset;
        Object->dwType = Type;
        Object->dwFlags = Flags;
        Hid->UsagePage = Page;
        Hid->Usage = Usage;
        Hid->Collection = Collection;
        Hid->Number = Collection;
        Hid->Value = Value;
        Hid->LogicalMin = Minimum;
        Hid->LogicalMax = Maximum;
        Hid->RangeMin = 0;
        Hid->RangeMax = DI_HID_AXIS_MAX;
        Hid->Saturation = 10000;
        Format.dwNumObjs++;
    }

    /* What DirectInput calls a value, by usage page */
    static
    ULONG
    ClassifyValue(
        _In_ const HIDP_VALUE_CAPS* Value,
        _In_ USHORT Usage)
    {
        if (Value->UsagePage >= 0xFF00)
            return DIDFT_VENDORDEFINED;

        if (Value->UsagePage == HID_USAGE_PAGE_GENERIC && Usage == HID_USAGE_GENERIC_HATSWITCH)
            return DIDFT_POV;

        switch (Value->UsagePage)
        {
            case HID_USAGE_PAGE_GENERIC:
                if (Usage == 0x3A || Usage == 0x3B)
                    return 0;
                return Value->IsAbsolute ? DIDFT_ABSAXIS : DIDFT_RELAXIS;

            case HID_USAGE_PAGE_SIMULATION:
            case HID_USAGE_PAGE_VR:
            case HID_USAGE_PAGE_SPORT:
            case HID_USAGE_PAGE_GAME:
            case HID_USAGE_PAGE_KEYBOARD:
            case HID_USAGE_PAGE_CONSUMER:
            case HID_USAGE_PAGE_DIGITIZER:
                return Value->IsAbsolute ? DIDFT_ABSAXIS : DIDFT_RELAXIS;

            case HID_USAGE_PAGE_BUTTON:
                return Value->IsAbsolute ? DIDFT_PSHBUTTON : DIDFT_RELAXIS;
        }
        return 0;
    }

    static
    HRESULT
    SetAxisProperty(
        _Inout_ RX_DI_HID_OBJECT* Hid,
        _In_ ULONG_PTR Property,
        _In_ LPCDIPROPHEADER Header)
    {
        const DIPROPDWORD* Value = reinterpret_cast<const DIPROPDWORD*>(Header);

        if (Property == 4)
        {
            const DIPROPRANGE* Range = reinterpret_cast<const DIPROPRANGE*>(Header);

            if (Range->lMin >= Range->lMax)
                return E_INVALIDARG;
            Hid->RangeMin = Range->lMin;
            Hid->RangeMax = Range->lMax;
            return S_OK;
        }

        if (Value->dwData > 10000)
            return E_INVALIDARG;
        if (Property == 5)
            Hid->DeadZone = Value->dwData;
        else
            Hid->Saturation = Value->dwData;
        return S_OK;
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

    /* Axes rest in the middle of their range and POVs point nowhere */
    VOID
    ResetState(VOID)
    {
        ZeroMemory(State, Format.dwDataSize);
        for (ULONG Index = 0; Index < Format.dwNumObjs; Index++)
        {
            DWORD* Slot = reinterpret_cast<DWORD*>(State + Objects[Index].dwOfs);

            if (Objects[Index].dwType & DIDFT_ABSAXIS)
                *Slot = static_cast<DWORD>((Info[Index].RangeMin + Info[Index].RangeMax) / 2);
            else if (Objects[Index].dwType & DIDFT_POV)
                *Slot = MAXDWORD;
        }
    }

    LONG
    ScaleAxis(
        _In_ const RX_DI_HID_OBJECT* Hid,
        _In_ LONG Raw)
    {
        LONGLONG Span = static_cast<LONGLONG>(Hid->LogicalMax) - Hid->LogicalMin;
        LONGLONG Out = static_cast<LONGLONG>(Hid->RangeMax) - Hid->RangeMin;

        if (Span <= 0)
            return Hid->RangeMin;
        if (Raw < Hid->LogicalMin)
            Raw = Hid->LogicalMin;
        if (Raw > Hid->LogicalMax)
            Raw = Hid->LogicalMax;
        return static_cast<LONG>(Hid->RangeMin + ((Raw - Hid->LogicalMin) * Out + Span / 2) / Span);
    }

    VOID
    ParseReport(
        _In_ ULONG Length)
    {
        PBYTE Next = nullptr;

        if (FAILED(DiAllocate(Format.dwDataSize + sizeof(DWORD), reinterpret_cast<PVOID*>(&Next))))
            return;
        CopyMemory(Next, State, Format.dwDataSize);

        for (ULONG Index = 0; Index < Format.dwNumObjs; Index++)
        {
            const RX_DI_HID_OBJECT* Hid = &Info[Index];
            ULONG Type = Objects[Index].dwType;
            PBYTE Slot = Next + Objects[Index].dwOfs;

            if ((Type & DIDFT_COLLECTION) || !Hid->Input)
                continue;

            if (Hid->Value)
            {
                ULONG Raw = 0;
                LONG Signed = 0;

                if (HidP_GetUsageValue(HidP_Input, Hid->UsagePage, Hid->Collection, Hid->Usage, &Raw, Data,
                                       reinterpret_cast<PCHAR>(Report), Length) != HIDP_STATUS_SUCCESS)
                {
                    continue;
                }
                if (HidP_GetScaledUsageValue(HidP_Input, Hid->UsagePage, Hid->Collection, Hid->Usage, &Signed, Data,
                                             reinterpret_cast<PCHAR>(Report), Length) != HIDP_STATUS_SUCCESS)
                {
                    Signed = static_cast<LONG>(Raw);
                }

                if (Type & DIDFT_POV)
                {
                    LONG Count = Hid->LogicalMax - Hid->LogicalMin + 1;
                    LONG Step = static_cast<LONG>(Raw) - Hid->LogicalMin;

                    *reinterpret_cast<DWORD*>(Slot) = (Count <= 0 || Step < 0 || Step >= Count) ? MAXDWORD :
                                                      static_cast<DWORD>(Step * 36000 / Count);
                }
                else
                {
                    *reinterpret_cast<LONG*>(Slot) = ScaleAxis(Hid, static_cast<LONG>(Raw));
                }
            }
            else
            {
                USAGE Pressed[1];
                ULONG Count = 0;
                NTSTATUS Status;

                Status = HidP_GetUsages(HidP_Input, Hid->UsagePage, Hid->Collection, Pressed, &Count, Data,
                                        reinterpret_cast<PCHAR>(Report), Length);
                if (Status != HIDP_STATUS_SUCCESS && Status != HIDP_STATUS_BUFFER_TOO_SMALL)
                    continue;
                *Slot = 0;
            }
        }

        /* Buttons are read as one list per page and collection */
        for (ULONG Index = 0; Index < Format.dwNumObjs; Index++)
        {
            const RX_DI_HID_OBJECT* Hid = &Info[Index];
            USAGE List[128];
            ULONG Count = _countof(List);

            if (Hid->Value || !Hid->Input || (Objects[Index].dwType & DIDFT_BUTTON) == 0)
                continue;
            if (HidP_GetUsages(HidP_Input, Hid->UsagePage, Hid->Collection, List, &Count, Data,
                               reinterpret_cast<PCHAR>(Report), Length) != HIDP_STATUS_SUCCESS)
            {
                continue;
            }
            for (ULONG Pressed = 0; Pressed < Count; Pressed++)
            {
                if (List[Pressed] == Hid->Usage)
                    Next[Objects[Index].dwOfs] = 0x80;
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
        RxDiHid* Self = reinterpret_cast<RxDiHid*>(Sink->Extra);

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
    HKEY TypeKey = nullptr;
    BOOLEAN PidDevice = FALSE;
    USHORT VendorId = 0;
    USHORT ProductId = 0;
    BOOL Polled = FALSE;
    ULONG AxisCount = 0;
    ULONG ButtonCount = 0;
    ULONG PovCount = 0;
    ULONG ValueInstance = 0;
    ULONG ButtonInstance = 0;
    DIDATAFORMAT Format = {};
    DIOBJECTDATAFORMAT* Objects = nullptr;
    RX_DI_HID_OBJECT* Info = nullptr;
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
