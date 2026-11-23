/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Creating and destroying a device on the translation layer's immediate context
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* The creator ID the translation layer tags its Direct3D 12 work with for 9on12 */
static const GUID NineCreatorId = { 0xfffcbb7f, 0x15d3, 0x42a2, { 0x84, 0x1e, 0x9d, 0x8d, 0x32, 0xf3, 0x7d, 0xdd } };

/* Driver versions from this one on no longer need the YUY2 blit coordinates adjusted */
static constexpr LONG NineQualcommFixedDriver = 0x1F0000;
static constexpr UINT NineQualcommVendor = 0x4D4F4351;

/* Upload buffers start at 1 MB and hand out 256 byte aligned pieces */
static constexpr UINT NineUploadCapacity = 0x100000;
static constexpr UINT NineUploadAlignment = 0x100;
static constexpr UINT NineZeroConstantsSize = 0x1000;

static
UINT
NineSmaller(
    _In_ UINT Left,
    _In_ UINT Right)
{
    return Left < Right ? Left : Right;
}

/* The arguments of a dynamic, CPU written buffer usable as vertex, index and constant data */
static
VOID
NineDescribeUploadBuffer(
    _Out_ TL::ResourceCreationArgs* Args,
    _In_ ID3D12Device* Device,
    _In_ UINT Width,
    _In_ UINT Alignment)
{
    D3D12_RESOURCE_DESC& Desc = Args->m_desc12;
    D3D12_RESOURCE_ALLOCATION_INFO Info;

    Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Alignment = (static_cast<UINT64>(Alignment) + 0xFFFF) & ~static_cast<UINT64>(0xFFFF);
    Desc.Width = Width;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    UINT8 Planes = static_cast<UINT8>(CD3D11FormatHelper::NonOpaquePlaneCount(DXGI_FORMAT_UNKNOWN));
    Args->m_appDesc = TL::AppResourceDesc(1, Planes, Planes, 1, 1, 1, Width, 1, DXGI_FORMAT_UNKNOWN, 1, 0,
                                          TL::RESOURCE_USAGE_DYNAMIC, TL::RESOURCE_CPU_ACCESS_WRITE,
                                          TL::RESOURCE_BIND_VERTEX_BUFFER | TL::RESOURCE_BIND_INDEX_BUFFER |
                                              TL::RESOURCE_BIND_CONSTANT_BUFFER,
                                          D3D12_RESOURCE_DIMENSION_BUFFER);

    Info = Device->GetResourceAllocationInfo(0, 1, &Desc);
    Args->m_heapDesc = {};
    Args->m_heapDesc.SizeInBytes = Info.SizeInBytes;
    Args->m_heapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    Args->m_heapDesc.Properties.CreationNodeMask = 1;
    Args->m_heapDesc.Properties.VisibleNodeMask = 1;
    Args->m_heapDesc.Alignment = Info.Alignment;
}

/* Swaps in a new upload buffer of the ring's capacity and maps all of it */
static
VOID
NineRefillRing(
    _Inout_ NineUploadRing* Ring)
{
    TL::ResourceCreationArgs Args = {};
    TL::MappedSubresource Mapped = {};
    NineDevice* Device = Ring->Owner;

    NineDescribeUploadBuffer(&Args, Device->Device12, Ring->Capacity, Ring->Alignment);
    Ring->Buffer = TL::Resource::CreateResource(&*Device->Context, Args,
                                                TL::ResourceAllocationContext::ImmediateContextThreadLongLived);
    Device->Context->Map(Ring->Buffer.get(), 0, TL::MAP_TYPE_WRITE_DISCARD, false, nullptr, &Mapped);
    Ring->Mapped = static_cast<BYTE*>(Mapped.pData);
    Ring->Cursor = 0;
}

static
NineUploadSlice
NineAllocateUpload(
    _Inout_ NineUploadRing* Ring,
    _In_ UINT Size)
{
    UINT Padded = (Size + Ring->Alignment - 1) & ~(Ring->Alignment - 1);
    NineUploadSlice Slice;

    if (static_cast<UINT64>(Ring->Cursor) + Padded > Ring->Capacity)
    {
        /* A request bigger than the whole buffer grows it to the next power of two */
        if (Size > Ring->Capacity)
        {
            UINT Grown = 1;

            while (Grown < Size)
                Grown <<= 1;
            Ring->Capacity = Grown;
        }
        NineRefillRing(Ring);
    }

    Slice.Buffer = Ring->Buffer.get();
    Slice.Cpu = Ring->Mapped + Ring->Cursor;
    Slice.Offset = Ring->Cursor;
    Ring->Cursor += Padded;
    return Slice;
}

