/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Presenting through the D3D12 runtime's present path
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "internal.hpp"

using D3D12TranslationLayer::COMMAND_LIST_TYPE;

On12PresentArgs::On12PresentArgs(
    _In_ const DXGI1_6_1_DDI_ARG_PRESENT* Args)
    : SurfaceCount(Args->SurfacesToPresent)
    , FlipInterval(Args->FlipInterval)
    , Destination(D3D11On12::Resource::CastFrom(Args->hDstResource))
    , DxgiContext(Args->pDXGIContext)
    , VidPnSourceId(D3DDDI_ID_UNINITIALIZED)
{
    D3D12TranslationLayer::PresentSurface* Out = Surfaces();

    for (UINT Index = 0; Index < SurfaceCount; Index += 1)
    {
        const DXGI_DDI_ARG_PRESENTSURFACE& Source = Args->phSurfacesToPresent[Index];
        D3D11On12::Resource* Surface = D3D11On12::Resource::CastFrom(Source.hSurface);

        Out[Index].m_pResource = Surface != nullptr ? Surface->ImmediateResource() : nullptr;
        Out[Index].m_subresource = Source.SubResourceIndex;
    }

    if (SurfaceCount != 0)
        VidPnSourceId = D3D11On12::Resource::CastFrom(Args->phSurfacesToPresent[0].hSurface)->m_VidPnSourceId;
}

size_t
On12PresentArgs::GetExtensionSize(
    _In_ const DXGI1_6_1_DDI_ARG_PRESENT* Args)
{
    return sizeof(On12PresentArgs) + sizeof(D3D12TranslationLayer::PresentSurface) * Args->SurfacesToPresent;
}

/* Hands the present to DXGI, which calls back into Present with the kernel arguments */
void
On12PresentExtension::Dispatch(
    _In_ D3D12TranslationLayer::ImmediateContext& Context,
    _In_ const void* Data,
    _In_ size_t DataSize)
{
    DXGIDDICB_PRESENT Callback = {};
    HRESULT hr;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(DataSize);

    Device->m_PendingPresent = static_cast<const On12PresentArgs*>(Data);
    Callback.pDXGIContext = Device->m_PendingPresent->DxgiContext;
    hr = Device->m_pDXGICallbacks->pfnPresentCb(Device->m_hRTDevice.handle, &Callback);
    if (FAILED(hr))
        On12Trace(L"DXGI present callback failed with 0x%08lX", hr);
    ThrowFailure(hr);
}

