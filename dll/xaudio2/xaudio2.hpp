/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Engine, voices and the render path shared by the runtime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <objbase.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <xaudio2.h>
#include <xapo.h>

#include <new>

#include "trace.hpp"

/* Creation flags XAudio2Create takes */
#define XA_ENGINE_FLAGS         0x0000AC01
#define XA_ENGINE_NO_SHAPE      0x00000400

/* Creation flags each voice kind takes */
#define XA_SOURCE_FLAGS         0x0000040E
#define XA_SUBMIX_FLAGS         0x00000408
#define XA_MASTER_FLAGS         0x00035400

/* Mastering voice flag set when it follows the default endpoint */
#define XA_MASTER_DEFAULT_DEVICE 0x00020000

/* Mastering voice flag that keeps the voice's channels on the endpoint stream */
#define XA_MASTER_KEEP_CHANNELS 0x00001000

/* Compressed format tags the runtime knows of */
#define XA_TAG_WMA2             0x0161
#define XA_TAG_WMA3             0x0162
#define XA_TAG_XMA              0x0165
#define XA_TAG_XMA2             0x0166

/* Engine versions from this one on raise on a voice destroyed twice */
#define XA_VERSION_STRICT       0x0A000006

/* Operation set that applies a change at once */
#define XA_OPSET_NOW            0

/* How long an idle engine keeps running before XAUDIO2_STOP_ENGINE_WHEN_IDLE stops it, in 100 ns units */
#define XA_IDLE_STOP_DELAY      20000000

#define XA_MAX_CHANNELS         64
#define XA_MIN_RATE             1000
#define XA_MAX_RATE             384000
#define XA_MIN_RATIO            (1.0f / 1024.0f)
#define XA_MAX_RATIO            1024.0f
#define XA_MAX_QUEUED           64
#define XA_MAX_BUFFER_BYTES     0x80000000u
#define XA_MAX_LOOP_COUNT       255

/* Scale from 16 bit samples to float */
#define XA_INT16_SCALE          (1.0f / 32768.0f)

/* Rates below this get the next quantum multiple up rather than down */
#define XA_RATE_ROUND_UP_BELOW  32000

/* What a voice is */
enum XaVoiceKind
{
    XaKindSource,
    XaKindSubmix,
    XaKindMaster,
};

/* Where a source voice is in its start and stop life */
enum XaPlayState
{
    XaPlayRunning,
    XaPlayStopped,
    XaPlayTails,
};

class XaEngine;
class XaVoice;
class XaSourceVoice;
class XaSubmixVoice;
class XaMasterVoice;
struct XaSend;
struct XaEffectChain;
class XaEndpoint;

template <typename T>
constexpr
T
XaMin(
    _In_ T Left,
    _In_ T Right)
{
    return Left < Right ? Left : Right;
}

template <typename T>
constexpr
T
XaMax(
    _In_ T Left,
    _In_ T Right)
{
    return Left > Right ? Left : Right;
}

/* format.cpp */
WORD
XaFormatTag(
    _In_ const WAVEFORMATEX* Format);

DWORD
XaPseudoMask(
    _In_ UINT32 Channels);

DWORD
XaFormatMask(
    _In_ const WAVEFORMATEX* Format);

UINT32
XaMaskChannels(
    _In_ DWORD Mask);

BOOLEAN
XaFormatValid(
    _In_ const WAVEFORMATEX* Format);

UINT32
XaQuantumRate(
    _In_ UINT32 Rate,
    _In_ UINT32 Denominator);

UINT32
XaAdpcmFramesPerBlock(
    _In_ const WAVEFORMATEX* Format);

/* matrix.cpp */
HRESULT
XaDefaultMatrix(
    _In_ UINT32 InputChannels,
    _In_ UINT32 OutputChannels,
    _In_ DWORD InputMask,
    _In_ DWORD OutputMask,
    _Out_writes_(InputChannels * OutputChannels) float* Matrix);

/* Where a parameter change is on its way to the mixer */
enum XaSlotState
{
    XaSlotIdle,
    XaSlotWaiting,
    XaSlotQueued,
    XaSlotReady,
};

/* XAPO style parameters: the API side writes, the mixer takes the newest when it next processes */
struct XaParameterSlot
{
    BYTE* Pending;
    BYTE* Current;
    UINT32 Size;
    UINT32 OperationSet;
    XaSlotState State;
};

BOOLEAN
XaParameterInit(
    _Out_ XaParameterSlot* Slot,
    _In_ UINT32 Size,
    _In_reads_bytes_(Size) const VOID* Initial);

VOID
XaParameterFree(
    _Inout_ XaParameterSlot* Slot);

