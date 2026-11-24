/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 runtime behavior, printed as comparable facts
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * d3d12check [-runtime system|reactx|<dir>] [check ...]
 */

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "internalshaders.h"

using Microsoft::WRL::ComPtr;

struct TwelveCheck
{
    HMODULE D3d12 = NULL;
    PFN_D3D12_CREATE_DEVICE CreateDevice = nullptr;
    PFN_D3D12_GET_DEBUG_INTERFACE GetDebugInterface = nullptr;
    PFN_D3D12_GET_INTERFACE GetInterface = nullptr;
    PFN_D3D12_SERIALIZE_ROOT_SIGNATURE SerializeRootSignature = nullptr;
    PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER CreateDeserializer = nullptr;
    PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE SerializeVersioned = nullptr;
    PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER CreateVersionedDeserializer = nullptr;
    PCSTR CheckName = "";

    VOID
    Report(
        _In_z_ PCSTR Key,
        _In_z_ _Printf_format_string_ PCSTR Format,
        ...)
    {
        va_list Arguments;

        printf("%s.%s = ", CheckName, Key);
        va_start(Arguments, Format);
        vprintf(Format, Arguments);
        va_end(Arguments);
        printf("\n");
    }

    HRESULT
    MakeDevice(
        _Outptr_ ID3D12Device** Device)
    {
        return CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(Device));
    }
};

/* create: argument checks of D3D12CreateDevice and what one device per adapter means */
static
VOID
TcCheckCreate(
    _In_ TwelveCheck& Check)
{
    ComPtr<ID3D12Device> First;
    ComPtr<ID3D12Device> Second;
    ComPtr<IUnknown> Unknown;
    void* Raw = nullptr;
    HRESULT hr;

    Check.Report("null_out", "0x%08lX", Check.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                                            nullptr));
    Check.Report("level_9_3", "0x%08lX", Check.CreateDevice(nullptr, D3D_FEATURE_LEVEL_9_3, __uuidof(ID3D12Device),
                                                             nullptr));
    Check.Report("level_10_1", "0x%08lX", Check.CreateDevice(nullptr, D3D_FEATURE_LEVEL_10_1,
                                                              __uuidof(ID3D12Device), nullptr));
    Check.Report("level_bogus", "0x%08lX", Check.CreateDevice(nullptr, (D3D_FEATURE_LEVEL)0xc123,
                                                               __uuidof(ID3D12Device), nullptr));
    Check.Report("level_12_0_null", "0x%08lX", Check.CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                                                                   __uuidof(ID3D12Device), nullptr));

    hr = Check.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(IDXGIFactory), &Raw);
    Check.Report("wrong_iid", "0x%08lX %d", hr, Raw == nullptr);

    hr = Check.MakeDevice(&First);
    Check.Report("device", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    hr = Check.CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&Second));
    Check.Report("second", "0x%08lX same %d", hr, Second.Get() == First.Get());

    hr = Check.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Unknown));
    Check.Report("as_unknown", "0x%08lX", hr);
}

/* lifetime: reference counts and what of a device outlives its last reference */
static
VOID
TcCheckLifetime(
    _In_ TwelveCheck& Check)
{
    static const GUID Tag = { 0x6b1f0c3a, 0x55d1, 0x4c0e, { 0x9a, 0x1b, 0x2d, 0x7c, 0x44, 0x10, 0x8e, 0x21 } };
    ID3D12Device* Device = nullptr;
    ID3D12Device* Again = nullptr;
    ID3D12Device* Parent = nullptr;
    ID3D12Fence* Fence = nullptr;
    UINT Value = 0x1234;
    UINT Size;
    HRESULT hr;

    hr = Check.MakeDevice(&Device);
    Check.Report("create", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Check.Report("addref", "%lu", Device->AddRef());
    Check.Report("release", "%lu", Device->Release());

    hr = Check.MakeDevice(&Again);
    Check.Report("second", "0x%08lX same %d", hr, Again == Device);
    if (SUCCEEDED(hr))
        Check.Report("second_release", "%lu", Again->Release());

    hr = Device->SetPrivateData(Tag, sizeof(Value), &Value);
    Check.Report("set_tag", "0x%08lX", hr);
    Check.Report("last_release", "%lu", Device->Release());

    /* A new device shows whether the old one survived its last reference */
    hr = Check.MakeDevice(&Device);
    Check.Report("recreate", "0x%08lX", hr);
    if (FAILED(hr))
        return;
    Value = 0;
    Size = sizeof(Value);
    hr = Device->GetPrivateData(Tag, &Size, &Value);
    Check.Report("recreate_tag", "0x%08lX %u 0x%x", hr, Size, Value);

    /* A child holds the device */
    hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
    Check.Report("fence", "0x%08lX", hr);
    if (FAILED(hr))
    {
        Device->Release();
        return;
    }
    Check.Report("release_with_child", "%lu", Device->Release());

    hr = Fence->GetDevice(IID_PPV_ARGS(&Parent));
    Check.Report("child_device", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        hr = Check.MakeDevice(&Again);
        Check.Report("create_with_child", "0x%08lX same %d", hr, Again == Parent);
        if (SUCCEEDED(hr))
            Check.Report("create_with_child_release", "%lu", Again->Release());
        Check.Report("child_device_release", "%lu", Parent->Release());
    }
    Check.Report("fence_release", "%lu", Fence->Release());

    hr = Check.MakeDevice(&Device);
    Check.Report("after_child", "0x%08lX", hr);
    if (SUCCEEDED(hr))
        Check.Report("after_child_release", "%lu", Device->Release());
}

/* device: fixed answers every application reads right after creation */
static
VOID
TcCheckDevice(
    _In_ TwelveCheck& Check)
{
    static const struct
    {
        PCSTR Name;
        IID Iid;
    } Interfaces[] =
    {
        { "Device1", __uuidof(ID3D12Device1) }, { "Device2", __uuidof(ID3D12Device2) },
        { "Device3", __uuidof(ID3D12Device3) }, { "Device4", __uuidof(ID3D12Device4) },
        { "Device5", __uuidof(ID3D12Device5) }, { "Device6", __uuidof(ID3D12Device6) },
        { "Device7", __uuidof(ID3D12Device7) }, { "Device8", __uuidof(ID3D12Device8) },
        { "Device9", __uuidof(ID3D12Device9) }, { "Device10", __uuidof(ID3D12Device10) },
        { "Device11", __uuidof(ID3D12Device11) }, { "Device12", __uuidof(ID3D12Device12) },
        { "Device13", __uuidof(ID3D12Device13) }, { "Device14", __uuidof(ID3D12Device14) },
        { "DXGIDevice", __uuidof(IDXGIDevice) }, { "InfoQueue", __uuidof(ID3D12InfoQueue) },
        { "DebugDevice", __uuidof(ID3D12DebugDevice) },
    };
    ComPtr<ID3D12Device> Device;
    ComPtr<IDXGIFactory4> Factory;
    ComPtr<IDXGIAdapter1> Adapter;
    DXGI_ADAPTER_DESC1 AdapterDesc = {};
    decltype(&CreateDXGIFactory1) CreateFactory;
    LUID Luid;
    UINT Type;
    char Key[64];

    if (FAILED(Check.MakeDevice(&Device)))
        return;

    Check.Report("nodes", "%u", Device->GetNodeCount());
    for (Type = 0; Type < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; Type++)
    {
        sprintf_s(Key, "increment_%u", Type);
        Check.Report(Key, "%u", Device->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)Type));
    }

    Luid = Device->GetAdapterLuid();
    CreateFactory = reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(LoadLibraryW(L"dxgi.dll"),
                                                                                  "CreateDXGIFactory1"));
    if (CreateFactory != nullptr && SUCCEEDED(CreateFactory(IID_PPV_ARGS(&Factory))) &&
        SUCCEEDED(Factory->EnumAdapters1(0, &Adapter)))
    {
        Adapter->GetDesc1(&AdapterDesc);
    }
    Check.Report("luid_is_adapter0", "%d", memcmp(&Luid, &AdapterDesc.AdapterLuid, sizeof(Luid)) == 0);
    Check.Report("removed", "0x%08lX", Device->GetDeviceRemovedReason());

    for (const auto& Each : Interfaces)
    {
        ComPtr<IUnknown> Face;

        sprintf_s(Key, "qi_%s", Each.Name);
        Check.Report(Key, "0x%08lX", Device->QueryInterface(Each.Iid, &Face));
    }
}

/* features: CheckFeatureSupport answers that do not depend on which GPU this is */
static
VOID
TcCheckFeatures(
    _In_ TwelveCheck& Check)
{
    static const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
                                                D3D_FEATURE_LEVEL_12_1 };
    static const DXGI_FORMAT Formats[] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
                                           DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_BC1_UNORM,
                                           DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_NV12, DXGI_FORMAT_R32G32B32_FLOAT,
                                           DXGI_FORMAT_UNKNOWN };
    ComPtr<ID3D12Device> Device;
    D3D12_FEATURE_DATA_FEATURE_LEVELS FeatureLevels = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS Options = {};
    D3D12_FEATURE_DATA_ARCHITECTURE Architecture = {};
    D3D12_FEATURE_DATA_ROOT_SIGNATURE RootSignature = {};
    D3D12_FEATURE_DATA_SHADER_MODEL ShaderModel = {};
    D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT Address = {};
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS Quality = {};
    char Key[64];
    HRESULT hr;

    if (FAILED(Check.MakeDevice(&Device)))
        return;

    FeatureLevels.NumFeatureLevels = _countof(Levels);
    FeatureLevels.pFeatureLevelsRequested = Levels;
    hr = Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &FeatureLevels, sizeof(FeatureLevels));
    Check.Report("levels", "0x%08lX 0x%x", hr, FeatureLevels.MaxSupportedFeatureLevel);
    Check.Report("levels_size", "0x%08lX", Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &FeatureLevels,
                                                                       sizeof(FeatureLevels) - 1));
    FeatureLevels.NumFeatureLevels = 0;
    Check.Report("levels_zero", "0x%08lX", Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &FeatureLevels,
                                                                       sizeof(FeatureLevels)));

    hr = Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof(Options));
    Check.Report("options", "0x%08lX binding %d tiled %d conservative %d heap %d rov %d typed_uav %d", hr,
                 Options.ResourceBindingTier, Options.TiledResourcesTier, Options.ConservativeRasterizationTier,
                 Options.ResourceHeapTier, Options.ROVsSupported, Options.TypedUAVLoadAdditionalFormats);

    hr = Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &Architecture, sizeof(Architecture));
    Check.Report("architecture", "0x%08lX uma %d", hr, Architecture.UMA);
    Architecture.NodeIndex = 1;
    Check.Report("architecture_node1", "0x%08lX", Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE,
                                                                              &Architecture, sizeof(Architecture)));

    RootSignature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_2;
    hr = Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &RootSignature, sizeof(RootSignature));
    Check.Report("root_signature", "0x%08lX 0x%x", hr, RootSignature.HighestVersion);

    ShaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_0;
    hr = Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &ShaderModel, sizeof(ShaderModel));
    Check.Report("shader_model_6_0", "0x%08lX 0x%x", hr, ShaderModel.HighestShaderModel);
    ShaderModel.HighestShaderModel = (D3D_SHADER_MODEL)0x99;
    hr = Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &ShaderModel, sizeof(ShaderModel));
    Check.Report("shader_model_bogus", "0x%08lX", hr);

    hr = Device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &Address, sizeof(Address));
    Check.Report("va", "0x%08lX %u %u", hr, Address.MaxGPUVirtualAddressBitsPerResource,
                 Address.MaxGPUVirtualAddressBitsPerProcess);

    for (DXGI_FORMAT Format : Formats)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT Support = {};
        D3D12_FEATURE_DATA_FORMAT_INFO Info = {};

        Support.Format = Format;
        hr = Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support));
        sprintf_s(Key, "format_%u", Format);
        Check.Report(Key, "0x%08lX 0x%08x 0x%08x", hr, Support.Support1, Support.Support2);

        Info.Format = Format;
        hr = Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &Info, sizeof(Info));
        sprintf_s(Key, "planes_%u", Format);
        Check.Report(Key, "0x%08lX %u", hr, Info.PlaneCount);
    }

    Quality.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Quality.SampleCount = 4;
    hr = Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &Quality, sizeof(Quality));
    Check.Report("msaa4", "0x%08lX levels>0 %d", hr, Quality.NumQualityLevels > 0);
    Quality.SampleCount = 3;
    hr = Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &Quality, sizeof(Quality));
    Check.Report("msaa3", "0x%08lX %u", hr, Quality.NumQualityLevels);

    Check.Report("bogus_feature", "0x%08lX", Device->CheckFeatureSupport((D3D12_FEATURE)0x7777, &Options,
                                                                         sizeof(Options)));
}

/* fence: CPU side signaling and waiting */
static
VOID
TcCheckFence(
    _In_ TwelveCheck& Check)
{
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12Fence> Fence;
    ComPtr<ID3D12Fence1> Fence1;
    HANDLE Event;
    HRESULT hr;

    if (FAILED(Check.MakeDevice(&Device)))
        return;

    hr = Device->CreateFence(5, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
    Check.Report("create", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Check.Report("initial", "%llu", Fence->GetCompletedValue());
    Check.Report("signal", "0x%08lX %llu", Fence->Signal(10), Fence->GetCompletedValue());
    Check.Report("signal_lower", "0x%08lX %llu", Fence->Signal(7), Fence->GetCompletedValue());
    Check.Report("wait_done_null_event", "0x%08lX", Fence->SetEventOnCompletion(3, nullptr));

    Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hr = Fence->SetEventOnCompletion(20, Event);
    Check.Report("wait_pending", "0x%08lX %lu", hr, WaitForSingleObject(Event, 0));
    Fence->Signal(20);
    Check.Report("wait_signaled", "%lu", WaitForSingleObject(Event, 1000));
    CloseHandle(Event);

    hr = Fence.As(&Fence1);
    Check.Report("fence1", "0x%08lX flags %d", hr, SUCCEEDED(hr) ? Fence1->GetCreationFlags() : -1);
    Check.Report("bad_flags", "0x%08lX", Device->CreateFence(0, (D3D12_FENCE_FLAGS)0x80, IID_PPV_ARGS(&Fence)));
}

/* resources: committed buffers and textures, their sizes and footprints */
static
VOID
TcCheckResources(
    _In_ TwelveCheck& Check)
{
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12Resource> Buffer;
    ComPtr<ID3D12Resource> Texture;
    D3D12_HEAP_PROPERTIES Upload = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_HEAP_PROPERTIES Default = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_HEAP_PROPERTIES Properties;
    D3D12_HEAP_FLAGS Flags;
    D3D12_RESOURCE_DESC Desc = {};
    D3D12_RESOURCE_DESC Got;
    D3D12_RESOURCE_ALLOCATION_INFO Info;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layouts[3];
    UINT Rows[3];
    UINT64 RowBytes[3];
    UINT64 Total;
    BYTE* Data;
    HRESULT hr;
    UINT Index;
    char Key[64];

    if (FAILED(Check.MakeDevice(&Device)))
        return;

    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = 1000;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Info = Device->GetResourceAllocationInfo(0, 1, &Desc);
    Check.Report("buffer_info", "%llu %llu", Info.SizeInBytes, Info.Alignment);

    hr = Device->CreateCommittedResource(&Upload, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                         nullptr, IID_PPV_ARGS(&Buffer));
    Check.Report("buffer", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Got = Buffer->GetDesc();
        Check.Report("buffer_desc", "%u %llu %u %u align %llu flags 0x%x", Got.Dimension, Got.Width, Got.Height,
                     Got.MipLevels, Got.Alignment, Got.Flags);
        Check.Report("buffer_va", "%d", Buffer->GetGPUVirtualAddress() != 0 &&
                                         (Buffer->GetGPUVirtualAddress() & 0xFFFF) == 0);
        hr = Buffer->GetHeapProperties(&Properties, &Flags);
        Check.Report("buffer_heap", "0x%08lX type %d page %d pool %d flags 0x%x", hr, Properties.Type,
                     Properties.CPUPageProperty, Properties.MemoryPoolPreference, Flags);

        hr = Buffer->Map(0, nullptr, (void**)&Data);
        Check.Report("map", "0x%08lX", hr);
        if (SUCCEEDED(hr))
        {
            Data[999] = 0x5A;
            Buffer->Unmap(0, nullptr);
            hr = Buffer->Map(0, nullptr, (void**)&Data);
            Check.Report("map_again", "0x%08lX 0x%02x", hr, SUCCEEDED(hr) ? Data[999] : 0);
            Buffer->Unmap(0, nullptr);
        }
        Check.Report("map_sub1", "0x%08lX", Buffer->Map(1, nullptr, (void**)&Data));
    }

    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr = Device->CreateCommittedResource(&Upload, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                         nullptr, IID_PPV_ARGS(&Texture));
    Check.Report("buffer_rt", "0x%08lX", hr);
    Desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = Device->CreateCommittedResource(&Upload, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COMMON,
                                         nullptr, IID_PPV_ARGS(&Texture));
    Check.Report("upload_common", "0x%08lX", hr);

    Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 100;
    Desc.Height = 60;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 0;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    Info = Device->GetResourceAllocationInfo(0, 1, &Desc);
    Check.Report("texture_info", "%llu %llu", Info.SizeInBytes, Info.Alignment);

    hr = Device->CreateCommittedResource(&Default, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                         nullptr, IID_PPV_ARGS(&Texture));
    Check.Report("texture", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Got = Texture->GetDesc();
        Check.Report("texture_desc", "%u %llux%u mips %u align %llu layout %d", Got.Dimension, Got.Width, Got.Height,
                     Got.MipLevels, Got.Alignment, Got.Layout);
        Check.Report("texture_va", "%llu", Texture->GetGPUVirtualAddress());
        Check.Report("texture_map", "0x%08lX", Texture->Map(0, nullptr, (void**)&Data));
    }

    Device->GetCopyableFootprints(&Desc, 0, 3, 0, Layouts, Rows, RowBytes, &Total);
    for (Index = 0; Index < 3; Index++)
    {
        sprintf_s(Key, "footprint_%u", Index);
        Check.Report(Key, "%llu %ux%u pitch %u rows %u bytes %llu", Layouts[Index].Offset,
                     Layouts[Index].Footprint.Width, Layouts[Index].Footprint.Height, Layouts[Index].Footprint.RowPitch,
                     Rows[Index], RowBytes[Index]);
    }
    Check.Report("footprint_total", "%llu", Total);

    Desc.Format = DXGI_FORMAT_BC1_UNORM;
    Desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    Device->GetCopyableFootprints(&Desc, 0, 1, 0, Layouts, Rows, RowBytes, &Total);
    Check.Report("footprint_bc1", "%ux%u pitch %u rows %u bytes %llu total %llu", Layouts[0].Footprint.Width,
                 Layouts[0].Footprint.Height, Layouts[0].Footprint.RowPitch, Rows[0], RowBytes[0], Total);

    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.Width = 0;
    hr = Device->CreateCommittedResource(&Default, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COMMON,
                                         nullptr, IID_PPV_ARGS(&Texture));
    Check.Report("texture_zero", "0x%08lX", hr);
    Info = Device->GetResourceAllocationInfo(0, 1, &Desc);
    Check.Report("texture_zero_info", "%llu %llu", Info.SizeInBytes, Info.Alignment);
}

/* clear: record a clear and a copy, run it, and read the pixel back */
static
VOID
TcCheckClear(
    _In_ TwelveCheck& Check)
{
    static const FLOAT Color[4] = { 1.0f, 0.5f, 0.25f, 1.0f };
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> List;
    ComPtr<ID3D12DescriptorHeap> Heap;
    ComPtr<ID3D12Resource> Target;
    ComPtr<ID3D12Resource> Readback;
    ComPtr<ID3D12Fence> Fence;
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
    D3D12_HEAP_PROPERTIES Default = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_HEAP_PROPERTIES Read = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC Desc = {};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
    D3D12_TEXTURE_COPY_LOCATION Destination = {};
    D3D12_TEXTURE_COPY_LOCATION Source = {};
    D3D12_RESOURCE_BARRIER Barrier = {};
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv;
    ID3D12CommandList* Lists[1];
    UINT64 Total;
    const UINT* Pixels;
    HANDLE Event;
    HRESULT hr;

    if (FAILED(Check.MakeDevice(&Device)))
        return;

    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue));
    Check.Report("queue", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    {
        D3D12_COMMAND_QUEUE_DESC Got = Queue->GetDesc();
        UINT64 Frequency = 0;

        Check.Report("queue_desc", "%d %d 0x%x %u", Got.Type, Got.Priority, Got.Flags, Got.NodeMask);
        hr = Queue->GetTimestampFrequency(&Frequency);
        Check.Report("frequency", "0x%08lX %d", hr, Frequency != 0);
    }

    hr = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Allocator));
    Check.Report("allocator", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    hr = Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS(&List));
    Check.Report("list", "0x%08lX type %d", hr, SUCCEEDED(hr) ? List->GetType() : -1);
    if (FAILED(hr))
        return;

    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 4;
    hr = Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Heap));
    Check.Report("rtv_heap", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr = Device->CreateCommittedResource(&Default, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                         nullptr, IID_PPV_ARGS(&Target));
    Check.Report("target", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Device->GetCopyableFootprints(&Desc, 0, 1, 0, &Footprint, nullptr, nullptr, &Total);

    Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Total;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = Device->CreateCommittedResource(&Read, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         IID_PPV_ARGS(&Readback));
    Check.Report("readback", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Rtv = Heap->GetCPUDescriptorHandleForHeapStart();
    Rtv.ptr += Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    Device->CreateRenderTargetView(Target.Get(), nullptr, Rtv);
    List->ClearRenderTargetView(Rtv, Color, 0, nullptr);

    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Target.Get();
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    List->ResourceBarrier(1, &Barrier);

    Destination.pResource = Readback.Get();
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Destination.PlacedFootprint = Footprint;
    Source.pResource = Target.Get();
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    Source.SubresourceIndex = 0;
    List->CopyTextureRegion(&Destination, 0, 0, 0, &Source, nullptr);

    Check.Report("close", "0x%08lX", List->Close());
    Check.Report("close_again", "0x%08lX", List->Close());

    hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
    Check.Report("fence", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Lists[0] = List.Get();
    Queue->ExecuteCommandLists(1, Lists);
    Check.Report("signal", "0x%08lX", Queue->Signal(Fence.Get(), 1));

    Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Fence->SetEventOnCompletion(1, Event);
    Check.Report("wait", "%lu", WaitForSingleObject(Event, 5000));
    CloseHandle(Event);
    Check.Report("completed", "%llu", Fence->GetCompletedValue());

    hr = Readback->Map(0, nullptr, (void**)&Pixels);
    if (SUCCEEDED(hr))
    {
        Check.Report("pixel", "0x%08x 0x%08x", Pixels[0], Pixels[Footprint.Footprint.RowPitch / 4 * 63 + 63]);
        Readback->Unmap(0, nullptr);
    }

    Check.Report("reset_allocator", "0x%08lX", Allocator->Reset());
    Check.Report("reset_list", "0x%08lX", List->Reset(Allocator.Get(), nullptr));
    Check.Report("close_empty", "0x%08lX", List->Close());
    Check.Report("removed", "0x%08lX", Device->GetDeviceRemovedReason());
}

static
UINT
TcBlobHash(
    _In_ ID3DBlob* Blob)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Blob->GetBufferPointer());
    UINT Hash = 2166136261u;
    SIZE_T Index;

    for (Index = 0; Index < Blob->GetBufferSize(); Index++)
        Hash = (Hash ^ Bytes[Index]) * 16777619u;
    return Hash;
}

/* Every byte, so a layout difference shows where it is */
static
VOID
TcReportBytes(
    _In_ TwelveCheck& Check,
    _In_z_ PCSTR Key,
    _In_ ID3DBlob* Blob)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Blob->GetBufferPointer());
    std::string Text;
    char Hex[4];
    SIZE_T Index;

    for (Index = 0; Index < Blob->GetBufferSize(); Index++)
    {
        sprintf_s(Hex, "%02x", Bytes[Index]);
        Text += Hex;
        if (Index % 4 == 3)
            Text += ' ';
    }
    Check.Report(Key, "%s", Text.c_str());
}

