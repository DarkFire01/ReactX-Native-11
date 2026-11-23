/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 runtime traffic to its user mode driver and the kernel, printed for comparison
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * d3d12trace [-runtime system|reactx|<dir>] [-threads] [-ddi] [-nokernel] [scenario ...]
 *
 * The core's imports of GetProcAddress and the D3DKMT thunks are pointed at wrappers in this process
 * only. The wrapper for the driver's OpenAdapter12 wraps the adapter table, every DDI table the driver
 * fills and the callback tables handed to it, so each call prints with its arguments. Handles print as
 * ids in the order they first appear: r for runtime handles, d for driver handles, k for kernel handles.
 * With -ddi the driver's callbacks and what they call are left out. With -nokernel the D3DKMT thunks are left
 * out, since the system core reaches the kernel through calls the tracer does not see.
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>
#include <d3d12.h>
#include <d3dkmthk.h>
#include <d3d12umddi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "ddi.hpp"
#include "internalshaders.h"

using Microsoft::WRL::ComPtr;

/* The tables the tracer wraps; command list tables can come several to a type */
enum TrFamily
{
    TrFamilyAdapter,
    TrFamilyAdapterCallback,
    TrFamilyDevice,
    TrFamilyList0,
    TrFamilyList1,
    TrFamilyList2,
    TrFamilyList3,
    TrFamilyQueue,
    TrFamilyExtended,
    TrFamilyKernelCallback,
    TrFamilyCoreCallback,
    TrFamilyKernel,
    TrFamilyCount,
};

enum TrKind
{
    TrKindRuntime,
    TrKindDriver,
    TrKindKernel,
    TrKindCount,
};

static constexpr UINT TrSlotLimit = 512;

struct TrState
{
    DWORD MainThread;
    BOOLEAN AllThreads;
    BOOLEAN DdiOnly;
    BOOLEAN NoKernel;
    BOOLEAN LineOpen;
    UINT Depth;
    VOID* Originals[TrFamilyCount][TrSlotLimit];
    PFND3D12DDI_OPENADAPTER OpenAdapter;
    FARPROC (WINAPI* GetProcAddress)(HMODULE, LPCSTR);
    std::map<ULONG_PTR, UINT> Ids[TrKindCount];
    std::map<ULONG_PTR, ULONG_PTR> Partners;
    UINT NextId[TrKindCount];
    SRWLOCK IdLock;
};

static TrState Tr;

/* Set while a call runs that is not printed, so it uses up no ids */
static thread_local BOOLEAN TrQuiet;

/* How deep the driver is in runtime callbacks on this thread */
static thread_local UINT TrCallbackDepth;

/* The DDI call whose arguments are being printed */
static thread_local const CHAR* TrCallName;

static
VOID
TrAppend(
    _Inout_ std::string* Text,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[512];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf_s(Buffer, sizeof(Buffer), _TRUNCATE, Format, Arguments);
    va_end(Arguments);
    Text->append(Buffer);
}

/* A handle as a stable id; zero stays zero */
static
std::string
TrId(
    _In_ TrKind Kind,
    _In_ ULONG_PTR Value)
{
    static const CHAR Prefix[] = { 'r', 'd', 'k' };
    std::string Text;
    UINT Id = 0;

    if (Value == 0)
        return "0";

    AcquireSRWLockExclusive(&Tr.IdLock);
    auto Found = Tr.Ids[Kind].find(Value);
    if (Found != Tr.Ids[Kind].end())
    {
        Id = Found->second;
    }
    else if (!TrQuiet)
    {
        Tr.NextId[Kind] += 1;
        Id = Tr.NextId[Kind];
        Tr.Ids[Kind].emplace(Value, Id);
    }
    ReleaseSRWLockExclusive(&Tr.IdLock);

    TrAppend(&Text, "%c%u", Prefix[Kind], Id);
    return Text;
}

static
UINT
TrHash(
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    UINT Hash = 2166136261u;

    if (Data == nullptr)
        return 0;
    for (SIZE_T Index = 0; Index < Size; Index++)
        Hash = (Hash ^ Bytes[Index]) * 16777619u;
    return Hash;
}

static
VOID
TrAppendBytes(
    _Inout_ std::string* Text,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);

    for (SIZE_T Index = 0; Index < Size && Index < 96; Index++)
        TrAppend(Text, "%02X", Bytes[Index]);
    if (Size > 96)
        TrAppend(Text, "..#%08X", TrHash(Data, Size));
}

static
BOOLEAN
TrShouldLog(VOID)
{
    if (Tr.DdiOnly && TrCallbackDepth != 0)
        return FALSE;
    return Tr.AllThreads || GetCurrentThreadId() == Tr.MainThread;
}

static
VOID
TrCloseLine(VOID)
{
    if (Tr.LineOpen)
    {
        fputc('\n', stdout);
        Tr.LineOpen = FALSE;
    }
}

static
VOID
TrPhase(
    _In_z_ PCSTR Name)
{
    TrCloseLine();
    printf("== %s\n", Name);
}

static
VOID
TrBegin(
    _In_ const std::string& Text)
{
    if (GetCurrentThreadId() != Tr.MainThread)
    {
        printf("[worker] %s\n", Text.c_str());
        return;
    }
    TrCloseLine();
    printf("%*s%s", static_cast<int>(Tr.Depth * 2), "", Text.c_str());
    Tr.LineOpen = TRUE;
    Tr.Depth += 1;
}

static
VOID
TrEnd(
    _In_ const std::string& Text)
{
    if (GetCurrentThreadId() != Tr.MainThread)
        return;
    Tr.Depth -= 1;
    if (Tr.LineOpen)
        printf("%s%s\n", Text.empty() ? "" : " = ", Text.c_str());
    else if (!Text.empty())
        printf("%*s= %s\n", static_cast<int>(Tr.Depth * 2), "", Text.c_str());
    Tr.LineOpen = FALSE;
}

/* Argument printing, chosen by the argument's type */

template <typename T, typename = void>
struct TrIsDriverHandle : std::false_type
{
};

template <typename T>
struct TrIsDriverHandle<T, std::void_t<decltype(std::declval<T>().pDrvPrivate)>> : std::true_type
{
};

template <typename T, typename = void>
struct TrIsRuntimeHandle : std::false_type
{
};

template <typename T>
struct TrIsRuntimeHandle<T, std::void_t<decltype(std::declval<T>().handle)>> : std::true_type
{
};

template <typename T, typename = void>
struct TrIsDescriptor : std::false_type
{
};

template <typename T>
struct TrIsDescriptor<T, std::void_t<decltype(std::declval<T>().ptr)>> : std::true_type
{
};

template <typename T, typename = void>
struct TrIsComplete : std::false_type
{
};

template <typename T>
struct TrIsComplete<T, std::void_t<decltype(sizeof(T))>> : std::true_type
{
};

/* What a pointed to structure holds; specific structures print their own way below */
static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const VOID* Pointer,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{");
    TrAppendBytes(Text, Pointer, Size);
    TrAppend(Text, "}");
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_GETCAPS* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{type %u info %s size %u}", Args->Type, Args->pInfo != nullptr ? "p" : "0", Args->DataSize);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATEDEVICE_0003* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{rt %s interface %u version %08X drv %s flags %08X}", TrId(TrKindRuntime,
                                                                             (ULONG_PTR)Args->hRTDevice.handle).c_str(),
             Args->Interface, Args->Version, TrId(TrKindDriver, (ULONG_PTR)Args->hDrvDevice.pDrvPrivate).c_str(),
             Args->Flags);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATE_COMMAND_LIST_0040* Args,
    _In_ SIZE_T Size)
{
    /* Sizing passes no ID or flags; the system core leaves those fields unset */
    if (TrCallName != nullptr && strcmp(TrCallName, "CalcPrivateCommandListSize") == 0)
    {
        TrAppend(Text, "{type %u queue %08X node %u}", Args->Type, Args->QueueFlags, Args->NodeMask);
        return;
    }
    TrAppend(Text, "{type %u queue %08X id %llu flags %08X node %u}", Args->Type, Args->QueueFlags, Args->ID,
             Args->CommandListFlags, Args->NodeMask);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATECOMMANDQUEUE_0050* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{flags %08X node %u creation %08X group %s}", Args->QueueFlags, Args->NodeMask,
             Args->QueueCreationFlags, TrId(TrKindDriver, (ULONG_PTR)Args->SchedulingGroup.pDrvPrivate).c_str());
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0100* Args,
    _In_ SIZE_T Size)
{
    const D3D12DDI_ROOT_SIGNATURE_0100* Root = Args->pRootSignature_1_2;

    TrAppend(Text, "{version %u node %u", Args->Version, Args->NodeMask);
    if (Root == nullptr)
    {
        TrAppend(Text, " 0}");
        return;
    }

    TrAppend(Text, " flags %08X params %u [", Root->Flags, Root->NumParameters);
    for (UINT Index = 0; Index < Root->NumParameters; Index++)
    {
        const D3D12DDI_ROOT_PARAMETER_0013& Parameter = Root->pRootParameters[Index];

        TrAppend(Text, "%s(t%u v%u", Index != 0 ? " " : "", Parameter.ParameterType, Parameter.ShaderVisibility);
        switch (Parameter.ParameterType)
        {
            case D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                for (UINT Range = 0; Range < Parameter.DescriptorTable.NumDescriptorRanges; Range++)
                {
                    const D3D12DDI_DESCRIPTOR_RANGE_0013& Each = Parameter.DescriptorTable.pDescriptorRanges[Range];

                    TrAppend(Text, " r%u:%u:%u:%u:%X:%u", Each.RangeType, Each.NumDescriptors, Each.BaseShaderRegister,
                             Each.RegisterSpace, Each.Flags, Each.OffsetInDescriptorsFromTableStart);
                }
                break;
            case D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                TrAppend(Text, " c%u:%u:%u", Parameter.Constants.ShaderRegister, Parameter.Constants.RegisterSpace,
                         Parameter.Constants.Num32BitValues);
                break;
            default:
                TrAppend(Text, " d%u:%u:%X", Parameter.Descriptor.ShaderRegister, Parameter.Descriptor.RegisterSpace,
                         Parameter.Descriptor.Flags);
                break;
        }
        TrAppend(Text, ")");
    }

    TrAppend(Text, "] samplers %u [", Root->NumStaticSamplers);
    for (UINT Index = 0; Index < Root->NumStaticSamplers; Index++)
    {
        TrAppend(Text, "%s", Index != 0 ? " " : "");
        TrAppendBytes(Text, &Root->pStaticSamplers[Index], sizeof(Root->pStaticSamplers[Index]));
    }
    TrAppend(Text, "]}");
}

static
VOID
TrDescribeSignature(
    _Inout_ std::string* Text,
    _In_reads_(Count) const D3D12DDIARG_SIGNATURE_ENTRY_0012* Entries,
    _In_ UINT Count)
{
    TrAppend(Text, "[");
    for (UINT Index = 0; Index < Count; Index++)
    {
        const D3D12DDIARG_SIGNATURE_ENTRY_0012& Entry = Entries[Index];

        TrAppend(Text, "%s%u:%u:%X:%u:%u:%u", Index != 0 ? " " : "", Entry.SystemValue, Entry.Register, Entry.Mask,
                 Entry.Stream, Entry.RegisterComponentType, Entry.MinPrecision);
    }
    TrAppend(Text, "]");
}

