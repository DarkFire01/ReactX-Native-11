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

/* Bytes between rows of a surface kept in runtime memory, rounded up to whole DWORDs */
UINT64
D9MemoryPitch(
    _In_ D3DFORMAT Format,
    _In_ UINT Width)
{
    LONG Stride = D9PixelStride(Format);
    UINT64 Columns = Width;

    if (Stride < 0)
    {
        Stride = -Stride;
        Columns = (static_cast<UINT64>(Width) + 3) / 4;
    }
    else if (Format == D3DFMT_A1)
    {
        Stride = 1;
        Columns = (static_cast<UINT64>(Width) + 7) / 8;
    }
    return (Columns * static_cast<UINT64>(Stride) + 3) & ~3ULL;
}

/* Rows of a surface kept in runtime memory; block compressed formats store four pixel rows in one */
UINT
D9MemoryRows(
    _In_ D3DFORMAT Format,
    _In_ UINT Height)
{
    if (D9BytesPerPixel(Format) < 0)
        return (Height + 3) / 4;
    return Height;
}

static
BOOLEAN
D9IsBlockFormat(
    _In_ D3DFORMAT Format)
{
    return D9BytesPerPixel(Format) < 0;
}

/* YUV formats that share chroma across two pixels of a row */
static
BOOLEAN
D9IsPackedYuv(
    _In_ D3DFORMAT Format)
{
    switch (static_cast<DWORD>(Format))
    {
    case D3DFMT_YUY2:
    case D3DFMT_UYVY:
    case MAKEFOURCC('P', '2', '1', '6'):
    case MAKEFOURCC('P', '2', '1', '0'):
    case MAKEFOURCC('Y', '2', '1', '6'):
    case MAKEFOURCC('Y', '2', '1', '0'):
        return TRUE;
    default:
        return FALSE;
    }
}

/* FOURCC formats without subsampling, which a rectangle may start anywhere in */
static
BOOLEAN
D9IsFullYuv(
    _In_ D3DFORMAT Format)
{
    switch (static_cast<DWORD>(Format))
    {
    case MAKEFOURCC('A', 'Y', 'U', 'V'):
    case MAKEFOURCC('A', 'I', '4', '4'):
    case MAKEFOURCC('I', 'A', '4', '4'):
    case MAKEFOURCC('Y', '4', '1', '6'):
    case MAKEFOURCC('Y', '4', '1', '0'):
        return TRUE;
    default:
        return FALSE;
    }
}

/* An edge off the block grid is only accepted where the surface ends */
static
BOOLEAN
D9OnGrid(
    _In_ LONG Edge,
    _In_ UINT Grid,
    _In_ UINT Limit)
{
    return (static_cast<UINT>(Edge) % Grid) == 0 || static_cast<UINT>(Edge) == Limit;
}

/* A lock rectangle inside the surface that respects the block layout of its format */
BOOLEAN
D9IsValidRect(
    _In_ D3DFORMAT Format,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ const RECT* Rect)
{
    UINT Right = Width != 0 ? Width : 1;
    UINT Bottom = Height != 0 ? Height : 1;

    if (Rect->left >= Rect->right || Rect->top >= Rect->bottom || Rect->left < 0 || Rect->top < 0 ||
        static_cast<UINT>(Rect->right) > Right || static_cast<UINT>(Rect->bottom) > Bottom)
    {
        return FALSE;
    }

    if (Format == D3DFMT_A1)
        return (Rect->left % 8) == 0 && ((Rect->right - Rect->left) % 8) == 0;
    if (D9IsBlockFormat(Format))
    {
        return (Rect->left % 4) == 0 && (Rect->top % 4) == 0 && D9OnGrid(Rect->right, 4, Right) &&
               D9OnGrid(Rect->bottom, 4, Bottom);
    }
    if ((static_cast<DWORD>(Format) & 0xFF00) == 0 || D9IsFullYuv(Format))
        return TRUE;
    if (D9IsPackedYuv(Format))
        return (Rect->left % 2) == 0 && D9OnGrid(Rect->right, 2, Right);
    return (Rect->left % 2) == 0 && (Rect->top % 2) == 0 && D9OnGrid(Rect->right, 2, Right) &&
           D9OnGrid(Rect->bottom, 2, Bottom);
}

