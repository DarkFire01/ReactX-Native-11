/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Audio endpoints by DirectSound GUID and the shared mode stream the mixer feeds
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <functiondiscoverykeys_devpkey.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <stdlib.h>

/* The endpoint GUID DirectSound names a device by */
static const PROPERTYKEY DsEndpointGuidKey = { { 0x1DA5D803, 0xD492, 0x4EDD, { 0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E } }, 4 };

/* Ten millisecond periods mean a local endpoint, which gets two periods of buffering, remote ones four */
#define DS_LOCAL_PERIOD_LOW     95000
#define DS_LOCAL_PERIOD_HIGH    105000

static
HRESULT
DsEnumerator(
    _COM_Outptr_ IMMDeviceEnumerator** Enumerator)
{
    return CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(Enumerator));
}

static
HRESULT
DsEndpointGuid(
    _In_ IMMDevice* Endpoint,
    _Out_ GUID* Id)
{
    IPropertyStore* Store = nullptr;
    PROPVARIANT Value;
    HRESULT hr;

    *Id = GUID_NULL;
    hr = Endpoint->OpenPropertyStore(STGM_READ, &Store);
    if (FAILED(hr))
        return hr;
    PropVariantInit(&Value);
    hr = Store->GetValue(DsEndpointGuidKey, &Value);
    if (SUCCEEDED(hr) && Value.vt == VT_LPWSTR)
        hr = CLSIDFromString(Value.pwszVal, Id);
    else if (SUCCEEDED(hr))
        hr = E_FAIL;
    PropVariantClear(&Value);
    Store->Release();
    return hr;
}

static
HRESULT
DsEndpointName(
    _In_ IMMDevice* Endpoint,
    _Out_writes_(Count) WCHAR* Name,
    _In_ ULONG Count)
{
    IPropertyStore* Store = nullptr;
    PROPVARIANT Value;
    HRESULT hr;

    Name[0] = L'\0';
    hr = Endpoint->OpenPropertyStore(STGM_READ, &Store);
    if (FAILED(hr))
        return hr;
    PropVariantInit(&Value);
    hr = Store->GetValue(PKEY_Device_FriendlyName, &Value);
    if (SUCCEEDED(hr) && Value.vt == VT_LPWSTR)
        wcsncpy_s(Name, Count, Value.pwszVal, _TRUNCATE);
    PropVariantClear(&Value);
    Store->Release();
    return hr;
}

/* Default device ids map to the console or communications default of their flow */
static
BOOLEAN
DsDefaultRole(
    _In_ LPCGUID Guid,
    _Out_ EDataFlow* Flow,
    _Out_ ERole* Role)
{
    if (*Guid == DSDEVID_DefaultPlayback)
    {
        *Flow = eRender;
        *Role = eConsole;
        return TRUE;
    }
    if (*Guid == DSDEVID_DefaultVoicePlayback)
    {
        *Flow = eRender;
        *Role = eCommunications;
        return TRUE;
    }
    if (*Guid == DSDEVID_DefaultCapture)
    {
        *Flow = eCapture;
        *Role = eConsole;
        return TRUE;
    }
    if (*Guid == DSDEVID_DefaultVoiceCapture)
    {
        *Flow = eCapture;
        *Role = eCommunications;
        return TRUE;
    }
    return FALSE;
}

