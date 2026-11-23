/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The comb filter reverb from the FX library
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xapo.hpp"

#include <math.h>

#pragma fp_contract(off)

#define XA_RV_COMBS             7
#define XA_RV_GROUPS            5
#define XA_RV_LINE              9973
#define XA_RV_DECAY_SECONDS     4.0f
#define XA_RV_PRIMES            1230
#define XA_RV_CHANNELS          6

static const XAPO_REGISTRATION_PROPERTIES XaRvRegistration =
{
    __uuidof(FXReverb),
    L"Reverb",
    L"Copyright (c) Microsoft Corporation",
    1,
    0,
    XA_APO_FLAGS_STRICT | XAPO_FLAG_INPLACE_REQUIRED,
    1,
    1,
    1,
    1,
};

static const FXREVERB_PARAMETERS XaRvDefaults =
{
    FXREVERB_DEFAULT_DIFFUSION,
    FXREVERB_DEFAULT_ROOMSIZE,
};

/* Combs for each reverberated channel in order; the sixth one gets none */
static const UINT32 XaRvCombCounts[XA_RV_CHANNELS] = { 7, 7, 7, 7, 7, 0 };

/* Comb lengths in samples at the smallest room, seven for each reverberated channel */
static const UINT32 XaRvCombLengths[XA_RV_GROUPS][XA_RV_COMBS] =
{
    { 1571, 1951, 2111, 2297, 2459, 2657, 2851 },
    { 1583, 1933, 2129, 2287, 2573, 2633, 2857 },
    { 1579, 1949, 2113, 2293, 2467, 2647, 2843 },
    { 1559, 1931, 2131, 2281, 2477, 2621, 2833 },
    { 1567, 1979, 2099, 2309, 2473, 2659, 2857 },
};

/* Second order sections as b0, b1, b2, a0, a1, a2 */
static const float XaRvDcBlock[6] = { 1.0f, -1.99999428f, 0.999994278f, 1.0f, -1.99714649f, 0.997154534f };
static const float XaRvSmooth[6] = { 0.997154534f, -0.997160256f, 0.0f, 1.0f, -0.997154534f, 0.0f };
static const float XaRvBand[6] = { 0.178900003f, 0.314000010f, 0.179399997f, 1.0f, -0.354299992f, 0.0575000010f };
static const float XaRvLoop[6] = { 0.405400008f, 0.756500006f, 0.226699993f, 1.0f, 0.473500013f, -0.0852999985f };

/* Four lane coefficient rows; history runs oldest first so lane three meets the newest value */
struct XaRvSection
{
    float Forward[4];
    float Back[4];
};

static
VOID
XaRvDesign(
    _Out_ XaRvSection* Section,
    _In_reads_(6) const float* Values)
{
    Section->Forward[0] = 0.0f;
    Section->Forward[1] = Values[2];
    Section->Forward[2] = Values[1];
    Section->Forward[3] = Values[0];
    Section->Back[0] = 0.0f;
    Section->Back[1] = 0.0f;
    Section->Back[2] = -Values[5];
    Section->Back[3] = -Values[4];
}

static
inline
VOID
XaRvShift(
    _Inout_updates_(4) float* History,
    _In_ float Value)
{
    History[0] = History[1];
    History[1] = History[2];
    History[2] = History[3];
    History[3] = Value;
}

/* Lane products summed as the input filters do, newest lane first */
static
inline
float
XaRvRunning(
    _In_reads_(4) const float* Left,
    _In_reads_(4) const float* Right)
{
    return ((Left[3] * Right[3] + Left[2] * Right[2]) + Left[1] * Right[1]) + Left[0] * Right[0];
}

/* Lane products summed in pairs, as the combs do */
static
inline
float
XaRvPaired(
    _In_reads_(4) const float* Left,
    _In_reads_(4) const float* Right)
{
    return (Left[0] * Right[0] + Left[2] * Right[2]) + (Left[1] * Right[1] + Left[3] * Right[3]);
}

