/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Objects, interfaces and the render engine shared by the runtime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <mmreg.h>
#include <dsound.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>

#include <new>

#include "trace.hpp"

/* Interface versions the runtime answers as */
#define DS_VERSION_7        0x0700
#define DS_VERSION_8        0x0800

/* Cooperative levels a DirectSound object can hold */
#define DS_LEVEL_NONE       0

/* Buffer flags a secondary buffer may carry, with and without mixing into another buffer */
#define DS_SECONDARY_FLAGS  0x000FE3FF
#define DS_MIXIN_FLAGS      0x0002E2DC
#define DS_PRIMARY_DENIED   0x0000A322
#define DS_BUFFER_MIXIN     0x00002000
#define DS_BUFFER_SINKIN    0x00001000

/* The cursor lead the engine keeps, in milliseconds */
#define DS_WRITE_LEAD_MS    30

/* Effects need this much audio in the buffer, in milliseconds */
#define DS_FX_MINIMUM_MS    150

/* Mix quanta per second, the engine period */
#define DS_QUANTA_PER_SECOND 100

/* Voice play state bits */
#define DS_VOICE_PLAYING    0x1
#define DS_VOICE_LOOPING    0x2

/* What the mixer has done with a voice since the last cursor read */
#define DS_PASS_SEEN        0
#define DS_PASS_FINISHED    1
#define DS_PASS_MOVED       2

/* The largest channel count a stream or the final mix may carry */
#define DS_MAX_CHANNELS     18

/* Coefficient ramp after a matrix change, in milliseconds */
#define DS_RAMP_MS          4

template <typename T>
constexpr
T
DsMin(
    _In_ T Left,
    _In_ T Right)
{
    return Left < Right ? Left : Right;
}

template <typename T>
constexpr
T
DsMax(
    _In_ T Left,
    _In_ T Right)
{
    return Left > Right ? Left : Right;
}

/* The send effect DirectMusic buffers use */
static const GUID DsSendEffect = { 0xEF602176, 0xBCBB, 0x49E0, { 0x8C, 0xCA, 0xE0, 0x9A, 0x5A, 0x15, 0x2B, 0x33 } };

class DsObject;
class DsSound;
class DsBuffer;
class DsSecondary;
class DsPrimary;
class DsDevice;
class DsCapture;
class DsCaptureBuffer;
struct DsEffectChain;

/* Per interface reference count, kept apart from the object count */
struct DsFaceLink
{
    DsObject* Object;
    LONG Count;
};

struct DsInterfaceEntry
{
    IID Iid;
    IUnknown* Pointer;
    DsFaceLink* Link;
};

#define DS_MAX_INTERFACES   8

HRESULT
DsFaceQuery(
    _In_ DsFaceLink* Link,
    _In_ REFIID Iid,
    _COM_Outptr_ void** Object);

ULONG
DsFaceAddRef(
    _In_ DsFaceLink* Link);

ULONG
DsFaceRelease(
    _In_ DsFaceLink* Link);

#define DS_FACE_IUNKNOWN \
    HRESULT \
    STDMETHODCALLTYPE \
    QueryInterface( \
        _In_ REFIID Iid, \
        _COM_Outptr_ void** Object) override \
    { \
        return DsFaceQuery(&Link, Iid, Object); \
    } \
    ULONG STDMETHODCALLTYPE AddRef(VOID) override { return DsFaceAddRef(&Link); } \
    ULONG STDMETHODCALLTYPE Release(VOID) override { return DsFaceRelease(&Link); }

/* The IUnknown every object exposes */
class DsUnknownFace final : public IUnknown
{
public:
    DsFaceLink Link;

    DS_FACE_IUNKNOWN
};

/* An object with interfaces registered against it, each counted on its own */
class DsObject
{
public:
    DsObject();
    virtual ~DsObject();

    virtual
    ULONG
    Grab(VOID);

    virtual
    ULONG
    Drop(VOID);

    HRESULT
    Query(
        _In_ REFIID Iid,
        _In_ BOOLEAN Creator,
        _COM_Outptr_ void** Object);

    VOID
    Expose(
        _In_ REFIID Iid,
        _In_ IUnknown* Pointer,
        _In_ DsFaceLink* Link);

    VOID
    Hide(
        _In_ REFIID Iid);

    LONG References;

private:
    DsInterfaceEntry Entries[DS_MAX_INTERFACES];
    UINT EntryCount;
    DsUnknownFace UnknownFace;
};

/* main.cpp */

extern CRITICAL_SECTION DsApiLock;
extern HMODULE DsModule;

class DsApiGuard
{
public:
    DsApiGuard() { EnterCriticalSection(&DsApiLock); }
    ~DsApiGuard() { LeaveCriticalSection(&DsApiLock); }
};

/* format.cpp */

WORD
DsFormatTag(
    _In_ const WAVEFORMATEX* Format);

BOOLEAN
DsIsValidFormat(
    _In_ const WAVEFORMATEX* Format);

BOOLEAN
DsIsValidPcmFormat(
    _In_ const WAVEFORMATEX* Format);

HRESULT
DsCheckEngineFormat(
    _Inout_ WAVEFORMATEX* Format);

ULONG
DsFormatSize(
    _In_ const WAVEFORMATEX* Format);

WAVEFORMATEX*
DsCopyFormat(
    _In_ const WAVEFORMATEX* Format);

