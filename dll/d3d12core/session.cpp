/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Protected resource sessions
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "session.hpp"

/* The kernel session a driver asks for while it makes its own; its status is reported through the fence */
HRESULT
RxCreateKernelSession12(
    _In_ RxKernelSession12& Session,
    _In_ const D3D12DDICB_CREATE_PROTECTED_SESSION_0030& Args)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CREATEPROTECTEDSESSION Create = {};
    NTSTATUS Status;
    HRESULT hr;

    if (Kmt->CreateProtectedSession == nullptr)
        return E_NOTIMPL;
    if (Session.Session != 0)
        return E_INVALIDARG;

    Create.hDevice = Session.Device->KernelDevice();
    Create.hSyncObject = Session.StatusFence;
    Create.pPrivateDriverData = Args.pPrivateDriverData;
    Create.PrivateDriverDataSize = Args.PrivateDriverDataSize;
    Create.pPrivateRuntimeData = Session.Desc;
    Create.PrivateRuntimeDataSize = sizeof(*Session.Desc);

    /* A lost device still lets the driver finish making its session */
    Status = Kmt->CreateProtectedSession(&Create);
    hr = RxKernelResult(*Session.Device, Status, "D3DKMTCreateProtectedSession", nullptr);
    if (hr == D3DDDIERR_DEVICEREMOVED)
        return S_OK;
    if (FAILED(hr))
    {
        RX_TRACE(L"D3DKMTCreateProtectedSession failed 0x%08lX", Status);
        return hr;
    }

    Session.Session = Create.hHandle;
    return S_OK;
}

HRESULT
RxProtectedResourceSession::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_PROTECTED_RESOURCE_SESSION_DESC1& Desc,
    _In_ UINT DriverNodeMask,
    _Outptr_ RxProtectedResourceSession** Session)
{
    RxProtectedResourceSession* Created;
    HRESULT hr;

    *Session = nullptr;

    if (FAILED(Device.CheckUsable()))
        return DXGI_ERROR_DEVICE_REMOVED;

    Created = new (std::nothrow) RxProtectedResourceSession(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc, DriverNodeMask);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Session = Created;
    return S_OK;
}

HRESULT
RxProtectedResourceSession::Initialize(
    _In_ const D3D12_PROTECTED_RESOURCE_SESSION_DESC1& SessionDesc,
    _In_ UINT DriverNodeMask)
{
    const D3D12DDI_DEVICE_FUNCS_CONTENT_PROTECTION_RESOURCES_0074& Ddi = Device.SessionDdi();
    D3D12DDIARG_CREATE_PROTECTED_RESOURCE_SESSION_0074 Args = {};
    D3D12DDI_HRTPROTECTEDSESSION_0030 Runtime;
    HRESULT hr;

    Desc = SessionDesc;
    if (Ddi.pfnCalcPrivateProtectedResourceSessionSize == nullptr || Ddi.pfnCreateProtectedResourceSession == nullptr)
        return E_NOTIMPL;

    /* The shared fence the kernel advances when the session stops being valid */
    hr = RxFence::Create(Device, 0, D3D12_FENCE_FLAG_SHARED, &StatusFence);
    if (FAILED(hr))
        return hr;

    Kernel.Device = &Device;
    Kernel.StatusFence = StatusFence->GetSyncObject();
    Kernel.Desc = &Desc;

    Args.NodeMask = DriverNodeMask;
    Args.ProtectionType = Desc.ProtectionType;
    DriverSession.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivateProtectedResourceSessionSize(Device.Handle(), &Args));
    if (DriverSession.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    Runtime.handle = &Kernel;
    hr = Ddi.pfnCreateProtectedResourceSession(Device.Handle(), &Args, DriverSession, Runtime);
    if (SUCCEEDED(hr))
        hr = RxDevice12::TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateProtectedResourceSession failed 0x%08lX", hr);
        return hr;
    }

    IsDriverSessionCreated = TRUE;
    return S_OK;
}

RxProtectedResourceSession::~RxProtectedResourceSession()
{
    D3DKMT_DESTROYPROTECTEDSESSION Destroy = {};

    if (IsDriverSessionCreated)
        Device.SessionDdi().pfnDestroyProtectedResourceSession(Device.Handle(), DriverSession);

    if (Kernel.Session != 0 && RxGetKmtThunks()->DestroyProtectedSession != nullptr)
    {
        Destroy.hHandle = Kernel.Session;
        RxGetKmtThunks()->DestroyProtectedSession(&Destroy);
    }

    if (StatusFence != nullptr)
        StatusFence->Release();
}

HRESULT
STDMETHODCALLTYPE
RxProtectedResourceSession::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12ProtectedSession) || riid == __uuidof(ID3D12ProtectedResourceSession) ||
        riid == __uuidof(ID3D12ProtectedResourceSession1) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12ProtectedResourceSession1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxProtectedResourceSession::GetStatusFence(
    REFIID riid,
    _COM_Outptr_opt_ void** ppFence)
{
    return StatusFence->QueryInterface(riid, ppFence);
}

