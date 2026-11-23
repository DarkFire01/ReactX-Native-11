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

/* Copies a wide string into a fixed field of the old style description, cut to fit */
static
VOID
DsCopyField1(
    _Out_writes_(Room) WCHAR* Target,
    _In_ ULONG Room,
    _In_z_ const WCHAR* Source)
{
    ULONG Length = static_cast<ULONG>(wcslen(Source));

    if (Length >= Room)
        Length = Room - 1;
    memcpy(Target, Source, Length * sizeof(*Target));
    Target[Length] = L'\0';
}

static
VOID
DsCopyField1A(
    _Out_writes_(Room) CHAR* Target,
    _In_ ULONG Room,
    _In_z_ const WCHAR* Source)
{
    int Size = WideCharToMultiByte(CP_ACP, 0, Source, -1, nullptr, 0, nullptr, nullptr);
    CHAR* Narrow;
    ULONG Length;

    Target[0] = '\0';
    if (Size <= 0)
        return;
    Narrow = static_cast<CHAR*>(HeapAlloc(GetProcessHeap(), 0, Size));
    if (Narrow == nullptr)
        return;
    WideCharToMultiByte(CP_ACP, 0, Source, -1, Narrow, Size, nullptr, nullptr);
    Length = static_cast<ULONG>(strlen(Narrow));
    if (Length >= Room)
        Length = Room - 1;
    memcpy(Target, Narrow, Length);
    Target[Length] = '\0';
    HeapFree(GetProcessHeap(), 0, Narrow);
}

/* Fills the old style description, which carries both string widths in fixed fields */
static
VOID
DsWriteDescription1(
    _In_ const DsDeviceRecord* Record,
    _Out_ PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA Data)
{
    Data->Type = DIRECTSOUNDDEVICE_TYPE_WDM;
    Data->DataFlow = Record->Capture ? DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE : DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
    Data->DeviceId = Record->Id;
    DsCopyField1A(Data->DescriptionA, ARRAYSIZE(Data->DescriptionA), Record->Name);
    DsCopyField1(Data->DescriptionW, ARRAYSIZE(Data->DescriptionW), Record->Name);
    DsCopyField1A(Data->ModuleA, ARRAYSIZE(Data->ModuleA), Record->Module);
    DsCopyField1(Data->ModuleW, ARRAYSIZE(Data->ModuleW), Record->Module);
    Data->WaveDeviceId = Record->WaveId;
    Data->Devnode = 0;
}

static
HRESULT
DsGetDescription1(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA Description = static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA>(Data);
    const DsDeviceRecord* Record;
    DsDeviceRecord* Records;
    ULONG Count;
    HRESULT hr;

    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    hr = DsListDeviceRecords(&Records, &Count);
    if (FAILED(hr))
        return hr;
    hr = DsFindRecord(&Description->DeviceId, Description->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE, Records, Count, &Record);
    if (SUCCEEDED(hr))
        DsWriteDescription1(Record, Description);
    DsFreeDeviceRecords(Records, Count);
    return hr;
}

/* One description block serves every callback, rewritten for each endpoint */
static
HRESULT
DsEnumerate1(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1_DATA Request = static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1_DATA>(Data);
    PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA Description;
    DsDeviceRecord* Records;
    ULONG Count;
    HRESULT hr;

    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    Description = static_cast<PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Description)));
    if (Description == nullptr)
        return E_OUTOFMEMORY;
    hr = DsListDeviceRecords(&Records, &Count);
    if (SUCCEEDED(hr))
    {
        for (ULONG Index = 0; Index < Count; Index++)
        {
            DsWriteDescription1(&Records[Index], Description);
            if (!Request->Callback(Description, Request->Context))
                break;
        }
        DsFreeDeviceRecords(Records, Count);
    }
    HeapFree(GetProcessHeap(), 0, Description);
    return hr;
}

/* The device id leads every presence, mixer and acceleration property, and the value follows at the given offset */
static
VOID
DsWriteValue(
    _Out_ PVOID Data,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    *reinterpret_cast<ULONG*>(static_cast<BYTE*>(Data) + Offset) = Value;
}

/* Values the system keeps for the process only, changing nothing in the mix */
static ULONG DsSrcQuality = 3;
static ULONG DsMixerAcceleration = 15;
static ULONG DsAccelerationLevel = 1;

static
HRESULT
DsGetPresent(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    DsWriteValue(Data, sizeof(GUID), TRUE);
    return S_OK;
}

static
HRESULT
DsGetPresentFlow(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    DsWriteValue(Data, sizeof(GUID) + sizeof(ULONG), TRUE);
    return S_OK;
}

