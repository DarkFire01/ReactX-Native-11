/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The checksum a DXBC container carries after its magic
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "rxcommon.hpp"
#include "dxbchash.hpp"

/*
 * MD5 rounds over the data, but the tail is laid out differently from RFC 1321: the final
 * block starts with the bit count and ends with (byte count * 2) | 1 in place of the high length word.
 */

static const UINT RxShifts[64] =
{
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static const UINT RxSines[64] =
{
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static
VOID
RxMd5Block(
    _Inout_updates_(4) UINT* State,
    _In_reads_(16) const UINT* Words)
{
    UINT A = State[0];
    UINT B = State[1];
    UINT C = State[2];
    UINT D = State[3];
    UINT Step;

    for (Step = 0; Step < 64; Step++)
    {
        UINT Mix;
        UINT Word;
        UINT Next;

        if (Step < 16)
        {
            Mix = (B & C) | (~B & D);
            Word = Step;
        }
        else if (Step < 32)
        {
            Mix = (D & B) | (~D & C);
            Word = (5 * Step + 1) % 16;
        }
        else if (Step < 48)
        {
            Mix = B ^ C ^ D;
            Word = (3 * Step + 5) % 16;
        }
        else
        {
            Mix = C ^ (B | ~D);
            Word = (7 * Step) % 16;
        }

        Next = A + Mix + RxSines[Step] + Words[Word];
        A = D;
        D = C;
        C = B;
        B += (Next << RxShifts[Step]) | (Next >> (32 - RxShifts[Step]));
    }

    State[0] += A;
    State[1] += B;
    State[2] += C;
    State[3] += D;
}

VOID
RxDxbcHash(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ UINT Size,
    _Out_writes_bytes_all_(16) BYTE* Digest)
{
    UINT State[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    UINT Words[16];
    UINT Whole = Size & ~63u;
    UINT Tail = Size - Whole;
    UINT Offset;

    for (Offset = 0; Offset < Whole; Offset += 64)
    {
        CopyMemory(Words, Data + Offset, sizeof(Words));
        RxMd5Block(State, Words);
    }

    /* A short tail shares the last block with the lengths; a long one gets its own padded block */
    ZeroMemory(Words, sizeof(Words));
    if (Tail < 56)
    {
        Words[0] = Size << 3;
        CopyMemory((BYTE*)Words + 4, Data + Whole, Tail);
        ((BYTE*)Words)[4 + Tail] = 0x80;
    }
    else
    {
        CopyMemory(Words, Data + Whole, Tail);
        ((BYTE*)Words)[Tail] = 0x80;
        RxMd5Block(State, Words);
        ZeroMemory(Words, sizeof(Words));
        Words[0] = Size << 3;
    }
    Words[15] = (Size << 1) | 1;
    RxMd5Block(State, Words);

    CopyMemory(Digest, State, sizeof(State));
}
