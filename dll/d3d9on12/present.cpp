/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Presenting: the runtime's present callback and the Direct3D 12 present recording
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

using TL::COMMAND_LIST_TYPE;

static_assert(sizeof(VOID*) != 8 || sizeof(D3DDDICB_PRESENT) == 584, "the runtime takes a 584 byte present callback argument");

/* A handle of 1 is how the runtime names no resource */
static
NineResource*
NinePresentResource(
    _In_opt_ HANDLE Handle)
{
    if (Handle == reinterpret_cast<HANDLE>(1))
        return nullptr;
    return static_cast<NineResource*>(Handle);
}

/* A blt present copies from the source, so it has to be fully typed and single sampled */
static
BOOLEAN
NineCanBltFrom(
    _In_ const D3D12_RESOURCE_DESC* Desc)
{
    if (CD3D11FormatHelper::GetTypeLevel(Desc->Format) == D3D11FTL_PARTIAL_TYPE)
        return FALSE;
    return Desc->SampleDesc.Count == 1;
}

/* Records the present on the direct command list, submits that list and then the present itself */
static
VOID
NineSubmitPresent(
    _Inout_ NineDevice* Device,
    _In_ const TL::PresentSurface* Surface,
    _In_opt_ TL::Resource* Destination,
    _In_ UINT FlipInterval,
    _In_ UINT Model,
    _In_ UINT VidPnSourceId,
    _Inout_ D3DKMT_PRESENT* KmtPresent)
{
    TL::ImmediateContext& Context = *Device->Context;
    TL::CommandListManager* Graphics = Context.GetCommandListManager(COMMAND_LIST_TYPE::GRAPHICS);
    TL::ResourceStateManager& States = Context.GetResourceStateManager();
    ID3D12CommandQueue* Queue = Graphics->GetCommandQueue();
    TL::unique_comptr<INinePresentCommandList> Recorder;
    TL::PresentSurface Shown = *Surface;
    NinePresentPlane Plane;
    BOOL CommandsAdded = FALSE;
    UINT64 ListId;

    if (!Device->PresentQueue)
        TL::ThrowFailure(Queue->QueryInterface(IID_PPV_ARGS(&Device->PresentQueue)));

    if (!KmtPresent->Flags.RedirectedFlip)
        Device->Latency.WaitForMaximumFrameLatency();

    /* A multisampled source is resolved into a copy kept per window before a blt */
    if (Destination != nullptr && Shown.m_pResource->AppDesc()->Samples() > 1)
    {
        TL::Resource* Source = Shown.m_pResource;

        Shown.m_pResource = Context.m_BltResolveManager.GetBltResolveTempForWindow(KmtPresent->hWindow, *Source);
        Shown.m_subresource = 0;
        Context.ResourceResolveSubresource(Shown.m_pResource, 0, Source, Surface->m_subresource,
                                           Source->AppDesc()->Format());
    }

    for (UINT Index = 0; Index < Shown.m_pResource->AppDesc()->NonOpaquePlaneCount(); Index += 1)
    {
        States.TransitionSubresource(Shown.m_pResource,
                                     Shown.m_subresource + Index * Shown.m_pResource->AppDesc()->SubresourcesPerPlane(),
                                     D3D12_RESOURCE_STATE_PRESENT,
                                     COMMAND_LIST_TYPE::GRAPHICS,
                                     TL::SubresourceTransitionFlags::StateMatchExact);
    }
    if (Destination != nullptr)
        States.TransitionResource(Destination, D3D12_RESOURCE_STATE_COPY_DEST);
    States.ApplyAllResourceTransitions();

    TL::ThrowFailure(Graphics->GetGraphicsCommandList()->QueryInterface(IID_PPV_ARGS(&Recorder)));

    ZeroMemory(&Device->Record, sizeof(Device->Record));
    Device->Record.Initialized = 1;
    Device->Record.VidPnSourceId = VidPnSourceId;
    Plane.Resource = Shown.m_pResource->GetUnderlyingResource();
    Plane.Subresource = Shown.m_subresource;
    Recorder->RecordPresent(Queue, &Plane, 1, Destination != nullptr ? Destination->GetUnderlyingResource() : nullptr,
                            nullptr, 0, Model, FlipInterval, &CommandsAdded, &Device->Record);
    if (CommandsAdded)
        Graphics->AdditionalCommandsAdded();

    if (!Context.Flush(TL::COMMAND_LIST_TYPE_GRAPHICS_MASK))
    {
        Context.CloseCommandList(TL::COMMAND_LIST_TYPE_GRAPHICS_MASK);
        Context.ResetCommandList(TL::COMMAND_LIST_TYPE_GRAPHICS_MASK);
    }

    if (Graphics->GetSharingContract() != nullptr)
    {
        Graphics->GetSharingContract()->Present(Shown.m_pResource->GetUnderlyingResource(), Shown.m_subresource,
                                                KmtPresent->hWindow);
    }

    /* Marked after the flush, so the resources live until the list after this one is done */
    ListId = Graphics->GetCommandListID();
    if (Destination != nullptr)
        Destination->UsedInCommandList(COMMAND_LIST_TYPE::GRAPHICS, ListId);
    Shown.m_pResource->UsedInCommandList(COMMAND_LIST_TYPE::GRAPHICS, ListId);
    if (!KmtPresent->Flags.RedirectedFlip)
        Device->Latency.RecordPresentFenceValue(ListId);

    Graphics->ExecuteCommandQueueCommand([Device, KmtPresent]()
    {
        Device->PresentQueue->SubmitPresent(KmtPresent, &Device->Record);
    });
    Graphics->PrepForCommandQueueSync();
}