/* Writes the system takes and drops */
static
HRESULT
DsSetIgnored(
    _In_opt_ PVOID Owner,
    _In_opt_ PVOID Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Length);
    return S_OK;
}

static
HRESULT
DsGetSrcQuality(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    DsWriteValue(Data, sizeof(GUID), DsSrcQuality);
    return S_OK;
}

static
HRESULT
DsSetSrcQuality(
    _In_opt_ PVOID Owner,
    _In_opt_ PVOID Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    DsSrcQuality = *reinterpret_cast<const ULONG*>(static_cast<const BYTE*>(Data) + sizeof(GUID));
    return S_OK;
}

static
HRESULT
DsGetMixerAcceleration(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    DsWriteValue(Data, sizeof(GUID), DsMixerAcceleration);
    return S_OK;
}

static
HRESULT
DsSetMixerAcceleration(
    _In_opt_ PVOID Owner,
    _In_opt_ PVOID Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    DsMixerAcceleration = *reinterpret_cast<const ULONG*>(static_cast<const BYTE*>(Data) + sizeof(GUID));
    return S_OK;
}

static
HRESULT
DsGetAcceleration(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    DsWriteValue(Data, sizeof(GUID), DsAccelerationLevel);
    return S_OK;
}

static
HRESULT
DsSetAcceleration(
    _In_opt_ PVOID Owner,
    _In_opt_ PVOID Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    DsAccelerationLevel = *reinterpret_cast<const ULONG*>(static_cast<const BYTE*>(Data) + sizeof(GUID));
    return S_OK;
}

/* No debug output is kept, so the level, the flags and the module name come back empty */
static
HRESULT
DsGetDebugInfoA(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    ZeroMemory(Data, 3 * sizeof(ULONG) + sizeof(CHAR));
    return S_OK;
}

static
HRESULT
DsGetDebugInfoW(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    ZeroMemory(Data, 3 * sizeof(ULONG) + sizeof(WCHAR));
    return S_OK;
}

struct DsCodeText
{
    ULONG Code;
    const WCHAR* Name;
    const WCHAR* Text;
};

static const DsCodeText DsCodeTexts[] =
{
    { 0x00000000, L"DS_OK", L"The function completed successfully" },
    { 0x00000001, L"S_FALSE", L"Special success code" },
    { 0x0878000A, L"DS_NO_VIRTUALIZATION", L"The function succeeded, substituting the Pan3D algorithm" },
    { 0x80004001, L"DSERR_UNSUPPORTED", L"The function called is not supported at this time" },
    { 0x80004002, L"DSERR_NOINTERFACE", L"The requested COM interface is not available" },
    { 0x80004005, L"DSERR_GENERIC", L"An undetermined error occurred inside the DirectSound subsystem" },
    { 0x80040110, L"DSERR_NOAGGREGATION", L"This object does not support aggregation" },
    { 0x80040154, L"REGDB_E_CLASSNOTREG", L"Class not registered" },
    { 0x80040205, L"DMO_E_TYPE_NOT_ACCEPTED", L"Wave format not supported by effect" },
    { 0x80070005, L"DSERR_ACCESSDENIED", L"Access is denied" },
    { 0x8007000E, L"DSERR_OUTOFMEMORY", L"Not enough free memory is available to complete the operation" },
    { 0x80070057, L"DSERR_INVALIDPARAM", L"An invalid parameter was passed to the returning function" },
    { 0x8878000A, L"DSERR_ALLOCATED",
      L"The call failed because resources (such as a priority level) were already being used by another caller" },
    { 0x8878001E, L"DSERR_CONTROLUNAVAIL", L"The control (vol, pan, etc.) requested by the caller is not available" },
    { 0x88780032, L"DSERR_INVALIDCALL", L"This call is not valid for the current state of this object" },
    { 0x88780046, L"DSERR_PRIOLEVELNEEDED", L"The caller does not have the priority level required for the function to succeed" },
    { 0x88780064, L"DSERR_BADFORMAT", L"The specified WAVE format is not supported" },
    { 0x88780078, L"DSERR_NODRIVER", L"No sound device is available for use, or the given device ID isn't valid" },
    { 0x88780082, L"DSERR_ALREADYINITIALIZED", L"This object is already initialized" },
    { 0x88780096, L"DSERR_BUFFERLOST", L"The buffer memory has been lost, and must be restored" },
    { 0x887800A0, L"DSERR_OTHERAPPHASPRIO", L"Another app has a higher priority level, preventing this call from succeeding" },
    { 0x887800AA, L"DSERR_UNINITIALIZED", L"This object has not been initialized" },
    { 0x887800BE, L"DSERR_DS8_REQUIRED", L"Attempt to use DirectSound 8 functionality on an older DirectSound object" },
    { 0x887800C8, L"DSERR_SENDLOOP", L"A circular loop of send effects was detected" },
    { 0x887800D2, L"DSERR_BADSENDBUFFERGUID", L"The GUID specified does not match a valid MIXIN buffer" },
    { 0x88781161, L"DSERR_OBJECTNOTFOUND", L"The object was not found" },
    { 0x887D0003, L"APOERR_FORMAT_NOT_SUPPORTED", L"A pin supporting the format cannot be found" },
    { 0x88880007, L"LEAP_E_INVALID_FORMAT", L"An invalid or unsupported audio format was used" },
    { 0x88880101, L"LEAP_E_NOT_INITIALIZED", L"LEAP wasn't successfully initialized" },
    { 0x88880302, L"LEAP_E_RENDERER_INVALIDATED", L"Audio device was removed or output format was changed" },
    { 0x88880304, L"LEAP_E_INTERNAL_ERROR", L"An internal error such as memory corruption was detected" },
    { 0x88890004, L"AUDCLNT_E_DEVICE_INVALIDATED", L"Output device is no longer available" },
    { 0x88890008, L"AUDCLNT_E_UNSUPPORTED_FORMAT", L"Audio format not supported by the device" },
    { 0x8889000A, L"AUDCLNT_E_DEVICE_IN_USE", L"Could not connect to the audio driver" },
    { 0x8889000F, L"AUDCLNT_E_ENDPOINT_CREATE_FAILED", L"Could not create an audio endpoint" },
    { 0x88890010, L"AUDCLNT_E_SERVICE_NOT_RUNNING", L"Windows Audio service is not running" },
};

