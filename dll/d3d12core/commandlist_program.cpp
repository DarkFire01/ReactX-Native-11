/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Programs, work graph dispatch and meta commands on command lists
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "metacommand.hpp"
#include "objects.hpp"

/* Program and dispatch descs share their layout with the driver's */
static_assert(sizeof(D3D12_SET_PROGRAM_DESC) == sizeof(D3D12DDI_SET_PROGRAM_DESC_0108));
static_assert(sizeof(D3D12_DISPATCH_GRAPH_DESC) == sizeof(D3D12DDI_DISPATCH_GRAPH_DESC_0108));
static_assert(sizeof(D3D12_NODE_CPU_INPUT) == sizeof(D3D12DDI_NODE_CPU_INPUT_0108));
static_assert(sizeof(D3D12_MULTI_NODE_CPU_INPUT) == sizeof(D3D12DDI_MULTI_NODE_CPU_INPUT_0108));
static_assert(sizeof(D3D12_PROGRAM_IDENTIFIER) == sizeof(D3D12DDI_PROGRAM_IDENTIFIER_0108));

/* Meta commands run on direct and compute lists, outside any render pass */
constexpr UINT RxMetaCommandWork = RxListDoesCopy | RxListDoesCompute | RxListTopLevel;

/* Parameters are GPU addresses and shader visible descriptors, which are driver values already */
VOID
STDMETHODCALLTYPE
RxCommandList::InitializeMetaCommand(
    _In_ ID3D12MetaCommand* pMetaCommand,
    _In_reads_bytes_opt_(InitializationParametersDataSizeInBytes) const void* pInitializationParametersData,
    _In_ SIZE_T InitializationParametersDataSizeInBytes)
{
    RxMetaCommand* Command;

    if (!Takes(RxMetaCommandWork) || !Records() || !PassAllows(FALSE))
        return;

    Command = RxUnwrap12<RxMetaCommand>(pMetaCommand);
    if (Command == nullptr)
        return;

    DriverTable->pfnInitializeMetaCommand(DriverList, Command->GetDriverHandle(), pInitializationParametersData,
                                          InitializationParametersDataSizeInBytes);
}

VOID
STDMETHODCALLTYPE
RxCommandList::ExecuteMetaCommand(
    _In_ ID3D12MetaCommand* pMetaCommand,
    _In_reads_bytes_opt_(ExecutionParametersDataSizeInBytes) const void* pExecutionParametersData,
    _In_ SIZE_T ExecutionParametersDataSizeInBytes)
{
    RxMetaCommand* Command;

    if (!Takes(RxMetaCommandWork) || !Records() || !PassAllows(FALSE))
        return;

    Command = RxUnwrap12<RxMetaCommand>(pMetaCommand);
    if (Command == nullptr)
        return;

    DriverTable->pfnExecuteMetaCommand(DriverList, Command->GetDriverHandle(), pExecutionParametersData,
                                       ExecutionParametersDataSizeInBytes);
}

/* Without work graphs only generic pipelines are set; anything else is dropped */
VOID
STDMETHODCALLTYPE
RxCommandList::SetProgram(
    _In_ const D3D12_SET_PROGRAM_DESC* pDesc)
{
    if (!Records() || DriverTable->pfnSetProgram == nullptr)
        return;
    if (Device.GetWorkGraphsTier() < D3D12DDI_WORK_GRAPHS_TIER_1_0 && pDesc->Type != D3D12_PROGRAM_TYPE_GENERIC_PIPELINE)
        return;

    DriverTable->pfnSetProgram(Api(), reinterpret_cast<const D3D12DDI_SET_PROGRAM_DESC_0108*>(pDesc));
}

VOID
STDMETHODCALLTYPE
RxCommandList::DispatchGraph(
    _In_ const D3D12_DISPATCH_GRAPH_DESC* pDesc)
{
    if (!Records() || DriverTable->pfnDispatchGraph == nullptr)
        return;

    DriverTable->pfnDispatchGraph(Api(), reinterpret_cast<const D3D12DDI_DISPATCH_GRAPH_DESC_0108*>(pDesc));
}