/* Presents what the pending Present1 named, now that the runtime handed over the kernel arguments */
static
HRESULT
NinePresentPending(
    _Inout_ NineDevice* Device,
    _In_ const D3DDDIARG_PRESENT1* Args,
    _Inout_ D3DKMT_PRESENT* KmtPresent)
{
    NineResource* Destination = NinePresentResource(Args->hDstResource);
    NineResource* Source = NinePresentResource(Args->phSrcResources[0].hResource);
    TL::PresentSurface Surface;
    BOOLEAN Direct;

    if (Source == nullptr)
        return E_INVALIDARG;

    /* System memory and 10 bit sources are presented through a plain copy */
    Direct = !Source->SystemMemory && Source->Format != D3DDDIFMT_A2R10G10B10;
    if (Destination != nullptr)
        Direct = Direct && NineCanBltFrom(&Source->Desc);
    if (!Direct)
    {
        NINE_TRACE_ONCE(L"presenting through a plain copy of the source is not implemented");
        return E_NOTIMPL;
    }

    Surface.m_pResource = Source->Underlying.get();
    Surface.m_subresource = Args->phSrcResources[0].SubResourceIndex;
    NineSubmitPresent(Device,
                      &Surface,
                      Destination != nullptr ? Destination->Underlying.get() : nullptr,
                      Args->FlipInterval,
                      KmtPresent->Flags.Flip ? NinePresentModelFlip : NinePresentModelBlt,
                      Source->VidPnSourceId,
                      KmtPresent);
    return S_OK;
}

HRESULT
APIENTRY
NinePresent1(
    _In_ HANDLE hDevice,
    _In_ D3DDDIARG_PRESENT1* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    D3DDDICB_PRESENT Callback = {};
    HRESULT hr;

    if (Device == nullptr)
        return E_INVALIDARG;

    /* The runtime answers the callback through the private kernel present, which takes this up */
    Device->PendingPresent = Args;
    hr = Device->Callbacks.pfnPresentCb(Device->RuntimeDevice, &Callback);
    if (FAILED(hr))
        NineTrace(L"the runtime's present callback failed (0x%08lX)", hr);
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineKernelPresent(
    _In_ HANDLE hDevice,
    _Inout_ D3DKMT_PRESENT* KmtPresent)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    HRESULT hr;

    if (Device == nullptr || Device->PendingPresent == nullptr || KmtPresent == nullptr)
        return E_INVALIDARG;

    try
    {
        hr = NinePresentPending(Device, Device->PendingPresent, KmtPresent);
    }
    catch (_com_error& Error)
    {
        hr = Error.Error();
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }

    if (FAILED(hr))
        NineTrace(L"the present is not submitted (0x%08lX)", hr);
    Device->PendingPresent = nullptr;
    return NineDxgiResult(hr);
}
