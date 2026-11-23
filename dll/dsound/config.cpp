/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DirectMusic buffer configuration object and the RIFF reader that loads it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <mediaobj.h>
#include <strsafe.h>

const CLSID DsBufferConfigClass = { 0xB2F586D4, 0x5558, 0x49D1, { 0xA0, 0x7B, 0x32, 0x49, 0xDB, 0xBB, 0x33, 0xC2 } };
static const IID DsMusicObjectIid = { 0xD2AC28B5, 0xB39B, 0x11D1, { 0x87, 0x04, 0x00, 0x60, 0x08, 0x93, 0xB1, 0xBD } };
static const GUID DsSendEffectClass = { 0xEF602176, 0xBCBB, 0x49E0, { 0x8C, 0xCA, 0xE0, 0x9A, 0x5A, 0x15, 0x2B, 0x33 } };

#define DS_TAG(a, b, c, d) (static_cast<ULONG>(a) | (static_cast<ULONG>(b) << 8) | (static_cast<ULONG>(c) << 16) | (static_cast<ULONG>(d) << 24))

/* DirectMusic file errors the loader answers with */
#define DS_RIFF_E_PAST_END      static_cast<HRESULT>(0x88781210)
#define DS_RIFF_E_BAD_FILE      static_cast<HRESULT>(0x8878122D)
#define DS_RIFF_E_NOT_CONFIG    static_cast<HRESULT>(0x88781117)
#define DS_RIFF_E_OVERFLOW      HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)

/* Descriptor fields, as DirectMusic numbers them */
#define DS_DESC_OBJECT      0x0001
#define DS_DESC_CLASS       0x0002
#define DS_DESC_NAME        0x0004
#define DS_DESC_CATEGORY    0x0008
#define DS_DESC_FILE        0x0030
#define DS_DESC_VERSION     0x0080
#define DS_DESC_DATE        0x0100
#define DS_DESC_LOADED      0x0200

/* Parts of a configuration that were found */
#define DS_PART_BUFFER      0x01
#define DS_PART_MIXBINS     0x02
#define DS_PART_SPATIAL     0x04
#define DS_PART_EFFECTS     0x08

/* A send level must sit between silence and full volume */
#define DS_SEND_FLOOR       0xFFFFD8F0

/* The DirectMusic object descriptor */
struct DsMusicDesc
{
    DWORD Size;
    DWORD Valid;
    GUID Object;
    GUID Class;
    FILETIME Date;
    DWORD Version[2];
    WCHAR Name[64];
    WCHAR Category[64];
    WCHAR File[MAX_PATH];
    LONGLONG MemLength;
    BYTE* MemData;
    IStream* Stream;
};

static_assert(sizeof(DsMusicDesc) == 856, "the descriptor layout is fixed");

struct DECLSPEC_NOVTABLE DsMusicObject : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDescriptor(_Out_ DsMusicDesc* Desc) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDescriptor(_Inout_ DsMusicDesc* Desc) = 0;
    virtual HRESULT STDMETHODCALLTYPE ParseDescriptor(_In_ IStream* Stream, _Inout_ DsMusicDesc* Desc) = 0;
};

/* A chunk being read: its header, how much of it was consumed and the chunk it sits in */
struct DsRiffChunk
{
    ULONG Id;
    ULONG Size;
    ULONG Type;
    ULONG Done;
    DsRiffChunk* Outer;
    ULONGLONG End;
};

/* Walks the chunks of one level at a time; a chunk struct keeps its last header until it is read into again */
class DsRiffReader
{
public:
    DsRiffReader(
        _In_ IStream* Source,
        _In_ DsRiffChunk* Top) : Stream(Source), Current(Top), Outer(nullptr), Fresh(TRUE)
    {
    }

    BOOLEAN
    Next(
        _Inout_ HRESULT* Status);

    HRESULT
    Read(
        _Out_writes_bytes_(Bytes) PVOID Target,
        _In_ ULONG Bytes);

    VOID
    Descend(
        _Inout_ DsRiffChunk* Inner);

    VOID
    Ascend(VOID);

    IStream* Stream;
    DsRiffChunk* Current;
    DsRiffChunk* Outer;
    BOOLEAN Fresh;

private:
    HRESULT
    Enter(VOID);

    HRESULT
    Skip(VOID);
};

