/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 11 on 12 behavior, printed as comparable facts
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * d3d11on12check [-runtime system|reactx|<dir>] [check ...]
 *
 * The runtime picks the d3d11on12.dll that is loaded before d3d11.dll from System32 asks for it, so
 * the same Direct3D 11 runtime drives the system layer or the ReactX one. D3D12 is always the
 * system's. The trace checks print the D3D12 calls the layer makes, hooked on the device and queue.
 */

#include <windows.h>
#include <d3d12.h>
#include <d3d11_4.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "trace12.hpp"

using Microsoft::WRL::ComPtr;

struct OnCheck
{
    PFN_D3D11ON12_CREATE_DEVICE CreateDevice = nullptr;
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue;
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
    Make(
        _In_ UINT Flags,
        _Outptr_ ID3D11Device** Out,
        _Out_opt_ D3D_FEATURE_LEVEL* Level = nullptr)
    {
        IUnknown* Queues[] = { Queue.Get() };

        return CreateDevice(Device.Get(), Flags, nullptr, 0, Queues, 1, 0, Out, nullptr, Level);
    }

    /* The reference count an object has right now */
    static
    ULONG
    Count(
        _In_ IUnknown* Object)
    {
        Object->AddRef();
        return Object->Release();
    }
};

/* create: argument checks of D3D11On12CreateDevice */
static
VOID
OcCheckCreate(
    _In_ OnCheck& Check)
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<IDXGIFactory1> Factory;
    ComPtr<ID3D12CommandQueue> Compute;
    ComPtr<ID3D12CommandQueue> Second;
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    D3D_FEATURE_LEVEL Levels[2] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL High[] = { D3D_FEATURE_LEVEL_12_2 };
    D3D_FEATURE_LEVEL Bogus[] = { (D3D_FEATURE_LEVEL)0x1234 };
    D3D_FEATURE_LEVEL Chosen;
    IUnknown* Queues[2] = { Check.Queue.Get(), nullptr };
    HRESULT hr;

    hr = Check.CreateDevice(nullptr, 0, nullptr, 0, Queues, 1, 0, &Device, nullptr, nullptr);
    Check.Report("null_device", "0x%08lX %d", hr, Device == nullptr);

    CreateDXGIFactory1(IID_PPV_ARGS(&Factory));
    hr = Check.CreateDevice(Factory.Get(), 0, nullptr, 0, Queues, 1, 0, &Device, nullptr, nullptr);
    Check.Report("not_a_device", "0x%08lX", hr);

    Chosen = (D3D_FEATURE_LEVEL)0x77;
    hr = Check.CreateDevice(Check.Device.Get(), 0, Levels, 2, Queues, 1, 0, &Device, nullptr, &Levels[1]);
    Check.Report("overlap", "0x%08lX level 0x%X", hr, Levels[1]);
    Levels[1] = D3D_FEATURE_LEVEL_11_0;

    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, Queues, 1, 3, &Device, nullptr, &Chosen);
    Check.Report("two_nodes", "0x%08lX level 0x%X", hr, Chosen);

    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, Queues, 1, 2, &Device, nullptr, &Chosen);
    Check.Report("wrong_node", "0x%08lX", hr);

    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, nullptr, 1, 0, &Device, nullptr, &Chosen);
    Check.Report("count_without_queues", "0x%08lX", hr);

    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, Queues, 0, 0, &Device, nullptr, &Chosen);
    Check.Report("queues_without_count", "0x%08lX", hr);

    Queues[1] = Factory.Get();
    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, Queues, 2, 0, &Device, nullptr, &Chosen);
    Check.Report("not_a_queue", "0x%08lX", hr);

    hr = Check.CreateDevice(Check.Device.Get(), D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT, nullptr, 0, Queues, 1, 0,
                            &Device, nullptr, &Chosen);
    Check.Report("timeout_queue", "0x%08lX", hr);

    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    Check.Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Compute));
    Queues[1] = Compute.Get();
    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, Queues, 2, 0, &Device, nullptr, &Chosen);
    Check.Report("compute_queue", "0x%08lX", hr);

    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check.Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Second));
    Queues[1] = Second.Get();
    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, Queues, 2, 0, &Device, nullptr, &Chosen);
    Check.Report("two_direct_queues", "0x%08lX", hr);

    hr = Check.CreateDevice(Check.Device.Get(), 0, Bogus, 1, Queues, 1, 0, &Device, nullptr, &Chosen);
    Check.Report("bogus_level", "0x%08lX level 0x%X %d", hr, Chosen, Device == nullptr);
    Device.Reset();

    hr = Check.CreateDevice(Check.Device.Get(), 0, High, 1, Queues, 1, 0, &Device, nullptr, &Chosen);
    Check.Report("level_12_2", "0x%08lX level 0x%X", hr, Chosen);
    Device.Reset();

    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, Queues, 1, 0, nullptr, nullptr, &Chosen);
    Check.Report("no_outputs", "0x%08lX level 0x%X", hr, Chosen);

    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, Queues, 1, 0, nullptr, &Context, &Chosen);
    Check.Report("context_only", "0x%08lX level 0x%X %d", hr, Chosen, Context != nullptr);
    Context.Reset();

    hr = Check.CreateDevice(Check.Device.Get(), 0, Levels, 2, Queues, 1, 0, &Device, &Context, &Chosen);
    Check.Report("levels", "0x%08lX level 0x%X %d", hr, Chosen, Device != nullptr && Context != nullptr);
    Context.Reset();
    Device.Reset();

    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, nullptr, 0, 0, &Device, nullptr, &Chosen);
    Check.Report("no_queue", "0x%08lX level 0x%X", hr, Chosen);
    Device.Reset();

    hr = Check.CreateDevice(Check.Device.Get(), 0, nullptr, 0, Queues, 1, 1, &Device, nullptr, &Chosen);
    Check.Report("node_1", "0x%08lX level 0x%X", hr, Chosen);
}