/* helpers: the loose exports beside device creation */
static
VOID
TcCheckHelpers(
    _In_ TwelveCheck& Check)
{
    D3D12_ROOT_PARAMETER Parameters[2] = {};
    D3D12_DESCRIPTOR_RANGE Range = {};
    D3D12_STATIC_SAMPLER_DESC Sampler = {};
    D3D12_ROOT_SIGNATURE_DESC Desc = {};
    ComPtr<ID3DBlob> Blob;
    ComPtr<ID3DBlob> Errors;
    ComPtr<IUnknown> Unknown;
    ComPtr<ID3D12RootSignatureDeserializer> Deserializer;
    const BYTE* Bytes;
    UINT Hash = 2166136261u;
    SIZE_T Index;
    HRESULT hr;

    Check.Report("debug", "0x%08lX", Check.GetDebugInterface(__uuidof(ID3D12Debug), &Unknown));
    Check.Report("dred", "0x%08lX", Check.GetDebugInterface(__uuidof(ID3D12DeviceRemovedExtendedDataSettings),
                                                             &Unknown));

    Range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    Range.NumDescriptors = 2;
    Range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    Parameters[0].DescriptorTable.pDescriptorRanges = &Range;
    Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    Parameters[1].Constants.Num32BitValues = 4;
    Parameters[1].Constants.ShaderRegister = 1;
    Sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    Sampler.MaxLOD = D3D12_FLOAT32_MAX;
    Sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    Desc.NumParameters = 2;
    Desc.pParameters = Parameters;
    Desc.NumStaticSamplers = 1;
    Desc.pStaticSamplers = &Sampler;
    Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    hr = Check.SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Errors);
    Check.Report("serialize", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Bytes = static_cast<const BYTE*>(Blob->GetBufferPointer());
        for (Index = 0; Index < Blob->GetBufferSize(); Index++)
            Hash = (Hash ^ Bytes[Index]) * 16777619u;
        Check.Report("serialized", "%zu 0x%08x", Blob->GetBufferSize(), Hash);
        TcReportBytes(Check, "serialized_bytes", Blob.Get());

        hr = Check.CreateDeserializer(Blob->GetBufferPointer(), Blob->GetBufferSize(), IID_PPV_ARGS(&Deserializer));
        Check.Report("deserialize", "0x%08lX", hr);
        if (SUCCEEDED(hr))
        {
            const D3D12_ROOT_SIGNATURE_DESC* Back = Deserializer->GetRootSignatureDesc();

            Check.Report("deserialized", "%u %u 0x%x %u", Back->NumParameters, Back->NumStaticSamplers, Back->Flags,
                         Back->pParameters[1].Constants.Num32BitValues);
        }
    }

    {
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC Versioned = {};
        D3D12_ROOT_PARAMETER1 Parameters1[3] = {};
        D3D12_DESCRIPTOR_RANGE1 Range1 = {};
        ComPtr<ID3D12VersionedRootSignatureDeserializer> Versions;
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* Back = nullptr;

        Range1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        Range1.NumDescriptors = 3;
        Range1.BaseShaderRegister = 2;
        Range1.RegisterSpace = 1;
        Range1.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
        Range1.OffsetInDescriptorsFromTableStart = 5;
        Parameters1[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        Parameters1[0].DescriptorTable.NumDescriptorRanges = 1;
        Parameters1[0].DescriptorTable.pDescriptorRanges = &Range1;
        Parameters1[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        Parameters1[1].Descriptor.ShaderRegister = 3;
        Parameters1[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
        Parameters1[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        Parameters1[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        Parameters1[2].Descriptor.ShaderRegister = 7;
        Versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        Versioned.Desc_1_1.NumParameters = 3;
        Versioned.Desc_1_1.pParameters = Parameters1;
        Versioned.Desc_1_1.NumStaticSamplers = 1;
        Versioned.Desc_1_1.pStaticSamplers = &Sampler;
        Versioned.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

        hr = Check.SerializeVersioned(&Versioned, &Blob, &Errors);
        Check.Report("serialize_1_1", "0x%08lX", hr);
        if (SUCCEEDED(hr))
        {
            TcReportBytes(Check, "serialized_1_1_bytes", Blob.Get());

            hr = Check.CreateVersionedDeserializer(Blob->GetBufferPointer(), Blob->GetBufferSize(),
                                                   IID_PPV_ARGS(&Versions));
            Check.Report("versioned_deserialize", "0x%08lX", hr);
            if (SUCCEEDED(hr))
            {
                hr = Versions->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &Back);
                Check.Report("at_1_0", "0x%08lX %u range %u %u", hr, SUCCEEDED(hr) ? Back->Desc_1_0.NumParameters : 0,
                             SUCCEEDED(hr) ? Back->Desc_1_0.pParameters[0].DescriptorTable.pDescriptorRanges[0].NumDescriptors : 0,
                             SUCCEEDED(hr) ? Back->Desc_1_0.pParameters[1].ParameterType : 0);
                hr = Versions->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_2, &Back);
                Check.Report("at_1_2", "0x%08lX %u flags 0x%x", hr, SUCCEEDED(hr) ? Back->Version : 0,
                             SUCCEEDED(hr) ? Back->Desc_1_2.pStaticSamplers[0].Flags : 0);
                Back = Versions->GetUnconvertedRootSignatureDesc();
                Check.Report("unconverted", "%u %u 0x%x", Back->Version, Back->Desc_1_1.NumParameters,
                             Back->Desc_1_1.pParameters[0].DescriptorTable.pDescriptorRanges[0].Flags);
            }

            hr = Check.CreateDeserializer(Blob->GetBufferPointer(), Blob->GetBufferSize(), IID_PPV_ARGS(&Deserializer));
            Check.Report("legacy_on_1_1", "0x%08lX", hr);
        }

        {
            D3D12_ROOT_PARAMETER Mixed[3] = { Parameters[0], Parameters[1] };
            D3D12_DESCRIPTOR_RANGE SamplerRange = {};
            D3D12_ROOT_PARAMETER SamplerTable = {};
            D3D12_ROOT_SIGNATURE_DESC Old = Desc;

            Mixed[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            Mixed[2].Descriptor.ShaderRegister = 4;
            Old.NumParameters = 3;
            Old.pParameters = Mixed;
            hr = Check.SerializeRootSignature(&Old, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Errors);
            if (SUCCEEDED(hr))
                hr = Check.CreateVersionedDeserializer(Blob->GetBufferPointer(), Blob->GetBufferSize(),
                                                       IID_PPV_ARGS(&Versions));
            if (SUCCEEDED(hr))
                hr = Versions->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &Back);
            Check.Report("from_1_0", "0x%08lX range 0x%x uav 0x%x", hr,
                         SUCCEEDED(hr) ? Back->Desc_1_1.pParameters[0].DescriptorTable.pDescriptorRanges[0].Flags : 0,
                         SUCCEEDED(hr) ? Back->Desc_1_1.pParameters[2].Descriptor.Flags : 0);

            SamplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            SamplerRange.NumDescriptors = 1;
            SamplerTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            SamplerTable.DescriptorTable.NumDescriptorRanges = 1;
            SamplerTable.DescriptorTable.pDescriptorRanges = &SamplerRange;
            Old.NumParameters = 1;
            Old.pParameters = &SamplerTable;
            hr = Check.SerializeRootSignature(&Old, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Errors);
            Check.Report("sampler_overlap", "0x%08lX errors %d", hr, Errors != nullptr);
            SamplerRange.BaseShaderRegister = 1;
            SamplerTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            SamplerTable.DescriptorTable.NumDescriptorRanges = 1;
            SamplerTable.DescriptorTable.pDescriptorRanges = &SamplerRange;
            Old.NumParameters = 1;
            Old.pParameters = &SamplerTable;
            hr = Check.SerializeRootSignature(&Old, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Errors);
            if (SUCCEEDED(hr))
                hr = Check.CreateVersionedDeserializer(Blob->GetBufferPointer(), Blob->GetBufferSize(),
                                                       IID_PPV_ARGS(&Versions));
            if (SUCCEEDED(hr))
                hr = Versions->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &Back);
            Check.Report("sampler_from_1_0", "0x%08lX 0x%x", hr,
                         SUCCEEDED(hr) ? Back->Desc_1_1.pParameters[0].DescriptorTable.pDescriptorRanges[0].Flags : 0);
            Check.Report("unconverted_1_0", "%u", Versions ? Versions->GetUnconvertedRootSignatureDesc()->Version : 0);
        }

        Versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
        Versioned.Desc_1_0 = Desc;
        hr = Check.SerializeVersioned(&Versioned, &Blob, &Errors);
        Check.Report("versioned_1_0_same", "0x%08lX %u", hr, SUCCEEDED(hr) ? TcBlobHash(Blob.Get()) == Hash : 0);
        Versioned.Version = (D3D_ROOT_SIGNATURE_VERSION)9;
        Check.Report("versioned_bogus", "0x%08lX", Check.SerializeVersioned(&Versioned, &Blob, &Errors));
    }

    hr = Check.CreateDeserializer(&Hash, sizeof(Hash), IID_PPV_ARGS(&Deserializer));
    Check.Report("deserialize_junk", "0x%08lX", hr);
    Check.Report("serialize_null", "0x%08lX", Check.SerializeRootSignature(nullptr, D3D_ROOT_SIGNATURE_VERSION_1, &Blob,
                                                                            &Errors));
    Check.Report("serialize_v11", "0x%08lX", Check.SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &Blob,
                                                                           &Errors));
    Range.NumDescriptors = 0;
    hr = Check.SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Errors);
    Check.Report("serialize_empty_range", "0x%08lX errors %d", hr, Errors != nullptr);
    Range.NumDescriptors = 2;
    Parameters[0].ShaderVisibility = (D3D12_SHADER_VISIBILITY)42;
    hr = Check.SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Errors);
    Check.Report("serialize_bad_visibility", "0x%08lX errors %d", hr, Errors != nullptr);
    Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    Parameters[1].Constants.Num32BitValues = 70;
    hr = Check.SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Errors);
    Check.Report("serialize_too_big", "0x%08lX errors %d", hr, Errors != nullptr);
}

/* Raw bytes of a structure, grouped by four */
static
std::string
TcHex(
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    std::string Text;
    char Hex[4];
    SIZE_T Index;

    for (Index = 0; Index < Size; Index++)
    {
        sprintf_s(Hex, "%02x", Bytes[Index]);
        Text += Hex;
        if (Index % 4 == 3 && Index + 1 < Size)
            Text += ' ';
    }
    return Text;
}

/* exports: names and ordinals of the loaded d3d12.dll and the core it uses */
static
VOID
TcReportExports(
    _In_ TwelveCheck& Check,
    _In_z_ PCSTR Prefix,
    _In_opt_ HMODULE Module)
{
    const BYTE* Base = reinterpret_cast<const BYTE*>(Module);
    const IMAGE_NT_HEADERS* Nt;
    const IMAGE_DATA_DIRECTORY* Directory;
    const IMAGE_EXPORT_DIRECTORY* Exports;
    const DWORD* Names;
    const WORD* NameOrdinals;
    const DWORD* Functions;
    std::vector<std::string> Named;
    char Key[96];
    DWORD Index;

    if (Module == NULL)
    {
        sprintf_s(Key, "%s_missing", Prefix);
        Check.Report(Key, "1");
        return;
    }

    Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(Base + reinterpret_cast<const IMAGE_DOS_HEADER*>(Base)->e_lfanew);
    Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    Exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(Base + Directory->VirtualAddress);
    Names = reinterpret_cast<const DWORD*>(Base + Exports->AddressOfNames);
    NameOrdinals = reinterpret_cast<const WORD*>(Base + Exports->AddressOfNameOrdinals);
    Functions = reinterpret_cast<const DWORD*>(Base + Exports->AddressOfFunctions);

    sprintf_s(Key, "%s_count", Prefix);
    Check.Report(Key, "base %lu functions %lu names %lu", Exports->Base, Exports->NumberOfFunctions,
                 Exports->NumberOfNames);

    for (Index = 0; Index < Exports->NumberOfFunctions; Index++)
    {
        PCSTR Name = nullptr;
        BOOLEAN IsData;
        DWORD Rva = Functions[Index];
        DWORD NameIndex;

        if (Rva == 0)
            continue;

        for (NameIndex = 0; NameIndex < Exports->NumberOfNames; NameIndex++)
        {
            if (NameOrdinals[NameIndex] == Index)
                Name = reinterpret_cast<PCSTR>(Base + Names[NameIndex]);
        }

        /* Data exports live outside executable sections */
        IsData = TRUE;
        {
            const IMAGE_SECTION_HEADER* Section = IMAGE_FIRST_SECTION(Nt);
            WORD SectionIndex;

            for (SectionIndex = 0; SectionIndex < Nt->FileHeader.NumberOfSections; SectionIndex++)
            {
                if (Rva >= Section[SectionIndex].VirtualAddress &&
                    Rva < Section[SectionIndex].VirtualAddress + Section[SectionIndex].Misc.VirtualSize)
                {
                    IsData = (Section[SectionIndex].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0;
                }
            }
        }

        sprintf_s(Key, "%s_%lu", Prefix, Exports->Base + Index);
        Check.Report(Key, "%s%s", Name != nullptr ? Name : "[noname]", IsData ? " data" : "");
    }
}

static
VOID
TcCheckExports(
    _In_ TwelveCheck& Check)
{
    TcReportExports(Check, "d3d12", Check.D3d12);
    TcReportExports(Check, "core", GetModuleHandleW(L"d3d12core.dll"));
}

/* interfaces: D3D12GetInterface and D3D12GetDebugInterface for each class and interface */
static
VOID
TcCheckInterfaces(
    _In_ TwelveCheck& Check)
{
    static const GUID Bogus = {0x12345678, 0x1234, 0x1234, {1, 2, 3, 4, 5, 6, 7, 8}};
    static const GUID DriverVerifierClsid = {0xb7b44efa, 0x03ed, 0x4af2, {0xab, 0xc8, 0x51, 0x30, 0x04, 0x51, 0x9d, 0xcb}};
    static const GUID DriverVerifierIid = {0x0f8258d2, 0xe908, 0x4505, {0x93, 0xe4, 0x5c, 0x17, 0xa1, 0x1b, 0x67, 0x9c}};
    static const struct
    {
        PCSTR Name;
        GUID Clsid;
    } Classes[] =
    {
        { "debug", CLSID_D3D12Debug }, { "tools", CLSID_D3D12Tools },
        { "dred", CLSID_D3D12DeviceRemovedExtendedData }, { "sdk", CLSID_D3D12SDKConfiguration },
        { "factory", CLSID_D3D12DeviceFactory }, { "verifier", DriverVerifierClsid }, { "bogus", Bogus },
    };
    static const struct
    {
        PCSTR Name;
        IID Iid;
    } Interfaces[] =
    {
        { "unknown", __uuidof(IUnknown) }, { "debug", __uuidof(ID3D12Debug) }, { "debug1", __uuidof(ID3D12Debug1) },
        { "debug2", __uuidof(ID3D12Debug2) }, { "debug3", __uuidof(ID3D12Debug3) },
        { "debug6", __uuidof(ID3D12Debug6) }, { "tools", __uuidof(ID3D12Tools) },
        { "tools1", __uuidof(ID3D12Tools1) }, { "tools2", __uuidof(ID3D12Tools2) },
        { "dred", __uuidof(ID3D12DeviceRemovedExtendedDataSettings) },
        { "dred1", __uuidof(ID3D12DeviceRemovedExtendedDataSettings1) },
        { "dred2", __uuidof(ID3D12DeviceRemovedExtendedDataSettings2) },
        { "sdk", __uuidof(ID3D12SDKConfiguration) }, { "sdk1", __uuidof(ID3D12SDKConfiguration1) },
        { "factory", __uuidof(ID3D12DeviceFactory) }, { "config", __uuidof(ID3D12DeviceConfiguration) },
        { "verifier", DriverVerifierIid }, { "bogus", Bogus },
    };
    char Key[96];

    for (const auto& Class : Classes)
    {
        for (const auto& Each : Interfaces)
        {
            IUnknown* Unknown = reinterpret_cast<IUnknown*>(1);
            HRESULT hr;

            hr = Check.GetInterface(Class.Clsid, Each.Iid, reinterpret_cast<void**>(&Unknown));
            sprintf_s(Key, "get_%s_%s", Class.Name, Each.Name);
            Check.Report(Key, "0x%08lX %s", hr, Unknown == nullptr ? "null" : Unknown == reinterpret_cast<IUnknown*>(1) ? "kept" : "set");
            if (SUCCEEDED(hr) && Unknown != nullptr && Unknown != reinterpret_cast<IUnknown*>(1))
                Unknown->Release();

            sprintf_s(Key, "get_%s_%s_check", Class.Name, Each.Name);
            Check.Report(Key, "0x%08lX", Check.GetInterface(Class.Clsid, Each.Iid, nullptr));
        }
    }

    for (const auto& Each : Interfaces)
    {
        IUnknown* Unknown = reinterpret_cast<IUnknown*>(1);
        HRESULT hr;

        hr = Check.GetDebugInterface(Each.Iid, reinterpret_cast<void**>(&Unknown));
        sprintf_s(Key, "debug_%s", Each.Name);
        Check.Report(Key, "0x%08lX %s", hr, Unknown == nullptr ? "null" : Unknown == reinterpret_cast<IUnknown*>(1) ? "kept" : "set");
        if (SUCCEEDED(hr) && Unknown != nullptr && Unknown != reinterpret_cast<IUnknown*>(1))
            Unknown->Release();

        sprintf_s(Key, "debug_%s_check", Each.Name);
        Check.Report(Key, "0x%08lX", Check.GetDebugInterface(Each.Iid, nullptr));
    }

    /* The same object each time, and what it answers to */
    {
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> First;
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> Second;
        ComPtr<IUnknown> Face;

        Check.GetDebugInterface(IID_PPV_ARGS(&First));
        Check.GetDebugInterface(IID_PPV_ARGS(&Second));
        Check.Report("dred_same", "%d", First.Get() == Second.Get());
        if (First != nullptr)
        {
            Check.Report("dred_qi_unknown", "0x%08lX", First.As(&Face));
            Check.Report("dred_qi_debug", "0x%08lX", First->QueryInterface(__uuidof(ID3D12Debug), &Face));
            Check.Report("dred_qi_null", "0x%08lX", First->QueryInterface(__uuidof(IUnknown), nullptr));
        }
    }

    {
        ComPtr<ID3D12SDKConfiguration1> Sdk;
        ComPtr<ID3D12SDKConfiguration> Sdk0;
        ComPtr<IUnknown> Face;
        HRESULT hr;

        hr = Check.GetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&Sdk));
        Check.Report("sdk", "0x%08lX", hr);
        if (SUCCEEDED(hr))
        {
            Check.GetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&Sdk0));
            Check.Report("sdk_same", "%d", Sdk0.Get() == static_cast<ID3D12SDKConfiguration*>(Sdk.Get()));
            Check.Report("sdk_qi_unknown", "0x%08lX", Sdk.As(&Face));
            Check.Report("sdk_qi_factory", "0x%08lX", Sdk->QueryInterface(__uuidof(ID3D12DeviceFactory), &Face));
            Check.Report("sdk_set_null", "0x%08lX", Sdk->SetSDKVersion(616, nullptr));
            Check.Report("sdk_set", "0x%08lX", Sdk->SetSDKVersion(616, ".\\D3D12\\"));
            {
                ComPtr<ID3D12DeviceFactory> Factory;

                Check.Report("sdk_factory_bogus_path", "0x%08lX",
                             Sdk->CreateDeviceFactory(616, ".\\NoSuchDir\\", IID_PPV_ARGS(&Factory)));
            }
        }
    }

    /* Device factories: flags, configuration and the devices they hand out */
    {
        static const GUID CreatorA = {0x1d5bd3a1, 0x6a4e, 0x4b11, {0x9b, 0x1e, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc}};
        static const GUID CreatorB = {0x2e6ce4b2, 0x7b5f, 0x4c22, {0xac, 0x2f, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd}};
        ComPtr<ID3D12DeviceFactory> Factory;
        ComPtr<ID3D12DeviceConfiguration> Config;
        ComPtr<ID3D12DeviceConfiguration1> Config1;
        ComPtr<ID3D12Device> Global;
        HRESULT hr;

        hr = Check.GetInterface(CLSID_D3D12DeviceFactory, IID_PPV_ARGS(&Factory));
        Check.Report("factory", "0x%08lX", hr);
        if (SUCCEEDED(hr))
        {
            D3D12_DEVICE_CONFIGURATION_DESC Desc;
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings> Dred;
            ComPtr<ID3D12Device> First;
            ComPtr<ID3D12Device> Second;
            ComPtr<ID3D12Device> Third;
            GUID Features[2] = {};

            Check.Report("factory_flags", "0x%x", Factory->GetFlags());
            for (UINT Flags = 0; Flags < 9; Flags++)
            {
                sprintf_s(Key, "factory_set_flags_%u", Flags);
                Check.Report(Key, "0x%08lX 0x%x", Factory->SetFlags((D3D12_DEVICE_FACTORY_FLAGS)Flags),
                             Factory->GetFlags());
            }
            Factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_NONE);

            Check.Report("factory_qi_config", "0x%08lX", Factory.As(&Config));
            Check.Report("factory_qi_config1", "0x%08lX", Factory.As(&Config1));
            if (Config != nullptr)
            {
                Desc = Config->GetDesc();
                Check.Report("config_desc", "0x%x 0x%x %u %u", Desc.Flags, Desc.GpuBasedValidationFlags,
                             Desc.SDKVersion, Desc.NumEnabledExperimentalFeatures);
                Check.Report("config_features_none", "0x%08lX", Config->GetEnabledExperimentalFeatures(nullptr, 0));
                Check.Report("config_features_two", "0x%08lX", Config->GetEnabledExperimentalFeatures(Features, 2));
            }

            Check.Report("factory_init_global", "0x%08lX", Factory->InitializeFromGlobalState());
            Check.Report("factory_dred", "0x%08lX",
                         Factory->GetConfigurationInterface(CLSID_D3D12DeviceRemovedExtendedData, IID_PPV_ARGS(&Dred)));
            if (Dred != nullptr && Config != nullptr)
            {
                Dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                Desc = Config->GetDesc();
                Check.Report("config_desc_dred", "0x%x", Desc.Flags);
                Dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED);
            }
            Check.Report("factory_experimental_none", "0x%08lX",
                         Factory->EnableExperimentalFeatures(0, nullptr, nullptr, nullptr));
            Check.Report("factory_experimental_models", "0x%08lX",
                         Factory->EnableExperimentalFeatures(1, &D3D12ExperimentalShaderModels, nullptr, nullptr));

            /* Which devices come back depends on the flags and the global singleton */
            Check.MakeDevice(&Global);
            Check.Report("factory_device_check", "0x%08lX",
                         Factory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
            Check.Report("factory_device_bad_level", "0x%08lX",
                         Factory->CreateDevice(nullptr, (D3D_FEATURE_LEVEL)0x1234, IID_PPV_ARGS(&Third)));
            Third.Reset();
            hr = Factory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&First));
            Check.Report("factory_device_new", "0x%08lX same %d", hr, First.Get() == Global.Get());
            Factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE);
            hr = Factory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Second));
            Check.Report("factory_device_existing", "0x%08lX same %d %d", hr, Second.Get() == Global.Get(),
                         Second.Get() == First.Get());
            Factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
            hr = Factory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Third));
            Check.Report("factory_device_unstored", "0x%08lX same %d %d", hr, Third.Get() == Global.Get(),
                         Third.Get() == First.Get());

            /* Fresh factories, with and without the global state, asking for the existing device */
            for (UINT Variant = 0; Variant < 3; Variant++)
            {
                ComPtr<ID3D12DeviceFactory> Fresh;
                ComPtr<ID3D12Device> Found;

                if (FAILED(Check.GetInterface(CLSID_D3D12DeviceFactory, IID_PPV_ARGS(&Fresh))))
                    break;
                if (Variant != 0)
                    Fresh->InitializeFromGlobalState();
                Fresh->SetFlags(Variant == 2 ? (D3D12_DEVICE_FACTORY_FLAGS)3
                                             : D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE);
                hr = Fresh->CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Found));
                sprintf_s(Key, "factory_fresh_existing_%u", Variant);
                Check.Report(Key, "0x%08lX same %d", hr, Found.Get() == Global.Get());
            }

            if (First != nullptr && First.Get() != Global.Get())
            {
                ComPtr<ID3D12Device> Again;

                Check.MakeDevice(&Again);
                Check.Report("factory_global_after", "same %d", Again.Get() == Global.Get());
            }
        }

        /* Queues from different creators */
        if (Global != nullptr)
        {
            ComPtr<ID3D12Device9> Device9;

            if (SUCCEEDED(Global.As(&Device9)))
            {
                D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
                ComPtr<ID3D12CommandQueue> QueueA;
                ComPtr<ID3D12CommandQueue> QueueB;
                ComPtr<ID3D12CommandQueue> QueueNull;

                QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                Check.Report("queue1_a", "0x%08lX",
                             Device9->CreateCommandQueue1(&QueueDesc, CreatorA, IID_PPV_ARGS(&QueueA)));
                Check.Report("queue1_b", "0x%08lX",
                             Device9->CreateCommandQueue1(&QueueDesc, CreatorB, IID_PPV_ARGS(&QueueB)));
                Check.Report("queue1_null_creator", "0x%08lX",
                             Device9->CreateCommandQueue1(&QueueDesc, GUID_NULL, IID_PPV_ARGS(&QueueNull)));
                QueueNull.Reset();
                Check.Report("queue1_no_out", "0x%08lX",
                             Device9->CreateCommandQueue1(&QueueDesc, CreatorA, __uuidof(ID3D12CommandQueue), nullptr));
                QueueDesc.Type = (D3D12_COMMAND_LIST_TYPE)99;
                Check.Report("queue1_bad_type", "0x%08lX",
                             Device9->CreateCommandQueue1(&QueueDesc, CreatorA, IID_PPV_ARGS(&QueueNull)));
            }
        }
    }
}

