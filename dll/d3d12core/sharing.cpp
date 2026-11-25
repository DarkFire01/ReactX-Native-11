/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NT handles for shared device children, and opening what other devices shared
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "sharedrecord12.hpp"

#include <winternl.h>
#include <stdio.h>

/* What a name that is already taken comes back as */
constexpr HRESULT RxHandleInUse = E_HANDLE;

/* The only access shared handles are made or opened with */
constexpr DWORD RxShareAccess = GENERIC_ALL;

/* The longest name a counted kernel string holds */
constexpr SIZE_T RxLongestObjectPath = 0xFFFE;

/* NT handles to bundles of objects; only protected sessions share those */
static const WCHAR RxBundleTypeName[] = L"DxgkSharedBundleObject";

/* D3D11 bind and misc flags the record of a D3D11 resource can carry */
constexpr UINT RxBindDepthAndShader = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

typedef NTSTATUS (NTAPI* RxQueryObjectRoutine)(HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG);

/* Object attributes for a shared handle, with a name placed in this session's named object directory */
class RxShareAttributes12
{
public:
    RxShareAttributes12() = default;

    ~RxShareAttributes12()
    {
        delete[] Path;
    }

    RxShareAttributes12(const RxShareAttributes12&) = delete;
    RxShareAttributes12& operator=(const RxShareAttributes12&) = delete;

    HRESULT
    Initialize(
        _In_opt_ const SECURITY_ATTRIBUTES* Security,
        _In_opt_ LPCWSTR Name)
    {
        DWORD Session = 0;
        SIZE_T Length;

        InitializeObjectAttributes(&Attributes, nullptr, 0, nullptr,
                                   Security != nullptr ? Security->lpSecurityDescriptor : nullptr);
        if (Security != nullptr && Security->bInheritHandle)
            Attributes.Attributes |= OBJ_INHERIT;
        if (Name == nullptr)
            return S_OK;

        /* Named objects of session 0 live in the root directory, the rest in their session's */
        ProcessIdToSessionId(GetCurrentProcessId(), &Session);
        Length = wcslen(Name) + 64;
        if (Length * sizeof(WCHAR) > RxLongestObjectPath)
            return E_INVALIDARG;

        Path = new (std::nothrow) WCHAR[Length];
        if (Path == nullptr)
            return E_OUTOFMEMORY;
        if (Session == 0)
            swprintf_s(Path, Length, L"\\BaseNamedObjects\\%ls", Name);
        else
            swprintf_s(Path, Length, L"\\Sessions\\%lu\\BaseNamedObjects\\%ls", Session, Name);

        String.Buffer = Path;
        String.Length = (USHORT)(wcslen(Path) * sizeof(WCHAR));
        String.MaximumLength = String.Length + sizeof(WCHAR);
        Attributes.ObjectName = &String;
        return S_OK;
    }

    OBJECT_ATTRIBUTES*
    Get()
    {
        return &Attributes;
    }

private:
    WCHAR* Path = nullptr;
    UNICODE_STRING String = {};
    OBJECT_ATTRIBUTES Attributes = {};
};

/* Zeroed scratch memory freed with its owner */
class RxShareBuffer12
{
public:
    RxShareBuffer12() = default;

    ~RxShareBuffer12()
    {
        free(Data);
    }

    RxShareBuffer12(const RxShareBuffer12&) = delete;
    RxShareBuffer12& operator=(const RxShareBuffer12&) = delete;

    _Ret_maybenull_
    BYTE*
    Allocate(
        _In_ SIZE_T Size)
    {
        Data = static_cast<BYTE*>(calloc(Size != 0 ? Size : 1, 1));
        return Data;
    }

    BYTE*
    Get() const
    {
        return Data;
    }

private:
    BYTE* Data = nullptr;
};

