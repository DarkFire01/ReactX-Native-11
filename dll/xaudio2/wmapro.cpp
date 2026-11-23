/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     WMA Pro frames: tiling, channel transforms, masks and coefficients
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wmadec.hpp"
#include "wmaprocodes.hpp"
#include "wmaxform.hpp"

#include <math.h>

#pragma fp_contract(off)

#define XA_WMA_E_CORRUPT        static_cast<HRESULT>(0x80040002)
#define XA_WMA_E_STARVED        static_cast<HRESULT>(0x80040004)

/* Whole masks are sent as deltas from the band below, the first from a start the step divides */
#define XA_PRO_MASK_START       45
#define XA_PRO_MASK_BIAS        60

/* Symbols of the vector trees that switch to the next smaller vector */
#define XA_PRO_QUAD_ESCAPE      126
#define XA_PRO_PAIR_ESCAPE      136
#define XA_PRO_SINGLE_ESCAPE    100

/* Run level and mask run symbols below their tables */
#define XA_PRO_SYMBOL_ESCAPE    0
#define XA_PRO_SYMBOL_END       1
#define XA_PRO_SYMBOL_FIRST     2

/* Sum and difference weights of a fixed channel pair, and the scale of bands it skips */
#define XA_PRO_PAIR_WEIGHT      0x3F350000u
#define XA_PRO_PAIR_KEEP        0x3FB50000u

/* Two to the minus 30, which takes rotation matrices out of fixed point */
#define XA_PRO_FIXED_SCALE      0x30800000u

/* Group transforms */
#define XA_PRO_KIND_PAIR        1
#define XA_PRO_KIND_IDENTITY    2

/* Reads that run out of stream end the frame without touching anything after it */
#define XA_PRO_READ(Call)                   \
    do                                      \
    {                                       \
        if (!(Call))                        \
            return XA_WMA_E_STARVED;        \
    } while (0)

