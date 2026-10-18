/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What the ReactX dxgi.dll needs from a device to back a swapchain
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "textures.hpp"

#include <memory>

/* A shared surface opened as a present destination; the swapchain holds it by IUnknown */
class RxPresentTarget final : public IUnknown
{
public:
    static constexpr GUID PrivateId = { 0x8e1f4b62, 0x37ad, 0x4c19, { 0xa5, 0x70, 0xd2, 0x6b, 0x08, 0xe9, 0x41, 0x3c } };

    explicit RxPresentTarget(
        _In_ RxDevice& Owner)
        : Device(Owner),
          Resource(new (std::nothrow) DdiResource(Owner))
    {
        Device.AddRef();
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        if (riid == PrivateId)
        {
            *ppvObject = this;
            return S_OK;
        }

        if (riid == __uuidof(IUnknown))
        {
            *ppvObject = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return InterlockedIncrement(&RefCount);
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        ULONG Count = InterlockedDecrement(&RefCount);

        if (Count == 0)
            delete this;
        return Count;
    }

    _Ret_maybenull_
    DdiResource*
    GetResource()
    {
        return Resource.get();
    }

private:
    ~RxPresentTarget()
    {
        /* The resource goes before the device it was opened on */
        Resource.reset();
        Device.Release();
    }

    LONG RefCount = 1;
    RxDevice& Device;
    std::unique_ptr<DdiResource> Resource;
};

/* The record's header as Windows writes it for a single sample swapchain buffer */
constexpr UINT RxSharedRecordVersion = 4;
constexpr UINT RxSharedRecordConstants[9] = { 1, 0, 0, 0, 0, 1, 1, 0, 1 };
constexpr UINT RxSharedRecordArraySizeIndex = 0;

/* Initial and largest count of the device's present limit semaphore */
constexpr UINT RxPresentLimitMaximum = 16;

/* What a flip fence is created with: shared by NT handle, never touched by the GPU */
constexpr UINT RxFlipFenceFlags = 0x80000083;

static
UINT
RxBindFlagsForUsage(
    _In_ DXGI_USAGE Usage)
{
    UINT BindFlags = 0;

    if (Usage & DXGI_USAGE_RENDER_TARGET_OUTPUT)
        BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (Usage & DXGI_USAGE_SHADER_INPUT)
        BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (Usage & DXGI_USAGE_UNORDERED_ACCESS)
        BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    return BindFlags;
}

HRESULT
STDMETHODCALLTYPE
RxPresentBridge::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    return Device.QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxPresentBridge::AddRef(VOID)
{
    return Device.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxPresentBridge::Release(VOID)
{
    return Device.Release();
}

HRESULT
STDMETHODCALLTYPE
RxPresentBridge::GetAdapterLuid(
    _Out_ LUID* Luid)
{
    *Luid = Device.GetAdapter().GetLuid();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxPresentBridge::CreatePresentBuffer(
    _In_ const DXGI_SWAP_CHAIN_DESC1* Desc,
    _In_ IUnknown* Parent,
    _COM_Outptr_ IUnknown** Buffer)
{
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    const RxSharedTextureRecord* SharedRecord = nullptr;
    RxSharedTextureRecord Record = {};
    UINT BindFlags = D3D10_DDI_BIND_PRESENT;
    RxTexture2D* Texture;
    UINT MiscFlags = 0;
    HRESULT hr;

    *Buffer = nullptr;

    TextureDesc.Width = Desc->Width;
    TextureDesc.Height = Desc->Height;
    /* A stereo buffer holds one slice per eye */
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = Desc->Stereo ? 2 : 1;
    TextureDesc.Format = Desc->Format;
    TextureDesc.SampleDesc = Desc->SampleDesc;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = RxBindFlagsForUsage(Desc->BufferUsage);

    /* Discard effects promise the contents are gone after a present; the driver can use that */
    if (Desc->SwapEffect == DXGI_SWAP_EFFECT_DISCARD || Desc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD)
        MiscFlags |= D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT;

    /* Flip model buffers are read by the compositor straight from the swapchain */
    if (Desc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || Desc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD)
    {
        Record.Size = sizeof(Record);
        Record.Version = RxSharedRecordVersion;
        Record.Width = TextureDesc.Width;
        Record.Height = TextureDesc.Height;
        Record.Format = TextureDesc.Format;
        memcpy(Record.Constants, RxSharedRecordConstants, sizeof(Record.Constants));
        Record.Constants[RxSharedRecordArraySizeIndex] = TextureDesc.ArraySize;
        Record.Dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
        Record.Desc = TextureDesc;
        Record.Desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        Record.Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        SharedRecord = &Record;
        MiscFlags |= D3D10_DDI_RESOURCE_MISC_SHARED;
        BindFlags |= D3D10_DDI_BIND_SHADER_RESOURCE;
    }

    hr = RxTexture2D::Create(Device, TextureDesc, nullptr, &Texture, BindFlags, MiscFlags, SharedRecord);
    if (FAILED(hr))
        return hr;

    Texture->GetDxgiSurface().SetUsage(Desc->BufferUsage | DXGI_USAGE_BACK_BUFFER);
    Texture->GetDxgiSurface().SetParent(Parent);
    *Buffer = static_cast<ID3D11Texture2D*>(Texture);
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxPresentBridge::DetachPresentBuffer(
    _In_ IUnknown* Buffer)
{
    RxTexture2D* Texture = RxUnwrap<RxTexture2D>(Buffer);

    if (Texture != nullptr)
        Texture->GetDxgiSurface().SetParent(nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxPresentBridge::OpenPresentTarget(
    _In_ HANDLE SharedHandle,
    _COM_Outptr_ IUnknown** Target)
{
    RxPresentTarget* Opened;
    HRESULT hr;

    *Target = nullptr;

    Opened = new (std::nothrow) RxPresentTarget(Device);
    if (Opened == nullptr)
        return E_OUTOFMEMORY;

    hr = Opened->GetResource() ? Opened->GetResource()->OpenShared((D3DKMT_HANDLE)(ULONG_PTR)SharedHandle)
                               : E_OUTOFMEMORY;
    if (FAILED(hr))
    {
        Opened->Release();
        return hr;
    }

    *Target = Opened;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxPresentBridge::SubmitPresent(
    _In_ IUnknown* Buffer,
    _In_opt_ IUnknown* Target,
    _In_ RxPresentContext* Context)
{
    RxTexture2D* Source = RxUnwrap<RxTexture2D>(Buffer);
    RxPresentTarget* Destination = RxUnwrap<RxPresentTarget>(Target);
    DXGI_DDI_ARG_PRESENT Args = {};
    HRESULT hr;

    if (Source == nullptr || (Target != nullptr && Destination == nullptr))
        return DXGI_ERROR_INVALID_CALL;

    Args.hDevice = (DXGI_DDI_HDEVICE)Device.GetDriverDevice().pDrvPrivate;
    Args.hSurfaceToPresent = (DXGI_DDI_HRESOURCE)Source->GetDdiResource().GetHandle().pDrvPrivate;
    Args.SrcSubResourceIndex = 0;
    Args.hDstResource = Destination ? (DXGI_DDI_HRESOURCE)Destination->GetResource()->GetHandle().pDrvPrivate : 0;
    Args.DstSubResourceIndex = 0;
    Args.pDXGIContext = Context;
    Args.Flags.Blt = 1;
    Args.FlipInterval = DXGI_DDI_FLIP_INTERVAL_IMMEDIATE;

    hr = Device.GetDxgiFuncs().pfnPresent(&Args);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    if (FAILED(hr))
        RX_TRACE(L"UMD Present failed 0x%08lX", hr);

    return RxApiResult(hr);
}

RxPresentBridge::~RxPresentBridge()
{
    if (LimitSemaphore != NULL)
        CloseHandle(LimitSemaphore);
}

HRESULT
STDMETHODCALLTYPE
RxPresentBridge::ShareBuffer(
    _In_ IUnknown* Buffer,
    _Out_ HANDLE* SharedHandle)
{
    RxTexture2D* Texture = RxUnwrap<RxTexture2D>(Buffer);
    const KmtThunks* Kmt = RxGetKmtThunks();
    OBJECT_ATTRIBUTES Attributes;
    D3DKMT_HANDLE Resource;

    *SharedHandle = NULL;
    if (Texture == nullptr || Texture->GetDdiResource().GetKmResource() == 0)
        return DXGI_ERROR_INVALID_CALL;
    if (Kmt->ShareObjects == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    InitializeObjectAttributes(&Attributes, nullptr, 0, nullptr, nullptr);
    Resource = Texture->GetDdiResource().GetKmResource();
    return RxHresultFromStatus(Kmt->ShareObjects(1, &Resource, &Attributes, GENERIC_ALL, SharedHandle));
}

HRESULT
STDMETHODCALLTYPE
RxPresentBridge::CreateFlipFence(
    _Out_ UINT* Fence,
    _Out_ HANDLE* SharedHandle)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create = {};
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy = {};
    const KmtThunks* Kmt = RxGetKmtThunks();
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;

    *Fence = 0;
    *SharedHandle = NULL;
    if (Kmt->CreateSynchronizationObject2 == nullptr || Kmt->ShareObjects == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Create.hDevice = Device.GetKmDevice();
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.Value = RxFlipFenceFlags;
    Status = Kmt->CreateSynchronizationObject2(&Create);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    InitializeObjectAttributes(&Attributes, nullptr, 0, nullptr, nullptr);
    Status = Kmt->ShareObjects(1, &Create.hSyncObject, &Attributes, GENERIC_ALL, SharedHandle);
    if (!NT_SUCCESS(Status))
    {
        Destroy.hSyncObject = Create.hSyncObject;
        Kmt->DestroySynchronizationObject(&Destroy);
        return RxHresultFromStatus(Status);
    }

    *Fence = Create.hSyncObject;
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxPresentBridge::DestroyFlipFence(
    _In_ UINT Fence)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy = {};

    if (Fence == 0)
        return;

    Destroy.hSyncObject = Fence;
    RxGetKmtThunks()->DestroySynchronizationObject(&Destroy);
}

HANDLE
STDMETHODCALLTYPE
RxPresentBridge::GetPresentLimitSemaphore()
{
    /* Made on first use with one count per frame the device may queue */
    AcquireSRWLockExclusive(&SemaphoreLock);
    if (LimitSemaphore == NULL)
    {
        LimitSemaphore = CreateSemaphoreExW(nullptr, (LONG)Device.GetDxgiDevice().GetFrameLatency(),
                                            RxPresentLimitMaximum, nullptr, 0, SEMAPHORE_MODIFY_STATE | SYNCHRONIZE);
    }
    ReleaseSRWLockExclusive(&SemaphoreLock);

    return LimitSemaphore;
}

HRESULT
STDMETHODCALLTYPE
RxPresentBridge::PresentFlip(
    _In_ IUnknown* Buffer,
    _In_ RxPresentContext* Context)
{
    RxTexture2D* Source = RxUnwrap<RxTexture2D>(Buffer);
    DXGI1_6_1_DDI_ARG_PRESENT Args = {};
    DXGI_DDI_ARG_PRESENTSURFACE Surface = {};
    HRESULT hr;

    if (Source == nullptr || Device.GetDxgiFuncs().pfnPresent1 == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    /* No destination: the kernel flip takes the buffer itself to the compositor */
    Surface.hSurface = (DXGI_DDI_HRESOURCE)Source->GetDdiResource().GetHandle().pDrvPrivate;
    Args.hDevice = (DXGI_DDI_HDEVICE)Device.GetDriverDevice().pDrvPrivate;
    Args.phSurfacesToPresent = &Surface;
    Args.SurfacesToPresent = 1;
    Args.pDXGIContext = Context;
    Args.Flags.Blt = 1;
    Args.Flags.AllowFlexibleRefresh = 1;
    Args.FlipInterval = DXGI_DDI_FLIP_INTERVAL_IMMEDIATE;
    Args.RotationHint = DXGI_DDI_MODE_ROTATION_IDENTITY;

    hr = Device.GetDxgiFuncs().pfnPresent1(&Args);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    if (FAILED(hr))
        RX_TRACE(L"UMD Present1 failed 0x%08lX", hr);

    return RxApiResult(hr);
}

HRESULT
STDMETHODCALLTYPE
RxPresentBridge::RotateBuffers(
    _In_reads_(Count) IUnknown* const* Buffers,
    _In_ UINT Count)
{
    DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES Args = {};
    DXGI_DDI_HRESOURCE Handles[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    DdiResource* Resources[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    RxTexture2D* Texture;
    D3DKMT_HANDLE First;
    UINT Index;
    HRESULT hr;

    if (Count < 2 || Count > DXGI_MAX_SWAP_CHAIN_BUFFERS)
        return S_OK;

    for (Index = 0; Index < Count; Index++)
    {
        Texture = RxUnwrap<RxTexture2D>(Buffers[Index]);
        if (Texture == nullptr)
            return DXGI_ERROR_INVALID_CALL;

        Resources[Index] = &Texture->GetDdiResource();
        Handles[Index] = (DXGI_DDI_HRESOURCE)Resources[Index]->GetHandle().pDrvPrivate;
    }

    Args.hDevice = (DXGI_DDI_HDEVICE)Device.GetDriverDevice().pDrvPrivate;
    Args.pResources = Handles;
    Args.Resources = Count;
    hr = Device.GetDxgiFuncs().pfnRotateResourceIdentities(&Args);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    if (FAILED(hr))
        return RxApiResult(hr);

    /* The kernel resources move with the driver's storage, so deallocation still finds them */
    First = Resources[0]->GetRuntimeResource()->KmResource;
    for (Index = 0; Index + 1 < Count; Index++)
        Resources[Index]->GetRuntimeResource()->KmResource = Resources[Index + 1]->GetRuntimeResource()->KmResource;
    Resources[Count - 1]->GetRuntimeResource()->KmResource = First;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxPresentBridge::CopyBufferRegions(
    _In_ IUnknown* Destination,
    _In_ IUnknown* Source,
    _In_reads_(Count) const RECT* Rects,
    _In_ UINT Count,
    _In_ POINT Offset)
{
    ID3D11Resource* To;
    ID3D11Resource* From;
    D3D11_BOX Box;
    UINT Index;

    if (FAILED(Destination->QueryInterface(IID_PPV_ARGS(&To))))
        return DXGI_ERROR_INVALID_CALL;
    if (FAILED(Source->QueryInterface(IID_PPV_ARGS(&From))))
    {
        To->Release();
        return DXGI_ERROR_INVALID_CALL;
    }

    for (Index = 0; Index < Count; Index++)
    {
        Box.left = (UINT)Rects[Index].left;
        Box.top = (UINT)Rects[Index].top;
        Box.right = (UINT)Rects[Index].right;
        Box.bottom = (UINT)Rects[Index].bottom;
        Box.front = 0;
        Box.back = 1;
        Device.GetContext().CopySubresourceRegion(To, 0, (UINT)(Rects[Index].left + Offset.x),
                                                  (UINT)(Rects[Index].top + Offset.y), 0, From, 0, &Box);
    }

    From->Release();
    To->Release();
    return S_OK;
}
