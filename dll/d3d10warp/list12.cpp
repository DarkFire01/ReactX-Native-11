/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 command lists: recording, copies, clears and barriers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device12.hpp"

static
RxWarp12CommandList *
RxWarp12GetList(
    _In_ D3D12DDI_HCOMMANDLIST List)
{
    return RxWarpObject<RxWarp12CommandList>(List);
}

static constexpr UINT RxWarp12PoolSize = 16384;

/* Records a command; the queue runs them in order */
template <typename Work>
static
VOID
RxWarp12Record(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ Work &&Command)
{
    RxWarp12CommandList *Object = RxWarp12GetList(List);
    RxWarp12CommandPool *Pool = Object->Recorder ? Object->Recorder->Pool : nullptr;

    /* Pools take their kernel memory when the first command lands in them */
    if (Pool != nullptr && Pool->Allocation == 0)
    {
        RX_WARP_ALLOCATION_DATA Data = {};

        Data.Dimension = D3D10DDIRESOURCE_BUFFER;
        Data.Width = RxWarp12PoolSize;
        Data.Height = 1;
        Data.Depth = 1;
        Data.RowPitch = RxWarp12PoolSize;
        Data.Size = RxWarp12PoolSize;
        Data.ArraySize = 1;
        Data.MipLevels = 1;
        Data.Flags = RX_WARP_ALLOCATION_LINEAR;
        RxWarp12AllocateKernel(Object->Device, &Data, nullptr, FALSE, &Pool->Allocation, &Pool->Memory);
    }

    try
    {
        Object->Commands.emplace_back(std::forward<Work>(Command));
    }
    catch (...)
    {
        RX_WARP_TRACE("D3D12 command list out of memory\n");
        Object->Device->Callbacks->pfnSetCommandListErrorCb(Object->RuntimeList, E_OUTOFMEMORY);
    }
}

/* Newer tables pass the application's object; its bypass header leads to ours */
static
D3D12DDI_HCOMMANDLIST
RxWarp12DriverList(
    _In_ D3D12DDI_HCOMMANDLIST List)
{
    return List;
}

static
D3D12DDI_HCOMMANDLIST
RxWarp12DriverList(
    _In_ D3D12DDI_API_HCOMMANDLIST List)
{
    return GetDriverCommandListHandle(List);
}

/* State the executor does not read yet; recording it still lands in the pool */
template <typename Function>
struct RxWarp12StateOf;

template <typename Handle, typename... Arguments>
struct RxWarp12StateOf<VOID (APIENTRY *)(Handle, Arguments...)>
{
    static
    VOID
    APIENTRY
    Run(
        _In_ Handle List,
        _In_ Arguments...)
    {
        RxWarpLock Lock;

        RxWarp12Record(RxWarp12DriverList(List), []()
        {
        });
    }
};

template <typename Function>
static
VOID
RxWarp12State(
    _Out_ Function *Slot)
{
    *Slot = &RxWarp12StateOf<Function>::Run;
}

static
HRESULT
APIENTRY
RxWarp12CreateCommandList(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_COMMAND_LIST_0040 *Args,
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HRTCOMMANDLIST RuntimeList)
{
    RxWarpLock Lock;
    RxWarp12Device *This = RxWarpObject<RxWarp12Device>(Device);
    RxWarp12CommandList *Object;
    D3D12DDI_HRTTABLE Table = {};
    UINT TableIndex = (Args->Type == D3D12DDI_COMMAND_LIST_TYPE_BUNDLE) ? 1 : 0;

    Object = new (std::nothrow) RxWarp12CommandList();
    RxWarpBind(List, Object);
    if (Object == nullptr)
    {
        RX_WARP_TRACE("D3D12 command list allocation failed\n");
        return E_OUTOFMEMORY;
    }
    Object->Device = This;
    Object->RuntimeList = RuntimeList;
    Object->Type = Args->Type;

    /* The runtime learns which of its tables this list calls through */
    Table.handle = This->Adapter->CommandListTables[TableIndex];
    This->Callbacks->pfnSetCommandListDDITableCb(RuntimeList, Table);
    return S_OK;
}

static
VOID
APIENTRY
RxWarp12DestroyCommandList(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HCOMMANDLIST List)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarp12GetList(List);
}

static
VOID
APIENTRY
RxWarp12CloseCommandList(
    _In_ D3D12DDI_HCOMMANDLIST List)
{
    UNREFERENCED_PARAMETER(List);
}

