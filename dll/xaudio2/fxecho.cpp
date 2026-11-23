/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The feedback echo from the FX library
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xapo.hpp"

#include <math.h>

#pragma fp_contract(off)

/* Delay positions count frames in 20.12 fixed point */
#define XA_ECHO_FRACTION        12
#define XA_ECHO_ONE             (1ll << XA_ECHO_FRACTION)

/* The delay a lock starts with never goes past this many milliseconds */
#define XA_ECHO_START_CAP       500.0f

/* Room past the longest delay, in frames */
#define XA_ECHO_SPARE_FRAMES    16.0f

static const XAPO_REGISTRATION_PROPERTIES XaEchoRegistration =
{
    __uuidof(FXEcho),
    L"Echo",
    L"Copyright (c) Microsoft Corporation",
    1,
    0,
    XA_APO_FLAGS_STRICT | XAPO_FLAG_INPLACE_REQUIRED,
    1,
    1,
    1,
    1,
};

class XaEcho final : public XaApo
{
public:
    XaEcho() : XaApo(&XaEchoRegistration)
    {
        MaxDelay = FXECHO_DEFAULT_DELAY;
        Line = nullptr;
        Carry = nullptr;
        ClearState();
    }

    ~XaEcho() override
    {
        UnlockForProcess();
    }

    /* Only the longest delay comes from here; the size is not looked at */
    HRESULT
    STDMETHODCALLTYPE
    Initialize(
        _In_reads_bytes_opt_(Size) const void* Data,
        UINT32 Size) override
    {
        UNREFERENCED_PARAMETER(Size);
        MaxDelay = static_cast<const FXECHO_INITDATA*>(Data)->MaxDelay;
        return S_OK;
    }

    void
    STDMETHODCALLTYPE
    Reset() override
    {
        memset(Line, 0, sizeof(*Line) * Channels * Length);
        memset(Carry, 0, sizeof(*Carry) * Channels);
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
        CoTaskMemFree(Line);
        CoTaskMemFree(Carry);
        Line = nullptr;
        Carry = nullptr;
        ClearState();
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
        Delay = 0;
        Length = 0;
        Position = 0;
        FeedbackHalf = 0.0f;
        Blend = 0.0f;
        Channels = 0;
        Rate = 0;
        WasEnabled = FALSE;
    }

    /* Frame index of a fixed point position folded into the line */
    UINT64
    Wrap(
        _In_ INT64 Point) const
    {
        UINT64 Frame;

        while (Point < 0)
            Point += static_cast<INT64>(Length) << XA_ECHO_FRACTION;
        Frame = static_cast<UINT64>(Point) >> XA_ECHO_FRACTION;
        while (Frame >= Length)
            Frame -= Length;
        return Frame;
    }

    VOID
    Advance(VOID)
    {
        INT64 Next = Position;

        if (Next == 0)
            Next = static_cast<INT64>(Length) << XA_ECHO_FRACTION;
        if (Next < 0)
            Next += static_cast<INT64>(Length) << XA_ECHO_FRACTION;
        Position = Next - XA_ECHO_ONE;
    }

    VOID
    Run(
        _In_opt_ const float* In,
        _Out_writes_(Frames * Channels) float* Out,
        _In_ UINT32 Frames,
        _In_ float Wet,
        _In_ float Ramp);

    float MaxDelay;
    float* Line;
    float* Carry;
    UINT64 Delay;
    UINT64 Length;
    INT64 Position;
    float FeedbackHalf;
    float Blend;
    UINT64 Channels;
    UINT64 Rate;
    BOOLEAN WasEnabled;
};

HRESULT
STDMETHODCALLTYPE
XaEcho::LockForProcess(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
    UINT32 OutputCount,
    _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output)
{
    FXECHO_PARAMETERS Start;
    float StartDelay;
    float RateValue;
    HRESULT hr;

    hr = XaApo::LockForProcess(InputCount, Input, OutputCount, Output);
    if (FAILED(hr))
    {
        UnlockForProcess();
        return hr;
    }
    StartDelay = XA_ECHO_START_CAP < MaxDelay ? XA_ECHO_START_CAP : MaxDelay;
    Channels = Input->pFormat->nChannels;
    Rate = Input->pFormat->nSamplesPerSec;
    RateValue = static_cast<float>(Rate);
    Length = XaFloatToCount((MaxDelay * RateValue) / 1000.0f + XA_ECHO_SPARE_FRAMES);
    Delay = XaFloatToCount(((StartDelay * 4096.0f) / 1000.0f) * RateValue);
    FeedbackHalf = 0.25f;
    Blend = sqrtf(0.75f);
    Line = static_cast<float*>(CoTaskMemAlloc(sizeof(*Line) * Channels * Length));
    Carry = Line != nullptr ? static_cast<float*>(CoTaskMemAlloc(sizeof(*Carry) * Channels)) : nullptr;
    if (Line == nullptr || Carry == nullptr)
    {
        UnlockForProcess();
        return E_OUTOFMEMORY;
    }
    Reset();

    /* Every lock starts from the default mix, whatever was set before */
    Start.WetDryMix = FXECHO_DEFAULT_WETDRYMIX;
    Start.Feedback = FXECHO_DEFAULT_FEEDBACK;
    Start.Delay = StartDelay;
    Parameters.Overwrite(&Start);
    return S_OK;
}

