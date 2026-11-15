/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Rendering parameters
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

/* What glyph rendering needs from any params object, ours or the caller's */
struct RxParamValues
{
    FLOAT Gamma;
    FLOAT CompatibleGamma;
    FLOAT EnhancedContrast;
    FLOAT GrayscaleEnhancedContrast;
    FLOAT ClearTypeLevel;
    DWRITE_PIXEL_GEOMETRY PixelGeometry;
    DWRITE_RENDERING_MODE1 RenderingMode;
    DWRITE_GRID_FIT_MODE GridFitMode;
    /* 1 none, 2 grayscale, 3 ClearType, as the system smoothing settings say */
    UINT32 CompatibleSmoothing;
    BOOLEAN IsModern;
};

class RxRenderingParams final : public IDWriteRenderingParams3
{
public:
    static
    HRESULT
    Create(
        _In_ const RxParamValues& Values,
        _Outptr_ RxRenderingParams** Params);

    /* Defaults with the registry and system settings for a display, or the primary one */
    static
    VOID
    SystemValues(
        _In_opt_z_ PCWSTR DeviceName,
        _Out_ RxParamValues* Values);

    /* Reads any params object; ours carry the fields the interfaces do not expose */
    static
    VOID
    ReadValues(
        _In_opt_ IDWriteRenderingParams* Params,
        _Out_ RxParamValues* Values);

    RX_DECLARE_IUNKNOWN

#include "params.inl"

    const RxParamValues&
    Values(VOID) const
    {
        return Fields;
    }

private:
    RxRenderingParams() : RefCount(1), Fields{} {}

    LONG RefCount;
    RxParamValues Fields;
};

FLOAT
RxCompatibleGamma(VOID);

UINT32
RxCompatibleSmoothing(VOID);
