/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Scheduling groups the driver shares between direct and compute queues
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

/* Context flags a driver may ask for; anything at or above this is reserved */
constexpr UINT RxReservedGroupContextFlags = 0x80;

static
UINT
RxNodeIndexOf(
    _In_ UINT NodeMask)
{
    UINT Index = 0;

    if (NodeMask == 0)
        return 0;

    while ((NodeMask & 1) == 0)
    {
        NodeMask >>= 1;
        Index += 1;
    }

    return Index;
}

RxSchedulingGroup::RxSchedulingGroup(
    _In_ RxDevice12& Owner,
    _In_ UINT NodeMask,
    _In_ const GUID& Creator)
    : Device(Owner),
      Mask(NodeMask),
      NodeIndex(RxNodeIndexOf(NodeMask)),
      CreatorId(Creator)
{
    Owner.AddRef();
}

RxSchedulingGroup::~RxSchedulingGroup()
{
    D3DKMT_DESTROYCONTEXT DestroyContext;
    UINT Index;

    if (IsDriverGroupCreated)
        Device.Ddi().pfnDestroySchedulingGroup(Device.Handle(), DriverGroup);

    for (Index = 0; Index < ContextCount; Index++)
    {
        DestroyContext.hContext = Contexts[Index];
        RxGetKmtThunks()->DestroyContext(&DestroyContext);
    }

    Device.Release();
}

HRESULT
RxSchedulingGroup::Create(
    _In_ RxDevice12& Device,
    _In_ UINT NodeMask,
    _In_ const GUID& Creator,
    _Outptr_ RxSchedulingGroup** Group)
{
    RxSchedulingGroup* Created;
    HRESULT hr;

    *Group = nullptr;

    Created = new (std::nothrow) RxSchedulingGroup(Device, NodeMask, Creator);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize();
    if (FAILED(hr))
    {
        Created->ReleaseUse();
        return hr;
    }

    *Group = Created;
    return S_OK;
}

HRESULT
RxSchedulingGroup::Initialize()
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATESCHEDULINGGROUP_0050 Args = {};
    D3D12DDI_HRTSCHEDULINGGROUP_0050 Runtime;
    HRESULT hr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Args.NodeMask = Mask;
    Memory.Allocate(Ddi.pfnCalcPrivateSchedulingGroupSize(Device.Handle(), &Args));
    DriverGroup.pDrvPrivate = Memory.Get();
    if (DriverGroup.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    /* The driver makes the shared contexts through callbacks while this runs */
    Runtime.handle = this;
    CreatingThread = GetCurrentThreadId();
    hr = Ddi.pfnCreateSchedulingGroup(Device.Handle(), &Args, DriverGroup, Runtime);
    CreatingThread = 0;
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateSchedulingGroup failed 0x%08lX", hr);
        return hr;
    }

    IsDriverGroupCreated = TRUE;
    return S_OK;
}