/* Shader code as the driver gets it: the token stream or DXIL program, sized by its second word */
static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATE_SHADER_0026* Args,
    _In_ SIZE_T Size)
{
    const D3D12DDIARG_STAGE_IO_SIGNATURES* Io = Args->IOSignatures.Standard;

    TrAppend(Text, "{root %s", TrId(TrKindDriver, (ULONG_PTR)Args->hRootSignature.pDrvPrivate).c_str());
    if (Args->pShaderCode != nullptr)
        TrAppend(Text, " code %08X %u #%08X", Args->pShaderCode[0], Args->pShaderCode[1],
                 TrHash(Args->pShaderCode, (SIZE_T)Args->pShaderCode[1] * sizeof(UINT)));
    else
        TrAppend(Text, " code 0");
    if (Io != nullptr)
    {
        TrAppend(Text, " in ");
        TrDescribeSignature(Text, Io->pInputSignature, Io->NumInputSignatureEntries);
        TrAppend(Text, " out ");
        TrDescribeSignature(Text, Io->pOutputSignature, Io->NumOutputSignatureEntries);
    }
    else
    {
        TrAppend(Text, " io 0");
    }
    TrAppend(Text, " flags %08X library %s %u hash ", Args->Flags,
             TrId(TrKindDriver, (ULONG_PTR)Args->LibraryReference.hLibrary.pDrvPrivate).c_str(),
             Args->LibraryReference.PipelineIndex);
    TrAppendBytes(Text, &Args->ShaderCodeHash, sizeof(Args->ShaderCodeHash));
    TrAppend(Text, "}");
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATE_PIPELINE_STATE_0119* Newest,
    _In_ SIZE_T Size)
{
    /* The core drives DDI build 118, which passes the shorter 0099 form */
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0099* Args =
        reinterpret_cast<const D3D12DDIARG_CREATE_PIPELINE_STATE_0099*>(Newest);
    const VOID* const Handles[] = {
        Args->hComputeShader.pDrvPrivate, Args->hVertexShader.pDrvPrivate, Args->hPixelShader.pDrvPrivate,
        Args->hDomainShader.pDrvPrivate, Args->hHullShader.pDrvPrivate, Args->hGeometryShader.pDrvPrivate,
        Args->hMeshShader.pDrvPrivate, Args->hAmplificationShader.pDrvPrivate,
    };

    TrAppend(Text, "{shaders");
    for (const VOID* Handle : Handles)
        TrAppend(Text, " %s", TrId(TrKindDriver, (ULONG_PTR)Handle).c_str());
    TrAppend(Text, " root %s blend %s mask %08X raster %s depth %s layout %s cut %u topology %u rts %u [",
             TrId(TrKindDriver, (ULONG_PTR)Args->hRootSignature.pDrvPrivate).c_str(),
             TrId(TrKindDriver, (ULONG_PTR)Args->hBlendState.pDrvPrivate).c_str(), Args->SampleMask,
             TrId(TrKindDriver, (ULONG_PTR)Args->hRasterizerState.pDrvPrivate).c_str(),
             TrId(TrKindDriver, (ULONG_PTR)Args->hDepthStencilState.pDrvPrivate).c_str(),
             TrId(TrKindDriver, (ULONG_PTR)Args->hElementLayout.pDrvPrivate).c_str(), Args->IBStripCutValue,
             Args->PrimitiveTopologyType, Args->NumRenderTargets);
    for (UINT Index = 0; Index < 8; Index++)
        TrAppend(Text, "%s%u", Index != 0 ? " " : "", Args->RTVFormats[Index]);
    TrAppend(Text, "] dsv %u samples %u:%u node %u library %s %u views %u %s %08X flags %08X}", Args->DSVFormat,
             Args->SampleDesc.Count, Args->SampleDesc.Quality, Args->NodeMask,
             TrId(TrKindDriver, (ULONG_PTR)Args->LibraryReference.hLibrary.pDrvPrivate).c_str(),
             Args->LibraryReference.PipelineIndex, Args->ViewInstancingDesc.ViewInstanceCount,
             Args->ViewInstancingDesc.pViewInstanceLocations != nullptr ? "p" : "0", Args->ViewInstancingDesc.Flags,
             Args->Flags);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_MAKERESIDENT_0001* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{adapters %u queue %s objects %u %s [", Args->NumAdapters, Args->pRTPagingQueue != nullptr ? "p" : "0",
             Args->NumObjects, Args->pObjects != nullptr ? "p" : "0");
    for (UINT Index = 0; Index < Args->NumObjects; Index++)
        TrAppend(Text, "%s%s:%u", Index != 0 ? " " : "", TrId(TrKindDriver, (ULONG_PTR)Args->pObjects[Index].Handle).c_str(),
                 Args->pObjects[Index].Type);
    TrAppend(Text, "] flags %08X fence %s}", Args->Flags.Value, Args->pPagingFenceValue != nullptr ? "p" : "0");
}

static
std::string
TrDriverId(
    _In_opt_ const VOID* Handle)
{
    return TrId(TrKindDriver, (ULONG_PTR)Handle);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATEELEMENTLAYOUT_0010* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{count %u [", Args->NumElements);
    for (UINT Index = 0; Index < Args->NumElements; Index++)
    {
        const D3D12DDIARG_INPUT_ELEMENT_DESC& Element = Args->pVertexElements[Index];

        TrAppend(Text, "%s%u:%u:%u:%u:%u:%u", Index != 0 ? " " : "", Element.InputSlot, Element.AlignedByteOffset,
                 Element.Format, Element.InputSlotClass, Element.InstanceDataStepRate, Element.InputRegister);
    }
    TrAppend(Text, "] library %s %u}", TrDriverId(Args->LibraryReference.hLibrary.pDrvPrivate).c_str(),
             Args->LibraryReference.PipelineIndex);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{type %u count %u flags %08X node %u}", Args->Type, Args->NumDescriptors, Args->Flags, Args->NodeMask);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_RESETCOMMANDLIST_0040* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{recorder %s id %llu flags %08X}", TrDriverId(Args->hDrvCommandRecorder.pDrvPrivate).c_str(), Args->ID,
             Args->CommandListFlags);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATEHEAP_0001* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{size %llu align %llu pool %u page %u flags %08X nodes %u:%u}", Args->ByteSize, Args->Alignment,
             Args->MemoryPool, Args->CPUPageProperty, Args->Flags, Args->CreationNodeMask, Args->VisibleNodeMask);
}

/* Every field but the padding after the resource type, which the system core leaves unset */
static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATERESOURCE_0111* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{reuse %s+%llu type %u width %llu height %u depth %u mips %u format %u samples %u:%u layout %u",
             TrDriverId(Args->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate).c_str(),
             Args->ReuseBufferGPUVA.BaseAddress.UMD.Offset, Args->ResourceType, Args->Width, Args->Height,
             Args->DepthOrArraySize, Args->MipLevels, Args->Format, Args->SampleDesc.Count, Args->SampleDesc.Quality,
             Args->Layout);
    TrAppend(Text, " flags %08X barrier %u", Args->Flags, Args->InitialBarrierLayout);
    if (Args->pRowMajorLayout != nullptr)
        TrAppend(Text, " rowmajor %u:%u", Args->pRowMajorLayout->RowPitch, Args->pRowMajorLayout->SlicePitch);
    else
        TrAppend(Text, " rowmajor 0");
    TrAppend(Text, " region %u:%u:%u castable %u [", Args->SamplerFeedbackMipRegion.Width,
             Args->SamplerFeedbackMipRegion.Height, Args->SamplerFeedbackMipRegion.Depth, Args->NumCastableFormats);
    for (UINT Index = 0; Args->pCastableFormats != nullptr && Index < Args->NumCastableFormats; Index++)
        TrAppend(Text, "%s%u", Index != 0 ? " " : "", Args->pCastableFormats[Index]);
    TrAppend(Text, "] va %llX guid ", Args->CreateAtVirtualAddress);
    TrAppendBytes(Text, &Args->LayoutGuid, sizeof(Args->LayoutGuid));
    TrAppend(Text, "}");
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDI_CLEAR_VALUES* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{format %u ", Args->Format);
    TrAppendBytes(Text, Args->Color, sizeof(Args->Color));
    TrAppend(Text, "}");
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_BUFFER_PLACEMENT* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{%s+%llu}", TrDriverId(Args->BaseAddress.UMD.hResource.pDrvPrivate).c_str(),
             Args->BaseAddress.UMD.Offset);
}

/* A view desc: the resource, format and dimension, then the dimension's own fields as bytes */
static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{res %s format %u dim %u ", TrDriverId(Args->hDrvResource.pDrvPrivate).c_str(), Args->Format,
             Args->ResourceDimension);
    TrAppendBytes(Text, &Args->Buffer, sizeof(*Args) - offsetof(D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002, Buffer));
    TrAppend(Text, "}");
}

/* Where a texture copy reads or writes: a subresource index or a footprint */
static
VOID
TrDescribePlaced(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_PLACED_RESOURCE& Placed)
{
    TrAppend(Text, "{layout %u", Placed.Layout);
    if (Placed.pLayout == nullptr)
    {
        TrAppend(Text, " 0}");
    }
    else if (Placed.Layout == D3D12DDI_RL_SELECT_SUBRESOURCE)
    {
        TrAppend(Text, " sub %u}", *static_cast<const UINT*>(Placed.pLayout));
    }
    else
    {
        const D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT* Pitched =
            static_cast<const D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT*>(Placed.pLayout);

        TrAppend(Text, " format %u size %u:%u:%u pitch %u:%u}", Pitched->Format, Pitched->PhysicalWidth,
                 Pitched->PhysicalHeight, Pitched->PhysicalDepth, Pitched->Pitch, Pitched->SlicePitch);
    }
}

/* An application object handle; the driver object behind its bypass header stands for it */
template <typename T>
struct TrIsApiHandle : std::bool_constant<std::is_same_v<T, D3D12DDI_API_HCOMMANDLIST> ||
                                          std::is_same_v<T, D3D12DDI_API_HPIPELINESTATE> ||
                                          std::is_same_v<T, D3D12DDI_API_HSTATEOBJECT> ||
                                          std::is_same_v<T, D3D12DDI_API_HROOTSIGNATURE>>
{
};

