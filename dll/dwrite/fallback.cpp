/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font fallback and the font fallback builder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "fallback.hpp"

/* Private interface our fallbacks answer to, so AddMappings can copy their mappings */
static constexpr GUID RxFallbackIid = { 0x6d2b94e0, 0x18c3, 0x4f7a, { 0xb5, 0x42, 0x0e, 0x93, 0x7c, 0x21, 0xa8, 0x5d } };

/* Classify keys for characters no mapping holds */
static constexpr INT32 RxNoMapping = -1;
static constexpr INT32 RxScanOnly = -2;

/* Matched fonts a cache keeps before it stops adding */
static constexpr SIZE_T RxMaxCachedFonts = 512;

/* Built in mappings */

struct RxSystemMapping
{
    const DWRITE_UNICODE_RANGE* Ranges;
    UINT32 RangeCount;
    /* Both lists end with NULL; no locales means every locale */
    const PCWSTR* Families;
    const PCWSTR* Locales;
};

/* Characters the system draws in color first */
static const DWRITE_UNICODE_RANGE RxEmojiRanges[] =
{
    { 0x2139, 0x2139 }, { 0x231A, 0x231B }, { 0x2328, 0x2328 }, { 0x23CF, 0x23CF }, { 0x23E9, 0x23F3 },
    { 0x23F8, 0x23FA }, { 0x25FD, 0x25FE }, { 0x2600, 0x2604 }, { 0x260E, 0x260E }, { 0x2611, 0x2611 },
    { 0x2614, 0x2615 }, { 0x2618, 0x2618 }, { 0x2620, 0x2620 }, { 0x2622, 0x2623 }, { 0x2626, 0x2626 },
    { 0x262A, 0x262A }, { 0x262E, 0x262F }, { 0x2638, 0x263A }, { 0x2640, 0x2640 }, { 0x2642, 0x2642 },
    { 0x2648, 0x2653 }, { 0x265F, 0x265F }, { 0x2668, 0x2668 }, { 0x267B, 0x267B }, { 0x267E, 0x267F },
    { 0x2692, 0x2697 }, { 0x2699, 0x2699 }, { 0x269B, 0x269C }, { 0x26A0, 0x26A1 }, { 0x26A7, 0x26A7 },
    { 0x26AA, 0x26AB }, { 0x26B0, 0x26B1 }, { 0x26BD, 0x26BE }, { 0x26C4, 0x26C5 }, { 0x26C8, 0x26C8 },
    { 0x26CE, 0x26CF }, { 0x26D1, 0x26D1 }, { 0x26D3, 0x26D4 }, { 0x26E9, 0x26EA }, { 0x26F0, 0x26F5 },
    { 0x26F7, 0x26F8 }, { 0x26FA, 0x26FA }, { 0x26FD, 0x26FD }, { 0x2702, 0x2702 }, { 0x2705, 0x2705 },
    { 0x2708, 0x270C }, { 0x270F, 0x270F }, { 0x2712, 0x2712 }, { 0x2714, 0x2714 }, { 0x2716, 0x2716 },
    { 0x271D, 0x271D }, { 0x2721, 0x2721 }, { 0x2728, 0x2728 }, { 0x2733, 0x2734 }, { 0x2744, 0x2744 },
    { 0x2747, 0x2747 }, { 0x274C, 0x274C }, { 0x274E, 0x274E }, { 0x2753, 0x2755 }, { 0x2757, 0x2757 },
    { 0x2763, 0x2764 }, { 0x2795, 0x2797 }, { 0x27A1, 0x27A1 }, { 0x27B0, 0x27B0 }, { 0x27BF, 0x27BF },
    { 0x2B1B, 0x2B1C }, { 0x2B50, 0x2B50 }, { 0x2B55, 0x2B55 }, { 0x1F18E, 0x1F18E }, { 0x1F191, 0x1F19A },
    { 0x1F1E6, 0x1F1FF }, { 0x1F201, 0x1F201 }, { 0x1F21A, 0x1F21A }, { 0x1F22F, 0x1F22F }, { 0x1F232, 0x1F236 },
    { 0x1F238, 0x1F23A }, { 0x1F250, 0x1F251 }, { 0x1F300, 0x1F321 }, { 0x1F324, 0x1F393 }, { 0x1F396, 0x1F397 },
    { 0x1F399, 0x1F39B }, { 0x1F39E, 0x1F3F0 }, { 0x1F3F3, 0x1F3F5 }, { 0x1F3F7, 0x1F4FD }, { 0x1F4FF, 0x1F53D },
    { 0x1F549, 0x1F567 }, { 0x1F56F, 0x1F570 }, { 0x1F573, 0x1F57A }, { 0x1F587, 0x1F587 }, { 0x1F58A, 0x1F58D },
    { 0x1F590, 0x1F590 }, { 0x1F595, 0x1F596 }, { 0x1F5A4, 0x1F5A5 }, { 0x1F5A8, 0x1F5A8 }, { 0x1F5B1, 0x1F5B2 },
    { 0x1F5BC, 0x1F5BC }, { 0x1F5C2, 0x1F5C4 }, { 0x1F5D1, 0x1F5D3 }, { 0x1F5DC, 0x1F5DE }, { 0x1F5E1, 0x1F5E1 },
    { 0x1F5E3, 0x1F5E3 }, { 0x1F5E8, 0x1F5E8 }, { 0x1F5EF, 0x1F5EF }, { 0x1F5F3, 0x1F5F3 }, { 0x1F5FA, 0x1F64F },
    { 0x1F680, 0x1F6C5 }, { 0x1F6CB, 0x1F6E5 }, { 0x1F6E9, 0x1F6E9 }, { 0x1F6EB, 0x1F6F0 }, { 0x1F6F3, 0x1F6FC },
    { 0x1F7E0, 0x1F7F0 }, { 0x1F90C, 0x1F93A }, { 0x1F93C, 0x1FAF8 }
};
/* Scripts with no font of their own besides the collection font */
static const DWRITE_UNICODE_RANGE RxCollectionRanges[] =
{
    { 0x0800, 0x085E }, { 0x1700, 0x1773 }, { 0x1900, 0x194F }, { 0x1A20, 0x1C4F }, { 0x1C80, 0x1C88 },
    { 0x1CC0, 0x1CC7 }, { 0x1DCB, 0x1DCC }, { 0x1DCE, 0x1DFD }, { 0xA800, 0xA82C }, { 0xA880, 0xA8D9 },
    { 0xA900, 0xA95F }, { 0xAA00, 0xAA5F }, { 0xAA80, 0xAAF6 }, { 0xABC0, 0xABF9 }, { 0xFE27, 0xFE2F },
    { 0x10000, 0x1013F }, { 0x102E0, 0x102FB }, { 0x10350, 0x1037A }, { 0x10500, 0x107BA }, { 0x10860, 0x108FF },
    { 0x10980, 0x1099F }, { 0x109BC, 0x109BD }, { 0x109C0, 0x109FF }, { 0x10A80, 0x10B3F }, { 0x10B80, 0x10BAF },
    { 0x10C80, 0x10FF6 }, { 0x11080, 0x110CD }, { 0x11150, 0x111DF }, { 0x11200, 0x11659 }, { 0x11680, 0x11AA2 },
    { 0x11AC0, 0x11FFF }, { 0x12F90, 0x12FF2 }, { 0x14400, 0x14646 }, { 0x16A40, 0x16FE1 }, { 0x1B170, 0x1D0F5 },
    { 0x1D200, 0x1D2F3 }, { 0x1DF00, 0x1E4F9 }, { 0x1E800, 0x1E8D6 }, { 0x1EC71, 0x1ED3D }
};
static const DWRITE_UNICODE_RANGE RxLatinRanges[] =
{
    { 0x0000, 0x052F }, { 0x1C80, 0x1C8F }, { 0x1D00, 0x1DFF }, { 0x1E00, 0x1FFF }, { 0x2000, 0x218F },
    { 0x2C60, 0x2C7F }, { 0x2DE0, 0x2DFF }, { 0xA4D0, 0xA4FF }, { 0xA640, 0xA69F }, { 0xA700, 0xA7FF },
    { 0xAB30, 0xAB6F }, { 0xFB00, 0xFB06 }, { 0xFE20, 0xFE2F }, { 0xFFF9, 0xFFFD }
};
static const DWRITE_UNICODE_RANGE RxArmenianRanges[] = { { 0x0530, 0x058F }, { 0xFB13, 0xFB17 } };
static const DWRITE_UNICODE_RANGE RxHebrewRanges[] = { { 0x0590, 0x05FF }, { 0xFB1D, 0xFB4F } };
static const DWRITE_UNICODE_RANGE RxArabicRanges[] =
{
    { 0x0600, 0x06FF }, { 0x0750, 0x077F }, { 0x0870, 0x08FF }, { 0xFB50, 0xFDFF }, { 0xFE70, 0xFEFE }
};
static const DWRITE_UNICODE_RANGE RxSyriacRanges[] = { { 0x0700, 0x074F }, { 0x0800, 0x086F } };
static const DWRITE_UNICODE_RANGE RxThaanaRanges[] = { { 0x0780, 0x07BF } };
static const DWRITE_UNICODE_RANGE RxAfricanRanges[] =
{
    { 0x07C0, 0x07FF }, { 0x2D30, 0x2D7F }, { 0xA500, 0xA63F }, { 0xA6A0, 0xA6FF }, { 0x10480, 0x104AF },
    { 0x1E900, 0x1E95F }
};
static const DWRITE_UNICODE_RANGE RxEthiopicRanges[] = { { 0x1200, 0x139F }, { 0x2D80, 0x2DDF }, { 0xAB00, 0xAB2F } };
static const DWRITE_UNICODE_RANGE RxIndicRanges[] =
{
    { 0x0900, 0x0DFF }, { 0x1C50, 0x1C7F }, { 0x1CD0, 0x1CFF }, { 0xA830, 0xA83F }, { 0xA8E0, 0xA8FF }
};
static const DWRITE_UNICODE_RANGE RxThaiRanges[] = { { 0x0E00, 0x0E7F } };
static const DWRITE_UNICODE_RANGE RxLaoRanges[] = { { 0x0E80, 0x0EFF } };
static const DWRITE_UNICODE_RANGE RxTibetanRanges[] = { { 0x0F00, 0x0FFF } };
static const DWRITE_UNICODE_RANGE RxMyanmarRanges[] = { { 0x1000, 0x109F }, { 0xA9E0, 0xA9FF }, { 0xAA60, 0xAA7F } };
static const DWRITE_UNICODE_RANGE RxGeorgianRanges[] = { { 0x10A0, 0x10FF }, { 0x1C90, 0x1CBF }, { 0x2D00, 0x2D2F } };
static const DWRITE_UNICODE_RANGE RxHangulRanges[] =
{
    { 0x1100, 0x11FF }, { 0x3130, 0x318F }, { 0x3200, 0x321F }, { 0x3260, 0x327F }, { 0xA960, 0xA97F },
    { 0xAC00, 0xD7FF }
};
static const DWRITE_UNICODE_RANGE RxAmericanRanges[] =
{
    { 0x13A0, 0x13FF }, { 0x1400, 0x167F }, { 0x18B0, 0x18FF }, { 0xAB70, 0xABBF }
};
static const DWRITE_UNICODE_RANGE RxHistoricRanges[] =
{
    { 0x1680, 0x16FF }, { 0x1700, 0x177F }, { 0x2C00, 0x2C5F }, { 0x2C80, 0x2CFF }, { 0x10000, 0x1047F },
    { 0x104B0, 0x10FFF }, { 0x11000, 0x1165F }, { 0x11680, 0x11FFF }, { 0x12000, 0x1254F }, { 0x13000, 0x1345F }
};
static const DWRITE_UNICODE_RANGE RxKhmerRanges[] = { { 0x1780, 0x17FF }, { 0x19E0, 0x19FF } };
static const DWRITE_UNICODE_RANGE RxMongolianRanges[] = { { 0x1800, 0x18AF }, { 0x11660, 0x1167F } };
static const DWRITE_UNICODE_RANGE RxTaiRanges[] = { { 0x1950, 0x19DF } };
static const DWRITE_UNICODE_RANGE RxBugineseRanges[] = { { 0x1A00, 0x1A1F } };
static const DWRITE_UNICODE_RANGE RxYiRanges[] = { { 0xA000, 0xA4CF } };
static const DWRITE_UNICODE_RANGE RxJavaneseRanges[] = { { 0xA980, 0xA9DF } };
static const DWRITE_UNICODE_RANGE RxSymbolRanges[] =
{
    { 0x2190, 0x2BFF }, { 0x2E00, 0x2E7F }, { 0x4DC0, 0x4DFF }, { 0x1D000, 0x1D24F }, { 0x1D300, 0x1D35F },
    { 0x1F000, 0x1FBFF }
};
static const DWRITE_UNICODE_RANGE RxMathRanges[] = { { 0x1D400, 0x1D7FF }, { 0x1EE00, 0x1EEFF } };
static const DWRITE_UNICODE_RANGE RxHanRanges[] =
{
    { 0x2E80, 0x2FFF }, { 0x3000, 0x303F }, { 0x3100, 0x312F }, { 0x3190, 0x31EF }, { 0x3200, 0x33FF },
    { 0x3400, 0x4DBF }, { 0x4E00, 0x9FFF }, { 0xF900, 0xFAFF }, { 0xFE10, 0xFE1F }, { 0xFE30, 0xFE6F },
    { 0xFF00, 0xFF64 }, { 0xFFA0, 0xFFEF }, { 0x20000, 0x323AF }
};
/* Where the traditional face is the first choice for any locale */
static const DWRITE_UNICODE_RANGE RxVerticalRanges[] = { { 0x31A0, 0x31BF }, { 0xFE10, 0xFE1F }, { 0xFE30, 0xFE4F } };
static const DWRITE_UNICODE_RANGE RxRomanRanges[] = { { 0x2160, 0x216B }, { 0x2170, 0x2179 } };
static const DWRITE_UNICODE_RANGE RxHanExtensionRanges[] = { { 0x20000, 0x323AF } };
static const DWRITE_UNICODE_RANGE RxKanaRanges[] =
{
    { 0x3040, 0x30FF }, { 0x31F0, 0x31FF }, { 0xFF65, 0xFF9F }, { 0x1AFF0, 0x1B16F }
};