HRESULT
DsFindEndpoint(
    _In_ EDataFlow Flow,
    _In_opt_ LPCGUID Guid,
    _COM_Outptr_ IMMDevice** Endpoint,
    _Out_opt_ GUID* Id)
{
    IMMDeviceEnumerator* Enumerator = nullptr;
    IMMDeviceCollection* Collection = nullptr;
    EDataFlow DefaultFlow = Flow;
    ERole Role = eConsole;
    UINT Count = 0;
    HRESULT hr;

    *Endpoint = nullptr;
    hr = DsEnumerator(&Enumerator);
    if (FAILED(hr))
    {
        DsTrace(L"cannot create the device enumerator (0x%08lX)", hr);
        return hr;
    }

    if (Guid == nullptr || *Guid == GUID_NULL || DsDefaultRole(Guid, &DefaultFlow, &Role))
    {
        if (DefaultFlow != Flow)
        {
            Enumerator->Release();
            return DSERR_NODRIVER;
        }
        hr = Enumerator->GetDefaultAudioEndpoint(Flow, Role, Endpoint);
        Enumerator->Release();
        if (FAILED(hr))
            return DSERR_NODRIVER;
        if (Id != nullptr)
            DsEndpointGuid(*Endpoint, Id);
        return S_OK;
    }

    hr = Enumerator->EnumAudioEndpoints(Flow, DEVICE_STATE_ACTIVE, &Collection);
    Enumerator->Release();
    if (FAILED(hr))
        return DSERR_NODRIVER;
    Collection->GetCount(&Count);
    for (UINT Index = 0; Index < Count; Index++)
    {
        IMMDevice* Candidate = nullptr;
        GUID Found;

        if (FAILED(Collection->Item(Index, &Candidate)))
            continue;
        if (SUCCEEDED(DsEndpointGuid(Candidate, &Found)) && Found == *Guid)
        {
            *Endpoint = Candidate;
            if (Id != nullptr)
                *Id = Found;
            Collection->Release();
            return S_OK;
        }
        Candidate->Release();
    }
    Collection->Release();
    return DSERR_NODRIVER;
}

static
BOOL
DsCallEnumerate(
    _In_opt_ LPDSENUMCALLBACKA CallbackA,
    _In_opt_ LPDSENUMCALLBACKW CallbackW,
    _In_opt_ PVOID Context,
    _In_opt_ GUID* Guid,
    _In_z_ PCWSTR Description,
    _In_z_ PCWSTR Module)
{
    BOOL Continue = TRUE;

    if (CallbackA != nullptr)
    {
        CHAR DescriptionA[256];
        CHAR ModuleA[256];

        WideCharToMultiByte(CP_ACP, 0, Description, -1, DescriptionA, sizeof(DescriptionA), nullptr, nullptr);
        WideCharToMultiByte(CP_ACP, 0, Module, -1, ModuleA, sizeof(ModuleA), nullptr, nullptr);
        Continue = CallbackA(Guid, DescriptionA, ModuleA, Context);
        if (!Continue)
            return FALSE;
    }
    if (CallbackW != nullptr)
        Continue = CallbackW(Guid, Description, Module, Context);
    return Continue;
}

/* The primary driver comes first, then each active endpoint with the default one leading */
HRESULT
DsEnumerateEndpoints(
    _In_ EDataFlow Flow,
    _In_opt_ LPDSENUMCALLBACKA CallbackA,
    _In_opt_ LPDSENUMCALLBACKW CallbackW,
    _In_opt_ PVOID Context)
{
    IMMDeviceEnumerator* Enumerator = nullptr;
    IMMDeviceCollection* Collection = nullptr;
    IMMDevice* Default = nullptr;
    WCHAR* DefaultId = nullptr;
    WCHAR Primary[128];
    UINT Count = 0;
    HRESULT hr;

    if (CallbackA == nullptr && CallbackW == nullptr)
        return E_INVALIDARG;

    hr = DsEnumerator(&Enumerator);
    if (FAILED(hr))
        return hr;
    hr = Enumerator->EnumAudioEndpoints(Flow, DEVICE_STATE_ACTIVE, &Collection);
    if (SUCCEEDED(Enumerator->GetDefaultAudioEndpoint(Flow, eConsole, &Default)))
    {
        Default->GetId(&DefaultId);
        Default->Release();
    }
    Enumerator->Release();
    if (FAILED(hr))
    {
        CoTaskMemFree(DefaultId);
        return hr;
    }
    Collection->GetCount(&Count);

    if (Count != 0 &&
        LoadStringW(DsModule, Flow == eRender ? 1000 : 1001, Primary, _countof(Primary)) != 0 &&
        !DsCallEnumerate(CallbackA, CallbackW, Context, nullptr, Primary, L""))
    {
        Count = 0;
    }

    for (UINT Pass = 0; Pass < 2; Pass++)
    {
        for (UINT Index = 0; Index < Count; Index++)
        {
            IMMDevice* Endpoint = nullptr;
            WCHAR* Id = nullptr;
            WCHAR Name[256];
            GUID Guid;
            BOOLEAN IsDefault;

            if (FAILED(Collection->Item(Index, &Endpoint)))
                continue;
            Endpoint->GetId(&Id);
            IsDefault = DefaultId != nullptr && Id != nullptr && wcscmp(Id, DefaultId) == 0;
            if ((Pass == 0) == IsDefault && SUCCEEDED(DsEndpointGuid(Endpoint, &Guid)))
            {
                DsEndpointName(Endpoint, Name, _countof(Name));
                if (!DsCallEnumerate(CallbackA, CallbackW, Context, &Guid, Name, Id != nullptr ? Id : L""))
                    Count = 0;
            }
            CoTaskMemFree(Id);
            Endpoint->Release();
        }
    }

    CoTaskMemFree(DefaultId);
    Collection->Release();
    return S_OK;
}

