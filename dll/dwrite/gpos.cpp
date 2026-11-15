/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     OpenType glyph positioning: pair kerning
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "gpos.hpp"

constexpr USHORT RxPairPositioning = 2;
constexpr USHORT RxExtensionPositioning = 9;

/* Value record fields */
constexpr USHORT RxValueXAdvance = 0x0004;

static
UINT32
RxValueSize(
    _In_ USHORT Format)
{
    UINT32 Size = 0;

    for (USHORT Bits = Format & 0xFF; Bits != 0; Bits &= Bits - 1)
        Size += 2;
    return Size;
}

/* XAdvance out of a value record, zero when the format has none */
static
INT32
RxValueAdvance(
    _In_ const RxSpan& Data,
    _In_ UINT32 Offset,
    _In_ USHORT Format)
{
    UINT32 Field = 0;

    if (!(Format & RxValueXAdvance))
        return 0;
    for (USHORT Bit = 1; Bit < RxValueXAdvance; Bit <<= 1)
    {
        if (Format & Bit)
            Field += 2;
    }
    return Data.S16(Offset + Field);
}

/* Coverage index of a glyph, or MAXUINT32 */
static
UINT32
RxCoverage(
    _In_ const RxSpan& Coverage,
    _In_ USHORT Glyph)
{
    USHORT Format = Coverage.U16(0);
    UINT32 Count = Coverage.U16(2);
    UINT32 Low = 0;
    UINT32 High = Count;

    if (Format == 1)
    {
        while (Low < High)
        {
            UINT32 Middle = (Low + High) / 2;
            USHORT Found = Coverage.U16(4 + Middle * 2);

            if (Found == Glyph)
                return Middle;
            if (Found < Glyph)
                Low = Middle + 1;
            else
                High = Middle;
        }
        return MAXUINT32;
    }

    if (Format == 2)
    {
        while (Low < High)
        {
            UINT32 Middle = (Low + High) / 2;
            UINT32 Record = 4 + Middle * 6;
            USHORT Start = Coverage.U16(Record);
            USHORT End = Coverage.U16(Record + 2);

            if (Glyph < Start)
                High = Middle;
            else if (Glyph > End)
                Low = Middle + 1;
            else
                return Coverage.U16(Record + 4) + (Glyph - Start);
        }
    }
    return MAXUINT32;
}

static
USHORT
RxGlyphClass(
    _In_ const RxSpan& ClassDef,
    _In_ USHORT Glyph)
{
    USHORT Format = ClassDef.U16(0);

    if (Format == 1)
    {
        USHORT Start = ClassDef.U16(2);
        USHORT Count = ClassDef.U16(4);

        if (Glyph >= Start && Glyph - Start < Count)
            return ClassDef.U16(6 + (Glyph - Start) * 2);
        return 0;
    }

    if (Format == 2)
    {
        UINT32 Low = 0;
        UINT32 High = ClassDef.U16(2);

        while (Low < High)
        {
            UINT32 Middle = (Low + High) / 2;
            UINT32 Record = 4 + Middle * 6;
            USHORT Start = ClassDef.U16(Record);
            USHORT End = ClassDef.U16(Record + 2);

            if (Glyph < Start)
                High = Middle;
            else if (Glyph > End)
                Low = Middle + 1;
            else
                return ClassDef.U16(Record + 4);
        }
    }
    return 0;
}