static const PCWSTR RxEmojiFamilies[] = { L"Segoe UI Emoji", L"Segoe UI Symbol", nullptr };
static const PCWSTR RxCollectionFamilies[] = { L"Sans Serif Collection", nullptr };
static const PCWSTR RxLatinFamilies[] =
{
    L"Segoe UI", L"Segoe UI Symbol", L"Tahoma", L"Arial", L"Microsoft Sans Serif", L"Lucida Sans Unicode", nullptr
};
static const PCWSTR RxArmenianFamilies[] = { L"Segoe UI", L"Sylfaen", nullptr };
static const PCWSTR RxRtlFamilies[] = { L"Segoe UI", L"Arial", L"Tahoma", L"Microsoft Sans Serif", nullptr };
static const PCWSTR RxSyriacFamilies[] = { L"Segoe UI Historic", L"Estrangelo Edessa", nullptr };
static const PCWSTR RxThaanaFamilies[] = { L"MV Boli", nullptr };
static const PCWSTR RxAfricanFamilies[] = { L"Ebrima", nullptr };
static const PCWSTR RxEthiopicFamilies[] = { L"Ebrima", L"Nyala", nullptr };
static const PCWSTR RxIndicFamilies[] =
{
    L"Nirmala UI", L"Mangal", L"Vrinda", L"Raavi", L"Shruti", L"Kalinga", L"Latha", L"Gautami", L"Tunga", L"Kartika",
    L"Iskoola Pota", nullptr
};
static const PCWSTR RxThaiFamilies[] = { L"Leelawadee UI", L"Leelawadee", L"Tahoma", L"Microsoft Sans Serif", nullptr };
static const PCWSTR RxLaoFamilies[] = { L"Leelawadee UI", L"Lao UI", L"DokChampa", nullptr };
static const PCWSTR RxTibetanFamilies[] = { L"Microsoft Himalaya", nullptr };
static const PCWSTR RxMyanmarFamilies[] = { L"Myanmar Text", nullptr };
static const PCWSTR RxGeorgianFamilies[] = { L"Segoe UI", L"Sylfaen", nullptr };
static const PCWSTR RxKoreanFamilies[] = { L"Malgun Gothic", L"Gulim", L"Dotum", L"Batang", nullptr };
static const PCWSTR RxKoreanHanFamilies[] =
{
    L"Malgun Gothic", L"Gulim", L"Dotum", L"Batang", L"Microsoft JhengHei UI", nullptr
};
static const PCWSTR RxAmericanFamilies[] = { L"Gadugi", L"Euphemia", L"Plantagenet Cherokee", nullptr };
static const PCWSTR RxHistoricFamilies[] = { L"Segoe UI Historic", L"Segoe UI Symbol", nullptr };
static const PCWSTR RxKhmerFamilies[] = { L"Leelawadee UI", L"Khmer UI", L"DaunPenh", L"MoolBoran", nullptr };
static const PCWSTR RxMongolianFamilies[] = { L"Mongolian Baiti", nullptr };
static const PCWSTR RxTaiFamilies[] = { L"Microsoft Tai Le", L"Microsoft New Tai Lue", nullptr };
static const PCWSTR RxBugineseFamilies[] = { L"Leelawadee UI", nullptr };
static const PCWSTR RxYiFamilies[] = { L"Microsoft Yi Baiti", nullptr };
static const PCWSTR RxJavaneseFamilies[] = { L"Javanese Text", nullptr };
static const PCWSTR RxSymbolFamilies[] =
{
    L"Segoe UI", L"Segoe UI Symbol", L"Segoe UI Emoji", L"Cambria Math", L"Arial", L"Lucida Sans Unicode", nullptr
};
static const PCWSTR RxMathFamilies[] = { L"Segoe UI Symbol", L"Cambria Math", nullptr };
static const PCWSTR RxTraditionalFamilies[] =
{
    L"Microsoft JhengHei UI", L"Microsoft JhengHei", L"PMingLiU", L"MingLiU", nullptr
};
static const PCWSTR RxSimplifiedFamilies[] =
{
    L"Microsoft YaHei UI", L"Microsoft YaHei", L"SimSun", L"NSimSun", nullptr
};
static const PCWSTR RxJapaneseFamilies[] =
{
    L"Yu Gothic UI", L"Meiryo UI", L"Meiryo", L"MS UI Gothic", L"MS Gothic", L"MS PGothic", nullptr
};
static const PCWSTR RxEastAsianFamilies[] =
{
    L"Yu Gothic UI", L"Microsoft JhengHei UI", L"Microsoft YaHei UI", L"Malgun Gothic", L"Meiryo UI", L"SimSun",
    L"MS Gothic", L"SimSun-ExtB", L"MingLiU-ExtB", L"Segoe UI Symbol", nullptr
};

