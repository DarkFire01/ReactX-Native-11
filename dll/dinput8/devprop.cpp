/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device properties: validation, the generic answers and what devices override
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Where a property may be used */
#define DI_PF_OBJECT_ONLY       0x01
#define DI_PF_DEVICE_ONLY       0x02
#define DI_PF_READ_ONLY         0x04
#define DI_PF_NEEDS_EMULATION   0x08
#define DI_PF_NOT_ACQUIRED      0x10

/* Internal properties */
#define DI_PROP_MAX_BUFFER      0xFFFF
#define DI_PROP_TYPE_KEY        0xFFFD
#define DI_PROP_ENABLE_OBJECT   0xFFFB

/* DIPROPCAL grew a second layout later */
#define DI_PROP_CAL_LONG_SIZE   56

struct DI_PROPERTY_RULE
{
    DWORD Id;
    DWORD Size;
    DWORD Flags;
};

static const DI_PROPERTY_RULE DiPropertyRules[] =
{
    { 1, sizeof(DIPROPDWORD), DI_PF_DEVICE_ONLY | DI_PF_NEEDS_EMULATION | DI_PF_NOT_ACQUIRED },
    { 2, sizeof(DIPROPDWORD), DI_PF_DEVICE_ONLY | DI_PF_NEEDS_EMULATION | DI_PF_NOT_ACQUIRED },
    { 3, sizeof(DIPROPDWORD), DI_PF_OBJECT_ONLY | DI_PF_READ_ONLY | DI_PF_NOT_ACQUIRED },
    { 4, sizeof(DIPROPRANGE), DI_PF_OBJECT_ONLY | DI_PF_NOT_ACQUIRED },
    { 5, sizeof(DIPROPDWORD), DI_PF_NOT_ACQUIRED },
    { 6, sizeof(DIPROPDWORD), DI_PF_NOT_ACQUIRED },
    { 7, sizeof(DIPROPDWORD), DI_PF_DEVICE_ONLY },
    { 8, sizeof(DIPROPDWORD), DI_PF_DEVICE_ONLY | DI_PF_READ_ONLY },
    { 9, sizeof(DIPROPDWORD), DI_PF_DEVICE_ONLY | DI_PF_NOT_ACQUIRED },
    { 10, sizeof(DIPROPDWORD), DI_PF_DEVICE_ONLY | DI_PF_NOT_ACQUIRED },
    { 11, sizeof(DIPROPCAL), DI_PF_OBJECT_ONLY | DI_PF_NOT_ACQUIRED },
    { 12, sizeof(DIPROPGUIDANDPATH), DI_PF_DEVICE_ONLY | DI_PF_READ_ONLY },
    { 13, sizeof(DIPROPSTRING), DI_PF_DEVICE_ONLY },
    { 14, sizeof(DIPROPSTRING), DI_PF_DEVICE_ONLY },
    { DI_PROP_MAX_BUFFER, sizeof(DIPROPDWORD), DI_PF_DEVICE_ONLY | DI_PF_NEEDS_EMULATION | DI_PF_NOT_ACQUIRED },
    { 15, sizeof(DIPROPDWORD), DI_PF_DEVICE_ONLY | DI_PF_NOT_ACQUIRED },
    { 16, sizeof(DIPROPSTRING), DI_PF_DEVICE_ONLY | DI_PF_READ_ONLY },
    { DI_PROP_ENABLE_OBJECT, sizeof(DIPROPDWORD), 0 },
    { 18, sizeof(DIPROPRANGE), DI_PF_OBJECT_ONLY | DI_PF_NOT_ACQUIRED },
    { 19, sizeof(DIPROPRANGE), DI_PF_OBJECT_ONLY | DI_PF_NOT_ACQUIRED },
    { 20, sizeof(DIPROPSTRING), DI_PF_OBJECT_ONLY | DI_PF_NOT_ACQUIRED },
    { 23, sizeof(DIPROPDWORD), DI_PF_OBJECT_ONLY | DI_PF_NOT_ACQUIRED },
    { 22, sizeof(DIPROPPOINTER), DI_PF_OBJECT_ONLY | DI_PF_NOT_ACQUIRED },
    { 21, sizeof(DIPROPCPOINTS), DI_PF_OBJECT_ONLY | DI_PF_NOT_ACQUIRED },
    { 24, sizeof(DIPROPDWORD), DI_PF_DEVICE_ONLY | DI_PF_READ_ONLY },
    { 25, sizeof(DIPROPSTRING), DI_PF_DEVICE_ONLY | DI_PF_READ_ONLY },
    { 26, sizeof(DIPROPSTRING), DI_PF_DEVICE_ONLY | DI_PF_READ_ONLY },
    { DI_PROP_TYPE_KEY, sizeof(DIPROPSTRING), DI_PF_DEVICE_ONLY | DI_PF_READ_ONLY },
};

static
const DI_PROPERTY_RULE*
DiFindPropertyRule(
    _In_ DWORD Id)
{
    for (const auto& Rule : DiPropertyRules)
    {
        if (Rule.Id == Id)
            return &Rule;
    }
    return nullptr;
}

