/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device action maps: building, applying as a data format and device images
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "actionmap.hpp"

#include <stdlib.h>

/* Internal properties */
#define DI_PROP_TYPE_KEY            0xFFFD
#define DI_PROP_ENABLE_OBJECT       0xFFFB

/* Force feedback bits a native object type carries */
#define DI_OBJECT_FF_MASK           (DIDFT_FFACTUATOR | DIDFT_FFEFFECTTRIGGER)

/* Build flags DIDBAM accepts on their own */
#define DI_BUILD_MODE_MASK          (DIDBAM_PRESERVE | DIDBAM_INITIALIZE | DIDBAM_HWDEFAULTS)

/* Semantic type bits naming an axis, absolute or relative */
#define DI_SEMANTIC_IS_AXIS(Semantic)   ((((Semantic) >> 9) & 3) == 1)

static
BOOLEAN
DiIsUnsaved(
    _In_ const FILETIME* Stamp)
{
    return Stamp->dwLowDateTime == 0xFFFFFFFF && Stamp->dwHighDateTime == 0xFFFFFFFF;
}

/* Only devices with a type key have anything in the map store */
BOOLEAN
DiDevice::HasTypeKey(VOID)
{
    DIPROPSTRING Key = { { sizeof(Key), sizeof(DIPROPHEADER), 0, DIPH_DEVICE } };

    return SUCCEEDED(GetProperty(MAKEDIPROP(DI_PROP_TYPE_KEY), &Key.diph));
}

/* An app mapped object a system class has no native object for must still be one it could have */
static
HRESULT
DiCheckSystemObject(
    _In_ DWORD Class,
    _In_ DWORD Object)
{
    DWORD Instance = DIDFT_GETINSTANCE(Object);

    switch (Class)
    {
        case DI_SEMANTIC_KEYBOARD:
            return (Object & DIDFT_BUTTON) && (Object & 0x00FF0000) == 0 ? S_OK : E_INVALIDARG;
        case DI_SEMANTIC_MOUSE:
            if ((Object & DIDFT_PSHBUTTON) && Instance < 8)
                return S_OK;
            if ((Object & DIDFT_AXIS) && Instance < 3)
                return S_OK;
            return E_INVALIDARG;
        case DI_SEMANTIC_COMMS:
            return E_INVALIDARG;
        default:
            return S_OK;
    }
}

/*
 * Checks the actions already mapped to this device and, when asked, takes the app mapped ones
 * aimed at it. S_FALSE means nothing was checked or taken.
 */
