/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 descriptor heaps and the views written into them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device12.hpp"

static constexpr UINT RxWarp12FullDescriptor = 64;
static constexpr UINT RxWarp12TargetDescriptor = 8;

static
BOOLEAN
RxWarp12HoldsTargets(
    _In_ D3D12DDI_DESCRIPTOR_HEAP_TYPE Type)
{
    return Type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_RTV || Type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_DSV;
}

static
UINT
APIENTRY
RxWarp12GetDescriptorSizeInBytes(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_DESCRIPTOR_HEAP_TYPE Type)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    return RxWarp12HoldsTargets(Type) ? RxWarp12TargetDescriptor : RxWarp12FullDescriptor;
}

static
HRESULT
APIENTRY
RxWarp12CreateDescriptorHeap(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 *Args,
    _In_ D3D12DDI_HDESCRIPTORHEAP Heap)
{
    RxWarpLock Lock;
    RxWarp12DescriptorHeap *Object;
    UINT Count = Args->NumDescriptors ? Args->NumDescriptors : 1;

    Object = new (std::nothrow) RxWarp12DescriptorHeap();
    RxWarpBind(Heap, Object);
    if (Object == nullptr)
        return E_OUTOFMEMORY;

    Object->Type = Args->Type;
    Object->Count = Args->NumDescriptors;
    Object->Stride = RxWarp12HoldsTargets(Args->Type) ? RxWarp12TargetDescriptor : RxWarp12FullDescriptor;
    /* Shader visible heaps live in kernel memory, whole pages of it */
    if (Args->Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        RX_WARP_ALLOCATION_DATA Data = {};
        UINT Bytes = (UINT)(((SIZE_T)Count * Object->Stride + 4095) & ~(SIZE_T)4095);

        Data.Dimension = D3D10DDIRESOURCE_BUFFER;
        Data.Width = Bytes;
        Data.Height = 1;
        Data.Depth = 1;
        Data.RowPitch = Bytes;
        Data.Size = Bytes;
        Data.ArraySize = 1;
        Data.MipLevels = 1;
        Data.Flags = RX_WARP_ALLOCATION_LINEAR;
        Object->Device = RxWarpObject<RxWarp12Device>(Device);
        if (FAILED(RxWarp12AllocateKernel(Object->Device, &Data, nullptr, FALSE, &Object->Allocation, reinterpret_cast<VOID **>(&Object->Memory))))
            return E_OUTOFMEMORY;
        memset(Object->Memory, 0, Bytes);
    }
    else
    {
        Object->Memory = static_cast<BYTE *>(VirtualAlloc(nullptr, (SIZE_T)Count * Object->Stride, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    }
    if (Object->Memory == nullptr)
    {
        RX_WARP_TRACE("D3D12 descriptor heap of %u descriptors failed\n", Count);
        return E_OUTOFMEMORY;
    }

    /* Target descriptors point at their own view slot; copies move the view, never the pointer */
    if (RxWarp12HoldsTargets(Args->Type))
    {
        Object->Views = new (std::nothrow) RxWarpView[Count];
        if (Object->Views == nullptr)
            return E_OUTOFMEMORY;
        for (UINT Index = 0; Index < Count; Index++)
            reinterpret_cast<RxWarpView **>(Object->Memory)[Index] = &Object->Views[Index];
    }
    return S_OK;
}

static
VOID
APIENTRY
RxWarp12DestroyDescriptorHeap(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HDESCRIPTORHEAP Heap)
{
    RxWarpLock Lock;
    RxWarp12DescriptorHeap *Object = RxWarpObject<RxWarp12DescriptorHeap>(Heap);

    UNREFERENCED_PARAMETER(Device);
    if (Object == nullptr)
        return;
    if (Object->Allocation != 0)
        RxWarp12FreeKernel(Object->Device, nullptr, Object->Allocation, Object->Memory != nullptr);
    else if (Object->Memory != nullptr)
        VirtualFree(Object->Memory, 0, MEM_RELEASE);
    delete[] Object->Views;
    delete Object;
}

static
D3D12DDI_CPU_DESCRIPTOR_HANDLE
APIENTRY
RxWarp12GetCpuDescriptorHandleForHeapStart(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HDESCRIPTORHEAP Heap)
{
    RxWarpLock Lock;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Handle = {};

    UNREFERENCED_PARAMETER(Device);
    Handle.ptr = (SIZE_T)RxWarpObject<RxWarp12DescriptorHeap>(Heap)->Memory;
    return Handle;
}

/* Shader visible heaps are read through the same address the CPU writes */
static
D3D12DDI_GPU_DESCRIPTOR_HANDLE
APIENTRY
RxWarp12GetGpuDescriptorHandleForHeapStart(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HDESCRIPTORHEAP Heap)
{
    RxWarpLock Lock;
    D3D12DDI_GPU_DESCRIPTOR_HANDLE Handle = {};

    UNREFERENCED_PARAMETER(Device);
    Handle.ptr = (UINT64)RxWarpObject<RxWarp12DescriptorHeap>(Heap)->Memory;
    return Handle;
}

RxWarpView *
RxWarp12TargetView(
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Handle)
{
    return Handle.ptr ? *reinterpret_cast<RxWarpView **>(Handle.ptr) : nullptr;
}

static
RxWarpView
RxWarp12NewView(
    _In_ D3D12DDI_HRESOURCE Resource,
    _In_ DXGI_FORMAT Format,
    _In_ D3D12DDI_RESOURCE_DIMENSION Dimension)
{
    RxWarpView View;
    RxWarp12Resource *Object = Resource.pDrvPrivate ? RxWarp12GetResource(Resource) : nullptr;

    View.Resource = Object ? &Object->Storage : nullptr;
    View.Format = Format;
    View.Dimension = (D3D10DDIRESOURCE_TYPE)Dimension;
    return View;
}

static
VOID
APIENTRY
RxWarp12CreateRenderTargetView(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002 *Args,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Descriptor)
{
    RxWarpLock Lock;
    RxWarpView View = RxWarp12NewView(Args->hDrvResource, Args->Format, Args->ResourceDimension);

    UNREFERENCED_PARAMETER(Device);
    switch (Args->ResourceDimension)
    {
        case D3D12DDI_RD_BUFFER:
            View.FirstElement = (UINT)Args->Buffer.FirstElement;
            View.ElementCount = Args->Buffer.NumElements;
            break;
        case D3D12DDI_RD_TEXTURE1D:
            View.MipSlice = Args->Tex1D.MipSlice;
            View.FirstSlice = Args->Tex1D.FirstArraySlice;
            View.SliceCount = Args->Tex1D.ArraySize;
            break;
        case D3D12DDI_RD_TEXTURE3D:
            View.MipSlice = Args->Tex3D.MipSlice;
            View.FirstSlice = Args->Tex3D.FirstW;
            View.SliceCount = Args->Tex3D.WSize;
            break;
        default:
            View.MipSlice = Args->Tex2D.MipSlice;
            View.FirstSlice = Args->Tex2D.FirstArraySlice;
            View.SliceCount = Args->Tex2D.ArraySize;
            break;
    }
    *RxWarp12TargetView(Descriptor) = View;
}

static
VOID
APIENTRY
RxWarp12CreateDepthStencilView(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_DEPTH_STENCIL_VIEW *Args,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Descriptor)
{
    RxWarpLock Lock;
    RxWarpView View = RxWarp12NewView(Args->hDrvResource, Args->Format, Args->ResourceDimension);

    UNREFERENCED_PARAMETER(Device);
    if (Args->ResourceDimension == D3D12DDI_RD_TEXTURE1D)
    {
        View.MipSlice = Args->Tex1D.MipSlice;
        View.FirstSlice = Args->Tex1D.FirstArraySlice;
        View.SliceCount = Args->Tex1D.ArraySize;
    }
    else
    {
        View.MipSlice = Args->Tex2D.MipSlice;
        View.FirstSlice = Args->Tex2D.FirstArraySlice;
        View.SliceCount = Args->Tex2D.ArraySize;
    }
    View.Flags = (UINT16)Args->Flags;
    *RxWarp12TargetView(Descriptor) = View;
}

static
VOID
APIENTRY
RxWarp12CreateShaderResourceView(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 *Args,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Descriptor)
{
    RxWarpLock Lock;
    RX_WARP12_DESCRIPTOR *Target = RxWarp12Descriptor(Descriptor.ptr);
    RxWarpView View = RxWarp12NewView(Args->hDrvResource, Args->Format, Args->ResourceDimension);

    UNREFERENCED_PARAMETER(Device);
    Target->Stride = 0;
    View.Mapping = (UINT16)(Args->Shader4ComponentMapping & 0xFFF);
    switch (Args->ResourceDimension)
    {
        case D3D12DDI_RD_BUFFER:
            View.FirstElement = (UINT)Args->Buffer.FirstElement;
            View.ElementCount = Args->Buffer.NumElements;
            View.Flags = (UINT16)Args->Buffer.Flags;
            Target->Stride = Args->Buffer.StructureByteStride;
            break;
        case D3D12DDI_RD_TEXTURE1D:
            View.MipSlice = Args->Tex1D.MostDetailedMip;
            View.MipCount = Args->Tex1D.MipLevels;
            View.FirstSlice = Args->Tex1D.FirstArraySlice;
            View.SliceCount = Args->Tex1D.ArraySize;
            break;
        case D3D12DDI_RD_TEXTURE3D:
            View.MipSlice = Args->Tex3D.MostDetailedMip;
            View.MipCount = Args->Tex3D.MipLevels;
            break;
        case D3D12DDI_RD_TEXTURECUBE:
            View.MipSlice = Args->TexCube.MostDetailedMip;
            View.MipCount = Args->TexCube.MipLevels;
            View.FirstSlice = Args->TexCube.First2DArrayFace;
            View.SliceCount = Args->TexCube.NumCubes * 6;
            break;
        default:
            View.MipSlice = Args->Tex2D.MostDetailedMip;
            View.MipCount = Args->Tex2D.MipLevels;
            View.FirstSlice = Args->Tex2D.FirstArraySlice;
            View.SliceCount = Args->Tex2D.ArraySize;
            break;
    }
    Target->Kind = RxWarp12DescriptorShaderResource;
    Target->View = View;
}

static
VOID
APIENTRY
RxWarp12CreateUnorderedAccessView(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 *Args,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Descriptor)
{
    RxWarpLock Lock;
    RX_WARP12_DESCRIPTOR *Target = RxWarp12Descriptor(Descriptor.ptr);
    RxWarpView View = RxWarp12NewView(Args->hDrvResource, Args->Format, Args->ResourceDimension);

    UNREFERENCED_PARAMETER(Device);
    Target->Stride = 0;
    switch (Args->ResourceDimension)
    {
        case D3D12DDI_RD_BUFFER:
            View.FirstElement = (UINT)Args->Buffer.FirstElement;
            View.ElementCount = Args->Buffer.NumElements;
            View.Flags = (UINT16)Args->Buffer.Flags;
            Target->Stride = Args->Buffer.StructureByteStride;

            /* The counter is four bytes of the counter resource */
            if (Args->Buffer.hDrvCounterResource.pDrvPrivate != nullptr)
            {
                RxWarp12Resource *Counter = RxWarp12GetResource(Args->Buffer.hDrvCounterResource);

                if (Counter->Storage.Memory != nullptr && Args->Buffer.CounterOffsetInBytes + 4 <= Counter->Storage.MemorySize)
                    View.CounterMemory = reinterpret_cast<UINT *>(Counter->Storage.Memory + Args->Buffer.CounterOffsetInBytes);
            }
            break;
        case D3D12DDI_RD_TEXTURE1D:
            View.MipSlice = Args->Tex1D.MipSlice;
            View.FirstSlice = Args->Tex1D.FirstArraySlice;
            View.SliceCount = Args->Tex1D.ArraySize;
            break;
        case D3D12DDI_RD_TEXTURE3D:
            View.MipSlice = Args->Tex3D.MipSlice;
            View.FirstSlice = Args->Tex3D.FirstW;
            View.SliceCount = Args->Tex3D.WSize;
            break;
        default:
            View.MipSlice = Args->Tex2D.MipSlice;
            View.FirstSlice = Args->Tex2D.FirstArraySlice;
            View.SliceCount = Args->Tex2D.ArraySize;
            break;
    }
    Target->Kind = RxWarp12DescriptorUnorderedAccess;
    Target->View = View;
}

static
VOID
APIENTRY
RxWarp12CreateConstantBufferView(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDI_CONSTANT_BUFFER_VIEW_DESC *Desc,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Descriptor)
{
    RxWarpLock Lock;
    RX_WARP12_DESCRIPTOR *Target = RxWarp12Descriptor(Descriptor.ptr);
    RxWarp12Resource *Resource;
    UINT64 Offset;

    Resource = RxWarp12ResolveAddress(RxWarpObject<RxWarp12Device>(Device), Desc->BufferLocation, &Offset);
    Target->Kind = RxWarp12DescriptorConstants;
    Target->Stride = 0;
    Target->Constants.Resource = Resource ? &Resource->Storage : nullptr;
    Target->Constants.Offset = Offset;
    Target->Constants.Size = Desc->SizeInBytes;
}

static
VOID
APIENTRY
RxWarp12CreateSampler(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_SAMPLER_0096 *Args,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Descriptor)
{
    RxWarpLock Lock;
    RX_WARP12_DESCRIPTOR *Target = RxWarp12Descriptor(Descriptor.ptr);
    const D3D12DDI_SAMPLER_DESC_0096 *Desc = Args->pSamplerDesc;
    D3D10_DDI_SAMPLER_DESC *State = &Target->Sampler.Desc;

    UNREFERENCED_PARAMETER(Device);
    Target->Kind = RxWarp12DescriptorSampler;
    Target->Stride = 0;
    State->Filter = (D3D10_DDI_FILTER)Desc->Filter;
    State->AddressU = (D3D10_DDI_TEXTURE_ADDRESS_MODE)Desc->AddressU;
    State->AddressV = (D3D10_DDI_TEXTURE_ADDRESS_MODE)Desc->AddressV;
    State->AddressW = (D3D10_DDI_TEXTURE_ADDRESS_MODE)Desc->AddressW;
    State->MipLODBias = Desc->MipLODBias;
    State->MaxAnisotropy = Desc->MaxAnisotropy;
    State->ComparisonFunc = (D3D10_DDI_COMPARISON_FUNC)Desc->ComparisonFunc;
    memcpy(State->BorderColor, Desc->FloatBorderColor, sizeof(State->BorderColor));
    State->MinLOD = Desc->MinLOD;
    State->MaxLOD = Desc->MaxLOD;
}

/* Moves Count descriptors; target heaps copy the views behind the pointers */
static
VOID
RxWarp12MoveDescriptors(
    _In_ SIZE_T To,
    _In_ SIZE_T From,
    _In_ UINT Count,
    _In_ D3D12DDI_DESCRIPTOR_HEAP_TYPE Type)
{
    for (UINT Index = 0; Index < Count; Index++)
    {
        if (RxWarp12HoldsTargets(Type))
        {
            D3D12DDI_CPU_DESCRIPTOR_HANDLE Target = { To + (SIZE_T)Index * RxWarp12TargetDescriptor };
            D3D12DDI_CPU_DESCRIPTOR_HANDLE Source = { From + (SIZE_T)Index * RxWarp12TargetDescriptor };

            *RxWarp12TargetView(Target) = *RxWarp12TargetView(Source);
        }
        else
        {
            memcpy(reinterpret_cast<VOID *>(To + (SIZE_T)Index * RxWarp12FullDescriptor),
                   reinterpret_cast<const VOID *>(From + (SIZE_T)Index * RxWarp12FullDescriptor), RxWarp12FullDescriptor);
        }
    }
}

static
VOID
APIENTRY
RxWarp12CopyDescriptors(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ UINT TargetRanges,
    _In_reads_(TargetRanges) const D3D12DDI_CPU_DESCRIPTOR_HANDLE *TargetStarts,
    _In_reads_opt_(TargetRanges) const UINT *TargetSizes,
    _In_ UINT SourceRanges,
    _In_reads_(SourceRanges) const D3D12DDI_CPU_DESCRIPTOR_HANDLE *SourceStarts,
    _In_reads_opt_(SourceRanges) const UINT *SourceSizes,
    _In_ D3D12DDI_DESCRIPTOR_HEAP_TYPE Type)
{
    RxWarpLock Lock;
    UINT Stride = RxWarp12HoldsTargets(Type) ? RxWarp12TargetDescriptor : RxWarp12FullDescriptor;
    UINT TargetRange = 0;
    UINT TargetUsed = 0;

    UNREFERENCED_PARAMETER(Device);

    /* Walk the source ranges, filling the target ranges one descriptor at a time */
    for (UINT SourceRange = 0; SourceRange < SourceRanges; SourceRange++)
    {
        UINT SourceCount = SourceSizes ? SourceSizes[SourceRange] : 1;

        for (UINT Index = 0; Index < SourceCount; Index++)
        {
            while (TargetRange < TargetRanges && TargetUsed >= (TargetSizes ? TargetSizes[TargetRange] : 1))
            {
                TargetRange += 1;
                TargetUsed = 0;
            }
            if (TargetRange >= TargetRanges)
                return;
            RxWarp12MoveDescriptors(TargetStarts[TargetRange].ptr + (SIZE_T)TargetUsed * Stride,
                                    SourceStarts[SourceRange].ptr + (SIZE_T)Index * Stride, 1, Type);
            TargetUsed += 1;
        }
    }
}

static
VOID
APIENTRY
RxWarp12CopyDescriptorsSimple(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ UINT Count,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Target,
    _In_ D3D12DDI_CPU_DESCRIPTOR_HANDLE Source,
    _In_ D3D12DDI_DESCRIPTOR_HEAP_TYPE Type)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    RxWarp12MoveDescriptors(Target.ptr, Source.ptr, Count, Type);
}

VOID
RxWarp12FillDescriptorTable(
    _Inout_ D3D12DDI_DEVICE_FUNCS_CORE_0116 *Table)
{
    Table->pfnCreateDescriptorHeap = RxWarp12CreateDescriptorHeap;
    Table->pfnDestroyDescriptorHeap = RxWarp12DestroyDescriptorHeap;
    Table->pfnGetDescriptorSizeInBytes = RxWarp12GetDescriptorSizeInBytes;
    Table->pfnGetCPUDescriptorHandleForHeapStart = RxWarp12GetCpuDescriptorHandleForHeapStart;
    Table->pfnGetGPUDescriptorHandleForHeapStart = RxWarp12GetGpuDescriptorHandleForHeapStart;
    Table->pfnCreateShaderResourceView = RxWarp12CreateShaderResourceView;
    Table->pfnCreateConstantBufferView = RxWarp12CreateConstantBufferView;
    Table->pfnCreateSampler = RxWarp12CreateSampler;
    Table->pfnCreateUnorderedAccessView = RxWarp12CreateUnorderedAccessView;
    Table->pfnCreateRenderTargetView = RxWarp12CreateRenderTargetView;
    Table->pfnCreateDepthStencilView = RxWarp12CreateDepthStencilView;
    Table->pfnCopyDescriptors = RxWarp12CopyDescriptors;
    Table->pfnCopyDescriptorsSimple = RxWarp12CopyDescriptorsSimple;
}