/* fcvtzu to 32 bits: NaN and negatives give zero, large values saturate */
static
inline
UINT32
XaRvToIndex(
    _In_ float Value)
{
    if (!(Value > 0.0f))
        return 0;
    if (Value >= 4294967296.0f)
        return MAXUINT32;
    return static_cast<UINT32>(Value);
}

static UINT32 XaRvPrimeTable[XA_RV_PRIMES];
static INIT_ONCE XaRvPrimeOnce = INIT_ONCE_STATIC_INIT;

/* One followed by every prime up to the line length */
static
BOOL
CALLBACK
XaRvFillPrimes(
    _Inout_ PINIT_ONCE Once,
    _Inout_opt_ PVOID Context,
    _Out_opt_ PVOID* Result)
{
    UINT32 Count = 1;

    UNREFERENCED_PARAMETER(Once);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Result);
    XaRvPrimeTable[0] = 1;
    for (UINT32 Candidate = 2; Candidate <= XA_RV_LINE && Count < XA_RV_PRIMES; Candidate++)
    {
        BOOLEAN Prime = TRUE;

        for (UINT32 Divisor = 2; Divisor * Divisor <= Candidate; Divisor++)
        {
            if (Candidate % Divisor == 0)
            {
                Prime = FALSE;
                break;
            }
        }
        if (Prime)
            XaRvPrimeTable[Count++] = Candidate;
    }
    return TRUE;
}

/* The prime equal to the value, or else the next one up */
UINT32
XaReverbPrime(
    _In_ UINT32 Value)
{
    INT Low = 0;
    INT High = XA_RV_PRIMES - 1;

    InitOnceExecuteOnce(&XaRvPrimeOnce, XaRvFillPrimes, nullptr, nullptr);
    if (Value <= 1)
        return 1;
    if (Value >= XA_RV_LINE)
        return XA_RV_LINE;
    while (Low <= High)
    {
        INT Middle = (Low + High) / 2;

        if (XaRvPrimeTable[Middle] == Value)
            return Value;
        if (XaRvPrimeTable[Middle] < Value)
            Low = Middle + 1;
        else
            High = Middle - 1;
    }
    return XaRvPrimeTable[Low];
}

/* A feedback comb whose loop runs through three sections; two read taps let a new length fade in */
struct XaRvComb
{
    XaRvSection Sections[3];
    float LastForward[4];
    float Taps[4];
    float Stages[3][4];
    float Feedback;
    float Through;
    float Weight;
    float* Line;
    UINT32 Write;
    UINT32 Read;
    UINT32 ReadBefore;
};

static
VOID
XaRvCombDecay(
    _Inout_ XaRvComb* Comb,
    _In_ UINT32 Rate)
{
    float PerSample = 1.0f / static_cast<float>(Rate);
    float PerDecay = 1.0f / XA_RV_DECAY_SECONDS;

    /* The decay follows the full line length, whatever the tap distance */
    Comb->Feedback = powf(0.001f, (static_cast<float>(XA_RV_LINE) * PerSample) * PerDecay);
    Comb->Through = sqrtf(fmaf(-Comb->Feedback, Comb->Feedback, 1.0f));
    for (UINT32 Lane = 0; Lane < 4; Lane++)
        Comb->LastForward[Lane] = Comb->Sections[2].Forward[Lane] * Comb->Feedback;
}

static
VOID
XaRvCombLength(
    _Inout_ XaRvComb* Comb,
    _In_ UINT32 Length,
    _In_ BOOLEAN Fade)
{
    UINT32 Tap = (Comb->Write + XA_RV_LINE - Length) % XA_RV_LINE;

    if (!Fade)
        Comb->ReadBefore = Tap;
    Comb->Read = Tap;
}

