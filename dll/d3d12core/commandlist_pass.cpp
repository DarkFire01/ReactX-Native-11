/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12GraphicsCommandList: render passes and enhanced barriers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "commandlist_internal.hpp"

/* Render passes, built from clears, render target binding and resolves */

/* The resolve parameters a pass ends with live in the list, as the application's may not last */
static
UINT
RxPassResolveCount(
    _In_ const RxRenderPass& Pass)
{
    UINT Count = 0;
    UINT Index;

    for (Index = 0; Index < Pass.TargetCount; Index++)
    {
        if (Pass.Targets[Index].EndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
            Count += Pass.Targets[Index].EndingAccess.Resolve.SubresourceCount;
    }

    if (Pass.HasDepthStencil)
    {
        if (Pass.DepthStencil.DepthEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE &&
            Pass.DepthStencil.DepthEndingAccess.Resolve.pSrcResource != nullptr)
        {
            Count += Pass.DepthStencil.DepthEndingAccess.Resolve.SubresourceCount;
        }
        if (Pass.DepthStencil.StencilEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE &&
            Pass.DepthStencil.StencilEndingAccess.Resolve.pSrcResource != nullptr)
        {
            Count += Pass.DepthStencil.StencilEndingAccess.Resolve.SubresourceCount;
        }
    }

    return Count;
}

/* Moves one ending access's subresource list into the pass's own storage */
static
VOID
RxKeepResolves(
    _Inout_ D3D12_RENDER_PASS_ENDING_ACCESS& Ending,
    _Inout_ D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS** Next)
{
    if (Ending.Type != D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE || Ending.Resolve.pSrcResource == nullptr ||
        Ending.Resolve.SubresourceCount == 0 || Ending.Resolve.pSubresourceParameters == nullptr)
    {
        return;
    }

    memcpy(*Next, Ending.Resolve.pSubresourceParameters, Ending.Resolve.SubresourceCount * sizeof(**Next));
    Ending.Resolve.pSubresourceParameters = *Next;
    *Next += Ending.Resolve.SubresourceCount;
}

VOID
STDMETHODCALLTYPE
RxCommandList::BeginRenderPass(
    _In_ UINT NumRenderTargets,
    _In_reads_opt_(NumRenderTargets) const D3D12_RENDER_PASS_RENDER_TARGET_DESC* pRenderTargets,
    _In_opt_ const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* pDepthStencil,
    D3D12_RENDER_PASS_FLAGS Flags)
{
    const BOOLEAN Resuming = (Flags & D3D12_RENDER_PASS_FLAG_RESUMING_PASS) != 0;
    D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS* Next;
    UINT Needed;
    UINT Index;

    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListDoesGraphics | RxListTopLevel) || !Records())
        return;

    if (InPass || (PassSuspended && !Resuming))
    {
        Fail(E_INVALIDARG);
        return;
    }
    if (NumRenderTargets > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)
    {
        Fail(E_FAIL);
        return;
    }
    if (NumRenderTargets != 0 && pRenderTargets == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    /* A pass resumed in a list that suspended none resumes one from another list, unchecked */
    if (Resuming && !PassSuspended)
        PassResumedFresh = TRUE;
    if (!PassResumedFresh && Resuming &&
        (NumRenderTargets != Pass.TargetCount || (pDepthStencil != nullptr) != Pass.HasDepthStencil ||
         ((Flags ^ Pass.Flags) & ~(D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS | D3D12_RENDER_PASS_FLAG_RESUMING_PASS)) != 0))
    {
        Fail(E_INVALIDARG);
        return;
    }

    Pass.TargetCount = NumRenderTargets;
    if (NumRenderTargets != 0)
        memcpy(Pass.Targets, pRenderTargets, NumRenderTargets * sizeof(*pRenderTargets));
    Pass.HasDepthStencil = pDepthStencil != nullptr;
    if (pDepthStencil != nullptr)
        Pass.DepthStencil = *pDepthStencil;
    else
        ZeroMemory(&Pass.DepthStencil, sizeof(Pass.DepthStencil));
    Pass.Flags = Flags;

    Needed = RxPassResolveCount(Pass);
    if (Needed > Pass.ResolveCapacity)
    {
        delete[] Pass.Resolves;
        Pass.ResolveCapacity = 0;
        Pass.Resolves = new (std::nothrow) D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS[Needed];
        if (Pass.Resolves == nullptr)
        {
            Fail(E_OUTOFMEMORY);
            return;
        }
        Pass.ResolveCapacity = Needed;
    }

    Next = Pass.Resolves;
    for (Index = 0; Index < Pass.TargetCount; Index++)
        RxKeepResolves(Pass.Targets[Index].EndingAccess, &Next);
    if (Pass.HasDepthStencil)
    {
        RxKeepResolves(Pass.DepthStencil.DepthEndingAccess, &Next);
        RxKeepResolves(Pass.DepthStencil.StencilEndingAccess, &Next);
    }

    StartEmulatedPass(NumRenderTargets, pRenderTargets, pDepthStencil, Flags);
    InPass = TRUE;
    PassSuspended = FALSE;
}

/* Whether a beginning access leaves the view bound as a target */
static
BOOLEAN
RxBindsTarget(
    _In_ D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE Type)
{
    return Type != D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS &&
           Type != D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV &&
           Type != D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_UAV;
}

