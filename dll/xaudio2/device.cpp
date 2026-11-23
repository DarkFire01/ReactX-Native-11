/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The shared mode WASAPI stream a mastering voice renders into
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

/* A device period near 10 ms keeps two periods queued, anything else four */
#define XA_LOCAL_PERIOD_LOW     95000
#define XA_LOCAL_PERIOD_HIGH    105000
#define XA_LOCAL_PACKETS        2
#define XA_REMOTE_PACKETS       4

#define XA_DEVICE_PREFIX        L"\\\\?\\SWD#MMDEVAPI#"

static
HRESULT
XaEnumerator(
    _Outptr_ IMMDeviceEnumerator** Enumerator)
{
    return CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(Enumerator));
}

HRESULT
XaDefaultDeviceId(
    _Outptr_ PWSTR* DeviceId)
{
    IMMDeviceEnumerator* Enumerator = nullptr;
    IMMDevice* Device = nullptr;
    HRESULT hr;

    *DeviceId = nullptr;
    hr = XaEnumerator(&Enumerator);
    if (SUCCEEDED(hr))
        hr = Enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &Device);
    if (SUCCEEDED(hr))
        hr = Device->GetId(DeviceId);
    if (Device != nullptr)
        Device->Release();
    if (Enumerator != nullptr)
        Enumerator->Release();
    return hr;
}

/* Accepts an endpoint id, or the device interface path that wraps one */
HRESULT
XaCanonicalDeviceId(
    _In_z_ PCWSTR DeviceId,
    _Outptr_ PWSTR* Canonical)
{
    IMMDeviceEnumerator* Enumerator = nullptr;
    IMMDevice* Device = nullptr;
    SIZE_T Length;
    PCWSTR Start = DeviceId;
    PCWSTR End;
    HRESULT hr;

    *Canonical = nullptr;
    if (_wcsnicmp(DeviceId, XA_DEVICE_PREFIX, wcslen(XA_DEVICE_PREFIX)) == 0)
        Start = DeviceId + wcslen(XA_DEVICE_PREFIX);
    End = wcschr(Start, L'#');
    Length = End != nullptr ? static_cast<SIZE_T>(End - Start) : wcslen(Start);
    if (Length == 0)
        return E_INVALIDARG;
    *Canonical = static_cast<PWSTR>(CoTaskMemAlloc((Length + 1) * sizeof(WCHAR)));
    if (*Canonical == nullptr)
        return E_OUTOFMEMORY;
    memcpy(*Canonical, Start, Length * sizeof(WCHAR));
    (*Canonical)[Length] = L'\0';

    hr = XaEnumerator(&Enumerator);
    if (SUCCEEDED(hr))
        hr = Enumerator->GetDevice(*Canonical, &Device) == S_OK ? S_OK : E_INVALIDARG;
    if (Device != nullptr)
        Device->Release();
    if (Enumerator != nullptr)
        Enumerator->Release();
    if (FAILED(hr))
    {
        CoTaskMemFree(*Canonical);
        *Canonical = nullptr;
    }
    return hr;
}

/* An audio client on the endpoint with the stream category already set */
static
HRESULT
XaActivateClient(
    _In_z_ PCWSTR DeviceId,
    _In_ AUDIO_STREAM_CATEGORY Category,
    _Outptr_opt_ IMMDevice** DeviceOut,
    _Outptr_ IAudioClient2** Client)
{
    IMMDeviceEnumerator* Enumerator = nullptr;
    IMMDevice* Device = nullptr;
    AudioClientProperties Properties = {};
    HRESULT hr;

    *Client = nullptr;
    hr = XaEnumerator(&Enumerator);
    if (SUCCEEDED(hr))
        hr = Enumerator->GetDevice(DeviceId, &Device);
    if (SUCCEEDED(hr))
        hr = Device->Activate(__uuidof(IAudioClient2), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(Client));
    if (SUCCEEDED(hr))
    {
        Properties.cbSize = sizeof(Properties);
        Properties.bIsOffload = FALSE;
        Properties.eCategory = Category;
        Properties.Options = AUDCLNT_STREAMOPTIONS_NONE;
        hr = (*Client)->SetClientProperties(&Properties);
    }
    if (FAILED(hr) && *Client != nullptr)
    {
        (*Client)->Release();
        *Client = nullptr;
    }
    if (SUCCEEDED(hr) && DeviceOut != nullptr)
    {
        *DeviceOut = Device;
        Device = nullptr;
    }
    if (Device != nullptr)
        Device->Release();
    if (Enumerator != nullptr)
        Enumerator->Release();
    return hr;
}

static
DWORD
XaMixFormatMask(
    _In_ const WAVEFORMATEX* Format)
{
    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && Format->cbSize >= 22)
        return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format)->dwChannelMask;
    return XaPseudoMask(Format->nChannels);
}

