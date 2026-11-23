/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Surfaces a device owns and the reference counting every device child shares
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "kmt.hpp"

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

D9PrivateData::~D9PrivateData()
{
    while (First != nullptr)
    {
        Entry* Item = First;

        First = Item->Next;
        Empty(Item);
        delete Item;
    }
}

/* Lets go of what an entry holds; the entry keeps its GUID */
VOID
D9PrivateData::Empty(
    _Inout_ Entry* Item)
{
    if ((Item->Flags & D3DSPD_IUNKNOWN) != 0)
        static_cast<IUnknown*>(Item->Data)->Release();
    else if (Item->Data != nullptr)
        _aligned_free(Item->Data);
    Item->Data = nullptr;
    Item->Size = 0;
    Item->Flags &= ~D3DSPD_IUNKNOWN;
}

D9PrivateData::Entry*
D9PrivateData::Find(
    _In_ REFGUID Guid) const
{
    for (Entry* Item = First; Item != nullptr; Item = Item->Next)
    {
        if (IsEqualGUID(Item->Guid, Guid))
            return Item;
    }
    return nullptr;
}

/* An interface is held by reference and must be passed with the size of a pointer; anything else is copied */
HRESULT
D9PrivateData::Set(
    _In_ REFGUID Guid,
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ DWORD Size,
    _In_ DWORD Flags)
{
    Entry* Item;
    BOOLEAN Added = FALSE;

    if ((Flags & ~D3DSPD_IUNKNOWN) != 0)
        return D3DERR_INVALIDCALL;
    if ((Flags & D3DSPD_IUNKNOWN) != 0 && Size != sizeof(IUnknown*))
        return D3DERR_INVALIDCALL;

    Item = Find(Guid);
    if (Item != nullptr)
    {
        Empty(Item);
    }
    else
    {
        Item = new (std::nothrow) Entry();
        if (Item == nullptr)
            return E_OUTOFMEMORY;
        Item->Guid = Guid;
        Added = TRUE;
    }
    Item->Size = Size;
    Item->Flags = Flags;

    if ((Flags & D3DSPD_IUNKNOWN) != 0)
    {
        Item->Data = const_cast<VOID*>(Data);
        static_cast<IUnknown*>(Item->Data)->AddRef();
    }
    else
    {
        /* An empty block cannot be allocated, as the system finds */
        Item->Data = Size != 0 ? _aligned_malloc(Size, 16) : nullptr;
        if (Item->Data == nullptr)
        {
            if (Added)
                delete Item;
            return E_OUTOFMEMORY;
        }
        CopyMemory(Item->Data, Data, Size);
    }

    if (Added)
    {
        Item->Next = First;
        First = Item;
    }
    return S_OK;
}

HRESULT
D9PrivateData::Get(
    _In_ REFGUID Guid,
    _Out_writes_bytes_opt_(*Size) VOID* Data,
    _Inout_ DWORD* Size) const
{
    const Entry* Item = Find(Guid);
    DWORD Room;

    if (Item == nullptr)
        return D3DERR_NOTFOUND;
    if (Size == nullptr)
        return D3DERR_INVALIDCALL;

    Room = *Size;
    *Size = Item->Size;
    if (Data == nullptr)
        return S_OK;
    if (Room < Item->Size)
        return D3DERR_MOREDATA;

    if ((Item->Flags & D3DSPD_IUNKNOWN) != 0)
    {
        *static_cast<IUnknown**>(Data) = static_cast<IUnknown*>(Item->Data);
        static_cast<IUnknown*>(Item->Data)->AddRef();
    }
    else if (Item->Size != 0)
    {
        CopyMemory(Data, Item->Data, Item->Size);
    }
    return S_OK;
}

HRESULT
D9PrivateData::Free(
    _In_ REFGUID Guid)
{
    for (Entry** Link = &First; *Link != nullptr; Link = &(*Link)->Next)
    {
        Entry* Item = *Link;

        if (!IsEqualGUID(Item->Guid, Guid))
            continue;
        *Link = Item->Next;
        Empty(Item);
        delete Item;
        return S_OK;
    }
    return D3DERR_NOTFOUND;
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

BOOLEAN
D9Surface::CanLock() const
{
    if (Texture != nullptr)
        return Texture->IsLockable();
    return Desc.Pool != D3DPOOL_DEFAULT || Lockable;
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
                                       static_cast<UINT>(Size / Desc.Height), &Sharing, Resource);
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

    /* The device outlives the surface it destroys; a pinned surface waits for the runtime to let go */
    Count = InterlockedDecrement(&RefCount);
    if (Count == 0)
    {
        if (PinCount > 0)
        {
            Orphaned = TRUE;
            Owner->Release();
            return 0;
        }
        delete this;
        Owner->Release();
    }
    return static_cast<ULONG>(Count);
}

VOID
D9Surface::Pin(VOID)
{
    if (Texture != nullptr)
    {
        Texture->Bind();
        return;
    }
    PinCount += 1;
}

VOID
D9Surface::Unpin(VOID)
{
    if (Texture != nullptr)
    {
        Texture->Unbind();
        return;
    }
    PinCount -= 1;
    if (PinCount == 0 && Orphaned)
        delete this;
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
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Private.Set(Guid, Data, Size, Flags);
}

STDMETHODIMP
D9Surface::GetPrivateData(
    _In_ REFGUID Guid,
    _Out_writes_bytes_opt_(*Size) VOID* Data,
    _Inout_ DWORD* Size)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Private.Get(Guid, Data, Size);
}

STDMETHODIMP
D9Surface::FreePrivateData(
    _In_ REFGUID Guid)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Private.Free(Guid);
}