static
VOID
TrDescribeApi(
    _Inout_ std::string* Text,
    _In_opt_ const VOID* Handle)
{
    const D3D12DDI_RUNTIME_BYPASS_HEADER* Header = static_cast<const D3D12DDI_RUNTIME_BYPASS_HEADER*>(Handle);

    if (Header == nullptr)
        Text->append("0");
    else
        Text->append("@" + TrDriverId(Header->pDriverObject));
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_QUERYADAPTERINFO* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{adapter %s type %u size %u}", TrId(TrKindKernel, Args->hAdapter).c_str(), Args->Type,
             Args->PrivateDriverDataSize);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_ESCAPE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{adapter %s device %s type %u flags %08X size %u data #%08X context %s}",
             TrId(TrKindKernel, Args->hAdapter).c_str(), TrId(TrKindKernel, Args->hDevice).c_str(), Args->Type,
             Args->Flags.Value, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                    Args->PrivateDriverDataSize),
             TrId(TrKindKernel, Args->hContext).c_str());
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_CREATEDEVICE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{adapter %s flags %08X}", TrId(TrKindKernel, Args->hAdapter).c_str(), *(const UINT*)&Args->Flags);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_CREATECONTEXTVIRTUAL* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{device %s node %u affinity %u flags %08X size %u data #%08X hint %u}",
             TrId(TrKindKernel, Args->hDevice).c_str(), Args->NodeOrdinal, Args->EngineAffinity, Args->Flags.Value,
             Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData, Args->PrivateDriverDataSize),
             Args->ClientHint);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_CREATECONTEXT* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{node %u affinity %u flags %08X size %u data #%08X}", Args->NodeOrdinal, Args->EngineAffinity,
             Args->Flags.Value, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                    Args->PrivateDriverDataSize));
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_CREATEHWQUEUE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{context %s flags %08X size %u data #%08X}", TrId(TrKindKernel, (ULONG_PTR)Args->hHwContext).c_str(),
             Args->Flags.Value, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                    Args->PrivateDriverDataSize));
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_CREATEHWQUEUE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{context %s flags %08X size %u data #%08X}", TrId(TrKindKernel, Args->hHwContext).c_str(),
             Args->Flags.Value, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                    Args->PrivateDriverDataSize));
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_CREATEPAGINGQUEUE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{device %s priority %d node %u}", TrId(TrKindKernel, Args->hDevice).c_str(), Args->Priority,
             Args->PhysicalAdapterIndex);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_ALLOCATE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{res %s count %u size %u data #%08X", TrId(TrKindRuntime, (ULONG_PTR)Args->hResource).c_str(),
             Args->NumAllocations, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                       Args->PrivateDriverDataSize));
    for (UINT Index = 0; Index < Args->NumAllocations && Index < 4; Index++)
    {
        const D3DDDI_ALLOCATIONINFO* Info = &Args->pAllocationInfo[Index];

        TrAppend(Text, " [%u #%08X src %u flags %08X]", Info->PrivateDriverDataSize,
                 TrHash(Info->pPrivateDriverData, Info->PrivateDriverDataSize), Info->VidPnSourceId, Info->Flags.Value);
    }
    TrAppend(Text, "}");
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDI_MAKERESIDENT* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{queue %s count %u flags %08X}", TrId(TrKindKernel, Args->hPagingQueue).c_str(), Args->NumAllocations,
             Args->Flags.Value);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_SUBMITCOMMAND* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{length %u size %u flags %08X contexts %u}", Args->CommandLength, Args->PrivateDriverDataSize,
             Args->Flags.Value, Args->BroadcastContextCount);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_CREATESYNCHRONIZATIONOBJECT2* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{type %u flags %08X}", Args->Info.Type, Args->Info.Flags.Value);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_CREATESYNCHRONIZATIONOBJECT2* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{device %s type %u flags %08X}", TrId(TrKindKernel, Args->hDevice).c_str(), Args->Info.Type,
             Args->Info.Flags.Value);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_QUERYADAPTERINFO* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{size %u}", Args->PrivateDriverDataSize);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_QUERYADAPTERINFO2* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{type %u size %u}", Args->QueryType, Args->PrivateDriverDataSize);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_ESCAPE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{device %s flags %08X size %u data #%08X context %s}",
             TrId(TrKindRuntime, (ULONG_PTR)Args->hDevice).c_str(), Args->Flags.Value, Args->PrivateDriverDataSize,
             TrHash(Args->pPrivateDriverData, Args->PrivateDriverDataSize),
             TrId(TrKindRuntime, (ULONG_PTR)Args->hContext).c_str());
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_CREATESYNCHRONIZATIONOBJECT* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{type %u}", Args->Info.Type);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_CREATECONTEXTVIRTUAL* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{node %u affinity %u flags %08X size %u data #%08X}", Args->NodeOrdinal, Args->EngineAffinity,
             Args->Flags.Value, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                    Args->PrivateDriverDataSize));
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_LOCK2* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{alloc %s flags %08X}", TrId(TrKindKernel, Args->hAllocation).c_str(), Args->Flags.Value);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_DEALLOCATE2* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{res %s count %u flags %08X}", TrId(TrKindRuntime, (ULONG_PTR)Args->hResource).c_str(),
             Args->NumAllocations, Args->Flags.Value);
}

template <typename T>
static
VOID
TrArg(
    _Inout_ std::string* Text,
    _In_ const T& Value)
{
    if (!Text->empty() && Text->back() != '(')
        Text->append(" ");

    if constexpr (std::is_pointer_v<T>)
    {
        using Pointee = std::remove_cv_t<std::remove_pointer_t<T>>;

        if (Value == nullptr)
            Text->append("0");
        else if constexpr (std::is_void_v<Pointee>)
            Text->append(TrId(TrKindRuntime, (ULONG_PTR)Value));
        else if constexpr (std::is_function_v<Pointee> || !TrIsComplete<Pointee>::value || std::is_arithmetic_v<Pointee> ||
                           std::is_enum_v<Pointee>)
            Text->append("p");
        else
            TrDescribe(Text, Value, sizeof(Pointee));
    }
    else if constexpr (TrIsApiHandle<T>::value)
    {
        TrDescribeApi(Text, Value.pDrvPrivate);
    }
    else if constexpr (std::is_same_v<T, D3D12DDIARG_PLACED_RESOURCE>)
    {
        TrDescribePlaced(Text, Value);
    }
    else if constexpr (std::is_same_v<T, D3D12DDIARG_BUFFER_PLACEMENT>)
    {
        TrDescribe(Text, &Value, sizeof(Value));
    }
    else if constexpr (TrIsDriverHandle<T>::value)
    {
        Text->append(TrId(TrKindDriver, (ULONG_PTR)Value.pDrvPrivate));
    }
    else if constexpr (TrIsRuntimeHandle<T>::value)
    {
        Text->append(TrId(TrKindRuntime, (ULONG_PTR)Value.handle));
    }
    else if constexpr (TrIsDescriptor<T>::value)
    {
        Text->append(TrId(TrKindDriver, (ULONG_PTR)Value.ptr));
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        FLOAT Single = (FLOAT)Value;
        UINT Bits;

        memcpy(&Bits, &Single, sizeof(Bits));
        TrAppend(Text, "f%08X", Bits);
    }
    else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)
    {
        TrAppend(Text, "%llu", (UINT64)Value);
    }
    else
    {
        TrAppend(Text, "<");
        TrAppendBytes(Text, &Value, sizeof(Value));
        TrAppend(Text, ">");
    }
}

template <typename T>
static
std::string
TrResult(
    _In_ const T& Value)
{
    std::string Text;

    if constexpr (std::is_same_v<T, HRESULT> || std::is_same_v<T, NTSTATUS>)
        TrAppend(&Text, "%08X", (UINT)Value);
    else
        TrArg(&Text, Value);
    return Text;
}

/* Arrays sized by another argument: every element, or 0 or p alone when the count is zero */
template <typename T, typename F>
static
VOID
TrArray(
    _Inout_ std::string* Text,
    _In_ UINT Count,
    _In_reads_opt_(Count) const T* Items,
    _In_ F Each)
{
    Text->append(" ");
    if (Items == nullptr)
    {
        Text->append("0");
        return;
    }
    if (Count == 0)
    {
        Text->append("p");
        return;
    }

    Text->append("[");
    for (UINT Index = 0; Index < Count; Index++)
    {
        if (Index != 0)
            Text->append(" ");
        Each(Items[Index]);
    }
    Text->append("]");
}

static
VOID
TrFloats(
    _Inout_ std::string* Text,
    _In_ UINT Count,
    _In_reads_opt_(Count) const FLOAT* Values)
{
    TrArray(Text, Count, Values, [Text](FLOAT Value) {
        UINT Bits;

        memcpy(&Bits, &Value, sizeof(Bits));
        TrAppend(Text, "f%08X", Bits);
    });
}

/* Calls that print through their own formatter instead of one argument at a time */
template <TrFamily Family, UINT Slot>
struct TrFormat
{
    static constexpr BOOLEAN Custom = FALSE;
};

#define TR_LIST_SLOT(Member) (offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0114, Member) / sizeof(VOID*))
#define TR_QUEUE_SLOT(Member) (offsetof(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, Member) / sizeof(VOID*))

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnSetDescriptorHeaps)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_HCOMMANDLIST List,
        UINT Count,
        D3D12DDI_HDESCRIPTORHEAP* Heaps)
    {
        TrArg(Text, List);
        TrArg(Text, Count);
        TrArray(Text, Count, Heaps, [Text](const D3D12DDI_HDESCRIPTORHEAP& Heap) {
            Text->append(TrDriverId(Heap.pDrvPrivate));
        });
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnIASetVertexBuffers)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_API_HCOMMANDLIST List,
        UINT StartSlot,
        UINT Count,
        const D3D12DDI_VERTEX_BUFFER_VIEW* Views)
    {
        TrArg(Text, List);
        TrArg(Text, StartSlot);
        TrArg(Text, Count);
        TrArray(Text, Count, Views, [Text](const D3D12DDI_VERTEX_BUFFER_VIEW& View) {
            TrAppend(Text, "%llX:%u:%u", View.BufferLocation, View.SizeInBytes, View.StrideInBytes);
        });
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnSOSetTargets)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_API_HCOMMANDLIST List,
        UINT StartSlot,
        UINT Count,
        const D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW* Views)
    {
        TrArg(Text, List);
        TrArg(Text, StartSlot);
        TrArg(Text, Count);
        TrArray(Text, Count, Views, [Text](const D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW& View) {
            TrAppend(Text, "%llX:%llu:%llX", View.BufferLocation, View.SizeInBytes, View.BufferFilledSizeLocation);
        });
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnIASetIndexBuffer)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_API_HCOMMANDLIST List,
        const D3D12DDI_INDEX_BUFFER_VIEW* View)
    {
        TrArg(Text, List);
        if (View == nullptr)
            Text->append(" 0");
        else
            TrAppend(Text, " {%llX:%u:%u}", View->BufferLocation, View->SizeInBytes, View->Format);
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnOMSetRenderTargets)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_API_HCOMMANDLIST List,
        UINT Count,
        const D3D12DDI_CPU_DESCRIPTOR_HANDLE* Targets,
        BOOL SingleRange,
        const D3D12DDI_CPU_DESCRIPTOR_HANDLE* DepthStencil)
    {
        TrArg(Text, List);
        TrArg(Text, Count);
        TrArray(Text, SingleRange && Count != 0 ? 1 : Count, Targets, [Text](const D3D12DDI_CPU_DESCRIPTOR_HANDLE& Each) {
            Text->append(TrDriverId((const VOID*)Each.ptr));
        });
        TrArg(Text, SingleRange);
        if (DepthStencil == nullptr)
            Text->append(" 0");
        else
            Text->append(" " + TrDriverId((const VOID*)DepthStencil->ptr));
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnRsSetViewports)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_API_HCOMMANDLIST List,
        UINT Count,
        const D3D12DDI_VIEWPORT* Viewports)
    {
        TrArg(Text, List);
        TrArg(Text, Count);
        TrArray(Text, Count, Viewports, [Text](const D3D12DDI_VIEWPORT& Viewport) {
            TrAppendBytes(Text, &Viewport, sizeof(Viewport));
        });
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnRsSetScissorRects)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_API_HCOMMANDLIST List,
        UINT Count,
        const D3D12DDI_RECT* Rects)
    {
        TrArg(Text, List);
        TrArg(Text, Count);
        TrArray(Text, Count, Rects, [Text](const D3D12DDI_RECT& Rect) {
            TrAppend(Text, "%d:%d:%d:%d", Rect.left, Rect.top, Rect.right, Rect.bottom);
        });
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnOmSetBlendFactor)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_API_HCOMMANDLIST List,
        const FLOAT* Factor)
    {
        TrArg(Text, List);
        TrFloats(Text, 4, Factor);
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnRSSetShadingRate)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_API_HCOMMANDLIST List,
        D3D12DDI_SHADING_RATE_0062 Rate,
        const D3D12DDI_SHADING_RATE_COMBINER_0062* Combiners)
    {
        TrArg(Text, List);
        TrArg(Text, Rate);
        TrArray(Text, D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT, Combiners,
                [Text](D3D12DDI_SHADING_RATE_COMBINER_0062 Combiner) { TrAppend(Text, "%u", Combiner); });
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnClearRenderTargetView)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_HCOMMANDLIST List,
        D3D12DDI_CPU_DESCRIPTOR_HANDLE View,
        const FLOAT* Color,
        UINT Count,
        const D3D12DDI_RECT* Rects)
    {
        TrArg(Text, List);
        TrArg(Text, View);
        TrFloats(Text, 4, Color);
        TrArg(Text, Count);
        TrArray(Text, Count, Rects, [Text](const D3D12DDI_RECT& Rect) {
            TrAppend(Text, "%d:%d:%d:%d", Rect.left, Rect.top, Rect.right, Rect.bottom);
        });
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnResourceBarrier)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_HCOMMANDLIST List,
        UINT Count,
        const D3D12DDIARG_RESOURCE_BARRIER_0022* Barriers)
    {
        TrArg(Text, List);
        TrArg(Text, Count);
        TrArray(Text, Count, Barriers, [Text](const D3D12DDIARG_RESOURCE_BARRIER_0022& Barrier) {
            TrAppend(Text, "(t%u f%X", Barrier.Type, Barrier.Flags);
            switch (Barrier.Type)
            {
                case D3D12DDI_RESOURCE_BARRIER_TYPE_TRANSITION:
                    TrAppend(Text, " %s sub %u %X>%X)", TrDriverId(Barrier.Transition.hResource.pDrvPrivate).c_str(),
                             Barrier.Transition.Subresource, Barrier.Transition.StateBefore,
                             Barrier.Transition.StateAfter);
                    break;
                case D3D12DDI_RESOURCE_BARRIER_TYPE_UAV:
                    TrAppend(Text, " %s)", TrDriverId(Barrier.UAV.hResource.pDrvPrivate).c_str());
                    break;
                default:
                    Text->append(" ");
                    TrAppendBytes(Text, &Barrier.Ranged, sizeof(Barrier.Ranged));
                    Text->append(")");
                    break;
            }
        });
    }
};

