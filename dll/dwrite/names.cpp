/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Localized names and the weight, stretch and style of a face
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "names.hpp"

/* Name IDs */
constexpr USHORT RxNameFamily = 1;
constexpr USHORT RxNameSubfamily = 2;
constexpr USHORT RxNameFull = 4;
constexpr USHORT RxNamePostscript = 6;
constexpr USHORT RxNameTypographicFamily = 16;
constexpr USHORT RxNameTypographicSubfamily = 17;
constexpr USHORT RxNameWwsFamily = 21;
constexpr USHORT RxNameWwsSubfamily = 22;

static const std::wstring RxEmptyName;

const std::wstring&
RxIdentityName(
    _In_ const RxNameList& Names)
{
    for (const auto& Entry : Names)
    {
        if (Entry.Locale == L"en-us")
            return Entry.Text;
    }
    return Names.empty() ? RxEmptyName : Names[0].Text;
}

static
VOID
RxLowerAscii(
    _Inout_ std::wstring& Text)
{
    for (auto& Char : Text)
    {
        if (Char >= L'A' && Char <= L'Z')
            Char = static_cast<WCHAR>(Char - L'A' + L'a');
        else if (Char == L'_')
            Char = L'-';
    }
}

static
BOOLEAN
RxLanguageTag(
    _In_ USHORT Platform,
    _In_ USHORT Language,
    _Out_ std::wstring& Tag)
{
    WCHAR Buffer[LOCALE_NAME_MAX_LENGTH];

    if (Platform == 1 && Language == 0)
    {
        Tag = L"en-us";
        return TRUE;
    }
    if (Platform != 3 || Language == 0x0F00 || Language == 0x1000)
        return FALSE;
    if (LCIDToLocaleName(Language, Buffer, _countof(Buffer), 0) == 0)
        return FALSE;

    /* Lowercased; an underscore stays as LCIDToLocaleName wrote it */
    Tag = Buffer;
    for (auto& Char : Tag)
    {
        if (Char >= L'A' && Char <= L'Z')
            Char = static_cast<WCHAR>(Char - L'A' + L'a');
    }
    return TRUE;
}

static
BOOLEAN
RxDecodeName(
    _In_ const RxSpan& Name,
    _In_ USHORT Platform,
    _In_ USHORT Encoding,
    _In_ UINT32 Offset,
    _In_ UINT32 Length,
    _Out_ std::wstring& Text)
{
    UINT CodePage = 0;

    Text.clear();
    if (!Name.Has(Offset, Length))
        return FALSE;

    if (Platform == 0 || (Platform == 3 && (Encoding <= 2 || Encoding == 10)))
    {
        for (UINT32 Char = 0; Char + 1 < Length; Char += 2)
        {
            WCHAR Value = Name.U16(Offset + Char);

            if (Value == UNICODE_NULL)
                break;
            Text.push_back(Value);
        }
        return TRUE;
    }

    if (Platform == 1 && Encoding == 0)
    {
        for (UINT32 Char = 0; Char < Length; Char++)
            Text.push_back(RxMacRomanChar(Name.U8(Offset + Char)));
        return TRUE;
    }

    if (Platform == 3 && Encoding == 3)
        CodePage = 936;
    else if (Platform == 3 && Encoding == 4)
        CodePage = 950;
    else if (Platform == 3 && Encoding == 5)
        CodePage = 949;
    else
        return FALSE;

    {
        std::string Bytes;
        INT Chars;

        /* Double byte names are stored padded with zero bytes */
        for (UINT32 Char = 0; Char < Length; Char++)
        {
            BYTE Byte = Name.U8(Offset + Char);

            if (Byte != 0)
                Bytes.push_back(static_cast<CHAR>(Byte));
        }
        if (Bytes.empty())
            return TRUE;

        Chars = MultiByteToWideChar(CodePage, 0, Bytes.data(), static_cast<INT>(Bytes.size()), nullptr, 0);
        if (Chars <= 0)
            return FALSE;
        Text.resize(Chars);
        MultiByteToWideChar(CodePage, 0, Bytes.data(), static_cast<INT>(Bytes.size()), &Text[0], Chars);
    }
    return TRUE;
}

