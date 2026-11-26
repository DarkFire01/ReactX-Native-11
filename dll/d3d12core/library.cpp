/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12PipelineLibrary: named pipeline states kept for later runs
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "pipeline.hpp"
#include "dxbchash.hpp"

extern "C" IMAGE_DOS_HEADER __ImageBase;

/*
 * A serialized library is the header, one record per name, the names one after another with their
 * terminators, zeros up to an 8 byte boundary, and last whatever the driver serialized for its pipelines.
 */
struct RxLibraryHeader
{
    GUID Magic;
    UINT FormatVersion;
    UINT HeaderSize;
    RxLibraryIdentity Identity;
    UINT EntryCount;
    UINT NameChars;
    UINT64 DriverDataOffset;
    UINT64 DriverDataSize;

    /* Covers the records and names, from the end of the header to the driver data */
    BYTE Checksum[16];

    /* Room for later fields, never read; an empty library serializes to the size Windows gives */
    BYTE Reserved[768];
};

static_assert(sizeof(RxLibraryHeader) == 872, "the serialized header has a fixed layout");

struct RxLibraryRecord
{
    BYTE Digest[16];
    UINT NameOffset;
    UINT NameLength;
    UINT DriverIndex;
};

static_assert(sizeof(RxLibraryRecord) == 28, "serialized records have a fixed layout");

static constexpr GUID RxLibraryMagic = {0x6f1c2a94, 0x3b5e, 0x4d07, {0x9a, 0x82, 0x51, 0xe4, 0x0c, 0x7b, 0xd3, 0x6e}};

/* Raised whenever the layout or the digest of a pipeline changes */
static constexpr UINT RxLibraryFormat = 1;

/* One name and the pipeline it stands for */
struct RxLibraryEntry
{
    RxLibraryEntry* NextInBucket;
    WCHAR* Name;
    SIZE_T NameLength;
    UINT NameHash;
    UINT DriverIndex;
    RxPipelineDigest Digest;

    /* The pipeline stored or last loaded under the name, which the library holds a use of */
    RxPipelineState* Live;
};

static
UINT
RxHashName(
    _In_reads_(Length) const WCHAR* Name,
    _In_ SIZE_T Length)
{
    UINT Hash = 2166136261u;
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
    {
        Hash ^= Name[Index];
        Hash *= 16777619u;
    }
    return Hash;
}

static
UINT64
RxAlignUp8(
    _In_ UINT64 Value)
{
    return (Value + 7) & ~(UINT64)7;
}

HRESULT
RxPipelineLibrary::Create(
    _In_ RxDevice12& Device,
    _In_reads_bytes_opt_(BlobLength) const BYTE* Blob,
    _In_ SIZE_T BlobLength,
    _Outptr_ RxPipelineLibrary** Library)
{
    RxPipelineLibrary* Created;
    HRESULT hr;

    *Library = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxPipelineLibrary(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Blob, BlobLength);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Library = Created;
    return S_OK;
}

VOID
RxPipelineLibrary::ReadIdentity()
{
    const IMAGE_NT_HEADERS* Nt;
    D3DKMT_QUERY_DEVICE_IDS Ids = {};
    D3DKMT_UMD_DRIVER_VERSION Umd = {};

    /* Asked for only once a blob is read or written, so plain libraries leave the kernel alone */
    if (IsIdentityRead)
        return;
    IsIdentityRead = TRUE;

    if (SUCCEEDED(Device.GetAdapter().QueryKernel(KMTQAITYPE_PHYSICALADAPTERDEVICEIDS, &Ids, sizeof(Ids))))
    {
        BlobIdentity.VendorId = Ids.DeviceIds.VendorID;
        BlobIdentity.DeviceId = Ids.DeviceIds.DeviceID;
        BlobIdentity.SubVendorId = Ids.DeviceIds.SubVendorID;
        BlobIdentity.SubSystemId = Ids.DeviceIds.SubSystemID;
        BlobIdentity.RevisionId = Ids.DeviceIds.RevisionID;
    }
    if (SUCCEEDED(Device.GetAdapter().QueryKernel(KMTQAITYPE_UMD_DRIVER_VERSION, &Umd, sizeof(Umd))))
        BlobIdentity.DriverVersion = (UINT64)Umd.DriverVersion.QuadPart;
    BlobIdentity.DdiVersion = Device.GetAdapter().GetVersion();

    /* The link time names the build, as the digest of a pipeline may change between builds */
    Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(&__ImageBase) + __ImageBase.e_lfanew);
    BlobIdentity.RuntimeBuild = Nt->FileHeader.TimeDateStamp;
}