static
float
XaProFloat(
    _In_ UINT32 Bits)
{
    float Value;

    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static
UINT32
XaProLog2(
    _In_ UINT32 Value)
{
    UINT32 Result = 0;

    while ((Value >> (Result + 1)) != 0)
        Result += 1;
    return Result;
}

static
float
XaProQuantStep(
    _In_ INT32 Index)
{
    return static_cast<float>(pow(10.0, static_cast<double>(static_cast<float>(Index) * 0.05f)));
}

static
BOOLEAN
XaProBit(
    _Inout_ XaWmaDecoder* Decoder,
    _Out_ UINT32* Value)
{
    return XaWmaGetBits(Decoder, 1, Value);
}

/**
 * @brief Lays out the bands of every subframe size and how a mask moves between sizes.
 *
 * Edges are rounded to the nearest multiple of four. A band of one size takes the mask value of
 * the band of another size that holds its middle.
 */
VOID
XaWmaBuildProBands(
    _Inout_ XaWmaDecoder* Decoder)
{
    INT32 Rate = static_cast<INT32>(Decoder->Layout.Rate);

    for (UINT32 Level = 0; Level < Decoder->Levels; Level++)
    {
        INT32* Edges = Decoder->BandEdges[Level];
        INT32 Span = (2 * Decoder->FrameSize) >> Level;
        INT32 Next = 1;

        Edges[0] = 0;
        for (UINT32 Bark = 0; Bark < ARRAYSIZE(XaProBarkEdges); Bark++)
        {
            INT32 Exact = XaProBarkEdges[Bark] * Span / Rate + 2;
            INT32 Edge = Exact - Exact % 4;

            if (Edge > Edges[Next - 1])
            {
                Edges[Next] = Edge;
                Next += 1;
            }
            if (Edges[Next - 1] >= Span / 2)
                break;
        }
        Edges[Next - 1] = Span / 2;
        Decoder->BandCounts[Level] = Next - 1;
    }

    for (UINT32 To = 0; To < Decoder->Levels; To++)
    {
        const INT32* Edges = Decoder->BandEdges[To];

        for (INT32 Band = 0; Band < Decoder->BandCounts[To]; Band++)
        {
            INT32 Middle = ((Edges[Band + 1] + Edges[Band] - 1) << To) / 2;

            for (UINT32 From = 0; From < Decoder->Levels; From++)
            {
                const INT32* Other = Decoder->BandEdges[From];
                INT32 Match = 0;

                while ((Other[Match + 1] << From) < Middle)
                    Match += 1;
                Decoder->Resample[To][From][Band] = static_cast<UINT8>(Match);
            }
        }
    }
}

/* Reads a subframe size code; sizes halve from the whole frame */
static
HRESULT
XaProReadSize(
    _Inout_ XaWmaDecoder* Decoder,
    _Out_ INT32* Size)
{
    UINT32 Splits = Decoder->Pro.MaxSplits;
    UINT32 Code;

    if (Splits == 4 || Splits == 16)
    {
        XA_PRO_READ(XaProBit(Decoder, &Code));
        if (Code != 0)
        {
            UINT32 More;

            XA_PRO_READ(XaWmaGetBits(Decoder, Splits == 4 ? 1 : 2, &More));
            Code = More + 1;
        }
    }
    else
    {
        XA_PRO_READ(XaWmaGetBits(Decoder, XaProLog2(XaProLog2(Splits)) + 1, &Code));
    }
    if (Code > 30)
        return XA_WMA_E_CORRUPT;
    *Size = Decoder->FrameSize >> Code;
    return S_OK;
}

/**
 * @brief Reads how every channel of the frame is cut into subframes.
 *
 * Subframes are handed out in time order to the channels that are furthest behind; when they
 * differ, a bit per such channel says which of them take the next size.
 */
static
HRESULT
XaProReadTiling(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Channels = Decoder->Layout.Channels;
    INT32 Frame = Decoder->FrameSize;
    INT32 Smallest = Frame / static_cast<INT32>(Decoder->Pro.MaxSplits);
    INT32 Left = static_cast<INT32>(Channels) * Frame;

    for (UINT32 Index = 0; Index < Channels; Index++)
    {
        XaWmaTiling* Tiles = &Decoder->Channels[Index].Tiles;

        Tiles->Count = 0;
        Tiles->Covered = 0;
    }
    if (Decoder->Pro.MaxSplits == 1)
    {
        for (UINT32 Index = 0; Index < Channels; Index++)
        {
            Decoder->Channels[Index].Tiles.Sizes[1] = Frame;
            Decoder->Channels[Index].Tiles.Count = 1;
        }
        return S_OK;
    }

    while (Left > 0)
    {
        INT32 Behind = Frame;
        INT32 Waiting = 0;
        INT32 Size = Smallest;
        INT32 Before = Left;
        UINT64 Takers = MAXUINT64;
        BOOLEAN Everyone;

        if (Left == static_cast<INT32>(Channels) * Frame)
        {
            UINT32 Same;

            XA_PRO_READ(XaProBit(Decoder, &Same));
            Decoder->SameTiling = Same != 0;
        }
        for (UINT32 Index = 0; Index < Channels; Index++)
            Behind = XaMin(Behind, Decoder->Channels[Index].Tiles.Covered);

        if (Decoder->SameTiling)
        {
            Waiting = static_cast<INT32>(Channels);
            Everyone = TRUE;
        }
        else
        {
            for (UINT32 Index = 0; Index < Channels; Index++)
            {
                if (Decoder->Channels[Index].Tiles.Covered == Behind)
                    Waiting += 1;
            }
            Everyone = Waiting <= 1;
        }
        if (Waiting == 0)
            return XA_WMA_E_CORRUPT;
        if (Left / Waiting == Smallest)
            Everyone = TRUE;
        if (!Everyone)
        {
            UINT32 Bits;

            XA_PRO_READ(XaWmaGetBits(Decoder, static_cast<UINT32>(Waiting), &Bits));
            Takers = Bits;
        }
        if (Left / Waiting != Smallest)
        {
            HRESULT hr = XaProReadSize(Decoder, &Size);

            if (FAILED(hr))
                return hr;
        }
        if (Size < Smallest || Size > Frame || Size % 4 != 0)
            return XA_WMA_E_CORRUPT;

        for (UINT32 Index = 0; Index < Channels; Index++)
        {
            XaWmaTiling* Tiles = &Decoder->Channels[Index].Tiles;

            if (Tiles->Count > XA_WMA_PRO_SUBFRAMES)
                return XA_WMA_E_CORRUPT;
            if (Tiles->Covered != Behind)
                continue;
            Waiting -= 1;
            if (((Takers >> Waiting) & 1) == 0)
                continue;
            if (Tiles->Count >= XA_WMA_PRO_SUBFRAMES)
                return XA_WMA_E_CORRUPT;
            Left -= Size;
            Tiles->Sizes[1 + Tiles->Count] = Size;
            Tiles->Count += 1;
            Tiles->Covered += Size;
            if (Left < 0 || Tiles->Covered > Frame)
                return XA_WMA_E_CORRUPT;
        }
        if (Left >= Before)
            return XA_WMA_E_CORRUPT;
    }

    for (UINT32 Index = 0; Index < Channels; Index++)
        Decoder->Channels[Index].Tiles.Covered = 0;
    return S_OK;
}

/* Reads the frame's optional output transform; only its absence is supported */
static
HRESULT
XaProReadOutputTransform(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Present;

    if (Decoder->Layout.Channels < 2)
        return S_OK;
    XA_PRO_READ(XaProBit(Decoder, &Present));
    if (Present != 0)
    {
        XaTrace(L"WMA Pro output channel transforms are not supported");
        return XA_WMA_E_CORRUPT;
    }
    return S_OK;
}

static
HRESULT
XaProReadFrameHeader(
    _Inout_ XaWmaDecoder* Decoder)
{
    XaWmaPro* Pro = &Decoder->Pro;
    UINT32 Bit;
    HRESULT hr;

    if (Pro->FrameLength)
        XA_PRO_READ(XaWmaGetBits(Decoder, Pro->FrameLengthBits, &Decoder->FrameBits));
    hr = XaProReadTiling(Decoder);
    if (SUCCEEDED(hr))
        hr = XaProReadOutputTransform(Decoder);
    if (FAILED(hr))
        return hr;
    if (Pro->DrcByte)
        XA_PRO_READ(XaWmaSkipBits(Decoder, 8));

    Decoder->StartTrimmed = FALSE;
    XA_PRO_READ(XaProBit(Decoder, &Bit));
    if (Bit != 0)
    {
        UINT32 Value;

        XA_PRO_READ(XaProBit(Decoder, &Bit));
        if (Bit != 0)
        {
            XA_PRO_READ(XaWmaGetBits(Decoder, Pro->TrimBits, &Value));
            Decoder->StartTrimmed = TRUE;
            Decoder->StartTrim = static_cast<INT32>(Value);
        }
        XA_PRO_READ(XaProBit(Decoder, &Bit));
        if (Bit != 0)
        {
            XA_PRO_READ(XaWmaGetBits(Decoder, Pro->TrimBits, &Value));
            Decoder->EndTrim = static_cast<INT32>(Value);
        }
    }
    return S_OK;
}

/**
 * @brief Picks the channels of the next subframe and points them at their bands and output.
 *
 * These are the channels furthest behind whose next subframe has the size of the first of them.
 */
static
HRESULT
XaProEnterSubframe(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Channels = Decoder->Layout.Channels;
    INT32 Frame = Decoder->FrameSize;
    INT32 Behind = Frame;
    INT32 Wanted = Frame;
    INT32 Left = static_cast<INT32>(Channels) * Frame;

    for (UINT32 Index = 0; Index < Channels; Index++)
    {
        const XaWmaTiling* Tiles = &Decoder->Channels[Index].Tiles;

        if (Tiles->Covered < Behind)
        {
            Behind = Tiles->Covered;
            Wanted = Tiles->Sizes[1 + Tiles->Index];
        }
    }

    Decoder->PickedCount = 0;
    for (UINT32 Index = 0; Index < Channels; Index++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Index];
        XaWmaTiling* Tiles = &Channel->Tiles;

        Left -= Tiles->Covered;
        if (Tiles->Covered != Behind || Tiles->Sizes[1 + Tiles->Index] != Wanted)
            continue;
        Decoder->Picked[Decoder->PickedCount] = Index;
        Decoder->PickedCount += 1;
        Channel->PrevSize = Tiles->Sizes[Tiles->Index];
        Channel->Size = Tiles->Sizes[1 + Tiles->Index];
        Channel->NextSize = Tiles->Sizes[2 + Tiles->Index];
        Left -= Channel->Size;
        Tiles->Covered += Channel->Size;
    }
    if (Decoder->PickedCount == 0 || Left < 0)
        return XA_WMA_E_CORRUPT;
    Decoder->FrameDone = Left == 0;
    Decoder->CodedLimit = 1;

    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];
        UINT32 Level = 0;

        Channel->CodedLength = 1;
        if (Channel->Tiles.Count > 1)
            Level = XaProLog2(static_cast<UINT32>(Frame / Channel->Size));
        if (Level >= Decoder->Levels)
            return XA_WMA_E_CORRUPT;
        Decoder->Bands = Decoder->BandCounts[Level];
        Decoder->Edges = Decoder->BandEdges[Level];
        Decoder->LowIndex = 0;
        Decoder->HighIndex = Channel->Size;
        Channel->Width = Channel->Size;
        XaWmaSetTurn(Channel);

        if (Channel->Fill + Channel->Size > Frame)
            return XA_WMA_E_CORRUPT;
        Channel->Coefs = Channel->Pcm + Frame / 2 + Channel->Fill;
        Channel->Fill += Channel->Size;
    }

    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];

        if (Channel->Tiles.Index != 0)
            continue;

        /* Masks never carry over into a new frame */
        Channel->HasMask = FALSE;
        Channel->MaskStep = 1;
        Channel->MaskPeak = 0;
        memset(Channel->Mask, 0, sizeof(Channel->Mask));
        memset(Channel->Resampled, 0, sizeof(Channel->Resampled));
    }
    return S_OK;
}

