/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The loudness driven mastering limiter from the FX library
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xapo.hpp"

#include <math.h>

#pragma fp_contract(off)

#define XA_LEQ_BANDS            25
#define XA_LEQ_LARGEST_ORDER    12
#define XA_LEQ_PI               3.141592653589793
#define XA_LEQ_WINDOW_PI        3.14159265358979

static const XAPO_REGISTRATION_PROPERTIES XaLimiterRegistration =
{
    __uuidof(FXMasteringLimiter),
    L"Mastering Limiter",
    L"Copyright (c) Microsoft Corporation",
    1,
    0,
    XA_APO_FLAGS_STRICT | XAPO_FLAG_INPLACE_REQUIRED,
    1,
    1,
    1,
    1,
};

static const FXMASTERINGLIMITER_PARAMETERS XaLimiterDefaults =
{
    FXMASTERINGLIMITER_DEFAULT_RELEASE,
    FXMASTERINGLIMITER_DEFAULT_LOUDNESS,
};

/* Upper edge of each critical band in Hz */
static const UINT32 XaLeqBandTops[XA_LEQ_BANDS] =
{
    100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720, 2000,
    2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500, 22000,
};

/* Band energy below these levels does not count toward loudness */
static const float XaLeqQuietLevels[XA_LEQ_BANDS] =
{
    1000.0f, 100.0f, 31.6f, 10.0f, 3.16f, 2.0f, 1.0f, 1.0f, 0.316f, 0.316f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 4.0f, 10.0f, 100.0f, 1000.0f,
};

/* fmin and fmax as the hardware does them: NaN wins and -0 sorts below +0 */
static
inline
float
XaLeqLower(
    _In_ float Left,
    _In_ float Right)
{
    UINT32 LeftBits;
    UINT32 RightBits;

    if (Left != Left || Right != Right)
        return Left + Right;
    if (Left != Right)
        return Left < Right ? Left : Right;
    memcpy(&LeftBits, &Left, sizeof(LeftBits));
    memcpy(&RightBits, &Right, sizeof(RightBits));
    LeftBits |= RightBits;
    memcpy(&Left, &LeftBits, sizeof(Left));
    return Left;
}

static
inline
float
XaLeqUpper(
    _In_ float Left,
    _In_ float Right)
{
    UINT32 LeftBits;
    UINT32 RightBits;

    if (Left != Left || Right != Right)
        return Left + Right;
    if (Left != Right)
        return Left > Right ? Left : Right;
    memcpy(&LeftBits, &Left, sizeof(LeftBits));
    memcpy(&RightBits, &Right, sizeof(RightBits));
    LeftBits &= RightBits;
    memcpy(&Left, &LeftBits, sizeof(Left));
    return Left;
}

static
inline
VOID
XaLeqSwap(
    _Inout_ float* Data,
    _In_ INT First,
    _In_ INT Second)
{
    float Kept = Data[First];

    Data[First] = Data[Second];
    Data[Second] = Kept;
}

/* Reorders the transform output into natural order, two complex values per step */
static
VOID
XaLeqUnscramble(
    _Inout_ float* Data,
    _In_ INT Count)
{
    INT Mirror = 0;

    for (INT Slot = 0; Slot < Count; Slot += 4)
    {
        INT Step;

        if (Slot < Mirror)
        {
            XaLeqSwap(Data, Slot, Mirror);
            XaLeqSwap(Data, Slot + 1, Mirror + 1);
            XaLeqSwap(Data, Slot + Count + 2, Mirror + Count + 2);
            XaLeqSwap(Data, Slot + Count + 3, Mirror + Count + 3);
        }
        XaLeqSwap(Data, Slot + 2, Mirror + Count);
        XaLeqSwap(Data, Slot + 3, Mirror + Count + 1);
        for (Step = Count / 2; Step <= Mirror; Step /= 2)
            Mirror -= Step;
        Mirror += Step;
    }
}

