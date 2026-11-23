/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The built in XAPOs: reverb, volume meter and the FX library
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xapo.hpp"

HRESULT
XaCreateReverb(
    _Outptr_ IUnknown** Apo,
    _In_ BOOLEAN Legacy)
{
    UNREFERENCED_PARAMETER(Legacy);
    if (Apo != nullptr)
        *Apo = nullptr;
    XA_TRACE_ONCE(L"CreateAudioReverb: not available yet");
    return E_NOTIMPL;
}

HRESULT
XaCreateVolumeMeter(
    _Outptr_ IUnknown** Apo)
{
    return XaCreateMeterObject(Apo);
}

HRESULT
XaCreateFx(
    _In_ REFCLSID Clsid,
    _Outptr_ IUnknown** Effect,
    _In_reads_bytes_opt_(InitDataByteSize) const void* InitData,
    _In_ UINT32 InitDataByteSize)
{
    /* Only the echo takes its creation data, the others start from their defaults */
    if (IsEqualCLSID(Clsid, __uuidof(FXEQ)))
        return XaCreateEqObject(Effect);
    if (IsEqualCLSID(Clsid, __uuidof(FXMasteringLimiter)))
        return XaCreateLimiterObject(Effect);
    if (IsEqualCLSID(Clsid, __uuidof(FXEcho)))
        return XaCreateEchoObject(Effect, InitData, InitDataByteSize);

    /* Unknown classes report the same failure as an allocation that did not succeed */
    XA_TRACE_ONCE(L"CreateFX: class not available");
    return E_OUTOFMEMORY;
}
