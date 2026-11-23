/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Container layout checks, part lookup, digests and the output writer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxilconv.hpp"

#define DXC_CONTAINER_MAGIC DXC_FOURCC('D', 'X', 'B', 'C')
#define DXC_HEADER_SIZE 32

static const ULONG DxcRoundShift[64] =
{
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static const ULONG DxcRoundAdd[64] =
{
    0xD76AA478, 0xE8C7B756, 0x242070DB, 0xC1BDCEEE, 0xF57C0FAF, 0x4787C62A, 0xA8304613, 0xFD469501,
    0x698098D8, 0x8B44F7AF, 0xFFFF5BB1, 0x895CD7BE, 0x6B901122, 0xFD987193, 0xA679438E, 0x49B40821,
    0xF61E2562, 0xC040B340, 0x265E5A51, 0xE9B6C7AA, 0xD62F105D, 0x02441453, 0xD8A1E681, 0xE7D3FBC8,
    0x21E1CDE6, 0xC33707D6, 0xF4D50D87, 0x455A14ED, 0xA9E3E905, 0xFCEFA3F8, 0x676F02D9, 0x8D2A4C8A,
    0xFFFA3942, 0x8771F681, 0x6D9D6122, 0xFDE5380C, 0xA4BEEA44, 0x4BDECFA9, 0xF6BB4B60, 0xBEBFBC70,
    0x289B7EC6, 0xEAA127FA, 0xD4EF3085, 0x04881D05, 0xD9D4D039, 0xE6DB99E5, 0x1FA27CF8, 0xC4AC5665,
    0xF4292244, 0x432AFF97, 0xAB9423A7, 0xFC93A039, 0x655B59C3, 0x8F0CCC92, 0xFFEFF47D, 0x85845DD1,
    0x6FA87E4F, 0xFE2CE6E0, 0xA3014314, 0x4E0811A1, 0xF7537E82, 0xBD3AF235, 0x2AD7D2BB, 0xEB86D391,
};

static
ULONG
DxcRotate(
    _In_ ULONG Value,
    _In_ ULONG Count)
{
    return (Value << Count) | (Value >> (32 - Count));
}

/* One 64 byte block of the MD5 compression function */
static
VOID
DxcDigestBlock(
    _Inout_updates_(4) ULONG* State,
    _In_reads_bytes_(64) const BYTE* Block)
{
    ULONG Words[16];
    ULONG A = State[0];
    ULONG B = State[1];
    ULONG C = State[2];
    ULONG D = State[3];

    memcpy(Words, Block, sizeof(Words));

    for (ULONG Step = 0; Step < 64; Step++)
    {
        ULONG Mix;
        ULONG Pick;
        ULONG Next;

        if (Step < 16)
        {
            Mix = (B & C) | (~B & D);
            Pick = Step;
        }
        else if (Step < 32)
        {
            Mix = (D & B) | (~D & C);
            Pick = (5 * Step + 1) & 15;
        }
        else if (Step < 48)
        {
            Mix = B ^ C ^ D;
            Pick = (3 * Step + 5) & 15;
        }
        else
        {
            Mix = C ^ (B | ~D);
            Pick = (7 * Step) & 15;
        }

        Next = B + DxcRotate(A + Mix + DxcRoundAdd[Step] + Words[Pick], DxcRoundShift[Step]);
        A = D;
        D = C;
        C = B;
        B = Next;
    }

    State[0] += A;
    State[1] += B;
    State[2] += C;
    State[3] += D;
}

/**
 * The container digest: MD5 blocks over the data, then a closing block that carries
 * the length in its first and last words instead of the usual MD5 trailer.
 */
VOID
DxcComputeDigest(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ ULONG Size,
    _In_ DXC_DIGEST_KIND Kind,
    _Out_writes_bytes_(16) BYTE* Digest)
{
    ULONG State[4] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476 };
    ULONG FullBlocks = Size / 64;
    ULONG Tail = Size % 64;
    ULONG LeadWord;
    ULONG LastWord;
    BYTE Block[64];

    if (Kind == DxcDigestDebug)
    {
        LeadWord = (Size << 4) | 0xF;
        LastWord = (Size | 0x4000000) << 2;
    }
    else
    {
        LeadWord = Size << 3;
        LastWord = (Size << 1) | 1;
    }

    for (ULONG Index = 0; Index < FullBlocks; Index++)
        DxcDigestBlock(State, Data + Index * 64);

    Data += FullBlocks * 64;
    memset(Block, 0, sizeof(Block));

    if (Tail < 56)
    {
        memcpy(Block, &LeadWord, sizeof(LeadWord));
        memcpy(Block + 4, Data, Tail);
        Block[4 + Tail] = 0x80;
    }
    else
    {
        memcpy(Block, Data, Tail);
        Block[Tail] = 0x80;
        DxcDigestBlock(State, Block);

        memset(Block, 0, sizeof(Block));
        memcpy(Block, &LeadWord, sizeof(LeadWord));
    }

    memcpy(Block + 60, &LastWord, sizeof(LastWord));
    DxcDigestBlock(State, Block);

    memcpy(Digest, State, sizeof(State));
}