VOID
XaParameterSet(
    _Inout_ XaParameterSlot* Slot,
    _In_reads_bytes_(Slot->Size) const VOID* Value,
    _In_ UINT32 OperationSet);

VOID
XaParameterCommit(
    _Inout_ XaParameterSlot* Slot,
    _In_ UINT32 OperationSet);

BOOLEAN
XaParameterTake(
    _Inout_ XaParameterSlot* Slot);

VOID
XaParameterPromote(
    _Inout_ XaParameterSlot* Slot);

/* resample.cpp: linear interpolation that carries its position and the last frames between blocks */
struct XaResampler
{
    UINT32 Channels;
    UINT32 InputRate;
    UINT32 OutputRate;
    float BaseStep;
    float Step;
    float Fraction;
    INT64 Position;
    BOOLEAN FixedRatio;
    float* History;
    UINT32 HistoryFrames;
};

XaResampler*
XaResamplerCreate(
    _In_ UINT32 Channels,
    _In_ UINT32 InputRate,
    _In_ UINT32 OutputRate,
    _In_ BOOLEAN FixedRatio);

VOID
XaResamplerFree(
    _In_opt_ XaResampler* Resampler);

VOID
XaResamplerReset(
    _Inout_ XaResampler* Resampler);

VOID
XaResamplerSetRatio(
    _Inout_ XaResampler* Resampler,
    _In_ float Ratio);

UINT32
XaResamplerInputFrames(
    _Inout_ XaResampler* Resampler,
    _In_ UINT32 OutputFrames);

VOID
XaResamplerProcess(
    _Inout_ XaResampler* Resampler,
    _In_reads_(InputFrames * Resampler->Channels) const float* Input,
    _In_ UINT32 InputFrames,
    _In_reads_opt_(InputFrames * Resampler->Channels) const SHORT* Raw,
    _Out_writes_(OutputFrames * Resampler->Channels) float* Output,
    _In_ UINT32 OutputFrames);

/* mixer.cpp */
struct XaScratchBuffer
{
    float* Data;
    SIZE_T Capacity;
};

VOID
XaScratchFree(
    _Inout_ XaScratchBuffer* Scratch);

HANDLE
XaJoinAudioTask(
    _Out_ DWORD* Task);

VOID
XaLeaveAudioTask(
    _In_opt_ HANDLE Mmcss);

/* apo.cpp */
HRESULT
XaCreateReverb(
    _Outptr_ IUnknown** Apo,
    _In_ BOOLEAN Legacy);

HRESULT
XaCreateVolumeMeter(
    _Outptr_ IUnknown** Apo);

HRESULT
XaCreateFx(
    _In_ REFCLSID Clsid,
    _Outptr_ IUnknown** Effect,
    _In_reads_bytes_opt_(InitDataByteSize) const void* InitData,
    _In_ UINT32 InitDataByteSize);

/* effects.cpp */
struct XaEffectChain
{
    UINT32 Count;
    UINT32 OutputChannels;
    DWORD OutputMask;
    UINT32 Rate;
    UINT32 MaxFrames;
    struct XaEffect* Entries;
};

float*
XaEffectsProcess(
    _Inout_ XaEffectChain* Chain,
    _Inout_ float* Buffer,
    _In_ UINT32 Frames,
    _Inout_ BOOLEAN* Silent);

BOOLEAN
XaEffectsValidate(
    _In_ XaEngine* Engine,
    _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain,
    _In_ UINT32 InputChannels);

HRESULT
XaEffectsCreate(
    _In_ XaVoice* Voice,
    _In_ const XAUDIO2_EFFECT_CHAIN* Chain,
    _Outptr_ XaEffectChain** Result);

VOID
XaEffectsFree(
    _In_opt_ XaEffectChain* Chain);

VOID
XaEffectsCommit(
    _Inout_ XaEffectChain* Chain,
    _In_ UINT32 OperationSet);

VOID
XaEffectsApplyReady(
    _Inout_ XaEffectChain* Chain);

HRESULT
XaEffectsEnable(
    _Inout_ XaEffectChain* Chain,
    _In_ UINT32 Index,
    _In_ BOOLEAN Enable,
    _In_ UINT32 OperationSet);

BOOL
XaEffectsEnabled(
    _In_ XaEffectChain* Chain,
    _In_ UINT32 Index);

HRESULT
XaEffectsSetParameters(
    _Inout_ XaEffectChain* Chain,
    _In_ UINT32 Index,
    _In_reads_bytes_(Size) const void* Parameters,
    _In_ UINT32 Size,
    _In_ UINT32 OperationSet);

