/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font collections, families and fonts
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "collection.hpp"

static constexpr GUID RxCollectionIid = { 0x8e4a2f17, 0x93d0, 0x4b6c, { 0xa1, 0x5f, 0x2d, 0x70, 0xc8, 0x14, 0x6b, 0xe9 } };
static constexpr GUID RxFontIid = { 0x3c61d0a4, 0x5be2, 0x4f19, { 0x8d, 0x27, 0x96, 0x0e, 0xb3, 0x41, 0xc5, 0x7a } };

static const WCHAR RxFontsKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";

/* Invariant, case insensitive, as family names compare */
static
INT
RxCompareNames(
    _In_ const std::wstring& A,
    _In_ const std::wstring& B)
{
    return CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, A.c_str(), static_cast<INT>(A.size()), B.c_str(),
                          static_cast<INT>(B.size())) - CSTR_EQUAL;
}

USHORT
RxEffectiveWeight(
    _In_ const RxFontEntry& Entry,
    _In_ DWRITE_FONT_SIMULATIONS Simulations)
{
    if ((Simulations & DWRITE_FONT_SIMULATIONS_BOLD) && Entry.Description.Weight <= 700)
        return 700;
    return Entry.Description.Weight;
}

BYTE
RxEffectiveStyle(
    _In_ const RxFontEntry& Entry,
    _In_ DWRITE_FONT_SIMULATIONS Simulations)
{
    if (Simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE)
        return DWRITE_FONT_STYLE_OBLIQUE;
    return Entry.Description.Style;
}

/* Building the set */

static
VOID
RxAddPath(
    _Inout_ std::vector<std::wstring>& Paths,
    _In_ const std::wstring& Path)
{
    for (const auto& Existing : Paths)
    {
        if (_wcsicmp(Existing.c_str(), Path.c_str()) == 0)
            return;
    }
    Paths.push_back(Path);
}

/* REG_SZ values of a key, combined with a folder unless they are full paths */
static
VOID
RxEnumerateFontKey(
    _In_ HKEY Key,
    _In_opt_z_ PCWSTR Folder,
    _Inout_ std::vector<std::wstring>& Paths)
{
    WCHAR Name[256];
    WCHAR Data[MAX_PATH];

    for (DWORD Index = 0;; Index++)
    {
        DWORD NameSize = _countof(Name);
        DWORD DataSize = sizeof(Data) - sizeof(WCHAR);
        DWORD Type;
        LSTATUS Status;
        std::wstring Path;

        Status = RegEnumValueW(Key, Index, Name, &NameSize, nullptr, &Type, reinterpret_cast<BYTE*>(Data), &DataSize);
        if (Status == ERROR_NO_MORE_ITEMS)
            break;
        if (Status != ERROR_SUCCESS || Type != REG_SZ || DataSize < sizeof(WCHAR))
            continue;
        Data[DataSize / sizeof(WCHAR)] = UNICODE_NULL;

        BOOLEAN IsFull = (Data[0] != UNICODE_NULL && Data[1] == L':') || (Data[0] == L'\\' && Data[1] == L'\\');
        if (IsFull || Folder == nullptr)
            Path = Data;
        else
            Path = std::wstring(Folder) + Data;
        RxAddPath(Paths, Path);
    }
}