template <>
struct TrFormat<TrFamilyList0, TR_LIST_SLOT(pfnBarrier)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_HCOMMANDLIST List,
        UINT32 Count,
        const D3D12DDIARG_BARRIER_0094* Barriers)
    {
        TrArg(Text, List);
        TrArg(Text, Count);
        TrArray(Text, Count, Barriers, [Text](const D3D12DDIARG_BARRIER_0094& Barrier) {
            TrAppend(Text, "(t%u", Barrier.Type);
            switch (Barrier.Type)
            {
                case D3D12DDI_BARRIER_TYPE_GLOBAL:
                    TrAppend(Text, " sync %X>%X access %X>%X)", Barrier.GlobalBarrier.SyncBefore,
                             Barrier.GlobalBarrier.SyncAfter, Barrier.GlobalBarrier.AccessBefore,
                             Barrier.GlobalBarrier.AccessAfter);
                    break;
                case D3D12DDI_BARRIER_TYPE_TEXTURE:
                {
                    const D3D12DDI_TEXTURE_BARRIER_0088& Each = Barrier.TextureBarrier;

                    TrAppend(Text, " sync %X>%X access %X>%X layout %u>%u %s sub %u:%u:%u:%u:%u:%u flags %X)",
                             Each.SyncBefore, Each.SyncAfter, Each.AccessBefore, Each.AccessAfter, Each.LayoutBefore,
                             Each.LayoutAfter, TrDriverId(Each.hResource.pDrvPrivate).c_str(),
                             Each.Subresources.IndexOrFirstMipLevel, Each.Subresources.NumMipLevels,
                             Each.Subresources.FirstArraySlice, Each.Subresources.NumArraySlices,
                             Each.Subresources.FirstPlane, Each.Subresources.NumPlanes, Each.Flags);
                    break;
                }
                case D3D12DDI_BARRIER_TYPE_BUFFER:
                    TrAppend(Text, " sync %X>%X access %X>%X %s)", Barrier.BufferBarrier.SyncBefore,
                             Barrier.BufferBarrier.SyncAfter, Barrier.BufferBarrier.AccessBefore,
                             Barrier.BufferBarrier.AccessAfter,
                             TrDriverId(Barrier.BufferBarrier.hResource.pDrvPrivate).c_str());
                    break;
                default:
                    TrAppend(Text, " flags %X %s sub %u range %llu:%llu)", Barrier.RangedBarrier.Flags,
                             TrDriverId(Barrier.RangedBarrier.hResource.pDrvPrivate).c_str(),
                             Barrier.RangedBarrier.Subresource, (UINT64)Barrier.RangedBarrier.Range.Begin,
                             (UINT64)Barrier.RangedBarrier.Range.End);
                    break;
            }
        });
    }
};

template <>
struct TrFormat<TrFamilyQueue, TR_QUEUE_SLOT(pfnExecuteCommandLists)>
{
    static constexpr BOOLEAN Custom = TRUE;

    static
    VOID
    Format(
        _Inout_ std::string* Text,
        D3D12DDI_HCOMMANDQUEUE Queue,
        UINT Count,
        const D3D12DDI_HCOMMANDLIST* Lists)
    {
        TrArg(Text, Queue);
        TrArg(Text, Count);
        TrArray(Text, Count, Lists, [Text](const D3D12DDI_HCOMMANDLIST& List) {
            Text->append(TrDriverId(List.pDrvPrivate));
        });
    }
};

static
const CHAR*
TrName(
    _In_ TrFamily Family,
    _In_ UINT Slot);

/* Work some calls need around them, such as wrapping the tables they hand out */
template <TrFamily Family, UINT Slot>
struct TrAround
{
    template <typename... A>
    static
    VOID
    Before(
        A... Args)
    {
    }

    template <typename... A>
    static
    VOID
    After(
        A... Args)
    {
    }
};

/*
 * Once an adapter closes, every handle of its devices is gone, so later handles get new ids even when
 * the heap hands back the same addresses. Both runtimes then number a second device alike.
 */
static
VOID
TrForgetAfter(
    _In_ TrFamily Family,
    _In_ UINT Slot)
{
    if (Family != TrFamilyAdapter || strcmp(TrName(Family, Slot), "CloseAdapter") != 0)
        return;

    AcquireSRWLockExclusive(&Tr.IdLock);
    for (std::map<ULONG_PTR, UINT>& Ids : Tr.Ids)
        Ids.clear();
    Tr.Partners.clear();
    ReleaseSRWLockExclusive(&Tr.IdLock);
}

/* A destroyed driver object's address may come back for a new one, which then gets a new id */
template <typename T>
static
VOID
TrForgetHandle(
    _In_ const T& Arg)
{
    if constexpr (TrIsDriverHandle<T>::value && !std::is_same_v<T, D3D12DDI_HDEVICE>)
    {
        AcquireSRWLockExclusive(&Tr.IdLock);
        auto Partner = Tr.Partners.find((ULONG_PTR)Arg.pDrvPrivate);
        if (Partner != Tr.Partners.end())
        {
            Tr.Ids[TrKindRuntime].erase(Partner->second);
            Tr.Partners.erase(Partner);
        }
        Tr.Ids[TrKindDriver].erase((ULONG_PTR)Arg.pDrvPrivate);
        ReleaseSRWLockExclusive(&Tr.IdLock);
    }
}

template <typename... A>
static
VOID
TrForgetDestroyed(
    _In_ TrFamily Family,
    _In_ UINT Slot,
    _In_ const A&... Args)
{
    if (Family == TrFamilyDevice && strncmp(TrName(Family, Slot), "Destroy", 7) == 0)
        (TrForgetHandle(Args), ...);
}

template <typename T>
static
VOID
TrNotePartner(
    _In_ const T& Arg,
    _In_ ULONG_PTR Runtime)
{
    if constexpr (TrIsDriverHandle<T>::value && !std::is_same_v<T, D3D12DDI_HDEVICE>)
    {
        if (Runtime != 0 && Arg.pDrvPrivate != nullptr)
            Tr.Partners[(ULONG_PTR)Arg.pDrvPrivate] = Runtime;
    }
}

template <typename T>
static
VOID
TrFindRuntime(
    _In_ const T& Arg,
    _Inout_ ULONG_PTR* Runtime)
{
    if constexpr (TrIsRuntimeHandle<T>::value)
    {
        if (*Runtime == 0)
            *Runtime = (ULONG_PTR)Arg.handle;
    }
}

/* A runtime object's address may come back too once the driver object made with it is destroyed */
template <typename... A>
static
VOID
TrNoteCreated(
    _In_ TrFamily Family,
    _In_ UINT Slot,
    _In_ const A&... Args)
{
    ULONG_PTR Runtime = 0;

    if (Family != TrFamilyDevice || strncmp(TrName(Family, Slot), "Create", 6) != 0)
        return;

    (TrFindRuntime(Args, &Runtime), ...);
    AcquireSRWLockExclusive(&Tr.IdLock);
    (TrNotePartner(Args, Runtime), ...);
    ReleaseSRWLockExclusive(&Tr.IdLock);
}

template <TrFamily Family, UINT Slot, typename Pfn>
struct TrHook;

template <TrFamily Family, UINT Slot, typename R, typename... A>
struct TrHook<Family, Slot, R (APIENTRY*)(A...)>
{
    static
    R
    APIENTRY
    Call(
        A... Args)
    {
        using Pfn = R (APIENTRY*)(A...);
        Pfn Original = reinterpret_cast<Pfn>(Tr.Originals[Family][Slot]);
        BOOLEAN Callback = Family == TrFamilyAdapterCallback || Family == TrFamilyKernelCallback ||
                           Family == TrFamilyCoreCallback;
        BOOLEAN WasQuiet = TrQuiet;
        BOOLEAN Log;
        std::string Text;

        if (Callback)
            TrCallbackDepth += 1;
        Log = TrShouldLog() && !(Family == TrFamilyKernel && Tr.NoKernel);
        TrQuiet = !Log;

        if (Log)
        {
            constexpr TrFamily Shape = Family >= TrFamilyList0 && Family <= TrFamilyList3 ? TrFamilyList0 : Family;

            Text = TrName(Family, Slot);
            Text.append("(");
            TrCallName = TrName(Family, Slot);
            if constexpr (TrFormat<Shape, Slot>::Custom)
                TrFormat<Shape, Slot>::Format(&Text, Args...);
            else
                (TrArg(&Text, Args), ...);
            TrCallName = nullptr;
            Text.append(")");
            TrBegin(Text);
        }

        TrAround<Family, Slot>::Before(Args...);
        if constexpr (std::is_void_v<R>)
        {
            Original(Args...);
            TrAround<Family, Slot>::After(Args...);
            TrNoteCreated(Family, Slot, Args...);
            if (Log)
                TrEnd("");
            TrForgetAfter(Family, Slot);
            TrForgetDestroyed(Family, Slot, Args...);
            TrQuiet = WasQuiet;
            if (Callback)
                TrCallbackDepth -= 1;
        }
        else
        {
            R Result = Original(Args...);

            TrAround<Family, Slot>::After(Args...);
            TrNoteCreated(Family, Slot, Args...);
            if (Log)
                TrEnd(TrResult(Result));
            TrForgetAfter(Family, Slot);
            TrForgetDestroyed(Family, Slot, Args...);
            TrQuiet = WasQuiet;
            if (Callback)
                TrCallbackDepth -= 1;
            return Result;
        }
    }
};

/* One wrapped table: names and the wrapper for each slot */
struct TrTable
{
    const CHAR* Names[TrSlotLimit];
    VOID* Thunks[TrSlotLimit];
    UINT Count;
};

static TrTable TrTables[TrFamilyCount];

template <TrFamily Family, typename Struct, SIZE_T Offset, typename Member>
static
VOID
TrRegister(
    _In_z_ const CHAR* Name)
{
    constexpr UINT Slot = (UINT)(Offset / sizeof(VOID*));

    static_assert(Slot < TrSlotLimit);
    TrTables[Family].Names[Slot] = Name;
    TrTables[Family].Thunks[Slot] = reinterpret_cast<VOID*>(&TrHook<Family, Slot, Member>::Call);
    if (TrTables[Family].Count < Slot + 1)
        TrTables[Family].Count = Slot + 1;
}

#define TR_ENTRY_FOR(Family) \
    [](){} /* placeholder */