/* Kernel objects shared in one NT handle: the resource first, then a keyed mutex and its fence */
static
HRESULT
RxShareKernelObjects(
    _In_ RxDevice12& Device,
    _In_ UINT Count,
    _In_reads_(Count) const D3DKMT_HANDLE* Objects,
    _In_opt_ const SECURITY_ATTRIBUTES* pAttributes,
    _In_opt_ LPCWSTR Name,
    _Out_ HANDLE* pHandle)
{
    RxShareAttributes12 Attributes;
    HRESULT hr;

    hr = Attributes.Initialize(pAttributes, Name);
    if (FAILED(hr))
        return hr;

    hr = RxApiResult12(Device, RxGetKmtThunks()->ShareObjects(Count, Objects, Attributes.Get(), RxShareAccess,
                                                               pHandle),
                       "D3DKMTShareObjects");
    return hr == RxHandleInUse ? DXGI_ERROR_NAME_ALREADY_EXISTS : hr;
}

/* A heap's kernel resource, with the keyed mutex an opened D3D11 resource brought along */
static
HRESULT
RxShareRuntimeResource(
    _In_ RxDevice12& Device,
    _In_ const RxRuntimeResource12& Runtime,
    _In_opt_ const SECURITY_ATTRIBUTES* pAttributes,
    _In_ DWORD Access,
    _In_opt_ LPCWSTR Name,
    _Out_ HANDLE* pHandle)
{
    D3DKMT_HANDLE Objects[3] = { Runtime.KmResource, Runtime.KeyedMutex, Runtime.KeyedMutexSync };

    if (Runtime.KmResource == 0 || Access != RxShareAccess)
        return E_INVALIDARG;

    return RxShareKernelObjects(Device, Runtime.KeyedMutex != 0 ? 3 : 1, Objects, pAttributes, Name, pHandle);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateSharedHandle(
    _In_ ID3D12DeviceChild* pObject,
    _In_opt_ const SECURITY_ATTRIBUTES* pAttributes,
    DWORD Access,
    _In_opt_ LPCWSTR Name,
    _Out_ HANDLE* pHandle)
{
    RxFence* Fence;
    RxHeap* Heap;
    RxResource* Resource;
    D3DKMT_HANDLE Object;

    /* Failures leave the output as the application had it */
    if (pObject == nullptr || pHandle == nullptr)
        return E_INVALIDARG;

    /* A committed resource is shared through the heap it owns */
    Resource = RxUnwrap12<RxResource>(pObject);
    if (Resource != nullptr)
    {
        if (!Resource->IsCommitted() || (Resource->GetHeapFlags() & D3D12_HEAP_FLAG_SHARED) == 0)
            return E_INVALIDARG;

        return RxShareRuntimeResource(*this, Resource->GetRuntime(), pAttributes, Access, Name, pHandle);
    }

    Heap = RxUnwrap12<RxHeap>(pObject);
    if (Heap != nullptr)
    {
        if ((Heap->GetApiDesc().Flags & D3D12_HEAP_FLAG_SHARED) == 0)
            return E_INVALIDARG;

        return RxShareRuntimeResource(*this, Heap->GetRuntime(), pAttributes, Access, Name, pHandle);
    }

    /* A fence shares its monitored fence */
    Fence = RxUnwrap12<RxFence>(pObject);
    if (Fence != nullptr)
    {
        if ((Fence->GetCreationFlags() & D3D12_FENCE_FLAG_SHARED) == 0 || Access != RxShareAccess)
            return E_INVALIDARG;

        Object = Fence->GetSyncObject();
        return RxShareKernelObjects(*this, 1, &Object, pAttributes, Name, pHandle);
    }

    return E_INVALIDARG;
}

/* Whether an NT handle names an object of this kernel type */
static
BOOLEAN
RxIsHandleOfType(
    _In_ HANDLE Handle,
    _In_z_ PCWSTR TypeName)
{
    static RxQueryObjectRoutine QueryObject =
        reinterpret_cast<RxQueryObjectRoutine>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject"));
    union
    {
        PUBLIC_OBJECT_TYPE_INFORMATION Info;
        BYTE Space[0x100];
    } Type;
    SIZE_T Length = wcslen(TypeName) * sizeof(WCHAR);

    if (QueryObject == nullptr ||
        !NT_SUCCESS(QueryObject(Handle, ObjectTypeInformation, &Type, sizeof(Type), nullptr)))
    {
        return FALSE;
    }

    return Type.Info.TypeName.Length == Length && _wcsnicmp(Type.Info.TypeName.Buffer, TypeName, Length / sizeof(WCHAR)) == 0;
}

/* Kernel handles from D3D11's GetSharedHandle have bit 1 set; NT handles never do */
static
BOOLEAN
RxIsNtShareHandle(
    _In_ HANDLE Handle)
{
    return ((ULONG_PTR)Handle & 2) == 0;
}

/* How big what the creator stored with a shared resource is */
struct RxShareSizes12
{
    UINT AllocationCount;
    UINT RuntimeDataSize;
    UINT ResourceDataSize;
    UINT AllocationDataSize;
};

static
NTSTATUS
RxQueryShare12(
    _In_ RxDevice12& Device,
    _In_ HANDLE Handle,
    _Out_ RxShareSizes12* Sizes)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    NTSTATUS Status;

    ZeroMemory(Sizes, sizeof(*Sizes));

    if (RxIsNtShareHandle(Handle))
    {
        D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE Query = {};

        if (Kmt->QueryResourceInfoFromNtHandle == nullptr)
            return STATUS_NOT_SUPPORTED;

        Query.hDevice = Device.KernelDevice();
        Query.hNtHandle = Handle;
        Status = Kmt->QueryResourceInfoFromNtHandle(&Query);
        Sizes->AllocationCount = Query.NumAllocations;
        Sizes->RuntimeDataSize = Query.PrivateRuntimeDataSize;
        Sizes->ResourceDataSize = Query.ResourcePrivateDriverDataSize;
        Sizes->AllocationDataSize = Query.TotalPrivateDriverDataSize;
        return Status;
    }

    D3DKMT_QUERYRESOURCEINFO Query = {};

    if (Kmt->QueryResourceInfo == nullptr)
        return STATUS_NOT_SUPPORTED;

    Query.hDevice = Device.KernelDevice();
    Query.hGlobalShare = (D3DKMT_HANDLE)(ULONG_PTR)Handle;
    Status = Kmt->QueryResourceInfo(&Query);
    Sizes->AllocationCount = Query.NumAllocations;
    Sizes->RuntimeDataSize = Query.PrivateRuntimeDataSize;
    Sizes->ResourceDataSize = Query.ResourcePrivateDriverDataSize;
    Sizes->AllocationDataSize = Query.TotalPrivateDriverDataSize;
    return Status;
}