static
VOID
APIENTRY
RxWarp12ResetCommandList(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ const D3D12DDIARG_RESETCOMMANDLIST_0040 *Args)
{
    RxWarpLock Lock;

    RxWarp12CommandList *Object = RxWarp12GetList(List);

    Object->Commands.clear();
    Object->Recorder = Args->hDrvCommandRecorder.pDrvPrivate ? RxWarpObject<RxWarp12CommandRecorder>(Args->hDrvCommandRecorder) : nullptr;
}

/* Commands live in the list until it is reset; the recorder names the pool that backs them */
static
HRESULT
APIENTRY
RxWarp12CreateCommandRecorder(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 *Args,
    _In_ D3D12DDI_HCOMMANDRECORDER_0040 Recorder)
{
    RxWarpLock Lock;
    RxWarp12CommandRecorder *Object = new (std::nothrow) RxWarp12CommandRecorder();

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Args);
    RxWarpBind(Recorder, Object);
    return Object ? S_OK : E_OUTOFMEMORY;
}

static
VOID
APIENTRY
RxWarp12DestroyCommandRecorder(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HCOMMANDRECORDER_0040 Recorder)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarp12CommandRecorder>(Recorder);
}

static
VOID
APIENTRY
RxWarp12CommandRecorderSetCommandPoolAsTarget(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HCOMMANDRECORDER_0040 Recorder,
    _In_ D3D12DDI_HCOMMANDPOOL_0040 Pool)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    RxWarpObject<RxWarp12CommandRecorder>(Recorder)->Pool = RxWarpObject<RxWarp12CommandPool>(Pool);
}

/* A run of texel rows: a subresource of a texture or a footprint in a buffer */
typedef struct _RX_WARP12_SURFACE
{
    BYTE *Base;
    DXGI_FORMAT Format;
    UINT Width;
    UINT Height;
    UINT Depth;
    SIZE_T RowPitch;
    SIZE_T SlicePitch;
    UINT TexelStride;       /* depth stencil planes: bytes per texel of the whole format */
    UINT TexelOffset;       /* where the plane starts inside each texel */
    UINT TexelBytes;        /* bytes the plane covers */
    UINT TexelMask;         /* bits of those bytes the plane owns */
} RX_WARP12_SURFACE;

/* Depth stencil formats interleave their planes; plane 0 is depth, plane 1 stencil */
static
BOOLEAN
RxWarp12PlaneLayout(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Plane,
    _Inout_ RX_WARP12_SURFACE *Surface)
{
    switch (Format)
    {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
            Surface->TexelStride = 4;
            Surface->TexelOffset = Plane ? 3 : 0;
            Surface->TexelBytes = Plane ? 1 : 4;
            Surface->TexelMask = Plane ? 0xFF : 0x00FFFFFF;
            return Plane < 2;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
            Surface->TexelStride = 8;
            Surface->TexelOffset = Plane ? 4 : 0;
            Surface->TexelBytes = Plane ? 1 : 4;
            Surface->TexelMask = Plane ? 0xFF : 0xFFFFFFFF;
            return Plane < 2;
        default:
            return Plane == 0;
    }
}

static
BOOLEAN
RxWarp12Surface(
    _In_ const D3D12DDIARG_BUFFER_PLACEMENT *Placement,
    _In_ const D3D12DDIARG_PLACED_RESOURCE &Layout,
    _Out_ RX_WARP12_SURFACE *Surface)
{
    RxWarp12Resource *Resource = RxWarp12GetResource(Placement->BaseAddress.UMD.hResource);

    memset(Surface, 0, sizeof(*Surface));
    if (Resource == nullptr || Resource->Storage.Memory == nullptr)
        return FALSE;

    if (Layout.Layout == D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED)
    {
        const D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT *Pitched =
            static_cast<const D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT *>(Layout.pLayout);

        Surface->Base = Resource->Storage.Memory + Placement->BaseAddress.UMD.Offset;
        Surface->Format = Pitched->Format;
        Surface->Width = Pitched->PhysicalWidth;
        Surface->Height = Pitched->PhysicalHeight;
        Surface->Depth = Pitched->PhysicalDepth;
        Surface->RowPitch = Pitched->Pitch;
        Surface->SlicePitch = Pitched->SlicePitch;
        return TRUE;
    }
    if (Layout.Layout == D3D12DDI_RL_SELECT_SUBRESOURCE)
    {
        UINT Index = (UINT)Placement->BaseAddress.UMD.Offset;
        UINT Count = Resource->Storage.SubresourceCount;
        RX_WARP_SUBRESOURCE *Sub = Count ? Resource->Storage.Subresource(Index % Count) : nullptr;

        if (Sub == nullptr || !RxWarp12PlaneLayout(Resource->Storage.Format, Index / Count, Surface))
            return FALSE;
        Surface->Base = Resource->Storage.Data(Index % Count);
        Surface->Format = Resource->Storage.Format;
        Surface->Width = Sub->Width;
        Surface->Height = Sub->Height;
        Surface->Depth = Sub->Depth;
        Surface->RowPitch = Sub->RowPitch;
        Surface->SlicePitch = Sub->DepthPitch;
        return TRUE;
    }
    return FALSE;
}