/* One radix two pass over a pair of complex values: Even keeps the sum, Odd the rotated difference */
static
inline
VOID
XaLeqButterfly(
    _Inout_ float* Even,
    _Inout_ float* Odd,
    _In_ float TurnReal,
    _In_ float TurnImag)
{
    float Real = Even[0] - Odd[0];
    float Imag = Even[1] - Odd[1];

    Even[0] = Even[0] + Odd[0];
    Even[1] = Even[1] + Odd[1];
    Odd[0] = fmaf(-TurnImag, Imag, TurnReal * Real);
    Odd[1] = fmaf(TurnImag, Real, TurnReal * Imag);
}

/* Forward complex transform of Count interleaved values, decimated in frequency */
static
VOID
XaLeqTransform(
    _Inout_ float* Data,
    _In_ INT Count)
{
    INT Length = Count * 2;
    INT Span = Length;
    double Step = 1.0 / static_cast<double>(Count);
    double StartCos = cos(XA_LEQ_PI * Step);
    double StartSin = sin(-XA_LEQ_PI * Step);

    if (Length > 4)
    {
        float Sine = static_cast<float>(StartSin);
        float Cosine = static_cast<float>(StartCos);

        for (;;)
        {
            float Twice = Sine + Sine;
            float PairCos = fmaf(-Twice, Sine, 1.0f);
            float PairSin = Twice * Cosine;
            float Advance = Twice * (Cosine + Cosine);
            float FirstCos = 1.0f;
            float FirstSin = 0.0f;
            INT Middle = Span >> 1;

            Sine = PairSin;
            Cosine = PairCos;

            /* The first pair of each group needs no rotation */
            for (INT Base = 0; Base < Length; Base += Span)
            {
                float* Even = Data + Base;
                float* Odd = Data + Base + Middle;
                float Imag = Even[1] - Odd[1];
                float Real = Even[0] - Odd[0];

                Even[0] = Even[0] + Odd[0];
                Even[1] = Even[1] + Odd[1];
                Odd[0] = Real;
                Odd[1] = Imag;
                XaLeqButterfly(Even + 2, Odd + 2, PairCos, PairSin);
            }
            if (Middle <= 4)
            {
                Span >>= 1;
                break;
            }
            for (INT Offset = 4; Offset < Middle; Offset += 4)
            {
                FirstCos = fmaf(-Advance, PairSin, FirstCos);
                FirstSin = fmaf(Advance, PairCos, FirstSin);
                PairSin = fmaf(Advance, FirstCos, PairSin);
                PairCos = fmaf(-Advance, FirstSin, PairCos);
                for (INT Base = Offset; Base < Length + Offset; Base += Span)
                {
                    XaLeqButterfly(Data + Base, Data + Base + Middle, FirstCos, FirstSin);
                    XaLeqButterfly(Data + Base + 2, Data + Base + Middle + 2, PairCos, PairSin);
                }
            }
            Span >>= 1;
        }
    }
    if (Span > 2)
    {
        for (INT Base = 0; Base < Length; Base += 4)
        {
            float* Value = Data + Base;
            float Real = Value[0];
            float Imag = Value[1];

            Value[0] = Value[2] + Real;
            Value[1] = Value[3] + Imag;
            Value[2] = Real - Value[2];
            Value[3] = Imag - Value[3];
        }
    }
    if (Length > 4)
        XaLeqUnscramble(Data, Count);
}

/* Real transform of Length samples, packed with the Nyquist term in slot one */
static
VOID
XaLeqRealTransform(
    _Inout_ float* Data,
    _In_ INT Length)
{
    double Angle = (2.0 * XA_LEQ_PI) / static_cast<double>(Length);
    float RotateCos = static_cast<float>(cos(Angle));
    float RotateSin = static_cast<float>(sin(Angle));
    float TurnCos = 1.0f;
    float TurnSin = 0.0f;
    INT Middle = Length >> 1;
    float* Imag = Data + 1;
    float Low;
    float High;

    XaLeqTransform(Data, Middle);
    for (INT Front = 2; Front < Middle; Front += 2)
    {
        INT Back = Length - Front;
        float NextCos = fmaf(-RotateSin, TurnSin, RotateCos * TurnCos);
        float Sum = Data[Back] + Data[Front];
        float Difference = Data[Front] - Data[Back];
        float ImagSum;
        float ImagDifference;
        float MixReal;
        float MixImag;

        TurnSin = fmaf(RotateSin, TurnCos, RotateCos * TurnSin);
        TurnCos = NextCos;
        ImagSum = Imag[Back] + Imag[Front];
        ImagDifference = Imag[Front] - Imag[Back];
        MixReal = fmaf(-(TurnSin * Difference), 0.5f, (TurnCos * ImagSum) * 0.5f);
        MixImag = fmaf(-(TurnCos * Difference), 0.5f, (TurnSin * ImagSum) * -0.5f);
        Data[Front] = fmaf(Sum, 0.5f, MixReal);
        Data[Back] = fmaf(Sum, 0.5f, -MixReal);
        Imag[Front] = fmaf(ImagDifference, 0.5f, MixImag);
        Imag[Back] = fmaf(-ImagDifference, 0.5f, MixImag);
    }
    Data[Middle + 1] = -Data[Middle + 1];
    Low = Data[0];
    High = Data[1];
    Data[1] = Low - High;
    Data[0] = High + Low;
}

