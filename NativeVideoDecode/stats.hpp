/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Decode and presentation statistics
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>

#include <stdio.h>
#include <string>
#include <vector>

double
NvdNowMs(VOID);

struct IntervalCounters
{
    UINT64 Decoded = 0;
    UINT64 Presented = 0;
    UINT64 Dropped = 0;
    UINT64 Bytes = 0;
    UINT64 PendingRetries = 0;
    double SubmitTotalMs = 0;
    double SubmitMaxMs = 0;
    double GpuTotalMs = 0;
    UINT64 GpuSamples = 0;
    double GpuMaxMs = 0;
};

class PlaybackStats
{
public:
    ~PlaybackStats();

    VOID
    Start();

    BOOLEAN
    OpenCsv(
        _In_z_ PCWSTR Path);

    VOID
    OnPictureDecoded(
        _In_ UINT64 DecodeIndex,
        _In_ CHAR Type,
        _In_ INT32 Poc,
        _In_ UINT32 Bytes,
        _In_ UINT32 Slices,
        _In_ double SubmitMs,
        _In_ UINT32 PendingRetries);

    VOID
    OnGpuComplete(
        _In_ double LatencyMs);

    VOID
    OnFramePresented(
        _In_ double LatenessMs);

    VOID
    OnFrameDropped();

    VOID
    OnFrameChecksum(
        _In_ UINT64 DisplayIndex,
        _In_ UINT32 Crc);

    VOID
    SetQueueState(
        _In_ UINT32 OutputQueue,
        _In_ UINT32 DpbFill,
        _In_ UINT32 DpbSize);

    /* Prints a one line interval report once a second */
    VOID
    ReportIfDue();

    VOID
    PrintSummary();

    std::wstring
    OverlayText() const;

    std::wstring
    TypeSummary() const;

    UINT64
    GetDecodedCount() const
    {
        return TotalDecoded;
    }

private:
    VOID
    PrintInterval(
        _In_ double Now);

    double StartMs = 0;
    double IntervalStartMs = 0;
    IntervalCounters Interval;
    IntervalCounters LastInterval;
    double LastIntervalSeconds = 1;

    UINT64 TotalDecoded = 0;
    UINT64 TotalPresented = 0;
    UINT64 TotalDropped = 0;
    UINT64 TotalBytes = 0;
    UINT64 TotalPendingRetries = 0;
    /* Indexed by picture type letter, A to Z */
    UINT64 TypeCounts[26] = {};
    UINT64 ChecksummedFrames = 0;
    UINT32 StreamCrc = 0;

    std::vector<double> SubmitMs;
    std::vector<double> GpuMs;
    std::vector<double> LatenessMs;

    UINT32 OutputQueue = 0;
    UINT32 DpbFill = 0;
    UINT32 DpbSize = 0;

    FILE* Csv = nullptr;
};