HRESULT
DsRiffReader::Enter(VOID)
{
    HRESULT hr;

    if (Current == nullptr)
        return E_FAIL;
    hr = Stream->Read(Current, 2 * sizeof(ULONG), nullptr);
    if (FAILED(hr))
        return hr;
    Current->Done = 0;
    if (Current->Id != DS_TAG('R', 'I', 'F', 'F') && Current->Id != DS_TAG('L', 'I', 'S', 'T'))
        return hr;
    hr = Stream->Read(&Current->Type, sizeof(Current->Type), nullptr);
    if (SUCCEEDED(hr))
        Current->Done += sizeof(Current->Type);
    return hr;
}

/* Charges the chunk to the one around it and moves the stream past whatever of it was not read */
HRESULT
DsRiffReader::Skip(VOID)
{
    ULONG Padded;
    HRESULT hr = S_OK;

    if (Current == nullptr)
        return E_FAIL;
    Padded = Current->Size + (Current->Size & 1);
    if (Outer != nullptr)
    {
        ULONG Whole = Current->Size + ((Current->Size & 1) | 8);
        ULONG Sum;

        if (Whole < Padded)
            return DS_RIFF_E_OVERFLOW;
        Sum = Outer->Done + Whole;
        if (Sum < Whole)
        {
            Outer->Done = 0xFFFFFFFFUL;
            return DS_RIFF_E_OVERFLOW;
        }
        Outer->Done = Sum;
        if (Outer->Done > Outer->Size + (Outer->Size & 1))
            hr = DS_RIFF_E_PAST_END;
    }
    if (Current->Done < Padded)
    {
        LARGE_INTEGER Move;

        Move.QuadPart = Padded - Current->Done;
        hr = Stream->Seek(Move, STREAM_SEEK_CUR, nullptr);
        if (FAILED(hr))
        {
            if (Outer != nullptr)
            {
                if (Outer->Size >= Outer->Done - 1)
                    hr = S_OK;
            }
            else if (Current->Size & 1)
            {
                hr = S_OK;
            }
        }
    }
    return hr;
}

/* Steps to the next chunk of this level, which fits only while the level has room for a header */
BOOLEAN
DsRiffReader::Next(
    _Inout_ HRESULT* Status)
{
    HRESULT Last = *Status;
    BOOLEAN Room;

    if (FAILED(Last))
    {
        Skip();
        return FALSE;
    }
    if (Fresh)
    {
        Fresh = FALSE;
    }
    else
    {
        Last = Skip();
        *Status = Last;
    }
    if (Current == nullptr)
        return FALSE;

    Room = Outer != nullptr ? Outer->Done < Outer->Size - 8 : Current->Done == 0;
    if (!Room)
        return FALSE;
    if (SUCCEEDED(Last))
        *Status = Enter();
    return SUCCEEDED(*Status);
}

HRESULT
DsRiffReader::Read(
    _Out_writes_bytes_(Bytes) PVOID Target,
    _In_ ULONG Bytes)
{
    ULONG Sum;
    HRESULT hr;

    if (Current == nullptr)
        return E_FAIL;
    if (Current->Done + Bytes < Bytes)
        return DS_RIFF_E_OVERFLOW;
    if (Current->Done + Bytes > Current->Size)
        Bytes = Current->Size - Current->Done;
    hr = Stream->Read(Target, Bytes, nullptr);
    if (FAILED(hr))
        return hr;
    Sum = Current->Done + Bytes;
    if (Sum < Current->Done)
    {
        Current->Done = 0xFFFFFFFFUL;
        return DS_RIFF_E_OVERFLOW;
    }
    Current->Done = Sum;
    return S_OK;
}

VOID
DsRiffReader::Descend(
    _Inout_ DsRiffChunk* Inner)
{
    Inner->Done = 0;
    Inner->Outer = Current;
    Outer = Current;
    Current = Inner;
    Fresh = TRUE;
}

VOID
DsRiffReader::Ascend(VOID)
{
    if (Current == nullptr)
        return;
    Current = Current->Outer;
    if (Current != nullptr)
        Outer = Current->Outer;
}

/* The header in front of each effect of a configuration */
struct DsEffectHeader
{
    ULONG Flags;
    GUID Class;
    GUID Reserved;
    GUID Send;
    ULONG Spare;
};

static_assert(sizeof(DsEffectHeader) == 56, "the effect header layout is fixed");

