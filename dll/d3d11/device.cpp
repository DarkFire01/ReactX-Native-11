/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device creation and the hand written ID3D11Device methods
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "resourcecheck.hpp"
#include "shaders.hpp"
#include "states.hpp"
#include "textures.hpp"
#include "views.hpp"

#include <malloc.h>

static
UINT
RxEncodePipelineLevel(
    _In_ D3D11DDI_3DPIPELINELEVEL Level)
{
    /* The level is split across two bit fields of the create flags */
    return ((Level << D3D11DDI_CREATEDEVICE_FLAG_3DPIPELINESUPPORT_SHIFT) &
            D3D11DDI_CREATEDEVICE_FLAG_3DPIPELINESUPPORT_MASK) |
           ((Level << D3D11DDI_CREATEDEVICE_FLAG_3DPIPELINESUPPORT_SHIFT2) &
            D3D11DDI_CREATEDEVICE_FLAG_3DPIPELINESUPPORT_MASK2);
}

RxDevice::RxDevice(
    _In_ UmdAdapter* Adapter,
    _In_ D3D_FEATURE_LEVEL FeatureLevel,
    _In_ UINT CreationFlags)
    : Adapter(Adapter),
      FeatureLevel(FeatureLevel),
      CreationFlags(CreationFlags),
      Context(*this),
      VideoDevice(*this),
      DxgiDevice(*this),
      PresentBridge(*this),
      Multithread(*this, *this),
      CompositionPartner(*this),
      D3D10Device(*this)
{
    InitializeCriticalSection(&ThreadLock);
}

RxDevice::~RxDevice()
{
    D3DKMT_DESTROYDEVICE Destroy;

    if (DxgiAdapter != nullptr)
        DxgiAdapter->Release();

    DeleteCriticalSection(&ThreadLock);

    /* Bound objects and defaults destroy driver objects, so they go while the driver device lives */
    Context.ReleaseBindings();
    Defaults = {};

    if (IsDriverDeviceCreated)
        DeviceFuncs.pfnDestroyDevice(DriverDevice);

    if (LayerQueue != nullptr)
        LayerQueue->Release();
    if (LayerPrivate != nullptr)
        LayerPrivate->Release();
    if (LayerDevice12 != nullptr)
        LayerDevice12->Release();

    /* Whatever the driver never deallocated goes with the kernel device */
    for (RxRuntimeResource* Resource : OrphanedResources)
    {
        D3DKMT_DESTROYALLOCATION DestroyResource = {};

        if (Resource->KmResource != 0 && KmDevice != 0)
        {
            DestroyResource.hDevice = KmDevice;
            DestroyResource.hResource = Resource->KmResource;
            RxGetKmtThunks()->DestroyAllocation(&DestroyResource);
        }
        RxCloseKeyedMutex(*Resource);
        delete Resource;
    }
    OrphanedResources.clear();

    if (DriverMemory != nullptr)
        _aligned_free(DriverMemory);

    if (KmDevice != 0)
    {
        Destroy.hDevice = KmDevice;
        RxGetKmtThunks()->DestroyDevice(&Destroy);
    }

    delete Adapter;
}

VOID
RxDevice::RetireRuntimeResource(
    _In_ RxRuntimeResource* Resource)
{
    AcquireSRWLockExclusive(&RuntimeResourceLock);
    if (Resource->KmResource == 0)
    {
        RxCloseKeyedMutex(*Resource);
        delete Resource;
    }
    else
    {
        Resource->IsOrphaned = TRUE;
        OrphanedResources.push_back(Resource);
    }
    ReleaseSRWLockExclusive(&RuntimeResourceLock);
}

VOID
RxDevice::ReleaseRuntimeResource(
    _In_ RxRuntimeResource* Resource)
{
    AcquireSRWLockExclusive(&RuntimeResourceLock);
    Resource->KmResource = 0;
    if (Resource->IsOrphaned)
    {
        std::erase(OrphanedResources, Resource);
        RxCloseKeyedMutex(*Resource);
        delete Resource;
    }
    ReleaseSRWLockExclusive(&RuntimeResourceLock);
}