HRESULT
XaEffectsGetParameters(
    _In_ XaEffectChain* Chain,
    _In_ UINT32 Index,
    _Out_writes_bytes_(Size) void* Parameters,
    _In_ UINT32 Size);

/* One send from a voice to an output voice */
struct XaSend
{
    XaSend* Next;
    XaVoice* Target;
    UINT32 Flags;

    /* The levels the application set, Target channels by source channels */
    float* Levels;

    /* The send filter followed by the levels scaled by both voices' volumes, as the mixer takes them */
    XaParameterSlot Mix;

    /* Mixer side state: the matrix it is ramping from and the send filter memory */
    float* MixState;
    BOOLEAN Primed;
    BOOLEAN Changed;
    float* FilterState;
    float* Filtered;
};

/* filter.cpp */
BOOLEAN
XaFilterSilences(
    _In_ const XAUDIO2_FILTER_PARAMETERS* Parameters);

VOID
XaFilterRun(
    _In_ const XAUDIO2_FILTER_PARAMETERS* Parameters,
    _Inout_updates_(2 * Channels) float* State,
    _In_reads_(Frames * Channels) const float* Input,
    _Out_writes_(Frames * Channels) float* Output,
    _In_ UINT32 Frames,
    _In_ UINT32 Channels);

/* mixmatrix.cpp: one block through a matrix, optionally ramping from Matrix by Delta a frame */
struct XaMixBlock
{
    const float* Input;
    float* Output;
    UINT32 Frames;
    UINT32 InputChannels;
    UINT32 OutputChannels;
    const float* Matrix;
    const float* Delta;
    BOOLEAN Accumulate;
};

VOID
XaMatrixMix(
    _In_ const XaMixBlock* Block);

VOID
XaMixThroughSend(
    _Inout_ XaSend* Send,
    _In_reads_(Frames * InputChannels) const float* Input,
    _In_ UINT32 InputChannels,
    _Out_writes_(Frames * Send->Target->InputChannels) float* Output,
    _In_ UINT32 Frames,
    _In_ UINT32 Rate,
    _In_ BOOLEAN Accumulate);

/* A change waiting for CommitChanges */
enum XaDeferredKind
{
    XaDeferStart,
    XaDeferStop,
    XaDeferStopTails,
};

struct XaDeferred
{
    XaDeferred* Next;
    UINT32 OperationSet;
    XaDeferredKind Kind;
};

/* A queued source buffer, with its regions already in frames */
struct XaQueuedBuffer
{
    XaQueuedBuffer* Next;
    const BYTE* Data;
    UINT32 Bytes;
    UINT32 Flags;
    UINT32 PlayBegin;
    UINT32 PlayEnd;
    UINT32 LoopBegin;
    UINT32 LoopEnd;
    UINT32 LoopCount;
    void* Context;

    /* Where the mixer has got to in this buffer */
    UINT32 Position;
    UINT32 LoopsDone;
    BOOLEAN Started;
};

class XaVoice
{
public:
    XaVoice(
        _In_ XaEngine* NewEngine,
        _In_ XaVoiceKind NewKind);

    virtual ~XaVoice();

    virtual IXAudio2Voice* Face() = 0;

    XaEngine* Engine;
    XaVoiceKind Kind;
    XaVoice* NextVoice;

    UINT32 CreationFlags;
    UINT32 ActiveFlags;
    UINT32 InputChannels;
    UINT32 InputRate;
    DWORD InputMask;

    /* The rate the voice produces, which is the rate of its outputs */
    UINT32 OutputRate;
    UINT32 ProcessingStage;

    float Volume;
    float* ChannelVolumes;

    XaSend* Sends;
    UINT32 SendCount;

    /* Voices that send to this one */
    UINT32 SenderCount;

    XaEffectChain* Effects;
    XaParameterSlot Filter;
    BOOLEAN Destroyed;

    /* Mixing state, owned by the render thread */
    float* InputMix;
    UINT32 InputMixFrames;
    BOOLEAN InputFed;
    XaResampler* Resampler;
    float* FilterState;
    BOOLEAN FilterActive;
    XaScratchBuffer InputScratch;
    XaScratchBuffer OutputScratch;

    UINT32
    OutputChannels(VOID) const;

    float
    SourceLevel(
        _In_ UINT32 Channel) const;

    UINT32
    TargetRate(
        _In_ const XAUDIO2_VOICE_SENDS* SendList);

    HRESULT
    SetOutputRate(
        _In_ UINT32 Rate);

    DWORD
    OutputMask(VOID) const;

    /* IXAudio2Voice */
    VOID
    GetVoiceDetailsCommon(
        _Out_ XAUDIO2_VOICE_DETAILS* Details);

