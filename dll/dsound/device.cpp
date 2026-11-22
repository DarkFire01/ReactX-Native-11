/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Audio endpoints by DirectSound GUID and the shared mode stream the mixer feeds
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <functiondiscoverykeys_devpkey.h>
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
    Pan = 0;
    Global = nullptr;
    Endpoint = nullptr;
    Client = nullptr;
    Render = nullptr;
    Volume = nullptr;
    MixFormat = nullptr;
    FloatOutput = FALSE;
    BufferFrames = 0;
    Quantum = 480;
    Event = nullptr;
    Thread = nullptr;
    Quit = nullptr;
    Streaming = FALSE;
    Failed = FALSE;
    Voices = nullptr;
    Scratch = nullptr;
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
    FloatOutput = DsFormatTag(MixFormat) == WAVE_FORMAT_IEEE_FLOAT && MixFormat->wBitsPerSample == 32;
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

    Scratch = static_cast<float*>(HeapAlloc(GetProcessHeap(), 0, Quantum * Channels * sizeof(float)));
    if (Scratch == nullptr)
        return E_OUTOFMEMORY;
    return S_OK;
}

DWORD
DsDevice::SpeakerConfig(VOID)
{
    switch (Mask)
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
    UINT Packets;
    HRESULT hr;

    if (Streaming)
    {
        Kick();
        return S_OK;
    }
    if (Failed || Client == nullptr)
        return Failed ? DSERR_NODRIVER : S_OK;

    hr = Client->GetDevicePeriod(&Period, &Minimum);
    if (FAILED(hr))
        Period = 100000;
    Packets = Period >= DS_LOCAL_PERIOD_LOW && Period < DS_LOCAL_PERIOD_HIGH ? 2 : 4;
    Duration = Packets * ((Rate * Period + 5000000) / 10000000) * 10000000 / Rate;

    hr = Client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                            Duration, 0, MixFormat, nullptr);
    if (FAILED(hr))
    {
        DsTrace(L"cannot initialize the shared mode stream (0x%08lX)", hr);
        Failed = TRUE;
        return DSERR_NODRIVER;
    }
    Client->GetBufferSize(&BufferFrames);
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

    /* The stream opens on silence, leaving one quantum of room for the first mix */
    if (BufferFrames > Quantum)
    {
        BYTE* Target = nullptr;

        if (SUCCEEDED(Render->GetBuffer(BufferFrames - Quantum, &Target)))
            Render->ReleaseBuffer(BufferFrames - Quantum, AUDCLNT_BUFFERFLAGS_SILENT);
    }
    Pump();
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

/* Fills whatever room the stream has, a quantum at a time */
VOID
DsDevice::Pump(VOID)
{
    UINT32 Padding = 0;

    if (FAILED(Client->GetCurrentPadding(&Padding)))
        return;

    while (BufferFrames - Padding >= Quantum)
    {
        BYTE* Target = nullptr;
        HRESULT hr = Render->GetBuffer(Quantum, &Target);

        if (FAILED(hr))
        {
            DS_TRACE_ONCE(L"cannot get stream buffer space (0x%08lX)", hr);
            return;
        }

        RenderQuantum(Scratch);
        if (FloatOutput && MixFormat->nChannels == Channels)
        {
            memcpy(Target, Scratch, Quantum * Channels * sizeof(float));
        }
        else
        {
            UINT Stride = MixFormat->nChannels;

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
        Padding += Quantum;
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
