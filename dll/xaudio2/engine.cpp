/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The engine object: voice creation, callbacks and the processing thread
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

XaEngine::XaEngine()
{
    Extension.Self = this;
    Unknown.Self = this;
    References = 1;
    InitializeCriticalSection(&ApiLock);
    CreateFlags = 0;
    Processor = 0;
    Version = 0;
    QuantumNumerator = 1;
    QuantumDenominator = 100;
    Initialized = FALSE;
    Started = FALSE;
    Voices = nullptr;
    Master = nullptr;
    SourceCount = 0;
    Callbacks = nullptr;
    CallbackCount = 0;
    CallbackCapacity = 0;
    ActiveSources = 0;
    ActiveSubmixes = 0;
    ActiveResamplers = 0;
    ActiveMatrices = 0;
    Glitches = 0;
    Thread = nullptr;
    ThreadStop = nullptr;
    RenderReady = nullptr;
    RenderThreadId = 0;
}

XaEngine::~XaEngine()
{
    StopRendering();

    /* Whatever the application left behind goes with the engine, senders first */
    while (Voices != nullptr)
    {
        XaVoice* Voice = Voices;

        for (XaVoice* Other = Voices; Other != nullptr; Other = Other->NextVoice)
        {
            if (Other->Kind == XaKindSource || (Other->Kind == XaKindSubmix && Other->SenderCount == 0))
            {
                Voice = Other;
                break;
            }
        }
        UnlinkVoice(Voice);
        Voice->Teardown();
        Voice->ReleaseSends();
        delete Voice;
    }
    delete[] Callbacks;
    if (ThreadStop != nullptr)
        CloseHandle(ThreadStop);
    DeleteCriticalSection(&ApiLock);
}

HRESULT
XaEngine::Initialize(
    _In_ UINT32 Flags,
    _In_ XAUDIO2_PROCESSOR NewProcessor,
    _In_ DWORD NewVersion)
{
    CreateFlags = Flags;
    Processor = NewProcessor;
    Version = NewVersion;
    if ((Flags & XAUDIO2_1024_QUANTUM) != 0)
    {
        QuantumNumerator = 8;
        QuantumDenominator = 375;
    }
    ThreadStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ThreadStop == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());
    Initialized = TRUE;
    Started = TRUE;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