/* The kernel streaming interface behind an endpoint, without the property's "{N}." prefix */
static
WCHAR*
DsEndpointInterface(
    _In_ IMMDevice* Endpoint)
{
    static const PROPERTYKEY InterfaceKey = { { 0x233164C8, 0x1B2C, 0x4C7D, { 0xBC, 0x68, 0xB6, 0x71, 0x68, 0x7A, 0x25, 0x67 } }, 1 };
    IPropertyStore* Store = nullptr;
    WCHAR* Copy = nullptr;
    PROPVARIANT Value;

    if (FAILED(Endpoint->OpenPropertyStore(STGM_READ, &Store)))
        return nullptr;
    PropVariantInit(&Value);
    if (SUCCEEDED(Store->GetValue(InterfaceKey, &Value)) && Value.vt == VT_LPWSTR)
    {
        const WCHAR* Path = Value.pwszVal;
        const WCHAR* Brace = wcschr(Path, L'}');
        SIZE_T Length;

        if (Path[0] == L'{' && Brace != nullptr && Brace[1] == L'.')
            Path = Brace + 2;
        Length = wcslen(Path) + 1;
        Copy = static_cast<WCHAR*>(HeapAlloc(GetProcessHeap(), 0, Length * sizeof(*Copy)));
        if (Copy != nullptr)
            memcpy(Copy, Path, Length * sizeof(*Copy));
    }
    PropVariantClear(&Value);
    Store->Release();
    return Copy;
}

/* The waveOut or waveIn index whose function instance is this endpoint, or all ones */
static
ULONG
DsEndpointWaveId(
    _In_ BOOLEAN Capture,
    _In_z_ const WCHAR* EndpointId)
{
    UINT Count = Capture ? waveInGetNumDevs() : waveOutGetNumDevs();

    for (UINT Index = 0; Index < Count; Index++)
    {
        ULONG Size = 0;
        WCHAR* Instance;
        BOOLEAN Same;

        if (Capture)
        {
            if (waveInMessage(reinterpret_cast<HWAVEIN>(static_cast<UINT_PTR>(Index)), DRV_QUERYFUNCTIONINSTANCEIDSIZE,
                              reinterpret_cast<DWORD_PTR>(&Size), 0) != MMSYSERR_NOERROR)
            {
                continue;
            }
        }
        else if (waveOutMessage(reinterpret_cast<HWAVEOUT>(static_cast<UINT_PTR>(Index)), DRV_QUERYFUNCTIONINSTANCEIDSIZE,
                                reinterpret_cast<DWORD_PTR>(&Size), 0) != MMSYSERR_NOERROR)
        {
            continue;
        }
        if (Size == 0)
            continue;
        Instance = static_cast<WCHAR*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size));
        if (Instance == nullptr)
            return 0xFFFFFFFF;
        if (Capture)
            waveInMessage(reinterpret_cast<HWAVEIN>(static_cast<UINT_PTR>(Index)), DRV_QUERYFUNCTIONINSTANCEID,
                          reinterpret_cast<DWORD_PTR>(Instance), Size);
        else
            waveOutMessage(reinterpret_cast<HWAVEOUT>(static_cast<UINT_PTR>(Index)), DRV_QUERYFUNCTIONINSTANCEID,
                           reinterpret_cast<DWORD_PTR>(Instance), Size);
        Same = wcscmp(Instance, EndpointId) == 0;
        HeapFree(GetProcessHeap(), 0, Instance);
        if (Same)
            return Index;
    }
    return 0xFFFFFFFF;
}

