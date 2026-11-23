/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Adapter enumeration and what the kernel reports about each adapter
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

/* D3DKMT_ENUMADAPTERS_FILTER with compute only and display only adapters included */
static constexpr ULONG64 DcEnumerateEverything = 0x3;

/* The node description KMTQAITYPE_NODEMETADATA fills */
#pragma pack(push, 1)
struct DC_NODE_METADATA
{
    UINT NodeAndAdapter;
    UINT EngineType;
    WCHAR FriendlyName[32];
    UINT Flags;
    BYTE GpuMmuSupported;
    BYTE IoMmuSupported;
};
#pragma pack(pop)

/* The kernel engine kinds that carry a fixed name */
static const PCWSTR DcEngineTypeNames[] =
{
    nullptr,
    L"3D",
    L"Video Decode",
    L"Video Encode",
    L"Video Processing",
    L"Scene Assembly",
    L"Copy",
    L"Overlay",
    L"Crypto",
    L"Video Codec",
};

/* Names every engine, numbering repeats the way the system does */
class DcEngineNamer
{
public:
    std::wstring
    Next(
        _In_ const std::wstring& Base)
    {
        for (Entry& Seen : Names)
        {
            if (Seen.Name == Base)
            {
                UINT Number = Seen.Count;

                Seen.Count += 1;
                return Base + L" " + std::to_wstring(Number);
            }
        }

        Names.push_back({ Base, 1 });
        return Base;
    }

private:
    struct Entry
    {
        std::wstring Name;
        UINT Count;
    };

    std::vector<Entry> Names;
};

