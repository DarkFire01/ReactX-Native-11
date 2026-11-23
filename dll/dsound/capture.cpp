/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DirectSound capture over waveIn, and the full duplex object
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <mmsystem.h>

/* Capture packets hold a thirty second of a second, rounded up to whole frames */
#define DS_CAPTURE_PACKET_DIVISOR   32
#define DS_CAPTURE_MAX_PACKETS      8

/* Driver messages that report the endpoint id behind a waveIn device */
#define DS_DRV_QUERY_INSTANCE_ID        0x0811
#define DS_DRV_QUERY_INSTANCE_ID_SIZE   0x0812

class DsCaptureFace final : public IDirectSoundCapture
{
public:
    DsFaceLink Link;
    DsCapture* Self;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    CreateCaptureBuffer(
        _In_ LPCDSCBUFFERDESC Desc,
        _Outptr_ LPDIRECTSOUNDCAPTUREBUFFER* Buffer,
        _Pre_null_ LPUNKNOWN Outer) override;

    HRESULT
    STDMETHODCALLTYPE
    GetCaps(
        _Out_ LPDSCCAPS Caps) override;

    HRESULT
    STDMETHODCALLTYPE
    Initialize(
        _In_opt_ LPCGUID Device) override;
};

class DsCaptureBufferFace final : public IDirectSoundCaptureBuffer8
{
public:
    DsFaceLink Link;
    DsCaptureBuffer* Self;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    GetCaps(
        _Out_ LPDSCBCAPS Caps) override;

    HRESULT
    STDMETHODCALLTYPE
    GetCurrentPosition(
        _Out_opt_ LPDWORD Capture,
        _Out_opt_ LPDWORD Read) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFormat(
        _Out_writes_bytes_opt_(SizeAllocated) LPWAVEFORMATEX Format,
        DWORD SizeAllocated,
        _Out_opt_ LPDWORD SizeWritten) override;

    HRESULT
    STDMETHODCALLTYPE
    GetStatus(
        _Out_ LPDWORD Status) override;

    HRESULT
    STDMETHODCALLTYPE
    Initialize(
        _In_ LPDIRECTSOUNDCAPTURE Capture,
        _In_ LPCDSCBUFFERDESC Desc) override;

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
    Start(
        DWORD Flags) override;

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
    GetObjectInPath(
        _In_ REFGUID Object,
        DWORD Index,
        _In_ REFGUID Interface,
        _Outptr_ LPVOID* Found) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFXStatus(
        DWORD Count,
        _Out_writes_(Count) LPDWORD Results) override;
};

class DsCapture final : public DsObject
{
public:
    DsCapture();
    ~DsCapture() override;

    HRESULT
    Initialize(
        _In_opt_ LPCGUID Guid);

    ULONG Version;
    HRESULT InitStatus;
    UINT WaveId;
    BOOLEAN Busy;
    GUID Id;
    DsCapture* NextCapture;
    DsCaptureFace Face;
};

/* Every capture object in the process, oldest first */
static DsCapture* DsCaptures;

class DsCaptureBuffer final : public DsObject
{
public:
    DsCaptureBuffer(
        _In_ DsCapture* Owner);
    ~DsCaptureBuffer() override;

    HRESULT
    Initialize(
        _In_ const DSCBUFFERDESC* Desc);

    HRESULT
    StartCapture(
        _In_ DWORD Flags);

    HRESULT
    StopCapture(VOID);

    VOID
    Collect(VOID);

    VOID
    GetCursors(
        _Out_opt_ DWORD* Capture,
        _Out_opt_ DWORD* Read);

    static
    DWORD
    WINAPI
    ThreadMain(
        _In_ PVOID Context);

    DsCapture* Owner;
    DWORD Flags;
    ULONG Bytes;
    WAVEFORMATEX* Format;
    BYTE* Data;
    HWAVEIN Wave;
    WAVEHDR Headers[DS_CAPTURE_MAX_PACKETS];
    UINT HeaderCount;
    UINT NextDone;
    ULONG Packet;
    ULONG QueueOffset;
    ULONG Base;
    ULONG Done;
    BOOLEAN Capturing;
    BOOLEAN Looping;
    BOOLEAN Stopping;
    DsNotifyPoint* Points;
    ULONG PointCount;
    HANDLE Event;
    HANDLE Quit;
    HANDLE Thread;
    CRITICAL_SECTION Guard;
    DsCaptureBufferFace Face;
    DsNotifyFace NotifyFace;
};