static
VOID
RxInstalledFontPaths(
    _Inout_ std::vector<std::wstring>& Paths)
{
    PCWSTR Folder = RxFontsDirectory();
    HKEY User = nullptr;
    HKEY Machine;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, RxFontsKey, 0, KEY_READ, &User) == ERROR_SUCCESS)
        RxEnumerateFontKey(User, nullptr, Paths);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, RxFontsKey, 0, KEY_READ, &Machine) == ERROR_SUCCESS)
    {
        RxEnumerateFontKey(Machine, Folder, Paths);
        RegCloseKey(Machine);
    }

    /* These are always present whatever the registry says */
    RxAddPath(Paths, std::wstring(Folder) + L"MARLETT.TTF");
    RxAddPath(Paths, std::wstring(Folder) + L"arial.ttf");
    RxAddPath(Paths, std::wstring(Folder) + L"segoeui.ttf");

    /* Fonts installed per package sit in subkeys of the user key and follow the system ones */
    if (User != nullptr)
    {
        WCHAR Subkey[256];

        for (DWORD Index = 0;; Index++)
        {
            DWORD Size = _countof(Subkey);
            HKEY Child;

            if (RegEnumKeyExW(User, Index, Subkey, &Size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            if (RegOpenKeyExW(User, Subkey, 0, KEY_READ, &Child) == ERROR_SUCCESS)
            {
                RxEnumerateFontKey(Child, nullptr, Paths);
                RegCloseKey(Child);
            }
        }
        RegCloseKey(User);
    }
}

VOID
RxFontSetData::AddFile(
    _In_ IDWriteFontFile* File)
{
    BOOL Supported;
    DWRITE_FONT_FILE_TYPE FileType;
    DWRITE_FONT_FACE_TYPE FaceType;
    UINT32 Faces;

    if (FAILED(File->Analyze(&Supported, &FileType, &FaceType, &Faces)) || !Supported)
        return;

    for (UINT32 Index = 0; Index < std::min<UINT32>(Faces, 65535); Index++)
    {
        RxFaceTables* Tables;
        RxFontEntry Entry;

        if (FAILED(RxFaceTables::Create(File, Index, &Tables)))
            continue;

        Entry.File = RxComPtr<IDWriteFontFile>(File);
        Entry.FaceIndex = Index;
        Entry.FaceType = Tables->Sfnt().IsCff() ? DWRITE_FONT_FACE_TYPE_CFF : DWRITE_FONT_FACE_TYPE_TRUETYPE;
        Entry.IsSymbol = Tables->IsSymbolFont() || (Tables->Header().HasOs2 && Tables->Header().Panose[0] == 5);
        RxDescribeFace(Tables, &Entry.Description);
        memset(&Entry.Signature, 0, sizeof(Entry.Signature));
        if (Tables->Header().HasOs2)
        {
            memcpy(Entry.Signature.fsUsb, Tables->Header().UnicodeRange, sizeof(Entry.Signature.fsUsb));
            memcpy(Entry.Signature.fsCsb, Tables->Header().CodePageRange, sizeof(Entry.Signature.fsCsb));
        }
        Tables->Release();

        if (!Entry.Description.WwsFamily.empty())
            Entries.push_back(std::move(Entry));
    }
}

VOID
RxFontSetData::BuildFamilies(VOID)
{
    auto Lower = [this](const NameIndex& Item, const std::wstring& Name) { return RxCompareNames(Item.Name, Name) < 0; };

    for (UINT32 Index = 0; Index < Entries.size(); Index++)
    {
        const RxNameList& Names = Entries[Index].Description.WwsFamily;
        const std::wstring& Identity = RxIdentityName(Names);
        UINT32 Family;
        auto Found = std::lower_bound(NameMap.begin(), NameMap.end(), Identity, Lower);

        if (Found != NameMap.end() && RxCompareNames(Found->Name, Identity) == 0)
        {
            Family = Found->Family;
        }
        else
        {
            Family = static_cast<UINT32>(Families.size());
            Families.push_back({ Names, {} });
            NameMap.insert(Found, { Identity, Family });
        }
        Families[Family].Items.push_back({ Index, DWRITE_FONT_SIMULATIONS_NONE });

        /* Other languages point at the same family unless another family took the name first */
        for (const auto& Localized : Names)
        {
            auto Slot = std::lower_bound(NameMap.begin(), NameMap.end(), Localized.Text, Lower);

            if (Slot == NameMap.end() || RxCompareNames(Slot->Name, Localized.Text) != 0)
                NameMap.insert(Slot, { Localized.Text, Family });
        }
    }

    /* Within a family: weight, style, then normal stretch first, then stretch, then set order */
    for (auto& Family : Families)
    {
        std::stable_sort(Family.Items.begin(), Family.Items.end(), [this](const RxFontItem& A, const RxFontItem& B)
        {
            const RxFontEntry& First = Entries[A.Entry];
            const RxFontEntry& Second = Entries[B.Entry];
            USHORT WeightA = RxEffectiveWeight(First, A.Simulations);
            USHORT WeightB = RxEffectiveWeight(Second, B.Simulations);
            BYTE StyleA = RxEffectiveStyle(First, A.Simulations);
            BYTE StyleB = RxEffectiveStyle(Second, B.Simulations);
            BYTE StretchA = First.Description.Stretch;
            BYTE StretchB = Second.Description.Stretch;

            if (WeightA != WeightB)
                return WeightA < WeightB;
            if (StyleA != StyleB)
                return StyleA < StyleB;
            if (StretchA != StretchB)
            {
                if (StretchA == DWRITE_FONT_STRETCH_NORMAL)
                    return true;
                if (StretchB == DWRITE_FONT_STRETCH_NORMAL)
                    return false;
                return StretchA < StretchB;
            }
            return A.Entry < B.Entry;
        });
    }
}

HRESULT
RxFontSetData::CreateSystem(
    _Outptr_ RxFontSetData** Data)
{
    std::vector<std::wstring> Paths;
    RxFontSetData* Result;

    *Data = nullptr;
    Result = new (std::nothrow) RxFontSetData();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    RxInstalledFontPaths(Paths);
    for (const auto& Path : Paths)
    {
        WIN32_FILE_ATTRIBUTE_DATA Attributes;
        std::vector<BYTE> Key;
        RxFontFile* File;

        if (!GetFileAttributesExW(Path.c_str(), GetFileExInfoStandard, &Attributes) ||
            (Attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (FAILED(RxLocalLoader::MakeKey(Path.c_str(), &Attributes.ftLastWriteTime, Key)))
            continue;
        if (FAILED(RxFontFile::Create(RxLocalLoader::Instance(), Key.data(), static_cast<UINT32>(Key.size()), &File)))
            continue;

        Result->AddFile(File);
        File->Release();
    }

    Result->BuildFamilies();
    Result->IsSystem = TRUE;
    RX_TRACE(L"system collection: %Iu fonts in %Iu families from %Iu files", Result->Entries.size(),
             Result->Families.size(), Paths.size());
    *Data = Result;
    return S_OK;
}

HRESULT
RxFontSetData::CreateFromFiles(
    _In_reads_(Count) IDWriteFontFile* const* Files,
    _In_ UINT32 Count,
    _Outptr_ RxFontSetData** Data)
{
    RxFontSetData* Result;

    *Data = nullptr;
    Result = new (std::nothrow) RxFontSetData();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    for (UINT32 Index = 0; Index < Count; Index++)
        Result->AddFile(Files[Index]);
    Result->BuildFamilies();
    *Data = Result;
    return S_OK;
}

UINT32
RxFontSetData::FindFamily(
    _In_z_ PCWSTR Name) const
{
    std::wstring Wanted = Name;
    auto Found = std::lower_bound(NameMap.begin(), NameMap.end(), Wanted,
                                  [](const NameIndex& Item, const std::wstring& Key) { return RxCompareNames(Item.Name, Key) < 0; });

    if (Found != NameMap.end() && RxCompareNames(Found->Name, Wanted) == 0)
        return Found->Family;
    return MAXUINT32;
}

UINT32
RxFontSetData::FamilyOfEntry(
    _In_ UINT32 Entry) const
{
    for (UINT32 Family = 0; Family < Families.size(); Family++)
    {
        for (const auto& Item : Families[Family].Items)
        {
            if (Item.Entry == Entry)
                return Family;
        }
    }
    return MAXUINT32;
}

/* GDI style matching */

static
BOOLEAN
RxListHasName(
    _In_ const RxNameList& Names,
    _In_reads_(Length) PCWCH Name,
    _In_ SIZE_T Length)
{
    for (const auto& Localized : Names)
    {
        if (CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, Localized.Text.c_str(),
                           static_cast<INT>(Localized.Text.size()), Name, static_cast<INT>(Length)) == CSTR_EQUAL)
            return TRUE;
    }
    return FALSE;
}

VOID
RxWin32Candidates(
    _In_ const RxFontSetData* Data,
    _In_reads_(Length) PCWCH Name,
    _In_ SIZE_T Length,
    _Inout_ std::vector<UINT32>& Entries)
{
    Entries.clear();

    /* Walked family by family in WWS order, which is the order earlier matches win ties in */
    for (UINT32 Pass = 0; Pass < 2 && Entries.empty(); Pass++)
    {
        for (const auto& Family : Data->Families)
        {
            for (const auto& Item : Family.Items)
            {
                const RxFaceDescription& Description = Data->Entries[Item.Entry].Description;

                if (RxListHasName(Pass == 0 ? Description.Win32Family : Description.FullName, Name, Length))
                    Entries.push_back(Item.Entry);
            }
        }
    }
}

UINT32
RxWin32Penalty(
    _In_ const RxFontEntry& Entry,
    _In_ UINT32 CharsetMask,
    _In_ LONG Weight,
    _In_ BOOLEAN Italic,
    _Out_ DWRITE_FONT_SIMULATIONS* Simulations)
{
    BYTE Style = Entry.Description.Style;
    BYTE Stretch = Entry.Description.Stretch;
    LONG Delta = Weight - static_cast<LONG>(Entry.Description.Win32Weight);
    UINT32 Penalty = 0;
    UINT32 Simulated = DWRITE_FONT_SIMULATIONS_NONE;
    UINT32 StretchCost;

    if (Italic)
    {
        if (Style != DWRITE_FONT_STYLE_ITALIC)
            Penalty = 1;
        if (Style == DWRITE_FONT_STYLE_NORMAL)
            Simulated |= DWRITE_FONT_SIMULATIONS_OBLIQUE;
    }
    else if (Style != DWRITE_FONT_STYLE_NORMAL)
    {
        Penalty = 4;
    }

    /* Far lighter faces get bold simulation and a smaller weight cost */
    if (Delta > 150)
    {
        Simulated |= DWRITE_FONT_SIMULATIONS_BOLD;
        Delta -= 120;
    }
    if (Delta < 0)
        Delta = -Delta;
    Penalty += static_cast<UINT32>(73 * Delta / 256);

    if ((Entry.Signature.fsCsb[0] & CharsetMask) == 0)
        Penalty += 65000;

    StretchCost = Stretch <= 5 ? 5 - Stretch : Stretch - 6;
    *Simulations = static_cast<DWRITE_FONT_SIMULATIONS>(Simulated);
    return StretchCost + 10 * Penalty;
}

/* Matching */

namespace
{

struct RxMatchPoint
{
    INT64 X;
    INT64 Y;
    INT64 Z;
    DWRITE_FONT_SIMULATIONS Simulations;
    USHORT RealWeight;
    BYTE RealStyle;
    BYTE Stretch;
    USHORT EffectiveWeight;
    BYTE EffectiveStyle;
};

RxMatchPoint
RxPointFor(
    _In_ const RxFontEntry& Entry,
    _In_ DWRITE_FONT_WEIGHT Weight,
    _In_ DWRITE_FONT_STYLE Style)
{
    RxMatchPoint Point = {};
    USHORT FaceWeight = Entry.Description.Weight;
    BYTE FaceStyle = Entry.Description.Style;
    UINT32 Simulations = 0;

    if (Weight > 550 && FaceWeight <= 550 && FaceWeight < static_cast<INT>(Weight) - 100)
        Simulations |= DWRITE_FONT_SIMULATIONS_BOLD;
    if (Style != DWRITE_FONT_STYLE_NORMAL && FaceStyle == DWRITE_FONT_STYLE_NORMAL)
        Simulations |= DWRITE_FONT_SIMULATIONS_OBLIQUE;

    Point.Simulations = static_cast<DWRITE_FONT_SIMULATIONS>(Simulations);
    Point.RealWeight = FaceWeight;
    Point.RealStyle = FaceStyle;
    Point.Stretch = Entry.Description.Stretch;
    Point.EffectiveWeight = (Simulations & DWRITE_FONT_SIMULATIONS_BOLD) ? 700 : FaceWeight;
    Point.EffectiveStyle = (Simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) ? DWRITE_FONT_STYLE_OBLIQUE : FaceStyle;
    Point.X = 1100 * (static_cast<INT64>(Point.Stretch) - 5);
    Point.Y = 700 * static_cast<INT64>(Point.EffectiveStyle);
    Point.Z = 5 * (static_cast<INT64>(Point.EffectiveWeight) - 400);
    return Point;
}

/* Whether Candidate beats Best for the request */
BOOLEAN
RxIsBetterMatch(
    _In_ const RxMatchPoint& Candidate,
    _In_ const RxMatchPoint& Best,
    _In_ INT64 X,
    _In_ INT64 Y,
    _In_ INT64 Z)
{
    INT64 CandidateDistance;
    INT64 BestDistance;
    INT64 CandidateDot;
    INT64 BestDot;

    if (Candidate.Simulations != Best.Simulations && Candidate.Stretch == Best.Stretch)
    {
        BOOLEAN CandidateOblique = (Candidate.Simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) != 0;
        BOOLEAN BestOblique = (Best.Simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) != 0;
        BOOLEAN CandidateBold = (Candidate.Simulations & DWRITE_FONT_SIMULATIONS_BOLD) != 0;
        BOOLEAN BestBold = (Best.Simulations & DWRITE_FONT_SIMULATIONS_BOLD) != 0;

        /* A real oblique face beats a simulated one */
        if (CandidateOblique != BestOblique && Candidate.EffectiveWeight == Best.EffectiveWeight)
        {
            const RxMatchPoint& Real = CandidateOblique ? Best : Candidate;

            if (Real.RealStyle == DWRITE_FONT_STYLE_OBLIQUE)
                return !CandidateOblique;
        }

        /* A real bold face beats a simulated one */
        if (CandidateBold != BestBold && Candidate.EffectiveStyle == Best.EffectiveStyle)
        {
            const RxMatchPoint& Real = CandidateBold ? Best : Candidate;

            if (Real.RealWeight > 550)
                return !CandidateBold;
        }
    }

    CandidateDistance = (Candidate.X - X) * (Candidate.X - X) + (Candidate.Y - Y) * (Candidate.Y - Y) +
                        (Candidate.Z - Z) * (Candidate.Z - Z);
    BestDistance = (Best.X - X) * (Best.X - X) + (Best.Y - Y) * (Best.Y - Y) + (Best.Z - Z) * (Best.Z - Z);
    if (CandidateDistance != BestDistance)
        return CandidateDistance < BestDistance;

    CandidateDot = Candidate.X * X + Candidate.Y * Y + Candidate.Z * Z;
    BestDot = Best.X * X + Best.Y * Y + Best.Z * Z;
    if (CandidateDot != BestDot)
        return CandidateDot > BestDot;

    if (Candidate.X != Best.X)
        return Candidate.X > Best.X;
    if (Candidate.Y != Best.Y)
        return Candidate.Y > Best.Y;
    return Candidate.Z > Best.Z;
}

}

SIZE_T
RxFindMatchingItem(
    _In_ const RxFontSetData* Data,
    _In_ const std::vector<RxFontItem>& Items,
    _In_ DWRITE_FONT_WEIGHT Weight,
    _In_ DWRITE_FONT_STRETCH Stretch,
    _In_ DWRITE_FONT_STYLE Style,
    _Out_ DWRITE_FONT_SIMULATIONS* Simulations)
{
    INT64 X = 1100 * (static_cast<INT64>(Stretch) - 5);
    INT64 Y = 700 * static_cast<INT64>(Style);
    INT64 Z = 5 * (static_cast<INT64>(Weight) - 400);
    RxMatchPoint Best;
    SIZE_T BestIndex = 0;

    *Simulations = DWRITE_FONT_SIMULATIONS_NONE;
    if (Items.empty())
        return MAXSIZE_T;

    for (SIZE_T Index = 0; Index < Items.size(); Index++)
    {
        const RxFaceDescription& Face = Data->Entries[Items[Index].Entry].Description;

        if (Face.Weight == Weight && Face.Stretch == Stretch && Face.Style == Style)
            return Index;
    }

    Best = RxPointFor(Data->Entries[Items[0].Entry], Weight, Style);
    for (SIZE_T Index = 1; Index < Items.size(); Index++)
    {
        RxMatchPoint Candidate = RxPointFor(Data->Entries[Items[Index].Entry], Weight, Style);

        if (RxIsBetterMatch(Candidate, Best, X, Y, Z))
        {
            Best = Candidate;
            BestIndex = Index;
        }
    }

    *Simulations = Best.Simulations;
    return BestIndex;
}

/* Simulated faces a WWS family adds so bold and oblique requests have something to match */
static
VOID
RxAddCompletenessSimulations(
    _In_ const RxFontSetData* Data,
    _Inout_ std::vector<RxFontItem>& Items)
{
    struct Record
    {
        RxFontItem Item;
        BYTE Style;
        BYTE Stretch;
        USHORT Weight;
    };
    std::vector<Record> Records;
    std::vector<Record> Added;
    SIZE_T Original;

    for (const auto& Item : Items)
    {
        const RxFontEntry& Entry = Data->Entries[Item.Entry];

        Records.push_back({ Item, RxEffectiveStyle(Entry, Item.Simulations), Entry.Description.Stretch,
                            RxEffectiveWeight(Entry, Item.Simulations) });
    }
    Original = Records.size();

    for (SIZE_T Index = 0; Index < Original; Index++)
    {
        SIZE_T Heaviest = Index;

        for (SIZE_T Other = Index; Other < Original; Other++)
        {
            if (Records[Other].Style == Records[Index].Style && Records[Other].Stretch == Records[Index].Stretch &&
                Records[Other].Weight > Records[Heaviest].Weight)
                Heaviest = Other;
        }
        if (Records[Heaviest].Weight >= 350 && Records[Heaviest].Weight <= 550)
        {
            Record Bold = Records[Heaviest];

            Bold.Weight = 700;
            Bold.Item.Simulations = static_cast<DWRITE_FONT_SIMULATIONS>(Bold.Item.Simulations | DWRITE_FONT_SIMULATIONS_BOLD);
            Records.push_back(Bold);
            Added.push_back(Bold);
        }
    }

    for (SIZE_T Index = 0; Index < Records.size(); Index++)
    {
        BOOLEAN HasOblique = FALSE;

        if (Records[Index].Style != DWRITE_FONT_STYLE_NORMAL)
            continue;
        for (SIZE_T Other = Index; Other < Records.size(); Other++)
        {
            if (Records[Other].Stretch == Records[Index].Stretch && Records[Other].Weight == Records[Index].Weight &&
                Records[Other].Style == DWRITE_FONT_STYLE_OBLIQUE)
            {
                HasOblique = TRUE;
                break;
            }
        }
        if (!HasOblique)
        {
            Record Oblique = Records[Index];

            Oblique.Style = DWRITE_FONT_STYLE_OBLIQUE;
            Oblique.Item.Simulations = static_cast<DWRITE_FONT_SIMULATIONS>(Oblique.Item.Simulations |
                                                                             DWRITE_FONT_SIMULATIONS_OBLIQUE);
            Records.push_back(Oblique);
            Added.push_back(Oblique);
        }
    }

    std::stable_sort(Added.begin(), Added.end(), [](const Record& A, const Record& B)
    {
        if (A.Weight != B.Weight)
            return A.Weight < B.Weight;
        if (A.Style != B.Style)
            return A.Style < B.Style;
        if (A.Stretch != B.Stretch)
        {
            if (A.Stretch == DWRITE_FONT_STRETCH_NORMAL)
                return true;
            if (B.Stretch == DWRITE_FONT_STRETCH_NORMAL)
                return false;
            return A.Stretch < B.Stretch;
        }
        return A.Item.Entry < B.Item.Entry;
    });

    for (const auto& Extra : Added)
        Items.push_back(Extra.Item);
}

/* RxFontCollection */

HRESULT
RxFontCollection::Create(
    _In_ RxFontSetData* Data,
    _Outptr_ RxFontCollection** Collection)
{
    *Collection = new (std::nothrow) RxFontCollection();
    if (*Collection == nullptr)
        return E_OUTOFMEMORY;
    Data->AddRef();
    (*Collection)->SetData = Data;
    return S_OK;
}

RxFontCollection::~RxFontCollection()
{
    if (SetData != nullptr)
        SetData->Release();
}

RxFontCollection*
RxFontCollection::FromInterface(
    _In_opt_ IUnknown* Collection)
{
    RxFontCollection* Result = nullptr;

    if (Collection == nullptr || FAILED(Collection->QueryInterface(RxCollectionIid, reinterpret_cast<void**>(&Result))))
        return nullptr;
    Result->Release();
    return Result;
}

HRESULT
STDMETHODCALLTYPE
RxFontCollection::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == RxCollectionIid || riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontCollection))
    {
        *ppvObject = static_cast<IDWriteFontCollection*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxFontCollection)