/* The analysis and fade state of one locked limiter */
struct XaLeq
{
    UINT32 Channels;
    UINT32 Rate;
    UINT32 Size;
    UINT32 Half;
    UINT32 Bins;
    UINT32 Filled;
    UINT32 Release;
    UINT32 Loudness;
    float Gain;
    float Recovery;
    UINT32 BandEnds[XA_LEQ_BANDS + 1];
    float* Window;
    float* Spectrum;

    /* Halves of input and output: the older input, the one filling, the one playing and the one queued */
    float* Older;
    float* Filling;
    float* Playing;
    float* Queued;
};

static
float
XaLeqRecovery(
    _In_ const XaLeq* Leq)
{
    double Seconds = static_cast<double>(Leq->Half) / static_cast<double>(Leq->Rate);
    double Quarter = static_cast<double>(static_cast<float>(Leq->Release) * 0.25f);

    return static_cast<float>(pow(10.0, (Seconds * Quarter) * 5.0));
}

static
VOID
XaLeqClear(
    _Inout_ XaLeq* Leq)
{
    SIZE_T Bytes = sizeof(*Leq->Older) * Leq->Half * Leq->Channels;

    memset(Leq->Older, 0, Bytes);
    memset(Leq->Filling, 0, Bytes);
    memset(Leq->Playing, 0, Bytes);
    memset(Leq->Queued, 0, Bytes);
    Leq->Filled = 0;
    Leq->Gain = 1.0f;
}

/* Sizes the transform to cover about twenty milliseconds */
static
VOID
XaLeqMeasure(
    _Inout_ XaLeq* Leq)
{
    UINT32 Hundredth = Leq->Rate / 100;
    UINT32 Order = (Hundredth & (Hundredth - 1)) != 0 ? 1 : 0;

    for (UINT32 Rest = Hundredth; Rest > 1; Rest >>= 1)
        Order += 1;
    Order += 1;
    if (Order > XA_LEQ_LARGEST_ORDER)
        Order = XA_LEQ_LARGEST_ORDER;
    Leq->Size = 1u << Order;
    Leq->Half = Leq->Size >> 1;
}

/* Lays out the band edges in transform bins and the analysis window */
static
VOID
XaLeqSetup(
    _Inout_ XaLeq* Leq)
{
    UINT32 BinWidth;
    double Length;
    double Middle;

    BinWidth = Leq->Rate / Leq->Size;
    Leq->BandEnds[0] = 0;
    for (UINT32 Band = 0; Band < XA_LEQ_BANDS; Band++)
    {
        UINT32 End = XaLeqBandTops[Band] / BinWidth + 1;

        if (End < Leq->BandEnds[Band] + 1)
            End = Leq->BandEnds[Band] + 1;
        if (End > Leq->Half)
            End = Leq->Half;
        Leq->BandEnds[Band + 1] = End;
    }
    Leq->Bins = Leq->BandEnds[XA_LEQ_BANDS];

    /* Hann window scaled up to sixteen bit range over half the length */
    Length = static_cast<double>(Leq->Size);
    Middle = static_cast<double>(Leq->Half);
    for (UINT32 Index = 0; Index < Leq->Size; Index++)
    {
        double Point = static_cast<double>(static_cast<float>(Index));
        double Phase = cos((((Point + 1.0) + Point) / Length) * XA_LEQ_WINDOW_PI);

        Leq->Window[Index] = static_cast<float>((fma(-Phase, 0.5, 0.5) / Middle) * 32768.0);
    }
}