/* Traditional comes first so a plain zh prefix only catches what is left */
static const PCWSTR RxVerticalFamilies[] = { L"Microsoft JhengHei UI", L"Yu Gothic UI", L"Microsoft YaHei UI", nullptr };
static const PCWSTR RxRomanFamilies[] = { L"Segoe UI", L"Yu Gothic UI", L"Microsoft JhengHei UI", L"Tahoma", nullptr };
static const PCWSTR RxHanExtensionFamilies[] = { L"MingLiU-ExtB", L"SimSun-ExtB", nullptr };
static const PCWSTR RxSimplifiedExtensionFamilies[] = { L"SimSun-ExtB", L"MingLiU-ExtB", nullptr };
static const PCWSTR RxTraditionalExtensionFamilies[] = { L"MingLiU-ExtB", L"PMingLiU-ExtB", L"SimSun-ExtB", nullptr };

static const PCWSTR RxTraditionalLocales[] = { L"zh-Hant", L"zh-TW", L"zh-HK", L"zh-MO", nullptr };
static const PCWSTR RxSimplifiedLocales[] = { L"zh-Hans", L"zh-CN", L"zh-SG", L"zh", nullptr };
static const PCWSTR RxJapaneseLocales[] = { L"ja", nullptr };
static const PCWSTR RxKoreanLocales[] = { L"ko", nullptr };

/* Locale specific entries first; among the rest, earlier entries win where ranges overlap. Chinese and Korean
   Han entries leave out the last Han range, where the extension faces come first. */
