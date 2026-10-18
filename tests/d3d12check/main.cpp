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

struct CheckInfo
{
    PCSTR Name;
    VOID (*Run)(TwelveCheck& Check);
};

static const CheckInfo Checks[] =
{
    { "create", TcCheckCreate },
    { "device", TcCheckDevice },
    { "features", TcCheckFeatures },
    { "fence", TcCheckFence },
    { "resources", TcCheckResources },
    { "clear", TcCheckClear },
    { "helpers", TcCheckHelpers },
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

    printf("failures = 0\n");
    return 0;
}