/* Runs one comb over a channel and adds its tap into the output */
static
VOID
XaRvCombRun(
    _Inout_ XaRvComb* Comb,
    _Inout_ float* Output,
    _In_ const float* Input,
    _In_ UINT32 Frames,
    _In_ UINT32 Stride)
{
    float* Line = Comb->Line;
    float Span = 1.0f / static_cast<float>(Frames - 1);
    float Fresh = Line[Comb->Read];
    float Stale = Line[Comb->ReadBefore];
    float Taps[4];
    float Steps = 0.0f;
    float Rising = 0.0f;
    float Falling = 1.0f;

    memcpy(Taps, Comb->Taps, sizeof(Taps));
    XaRvShift(Taps, Fresh);
    for (UINT32 Frame = 0; Frame < Frames; Frame++)
    {
        float Sample = *Input;
        float Tap = fmaf(Fresh, Rising, Stale * Falling);
        float Stage;

        *Output = fmaf(Tap, Comb->Weight, *Output);
        Input += Stride;
        Output += Stride;
        memcpy(Comb->Taps, Taps, sizeof(Taps));

        Stage = XaRvPaired(Comb->Stages[0], Comb->Sections[0].Back) + XaRvPaired(Taps, Comb->Sections[0].Forward);
        XaRvShift(Comb->Stages[0], Stage);
        Stage = XaRvPaired(Comb->Stages[1], Comb->Sections[1].Back) + XaRvPaired(Comb->Stages[0], Comb->Sections[1].Forward);
        XaRvShift(Comb->Stages[1], Stage);
        Stage = XaRvPaired(Comb->Stages[2], Comb->Sections[2].Back) + XaRvPaired(Comb->Stages[1], Comb->LastForward);
        XaRvShift(Comb->Stages[2], Stage);

        /* Both taps move on and are read before this sample lands in the line */
        Comb->Read = (Comb->Read + 1) % XA_RV_LINE;
        Comb->ReadBefore = (Comb->ReadBefore + 1) % XA_RV_LINE;
        Fresh = Line[Comb->Read];
        Stale = Line[Comb->ReadBefore];
        Line[Comb->Write] = fmaf(Comb->Through, Sample, Stage);
        Comb->Write = (Comb->Write + 1) % XA_RV_LINE;
        XaRvShift(Taps, Fresh);

        Steps = Steps + 1.0f;
        Rising = Steps * Span;
        Falling = 1.0f - Rising;
    }
    Comb->ReadBefore = Comb->Read;
}

/* The input chain: one section with a history per channel */
struct XaRvFilter
{
    XaRvSection Section;
    float* History;
};

static
VOID
XaRvFilterRun(
    _Inout_ XaRvFilter* Filter,
    _Out_writes_(Frames * Channels) float* Output,
    _In_reads_(Frames * Channels) const float* Input,
    _In_ UINT32 Channels,
    _In_ UINT32 Frames)
{
    for (UINT32 Frame = 0; Frame < Frames; Frame++)
    {
        float* History = Filter->History;

        for (UINT32 Channel = 0; Channel < Channels; Channel++)
        {
            float* Inputs = History;
            float* Outputs = History + 4;
            float Result;

            XaRvShift(Inputs, *Input++);
            Result = XaRvRunning(Inputs, Filter->Section.Forward) + XaRvRunning(Outputs, Filter->Section.Back);
            *Output++ = Result;
            XaRvShift(Outputs, Result);
            History += 8;
        }
    }
}

class XaRv final : public XaApo
{
public:
    XaRv() : XaApo(&XaRvRegistration)
    {
        Memory = nullptr;
        ClearState();
    }

    ~XaRv() override
    {
        CoTaskMemFree(Memory);
    }

    /* Creation data is not used */
    HRESULT
    STDMETHODCALLTYPE
    Initialize(
        _In_reads_bytes_opt_(Size) const void* Data,
        UINT32 Size) override
    {
        UNREFERENCED_PARAMETER(Data);
        UNREFERENCED_PARAMETER(Size);
        return S_OK;
    }

    HRESULT
    ValidateFormat(
        _Inout_ WAVEFORMATEX* Format,
        _In_ BOOLEAN Fix) override
    {
        HRESULT hr = XaApo::ValidateFormat(Format, Fix);

        if (Format->nChannels < 1 || Format->nChannels > 2)
        {
            hr = XAPO_E_FORMAT_UNSUPPORTED;
            if (Fix)
                Format->nChannels = 1;
        }
        return hr;
    }

