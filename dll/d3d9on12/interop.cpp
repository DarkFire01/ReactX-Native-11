/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The private entries behind IDirect3DDevice9On12: the Direct3D 12 device, resources and their waits
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

using TL::COMMAND_LIST_TYPE;

/* How the runtime's transition requests are made: never bound, exact, and free when nothing changes */
static constexpr TL::SubresourceTransitionFlags NineRuntimeTransition =
    TL::SubresourceTransitionFlags::NoBindingTransitions | TL::SubresourceTransitionFlags::StateMatchExact |
    TL::SubresourceTransitionFlags::NotUsedInCommandListIfNoStateChange;

/* A handle of 1 is how the runtime names no resource */
static
NineResource*
NineInteropResource(
    _In_opt_ HANDLE Handle)
{
    if (Handle == reinterpret_cast<HANDLE>(1))
        return nullptr;
    return static_cast<NineResource*>(Handle);
}

/* Submits what is recorded so far, then hands the sync token to the runtime */
static
VOID
NineSyncToken(
    _In_ NineDevice* Device,
    _In_ const D3DDDIARG_SYNCTOKEN* Args,
    _In_ PFND3DDDI_SYNCTOKENCB Callback)
{
    D3DDDICB_SYNCTOKEN Token = {};
    HRESULT hr;

    try
    {
        Device->Context->Flush(TL::COMMAND_LIST_TYPE_ALL_MASK);
    }
    catch (_com_error& Error)
    {
        NineTrace(L"flushing before a resource changes hands failed (0x%08lX)", Error.Error());
    }

    Token.hSyncToken = Args->hSyncToken;
    hr = Callback(Device->RuntimeDevice, &Token);
    if (FAILED(hr))
        NineTrace(L"the runtime's sync token callback failed (0x%08lX)", hr);
}

VOID
APIENTRY
NineAcquireResource(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SYNCTOKEN* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    NineSyncToken(Device, Args, Device->Callbacks.pfnAcquireResourceCb);
}

VOID
APIENTRY
NineReleaseResource(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SYNCTOKEN* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    NineSyncToken(Device, Args, Device->Callbacks.pfnReleaseResourceCb);
}

HRESULT
APIENTRY
NineGetD3D12Device(
    _In_ HANDLE hDevice,
    _In_ REFIID Riid,
    _COM_Outptr_ VOID** Object)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr || Object == nullptr)
        return E_INVALIDARG;
    return NineDxgiResult(Device->Device12->QueryInterface(Riid, Object));
}

HRESULT
APIENTRY
NineGetD3D12Resource(
    _In_ HANDLE hResource,
    _In_ REFIID Riid,
    _COM_Outptr_ VOID** Object)
{
    NineResource* Resource = NineInteropResource(hResource);
    HRESULT hr = E_INVALIDARG;

    if (Resource != nullptr && Object != nullptr && Resource->Underlying &&
        Resource->Underlying->GetUnderlyingResource() != nullptr)
    {
        hr = Resource->Underlying->GetUnderlyingResource()->QueryInterface(Riid, Object);
    }
    return NineDxgiResult(hr);
}

VOID
APIENTRY
NineTransitionResource(
    _In_ HANDLE hDevice,
    _In_ HANDLE hResource,
    _In_ D3D12_RESOURCE_STATES State)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineResource* Resource = NineInteropResource(hResource);

    if (Device == nullptr || hResource == NULL)
        return;

    TL::ResourceStateManager& States = Device->Context->GetResourceStateManager();

    States.TransitionResource(Resource->Underlying.get(), State, COMMAND_LIST_TYPE::GRAPHICS, NineRuntimeTransition);
    States.ApplyAllResourceTransitions();
}

