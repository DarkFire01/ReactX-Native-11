/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CPU copies through parameterized swizzle patterns
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "swizzle12.hpp"

/* Columns of one row worked out at a time; a multiple of the largest run */
constexpr UINT RxSwizzleColumns = 2048;
constexpr UINT RxSwizzleMaxRunLog2 = 8;

static
VOID
RxSetBitEntry(
    _Out_ D3D12DDI_SWIZZLE_BIT_ENTRY* Entry,
    _In_ UINT Channel,
    _In_ UINT Source)
{
    Entry->Valid = 1;
    Entry->ChannelIndex = Channel;
    Entry->SourceBitIndex = Source;
}

VOID
RxLinearSwizzlePattern12(
    _In_ UINT ElementLog2,
    _Out_ D3D12DDI_SWIZZLE_PATTERN_DESC_0022* Pattern)
{
    UINT Bit;

    ZeroMemory(Pattern, sizeof(*Pattern));
    for (Bit = 0; Bit < ElementLog2 && Bit < 32; Bit++)
        RxSetBitEntry(&Pattern->InterleavePatternSourceBits[Bit], 0, Bit);
}

VOID
RxStandardSwizzlePattern12(
    _In_ BOOLEAN Volume,
    _In_ UINT ElementLog2,
    _Out_ D3D12DDI_SWIZZLE_PATTERN_DESC_0022* Pattern)
{
    /* Address bits from lowest to highest, by the coordinate whose next bit fills each */
    static const char* const Planes[5] =
    {
        "XXXXYYYYYXYXYXYX", "XXXXYYYXYXYXYXYX", "XXXXYYYXYXYXYXYX", "XXXXYYXXYXYXYXYX", "XXXXYYXXYXYXYXYX"
    };
    static const char* const Volumes[5] =
    {
        "XXXXYYZZZYZYXZYX", "XXXXYYZZZYZYXZYX", "XXXXYYZZYXZYXZYX", "XXXXYYZZXXZYXZYX", "XXXXYYZZXXZYXZYX"
    };
    UINT Next[3] = {};
    const char* Order;
    UINT Bit;

    ZeroMemory(Pattern, sizeof(*Pattern));
    if (ElementLog2 >= _countof(Planes))
        return;

    Order = Volume ? Volumes[ElementLog2] : Planes[ElementLog2];
    for (Bit = 0; Order[Bit] != '\0'; Bit++)
    {
        UINT Channel = (UINT)(Order[Bit] - 'X');

        RxSetBitEntry(&Pattern->InterleavePatternSourceBits[Bit], Channel, Next[Channel]++);
    }
}

static
BOOLEAN
RxEntryUnused(
    _In_ const D3D12DDI_SWIZZLE_BIT_ENTRY& Entry)
{
    return !Entry.Valid && Entry.ChannelIndex == 0;
}

static
VOID
RxAddFlip(
    _Inout_updates_(3) UINT32 (*Flips)[32],
    _In_ const D3D12DDI_SWIZZLE_BIT_ENTRY& Entry,
    _In_ UINT Bit)
{
    /* Sample bits have no coordinate in CPU copies */
    if (Entry.ChannelIndex < 3)
        Flips[Entry.ChannelIndex][Entry.SourceBitIndex] ^= 1u << Bit;
}

