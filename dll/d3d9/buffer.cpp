/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Vertex and index buffers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Driver flags for a buffer: what it holds, how often it changes and what draws from it */
static
UINT
D9BufferFlags(
    _In_ BOOLEAN IsVertex,
    _In_ DWORD Usage)
{
    D3DDDI_RESOURCEFLAGS Flags = {};

    Flags.VertexBuffer = IsVertex;
    Flags.IndexBuffer = !IsVertex;
    Flags.Dynamic = (Usage & D3DUSAGE_DYNAMIC) != 0;
    Flags.HintStatic = (Usage & D3DUSAGE_DYNAMIC) == 0;
    Flags.WriteOnly = (Usage & D3DUSAGE_WRITEONLY) != 0;
    Flags.Points = (Usage & D3DUSAGE_POINTS) != 0;
    Flags.RtPatches = (Usage & D3DUSAGE_RTPATCHES) != 0;
    Flags.NPatches = (Usage & D3DUSAGE_NPATCHES) != 0;
    return Flags.Value;
}

template <class Interface, class Description>
D9Buffer<Interface, Description>::D9Buffer(
    _In_ D9Device* Owner,
    _In_ UINT Length,
    _In_ DWORD Usage,
    _In_ D3DFORMAT Format,
    _In_ DWORD Fvf,
    _In_ D3DPOOL Pool)
    : D9Child(Owner),
      Length(Length),
      Usage(Usage),
      Format(Format),
      Fvf(Fvf),
      Pool(Pool)
{
    if (Pool == D3DPOOL_DEFAULT)
        Owner->NoteDefaultResource(TRUE);
}

template <class Interface, class Description>
D9Buffer<Interface, Description>::~D9Buffer()
{
    if (Pool == D3DPOOL_DEFAULT)
        Device->NoteDefaultResource(FALSE);
    if (IsManaged())
        Device->ForgetManaged(this);
    if (Resource != nullptr)
    {
        Device->DestroyDriverSurface(Resource);
        delete Resource;
    }
    if (VideoResource != nullptr)
    {
        Device->DestroyDriverSurface(VideoResource);
        delete VideoResource;
    }
    if (Memory != nullptr)
        _aligned_free(Memory);
}

/* System memory buffers keep their bytes in memory of the runtime's own that the driver reads */
template <class Interface, class Description>
HRESULT
D9Buffer<Interface, Description>::Create(
    _In_ D3DDDI_POOL Placement,
    _In_opt_ const D9ShareRequest* Share)
{
    UINT Bytes = Length;
    HRESULT hr;

    /* The runtime's own memory holds whole dwords, and the driver hears of all of it */
    if (Placement == D3DDDIPOOL_SYSTEMMEM)
    {
        Bytes = (Length + 3) & ~3u;
        Memory = static_cast<BYTE*>(_aligned_malloc(Bytes, 32));
        if (Memory == nullptr)
            return E_OUTOFMEMORY;
        ZeroMemory(Memory, Bytes);
    }

    Resource = new (std::nothrow) D9Resource();
    if (Resource == nullptr)
        return E_OUTOFMEMORY;
    hr = Device->CreateBufferResource(Format, Bytes, D9BufferFlags(IsVertex(), Usage), Fvf, Placement, Memory,
                                      Share, Resource);
    if (SUCCEEDED(hr) && IsManaged())
    {
        DirtyEnd = Length;
        Device->TrackManaged(this);
    }
    return hr;
}