/* Loudness of the window spanning both halves, turned into the gain that reaches the target */
static
float
XaLeqTargetGain(
    _Inout_ XaLeq* Leq)
{
    float Bands[XA_LEQ_BANDS] = {};
    float* Spectrum = Leq->Spectrum;
    UINT32 Channels = Leq->Channels;
    UINT32 Passes = Channels == 6 ? 4 : Channels;
    float Sum = 0.0f;

    /* With 5.1 the back channels fold into the front pair and the LFE counts on its own */
    for (UINT32 Channel = 0; Channel < Passes; Channel++)
    {
        BOOLEAN Fold = Channels == 6 && Channel < 2;
        UINT32 Bin = 0;

        for (UINT32 Frame = 0; Frame < Leq->Half; Frame++)
        {
            const float* Sample = Leq->Older + Frame * Channels + Channel;

            Spectrum[Frame] = Fold ? Sample[4] + Sample[0] : Sample[0];
        }
        for (UINT32 Frame = 0; Frame < Leq->Half; Frame++)
        {
            const float* Sample = Leq->Filling + Frame * Channels + Channel;

            Spectrum[Leq->Half + Frame] = Fold ? Sample[4] + Sample[0] : Sample[0];
        }
        for (UINT32 Index = 0; Index < Leq->Size; Index++)
            Spectrum[Index] = Leq->Window[Index] * Spectrum[Index];
        XaLeqRealTransform(Spectrum, static_cast<INT>(Leq->Size));

        Spectrum[0] = Spectrum[0] * Spectrum[0];
        for (UINT32 Index = 1; Index < Leq->Bins; Index++)
        {
            float Real = Spectrum[2 * Index];
            float Imag = Spectrum[2 * Index + 1];

            Spectrum[Index] = fmaf(Real, Real, Imag * Imag);
        }

        for (UINT32 Band = 0; Band < XA_LEQ_BANDS; Band++)
        {
            float Energy = 0.0f;

            for (; Bin < Leq->BandEnds[Band + 1]; Bin++)
                Energy = Spectrum[Bin] + Energy;
            Bands[Band] = Bands[Band] + Energy;
        }
        for (UINT32 Band = 0; Band < XA_LEQ_BANDS; Band++)
            Bands[Band] = XaLeqQuietLevels[Band] < Bands[Band] ? Bands[Band] - XaLeqQuietLevels[Band] : 0.0f;
    }

    /* Low bands leak upward a little more, then every band masks its two neighbors above */
    for (UINT32 Band = 4; Band > 1; Band -= 1)
    {
        float Half = static_cast<float>(static_cast<double>(Bands[Band - 1]) * 0.5);
        float Quarter = static_cast<float>(static_cast<double>(Bands[Band - 2]) * 0.25);

        Bands[Band] = Quarter + (Half + Bands[Band]);
    }
    for (UINT32 Band = XA_LEQ_BANDS - 1; Band > 1; Band -= 1)
    {
        double Spread = fma(static_cast<double>(Bands[Band - 1]), 0.0316, static_cast<double>(Bands[Band]));

        Bands[Band] = static_cast<float>(fma(static_cast<double>(Bands[Band - 2]), 0.000316, Spread));
    }
    Bands[1] = static_cast<float>(fma(static_cast<double>(Bands[0]), 0.0316, static_cast<double>(Bands[1])));

    for (UINT32 Band = 0; Band < XA_LEQ_BANDS; Band++)
    {
        if (Bands[Band] > 0.0f)
            Sum = static_cast<float>(sqrt(sqrt(static_cast<double>(Bands[Band])))) + Sum;
    }
    Sum = static_cast<float>(static_cast<double>(Sum) * 1.72);
    return powf(static_cast<float>(Leq->Loudness) / (Sum + 10.0f), 1.75f);
}

