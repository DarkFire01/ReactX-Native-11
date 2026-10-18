/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Bit reader over an RBSP with Exp-Golomb support
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>

#include <vector>

/* Copies a NAL payload with the 0x000003 emulation prevention bytes removed */
inline
std::vector<UINT8>
NvdUnescapeRbsp(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size)
{
    std::vector<UINT8> Rbsp;
    SIZE_T Zeros = 0;
    SIZE_T Index;

    Rbsp.reserve(Size);
    for (Index = 0; Index < Size; Index++)
    {
        if (Zeros >= 2 && Data[Index] == 3)
        {
            Zeros = 0;
            continue;
        }

        Zeros = (Data[Index] == 0) ? Zeros + 1 : 0;
        Rbsp.push_back(Data[Index]);
    }

    return Rbsp;
}

class BitReader
{
public:
    BitReader(
        _In_reads_bytes_(Size) const UINT8* Data,
        _In_ SIZE_T Size)
        : Data(Data),
          SizeInBits(Size * 8)
    {
    }

    UINT32
    ReadBits(
        _In_ UINT Count)
    {
        UINT32 Value = 0;

        while (Count-- > 0)
            Value = (Value << 1) | ReadBit();

        return Value;
    }

    UINT32
    ReadBit()
    {
        UINT32 Bit;

        if (Position >= SizeInBits)
        {
            IsOverrun = TRUE;
            return 0;
        }

        Bit = (Data[Position >> 3] >> (7 - (Position & 7))) & 1;
        Position++;
        return Bit;
    }

    BOOLEAN
    ReadFlag()
    {
        return ReadBit() != 0;
    }

    UINT32
    ReadUe()
    {
        UINT LeadingZeros = 0;

        while (ReadBit() == 0)
        {
            if (IsOverrun || ++LeadingZeros > 31)
            {
                IsOverrun = TRUE;
                return 0;
            }
        }

        return ((1u << LeadingZeros) - 1) + ReadBits(LeadingZeros);
    }

    INT32
    ReadSe()
    {
        UINT32 Code = ReadUe();

        if (Code & 1)
            return (INT32)((Code + 1) / 2);

        return -(INT32)(Code / 2);
    }

    VOID
    SkipBits(
        _In_ SIZE_T Count)
    {
        Position += Count;
        if (Position > SizeInBits)
        {
            Position = SizeInBits;
            IsOverrun = TRUE;
        }
    }

    /* True while payload bits remain before the rbsp_stop_one_bit */
    BOOLEAN
    HasMoreRbspData() const
    {
        SIZE_T Last = SizeInBits;

        while (Last > Position)
        {
            Last--;
            if ((Data[Last >> 3] >> (7 - (Last & 7))) & 1)
                return Last > Position;
        }

        return FALSE;
    }

    BOOLEAN
    HasFailed() const
    {
        return IsOverrun;
    }

    SIZE_T
    GetPosition() const
    {
        return Position;
    }

private:
    const UINT8* Data;
    SIZE_T SizeInBits;
    SIZE_T Position = 0;
    BOOLEAN IsOverrun = FALSE;
};