D9Surface::D9Surface(
    _In_ D9Device* Owner,
    _In_opt_ IUnknown* Container,
    _In_ const D3DSURFACE_DESC* Desc,
    _In_ BOOLEAN Lockable,
    _In_ BOOLEAN Discard,
    _In_ BOOLEAN Standalone)
    : D9Child(Owner),
      Container(Container),
      Desc(*Desc),
      Lockable(Lockable),
      Discard(Discard),
      Standalone(Standalone)
{
}

D9Surface::D9Surface(
    _In_ D9TextureCore* Texture,
    _In_ UINT Subresource,
    _In_ const D3DSURFACE_DESC* Desc)
    : D9Child(Texture->Device9()),
      Container(Texture->Identity()),
      Desc(*Desc),
      Lockable(FALSE),
      Discard(FALSE),
      Standalone(FALSE),
      Texture(Texture),
      Subresource(Subresource)
{
}

/* A lock may discard along with any other flag but read only */
BOOLEAN
D9LockFlagsAllowed(
    _In_ DWORD Flags)
{
    static constexpr DWORD AllowedFlags = D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK | D3DLOCK_DONOTWAIT |
                                          D3DLOCK_NO_DIRTY_UPDATE;

    return (Flags & ~AllowedFlags) == 0 || ((Flags & D3DLOCK_DISCARD) != 0 && (Flags & D3DLOCK_READONLY) == 0);
}

VOID
D9Surface::Abandon(VOID)
{
    if (RefCount == 0)
    {
        delete this;
        return;
    }
    Standalone = TRUE;
}

VOID
D9Surface::CountAsDefault(VOID)
{
    CountedDefault = TRUE;
    Device->NoteDefaultResource(TRUE);
}

D9Surface::~D9Surface()
{
    if (CountedDefault)
        Device->NoteDefaultResource(FALSE);
    if (Resource != nullptr)
    {
        Device->DestroyDriverSurface(Resource);
        delete Resource;
    }
    if (Memory != nullptr)
        _aligned_free(Memory);
}

/* Back buffers and offscreen surfaces stay in local video memory; other targets and depth buffers go anywhere */
HRESULT
D9Surface::Create(VOID)
{
    D3DDDI_POOL DriverPool = D3DDDIPOOL_LOCALVIDMEM;
    UINT64 Size = 0;

    if (Desc.Pool == D3DPOOL_SYSTEMMEM || Desc.Pool == D3DPOOL_SCRATCH)
    {
        MemoryPitch = static_cast<UINT>(D9MemoryPitch(Desc.Format, Desc.Width));
        Size = static_cast<UINT64>(MemoryPitch) * D9MemoryRows(Desc.Format, Desc.Height);
        if (Size == 0)
            return E_OUTOFMEMORY;
        Memory = static_cast<BYTE*>(_aligned_malloc(static_cast<SIZE_T>(Size), 32));
        if (Memory == nullptr)
            return E_OUTOFMEMORY;
        ZeroMemory(Memory, static_cast<SIZE_T>(Size));
        if (Desc.Pool == D3DPOOL_SCRATCH)
            return S_OK;
        DriverPool = D3DDDIPOOL_SYSTEMMEM;
    }
    else if ((Desc.Usage & D3DUSAGE_DEPTHSTENCIL) != 0 ||
             (Container == nullptr && (Desc.Usage & D3DUSAGE_RENDERTARGET) != 0))
    {
        DriverPool = D3DDDIPOOL_VIDEOMEMORY;
    }

    if (Resource == nullptr)
        Resource = new (std::nothrow) D9Resource();
    if (Resource == nullptr)
        return E_OUTOFMEMORY;

    /* The driver hears the bytes per pixel row, which block rows spread over four */
    return Device->CreateDriverSurface(&Desc, Lockable, Discard, DriverPool, Memory,
                                       static_cast<UINT>(Size / Desc.Height), Resource);
}

/* Flip swap chains move the driver surfaces between their buffers; each keeps its runtime handle */
VOID
D9Surface::ExchangeResource(
    _Inout_ D9Resource** Held)
{
    D9Resource* Previous = Resource;

    Resource = *Held;
    *Held = Previous;
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
    if (Texture != nullptr)
        return Texture->AddRefTexture();
    return AddRefChild();
}