static const RxSystemMapping RxSystemMappings[] =
{
    { RxHanRanges, _countof(RxHanRanges) - 1, RxTraditionalFamilies, RxTraditionalLocales },
    { RxHanExtensionRanges, _countof(RxHanExtensionRanges), RxTraditionalExtensionFamilies, RxTraditionalLocales },
    { RxHanRanges, _countof(RxHanRanges) - 1, RxSimplifiedFamilies, RxSimplifiedLocales },
    { RxHanExtensionRanges, _countof(RxHanExtensionRanges), RxSimplifiedExtensionFamilies, RxSimplifiedLocales },
    { RxHanRanges, _countof(RxHanRanges), RxJapaneseFamilies, RxJapaneseLocales },
    { RxHanRanges, _countof(RxHanRanges) - 1, RxKoreanHanFamilies, RxKoreanLocales },
    { RxEmojiRanges, _countof(RxEmojiRanges), RxEmojiFamilies, nullptr },
    { RxCollectionRanges, _countof(RxCollectionRanges), RxCollectionFamilies, nullptr },
    { RxRomanRanges, _countof(RxRomanRanges), RxRomanFamilies, nullptr },
    { RxLatinRanges, _countof(RxLatinRanges), RxLatinFamilies, nullptr },
    { RxArmenianRanges, _countof(RxArmenianRanges), RxArmenianFamilies, nullptr },
    { RxHebrewRanges, _countof(RxHebrewRanges), RxRtlFamilies, nullptr },
    { RxArabicRanges, _countof(RxArabicRanges), RxRtlFamilies, nullptr },
    { RxSyriacRanges, _countof(RxSyriacRanges), RxSyriacFamilies, nullptr },
    { RxThaanaRanges, _countof(RxThaanaRanges), RxThaanaFamilies, nullptr },
    { RxAfricanRanges, _countof(RxAfricanRanges), RxAfricanFamilies, nullptr },
    { RxEthiopicRanges, _countof(RxEthiopicRanges), RxEthiopicFamilies, nullptr },
    { RxIndicRanges, _countof(RxIndicRanges), RxIndicFamilies, nullptr },
    { RxThaiRanges, _countof(RxThaiRanges), RxThaiFamilies, nullptr },
    { RxLaoRanges, _countof(RxLaoRanges), RxLaoFamilies, nullptr },
    { RxTibetanRanges, _countof(RxTibetanRanges), RxTibetanFamilies, nullptr },
    { RxMyanmarRanges, _countof(RxMyanmarRanges), RxMyanmarFamilies, nullptr },
    { RxGeorgianRanges, _countof(RxGeorgianRanges), RxGeorgianFamilies, nullptr },
    { RxHangulRanges, _countof(RxHangulRanges), RxKoreanFamilies, nullptr },
    { RxAmericanRanges, _countof(RxAmericanRanges), RxAmericanFamilies, nullptr },
    { RxHistoricRanges, _countof(RxHistoricRanges), RxHistoricFamilies, nullptr },
    { RxKhmerRanges, _countof(RxKhmerRanges), RxKhmerFamilies, nullptr },
    { RxMongolianRanges, _countof(RxMongolianRanges), RxMongolianFamilies, nullptr },
    { RxTaiRanges, _countof(RxTaiRanges), RxTaiFamilies, nullptr },
    { RxBugineseRanges, _countof(RxBugineseRanges), RxBugineseFamilies, nullptr },
    { RxYiRanges, _countof(RxYiRanges), RxYiFamilies, nullptr },
    { RxJavaneseRanges, _countof(RxJavaneseRanges), RxJavaneseFamilies, nullptr },
    { RxSymbolRanges, _countof(RxSymbolRanges), RxSymbolFamilies, nullptr },
    { RxMathRanges, _countof(RxMathRanges), RxMathFamilies, nullptr },
    { RxKanaRanges, _countof(RxKanaRanges), RxJapaneseFamilies, nullptr },
    { RxVerticalRanges, _countof(RxVerticalRanges), RxVerticalFamilies, nullptr },
    { RxHanExtensionRanges, _countof(RxHanExtensionRanges), RxHanExtensionFamilies, nullptr },
    { RxHangulRanges, _countof(RxHangulRanges), RxEastAsianFamilies, nullptr },
    { RxKanaRanges, _countof(RxKanaRanges), RxEastAsianFamilies, nullptr },
    { RxHanRanges, _countof(RxHanRanges), RxEastAsianFamilies, nullptr },
};

/* Character classes */

/* Invisible format characters, which stay with the font of the character before them */
static
BOOLEAN
RxIsIgnorable(
    _In_ UINT32 Cp)
{
    return Cp == 0x034F || (Cp >= 0x180B && Cp <= 0x180F) || (Cp >= 0x200B && Cp <= 0x200F) ||
           (Cp >= 0x202A && Cp <= 0x202E) || (Cp >= 0x2060 && Cp <= 0x206F) || (Cp >= 0xFE00 && Cp <= 0xFE0F) ||
           Cp == 0xFEFF || (Cp >= 0xE0000 && Cp <= 0xE0FFF);
}

/* Marks drawn on the character before them */
static
BOOLEAN
RxIsCombining(
    _In_ UINT32 Cp)
{
    return (Cp >= 0x0300 && Cp <= 0x036F) || (Cp >= 0x0483 && Cp <= 0x0489) || (Cp >= 0x1AB0 && Cp <= 0x1AFF) ||
           (Cp >= 0x1DC0 && Cp <= 0x1DFF) || (Cp >= 0x20D0 && Cp <= 0x20FF) || (Cp >= 0xFE20 && Cp <= 0xFE2F);
}

/* Spaces, digits and punctuation that text of any script uses */
static
BOOLEAN
RxIsCommon(
    _In_ UINT32 Cp)
{
    return Cp <= 0x0040 || (Cp >= 0x005B && Cp <= 0x0060) || (Cp >= 0x007B && Cp <= 0x00BF) || Cp == 0x00D7 ||
           Cp == 0x00F7 || (Cp >= 0x2000 && Cp <= 0x206F) || Cp == 0x3000;
}

/* Controls, private use, surrogates and noncharacters belong to no family in particular */
static
BOOLEAN
RxIsScannable(
    _In_ UINT32 Cp)
{
    if (Cp < 0x20 || (Cp >= 0x7F && Cp < 0xA0) || RxIsIgnorable(Cp))
        return FALSE;
    if ((Cp >= 0xD800 && Cp <= 0xF8FF) || (Cp >= 0xFDD0 && Cp <= 0xFDEF) || (Cp & 0xFFFE) == 0xFFFE || Cp >= 0xF0000)
        return FALSE;
    return TRUE;
}

static
BOOLEAN
RxRangesHold(
    _In_ const std::vector<DWRITE_UNICODE_RANGE>& Ranges,
    _In_ UINT32 Cp)
{
    for (const auto& Range : Ranges)
    {
        if (Cp >= Range.first && Cp <= Range.last)
            return TRUE;
    }
    return FALSE;
}

/* A mapping locale matches the text locale itself and any more specific tag under it */
static
BOOLEAN
RxLocaleMatches(
    _In_z_ PCWSTR Locale,
    _In_ const std::wstring& Wanted)
{
    SIZE_T Length = Wanted.size();

    return _wcsnicmp(Locale, Wanted.c_str(), Length) == 0 && (Locale[Length] == L'\0' || Locale[Length] == L'-');
}

/* Source text */

struct RxSourceChar
{
    UINT32 CodePoint;
    UINT32 Units;
};

/* Text of an analysis source decoded on demand; a pair split over two chunks reads as one character */
class RxSourceText
{
public:
    RxSourceText(
        _In_ IDWriteTextAnalysisSource* TextSource,
        _In_ UINT32 Position,
        _In_ UINT32 Length) : Source(TextSource), Next(Position), Chunk(nullptr), ChunkStart(0), ChunkLength(0)
    {
        End = static_cast<UINT32>(std::min<UINT64>(static_cast<UINT64>(Position) + Length, MAXUINT32));
    }

