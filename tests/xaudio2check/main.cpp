/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     XAudio2 answers for every entry point and argument shape, printed for comparing runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * xaudio2check [-runtime system|reactx|<path>] [-section <name>]
 */

#include <windows.h>
#include <objbase.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <xaudio2.h>
#include <xaudio2fx.h>
#include <xapo.h>
#include <xapofx.h>
#include <x3daudio.h>

#include <cstdio>
#include <math.h>
#include <string.h>

#include <new>

/* The XMA2 header layout, which only the Xbox headers declare */
#pragma pack(push, 1)
struct XcXma2Format
{
    WAVEFORMATEX wfx;
    WORD NumStreams;
    DWORD ChannelMask;
    DWORD SamplesEncoded;
    DWORD BytesPerBlock;
    DWORD PlayBegin;
    DWORD PlayLength;
    DWORD LoopBegin;
    DWORD LoopLength;
    BYTE LoopCount;
    BYTE EncoderVersion;
    WORD BlockCount;
};
#pragma pack(pop)

typedef HRESULT (WINAPI* PXC_CREATE)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
typedef HRESULT (WINAPI* PXC_CREATE_VERSION)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR, DWORD);
typedef HRESULT (WINAPI* PXC_CREATE_SHARED)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR, const void*);
typedef HRESULT (WINAPI* PXC_CREATE_APO)(IUnknown**);
typedef HRESULT (__cdecl* PXC_CREATE_FX)(REFCLSID, IUnknown**, const void*, UINT32);

static HMODULE XcModule;
static PCWSTR XcSection;
static PXC_CREATE XcCreateFunction;

static
BOOLEAN
XcWant(
    _In_z_ PCWSTR Name)
{
    return XcSection == nullptr || _wcsicmp(XcSection, Name) == 0;
}

static
DWORD
XcBits(
    _In_ float Value)
{
    DWORD Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

static
VOID
XcFloats(
    _In_z_ PCSTR Name,
    _In_reads_(Count) const float* Values,
    _In_ UINT32 Count)
{
    printf("%s =", Name);
    for (UINT32 Index = 0; Index < Count; Index++)
        printf(" %08lX", XcBits(Values[Index]));
    printf("\n");
}

static
VOID
XcBytes(
    _In_z_ PCSTR Name,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    printf("%s =", Name);
    for (SIZE_T Index = 0; Index < Size; Index++)
        printf(" %02X", static_cast<const BYTE*>(Data)[Index]);
    printf("\n");
}

static
VOID
XcRuntimePath(
    _In_ PCWSTR Runtime,
    _Out_writes_(Count) PWSTR Path,
    _In_ DWORD Count)
{
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
    {
        GetSystemDirectoryW(Path, Count);
        wcscat_s(Path, Count, L"\\xaudio2_9.dll");
        return;
    }
    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Path, Count);
        Slash = wcsrchr(Path, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        wcscat_s(Path, Count, L"reactx\\xaudio2_9.dll");
        return;
    }
    wcscpy_s(Path, Count, Runtime);
}

static
HRESULT
XcCreate(
    _Out_ IXAudio2** Engine,
    _In_ UINT32 Flags)
{
    *Engine = nullptr;
    return XcCreateFunction(Engine, Flags, XAUDIO2_DEFAULT_PROCESSOR);
}

static
VOID
XcPcm(
    _Out_ WAVEFORMATEX* Format,
    _In_ WORD Tag,
    _In_ WORD Channels,
    _In_ DWORD Rate,
    _In_ WORD Bits)
{
    Format->wFormatTag = Tag;
    Format->nChannels = Channels;
    Format->nSamplesPerSec = Rate;
    Format->wBitsPerSample = Bits;
    Format->nBlockAlign = static_cast<WORD>(Channels * Bits / 8);
    Format->nAvgBytesPerSec = Rate * Format->nBlockAlign;
    Format->cbSize = 0;
}

static
VOID
XcExtensible(
    _Out_ WAVEFORMATEXTENSIBLE* Format,
    _In_ const GUID& SubFormat,
    _In_ WORD Channels,
    _In_ DWORD Rate,
    _In_ WORD Bits,
    _In_ WORD ValidBits,
    _In_ DWORD Mask)
{
    XcPcm(&Format->Format, WAVE_FORMAT_EXTENSIBLE, Channels, Rate, Bits);
    Format->Format.cbSize = 22;
    Format->Samples.wValidBitsPerSample = ValidBits;
    Format->dwChannelMask = Mask;
    Format->SubFormat = SubFormat;
}

static
VOID
XcDetails(
    _In_z_ PCSTR Name,
    _In_ IXAudio2Voice* Voice)
{
    XAUDIO2_VOICE_DETAILS Details;

    memset(&Details, 0xCC, sizeof(Details));
    Voice->GetVoiceDetails(&Details);
    printf("%s details = %08X %08X %u %u\n", Name, Details.CreationFlags, Details.ActiveFlags, Details.InputChannels,
           Details.InputSampleRate);
}

/* Callback events, written on the engine thread and read once the run is over */
struct XcEvent
{
    CHAR Kind;
    UINT32 Value;
};

static XcEvent XcEvents[4096];
static volatile LONG XcEventCount;
static HANDLE XcStreamEnded;

static
VOID
XcRecord(
    _In_ CHAR Kind,
    _In_ UINT32 Value)
{
    LONG Slot = InterlockedIncrement(&XcEventCount) - 1;

    if (Slot < static_cast<LONG>(ARRAYSIZE(XcEvents)))
    {
        XcEvents[Slot].Kind = Kind;
        XcEvents[Slot].Value = Value;
    }
}

class XcEngineCallback final : public IXAudio2EngineCallback
{
public:
    void
    STDMETHODCALLTYPE
    OnProcessingPassStart() override
    {
        XcRecord('P', 0);
    }

    void
    STDMETHODCALLTYPE
    OnProcessingPassEnd() override
    {
        XcRecord('p', 0);
    }

    void
    STDMETHODCALLTYPE
    OnCriticalError(
        _In_ HRESULT Error) override
    {
        XcRecord('X', static_cast<UINT32>(Error));
    }
};

class XcVoiceCallback final : public IXAudio2VoiceCallback
{
public:
    void
    STDMETHODCALLTYPE
    OnVoiceProcessingPassStart(
        _In_ UINT32 BytesRequired) override
    {
        XcRecord('V', BytesRequired);
    }

    void
    STDMETHODCALLTYPE
    OnVoiceProcessingPassEnd() override
    {
        XcRecord('v', 0);
    }

    void
    STDMETHODCALLTYPE
    OnStreamEnd() override
    {
        XcRecord('E', 0);
        if (XcStreamEnded != nullptr)
            SetEvent(XcStreamEnded);
    }

    void
    STDMETHODCALLTYPE
    OnBufferStart(
        _In_opt_ void* Context) override
    {
        XcRecord('B', static_cast<UINT32>(reinterpret_cast<ULONG_PTR>(Context)));
    }

    void
    STDMETHODCALLTYPE
    OnBufferEnd(
        _In_opt_ void* Context) override
    {
        XcRecord('b', static_cast<UINT32>(reinterpret_cast<ULONG_PTR>(Context)));
    }

    void
    STDMETHODCALLTYPE
    OnLoopEnd(
        _In_opt_ void* Context) override
    {
        XcRecord('L', static_cast<UINT32>(reinterpret_cast<ULONG_PTR>(Context)));
    }

    void
    STDMETHODCALLTYPE
    OnVoiceError(
        _In_opt_ void* Context,
        _In_ HRESULT Error) override
    {
        XcRecord('R', static_cast<UINT32>(Error));
    }
};

/* Prints the events from the first buffer event on, with runs of passes counted */
static
VOID
XcPrintEvents(
    _In_z_ PCSTR Name)
{
    LONG Count = XcEventCount;
    LONG First;
    UINT32 Passes = 0;
    UINT32 LastBytes = 0;

    if (Count > static_cast<LONG>(ARRAYSIZE(XcEvents)))
        Count = ARRAYSIZE(XcEvents);
    for (First = 0; First < Count; First++)
    {
        if (XcEvents[First].Kind == 'B')
            break;
    }
    printf("%s events", Name);
    if (First == Count)
    {
        printf(" none\n");
        return;
    }

    /* Back up to the voice pass that holds the first buffer start */
    while (First > 0 && XcEvents[First - 1].Kind != 'v' && XcEvents[First - 1].Kind != 'p')
        First -= 1;
    for (LONG Index = First; Index < Count; Index++)
    {
        CHAR Kind = XcEvents[Index].Kind;

        if (Kind == 'P' || Kind == 'p' || Kind == 'v')
            continue;
        if (Kind == 'V')
        {
            if (Passes != 0 && XcEvents[Index].Value != LastBytes)
            {
                printf(" V%ux%u", LastBytes, Passes);
                Passes = 0;
            }
            LastBytes = XcEvents[Index].Value;
            Passes += 1;
            continue;
        }
        if (Passes != 0)
        {
            printf(" V%ux%u", LastBytes, Passes);
            Passes = 0;
        }
        printf(" %c%u", Kind, XcEvents[Index].Value);
        if (Kind == 'E')
            break;
    }
    if (Passes != 0)
        printf(" V%ux%u", LastBytes, Passes);
    printf("\n");
}

static
VOID
XcResetEvents(VOID)
{
    InterlockedExchange(&XcEventCount, 0);
}

static
VOID
XcCheckExports(VOID)
{
    static const PCSTR Names[] =
    {
        "XAudio2Create", "CreateAudioReverb", "CreateAudioVolumeMeter", "CreateFX", "X3DAudioCalculate",
        "X3DAudioInitialize", "CreateAudioReverbV2_8", "XAudio2CreateV2_9", "XAudio2CreateWithVersionInfo",
        "XAudio2CreateWithSharedContexts", "DllCanUnloadNow", "DllGetClassObject",
    };

    for (UINT Index = 0; Index < ARRAYSIZE(Names); Index++)
    {
        FARPROC Named = GetProcAddress(XcModule, Names[Index]);
        int Ordinal = 0;

        for (int Probe = 1; Probe <= 16 && Named != nullptr; Probe++)
        {
            if (GetProcAddress(XcModule, MAKEINTRESOURCEA(Probe)) == Named)
            {
                Ordinal = Probe;
                break;
            }
        }
        printf("export %s = %s ordinal %d\n", Names[Index], Named != nullptr ? "present" : "missing", Ordinal);
    }
    printf("export same create = %d\n",
           GetProcAddress(XcModule, "XAudio2Create") == GetProcAddress(XcModule, "XAudio2CreateV2_9"));
}

static
VOID
XcCheckCreate(VOID)
{
    PXC_CREATE_VERSION CreateVersion = reinterpret_cast<PXC_CREATE_VERSION>(GetProcAddress(XcModule, "XAudio2CreateWithVersionInfo"));
    PXC_CREATE_SHARED CreateShared = reinterpret_cast<PXC_CREATE_SHARED>(GetProcAddress(XcModule, "XAudio2CreateWithSharedContexts"));
    static const UINT32 Processors[] = { 0, 1, 2, 3, 0x80000000, XAUDIO2_ANY_PROCESSOR };
    IXAudio2* Engine;
    HRESULT hr;

    printf("create null = %08lX\n", XcCreateFunction(nullptr, 0, XAUDIO2_DEFAULT_PROCESSOR));
    for (UINT Bit = 0; Bit < 32; Bit++)
    {
        UINT32 Flags = 1u << Bit;

        hr = XcCreate(&Engine, Flags);
        printf("create flags %08X = %08lX %d\n", Flags, hr, Engine != nullptr);
        if (SUCCEEDED(hr))
            printf("  release %lu\n", Engine->Release());
    }
    for (UINT Index = 0; Index < ARRAYSIZE(Processors); Index++)
    {
        IXAudio2Extension* Extension = nullptr;

        Engine = nullptr;
        hr = XcCreateFunction(&Engine, 0, Processors[Index]);
        printf("create processor %08X = %08lX\n", Processors[Index], hr);
        if (FAILED(hr))
            continue;
        if (SUCCEEDED(Engine->QueryInterface(__uuidof(IXAudio2Extension), reinterpret_cast<void**>(&Extension))))
        {
            XAUDIO2_PROCESSOR Processor = 0xCCCCCCCC;

            Extension->GetProcessor(&Processor);
            printf("  processor %08X\n", Processor);
            Extension->Release();
        }
        printf("  release %lu\n", Engine->Release());
    }
    if (CreateVersion != nullptr)
    {
        static const DWORD Versions[] = { 0, 1, 0x0A000000, 0x0A000005, 0x0A000006, 0x0A00FFFF, 0xFFFFFFFF };

        for (UINT Index = 0; Index < ARRAYSIZE(Versions); Index++)
        {
            Engine = nullptr;
            hr = CreateVersion(&Engine, 0, XAUDIO2_DEFAULT_PROCESSOR, Versions[Index]);
            printf("create version %08lX = %08lX\n", Versions[Index], hr);
            if (SUCCEEDED(hr))
                printf("  release %lu\n", Engine->Release());
        }
        printf("create version null = %08lX\n", CreateVersion(nullptr, 0, 1, 0));
        printf("create version flags = %08lX\n", CreateVersion(&Engine, 2, 1, 0));
    }
    if (CreateShared != nullptr)
    {
        static const BYTE Context[64] = {};

        Engine = nullptr;
        hr = CreateShared(&Engine, 0, 1, nullptr);
        printf("create shared null = %08lX\n", hr);
        if (SUCCEEDED(hr))
            printf("  release %lu\n", Engine->Release());
        Engine = nullptr;
        hr = CreateShared(&Engine, 0x400, 1, Context);
        printf("create shared noshape = %08lX\n", hr);
        if (SUCCEEDED(hr))
            printf("  release %lu\n", Engine->Release());
    }
}

