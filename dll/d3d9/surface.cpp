/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Surfaces a device owns and the reference counting every device child shares
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

ULONG
D9Child::AddRefChild(VOID)
{
    LONG Count = InterlockedIncrement(&RefCount);

    if (Count == 1)
        Device->AddRef();
    return static_cast<ULONG>(Count);
}

ULONG
D9Child::ReleaseChild(VOID)
{
    LONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        Device->Release();
    return static_cast<ULONG>(Count);
}

D9Surface::D9Surface(
    _In_ D9Device* Owner,
    _In_opt_ IUnknown* Container,
    _In_ const D3DSURFACE_DESC* Desc,
    _In_ BOOLEAN Lockable)
    : D9Child(Owner),
      Container(Container),
      Desc(*Desc),
      Lockable(Lockable)
{
}

D9Surface::~D9Surface()
{
    Device->DestroyDriverSurface(&Resource);
}

HRESULT
D9Surface::Create(VOID)
{
    return Device->CreateDriverSurface(&Desc, Lockable, &Resource);
}

STDMETHODIMP
D9Surface::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Object)
{
    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    if (IsEqualGUID(Iid, __uuidof(IUnknown)) || IsEqualGUID(Iid, __uuidof(IDirect3DResource9)) ||
        IsEqualGUID(Iid, __uuidof(IDirect3DSurface9)))
    {
        *Object = static_cast<IDirect3DSurface9*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
D9Surface::AddRef()
{
    return AddRefChild();
}

STDMETHODIMP_(ULONG)
D9Surface::Release()
{
    return ReleaseChild();
}

STDMETHODIMP
D9Surface::GetDevice(
    _Out_ IDirect3DDevice9** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Device;
    Device->AddRef();
    return S_OK;
}

STDMETHODIMP
D9Surface::SetPrivateData(
    _In_ REFGUID Guid,
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ DWORD Size,
    _In_ DWORD Flags)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Surface::GetPrivateData(
    _In_ REFGUID Guid,
    _Out_writes_bytes_opt_(*Size) VOID* Data,
    _Inout_ DWORD* Size)
{
    RX_UNIMPLEMENTED();
    return D3DERR_NOTFOUND;
}

STDMETHODIMP
D9Surface::FreePrivateData(
    _In_ REFGUID Guid)
{
    RX_UNIMPLEMENTED();
    return D3DERR_NOTFOUND;
}

STDMETHODIMP_(DWORD)
D9Surface::SetPriority(
    _In_ DWORD Priority)
{
    DWORD Previous = ResourcePriority;

    ResourcePriority = Priority;
    return Previous;
}

STDMETHODIMP_(DWORD)
D9Surface::GetPriority()
{
    return ResourcePriority;
}

STDMETHODIMP_(VOID)
D9Surface::PreLoad()
{
}

STDMETHODIMP_(D3DRESOURCETYPE)
D9Surface::GetType()
{
    return D3DRTYPE_SURFACE;
}

/* Back buffers belong to their swap chain; surfaces made on their own belong to the device */
STDMETHODIMP
D9Surface::GetContainer(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = nullptr;
    if (Container != nullptr)
        return Container->QueryInterface(Iid, Result);
    return Device->QueryInterface(Iid, Result);
}

STDMETHODIMP
D9Surface::GetDesc(
    _Out_ D3DSURFACE_DESC* Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Desc;
    return S_OK;
}

STDMETHODIMP
D9Surface::LockRect(
    _Out_ D3DLOCKED_RECT* Locked,
    _In_opt_ const RECT* Rect,
    _In_ DWORD Flags)
{
    HRESULT hr;

    if (Locked == nullptr)
        return D3DERR_INVALIDCALL;

    ZeroMemory(Locked, sizeof(*Locked));
    if (!Lockable || IsLocked)
    {
        D9_TRACE_ONCE(L"LockRect: the surface cannot be locked now");
        return D3DERR_INVALIDCALL;
    }
    if (Rect != nullptr && (Rect->left < 0 || Rect->top < 0 || Rect->left >= Rect->right ||
                            Rect->top >= Rect->bottom || static_cast<UINT>(Rect->right) > Desc.Width ||
                            static_cast<UINT>(Rect->bottom) > Desc.Height))
    {
        return D3DERR_INVALIDCALL;
    }

    hr = Device->LockDriverSurface(&Resource, Rect, Flags, Locked);
    if (SUCCEEDED(hr))
        IsLocked = TRUE;
    return hr;
}

STDMETHODIMP
D9Surface::UnlockRect()
{
    HRESULT hr;

    if (!IsLocked)
        return D3DERR_INVALIDCALL;

    hr = Device->UnlockDriverSurface(&Resource);
    IsLocked = FALSE;
    return hr;
}

STDMETHODIMP
D9Surface::GetDC(
    _Out_ HDC* Dc)
{
    RX_UNIMPLEMENTED();
    if (Dc != nullptr)
        *Dc = NULL;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Surface::ReleaseDC(
    _In_ HDC Dc)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}