struct DsConfigEffect
{
    ULONG Flags;
    GUID Class;
    GUID Send;
    ULONG Spare;
    LONG Level;
    IMediaObject* Effect;
    DsConfigEffect* Next;
};

class DsBufferConfig;

class DsConfigPersistFace final : public IPersistStream
{
public:
    DsFaceLink Link;
    DsBufferConfig* Self;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    GetClassID(
        _Out_ CLSID* Class) override;

    HRESULT
    STDMETHODCALLTYPE
    IsDirty(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    Load(
        _In_ IStream* Stream) override;

    HRESULT
    STDMETHODCALLTYPE
    Save(
        _In_ IStream* Stream,
        BOOL ClearDirty) override;

    HRESULT
    STDMETHODCALLTYPE
    GetSizeMax(
        _Out_ ULARGE_INTEGER* Size) override;
};

class DsConfigMusicFace final : public DsMusicObject
{
public:
    DsFaceLink Link;
    DsBufferConfig* Self;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    GetDescriptor(
        _Out_ DsMusicDesc* Desc) override;

    HRESULT
    STDMETHODCALLTYPE
    SetDescriptor(
        _Inout_ DsMusicDesc* Desc) override;

    HRESULT
    STDMETHODCALLTYPE
    ParseDescriptor(
        _In_ IStream* Stream,
        _Inout_ DsMusicDesc* Desc) override;
};

/* A buffer description, 3D settings, mix bins and effects read from a DirectMusic file for a sink to build from */
class DsBufferConfig final : public DsObject
{
public:
    DsBufferConfig();
    ~DsBufferConfig() override;

    HRESULT
    Load(
        _In_ IStream* Stream);

    HRESULT
    LoadEffect(
        _Inout_ DsRiffReader* Reader);

    HRESULT
    SetDescriptor(
        _Inout_ DsMusicDesc* Desc);

