/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DirectSoundPrivate class and the device configuration property sets it answers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <mmsystem.h>
#include <dsconf.h>

const CLSID DsPrivateClass = { 0x11AB3EC0, 0x25EC, 0x11D1, { 0xA4, 0xD8, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } };

/* The public device set and the private sets that ride along with it */
static const GUID DsDeviceSet = { 0x84624F82, 0x25EC, 0x11D1, { 0xA4, 0xD8, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } };
static const GUID DsMixerSet = { 0x84624F80, 0x25EC, 0x11D1, { 0xA4, 0xD8, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } };
static const GUID DsAccelerationSet = { 0x1AEAA606, 0x35F0, 0x11D1, { 0xB1, 0x61, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } };
static const GUID DsDebugSet = { 0xF2957840, 0x260C, 0x11D1, { 0xA4, 0xD8, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } };
static const GUID DsPersistentSet = { 0x1BE55C3E, 0x36AB, 0x11D1, { 0xB1, 0x62, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } };
static const GUID DsBufferSet = { 0x50393DEA, 0x51AD, 0x11D2, { 0x91, 0xB2, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } };
static const GUID DsObjectSet = { 0x4BE334F0, 0x1C8E, 0x4D91, { 0x8C, 0x17, 0x58, 0x89, 0xD6, 0xF0, 0xB1, 0x54 } };

/* Room the fixed part of an ANSI or wide description takes before its strings */
#define DS_DESCRIPTION_HEAD 56

/* Properties the system answers that this runtime does not carry yet */
static
HRESULT
DsPrivateGetMissing(
    _In_opt_ PVOID Owner,
    _In_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Length);
    DS_TRACE_ONCE(L"a private property was asked for that is not carried");
    return E_NOTIMPL;
}

static
HRESULT
DsPrivateSetMissing(
    _In_opt_ PVOID Owner,
    _In_opt_ PVOID Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Length);
    DS_TRACE_ONCE(L"a private property was written that is not carried");
    return E_NOTIMPL;
}

/* Turns a default or empty device id into the endpoint it names and finds that endpoint's record */
static
HRESULT
DsFindRecord(
    _Inout_ GUID* Device,
    _In_ BOOLEAN Capture,
    _In_reads_(Count) const DsDeviceRecord* Records,
    _In_ ULONG Count,
    _Outptr_ const DsDeviceRecord** Found)
{
    GUID Wanted;

    *Found = nullptr;
    if (*Device == GUID_NULL)
        *Device = Capture ? DSDEVID_DefaultCapture : DSDEVID_DefaultPlayback;

    Wanted = *Device;
    if (*Device == DSDEVID_DefaultPlayback || *Device == DSDEVID_DefaultVoicePlayback || *Device == DSDEVID_DefaultCapture ||
        *Device == DSDEVID_DefaultVoiceCapture)
    {
        if (FAILED(DsResolveDeviceId(Device, &Wanted)))
            return DSERR_NODRIVER;
    }
    for (ULONG Index = 0; Index < Count; Index++)
    {
        if (Records[Index].Id == Wanted)
        {
            *Found = &Records[Index];
            return S_OK;
        }
    }
    return DSERR_NODRIVER;
}

static
const WCHAR*
DsRecordInterface(
    _In_ const DsDeviceRecord* Record)
{
    return Record->Interface != nullptr ? Record->Interface : Record->Module;
}

/* Lays a description out as the head and then its three strings, reporting the room it needs either way */
static
VOID
DsWriteDescriptionW(
    _In_ const DsDeviceRecord* Record,
    _Out_writes_bytes_opt_(*Length) PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA Data,
    _Inout_ ULONG* Length)
{
    const WCHAR* Strings[3] = { Record->Name, Record->Module, DsRecordInterface(Record) };
    ULONG Sizes[3];
    ULONG Needed = DS_DESCRIPTION_HEAD;

    for (UINT Index = 0; Index < 3; Index++)
    {
        Sizes[Index] = static_cast<ULONG>((wcslen(Strings[Index]) + 1) * sizeof(WCHAR));
        Needed += Sizes[Index];
    }

    if (Data != nullptr && *Length >= DS_DESCRIPTION_HEAD)
    {
        Data->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
        Data->DataFlow = Record->Capture ? DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE : DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
        Data->DeviceId = Record->Id;
    }
    if (Data != nullptr && Needed <= *Length)
    {
        BYTE* Next = reinterpret_cast<BYTE*>(Data) + DS_DESCRIPTION_HEAD;
        LPWSTR* Targets[3] = { &Data->Description, &Data->Module, &Data->Interface };

        for (UINT Index = 0; Index < 3; Index++)
        {
            memcpy(Next, Strings[Index], Sizes[Index]);
            *Targets[Index] = reinterpret_cast<LPWSTR>(Next);
            Next += Sizes[Index];
        }
        Data->WaveDeviceId = Record->WaveId;
    }
    *Length = Needed;
}