/* create: every feature level, adapter and interface the device can be asked for */
static
VOID
TcCheckCreateLevels(
    _In_ TwelveCheck& Check)
{
    static const UINT Levels[] = { 0, 0x9100, 0x9200, 0x9300, 0xa000, 0xa100, 0xb000, 0xb100, 0xc000, 0xc100,
                                   0xc200, 0x1000, 0x100, 0xb001, 0xd000, 0xffffffff };
    static const struct
    {
        PCSTR Name;
        IID Iid;
    } Interfaces[] =
    {
        { "unknown", __uuidof(IUnknown) }, { "device", __uuidof(ID3D12Device) },
        { "device5", __uuidof(ID3D12Device5) }, { "device14", __uuidof(ID3D12Device14) },
        { "object", __uuidof(ID3D12Object) }, { "debug", __uuidof(ID3D12DebugDevice) },
        { "infoqueue", __uuidof(ID3D12InfoQueue) }, { "dxgi", __uuidof(IDXGIDevice) },
        { "pageable", __uuidof(ID3D12Pageable) },
    };
    decltype(&CreateDXGIFactory1) CreateFactory;
    ComPtr<IDXGIFactory4> Factory;
    ComPtr<IDXGIAdapter1> Adapter;
    char Key[96];

    for (UINT Level : Levels)
    {
        ComPtr<ID3D12Device> Device;
        HRESULT hr;

        sprintf_s(Key, "check_%x", Level);
        Check.Report(Key, "0x%08lX", Check.CreateDevice(nullptr, (D3D_FEATURE_LEVEL)Level, __uuidof(ID3D12Device),
                                                         nullptr));
        hr = Check.CreateDevice(nullptr, (D3D_FEATURE_LEVEL)Level, IID_PPV_ARGS(&Device));
        sprintf_s(Key, "make_%x", Level);
        Check.Report(Key, "0x%08lX %s", hr, Device != nullptr ? "set" : "null");
    }

    for (const auto& Each : Interfaces)
    {
        IUnknown* Unknown = reinterpret_cast<IUnknown*>(1);
        HRESULT hr;

        hr = Check.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, Each.Iid, reinterpret_cast<void**>(&Unknown));
        sprintf_s(Key, "iid_%s", Each.Name);
        Check.Report(Key, "0x%08lX %s", hr, Unknown == nullptr ? "null" : Unknown == reinterpret_cast<IUnknown*>(1) ? "kept" : "set");
        if (SUCCEEDED(hr) && Unknown != nullptr && Unknown != reinterpret_cast<IUnknown*>(1))
            Unknown->Release();
    }

    CreateFactory = reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(LoadLibraryW(L"dxgi.dll"),
                                                                                  "CreateDXGIFactory1"));
    if (CreateFactory == nullptr || FAILED(CreateFactory(IID_PPV_ARGS(&Factory))))
        return;

    if (SUCCEEDED(Factory->EnumAdapters1(0, &Adapter)))
    {
        ComPtr<ID3D12Device> Mine;
        ComPtr<ID3D12Device> Plain;
        LUID Luid;
        DXGI_ADAPTER_DESC1 Desc;

        Check.Report("adapter0_check", "0x%08lX", Check.CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                                                      __uuidof(ID3D12Device), nullptr));
        Check.Report("adapter0", "0x%08lX", Check.CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                                                IID_PPV_ARGS(&Mine)));
        Check.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Plain));
        Check.Report("adapter0_same", "%d", Mine.Get() == Plain.Get());
        Adapter->GetDesc1(&Desc);
        if (Mine != nullptr)
        {
            Luid = Mine->GetAdapterLuid();
            Check.Report("adapter0_luid", "%d", memcmp(&Luid, &Desc.AdapterLuid, sizeof(Luid)) == 0);
        }
    }

    Check.Report("factory_as_adapter", "0x%08lX", Check.CreateDevice(Factory.Get(), D3D_FEATURE_LEVEL_11_0,
                                                                      __uuidof(ID3D12Device), nullptr));
}

/* Sizes of the CheckFeatureSupport structures this SDK knows */
static
UINT
TcFeatureSize(
    _In_ UINT Feature)
{
    switch (Feature)
    {
        case D3D12_FEATURE_D3D12_OPTIONS: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS);
        case D3D12_FEATURE_ARCHITECTURE: return sizeof(D3D12_FEATURE_DATA_ARCHITECTURE);
        case D3D12_FEATURE_FEATURE_LEVELS: return sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS);
        case D3D12_FEATURE_FORMAT_SUPPORT: return sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT);
        case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS: return sizeof(D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS);
        case D3D12_FEATURE_FORMAT_INFO: return sizeof(D3D12_FEATURE_DATA_FORMAT_INFO);
        case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT: return sizeof(D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT);
        case D3D12_FEATURE_SHADER_MODEL: return sizeof(D3D12_FEATURE_DATA_SHADER_MODEL);
        case D3D12_FEATURE_D3D12_OPTIONS1: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS1);
        case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT:
            return sizeof(D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT);
        case D3D12_FEATURE_ROOT_SIGNATURE: return sizeof(D3D12_FEATURE_DATA_ROOT_SIGNATURE);
        case D3D12_FEATURE_ARCHITECTURE1: return sizeof(D3D12_FEATURE_DATA_ARCHITECTURE1);
        case D3D12_FEATURE_D3D12_OPTIONS2: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS2);
        case D3D12_FEATURE_SHADER_CACHE: return sizeof(D3D12_FEATURE_DATA_SHADER_CACHE);
        case D3D12_FEATURE_COMMAND_QUEUE_PRIORITY: return sizeof(D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY);
        case D3D12_FEATURE_D3D12_OPTIONS3: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS3);
        case D3D12_FEATURE_EXISTING_HEAPS: return sizeof(D3D12_FEATURE_DATA_EXISTING_HEAPS);
        case D3D12_FEATURE_D3D12_OPTIONS4: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS4);
        case D3D12_FEATURE_SERIALIZATION: return sizeof(D3D12_FEATURE_DATA_SERIALIZATION);
        case D3D12_FEATURE_CROSS_NODE: return sizeof(D3D12_FEATURE_DATA_CROSS_NODE);
        case D3D12_FEATURE_D3D12_OPTIONS5: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5);
        case D3D12_FEATURE_DISPLAYABLE: return sizeof(D3D12_FEATURE_DATA_DISPLAYABLE);
        case D3D12_FEATURE_D3D12_OPTIONS6: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS6);
        case D3D12_FEATURE_QUERY_META_COMMAND: return sizeof(D3D12_FEATURE_DATA_QUERY_META_COMMAND);
        case D3D12_FEATURE_D3D12_OPTIONS7: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7);
        case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT:
            return sizeof(D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT);
        case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES:
            return sizeof(D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES);
        case D3D12_FEATURE_D3D12_OPTIONS8: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS8);
        case D3D12_FEATURE_D3D12_OPTIONS9: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS9);
        case D3D12_FEATURE_D3D12_OPTIONS10: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS10);
        case D3D12_FEATURE_D3D12_OPTIONS11: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS11);
        case D3D12_FEATURE_D3D12_OPTIONS12: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS12);
        case D3D12_FEATURE_D3D12_OPTIONS13: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS13);
        case D3D12_FEATURE_D3D12_OPTIONS14: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS14);
        case D3D12_FEATURE_D3D12_OPTIONS15: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS15);
        case D3D12_FEATURE_D3D12_OPTIONS16: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS16);
        case D3D12_FEATURE_D3D12_OPTIONS17: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS17);
        case D3D12_FEATURE_D3D12_OPTIONS18: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS18);
        case D3D12_FEATURE_D3D12_OPTIONS19: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS19);
        case D3D12_FEATURE_D3D12_OPTIONS20: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS20);
        case D3D12_FEATURE_PREDICATION: return sizeof(D3D12_FEATURE_DATA_PREDICATION);
        case D3D12_FEATURE_PLACED_RESOURCE_SUPPORT_INFO:
            return sizeof(D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO);
        case D3D12_FEATURE_HARDWARE_COPY: return sizeof(D3D12_FEATURE_DATA_HARDWARE_COPY);
        case D3D12_FEATURE_D3D12_OPTIONS21: return sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS21);
        case D3D12_FEATURE_D3D12_TIGHT_ALIGNMENT: return sizeof(D3D12_FEATURE_DATA_TIGHT_ALIGNMENT);
        case D3D12_FEATURE_APPLICATION_SPECIFIC_DRIVER_STATE:
            return sizeof(D3D12_FEATURE_DATA_APPLICATION_SPECIFIC_DRIVER_STATE);
        case D3D12_FEATURE_BYTECODE_BYPASS_HASH_SUPPORTED:
            return sizeof(D3D12_FEATURE_DATA_BYTECODE_BYPASS_HASH_SUPPORTED);
        default: return 0;
    }
}

/* One query: the buffer starts as 0xcd past the inputs so every byte the runtime writes shows */
static
VOID
TcQueryFeature(
    _In_ TwelveCheck& Check,
    _In_ ID3D12Device* Device,
    _In_z_ PCSTR Key,
    _In_ UINT Feature,
    _In_reads_bytes_(InputSize) const VOID* Input,
    _In_ UINT InputSize,
    _In_ UINT Size)
{
    alignas(16) BYTE Buffer[512];
    HRESULT hr;

    memset(Buffer, 0xcd, sizeof(Buffer));
    memcpy(Buffer, Input, InputSize);
    hr = Device->CheckFeatureSupport((D3D12_FEATURE)Feature, Buffer, Size);
    Check.Report(Key, "0x%08lX %s", hr, TcHex(Buffer, Size > 0 ? (Size < 256 ? Size : 256) : 4).c_str());
}

/* featureall: every feature with the right size, a wrong size and no buffer */
static
VOID
TcCheckFeatureAll(
    _In_ TwelveCheck& Check)
{
    ComPtr<ID3D12Device> Device;
    char Key[96];
    UINT Feature;

    if (FAILED(Check.MakeDevice(&Device)))
        return;

    for (Feature = 0; Feature < 70; Feature++)
    {
        UINT Size = TcFeatureSize(Feature);
        BYTE Input[64] = {};
        UINT InputSize = 0;

        /* Queries carrying pointers are covered on their own */
        if (Feature == D3D12_FEATURE_FEATURE_LEVELS || Feature == D3D12_FEATURE_QUERY_META_COMMAND ||
            Feature == D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES)
        {
            continue;
        }

        switch (Feature)
        {
            case D3D12_FEATURE_ARCHITECTURE:
            case D3D12_FEATURE_ARCHITECTURE1:
            case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT:
            case D3D12_FEATURE_SERIALIZATION:
            case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT:
                InputSize = 4;
                break;
            case D3D12_FEATURE_FORMAT_SUPPORT:
            case D3D12_FEATURE_FORMAT_INFO:
                *reinterpret_cast<UINT*>(Input) = DXGI_FORMAT_R8G8B8A8_UNORM;
                InputSize = 4;
                break;
            case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS:
                reinterpret_cast<UINT*>(Input)[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                reinterpret_cast<UINT*>(Input)[1] = 4;
                InputSize = 12;
                break;
            case D3D12_FEATURE_SHADER_MODEL:
                *reinterpret_cast<UINT*>(Input) = D3D_SHADER_MODEL_6_9;
                InputSize = 4;
                break;
            case D3D12_FEATURE_ROOT_SIGNATURE:
                *reinterpret_cast<UINT*>(Input) = D3D_ROOT_SIGNATURE_VERSION_1_2;
                InputSize = 4;
                break;
            case D3D12_FEATURE_COMMAND_QUEUE_PRIORITY:
                InputSize = 8;
                break;
            case D3D12_FEATURE_PLACED_RESOURCE_SUPPORT_INFO:
            {
                D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO* Placed =
                    reinterpret_cast<D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO*>(Input);

                Placed->Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                Placed->Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                Placed->DestHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
                InputSize = offsetof(D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO, Supported);
                break;
            }
            default:
                break;
        }

        if (Size == 0)
        {
            sprintf_s(Key, "unknown_%u_4", Feature);
            TcQueryFeature(Check, Device.Get(), Key, Feature, Input, 0, 4);
            sprintf_s(Key, "unknown_%u_64", Feature);
            TcQueryFeature(Check, Device.Get(), Key, Feature, Input, 0, 64);
            continue;
        }

        sprintf_s(Key, "f%u", Feature);
        TcQueryFeature(Check, Device.Get(), Key, Feature, Input, InputSize, Size);
        sprintf_s(Key, "f%u_small", Feature);
        TcQueryFeature(Check, Device.Get(), Key, Feature, Input, InputSize < Size - 1 ? InputSize : Size - 1, Size - 1);
        sprintf_s(Key, "f%u_big", Feature);
        TcQueryFeature(Check, Device.Get(), Key, Feature, Input, InputSize, Size + 4);

    }

    /* Inputs that steer the answer */
    for (UINT Node = 0; Node < 3; Node++)
    {
        UINT Nodes[1] = { Node };

        sprintf_s(Key, "architecture1_node%u", Node);
        TcQueryFeature(Check, Device.Get(), Key, D3D12_FEATURE_ARCHITECTURE1, Nodes, 4,
                       sizeof(D3D12_FEATURE_DATA_ARCHITECTURE1));
        sprintf_s(Key, "serialization_node%u", Node);
        TcQueryFeature(Check, Device.Get(), Key, D3D12_FEATURE_SERIALIZATION, Nodes, 4,
                       sizeof(D3D12_FEATURE_DATA_SERIALIZATION));
        sprintf_s(Key, "session_support_node%u", Node);
        TcQueryFeature(Check, Device.Get(), Key, D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT, Nodes, 4,
                       sizeof(D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT));
        sprintf_s(Key, "session_count_node%u", Node);
        TcQueryFeature(Check, Device.Get(), Key, D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT, Nodes, 4,
                       sizeof(D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT));
    }

    for (UINT Type = 0; Type < 8; Type++)
    {
        static const UINT Priorities[] = { 0, 100, 10000, 7 };

        for (UINT Priority : Priorities)
        {
            UINT Input[2] = { Type, Priority };

            sprintf_s(Key, "queue_priority_%u_%u", Type, Priority);
            TcQueryFeature(Check, Device.Get(), Key, D3D12_FEATURE_COMMAND_QUEUE_PRIORITY, Input, sizeof(Input),
                           sizeof(D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY));
        }
    }

    for (UINT Model = 0x50; Model <= 0x6a; Model++)
    {
        UINT Input[1] = { Model };

        sprintf_s(Key, "shader_model_%x", Model);
        TcQueryFeature(Check, Device.Get(), Key, D3D12_FEATURE_SHADER_MODEL, Input, 4,
                       sizeof(D3D12_FEATURE_DATA_SHADER_MODEL));
    }

    for (UINT Version = 0; Version < 6; Version++)
    {
        UINT Input[1] = { Version };

        sprintf_s(Key, "root_signature_%u", Version);
        TcQueryFeature(Check, Device.Get(), Key, D3D12_FEATURE_ROOT_SIGNATURE, Input, 4,
                       sizeof(D3D12_FEATURE_DATA_ROOT_SIGNATURE));
    }

    {
        static const D3D_FEATURE_LEVEL All[] = { D3D_FEATURE_LEVEL_1_0_CORE, D3D_FEATURE_LEVEL_9_1,
                                                 D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_11_0,
                                                 D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_12_1,
                                                 D3D_FEATURE_LEVEL_12_2, (D3D_FEATURE_LEVEL)0xd000 };
        D3D12_FEATURE_DATA_FEATURE_LEVELS Levels;
        UINT Count;

        for (Count = 1; Count <= _countof(All); Count++)
        {
            Levels.NumFeatureLevels = Count;
            Levels.pFeatureLevelsRequested = All;
            Levels.MaxSupportedFeatureLevel = (D3D_FEATURE_LEVEL)0xcdcdcdcd;
            sprintf_s(Key, "levels_%u", Count);
            Check.Report(Key, "0x%08lX 0x%x", Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &Levels,
                                                                           sizeof(Levels)),
                         Levels.MaxSupportedFeatureLevel);
        }

        Levels.NumFeatureLevels = 2;
        Levels.pFeatureLevelsRequested = nullptr;
        Levels.MaxSupportedFeatureLevel = (D3D_FEATURE_LEVEL)0xcdcdcdcd;
        Check.Report("levels_null_list", "0x%08lX 0x%x", Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
                                                                                      &Levels, sizeof(Levels)),
                     Levels.MaxSupportedFeatureLevel);
        Levels.NumFeatureLevels = 1;
        Levels.pFeatureLevelsRequested = &All[_countof(All) - 1];
        Levels.MaxSupportedFeatureLevel = (D3D_FEATURE_LEVEL)0xcdcdcdcd;
        Check.Report("levels_unknown_only", "0x%08lX 0x%x", Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
                                                                                         &Levels, sizeof(Levels)),
                     Levels.MaxSupportedFeatureLevel);
    }

    {
        D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES Types = {};
        GUID Found[4] = {};

        Types.Count = 0;
        Types.pTypes = nullptr;
        Check.Report("session_types_zero", "0x%08lX", Device->CheckFeatureSupport(
                                                          D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES, &Types,
                                                          sizeof(Types)));
        Types.Count = 4;
        Types.pTypes = Found;
        Check.Report("session_types_four", "0x%08lX %s", Device->CheckFeatureSupport(
                                                             D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES, &Types,
                                                             sizeof(Types)),
                     TcHex(Found, sizeof(Found)).c_str());
    }

    {
        D3D12_FEATURE_DATA_QUERY_META_COMMAND Meta = {};
        BYTE Output[16] = {};

        Check.Report("meta_null", "0x%08lX", Device->CheckFeatureSupport(D3D12_FEATURE_QUERY_META_COMMAND, &Meta,
                                                                          sizeof(Meta)));
        Meta.pQueryOutputData = Output;
        Meta.QueryOutputDataSizeInBytes = sizeof(Output);
        Check.Report("meta_unknown", "0x%08lX", Device->CheckFeatureSupport(D3D12_FEATURE_QUERY_META_COMMAND, &Meta,
                                                                             sizeof(Meta)));
    }
}