/* Skips the extension a subframe may carry ahead of its contents */
static
HRESULT
XaProSkipExtension(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Present;
    UINT32 Bits;

    XA_PRO_READ(XaProBit(Decoder, &Present));
    if (Present == 0)
        return S_OK;
    XA_PRO_READ(XaWmaGetBits(Decoder, 2, &Bits));
    if (Bits == 0)
    {
        UINT32 Width;

        XA_PRO_READ(XaWmaGetBits(Decoder, 4, &Width));
        XA_PRO_READ(XaWmaGetBits(Decoder, Width, &Bits));
        Bits += 1;
    }
    XA_PRO_READ(XaWmaSkipBits(Decoder, Bits));
    return S_OK;
}

/* Reads the transform of one group of two or more channels */
static
HRESULT
XaProReadGroupKind(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaGroup* Group)
{
    UINT32 Bit;

    Group->Fixed = FALSE;
    if (Group->Count == 1)
    {
        Group->Fixed = TRUE;
        Group->Kind = XA_PRO_KIND_IDENTITY;
        Group->Matrix[0] = 1.0f;
        return S_OK;
    }

    XA_PRO_READ(XaProBit(Decoder, &Bit));
    if (Group->Count == 2)
    {
        if (Bit == 0)
        {
            Group->Fixed = TRUE;
            Group->Kind = XA_PRO_KIND_PAIR;
            Group->Matrix[0] = XaProFloat(XA_PRO_PAIR_WEIGHT);
            Group->Matrix[1] = -XaProFloat(XA_PRO_PAIR_WEIGHT);
            Group->Matrix[2] = XaProFloat(XA_PRO_PAIR_WEIGHT);
            Group->Matrix[3] = XaProFloat(XA_PRO_PAIR_WEIGHT);
            return S_OK;
        }
        XA_PRO_READ(XaProBit(Decoder, &Bit));
        if (Bit == 0)
        {
            Group->Fixed = TRUE;
            Group->Kind = XA_PRO_KIND_IDENTITY;
            Group->Matrix[0] = 1.0f;
            Group->Matrix[1] = 0.0f;
            Group->Matrix[2] = 0.0f;
            Group->Matrix[3] = 1.0f;
        }
        return S_OK;
    }

    /* Larger groups only come with more channels than the decoder takes */
    return XA_WMA_E_CORRUPT;
}

/* Reads which bands of a group go through its transform */
static
HRESULT
XaProReadGroupBands(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaGroup* Group)
{
    INT32 Bands = Decoder->Bands;
    UINT32 All;

    if (Bands < 0 || Bands > XA_WMA_PRO_BANDS)
        return XA_WMA_E_CORRUPT;
    if (Group->Count <= 1 || (Group->Fixed && Group->Kind == XA_PRO_KIND_IDENTITY))
    {
        for (INT32 Band = 0; Band < Bands; Band++)
            Group->BandOn[Band] = TRUE;
        return S_OK;
    }
    XA_PRO_READ(XaProBit(Decoder, &All));
    for (INT32 Band = 0; Band < Bands; Band++)
    {
        UINT32 On = 1;

        if (All == 0)
            XA_PRO_READ(XaProBit(Decoder, &On));
        Group->BandOn[Band] = On != 0;
    }
    return S_OK;
}

/**
 * @brief Reads how the subframe's channels are grouped and transformed.
 *
 * A first bit sends groups for the first transform pass; once every channel has a group, a bit
 * of zero starts the groups of the second pass.
 */
static
HRESULT
XaProReadGroups(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Channels = Decoder->Layout.Channels;
    INT32 Left = static_cast<INT32>(Decoder->PickedCount);
    UINT32 Early;

    Decoder->GroupCount = 0;
    for (UINT32 Index = 0; Index < Channels; Index++)
        Decoder->Channels[Index].Grouped = FALSE;
    XA_PRO_READ(XaProBit(Decoder, &Early));

    for (;;)
    {
        XaWmaGroup* Group;
        HRESULT hr;

        if (Left == 0)
        {
            if (Early == 0)
                return S_OK;
            return XA_WMA_E_CORRUPT;
        }
        if (Decoder->GroupCount >= static_cast<INT32>(Channels))
            return XA_WMA_E_CORRUPT;

        Group = &Decoder->Groups[Decoder->GroupCount];
        memset(Group, 0, sizeof(*Group));
        Group->Early = Early != 0;
        if (Left > 2)
        {
            for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
            {
                XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];
                UINT32 Member;

                if (Channel->Grouped)
                    continue;
                XA_PRO_READ(XaProBit(Decoder, &Member));
                if (Member != 0)
                {
                    Group->Members[Decoder->Picked[Pick]] = TRUE;
                    Channel->Grouped = TRUE;
                    Group->Count += 1;
                }
            }
        }
        else
        {
            for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
            {
                XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];

                Group->Members[Decoder->Picked[Pick]] = !Channel->Grouped;
                Channel->Grouped = TRUE;
            }
            Group->Count = Left;
        }
        if (Group->Count < 1)
            return XA_WMA_E_CORRUPT;

        hr = XaProReadGroupKind(Decoder, Group);
        if (FAILED(hr))
            return hr;
        if (!Group->Fixed)
        {
            UINT32 Value;

            for (INT32 Angle = 0; Angle < Group->Count * (Group->Count - 1) / 2; Angle++)
            {
                XA_PRO_READ(XaWmaGetBits(Decoder, 6, &Value));
                Group->Angles[Angle] = static_cast<INT8>(static_cast<INT32>(Value) - 32);
            }
            for (INT32 Sign = 0; Sign < Group->Count; Sign++)
            {
                XA_PRO_READ(XaProBit(Decoder, &Value));
                Group->Signs[Sign] = static_cast<UINT8>(Value);
            }
        }
        hr = XaProReadGroupBands(Decoder, Group);
        if (FAILED(hr))
            return hr;
        Left -= Group->Count;
        Decoder->GroupCount += 1;

        if (Early != 0)
        {
            UINT32 Again;

            XA_PRO_READ(XaProBit(Decoder, &Again));
            if (Again == 0)
            {
                Early = 0;
                Left = static_cast<INT32>(Decoder->PickedCount);
                for (UINT32 Index = 0; Index < Channels; Index++)
                    Decoder->Channels[Index].Grouped = FALSE;
            }
        }
    }
}

