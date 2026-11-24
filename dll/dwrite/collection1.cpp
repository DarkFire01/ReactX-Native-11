/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The newer collection, family and font interfaces
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "collection.hpp"

#include <cmath>

/* RxFontCollection */

HRESULT
STDMETHODCALLTYPE
RxFontCollection::GetFontFamily(
    UINT32 index,
    _COM_Outptr_ IDWriteFontFamily1** fontFamily)
{
    return GetFontFamily(index, reinterpret_cast<IDWriteFontFamily**>(fontFamily));
}

HRESULT
STDMETHODCALLTYPE
RxFontCollection::GetFontFamily(
    UINT32 index,
    _COM_Outptr_ IDWriteFontFamily2** fontFamily)
{
    return GetFontFamily(index, reinterpret_cast<IDWriteFontFamily**>(fontFamily));
}

DWRITE_FONT_FAMILY_MODEL
STDMETHODCALLTYPE
RxFontCollection::GetFontFamilyModel(VOID)
{
    return FamilyModel;
}

/* RxFontFamily */

HRESULT
STDMETHODCALLTYPE
RxFontFamily::GetFont(
    UINT32 listIndex,
    _COM_Outptr_ IDWriteFont3** font)
{
    return GetFont(listIndex, reinterpret_cast<IDWriteFont**>(font));
}

DWRITE_LOCALITY
STDMETHODCALLTYPE
RxFontFamily::GetFontLocality(
    UINT32 listIndex)
{
    UNREFERENCED_PARAMETER(listIndex);
    return DWRITE_LOCALITY_LOCAL;
}

/* RxFont */

VOID
STDMETHODCALLTYPE
RxFont::GetMetrics(
    _Out_ DWRITE_FONT_METRICS1* fontMetrics)
{
    RxFontFace* Result;

    memset(fontMetrics, 0, sizeof(*fontMetrics));
    if (SUCCEEDED(Face(&Result)))
        Result->GetMetrics(fontMetrics);
}

VOID
STDMETHODCALLTYPE
RxFont::GetPanose(
    _Out_ DWRITE_PANOSE* panose)
{
    RxFontFace* Result;

    memset(panose, 0, sizeof(*panose));
    if (SUCCEEDED(Face(&Result)))
        Result->GetPanose(panose);
}

BOOL
STDMETHODCALLTYPE
RxFont::IsMonospacedFont(VOID)
{
    RxFontFace* Result;

    return SUCCEEDED(Face(&Result)) ? Result->IsMonospacedFont() : FALSE;
}

BOOL
STDMETHODCALLTYPE
RxFont::IsColorFont(VOID)
{
    RxFontFace* Result;

    return SUCCEEDED(Face(&Result)) ? Result->IsColorFont() : FALSE;
}

HRESULT
STDMETHODCALLTYPE
RxFont::CreateFontFace(
    _COM_Outptr_ IDWriteFontFace3** fontFace)
{
    return CreateFontFace(reinterpret_cast<IDWriteFontFace**>(fontFace));
}

BOOL
STDMETHODCALLTYPE
RxFont::Equals(
    IDWriteFont* font)
{
    RxFont* Other = RxFont::FromInterface(font);

    return Other != nullptr && Other->Collection->Data() == Collection->Data() && Other->Item.Entry == Item.Entry &&
           Other->Item.Simulations == Item.Simulations;
}

BOOL
STDMETHODCALLTYPE
RxFont::HasCharacter(
    UINT32 unicodeValue)
{
    BOOL Exists = FALSE;

    HasCharacter(unicodeValue, &Exists);
    return Exists;
}

DWRITE_LOCALITY
STDMETHODCALLTYPE
RxFont::GetLocality(VOID)
{
    return DWRITE_LOCALITY_LOCAL;
}

/* Axis matching */

