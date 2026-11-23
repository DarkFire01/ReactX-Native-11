/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The four band equalizer from the FX library
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xapo.hpp"

#include <math.h>

/* The kernels round every product and sum on its own unless they say otherwise */
#pragma fp_contract(off)

#define XA_EQ_BANDS             4
#define XA_EQ_PI                3.14159274f

static const XAPO_REGISTRATION_PROPERTIES XaEqRegistration =
{
    __uuidof(FXEQ),
    L"EQ",
    L"Copyright (c) Microsoft Corporation",
    1,
    0,
    XA_APO_FLAGS_STRICT | XAPO_FLAG_INPLACE_REQUIRED,
    1,
    1,
    1,
    1,
};

static const FXEQ_PARAMETERS XaEqDefaults =
{
    FXEQ_DEFAULT_FREQUENCY_CENTER_0, FXEQ_DEFAULT_GAIN, FXEQ_DEFAULT_BANDWIDTH,
    FXEQ_DEFAULT_FREQUENCY_CENTER_1, FXEQ_DEFAULT_GAIN, FXEQ_DEFAULT_BANDWIDTH,
    FXEQ_DEFAULT_FREQUENCY_CENTER_2, FXEQ_DEFAULT_GAIN, FXEQ_DEFAULT_BANDWIDTH,
    FXEQ_DEFAULT_FREQUENCY_CENTER_3, FXEQ_DEFAULT_GAIN, FXEQ_DEFAULT_BANDWIDTH,
};

/*
 * One peaking band. Both coefficient sets run oldest tap first, so the last
 * entry multiplies the newest input and the newest output.
 */
struct XaEqBand
{
    float Forward[4];
    float Feedback[4];

    /* Four inputs then four outputs per channel, oldest first */
    float* History;
};

/* Peaking coefficients through the bilinear transform with a prewarped center */
static
VOID
XaEqDesign(
    _Out_ XaEqBand* Band,
    _In_ float Rate,
    _In_ float Center,
    _In_ float Bandwidth,
    _In_ float Gain)
{
    float Warped = (Center * (2.0f * XA_EQ_PI)) / tanf((Center * XA_EQ_PI) / Rate);
    float Width = (Center * (4.0f * XA_EQ_PI)) * Bandwidth;
    float Square = (Center * Center) * 39.4784203f;
    float WarpedSquare = Warped * Warped;
    float Middle = (Square - WarpedSquare) + (Square - WarpedSquare);
    float Boosted = Width * Gain;
    float Scale;

    /* Denominator first, the numerator carries the gain */
    Band->Feedback[0] = 0.0f;
    Band->Feedback[1] = fmaf(-Width, Warped, WarpedSquare) + Square;
    Band->Feedback[2] = Middle;
    Band->Feedback[3] = fmaf(Warped, Warped + Width, Square);
    Band->Forward[0] = 0.0f;
    Band->Forward[1] = fmaf(-Boosted, Warped, WarpedSquare) + Square;
    Band->Forward[2] = Middle;
    Band->Forward[3] = fmaf(Warped, Warped + Boosted, Square);

    /* Normalize to the leading denominator term and move the feedback to the output side */
    Scale = 1.0f / Band->Feedback[3];
    for (UINT32 Tap = 0; Tap < 4; Tap++)
        Band->Forward[Tap] *= Scale;
    {
        float Older = Band->Feedback[1] * Scale;
        float Newer = Band->Feedback[2] * Scale;

        Band->Feedback[0] = -0.0f;
        Band->Feedback[1] = -0.0f;
        Band->Feedback[2] = -Older;
        Band->Feedback[3] = -Newer;
    }
}

/* Runs Frames interleaved frames of Channels through one band */
static
VOID
XaEqRun(
    _Inout_ XaEqBand* Band,
    _In_reads_(Frames * Channels) const float* Input,
    _Out_writes_(Frames * Channels) float* Output,
    _In_ UINT32 Channels,
    _In_ UINT32 Frames)
{
    const float* Forward = Band->Forward;
    const float* Feedback = Band->Feedback;

    for (UINT32 Frame = 0; Frame < Frames; Frame++)
    {
        float* History = Band->History;

        for (UINT32 Channel = 0; Channel < Channels; Channel++)
        {
            float* In = History;
            float* Out = History + 4;
            float FromInput;
            float FromOutput;
            float Result;

            In[0] = In[1];
            In[1] = In[2];
            In[2] = In[3];
            In[3] = *Input++;
            FromOutput = ((Out[3] * Feedback[3] + Out[2] * Feedback[2]) + Out[1] * Feedback[1]) + Out[0] * Feedback[0];
            FromInput = ((In[3] * Forward[3] + In[2] * Forward[2]) + In[1] * Forward[1]) + In[0] * Forward[0];
            Result = FromInput + FromOutput;
            *Output++ = Result;
            Out[0] = Out[1];
            Out[1] = Out[2];
            Out[2] = Out[3];
            Out[3] = Result;
            History += 8;
        }
    }
}

