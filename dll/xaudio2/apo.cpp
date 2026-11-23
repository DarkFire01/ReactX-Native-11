/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The built in XAPOs: reverb, volume meter and the FX library
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

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
    if (Apo != nullptr)
        *Apo = nullptr;
    XA_TRACE_ONCE(L"CreateAudioVolumeMeter: not available yet");
    return E_NOTIMPL;
}

HRESULT
XaCreateFx(
    _In_ REFCLSID Clsid,
    _Outptr_ IUnknown** Effect,
    _In_reads_bytes_opt_(InitDataByteSize) const void* InitData,
    _In_ UINT32 InitDataByteSize)
{
    UNREFERENCED_PARAMETER(Clsid);
    UNREFERENCED_PARAMETER(InitData);
    UNREFERENCED_PARAMETER(InitDataByteSize);
    if (Effect != nullptr)
        *Effect = nullptr;
    XA_TRACE_ONCE(L"CreateFX: not available yet");
    return E_NOTIMPL;
}