/* Texel by texel between a depth stencil plane and a tightly packed surface */
static
VOID
RxWarp12CopyPlane(
    _In_ const RX_WARP12_SURFACE &To,
    _In_ UINT X,
    _In_ UINT Y,
    _In_ UINT Z,
    _In_ const RX_WARP12_SURFACE &From,
    _In_ const D3D12DDI_BOX &Box)
{
    const RX_WARP12_SURFACE &Plane = To.TexelStride ? To : From;
    UINT ToStride = To.TexelStride ? To.TexelStride : Plane.TexelBytes;
    UINT FromStride = From.TexelStride ? From.TexelStride : Plane.TexelBytes;

    for (UINT Slice = 0; Slice < (UINT)(Box.Back - Box.Front); Slice++)
    {
        for (UINT Row = 0; Row < (UINT)(Box.Bottom - Box.Top); Row++)
        {
            BYTE *Target = To.Base + (Z + Slice) * To.SlicePitch + (Y + Row) * To.RowPitch + (SIZE_T)X * ToStride + To.TexelOffset;
            const BYTE *Source = From.Base + (Box.Front + Slice) * From.SlicePitch + (Box.Top + Row) * From.RowPitch +
                                 (SIZE_T)Box.Left * FromStride + From.TexelOffset;

            for (UINT Column = 0; Column < (UINT)(Box.Right - Box.Left); Column++)
            {
                UINT Old = 0;
                UINT New = 0;

                memcpy(&Old, Target + (SIZE_T)Column * ToStride, Plane.TexelBytes);
                memcpy(&New, Source + (SIZE_T)Column * FromStride, Plane.TexelBytes);

                /* Packed side gets only the plane's bits; the interleaved side keeps the other plane */
                if (To.TexelStride)
                    New = (Old & ~Plane.TexelMask) | (New & Plane.TexelMask);
                else
                    New &= Plane.TexelMask;
                memcpy(Target + (SIZE_T)Column * ToStride, &New, Plane.TexelBytes);
            }
        }
    }
}

/* Copies a box of texels; block formats move whole blocks */
static
VOID
RxWarp12CopySurface(
    _In_ const RX_WARP12_SURFACE &To,
    _In_ UINT X,
    _In_ UINT Y,
    _In_ UINT Z,
    _In_ const RX_WARP12_SURFACE &From,
    _In_ const D3D12DDI_BOX &Box)
{
    SIZE_T Bytes = RxWarpFormatRowBytes(From.Format, Box.Right) - RxWarpFormatRowBytes(From.Format, Box.Left);
    UINT Rows = RxWarpFormatRows(From.Format, Box.Bottom) - RxWarpFormatRows(From.Format, Box.Top);
    SIZE_T FromX = RxWarpFormatRowBytes(From.Format, Box.Left);
    SIZE_T ToX = RxWarpFormatRowBytes(To.Format, X);
    UINT FromY = RxWarpFormatRows(From.Format, Box.Top);
    UINT ToY = RxWarpFormatRows(To.Format, Y);

    if (To.TexelStride != 0 || From.TexelStride != 0)
    {
        RxWarp12CopyPlane(To, X, Y, Z, From, Box);
        return;
    }
    for (UINT Slice = 0; Slice < Box.Back - Box.Front; Slice++)
    {
        for (UINT Row = 0; Row < Rows; Row++)
        {
            memmove(To.Base + (Z + Slice) * To.SlicePitch + (ToY + Row) * To.RowPitch + ToX,
                    From.Base + (Box.Front + Slice) * From.SlicePitch + (FromY + Row) * From.RowPitch + FromX, Bytes);
        }
    }
}

