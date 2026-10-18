/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared pieces of the synthetic clip generator
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

#include <vector>

using Microsoft::WRL::ComPtr;

struct ClipSpec
{
    GUID Subtype;
    PCWSTR CodecName;
    UINT Width;
    UINT Height;
    UINT FrameRate;
    UINT Frames;
    UINT Bitrate;
};

/* One compressed picture as the encoder produced it */
struct EncodedUnit
{
    std::vector<UINT8> Data;
    INT64 Time;
    BOOLEAN IsKeyFrame;
};

/* The source picture for frame Index as 8 bit NV12; identical every time it is asked for */
VOID
GeneratePattern(
    _Out_ std::vector<UINT8>& Frame,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Index);

/* Drives a hardware encoder MFT directly, for codecs the sink writer cannot wire up itself */
HRESULT
EncodeWithMft(
    _In_ ID3D11Device* Device,
    _In_ IMFDXGIDeviceManager* Manager,
    _In_ const ClipSpec& Spec,
    _Out_ std::vector<EncodedUnit>& Units,
    _Out_ ComPtr<IMFMediaType>& OutputType);

/* AV1 in WebM, with Cues ahead of the clusters */
HRESULT
WriteAv1WebM(
    _In_z_ PCWSTR Path,
    _In_ const ClipSpec& Spec,
    _In_ const std::vector<EncodedUnit>& Units);