HRESULT
RxPrepareSwizzle12(
    _In_ const D3D12DDI_SWIZZLE_PATTERN_DESC_0022& Pattern,
    _In_ UINT ElementLog2,
    _In_reads_(3) const UINT* Origin,
    _Out_ RxSwizzle12* Swizzle)
{
    const D3D12DDI_SWIZZLE_BIT_ENTRY* Xors[4] =
    {
        Pattern.InterleavePatternXORSourceBits, Pattern.InterleavePatternXOR2SourceBits,
        Pattern.InterleavePatternXOR3SourceBits, Pattern.InterleavePatternXOR4SourceBits
    };
    D3D12DDI_SWIZZLE_BIT_ENTRY Late[2][32];
    UINT Extent[4] = {};
    UINT Count = 0;
    UINT Bit;
    UINT List;
    UINT Run;

    ZeroMemory(Swizzle, sizeof(*Swizzle));

    /* An element's own bytes lead the address in order */
    for (Bit = 0; Bit < ElementLog2; Bit++)
    {
        const D3D12DDI_SWIZZLE_BIT_ENTRY& Entry = Pattern.InterleavePatternSourceBits[Bit];

        if (Bit >= 32 || !Entry.Valid || Entry.ChannelIndex != 0 || Entry.SourceBitIndex != Bit)
            return E_INVALIDARG;
    }

    /* The tile spans the leading valid bits; nothing may follow them */
    while (Count < 32 && Pattern.InterleavePatternSourceBits[Count].Valid)
    {
        const D3D12DDI_SWIZZLE_BIT_ENTRY& Entry = Pattern.InterleavePatternSourceBits[Count];

        if (Extent[Entry.ChannelIndex] < Entry.SourceBitIndex + 1u)
            Extent[Entry.ChannelIndex] = Entry.SourceBitIndex + 1u;
        Count++;
    }
    for (Bit = Count; Bit < 32; Bit++)
    {
        if (!RxEntryUnused(Pattern.InterleavePatternSourceBits[Bit]))
            return E_INVALIDARG;
    }

    Swizzle->StackSlices = (Pattern.Flags & D3D12DDI_SWIZZLE_PATTERN_FLAGS_STACK_DEPTH_SLICES) != 0;
    if (Swizzle->StackSlices && Extent[2] != 0)
        return E_INVALIDARG;

    for (List = 0; List < _countof(Xors); List++)
    {
        for (Bit = 0; Bit < 32; Bit++)
        {
            if (Xors[List][Bit].Valid ? Bit >= Count : Xors[List][Bit].ChannelIndex != 0)
                return E_INVALIDARG;
        }
    }

    if ((Pattern.PostambleXORImmediate & (0xFFFFFFFFu << (Count & 31))) != 0)
        return E_INVALIDARG;

    /* The postamble only rewrites address bits 8 through 23 */
    CopyMemory(Late[0], Pattern.PostambleXORSourceBits, sizeof(Late[0]));
    CopyMemory(Late[1], Pattern.PostambleXOR2SourceBits, sizeof(Late[1]));
    for (List = 0; List < _countof(Late); List++)
    {
        for (Bit = 0; Bit < 32; Bit++)
        {
            if (Bit >= 8 && Bit < 24 ? (!Late[List][Bit].Valid && Late[List][Bit].ChannelIndex != 0)
                                     : !RxEntryUnused(Late[List][Bit]))
            {
                return E_INVALIDARG;
            }
        }
    }

    if (Swizzle->StackSlices && Origin[2] != 0)
        return E_INVALIDARG;

    /* Conditional postambles take the row byte offset as a mask of the XORs in effect, with no offsets */
    if ((Pattern.Flags & D3D12DDI_SWIZZLE_PATTERN_FLAGS_0022_CONDITIONAL_POSTAMBLE_XORS) != 0)
    {
        for (Bit = 8; Bit < 24; Bit++)
        {
            if ((Origin[0] & (1u << (Bit - 8))) == 0)
            {
                ZeroMemory(&Late[0][Bit], sizeof(Late[0][Bit]));
                ZeroMemory(&Late[1][Bit], sizeof(Late[1][Bit]));
            }
        }
    }
    else
    {
        Swizzle->Origin[0] = Origin[0];
        Swizzle->Origin[1] = Origin[1];
        Swizzle->Origin[2] = Origin[2];
        ZeroMemory(Late, sizeof(Late));
    }

    for (Bit = 0; Bit < Count; Bit++)
    {
        RxAddFlip(Swizzle->TileFlips, Pattern.InterleavePatternSourceBits[Bit], Bit);
        for (List = 0; List < _countof(Xors); List++)
        {
            if (Xors[List][Bit].Valid)
                RxAddFlip(Swizzle->TileFlips, Xors[List][Bit], Bit);
        }
    }
    for (Bit = 8; Bit < 24; Bit++)
    {
        for (List = 0; List < _countof(Late); List++)
        {
            if (Late[List][Bit].Valid)
                RxAddFlip(Swizzle->LateFlips, Late[List][Bit], Bit);
        }
    }
    for (List = 0; List < 3 * 32; List++)
    {
        if (Swizzle->LateFlips[List / 32][List % 32] != 0)
            Swizzle->HasLate = TRUE;
    }

    Swizzle->Immediate = Pattern.PostambleXORImmediate;
    Swizzle->TileLog2 = Count;
    Swizzle->Shift[0] = Extent[0];
    Swizzle->Shift[1] = Extent[1];
    Swizzle->Shift[2] = Extent[2];

    /* Low bits copied straight from the byte column, which nothing else reads or flips */
    for (Run = 0; Run < Count && Run < (Extent[0] & 31) && Run < RxSwizzleMaxRunLog2; Run++)
    {
        UINT Channel;
        UINT Source;
        BOOLEAN Clean = Swizzle->TileFlips[0][Run] == (1u << Run) && Swizzle->LateFlips[0][Run] == 0 &&
                        (Swizzle->Immediate & (1u << Run)) == 0;

        for (Channel = 0; Channel < 3 && Clean; Channel++)
        {
            for (Source = 0; Source < 32; Source++)
            {
                if ((Channel != 0 || Source != Run) && (Swizzle->TileFlips[Channel][Source] & (1u << Run)) != 0)
                    Clean = FALSE;
            }
        }
        if (!Clean)
            break;
    }
    Swizzle->RunLog2 = Run;

    return S_OK;
}