/* device: what the device answers and which interfaces it hands out */
static
VOID
OcCheckDevice(
    _In_ OnCheck& Check)
{
    static const struct
    {
        PCSTR Name;
        IID Iid;
    } Interfaces[] =
    {
        { "Device", __uuidof(ID3D11Device) }, { "Device1", __uuidof(ID3D11Device1) },
        { "Device2", __uuidof(ID3D11Device2) }, { "Device3", __uuidof(ID3D11Device3) },
        { "Device4", __uuidof(ID3D11Device4) }, { "Device5", __uuidof(ID3D11Device5) },
        { "On12Device", __uuidof(ID3D11On12Device) }, { "On12Device1", __uuidof(ID3D11On12Device1) },
        { "On12Device2", __uuidof(ID3D11On12Device2) }, { "DXGIDevice", __uuidof(IDXGIDevice) },
        { "DXGIDevice1", __uuidof(IDXGIDevice1) }, { "DXGIDevice2", __uuidof(IDXGIDevice2) },
        { "DXGIDevice3", __uuidof(IDXGIDevice3) }, { "DXGIDevice4", __uuidof(IDXGIDevice4) },
        { "Multithread", __uuidof(ID3D11Multithread) }, { "D3D10Multithread", __uuidof(ID3D10Multithread) },
        { "VideoDevice", __uuidof(ID3D11VideoDevice) }, { "VideoDevice1", __uuidof(ID3D11VideoDevice1) },
        { "D3D12Device", __uuidof(ID3D12Device) }, { "Debug", __uuidof(ID3D11Debug) },
        { "InfoQueue", __uuidof(ID3D11InfoQueue) },
    };
    static const DXGI_FORMAT Formats[] =
    {
        DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_D24_UNORM_S8_UINT,
        DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_NV12, DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_YUY2, DXGI_FORMAT_B5G6R5_UNORM,
    };
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11On12Device> On12;
    ComPtr<ID3D12Device> Back;
    ComPtr<ID3D11DeviceContext> Context;
    D3D11_FEATURE_DATA_THREADING Threading = {};
    D3D11_FEATURE_DATA_DOUBLES Doubles = {};
    D3D11_FEATURE_DATA_D3D11_OPTIONS Options = {};
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 Options1 = {};
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 Options2 = {};
    D3D11_FEATURE_DATA_D3D11_OPTIONS3 Options3 = {};
    D3D11_FEATURE_DATA_ARCHITECTURE_INFO Architecture = {};
    D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT Precision = {};
    D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT Address = {};
    D3D11_FEATURE_DATA_SHADER_CACHE Cache = {};
    D3D_FEATURE_LEVEL Level;
    ULONG DeviceCount;
    ULONG QueueCount;
    char Key[64];
    UINT Support;
    UINT Quality;
    HRESULT hr;

    DeviceCount = OnCheck::Count(Check.Device.Get());
    QueueCount = OnCheck::Count(Check.Queue.Get());

    hr = Check.Make(0, &Device, &Level);
    Check.Report("create", "0x%08lX level 0x%X", hr, Level);
    if (FAILED(hr))
        return;

    Check.Report("refs", "device +%lu queue +%lu", OnCheck::Count(Check.Device.Get()) - DeviceCount,
                 OnCheck::Count(Check.Queue.Get()) - QueueCount);
    Check.Report("flags", "0x%X", Device->GetCreationFlags());
    Check.Report("level", "0x%X", Device->GetFeatureLevel());
    Check.Report("removed", "0x%08lX", Device->GetDeviceRemovedReason());

    for (const auto& Each : Interfaces)
    {
        ComPtr<IUnknown> Face;

        sprintf_s(Key, "qi_%s", Each.Name);
        Check.Report(Key, "0x%08lX", Device->QueryInterface(Each.Iid, &Face));
    }

    Device.As(&On12);
    hr = On12->QueryInterface(IID_PPV_ARGS(&Back));
    Check.Report("on12_qi_d3d12", "0x%08lX", hr);

    {
        ComPtr<ID3D11On12Device1> On12One;
        ComPtr<ID3D12CommandQueue> Queue;

        if (SUCCEEDED(Device.As(&On12One)))
        {
            hr = On12One->GetD3D12Device(IID_PPV_ARGS(&Back));
            Check.Report("get_d3d12", "0x%08lX same %d", hr, Back.Get() == Check.Device.Get());
            hr = On12One->GetD3D12Device(IID_PPV_ARGS(&Queue));
            Check.Report("get_d3d12_queue_iid", "0x%08lX", hr);
        }
    }

    hr = Device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &Threading, sizeof(Threading));
    Check.Report("threading", "0x%08lX %d %d", hr, Threading.DriverConcurrentCreates, Threading.DriverCommandLists);
    hr = Device->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &Doubles, sizeof(Doubles));
    Check.Report("doubles", "0x%08lX %d", hr, Doubles.DoublePrecisionFloatShaderOps);
    hr = Device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &Options, sizeof(Options));
    Check.Report("options", "0x%08lX logic %d uavonly %d discard %d cb %d map %d", hr, Options.OutputMergerLogicOp,
                 Options.UAVOnlyRenderingForcedSampleCount, Options.DiscardAPIsSeenByDriver,
                 Options.ConstantBufferOffsetting, Options.MapNoOverwriteOnDynamicConstantBuffer);
    hr = Device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &Options1, sizeof(Options1));
    Check.Report("options1", "0x%08lX tiled %d minmax %d", hr, Options1.TiledResourcesTier,
                 Options1.MinMaxFiltering);
    hr = Device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &Options2, sizeof(Options2));
    Check.Report("options2", "0x%08lX ps_stencil %d typed %d rov %d cr %d tiled %d", hr,
                 Options2.PSSpecifiedStencilRefSupported, Options2.TypedUAVLoadAdditionalFormats,
                 Options2.ROVsSupported, Options2.ConservativeRasterizationTier, Options2.TiledResourcesTier);
    hr = Device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3, &Options3, sizeof(Options3));
    Check.Report("options3", "0x%08lX %d", hr, Options3.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer);
    hr = Device->CheckFeatureSupport(D3D11_FEATURE_ARCHITECTURE_INFO, &Architecture, sizeof(Architecture));
    Check.Report("architecture", "0x%08lX %d", hr, Architecture.TileBasedDeferredRenderer);
    hr = Device->CheckFeatureSupport(D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &Precision, sizeof(Precision));
    Check.Report("precision", "0x%08lX %u %u", hr, Precision.PixelShaderMinPrecision,
                 Precision.AllOtherShaderStagesMinPrecision);
    hr = Device->CheckFeatureSupport(D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &Address, sizeof(Address));
    Check.Report("gpuva", "0x%08lX %u %u", hr, Address.MaxGPUVirtualAddressBitsPerResource,
                 Address.MaxGPUVirtualAddressBitsPerProcess);
    hr = Device->CheckFeatureSupport(D3D11_FEATURE_SHADER_CACHE, &Cache, sizeof(Cache));
    Check.Report("shader_cache", "0x%08lX 0x%X", hr, Cache.SupportFlags);

    for (DXGI_FORMAT Format : Formats)
    {
        Support = 0;
        hr = Device->CheckFormatSupport(Format, &Support);
        sprintf_s(Key, "format_%u", Format);
        Check.Report(Key, "0x%08lX 0x%08X", hr, Support);

        Quality = 0;
        hr = Device->CheckMultisampleQualityLevels(Format, 4, &Quality);
        sprintf_s(Key, "msaa4_%u", Format);
        Check.Report(Key, "0x%08lX %u", hr, Quality);
    }

    Device->GetImmediateContext(&Context);
    Check.Report("context_type", "%d", Context->GetType());
    Check.Report("context_flags", "0x%X", Context->GetContextFlags());

    Context.Reset();
    On12.Reset();
    Back.Reset();
    Device.Reset();
    Check.Report("refs_after", "device +%lu queue +%lu", OnCheck::Count(Check.Device.Get()) - DeviceCount,
                 OnCheck::Count(Check.Queue.Get()) - QueueCount);
}