/**
 * @brief Builds a group's matrix from its rotations, in fixed point as the encoder does.
 */
static
VOID
XaProBuildRotation(
    _Inout_ XaWmaGroup* Group)
{
    INT32 Count = Group->Count;
    INT32 Fixed[XA_WMA_MAX_CHANNELS * XA_WMA_MAX_CHANNELS] = {};
    INT32 Angle = 0;

    for (INT32 Index = 0; Index < Count; Index++)
        Fixed[Index * Count + Index] = Group->Signs[Index] != 0 ? 0x3FFFFFFF : -0x40000000;

    for (INT32 Order = 1; Order <= Count; Order++)
    {
        for (INT32 Row = 0; Row + 1 < Order; Row++)
        {
            INT32 Step = Group->Angles[Angle + Row] + 32;
            INT64 Sin = XaProTurnSin[Step];
            INT64 Cos = XaProTurnCos[Step];
            INT32 Upper[XA_WMA_MAX_CHANNELS];
            INT32 Lower[XA_WMA_MAX_CHANNELS];

            memcpy(Upper, &Fixed[Row * Count], sizeof(*Upper) * Order);
            memcpy(Lower, &Fixed[(Order - 1) * Count], sizeof(*Lower) * Order);
            for (INT32 Column = 0; Column < Order; Column++)
            {
                Fixed[Row * Count + Column] = static_cast<INT32>((Lower[Column] * Sin) >> 30) +
                                              static_cast<INT32>((Upper[Column] * Cos) >> 30);
                Fixed[(Order - 1) * Count + Column] = static_cast<INT32>((Upper[Column] * -Sin) >> 30) +
                                                      static_cast<INT32>((Lower[Column] * Cos) >> 30);
            }
        }
        Angle += Order - 1;
    }

    for (INT32 Index = 0; Index < Count * Count; Index++)
    {
        INT32 Value = Fixed[Index];

        Value = static_cast<INT32>((static_cast<UINT32>(Value) + (Value < 0 ? 0xFFFFFFFFu : 0) + 0x200000) & 0xFFC00000);
        Group->Matrix[Index] = static_cast<float>(Value) * XaProFloat(XA_PRO_FIXED_SCALE);
    }
}

/* Resamples the mask of the last subframe that sent one to the current subframe's bands */
static
VOID
XaProResampleMask(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    UINT32 From;
    UINT32 To;
    INT32 Peak;

    if (!Channel->HasMask)
        return;
    From = XaProLog2(static_cast<UINT32>(Decoder->FrameSize / Channel->MaskSize));
    To = XaProLog2(static_cast<UINT32>(Decoder->FrameSize / Channel->Size));
    for (INT32 Band = 0; Band < Decoder->Bands; Band++)
        Channel->Resampled[Band] = Channel->Mask[Decoder->Resample[To][From][Band]];
    Peak = Channel->Resampled[0];
    for (INT32 Band = 1; Band < Decoder->Bands; Band++)
        Peak = XaMax(Peak, Channel->Resampled[Band]);
    Channel->MaskPeak = Peak;
    Channel->MaskSent = FALSE;
}

static
HRESULT
XaProReadQuant(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Value;
    INT32 Delta;
    INT32 Sign;

    XA_PRO_READ(XaWmaGetBits(Decoder, 6, &Value));
    Sign = (Value & 0x20) != 0 ? -1 : 1;
    Delta = static_cast<INT32>(Value) | ((Value & 0x20) != 0 ? -64 : 0);
    Decoder->QuantIndex += Delta;
    if (Delta != -32 && Delta != 31)
        return S_OK;

    /* The largest steps carry on in five bit pieces */
    for (;;)
    {
        XA_PRO_READ(XaWmaGetBits(Decoder, 5, &Value));
        if (Value != 31)
        {
            Decoder->QuantIndex += Sign * static_cast<INT32>(Value);
            return S_OK;
        }
        Decoder->QuantIndex += 31 * Sign;
        if (Decoder->QuantIndex < 1)
        {
            Decoder->QuantIndex = 62;
            return XA_WMA_E_CORRUPT;
        }
    }
}

/* Reads each channel's quantizer offset from the subframe step */
static
HRESULT
XaProReadQuantShifts(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Width;

    if (Decoder->PickedCount == 1)
    {
        Decoder->Channels[Decoder->Picked[0]].QuantShift = 0;
        return S_OK;
    }
    XA_PRO_READ(XaWmaGetBits(Decoder, 3, &Width));
    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];
        UINT32 Present;
        UINT32 Shift = 0;

        XA_PRO_READ(XaProBit(Decoder, &Present));
        if (Present != 0)
        {
            Shift = 1;
            if (Width != 0)
            {
                XA_PRO_READ(XaWmaGetBits(Decoder, Width, &Shift));
                Shift = (Shift + 1) & 0xFF;
            }
        }
        Channel->QuantShift = Shift;
    }
    return S_OK;
}

/* Applies run level coded changes to the mask the subframe resampled */
static
HRESULT
XaProReadMaskChanges(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    INT32 Band = 0;

    while (Band < Decoder->Bands)
    {
        UINT32 Symbol;
        UINT32 After;
        INT32 Run;
        INT32 Level;
        BOOLEAN Negative;

        XA_PRO_READ(XaWmaGetSymbol(Decoder, XaProMaskRunTree, &Symbol, &After));
        if (Symbol == XA_PRO_SYMBOL_ESCAPE)
        {
            UINT32 Escape;

            XA_PRO_READ(XaWmaGetBits(Decoder, 14, &Escape));
            Level = static_cast<INT32>(Escape >> 6);
            Negative = (Escape & 1) == 0;
            Run = static_cast<INT32>((Escape >> 1) & 0x1F);
        }
        else if (Symbol == XA_PRO_SYMBOL_END)
        {
            Level = 0;
            Negative = FALSE;
            Run = static_cast<INT16>(Decoder->Bands - Band - 1);
        }
        else
        {
            XA_PRO_READ(XaWmaSkipBits(Decoder, 1));
            Run = XaProMaskRuns[Symbol - XA_PRO_SYMBOL_FIRST];
            Level = XaProMaskLevels[Symbol - XA_PRO_SYMBOL_FIRST];
            Negative = After == 0;
        }
        if (Band + Run >= Decoder->Bands)
            break;
        Band += Run;
        Channel->Mask[Band] += static_cast<INT16>(Negative ? -Level : Level);
        Band += 1;
    }
    return S_OK;
}