/* Address bits a coordinate flips through one table */
static
UINT32
RxFlipsOf(
    _In_reads_(32) const UINT32* Flips,
    _In_ UINT Coordinate)
{
    UINT32 Result = 0;
    DWORD Bit;

    while (_BitScanForward(&Bit, Coordinate))
    {
        Result ^= Flips[Bit];
        Coordinate &= Coordinate - 1;
    }
    return Result;
}

VOID
RxSwizzleCopy12(
    _In_ const RxSwizzle12& Swizzle,
    _In_ const RxSwizzleRegion12& Region,
    _Inout_ BYTE* Linear,
    _In_ UINT LinearPitch,
    _In_ UINT LinearDepthPitch,
    _In_ BOOLEAN ToSwizzled)
{
    UINT32 Inside[RxSwizzleColumns];
    UINT32 Outside[RxSwizzleColumns];
    UINT32 After[RxSwizzleColumns];
    UINT32 Below[32];
    UINT32 LateBelow[32];
    const UINT Run = 1u << Swizzle.RunLog2;
    const UINT32 TileBytes = 1u << (Swizzle.TileLog2 & 31);
    UINT Column = 0;
    UINT Bit;

    /* Running XORs of the column's low bits, so each next column costs one lookup */
    Below[0] = Swizzle.TileFlips[0][0];
    LateBelow[0] = Swizzle.LateFlips[0][0];
    for (Bit = 1; Bit < 32; Bit++)
    {
        Below[Bit] = Below[Bit - 1] ^ Swizzle.TileFlips[0][Bit];
        LateBelow[Bit] = LateBelow[Bit - 1] ^ Swizzle.LateFlips[0][Bit];
    }

    while (Column < Region.Size[0])
    {
        UINT First = Region.Start[0] + Column + Swizzle.Origin[0];
        UINT Width = RxSwizzleColumns - (First & (RxSwizzleColumns - 1));
        UINT32 In = RxFlipsOf(Swizzle.TileFlips[0], First);
        UINT32 Late = RxFlipsOf(Swizzle.LateFlips[0], First);
        UINT Index;
        UINT Slice;

        if (Width > Region.Size[0] - Column)
            Width = Region.Size[0] - Column;

        for (Index = 0; Index < Width; Index++)
        {
            UINT X = First + Index;
            DWORD Low;

            Inside[Index] = In;
            After[Index] = Late;
            Outside[Index] = (X >> (Swizzle.Shift[0] & 31)) * TileBytes;
            if (_BitScanForward(&Low, X + 1))
            {
                In ^= Below[Low];
                Late ^= LateBelow[Low];
            }
            else
            {
                In = 0;
                Late = 0;
            }
        }

        for (Slice = 0; Slice < Region.Size[2]; Slice++)
        {
            UINT Row;

            for (Row = 0; Row < Region.Size[1]; Row++)
            {
                UINT Y = Region.Start[1] + Row;
                UINT Z = Region.Start[2] + Slice;
                BYTE* Line = Linear + (SIZE_T)Slice * LinearDepthPitch + (SIZE_T)Row * LinearPitch + Column;
                UINT32 RowIn;
                UINT32 RowLate;
                UINT32 RowOffset;

                /* Stacked slices continue down the rows of the first one */
                if (Swizzle.StackSlices)
                {
                    Y += Z * Region.DepthPitch;
                    Z = 0;
                }
                Y += Swizzle.Origin[1];
                Z += Swizzle.Origin[2];

                RowIn = RxFlipsOf(Swizzle.TileFlips[1], Y) ^ RxFlipsOf(Swizzle.TileFlips[2], Z) ^ Swizzle.Immediate;
                RowLate = RxFlipsOf(Swizzle.LateFlips[1], Y) ^ RxFlipsOf(Swizzle.LateFlips[2], Z);
                RowOffset = (Y >> (Swizzle.Shift[1] & 31)) * Region.RowPitch +
                            (Z >> (Swizzle.Shift[2] & 31)) * Region.DepthPitch;

                Index = 0;
                while (Index < Width)
                {
                    UINT32 Sum = (Inside[Index] ^ RowIn) + Outside[Index] + RowOffset;
                    UINT32 Flip = After[Index] ^ RowLate;
                    BYTE* Texel = Region.Base + (Sum ^ Flip);

                    /* A run stays contiguous unless the sum wraps or carries into flipped bits */
                    if (Run > 1 && ((First + Index) & (Run - 1)) == 0 && Width - Index >= Run &&
                        (Flip == 0 ? Sum <= 0xFFFFFFFFu - (Run - 1) : (Sum & (Run - 1)) == 0))
                    {
                        if (ToSwizzled)
                            CopyMemory(Texel, Line + Index, Run);
                        else
                            CopyMemory(Line + Index, Texel, Run);
                        Index += Run;
                        continue;
                    }

                    if (ToSwizzled)
                        *Texel = Line[Index];
                    else
                        Line[Index] = *Texel;
                    Index++;
                }
            }
        }

        Column += Width;
    }
}