/* Whether the engine takes the mix format with another channel count and the plain layout for it */
static
BOOLEAN
XaEngineTakesChannels(
    _In_ IAudioClient2* Client,
    _In_ const WAVEFORMATEX* Mix,
    _In_ UINT32 Channels)
{
    WAVEFORMATEXTENSIBLE Alternate = {};
    WAVEFORMATEX* Closest = nullptr;
    HRESULT hr;

    memcpy(&Alternate, Mix, XaMin<SIZE_T>(sizeof(Alternate), sizeof(WAVEFORMATEX) + Mix->cbSize));
    if (Mix->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
    {
        Alternate.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        Alternate.Format.cbSize = sizeof(Alternate) - sizeof(Alternate.Format);
        Alternate.Samples.wValidBitsPerSample = Mix->wBitsPerSample;
        Alternate.SubFormat = Mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
    }
    Alternate.Format.nChannels = static_cast<WORD>(Channels);
    Alternate.Format.nBlockAlign = static_cast<WORD>((Alternate.Format.wBitsPerSample / 8) * Channels);
    Alternate.Format.nAvgBytesPerSec = Alternate.Format.nBlockAlign * Alternate.Format.nSamplesPerSec;
    Alternate.dwChannelMask = XaPseudoMask(Channels);
    hr = Client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &Alternate.Format, &Closest);
    CoTaskMemFree(Closest);
    return hr == S_OK;
}

HRESULT
XaQueryEndpointFormat(
    _In_opt_z_ PCWSTR DeviceId,
    _In_ AUDIO_STREAM_CATEGORY Category,
    _In_ UINT32 Preferred,
    _Out_ UINT32* Channels,
    _Out_ UINT32* Rate,
    _Out_ DWORD* Mask)
{
    IAudioClient2* Client = nullptr;
    WAVEFORMATEX* Mix = nullptr;
    WORD Tag;
    HRESULT hr;

    *Channels = 0;
    *Rate = 0;
    *Mask = 0;
    hr = XaActivateClient(DeviceId, Category, nullptr, &Client);
    if (SUCCEEDED(hr))
        hr = Client->GetMixFormat(&Mix);
    if (SUCCEEDED(hr))
    {
        UINT32 MixChannels = Mix->nChannels;

        *Channels = MixChannels;
        *Rate = Mix->nSamplesPerSec;
        *Mask = XaMixFormatMask(Mix);

        /* A stereo mix can often take more channels, and a wide one fewer */
        Tag = XaFormatTag(Mix);
        if (Preferred != 0 && (Tag == WAVE_FORMAT_PCM || Tag == WAVE_FORMAT_IEEE_FLOAT) &&
            ((MixChannels == 2 && Preferred > 2) || (MixChannels > 2 && Preferred < MixChannels)))
        {
            UINT32 Wanted = XaMin<UINT32>(Preferred, 8);
            UINT32 Found = 0;

            if (XaEngineTakesChannels(Client, Mix, Wanted))
                Found = Wanted;
            else if (Preferred >= 3 && Preferred <= 5 && XaEngineTakesChannels(Client, Mix, 6))
                Found = 6;
            if (Found == 0 && Preferred >= 3 && Preferred <= 7 && XaEngineTakesChannels(Client, Mix, 8))
                Found = 8;
            if (Found != 0)
            {
                *Channels = Found;
                *Mask = XaPseudoMask(Found);
            }
        }
    }
    CoTaskMemFree(Mix);
    if (Client != nullptr)
        Client->Release();
    return hr;
}

XaEndpoint::XaEndpoint()
{
    Device = nullptr;
    Client = nullptr;
    Render = nullptr;
    memset(&Format, 0, sizeof(Format));
    BufferFrames = 0;
    LatencyFrames = 0;
    PeriodFrames = 0;
    QueueFrames = 0;
    Ready = nullptr;
    Running = FALSE;
}

XaEndpoint::~XaEndpoint()
{
    Close();
}