HRESULT
DcFactory::ReadPhysicalAdapters(
    _In_ const D3DKMT_ADAPTERINFO& Info,
    _Inout_ DcAdapterTraits* Traits)
{
    D3DKMT_NODE_PERFDATA NodePerf = {};
    D3DKMT_ADAPTER_PERFDATA AdapterPerf = {};
    BYTE Paravirtualized = 0;
    UINT PhysicalCount = 0;
    NTSTATUS Status;

    Status = DcQueryAdapterInfo(Info.hAdapter, KMTQAITYPE_PHYSICALADAPTERCOUNT, &PhysicalCount, sizeof(PhysicalCount));
    if (!NT_SUCCESS(Status))
    {
        if (Status != STATUS_INVALID_PARAMETER)
        {
            DcTrace(L"physical adapter count query failed (0x%08lX)", Status);
            return DcStatusHresult(Status);
        }

        PhysicalCount = 1;
    }

    if (NT_SUCCESS(DcQueryAdapterInfo(Info.hAdapter, KMTQAITYPE_NODEPERFDATA, &NodePerf, sizeof(NodePerf))) &&
        NodePerf.Frequency != 0 && NodePerf.MaxFrequency != 0 && NodePerf.MaxFrequencyOC != 0)
    {
        Traits->HasEngineFrequency = TRUE;
    }

    if (NT_SUCCESS(DcQueryAdapterInfo(Info.hAdapter, KMTQAITYPE_ADAPTERPERFDATA, &AdapterPerf, sizeof(AdapterPerf))))
    {
        if (AdapterPerf.MemoryFrequency != 0 && AdapterPerf.MaxMemoryFrequency != 0 &&
            AdapterPerf.MaxMemoryFrequencyOC != 0)
        {
            Traits->HasMemoryFrequency = TRUE;
        }

        if (AdapterPerf.Temperature != 0)
            Traits->HasTemperature = TRUE;
    }

    if (NT_SUCCESS(DcQueryAdapterInfo(Info.hAdapter, KMTQAITYPE_PARAVIRTUALIZATION_RENDER, &Paravirtualized,
                                      sizeof(Paravirtualized))))
    {
        Traits->IsParavirtualized = Paravirtualized != 0;
    }

    for (UINT Physical = 0; Physical < PhysicalCount; Physical++)
    {
        DcPhysicalAdapter Entry;
        D3DKMT_QUERYSTATISTICS Stats;
        DcEngineNamer Namer;
        UINT SegmentCount = 0;

        memset(&Stats, 0, sizeof(Stats));
        Stats.AdapterLuid = Info.AdapterLuid;

        if (Capabilities & DcFactoryEngineStatistics)
        {
            Stats.Type = D3DKMT_QUERYSTATISTICS_ADAPTER2;
            Stats.QueryAdapter2.PhysicalAdapterIndex = static_cast<UINT16>(Physical);
            Status = DcKmtCall(DcKmt()->QueryStatistics, &Stats);
            if (!NT_SUCCESS(Status))
            {
                DcTrace(L"engine count query failed (0x%08lX)", Status);
                return DcStatusHresult(Status);
            }

            SegmentCount = Stats.QueryResult.AdapterInformation.NbSegments;
            Entry.EngineCount = Stats.QueryResult.AdapterInformation.NodeCount;
        }

        for (UINT Node = 0; Node < Entry.EngineCount; Node++)
        {
            DC_NODE_METADATA Metadata = {};
            DcEngineName Name = {};
            std::wstring Base;

            Metadata.NodeAndAdapter = Node + (Physical << 16);
            Status = DcQueryAdapterInfo(Info.hAdapter, KMTQAITYPE_NODEMETADATA, &Metadata, sizeof(Metadata));
            if (!NT_SUCCESS(Status))
            {
                DcTrace(L"engine name query failed (0x%08lX)", Status);
                return DcStatusHresult(Status);
            }

            if (Metadata.EngineType != 0)
            {
                if (Metadata.EngineType < _countof(DcEngineTypeNames))
                    Base = DcEngineTypeNames[Metadata.EngineType];
            }
            else
            {
                Metadata.FriendlyName[_countof(Metadata.FriendlyName) - 1] = UNICODE_NULL;
                Base = Metadata.FriendlyName;
                if (Base.empty())
                    Base = L"Engine";
            }

            wcscpy_s(Name.Name, Namer.Next(Base).c_str());
            Entry.Engines.push_back(Name);
        }

        if (Capabilities & DcFactoryEngineStatistics)
        {
            for (UINT Segment = 0; Segment < SegmentCount; Segment++)
            {
                memset(&Stats.QueryResult, 0, sizeof(Stats.QueryResult));
                Stats.Type = D3DKMT_QUERYSTATISTICS_SEGMENT2;
                Stats.QuerySegment2.PhysicalAdapterIndex = static_cast<UINT16>(Physical);
                Stats.QuerySegment2.SegmentId = static_cast<UINT16>(Segment);
                Status = DcKmtCall(DcKmt()->QueryStatistics, &Stats);
                if (!NT_SUCCESS(Status))
                {
                    DcTrace(L"memory segment query failed (0x%08lX)", Status);
                    return DcStatusHresult(Status);
                }

                switch (Stats.QueryResult.SegmentInformation.SegmentProperties.SegmentType)
                {
                    case D3DKMT_QUERYSTATISTICS_SEGMENT_TYPE_MEMORY:
                        Entry.DedicatedSegments.push_back(static_cast<UINT16>(Segment));
                        break;

                    case D3DKMT_QUERYSTATISTICS_SEGMENT_TYPE_APERTURE:
                    case D3DKMT_QUERYSTATISTICS_SEGMENT_TYPE_SYSMEM:
                        Entry.SharedSegments.push_back(static_cast<UINT16>(Segment));
                        break;

                    case D3DKMT_QUERYSTATISTICS_SEGMENT_TYPE_PARTITION:
                        if (DcIsSystemFeatureEnabled(DcFeatureCarveoutApi))
                            Entry.PartitionSegments.push_back(static_cast<UINT16>(Segment));
                        break;

                    default:
                        break;
                }
            }
        }

        if (!DcIsSystemFeatureEnabled(DcFeatureCarveoutApi))
            Entry.PartitionSegments.clear();

        Traits->Physical.push_back(Entry);
    }

    return S_OK;
}

/* LUIDs order as one 64 bit number, high part on top */
static
ULONG64
DcLuidKey(
    _In_ const LUID& Luid)
{
    return (static_cast<ULONG64>(static_cast<ULONG>(Luid.HighPart)) << 32) | Luid.LowPart;
}