/* The code, then pointers to its name and description, which follow as strings when they fit */
struct DsCodeHead
{
    HRESULT Code;
    PVOID Name;
    PVOID Text;
};

static
HRESULT
DsTranslate(
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length,
    _In_ BOOLEAN Wide)
{
    DsCodeHead* Head = static_cast<DsCodeHead*>(Data);
    const WCHAR* Strings[2] = { nullptr, L"Unknown error" };
    WCHAR Unknown[16];
    ULONG Sizes[2];
    ULONG Needed = sizeof(*Head);

    for (ULONG Index = 0; Index < ARRAYSIZE(DsCodeTexts); Index++)
    {
        if (DsCodeTexts[Index].Code == static_cast<ULONG>(Head->Code))
        {
            Strings[0] = DsCodeTexts[Index].Name;
            Strings[1] = DsCodeTexts[Index].Text;
            break;
        }
    }
    if (Strings[0] == nullptr)
    {
        swprintf_s(Unknown, L"0x%8.8lX", static_cast<ULONG>(Head->Code));
        Strings[0] = Unknown;
    }

    for (ULONG Index = 0; Index < 2; Index++)
    {
        Sizes[Index] = Wide ? static_cast<ULONG>(wcslen(Strings[Index]) + 1) * sizeof(WCHAR) :
                              static_cast<ULONG>(WideCharToMultiByte(CP_ACP, 0, Strings[Index], -1, nullptr, 0, nullptr, nullptr));
        Needed += Sizes[Index];
    }
    if (*Length >= Needed)
    {
        BYTE* Next = reinterpret_cast<BYTE*>(Head + 1);
        PVOID* Targets[2] = { &Head->Name, &Head->Text };

        for (ULONG Index = 0; Index < 2; Index++)
        {
            *Targets[Index] = Next;
            if (Wide)
                memcpy(Next, Strings[Index], Sizes[Index]);
            else
                WideCharToMultiByte(CP_ACP, 0, Strings[Index], -1, reinterpret_cast<CHAR*>(Next), static_cast<int>(Sizes[Index]), nullptr, nullptr);
            Next += Sizes[Index];
        }
    }
    *Length = Needed;
    return S_OK;
}

static
HRESULT
DsTranslateA(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    return DsTranslate(Data, Length, FALSE);
}

static
HRESULT
DsTranslateW(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    UNREFERENCED_PARAMETER(Owner);
    return DsTranslate(Data, Length, TRUE);
}

/* A buffer interface pointer in, the device it plays on out */
struct DsBufferDeviceData
{
    PVOID Buffer;
    GUID DeviceId;
};

static
HRESULT
DsGetBufferDevice(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    DsBufferDeviceData* Request = static_cast<DsBufferDeviceData*>(Data);
    GUID Device;

    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Length);
    if (!DsFindBufferDevice(Request->Buffer, &Device))
        return E_INVALIDARG;
    Request->DeviceId = Device;
    return S_OK;
}