namespace
{

/* How far a font lies from an axis request, and where on its axes it ends up */
struct RxAxisDistance
{
    INT64 Primary;
    INT64 Secondary;
    INT64 Dot;
    UINT32 Position;
    RxFontItem Item;
};

/* Weight of a unit of each standard axis against the others, and where the axis is centered */
VOID
RxAxisScale(
    _In_ DWRITE_FONT_AXIS_TAG Tag,
    _Out_ INT32* Scale,
    _Out_ INT32* Center)
{
    *Center = 0;
    switch (Tag)
    {
        case DWRITE_FONT_AXIS_TAG_WEIGHT:
            *Scale = 5 * RxFixedOne;
            *Center = 400 * RxFixedOne;
            break;
        case DWRITE_FONT_AXIS_TAG_WIDTH:
            *Scale = 55 * RxFixedOne;
            *Center = 100 * RxFixedOne;
            break;
        case DWRITE_FONT_AXIS_TAG_ITALIC:
            *Scale = 1400 * RxFixedOne;
            break;
        case DWRITE_FONT_AXIS_TAG_SLANT:
            *Scale = 35 * RxFixedOne;
            break;
        case DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE:
            *Scale = RxFixedOne;
            *Center = 12 * RxFixedOne;
            break;
        default:
            *Scale = RxFixedOne;
            break;
    }
}

BOOLEAN
RxIsFixedInfinite(
    _In_ INT32 Value)
{
    return Value == RxFixedMinimum || Value == RxFixedMaximum;
}

INT32
RxSaturate(
    _In_ INT64 Value)
{
    if (Value < RxFixedMinimum)
        return RxFixedMinimum;
    if (Value > RxFixedMaximum)
        return RxFixedMaximum;
    return static_cast<INT32>(Value);
}

/* Rounded fixed product; a nonzero result keeps an infinite operand */
INT32
RxScaleFixed(
    _In_ INT32 Value,
    _In_ INT32 Scale)
{
    INT32 Product = RxSaturate((static_cast<INT64>(Value) * Scale + 0x8000) >> 16);

    if (Product != 0)
    {
        if (RxIsFixedInfinite(Value))
            return Value;
        if (RxIsFixedInfinite(Scale))
            return Scale;
    }
    return Product;
}

INT64
RxSquareFixed(
    _In_ INT32 Value)
{
    INT64 Magnitude = Value < 0 ? (Value == RxFixedMinimum ? RxFixedMaximum : -static_cast<INT64>(Value)) : Value;

    return Magnitude * Magnitude;
}

/* Distance of one font from a request sorted by tag; Values holds the font's axis values sorted by tag */
VOID
RxMeasureAxisDistance(
    _In_ const std::vector<RxAxisValue>& Request,
    _In_ const std::vector<RxAxisValue>& Values,
    _In_ const std::vector<RxAxisRange>& Ranges,
    _In_ DWRITE_FONT_SIMULATIONS Allowed,
    _Inout_ RxAxisDistance& Distance)
{
    std::vector<RxAxisValue> Settled;
    SIZE_T Wanted = 0;
    SIZE_T Own = 0;
    UINT32 Simulations = DWRITE_FONT_SIMULATIONS_NONE;

    while (Wanted < Request.size() || Own < Values.size())
    {
        BOOLEAN Requested = Own == Values.size() ||
                            (Wanted < Request.size() && Request[Wanted].Tag <= Values[Own].Tag);
        BOOLEAN Present = !(Wanted < Request.size() && (Own == Values.size() || Request[Wanted].Tag < Values[Own].Tag));
        DWRITE_FONT_AXIS_TAG Tag = Present ? Values[Own].Tag : Request[Wanted].Tag;
        INT32 Scale;
        INT32 Center;
        INT32 Desired;
        INT32 Original;
        INT32 Actual;
        INT32 DesiredScaled;
        INT32 ActualScaled;
        INT32 OriginalScaled;
        INT64 Gap;

        RxAxisScale(Tag, &Scale, &Center);
        Desired = Requested ? Request[Wanted].Value : Center;
        Original = Center;
        Actual = Center;

        if (Present)
        {
            Original = Values[Own].Value;
            Actual = Original;
            for (const auto& Range : Ranges)
            {
                if (Requested && Range.Tag == Tag)
                {
                    Actual = std::max(std::min(Range.Maximum, Desired), Range.Minimum);
                    break;
                }
            }
            Settled.push_back({ Tag, Actual });
        }
        if (Requested)
            Wanted += 1;
        if (Present)
            Own += 1;

        /* Bold makes up for 300 units of weight, oblique for 20 degrees of slant */
        if (Tag == DWRITE_FONT_AXIS_TAG_WEIGHT)
        {
            if (Desired > 550 * RxFixedOne && (Allowed & DWRITE_FONT_SIMULATIONS_BOLD))
            {
                INT32 Reach = RxIsFixedInfinite(Actual) ? Actual : RxSaturate(static_cast<INT64>(Actual) + 150 * RxFixedOne);

                if (Desired > Reach)
                {
                    Simulations |= DWRITE_FONT_SIMULATIONS_BOLD;
                    if (!RxIsFixedInfinite(Actual))
                        Actual = RxSaturate(static_cast<INT64>(Actual) + 300 * RxFixedOne);
                }
            }
        }
        else if (Tag == DWRITE_FONT_AXIS_TAG_SLANT && (Allowed & DWRITE_FONT_SIMULATIONS_OBLIQUE) &&
                 Desired <= -10 * RxFixedOne && Actual >= -10 * RxFixedOne)
        {
            INT32 Reach = Actual == RxFixedMaximum ? Actual : RxSaturate(static_cast<INT64>(Actual) - 10 * RxFixedOne);

            if (Desired <= Reach)
            {
                Simulations |= DWRITE_FONT_SIMULATIONS_OBLIQUE;
                if (Actual != RxFixedMaximum)
                    Actual = RxSaturate(static_cast<INT64>(Actual) - 20 * RxFixedOne);
            }
        }

        DesiredScaled = RxScaleFixed(RxFixedSubtract(Desired, Center), Scale);
        OriginalScaled = RxScaleFixed(RxFixedSubtract(Original, Center), Scale);
        ActualScaled = RxScaleFixed(RxFixedSubtract(Actual, Center), Scale);

        Gap = RxSquareFixed(RxFixedSubtract(DesiredScaled, ActualScaled));
        Distance.Primary += Gap >> 16;
        if (ActualScaled != OriginalScaled)
            Gap = RxSquareFixed(RxFixedSubtract(DesiredScaled, OriginalScaled));
        Distance.Secondary += Gap >> 16;
        Distance.Dot += (static_cast<INT64>(OriginalScaled) * DesiredScaled) >> 16;
    }

    /* Axes with a range come first, in range order, then the rest */
    Distance.Item.AxisValues.clear();
    for (const auto& Range : Ranges)
    {
        for (auto& Value : Settled)
        {
            if (Value.Tag != 0 && Value.Tag == Range.Tag)
            {
                Distance.Item.AxisValues.push_back(Value);
                Value.Tag = static_cast<DWRITE_FONT_AXIS_TAG>(0);
                break;
            }
        }
    }
    for (const auto& Value : Settled)
    {
        if (Value.Tag != 0)
            Distance.Item.AxisValues.push_back(Value);
    }
    Distance.Item.Simulations = static_cast<DWRITE_FONT_SIMULATIONS>(Simulations);
}

BOOLEAN
RxTagLess(
    _In_ const RxAxisValue& A,
    _In_ const RxAxisValue& B)
{
    return static_cast<UINT32>(A.Tag) < static_cast<UINT32>(B.Tag);
}

}

