/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The Direct3D 12 device and its DDI tables
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <d3d12umddi.h>
#include <functional>
#include <vector>
#include "device.hpp"

class RxWarpAdapter;
class RxWarp12Resource;
class RxWarp12CommandRecorder;
class RxWarp12Replay;

class RxWarp12Device
{
public:
    static
    HRESULT
    Create(
        _In_ RxWarpAdapter *Adapter,
        _In_ const D3D12DDIARG_CREATEDEVICE_0003 *Args);

    static
    VOID
    APIENTRY
    Destroy(
        _In_ D3D12DDI_HDEVICE Device);

    /* The newest table of each kind; older interfaces take a prefix of it */
    static const D3D12DDI_DEVICE_FUNCS_CORE_0116 *DeviceTable();
    static const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 *CommandListTable();
    static const D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *CommandQueueTable();
    static const D3D12DDI_EXTENDED_FEATURES_FUNCS_0096 *ExtendedFeaturesTable();
    static const DXGI1_4_DDI_BASE_FUNCTIONS *DxgiTable();

    RxWarpAdapter *Adapter = nullptr;
    D3D12DDI_HRTDEVICE RuntimeDevice = {};
    UINT Interface = 0;
    UINT Version = 0;
    D3D12DDI_CREATE_DEVICE_FLAGS Flags = D3D12DDI_CREATE_DEVICE_FLAG_NONE;
    const D3DDDI_DEVICECALLBACKS *KernelCallbacks = nullptr;
    const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *Callbacks = nullptr;
    VOID *ContentProtectionCallback = nullptr;

    /* The D3D11 executor recorded work runs on */
    RxWarpDevice *Executor = nullptr;

    /* Buffers by GPU address: address bits 40 and up are the index */
    std::vector<RxWarp12Resource *> Buffers;
    BOOLEAN Registered = FALSE;     /* the protocol revision went to the kernel driver */
    D3DKMT_HANDLE PageAllocation = 0;
    VOID *Page = nullptr;

    /* Every runtime that reaches us passes callbacks of at least this revision */
    const D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 *
    Callbacks0022() const
    {
        return reinterpret_cast<const D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 *>(Callbacks);
    }
};

/* Memory the runtime pages: one kernel allocation, or our own pages when there is no kernel driver */
class RxWarp12Heap
{
public:
    RxWarp12Device *Device = nullptr;
    UINT64 Size = 0;
    BYTE *Memory = nullptr;
    D3DKMT_HANDLE Allocation = 0;
    HANDLE RuntimeResource = nullptr;
    UINT AllocationFlags = 0;
    D3D12DDI_CPU_PAGE_PROPERTY CpuPage = D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
    D3D12DDI_HEAP_FLAGS Flags = D3D12DDI_HEAP_FLAG_NONE;
};

class RxWarp12Resource
{
public:
    RxWarp12Device *Device = nullptr;
    RxWarpResource Storage;         /* layout and memory as the D3D11 executor sees them */
    RxWarp12Heap *Heap = nullptr;   /* the heap created with this resource, for committed ones */
    D3D12DDI_RESOURCE_TYPE Type = D3D12DDI_RT_BUFFER;
    UINT64 Width = 0;
    UINT Height = 0;
    UINT16 DepthOrArraySize = 1;
    UINT16 MipLevels = 1;
    D3D12DDI_RESOURCE_FLAGS_0003 Flags = D3D12DDI_RESOURCE_FLAG_0003_NONE;
    D3D12DDI_TEXTURE_LAYOUT Layout = D3D12DDI_TL_UNDEFINED;
    UINT Index = 0;                 /* slot in the device's buffer table, 0 for none */
    D3DKMT_HANDLE ShadowAllocation = 0; /* eight bit copy presented in place of other formats */
    BYTE *ShadowMemory = nullptr;
};

/* Descriptor heaps: CBV_SRV_UAV and sampler heaps hold descriptors in place, the others a pointer to a view */
class RxWarp12DescriptorHeap
{
public:
    RxWarp12Device *Device = nullptr;
    D3DKMT_HANDLE Allocation = 0;
    D3D12DDI_DESCRIPTOR_HEAP_TYPE Type = D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    UINT Count = 0;
    UINT Stride = 0;
    BYTE *Memory = nullptr;
    RxWarpView *Views = nullptr;
};