/* A device filter in, then a count and one entry per object the room allows */
struct DsObjectEntry
{
    PVOID Object;
    GUID DeviceId;
};

struct DsObjectListData
{
    GUID DeviceId;
    ULONG Count;
    DsObjectEntry Entries[1];
};

struct DsObjectWalk
{
    DsObjectListData* List;
    ULONG Room;
};

static
VOID
DsAddObject(
    _In_ PVOID Context,
    _In_ PVOID Face,
    _In_ const GUID* Device)
{
    DsObjectWalk* Walk = static_cast<DsObjectWalk*>(Context);
    DsObjectListData* List = Walk->List;

    if (List->DeviceId != GUID_NULL && List->DeviceId != *Device)
        return;
    if (FIELD_OFFSET(DsObjectListData, Entries) + (List->Count + 1) * sizeof(DsObjectEntry) <= Walk->Room)
    {
        List->Entries[List->Count].Object = Face;
        List->Entries[List->Count].DeviceId = *Device;
    }
    List->Count += 1;
}

static
HRESULT
DsGetObjects(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    DsObjectWalk Walk = { static_cast<DsObjectListData*>(Data), *Length };

    UNREFERENCED_PARAMETER(Owner);
    Walk.List->Count = 0;
    for (DsSound* Sound = DsFirstSound(); Sound != nullptr; Sound = Sound->NextSound)
    {
        if (SUCCEEDED(Sound->InitStatus) && Sound->Device != nullptr)
            DsAddObject(&Walk, &Sound->Face, &Sound->Device->Id);
    }
    *Length = static_cast<ULONG>(FIELD_OFFSET(DsObjectListData, Entries) + Walk.List->Count * sizeof(DsObjectEntry));
    return S_OK;
}

static
HRESULT
DsGetCaptureObjects(
    _In_opt_ PVOID Owner,
    _Inout_ PVOID Data,
    _Inout_ ULONG* Length)
{
    DsObjectWalk Walk = { static_cast<DsObjectListData*>(Data), *Length };

    UNREFERENCED_PARAMETER(Owner);
    Walk.List->Count = 0;
    DsVisitCaptures(DsAddObject, &Walk);
    *Length = static_cast<ULONG>(FIELD_OFFSET(DsObjectListData, Entries) + Walk.List->Count * sizeof(DsObjectEntry));
    return S_OK;
}

static const DsPropertyItem DsMixerItems[] =
{
    { 0, DsGetSrcQuality, DsSetSrcQuality, 20 },
    { 1, DsGetMixerAcceleration, DsSetMixerAcceleration, 20 },
};

static const DsPropertyItem DsDeviceItems[] =
{
    { 0, DsGetPresent, DsSetIgnored, 20 },
    { DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A, DsGetMappingA, nullptr, 32 },
    { DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1, DsGetDescription1, nullptr, sizeof(DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA) },
    { DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1, DsEnumerate1, nullptr, 16 },
    { DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W, DsGetMappingW, nullptr, 32 },
    { DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A, DsGetDescriptionA, nullptr, DS_DESCRIPTION_HEAD },
    { DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W, DsGetDescriptionW, nullptr, DS_DESCRIPTION_HEAD },
    { DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A, DsEnumerateA, nullptr, 16 },
    { DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W, DsEnumerateW, nullptr, 16 },
    { 9, DsGetPresentFlow, DsSetIgnored, 24 },
    { 10, DsGetMappingA, nullptr, 32 },
    { 11, DsGetMappingW, nullptr, 32 },
};

static const DsPropertyItem DsAccelerationItems[] =
{
    { 0, DsGetAcceleration, DsSetAcceleration, 20 },
    { 1, DsGetPresent, nullptr, 20 },
};

static const DsPropertyItem DsDebugItems[] =
{
    { 0, DsGetDebugInfoA, DsSetIgnored, 272 },
    { 1, DsGetDebugInfoW, DsSetIgnored, 532 },
    { 2, DsTranslateA, nullptr, 24 },
    { 3, DsTranslateW, nullptr, 24 },
};

static const DsPropertyItem DsPersistentItems[] =
{
    { 0, DsPrivateGetMissing, DsPrivateSetMissing, 56 },
    { 1, DsPrivateGetMissing, DsPrivateSetMissing, 56 },
};

static const DsPropertyItem DsBufferItems[] =
{
    { 0, DsGetBufferDevice, nullptr, 24 },
};

static const DsPropertyItem DsObjectItems[] =
{
    { 0, DsGetObjects, nullptr, 24 },
    { 1, DsGetCaptureObjects, nullptr, 24 },
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