VOID
DcFactory::KeepAdapter(
    _In_ BOOLEAN Initial,
    _In_ const D3DKMT_ADAPTERINFO& Info,
    _In_ DcAdapterTraits& Traits,
    _In_ ULONG Flags,
    _In_ const DXCoreHardwareIDParts& HardwareId)
{
    ULONG64 Key = DcLuidKey(Info.AdapterLuid);
    size_t Slot = 0;
    DcAdapter* Adapter;

    while (Slot < Adapters.size() && DcLuidKey(Adapters[Slot]->Luid) < Key)
        Slot += 1;

    if (Slot < Adapters.size() && DcLuidKey(Adapters[Slot]->Luid) == Key)
    {
        /* Seen again: a removed adapter comes back as new, anything else stays */
        if (Adapters[Slot]->GetLifeState() == DcStateRemoved)
            Adapters[Slot]->SetLifeState(DcStateNew);
        else
            Adapters[Slot]->SetLifeState(DcStateValid);
        return;
    }

    Adapter = new (std::nothrow) DcAdapter();
    if (Adapter == nullptr)
        return;

    Adapter->Luid = Info.AdapterLuid;
    Adapter->Handle = Info.hAdapter;
    Adapter->Traits = Traits;
    Adapter->Flags = Flags;
    Adapter->HardwareId = HardwareId;
    Adapter->FactoryCapabilities = Capabilities;
    Adapter->ReadDriverDetails(Capabilities);
    Adapter->SetLifeState(Initial ? DcStateValid : DcStateNew);
    Adapters.insert(Adapters.begin() + Slot, Adapter);
}

HRESULT
DcFactory::Enumerate(
    _In_ BOOLEAN Initial)
{
    std::vector<D3DKMT_ADAPTERINFO> Found;
    D3DKMT_ENUMADAPTERS3 Enum;
    NTSTATUS Status;

    if (DcKmt()->EnumAdapters3 != nullptr)
    {
        do
        {
            memset(&Enum, 0, sizeof(Enum));
            *reinterpret_cast<ULONG64*>(&Enum.Filter) = DcEnumerateEverything;
            Status = DcKmt()->EnumAdapters3(&Enum);
            if (!NT_SUCCESS(Status))
            {
                DcTrace(L"adapter enumeration failed (0x%08lX)", Status);
                return DcStatusHresult(Status);
            }

            Found.resize(Enum.NumAdapters);
            Enum.pAdapters = Found.data();
            Status = DcKmt()->EnumAdapters3(&Enum);
        } while (Status == STATUS_BUFFER_TOO_SMALL);
    }
    else
    {
        D3DKMT_ENUMADAPTERS2 Enum2;

        /* Releases without the third form only list render and display adapters */
        do
        {
            memset(&Enum2, 0, sizeof(Enum2));
            Status = DcKmtCall(DcKmt()->EnumAdapters2, &Enum2);
            if (!NT_SUCCESS(Status))
            {
                DcTrace(L"adapter enumeration failed (0x%08lX)", Status);
                return DcStatusHresult(Status);
            }

            Found.resize(Enum2.NumAdapters);
            Enum2.pAdapters = Found.data();
            Status = DcKmt()->EnumAdapters2(&Enum2);
        } while (Status == STATUS_BUFFER_TOO_SMALL);

        Enum.NumAdapters = Enum2.NumAdapters;
    }

    if (!NT_SUCCESS(Status))
    {
        DcTrace(L"adapter enumeration failed (0x%08lX)", Status);
        return DcStatusHresult(Status);
    }

    Found.resize(Enum.NumAdapters);
    for (const D3DKMT_ADAPTERINFO& Info : Found)
    {
        D3DKMT_ADAPTERTYPE Type = {};
        D3DKMT_QUERY_DEVICE_IDS Ids = {};
        DXCoreHardwareIDParts HardwareId;
        DcAdapterTraits Traits;
        D3DKMT_CLOSEADAPTER Close;
        ULONG Flags = 0;

        if (!NT_SUCCESS(DcQueryAdapterInfo(Info.hAdapter, KMTQAITYPE_ADAPTERTYPE, &Type, sizeof(Type))) ||
            !NT_SUCCESS(DcQueryAdapterInfo(Info.hAdapter, KMTQAITYPE_PHYSICALADAPTERDEVICEIDS, &Ids, sizeof(Ids))))
        {
            Close.hAdapter = Info.hAdapter;
            DcKmtCall(DcKmt()->CloseAdapter, &Close);
            continue;
        }

        Traits.IsHardware = (Type.Value & DcKmtSoftwareDevice) == 0;
        Traits.AcgCompatible = (Type.Value & DcKmtAcgSupported) != 0;

        if (Type.Value & DcKmtDetachable)
            Flags |= DcFlagDetachable;
        if (Type.Value & DcKmtRenderSupported)
            Flags |= DcFlagRender;
        if (Type.Value & DcKmtComputeOnly)
            Flags |= DcFlagComputeOnly;
        if (Type.Value & DcKmtHybridIntegrated)
            Flags |= DcFlagHybrid | DcFlagIntegrated;
        if (Type.Value & DcKmtHybridDiscrete)
            Flags |= DcFlagHybrid | DcFlagDiscrete;
        if (Ids.DeviceIds.BusType == 2)
            Flags |= DcFlagPlainSubSystem;

        HardwareId.vendorID = Ids.DeviceIds.VendorID;
        HardwareId.deviceID = Ids.DeviceIds.DeviceID;
        HardwareId.subSystemID = Ids.DeviceIds.SubSystemID;
        HardwareId.subVendorID = Ids.DeviceIds.SubVendorID;
        HardwareId.revisionID = Ids.DeviceIds.RevisionID;

        /* The Basic Render Driver is never counted as hardware */
        if (Traits.IsHardware && HardwareId.vendorID == 0x1414 && HardwareId.deviceID == 0x8C)
            Traits.IsHardware = FALSE;

        if (FAILED(ReadPhysicalAdapters(Info, &Traits)))
        {
            Close.hAdapter = Info.hAdapter;
            DcKmtCall(DcKmt()->CloseAdapter, &Close);
            continue;
        }

        KeepAdapter(Initial, Info, Traits, Flags, HardwareId);
    }

    return S_OK;
}

