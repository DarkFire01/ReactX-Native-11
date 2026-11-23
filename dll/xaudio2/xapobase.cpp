/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared base for the built in XAPOs
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xapo.hpp"

static const GUID XaApoSubFormatBase = { 0x00000000, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

/* Bytes of a format that carry meaning, as the XAPO helpers count them */
static
SIZE_T
XaApoFormatBytes(
    _In_ const WAVEFORMATEX* Format,
    _In_ BOOLEAN WithSize)
{
    if (Format->wFormatTag == XA_TAG_XMA)
        return 32;
    if (Format->wFormatTag == WAVE_FORMAT_PCM)
        return WithSize ? sizeof(WAVEFORMATEX) : sizeof(PCMWAVEFORMAT);
    return sizeof(WAVEFORMATEX) + Format->cbSize;
}

/* The channel mask a format stands for, including the XMA stream masks */
static
DWORD
XaApoChannelMask(
    _In_ const WAVEFORMATEX* Format)
{
    if (Format->wFormatTag == XA_TAG_XMA)
    {
        const BYTE* Streams = reinterpret_cast<const BYTE*>(Format) + 12;
        WORD StreamCount = *reinterpret_cast<const WORD*>(reinterpret_cast<const BYTE*>(Format) + 8);
        DWORD Mask = 0;

        for (UINT32 Index = 0; Index < StreamCount; Index++)
        {
            WORD StreamMask = *reinterpret_cast<const WORD*>(Streams + Index * 20 + 18);

            Mask |= (StreamMask & 0xFF) | (StreamMask >> 8);
        }
        return Mask;
    }
    return XaFormatMask(Format);
}

WAVEFORMATEX*
XaApoCopyFormat(
    _In_ const WAVEFORMATEX* Format)
{
    SIZE_T Allocation = XaMax<SIZE_T>(XaApoFormatBytes(Format, TRUE), sizeof(WAVEFORMATEXTENSIBLE));
    SIZE_T Used = XaApoFormatBytes(Format, FALSE);
    BYTE* Copy = static_cast<BYTE*>(CoTaskMemAlloc(Allocation));
    WAVEFORMATEXTENSIBLE* Extensible;

    if (Copy == nullptr)
        return nullptr;
    memcpy(Copy, Format, Used);
    memset(Copy + Used, 0, Allocation - Used);
    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        return reinterpret_cast<WAVEFORMATEX*>(Copy);

    /* Anything else comes back in its extensible spelling */
    Extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Copy);
    Extensible->Samples.wValidBitsPerSample = Format->wBitsPerSample;
    Extensible->Format.cbSize = sizeof(*Extensible) - sizeof(Extensible->Format);
    Extensible->dwChannelMask = XaApoChannelMask(Format);
    Extensible->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    Extensible->SubFormat = XaApoSubFormatBase;
    Extensible->SubFormat.Data1 = Format->wFormatTag;
    return reinterpret_cast<WAVEFORMATEX*>(Copy);
}

XaApoParameters::XaApoParameters()
{
    InitializeSRWLock(&Lock);
    Size = 0;
    Newest = nullptr;
    Taken = nullptr;
    Reported = nullptr;
    Waiting = FALSE;
}

XaApoParameters::~XaApoParameters()
{
    delete[] Newest;
}

BOOLEAN
XaApoParameters::Allocate(
    _In_ UINT32 NewSize,
    _In_reads_bytes_(NewSize) const VOID* Initial)
{
    Newest = new (std::nothrow) BYTE[3 * NewSize];
    if (Newest == nullptr)
        return FALSE;
    Taken = Newest + NewSize;
    Reported = Taken + NewSize;
    Size = NewSize;
    memcpy(Newest, Initial, NewSize);
    memcpy(Taken, Initial, NewSize);
    memcpy(Reported, Initial, NewSize);
    return TRUE;
}

