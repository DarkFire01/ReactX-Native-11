/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compute shaders: bindings and dispatch
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"

static
RxWarpDevice *
RxWarpGetDevice(
    _In_ D3D10DDI_HDEVICE Device)
{
    return RxWarpObject<RxWarpDevice>(Device);
}

VOID
APIENTRY
RxWarpDdi::CreateComputeShader(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const UINT *Code,
    _In_ D3D10DDI_HSHADER Shader,
    _In_ D3D10DDI_HRTSHADER RuntimeShader)
{
    RxWarpLock Lock;
    RxWarpShader *Object = new (std::nothrow) RxWarpShader();

    UNREFERENCED_PARAMETER(RuntimeShader);
    if (Object == nullptr)
    {
        RxWarpGetDevice(Device)->SetError(E_OUTOFMEMORY);
        return;
    }
    RxWarpBind(Shader, Object);
    if (FAILED(Object->Initialize(Code)))
        RX_WARP_TRACE("WARP: compute shader decode failed\n");
}

VOID
APIENTRY
RxWarpDdi::CsSetShader(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HSHADER Shader)
{
    RxWarpLock Lock;

    RxWarpGetDevice(Device)->Pipeline.Stages[RxWarpStageCompute].Shader = RxWarpObject<RxWarpShader>(Shader);
}

VOID
APIENTRY
RxWarpDdi::CsSetShaderResources(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HSHADERRESOURCEVIEW *Views)
{
    RxWarpLock Lock;
    RX_WARP_STAGE_STATE *State = &RxWarpGetDevice(Device)->Pipeline.Stages[RxWarpStageCompute];

    for (UINT Index = 0; Index < Count && StartSlot + Index < 128; Index++)
        State->Views[StartSlot + Index] = RxWarpObject<RxWarpView>(Views[Index]);
}

VOID
APIENTRY
RxWarpDdi::CsSetSamplers(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HSAMPLER *Samplers)
{
    RxWarpLock Lock;
    RX_WARP_STAGE_STATE *State = &RxWarpGetDevice(Device)->Pipeline.Stages[RxWarpStageCompute];

    for (UINT Index = 0; Index < Count && StartSlot + Index < 16; Index++)
        State->Samplers[StartSlot + Index] = RxWarpObject<RxWarpSampler>(Samplers[Index]);
}

VOID
APIENTRY
RxWarpDdi::CsSetConstantBuffers(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HRESOURCE *Buffers,
    _In_reads_opt_(Count) const UINT *First,
    _In_reads_opt_(Count) const UINT *Constants)
{
    RxWarpLock Lock;
    RX_WARP_STAGE_STATE *State = &RxWarpGetDevice(Device)->Pipeline.Stages[RxWarpStageCompute];

    for (UINT Index = 0; Index < Count && StartSlot + Index < RxWarpConstantBufferSlots; Index++)
    {
        RX_WARP_CONSTANT_BINDING *Binding = &State->ConstantBuffers[StartSlot + Index];

        Binding->Resource = RxWarpObject<RxWarpResource>(Buffers[Index]);
        Binding->First = (First != nullptr) ? First[Index] : 0;
        Binding->Count = (Constants != nullptr) ? Constants[Index] : UINT_MAX;
    }
}

/* Binds UAVs to a stage; an initial count of all ones keeps the view's hidden counter */
VOID
RxWarpBindUavs(
    _Inout_ RX_WARP_STAGE_STATE *State,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_opt_(Count) const D3D11DDI_HUNORDEREDACCESSVIEW *Views,
    _In_reads_opt_(Count) const UINT *InitialCounts)
{
    for (UINT Index = 0; Index < Count && StartSlot + Index < 64; Index++)
    {
        RxWarpView *View = (Views != nullptr) ? RxWarpObject<RxWarpView>(Views[Index]) : nullptr;

        State->Uavs[StartSlot + Index] = View;
        if (View != nullptr && InitialCounts != nullptr && InitialCounts[Index] != UINT_MAX)
            View->Counter = InitialCounts[Index];
    }
}

VOID
APIENTRY
RxWarpDdi::CsSetUnorderedAccessViews(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_(Count) const D3D11DDI_HUNORDEREDACCESSVIEW *Views,
    _In_reads_opt_(Count) const UINT *InitialCounts)
{
    RxWarpLock Lock;

    RxWarpBindUavs(&RxWarpGetDevice(Device)->Pipeline.Stages[RxWarpStageCompute], StartSlot, Count, Views, InitialCounts);
}