typedef enum _RX_WARP12_DESCRIPTOR_KIND
{
    RxWarp12DescriptorNone,
    RxWarp12DescriptorConstants,
    RxWarp12DescriptorShaderResource,
    RxWarp12DescriptorUnorderedAccess,
    RxWarp12DescriptorSampler,
} RX_WARP12_DESCRIPTOR_KIND;

/* What a CBV_SRV_UAV or sampler descriptor holds */
typedef struct _RX_WARP12_DESCRIPTOR
{
    UINT Kind;
    UINT Stride;                    /* structured buffer views */
    union
    {
        RxWarpView View;
        struct
        {
            RxWarpResource *Resource;
            UINT64 Offset;
            UINT Size;
        } Constants;
        RX_WARP_SAMPLER Sampler;
    };
} RX_WARP12_DESCRIPTOR;

static_assert(sizeof(RX_WARP12_DESCRIPTOR) <= 64, "descriptors are 64 bytes");

class RxWarp12Fence
{
public:
    D3D12DDI_FENCE_TYPE_0112 Type = D3D12DDI_FENCE_TYPE_MONITORED;
};

class RxWarp12CommandQueue
{
public:
    RxWarp12Device *Device = nullptr;
    D3D12DDI_HRTCOMMANDQUEUE RuntimeQueue = {};
    D3D12DDI_COMMAND_QUEUE_FLAGS Flags = D3D12DDI_COMMAND_QUEUE_FLAG_NONE;
    HANDLE KernelContext = nullptr;
    D3DKMT_HANDLE ContextAllocation = 0;
    VOID *ContextBuffer = nullptr;
};

/* Recorded work: each command runs on the executor when a queue gets the list */
class RxWarp12CommandList
{
public:
    RxWarp12Device *Device = nullptr;
    D3D12DDI_HRTCOMMANDLIST RuntimeList = {};
    D3D12DDI_COMMAND_LIST_TYPE Type = D3D12DDI_COMMAND_LIST_TYPE_DIRECT;
    RxWarp12CommandRecorder *Recorder = nullptr;
    std::vector<std::function<VOID(RxWarp12Replay *)>> Commands;
};

RxWarp12Resource *
RxWarp12GetResource(
    _In_ D3D12DDI_HRESOURCE Resource);

/* A kernel allocation described by Data, locked for the CPU */
HRESULT
RxWarp12AllocateKernel(
    _In_ RxWarp12Device *Device,
    _In_ RX_WARP_ALLOCATION_DATA *Data,
    _In_opt_ HANDLE RuntimeResource,
    _In_ BOOLEAN Primary,
    _Out_ D3DKMT_HANDLE *Allocation,
    _Out_ VOID **Memory);

VOID
RxWarp12FreeKernel(
    _In_ RxWarp12Device *Device,
    _In_opt_ HANDLE RuntimeResource,
    _In_ D3DKMT_HANDLE Allocation,
    _In_ BOOLEAN Locked);

/* The resource and byte offset a GPU address falls in */
RxWarp12Resource *
RxWarp12ResolveAddress(
    _In_ RxWarp12Device *Device,
    _In_ D3D12DDI_GPU_VIRTUAL_ADDRESS Address,
    _Out_ UINT64 *Offset);

/* The view a render target or depth stencil descriptor stands for */
RxWarpView *
RxWarp12TargetView(
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Handle);

/* The descriptor at a CPU or GPU handle of a CBV_SRV_UAV or sampler heap */
inline
RX_WARP12_DESCRIPTOR *
RxWarp12Descriptor(
    _In_ SIZE_T Handle)
{
    return reinterpret_cast<RX_WARP12_DESCRIPTOR *>(Handle);
}

/* Fills the device table entries each file implements */
VOID
RxWarp12FillResourceTable(
    _Inout_ D3D12DDI_DEVICE_FUNCS_CORE_0116 *Table);

VOID
RxWarp12FillDescriptorTable(
    _Inout_ D3D12DDI_DEVICE_FUNCS_CORE_0116 *Table);