static
VOID
OcPrintTrace(
    _In_ OnCheck& Check,
    _In_z_ PCSTR Key,
    _In_ const std::vector<std::string>& Lines)
{
    UINT Index = 0;
    char Name[64];

    Check.Report(Key, "%u calls", static_cast<UINT>(Lines.size()));
    for (const std::string& Line : Lines)
    {
        sprintf_s(Name, "%s.%03u", Key, Index);
        Check.Report(Name, "%s", Line.c_str());
        Index += 1;
    }
}

/* trace_create: the D3D12 calls device creation and destruction make */
static
VOID
OcCheckTraceCreate(
    _In_ OnCheck& Check)
{
    ComPtr<ID3D11Device> Device;
    HRESULT hr;

    Tr12Reset();
    Tr12Begin();
    hr = Check.Make(D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS, &Device);
    OcPrintTrace(Check, "create", Tr12End());
    Check.Report("hr", "0x%08lX", hr);

    Tr12Begin();
    Device.Reset();
    OcPrintTrace(Check, "destroy", Tr12End());
}

/* A committed D3D12 texture or buffer the wrap checks hand to D3D11 */
static
HRESULT
OcMakeResource(
    _In_ OnCheck& Check,
    _In_ BOOLEAN IsBuffer,
    _In_ D3D12_RESOURCE_FLAGS Flags,
    _In_ D3D12_RESOURCE_STATES State,
    _Outptr_ ID3D12Resource** Resource)
{
    D3D12_HEAP_PROPERTIES Heap = {};
    D3D12_RESOURCE_DESC Desc = {};

    Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Desc.Dimension = IsBuffer ? D3D12_RESOURCE_DIMENSION_BUFFER : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = IsBuffer ? 4096 : 64;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = IsBuffer ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = IsBuffer ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;
    Desc.Flags = Flags;
    if (!IsBuffer)
        Desc.Height = 32;

    return Check.Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, State, nullptr,
                                                 IID_PPV_ARGS(Resource));
}

