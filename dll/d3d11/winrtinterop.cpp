/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Windows.Graphics.DirectX.Direct3D11 objects over DXGI devices and surfaces
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

#include <inspectable.h>
#include <weakreference.h>
#include <windows.foundation.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>

using ABI::Windows::Foundation::IClosable;
using ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface;
using ABI::Windows::Graphics::DirectX::Direct3D11::Direct3DSurfaceDescription;
using Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;

/* What a closed object answers once the DXGI object it wrapped is gone */
static constexpr HRESULT RxClosedResult = RO_E_CLOSED;

/*
 * Strong and weak counts live apart from the object, so a weak reference can outlast it.
 * The block goes away when both reach zero.
 */
class RxWeakBlock final : public IWeakReference
{
public:
    explicit RxWeakBlock(
        _In_ IInspectable* Target)
        : Object(Target)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWeakReference))
        {
            *ppvObject = static_cast<IWeakReference*>(this);
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
        return InterlockedIncrement(&Weak);
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        ULONG Count = InterlockedDecrement(&Weak);

        if (Count == 0)
            delete this;
        return Count;
    }

    HRESULT
    STDMETHODCALLTYPE
    Resolve(
        _In_ REFIID riid,
        _COM_Outptr_ IInspectable** objectReference) override
    {
        LONG Seen;
        HRESULT hr;

        if (objectReference == nullptr)
            return E_POINTER;
        *objectReference = nullptr;

        /* Only an object that still has a strong reference can hand out another */
        for (Seen = Strong; Seen > 0; Seen = Strong)
        {
            if (InterlockedCompareExchange(&Strong, Seen + 1, Seen) == Seen)
                break;
        }

        if (Seen <= 0)
            return S_OK;

        hr = Object->QueryInterface(riid, reinterpret_cast<void**>(objectReference));
        Object->Release();
        return hr;
    }

    LONG Strong = 1;

    /* The object itself holds one weak reference until it is destroyed */
    LONG Weak = 1;

private:
    IInspectable* Object;
};

/*
 * The parts both objects share. Wrapped is the DXGI interface the object hands out
 * through IDirect3DDxgiInterfaceAccess; Close drops it.
 */