VOID
RxWarp12FillQueueTables(
    _Inout_opt_ D3D12DDI_DEVICE_FUNCS_CORE_0116 *Device,
    _Inout_opt_ D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *Queue,
    _Inout_opt_ D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 *List);

VOID
RxWarp12FillListTable(
    _Inout_opt_ D3D12DDI_DEVICE_FUNCS_CORE_0116 *Device,
    _Inout_opt_ D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 *List);

/* Command allocators: the runtime makes one with every device */
class RxWarp12CommandPool
{
public:
    RxWarp12Device *Device = nullptr;
    D3DKMT_HANDLE Allocation = 0;   /* kernel memory taken on the first recorded command */
    VOID *Memory = nullptr;
};

/* Points command lists at the pool their commands come from */
class RxWarp12CommandRecorder
{
public:
    RxWarp12CommandPool *Pool = nullptr;
};

/* One range of a descriptor table, with appended offsets already resolved */
typedef struct _RX_WARP12_ROOT_RANGE
{
    D3D12DDI_DESCRIPTOR_RANGE_TYPE Kind;
    UINT Base;
    UINT Count;
    UINT Space;
    UINT Offset;
} RX_WARP12_ROOT_RANGE;

typedef struct _RX_WARP12_ROOT_PARAMETER
{
    D3D12DDI_ROOT_PARAMETER_TYPE Type;
    D3D12DDI_SHADER_VISIBILITY Visibility;
    UINT Register;
    UINT Space;
    UINT Values;                    /* root constants: the DWORD count */
    std::vector<RX_WARP12_ROOT_RANGE> Ranges;
} RX_WARP12_ROOT_PARAMETER;

typedef struct _RX_WARP12_STATIC_SAMPLER
{
    RX_WARP_SAMPLER Sampler;
    UINT Register;
    UINT Space;
    D3D12DDI_SHADER_VISIBILITY Visibility;
} RX_WARP12_STATIC_SAMPLER;

static constexpr UINT RxWarp12RootSlots = 64;

/* Root signatures: what each root parameter binds, read when a command list draws */
class RxWarp12RootSignature
{
public:
    RxWarp12Device *Device = nullptr;
    std::vector<RX_WARP12_ROOT_PARAMETER> Parameters;
    std::vector<RX_WARP12_STATIC_SAMPLER> Samplers;
};

/* Pipeline states: the shaders and a copy of every fixed function state they were made with */
class RxWarp12PipelineState
{
public:
    RxWarp12Device *Device = nullptr;
    RxWarpShader *Shaders[RxWarpStageCount] = {};
    RxWarpElementLayout Layout;
    RxWarpBlendState Blend;
    RxWarpDepthStencilState DepthStencil;
    RxWarpRasterizerState Rasterizer;
    UINT SampleMask = UINT_MAX;
    D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE StripCut = D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    BOOLEAN Compute = FALSE;
};

class RxWarp12QueryHeap
{
public:
    RxWarp12Device *Device = nullptr;
    D3DKMT_HANDLE Allocation = 0;   /* kernel pages the reference keeps its query data in */
    VOID *Memory = nullptr;
    D3D12DDI_QUERY_HEAP_TYPE Type = D3D12DDI_QUERY_HEAP_TYPE_OCCLUSION;
    UINT Count = 0;
    std::vector<UINT64> Values;     /* RxWarpCountTotal counters per query, sampled at begin */
    std::vector<UINT64> Results;    /* what ResolveQueryData writes, per query */
};

class RxWarp12CommandSignature
{
public:
    UINT Stride = 0;
    std::vector<D3D12DDI_INDIRECT_ARGUMENT_DESC> Arguments;
    RxWarp12RootSignature *RootSignature = nullptr;
};

/* A buffer bound by GPU address, seen by the executor as a resource of its own */
class RxWarp12Window
{
public:
    RxWarp12Window()
    {
        Storage.Subresources = &Sub;
        Storage.SubresourceCount = 1;
        Storage.Locked = TRUE;
    }

    ~RxWarp12Window()
    {
        Storage.Subresources = nullptr;
        Storage.Memory = nullptr;
    }

    RxWarp12Window(const RxWarp12Window &) = delete;
    RxWarp12Window &operator=(const RxWarp12Window &) = delete;