class XaEq final : public XaApo
{
public:
    XaEq() : XaApo(&XaEqRegistration)
    {
        Channels = 0;
        Rate = 0;
        Running = FALSE;
        State = nullptr;
        memset(Bands, 0, sizeof(Bands));
    }

    ~XaEq() override
    {
        delete[] State;
    }

    HRESULT
    ValidateFormat(
        _Inout_ WAVEFORMATEX* Format,
        _In_ BOOLEAN Fix) override
    {
        HRESULT hr = XaApo::ValidateFormat(Format, Fix);

        /* The rate check replaces whatever the base answered */
        if (Format->nSamplesPerSec < FXEQ_MIN_FRAMERATE || Format->nSamplesPerSec > FXEQ_MAX_FRAMERATE)
        {
            hr = XAPO_E_FORMAT_UNSUPPORTED;
            if (Fix)
                Format->nSamplesPerSec = Format->nSamplesPerSec < FXEQ_MIN_FRAMERATE ? FXEQ_MIN_FRAMERATE : FXEQ_MAX_FRAMERATE;
        }
        return hr;
    }

    void
    STDMETHODCALLTYPE
    Reset() override
    {
        if (State != nullptr)
            memset(State, 0, sizeof(*State) * 8 * XA_EQ_BANDS * Channels);
        Running = FALSE;
    }

    HRESULT
    STDMETHODCALLTYPE
    LockForProcess(
        UINT32 InputCount,
        _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
        UINT32 OutputCount,
        _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output) override
    {
        HRESULT hr = XaApo::LockForProcess(InputCount, Input, OutputCount, Output);

        if (SUCCEEDED(hr))
        {
            Channels = Input->pFormat->nChannels;
            Rate = Input->pFormat->nSamplesPerSec;
            delete[] State;
            State = new (std::nothrow) float[8 * XA_EQ_BANDS * Channels]();
            if (State != nullptr)
            {
                /* Locking always starts from the default bands */
                for (UINT32 Index = 0; Index < XA_EQ_BANDS; Index++)
                {
                    Bands[Index].History = State + Index * 8 * Channels;
                    XaEqDesign(&Bands[Index], static_cast<float>(Rate), (&XaEqDefaults.FrequencyCenter0)[Index * 3],
                               FXEQ_DEFAULT_BANDWIDTH, FXEQ_DEFAULT_GAIN);
                }
                Running = FALSE;
                return hr;
            }
            hr = E_OUTOFMEMORY;
        }
        UnlockForProcess();
        return hr;
    }

    void
    STDMETHODCALLTYPE
    UnlockForProcess() override
    {
        delete[] State;
        State = nullptr;
        Running = FALSE;
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

    UINT32 Channels;
    UINT32 Rate;
    BOOLEAN Running;
    float* State;
    XaEqBand Bands[XA_EQ_BANDS];
};

void
STDMETHODCALLTYPE
XaEq::Process(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_PROCESS_BUFFER_PARAMETERS* Input,
    UINT32 OutputCount,
    _Inout_updates_opt_(OutputCount) XAPO_PROCESS_BUFFER_PARAMETERS* Output,
    BOOL Enabled)
{
    const FXEQ_PARAMETERS* Changed = reinterpret_cast<const FXEQ_PARAMETERS*>(Parameters.Take());

    UNREFERENCED_PARAMETER(InputCount);
    UNREFERENCED_PARAMETER(OutputCount);
    if (Changed != nullptr)
    {
        const float* Values = &Changed->FrequencyCenter0;

        for (UINT32 Index = 0; Index < XA_EQ_BANDS; Index++)
            XaEqDesign(&Bands[Index], static_cast<float>(Rate), Values[Index * 3], Values[Index * 3 + 2], Values[Index * 3 + 1]);
    }

    if (Input->BufferFlags == XAPO_BUFFER_VALID && Enabled)
    {
        /* The EQ only runs in place: it reads the output side and writes the input side */
        float* Target = static_cast<float*>(Input->pBuffer);

        XaEqRun(&Bands[0], static_cast<const float*>(Output->pBuffer), Target, Channels, Input->ValidFrameCount);
        for (UINT32 Index = 1; Index < XA_EQ_BANDS; Index++)
            XaEqRun(&Bands[Index], Target, Target, Channels, Input->ValidFrameCount);
        Running = TRUE;
    }
    else if (Input->BufferFlags == XAPO_BUFFER_SILENT || Input->BufferFlags == XAPO_BUFFER_VALID)
    {
        if (Running)
            Reset();
    }
    Output->ValidFrameCount = Input->ValidFrameCount;
    Output->BufferFlags = Input->BufferFlags;
}

HRESULT
XaCreateEqObject(
    _Outptr_ IUnknown** Apo)
{
    XaEq* Eq = new (std::nothrow) XaEq();

    if (Eq == nullptr)
        return E_OUTOFMEMORY;
    if (!Eq->Parameters.Allocate(sizeof(FXEQ_PARAMETERS), &XaEqDefaults))
    {
        Eq->Release();
        return E_OUTOFMEMORY;
    }
    *Apo = static_cast<IXAPO*>(Eq);
    return S_OK;
}