/* Texture levels take no priority of their own */
STDMETHODIMP_(DWORD)
D9Surface::SetPriority(
    _In_ DWORD Priority)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    if (Texture != nullptr)
        return 0;
    return Device->ChangePriority(Resource, Desc.Pool, &ResourcePriority, Priority);
}

STDMETHODIMP_(DWORD)
D9Surface::GetPriority()
{
    if (Texture != nullptr)
        return 0;
    return Device->ReadPriority(Desc.Pool, ResourcePriority);
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

D9Resource*
D9Surface::CopyResource(
    _Out_ UINT* Index) const
{
    if (Texture != nullptr)
    {
        *Index = Subresource;
        return Texture->DriverResource();
    }
    *Index = 0;
    return Resource;
}

BOOLEAN
D9Surface::IsBusy() const
{
    if (Texture != nullptr)
        return Texture->IsSubresourceLocked(Subresource);
    return IsLocked;
}

/* The formats GDI draws into */
static
BOOLEAN
D9IsGdiFormat(
    _In_ D3DFORMAT Format)
{
    return Format == D3DFMT_R8G8B8 || Format == D3DFMT_A8R8G8B8 || Format == D3DFMT_X8R8G8B8 ||
           Format == D3DFMT_R5G6B5 || Format == D3DFMT_X1R5G5B5 || Format == D3DFMT_A1R5G5B5;
}

/* The surface stays locked while GDI draws into its memory */
STDMETHODIMP
D9Surface::GetDC(
    _Out_ HDC* Dc)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CREATEDCFROMMEMORY Create = {};
    D3DLOCKED_RECT Locked = {};
    D3DLOCKED_BOX Box = {};
    PCSTR Display = Device->DisplayName();
    BOOLEAN Plain = Container == nullptr && (Desc.Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) == 0;
    HDC DeviceDc;
    NTSTATUS Status;
    HRESULT hr;

    if (Dc == nullptr || Kmt == nullptr || Kmt->CreateDCFromMemory == nullptr)
        return D3DERR_INVALIDCALL;
    if (Texture != nullptr ? Texture->IsAnySubresourceLocked() : IsLocked)
    {
        D9_TRACE_ONCE(L"GetDC: the surface is locked");
        return D3DERR_INVALIDCALL;
    }
    if ((Desc.Usage & D3DUSAGE_DEPTHSTENCIL) != 0 || Desc.Pool == D3DPOOL_SCRATCH)
        return D3DERR_INVALIDCALL;
    if (Desc.Pool == D3DPOOL_DEFAULT && !((Desc.Usage & D3DUSAGE_RENDERTARGET) != 0 && Lockable) &&
        (Desc.Usage & D3DUSAGE_DYNAMIC) == 0 && !(Texture == nullptr && Plain))
    {
        D9_TRACE_ONCE(L"GetDC: default pool surfaces must be lockable targets, dynamic or offscreen plains");
        return D3DERR_INVALIDCALL;
    }
    if (!D9IsGdiFormat(Desc.Format))
    {
        D9_TRACE_ONCE(L"GetDC: format %u is not one GDI draws into", Desc.Format);
        return D3DERR_INVALIDCALL;
    }

    if (Texture != nullptr)
    {
        hr = Texture->LockSubresource(Subresource, nullptr, 0, TRUE, &Box);
        Locked.pBits = Box.pBits;
        Locked.Pitch = Box.RowPitch;
    }
    else
    {
        hr = LockRect(&Locked, nullptr, 0);
    }
    if (FAILED(hr))
        return hr;

    /* A display named the way CreateDC wants a device rather than a driver */
    if (Display[0] == '\\' && Display[1] == '\\' && Display[2] == '.')
        DeviceDc = CreateDCA(nullptr, Display, nullptr, nullptr);
    else
        DeviceDc = CreateDCA(Display, nullptr, nullptr, nullptr);
    Create.pMemory = Locked.pBits;
    Create.Format = static_cast<D3DDDIFORMAT>(Desc.Format);
    Create.Width = Desc.Width;
    Create.Height = Desc.Height;
    Create.Pitch = static_cast<UINT>(Locked.Pitch);
    Create.hDeviceDc = DeviceDc;
    Status = Kmt->CreateDCFromMemory(&Create);
    if (DeviceDc != NULL)
        DeleteDC(DeviceDc);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"GetDC: the kernel made no device context (0x%08lX)", Status);
        if (Texture != nullptr)
            Texture->UnlockSubresource(Subresource, TRUE);
        else
            UnlockRect();
        return E_FAIL;
    }

    HeldDc = Create.hDc;
    HeldBitmap = Create.hBitmap;
    if (Texture != nullptr)
        Texture->NoteDc(TRUE);
    *Dc = HeldDc;
    return S_OK;
}

STDMETHODIMP
D9Surface::ReleaseDC(
    _In_ HDC Dc)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_DESTROYDCFROMMEMORY Destroy = {};

    if (HeldDc == NULL || Dc != HeldDc)
    {
        D9_TRACE_ONCE(L"ReleaseDC: the device context did not come from this surface");
        return D3DERR_INVALIDCALL;
    }

    Destroy.hDc = HeldDc;
    Destroy.hBitmap = HeldBitmap;
    HeldDc = NULL;
    HeldBitmap = NULL;
    if (Kmt != nullptr && Kmt->DestroyDCFromMemory != nullptr)
        Kmt->DestroyDCFromMemory(&Destroy);
    if (Texture == nullptr)
        return UnlockRect();
    Texture->NoteDc(FALSE);
    return Texture->UnlockSubresource(Subresource, TRUE);
}