static
VOID
APIENTRY
RxWarp12CopyTextureRegion(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ const D3D12DDIARG_BUFFER_PLACEMENT *Target,
    _In_ D3D12DDIARG_PLACED_RESOURCE TargetLayout,
    _In_ UINT X,
    _In_ UINT Y,
    _In_ UINT Z,
    _In_ const D3D12DDIARG_BUFFER_PLACEMENT *Source,
    _In_ D3D12DDIARG_PLACED_RESOURCE SourceLayout,
    _In_opt_ const D3D12DDI_BOX *Box)
{
    RxWarpLock Lock;
    RX_WARP12_SURFACE To;
    RX_WARP12_SURFACE From;
    D3D12DDI_BOX Region;

    /* Layouts point into the runtime's argument memory, so take what they say now */
    if (!RxWarp12Surface(Target, TargetLayout, &To) || !RxWarp12Surface(Source, SourceLayout, &From))
    {
        RX_WARP_TRACE("D3D12 CopyTextureRegion with layouts %u and %u not handled\n", TargetLayout.Layout, SourceLayout.Layout);
        return;
    }
    if (Box != nullptr)
    {
        Region = *Box;
    }
    else
    {
        Region.Left = 0;
        Region.Top = 0;
        Region.Front = 0;
        Region.Right = From.Width;
        Region.Bottom = From.Height;
        Region.Back = From.Depth;
    }
    RxWarp12Record(List, [To, X, Y, Z, From, Region]()
    {
        RxWarp12CopySurface(To, X, Y, Z, From, Region);
    });
}

static
VOID
APIENTRY
RxWarp12CopyBufferRegion(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDIARG_BUFFER_PLACEMENT Target,
    _In_ D3D12DDIARG_BUFFER_PLACEMENT Source,
    _In_ UINT64 Bytes)
{
    RxWarpLock Lock;
    RxWarp12Resource *To = RxWarp12GetResource(Target.BaseAddress.UMD.hResource);
    RxWarp12Resource *From = RxWarp12GetResource(Source.BaseAddress.UMD.hResource);
    UINT64 ToOffset = Target.BaseAddress.UMD.Offset;
    UINT64 FromOffset = Source.BaseAddress.UMD.Offset;

    if (To == nullptr || From == nullptr)
        return;
    RxWarp12Record(List, [To, From, ToOffset, FromOffset, Bytes]()
    {
        if (To->Storage.Memory != nullptr && From->Storage.Memory != nullptr)
            memmove(To->Storage.Memory + ToOffset, From->Storage.Memory + FromOffset, (SIZE_T)Bytes);
    });
}

/* Identical descriptions share one layout, so the whole storage moves at once */
static
VOID
APIENTRY
RxWarp12ResourceCopy(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HRESOURCE Target,
    _In_ D3D12DDI_HRESOURCE Source)
{
    RxWarpLock Lock;
    RxWarp12Resource *To = RxWarp12GetResource(Target);
    RxWarp12Resource *From = RxWarp12GetResource(Source);

    if (To == nullptr || From == nullptr)
        return;
    RxWarp12Record(List, [To, From]()
    {
        SIZE_T Bytes = (From->Type == D3D12DDI_RT_BUFFER) ? (SIZE_T)From->Width : From->Storage.MemorySize;

        if (To->Storage.Memory != nullptr && From->Storage.Memory != nullptr)
            memmove(To->Storage.Memory, From->Storage.Memory, Bytes);
    });
}

/* Barriers order work on a GPU; here every command already finished before the next starts */
static
VOID
APIENTRY
RxWarp12ResourceBarrier(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ UINT Count,
    _In_reads_(Count) const D3D12DDIARG_RESOURCE_BARRIER_0022 *Barriers)
{
    UNREFERENCED_PARAMETER(List);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Barriers);
}

/* A clear value carried by a recorded command */
typedef struct _RX_WARP12_ELEMENT
{
    BYTE Bytes[16];
} RX_WARP12_ELEMENT;

static
RX_WARP12_ELEMENT
RxWarp12Element(
    _In_reads_bytes_(16) const BYTE *Bytes)
{
    RX_WARP12_ELEMENT Value;

    memcpy(Value.Bytes, Bytes, sizeof(Value.Bytes));
    return Value;
}