STDMETHODIMP_(ULONG)
D9Surface::Release()
{
    D9Device* Owner = Device;
    LONG Count;

    if (Texture != nullptr)
        return Texture->ReleaseTexture();
    if (!Standalone)
        return ReleaseChild();

    /* The device outlives the surface it destroys */
    Count = InterlockedDecrement(&RefCount);
    if (Count == 0)
    {
        delete this;
        Owner->Release();
    }
    return static_cast<ULONG>(Count);
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

/* Texture levels take no priority of their own */
STDMETHODIMP_(DWORD)
D9Surface::SetPriority(
    _In_ DWORD Priority)
{
    DWORD Previous = ResourcePriority;

    if (Texture != nullptr)
        return 0;
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

/* Memory surfaces lock without checking the rectangle; the driver only hears of system memory locks */
HRESULT
D9Surface::LockMemory(
    _Out_ D3DLOCKED_RECT* Locked,
    _In_opt_ const RECT* Rect,
    _In_ DWORD Flags)
{
    LONG Stride = D9PixelStride(Desc.Format);
    UINT Offset = 0;
    HRESULT hr;

    if (IsLocked)
        return D3DERR_INVALIDCALL;
    if (Resource != nullptr)
    {
        hr = Device->LockDriverSurface(Resource, nullptr, Flags, Locked);
        if (FAILED(hr))
            return hr;
    }

    /* The offset wraps at 32 bits, so a rectangle left of the surface lands four gigabytes on */
    if (Rect != nullptr)
    {
        if (Stride < 0)
            Offset = static_cast<UINT>(Rect->top / 4) * MemoryPitch + static_cast<UINT>(Rect->left / 4 * -Stride);
        else if (Desc.Format == D3DFMT_A1)
            Offset = static_cast<UINT>(Rect->top) * MemoryPitch + static_cast<UINT>(Rect->left >> 3);
        else
            Offset = static_cast<UINT>(Rect->top) * MemoryPitch + static_cast<UINT>(Rect->left * Stride);
    }
    Locked->pBits = Memory + Offset;
    Locked->Pitch = static_cast<INT>(MemoryPitch);
    IsLocked = TRUE;
    return S_OK;
}

STDMETHODIMP
D9Surface::LockRect(
    _Out_ D3DLOCKED_RECT* Locked,
    _In_opt_ const RECT* Rect,
    _In_ DWORD Flags)
{
    D3DLOCKED_BOX Box = {};
    D3DBOX Area = {};
    HRESULT hr;

    if (Locked == nullptr)
        return D3DERR_INVALIDCALL;

    ZeroMemory(Locked, sizeof(*Locked));
    if (Texture != nullptr)
    {
        if (Rect != nullptr)
        {
            Area.Left = static_cast<UINT>(Rect->left);
            Area.Top = static_cast<UINT>(Rect->top);
            Area.Right = static_cast<UINT>(Rect->right);
            Area.Bottom = static_cast<UINT>(Rect->bottom);
            Area.Back = 1;
        }
        hr = Texture->LockSubresource(Subresource, Rect != nullptr ? &Area : nullptr, Flags, TRUE, &Box);
        Locked->pBits = Box.pBits;
        Locked->Pitch = Box.RowPitch;
        return hr;
    }
    if (Memory != nullptr)
        return LockMemory(Locked, Rect, Flags);

    if (Rect != nullptr && !D9IsValidRect(Desc.Format, Desc.Width, Desc.Height, Rect))
    {
        D9_TRACE_ONCE(L"LockRect: the rectangle does not fit the surface");
        return D3DERR_INVALIDCALL;
    }

    if (!D9LockFlagsAllowed(Flags))
    {
        D9_TRACE_ONCE(L"LockRect: invalid flags 0x%08lX", Flags);
        return D3DERR_INVALIDCALL;
    }
    if (!Lockable || IsLocked)
    {
        D9_TRACE_ONCE(L"LockRect: the surface cannot be locked now");
        return D3DERR_INVALIDCALL;
    }

    hr = Device->LockDriverSurface(Resource, Rect, Flags, Locked);
    if (FAILED(hr))
        return hr;

    /* Block compressed surfaces report the pitch of their block rows */
    if (D9IsBlockFormat(Desc.Format))
        Locked->Pitch = static_cast<INT>(((Desc.Width + 3) / 4) * -D9BytesPerPixel(Desc.Format));
    IsLocked = TRUE;
    return S_OK;
}

STDMETHODIMP
D9Surface::UnlockRect()
{
    HRESULT hr = S_OK;

    if (Texture != nullptr)
        return Texture->UnlockSubresource(Subresource, TRUE);
    if (!IsLocked)
        return D3DERR_INVALIDCALL;

    if (Resource != nullptr)
        hr = Device->UnlockDriverSurface(Resource);
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