    HRESULT
    SetOutputVoicesCommon(
        _In_opt_ const XAUDIO2_VOICE_SENDS* SendList);

    HRESULT
    SetEffectChainCommon(
        _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain);

    HRESULT
    EnableEffectCommon(
        _In_ UINT32 Index,
        _In_ BOOLEAN Enable,
        _In_ UINT32 OperationSet);

    VOID
    GetEffectStateCommon(
        _In_ UINT32 Index,
        _Out_ BOOL* Enabled);

    HRESULT
    SetEffectParametersCommon(
        _In_ UINT32 Index,
        _In_reads_bytes_(Size) const void* Parameters,
        _In_ UINT32 Size,
        _In_ UINT32 OperationSet);

    HRESULT
    GetEffectParametersCommon(
        _In_ UINT32 Index,
        _Out_writes_bytes_(Size) void* Parameters,
        _In_ UINT32 Size);

    HRESULT
    SetFilterParametersCommon(
        _In_ const XAUDIO2_FILTER_PARAMETERS* Parameters,
        _In_ UINT32 OperationSet);

    VOID
    GetFilterParametersCommon(
        _Out_ XAUDIO2_FILTER_PARAMETERS* Parameters);

    HRESULT
    SetOutputFilterParametersCommon(
        _In_opt_ IXAudio2Voice* Destination,
        _In_ const XAUDIO2_FILTER_PARAMETERS* Parameters,
        _In_ UINT32 OperationSet);

    VOID
    GetOutputFilterParametersCommon(
        _In_opt_ IXAudio2Voice* Destination,
        _Out_ XAUDIO2_FILTER_PARAMETERS* Parameters);

    HRESULT
    SetVolumeCommon(
        _In_ float NewVolume,
        _In_ UINT32 OperationSet);

    VOID
    GetVolumeCommon(
        _Out_ float* Result);

    HRESULT
    SetChannelVolumesCommon(
        _In_ UINT32 Channels,
        _In_reads_(Channels) const float* Volumes,
        _In_ UINT32 OperationSet);

    VOID
    GetChannelVolumesCommon(
        _In_ UINT32 Channels,
        _Out_writes_(Channels) float* Volumes);

    HRESULT
    SetOutputMatrixCommon(
        _In_opt_ IXAudio2Voice* Destination,
        _In_ UINT32 SourceChannels,
        _In_ UINT32 DestinationChannels,
        _In_reads_(SourceChannels * DestinationChannels) const float* Matrix,
        _In_ UINT32 OperationSet);

    VOID
    GetOutputMatrixCommon(
        _In_opt_ IXAudio2Voice* Destination,
        _In_ UINT32 SourceChannels,
        _In_ UINT32 DestinationChannels,
        _Out_writes_(SourceChannels * DestinationChannels) float* Matrix);

    VOID
    DestroyVoiceCommon(VOID);

    /* Shared setup and helpers */
    HRESULT
    InitializeCommon(
        _In_ UINT32 Flags,
        _In_opt_ const XAUDIO2_VOICE_SENDS* SendList,
        _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain);

    BOOLEAN
    ValidateSends(
        _In_ const XAUDIO2_VOICE_SENDS* SendList);

    HRESULT
    BuildSends(
        _In_ const XAUDIO2_VOICE_SENDS* SendList);

    VOID
    ReleaseSends(VOID);

    XaSend*
    FindSend(
        _In_opt_ IXAudio2Voice* Destination);

    VOID
    ApplySendLevels(
        _Inout_ XaSend* Send,
        _In_ UINT32 OperationSet,
        _In_ BOOLEAN Immediate);

    HRESULT
    EnsureInputMix(VOID);

    VOID
    CommitParameters(
        _In_ UINT32 OperationSet);

    virtual
    VOID
    Teardown(VOID);
};