HRESULT
RxPipelineLibrary::Initialize(
    _In_reads_bytes_opt_(BlobLength) const BYTE* Blob,
    _In_ SIZE_T BlobLength)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 Args = {};
    SIZE_T DriverOffset = 0;
    SIZE_T DriverSize = 0;
    HRESULT hr;

    if (BlobLength != 0)
    {
        hr = ReadBlob(Blob, BlobLength, &DriverOffset, &DriverSize);
        if (FAILED(hr))
            return hr;
    }

    /* The application's blob only has to last for this call */
    if (DriverSize != 0)
    {
        DriverData = new (std::nothrow) BYTE[DriverSize];
        if (DriverData == nullptr)
            return E_OUTOFMEMORY;
        CopyMemory(DriverData, Blob + DriverOffset, DriverSize);
        Args.pInitialData = DriverData;
        Args.InitialDataSize = DriverSize;
    }

    DriverLibrary.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivatePipelineLibrarySize(Device.Handle(), &Args));
    if (DriverLibrary.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreatePipelineLibrary(Device.Handle(), &Args, DriverLibrary);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreatePipelineLibrary of %Iu bytes failed 0x%08lX", DriverSize, hr);
        return hr == E_INVALIDARG ? D3D12_ERROR_DRIVER_VERSION_MISMATCH : hr;
    }
    IsDriverLibraryCreated = TRUE;
    return S_OK;
}

HRESULT
RxPipelineLibrary::ReadBlob(
    _In_reads_bytes_(BlobLength) const BYTE* Blob,
    _In_ SIZE_T BlobLength,
    _Out_ SIZE_T* DriverOffset,
    _Out_ SIZE_T* DriverSize)
{
    RxLibraryHeader Header;
    BYTE Checksum[16];
    const BYTE* Names;
    UINT64 Offset;
    UINT Index;
    HRESULT hr;

    *DriverOffset = 0;
    *DriverSize = 0;

    if (BlobLength < sizeof(Header))
        return E_INVALIDARG;
    CopyMemory(&Header, Blob, sizeof(Header));
    if (Header.Magic != RxLibraryMagic)
        return E_INVALIDARG;
    ReadIdentity();

    /* Another build of the runtime may lay out or digest pipelines differently */
    if (Header.FormatVersion != RxLibraryFormat || Header.Identity.RuntimeBuild != BlobIdentity.RuntimeBuild)
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    if (Header.HeaderSize != sizeof(Header))
        return E_INVALIDARG;

    if (Header.Identity.VendorId != BlobIdentity.VendorId || Header.Identity.DeviceId != BlobIdentity.DeviceId ||
        Header.Identity.SubVendorId != BlobIdentity.SubVendorId ||
        Header.Identity.SubSystemId != BlobIdentity.SubSystemId || Header.Identity.RevisionId != BlobIdentity.RevisionId)
    {
        return D3D12_ERROR_ADAPTER_NOT_FOUND;
    }
    if (Header.Identity.DriverVersion != BlobIdentity.DriverVersion ||
        Header.Identity.DdiVersion != BlobIdentity.DdiVersion)
    {
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    }

    /* Trailing bytes past the driver data are allowed, as on Windows */
    Offset = RxAlignUp8(sizeof(Header) + (UINT64)Header.EntryCount * sizeof(RxLibraryRecord) +
                        (UINT64)Header.NameChars * sizeof(WCHAR));
    if (Header.DriverDataOffset != Offset || Offset > BlobLength || Header.DriverDataSize > BlobLength - Offset ||
        Offset - sizeof(Header) > UINT_MAX)
    {
        return E_INVALIDARG;
    }

    RxDxbcHash(Blob + sizeof(Header), (UINT)(Offset - sizeof(Header)), Checksum);
    if (memcmp(Checksum, Header.Checksum, sizeof(Checksum)) != 0)
        return E_INVALIDARG;

    Names = Blob + sizeof(Header) + (SIZE_T)Header.EntryCount * sizeof(RxLibraryRecord);
    for (Index = 0; Index < Header.EntryCount; Index++)
    {
        RxLibraryRecord Record;
        RxPipelineDigest Digest;
        RxLibraryEntry* Entry;
        RxLibraryEntry* Other;
        WCHAR Terminator;

        CopyMemory(&Record, Blob + sizeof(Header) + (SIZE_T)Index * sizeof(Record), sizeof(Record));
        if ((UINT64)Record.NameOffset + Record.NameLength >= Header.NameChars || Record.DriverIndex == UINT_MAX)
            return E_INVALIDARG;
        CopyMemory(&Terminator, Names + ((SIZE_T)Record.NameOffset + Record.NameLength) * sizeof(WCHAR),
                   sizeof(Terminator));
        if (Terminator != UNICODE_NULL)
            return E_INVALIDARG;

        CopyMemory(Digest.Bytes, Record.Digest, sizeof(Digest.Bytes));
        hr = Insert(Names + (SIZE_T)Record.NameOffset * sizeof(WCHAR), Record.NameLength, Digest, Record.DriverIndex,
                    &Entry);
        if (FAILED(hr))
            return hr;

        /* A name with a terminator inside it, or one given twice, cannot have come from Serialize */
        if (wcslen(Entry->Name) != Entry->NameLength)
            return E_INVALIDARG;
        for (Other = Entry->NextInBucket; Other != nullptr; Other = Other->NextInBucket)
        {
            if (Other->NameHash == Entry->NameHash && Other->NameLength == Entry->NameLength &&
                memcmp(Other->Name, Entry->Name, Entry->NameLength * sizeof(WCHAR)) == 0)
            {
                return E_INVALIDARG;
            }
        }

        if (Record.DriverIndex >= NextDriverIndex)
            NextDriverIndex = Record.DriverIndex + 1;
    }

    *DriverOffset = (SIZE_T)Offset;
    *DriverSize = (SIZE_T)Header.DriverDataSize;
    return S_OK;
}

