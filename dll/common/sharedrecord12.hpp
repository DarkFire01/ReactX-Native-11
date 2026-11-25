/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The runtime record a Direct3D 12 heap or committed resource is shared with
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "sharedrecord.hpp"

#include <d3d12.h>

/*
 * Runtime data is a run of parts, each starting with its size and version. Direct3D 12
 * writes a D3D11 record with version 0, then one part for the heap and one for the
 * resource that lives at its start. Openers skip parts they do not know.
 */
struct RxSharedPart
{
    UINT Size;
    UINT Version;
};

/* Direct3D 12 parts carry this version and say which part they are */
constexpr UINT RxSharedPartVersion12 = 6;
constexpr UINT RxSharedHeapPart12 = 0;
constexpr UINT RxSharedResourcePart12 = 1;

/* A part shorter than this tells an opener nothing but the texture size and format */
constexpr UINT RxSharedBasicRecordSize = 0x38;

struct RxSharedHeapRecord12
{
    UINT Size;
    UINT Version;
    UINT Part;
    UINT Reserved;
    D3D12_HEAP_DESC Desc;
    UINT Sharing;
    UINT Reserved2;
};

static_assert(sizeof(RxSharedHeapRecord12) == 0x48);

struct RxSharedResourceRecord12
{
    UINT Size;
    UINT Version;
    UINT Part;
    UINT Reserved;
    D3D12_RESOURCE_DESC1 Desc;
    UINT Reserved2[4];
    UINT OpenAs;
    UINT Reserved3;
};

static_assert(sizeof(RxSharedResourceRecord12) == 0x68);

struct RxSharedRecord12
{
    RxSharedTextureRecord Api11;
    RxSharedHeapRecord12 Heap;
    RxSharedResourceRecord12 Resource;
};

static_assert(sizeof(RxSharedRecord12) == 0x118);

/* How the heap part says the allocation was shared */
constexpr UINT RxShareNtHandle12 = 0x2;
constexpr UINT RxShareGlobalHandle12 = 0x4;
constexpr UINT RxShareKeyedMutex12 = 0x8;
constexpr UINT RxShareCrossAdapter12 = 0x10;
constexpr UINT RxShareCommitted12 = 0x100;
constexpr UINT RxShareHasResource12 = 0x10000000;

/* The resource part names a heap when the handle opens as an ID3D12Heap */
constexpr UINT RxShareOpenAsHeap12 = 0x2;

/* The record constant Direct3D 12 sets when the allocation is shared across adapters */
constexpr UINT RxSharedRecordCrossAdapterIndex = 7;

/* Gathers every part an opener knows from runtime data; parts that are missing stay zero */
inline
VOID
RxReadSharedRecord12(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ SIZE_T Size,
    _Out_ RxSharedRecord12* Record)
{
    ZeroMemory(Record, sizeof(*Record));

    while (Size >= sizeof(RxSharedPart))
    {
        RxSharedPart Part;
        SIZE_T Take = 0;
        VOID* Target = nullptr;

        memcpy(&Part, Data, sizeof(Part));
        if (Part.Size < sizeof(Part) || Part.Size > Size)
            break;

        if (Part.Version == RxSharedRecordVersion && Part.Size >= sizeof(Record->Api11))
        {
            Target = &Record->Api11;
            Take = sizeof(Record->Api11);
        }
        else if (Part.Version == RxSharedPartVersion12 && Part.Size >= 3 * sizeof(UINT))
        {
            UINT Kind;

            memcpy(&Kind, Data + 2 * sizeof(UINT), sizeof(Kind));
            if (Kind == RxSharedHeapPart12 && Part.Size >= sizeof(Record->Heap))
            {
                Target = &Record->Heap;
                Take = sizeof(Record->Heap);
            }
            else
            {
                Target = &Record->Resource;
                Take = Part.Size < sizeof(Record->Resource) ? Part.Size : sizeof(Record->Resource);
            }
        }
        else if (Part.Size >= RxSharedBasicRecordSize)
        {
            /* Anything else only lends its texture size and format, as an unversioned record */
            memcpy(&Record->Api11, Data, RxSharedBasicRecordSize);
            Record->Api11.Size = RxSharedBasicRecordSize;
            Record->Api11.Version = 0;
        }

        if (Target != nullptr)
        {
            memcpy(Target, Data, Take);
            static_cast<RxSharedPart*>(Target)->Size = (UINT)Take;
        }

        Data += Part.Size;
        Size -= Part.Size;
    }
}