    DsMusicDesc Desc;
    ULONG Parts;
    BYTE BufferDesc[20];
    BYTE Spatial[80];
    ULONG* MixBins;
    ULONG MixBinCount;
    DsConfigEffect* Effects;
    ULONG EffectCount;
    DsConfigPersistFace PersistFace;
    DsConfigMusicFace MusicFace;
};

DsBufferConfig::DsBufferConfig()
{
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.Size = sizeof(Desc);
    Desc.Valid = DS_DESC_CLASS;
    Desc.Class = DsBufferConfigClass;
    Parts = 0;
    ZeroMemory(BufferDesc, sizeof(BufferDesc));
    ZeroMemory(Spatial, sizeof(Spatial));
    MixBins = nullptr;
    MixBinCount = 0;
    Effects = nullptr;
    EffectCount = 0;
    PersistFace.Link.Object = this;
    PersistFace.Link.Count = 0;
    PersistFace.Self = this;
    MusicFace.Link.Object = this;
    MusicFace.Link.Count = 0;
    MusicFace.Self = this;
    Expose(IID_IPersistStream, &PersistFace, &PersistFace.Link);
    Expose(DsMusicObjectIid, &MusicFace, &MusicFace.Link);
}

DsBufferConfig::~DsBufferConfig()
{
    while (Effects != nullptr)
    {
        DsConfigEffect* Next = Effects->Next;

        if (Effects->Effect != nullptr)
            Effects->Effect->Release();
        HeapFree(GetProcessHeap(), 0, Effects);
        Effects = Next;
    }
    HeapFree(GetProcessHeap(), 0, MixBins);
}

/* Reads one effect form: its header makes the effect, a data chunk after it holds the effect's settings */
HRESULT
DsBufferConfig::LoadEffect(
    _Inout_ DsRiffReader* Reader)
{
    DsRiffChunk Part = {};
    DsEffectHeader Header = {};
    DsConfigEffect* Pending = nullptr;
    HRESULT Status = S_OK;
    BOOLEAN More;

    Reader->Descend(&Part);
    More = Reader->Next(&Status);
    while (More)
    {
        if (Part.Id == DS_TAG('f', 'x', 'h', 'r'))
        {
            BOOLEAN Usable;

            Status = Reader->Read(&Header, sizeof(Header));
            if (SUCCEEDED(Status) && (Header.Spare != 0 || Header.Reserved != GUID_NULL))
            {
                DsTrace(L"effect header with reserved fields set");
                Status = DS_RIFF_E_BAD_FILE;
            }
            if (SUCCEEDED(Status) && Header.Class == DsSendEffectClass)
            {
                if (Header.Send == GUID_NULL)
                    Status = DS_RIFF_E_BAD_FILE;
                Usable = TRUE;
            }
            else
            {
                Usable = Header.Send == GUID_NULL;
                if (!Usable)
                    Status = DS_RIFF_E_BAD_FILE;
            }

            /* Effects only count once the buffer description has been read */
            if (Usable && SUCCEEDED(Status) && (Parts & DS_PART_BUFFER))
            {
                DsConfigEffect* Effect = static_cast<DsConfigEffect*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Effect)));
                HRESULT Made = S_OK;

                if (Effect == nullptr)
                {
                    Status = E_OUTOFMEMORY;
                    Pending = nullptr;
                }
                else
                {
                    Effect->Flags = Header.Flags;
                    Effect->Class = Header.Class;
                    Effect->Send = Header.Send;
                    Effect->Spare = Header.Spare;
                    Status = S_OK;
                    if (Header.Class != DsSendEffectClass)
                    {
                        Made = CoCreateInstance(Header.Class, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IMediaObject),
                                                reinterpret_cast<void**>(&Effect->Effect));
                        Status = Made;
                    }
                    if (SUCCEEDED(Made))
                    {
                        DsConfigEffect** Link = &Effects;

                        while (*Link != nullptr)
                            Link = &(*Link)->Next;
                        *Link = Effect;
                        EffectCount += 1;
                        Parts |= DS_PART_EFFECTS;
                        Pending = Effect;
                    }
                    else
                    {
                        DsTrace(L"cannot make a configured effect (0x%08lX)", Made);
                        HeapFree(GetProcessHeap(), 0, Effect);
                        Pending = nullptr;
                    }
                }
            }
        }
        else if (Part.Id == DS_TAG('d', 'a', 't', 'a') && Pending != nullptr)
        {
            if (Pending->Effect != nullptr)
            {
                IPersistStream* Settings = nullptr;

                Status = Pending->Effect->QueryInterface(IID_IPersistStream, reinterpret_cast<void**>(&Settings));
                if (SUCCEEDED(Status))
                {
                    /* The effect reads its own settings, then the stream goes to the end of the chunk whatever it read */
                    if (Reader->Current != nullptr)
                    {
                        LARGE_INTEGER Zero = {};
                        ULARGE_INTEGER Here = {};

                        Reader->Stream->Seek(Zero, STREAM_SEEK_CUR, &Here);
                        Reader->Current->End = Here.QuadPart + (Reader->Current->Size & 1) + Reader->Current->Size;
                    }
                    Status = Settings->Load(Reader->Stream);
                    if (Reader->Current != nullptr)
                    {
                        LARGE_INTEGER To;

                        Reader->Current->Done = Reader->Current->Size + (Reader->Current->Size & 1);
                        To.QuadPart = static_cast<LONGLONG>(Reader->Current->End);
                        Reader->Stream->Seek(To, STREAM_SEEK_SET, nullptr);
                    }
                    Settings->Release();
                }
            }
            else if (Pending->Class == DsSendEffectClass)
            {
                Status = Reader->Read(&Pending->Level, sizeof(Pending->Level));
                if (SUCCEEDED(Status) && Pending->Level != 0 && static_cast<ULONG>(Pending->Level) < DS_SEND_FLOOR)
                {
                    Pending->Level = 0;
                    Status = E_INVALIDARG;
                }
            }
            Pending = nullptr;
        }
        More = Reader->Next(&Status);
    }
    Reader->Ascend();
    return Status;
}