/* formats: support, plane count and sample quality for every format value */
static
VOID
TcCheckFormats(
    _In_ TwelveCheck& Check)
{
    static const UINT Counts[] = { 0, 1, 2, 4, 8, 16, 32 };
    ComPtr<ID3D12Device> Device;
    char Key[96];

    if (FAILED(Check.MakeDevice(&Device)))
        return;

    for (UINT Format = 0; Format < 200; Format++)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT Support;
        D3D12_FEATURE_DATA_FORMAT_INFO Info;
        std::string Quality;
        HRESULT SupportResult;
        HRESULT InfoResult;

        memset(&Support, 0xcd, sizeof(Support));
        Support.Format = (DXGI_FORMAT)Format;
        SupportResult = Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support));

        memset(&Info, 0xcd, sizeof(Info));
        Info.Format = (DXGI_FORMAT)Format;
        InfoResult = Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &Info, sizeof(Info));

        for (UINT Count : Counts)
        {
            for (UINT Flags = 0; Flags < 3; Flags++)
            {
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS Levels;
                HRESULT hr;
                char Part[48];

                memset(&Levels, 0xcd, sizeof(Levels));
                Levels.Format = (DXGI_FORMAT)Format;
                Levels.SampleCount = Count;
                Levels.Flags = (D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS)Flags;
                hr = Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &Levels, sizeof(Levels));
                sprintf_s(Part, " %u/%u:%lx:%x", Count, Flags, hr & 0xffff, Levels.NumQualityLevels);
                Quality += Part;
            }
        }

        sprintf_s(Key, "format_%u", Format);
        Check.Report(Key, "0x%08lX %s info 0x%08lX %s ms%s", SupportResult, TcHex(&Support, sizeof(Support)).c_str(),
                     InfoResult, TcHex(&Info, sizeof(Info)).c_str(), Quality.c_str());
    }
}

/* queues: command queue, allocator and list creation checks */
static
VOID
TcCheckQueues(
    _In_ TwelveCheck& Check)
{
    static const INT Priorities[] = { 0, 100, 10000, 50, -1 };
    static const UINT Flags[] = { 0, 1, 2, 4 };
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12Device4> Device4;
    char Key[96];

    if (FAILED(Check.MakeDevice(&Device)))
        return;

    Check.Report("queue_null_desc", "0x%08lX", Device->CreateCommandQueue(nullptr, __uuidof(ID3D12CommandQueue),
                                                                           nullptr));

    for (UINT Type = 0; Type < 9; Type++)
    {
        for (INT Priority : Priorities)
        {
            for (UINT Flag : Flags)
            {
                for (UINT Node = 0; Node < 3; Node++)
                {
                    D3D12_COMMAND_QUEUE_DESC Desc = {};
                    HRESULT hr;

                    Desc.Type = (D3D12_COMMAND_LIST_TYPE)Type;
                    Desc.Priority = Priority;
                    Desc.Flags = (D3D12_COMMAND_QUEUE_FLAGS)Flag;
                    Desc.NodeMask = Node;
                    ComPtr<ID3D12CommandQueue> Queue;

                    hr = Device->CreateCommandQueue(&Desc, __uuidof(ID3D12CommandQueue), nullptr);
                    sprintf_s(Key, "queue_%u_%d_%u_%u", Type, Priority, Flag, Node);
                    Check.Report(Key, "0x%08lX 0x%08lX", hr, Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Queue)));
                }
            }
        }
    }

    for (UINT Type = 0; Type < 9; Type++)
    {
        D3D12_COMMAND_QUEUE_DESC Desc = {};
        ComPtr<ID3D12CommandQueue> Queue;
        HRESULT hr;

        Desc.Type = (D3D12_COMMAND_LIST_TYPE)Type;
        hr = Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Queue));
        sprintf_s(Key, "queue_make_%u", Type);
        if (SUCCEEDED(hr))
        {
            D3D12_COMMAND_QUEUE_DESC Got = Queue->GetDesc();
            UINT64 Frequency = 0;
            UINT64 Gpu = 0;
            UINT64 Cpu = 0;
            HRESULT FrequencyResult = Queue->GetTimestampFrequency(&Frequency);
            HRESULT ClockResult = Queue->GetClockCalibration(&Gpu, &Cpu);

            Check.Report(Key, "0x%08lX desc %d %d 0x%x %u frequency 0x%08lX %d clock 0x%08lX %d", hr, Got.Type,
                         Got.Priority, Got.Flags, Got.NodeMask, FrequencyResult, Frequency != 0, ClockResult, Cpu != 0);
        }
        else
        {
            Check.Report(Key, "0x%08lX %s", hr, Queue != nullptr ? "set" : "null");
        }
    }

    {
        D3D12_COMMAND_QUEUE_DESC Desc = {};
        ComPtr<ID3D12CommandQueue> Queue;
        ComPtr<IUnknown> Face;

        Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Queue));
        if (Queue != nullptr)
        {
            static const struct
            {
                PCSTR Name;
                IID Iid;
            } Faces[] =
            {
                { "unknown", __uuidof(IUnknown) }, { "object", __uuidof(ID3D12Object) },
                { "child", __uuidof(ID3D12DeviceChild) }, { "pageable", __uuidof(ID3D12Pageable) },
                { "queue", __uuidof(ID3D12CommandQueue) },
                { "debug", __uuidof(ID3D12DebugCommandQueue) }, { "device", __uuidof(ID3D12Device) },
            };

            for (const auto& Each : Faces)
            {
                sprintf_s(Key, "queue_qi_%s", Each.Name);
                Check.Report(Key, "0x%08lX", Queue->QueryInterface(Each.Iid, &Face));
            }
        }
    }

    for (UINT Type = 0; Type < 9; Type++)
    {
        ComPtr<ID3D12CommandAllocator> Allocator;
        HRESULT hr;

        sprintf_s(Key, "allocator_check_%u", Type);
        Check.Report(Key, "0x%08lX", Device->CreateCommandAllocator((D3D12_COMMAND_LIST_TYPE)Type,
                                                                     __uuidof(ID3D12CommandAllocator), nullptr));
        hr = Device->CreateCommandAllocator((D3D12_COMMAND_LIST_TYPE)Type, IID_PPV_ARGS(&Allocator));
        sprintf_s(Key, "allocator_%u", Type);
        Check.Report(Key, "0x%08lX %s", hr, Allocator != nullptr ? "set" : "null");
        if (Allocator == nullptr)
            continue;

        for (UINT ListType = 0; ListType < 9; ListType++)
        {
            ComPtr<ID3D12GraphicsCommandList> List;

            hr = Device->CreateCommandList(0, (D3D12_COMMAND_LIST_TYPE)ListType, Allocator.Get(), nullptr,
                                           IID_PPV_ARGS(&List));
            sprintf_s(Key, "list_%u_on_%u", ListType, Type);
            Check.Report(Key, "0x%08lX %s", hr, List != nullptr ? "set" : "null");
            if (List != nullptr)
                List->Close();
        }
    }

    {
        ComPtr<ID3D12CommandAllocator> Allocator;
        ComPtr<ID3D12GraphicsCommandList> List;
        ComPtr<ID3D12GraphicsCommandList> Other;
        ComPtr<IUnknown> Face;
        HRESULT hr;

        Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Allocator));
        Check.Report("list_null_allocator", "0x%08lX", Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                                 nullptr, nullptr,
                                                                                 IID_PPV_ARGS(&List)));
        Check.Report("list_node2", "0x%08lX", Device->CreateCommandList(2, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                        Allocator.Get(), nullptr, IID_PPV_ARGS(&List)));
        Check.Report("list_check", "0x%08lX", Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                        Allocator.Get(), nullptr,
                                                                        __uuidof(ID3D12GraphicsCommandList), nullptr));
        hr = Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr,
                                       IID_PPV_ARGS(&List));
        Check.Report("list", "0x%08lX", hr);
        hr = Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr,
                                       IID_PPV_ARGS(&Other));
        Check.Report("list_same_allocator", "0x%08lX %s", hr, Other != nullptr ? "set" : "null");
        Check.Report("allocator_reset_recording", "0x%08lX", Allocator->Reset());
        if (List != nullptr)
        {
            static const struct
            {
                PCSTR Name;
                IID Iid;
            } Faces[] =
            {
                { "list", __uuidof(ID3D12CommandList) }, { "gfx", __uuidof(ID3D12GraphicsCommandList) },
                { "gfx1", __uuidof(ID3D12GraphicsCommandList1) }, { "gfx4", __uuidof(ID3D12GraphicsCommandList4) },
                { "gfx7", __uuidof(ID3D12GraphicsCommandList7) }, { "gfx10", __uuidof(ID3D12GraphicsCommandList10) },
                { "debug", __uuidof(ID3D12DebugCommandList) }, { "pageable", __uuidof(ID3D12Pageable) },
                { "child", __uuidof(ID3D12DeviceChild) },
            };

            for (const auto& Each : Faces)
            {
                sprintf_s(Key, "list_qi_%s", Each.Name);
                Check.Report(Key, "0x%08lX", List->QueryInterface(Each.Iid, &Face));
            }

            Check.Report("list_reset_open", "0x%08lX", List->Reset(Allocator.Get(), nullptr));
            Check.Report("list_close", "0x%08lX", List->Close());
            Check.Report("list_close_closed", "0x%08lX", List->Close());
            Check.Report("list_reset", "0x%08lX", List->Reset(Allocator.Get(), nullptr));
            List->Close();
            Check.Report("allocator_reset", "0x%08lX", Allocator->Reset());
        }
    }

    if (SUCCEEDED(Device.As(&Device4)))
    {
        for (UINT Type = 0; Type < 9; Type++)
        {
            ComPtr<ID3D12GraphicsCommandList> List;
            HRESULT hr;

            hr = Device4->CreateCommandList1(0, (D3D12_COMMAND_LIST_TYPE)Type, D3D12_COMMAND_LIST_FLAG_NONE,
                                             IID_PPV_ARGS(&List));
            sprintf_s(Key, "list1_%u", Type);
            Check.Report(Key, "0x%08lX %s", hr, List != nullptr ? "set" : "null");
            if (List != nullptr)
            {
                sprintf_s(Key, "list1_%u_close", Type);
                Check.Report(Key, "0x%08lX", List->Close());
            }
        }
        Check.Report("list1_flags", "0x%08lX", Device4->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                           (D3D12_COMMAND_LIST_FLAGS)1,
                                                                           __uuidof(ID3D12GraphicsCommandList),
                                                                           nullptr));
    }
}

/* heaps: descriptor heap creation, starts and descriptor sizes */
static
VOID
TcCheckDescriptorHeaps(
    _In_ TwelveCheck& Check)
{
    static const UINT Counts[] = { 0, 1, 2048, 2049, 1000000, 1000001, 2000000 };
    ComPtr<ID3D12Device> Device;
    char Key[96];

    if (FAILED(Check.MakeDevice(&Device)))
        return;

    for (UINT Type = 0; Type < 6; Type++)
    {
        sprintf_s(Key, "increment_%u", Type);
        Check.Report(Key, "%u", Device->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)Type));
    }

    Check.Report("null_desc", "0x%08lX", Device->CreateDescriptorHeap(nullptr, __uuidof(ID3D12DescriptorHeap),
                                                                       nullptr));

    for (UINT Type = 0; Type < 6; Type++)
    {
        for (UINT Count : Counts)
        {
            for (UINT Flags = 0; Flags < 3; Flags++)
            {
                for (UINT Node = 0; Node < 3; Node += 2)
                {
                    D3D12_DESCRIPTOR_HEAP_DESC Desc = {};

                    Desc.Type = (D3D12_DESCRIPTOR_HEAP_TYPE)Type;
                    Desc.NumDescriptors = Count;
                    Desc.Flags = (D3D12_DESCRIPTOR_HEAP_FLAGS)Flags;
                    Desc.NodeMask = Node;
                    ComPtr<ID3D12DescriptorHeap> Heap;
                    HRESULT Checked = Device->CreateDescriptorHeap(&Desc, __uuidof(ID3D12DescriptorHeap), nullptr);
                    HRESULT Made = E_ABORT;

                    /* The system runtime crashes making an empty shader visible heap */
                    if (Count != 0 || (Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) == 0)
                        Made = Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Heap));

                    sprintf_s(Key, "heap_%u_%u_%u_%u", Type, Count, Flags, Node);
                    Check.Report(Key, "0x%08lX 0x%08lX", Checked, Made);
                }
            }
        }
    }

    for (UINT Type = 0; Type < 4; Type++)
    {
        for (UINT Flags = 0; Flags < 2; Flags++)
        {
            D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
            ComPtr<ID3D12DescriptorHeap> Heap;
            HRESULT hr;

            Desc.Type = (D3D12_DESCRIPTOR_HEAP_TYPE)Type;
            Desc.NumDescriptors = 16;
            Desc.Flags = (D3D12_DESCRIPTOR_HEAP_FLAGS)Flags;
            hr = Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Heap));
            sprintf_s(Key, "heap_make_%u_%u", Type, Flags);
            if (FAILED(hr))
            {
                Check.Report(Key, "0x%08lX", hr);
                continue;
            }

            {
                D3D12_DESCRIPTOR_HEAP_DESC Got = Heap->GetDesc();
                D3D12_CPU_DESCRIPTOR_HANDLE Cpu = Heap->GetCPUDescriptorHandleForHeapStart();
                D3D12_GPU_DESCRIPTOR_HANDLE Gpu = Heap->GetGPUDescriptorHandleForHeapStart();

                Check.Report(Key, "0x%08lX desc %d %u 0x%x %u cpu %d gpu %d", hr, Got.Type, Got.NumDescriptors,
                             Got.Flags, Got.NodeMask, Cpu.ptr != 0, Gpu.ptr != 0);
            }
        }
    }
}

/* Footprints and allocation info for one description */
static
VOID
TcReportLayout(
    _In_ TwelveCheck& Check,
    _In_ ID3D12Device* Device,
    _In_z_ PCSTR Name,
    _In_ const D3D12_RESOURCE_DESC& Desc)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layouts[8];
    UINT Rows[8];
    UINT64 RowBytes[8];
    UINT64 Total;
    D3D12_RESOURCE_ALLOCATION_INFO Info;
    D3D12_RESOURCE_ALLOCATION_INFO1 Info1[2];
    ComPtr<ID3D12Device4> Device4;
    std::string Text;
    char Key[128];
    UINT Count = Desc.MipLevels * (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Desc.DepthOrArraySize);

    /* Depth stencil and 4:2:0 formats copy as two planes */
    switch (Desc.Format)
    {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_P010:
            Count *= 2;
            break;
        default:
            break;
    }

    if (Count == 0 || Count > 8)
        Count = 8;

    Info = Device->GetResourceAllocationInfo(0, 1, &Desc);
    sprintf_s(Key, "%s_info", Name);
    Check.Report(Key, "%llu %llu", Info.SizeInBytes, Info.Alignment);

    if (SUCCEEDED(Device->QueryInterface(IID_PPV_ARGS(&Device4))))
    {
        D3D12_RESOURCE_DESC Pair[2] = { Desc, Desc };

        memset(Info1, 0xcd, sizeof(Info1));
        Info = Device4->GetResourceAllocationInfo1(0, 2, Pair, Info1);
        sprintf_s(Key, "%s_info1", Name);
        Check.Report(Key, "%llu %llu [%llu %llu %llu] [%llu %llu %llu]", Info.SizeInBytes, Info.Alignment,
                     Info1[0].Offset, Info1[0].Alignment, Info1[0].SizeInBytes, Info1[1].Offset, Info1[1].Alignment,
                     Info1[1].SizeInBytes);
    }

    memset(Layouts, 0xcd, sizeof(Layouts));
    memset(Rows, 0xcd, sizeof(Rows));
    memset(RowBytes, 0xcd, sizeof(RowBytes));
    Total = 0xcdcdcdcd;
    Device->GetCopyableFootprints(&Desc, 0, Count, 256, Layouts, Rows, RowBytes, &Total);
    sprintf_s(Key, "%s_total", Name);
    Check.Report(Key, "%llu", Total);
    for (UINT Index = 0; Index < Count; Index++)
    {
        sprintf_s(Key, "%s_footprint_%u", Name, Index);
        Check.Report(Key, "%llu fmt %u %ux%ux%u pitch %u rows %u bytes %llu", Layouts[Index].Offset,
                     Layouts[Index].Footprint.Format, Layouts[Index].Footprint.Width, Layouts[Index].Footprint.Height,
                     Layouts[Index].Footprint.Depth, Layouts[Index].Footprint.RowPitch, Rows[Index], RowBytes[Index]);
    }
}

/* Committed creation answered without making anything */
static
HRESULT
TcCommitted(
    _In_ ID3D12Device* Device,
    _In_ D3D12_HEAP_TYPE HeapType,
    _In_ D3D12_HEAP_FLAGS HeapFlags,
    _In_ const D3D12_RESOURCE_DESC& Desc,
    _In_ D3D12_RESOURCE_STATES State,
    _In_opt_ const D3D12_CLEAR_VALUE* Clear)
{
    D3D12_HEAP_PROPERTIES Properties = {};

    Properties.Type = HeapType;
    return Device->CreateCommittedResource(&Properties, HeapFlags, &Desc, State, Clear, __uuidof(ID3D12Resource),
                                           nullptr);
}