BOOLEAN
DsSameFormat(
    _In_ const WAVEFORMATEX* Left,
    _In_ const WAVEFORMATEX* Right);

HRESULT
DsCopyFormatOut(
    _In_ const WAVEFORMATEX* Format,
    _Out_writes_bytes_opt_(*Size) WAVEFORMATEX* Out,
    _Inout_ DWORD* Size);

DWORD
DsChannelMask(
    _In_ const WAVEFORMATEX* Format);

ULONG
DsAlignSize(
    _In_ const WAVEFORMATEX* Format,
    _In_ ULONG Size);

VOID
DsFillSilence(
    _Out_writes_bytes_(Size) BYTE* Data,
    _In_ ULONG Size,
    _In_ WORD Bits);

HRESULT
DsBuildBufferDesc(
    _In_ const DSBUFFERDESC* Desc,
    _Out_ DSBUFFERDESC* Valid,
    _In_ ULONG Version);

HRESULT
DsBuildCaptureDesc(
    _In_ const DSCBUFFERDESC* Desc,
    _Out_ DSCBUFFERDESC* Valid,
    _In_ ULONG Version);

BOOLEAN
DsIsValidEffectDesc(
    _In_ const DSEFFECTDESC* Effect);

/* The attenuation a volume and pan resolve to */
struct DsAttenuation
{
    ULONG LeftAmplitude;
    ULONG RightAmplitude;
    LONG Volume;
    ULONG VolumeAmplitude;
    LONG Pan;
    ULONG PanLeftAmplitude;
    ULONG PanRightAmplitude;
};

ULONG
DsDecibelsToAmplitude(
    _In_ LONG Hundredths);

LONG
DsAmplitudeToDecibels(
    _In_ ULONG Amplitude);

VOID
DsResolveVolumePan(
    _In_ LONG Volume,
    _In_ LONG Pan,
    _Out_ DsAttenuation* Attenuation);

/* The float factor one unit of amplitude stands for */
extern const float DsAmplitudeScale;

/* matrix.cpp */

HRESULT
DsBuildChannelMatrix(
    _In_ UINT InputChannels,
    _In_ UINT OutputChannels,
    _In_ DWORD InputMask,
    _In_ DWORD OutputMask,
    _Out_writes_(InputChannels * OutputChannels) float* Matrix);

/* An event to set when the play cursor passes an offset */
struct DsNotifyPoint
{
    DWORD Offset;
    HANDLE Event;
};

/* What the cubic converter reads: float samples, raw 16 bit values or other integer widths */
enum
{
    DS_CUBIC_FLOAT,
    DS_CUBIC_WHOLE,
    DS_CUBIC_INTEGER
};

/* Kaiser windowed sinc rate converter, polyphase when the rate pair allows and table driven otherwise */
class DsResampler
{
public:
    DsResampler();
    ~DsResampler();

        HRESULT
    Configure(
        _In_ ULONG InRate,
        _In_ ULONG OutRate,
        _In_ UINT Channels);

    /* Four point cubic converter the system falls back to once a buffer changes rate */
    HRESULT
    ConfigureCubic(
        _In_ ULONG InRate,
        _In_ ULONG OutRate,
        _In_ UINT Channels,
        _In_ UCHAR Kind);

    VOID
    Retune(
        _In_ ULONG InRate,
        _In_ ULONG OutRate);

    /* Changes the input rate in place, as a small rate change during play does */
    HRESULT
    Rerate(
        _In_ float In);

    VOID
    Release(VOID);

    VOID
    Reset(VOID);

    BOOLEAN
    WantsInput(VOID) const
    {
        return Filled <= Newest();
    }

    VOID
    Push(
        _In_reads_(Channels) const float* Frame);

    VOID
    Pull(
        _Out_writes_(Channels) float* Frame);

    /* Input frames an engine pass of this many outputs takes */
    ULONG
    BeginPass(
        _In_ ULONG Outputs);

    /* The pass has not yet taken all the input it is due */
    BOOLEAN
    Lacking(VOID) const
    {
        return Filled < PassEnd;
    }

    BOOLEAN Active;
    BOOLEAN Cubic;

private:
    VOID
    PullCubic(
        _Out_writes_(Channels) float* Frame);

    float
    Convolve(
        _In_reads_(Count) const float* Row,
        _In_ ULONG Count,
        _In_ const float* Line,
        _In_ ULONG Slot) const;

    HRESULT
    Design(
        _In_ float In,
        _In_ float Out,
        _Out_ ULONG* Needed);

    LONGLONG
    Taken(
        _In_ LONGLONG Output) const;

    LONGLONG
    Newest(VOID) const;

    ULONG
    Slot(
        _In_ LONGLONG Frame) const;

    BOOLEAN Polyphase;
    ULONG Up;
    ULONG Down;
    ULONG Half;
    ULONG Taps;
    ULONG Capacity;
    UINT Channels;
    float* Coefficients;
    float* Ring;
    float SourceRate;
    float TargetRate;
    LONGLONG Position;
    LONGLONG Filled;

    /* Filter frame numbers run ahead of ring frame numbers by this much after rate changes */
    LONGLONG Shift;

    /* Time of the output clock origin after rate changes, as input frames and filter units */
    double TimeOffset;
    double InOffset;
    double FilterOffset;
    double Stretch;

