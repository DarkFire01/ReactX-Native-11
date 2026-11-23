/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDXCoreAdapter1: identity, properties and attributes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

BOOLEAN
DcIsEqualGuid(
    _In_ const GUID& Left,
    _In_ const GUID& Right)
{
    return memcmp(&Left, &Right, sizeof(Left)) == 0;
}

BOOLEAN
DcHasGuid(
    _In_ const std::vector<GUID>& List,
    _In_ const GUID& Item)
{
    for (const GUID& Entry : List)
    {
        if (DcIsEqualGuid(Entry, Item))
            return TRUE;
    }

    return FALSE;
}

DcAdapter::DcAdapter(VOID)
{
}

DcAdapter::~DcAdapter(VOID)
{
    D3DKMT_CLOSEADAPTER Close;

    if (Handle != 0)
    {
        Close.hAdapter = Handle;
        DcKmtCall(DcKmt()->CloseAdapter, &Close);
    }
}

STDMETHODIMP
DcAdapter::QueryInterface(
    _In_ REFIID Riid,
    _COM_Outptr_ void** Object)
{
    *Object = nullptr;
    if (DcIsEqualGuid(Riid, __uuidof(IUnknown)) || DcIsEqualGuid(Riid, __uuidof(IDXCoreAdapter1)) ||
        DcIsEqualGuid(Riid, __uuidof(IDXCoreAdapter)))
    {
        *Object = static_cast<IDXCoreAdapter1*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
DcAdapter::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

STDMETHODIMP_(ULONG)
DcAdapter::Release(VOID)
{
    ULONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        delete this;

    return Count;
}

bool
STDMETHODCALLTYPE
DcAdapter::IsValid(VOID)
{
    return GetLifeState() == DcStateValid;
}

bool
STDMETHODCALLTYPE
DcAdapter::IsAttributeSupported(
    _In_ REFGUID Attribute)
{
    return DcHasGuid(Attributes, Attribute) != FALSE;
}

VOID
DcAdapter::ClassifyProperty(
    _In_ ULONG Property,
    _Out_ BOOLEAN* Known,
    _Out_ BOOLEAN* Present)
{
    *Known = TRUE;
    *Present = TRUE;

    switch (Property)
    {
        case static_cast<ULONG>(DXCoreAdapterProperty::ComputePreemptionGranularity):
            *Present = (Flags & (DcFlagRender | DcFlagComputeOnly)) != 0;
            break;

        case static_cast<ULONG>(DXCoreAdapterProperty::GraphicsPreemptionGranularity):
            *Present = (Flags & DcFlagRender) != 0;
            break;

        case static_cast<ULONG>(DXCoreAdapterProperty::IsIntegrated):
            *Present = (Flags & DcFlagComputeOnly) == 0;
            break;

        case static_cast<ULONG>(DXCoreAdapterProperty::AdapterEngineCount):
        case static_cast<ULONG>(DXCoreAdapterProperty::AdapterEngineName):
            *Known = (FactoryCapabilities & DcFactoryEngineStatistics) != 0;
            *Present = KmdModelVersion >= 2000;
            break;

        case static_cast<ULONG>(DXCoreAdapterProperty::InstanceLuid):
        case static_cast<ULONG>(DXCoreAdapterProperty::DriverVersion):
        case static_cast<ULONG>(DXCoreAdapterProperty::DriverDescription):
        case static_cast<ULONG>(DXCoreAdapterProperty::HardwareID):
        case static_cast<ULONG>(DXCoreAdapterProperty::KmdModelVersion):
        case static_cast<ULONG>(DXCoreAdapterProperty::DedicatedAdapterMemory):
        case static_cast<ULONG>(DXCoreAdapterProperty::DedicatedSystemMemory):
        case static_cast<ULONG>(DXCoreAdapterProperty::SharedSystemMemory):
        case static_cast<ULONG>(DXCoreAdapterProperty::AcgCompatible):
        case static_cast<ULONG>(DXCoreAdapterProperty::IsHardware):
        case static_cast<ULONG>(DXCoreAdapterProperty::IsDetachable):
        case static_cast<ULONG>(DXCoreAdapterProperty::HardwareIDParts):
        case static_cast<ULONG>(DXCoreAdapterProperty::PhysicalAdapterCount):
        case DcPropertyUniqueGuid:
            break;

        default:
            *Known = FALSE;
            *Present = FALSE;
            break;
    }
}

bool
STDMETHODCALLTYPE
DcAdapter::IsPropertySupported(
    _In_ DXCoreAdapterProperty Property)
{
    BOOLEAN Known;
    BOOLEAN Present;

    ClassifyProperty(static_cast<ULONG>(Property), &Known, &Present);
    return Known && Present;
}

/* Copies a fixed size value, failing like the system when the buffer is short */
template <typename Value>
static
HRESULT
DcPutValue(
    _In_ const Value& Source,
    _In_ size_t BufferSize,
    _Out_writes_bytes_(BufferSize) void* Buffer)
{
    if (BufferSize < sizeof(Source))
        return E_INVALIDARG;

    memcpy(Buffer, &Source, sizeof(Source));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DcAdapter::GetProperty(
    _In_ DXCoreAdapterProperty Property,
    _In_ size_t BufferSize,
    _Out_writes_bytes_(BufferSize) void* Buffer)
{
    ULONG Which = static_cast<ULONG>(Property);
    BOOLEAN Known;
    BOOLEAN Present;

    if (Buffer == nullptr)
        return E_POINTER;

    memset(Buffer, 0, BufferSize);
    ClassifyProperty(Which, &Known, &Present);
    if (!Known)
        return DXGI_ERROR_INVALID_CALL;
    if (!Present)
        return DXGI_ERROR_UNSUPPORTED;

    switch (Which)
    {
        case static_cast<ULONG>(DXCoreAdapterProperty::InstanceLuid):
            return DcPutValue(Luid, BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::DriverVersion):
            return DcPutValue(DriverVersion, BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::DriverDescription):
        {
            size_t Needed = Description.size() + 1;

            if (BufferSize < Needed)
                return E_INVALIDARG;
            if (Needed > 1)
                memcpy_s(Buffer, BufferSize, Description.c_str(), Needed);
            return S_OK;
        }

        case static_cast<ULONG>(DXCoreAdapterProperty::HardwareID):
        {
            DXCoreHardwareID Id;

            Id.vendorID = HardwareId.vendorID;
            Id.deviceID = HardwareId.deviceID;
            Id.subSysID = HardwareId.subSystemID;
            if ((Flags & DcFlagPlainSubSystem) == 0)
                Id.subSysID = HardwareId.subVendorID | (HardwareId.subSystemID << 16);
            Id.revision = HardwareId.revisionID;
            return DcPutValue(Id, BufferSize, Buffer);
        }

        case static_cast<ULONG>(DXCoreAdapterProperty::KmdModelVersion):
            return DcPutValue(KmdModelVersion, BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::ComputePreemptionGranularity):
            return DcPutValue(static_cast<UINT16>(ComputePreemption), BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::GraphicsPreemptionGranularity):
            return DcPutValue(static_cast<UINT16>(GraphicsPreemption), BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::DedicatedAdapterMemory):
            return DcPutValue(DedicatedAdapterMemory, BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::DedicatedSystemMemory):
            return DcPutValue(DedicatedSystemMemory, BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::SharedSystemMemory):
            return DcPutValue(SharedSystemMemory, BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::AcgCompatible):
            return DcPutValue(static_cast<BYTE>(Traits.AcgCompatible), BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::IsHardware):
            return DcPutValue(static_cast<BYTE>(Traits.IsHardware), BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::IsIntegrated):
            return DcPutValue(static_cast<BYTE>(Flags & DcFlagIntegrated), BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::IsDetachable):
            return DcPutValue(static_cast<BYTE>((Flags & DcFlagDetachable) != 0), BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::HardwareIDParts):
            return DcPutValue(HardwareId, BufferSize, Buffer);

        case static_cast<ULONG>(DXCoreAdapterProperty::PhysicalAdapterCount):
            return DcPutValue(static_cast<UINT>(Traits.Physical.size()), BufferSize, Buffer);

        case DcPropertyUniqueGuid:
        {
            if (BufferSize < DcUniqueGuidSize)
                return E_INVALIDARG;

            /* Builds without the copy fix move only the first half */
            memcpy_s(Buffer, BufferSize, UniqueGuid,
                     DcIsSystemFeatureEnabled(DcFeatureUniqueGuidCopyFix) ? DcUniqueGuidSize : DcUniqueGuidSize / 2);
            return S_OK;
        }

        default:
            return E_INVALIDARG;
    }
}

HRESULT
STDMETHODCALLTYPE
DcAdapter::GetPropertySize(
    _In_ DXCoreAdapterProperty Property,
    _Out_ size_t* BufferSize)
{
    ULONG Which = static_cast<ULONG>(Property);
    BOOLEAN Known;
    BOOLEAN Present;

    if (BufferSize == nullptr)
        return E_POINTER;

    *BufferSize = 0;
    ClassifyProperty(Which, &Known, &Present);
    if (!Known)
        return DXGI_ERROR_INVALID_CALL;
    if (!Present)
        return DXGI_ERROR_UNSUPPORTED;

    switch (Which)
    {
        case static_cast<ULONG>(DXCoreAdapterProperty::InstanceLuid):
        case static_cast<ULONG>(DXCoreAdapterProperty::DriverVersion):
        case static_cast<ULONG>(DXCoreAdapterProperty::DedicatedAdapterMemory):
        case static_cast<ULONG>(DXCoreAdapterProperty::DedicatedSystemMemory):
        case static_cast<ULONG>(DXCoreAdapterProperty::SharedSystemMemory):
            *BufferSize = 8;
            break;

        case static_cast<ULONG>(DXCoreAdapterProperty::DriverDescription):
            *BufferSize = Description.size() + 1;
            break;

        case static_cast<ULONG>(DXCoreAdapterProperty::HardwareID):
            *BufferSize = sizeof(DXCoreHardwareID);
            break;

        case static_cast<ULONG>(DXCoreAdapterProperty::KmdModelVersion):
        case static_cast<ULONG>(DXCoreAdapterProperty::ComputePreemptionGranularity):
        case static_cast<ULONG>(DXCoreAdapterProperty::GraphicsPreemptionGranularity):
        case static_cast<ULONG>(DXCoreAdapterProperty::PhysicalAdapterCount):
        case static_cast<ULONG>(DXCoreAdapterProperty::AdapterEngineCount):
        case static_cast<ULONG>(DXCoreAdapterProperty::AdapterEngineName):
            *BufferSize = 4;
            break;

        case static_cast<ULONG>(DXCoreAdapterProperty::AcgCompatible):
        case static_cast<ULONG>(DXCoreAdapterProperty::IsHardware):
        case static_cast<ULONG>(DXCoreAdapterProperty::IsIntegrated):
        case static_cast<ULONG>(DXCoreAdapterProperty::IsDetachable):
            *BufferSize = 1;
            break;

        case static_cast<ULONG>(DXCoreAdapterProperty::HardwareIDParts):
            *BufferSize = sizeof(DXCoreHardwareIDParts);
            break;

        case DcPropertyUniqueGuid:
            *BufferSize = DcUniqueGuidSize;
            break;

        default:
            return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DcAdapter::GetPropertyWithInput(
    _In_ DXCoreAdapterProperty Property,
    _In_ size_t InputSize,
    _In_reads_bytes_opt_(InputSize) const void* Input,
    _In_ size_t OutputSize,
    _Out_writes_bytes_(OutputSize) void* Output)
{
    ULONG Which = static_cast<ULONG>(Property);
    BOOLEAN Known;
    BOOLEAN Present;

    if (Output == nullptr)
        return E_POINTER;

    memset(Output, 0, OutputSize);
    ClassifyProperty(Which, &Known, &Present);
    if (!Known)
        return DXGI_ERROR_INVALID_CALL;
    if (!Present)
        return DXGI_ERROR_UNSUPPORTED;

    if (Which <= static_cast<ULONG>(DXCoreAdapterProperty::PhysicalAdapterCount))
    {
        if (InputSize != 0)
            return E_INVALIDARG;

        return GetProperty(Property, OutputSize, Output);
    }

    if (Which == static_cast<ULONG>(DXCoreAdapterProperty::AdapterEngineCount))
    {
        const DXCoreAdapterEngineIndex* Engine = static_cast<const DXCoreAdapterEngineIndex*>(Input);

        if (InputSize < 4 || OutputSize < 4)
            return E_INVALIDARG;
        if (Engine->physicalAdapterIndex >= Traits.Physical.size())
            return E_INVALIDARG;

        *static_cast<UINT*>(Output) = Traits.Physical[Engine->physicalAdapterIndex].EngineCount;
        return S_OK;
    }

    if (Which == static_cast<ULONG>(DXCoreAdapterProperty::AdapterEngineName))
    {
        const DXCoreEngineNamePropertyInput* Request = static_cast<const DXCoreEngineNamePropertyInput*>(Input);
        const DcPhysicalAdapter* Physical;
        PCWSTR Name;

        if (InputSize < sizeof(*Request) || OutputSize < 4)
            return E_INVALIDARG;
        if (Request->adapterEngineIndex.physicalAdapterIndex >= Traits.Physical.size())
            return E_INVALIDARG;

        Physical = &Traits.Physical[Request->adapterEngineIndex.physicalAdapterIndex];
        if (Request->adapterEngineIndex.engineIndex >= Physical->Engines.size())
            return E_INVALIDARG;

        Name = Physical->Engines[Request->adapterEngineIndex.engineIndex].Name;
        static_cast<DXCoreEngineNamePropertyOutput*>(Output)->engineNameLength =
            static_cast<uint32_t>(wcslen(Name) + 1);
        wcscpy_s(Request->engineName, Request->engineNameLength, Name);
        return S_OK;
    }

    return E_INVALIDARG;
}

HRESULT
STDMETHODCALLTYPE
DcAdapter::GetFactory(
    _In_ REFIID Riid,
    _COM_Outptr_ void** Factory)
{
    HRESULT hr = E_UNEXPECTED;

    if (Factory == nullptr)
        return E_POINTER;

    *Factory = nullptr;
    EnterCriticalSection(&DcGlobalLock);
    if (DcGlobalFactory != nullptr)
        hr = DcGlobalFactory->QueryInterface(Riid, Factory);
    LeaveCriticalSection(&DcGlobalLock);
    return hr;
}