VOID
RxCommandList::StartEmulatedPass(
    _In_ UINT NumRenderTargets,
    _In_reads_opt_(NumRenderTargets) const D3D12_RENDER_PASS_RENDER_TARGET_DESC* pRenderTargets,
    _In_opt_ const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* pDepthStencil,
    _In_ D3D12_RENDER_PASS_FLAGS Flags)
{
    const UINT ReadOnly = D3D12_RENDER_PASS_FLAG_BIND_READ_ONLY_DEPTH | D3D12_RENDER_PASS_FLAG_BIND_READ_ONLY_STENCIL;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Targets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DepthStencil = {};
    BOOLEAN BindDepth = FALSE;
    UINT Bound = 0;
    UINT Index;

    for (Index = 0; Index < NumRenderTargets; Index++)
    {
        if (RxBindsTarget(pRenderTargets[Index].BeginningAccess.Type))
            Targets[Bound++] = RxDriverTarget(pRenderTargets[Index].cpuDescriptor);
    }

    /* The depth view stays bound unless both planes leave it, or one is read as a shader resource unbound */
    if (pDepthStencil != nullptr)
    {
        D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE Depth = pDepthStencil->DepthBeginningAccess.Type;
        D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE Stencil = pDepthStencil->StencilBeginningAccess.Type;
        BOOLEAN DepthGone = Depth == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS ||
                            Depth == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_UAV;
        BOOLEAN StencilGone = Stencil == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS ||
                              Stencil == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_UAV;

        if (!DepthGone || !StencilGone)
        {
            if (Depth == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV ||
                Stencil == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV)
            {
                BindDepth = (Flags & ReadOnly) != 0;
            }
            else
            {
                BindDepth = TRUE;
            }
        }
        DepthStencil.ptr = pDepthStencil->cpuDescriptor.ptr;
    }

    /* A resumed pass keeps what the suspended one left */
    if ((Flags & D3D12_RENDER_PASS_FLAG_RESUMING_PASS) == 0)
    {
        for (Index = 0; Index < NumRenderTargets; Index++)
        {
            const D3D12_RENDER_PASS_BEGINNING_ACCESS& Begin = pRenderTargets[Index].BeginningAccess;
            FLOAT Color[4];
            UINT Channel;

            if (Begin.Type != D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
                continue;

            for (Channel = 0; Channel < 4; Channel++)
                Color[Channel] = RxZeroDenormal(Begin.Clear.ClearValue.Color[Channel]);
            DriverTable->pfnClearRenderTargetView(DriverList, RxDriverTarget(pRenderTargets[Index].cpuDescriptor),
                                                  Color, 0, nullptr);
        }

        if (pDepthStencil != nullptr)
        {
            BOOLEAN ClearDepth = pDepthStencil->DepthBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
            BOOLEAN ClearStencil = pDepthStencil->StencilBeginningAccess.Type ==
                                   D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
            UINT ClearFlags = (ClearDepth ? D3D12_CLEAR_FLAG_DEPTH : 0) | (ClearStencil ? D3D12_CLEAR_FLAG_STENCIL : 0);
            FLOAT Depth = ClearDepth ? pDepthStencil->DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth : 0.0f;
            UINT8 Stencil = ClearStencil ? pDepthStencil->StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil
                                         : 0;

            if (ClearFlags != 0)
            {
                DriverTable->pfnClearDepthStencilView(DriverList, DepthStencil, ClearFlags, RxZeroDenormal(Depth),
                                                      Stencil, 0, nullptr);
            }
        }
    }

    DriverTable->pfnOMSetRenderTargets(Api(), Bound, Bound != 0 ? Targets : nullptr, FALSE,
                                       BindDepth ? &DepthStencil : nullptr);
}

VOID
STDMETHODCALLTYPE
RxCommandList::EndRenderPass(VOID)
{
    if (!Takes(RxListDoesCopy | RxListDoesCompute | RxListDoesGraphics | RxListTopLevel) || !Records())
        return;

    if (!InPass)
    {
        Fail(E_INVALIDARG);
        return;
    }

    InPass = FALSE;
    PassSuspended = (Pass.Flags & D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS) != 0;
    EndEmulatedPass();
}

/* The state a target is in during the pass, from how the pass began with it */
static
D3D12_RESOURCE_STATES
RxPassStateOf(
    _In_ D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE Begin,
    _In_ D3D12_RESOURCE_STATES Bound)
{
    if (Begin == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV)
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (Begin == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_UAV)
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    return Bound;
}

/* One resolving ending access and the state its source is in during the pass */
struct RxPassResolve
{
    const D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_PARAMETERS* Resolve;
    D3D12_RESOURCE_STATES State;
    INT Plane;
};

/* The barriers moving every resolve source to a resolve source, or back when Back is set */
static
UINT
RxPassResolveBarriers(
    _In_reads_(Count) const RxPassResolve* Resolves,
    _In_ UINT Count,
    _In_ BOOLEAN Back,
    _Out_writes_opt_(return) D3D12_RESOURCE_BARRIER* Barriers)
{
    UINT Written = 0;
    UINT Index;
    UINT Part;

    for (Index = 0; Index < Count; Index++)
    {
        const D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_PARAMETERS& Resolve = *Resolves[Index].Resolve;

        /* Color targets move each resolved subresource; a depth or stencil source moves its plane */
        UINT Parts = Resolves[Index].Plane < 0 ? Resolve.SubresourceCount : 1;

        for (Part = 0; Part < Parts; Part++)
        {
            if (Barriers != nullptr)
            {
                D3D12_RESOURCE_BARRIER& Out = Barriers[Written];

                ZeroMemory(&Out, sizeof(Out));
                Out.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Out.Transition.pResource = Resolve.pSrcResource;
                Out.Transition.Subresource = Resolves[Index].Plane < 0 ? Resolve.pSubresourceParameters[Part].SrcSubresource
                                                                       : (UINT)Resolves[Index].Plane;
                Out.Transition.StateBefore = Back ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE : Resolves[Index].State;
                Out.Transition.StateAfter = Back ? Resolves[Index].State : D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
            }
            Written++;
        }
    }

    return Written;
}

VOID
RxCommandList::EndEmulatedPass()
{
    RxPassResolve Resolves[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 2];
    D3D12_RESOURCE_BARRIER* Barriers;
    UINT BarrierCount;
    UINT Count = 0;
    UINT Index;
    UINT Part;

    DriverTable->pfnOMSetRenderTargets(Api(), 0, nullptr, FALSE, nullptr);

    /* A suspended pass resolves nothing until the pass that resumes it ends */
    if ((Pass.Flags & D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS) != 0)
        return;

    for (Index = 0; Index < Pass.TargetCount; Index++)
    {
        const D3D12_RENDER_PASS_RENDER_TARGET_DESC& Target = Pass.Targets[Index];

        if (Target.EndingAccess.Type != D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
            continue;

        Resolves[Count].Resolve = &Target.EndingAccess.Resolve;
        Resolves[Count].State = RxPassStateOf(Target.BeginningAccess.Type, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Resolves[Count].Plane = -1;
        Count++;
    }

    if (Pass.HasDepthStencil)
    {
        const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC& Ds = Pass.DepthStencil;

        if (Ds.DepthEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE &&
            Ds.DepthEndingAccess.Resolve.pSrcResource != nullptr)
        {
            Resolves[Count].Resolve = &Ds.DepthEndingAccess.Resolve;
            Resolves[Count].State = RxPassStateOf(Ds.DepthBeginningAccess.Type, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            Resolves[Count].Plane = 0;
            Count++;
        }
        if (Ds.StencilEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE &&
            Ds.StencilEndingAccess.Resolve.pSrcResource != nullptr)
        {
            Resolves[Count].Resolve = &Ds.StencilEndingAccess.Resolve;
            Resolves[Count].State = RxPassStateOf(Ds.StencilBeginningAccess.Type, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            Resolves[Count].Plane = 1;
            Count++;
        }
    }

    BarrierCount = RxPassResolveBarriers(Resolves, Count, FALSE, nullptr);
    Barriers = BarrierCount != 0 ? new (std::nothrow) D3D12_RESOURCE_BARRIER[BarrierCount] : nullptr;
    if (Barriers != nullptr)
    {
        RxPassResolveBarriers(Resolves, Count, FALSE, Barriers);
        ResourceBarrier(BarrierCount, Barriers);
    }

    for (Index = 0; Index < Count; Index++)
    {
        const D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_PARAMETERS& Resolve = *Resolves[Index].Resolve;
        RxResourceFacts Dst;
        RxResourceFacts Src;

        if (!RxReadResource(Resolve.pDstResource, &Dst) || !RxReadResource(Resolve.pSrcResource, &Src))
        {
            Fail(E_INVALIDARG);
            continue;
        }

        for (Part = 0; Part < Resolve.SubresourceCount; Part++)
        {
            const D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS& Region = Resolve.pSubresourceParameters[Part];
            const D3D12_RECT& Rect = Region.SrcRect;
            BOOLEAN Whole = Rect.left == 0 && Rect.top == 0 && Rect.right == 0 && Rect.bottom == 0;
            D3D12_RECT* SrcRect = Whole ? nullptr : const_cast<D3D12_RECT*>(&Rect);
            HRESULT hr;

            hr = RxCheckResolve(Device, Dst, Region.DstSubresource, Region.DstX, Region.DstY, Src, Region.SrcSubresource,
                                SrcRect, Resolve.Format, Resolve.ResolveMode);
            if (FAILED(hr))
            {
                Fail(hr);
                continue;
            }

            if (!Records() || DriverTable->pfnResourceResolveSubresourceRegion == nullptr)
                continue;

            DriverTable->pfnResourceResolveSubresourceRegion(DriverList, Dst.Resource->GetDriverHandle(),
                                                             Region.DstSubresource, Region.DstX, Region.DstY,
                                                             Src.Resource->GetDriverHandle(), Region.SrcSubresource,
                                                             SrcRect, Resolve.Format,
                                                             (D3D12DDI_RESOLVE_MODE)Resolve.ResolveMode);
        }
    }

    if (Barriers != nullptr)
    {
        RxPassResolveBarriers(Resolves, Count, TRUE, Barriers);
        ResourceBarrier(BarrierCount, Barriers);
        delete[] Barriers;
    }
}

/* Enhanced barriers */

constexpr UINT RxSyncShading = D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_INDEX_INPUT | D3D12_BARRIER_SYNC_VERTEX_SHADING |
                               D3D12_BARRIER_SYNC_PIXEL_SHADING | D3D12_BARRIER_SYNC_DEPTH_STENCIL |
                               D3D12_BARRIER_SYNC_RENDER_TARGET | D3D12_BARRIER_SYNC_RESOLVE;
constexpr UINT RxSyncComputeWork = D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_COMPUTE_SHADING |
                                   D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_COPY |
                                   D3D12_BARRIER_SYNC_EXECUTE_INDIRECT | D3D12_BARRIER_SYNC_ALL_SHADING |
                                   D3D12_BARRIER_SYNC_NON_PIXEL_SHADING |
                                   D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO |
                                   D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW |
                                   D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
                                   D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE;
constexpr UINT RxSyncCopyWork = D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_COPY | D3D12_BARRIER_SYNC_PREDICATION;

/* The work a list type may wait on; the split bit is not counted */
static
UINT
RxListSyncs(
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    switch (Type)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            return RxSyncComputeWork | RxSyncShading;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            return RxSyncComputeWork;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return RxSyncCopyWork;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
            return RxSyncCopyWork | D3D12_BARRIER_SYNC_VIDEO_DECODE;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
            return RxSyncCopyWork | D3D12_BARRIER_SYNC_VIDEO_PROCESS;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
            return RxSyncCopyWork | D3D12_BARRIER_SYNC_VIDEO_ENCODE;
        default:
            return 0;
    }
}

constexpr UINT RxAccessCopyWork = D3D12_BARRIER_ACCESS_PREDICATION | D3D12_BARRIER_ACCESS_COPY_DEST;

/* The accesses a list type may name, besides no access */
static
UINT
RxListAccesses(
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    switch (Type)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            return D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE * 2 - 1;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            return D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
                   D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT |
                   D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE |
                   D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ |
                   D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return RxAccessCopyWork | D3D12_BARRIER_ACCESS_COPY_SOURCE;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
            return RxAccessCopyWork | D3D12_BARRIER_ACCESS_VIDEO_DECODE_READ | D3D12_BARRIER_ACCESS_VIDEO_DECODE_WRITE;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
            return RxAccessCopyWork | D3D12_BARRIER_ACCESS_VIDEO_PROCESS_READ | D3D12_BARRIER_ACCESS_VIDEO_PROCESS_WRITE;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
            return RxAccessCopyWork | D3D12_BARRIER_ACCESS_VIDEO_ENCODE_READ | D3D12_BARRIER_ACCESS_VIDEO_ENCODE_WRITE;
        default:
            return 0;
    }
}

/* The work an access can be waited on through */
static
UINT
RxSyncsFor(
    _In_ UINT Access)
{
    struct RxAccessSync
    {
        UINT Access;
        UINT Sync;
    };
    static const RxAccessSync Table[] =
    {
        { D3D12_BARRIER_ACCESS_VERTEX_BUFFER, D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING |
                                                  D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING },
        { D3D12_BARRIER_ACCESS_CONSTANT_BUFFER, D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING |
                                                    D3D12_BARRIER_SYNC_PIXEL_SHADING | D3D12_BARRIER_SYNC_COMPUTE_SHADING |
                                                    D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_ALL_SHADING |
                                                    D3D12_BARRIER_SYNC_NON_PIXEL_SHADING },
        { D3D12_BARRIER_ACCESS_INDEX_BUFFER, D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_INDEX_INPUT },
        { D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_RENDER_TARGET },
        { D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
          D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_PIXEL_SHADING |
              D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_ALL_SHADING |
              D3D12_BARRIER_SYNC_NON_PIXEL_SHADING | D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO |
              D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW | D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
              D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE },
        { D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_DEPTH_STENCIL },
        { D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ, D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_DEPTH_STENCIL },
        { D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
          D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_PIXEL_SHADING |
              D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_ALL_SHADING |
              D3D12_BARRIER_SYNC_NON_PIXEL_SHADING | D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE },
        { D3D12_BARRIER_ACCESS_STREAM_OUTPUT, D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING |
                                                  D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING },
        { D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT, D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_EXECUTE_INDIRECT },
        { D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_SYNC_COPY },
        { D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_SYNC_COPY },
        { D3D12_BARRIER_ACCESS_RESOLVE_DEST, D3D12_BARRIER_SYNC_RESOLVE },
        { D3D12_BARRIER_ACCESS_RESOLVE_SOURCE, D3D12_BARRIER_SYNC_RESOLVE },
        { D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ,
          D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_ALL_SHADING |
              D3D12_BARRIER_SYNC_NON_PIXEL_SHADING | D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO |
              D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
              D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE },
        { D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE,
          D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_ALL_SHADING |
              D3D12_BARRIER_SYNC_NON_PIXEL_SHADING | D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
              D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE },
        { D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE,
          D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_PIXEL_SHADING | D3D12_BARRIER_SYNC_ALL_SHADING },
        { D3D12_BARRIER_ACCESS_VIDEO_DECODE_READ | D3D12_BARRIER_ACCESS_VIDEO_DECODE_WRITE, D3D12_BARRIER_SYNC_VIDEO_DECODE },
        { D3D12_BARRIER_ACCESS_VIDEO_PROCESS_READ | D3D12_BARRIER_ACCESS_VIDEO_PROCESS_WRITE, D3D12_BARRIER_SYNC_VIDEO_PROCESS },
        { D3D12_BARRIER_ACCESS_VIDEO_ENCODE_READ | D3D12_BARRIER_ACCESS_VIDEO_ENCODE_WRITE, D3D12_BARRIER_SYNC_VIDEO_ENCODE },
    };
    UINT Sync = D3D12_BARRIER_SYNC_ALL;

    /* Common and no access go with any work but view clears */
    if ((Access & ~(UINT)D3D12_BARRIER_ACCESS_NO_ACCESS) == 0)
    {
        return (UINT)D3D12_BARRIER_SYNC_SPLIT | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE |
               D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_VIDEO_ENCODE |
               D3D12_BARRIER_SYNC_VIDEO_PROCESS | D3D12_BARRIER_SYNC_VIDEO_DECODE |
               (D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW - 1);
    }

    for (const RxAccessSync& Entry : Table)
    {
        if ((Access & Entry.Access) != 0)
            Sync |= Entry.Sync;
    }

    return Sync;
}

static
BOOLEAN
RxSyncIsValid(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ UINT Access,
    _In_ UINT Sync)
{
    const UINT Known = RxListSyncs(ListType) | (UINT)D3D12_BARRIER_SYNC_SPLIT;

    if ((Sync & ~Known) != 0)
        return FALSE;
    if (Sync == D3D12_BARRIER_SYNC_NONE)
        return Access == (UINT)D3D12_BARRIER_ACCESS_NO_ACCESS;
    if (Sync == (UINT)D3D12_BARRIER_SYNC_SPLIT)
        return TRUE;

    return (Sync & ~RxSyncsFor(Access)) == 0;
}

/* What kind of barrier an access is named in */
enum RxBarrierKind
{
    RxGlobalKind,
    RxTextureKind,
    RxBufferKind,
};

static
BOOLEAN
RxAccessIsValid(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ UINT Access,
    _In_ RxBarrierKind Kind,
    _In_ UINT Forbidden)
{
    const UINT Defined = D3D12_BARRIER_ACCESS_VIDEO_ENCODE_WRITE * 2 - 1;

    if ((Access & ~(RxListAccesses(ListType) | (UINT)D3D12_BARRIER_ACCESS_NO_ACCESS)) != 0)
        return FALSE;

    /* No access stands alone */
    if ((Access & (UINT)D3D12_BARRIER_ACCESS_NO_ACCESS) != 0 && Access != (UINT)D3D12_BARRIER_ACCESS_NO_ACCESS)
        return FALSE;

    if (Kind == RxTextureKind)
    {
        Forbidden |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER |
                     D3D12_BARRIER_ACCESS_INDEX_BUFFER | D3D12_BARRIER_ACCESS_STREAM_OUTPUT |
                     D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ |
                     D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
    }
    else if (Kind == RxBufferKind)
    {
        Forbidden |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ |
                     (Defined & ~(D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE - 1));
    }

    return (Access & Forbidden & ~(UINT)D3D12_BARRIER_ACCESS_NO_ACCESS) == 0 &&
           (Access & ~(Defined | (UINT)D3D12_BARRIER_ACCESS_NO_ACCESS)) == 0;
}

/* An access a resource was not made for is no access it can have */
static
BOOLEAN
RxResourceAccessIsValid(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ UINT Access,
    _In_ RxBarrierKind Kind,
    _In_ UINT ResourceFlags,
    _In_ UINT Forbidden)
{
    const UINT Structures = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ |
                            D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
    const UINT Depth = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;

    if (!RxAccessIsValid(ListType, Access, Kind, Forbidden))
        return FALSE;
    if ((Access & Structures) != 0 && (ResourceFlags & D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE) == 0)
        return FALSE;
    if ((Access & Depth) != 0 && (ResourceFlags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == 0)
        return FALSE;
    if ((Access & D3D12_BARRIER_ACCESS_RENDER_TARGET) != 0 && (ResourceFlags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == 0)
        return FALSE;
    if ((Access & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS) != 0 &&
        (ResourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
RxLayoutIsForList(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ UINT Layout)
{
    const BOOLEAN Direct = ListType == D3D12_COMMAND_LIST_TYPE_DIRECT;
    const BOOLEAN Compute = ListType == D3D12_COMMAND_LIST_TYPE_COMPUTE;
    const BOOLEAN Video = ListType == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE ||
                          ListType == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS ||
                          ListType == D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE;

    switch (Layout)
    {
        case D3D12_BARRIER_LAYOUT_UNDEFINED:
        case D3D12_BARRIER_LAYOUT_COMMON:
            return TRUE;
        case D3D12_BARRIER_LAYOUT_GENERIC_READ:
        case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
        case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
        case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
        case D3D12_BARRIER_LAYOUT_COPY_DEST:
        case D3D12DDI_BARRIER_LAYOUT_LEGACY_COPY_SOURCE:
        case D3D12DDI_BARRIER_LAYOUT_LEGACY_COPY_DEST:
        case D3D12DDI_BARRIER_LAYOUT_LEGACY_SHADER_RESOURCE:
            return Direct || Compute;
        case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
        case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
        case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
        case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
        case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
        case D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COMMON:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST:
        case D3D12DDI_BARRIER_LAYOUT_LEGACY_PIXEL_SHADER_RESOURCE:
            return Direct;
        case D3D12_BARRIER_LAYOUT_VIDEO_DECODE_READ:
        case D3D12_BARRIER_LAYOUT_VIDEO_DECODE_WRITE:
            return ListType == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE;
        case D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_READ:
        case D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_WRITE:
            return ListType == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS;
        case D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_READ:
        case D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_WRITE:
            return ListType == D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE;
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COMMON:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_GENERIC_READ:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_SOURCE:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_DEST:
            return Compute;
        case D3D12DDI_BARRIER_LAYOUT_VIDEO_QUEUE_COMMON:
            return Video;
        default:
            return FALSE;
    }
}

/* The accesses a texture in a layout may have, no access included */
static
UINT
RxLayoutAccesses(
    _In_ UINT Layout)
{
    const UINT Copies = D3D12_BARRIER_ACCESS_COPY_SOURCE | D3D12_BARRIER_ACCESS_COPY_DEST;
    UINT Access;

    switch (Layout)
    {
        case D3D12_BARRIER_LAYOUT_COMMON:
            Access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE | Copies;
            break;
        case D3D12_BARRIER_LAYOUT_GENERIC_READ:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_GENERIC_READ:
            Access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_COPY_SOURCE;
            break;
        case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
            Access = D3D12_BARRIER_ACCESS_RENDER_TARGET;
            break;
        case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS:
            Access = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            break;
        case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
            Access = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
            break;
        case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
            Access = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
            break;
        case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE:
        case D3D12DDI_BARRIER_LAYOUT_LEGACY_SHADER_RESOURCE:
        case D3D12DDI_BARRIER_LAYOUT_LEGACY_PIXEL_SHADER_RESOURCE:
            Access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
            break;
        case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_SOURCE:
        case D3D12DDI_BARRIER_LAYOUT_LEGACY_COPY_SOURCE:
            Access = D3D12_BARRIER_ACCESS_COPY_SOURCE;
            break;
        case D3D12_BARRIER_LAYOUT_COPY_DEST:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_DEST:
        case D3D12DDI_BARRIER_LAYOUT_LEGACY_COPY_DEST:
            Access = D3D12_BARRIER_ACCESS_COPY_DEST;
            break;
        case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
            Access = D3D12_BARRIER_ACCESS_RESOLVE_SOURCE;
            break;
        case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
            Access = D3D12_BARRIER_ACCESS_RESOLVE_DEST;
            break;
        case D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE:
            Access = D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE;
            break;
        case D3D12_BARRIER_LAYOUT_VIDEO_DECODE_READ:
            Access = D3D12_BARRIER_ACCESS_VIDEO_DECODE_READ;
            break;
        case D3D12_BARRIER_LAYOUT_VIDEO_DECODE_WRITE:
            Access = D3D12_BARRIER_ACCESS_VIDEO_DECODE_WRITE;
            break;
        case D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_READ:
            Access = D3D12_BARRIER_ACCESS_VIDEO_PROCESS_READ;
            break;
        case D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_WRITE:
            Access = D3D12_BARRIER_ACCESS_VIDEO_PROCESS_WRITE;
            break;
        case D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_READ:
            Access = D3D12_BARRIER_ACCESS_VIDEO_ENCODE_READ;
            break;
        case D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_WRITE:
            Access = D3D12_BARRIER_ACCESS_VIDEO_ENCODE_WRITE;
            break;
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COMMON:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COMMON:
            Access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS | Copies;
            break;
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ:
            Access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ |
                     D3D12_BARRIER_ACCESS_COPY_SOURCE | D3D12_BARRIER_ACCESS_RESOLVE_SOURCE |
                     D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE;
            break;
        case D3D12DDI_BARRIER_LAYOUT_VIDEO_QUEUE_COMMON:
            Access = Copies;
            break;
        default:
            Access = 0;
            break;
    }

    return Access | (UINT)D3D12_BARRIER_ACCESS_NO_ACCESS;
}

/* What simultaneous access textures may be accessed as in the common layout */
static
UINT
RxSharedTextureAccesses(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ UINT ResourceFlags)
{
    UINT Access = (UINT)D3D12_BARRIER_ACCESS_NO_ACCESS;

    switch (ListType)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            Access |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_COPY_DEST |
                      D3D12_BARRIER_ACCESS_COPY_SOURCE | D3D12_BARRIER_ACCESS_RESOLVE_DEST |
                      D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE;
            if ((ResourceFlags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
                Access |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
            if ((ResourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
                Access |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            break;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            Access |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_COPY_DEST |
                      D3D12_BARRIER_ACCESS_COPY_SOURCE;
            if ((ResourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
                Access |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            break;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            Access |= D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE;
            break;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
            Access |= D3D12_BARRIER_ACCESS_VIDEO_DECODE_READ | D3D12_BARRIER_ACCESS_VIDEO_DECODE_WRITE;
            break;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
            Access |= D3D12_BARRIER_ACCESS_VIDEO_PROCESS_READ | D3D12_BARRIER_ACCESS_VIDEO_PROCESS_WRITE;
            break;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
            Access |= D3D12_BARRIER_ACCESS_VIDEO_ENCODE_READ | D3D12_BARRIER_ACCESS_VIDEO_ENCODE_WRITE;
            break;
        default:
            break;
    }

    return Access;
}

/* One side of a texture barrier: its access, work and layout belong together */
static
BOOLEAN
RxTextureSideIsValid(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ UINT Layout,
    _In_ UINT Access,
    _In_ UINT Sync,
    _In_ UINT ResourceFlags)
{
    UINT Allowed;

    if (!RxResourceAccessIsValid(ListType, Access, RxTextureKind, ResourceFlags, 0) ||
        !RxLayoutIsForList(ListType, Layout))
    {
        return FALSE;
    }

    /* An undefined layout has no access, and no access waits on no work */
    if (Layout == (UINT)D3D12_BARRIER_LAYOUT_UNDEFINED)
    {
        if (Access != (UINT)D3D12_BARRIER_ACCESS_NO_ACCESS)
            return FALSE;
    }
    else if (Access == (UINT)D3D12_BARRIER_ACCESS_NO_ACCESS && Sync != D3D12_BARRIER_SYNC_NONE)
    {
        return FALSE;
    }

    if ((ResourceFlags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) != 0)
    {
        if (Layout != D3D12_BARRIER_LAYOUT_COMMON && Layout != (UINT)D3D12_BARRIER_LAYOUT_UNDEFINED)
            return FALSE;
        Allowed = RxSharedTextureAccesses(ListType, ResourceFlags);
    }
    else
    {
        Allowed = RxLayoutAccesses(Layout);
    }

    if ((Access & ~Allowed) != 0)
        return FALSE;

    return RxSyncIsValid(ListType, Access, Sync);
}

BOOLEAN
RxCheckGlobalBarrier(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ const D3D12_GLOBAL_BARRIER& Barrier)
{
    const UINT Before = Barrier.AccessBefore;
    const UINT After = Barrier.AccessAfter;
    const UINT Depth = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;

    /* Common stays common, and a depth write is followed only by depth access */
    if (Before == D3D12_BARRIER_ACCESS_COMMON && After != D3D12_BARRIER_ACCESS_COMMON)
        return FALSE;
    if ((Before & D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE) != 0 && (After == 0 || (After & ~Depth) != 0))
        return FALSE;

    return RxSyncIsValid(ListType, Before, Barrier.SyncBefore) && RxSyncIsValid(ListType, After, Barrier.SyncAfter) &&
           RxAccessIsValid(ListType, Before, RxGlobalKind, 0) && RxAccessIsValid(ListType, After, RxGlobalKind, 0);
}

BOOLEAN
RxCheckBufferBarrier(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ const D3D12_BUFFER_BARRIER& Barrier,
    _In_ BOOLEAN BufferResolves)
{
    const UINT Resolves = BufferResolves ? 0 : D3D12_BARRIER_ACCESS_RESOLVE_DEST | D3D12_BARRIER_ACCESS_RESOLVE_SOURCE;
    RxResourceFacts Facts;

    /* A barrier covers the whole buffer */
    if (!RxReadResource(Barrier.pResource, &Facts) ||
        (Facts.Desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER && Facts.Desc->Dimension != D3D12_RESOURCE_DIMENSION_UNKNOWN))
    {
        return FALSE;
    }
    if (Barrier.Offset != 0 || (Barrier.Size != UINT64_MAX && Barrier.Size != Facts.Desc->Width))
        return FALSE;

    return RxResourceAccessIsValid(ListType, Barrier.AccessBefore, RxBufferKind, Facts.Desc->Flags, Resolves) &&
           RxResourceAccessIsValid(ListType, Barrier.AccessAfter, RxBufferKind, Facts.Desc->Flags, Resolves) &&
           RxSyncIsValid(ListType, Barrier.AccessBefore, Barrier.SyncBefore) &&
           RxSyncIsValid(ListType, Barrier.AccessAfter, Barrier.SyncAfter);
}

BOOLEAN
RxCheckTextureBarrier(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_ const D3D12_TEXTURE_BARRIER& Barrier)
{
    const D3D12_BARRIER_SUBRESOURCE_RANGE& Range = Barrier.Subresources;
    RxResourceFacts Facts;
    UINT Flags;
    UINT Slices;

    if (!RxReadResource(Barrier.pResource, &Facts) || Facts.Desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return FALSE;
    Flags = Facts.Desc->Flags;

    if (Barrier.LayoutBefore == D3D12_BARRIER_LAYOUT_UNDEFINED && Barrier.LayoutAfter == D3D12_BARRIER_LAYOUT_UNDEFINED)
    {
        if (!RxResourceAccessIsValid(ListType, Barrier.AccessBefore, RxTextureKind, Flags, 0) ||
            !RxResourceAccessIsValid(ListType, Barrier.AccessAfter, RxTextureKind, Flags, 0) ||
            !RxSyncIsValid(ListType, Barrier.AccessBefore, Barrier.SyncBefore) ||
            !RxSyncIsValid(ListType, Barrier.AccessAfter, Barrier.SyncAfter))
        {
            return FALSE;
        }
    }
    else if (!RxTextureSideIsValid(ListType, Barrier.LayoutBefore, Barrier.AccessBefore, Barrier.SyncBefore, Flags) ||
             !RxTextureSideIsValid(ListType, Barrier.LayoutAfter, Barrier.AccessAfter, Barrier.SyncAfter, Flags))
    {
        return FALSE;
    }

    /* No mip count names one subresource by index, or all of them */
    if (Range.NumMipLevels == 0)
    {
        return Range.IndexOrFirstMipLevel == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
               Range.IndexOrFirstMipLevel < Facts.Subresources;
    }

    Slices = Facts.Desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Facts.Desc->DepthOrArraySize;
    return Range.IndexOrFirstMipLevel + Range.NumMipLevels <= Facts.Desc->MipLevels &&
           Range.FirstArraySlice + Range.NumArraySlices <= Slices &&
           Range.FirstPlane + Range.NumPlanes <= RxFormatPlaneCount(Facts.Desc->Format);
}

VOID
STDMETHODCALLTYPE
RxCommandList::Barrier(
    UINT32 NumBarrierGroups,
    _In_reads_(NumBarrierGroups) const D3D12_BARRIER_GROUP* pBarrierGroups)
{
    D3D12DDIARG_BARRIER_0094 Stack[8];
    D3D12DDIARG_BARRIER_0094* Barriers = Stack;
    UINT Total = 0;
    UINT Written = 0;
    UINT Group;
    UINT Index;

    if (!Takes(RxListDoesCopy | RxListTopLevel) || !Records() || !PassAllows(TRUE))
        return;

    if (!Device.GetOptions().EnhancedBarriersSupported || DriverTable->pfnBarrier == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    /* Every barrier is checked; any bad one fails the list */
    for (Group = 0; Group < NumBarrierGroups; Group++)
    {
        const D3D12_BARRIER_GROUP& Each = pBarrierGroups[Group];

        if (Each.Type > D3D12_BARRIER_TYPE_BUFFER)
        {
            Fail(E_INVALIDARG);
            continue;
        }

        for (Index = 0; Index < Each.NumBarriers; Index++)
        {
            BOOLEAN Valid;

            switch (Each.Type)
            {
                case D3D12_BARRIER_TYPE_GLOBAL:
                    Valid = RxCheckGlobalBarrier(Type, Each.pGlobalBarriers[Index]);
                    break;
                case D3D12_BARRIER_TYPE_TEXTURE:
                    Valid = RxCheckTextureBarrier(Type, Each.pTextureBarriers[Index]);
                    break;
                case D3D12_BARRIER_TYPE_BUFFER:
                    Valid = RxCheckBufferBarrier(Type, Each.pBufferBarriers[Index],
                                                 Device.GetOptions().SamplerFeedbackTier != 0);
                    break;
                default:
                    Valid = FALSE;
                    break;
            }

            if (!Valid)
                Fail(E_INVALIDARG);
        }

        Total += Each.NumBarriers;
    }

    if (!Records())
        return;

    if (Total > _countof(Stack))
    {
        Barriers = new (std::nothrow) D3D12DDIARG_BARRIER_0094[Total];
        if (Barriers == nullptr)
            return;
    }

    /* The groups reach the driver as one list */
    for (Group = 0; Group < NumBarrierGroups; Group++)
    {
        const D3D12_BARRIER_GROUP& Each = pBarrierGroups[Group];

        for (Index = 0; Index < Each.NumBarriers; Index++)
        {
            D3D12DDIARG_BARRIER_0094& Out = Barriers[Written++];

            ZeroMemory(&Out, sizeof(Out));
            if (Each.Type == D3D12_BARRIER_TYPE_GLOBAL)
            {
                const D3D12_GLOBAL_BARRIER& In = Each.pGlobalBarriers[Index];

                Out.Type = D3D12DDI_BARRIER_TYPE_GLOBAL;
                Out.GlobalBarrier.SyncBefore = (D3D12DDI_BARRIER_SYNC)In.SyncBefore;
                Out.GlobalBarrier.SyncAfter = (D3D12DDI_BARRIER_SYNC)In.SyncAfter;
                Out.GlobalBarrier.AccessBefore = (D3D12DDI_BARRIER_ACCESS)In.AccessBefore;
                Out.GlobalBarrier.AccessAfter = (D3D12DDI_BARRIER_ACCESS)In.AccessAfter;
            }
            else if (Each.Type == D3D12_BARRIER_TYPE_TEXTURE)
            {
                const D3D12_TEXTURE_BARRIER& In = Each.pTextureBarriers[Index];

                static_assert(sizeof(In.Subresources) == sizeof(Out.TextureBarrier.Subresources));
                Out.Type = D3D12DDI_BARRIER_TYPE_TEXTURE;
                Out.TextureBarrier.SyncBefore = (D3D12DDI_BARRIER_SYNC)In.SyncBefore;
                Out.TextureBarrier.SyncAfter = (D3D12DDI_BARRIER_SYNC)In.SyncAfter;
                Out.TextureBarrier.AccessBefore = (D3D12DDI_BARRIER_ACCESS)In.AccessBefore;
                Out.TextureBarrier.AccessAfter = (D3D12DDI_BARRIER_ACCESS)In.AccessAfter;
                Out.TextureBarrier.LayoutBefore = (D3D12DDI_BARRIER_LAYOUT)In.LayoutBefore;
                Out.TextureBarrier.LayoutAfter = (D3D12DDI_BARRIER_LAYOUT)In.LayoutAfter;
                Out.TextureBarrier.hResource = RxDriverResource(In.pResource);
                memcpy(&Out.TextureBarrier.Subresources, &In.Subresources, sizeof(In.Subresources));
                Out.TextureBarrier.Flags = (D3D12DDI_TEXTURE_BARRIER_FLAGS_0088)In.Flags;
            }
            else
            {
                const D3D12_BUFFER_BARRIER& In = Each.pBufferBarriers[Index];

                Out.Type = D3D12DDI_BARRIER_TYPE_BUFFER;
                Out.BufferBarrier.SyncBefore = (D3D12DDI_BARRIER_SYNC)In.SyncBefore;
                Out.BufferBarrier.SyncAfter = (D3D12DDI_BARRIER_SYNC)In.SyncAfter;
                Out.BufferBarrier.AccessBefore = (D3D12DDI_BARRIER_ACCESS)In.AccessBefore;
                Out.BufferBarrier.AccessAfter = (D3D12DDI_BARRIER_ACCESS)In.AccessAfter;
                Out.BufferBarrier.hResource = RxDriverResource(In.pResource);
            }
        }
    }

    DriverTable->pfnBarrier(DriverList, Written, Barriers);

    if (Barriers != Stack)
        delete[] Barriers;
}
