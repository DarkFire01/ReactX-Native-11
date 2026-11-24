/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The output interface the desktop window manager composes to
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxgiobjects.hpp"

/* Windows reports this when there is no target, and always composes in BGRA */
constexpr UINT RxNoVidPnTarget = 0xFFFFFFFF;

const LUID&
RxDxgiOutput::GetAdapterLuid() const
{
    return Adapter->GetLuid();
}

/* True while any process duplicates this output; the kernel counts them per source */
BOOL
STDMETHODCALLTYPE
RxDxgiOutput::HasDDAClient()
{
    D3DKMT_OUTPUTDUPLCONTEXTSCOUNT Count = {};
    D3DKMT_QUERYADAPTERINFO Query = {};

    Count.VidPnSourceId = Info.VidPnSourceId;
    Query.hAdapter = KmtAdapter;
    Query.Type = KMTQAITYPE_OUTPUTDUPLCONTEXTSCOUNT;
    Query.pPrivateDriverData = &Count;
    Query.PrivateDriverDataSize = sizeof(Count);
    if (!NT_SUCCESS(RxGetKmtThunks()->QueryAdapterInfo(&Query)))
        return FALSE;

    return Count.OutputDuplicationCount != 0;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetDesc(
    _Out_ DXGI_OUTPUT_DWM_DESC* pDesc)
{
    DEVMODEW Current = {};

    if (pDesc == nullptr)
        return E_INVALIDARG;

    *pDesc = {};
    Current.dmSize = sizeof(Current);
    if (!EnumDisplaySettingsW(Info.DeviceName, ENUM_CURRENT_SETTINGS, &Current))
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    pDesc->AdapterLuid = GetAdapterLuid();
    pDesc->VidPnSourceId = Info.VidPnSourceId;
    pDesc->VidPnTargetId = RxNoVidPnTarget;
    pDesc->Width = Current.dmPelsWidth;
    pDesc->Height = Current.dmPelsHeight;
    pDesc->Format = DXGI_FORMAT_B8G8R8A8_UNORM;

    /* A fixed rate display reports the same rate as its range */
    pDesc->RefreshRate.Numerator = Current.dmDisplayFrequency > 1 ? Current.dmDisplayFrequency : 60;
    pDesc->RefreshRate.Denominator = 1;
    pDesc->MinimumRefreshRate = pDesc->RefreshRate;
    pDesc->MaximumRefreshRate = pDesc->RefreshRate;
    pDesc->RefreshMultiplier = 1;
    pDesc->Rotation = (DXGI_MODE_ROTATION)(Current.dmDisplayOrientation + DXGI_MODE_ROTATION_IDENTITY);
    pDesc->ScanlineOrdering = (Current.dmDisplayFlags & DM_INTERLACED) ? DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST
                                                                         : DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    pDesc->SdrWhiteLevel = 1.0f;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::FindClosestMatchingModeFromDesktop(
    _In_ const DXGI_MODE_DESC1* pModeToMatch,
    _Out_ DXGI_MODE_DESC1* pClosestMatch,
    _In_opt_ IUnknown* pConcernedDevice)
{
    return FindClosestMatchingMode1(pModeToMatch, pClosestMatch, pConcernedDevice);
}

/* Returns at the next vertical blank, or sooner when one of the objects is already signaled */
HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::WaitForVBlankOrObjects(
    _In_ UINT Count,
    _In_reads_(Count) const HANDLE* pHandles)
{
    if (Count > MAXIMUM_WAIT_OBJECTS || (Count != 0 && pHandles == nullptr))
        return DXGI_ERROR_INVALID_CALL;

    if (Count != 0 && WaitForMultipleObjects(Count, pHandles, FALSE, 0) < WAIT_OBJECT_0 + Count)
        return S_OK;

    return WaitForVBlank();
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::SetSyncRefreshCountWaitTarget(
    _In_ UINT Target)
{
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetFrameStatisticsDWM(
    _Out_ DXGI_FRAME_STATISTICS_DWM* pStats)
{
    if (pStats == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pStats = {};
    return DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetVBlankEvent(
    _Out_ HANDLE* pEvent)
{
    if (pEvent == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pEvent = NULL;
    return DXGI_ERROR_UNSUPPORTED;
}

BOOL
STDMETHODCALLTYPE
RxDxgiOutput::IsIndependentFlipSupported()
{
    return FALSE;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetMultiplaneOverlayCaps(
    _In_ IUnknown* pDevice,
    _Out_ DXGI_MULTIPLANE_OVERLAY_CAPS* pCaps)
{
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetStereoCaps(
    _Out_ UINT* pCaps)
{
    if (pCaps == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pCaps = 0;
    return S_OK;
}