/* The voice faces forward every IXAudio2Voice method to the shared implementation */
#define XA_VOICE_FACE_METHODS \
    void STDMETHODCALLTYPE GetVoiceDetails(_Out_ XAUDIO2_VOICE_DETAILS* Details) override \
    { \
        GetVoiceDetailsCommon(Details); \
    } \
    HRESULT STDMETHODCALLTYPE SetOutputVoices(_In_opt_ const XAUDIO2_VOICE_SENDS* SendList) override \
    { \
        return SetOutputVoicesCommon(SendList); \
    } \
    HRESULT STDMETHODCALLTYPE SetEffectChain(_In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain) override \
    { \
        return SetEffectChainCommon(Chain); \
    } \
    HRESULT STDMETHODCALLTYPE EnableEffect(UINT32 Index, UINT32 OperationSet) override \
    { \
        return EnableEffectCommon(Index, TRUE, OperationSet); \
    } \
    HRESULT STDMETHODCALLTYPE DisableEffect(UINT32 Index, UINT32 OperationSet) override \
    { \
        return EnableEffectCommon(Index, FALSE, OperationSet); \
    } \
    void STDMETHODCALLTYPE GetEffectState(UINT32 Index, _Out_ BOOL* Enabled) override \
    { \
        GetEffectStateCommon(Index, Enabled); \
    } \
    HRESULT STDMETHODCALLTYPE SetEffectParameters(UINT32 Index, _In_reads_bytes_(Size) const void* Parameters, \
                                                  UINT32 Size, UINT32 OperationSet) override \
    { \
        return SetEffectParametersCommon(Index, Parameters, Size, OperationSet); \
    } \
    HRESULT STDMETHODCALLTYPE GetEffectParameters(UINT32 Index, _Out_writes_bytes_(Size) void* Parameters, \
                                                  UINT32 Size) override \
    { \
        return GetEffectParametersCommon(Index, Parameters, Size); \
    } \
    HRESULT STDMETHODCALLTYPE SetFilterParameters(_In_ const XAUDIO2_FILTER_PARAMETERS* Parameters, \
                                                  UINT32 OperationSet) override \
    { \
        return SetFilterParametersCommon(Parameters, OperationSet); \
    } \
    void STDMETHODCALLTYPE GetFilterParameters(_Out_ XAUDIO2_FILTER_PARAMETERS* Parameters) override \
    { \
        GetFilterParametersCommon(Parameters); \
    } \
    HRESULT STDMETHODCALLTYPE SetOutputFilterParameters(_In_opt_ IXAudio2Voice* Destination, \
                                                        _In_ const XAUDIO2_FILTER_PARAMETERS* Parameters, \
                                                        UINT32 OperationSet) override \
    { \
        return SetOutputFilterParametersCommon(Destination, Parameters, OperationSet); \
    } \
    void STDMETHODCALLTYPE GetOutputFilterParameters(_In_opt_ IXAudio2Voice* Destination, \
                                                     _Out_ XAUDIO2_FILTER_PARAMETERS* Parameters) override \
    { \
        GetOutputFilterParametersCommon(Destination, Parameters); \
    } \
    HRESULT STDMETHODCALLTYPE SetChannelVolumes(UINT32 Channels, _In_reads_(Channels) const float* Volumes, \
                                                UINT32 OperationSet) override \
    { \
        return SetChannelVolumesCommon(Channels, Volumes, OperationSet); \
    } \
    void STDMETHODCALLTYPE GetChannelVolumes(UINT32 Channels, _Out_writes_(Channels) float* Volumes) override \
    { \
        GetChannelVolumesCommon(Channels, Volumes); \
    } \
    HRESULT STDMETHODCALLTYPE SetOutputMatrix(_In_opt_ IXAudio2Voice* Destination, UINT32 SourceChannels, \
                                              UINT32 DestinationChannels, \
                                              _In_reads_(SourceChannels * DestinationChannels) const float* Matrix, \
                                              UINT32 OperationSet) override \
    { \
        return SetOutputMatrixCommon(Destination, SourceChannels, DestinationChannels, Matrix, OperationSet); \
    } \
    void STDMETHODCALLTYPE GetOutputMatrix(_In_opt_ IXAudio2Voice* Destination, UINT32 SourceChannels, \
                                           UINT32 DestinationChannels, \
                                           _Out_writes_(SourceChannels * DestinationChannels) float* Matrix) override \
    { \
        GetOutputMatrixCommon(Destination, SourceChannels, DestinationChannels, Matrix); \
    } \
    void STDMETHODCALLTYPE DestroyVoice() override \
    { \
        DestroyVoiceCommon(); \
    } \
    void STDMETHODCALLTYPE GetVolume(_Out_ float* Result) override \
    { \
        GetVolumeCommon(Result); \
    }

class XaSourceVoice final : public IXAudio2SourceVoice, public XaVoice
{
public:
    XaSourceVoice(
        _In_ XaEngine* NewEngine,
        _In_opt_ IXAudio2VoiceCallback* NewCallback);

    ~XaSourceVoice() override;

    IXAudio2Voice*
    Face() override
    {
        return this;
    }

    XA_VOICE_FACE_METHODS

    HRESULT
    STDMETHODCALLTYPE
    SetVolume(
        float NewVolume,
        UINT32 OperationSet) override;

    HRESULT
    STDMETHODCALLTYPE
    Start(
        UINT32 Flags,
        UINT32 OperationSet) override;