VOID
DsFreeDeviceRecords(
    _In_reads_opt_(Count) DsDeviceRecord* Records,
    _In_ ULONG Count)
{
    if (Records == nullptr)
        return;
    for (ULONG Index = 0; Index < Count; Index++)
    {
        CoTaskMemFree(Records[Index].Module);
        HeapFree(GetProcessHeap(), 0, Records[Index].Interface);
    }
    HeapFree(GetProcessHeap(), 0, Records);
}

/* Every active render endpoint, then every active capture endpoint, as the device property set describes them */
HRESULT
DsListDeviceRecords(
    _Outptr_result_buffer_(*Count) DsDeviceRecord** Records,
    _Out_ ULONG* Count)
{
    IMMDeviceEnumerator* Enumerator = nullptr;
    DsDeviceRecord* List = nullptr;
    ULONG Used = 0;
    ULONG Room = 0;
    HRESULT hr;

    *Records = nullptr;
    *Count = 0;
    hr = DsEnumerator(&Enumerator);
    if (FAILED(hr))
        return hr;

    for (UINT Pass = 0; Pass < 2 && SUCCEEDED(hr); Pass++)
    {
        IMMDeviceCollection* Collection = nullptr;
        UINT Total = 0;

        if (FAILED(Enumerator->EnumAudioEndpoints(Pass == 0 ? eRender : eCapture, DEVICE_STATE_ACTIVE, &Collection)))
            continue;
        Collection->GetCount(&Total);
        for (UINT Index = 0; Index < Total && SUCCEEDED(hr); Index++)
        {
            IMMDevice* Endpoint = nullptr;
            DsDeviceRecord* Record;

            if (FAILED(Collection->Item(Index, &Endpoint)))
                continue;
            if (Used == Room)
            {
                ULONG Grown = Room == 0 ? 4 : Room * 2;
                VOID* Bigger = List == nullptr ?
                    HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Grown * sizeof(*List)) :
                    HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, List, Grown * sizeof(*List));

                if (Bigger == nullptr)
                {
                    Endpoint->Release();
                    hr = E_OUTOFMEMORY;
                    break;
                }
                List = static_cast<DsDeviceRecord*>(Bigger);
                Room = Grown;
            }
            Record = &List[Used];
            Record->Capture = Pass != 0;
            if (SUCCEEDED(DsEndpointGuid(Endpoint, &Record->Id)) && SUCCEEDED(Endpoint->GetId(&Record->Module)))
            {
                DsEndpointName(Endpoint, Record->Name, _countof(Record->Name));
                Record->Interface = DsEndpointInterface(Endpoint);
                if (Record->Interface != nullptr && !Record->Capture)
                    CharUpperW(Record->Interface);
                Record->WaveId = DsEndpointWaveId(Record->Capture, Record->Module);
                Used++;
            }
            else
            {
                CoTaskMemFree(Record->Module);
                ZeroMemory(Record, sizeof(*Record));
            }
            Endpoint->Release();
        }
        Collection->Release();
    }
    Enumerator->Release();
    if (FAILED(hr))
    {
        DsFreeDeviceRecords(List, Used);
        return hr;
    }
    *Records = List;
    *Count = Used;
    return S_OK;
}

HRESULT
DsResolveDeviceId(
    _In_ LPCGUID Source,
    _Out_ GUID* Destination)
{
    IMMDevice* Endpoint = nullptr;
    EDataFlow Flow;
    ERole Role;
    GUID Found = GUID_NULL;
    HRESULT hr;

    if (DsDefaultRole(Source, &Flow, &Role))
    {
        hr = DsFindEndpoint(Flow, Source, &Endpoint, &Found);
    }
    else
    {
        if (*Source == GUID_NULL)
            return DSERR_NODRIVER;
        hr = DsFindEndpoint(eRender, Source, &Endpoint, &Found);
        if (FAILED(hr))
            hr = DsFindEndpoint(eCapture, Source, &Endpoint, &Found);
    }
    if (FAILED(hr))
        return hr;
    Endpoint->Release();
    *Destination = Found;
    return S_OK;
}