VOID
RxKerning::Load(
    _In_ const RxFaceTables* Face,
    _In_ UINT32 Script)
{
    RxSpan Gpos = Face->Sfnt().Table(RxTag('G', 'P', 'O', 'S'));
    RxSpan Scripts;
    RxSpan Features;
    RxSpan Lookups;
    RxSpan LangSys;
    std::vector<USHORT> LookupIndices;

    Subtables.clear();
    if (Gpos.U16(0) != 1)
        return;

    Scripts = Gpos.Sub(Gpos.U16(4));
    Features = Gpos.Sub(Gpos.U16(6));
    Lookups = Gpos.Sub(Gpos.U16(8));

    /* The script's default language system, else DFLT's */
    for (UINT32 Pass = 0; Pass < 2 && LangSys.IsEmpty(); Pass++)
    {
        UINT32 Wanted = Pass == 0 ? Script : RxTag('D', 'F', 'L', 'T');

        for (USHORT Index = 0; Index < Scripts.U16(0); Index++)
        {
            UINT32 Record = 2 + Index * 6;

            if (Scripts.U32(Record) == Wanted)
            {
                RxSpan ScriptTable = Scripts.Sub(Scripts.U16(Record + 4));
                USHORT Default = ScriptTable.U16(0);

                if (Default != 0)
                    LangSys = ScriptTable.Sub(Default);
                break;
            }
        }
    }
    if (LangSys.IsEmpty())
        return;

    for (USHORT Index = 0; Index < LangSys.U16(4); Index++)
    {
        USHORT FeatureIndex = LangSys.U16(6 + Index * 2);
        UINT32 Record = 2 + FeatureIndex * 6;
        RxSpan Feature;

        if (FeatureIndex >= Features.U16(0) || Features.U32(Record) != RxTag('k', 'e', 'r', 'n'))
            continue;

        Feature = Features.Sub(Features.U16(Record + 4));
        for (USHORT Lookup = 0; Lookup < Feature.U16(2); Lookup++)
            LookupIndices.push_back(Feature.U16(4 + Lookup * 2));
    }

    /* Lookups run in lookup list order */
    std::sort(LookupIndices.begin(), LookupIndices.end());
    LookupIndices.erase(std::unique(LookupIndices.begin(), LookupIndices.end()), LookupIndices.end());

    for (USHORT LookupIndex : LookupIndices)
    {
        RxSpan Lookup;
        USHORT Type;

        if (LookupIndex >= Lookups.U16(0))
            continue;
        Lookup = Lookups.Sub(Lookups.U16(2 + LookupIndex * 2));
        Type = Lookup.U16(0);

        for (USHORT Index = 0; Index < Lookup.U16(4); Index++)
        {
            RxSpan Table = Lookup.Sub(Lookup.U16(6 + Index * 2));
            USHORT TableType = Type;

            if (Type == RxExtensionPositioning)
            {
                TableType = Table.U16(2);
                Table = Table.Sub(Table.U32(4));
            }
            if (TableType == RxPairPositioning)
                Subtables.push_back({ LookupIndex, Table });
        }
    }
}

VOID
RxKerning::Apply(
    _In_reads_(Count) const USHORT* Glyphs,
    _In_ UINT32 Count,
    _Inout_updates_(Count) INT32* Adjustments) const
{
    SIZE_T First = 0;

    /* One lookup at a time over the whole run, its subtables tried in order */
    while (First < Subtables.size())
    {
        SIZE_T Last = First;

        while (Last < Subtables.size() && Subtables[Last].Lookup == Subtables[First].Lookup)
            Last++;

        for (UINT32 Index = 0; Index + 1 < Count; Index++)
        {
            USHORT Left = Glyphs[Index];
            USHORT Right = Glyphs[Index + 1];

            for (SIZE_T Table = First; Table < Last; Table++)
            {
                const RxSpan& Data = Subtables[Table].Data;
                USHORT Format = Data.U16(0);
                USHORT Value1 = Data.U16(4);
                USHORT Value2 = Data.U16(6);
                UINT32 Size1 = RxValueSize(Value1);
                UINT32 Size2 = RxValueSize(Value2);
                UINT32 Covered = RxCoverage(Data.Sub(Data.U16(2)), Left);
                BOOLEAN Matched = FALSE;

                if (Covered == MAXUINT32)
                    continue;

                if (Format == 1)
                {
                    RxSpan Set = Data.Sub(Data.U16(10 + Covered * 2));
                    UINT32 Stride = 2 + Size1 + Size2;
                    UINT32 Low = 0;
                    UINT32 High = Set.U16(0);

                    if (Covered >= Data.U16(8))
                        continue;
                    while (Low < High)
                    {
                        UINT32 Middle = (Low + High) / 2;
                        UINT32 Record = 2 + Middle * Stride;
                        USHORT Second = Set.U16(Record);

                        if (Second == Right)
                        {
                            Adjustments[Index] += RxValueAdvance(Set, Record + 2, Value1);
                            Adjustments[Index + 1] += RxValueAdvance(Set, Record + 2 + Size1, Value2);
                            Matched = TRUE;
                            break;
                        }
                        if (Second < Right)
                            Low = Middle + 1;
                        else
                            High = Middle;
                    }
                }
                else if (Format == 2)
                {
                    USHORT Class1 = RxGlyphClass(Data.Sub(Data.U16(8)), Left);
                    USHORT Class2 = RxGlyphClass(Data.Sub(Data.U16(10)), Right);
                    USHORT Class1Count = Data.U16(12);
                    USHORT Class2Count = Data.U16(14);

                    if (Class1 < Class1Count && Class2 < Class2Count)
                    {
                        UINT32 Record = 16 + (Class1 * Class2Count + Class2) * (Size1 + Size2);

                        Adjustments[Index] += RxValueAdvance(Data, Record, Value1);
                        Adjustments[Index + 1] += RxValueAdvance(Data, Record + Size1, Value2);
                        Matched = TRUE;
                    }
                }

                if (Matched)
                {
                    /* A second value record consumes the second glyph */
                    if (Value2 != 0)
                        Index++;
                    break;
                }
            }
        }
        First = Last;
    }
}
