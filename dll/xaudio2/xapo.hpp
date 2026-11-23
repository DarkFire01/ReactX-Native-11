/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared base for the built in XAPOs
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "xaudio2.hpp"
#include <xaudio2fx.h>
#include <xapofx.h>

/* Registration flags every built in XAPO starts from */
#define XA_APO_FLAGS_STRICT     (XAPO_FLAG_CHANNELS_MUST_MATCH | XAPO_FLAG_FRAMERATE_MUST_MATCH | \
                                 XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH | XAPO_FLAG_BUFFERCOUNT_MUST_MATCH | \
                                 XAPO_FLAG_INPLACE_SUPPORTED)

/*
 * Parameters as the built in XAPOs keep them: the newest set waits for the
 * next Process, and GetParameters reports what Process last picked up.
 */
class XaApoParameters
{
public:
    XaApoParameters();
    ~XaApoParameters();

    BOOLEAN
    Allocate(
        _In_ UINT32 NewSize,
        _In_reads_bytes_(NewSize) const VOID* Initial);

    VOID
    Store(
        _In_reads_bytes_(Bytes) const VOID* Value,
        _In_ UINT32 Bytes);

    /* Returns the newest parameters when a set is waiting, otherwise nullptr */
    const BYTE*
    Take(VOID);

    VOID
    Read(
        _Out_writes_bytes_(Bytes) VOID* Value,
        _In_ UINT32 Bytes);

    UINT32 Size;

private:
    SRWLOCK Lock;
    BYTE* Newest;
    BYTE* Taken;
    BYTE* Reported;
    BOOLEAN Waiting;
};

class XaApo : public IXAPO, public IXAPOParameters
{
public:
    XaApo(
        _In_ const XAPO_REGISTRATION_PROPERTIES* NewRegistration);

    virtual ~XaApo();

    /* IUnknown */
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

    /* IXAPO */
    HRESULT
    STDMETHODCALLTYPE
    GetRegistrationProperties(
        _Outptr_ XAPO_REGISTRATION_PROPERTIES** Properties) override;

    HRESULT
    STDMETHODCALLTYPE
    IsInputFormatSupported(
        const WAVEFORMATEX* Other,
        const WAVEFORMATEX* Requested,
        _Outptr_opt_ WAVEFORMATEX** Supported) override;

    HRESULT
    STDMETHODCALLTYPE
    IsOutputFormatSupported(
        const WAVEFORMATEX* Other,
        const WAVEFORMATEX* Requested,
        _Outptr_opt_ WAVEFORMATEX** Supported) override;

    HRESULT
    STDMETHODCALLTYPE
    Initialize(
        _In_reads_bytes_opt_(Size) const void* Data,
        UINT32 Size) override;

    void
    STDMETHODCALLTYPE
    Reset() override;

    HRESULT
    STDMETHODCALLTYPE
    LockForProcess(
        UINT32 InputCount,
        _In_reads_opt_(InputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
        UINT32 OutputCount,
        _In_reads_opt_(OutputCount) const XAPO_LOCKFORPROCESS_PARAMETERS* Output) override;

    void
    STDMETHODCALLTYPE
    UnlockForProcess() override;

    UINT32
    STDMETHODCALLTYPE
    CalcInputFrames(
        UINT32 OutputFrames) override;

    UINT32
    STDMETHODCALLTYPE
    CalcOutputFrames(
        UINT32 InputFrames) override;

    /* IXAPOParameters */
    void
    STDMETHODCALLTYPE
    SetParameters(
        _In_reads_bytes_(Size) const void* Value,
        UINT32 Size) override;

    void
    STDMETHODCALLTYPE
    GetParameters(
        _Out_writes_bytes_(Size) void* Value,
        UINT32 Size) override;

    /* Checks one format on its own and, when Fix is set, moves it to the nearest one supported */
    virtual
    HRESULT
    ValidateFormat(
        _Inout_ WAVEFORMATEX* Format,
        _In_ BOOLEAN Fix);

    HRESULT
    ValidatePair(
        _In_ const WAVEFORMATEX* Other,
        _Inout_ WAVEFORMATEX* Format,
        _In_ BOOLEAN Fix);

    HRESULT
    CheckFormat(
        _In_ const WAVEFORMATEX* Other,
        _In_ const WAVEFORMATEX* Requested,
        _Outptr_opt_ WAVEFORMATEX** Supported);

    HRESULT
    LockCommon(
        _In_ const XAPO_LOCKFORPROCESS_PARAMETERS* Input,
        _In_ const XAPO_LOCKFORPROCESS_PARAMETERS* Output);

    const XAPO_REGISTRATION_PROPERTIES* Registration;
    XaApoParameters Parameters;
    LONG References;
    BOOLEAN Locked;
};

/* format.cpp helpers the XAPOs share */
WAVEFORMATEX*
XaApoCopyFormat(
    _In_ const WAVEFORMATEX* Format);

HRESULT
XaCreateMeterObject(
    _Outptr_ IUnknown** Apo);

HRESULT
XaCreateEqObject(
    _Outptr_ IUnknown** Apo);