DsDevice::DsDevice()
{
    Id = GUID_NULL;
    EndpointId = nullptr;
    Rate = 48000;
    Channels = 2;
    Mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    EndpointChannels = Channels;
    EndpointMask = Mask;
    LayoutChosen = FALSE;
    LayoutLocked = FALSE;
    Secondaries = 0;
    Pan = 0;
    Global = nullptr;
    Endpoint = nullptr;
    Client = nullptr;
    Render = nullptr;
    Volume = nullptr;
    MixFormat = nullptr;
    StreamFormat = nullptr;
    FloatOutput = FALSE;
    QueueFrames = 0;
    Quantum = 480;
    Event = nullptr;
    Thread = nullptr;
    Quit = nullptr;
    Streaming = FALSE;
    Failed = FALSE;
    Voices = nullptr;
    Scratch = nullptr;
    CubicRates = FALSE;
    InitializeCriticalSection(&MixLock);
}

DsDevice::~DsDevice()
{
    if (Thread != nullptr)
    {
        SetEvent(Quit);
        WaitForSingleObject(Thread, INFINITE);
        CloseHandle(Thread);
    }
    if (Client != nullptr)
        Client->Stop();
    if (Volume != nullptr)
        Volume->Release();
    if (Render != nullptr)
        Render->Release();
    if (Client != nullptr)
        Client->Release();
    if (Endpoint != nullptr)
        Endpoint->Release();
    if (Event != nullptr)
        CloseHandle(Event);
    if (Quit != nullptr)
        CloseHandle(Quit);
    CoTaskMemFree(MixFormat);
    CoTaskMemFree(EndpointId);
    HeapFree(GetProcessHeap(), 0, Global);
    HeapFree(GetProcessHeap(), 0, Scratch);
    DeleteCriticalSection(&MixLock);
}

/* Finds the endpoint and learns its mix format; the stream itself starts with the first sound */
HRESULT
DsDevice::Open(
    _In_opt_ LPCGUID Guid)
{
    WAVEFORMATEX Default;
    HRESULT hr;

    hr = DsFindEndpoint(eRender, Guid, &Endpoint, &Id);
    if (FAILED(hr))
        return hr;
    Endpoint->GetId(&EndpointId);

    hr = Endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(&Client));
    if (FAILED(hr))
    {
        DsTrace(L"cannot activate the audio client (0x%08lX)", hr);
        return DSERR_NODRIVER;
    }
    hr = Client->GetMixFormat(&MixFormat);
    if (FAILED(hr))
    {
        DsTrace(L"cannot read the mix format (0x%08lX)", hr);
        return DSERR_NODRIVER;
    }

    Rate = MixFormat->nSamplesPerSec;
    Channels = DsMin<UINT>(MixFormat->nChannels, DS_MAX_CHANNELS);
    if (MixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        Mask = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(MixFormat)->dwChannelMask;
    else
        Mask = MixFormat->nChannels == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    EndpointChannels = Channels;
    EndpointMask = Mask;
    StreamFormat = MixFormat;
    Quantum = (Rate + DS_QUANTA_PER_SECOND / 2) / DS_QUANTA_PER_SECOND;

    /* The primary buffer format starts at 22 kHz 8 bit stereo */
    Default.wFormatTag = WAVE_FORMAT_PCM;
    Default.nChannels = 2;
    Default.nSamplesPerSec = 22050;
    Default.wBitsPerSample = 8;
    Default.nBlockAlign = 2;
    Default.nAvgBytesPerSec = 22050 * 2;
    Default.cbSize = 0;
    Global = DsCopyFormat(&Default);
    if (Global == nullptr)
        return E_OUTOFMEMORY;

    Scratch = static_cast<float*>(HeapAlloc(GetProcessHeap(), 0, Quantum * DS_MAX_CHANNELS * sizeof(float)));
    if (Scratch == nullptr)
        return E_OUTOFMEMORY;
    return S_OK;
}

DWORD
DsDevice::SpeakerConfig(VOID)
{
    switch (EndpointMask)
    {
        case 0:
            return 0;
        case KSAUDIO_SPEAKER_MONO:
            return DSSPEAKER_MONO;
        case KSAUDIO_SPEAKER_STEREO:
            return DSSPEAKER_COMBINED(DSSPEAKER_STEREO, DSSPEAKER_GEOMETRY_WIDE);
        case KSAUDIO_SPEAKER_QUAD:
            return DSSPEAKER_QUAD;
        case KSAUDIO_SPEAKER_SURROUND:
            return DSSPEAKER_SURROUND;
        case KSAUDIO_SPEAKER_5POINT1:
            return DSSPEAKER_5POINT1;
        case KSAUDIO_SPEAKER_7POINT1:
            return DSSPEAKER_7POINT1;
        case KSAUDIO_SPEAKER_5POINT1_SURROUND:
            return DSSPEAKER_5POINT1_SURROUND;
        case KSAUDIO_SPEAKER_7POINT1_SURROUND:
            return DSSPEAKER_7POINT1_SURROUND;
    }
    return 0xFFFFFFFF;
}