static
VOID
XcCheckEngine(VOID)
{
    IXAudio2* Engine;
    IXAudio2* Again = nullptr;
    IXAudio2Extension* Extension = nullptr;
    IUnknown* Unknown = nullptr;
    IUnknown* Other = nullptr;
    XAUDIO2_PERFORMANCE_DATA Perf;
    XcEngineCallback Callback;
    IXAudio2SourceVoice* Source = nullptr;
    IXAudio2SubmixVoice* Submix = nullptr;
    WAVEFORMATEX Format;
    HRESULT hr;
    static const GUID Random = { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } };

    if (FAILED(XcCreate(&Engine, 0)))
        return;
    printf("engine addref %lu\n", Engine->AddRef());
    printf("engine release %lu\n", Engine->Release());
    printf("qi null = %08lX\n", Engine->QueryInterface(__uuidof(IXAudio2), nullptr));
    hr = Engine->QueryInterface(__uuidof(IXAudio2), reinterpret_cast<void**>(&Again));
    printf("qi xaudio2 = %08lX same %d\n", hr, Again == Engine);
    if (SUCCEEDED(hr))
        printf("  release %lu\n", Again->Release());
    hr = Engine->QueryInterface(__uuidof(IXAudio2Extension), reinterpret_cast<void**>(&Extension));
    printf("qi extension = %08lX same %d\n", hr, static_cast<void*>(Extension) == static_cast<void*>(Engine));
    hr = Engine->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&Unknown));
    printf("qi unknown = %08lX same %d\n", hr, static_cast<void*>(Unknown) == static_cast<void*>(Engine));
    if (SUCCEEDED(hr))
    {
        Again = nullptr;
        hr = Unknown->QueryInterface(__uuidof(IXAudio2), reinterpret_cast<void**>(&Again));
        printf("  unknown qi xaudio2 = %08lX %d\n", hr, Again != nullptr);
        if (SUCCEEDED(hr))
            Again->Release();
        hr = Unknown->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&Other));
        printf("  unknown qi unknown = %08lX same %d\n", hr, Other == Unknown);
        if (SUCCEEDED(hr))
            Other->Release();
        printf("  unknown addref %lu\n", Unknown->AddRef());
        printf("  unknown release %lu\n", Unknown->Release());
        printf("  unknown release %lu\n", Unknown->Release());
    }
    Other = reinterpret_cast<IUnknown*>(0x1);
    hr = Engine->QueryInterface(Random, reinterpret_cast<void**>(&Other));
    printf("qi random = %08lX %d\n", hr, Other == nullptr);
    if (Extension != nullptr)
    {
        UINT32 Numerator = 0xCCCCCCCC;
        UINT32 Denominator = 0xCCCCCCCC;

        Extension->GetProcessingQuantum(&Numerator, &Denominator);
        printf("quantum %u/%u\n", Numerator, Denominator);
        Numerator = 0xCCCCCCCC;
        Extension->GetProcessingQuantum(&Numerator, nullptr);
        Extension->GetProcessingQuantum(nullptr, &Denominator);
        printf("quantum partial %08X %08X\n", Numerator, Denominator);
        Extension->GetProcessor(nullptr);
        hr = Extension->QueryInterface(__uuidof(IXAudio2), reinterpret_cast<void**>(&Again));
        printf("extension qi xaudio2 = %08lX same %d\n", hr, Again == Engine);
        if (SUCCEEDED(hr))
            Again->Release();
        printf("extension addref %lu\n", Extension->AddRef());
        printf("extension release %lu\n", Extension->Release());
        printf("extension release %lu\n", Extension->Release());
    }

    printf("register null = %08lX\n", Engine->RegisterForCallbacks(nullptr));
    printf("register = %08lX\n", Engine->RegisterForCallbacks(&Callback));
    printf("register again = %08lX\n", Engine->RegisterForCallbacks(&Callback));
    Engine->UnregisterForCallbacks(nullptr);
    Engine->UnregisterForCallbacks(&Callback);
    Engine->UnregisterForCallbacks(&Callback);
    Engine->UnregisterForCallbacks(&Callback);

    XcPcm(&Format, WAVE_FORMAT_PCM, 2, 44100, 16);
    printf("source before master = %08lX\n", Engine->CreateSourceVoice(&Source, &Format));
    printf("submix before master = %08lX\n", Engine->CreateSubmixVoice(&Submix, 2, 48000));
    printf("start before master = %08lX\n", Engine->StartEngine());
    Engine->StopEngine();
    printf("commit = %08lX\n", Engine->CommitChanges(0));
    printf("commit 5 = %08lX\n", Engine->CommitChanges(5));
    printf("commit invalid = %08lX\n", Engine->CommitChanges(XAUDIO2_INVALID_OPSET));
    Engine->GetPerformanceData(nullptr);
    memset(&Perf, 0xCC, sizeof(Perf));
    Engine->GetPerformanceData(&Perf);
    printf("perf counts %u %u %u %u %u %u %u glitches %u\n", Perf.ActiveSourceVoiceCount, Perf.TotalSourceVoiceCount,
           Perf.ActiveSubmixVoiceCount, Perf.ActiveResamplerCount, Perf.ActiveMatrixMixCount, Perf.ActiveXmaSourceVoices,
           Perf.ActiveXmaStreams, Perf.GlitchesSinceEngineStarted);
    {
        XAUDIO2_DEBUG_CONFIGURATION Debug = {};

        Engine->SetDebugConfiguration(nullptr, nullptr);
        Debug.TraceMask = XAUDIO2_LOG_ERRORS;
        Engine->SetDebugConfiguration(&Debug, nullptr);
        printf("debug commit invalid = %08lX\n", Engine->CommitChanges(XAUDIO2_INVALID_OPSET));
        Debug.TraceMask = 0;
        Engine->SetDebugConfiguration(&Debug, nullptr);
    }
    printf("release %lu\n", Engine->Release());

    if (SUCCEEDED(XcCreate(&Engine, XAUDIO2_1024_QUANTUM)))
    {
        if (SUCCEEDED(Engine->QueryInterface(__uuidof(IXAudio2Extension), reinterpret_cast<void**>(&Extension))))
        {
            UINT32 Numerator = 0;
            UINT32 Denominator = 0;

            Extension->GetProcessingQuantum(&Numerator, &Denominator);
            printf("quantum 1024 %u/%u\n", Numerator, Denominator);
            Extension->Release();
        }
        printf("release %lu\n", Engine->Release());
    }
}

static
VOID
XcCheckMaster(VOID)
{
    static const struct
    {
        UINT32 Channels;
        UINT32 Rate;
        UINT32 Flags;
    } Cases[] =
    {
        { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 }, { 6, 0, 0 }, { 8, 0, 0 }, { 64, 0, 0 }, { 65, 0, 0 },
        { 0, 999, 0 }, { 0, 1000, 0 }, { 0, 1001, 0 }, { 0, 22050, 0 }, { 0, 32001, 0 }, { 0, 44100, 0 },
        { 0, 48000, 0 }, { 0, 96000, 0 }, { 0, 384000, 0 }, { 0, 384001, 0 }, { 0, 0, 1 }, { 0, 0, 8 },
        { 0, 0, 0x400 }, { 0, 0, 0x1000 }, { 0, 0, 0x4000 }, { 0, 0, 0x10000 }, { 0, 0, 0x20000 }, { 0, 0, 0x40000 },
        { 3, 0, 0 }, { 5, 0, 0 }, { 7, 0, 0 }, { 12, 0, 0 },
    };
    static const AUDIO_STREAM_CATEGORY Categories[] =
    {
        AudioCategory_GameEffects, AudioCategory_Other, AudioCategory_Media, AudioCategory_Communications,
        static_cast<AUDIO_STREAM_CATEGORY>(100),
    };
    IXAudio2* Engine;
    IXAudio2MasteringVoice* Master;
    IXAudio2MasteringVoice* Second;
    HRESULT hr;

    if (FAILED(XcCreate(&Engine, 0)))
        return;
    printf("master null = %08lX\n", Engine->CreateMasteringVoice(nullptr));
    for (UINT Index = 0; Index < ARRAYSIZE(Cases); Index++)
    {
        Master = nullptr;
        hr = Engine->CreateMasteringVoice(&Master, Cases[Index].Channels, Cases[Index].Rate, Cases[Index].Flags);
        printf("master %u %u %08X = %08lX\n", Cases[Index].Channels, Cases[Index].Rate, Cases[Index].Flags, hr);
        if (FAILED(hr))
            continue;
        {
            DWORD Mask = 0xCCCCCCCC;
            float Volume = -1.0f;

            HRESULT MaskResult;

            XcDetails("  master", Master);
            MaskResult = Master->GetChannelMask(&Mask);
            printf("  mask = %08lX %08lX\n", MaskResult, Mask);
            Master->GetVolume(&Volume);
            printf("  volume %08lX\n", XcBits(Volume));
        }
        Master->DestroyVoice();
    }
    for (UINT Index = 0; Index < ARRAYSIZE(Categories); Index++)
    {
        Master = nullptr;
        hr = Engine->CreateMasteringVoice(&Master, 0, 0, 0, nullptr, nullptr, Categories[Index]);
        printf("master category %d = %08lX\n", Categories[Index], hr);
        if (SUCCEEDED(hr))
            Master->DestroyVoice();
    }
    Master = nullptr;
    hr = Engine->CreateMasteringVoice(&Master, 0, 0, 0, L"bogus");
    printf("master bogus device = %08lX\n", hr);
    if (SUCCEEDED(hr))
        Master->DestroyVoice();
    Master = nullptr;
    hr = Engine->CreateMasteringVoice(&Master, 0, 0, 0, L"");
    printf("master empty device = %08lX\n", hr);
    if (SUCCEEDED(hr))
        Master->DestroyVoice();
    {
        XAUDIO2_EFFECT_CHAIN Chain = {};

        Master = nullptr;
        hr = Engine->CreateMasteringVoice(&Master, 0, 0, 0, nullptr, &Chain);
        printf("master empty chain = %08lX\n", hr);
        if (SUCCEEDED(hr))
            Master->DestroyVoice();
    }

    Master = nullptr;
    hr = Engine->CreateMasteringVoice(&Master, 2, 48000);
    printf("master 2 48000 = %08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        Second = nullptr;
        printf("second master = %08lX %d\n", Engine->CreateMasteringVoice(&Second, 2, 48000), Second != nullptr);
        printf("start = %08lX\n", Engine->StartEngine());
        printf("start again = %08lX\n", Engine->StartEngine());
        Engine->StopEngine();
        Engine->StopEngine();
        printf("start after stop = %08lX\n", Engine->StartEngine());
        {
            XAUDIO2_FILTER_PARAMETERS Filter = { LowPassFilter, 0.5f, 1.0f };
            float Volumes[2] = { 0.5f, 0.5f };
            float Matrix[4] = {};
            XAUDIO2_VOICE_SENDS Sends = {};

            printf("master setfilter = %08lX\n", Master->SetFilterParameters(&Filter));
            memset(&Filter, 0xCC, sizeof(Filter));
            Master->GetFilterParameters(&Filter);
            XcBytes("master getfilter", &Filter, sizeof(Filter));
            printf("master setchannelvolumes = %08lX\n", Master->SetChannelVolumes(2, Volumes));
            memset(Volumes, 0xCC, sizeof(Volumes));
            Master->GetChannelVolumes(2, Volumes);
            XcFloats("master getchannelvolumes", Volumes, 2);
            printf("master setoutputmatrix = %08lX\n", Master->SetOutputMatrix(nullptr, 2, 2, Matrix));
            printf("master setoutputvoices = %08lX\n", Master->SetOutputVoices(&Sends));
            printf("master setoutputvoices null = %08lX\n", Master->SetOutputVoices(nullptr));
            printf("master setoutputfilter = %08lX\n", Master->SetOutputFilterParameters(nullptr, &Filter));
            printf("master setvolume = %08lX\n", Master->SetVolume(0.25f));
            Volumes[0] = 0.0f;
            Master->GetVolume(Volumes);
            XcFloats("master getvolume", Volumes, 1);
            printf("master setvolume big = %08lX\n", Master->SetVolume(16777001.0f));
            printf("master setvolume neg = %08lX\n", Master->SetVolume(-16777000.0f));
            printf("master setvolume opset = %08lX\n", Master->SetVolume(1.0f, XAUDIO2_INVALID_OPSET));
            printf("master setvolume deferred = %08lX\n", Master->SetVolume(0.75f, 3));
            Master->GetVolume(Volumes);
            XcFloats("master getvolume deferred", Volumes, 1);
            printf("master enableeffect = %08lX\n", Master->EnableEffect(0));
            printf("master disableeffect = %08lX\n", Master->DisableEffect(0));
            printf("master seteffectparams = %08lX\n", Master->SetEffectParameters(0, Volumes, 4));
            printf("master geteffectparams = %08lX\n", Master->GetEffectParameters(0, Volumes, 4));
            printf("master seteffectchain null = %08lX\n", Master->SetEffectChain(nullptr));
        }
        Master->DestroyVoice();
    }
    printf("release %lu\n", Engine->Release());
}

static
IXAudio2*
XcEngineWithMaster(
    _Out_ IXAudio2MasteringVoice** Master,
    _In_ UINT32 Channels,
    _In_ UINT32 Rate)
{
    IXAudio2* Engine;

    *Master = nullptr;
    if (FAILED(XcCreate(&Engine, 0)))
        return nullptr;
    if (FAILED(Engine->CreateMasteringVoice(Master, Channels, Rate)))
    {
        printf("master failed\n");
        Engine->Release();
        return nullptr;
    }
    return Engine;
}