VOID
XaApoParameters::Store(
    _In_reads_bytes_(Bytes) const VOID* Value,
    _In_ UINT32 Bytes)
{
    AcquireSRWLockExclusive(&Lock);
    memcpy(Newest, Value, XaMin(Bytes, Size));
    Waiting = TRUE;
    ReleaseSRWLockExclusive(&Lock);
}

const BYTE*
XaApoParameters::Take(VOID)
{
    BOOLEAN Fresh;

    AcquireSRWLockExclusive(&Lock);
    Fresh = Waiting;
    if (Fresh)
    {
        memcpy(Taken, Newest, Size);
        memcpy(Reported, Newest, Size);
        Waiting = FALSE;
    }
    ReleaseSRWLockExclusive(&Lock);
    return Fresh ? Taken : nullptr;
}

VOID
XaApoParameters::Overwrite(
    _In_reads_bytes_(Size) const VOID* Value)
{
    AcquireSRWLockExclusive(&Lock);
    memcpy(Newest, Value, Size);
    memcpy(Taken, Value, Size);
    memcpy(Reported, Value, Size);
    ReleaseSRWLockExclusive(&Lock);
}

VOID
XaApoParameters::Read(
    _Out_writes_bytes_(Bytes) VOID* Value,
    _In_ UINT32 Bytes)
{
    AcquireSRWLockShared(&Lock);
    memcpy(Value, Reported, XaMin(Bytes, Size));
    ReleaseSRWLockShared(&Lock);
}

XaApo::XaApo(
    _In_ const XAPO_REGISTRATION_PROPERTIES* NewRegistration)
{
    Registration = NewRegistration;
    References = 1;
    Locked = FALSE;
}

XaApo::~XaApo()
{
}

HRESULT
STDMETHODCALLTYPE
XaApo::QueryInterface(
    REFIID Iid,
    _COM_Outptr_ void** Object)
{
    if (IsEqualIID(Iid, __uuidof(IXAPOParameters)))
    {
        *Object = static_cast<IXAPOParameters*>(this);
        AddRef();
        return S_OK;
    }
    if (IsEqualIID(Iid, __uuidof(IXAPO)) || IsEqualIID(Iid, IID_IUnknown))
    {
        *Object = static_cast<IXAPO*>(this);
        AddRef();
        return S_OK;
    }
    *Object = nullptr;
    return E_NOINTERFACE;
}

ULONG
STDMETHODCALLTYPE
XaApo::AddRef()
{
    return InterlockedIncrement(&References);
}

ULONG
STDMETHODCALLTYPE
XaApo::Release()
{
    ULONG Remaining = InterlockedDecrement(&References);

    if (Remaining == 0)
        delete this;
    return Remaining;
}

HRESULT
STDMETHODCALLTYPE
XaApo::GetRegistrationProperties(
    _Outptr_ XAPO_REGISTRATION_PROPERTIES** Properties)
{
    XAPO_REGISTRATION_PROPERTIES* Copy;

    Copy = static_cast<XAPO_REGISTRATION_PROPERTIES*>(CoTaskMemAlloc(sizeof(*Copy)));
    if (Copy == nullptr)
        return E_OUTOFMEMORY;
    memcpy(Copy, Registration, sizeof(*Copy));
    *Properties = Copy;
    return S_OK;
}

