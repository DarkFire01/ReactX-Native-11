/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Exported entry points
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

/* The version an engine reports when the caller does not give one */
#define XA_DEFAULT_VERSION      0x0A000000

static
HRESULT
XaCreateEngine(
    _Outptr_ IXAudio2** Result,
    _In_ UINT32 Flags,
    _In_ XAUDIO2_PROCESSOR Processor,
    _In_opt_ const void* SharedContexts,
    _In_ DWORD Version)
{
    XaEngine* Engine;
    HRESULT hr;

    if (Result == nullptr || (Flags & ~XA_ENGINE_FLAGS) != 0)
    {
        XA_TRACE_ONCE(L"XAudio2Create: NULL engine or flags %08X", Flags);
        return XAUDIO2_E_INVALID_CALL;
    }
    if (SharedContexts != nullptr && (Flags & XA_ENGINE_NO_SHAPE) != 0)
    {
        XA_TRACE_ONCE(L"XAudio2Create: shared contexts with the decoder turned off");
        return XAUDIO2_E_INVALID_CALL;
    }
    if (Processor == XAUDIO2_USE_DEFAULT_PROCESSOR)
        Processor = XAUDIO2_DEFAULT_PROCESSOR;

    Engine = new (std::nothrow) XaEngine;
    if (Engine == nullptr)
        return E_OUTOFMEMORY;
    hr = Engine->Initialize(Flags, Processor, Version);
    if (FAILED(hr))
    {
        Engine->Release();
        return hr;
    }
    XaTrace(L"engine %p created, flags %08X processor %08X version %08X", Engine, Flags, Processor, Version);
    *Result = Engine;
    return S_OK;
}

EXTERN_C
HRESULT
WINAPI
XaExportCreate(
    _Outptr_ IXAudio2** Result,
    _In_ UINT32 Flags,
    _In_ XAUDIO2_PROCESSOR Processor)
{
    return XaCreateEngine(Result, Flags, Processor, nullptr, XA_DEFAULT_VERSION);
}

EXTERN_C
HRESULT
WINAPI
XaExportCreateWithVersionInfo(
    _Outptr_ IXAudio2** Result,
    _In_ UINT32 Flags,
    _In_ XAUDIO2_PROCESSOR Processor,
    _In_ DWORD Version)
{
    return XaCreateEngine(Result, Flags, Processor, nullptr, Version);
}

EXTERN_C
HRESULT
WINAPI
XaExportCreateWithSharedContexts(
    _Outptr_ IXAudio2** Result,
    _In_ UINT32 Flags,
    _In_ XAUDIO2_PROCESSOR Processor,
    _In_opt_ const void* SharedContexts)
{
    return XaCreateEngine(Result, Flags, Processor, SharedContexts, XA_DEFAULT_VERSION);
}

EXTERN_C
HRESULT
WINAPI
XaExportCreateReverb(
    _Outptr_ IUnknown** Apo)
{
    return XaCreateReverb(Apo, FALSE);
}

EXTERN_C
HRESULT
WINAPI
XaExportCreateReverbLegacy(
    _Outptr_ IUnknown** Apo)
{
    return XaCreateReverb(Apo, TRUE);
}

EXTERN_C
HRESULT
WINAPI
XaExportCreateVolumeMeter(
    _Outptr_ IUnknown** Apo)
{
    return XaCreateVolumeMeter(Apo);
}

EXTERN_C
HRESULT
__cdecl
XaExportCreateFx(
    _In_ REFCLSID Clsid,
    _Outptr_ IUnknown** Effect,
    _In_reads_bytes_opt_(InitDataByteSize) const void* InitData,
    _In_ UINT32 InitDataByteSize)
{
    return XaCreateFx(Clsid, Effect, InitData, InitDataByteSize);
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);
    if (Reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(Instance);
    return TRUE;
}