/* Encodes each UTF-16 unit on its own, the way codecvt_utf8 over 16 bit units does */
static
std::string
DcNarrowDescription(
    _In_z_ PCWSTR Text)
{
    std::string Result;

    for (; *Text != UNICODE_NULL; Text += 1)
    {
        ULONG Unit = static_cast<USHORT>(*Text);

        if (Unit < 0x80)
        {
            Result.push_back(static_cast<char>(Unit));
        }
        else if (Unit < 0x800)
        {
            Result.push_back(static_cast<char>(0xC0 | (Unit >> 6)));
            Result.push_back(static_cast<char>(0x80 | (Unit & 0x3F)));
        }
        else
        {
            Result.push_back(static_cast<char>(0xE0 | (Unit >> 12)));
            Result.push_back(static_cast<char>(0x80 | ((Unit >> 6) & 0x3F)));
            Result.push_back(static_cast<char>(0x80 | (Unit & 0x3F)));
        }
    }

    return Result;
}

HRESULT
DcAdapter::ReadDriverStrings(
    _In_z_ PCWSTR ValueName,
    _Inout_ std::vector<std::wstring>* Strings)
{
    std::vector<BYTE> Buffer(sizeof(D3DDDI_QUERYREGISTRY_INFO));
    D3DDDI_QUERYREGISTRY_INFO* Query;
    D3DKMT_QUERYADAPTERINFO Request = {};
    size_t NameLength = wcslen(ValueName);
    NTSTATUS Status;
    PCWSTR Cursor;
    PCWSTR End;

    if (NameLength > 0x103)
        return E_INVALIDARG;

    Query = reinterpret_cast<D3DDDI_QUERYREGISTRY_INFO*>(Buffer.data());
    Query->QueryType = D3DDDI_QUERYREGISTRY_ADAPTERKEY;
    Query->ValueType = REG_MULTI_SZ;
    memcpy(Query->ValueName, ValueName, NameLength * sizeof(WCHAR));

    Request.hAdapter = Handle;
    Request.Type = KMTQAITYPE_QUERYREGISTRY;
    Request.pPrivateDriverData = Buffer.data();
    Request.PrivateDriverDataSize = static_cast<UINT>(Buffer.size());
    Status = DcKmtCall(DcKmt()->QueryAdapterInfo, &Request);

    if (NT_SUCCESS(Status) && Query->Status == D3DDDI_QUERYREGISTRY_STATUS_BUFFER_OVERFLOW)
    {
        D3DDDI_QUERYREGISTRY_INFO First = *Query;

        Buffer.resize(sizeof(D3DDDI_QUERYREGISTRY_INFO) + First.OutputValueSize);
        memcpy(Buffer.data(), &First, sizeof(First));
        Query = reinterpret_cast<D3DDDI_QUERYREGISTRY_INFO*>(Buffer.data());
        Request.pPrivateDriverData = Buffer.data();
        Request.PrivateDriverDataSize = static_cast<UINT>(Buffer.size());
        Status = DcKmtCall(DcKmt()->QueryAdapterInfo, &Request);
    }

    if (!NT_SUCCESS(Status))
        return DcStatusHresult(Status);

    if (Query->Status != D3DDDI_QUERYREGISTRY_STATUS_SUCCESS)
        return S_OK;

    Cursor = Query->OutputString;
    End = reinterpret_cast<PCWSTR>(reinterpret_cast<const BYTE*>(Query->OutputString) + Query->OutputValueSize);
    if (*Cursor == UNICODE_NULL)
        return S_OK;

    while (Cursor < End)
    {
        size_t Length = wcslen(Cursor);

        Strings->push_back(std::wstring(Cursor, Length));
        Cursor += Length + 1;
        if (*Cursor == UNICODE_NULL)
            break;
    }

    return S_OK;
}