HRESULT
DiDevice::CheckDeviceMap(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_ BOOLEAN TakeAppMapped,
    _Out_ DWORD* Comms)
{
    const DI_FORMAT* Native = Backend->Format();
    DWORD Class = 0;
    DWORD Kind = 0;
    DWORD LastPass;
    BYTE* Taken;
    BOOLEAN AnyChecked = FALSE;
    BOOLEAN AnyTaken = FALSE;
    BOOLEAN MarkNext = FALSE;
    HRESULT Result = S_OK;

    switch (LOBYTE(DevType))
    {
        case DI8DEVTYPE_MOUSE:
            Class = DI_SEMANTIC_MOUSE;
            break;
        case DI8DEVTYPE_KEYBOARD:
            Class = DI_SEMANTIC_KEYBOARD;
            break;
        case DI8DEVTYPE_DEVICECTRL:
            if (HIBYTE(LOWORD(DevType)) == DI_COMMS_SELECTION || HIBYTE(LOWORD(DevType)) == DI_COMMS_HARDWIRED)
            {
                Kind = HIBYTE(LOWORD(DevType));
                Class = DI_SEMANTIC_COMMS;
            }
            break;
        default:
            break;
    }
    *Comms = Kind;

    Taken = static_cast<BYTE*>(calloc(Native->ObjectCount + 1, sizeof(*Taken)));
    if (Taken == nullptr)
        return E_OUTOFMEMORY;

    /* Mapped actions first, then app mapped ones aimed at this device, then those of its class */
    LastPass = TakeAppMapped ? (Class != 0 ? 2 : 1) : 0;
    for (DWORD Pass = 0; Pass <= LastPass && !MarkNext; Pass++)
    {
        for (DWORD Index = 0; Index < Format->dwNumActions; Index++)
        {
            DIACTIONW* Action = &Format->rgoAction[Index];
            BOOLEAN Mapped = (Action->dwHow & DI_HOW_MAPPED_MASK) != 0;
            DWORD Object;

            /* A hardwired device rejects the action after the one it refused */
            if (MarkNext)
            {
                Action->dwHow = DIAH_ERROR;
                goto Exit;
            }
            if (Action->dwFlags & DIA_APPNOMAP)
                continue;
            if (Pass == 0)
            {
                if (!Mapped || !IsEqualGUID(Action->guidInstance, Instance))
                    continue;
            }
            else
            {
                if (!(Action->dwFlags & DIA_APPMAPPED) || Mapped)
                    continue;
                if (Pass == 1 && !IsEqualGUID(Action->guidInstance, Instance))
                    continue;
                if (Pass == 2 && (Action->dwSemantic >> 24) != (Class >> 24))
                    continue;
                if (Kind == DI_COMMS_HARDWIRED)
                {
                    Result = E_INVALIDARG;
                    MarkNext = TRUE;
                    continue;
                }
            }

            for (Object = 0; Object < Native->ObjectCount; Object++)
            {
                if (((Native->Objects[Object].Type ^ Action->dwObjID) & ~DI_OBJECT_FF_MASK) == 0)
                    break;
            }
            if (Object == Native->ObjectCount)
            {
                if (Pass < 2)
                    goto Reject;
                Result = DiCheckSystemObject(Class, Action->dwObjID);
                if (FAILED(Result))
                {
                    Action->dwHow = DIAH_ERROR;
                    goto Exit;
                }
                continue;
            }

            if (Action->dwFlags & DIA_APPMAPPED)
            {
                if (Taken[Object] ||
                    ((Action->dwFlags & DIA_FORCEFEEDBACK) && !(Native->Objects[Object].Type & DI_OBJECT_FF_MASK)))
                {
                    goto Reject;
                }
                Taken[Object] = 1;
                AnyChecked = TRUE;
                if (Pass != 0)
                {
                    AnyTaken = TRUE;
                    Action->dwHow = DIAH_APPREQUESTED;
                    if (Pass == 2)
                        Action->guidInstance = Instance;
                }
            }
            else
            {
                if ((DiSemanticObjectType(Action->dwSemantic) & Action->dwObjID) == 0 || Taken[Object])
                    goto Reject;
                Taken[Object] = 1;
                AnyChecked = TRUE;
            }
            continue;

Reject:
            Action->dwHow = DIAH_ERROR;
            Result = E_INVALIDARG;
            goto Exit;
        }
    }

Exit:
    free(Taken);
    if (FAILED(Result))
        return Result;
    if (Kind == DI_COMMS_HARDWIRED)
        return DI_MAP_HARDWIRED;
    if (TakeAppMapped)
        return AnyTaken ? S_OK : S_FALSE;
    return AnyChecked ? S_OK : S_FALSE;
}