/* Items ranked by axis distance, one instance kept per face */
static
VOID
RxRankByAxisDistance(
    _In_ const RxFontSetData* Data,
    _In_ std::vector<RxAxisValue> Request,
    _In_ DWRITE_FONT_SIMULATIONS Allowed,
    _Inout_ std::vector<RxFontItem>& Items)
{
    std::vector<RxAxisDistance> Distances;

    std::sort(Items.begin(), Items.end(), [](const RxFontItem& A, const RxFontItem& B) { return A.Entry < B.Entry; });
    Items.erase(std::unique(Items.begin(), Items.end(),
                            [](const RxFontItem& A, const RxFontItem& B) { return A.Entry == B.Entry; }),
                Items.end());
    std::sort(Request.begin(), Request.end(), RxTagLess);

    for (UINT32 Index = 0; Index < Items.size(); Index++)
    {
        const RxFaceDescription& Description = Data->Entries[Items[Index].Entry].Description;
        std::vector<RxAxisValue> Values = Description.AxisValues;
        RxAxisDistance Distance = { 0, 0, 0, Index, { Items[Index].Entry, DWRITE_FONT_SIMULATIONS_NONE, {} } };

        std::sort(Values.begin(), Values.end(), RxTagLess);
        RxMeasureAxisDistance(Request, Values, Description.AxisRanges, Allowed, Distance);
        Distances.push_back(std::move(Distance));
    }

    std::sort(Distances.begin(), Distances.end(), [](const RxAxisDistance& A, const RxAxisDistance& B)
    {
        INT64 First = A.Primary + ((A.Item.Simulations & DWRITE_FONT_SIMULATIONS_BOLD) ? 745 : 0) +
                      ((A.Item.Simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) ? 700 : 0);
        INT64 Second = B.Primary + ((B.Item.Simulations & DWRITE_FONT_SIMULATIONS_BOLD) ? 745 : 0) +
                       ((B.Item.Simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) ? 700 : 0);

        if (First != Second)
            return First < Second;
        if (A.Secondary != B.Secondary)
            return A.Secondary < B.Secondary;
        if (A.Dot != B.Dot)
            return A.Dot < B.Dot;
        return A.Position < B.Position;
    });

    /* One item per face survives; the system picks it with an unstable sort by file, so ties land where that sort leaves them */
    {
        struct Resource
        {
            const BYTE* Key;
            UINT32 KeySize;
            UINT32 FaceIndex;
            UINT32 Position;
        };
        std::vector<Resource> Resources;
        std::vector<RxFontItem> Ranked;

        for (UINT32 Index = 0; Index < Distances.size(); Index++)
        {
            const RxFontEntry& Entry = Data->Entries[Distances[Index].Item.Entry];
            const void* Key = nullptr;
            UINT32 KeySize = 0;

            if (FAILED(Entry.File->GetReferenceKey(&Key, &KeySize)))
                KeySize = 0;
            Resources.push_back({ static_cast<const BYTE*>(Key), KeySize, Entry.FaceIndex, Index });
        }

        auto FileLess = [](const Resource& A, const Resource& B)
        {
            if (A.KeySize != B.KeySize)
                return A.KeySize < B.KeySize;
            return memcmp(A.Key, B.Key, A.KeySize) < 0;
        };
        std::sort(Resources.begin(), Resources.end(), [&](const Resource& A, const Resource& B)
        {
            if (FileLess(A, B) || FileLess(B, A))
                return FileLess(A, B);
            return A.FaceIndex < B.FaceIndex;
        });
        Resources.erase(std::unique(Resources.begin(), Resources.end(), [&](const Resource& A, const Resource& B)
        {
            return !FileLess(A, B) && !FileLess(B, A) && A.FaceIndex == B.FaceIndex;
        }), Resources.end());
        std::sort(Resources.begin(), Resources.end(),
                  [](const Resource& A, const Resource& B) { return A.Position < B.Position; });

        for (const auto& Kept : Resources)
            Ranked.push_back(std::move(Distances[Kept.Position].Item));
        Items = std::move(Ranked);
    }
}

