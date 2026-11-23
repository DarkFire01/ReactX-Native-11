/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Resources: storage layout, mapping, copies and updates
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"
#include "sample.hpp"

#define RX_ALIGN(Value, Alignment) (((Value) + (Alignment) - 1) & ~((SIZE_T)(Alignment) - 1))

RxWarpResource::~RxWarpResource()
{
    delete Shadow;
    if (Memory != nullptr && !Locked)
        VirtualFree(Memory, 0, MEM_RELEASE);
    delete[] Subresources;
}

/*
 * Rows are padded to four bytes and every subresource to sixteen. Subresources are laid out
 * one mip level at a time with all array slices of that level together; each resource gets
 * its own pages.
 */
HRESULT
RxWarpResource::Initialize(
    _In_ const D3D11DDIARG_CREATERESOURCE *Args)
{
    UINT Element;
    UINT Mip;
    UINT Slice;
    SIZE_T Offset = 0;

    Dimension = Args->ResourceDimension;
    Format = Args->Format;
    Usage = Args->Usage;
    BindFlags = Args->BindFlags;
    MapFlags = Args->MapFlags;
    MiscFlags = Args->MiscFlags;
    MipLevels = Args->MipLevels ? Args->MipLevels : 1;
    ArraySize = Args->ArraySize ? Args->ArraySize : 1;
    SampleCount = Args->SampleDesc.Count ? Args->SampleDesc.Count : 1;
    ByteStride = Args->ByteStride;

    SubresourceCount = MipLevels * ArraySize;
    Subresources = new (std::nothrow) RX_WARP_SUBRESOURCE[SubresourceCount];
    if (Subresources == nullptr)
        return E_OUTOFMEMORY;

    for (Mip = 0; Mip < MipLevels; Mip++)
    {
        const D3D10DDI_MIPINFO &Info = Args->pMipInfoList[Mip];

        for (Slice = 0; Slice < ArraySize; Slice++)
        {
            RX_WARP_SUBRESOURCE *Sub = &Subresources[Slice * MipLevels + Mip];

            Sub->Width = Info.TexelWidth;
            Sub->Height = (Dimension == D3D10DDIRESOURCE_BUFFER || Dimension == D3D10DDIRESOURCE_TEXTURE1D) ? 1 : Info.TexelHeight;
            Sub->Depth = (Dimension == D3D10DDIRESOURCE_TEXTURE3D) ? Info.TexelDepth : 1;

            if (Dimension == D3D10DDIRESOURCE_BUFFER)
            {
                Sub->RowPitch = Sub->Width;
                Sub->DepthPitch = Sub->Width;
                Sub->Size = Sub->Width;
            }
            else
            {
                Sub->RowPitch = (UINT)RX_ALIGN((SIZE_T)RxWarpFormatRowBytes(Format, Sub->Width) * SampleCount, 4);
                Element = RxWarpFormatRows(Format, Sub->Height);
                if (Dimension == D3D10DDIRESOURCE_TEXTURE3D)
                {
                    Sub->DepthPitch = Sub->RowPitch * Element;
                    Sub->Size = RX_ALIGN((SIZE_T)Sub->DepthPitch * Sub->Depth, 16);
                }
                else
                {
                    Sub->DepthPitch = (UINT)RX_ALIGN((SIZE_T)Sub->RowPitch * Element, 16);
                    Sub->Size = Sub->DepthPitch;
                }
            }

            Sub->Offset = Offset;
            Offset += RX_ALIGN(Sub->Size, 16);
        }
    }

    MemorySize = Offset ? Offset : 16;
    Memory = static_cast<BYTE *>(VirtualAlloc(NULL, MemorySize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (Memory == nullptr)
        return E_OUTOFMEMORY;

    if (Args->pInitialDataUP != nullptr)
    {
        for (UINT Index = 0; Index < SubresourceCount; Index++)
        {
            const D3D10_DDIARG_SUBRESOURCE_UP &Initial = Args->pInitialDataUP[Index];
            RX_WARP_SUBRESOURCE *Sub = &Subresources[Index];
            UINT Rows = (Dimension == D3D10DDIRESOURCE_BUFFER) ? 1 : RxWarpFormatRows(Format, Sub->Height);
            UINT Bytes = (Dimension == D3D10DDIRESOURCE_BUFFER) ? Sub->Width : RxWarpFormatRowBytes(Format, Sub->Width) * SampleCount;

            if (Initial.pSysMem == nullptr)
                continue;
            for (UINT Z = 0; Z < Sub->Depth; Z++)
            {
                for (UINT Y = 0; Y < Rows; Y++)
                {
                    memcpy(Data(Index) + (SIZE_T)Z * Sub->DepthPitch + (SIZE_T)Y * Sub->RowPitch,
                           static_cast<const BYTE *>(Initial.pSysMem) + (SIZE_T)Z * Initial.SysMemSlicePitch + (SIZE_T)Y * Initial.SysMemPitch,
                           Bytes);
                }
            }
        }
    }
    return S_OK;
}

/* DXGI presents kernel allocations, so present targets keep their storage in one */
HRESULT
RxWarpAllocateForPresent(
    _In_ RxWarpDevice *Device,
    _Inout_ RxWarpResource *Resource,
    _In_ const D3D11DDIARG_CREATERESOURCE *Args,
    _In_opt_ HANDLE RuntimeResource)
{
    RX_WARP_ALLOCATION_DATA Data = {};
    D3DDDI_ALLOCATIONINFO Info = {};
    D3DDDICB_ALLOCATE Allocate = {};
    D3DDDICB_LOCK Lock = {};
    HRESULT hr;

    if (Device->KernelCallbacks == nullptr || Device->KernelCallbacks->pfnAllocateCb == nullptr || Device->KernelCallbacks->pfnLockCb == nullptr)
        return S_OK;

    Data.Dimension = Args->ResourceDimension;
    Data.Width = Args->pMipInfoList[0].PhysicalWidth;
    Data.Height = Args->pMipInfoList[0].PhysicalHeight;
    Data.Depth = Args->pMipInfoList[0].PhysicalDepth;
    Data.Format = Args->Format;
    Data.RowPitch = Resource->Subresources[0].RowPitch;
    Data.Size = (UINT)Resource->MemorySize;
    Data.ArraySize = Args->ArraySize;
    Data.MipLevels = Args->MipLevels;
    Data.Usage = Args->Usage;
    Data.BindFlags = Args->BindFlags;
    Data.SampleCount = Args->SampleDesc.Count;
    Data.SampleQuality = Args->SampleDesc.Quality;
    Data.Flags = RX_WARP_ALLOCATION_SYSTEM | RX_WARP_ALLOCATION_LINEAR;
    if (Args->MiscFlags & D3DWDDM1_3DDI_RESOURCE_MISC_CROSS_ADAPTER)
        Data.Flags |= RX_WARP_ALLOCATION_CROSS_ADAPTER;
    if (Args->MiscFlags & D3DWDDM1_3DDI_RESOURCE_MISC_TILE_POOL)
        Data.Flags |= RX_WARP_ALLOCATION_TILE_POOL;
    if (Args->MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED)
        Data.Flags |= RX_WARP_ALLOCATION_SHARED;
    if (Args->pPrimaryDesc != nullptr)
    {
        Data.Flags |= RX_WARP_ALLOCATION_PRIMARY;
        Data.Rotation = Args->pPrimaryDesc->ModeDesc.Rotation;
        Data.VidPnSourceId = Args->pPrimaryDesc->VidPnSourceId;
        Data.RefreshNumerator = Args->pPrimaryDesc->ModeDesc.RefreshRate.Numerator;
        Data.RefreshDenominator = Args->pPrimaryDesc->ModeDesc.RefreshRate.Denominator;
        if (!(Args->pPrimaryDesc->Flags & DXGI_DDI_PRIMARY_NONPREROTATED) && Data.Rotation > DXGI_DDI_MODE_ROTATION_IDENTITY)
            Data.Flags |= RX_WARP_ALLOCATION_ROTATED;
        if (Args->pPrimaryDesc->Flags & DXGI_DDI_PRIMARY_STEREO)
            Data.Flags |= RX_WARP_ALLOCATION_STEREO;
        Info.VidPnSourceId = Data.VidPnSourceId;
        Info.Flags.Primary = 1;
    }

    Info.pPrivateDriverData = &Data;
    Info.PrivateDriverDataSize = sizeof(Data);
    Allocate.hResource = RuntimeResource;
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &Info;
    hr = Device->KernelCallbacks->pfnAllocateCb(Device->RuntimeDevice.handle, &Allocate);
    if (FAILED(hr))
        return hr;
    Resource->Allocation = Info.hAllocation;

    /* The allocation's memory replaces the resource's own, keeping what is already in it */
    Lock.hAllocation = Info.hAllocation;
    Lock.Flags.DonotWait = 1;
    Lock.Flags.IgnoreSync = 1;
    hr = Device->KernelCallbacks->pfnLockCb(Device->RuntimeDevice.handle, &Lock);
    if (FAILED(hr))
        return hr;
    memcpy(Lock.pData, Resource->Memory, Resource->MemorySize);
    VirtualFree(Resource->Memory, 0, MEM_RELEASE);
    Resource->Memory = (BYTE *)Lock.pData;
    Resource->Locked = TRUE;
    return S_OK;
}

/* A plain kernel buffer the context hands the kernel driver for its own bookkeeping */
HRESULT
RxWarpAllocateRaw(
    _In_ RxWarpDevice *Device,
    _In_ UINT Size,
    _Out_ D3DKMT_HANDLE *Allocation,
    _Out_ VOID **Memory)
{
    RX_WARP_ALLOCATION_DATA Data = {};
    D3DDDI_ALLOCATIONINFO Info = {};
    D3DDDICB_ALLOCATE Allocate = {};
    D3DDDICB_LOCK Lock = {};
    HRESULT hr;

    *Allocation = 0;
    *Memory = nullptr;
    Data.Dimension = D3D10DDIRESOURCE_BUFFER;
    Data.Width = Size;
    Data.Height = 1;
    Data.Depth = 1;
    Data.RowPitch = Size;
    Data.Size = Size;
    Data.ArraySize = 1;
    Data.MipLevels = 1;
    Data.Flags = RX_WARP_ALLOCATION_LINEAR;
    Info.pPrivateDriverData = &Data;
    Info.PrivateDriverDataSize = sizeof(Data);
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &Info;
    hr = Device->KernelCallbacks->pfnAllocateCb(Device->RuntimeDevice.handle, &Allocate);
    if (FAILED(hr))
        return hr;
    *Allocation = Info.hAllocation;
    Lock.hAllocation = Info.hAllocation;
    Lock.Flags.DonotWait = 1;
    Lock.Flags.IgnoreSync = 1;
    hr = Device->KernelCallbacks->pfnLockCb(Device->RuntimeDevice.handle, &Lock);
    if (SUCCEEDED(hr))
        *Memory = Lock.pData;
    return hr;
}

VOID
RxWarpReleaseForPresent(
    _In_ RxWarpDevice *Device,
    _Inout_ RxWarpResource *Resource)
{
    D3DDDICB_UNLOCK Unlock = {};
    D3DDDICB_DEALLOCATE Deallocate = {};

    if (Resource->Shadow != nullptr)
        RxWarpReleaseForPresent(Device, Resource->Shadow);
    if (Resource->Allocation == 0 || Device == nullptr || Device->KernelCallbacks == nullptr)
        return;
    if (Resource->Locked && Device->KernelCallbacks->pfnUnlockCb != nullptr)
    {
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Resource->Allocation;
        Device->KernelCallbacks->pfnUnlockCb(Device->RuntimeDevice.handle, &Unlock);
    }
    Resource->Memory = nullptr;
    Resource->Locked = FALSE;
    if (Device->KernelCallbacks->pfnDeallocateCb != nullptr)
    {
        Deallocate.NumAllocations = 1;
        Deallocate.HandleList = &Resource->Allocation;
        Device->KernelCallbacks->pfnDeallocateCb(Device->RuntimeDevice.handle, &Deallocate);
    }
    Resource->Allocation = 0;
}

VOID
APIENTRY
RxWarpDdi::CreateResource(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D11DDIARG_CREATERESOURCE *Args,
    _In_ D3D10DDI_HRESOURCE Resource,
    _In_ D3D10DDI_HRTRESOURCE RuntimeResource)
{
    RxWarpLock Lock;
    RxWarpDevice *This = RxWarpObject<RxWarpDevice>(Device);
    RxWarpResource *Object;
    HRESULT hr;

    UNREFERENCED_PARAMETER(RuntimeResource);

    Object = new (std::nothrow) RxWarpResource();
    if (Object == nullptr)
    {
        RxWarpBind(Resource, (RxWarpResource *)nullptr);
        This->SetError(E_OUTOFMEMORY);
        return;
    }

    hr = Object->Initialize(Args);
    if (SUCCEEDED(hr) && (Args->BindFlags & D3D10_DDI_BIND_PRESENT))
        hr = RxWarpAllocateForPresent(This, Object, Args, RuntimeResource.handle);
    if (FAILED(hr))
    {
        RxWarpReleaseForPresent(This, Object);
        delete Object;
        Object = nullptr;
        This->SetError(hr);
    }
    RxWarpBind(Resource, Object);
}

VOID
APIENTRY
RxWarpDdi::OpenResource(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D10DDIARG_OPENRESOURCE *Args,
    _In_ D3D10DDI_HRESOURCE Resource,
    _In_ D3D10DDI_HRTRESOURCE RuntimeResource)
{
    RxWarpLock Lock;

    /* Shared resources need the kernel adapter, which software mode does not have */
    UNREFERENCED_PARAMETER(Args);
    UNREFERENCED_PARAMETER(RuntimeResource);
    RxWarpBind(Resource, (RxWarpResource *)nullptr);
    RxWarpObject<RxWarpDevice>(Device)->SetError(E_NOTIMPL);
}

VOID
APIENTRY
RxWarpDdi::DestroyResource(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Resource)
{
    RxWarpLock Lock;
    RxWarpResource *Object = RxWarpObject<RxWarpResource>(Resource);

    if (Object != nullptr)
    {
        RxWarpReleaseForPresent(RxWarpObject<RxWarpDevice>(Device), Object);
        delete Object;
    }
    RxWarpBind(Resource, (RxWarpResource *)nullptr);
}

VOID
APIENTRY
RxWarpDdi::ResourceMap(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Resource,
    _In_ UINT Subresource,
    _In_ D3D10_DDI_MAP MapType,
    _In_ UINT MapFlags,
    _Out_ D3D10DDI_MAPPED_SUBRESOURCE *Mapped)
{
    RxWarpLock Lock;
    RxWarpResource *Object = RxWarpObject<RxWarpResource>(Resource);
    RX_WARP_SUBRESOURCE *Sub = Object ? Object->Subresource(Subresource) : nullptr;

    UNREFERENCED_PARAMETER(MapType);
    UNREFERENCED_PARAMETER(MapFlags);

    /* Work is done when it is recorded, so a map never has to wait */
    if (Sub == nullptr)
    {
        ZeroMemory(Mapped, sizeof(*Mapped));
        RxWarpObject<RxWarpDevice>(Device)->SetError(E_INVALIDARG);
        return;
    }
    Mapped->pData = Object->Data(Subresource);
    Mapped->RowPitch = Sub->RowPitch;
    Mapped->DepthPitch = Sub->DepthPitch;
}

VOID
APIENTRY
RxWarpDdi::ResourceUnmap(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Resource,
    _In_ UINT Subresource)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Resource);
    UNREFERENCED_PARAMETER(Subresource);
}