HRESULT
DiDevice::BuildMapCore(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_ DWORD MapFlags)
{
    BOOLEAN HwDefaults = (MapFlags & DIDBAM_HWDEFAULTS) != 0;
    BOOLEAN UseDefaults;
    DWORD Mode = MapFlags & DI_BUILD_MODE_MASK;
    DWORD Comms;
    HRESULT Checked;
    HRESULT Stored;
    HRESULT Result;

    DiRegisterApplication(Version, TRUE);
    if ((Mode != DIDBAM_DEFAULT && Mode != DIDBAM_PRESERVE && Mode != DIDBAM_INITIALIZE && Mode != DIDBAM_HWDEFAULTS) ||
        (MapFlags & ~DI_BUILD_MODE_MASK) != 0)
    {
        DI_TRACE_ONCE(L"BuildActionMap rejected flags %#lx", MapFlags);
        return E_INVALIDARG;
    }
    Result = DiValidateSemantics(Format, MapFlags);
    if (FAILED(Result))
        return Result;

    Checked = CheckDeviceMap(Format, !HwDefaults, &Comms);
    if (FAILED(Checked))
        return Checked;
    if (HwDefaults && Checked == S_OK)
        Checked = S_FALSE;

    /* What a map store would say; there is none, so a device with a type key gets its failure */
    if (Comms != 0)
    {
        if (HasTypeKey())
            return DI_MAP_STORE_MISSING;
        Stored = S_FALSE;
    }
    else if (HwDefaults && !HasTypeKey())
    {
        Stored = DI_MAP_NOT_STORED;
    }
    else
    {
        Stored = DI_MAP_STORE_MISSING;
    }

    UseDefaults = FAILED(Stored) || Stored == DI_MAP_NOT_STORED;
    for (DWORD Index = 0; !UseDefaults && Comms != DI_COMMS_HARDWIRED && Index < Format->dwNumActions; Index++)
    {
        const DIACTIONW* Action = &Format->rgoAction[Index];

        /* A stored map that left hardware defaults on this device asks for the defaults again */
        if ((Action->dwFlags & (DIA_APPMAPPED | DIA_APPNOMAP)) == 0 && (Action->dwHow & DIAH_HWDEFAULT) &&
            IsEqualGUID(Action->guidInstance, Instance))
        {
            UseDefaults = TRUE;
        }
    }
    if (UseDefaults)
    {
        Result = Backend->BuildDefaultActionMap(Format, MapFlags, Instance);
        if (SUCCEEDED(Result))
        {
            Format->ftTimeStamp.dwLowDateTime = 0xFFFFFFFF;
            Format->ftTimeStamp.dwHighDateTime = 0xFFFFFFFF;
        }
    }
    else
    {
        Result = CheckDeviceMap(Format, FALSE, &Comms);
    }
    if (Result == S_FALSE)
        Result = Checked;

    /* A fresh map would be saved now; with no store it keeps no time stamp */
    if (HwDefaults || (DiIsUnsaved(&Format->ftTimeStamp) && SUCCEEDED(Result)))
    {
        Format->ftTimeStamp.dwLowDateTime = 0;
        Format->ftTimeStamp.dwHighDateTime = 0;
    }
    if (SUCCEEDED(Result))
        Format->dwCRC = DiActionMapCrc(Format, Instance);
    return Result;
}

STDMETHODIMP
DiDevice::BuildActionMap(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_opt_z_ LPCWSTR UserName,
    _In_ DWORD MapFlags)
{
    PWSTR User;
    HRESULT Result;

    Result = DiCheckActionFormat(Format);
    if (FAILED(Result))
        return Result;
    if (Format->dwSize != sizeof(*Format))
        return E_INVALIDARG;
    Result = DiResolveMapUser(nullptr, UserName, &User);
    if (FAILED(Result))
        return Result;
    if (Backend == nullptr)
        Result = DIERR_NOTINITIALIZED;
    else
        Result = BuildMapCore(Format, MapFlags);
    DiFreeMapUser(User, UserName);
    return Result;
}

STDMETHODIMP
DiDevice::AnsiFace::BuildActionMap(
    _Inout_ LPDIACTIONFORMATA Format,
    _In_opt_z_ LPCSTR UserName,
    _In_ DWORD Flags)
{
    DIACTIONFORMATW Wide;
    PWSTR User;
    HRESULT Result;

    Result = DiCheckActionFormat(reinterpret_cast<const DIACTIONFORMATW*>(Format));
    if (FAILED(Result))
        return Result;
    if (Format->dwSize != sizeof(*Format))
        return E_INVALIDARG;
    Result = DiResolveMapUser(UserName, nullptr, &User);
    if (FAILED(Result))
        return Result;
    if (Owner->Backend == nullptr)
    {
        Result = DIERR_NOTINITIALIZED;
    }
    else
    {
        DiActionHeaderFromAnsi(&Wide, Format);
        Result = Owner->BuildMapCore(&Wide, Flags);
        Format->ftTimeStamp = Wide.ftTimeStamp;
        Format->dwCRC = Wide.dwCRC;
    }
    DiFreeMapUser(User, nullptr);
    return Result;
}