/* layouts: allocation info, footprints and committed resource checks for many descriptions */
static
VOID
TcCheckLayouts(
    _In_ TwelveCheck& Check)
{
    struct LayoutCase
    {
        PCSTR Name;
        D3D12_RESOURCE_DIMENSION Dimension;
        UINT64 Width;
        UINT Height;
        UINT16 Depth;
        UINT16 Mips;
        DXGI_FORMAT Format;
        UINT Samples;
        D3D12_TEXTURE_LAYOUT Layout;
        D3D12_RESOURCE_FLAGS Flags;
        UINT64 Alignment;
    };
    static const LayoutCase Cases[] =
    {
        { "buf1", D3D12_RESOURCE_DIMENSION_BUFFER, 1, 1, 1, 1, DXGI_FORMAT_UNKNOWN, 1, D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
          D3D12_RESOURCE_FLAG_NONE, 0 },
        { "buf64k", D3D12_RESOURCE_DIMENSION_BUFFER, 65536, 1, 1, 1, DXGI_FORMAT_UNKNOWN, 1,
          D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "buf_uav", D3D12_RESOURCE_DIMENSION_BUFFER, 70000, 1, 1, 1, DXGI_FORMAT_UNKNOWN, 1,
          D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 65536 },
        { "buf_4m", D3D12_RESOURCE_DIMENSION_BUFFER, 4096, 1, 1, 1, DXGI_FORMAT_UNKNOWN, 1,
          D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE, 4194304 },
        { "buf_badalign", D3D12_RESOURCE_DIMENSION_BUFFER, 4096, 1, 1, 1, DXGI_FORMAT_UNKNOWN, 1,
          D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE, 4096 },
        { "buf_unknown_layout", D3D12_RESOURCE_DIMENSION_BUFFER, 4096, 1, 1, 1, DXGI_FORMAT_UNKNOWN, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "buf_format", D3D12_RESOURCE_DIMENSION_BUFFER, 4096, 1, 1, 1, DXGI_FORMAT_R8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "buf_height", D3D12_RESOURCE_DIMENSION_BUFFER, 4096, 2, 1, 1, DXGI_FORMAT_UNKNOWN, 1,
          D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "buf_zero", D3D12_RESOURCE_DIMENSION_BUFFER, 0, 1, 1, 1, DXGI_FORMAT_UNKNOWN, 1,
          D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex1d", D3D12_RESOURCE_DIMENSION_TEXTURE1D, 300, 1, 4, 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_rgba", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 256, 256, 1, 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_small", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 4096 },
        { "tex2d_small_rt", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 4096 },
        { "tex2d_array", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 100, 50, 3, 2, DXGI_FORMAT_B8G8R8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_r8", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 333, 77, 1, 0, DXGI_FORMAT_R8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_bc1", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 130, 66, 1, 0, DXGI_FORMAT_BC1_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_bc7", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 64, 64, 1, 0, DXGI_FORMAT_BC7_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_bc_odd", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 65, 63, 1, 1, DXGI_FORMAT_BC3_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_d24", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 128, 128, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 0 },
        { "tex2d_d32s8", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 64, 32, 1, 1, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 0 },
        { "tex2d_nv12", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 640, 480, 1, 1, DXGI_FORMAT_NV12, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_nv12_odd", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 641, 481, 1, 1, DXGI_FORMAT_NV12, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_p010", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 320, 240, 1, 1, DXGI_FORMAT_P010, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_yuy2", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 320, 240, 1, 1, DXGI_FORMAT_YUY2, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_r32g32b32", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 17, 9, 1, 0, DXGI_FORMAT_R32G32B32_FLOAT, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_msaa4", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 256, 256, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 4,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 0 },
        { "tex2d_msaa4_small", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 4,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 65536 },
        { "tex2d_rowmajor", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_64kb_swizzle", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_toomanymips", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16, 16, 1, 6, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_huge", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16385, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_unknown_format", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16, 16, 1, 1, DXGI_FORMAT_UNKNOWN, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex2d_typeless", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 32, 32, 1, 1, DXGI_FORMAT_R8G8B8A8_TYPELESS, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex3d", D3D12_RESOURCE_DIMENSION_TEXTURE3D, 64, 32, 16, 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex3d_bc1", D3D12_RESOURCE_DIMENSION_TEXTURE3D, 32, 32, 5, 0, DXGI_FORMAT_BC1_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "tex3d_uav", D3D12_RESOURCE_DIMENSION_TEXTURE3D, 32, 32, 32, 1, DXGI_FORMAT_R32_FLOAT, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 0 },
        { "bad_dimension", (D3D12_RESOURCE_DIMENSION)7, 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, 0 },
        { "bad_flags", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN, (D3D12_RESOURCE_FLAGS)0x8000, 0 },
        { "rt_and_ds", D3D12_RESOURCE_DIMENSION_TEXTURE2D, 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
          D3D12_TEXTURE_LAYOUT_UNKNOWN,
          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 0 },
    };
    ComPtr<ID3D12Device> Device;
    D3D12_RESOURCE_DESC Desc;
    char Key[128];

    if (FAILED(Check.MakeDevice(&Device)))
        return;

    for (const LayoutCase& Each : Cases)
    {
        Desc = {};
        Desc.Dimension = Each.Dimension;
        Desc.Alignment = Each.Alignment;
        Desc.Width = Each.Width;
        Desc.Height = Each.Height;
        Desc.DepthOrArraySize = Each.Depth;
        Desc.MipLevels = Each.Mips;
        Desc.Format = Each.Format;
        Desc.SampleDesc.Count = Each.Samples;
        Desc.Layout = Each.Layout;
        Desc.Flags = Each.Flags;
        TcReportLayout(Check, Device.Get(), Each.Name, Desc);

        sprintf_s(Key, "%s_default", Each.Name);
        Check.Report(Key, "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_NONE, Desc,
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr));
        sprintf_s(Key, "%s_upload", Each.Name);
        Check.Report(Key, "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_FLAG_NONE, Desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr));
        sprintf_s(Key, "%s_readback", Each.Name);
        Check.Report(Key, "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_READBACK, D3D12_HEAP_FLAG_NONE, Desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr));
    }

    /* Ranges and outputs of GetCopyableFootprints */
    Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 2;
    Desc.MipLevels = 3;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layouts[8];
        UINT64 Total;

        Total = 7;
        Device->GetCopyableFootprints(&Desc, 4, 3, 0, Layouts, nullptr, nullptr, &Total);
        Check.Report("range_past_end", "%llu", Total);
        Total = 7;
        Device->GetCopyableFootprints(&Desc, 3, 3, 1, Layouts, nullptr, nullptr, &Total);
        Check.Report("range_tail_offset1", "%llu first %llu", Total, Layouts[0].Offset);
        Total = 7;
        Device->GetCopyableFootprints(&Desc, 0, 0, 0, nullptr, nullptr, nullptr, &Total);
        Check.Report("range_empty", "%llu", Total);
        Total = 7;
        Desc.Width = 0;
        memset(Layouts, 0xcd, sizeof(Layouts));
        Device->GetCopyableFootprints(&Desc, 0, 1, 0, Layouts, nullptr, nullptr, &Total);
        Check.Report("range_bad_desc", "%llu %s", Total, TcHex(&Layouts[0], sizeof(Layouts[0])).c_str());
    }

    /* Committed creation rules on a plain texture and buffer */
    {
        static const struct
        {
            PCSTR Name;
            D3D12_HEAP_TYPE Heap;
            UINT HeapFlags;
            UINT State;
        } Rules[] =
        {
            { "default_common", D3D12_HEAP_TYPE_DEFAULT, 0, D3D12_RESOURCE_STATE_COMMON },
            { "default_rt", D3D12_HEAP_TYPE_DEFAULT, 0, D3D12_RESOURCE_STATE_RENDER_TARGET },
            { "default_genread", D3D12_HEAP_TYPE_DEFAULT, 0, D3D12_RESOURCE_STATE_GENERIC_READ },
            { "default_bogus_state", D3D12_HEAP_TYPE_DEFAULT, 0, 0x80000000 },
            { "upload_common", D3D12_HEAP_TYPE_UPLOAD, 0, D3D12_RESOURCE_STATE_COMMON },
            { "upload_copydest", D3D12_HEAP_TYPE_UPLOAD, 0, D3D12_RESOURCE_STATE_COPY_DEST },
            { "readback_genread", D3D12_HEAP_TYPE_READBACK, 0, D3D12_RESOURCE_STATE_GENERIC_READ },
            { "custom", D3D12_HEAP_TYPE_CUSTOM, 0, D3D12_RESOURCE_STATE_COMMON },
            { "gpu_upload", D3D12_HEAP_TYPE_GPU_UPLOAD, 0, D3D12_RESOURCE_STATE_COMMON },
            { "bogus_heap", (D3D12_HEAP_TYPE)9, 0, D3D12_RESOURCE_STATE_COMMON },
            { "flag_shared", D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_SHARED, D3D12_RESOURCE_STATE_COMMON },
            { "flag_deny_buffers", D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_DENY_BUFFERS,
              D3D12_RESOURCE_STATE_COMMON },
            { "flag_deny_textures", D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES,
              D3D12_RESOURCE_STATE_COMMON },
            { "flag_allow_all", D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
              D3D12_RESOURCE_STATE_COMMON },
            { "flag_zeroed", D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
              D3D12_RESOURCE_STATE_COMMON },
            { "flag_bogus", D3D12_HEAP_TYPE_DEFAULT, 0x40000000, D3D12_RESOURCE_STATE_COMMON },
        };
        D3D12_RESOURCE_DESC Buffer = {};
        D3D12_RESOURCE_DESC Texture = {};

        Buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        Buffer.Width = 4096;
        Buffer.Height = 1;
        Buffer.DepthOrArraySize = 1;
        Buffer.MipLevels = 1;
        Buffer.SampleDesc.Count = 1;
        Buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        Texture.Width = 64;
        Texture.Height = 64;
        Texture.DepthOrArraySize = 1;
        Texture.MipLevels = 1;
        Texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Texture.SampleDesc.Count = 1;

        for (const auto& Rule : Rules)
        {
            sprintf_s(Key, "rule_buffer_%s", Rule.Name);
            Check.Report(Key, "0x%08lX", TcCommitted(Device.Get(), Rule.Heap, (D3D12_HEAP_FLAGS)Rule.HeapFlags, Buffer,
                                                      (D3D12_RESOURCE_STATES)Rule.State, nullptr));
            sprintf_s(Key, "rule_texture_%s", Rule.Name);
            Check.Report(Key, "0x%08lX", TcCommitted(Device.Get(), Rule.Heap, (D3D12_HEAP_FLAGS)Rule.HeapFlags,
                                                      Texture, (D3D12_RESOURCE_STATES)Rule.State, nullptr));
        }

        {
            D3D12_CLEAR_VALUE Clear = {};

            Texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            Clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            Check.Report("clear_rt", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                             D3D12_HEAP_FLAG_NONE, Texture,
                                                             D3D12_RESOURCE_STATE_RENDER_TARGET, &Clear));
            Clear.Format = DXGI_FORMAT_R16_FLOAT;
            Check.Report("clear_rt_wrong_format", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                          D3D12_HEAP_FLAG_NONE, Texture,
                                                                          D3D12_RESOURCE_STATE_RENDER_TARGET, &Clear));
            Texture.Flags = D3D12_RESOURCE_FLAG_NONE;
            Clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            Check.Report("clear_plain", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                D3D12_HEAP_FLAG_NONE, Texture,
                                                                D3D12_RESOURCE_STATE_COMMON, &Clear));
            Check.Report("clear_buffer", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                 D3D12_HEAP_FLAG_NONE, Buffer,
                                                                 D3D12_RESOURCE_STATE_COMMON, &Clear));

            /* Typeless resources take any clear format of their family, depth must stay in range */
            Texture.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            Texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            Clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            Check.Report("clear_typeless_rt", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                      D3D12_HEAP_FLAG_NONE, Texture,
                                                                      D3D12_RESOURCE_STATE_RENDER_TARGET, &Clear));
            Clear.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            Check.Report("clear_typeless_value", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                         D3D12_HEAP_FLAG_NONE, Texture,
                                                                         D3D12_RESOURCE_STATE_RENDER_TARGET, &Clear));
            Texture.Format = DXGI_FORMAT_D32_FLOAT;
            Texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            Clear.Format = DXGI_FORMAT_D32_FLOAT;
            Clear.DepthStencil.Depth = 1.0f;
            Check.Report("clear_ds", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                             D3D12_HEAP_FLAG_NONE, Texture,
                                                             D3D12_RESOURCE_STATE_DEPTH_WRITE, &Clear));
            Clear.DepthStencil.Depth = 2.0f;
            Check.Report("clear_ds_far", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                 D3D12_HEAP_FLAG_NONE, Texture,
                                                                 D3D12_RESOURCE_STATE_DEPTH_WRITE, &Clear));
            Check.Report("state_ds_unordered", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                       D3D12_HEAP_FLAG_NONE, Texture,
                                                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr));
            Texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            Texture.Flags = D3D12_RESOURCE_FLAG_NONE;
            Check.Report("state_tex_depth", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                    D3D12_HEAP_FLAG_NONE, Texture,
                                                                    D3D12_RESOURCE_STATE_DEPTH_READ, nullptr));
            Check.Report("state_tex_index", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                    D3D12_HEAP_FLAG_NONE, Texture,
                                                                    D3D12_RESOURCE_STATE_INDEX_BUFFER, nullptr));
            Check.Report("state_tex_mixed", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                    D3D12_HEAP_FLAG_NONE, Texture,
                                                                    (D3D12_RESOURCE_STATES)(D3D12_RESOURCE_STATE_COPY_DEST |
                                                                    D3D12_RESOURCE_STATE_COPY_SOURCE), nullptr));
            Check.Report("state_readback_resolve", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_READBACK,
                                                                           D3D12_HEAP_FLAG_NONE, Buffer,
                                                                           D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr));
            Check.Report("state_upload_resolve", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_UPLOAD,
                                                                         D3D12_HEAP_FLAG_NONE, Buffer,
                                                                         D3D12_RESOURCE_STATE_RESOLVE_SOURCE, nullptr));
            Buffer.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            Check.Report("buffer_rt_default", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                                                      D3D12_HEAP_FLAG_NONE, Buffer,
                                                                      D3D12_RESOURCE_STATE_COMMON, nullptr));
            Check.Report("buffer_rt_upload", "0x%08lX", TcCommitted(Device.Get(), D3D12_HEAP_TYPE_UPLOAD,
                                                                     D3D12_HEAP_FLAG_NONE, Buffer,
                                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr));
            Buffer.Flags = D3D12_RESOURCE_FLAG_NONE;
        }


        {
            D3D12_HEAP_PROPERTIES Properties = {};

            Properties.Type = D3D12_HEAP_TYPE_DEFAULT;

            Properties.CreationNodeMask = 2;
            Check.Report("committed_node2", "0x%08lX", Device->CreateCommittedResource(
                                                           &Properties, D3D12_HEAP_FLAG_NONE, &Buffer,
                                                           D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                           __uuidof(ID3D12Resource), nullptr));
            Properties.CreationNodeMask = 0;
            Properties.VisibleNodeMask = 3;
            Check.Report("committed_visible3", "0x%08lX", Device->CreateCommittedResource(
                                                              &Properties, D3D12_HEAP_FLAG_NONE, &Buffer,
                                                              D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                              __uuidof(ID3D12Resource), nullptr));
            Properties = {};
            Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
            Check.Report("committed_default_page", "0x%08lX", Device->CreateCommittedResource(
                                                                  &Properties, D3D12_HEAP_FLAG_NONE, &Buffer,
                                                                  D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                  __uuidof(ID3D12Resource), nullptr));
            Properties = {};
            Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
            Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
            Check.Report("committed_custom_wc_l0", "0x%08lX", Device->CreateCommittedResource(
                                                                  &Properties, D3D12_HEAP_FLAG_NONE, &Buffer,
                                                                  D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                  __uuidof(ID3D12Resource), nullptr));
            Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L1;
            Check.Report("committed_custom_wc_l1", "0x%08lX", Device->CreateCommittedResource(
                                                                  &Properties, D3D12_HEAP_FLAG_NONE, &Buffer,
                                                                  D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                  __uuidof(ID3D12Resource), nullptr));
            {
                D3D12_HEAP_PROPERTIES Custom;

                for (UINT Type = 0; Type < 7; Type++)
                {
                    memset(&Custom, 0xcd, sizeof(Custom));
                    Custom = Device->GetCustomHeapProperties(0, (D3D12_HEAP_TYPE)Type);
                    sprintf_s(Key, "custom_props_%u", Type);
                    Check.Report(Key, "%d %d %d %u %u", Custom.Type, Custom.CPUPageProperty,
                                 Custom.MemoryPoolPreference, Custom.CreationNodeMask, Custom.VisibleNodeMask);
                }
            }
        }
    }
}

/* A copy of a serialized root signature to patch; offsets are inside the RTS0 part */
struct TcRootBlob
{
    std::vector<BYTE> Bytes;

    BYTE*
    Part()
    {
        UINT Offset;

        memcpy(&Offset, &Bytes[32], sizeof(Offset));
        return &Bytes[Offset + 8];
    }

    VOID
    Put(
        _In_ UINT Offset,
        _In_ UINT Value)
    {
        memcpy(Part() + Offset, &Value, sizeof(Value));
    }

    UINT
    Get(
        _In_ UINT Offset)
    {
        UINT Value;

        memcpy(&Value, Part() + Offset, sizeof(Value));
        return Value;
    }
};

static
BOOLEAN
TcSerializeRoot(
    _In_ TwelveCheck& Check,
    _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& Desc,
    _Out_ TcRootBlob* Out)
{
    ComPtr<ID3DBlob> Blob;
    ComPtr<ID3DBlob> Errors;
    const BYTE* Bytes;

    Out->Bytes.clear();
    if (Check.SerializeVersioned == nullptr || FAILED(Check.SerializeVersioned(&Desc, &Blob, &Errors)))
        return FALSE;
    Bytes = static_cast<const BYTE*>(Blob->GetBufferPointer());
    Out->Bytes.assign(Bytes, Bytes + Blob->GetBufferSize());
    return TRUE;
}

static
VOID
TcReportRoot(
    _In_ TwelveCheck& Check,
    _In_ ID3D12Device* Device,
    _In_z_ PCSTR Key,
    _In_ TcRootBlob& Blob)
{
    ComPtr<ID3D12RootSignature> Signature;
    HRESULT hr;

    hr = Device->CreateRootSignature(0, Blob.Bytes.data(), Blob.Bytes.size(), IID_PPV_ARGS(&Signature));
    Check.Report(Key, "0x%08lX %s", hr, Signature != nullptr ? "object" : "null");
}

static
ULONG
TcRefCount(
    _In_ IUnknown* Object)
{
    Object->AddRef();
    return Object->Release();
}