/* Two periods of shared mode buffering on a local endpoint, four on a remote one, fed on the engine event */
HRESULT
DsDevice::StartStreaming(VOID)
{
    REFERENCE_TIME Period = 0;
    REFERENCE_TIME Minimum = 0;
    REFERENCE_TIME Duration;
    UINT32 PeriodFrames;
    UINT Packets;
    HRESULT hr;

    if (Streaming)
    {
        Kick();
        return S_OK;
    }
    if (Failed || Client == nullptr)
        return Failed ? DSERR_NODRIVER : S_OK;

    ChooseStreamFormat();
    hr = Client->GetDevicePeriod(&Period, &Minimum);
    if (FAILED(hr))
        Period = 100000;
    Packets = Period >= DS_LOCAL_PERIOD_LOW && Period < DS_LOCAL_PERIOD_HIGH ? 2 : 4;

    /* The period and the whole queue go to frames and back, each rounded to the nearest */
    PeriodFrames = static_cast<UINT32>((Rate * Period + 5000000) / 10000000);
    Duration = (10000000LL * Packets * PeriodFrames + Rate / 2) / Rate;
    Period = (10000000LL * PeriodFrames + Rate / 2) / Rate;
    QueueFrames = static_cast<UINT32>((Rate * Duration + 5000000) / 10000000);
    PeriodFrames = static_cast<UINT32>((Rate * Period + 5000000) / 10000000);

    hr = Client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                            Duration, 0, StreamFormat, nullptr);
    if (FAILED(hr))
    {
        DsTrace(L"cannot initialize the shared mode stream (0x%08lX)", hr);
        Failed = TRUE;
        return DSERR_NODRIVER;
    }
    hr = Client->GetService(IID_PPV_ARGS(&Render));
    if (SUCCEEDED(hr))
        hr = Client->GetService(IID_PPV_ARGS(&Volume));
    if (FAILED(hr))
    {
        DsTrace(L"cannot reach the render client (0x%08lX)", hr);
        Failed = TRUE;
        return DSERR_NODRIVER;
    }

    Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Quit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (Event == nullptr || Quit == nullptr)
        return E_OUTOFMEMORY;
    Client->SetEventHandle(Event);

    /* The stream opens on silence, one period short of the queue it keeps */
    if (QueueFrames > PeriodFrames)
    {
        BYTE* Target = nullptr;

        if (SUCCEEDED(Render->GetBuffer(QueueFrames - PeriodFrames, &Target)))
            Render->ReleaseBuffer(QueueFrames - PeriodFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    }
    SetEvent(Event);
    hr = Client->Start();
    if (FAILED(hr))
    {
        DsTrace(L"cannot start the stream (0x%08lX)", hr);
        Failed = TRUE;
        return DSERR_NODRIVER;
    }
    Thread = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
    if (Thread == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());
    Streaming = TRUE;
    return S_OK;
}

/* The stream carries the final mix as it is when the engine takes that layout, and the endpoint layout otherwise */
VOID
DsDevice::ChooseStreamFormat(VOID)
{
    WAVEFORMATEX* Closest = nullptr;
    HRESULT hr;

    StreamFormat = MixFormat;
    if (LayoutChosen && !LayoutLocked && (Channels != EndpointChannels || Mask != EndpointMask))
    {
        OwnFormat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        OwnFormat.Format.nChannels = static_cast<WORD>(Channels);
        OwnFormat.Format.nSamplesPerSec = Rate;
        OwnFormat.Format.wBitsPerSample = 32;
        OwnFormat.Format.nBlockAlign = static_cast<WORD>(Channels * sizeof(float));
        OwnFormat.Format.nAvgBytesPerSec = Rate * OwnFormat.Format.nBlockAlign;
        OwnFormat.Format.cbSize = sizeof(OwnFormat) - sizeof(OwnFormat.Format);
        OwnFormat.Samples.wValidBitsPerSample = 32;
        OwnFormat.dwChannelMask = Mask;
        OwnFormat.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

        hr = Client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &OwnFormat.Format, &Closest);
        CoTaskMemFree(Closest);
        if (hr == S_OK)
            StreamFormat = &OwnFormat.Format;
        else
            DsTrace(L"the engine takes no %u channel mix (0x%08lX), locking the endpoint layout", Channels, hr);
    }
    if (StreamFormat == MixFormat)
        LockLayout();
    FloatOutput = DsFormatTag(StreamFormat) == WAVE_FORMAT_IEEE_FLOAT && StreamFormat->wBitsPerSample == 32;
}

