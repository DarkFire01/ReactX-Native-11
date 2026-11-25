/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12RootSignature and the device's checks before a root signature reaches the driver
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

/* What a device leaves of the flags a blob carries before checking it */
constexpr RxRootReadRules RxDeviceReadRules = { 0xFFF0, 0xFFF1, 0xF000, RxRootFillDefaults | RxRootFillUnbounded };

/* Drivers before this build do not know descriptors that are static but keep bounds checks */
constexpr UINT RxBoundsChecksDdiBuild = 28;

/* Drivers before this build cannot take directly indexed heaps */
constexpr UINT RxDirectHeapDdiBuild = 102;

/* Root arguments an application may use, in 32 bit units */
constexpr UINT RxRootArgumentLimit = 64;

/* Per stage counts, slot 0 holding what every stage sees */
struct RxStageCounts
{
    UINT64 Slots[D3D12_SHADER_VISIBILITY_MESH + 1];
};

/* Whether a class of descriptor stays within the tier's limit in every stage */
static
BOOLEAN
RxCountsFit(
    _In_ const RxStageCounts& Counts,
    _In_ UINT Limit)
{
    for (UINT Stage = 0; Stage < _countof(Counts.Slots); Stage++)
    {
        UINT64 Seen = Counts.Slots[Stage];

        if (Stage != 0)
            Seen += Counts.Slots[0];
        if (Seen > Limit && (Stage == 0 || Seen > Counts.Slots[0]))
            return FALSE;
    }
    return TRUE;
}

/* The heap offsets one table covers for one descriptor class, merged where ranges overlap */
struct RxTableSpan
{
    UINT64 First;
    UINT64 Last;
};

static
UINT64
RxTableClassSize(
    _In_ const D3D12_ROOT_DESCRIPTOR_TABLE1& Table,
    _In_ D3D12_DESCRIPTOR_RANGE_TYPE Class,
    _In_ BOOLEAN MergeOverlaps)
{
    RxTableSpan Spans[64];
    UINT SpanCount = 0;
    UINT64 NextOffset = 0;
    UINT64 Total = 0;

    for (UINT Index = 0; Index < Table.NumDescriptorRanges; Index++)
    {
        const D3D12_DESCRIPTOR_RANGE1& Range = Table.pDescriptorRanges[Index];
        UINT64 First = Range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                           ? NextOffset
                           : Range.OffsetInDescriptorsFromTableStart;
        UINT64 Last = Range.NumDescriptors == UINT_MAX ? UINT_MAX : First + Range.NumDescriptors - 1;

        NextOffset = Range.NumDescriptors == UINT_MAX ? 0 : First + Range.NumDescriptors;
        if (Range.RangeType != Class)
            continue;

        if (!MergeOverlaps || SpanCount == _countof(Spans))
        {
            Total += Last - First + 1;
            continue;
        }

        /* Fold every span this range touches into it */
        for (UINT Other = 0; Other < SpanCount;)
        {
            if (Spans[Other].First <= Last && First <= Spans[Other].Last)
            {
                if (Spans[Other].First < First)
                    First = Spans[Other].First;
                if (Spans[Other].Last > Last)
                    Last = Spans[Other].Last;
                Spans[Other] = Spans[SpanCount - 1];
                SpanCount -= 1;
                continue;
            }
            Other++;
        }
        Spans[SpanCount].First = First;
        Spans[SpanCount].Last = Last;
        SpanCount++;
    }

    for (UINT Index = 0; Index < SpanCount; Index++)
        Total += Spans[Index].Last - Spans[Index].First + 1;
    return Total;
}