#define TR_REGISTER(FamilyValue, Struct, Member) \
    TrRegister<FamilyValue, Struct, offsetof(Struct, Member), decltype(Struct::Member)>(#Member + 3);

#define TR_ADAPTER(Struct, Member) TR_REGISTER(TrFamilyAdapter, Struct, Member)
#define TR_ADAPTERCALLBACK(Struct, Member) TR_REGISTER(TrFamilyAdapterCallback, Struct, Member)
#define TR_DEVICE(Struct, Member) TR_REGISTER(TrFamilyDevice, Struct, Member)
#define TR_LIST0(Struct, Member) TR_REGISTER(TrFamilyList0, Struct, Member)
#define TR_LIST1(Struct, Member) TR_REGISTER(TrFamilyList1, Struct, Member)
#define TR_LIST2(Struct, Member) TR_REGISTER(TrFamilyList2, Struct, Member)
#define TR_LIST3(Struct, Member) TR_REGISTER(TrFamilyList3, Struct, Member)
#define TR_QUEUE(Struct, Member) TR_REGISTER(TrFamilyQueue, Struct, Member)
#define TR_EXTENDED(Struct, Member) TR_REGISTER(TrFamilyExtended, Struct, Member)
#define TR_KERNELCALLBACK(Struct, Member) TR_REGISTER(TrFamilyKernelCallback, Struct, Member)
#define TR_CORECALLBACK(Struct, Member) TR_REGISTER(TrFamilyCoreCallback, Struct, Member)

/* Kernel thunks the core may call, by name and type */
#define TR_TABLE_KERNEL(Entry) \
    Entry(D3DKMTCreateDevice, PFND3DKMT_CREATEDEVICE) \
    Entry(D3DKMTDestroyDevice, PFND3DKMT_DESTROYDEVICE) \
    Entry(D3DKMTCreateContextVirtual, PFND3DKMT_CREATECONTEXTVIRTUAL) \
    Entry(D3DKMTDestroyContext, PFND3DKMT_DESTROYCONTEXT) \
    Entry(D3DKMTCreateHwQueue, PFND3DKMT_CREATEHWQUEUE) \
    Entry(D3DKMTDestroyHwQueue, PFND3DKMT_DESTROYHWQUEUE) \
    Entry(D3DKMTCreatePagingQueue, PFND3DKMT_CREATEPAGINGQUEUE) \
    Entry(D3DKMTDestroyPagingQueue, PFND3DKMT_DESTROYPAGINGQUEUE) \
    Entry(D3DKMTCreateAllocation2, PFND3DKMT_CREATEALLOCATION) \
    Entry(D3DKMTDestroyAllocation2, PFND3DKMT_DESTROYALLOCATION2) \
    Entry(D3DKMTMakeResident, PFND3DKMT_MAKERESIDENT) \
    Entry(D3DKMTEvict, PFND3DKMT_EVICT) \
    Entry(D3DKMTLock2, PFND3DKMT_LOCK2) \
    Entry(D3DKMTUnlock2, PFND3DKMT_UNLOCK2) \
    Entry(D3DKMTEscape, PFND3DKMT_ESCAPE) \
    Entry(D3DKMTQueryAdapterInfo, PFND3DKMT_QUERYADAPTERINFO) \
    Entry(D3DKMTSubmitCommand, PFND3DKMT_SUBMITCOMMAND) \
    Entry(D3DKMTSubmitCommandToHwQueue, PFND3DKMT_SUBMITCOMMANDTOHWQUEUE) \
    Entry(D3DKMTCreateSynchronizationObject2, PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2) \
    Entry(D3DKMTDestroySynchronizationObject, PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT) \
    Entry(D3DKMTSignalSynchronizationObjectFromCpu, PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU) \
    Entry(D3DKMTWaitForSynchronizationObjectFromCpu, PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU) \
    Entry(D3DKMTSignalSynchronizationObjectFromGpu, PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU) \
    Entry(D3DKMTWaitForSynchronizationObjectFromGpu, PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU) \
    Entry(D3DKMTOpenAdapterFromLuid, PFND3DKMT_OPENADAPTERFROMLUID) \
    Entry(D3DKMTCloseAdapter, PFND3DKMT_CLOSEADAPTER) \
    Entry(D3DKMTMapGpuVirtualAddress, PFND3DKMT_MAPGPUVIRTUALADDRESS) \
    Entry(D3DKMTReserveGpuVirtualAddress, PFND3DKMT_RESERVEGPUVIRTUALADDRESS) \
    Entry(D3DKMTFreeGpuVirtualAddress, PFND3DKMT_FREEGPUVIRTUALADDRESS) \
    Entry(D3DKMTUpdateGpuVirtualAddress, PFND3DKMT_UPDATEGPUVIRTUALADDRESS) \
    Entry(D3DKMTSetContextSchedulingPriority, PFND3DKMT_SETCONTEXTSCHEDULINGPRIORITY) \
    Entry(D3DKMTSetContextInProcessSchedulingPriority, PFND3DKMT_SETCONTEXTINPROCESSSCHEDULINGPRIORITY) \
    Entry(D3DKMTQueryResourceInfo, PFND3DKMT_QUERYRESOURCEINFO) \
    Entry(D3DKMTOpenResource2, PFND3DKMT_OPENRESOURCE2) \
    Entry(D3DKMTGetDeviceState, PFND3DKMT_GETDEVICESTATE) \
    Entry(D3DKMTCreateNativeFence, PFND3DKMT_CREATENATIVEFENCE) \
    Entry(D3DKMTSubmitWaitForSyncObjectsToHwQueue, PFND3DKMT_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE) \
    Entry(D3DKMTSubmitSignalSyncObjectsToHwQueue, PFND3DKMT_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE) \
    Entry(D3DKMTChangeVideoMemoryReservation, PFND3DKMT_CHANGEVIDEOMEMORYRESERVATION) \
    Entry(D3DKMTQueryVideoMemoryInfo, PFND3DKMT_QUERYVIDEOMEMORYINFO) \
    Entry(D3DKMTSetAllocationPriority, PFND3DKMT_SETALLOCATIONPRIORITY) \
    Entry(D3DKMTRegisterTrimNotification, PFND3DKMT_REGISTERTRIMNOTIFICATION) \
    Entry(D3DKMTUnregisterTrimNotification, PFND3DKMT_UNREGISTERTRIMNOTIFICATION) \
    Entry(D3DKMTOfferAllocations, PFND3DKMT_OFFERALLOCATIONS) \
    Entry(D3DKMTReclaimAllocations2, PFND3DKMT_RECLAIMALLOCATIONS2) \
    Entry(D3DKMTInvalidateCache, PFND3DKMT_INVALIDATECACHE) \
    Entry(D3DKMTEnumAdapters2, PFND3DKMT_ENUMADAPTERS2) \
    Entry(D3DKMTQueryClockCalibration, PFND3DKMT_QUERYCLOCKCALIBRATION) \
    Entry(D3DKMTGetSharedResourceAdapterLuid, PFND3DKMT_GETSHAREDRESOURCEADAPTERLUID) \

/* The kernel thunk list as a table with one slot per entry */
struct TrKernelSlots
{
#define TR_KERNEL_MEMBER(Name, Type) Type Name;
    TR_TABLE_KERNEL(TR_KERNEL_MEMBER)
#undef TR_KERNEL_MEMBER
};

#define TR_KERNEL(Name, Type) TR_REGISTER(TrFamilyKernel, TrKernelSlots, Name)

static
VOID
TrRegisterAll(VOID)
{
    TR_TABLE_ADAPTER(TR_ADAPTER)
    TR_TABLE_ADAPTERCALLBACK(TR_ADAPTERCALLBACK)
    TR_TABLE_DEVICE(TR_DEVICE)
    TR_TABLE_LIST(TR_LIST0)
    TR_TABLE_LIST(TR_LIST1)
    TR_TABLE_LIST(TR_LIST2)
    TR_TABLE_LIST(TR_LIST3)
    TR_TABLE_QUEUE(TR_QUEUE)
    TR_TABLE_EXTENDED(TR_EXTENDED)
    TR_TABLE_KERNELCALLBACK(TR_KERNELCALLBACK)
    TR_TABLE_CORECALLBACK(TR_CORECALLBACK)
    TR_TABLE_KERNEL(TR_KERNEL)
}

static
const CHAR*
TrName(
    _In_ TrFamily Family,
    _In_ UINT Slot)
{
    const CHAR* Name = TrTables[Family].Names[Slot];

    return Name != nullptr ? Name : "?";
}

/* Saves each filled entry of a table and points it at the wrapper for its slot */
static
VOID
TrWrapTable(
    _In_ TrFamily Family,
    _Inout_updates_bytes_(Size) VOID* Table,
    _In_ SIZE_T Size)
{
    VOID** Slots = static_cast<VOID**>(Table);
    UINT Count = (UINT)(Size / sizeof(VOID*));

    if (Count > TrTables[Family].Count)
        Count = TrTables[Family].Count;

    for (UINT Slot = 0; Slot < Count; Slot++)
    {
        if (Slots[Slot] == nullptr || Slots[Slot] == TrTables[Family].Thunks[Slot] ||
            TrTables[Family].Thunks[Slot] == nullptr)
        {
            continue;
        }
        Tr.Originals[Family][Slot] = Slots[Slot];
        Slots[Slot] = TrTables[Family].Thunks[Slot];
    }
}

/* A runtime callback table copied so the driver calls the copy, which is wrapped */
static
const VOID*
TrWrapCallbacks(
    _In_ TrFamily Family,
    _In_opt_ const VOID* Table,
    _In_ SIZE_T Size)
{
    VOID* Copy;

    if (Table == nullptr)
        return nullptr;

    /* The driver keeps these for the life of the device, so the copy is never freed */
    Copy = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Copy == nullptr)
        return Table;
    memcpy(Copy, Table, Size);
    TrWrapTable(Family, Copy, Size);
    return Copy;
}

/* The adapter's FillDDITable: the tables it fills are wrapped once the driver is done */
template <>
struct TrAround<TrFamilyAdapter, offsetof(D3D12DDI_ADAPTERFUNCS, pfnFillDDITable) / sizeof(VOID*)>
{
    static
    VOID
    Before(
        D3D12DDI_HADAPTER Adapter,
        D3D12DDI_TABLE_TYPE Type,
        VOID* Table,
        SIZE_T Size,
        UINT Index,
        D3D12DDI_HRTTABLE RuntimeTable)
    {
    }

    static
    VOID
    After(
        D3D12DDI_HADAPTER Adapter,
        D3D12DDI_TABLE_TYPE Type,
        VOID* Table,
        SIZE_T Size,
        UINT Index,
        D3D12DDI_HRTTABLE RuntimeTable)
    {
        if (Type == D3D12DDI_TABLE_TYPE_DEVICE_CORE)
            TrWrapTable(TrFamilyDevice, Table, Size);
        else if (Type == D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D && Index < 4)
            TrWrapTable((TrFamily)(TrFamilyList0 + Index), Table, Size);
        else if (Type == D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D)
            TrWrapTable(TrFamilyQueue, Table, Size);
        else if (Type == D3D12DDI_TABLE_TYPE_0096_EXTENDED_FEATURES)
            TrWrapTable(TrFamilyExtended, Table, Size);
    }
};

/* The adapter's CreateDevice: the callbacks handed in are swapped for wrapped copies */
template <>
struct TrAround<TrFamilyAdapter, offsetof(D3D12DDI_ADAPTERFUNCS, pfnCreateDevice) / sizeof(VOID*)>
{
    static
    VOID
    Before(
        D3D12DDI_HADAPTER Adapter,
        const D3D12DDIARG_CREATEDEVICE_0003* Args)
    {
        D3D12DDIARG_CREATEDEVICE_0003* Writable = const_cast<D3D12DDIARG_CREATEDEVICE_0003*>(Args);

        Writable->pKTCallbacks = static_cast<const D3DDDI_DEVICECALLBACKS*>(
            TrWrapCallbacks(TrFamilyKernelCallback, Args->pKTCallbacks, sizeof(D3DDDI_DEVICECALLBACKS)));
        Writable->p12UMCallbacks_0116 = static_cast<const D3D12DDI_CORELAYER_DEVICECALLBACKS_0116*>(
            TrWrapCallbacks(TrFamilyCoreCallback, Args->p12UMCallbacks_0116,
                            sizeof(D3D12DDI_CORELAYER_DEVICECALLBACKS_0116)));
    }