/* The waveIn device behind an endpoint, matched by its function instance id */
static
UINT
DsWaveForEndpoint(
    _In_ IMMDevice* Endpoint)
{
    WCHAR* Id = nullptr;
    UINT Found = WAVE_MAPPER;
    UINT Count = waveInGetNumDevs();

    if (FAILED(Endpoint->GetId(&Id)))
        return WAVE_MAPPER;
    for (UINT Device = 0; Device < Count; Device++)
    {
        ULONG Size = 0;
        WCHAR* Instance;

        if (waveInMessage(reinterpret_cast<HWAVEIN>(static_cast<UINT_PTR>(Device)), DS_DRV_QUERY_INSTANCE_ID_SIZE,
                          reinterpret_cast<DWORD_PTR>(&Size), 0) != MMSYSERR_NOERROR || Size == 0)
        {
            continue;
        }
        Instance = static_cast<WCHAR*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size + sizeof(WCHAR)));
        if (Instance == nullptr)
            continue;
        if (waveInMessage(reinterpret_cast<HWAVEIN>(static_cast<UINT_PTR>(Device)), DS_DRV_QUERY_INSTANCE_ID,
                          reinterpret_cast<DWORD_PTR>(Instance), Size) == MMSYSERR_NOERROR &&
            _wcsicmp(Instance, Id) == 0)
        {
            Found = Device;
        }
        HeapFree(GetProcessHeap(), 0, Instance);
        if (Found != WAVE_MAPPER)
            break;
    }
    CoTaskMemFree(Id);
    return Found;
}

static
HRESULT
DsWaveError(
    _In_ MMRESULT Result)
{
    switch (Result)
    {
        case MMSYSERR_NOERROR:
            return S_OK;
        case MMSYSERR_ALLOCATED:
            return DSERR_ALLOCATED;
        case MMSYSERR_NOMEM:
            return E_OUTOFMEMORY;
        case WAVERR_BADFORMAT:
            return DSERR_BADFORMAT;
        case MMSYSERR_BADDEVICEID:
        case MMSYSERR_NODRIVER:
            return DSERR_NODRIVER;
    }
    return DSERR_GENERIC;
}

DsCapture::DsCapture()
{
    Version = DS_VERSION_7;
    InitStatus = DSERR_UNINITIALIZED;
    WaveId = WAVE_MAPPER;
    Busy = FALSE;
    Id = GUID_NULL;
    NextCapture = nullptr;
    Face.Link.Object = this;
    Face.Link.Count = 0;
    Face.Self = this;
    Expose(IID_IDirectSoundCapture, &Face, &Face.Link);

    {
        DsApiGuard Guard;
        DsCapture** Link = &DsCaptures;

        while (*Link != nullptr)
            Link = &(*Link)->NextCapture;
        *Link = this;
    }
}

DsCapture::~DsCapture()
{
    DsApiGuard Guard;

    for (DsCapture** Link = &DsCaptures; *Link != nullptr; Link = &(*Link)->NextCapture)
    {
        if (*Link == this)
        {
            *Link = NextCapture;
            break;
        }
    }
}

/* Hands each initialized capture object of the process to the visitor; called with the API lock held */
VOID
DsVisitCaptures(
    _In_ PDS_OBJECT_VISITOR Visitor,
    _In_opt_ PVOID Context)
{
    for (DsCapture* Capture = DsCaptures; Capture != nullptr; Capture = Capture->NextCapture)
    {
        if (SUCCEEDED(Capture->InitStatus))
            Visitor(Context, &Capture->Face, &Capture->Id);
    }
}

HRESULT
DsCapture::Initialize(
    _In_opt_ LPCGUID Guid)
{
    IMMDevice* Endpoint = nullptr;
    HRESULT hr;

    hr = DsFindEndpoint(eCapture, Guid, &Endpoint, &Id);
    if (FAILED(hr))
        return hr;
    WaveId = DsWaveForEndpoint(Endpoint);
    Endpoint->Release();
    InitStatus = S_OK;
    return S_OK;
}

HRESULT
DsCreateCapture(
    _In_ ULONG Version,
    _In_opt_ LPCGUID Guid,
    _In_ BOOLEAN Initialize,
    _In_ REFIID Iid,
    _COM_Outptr_ void** Object)
{
    DsCapture* Capture = new (std::nothrow) DsCapture();
    HRESULT hr;

    *Object = nullptr;
    if (Capture == nullptr)
        return E_OUTOFMEMORY;
    Capture->Version = Version;
    if (Initialize)
    {
        hr = Capture->Initialize(Guid);
        if (FAILED(hr))
        {
            Capture->Drop();
            return hr;
        }
    }
    hr = Capture->Query(Iid, TRUE, Object);
    if (FAILED(hr))
        Capture->Drop();
    return hr;
}