HRESULT
RxDevice::Create(
    _In_ UmdAdapter* Adapter,
    _In_ D3D_FEATURE_LEVEL FeatureLevel,
    _In_ D3D11DDI_3DPIPELINELEVEL PipelineLevel,
    _In_ UINT CreationFlags,
    _Outptr_ RxDevice** Device)
{
    RxDevice* NewDevice;
    HRESULT hr;

    *Device = nullptr;

    NewDevice = new (std::nothrow) RxDevice(Adapter, FeatureLevel, CreationFlags);
    if (NewDevice == nullptr)
    {
        delete Adapter;
        return E_OUTOFMEMORY;
    }

    hr = NewDevice->Initialize(PipelineLevel);
    if (FAILED(hr))
    {
        NewDevice->Release();
        return hr;
    }

    *Device = NewDevice;
    return S_OK;
}

HRESULT
RxDevice::Initialize(
    _In_ D3D11DDI_3DPIPELINELEVEL PipelineLevel)
{
    const D3D10_2DDI_ADAPTERFUNCS& AdapterFuncs = Adapter->GetFunctions();
    D3D10DDIARG_CALCPRIVATEDEVICESIZE Calc = {};
    D3D10DDIARG_CREATEDEVICE Args = {};
    D3DKMT_CREATEDEVICE KmCreate = {};
    SIZE_T DriverSize;
    NTSTATUS Status;
    UINT Flags;
    HRESULT hr;

    /* The layer runs on the D3D12 device's kernel device and always at its strongest level */
    if (Adapter->IsLayer())
    {
        PipelineLevel = Adapter->GetHighestPipelineLevel();
    }
    else
    {
        KmCreate.hAdapter = Adapter->GetKmtHandle();
        Status = RxGetKmtThunks()->CreateDevice(&KmCreate);
        if (!NT_SUCCESS(Status))
        {
            RX_TRACE(L"D3DKMTCreateDevice failed 0x%08lX", Status);
            return RxHresultFromStatus(Status);
        }
        KmDevice = KmCreate.hDevice;
    }

    Flags = RxEncodePipelineLevel(PipelineLevel);
    if (CreationFlags & D3D11_CREATE_DEVICE_SINGLETHREADED)
        Flags |= D3D11DDI_CREATEDEVICE_FLAG_SINGLETHREADED;
    if (CreationFlags & D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS)
        Flags |= D3D10DDI_CREATEDEVICE_FLAG_DISABLE_EXTRA_THREAD_CREATION;
    if (CreationFlags & D3D11_CREATE_DEVICE_DEBUGGABLE)
        Flags |= D3D11DDI_CREATEDEVICE_FLAG_DEBUGGABLE;

    Calc.Interface = Adapter->GetRevision()->Interface;
    Calc.Version = Adapter->GetRuntimeVersion();
    Calc.Flags = Flags;

    DriverSize = AdapterFuncs.pfnCalcPrivateDeviceSize(Adapter->GetDriverHandle(), &Calc);
    DriverMemory = _aligned_malloc(DriverSize, 16);
    if (DriverMemory == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(DriverMemory, DriverSize);

    Args.hRTDevice.handle = this;
    Args.Interface = Calc.Interface;
    Args.Version = Calc.Version;
    Args.pKTCallbacks = RxGetKernelCallbacks();
    Args.pWDDM2_6DeviceFuncs = &DeviceFuncs;
    Args.hDrvDevice.pDrvPrivate = DriverMemory;
    Args.DXGIBaseDDI.pDXGIBaseCallbacks = RxGetDxgiCallbacks();
    Args.DXGIBaseDDI.pDXGIDDIBaseFunctions6_1 = &DxgiFuncs;
    Args.hRTCoreLayer.handle = this;
    Args.pWDDM2_6UMCallbacks = RxGetCoreLayerCallbacks();
    Args.Flags = Flags;
    Args.ppfnRetrieveSubObject = &RetrieveSubObject;

    hr = AdapterFuncs.pfnCreateDevice(Adapter->GetDriverHandle(), &Args);
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateDevice failed 0x%08lX", hr);
        return hr;
    }

    DriverDevice = Args.hDrvDevice;
    IsDriverDeviceCreated = TRUE;

    RX_TRACE(L"device created: %Iu bytes of driver state, sub-object lookup %s",
             DriverSize,
             RetrieveSubObject != nullptr ? L"present" : L"absent");

    if (Adapter->IsLayer())
    {
        hr = AttachLayer();
        if (FAILED(hr))
            return hr;
    }

    /* A new device starts with its default states bound, like the D3D11 runtime's */
    hr = BuildStockStates();
    if (FAILED(hr))
    {
        RX_TRACE(L"creating the default states failed 0x%08lX", hr);
        return hr;
    }

    /* A layer device asks for its virtual address limits once its defaults exist */
    if (LayerDevice12 != nullptr)
    {
        D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT Address = {};

        LayerDevice12->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &Address, sizeof(Address));
    }
    Context.LoadPipelineDefaults();
    return S_OK;
}