/**
 * @brief Reads a channel's band mask when the subframe sends one.
 *
 * The first subframe of a frame always sends it whole; later ones may send changes to the
 * resampled mask of the subframe before.
 */
static
HRESULT
XaProReadMask(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    INT32 Peak;

    if (!Channel->MaskRead)
    {
        UINT32 Sent = 1;

        if (Channel->Tiles.Index > 0)
            XA_PRO_READ(XaProBit(Decoder, &Sent));
        Channel->MaskSent = Sent != 0;
        if (Channel->MaskSent && !Channel->HasMask)
        {
            UINT32 Step;

            XA_PRO_READ(XaWmaGetBits(Decoder, 2, &Step));
            Channel->MaskStep = static_cast<INT32>(Step) + 1;
        }
        if (Channel->MaskSent && Channel->HasMask)
            memcpy(Channel->Mask, Channel->Resampled, sizeof(*Channel->Mask) * Decoder->Bands);
        Channel->MaskRead = TRUE;
    }
    if (!Channel->MaskSent)
        return S_OK;

    if (Channel->HasMask)
    {
        HRESULT hr = XaProReadMaskChanges(Decoder, Channel);

        if (FAILED(hr))
            return hr;
    }
    else
    {
        for (INT32 Band = 0; Band < Decoder->Bands; Band++)
        {
            UINT32 Symbol;
            UINT32 After;
            INT32 Base = Band != 0 ? Channel->Mask[Band - 1] : XA_PRO_MASK_START / Channel->MaskStep;

            XA_PRO_READ(XaWmaGetSymbol(Decoder, XaProMaskTree, &Symbol, &After));
            Channel->Mask[Band] = static_cast<INT32>(Symbol) - XA_PRO_MASK_BIAS + Base;
        }
    }

    Peak = Channel->Mask[0];
    for (INT32 Band = 1; Band < Decoder->Bands; Band++)
        Peak = XaMax(Peak, Channel->Mask[Band]);
    Channel->MaskPeak = Peak;
    Channel->HasMask = TRUE;
    Channel->MaskSize = Channel->Size;
    return S_OK;
}

/**
 * @brief Reads everything a subframe sends ahead of its coefficients.
 *
 * Returns S_FALSE when no channel of the subframe is coded.
 */
static
HRESULT
XaProReadSubframeHeader(
    _Inout_ XaWmaDecoder* Decoder)
{
    BOOLEAN Silent = TRUE;
    UINT32 Bit;
    HRESULT hr;

    hr = XaProEnterSubframe(Decoder);
    if (SUCCEEDED(hr))
        hr = XaProSkipExtension(Decoder);
    if (FAILED(hr))
        return hr;

    /* Lossless subframes are not supported */
    XA_PRO_READ(XaProBit(Decoder, &Bit));
    if (Bit != 0)
    {
        XaTrace(L"WMA Pro lossless subframes are not supported");
        return XA_WMA_E_CORRUPT;
    }

    hr = XaProReadGroups(Decoder);
    if (FAILED(hr))
        return hr;
    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];

        XA_PRO_READ(XaProBit(Decoder, &Bit));
        Channel->Coded = Bit != 0;
        XaProResampleMask(Decoder, Channel);
        if (Channel->Coded)
            Silent = FALSE;
    }
    Decoder->QuantIndex = Decoder->Pro.BaseQuant;
    if (Silent)
        return S_FALSE;

    /* An explicit count of coefficient vectors per channel */
    XA_PRO_READ(XaProBit(Decoder, &Bit));
    Decoder->Counted = Bit != 0;
    if (Decoder->Counted)
    {
        for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
        {
            XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];
            UINT32 Value;

            XA_PRO_READ(XaWmaGetBits(Decoder, XaProLog2(static_cast<UINT32>((Channel->Width + 3) >> 2)) + 1, &Value));
            Channel->Vectors = static_cast<INT32>(Value);
        }
    }

    hr = XaProReadQuant(Decoder);
    if (SUCCEEDED(hr))
        hr = XaProReadQuantShifts(Decoder);
    if (FAILED(hr))
        return hr;

    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];

        memset(Channel->Coefs, 0, sizeof(*Channel->Coefs) * Channel->Width);
        Channel->MaskRead = FALSE;
        Channel->MaskSent = FALSE;
    }
    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];

        hr = XaProReadMask(Decoder, Channel);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* Bits that hold a large magnitude: 8, 16, 24 or 31, told apart by a prefix */
static
HRESULT
XaProReadLarge(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ INT32 Base,
    _Out_ INT32* Value)
{
    UINT32 Bit;
    UINT32 Bits;

    XA_PRO_READ(XaProBit(Decoder, &Bit));
    if (Bit == 0)
    {
        XA_PRO_READ(XaWmaGetBits(Decoder, 8, &Bits));
    }
    else
    {
        XA_PRO_READ(XaProBit(Decoder, &Bit));
        if (Bit == 0)
        {
            XA_PRO_READ(XaWmaGetBits(Decoder, 16, &Bits));
        }
        else
        {
            XA_PRO_READ(XaProBit(Decoder, &Bit));
            XA_PRO_READ(XaWmaGetBits(Decoder, 24, &Bits));
            if (Bit != 0)
            {
                UINT32 Low;

                XA_PRO_READ(XaWmaGetBits(Decoder, 7, &Low));
                Bits = Low | (Bits << 7);
            }
        }
    }
    *Value = static_cast<INT32>(Bits) + Base;
    return S_OK;
}

/* A run too long for the run level table */
static
HRESULT
XaProReadLongRun(
    _Inout_ XaWmaDecoder* Decoder,
    _Out_ INT32* Run)
{
    UINT32 Bit;
    UINT32 Bits;

    XA_PRO_READ(XaProBit(Decoder, &Bit));
    if (Bit == 0)
    {
        *Run = 0;
        return S_OK;
    }
    XA_PRO_READ(XaProBit(Decoder, &Bit));
    if (Bit == 0)
    {
        XA_PRO_READ(XaWmaGetBits(Decoder, 2, &Bits));
        *Run = static_cast<INT32>(Bits) + 1;
        return S_OK;
    }
    XA_PRO_READ(XaProBit(Decoder, &Bit));
    if (Bit != 0)
        return XA_WMA_E_CORRUPT;
    XA_PRO_READ(XaWmaGetBits(Decoder, static_cast<UINT32>(Decoder->RunBitsPro), &Bits));
    *Run = static_cast<INT32>(Bits) + 4;
    return S_OK;
}