    HRESULT
    STDMETHODCALLTYPE
    Stop(
        UINT32 Flags,
        UINT32 OperationSet) override;

    HRESULT
    STDMETHODCALLTYPE
    SubmitSourceBuffer(
        _In_ const XAUDIO2_BUFFER* Buffer,
        _In_opt_ const XAUDIO2_BUFFER_WMA* BufferWma) override;

    HRESULT
    STDMETHODCALLTYPE
    FlushSourceBuffers() override;

    HRESULT
    STDMETHODCALLTYPE
    Discontinuity() override;

    HRESULT
    STDMETHODCALLTYPE
    ExitLoop(
        UINT32 OperationSet) override;

    void
    STDMETHODCALLTYPE
    GetState(
        _Out_ XAUDIO2_VOICE_STATE* State,
        UINT32 Flags) override;

    HRESULT
    STDMETHODCALLTYPE
    SetFrequencyRatio(
        float Ratio,
        UINT32 OperationSet) override;

    void
    STDMETHODCALLTYPE
    GetFrequencyRatio(
        _Out_ float* Ratio) override;

    HRESULT
    STDMETHODCALLTYPE
    SetSourceSampleRate(
        UINT32 NewRate) override;

    HRESULT
    Initialize(
        _In_ UINT32 Flags,
        _In_ const WAVEFORMATEX* Format,
        _In_ float MaxRatio,
        _In_opt_ const XAUDIO2_VOICE_SENDS* SendList,
        _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain);

    BOOLEAN
    ValidateBuffer(
        _In_ const XAUDIO2_BUFFER* Buffer);

    VOID
    Transition(
        _In_ XaPlayState NewState);

    VOID
    CommitDeferred(
        _In_ UINT32 OperationSet);

    VOID
    Teardown() override;

    HRESULT
    AddDeferred(
        _In_ UINT32 OperationSet,
        _In_ XaDeferredKind DeferredKind);

    IXAudio2VoiceCallback* Callback;
    WAVEFORMATEX* Format;
    WORD FormatTag;
    UINT32 BlockAlign;
    UINT32 FramesPerBlock;
    float MaxFrequencyRatio;
    BOOLEAN HasConverter;
    BOOLEAN Passthrough;

    XaPlayState PlayState;
    XaDeferred* DeferredList;
    UINT32 CountedResamplers;
    UINT32 CountedMatrices;

    XaQueuedBuffer* Queue;
    XaQueuedBuffer* QueueTail;
    UINT32 QueuedCount;

    XaParameterSlot Ratio;
    BOOLEAN ExitLoopPending;
    UINT32 ExitLoopSet;
    BOOLEAN FlushPending;

    /* Mixing state, owned by the render thread */
    UINT64 PlayedFrames;
    SHORT* Decoded;
    SHORT* RawFrames;
    SIZE_T RawCapacity;
    const BYTE* DecodedData;
    UINT32 DecodedBlock;
};

class XaSubmixVoice final : public IXAudio2SubmixVoice, public XaVoice
{
public:
    XaSubmixVoice(
        _In_ XaEngine* NewEngine) : XaVoice(NewEngine, XaKindSubmix)
    {
        Counted = FALSE;
        CountedResamplers = 0;
        CountedMatrices = 0;
    }

    IXAudio2Voice*
    Face() override
    {
        return this;
    }

    XA_VOICE_FACE_METHODS

    HRESULT
    STDMETHODCALLTYPE
    SetVolume(
        float NewVolume,
        UINT32 OperationSet) override
    {
        return SetVolumeCommon(NewVolume, OperationSet);
    }

    HRESULT
    Initialize(
        _In_ UINT32 Flags,
        _In_ UINT32 Channels,
        _In_ UINT32 Rate,
        _In_ UINT32 Stage,
        _In_opt_ const XAUDIO2_VOICE_SENDS* SendList,
        _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain);

    VOID
    CountActive(
        _In_ BOOLEAN Active);

    VOID
    Teardown() override;

    BOOLEAN Counted;
    UINT32 CountedResamplers;
    UINT32 CountedMatrices;
};

class XaMasterVoice final : public IXAudio2MasteringVoice, public XaVoice
{
public:
    XaMasterVoice(
        _In_ XaEngine* NewEngine) : XaVoice(NewEngine, XaKindMaster), Endpoint(nullptr), DeviceMatrix(nullptr),
        DeviceRunning(FALSE)
    {
    }

    IXAudio2Voice*
    Face() override
    {
        return this;
    }

    XA_VOICE_FACE_METHODS

    HRESULT
    STDMETHODCALLTYPE
    SetVolume(
        float NewVolume,
        UINT32 OperationSet) override
    {
        return SetVolumeCommon(NewVolume, OperationSet);
    }