    /* Clears every history and line; tap positions stay where they are */
    void
    STDMETHODCALLTYPE
    Reset() override
    {
        for (UINT32 Index = 0; Index < ARRAYSIZE(Filters); Index++)
            memset(Filters[Index].History, 0, sizeof(*Filters[Index].History) * 8 * Channels);
        for (UINT32 Index = 0; Index < CombCount; Index++)
        {
            memset(Combs[Index].Line, 0, sizeof(*Combs[Index].Line) * XA_RV_LINE);
            memset(Combs[Index].Taps, 0, sizeof(Combs[Index].Taps));
            memset(Combs[Index].Stages, 0, sizeof(Combs[Index].Stages));
        }
    }

    HRESULT
    STDMETHODCALLTYPE
    LockForProcess(
        UINT32 InputCount,
        _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
        UINT32 OutputCount,
        _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output) override;

    /* Unlocking also wipes the parameters back to zero */
    void
    STDMETHODCALLTYPE
    UnlockForProcess() override
    {
        static const FXREVERB_PARAMETERS Empty = {};

        CoTaskMemFree(Memory);
        Memory = nullptr;
        ClearState();
        Parameters.Overwrite(&Empty);
        XaApo::UnlockForProcess();
    }

    void
    STDMETHODCALLTYPE
    Process(
        UINT32 InputCount,
        _In_reads_opt_(InputCount) const XAPO_PROCESS_BUFFER_PARAMETERS* Input,
        UINT32 OutputCount,
        _Inout_updates_opt_(OutputCount) XAPO_PROCESS_BUFFER_PARAMETERS* Output,
        BOOL Enabled) override;

    VOID
    ClearState(VOID)
    {
        Channels = 0;
        Rate = 0;
        CombCount = 0;
        Filtered = nullptr;
        Combs = nullptr;
        memset(Filters, 0, sizeof(Filters));
        memset(ChannelCombs, 0, sizeof(ChannelCombs));
        memset(ChannelCounts, 0, sizeof(ChannelCounts));
    }

    VOID
    ApplyDiffusion(
        _In_ float Diffusion);

    VOID
    ApplyRoom(
        _In_ float Room,
        _In_ BOOLEAN Fade);

    VOID* Memory;
    UINT32 Channels;
    UINT32 Rate;
    UINT32 CombCount;
    float Diffusion;
    float Room;
    float Slope;
    float Smallest;
    float* Filtered;
    XaRvFilter Filters[3];
    XaRvComb* Combs;

    /* First comb of each channel and how many follow; the LFE has none */
    XaRvComb* ChannelCombs[XA_RV_CHANNELS];
    UINT32 ChannelCounts[XA_RV_CHANNELS];
};

/* Spreads the output over the first combs: more diffusion brings more of them in */
VOID
XaRv::ApplyDiffusion(
    _In_ float NewDiffusion)
{
    Diffusion = NewDiffusion;
    for (UINT32 Channel = 0; Channel < XaMin(Channels, static_cast<UINT32>(XA_RV_CHANNELS)); Channel++)
    {
        XaRvComb* Set = ChannelCombs[Channel];
        UINT32 Count = ChannelCounts[Channel];
        float Reach = fmaf(static_cast<float>(Count) - 1.0f, NewDiffusion, 1.0f);
        float Whole = floorf(Reach);
        UINT32 Full = XaRvToIndex(Whole);
        float Inside = 1.0f / sqrtf(Whole);
        float Beyond = 1.0f / sqrtf(Whole + 1.0f);
        float Part = sqrtf(Reach - Whole);
        float Level = fmaf(Beyond - Inside, Part, Inside);
        UINT32 Index = 0;

        if (Set == nullptr)
            continue;
        for (; Index < Full && Index < Count; Index++)
            Set[Index].Weight = Level;
        if (Index < Full)
            continue;
        if (Index < Count)
        {
            Set[Index].Weight = Part * Beyond;
            Index += 1;
        }

        /* Combs left out go quiet and forget what they held */
        for (; Index < Count; Index++)
        {
            Set[Index].Weight = 0.0f;
            memset(Set[Index].Line, 0, sizeof(*Set[Index].Line) * XA_RV_LINE);
            memset(Set[Index].Taps, 0, sizeof(Set[Index].Taps));
            memset(Set[Index].Stages, 0, sizeof(Set[Index].Stages));
        }
    }
}