/** Returns which digest kind the stored digest was made with, or none when neither fits */
DXC_DIGEST_KIND
DxcMatchDigest(
    _In_reads_bytes_(Size) const BYTE* Container,
    _In_ ULONG Size)
{
    BYTE Digest[16];

    DxcComputeDigest(Container + 20, Size - 20, DxcDigestRetail, Digest);
    if (memcmp(Digest, Container + 4, sizeof(Digest)) == 0)
        return DxcDigestRetail;

    DxcComputeDigest(Container + 20, Size - 20, DxcDigestDebug, Digest);
    if (memcmp(Digest, Container + 4, sizeof(Digest)) == 0)
        return DxcDigestDebug;

    return DxcDigestNone;
}

static
ULONG
DxcReadUlong(
    _In_reads_bytes_(4) const BYTE* At)
{
    ULONG Value;

    memcpy(&Value, At, sizeof(Value));
    return Value;
}

/**
 * Accepts a version 1 container whose parts follow the offset table back to back
 * and end exactly at the declared size.
 */
HRESULT
DxcOpenContainer(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ ULONG Size,
    _Out_ DXC_CONTAINER* Container)
{
    ULONG Declared;
    ULONG Count;
    ULONGLONG Expected;

    Container->Base = nullptr;
    Container->Size = 0;
    Container->PartCount = 0;

    if (Size < DXC_HEADER_SIZE || DxcReadUlong(Data) != DXC_CONTAINER_MAGIC)
        return E_FAIL;

    if (*reinterpret_cast<const USHORT*>(Data + 20) != 1)
        return E_FAIL;

    Declared = DxcReadUlong(Data + 24);
    if (Declared > Size || Declared > 0x80000000)
        return E_FAIL;

    Count = DxcReadUlong(Data + 28);
    Expected = DXC_HEADER_SIZE + static_cast<ULONGLONG>(Count) * 4;
    if (Expected > Declared)
        return E_FAIL;

    for (ULONG Index = 0; Index < Count; Index++)
    {
        ULONG Offset = DxcReadUlong(Data + DXC_HEADER_SIZE + Index * 4);

        if (Offset > static_cast<ULONGLONG>(Declared) - 8 || Offset != Expected)
            return E_FAIL;

        Expected += 8 + static_cast<ULONGLONG>(DxcReadUlong(Data + Offset + 4));
        if (Expected > Declared)
            return E_FAIL;
    }

    if (Expected != Declared)
        return E_FAIL;

    Container->Base = Data;
    Container->Size = Size;
    Container->PartCount = Count;
    return S_OK;
}

/** Finds the first part with the given code */
BOOLEAN
DxcFindPart(
    _In_ const DXC_CONTAINER* Container,
    _In_ ULONG Code,
    _Out_ DXC_PART* Part)
{
    for (ULONG Index = 0; Index < Container->PartCount; Index++)
    {
        const BYTE* Header = Container->Base + DxcReadUlong(Container->Base + DXC_HEADER_SIZE + Index * 4);

        if (DxcReadUlong(Header) != Code)
            continue;

        Part->Code = Code;
        Part->Size = DxcReadUlong(Header + 4);
        Part->Data = Header + 8;
        return TRUE;
    }

    Part->Code = 0;
    Part->Data = nullptr;
    Part->Size = 0;
    return FALSE;
}

CDxcContainerWriter::CDxcContainerWriter()
    : m_Count(0)
{
}

CDxcContainerWriter::~CDxcContainerWriter()
{
    for (ULONG Index = 0; Index < m_Count; Index++)
        HeapFree(GetProcessHeap(), 0, m_Parts[Index].Data);
}

/** Keeps a copy of the payload; parts are written in the order they are added */
BOOLEAN
CDxcContainerWriter::AddPart(
    _In_ ULONG Code,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ ULONG Size)
{
    BYTE* Copy;

    if (m_Count == _countof(m_Parts))
        return FALSE;

    Copy = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, Size != 0 ? Size : 1));
    if (Copy == nullptr)
        return FALSE;

    if (Size != 0)
        memcpy(Copy, Data, Size);

    m_Parts[m_Count].Code = Code;
    m_Parts[m_Count].Data = Copy;
    m_Parts[m_Count].Size = Size;
    m_Count += 1;
    return TRUE;
}

ULONG
CDxcContainerWriter::TotalSize() const
{
    ULONG Total = DXC_HEADER_SIZE + m_Count * 4;

    for (ULONG Index = 0; Index < m_Count; Index++)
        Total += 8 + m_Parts[Index].Size;

    return Total;
}

/** Lays out the header, offset table and parts; the digest is left zero for the caller */
VOID
CDxcContainerWriter::Write(
    _Out_writes_bytes_(TotalSize()) BYTE* Out) const
{
    ULONG Magic = DXC_CONTAINER_MAGIC;
    ULONG Version = 1;
    ULONG Total = TotalSize();
    ULONG Offset = DXC_HEADER_SIZE + m_Count * 4;

    memcpy(Out, &Magic, sizeof(Magic));
    memset(Out + 4, 0, 16);
    memcpy(Out + 20, &Version, sizeof(Version));
    memcpy(Out + 24, &Total, sizeof(Total));
    memcpy(Out + 28, &m_Count, sizeof(m_Count));

    for (ULONG Index = 0; Index < m_Count; Index++)
    {
        memcpy(Out + DXC_HEADER_SIZE + Index * 4, &Offset, sizeof(Offset));
        memcpy(Out + Offset, &m_Parts[Index].Code, sizeof(ULONG));
        memcpy(Out + Offset + 4, &m_Parts[Index].Size, sizeof(ULONG));
        memcpy(Out + Offset + 8, m_Parts[Index].Data, m_Parts[Index].Size);
        Offset += 8 + m_Parts[Index].Size;
    }
}