/* The translation layer's creation flags as the reference picks them for this adapter */
static
TL::ImmediateContext::CreationArgs
NineContextArgs(
    _In_ const NineAdapter* Adapter)
{
    const NineSettings& Settings = NineGetSettings();
    TL::ImmediateContext::CreationArgs Args;

    Args.UseResidencyManagement = 1;
    if (!Settings.SingleThread)
    {
        Args.CreatesAndDestroysAreMultithreaded = 1;
        Args.RenamingIsMultithreaded = 1;
    }
    if (Adapter->HardwareId.vendorID == NineQualcommVendor && Adapter->DriverVersion.HighPart < NineQualcommFixedDriver)
        Args.AdjustYUY2BlitCoords = 1;

    Args.CreatorID = NineCreatorId;
    Args.MaxAllocatedUploadHeapSpacePerCommandList =
        NineSmaller(NineAppCompat.MaxUploadPerCommandList, Settings.MaxUploadPerCommandList);
    Args.MaxSRVHeapSize = NineSmaller(NineAppCompat.MaxSrvHeapSize, Settings.MaxSrvHeapSize);
    Args.BufferPoolTrimThreshold = NineSmaller(NineAppCompat.BufferPoolTrimThreshold, Settings.BufferPoolTrimThreshold);
    return Args;
}

/* Every stage starts out reading zeroed constants */
static
VOID
NineInitConstants(
    _Inout_ NineDevice* Device)
{
    Device->ConstantRing.Owner = Device;
    Device->ConstantRing.Capacity = NineUploadCapacity;
    Device->ConstantRing.Alignment = NineUploadAlignment;
    Device->ConstantRing.Cursor = NineUploadCapacity;

    Device->ZeroConstants = NineAllocateUpload(&Device->ConstantRing, NineZeroConstantsSize);
    ZeroMemory(Device->ZeroConstants.Cpu, NineZeroConstantsSize);
    Device->ZeroConstants.Cpu = nullptr;

    for (UINT Kind = 0; Kind < NineConstantKindCount; Kind += 1)
    {
        Device->VertexConstants[Kind] = Device->ZeroConstants;
        Device->PixelConstants[Kind] = Device->ZeroConstants;
    }
}

/* Builds the immediate context over the adapter's device and queue; throws what the translation layer throws */
static
HRESULT
NineInitDevice(
    _Inout_ NineDevice* Device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS Options = {};
    TL::TranslationLayerCallbacks Callbacks;
    TL::SharedResourceHelpers::CreationFlags SharingFlags = {};
    HRESULT hr;

    Device->Device12 = Device->Adapter->Device;
    Device->Device12->AddRef();
    Device->Queue = Device->Adapter->Queue;
    Device->Queue->AddRef();

    hr = Device->Device12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof(Options));
    if (SUCCEEDED(hr))
    {
        Device->Context.emplace(0, Options, Device->Device12, Device->Queue, Callbacks, 0,
                                NineContextArgs(Device->Adapter));
        Device->Sharing.emplace(*Device->Context, SharingFlags);
        Device->Latency.Init(&*Device->Context);

        Device->Context->SetNumViewports(1);
        Device->Context->SetNumScissorRects(1);

        NineInitConstants(Device);
        Device->Adapter->VideoDevice = Device;
    }

    /* Asked even when the context was never built, as the reference does */
    Device->Device12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS19, &Device->Options19, sizeof(Device->Options19));
    return hr;
}

static
VOID
NineFreeDevice(
    _In_ _Post_invalid_ NineDevice* Device)
{
    Device->ConstantRing.Buffer.reset();
    Device->Sharing.reset();
    Device->Context.reset();
    if (Device->Queue != nullptr)
        Device->Queue->Release();
    if (Device->Device12 != nullptr)
        Device->Device12->Release();
    delete Device;
}

HRESULT
APIENTRY
NineCreateDevice(
    _In_ HANDLE hAdapter,
    _Inout_ D3DDDIARG_CREATEDEVICE* Args)
{
    NineDevice* Device;
    HRESULT hr;

    if (hAdapter == NULL || Args->pCallbacks == nullptr)
        return E_INVALIDARG;

    Device = new (std::nothrow) NineDevice();
    if (Device == nullptr)
        return E_OUTOFMEMORY;

    Device->Adapter = static_cast<NineAdapter*>(hAdapter);
    Device->RuntimeDevice = Args->hDevice;
    Device->RuntimeVersion = Args->Version;
    Device->Callbacks = *Args->pCallbacks;

    try
    {
        hr = NineInitDevice(Device);
    }
    catch (_com_error& Error)
    {
        hr = Error.Error();
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }

    if (FAILED(hr))
    {
        NineTrace(L"the device is not created (0x%08lX)", hr);
        if (Device->Adapter->VideoDevice == Device)
            Device->Adapter->VideoDevice = nullptr;
        NineFreeDevice(Device);
        return NineDxgiResult(hr);
    }

    Args->hDevice = Device;
    Args->pAllocationList = nullptr;
    Args->pPatchLocationList = nullptr;
    Args->CommandBuffer = 0;
    NineFillDeviceFuncs(Args->pDeviceFuncs);
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineDestroyDevice(
    _In_ HANDLE hDevice)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr)
        return E_INVALIDARG;

    Device->ConstantRing.Buffer.reset();
    Device->Adapter->VideoDevice = nullptr;
    NineFreeDevice(Device);
    return NineDxgiResult(S_OK);
}
