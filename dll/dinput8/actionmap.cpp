/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Action map checks, the map checksum, device users and system default maps
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "actionmap.hpp"

#include <stdlib.h>
#include <strsafe.h>

/* Action flags a caller may set */
#define DI_ACTION_FLAGS_VALID       (DIA_FORCEFEEDBACK | DIA_APPMAPPED | DIA_APPNOMAP | DIA_NORANGE | DIA_APPFIXED)

/* A how value DirectInput reports for maps another application set up */
#define DI_HOW_OTHER_APP            0x10

/* Instance and type bits a default map compares when an action already names its object */
#define DI_OBJECT_ID_MASK           0x00FFFFFF

/* Mouse semantics past the last button */
#define DI_MOUSE_SEMANTIC_LAST      0x13

/* Resource string used as the user name when the system has none */
#define IDS_DEFAULT_USER            36

/* Pointers this low are resource ids, not strings */
#define DI_IS_RESOURCE_ID(Pointer)  ((reinterpret_cast<ULONG_PTR>(Pointer) >> 16) == 0)

BYTE
DiSemanticObjectType(
    _In_ DWORD Semantic)
{
    static const BYTE Types[8] = { 0, 0, DIDFT_ABSAXIS, DIDFT_RELAXIS, DIDFT_BUTTON, 0, DIDFT_POV, 0 };

    return Types[(Semantic >> 8) & 7];
}