/* Everything an opened kernel resource came with, kept until the driver took what it needs */
struct RxShareContents12
{
    RxShareBuffer12 Allocations;
    RxShareBuffer12 RuntimeData;
    RxShareBuffer12 ResourceData;
    RxShareBuffer12 AllocationData;
    RxShareBuffer12 DriverAllocations;
};

/* Opens a kernel resource shared through a global handle, with the keyed mutex its record names */
static
HRESULT
RxOpenGlobalShare12(
    _In_ RxDevice12& Device,
    _In_ HANDLE Handle,
    _In_ const RxShareSizes12& Sizes,
    _Inout_ RxShareContents12& Contents,
    _Inout_ RxOpenedShare12& Opened)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_OPENRESOURCE Open = {};
    NTSTATUS Status;

    if (Kmt->OpenResource == nullptr)
        return E_NOTIMPL;

    Open.hDevice = Device.KernelDevice();
    Open.hGlobalShare = (D3DKMT_HANDLE)(ULONG_PTR)Handle;
    Open.NumAllocations = Sizes.AllocationCount;
    Open.pOpenAllocationInfo2 = reinterpret_cast<D3DDDI_OPENALLOCATIONINFO2*>(Contents.Allocations.Get());
    Open.pPrivateRuntimeData = Contents.RuntimeData.Get();
    Open.PrivateRuntimeDataSize = Sizes.RuntimeDataSize;
    Open.pResourcePrivateDriverData = Contents.ResourceData.Get();
    Open.ResourcePrivateDriverDataSize = Sizes.ResourceDataSize;
    Open.pTotalPrivateDriverDataBuffer = Contents.AllocationData.Get();
    Open.TotalPrivateDriverDataBufferSize = Sizes.AllocationDataSize;
    Status = Kmt->OpenResource(&Open);
    if (!NT_SUCCESS(Status))
        return RxApiResult12(Device, Status, "D3DKMTOpenResource");

    Opened.KmResource = Open.hResource;
    return S_OK;
}