    /* Interpolated table path: input frames per output, filter units per frame, reach and gain */
    double Step;
    double InStep;
    double OutStep;
    double Reach;
    LONG Stride;
    float Gain;
    LONGLONG Made;

    /* Cubic path: whole input frames and the fraction each output moves, and where between frames it sits */
    UCHAR Source;
    float Pace;
    ULONG Leap;
    float Creep;

    /* Frame numbers the current pass's input starts and ends at */
    LONGLONG PassStart;
    LONGLONG PassEnd;
    float Fraction;
};

/* The mixer side of a playing buffer */
class DsVoice
{
public:
    DsVoice();
    ~DsVoice();

    HRESULT
    Setup(
        _In_ DsDevice* Device,
        _In_ const WAVEFORMATEX* Format,
        _In_ BYTE* Data,
        _In_ ULONG Size);

    VOID
    GetCursors(
        _Out_opt_ DWORD* Play,
        _Out_opt_ DWORD* Write);

    HRESULT
    SetState(
        _In_ ULONG State);

    ULONG
    GetState(VOID);

    VOID
    SetPosition(
        _In_ DWORD Position);

    VOID
    SetFrequency(
        _In_ DWORD Frequency);

    VOID
    StartCubic(VOID);

    VOID
    SetAttenuation(
        _In_ const DsAttenuation* Level);

    /* Builds the matrix again from the last attenuation, after the final mix layout changed */
    VOID
    Rebuild(VOID);

    VOID
    SetMute(
        _In_ BOOLEAN Mute);

    VOID
    SetFocusMute(
        _In_ BOOLEAN Mute);

    VOID
    SetSpeakerVolumes(
        _In_ LONG Volume,
        _In_ UINT Count,
        _In_reads_(Count) const LONG* Levels);

    VOID
    CommitMatrix(
        _In_reads_(DS_MAX_CHANNELS * DS_MAX_CHANNELS) const float* Built);

    HRESULT
    SetNotifications(
        _In_ ULONG Count,
        _In_reads_opt_(Count) const DsNotifyPoint* Points);

    VOID
    Mix(
        _Inout_updates_(Frames * OutChannels) float* Output,
        _In_ ULONG Frames,
        _In_ UINT OutChannels);

    VOID
    PassStart(VOID);

    VOID
    PassEnd(VOID);

    DsDevice* Device;
    DsVoice* Next;
    WAVEFORMATEX* Format;
    BYTE* Data;
    ULONG Size;
    ULONG BlockAlign;
    UINT Channels;
    ULONG Lead;

    /* Guarded by the device mix lock */
    ULONG State;
    ULONG StopPosition;
    ULONG LastPlay;
    ULONG ReadPosition;
    LONG Pass;
    BOOLEAN Finished;
    BOOLEAN Muted;
    BOOLEAN FocusMuted;
    DsNotifyPoint* Points;
    ULONG PointCount;
    ULONG NotifyPlay;
    ULONG NotifySeen;
    float Ratio;
    float Matrix[DS_MAX_CHANNELS * DS_MAX_CHANNELS];
    float Previous[DS_MAX_CHANNELS * DS_MAX_CHANNELS];
    float Mixed[DS_MAX_CHANNELS * DS_MAX_CHANNELS];
    BOOLEAN MatrixReady;
    BOOLEAN Secondary;
    BOOLEAN HasAttenuation;
    DsAttenuation Attenuation;
    ULONG RampLeft;
    ULONG RampTotal;
    DsResampler Resampler;
    DsEffectChain* Effects;

private:
    ULONG
    PlayFromRead(
        _In_ ULONG Read,
        _In_ ULONG Previous);

    BOOLEAN
    ReadFrame(
        _Out_writes_(Channels) float* Frame);

    VOID
    ApplyMatrix(
        _In_reads_(Channels) const float* Frame,
        _Inout_updates_(OutChannels) float* Output,
        _In_ UINT OutChannels);
};

/* The endpoint a DirectSound object renders to and the thread that feeds it */
class DsDevice
{
public:
    DsDevice();
    ~DsDevice();

    HRESULT
    Open(
        _In_opt_ LPCGUID Guid);

    HRESULT
    StartStreaming(VOID);

    VOID
    AddVoice(
        _In_ DsVoice* Voice);

    VOID
    RemoveVoice(
        _In_ DsVoice* Voice);

    VOID
    UseCubicRates(VOID);

    /* Settles the final mix layout for a new secondary buffer before its voice is set up */
    VOID
    PlaceBuffer(
        _In_ const WAVEFORMATEX* Format,
        _In_ BOOLEAN Fixed);

    VOID
    Kick(VOID);

    HRESULT
    GetMasterVolume(
        _Out_ LONG* Volume);

    HRESULT
    SetMasterVolume(
        _In_ LONG Volume);

    DWORD
    SpeakerConfig(VOID);

    VOID
    Lock(VOID) { EnterCriticalSection(&MixLock); }

    VOID
    Unlock(VOID) { LeaveCriticalSection(&MixLock); }

    GUID Id;
    WCHAR* EndpointId;
    UINT Rate;

    /* Set for good once any buffer changes rate by more than a fifth of a percent */
    BOOLEAN CubicRates;

