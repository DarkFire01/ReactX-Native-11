/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The desktop window manager's swapchain: primaries flipped onto one output
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dwmswapchain.hpp"

HRESULT
RxDwmSwapChain::Create(
    _In_ RxDxgiFactory& Factory,
    _In_ IRxPresentDevice* Device,
    _In_ RxDxgiOutput* Output,
    _In_ const DXGI_SWAP_CHAIN_DESC& Desc,
    _Outptr_ RxDwmSwapChain** SwapChain)
{
    RxDwmSwapChain* NewSwapChain;
    HRESULT hr;

    *SwapChain = nullptr;

    /* Fullscreen on its output with no window, the only way the DWM makes one */
    if (Desc.Windowed || Desc.OutputWindow != NULL || Desc.BufferCount == 0 ||
        Desc.BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS || Desc.BufferDesc.Width == 0 || Desc.BufferDesc.Height == 0)
    {
        return DXGI_ERROR_INVALID_CALL;
    }
    if (Desc.SwapEffect != DXGI_SWAP_EFFECT_SEQUENTIAL && Desc.SwapEffect != DXGI_SWAP_EFFECT_DISCARD)
        return DXGI_ERROR_INVALID_CALL;

    NewSwapChain = new (std::nothrow) RxDwmSwapChain(Factory, Device, Output);
    if (NewSwapChain == nullptr)
        return E_OUTOFMEMORY;

    NewSwapChain->Desc = Desc;

    /* Ownership comes first: the kernel only lets an owner scan out its primaries */
    hr = NewSwapChain->TakeOwnership();
    if (SUCCEEDED(hr))
        hr = NewSwapChain->CreateBuffers();
    if (FAILED(hr))
    {
        RX_TRACE(L"DWM swapchain on source %u failed 0x%08lX", Output->GetVidPnSourceId(), hr);
        NewSwapChain->Release();
        return hr;
    }

    RX_TRACE(L"DWM swapchain on source %u: %ux%u format %u buffers %u", Output->GetVidPnSourceId(),
             Desc.BufferDesc.Width, Desc.BufferDesc.Height, Desc.BufferDesc.Format, Desc.BufferCount);
    *SwapChain = NewSwapChain;
    return S_OK;
}

RxDwmSwapChain::RxDwmSwapChain(
    _In_ RxDxgiFactory& Owner,
    _In_ IRxPresentDevice* BackingDevice,
    _In_ RxDxgiOutput* TargetOutput)
    : Factory(Owner),
      Device(BackingDevice),
      Output(TargetOutput)
{
    Factory.AddRef();
    Device->AddRef();
    Output->AddRef();
}

RxDwmSwapChain::~RxDwmSwapChain()
{
    ReleaseBuffers();
    ReleaseOwnership();
    Output->Release();
    Device->Release();
    Factory.Release();
}

HRESULT
RxDwmSwapChain::TakeOwnership()
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_VIDPNSOURCEOWNER_TYPE Type = D3DKMT_VIDPNSOURCEOWNER_SHARED;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = Output->GetVidPnSourceId();
    D3DKMT_SETVIDPNSOURCEOWNER1 Owner = {};
    NTSTATUS Status;

    if (Kmt->SetVidPnSourceOwner1 == nullptr && Kmt->SetVidPnSourceOwner == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    /* The revision with flags when there is one, so the source can be duplicated */
    Owner.Version0.hDevice = Device->GetKernelDevice();
    Owner.Version0.pType = &Type;
    Owner.Version0.pVidPnSourceId = &SourceId;
    Owner.Version0.VidPnSourceCount = 1;
    Owner.Flags.AllowOutputDuplication = 1;
    if (Kmt->SetVidPnSourceOwner1 != nullptr)
        Status = Kmt->SetVidPnSourceOwner1(&Owner);
    else
        Status = Kmt->SetVidPnSourceOwner(&Owner.Version0);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTSetVidPnSourceOwner shared on source %u failed 0x%08lX", SourceId, Status);
        return Status == STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE ? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE
                                                             : RxHresultFromStatus(Status);
    }

    OwnsSource = TRUE;
    return S_OK;
}

