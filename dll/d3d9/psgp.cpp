/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Entry points for processor specific geometry pipeline plug-ins
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

/* A plug-in reports a fatal error by unwinding back into the runtime with an HRESULT */
extern "C"
VOID
__cdecl
PSGPError(
    _In_opt_ VOID* Pipeline,
    _In_ ULONG Error,
    _In_ UINT Line)
{
    HRESULT Failure = D3DERR_INVALIDCALL;

    D9Trace(L"geometry plug-in error %lu at line %u", Error, Line);
    throw Failure;
}

/* No software vertex pipeline runs yet, so there is no sampler state to read */
extern "C"
VOID
__cdecl
PSGPSampleTexture(
    _In_opt_ VOID* Pipeline,
    _In_ UINT Stage,
    _In_reads_(Count) FLOAT (*Coordinates)[4],
    _In_ UINT Count,
    _Out_writes_(Count) FLOAT (*Results)[4])
{
    D9_TRACE_ONCE(L"PSGPSampleTexture has no software pipeline to sample from");
}
