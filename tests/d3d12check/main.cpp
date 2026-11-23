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
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

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