/* Room size scales every comb length, rounded to a prime */
VOID
XaRv::ApplyRoom(
    _In_ float NewRoom,
    _In_ BOOLEAN Fade)
{
    float Scale = fmaf(Slope, NewRoom, Smallest);
    UINT32 Group = 0;

    Room = NewRoom;
    for (UINT32 Channel = 0; Channel < XaMin(Channels, static_cast<UINT32>(XA_RV_CHANNELS)); Channel++)
    {
        XaRvComb* Set = ChannelCombs[Channel];

        if (Set == nullptr)
            continue;
        for (UINT32 Index = 0; Index < ChannelCounts[Channel]; Index++)
        {
            float Length = fmaf(static_cast<float>(XaRvCombLengths[Group][Index]), Scale, 0.5f);

            XaRvCombLength(&Set[Index], XaReverbPrime(XaRvToIndex(Length)), Fade);
        }
        Group += 1;
    }
}

HRESULT
STDMETHODCALLTYPE
XaRv::LockForProcess(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
    UINT32 OutputCount,
    _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output)
{
    const WAVEFORMATEX* Format;
    UINT32 Lfe;
    UINT32 Shortest = XaRvCombLengths[0][0];
    UINT32 Longest = XaRvCombLengths[0][0];
    SIZE_T Floats;
    float* Next;
    HRESULT hr;

    hr = XaApo::LockForProcess(InputCount, Input, OutputCount, Output);
    if (FAILED(hr))
    {
        UnlockForProcess();
        return hr;
    }
    CoTaskMemFree(Memory);
    ClearState();
    InitOnceExecuteOnce(&XaRvPrimeOnce, XaRvFillPrimes, nullptr, nullptr);
    Format = Input->pFormat;
    Channels = Format->nChannels;
    Rate = Format->nSamplesPerSec;

    /* An extensible layout with an LFE leaves that channel dry; its index counts the speakers before it */
    Lfe = Channels;
    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        DWORD Mask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format)->dwChannelMask;

        if ((Mask & SPEAKER_LOW_FREQUENCY) != 0)
            Lfe = (Mask & 1) + ((Mask >> 1) & 1) + ((Mask >> 2) & 1);
    }
    {
        UINT32 Reverberated = XaMin(Lfe < Channels ? Channels - 1 : Channels, static_cast<UINT32>(XA_RV_CHANNELS));

        for (UINT32 Index = 0; Index < Reverberated; Index++)
            CombCount += XaRvCombCounts[Index];
    }

    Floats = static_cast<SIZE_T>(Channels) * Input->MaxFrameCount + 3 * 8 * static_cast<SIZE_T>(Channels) +
             static_cast<SIZE_T>(CombCount) * XA_RV_LINE;
    Memory = CoTaskMemAlloc(sizeof(XaRvComb) * CombCount + sizeof(float) * Floats);
    if (Memory == nullptr)
    {
        UnlockForProcess();
        return E_OUTOFMEMORY;
    }
    memset(Memory, 0, sizeof(XaRvComb) * CombCount + sizeof(float) * Floats);
    Combs = static_cast<XaRvComb*>(Memory);
    Next = reinterpret_cast<float*>(Combs + CombCount);
    Filtered = Next;
    Next += static_cast<SIZE_T>(Channels) * Input->MaxFrameCount;
    for (UINT32 Index = 0; Index < ARRAYSIZE(Filters); Index++)
    {
        Filters[Index].History = Next;
        Next += 8 * Channels;
    }
    XaRvDesign(&Filters[0].Section, XaRvDcBlock);
    XaRvDesign(&Filters[1].Section, XaRvSmooth);
    XaRvDesign(&Filters[2].Section, XaRvBand);

    {
        UINT32 Used = 0;
        UINT32 Order = 0;

        for (UINT32 Channel = 0; Channel < XaMin(Channels, static_cast<UINT32>(XA_RV_CHANNELS)); Channel++)
        {
            if (Channel == Lfe || Order >= XA_RV_CHANNELS)
                continue;
            ChannelCombs[Channel] = Combs + Used;
            ChannelCounts[Channel] = XaRvCombCounts[Order];
            Used += XaRvCombCounts[Order];
            Order += 1;
        }
    }
    for (UINT32 Index = 0; Index < CombCount; Index++)
    {
        XaRvComb* Comb = &Combs[Index];

        for (UINT32 Section = 0; Section < 3; Section++)
            XaRvDesign(&Comb->Sections[Section], XaRvLoop);
        Comb->Line = Next;
        Next += XA_RV_LINE;
        Comb->Through = 1.0f;
        Comb->Weight = 1.0f;
        XaRvCombDecay(Comb, Rate);
    }

    /* The room line runs from the shortest comb at the smallest room to the full line at the largest */
    for (UINT32 Group = 0; Group < XA_RV_GROUPS; Group++)
    {
        for (UINT32 Index = 0; Index < XA_RV_COMBS; Index++)
        {
            Shortest = XaMin(Shortest, XaRvCombLengths[Group][Index]);
            Longest = XaMax(Longest, XaRvCombLengths[Group][Index]);
        }
    }
    Smallest = 1.0f / static_cast<float>(Shortest);
    Slope = static_cast<float>(XA_RV_LINE) / static_cast<float>(Longest) - Smallest;
    ApplyDiffusion(XaRvDefaults.Diffusion);
    ApplyRoom(XaRvDefaults.RoomSize, FALSE);

    /* Every lock starts from the defaults, whatever was set before */
    Parameters.Overwrite(&XaRvDefaults);
    return S_OK;
}