static
VOID
DsWriteDescriptionA(
    _In_ const DsDeviceRecord* Record,
    _Out_writes_bytes_opt_(*Length) PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA Data,
    _Inout_ ULONG* Length)
{
    const WCHAR* Strings[3] = { Record->Name, Record->Module, DsRecordInterface(Record) };
    ULONG Sizes[3];
    ULONG Needed = DS_DESCRIPTION_HEAD;

    for (UINT Index = 0; Index < 3; Index++)
    {
        Sizes[Index] = static_cast<ULONG>(WideCharToMultiByte(CP_ACP, 0, Strings[Index], -1, nullptr, 0, nullptr, nullptr));
        Needed += Sizes[Index];
    }

    if (Data != nullptr && *Length >= DS_DESCRIPTION_HEAD)
    {
        Data->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
        Data->DataFlow = Record->Capture ? DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE : DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
        Data->DeviceId = Record->Id;
    }
    if (Data != nullptr && Needed <= *Length)
    {
        CHAR* Next = reinterpret_cast<CHAR*>(Data) + DS_DESCRIPTION_HEAD;
        LPSTR* Targets[3] = { &Data->Description, &Data->Module, &Data->Interface };

        for (UINT Index = 0; Index < 3; Index++)
        {
            WideCharToMultiByte(CP_ACP, 0, Strings[Index], -1, Next, static_cast<int>(Sizes[Index]), nullptr, nullptr);
            *Targets[Index] = Next;
            Next += Sizes[Index];
        }
        Data->WaveDeviceId = Record->WaveId;
    }
    *Length = Needed;
}

static
HRESULT
DsGetDescription(
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length,
    _In_ BOOLEAN Wide)
{
    PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA Head = static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA>(Data);
    const DsDeviceRecord* Record;
    DsDeviceRecord* Records;
    ULONG Count;
    HRESULT hr;

    hr = DsListDeviceRecords(&Records, &Count);
    if (FAILED(hr))
        return hr;
    hr = DsFindRecord(&Head->DeviceId, Head->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE, Records, Count, &Record);
    if (SUCCEEDED(hr) && Wide)
        DsWriteDescriptionW(Record, Head, Length);
    else if (SUCCEEDED(hr))
        DsWriteDescriptionA(Record, static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA>(Data), Length);
    DsFreeDeviceRecords(Records, Count);
    return hr;
}

static
HRESULT
DsGetDescriptionW(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    return DsGetDescription(Data, Length, TRUE);
}

static
HRESULT
DsGetDescriptionA(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    return DsGetDescription(Data, Length, FALSE);
}

/* Hands every endpoint's description to the callback until it asks to stop */
static
HRESULT
DsEnumerate(
    _In_ PVOID Data,
    _In_ BOOLEAN Wide)
{
    PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA Request = static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA>(Data);
    DsDeviceRecord* Records;
    ULONG Count;
    HRESULT hr;

    hr = DsListDeviceRecords(&Records, &Count);
    if (FAILED(hr))
        return hr;
    for (ULONG Index = 0; Index < Count; Index++)
    {
        ULONG Size = 0;
        VOID* Description;
        BOOL Continue;

        if (Wide)
            DsWriteDescriptionW(&Records[Index], nullptr, &Size);
        else
            DsWriteDescriptionA(&Records[Index], nullptr, &Size);
        Description = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
        if (Description == nullptr)
        {
            hr = E_OUTOFMEMORY;
            break;
        }
        if (Wide)
        {
            DsWriteDescriptionW(&Records[Index], static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA>(Description), &Size);
            Continue = Request->Callback(static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA>(Description), Request->Context);
        }
        else
        {
            PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A_DATA RequestA = static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A_DATA>(Data);

            DsWriteDescriptionA(&Records[Index], static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA>(Description), &Size);
            Continue = RequestA->Callback(static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA>(Description), RequestA->Context);
        }
        HeapFree(GetProcessHeap(), 0, Description);
        if (!Continue)
            break;
    }
    DsFreeDeviceRecords(Records, Count);
    return hr;
}