VOID
XaEcho::Run(
    _In_opt_ const float* In,
    _Out_writes_(Frames * Channels) float* Out,
    _In_ UINT32 Frames,
    _In_ float Wet,
    _In_ float Ramp)
{
    float Gain = Ramp > 0.0f ? 0.0f : Wet;

    for (UINT32 Frame = 0; Frame < Frames; Frame++)
    {
        float* Read = Line + Channels * Wrap(Position + static_cast<INT64>(Delay));
        float* Write = Line + Channels * Wrap(Position);

        for (UINT64 Channel = 0; Channel < Channels; Channel++)
        {
            float Delayed = Read[Channel];
            float Echoed = Carry[Channel] + FeedbackHalf * Delayed;

            if (In != nullptr)
            {
                float Dry = In[Channel];

                Write[Channel] = Echoed + Blend * Dry;
                Carry[Channel] = FeedbackHalf * Delayed;
                Out[Channel] = (Delayed - Dry) * Gain + Dry;
            }
            else
            {
                Write[Channel] = Echoed;
                Carry[Channel] = FeedbackHalf * Delayed;
                Out[Channel] = Delayed * Gain;
            }
        }
        if (In != nullptr)
            In += Channels;
        Out += Channels;
        Gain = Gain + Ramp;
        Advance();
    }
}

void
STDMETHODCALLTYPE
XaEcho::Process(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_PROCESS_BUFFER_PARAMETERS* Input,
    UINT32 OutputCount,
    _Inout_updates_opt_(OutputCount) XAPO_PROCESS_BUFFER_PARAMETERS* Output,
    BOOL Enabled)
{
    const FXECHO_PARAMETERS* Values;
    float Frames;
    BOOLEAN Silent;

    UNREFERENCED_PARAMETER(InputCount);
    UNREFERENCED_PARAMETER(OutputCount);
    if (Parameters.Take() != nullptr)
    {
        Values = reinterpret_cast<const FXECHO_PARAMETERS*>(Parameters.Active());
        Delay = XaFloatToCount(((Values->Delay * 4096.0f) / 1000.0f) * static_cast<float>(Rate));
        FeedbackHalf = Values->Feedback * 0.5f;
        Blend = sqrtf(1.0f - Values->Feedback * Values->Feedback);
    }
    Values = reinterpret_cast<const FXECHO_PARAMETERS*>(Parameters.Active());
    Frames = static_cast<float>(Input->ValidFrameCount);

    /* Turning on fades the echo in over the block, turning off fades it out and empties the line */
    if (Input->BufferFlags == XAPO_BUFFER_SILENT || Input->BufferFlags == XAPO_BUFFER_VALID)
    {
        Silent = Input->BufferFlags == XAPO_BUFFER_SILENT;
        const float* Dry = Silent ? nullptr : static_cast<const float*>(Input->pBuffer);
        float* Wet = static_cast<float*>(Output->pBuffer);

        if (Enabled)
        {
            Run(Dry, Wet, Input->ValidFrameCount, Values->WetDryMix, WasEnabled ? 0.0f : Values->WetDryMix / Frames);
            if (Silent)
                Output->BufferFlags = XAPO_BUFFER_VALID;
        }
        else if (WasEnabled)
        {
            Run(Dry, Wet, Input->ValidFrameCount, Values->WetDryMix, -Values->WetDryMix / Frames);
            memset(Line, 0, sizeof(*Line) * Channels * Length);
            memset(Carry, 0, sizeof(*Carry) * Channels);
            if (Silent)
                Output->BufferFlags = XAPO_BUFFER_VALID;
        }
    }
    WasEnabled = Enabled != FALSE;
}

HRESULT
XaCreateEchoObject(
    _Outptr_ IUnknown** Apo,
    _In_opt_ const VOID* InitData,
    _In_ UINT32 InitSize)
{
    static const FXECHO_PARAMETERS Empty = {};
    XaEcho* Echo = new (std::nothrow) XaEcho();
    HRESULT hr = S_OK;

    if (Echo == nullptr)
        return E_OUTOFMEMORY;
    if (!Echo->Parameters.Allocate(sizeof(FXECHO_PARAMETERS), &Empty))
    {
        Echo->Release();
        return E_OUTOFMEMORY;
    }

    /* The object comes back even when the creation data is refused */
    if (InitData != nullptr)
        hr = Echo->Initialize(InitData, InitSize);
    *Apo = static_cast<IXAPO*>(Echo);
    return hr;
}