RxPipelineLibrary::~RxPipelineLibrary()
{
    UINT Index;

    /* Windows lets go of the driver library first; pipelines made from it outlive it */
    if (IsDriverLibraryCreated)
        Device.Ddi().pfnDestroyPipelineLibrary(Device.Handle(), DriverLibrary);

    for (Index = EntryCount; Index != 0; Index -= 1)
    {
        RxLibraryEntry* Entry = Entries[Index - 1];

        if (Entry->Live != nullptr)
            Entry->Live->ReleaseUse();
        delete[] Entry->Name;
        delete Entry;
    }
    delete[] Entries;
    delete[] Buckets;
    delete[] DriverData;
}

_Ret_maybenull_
RxLibraryEntry*
RxPipelineLibrary::Find(
    _In_reads_(Length) const WCHAR* Name,
    _In_ SIZE_T Length,
    _In_ UINT Hash) const
{
    RxLibraryEntry* Entry;

    if (BucketCount == 0)
        return nullptr;

    for (Entry = Buckets[Hash % BucketCount]; Entry != nullptr; Entry = Entry->NextInBucket)
    {
        if (Entry->NameHash == Hash && Entry->NameLength == Length &&
            memcmp(Entry->Name, Name, Length * sizeof(WCHAR)) == 0)
        {
            return Entry;
        }
    }
    return nullptr;
}

