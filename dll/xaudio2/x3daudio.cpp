/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     X3DAudio positional calculations
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include <x3daudio.h>

EXTERN_C
HRESULT
__cdecl
XaExportX3DInitialize(
    _In_ UINT32 SpeakerChannelMask,
    _In_ FLOAT32 SpeedOfSound,
    _Out_writes_bytes_(X3DAUDIO_HANDLE_BYTESIZE) X3DAUDIO_HANDLE Instance)
{
    UNREFERENCED_PARAMETER(SpeakerChannelMask);
    UNREFERENCED_PARAMETER(SpeedOfSound);
    memset(Instance, 0, X3DAUDIO_HANDLE_BYTESIZE);
    XA_TRACE_ONCE(L"X3DAudioInitialize: not available yet");
    return E_NOTIMPL;
}

EXTERN_C
void
__cdecl
XaExportX3DCalculate(
    _In_reads_bytes_(X3DAUDIO_HANDLE_BYTESIZE) const X3DAUDIO_HANDLE Instance,
    _In_ const X3DAUDIO_LISTENER* Listener,
    _In_ const X3DAUDIO_EMITTER* Emitter,
    _In_ UINT32 Flags,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Listener);
    UNREFERENCED_PARAMETER(Emitter);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Settings);
    XA_TRACE_ONCE(L"X3DAudioCalculate: not available yet");
}