HRESULT
XaEndpoint::Open(
    _In_opt_z_ PCWSTR DeviceId,
    _In_ AUDIO_STREAM_CATEGORY Category,
    _In_ UINT32 Channels,
    _In_ DWORD ChannelMask,
    _In_ UINT32 Rate)
{
    IAudioClient2* Client2 = nullptr;
    WAVEFORMATEXTENSIBLE Wanted = {};
    WAVEFORMATEX* Closest = nullptr;
    REFERENCE_TIME DefaultPeriod = 0;
    REFERENCE_TIME MinimumPeriod = 0;
    REFERENCE_TIME Duration;
    UINT32 Packets;
    HRESULT hr;

    hr = XaActivateClient(DeviceId, Category, &Device, &Client2);
    if (FAILED(hr))
        return hr;
    Client = Client2;

    /* Ask for float at the voice rate; the closest match the engine offers wins otherwise */
    Wanted.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    Wanted.Format.nChannels = static_cast<WORD>(Channels);
    Wanted.Format.nSamplesPerSec = Rate;
    Wanted.Format.wBitsPerSample = 32;
    Wanted.Format.nBlockAlign = static_cast<WORD>(Channels * sizeof(float));
    Wanted.Format.nAvgBytesPerSec = Rate * Wanted.Format.nBlockAlign;
    Wanted.Format.cbSize = sizeof(Wanted) - sizeof(Wanted.Format);
    Wanted.Samples.wValidBitsPerSample = 32;
    Wanted.dwChannelMask = ChannelMask;
    Wanted.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    hr = Client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &Wanted.Format, &Closest);
    if (hr == S_OK)
    {
        Format = Wanted;
    }
    else if (hr == S_FALSE && Closest != nullptr)
    {
        memcpy(&Format, Closest, XaMin<SIZE_T>(sizeof(Format), sizeof(WAVEFORMATEX) + Closest->cbSize));
    }
    else
    {
        CoTaskMemFree(Closest);
        Closest = nullptr;
        hr = Client->GetMixFormat(&Closest);
        if (FAILED(hr))
            return hr;
        memcpy(&Format, Closest, XaMin<SIZE_T>(sizeof(Format), sizeof(WAVEFORMATEX) + Closest->cbSize));
    }
    CoTaskMemFree(Closest);

    hr = Client->GetDevicePeriod(&DefaultPeriod, &MinimumPeriod);
    if (FAILED(hr))
        return hr;
    Packets = DefaultPeriod >= XA_LOCAL_PERIOD_LOW && DefaultPeriod < XA_LOCAL_PERIOD_HIGH ? XA_LOCAL_PACKETS : XA_REMOTE_PACKETS;
    Duration = static_cast<REFERENCE_TIME>(Format.Format.nSamplesPerSec) * DefaultPeriod / 10000000;
    Duration = Duration * 10000000 / Format.Format.nSamplesPerSec;
    PeriodFrames = static_cast<UINT32>(Format.Format.nSamplesPerSec * Duration / 10000000);
    QueueFrames = static_cast<UINT32>(Format.Format.nSamplesPerSec * (Packets * Duration) / 10000000);
    hr = Client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, Packets * Duration, 0,
                            &Format.Format, nullptr);
    if (FAILED(hr))
    {
        XaTrace(L"endpoint: Initialize failed (%08lX)", hr);
        return hr;
    }
    hr = Client->GetBufferSize(&BufferFrames);
    if (SUCCEEDED(hr))
        hr = Client->GetService(IID_PPV_ARGS(&Render));
    if (FAILED(hr))
        return hr;
    Ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (Ready == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());
    hr = Client->SetEventHandle(Ready);
    LatencyFrames = BufferFrames;
    return hr;
}

HRESULT
XaEndpoint::Start(VOID)
{
    HRESULT hr;

    if (Running)
        return S_OK;

    /* Silence fills all but one period ahead of the first pass */
    {
        UINT32 Ahead = QueueFrames > PeriodFrames ? QueueFrames - PeriodFrames : 0;
        UINT32 Padding = 0;
        BYTE* Data;

        hr = Client->GetCurrentPadding(&Padding);
        if (FAILED(hr))
            return hr;
        Ahead -= XaMin(Padding, Ahead);
        if (Ahead != 0)
        {
            hr = Render->GetBuffer(Ahead, &Data);
            if (SUCCEEDED(hr))
                hr = Render->ReleaseBuffer(Ahead, AUDCLNT_BUFFERFLAGS_SILENT);
            if (FAILED(hr))
            {
                XaTrace(L"endpoint: prefill failed (%08lX)", hr);
                return hr;
            }
        }
    }
    hr = Client->Start();
    if (SUCCEEDED(hr))
        Running = TRUE;
    else
        XaTrace(L"endpoint: Start failed (%08lX)", hr);
    return hr;
}

VOID
XaEndpoint::Stop(VOID)
{
    if (!Running)
        return;
    Client->Stop();
    Client->Reset();
    Running = FALSE;
}

VOID
XaEndpoint::Close(VOID)
{
    if (Client != nullptr)
    {
        Stop();
        Client->Reset();
    }
    if (Render != nullptr)
    {
        Render->Release();
        Render = nullptr;
    }
    if (Client != nullptr)
    {
        Client->Release();
        Client = nullptr;
    }
    if (Device != nullptr)
    {
        Device->Release();
        Device = nullptr;
    }
    if (Ready != nullptr)
    {
        CloseHandle(Ready);
        Ready = nullptr;
    }
}