HRESULT
STDMETHODCALLTYPE
RxFontFamily::GetMatchingFonts(
    _In_reads_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE const* fontAxisValues,
    UINT32 fontAxisValueCount,
    _COM_Outptr_ IDWriteFontList2** matchingFonts)
{
    std::vector<RxAxisValue> Request;
    std::vector<RxFontItem> Ranked = Collection->FamilyList()[FamilyIndex].Items;
    RxFontFamily* Result;
    HRESULT hr;

    if (matchingFonts == nullptr)
        return E_INVALIDARG;
    *matchingFonts = nullptr;
    if (fontAxisValueCount >= 0x10000 || (fontAxisValueCount != 0 && fontAxisValues == nullptr))
        return E_INVALIDARG;

    for (UINT32 Index = 0; Index < fontAxisValueCount; Index++)
        Request.push_back({ fontAxisValues[Index].axisTag, RxFloatToFixed(fontAxisValues[Index].value) });

    RxRankByAxisDistance(Collection->Data(), std::move(Request),
                         static_cast<DWRITE_FONT_SIMULATIONS>(DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE),
                         Ranked);

    hr = CreateList(Collection.Get(), FamilyIndex, std::move(Ranked), &Result);
    if (FAILED(hr))
        return hr;
    *matchingFonts = static_cast<IDWriteFontList2*>(Result);
    return S_OK;
}