    /* FALSE past the end of the range or of the text */
    BOOLEAN
    At(
        _In_ SIZE_T Index,
        _Out_ RxSourceChar* Char)
    {
        *Char = {};
        while (Chars.size() <= Index)
        {
            RxSourceChar Read = { 0, 1 };
            WCHAR High;
            WCHAR Low;

            if (!UnitAt(Next, &High))
                return FALSE;
            Read.CodePoint = High;
            if (High >= 0xD800 && High <= 0xDBFF && UnitAt(Next + 1, &Low) && Low >= 0xDC00 && Low <= 0xDFFF)
            {
                Read.CodePoint = 0x10000 + ((High - 0xD800) << 10) + (Low - 0xDC00);
                Read.Units = 2;
            }
            Next += Read.Units;
            Chars.push_back(Read);
        }
        *Char = Chars[Index];
        return TRUE;
    }

private:
    BOOLEAN
    UnitAt(
        _In_ UINT32 Offset,
        _Out_ WCHAR* Unit)
    {
        *Unit = 0;
        if (Offset >= End)
            return FALSE;

        if (Chunk == nullptr || Offset < ChunkStart || Offset - ChunkStart >= ChunkLength)
        {
            Chunk = nullptr;
            ChunkLength = 0;
            if (FAILED(Source->GetTextAtPosition(Offset, &Chunk, &ChunkLength)) || Chunk == nullptr || ChunkLength == 0)
            {
                /* The text ends here */
                Chunk = nullptr;
                End = Offset;
                return FALSE;
            }
            ChunkStart = Offset;
        }
        *Unit = Chunk[Offset - ChunkStart];
        return TRUE;
    }

    IDWriteTextAnalysisSource* Source;
    UINT32 Next;
    UINT32 End;
    const WCHAR* Chunk;
    UINT32 ChunkStart;
    UINT32 ChunkLength;
    std::vector<RxSourceChar> Chars;
};

/* Fonts */

static
BOOLEAN
RxFontHas(
    _In_ IDWriteFont* Font,
    _In_ UINT32 Cp)
{
    BOOL Exists = FALSE;

    return SUCCEEDED(Font->HasCharacter(Cp, &Exists)) && Exists;
}

/* Code units of the first Chars characters a font can draw; ignorables after the first ride along */
static
UINT32
RxSupportedUnits(
    _In_ IDWriteFont* Font,
    _Inout_ RxSourceText& Text,
    _In_ SIZE_T Chars)
{
    RxSourceChar Char;
    UINT32 Units = 0;

    for (SIZE_T Index = 0; Index < Chars && Text.At(Index, &Char); Index++)
    {
        if ((Index == 0 || !RxIsIgnorable(Char.CodePoint)) && !RxFontHas(Font, Char.CodePoint))
            break;
        Units += Char.Units;
    }
    return Units;
}

static
HRESULT
RxFamilyFont(
    _In_ IDWriteFontCollection* Collection,
    _In_ UINT32 Family,
    _In_ DWRITE_FONT_WEIGHT Weight,
    _In_ DWRITE_FONT_STYLE Style,
    _In_ DWRITE_FONT_STRETCH Stretch,
    _COM_Outptr_ IDWriteFont** Font)
{
    RxComPtr<IDWriteFontFamily> Found;
    HRESULT hr;

    *Font = nullptr;
    hr = Collection->GetFontFamily(Family, Found.Put());
    if (FAILED(hr))
        return hr;
    return Found->GetFirstMatchingFont(Weight, Stretch, Style, Font);
}

/* The closest face of a named family, from the cache for the collection it covers */
static
HRESULT
RxNamedFont(
    _In_ IDWriteFontCollection* Collection,
    _In_opt_ RxFallbackCache* Cache,
    _In_z_ PCWSTR Name,
    _In_ DWRITE_FONT_WEIGHT Weight,
    _In_ DWRITE_FONT_STYLE Style,
    _In_ DWRITE_FONT_STRETCH Stretch,
    _Out_opt_ UINT32* FamilyIndex,
    _COM_Outptr_ IDWriteFont** Font)
{
    UINT32 Index = 0;
    BOOL Exists = FALSE;
    HRESULT hr;

    *Font = nullptr;
    hr = Collection->FindFamilyName(Name, &Index, &Exists);
    if (FAILED(hr))
        return hr;
    if (!Exists)
        return DWRITE_E_NOFONT;
    if (FamilyIndex != nullptr)
        *FamilyIndex = Index;
    if (Cache != nullptr && Collection == Cache->Collection())
        return Cache->MatchingFont(Index, Weight, Style, Stretch, Font);
    return RxFamilyFont(Collection, Index, Weight, Style, Stretch, Font);
}

/* RxFallbackCache */

HRESULT
RxFallbackCache::Create(
    _In_ RxFontCollection* Collection,
    _Outptr_ RxFallbackCache** Cache)
{
    RxFallbackCache* Result;

    *Cache = nullptr;
    Result = new (std::nothrow) RxFallbackCache();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    Result->System = RxComPtr<RxFontCollection>(Collection);
    Result->Families.resize(Collection->FamilyList().size());
    *Cache = Result;
    return S_OK;
}

VOID
RxFallbackCache::Load(
    _Inout_ FamilyRanges& Slot,
    _In_ UINT32 Family)
{
    const RxFontSetData* Data = System->Data();
    const std::vector<RxFontItem>& Items = System->FamilyList()[Family].Items;
    DWRITE_FONT_SIMULATIONS Simulations;
    RxFaceTables* Tables;
    SIZE_T Index;

    Slot.Loaded = TRUE;
    Index = RxFindMatchingItem(Data, Items, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                               DWRITE_FONT_STYLE_NORMAL, &Simulations);
    if (Index == MAXSIZE_T)
        return;

    /* Symbol fonts claim the Latin range through their F000 mapping */
    const RxFontEntry& Entry = Data->Entries[Items[Index].Entry];
    if (Entry.IsSymbol || FAILED(RxFaceTables::Create(Entry.File.Get(), Entry.FaceIndex, &Tables)))
        return;

    Tables->UnicodeRanges(Slot.Ranges);
    Tables->Release();
    std::sort(Slot.Ranges.begin(), Slot.Ranges.end(),
              [](const DWRITE_UNICODE_RANGE& A, const DWRITE_UNICODE_RANGE& B) { return A.first < B.first; });
    Slot.Usable = TRUE;
}

BOOLEAN
RxFallbackCache::MayHave(
    _In_ UINT32 Family,
    _In_ UINT32 CodePoint)
{
    FamilyRanges* Slot;
    BOOLEAN Loaded;

    if (Family >= Families.size())
        return FALSE;
    Slot = &Families[Family];

    AcquireSRWLockShared(&Lock);
    Loaded = Slot->Loaded;
    ReleaseSRWLockShared(&Lock);
    if (!Loaded)
    {
        AcquireSRWLockExclusive(&Lock);
        if (!Slot->Loaded)
            Load(*Slot, Family);
        ReleaseSRWLockExclusive(&Lock);
    }

    if (!Slot->Usable)
        return FALSE;

    /* The last segment starting at or below CodePoint is the only one that can hold it */
    auto Found = std::upper_bound(Slot->Ranges.begin(), Slot->Ranges.end(), CodePoint,
                                  [](UINT32 Value, const DWRITE_UNICODE_RANGE& Range) { return Value < Range.first; });
    return Found != Slot->Ranges.begin() && CodePoint <= (Found - 1)->last;
}