/* rootsigs: CreateRootSignature argument checks, what reaches the driver and how objects are shared */
static
VOID
TcCheckRootSignatures(
    _In_ TwelveCheck& Check)
{
    ComPtr<ID3D12Device> Device;
    D3D12_DESCRIPTOR_RANGE1 Ranges[2] = {};
    D3D12_ROOT_PARAMETER1 Parameters[3] = {};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC Desc = {};
    TcRootBlob Base;
    TcRootBlob Patched;
    ComPtr<ID3D12RootSignature> First;
    ComPtr<ID3D12RootSignature> Second;
    ComPtr<ID3D12RootSignature> Third;
    ComPtr<IUnknown> Unknown;
    ComPtr<ID3D12Device> Owner;
    void* Raw;
    ULONG DeviceRefs;
    UINT TableOffset;
    UINT RangeOffset;
    HRESULT hr;

    if (FAILED(Check.MakeDevice(&Device)))
    {
        Check.Report("device", "none");
        return;
    }

    Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    Ranges[0].NumDescriptors = 4;
    Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    Ranges[1].NumDescriptors = 2;
    Ranges[1].OffsetInDescriptorsFromTableStart = 4;
    Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters[0].DescriptorTable = { 2, Ranges };
    Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    Parameters[1].Descriptor = { 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE };
    Parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    Parameters[2].Constants = { 1, 0, 4 };
    Desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    Desc.Desc_1_1 = { 3, Parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    if (!TcSerializeRoot(Check, Desc, &Base))
    {
        Check.Report("serialize", "failed");
        return;
    }

    /* The table's range array sits after the three parameter entries and the table header */
    TableOffset = Base.Get(24 + 8);
    RangeOffset = Base.Get(TableOffset + 4);

    DeviceRefs = TcRefCount(Device.Get());
    hr = Device->CreateRootSignature(0, Base.Bytes.data(), Base.Bytes.size(), IID_PPV_ARGS(&First));
    Check.Report("create", "0x%08lX", hr);
    if (FAILED(hr))
        return;
    Check.Report("device_refs", "%ld", (LONG)TcRefCount(Device.Get()) - (LONG)DeviceRefs);
    Check.Report("own_refs", "%lu", TcRefCount(First.Get()));

    hr = Device->CreateRootSignature(0, Base.Bytes.data(), Base.Bytes.size(), IID_PPV_ARGS(&Second));
    Check.Report("same_blob", "0x%08lX same %u refs %lu", hr, First.Get() == Second.Get(), TcRefCount(First.Get()));
    Second.Reset();
    hr = Device->CreateRootSignature(1, Base.Bytes.data(), Base.Bytes.size(), IID_PPV_ARGS(&Second));
    Check.Report("node1", "0x%08lX same %u", hr, First.Get() == Second.Get());
    Second.Reset();
    hr = Device->CreateRootSignature(2, Base.Bytes.data(), Base.Bytes.size(), IID_PPV_ARGS(&Second));
    Check.Report("node2", "0x%08lX %s", hr, Second != nullptr ? "object" : "null");
    hr = Device->CreateRootSignature(3, Base.Bytes.data(), Base.Bytes.size(), IID_PPV_ARGS(&Second));
    Check.Report("node3", "0x%08lX %s", hr, Second != nullptr ? "object" : "null");

    Patched = Base;
    Patched.Bytes[4] ^= 0xFF;
    hr = Device->CreateRootSignature(0, Patched.Bytes.data(), Patched.Bytes.size(), IID_PPV_ARGS(&Second));
    Check.Report("bad_hash", "0x%08lX same %u", hr, First.Get() == Second.Get());
    Second.Reset();

    Check.Report("null_out", "0x%08lX", Device->CreateRootSignature(0, Base.Bytes.data(), Base.Bytes.size(),
                                                                     __uuidof(ID3D12RootSignature), nullptr));
    Raw = reinterpret_cast<void*>(1);
    hr = Device->CreateRootSignature(0, Base.Bytes.data(), Base.Bytes.size(), __uuidof(ID3D12Fence), &Raw);
    Check.Report("bad_riid", "0x%08lX %s", hr, Raw == nullptr ? "null" : "set");
    hr = Device->CreateRootSignature(0, Base.Bytes.data(), Base.Bytes.size(), __uuidof(IUnknown), &Raw);
    Check.Report("unknown_riid", "0x%08lX same %u", hr, Raw == static_cast<void*>(First.Get()));
    if (SUCCEEDED(hr))
        static_cast<IUnknown*>(Raw)->Release();

    Check.Report("null_blob", "0x%08lX", Device->CreateRootSignature(0, nullptr, 0, IID_PPV_ARGS(&Second)));
    Check.Report("short_length", "0x%08lX", Device->CreateRootSignature(0, Base.Bytes.data(), Base.Bytes.size() - 1,
                                                                         IID_PPV_ARGS(&Second)));
    Check.Report("long_length", "0x%08lX", Device->CreateRootSignature(0, Base.Bytes.data(), Base.Bytes.size() + 1,
                                                                        IID_PPV_ARGS(&Second)));

    Patched = Base;
    Patched.Bytes[36] = 'X';
    TcReportRoot(Check, Device.Get(), "no_part", Patched);
    Patched = Base;
    Patched.Bytes[0] = 'Q';
    TcReportRoot(Check, Device.Get(), "bad_magic", Patched);
    Patched = Base;
    Patched.Put(0, 4);
    TcReportRoot(Check, Device.Get(), "version4", Patched);
    Patched = Base;
    Patched.Put(0, 0);
    TcReportRoot(Check, Device.Get(), "version0", Patched);
    Patched = Base;
    Patched.Put(24, 7);
    TcReportRoot(Check, Device.Get(), "param_type7", Patched);
    Patched = Base;
    Patched.Put(24 + 4, 9);
    TcReportRoot(Check, Device.Get(), "visibility9", Patched);
    Patched = Base;
    Patched.Put(RangeOffset, 4);
    TcReportRoot(Check, Device.Get(), "range_type4", Patched);
    Patched = Base;
    Patched.Put(RangeOffset + 4, 0);
    TcReportRoot(Check, Device.Get(), "range_empty", Patched);
    Patched = Base;
    Patched.Put(RangeOffset + 24, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
    TcReportRoot(Check, Device.Get(), "range_overlap", Patched);

    for (UINT Bit = 0; Bit < 32; Bit++)
    {
        CHAR Key[64];

        Patched = Base;
        Patched.Put(RangeOffset + 16, 1u << Bit);
        sprintf_s(Key, "range_flag%u", Bit);
        TcReportRoot(Check, Device.Get(), Key, Patched);

        Patched = Base;
        Patched.Put(Base.Get(24 + 12 + 8) + 8, 1u << Bit);
        sprintf_s(Key, "descriptor_flag%u", Bit);
        TcReportRoot(Check, Device.Get(), Key, Patched);

        Patched = Base;
        Patched.Put(20, 1u << Bit);
        sprintf_s(Key, "root_flag%u", Bit);
        TcReportRoot(Check, Device.Get(), Key, Patched);
    }

    /* Root argument space: the table costs 1, the CBV 2 */
    for (UINT Values : { 61u, 62u })
    {
        CHAR Key[32];

        Patched = Base;
        Patched.Put(Base.Get(24 + 24 + 8) + 8, Values);
        sprintf_s(Key, "constants%u", Values);
        TcReportRoot(Check, Device.Get(), Key, Patched);
    }

    Patched = Base;
    Patched.Put(Base.Get(24 + 12 + 8), 1);
    Patched.Put(Base.Get(24 + 24 + 8), 1);
    TcReportRoot(Check, Device.Get(), "cbv_overlap", Patched);

    Patched = Base;
    Patched.Put(20, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
    TcReportRoot(Check, Device.Get(), "local", Patched);

    for (const IID* Iid : { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild),
                            &__uuidof(ID3D12Pageable), &__uuidof(ID3D12RootSignature),
                            &__uuidof(ID3D12RootSignatureDeserializer) })
    {
        Unknown.Reset();
        hr = First->QueryInterface(*Iid, &Unknown);
        Check.Report("qi", "0x%08lX", hr);
    }

    hr = First->GetDevice(IID_PPV_ARGS(&Owner));
    Check.Report("get_device", "0x%08lX same %u", hr, Owner.Get() == Device.Get());
    Owner.Reset();

    /* A released root signature is made again, not found */
    Second.Reset();
    Third.Reset();
    First.Reset();
    hr = Device->CreateRootSignature(0, Base.Bytes.data(), Base.Bytes.size(), IID_PPV_ARGS(&First));
    Check.Report("recreate", "0x%08lX refs %lu", hr, First != nullptr ? TcRefCount(First.Get()) : 0);
    First.Reset();
    Check.Report("device_refs_after", "%ld", (LONG)TcRefCount(Device.Get()) - (LONG)DeviceRefs);
}

/* A graphics desc like the one every device makes for itself: position only, default states */
static
VOID
TcDefaultGraphicsDesc(
    _In_ ID3D12RootSignature* Root,
    _Out_ D3D12_GRAPHICS_PIPELINE_STATE_DESC* Desc)
{
    ZeroMemory(Desc, sizeof(*Desc));
    Desc->pRootSignature = Root;
    Desc->VS = { RxInternalVertexDxil, sizeof(RxInternalVertexDxil) };
    for (D3D12_RENDER_TARGET_BLEND_DESC& Target : Desc->BlendState.RenderTarget)
    {
        Target.SrcBlend = D3D12_BLEND_ONE;
        Target.DestBlend = D3D12_BLEND_ZERO;
        Target.BlendOp = D3D12_BLEND_OP_ADD;
        Target.SrcBlendAlpha = D3D12_BLEND_ONE;
        Target.DestBlendAlpha = D3D12_BLEND_ZERO;
        Target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        Target.LogicOp = D3D12_LOGIC_OP_NOOP;
        Target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    Desc->SampleMask = UINT_MAX;
    Desc->RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    Desc->RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    Desc->RasterizerState.DepthClipEnable = TRUE;
    Desc->DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    Desc->DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    Desc->DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    Desc->DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    for (D3D12_DEPTH_STENCILOP_DESC* Face : { &Desc->DepthStencilState.FrontFace, &Desc->DepthStencilState.BackFace })
    {
        Face->StencilFailOp = D3D12_STENCIL_OP_KEEP;
        Face->StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        Face->StencilPassOp = D3D12_STENCIL_OP_KEEP;
        Face->StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    }
    Desc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    Desc->SampleDesc.Count = 1;
}

static
VOID
TcReportGraphics(
    _In_ TwelveCheck& Check,
    _In_ ID3D12Device* Device,
    _In_z_ PCSTR Key,
    _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC& Desc)
{
    ComPtr<ID3D12PipelineState> State;
    HRESULT hr;

    hr = Device->CreateGraphicsPipelineState(&Desc, IID_PPV_ARGS(&State));
    Check.Report(Key, "0x%08lX %s", hr, State != nullptr ? "object" : "null");
}

/* A vertex shader reading four of the elements below; the normal is left unread */
static const CHAR TcLayoutShader[] =
    "struct In { float4 Color : COLOR; float3 Position : POSITION; float2 Uv : TEXCOORD0; uint Id : BLENDINDICES; };\n"
    "float4 main(In I) : SV_Position { return float4(I.Position + I.Color.xyz, I.Uv.x + (float)I.Id); }\n";

static const D3D12_INPUT_ELEMENT_DESC TcLayout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 4, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 2 },
    { "blendindices", 0, DXGI_FORMAT_R8_UINT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 2 },
};

/* Input layout rules, each case one change to a layout the shader takes */
static
VOID
TcCheckInputLayouts(
    _In_ TwelveCheck& Check,
    _In_ ID3D12Device* Device,
    _In_ ID3D12RootSignature* Root)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc;
    D3D12_INPUT_ELEMENT_DESC Elements[D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT + 1];
    ComPtr<ID3DBlob> Vertex;
    ComPtr<ID3DBlob> Errors;
    ComPtr<ID3DBlob> Blob;
    ComPtr<ID3D12RootSignature> Assembler;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC RootDesc = {};
    static const struct
    {
        PCSTR Name;
        UINT Element;
        UINT Field;
        UINT Value;
    } Cases[] =
    {
        { "layout_slot_class", 4, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
        { "layout_slot_rate", 4, 1, 3 },
        { "layout_vertex_rate", 0, 1, 1 },
        { "layout_bad_class", 0, 0, 2 },
        { "layout_slot32", 2, 2, 32 },
        { "layout_slot31", 2, 2, 31 },
        { "layout_misaligned", 3, 3, 5 },
        { "layout_aligned", 3, 3, 8 },
        { "layout_bad_format", 2, 4, DXGI_FORMAT_BC1_UNORM },
        { "layout_typeless_format", 2, 4, DXGI_FORMAT_R16G16_TYPELESS },
        { "layout_duplicate", 1, 5, 0 },
        { "layout_trailing_digit", 1, 6, 0 },
    };

    if (FAILED(D3DCompile(TcLayoutShader, strlen(TcLayoutShader), "layout", nullptr, nullptr, "main", "vs_5_0", 0, 0,
                          &Vertex, &Errors)))
    {
        Check.Report("layout_shader", "none");
        return;
    }

    TcDefaultGraphicsDesc(Root, &Desc);
    Desc.VS = { Vertex->GetBufferPointer(), Vertex->GetBufferSize() };
    Desc.InputLayout = { TcLayout, _countof(TcLayout) };
    TcReportGraphics(Check, Device, "layout_no_assembler_flag", Desc);

    /* The remaining cases use a root signature that lets the input assembler read layouts */
    RootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    RootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (Check.SerializeVersioned == nullptr || FAILED(Check.SerializeVersioned(&RootDesc, &Blob, &Errors)) ||
        FAILED(Device->CreateRootSignature(0, Blob->GetBufferPointer(), Blob->GetBufferSize(), IID_PPV_ARGS(&Assembler))))
    {
        Check.Report("layout_root", "none");
        return;
    }
    Desc.pRootSignature = Assembler.Get();
    TcReportGraphics(Check, Device, "layout", Desc);

    for (const auto& Case : Cases)
    {
        memcpy(Elements, TcLayout, sizeof(TcLayout));
        switch (Case.Field)
        {
            case 0:
                Elements[Case.Element].InputSlotClass = (D3D12_INPUT_CLASSIFICATION)Case.Value;
                break;
            case 1:
                Elements[Case.Element].InstanceDataStepRate = Case.Value;
                break;
            case 2:
                Elements[Case.Element].InputSlot = Case.Value;
                break;
            case 3:
                Elements[Case.Element].AlignedByteOffset = Case.Value;
                break;
            case 4:
                Elements[Case.Element].Format = (DXGI_FORMAT)Case.Value;
                break;
            case 5:
                Elements[Case.Element].SemanticName = "POSITION";
                break;
            default:
                Elements[Case.Element].SemanticName = "NORMAL1";
                break;
        }
        Desc.InputLayout = { Elements, _countof(TcLayout) };
        TcReportGraphics(Check, Device, Case.Name, Desc);
    }

    /* Every shader input must be fed; elements the shader does not read are allowed */
    Desc.InputLayout = { TcLayout, 3 };
    TcReportGraphics(Check, Device, "layout_unfed", Desc);
    Desc.InputLayout = { TcLayout, 0 };
    TcReportGraphics(Check, Device, "layout_empty", Desc);
    Desc.VS = { RxInternalVertexDxil, sizeof(RxInternalVertexDxil) };
    Desc.InputLayout = { TcLayout + 1, 1 };
    TcReportGraphics(Check, Device, "layout_unread", Desc);

    for (UINT Index = 0; Index < _countof(Elements); Index++)
    {
        Elements[Index] = TcLayout[1];
        Elements[Index].SemanticIndex = Index;
    }
    Desc.InputLayout = { Elements, D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT };
    TcReportGraphics(Check, Device, "layout_most", Desc);
    Desc.InputLayout = { Elements, _countof(Elements) };
    TcReportGraphics(Check, Device, "layout_too_many", Desc);
}

static
VOID
TcReportCompute(
    _In_ TwelveCheck& Check,
    _In_ ID3D12Device* Device,
    _In_z_ PCSTR Key,
    _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC& Desc)
{
    ComPtr<ID3D12PipelineState> State;
    HRESULT hr;

    hr = Device->CreateComputePipelineState(&Desc, IID_PPV_ARGS(&State));
    Check.Report(Key, "0x%08lX %s", hr, State != nullptr ? "object" : "null");
}

/* pipelines: graphics and compute pipeline creation from fixed descs */
static
VOID
TcCheckPipelines(
    _In_ TwelveCheck& Check)
{
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12RootSignature> Root;
    ComPtr<ID3D12PipelineState> First;
    ComPtr<ID3D12PipelineState> Second;
    ComPtr<IUnknown> Unknown;
    ComPtr<ID3D12Device> Owner;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC RootDesc = {};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC Graphics;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC Changed;
    D3D12_COMPUTE_PIPELINE_STATE_DESC Compute = {};
    TcRootBlob Blob;
    void* Raw;
    ULONG DeviceRefs;
    HRESULT hr;

    if (FAILED(Check.MakeDevice(&Device)))
    {
        Check.Report("device", "none");
        return;
    }

    RootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (!TcSerializeRoot(Check, RootDesc, &Blob) ||
        FAILED(Device->CreateRootSignature(0, Blob.Bytes.data(), Blob.Bytes.size(), IID_PPV_ARGS(&Root))))
    {
        Check.Report("root", "failed");
        return;
    }

    TcDefaultGraphicsDesc(Root.Get(), &Graphics);
    DeviceRefs = TcRefCount(Device.Get());
    hr = Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&First));
    Check.Report("graphics", "0x%08lX", hr);
    if (FAILED(hr))
        return;
    Check.Report("device_refs", "%ld", (LONG)TcRefCount(Device.Get()) - (LONG)DeviceRefs);
    Check.Report("root_refs", "%lu", TcRefCount(Root.Get()));
    hr = Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&Second));
    Check.Report("graphics_again", "0x%08lX same %u", hr, First.Get() == Second.Get());
    Second.Reset();

    for (const IID* Iid : { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild),
                            &__uuidof(ID3D12Pageable), &__uuidof(ID3D12PipelineState),
                            &__uuidof(ID3D12RootSignature) })
    {
        Unknown.Reset();
        Check.Report("qi", "0x%08lX", First->QueryInterface(*Iid, &Unknown));
    }
    hr = First->GetDevice(IID_PPV_ARGS(&Owner));
    Check.Report("get_device", "0x%08lX same %u", hr, Owner.Get() == Device.Get());
    Owner.Reset();

    Check.Report("null_out", "0x%08lX", Device->CreateGraphicsPipelineState(&Graphics, __uuidof(ID3D12PipelineState),
                                                                             nullptr));
    Raw = reinterpret_cast<void*>(1);
    hr = Device->CreateGraphicsPipelineState(&Graphics, __uuidof(ID3D12Fence), &Raw);
    Check.Report("bad_riid", "0x%08lX %s", hr, Raw == nullptr ? "null" : "set");

    Changed = Graphics;
    Changed.VS = {};
    TcReportGraphics(Check, Device.Get(), "no_vs", Changed);
    Changed = Graphics;
    Changed.pRootSignature = nullptr;
    TcReportGraphics(Check, Device.Get(), "no_root", Changed);
    Changed = Graphics;
    Changed.VS = { RxInternalVertexDxbc, sizeof(RxInternalVertexDxbc) };
    TcReportGraphics(Check, Device.Get(), "dxbc_vs", Changed);
    Changed = Graphics;
    Changed.NodeMask = 2;
    TcReportGraphics(Check, Device.Get(), "node2", Changed);
    Changed = Graphics;
    Changed.SampleDesc.Count = 0;
    TcReportGraphics(Check, Device.Get(), "samples0", Changed);
    Changed = Graphics;
    Changed.SampleMask = 0;
    TcReportGraphics(Check, Device.Get(), "sample_mask0", Changed);
    Changed = Graphics;
    Changed.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    Changed.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    TcReportGraphics(Check, Device.Get(), "raster", Changed);
    Changed = Graphics;
    Changed.BlendState.RenderTarget[0].BlendEnable = TRUE;
    Changed.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    Changed.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    TcReportGraphics(Check, Device.Get(), "blend", Changed);
    Changed = Graphics;
    Changed.DepthStencilState.DepthEnable = TRUE;
    Changed.DepthStencilState.StencilEnable = TRUE;
    Changed.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
    TcReportGraphics(Check, Device.Get(), "depth_stencil", Changed);

    Compute.pRootSignature = Root.Get();
    Compute.CS = { RxInternalComputeDxil, sizeof(RxInternalComputeDxil) };
    TcReportCompute(Check, Device.Get(), "compute", Compute);
    Compute.CS = { RxInternalComputeDxbc, sizeof(RxInternalComputeDxbc) };
    TcReportCompute(Check, Device.Get(), "compute_dxbc", Compute);
    Compute.CS = {};
    TcReportCompute(Check, Device.Get(), "compute_none", Compute);
    Compute.CS = { RxInternalComputeDxil, sizeof(RxInternalComputeDxil) };
    Compute.pRootSignature = nullptr;
    TcReportCompute(Check, Device.Get(), "compute_no_root", Compute);
    Compute.pRootSignature = Root.Get();
    Compute.NodeMask = 2;
    TcReportCompute(Check, Device.Get(), "compute_node2", Compute);
    Compute.NodeMask = 0;
    Check.Report("compute_null_out", "0x%08lX", Device->CreateComputePipelineState(&Compute,
                                                                                    __uuidof(ID3D12PipelineState),
                                                                                    nullptr));

    TcCheckInputLayouts(Check, Device.Get(), Root.Get());

    First.Reset();
    Check.Report("root_refs_after", "%lu", TcRefCount(Root.Get()));
    Check.Report("device_refs_after", "%ld", (LONG)TcRefCount(Device.Get()) - (LONG)DeviceRefs);
}

/* Whether a device child's IUnknown stands apart from its API interfaces, and is one object with them */
static
VOID
TcReportIdentity(
    _In_ TwelveCheck& Check,
    _In_z_ PCSTR Name,
    _In_ HRESULT Created,
    _In_opt_ ID3D12DeviceChild* Child)
{
    ComPtr<IUnknown> Unknown;
    ComPtr<IUnknown> Again;
    ComPtr<ID3D12DeviceChild> Back;
    ComPtr<ID3D12Object> Object;
    ULONG Before;
    ULONG Held;
    CHAR Key[64];

    sprintf_s(Key, "%s", Name);
    if (Child == nullptr)
    {
        Check.Report(Key, "0x%08lX none", Created);
        return;
    }

    Child->QueryInterface(IID_PPV_ARGS(&Unknown));
    Child->QueryInterface(IID_PPV_ARGS(&Again));
    Child->QueryInterface(IID_PPV_ARGS(&Object));
    Unknown->QueryInterface(IID_PPV_ARGS(&Back));
    Before = TcRefCount(Child);
    Unknown->AddRef();
    Held = TcRefCount(Child);
    Unknown->Release();

    Check.Report(Key, "0x%08lX apart %u stable %u object %u back %u shared %u", Created,
                 static_cast<void*>(Unknown.Get()) != static_cast<void*>(Child), Unknown.Get() == Again.Get(),
                 static_cast<void*>(Object.Get()) == static_cast<void*>(Child), Back.Get() == Child, Held == Before + 1);
}

/*
 * Which interface pointers of a child are the same: each face is numbered by the first face that gave
 * the same pointer, with 0 for the pointer creation handed out. A face the child lacks prints as x.
 */