/* The hardware types the driver may name, in the order a tie is broken */
static const GUID* const DcHardwareTypes[] =
{
    &DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU,
    &DXCORE_HARDWARE_TYPE_ATTRIBUTE_COMPUTE_ACCELERATOR,
    &DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU,
    &DXCORE_HARDWARE_TYPE_ATTRIBUTE_MEDIA_ACCELERATOR,
};

static const GUID DcWslAttribute = {0x42696d9d, 0xd678, 0x4006, {0xa9, 0x3d, 0x30, 0x0c, 0x35, 0x65, 0xbb, 0xe5}};

/* A block of attribute GUIDs the driver may list that the factory leaves out */
static
BOOLEAN
DcIsWithheldAttribute(
    _In_ const GUID& Attribute)
{
    static const GUID Base = {0x0bbdf5c6, 0x753d, 0x489f, {0x92, 0x15, 0x23, 0x9c, 0x75, 0xc9, 0x1b, 0xc0}};

    return memcmp(&Attribute, &Base, sizeof(GUID) - 1) == 0 && (Attribute.Data4[7] & 0xF0) == 0xC0;
}

static
BOOLEAN
DcIsRuntimeAttribute(
    _In_ const GUID& Attribute)
{
    return DcIsEqualGuid(Attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS) ||
           DcIsEqualGuid(Attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS) ||
           DcIsEqualGuid(Attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE) ||
           DcIsEqualGuid(Attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML) ||
           DcIsEqualGuid(Attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_MEDIA) ||
           DcIsEqualGuid(Attribute, DcWslAttribute);
}