    static
    VOID
    After(
        D3D12DDI_HADAPTER Adapter,
        const D3D12DDIARG_CREATEDEVICE_0003* Args)
    {
    }
};

/* The driver's format answer, which the runtime turns into the API bits */
template <>
struct TrAround<TrFamilyDevice, offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0119, pfnCheckFormatSupport) / sizeof(VOID*)>
{
    static
    VOID
    Before(
        D3D12DDI_HDEVICE Device,
        DXGI_FORMAT Format,
        UINT* Support)
    {
    }

    static
    VOID
    After(
        D3D12DDI_HDEVICE Device,
        DXGI_FORMAT Format,
        UINT* Support)
    {
        if (TrShouldLog())
        {
            TrCloseLine();
            printf("%*sformat %u support %08X\n", static_cast<int>(Tr.Depth * 2), "", Format, *Support);
        }
    }
};

/* The numbers an extended feature list or version list comes back with */
static
VOID
TrPrintList(
    _In_z_ PCSTR Label,
    _In_ const UINT32* Count,
    _In_opt_ const UINT32* Values)
{
    if (!TrShouldLog() || Count == nullptr)
        return;

    TrCloseLine();
    printf("%*s%s count %u", static_cast<int>(Tr.Depth * 2), "", Label, *Count);
    for (UINT Index = 0; Values != nullptr && Index < *Count; Index++)
        printf(" %u", Values[Index]);
    printf("\n");
}

template <>
struct TrAround<TrFamilyExtended, offsetof(D3D12DDI_EXTENDED_FEATURES_FUNCS_0096, pfnGetSupportedExtendedFeatures) /
                                  sizeof(VOID*)>
{
    static
    VOID
    Before(
        D3D12DDI_HDEVICE Device,
        D3D12DDI_FEATURE_0020 Highest,
        UINT32* Count,
        D3D12DDI_FEATURE_0020* Features)
    {
    }

    static
    VOID
    After(
        D3D12DDI_HDEVICE Device,
        D3D12DDI_FEATURE_0020 Highest,
        UINT32* Count,
        D3D12DDI_FEATURE_0020* Features)
    {
        TrPrintList("features", Count, reinterpret_cast<const UINT32*>(Features));
    }
};

template <>
struct TrAround<TrFamilyExtended, offsetof(D3D12DDI_EXTENDED_FEATURES_FUNCS_0096,
                                           pfnGetSupportedExtendedFeatureVersions) / sizeof(VOID*)>
{
    static
    VOID
    Before(
        D3D12DDI_HDEVICE Device,
        D3D12DDI_FEATURE_0020 Feature,
        UINT32* Count,
        UINT32* Versions)
    {
    }

    static
    VOID
    After(
        D3D12DDI_HDEVICE Device,
        D3D12DDI_FEATURE_0020 Feature,
        UINT32* Count,
        UINT32* Versions)
    {
        TrPrintList("versions", Count, Versions);
    }
};

/* The driver's OpenAdapter12: adapter callbacks in, adapter table out */
static
HRESULT
APIENTRY
TrOpenAdapter12(
    _Inout_ D3D12DDIARG_OPENADAPTER* Args)
{
    std::string Text;
    HRESULT hr;

    Args->pAdapterCallbacks = static_cast<const D3DDDI_ADAPTERCALLBACKS*>(
        TrWrapCallbacks(TrFamilyAdapterCallback, Args->pAdapterCallbacks, sizeof(D3DDDI_ADAPTERCALLBACKS)));

    TrAppend(&Text, "OpenAdapter12(rt %s)", TrId(TrKindRuntime, (ULONG_PTR)Args->hRTAdapter.handle).c_str());
    TrBegin(Text);
    hr = Tr.OpenAdapter(Args);
    Text.clear();
    TrAppend(&Text, "%08X adapter %s", (UINT)hr, TrId(TrKindDriver, (ULONG_PTR)Args->hAdapter.pDrvPrivate).c_str());
    TrEnd(Text);

    if (SUCCEEDED(hr))
        TrWrapTable(TrFamilyAdapter, Args->pAdapterFuncs, sizeof(D3D12DDI_ADAPTERFUNCS_0109));
    return hr;
}

/* Kernel thunk slots by name; stored names drop the D3D prefix */
static
INT
TrKernelSlot(
    _In_z_ LPCSTR Name)
{
    for (UINT Slot = 0; Slot < TrTables[TrFamilyKernel].Count; Slot++)
    {
        if (TrTables[TrFamilyKernel].Names[Slot] != nullptr &&
            strcmp(TrTables[TrFamilyKernel].Names[Slot], Name + 3) == 0)
        {
            return (INT)Slot;
        }
    }
    return -1;
}

static
FARPROC
WINAPI
TrGetProcAddress(
    _In_ HMODULE Module,
    _In_ LPCSTR Name)
{
    FARPROC Found = Tr.GetProcAddress(Module, Name);
    INT Slot;

    if (Found == nullptr || IS_INTRESOURCE(Name))
        return Found;

    if (strcmp(Name, "OpenAdapter12") == 0)
    {
        Tr.OpenAdapter = reinterpret_cast<PFND3D12DDI_OPENADAPTER>(Found);
        return reinterpret_cast<FARPROC>(&TrOpenAdapter12);
    }

    if (strncmp(Name, "D3DKMT", 6) == 0)
    {
        Slot = TrKernelSlot(Name);
        if (Slot >= 0)
        {
            Tr.Originals[TrFamilyKernel][Slot] = reinterpret_cast<VOID*>(Found);
            return reinterpret_cast<FARPROC>(TrTables[TrFamilyKernel].Thunks[Slot]);
        }
    }

    return Found;
}

/* The exported name of a function, found in the module that holds it */
static
LPCSTR
TrExportName(
    _In_opt_ FARPROC Function)
{
    HMODULE Owner = NULL;
    BYTE* Base;
    IMAGE_NT_HEADERS* Nt;
    IMAGE_EXPORT_DIRECTORY* Exports;
    DWORD* Names;
    WORD* Ordinals;
    DWORD* Functions;

    if (Function == nullptr ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(Function), &Owner))
    {
        return nullptr;
    }

    Base = reinterpret_cast<BYTE*>(Owner);
    Nt = reinterpret_cast<IMAGE_NT_HEADERS*>(Base + reinterpret_cast<IMAGE_DOS_HEADER*>(Base)->e_lfanew);
    Exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(
        Base + Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    Names = reinterpret_cast<DWORD*>(Base + Exports->AddressOfNames);
    Ordinals = reinterpret_cast<WORD*>(Base + Exports->AddressOfNameOrdinals);
    Functions = reinterpret_cast<DWORD*>(Base + Exports->AddressOfFunctions);

    for (DWORD Index = 0; Index < Exports->NumberOfNames; Index++)
    {
        if (Base + Functions[Ordinals[Index]] == reinterpret_cast<BYTE*>(Function))
            return reinterpret_cast<LPCSTR>(Base + Names[Index]);
    }
    return nullptr;
}

/* Points a module's imports of GetProcAddress and the D3DKMT thunks at the wrappers */
static
VOID
TrPatchImports(
    _In_ HMODULE Module)
{
    BYTE* Base = reinterpret_cast<BYTE*>(Module);
    IMAGE_NT_HEADERS* Nt = reinterpret_cast<IMAGE_NT_HEADERS*>(Base + reinterpret_cast<IMAGE_DOS_HEADER*>(Base)->e_lfanew);
    IMAGE_DATA_DIRECTORY* Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR* Import = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(Base + Directory->VirtualAddress);

    for (; Import->Name != 0; Import++)
    {
        IMAGE_THUNK_DATA* Names = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Import->OriginalFirstThunk);
        IMAGE_THUNK_DATA* Slots = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Import->FirstThunk);

        for (; Names->u1.AddressOfData != 0; Names++, Slots++)
        {
            VOID* Replacement = nullptr;
            LPCSTR Name;
            DWORD Old;
            INT Slot;

            if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
                continue;
            Name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(Base + Names->u1.AddressOfData)->Name;

            if (strcmp(Name, "GetProcAddress") == 0)
            {
                Tr.GetProcAddress = reinterpret_cast<FARPROC (WINAPI*)(HMODULE, LPCSTR)>(Slots->u1.Function);
                Replacement = reinterpret_cast<VOID*>(&TrGetProcAddress);
            }
            else if (strncmp(Name, "D3DKMT", 6) == 0 && (Slot = TrKernelSlot(Name)) >= 0)
            {
                Tr.Originals[TrFamilyKernel][Slot] = reinterpret_cast<VOID*>(Slots->u1.Function);
                Replacement = TrTables[TrFamilyKernel].Thunks[Slot];
            }

            if (Replacement == nullptr)
                continue;
            VirtualProtect(&Slots->u1.Function, sizeof(VOID*), PAGE_READWRITE, &Old);
            Slots->u1.Function = reinterpret_cast<ULONG_PTR>(Replacement);
            VirtualProtect(&Slots->u1.Function, sizeof(VOID*), Old, &Old);
        }
    }

    /* Delay loaded thunks are resolved here so the loader stub never replaces the wrapper */
    Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (Directory->VirtualAddress == 0)
        return;

    for (IMAGE_DELAYLOAD_DESCRIPTOR* Delay = reinterpret_cast<IMAGE_DELAYLOAD_DESCRIPTOR*>(Base + Directory->VirtualAddress);
         Delay->DllNameRVA != 0;
         Delay++)
    {
        IMAGE_THUNK_DATA* Names = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Delay->ImportNameTableRVA);
        IMAGE_THUNK_DATA* Slots = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Delay->ImportAddressTableRVA);
        HMODULE Target = NULL;

        for (; Names->u1.AddressOfData != 0; Names++, Slots++)
        {
            LPCSTR Name;
            FARPROC Real;
            DWORD Old;
            INT Slot;

            if (Target == NULL)
                Target = LoadLibraryA(reinterpret_cast<LPCSTR>(Base + Delay->DllNameRVA));
            if (Target == NULL)
                break;

            /* The system core imports these by ordinal; the name comes from the target's exports */
            if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
            {
                Real = GetProcAddress(Target, MAKEINTRESOURCEA(IMAGE_ORDINAL(Names->u1.Ordinal)));
                Name = TrExportName(Real);
            }
            else
            {
                Name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(Base + Names->u1.AddressOfData)->Name;
                Real = GetProcAddress(Target, Name);
            }

            if (Real == nullptr || Name == nullptr || strncmp(Name, "D3DKMT", 6) != 0 ||
                (Slot = TrKernelSlot(Name)) < 0)
            {
                continue;
            }

            Tr.Originals[TrFamilyKernel][Slot] = reinterpret_cast<VOID*>(Real);
            VirtualProtect(&Slots->u1.Function, sizeof(VOID*), PAGE_READWRITE, &Old);
            Slots->u1.Function = reinterpret_cast<ULONG_PTR>(TrTables[TrFamilyKernel].Thunks[Slot]);
            VirtualProtect(&Slots->u1.Function, sizeof(VOID*), Old, &Old);
        }
    }
}

/* Scenarios */

struct TrRun
{
    PFN_D3D12_CREATE_DEVICE CreateDevice;
    PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE SerializeVersioned;
};

static
VOID
TrScenarioDevice(
    _In_ TrRun& Run)
{
    ComPtr<ID3D12Device> Device;

    TrPhase("D3D12CreateDevice");
    printf("result %08X\n", (UINT)Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)));
    TrPhase("D3D12CreateDevice again");
    printf("result %08X\n", (UINT)Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)));
}