HRESULT
RxDevice::EnsureVideoFuncs()
{
    D3D11_1DDI_VIDEO_INPUT Input = {};
    D3D11_1DDI_VIDEO_OUTPUT Output = {};

    AcquireSRWLockExclusive(&VideoLock);

    if (!IsVideoQueried)
    {
        IsVideoQueried = TRUE;

        if (RetrieveSubObject != nullptr)
        {
            Input.Relocate = FALSE;
            Input.p11VideoDeviceFuncs = &VideoFuncs;
            VideoStatus = RetrieveSubObject(DriverDevice,
                                            D3D11_1DDI_VIDEO_FUNCTIONS,
                                            sizeof(Input),
                                            &Input,
                                            sizeof(Output),
                                            &Output);
        }

        RX_TRACE(L"video function table: 0x%08lX", VideoStatus);
    }

    ReleaseSRWLockExclusive(&VideoLock);
    return VideoStatus;
}

/* SetErrorCb runs on the thread that made the failing call, so each thread keeps its own */
static thread_local HRESULT RxThreadDriverError = S_OK;

VOID
RxDevice::SetDriverError(
    _In_ HRESULT Error)
{
    if (Error != DXGI_DDI_ERR_WASSTILLDRAWING)
        RX_TRACE(L"UMD reported error 0x%08lX", Error);
    if (RxThreadDriverError == S_OK)
        RxThreadDriverError = Error;
}

HRESULT
RxDevice::TakeDriverError()
{
    HRESULT Error = RxThreadDriverError;

    RxThreadDriverError = S_OK;
    return Error;
}

/* True when the driver speaks the WDDM 2.0 DDI or a later one */
BOOLEAN
RxDevice::IsWddm2Driver()
{
    return GetAdapter().GetRevision()->Interface >= D3DWDDM2_0_DDI_INTERFACE_VERSION;
}