VOID
APIENTRY
NineSetResourceState(
    _In_ HANDLE hDevice,
    _In_ HANDLE hResource,
    _In_ D3D12_RESOURCE_STATES State)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineResource* Resource = NineInteropResource(hResource);
    TL::CCurrentResourceState::ExclusiveState Exclusive;

    if (Device == nullptr || hResource == NULL)
        return;

    /* The application left the resource in this state on the graphics queue as of now */
    Exclusive.FenceValue = Device->Context->GetCommandListID(COMMAND_LIST_TYPE::GRAPHICS);
    Exclusive.State = State;
    Exclusive.CommandListType = COMMAND_LIST_TYPE::GRAPHICS;
    Resource->Underlying->GetIdentity()->m_currentState.SetExclusiveResourceState(Exclusive);
}

VOID
APIENTRY
NineSetMaximumFrameLatency(
    _In_ HANDLE hDevice,
    _In_ UINT MaxFrameLatency)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    Device->Context->m_MaxFrameLatencyHelper.SetMaximumFrameLatency(MaxFrameLatency);
}

HRESULT
APIENTRY
NineAddResourceWaitsToQueue(
    _In_ HANDLE hResource,
    _In_ ID3D12CommandQueue* Queue)
{
    NineResource* Resource = NineInteropResource(hResource);
    TL::Resource* Underlying;
    TL::ManagedObject* Residency;
    TL::ImmediateContext* Context;

    if (Resource == nullptr || Queue == nullptr)
        return E_INVALIDARG;

    Underlying = Resource->Underlying.get();
    Residency = Underlying->GetResidencyHandle();
    Context = &*Resource->Device->Context;

    /* The resource stays pinned resident until the application hands it back */
    if (Residency != nullptr)
    {
        TL::ResidencySet Pinned;

        Residency->Pin();
        Pinned.Open(static_cast<UINT>(COMMAND_LIST_TYPE::MAX_VALID));
        Pinned.Insert(Residency);
        Pinned.Close();
        Context->GetResidencyManager().PreExecuteCommandQueueCommand(Queue, ~0U, &Pinned);
        Underlying->AddFenceForUnwrapResidency(Queue);
    }

    /* The application's queue waits for whatever work of ours still uses the resource */
    for (UINT Type = 0; Type < static_cast<UINT>(COMMAND_LIST_TYPE::MAX_VALID); Type += 1)
    {
        TL::CommandListManager* Manager = Context->GetCommandListManager(static_cast<COMMAND_LIST_TYPE>(Type));
        UINT64 LastUsed;

        if (Manager == nullptr)
            continue;

        LastUsed = Underlying->m_LastUsedCommandListID[Type];
        if (LastUsed > Manager->GetCompletedFenceValue())
        {
            Queue->Wait(Manager->GetFence()->Get(), LastUsed);
            Manager->SetNeedSubmitFence();
        }
    }
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineAddDeferredWaitsToResource(
    _In_ HANDLE hResource,
    _In_ UINT NumSync,
    _In_reads_(NumSync) UINT64* SignalValues,
    _In_reads_(NumSync) ID3D12Fence** Fences)
{
    NineResource* Resource = NineInteropResource(hResource);
    std::vector<TL::DeferredWait> Waits;
    TL::Resource* Underlying;
    TL::ManagedObject* Residency;

    if (Resource == nullptr)
        return E_INVALIDARG;

    Underlying = Resource->Underlying.get();
    Residency = Underlying->GetResidencyHandle();

    try
    {
        Waits.reserve(NumSync);
        for (UINT Index = 0; Index < NumSync; Index += 1)
        {
            Waits.push_back({ std::make_shared<TL::Fence>(&*Resource->Device->Context, Fences[Index]),
                              SignalValues[Index] });
        }
        Underlying->AddDeferredWaits(Waits);
    }
    catch (_com_error& Error)
    {
        return NineDxgiResult(Error.Error());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    /* Residency keeps the pin until the application's work is done, then lets it go */
    if (Residency != nullptr)
    {
        Residency->AddPinWaits(NumSync, SignalValues, Fences);
        Residency->UnPin();
    }
    return NineDxgiResult(S_OK);
}