/* Giving the source up lets GDI's display driver present to it again */
VOID
RxDwmSwapChain::ReleaseOwnership()
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_SETVIDPNSOURCEOWNER1 Owner = {};

    if (!OwnsSource)
        return;

    Owner.Version0.hDevice = Device->GetKernelDevice();
    if (Kmt->SetVidPnSourceOwner1 != nullptr)
        Kmt->SetVidPnSourceOwner1(&Owner);
    else
        Kmt->SetVidPnSourceOwner(&Owner.Version0);
    OwnsSource = FALSE;
}

HRESULT
RxDwmSwapChain::CreateBuffers()
{
    DXGI_SWAP_CHAIN_DESC1 BufferDesc = {};
    DXGI_DDI_PRIMARY_DESC Primary = {};
    IUnknown* Buffer;
    UINT Index;
    HRESULT hr;

    BufferDesc.Width = Desc.BufferDesc.Width;
    BufferDesc.Height = Desc.BufferDesc.Height;
    BufferDesc.Format = Desc.BufferDesc.Format;
    BufferDesc.SampleDesc = Desc.SampleDesc;
    BufferDesc.BufferUsage = Desc.BufferUsage;
    BufferDesc.BufferCount = Desc.BufferCount;
    BufferDesc.SwapEffect = Desc.SwapEffect;

    /* Primaries are always scanned out in the plain format, without the sRGB view */
    Primary.Flags = (Desc.Flags & DXGI_SWAP_CHAIN_FLAG_NONPREROTATED) ? DXGI_DDI_PRIMARY_NONPREROTATED : 0;
    Primary.VidPnSourceId = Output->GetVidPnSourceId();
    Primary.ModeDesc.Width = Desc.BufferDesc.Width;
    Primary.ModeDesc.Height = Desc.BufferDesc.Height;
    Primary.ModeDesc.Format = Desc.BufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ? DXGI_FORMAT_B8G8R8A8_UNORM
                                                                                          : Desc.BufferDesc.Format;
    Primary.ModeDesc.RefreshRate.Numerator = Desc.BufferDesc.RefreshRate.Numerator;
    Primary.ModeDesc.RefreshRate.Denominator = Desc.BufferDesc.RefreshRate.Denominator;
    Primary.ModeDesc.ScanlineOrdering = (DXGI_DDI_MODE_SCANLINE_ORDER)Desc.BufferDesc.ScanlineOrdering;
    Primary.ModeDesc.Rotation = DXGI_DDI_MODE_ROTATION_IDENTITY;
    Primary.ModeDesc.Scaling = (DXGI_DDI_MODE_SCALING)Desc.BufferDesc.Scaling;

    for (Index = 0; Index < Desc.BufferCount; Index++)
    {
        hr = Device->CreatePrimaryBuffer(&BufferDesc, &Primary, static_cast<IDXGISwapChainDWM1*>(this), &Buffer);
        if (FAILED(hr))
            return hr;
        Buffers.push_back(Buffer);
    }

    /* The last buffer is the front one; buffer 0 is always the next to draw */
    return Device->SetDisplaySurface(Buffers.back());
}