/* Predefined properties are small integers in place of a GUID pointer */
static
BOOLEAN
DiIsPredefined(
    _In_ REFGUID Property)
{
    return (reinterpret_cast<ULONG_PTR>(&Property) & ~static_cast<ULONG_PTR>(0xFFFF)) == 0;
}

static
DWORD
DiPredefinedId(
    _In_ REFGUID Property)
{
    return static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(&Property));
}

HRESULT
DiDevice::CheckProperty(
    _In_ REFGUID Property,
    _In_ LPCDIPROPHEADER Header,
    _Out_ DI_PROPERTY* Resolved)
{
    const DI_PROPERTY_RULE* Rule;
    HRESULT Result;

    Resolved->Id = 0;
    Resolved->Index = static_cast<DWORD>(-1);
    Resolved->Type = 0;
    if (Header == nullptr || Header->dwHeaderSize != sizeof(DIPROPHEADER) || (Header->dwSize & 3) != 0 ||
        Header->dwSize < sizeof(DIPROPHEADER))
    {
        return E_INVALIDARG;
    }
    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;

    if (Header->dwHow == DIPH_DEVICE)
    {
        if (Header->dwObj != 0)
            return E_INVALIDARG;
    }
    else
    {
        Result = ResolveObject(Header->dwObj, Header->dwHow, &Resolved->Index);
        if (FAILED(Result))
            return Result;
        Resolved->Type = Backend->Format()->Objects[Resolved->Index].Type;
    }

    if (!DiIsPredefined(Property))
        return S_OK;
    Resolved->Id = DiPredefinedId(Property);
    Rule = DiFindPropertyRule(Resolved->Id);
    if (Rule == nullptr)
        return E_NOTIMPL;
    if (Rule->Id == 11)
        return Header->dwSize == Rule->Size || Header->dwSize == DI_PROP_CAL_LONG_SIZE ? S_OK : E_INVALIDARG;
    if (Header->dwSize != Rule->Size)
        return E_INVALIDARG;
    if ((Rule->Id == 20 || Rule->Id == 23) && Header->dwHow == DIPH_DEVICE)
        return E_INVALIDARG;
    return S_OK;
}

/* Which restriction of a rule a request runs into, as an error, or S_OK */
static
HRESULT
DiCheckRule(
    _In_ const DI_PROPERTY_RULE* Rule,
    _In_ const DI_PROPERTY* Property,
    _In_ BOOLEAN Writing,
    _In_ BOOLEAN HasEmulation,
    _In_ BOOLEAN Acquired)
{
    DWORD Blocked = Property->Index == static_cast<DWORD>(-1) ? DI_PF_OBJECT_ONLY : DI_PF_DEVICE_ONLY;

    if (Writing)
        Blocked |= DI_PF_READ_ONLY;
    if (!HasEmulation)
        Blocked |= DI_PF_NEEDS_EMULATION;
    if (Writing && Acquired)
        Blocked |= DI_PF_NOT_ACQUIRED;
    Blocked &= Rule->Flags;
    if (Blocked == 0)
        return S_OK;
    if (Blocked & DI_PF_READ_ONLY)
        return DIERR_READONLY;
    if (Blocked & DI_PF_NOT_ACQUIRED)
        return DIERR_ACQUIRED;
    return E_NOTIMPL;
}

HRESULT
DiDevice::DefaultGetProperty(
    _In_ const DI_PROPERTY* Property,
    _Inout_ LPDIPROPHEADER Header)
{
    const DI_PROPERTY_RULE* Rule = DiFindPropertyRule(Property->Id);
    LPDIPROPDWORD Value = reinterpret_cast<LPDIPROPDWORD>(Header);
    HRESULT Result;

    if (Rule == nullptr)
        return E_NOTIMPL;
    Result = DiCheckRule(Rule, Property, FALSE, Emulation != nullptr, (Flags & DI_DEV_ACQUIRED) != 0);
    if (FAILED(Result))
        return Result;

    switch (Property->Id)
    {
        case 1:
            Value->dwData = BufferSize;
            return DI_OK;
        case 2:
            Value->dwData = Relative ? DIPROPAXISMODE_REL : DIPROPAXISMODE_ABS;
            return DI_OK;
        case 3:
            if (Property->Type & DIDFT_AXIS)
            {
                Value->dwData = 1;
                return DI_OK;
            }
            break;
        case 4:
            if (Property->Type & DIDFT_RELAXIS)
            {
                LPDIPROPRANGE Range = reinterpret_cast<LPDIPROPRANGE>(Header);

                Range->lMin = DIPROPRANGE_NOMIN;
                Range->lMax = DIPROPRANGE_NOMAX;
                return DI_OK;
            }
            break;
        case 7:
            Value->dwData = Gain;
            return DI_OK;
        case 9:
            if (CapsFlags & DIDC_FORCEFEEDBACK)
            {
                Value->dwData = AutoCenter;
                return DI_OK;
            }
            break;
        case 22:
            if ((Property->Type & 0x1F) != 0 && !(Property->Type & DIDFT_NODATA))
            {
                LPDIPROPPOINTER Pointer = reinterpret_cast<LPDIPROPPOINTER>(Header);

                if (AppObjects == nullptr)
                    return DIERR_NOTINITIALIZED;
                if (AppObjects[Property->Index].Offset == -1)
                    return DIERR_OBJECTNOTFOUND;
                Pointer->uData = AppObjects[Property->Index].AppData;
                return DI_OK;
            }
            break;
        case 25:
            reinterpret_cast<LPDIPROPSTRING>(Header)->wsz[0] = L'\0';
            return S_FALSE;
        case DI_PROP_MAX_BUFFER:
            Value->dwData = MaxBufferSize;
            return DI_OK;
        default:
            break;
    }
    return E_NOTIMPL;
}