static
HRESULT
DsEnumerateW(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    return DsEnumerate(Data, TRUE);
}

static
HRESULT
DsEnumerateA(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    return DsEnumerate(Data, FALSE);
}

/* Finds the endpoint behind a waveOut or waveIn device name */
static
HRESULT
DsMapWaveDevice(
    _In_z_ const WCHAR* Name,
    _In_ DIRECTSOUNDDEVICE_DATAFLOW Flow,
    _Out_ GUID* Device)
{
    BOOLEAN Capture = Flow == DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE;
    DsDeviceRecord* Records;
    ULONG Count;
    UINT Total;
    UINT Wave;
    HRESULT hr;

    if (Flow != DIRECTSOUNDDEVICE_DATAFLOW_RENDER && Flow != DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE)
        return E_INVALIDARG;

    Total = Capture ? waveInGetNumDevs() : waveOutGetNumDevs();
    for (Wave = 0; Wave < Total; Wave++)
    {
        WAVEINCAPSW InCaps;
        WAVEOUTCAPSW OutCaps;
        const WCHAR* Product;

        if (Capture)
        {
            if (waveInGetDevCapsW(Wave, &InCaps, sizeof(InCaps)) != MMSYSERR_NOERROR)
                continue;
            Product = InCaps.szPname;
        }
        else
        {
            if (waveOutGetDevCapsW(Wave, &OutCaps, sizeof(OutCaps)) != MMSYSERR_NOERROR)
                continue;
            Product = OutCaps.szPname;
        }
        if (lstrcmpW(Product, Name) == 0)
            break;
    }
    if (Wave >= Total)
        return DSERR_NODRIVER;

    hr = DsListDeviceRecords(&Records, &Count);
    if (FAILED(hr))
        return hr;
    hr = DSERR_NODRIVER;
    for (ULONG Index = 0; Index < Count; Index++)
    {
        if (Records[Index].Capture == Capture && Records[Index].WaveId == Wave)
        {
            *Device = Records[Index].Id;
            hr = S_OK;
            break;
        }
    }
    DsFreeDeviceRecords(Records, Count);
    return hr;
}

static
HRESULT
DsGetMappingW(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    PDSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA Mapping = static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA>(Data);

    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    if (Mapping->DeviceName == nullptr)
        return E_OUTOFMEMORY;
    return DsMapWaveDevice(Mapping->DeviceName, Mapping->DataFlow, &Mapping->DeviceId);
}

static
HRESULT
DsGetMappingA(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    PDSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA Mapping = static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA>(Data);
    WCHAR* Name;
    int Size;
    HRESULT hr;

    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    if (Mapping->DeviceName == nullptr)
        return E_OUTOFMEMORY;
    Size = MultiByteToWideChar(CP_ACP, 0, Mapping->DeviceName, -1, nullptr, 0);
    Name = static_cast<WCHAR*>(HeapAlloc(GetProcessHeap(), 0, Size * sizeof(*Name)));
    if (Name == nullptr)
        return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_ACP, 0, Mapping->DeviceName, -1, Name, Size);
    hr = DsMapWaveDevice(Name, Mapping->DataFlow, &Mapping->DeviceId);
    HeapFree(GetProcessHeap(), 0, Name);
    return hr;
}

static const DsPropertyItem DsMixerItems[] =
{
    { 0, DsPrivateGetMissing, DsPrivateSetMissing, 20 },
    { 1, DsPrivateGetMissing, DsPrivateSetMissing, 20 },
};

static const DsPropertyItem DsDeviceItems[] =
{
    { 0, DsPrivateGetMissing, DsPrivateSetMissing, 20 },
    { DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A, DsGetMappingA, nullptr, 32 },
    { DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1, DsPrivateGetMissing, nullptr, sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA) },
    { DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1, DsPrivateGetMissing, nullptr, 16 },
    { DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W, DsGetMappingW, nullptr, 32 },
    { DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A, DsGetDescriptionA, nullptr, DS_DESCRIPTION_HEAD },
    { DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W, DsGetDescriptionW, nullptr, DS_DESCRIPTION_HEAD },
    { DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A, DsEnumerateA, nullptr, 16 },
    { DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W, DsEnumerateW, nullptr, 16 },
    { 9, DsPrivateGetMissing, DsPrivateSetMissing, 24 },
    { 10, DsGetMappingA, nullptr, 32 },
    { 11, DsGetMappingW, nullptr, 32 },
};