UINT32
STDMETHODCALLTYPE
RxFontCollection::GetFontFamilyCount(VOID)
{
    return static_cast<UINT32>(SetData->Families.size());
}

HRESULT
STDMETHODCALLTYPE
RxFontCollection::GetFontFamily(
    UINT32 index,
    _COM_Outptr_ IDWriteFontFamily** fontFamily)
{
    RxFontFamily* Family;
    HRESULT hr;

    if (fontFamily == nullptr)
        return E_INVALIDARG;
    *fontFamily = nullptr;
    if (index >= SetData->Families.size())
        return E_FAIL;

    hr = RxFontFamily::Create(this, index, TRUE, &Family);
    if (FAILED(hr))
        return hr;
    *fontFamily = Family;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontCollection::FindFamilyName(
    _In_z_ WCHAR const* familyName,
    _Out_ UINT32* index,
    _Out_ BOOL* exists)
{
    *index = MAXUINT32;
    *exists = FALSE;
    if (familyName == nullptr)
        return E_INVALIDARG;

    *index = SetData->FindFamily(familyName);
    *exists = *index != MAXUINT32;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontCollection::GetFontFromFontFace(
    _In_ IDWriteFontFace* fontFace,
    _COM_Outptr_ IDWriteFont** font)
{
    RxComPtr<IDWriteFontFile> File;
    UINT32 Files = 1;
    HRESULT hr;

    if (font == nullptr)
        return E_INVALIDARG;
    *font = nullptr;
    if (fontFace == nullptr)
        return E_INVALIDARG;

    hr = fontFace->GetFiles(&Files, File.Put());
    if (FAILED(hr))
        return hr;

    for (UINT32 Family = 0; Family < SetData->Families.size(); Family++)
    {
        for (const auto& Item : SetData->Families[Family].Items)
        {
            const RxFontEntry& Entry = SetData->Entries[Item.Entry];
            RxFontFile* Ours = static_cast<RxFontFile*>(Entry.File.Get());
            RxFontItem Wanted = { Item.Entry, fontFace->GetSimulations() };
            RxFont* Result;

            if (Entry.FaceIndex != fontFace->GetIndex() || !Ours->Matches(File.Get()))
                continue;

            hr = RxFont::Create(this, Family, Wanted, &Result);
            if (FAILED(hr))
                return hr;
            *font = Result;
            return S_OK;
        }
    }
    return DWRITE_E_NOFONT;
}

/* RxFontFamily */

HRESULT
RxFontFamily::Create(
    _In_ RxFontCollection* Collection,
    _In_ UINT32 Family,
    _In_ BOOLEAN AddSimulations,
    _Outptr_ RxFontFamily** Result)
{
    std::vector<RxFontItem> Items = Collection->Data()->Families[Family].Items;

    if (AddSimulations)
        RxAddCompletenessSimulations(Collection->Data(), Items);
    return CreateList(Collection, Family, std::move(Items), Result);
}

HRESULT
RxFontFamily::CreateList(
    _In_ RxFontCollection* Collection,
    _In_ UINT32 Family,
    _In_ std::vector<RxFontItem>&& Items,
    _Outptr_ RxFontFamily** Result)
{
    *Result = new (std::nothrow) RxFontFamily();
    if (*Result == nullptr)
        return E_OUTOFMEMORY;
    (*Result)->Collection = RxComPtr<RxFontCollection>(Collection);
    (*Result)->FamilyIndex = Family;
    (*Result)->Items = std::move(Items);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFamily::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontList) || riid == __uuidof(IDWriteFontFamily))
    {
        *ppvObject = static_cast<IDWriteFontFamily*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxFontFamily)

HRESULT
STDMETHODCALLTYPE
RxFontFamily::GetFontCollection(
    _COM_Outptr_ IDWriteFontCollection** fontCollection)
{
    if (fontCollection == nullptr)
        return E_INVALIDARG;
    *fontCollection = Collection.Copy();
    return S_OK;
}

UINT32
STDMETHODCALLTYPE
RxFontFamily::GetFontCount(VOID)
{
    return static_cast<UINT32>(Items.size());
}

HRESULT
STDMETHODCALLTYPE
RxFontFamily::GetFont(
    UINT32 index,
    _COM_Outptr_ IDWriteFont** font)
{
    RxFont* Result;
    HRESULT hr;

    if (font == nullptr)
        return E_INVALIDARG;
    *font = nullptr;
    if (index >= Items.size())
        return E_FAIL;

    hr = RxFont::Create(Collection.Get(), FamilyIndex, Items[index], &Result);
    if (FAILED(hr))
        return hr;
    *font = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFamily::GetFamilyNames(
    _COM_Outptr_ IDWriteLocalizedStrings** names)
{
    if (names == nullptr)
        return E_INVALIDARG;
    return RxLocalizedStrings::Create(Collection->Data()->Families[FamilyIndex].Names, names);
}

HRESULT
STDMETHODCALLTYPE
RxFontFamily::GetFirstMatchingFont(
    DWRITE_FONT_WEIGHT weight,
    DWRITE_FONT_STRETCH stretch,
    DWRITE_FONT_STYLE style,
    _COM_Outptr_ IDWriteFont** matchingFont)
{
    const std::vector<RxFontItem>& Real = Collection->Data()->Families[FamilyIndex].Items;
    DWRITE_FONT_SIMULATIONS Simulations;
    SIZE_T Index;
    RxFont* Result;
    HRESULT hr;

    if (matchingFont == nullptr)
        return E_INVALIDARG;
    *matchingFont = nullptr;

    Index = RxFindMatchingItem(Collection->Data(), Real, weight, stretch, style, &Simulations);
    if (Index == MAXSIZE_T)
        return E_FAIL;

    hr = RxFont::Create(Collection.Get(), FamilyIndex, { Real[Index].Entry, Simulations }, &Result);
    if (FAILED(hr))
        return hr;
    *matchingFont = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFamily::GetMatchingFonts(
    DWRITE_FONT_WEIGHT weight,
    DWRITE_FONT_STRETCH stretch,
    DWRITE_FONT_STYLE style,
    _COM_Outptr_ IDWriteFontList** matchingFonts)
{
    const RxFontSetData* Data = Collection->Data();
    std::vector<RxFontItem> Sorted = Data->Families[FamilyIndex].Items;
    INT64 X = 1100 * (static_cast<INT64>(stretch) - 5);
    INT64 Y = 700 * static_cast<INT64>(style);
    INT64 Z = 5 * (static_cast<INT64>(weight) - 400);
    RxFontFamily* Result;
    HRESULT hr;

    if (matchingFonts == nullptr)
        return E_INVALIDARG;
    *matchingFonts = nullptr;

    for (auto& Item : Sorted)
        Item.Simulations = RxPointFor(Data->Entries[Item.Entry], weight, style).Simulations;

    std::stable_sort(Sorted.begin(), Sorted.end(), [&](const RxFontItem& A, const RxFontItem& B)
    {
        return RxIsBetterMatch(RxPointFor(Data->Entries[A.Entry], weight, style),
                               RxPointFor(Data->Entries[B.Entry], weight, style), X, Y, Z) != FALSE;
    });

    hr = CreateList(Collection.Get(), FamilyIndex, std::move(Sorted), &Result);
    if (FAILED(hr))
        return hr;
    *matchingFonts = Result;
    return S_OK;
}

/* RxFont */

RxFont*
RxFont::FromInterface(
    _In_opt_ IUnknown* Font)
{
    RxFont* Result = nullptr;

    if (Font == nullptr || FAILED(Font->QueryInterface(RxFontIid, reinterpret_cast<void**>(&Result))))
        return nullptr;
    Result->Release();
    return Result;
}

HRESULT
RxFont::Create(
    _In_ RxFontCollection* Collection,
    _In_ UINT32 Family,
    _In_ const RxFontItem& Item,
    _Outptr_ RxFont** Result)
{
    *Result = new (std::nothrow) RxFont();
    if (*Result == nullptr)
        return E_OUTOFMEMORY;
    (*Result)->Collection = RxComPtr<RxFontCollection>(Collection);
    (*Result)->FamilyIndex = Family;
    (*Result)->Item = Item;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFont::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == RxFontIid || riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFont))
    {
        *ppvObject = static_cast<IDWriteFont*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxFont)

HRESULT
RxFont::Face(
    _Outptr_ RxFontFace** Face)
{
    RxFontFace* Result;
    HRESULT hr;

    if (!CachedFace.Get())
    {
        hr = RxFontFace::Create(Entry().File.Get(), Entry().FaceIndex, Item.Simulations, &Result);
        if (FAILED(hr))
            return hr;
        CachedFace.Attach(Result);
    }
    *Face = static_cast<RxFontFace*>(CachedFace.Get());
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFont::GetFontFamily(
    _COM_Outptr_ IDWriteFontFamily** fontFamily)
{
    RxFontFamily* Family;
    HRESULT hr;

    if (fontFamily == nullptr)
        return E_INVALIDARG;
    *fontFamily = nullptr;

    hr = RxFontFamily::Create(Collection.Get(), FamilyIndex, TRUE, &Family);
    if (FAILED(hr))
        return hr;
    *fontFamily = Family;
    return S_OK;
}

DWRITE_FONT_WEIGHT
STDMETHODCALLTYPE
RxFont::GetWeight(VOID)
{
    return static_cast<DWRITE_FONT_WEIGHT>(RxEffectiveWeight(Entry(), Item.Simulations));
}

DWRITE_FONT_STRETCH
STDMETHODCALLTYPE
RxFont::GetStretch(VOID)
{
    return static_cast<DWRITE_FONT_STRETCH>(Entry().Description.Stretch);
}

DWRITE_FONT_STYLE
STDMETHODCALLTYPE
RxFont::GetStyle(VOID)
{
    return static_cast<DWRITE_FONT_STYLE>(RxEffectiveStyle(Entry(), Item.Simulations));
}

BOOL
STDMETHODCALLTYPE
RxFont::IsSymbolFont(VOID)
{
    return Entry().IsSymbol;
}

HRESULT
STDMETHODCALLTYPE
RxFont::GetFaceNames(
    _COM_Outptr_ IDWriteLocalizedStrings** names)
{
    const RxNameList& Faces = Entry().Description.WwsFace;

    if (names == nullptr)
        return E_INVALIDARG;

    if (Item.Simulations != DWRITE_FONT_SIMULATIONS_NONE && !Faces.empty())
    {
        RxNameList Simulated;
        const RxNameEntry* Source = &Faces[0];

        for (const auto& Entry : Faces)
        {
            if (Entry.Locale == L"en-us")
                Source = &Entry;
        }
        Simulated.push_back({ Source->Locale, RxSimulatedFaceName(Source->Text, Item.Simulations) });
        return RxLocalizedStrings::Create(Simulated, names);
    }
    return RxLocalizedStrings::Create(Faces, names);
}

HRESULT
STDMETHODCALLTYPE
RxFont::GetInformationalStrings(
    DWRITE_INFORMATIONAL_STRING_ID informationalStringID,
    _COM_Outptr_result_maybenull_ IDWriteLocalizedStrings** informationalStrings,
    _Out_ BOOL* exists)
{
    /* Name ID for each informational string, by enum value */
    static const SHORT NameIds[] = { -1, 0, 5, 7, 8, 9, 12, 10, 11, 13, 14, 1, 2, 16, 17, 19, 4, 6, 20, 21 };
    RxFaceTables* Tables;
    RxNameList Names;
    HRESULT hr;

    if (informationalStrings == nullptr || exists == nullptr)
        return E_INVALIDARG;
    *informationalStrings = nullptr;
    *exists = FALSE;

    if (static_cast<UINT32>(informationalStringID) >= _countof(NameIds) || NameIds[informationalStringID] < 0)
        return S_OK;

    if (informationalStringID == DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES)
        Names = Entry().Description.Win32Family;
    else if (informationalStringID == DWRITE_INFORMATIONAL_STRING_WIN32_SUBFAMILY_NAMES)
        Names = Entry().Description.Win32Face;
    else if (informationalStringID == DWRITE_INFORMATIONAL_STRING_FULL_NAME)
        Names = Entry().Description.FullName;
    else if (informationalStringID == DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME)
        Names = Entry().Description.PostscriptName;
    else
    {
        hr = RxFaceTables::Create(Entry().File.Get(), Entry().FaceIndex, &Tables);
        if (FAILED(hr))
            return hr;
        RxReadNames(Tables, static_cast<USHORT>(NameIds[informationalStringID]), Names);
        Tables->Release();
    }

    if (Names.empty())
        return S_OK;

    hr = RxLocalizedStrings::Create(Names, informationalStrings);
    if (SUCCEEDED(hr))
        *exists = TRUE;
    return hr;
}

DWRITE_FONT_SIMULATIONS
STDMETHODCALLTYPE
RxFont::GetSimulations(VOID)
{
    return Item.Simulations;
}

VOID
STDMETHODCALLTYPE
RxFont::GetMetrics(
    _Out_ DWRITE_FONT_METRICS* fontMetrics)
{
    RxFontFace* Result;

    memset(fontMetrics, 0, sizeof(*fontMetrics));
    if (SUCCEEDED(Face(&Result)))
        Result->GetMetrics(fontMetrics);
}

HRESULT
STDMETHODCALLTYPE
RxFont::HasCharacter(
    UINT32 unicodeValue,
    _Out_ BOOL* exists)
{
    RxFontFace* Result;
    HRESULT hr;

    *exists = FALSE;
    hr = Face(&Result);
    if (FAILED(hr))
        return hr;
    *exists = Result->HasCharacter(unicodeValue);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFont::CreateFontFace(
    _COM_Outptr_ IDWriteFontFace** fontFace)
{
    RxFontFace* Result;
    HRESULT hr;

    if (fontFace == nullptr)
        return E_INVALIDARG;
    *fontFace = nullptr;

    hr = Face(&Result);
    if (FAILED(hr))
        return hr;
    Result->AddRef();
    *fontFace = Result;
    return S_OK;
}