template <class Interface, class Description>
STDMETHODIMP
D9Buffer<Interface, Description>::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Object)
{
    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    if (IsEqualGUID(Iid, __uuidof(IUnknown)) || IsEqualGUID(Iid, __uuidof(IDirect3DResource9)) ||
        IsEqualGUID(Iid, __uuidof(Interface)))
    {
        *Object = static_cast<Interface*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

template <class Interface, class Description>
STDMETHODIMP_(ULONG)
D9Buffer<Interface, Description>::AddRef()
{
    ULONG Count = AddRefChild();

    if (Count == 1)
        Orphaned = FALSE;
    return Count;
}

/* The device outlives the buffer it destroys; a bound buffer waits for its binding to let go */
template <class Interface, class Description>
STDMETHODIMP_(ULONG)
D9Buffer<Interface, Description>::Release()
{
    D9Device* Owner = Device;
    LONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
    {
        if (BindCount > 0)
            Orphaned = TRUE;
        else
            delete this;
        Owner->Release();
    }
    return static_cast<ULONG>(Count);
}

template <class Interface, class Description>
VOID
D9Buffer<Interface, Description>::Unbind(VOID)
{
    BindCount -= 1;
    if (BindCount == 0 && Orphaned)
        delete this;
}

template <class Interface, class Description>
STDMETHODIMP
D9Buffer<Interface, Description>::GetDevice(
    _Out_ IDirect3DDevice9** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Device;
    Device->AddRef();
    return S_OK;
}

template <class Interface, class Description>
STDMETHODIMP
D9Buffer<Interface, Description>::SetPrivateData(
    _In_ REFGUID Guid,
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ DWORD Size,
    _In_ DWORD Flags)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Private.Set(Guid, Data, Size, Flags);
}

template <class Interface, class Description>
STDMETHODIMP
D9Buffer<Interface, Description>::GetPrivateData(
    _In_ REFGUID Guid,
    _Out_writes_bytes_opt_(*Size) VOID* Data,
    _Inout_ DWORD* Size)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Private.Get(Guid, Data, Size);
}

template <class Interface, class Description>
STDMETHODIMP
D9Buffer<Interface, Description>::FreePrivateData(
    _In_ REFGUID Guid)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Private.Free(Guid);
}

template <class Interface, class Description>
STDMETHODIMP_(DWORD)
D9Buffer<Interface, Description>::SetPriority(
    _In_ DWORD Priority)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Device->ChangePriority(Resource, Pool, &ResourcePriority, Priority);
}

template <class Interface, class Description>
STDMETHODIMP_(DWORD)
D9Buffer<Interface, Description>::GetPriority()
{
    return Device->ReadPriority(Pool, ResourcePriority);
}

template <class Interface, class Description>
STDMETHODIMP_(VOID)
D9Buffer<Interface, Description>::PreLoad()
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    Upload();
}

/* Makes the video memory copy of a managed buffer, which is write only, then copies the range that changed */
template <class Interface, class Description>
VOID
D9Buffer<Interface, Description>::Upload(VOID)
{
    UINT Flags;
    HRESULT hr;

    if (!IsManaged())
        return;

    if (VideoResource == nullptr)
    {
        VideoResource = new (std::nothrow) D9Resource();
        if (VideoResource == nullptr)
            return;
        Flags = Resource->Flags.Value | D9BufferFlags(IsVertex(), D3DUSAGE_WRITEONLY);
        hr = Device->CreateBufferResource(Format, Length, Flags, Fvf, D3DDDIPOOL_VIDEOMEMORY, nullptr, nullptr,
                                          VideoResource);
        if (FAILED(hr))
        {
            D9Trace(L"the video copy of a managed buffer could not be made (0x%08lX)", hr);
            delete VideoResource;
            VideoResource = nullptr;
            return;
        }
        DirtyStart = 0;
        DirtyEnd = Length;
    }
    if (DirtyEnd > DirtyStart)
        Device->DriverBufBlt(VideoResource, Resource, DirtyStart, DirtyEnd - DirtyStart);
    DirtyStart = 0;
    DirtyEnd = 0;
}

template <class Interface, class Description>
D9Resource*
D9Buffer<Interface, Description>::DrawResource(
    _Out_ BOOLEAN* Made)
{
    BOOLEAN Missing = VideoResource == nullptr;

    *Made = FALSE;
    if (!IsManaged())
        return Resource;
    Upload();
    *Made = Missing && VideoResource != nullptr;
    return VideoResource;
}