/* Run level coding that takes over from the vectors for the sparse top of the spectrum */
static
HRESULT
XaProNextRunLevel(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ const XaWmaChannel* Channel)
{
    XaWmaCoder* Coder = &Decoder->Coder;
    XaWmaRun* Run = &Decoder->RunLevel;
    UINT32 Symbol;
    UINT32 After;
    UINT32 Bit;

    XA_PRO_READ(XaWmaGetSymbol(Decoder, Channel->Book->Tree, &Symbol, &After));
    if (Symbol == XA_PRO_SYMBOL_ESCAPE)
    {
        INT32 Extra = 0;
        HRESULT hr;

        Run->Sign = 0;
        hr = XaProReadLarge(Decoder, 0, &Run->Level);
        if (SUCCEEDED(hr))
            hr = XaProReadLongRun(Decoder, &Extra);
        if (FAILED(hr))
            return hr;
        Run->Run = static_cast<INT16>(Coder->Zeros + Extra);
        Coder->Zeros = 0;
        XA_PRO_READ(XaProBit(Decoder, &Bit));
        Run->Sign = static_cast<INT32>(Bit) - 1;
        return S_OK;
    }
    if (Symbol == XA_PRO_SYMBOL_END)
    {
        Run->Level = 0;
        Run->Sign = 0;
        Run->Run = static_cast<INT16>(Channel->Width - Decoder->CoefIndex - 1);
        return S_OK;
    }
    XA_PRO_READ(XaWmaSkipBits(Decoder, 1));
    Run->Run = static_cast<INT16>(Channel->Book->Runs[Symbol - XA_PRO_SYMBOL_FIRST] + Coder->Zeros);
    Coder->Zeros = 0;
    Run->Level = Channel->Book->Levels[Symbol - XA_PRO_SYMBOL_FIRST];
    Run->Sign = After != 0 ? 0 : -1;
    return S_OK;
}

static const XaWmaCodeBook XaProBooks[2] =
{
    { XaProMidTree, XaProMidRuns, XaProMidLevels },
    { XaProSideTree, XaProSideRuns, XaProSideLevels },
};

/* Stores a magnitude of the vector being read; MoreStep is where the next one comes from */
static
VOID
XaProTakeValue(
    _Inout_ XaWmaCoder* Coder,
    _In_ INT32 Value,
    _In_ XaWmaCoderStep MoreStep)
{
    INT32 Slot = Coder->Slot;

    Coder->Values[Slot] = Value;
    Coder->Slot = Slot + 1;
    if (Slot == 1)
        Coder->Step = XaCoderPair;
    else if (Slot == 3)
        Coder->Step = XaCoderSigns;
    else
        Coder->Step = MoreStep;
}

/**
 * @brief Produces the next run and level of a channel's coefficients.
 *
 * Coefficients come four at a time, each group falling back to pairs, single values and escaped
 * large values. Once zeros get common, run level coding takes over for the rest of the subframe.
 */
static
HRESULT
XaProNextRun(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    XaWmaCoder* Coder = &Decoder->Coder;
    XaWmaRun* Run = &Decoder->RunLevel;
    INT32 Width = Channel->Width;
    INT32 Sparse = Width / 256;
    BOOLEAN Counted = Decoder->Counted;
    UINT32 Symbol;
    UINT32 After;

    for (;;)
    {
        switch (Coder->Step)
        {
            case XaCoderPickBook:
            {
                UINT32 Side;

                XA_PRO_READ(XaProBit(Decoder, &Side));
                Channel->Book = &XaProBooks[Side != 0 ? 1 : 0];
                Coder->Zeros = 0;
                Coder->Leave = FALSE;
                Coder->Vectors = 0;
                Coder->Slot = 0;
                memset(Coder->Signs, 0, sizeof(Coder->Signs));
                Coder->Step = XaCoderQuad;
                break;
            }

            case XaCoderQuad:
                Coder->Slot = 0;
                if (Counted && Coder->Vectors == Channel->Vectors)
                {
                    Coder->Step = XaCoderRunLevel;
                    break;
                }
                if (Decoder->CoefIndex + Coder->Zeros + 1 >= Width)
                {
                    Run->Level = 0;
                    Run->Sign = 0;
                    Run->Run = static_cast<INT16>(Coder->Zeros);
                    return S_OK;
                }
                XA_PRO_READ(XaWmaGetSymbol(Decoder, XaProQuadTree, &Symbol, &After));
                if (Symbol == XA_PRO_QUAD_ESCAPE)
                {
                    Coder->Step = XaCoderPair;
                }
                else
                {
                    UINT32 Packed = XaProQuads[Symbol];

                    Coder->Values[0] = static_cast<INT32>(Packed >> 12);
                    Coder->Values[1] = static_cast<INT32>((Packed >> 8) & 0xF);
                    Coder->Values[2] = static_cast<INT32>((Packed >> 4) & 0xF);
                    Coder->Values[3] = static_cast<INT32>(Packed & 0xF);
                    Coder->Step = XaCoderSigns;
                }
                Coder->Vectors += 1;
                break;

            case XaCoderPair:
                XA_PRO_READ(XaWmaGetSymbol(Decoder, XaProPairTree, &Symbol, &After));
                if (Symbol == XA_PRO_PAIR_ESCAPE)
                {
                    Coder->Step = XaCoderSingle;
                    break;
                }
                Coder->Values[Coder->Slot] = XaProPairs[Symbol] >> 4;
                Coder->Values[Coder->Slot + 1] = XaProPairs[Symbol] & 0xF;
                Coder->Slot += 2;
                if (Coder->Slot == 4)
                    Coder->Step = XaCoderSigns;
                break;

            case XaCoderSingle:
                XA_PRO_READ(XaWmaGetSymbol(Decoder, XaProSingleTree, &Symbol, &After));
                if (Symbol == XA_PRO_SINGLE_ESCAPE)
                {
                    Coder->Step = XaCoderLarge;
                    break;
                }
                XaProTakeValue(Coder, static_cast<INT32>(Symbol), XaCoderSingle);
                break;

            case XaCoderLarge:
            {
                INT32 Value;
                HRESULT hr = XaProReadLarge(Decoder, XA_PRO_SINGLE_ESCAPE, &Value);

                if (FAILED(hr))
                    return hr;
                XaProTakeValue(Coder, Value, XaCoderSingle);
                break;
            }

            case XaCoderSigns:
                for (INT32 Slot = 0; Slot < 4; Slot++)
                {
                    UINT32 Bit;

                    if (Coder->Values[Slot] == 0)
                        continue;
                    XA_PRO_READ(XaProBit(Decoder, &Bit));
                    Coder->Signs[Slot] = static_cast<INT32>(Bit);
                }
                Coder->Slot = 0;
                Coder->Step = XaCoderEmit;
                break;

            case XaCoderEmit:
            {
                INT32 Slot = Coder->Slot;

                Coder->Slot = Slot + 1;
                if (Coder->Values[Slot] == 0)
                {
                    INT32 Before = Coder->Zeros;

                    Coder->Zeros = Before + 1;
                    if (!Counted && Coder->Zeros > Sparse)
                        Coder->Leave = TRUE;
                    if (Decoder->CoefIndex + Before + 2 >= Width)
                    {
                        Run->Level = 0;
                        Run->Sign = 0;
                        Run->Run = static_cast<INT16>(Coder->Zeros);
                        return S_OK;
                    }
                    if (Slot == 3)
                        Coder->Step = Coder->Leave ? XaCoderRunLevel : XaCoderQuad;
                    break;
                }
                if (!Counted && Coder->Zeros > Sparse)
                    Coder->Leave = TRUE;
                Run->Run = static_cast<INT16>(Coder->Zeros);
                Coder->Zeros = 0;
                Run->Level = Coder->Values[Slot];
                Run->Sign = Coder->Signs[Slot] - 1;
                if (Slot == 3)
                    Coder->Step = Coder->Leave ? XaCoderRunLevel : XaCoderQuad;
                return S_OK;
            }

            case XaCoderRunLevel:
                return XaProNextRunLevel(Decoder, Channel);
        }
    }
}