BOOLEAN
RxFallbackCache::AnyMayHave(
    _In_ UINT32 CodePoint)
{
    for (UINT32 Family = 0; Family < Families.size(); Family++)
    {
        if (MayHave(Family, CodePoint))
            return TRUE;
    }
    return FALSE;
}

HRESULT
RxFallbackCache::MatchingFont(
    _In_ UINT32 Family,
    _In_ DWRITE_FONT_WEIGHT Weight,
    _In_ DWRITE_FONT_STYLE Style,
    _In_ DWRITE_FONT_STRETCH Stretch,
    _COM_Outptr_ IDWriteFont** Font)
{
    RxComPtr<IDWriteFont> Created;
    HRESULT hr;

    *Font = nullptr;
    AcquireSRWLockShared(&Lock);
    for (const auto& Entry : Matched)
    {
        if (Entry.Family == Family && Entry.Weight == Weight && Entry.Style == Style && Entry.Stretch == Stretch)
        {
            *Font = Entry.Font.Copy();
            break;
        }
    }
    ReleaseSRWLockShared(&Lock);
    if (*Font != nullptr)
        return S_OK;

    hr = RxFamilyFont(System.Get(), Family, Weight, Style, Stretch, Created.Put());
    if (FAILED(hr))
        return hr;

    /* Loading the face now leaves nothing for threads sharing the font to race on */
    RxFontHas(Created.Get(), 0x20);

    AcquireSRWLockExclusive(&Lock);
    for (const auto& Entry : Matched)
    {
        if (Entry.Family == Family && Entry.Weight == Weight && Entry.Style == Style && Entry.Stretch == Stretch)
        {
            Created = Entry.Font;
            break;
        }
    }
    if (Matched.size() < RxMaxCachedFonts)
        Matched.push_back({ Family, Weight, Style, Stretch, Created });
    ReleaseSRWLockExclusive(&Lock);

    *Font = Created.Copy();
    return S_OK;
}

/* RxFontFallback */

HRESULT
RxFontFallback::Create(
    _In_opt_ RxWriteFactory* Owner,
    _In_ RxFontCollection* System,
    _In_ const std::vector<RxFallbackMapping>& Mappings,
    _In_opt_ RxFallbackCache* Cache,
    _Outptr_ RxFontFallback** Fallback)
{
    RxFontFallback* Result;

    *Fallback = nullptr;
    Result = new (std::nothrow) RxFontFallback();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    Result->Factory = RxComPtr<RxWriteFactory>(Owner);
    Result->System = RxComPtr<RxFontCollection>(System);
    Result->Mappings = Mappings;
    Result->Scan = RxComPtr<RxFallbackCache>(Cache);
    *Fallback = Result;
    return S_OK;
}

HRESULT
RxFontFallback::CreateSystem(
    _In_ RxFontCollection* System,
    _Outptr_ RxFontFallback** Fallback)
{
    std::vector<RxFallbackMapping> Mappings;
    RxFallbackCache* Cache;
    HRESULT hr;

    *Fallback = nullptr;

    /* One mapping per range, since a run never crosses from one built in range to another */
    for (const auto& Entry : RxSystemMappings)
    {
        for (UINT32 Range = 0; Range < Entry.RangeCount; Range++)
        {
            RxFallbackMapping Mapping;

            Mapping.Ranges.push_back(Entry.Ranges[Range]);
            for (const PCWSTR* Family = Entry.Families; *Family != nullptr; Family++)
                Mapping.Families.push_back(*Family);
            for (const PCWSTR* Locale = Entry.Locales; Locale != nullptr && *Locale != nullptr; Locale++)
                Mapping.Locales.push_back(*Locale);
            Mapping.Scale = 1.0f;
            Mappings.push_back(std::move(Mapping));
        }
    }

    hr = RxFallbackCache::Create(System, &Cache);
    if (FAILED(hr))
        return hr;

    hr = Create(nullptr, System, Mappings, Cache, Fallback);
    Cache->Release();
    return hr;
}

RxFontFallback*
RxFontFallback::FromInterface(
    _In_opt_ IUnknown* Fallback)
{
    RxFontFallback* Result = nullptr;

    if (Fallback == nullptr || FAILED(Fallback->QueryInterface(RxFallbackIid, reinterpret_cast<void**>(&Result))))
        return nullptr;
    Result->Release();
    return Result;
}

HRESULT
STDMETHODCALLTYPE
RxFontFallback::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == RxFallbackIid || riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFallback) ||
        riid == __uuidof(IDWriteFontFallback1))
    {
        *ppvObject = static_cast<IDWriteFontFallback1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxFontFallback)

VOID
RxFontFallback::Applicable(
    _In_z_ PCWSTR Locale,
    _In_opt_z_ PCWSTR BaseFamily,
    _Out_ std::vector<UINT32>& Order) const
{
    Order.clear();
    for (UINT32 Pass = 0; Pass < 2; Pass++)
    {
        for (UINT32 Index = 0; Index < Mappings.size(); Index++)
        {
            const RxFallbackMapping& Mapping = Mappings[Index];
            BOOLEAN Matches = FALSE;

            if (!Mapping.BaseFamily.empty() &&
                (BaseFamily == nullptr || _wcsicmp(BaseFamily, Mapping.BaseFamily.c_str()) != 0))
                continue;

            if (Pass == 0)
            {
                for (const auto& Wanted : Mapping.Locales)
                {
                    if (RxLocaleMatches(Locale, Wanted))
                    {
                        Matches = TRUE;
                        break;
                    }
                }
            }
            else
            {
                Matches = Mapping.Locales.empty();
            }

            if (Matches)
                Order.push_back(Index);
        }
    }
}

INT32
RxFontFallback::Classify(
    _In_ const std::vector<UINT32>& Order,
    _In_ UINT32 CodePoint) const
{
    for (UINT32 Index : Order)
    {
        if (RxRangesHold(Mappings[Index].Ranges, CodePoint))
            return static_cast<INT32>(Index);
    }
    return (Scan.Get() != nullptr && RxIsScannable(CodePoint)) ? RxScanOnly : RxNoMapping;
}