VOID
RxDwmSwapChain::ReleaseBuffers()
{
    for (IUnknown* Buffer : Buffers)
    {
        Device->DetachPresentBuffer(Buffer);
        Buffer->Release();
    }
    Buffers.clear();
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISwapChainDWM1) || riid == IID_IDXGISwapChainDWM)
    {
        *ppvObject = static_cast<IDXGISwapChainDWM1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG
STDMETHODCALLTYPE
RxDwmSwapChain::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

ULONG
STDMETHODCALLTYPE
RxDwmSwapChain::Release(VOID)
{
    ULONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        delete this;
    return Count;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::SetPrivateData(
    _In_ REFGUID Name,
    _In_ UINT DataSize,
    _In_reads_bytes_(DataSize) const void* pData)
{
    return PrivateData.Set(Name, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::SetPrivateDataInterface(
    _In_ REFGUID Name,
    _In_opt_ const IUnknown* pUnknown)
{
    return PrivateData.Set(Name, 0, nullptr, pUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetPrivateData(
    _In_ REFGUID Name,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_(*pDataSize) void* pData)
{
    return PrivateData.Get(Name, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetParent(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppParent)
{
    if (ppParent == nullptr)
        return E_INVALIDARG;

    return Factory.QueryInterface(riid, ppParent);
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetDevice(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppDevice)
{
    if (ppDevice == nullptr)
        return E_INVALIDARG;

    return Device->QueryInterface(riid, ppDevice);
}

/* What a test present says about the output: the kernel knows whether someone else holds the source */
static
HRESULT
RxCheckSourceOwnership(
    _In_ RxDxgiOutput* Output)
{
    D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP Check = {};
    NTSTATUS Status;

    if (RxGetKmtThunks()->CheckVidPnExclusiveOwnership == nullptr)
        return S_OK;

    Check.hAdapter = Output->GetKernelAdapter();
    Check.VidPnSourceId = Output->GetVidPnSourceId();
    Status = RxGetKmtThunks()->CheckVidPnExclusiveOwnership(&Check);
    switch (Status)
    {
        case STATUS_GRAPHICS_PRESENT_OCCLUDED:
            return DXGI_STATUS_OCCLUDED;

        case STATUS_GRAPHICS_PRESENT_MODE_CHANGED:
            return DXGI_STATUS_MODE_CHANGED;

        case STATUS_GRAPHICS_PRESENT_UNOCCLUDED:
            return DXGI_STATUS_UNOCCLUDED;

        default:
            return RxHresultFromStatus(Status);
    }
}

/* Flips buffer 0 onto the output, then moves the identities so buffer 0 is free to draw again */
HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::Present(
    _In_ UINT SyncInterval,
    _In_ UINT Flags)
{
    RxPresentContext* Context;
    D3DKMT_PRESENT* Template;
    HRESULT hr;

    if (SyncInterval > 4)
        return DXGI_ERROR_INVALID_CALL;
    if (!OwnsSource)
        return DXGI_STATUS_OCCLUDED;
    if (Flags & DXGI_PRESENT_TEST)
    {
        static LONG LastTest;

        /* Only changes are traced; the compositor asks often */
        hr = RxCheckSourceOwnership(Output);
        if (InterlockedExchange(&LastTest, hr) != hr)
            RX_TRACE(L"test present on source %u: 0x%08lX", Output->GetVidPnSourceId(), hr);
        return hr;
    }

    /* Duplication clients get the frame before it is flipped away from buffer 0 */
    if (Output->HasDDAClient())
        PresentToDuplication();

    Context = &Contexts[NextContext];
    NextContext = (NextContext + 1) % Contexts.size();
    *Context = {};
    Template = &Context->Template;

    Context->SourceRect = { 0, 0, (LONG)Desc.BufferDesc.Width, (LONG)Desc.BufferDesc.Height };
    Template->VidPnSourceId = Output->GetVidPnSourceId();
    Template->SrcRect = Context->SourceRect;
    Template->SubRectCnt = 1;
    Template->pSrcSubRects = &Context->SourceRect;
    Template->PresentCount = PresentCount + 1;
    Template->FlipInterval = (D3DDDI_FLIPINTERVAL_TYPE)SyncInterval;
    /* A flip takes the whole primary; the kernel refuses rectangles and blt bits with it */
    Template->Flags.Flip = 1;
    Template->Flags.PresentCountValid = 1;
    Template->Flags.FlipDoNotFlip = (Flags & DXGI_PRESENT_DO_NOT_SEQUENCE) ? 1 : 0;
    Template->Flags.FlipDoNotWait = (Flags & DXGI_PRESENT_DO_NOT_WAIT) ? 1 : 0;
    Template->Flags.FlipRestart = (Flags & DXGI_PRESENT_RESTART) ? 1 : 0;

    hr = Device->PresentPrimary(Buffers[0], SyncInterval, Context);
    if (Context->KernelStatus == STATUS_GRAPHICS_PRESENT_OCCLUDED)
        return DXGI_STATUS_OCCLUDED;

    /* The output runs another mode now, the compositor has to make its swapchain again */
    if (Context->KernelStatus == STATUS_GRAPHICS_PRESENT_MODE_CHANGED)
        return DXGI_STATUS_MODE_CHANGED;
    if (FAILED(hr))
        return hr;

    PresentCount++;
    return Device->RotateBuffers(Buffers.data(), (UINT)Buffers.size());
}

/*
 * A blt of buffer 0 with no destination, marked as a DDA present. The runtime's present callback
 * passes it to D3DKMTOutputDuplPresent, and the kernel copies the frame into each client's texture.
 */
VOID
RxDwmSwapChain::PresentToDuplication()
{
    RxPresentContext* Context;
    D3DKMT_PRESENT* Template;
    HRESULT hr;

    Context = &Contexts[NextContext];
    NextContext = (NextContext + 1) % Contexts.size();
    *Context = {};
    Template = &Context->Template;

    Context->SourceRect = { 0, 0, (LONG)Desc.BufferDesc.Width, (LONG)Desc.BufferDesc.Height };
    Template->VidPnSourceId = Output->GetVidPnSourceId();
    Template->SrcRect = Context->SourceRect;
    Template->SubRectCnt = 1;
    Template->pSrcSubRects = &Context->SourceRect;
    Template->PresentCount = PresentCount + 1;
    Template->FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
    Template->Flags.Blt = 1;
    Template->Flags.SrcRectValid = 1;
    Template->Flags.PresentCountValid = 1;
    Template->Flags.PresentDDA = 1;

    /* The DWM keeps presenting whether or not the copy went through */
    hr = Device->SubmitPresent(Buffers[0], nullptr, Context);
    if (FAILED(hr))
    {
        RX_TRACE(L"Duplication present on source %u failed 0x%08lX (kernel 0x%08lX)", Output->GetVidPnSourceId(), hr,
                 Context->KernelStatus);
    }
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetBuffer(
    _In_ UINT Buffer,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppSurface)
{
    if (ppSurface == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *ppSurface = nullptr;
    if (Buffer >= Buffers.size())
        return DXGI_ERROR_INVALID_CALL;

    return Buffers[Buffer]->QueryInterface(riid, ppSurface);
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetDesc(
    _Out_ DXGI_SWAP_CHAIN_DESC* pDesc)
{
    if (pDesc == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pDesc = Desc;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::ResizeBuffers(
    _In_ UINT BufferCount,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ DXGI_FORMAT NewFormat,
    _In_ UINT SwapChainFlags)
{
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::ResizeTarget(
    _In_ const DXGI_MODE_DESC* pNewTargetParameters)
{
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetContainingOutput(
    _COM_Outptr_ IDXGIOutput** ppOutput)
{
    if (ppOutput == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *ppOutput = static_cast<IDXGIOutput6*>(Output);
    Output->AddRef();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetFrameStatistics(
    _Out_ DXGI_FRAME_STATISTICS* pStats)
{
    if (pStats == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pStats = {};
    return DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetLastPresentCount(
    _Out_ UINT* pLastPresentCount)
{
    if (pLastPresentCount == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pLastPresentCount = PresentCount;
    return S_OK;
}

/* Every flip sends the whole buffer, so the dirty and scroll rectangles are only hints */
HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::PresentDWM(
    _In_ UINT SyncInterval,
    _In_ UINT Flags,
    _In_ UINT DirtyRectCount,
    _In_reads_opt_(DirtyRectCount) const RECT* pDirtyRects,
    _In_ UINT ScrollRectCount,
    _In_reads_opt_(ScrollRectCount) const DXGI_SCROLL_RECT* pScrollRects,
    _In_opt_ IDXGIResource* pResource,
    _In_ UINT Reserved)
{
    if (pResource != nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    return Present(SyncInterval, Flags);
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetLogicalSurfaceHandle(
    _Out_ UINT64* pHandle)
{
    if (pHandle == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pHandle = 0;
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::CheckDirectFlipSupport(
    _In_ UINT Flags,
    _In_ IDXGIResource* pResource,
    _Out_ BOOL* pSupported)
{
    if (pSupported == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pSupported = FALSE;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetCompositionSurface(
    _Out_ HANDLE* pSurface)
{
    if (pSurface == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pSurface = NULL;
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetFrameStatisticsDWM(
    _Out_ DXGI_FRAME_STATISTICS_DWM* pStats)
{
    return Output->GetFrameStatisticsDWM(pStats);
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetMultiplaneOverlayCaps(
    _Out_ DXGI_MULTIPLANE_OVERLAY_CAPS* pCaps)
{
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::CheckMultiplaneOverlaySupport(
    _In_ UINT Count,
    _In_ const DXGI_CHECK_MULTIPLANEOVERLAYSUPPORT_PLANE_INFO* pInfo,
    _Out_ BOOL* pSupported,
    _Out_ BOOL* pIndividuallySupported)
{
    if (pSupported == nullptr || pIndividuallySupported == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pSupported = FALSE;
    *pIndividuallySupported = FALSE;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::PresentMultiplaneOverlay(
    _In_ UINT SyncInterval,
    _In_ UINT Flags,
    _In_ DXGI_HDR_METADATA_TYPE MetadataType,
    _In_opt_ const void* pMetadata,
    _In_ UINT Count,
    _In_ const DXGI_PRESENT_MULTIPLANE_OVERLAY* pPlanes)
{
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::CheckPresentDurationSupport(
    _In_ UINT Duration,
    _Out_ UINT* pClosestSmaller,
    _Out_ UINT* pClosestLarger)
{
    if (pClosestSmaller == nullptr || pClosestLarger == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pClosestSmaller = 0;
    *pClosestLarger = 0;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::SetPrivateFrameDuration(
    _In_ UINT Duration,
    _In_ UINT Flags)
{
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::SetHardwareProtection(
    _In_ BOOL Protect)
{
    return Protect ? DXGI_ERROR_UNSUPPORTED : S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::GetHardwareProtection(
    _Out_ BOOL* pProtected)
{
    if (pProtected == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pProtected = FALSE;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::SetLatencyHint(
    _In_ UINT Hint)
{
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::SwapBuffers(
    _In_ UINT First,
    _In_ UINT Second)
{
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::CheckDwmVidPnOwnership(
    _Out_ BOOL* pOwned)
{
    if (pOwned == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pOwned = OwnsSource;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::Reserved31()
{
    return S_OK;
}

/* Identities rotate on every present, so the buffer to draw is always 0 */
UINT
STDMETHODCALLTYPE
RxDwmSwapChain::GetBackBufferImplicitRotationCount()
{
    return 0;
}

UINT
STDMETHODCALLTYPE
RxDwmSwapChain::GetFrontBufferRenderingCapability()
{
    return 0;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::SetFrontBufferRenderingMode(
    _In_ BOOL Enable)
{
    return Enable ? DXGI_ERROR_UNSUPPORTED : S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDwmSwapChain::SetPlaneFlipAwaySignals(
    _In_ UINT Count,
    _In_reads_(Count) const DXGI_PRESENT_PLANE_FLIP_AWAY_SIGNAL* pSignals)
{
    return Count == 0 ? S_OK : DXGI_ERROR_UNSUPPORTED;
}