HRESULT
XaApo::ValidateFormat(
    _Inout_ WAVEFORMATEX* Format,
    _In_ BOOLEAN Fix)
{
    HRESULT hr = S_OK;

    if (!XaFormatValid(Format))
        return E_INVALIDARG;

    /* Built in XAPOs run on 32 bit float alone */
    if (XaFormatTag(Format) != WAVE_FORMAT_IEEE_FLOAT)
    {
        hr = XAPO_E_FORMAT_UNSUPPORTED;
        if (Fix && Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Format)->SubFormat = XaApoSubFormatBase;
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Format)->SubFormat.Data1 = WAVE_FORMAT_IEEE_FLOAT;
        }
        else if (Fix)
        {
            Format->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        }
    }
    if (Format->nChannels == 0 || Format->nChannels > XAPO_MAX_CHANNELS)
    {
        hr = XAPO_E_FORMAT_UNSUPPORTED;
        if (Fix)
            Format->nChannels = Format->nChannels == 0 ? 1 : XAPO_MAX_CHANNELS;
    }
    if (Format->nSamplesPerSec < XAPO_MIN_FRAMERATE || Format->nSamplesPerSec > XAPO_MAX_FRAMERATE)
    {
        hr = XAPO_E_FORMAT_UNSUPPORTED;
        if (Fix)
            Format->nSamplesPerSec = Format->nSamplesPerSec < XAPO_MIN_FRAMERATE ? XAPO_MIN_FRAMERATE : XAPO_MAX_FRAMERATE;
    }
    if (Format->wBitsPerSample != 32)
    {
        hr = XAPO_E_FORMAT_UNSUPPORTED;
        if (Fix)
            Format->wBitsPerSample = 32;
    }
    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Format)->Samples.wValidBitsPerSample != 32)
    {
        hr = XAPO_E_FORMAT_UNSUPPORTED;
        if (Fix)
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Format)->Samples.wValidBitsPerSample = 32;
    }
    return hr;
}

HRESULT
XaApo::ValidatePair(
    _In_ const WAVEFORMATEX* Other,
    _Inout_ WAVEFORMATEX* Format,
    _In_ BOOLEAN Fix)
{
    HRESULT hr = S_OK;
    UINT32 Flags = Registration->Flags;
    WORD Bits;
    WORD OtherValid;

    if ((Flags & XAPO_FLAG_CHANNELS_MUST_MATCH) && Format->nChannels != Other->nChannels)
    {
        hr = XAPO_E_FORMAT_UNSUPPORTED;
        if (Fix)
            Format->nChannels = Other->nChannels;
    }
    if ((Flags & XAPO_FLAG_FRAMERATE_MUST_MATCH) && Format->nSamplesPerSec != Other->nSamplesPerSec)
    {
        hr = XAPO_E_FORMAT_UNSUPPORTED;
        if (Fix)
            Format->nSamplesPerSec = Other->nSamplesPerSec;
    }
    if (!(Flags & XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH))
        return hr;

    Bits = Format->wBitsPerSample;
    if (Bits != Other->wBitsPerSample)
    {
        hr = XAPO_E_FORMAT_UNSUPPORTED;
        if (Fix)
        {
            Format->wBitsPerSample = Other->wBitsPerSample;
            Bits = Other->wBitsPerSample;
        }
    }

    /* Valid bits compare against the other side's, or its container size when it has none */
    if (Other->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        OtherValid = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Other)->Samples.wValidBitsPerSample;
        if (Format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        {
            if (Bits != OtherValid)
                hr = XAPO_E_FORMAT_UNSUPPORTED;
            return hr;
        }
    }
    else
    {
        if (Format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
            return hr;
        OtherValid = Other->wBitsPerSample;
    }
    if (reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Format)->Samples.wValidBitsPerSample != OtherValid)
    {
        hr = XAPO_E_FORMAT_UNSUPPORTED;
        if (Fix)
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Format)->Samples.wValidBitsPerSample = OtherValid;
    }
    return hr;
}