BOOL
APIENTRY
RxWarpDdi::ResourceIsStagingBusy(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Resource)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Resource);
    return FALSE;
}

VOID
APIENTRY
RxWarpDdi::ResourceCopy(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Destination,
    _In_ D3D10DDI_HRESOURCE Source)
{
    RxWarpResource *Target = RxWarpObject<RxWarpResource>(Destination);

    for (UINT Index = 0; Index < Target->SubresourceCount; Index++)
        ResourceCopyRegion(Device, Destination, Index, 0, 0, 0, Source, Index, nullptr, 0);
}

/* Copies a box of whole elements; block formats move four rows of texels per stored row */
static
VOID
RxWarpCopyBox(
    _In_ RxWarpResource *Target,
    _In_ UINT TargetIndex,
    _In_ UINT X,
    _In_ UINT Y,
    _In_ UINT Z,
    _In_reads_bytes_(Bytes) const BYTE *Source,
    _In_ UINT SourceRowPitch,
    _In_ UINT SourceDepthPitch,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Depth)
{
    RX_WARP_SUBRESOURCE *Sub = Target->Subresource(TargetIndex);
    UINT Block = RxWarpFormatBlockWidth(Target->Format);
    UINT RowBytes;
    UINT Rows;
    SIZE_T Left;

    if (Target->Dimension == D3D10DDIRESOURCE_BUFFER)
    {
        memcpy(Target->Data(TargetIndex) + X, Source, Width);
        return;
    }

    RowBytes = RxWarpFormatRowBytes(Target->Format, Width) * Target->SampleCount;
    Rows = RxWarpFormatRows(Target->Format, Height);
    Left = (SIZE_T)RxWarpFormatRowBytes(Target->Format, X - (X % Block)) * Target->SampleCount;
    if (Block > 1 && RxWarpFormatInfo(Target->Format)->Kind != RxKindBlock)
        Block = 1;

    for (UINT Slice = 0; Slice < Depth; Slice++)
    {
        for (UINT Row = 0; Row < Rows; Row++)
        {
            BYTE *Line = Target->Data(TargetIndex) + (SIZE_T)(Z + Slice) * Sub->DepthPitch +
                         (SIZE_T)(Y / Block + Row) * Sub->RowPitch + Left;

            memcpy(Line, Source + (SIZE_T)Slice * SourceDepthPitch + (SIZE_T)Row * SourceRowPitch, RowBytes);
        }
    }
}