/* Rectangles are copied with the command; none means the whole view */
static
std::vector<D3D10_DDI_RECT>
RxWarp12Rects(
    _In_ UINT Count,
    _In_reads_(Count) const D3D12DDI_RECT *Rects)
{
    std::vector<D3D10_DDI_RECT> Copy(Count);

    for (UINT Index = 0; Index < Count; Index++)
    {
        Copy[Index].left = Rects[Index].left;
        Copy[Index].top = Rects[Index].top;
        Copy[Index].right = Rects[Index].right;
        Copy[Index].bottom = Rects[Index].bottom;
    }
    return Copy;
}

static
VOID
APIENTRY
RxWarp12ClearRenderTargetView(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Descriptor,
    _In_ const FLOAT Color[4],
    _In_ UINT RectCount,
    _In_reads_(RectCount) const D3D12DDI_RECT *Rects)
{
    RxWarpLock Lock;
    RxWarpView View = *RxWarp12TargetView(Descriptor);
    std::vector<D3D10_DDI_RECT> Region = RxWarp12Rects(RectCount, Rects);
    BYTE Element[16];

    if (!RxWarpEncodeClearColor(View.Format, Color, Element))
        return;
    RxWarp12Record(List, [View, Region, Value = RxWarp12Element(Element)]() mutable
    {
        RxWarpFillView(&View, Value.Bytes, RxWarpAllBitsMask(), Region.empty() ? nullptr : Region.data(), (UINT)Region.size());
    });
}

static
VOID
APIENTRY
RxWarp12ClearDepthStencilView(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Descriptor,
    _In_ UINT Flags,
    _In_ FLOAT Depth,
    _In_ UINT8 Stencil,
    _In_ UINT RectCount,
    _In_reads_(RectCount) const D3D12DDI_RECT *Rects)
{
    RxWarpLock Lock;
    RxWarpView View = *RxWarp12TargetView(Descriptor);
    std::vector<D3D10_DDI_RECT> Region = RxWarp12Rects(RectCount, Rects);

    RxWarp12Record(List, [View, Region, Flags, Depth, Stencil]() mutable
    {
        RxWarpClearDepthStencil(&View, Flags, Depth, Stencil, Region.empty() ? nullptr : Region.data(), (UINT)Region.size());
    });
}

/* Unordered access clears read the view from the CPU descriptor the runtime keeps beside the GPU one */
static
VOID
APIENTRY
RxWarp12ClearUnorderedAccessViewUint(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_GPU_DESCRIPTOR_HANDLE GpuDescriptor,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE CpuDescriptor,
    _In_ D3D12DDI_HRESOURCE Resource,
    _In_ const UINT Values[4],
    _In_ UINT RectCount,
    _In_reads_(RectCount) const D3D12DDI_RECT *Rects)
{
    RxWarpLock Lock;
    RxWarpView View = RxWarp12Descriptor(CpuDescriptor.ptr)->View;
    std::vector<D3D10_DDI_RECT> Region = RxWarp12Rects(RectCount, Rects);
    BYTE Element[16];

    UNREFERENCED_PARAMETER(GpuDescriptor);
    UNREFERENCED_PARAMETER(Resource);

    /* Raw and structured buffers repeat the first value in every DWORD */
    if (View.Format == DXGI_FORMAT_UNKNOWN || View.Format == DXGI_FORMAT_R32_TYPELESS)
    {
        for (UINT Index = 0; Index < 4; Index++)
            memcpy(Element + Index * 4, &Values[0], 4);
    }
    else if (!RxWarpEncodeUint(View.Format, Values, Element))
    {
        return;
    }
    RxWarp12Record(List, [View, Region, Value = RxWarp12Element(Element)]() mutable
    {
        RxWarpFillView(&View, Value.Bytes, RxWarpAllBitsMask(), Region.empty() ? nullptr : Region.data(), (UINT)Region.size());
    });
}