/* Settles the gain for the window, then fades the older half into it and queues the newer one */
static
VOID
XaLeqBlock(
    _Inout_ XaLeq* Leq)
{
    UINT32 Count = Leq->Half * Leq->Channels;
    float Target = XaLeqLower(powf(XaLeqTargetGain(Leq), 0.5f), 20.0f);
    float Grown = Leq->Recovery * Leq->Gain;
    float Gain;
    float Ceiling = 100.0f;
    float Slope;
    float* Swap;

    if (Grown == 0.0f)
        Grown = 0.01f;
    Gain = XaLeqLower(Grown, Target);
    Leq->Gain = Gain;
    if (Count != 0)
    {
        float Peak = 0.0f;

        for (UINT32 Index = 0; Index < Count; Index++)
            Peak = XaLeqUpper(fabsf(Leq->Older[Index]), Peak);
        for (UINT32 Index = 0; Index < Count; Index++)
            Peak = XaLeqUpper(fabsf(Leq->Filling[Index]), Peak);
        if (!(Peak < 0.01f))
            Ceiling = 1.0f / Peak;
    }
    Gain = XaLeqLower(Gain, Ceiling);
    if (Gain < 0.0f)
        Gain = 0.0f;
    Leq->Gain = Gain;

    Slope = Gain / static_cast<float>(Leq->Half);
    for (UINT32 Frame = 0; Frame < Leq->Half; Frame++)
    {
        float Rise = static_cast<float>(Frame) * Slope;
        float Fall = Gain - Rise;
        UINT32 Base = Frame * Leq->Channels;

        for (UINT32 Channel = 0; Channel < Leq->Channels; Channel++)
        {
            Leq->Queued[Base + Channel] = fmaf(Leq->Older[Base + Channel], Rise, Leq->Queued[Base + Channel]);
            Leq->Playing[Base + Channel] = Leq->Filling[Base + Channel] * Fall;
        }
    }

    /* The queued half plays next and the half just filled becomes the older one */
    Swap = Leq->Playing;
    Leq->Playing = Leq->Queued;
    Leq->Queued = Swap;
    Swap = Leq->Older;
    Leq->Older = Leq->Filling;
    Leq->Filling = Swap;
}

/* Streams frames through with a delay of one transform length */
static
VOID
XaLeqStream(
    _Inout_ XaLeq* Leq,
    _In_reads_(Frames * Leq->Channels) const float* Input,
    _Out_writes_(Frames * Leq->Channels) float* Output,
    _In_ UINT32 Frames)
{
    while (Frames != 0)
    {
        UINT32 Take = XaMin(Frames, Leq->Half - Leq->Filled);
        SIZE_T Offset = static_cast<SIZE_T>(Leq->Filled) * Leq->Channels;
        SIZE_T Bytes = sizeof(*Input) * Take * Leq->Channels;

        memcpy(Output, Leq->Playing + Offset, Bytes);
        memcpy(Leq->Filling + Offset, Input, Bytes);
        Leq->Filled += Take;
        Input += Take * Leq->Channels;
        Output += Take * Leq->Channels;
        Frames -= Take;
        if (Leq->Filled == Leq->Half)
        {
            XaLeqBlock(Leq);
            Leq->Filled = 0;
        }
    }
}

class XaLimiter final : public XaApo
{
public:
    XaLimiter() : XaApo(&XaLimiterRegistration)
    {
        memset(&Leq, 0, sizeof(Leq));
        Memory = nullptr;
        Copy = nullptr;
        Running = FALSE;
    }