VOID
RxReadNames(
    _In_ const RxFaceTables* Face,
    _In_ USHORT NameId,
    _Inout_ RxNameList& Names)
{
    RxSpan Name = Face->Sfnt().Table(RxTag('n', 'a', 'm', 'e'));
    USHORT Count = Name.U16(2);
    UINT32 Storage = Name.U16(4);
    std::vector<BOOLEAN> FromWindows;

    Names.clear();

    for (USHORT Index = 0; Index < Count; Index++)
    {
        UINT32 Record = 6 + Index * 12;
        USHORT Platform = Name.U16(Record);
        USHORT Encoding = Name.U16(Record + 2);
        USHORT Language = Name.U16(Record + 4);
        std::wstring Tag;
        std::wstring Text;
        SIZE_T Existing;

        if (Name.U16(Record + 6) != NameId)
            continue;
        if (!RxLanguageTag(Platform, Language, Tag))
            continue;
        if (!RxDecodeName(Name, Platform, Encoding, Storage + Name.U16(Record + 10), Name.U16(Record + 8), Text))
            continue;

        /* One string per language; a Windows record beats any other */
        for (Existing = 0; Existing < Names.size(); Existing++)
        {
            if (Names[Existing].Locale == Tag)
                break;
        }
        if (Existing == Names.size())
        {
            Names.push_back({ Tag, Text });
            FromWindows.push_back(Platform == 3);
        }
        else if (Platform == 3 && !FromWindows[Existing])
        {
            Names[Existing].Text = Text;
            FromWindows[Existing] = TRUE;
        }
    }

    std::sort(Names.begin(), Names.end(),
              [](const RxNameEntry& A, const RxNameEntry& B) { return A.Locale < B.Locale; });
}