XaEngine::QueryInterface(
    REFIID Iid,
    _COM_Outptr_ void** Object)
{
    if (Object == nullptr)
    {
        XA_TRACE_ONCE(L"QueryInterface: NULL object");
        return XAUDIO2_E_INVALID_CALL;
    }
    *Object = nullptr;
    if (IsEqualIID(Iid, __uuidof(IXAudio2)))
        *Object = static_cast<IXAudio2*>(this);
    else if (IsEqualIID(Iid, __uuidof(IXAudio2Extension)))
        *Object = static_cast<IXAudio2Extension*>(&Extension);
    else if (IsEqualIID(Iid, IID_IUnknown))
        *Object = static_cast<IUnknown*>(&Unknown);
    else
        return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

ULONG
STDMETHODCALLTYPE
XaEngine::AddRef()
{
    return InterlockedIncrement(&References);
}

ULONG
STDMETHODCALLTYPE
XaEngine::Release()
{
    LONG Count;

    if (InCallback())
        XA_TRACE_ONCE(L"Release: the engine is released from its own callback");
    Count = InterlockedDecrement(&References);
    if (Count == 0)
    {
        XaTrace(L"engine %p shut down", this);
        delete this;
    }
    return Count;
}

HRESULT
STDMETHODCALLTYPE
XaEngine::ExtensionFace::QueryInterface(
    REFIID Iid,
    _COM_Outptr_ void** Object)
{
    return Self->QueryInterface(Iid, Object);
}

ULONG
STDMETHODCALLTYPE
XaEngine::ExtensionFace::AddRef()
{
    return Self->AddRef();
}

ULONG
STDMETHODCALLTYPE
XaEngine::ExtensionFace::Release()
{
    return Self->Release();
}

void
STDMETHODCALLTYPE
XaEngine::ExtensionFace::GetProcessingQuantum(
    _Out_ UINT32* Numerator,
    _Out_ UINT32* Denominator)
{
    Self->Lock();
    if (Numerator == nullptr || Denominator == nullptr)
    {
        XA_TRACE_ONCE(L"GetProcessingQuantum: NULL argument");
    }
    else
    {
        *Numerator = Self->QuantumNumerator;
        *Denominator = Self->QuantumDenominator;
    }
    Self->Unlock();
}

void
STDMETHODCALLTYPE
XaEngine::ExtensionFace::GetProcessor(
    _Out_ XAUDIO2_PROCESSOR* Result)
{
    Self->Lock();
    if (Result == nullptr)
        XA_TRACE_ONCE(L"GetProcessor: NULL processor");
    else
        *Result = Self->Processor;
    Self->Unlock();
}

HRESULT
STDMETHODCALLTYPE
XaEngine::UnknownFace::QueryInterface(
    REFIID Iid,
    _COM_Outptr_ void** Object)
{
    return Self->QueryInterface(Iid, Object);
}

ULONG
STDMETHODCALLTYPE
XaEngine::UnknownFace::AddRef()
{
    return Self->AddRef();
}

ULONG
STDMETHODCALLTYPE
XaEngine::UnknownFace::Release()
{
    return Self->Release();
}

HRESULT
STDMETHODCALLTYPE
XaEngine::RegisterForCallbacks(
    _In_ IXAudio2EngineCallback* Callback)
{
    if (Callback == nullptr || InCallback())
    {
        XA_TRACE_ONCE(L"RegisterForCallbacks: NULL callback or called from a callback");
        return XAUDIO2_E_INVALID_CALL;
    }
    Lock();
    if (CallbackCount == CallbackCapacity)
    {
        UINT32 Capacity = CallbackCapacity != 0 ? CallbackCapacity * 2 : 4;
        IXAudio2EngineCallback** Grown = new (std::nothrow) IXAudio2EngineCallback*[Capacity];

        if (Grown == nullptr)
        {
            Unlock();
            return E_OUTOFMEMORY;
        }
        if (CallbackCount != 0)
            memcpy(Grown, Callbacks, sizeof(*Grown) * CallbackCount);
        delete[] Callbacks;
        Callbacks = Grown;
        CallbackCapacity = Capacity;
    }
    Callbacks[CallbackCount++] = Callback;
    Unlock();
    return S_OK;
}

void
STDMETHODCALLTYPE
XaEngine::UnregisterForCallbacks(
    _In_ IXAudio2EngineCallback* Callback)
{
    if (Callback == nullptr || InCallback())
    {
        XA_TRACE_ONCE(L"UnregisterForCallbacks: NULL callback or called from a callback");
        return;
    }
    Lock();
    for (UINT32 Index = 0; Index < CallbackCount; Index++)
    {
        if (Callbacks[Index] == Callback)
        {
            memmove(&Callbacks[Index], &Callbacks[Index + 1], sizeof(*Callbacks) * (CallbackCount - Index - 1));
            CallbackCount -= 1;
            break;
        }
    }
    Unlock();
}

XaVoice*
XaEngine::FindVoice(
    _In_opt_ IXAudio2Voice* Face)
{
    if (Face == nullptr)
        return nullptr;
    for (XaVoice* Voice = Voices; Voice != nullptr; Voice = Voice->NextVoice)
    {
        if (Voice->Face() == Face)
            return Voice;
    }
    return nullptr;
}

/* Voices stay in creation order, which is the order the mixer runs them in */
VOID
XaEngine::LinkVoice(
    _In_ XaVoice* Voice)
{
    XaVoice** Link = &Voices;

    while (*Link != nullptr)
        Link = &(*Link)->NextVoice;
    Voice->NextVoice = nullptr;
    *Link = Voice;
    if (Voice->Kind == XaKindMaster)
        Master = static_cast<XaMasterVoice*>(Voice);
}

VOID
XaEngine::UnlinkVoice(
    _In_ XaVoice* Voice)
{
    for (XaVoice** Link = &Voices; *Link != nullptr; Link = &(*Link)->NextVoice)
    {
        if (*Link == Voice)
        {
            *Link = Voice->NextVoice;
            break;
        }
    }
    if (Master == Voice)
        Master = nullptr;
}

HRESULT
STDMETHODCALLTYPE
XaEngine::CreateSourceVoice(
    _Outptr_ IXAudio2SourceVoice** Voice,
    _In_ const WAVEFORMATEX* Format,
    UINT32 Flags,
    float MaxRatio,
    _In_opt_ IXAudio2VoiceCallback* Callback,
    _In_opt_ const XAUDIO2_VOICE_SENDS* SendList,
    _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain)
{
    XaSourceVoice* Source;
    WORD Tag;
    HRESULT hr;

    Lock();
    if (Master == nullptr || InCallback() || Voice == nullptr || Format == nullptr)
    {
        XA_TRACE_ONCE(L"CreateSourceVoice: no mastering voice, called from a callback, or NULL argument");
        Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (!XaFormatValid(Format))
    {
        XA_TRACE_ONCE(L"CreateSourceVoice: format %04X is not valid", Format->wFormatTag);
        Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if ((Flags & ~XA_SOURCE_FLAGS) != 0)
    {
        XA_TRACE_ONCE(L"CreateSourceVoice: flags %08X", Flags);
        Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    Tag = XaFormatTag(Format);
    if (Tag == WAVE_FORMAT_ADPCM)
    {
        UINT32 PerBlock = XaAdpcmFramesPerBlock(Format);

        if (PerBlock != 32 && PerBlock != 64 && PerBlock != 128 && PerBlock != 256 && PerBlock != 512)
        {
            XA_TRACE_ONCE(L"CreateSourceVoice: ADPCM blocks of %u samples", PerBlock);
            Unlock();
            return XAUDIO2_E_INVALID_CALL;
        }
    }
    else if (Tag != WAVE_FORMAT_PCM && Tag != WAVE_FORMAT_IEEE_FLOAT && Tag != XA_TAG_WMA2 && Tag != XA_TAG_WMA3)
    {
        /* XMA needs decoder hardware this platform does not have */
        XA_TRACE_ONCE(L"CreateSourceVoice: format tag %04X is not supported", Tag);
        Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }

    Source = new (std::nothrow) XaSourceVoice(this, Callback);
    if (Source == nullptr)
    {
        Unlock();
        return E_OUTOFMEMORY;
    }
    hr = Source->Initialize(Flags, Format, MaxRatio, SendList, Chain);
    if (FAILED(hr))
    {
        delete Source;
        Unlock();
        return hr;
    }
    LinkVoice(Source);
    SourceCount += 1;
    *Voice = Source;
    Unlock();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
XaEngine::CreateSubmixVoice(
    _Outptr_ IXAudio2SubmixVoice** Voice,
    UINT32 Channels,
    UINT32 Rate,
    UINT32 Flags,
    UINT32 Stage,
    _In_opt_ const XAUDIO2_VOICE_SENDS* SendList,
    _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain)
{
    XaSubmixVoice* Submix;
    HRESULT hr;

    Lock();
    if (Master == nullptr || InCallback() || Voice == nullptr)
    {
        XA_TRACE_ONCE(L"CreateSubmixVoice: no mastering voice, called from a callback, or NULL voice");
        Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (Channels == 0 || Channels > XA_MAX_CHANNELS || Rate < XA_MIN_RATE || Rate > XA_MAX_RATE ||
        (Flags & ~XA_SUBMIX_FLAGS) != 0)
    {
        XA_TRACE_ONCE(L"CreateSubmixVoice: %u channels at %u Hz, flags %08X", Channels, Rate, Flags);
        Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    Submix = new (std::nothrow) XaSubmixVoice(this);
    if (Submix == nullptr)
    {
        Unlock();
        return E_OUTOFMEMORY;
    }
    hr = Submix->Initialize(Flags, Channels, XaQuantumRate(Rate, QuantumDenominator), Stage, SendList, Chain);
    if (FAILED(hr))
    {
        delete Submix;
        Unlock();
        return hr;
    }
    LinkVoice(Submix);
    Submix->CountActive(TRUE);
    *Voice = Submix;
    Unlock();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
XaEngine::CreateMasteringVoice(
    _Outptr_ IXAudio2MasteringVoice** Voice,
    UINT32 Channels,
    UINT32 Rate,
    UINT32 Flags,
    _In_opt_ LPCWSTR DeviceId,
    _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain,
    AUDIO_STREAM_CATEGORY Category)
{
    XaMasterVoice* NewMaster;
    PWSTR Endpoint = nullptr;
    HRESULT hr;

    Lock();
    if (Master != nullptr || InCallback() || Voice == nullptr)
    {
        XA_TRACE_ONCE(L"CreateMasteringVoice: one exists, called from a callback, or NULL voice");
        Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (Channels > XA_MAX_CHANNELS || Rate > XA_MAX_RATE || (Rate != 0 && Rate < XA_MIN_RATE) ||
        (Flags & ~XA_MASTER_FLAGS) != 0)
    {
        XA_TRACE_ONCE(L"CreateMasteringVoice: %u channels at %u Hz, flags %08X", Channels, Rate, Flags);
        Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (DeviceId == nullptr)
    {
        hr = XaDefaultDeviceId(&Endpoint);
        if ((Flags & XAUDIO2_NO_VIRTUAL_AUDIO_CLIENT) == 0)
            Flags |= XA_MASTER_DEFAULT_DEVICE;
    }
    else
    {
        hr = XaCanonicalDeviceId(DeviceId, &Endpoint);
    }
    if (SUCCEEDED(hr) && (Channels == 0 || Rate == 0))
    {
        UINT32 DeviceChannels;
        UINT32 DeviceRate;
        DWORD DeviceMask;

        /* Without effects the voice takes the widest layout the endpoint offers */
        hr = XaQueryEndpointFormat(Endpoint, Category, Chain != nullptr && Chain->EffectCount != 0 ? 0 : 0xFFFF,
                                   &DeviceChannels, &DeviceRate, &DeviceMask);
        if (SUCCEEDED(hr))
        {
            if (Channels == 0)
                Channels = DeviceChannels;
            if (Rate == 0)
                Rate = DeviceRate;
        }
    }
    if (FAILED(hr))
    {
        XaTrace(L"CreateMasteringVoice: endpoint unavailable (%08lX)", hr);
        CoTaskMemFree(Endpoint);
        Unlock();
        return hr;
    }
    Rate = XaQuantumRate(Rate, QuantumDenominator);

    NewMaster = new (std::nothrow) XaMasterVoice(this);
    if (NewMaster == nullptr)
    {
        CoTaskMemFree(Endpoint);
        Unlock();
        return E_OUTOFMEMORY;
    }
    hr = NewMaster->Initialize(Flags & ~XAUDIO2_NO_VIRTUAL_AUDIO_CLIENT, Channels, Rate, Chain);
    if (SUCCEEDED(hr))
        hr = NewMaster->OpenEndpoint(Endpoint, Category);
    CoTaskMemFree(Endpoint);
    if (FAILED(hr))
    {
        NewMaster->Teardown();
        delete NewMaster;
        Unlock();
        return hr;
    }
    LinkVoice(NewMaster);
    *Voice = NewMaster;
    if (Started)
        hr = StartRendering();
    XaTrace(L"mastering voice %u channels at %u Hz, endpoint %u channels at %u Hz", Channels, Rate,
            NewMaster->Endpoint->Format.Format.nChannels, NewMaster->Endpoint->Format.Format.nSamplesPerSec);
    Unlock();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
XaEngine::StartEngine()
{
    HRESULT hr = S_OK;

    Lock();
    if (InCallback())
    {
        XA_TRACE_ONCE(L"StartEngine: called from a callback");
        Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (!Started)
    {
        if (Master != nullptr)
            hr = StartRendering();
        if (SUCCEEDED(hr))
        {
            Started = TRUE;
            XaTrace(L"engine started");
        }
    }
    Unlock();
    return hr;
}

void
STDMETHODCALLTYPE
XaEngine::StopEngine()
{
    Lock();
    if (InCallback())
    {
        XA_TRACE_ONCE(L"StopEngine: called from a callback");
        Unlock();
        return;
    }
    if (Started)
    {
        Started = FALSE;
        Unlock();
        StopRendering();
        XaTrace(L"engine stopped");
        return;
    }
    Unlock();
}

HRESULT
STDMETHODCALLTYPE
XaEngine::CommitChanges(
    UINT32 OperationSet)
{
    Lock();
    if (OperationSet == XAUDIO2_INVALID_OPSET)
    {
        XA_TRACE_ONCE(L"CommitChanges: invalid operation set");
        Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    for (XaVoice* Voice = Voices; Voice != nullptr; Voice = Voice->NextVoice)
    {
        if (Voice->Kind == XaKindSource)
            static_cast<XaSourceVoice*>(Voice)->CommitDeferred(OperationSet);
    }
    for (XaVoice* Voice = Voices; Voice != nullptr; Voice = Voice->NextVoice)
        Voice->CommitParameters(OperationSet);
    Unlock();
    return S_OK;
}

void
STDMETHODCALLTYPE
XaEngine::GetPerformanceData(
    _Out_ XAUDIO2_PERFORMANCE_DATA* Data)
{
    Lock();
    if (Data == nullptr)
    {
        XA_TRACE_ONCE(L"GetPerformanceData: NULL data");
        Unlock();
        return;
    }
    memset(Data, 0, sizeof(*Data));
    Data->CurrentLatencyInSamples = Master != nullptr && Master->Endpoint != nullptr ? Master->Endpoint->LatencyFrames : 0;
    Data->GlitchesSinceEngineStarted = Glitches;
    Data->ActiveSourceVoiceCount = ActiveSources;
    Data->TotalSourceVoiceCount = SourceCount;
    Data->ActiveSubmixVoiceCount = ActiveSubmixes;
    Data->ActiveResamplerCount = ActiveResamplers;
    Data->ActiveMatrixMixCount = ActiveMatrices;
    Unlock();
}

void
STDMETHODCALLTYPE
XaEngine::SetDebugConfiguration(
    _In_opt_ const XAUDIO2_DEBUG_CONFIGURATION* Configuration,
    _Reserved_ void* Reserved)
{
    /* Rejected arguments are always traced once, so the configuration only gets recorded */
    Lock();
    if (Configuration == nullptr && Reserved == nullptr)
    {
        XA_TRACE_ONCE(L"SetDebugConfiguration: NULL configuration");
        Unlock();
        return;
    }
    if (Configuration != nullptr)
        DebugConfiguration = *Configuration;
    Unlock();
}

HRESULT
XaEngine::StartRendering(VOID)
{
    HRESULT hr;

    if (Thread != nullptr)
        return S_OK;
    hr = Master->Endpoint->Start();
    if (FAILED(hr))
        return hr;
    ResetEvent(ThreadStop);
    RenderReady = Master->Endpoint->Ready;
    Thread = CreateThread(nullptr, 0, RenderThread, this, 0, &RenderThreadId);
    if (Thread == nullptr)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Master->Endpoint->Stop();
        RenderThreadId = 0;
        return hr;
    }
    return S_OK;
}

/* Stops the processing thread, which needs the lock to finish a pass, so the lock is not held */
VOID
XaEngine::StopRendering(VOID)
{
    HANDLE Running = Thread;

    if (Running == nullptr)
        return;
    SetEvent(ThreadStop);
    WaitForSingleObject(Running, INFINITE);
    CloseHandle(Running);
    Lock();
    Thread = nullptr;
    RenderThreadId = 0;
    if (Master != nullptr && Master->Endpoint != nullptr)
        Master->Endpoint->Stop();
    Unlock();
}

DWORD
WINAPI
XaEngine::RenderThread(
    _In_ LPVOID Parameter)
{
    XaEngine* Engine = static_cast<XaEngine*>(Parameter);
    HANDLE Waits[2];
    DWORD Task = 0;
    HANDLE Mmcss;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Mmcss = XaJoinAudioTask(&Task);
    Waits[0] = Engine->ThreadStop;
    Waits[1] = Engine->RenderReady;
    for (;;)
    {
        DWORD Wait = WaitForMultipleObjects(ARRAYSIZE(Waits), Waits, FALSE, 200);

        if (Wait == WAIT_OBJECT_0)
            break;
        Engine->Lock();
        if (Engine->Master != nullptr)
            Engine->RenderAvailable();
        Engine->Unlock();
    }
    XaLeaveAudioTask(Mmcss);
    CoUninitialize();
    return 0;
}