static
HRESULT
RxOpenNtShare12(
    _In_ RxDevice12& Device,
    _In_ HANDLE Handle,
    _In_ const RxShareSizes12& Sizes,
    _Inout_ RxShareContents12& Contents,
    _Inout_ RxOpenedShare12& Opened)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_OPENRESOURCEFROMNTHANDLE Open = {};
    UINT MutexData = 0;
    NTSTATUS Status;

    if (Kmt->OpenResourceFromNtHandle == nullptr)
        return E_NOTIMPL;

    Open.hDevice = Device.KernelDevice();
    Open.hNtHandle = Handle;
    Open.NumAllocations = Sizes.AllocationCount;
    Open.pOpenAllocationInfo2 = reinterpret_cast<D3DDDI_OPENALLOCATIONINFO2*>(Contents.Allocations.Get());
    Open.pPrivateRuntimeData = Contents.RuntimeData.Get();
    Open.PrivateRuntimeDataSize = Sizes.RuntimeDataSize;
    Open.pResourcePrivateDriverData = Contents.ResourceData.Get();
    Open.ResourcePrivateDriverDataSize = Sizes.ResourceDataSize;
    Open.pTotalPrivateDriverDataBuffer = Contents.AllocationData.Get();
    Open.TotalPrivateDriverDataBufferSize = Sizes.AllocationDataSize;
    Open.pKeyedMutexPrivateRuntimeData = &MutexData;
    Open.KeyedMutexPrivateRuntimeDataSize = sizeof(MutexData);
    Status = Kmt->OpenResourceFromNtHandle(&Open);

    /* A handle that names no resource is a bad argument, not a bad handle */
    if (Status == STATUS_INVALID_HANDLE || Status == STATUS_GRAPHICS_ALLOCATION_INVALID ||
        Status == STATUS_GRAPHICS_ALLOCATION_CLOSED || Status == STATUS_GRAPHICS_INVALID_ALLOCATION_INSTANCE ||
        Status == STATUS_GRAPHICS_INVALID_ALLOCATION_HANDLE)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    if (!NT_SUCCESS(Status))
        return RxApiResult12(Device, Status, "D3DKMTOpenResourceFromNtHandle");

    Opened.KmResource = Open.hResource;
    Opened.KeyedMutex = Open.hKeyedMutex;
    Opened.KeyedMutexSync = Open.hSyncObject;
    return S_OK;
}

/* A D3D11 resource shared by global handle names its keyed mutex and fence in its record */
static
HRESULT
RxOpenRecordMutex12(
    _In_ RxDevice12& Device,
    _In_ const RxSharedTextureRecord& Record,
    _Inout_ RxOpenedShare12& Opened)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_OPENKEYEDMUTEX2 Mutex = {};
    D3DKMT_OPENSYNCHRONIZATIONOBJECT Sync = {};
    UINT MutexData = 0;
    NTSTATUS Status;

    if (Opened.KeyedMutex != 0 || Record.Constants[RxSharedRecordKeyedMutexIndex] == 0)
        return S_OK;
    if (Kmt->OpenKeyedMutex2 == nullptr || Kmt->OpenSynchronizationObject == nullptr)
        return E_NOTIMPL;

    Mutex.hSharedHandle = Record.Constants[RxSharedRecordKeyedMutexShareIndex];
    Mutex.pPrivateRuntimeData = &MutexData;
    Mutex.PrivateRuntimeDataSize = sizeof(MutexData);
    Status = Kmt->OpenKeyedMutex2(&Mutex);
    if (!NT_SUCCESS(Status))
        return RxApiResult12(Device, Status, "D3DKMTOpenKeyedMutex2");
    Opened.KeyedMutex = Mutex.hKeyedMutex;

    Sync.hSharedHandle = Record.Constants[RxSharedRecordSyncObjectShareIndex];
    Status = Kmt->OpenSynchronizationObject(&Sync);
    if (!NT_SUCCESS(Status))
        return RxApiResult12(Device, Status, "D3DKMTOpenSynchronizationObject");
    Opened.KeyedMutexSync = Sync.hSyncObject;
    return S_OK;
}