/* wrap: CreateWrappedResource, ReleaseWrappedResources and AcquireWrappedResources rules */
static
VOID
OcCheckWrap(
    _In_ OnCheck& Check)
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11On12Device> On12;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D12Resource> Texture;
    ComPtr<ID3D12Resource> Buffer;
    ComPtr<ID3D11Texture2D> Wrapped;
    ComPtr<ID3D11Buffer> WrappedBuffer;
    ComPtr<ID3D11Resource> Plain;
    D3D11_RESOURCE_FLAGS Flags = {};
    D3D11_TEXTURE2D_DESC Desc = {};
    D3D11_BUFFER_DESC BufferDesc = {};
    ID3D11Resource* List[1];
    HRESULT hr;

    Tr12Reset();
    if (FAILED(Check.Make(D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS, &Device)))
        return;
    Device.As(&On12);
    Device->GetImmediateContext(&Context);

    OcMakeResource(Check, FALSE, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON, &Texture);
    OcMakeResource(Check, TRUE, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, &Buffer);
    Tr12Name(Texture.Get(), "tex");
    Tr12Name(Buffer.Get(), "buf");

    /* A null resource is left out: the system layer dereferences it */
    Flags.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    hr = On12->CreateWrappedResource(Texture.Get(), &Flags, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                     D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&WrappedBuffer));
    Check.Report("wrong_iid", "0x%08lX %d", hr, WrappedBuffer == nullptr);

    Flags.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    hr = On12->CreateWrappedResource(Texture.Get(), &Flags, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                     D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&Wrapped));
    Check.Report("uav_without_flag", "0x%08lX", hr);
    Wrapped.Reset();

    Tr12Begin();
    Flags.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    hr = On12->CreateWrappedResource(Texture.Get(), &Flags, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                     D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&Wrapped));
    OcPrintTrace(Check, "wrap", Tr12End());
    Check.Report("texture", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Wrapped->GetDesc(&Desc);
    Check.Report("texture_desc", "%ux%u mips %u array %u fmt %u ms %u/%u usage %u bind 0x%X cpu 0x%X misc 0x%X",
                 Desc.Width, Desc.Height, Desc.MipLevels, Desc.ArraySize, Desc.Format, Desc.SampleDesc.Count,
                 Desc.SampleDesc.Quality, Desc.Usage, Desc.BindFlags, Desc.CPUAccessFlags, Desc.MiscFlags);

    Flags.BindFlags = 0;
    hr = On12->CreateWrappedResource(Buffer.Get(), &Flags, D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_STATE_COMMON, IID_PPV_ARGS(&WrappedBuffer));
    Check.Report("buffer", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        WrappedBuffer->GetDesc(&BufferDesc);
        Check.Report("buffer_desc", "%u usage %u bind 0x%X cpu 0x%X misc 0x%X stride %u", BufferDesc.ByteWidth,
                     BufferDesc.Usage, BufferDesc.BindFlags, BufferDesc.CPUAccessFlags, BufferDesc.MiscFlags,
                     BufferDesc.StructureByteStride);
    }

    List[0] = Wrapped.Get();
    Tr12Begin();
    On12->ReleaseWrappedResources(List, 1);
    Context->Flush();
    OcPrintTrace(Check, "release", Tr12End());

    Tr12Begin();
    On12->AcquireWrappedResources(List, 1);
    Context->Flush();
    OcPrintTrace(Check, "acquire", Tr12End());

    Tr12Begin();
    Wrapped.Reset();
    WrappedBuffer.Reset();
    Context->Flush();
    OcPrintTrace(Check, "drop", Tr12End());
}