D3D12_PROTECTED_SESSION_STATUS
STDMETHODCALLTYPE
RxProtectedResourceSession::GetSessionStatus(VOID)
{
    D3DKMT_QUERYPROTECTEDSESSIONSTATUS Query = {};

    if (Kernel.Session == 0 || RxGetKmtThunks()->QueryProtectedSessionStatus == nullptr)
        return D3D12_PROTECTED_SESSION_STATUS_OK;

    Query.hHandle = Kernel.Session;
    if (!NT_SUCCESS(RxGetKmtThunks()->QueryProtectedSessionStatus(&Query)))
        return D3D12_PROTECTED_SESSION_STATUS_INVALID;

    return Query.Status == D3DKMT_PROTECTED_SESSION_STATUS_OK ? D3D12_PROTECTED_SESSION_STATUS_OK
                                                             : D3D12_PROTECTED_SESSION_STATUS_INVALID;
}

D3D12_PROTECTED_RESOURCE_SESSION_DESC
STDMETHODCALLTYPE
RxProtectedResourceSession::GetDesc(VOID)
{
    D3D12_PROTECTED_RESOURCE_SESSION_DESC Old;

    Old.NodeMask = Desc.NodeMask;
    Old.Flags = Desc.Flags;
    return Old;
}

D3D12_PROTECTED_RESOURCE_SESSION_DESC1
STDMETHODCALLTYPE
RxProtectedResourceSession::GetDesc1(VOID)
{
    return Desc;
}

/* Device methods */

/*
 * A session goes on one node that has it, of a type the node lists, with no flags. Every rule is looked at;
 * the call fails if any is broken.
 */
static
HRESULT
RxCheckSessionDesc(
    _In_ RxDevice12& Device,
    _In_ const D3D12_PROTECTED_RESOURCE_SESSION_DESC1& Desc)
{
    D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT Count = {};
    D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES Types = {};
    GUID* Listed = nullptr;
    UINT Mask = Desc.NodeMask != 0 ? Desc.NodeMask : 1;
    UINT Node = 0;
    HRESULT hr = S_OK;
    UINT Index;

    if (Device.GetFeatureLevel() == D3D_FEATURE_LEVEL_1_0_GENERIC || Device.GetFeatureLevel() == D3D_FEATURE_LEVEL_1_0_CORE)
    {
        hr = E_INVALIDARG;
    }
    else if ((Desc.NodeMask & (Desc.NodeMask - 1)) != 0)
    {
        hr = E_INVALIDARG;
    }
    else
    {
        while ((Mask & (1u << Node)) == 0)
            Node++;
        if (Node >= Device.GetNodeCount())
            hr = E_INVALIDARG;

        Count.NodeIndex = Node;
        if (FAILED(Device.CheckFeatureSupport(D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT, &Count, sizeof(Count))))
            return E_INVALIDARG;

        if (Count.Count != 0)
        {
            Listed = new (std::nothrow) GUID[Count.Count];
            if (Listed == nullptr)
                return E_OUTOFMEMORY;
        }

        Types.NodeIndex = Node;
        Types.Count = Count.Count;
        Types.pTypes = Listed;
        if (FAILED(Device.CheckFeatureSupport(D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES, &Types, sizeof(Types))))
        {
            hr = E_INVALIDARG;
        }
        else
        {
            for (Index = 0; Index < Count.Count; Index++)
            {
                if (IsEqualGUID(Listed[Index], Desc.ProtectionType))
                    break;
            }
            if (Index == Count.Count)
                hr = E_INVALIDARG;
        }
        delete[] Listed;
    }

    if (Desc.Flags != D3D12_PROTECTED_RESOURCE_SESSION_FLAG_NONE)
        hr = E_INVALIDARG;
    return hr;
}

static
HRESULT
RxCreateSessionCommon(
    _In_ RxDevice12& Device,
    _In_ const D3D12_PROTECTED_RESOURCE_SESSION_DESC1& Desc,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppSession)
{
    D3D12_PROTECTED_RESOURCE_SESSION_DESC1 Kept = Desc;
    RxProtectedResourceSession* Session;
    UINT DriverMask;
    HRESULT hr;

    hr = RxCheckSessionDesc(Device, Desc);
    if (ppSession == nullptr)
        return FAILED(hr) ? hr : S_FALSE;
    if (FAILED(hr))
        return hr;
    *ppSession = nullptr;

    if (Kept.NodeMask == 0)
        Kept.NodeMask = 1;
    hr = Device.TranslateNodeMask(Kept.NodeMask, TRUE, &DriverMask);
    if (FAILED(hr))
        return hr;

    hr = RxProtectedResourceSession::Create(Device, Kept, DriverMask, &Session);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Session, riid, ppSession);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateProtectedResourceSession(
    _In_ const D3D12_PROTECTED_RESOURCE_SESSION_DESC* pDesc,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppSession)
{
    D3D12_PROTECTED_RESOURCE_SESSION_DESC1 Wide;

    Wide.NodeMask = pDesc->NodeMask;
    Wide.Flags = pDesc->Flags;
    Wide.ProtectionType = D3D12_PROTECTED_RESOURCES_SESSION_HARDWARE_PROTECTED;
    return RxCreateSessionCommon(*this, Wide, riid, ppSession);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateProtectedResourceSession1(
    _In_ const D3D12_PROTECTED_RESOURCE_SESSION_DESC1* pDesc,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppSession)
{
    return RxCreateSessionCommon(*this, *pDesc, riid, ppSession);
}