HRESULT
STDMETHODCALLTYPE
RxFontFallback::MapCharacters(
    IDWriteTextAnalysisSource* analysisSource,
    UINT32 textPosition,
    UINT32 textLength,
    _In_opt_ IDWriteFontCollection* baseFontCollection,
    _In_opt_z_ wchar_t const* baseFamilyName,
    DWRITE_FONT_WEIGHT baseWeight,
    DWRITE_FONT_STYLE baseStyle,
    DWRITE_FONT_STRETCH baseStretch,
    _Out_range_(0, textLength) UINT32* mappedLength,
    _COM_Outptr_result_maybenull_ IDWriteFont** mappedFont,
    _Out_ FLOAT* scale)
{
    IDWriteFontCollection* SystemCollection = System.Get();
    std::vector<RxComPtr<IDWriteFont>> Tried;
    std::vector<UINT32> TriedFamilies;
    std::vector<UINT32> Order;
    RxComPtr<IDWriteFont> Base;
    PCWSTR Locale = nullptr;
    UINT32 LocaleLength = 0;
    RxSourceChar First;
    RxSourceChar Char;
    BOOLEAN Scanned = FALSE;
    SIZE_T RunChars;
    UINT32 Limit;
    UINT32 Units;
    INT32 Key;
    HRESULT hr;

    if (mappedLength == nullptr || mappedFont == nullptr || scale == nullptr)
        return E_INVALIDARG;
    *mappedLength = 0;
    *mappedFont = nullptr;
    *scale = 1.0f;
    if (analysisSource == nullptr)
        return E_INVALIDARG;
    if (textLength == 0)
        return S_OK;

    hr = analysisSource->GetLocaleName(textPosition, &LocaleLength, &Locale);
    if (FAILED(hr))
        return hr;
    if (Locale == nullptr)
        Locale = L"";

    RxSourceText Text(analysisSource, textPosition, textLength);
    if (!Text.At(0, &First))
        return S_OK;

    /* The base family keeps every leading character it has */
    if (baseFamilyName != nullptr && *baseFamilyName != L'\0')
    {
        RxNamedFont(baseFontCollection != nullptr ? baseFontCollection : SystemCollection, Scan.Get(), baseFamilyName,
                    baseWeight, baseStyle, baseStretch, nullptr, Base.Put());
        if (Base.Get() != nullptr)
        {
            Units = RxSupportedUnits(Base.Get(), Text, MAXSIZE_T);
            if (Units != 0)
            {
                *mappedLength = Units;
                *mappedFont = Base.Copy();
                return S_OK;
            }
        }
    }

    /* The run is the characters the first one's mapping holds, within its locale */
    Applicable(Locale, baseFamilyName, Order);
    Key = Classify(Order, First.CodePoint);
    Limit = LocaleLength != 0 ? std::min(textLength, LocaleLength) : textLength;
    RunChars = 1;
    Units = First.Units;
    while (Text.At(RunChars, &Char) && Units + Char.Units <= Limit)
    {
        UINT32 Cp = Char.CodePoint;

        if (!RxIsIgnorable(Cp))
        {
            /* Shared punctuation goes back to the base font when it has it */
            if (RxIsCommon(Cp) && Base.Get() != nullptr && RxFontHas(Base.Get(), Cp))
                break;
            if (!RxIsCombining(Cp) && !RxIsCommon(Cp) && Classify(Order, Cp) != Key)
                break;
        }
        Units += Char.Units;
        RunChars++;
    }

    /* Families of every applicable mapping holding the character, from the run's own on */
    if (Key >= 0)
    {
        BOOLEAN Reached = FALSE;

        for (UINT32 Index : Order)
        {
            const RxFallbackMapping& Mapping = Mappings[Index];
            IDWriteFontCollection* Collection;

            Reached = Reached || Index == static_cast<UINT32>(Key);
            if (!Reached || !RxRangesHold(Mapping.Ranges, First.CodePoint))
                continue;

            Collection = Mapping.Collection.Get() != nullptr ? Mapping.Collection.Get() : SystemCollection;
            for (const auto& Family : Mapping.Families)
            {
                RxComPtr<IDWriteFont> Font;
                UINT32 FamilyIndex = MAXUINT32;

                if (FAILED(RxNamedFont(Collection, Scan.Get(), Family.c_str(), baseWeight, baseStyle, baseStretch,
                                       &FamilyIndex, Font.Put())))
                    continue;
                if (Collection == SystemCollection)
                {
                    if (std::find(TriedFamilies.begin(), TriedFamilies.end(), FamilyIndex) != TriedFamilies.end())
                        continue;
                    TriedFamilies.push_back(FamilyIndex);
                }

                Units = RxSupportedUnits(Font.Get(), Text, RunChars);
                if (Units != 0)
                {
                    *mappedLength = Units;
                    *mappedFont = Font.Copy();
                    *scale = Mapping.Scale;
                    return S_OK;
                }
                Tried.push_back(std::move(Font));
            }
        }
    }

    /* Any installed family that has the character */
    if (Scan.Get() != nullptr && RxIsScannable(First.CodePoint))
    {
        IDWriteFontCollection* Collection = Scan->Collection();

        Scanned = TRUE;
        for (UINT32 Family = 0; Family < Scan->FamilyCount(); Family++)
        {
            RxComPtr<IDWriteFont> Font;

            if (Collection == SystemCollection &&
                std::find(TriedFamilies.begin(), TriedFamilies.end(), Family) != TriedFamilies.end())
                continue;
            if (!Scan->MayHave(Family, First.CodePoint) ||
                FAILED(Scan->MatchingFont(Family, baseWeight, baseStyle, baseStretch, Font.Put())))
                continue;

            Units = RxSupportedUnits(Font.Get(), Text, RunChars);
            if (Units != 0)
            {
                *mappedLength = Units;
                *mappedFont = Font.Copy();
                return S_OK;
            }
        }
    }

    /* Nothing draws it: report the characters no candidate has, with no font */
    Units = First.Units;
    for (SIZE_T Index = 1; Index < RunChars && Text.At(Index, &Char); Index++)
    {
        BOOLEAN Drawable = FALSE;

        if (!RxIsIgnorable(Char.CodePoint))
        {
            Drawable = Base.Get() != nullptr && RxFontHas(Base.Get(), Char.CodePoint);
            for (SIZE_T Font = 0; Font < Tried.size() && !Drawable; Font++)
                Drawable = RxFontHas(Tried[Font].Get(), Char.CodePoint);
            if (!Drawable && Scanned)
                Drawable = Scan->AnyMayHave(Char.CodePoint);
        }
        if (Drawable)
            break;
        Units += Char.Units;
    }
    *mappedLength = Units;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFallback::MapCharacters(
    IDWriteTextAnalysisSource* analysisSource,
    UINT32 textPosition,
    UINT32 textLength,
    _In_opt_ IDWriteFontCollection* baseFontCollection,
    _In_opt_z_ WCHAR const* baseFamilyName,
    _In_reads_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE const* fontAxisValues,
    UINT32 fontAxisValueCount,
    _Deref_out_range_(0, textLength) UINT32* mappedLength,
    _Out_ FLOAT* scale,
    _COM_Outptr_ IDWriteFontFace5** mappedFontFace)
{
    std::vector<RxAxisValue> Values;
    RxFaceHeader Defaults = {};
    RxComPtr<IDWriteFont> Font;
    RxComPtr<IDWriteFontFace> Face;
    RxComPtr<IDWriteFontFace5> Face5;
    HRESULT hr;

    if (mappedFontFace == nullptr)
        return E_INVALIDARG;
    *mappedFontFace = nullptr;
    if (fontAxisValues == nullptr && fontAxisValueCount != 0)
    {
        if (mappedLength != nullptr)
            *mappedLength = 0;
        if (scale != nullptr)
            *scale = 1.0f;
        return E_INVALIDARG;
    }

    /* Axes a family is matched on read as weight, stretch and style; the face is then placed on all of them */
    for (UINT32 Index = 0; Index < fontAxisValueCount; Index++)
        Values.push_back({ fontAxisValues[Index].axisTag, RxFloatToFixed(fontAxisValues[Index].value) });

    hr = MapCharacters(analysisSource, textPosition, textLength, baseFontCollection, baseFamilyName,
                       static_cast<DWRITE_FONT_WEIGHT>(RxAxisWeight(Defaults, Values)),
                       static_cast<DWRITE_FONT_STYLE>(RxAxisStyle(Defaults, Values)),
                       static_cast<DWRITE_FONT_STRETCH>(RxAxisStretch(Defaults, Values)), mappedLength, Font.Put(),
                       scale);
    if (FAILED(hr) || Font.Get() == nullptr)
        return hr;

    hr = Font->CreateFontFace(Face.Put());
    if (SUCCEEDED(hr))
        hr = Face->QueryInterface(__uuidof(IDWriteFontFace5), Face5.PutVoid());
    if (FAILED(hr))
        return hr;

    if (fontAxisValueCount != 0 && Face5->HasVariations())
    {
        RxComPtr<IDWriteFontResource> Resource;
        RxComPtr<IDWriteFontFace5> Placed;

        if (SUCCEEDED(Face5->GetFontResource(Resource.Put())) &&
            SUCCEEDED(Resource->CreateFontFace(Face5->GetSimulations(), fontAxisValues, fontAxisValueCount,
                                               Placed.Put())))
            Face5 = std::move(Placed);
    }

    *mappedFontFace = Face5.Copy();
    return S_OK;
}

/* RxFontFallbackBuilder */

HRESULT
RxFontFallbackBuilder::Create(
    _In_ RxWriteFactory* Owner,
    _Outptr_ RxFontFallbackBuilder** Builder)
{
    *Builder = new (std::nothrow) RxFontFallbackBuilder();
    if (*Builder == nullptr)
        return E_OUTOFMEMORY;
    (*Builder)->Factory = RxComPtr<RxWriteFactory>(Owner);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFallbackBuilder::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFallbackBuilder))
    {
        *ppvObject = static_cast<IDWriteFontFallbackBuilder*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxFontFallbackBuilder)