static
VOID
TcReportFaces(
    _In_ TwelveCheck& Check,
    _In_z_ PCSTR Name,
    _In_opt_ IUnknown* Created,
    _In_reads_(Count) const IID* const* Faces,
    _In_ UINT Count)
{
    void* Seen[16] = {};
    std::string Text;
    CHAR Key[64];

    if (Created == nullptr)
        return;

    Seen[0] = Created;
    for (UINT Index = 0; Index < Count && Index + 1 < _countof(Seen); Index++)
    {
        IUnknown* Face = nullptr;
        UINT Class = Index + 1;

        if (FAILED(Created->QueryInterface(*Faces[Index], reinterpret_cast<void**>(&Face))))
        {
            Text += " x";
            continue;
        }
        Seen[Index + 1] = Face;
        for (UINT Earlier = 0; Earlier <= Index; Earlier++)
        {
            if (Seen[Earlier] == Face)
            {
                Class = Earlier;
                break;
            }
        }
        Face->Release();
        Text += " " + std::to_string(Class);
    }

    sprintf_s(Key, "%s_faces", Name);
    Check.Report(Key, "%s", Text.c_str() + 1);
}

/* identity: the IUnknown each kind of device child hands out */
static
VOID
TcCheckIdentity(
    _In_ TwelveCheck& Check)
{
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> List;
    ComPtr<ID3D12Fence> Fence;
    ComPtr<ID3D12DescriptorHeap> DescriptorHeap;
    ComPtr<ID3D12Resource> Committed;
    ComPtr<ID3D12Heap> Heap;
    ComPtr<ID3D12Resource> Placed;
    ComPtr<ID3D12QueryHeap> QueryHeap;
    ComPtr<ID3D12CommandSignature> Signature;
    ComPtr<ID3D12RootSignature> Root;
    ComPtr<ID3D12PipelineState> Pipeline;
    ComPtr<ID3D12Device1> Device1;
    ComPtr<ID3D12PipelineLibrary> Library;
    ComPtr<ID3DBlob> Blob;
    ComPtr<ID3DBlob> Errors;
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
    D3D12_HEAP_PROPERTIES Properties = {};
    D3D12_RESOURCE_DESC Buffer = {};
    D3D12_HEAP_DESC Memory = {};
    D3D12_QUERY_HEAP_DESC Queries = {};
    D3D12_INDIRECT_ARGUMENT_DESC Argument = {};
    D3D12_COMMAND_SIGNATURE_DESC SignatureDesc = {};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC RootDesc = {};
    D3D12_COMPUTE_PIPELINE_STATE_DESC Compute = {};
    HRESULT hr;

    if (FAILED(Check.MakeDevice(&Device)))
    {
        Check.Report("device", "none");
        return;
    }

    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue));
    TcReportIdentity(Check, "queue", hr, Queue.Get());
    {
        static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12CommandQueue) };

        TcReportFaces(Check, "queue", Queue.Get(), Faces, _countof(Faces));
    }

    hr = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Allocator));
    TcReportIdentity(Check, "allocator", hr, Allocator.Get());
    {
        static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12CommandAllocator) };

        TcReportFaces(Check, "allocator", Allocator.Get(), Faces, _countof(Faces));
    }
    if (Allocator != nullptr)
    {
        hr = Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS(&List));
        TcReportIdentity(Check, "list", hr, List.Get());
        {
            static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12CommandList), &__uuidof(ID3D12GraphicsCommandList), &__uuidof(ID3D12GraphicsCommandList1), &__uuidof(ID3D12GraphicsCommandList4), &__uuidof(ID3D12GraphicsCommandList10) };

            TcReportFaces(Check, "list", List.Get(), Faces, _countof(Faces));
        }
        if (List != nullptr)
            List->Close();
    }

    hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
    TcReportIdentity(Check, "fence", hr, Fence.Get());
    {
        static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12Fence), &__uuidof(ID3D12Fence1) };

        TcReportFaces(Check, "fence", Fence.Get(), Faces, _countof(Faces));
    }

    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 4;
    hr = Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&DescriptorHeap));
    TcReportIdentity(Check, "descriptor_heap", hr, DescriptorHeap.Get());
    {
        static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12DescriptorHeap) };

        TcReportFaces(Check, "descriptor_heap", DescriptorHeap.Get(), Faces, _countof(Faces));
    }

    Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    Buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Buffer.Width = 65536;
    Buffer.Height = 1;
    Buffer.DepthOrArraySize = 1;
    Buffer.MipLevels = 1;
    Buffer.SampleDesc.Count = 1;
    Buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = Device->CreateCommittedResource(&Properties, D3D12_HEAP_FLAG_NONE, &Buffer, D3D12_RESOURCE_STATE_GENERIC_READ,
                                         nullptr, IID_PPV_ARGS(&Committed));
    TcReportIdentity(Check, "committed", hr, Committed.Get());
    {
        static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12Resource), &__uuidof(ID3D12Resource1), &__uuidof(ID3D12Resource2) };

        TcReportFaces(Check, "committed", Committed.Get(), Faces, _countof(Faces));
    }

    Memory.SizeInBytes = 65536;
    Memory.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    Memory.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    hr = Device->CreateHeap(&Memory, IID_PPV_ARGS(&Heap));
    TcReportIdentity(Check, "heap", hr, Heap.Get());
    {
        static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12Heap), &__uuidof(ID3D12Heap1) };

        TcReportFaces(Check, "heap", Heap.Get(), Faces, _countof(Faces));
    }
    if (Heap != nullptr)
    {
        hr = Device->CreatePlacedResource(Heap.Get(), 0, &Buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                          IID_PPV_ARGS(&Placed));
        TcReportIdentity(Check, "placed", hr, Placed.Get());
        {
            static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12Resource), &__uuidof(ID3D12Resource2) };

            TcReportFaces(Check, "placed", Placed.Get(), Faces, _countof(Faces));
        }
    }

    Queries.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    Queries.Count = 4;
    hr = Device->CreateQueryHeap(&Queries, IID_PPV_ARGS(&QueryHeap));
    TcReportIdentity(Check, "query_heap", hr, QueryHeap.Get());
    {
        static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12QueryHeap) };

        TcReportFaces(Check, "query_heap", QueryHeap.Get(), Faces, _countof(Faces));
    }

    Argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    SignatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    SignatureDesc.NumArgumentDescs = 1;
    SignatureDesc.pArgumentDescs = &Argument;
    hr = Device->CreateCommandSignature(&SignatureDesc, nullptr, IID_PPV_ARGS(&Signature));
    TcReportIdentity(Check, "command_signature", hr, Signature.Get());
    {
        static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12CommandSignature) };

        TcReportFaces(Check, "command_signature", Signature.Get(), Faces, _countof(Faces));
    }

    RootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (Check.SerializeVersioned != nullptr && SUCCEEDED(Check.SerializeVersioned(&RootDesc, &Blob, &Errors)))
    {
        hr = Device->CreateRootSignature(0, Blob->GetBufferPointer(), Blob->GetBufferSize(), IID_PPV_ARGS(&Root));
        TcReportIdentity(Check, "root_signature", hr, Root.Get());
        {
            static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12RootSignature) };

            TcReportFaces(Check, "root_signature", Root.Get(), Faces, _countof(Faces));
        }
    }
    if (Root != nullptr)
    {
        Compute.pRootSignature = Root.Get();
        Compute.CS = { RxInternalComputeDxil, sizeof(RxInternalComputeDxil) };
        hr = Device->CreateComputePipelineState(&Compute, IID_PPV_ARGS(&Pipeline));
        TcReportIdentity(Check, "pipeline", hr, Pipeline.Get());
        {
            static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12PipelineState) };

            TcReportFaces(Check, "pipeline", Pipeline.Get(), Faces, _countof(Faces));
        }
    }

    if (SUCCEEDED(Device.As(&Device1)))
    {
        hr = Device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&Library));
        TcReportIdentity(Check, "pipeline_library", hr, Library.Get());
        {
            static const IID* const Faces[] = { &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12Pageable), &__uuidof(ID3D12PipelineLibrary), &__uuidof(ID3D12PipelineLibrary1) };

            TcReportFaces(Check, "pipeline_library", Library.Get(), Faces, _countof(Faces));
        }
    }
}

/* A heap's description and what its second interface answers */
static
VOID
TcReportHeap(
    _In_ TwelveCheck& Check,
    _In_z_ PCSTR Name,
    _In_ ID3D12Heap* Heap)
{
    ComPtr<ID3D12Heap1> Heap1;
    ComPtr<IUnknown> Session;
    D3D12_HEAP_DESC Desc = Heap->GetDesc();
    CHAR Key[96];
    HRESULT hr = E_NOINTERFACE;

    if (SUCCEEDED(Heap->QueryInterface(IID_PPV_ARGS(&Heap1))))
        hr = Heap1->GetProtectedResourceSession(IID_PPV_ARGS(&Session));

    sprintf_s(Key, "%s_desc", Name);
    Check.Report(Key, "size %llu type %d page %d pool %d nodes %u:%u align %llu flags 0x%x session 0x%08lX %d",
                 Desc.SizeInBytes, Desc.Properties.Type, Desc.Properties.CPUPageProperty,
                 Desc.Properties.MemoryPoolPreference, Desc.Properties.CreationNodeMask,
                 Desc.Properties.VisibleNodeMask, Desc.Alignment, Desc.Flags, hr, Session == nullptr);
}

/* What a placed resource says about itself and where it sits */
static
VOID
TcReportPlaced(
    _In_ TwelveCheck& Check,
    _In_z_ PCSTR Name,
    _In_ ID3D12Resource* Resource,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS Base)
{
    D3D12_RESOURCE_DESC Desc = Resource->GetDesc();
    D3D12_HEAP_PROPERTIES Properties = {};
    D3D12_HEAP_FLAGS Flags = D3D12_HEAP_FLAG_NONE;
    D3D12_GPU_VIRTUAL_ADDRESS Address = Resource->GetGPUVirtualAddress();
    CHAR Key[96];
    HRESULT hr;

    hr = Resource->GetHeapProperties(&Properties, &Flags);
    sprintf_s(Key, "%s_desc", Name);
    Check.Report(Key, "align %llu mips %u heap 0x%08lX type %d page %d pool %d nodes %u:%u flags 0x%x va %s %lld",
                 Desc.Alignment, Desc.MipLevels, hr, Properties.Type, Properties.CPUPageProperty,
                 Properties.MemoryPoolPreference, Properties.CreationNodeMask, Properties.VisibleNodeMask, Flags,
                 Address == 0 ? "0" : "set", Address == 0 || Base == 0 ? 0ll : (LONGLONG)(Address - Base));
}

/* placed: heaps, their checks and resources placed in them */
static
VOID
TcCheckPlaced(
    _In_ TwelveCheck& Check)
{
    static const struct
    {
        PCSTR Name;
        UINT64 Size;
        D3D12_HEAP_TYPE Type;
        D3D12_CPU_PAGE_PROPERTY Page;
        D3D12_MEMORY_POOL Pool;
        UINT Creation;
        UINT Visible;
        UINT64 Alignment;
        UINT Flags;
    } Heaps[] =
    {
        { "default", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "odd_size", 100000, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "tiny", 1, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "zero", 0, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "huge", 0xFFFFFFFFFFC00000ull, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "huge_upload", 0x100000000ull, D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "align_4k", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 4096, 0 },
        { "align_64k", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 65536, 0 },
        { "align_4m", 4194304, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 4194304, 0 },
        { "align_both", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 4194304 | 65536, 0 },
        { "buffers_only", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS },
        { "rt_ds_only", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES },
        { "non_rt_ds_only", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES },
        { "deny_buffers", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_DENY_BUFFERS },
        { "deny_all", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES },
        { "display", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_ALLOW_DISPLAY },
        { "shared", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_SHARED },
        { "shared_upload", 65536, D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_SHARED },
        { "cross_adapter", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER },
        { "cross_adapter_deny", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER | D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS },
        { "write_watch_default", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH },
        { "write_watch_upload", 65536, D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH },
        { "atomics_default", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS },
        { "atomics_upload", 65536, D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS },
        { "unknown_flag", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0x10 },
        { "create_not_zeroed", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED },
        { "upload", 65536, D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "readback", 65536, D3D12_HEAP_TYPE_READBACK, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "gpu_upload", 65536, D3D12_HEAP_TYPE_GPU_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "type_zero", 65536, (D3D12_HEAP_TYPE)0, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "type_bogus", 65536, (D3D12_HEAP_TYPE)9, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "default_with_page", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_UNKNOWN, 0, 0, 0, 0 },
        { "custom_wb_l0", 65536, D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0, 0, 0, 0, 0 },
        { "custom_na_l1", 65536, D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE, D3D12_MEMORY_POOL_L1, 0, 0, 0, 0 },
        { "custom_wc_l1", 65536, D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_MEMORY_POOL_L1, 0, 0, 0, 0 },
        { "custom_unknown", 65536, D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_L0, 0, 0, 0, 0 },
        { "nodes_1_1", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1, 0, 0 },
        { "nodes_2_2", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 2, 2, 0, 0 },
        { "nodes_1_0", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 0, 0, 0 },
        { "nodes_1_3", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 3, 0, 0 },
        { "nodes_3_3", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 3, 3, 0, 0 },
        { "nodes_0_2", 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 2, 0, 0 },
    };
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12Device4> Device4;
    ComPtr<ID3D12Device8> Device8;
    ComPtr<ID3D12Device10> Device10;
    ComPtr<ID3D12Heap> Buffers;
    ComPtr<ID3D12Heap> Upload;
    ComPtr<ID3D12Heap> Targets;
    ComPtr<ID3D12Heap> Textures;
    ComPtr<ID3D12Resource> First;
    ComPtr<ID3D12Resource> Second;
    ComPtr<ID3D12Resource> Mapped[2];
    D3D12_HEAP_DESC HeapDesc = {};
    D3D12_RESOURCE_DESC Buffer = {};
    D3D12_RESOURCE_DESC Texture = {};
    D3D12_GPU_VIRTUAL_ADDRESS Base = 0;
    HRESULT hr;

    if (FAILED(Check.MakeDevice(&Device)))
    {
        Check.Report("device", "none");
        return;
    }

    /* A NULL heap desc is left out: the system reads its node mask before checking it */
    for (const auto& Each : Heaps)
    {
        ComPtr<ID3D12Heap> Heap;
        CHAR Key[96];

        HeapDesc = {};
        HeapDesc.SizeInBytes = Each.Size;
        HeapDesc.Properties.Type = Each.Type;
        HeapDesc.Properties.CPUPageProperty = Each.Page;
        HeapDesc.Properties.MemoryPoolPreference = Each.Pool;
        HeapDesc.Properties.CreationNodeMask = Each.Creation;
        HeapDesc.Properties.VisibleNodeMask = Each.Visible;
        HeapDesc.Alignment = Each.Alignment;
        HeapDesc.Flags = (D3D12_HEAP_FLAGS)Each.Flags;

        sprintf_s(Key, "%s_check", Each.Name);
        Check.Report(Key, "0x%08lX", Device->CreateHeap(&HeapDesc, __uuidof(ID3D12Heap), nullptr));
        hr = Device->CreateHeap(&HeapDesc, IID_PPV_ARGS(&Heap));
        Check.Report(Each.Name, "0x%08lX", hr);
        if (SUCCEEDED(hr))
            TcReportHeap(Check, Each.Name, Heap.Get());
    }

    HeapDesc = {};
    HeapDesc.SizeInBytes = 4 * 65536;
    HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    Check.Report("buffers", "0x%08lX", Device->CreateHeap(&HeapDesc, IID_PPV_ARGS(&Buffers)));
    HeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    Check.Report("upload_heap", "0x%08lX", Device->CreateHeap(&HeapDesc, IID_PPV_ARGS(&Upload)));
    HeapDesc.SizeInBytes = 8 * 65536;
    HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    Check.Report("targets", "0x%08lX", Device->CreateHeap(&HeapDesc, IID_PPV_ARGS(&Targets)));
    HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    Check.Report("textures", "0x%08lX", Device->CreateHeap(&HeapDesc, IID_PPV_ARGS(&Textures)));
    if (Buffers == nullptr || Upload == nullptr || Targets == nullptr || Textures == nullptr)
        return;

    Buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Buffer.Width = 65536;
    Buffer.Height = 1;
    Buffer.DepthOrArraySize = 1;
    Buffer.MipLevels = 1;
    Buffer.SampleDesc.Count = 1;
    Buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Texture.Width = 64;
    Texture.Height = 64;
    Texture.DepthOrArraySize = 1;
    Texture.MipLevels = 1;
    Texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Texture.SampleDesc.Count = 1;

    Check.Report("placed_null_heap", "0x%08lX", Device->CreatePlacedResource(nullptr, 0, &Buffer,
                                                                             D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                             IID_PPV_ARGS(&First)));
    Check.Report("placed_null_desc", "0x%08lX", Device->CreatePlacedResource(Buffers.Get(), 0, nullptr,
                                                                             D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                             IID_PPV_ARGS(&First)));
    Check.Report("placed_check", "0x%08lX", Device->CreatePlacedResource(Buffers.Get(), 0, &Buffer,
                                                                         D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                         __uuidof(ID3D12Resource), nullptr));
    hr = Device->CreatePlacedResource(Buffers.Get(), 0, &Buffer, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                      IID_PPV_ARGS(&First));
    Check.Report("placed_first", "0x%08lX", hr);
    if (First != nullptr)
    {
        Base = First->GetGPUVirtualAddress();
        TcReportPlaced(Check, "placed_first", First.Get(), Base);
    }
    Check.Report("heap_refs", "%lu", (Buffers->AddRef(), Buffers->Release()));
    hr = Device->CreatePlacedResource(Buffers.Get(), 3 * 65536, &Buffer, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                      IID_PPV_ARGS(&Second));
    Check.Report("placed_last", "0x%08lX", hr);
    if (Second != nullptr)
        TcReportPlaced(Check, "placed_last", Second.Get(), Base);
    First.Reset();
    Second.Reset();
    Check.Report("heap_refs_after", "%lu", (Buffers->AddRef(), Buffers->Release()));

    {
        static const struct
        {
            PCSTR Name;
            UINT64 Offset;
            UINT64 Width;
            UINT Flags;
            D3D12_RESOURCE_STATES State;
        } Cases[] =
        {
            { "overlap", 0, 65536, 0, D3D12_RESOURCE_STATE_COMMON },
            { "unaligned", 256, 4096, 0, D3D12_RESOURCE_STATE_COMMON },
            { "past_end", 4 * 65536, 4096, 0, D3D12_RESOURCE_STATE_COMMON },
            { "straddle", 3 * 65536, 2 * 65536, 0, D3D12_RESOURCE_STATE_COMMON },
            { "wrap", 0xFFFFFFFFFFFF0000ull, 0x20000, 0, D3D12_RESOURCE_STATE_COMMON },
            { "small", 65536, 1, 0, D3D12_RESOURCE_STATE_COMMON },
            { "uav", 65536, 4096, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS },
            { "copy_dest", 65536, 4096, 0, D3D12_RESOURCE_STATE_COPY_DEST },
            { "render_target_flag", 65536, 4096, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON },
        };

        for (const auto& Each : Cases)
        {
            ComPtr<ID3D12Resource> Placed;
            D3D12_RESOURCE_DESC Desc = Buffer;
            CHAR Key[96];

            Desc.Width = Each.Width;
            Desc.Flags = (D3D12_RESOURCE_FLAGS)Each.Flags;
            hr = Device->CreatePlacedResource(Buffers.Get(), Each.Offset, &Desc, Each.State, nullptr,
                                              IID_PPV_ARGS(&Placed));
            sprintf_s(Key, "buffer_%s", Each.Name);
            Check.Report(Key, "0x%08lX", hr);
            if (Placed != nullptr)
                TcReportPlaced(Check, Key, Placed.Get(), Base);
        }
    }

    for (UINT Index = 0; Index < 2; Index++)
    {
        D3D12_RESOURCE_DESC Desc = Buffer;
        CHAR Key[64];

        Desc.Width = 4096;
        hr = Device->CreatePlacedResource(Upload.Get(), Index * 65536, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&Mapped[Index]));
        sprintf_s(Key, "upload_%u", Index);
        Check.Report(Key, "0x%08lX", hr);
    }
    Check.Report("upload_common", "0x%08lX", Device->CreatePlacedResource(Upload.Get(), 2 * 65536, &Buffer,
                                                                          D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                          IID_PPV_ARGS(&First)));
    First.Reset();
    Check.Report("upload_texture", "0x%08lX", Device->CreatePlacedResource(Upload.Get(), 2 * 65536, &Texture,
                                                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                           IID_PPV_ARGS(&First)));
    First.Reset();
    if (Mapped[0] != nullptr && Mapped[1] != nullptr)
    {
        BYTE* Data[2] = {};
        HRESULT Results[2];
        D3D12_RANGE None = { 0, 0 };

        TcReportPlaced(Check, "upload_0", Mapped[0].Get(), 0);
        TcReportPlaced(Check, "upload_1", Mapped[1].Get(), Mapped[0]->GetGPUVirtualAddress());
        Results[0] = Mapped[0]->Map(0, &None, reinterpret_cast<void**>(&Data[0]));
        Results[1] = Mapped[1]->Map(0, &None, reinterpret_cast<void**>(&Data[1]));
        Check.Report("map", "0x%08lX 0x%08lX apart %lld", Results[0], Results[1],
                     Data[0] != nullptr && Data[1] != nullptr ? (LONGLONG)(Data[1] - Data[0]) : -1ll);
        if (Data[0] != nullptr)
        {
            Data[0][0] = 0x5A;
            Mapped[0]->Unmap(0, nullptr);
        }
        if (Data[1] != nullptr)
            Mapped[1]->Unmap(0, nullptr);
        Data[0] = nullptr;
        Check.Report("map_again", "0x%08lX", Mapped[0]->Map(0, nullptr, reinterpret_cast<void**>(&Data[0])));
        if (Data[0] != nullptr)
        {
            Check.Report("map_kept", "0x%02x", Data[0][0]);
            Mapped[0]->Unmap(0, nullptr);
        }
        Check.Report("map_sub1", "0x%08lX", Mapped[0]->Map(1, nullptr, nullptr));
    }

    {
        static const struct
        {
            PCSTR Name;
            BOOLEAN Target;
            UINT64 Offset;
            UINT Flags;
            UINT Samples;
            UINT64 Alignment;
            D3D12_RESOURCE_STATES State;
        } Cases[] =
        {
            { "rt_in_targets", TRUE, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1, 0, D3D12_RESOURCE_STATE_RENDER_TARGET },
            { "plain_in_targets", TRUE, 0, 0, 1, 0, D3D12_RESOURCE_STATE_COMMON },
            { "plain_in_textures", FALSE, 0, 0, 1, 0, D3D12_RESOURCE_STATE_COMMON },
            { "rt_in_textures", FALSE, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1, 0, D3D12_RESOURCE_STATE_RENDER_TARGET },
            { "small_align", FALSE, 4096, 0, 1, 4096, D3D12_RESOURCE_STATE_COMMON },
            { "small_align_bad", FALSE, 256, 0, 1, 4096, D3D12_RESOURCE_STATE_COMMON },
            { "msaa", TRUE, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 4, 0, D3D12_RESOURCE_STATE_RENDER_TARGET },
            { "msaa_64k", TRUE, 65536, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 4, 65536, D3D12_RESOURCE_STATE_RENDER_TARGET },
            { "offset_64k", FALSE, 7 * 65536, 0, 1, 0, D3D12_RESOURCE_STATE_COPY_DEST },
            { "past_end", FALSE, 8 * 65536, 0, 1, 0, D3D12_RESOURCE_STATE_COMMON },
        };

        for (const auto& Each : Cases)
        {
            ComPtr<ID3D12Resource> Placed;
            D3D12_RESOURCE_DESC Desc = Texture;
            CHAR Key[96];

            Desc.Flags = (D3D12_RESOURCE_FLAGS)Each.Flags;
            Desc.SampleDesc.Count = Each.Samples;
            Desc.Alignment = Each.Alignment;
            hr = Device->CreatePlacedResource(Each.Target ? Targets.Get() : Textures.Get(), Each.Offset, &Desc,
                                              Each.State, nullptr, IID_PPV_ARGS(&Placed));
            sprintf_s(Key, "texture_%s", Each.Name);
            Check.Report(Key, "0x%08lX", hr);
            if (Placed != nullptr)
                TcReportPlaced(Check, Key, Placed.Get(), 0);
        }
    }

    Check.Report("buffer_in_targets", "0x%08lX", Device->CreatePlacedResource(Targets.Get(), 0, &Buffer,
                                                                              D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                              IID_PPV_ARGS(&First)));
    First.Reset();

    if (SUCCEEDED(Device.As(&Device8)))
    {
        D3D12_RESOURCE_DESC1 Wide = {};

        Wide.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        Wide.Width = 4096;
        Wide.Height = 1;
        Wide.DepthOrArraySize = 1;
        Wide.MipLevels = 1;
        Wide.SampleDesc.Count = 1;
        Wide.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = Device8->CreatePlacedResource1(Buffers.Get(), 65536, &Wide, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                            IID_PPV_ARGS(&First));
        Check.Report("placed1", "0x%08lX", hr);
        if (First != nullptr)
            TcReportPlaced(Check, "placed1", First.Get(), Base);
        First.Reset();
        Check.Report("placed1_null_desc", "0x%08lX", Device8->CreatePlacedResource1(Buffers.Get(), 0, nullptr,
                                                                                    D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                                    IID_PPV_ARGS(&First)));
    }

    if (SUCCEEDED(Device.As(&Device10)))
    {
        D3D12_RESOURCE_DESC1 Wide = {};

        Wide.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        Wide.Width = 4096;
        Wide.Height = 1;
        Wide.DepthOrArraySize = 1;
        Wide.MipLevels = 1;
        Wide.SampleDesc.Count = 1;
        Wide.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = Device10->CreatePlacedResource2(Buffers.Get(), 65536, &Wide, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0,
                                             nullptr, IID_PPV_ARGS(&First));
        Check.Report("placed2", "0x%08lX", hr);
        First.Reset();
        hr = Device10->CreatePlacedResource2(Buffers.Get(), 65536, &Wide, D3D12_BARRIER_LAYOUT_COMMON, nullptr, 0,
                                             nullptr, IID_PPV_ARGS(&First));
        Check.Report("placed2_common", "0x%08lX", hr);
        First.Reset();
        Wide = {};
        Wide.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        Wide.Width = 64;
        Wide.Height = 64;
        Wide.DepthOrArraySize = 1;
        Wide.MipLevels = 1;
        Wide.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Wide.SampleDesc.Count = 1;
        Wide.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        hr = Device10->CreatePlacedResource2(Targets.Get(), 0, &Wide, D3D12_BARRIER_LAYOUT_RENDER_TARGET, nullptr, 0,
                                             nullptr, IID_PPV_ARGS(&First));
        Check.Report("placed2_rt", "0x%08lX", hr);
        First.Reset();
        hr = Device10->CreatePlacedResource2(Targets.Get(), 0, &Wide, D3D12_BARRIER_LAYOUT_COPY_SOURCE, nullptr, 0,
                                             nullptr, IID_PPV_ARGS(&First));
        Check.Report("placed2_copy_source", "0x%08lX", hr);
        First.Reset();
    }

    if (SUCCEEDED(Device.As(&Device4)))
    {
        ComPtr<ID3D12Heap> Heap;

        HeapDesc = {};
        HeapDesc.SizeInBytes = 65536;
        HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        Check.Report("heap1", "0x%08lX", Device4->CreateHeap1(&HeapDesc, nullptr, IID_PPV_ARGS(&Heap)));
        if (Heap != nullptr)
            TcReportHeap(Check, "heap1", Heap.Get());
    }
}