HRESULT
APIENTRY
On12Device::Present1(
    _In_ DXGI1_6_1_DDI_ARG_PRESENT* Args)
{
    On12Device* Self = static_cast<On12Device*>(D3D11On12::Device::CastFrom(Args->hDevice));
    D3D12TranslationLayer::BatchedContext& Batch = Self->GetBatchedContext();

    try
    {
        /* Without a batch thread the present runs right here, from a heap copy of its arguments */
        if (Self->GetImmediateContextNoFlush().IsXbox())
        {
            std::unique_ptr<void, decltype(&free)> Storage(nullptr, &free);
            size_t Size;

            Batch.ProcessBatch();
            Size = On12PresentArgs::GetExtensionSize(Args);
            Storage.reset(malloc(Size));
            if (Storage == nullptr)
                return E_OUTOFMEMORY;

            new (Storage.get()) On12PresentArgs(Args);
            On12PresentExtension Direct(Self);
            Direct.Dispatch(Self->GetImmediateContextNoFlush(), Storage.get(), Size);
        }
        else
        {
            Batch.EmplaceBatchExtension<On12PresentArgs>(&Self->m_PresentExtension, Args);
            Batch.SubmitBatch(false);
        }
    }
    catch (_com_error& Error)
    {
        return Error.Error();
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

STDMETHODIMP
On12Device::Present(
    _In_ D3DKMT_PRESENT* KmtPresent) noexcept
{
    const On12PresentArgs* Pending = m_PendingPresent;
    CComPtr<IOn12PresentCommandList> Recorder;
    D3D12TranslationLayer::Resource* Destination = nullptr;
    bool DoNotSequence;
    UINT Model;

    m_PendingPresent = nullptr;

    try
    {
        if (FAILED(m_ImmediateContext.GetGraphicsCommandList()->QueryInterface(IID_PPV_ARGS(&Recorder))))
        {
            ON12_TRACE_ONCE(L"D3D12 offers no present recording on its command lists");
            return E_FAIL;
        }

        if (Pending->Destination != nullptr)
            Destination = Pending->Destination->ImmediateResource();

        DoNotSequence = KmtPresent->Flags.FlipDoNotFlip ||
                        (KmtPresent->Flags.RedirectedFlip &&
                         KmtPresent->PresentHistoryToken.Model == D3DKMT_PM_REDIRECTED_FLIP &&
                         KmtPresent->PresentHistoryToken.Token.Flip.Flags.IndependentFlipDoNotFlip);
        Model = KmtPresent->Flags.Flip ? ON12_PRESENT_MODEL_FLIP : ON12_PRESENT_MODEL_BLT;

        m_PresentContext.Present(Pending->Surfaces(),
                                 Pending->SurfaceCount,
                                 Destination,
                                 Pending->FlipInterval,
                                 Model,
                                 Pending->VidPnSourceId,
                                 KmtPresent,
                                 DoNotSequence);
    }
    catch (_com_error& Error)
    {
        On12Trace(L"present failed with 0x%08lX", Error.Error());
        return Error.Error();
    }
    catch (std::bad_alloc&)
    {
        On12Trace(L"present ran out of memory");
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

On12PresentContext::On12PresentContext(
    _In_ D3D12TranslationLayer::ImmediateContext& Context)
    : m_Context(Context)
{
    Latency.Init(&Context);
}

VOID
On12PresentContext::Present(
    _In_reads_(SurfaceCount) const D3D12TranslationLayer::PresentSurface* Surfaces,
    _In_ UINT SurfaceCount,
    _In_opt_ D3D12TranslationLayer::Resource* Destination,
    _In_ UINT FlipInterval,
    _In_ UINT PresentModel,
    _In_ UINT VidPnSourceId,
    _In_ D3DKMT_PRESENT* KmtPresent,
    _In_ bool DoNotSequence)
{
    D3D12TranslationLayer::CommandListManager* Graphics = m_Context.GetCommandListManager(COMMAND_LIST_TYPE::GRAPHICS);
    ID3D12CommandQueue* Queue = nullptr;
    IOn12PresentQueue* Submitter = nullptr;

    if (Graphics != nullptr)
        Queue = Graphics->GetCommandQueue();

    if (Queue != nullptr)
    {
        if (m_PresentQueue == nullptr)
        {
            ThrowFailure(m_Context.GetCommandQueue(COMMAND_LIST_TYPE::GRAPHICS)->QueryInterface(
                IID_PPV_ARGS(&m_PresentQueue)));
        }
        Submitter = m_PresentQueue;
    }

    if (DoNotSequence)
    {
        /* Sequencing is what defers the destination's destruction, so a blt needs it */
        if (Destination != nullptr)
            ThrowFailure(E_INVALIDARG);
    }
    else
    {
        D3D12TranslationLayer::ResourceStateManager& States = m_Context.GetResourceStateManager();
        D3D12TranslationLayer::PresentSurface Resolved;
        std::vector<ON12_PRESENT_SURFACE> Planes;
        CComPtr<IOn12PresentCommandList> Recorder;
        ID3D12Resource* Target = nullptr;
        BOOL CommandsAdded = FALSE;
        UINT64 ListId;

        if (!KmtPresent->Flags.RedirectedFlip)
            Latency.WaitForMaximumFrameLatency();

        /* A blt reads a multisampled source through a resolved copy kept per window */
        if (Destination != nullptr)
        {
            D3D12TranslationLayer::Resource* Source = Surfaces[0].m_pResource;

            if (Source->AppDesc()->Samples() > 1)
            {
                Resolved.m_pResource = m_Context.m_BltResolveManager.GetBltResolveTempForWindow(KmtPresent->hWindow,
                                                                                                *Source);
                Resolved.m_subresource = 0;
                m_Context.ResourceResolveSubresource(Resolved.m_pResource, 0, Source, Surfaces[0].m_subresource,
                                                     Source->AppDesc()->Format());
                Surfaces = &Resolved;
                SurfaceCount = 1;
            }
        }

        Planes.resize(SurfaceCount);
        for (UINT Index = 0; Index < SurfaceCount; Index += 1)
        {
            D3D12TranslationLayer::Resource* Surface = Surfaces[Index].m_pResource;
            UINT Subresource = Surfaces[Index].m_subresource;
            UINT PlaneCount = Surface->AppDesc()->NonOpaquePlaneCount();
            UINT PerPlane = Surface->AppDesc()->SubresourcesPerPlane();

            for (UINT Plane = 0; Plane < PlaneCount; Plane += 1)
            {
                States.TransitionSubresource(Surface,
                                             Subresource + Plane * PerPlane,
                                             D3D12_RESOURCE_STATE_PRESENT,
                                             COMMAND_LIST_TYPE::GRAPHICS,
                                             D3D12TranslationLayer::SubresourceTransitionFlags::StateMatchExact);
            }

            Planes[Index].Resource = Surface->GetUnderlyingResource();
            Planes[Index].Subresource = Subresource;
        }

        if (Destination != nullptr)
        {
            States.TransitionResource(Destination, D3D12_RESOURCE_STATE_COPY_DEST);
            Target = Destination->GetUnderlyingResource();
        }

        States.ApplyAllResourceTransitions();

        ThrowFailure(Graphics->GetGraphicsCommandList()->QueryInterface(IID_PPV_ARGS(&Recorder)));

        ZeroMemory(&m_Record, sizeof(m_Record));
        m_Record.Initialized = 1;
        m_Record.VidPnSourceId = VidPnSourceId;
        Recorder->RecordPresent(Queue, Planes.data(), SurfaceCount, Target, nullptr, nullptr, PresentModel,
                                FlipInterval, &CommandsAdded, &m_Record);
        if (CommandsAdded)
            Graphics->AdditionalCommandsAdded();

        if (!m_Context.Flush(D3D12TranslationLayer::COMMAND_LIST_TYPE_GRAPHICS_MASK))
        {
            m_Context.CloseCommandList(D3D12TranslationLayer::COMMAND_LIST_TYPE_GRAPHICS_MASK);
            m_Context.ResetCommandList(D3D12TranslationLayer::COMMAND_LIST_TYPE_GRAPHICS_MASK);
        }

        if (Graphics->GetSharingContract() != nullptr)
        {
            for (UINT Index = 0; Index < SurfaceCount; Index += 1)
            {
                Graphics->GetSharingContract()->Present(Surfaces[Index].m_pResource->GetUnderlyingResource(),
                                                        Surfaces[Index].m_subresource,
                                                        KmtPresent->hWindow);
            }
        }

        /* Marked after the flush so the next command list keeps these alive */
        ListId = Graphics->GetCommandListID();
        if (Destination != nullptr)
            Destination->UsedInCommandList(COMMAND_LIST_TYPE::GRAPHICS, ListId);
        for (UINT Index = 0; Index < SurfaceCount; Index += 1)
            Surfaces[Index].m_pResource->UsedInCommandList(COMMAND_LIST_TYPE::GRAPHICS, ListId);

        if (!KmtPresent->Flags.RedirectedFlip)
            Latency.RecordPresentFenceValue(ListId);
    }

    Graphics->ExecuteCommandQueueCommand([Submitter, KmtPresent, this]()
    {
        Submitter->SubmitPresent(KmtPresent, &m_Record);
    });
    Graphics->PrepForCommandQueueSync();
}