    HRESULT
    STDMETHODCALLTYPE
    GetChannelMask(
        _Out_ DWORD* ChannelMask) override;

    HRESULT
    Initialize(
        _In_ UINT32 Flags,
        _In_ UINT32 Channels,
        _In_ UINT32 Rate,
        _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain);

    HRESULT
    OpenEndpoint(
        _In_z_ PCWSTR DeviceId,
        _In_ AUDIO_STREAM_CATEGORY Category);

    VOID
    Teardown() override;

    XaEndpoint* Endpoint;

    /* Maps the voice's channels onto the endpoint's when they differ */
    float* DeviceMatrix;

    /* Whether the device converter holds state from a sounding pass */
    BOOLEAN DeviceRunning;
};

/* The shared mode WASAPI stream behind a mastering voice */
class XaEndpoint
{
public:
    XaEndpoint();
    ~XaEndpoint();

    HRESULT
    Open(
        _In_opt_z_ PCWSTR DeviceId,
        _In_ AUDIO_STREAM_CATEGORY Category,
        _In_ UINT32 Channels,
        _In_ DWORD ChannelMask,
        _In_ UINT32 Rate);

    HRESULT
    Start(VOID);

    VOID
    Stop(VOID);

    VOID
    Close(VOID);

    IMMDevice* Device;
    IAudioClient* Client;
    IAudioRenderClient* Render;
    WAVEFORMATEXTENSIBLE Format;
    UINT32 BufferFrames;
    UINT32 LatencyFrames;

    /* One device period and the periods the stream keeps queued, in frames */
    UINT32 PeriodFrames;
    UINT32 QueueFrames;
    HANDLE Ready;
    BOOLEAN Running;
};

UINT32
XaQuantumFrames(
    _In_ UINT32 Rate,
    _In_ const XaEngine* Engine);

HRESULT
XaDefaultDeviceId(
    _Outptr_ PWSTR* DeviceId);

HRESULT
XaCanonicalDeviceId(
    _In_z_ PCWSTR DeviceId,
    _Outptr_ PWSTR* Canonical);

HRESULT
XaMixerPrepareMaster(
    _Inout_ XaMasterVoice* Master);

VOID
XaMixerReleaseMaster(
    _Inout_ XaMasterVoice* Master);

HRESULT
XaQueryEndpointFormat(
    _In_opt_z_ PCWSTR DeviceId,
    _In_ AUDIO_STREAM_CATEGORY Category,
    _In_ UINT32 Preferred,
    _Out_ UINT32* Channels,
    _Out_ UINT32* Rate,
    _Out_ DWORD* Mask);

class XaEngine final : public IXAudio2
{
public:
    XaEngine();
    ~XaEngine();

    /* The other faces share the engine reference count */
    class ExtensionFace final : public IXAudio2Extension
    {
    public:
        XaEngine* Self;

        HRESULT
        STDMETHODCALLTYPE
        QueryInterface(
            REFIID Iid,
            _COM_Outptr_ void** Object) override;

        ULONG
        STDMETHODCALLTYPE
        AddRef() override;

        ULONG
        STDMETHODCALLTYPE
        Release() override;

        void
        STDMETHODCALLTYPE
        GetProcessingQuantum(
            _Out_ UINT32* Numerator,
            _Out_ UINT32* Denominator) override;

        void
        STDMETHODCALLTYPE
        GetProcessor(
            _Out_ XAUDIO2_PROCESSOR* Processor) override;
    };

    class UnknownFace final : public IUnknown
    {
    public:
        XaEngine* Self;

        HRESULT
        STDMETHODCALLTYPE
        QueryInterface(
            REFIID Iid,
            _COM_Outptr_ void** Object) override;

        ULONG
        STDMETHODCALLTYPE
        AddRef() override;

