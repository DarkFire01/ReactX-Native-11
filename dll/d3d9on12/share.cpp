/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The private entries d3d9.dll shares resources through: local handles and wrapped objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Usage bits a shared resource reports; the high bit is always present */
static constexpr UINT64 NineSharedUsageBase = 0x8000000;
static constexpr UINT64 NineSharedUsageTarget = 0x1;
static constexpr UINT64 NineSharedUsageDepth = 0x2;

/* The two buffer formats a shared buffer is reported as an index buffer for */
static
BOOLEAN
NineSharedIndexFormat(
    _In_ DXGI_FORMAT Format)
{
    return Format == DXGI_FORMAT_R16_UINT || Format == DXGI_FORMAT_R8_UINT;
}

UINT
APIENTRY
NineQueryResourceDriverDataSize(
    _In_ HANDLE hDevice)
{
    if (hDevice == NULL)
        return 0;
    return TL::SharedResourceHelpers::cPrivateResourceDriverDataSize;
}

HRESULT
APIENTRY
NineCreateKmtHandle(
    _In_ HANDLE hDevice,
    _In_ HANDLE Object,
    _Out_ UINT* Kmt,
    _Out_writes_bytes_(DataSize) VOID* Data,
    _In_ UINT DataSize)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    IUnknown* Own = nullptr;

    if (Device == nullptr || Kmt == nullptr)
        return E_INVALIDARG;

    try
    {
        {
            std::lock_guard<std::mutex> Guard(Device->WrappedLock);

            if (Object != NULL && Device->Wrapped.count(static_cast<IUnknown*>(Object)) != 0)
            {
                Own = static_cast<IUnknown*>(Object);
                Own->AddRef();
            }
        }

        /* A registered object is handed over as is; anything else is a handle to open */
        if (Own != nullptr)
        {
            *Kmt = Device->Sharing->CreateKMTHandle(Own);
            Device->Sharing->InitializePrivateDriverData(TL::DeferredDestructionType::Submission, Data, DataSize);
        }
        else
        {
            *Kmt = Device->Sharing->CreateKMTHandle(Object);
            Device->Sharing->InitializePrivateDriverData(TL::DeferredDestructionType::Completion, Data, DataSize);
        }
    }
    catch (_com_error& Error)
    {
        NineTrace(L"no local handle for a shared object (0x%08lX)", Error.Error());
        return NineDxgiResult(Error.Error());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineQueryResourceFromKmtHandle(
    _In_ HANDLE hDevice,
    _In_ UINT Kmt,
    _Out_ NineSharedResourceInfo* Info)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    TL::ResourceInfo Shared = {};

    if (Device == nullptr || Info == nullptr)
        return E_INVALIDARG;

    try
    {
        Device->Sharing->QueryResourceInfoFromKMTHandle(Kmt, nullptr, &Shared);
        if (Shared.m_Type != TL::ResourceType)
            TL::ThrowFailure(E_UNEXPECTED);
    }
    catch (_com_error& Error)
    {
        return NineDxgiResult(Error.Error());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    const D3D12_RESOURCE_DESC& Desc = Shared.Resource.m_ResourceDesc;

    Info->Width = 0;
    Info->Height = 0;
    Info->Type = static_cast<D3DRESOURCETYPE>(0);
    Info->Levels = 0;
    Info->Depth = 0;
    Info->NtHandle = Shared.m_bNTHandle;
    Info->CreatedBy9On12 = Shared.m_bAllocatedBy9on12;
    Info->Format = NineD3dFormat(Desc.Format);
    Info->Usage = NineSharedUsageBase;

    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        Info->Multisample = 0;
        Info->QualityOrSize = static_cast<UINT>(Desc.Width);
        Info->Type = NineSharedIndexFormat(Desc.Format) ? D3DRTYPE_INDEXBUFFER : D3DRTYPE_VERTEXBUFFER;
        return NineDxgiResult(S_OK);
    }

    Info->Width = static_cast<UINT>(Desc.Width);
    Info->Height = Desc.Height;
    Info->Levels = Desc.MipLevels;
    Info->Depth = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? Desc.DepthOrArraySize : 1;
    Info->Multisample = Desc.SampleDesc.Count == 1 ? 0 : Desc.SampleDesc.Count;
    Info->QualityOrSize = Desc.SampleDesc.Quality;
    if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
        Info->Usage |= NineSharedUsageTarget;
    if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
        Info->Usage |= NineSharedUsageDepth;

    /* One dimensional textures have no Direct3D 9 type and stay at zero */
    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        Info->Type = D3DRTYPE_VOLUMETEXTURE;
    else if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && Desc.DepthOrArraySize == 6)
        Info->Type = D3DRTYPE_CUBETEXTURE;
    else if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        Info->Type = Desc.SampleDesc.Count > 1 ? D3DRTYPE_SURFACE : D3DRTYPE_TEXTURE;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineDestroyKmtHandle(
    _In_ HANDLE hDevice,
    _In_ UINT Kmt)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr)
        return E_INVALIDARG;

    Device->Sharing->DestroyKMTHandle(Kmt);
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineCreateWrappingHandle(
    _In_ HANDLE hDevice,
    _In_ IUnknown* Object,
    _Out_ HANDLE* Wrapping)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr || Object == nullptr || Wrapping == nullptr)
        return E_INVALIDARG;

    try
    {
        std::lock_guard<std::mutex> Guard(Device->WrappedLock);

        Device->Wrapped.insert(Object);
        *Wrapping = Object;
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    return NineDxgiResult(S_OK);
}

VOID
APIENTRY
NineDestroyWrappingHandle(
    _In_ HANDLE hDevice,
    _In_ HANDLE Wrapping)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr || Wrapping == NULL)
        return;

    std::lock_guard<std::mutex> Guard(Device->WrappedLock);
    Device->Wrapped.erase(static_cast<IUnknown*>(Wrapping));
}