/**
 * @brief Decodes a channel's quantized coefficients as signed whole numbers.
 *
 * CodedLength ends up one past the last coefficient that was sent.
 */
static
HRESULT
XaProReadCoefficients(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    XaWmaRun* Run = &Decoder->RunLevel;
    INT32 Last = Channel->Width - 1;
    INT32 Index = -1;
    HRESULT hr;

    Decoder->Coder.Step = XaCoderPickBook;
    hr = XaProNextRun(Decoder, Channel);
    while (SUCCEEDED(hr))
    {
        INT32 Target;

        if (static_cast<UINT32>(Run->Run) >= 0x8000 || static_cast<INT16>(Run->Run) == 0x7FFF)
        {
            hr = XA_WMA_E_CORRUPT;
            break;
        }
        Target = Index + static_cast<INT16>(Run->Run + 1);
        if (Target < -0x8000 || Target > 0x7FFF)
        {
            hr = XA_WMA_E_CORRUPT;
            break;
        }
        Index = Target;
        if (Index >= Last)
        {
            if (Index == Last)
                Channel->Coefs[Last] = Run->Sign != 0 ? -static_cast<float>(Run->Level) : static_cast<float>(Run->Level);
            if (Index > Channel->Width)
                hr = XA_WMA_E_CORRUPT;
            break;
        }
        Channel->Coefs[Index] = Run->Sign != 0 ? -static_cast<float>(Run->Level) : static_cast<float>(Run->Level);
        Decoder->CoefIndex = Index;
        hr = XaProNextRun(Decoder, Channel);
    }
    Decoder->CoefIndex = Index;
    Channel->CodedLength = static_cast<INT16>(Index == Last ? Last + 1 : Index - Run->Run);
    return hr;
}

/* Sum and difference of a fixed pair; bands it leaves out are only scaled */
static
VOID
XaProUndoPair(
    _In_ const XaWmaDecoder* Decoder,
    _In_ const XaWmaGroup* Group)
{
    float* First = Decoder->Channels[0].Coefs;
    float* Second = Decoder->Channels[1].Coefs;
    float Keep = XaProFloat(XA_PRO_PAIR_KEEP);

    for (INT32 Band = 0; Band < Decoder->Bands; Band++)
    {
        INT32 End = XaMin(Decoder->Edges[Band + 1], Decoder->CodedLimit);

        for (INT32 Index = Decoder->Edges[Band]; Index < End; Index++)
        {
            float A = First[Index];
            float B = Second[Index];

            if (Group->BandOn[Band])
            {
                First[Index] = A - B;
                Second[Index] = B + A;
            }
            else
            {
                First[Index] = A * Keep;
                Second[Index] = B * Keep;
            }
        }
    }
}

/* Runs a two channel group's matrix over the bands it covers */
static
VOID
XaProUndoMatrix(
    _In_ const XaWmaDecoder* Decoder,
    _In_ const XaWmaGroup* Group)
{
    float* Lanes[XA_WMA_MAX_CHANNELS];
    INT32 Count = 0;
    const float* M = Group->Matrix;

    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        if (Group->Members[Decoder->Picked[Pick]])
        {
            Lanes[Count] = Decoder->Channels[Decoder->Picked[Pick]].Coefs;
            Count += 1;
        }
    }
    if (Count != 2)
        return;
    for (INT32 Band = 0; Band < Decoder->Bands; Band++)
    {
        INT32 End = XaMin(Decoder->Edges[Band + 1], Decoder->CodedLimit);

        if (!Group->BandOn[Band])
            continue;
        for (INT32 Index = Decoder->Edges[Band]; Index < End; Index++)
        {
            float A = Lanes[0][Index];
            float B = Lanes[1][Index];

            Lanes[0][Index] = fmaf(A, M[0], M[1] * B);
            Lanes[1][Index] = fmaf(M[3], B, M[2] * A);
        }
    }
}

/* Inverse channel transforms of one pass */
static
VOID
XaProUndoGroups(
    _In_ const XaWmaDecoder* Decoder,
    _In_ BOOLEAN Early)
{
    for (INT32 Index = 0; Index < Decoder->GroupCount; Index++)
    {
        const XaWmaGroup* Group = &Decoder->Groups[Index];

        if (Group->Early != Early || Group->Count == 1)
            continue;
        if (Group->Fixed && Group->Kind == XA_PRO_KIND_IDENTITY)
            continue;
        if (Group->Fixed && Group->Kind == XA_PRO_KIND_PAIR && Decoder->Layout.Channels == 2)
            XaProUndoPair(Decoder, Group);
        else
            XaProUndoMatrix(Decoder, Group);
    }
}

/* Scales each band of a channel by the quantizer step its mask value calls for */
static
HRESULT
XaProWeight(
    _In_ const XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    const INT32* Mask = Channel->MaskSent ? Channel->Mask : Channel->Resampled;
    INT32 Limit = XaMin(Channel->Size, Decoder->CodedLimit);

    if (Channel->MaskSize <= 0 || Channel->Size <= 0)
        return XA_WMA_E_CORRUPT;
    for (INT32 Band = 0; Band < Decoder->Bands; Band++)
    {
        INT32 End = XaMin(Decoder->Edges[Band + 1], Limit);
        INT32 Index = static_cast<INT32>(Channel->QuantShift) - Channel->MaskStep * (Channel->MaskPeak - Mask[Band]) +
                      Decoder->QuantIndex;
        float Step = XaProQuantStep(Index);

        for (INT32 Coef = Decoder->Edges[Band]; Coef < End; Coef++)
            Channel->Coefs[Coef] = Channel->Coefs[Coef] * Step;
    }
    return S_OK;
}

