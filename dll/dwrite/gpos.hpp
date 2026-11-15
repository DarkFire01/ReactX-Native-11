/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     OpenType glyph positioning: pair kerning
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "facetables.hpp"

/* Pair positioning lookups of a face's kern feature, read once */
class RxKerning
{
public:
    /* Lookups for the script tag (big endian), falling back to DFLT */
    VOID
    Load(
        _In_ const RxFaceTables* Face,
        _In_ UINT32 Script);

    BOOLEAN
    IsEmpty(VOID) const
    {
        return Subtables.empty();
    }

    /* Adds the design unit advance changes of each glyph to Adjustments */
    VOID
    Apply(
        _In_reads_(Count) const USHORT* Glyphs,
        _In_ UINT32 Count,
        _Inout_updates_(Count) INT32* Adjustments) const;

private:
    struct Subtable
    {
        UINT32 Lookup;
        RxSpan Data;
    };

    std::vector<Subtable> Subtables;
};
