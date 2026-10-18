/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Deterministic synthetic test pattern
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "clips.hpp"

#include <algorithm>
#include <math.h>

/* Seven segment layout of each digit: bits a..g */
static const UINT8 DigitSegments[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

static
VOID
FillRect(
    _Inout_ std::vector<UINT8>& Frame,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ INT Left,
    _In_ INT Top,
    _In_ INT Right,
    _In_ INT Bottom,
    _In_ UINT8 Y,
    _In_ UINT8 U,
    _In_ UINT8 V)
{
    UINT8* Chroma = Frame.data() + (SIZE_T)Width * Height;
    INT Row;
    INT Column;

    Left = std::max(Left, 0);
    Top = std::max(Top, 0);
    Right = std::min(Right, (INT)Width);
    Bottom = std::min(Bottom, (INT)Height);

    for (Row = Top; Row < Bottom; Row++)
    {
        for (Column = Left; Column < Right; Column++)
            Frame[(SIZE_T)Row * Width + Column] = Y;
    }

    for (Row = Top / 2; Row < Bottom / 2; Row++)
    {
        for (Column = Left / 2; Column < Right / 2; Column++)
        {
            Chroma[(SIZE_T)Row * Width + Column * 2] = U;
            Chroma[(SIZE_T)Row * Width + Column * 2 + 1] = V;
        }
    }
}

static
VOID
DrawDigit(
    _Inout_ std::vector<UINT8>& Frame,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ INT X,
    _In_ INT Y,
    _In_ INT Size,
    _In_ UINT Digit)
{
    INT Thick = std::max(Size / 6, 2);
    INT Half = Thick / 2;
    UINT8 Segments = DigitSegments[Digit % 10];

    if (Segments & 0x01)
        FillRect(Frame, Width, Height, X, Y, X + Size, Y + Thick, 235, 128, 128);
    if (Segments & 0x02)
        FillRect(Frame, Width, Height, X + Size - Thick, Y, X + Size, Y + Size, 235, 128, 128);
    if (Segments & 0x04)
        FillRect(Frame, Width, Height, X + Size - Thick, Y + Size, X + Size, Y + 2 * Size, 235, 128, 128);
    if (Segments & 0x08)
        FillRect(Frame, Width, Height, X, Y + 2 * Size - Thick, X + Size, Y + 2 * Size, 235, 128, 128);
    if (Segments & 0x10)
        FillRect(Frame, Width, Height, X, Y + Size, X + Thick, Y + 2 * Size, 235, 128, 128);
    if (Segments & 0x20)
        FillRect(Frame, Width, Height, X, Y, X + Thick, Y + Size, 235, 128, 128);
    if (Segments & 0x40)
        FillRect(Frame, Width, Height, X, Y + Size - Half, X + Size, Y + Size + Half, 235, 128, 128);
}

VOID
GeneratePattern(
    _Out_ std::vector<UINT8>& Frame,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Index)
{
    UINT8* Chroma;
    UINT Row;
    UINT Column;
    INT BoxSize = (INT)Height / 5;
    INT BoxX = (INT)((Index * 7) % (Width + BoxSize)) - BoxSize;
    INT BoxY = (INT)(Height / 2 + sin(Index * 0.05) * Height / 4) - BoxSize / 2;
    INT DigitSize = (INT)Height / 12;
    UINT Number = Index;
    INT Place;

    Frame.assign((SIZE_T)Width * Height * 3 / 2, 0);
    Chroma = Frame.data() + (SIZE_T)Width * Height;

    /* Diagonal luma ramp drifting with time, slow chroma gradients */
    for (Row = 0; Row < Height; Row++)
    {
        for (Column = 0; Column < Width; Column++)
            Frame[(SIZE_T)Row * Width + Column] = (UINT8)(32 + ((Column + Row + Index * 3) % 384) * 180 / 384);
    }
    for (Row = 0; Row < Height / 2; Row++)
    {
        for (Column = 0; Column < Width / 2; Column++)
        {
            Chroma[(SIZE_T)Row * Width + Column * 2] = (UINT8)(64 + Column * 128 / (Width / 2));
            Chroma[(SIZE_T)Row * Width + Column * 2 + 1] = (UINT8)(64 + Row * 128 / (Height / 2));
        }
    }

    /* Fine detail that a broken decoder smears: a checkerboard in the lower left */
    for (Row = Height * 3 / 4; Row < Height; Row++)
    {
        for (Column = 0; Column < Width / 4; Column++)
            Frame[(SIZE_T)Row * Width + Column] = (((Row / 4) + (Column / 4) + Index / 15) & 1) ? 220 : 40;
    }

    FillRect(Frame, Width, Height, BoxX, BoxY, BoxX + BoxSize, BoxY + BoxSize, 81, 90, 240);
    FillRect(Frame, Width, Height, (INT)Width - BoxX - BoxSize, (INT)Height / 8,
             (INT)Width - BoxX, (INT)Height / 8 + BoxSize / 2, 145, 54, 34);

    /* Frame number, five digits, top right */
    FillRect(Frame, Width, Height, (INT)Width - 6 * DigitSize * 3 / 2, DigitSize / 2,
             (INT)Width - DigitSize / 4, DigitSize * 3, 16, 128, 128);
    for (Place = 4; Place >= 0; Place--)
    {
        DrawDigit(Frame, Width, Height, (INT)Width - (5 - Place) * DigitSize * 3 / 2, DigitSize,
                  DigitSize, Number % 10);
        Number /= 10;
    }
}