static
VOID
APIENTRY
RxWarp12ClearUnorderedAccessViewFloat(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_GPU_DESCRIPTOR_HANDLE GpuDescriptor,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE CpuDescriptor,
    _In_ D3D12DDI_HRESOURCE Resource,
    _In_ const FLOAT Values[4],
    _In_ UINT RectCount,
    _In_reads_(RectCount) const D3D12DDI_RECT *Rects)
{
    RxWarpLock Lock;
    RxWarpView View = RxWarp12Descriptor(CpuDescriptor.ptr)->View;
    std::vector<D3D10_DDI_RECT> Region = RxWarp12Rects(RectCount, Rects);
    BYTE Element[16];

    UNREFERENCED_PARAMETER(GpuDescriptor);
    UNREFERENCED_PARAMETER(Resource);
    if (!RxWarpEncodeClearColor(View.Format, Values, Element))
        return;
    RxWarp12Record(List, [View, Region, Value = RxWarp12Element(Element)]() mutable
    {
        RxWarpFillView(&View, Value.Bytes, RxWarpAllBitsMask(), Region.empty() ? nullptr : Region.data(), (UINT)Region.size());
    });
}

static
VOID
APIENTRY
RxWarp12DiscardResource(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HRESOURCE Resource,
    _In_opt_ const D3D12DDIARG_DISCARD_RESOURCE_0003 *Args)
{
    UNREFERENCED_PARAMETER(List);
    UNREFERENCED_PARAMETER(Resource);
    UNREFERENCED_PARAMETER(Args);
}

VOID
RxWarp12FillListTable(
    _Inout_opt_ D3D12DDI_DEVICE_FUNCS_CORE_0116 *Device,
    _Inout_opt_ D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 *List)
{
    if (Device != nullptr)
    {
        Device->pfnCreateCommandList = RxWarp12CreateCommandList;
        Device->pfnDestroyCommandList = RxWarp12DestroyCommandList;
        Device->pfnCreateCommandRecorder = RxWarp12CreateCommandRecorder;
        Device->pfnDestroyCommandRecorder = RxWarp12DestroyCommandRecorder;
        Device->pfnCommandRecorderSetCommandPoolAsTarget = RxWarp12CommandRecorderSetCommandPoolAsTarget;
    }
    if (List == nullptr)
        return;
    RxWarp12State(&List->pfnIaSetTopology);
    RxWarp12State(&List->pfnRsSetViewports);
    RxWarp12State(&List->pfnRsSetScissorRects);
    RxWarp12State(&List->pfnOmSetBlendFactor);
    RxWarp12State(&List->pfnOmSetStencilRef);
    RxWarp12State(&List->pfnSetPipelineState);
    RxWarp12State(&List->pfnSetDescriptorHeaps);
    RxWarp12State(&List->pfnSetComputeRootSignature);
    RxWarp12State(&List->pfnSetGraphicsRootSignature);
    RxWarp12State(&List->pfnIASetIndexBuffer);
    RxWarp12State(&List->pfnIASetVertexBuffers);
    RxWarp12State(&List->pfnSOSetTargets);
    RxWarp12State(&List->pfnOMSetRenderTargets);
    RxWarp12State(&List->pfnOMSetDepthBounds);
    RxWarp12State(&List->pfnSetPredication);
    RxWarp12State(&List->pfnRSSetShadingRate);
    RxWarp12State(&List->pfnRSSetShadingRateImage);
    RxWarp12State(&List->pfnSetViewInstanceMask);
    RxWarp12State(&List->pfnSetSamplePositions);
    RxWarp12State(&List->pfnIASetIndexBufferStripCutValue);
    RxWarp12State(&List->pfnRSSetDepthBias);
    RxWarp12State(&List->pfnOmSetFrontAndBackStencilRef);
    List->pfnCloseCommandList = RxWarp12CloseCommandList;
    List->pfnResetCommandList = RxWarp12ResetCommandList;
    List->pfnCopyTextureRegion = RxWarp12CopyTextureRegion;
    List->pfnCopyBufferRegion = RxWarp12CopyBufferRegion;
    List->pfnResourceCopy = RxWarp12ResourceCopy;
    List->pfnResourceBarrier = RxWarp12ResourceBarrier;
    List->pfnClearRenderTargetView = RxWarp12ClearRenderTargetView;
    List->pfnClearDepthStencilView = RxWarp12ClearDepthStencilView;
    List->pfnClearUnorderedAccessViewUint = RxWarp12ClearUnorderedAccessViewUint;
    List->pfnClearUnorderedAccessViewFloat = RxWarp12ClearUnorderedAccessViewFloat;
    List->pfnDiscardResource = RxWarp12DiscardResource;
}