HRESULT
STDMETHODCALLTYPE
RxFontFallbackBuilder::AddMapping(
    _In_reads_(rangesCount) DWRITE_UNICODE_RANGE const* ranges,
    UINT32 rangesCount,
    _In_reads_(targetFamilyNamesCount) WCHAR const** targetFamilyNames,
    UINT32 targetFamilyNamesCount,
    _In_opt_ IDWriteFontCollection* fontCollection,
    _In_opt_z_ WCHAR const* localeName,
    _In_opt_z_ WCHAR const* baseFamilyName,
    FLOAT scale)
{
    RxFallbackMapping Mapping;

    if (ranges == nullptr || rangesCount == 0 || targetFamilyNames == nullptr || targetFamilyNamesCount == 0 ||
        !(scale >= 0.0f))
        return E_INVALIDARG;
    for (UINT32 Index = 0; Index < targetFamilyNamesCount; Index++)
    {
        if (targetFamilyNames[Index] == nullptr)
            return E_INVALIDARG;
    }

    /* Ranges that run backwards or start past the last code point never match */
    for (UINT32 Index = 0; Index < rangesCount; Index++)
    {
        if (ranges[Index].first > ranges[Index].last || ranges[Index].first > 0x10FFFF)
            continue;
        Mapping.Ranges.push_back({ ranges[Index].first, std::min<UINT32>(ranges[Index].last, 0x10FFFF) });
    }
    if (Mapping.Ranges.empty())
        return S_OK;

    for (UINT32 Index = 0; Index < targetFamilyNamesCount; Index++)
        Mapping.Families.push_back(targetFamilyNames[Index]);
    Mapping.Collection = RxComPtr<IDWriteFontCollection>(fontCollection);
    if (localeName != nullptr && *localeName != L'\0')
        Mapping.Locales.push_back(localeName);
    if (baseFamilyName != nullptr)
        Mapping.BaseFamily = baseFamilyName;
    Mapping.Scale = scale;
    Mappings.push_back(std::move(Mapping));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFallbackBuilder::AddMappings(
    IDWriteFontFallback* fontFallback)
{
    RxFontFallback* Source = RxFontFallback::FromInterface(fontFallback);

    if (Source == nullptr)
        return E_INVALIDARG;

    Mappings.insert(Mappings.end(), Source->MappingList().begin(), Source->MappingList().end());
    if (Source->Cache() != nullptr)
        Scan = RxComPtr<RxFallbackCache>(Source->Cache());
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFallbackBuilder::CreateFontFallback(
    _COM_Outptr_ IDWriteFontFallback** fontFallback)
{
    RxFontCollection* Collection;
    RxFontFallback* Result;
    HRESULT hr;

    if (fontFallback == nullptr)
        return E_INVALIDARG;
    *fontFallback = nullptr;

    hr = Factory->SystemCollection(&Collection);
    if (FAILED(hr))
        return hr;
    hr = RxFontFallback::Create(Factory.Get(), Collection, Mappings, Scan.Get(), &Result);
    Collection->Release();
    if (FAILED(hr))
        return hr;
    *fontFallback = Result;
    return S_OK;
}

/* RxWriteFactory */

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::GetSystemFontFallback(
    _COM_Outptr_ IDWriteFontFallback** fontFallback)
{
    RxFontCollection* Collection;
    RxFontFallback* Created = nullptr;
    RxFontFallback* Result;
    HRESULT hr;

    if (fontFallback == nullptr)
        return E_INVALIDARG;
    *fontFallback = nullptr;

    /* The mappings and font cache are built once; each call hands out its own object over them */
    AcquireSRWLockShared(&Lock);
    if (Fallback != nullptr)
    {
        Fallback->AddRef();
        Created = Fallback;
    }
    ReleaseSRWLockShared(&Lock);

    if (Created == nullptr)
    {
        /* Built outside the lock, since the system collection takes it too */
        hr = SystemCollection(&Collection);
        if (FAILED(hr))
            return hr;
        hr = RxFontFallback::CreateSystem(Collection, &Created);
        Collection->Release();
        if (FAILED(hr))
            return hr;

        AcquireSRWLockExclusive(&Lock);
        if (Fallback == nullptr)
        {
            Created->AddRef();
            Fallback = Created;
        }
        ReleaseSRWLockExclusive(&Lock);
    }

    hr = RxFontFallback::Create(nullptr, Created->Collection(), Created->MappingList(), Created->Cache(), &Result);
    Created->Release();
    if (FAILED(hr))
        return hr;
    *fontFallback = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateFontFallbackBuilder(
    _COM_Outptr_ IDWriteFontFallbackBuilder** fontFallbackBuilder)
{
    RxFontFallbackBuilder* Builder;
    HRESULT hr;

    if (fontFallbackBuilder == nullptr)
        return E_INVALIDARG;
    *fontFallbackBuilder = nullptr;

    hr = RxFontFallbackBuilder::Create(this, &Builder);
    if (FAILED(hr))
        return hr;
    *fontFallbackBuilder = Builder;
    return S_OK;
}