VOID
APIENTRY
RxWarpDdi::ResourceCopyRegion(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Destination,
    _In_ UINT DestinationIndex,
    _In_ UINT X,
    _In_ UINT Y,
    _In_ UINT Z,
    _In_ D3D10DDI_HRESOURCE Source,
    _In_ UINT SourceIndex,
    _In_opt_ const D3D10_DDI_BOX *Box,
    _In_ UINT CopyFlags)
{
    RxWarpLock Lock;
    RxWarpResource *Target = RxWarpObject<RxWarpResource>(Destination);
    RxWarpResource *From = RxWarpObject<RxWarpResource>(Source);
    RX_WARP_SUBRESOURCE *Sub = From ? From->Subresource(SourceIndex) : nullptr;
    D3D10_DDI_BOX Whole;
    UINT Block;
    SIZE_T Start;

    if (RxWarpObject<RxWarpDevice>(Device)->Predicated())
        return;
    UNREFERENCED_PARAMETER(CopyFlags);
    if (Target == nullptr || Sub == nullptr || Target->Subresource(DestinationIndex) == nullptr)
        return;

    if (Box == nullptr)
    {
        Whole = { 0, 0, 0, (INT)Sub->Width, (INT)Sub->Height, (INT)Sub->Depth };
        Box = &Whole;
    }

    if (From->Dimension == D3D10DDIRESOURCE_BUFFER)
    {
        RxWarpCopyBox(Target, DestinationIndex, X, 0, 0, From->Data(SourceIndex) + Box->left, 0, 0,
                      (UINT)(Box->right - Box->left), 1, 1);
        return;
    }

    Block = (RxWarpFormatInfo(From->Format) && RxWarpFormatInfo(From->Format)->Kind == RxKindBlock) ? 4 : 1;
    Start = (SIZE_T)Box->front * Sub->DepthPitch + (SIZE_T)(Box->top / Block) * Sub->RowPitch +
            (SIZE_T)RxWarpFormatRowBytes(From->Format, (UINT)Box->left - (UINT)Box->left % RxWarpFormatBlockWidth(From->Format)) * From->SampleCount;
    RxWarpCopyBox(Target, DestinationIndex, X, Y, Z, From->Data(SourceIndex) + Start, Sub->RowPitch, Sub->DepthPitch,
                  (UINT)(Box->right - Box->left), (UINT)(Box->bottom - Box->top), (UINT)(Box->back - Box->front));
}