/* Fills the format from the actions mapped to this device; S_FALSE when none are */
HRESULT
DiDevice::ParseMapFormat(
    _In_ LPCDIACTIONFORMATW Format)
{
    const DI_FORMAT* Native = Backend->Format();
    DI_APP_OBJECT* Objects;
    LONG* ByOffset;
    LONG* ByNative;
    BOOLEAN AnyMapped = FALSE;
    BOOLEAN AxisSeen = FALSE;
    DWORD AxisMode = DIPROPAXISMODE_REL;
    HRESULT Result = S_OK;

    if (Format->dwDataSize != Format->dwNumActions * sizeof(DWORD))
        return E_INVALIDARG;
    Objects = static_cast<DI_APP_OBJECT*>(malloc((Native->ObjectCount + 1) * sizeof(*Objects)));
    ByOffset = static_cast<LONG*>(malloc((Format->dwDataSize + 1) * sizeof(*ByOffset)));
    ByNative = static_cast<LONG*>(malloc((Native->DataSize + 1) * sizeof(*ByNative)));
    if (Objects == nullptr || ByOffset == nullptr || ByNative == nullptr)
    {
        Result = E_OUTOFMEMORY;
        goto Exit;
    }
    for (DWORD Index = 0; Index <= Native->ObjectCount; Index++)
    {
        Objects[Index].Offset = -1;
        Objects[Index].AppData = static_cast<UINT_PTR>(-1);
    }
    memset(ByOffset, 0xFF, (Format->dwDataSize + 1) * sizeof(*ByOffset));
    memset(ByNative, 0xFF, (Native->DataSize + 1) * sizeof(*ByNative));

    for (DWORD Index = 0; Index < Format->dwNumActions; Index++)
    {
        const DIACTIONW* Action = &Format->rgoAction[Index];
        const DI_OBJECT* Object;
        DWORD Match;

        if (!IsEqualGUID(Action->guidInstance, Instance) || (Action->dwHow & DI_HOW_MAPPED_MASK) == 0 ||
            (Action->dwFlags & DIA_APPNOMAP))
        {
            continue;
        }
        for (Match = 0; Match < Native->ObjectCount; Match++)
        {
            if (Native->Objects[Match].Type == Action->dwObjID)
                break;
        }
        if (Match == Native->ObjectCount)
        {
            Result = E_INVALIDARG;
            goto Exit;
        }

        /* Each action reports through the DWORD at its own index */
        Object = &Native->Objects[Match];
        ByNative[Object->Offset] = static_cast<LONG>(Match);
        Objects[Match].Offset = static_cast<LONG>(Index * sizeof(DWORD));
        Objects[Match].AppData = Action->uAppData;
        ByOffset[Index * sizeof(DWORD)] = static_cast<LONG>(Match);
        if (Object->Flags & DIDOI_POLLED)
            Flags |= DI_DEV_POLLED_FORMAT;
        {
            DI_PROPERTY Enable = { DI_PROP_ENABLE_OBJECT, Match, Object->Type };
            DIPROPDWORD Value = { { sizeof(Value), sizeof(DIPROPHEADER), Object->Type, DIPH_BYID }, 1 };
            HRESULT Enabled = Backend->SetProperty(&Enable, &Value.diph);

            /* A device that cannot report an object keeps what was set up so far */
            if (FAILED(Enabled) && Enabled != E_NOTIMPL)
            {
                AnyMapped = TRUE;
                break;
            }
        }
        AnyMapped = TRUE;

        if (DI_SEMANTIC_IS_AXIS(Action->dwSemantic))
        {
            BOOLEAN First = !AxisSeen;

            AxisSeen = TRUE;
            if (Object->Type & DIDFT_ABSAXIS)
            {
                if (First)
                    AxisMode = DIPROPAXISMODE_ABS;
                if (!(Action->dwFlags & DIA_NORANGE) && (Format->lAxisMin != 0 || Format->lAxisMax != 0))
                {
                    DIPROPRANGE Range = { { sizeof(Range), sizeof(DIPROPHEADER), Object->Type, DIPH_BYID },
                                          Format->lAxisMin, Format->lAxisMax };

                    SetProperty(DIPROP_RANGE, &Range.diph);
                }
            }
        }
    }
    if (!AnyMapped)
    {
        Result = S_FALSE;
        goto Exit;
    }

    if (Emulation != nullptr)
    {
        Result = DiEmSetObjectMap(Emulation, ByNative, Native->DataSize);
        if (FAILED(Result))
            goto Exit;
    }
    AppObjects = Objects;
    ObjectByOffset = ByOffset;
    AppDataSize = Format->dwDataSize;
    Objects = nullptr;
    ByOffset = nullptr;
    {
        DIPROPDWORD Value = { { sizeof(Value), sizeof(DIPROPHEADER), 0, DIPH_DEVICE }, Format->dwBufferSize };

        if (FAILED(SetProperty(DIPROP_BUFFERSIZE, &Value.diph)))
        {
            Result = E_INVALIDARG;
            goto Exit;
        }
        if (AxisSeen)
        {
            Value.dwData = AxisMode;
            SetProperty(DIPROP_AXISMODE, &Value.diph);
        }
    }
    Result = S_OK;

Exit:
    free(Objects);
    free(ByOffset);
    free(ByNative);
    return Result;
}