/* One indirect argument as a command signature lists it */
struct TcArgument
{
    D3D12_INDIRECT_ARGUMENT_TYPE Type;
    UINT First;
    UINT Second;
    UINT Third;
};

/* queries: query heaps and command signatures */
static
VOID
TcCheckQueries(
    _In_ TwelveCheck& Check)
{
    static const struct
    {
        PCSTR Name;
        UINT Type;
        UINT Count;
        UINT NodeMask;
    } Heaps[] =
    {
        { "occlusion", D3D12_QUERY_HEAP_TYPE_OCCLUSION, 16, 0 },
        { "timestamp", D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 8, 0 },
        { "pipeline_statistics", D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, 4, 0 },
        { "so_statistics", D3D12_QUERY_HEAP_TYPE_SO_STATISTICS, 4, 0 },
        { "video_decode_statistics", D3D12_QUERY_HEAP_TYPE_VIDEO_DECODE_STATISTICS, 4, 0 },
        { "copy_queue_timestamp", D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP, 4, 0 },
        { "type_6", 6, 4, 0 },
        { "pipeline_statistics1", D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1, 4, 0 },
        { "type_8", 8, 4, 0 },
        { "count_zero", D3D12_QUERY_HEAP_TYPE_OCCLUSION, 0, 0 },
        { "count_large", D3D12_QUERY_HEAP_TYPE_OCCLUSION, 65536, 0 },
        { "node_1", D3D12_QUERY_HEAP_TYPE_OCCLUSION, 4, 1 },
        { "node_2", D3D12_QUERY_HEAP_TYPE_OCCLUSION, 4, 2 },
        { "node_3", D3D12_QUERY_HEAP_TYPE_OCCLUSION, 4, 3 },
    };
    static const TcArgument Draw[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument DrawIndexed[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED } };
    static const TcArgument Dispatch[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH } };
    static const TcArgument Mesh[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH } };
    static const TcArgument Rays[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS } };
    static const TcArgument None[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW, 0 } };
    static const TcArgument TwoDraws[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument DrawFirst[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW }, { D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW, 0 } };
    static const TcArgument Vertex[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW, 1 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument VertexTwice[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW, 1 }, { D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW, 1 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument VertexSlot32[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW, 32 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument VertexDispatch[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW, 0 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH } };
    static const TcArgument Index[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED } };
    static const TcArgument IndexDraw[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument IndexTwice[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW }, { D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED } };
    static const TcArgument Constants[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0, 0, 4 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument ConstantsZero[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0, 0, 0 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument ConstantsTooMany[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0, 2, 3 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument ConstantsSplit[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0, 0, 2 }, { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0, 2, 2 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument ConstantsOverlap[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0, 0, 2 }, { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0, 1, 2 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument ConstantsBackward[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0, 2, 2 }, { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0, 0, 2 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument ConstantsWrongSlot[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 1, 0, 1 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument Views[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW, 1 }, { D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW, 2 }, { D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW, 3 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH } };
    static const TcArgument ViewsBackward[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW, 2 }, { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW, 1 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH } };
    static const TcArgument ViewWrongType[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW, 1 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH } };
    static const TcArgument ViewTable[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW, 4 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH } };
    static const TcArgument ViewPastEnd[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW, 9 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH } };
    static const TcArgument Incrementing[] = { { D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT, 0, 1 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const TcArgument UnknownType[] = { { (D3D12_INDIRECT_ARGUMENT_TYPE)12 }, { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW } };
    static const struct
    {
        PCSTR Name;
        const TcArgument* Arguments;
        UINT Count;
        UINT Stride;
        BOOLEAN Root;
        UINT NodeMask;
    } Signatures[] =
    {
        { "draw", Draw, 1, 16, FALSE, 0 },
        { "draw_root", Draw, 1, 16, TRUE, 0 },
        { "draw_stride_12", Draw, 1, 12, FALSE, 0 },
        { "draw_stride_18", Draw, 1, 18, FALSE, 0 },
        { "draw_stride_64", Draw, 1, 64, FALSE, 0 },
        { "draw_stride_0", Draw, 1, 0, FALSE, 0 },
        { "draw_node_1", Draw, 1, 16, FALSE, 1 },
        { "draw_node_2", Draw, 1, 16, FALSE, 2 },
        { "draw_node_3", Draw, 1, 16, FALSE, 3 },
        { "draw_indexed", DrawIndexed, 1, 20, FALSE, 0 },
        { "draw_indexed_16", DrawIndexed, 1, 16, FALSE, 0 },
        { "dispatch", Dispatch, 1, 12, FALSE, 0 },
        { "dispatch_mesh", Mesh, 1, 12, FALSE, 0 },
        { "dispatch_rays", Rays, 1, 104, FALSE, 0 },
        { "empty", Draw, 0, 16, FALSE, 0 },
        { "no_draw", None, 1, 16, FALSE, 0 },
        { "two_draws", TwoDraws, 2, 32, FALSE, 0 },
        { "draw_first", DrawFirst, 2, 32, FALSE, 0 },
        { "vertex", Vertex, 2, 32, FALSE, 0 },
        { "vertex_short", Vertex, 2, 28, FALSE, 0 },
        { "vertex_twice", VertexTwice, 3, 48, FALSE, 0 },
        { "vertex_slot_32", VertexSlot32, 2, 32, FALSE, 0 },
        { "vertex_dispatch", VertexDispatch, 2, 28, FALSE, 0 },
        { "index", Index, 2, 36, FALSE, 0 },
        { "index_draw", IndexDraw, 2, 32, FALSE, 0 },
        { "index_twice", IndexTwice, 3, 52, FALSE, 0 },
        { "constants", Constants, 2, 32, TRUE, 0 },
        { "constants_no_root", Constants, 2, 32, FALSE, 0 },
        { "constants_zero", ConstantsZero, 2, 32, TRUE, 0 },
        { "constants_too_many", ConstantsTooMany, 2, 32, TRUE, 0 },
        { "constants_split", ConstantsSplit, 3, 32, TRUE, 0 },
        { "constants_overlap", ConstantsOverlap, 3, 32, TRUE, 0 },
        { "constants_backward", ConstantsBackward, 3, 32, TRUE, 0 },
        { "constants_wrong_slot", ConstantsWrongSlot, 2, 32, TRUE, 0 },
        { "views", Views, 4, 36, TRUE, 0 },
        { "views_backward", ViewsBackward, 3, 28, TRUE, 0 },
        { "view_wrong_type", ViewWrongType, 2, 20, TRUE, 0 },
        { "view_table", ViewTable, 2, 20, TRUE, 0 },
        { "view_past_end", ViewPastEnd, 2, 20, TRUE, 0 },
        { "incrementing", Incrementing, 2, 20, TRUE, 0 },
        { "unknown_type", UnknownType, 2, 32, FALSE, 0 },
    };
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12RootSignature> Root;
    ComPtr<ID3DBlob> Blob;
    ComPtr<ID3DBlob> Errors;
    D3D12_ROOT_PARAMETER1 Parameters[5] = {};
    D3D12_DESCRIPTOR_RANGE1 Range = {};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC RootDesc = {};
    HRESULT hr;

    if (FAILED(Check.MakeDevice(&Device)))
    {
        Check.Report("device", "none");
        return;
    }

    /* A NULL query heap desc is left out: the system reads the type before checking it */
    for (const auto& Each : Heaps)
    {
        ComPtr<ID3D12QueryHeap> Heap;
        D3D12_QUERY_HEAP_DESC Desc = {};
        CHAR Key[96];

        Desc.Type = (D3D12_QUERY_HEAP_TYPE)Each.Type;
        Desc.Count = Each.Count;
        Desc.NodeMask = Each.NodeMask;
        sprintf_s(Key, "heap_%s_check", Each.Name);
        Check.Report(Key, "0x%08lX", Device->CreateQueryHeap(&Desc, __uuidof(ID3D12QueryHeap), nullptr));
        hr = Device->CreateQueryHeap(&Desc, IID_PPV_ARGS(&Heap));
        sprintf_s(Key, "heap_%s", Each.Name);
        Check.Report(Key, "0x%08lX", hr);
    }

    Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    Parameters[0].Constants.Num32BitValues = 4;
    Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    Parameters[1].Descriptor.ShaderRegister = 1;
    Parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    Parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    Range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    Range.NumDescriptors = 1;
    Range.BaseShaderRegister = 1;
    Parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters[4].DescriptorTable.NumDescriptorRanges = 1;
    Parameters[4].DescriptorTable.pDescriptorRanges = &Range;
    RootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    RootDesc.Desc_1_1.NumParameters = _countof(Parameters);
    RootDesc.Desc_1_1.pParameters = Parameters;
    if (Check.SerializeVersioned == nullptr || FAILED(Check.SerializeVersioned(&RootDesc, &Blob, &Errors)))
    {
        Check.Report("root", "none");
        return;
    }
    hr = Device->CreateRootSignature(0, Blob->GetBufferPointer(), Blob->GetBufferSize(), IID_PPV_ARGS(&Root));
    Check.Report("root", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    /* A NULL command signature desc is left out: the system reads its arguments before checking it */
    for (const auto& Each : Signatures)
    {
        ComPtr<ID3D12CommandSignature> Signature;
        D3D12_INDIRECT_ARGUMENT_DESC Arguments[4] = {};
        D3D12_COMMAND_SIGNATURE_DESC Desc = {};
        CHAR Key[96];

        for (UINT Index = 0; Index < Each.Count; Index++)
        {
            Arguments[Index].Type = Each.Arguments[Index].Type;
            switch (Arguments[Index].Type)
            {
                case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
                    Arguments[Index].VertexBuffer.Slot = Each.Arguments[Index].First;
                    break;
                case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
                    Arguments[Index].Constant.RootParameterIndex = Each.Arguments[Index].First;
                    Arguments[Index].Constant.DestOffsetIn32BitValues = Each.Arguments[Index].Second;
                    Arguments[Index].Constant.Num32BitValuesToSet = Each.Arguments[Index].Third;
                    break;
                case D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT:
                    Arguments[Index].IncrementingConstant.RootParameterIndex = Each.Arguments[Index].First;
                    Arguments[Index].IncrementingConstant.DestOffsetIn32BitValues = Each.Arguments[Index].Second;
                    break;
                case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
                case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
                case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
                    Arguments[Index].ConstantBufferView.RootParameterIndex = Each.Arguments[Index].First;
                    break;
                default:
                    break;
            }
        }
        Desc.ByteStride = Each.Stride;
        Desc.NumArgumentDescs = Each.Count;
        Desc.pArgumentDescs = Arguments;
        Desc.NodeMask = Each.NodeMask;

        sprintf_s(Key, "signature_%s_check", Each.Name);
        Check.Report(Key, "0x%08lX", Device->CreateCommandSignature(&Desc, Each.Root ? Root.Get() : nullptr,
                                                                    __uuidof(ID3D12CommandSignature), nullptr));
        hr = Device->CreateCommandSignature(&Desc, Each.Root ? Root.Get() : nullptr, IID_PPV_ARGS(&Signature));
        sprintf_s(Key, "signature_%s", Each.Name);
        if (Signature != nullptr && Each.Root)
        {
            Check.Report(Key, "0x%08lX root refs %lu", hr, (Root->AddRef(), Root->Release()));
            Signature.Reset();
            Check.Report("root_refs_after", "%lu", (Root->AddRef(), Root->Release()));
        }
        else
        {
            Check.Report(Key, "0x%08lX", hr);
        }
    }
}

struct CheckInfo
{
    PCSTR Name;
    VOID (*Run)(TwelveCheck& Check);
};

static const CheckInfo Checks[] =
{
    { "create", TcCheckCreate },
    { "lifetime", TcCheckLifetime },
    { "device", TcCheckDevice },
    { "features", TcCheckFeatures },
    { "fence", TcCheckFence },
    { "resources", TcCheckResources },
    { "clear", TcCheckClear },
    { "helpers", TcCheckHelpers },
    { "exports", TcCheckExports },
    { "interfaces", TcCheckInterfaces },
    { "levels", TcCheckCreateLevels },
    { "featureall", TcCheckFeatureAll },
    { "formats", TcCheckFormats },
    { "queues", TcCheckQueues },
    { "heaps", TcCheckDescriptorHeaps },
    { "layouts", TcCheckLayouts },
    { "rootsigs", TcCheckRootSignatures },
    { "pipelines", TcCheckPipelines },
    { "identity", TcCheckIdentity },
    { "placed", TcCheckPlaced },
    { "queries", TcCheckQueries },
};

/* System32 for "system", the ReactX build next to this exe for "reactx", else a directory */
static
std::wstring
TcRuntimeDirectory(
    _In_z_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
    {
        GetSystemDirectoryW(Module, _countof(Module));
        return std::wstring(Module) + L"\\";
    }

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\";
    }

    return std::wstring(Runtime) + L"\\";
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    std::vector<std::string> Wanted;
    PCWSTR Runtime = L"system";
    TwelveCheck Check;
    int Index;
    char Name[128];

    setvbuf(stdout, nullptr, _IONBF, 0);

    for (Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Runtime = argv[++Index];
            continue;
        }

        WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
        Wanted.push_back(Name);
    }

    Check.D3d12 = LoadLibraryExW((TcRuntimeDirectory(Runtime) + L"d3d12.dll").c_str(), NULL,
                                 LOAD_WITH_ALTERED_SEARCH_PATH);
    if (Check.D3d12 == NULL)
    {
        fprintf(stderr, "error: cannot load d3d12.dll\n");
        return 1;
    }

    Check.CreateDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(Check.D3d12, "D3D12CreateDevice"));
    Check.GetDebugInterface = reinterpret_cast<PFN_D3D12_GET_DEBUG_INTERFACE>(
        GetProcAddress(Check.D3d12, "D3D12GetDebugInterface"));
    Check.GetInterface = reinterpret_cast<PFN_D3D12_GET_INTERFACE>(GetProcAddress(Check.D3d12, "D3D12GetInterface"));
    Check.SerializeRootSignature = reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(
        GetProcAddress(Check.D3d12, "D3D12SerializeRootSignature"));
    Check.CreateDeserializer = reinterpret_cast<PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER>(
        GetProcAddress(Check.D3d12, "D3D12CreateRootSignatureDeserializer"));
    Check.SerializeVersioned = reinterpret_cast<PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE>(
        GetProcAddress(Check.D3d12, "D3D12SerializeVersionedRootSignature"));
    Check.CreateVersionedDeserializer = reinterpret_cast<PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER>(
        GetProcAddress(Check.D3d12, "D3D12CreateVersionedRootSignatureDeserializer"));
    if (Check.CreateDevice == nullptr || Check.SerializeRootSignature == nullptr || Check.CreateDeserializer == nullptr)
    {
        fprintf(stderr, "error: missing d3d12 exports\n");
        return 1;
    }

    for (const CheckInfo& Info : Checks)
    {
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Each : Wanted)
            IsWanted = IsWanted || Each == Info.Name;
        if (!IsWanted)
            continue;

        Check.CheckName = Info.Name;
        Info.Run(Check);
    }

    /* Which runtime really ran, kept out of the compared output */
    for (PCWSTR Each : { L"d3d12.dll", L"d3d12core.dll" })
    {
        WCHAR Path[MAX_PATH] = L"(not loaded)";
        HMODULE Loaded = GetModuleHandleW(Each);

        if (Loaded != NULL)
            GetModuleFileNameW(Loaded, Path, _countof(Path));
        fwprintf(stderr, L"%s: %s\n", Each, Path);
    }

    printf("failures = 0\n");
    return 0;
}
