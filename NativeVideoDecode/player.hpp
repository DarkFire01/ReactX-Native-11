/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     One open file: source, decoder, frame queue, clock and statistics
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <deque>
#include <memory>
#include <string>

#include "videodecoder.hpp"
#include "source.hpp"
#include "stats.hpp"

using Microsoft::WRL::ComPtr;

struct PlayerOptions
{
    double FrameRate = 0;
    std::wstring CsvPath;
    BOOLEAN Checksum = FALSE;
    std::wstring DumpDirectory;
    UINT DumpCount = 10;
    BOOLEAN GpuTiming = TRUE;
    UINT ConsumerSurfaces = 6;
};

enum class TickResult
{
    /* Nothing is due yet; call again after WaitMs (or sooner on input) */
    Wait,
    Show,
    Finished,
    Failed,
};

class Player
{
public:
    Player(
        _In_ ID3D11Device* Device,
        _In_ ID3D11DeviceContext* Context,
        _In_ const PlayerOptions& Options);

    ~Player();

    /* Opens the file and decodes until the first picture configures the decoder */
    HRESULT
    Open(
        _In_z_ PCWSTR Path);

    /*
     * Decodes ahead and decides what to show. Unpaced and Step hand out the next
     * frame at once; otherwise frames follow their timestamps and late ones drop.
     */
    TickResult
    Tick(
        _In_ BOOLEAN IsUnpaced,
        _In_ BOOLEAN IsStep,
        _Out_ DecodedFrame* Frame,
        _Out_ DWORD* WaitMs);

    /* The frame from Tick is on screen; the one before it goes back to the decoder */
    VOID
    FrameShown(
        _In_ const DecodedFrame& Frame);

    VOID
    SetPaused(
        _In_ BOOLEAN IsPaused);

    HRESULT
    SeekTo(
        _In_ INT64 Time);

    HRESULT
    SeekRelative(
        _In_ double Seconds);

    VOID
    SetLooping(
        _In_ BOOLEAN IsLooping)
    {
        Looping = IsLooping;
    }

    std::wstring
    DescribeStream() const;

    /* The decoder's format with the visible area limited to what the container declares */
    VideoFormatInfo
    GetVideoFormat() const;

    INT64
    GetPosition() const;

    INT64
    GetDuration() const
    {
        return Source->GetDuration();
    }

    BOOLEAN
    HasDisplayedFrame() const
    {
        return HasDisplayed;
    }

    const DecodedFrame&
    GetDisplayedFrame() const
    {
        return Displayed;
    }

    VideoDecoder&
    GetDecoder()
    {
        return *Decoder;
    }

    PlaybackStats&
    GetStats()
    {
        return *Stats;
    }

    double
    GetFrameRate() const
    {
        return FrameRate;
    }

    const std::wstring&
    GetPath() const
    {
        return Path;
    }

private:
    HRESULT
    FillQueue();

    VOID
    ReadBack(
        _In_ const DecodedFrame& Frame);

    VOID
    ReleaseQueue();

    VOID
    ClampToDisplaySize(
        _Inout_ RECT& Visible) const;

    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    PlayerOptions Options;
    std::wstring Path;

    std::unique_ptr<VideoSource> Source;
    std::unique_ptr<PlaybackStats> Stats;
    std::unique_ptr<VideoDecoder> Decoder;
    std::deque<DecodedFrame> Queue;
    std::vector<DecodedFrame> Ready;
    AccessUnit Unit;

    BOOLEAN IsEndOfStream = FALSE;
    BOOLEAN Looping = FALSE;
    double FrameRate = 30;

    DecodedFrame Displayed = {};
    BOOLEAN HasDisplayed = FALSE;
    UINT64 ShownCount = 0;

    /* Wall clock anchored to a stream time; re-anchored after pauses and seeks */
    INT64 ClockBaseTime = NvdNoTimestamp;
    double ClockBaseMs = 0;
    double PausedAtMs = 0;

    /* After a seek, pictures before this time are decoded but not shown */
    INT64 SkipUntil = NvdNoTimestamp;

    ComPtr<ID3D11Texture2D> Staging;
    UINT64 NextReadBack = 0;
};