VOID
APIENTRY
RxWarpDdi::ResourceUpdateSubresourceUP(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Destination,
    _In_ UINT DestinationIndex,
    _In_opt_ const D3D10_DDI_BOX *Box,
    _In_ const VOID *Data,
    _In_ UINT RowPitch,
    _In_ UINT DepthPitch,
    _In_ UINT CopyFlags)
{
    RxWarpLock Lock;
    RxWarpResource *Target = RxWarpObject<RxWarpResource>(Destination);
    RX_WARP_SUBRESOURCE *Sub = Target ? Target->Subresource(DestinationIndex) : nullptr;
    D3D10_DDI_BOX Whole;

    if (RxWarpObject<RxWarpDevice>(Device)->Predicated())
        return;
    UNREFERENCED_PARAMETER(CopyFlags);
    if (Sub == nullptr || Data == nullptr)
        return;

    if (Box == nullptr)
    {
        Whole = { 0, 0, 0, (INT)Sub->Width, (INT)Sub->Height, (INT)Sub->Depth };
        Box = &Whole;
    }

    RxWarpCopyBox(Target, DestinationIndex, (UINT)Box->left, (UINT)Box->top, (UINT)Box->front, static_cast<const BYTE *>(Data),
                  RowPitch, DepthPitch, (UINT)(Box->right - Box->left), (UINT)(Box->bottom - Box->top), (UINT)(Box->back - Box->front));
}