HRESULT
XaApo::CheckFormat(
    _In_ const WAVEFORMATEX* Other,
    _In_ const WAVEFORMATEX* Requested,
    _Outptr_opt_ WAVEFORMATEX** Supported)
{
    WAVEFORMATEX* Copy;
    HRESULT hr;

    if (FAILED(ValidateFormat(const_cast<WAVEFORMATEX*>(Other), FALSE)) || !XaFormatValid(Requested))
        return E_INVALIDARG;

    /* Without an out parameter the request is only judged */
    if (Supported == nullptr)
    {
        hr = ValidateFormat(const_cast<WAVEFORMATEX*>(Requested), FALSE);
        if (SUCCEEDED(hr))
            hr = ValidatePair(Other, const_cast<WAVEFORMATEX*>(Requested), FALSE);
        return hr;
    }

    Copy = XaApoCopyFormat(Requested);
    if (Copy == nullptr)
        return E_OUTOFMEMORY;
    hr = ValidateFormat(Copy, TRUE);
    if (SUCCEEDED(hr))
        hr = ValidatePair(Other, Copy, TRUE);

    /* Only an unsupported answer hands the nearest format back */
    if (hr == XAPO_E_FORMAT_UNSUPPORTED)
    {
        *Supported = Copy;
        return hr;
    }
    CoTaskMemFree(Copy);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
XaApo::IsInputFormatSupported(
    const WAVEFORMATEX* Other,
    const WAVEFORMATEX* Requested,
    _Outptr_opt_ WAVEFORMATEX** Supported)
{
    return CheckFormat(Other, Requested, Supported);
}

HRESULT
STDMETHODCALLTYPE
XaApo::IsOutputFormatSupported(
    const WAVEFORMATEX* Other,
    const WAVEFORMATEX* Requested,
    _Outptr_opt_ WAVEFORMATEX** Supported)
{
    return CheckFormat(Other, Requested, Supported);
}

HRESULT
STDMETHODCALLTYPE
XaApo::Initialize(
    _In_reads_bytes_opt_(Size) const void* Data,
    UINT32 Size)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Size);
    return S_OK;
}

void
STDMETHODCALLTYPE
XaApo::Reset()
{
}

HRESULT
XaApo::LockCommon(
    _In_ const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
    _In_ const XAPO_LOCKFORPROCESS_PARAMETERS* Output)
{
    const WAVEFORMATEX* In = Input->pFormat;
    const WAVEFORMATEX* Out = Output->pFormat;
    float* Matrix;
    DWORD InMask = 0;
    DWORD OutMask = 0;
    HRESULT hr;

    /* XAPOs that hold the rate fixed also hold a channel map between the two sides */
    if (Registration->Flags & XAPO_FLAG_FRAMERATE_MUST_MATCH)
    {
        Matrix = static_cast<float*>(CoTaskMemAlloc(sizeof(*Matrix) * In->nChannels * Out->nChannels));
        if (Matrix == nullptr)
            return E_OUTOFMEMORY;
        if (In->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
            InMask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(In)->dwChannelMask;
        if (Out->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
            OutMask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Out)->dwChannelMask;
        if (InMask == 0)
            InMask = XaPseudoMask(In->nChannels);
        if (OutMask == 0)
            OutMask = XaPseudoMask(Out->nChannels);
        hr = XaDefaultMatrix(In->nChannels, Out->nChannels, InMask, OutMask, Matrix);
        CoTaskMemFree(Matrix);
        if (FAILED(hr))
            return hr;
    }
    Locked = TRUE;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
XaApo::LockForProcess(
    UINT32 InputCount,
    _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
    UINT32 OutputCount,
    _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output)
{
    UNREFERENCED_PARAMETER(InputCount);
    UNREFERENCED_PARAMETER(OutputCount);
    return LockCommon(Input, Output);
}

void
STDMETHODCALLTYPE
XaApo::UnlockForProcess()
{
    Locked = FALSE;
}

UINT32
STDMETHODCALLTYPE
XaApo::CalcInputFrames(
    UINT32 OutputFrames)
{
    return OutputFrames;
}

UINT32
STDMETHODCALLTYPE
XaApo::CalcOutputFrames(
    UINT32 InputFrames)
{
    return InputFrames;
}

void
STDMETHODCALLTYPE
XaApo::SetParameters(
    _In_reads_bytes_(Size) const void* Value,
    UINT32 Size)
{
    Parameters.Store(Value, Size);
}

void
STDMETHODCALLTYPE
XaApo::GetParameters(
    _Out_writes_bytes_(Size) void* Value,
    UINT32 Size)
{
    Parameters.Read(Value, Size);
}