HRESULT
DsBufferConfig::Load(
    _In_ IStream* Stream)
{
    DsRiffChunk Top = {};
    DsRiffChunk Child = {};
    DsRiffChunk Inner = {};
    DsRiffReader Reader(Stream, &Top);
    HRESULT Status = S_OK;
    BOOLEAN More;

    if (!Reader.Next(&Status) || Top.Type != DS_TAG('D', 'S', 'B', 'C'))
    {
        DsTrace(L"the stream holds no buffer configuration");
        return DS_RIFF_E_BAD_FILE;
    }

    Reader.Descend(&Child);
    More = Reader.Next(&Status);
    while (More)
    {
        switch (Child.Id)
        {
            case DS_TAG('L', 'I', 'S', 'T'):
                if (Child.Type == DS_TAG('U', 'N', 'F', 'O'))
                {
                    Reader.Descend(&Inner);
                    while (Reader.Next(&Status))
                    {
                        if (Inner.Id == DS_TAG('U', 'N', 'A', 'M'))
                        {
                            Status = Reader.Read(Desc.Name, sizeof(Desc.Name));
                            Desc.Valid |= DS_DESC_NAME;
                        }
                    }
                    Reader.Ascend();
                }
                else if (Child.Type == DS_TAG('f', 'x', 'l', 's'))
                {
                    Reader.Descend(&Inner);
                    while (Reader.Next(&Status))
                    {
                        if (Inner.Id == DS_TAG('R', 'I', 'F', 'F') && Inner.Type == DS_TAG('D', 'S', 'F', 'X'))
                            Status = LoadEffect(&Reader);
                    }
                    Reader.Ascend();
                }
                break;
            case DS_TAG('d', 's', '3', 'd'):
                Status = Reader.Read(Spatial, sizeof(Spatial));
                Parts |= DS_PART_SPATIAL;
                break;
            case DS_TAG('d', 's', 'b', 'd'):
                Status = Reader.Read(BufferDesc, sizeof(BufferDesc));
                Parts |= DS_PART_BUFFER;
                break;
            case DS_TAG('b', 's', 'i', 'd'):
            {
                SIZE_T Room = (Child.Size & ~7UL) + ((Child.Size & 7) != 0 ? 8 : 0);

                HeapFree(GetProcessHeap(), 0, MixBins);
                MixBins = static_cast<ULONG*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Room));
                if (MixBins == nullptr)
                {
                    Status = E_OUTOFMEMORY;
                    break;
                }
                Status = Reader.Read(MixBins, Child.Size);
                if (SUCCEEDED(Status))
                {
                    Parts |= DS_PART_MIXBINS;
                    MixBinCount = Child.Size / sizeof(ULONG);
                }
                break;
            }
            case DS_TAG('g', 'u', 'i', 'd'):
                Status = Reader.Read(&Desc.Object, sizeof(Desc.Object));
                Desc.Valid |= DS_DESC_OBJECT;
                break;
        }
        More = Reader.Next(&Status);
    }

    if (SUCCEEDED(Status))
        Desc.Valid |= DS_DESC_LOADED;
    return Status;
}

/* Takes the fields the caller marks that a configuration keeps, and reports S_FALSE when some were left out */
HRESULT
DsBufferConfig::SetDescriptor(
    _Inout_ DsMusicDesc* Source)
{
    ULONG Taken = 0;

    if (Source->Size < sizeof(*Source))
        return E_INVALIDARG;
    if (Source->Valid & DS_DESC_OBJECT)
    {
        Desc.Object = Source->Object;
        Taken |= DS_DESC_OBJECT;
    }
    if (Source->Valid & DS_DESC_NAME)
    {
        StringCchCopyW(Desc.Name, ARRAYSIZE(Desc.Name), Source->Name);
        Taken |= DS_DESC_NAME;
    }
    if (Source->Valid & DS_DESC_CATEGORY)
    {
        StringCchCopyW(Desc.Category, ARRAYSIZE(Desc.Category), Source->Category);
        Taken |= DS_DESC_CATEGORY;
    }
    if (Source->Valid & DS_DESC_FILE)
    {
        memcpy(Desc.File, Source->File, sizeof(Desc.File));
        Taken |= Source->Valid & DS_DESC_FILE;
    }
    if (Source->Valid & DS_DESC_VERSION)
    {
        memcpy(Desc.Version, Source->Version, sizeof(Desc.Version));
        Taken |= DS_DESC_VERSION;
    }
    if (Source->Valid & DS_DESC_DATE)
    {
        Desc.Date = Source->Date;
        Taken |= DS_DESC_DATE;
    }
    Desc.Valid |= Taken;
    if ((Source->Valid & ~Taken) == 0)
        return S_OK;
    Source->Valid = Taken;
    return S_FALSE;
}