BOOLEAN
RxSchedulingGroup::HasRoomFor(
    _In_ D3D12_COMMAND_LIST_TYPE Type) const
{
    if (Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        return DirectQueues == 0;

    return ComputeQueues != Device.GetComputeQueuesPerGroup();
}

BOOLEAN
RxSchedulingGroup::Attach(
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    BOOLEAN WasEmpty = (DirectQueues == 0 && ComputeQueues == 0);

    if (Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        DirectQueues += 1;
    else
        ComputeQueues += 1;

    return WasEmpty;
}

BOOLEAN
RxSchedulingGroup::Detach(
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    if (Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        DirectQueues -= 1;
    else
        ComputeQueues -= 1;

    return DirectQueues == 0 && ComputeQueues == 0;
}

BOOLEAN
RxSchedulingGroup::OwnsContext(
    _In_ D3DKMT_HANDLE Context) const
{
    UINT Index;

    for (Index = 0; Index < ContextCount; Index++)
    {
        if (Contexts[Index] == Context)
            return TRUE;
    }

    return FALSE;
}

/* Group contexts are made only while the group is being made, and must take hardware queues */
BOOLEAN
RxSchedulingGroup::CanMakeContext(
    _In_ D3DDDI_CREATECONTEXTFLAGS Flags) const
{
    if (Flags.Value >= RxReservedGroupContextFlags || CreatingThread != GetCurrentThreadId())
        return FALSE;

    return Flags.HwQueueSupported && ContextCount < RxMaxQueueEngines12;
}

HRESULT
APIENTRY
RxSchedulingGroup::CreateContextCb(
    _In_ D3D12DDI_HRTSCHEDULINGGROUP_0050 hRTSchedulingGroup,
    _Inout_ D3DDDICB_CREATECONTEXT* Args)
{
    RxSchedulingGroup* Self = static_cast<RxSchedulingGroup*>(hRTSchedulingGroup.handle);
    D3DKMT_CREATECONTEXT Create = {};
    NTSTATUS Status;

    if (!Self->CanMakeContext(Args->Flags))
        return E_INVALIDARG;

    Args->hContext = NULL;
    Args->pCommandBuffer = nullptr;
    Args->CommandBufferSize = 0;
    Args->pAllocationList = nullptr;
    Args->AllocationListSize = 0;
    Args->pPatchLocationList = nullptr;
    Args->PatchLocationListSize = 0;
    Args->CommandBuffer = 0;

    Create.hDevice = Self->Device.KernelDevice();
    Create.NodeOrdinal = Args->NodeOrdinal;
    Create.EngineAffinity = Args->EngineAffinity;
    Create.Flags.HwQueueSupported = TRUE;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.ClientHint = Self->Device.KernelClientHint();

    Status = RxGetKmtThunks()->CreateContext(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateContext for a scheduling group failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    Self->Contexts[Self->ContextCount] = Create.hContext;
    Self->ContextCount += 1;
    Args->hContext = (HANDLE)(ULONG_PTR)Create.hContext;
    Args->pCommandBuffer = Create.pCommandBuffer;
    Args->CommandBufferSize = Create.CommandBufferSize;
    Args->pAllocationList = Create.pAllocationList;
    Args->AllocationListSize = Create.AllocationListSize;
    Args->pPatchLocationList = Create.pPatchLocationList;
    Args->PatchLocationListSize = Create.PatchLocationListSize;
    Args->CommandBuffer = Create.CommandBuffer;
    return S_OK;
}

HRESULT
APIENTRY
RxSchedulingGroup::CreateContextVirtualCb(
    _In_ D3D12DDI_HRTSCHEDULINGGROUP_0050 hRTSchedulingGroup,
    _Inout_ D3DDDICB_CREATECONTEXTVIRTUAL* Args)
{
    RxSchedulingGroup* Self = static_cast<RxSchedulingGroup*>(hRTSchedulingGroup.handle);
    D3DKMT_CREATECONTEXTVIRTUAL Create = {};
    NTSTATUS Status;

    if (!Self->CanMakeContext(Args->Flags) || RxGetKmtThunks()->CreateContextVirtual == nullptr)
        return E_INVALIDARG;

    Args->hContext = NULL;

    Create.hDevice = Self->Device.KernelDevice();
    Create.NodeOrdinal = Args->NodeOrdinal;
    Create.EngineAffinity = Args->EngineAffinity;
    Create.Flags.HwQueueSupported = TRUE;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.ClientHint = Self->Device.KernelClientHint();

    Status = RxGetKmtThunks()->CreateContextVirtual(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateContextVirtual for a scheduling group failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    Self->Contexts[Self->ContextCount] = Create.hContext;
    Self->ContextCount += 1;
    Args->hContext = (HANDLE)(ULONG_PTR)Create.hContext;
    return S_OK;
}

/* Device side bookkeeping of the groups still open to new queues */

HRESULT
RxDevice12::JoinSchedulingGroup(
    _In_ D3D12_COMMAND_LIST_TYPE Type,
    _In_ UINT NodeMask,
    _In_ const GUID& Creator,
    _Outptr_result_maybenull_ RxSchedulingGroup** Group)
{
    RxSchedulingGroup* Found = nullptr;
    RxSchedulingGroup** Tail;
    UINT Node = RxNodeIndexOf(NodeMask);
    HRESULT hr = S_OK;

    *Group = nullptr;
    if (ComputeQueuesPerGroup == 0 ||
        (Type != D3D12_COMMAND_LIST_TYPE_DIRECT && Type != D3D12_COMMAND_LIST_TYPE_COMPUTE))
    {
        return S_OK;
    }

    AcquireSRWLockExclusive(&GroupLock);

    for (Tail = &OpenGroups; *Tail != nullptr; Tail = &(*Tail)->Next)
    {
        if (Found == nullptr && (*Tail)->IsFor(Node, Creator) && (*Tail)->HasRoomFor(Type))
            Found = *Tail;
    }

    if (Found != nullptr)
    {
        Found->AddUse();
    }
    else
    {
        hr = RxSchedulingGroup::Create(*this, NodeMask, Creator, &Found);
        if (FAILED(hr))
            Found = nullptr;
    }

    /* A group with no queues is off the list until one joins */
    if (Found != nullptr && Found->Attach(Type))
    {
        Found->Next = nullptr;
        *Tail = Found;
    }

    ReleaseSRWLockExclusive(&GroupLock);

    *Group = Found;
    return hr;
}

VOID
RxDevice12::LeaveSchedulingGroup(
    _In_ RxSchedulingGroup* Group,
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    RxSchedulingGroup** Link;

    AcquireSRWLockExclusive(&GroupLock);

    if (Group->Detach(Type))
    {
        for (Link = &OpenGroups; *Link != nullptr; Link = &(*Link)->Next)
        {
            if (*Link == Group)
            {
                *Link = Group->Next;
                Group->Next = nullptr;
                break;
            }
        }
    }

    ReleaseSRWLockExclusive(&GroupLock);
    Group->ReleaseUse();
}