static const DsPropertyItem DsAccelerationItems[] =
{
    { 0, DsPrivateGetMissing, DsPrivateSetMissing, 20 },
    { 1, DsPrivateGetMissing, nullptr, 20 },
};

static const DsPropertyItem DsDebugItems[] =
{
    { 0, DsPrivateGetMissing, DsPrivateSetMissing, 272 },
    { 1, DsPrivateGetMissing, DsPrivateSetMissing, 532 },
    { 2, DsPrivateGetMissing, nullptr, 24 },
    { 3, DsPrivateGetMissing, nullptr, 24 },
};

static const DsPropertyItem DsPersistentItems[] =
{
    { 0, DsPrivateGetMissing, DsPrivateSetMissing, 56 },
    { 1, DsPrivateGetMissing, DsPrivateSetMissing, 56 },
};

static const DsPropertyItem DsBufferItems[] =
{
    { 0, DsPrivateGetMissing, nullptr, 24 },
};

static const DsPropertyItem DsObjectItems[] =
{
    { 0, DsPrivateGetMissing, nullptr, 24 },
    { 1, DsPrivateGetMissing, nullptr, 24 },
};

static const DsPropertyTable DsPrivateTables[] =
{
    { &DsMixerSet, ARRAYSIZE(DsMixerItems), DsMixerItems },
    { &DsDeviceSet, ARRAYSIZE(DsDeviceItems), DsDeviceItems },
    { &DsAccelerationSet, ARRAYSIZE(DsAccelerationItems), DsAccelerationItems },
    { &DsDebugSet, ARRAYSIZE(DsDebugItems), DsDebugItems },
    { &DsPersistentSet, ARRAYSIZE(DsPersistentItems), DsPersistentItems },
    { &DsBufferSet, ARRAYSIZE(DsBufferItems), DsBufferItems },
    { &DsObjectSet, ARRAYSIZE(DsObjectItems), DsObjectItems },
};

class DsPrivate;

class DsPrivateFace final : public IKsPropertySet
{
public:
    DsFaceLink Link;
    DsPrivate* Self;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    Get(
        _In_ REFGUID Set,
        ULONG Id,
        _In_reads_bytes_opt_(InstanceLength) LPVOID Instance,
        ULONG InstanceLength,
        _Out_writes_bytes_(DataLength) LPVOID Data,
        ULONG DataLength,
        _Out_opt_ PULONG Returned) override
    {
        DsApiGuard Guard;

        UNREFERENCED_PARAMETER(Instance);
        UNREFERENCED_PARAMETER(InstanceLength);
        return DsPropertyGet(DsPrivateTables, ARRAYSIZE(DsPrivateTables), Self, &Set, Id, Data, DataLength, Returned);
    }

    HRESULT
    STDMETHODCALLTYPE
    Set(
        _In_ REFGUID Set,
        ULONG Id,
        _In_reads_bytes_opt_(InstanceLength) LPVOID Instance,
        ULONG InstanceLength,
        _In_reads_bytes_(DataLength) LPVOID Data,
        ULONG DataLength) override
    {
        DsApiGuard Guard;

        UNREFERENCED_PARAMETER(Instance);
        UNREFERENCED_PARAMETER(InstanceLength);
        return DsPropertySet(DsPrivateTables, ARRAYSIZE(DsPrivateTables), Self, &Set, Id, Data, DataLength);
    }

    HRESULT
    STDMETHODCALLTYPE
    QuerySupport(
        _In_ REFGUID Set,
        ULONG Id,
        _Out_ PULONG Support) override
    {
        DsApiGuard Guard;

        return DsPropertyQuery(DsPrivateTables, ARRAYSIZE(DsPrivateTables), &Set, Id, Support);
    }
};

class DsPrivate final : public DsObject
{
public:
    DsPrivate()
    {
        Face.Link.Object = this;
        Face.Link.Count = 0;
        Face.Self = this;
        Expose(IID_IKsPropertySet, &Face, &Face.Link);
    }

    DsPrivateFace Face;
};

HRESULT
DsCreatePrivate(
    _In_ REFIID Iid,
    _COM_Outptr_ void** Object)
{
    DsPrivate* Private = new (std::nothrow) DsPrivate();
    HRESULT hr;

    *Object = nullptr;
    if (Private == nullptr)
        return E_OUTOFMEMORY;
    hr = Private->Query(Iid, TRUE, Object);
    if (FAILED(hr))
        delete Private;
    return hr;
}
