/*
 * PROJECT:     ReactX third party build support
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The PIX event calls D3D12TranslationLayer makes, without WinPixEventRuntime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * Events and markers on a command list are encoded as PIX event blobs (metadata 2) and handed to
 * BeginEvent and SetMarker, which is what the system layer sends. Events on the CPU timeline only
 * reach a running PIX capture, so they are left out.
 */

#pragma once

#include <windows.h>
#include <d3d12.h>

#include <type_traits>

/* Blob words the system layer writes for each kind of event */
constexpr UINT RxPixBlobMetadata = 2;
constexpr UINT64 RxPixBeginNoArgs = 0x800;
constexpr UINT64 RxPixBeginVarArgs = 0x400;
constexpr UINT64 RxPixMarkerNoArgs = 0x2000;
constexpr UINT64 RxPixMarkerVarArgs = 0x1C00;
constexpr UINT64 RxPixWideString = 0x0400000000000000ull;

/* The system layer stops writing a blob at 62 words */
constexpr UINT RxPixBlobWords = 62;

struct RxPixBlob
{
    UINT64 Words[RxPixBlobWords + 1];
    UINT Count;
};

/* Appends a wide string: a header word, then four characters a word up to the word with the null */
static
inline
VOID
RxPixPutString(
    _Inout_ RxPixBlob& Blob,
    _In_opt_z_ PCWSTR Text)
{
    if (Blob.Count >= RxPixBlobWords)
        return;

    if (Text == nullptr)
    {
        Blob.Words[Blob.Count] = 0;
        Blob.Count += 1;
        return;
    }

    Blob.Words[Blob.Count] = RxPixWideString;
    Blob.Count += 1;
    while (Blob.Count < RxPixBlobWords)
    {
        UINT64 Packed = 0;
        BOOLEAN Ended = FALSE;

        for (UINT Shift = 0; Shift < 64; Shift += 16)
        {
            if (*Text == UNICODE_NULL)
            {
                Ended = TRUE;
                break;
            }

            Packed |= static_cast<UINT64>(*Text) << Shift;
            Text += 1;
        }

        Blob.Words[Blob.Count] = Packed;
        Blob.Count += 1;
        if (Ended)
            return;
    }
}

static
inline
VOID
RxPixStart(
    _Out_ RxPixBlob& Blob,
    _In_ UINT64 Kind,
    _In_ UINT64 Color)
{
    Blob.Words[0] = Kind;
    Blob.Words[1] = Color;
    Blob.Count = 2;
}

/* Size handed to D3D12: the words written, without the closing zero word */
static
inline
UINT
RxPixSize(
    _Inout_ RxPixBlob& Blob)
{
    Blob.Words[Blob.Count] = 0;
    return Blob.Count * sizeof(UINT64);
}

template <typename CONTEXT>
static
inline
VOID
PIXBeginEvent(
    _In_ CONTEXT* Context,
    _In_ UINT64 Color,
    _In_z_ PCWSTR Name)
{
    RxPixBlob Blob;

    RxPixStart(Blob, RxPixBeginNoArgs, Color);
    RxPixPutString(Blob, Name);
    Context->BeginEvent(RxPixBlobMetadata, Blob.Words, RxPixSize(Blob));
}

template <typename CONTEXT>
static
inline
VOID
PIXBeginEvent(
    _In_ CONTEXT* Context,
    _In_ UINT64 Color,
    _In_z_ PCWSTR Format,
    _In_opt_z_ PCWSTR Argument)
{
    RxPixBlob Blob;

    RxPixStart(Blob, RxPixBeginVarArgs, Color);
    RxPixPutString(Blob, Format);
    RxPixPutString(Blob, Argument);
    Context->BeginEvent(RxPixBlobMetadata, Blob.Words, RxPixSize(Blob));
}

template <typename CONTEXT>
static
inline
VOID
PIXEndEvent(
    _In_ CONTEXT* Context)
{
    Context->EndEvent();
}

template <typename CONTEXT>
static
inline
VOID
PIXSetMarker(
    _In_ CONTEXT* Context,
    _In_ UINT64 Color,
    _In_z_ PCWSTR Name)
{
    RxPixBlob Blob;

    RxPixStart(Blob, RxPixMarkerNoArgs, Color);
    RxPixPutString(Blob, Name);
    Context->SetMarker(RxPixBlobMetadata, Blob.Words, RxPixSize(Blob));
}

template <typename CONTEXT>
static
inline
VOID
PIXSetMarker(
    _In_ CONTEXT* Context,
    _In_ UINT64 Color,
    _In_z_ PCWSTR Format,
    _In_opt_z_ PCWSTR Argument)
{
    RxPixBlob Blob;

    RxPixStart(Blob, RxPixMarkerVarArgs, Color);
    RxPixPutString(Blob, Format);
    RxPixPutString(Blob, Argument);
    Context->SetMarker(RxPixBlobMetadata, Blob.Words, RxPixSize(Blob));
}

/* CPU timeline events */
static
inline
VOID
PIXBeginEvent(
    _In_ UINT64 Color,
    _In_z_ PCWSTR Name)
{
    UNREFERENCED_PARAMETER(Color);
    UNREFERENCED_PARAMETER(Name);
}

static
inline
VOID
PIXEndEvent(VOID)
{
}

static
inline
VOID
PIXSetMarker(
    _In_ UINT64 Color,
    _In_z_ PCWSTR Name)
{
    UNREFERENCED_PARAMETER(Color);
    UNREFERENCED_PARAMETER(Name);
}

static
inline
VOID
PIXNotifyWakeFromFenceSignal(
    _In_ HANDLE Event)
{
    UNREFERENCED_PARAMETER(Event);
}

/* Ends a command list event when the scope closes */
template <typename CONTEXT>
class RxPixScope
{
public:
    RxPixScope(
        _In_ CONTEXT* Context,
        _In_ UINT64 Color,
        _In_z_ PCWSTR Name)
        : Context(Context)
    {
        PIXBeginEvent(Context, Color, Name);
    }

    ~RxPixScope()
    {
        PIXEndEvent(Context);
    }

private:
    CONTEXT* Context;
};

class RxPixCpuScope
{
public:
    RxPixCpuScope(
        _In_ UINT64 Color,
        _In_z_ PCWSTR Name)
    {
        PIXBeginEvent(Color, Name);
    }

    ~RxPixCpuScope()
    {
        PIXEndEvent();
    }
};

template <typename CONTEXT>
static
inline
RxPixScope<CONTEXT>
RxPixMakeScope(
    _In_ CONTEXT* Context,
    _In_ UINT64 Color,
    _In_z_ PCWSTR Name)
{
    return RxPixScope<CONTEXT>(Context, Color, Name);
}

static
inline
RxPixCpuScope
RxPixMakeScope(
    _In_ UINT64 Color,
    _In_z_ PCWSTR Name)
{
    return RxPixCpuScope(Color, Name);
}

#define RX_PIX_CONCAT_INNER(First, Second) First##Second
#define RX_PIX_CONCAT(First, Second) RX_PIX_CONCAT_INNER(First, Second)
#define PIXScopedEvent(...) auto RX_PIX_CONCAT(RxPixScopedEvent, __LINE__) = RxPixMakeScope(__VA_ARGS__)
