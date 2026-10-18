/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Container readers that hand out H.264 access units in Annex B form
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

constexpr INT64 NvdNoTimestamp = INT64_MIN;

enum class VideoCodec
{
    H264,
    Av1,
};

struct AccessUnit
{
    /* H.264: start code prefixed NAL units. AV1: one temporal unit of sized OBUs */
    std::vector<UINT8> Data;

    /* 100 ns units, NvdNoTimestamp when the container has none */
    INT64 PresentationTime = NvdNoTimestamp;
    BOOLEAN IsKeyFrame = FALSE;
};

class VideoSource
{
public:
    virtual ~VideoSource() = default;

    virtual
    BOOLEAN
    ReadAccessUnit(
        _Out_ AccessUnit& Unit) = 0;

    virtual
    VOID
    Rewind() = 0;

    virtual
    PCSTR
    GetContainerName() const = 0;

    virtual
    VideoCodec
    GetCodec() const = 0;

    /* Zero when the container does not say */
    virtual
    double
    GetFrameRate() const = 0;

    virtual
    UINT64
    GetFrameCount() const = 0;

    /* 100 ns units, zero when unknown */
    virtual
    INT64
    GetDuration() const = 0;

    /* The picture size the container declares, zero when it says nothing */
    virtual
    SIZE
    GetDisplaySize() const
    {
        return DisplaySize;
    }

    /* Moves to the last key frame at or before Time; FALSE when the container cannot seek */
    virtual
    BOOLEAN
    SeekToKeyFrame(
        _In_ INT64 Time) = 0;

protected:
    SIZE DisplaySize = {};
};

std::unique_ptr<VideoSource>
NvdOpenMp4Source(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error);

std::unique_ptr<VideoSource>
NvdOpenAnnexBSource(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error);

std::unique_ptr<VideoSource>
NvdOpenMatroskaSource(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error);

/* Picks a reader from the file's first bytes, then its extension */
std::unique_ptr<VideoSource>
NvdOpenSource(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error);

/* avcC: start code prefixed SPS and PPS units, and the NAL length field size */
BOOLEAN
NvdParseAvcConfig(
    _In_reads_bytes_(Size) const UINT8* Config,
    _In_ SIZE_T Size,
    _Out_ std::vector<UINT8>& ParameterSets,
    _Out_ UINT32* LengthSize,
    _Out_ std::string& Error);

/* av1C: the configOBUs that follow the four byte header (usually a sequence header) */
BOOLEAN
NvdParseAv1Config(
    _In_reads_bytes_(Size) const UINT8* Config,
    _In_ SIZE_T Size,
    _Out_ std::vector<UINT8>& ConfigObus,
    _Out_ std::string& Error);

/* Appends length prefixed NAL units to Out as start code prefixed ones */
VOID
NvdAppendAnnexB(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size,
    _In_ UINT32 LengthSize,
    _Inout_ std::vector<UINT8>& Out);
