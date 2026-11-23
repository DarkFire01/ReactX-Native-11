/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The volume meter XAPO
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xapo.hpp"

#include <float.h>
#include <math.h>

static const XAPO_REGISTRATION_PROPERTIES XaMeterRegistration =
{
    __uuidof(AudioVolumeMeter),
    L"VolumeMeter",
    L"Copyright (c) Microsoft Corporation",
    1,
    0,
    XA_APO_FLAGS_STRICT | XAPO_FLAG_INPLACE_REQUIRED,
    1,
    1,
    1,
    1,
};

/* Squared peaks and summed squares for every channel of one block */
struct XaMeterLevels
{
    float* PeakSquares;
    float* SumSquares;
};

class XaMeter final : public XaApo
{
public:
    XaMeter() : XaApo(&XaMeterRegistration)
    {
        InitializeSRWLock(&LevelLock);
        Channels = 0;
        MaxFrames = 0;
        Storage = nullptr;
        Writing = 0;
        Fresh = FALSE;
        memset(Sets, 0, sizeof(Sets));
    }

    ~XaMeter() override
    {
        FreeLevels();
    }

    void
    STDMETHODCALLTYPE
    Reset() override
    {
        if (Storage != nullptr)
            memset(Storage, 0, sizeof(*Storage) * 4 * Channels);
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
        FreeLevels();
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

    /* The meter only reports */
    void
    STDMETHODCALLTYPE
    SetParameters(
        _In_reads_bytes_(Size) const void* Value,
        UINT32 Size) override
    {
        UNREFERENCED_PARAMETER(Value);
        UNREFERENCED_PARAMETER(Size);
    }

    void
    STDMETHODCALLTYPE
    GetParameters(
        _Out_writes_bytes_(Size) void* Value,
        UINT32 Size) override;

    VOID
    FreeLevels(VOID)
    {
        delete[] Storage;
        Storage = nullptr;
        memset(Sets, 0, sizeof(Sets));
        Channels = 0;
        Writing = 0;
        Fresh = FALSE;
    }

    SRWLOCK LevelLock;
    UINT32 Channels;
    UINT32 MaxFrames;
    float* Storage;

    /* Process fills one set while GetParameters reads the other */
    XaMeterLevels Sets[2];
    UINT32 Writing;
    BOOLEAN Fresh;
};

HRESULT
STDMETHODCALLTYPE
XaMeter::LockForProcess(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
    UINT32 OutputCount,
    _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output)
{
    const WAVEFORMATEX* In;
    const WAVEFORMATEX* Out;
    HRESULT hr;

    UNREFERENCED_PARAMETER(InputCount);
    UNREFERENCED_PARAMETER(OutputCount);
    hr = LockCommon(Input, Output);
    if (FAILED(hr))
    {
        UnlockForProcess();
        return hr;
    }
    In = Input->pFormat;
    Out = Output->pFormat;
    if (XaFormatTag(In) != WAVE_FORMAT_IEEE_FLOAT || XaFormatTag(Out) != WAVE_FORMAT_IEEE_FLOAT || In->nChannels == 0 ||
        In->nChannels != Out->nChannels)
    {
        UnlockForProcess();
        return XAPO_E_FORMAT_UNSUPPORTED;
    }

    /* A second lock keeps the levels it already has */
    if (Storage == nullptr)
    {
        if (In->nChannels > XAPO_MAX_CHANNELS)
        {
            UnlockForProcess();
            return XAPO_E_FORMAT_UNSUPPORTED;
        }
        Storage = new (std::nothrow) float[4 * In->nChannels];
        if (Storage == nullptr)
        {
            UnlockForProcess();
            return E_OUTOFMEMORY;
        }
        Channels = In->nChannels;
        memset(Storage, 0, sizeof(*Storage) * 4 * Channels);
        for (UINT32 Index = 0; Index < 2; Index++)
        {
            Sets[Index].PeakSquares = Storage + Index * 2 * Channels;
            Sets[Index].SumSquares = Sets[Index].PeakSquares + Channels;
        }
        Writing = 0;
        Fresh = FALSE;
    }
    MaxFrames = Input->MaxFrameCount;
    return S_OK;
}

void
STDMETHODCALLTYPE
XaMeter::Process(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_PROCESS_BUFFER_PARAMETERS* Input,
    UINT32 OutputCount,
    _Inout_updates_opt_(OutputCount) XAPO_PROCESS_BUFFER_PARAMETERS* Output,
    BOOL Enabled)
{
    XaMeterLevels* Levels;

    UNREFERENCED_PARAMETER(InputCount);
    UNREFERENCED_PARAMETER(OutputCount);
    AcquireSRWLockExclusive(&LevelLock);
    Levels = &Sets[Writing];
    memset(Levels->SumSquares, 0, sizeof(float) * Channels);
    if (Input->BufferFlags == XAPO_BUFFER_SILENT || !Enabled)
    {
        memset(Levels->PeakSquares, 0, sizeof(float) * Channels);
    }
    else
    {
        const float* Samples = static_cast<const float*>(Input->pBuffer);

        for (UINT32 Channel = 0; Channel < Channels; Channel++)
            Levels->PeakSquares[Channel] = -FLT_MAX;
        for (UINT32 Frame = 0; Frame < Input->ValidFrameCount; Frame++)
        {
            for (UINT32 Channel = 0; Channel < Channels; Channel++)
            {
                float Square = Samples[Channel] * Samples[Channel];

                Levels->SumSquares[Channel] += Square;
                if (Square > Levels->PeakSquares[Channel])
                    Levels->PeakSquares[Channel] = Square;
            }
            Samples += Channels;
        }
    }
    Fresh = TRUE;
    ReleaseSRWLockExclusive(&LevelLock);
    Output->ValidFrameCount = Input->ValidFrameCount;
    Output->BufferFlags = Input->BufferFlags;
}

void
STDMETHODCALLTYPE
XaMeter::GetParameters(
    _Out_writes_bytes_(Size) void* Value,
    UINT32 Size)
{
    XAUDIO2FX_VOLUMEMETER_LEVELS* Result = static_cast<XAUDIO2FX_VOLUMEMETER_LEVELS*>(Value);
    const XaMeterLevels* Levels;

    UNREFERENCED_PARAMETER(Size);
    AcquireSRWLockExclusive(&LevelLock);

    /* The block Process finished last becomes the one read, and Process moves to the other set */
    if (Fresh)
    {
        Writing ^= 1;
        Fresh = FALSE;
    }
    Levels = &Sets[Writing ^ 1];
    for (UINT32 Channel = 0; Channel < Result->ChannelCount; Channel++)
    {
        float Square = Channel < Channels ? Levels->PeakSquares[Channel] : 0.0f;

        if (Square == 0.0f)
        {
            if (Result->pPeakLevels != nullptr)
                Result->pPeakLevels[Channel] = 0.0f;
            if (Result->pRMSLevels != nullptr)
                Result->pRMSLevels[Channel] = 0.0f;
            continue;
        }
        if (Result->pPeakLevels != nullptr)
            Result->pPeakLevels[Channel] = sqrtf(Square);
        if (Result->pRMSLevels != nullptr)
            Result->pRMSLevels[Channel] = sqrtf(Levels->SumSquares[Channel] / static_cast<float>(MaxFrames));
    }
    ReleaseSRWLockExclusive(&LevelLock);
}

HRESULT
XaCreateMeterObject(
    _Outptr_ IUnknown** Apo)
{
    XaMeter* Meter;

    if (Apo == nullptr)
        return E_POINTER;
    Meter = new (std::nothrow) XaMeter();
    if (Meter == nullptr)
        return E_OUTOFMEMORY;
    *Apo = static_cast<IXAPO*>(Meter);
    return S_OK;
}