typedef VOID (*PFN_OC_CHECK)(OnCheck&);

struct CheckInfo
{
    PCSTR Name;
    PFN_OC_CHECK Run;
};

static const CheckInfo Checks[] =
{
    { "create", OcCheckCreate },
    { "device", OcCheckDevice },
    { "trace_create", OcCheckTraceCreate },
    { "wrap", OcCheckWrap },
};

/* System32 for "system", the ReactX build next to this exe for "reactx", else a directory */
static
std::wstring
OcRuntimeDirectory(
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
    WCHAR System[MAX_PATH];
    WCHAR Loaded[MAX_PATH];
    PFN_D3D12_CREATE_DEVICE CreateD3D12;
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    HMODULE Layer;
    HMODULE D3d11;
    OnCheck Check;
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

    GetSystemDirectoryW(System, _countof(System));

    Layer = LoadLibraryExW((OcRuntimeDirectory(Runtime) + L"d3d11on12.dll").c_str(), NULL,
                           LOAD_WITH_ALTERED_SEARCH_PATH);
    if (Layer == NULL)
    {
        fprintf(stderr, "error: cannot load d3d11on12.dll\n");
        return 1;
    }
    GetModuleFileNameW(Layer, Loaded, _countof(Loaded));
    fprintf(stderr, "layer: %ls\n", Loaded);

    CreateD3D12 = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(
        GetProcAddress(LoadLibraryW((std::wstring(System) + L"\\d3d12.dll").c_str()), "D3D12CreateDevice"));
    D3d11 = LoadLibraryW((std::wstring(System) + L"\\d3d11.dll").c_str());
    Check.CreateDevice = reinterpret_cast<PFN_D3D11ON12_CREATE_DEVICE>(GetProcAddress(D3d11, "D3D11On12CreateDevice"));
    if (CreateD3D12 == nullptr || Check.CreateDevice == nullptr)
    {
        fprintf(stderr, "error: missing exports\n");
        return 1;
    }

    if (FAILED(CreateD3D12(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Check.Device))))
    {
        fprintf(stderr, "error: no D3D12 device\n");
        return 1;
    }

    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check.Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Check.Queue));
    Tr12Install(Check.Device.Get(), Check.Queue.Get());

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

    GetModuleFileNameW(GetModuleHandleW(L"d3d11on12.dll"), Loaded, _countof(Loaded));
    fprintf(stderr, "layer at exit: %ls\n", Loaded);
    printf("failures = 0\n");
    return 0;
}