/* Reads only what describes a configuration file, into the caller's descriptor */
static
HRESULT
DsParseConfig(
    _In_ IStream* Stream,
    _Inout_ DsMusicDesc* Desc)
{
    DsRiffChunk Top = {};
    DsRiffChunk Child = {};
    DsRiffChunk Inner = {};
    DsRiffReader Reader(Stream, &Top);
    HRESULT Status = S_OK;
    ULONG Found = 0;

    if (!Reader.Next(&Status) || Top.Type != DS_TAG('D', 'S', 'B', 'C'))
        return DS_RIFF_E_NOT_CONFIG;

    Reader.Descend(&Child);
    while (Reader.Next(&Status))
    {
        switch (Child.Id)
        {
            case DS_TAG('L', 'I', 'S', 'T'):
                if (Child.Type != DS_TAG('U', 'N', 'F', 'O'))
                    break;
                Reader.Descend(&Inner);
                while (Reader.Next(&Status))
                {
                    if (Inner.Id == DS_TAG('U', 'N', 'A', 'M'))
                    {
                        Found |= DS_DESC_NAME;
                        Status = Reader.Read(Desc->Name, sizeof(Desc->Name));
                    }
                }
                Reader.Ascend();
                break;
            case DS_TAG('g', 'u', 'i', 'd'):
                Found |= DS_DESC_OBJECT;
                Status = Reader.Read(&Desc->Object, sizeof(Desc->Object));
                break;
            case DS_TAG('d', 'a', 't', 'e'):
                Found |= DS_DESC_DATE;
                Status = Reader.Read(&Desc->Date, sizeof(Desc->Date));
                break;
            case DS_TAG('c', 'a', 't', 'g'):
                Found |= DS_DESC_CATEGORY;
                Status = Reader.Read(Desc->Category, sizeof(Desc->Category));
                break;
            case DS_TAG('v', 'e', 'r', 's'):
                Found |= DS_DESC_VERSION;
                Status = Reader.Read(Desc->Version, sizeof(Desc->Version));
                break;
        }
    }
    if (SUCCEEDED(Status))
        Desc->Valid = Found;
    return Status;
}

/* A stream is only taken when it and its method table are there */
static
BOOLEAN
DsUsableStream(
    _In_opt_ IStream* Stream)
{
    return Stream != nullptr && *reinterpret_cast<void**>(Stream) != nullptr && **reinterpret_cast<void***>(Stream) != nullptr;
}

HRESULT
STDMETHODCALLTYPE
DsConfigPersistFace::GetClassID(
    _Out_ CLSID* Class)
{
    DsApiGuard Guard;

    if (Class == nullptr)
        return E_INVALIDARG;
    *Class = DsBufferConfigClass;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsConfigPersistFace::IsDirty(VOID)
{
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
DsConfigPersistFace::Load(
    _In_ IStream* Stream)
{
    DsApiGuard Guard;

    if (!DsUsableStream(Stream))
        return E_INVALIDARG;
    return Self->Load(Stream);
}

HRESULT
STDMETHODCALLTYPE
DsConfigPersistFace::Save(
    _In_ IStream* Stream,
    BOOL ClearDirty)
{
    UNREFERENCED_PARAMETER(ClearDirty);
    if (!DsUsableStream(Stream))
        return E_INVALIDARG;
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
DsConfigPersistFace::GetSizeMax(
    _Out_ ULARGE_INTEGER* Size)
{
    if (Size == nullptr)
        return E_INVALIDARG;
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
DsConfigMusicFace::GetDescriptor(
    _Out_ DsMusicDesc* Desc)
{
    DsApiGuard Guard;

    if (Desc == nullptr)
        return E_INVALIDARG;
    if (Desc != &Self->Desc)
        memcpy(Desc, &Self->Desc, sizeof(*Desc));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsConfigMusicFace::SetDescriptor(
    _Inout_ DsMusicDesc* Desc)
{
    DsApiGuard Guard;

    if (Desc == nullptr)
        return E_INVALIDARG;
    return Self->SetDescriptor(Desc);
}

HRESULT
STDMETHODCALLTYPE
DsConfigMusicFace::ParseDescriptor(
    _In_ IStream* Stream,
    _Inout_ DsMusicDesc* Desc)
{
    DsApiGuard Guard;

    if (!DsUsableStream(Stream) || Desc == nullptr)
        return E_INVALIDARG;
    return DsParseConfig(Stream, Desc);
}

HRESULT
DsCreateBufferConfig(
    _In_ REFIID Iid,
    _COM_Outptr_ void** Object)
{
    DsBufferConfig* Config = new (std::nothrow) DsBufferConfig();
    HRESULT hr;

    *Object = nullptr;
    if (Config == nullptr)
        return E_OUTOFMEMORY;
    hr = Config->Query(Iid, TRUE, Object);
    if (FAILED(hr))
        delete Config;
    return hr;
}