HRESULT
DiDevice::DefaultSetProperty(
    _In_ const DI_PROPERTY* Property,
    _In_ LPCDIPROPHEADER Header)
{
    const DI_PROPERTY_RULE* Rule = DiFindPropertyRule(Property->Id);
    DWORD Data = reinterpret_cast<LPCDIPROPDWORD>(Header)->dwData;
    HRESULT Result;

    if (Rule == nullptr)
        return E_NOTIMPL;
    Result = DiCheckRule(Rule, Property, TRUE, Emulation != nullptr, (Flags & DI_DEV_ACQUIRED) != 0);
    if (FAILED(Result))
        return Result;

    switch (Property->Id)
    {
        case 1:
        {
            DWORD Wanted = Data > MaxBufferSize ? MaxBufferSize : Data;

            Result = DiEmSetBufferSize(Emulation, Wanted);
            if (FAILED(Result))
                return Result;
            BufferSize = Data;
            return PolledResult;
        }
        case 2:
            return SetAxisMode(Data);
        case 4:
        case 5:
        case 6:
        case 10:
        case 11:
            if (Property->Type != 0)
                return E_NOTIMPL;
            {
                const DI_FORMAT* Native = Backend->Format();
                DI_PROPERTY Axis = *Property;

                /* A device wide setting goes to each absolute axis in turn */
                for (DWORD Index = 0; Index < Native->ObjectCount; Index++)
                {
                    if (!(Native->Objects[Index].Type & DIDFT_ABSAXIS))
                        continue;
                    Axis.Index = Index;
                    Axis.Type = Native->Objects[Index].Type;
                    Result = Backend->SetProperty(&Axis, Header);
                    if (FAILED(Result) && Result != E_NOTIMPL)
                        return Result;
                }
                return DI_OK;
            }
        case 7:
            if (Data & 0xFFFF0000)
                return E_INVALIDARG;
            Gain = Data;
            return DI_OK;
        case 9:
            if (Data & ~1u)
                return E_INVALIDARG;
            if (!(CapsFlags & DIDC_FORCEFEEDBACK))
                return E_NOTIMPL;
            AutoCenter = Data;
            return DI_OK;
        case 22:
            if ((Property->Type & 0x1F) != 0 && !(Property->Type & DIDFT_NODATA))
            {
                if (AppObjects == nullptr)
                    return DIERR_NOTINITIALIZED;
                if (AppObjects[Property->Index].Offset == -1)
                    return DIERR_OBJECTNOTFOUND;
                AppObjects[Property->Index].AppData = reinterpret_cast<LPCDIPROPPOINTER>(Header)->uData;
                return DI_OK;
            }
            break;
        case DI_PROP_MAX_BUFFER:
            MaxBufferSize = Data;
            return DI_OK;
        default:
            break;
    }
    return E_NOTIMPL;
}

STDMETHODIMP
DiDevice::GetProperty(
    _In_ REFGUID Property,
    _Inout_ LPDIPROPHEADER Header)
{
    DI_PROPERTY Resolved;
    HRESULT Result;

    EnterCriticalSection(&Lock);
    Result = CheckProperty(Property, Header, &Resolved);
    if (SUCCEEDED(Result))
    {
        Result = Backend->GetProperty(&Resolved, Header);
        if (Result == E_NOTIMPL)
            Result = DefaultGetProperty(&Resolved, Header);
    }
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::SetProperty(
    _In_ REFGUID Property,
    _In_ LPCDIPROPHEADER Header)
{
    DI_PROPERTY Resolved;
    HRESULT Result;

    EnterCriticalSection(&Lock);
    Result = CheckProperty(Property, Header, &Resolved);
    if (SUCCEEDED(Result))
    {
        Result = Backend->SetProperty(&Resolved, Header);
        if (Result == E_NOTIMPL)
            Result = DefaultSetProperty(&Resolved, Header);
    }
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::AnsiFace::GetProperty(
    _In_ REFGUID Property,
    _Inout_ LPDIPROPHEADER Header)
{
    return Owner->GetProperty(Property, Header);
}

STDMETHODIMP
DiDevice::AnsiFace::SetProperty(
    _In_ REFGUID Property,
    _In_ LPCDIPROPHEADER Header)
{
    return Owner->SetProperty(Property, Header);
}