/**
 * @brief Decodes one subframe of the channels furthest behind and runs their inverse transforms.
 */
static
HRESULT
XaProDecodeSubframe(
    _Inout_ XaWmaDecoder* Decoder)
{
    BOOLEAN AnyCoded = FALSE;
    HRESULT hr;

    hr = XaProReadSubframeHeader(Decoder);
    if (FAILED(hr))
        return hr;

    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];

        Decoder->CoefIndex = -1;
        Decoder->RunBitsPro = static_cast<INT32>(XaProLog2(static_cast<UINT32>(Channel->Width - 1)) + 1);
        if (Channel->Coded)
        {
            AnyCoded = TRUE;
            hr = XaProReadCoefficients(Decoder, Channel);
            if (FAILED(hr))
                return Decoder->Reader.Dry ? XA_WMA_E_STARVED : hr;
        }
        Decoder->CodedLimit = XaMax(Decoder->CodedLimit, Channel->CodedLength);
        Decoder->CoefIndex = -1;
    }

    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];

        if (!Channel->Coded)
            memset(Channel->Coefs, 0, sizeof(*Channel->Coefs) * Channel->Size);
    }
    for (INT32 Index = 0; Index < Decoder->GroupCount; Index++)
    {
        if (!Decoder->Groups[Index].Fixed)
            XaProBuildRotation(&Decoder->Groups[Index]);
    }
    if (AnyCoded)
    {
        XaProUndoGroups(Decoder, TRUE);
        XaProUndoGroups(Decoder, FALSE);
    }
    if (!AnyCoded)
        return S_OK;

    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        hr = XaProWeight(Decoder, &Decoder->Channels[Decoder->Picked[Pick]]);
        if (FAILED(hr))
            return hr;
    }
    for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];

        XaWmaInverseTransform(Decoder->Transform, Channel->Coefs, static_cast<UINT32>(Channel->Size),
                              2.0f / static_cast<float>(Channel->Size));
    }
    return S_OK;
}

/* Samples the first frame hides: the stated start trim, or the encoder's lead in */
static
VOID
XaProSetDiscard(
    _Inout_ XaWmaDecoder* Decoder)
{
    INT32 First = 0;

    if (Decoder->StartTrimmed)
    {
        Decoder->Discard = Decoder->StartTrim;
        return;
    }
    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
        First = XaMax(First, Decoder->Channels[Index].Tiles.Sizes[1]);
    Decoder->Discard = (Decoder->FrameSize + First) / 2;
}

/* Works out how far the frame's output is final; the last subframe's second half waits for the next frame */
static
HRESULT
XaProFinishFrame(
    _Inout_ XaWmaDecoder* Decoder)
{
    INT32 Frame = Decoder->FrameSize;
    INT32 End = MAXINT32;
    INT32 Produced;

    if (Decoder->Read < 0 || Decoder->Read >= Frame / 2)
        return E_FAIL;
    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        const XaWmaTiling* Tiles = &Decoder->Channels[Index].Tiles;
        INT32 Last = Tiles->Sizes[Tiles->Count];

        End = XaMin(End, Frame / 2 - Last / 2 + Frame);
    }

    Produced = End - Decoder->Read;
    Decoder->End = End;
    if (Decoder->Discard > 0)
    {
        if (Decoder->Discard < Produced)
        {
            Decoder->Read += Decoder->Discard;
            Decoder->Discard = 0;
        }
        else
        {
            Decoder->Discard -= Produced;
            Decoder->Read = Decoder->End;
        }
        if (Decoder->Read >= Frame)
        {
            for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
            {
                float* Pcm = Decoder->Channels[Index].Pcm;

                memmove(Pcm, Pcm + Frame, sizeof(*Pcm) * (Frame / 2));
            }
            Decoder->Read -= Frame;
            Decoder->End -= Frame;
        }
        if (Decoder->EndTrim != 0 && Decoder->End - Decoder->EndTrim < Decoder->Read)
        {
            Decoder->Read = 0;
            Decoder->End = 0;
            Decoder->EndTrim = 0;
        }
    }
    Decoder->End -= Decoder->EndTrim;
    if (Decoder->End < Decoder->Read)
        return XA_WMA_E_CORRUPT;

    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        XaWmaTiling* Tiles = &Decoder->Channels[Index].Tiles;

        Tiles->Sizes[0] = Tiles->Sizes[Tiles->Count];
        Tiles->Count = 0;
        Tiles->Covered = 0;
    }
    return S_OK;
}

/**
 * @brief Decodes one WMA Pro frame and the bits that close it.
 */
HRESULT
XaWmaDecodeProFrame(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Channels = Decoder->Layout.Channels;
    UINT32 Bit;
    HRESULT hr;

    /* Output that only an end trim left behind does not carry into the next frame */
    if (Decoder->Read >= Decoder->FrameSize / 2)
    {
        Decoder->Read = 0;
        Decoder->End = 0;
    }
    Decoder->EndTrim = 0;
    Decoder->Reader.Taken = 0;
    for (UINT32 Index = 0; Index < Channels; Index++)
    {
        Decoder->Channels[Index].Tiles.Index = 0;
        Decoder->Channels[Index].Fill = 0;
    }

    hr = XaProReadFrameHeader(Decoder);
    if (FAILED(hr))
        return hr;
    do
    {
        hr = XaProDecodeSubframe(Decoder);
        if (FAILED(hr))
            return hr;
        for (UINT32 Pick = 0; Pick < Decoder->PickedCount; Pick++)
        {
            XaWmaChannel* Channel = &Decoder->Channels[Decoder->Picked[Pick]];

            XaWmaOverlap(Channel);
            if (Channel->Tiles.Index >= Channel->Tiles.Count)
                return XA_WMA_E_CORRUPT;
            Channel->Tiles.Index += 1;
        }
    } while (!Decoder->FrameDone);

    /*
     * The frame ends with a one after any padding, then a bit saying whether another frame starts
     * in the packet this one started in. The next frame follows directly either way.
     */
    do
    {
        XA_PRO_READ(XaProBit(Decoder, &Bit));
    } while (Bit != 1);
    XA_PRO_READ(XaWmaSkipBits(Decoder, 1));
    if (Decoder->Pro.FrameLength && Decoder->FrameBits != Decoder->Layout.PacketBytes * 8 &&
        Decoder->FrameBits != Decoder->Reader.Taken)
    {
        return XA_WMA_E_CORRUPT;
    }

    if (Decoder->FirstTile)
    {
        XaProSetDiscard(Decoder);
        Decoder->FirstTile = FALSE;
    }
    return XaProFinishFrame(Decoder);
}