    /* The final mix layout voices mix into, which can differ from the endpoint until it is locked */
    UINT Channels;
    DWORD Mask;
    UINT EndpointChannels;
    DWORD EndpointMask;
    BOOLEAN LayoutChosen;
    BOOLEAN LayoutLocked;

    /* Secondary buffer voices that exist on the device, playing or not */
    ULONG Secondaries;
    LONG Pan;
    WAVEFORMATEX* Global;

private:
    static
    DWORD
    WINAPI
    ThreadMain(
        _In_ PVOID Context);

    VOID
    Pump(VOID);

    VOID
    RenderQuantum(
        _Out_writes_(Quantum * Channels) float* Output);

    VOID
    LockLayout(VOID);

    VOID
    ChooseStreamFormat(VOID);

    VOID
    CloseStream(VOID);

    IMMDevice* Endpoint;
    IAudioClient* Client;
    IAudioRenderClient* Render;
    ISimpleAudioVolume* Volume;
    WAVEFORMATEX* MixFormat;
    WAVEFORMATEXTENSIBLE OwnFormat;
    const WAVEFORMATEX* StreamFormat;
    BOOLEAN FloatOutput;
    UINT32 QueueFrames;
    UINT Quantum;
    HANDLE Event;
    HANDLE Thread;
    HANDLE Quit;
    BOOLEAN Streaming;
    BOOLEAN Failed;
    CRITICAL_SECTION MixLock;
    DsVoice* Voices;
    float* Scratch;
};

/* device.cpp */

HRESULT
DsFindEndpoint(
    _In_ EDataFlow Flow,
    _In_opt_ LPCGUID Guid,
    _COM_Outptr_ IMMDevice** Endpoint,
    _Out_opt_ GUID* Id);

HRESULT
DsEnumerateEndpoints(
    _In_ EDataFlow Flow,
    _In_opt_ LPDSENUMCALLBACKA CallbackA,
    _In_opt_ LPDSENUMCALLBACKW CallbackW,
    _In_opt_ PVOID Context);

/* One endpoint as the DirectSoundPrivate device set describes it */
struct DsDeviceRecord
{
    GUID Id;
    BOOLEAN Capture;
    WCHAR Name[256];
    WCHAR* Module;
    WCHAR* Interface;
    ULONG WaveId;
};

HRESULT
DsListDeviceRecords(
    _Outptr_result_buffer_(*Count) DsDeviceRecord** Records,
    _Out_ ULONG* Count);

VOID
DsFreeDeviceRecords(
    _In_reads_opt_(Count) DsDeviceRecord* Records,
    _In_ ULONG Count);

/* private.cpp */

/* The class that hands out the device configuration property sets */
extern const CLSID DsPrivateClass;

HRESULT
DsCreatePrivate(
    _In_ REFIID Iid,
    _COM_Outptr_ void** Object);

HRESULT
DsResolveDeviceId(
    _In_ LPCGUID Source,
    _Out_ GUID* Destination);

/* The IDirectSound8 interface on a DirectSound object */
class DsSoundFace final : public IDirectSound8
{
public:
    DsFaceLink Link;
    DsSound* Self;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    CreateSoundBuffer(
        _In_ LPCDSBUFFERDESC Desc,
        _Outptr_ LPDIRECTSOUNDBUFFER* Buffer,
        _Pre_null_ LPUNKNOWN Outer) override;

    HRESULT
    STDMETHODCALLTYPE
    GetCaps(
        _Out_ LPDSCAPS Caps) override;

    HRESULT
    STDMETHODCALLTYPE
    DuplicateSoundBuffer(
        _In_ LPDIRECTSOUNDBUFFER Original,
        _Outptr_ LPDIRECTSOUNDBUFFER* Duplicate) override;

    HRESULT
    STDMETHODCALLTYPE
    SetCooperativeLevel(
        HWND Window,
        DWORD Level) override;

    HRESULT
    STDMETHODCALLTYPE
    Compact(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    GetSpeakerConfig(
        _Out_ LPDWORD Config) override;

    HRESULT
    STDMETHODCALLTYPE
    SetSpeakerConfig(
        DWORD Config) override;

    HRESULT
    STDMETHODCALLTYPE
    Initialize(
        _In_opt_ LPCGUID Device) override;

    HRESULT
    STDMETHODCALLTYPE
    VerifyCertification(
        _Out_ LPDWORD Certified) override;
};

/* A DirectSound object: the device, the cooperative level and the buffers made from it */
class DsSound final : public DsObject
{
public:
    DsSound();
    ~DsSound() override;

    VOID
    SetVersion(
        _In_ ULONG NewVersion);

    HRESULT
    Initialize(
        _In_opt_ LPCGUID Guid);

    HRESULT
    CreateBuffer(
        _In_ const DSBUFFERDESC* Desc,
        _Outptr_ DsBuffer** Buffer);

    HRESULT
    Duplicate(
        _In_ DsSecondary* Source,
        _Outptr_ DsBuffer** Buffer);

    HRESULT
    SetLevel(
        _In_ DWORD ThreadId,
        _In_ DWORD Level);

    VOID
    Forget(
        _In_ DsSecondary* Buffer);

