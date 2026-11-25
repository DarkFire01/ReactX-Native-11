/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Acceleration structures and ray dispatch
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "stateobject.hpp"

/* The API and driver forms share one layout, so descs pass through as they are */
static_assert(sizeof(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS) ==
              sizeof(D3D12DDI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_0054));
static_assert(sizeof(D3D12_RAYTRACING_GEOMETRY_DESC) == sizeof(D3D12DDI_RAYTRACING_GEOMETRY_DESC_0054));
static_assert(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) == sizeof(D3D12DDI_RAYTRACING_INSTANCE_DESC_0054));
static_assert(sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO) ==
              sizeof(D3D12DDI_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_0054));
static_assert(sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC) ==
              sizeof(D3D12DDI_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC_0054));
static_assert(sizeof(D3D12_DISPATCH_RAYS_DESC) == sizeof(D3D12DDIARG_DISPATCH_RAYS_0054));

VOID
STDMETHODCALLTYPE
RxDevice12::GetRaytracingAccelerationStructurePrebuildInfo(
    _In_ const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* pDesc,
    _Out_ D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO* pInfo)
{
    if (CoreFunctions.pfnGetRaytracingAccelerationStructurePrebuildInfo == nullptr)
    {
        ZeroMemory(pInfo, sizeof(*pInfo));
        return;
    }

    CoreFunctions.pfnGetRaytracingAccelerationStructurePrebuildInfo(
        DriverDevice, reinterpret_cast<const D3D12DDI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_0054*>(pDesc),
        reinterpret_cast<D3D12DDI_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_0054*>(pInfo));
}

/* Acceleration structure work needs a direct or compute list, outside any render pass */
constexpr UINT RxAccelerationWork = RxListDoesCopy | RxListDoesCompute | RxListTopLevel;

VOID
STDMETHODCALLTYPE
RxCommandList::BuildRaytracingAccelerationStructure(
    _In_ const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* pDesc,
    _In_ UINT NumPostbuildInfoDescs,
    _In_reads_opt_(NumPostbuildInfoDescs) const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* pPostbuildInfoDescs)
{
    D3D12DDIARG_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_0054 Args;

    if (!Takes(RxAccelerationWork) || !Records() || !PassAllows(FALSE))
        return;

    Args.DestAccelerationStructureData = pDesc->DestAccelerationStructureData;
    CopyMemory(&Args.Inputs, &pDesc->Inputs, sizeof(Args.Inputs));
    Args.SourceAccelerationStructureData = pDesc->SourceAccelerationStructureData;
    Args.ScratchAccelerationStructureData = pDesc->ScratchAccelerationStructureData;
    Args.NumPostbuildInfoDescs = NumPostbuildInfoDescs;
    Args.pPostbuildInfoDescs =
        reinterpret_cast<const D3D12DDI_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC_0054*>(pPostbuildInfoDescs);
    DriverTable->pfnBuildRaytracingAccelerationStructure(DriverList, &Args);
}

VOID
STDMETHODCALLTYPE
RxCommandList::EmitRaytracingAccelerationStructurePostbuildInfo(
    _In_ const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* pDesc,
    _In_ UINT NumSourceAccelerationStructures,
    _In_reads_(NumSourceAccelerationStructures) const D3D12_GPU_VIRTUAL_ADDRESS* pSourceAccelerationStructureData)
{
    D3D12DDIARG_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_0054 Args;

    if (!Takes(RxAccelerationWork) || !Records() || !PassAllows(FALSE))
        return;

    Args.Desc.DestBuffer = pDesc->DestBuffer;
    Args.Desc.InfoType = (D3D12DDI_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_TYPE)pDesc->InfoType;
    Args.NumSourceAccelerationStructures = NumSourceAccelerationStructures;
    Args.pSourceAccelerationStructureData = pSourceAccelerationStructureData;
    DriverTable->pfnEmitRaytracingAccelerationStructurePostbuildInfo(DriverList, &Args);
}

VOID
STDMETHODCALLTYPE
RxCommandList::CopyRaytracingAccelerationStructure(
    _In_ D3D12_GPU_VIRTUAL_ADDRESS DestAccelerationStructureData,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS SourceAccelerationStructureData,
    _In_ D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE Mode)
{
    D3D12DDIARG_COPY_RAYTRACING_ACCELERATION_STRUCTURE_0054 Args;

    if (!Takes(RxAccelerationWork) || !Records() || !PassAllows(FALSE))
        return;

    Args.DestAccelerationStructureData = DestAccelerationStructureData;
    Args.SourceAccelerationStructureData = SourceAccelerationStructureData;
    Args.Mode = (D3D12DDI_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE)Mode;
    DriverTable->pfnCopyRaytracingAccelerationStructure(DriverList, &Args);
}

/* The state object replaces whatever pipeline the driver had bound for dispatches */
VOID
STDMETHODCALLTYPE
RxCommandList::SetPipelineState1(
    _In_ ID3D12StateObject* pStateObject)
{
    RxStateObject* Object = RxUnwrap12<RxStateObject>(pStateObject);

    if (!Takes(RxListDoesCompute) || !Records() || Object == nullptr)
        return;

    DriverTable->pfnSetPipelineState1(Api(), Object->Api());
}

VOID
STDMETHODCALLTYPE
RxCommandList::DispatchRays(
    _In_ const D3D12_DISPATCH_RAYS_DESC* pDesc)
{
    D3D12DDIARG_DISPATCH_RAYS_0054 Args;

    if (!Takes(RxListDoesCompute) || !Records())
        return;

    CopyMemory(&Args, pDesc, sizeof(Args));
    DriverTable->pfnDispatchRays(Api(), &Args);
}