template <typename Face, typename Wrapped>
class RxDirect3DObject : public Face,
                         public IClosable,
                         public IDirect3DDxgiInterfaceAccess,
                         public IWeakReferenceSource,
                         public IAgileObject,
                         public IMarshal
{
public:
    RxDirect3DObject(
        _In_ Wrapped* Inner)
        : Dxgi(Inner)
    {
        Dxgi->AddRef();
    }

    BOOLEAN
    Initialize()
    {
        Control = new (std::nothrow) RxWeakBlock(static_cast<Face*>(this));
        return Control != nullptr;
    }

    /* IUnknown and IInspectable */

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override
    {
        IUnknown* Found = nullptr;

        if (ppvObject == nullptr)
            return E_POINTER;

        if (riid == __uuidof(IUnknown) || riid == __uuidof(IInspectable) || riid == __uuidof(Face))
            Found = static_cast<Face*>(this);
        else if (riid == __uuidof(IClosable))
            Found = static_cast<IClosable*>(this);
        else if (riid == __uuidof(IDirect3DDxgiInterfaceAccess))
            Found = static_cast<IDirect3DDxgiInterfaceAccess*>(this);
        else if (riid == __uuidof(IWeakReferenceSource))
            Found = static_cast<IWeakReferenceSource*>(this);
        else if (riid == __uuidof(IAgileObject))
            Found = static_cast<IAgileObject*>(this);
        else if (riid == __uuidof(IMarshal))
            Found = static_cast<IMarshal*>(this);

        *ppvObject = Found;
        if (Found == nullptr)
            return E_NOINTERFACE;

        Found->AddRef();
        return S_OK;
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return InterlockedIncrement(&Control->Strong);
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        ULONG Count = InterlockedDecrement(&Control->Strong);

        if (Count == 0)
            delete this;
        return Count;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetIids(
        _Out_ ULONG* iidCount,
        _Outptr_result_buffer_maybenull_(*iidCount) IID** iids) override
    {
        IID* List;

        *iidCount = 0;
        *iids = nullptr;

        List = static_cast<IID*>(CoTaskMemAlloc(3 * sizeof(*List)));
        if (List == nullptr)
            return E_OUTOFMEMORY;

        List[0] = __uuidof(Face);
        List[1] = __uuidof(IWeakReferenceSource);
        List[2] = __uuidof(IClosable);
        *iidCount = 3;
        *iids = List;
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetTrustLevel(
        _Out_ TrustLevel* trustLevel) override
    {
        *trustLevel = BaseTrust;
        return S_OK;
    }

    /* IClosable */

    HRESULT
    STDMETHODCALLTYPE
    Close(VOID) override
    {
        Wrapped* Old = static_cast<Wrapped*>(InterlockedExchangePointer(reinterpret_cast<PVOID*>(&Dxgi), nullptr));

        if (Old != nullptr)
            Old->Release();
        return S_OK;
    }

    /* IDirect3DDxgiInterfaceAccess */

    HRESULT
    STDMETHODCALLTYPE
    GetInterface(
        _In_ REFIID iid,
        _COM_Outptr_ void** p) override
    {
        if (Dxgi == nullptr)
            return RxClosedResult;

        return Dxgi->QueryInterface(iid, p);
    }

    /* IWeakReferenceSource */

    HRESULT
    STDMETHODCALLTYPE
    GetWeakReference(
        _Outptr_ IWeakReference** weakReference) override
    {
        if (weakReference == nullptr)
            return E_POINTER;

        Control->AddRef();
        *weakReference = Control;
        return S_OK;
    }

    /* IMarshal, through the free threaded marshaler as an agile object does */

    HRESULT
    STDMETHODCALLTYPE
    GetUnmarshalClass(
        _In_ REFIID riid,
        _In_opt_ void* pv,
        _In_ DWORD dwDestContext,
        _Reserved_ void* pvDestContext,
        _In_ DWORD mshlflags,
        _Out_ CLSID* pCid) override
    {
        IMarshal* Marshal;
        HRESULT hr;

        hr = GetMarshaler(&Marshal);
        if (FAILED(hr))
            return hr;

        hr = Marshal->GetUnmarshalClass(riid, pv, dwDestContext, pvDestContext, mshlflags, pCid);
        Marshal->Release();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetMarshalSizeMax(
        _In_ REFIID riid,
        _In_opt_ void* pv,
        _In_ DWORD dwDestContext,
        _Reserved_ void* pvDestContext,
        _In_ DWORD mshlflags,
        _Out_ DWORD* pSize) override
    {
        IMarshal* Marshal;
        HRESULT hr;

        hr = GetMarshaler(&Marshal);
        if (FAILED(hr))
            return hr;

        hr = Marshal->GetMarshalSizeMax(riid, pv, dwDestContext, pvDestContext, mshlflags, pSize);
        Marshal->Release();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    MarshalInterface(
        _In_ IStream* pStm,
        _In_ REFIID riid,
        _In_opt_ void* pv,
        _In_ DWORD dwDestContext,
        _Reserved_ void* pvDestContext,
        _In_ DWORD mshlflags) override
    {
        IMarshal* Marshal;
        HRESULT hr;

        hr = GetMarshaler(&Marshal);
        if (FAILED(hr))
            return hr;

        hr = Marshal->MarshalInterface(pStm, riid, pv, dwDestContext, pvDestContext, mshlflags);
        Marshal->Release();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    UnmarshalInterface(
        _In_ IStream* pStm,
        _In_ REFIID riid,
        _Outptr_ void** ppv) override
    {
        IMarshal* Marshal;
        HRESULT hr;

        hr = GetMarshaler(&Marshal);
        if (FAILED(hr))
            return hr;

        hr = Marshal->UnmarshalInterface(pStm, riid, ppv);
        Marshal->Release();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    ReleaseMarshalData(
        _In_ IStream* pStm) override
    {
        IMarshal* Marshal;
        HRESULT hr;

        hr = GetMarshaler(&Marshal);
        if (FAILED(hr))
            return hr;

        hr = Marshal->ReleaseMarshalData(pStm);
        Marshal->Release();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    DisconnectObject(
        _In_ DWORD dwReserved) override
    {
        IMarshal* Marshal;
        HRESULT hr;

        hr = GetMarshaler(&Marshal);
        if (FAILED(hr))
            return hr;

        hr = Marshal->DisconnectObject(dwReserved);
        Marshal->Release();
        return hr;
    }

protected:
    virtual
    ~RxDirect3DObject()
    {
        if (Dxgi != nullptr)
            Dxgi->Release();
        if (FreeMarshaler != nullptr)
            FreeMarshaler->Release();

        /* Weak references may still resolve against the block; they now find nothing */
        Control->Release();
    }

    Wrapped* Dxgi;

private:
    /* The free threaded marshaler is made on first use */
    HRESULT
    GetMarshaler(
        _Outptr_ IMarshal** Marshal)
    {
        IUnknown* Created;
        HRESULT hr;

        if (FreeMarshaler == nullptr)
        {
            hr = CoCreateFreeThreadedMarshaler(static_cast<Face*>(this), &Created);
            if (FAILED(hr))
                return hr;

            if (InterlockedCompareExchangePointer(reinterpret_cast<PVOID*>(&FreeMarshaler), Created, nullptr) != nullptr)
                Created->Release();
        }

        return FreeMarshaler->QueryInterface(IID_PPV_ARGS(Marshal));
    }

    RxWeakBlock* Control = nullptr;
    IUnknown* FreeMarshaler = nullptr;
};

class RxDirect3DDevice final : public RxDirect3DObject<IDirect3DDevice, IDXGIDevice3>
{
public:
    explicit RxDirect3DDevice(
        _In_ IDXGIDevice3* Device)
        : RxDirect3DObject(Device)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    GetRuntimeClassName(
        _Out_ HSTRING* className) override
    {
        static const WCHAR Name[] = L"Windows.Graphics.DirectX.Direct3D11.IDirect3DDevice";

        *className = nullptr;
        return WindowsCreateString(Name, _countof(Name) - 1, className);
    }

    HRESULT
    STDMETHODCALLTYPE
    Trim(VOID) override
    {
        if (Dxgi == nullptr)
            return RxClosedResult;

        Dxgi->Trim();
        return S_OK;
    }
};

class RxDirect3DSurface final : public RxDirect3DObject<IDirect3DSurface, IDXGISurface>
{
public:
    explicit RxDirect3DSurface(
        _In_ IDXGISurface* Surface)
        : RxDirect3DObject(Surface)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    GetRuntimeClassName(
        _Out_ HSTRING* className) override
    {
        static const WCHAR Name[] = L"Windows.Graphics.DirectX.Direct3D11.IDirect3DSurface";

        *className = nullptr;
        return WindowsCreateString(Name, _countof(Name) - 1, className);
    }

    HRESULT
    STDMETHODCALLTYPE
    get_Description(
        _Out_ Direct3DSurfaceDescription* value) override
    {
        static_assert(sizeof(Direct3DSurfaceDescription) == sizeof(DXGI_SURFACE_DESC));

        if (value == nullptr)
            return E_INVALIDARG;
        if (Dxgi == nullptr)
            return RxClosedResult;

        return Dxgi->GetDesc(reinterpret_cast<DXGI_SURFACE_DESC*>(value));
    }
};

/* Hands the new object out as IInspectable, dropping the creation reference either way */
template <typename Object>
static
HRESULT
RxPublishObject(
    _In_opt_ Object* Created,
    _COM_Outptr_ IInspectable** Result)
{
    HRESULT hr;

    if (Created == nullptr)
        return E_OUTOFMEMORY;

    if (!Created->Initialize())
    {
        delete Created;
        return E_OUTOFMEMORY;
    }

    hr = Created->QueryInterface(__uuidof(IInspectable), reinterpret_cast<void**>(Result));
    Created->Release();
    return hr;
}

extern "C"
HRESULT
WINAPI
RxCreateDirect3DDevice(
    _In_ IDXGIDevice* dxgiDevice,
    _COM_Outptr_ IInspectable** graphicsDevice)
{
    IDXGIDevice3* Device3 = nullptr;
    HRESULT hr;

    if (dxgiDevice == nullptr)
        return E_INVALIDARG;

    hr = dxgiDevice->QueryInterface(IID_PPV_ARGS(&Device3));
    if (FAILED(hr))
        return hr;

    hr = RxPublishObject(new (std::nothrow) RxDirect3DDevice(Device3), graphicsDevice);
    Device3->Release();
    return hr;
}

extern "C"
HRESULT
WINAPI
RxCreateDirect3DSurface(
    _In_ IDXGISurface* dgxiSurface,
    _COM_Outptr_ IInspectable** graphicsSurface)
{
    if (dgxiSurface == nullptr)
        return E_INVALIDARG;

    return RxPublishObject(new (std::nothrow) RxDirect3DSurface(dgxiSurface), graphicsSurface);
}