/* English family and subfamily that share a language, from Windows or Mac records */
static
BOOLEAN
RxCanonicalNames(
    _In_ const RxFaceTables* Face,
    _Out_ std::wstring& Family,
    _Out_ std::wstring& Subfamily,
    _Out_ std::wstring& Locale)
{
    RxSpan Name = Face->Sfnt().Table(RxTag('n', 'a', 'm', 'e'));
    USHORT Count = Name.U16(2);
    UINT32 Storage = Name.U16(4);
    struct Candidate
    {
        UINT32 Lcid;
        std::wstring Text;
    };
    std::vector<Candidate> Families;
    std::vector<Candidate> Subfamilies;

    for (USHORT Index = 0; Index < Count; Index++)
    {
        UINT32 Record = 6 + Index * 12;
        USHORT Platform = Name.U16(Record);
        USHORT Encoding = Name.U16(Record + 2);
        USHORT Language = Name.U16(Record + 4);
        USHORT NameId = Name.U16(Record + 6);
        UINT32 Lcid;
        std::wstring Text;

        if (NameId != RxNameFamily && NameId != RxNameSubfamily)
            continue;
        if (Platform == 3 && (Language & 0x3FF) == 9)
            Lcid = Language;
        else if (Platform == 1 && Language == 0)
            Lcid = 1033;
        else
            continue;
        if (!RxDecodeName(Name, Platform, Encoding, Storage + Name.U16(Record + 10), Name.U16(Record + 8), Text))
            continue;

        (NameId == RxNameFamily ? Families : Subfamilies).push_back({ Lcid, Text });
    }

    auto ByLcid = [](const Candidate& A, const Candidate& B) { return A.Lcid < B.Lcid; };
    std::stable_sort(Families.begin(), Families.end(), ByLcid);
    std::stable_sort(Subfamilies.begin(), Subfamilies.end(), ByLcid);

    for (const auto& A : Families)
    {
        for (const auto& B : Subfamilies)
        {
            if (A.Lcid == B.Lcid)
            {
                Family = A.Text;
                Subfamily = B.Text;
                if (!RxLanguageTag(3, static_cast<USHORT>(A.Lcid), Locale))
                    Locale = L"en-us";
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* OS/2 and head values */

USHORT
RxFaceWeight(
    _In_ const RxFaceHeader& Header)
{
    UINT32 Weight;

    if (!Header.HasOs2)
        return (Header.MacStyle & RxMacStyleBold) ? 700 : 400;

    Weight = Header.WeightClass;
    if (Weight == 0)
        Weight = 1;
    else if (Weight > 999)
        return 999;
    else if (Weight <= 9)
        Weight *= 100;
    if (Weight <= 500 && (Header.FsSelection & RxFsSelectionBold))
        Weight = 700;
    return static_cast<USHORT>(Weight);
}

BYTE
RxFaceStyle(
    _In_ const RxFaceHeader& Header)
{
    if (!Header.HasOs2)
        return static_cast<BYTE>((Header.MacStyle & RxMacStyleItalic) ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL);
    if (Header.Os2Version >= 4 && (Header.FsSelection & RxFsSelectionOblique))
        return DWRITE_FONT_STYLE_OBLIQUE;
    if (Header.FsSelection & RxFsSelectionItalic)
        return DWRITE_FONT_STYLE_ITALIC;
    return DWRITE_FONT_STYLE_NORMAL;
}

BYTE
RxFaceStretch(
    _In_ const RxFaceHeader& Header)
{
    if (!Header.HasOs2)
    {
        if (Header.MacStyle & 0x20)
            return DWRITE_FONT_STRETCH_CONDENSED;
        if (Header.MacStyle & 0x40)
            return DWRITE_FONT_STRETCH_EXPANDED;
        return DWRITE_FONT_STRETCH_NORMAL;
    }
    if (Header.WidthClass == 0)
        return DWRITE_FONT_STRETCH_NORMAL;
    return static_cast<BYTE>(std::min<USHORT>(Header.WidthClass, 9));
}

/* Name heuristic */

static
BOOLEAN
RxEqualNoCase(
    _In_ const std::wstring& A,
    _In_ PCWSTR B)
{
    return _wcsicmp(A.c_str(), B) == 0;
}

static
BOOLEAN
RxIsDelimiter(
    _In_ WCHAR Char)
{
    return Char == L' ' || Char == L'-' || Char == L'.' || Char == L'_';
}

static
VOID
RxTrim(
    _Inout_ std::wstring& Text)
{
    SIZE_T First = Text.find_first_not_of(L' ');

    if (First == std::wstring::npos)
    {
        Text.clear();
        return;
    }
    Text = Text.substr(First, Text.find_last_not_of(L' ') - First + 1);
}

/* Cuts the last regular word out of a face name; returns the word removed */
static
std::wstring
RxRemoveRegularWord(
    _Inout_ std::wstring& Face)
{
    static const PCWSTR Words[] = { L"roman", L"regular", L"book", L"normal", L"upright" };
    std::wstring Lower = Face;
    SIZE_T Best = std::wstring::npos;
    SIZE_T BestLength = 0;
    std::wstring Removed;

    if (Face.size() < 4)
        return Removed;

    RxLowerAscii(Lower);
    for (PCWSTR Word : Words)
    {
        SIZE_T Found = Lower.rfind(Word);

        if (Found != std::wstring::npos && (Best == std::wstring::npos || Found > Best))
        {
            Best = Found;
            BestLength = wcslen(Word);
        }
    }
    if (Best == std::wstring::npos)
        return Removed;

    Removed = Face.substr(Best, BestLength);
    {
        std::wstring Before = Face.substr(0, Best);
        std::wstring After = Face.substr(Best + BestLength);

        while (!Before.empty() && RxIsDelimiter(Before.back()))
            Before.pop_back();
        while (!After.empty() && RxIsDelimiter(After.front()))
            After.erase(0, 1);

        if (!Before.empty() && !After.empty())
            Face = Before + L" " + After;
        else
            Face = Before + After;
    }
    return Removed;
}

struct RxToken
{
    std::wstring Text;
    SIZE_T Start;
    BOOLEAN Deleted;
};

static
VOID
RxTokenize(
    _In_ const std::wstring& Text,
    _Out_ std::vector<RxToken>& Tokens)
{
    SIZE_T Index = 0;

    Tokens.clear();
    while (Index < Text.size())
    {
        SIZE_T Start;

        while (Index < Text.size() && RxIsDelimiter(Text[Index]))
            Index++;
        if (Index >= Text.size())
            break;
        Start = Index;
        while (Index < Text.size() && !RxIsDelimiter(Text[Index]))
            Index++;
        Tokens.push_back({ Text.substr(Start, Index - Start), Start, FALSE });
    }
}

static
BYTE
RxParseStyleWord(
    _In_ const std::wstring& Word)
{
    static const PCWSTR Italic[] = { L"italic", L"kursiv", L"ita", L"ital", L"cursive" };
    static const PCWSTR Oblique[] = { L"oblique", L"slanted", L"inclined", L"backslant", L"backslanted" };

    for (PCWSTR Name : Italic)
    {
        if (RxEqualNoCase(Word, Name))
            return DWRITE_FONT_STYLE_ITALIC;
    }
    for (PCWSTR Name : Oblique)
    {
        if (RxEqualNoCase(Word, Name))
            return DWRITE_FONT_STYLE_OBLIQUE;
    }
    return 0xFF;
}

/* Values for one word: plain, semi, demi, extra and ultra, each a value and a score; zero means none */
struct RxWordValues
{
    PCWSTR Word;
    USHORT Value[5];
    BYTE Score[5];
};

static const RxWordValues RxWeightWords[] =
{
    { L"thin",   { 100, 0, 0, 100, 100 }, { 6, 0, 0, 12, 12 } },
    { L"light",  { 300, 0, 0, 200, 200 }, { 5, 0, 0, 11, 11 } },
    { L"medium", { 500, 0, 0, 0, 0 },     { 4, 0, 0, 0, 0 } },
    { L"demi",   { 600, 0, 0, 0, 0 },     { 2, 0, 0, 0, 0 } },
    { L"bold",   { 700, 600, 600, 800, 800 }, { 7, 10, 10, 9, 9 } },
    { L"ultra",  { 800, 0, 0, 0, 0 },     { 1, 0, 0, 0, 0 } },
    { L"black",  { 900, 0, 0, 950, 950 }, { 3, 0, 0, 8, 8 } },
    { L"heavy",  { 900, 0, 0, 0, 0 },     { 3, 0, 0, 0, 0 } },
    { L"nord",   { 900, 0, 0, 0, 0 },     { 3, 0, 0, 0, 0 } },
};

static const RxWordValues RxStretchWords[] =
{
    { L"compressed", { 2, 0, 0, 1, 1 }, { 7, 0, 0, 8, 8 } },
    { L"condensed",  { 3, 4, 0, 2, 1 }, { 2, 6, 0, 7, 8 } },
    { L"cond",       { 3, 4, 0, 2, 1 }, { 2, 6, 0, 7, 8 } },
    { L"narrow",     { 4, 0, 0, 0, 0 }, { 6, 0, 0, 0, 0 } },
    { L"compact",    { 4, 0, 0, 0, 0 }, { 6, 0, 0, 0, 0 } },
    { L"wide",       { 6, 0, 0, 0, 0 }, { 5, 0, 0, 0, 0 } },
    { L"expanded",   { 7, 6, 0, 8, 9 }, { 1, 5, 0, 4, 3 } },
    { L"extended",   { 7, 6, 0, 8, 9 }, { 1, 5, 0, 4, 3 } },
};

/* 1 semi, 2 demi, 3 extra, 4 ultra; Length receives the prefix size */
static
UINT32
RxQualifier(
    _In_ const std::wstring& Lower,
    _Out_ SIZE_T* Length)
{
    static const struct
    {
        PCWSTR Prefix;
        UINT32 Index;
    } Prefixes[] = { { L"extra", 3 }, { L"ultra", 4 }, { L"semi", 1 }, { L"demi", 2 }, { L"ext", 3 } };

    for (const auto& Entry : Prefixes)
    {
        SIZE_T Size = wcslen(Entry.Prefix);

        if (Lower.compare(0, Size, Entry.Prefix) == 0)
        {
            *Length = Size;
            return Entry.Index;
        }
    }
    *Length = 0;
    return 0;
}

static
BOOLEAN
RxLookupWord(
    _In_reads_(Count) const RxWordValues* Table,
    _In_ SIZE_T Count,
    _In_ const std::wstring& Word,
    _In_ UINT32 Qualifier,
    _Out_ USHORT* Value,
    _Out_ BYTE* Score)
{
    for (SIZE_T Index = 0; Index < Count; Index++)
    {
        if (Word == Table[Index].Word && Table[Index].Value[Qualifier] != 0)
        {
            *Value = Table[Index].Value[Qualifier];
            *Score = Table[Index].Score[Qualifier];
            return TRUE;
        }
    }
    return FALSE;
}

/* A word with an optional glued qualifier, or a qualifier token before it */
static
BOOLEAN
RxParseWordWindow(
    _In_reads_(Count) const RxWordValues* Table,
    _In_ SIZE_T Count,
    _In_opt_ const RxToken* QualifierToken,
    _In_ const std::wstring& WordText,
    _Out_ USHORT* Value,
    _Out_ BYTE* Score,
    _Out_ BOOLEAN* UsedQualifier)
{
    std::wstring Word = WordText;
    SIZE_T Prefix;
    UINT32 Qualifier;

    RxLowerAscii(Word);
    *UsedQualifier = FALSE;

    if (QualifierToken != nullptr)
    {
        std::wstring Lower = QualifierToken->Text;

        RxLowerAscii(Lower);
        Qualifier = RxQualifier(Lower, &Prefix);
        if (Qualifier != 0 && Prefix == Lower.size() && RxLookupWord(Table, Count, Word, Qualifier, Value, Score))
        {
            *UsedQualifier = TRUE;
            return TRUE;
        }
        return FALSE;
    }

    if (RxLookupWord(Table, Count, Word, 0, Value, Score))
        return TRUE;

    Qualifier = RxQualifier(Word, &Prefix);
    if (Qualifier != 0 && Prefix < Word.size())
        return RxLookupWord(Table, Count, Word.substr(Prefix), Qualifier, Value, Score);
    return FALSE;
}

/* Finds the best stretch or weight words among the live tokens; returns the value or zero */
static
USHORT
RxExtractWords(
    _Inout_ std::vector<RxToken>& Tokens,
    _In_reads_(Count) const RxWordValues* Table,
    _In_ SIZE_T Count,
    _In_ BOOLEAN AllowFace,
    _Out_ std::wstring& Text)
{
    USHORT BestValue = 0;
    BYTE BestScore = 0;
    SIZE_T BestWord = 0;
    SIZE_T BestQualifier = MAXSIZE_T;
    SIZE_T BestFace = MAXSIZE_T;

    Text.clear();

    for (SIZE_T Index = Tokens.size(); Index-- > 1;)
    {
        std::wstring Word;
        SIZE_T WordIndex = Index;
        SIZE_T FaceIndex = MAXSIZE_T;
        SIZE_T Previous = MAXSIZE_T;
        USHORT Value;
        BYTE Score;
        BOOLEAN UsedQualifier;

        if (Tokens[Index].Deleted)
            continue;
        Word = Tokens[Index].Text;

        if (AllowFace)
        {
            std::wstring Lower = Word;

            RxLowerAscii(Lower);
            if (Lower == L"face")
            {
                /* The weight word is the token before a separate face */
                for (SIZE_T Back = Index; Back-- > 1;)
                {
                    if (!Tokens[Back].Deleted)
                    {
                        FaceIndex = Index;
                        WordIndex = Back;
                        Word = Tokens[Back].Text;
                        break;
                    }
                }
                if (FaceIndex == MAXSIZE_T)
                    continue;
            }
            else if (Lower.size() > 4 && Lower.compare(Lower.size() - 4, 4, L"face") == 0)
            {
                Word = Word.substr(0, Word.size() - 4);
            }
        }

        for (SIZE_T Back = WordIndex; Back-- > 1;)
        {
            if (!Tokens[Back].Deleted)
            {
                Previous = Back;
                break;
            }
        }

        if (Previous != MAXSIZE_T &&
            RxParseWordWindow(Table, Count, &Tokens[Previous], Word, &Value, &Score, &UsedQualifier))
        {
            if (Score > BestScore)
            {
                BestValue = Value;
                BestScore = Score;
                BestWord = WordIndex;
                BestQualifier = Previous;
                BestFace = FaceIndex;
            }
            continue;
        }

        if (RxParseWordWindow(Table, Count, nullptr, Word, &Value, &Score, &UsedQualifier) && Score > BestScore)
        {
            BestValue = Value;
            BestScore = Score;
            BestWord = WordIndex;
            BestQualifier = MAXSIZE_T;
            BestFace = FaceIndex;
        }
    }

    if (BestValue == 0)
        return 0;

    if (BestQualifier != MAXSIZE_T)
    {
        Text = Tokens[BestQualifier].Text + L" ";
        Tokens[BestQualifier].Deleted = TRUE;
    }
    Text += Tokens[BestWord].Text;
    Tokens[BestWord].Deleted = TRUE;
    if (BestFace != MAXSIZE_T)
    {
        Text += L" " + Tokens[BestFace].Text;
        Tokens[BestFace].Deleted = TRUE;
    }
    return BestValue;
}

static
USHORT
RxResolveWeight(
    _In_ USHORT Face,
    _In_ USHORT Name)
{
    if (Name == 0 || Name == Face)
        return Face;
    if (Face < 400 && Name < 400)
        return Face;
    if ((Face == 400 || Face == 500) && (Name == 400 || Name == 500))
        return Face;
    if (Face > 500 && Name >= 500 && Face != 700)
        return Face;
    if (std::abs(static_cast<INT>(Face) - static_cast<INT>(Name)) > 150 || Face == 400 || Face == 500 || Face == 700)
        return Name;
    return Face;
}

static
BYTE
RxResolveStretch(
    _In_ BYTE Face,
    _In_ BYTE Name)
{
    if (Name == 0)
        return Face;
    if ((Face < 5 && Name < 5) || (Face > 5 && Name > 5))
        return Face;
    return Name;
}

static
PCWSTR
RxWeightName(
    _In_ USHORT Weight)
{
    switch (Weight)
    {
        case 100: return L"Thin";
        case 200: return L"ExtraLight";
        case 300: return L"Light";
        case 500: return L"Medium";
        case 600: return L"SemiBold";
        case 700: return L"Bold";
        case 800: return L"ExtraBold";
        case 900: return L"Black";
        case 950: return L"ExtraBlack";
    }
    return nullptr;
}

static
PCWSTR
RxStretchName(
    _In_ BYTE Stretch)
{
    static const PCWSTR Names[] =
    {
        nullptr, L"UltraCondensed", L"ExtraCondensed", L"Condensed", L"SemiCondensed", nullptr, L"SemiExpanded",
        L"Expanded", L"ExtraExpanded", L"UltraExpanded"
    };

    return Stretch < _countof(Names) ? Names[Stretch] : nullptr;
}

/* Face name from the words found, or synthesized from values that are not the defaults */
static
std::wstring
RxBuildFaceName(
    _In_ const std::wstring& Prefix,
    _In_ const std::wstring& StretchText,
    _In_ const std::wstring& WeightText,
    _In_ const std::wstring& StyleText,
    _In_ BYTE Stretch,
    _In_ USHORT Weight,
    _In_ BYTE Style,
    _In_ const std::wstring& RemovedRegular)
{
    std::wstring Parts[4];
    std::wstring Result;

    Parts[0] = Prefix;
    if (!Prefix.empty())
    {
        Parts[1] = StretchText;
        Parts[2] = WeightText;
        Parts[3] = StyleText;
    }
    else
    {
        Parts[1] = !StretchText.empty() ? StretchText : (RxStretchName(Stretch) ? RxStretchName(Stretch) : L"");
        if (!WeightText.empty())
            Parts[2] = WeightText;
        else if (Weight != 400)
            Parts[2] = RxWeightName(Weight) ? RxWeightName(Weight) : std::to_wstring(Weight);
        if (!StyleText.empty())
            Parts[3] = StyleText;
        else if (Style == DWRITE_FONT_STYLE_ITALIC)
            Parts[3] = L"Italic";
        else if (Style == DWRITE_FONT_STYLE_OBLIQUE)
            Parts[3] = L"Oblique";
    }

    for (const auto& Part : Parts)
    {
        if (Part.empty())
            continue;
        if (!Result.empty())
            Result += L" ";
        Result += Part;
    }

    if (Result.empty())
        Result = !RemovedRegular.empty() ? RemovedRegular : L"Regular";
    return Result;
}

static
std::wstring
RxJoinLive(
    _In_ const std::vector<RxToken>& Tokens,
    _In_ SIZE_T First,
    _In_ SIZE_T Last)
{
    std::wstring Result;

    for (SIZE_T Index = First; Index < Last && Index < Tokens.size(); Index++)
    {
        if (Tokens[Index].Deleted)
            continue;
        if (!Result.empty())
            Result += L" ";
        Result += Tokens[Index].Text;
    }
    return Result;
}

/* Text of a list in a language, or the identity name */
static
std::wstring
RxNameIn(
    _In_ const RxNameList& Names,
    _In_ const std::wstring& Locale)
{
    for (const auto& Entry : Names)
    {
        if (Entry.Locale == Locale)
            return Entry.Text;
    }
    return RxIdentityName(Names);
}

VOID
RxDescribeFace(
    _In_ const RxFaceTables* Face,
    _In_ const std::vector<RxAxisValue>& Coordinates,
    _Out_ RxFaceDescription* Description)
{
    const RxFaceHeader& Header = Face->Header();
    BOOLEAN HasTypographic;
    BOOLEAN HasWws;
    std::wstring CanonicalFamily;
    std::wstring CanonicalFace;
    std::wstring Locale;
    BOOLEAN HaveCanonical;

    Description->FromStyleTable = FALSE;
    Description->HasVariations = !Coordinates.empty();
    if (Face->Sfnt().HasTable(RxTag('S', 'T', 'A', 'T')))
    {
        RxDescribeStyledFace(Face, Coordinates, Description);
        return;
    }

    RxReadNames(Face, RxNameFamily, Description->Win32Family);
    RxReadNames(Face, RxNameSubfamily, Description->Win32Face);
    RxReadNames(Face, RxNameTypographicFamily, Description->TypographicFamily);
    RxReadNames(Face, RxNameTypographicSubfamily, Description->TypographicFace);
    RxReadNames(Face, RxNameWwsFamily, Description->WwsFamily);
    RxReadNames(Face, RxNameWwsSubfamily, Description->WwsFace);
    RxReadNames(Face, RxNameFull, Description->FullName);
    RxReadNames(Face, RxNamePostscript, Description->PostscriptName);

    Description->Win32Weight = RxFaceWeight(Header);
    Description->Win32Style = RxFaceStyle(Header);
    Description->Weight = Description->Win32Weight;
    Description->Style = Description->Win32Style;
    Description->Stretch = RxFaceStretch(Header);

    HasTypographic = !Description->TypographicFamily.empty() || !Description->TypographicFace.empty();
    HasWws = !Description->WwsFamily.empty() && !Description->WwsFace.empty();
    HaveCanonical = RxCanonicalNames(Face, CanonicalFamily, CanonicalFace, Locale);

    if ((!HasTypographic || !HasWws) && HaveCanonical && !Face->Sfnt().HasTable(RxTag('S', 'T', 'A', 'T')) &&
        !(Header.HasOs2 && (Header.FsSelection & RxFsSelectionWws)))
    {
        std::wstring Family;
        std::wstring Subfamily;
        std::wstring Removed;
        std::wstring Combined;
        std::vector<RxToken> Tokens;
        std::wstring StyleText;
        std::wstring StretchText;
        std::wstring WeightText;
        BYTE NameStyle = 0xFF;
        BYTE NameStretch;
        USHORT NameWeight;
        SIZE_T FamilyEnd;

        Family = !Description->TypographicFamily.empty() ? RxNameIn(Description->TypographicFamily, Locale) :
                                                           CanonicalFamily;
        Subfamily = !Description->TypographicFace.empty() ? RxNameIn(Description->TypographicFace, Locale) :
                                                            CanonicalFace;
        RxTrim(Family);
        RxTrim(Subfamily);

        Removed = RxRemoveRegularWord(Subfamily);
        RxTrim(Subfamily);

        Combined = Family;
        if (!Subfamily.empty())
        {
            std::vector<RxToken> FamilyTokens;
            BOOLEAN Contained = FALSE;

            RxTokenize(Family, FamilyTokens);
            for (const auto& Token : FamilyTokens)
            {
                if (_wcsicmp(Token.Text.c_str(), Subfamily.c_str()) == 0)
                    Contained = TRUE;
            }
            if (!Contained)
                Combined = Family + L" " + Subfamily;
        }
        FamilyEnd = Family.size();

        RxTokenize(Combined, Tokens);

        /* Style, scanning back from the last token */
        for (SIZE_T Index = Tokens.size(); Index-- > 1;)
        {
            BYTE Style = RxParseStyleWord(Tokens[Index].Text);

            if (Style != 0xFF)
            {
                NameStyle = Style;
                StyleText = Tokens[Index].Text;
                Tokens[Index].Deleted = TRUE;
                break;
            }
        }

        NameStretch = static_cast<BYTE>(RxExtractWords(Tokens, RxStretchWords, _countof(RxStretchWords), FALSE,
                                                       StretchText));
        NameWeight = RxExtractWords(Tokens, RxWeightWords, _countof(RxWeightWords), TRUE, WeightText);

        Description->Weight = RxResolveWeight(Description->Win32Weight, NameWeight);
        Description->Stretch = RxResolveStretch(Description->Stretch, NameStretch);
        if (NameStyle != 0xFF)
            Description->Style = NameStyle;

        if (!HasWws)
        {
            std::wstring Live = RxJoinLive(Tokens, 0, Tokens.size());
            BOOLEAN AnyDeleted = FALSE;

            for (const auto& Token : Tokens)
                AnyDeleted |= Token.Deleted;

            /* Nothing removed leaves the family as written, delimiters and all */
            if (!AnyDeleted || Live == Family)
            {
                /* Defaults below make the WWS names the typographic ones */
            }
            else if (Live == CanonicalFamily)
            {
                Description->WwsFamily = Description->Win32Family;
                Description->WwsFace = Description->Win32Face;
            }
            else
            {
                Description->WwsFamily = { { Locale, Live } };
                Description->WwsFace = { { Locale, RxBuildFaceName(L"", StretchText, WeightText, StyleText,
                                                                   Description->Stretch, Description->Weight,
                                                                   Description->Style, Removed) } };
            }
        }

        if (!HasTypographic)
        {
            SIZE_T FamilyTokens = 0;
            BOOLEAN AnyDeleted = FALSE;

            for (const auto& Token : Tokens)
            {
                if (Token.Start < FamilyEnd)
                {
                    FamilyTokens++;
                    AnyDeleted |= Token.Deleted;
                }
            }

            if (AnyDeleted)
            {
                std::wstring Rest = RxJoinLive(Tokens, FamilyTokens, Tokens.size());

                Description->TypographicFamily = { { Locale, RxJoinLive(Tokens, 0, FamilyTokens) } };
                Description->TypographicFace = { { Locale, RxBuildFaceName(Rest.empty() ? L"" : Rest, StretchText,
                                                                           WeightText, StyleText, Description->Stretch,
                                                                           Description->Weight, Description->Style,
                                                                           Removed) } };
            }
        }
    }

    if (Description->TypographicFamily.empty())
        Description->TypographicFamily = Description->Win32Family;
    if (Description->TypographicFace.empty())
        Description->TypographicFace = Description->Win32Face;
    if (Description->WwsFamily.empty())
        Description->WwsFamily = Description->TypographicFamily;
    if (Description->WwsFace.empty())
        Description->WwsFace = Description->TypographicFace;
    RxFaceAxisValues(Face, Coordinates, Description->Weight, Description->Stretch, Description->Style,
                     Description->AxisValues);
}

std::wstring
RxSimulatedFaceName(
    _In_ const std::wstring& Face,
    _In_ DWRITE_FONT_SIMULATIONS Simulations)
{
    std::wstring Result = Face;

    if (Simulations & DWRITE_FONT_SIMULATIONS_BOLD)
    {
        std::vector<RxToken> Tokens;
        std::wstring Unused;

        RxRemoveRegularWord(Result);
        RxTokenize(L"x " + Result, Tokens);
        if (RxExtractWords(Tokens, RxWeightWords, _countof(RxWeightWords), TRUE, Unused) >= 800)
            RxTokenize(L"x " + Result, Tokens);
        Result = RxJoinLive(Tokens, 1, Tokens.size());
        RxTrim(Result);
        Result = Result.empty() ? L"Bold" : Result + L" Bold";
    }

    if (Simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE)
    {
        RxRemoveRegularWord(Result);
        RxTrim(Result);
        Result = Result.empty() ? L"Oblique" : Result + L" Oblique";
    }
    return Result;
}

/* RxLocalizedStrings */

HRESULT
RxLocalizedStrings::Create(
    _In_ const RxNameList& Names,
    _COM_Outptr_ IDWriteLocalizedStrings** Strings)
{
    RxLocalizedStrings* Result = new (std::nothrow) RxLocalizedStrings();

    *Strings = nullptr;
    if (Result == nullptr)
        return E_OUTOFMEMORY;
    Result->Entries = Names;
    *Strings = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxLocalizedStrings::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteLocalizedStrings))
    {
        *ppvObject = static_cast<IDWriteLocalizedStrings*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxLocalizedStrings)

UINT32
STDMETHODCALLTYPE
RxLocalizedStrings::GetCount(VOID)
{
    return static_cast<UINT32>(Entries.size());
}

HRESULT
STDMETHODCALLTYPE
RxLocalizedStrings::FindLocaleName(
    _In_z_ WCHAR const* localeName,
    _Out_ UINT32* index,
    _Out_ BOOL* exists)
{
    std::wstring Wanted;

    *index = MAXUINT32;
    *exists = FALSE;
    if (localeName == nullptr)
        return E_INVALIDARG;

    Wanted = localeName;
    RxLowerAscii(Wanted);
    for (SIZE_T Index = 0; Index < Entries.size(); Index++)
    {
        if (Entries[Index].Locale == Wanted)
        {
            *index = static_cast<UINT32>(Index);
            *exists = TRUE;
            break;
        }
    }
    return S_OK;
}

static
HRESULT
RxCopyText(
    _In_ const std::wstring& Text,
    _Out_writes_z_(Size) WCHAR* Buffer,
    _In_ UINT32 Size)
{
    if (Size == 0 || Buffer == nullptr)
        return E_NOT_SUFFICIENT_BUFFER;

    if (Text.size() + 1 > Size)
    {
        memcpy(Buffer, Text.data(), (Size - 1) * sizeof(WCHAR));
        Buffer[Size - 1] = UNICODE_NULL;
        return E_NOT_SUFFICIENT_BUFFER;
    }
    memcpy(Buffer, Text.c_str(), (Text.size() + 1) * sizeof(WCHAR));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxLocalizedStrings::GetLocaleNameLength(
    UINT32 index,
    _Out_ UINT32* length)
{
    *length = MAXUINT32;
    if (index >= Entries.size())
        return E_FAIL;
    *length = static_cast<UINT32>(Entries[index].Locale.size());
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxLocalizedStrings::GetLocaleName(
    UINT32 index,
    _Out_writes_z_(size) WCHAR* localeName,
    UINT32 size)
{
    if (size == 0)
        return E_NOT_SUFFICIENT_BUFFER;
    if (index >= Entries.size())
        return E_FAIL;
    return RxCopyText(Entries[index].Locale, localeName, size);
}

HRESULT
STDMETHODCALLTYPE
RxLocalizedStrings::GetStringLength(
    UINT32 index,
    _Out_ UINT32* length)
{
    *length = MAXUINT32;
    if (index >= Entries.size())
        return E_FAIL;
    *length = static_cast<UINT32>(Entries[index].Text.size());
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxLocalizedStrings::GetString(
    UINT32 index,
    _Out_writes_z_(size) WCHAR* stringBuffer,
    UINT32 size)
{
    if (size == 0)
        return E_NOT_SUFFICIENT_BUFFER;
    if (index >= Entries.size())
        return E_FAIL;
    return RxCopyText(Entries[index].Text, stringBuffer, size);
}