/* Stops the stream and its thread and starts over with a fresh audio client */
VOID
DsDevice::CloseStream(VOID)
{
    HRESULT hr;

    if (Thread != nullptr)
    {
        SetEvent(Quit);
        WaitForSingleObject(Thread, INFINITE);
        CloseHandle(Thread);
        Thread = nullptr;
    }
    if (Client != nullptr)
        Client->Stop();
    if (Volume != nullptr)
        Volume->Release();
    if (Render != nullptr)
        Render->Release();
    if (Client != nullptr)
        Client->Release();
    if (Event != nullptr)
        CloseHandle(Event);
    if (Quit != nullptr)
        CloseHandle(Quit);
    Volume = nullptr;
    Render = nullptr;
    Client = nullptr;
    Event = nullptr;
    Quit = nullptr;
    Streaming = FALSE;
    StreamFormat = MixFormat;

    hr = Endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(&Client));
    if (FAILED(hr))
    {
        DsTrace(L"cannot activate the audio client again (0x%08lX)", hr);
        Client = nullptr;
        Failed = TRUE;
    }
}

/* From here on the final mix is the endpoint layout, and voices built for another layout start over */
VOID
DsDevice::LockLayout(VOID)
{
    BOOLEAN Moved = Channels != EndpointChannels || Mask != EndpointMask;
    BOOLEAN Restart = Streaming && StreamFormat != MixFormat;

    if (LayoutLocked)
        return;
    if (Restart)
        CloseStream();

    Lock();
    LayoutChosen = TRUE;
    LayoutLocked = TRUE;
    Channels = EndpointChannels;
    Mask = EndpointMask;
    Unlock();

    if (Moved)
    {
        DsTrace(L"final mix locked to the endpoint layout of %u channels", Channels);
        for (DsVoice* Voice = Voices; Voice != nullptr; Voice = Voice->Next)
            Voice->Rebuild();
    }
    if (Restart)
        StartStreaming();
}

/* A buffer wider than a stereo endpoint, or narrower than a surround one, keeps the final mix in its own layout */
VOID
DsDevice::PlaceBuffer(
    _In_ const WAVEFORMATEX* Format,
    _In_ BOOLEAN Fixed)
{
    BOOLEAN Surround = EndpointChannels > 2;
    BOOLEAN Own = !Fixed && (Surround ? Format->nChannels <= 2 : Format->nChannels > 2);

    if (!LayoutChosen)
    {
        if (!Own)
        {
            LockLayout();
            return;
        }
        Lock();
        LayoutChosen = TRUE;
        Channels = Surround ? 2 : Format->nChannels;
        Mask = Surround ? KSAUDIO_SPEAKER_STEREO : DsChannelMask(Format);
        Unlock();
        DsTrace(L"final mix takes the %u channel layout of the first buffer", Channels);
        return;
    }

    if (!LayoutLocked && !Own)
        LockLayout();
    if (Secondaries >= 2)
        UseCubicRates();
}

VOID
DsDevice::Kick(VOID)
{
    if (Event != nullptr)
        SetEvent(Event);
}

VOID
DsDevice::AddVoice(
    _In_ DsVoice* Voice)
{
    Lock();
    Voice->Next = Voices;
    Voices = Voice;
    if (Voice->Secondary)
        Secondaries += 1;
    Unlock();
}

/* Every voice, present and future, converts rates with the cubic converter from here on */
VOID
DsDevice::UseCubicRates(VOID)
{
    Lock();
    CubicRates = TRUE;
    for (DsVoice* Voice = Voices; Voice != nullptr; Voice = Voice->Next)
        Voice->StartCubic();
    Unlock();
}