static
VOID
TrScenarioObjects(
    _In_ TrRun& Run)
{
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> List;
    ComPtr<ID3D12DescriptorHeap> Heap;
    ComPtr<ID3D12Fence> Fence;
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};

    if (FAILED(Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
        return;

    TrPhase("CreateCommandQueue direct");
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    printf("result %08X\n", (UINT)Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)));
    for (D3D12_COMMAND_LIST_TYPE Type : { D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY })
    {
        ComPtr<ID3D12CommandQueue> Other;

        TrPhase(Type == D3D12_COMMAND_LIST_TYPE_COMPUTE ? "CreateCommandQueue compute" : "CreateCommandQueue copy");
        QueueDesc.Type = Type;
        printf("result %08X\n", (UINT)Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Other)));
    }
    TrPhase("CreateCommandAllocator direct");
    printf("result %08X\n", (UINT)Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                 IID_PPV_ARGS(&Allocator)));
    TrPhase("CreateCommandList direct");
    printf("result %08X\n", (UINT)Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(),
                                                            nullptr, IID_PPV_ARGS(&List)));
    TrPhase("Close");
    printf("result %08X\n", (UINT)List->Close());
    TrPhase("CreateDescriptorHeap rtv");
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 4;
    printf("result %08X\n", (UINT)Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Heap)));
    TrPhase("CreateFence");
    printf("result %08X\n", (UINT)Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)));
    TrPhase("Release");
    Fence.Reset();
    Heap.Reset();
    List.Reset();
    Allocator.Reset();
    Queue.Reset();
}

static
VOID
TrScenarioResources(
    _In_ TrRun& Run)
{
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12Resource> Buffer;
    ComPtr<ID3D12Resource> Texture;
    D3D12_HEAP_PROPERTIES Upload = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_HEAP_PROPERTIES Default = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC Desc = {};

    if (FAILED(Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
        return;

    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = 65536;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    TrPhase("CreateCommittedResource upload buffer");
    printf("result %08X\n", (UINT)Device->CreateCommittedResource(&Upload, D3D12_HEAP_FLAG_NONE, &Desc,
                                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                  IID_PPV_ARGS(&Buffer)));

    Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    TrPhase("CreateCommittedResource default texture");
    printf("result %08X\n", (UINT)Device->CreateCommittedResource(&Default, D3D12_HEAP_FLAG_NONE, &Desc,
                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                                  IID_PPV_ARGS(&Texture)));
    TrPhase("Release");
    Texture.Reset();
    Buffer.Reset();
}

static
VOID
TrScenarioCopy(
    _In_ TrRun& Run)
{
    static const FLOAT Color[4] = { 1.0f, 0.5f, 0.25f, 1.0f };
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> List;
    ComPtr<ID3D12DescriptorHeap> Heap;
    ComPtr<ID3D12Resource> Target;
    ComPtr<ID3D12Resource> Readback;
    ComPtr<ID3D12Fence> Fence;
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
    D3D12_HEAP_PROPERTIES Default = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_HEAP_PROPERTIES Read = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC Desc = {};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
    D3D12_TEXTURE_COPY_LOCATION Destination = {};
    D3D12_TEXTURE_COPY_LOCATION Source = {};
    D3D12_RESOURCE_BARRIER Barrier = {};
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv;
    ID3D12CommandList* Lists[1];
    UINT64 Total;
    const UINT* Pixels;
    HANDLE Event;

    if (FAILED(Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
        return;

    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 1;
    if (FAILED(Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue))) ||
        FAILED(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Allocator))) ||
        FAILED(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&List))) ||
        FAILED(Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Heap))) ||
        FAILED(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence))))
    {
        printf("setup failed\n");
        return;
    }

    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Device->CreateCommittedResource(&Default, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                    IID_PPV_ARGS(&Target));
    Device->GetCopyableFootprints(&Desc, 0, 1, 0, &Footprint, nullptr, nullptr, &Total);
    Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Total;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Device->CreateCommittedResource(&Read, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                    IID_PPV_ARGS(&Readback));
    if (Target == nullptr || Readback == nullptr)
    {
        printf("resources failed\n");
        return;
    }

    TrPhase("CreateRenderTargetView");
    Rtv = Heap->GetCPUDescriptorHandleForHeapStart();
    Device->CreateRenderTargetView(Target.Get(), nullptr, Rtv);
    TrPhase("ClearRenderTargetView");
    List->ClearRenderTargetView(Rtv, Color, 0, nullptr);
    TrPhase("ResourceBarrier");
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Target.Get();
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    List->ResourceBarrier(1, &Barrier);
    TrPhase("CopyTextureRegion");
    Destination.pResource = Readback.Get();
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Destination.PlacedFootprint = Footprint;
    Source.pResource = Target.Get();
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    List->CopyTextureRegion(&Destination, 0, 0, 0, &Source, nullptr);
    TrPhase("Close");
    printf("result %08X\n", (UINT)List->Close());
    TrPhase("ExecuteCommandLists");
    Lists[0] = List.Get();
    Queue->ExecuteCommandLists(1, Lists);
    TrPhase("Signal");
    printf("result %08X\n", (UINT)Queue->Signal(Fence.Get(), 1));
    TrPhase("SetEventOnCompletion");
    Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    printf("result %08X\n", (UINT)Fence->SetEventOnCompletion(1, Event));
    printf("wait %lu\n", WaitForSingleObject(Event, 5000));
    CloseHandle(Event);
    TrPhase("Map");
    if (SUCCEEDED(Readback->Map(0, nullptr, (void**)&Pixels)))
    {
        printf("pixel %08X\n", Pixels[0]);
        Readback->Unmap(0, nullptr);
    }
    TrPhase("Release");
}