HRESULT
DiDevice::ApplyMapFormat(
    _In_ LPCDIACTIONFORMATW Format)
{
    HRESULT Result;

    EnterCriticalSection(&Lock);
    if (Flags & DI_DEV_ACQUIRED)
    {
        Result = DIERR_ACQUIRED;
        goto Exit;
    }
    ClearFormat();
    Flags &= ~DI_DEV_POLLED_FORMAT;
    {
        DI_PROPERTY Disable = { DI_PROP_ENABLE_OBJECT, static_cast<DWORD>(-1), 0 };
        DIPROPDWORD Value = { { sizeof(Value), sizeof(DIPROPHEADER), 0, DIPH_DEVICE }, 0 };

        Result = Backend->SetProperty(&Disable, &Value.diph);
        if (FAILED(Result) && Result != E_NOTIMPL)
            goto Exit;
    }
    Result = ParseMapFormat(Format);
    if (Result == S_OK)
        PlanStateCopy();

Exit:
    LeaveCriticalSection(&Lock);
    return Result;
}

HRESULT
DiDevice::SetMapCore(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_opt_z_ PCSTR AnsiUser,
    _In_opt_z_ PCWSTR WideUser,
    _In_ DWORD MapFlags)
{
    BOOLEAN Save;
    PWSTR User;
    DWORD Crc;
    DWORD Comms;
    HRESULT Result;

    if (MapFlags & ~(DIDSAM_NOUSER | DIDSAM_FORCESAVE))
        return E_INVALIDARG;
    if (MapFlags & DIDSAM_NOUSER)
        return MapFlags == DIDSAM_NOUSER ? DiSetDeviceUser(Instance, nullptr) : E_INVALIDARG;

    Result = DiResolveMapUser(AnsiUser, WideUser, &User);
    if (FAILED(Result))
        return Result;

    /* A map is saved when forced, when it changed since it was built, or when the user changed */
    Crc = DiActionMapCrc(Format, Instance);
    Save = (MapFlags & DIDSAM_FORCESAVE) != 0 || Format->dwCRC != Crc;
    if (!Save)
        Save = !DiIsDeviceUser(Instance, User);
    if (Save)
    {
        Result = DiValidateSemantics(Format, DIDBAM_PRESERVE);
        if (FAILED(Result))
            goto Exit;
        Result = CheckDeviceMap(Format, FALSE, &Comms);
        if (FAILED(Result))
            goto Exit;
        if (Result == DI_MAP_HARDWIRED && Format->dwCRC != Crc)
        {
            Result = E_INVALIDARG;
            goto Exit;
        }
    }

    Result = DiSetDeviceUser(Instance, User);
    if (FAILED(Result))
        goto Exit;
    Result = ApplyMapFormat(Format);

    /* Saving needs the map store, which is not there */
    if (SUCCEEDED(Result) && Save)
        Result = DI_SETTINGSNOTSAVED;

Exit:
    DiFreeMapUser(User, WideUser);
    return Result;
}