HRESULT
RxPipelineLibrary::Insert(
    _In_reads_bytes_(Length * sizeof(WCHAR)) const VOID* Name,
    _In_ SIZE_T Length,
    _In_ const RxPipelineDigest& Digest,
    _In_ UINT DriverIndex,
    _Outptr_ RxLibraryEntry** Entry)
{
    RxLibraryEntry* Created;
    UINT Index;

    *Entry = nullptr;

    if (EntryCount == UINT_MAX || Length >= UINT_MAX - NameChars)
        return E_INVALIDARG;

    if (EntryCount == EntryCapacity)
    {
        UINT Capacity = EntryCapacity != 0 ? EntryCapacity * 2 : 64;
        RxLibraryEntry** Grown = new (std::nothrow) RxLibraryEntry*[Capacity];

        if (Grown == nullptr)
            return E_OUTOFMEMORY;
        if (EntryCount != 0)
            CopyMemory(Grown, Entries, EntryCount * sizeof(*Entries));
        delete[] Entries;
        Entries = Grown;
        EntryCapacity = Capacity;
    }

    /* Two entries a bucket at most before the table doubles */
    if (EntryCount >= BucketCount * 2)
    {
        UINT Count = BucketCount != 0 ? BucketCount * 2 : 64;
        RxLibraryEntry** Grown = new (std::nothrow) RxLibraryEntry*[Count]();

        if (Grown == nullptr)
            return E_OUTOFMEMORY;
        for (Index = 0; Index < EntryCount; Index++)
        {
            RxLibraryEntry* Each = Entries[Index];

            Each->NextInBucket = Grown[Each->NameHash % Count];
            Grown[Each->NameHash % Count] = Each;
        }
        delete[] Buckets;
        Buckets = Grown;
        BucketCount = Count;
    }

    Created = new (std::nothrow) RxLibraryEntry();
    if (Created == nullptr)
        return E_OUTOFMEMORY;
    Created->Name = new (std::nothrow) WCHAR[Length + 1];
    if (Created->Name == nullptr)
    {
        delete Created;
        return E_OUTOFMEMORY;
    }
    CopyMemory(Created->Name, Name, Length * sizeof(WCHAR));
    Created->Name[Length] = UNICODE_NULL;
    Created->NameLength = Length;
    Created->NameHash = RxHashName(Created->Name, Length);
    Created->DriverIndex = DriverIndex;
    Created->Digest = Digest;

    Created->NextInBucket = Buckets[Created->NameHash % BucketCount];
    Buckets[Created->NameHash % BucketCount] = Created;
    Entries[EntryCount] = Created;
    EntryCount += 1;
    NameChars += Length + 1;
    SerializedSize = 0;

    *Entry = Created;
    return S_OK;
}

SIZE_T
RxPipelineLibrary::DriverDataOffset() const
{
    return (SIZE_T)RxAlignUp8(sizeof(RxLibraryHeader) + (SIZE_T)EntryCount * sizeof(RxLibraryRecord) +
                              NameChars * sizeof(WCHAR));
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    /* A library is a device child but not pageable */
    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12PipelineLibrary) || riid == __uuidof(ID3D12PipelineLibrary1) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12PipelineLibrary1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