VOID
DsDevice::RemoveVoice(
    _In_ DsVoice* Voice)
{
    Lock();
    for (DsVoice** Link = &Voices; *Link != nullptr; Link = &(*Link)->Next)
    {
        if (*Link == Voice)
        {
            *Link = Voice->Next;
            if (Voice->Secondary)
                Secondaries -= 1;
            break;
        }
    }
    Unlock();
}

/* One engine pass over every voice into the final mix */
VOID
DsDevice::RenderQuantum(
    _Out_writes_(Quantum * Channels) float* Output)
{
    ZeroMemory(Output, Quantum * Channels * sizeof(float));
    Lock();
    for (DsVoice* Voice = Voices; Voice != nullptr; Voice = Voice->Next)
    {
        if (!(Voice->State & DS_VOICE_PLAYING) || Voice->Pass == DS_PASS_FINISHED)
            continue;
        Voice->PassStart();
        Voice->Mix(Output, Quantum, Channels);
        Voice->PassEnd();
    }
    Unlock();
}

/* Mixes one quantum, or two when the queue has room for both, never past the queue the stream was opened with */
VOID
DsDevice::Pump(VOID)
{
    UINT32 Padding = 0;
    UINT Passes;

    if (FAILED(Client->GetCurrentPadding(&Padding)))
        return;
    if (Padding + Quantum > QueueFrames)
        return;
    Passes = Padding + 2 * Quantum > QueueFrames ? 1 : 2;

    for (UINT Done = 0; Done < Passes; Done++)
    {
        BYTE* Target = nullptr;
        HRESULT hr = Render->GetBuffer(Quantum, &Target);

        if (FAILED(hr))
        {
            DS_TRACE_ONCE(L"cannot get stream buffer space (0x%08lX)", hr);
            return;
        }

        RenderQuantum(Scratch);
        if (FloatOutput && StreamFormat->nChannels == Channels)
        {
            memcpy(Target, Scratch, Quantum * Channels * sizeof(float));
        }
        else
        {
            UINT Stride = StreamFormat->nChannels;

            for (UINT Frame = 0; Frame < Quantum; Frame++)
            {
                for (UINT Channel = 0; Channel < Stride; Channel++)
                {
                    float Value = Channel < Channels ? Scratch[Frame * Channels + Channel] : 0.0f;

                    if (FloatOutput)
                    {
                        reinterpret_cast<float*>(Target)[Frame * Stride + Channel] = Value;
                    }
                    else
                    {
                        Value = DsMin(DsMax(Value, -1.0f), 1.0f);
                        reinterpret_cast<SHORT*>(Target)[Frame * Stride + Channel] = static_cast<SHORT>(Value * 32767.0f);
                    }
                }
            }
        }
        Render->ReleaseBuffer(Quantum, 0);
    }
}

DWORD
WINAPI
DsDevice::ThreadMain(
    _In_ PVOID Context)
{
    DsDevice* Self = static_cast<DsDevice*>(Context);
    HANDLE Waits[2] = { Self->Quit, Self->Event };

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    for (;;)
    {
        DWORD Result = WaitForMultipleObjects(2, Waits, FALSE, 200);

        if (Result == WAIT_OBJECT_0)
            break;
        Self->Pump();
    }

    CoUninitialize();
    return 0;
}

/* The primary volume is the stream session volume */
HRESULT
DsDevice::GetMasterVolume(
    _Out_ LONG* Level)
{
    float Value = 1.0f;

    if (Volume != nullptr && FAILED(Volume->GetMasterVolume(&Value)))
        Value = 1.0f;
    *Level = DsAmplitudeToDecibels(static_cast<ULONG>(Value / DsAmplitudeScale));
    return S_OK;
}

HRESULT
DsDevice::SetMasterVolume(
    _In_ LONG Level)
{
    float Value = static_cast<float>(DsDecibelsToAmplitude(Level)) * DsAmplitudeScale;

    if (Volume == nullptr)
    {
        HRESULT hr = StartStreaming();

        if (FAILED(hr))
            return hr;
    }
    if (Volume == nullptr)
        return DSERR_GENERIC;
    return Volume->SetMasterVolume(Value, nullptr);
}