    /* FALSE when the address names no buffer */
    BOOLEAN
    Open(
        _In_ RxWarp12Device *Device,
        _In_ D3D12DDI_GPU_VIRTUAL_ADDRESS Address,
        _In_ UINT64 Size);

    RxWarpResource Storage;
    RX_WARP_SUBRESOURCE Sub = {};
};

typedef struct _RX_WARP12_ROOT_ARGUMENTS
{
    RxWarp12RootSignature *Signature;
    UINT64 Values[RxWarp12RootSlots];               /* descriptor table handles and root view addresses */
    UINT Constants[RxWarp12RootSlots][64];
} RX_WARP12_ROOT_ARGUMENTS;

typedef struct _RX_WARP12_VERTEX_BUFFER
{
    D3D12DDI_GPU_VIRTUAL_ADDRESS Address;
    UINT Size;
    UINT Stride;
} RX_WARP12_VERTEX_BUFFER;

typedef struct _RX_WARP12_STREAM_TARGET
{
    D3D12DDI_GPU_VIRTUAL_ADDRESS Address;
    UINT64 Size;
    D3D12DDI_GPU_VIRTUAL_ADDRESS Filled;
} RX_WARP12_STREAM_TARGET;

/* The state a command list builds up while it runs on a queue */
class RxWarp12Replay
{
public:
    RxWarp12Device *Device = nullptr;
    RxWarp12PipelineState *Pipeline = nullptr;
    RX_WARP12_ROOT_ARGUMENTS Graphics = {};
    RX_WARP12_ROOT_ARGUMENTS Compute = {};
    D3D12DDI_PRIMITIVE_TOPOLOGY Topology = D3D12DDI_PRIMITIVE_TOPOLOGY_UNDEFINED;
    D3D10_DDI_VIEWPORT Viewports[16] = {};
    UINT ViewportCount = 0;
    D3D10_DDI_RECT Scissors[16] = {};
    UINT ScissorCount = 0;
    FLOAT BlendFactor[4] = {};
    UINT StencilRef = 0;
    RxWarpView Targets[8];
    BOOLEAN TargetBound[8] = {};
    UINT TargetCount = 0;
    RxWarpView Depth;
    BOOLEAN DepthBound = FALSE;
    RX_WARP12_VERTEX_BUFFER Vertices[32] = {};
    D3D12DDI_GPU_VIRTUAL_ADDRESS IndexAddress = 0;
    UINT IndexSize = 0;
    DXGI_FORMAT IndexFormat = DXGI_FORMAT_UNKNOWN;
    RX_WARP12_STREAM_TARGET Streams[4] = {};
    RxWarpResource *PredicateBuffer = nullptr;
    UINT64 PredicateOffset = 0;
    BYTE *ViewHeap = nullptr;               /* shader visible heaps for descriptor heap handles */
    UINT ViewHeapCount = 0;
    BYTE *SamplerHeap = nullptr;
    UINT SamplerHeapCount = 0;
    D3D12DDI_PREDICATION_OP PredicateOp = D3D12DDI_PREDICATION_OP_EQUAL_ZERO;

    /* What a draw or dispatch hands the executor, rebuilt for each one */
    RxWarp12Window RootWindows[RxWarp12RootSlots];
    RxWarpView RootViews[RxWarp12RootSlots];
    RxWarp12Window VertexWindows[32];
    RxWarp12Window IndexWindow;
    RxWarp12Window StreamWindows[4];
    RX_WARP_STAGE_BINDINGS Bindings[RxWarpStageCount];

    /* TRUE when predication skips the command */
    BOOLEAN
    Skipped(VOID) const;
};

typedef std::function<VOID(RxWarp12Replay *)> RX_WARP12_COMMAND;

/* Adds a command to a list; running out of memory is reported to the runtime */
VOID
RxWarp12Append(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _Inout_ RX_WARP12_COMMAND &&Command);

/* Runs a list's commands against a replay state */
VOID
RxWarp12RunList(
    _In_ RxWarp12CommandList *List,
    _Inout_ RxWarp12Replay *State);

VOID
RxWarp12FillPipelineTable(
    _Inout_ D3D12DDI_DEVICE_FUNCS_CORE_0116 *Table);

VOID
RxWarp12FillDrawTable(
    _Inout_ D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 *List);