    ~XaLimiter() override
    {
        FreeMemory();
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

    void
    STDMETHODCALLTYPE
    Reset() override
    {
        XaLeqClear(&Leq);
        Running = FALSE;
    }

    HRESULT
    STDMETHODCALLTYPE
    LockForProcess(
        UINT32 InputCount,
        _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
        UINT32 OutputCount,
        _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output) override;

    void
    STDMETHODCALLTYPE
    UnlockForProcess() override
    {
        FreeMemory();
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
    FreeMemory(VOID)
    {
        CoTaskMemFree(Memory);
        CoTaskMemFree(Copy);
        Memory = nullptr;
        Copy = nullptr;
    }

    XaLeq Leq;
    float* Memory;
    float* Copy;
    BOOLEAN Running;
};

HRESULT
STDMETHODCALLTYPE
XaLimiter::LockForProcess(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
    UINT32 OutputCount,
    _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output)
{
    HRESULT hr = XaApo::LockForProcess(InputCount, Input, OutputCount, Output);
    SIZE_T Halves;

    if (FAILED(hr))
    {
        UnlockForProcess();
        return hr;
    }
    FreeMemory();
    memset(&Leq, 0, sizeof(Leq));
    Leq.Channels = Input->pFormat->nChannels;
    Leq.Rate = Input->pFormat->nSamplesPerSec;

    /* The window, spectrum and four halves share one block */
    XaLeqMeasure(&Leq);
    Halves = static_cast<SIZE_T>(Leq.Half) * Leq.Channels;
    Memory = static_cast<float*>(CoTaskMemAlloc(sizeof(*Memory) * (2 * static_cast<SIZE_T>(Leq.Size) + 4 * Halves)));
    Copy = Memory != nullptr ? static_cast<float*>(CoTaskMemAlloc(sizeof(*Copy) * Leq.Channels * Input->MaxFrameCount)) : nullptr;
    if (Memory == nullptr || Copy == nullptr)
    {
        UnlockForProcess();
        return E_OUTOFMEMORY;
    }
    Leq.Window = Memory;
    Leq.Spectrum = Memory + Leq.Size;
    Leq.Older = Leq.Spectrum + Leq.Size;
    Leq.Filling = Leq.Older + Halves;
    Leq.Playing = Leq.Filling + Halves;
    Leq.Queued = Leq.Playing + Halves;
    XaLeqSetup(&Leq);
    XaLeqClear(&Leq);

    /* The lock starts from the default settings; parameters already set arrive with the next Process */
    Leq.Release = FXMASTERINGLIMITER_DEFAULT_RELEASE;
    Leq.Loudness = FXMASTERINGLIMITER_DEFAULT_LOUDNESS;
    Leq.Recovery = XaLeqRecovery(&Leq);
    Running = FALSE;
    return S_OK;
}

void
STDMETHODCALLTYPE
XaLimiter::Process(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_PROCESS_BUFFER_PARAMETERS* Input,
    UINT32 OutputCount,
    _Inout_updates_opt_(OutputCount) XAPO_PROCESS_BUFFER_PARAMETERS* Output,
    BOOL Enabled)
{
    const FXMASTERINGLIMITER_PARAMETERS* Changed =
        reinterpret_cast<const FXMASTERINGLIMITER_PARAMETERS*>(Parameters.Take());

    UNREFERENCED_PARAMETER(InputCount);
    UNREFERENCED_PARAMETER(OutputCount);
    if (Changed != nullptr)
    {
        if (Changed->Release != Leq.Release)
        {
            Leq.Release = Changed->Release;
            Leq.Recovery = XaLeqRecovery(&Leq);
        }
        Leq.Loudness = Changed->Loudness;
    }

    /* Input is copied aside first because the output may be the same buffer */
    if (Input->BufferFlags == XAPO_BUFFER_VALID && Enabled)
    {
        memcpy(Copy, Input->pBuffer, sizeof(*Copy) * Leq.Channels * Input->ValidFrameCount);
        XaLeqStream(&Leq, Copy, static_cast<float*>(Output->pBuffer), Input->ValidFrameCount);
        Running = TRUE;
    }
    else if ((Input->BufferFlags == XAPO_BUFFER_SILENT || Input->BufferFlags == XAPO_BUFFER_VALID) && Running)
    {
        Reset();
    }
    Output->ValidFrameCount = Input->ValidFrameCount;
    Output->BufferFlags = Input->BufferFlags;
}

HRESULT
XaCreateLimiterObject(
    _Outptr_ IUnknown** Apo)
{
    XaLimiter* Limiter = new (std::nothrow) XaLimiter();

    if (Limiter == nullptr)
        return E_OUTOFMEMORY;
    if (!Limiter->Parameters.Allocate(sizeof(FXMASTERINGLIMITER_PARAMETERS), &XaLimiterDefaults))
    {
        Limiter->Release();
        return E_OUTOFMEMORY;
    }
    *Apo = static_cast<IXAPO*>(Limiter);
    return S_OK;
}