/* Every feature once with a zeroed buffer of the size the runtime takes, to show what reaches the driver */
static
VOID
TrScenarioFeatures(
    _In_ TrRun& Run)
{
    static const UINT Sizes[] =
    {
        60, 16, 24, 12, 16, 8, 8, 4, 24, 0, 8, 0, 4, 0, 0, 0, 20, 4, 8, 4, 12, 20, 4, 12, 8, 8, 0, 12, 8, 0,
        20, 0, 8, 8, 0, 0, 4, 24, 0, 8, 4, 12, 24, 12, 8, 8, 8, 4, 40, 8, 4, 32, 4, 16, 4, 0, 4, 4, 0, 0,
        4,
    };
    ComPtr<ID3D12Device> Device;
    alignas(16) BYTE Buffer[512];
    CHAR Phase[64];
    UINT Feature;

    if (FAILED(Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
        return;

    for (Feature = 0; Feature < _countof(Sizes); Feature++)
    {
        /* Feature levels and the meta command query carry pointers */
        if (Sizes[Feature] == 0 || Feature == D3D12_FEATURE_FEATURE_LEVELS ||
            Feature == D3D12_FEATURE_QUERY_META_COMMAND)
        {
            continue;
        }

        ZeroMemory(Buffer, sizeof(Buffer));
        if (Feature == D3D12_FEATURE_FORMAT_SUPPORT || Feature == D3D12_FEATURE_FORMAT_INFO ||
            Feature == D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS)
        {
            reinterpret_cast<UINT*>(Buffer)[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            reinterpret_cast<UINT*>(Buffer)[1] = 4;
        }
        else if (Feature == D3D12_FEATURE_SHADER_MODEL)
        {
            reinterpret_cast<UINT*>(Buffer)[0] = D3D_SHADER_MODEL_6_9;
        }
        else if (Feature == D3D12_FEATURE_ROOT_SIGNATURE)
        {
            reinterpret_cast<UINT*>(Buffer)[0] = D3D_ROOT_SIGNATURE_VERSION_1_2;
        }
        else if (Feature == D3D12_FEATURE_COMMAND_QUEUE_PRIORITY)
        {
            reinterpret_cast<UINT*>(Buffer)[1] = D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME;
        }
        else if (Feature == D3D12_FEATURE_PLACED_RESOURCE_SUPPORT_INFO)
        {
            D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO* Placed =
                reinterpret_cast<D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO*>(Buffer);

            Placed->Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            Placed->Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            Placed->DestHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        }

        sprintf_s(Phase, "CheckFeatureSupport %u", Feature);
        TrPhase(Phase);
        printf("result %08X\n", (UINT)Device->CheckFeatureSupport((D3D12_FEATURE)Feature, Buffer, Sizes[Feature]));
    }
}

/* Root signatures of each version, as the driver is handed them */
static
VOID
TrScenarioRootSignatures(
    _In_ TrRun& Run)
{
    ComPtr<ID3D12Device> Device;
    D3D12_DESCRIPTOR_RANGE Ranges10[] = {
        { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND },
        { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND },
        { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, UINT_MAX, 3, 1, 8 },
    };
    D3D12_DESCRIPTOR_RANGE SamplerRange10 = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 0, 0 };
    D3D12_ROOT_PARAMETER Parameters10[5] = {};
    D3D12_STATIC_SAMPLER_DESC Sampler = {};
    D3D12_DESCRIPTOR_RANGE1 Ranges11[] = {
        { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0 },
        { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 4, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 4 },
        { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 8 },
        { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, 9 },
        { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, UINT_MAX, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, 10 },
        { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UINT_MAX, 1, 0,
          D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS, 11 },
    };
    D3D12_DESCRIPTOR_RANGE1 SamplerRange11 = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0,
                                               D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0 };
    D3D12_ROOT_PARAMETER1 Parameters11[6] = {};
    D3D12_STATIC_SAMPLER_DESC1 Sampler1 = {};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC Desc = {};

    if (FAILED(Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))) ||
        Run.SerializeVersioned == nullptr)
    {
        return;
    }

    Parameters10[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters10[0].DescriptorTable = { _countof(Ranges10), Ranges10 };
    Parameters10[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters10[1].DescriptorTable = { 1, &SamplerRange10 };
    Parameters10[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    Parameters10[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    Parameters10[2].Constants = { 2, 0, 4 };
    Parameters10[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    Parameters10[3].Descriptor = { 5, 0 };
    Parameters10[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    Parameters10[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    Parameters10[4].Descriptor = { 6, 2 };
    Sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    Sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    Sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    Sampler.MaxAnisotropy = 16;
    Sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
    Sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    Sampler.MaxLOD = D3D12_FLOAT32_MAX;
    Sampler.ShaderRegister = 3;
    Sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    Parameters11[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters11[0].DescriptorTable = { _countof(Ranges11), Ranges11 };
    Parameters11[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters11[1].DescriptorTable = { 1, &SamplerRange11 };
    Parameters11[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    Parameters11[2].Descriptor = { 0, 1, D3D12_ROOT_DESCRIPTOR_FLAG_NONE };
    Parameters11[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    Parameters11[3].Descriptor = { 0, 1, D3D12_ROOT_DESCRIPTOR_FLAG_NONE };
    Parameters11[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    Parameters11[4].Descriptor = { 0, 1, D3D12_ROOT_DESCRIPTOR_FLAG_NONE };
    Parameters11[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    Parameters11[5].Descriptor = { 1, 1, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC };
    CopyMemory(&Sampler1, &Sampler, sizeof(Sampler));
    Sampler1.Flags = D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR;
    Sampler1.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT;

    for (UINT Version = D3D_ROOT_SIGNATURE_VERSION_1_0; Version <= D3D_ROOT_SIGNATURE_VERSION_1_2; Version++)
    {
        ComPtr<ID3DBlob> Blob;
        ComPtr<ID3DBlob> Errors;
        ComPtr<ID3D12RootSignature> First;
        ComPtr<ID3D12RootSignature> Second;
        CHAR Phase[64];

        Desc = {};
        Desc.Version = (D3D_ROOT_SIGNATURE_VERSION)Version;
        if (Version == D3D_ROOT_SIGNATURE_VERSION_1_0)
        {
            Desc.Desc_1_0 = { _countof(Parameters10), Parameters10, 1, &Sampler,
                              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
        }
        else if (Version == D3D_ROOT_SIGNATURE_VERSION_1_1)
        {
            Desc.Desc_1_1 = { _countof(Parameters11), Parameters11, 1, &Sampler,
                              D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS };
        }
        else
        {
            Desc.Desc_1_2 = { _countof(Parameters11), Parameters11, 1, &Sampler1,
                              D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED };
        }
        if (FAILED(Run.SerializeVersioned(&Desc, &Blob, &Errors)))
        {
            printf("serialize 1.%u failed\n", Version - 1);
            continue;
        }

        sprintf_s(Phase, "CreateRootSignature 1.%u", Version - 1);
        TrPhase(Phase);
        printf("result %08X\n", (UINT)Device->CreateRootSignature(0, Blob->GetBufferPointer(), Blob->GetBufferSize(),
                                                                  IID_PPV_ARGS(&First)));
        TrPhase("CreateRootSignature same blob");
        printf("result %08X\n", (UINT)Device->CreateRootSignature(1, Blob->GetBufferPointer(), Blob->GetBufferSize(),
                                                                  IID_PPV_ARGS(&Second)));
        printf("same %u\n", First.Get() == Second.Get());
        TrPhase("Release");
        First.Reset();
        Second.Reset();
    }
}

/* A vertex shader that reads some of the elements below; the normal is left unread */
static const CHAR TrLayoutShader[] =
    "struct In { float4 Color : COLOR; float3 Position : POSITION; float2 Uv : TEXCOORD0; uint Id : BLENDINDICES; };\n"
    "float4 main(In I) : SV_Position { return float4(I.Position + I.Color.xyz, I.Uv.x + (float)I.Id); }\n";

static const D3D12_INPUT_ELEMENT_DESC TrLayout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 4, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 2 },
    { "blendindices", 0, DXGI_FORMAT_R8_UINT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 2 },
};

/* Pipeline states and the shaders and states shared between them */
static
VOID
TrScenarioPipelines(
    _In_ TrRun& Run)
{
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12RootSignature> Root;
    ComPtr<ID3DBlob> Blob;
    ComPtr<ID3DBlob> Errors;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC RootDesc = {};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC Graphics = {};
    D3D12_COMPUTE_PIPELINE_STATE_DESC Compute = {};
    ComPtr<ID3D12PipelineState> States[6];
    ComPtr<ID3D12PipelineState> Layouts[4];
    D3D12_GRAPHICS_PIPELINE_STATE_DESC Valid = {};
    D3D12_INPUT_ELEMENT_DESC Mixed[_countof(TrLayout)];
    ComPtr<ID3DBlob> Vertex;

    if (FAILED(Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))) ||
        Run.SerializeVersioned == nullptr)
    {
        return;
    }

    RootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    RootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (FAILED(Run.SerializeVersioned(&RootDesc, &Blob, &Errors)))
        return;
    TrPhase("CreateRootSignature");
    printf("result %08X\n", (UINT)Device->CreateRootSignature(0, Blob->GetBufferPointer(), Blob->GetBufferSize(),
                                                              IID_PPV_ARGS(&Root)));
    if (Root == nullptr)
        return;

    Graphics.pRootSignature = Root.Get();
    Graphics.VS = { RxInternalVertexDxil, sizeof(RxInternalVertexDxil) };
    for (D3D12_RENDER_TARGET_BLEND_DESC& Target : Graphics.BlendState.RenderTarget)
    {
        Target.SrcBlend = D3D12_BLEND_ONE;
        Target.DestBlend = D3D12_BLEND_ZERO;
        Target.BlendOp = D3D12_BLEND_OP_ADD;
        Target.SrcBlendAlpha = D3D12_BLEND_ONE;
        Target.DestBlendAlpha = D3D12_BLEND_ZERO;
        Target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        Target.LogicOp = D3D12_LOGIC_OP_NOOP;
        Target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    Graphics.SampleMask = UINT_MAX;
    Graphics.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    Graphics.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    Graphics.RasterizerState.DepthClipEnable = TRUE;
    Graphics.DepthStencilState.DepthEnable = TRUE;
    Graphics.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    Graphics.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    Graphics.DepthStencilState.StencilReadMask = 0xFF;
    Graphics.DepthStencilState.StencilWriteMask = 0xFF;
    for (D3D12_DEPTH_STENCILOP_DESC* Face : { &Graphics.DepthStencilState.FrontFace,
                                              &Graphics.DepthStencilState.BackFace })
    {
        Face->StencilFailOp = D3D12_STENCIL_OP_KEEP;
        Face->StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        Face->StencilPassOp = D3D12_STENCIL_OP_KEEP;
        Face->StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    }
    Graphics.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Graphics.NumRenderTargets = 1;
    Graphics.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    Graphics.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    Graphics.SampleDesc.Count = 1;

    TrPhase("CreateGraphicsPipelineState");
    printf("result %08X\n", (UINT)Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&States[0])));
    TrPhase("CreateGraphicsPipelineState same desc");
    printf("result %08X\n", (UINT)Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&States[1])));
    Valid = Graphics;

    Graphics.BlendState.IndependentBlendEnable = TRUE;
    Graphics.BlendState.RenderTarget[0].BlendEnable = TRUE;
    Graphics.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    Graphics.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    Graphics.BlendState.RenderTarget[1].LogicOpEnable = TRUE;
    Graphics.BlendState.RenderTarget[1].LogicOp = D3D12_LOGIC_OP_XOR;
    Graphics.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    Graphics.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
    Graphics.RasterizerState.DepthBias = 4;
    Graphics.RasterizerState.MultisampleEnable = TRUE;
    Graphics.DepthStencilState.StencilEnable = TRUE;
    Graphics.DepthStencilState.StencilReadMask = 0x0F;
    Graphics.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
    Graphics.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_ZERO;
    TrPhase("CreateGraphicsPipelineState other states");
    printf("result %08X\n", (UINT)Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&States[2])));

    Graphics.VS = { RxInternalVertexDxbc, sizeof(RxInternalVertexDxbc) };
    TrPhase("CreateGraphicsPipelineState dxbc");
    printf("result %08X\n", (UINT)Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&States[3])));

    Compute.pRootSignature = Root.Get();
    Compute.CS = { RxInternalComputeDxil, sizeof(RxInternalComputeDxil) };
    TrPhase("CreateComputePipelineState");
    printf("result %08X\n", (UINT)Device->CreateComputePipelineState(&Compute, IID_PPV_ARGS(&States[4])));
    Compute.CS = { RxInternalComputeDxbc, sizeof(RxInternalComputeDxbc) };
    TrPhase("CreateComputePipelineState dxbc");
    printf("result %08X\n", (UINT)Device->CreateComputePipelineState(&Compute, IID_PPV_ARGS(&States[5])));

    if (SUCCEEDED(D3DCompile(TrLayoutShader, strlen(TrLayoutShader), "layout", nullptr, nullptr, "main", "vs_5_0", 0, 0,
                             &Vertex, &Errors)))
    {
        Graphics = Valid;
        Graphics.VS = { Vertex->GetBufferPointer(), Vertex->GetBufferSize() };
        Graphics.InputLayout = { TrLayout, _countof(TrLayout) };
        TrPhase("CreateGraphicsPipelineState input layout");
        printf("result %08X\n", (UINT)Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&Layouts[0])));
        TrPhase("CreateGraphicsPipelineState same input layout");
        printf("result %08X\n", (UINT)Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&Layouts[1])));
        memcpy(Mixed, TrLayout, sizeof(Mixed));
        Mixed[4].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        Mixed[4].InstanceDataStepRate = 0;
        Graphics.InputLayout = { Mixed, _countof(Mixed) };
        TrPhase("CreateGraphicsPipelineState input layout mixing steps in a slot");
        printf("result %08X\n", (UINT)Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&Layouts[2])));
        Graphics.InputLayout = { TrLayout + 1, 1 };
        TrPhase("CreateGraphicsPipelineState input layout feeding too little");
        printf("result %08X\n", (UINT)Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&Layouts[2])));
        Graphics.VS = { RxInternalVertexDxil, sizeof(RxInternalVertexDxil) };
        TrPhase("CreateGraphicsPipelineState input layout the shader does not read");
        printf("result %08X\n", (UINT)Device->CreateGraphicsPipelineState(&Graphics, IID_PPV_ARGS(&Layouts[3])));
        TrPhase("Release input layouts");
        for (ComPtr<ID3D12PipelineState>& State : Layouts)
            State.Reset();
    }

    TrPhase("Release");
    for (ComPtr<ID3D12PipelineState>& State : States)
        State.Reset();
    Root.Reset();
}

struct TrScenario
{
    PCSTR Name;
    VOID (*Run)(TrRun& Run);
};

static const TrScenario TrScenarios[] =
{
    { "device", TrScenarioDevice },
    { "objects", TrScenarioObjects },
    { "resources", TrScenarioResources },
    { "copy", TrScenarioCopy },
    { "features", TrScenarioFeatures },
    { "rootsigs", TrScenarioRootSignatures },
    { "pipelines", TrScenarioPipelines },
};

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    std::wstring Directory;
    std::vector<std::string> Wanted;
    PCWSTR Runtime = L"system";
    WCHAR Module[MAX_PATH];
    HMODULE D3d12;
    HMODULE Core;
    TrRun Run = {};
    PWSTR Slash;
    CHAR Name[64];

    setvbuf(stdout, nullptr, _IONBF, 0);
    Tr.MainThread = GetCurrentThreadId();
    InitializeSRWLock(&Tr.IdLock);
    TrRegisterAll();

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Index += 1;
            Runtime = argv[Index];
        }
        else if (_wcsicmp(argv[Index], L"-threads") == 0)
        {
            Tr.AllThreads = TRUE;
        }
        else if (_wcsicmp(argv[Index], L"-ddi") == 0)
        {
            Tr.DdiOnly = TRUE;
        }
        else if (_wcsicmp(argv[Index], L"-nokernel") == 0)
        {
            Tr.NoKernel = TRUE;
        }
        else
        {
            WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
            Wanted.push_back(Name);
        }
    }

    if (_wcsicmp(Runtime, L"system") == 0)
    {
        GetSystemDirectoryW(Module, _countof(Module));
        Directory = std::wstring(Module) + L"\\";
    }
    else if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = UNICODE_NULL;
        Directory = std::wstring(Module) + L"reactx\\";
    }
    else
    {
        Directory = std::wstring(Runtime) + L"\\";
    }

    /* The core is loaded first so its imports point at the wrappers before d3d12.dll uses it */
    Core = LoadLibraryExW((Directory + L"d3d12core.dll").c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    D3d12 = LoadLibraryExW((Directory + L"d3d12.dll").c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (Core == NULL || D3d12 == NULL)
    {
        fprintf(stderr, "error: cannot load the runtime\n");
        return 1;
    }
    TrPatchImports(Core);

    for (HMODULE Each : { D3d12, Core })
    {
        GetModuleFileNameW(Each, Module, _countof(Module));
        fwprintf(stderr, L"loaded %s\n", Module);
    }

    Run.CreateDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(D3d12, "D3D12CreateDevice"));
    Run.SerializeVersioned = reinterpret_cast<PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE>(
        GetProcAddress(D3d12, "D3D12SerializeVersionedRootSignature"));
    if (Run.CreateDevice == nullptr)
        return 1;

    for (const TrScenario& Scenario : TrScenarios)
    {
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Each : Wanted)
            IsWanted = IsWanted || Each == Scenario.Name;
        if (!IsWanted)
            continue;

        TrCloseLine();
        printf("### %s\n", Scenario.Name);
        Scenario.Run(Run);
    }

    TrCloseLine();
    fflush(stdout);

    /* Driver threads may still call in; leave without running their teardown */
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