VOID
DcAdapter::ReadDriverDetails(
    _In_ ULONG Capabilities)
{
    D3DKMT_WDDM_1_2_CAPS Caps = {};
    D3DKMT_SEGMENTSIZEINFO Sizes = {};
    D3DKMT_UMDFILENAMEINFO Umd;
    std::vector<std::wstring> Strings;
    std::vector<WCHAR> Text(4096);
    UINT64 UmdVersion = 0;
    UINT KmdVersion = 0;
    LONG KindIndex = -1;
    BOOLEAN HadD3D11;
    BOOLEAN HadD3D12;
    BOOLEAN HadCompute;
    HRESULT hr;

    if (NT_SUCCESS(DcQueryAdapterInfo(Handle, KMTQAITYPE_UMD_DRIVER_VERSION, &UmdVersion, sizeof(UmdVersion))))
        DriverVersion = UmdVersion;

    if (DcIsSystemFeatureEnabled(DcFeatureSingleAdapterHybridMode))
    {
        D3DKMT_ISFEATUREENABLED Feature = {};

        Feature.hAdapter = Handle;
        Feature.FeatureId = static_cast<DXGK_FEATURE_ID>(40);
        if (NT_SUCCESS(DcKmtCall(DcKmt()->IsFeatureEnabled, &Feature)) && Feature.Result.Enabled)
        {
            UINT Preference = 0x80000000;
            UINT Index = 0;
            UINT Factor = 0;
            DcAdapter* Self = this;

            Traits.HasHybridMode = TRUE;
            if (SUCCEEDED(DcQueryGpuPreference(&Self, 1, &Preference, &Index, &Factor)))
            {
                if (Preference == 1 || Preference == 2)
                {
                    HybridMode = Preference;
                }
                else if (Preference == 0x40000000)
                {
                    UINT Chosen = 0;

                    DcQueryUserSetting(17, &Chosen, sizeof(Chosen));
                    HybridMode = (Chosen == 1 || Chosen == 2) ? Chosen : 0;
                }
                else
                {
                    HybridMode = 0;
                }
            }
        }
    }

    if (NT_SUCCESS(DcQueryAdapterInfo(Handle, KMTQAITYPE_WDDM_1_2_CAPS, &Caps, sizeof(Caps))))
    {
        ComputePreemption = Caps.PreemptionCaps.ComputePreemptionGranularity;
        GraphicsPreemption = Caps.PreemptionCaps.GraphicsPreemptionGranularity;
    }

    if (NT_SUCCESS(DcQueryAdapterInfo(Handle, KMTQAITYPE_GETSEGMENTSIZE, &Sizes, sizeof(Sizes))))
    {
        DedicatedAdapterMemory = Sizes.DedicatedVideoMemorySize;
        DedicatedSystemMemory = Sizes.DedicatedSystemMemorySize;
        SharedSystemMemory = Sizes.SharedSystemMemorySize;
    }

    if (NT_SUCCESS(DcQueryAdapterInfo(Handle, KMTQAITYPE_DRIVERVERSION_RENDER, &KmdVersion, sizeof(KmdVersion))))
        KmdModelVersion = KmdVersion;

    if (NT_SUCCESS(DcQueryAdapterInfo(Handle, KMTQAITYPE_DRIVER_DESCRIPTION_RENDER, Text.data(),
                                      static_cast<UINT>(Text.size() * sizeof(WCHAR)))))
    {
        Text.back() = UNICODE_NULL;
        Description = DcNarrowDescription(Text.data());
    }

    if (!NT_SUCCESS(DcQueryAdapterInfo(Handle, KMTQAITYPE_QUERY_ADAPTER_UNIQUE_GUID, UniqueGuid, sizeof(UniqueGuid))))
        memset(UniqueGuid, 0, sizeof(UniqueGuid));

    /* Each user mode driver generation the adapter has a driver for */
    for (UINT Generation = KMTUMDVERSION_DX9; Generation <= KMTUMDVERSION_DX12; Generation++)
    {
        memset(&Umd, 0, sizeof(Umd));
        Umd.Version = static_cast<KMTUMDVERSION>(Generation);
        if (NT_SUCCESS(DcQueryAdapterInfo(Handle, KMTQAITYPE_UMDRIVERNAME, &Umd, sizeof(Umd))))
            Flags |= DcFlagUmd9 << Generation;
    }

    hr = ReadDriverStrings(L"DXCoreAttributes", &Strings);
    if (FAILED(hr) || Strings.empty())
    {
        Strings.clear();
        hr = ReadDriverStrings(L"DXAttributes", &Strings);
    }

    if (FAILED(hr) || Strings.empty())
    {
        /* No list from the driver, so the user mode drivers decide */
        if ((Flags & (DcFlagRender | DcFlagComputeOnly)) == DcFlagRender)
        {
            if (Flags & DcFlagUmdAny)
                Attributes.push_back(DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS);

            if (Flags & DcFlagUmd12)
            {
                Attributes.push_back(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS);
                Attributes.push_back(DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE);
            }
        }
    }
    else
    {
        for (const std::wstring& Item : Strings)
        {
            GUID Attribute;

            if (FAILED(IIDFromString(Item.c_str(), &Attribute)))
                continue;
            if (DcIsWithheldAttribute(Attribute))
                continue;

            if (DcIsRuntimeAttribute(Attribute))
            {
                Attributes.push_back(Attribute);
                continue;
            }

            for (LONG Kind = 0; Kind < static_cast<LONG>(_countof(DcHardwareTypes)); Kind++)
            {
                if (DcIsEqualGuid(Attribute, *DcHardwareTypes[Kind]))
                {
                    if (KindIndex < 0 || Kind < KindIndex)
                        KindIndex = Kind;
                    break;
                }
            }
        }
    }

    HadD3D11 = DcHasGuid(Attributes, DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS);
    HadD3D12 = DcHasGuid(Attributes, DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS);
    HadCompute = DcHasGuid(Attributes, DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE);

    if ((Flags & DcFlagUmd12) && ((Flags & DcFlagRender) || ((Flags & DcFlagComputeOnly) && HadCompute)) &&
        !DcHasGuid(Attributes, DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML))
    {
        Attributes.push_back(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML);
    }

    if ((Flags & (DcFlagUmd12 | DcFlagRender)) == (DcFlagUmd12 | DcFlagRender) && KmdModelVersion >= 2400 &&
        KmdModelVersion <= 3100 && HadD3D12 && !DcHasGuid(Attributes, DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_MEDIA))
    {
        Attributes.push_back(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_MEDIA);
    }

    if (KindIndex < 0)
    {
        if (HadD3D11 || HadD3D12)
            KindIndex = 0;
        else if (HadCompute)
            KindIndex = 1;
        else
            return;
    }

    Attributes.push_back(*DcHardwareTypes[KindIndex]);
    Traits.Kind = static_cast<ULONG>(KindIndex) + DcKindGpu;
}