STDMETHODIMP
DiDevice::SetActionMap(
    _In_ LPDIACTIONFORMATW Format,
    _In_opt_z_ LPCWSTR UserName,
    _In_ DWORD MapFlags)
{
    HRESULT Result;

    Result = DiCheckActionFormat(Format);
    if (FAILED(Result))
        return Result;
    if (Format->dwSize != sizeof(*Format))
        return E_INVALIDARG;
    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return SetMapCore(Format, nullptr, UserName, MapFlags);
}

STDMETHODIMP
DiDevice::AnsiFace::SetActionMap(
    _In_ LPDIACTIONFORMATA Format,
    _In_opt_z_ LPCSTR UserName,
    _In_ DWORD Flags)
{
    DIACTIONFORMATW Wide;
    HRESULT Result;

    Result = DiCheckActionFormat(reinterpret_cast<const DIACTIONFORMATW*>(Format));
    if (FAILED(Result))
        return Result;
    if (Format->dwSize != sizeof(*Format))
        return E_INVALIDARG;
    if (Owner->Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    DiActionHeaderFromAnsi(&Wide, Format);
    Result = Owner->SetMapCore(&Wide, UserName, nullptr, Flags);
    Format->dwCRC = Wide.dwCRC;
    return Result;
}

/* Device images come from the map store too, so only the checks before it can differ */
HRESULT
DiDevice::GetImageInfoCore(
    _Inout_ LPDIDEVICEIMAGEINFOHEADERW Header)
{
    if (Header->dwSize != sizeof(*Header))
        return E_INVALIDARG;
    if (Header->lprgImageInfoArray != nullptr && Header->dwBufferSize < Header->dwSizeImageInfo)
        return E_INVALIDARG;
    if (Header->dwBufferSize != 0 && Header->lprgImageInfoArray == nullptr)
        return E_INVALIDARG;
    if (!HasTypeKey())
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    Header->dwBufferUsed = 0;
    return DI_MAP_STORE_MISSING;
}

STDMETHODIMP
DiDevice::GetImageInfo(
    _Inout_ LPDIDEVICEIMAGEINFOHEADERW Header)
{
    if (Header == nullptr)
        return E_POINTER;
    if (Header->dwSize != sizeof(*Header) || Header->dwSizeImageInfo != sizeof(DIDEVICEIMAGEINFOW))
        return E_INVALIDARG;
    if (Header->lprgImageInfoArray == nullptr && Header->dwBufferSize != 0)
        return E_POINTER;
    return GetImageInfoCore(Header);
}

STDMETHODIMP
DiDevice::AnsiFace::GetImageInfo(
    _Inout_ LPDIDEVICEIMAGEINFOHEADERA Header)
{
    DIDEVICEIMAGEINFOHEADERW Wide;
    ULONG64 WideSize;

    if (Header == nullptr)
        return E_POINTER;
    if (Header->dwSize != sizeof(*Header) || Header->dwSizeImageInfo != sizeof(DIDEVICEIMAGEINFOA))
        return E_INVALIDARG;
    if (Header->lprgImageInfoArray == nullptr && Header->dwBufferSize != 0)
        return E_POINTER;

    /* The wide request would hold as many entries as the ANSI buffer has room for */
    WideSize = static_cast<ULONG64>(Header->dwBufferSize / sizeof(DIDEVICEIMAGEINFOA)) * sizeof(DIDEVICEIMAGEINFOW);
    if (WideSize > MAXDWORD)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    /* Nothing can fill the entries without a map store, so the request carries none */
    ZeroMemory(&Wide, sizeof(Wide));
    Wide.dwSize = sizeof(Wide);
    Wide.dwSizeImageInfo = sizeof(DIDEVICEIMAGEINFOW);
    return Owner->GetImageInfoCore(&Wide);
}