template <class Interface, class Description>
VOID
D9Buffer<Interface, Description>::Evict(VOID)
{
    if (VideoResource == nullptr)
        return;

    Device->DestroyDriverSurface(VideoResource);
    delete VideoResource;
    VideoResource = nullptr;
    DirtyStart = 0;
    DirtyEnd = Length;
}

template <class Interface, class Description>
STDMETHODIMP_(D3DRESOURCETYPE)
D9Buffer<Interface, Description>::GetType()
{
    return IsVertex() ? D3DRTYPE_VERTEXBUFFER : D3DRTYPE_INDEXBUFFER;
}

/* Vertex buffers hand the driver the range they lock; index buffers always lock whole */
template <class Interface, class Description>
STDMETHODIMP
D9Buffer<Interface, Description>::Lock(
    _In_ UINT OffsetToLock,
    _In_ UINT SizeToLock,
    _Outptr_ VOID** Data,
    _In_ DWORD Flags)
{
    BOOLEAN Ranged = IsVertex() && SizeToLock != 0;
    BOOLEAN Asynchronous = LockCount > 0 ? LockedAsync : Resource->Flags.Dynamic || Memory != nullptr;
    VOID* Base = nullptr;
    HRESULT hr;

    if (IsVertex() && Memory == nullptr)
        *Data = nullptr;

    LockCount += 1;
    hr = Device->LockDriverBuffer(Resource, OffsetToLock, SizeToLock, Ranged, Flags,
                                  static_cast<UINT>(LockCount - 1), &Asynchronous, &Base);
    if (FAILED(hr))
    {
        LockCount -= 1;
        return hr;
    }
    LockedAsync = Asynchronous;

    /* A writing lock of a managed buffer widens the range its next upload copies */
    if (IsManaged() && (Flags & (D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE)) == 0)
    {
        UINT End = SizeToLock != 0 && OffsetToLock + SizeToLock < Length ? OffsetToLock + SizeToLock : Length;

        if (DirtyEnd <= DirtyStart)
        {
            DirtyStart = OffsetToLock;
            DirtyEnd = End;
        }
        else
        {
            DirtyStart = OffsetToLock < DirtyStart ? OffsetToLock : DirtyStart;
            DirtyEnd = End > DirtyEnd ? End : DirtyEnd;
        }
    }

    /* The driver already moved a ranged vertex buffer lock to its offset */
    if (Memory != nullptr)
        *Data = Memory + OffsetToLock;
    else if (IsVertex())
        *Data = Base;
    else
        *Data = static_cast<BYTE*>(Base) + OffsetToLock;
    return S_OK;
}

template <class Interface, class Description>
STDMETHODIMP
D9Buffer<Interface, Description>::Unlock()
{
    HRESULT hr = Device->UnlockDriverBuffer(Resource, LockedAsync);

    if (FAILED(hr))
        return hr;
    LockCount -= 1;
    if (LockCount <= 0)
        LockedAsync = FALSE;
    return S_OK;
}

template <>
STDMETHODIMP
D9VertexBuffer::GetDesc(
    _Out_ D3DVERTEXBUFFER_DESC* Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    Result->Format = Format;
    Result->Type = D3DRTYPE_VERTEXBUFFER;
    Result->Usage = Usage;
    Result->Pool = Pool;
    Result->Size = Length;
    Result->FVF = Fvf;
    return S_OK;
}

template <>
STDMETHODIMP
D9IndexBuffer::GetDesc(
    _Out_ D3DINDEXBUFFER_DESC* Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    Result->Format = Format;
    Result->Type = D3DRTYPE_INDEXBUFFER;
    Result->Usage = Usage;
    Result->Pool = Pool;
    Result->Size = Length;
    return S_OK;
}

template class D9Buffer<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC>;
template class D9Buffer<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC>;