SIZE_T
STDMETHODCALLTYPE
RxPipelineLibrary::GetSerializedSize(VOID)
{
    SIZE_T DriverSize;
    SIZE_T Size;

    AcquireSRWLockExclusive(&Lock);
    if (SerializedSize == 0)
    {
        AcquireSRWLockExclusive(&DriverLock);
        DriverSize = Device.Ddi().pfnCalcSerializedLibrarySize(Device.Handle(), DriverLibrary);
        ReleaseSRWLockExclusive(&DriverLock);
        SerializedSize = DriverDataOffset() + DriverSize;
    }
    Size = SerializedSize;
    ReleaseSRWLockExclusive(&Lock);
    return Size;
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::StorePipeline(
    _In_opt_ LPCWSTR pName,
    _In_ ID3D12PipelineState* pPipeline)
{
    RxPipelineState* State = RxUnwrap12<RxPipelineState>(pPipeline);
    RxLibraryEntry* Entry;
    SIZE_T Length;
    UINT DriverIndex;
    HRESULT hr;

    /* Every stored pipeline needs a name to be loaded by */
    if (pName == nullptr || State == nullptr || &State->GetOwner() != &Device)
        return E_INVALIDARG;
    Length = wcslen(pName);

    AcquireSRWLockExclusive(&Lock);
    if (Find(pName, Length, RxHashName(pName, Length)) != nullptr)
    {
        ReleaseSRWLockExclusive(&Lock);
        return E_INVALIDARG;
    }

    DriverIndex = NextDriverIndex;
    AcquireSRWLockExclusive(&DriverLock);
    hr = Device.Ddi().pfnAddPipelineStateToLibrary(Device.Handle(), DriverLibrary, State->GetDriverHandle(),
                                                   DriverIndex);
    ReleaseSRWLockExclusive(&DriverLock);
    if (FAILED(hr))
    {
        ReleaseSRWLockExclusive(&Lock);
        RX_TRACE(L"driver AddPipelineStateToLibrary failed 0x%08lX", hr);
        return hr;
    }

    /* The driver holds the pipeline at this index even if the entry cannot be made */
    NextDriverIndex += 1;
    hr = Insert(pName, Length, State->GetDigest(), DriverIndex, &Entry);
    if (SUCCEEDED(hr))
    {
        Entry->Live = State;
        State->AddUse();
    }
    ReleaseSRWLockExclusive(&Lock);
    return hr;
}

HRESULT
RxPipelineLibrary::Load(
    _In_ LPCWSTR Name,
    _In_ const RxPipelineDesc& Desc,
    _In_ const RxPipelineDigest& Digest,
    _In_ REFIID riid,
    _Outptr_ void** ppPipelineState)
{
    SIZE_T Length = wcslen(Name);
    RxPipelineState* State = nullptr;
    RxPipelineState* Created;
    RxPipelineState* Unused = nullptr;
    RxPipelineOrigin Origin;
    RxLibraryEntry* Entry;
    HRESULT hr;

    AcquireSRWLockShared(&Lock);
    Entry = Find(Name, Length, RxHashName(Name, Length));
    if (Entry != nullptr && memcmp(&Entry->Digest, &Digest, sizeof(Digest)) != 0)
        Entry = nullptr;
    if (Entry != nullptr && Entry->Live != nullptr)
    {
        State = Entry->Live;
        State->AddRef();
    }
    ReleaseSRWLockShared(&Lock);

    /* A name not stored, or stored for a pipeline made from another desc */
    if (Entry == nullptr)
        return E_INVALIDARG;

    if (State == nullptr)
    {
        /* The driver builds the pipeline from what it serialized for the entry */
        Origin.Digest = Digest;
        Origin.Library.hLibrary = DriverLibrary;
        Origin.Library.PipelineIndex = Entry->DriverIndex;

        AcquireSRWLockShared(&DriverLock);
        hr = RxPipelineState::Create(Device, Desc, FALSE, &Origin, &Created);
        ReleaseSRWLockShared(&DriverLock);
        if (FAILED(hr))
            return hr;

        /* Of two threads loading one name at once, the first pipeline kept is the one both get */
        AcquireSRWLockExclusive(&Lock);
        if (Entry->Live == nullptr)
        {
            Entry->Live = Created;
            Created->AddUse();
            State = Created;
        }
        else
        {
            State = Entry->Live;
            State->AddRef();
            Unused = Created;
        }
        ReleaseSRWLockExclusive(&Lock);

        if (Unused != nullptr)
            Unused->Release();
    }

    return RxHandOut12(State, riid, ppPipelineState);
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::LoadGraphicsPipeline(
    _In_ LPCWSTR pName,
    _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineState)
{
    RxPipelineDigest Digest;
    RxPipelineDesc Desc;
    HRESULT hr;

    if (ppPipelineState == nullptr)
        return E_POINTER;
    *ppPipelineState = nullptr;
    if (pName == nullptr || pDesc == nullptr)
        return E_INVALIDARG;

    RxGatherGraphicsDesc(*pDesc, &Desc);
    hr = RxDigestPipelineDesc(Desc, &Digest);
    if (FAILED(hr))
        return hr;
    return Load(pName, Desc, Digest, riid, ppPipelineState);
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::LoadComputePipeline(
    _In_ LPCWSTR pName,
    _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineState)
{
    RxPipelineDigest Digest;
    RxPipelineDesc Desc;
    HRESULT hr;

    if (ppPipelineState == nullptr)
        return E_POINTER;
    *ppPipelineState = nullptr;
    if (pName == nullptr || pDesc == nullptr)
        return E_INVALIDARG;

    RxGatherComputeDesc(*pDesc, &Desc);
    hr = RxDigestPipelineDesc(Desc, &Digest);
    if (FAILED(hr))
        return hr;
    return Load(pName, Desc, Digest, riid, ppPipelineState);
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::LoadPipeline(
    _In_ LPCWSTR pName,
    _In_ const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineState)
{
    RxPipelineDigest Digest;
    RxPipelineDesc Desc;
    HRESULT hr;

    if (ppPipelineState == nullptr)
        return E_POINTER;
    *ppPipelineState = nullptr;
    if (pName == nullptr || pDesc == nullptr)
        return E_INVALIDARG;

    hr = RxGatherPipelineStream(Device, *pDesc, &Desc, &Digest);
    if (FAILED(hr))
        return hr;
    return Load(pName, Desc, Digest, riid, ppPipelineState);
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::Serialize(
    _Out_writes_(DataSizeInBytes) void* pData,
    SIZE_T DataSizeInBytes)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    BYTE* Out = static_cast<BYTE*>(pData);
    RxLibraryHeader Header = {};
    SIZE_T DriverOffset;
    SIZE_T DriverSize;
    SIZE_T NameOffset = 0;
    BYTE* Names;
    UINT Index;
    HRESULT hr = S_OK;

    if (pData == nullptr)
        return E_INVALIDARG;

    AcquireSRWLockExclusive(&Lock);
    AcquireSRWLockExclusive(&DriverLock);

    /* The size GetSerializedSize gave stands until a pipeline is stored */
    ReadIdentity();
    DriverOffset = DriverDataOffset();
    if (SerializedSize != 0)
        DriverSize = SerializedSize - DriverOffset;
    else
        DriverSize = Ddi.pfnCalcSerializedLibrarySize(Device.Handle(), DriverLibrary);
    if (DataSizeInBytes < DriverOffset || DataSizeInBytes - DriverOffset < DriverSize ||
        DriverOffset - sizeof(Header) > UINT_MAX)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        Names = Out + sizeof(Header) + (SIZE_T)EntryCount * sizeof(RxLibraryRecord);
        for (Index = 0; Index < EntryCount; Index++)
        {
            const RxLibraryEntry& Entry = *Entries[Index];
            RxLibraryRecord Record;

            CopyMemory(Record.Digest, Entry.Digest.Bytes, sizeof(Record.Digest));
            Record.NameOffset = (UINT)NameOffset;
            Record.NameLength = (UINT)Entry.NameLength;
            Record.DriverIndex = Entry.DriverIndex;
            CopyMemory(Out + sizeof(Header) + (SIZE_T)Index * sizeof(Record), &Record, sizeof(Record));
            CopyMemory(Names + NameOffset * sizeof(WCHAR), Entry.Name, (Entry.NameLength + 1) * sizeof(WCHAR));
            NameOffset += Entry.NameLength + 1;
        }
        ZeroMemory(Names + NameOffset * sizeof(WCHAR), Out + DriverOffset - (Names + NameOffset * sizeof(WCHAR)));

        hr = Ddi.pfnSerializeLibrary(Device.Handle(), DriverLibrary, Out + DriverOffset);
        if (FAILED(hr))
            RX_TRACE(L"driver SerializeLibrary of %Iu bytes failed 0x%08lX", DriverSize, hr);
    }

    if (SUCCEEDED(hr))
    {
        Header.Magic = RxLibraryMagic;
        Header.FormatVersion = RxLibraryFormat;
        Header.HeaderSize = sizeof(Header);
        Header.Identity = BlobIdentity;
        Header.EntryCount = EntryCount;
        Header.NameChars = (UINT)NameChars;
        Header.DriverDataOffset = DriverOffset;
        Header.DriverDataSize = DriverSize;
        RxDxbcHash(Out + sizeof(Header), (UINT)(DriverOffset - sizeof(Header)), Header.Checksum);
        CopyMemory(Out, &Header, sizeof(Header));
        SerializedSize = DriverOffset + DriverSize;
    }

    ReleaseSRWLockExclusive(&DriverLock);
    ReleaseSRWLockExclusive(&Lock);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreatePipelineLibrary(
    _In_reads_(BlobLength) const void* pLibraryBlob,
    SIZE_T BlobLength,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineLibrary)
{
    RxPipelineLibrary* Library;
    HRESULT hr;

    if (ppPipelineLibrary != nullptr)
        *ppPipelineLibrary = nullptr;

    if (BlobLength != 0 && pLibraryBlob == nullptr)
        return E_INVALIDARG;
    if (BlobLength == 0 && ppPipelineLibrary == nullptr)
        return S_FALSE;

    /* A blob is read in full even when nothing is handed back, so a bad one still fails */
    hr = RxPipelineLibrary::Create(*this, BlobLength != 0 ? static_cast<const BYTE*>(pLibraryBlob) : nullptr,
                                   BlobLength, &Library);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Library, riid, ppPipelineLibrary);
}