    ULONG Version;
    HRESULT InitStatus;
    DWORD LevelThread;
    DWORD Level;
    DsDevice* Device;
    DsPrimary* Primary;
    DsSecondary* Buffers;
    DsSound* NextSound;
    DsSoundFace Face;
};

/* focus.cpp */

VOID
DsRegisterSound(
    _In_ DsSound* Sound);

VOID
DsUnregisterSound(
    _In_ DsSound* Sound);

VOID
DsUpdateFocus(
    _In_ BOOLEAN Force);

ULONG
DsBufferFocus(
    _In_ DsBuffer* Buffer);

VOID
DsApplyBufferFocus(
    _In_ DsSecondary* Buffer);

/* IDirectSoundBuffer8, shared by primary and secondary buffers */
class DsBufferFace final : public IDirectSoundBuffer8
{
public:
    DsFaceLink Link;
    DsBuffer* Self;
    ULONG Signature;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    GetCaps(
        _Out_ LPDSBCAPS Caps) override;

    HRESULT
    STDMETHODCALLTYPE
    GetCurrentPosition(
        _Out_opt_ LPDWORD Play,
        _Out_opt_ LPDWORD Write) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFormat(
        _Out_writes_bytes_opt_(SizeAllocated) LPWAVEFORMATEX Format,
        DWORD SizeAllocated,
        _Out_opt_ LPDWORD SizeWritten) override;

    HRESULT
    STDMETHODCALLTYPE
    GetVolume(
        _Out_ LPLONG Volume) override;

    HRESULT
    STDMETHODCALLTYPE
    GetPan(
        _Out_ LPLONG Pan) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFrequency(
        _Out_ LPDWORD Frequency) override;

    HRESULT
    STDMETHODCALLTYPE
    GetStatus(
        _Out_ LPDWORD Status) override;

    HRESULT
    STDMETHODCALLTYPE
    Initialize(
        _In_ LPDIRECTSOUND Sound,
        _In_ LPCDSBUFFERDESC Desc) override;

    HRESULT
    STDMETHODCALLTYPE
    Lock(
        DWORD Offset,
        DWORD Bytes,
        _Outptr_result_bytebuffer_(*Bytes1) LPVOID* Part1,
        _Out_ LPDWORD Bytes1,
        _Outptr_opt_result_bytebuffer_(*Bytes2) LPVOID* Part2,
        _Out_opt_ LPDWORD Bytes2,
        DWORD Flags) override;

    HRESULT
    STDMETHODCALLTYPE
    Play(
        DWORD Reserved,
        DWORD Priority,
        DWORD Flags) override;

    HRESULT
    STDMETHODCALLTYPE
    SetCurrentPosition(
        DWORD Position) override;

    HRESULT
    STDMETHODCALLTYPE
    SetFormat(
        _In_ LPCWAVEFORMATEX Format) override;

    HRESULT
    STDMETHODCALLTYPE
    SetVolume(
        LONG Volume) override;

    HRESULT
    STDMETHODCALLTYPE
    SetPan(
        LONG Pan) override;

    HRESULT
    STDMETHODCALLTYPE
    SetFrequency(
        DWORD Frequency) override;