HRESULT
DiCheckActionFormat(
    _In_opt_ const DIACTIONFORMATW* Format)
{
    DWORD Genre;

    if (Format == nullptr || Format->dwActionSize != sizeof(DIACTIONW))
        return E_INVALIDARG;
    if (static_cast<ULONG64>(Format->dwDataSize) != static_cast<ULONG64>(Format->dwNumActions) * sizeof(DWORD))
        return E_INVALIDARG;
    if (IsEqualGUID(Format->guidActionMap, GUID_NULL))
        return E_INVALIDARG;
    Genre = Format->dwGenre >> 24;
    if (Genre > DI_GENRE_LAST || (Genre & 0x7F) == 0)
        return E_INVALIDARG;
    if ((Format->lAxisMin != 0 || Format->lAxisMax != 0) && Format->lAxisMin > Format->lAxisMax)
        return E_INVALIDARG;
    if (Format->dwNumActions == 0 || (Format->dwNumActions & 0xFF000000) != 0 || Format->rgoAction == nullptr)
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
DiCheckWideString(
    _In_opt_z_ PCWSTR Text,
    _In_ DWORD Limit)
{
    DWORD Length = static_cast<DWORD>(lstrlenW(Text));

    if (Length != 0)
        return Length < Limit ? S_OK : E_INVALIDARG;
    return Text != nullptr && *Text == L'\0' ? S_OK : E_INVALIDARG;
}

HRESULT
DiCheckAnsiString(
    _In_opt_z_ PCSTR Text,
    _In_ DWORD Limit)
{
    DWORD Length = static_cast<DWORD>(lstrlenA(Text));

    if (Length != 0)
        return Length < Limit ? S_OK : E_INVALIDARG;
    return Text != nullptr && *Text == '\0' ? S_OK : E_INVALIDARG;
}

HRESULT
DiCheckActionNames(
    _In_ const DIACTIONFORMATW* Format,
    _In_ BOOLEAN Ansi)
{
    for (DWORD Index = 0; Index < Format->dwNumActions; Index++)
    {
        PCWSTR Name = Format->rgoAction[Index].lptszActionName;
        HRESULT Result;

        if (Name == nullptr || DI_IS_RESOURCE_ID(Name))
            continue;
        if (Ansi)
            Result = DiCheckAnsiString(reinterpret_cast<PCSTR>(Name), DI_ACTION_NAME_LIMIT);
        else
            Result = DiCheckWideString(Name, DI_ACTION_NAME_LIMIT);
        if (FAILED(Result))
            return Result;
    }
    return S_OK;
}

/* Orders actions by device, app mapped ones first, then by object or semantic */
static
LONG
DiCompareActions(
    _In_ const DIACTIONW* Left,
    _In_ const DIACTIONW* Right)
{
    int Order = memcmp(&Left->guidInstance, &Right->guidInstance, sizeof(GUID));
    BOOLEAN LeftFixed = (Left->dwFlags & DIA_APPMAPPED) != 0;
    BOOLEAN RightFixed = (Right->dwFlags & DIA_APPMAPPED) != 0;

    if (Order != 0)
        return Order;
    if (LeftFixed != RightFixed)
        return LeftFixed ? -1 : 1;
    if (LeftFixed)
        return static_cast<LONG>(Left->dwObjID - Right->dwObjID);
    return static_cast<LONG>(Left->dwSemantic - Right->dwSemantic);
}

static
VOID
DiSwapActions(
    _Inout_ const DIACTIONW** Left,
    _Inout_ const DIACTIONW** Right)
{
    const DIACTIONW* Saved = *Left;

    *Left = *Right;
    *Right = Saved;
}

/*
 * Quick sort down to runs of nine or fewer, left for the insertion pass. The comparison wraps,
 * so the exact partition steps decide which duplicates end up side by side.
 */
static
VOID
DiPartitionActions(
    _Inout_ const DIACTIONW** First,
    _Inout_ const DIACTIONW** Last)
{
    while (Last - First > 8)
    {
        const DIACTIONW** Middle = First + (Last - First) / 2;
        const DIACTIONW** Low = First;
        const DIACTIONW** High = Last - 1;
        const DIACTIONW* Key;

        if (DiCompareActions(*First, *Middle) > 0)
            DiSwapActions(First, Middle);
        if (DiCompareActions(*First, *Last) > 0)
            DiSwapActions(First, Last);
        if (DiCompareActions(*Middle, *Last) > 0)
            DiSwapActions(Middle, Last);
        DiSwapActions(Middle, Last - 1);
        Key = *(Last - 1);

        for (;;)
        {
            do
            {
                Low += 1;
            } while (DiCompareActions(*Low, Key) < 0);
            do
            {
                High -= 1;
            } while (High > First && DiCompareActions(*High, Key) > 0);
            if (Low >= High)
                break;
            DiSwapActions(Low, High);
        }
        DiSwapActions(Low, Last - 1);

        if (Low - First <= Last - Low)
        {
            DiPartitionActions(First, Low - 1);
            First = Low + 1;
        }
        else
        {
            DiPartitionActions(Low + 1, Last);
            Last = Low - 1;
        }
    }
}

static
VOID
DiSortActions(
    _Inout_updates_(Count) const DIACTIONW** List,
    _In_ SIZE_T Count)
{
    DiPartitionActions(List, List + Count - 1);
    for (SIZE_T Index = 1; Index < Count; Index++)
    {
        const DIACTIONW* Moving = List[Index];
        SIZE_T Slot = Index;

        while (Slot > 0 && DiCompareActions(Moving, List[Slot - 1]) > 0)
        {
            List[Slot] = List[Slot - 1];
            Slot -= 1;
        }
        List[Slot] = Moving;
    }
}

static
BOOLEAN
DiIsKnownHow(
    _In_ DWORD How)
{
    switch (How)
    {
        case DIAH_USERCONFIG:
        case DIAH_APPREQUESTED:
        case DIAH_HWAPP:
        case DIAH_HWDEFAULT:
        case DI_HOW_OTHER_APP:
        case DIAH_DEFAULT:
            return TRUE;
        default:
            return FALSE;
    }
}

/* Whether a semantic belongs to the format's genre or to a class every genre may use */
static
BOOLEAN
DiSemanticFitsGenre(
    _In_ DWORD Semantic,
    _In_ DWORD Genre)
{
    DWORD Class = Semantic >> 24;

    if (((Semantic ^ Genre) & 0xFF000000) == 0)
        return TRUE;
    return Class == (DI_SEMANTIC_KEYBOARD >> 24) || Class == (DI_SEMANTIC_MOUSE >> 24) ||
           Class == (DI_SEMANTIC_COMMS >> 24) || Class == 0xFF;
}

HRESULT
DiValidateSemantics(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_ DWORD MapFlags)
{
    const DIACTIONW** Kept;
    SIZE_T KeptCount = 0;
    BOOLEAN Fresh = (MapFlags & (DIDBAM_INITIALIZE | DIDBAM_HWDEFAULTS)) != 0;
    BOOLEAN Preserve = (MapFlags & DIDBAM_PRESERVE) != 0;
    HRESULT Result = S_OK;

    Kept = static_cast<const DIACTIONW**>(malloc(Format->dwNumActions * sizeof(*Kept)));
    if (Kept == nullptr)
        return E_OUTOFMEMORY;

    for (DWORD Index = 0; Index < Format->dwNumActions; Index++)
    {
        DIACTIONW* Action = &Format->rgoAction[Index];

        if (Fresh)
        {
            Action->dwFlags = 0;
            Action->guidInstance = GUID_NULL;
            Action->dwHow = DIAH_UNMAPPED;
        }
        else
        {
            if (Action->dwFlags & ~DI_ACTION_FLAGS_VALID)
                goto Reject;
            if (Action->dwFlags & DIA_APPNOMAP)
                continue;
            if (!Preserve)
            {
                if (!(Action->dwFlags & DIA_APPMAPPED))
                    Action->guidInstance = GUID_NULL;
                Action->dwHow = DIAH_UNMAPPED;
            }
            else if (Action->dwHow != DIAH_UNMAPPED)
            {
                if (!DiIsKnownHow(Action->dwHow))
                {
                    if (Action->dwHow == DIAH_ERROR)
                    {
                        Result = E_INVALIDARG;
                        goto Exit;
                    }
                    goto Reject;
                }
                if (IsEqualGUID(Action->guidInstance, GUID_NULL))
                {
                    Result = E_INVALIDARG;
                    goto Exit;
                }
            }
        }

        if (!DiSemanticFitsGenre(Action->dwSemantic, Format->dwGenre))
            goto Reject;
        if (!(Action->dwFlags & DIA_APPMAPPED) &&
            (LOBYTE(Action->dwSemantic) == 0xFF || (Action->dwSemantic & 0x00F80000) != 0 ||
             DiSemanticObjectType(Action->dwSemantic) == 0))
        {
            goto Reject;
        }
        Kept[KeptCount++] = Action;
        continue;

Reject:
        Action->dwHow = DIAH_ERROR;
        Result = E_INVALIDARG;
        goto Exit;
    }

    if (KeptCount == 0)
    {
        Result = S_FALSE;
        goto Exit;
    }
    DiSortActions(Kept, KeptCount);
    for (SIZE_T Index = 1; Index < KeptCount; Index++)
    {
        /* Two unmapped actions may not ask one device for the same semantic */
        if (DiCompareActions(Kept[Index - 1], Kept[Index]) == 0 && !(Kept[Index]->dwFlags & DIA_APPMAPPED))
        {
            Result = E_INVALIDARG;
            break;
        }
    }

Exit:
    free(Kept);
    return Result;
}

/* The checksum's table holds the polynomial in its first slot, so index zero folds that in */
static
DWORD
DiCrcStep(
    _In_ DWORD Crc,
    _In_ BYTE Value)
{
    DWORD Entry = (Crc ^ Value) & 0xFF;

    if (Entry == 0)
    {
        Entry = 0x04C11DB7;
    }
    else
    {
        for (UINT Bit = 0; Bit < 8; Bit++)
            Entry = (Entry & 1) ? (Entry >> 1) ^ 0xEDB88320 : Entry >> 1;
    }
    return Entry ^ (Crc >> 8);
}

/* Folds the bytes from High down to Low, both included */
static
DWORD
DiCrcDown(
    _In_ DWORD Crc,
    _In_ const BYTE* Low,
    _In_ const BYTE* High)
{
    for (const BYTE* Byte = High; Byte >= Low; Byte -= 1)
        Crc = DiCrcStep(Crc, *Byte);
    return Crc;
}

DWORD
DiActionMapCrc(
    _In_ LPCDIACTIONFORMATW Format,
    _In_ REFGUID Device)
{
    const BYTE* Header = reinterpret_cast<const BYTE*>(Format);
    DWORD Crc = Format->dwNumActions;

    /* The map GUID, the genre and the low byte of the buffer size */
    Crc = DiCrcDown(Crc, Header + FIELD_OFFSET(DIACTIONFORMATW, guidActionMap),
                    Header + FIELD_OFFSET(DIACTIONFORMATW, dwBufferSize));
    for (DWORD Index = 0; Index < Format->dwNumActions; Index++)
    {
        const DIACTIONW* Action = &Format->rgoAction[Index];
        const BYTE* Bytes = reinterpret_cast<const BYTE*>(Action);

        if (!IsEqualGUID(Action->guidInstance, Device) || (Action->dwHow & DI_HOW_MAPPED_MASK) == 0 ||
            (Action->dwFlags & DIA_APPNOMAP))
        {
            continue;
        }
        Crc = DiCrcDown(Crc, Bytes + FIELD_OFFSET(DIACTIONW, guidInstance), Bytes + FIELD_OFFSET(DIACTIONW, dwHow));
    }
    return Crc;
}

HRESULT
DiResolveMapUser(
    _In_opt_z_ PCSTR Ansi,
    _In_opt_z_ PCWSTR Wide,
    _Outptr_ PWSTR* User)
{
    HRESULT Result;
    PWSTR Name;
    DWORD Size;

    *User = nullptr;
    if (Wide != nullptr)
    {
        Result = DiCheckWideString(Wide, DI_MAP_USER_LIMIT);
        if (SUCCEEDED(Result))
            *User = const_cast<PWSTR>(Wide);
        return Result;
    }
    if (Ansi != nullptr)
    {
        Result = DiCheckAnsiString(Ansi, DI_MAP_USER_LIMIT);
        if (FAILED(Result))
            return Result;
        Size = static_cast<DWORD>(lstrlenA(Ansi)) + 1;
        Name = static_cast<PWSTR>(calloc(Size, sizeof(WCHAR)));
        if (Name == nullptr)
            return E_OUTOFMEMORY;
        MultiByteToWideChar(CP_ACP, 0, Ansi, -1, Name, static_cast<int>(Size));
        *User = Name;
        return S_OK;
    }

    Name = static_cast<PWSTR>(calloc(DI_MAP_USER_LIMIT, sizeof(WCHAR)));
    if (Name == nullptr)
        return E_OUTOFMEMORY;
    Size = DI_MAP_USER_LIMIT;
    if (!GetUserNameW(Name, &Size) && LoadStringW(DiInstance, IDS_DEFAULT_USER, Name, DI_MAP_USER_LIMIT) == 0)
    {
        DiTrace(L"No user name for an action map (%lu)", GetLastError());
        free(Name);
        return E_FAIL;
    }
    *User = Name;
    return S_OK;
}

VOID
DiFreeMapUser(
    _In_opt_ PWSTR User,
    _In_opt_z_ PCWSTR Wide)
{
    if (User != Wide)
        free(User);
}

/* Who each device was last set up for, kept for the life of the process */
struct DI_DEVICE_USER
{
    GUID Device;
    WCHAR Name[MAX_PATH];
};

static DI_DEVICE_USER* DiDeviceUsers;
static DWORD DiDeviceUserSlots;

static
DI_DEVICE_USER*
DiFindDeviceUser(
    _In_ REFGUID Device)
{
    for (DWORD Index = 0; Index < DiDeviceUserSlots; Index++)
    {
        if (IsEqualGUID(DiDeviceUsers[Index].Device, Device))
            return &DiDeviceUsers[Index];
    }
    return nullptr;
}

HRESULT
DiSetDeviceUser(
    _In_ REFGUID Device,
    _In_opt_z_ PCWSTR User)
{
    DI_DEVICE_USER* Entry;
    HRESULT Result = S_OK;

    EnterCriticalSection(&DiLock);
    Entry = DiFindDeviceUser(Device);
    if (User == nullptr)
    {
        if (Entry == nullptr)
            Result = S_FALSE;
        else
            Entry->Device = GUID_NULL;
        goto Exit;
    }
    if (Entry == nullptr)
    {
        Entry = DiFindDeviceUser(GUID_NULL);
        if (Entry == nullptr)
        {
            DI_DEVICE_USER* Grown = static_cast<DI_DEVICE_USER*>(
                realloc(DiDeviceUsers, (DiDeviceUserSlots + 4) * sizeof(*Grown)));

            if (Grown == nullptr)
            {
                Result = E_OUTOFMEMORY;
                goto Exit;
            }
            ZeroMemory(Grown + DiDeviceUserSlots, 4 * sizeof(*Grown));
            Entry = Grown + DiDeviceUserSlots;
            DiDeviceUsers = Grown;
            DiDeviceUserSlots += 4;
        }
        Entry->Device = Device;
    }
    StringCchCopyW(Entry->Name, _countof(Entry->Name), User);

Exit:
    LeaveCriticalSection(&DiLock);
    return Result;
}

HRESULT
DiGetDeviceUser(
    _In_ REFGUID Device,
    _Out_writes_(MAX_PATH) PWSTR User)
{
    DI_DEVICE_USER* Entry;
    HRESULT Result = S_FALSE;

    User[0] = L'\0';
    EnterCriticalSection(&DiLock);
    Entry = DiFindDeviceUser(Device);
    if (Entry != nullptr && Entry->Name[0] != L'\0')
    {
        StringCchCopyW(User, MAX_PATH, Entry->Name);
        Result = S_OK;
    }
    LeaveCriticalSection(&DiLock);
    return Result;
}

BOOLEAN
DiIsDeviceUser(
    _In_ REFGUID Device,
    _In_z_ PCWSTR User)
{
    DI_DEVICE_USER* Entry;
    BOOLEAN Same;

    EnterCriticalSection(&DiLock);
    Entry = DiFindDeviceUser(Device);
    Same = Entry != nullptr && lstrcmpW(User, Entry->Name) == 0;
    LeaveCriticalSection(&DiLock);
    return Same;
}

/* Whether a native object is the one a system class semantic names */
static
BOOLEAN
DiSystemObjectMatches(
    _In_ DWORD Class,
    _In_ const DI_OBJECT* Object,
    _In_ BYTE Wanted,
    _In_ DWORD Semantic,
    _In_ DWORD FirstButton)
{
    DWORD Instance = DIDFT_GETINSTANCE(Object->Type);
    DWORD Slot = LOBYTE(Semantic);

    /* Keyboard semantics carry the scan code, which is the key's native offset */
    if (Class == DI_SEMANTIC_KEYBOARD)
        return Object->Offset == Slot;
    if (Wanted & DIDFT_BUTTON)
        return Instance - FirstButton == Slot - 12;
    return Instance * 4 == Slot;
}

HRESULT
DiBuildSystemDefaultMap(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_ DWORD Class,
    _In_ REFGUID Device,
    _In_ const DI_FORMAT* Native,
    _In_ DWORD FirstButton)
{
    BYTE* Taken;
    BOOLEAN AnyMapped = FALSE;
    HRESULT Result = S_OK;

    Taken = static_cast<BYTE*>(calloc(Native->ObjectCount + 1, sizeof(*Taken)));
    if (Taken == nullptr)
        return E_OUTOFMEMORY;

    /* Mapped actions claim their objects, then actions aimed at this device, then unaimed ones */
    for (UINT Pass = 0; Pass < 3; Pass++)
    {
        for (DWORD Index = 0; Index < Format->dwNumActions; Index++)
        {
            DIACTIONW* Action = &Format->rgoAction[Index];
            BOOLEAN Mapped = (Action->dwHow & DI_HOW_MAPPED_MASK) != 0;
            BOOLEAN SameClass = (Action->dwSemantic >> 24) == (Class >> 24);
            DWORD Chosen = static_cast<DWORD>(-1);
            DWORD Object;
            BYTE Wanted;

            if (Action->dwFlags & DIA_APPNOMAP)
                continue;
            if (Pass == 2)
            {
                if (Mapped || !IsEqualGUID(Action->guidInstance, GUID_NULL) || !SameClass)
                    continue;
            }
            else
            {
                if (Mapped != (Pass == 0) || !IsEqualGUID(Action->guidInstance, Device))
                    continue;

                /* An action of another class ends the pass early */
                if (!SameClass)
                    break;
            }

            if (Pass == 0)
            {
                for (Object = 0; Object < Native->ObjectCount; Object++)
                {
                    if ((Native->Objects[Object].Type & DI_OBJECT_ID_MASK) == (Action->dwObjID & DI_OBJECT_ID_MASK))
                        break;
                }
                if (Object == Native->ObjectCount)
                    goto Reject;
                Taken[Object] = 1;
                continue;
            }

            Wanted = DiSemanticObjectType(Action->dwSemantic);
            for (Object = 0; Object < Native->ObjectCount; Object++)
            {
                DWORD Type = Native->Objects[Object].Type;

                if ((Type & Wanted) == 0 || (Type & DIDFT_NODATA) ||
                    !DiSystemObjectMatches(Class, &Native->Objects[Object], Wanted, Action->dwSemantic, FirstButton))
                {
                    continue;
                }
                if (!Taken[Object] &&
                    (!(Action->dwFlags & DIA_FORCEFEEDBACK) || (Type & (DIDFT_FFACTUATOR | DIDFT_FFEFFECTTRIGGER))))
                {
                    Chosen = Type;
                }
                break;
            }
            if (Chosen == static_cast<DWORD>(-1))
            {
                /* Unaimed actions may go unmapped, except mouse semantics no mouse object has */
                if (Pass == 2 && (Class != DI_SEMANTIC_MOUSE || LOBYTE(Action->dwSemantic) <= DI_MOUSE_SEMANTIC_LAST))
                    continue;
                goto Reject;
            }
            Action->dwObjID = Chosen;
            Action->dwHow = DIAH_DEFAULT;
            Action->guidInstance = Device;
            Taken[Object] = 1;
            AnyMapped = TRUE;
            continue;

Reject:
            Action->dwHow = DIAH_ERROR;
            Result = E_INVALIDARG;
            goto Exit;
        }
    }
    Result = AnyMapped ? S_OK : S_FALSE;

Exit:
    free(Taken);
    return Result;
}

VOID
DiActionHeaderFromAnsi(
    _Out_ LPDIACTIONFORMATW Wide,
    _In_ LPCDIACTIONFORMATA Ansi)
{
    ZeroMemory(Wide, sizeof(*Wide));
    Wide->dwSize = sizeof(*Wide);
    Wide->dwActionSize = Ansi->dwActionSize;
    Wide->dwDataSize = Ansi->dwDataSize;
    Wide->dwNumActions = Ansi->dwNumActions;
    Wide->rgoAction = reinterpret_cast<LPDIACTIONW>(Ansi->rgoAction);
    Wide->guidActionMap = Ansi->guidActionMap;
    Wide->dwGenre = Ansi->dwGenre;
    Wide->dwBufferSize = Ansi->dwBufferSize;
    Wide->lAxisMin = Ansi->lAxisMin;
    Wide->lAxisMax = Ansi->lAxisMax;
    Wide->hInstString = Ansi->hInstString;
    Wide->ftTimeStamp = Ansi->ftTimeStamp;
    Wide->dwCRC = Ansi->dwCRC;
    MultiByteToWideChar(CP_ACP, 0, Ansi->tszActionMap, -1, Wide->tszActionMap, _countof(Wide->tszActionMap));
}