static
VOID
XcCheckSubmix(VOID)
{
    static const struct
    {
        UINT32 Channels;
        UINT32 Rate;
        UINT32 Flags;
        UINT32 Stage;
    } Cases[] =
    {
        { 0, 48000, 0, 0 }, { 1, 48000, 0, 0 }, { 64, 48000, 0, 0 }, { 65, 48000, 0, 0 }, { 2, 0, 0, 0 },
        { 2, 999, 0, 0 }, { 2, 1000, 0, 0 }, { 2, 22050, 0, 0 }, { 2, 384000, 0, 0 }, { 2, 384001, 0, 0 },
        { 2, 48000, 2, 0 }, { 2, 48000, 4, 0 }, { 2, 48000, 8, 0 }, { 2, 48000, 0x400, 0 }, { 2, 48000, 0x800, 0 },
        { 2, 48000, 0, 5 }, { 2, 48000, 0, 0xFFFFFFFF }, { 6, 44100, 8, 1 },
    };
    IXAudio2MasteringVoice* Master;
    IXAudio2SubmixVoice* Submix;
    IXAudio2SubmixVoice* Early;
    IXAudio2SubmixVoice* Late;
    IXAudio2* Engine = XcEngineWithMaster(&Master, 2, 48000);
    HRESULT hr;

    if (Engine == nullptr)
        return;
    printf("submix null = %08lX\n", Engine->CreateSubmixVoice(nullptr, 2, 48000));
    for (UINT Index = 0; Index < ARRAYSIZE(Cases); Index++)
    {
        Submix = nullptr;
        hr = Engine->CreateSubmixVoice(&Submix, Cases[Index].Channels, Cases[Index].Rate, Cases[Index].Flags, Cases[Index].Stage);
        printf("submix %u %u %08X %u = %08lX\n", Cases[Index].Channels, Cases[Index].Rate, Cases[Index].Flags, Cases[Index].Stage, hr);
        if (FAILED(hr))
            continue;
        XcDetails("  submix", Submix);
        Submix->DestroyVoice();
    }

    /* Sends between stages */
    Early = nullptr;
    Late = nullptr;
    printf("submix stage 1 = %08lX\n", Engine->CreateSubmixVoice(&Late, 2, 48000, 0, 1));
    if (Late != nullptr)
    {
        XAUDIO2_SEND_DESCRIPTOR Send = { 0, Late };
        XAUDIO2_VOICE_SENDS Sends = { 1, &Send };

        printf("submix to later stage = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 0, &Sends));
        if (Early != nullptr)
        {
            Early->DestroyVoice();
            Early = nullptr;
        }
        printf("submix to same stage = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 1, &Sends));
        if (Early != nullptr)
        {
            Early->DestroyVoice();
            Early = nullptr;
        }
        printf("submix to earlier stage = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 2, &Sends));
        if (Early != nullptr)
        {
            Early->DestroyVoice();
            Early = nullptr;
        }
        Send.Flags = XAUDIO2_SEND_USEFILTER;
        printf("submix send usefilter = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 0, &Sends));
        if (Early != nullptr)
        {
            XAUDIO2_FILTER_PARAMETERS Filter;

            memset(&Filter, 0xCC, sizeof(Filter));
            Early->GetOutputFilterParameters(Late, &Filter);
            XcBytes("  outputfilter default", &Filter, sizeof(Filter));
            Early->DestroyVoice();
            Early = nullptr;
        }
        Send.Flags = 0x100;
        printf("submix send badflag = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 0, &Sends));
        Send.Flags = 0;
        Sends.SendCount = 0;
        printf("submix send count mismatch = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 0, &Sends));
        Sends.SendCount = 1;
        Sends.pSends = nullptr;
        printf("submix send null array = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 0, &Sends));
        Sends.SendCount = 0;
        printf("submix send empty = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 0, &Sends));
        if (Early != nullptr)
        {
            XcDetails("  empty sends", Early);
            Early->DestroyVoice();
            Early = nullptr;
        }
        {
            XAUDIO2_SEND_DESCRIPTOR Pair[2] = { { 0, Late }, { 0, Late } };
            XAUDIO2_VOICE_SENDS Twice = { 2, Pair };

            printf("submix send twice = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 0, &Twice));
            if (Early != nullptr)
            {
                Early->DestroyVoice();
                Early = nullptr;
            }
            Pair[1].pOutputVoice = Master;
            printf("submix send late and master = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 0, &Twice));
            if (Early != nullptr)
            {
                Early->DestroyVoice();
                Early = nullptr;
            }
            Pair[1].pOutputVoice = nullptr;
            printf("submix send null voice = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 0, &Twice));
        }
        Late->DestroyVoice();
    }

    /* Rates on both ends of a send */
    {
        IXAudio2SubmixVoice* Slow = nullptr;
        IXAudio2SubmixVoice* Fast = nullptr;

        printf("submix slow = %08lX\n", Engine->CreateSubmixVoice(&Slow, 1, 22050, 0, 3));
        printf("submix fast = %08lX\n", Engine->CreateSubmixVoice(&Fast, 1, 96000, 0, 3));
        if (Slow != nullptr && Fast != nullptr)
        {
            XAUDIO2_SEND_DESCRIPTOR Pair[2] = { { 0, Slow }, { 0, Fast } };
            XAUDIO2_VOICE_SENDS Mixed = { 2, Pair };

            printf("submix mixed rates = %08lX\n", Engine->CreateSubmixVoice(&Early, 1, 48000, 0, 0, &Mixed));
            Mixed.SendCount = 1;
            printf("submix to slow = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000, 0, 0, &Mixed));
            if (Early != nullptr)
            {
                float Matrix[4];

                XcDetails("  to slow", Early);
                memset(Matrix, 0xCC, sizeof(Matrix));
                Early->GetOutputMatrix(Slow, 2, 1, Matrix);
                XcFloats("  to slow matrix", Matrix, 2);
                printf("  destroy slow with sender\n");
                Slow->DestroyVoice();
                XcDetails("  slow still", Slow);
                Early->DestroyVoice();
                Early = nullptr;
            }
        }
        if (Slow != nullptr)
            Slow->DestroyVoice();
        if (Fast != nullptr)
            Fast->DestroyVoice();
    }

    /* Destroying the mastering voice under a submix */
    Submix = nullptr;
    printf("submix plain = %08lX\n", Engine->CreateSubmixVoice(&Submix, 2, 48000));
    Master->DestroyVoice();
    printf("submix after master destroy = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000));
    if (Submix != nullptr)
    {
        Submix->DestroyVoice();
        Master->DestroyVoice();
    }
    printf("submix with master gone = %08lX\n", Engine->CreateSubmixVoice(&Early, 2, 48000));
    printf("release %lu\n", Engine->Release());
}

static
VOID
XcCheckSourceFormats(VOID)
{
    IXAudio2MasteringVoice* Master;
    IXAudio2* Engine = XcEngineWithMaster(&Master, 2, 48000);
    static const struct
    {
        WORD Tag;
        WORD Channels;
        DWORD Rate;
        WORD Bits;
    } Plain[] =
    {
        { WAVE_FORMAT_PCM, 1, 44100, 8 }, { WAVE_FORMAT_PCM, 2, 44100, 16 }, { WAVE_FORMAT_PCM, 2, 44100, 24 },
        { WAVE_FORMAT_PCM, 2, 44100, 32 }, { WAVE_FORMAT_PCM, 2, 44100, 12 }, { WAVE_FORMAT_PCM, 2, 44100, 20 },
        { WAVE_FORMAT_PCM, 2, 44100, 4 }, { WAVE_FORMAT_PCM, 2, 44100, 64 }, { WAVE_FORMAT_PCM, 0, 44100, 16 },
        { WAVE_FORMAT_PCM, 64, 44100, 16 }, { WAVE_FORMAT_PCM, 65, 44100, 16 }, { WAVE_FORMAT_PCM, 2, 0, 16 },
        { WAVE_FORMAT_PCM, 2, 999, 16 }, { WAVE_FORMAT_PCM, 2, 1000, 16 }, { WAVE_FORMAT_PCM, 2, 384000, 16 },
        { WAVE_FORMAT_PCM, 2, 384001, 16 }, { WAVE_FORMAT_PCM, 2, 22051, 16 }, { WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 32 },
        { WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 64 }, { WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 16 }, { WAVE_FORMAT_ALAW, 1, 8000, 8 },
        { WAVE_FORMAT_MULAW, 1, 8000, 8 }, { 0x0165, 2, 48000, 16 }, { 0x0166, 2, 48000, 16 }, { 0x0100, 2, 48000, 16 },
        { 0x0001 << 8, 2, 48000, 16 }, { 0x0003 << 8, 2, 48000, 32 }, { 0x6601, 2, 48000, 16 }, { 0x6101, 2, 48000, 16 },
    };
    IXAudio2SourceVoice* Source;
    WAVEFORMATEXTENSIBLE Format;
    HRESULT hr;

    if (Engine == nullptr)
        return;
    for (UINT Index = 0; Index < ARRAYSIZE(Plain); Index++)
    {
        Source = nullptr;
        XcPcm(&Format.Format, Plain[Index].Tag, Plain[Index].Channels, Plain[Index].Rate, Plain[Index].Bits);
        hr = Engine->CreateSourceVoice(&Source, &Format.Format);
        printf("source %04X %u %lu %u = %08lX\n", Plain[Index].Tag, Plain[Index].Channels, Plain[Index].Rate, Plain[Index].Bits, hr);
        if (SUCCEEDED(hr))
        {
            XcDetails("  source", Source);
            Source->DestroyVoice();
        }
    }

    /* Header fields that disagree with each other */
    XcPcm(&Format.Format, WAVE_FORMAT_PCM, 2, 44100, 16);
    Format.Format.nBlockAlign = 2;
    printf("source bad align = %08lX\n", Engine->CreateSourceVoice(&Source, &Format.Format));
    XcPcm(&Format.Format, WAVE_FORMAT_PCM, 2, 44100, 16);
    Format.Format.nAvgBytesPerSec = 1;
    printf("source bad avg = %08lX\n", Engine->CreateSourceVoice(&Source, &Format.Format));
    XcPcm(&Format.Format, WAVE_FORMAT_PCM, 2, 44100, 16);
    Format.Format.cbSize = 100;
    Source = nullptr;
    printf("source big cbsize = %08lX\n", Engine->CreateSourceVoice(&Source, &Format.Format));
    if (Source != nullptr)
        Source->DestroyVoice();

    {
        static const struct
        {
            const GUID* SubFormat;
            WORD Channels;
            WORD Bits;
            WORD ValidBits;
            DWORD Mask;
            WORD CbSize;
        } Ext[] =
        {
            { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 16, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 0, 3, 22 },
            { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 12, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 14, 3, 22 },
            { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 20, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 2, 32, 24, 3, 22 },
            { &KSDATAFORMAT_SUBTYPE_PCM, 2, 32, 20, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 2, 24, 20, 3, 22 },
            { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 16, 0, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 16, 7, 22 },
            { &KSDATAFORMAT_SUBTYPE_PCM, 3, 16, 16, 7, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 16, 3, 21 },
            { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 16, 3, 30 }, { &KSDATAFORMAT_SUBTYPE_PCM, 6, 16, 16, 0x3F, 22 },
            { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 16, 0x80000003, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 1, 16, 16, 0x80000000, 22 },
            { &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 2, 32, 32, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 2, 32, 0, 3, 22 },
            { &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 2, 32, 24, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 2, 64, 64, 3, 22 },
            { &KSDATAFORMAT_SUBTYPE_ADPCM, 2, 4, 4, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_MULAW, 1, 8, 8, 4, 22 },
            { &KSDATAFORMAT_SUBTYPE_ANALOG, 2, 16, 16, 3, 22 },
        };

        for (UINT Index = 0; Index < ARRAYSIZE(Ext); Index++)
        {
            Source = nullptr;
            XcExtensible(&Format, *Ext[Index].SubFormat, Ext[Index].Channels, 48000, Ext[Index].Bits, Ext[Index].ValidBits,
                         Ext[Index].Mask);
            Format.Format.cbSize = Ext[Index].CbSize;
            hr = Engine->CreateSourceVoice(&Source, &Format.Format);
            printf("source ext %lu %u %u %u %08lX %u = %08lX\n", Ext[Index].SubFormat->Data1, Ext[Index].Channels, Ext[Index].Bits,
                   Ext[Index].ValidBits, Ext[Index].Mask, Ext[Index].CbSize, hr);
            if (SUCCEEDED(hr))
            {
                float Matrix[64];

                XcDetails("  source", Source);
                memset(Matrix, 0xCC, sizeof(Matrix));
                Source->GetOutputMatrix(Master, Ext[Index].Channels, 2, Matrix);
                XcFloats("  matrix", Matrix, Ext[Index].Channels * 2);
                Source->DestroyVoice();
            }
        }
    }

    /* ADPCM with the standard coefficients */
    {
        BYTE Storage[sizeof(ADPCMWAVEFORMAT) + 7 * sizeof(ADPCMCOEFSET)];
        ADPCMWAVEFORMAT* Adpcm = reinterpret_cast<ADPCMWAVEFORMAT*>(Storage);
        static const SHORT Coef1[7] = { 256, 512, 0, 192, 240, 460, 392 };
        static const SHORT Coef2[7] = { 0, -256, 0, 64, 0, -208, -232 };
        static const WORD PerBlock[] = { 32, 64, 128, 256, 512, 1024, 33, 0 };

        for (UINT Index = 0; Index < ARRAYSIZE(PerBlock); Index++)
        {
            for (WORD Channels = 1; Channels <= 3; Channels++)
            {
                memset(Storage, 0, sizeof(Storage));
                Adpcm->wfx.wFormatTag = WAVE_FORMAT_ADPCM;
                Adpcm->wfx.nChannels = Channels;
                Adpcm->wfx.nSamplesPerSec = 22050;
                Adpcm->wfx.wBitsPerSample = 4;
                Adpcm->wSamplesPerBlock = PerBlock[Index];
                Adpcm->wfx.nBlockAlign = static_cast<WORD>((PerBlock[Index] > 2 ? (PerBlock[Index] - 2) * 4 / 8 : 0) * Channels / 1 + 7 * Channels);
                Adpcm->wfx.nBlockAlign = static_cast<WORD>(7 * Channels + (PerBlock[Index] >= 2 ? (PerBlock[Index] - 2) * Channels / 2 : 0));
                Adpcm->wfx.nAvgBytesPerSec = 22050 / (PerBlock[Index] != 0 ? PerBlock[Index] : 1) * Adpcm->wfx.nBlockAlign;
                Adpcm->wfx.cbSize = 32;
                Adpcm->wNumCoef = 7;
                for (UINT Coef = 0; Coef < 7; Coef++)
                {
                    Adpcm->aCoef[Coef].iCoef1 = Coef1[Coef];
                    Adpcm->aCoef[Coef].iCoef2 = Coef2[Coef];
                }
                Source = nullptr;
                hr = Engine->CreateSourceVoice(&Source, &Adpcm->wfx);
                printf("source adpcm %u %u align %u = %08lX\n", Channels, PerBlock[Index], Adpcm->wfx.nBlockAlign, hr);
                if (SUCCEEDED(hr))
                {
                    XcDetails("  adpcm", Source);
                    Source->DestroyVoice();
                }
            }
        }
        Adpcm->wfx.nChannels = 1;
        Adpcm->wSamplesPerBlock = 64;
        Adpcm->wfx.nBlockAlign = 7 + 31;
        Adpcm->aCoef[3].iCoef1 = 100;
        printf("source adpcm bad coef = %08lX\n", Engine->CreateSourceVoice(&Source, &Adpcm->wfx));
    }

    /* xWMA headers */
    {
        static const struct
        {
            WORD Tag;
            WORD Bits;
            WORD Align;
            DWORD Avg;
        } Wma[] =
        {
            { 0x0161, 16, 2230, 6000 }, { 0x0162, 16, 2230, 6000 }, { 0x0161, 8, 2230, 6000 }, { 0x0161, 16, 0, 6000 },
            { 0x0161, 16, 2230, 0 },
        };

        for (UINT Index = 0; Index < ARRAYSIZE(Wma); Index++)
        {
            XcPcm(&Format.Format, Wma[Index].Tag, 2, 44100, Wma[Index].Bits);
            Format.Format.nBlockAlign = Wma[Index].Align;
            Format.Format.nAvgBytesPerSec = Wma[Index].Avg;
            Source = nullptr;
            hr = Engine->CreateSourceVoice(&Source, &Format.Format);
            printf("source wma %04X %u %u %lu = %08lX\n", Wma[Index].Tag, Wma[Index].Bits, Wma[Index].Align, Wma[Index].Avg, hr);
            if (SUCCEEDED(hr))
            {
                XcDetails("  wma", Source);
                Source->DestroyVoice();
            }
        }
    }

    /* XMA2 is refused without the decoder hardware */
    {
        XcXma2Format Xma = {};

        Xma.wfx.wFormatTag = 0x0166;
        Xma.wfx.nChannels = 2;
        Xma.wfx.nSamplesPerSec = 48000;
        Xma.wfx.wBitsPerSample = 16;
        Xma.wfx.nBlockAlign = 4;
        Xma.wfx.nAvgBytesPerSec = 192000;
        Xma.wfx.cbSize = 34;
        Xma.NumStreams = 1;
        Xma.ChannelMask = 3;
        Xma.SamplesEncoded = 48000;
        Xma.BytesPerBlock = 0x10000;
        Xma.PlayLength = 48000;
        Xma.EncoderVersion = 4;
        Xma.BlockCount = 1;
        printf("source xma2 = %08lX\n", Engine->CreateSourceVoice(&Source, &Xma.wfx));
        printf("source xma2 noshape = %08lX\n", Engine->CreateSourceVoice(&Source, &Xma.wfx, 0x400));
    }
    printf("release %lu\n", Engine->Release());
}

static
VOID
XcCheckSourceArguments(VOID)
{
    static const UINT32 FlagCases[] = { 0, 1, 2, 4, 6, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x40E, 0x1000 };
    static const float Ratios[] = { 0.0f, 0.0009765f, 1.0f / 1024.0f, 0.5f, 1.0f, 2.0f, 1024.0f, 1024.5f, -1.0f };
    IXAudio2MasteringVoice* Master;
    IXAudio2* Engine = XcEngineWithMaster(&Master, 2, 48000);
    IXAudio2SourceVoice* Source;
    WAVEFORMATEX Format;
    XcVoiceCallback Callback;
    HRESULT hr;

    if (Engine == nullptr)
        return;
    XcPcm(&Format, WAVE_FORMAT_PCM, 1, 44100, 16);
    printf("source null = %08lX\n", Engine->CreateSourceVoice(nullptr, &Format));
    printf("source null format = %08lX\n", Engine->CreateSourceVoice(&Source, nullptr));
    for (UINT Index = 0; Index < ARRAYSIZE(FlagCases); Index++)
    {
        Source = nullptr;
        hr = Engine->CreateSourceVoice(&Source, &Format, FlagCases[Index]);
        printf("source flags %08X = %08lX\n", FlagCases[Index], hr);
        if (SUCCEEDED(hr))
        {
            float Ratio = -1.0f;

            XcDetails("  source", Source);
            Source->GetFrequencyRatio(&Ratio);
            printf("  ratio %08lX setratio = %08lX\n", XcBits(Ratio), Source->SetFrequencyRatio(1.5f));
            Source->DestroyVoice();
        }
    }
    for (UINT Index = 0; Index < ARRAYSIZE(Ratios); Index++)
    {
        Source = nullptr;
        hr = Engine->CreateSourceVoice(&Source, &Format, 0, Ratios[Index]);
        printf("source maxratio %08lX = %08lX\n", XcBits(Ratios[Index]), hr);
        if (SUCCEEDED(hr))
        {
            static const float Sets[] = { 0.0f, 0.0001f, 0.5f, 1.9f, 2.0f, 3.0f, 2000.0f, -5.0f };

            for (UINT Set = 0; Set < ARRAYSIZE(Sets); Set++)
            {
                float Ratio = -1.0f;

                hr = Source->SetFrequencyRatio(Sets[Set]);
                Source->GetFrequencyRatio(&Ratio);
                printf("  set %08lX = %08lX get %08lX\n", XcBits(Sets[Set]), hr, XcBits(Ratio));
            }
            Source->DestroyVoice();
        }
    }

    /* NOSRC with rates that do and do not match the mastering voice */
    {
        static const DWORD Rates[] = { 48000, 44100, 22050, 96000, 48050 };

        for (UINT Index = 0; Index < ARRAYSIZE(Rates); Index++)
        {
            WAVEFORMATEX Other;

            XcPcm(&Other, WAVE_FORMAT_PCM, 1, Rates[Index], 16);
            Source = nullptr;
            hr = Engine->CreateSourceVoice(&Source, &Other, XAUDIO2_VOICE_NOSRC);
            printf("source nosrc %lu = %08lX\n", Rates[Index], hr);
            if (SUCCEEDED(hr))
            {
                XcDetails("  nosrc", Source);
                printf("  setsourcerate 44100 = %08lX\n", Source->SetSourceSampleRate(44100));
                printf("  setsourcerate same = %08lX\n", Source->SetSourceSampleRate(Rates[Index]));
                Source->DestroyVoice();
            }
        }
    }

    /* SetSourceSampleRate on an ordinary voice */
    Source = nullptr;
    if (SUCCEEDED(Engine->CreateSourceVoice(&Source, &Format, 0, 2.0f, &Callback)))
    {
        static const UINT32 Rates[] = { 0, 999, 1000, 22050, 96000, 384000, 384001, 44100 };

        for (UINT Index = 0; Index < ARRAYSIZE(Rates); Index++)
        {
            printf("setsourcerate %u = %08lX\n", Rates[Index], Source->SetSourceSampleRate(Rates[Index]));
            XcDetails("  after", Source);
        }
        Source->DestroyVoice();
    }

    /* Sends to voices of other rates */
    {
        IXAudio2SubmixVoice* Slow = nullptr;
        IXAudio2SubmixVoice* Mono = nullptr;

        Engine->CreateSubmixVoice(&Slow, 2, 22050);
        Engine->CreateSubmixVoice(&Mono, 1, 48000);
        if (Slow != nullptr && Mono != nullptr)
        {
            XAUDIO2_SEND_DESCRIPTOR Pair[2] = { { 0, Slow }, { 0, Master } };
            XAUDIO2_VOICE_SENDS Sends = { 1, Pair };

            Source = nullptr;
            hr = Engine->CreateSourceVoice(&Source, &Format, 0, 2.0f, nullptr, &Sends);
            printf("source to slow = %08lX\n", hr);
            if (SUCCEEDED(hr))
            {
                XcDetails("  to slow", Source);
                Pair[0].pOutputVoice = Mono;
                printf("  setoutputvoices mono = %08lX\n", Source->SetOutputVoices(&Sends));
                Sends.SendCount = 2;
                Pair[1].pOutputVoice = Master;
                printf("  setoutputvoices mono+master = %08lX\n", Source->SetOutputVoices(&Sends));
                {
                    float Matrix[2];

                    memset(Matrix, 0xCC, sizeof(Matrix));
                    Source->GetOutputMatrix(Master, 1, 2, Matrix);
                    XcFloats("  matrix master", Matrix, 2);
                    Source->GetOutputMatrix(Mono, 1, 1, Matrix);
                    XcFloats("  matrix mono", Matrix, 1);
                    printf("  setoutputmatrix null dest = %08lX\n", Source->SetOutputMatrix(nullptr, 1, 2, Matrix));
                }
                Pair[1].pOutputVoice = Slow;
                printf("  setoutputvoices mono+slow = %08lX\n", Source->SetOutputVoices(&Sends));
                printf("  setoutputvoices null = %08lX\n", Source->SetOutputVoices(nullptr));
                Sends.SendCount = 0;
                printf("  setoutputvoices none = %08lX\n", Source->SetOutputVoices(&Sends));
                {
                    float Matrix[2] = { 1.0f, 1.0f };

                    printf("  setoutputmatrix no sends = %08lX\n", Source->SetOutputMatrix(nullptr, 1, 2, Matrix));
                }
                Source->DestroyVoice();
            }
            Pair[0].pOutputVoice = Source;
        }
        if (Slow != nullptr)
            Slow->DestroyVoice();
        if (Mono != nullptr)
            Mono->DestroyVoice();
    }

    /* A source voice is not a destination */
    {
        IXAudio2SourceVoice* First = nullptr;

        if (SUCCEEDED(Engine->CreateSourceVoice(&First, &Format)))
        {
            XAUDIO2_SEND_DESCRIPTOR Send = { 0, First };
            XAUDIO2_VOICE_SENDS Sends = { 1, &Send };

            printf("source to source = %08lX\n", Engine->CreateSourceVoice(&Source, &Format, 0, 2.0f, nullptr, &Sends));
            First->DestroyVoice();
        }
    }
    printf("release %lu\n", Engine->Release());
}

static
VOID
XcCheckMatrices(VOID)
{
    static const UINT32 MasterChannels[] = { 1, 2, 4, 6, 8 };
    static const DWORD SourceMasks[] =
    {
        0, SPEAKER_FRONT_CENTER, SPEAKER_FRONT_LEFT, SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT, KSAUDIO_SPEAKER_QUAD,
        KSAUDIO_SPEAKER_5POINT1, KSAUDIO_SPEAKER_7POINT1_SURROUND, KSAUDIO_SPEAKER_5POINT1_SURROUND,
        SPEAKER_FRONT_LEFT | SPEAKER_BACK_CENTER, SPEAKER_LOW_FREQUENCY, SPEAKER_TOP_CENTER | SPEAKER_FRONT_LEFT,
    };

    for (UINT MasterIndex = 0; MasterIndex < ARRAYSIZE(MasterChannels); MasterIndex++)
    {
        IXAudio2MasteringVoice* Master;
        IXAudio2* Engine = XcEngineWithMaster(&Master, MasterChannels[MasterIndex], 48000);

        if (Engine == nullptr)
            continue;
        {
            DWORD MasterMask = 0;

            Master->GetChannelMask(&MasterMask);
            printf("matrix master %u mask %08lX\n", MasterChannels[MasterIndex], MasterMask);
        }
        for (WORD Channels = 1; Channels <= 8; Channels++)
        {
            IXAudio2SourceVoice* Source = nullptr;
            WAVEFORMATEX Format;
            float Matrix[64];

            XcPcm(&Format, WAVE_FORMAT_PCM, Channels, 48000, 16);
            if (FAILED(Engine->CreateSourceVoice(&Source, &Format)))
                continue;
            memset(Matrix, 0xCC, sizeof(Matrix));
            Source->GetOutputMatrix(Master, Channels, MasterChannels[MasterIndex], Matrix);
            printf("  plain %u:", Channels);
            XcFloats("", Matrix, Channels * MasterChannels[MasterIndex]);
            Source->DestroyVoice();
        }
        for (UINT MaskIndex = 0; MaskIndex < ARRAYSIZE(SourceMasks); MaskIndex++)
        {
            IXAudio2SourceVoice* Source = nullptr;
            WAVEFORMATEXTENSIBLE Format;
            WORD Channels = 0;
            float Matrix[64];

            for (DWORD Bits = SourceMasks[MaskIndex]; Bits != 0; Bits &= Bits - 1)
                Channels += 1;
            if (Channels == 0)
                Channels = 3;
            XcExtensible(&Format, KSDATAFORMAT_SUBTYPE_PCM, Channels, 48000, 16, 16, SourceMasks[MaskIndex]);
            if (FAILED(Engine->CreateSourceVoice(&Source, &Format.Format)))
            {
                printf("  mask %08lX refused\n", SourceMasks[MaskIndex]);
                continue;
            }
            memset(Matrix, 0xCC, sizeof(Matrix));
            Source->GetOutputMatrix(Master, Channels, MasterChannels[MasterIndex], Matrix);
            printf("  mask %08lX:", SourceMasks[MaskIndex]);
            XcFloats("", Matrix, Channels * MasterChannels[MasterIndex]);
            Source->DestroyVoice();
        }
        for (UINT32 Channels = 1; Channels <= 8; Channels++)
        {
            IXAudio2SubmixVoice* Submix = nullptr;
            float Matrix[64];

            if (FAILED(Engine->CreateSubmixVoice(&Submix, Channels, 48000)))
                continue;
            memset(Matrix, 0xCC, sizeof(Matrix));
            Submix->GetOutputMatrix(Master, Channels, MasterChannels[MasterIndex], Matrix);
            printf("  submix %u:", Channels);
            XcFloats("", Matrix, Channels * MasterChannels[MasterIndex]);
            Submix->DestroyVoice();
        }
        printf("  release %lu\n", Engine->Release());
    }
}

static
VOID
XcCheckMethods(VOID)
{
    IXAudio2MasteringVoice* Master;
    IXAudio2* Engine = XcEngineWithMaster(&Master, 2, 48000);
    IXAudio2SourceVoice* Source = nullptr;
    IXAudio2SourceVoice* Filtered = nullptr;
    IXAudio2SubmixVoice* Submix = nullptr;
    WAVEFORMATEX Format;
    float Values[16];

    if (Engine == nullptr)
        return;
    XcPcm(&Format, WAVE_FORMAT_PCM, 2, 44100, 16);
    Engine->CreateSubmixVoice(&Submix, 2, 48000, XAUDIO2_VOICE_USEFILTER);
    Engine->CreateSourceVoice(&Source, &Format);
    Engine->CreateSourceVoice(&Filtered, &Format, XAUDIO2_VOICE_USEFILTER);
    if (Source == nullptr || Filtered == nullptr || Submix == nullptr)
    {
        printf("voices failed\n");
        return;
    }
    XcDetails("source", Source);
    XcDetails("filtered", Filtered);
    XcDetails("submix", Submix);
    Source->GetVoiceDetails(nullptr);

    /* Volume */
    Source->GetVolume(nullptr);
    Values[0] = -1.0f;
    Source->GetVolume(Values);
    XcFloats("volume default", Values, 1);
    {
        static const float Volumes[] = { 0.5f, 0.0f, -1.0f, 16777000.0f, 16777001.0f, -16777001.0f, 2.0f };

        for (UINT Index = 0; Index < ARRAYSIZE(Volumes); Index++)
        {
            HRESULT hr = Source->SetVolume(Volumes[Index]);

            Source->GetVolume(Values);
            printf("setvolume %08lX = %08lX get %08lX\n", XcBits(Volumes[Index]), hr, XcBits(Values[0]));
        }
        printf("setvolume nan = %08lX\n", Source->SetVolume(NAN));
        printf("setvolume opset = %08lX\n", Source->SetVolume(1.0f, XAUDIO2_INVALID_OPSET));
        printf("setvolume deferred = %08lX\n", Source->SetVolume(0.25f, 7));
        Source->GetVolume(Values);
        XcFloats("volume deferred", Values, 1);
        printf("commit 7 = %08lX\n", Engine->CommitChanges(7));
        Source->GetVolume(Values);
        XcFloats("volume committed", Values, 1);
        printf("submix setvolume = %08lX\n", Submix->SetVolume(0.5f));
        Submix->GetVolume(Values);
        XcFloats("submix volume", Values, 1);
        Source->SetVolume(1.0f);
    }

    /* Channel volumes */
    Source->GetChannelVolumes(2, nullptr);
    memset(Values, 0xCC, sizeof(Values));
    Source->GetChannelVolumes(2, Values);
    XcFloats("channelvolumes default", Values, 4);
    memset(Values, 0xCC, sizeof(Values));
    Source->GetChannelVolumes(1, Values);
    XcFloats("channelvolumes 1", Values, 4);
    memset(Values, 0xCC, sizeof(Values));
    Source->GetChannelVolumes(3, Values);
    XcFloats("channelvolumes 3", Values, 4);
    {
        float Set[3] = { 0.25f, -2.0f, 3.0f };

        printf("setchannelvolumes = %08lX\n", Source->SetChannelVolumes(2, Set));
        Source->GetChannelVolumes(2, Values);
        XcFloats("channelvolumes after", Values, 2);
        printf("setchannelvolumes 3 = %08lX\n", Source->SetChannelVolumes(3, Set));
        printf("setchannelvolumes 1 = %08lX\n", Source->SetChannelVolumes(1, Set));
        printf("setchannelvolumes null = %08lX\n", Source->SetChannelVolumes(2, nullptr));
        printf("setchannelvolumes opset = %08lX\n", Source->SetChannelVolumes(2, Set, XAUDIO2_INVALID_OPSET));
        Set[0] = 1e30f;
        printf("setchannelvolumes huge = %08lX\n", Source->SetChannelVolumes(2, Set));
        Set[0] = NAN;
        printf("setchannelvolumes nan = %08lX\n", Source->SetChannelVolumes(2, Set));
        Set[0] = 0.5f;
        Set[1] = 0.5f;
        printf("setchannelvolumes deferred = %08lX\n", Source->SetChannelVolumes(2, Set, 9));
        Source->GetChannelVolumes(2, Values);
        XcFloats("channelvolumes deferred", Values, 2);
        printf("submix setchannelvolumes = %08lX\n", Submix->SetChannelVolumes(2, Set));
        Submix->GetChannelVolumes(2, Values);
        XcFloats("submix channelvolumes", Values, 2);
    }

    /* Output matrix */
    memset(Values, 0xCC, sizeof(Values));
    Source->GetOutputMatrix(Master, 2, 2, Values);
    XcFloats("matrix default", Values, 4);
    Source->GetOutputMatrix(nullptr, 2, 2, Values);
    XcFloats("matrix null dest", Values, 4);
    memset(Values, 0xCC, sizeof(Values));
    Source->GetOutputMatrix(Master, 2, 1, Values);
    XcFloats("matrix 2x1", Values, 4);
    Source->GetOutputMatrix(Master, 1, 2, Values);
    XcFloats("matrix 1x2", Values, 4);
    Source->GetOutputMatrix(Submix, 2, 2, Values);
    XcFloats("matrix not sent", Values, 4);
    Source->GetOutputMatrix(Master, 2, 2, nullptr);
    {
        float Set[8] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f };

        printf("setoutputmatrix = %08lX\n", Source->SetOutputMatrix(Master, 2, 2, Set));
        Source->GetOutputMatrix(Master, 2, 2, Values);
        XcFloats("matrix after", Values, 4);
        printf("setoutputmatrix null dest = %08lX\n", Source->SetOutputMatrix(nullptr, 2, 2, Set));
        printf("setoutputmatrix small = %08lX\n", Source->SetOutputMatrix(Master, 2, 1, Set + 4));
        Source->GetOutputMatrix(Master, 2, 2, Values);
        XcFloats("matrix small", Values, 4);
        printf("setoutputmatrix big = %08lX\n", Source->SetOutputMatrix(Master, 2, 3, Set));
        printf("setoutputmatrix source channels = %08lX\n", Source->SetOutputMatrix(Master, 1, 2, Set));
        printf("setoutputmatrix null = %08lX\n", Source->SetOutputMatrix(Master, 2, 2, nullptr));
        printf("setoutputmatrix opset = %08lX\n", Source->SetOutputMatrix(Master, 2, 2, Set, XAUDIO2_INVALID_OPSET));
        printf("setoutputmatrix not sent = %08lX\n", Source->SetOutputMatrix(Submix, 2, 2, Set));
        Set[0] = 1e30f;
        Set[1] = -1e30f;
        printf("setoutputmatrix huge = %08lX\n", Source->SetOutputMatrix(Master, 2, 2, Set));
        Source->GetOutputMatrix(Master, 2, 2, Values);
        XcFloats("matrix huge", Values, 4);
        Set[0] = 0.9f;
        printf("setoutputmatrix deferred = %08lX\n", Source->SetOutputMatrix(Master, 2, 2, Set, 11));
        Source->GetOutputMatrix(Master, 2, 2, Values);
        XcFloats("matrix deferred", Values, 4);
    }

    /* Filters */
    {
        static const XAUDIO2_FILTER_PARAMETERS Filters[] =
        {
            { LowPassFilter, 0.5f, 1.0f }, { HighPassFilter, 0.0f, 1.5f }, { BandPassFilter, 1.0f, 0.1f },
            { NotchFilter, 0.3f, 1.0f }, { LowPassOnePoleFilter, 0.2f, 1.0f }, { HighPassOnePoleFilter, 0.2f, 0.5f },
            { static_cast<XAUDIO2_FILTER_TYPE>(6), 0.5f, 1.0f }, { LowPassFilter, -0.1f, 1.0f }, { LowPassFilter, 1.01f, 1.0f },
            { LowPassFilter, 0.5f, 0.0f }, { LowPassFilter, 0.5f, 1.51f }, { LowPassFilter, 0.5f, -1.0f },
            { LowPassFilter, NAN, 1.0f },
        };
        XAUDIO2_FILTER_PARAMETERS Filter;

        memset(&Filter, 0xCC, sizeof(Filter));
        Source->GetFilterParameters(&Filter);
        XcBytes("filter nofilter", &Filter, sizeof(Filter));
        memset(&Filter, 0xCC, sizeof(Filter));
        Filtered->GetFilterParameters(&Filter);
        XcBytes("filter default", &Filter, sizeof(Filter));
        Filtered->GetFilterParameters(nullptr);
        printf("setfilter nofilter = %08lX\n", Source->SetFilterParameters(&Filters[0]));
        printf("setfilter null = %08lX\n", Filtered->SetFilterParameters(nullptr));
        printf("setfilter opset = %08lX\n", Filtered->SetFilterParameters(&Filters[0], XAUDIO2_INVALID_OPSET));
        for (UINT Index = 0; Index < ARRAYSIZE(Filters); Index++)
        {
            HRESULT hr = Filtered->SetFilterParameters(&Filters[Index]);

            memset(&Filter, 0xCC, sizeof(Filter));
            Filtered->GetFilterParameters(&Filter);
            printf("setfilter %u = %08lX", Index, hr);
            XcBytes(" get", &Filter, sizeof(Filter));
        }
        printf("setfilter deferred = %08lX\n", Filtered->SetFilterParameters(&Filters[1], 13));
        Filtered->GetFilterParameters(&Filter);
        XcBytes("filter deferred", &Filter, sizeof(Filter));
        memset(&Filter, 0xCC, sizeof(Filter));
        Submix->GetFilterParameters(&Filter);
        XcBytes("submix filter default", &Filter, sizeof(Filter));

        memset(&Filter, 0xCC, sizeof(Filter));
        Source->GetOutputFilterParameters(Master, &Filter);
        XcBytes("outputfilter nofilter", &Filter, sizeof(Filter));
        printf("setoutputfilter nofilter = %08lX\n", Source->SetOutputFilterParameters(Master, &Filters[0]));
        printf("setoutputfilter null dest = %08lX\n", Source->SetOutputFilterParameters(nullptr, &Filters[0]));
    }

    /* Output filters on a send that has one */
    {
        IXAudio2SourceVoice* Sending = nullptr;
        XAUDIO2_SEND_DESCRIPTOR Pair[2] = { { XAUDIO2_SEND_USEFILTER, Master }, { 0, Submix } };
        XAUDIO2_VOICE_SENDS Sends = { 2, Pair };

        if (SUCCEEDED(Engine->CreateSourceVoice(&Sending, &Format, 0, 2.0f, nullptr, &Sends)))
        {
            XAUDIO2_FILTER_PARAMETERS Filter = { HighPassFilter, 0.25f, 0.75f };

            memset(Values, 0xCC, sizeof(Values));
            Sending->GetOutputFilterParameters(Master, reinterpret_cast<XAUDIO2_FILTER_PARAMETERS*>(Values));
            XcBytes("sendfilter default", Values, 12);
            printf("setoutputfilter = %08lX\n", Sending->SetOutputFilterParameters(Master, &Filter));
            Sending->GetOutputFilterParameters(Master, reinterpret_cast<XAUDIO2_FILTER_PARAMETERS*>(Values));
            XcBytes("sendfilter after", Values, 12);
            printf("setoutputfilter nofilter send = %08lX\n", Sending->SetOutputFilterParameters(Submix, &Filter));
            printf("setoutputfilter null dest = %08lX\n", Sending->SetOutputFilterParameters(nullptr, &Filter));
            printf("setoutputfilter null = %08lX\n", Sending->SetOutputFilterParameters(Master, nullptr));
            printf("setoutputfilter opset = %08lX\n", Sending->SetOutputFilterParameters(Master, &Filter, XAUDIO2_INVALID_OPSET));
            Filter.Frequency = 2.0f;
            printf("setoutputfilter bad = %08lX\n", Sending->SetOutputFilterParameters(Master, &Filter));
            Filter.Frequency = 0.5f;
            printf("setoutputfilter deferred = %08lX\n", Sending->SetOutputFilterParameters(Master, &Filter, 3));
            Sending->GetOutputFilterParameters(Master, reinterpret_cast<XAUDIO2_FILTER_PARAMETERS*>(Values));
            XcBytes("sendfilter deferred", Values, 12);
            memset(Values, 0xCC, sizeof(Values));
            Sending->GetOutputMatrix(Submix, 2, 2, Values);
            XcFloats("matrix to submix", Values, 4);
            Sending->DestroyVoice();
        }
    }

    /* Effect methods with no chain */
    printf("enableeffect = %08lX\n", Source->EnableEffect(0));
    printf("disableeffect = %08lX\n", Source->DisableEffect(0));
    {
        BOOL Enabled = 7;

        Source->GetEffectState(0, &Enabled);
        printf("geteffectstate %d\n", Enabled);
    }
    printf("seteffectparams = %08lX\n", Source->SetEffectParameters(0, Values, 4));
    printf("geteffectparams = %08lX\n", Source->GetEffectParameters(0, Values, 4));
    printf("seteffectchain null = %08lX\n", Source->SetEffectChain(nullptr));
    {
        XAUDIO2_EFFECT_CHAIN Chain = {};

        printf("seteffectchain empty = %08lX\n", Source->SetEffectChain(&Chain));
        Chain.EffectCount = 1;
        printf("seteffectchain null array = %08lX\n", Source->SetEffectChain(&Chain));
    }

    Filtered->DestroyVoice();
    Source->DestroyVoice();
    printf("submix destroy\n");
    Submix->DestroyVoice();
    printf("release %lu\n", Engine->Release());
}

static
VOID
XcCheckBuffers(VOID)
{
    IXAudio2MasteringVoice* Master;
    IXAudio2* Engine = XcEngineWithMaster(&Master, 2, 48000);
    IXAudio2SourceVoice* Source = nullptr;
    static BYTE Data[4800 * 4];
    WAVEFORMATEX Format;
    XAUDIO2_BUFFER Buffer;
    XAUDIO2_VOICE_STATE State;
    HRESULT hr;

    if (Engine == nullptr)
        return;
    XcPcm(&Format, WAVE_FORMAT_PCM, 2, 48000, 16);
    if (FAILED(Engine->CreateSourceVoice(&Source, &Format)))
        return;

    static const struct
    {
        UINT32 Flags;
        UINT32 Bytes;
        BOOLEAN NullData;
        UINT32 PlayBegin;
        UINT32 PlayLength;
        UINT32 LoopBegin;
        UINT32 LoopLength;
        UINT32 LoopCount;
    } Cases[] =
    {
        { 0, 400, FALSE, 0, 0, 0, 0, 0 }, { XAUDIO2_END_OF_STREAM, 400, FALSE, 0, 0, 0, 0, 0 }, { 0x80, 400, FALSE, 0, 0, 0, 0, 0 },
        { 0x1, 400, FALSE, 0, 0, 0, 0, 0 }, { 0, 0, FALSE, 0, 0, 0, 0, 0 }, { 0, 400, TRUE, 0, 0, 0, 0, 0 },
        { 0, 402, FALSE, 0, 0, 0, 0, 0 }, { 0, 401, FALSE, 0, 0, 0, 0, 0 }, { 0, 400, FALSE, 99, 0, 0, 0, 0 },
        { 0, 400, FALSE, 100, 0, 0, 0, 0 }, { 0, 400, FALSE, 50, 50, 0, 0, 0 }, { 0, 400, FALSE, 50, 51, 0, 0, 0 },
        { 0, 400, FALSE, 0, 100, 0, 0, 0 }, { 0, 400, FALSE, 0, 101, 0, 0, 0 }, { 0, 400, FALSE, 0, 0, 10, 0, 0 },
        { 0, 400, FALSE, 0, 0, 0, 10, 0 }, { 0, 400, FALSE, 0, 0, 0, 0, 1 }, { 0, 400, FALSE, 0, 0, 0, 0, 254 },
        { 0, 400, FALSE, 0, 0, 0, 0, 255 }, { 0, 400, FALSE, 0, 0, 0, 0, 256 }, { 0, 400, FALSE, 0, 0, 10, 20, 2 },
        { 0, 400, FALSE, 0, 0, 90, 10, 2 }, { 0, 400, FALSE, 0, 0, 90, 11, 2 }, { 0, 400, FALSE, 0, 0, 100, 1, 2 },
        { 0, 400, FALSE, 20, 30, 10, 5, 2 }, { 0, 400, FALSE, 20, 30, 10, 11, 2 }, { 0, 400, FALSE, 20, 30, 50, 1, 2 },
        { 0, 400, FALSE, 20, 30, 49, 1, 2 }, { 0, 400, FALSE, 20, 30, 30, 21, 2 }, { 0, 400, FALSE, 20, 30, 30, 20, 2 },
        { 0, 400, FALSE, 20, 0, 10, 15, 2 }, { 0, 400, FALSE, 20, 0, 0, 20, 2 }, { 0, 400, FALSE, 20, 0, 0, 21, 2 },
        { 0, 400, FALSE, 0, 0, 10, 0, 255 }, { 0, 0x80000000, FALSE, 0, 0, 0, 0, 0 }, { 0, 0x80000004, FALSE, 0, 0, 0, 0, 0 },
    };

    printf("submit null = %08lX\n", Source->SubmitSourceBuffer(nullptr));
    for (UINT Index = 0; Index < ARRAYSIZE(Cases); Index++)
    {
        memset(&Buffer, 0, sizeof(Buffer));
        Buffer.Flags = Cases[Index].Flags;
        Buffer.AudioBytes = Cases[Index].Bytes;
        Buffer.pAudioData = Cases[Index].NullData ? nullptr : Data;
        Buffer.PlayBegin = Cases[Index].PlayBegin;
        Buffer.PlayLength = Cases[Index].PlayLength;
        Buffer.LoopBegin = Cases[Index].LoopBegin;
        Buffer.LoopLength = Cases[Index].LoopLength;
        Buffer.LoopCount = Cases[Index].LoopCount;
        Buffer.pContext = reinterpret_cast<void*>(static_cast<ULONG_PTR>(Index + 1));
        hr = Source->SubmitSourceBuffer(&Buffer);
        printf("submit %u = %08lX\n", Index, hr);
    }
    Source->GetState(nullptr);
    memset(&State, 0xCC, sizeof(State));
    Source->GetState(&State);
    printf("state queued %u context %u played %llu\n", State.BuffersQueued,
           static_cast<UINT32>(reinterpret_cast<ULONG_PTR>(State.pCurrentBufferContext)), State.SamplesPlayed);
    memset(&State, 0xCC, sizeof(State));
    Source->GetState(&State, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    printf("state noplayed queued %u played %016llX\n", State.BuffersQueued, State.SamplesPlayed);
    memset(&State, 0xCC, sizeof(State));
    Source->GetState(&State, 1);
    printf("state badflag queued %08X\n", State.BuffersQueued);
    {
        XAUDIO2_BUFFER_WMA Wma = {};

        memset(&Buffer, 0, sizeof(Buffer));
        Buffer.AudioBytes = 400;
        Buffer.pAudioData = Data;
        printf("submit wma params = %08lX\n", Source->SubmitSourceBuffer(&Buffer, &Wma));
    }
    printf("flush = %08lX\n", Source->FlushSourceBuffers());
    Source->GetState(&State);
    printf("state after flush queued %u\n", State.BuffersQueued);
    printf("discontinuity = %08lX\n", Source->Discontinuity());
    printf("exitloop = %08lX\n", Source->ExitLoop());
    printf("exitloop opset = %08lX\n", Source->ExitLoop(XAUDIO2_INVALID_OPSET));
    printf("exitloop deferred = %08lX\n", Source->ExitLoop(4));
    Source->DestroyVoice();

    /* The queue limit */
    Source = nullptr;
    if (SUCCEEDED(Engine->CreateSourceVoice(&Source, &Format)))
    {
        UINT Accepted = 0;

        memset(&Buffer, 0, sizeof(Buffer));
        Buffer.AudioBytes = 400;
        Buffer.pAudioData = Data;
        for (UINT Index = 0; Index < 70; Index++)
        {
            hr = Source->SubmitSourceBuffer(&Buffer);
            if (SUCCEEDED(hr))
                Accepted += 1;
            else if (Index == 64 || Index == 69)
                printf("submit %u = %08lX\n", Index, hr);
        }
        Source->GetState(&State);
        printf("queue accepted %u queued %u\n", Accepted, State.BuffersQueued);
        Source->DestroyVoice();
    }

    /* ADPCM buffer rules */
    {
        BYTE Storage[sizeof(ADPCMWAVEFORMAT) + 7 * sizeof(ADPCMCOEFSET)];
        ADPCMWAVEFORMAT* Adpcm = reinterpret_cast<ADPCMWAVEFORMAT*>(Storage);
        static const SHORT Coef1[7] = { 256, 512, 0, 192, 240, 460, 392 };
        static const SHORT Coef2[7] = { 0, -256, 0, 64, 0, -208, -232 };

        memset(Storage, 0, sizeof(Storage));
        Adpcm->wfx.wFormatTag = WAVE_FORMAT_ADPCM;
        Adpcm->wfx.nChannels = 1;
        Adpcm->wfx.nSamplesPerSec = 22050;
        Adpcm->wfx.wBitsPerSample = 4;
        Adpcm->wSamplesPerBlock = 64;
        Adpcm->wfx.nBlockAlign = 7 + 31;
        Adpcm->wfx.nAvgBytesPerSec = 22050 / 64 * 38;
        Adpcm->wfx.cbSize = 32;
        Adpcm->wNumCoef = 7;
        for (UINT Coef = 0; Coef < 7; Coef++)
        {
            Adpcm->aCoef[Coef].iCoef1 = Coef1[Coef];
            Adpcm->aCoef[Coef].iCoef2 = Coef2[Coef];
        }
        Source = nullptr;
        if (SUCCEEDED(Engine->CreateSourceVoice(&Source, &Adpcm->wfx)))
        {
            static const UINT32 Shapes[][5] =
            {
                { 38 * 4, 0, 0, 0, 0 }, { 38 * 4 + 1, 0, 0, 0, 0 }, { 38 * 4, 64, 0, 0, 0 }, { 38 * 4, 32, 0, 0, 0 },
                { 38 * 4, 0, 64, 0, 0 }, { 38 * 4, 0, 63, 0, 0 }, { 38 * 4, 64, 192, 0, 0 }, { 38 * 4, 64, 193, 0, 0 },
                { 38 * 4, 0, 0, 64, 64 }, { 38 * 4, 0, 0, 65, 64 }, { 38 * 4, 0, 0, 64, 65 }, { 38 * 4, 0, 256, 0, 0 },
            };

            for (UINT Index = 0; Index < ARRAYSIZE(Shapes); Index++)
            {
                memset(&Buffer, 0, sizeof(Buffer));
                Buffer.AudioBytes = Shapes[Index][0];
                Buffer.pAudioData = Data;
                Buffer.PlayBegin = Shapes[Index][1];
                Buffer.PlayLength = Shapes[Index][2];
                Buffer.LoopBegin = Shapes[Index][3];
                Buffer.LoopLength = Shapes[Index][4];
                Buffer.LoopCount = Shapes[Index][4] != 0 ? 1 : 0;
                printf("adpcm submit %u = %08lX\n", Index, Source->SubmitSourceBuffer(&Buffer));
            }
            Source->GetState(&State);
            printf("adpcm queued %u played %llu\n", State.BuffersQueued, State.SamplesPlayed);
            Source->DestroyVoice();
        }
    }
    printf("release %lu\n", Engine->Release());
}

static
VOID
XcCheckState(VOID)
{
    IXAudio2MasteringVoice* Master;
    IXAudio2* Engine = XcEngineWithMaster(&Master, 2, 48000);
    IXAudio2SourceVoice* Source = nullptr;
    WAVEFORMATEX Format;
    XAUDIO2_PERFORMANCE_DATA Perf;

    if (Engine == nullptr)
        return;
    XcPcm(&Format, WAVE_FORMAT_PCM, 1, 44100, 16);
    if (FAILED(Engine->CreateSourceVoice(&Source, &Format)))
        return;
    printf("start flags = %08lX\n", Source->Start(1));
    printf("start opset = %08lX\n", Source->Start(0, XAUDIO2_INVALID_OPSET));
    printf("stop flags = %08lX\n", Source->Stop(1));
    printf("stop tails flag = %08lX\n", Source->Stop(XAUDIO2_PLAY_TAILS | 1));
    printf("stop opset = %08lX\n", Source->Stop(0, XAUDIO2_INVALID_OPSET));
    printf("stop stopped = %08lX\n", Source->Stop());
    printf("start = %08lX\n", Source->Start());
    Engine->GetPerformanceData(&Perf);
    printf("perf active %u total %u submix %u resamplers %u matrices %u\n", Perf.ActiveSourceVoiceCount,
           Perf.TotalSourceVoiceCount, Perf.ActiveSubmixVoiceCount, Perf.ActiveResamplerCount, Perf.ActiveMatrixMixCount);
    printf("start again = %08lX\n", Source->Start());
    printf("stop tails = %08lX\n", Source->Stop(XAUDIO2_PLAY_TAILS));
    Engine->GetPerformanceData(&Perf);
    printf("perf active %u resamplers %u matrices %u\n", Perf.ActiveSourceVoiceCount, Perf.ActiveResamplerCount,
           Perf.ActiveMatrixMixCount);
    printf("stop tails again = %08lX\n", Source->Stop(XAUDIO2_PLAY_TAILS));
    printf("stop = %08lX\n", Source->Stop());
    Engine->GetPerformanceData(&Perf);
    printf("perf active %u resamplers %u matrices %u\n", Perf.ActiveSourceVoiceCount, Perf.ActiveResamplerCount,
           Perf.ActiveMatrixMixCount);
    printf("start deferred = %08lX\n", Source->Start(0, 5));
    printf("start deferred again = %08lX\n", Source->Start(0, 5));
    printf("stop deferred same set = %08lX\n", Source->Stop(0, 5));
    printf("stop deferred = %08lX\n", Source->Stop(0, 6));
    Engine->GetPerformanceData(&Perf);
    printf("perf before commit active %u\n", Perf.ActiveSourceVoiceCount);
    printf("commit 5 = %08lX\n", Engine->CommitChanges(5));
    Engine->GetPerformanceData(&Perf);
    printf("perf after commit 5 active %u\n", Perf.ActiveSourceVoiceCount);
    printf("commit 5 again = %08lX\n", Engine->CommitChanges(5));
    printf("commit all = %08lX\n", Engine->CommitChanges(XAUDIO2_COMMIT_ALL));
    Engine->GetPerformanceData(&Perf);
    printf("perf after commit all active %u\n", Perf.ActiveSourceVoiceCount);
    printf("start deferred 8 = %08lX\n", Source->Start(0, 8));
    printf("stop now = %08lX\n", Source->Stop());
    printf("commit 8 = %08lX\n", Engine->CommitChanges(8));
    Engine->GetPerformanceData(&Perf);
    printf("perf after commit 8 active %u\n", Perf.ActiveSourceVoiceCount);
    printf("stop = %08lX\n", Source->Stop());
    Engine->GetPerformanceData(&Perf);
    printf("perf end active %u total %u\n", Perf.ActiveSourceVoiceCount, Perf.TotalSourceVoiceCount);

    /* Submix voices count as active once they exist */
    {
        IXAudio2SubmixVoice* Submix = nullptr;
        IXAudio2SubmixVoice* Other = nullptr;

        Engine->CreateSubmixVoice(&Submix, 2, 44100);
        Engine->CreateSubmixVoice(&Other, 6, 48000, XAUDIO2_VOICE_USEFILTER);
        Engine->GetPerformanceData(&Perf);
        printf("perf submix %u resamplers %u matrices %u\n", Perf.ActiveSubmixVoiceCount, Perf.ActiveResamplerCount,
               Perf.ActiveMatrixMixCount);
        if (Submix != nullptr)
            Submix->DestroyVoice();
        if (Other != nullptr)
            Other->DestroyVoice();
        Engine->GetPerformanceData(&Perf);
        printf("perf submix gone %u resamplers %u matrices %u\n", Perf.ActiveSubmixVoiceCount, Perf.ActiveResamplerCount,
               Perf.ActiveMatrixMixCount);
    }
    Source->DestroyVoice();
    Engine->GetPerformanceData(&Perf);
    printf("perf destroyed total %u\n", Perf.TotalSourceVoiceCount);
    printf("release %lu\n", Engine->Release());
}

/* Plays a short quiet buffer and prints the callbacks it caused */
static
VOID
XcCheckCallbacks(VOID)
{
    static const struct
    {
        WORD Channels;
        DWORD Rate;
        UINT32 Frames;
        UINT32 PlayBegin;
        UINT32 PlayLength;
        UINT32 LoopBegin;
        UINT32 LoopLength;
        UINT32 LoopCount;
        UINT32 Buffers;
        float Ratio;
    } Cases[] =
    {
        { 1, 48000, 2400, 0, 0, 0, 0, 0, 1, 1.0f }, { 2, 48000, 4800, 0, 0, 0, 0, 0, 2, 1.0f },
        { 1, 44100, 2205, 0, 0, 0, 0, 0, 1, 1.0f }, { 1, 22050, 1000, 0, 0, 0, 0, 0, 3, 1.0f },
        { 1, 48000, 2400, 100, 1000, 0, 0, 0, 1, 1.0f }, { 1, 48000, 2400, 0, 0, 200, 600, 2, 1, 1.0f },
        { 1, 48000, 1000, 0, 0, 0, 0, 0, 1, 2.0f }, { 1, 48000, 1000, 0, 0, 0, 0, 0, 1, 0.5f },
        { 2, 96000, 9600, 0, 0, 0, 0, 0, 1, 1.0f }, { 1, 8000, 800, 0, 0, 0, 0, 0, 1, 1.0f },
        { 1, 48000, 960, 0, 0, 0, 0, 0, 1, 1.0f }, { 1, 48000, 479, 0, 0, 0, 0, 0, 1, 1.0f },
    };
    static SHORT Data[9600 * 2];
    IXAudio2MasteringVoice* Master;
    IXAudio2* Engine = XcEngineWithMaster(&Master, 2, 48000);
    XcEngineCallback EngineCallback;
    XcVoiceCallback VoiceCallback;

    if (Engine == nullptr)
        return;
    for (UINT Index = 0; Index < ARRAYSIZE(Data); Index++)
        Data[Index] = static_cast<SHORT>((Index * 37) % 31 - 15);
    XcStreamEnded = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Engine->RegisterForCallbacks(&EngineCallback);
    printf("start engine = %08lX\n", Engine->StartEngine());
    for (UINT Case = 0; Case < ARRAYSIZE(Cases); Case++)
    {
        IXAudio2SourceVoice* Source = nullptr;
        WAVEFORMATEX Format;
        XAUDIO2_BUFFER Buffer = {};
        XAUDIO2_VOICE_STATE State;
        HRESULT hr;

        XcPcm(&Format, WAVE_FORMAT_PCM, Cases[Case].Channels, Cases[Case].Rate, 16);
        hr = Engine->CreateSourceVoice(&Source, &Format, 0, 2.0f, &VoiceCallback);
        if (FAILED(hr))
        {
            printf("callbacks[%u] create = %08lX\n", Case, hr);
            continue;
        }
        Source->SetVolume(0.01f);
        if (Cases[Case].Ratio != 1.0f)
            Source->SetFrequencyRatio(Cases[Case].Ratio);
        for (UINT32 Index = 0; Index < Cases[Case].Buffers; Index++)
        {
            Buffer.AudioBytes = Cases[Case].Frames * Format.nBlockAlign;
            Buffer.pAudioData = reinterpret_cast<const BYTE*>(Data);
            Buffer.PlayBegin = Cases[Case].PlayBegin;
            Buffer.PlayLength = Cases[Case].PlayLength;
            Buffer.LoopBegin = Cases[Case].LoopBegin;
            Buffer.LoopLength = Cases[Case].LoopLength;
            Buffer.LoopCount = Cases[Case].LoopCount;
            Buffer.Flags = Index + 1 == Cases[Case].Buffers ? XAUDIO2_END_OF_STREAM : 0;
            Buffer.pContext = reinterpret_cast<void*>(static_cast<ULONG_PTR>(Index + 1));
            Source->SubmitSourceBuffer(&Buffer);
        }
        XcResetEvents();
        ResetEvent(XcStreamEnded);
        Source->Start();
        if (WaitForSingleObject(XcStreamEnded, 3000) != WAIT_OBJECT_0)
            printf("callbacks[%u] no stream end\n", Case);
        Sleep(40);
        Source->GetState(&State);
        printf("callbacks[%u] state queued %u played %llu\n", Case, State.BuffersQueued, State.SamplesPlayed);
        Source->Stop();
        Source->DestroyVoice();
        {
            CHAR Name[32];

            sprintf_s(Name, "callbacks[%u]", Case);
            XcPrintEvents(Name);
        }
    }
    Engine->UnregisterForCallbacks(&EngineCallback);
    printf("release %lu\n", Engine->Release());
    CloseHandle(XcStreamEnded);
    XcStreamEnded = nullptr;
}

/* Waits until a callback event of the given kind shows up, or the time runs out */
static
BOOLEAN
XcWaitForEvent(
    _In_ CHAR Kind,
    _In_ UINT32 Count,
    _In_ DWORD Milliseconds)
{
    ULONGLONG End = GetTickCount64() + Milliseconds;

    while (GetTickCount64() < End)
    {
        UINT32 Seen = 0;
        LONG Total = XcEventCount;

        if (Total > static_cast<LONG>(ARRAYSIZE(XcEvents)))
            Total = ARRAYSIZE(XcEvents);
        for (LONG Index = 0; Index < Total; Index++)
        {
            if (XcEvents[Index].Kind == Kind)
                Seen += 1;
        }
        if (Seen >= Count)
            return TRUE;
        Sleep(2);
    }
    return FALSE;
}

/* Prints the buffer events alone, which do not depend on where passes fall */
static
VOID
XcPrintBufferEvents(
    _In_z_ PCSTR Name)
{
    LONG Count = XcEventCount;

    if (Count > static_cast<LONG>(ARRAYSIZE(XcEvents)))
        Count = ARRAYSIZE(XcEvents);
    printf("%s buffer events", Name);
    for (LONG Index = 0; Index < Count; Index++)
    {
        CHAR Kind = XcEvents[Index].Kind;

        if (Kind == 'B' || Kind == 'b' || Kind == 'L' || Kind == 'E' || Kind == 'R')
            printf(" %c%u", Kind, XcEvents[Index].Value);
    }
    printf("\n");
}

static
VOID
XcState(
    _In_z_ PCSTR Name,
    _In_ IXAudio2SourceVoice* Source)
{
    XAUDIO2_VOICE_STATE State;

    memset(&State, 0, sizeof(State));
    Source->GetState(&State);
    printf("%s queued %u context %u played %llu\n", Name, State.BuffersQueued,
           static_cast<UINT32>(reinterpret_cast<ULONG_PTR>(State.pCurrentBufferContext)), State.SamplesPlayed);
}

/* Buffer life on a playing voice: samples played, flushes, loop exits and restarts */
static
VOID
XcCheckQueue(VOID)
{
    static SHORT Data[48000];
    IXAudio2MasteringVoice* Master;
    IXAudio2* Engine = XcEngineWithMaster(&Master, 2, 48000);
    XcVoiceCallback Callback;
    IXAudio2SourceVoice* Source = nullptr;
    WAVEFORMATEX Format;
    XAUDIO2_BUFFER Buffer = {};

    if (Engine == nullptr)
        return;
    XcPcm(&Format, WAVE_FORMAT_PCM, 1, 48000, 16);
    if (FAILED(Engine->CreateSourceVoice(&Source, &Format, 0, 2.0f, &Callback)))
        return;
    Source->SetVolume(0.0f);
    Buffer.pAudioData = reinterpret_cast<const BYTE*>(Data);

    /* Starving after a buffer without END_OF_STREAM keeps the count */
    XcResetEvents();
    Buffer.AudioBytes = 960 * 2;
    Buffer.pContext = reinterpret_cast<void*>(1);
    Source->SubmitSourceBuffer(&Buffer);
    Source->Start();
    XcWaitForEvent('b', 1, 2000);
    Sleep(50);
    XcState("starved", Source);
    Source->Stop();
    Sleep(30);
    XcState("stopped", Source);
    Buffer.AudioBytes = 480 * 2;
    Buffer.pContext = reinterpret_cast<void*>(2);
    Source->SubmitSourceBuffer(&Buffer);
    XcState("submitted while stopped", Source);
    XcResetEvents();
    Source->Start();
    XcWaitForEvent('b', 1, 2000);
    Sleep(30);
    XcState("restarted", Source);
    Buffer.Flags = XAUDIO2_END_OF_STREAM;
    Buffer.pContext = reinterpret_cast<void*>(3);
    Source->SubmitSourceBuffer(&Buffer);
    XcWaitForEvent('E', 1, 2000);
    Sleep(30);
    XcState("after end of stream", Source);
    Buffer.Flags = 0;
    XcPrintBufferEvents("queue");

    /* Flushing a stopped voice */
    Source->Stop();
    XcResetEvents();
    for (UINT Index = 0; Index < 3; Index++)
    {
        Buffer.AudioBytes = 4800 * 2;
        Buffer.pContext = reinterpret_cast<void*>(static_cast<ULONG_PTR>(10 + Index));
        Source->SubmitSourceBuffer(&Buffer);
    }
    printf("flush stopped = %08lX\n", Source->FlushSourceBuffers());
    XcState("flushed", Source);
    Sleep(60);
    XcState("flushed later", Source);
    XcPrintBufferEvents("flush stopped");

    /* Flushing a playing voice keeps the buffer it is in */
    XcResetEvents();
    for (UINT Index = 0; Index < 3; Index++)
    {
        Buffer.AudioBytes = 9600 * 2;
        Buffer.pContext = reinterpret_cast<void*>(static_cast<ULONG_PTR>(20 + Index));
        Source->SubmitSourceBuffer(&Buffer);
    }
    Source->Start();
    XcWaitForEvent('B', 1, 2000);
    printf("flush playing = %08lX\n", Source->FlushSourceBuffers());
    XcWaitForEvent('b', 3, 2000);
    Sleep(30);
    XcState("flushed playing", Source);
    XcPrintBufferEvents("flush playing");

    /* ExitLoop and Discontinuity on a looping buffer */
    XcResetEvents();
    Buffer.AudioBytes = 960 * 2;
    Buffer.LoopBegin = 0;
    Buffer.LoopLength = 480;
    Buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    Buffer.pContext = reinterpret_cast<void*>(30);
    Source->SubmitSourceBuffer(&Buffer);
    XcWaitForEvent('L', 3, 2000);
    printf("exitloop playing = %08lX\n", Source->ExitLoop());
    XcWaitForEvent('b', 1, 2000);
    Sleep(30);
    XcState("exited loop", Source);
    Buffer.LoopLength = 0;
    Buffer.LoopCount = 0;
    Buffer.pContext = reinterpret_cast<void*>(31);
    Source->SubmitSourceBuffer(&Buffer);
    printf("discontinuity = %08lX\n", Source->Discontinuity());
    XcWaitForEvent('E', 1, 2000);
    Sleep(30);
    XcState("discontinuity ended", Source);
    XcPrintBufferEvents("loop");

    /* The ratio and filter reach their getters once the voice has run */
    {
        float Ratio = 0.0f;

        Source->SetFrequencyRatio(1.5f);
        Source->GetFrequencyRatio(&Ratio);
        printf("ratio before pass %08lX\n", XcBits(Ratio));
        Buffer.AudioBytes = 4800 * 2;
        Buffer.pContext = reinterpret_cast<void*>(40);
        Source->SubmitSourceBuffer(&Buffer);
        Sleep(60);
        Source->GetFrequencyRatio(&Ratio);
        printf("ratio after pass %08lX\n", XcBits(Ratio));
        Source->SetFrequencyRatio(0.75f, 9);
        Sleep(40);
        Source->GetFrequencyRatio(&Ratio);
        printf("ratio deferred %08lX\n", XcBits(Ratio));
        Engine->CommitChanges(9);
        Sleep(40);
        Source->GetFrequencyRatio(&Ratio);
        printf("ratio committed %08lX\n", XcBits(Ratio));
    }
    Source->Stop();
    Source->DestroyVoice();

    /* Filters on a running submix voice */
    {
        IXAudio2SubmixVoice* Submix = nullptr;
        XAUDIO2_FILTER_PARAMETERS Filter = { BandPassFilter, 0.25f, 0.5f };
        XAUDIO2_FILTER_PARAMETERS Read;

        if (SUCCEEDED(Engine->CreateSubmixVoice(&Submix, 2, 48000, XAUDIO2_VOICE_USEFILTER)))
        {
            printf("submix setfilter = %08lX\n", Submix->SetFilterParameters(&Filter));
            Sleep(40);
            memset(&Read, 0xCC, sizeof(Read));
            Submix->GetFilterParameters(&Read);
            XcBytes("submix filter after pass", &Read, sizeof(Read));
            Submix->DestroyVoice();
        }
    }
    printf("release %lu\n", Engine->Release());
}

/* An XAPO that logs how the host drives it and can record what passes through */
class XcProbeApo final : public IXAPO, public IXAPOParameters
{
public:
    XcProbeApo(
        _In_ UINT32 NewFlags,
        _In_ BOOLEAN NewLogging)
    {
        References = 1;
        Flags = NewFlags;
        Logging = NewLogging;
        InitializeCriticalSection(&Lock);
        memset(Parameters, 0, sizeof(Parameters));
    }

    ~XcProbeApo()
    {
        DeleteCriticalSection(&Lock);
        delete[] Recorded;
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        REFIID Iid,
        _COM_Outptr_ void** Object) override
    {
        if (IsEqualIID(Iid, __uuidof(IXAPO)) || IsEqualIID(Iid, IID_IUnknown))
            *Object = static_cast<IXAPO*>(this);
        else if (IsEqualIID(Iid, __uuidof(IXAPOParameters)))
            *Object = static_cast<IXAPOParameters*>(this);
        else
        {
            *Object = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef() override
    {
        return InterlockedIncrement(&References);
    }

    ULONG
    STDMETHODCALLTYPE
    Release() override
    {
        return InterlockedDecrement(&References);
    }

    HRESULT
    STDMETHODCALLTYPE
    GetRegistrationProperties(
        _Outptr_ XAPO_REGISTRATION_PROPERTIES** Properties) override
    {
        *Properties = static_cast<XAPO_REGISTRATION_PROPERTIES*>(CoTaskMemAlloc(sizeof(**Properties)));
        if (*Properties == nullptr)
            return E_OUTOFMEMORY;
        memset(*Properties, 0, sizeof(**Properties));
        (*Properties)->Flags = Flags;
        (*Properties)->MinInputBufferCount = 1;
        (*Properties)->MaxInputBufferCount = 1;
        (*Properties)->MinOutputBufferCount = 1;
        (*Properties)->MaxOutputBufferCount = 1;
        Note("registration");
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    IsInputFormatSupported(
        const WAVEFORMATEX* Output,
        const WAVEFORMATEX* Requested,
        _Outptr_opt_ WAVEFORMATEX** Supported) override
    {
        NoteFormat("isinput out", Output);
        NoteFormat("isinput requested", Requested);
        if (Supported != nullptr)
            *Supported = nullptr;
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    IsOutputFormatSupported(
        const WAVEFORMATEX* Input,
        const WAVEFORMATEX* Requested,
        _Outptr_opt_ WAVEFORMATEX** Supported) override
    {
        NoteFormat("isoutput in", Input);
        NoteFormat("isoutput requested", Requested);
        if (Supported != nullptr)
            *Supported = nullptr;
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    Initialize(
        _In_reads_bytes_opt_(Size) const void* Data,
        UINT32 Size) override
    {
        Note("initialize");
        return S_OK;
    }

    void
    STDMETHODCALLTYPE
    Reset() override
    {
        Note("reset");
    }

    HRESULT
    STDMETHODCALLTYPE
    LockForProcess(
        UINT32 InputCount,
        _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
        UINT32 OutputCount,
        _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output) override
    {
        CHAR Line[96];

        sprintf_s(Line, "lock %u %u max %u %u", InputCount, OutputCount, InputCount != 0 ? Input->MaxFrameCount : 0,
                  OutputCount != 0 ? Output->MaxFrameCount : 0);
        Note(Line);
        if (InputCount != 0)
        {
            NoteFormat("lock in", Input->pFormat);
            InputChannels = Input->pFormat->nChannels;
        }
        if (OutputCount != 0)
        {
            NoteFormat("lock out", Output->pFormat);
            OutputChannels = Output->pFormat->nChannels;
        }
        return S_OK;
    }

    void
    STDMETHODCALLTYPE
    UnlockForProcess() override
    {
        Note("unlock");
    }

    void
    STDMETHODCALLTYPE
    Process(
        UINT32 InputCount,
        _In_reads_opt_(InputCount) const XAPO_PROCESS_BUFFER_PARAMETERS* Input,
        UINT32 OutputCount,
        _Inout_updates_opt_(OutputCount) XAPO_PROCESS_BUFFER_PARAMETERS* Output,
        BOOL Enabled) override
    {
        Calls += 1;
        if (Calls <= 4)
        {
            CHAR Line[128];

            sprintf_s(Line, "process %u %u in flags %d frames %u out flags %d frames %u inplace %d enabled %d", InputCount,
                      OutputCount, Input->BufferFlags, Input->ValidFrameCount, Output->BufferFlags,
                      Output->ValidFrameCount, Input->pBuffer == Output->pBuffer, Enabled);
            Note(Line);
        }
        if (Recorded != nullptr && Recording && Input->BufferFlags == XAPO_BUFFER_VALID)
        {
            UINT32 Samples = Input->ValidFrameCount * InputChannels;

            for (UINT32 Index = 0; Index < Samples && RecordedCount < RecordedCapacity; Index++)
                Recorded[RecordedCount++] = static_cast<const float*>(Input->pBuffer)[Index];
        }
        else if (Recorded != nullptr && Recording)
        {
            UINT32 Samples = Input->ValidFrameCount * InputChannels;

            for (UINT32 Index = 0; Index < Samples && RecordedCount < RecordedCapacity; Index++)
                Recorded[RecordedCount++] = 0.0f;
        }
        if (Input->pBuffer != Output->pBuffer && InputChannels == OutputChannels)
            memcpy(Output->pBuffer, Input->pBuffer, sizeof(float) * Input->ValidFrameCount * InputChannels);
        Output->BufferFlags = Input->BufferFlags;
        Output->ValidFrameCount = Input->ValidFrameCount;
    }

    UINT32
    STDMETHODCALLTYPE
    CalcInputFrames(
        UINT32 OutputFrames) override
    {
        Note("calcinput");
        return OutputFrames;
    }

    UINT32
    STDMETHODCALLTYPE
    CalcOutputFrames(
        UINT32 InputFrames) override
    {
        Note("calcoutput");
        return InputFrames;
    }

    void
    STDMETHODCALLTYPE
    SetParameters(
        _In_reads_bytes_(Size) const void* NewParameters,
        UINT32 Size) override
    {
        CHAR Line[64];

        sprintf_s(Line, "setparameters %u", Size);
        Note(Line);
        memcpy(Parameters, NewParameters, Size < sizeof(Parameters) ? Size : sizeof(Parameters));
    }

    void
    STDMETHODCALLTYPE
    GetParameters(
        _Out_writes_bytes_(Size) void* Result,
        UINT32 Size) override
    {
        CHAR Line[64];

        sprintf_s(Line, "getparameters %u", Size);
        Note(Line);
        memcpy(Result, Parameters, Size < sizeof(Parameters) ? Size : sizeof(Parameters));
    }

    VOID
    Note(
        _In_z_ PCSTR Text)
    {
        if (!Logging)
            return;
        EnterCriticalSection(&Lock);
        if (LogLength + strlen(Text) + 2 < sizeof(Log))
        {
            strcpy_s(Log + LogLength, sizeof(Log) - LogLength, Text);
            LogLength += strlen(Text);
            Log[LogLength++] = '\n';
            Log[LogLength] = '\0';
        }
        LeaveCriticalSection(&Lock);
    }

    VOID
    NoteFormat(
        _In_z_ PCSTR Name,
        _In_opt_ const WAVEFORMATEX* Format)
    {
        CHAR Line[192];

        if (Format == nullptr)
        {
            sprintf_s(Line, "%s null", Name);
        }
        else if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            const WAVEFORMATEXTENSIBLE* Extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format);

            sprintf_s(Line, "%s ext %u %lu %lu %u %u %u valid %u mask %08lX sub %08lX", Name, Format->nChannels,
                      Format->nSamplesPerSec, Format->nAvgBytesPerSec, Format->nBlockAlign, Format->wBitsPerSample,
                      Format->cbSize, Extensible->Samples.wValidBitsPerSample, Extensible->dwChannelMask,
                      Extensible->SubFormat.Data1);
        }
        else
        {
            sprintf_s(Line, "%s tag %u %u %lu %lu %u %u %u", Name, Format->wFormatTag, Format->nChannels,
                      Format->nSamplesPerSec, Format->nAvgBytesPerSec, Format->nBlockAlign, Format->wBitsPerSample,
                      Format->cbSize);
        }
        Note(Line);
    }

    VOID
    PrintLog(
        _In_z_ PCSTR Name)
    {
        PCSTR Line = Log;

        EnterCriticalSection(&Lock);
        while (*Line != '\0')
        {
            PCSTR End = strchr(Line, '\n');
            SIZE_T Length = End != nullptr ? static_cast<SIZE_T>(End - Line) : strlen(Line);

            printf("%s %.*s\n", Name, static_cast<int>(Length), Line);
            Line += Length + (End != nullptr ? 1 : 0);
        }
        LogLength = 0;
        Log[0] = '\0';
        LeaveCriticalSection(&Lock);
    }

    LONG References;
    UINT32 Flags;
    BOOLEAN Logging;
    CRITICAL_SECTION Lock;
    CHAR Log[8192] = {};
    SIZE_T LogLength = 0;
    BYTE Parameters[64];
    volatile LONG Calls = 0;
    UINT32 InputChannels = 0;
    UINT32 OutputChannels = 0;
    float* Recorded = nullptr;
    SIZE_T RecordedCount = 0;
    SIZE_T RecordedCapacity = 0;
    volatile BOOLEAN Recording = FALSE;
};

/* How the host drives an application XAPO, and what it accepts */
static
VOID
XcCheckEffects(VOID)
{
    static const UINT32 FlagSets[] =
    {
        XAPO_FLAG_INPLACE_SUPPORTED | XAPO_FLAG_CHANNELS_MUST_MATCH | XAPO_FLAG_FRAMERATE_MUST_MATCH |
            XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH | XAPO_FLAG_BUFFERCOUNT_MUST_MATCH,
        XAPO_FLAG_INPLACE_REQUIRED,
        XAPO_FLAG_INPLACE_REQUIRED | XAPO_FLAG_INPLACE_SUPPORTED,
        0,
        XAPO_FLAG_FRAMERATE_MUST_MATCH | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH,
    };
    static const UINT32 Outputs[] = { 1, 2, 6 };
    IXAudio2MasteringVoice* Master;
    IXAudio2* Engine = XcEngineWithMaster(&Master, 2, 48000);
    static SHORT Data[4800];
    WAVEFORMATEX Format;

    if (Engine == nullptr)
        return;
    XcPcm(&Format, WAVE_FORMAT_PCM, 1, 44100, 16);
    for (UINT FlagIndex = 0; FlagIndex < ARRAYSIZE(FlagSets); FlagIndex++)
    {
        for (UINT OutputIndex = 0; OutputIndex < ARRAYSIZE(Outputs); OutputIndex++)
        {
            /* The host may keep references past the voice, so the probes stay alive */
            XcProbeApo& Probe = *new XcProbeApo(FlagSets[FlagIndex], TRUE);
            XAUDIO2_EFFECT_DESCRIPTOR Effect = { static_cast<IXAPO*>(&Probe), TRUE, Outputs[OutputIndex] };
            XAUDIO2_EFFECT_CHAIN Chain = { 1, &Effect };
            IXAudio2SourceVoice* Source = nullptr;
            HRESULT hr;
            CHAR Name[48];

            sprintf_s(Name, "  fx %u/%u", FlagIndex, Outputs[OutputIndex]);
            hr = Engine->CreateSourceVoice(&Source, &Format, 0, 2.0f, nullptr, nullptr, &Chain);
            printf("effect flags %08X outputs %u = %08lX refs %ld\n", FlagSets[FlagIndex], Outputs[OutputIndex], hr,
                   Probe.References);
            Probe.PrintLog(Name);
            if (FAILED(hr))
                continue;
            {
                XAUDIO2_VOICE_DETAILS Details;
                float Volumes[8];
                float Matrix[16];

                Source->GetVoiceDetails(&Details);
                printf("  details channels %u\n", Details.InputChannels);
                memset(Volumes, 0xCC, sizeof(Volumes));
                Source->GetChannelVolumes(Outputs[OutputIndex], Volumes);
                XcFloats("  channelvolumes", Volumes, Outputs[OutputIndex]);
                memset(Matrix, 0xCC, sizeof(Matrix));
                Source->GetOutputMatrix(Master, Outputs[OutputIndex], 2, Matrix);
                XcFloats("  matrix", Matrix, Outputs[OutputIndex] * 2);
            }
            if (FlagIndex == 0 && OutputIndex == 0)
            {
                XAUDIO2_BUFFER Buffer = {};
                BYTE Parameters[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
                BYTE Read[8];
                BOOL Enabled = 7;

                Buffer.AudioBytes = sizeof(Data);
                Buffer.pAudioData = reinterpret_cast<const BYTE*>(Data);
                Source->SubmitSourceBuffer(&Buffer);
                Source->Start();
                Sleep(60);
                Source->Stop();
                Sleep(20);
                Probe.PrintLog("  fx running");
                printf("  seteffectparams = %08lX\n", Source->SetEffectParameters(0, Parameters, sizeof(Parameters)));
                printf("  seteffectparams deferred = %08lX\n", Source->SetEffectParameters(0, Parameters, 4, 3));
                printf("  seteffectparams zero = %08lX\n", Source->SetEffectParameters(0, Parameters, 0));
                printf("  seteffectparams index = %08lX\n", Source->SetEffectParameters(1, Parameters, 4));
                memset(Read, 0, sizeof(Read));
                printf("  geteffectparams = %08lX\n", Source->GetEffectParameters(0, Read, sizeof(Read)));
                XcBytes("  read", Read, sizeof(Read));
                printf("  geteffectparams zero = %08lX\n", Source->GetEffectParameters(0, Read, 0));
                Probe.PrintLog("  fx params");
                Source->GetEffectState(0, &Enabled);
                printf("  state %d\n", Enabled);
                printf("  disable = %08lX\n", Source->DisableEffect(0));
                Source->GetEffectState(0, &Enabled);
                printf("  state %d\n", Enabled);
                printf("  enable deferred = %08lX\n", Source->EnableEffect(0, 5));
                Source->GetEffectState(0, &Enabled);
                printf("  state %d\n", Enabled);
                Engine->CommitChanges(5);
                Source->GetEffectState(0, &Enabled);
                printf("  state after commit %d\n", Enabled);
                printf("  enable index = %08lX\n", Source->EnableEffect(1));
                printf("  enable opset = %08lX\n", Source->EnableEffect(0, XAUDIO2_INVALID_OPSET));
                Source->GetEffectState(1, &Enabled);
                printf("  seteffectchain null = %08lX\n", Source->SetEffectChain(nullptr));
                Probe.PrintLog("  fx removed");
            }
            Source->DestroyVoice();
            Probe.PrintLog("  fx destroyed");
            printf("  refs %ld\n", Probe.References);
        }
    }

    /* Chains on submix and mastering voices */
    {
        XcProbeApo& First = *new XcProbeApo(XAPO_FLAG_INPLACE_SUPPORTED, TRUE);
        XcProbeApo& Second = *new XcProbeApo(0, TRUE);
        XAUDIO2_EFFECT_DESCRIPTOR Effects[2] = { { static_cast<IXAPO*>(&First), TRUE, 2 }, { static_cast<IXAPO*>(&Second), FALSE, 2 } };
        XAUDIO2_EFFECT_CHAIN Chain = { 2, Effects };
        IXAudio2SubmixVoice* Submix = nullptr;

        printf("submix chain = %08lX\n", Engine->CreateSubmixVoice(&Submix, 2, 32000, 0, 0, nullptr, &Chain));
        Sleep(40);
        First.PrintLog("  first");
        Second.PrintLog("  second");
        if (Submix != nullptr)
            Submix->DestroyVoice();
        First.PrintLog("  first gone");
        Second.PrintLog("  second gone");
        printf("master chain = %08lX\n", Master->SetEffectChain(&Chain));
        Sleep(40);
        First.PrintLog("  master first");
        Second.PrintLog("  master second");
        printf("master chain null = %08lX\n", Master->SetEffectChain(nullptr));
        Sleep(40);
        First.PrintLog("  master first gone");
        Second.PrintLog("  master second gone");
        printf("refs %ld %ld\n", First.References, Second.References);
    }
    printf("release %lu\n", Engine->Release());
}

/* Records what the default endpoint plays, which carries the engine output */
class XcLoopback
{
public:
    XcLoopback() : Client(nullptr), Capture(nullptr), Format(nullptr) {}

    ~XcLoopback()
    {
        if (Client != nullptr)
            Client->Stop();
        if (Capture != nullptr)
            Capture->Release();
        if (Client != nullptr)
            Client->Release();
        CoTaskMemFree(Format);
        delete[] Samples;
    }

    HRESULT
    Start(VOID)
    {
        IMMDeviceEnumerator* Enumerator = nullptr;
        IMMDevice* Endpoint = nullptr;
        HRESULT hr;

        Capacity = 48000 * 8;
        Count = 0;
        Samples = new (std::nothrow) float[Capacity];
        if (Samples == nullptr)
            return E_OUTOFMEMORY;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&Enumerator));
        if (SUCCEEDED(hr))
            hr = Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &Endpoint);
        if (SUCCEEDED(hr))
            hr = Endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&Client));
        if (SUCCEEDED(hr))
            hr = Client->GetMixFormat(&Format);
        if (SUCCEEDED(hr))
            hr = Client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, Format, nullptr);
        if (SUCCEEDED(hr))
            hr = Client->GetService(IID_PPV_ARGS(&Capture));
        if (SUCCEEDED(hr))
            hr = Client->Start();
        if (Endpoint != nullptr)
            Endpoint->Release();
        if (Enumerator != nullptr)
            Enumerator->Release();
        return hr;
    }

    VOID
    Collect(
        _In_ DWORD Milliseconds)
    {
        ULONGLONG End = GetTickCount64() + Milliseconds;

        while (GetTickCount64() < End)
        {
            UINT32 Packet = 0;

            Sleep(5);
            while (SUCCEEDED(Capture->GetNextPacketSize(&Packet)) && Packet != 0)
            {
                BYTE* Data;
                UINT32 Frames;
                DWORD Flags;

                if (FAILED(Capture->GetBuffer(&Data, &Frames, &Flags, nullptr, nullptr)))
                    break;
                for (UINT32 Index = 0; Index < Frames * Format->nChannels && Count < Capacity; Index++)
                    Samples[Count++] = (Flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.0f : reinterpret_cast<float*>(Data)[Index];
                Capture->ReleaseBuffer(Frames);
            }
        }
    }

    IAudioClient* Client;
    IAudioCaptureClient* Capture;
    WAVEFORMATEX* Format;
    float* Samples = nullptr;
    SIZE_T Count = 0;
    SIZE_T Capacity = 0;
};

/* Quiet noise with a marker frame first, so the start lines up between runtimes */
static
VOID
XcFillNoise(
    _Out_writes_bytes_(Bytes) BYTE* Data,
    _In_ DWORD Bytes,
    _In_ const WAVEFORMATEX* Format)
{
    UINT Width = Format->wBitsPerSample / 8;
    DWORD Samples = Bytes / Width;
    ULONG Seed = 12345;

    for (DWORD Index = 0; Index < Samples; Index++)
    {
        float Value;

        Seed = Seed * 1103515245 + 12345;
        Value = (static_cast<float>((Seed >> 16) & 0x7FFF) / 32767.0f - 0.5f) * 0.008f;
        if (Index < Format->nChannels)
            Value = 0.004f;
        if (Format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        {
            reinterpret_cast<float*>(Data)[Index] = Value;
        }
        else if (Width == 1)
        {
            Data[Index] = static_cast<BYTE>(128 + static_cast<int>(Value * 256.0f));
        }
        else if (Width == 2)
        {
            reinterpret_cast<SHORT*>(Data)[Index] = static_cast<SHORT>(Value * 32768.0f);
        }
        else if (Width == 3)
        {
            LONG Sample = static_cast<LONG>(Value * 8388608.0f);

            Data[Index * 3] = static_cast<BYTE>(Sample);
            Data[Index * 3 + 1] = static_cast<BYTE>(Sample >> 8);
            Data[Index * 3 + 2] = static_cast<BYTE>(Sample >> 16);
        }
        else if (Width == 4)
        {
            reinterpret_cast<LONG*>(Data)[Index] = static_cast<LONG>(Value * 2147483648.0f);
        }
    }
}

/* Changes made from inside a pass land on a known pass in both runtimes */
class XcSceneCallback final : public IXAudio2VoiceCallback
{
public:
    IXAudio2SourceVoice* Voice = nullptr;
    IXAudio2Voice* Destination = nullptr;
    UINT Scene = 0;
    UINT Pass = 0;

    void
    STDMETHODCALLTYPE
    OnVoiceProcessingPassStart(
        _In_ UINT32 BytesRequired) override
    {
        Pass += 1;
        if (Pass != 3)
            return;
        switch (Scene)
        {
            case 1:
                Voice->SetVolume(0.25f);
                break;
            case 2:
            {
                float Matrix[2] = { 0.3f, 1.2f };

                Voice->SetOutputMatrix(Destination, 1, 2, Matrix);
                break;
            }
            case 3:
                Voice->SetFrequencyRatio(1.25f);
                break;
            case 4:
            {
                XAUDIO2_FILTER_PARAMETERS Filter = { HighPassFilter, 0.3f, 1.0f };

                Voice->SetFilterParameters(&Filter);
                break;
            }
            case 5:
                Destination->SetVolume(0.5f);
                break;
        }
    }

    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

/* Plays short quiet buffers and compares what reaches the endpoint, sample for sample */
static
VOID
XcCheckMix(VOID)
{
    enum
    {
        XcRoutePlain,
        XcRouteSubmix,
        XcRouteSubmixSlow,
        XcRouteMasterSlow,
        XcRouteMasterWide,
    };
    static const struct
    {
        WORD Tag;
        WORD Channels;
        DWORD Rate;
        WORD Bits;
        UINT Route;
        float Volume;
        float Ratio;
        INT Filter;
        float Frequency;
        float OneOverQ;
        UINT Scene;
    } Cases[] =
    {
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 2, 48000, 16, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 2, 48000, 8, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 2, 48000, 24, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 2, 48000, 32, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 32, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 44100, 16, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 2, 22050, 16, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 96000, 16, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_IEEE_FLOAT, 2, 44100, 32, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.5f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 22050, 8, XcRoutePlain, 1.0f, 0.75f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 0.6f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 1 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 2 },
        { WAVE_FORMAT_PCM, 1, 44100, 16, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 3 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.0f, LowPassFilter, 0.2f, 1.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.0f, HighPassFilter, 0.1f, 1.4f, 0 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.0f, BandPassFilter, 0.3f, 0.5f, 0 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.0f, NotchFilter, 0.4f, 1.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.0f, LowPassOnePoleFilter, 0.15f, 1.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.0f, HighPassOnePoleFilter, 0.15f, 1.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRoutePlain, 1.0f, 1.0f, LowPassFilter, 1.0f, 1.0f, 4 },
        { WAVE_FORMAT_PCM, 2, 48000, 16, XcRouteSubmix, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 1, 48000, 16, XcRouteSubmix, 1.0f, 1.0f, -1, 0.0f, 0.0f, 5 },
        { WAVE_FORMAT_PCM, 2, 48000, 16, XcRouteSubmixSlow, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 2, 48000, 16, XcRouteMasterSlow, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 6, 48000, 16, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_PCM, 2, 48000, 16, XcRouteMasterWide, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
        { WAVE_FORMAT_ADPCM, 1, 22050, 4, XcRoutePlain, 1.0f, 1.0f, -1, 0.0f, 0.0f, 0 },
    };
    static BYTE Data[48000 * 6 * 4];

    for (UINT Case = 0; Case < ARRAYSIZE(Cases); Case++)
    {
        XcProbeApo& Recorder = *new XcProbeApo(XAPO_FLAG_INPLACE_SUPPORTED | XAPO_FLAG_INPLACE_REQUIRED | XAPO_FLAG_CHANNELS_MUST_MATCH |
                                XAPO_FLAG_FRAMERATE_MUST_MATCH | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH |
                                XAPO_FLAG_BUFFERCOUNT_MUST_MATCH, FALSE);
        IXAudio2* Engine = nullptr;
        IXAudio2MasteringVoice* Master = nullptr;
        IXAudio2SubmixVoice* Submix = nullptr;
        IXAudio2SourceVoice* Source = nullptr;
        XcSceneCallback Scene;
        BYTE FormatStorage[sizeof(ADPCMWAVEFORMAT) + 7 * sizeof(ADPCMCOEFSET)];
        WAVEFORMATEX* Format = reinterpret_cast<WAVEFORMATEX*>(FormatStorage);
        XAUDIO2_SEND_DESCRIPTOR Send = {};
        XAUDIO2_VOICE_SENDS Sends = { 1, &Send };
        XAUDIO2_BUFFER Buffer = {};
        UINT32 Bytes;
        UINT Channels;
        SIZE_T Onset;
        SIZE_T Frames;
        HRESULT hr;

        if (FAILED(XcCreate(&Engine, 0)))
            return;
        hr = Engine->CreateMasteringVoice(&Master, Cases[Case].Route == XcRouteMasterWide ? 0 : 2,
                                          Cases[Case].Route == XcRouteMasterSlow ? 44100 : 48000);
        if (FAILED(hr))
        {
            printf("mix[%u] master = %08lX\n", Case, hr);
            Engine->Release();
            continue;
        }
        {
            XAUDIO2_VOICE_DETAILS Details;
            XAUDIO2_EFFECT_DESCRIPTOR Effect = { static_cast<IXAPO*>(&Recorder), TRUE, 0 };
            XAUDIO2_EFFECT_CHAIN Chain = { 1, &Effect };

            Master->GetVoiceDetails(&Details);
            Effect.OutputChannels = Details.InputChannels;
            Recorder.RecordedCapacity = static_cast<SIZE_T>(Details.InputSampleRate) * Details.InputChannels * 6 / 10;
            Recorder.Recorded = new (std::nothrow) float[Recorder.RecordedCapacity];
            hr = Master->SetEffectChain(&Chain);
            if (FAILED(hr) || Recorder.Recorded == nullptr)
            {
                printf("mix[%u] recorder = %08lX\n", Case, hr);
                Engine->Release();
                continue;
            }
            Channels = Details.InputChannels;
        }
        if (Cases[Case].Route == XcRouteSubmix || Cases[Case].Route == XcRouteSubmixSlow)
        {
            hr = Engine->CreateSubmixVoice(&Submix, 2, Cases[Case].Route == XcRouteSubmixSlow ? 32000 : 48000);
            if (FAILED(hr))
                printf("mix[%u] submix = %08lX\n", Case, hr);
        }

        memset(FormatStorage, 0, sizeof(FormatStorage));
        if (Cases[Case].Tag == WAVE_FORMAT_ADPCM)
        {
            ADPCMWAVEFORMAT* Adpcm = reinterpret_cast<ADPCMWAVEFORMAT*>(FormatStorage);
            static const SHORT Coef1[7] = { 256, 512, 0, 192, 240, 460, 392 };
            static const SHORT Coef2[7] = { 0, -256, 0, 64, 0, -208, -232 };
            ULONG Seed = 777;

            Adpcm->wfx.wFormatTag = WAVE_FORMAT_ADPCM;
            Adpcm->wfx.nChannels = 1;
            Adpcm->wfx.nSamplesPerSec = Cases[Case].Rate;
            Adpcm->wfx.wBitsPerSample = 4;
            Adpcm->wSamplesPerBlock = 128;
            Adpcm->wfx.nBlockAlign = 7 + 63;
            Adpcm->wfx.nAvgBytesPerSec = Cases[Case].Rate / 128 * 70;
            Adpcm->wfx.cbSize = 32;
            Adpcm->wNumCoef = 7;
            for (UINT Coef = 0; Coef < 7; Coef++)
            {
                Adpcm->aCoef[Coef].iCoef1 = Coef1[Coef];
                Adpcm->aCoef[Coef].iCoef2 = Coef2[Coef];
            }

            /* Blocks with small deltas and pseudo random nibbles stay quiet */
            Bytes = Cases[Case].Rate / 4 / 128 * 70;
            for (UINT32 Block = 0; Block < Bytes / 70; Block++)
            {
                BYTE* Header = Data + Block * 70;

                static const BYTE Steps[3] = { 0x0, 0x1, 0xF };

                Header[0] = static_cast<BYTE>(Block % 2 == 0 ? 0 : 4);
                Header[1] = 16;
                Header[2] = 0;
                Header[3] = 40;
                Header[4] = 0;
                Header[5] = 20;
                Header[6] = 0;
                for (UINT32 Byte = 7; Byte < 70; Byte++)
                {
                    Seed = Seed * 1103515245 + 12345;
                    Header[Byte] = static_cast<BYTE>((Steps[(Seed >> 16) % 3] << 4) | Steps[(Seed >> 20) % 3]);
                }
            }
        }
        else
        {
            XcPcm(Format, Cases[Case].Tag, Cases[Case].Channels, Cases[Case].Rate, Cases[Case].Bits);
            Bytes = Format->nAvgBytesPerSec / 4 / Format->nBlockAlign * Format->nBlockAlign;
            XcFillNoise(Data, Bytes, Format);
        }

        Send.pOutputVoice = Submix != nullptr ? static_cast<IXAudio2Voice*>(Submix) : Master;
        Send.Flags = 0;
        hr = Engine->CreateSourceVoice(&Source, Format, Cases[Case].Filter >= 0 ? XAUDIO2_VOICE_USEFILTER : 0, 2.0f, &Scene,
                                       &Sends);
        if (FAILED(hr))
        {
            printf("mix[%u] source = %08lX\n", Case, hr);
            Engine->Release();
            continue;
        }
        Scene.Voice = Source;
        Scene.Destination = Send.pOutputVoice;
        Scene.Scene = Cases[Case].Scene;
        Source->SetVolume(Cases[Case].Volume);
        if (Cases[Case].Ratio != 1.0f)
            Source->SetFrequencyRatio(Cases[Case].Ratio);
        if (Cases[Case].Filter >= 0)
        {
            XAUDIO2_FILTER_PARAMETERS Filter = { static_cast<XAUDIO2_FILTER_TYPE>(Cases[Case].Filter), Cases[Case].Frequency,
                                                 Cases[Case].OneOverQ };

            Source->SetFilterParameters(&Filter);
        }
        Buffer.AudioBytes = Bytes;
        Buffer.pAudioData = Data;
        Buffer.Flags = XAUDIO2_END_OF_STREAM;
        Source->SubmitSourceBuffer(&Buffer);

        /* The recorder on the mastering voice hears the mix before the endpoint does */
        Sleep(50);
        Recorder.Recording = TRUE;
        Source->Start();
        Sleep(400);
        Recorder.Recording = FALSE;
        {
            float Peak = 0.0f;

            for (SIZE_T Index = 0; Index < Recorder.RecordedCount; Index++)
                Peak = fabsf(Recorder.Recorded[Index]) > Peak ? fabsf(Recorder.Recorded[Index]) : Peak;
            fprintf(stderr, "mix[%u] peak %g\n", Case, Peak);
        }
        Source->DestroyVoice();
        if (Submix != nullptr)
            Submix->DestroyVoice();
        Engine->Release();

        for (Onset = 0; Onset < Recorder.RecordedCount; Onset++)
        {
            if (fabsf(Recorder.Recorded[Onset]) > 1e-4f)
                break;
        }
        Onset -= Onset % Channels;
        Frames = (Recorder.RecordedCount - Onset) / Channels;
        printf("mix[%u] format %u/%u/%lu/%u route %u volume %08lX ratio %08lX filter %d scene %u\n", Case, Cases[Case].Tag,
               Cases[Case].Channels, Cases[Case].Rate, Cases[Case].Bits, Cases[Case].Route, XcBits(Cases[Case].Volume),
               XcBits(Cases[Case].Ratio), Cases[Case].Filter, Cases[Case].Scene);
        if (Onset >= Recorder.RecordedCount)
        {
            printf("  silent\n");
            continue;
        }
        {
            const float* Start = &Recorder.Recorded[Onset];
            SIZE_T Count = Frames < 12000 ? Frames : 12000;
            double Sum[8] = {};
            double Power[8] = {};

            for (SIZE_T Frame = 0; Frame < Count; Frame++)
            {
                for (UINT Channel = 0; Channel < Channels && Channel < 8; Channel++)
                {
                    Sum[Channel] += Start[Frame * Channels + Channel];
                    Power[Channel] += static_cast<double>(Start[Frame * Channels + Channel]) * Start[Frame * Channels + Channel];
                }
            }
            for (UINT Channel = 0; Channel < Channels && Channel < 8; Channel++)
                printf("  channel %u sum %.9g power %.9g\n", Channel, Sum[Channel], Power[Channel]);
            for (SIZE_T Frame = 0; Frame < 1200 && Frame < Frames; Frame++)
            {
                if (Frame >= 40 && !(Frame >= 940 && Frame < 1000) && Frame < 1160)
                    continue;
                printf("  frame %4zu", Frame);
                for (UINT Channel = 0; Channel < Channels && Channel < 8; Channel++)
                    printf(" %08lX", XcBits(Start[Frame * Channels + Channel]));
                printf("\n");
            }
        }
    }
}

/* XAudio2 2.8 forwards everything to 2.9 */
static
VOID
XcCheckLegacy(
    _In_ PCWSTR Runtime)
{
    static const PCSTR Names[] =
    {
        "XAudio2Create", "CreateAudioReverb", "CreateAudioVolumeMeter", "CreateFX", "X3DAudioCalculate",
        "X3DAudioInitialize", "XAudio2CreateWithVersionInfo", "XAudio2CreateV2_9", "CreateAudioReverbV2_8",
    };
    static const PCSTR Targets[] =
    {
        "XAudio2Create", "CreateAudioReverbV2_8", "CreateAudioVolumeMeter", "CreateFX", "X3DAudioCalculate",
        "X3DAudioInitialize", "XAudio2CreateWithVersionInfo", "", "",
    };
    WCHAR Path[MAX_PATH];
    PWSTR Slash;
    HMODULE Legacy;

    XcRuntimePath(Runtime, Path, ARRAYSIZE(Path));
    Slash = wcsrchr(Path, L'\\');
    if (Slash == nullptr)
        return;
    wcscpy_s(Slash + 1, ARRAYSIZE(Path) - (Slash + 1 - Path), L"xaudio2_8.dll");
    Legacy = LoadLibraryW(Path);
    if (Legacy == nullptr)
    {
        printf("legacy missing\n");
        return;
    }
    for (UINT Index = 0; Index < ARRAYSIZE(Names); Index++)
    {
        FARPROC Named = GetProcAddress(Legacy, Names[Index]);
        int Ordinal = 0;

        for (int Probe = 1; Probe <= 16 && Named != nullptr; Probe++)
        {
            if (GetProcAddress(Legacy, MAKEINTRESOURCEA(Probe)) == Named)
            {
                Ordinal = Probe;
                break;
            }
        }
        printf("legacy %s = %s ordinal %d forwards %d\n", Names[Index], Named != nullptr ? "present" : "missing", Ordinal,
               Named != nullptr && Targets[Index][0] != '\0' && Named == GetProcAddress(XcModule, Targets[Index]));
    }
    FreeLibrary(Legacy);
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    PCWSTR Runtime = L"system";
    WCHAR Path[MAX_PATH];

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
            Runtime = argv[++Index];
        else if (_wcsicmp(argv[Index], L"-section") == 0 && Index + 1 < argc)
            XcSection = argv[++Index];
    }

    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    XcRuntimePath(Runtime, Path, ARRAYSIZE(Path));
    XcModule = LoadLibraryW(Path);
    if (XcModule == NULL)
    {
        fprintf(stderr, "error: cannot load the runtime (%lu)\n", GetLastError());
        return 1;
    }
    GetModuleFileNameW(XcModule, Path, ARRAYSIZE(Path));
    fprintf(stderr, "runtime %ls\n", Path);
    XcCreateFunction = reinterpret_cast<PXC_CREATE>(GetProcAddress(XcModule, "XAudio2Create"));
    if (XcCreateFunction == nullptr)
    {
        fprintf(stderr, "error: no XAudio2Create\n");
        return 1;
    }

    if (XcWant(L"exports"))
        XcCheckExports();
    if (XcWant(L"create"))
        XcCheckCreate();
    if (XcWant(L"engine"))
        XcCheckEngine();
    if (XcWant(L"master"))
        XcCheckMaster();
    if (XcWant(L"submix"))
        XcCheckSubmix();
    if (XcWant(L"formats"))
        XcCheckSourceFormats();
    if (XcWant(L"source"))
        XcCheckSourceArguments();
    if (XcWant(L"matrices"))
        XcCheckMatrices();
    if (XcWant(L"methods"))
        XcCheckMethods();
    if (XcWant(L"buffers"))
        XcCheckBuffers();
    if (XcWant(L"state"))
        XcCheckState();
    if (XcWant(L"callbacks"))
        XcCheckCallbacks();
    if (XcWant(L"queue"))
        XcCheckQueue();
    if (XcWant(L"legacy"))
        XcCheckLegacy(Runtime);
    if (XcWant(L"effects"))
        XcCheckEffects();

    /* Audible, so only on request */
    if (XcSection != nullptr && _wcsicmp(XcSection, L"mix") == 0)
        XcCheckMix();

    FreeLibrary(XcModule);
    CoUninitialize();
    return 0;
}