#define DS_CAPTURE_ENTER() \
    DsApiGuard Guard; \
    if (FAILED(Self->InitStatus)) \
        return Self->InitStatus

HRESULT
STDMETHODCALLTYPE
DsCaptureFace::CreateCaptureBuffer(
    _In_ LPCDSCBUFFERDESC Desc,
    _Outptr_ LPDIRECTSOUNDCAPTUREBUFFER* Buffer,
    _Pre_null_ LPUNKNOWN Outer)
{
    DSCBUFFERDESC Valid;
    DsCaptureBuffer* Created;
    void* Face = nullptr;
    HRESULT hr;

    DS_CAPTURE_ENTER();
    if (Desc == nullptr)
        return E_INVALIDARG;
    hr = DsBuildCaptureDesc(Desc, &Valid, Self->Version);
    if (FAILED(hr))
        return hr;
    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;
    if (Buffer == nullptr)
        return E_INVALIDARG;
    *Buffer = nullptr;
    if (Self->Busy)
        return DSERR_ALLOCATED;

    Created = new (std::nothrow) DsCaptureBuffer(Self);
    if (Created == nullptr)
        return E_OUTOFMEMORY;
    hr = Created->Initialize(&Valid);
    if (FAILED(hr))
    {
        Created->Drop();
        return hr;
    }
    hr = Created->Query(IID_IDirectSoundCaptureBuffer, TRUE, &Face);
    if (FAILED(hr))
    {
        Created->Drop();
        return hr;
    }
    *Buffer = static_cast<LPDIRECTSOUNDCAPTUREBUFFER>(Face);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsCaptureFace::GetCaps(
    _Out_ LPDSCCAPS Caps)
{
    WAVEINCAPSW Device = {};
    HRESULT hr;

    DS_CAPTURE_ENTER();
    if (Caps == nullptr || Caps->dwSize != sizeof(*Caps))
        return E_INVALIDARG;
    hr = DsWaveError(waveInGetDevCapsW(Self->WaveId, &Device, sizeof(Device)));
    if (FAILED(hr))
        return hr;
    Caps->dwFlags = DSCCAPS_MULTIPLECAPTURE;
    Caps->dwFormats = Device.dwFormats;
    Caps->dwChannels = Device.wChannels;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsCaptureFace::Initialize(
    _In_opt_ LPCGUID Device)
{
    DsApiGuard Guard;

    if (Self->InitStatus == S_OK)
        return DSERR_ALREADYINITIALIZED;
    if (Self->InitStatus != DSERR_UNINITIALIZED)
        return Self->InitStatus;
    return Self->Initialize(Device);
}

DsCaptureBuffer::DsCaptureBuffer(
    _In_ DsCapture* NewOwner)
{
    Owner = NewOwner;
    Owner->Grab();
    Flags = 0;
    Bytes = 0;
    Format = nullptr;
    Data = nullptr;
    Wave = nullptr;
    ZeroMemory(Headers, sizeof(Headers));
    HeaderCount = 0;
    NextDone = 0;
    Packet = 0;
    QueueOffset = 0;
    Base = 0;
    Done = 0;
    Capturing = FALSE;
    Looping = FALSE;
    Stopping = FALSE;
    Points = nullptr;
    PointCount = 0;
    Event = nullptr;
    Quit = nullptr;
    Thread = nullptr;
    InitializeCriticalSection(&Guard);
    Face.Link.Object = this;
    Face.Link.Count = 0;
    Face.Self = this;
    NotifyFace.Link.Object = this;
    NotifyFace.Link.Count = 0;
    NotifyFace.Render = nullptr;
    NotifyFace.Capture = this;
    Expose(IID_IDirectSoundCaptureBuffer, &Face, &Face.Link);
    if (Owner->Version >= DS_VERSION_8)
        Expose(IID_IDirectSoundCaptureBuffer8, &Face, &Face.Link);
    Expose(IID_IDirectSoundNotify, &NotifyFace, &NotifyFace.Link);
}

DsCaptureBuffer::~DsCaptureBuffer()
{
    if (Wave != nullptr)
    {
        waveInReset(Wave);
        for (UINT Index = 0; Index < HeaderCount; Index++)
        {
            if (Headers[Index].dwFlags & WHDR_PREPARED)
                waveInUnprepareHeader(Wave, &Headers[Index], sizeof(Headers[Index]));
        }
        waveInClose(Wave);
        Owner->Busy = FALSE;
    }
    if (Thread != nullptr)
    {
        SetEvent(Quit);
        WaitForSingleObject(Thread, INFINITE);
        CloseHandle(Thread);
    }
    if (Event != nullptr)
        CloseHandle(Event);
    if (Quit != nullptr)
        CloseHandle(Quit);
    DeleteCriticalSection(&Guard);
    HeapFree(GetProcessHeap(), 0, Points);
    HeapFree(GetProcessHeap(), 0, Data);
    HeapFree(GetProcessHeap(), 0, Format);
    Owner->Drop();
}

HRESULT
DsCaptureBuffer::Initialize(
    _In_ const DSCBUFFERDESC* Desc)
{
    const WAVEFORMATEX* Wanted = Desc->lpwfxFormat;
    ULONG Align = Wanted->nBlockAlign;
    MMRESULT Result;

    if (Align == 0 || Desc->dwBufferBytes < Align || Desc->dwBufferBytes % Align != 0)
        return E_INVALIDARG;
    Flags = Desc->dwFlags;
    Bytes = Desc->dwBufferBytes;
    Format = DsCopyFormat(Wanted);
    Data = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Bytes));
    if (Format == nullptr || Data == nullptr)
        return E_OUTOFMEMORY;

    Packet = Format->nAvgBytesPerSec / DS_CAPTURE_PACKET_DIVISOR;
    if (Packet % Align != 0)
        Packet += Align - Packet % Align;
    if (Packet == 0)
        Packet = Align;
    HeaderCount = (Bytes + Packet - 1) / Packet;
    if (HeaderCount > DS_CAPTURE_MAX_PACKETS)
        HeaderCount = DS_CAPTURE_MAX_PACKETS;

    Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Quit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (Event == nullptr || Quit == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());

    Result = waveInOpen(&Wave, Owner->WaveId, Format, reinterpret_cast<DWORD_PTR>(Event), 0, CALLBACK_EVENT);
    if (Result != MMSYSERR_NOERROR)
    {
        DsTrace(L"waveInOpen failed (%u)", Result);
        Wave = nullptr;
        return DsWaveError(Result);
    }
    Owner->Busy = TRUE;

    Thread = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
    if (Thread == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

/* Hands the device the next stretch of the buffer, splitting at the end */
static
VOID
DsQueuePacket(
    _In_ DsCaptureBuffer* Buffer,
    _In_ WAVEHDR* Header)
{
    ULONG Length = DsMin(Buffer->Packet, Buffer->Bytes - Buffer->QueueOffset);

    if (Header->dwFlags & WHDR_PREPARED)
        waveInUnprepareHeader(Buffer->Wave, Header, sizeof(*Header));
    ZeroMemory(Header, sizeof(*Header));
    Header->lpData = reinterpret_cast<LPSTR>(Buffer->Data + Buffer->QueueOffset);
    Header->dwBufferLength = Length;
    waveInPrepareHeader(Buffer->Wave, Header, sizeof(*Header));
    waveInAddBuffer(Buffer->Wave, Header, sizeof(*Header));
    Buffer->QueueOffset = (Buffer->QueueOffset + Length) % Buffer->Bytes;
}

HRESULT
DsCaptureBuffer::StartCapture(
    _In_ DWORD StartFlags)
{
    EnterCriticalSection(&Guard);
    Looping = (StartFlags & DSCBSTART_LOOPING) != 0;
    if (Capturing)
    {
        LeaveCriticalSection(&Guard);
        return S_OK;
    }
    Base = (Base + Done) % Bytes;
    Done = 0;
    NextDone = 0;
    QueueOffset = Base;
    Stopping = FALSE;
    for (UINT Index = 0; Index < HeaderCount; Index++)
        DsQueuePacket(this, &Headers[Index]);
    Capturing = TRUE;
    LeaveCriticalSection(&Guard);
    return DsWaveError(waveInStart(Wave));
}

HRESULT
DsCaptureBuffer::StopCapture(VOID)
{
    EnterCriticalSection(&Guard);
    if (!Capturing)
    {
        LeaveCriticalSection(&Guard);
        return S_OK;
    }
    Stopping = TRUE;
    LeaveCriticalSection(&Guard);

    waveInReset(Wave);
    Collect();

    EnterCriticalSection(&Guard);
    Capturing = FALSE;
    for (ULONG Index = 0; Index < PointCount; Index++)
    {
        if (Points[Index].Offset == DSBPN_OFFSETSTOP)
            SetEvent(Points[Index].Event);
    }
    LeaveCriticalSection(&Guard);
    return S_OK;
}

/* Takes in every finished packet in order, firing events the read cursor passed */
VOID
DsCaptureBuffer::Collect(VOID)
{
    EnterCriticalSection(&Guard);
    while (Capturing && (Headers[NextDone].dwFlags & WHDR_DONE))
    {
        WAVEHDR* Header = &Headers[NextDone];
        ULONG Before = (Base + Done) % Bytes;
        ULONG After;
        BOOLEAN Wrapped;

        Done += Header->dwBytesRecorded;
        After = (Base + Done) % Bytes;
        Wrapped = Header->dwBytesRecorded != 0 && After <= Before;
        for (ULONG Index = 0; Index < PointCount; Index++)
        {
            DWORD Offset = Points[Index].Offset;
            BOOLEAN Passed;

            if (Offset == DSBPN_OFFSETSTOP || Header->dwBytesRecorded == 0)
                continue;
            if (Wrapped)
                Passed = Offset >= Before || Offset < After;
            else
                Passed = Offset >= Before && Offset < After;
            if (Passed)
                SetEvent(Points[Index].Event);
        }

        Header->dwFlags &= ~WHDR_DONE;
        NextDone = (NextDone + 1) % HeaderCount;
        if (Stopping)
            continue;
        if (!Looping && Done >= Bytes)
        {
            Stopping = TRUE;
            continue;
        }
        DsQueuePacket(this, Header);
    }

    if (Capturing && Stopping && !Looping && Done >= Bytes)
    {
        Capturing = FALSE;
        waveInReset(Wave);
        for (ULONG Index = 0; Index < PointCount; Index++)
        {
            if (Points[Index].Offset == DSBPN_OFFSETSTOP)
                SetEvent(Points[Index].Event);
        }
    }
    LeaveCriticalSection(&Guard);
}

DWORD
WINAPI
DsCaptureBuffer::ThreadMain(
    _In_ PVOID Context)
{
    DsCaptureBuffer* Self = static_cast<DsCaptureBuffer*>(Context);
    HANDLE Waits[2] = { Self->Quit, Self->Event };

    for (;;)
    {
        if (WaitForMultipleObjects(2, Waits, FALSE, INFINITE) == WAIT_OBJECT_0)
            break;
        Self->Collect();
    }
    return 0;
}

/* The read cursor follows finished packets; the capture cursor follows the device */
VOID
DsCaptureBuffer::GetCursors(
    _Out_opt_ DWORD* Capture,
    _Out_opt_ DWORD* Read)
{
    ULONG ReadAt;
    ULONG CaptureAt;

    EnterCriticalSection(&Guard);
    ReadAt = (Base + Done) % Bytes;
    CaptureAt = ReadAt;
    if (Capturing)
    {
        MMTIME Time = {};

        Time.wType = TIME_BYTES;
        if (waveInGetPosition(Wave, &Time, sizeof(Time)) == MMSYSERR_NOERROR && Time.wType == TIME_BYTES)
            CaptureAt = (Base + Time.u.cb) % Bytes;
    }
    LeaveCriticalSection(&Guard);
    if (Capture != nullptr)
        *Capture = CaptureAt;
    if (Read != nullptr)
        *Read = ReadAt;
}

HRESULT
DsCaptureNotify(
    _In_ DsCaptureBuffer* Buffer,
    _In_ DWORD Count,
    _In_reads_opt_(Count) LPCDSBPOSITIONNOTIFY Positions)
{
    DsNotifyPoint* Points = nullptr;
    ULONG Align = Buffer->Format->nBlockAlign;

    if (Count != 0)
    {
        Points = static_cast<DsNotifyPoint*>(HeapAlloc(GetProcessHeap(), 0, Count * sizeof(*Points)));
        if (Points == nullptr)
            return E_OUTOFMEMORY;
        for (DWORD Index = 0; Index < Count; Index++)
        {
            Points[Index].Offset = Positions[Index].dwOffset;
            Points[Index].Event = Positions[Index].hEventNotify;
            if (Points[Index].Offset != DSBPN_OFFSETSTOP)
                Points[Index].Offset = Points[Index].Offset / Align * Align;
            if ((Points[Index].Offset != DSBPN_OFFSETSTOP && Points[Index].Offset >= Buffer->Bytes) ||
                Points[Index].Event == nullptr)
            {
                HeapFree(GetProcessHeap(), 0, Points);
                return E_INVALIDARG;
            }
        }
    }
    EnterCriticalSection(&Buffer->Guard);
    HeapFree(GetProcessHeap(), 0, Buffer->Points);
    Buffer->Points = Points;
    Buffer->PointCount = Count;
    LeaveCriticalSection(&Buffer->Guard);
    return S_OK;
}

#define DS_CAPTURE_BUFFER_ENTER() \
    DsApiGuard Guard

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::GetCaps(
    _Out_ LPDSCBCAPS Caps)
{
    DS_CAPTURE_BUFFER_ENTER();
    if (Caps == nullptr || Caps->dwSize != sizeof(*Caps))
        return E_INVALIDARG;
    Caps->dwFlags = Self->Flags;
    Caps->dwBufferBytes = Self->Bytes;
    Caps->dwReserved = 0;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::GetCurrentPosition(
    _Out_opt_ LPDWORD Capture,
    _Out_opt_ LPDWORD Read)
{
    DWORD CaptureAt = 0;
    DWORD ReadAt = 0;
    ULONG Align = Self->Format->nBlockAlign;

    DS_CAPTURE_BUFFER_ENTER();
    if (Capture == nullptr && Read == nullptr)
        return E_INVALIDARG;
    Self->GetCursors(&CaptureAt, &ReadAt);
    if (Capture != nullptr)
        *Capture = CaptureAt / Align * Align;
    if (Read != nullptr)
        *Read = ReadAt / Align * Align;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::GetFormat(
    _Out_writes_bytes_opt_(SizeAllocated) LPWAVEFORMATEX Format,
    DWORD SizeAllocated,
    _Out_opt_ LPDWORD SizeWritten)
{
    DWORD Size = SizeAllocated;
    HRESULT hr;

    DS_CAPTURE_BUFFER_ENTER();
    if (Format == nullptr)
    {
        if (SizeWritten == nullptr)
            return E_INVALIDARG;
        Size = 0;
    }
    hr = DsCopyFormatOut(Self->Format, Format, &Size);
    if (SUCCEEDED(hr) && SizeWritten != nullptr)
        *SizeWritten = Size;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::GetStatus(
    _Out_ LPDWORD Status)
{
    DS_CAPTURE_BUFFER_ENTER();
    if (Status == nullptr)
        return E_INVALIDARG;
    Self->Collect();
    *Status = 0;
    if (Self->Capturing)
    {
        *Status = DSCBSTATUS_CAPTURING;
        if (Self->Looping)
            *Status |= DSCBSTATUS_LOOPING;
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::Initialize(
    _In_ LPDIRECTSOUNDCAPTURE Capture,
    _In_ LPCDSCBUFFERDESC Desc)
{
    DS_CAPTURE_BUFFER_ENTER();
    if (Capture == nullptr || Desc == nullptr)
        return E_INVALIDARG;
    return DSERR_ALREADYINITIALIZED;
}

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::Lock(
    DWORD Offset,
    DWORD Bytes,
    _Outptr_result_bytebuffer_(*Bytes1) LPVOID* Part1,
    _Out_ LPDWORD Bytes1,
    _Outptr_opt_result_bytebuffer_(*Bytes2) LPVOID* Part2,
    _Out_opt_ LPDWORD Bytes2,
    DWORD Flags)
{
    ULONG Size = Self->Bytes;

    DS_CAPTURE_BUFFER_ENTER();
    if (Part1 == nullptr || Bytes1 == nullptr)
        return E_INVALIDARG;
    *Part1 = nullptr;
    *Bytes1 = 0;
    if (Part2 != nullptr)
        *Part2 = nullptr;
    if (Bytes2 != nullptr)
        *Bytes2 = 0;
    if (Offset >= Size || (Flags & ~DSCBLOCK_ENTIREBUFFER))
        return E_INVALIDARG;
    if (Flags & DSCBLOCK_ENTIREBUFFER)
        Bytes = Size;
    if (Bytes > Size || Bytes == 0)
        return E_INVALIDARG;

    *Part1 = Self->Data + Offset;
    if (Offset + Bytes > Size)
    {
        *Bytes1 = Size - Offset;
        if (Part2 != nullptr)
            *Part2 = Self->Data;
        if (Bytes2 != nullptr)
            *Bytes2 = Offset + Bytes - Size;
    }
    else
    {
        *Bytes1 = Bytes;
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::Start(
    DWORD Flags)
{
    DS_CAPTURE_BUFFER_ENTER();
    if (Flags & ~DSCBSTART_LOOPING)
        return E_INVALIDARG;
    return Self->StartCapture(Flags);
}

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::Stop(VOID)
{
    DS_CAPTURE_BUFFER_ENTER();
    return Self->StopCapture();
}

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::Unlock(
    _In_reads_bytes_(Bytes1) LPVOID Part1,
    DWORD Bytes1,
    _In_reads_bytes_opt_(Bytes2) LPVOID Part2,
    DWORD Bytes2)
{
    DS_CAPTURE_BUFFER_ENTER();
    if (Part1 == nullptr && Bytes1 != 0)
        return E_INVALIDARG;
    if (Bytes2 != 0 && Part2 == nullptr)
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::GetObjectInPath(
    _In_ REFGUID Object,
    DWORD Index,
    _In_ REFGUID Interface,
    _Outptr_ LPVOID* Found)
{
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Interface);

    DS_CAPTURE_BUFFER_ENTER();
    if (Found == nullptr)
        return E_INVALIDARG;
    *Found = nullptr;
    return DSERR_CONTROLUNAVAIL;
}

HRESULT
STDMETHODCALLTYPE
DsCaptureBufferFace::GetFXStatus(
    DWORD Count,
    _Out_writes_(Count) LPDWORD Results)
{
    UNREFERENCED_PARAMETER(Count);

    DS_CAPTURE_BUFFER_ENTER();
    if (Results == nullptr)
        return E_INVALIDARG;
    return DSERR_CONTROLUNAVAIL;
}

class DsDuplex;

class DsDuplexFace final : public IDirectSoundFullDuplex
{
public:
    DsFaceLink Link;
    DsDuplex* Self;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    Initialize(
        _In_ LPCGUID CaptureGuid,
        _In_ LPCGUID RenderGuid,
        _In_ LPCDSCBUFFERDESC CaptureDesc,
        _In_ LPCDSBUFFERDESC RenderDesc,
        HWND Window,
        DWORD Level,
        _Outptr_ LPLPDIRECTSOUNDCAPTUREBUFFER8 CaptureBuffer,
        _Outptr_ LPLPDIRECTSOUNDBUFFER8 RenderBuffer) override;
};

/* Owns one capture and one render object and hands out their own interfaces */
class DsDuplex final : public DsObject
{
public:
    DsDuplex()
    {
        Sound = nullptr;
        Capture = nullptr;
        InitStatus = DSERR_UNINITIALIZED;
        Face.Link.Object = this;
        Face.Link.Count = 0;
        Face.Self = this;
        Expose(IID_IDirectSoundFullDuplex, &Face, &Face.Link);
    }

    ~DsDuplex() override
    {
        if (Sound != nullptr)
            Sound->Drop();
        if (Capture != nullptr)
            Capture->Drop();
    }

    HRESULT
    Initialize(
        _In_opt_ LPCGUID CaptureGuid,
        _In_opt_ LPCGUID RenderGuid,
        _In_ const DSCBUFFERDESC* CaptureDesc,
        _In_ const DSBUFFERDESC* RenderDesc,
        _In_ HWND Window,
        _In_ DWORD Level,
        _Outptr_ IDirectSoundCaptureBuffer8** CaptureBuffer,
        _Outptr_ IDirectSoundBuffer8** RenderBuffer);

    DsSound* Sound;
    DsCapture* Capture;
    HRESULT InitStatus;
    DsDuplexFace Face;
};

HRESULT
DsDuplex::Initialize(
    _In_opt_ LPCGUID CaptureGuid,
    _In_opt_ LPCGUID RenderGuid,
    _In_ const DSCBUFFERDESC* CaptureDesc,
    _In_ const DSBUFFERDESC* RenderDesc,
    _In_ HWND Window,
    _In_ DWORD Level,
    _Outptr_ IDirectSoundCaptureBuffer8** CaptureBuffer,
    _Outptr_ IDirectSoundBuffer8** RenderBuffer)
{
    IDirectSoundCaptureBuffer* CaptureMade = nullptr;
    IDirectSoundBuffer* RenderMade = nullptr;
    HRESULT hr;

    Capture = new (std::nothrow) DsCapture();
    Sound = new (std::nothrow) DsSound();
    if (Capture == nullptr || Sound == nullptr)
        return E_OUTOFMEMORY;
    Capture->Version = DS_VERSION_8;
    Sound->SetVersion(DS_VERSION_8);
    hr = Capture->Initialize(CaptureGuid);
    if (SUCCEEDED(hr))
        hr = Sound->Initialize(RenderGuid);
    if (FAILED(hr))
        return hr;

    hr = Capture->Face.CreateCaptureBuffer(CaptureDesc, &CaptureMade, nullptr);
    if (SUCCEEDED(hr))
        hr = Sound->Face.SetCooperativeLevel(Window, Level);
    if (SUCCEEDED(hr))
        hr = Sound->Face.CreateSoundBuffer(RenderDesc, &RenderMade, nullptr);
    if (FAILED(hr))
    {
        if (CaptureMade != nullptr)
            CaptureMade->Release();
        return hr;
    }

    /* The buffers keep only the caller's references */
    hr = CaptureMade->QueryInterface(IID_IDirectSoundCaptureBuffer8, reinterpret_cast<void**>(CaptureBuffer));
    CaptureMade->Release();
    if (SUCCEEDED(hr))
    {
        hr = RenderMade->QueryInterface(IID_IDirectSoundBuffer8, reinterpret_cast<void**>(RenderBuffer));
        if (FAILED(hr))
            (*CaptureBuffer)->Release();
    }
    RenderMade->Release();
    if (SUCCEEDED(hr))
    {
        Expose(IID_IDirectSound, &Sound->Face, &Sound->Face.Link);
        Expose(IID_IDirectSound8, &Sound->Face, &Sound->Face.Link);
        Expose(IID_IDirectSoundCapture, &Capture->Face, &Capture->Face.Link);
        InitStatus = S_OK;
    }
    return hr;
}

HRESULT
STDMETHODCALLTYPE
DsDuplexFace::Initialize(
    _In_ LPCGUID CaptureGuid,
    _In_ LPCGUID RenderGuid,
    _In_ LPCDSCBUFFERDESC CaptureDesc,
    _In_ LPCDSBUFFERDESC RenderDesc,
    HWND Window,
    DWORD Level,
    _Outptr_ LPLPDIRECTSOUNDCAPTUREBUFFER8 CaptureBuffer,
    _Outptr_ LPLPDIRECTSOUNDBUFFER8 RenderBuffer)
{
    DsApiGuard Guard;

    if (Self->InitStatus == S_OK)
        return DSERR_ALREADYINITIALIZED;
    if (CaptureDesc == nullptr || RenderDesc == nullptr || Window == nullptr || CaptureBuffer == nullptr ||
        RenderBuffer == nullptr)
    {
        return E_INVALIDARG;
    }
    return Self->Initialize(CaptureGuid, RenderGuid, CaptureDesc, RenderDesc, Window, Level, CaptureBuffer,
                            RenderBuffer);
}

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
    _Outptr_opt_ IDirectSoundBuffer8** RenderBuffer)
{
    DsDuplex* Created;
    void* Face = nullptr;
    HRESULT hr;

    if (Initialize)
    {
        DSCBUFFERDESC CaptureValid;
        DSBUFFERDESC RenderValid;

        if (CaptureDesc == nullptr)
            return E_INVALIDARG;
        hr = DsBuildCaptureDesc(CaptureDesc, &CaptureValid, DS_VERSION_8);
        if (FAILED(hr))
            return hr;
        if (RenderDesc == nullptr)
            return E_INVALIDARG;
        hr = DsBuildBufferDesc(RenderDesc, &RenderValid, DS_VERSION_8);
        if (FAILED(hr))
            return hr;
        if (RenderValid.dwFlags & DSBCAPS_PRIMARYBUFFER)
            return E_INVALIDARG;
        if (Window == nullptr || Level < DSSCL_NORMAL || Level > DSSCL_WRITEPRIMARY)
            return E_INVALIDARG;
        if (Duplex == nullptr || CaptureBuffer == nullptr || RenderBuffer == nullptr)
            return E_INVALIDARG;
    }
    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;

    Created = new (std::nothrow) DsDuplex();
    if (Created == nullptr)
        return E_OUTOFMEMORY;
    if (Initialize)
    {
        hr = Created->Initialize(CaptureGuid, RenderGuid, CaptureDesc, RenderDesc, Window, Level, CaptureBuffer,
                                 RenderBuffer);
        if (FAILED(hr))
        {
            Created->Drop();
            return hr;
        }
    }
    hr = Created->Query(IID_IDirectSoundFullDuplex, TRUE, &Face);
    if (FAILED(hr))
    {
        Created->Drop();
        return hr;
    }
    *Duplex = static_cast<IDirectSoundFullDuplex*>(Face);
    return S_OK;
}