/* The device level rules: binding tier limits, root argument space and what lower levels lack */
static
HRESULT
RxCheckRootLimits(
    _In_ RxDevice12& Device,
    _In_ const D3D12_ROOT_SIGNATURE_DESC2& Desc,
    _In_ UINT BlobFlags)
{
    const UINT Tier = (UINT)Device.GetOptions().ResourceBindingTier;
    const D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    const BOOLEAN IsCore = Level == D3D_FEATURE_LEVEL_1_0_CORE;
    const BOOLEAN MergeOverlaps = Device.GetHighestShaderModel() >= D3D_SHADER_MODEL_6_0;
    const BOOLEAN IsLocal = (Desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE) != 0;
    RxStageCounts Cbvs = {};
    RxStageCounts Srvs = {};
    RxStageCounts Uavs = {};
    RxStageCounts Samplers = {};
    UINT64 Cost = 0;
    UINT CbvLimit;
    UINT SrvLimit;
    UINT UavLimit;
    UINT SamplerLimit;
    BOOLEAN Failed = FALSE;

    if (Tier < D3D12_RESOURCE_BINDING_TIER_1 || Tier > D3D12_RESOURCE_BINDING_TIER_3)
        return E_INVALIDARG;
    if (Level == D3D_FEATURE_LEVEL_1_0_GENERIC)
        return E_INVALIDARG;

    switch (Tier)
    {
        case D3D12_RESOURCE_BINDING_TIER_1:
            SrvLimit = 128;
            SamplerLimit = 16;
            CbvLimit = 14;
            UavLimit = Level < D3D_FEATURE_LEVEL_11_1 ? 8 : 64;
            break;
        case D3D12_RESOURCE_BINDING_TIER_2:
            SrvLimit = UINT_MAX;
            SamplerLimit = UINT_MAX;
            CbvLimit = 14;
            UavLimit = 64;
            break;
        default:
            SrvLimit = UINT_MAX;
            SamplerLimit = UINT_MAX;
            CbvLimit = UINT_MAX;
            UavLimit = UINT_MAX;
            break;
    }
    if (CbvLimit < 15 && (INT)BlobFlags < 0)
        CbvLimit = 15;

    if (IsLocal && Device.GetOptions().RaytracingTier == 0)
        Failed = TRUE;

    for (UINT Index = 0; Index < Desc.NumParameters; Index++)
    {
        const D3D12_ROOT_PARAMETER1& Parameter = Desc.pParameters[Index];
        UINT Stage = (UINT)Parameter.ShaderVisibility;

        if (Stage > D3D12_SHADER_VISIBILITY_MESH)
            return E_INVALIDARG;

        switch (Parameter.ParameterType)
        {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            {
                const D3D12_ROOT_DESCRIPTOR_TABLE1& Table = Parameter.DescriptorTable;

                for (UINT RangeIndex = 0; RangeIndex < Table.NumDescriptorRanges; RangeIndex++)
                {
                    const D3D12_DESCRIPTOR_RANGE1& Range = Table.pDescriptorRanges[RangeIndex];

                    if ((UINT)Range.RangeType > D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                        return E_INVALIDARG;
                    if (IsCore &&
                        ((Range.Flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS) != 0 ||
                         Range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER))
                    {
                        Failed = TRUE;
                    }
                }

                Srvs.Slots[Stage] += RxTableClassSize(Table, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MergeOverlaps);
                Uavs.Slots[Stage] += RxTableClassSize(Table, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, MergeOverlaps);
                Cbvs.Slots[Stage] += RxTableClassSize(Table, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, MergeOverlaps);
                Samplers.Slots[Stage] += RxTableClassSize(Table, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, MergeOverlaps);
                Cost += 1;
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                Cost += Parameter.Constants.Num32BitValues;
                Cbvs.Slots[Stage] += 1;
                break;
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
                Cost += 2;
                Cbvs.Slots[Stage] += 1;
                break;
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
                Cost += 2;
                Srvs.Slots[Stage] += 1;
                break;
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                Cost += 2;
                Uavs.Slots[Stage] += 1;
                break;
            default:
                break;
        }
    }

    if (IsCore && Desc.NumStaticSamplers != 0)
        Failed = TRUE;

    for (UINT Index = 0; Index < Desc.NumStaticSamplers; Index++)
    {
        UINT Stage = (UINT)Desc.pStaticSamplers[Index].ShaderVisibility;

        if (Stage > D3D12_SHADER_VISIBILITY_MESH)
            return E_INVALIDARG;
        Samplers.Slots[Stage] += 1;
    }

    if (!IsLocal && Cost > RxRootArgumentLimit)
        Failed = TRUE;

    if (Tier <= D3D12_RESOURCE_BINDING_TIER_1 && (!RxCountsFit(Srvs, SrvLimit) || !RxCountsFit(Samplers, SamplerLimit)))
        Failed = TRUE;
    if (Tier <= D3D12_RESOURCE_BINDING_TIER_2 && (!RxCountsFit(Uavs, UavLimit) || !RxCountsFit(Cbvs, CbvLimit)))
        Failed = TRUE;

    if (Failed)
    {
        RX_TRACE(L"root signature exceeds what the device allows");
        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT
RxRootSignature::Create(
    _In_ RxDevice12& Device,
    _In_ UINT NodeMask,
    _In_reads_bytes_(PartSize) const BYTE* Part,
    _In_ UINT PartSize,
    _In_ BOOLEAN Internal,
    _Outptr_ RxRootSignature** Signature)
{
    RxRootSignature* Created;
    HRESULT hr;

    *Signature = nullptr;

    Created = new (std::nothrow) RxRootSignature(Device, Internal);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(NodeMask, Part, PartSize);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Signature = Created;
    return S_OK;
}

HRESULT
RxRootSignature::Initialize(
    _In_ UINT NodeMask,
    _In_reads_bytes_(PartSize) const BYTE* Part,
    _In_ UINT PartSize)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    RxRootReadRules Rules = RxDeviceReadRules;
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* Wide;
    D3D12DDIARG_CREATE_ROOT_SIGNATURE_0100 Args = {};
    UINT BlobFlags = 0;
    HRESULT hr;

    Mask = NodeMask;
    Serialized = new (std::nothrow) BYTE[PartSize ? PartSize : 1];
    if (Serialized == nullptr)
        return E_OUTOFMEMORY;
    CopyMemory(Serialized, Part, PartSize);
    SerializedSize = PartSize;

    if (Device.GetDdiBuild() < RxBoundsChecksDdiBuild)
        Rules.Options |= RxRootFillBoundsChecks;

    hr = Reader.Parse(Part, PartSize, Rules);
    if (FAILED(hr))
        return hr;

    Wide = Reader.At(D3D_ROOT_SIGNATURE_VERSION_1_2);
    if (Wide == nullptr)
        return E_OUTOFMEMORY;

    /* The top flag bit is the runtime's own and passes the register checks */
    {
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC Checked = *Wide;

        Checked.Desc_1_2.Flags &= ~(D3D12_ROOT_SIGNATURE_FLAGS)0x80000000u;
        if (!RxVerifyRootSignature(&Checked))
            return E_FAIL;
    }

    if (PartSize >= 6 * sizeof(UINT))
    {
        UINT Version;

        CopyMemory(&Version, Part, sizeof(Version));
        if (Version >= 1 && Version <= 3)
            CopyMemory(&BlobFlags, Part + 5 * sizeof(UINT), sizeof(BlobFlags));
    }

    hr = RxCheckRootLimits(Device, Wide->Desc_1_2, BlobFlags);
    if (FAILED(hr))
        return hr;

    Layout = Wide->Desc_1_2;
    if (Device.GetDdiBuild() < RxDirectHeapDdiBuild ||
        Device.GetOptions().ResourceBindingTier < D3D12DDI_RESOURCE_BINDING_TIER_3)
    {
        Layout.Flags &= ~(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);
    }

    Args.Version = D3D12DDI_ROOT_SIGNATURE_VERSION_1_2;
    Args.pRootSignature_1_2 = reinterpret_cast<const D3D12DDI_ROOT_SIGNATURE_0100*>(&Layout);
    hr = Device.TranslateNodeMask(NodeMask, FALSE, &Args.NodeMask);
    if (FAILED(hr))
        return hr;

    DriverRoot.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivateRootSignatureSize(Device.Handle(), &Args));
    if (DriverRoot.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateRootSignature(Device.Handle(), &Args, DriverRoot);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver root signature failed 0x%08lX", hr);
        return hr;
    }
    IsDriverRootCreated = TRUE;
    Bypass.pInterfaceVtable = *reinterpret_cast<void**>(static_cast<ID3D12RootSignature*>(this));
    Bypass.pDriverObject = DriverRoot.pDrvPrivate;
    return S_OK;
}

RxRootSignature::~RxRootSignature()
{
    if (IsCached)
        Device.ForgetRootSignature(this);

    if (IsDriverRootCreated)
        Device.Ddi().pfnDestroyRootSignature(Device.Handle(), DriverRoot);

    delete[] Serialized;
}

HRESULT
STDMETHODCALLTYPE
RxRootSignature::QueryInterface(
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

    /* The base interfaces are a face of their own; only the root signature interface is the created pointer */
    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild))
    {
        *ppvObject = static_cast<ID3D12DeviceChild*>(ChildFace());
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12RootSignature) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12RootSignature*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

_Ret_maybenull_
RxRootSignature*
RxDevice12::FindRootSignature(
    _In_reads_bytes_(Size) const BYTE* Part,
    _In_ UINT Size,
    _In_ UINT NodeMask)
{
    RxRootSignature* Found = nullptr;

    AcquireSRWLockShared(&RootCacheLock);
    for (RxRootSignature* Each = RootCache; Each != nullptr; Each = Each->NextCached)
    {
        if (Each->Matches(Part, Size, NodeMask) && Each->TryAddRef())
        {
            Found = Each;
            break;
        }
    }
    ReleaseSRWLockShared(&RootCacheLock);
    return Found;
}

VOID
RxDevice12::RememberRootSignature(
    _In_ RxRootSignature* Signature)
{
    AcquireSRWLockExclusive(&RootCacheLock);
    Signature->NextCached = RootCache;
    RootCache = Signature;
    Signature->IsCached = TRUE;
    ReleaseSRWLockExclusive(&RootCacheLock);
}

VOID
RxDevice12::ForgetRootSignature(
    _In_ RxRootSignature* Signature)
{
    AcquireSRWLockExclusive(&RootCacheLock);
    for (RxRootSignature** Link = &RootCache; *Link != nullptr; Link = &(*Link)->NextCached)
    {
        if (*Link == Signature)
        {
            *Link = Signature->NextCached;
            break;
        }
    }
    Signature->IsCached = FALSE;
    ReleaseSRWLockExclusive(&RootCacheLock);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateRootSignature(
    _In_ UINT nodeMask,
    _In_reads_(blobLengthInBytes) const void* pBlobWithRootSignature,
    _In_ SIZE_T blobLengthInBytes,
    REFIID riid,
    _COM_Outptr_ void** ppvRootSignature)
{
    const BYTE* Blob = static_cast<const BYTE*>(pBlobWithRootSignature);
    RxRootSignature* Signature;
    const BYTE* Part;
    UINT PartSize;
    UINT ContainerSize = 0;
    UINT Translated;
    HRESULT hr;

    if (Level == D3D_FEATURE_LEVEL_1_0_GENERIC)
        return E_INVALIDARG;
    if (ppvRootSignature == nullptr)
        return S_FALSE;
    *ppvRootSignature = nullptr;

    /* The length given must be the one the container states */
    if (Blob != nullptr)
        CopyMemory(&ContainerSize, Blob + 24, sizeof(ContainerSize));
    if (blobLengthInBytes != ContainerSize)
        return E_INVALIDARG;

    if (Blob == nullptr || !RxFindRootPart(Blob, blobLengthInBytes, &Part, &PartSize))
        return E_FAIL;

    hr = TranslateNodeMask(nodeMask, FALSE, &Translated);
    if (FAILED(hr))
        return hr;

    Signature = FindRootSignature(Part, PartSize, nodeMask != 0 ? nodeMask : 1);
    if (Signature == nullptr)
    {
        hr = RxRootSignature::Create(*this, nodeMask != 0 ? nodeMask : 1, Part, PartSize, FALSE, &Signature);
        if (FAILED(hr))
            return hr;
        RememberRootSignature(Signature);
    }

    hr = Signature->QueryInterface(riid, ppvRootSignature);
    Signature->Release();
    return hr;
}