/* Resource flags of a D3D11 resource's binds */
static
D3D12_RESOURCE_FLAGS
RxFlagsFromBinds(
    _In_ UINT BindFlags)
{
    UINT Flags = 0;

    if ((BindFlags & RxBindDepthAndShader) == D3D11_BIND_DEPTH_STENCIL)
        Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    if ((BindFlags & D3D11_BIND_RENDER_TARGET) != 0)
        Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if ((BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0)
        Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if ((BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0)
        Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return (D3D12_RESOURCE_FLAGS)Flags;
}

/* The heap type a D3D11 usage stands for */
static
D3D12_HEAP_TYPE
RxHeapTypeFromUsage(
    _In_ D3D11_USAGE Usage,
    _In_ UINT CpuAccess)
{
    if (Usage == D3D11_USAGE_DYNAMIC)
        return D3D12_HEAP_TYPE_UPLOAD;
    if (Usage == D3D11_USAGE_STAGING)
        return (CpuAccess & D3D11_CPU_ACCESS_READ) != 0 ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_UPLOAD;
    return D3D12_HEAP_TYPE_DEFAULT;
}

/* What a D3D11 runtime recorded, as the heap and resource descs Direct3D 12 opens it with */
static
HRESULT
RxDescribeFrom11(
    _In_ const RxSharedTextureRecord& Record,
    _Inout_ D3D12_HEAP_DESC& Heap,
    _Inout_ D3D12_RESOURCE_DESC1& Resource)
{
    D3D11_USAGE Usage;
    UINT CpuAccess;
    UINT Misc;

    switch (Record.Dimension)
    {
        case D3D11_RESOURCE_DIMENSION_BUFFER:
        {
            D3D11_BUFFER_DESC Desc;

            memcpy(&Desc, &Record.Desc, sizeof(Desc));
            Resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            Resource.Width = Desc.ByteWidth;
            Resource.Height = 1;
            Resource.DepthOrArraySize = 1;
            Resource.MipLevels = 1;
            Resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            Resource.Flags = RxFlagsFromBinds(Desc.BindFlags);
            if ((Desc.MiscFlags & D3D11_RESOURCE_MISC_TILE_POOL) != 0)
                Heap.SizeInBytes = Desc.ByteWidth;
            Usage = Desc.Usage;
            CpuAccess = Desc.CPUAccessFlags;
            Misc = Desc.MiscFlags;
            break;
        }

        case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        {
            D3D11_TEXTURE1D_DESC Desc;

            memcpy(&Desc, &Record.Desc, sizeof(Desc));
            Resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
            Resource.Width = Desc.Width;
            Resource.Height = 1;
            Resource.DepthOrArraySize = (UINT16)Desc.ArraySize;
            Resource.MipLevels = (UINT16)Desc.MipLevels;
            Resource.Format = Desc.Format;
            Resource.Flags = RxFlagsFromBinds(Desc.BindFlags);
            Usage = Desc.Usage;
            CpuAccess = Desc.CPUAccessFlags;
            Misc = Desc.MiscFlags;
            break;
        }

        case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        {
            const D3D11_TEXTURE2D_DESC& Desc = Record.Desc;

            Resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            Resource.Width = Desc.Width;
            Resource.Height = Desc.Height;
            Resource.DepthOrArraySize = (UINT16)Desc.ArraySize;
            Resource.MipLevels = (UINT16)Desc.MipLevels;
            Resource.Format = Desc.Format;
            Resource.SampleDesc = Desc.SampleDesc;
            Resource.Flags = RxFlagsFromBinds(Desc.BindFlags);
            if ((Desc.MiscFlags & D3D11_RESOURCE_MISC_TILED) != 0)
                Resource.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
            Usage = Desc.Usage;
            CpuAccess = Desc.CPUAccessFlags;
            Misc = Desc.MiscFlags;
            break;
        }

        case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
        {
            D3D11_TEXTURE3D_DESC Desc;

            memcpy(&Desc, &Record.Desc, sizeof(Desc));
            Resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            Resource.Width = Desc.Width;
            Resource.Height = Desc.Height;
            Resource.DepthOrArraySize = (UINT16)Desc.Depth;
            Resource.MipLevels = (UINT16)Desc.MipLevels;
            Resource.Format = Desc.Format;
            Resource.Flags = RxFlagsFromBinds(Desc.BindFlags);
            if ((Desc.MiscFlags & D3D11_RESOURCE_MISC_TILED) != 0)
                Resource.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
            Usage = Desc.Usage;
            CpuAccess = Desc.CPUAccessFlags;
            Misc = Desc.MiscFlags;
            break;
        }

        default:
            return E_INVALIDARG;
    }

    if (Resource.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        Resource.SampleDesc.Count = 1;

    /* Protected content needs a session this runtime cannot give it */
    if ((Misc & D3D11_RESOURCE_MISC_HW_PROTECTED) != 0)
        return E_INVALIDARG;

    /* D3D11 textures are used by both runtimes at once unless one writer owns them */
    if (Resource.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER && Resource.SampleDesc.Count == 1 &&
        Resource.SampleDesc.Quality == 0 && (Misc & D3D11_RESOURCE_MISC_SHARED_EXCLUSIVE_WRITER) == 0)
    {
        Resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    }

    Heap.Properties.Type = RxHeapTypeFromUsage(Usage, CpuAccess);
    if ((Misc & D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE) != 0)
        Heap.Flags |= D3D12_HEAP_FLAG_ALLOW_DISPLAY;
    return S_OK;
}

/* The heap and resource descs a shared kernel resource opens with, and whether it opens as a heap */
static
HRESULT
RxDescribeShare12(
    _In_ RxDevice12& Device,
    _In_ const RxSharedRecord12& Record,
    _Inout_ RxOpenedShare12& Opened,
    _Out_ BOOLEAN* OpenAsHeap)
{
    D3D12_HEAP_DESC& Heap = Opened.HeapDesc;
    D3D12_RESOURCE_DESC1& Resource = Opened.ResourceDesc;
    D3D12DDIARG_CREATERESOURCE_0111 DdiResource;
    D3D12_RESOURCE_ALLOCATION_INFO Info;
    HRESULT hr;

    *OpenAsHeap = FALSE;
    ZeroMemory(&Heap, sizeof(Heap));
    ZeroMemory(&Resource, sizeof(Resource));

    if (Record.Heap.Size != 0)
    {
        /* Direct3D 12 recorded both descs */
        Heap = Record.Heap.Desc;
        if (Record.Resource.Size != 0)
        {
            Resource = Record.Resource.Desc;
            *OpenAsHeap = (Record.Resource.OpenAs & RxShareOpenAsHeap12) != 0;
        }
    }
    else if (Record.Api11.Version == RxSharedRecordVersion)
    {
        hr = RxDescribeFrom11(Record.Api11, Heap, Resource);
        if (FAILED(hr))
            return hr;
    }
    else
    {
        /* Older runtimes only give a size and format: a single render target */
        Resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        Resource.Width = Record.Api11.Width;
        Resource.Height = Record.Api11.Height;
        Resource.DepthOrArraySize = (UINT16)Record.Api11.Constants[RxSharedRecordArraySizeIndex];
        Resource.MipLevels = 1;
        Resource.Format = Record.Api11.Format;
        Resource.SampleDesc.Count = 1;
        Resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Heap.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    }

    if (Resource.Dimension < D3D12_RESOURCE_DIMENSION_BUFFER || Resource.Dimension > D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        return E_INVALIDARG;

    Heap.Flags |= D3D12_HEAP_FLAG_SHARED;
    Heap.Properties.CreationNodeMask = 1;
    Heap.Properties.VisibleNodeMask = Device.GetAllNodesMask();

    /* What the record leaves out comes from what the driver says the resource needs */
    RxFillResourceDefaults(Resource);
    if (Heap.SizeInBytes == 0 || Heap.Alignment == 0)
    {
        RxToDdiResource(Resource, DdiResource);
        Info = RxAllocationInfo(Device, Resource, DdiResource, FALSE, Heap.Flags, nullptr);
        if (Resource.Alignment == 0)
            Resource.Alignment = Info.Alignment;
        if (Heap.SizeInBytes == 0)
            Heap.SizeInBytes = Info.SizeInBytes;
        if (Heap.Alignment == 0)
            Heap.Alignment = Info.Alignment;
    }
    if (Heap.Alignment == 0)
        Heap.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    return S_OK;
}

HRESULT
RxOpenSharedResource12(
    _In_ RxDevice12& Device,
    _In_ HANDLE Handle,
    _Outptr_ IUnknown** Object)
{
    RxShareContents12 Contents;
    RxShareSizes12 Sizes;
    RxOpenedShare12 Opened = {};
    RxSharedRecord12 Record;
    D3DDDI_OPENALLOCATIONINFO2* Wide;
    D3DDDI_OPENALLOCATIONINFO* Narrow;
    BOOLEAN OpenAsHeap = FALSE;
    NTSTATUS Status;
    HRESULT hr;
    UINT Index;

    *Object = nullptr;

    Status = RxQueryShare12(Device, Handle, &Sizes);
    if (!NT_SUCCESS(Status))
        return RxApiResult12(Device, Status, "D3DKMTQueryResourceInfo");

    if (Contents.Allocations.Allocate((SIZE_T)Sizes.AllocationCount * sizeof(D3DDDI_OPENALLOCATIONINFO2)) == nullptr ||
        Contents.DriverAllocations.Allocate((SIZE_T)Sizes.AllocationCount * sizeof(D3DDDI_OPENALLOCATIONINFO)) == nullptr ||
        Contents.RuntimeData.Allocate(Sizes.RuntimeDataSize) == nullptr ||
        Contents.ResourceData.Allocate(Sizes.ResourceDataSize) == nullptr ||
        Contents.AllocationData.Allocate(Sizes.AllocationDataSize) == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    if (RxIsNtShareHandle(Handle))
        hr = RxOpenNtShare12(Device, Handle, Sizes, Contents, Opened);
    else
        hr = RxOpenGlobalShare12(Device, Handle, Sizes, Contents, Opened);
    if (FAILED(hr))
        return hr;

    /* The driver takes the older allocation list, whose fields lead the newer one */
    Wide = reinterpret_cast<D3DDDI_OPENALLOCATIONINFO2*>(Contents.Allocations.Get());
    Narrow = reinterpret_cast<D3DDDI_OPENALLOCATIONINFO*>(Contents.DriverAllocations.Get());
    for (Index = 0; Index < Sizes.AllocationCount; Index++)
    {
        Narrow[Index].hAllocation = Wide[Index].hAllocation;
        Narrow[Index].pPrivateDriverData = Wide[Index].pPrivateDriverData;
        Narrow[Index].PrivateDriverDataSize = Wide[Index].PrivateDriverDataSize;
    }
    Opened.AllocationCount = Sizes.AllocationCount;
    Opened.Allocations = Narrow;
    Opened.ResourceData = Contents.ResourceData.Get();
    Opened.ResourceDataSize = Sizes.ResourceDataSize;

    RxReadSharedRecord12(Contents.RuntimeData.Get(), Sizes.RuntimeDataSize, &Record);
    hr = RxOpenRecordMutex12(Device, Record.Api11, Opened);
    if (SUCCEEDED(hr))
        hr = RxDescribeShare12(Device, Record, Opened, &OpenAsHeap);
    if (FAILED(hr))
    {
        RxRuntimeResource12 Unowned = {};

        Unowned.KmResource = Opened.KmResource;
        Unowned.IsOpened = TRUE;
        Unowned.KeyedMutex = Opened.KeyedMutex;
        Unowned.KeyedMutexSync = Opened.KeyedMutexSync;
        RxCloseOpenedShare12(Device, Unowned);
        return hr;
    }

    if (OpenAsHeap)
    {
        RxHeap* Heap;

        hr = RxHeap::Open(Device, Opened, &Heap);
        if (SUCCEEDED(hr))
            *Object = static_cast<ID3D12Heap1*>(Heap);
        return hr;
    }

    RxResource* Resource;

    hr = RxResource::Open(Device, Opened, &Resource);
    if (SUCCEEDED(hr))
        *Object = static_cast<ID3D12Resource2*>(Resource);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::OpenSharedHandle(
    _In_ HANDLE NTHandle,
    REFIID riid,
    _COM_Outptr_opt_ void** ppvObj)
{
    IUnknown* Object = nullptr;
    RxShareSizes12 Sizes;
    NTSTATUS Status;
    HRESULT hr;

    if (ppvObj == nullptr)
        return E_POINTER;
    *ppvObj = nullptr;

    hr = CheckUsable();
    if (FAILED(hr))
        return hr;

    /* Bundles carry protected sessions, which this runtime has no support for */
    if (RxIsNtShareHandle(NTHandle) && RxIsHandleOfType(NTHandle, RxBundleTypeName))
    {
        RX_TRACE(L"shared protected session bundles are not supported");
        return E_NOTIMPL;
    }

    /* Anything that is not a resource may still be a fence */
    Status = RxQueryShare12(*this, NTHandle, &Sizes);
    if (Status == STATUS_OBJECT_TYPE_MISMATCH && RxIsNtShareHandle(NTHandle))
    {
        RxFence* Fence;

        hr = RxFence::Open(*this, NTHandle, &Fence);
        if (FAILED(hr))
            return hr;
        Object = static_cast<ID3D12Fence1*>(Fence);
    }
    else if (!NT_SUCCESS(Status))
    {
        return RxApiResult12(*this, Status, "D3DKMTQueryResourceInfo");
    }
    else
    {
        hr = RxOpenSharedResource12(*this, NTHandle, &Object);
        if (FAILED(hr))
            return hr;
    }

    hr = Object->QueryInterface(riid, ppvObj);
    Object->Release();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::OpenSharedHandleByName(
    _In_ LPCWSTR Name,
    DWORD Access,
    _Out_ HANDLE* pNTHandle)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_OPENNTHANDLEFROMNAME Resource = {};
    D3DKMT_OPENSYNCOBJECTNTHANDLEFROMNAME Sync = {};
    RxShareAttributes12 Attributes;
    NTSTATUS Status;
    HRESULT hr;

    if (pNTHandle == nullptr || Access != RxShareAccess)
        return E_INVALIDARG;

    hr = Attributes.Initialize(nullptr, Name);
    if (FAILED(hr))
        return hr;

    /* The name may belong to a heap or resource, or else to a fence */
    Status = STATUS_NOT_SUPPORTED;
    if (Kmt->OpenNtHandleFromName != nullptr)
    {
        Resource.dwDesiredAccess = Access;
        Resource.pObjAttrib = Attributes.Get();
        Status = Kmt->OpenNtHandleFromName(&Resource);
        if (NT_SUCCESS(Status))
        {
            *pNTHandle = Resource.hNtHandle;
            return S_OK;
        }
    }

    if (Kmt->OpenSyncObjectNtHandleFromName != nullptr)
    {
        Sync.dwDesiredAccess = Access;
        Sync.pObjAttrib = Attributes.Get();
        Status = Kmt->OpenSyncObjectNtHandleFromName(&Sync);
        if (NT_SUCCESS(Status))
        {
            *pNTHandle = Sync.hNtHandle;
            return S_OK;
        }
    }

    return RxApiResult12(*this, Status, "D3DKMTOpenSyncObjectNtHandleFromName");
}