/* From WDDM 2.0 the flush entry takes the context types to flush ahead of the flags */
VOID
RxDevice::FlushDriver(
    _In_ UINT Flags)
{
    if (IsWddm2Driver())
        DeviceFuncs.pfnFlush(DriverDevice, D3DWDDM2_0DDI_CONTEXTTYPE_3D, Flags);
    else
        GetDeviceFuncs().pfnFlush(DriverDevice, Flags);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    WCHAR Text[40];

    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == IID_RxDevice)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11Device) || riid == __uuidof(ID3D11Device1) ||
        riid == __uuidof(ID3D11Device2))
    {
        *ppvObject = static_cast<ID3D11Device2*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDevice) || riid == __uuidof(IDXGIDevice1) ||
        riid == __uuidof(IDXGIDevice2) || riid == __uuidof(IDXGIDevice3))
    {
        *ppvObject = static_cast<IDXGIDevice3*>(&DxgiDevice);
        AddRef();
        return S_OK;
    }

    /* A device made single threaded has no lock to hand out */
    if (riid == __uuidof(ID3D10Multithread) && !(CreationFlags & D3D11_CREATE_DEVICE_SINGLETHREADED))
    {
        *ppvObject = static_cast<ID3D10Multithread*>(&Multithread);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IRxPresentDevice))
    {
        *ppvObject = static_cast<IRxPresentDevice*>(&PresentBridge);
        AddRef();
        return S_OK;
    }

    if ((riid == __uuidof(ID3D10Device) || riid == __uuidof(ID3D10Device1)) && IsD3D10())
    {
        *ppvObject = static_cast<ID3D10Device1*>(&D3D10Device);
        AddRef();
        return S_OK;
    }

    if (riid == IID_RxCompositionPartner || riid == IID_RxCompositionPartner1 || riid == IID_RxCompositionPartner0)
    {
        *ppvObject = static_cast<IRxCompositionPartner*>(&CompositionPartner);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D11VideoDevice) && SUCCEEDED(EnsureVideoFuncs()))
    {
        *ppvObject = static_cast<ID3D11VideoDevice*>(&VideoDevice);
        AddRef();
        return S_OK;
    }

    if (RxIsTraceEnabled())
    {
        StringFromGUID2(riid, Text, _countof(Text));
        RxTrace(L"device QueryInterface %s not supported", Text);
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG
STDMETHODCALLTYPE
RxDevice::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

ULONG
STDMETHODCALLTYPE
RxDevice::Release(VOID)
{
    ULONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        delete this;

    return Count;
}

D3D_FEATURE_LEVEL
STDMETHODCALLTYPE
RxDevice::GetFeatureLevel(VOID)
{
    return FeatureLevel;
}

UINT
STDMETHODCALLTYPE
RxDevice::GetCreationFlags(VOID)
{
    return CreationFlags;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::GetDeviceRemovedReason(VOID)
{
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxDevice::GetImmediateContext(
    _Outptr_ ID3D11DeviceContext** ppImmediateContext)
{
    Context.AddRef();
    *ppImmediateContext = &Context;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::SetExceptionMode(
    UINT RaiseFlags)
{
    if ((RaiseFlags & ~D3D11_RAISE_FLAG_DRIVER_INTERNAL_ERROR) != 0)
        return E_INVALIDARG;

    ExceptionMode = RaiseFlags;
    return S_OK;
}

UINT
STDMETHODCALLTYPE
RxDevice::GetExceptionMode(VOID)
{
    return ExceptionMode;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CheckMultisampleQualityLevels(
    _In_ DXGI_FORMAT Format,
    _In_ UINT SampleCount,
    _Out_ UINT* pNumQualityLevels)
{
    if (pNumQualityLevels == nullptr)
        return E_INVALIDARG;

    *pNumQualityLevels = 0;
    if (SampleCount == 0 || SampleCount > D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT)
        return E_INVALIDARG;

    /* WDDM 1.3 added a flags argument to this entry in place */
    if (Adapter->GetRevision()->Interface >= D3DWDDM1_3_DDI_INTERFACE_VERSION)
    {
        DeviceFuncs.pfnCheckMultisampleQualityLevels(DriverDevice, Format, SampleCount, 0, pNumQualityLevels);
    }
    else
    {
        reinterpret_cast<PFND3D10DDI_CHECKMULTISAMPLEQUALITYLEVELS>(
            DeviceFuncs.pfnCheckMultisampleQualityLevels)(DriverDevice, Format, SampleCount, pNumQualityLevels);
    }

    return TakeDriverError();
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateTexture2D(
    _In_ const D3D11_TEXTURE2D_DESC* pDesc,
    _In_reads_opt_(_Inexpressible_(pDesc->MipLevels* pDesc->ArraySize)) const D3D11_SUBRESOURCE_DATA* pInitialData,
    _COM_Outptr_opt_ ID3D11Texture2D** ppTexture2D)
{
    RxTexture2D* Texture;
    HRESULT hr;

    if (ppTexture2D != nullptr)
        *ppTexture2D = nullptr;
    if (pDesc == nullptr || FAILED(RxCheckTextureRequest(*this, RxMakeTextureRequest(*pDesc), pInitialData)))
        return E_INVALIDARG;

    hr = RxTexture2D::Create(*this, *pDesc, pInitialData, &Texture);
    if (FAILED(hr))
    {
        RX_TRACE(L"CreateTexture2D failed 0x%08lX", hr);
        return hr;
    }

    /* A null output pointer only validates the arguments */
    if (ppTexture2D == nullptr)
    {
        Texture->Release();
        return S_FALSE;
    }

    *ppTexture2D = Texture;
    return S_OK;
}

/* Opens a shared texture and hands out the interface asked for; every failure to open is E_INVALIDARG */
static
HRESULT
RxOpenSharedTexture(
    _In_ RxDevice& Device,
    _In_ HANDLE SharedHandle,
    _In_ BOOLEAN IsNtHandle,
    _In_ REFIID riid,
    _Outptr_result_maybenull_ void** ppResource)
{
    RxTexture2D* Texture;
    HRESULT hr;

    if (ppResource == nullptr || SharedHandle == NULL)
        return E_INVALIDARG;

    *ppResource = nullptr;
    hr = RxTexture2D::Open(Device, SharedHandle, IsNtHandle, &Texture);
    if (FAILED(hr))
    {
        RX_TRACE(L"OpenSharedResource%s failed 0x%08lX", IsNtHandle ? L"1" : L"", hr);
        return (hr == E_OUTOFMEMORY) ? hr : E_INVALIDARG;
    }

    hr = Texture->QueryInterface(riid, ppResource);
    Texture->Release();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::OpenSharedResource(
    _In_ HANDLE hResource,
    _In_ REFIID ReturnedInterface,
    _COM_Outptr_opt_ void** ppResource)
{
    return RxOpenSharedTexture(*this, hResource, FALSE, ReturnedInterface, ppResource);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::OpenSharedResource1(
    _In_ HANDLE hResource,
    _In_ REFIID returnedInterface,
    _COM_Outptr_ void** ppResource)
{
    return RxOpenSharedTexture(*this, hResource, TRUE, returnedInterface, ppResource);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::GetPrivateData(
    _In_ REFGUID guid,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_opt_(*pDataSize) void* pData)
{
    return PrivateData.Get(guid, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::SetPrivateData(
    _In_ REFGUID guid,
    _In_ UINT DataSize,
    _In_reads_bytes_opt_(DataSize) const void* pData)
{
    return PrivateData.Set(guid, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::SetPrivateDataInterface(
    _In_ REFGUID guid,
    _In_opt_ const IUnknown* pData)
{
    return PrivateData.Set(guid, 0, nullptr, pData);
}

_Ret_maybenull_
RxDevice*
RxGetDevice(
    _In_opt_ IUnknown* Unknown)
{
    RxDevice* Found = nullptr;

    if (Unknown == nullptr || FAILED(Unknown->QueryInterface(IID_RxDevice, reinterpret_cast<void**>(&Found))))
        return nullptr;

    return Found;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateRenderTargetView(
    _In_ ID3D11Resource* pResource,
    _In_opt_ const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
    _COM_Outptr_opt_ ID3D11RenderTargetView** ppRTView)
{
    RxRenderTargetView* View;
    HRESULT hr;

    if (ppRTView != nullptr)
        *ppRTView = nullptr;
    if (pResource == nullptr)
        return E_INVALIDARG;

    hr = RxRenderTargetView::Create(*this, pResource, pDesc, &View);
    if (FAILED(hr))
    {
        RX_TRACE(L"CreateRenderTargetView failed 0x%08lX", hr);
        return hr;
    }

    /* A null output pointer only validates the arguments */
    if (ppRTView == nullptr)
    {
        View->Release();
        return S_FALSE;
    }

    *ppRTView = View;
    return S_OK;
}

RxKernelDevice&
RxKernelDeviceFromHandle(
    _In_ HANDLE Handle)
{
    return *static_cast<RxDevice*>(Handle);
}