void
STDMETHODCALLTYPE
XaRv::Process(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_PROCESS_BUFFER_PARAMETERS* Input,
    UINT32 OutputCount,
    _Inout_updates_opt_(OutputCount) XAPO_PROCESS_BUFFER_PARAMETERS* Output,
    BOOL Enabled)
{
    const FXREVERB_PARAMETERS* Changed = reinterpret_cast<const FXREVERB_PARAMETERS*>(Parameters.Take());
    UINT32 Frames = Input->ValidFrameCount;

    UNREFERENCED_PARAMETER(InputCount);
    UNREFERENCED_PARAMETER(OutputCount);
    if (Changed != nullptr)
    {
        if (Changed->Diffusion != Diffusion)
            ApplyDiffusion(Changed->Diffusion);
        if (Changed->RoomSize != Room)
            ApplyRoom(Changed->RoomSize, TRUE);
    }

    /* Only runs in place: the dry signal comes from the output side and the wet one lands on the input side */
    if (Input->BufferFlags != XAPO_BUFFER_VALID || !Enabled)
        return;
    XaRvFilterRun(&Filters[0], Filtered, static_cast<const float*>(Output->pBuffer), Channels, Frames);
    XaRvFilterRun(&Filters[1], Filtered, Filtered, Channels, Frames);
    XaRvFilterRun(&Filters[2], Filtered, Filtered, Channels, Frames);
    memset(Input->pBuffer, 0, sizeof(float) * Channels * Frames);
    for (UINT32 Channel = 0; Channel < XaMin(Channels, static_cast<UINT32>(XA_RV_CHANNELS)); Channel++)
    {
        XaRvComb* Set = ChannelCombs[Channel];

        if (Set == nullptr)
            continue;
        for (UINT32 Index = 0; Index < ChannelCounts[Channel]; Index++)
        {
            if (Set[Index].Weight != 0.0f)
                XaRvCombRun(&Set[Index], static_cast<float*>(Input->pBuffer) + Channel, Filtered + Channel, Frames, Channels);
        }
    }
}

HRESULT
XaCreateFxReverbObject(
    _Outptr_ IUnknown** Apo)
{
    static const FXREVERB_PARAMETERS Empty = {};
    XaRv* Reverb = new (std::nothrow) XaRv();

    if (Reverb == nullptr)
        return E_OUTOFMEMORY;
    if (!Reverb->Parameters.Allocate(sizeof(FXREVERB_PARAMETERS), &Empty))
    {
        Reverb->Release();
        return E_OUTOFMEMORY;
    }
    *Apo = static_cast<IXAPO*>(Reverb);
    return S_OK;
}