        ULONG
        STDMETHODCALLTYPE
        Release() override;
    };

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        REFIID Iid,
        _COM_Outptr_ void** Object) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef() override;

    ULONG
    STDMETHODCALLTYPE
    Release() override;

    HRESULT
    STDMETHODCALLTYPE
    RegisterForCallbacks(
        _In_ IXAudio2EngineCallback* Callback) override;

    void
    STDMETHODCALLTYPE
    UnregisterForCallbacks(
        _In_ IXAudio2EngineCallback* Callback) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateSourceVoice(
        _Outptr_ IXAudio2SourceVoice** Voice,
        _In_ const WAVEFORMATEX* Format,
        UINT32 Flags,
        float MaxRatio,
        _In_opt_ IXAudio2VoiceCallback* Callback,
        _In_opt_ const XAUDIO2_VOICE_SENDS* SendList,
        _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateSubmixVoice(
        _Outptr_ IXAudio2SubmixVoice** Voice,
        UINT32 Channels,
        UINT32 Rate,
        UINT32 Flags,
        UINT32 Stage,
        _In_opt_ const XAUDIO2_VOICE_SENDS* SendList,
        _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateMasteringVoice(
        _Outptr_ IXAudio2MasteringVoice** Voice,
        UINT32 Channels,
        UINT32 Rate,
        UINT32 Flags,
        _In_opt_ LPCWSTR DeviceId,
        _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain,
        AUDIO_STREAM_CATEGORY Category) override;

    HRESULT
    STDMETHODCALLTYPE
    StartEngine() override;

    void
    STDMETHODCALLTYPE
    StopEngine() override;

    HRESULT
    STDMETHODCALLTYPE
    CommitChanges(
        UINT32 OperationSet) override;

    void
    STDMETHODCALLTYPE
    GetPerformanceData(
        _Out_ XAUDIO2_PERFORMANCE_DATA* Data) override;

    void
    STDMETHODCALLTYPE
    SetDebugConfiguration(
        _In_opt_ const XAUDIO2_DEBUG_CONFIGURATION* Configuration,
        _Reserved_ void* Reserved) override;

    HRESULT
    Initialize(
        _In_ UINT32 Flags,
        _In_ XAUDIO2_PROCESSOR Processor,
        _In_ DWORD Version);

    VOID
    Lock(VOID)
    {
        EnterCriticalSection(&ApiLock);
    }

    VOID
    Unlock(VOID)
    {
        LeaveCriticalSection(&ApiLock);
    }

    BOOLEAN
    InCallback(VOID) const
    {
        return RenderThreadId != 0 && GetCurrentThreadId() == RenderThreadId;
    }

    /* Immediate changes skip the pass queue when no pass can run or the caller is the pass */
    BOOLEAN
    RunsChangesNow(VOID) const
    {
        return Thread == nullptr || InCallback();
    }

    VOID
    ApplyReadyEffects(VOID);

    XaVoice*
    FindVoice(
        _In_opt_ IXAudio2Voice* Face);

    VOID
    LinkVoice(
        _In_ XaVoice* Voice);

    VOID
    UnlinkVoice(
        _In_ XaVoice* Voice);

    HRESULT
    StartRendering(VOID);

    VOID
    StopRendering(VOID);

    VOID
    RunPass(
        _Out_writes_(DeviceFrames * Master->Endpoint->Format.Format.nChannels) float* Device,
        _In_ UINT32 DeviceFrames);

    VOID
    RenderAvailable(VOID);

    static
    DWORD
    WINAPI
    RenderThread(
        _In_ LPVOID Parameter);

    /* Wakes the idle watcher when the active source voice count leaves or reaches zero */
    VOID
    NoteActiveChange(
        _In_ UINT32 Before);

    /* A new mastering voice also has the watcher look at the count, so an unused engine stops too */
    VOID
    WakeIdleWatch(VOID)
    {
        if (IdleThread != nullptr)
            SetEvent(IdleSignal);
    }

    static
    DWORD
    WINAPI
    IdleWatch(
        _In_ LPVOID Parameter);

    ExtensionFace Extension;
    UnknownFace Unknown;
    LONG References;

    CRITICAL_SECTION ApiLock;
    UINT32 CreateFlags;
    XAUDIO2_PROCESSOR Processor;
    DWORD Version;
    UINT32 QuantumNumerator;
    UINT32 QuantumDenominator;
    BOOLEAN Initialized;
    BOOLEAN Started;
    XAUDIO2_DEBUG_CONFIGURATION DebugConfiguration;

    XaVoice* Voices;
    XaMasterVoice* Master;
    UINT32 SourceCount;

    /* Engine callbacks, kept in registration order */
    IXAudio2EngineCallback** Callbacks;
    UINT32 CallbackCount;
    UINT32 CallbackCapacity;

    /* Counters GetPerformanceData reports */
    UINT32 ActiveSources;
    UINT32 ActiveSubmixes;
    UINT32 ActiveResamplers;
    UINT32 ActiveMatrices;
    UINT32 Glitches;

    HANDLE Thread;
    HANDLE ThreadStop;
    HANDLE RenderReady;
    DWORD RenderThreadId;

    /* XAUDIO2_STOP_ENGINE_WHEN_IDLE: a watcher thread starts and stops the engine with source voice activity */
    HANDLE IdleThread;
    HANDLE IdleQuit;
    HANDLE IdleSignal;
    HANDLE IdleTimer;
};