/* Averages each pixel's samples into a single sampled target, top sample first */
VOID
APIENTRY
RxWarpDdi::ResourceResolveSubresource(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Destination,
    _In_ UINT DestinationIndex,
    _In_ D3D10DDI_HRESOURCE Source,
    _In_ UINT SourceIndex,
    _In_ DXGI_FORMAT Format)
{
    RxWarpLock Lock;
    RxWarpResource *To = RxWarpObject<RxWarpResource>(Destination);
    RxWarpResource *From = RxWarpObject<RxWarpResource>(Source);
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);
    RX_WARP_SUBRESOURCE *ToSub;
    RX_WARP_SUBRESOURCE *FromSub;
    UINT Order[16];
    UINT Count;
    FLOAT Positions[16][2];

    UNREFERENCED_PARAMETER(Device);
    if (To == nullptr || From == nullptr || Info == nullptr || To->Memory == nullptr || From->Memory == nullptr)
        return;
    ToSub = To->Subresource(DestinationIndex);
    FromSub = From->Subresource(SourceIndex);
    if (ToSub == nullptr || FromSub == nullptr)
        return;

    Count = (From->SampleCount < 16) ? From->SampleCount : 16;
    for (UINT Index = 0; Index < Count; Index++)
    {
        RxWarpSamplePosition(Count, Index, Positions[Index]);
        Order[Index] = Index;
    }
    for (UINT Index = 1; Index < Count; Index++)
    {
        UINT Moving = Order[Index];
        UINT Place = Index;

        while (Place > 0 && Positions[Order[Place - 1]][1] > Positions[Moving][1])
        {
            Order[Place] = Order[Place - 1];
            Place--;
        }
        Order[Place] = Moving;
    }

    for (UINT Y = 0; Y < ToSub->Height && Y < FromSub->Height; Y++)
    {
        for (UINT X = 0; X < ToSub->Width && X < FromSub->Width; X++)
        {
            FLOAT Parts[16][4] = {};
            FLOAT Average[4];
            BYTE Element[16] = {};

            for (UINT Index = 0; Index < Count; Index++)
            {
                const BYTE *Texel = From->Memory + FromSub->Offset + (SIZE_T)Y * FromSub->RowPitch +
                                    ((SIZE_T)X * From->SampleCount + Order[Index]) * Info->Bytes;
                BYTE Raw[16] = {};

                memcpy(Raw, Texel, (Info->Bytes <= 16) ? Info->Bytes : 16);
                RxWarpDecodeTexel(Format, Raw, (UINT *)Parts[Index]);
            }

            /* Neighbours add in pairs, then the pairs, down to one */
            for (UINT Width = Count; Width > 1; Width /= 2)
            {
                for (UINT Index = 0; Index < Width / 2; Index++)
                {
                    for (UINT Component = 0; Component < 4; Component++)
                        Parts[Index][Component] = Parts[Index * 2][Component] + Parts[Index * 2 + 1][Component];
                }
            }
            for (UINT Component = 0; Component < 4; Component++)
                Average[Component] = Parts[0][Component] / (FLOAT)Count;
            if (RxWarpEncodeColor(Format, Average, Element))
                memcpy(To->Memory + ToSub->Offset + (SIZE_T)Y * ToSub->RowPitch + (SIZE_T)X * To->SampleCount * Info->Bytes, Element, Info->Bytes);
        }
    }
}