/* Runs every thread group; threads go four at a time, and a group barrier waits for all of a group's batches */
VOID
RxWarpDispatch(
    _In_ RxWarpDevice *Device,
    _In_ UINT GroupsX,
    _In_ UINT GroupsY,
    _In_ UINT GroupsZ)
{
    RX_WARP_STAGE_STATE *State = &Device->Pipeline.Stages[RxWarpStageCompute];
    RxWarpShader *Shader = State->Shader;
    RX_WARP_STAGE_BINDINGS Bindings;
    std::vector<RxWarpInvocation> Batches;
    std::vector<BYTE> Shared;
    std::vector<BOOLEAN> Waiting;
    UINT Threads;
    UINT BatchCount;

    if (Shader == nullptr || Device->Predicated())
        return;

    if (Device->Bound[RxWarpStageCompute] != nullptr)
        Bindings = *Device->Bound[RxWarpStageCompute];
    else
        RxWarpBindStage(State, &Bindings);
    Threads = Shader->GroupSize[0] * Shader->GroupSize[1] * Shader->GroupSize[2];
    if (Threads == 0)
        return;
    BatchCount = (Threads + RxWarpLanes - 1) / RxWarpLanes;
    Batches.resize(BatchCount);
    Waiting.resize(BatchCount);
    Shared.resize(Shader->SharedBytes ? Shader->SharedBytes : 16);

    for (UINT Gz = 0; Gz < GroupsZ; Gz++)
    {
        for (UINT Gy = 0; Gy < GroupsY; Gy++)
        {
            for (UINT Gx = 0; Gx < GroupsX; Gx++)
            {
                BOOLEAN Pending;

                std::fill(Shared.begin(), Shared.end(), (BYTE)0);
                for (UINT Batch = 0; Batch < BatchCount; Batch++)
                {
                    RxWarpInvocation &Invocation = Batches[Batch];

                    Invocation.Prepare(Shader);
                    Invocation.Live = 0;
                    Invocation.Helper = 0;
                    Invocation.GroupId[0] = Gx;
                    Invocation.GroupId[1] = Gy;
                    Invocation.GroupId[2] = Gz;
                    Invocation.SharedMemory = Shared.data();
                    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
                    {
                        UINT Flat = Batch * RxWarpLanes + Lane;
                        UINT X;
                        UINT Y;
                        UINT Z;

                        if (Flat >= Threads)
                            break;
                        X = Flat % Shader->GroupSize[0];
                        Y = (Flat / Shader->GroupSize[0]) % Shader->GroupSize[1];
                        Z = Flat / (Shader->GroupSize[0] * Shader->GroupSize[1]);
                        Invocation.Live |= 1u << Lane;
                        Invocation.Flattened[Lane] = Flat;
                        Invocation.ThreadInGroup[Lane][0] = X;
                        Invocation.ThreadInGroup[Lane][1] = Y;
                        Invocation.ThreadInGroup[Lane][2] = Z;
                        Invocation.ThreadId[Lane][0] = Gx * Shader->GroupSize[0] + X;
                        Invocation.ThreadId[Lane][1] = Gy * Shader->GroupSize[1] + Y;
                        Invocation.ThreadId[Lane][2] = Gz * Shader->GroupSize[2] + Z;
                    }
                    Waiting[Batch] = Invocation.Resume(Shader, &Bindings, TRUE);
                }

                /* Everyone reached the barrier: let them all go on to the next one */
                do
                {
                    Pending = FALSE;
                    for (UINT Batch = 0; Batch < BatchCount; Batch++)
                    {
                        if (!Waiting[Batch])
                            continue;
                        Waiting[Batch] = Batches[Batch].Resume(Shader, &Bindings, FALSE);
                        Pending |= Waiting[Batch];
                    }
                } while (Pending);

                Device->Counters[RxWarpCountCsInvocations] += Threads;
            }
        }
    }
}

VOID
APIENTRY
RxWarpDdi::Dispatch(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT GroupsX,
    _In_ UINT GroupsY,
    _In_ UINT GroupsZ)
{
    RxWarpLock Lock;

    RxWarpDispatch(RxWarpGetDevice(Device), GroupsX, GroupsY, GroupsZ);
}

VOID
APIENTRY
RxWarpDdi::DispatchIndirect(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Buffer,
    _In_ UINT Offset)
{
    RxWarpLock Lock;
    RxWarpResource *Arguments = RxWarpObject<RxWarpResource>(Buffer);
    UINT Groups[3];

    if (Arguments == nullptr || Arguments->Memory == nullptr || (SIZE_T)Offset + sizeof(Groups) > Arguments->MemorySize)
        return;
    memcpy(Groups, Arguments->Memory + Offset, sizeof(Groups));
    RxWarpDispatch(RxWarpGetDevice(Device), Groups[0], Groups[1], Groups[2]);
}

VOID
APIENTRY
RxWarpDdi::CopyStructureCount(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Destination,
    _In_ UINT Offset,
    _In_ D3D11DDI_HUNORDEREDACCESSVIEW Source)
{
    RxWarpLock Lock;
    RxWarpResource *Target = RxWarpObject<RxWarpResource>(Destination);
    RxWarpView *View = RxWarpObject<RxWarpView>(Source);

    if (RxWarpGetDevice(Device)->Predicated())
        return;
    if (Target == nullptr || View == nullptr || Target->Memory == nullptr || (SIZE_T)Offset + 4 > Target->MemorySize)
        return;
    memcpy(Target->Memory + Offset, &View->Counter, sizeof(View->Counter));
}