    HRESULT
    STDMETHODCALLTYPE
    Stop(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    Unlock(
        _In_reads_bytes_(Bytes1) LPVOID Part1,
        DWORD Bytes1,
        _In_reads_bytes_opt_(Bytes2) LPVOID Part2,
        DWORD Bytes2) override;

    HRESULT
    STDMETHODCALLTYPE
    Restore(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    SetFX(
        DWORD Count,
        _In_reads_opt_(Count) LPDSEFFECTDESC Effects,
        _Out_writes_opt_(Count) LPDWORD Results) override;

    HRESULT
    STDMETHODCALLTYPE
    AcquireResources(
        DWORD Flags,
        DWORD Count,
        _Out_writes_opt_(Count) LPDWORD Results) override;

    HRESULT
    STDMETHODCALLTYPE
    GetObjectInPath(
        _In_ REFGUID Object,
        DWORD Index,
        _In_ REFGUID Interface,
        _Outptr_ LPVOID* Found) override;
};

/* IDirectSoundNotify on a secondary or capture buffer */
class DsNotifyFace final : public IDirectSoundNotify
{
public:
    DsFaceLink Link;
    DsSecondary* Render;
    DsCaptureBuffer* Capture;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    SetNotificationPositions(
        DWORD Count,
        _In_reads_(Count) LPCDSBPOSITIONNOTIFY Positions) override;
};

/* One property of a set: the handlers take the owning object, and Get updates the length to what it wrote or needs */
struct DsPropertyItem
{
    ULONG Id;
    HRESULT (*Get)(PVOID Owner, PVOID Data, ULONG* Length);
    HRESULT (*Set)(PVOID Owner, PVOID Data, ULONG Length);
    ULONG Minimum;
};

struct DsPropertyTable
{
    const GUID* Set;
    ULONG Count;
    const DsPropertyItem* Items;
};

/* property.cpp */

HRESULT
DsPropertyQuery(
    _In_reads_(TableCount) const DsPropertyTable* Tables,
    _In_ ULONG TableCount,
    _In_ const GUID* Set,
    _In_ ULONG Id,
    _Out_ ULONG* Support);

HRESULT
DsPropertyGet(
    _In_reads_(TableCount) const DsPropertyTable* Tables,
    _In_ ULONG TableCount,
    _In_opt_ PVOID Owner,
    _In_ const GUID* Set,
    _In_ ULONG Id,
    _Out_writes_bytes_(Length) PVOID Data,
    _In_ ULONG Length,
    _Out_opt_ ULONG* Returned);

HRESULT
DsPropertySet(
    _In_reads_(TableCount) const DsPropertyTable* Tables,
    _In_ ULONG TableCount,
    _In_opt_ PVOID Owner,
    _In_ const GUID* Set,
    _In_ ULONG Id,
    _In_reads_bytes_opt_(Length) PVOID Data,
    _In_ ULONG Length);

/* IKsPropertySet on buffers, which carry no property sets */
class DsPropertyFace final : public IKsPropertySet
{
public:
    DsFaceLink Link;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    Get(
        _In_ REFGUID Set,
        ULONG Id,
        _In_reads_bytes_opt_(InstanceLength) LPVOID Instance,
        ULONG InstanceLength,
        _Out_writes_bytes_(DataLength) LPVOID Data,
        ULONG DataLength,
        _Out_opt_ PULONG Returned) override;

    HRESULT
    STDMETHODCALLTYPE
    Set(
        _In_ REFGUID Set,
        ULONG Id,
        _In_reads_bytes_opt_(InstanceLength) LPVOID Instance,
        ULONG InstanceLength,
        _In_reads_bytes_(DataLength) LPVOID Data,
        ULONG DataLength) override;

    HRESULT
    STDMETHODCALLTYPE
    QuerySupport(
        _In_ REFGUID Set,
        ULONG Id,
        _Out_ PULONG Support) override;
};

class Ds3dBuffer;
class DsListener;
struct DsEffectChain;

/* Buffer status bits beyond the public DSBSTATUS ones */
#define DS_STATUS_LOST_INTERNAL 0x0002

/* What both kinds of buffer answer to */
class DsBuffer : public DsObject
{
public:
    DsBuffer(
        _In_ DsSound* Owner);

    virtual HRESULT IsInit(VOID) { return InitStatus; }

    virtual
    HRESULT
    GetCaps(
        _Inout_ DSBCAPS* Caps) = 0;

    virtual
    HRESULT
    GetCurrentPosition(
        _Out_opt_ DWORD* Play,
        _Out_opt_ DWORD* Write) = 0;

    virtual
    HRESULT
    GetFormat(
        _Out_writes_bytes_opt_(*Size) WAVEFORMATEX* Format,
        _Inout_ DWORD* Size) = 0;

    virtual
    HRESULT
    GetVolume(
        _Out_ LONG* Volume) = 0;

    virtual
    HRESULT
    GetPan(
        _Out_ LONG* Pan) = 0;

    virtual
    HRESULT
    GetFrequency(
        _Out_ DWORD* Frequency) = 0;

    virtual
    HRESULT
    GetStatus(
        _Out_opt_ DWORD* Status) = 0;

    virtual
    HRESULT
    Lock(
        DWORD Offset,
        DWORD Bytes,
        _Out_ VOID** Part1,
        _Out_ DWORD* Bytes1,
        _Out_opt_ VOID** Part2,
        _Out_opt_ DWORD* Bytes2,
        DWORD Flags) = 0;

    virtual
    HRESULT
    Unlock(
        _In_opt_ VOID* Part1,
        DWORD Bytes1,
        _In_opt_ VOID* Part2,
        DWORD Bytes2) = 0;

    virtual
    HRESULT
    Play(
        DWORD Priority,
        DWORD Flags) = 0;

    virtual
    HRESULT
    SetCurrentPosition(
        DWORD Position) = 0;

    virtual
    HRESULT
    SetFormat(
        _In_ const WAVEFORMATEX* Format) = 0;

    virtual
    HRESULT
    SetVolume(
        LONG Volume) = 0;

    virtual
    HRESULT
    SetPan(
        LONG Pan) = 0;

    virtual
    HRESULT
    SetFrequency(
        DWORD Frequency) = 0;

    virtual
    HRESULT
    Stop(VOID) = 0;

    virtual
    HRESULT
    Restore(VOID) = 0;

    virtual
    HRESULT
    SetFx(
        DWORD Count,
        _In_reads_opt_(Count) DSEFFECTDESC* Effects,
        _Out_writes_opt_(Count) DWORD* Results) = 0;

    virtual
    HRESULT
    AcquireResources(
        DWORD Flags,
        DWORD Count,
        _Out_writes_opt_(Count) DWORD* Results) = 0;

    virtual
    HRESULT
    GetObjectInPath(
        REFGUID Object,
        DWORD Index,
        REFGUID Interface,
        _Outptr_ VOID** Found) = 0;

    virtual HRESULT SuccessCode(VOID) { return S_OK; }

    DsSound* Owner;
    HRESULT InitStatus;
    ULONG Version;
    DWORD Flags;
    ULONG Bytes;
    WAVEFORMATEX* Format;
    ULONG StatusFlags;
    BOOLEAN Active;
    DsBufferFace Face;
    DsPropertyFace PropertyFace;
};

/* A region handed out by Lock, tracked until Unlock */
struct DsLockRegion
{
    BYTE* Pointer;
    ULONG Offset;
    ULONG Bytes;
};

#define DS_MAX_LOCKS 16

class DsSecondary final : public DsBuffer
{
public:
    DsSecondary(
        _In_ DsSound* Owner);
    ~DsSecondary() override;

    HRESULT
    Initialize(
        _In_opt_ const DSBUFFERDESC* Desc,
        _In_opt_ DsSecondary* Source);

    HRESULT
    GetCaps(
        _Inout_ DSBCAPS* Caps) override;

    HRESULT
    GetCurrentPosition(
        _Out_opt_ DWORD* Play,
        _Out_opt_ DWORD* Write) override;

    HRESULT
    GetFormat(
        _Out_writes_bytes_opt_(*Size) WAVEFORMATEX* Format,
        _Inout_ DWORD* Size) override;

    HRESULT
    GetVolume(
        _Out_ LONG* Volume) override;

    HRESULT
    GetPan(
        _Out_ LONG* Pan) override;

    HRESULT
    GetFrequency(
        _Out_ DWORD* Frequency) override;

    HRESULT
    GetStatus(
        _Out_opt_ DWORD* Status) override;

    HRESULT
    Lock(
        DWORD Offset,
        DWORD Bytes,
        _Out_ VOID** Part1,
        _Out_ DWORD* Bytes1,
        _Out_opt_ VOID** Part2,
        _Out_opt_ DWORD* Bytes2,
        DWORD Flags) override;

    HRESULT
    Unlock(
        _In_opt_ VOID* Part1,
        DWORD Bytes1,
        _In_opt_ VOID* Part2,
        DWORD Bytes2) override;

    HRESULT
    Play(
        DWORD Priority,
        DWORD Flags) override;

    HRESULT
    SetCurrentPosition(
        DWORD Position) override;

    HRESULT
    SetFormat(
        _In_ const WAVEFORMATEX* Format) override;

    HRESULT
    SetVolume(
        LONG Volume) override;

    HRESULT
    SetPan(
        LONG Pan) override;

    HRESULT
    SetFrequency(
        DWORD Frequency) override;

    HRESULT
    Stop(VOID) override;

    HRESULT
    Restore(VOID) override;

    HRESULT
    SetFx(
        DWORD Count,
        _In_reads_opt_(Count) DSEFFECTDESC* Effects,
        _Out_writes_opt_(Count) DWORD* Results) override;

    HRESULT
    AcquireResources(
        DWORD Flags,
        DWORD Count,
        _Out_writes_opt_(Count) DWORD* Results) override;

    HRESULT
    GetObjectInPath(
        REFGUID Object,
        DWORD Index,
        REFGUID Interface,
        _Outptr_ VOID** Found) override;

    HRESULT SuccessCode(VOID) override { return Special; }

    HRESULT
    SetNotifications(
        _In_ DWORD Count,
        _In_reads_(Count) const DSBPOSITIONNOTIFY* Positions);

    HRESULT
    ApplyAttenuation(VOID);

    VOID
    Lose(VOID);

    HRESULT
    InitializeVoice(
        _Inout_ WAVEFORMATEX* Wave,
        _In_opt_ DsSecondary* Source);

    DsSecondary* NextBuffer;
    DsVoice Voice;
    BYTE* Data;
    LONG Volume;
    LONG Pan;
    DWORD Frequency;
    ULONG Mute;
    HRESULT Special;
    GUID Algorithm;
    DWORD Priority;
    DsEffectChain* Effects;
    DsLockRegion Locks[DS_MAX_LOCKS];
    UINT LockCount;
    Ds3dBuffer* Spatial;
    DsNotifyFace NotifyFace;

private:
    HRESULT
    TrackLock(
        _In_ BYTE* Pointer,
        _In_ ULONG Offset,
        _In_ ULONG Bytes);

    BOOLEAN
    UntrackLock(
        _In_ VOID* Pointer);
};

class DsPrimary final : public DsBuffer
{
public:
    DsPrimary(
        _In_ DsSound* Owner);
    ~DsPrimary() override;

    HRESULT
    Initialize(VOID);

    HRESULT
    OnCreate(
        _In_ DWORD NewFlags);

    HRESULT
    SetLevel(
        _In_ DWORD Level);

    ULONG
    Grab(VOID) override;

    ULONG
    Drop(VOID) override;

    HRESULT
    GetCaps(
        _Inout_ DSBCAPS* Caps) override;

    HRESULT
    GetCurrentPosition(
        _Out_opt_ DWORD* Play,
        _Out_opt_ DWORD* Write) override;

    HRESULT
    GetFormat(
        _Out_writes_bytes_opt_(*Size) WAVEFORMATEX* Format,
        _Inout_ DWORD* Size) override;

    HRESULT
    GetVolume(
        _Out_ LONG* Volume) override;

    HRESULT
    GetPan(
        _Out_ LONG* Pan) override;

    HRESULT
    GetFrequency(
        _Out_ DWORD* Frequency) override;

    HRESULT
    GetStatus(
        _Out_opt_ DWORD* Status) override;

    HRESULT
    Lock(
        DWORD Offset,
        DWORD Bytes,
        _Out_ VOID** Part1,
        _Out_ DWORD* Bytes1,
        _Out_opt_ VOID** Part2,
        _Out_opt_ DWORD* Bytes2,
        DWORD Flags) override;

    HRESULT
    Unlock(
        _In_opt_ VOID* Part1,
        DWORD Bytes1,
        _In_opt_ VOID* Part2,
        DWORD Bytes2) override;

    HRESULT
    Play(
        DWORD Priority,
        DWORD Flags) override;

    HRESULT
    SetCurrentPosition(
        DWORD Position) override;

    HRESULT
    SetFormat(
        _In_ const WAVEFORMATEX* Format) override;

    HRESULT
    SetVolume(
        LONG Volume) override;

    HRESULT
    SetPan(
        LONG Pan) override;

    HRESULT
    SetFrequency(
        DWORD Frequency) override;

    HRESULT
    Stop(VOID) override;

    HRESULT
    Restore(VOID) override;

    HRESULT
    SetFx(
        DWORD Count,
        _In_reads_opt_(Count) DSEFFECTDESC* Effects,
        _Out_writes_opt_(Count) DWORD* Results) override;

    HRESULT
    AcquireResources(
        DWORD Flags,
        DWORD Count,
        _Out_writes_opt_(Count) DWORD* Results) override;

    HRESULT
    GetObjectInPath(
        REFGUID Object,
        DWORD Index,
        REFGUID Interface,
        _Outptr_ VOID** Found) override;

    LONG UserReferences;
    BOOLEAN WritePrimary;
    ULONG PlayState;
    BOOLEAN Locked;
    DsVoice* Voice;
    BYTE* Data;
    DsListener* Listener;
    DsLockRegion Locks[DS_MAX_LOCKS];
    UINT LockCount;

private:
    HRESULT
    SetFlags(
        _In_ DWORD NewFlags);

    HRESULT
    ApplyState(
        _In_ ULONG State);

    HRESULT
    OpenVoice(VOID);

    VOID
    CloseVoice(VOID);
};

/* sound3d.cpp */

HRESULT
DsCreateListener(
    _In_ DsPrimary* Primary,
    _Outptr_ DsListener** Listener);

VOID
DsDestroyListener(
    _In_ DsListener* Listener);

VOID
DsExposeListener(
    _In_ DsPrimary* Primary,
    _In_ DsListener* Listener);

HRESULT
DsCreate3dBuffer(
    _In_ DsSecondary* Buffer,
    _In_opt_ Ds3dBuffer* Source,
    _Outptr_ Ds3dBuffer** Spatial);

VOID
DsDestroy3dBuffer(
    _In_ Ds3dBuffer* Spatial);

BOOLEAN
Ds3dFilterAttenuation(
    _In_ Ds3dBuffer* Spatial,
    _In_ const DsAttenuation* Attenuation);

BOOLEAN
Ds3dFilterFrequency(
    _In_ Ds3dBuffer* Spatial,
    _In_ DWORD Frequency);

HRESULT
Ds3dSpecialCode(
    _In_ Ds3dBuffer* Spatial);

/* effects.cpp */

HRESULT
DsSetEffects(
    _In_ DsSecondary* Buffer,
    _In_ DWORD Count,
    _In_reads_opt_(Count) const DSEFFECTDESC* Effects,
    _Out_writes_opt_(Count) DWORD* Results);

DWORD
DsEffectCount(
    _In_ DsSecondary* Buffer);

HRESULT
DsFindEffect(
    _In_ DsSecondary* Buffer,
    _In_ REFGUID Object,
    _In_ DWORD Index,
    _In_ REFGUID Interface,
    _Outptr_ VOID** Found);

VOID
DsFreeEffects(
    _In_ DsSecondary* Buffer);

BYTE*
DsEffectInput(
    _In_ DsSecondary* Buffer);

VOID
DsRunEffects(
    _In_ DsEffectChain* Chain,
    _In_ DsVoice* Voice,
    _In_ ULONG Ahead);

VOID
DsResetEffects(
    _In_ DsEffectChain* Chain);

/* capture.cpp */

HRESULT
DsCaptureNotify(
    _In_ DsCaptureBuffer* Buffer,
    _In_ DWORD Count,
    _In_reads_opt_(Count) LPCDSBPOSITIONNOTIFY Positions);

HRESULT
DsCreateCapture(
    _In_ ULONG Version,
    _In_opt_ LPCGUID Guid,
    _In_ BOOLEAN Initialize,
    _In_ REFIID Iid,
    _COM_Outptr_ void** Object);

HRESULT
DsCreateFullDuplex(
    _In_opt_ LPCGUID CaptureGuid,
    _In_opt_ LPCGUID RenderGuid,
    _In_opt_ const DSCBUFFERDESC* CaptureDesc,
    _In_opt_ const DSBUFFERDESC* RenderDesc,
    _In_opt_ HWND Window,
    _In_ DWORD Level,
    _In_ BOOLEAN Initialize,
    _In_opt_ IUnknown* Outer,
    _Outptr_opt_ IDirectSoundFullDuplex** Duplex,
    _Outptr_opt_ IDirectSoundCaptureBuffer8** CaptureBuffer,
    _Outptr_opt_ IDirectSoundBuffer8** RenderBuffer);

/* sound.cpp */

HRESULT
DsCreateSound(
    _In_ ULONG Version,
    _In_opt_ LPCGUID Guid,
    _In_ BOOLEAN Initialize,
    _In_ REFIID Iid,
    _COM_Outptr_ void** Object);

DsSound*
DsSoundFromInterface(
    _In_opt_ IUnknown* Interface);

DsBuffer*
DsBufferFromInterface(
    _In_opt_ IUnknown* Interface);

HWND
DsRootWindow(
    _In_ HWND Window);
