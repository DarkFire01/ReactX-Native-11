/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Wrappers that print the device DDI calls and the Direct3D 12 calls a driver makes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * Every wrapper takes twelve integer arguments and forwards all of them, which covers each wrapped
 * method on ARM64 and x64. Methods that return a structure are left alone. Objects print as the order
 * they first appeared in. The runtime hands its batch to whichever thread drains it first but never to
 * two at once, so lines print in one stream without naming the thread.
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>

#include <d3d9.h>
#include <d3d12.h>
#include <d3dkmthk.h>
#include <d3dumddi.h>

#include <stdio.h>
#include <string>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hooks.hpp"

typedef UINT_PTR (APIENTRY* PFN_CK_FORWARD)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR,
                                            UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);

enum CkFamily
{
    CkFamilyDdi,
    CkFamilyDevice,
    CkFamilyQueue,
    CkFamilyPrivate,
    CkFamilyCount
};

static constexpr UINT CkSlotLimit = 160;
static constexpr UINT CkArgumentCount = 12;

static const PCSTR CkDdiNames[] =
{
    "SetRenderState", "UpdateWInfo", "ValidateDevice", "SetTextureStageState", "SetTexture", "SetPixelShader",
    "SetPixelShaderConst", "SetStreamSourceUm", "SetIndices", "SetIndicesUm", "DrawPrimitive",
    "DrawIndexedPrimitive", "DrawRectPatch", "DrawTriPatch", "DrawPrimitive2", "DrawIndexedPrimitive2", "VolBlt",
    "BufBlt", "TexBlt", "StateSet", "SetPriority", "Clear", "UpdatePalette", "SetPalette", "SetVertexShaderConst",
    "MultiplyTransform", "SetTransform", "SetViewport", "SetZRange", "SetMaterial", "SetLight", "CreateLight",
    "DestroyLight", "SetClipPlane", "GetInfo", "Lock", "Unlock", "CreateResource", "DestroyResource",
    "SetDisplayMode", "Present", "Flush", "CreateVertexShaderFunc", "DeleteVertexShaderFunc",
    "SetVertexShaderFunc", "CreateVertexShaderDecl", "DeleteVertexShaderDecl", "SetVertexShaderDecl",
    "SetVertexShaderConstI", "SetVertexShaderConstB", "SetScissorRect", "SetStreamSource", "SetStreamSourceFreq",
    "SetConvolutionKernelMono", "ComposeRects", "Blt", "ColorFill", "DepthFill", "CreateQuery", "DestroyQuery",
    "IssueQuery", "GetQueryData", "SetRenderTarget", "SetDepthStencil", "GenerateMipSubLevels",
    "SetPixelShaderConstI", "SetPixelShaderConstB", "CreatePixelShader", "DeletePixelShader", "CreateDecodeDevice",
    "DestroyDecodeDevice", "SetDecodeRenderTarget", "DecodeBeginFrame", "DecodeEndFrame", "DecodeExecute",
    "DecodeExtensionExecute", "CreateVideoProcessDevice", "DestroyVideoProcessDevice", "VideoProcessBeginFrame",
    "VideoProcessEndFrame", "SetVideoProcessRenderTarget", "VideoProcessBlt", "CreateExtensionDevice",
    "DestroyExtensionDevice", "ExtensionExecute", "CreateOverlay", "UpdateOverlay", "FlipOverlay",
    "GetOverlayColorControls", "SetOverlayColorControls", "DestroyOverlay", "DestroyDevice",
    "QueryResourceResidency", "OpenResource", "GetCaptureAllocationHandle", "CaptureToSysMem", "LockAsync",
    "UnlockAsync", "Rename", "CreateVideoProcessor", "SetVideoProcessBltState", "GetVideoProcessBltStatePrivate",
    "SetVideoProcessStreamState", "GetVideoProcessStreamStatePrivate", "VideoProcessBltHD",
    "DestroyVideoProcessor", "CreateAuthenticatedChannel", "AuthenticatedChannelKeyExchange",
    "QueryAuthenticatedChannel", "ConfigureAuthenticatedChannel", "DestroyAuthenticatedChannel",
    "CreateCryptoSession", "CryptoSessionKeyExchange", "DestroyCryptoSession", "EncryptionBlt", "GetPitch",
    "StartSessionKeyRefresh", "FinishSessionKeyRefresh", "GetEncryptionBltKey", "DecryptionBlt",
    "ResolveSharedResource", "VolBlt1", "BufBlt1", "TexBlt1", "Discard", "OfferResources", "ReclaimResources",
    "CheckDirectFlipSupport", "CreateResource2", "CheckMultiPlaneOverlaySupport", "PresentMultiPlaneOverlay",
    "Reserved1", "Flush1", "CheckCounterInfo", "CheckCounter", "UpdateSubresourceUP", "Present1",
    "CheckPresentDurationSupport", "SetMarker", "SetMarkerMode", "TrimResidencySet", "AcquireResource",
    "ReleaseResource",
};

/* ID3D12Device through ID3D12Device10, by vtable slot; null names are not wrapped */
static const PCSTR CkDeviceNames[] =
{
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "SetName", nullptr, "CreateCommandQueue",
    "CreateCommandAllocator", "CreateGraphicsPipelineState", "CreateComputePipelineState", "CreateCommandList",
    "CheckFeatureSupport", "CreateDescriptorHeap", nullptr, "CreateRootSignature", "CreateConstantBufferView",
    "CreateShaderResourceView", "CreateUnorderedAccessView", "CreateRenderTargetView", "CreateDepthStencilView",
    "CreateSampler", "CopyDescriptors", "CopyDescriptorsSimple", nullptr, nullptr, "CreateCommittedResource",
    "CreateHeap", "CreatePlacedResource", "CreateReservedResource", "CreateSharedHandle", "OpenSharedHandle",
    "OpenSharedHandleByName", "MakeResident", "Evict", "CreateFence", "GetDeviceRemovedReason",
    "GetCopyableFootprints", "CreateQueryHeap", "SetStablePowerState", "CreateCommandSignature",
    "GetResourceTiling", nullptr, "CreatePipelineLibrary", "SetEventOnMultipleFenceCompletion",
    "SetResidencyPriority", "CreatePipelineState", "OpenExistingHeapFromAddress", "OpenExistingHeapFromFileMapping",
    "EnqueueMakeResident", "CreateCommandList1", "CreateProtectedResourceSession", "CreateCommittedResource1",
    "CreateHeap1", "CreateReservedResource1", nullptr, "CreateLifetimeTracker", "RemoveDevice",
    "EnumerateMetaCommands", "EnumerateMetaCommandParameters", "CreateMetaCommand", "CreateStateObject",
    "GetRaytracingAccelerationStructurePrebuildInfo", "CheckDriverMatchingIdentifier",
    "SetBackgroundProcessingMode", "AddToStateObject", "CreateProtectedResourceSession1", nullptr,
    "CreateCommittedResource2", "CreatePlacedResource1", "CreateSamplerFeedbackUnorderedAccessView",
    "GetCopyableFootprints1", "CreateShaderCacheSession", "ShaderCacheControl", "CreateCommandQueue1",
    "CreateCommittedResource3", "CreatePlacedResource2", "CreateReservedResource2",
};

/* The private table d3d9.dll looks up; the opens are wrapped apart */
static const PCSTR CkPrivateNames[] =
{
    nullptr, "GetSharedGdiHandle", "CreateSharedNtHandle", "GetExecutionState", "KernelPresent", "CreateFence",
    "OpenFence", "ShareFence", "WaitForFence", "SignalFence", "GetCompletedFenceValue", "DestroyTrackedFence",
    "QueryResourceDriverDataSize", "OpenResource", "CreateKmtHandle", "QueryResourceFromKmtHandle",
    "DestroyKmtHandle", "CreateWrappingHandle", "DestroyWrappingHandle", "GetD3D12Device", "TransitionResource",
    "SetResourceState", "SetMaximumFrameLatency", "IsMaximumFrameLatencyReached", "GetD3D12Resource",
    "AddResourceWaitsToQueue", "AddDeferredWaitsToResource", nullptr, "SubmitForPresent", "PreExecuteCommandList",
    "PostExecuteCommandList",
};

static const PCSTR CkQueueNames[] =
{
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "SetName", nullptr, "UpdateTileMappings",
    "CopyTileMappings", "ExecuteCommandLists", "SetMarker", "BeginEvent", "EndEvent", "Signal", "Wait",
    "GetTimestampFrequency", "GetClockCalibration",
};

struct CkHookState
{
    PFN_CK_FORWARD Originals[CkFamilyCount][CkSlotLimit];
    std::unordered_map<const VOID*, std::string> Ids;
    std::map<std::string, UINT> StateCalls;
    UINT NextId;
    UINT Printed;
    BOOLEAN Polling;
    SRWLOCK Lock;
};

static CkHookState CkHooks = { {}, {}, {}, 0, 0, FALSE, SRWLOCK_INIT };

/* How deep in wrapped calls the current thread is, for indenting the calls a call makes */
static thread_local UINT CkDepth;

/* The device DDI entry the current thread is inside, or none */
static thread_local UINT CkDdiSlot = ~0u;

/* A name for a pointer: the order it first appeared in */
static
std::string
CkName(
    _In_opt_ const VOID* Object)
{
    std::string Name;
    char Buffer[32];

    if (Object == nullptr)
        return "0";

    AcquireSRWLockExclusive(&CkHooks.Lock);
    auto Found = CkHooks.Ids.find(Object);
    if (Found != CkHooks.Ids.end())
    {
        Name = Found->second;
    }
    else
    {
        CkHooks.NextId += 1;
        sprintf_s(Buffer, "%u", CkHooks.NextId);
        Name = Buffer;
        CkHooks.Ids[Object] = Name;
    }
    ReleaseSRWLockExclusive(&CkHooks.Lock);
    return Name;
}

UINT
CkObjectId(
    _In_opt_ const VOID* Object)
{
    return static_cast<UINT>(strtoul(CkName(Object).c_str(), nullptr, 10));
}
/* A new name for an object an out pointer received, since a freed address can come back */
static
std::string
CkOutId(
    _In_ UINT_PTR Out)
{
    const VOID* Object;

    if (Out == 0 || (Object = *reinterpret_cast<VOID* const*>(Out)) == nullptr)
        return "0";

    AcquireSRWLockExclusive(&CkHooks.Lock);
    CkHooks.Ids.erase(Object);
    ReleaseSRWLockExclusive(&CkHooks.Lock);
    return CkName(Object);
}

static
VOID
CkAppend(
    _Inout_ std::string* Text,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    char Buffer[512];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf_s(Buffer, _countof(Buffer), _TRUNCATE, Format, Arguments);
    va_end(Arguments);
    *Text += Buffer;
}

static
VOID
CkDescribeResource(
    _Inout_ std::string* Text,
    _In_opt_ const D3D12_RESOURCE_DESC* Desc)
{
    if (Desc == nullptr)
        return;
    CkAppend(Text, " dim %u align %llu size %llux%ux%u mips %u format %u samples %u/%u layout %u flags %X",
             Desc->Dimension, Desc->Alignment, Desc->Width, Desc->Height, Desc->DepthOrArraySize, Desc->MipLevels,
             Desc->Format, Desc->SampleDesc.Count, Desc->SampleDesc.Quality, Desc->Layout, Desc->Flags);
}

static
VOID
CkDescribeHeap(
    _Inout_ std::string* Text,
    _In_opt_ const D3D12_HEAP_PROPERTIES* Heap,
    _In_ UINT_PTR Flags)
{
    if (Heap == nullptr)
        return;
    CkAppend(Text, " heap %u cpu %u pool %u flags %X", Heap->Type, Heap->CPUPageProperty, Heap->MemoryPoolPreference,
             static_cast<UINT>(Flags));
}

/* A short fingerprint of bytes, so shader code and root signatures compare in one word */
static
UINT
CkHashBytes(
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    UINT Hash = 0x811C9DC5;

    for (SIZE_T Index = 0; Index < Size; Index += 1)
        Hash = (Hash ^ Bytes[Index]) * 0x01000193;
    return Hash;
}

/* Prints the size and fingerprint of code, and every word of it when CK_DXBC is set */
static
VOID
CkDescribeBytecode(
    _Inout_ std::string* Text,
    _In_z_ PCSTR Label,
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    static const BOOL Full = GetEnvironmentVariableA("CK_DXBC", nullptr, 0) != 0;

    if (Data == nullptr || Size == 0)
        return;
    CkAppend(Text, " %s %zu/%08X", Label, Size, CkHashBytes(Data, Size));
    if (!Full)
        return;
    for (SIZE_T Offset = 0; Offset + 4 <= Size; Offset += 4)
    {
        if (Offset % 32 == 0)
            CkAppend(Text, "\n    %s %04zX:", Label, Offset);
        CkAppend(Text, " %08X", *reinterpret_cast<const UINT*>(static_cast<const BYTE*>(Data) + Offset));
    }
    CkAppend(Text, "\n   ");
}

static
VOID
CkDescribeGraphicsPipeline(
    _Inout_ std::string* Text,
    _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC* Desc)
{
    const D3D12_RASTERIZER_DESC* Raster = &Desc->RasterizerState;
    const D3D12_DEPTH_STENCIL_DESC* Depth = &Desc->DepthStencilState;

    CkAppend(Text, " root %s", CkName(Desc->pRootSignature).c_str());
    CkDescribeBytecode(Text, "vs", Desc->VS.pShaderBytecode, Desc->VS.BytecodeLength);
    CkDescribeBytecode(Text, "ps", Desc->PS.pShaderBytecode, Desc->PS.BytecodeLength);
    CkDescribeBytecode(Text, "gs", Desc->GS.pShaderBytecode, Desc->GS.BytecodeLength);
    CkDescribeBytecode(Text, "hs", Desc->HS.pShaderBytecode, Desc->HS.BytecodeLength);
    CkDescribeBytecode(Text, "ds", Desc->DS.pShaderBytecode, Desc->DS.BytecodeLength);
    CkAppend(Text, " so %u topology %u targets %u ds %u samples %u/%u mask %X cut %u flags %X", Desc->StreamOutput.NumEntries,
             Desc->PrimitiveTopologyType, Desc->NumRenderTargets, Desc->DSVFormat, Desc->SampleDesc.Count,
             Desc->SampleDesc.Quality, Desc->SampleMask, Desc->IBStripCutValue, Desc->Flags);
    CkAppend(Text, " rtv");
    for (UINT Index = 0; Index < Desc->NumRenderTargets && Index < 8; Index += 1)
        CkAppend(Text, " %u", Desc->RTVFormats[Index]);
    CkAppend(Text, " blend %u %u", Desc->BlendState.AlphaToCoverageEnable, Desc->BlendState.IndependentBlendEnable);
    for (UINT Index = 0; Index < 8; Index += 1)
    {
        const D3D12_RENDER_TARGET_BLEND_DESC* Blend = &Desc->BlendState.RenderTarget[Index];

        CkAppend(Text, " [%u %u %u %u %u %u %u %u %u %u]", Blend->BlendEnable, Blend->LogicOpEnable, Blend->SrcBlend,
                 Blend->DestBlend, Blend->BlendOp, Blend->SrcBlendAlpha, Blend->DestBlendAlpha, Blend->BlendOpAlpha,
                 Blend->LogicOp, Blend->RenderTargetWriteMask);
    }
    CkAppend(Text, " raster %u %u %u %d %g %g %u %u %u %u %u", Raster->FillMode, Raster->CullMode,
             Raster->FrontCounterClockwise, Raster->DepthBias, Raster->DepthBiasClamp, Raster->SlopeScaledDepthBias,
             Raster->DepthClipEnable, Raster->MultisampleEnable, Raster->AntialiasedLineEnable, Raster->ForcedSampleCount,
             Raster->ConservativeRaster);
    CkAppend(Text, " depth %u %u %u stencil %u %X %X [%u %u %u %u] [%u %u %u %u]", Depth->DepthEnable,
             Depth->DepthWriteMask, Depth->DepthFunc, Depth->StencilEnable, Depth->StencilReadMask,
             Depth->StencilWriteMask, Depth->FrontFace.StencilFailOp, Depth->FrontFace.StencilDepthFailOp,
             Depth->FrontFace.StencilPassOp, Depth->FrontFace.StencilFunc, Depth->BackFace.StencilFailOp,
             Depth->BackFace.StencilDepthFailOp, Depth->BackFace.StencilPassOp, Depth->BackFace.StencilFunc);
    CkAppend(Text, " inputs %u", Desc->InputLayout.NumElements);
    for (UINT Index = 0; Index < Desc->InputLayout.NumElements; Index += 1)
    {
        const D3D12_INPUT_ELEMENT_DESC* Element = &Desc->InputLayout.pInputElementDescs[Index];

        CkAppend(Text, " [%s %u %u %u %u %u %u]", Element->SemanticName, Element->SemanticIndex, Element->Format,
                 Element->InputSlot, Element->AlignedByteOffset, Element->InputSlotClass, Element->InstanceDataStepRate);
    }
}

/* The arguments of a Direct3D 12 device method that say what it makes */
static
VOID
CkDescribeDeviceCall(
    _Inout_ std::string* Text,
    _In_ UINT Slot,
    _In_reads_(CkArgumentCount) const UINT_PTR* A)
{
    switch (Slot)
    {
        case 8:
        {
            const D3D12_COMMAND_QUEUE_DESC* Desc = reinterpret_cast<const D3D12_COMMAND_QUEUE_DESC*>(A[1]);

            CkAppend(Text, " type %u priority %d flags %X", Desc->Type, Desc->Priority, Desc->Flags);
            break;
        }
        case 9:
            CkAppend(Text, " type %u", static_cast<UINT>(A[1]));
            break;
        case 10:
            CkDescribeGraphicsPipeline(Text, reinterpret_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(A[1]));
            break;
        case 11:
        {
            const D3D12_COMPUTE_PIPELINE_STATE_DESC* Desc =
                reinterpret_cast<const D3D12_COMPUTE_PIPELINE_STATE_DESC*>(A[1]);

            CkAppend(Text, " root %s cs %zu", CkName(Desc->pRootSignature).c_str(), Desc->CS.BytecodeLength);
            break;
        }
        case 12:
            CkAppend(Text, " node %u type %u allocator %s state %s", static_cast<UINT>(A[1]), static_cast<UINT>(A[2]),
                     CkName(reinterpret_cast<VOID*>(A[3])).c_str(), CkName(reinterpret_cast<VOID*>(A[4])).c_str());
            break;
        case 13:
            CkAppend(Text, " feature %u size %u", static_cast<UINT>(A[1]), static_cast<UINT>(A[3]));
            break;
        case 14:
        {
            const D3D12_DESCRIPTOR_HEAP_DESC* Desc = reinterpret_cast<const D3D12_DESCRIPTOR_HEAP_DESC*>(A[1]);

            CkAppend(Text, " type %u count %u flags %X", Desc->Type, Desc->NumDescriptors, Desc->Flags);
            break;
        }
        case 16:
            CkAppend(Text, " node %u", static_cast<UINT>(A[1]));
            CkDescribeBytecode(Text, "blob", reinterpret_cast<const VOID*>(A[2]), static_cast<SIZE_T>(A[3]));
            break;
        case 18:
            CkAppend(Text, " resource %s", CkName(reinterpret_cast<VOID*>(A[1])).c_str());
            if (A[2] != 0)
            {
                const D3D12_SHADER_RESOURCE_VIEW_DESC* Desc = reinterpret_cast<const D3D12_SHADER_RESOURCE_VIEW_DESC*>(A[2]);

                CkAppend(Text, " format %u dim %u mapping %X raw %08X %08X %08X %08X", Desc->Format, Desc->ViewDimension,
                         Desc->Shader4ComponentMapping, Desc->Texture2DArray.MostDetailedMip, Desc->Texture2DArray.MipLevels,
                         Desc->Texture2DArray.FirstArraySlice, Desc->Texture2DArray.ArraySize);
            }
            break;
        case 22:
        {
            const D3D12_SAMPLER_DESC* Desc = reinterpret_cast<const D3D12_SAMPLER_DESC*>(A[1]);

            CkAppend(Text, " filter %X address %u %u %u bias %g aniso %u compare %u border %g %g %g %g lod %g %g",
                     Desc->Filter, Desc->AddressU, Desc->AddressV, Desc->AddressW, Desc->MipLODBias, Desc->MaxAnisotropy,
                     Desc->ComparisonFunc, Desc->BorderColor[0], Desc->BorderColor[1], Desc->BorderColor[2],
                     Desc->BorderColor[3], Desc->MinLOD, Desc->MaxLOD);
            break;
        }
        case 19:
        case 20:
        case 21:
            CkAppend(Text, " resource %s", CkName(reinterpret_cast<VOID*>(A[1])).c_str());
            break;
        case 23:
            CkAppend(Text, " ranges %u/%u type %u", static_cast<UINT>(A[1]), static_cast<UINT>(A[4]),
                     static_cast<UINT>(A[7]));
            break;
        case 24:
            CkAppend(Text, " count %u type %u", static_cast<UINT>(A[1]), static_cast<UINT>(A[4]));
            break;
        case 27:
        case 53:
            CkDescribeHeap(Text, reinterpret_cast<const D3D12_HEAP_PROPERTIES*>(A[1]), A[2]);
            CkDescribeResource(Text, reinterpret_cast<const D3D12_RESOURCE_DESC*>(A[3]));
            CkAppend(Text, " state %X clear %s", static_cast<UINT>(A[4]), A[5] != 0 ? "set" : "none");
            break;
        case 28:
        case 54:
        {
            const D3D12_HEAP_DESC* Desc = reinterpret_cast<const D3D12_HEAP_DESC*>(A[1]);

            CkAppend(Text, " size %llu", Desc->SizeInBytes);
            CkDescribeHeap(Text, &Desc->Properties, Desc->Flags);
            break;
        }
        case 29:
            CkAppend(Text, " heap %s offset %llu", CkName(reinterpret_cast<VOID*>(A[1])).c_str(),
                     static_cast<ULONGLONG>(A[2]));
            CkDescribeResource(Text, reinterpret_cast<const D3D12_RESOURCE_DESC*>(A[3]));
            CkAppend(Text, " state %X", static_cast<UINT>(A[4]));
            break;
        case 36:
            CkAppend(Text, " value %llu flags %X", static_cast<ULONGLONG>(A[1]), static_cast<UINT>(A[2]));
            break;
        case 39:
        {
            const D3D12_QUERY_HEAP_DESC* Desc = reinterpret_cast<const D3D12_QUERY_HEAP_DESC*>(A[1]);

            CkAppend(Text, " type %u count %u", Desc->Type, Desc->Count);
            break;
        }
        case 51:
            CkAppend(Text, " node %u type %u flags %X", static_cast<UINT>(A[1]), static_cast<UINT>(A[2]),
                     static_cast<UINT>(A[3]));
            break;
        default:
            break;
    }
}

static constexpr UINT CkDeviceCreateCommandAllocator = 9;
static constexpr UINT CkDeviceCreateCommittedResource = 27;

/*
 * The translation layer's upload heap grows by 64 KB buffers only while the ones it has are still
 * in flight, which depends on GPU timing. Outside resource creation such a buffer is one of those.
 */
static
BOOLEAN
CkIsUploadChunk(
    _In_reads_(CkArgumentCount) const UINT_PTR* A)
{
    const D3D12_HEAP_PROPERTIES* Heap = reinterpret_cast<const D3D12_HEAP_PROPERTIES*>(A[1]);
    const D3D12_RESOURCE_DESC* Desc = reinterpret_cast<const D3D12_RESOURCE_DESC*>(A[3]);

    if (CkDdiSlot == 37 || CkDdiSlot == 128 || Heap == nullptr || Desc == nullptr)
        return FALSE;
    return Desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && Desc->Width == 65536 &&
           Heap->Type == D3D12_HEAP_TYPE_CUSTOM && static_cast<UINT>(A[4]) == D3D12_RESOURCE_STATE_GENERIC_READ;
}

/* The object a Direct3D 12 device method made, read from its out pointer */
static
UINT_PTR
CkDeviceOutPointer(
    _In_ UINT Slot,
    _In_reads_(CkArgumentCount) const UINT_PTR* A)
{
    switch (Slot)
    {
        case 8:
        case 10:
        case 11:
        case 14:
        case 28:
        case 39:
            return A[3];
        case 9:
            return A[3];
        case 36:
            return A[4];
        case 12:
            return A[6];
        case 16:
            return A[5];
        case 27:
        case 29:
            return A[7];
        case 51:
            return A[5];
        case 53:
            return A[8];
        default:
            return 0;
    }
}

static
VOID
CkDescribeQueueCall(
    _Inout_ std::string* Text,
    _In_ UINT Slot,
    _In_reads_(CkArgumentCount) const UINT_PTR* A)
{
    if (Slot == 10)
    {
        ID3D12CommandList* const* Lists = reinterpret_cast<ID3D12CommandList* const*>(A[2]);

        CkAppend(Text, " lists");
        for (UINT Index = 0; Index < static_cast<UINT>(A[1]); Index++)
            CkAppend(Text, " %s", CkName(Lists[Index]).c_str());
    }
    else if (Slot == 14 || Slot == 15)
    {
        CkAppend(Text, " fence %s value %llu", CkName(reinterpret_cast<VOID*>(A[1])).c_str(), static_cast<ULONGLONG>(A[2]));
    }
}

/* Slots of the device DDI table whose arguments are printed */
enum CkDdiSlot
{
    CkDdiLock = 35,
    CkDdiUnlock = 36,
    CkDdiDestroyResource = 38,
    CkDdiCreateQuery = 58,
    CkDdiDestroyQuery = 59,
    CkDdiIssueQuery = 60,
    CkDdiGetQueryData = 61,
    CkDdiFlush1 = 132,
    CkDdiLockAsync = 96,
    CkDdiUnlockAsync = 97,
    CkDdiRename = 98,
    CkDdiCreateResource2 = 128,
};

/* The arguments of a device DDI call the runtime made */
static
VOID
CkDescribeDdiCall(
    _Inout_ std::string* Text,
    _In_ UINT Slot,
    _In_reads_(CkArgumentCount) const UINT_PTR* A)
{
    switch (Slot)
    {
        case 4:
            CkAppend(Text, " stage %u texture %s", static_cast<UINT>(A[1]), CkName(reinterpret_cast<VOID*>(A[2])).c_str());
            break;
        case 7:
        {
            const D3DDDIARG_SETSTREAMSOURCEUM* Args = reinterpret_cast<const D3DDDIARG_SETSTREAMSOURCEUM*>(A[1]);

            CkAppend(Text, " stream %u stride %u data %s", Args->Stream, Args->Stride, A[2] != 0 ? "set" : "none");
            break;
        }
        case 8:
        {
            const D3DDDIARG_SETINDICES* Args = reinterpret_cast<const D3DDDIARG_SETINDICES*>(A[1]);

            CkAppend(Text, " buffer %s stride %u", CkName(Args->hIndexBuffer).c_str(), Args->Stride);
            break;
        }
        case 9:
            CkAppend(Text, " size %u data %s", static_cast<UINT>(A[1]), A[2] != 0 ? "set" : "none");
            break;
        case 10:
        {
            const D3DDDIARG_DRAWPRIMITIVE* Args = reinterpret_cast<const D3DDDIARG_DRAWPRIMITIVE*>(A[1]);

            CkAppend(Text, " type %u start %u count %u flags %s", Args->PrimitiveType, Args->VStart, Args->PrimitiveCount,
                     A[2] != 0 ? "set" : "none");
            break;
        }
        case 11:
        {
            const D3DDDIARG_DRAWINDEXEDPRIMITIVE* Args = reinterpret_cast<const D3DDDIARG_DRAWINDEXEDPRIMITIVE*>(A[1]);

            CkAppend(Text, " type %u base %d min %u vertices %u start %u count %u", Args->PrimitiveType,
                     Args->BaseVertexIndex, Args->MinIndex, Args->NumVertices, Args->StartIndex, Args->PrimitiveCount);
            break;
        }
        case 14:
        {
            const D3DDDIARG_DRAWPRIMITIVE2* Args = reinterpret_cast<const D3DDDIARG_DRAWPRIMITIVE2*>(A[1]);

            CkAppend(Text, " type %u offset %u count %u", Args->PrimitiveType, Args->FirstVertexOffset,
                     Args->PrimitiveCount);
            break;
        }
        case 15:
        {
            const D3DDDIARG_DRAWINDEXEDPRIMITIVE2* Args = reinterpret_cast<const D3DDDIARG_DRAWINDEXEDPRIMITIVE2*>(A[1]);

            CkAppend(Text, " type %u base %d min %u vertices %u offset %u count %u size %u indices %s flags %s",
                     Args->PrimitiveType, Args->BaseVertexOffset, Args->MinIndex, Args->NumVertices,
                     Args->StartIndexOffset, Args->PrimitiveCount, static_cast<UINT>(A[2]), A[3] != 0 ? "set" : "none",
                     A[4] != 0 ? "set" : "none");
            break;
        }
        case 51:
        {
            const D3DDDIARG_SETSTREAMSOURCE* Args = reinterpret_cast<const D3DDDIARG_SETSTREAMSOURCE*>(A[1]);

            CkAppend(Text, " stream %u buffer %s offset %u stride %u", Args->Stream, CkName(Args->hVertexBuffer).c_str(),
                     Args->Offset, Args->Stride);
            break;
        }
        case CkDdiCreateResource2:
        {
            const D3DDDIARG_CREATERESOURCE2* Args = reinterpret_cast<const D3DDDIARG_CREATERESOURCE2*>(A[1]);

            CkAppend(Text, " format %X pool %u ms %u/%u surfaces %u mips %u fvf %X source %u flags %X rotation %u flags2 %X",
                     Args->Format, Args->Pool, Args->MultisampleType, Args->MultisampleQuality, Args->SurfCount,
                     Args->MipLevels, Args->Fvf, Args->VidPnSourceId, Args->Flags.Value, Args->Rotation,
                     Args->Flags2.Value);
            for (UINT Index = 0; Index < Args->SurfCount && Index < 4; Index++)
            {
                const D3DDDI_SURFACEINFO* Surface = &Args->pSurfList[Index];

                CkAppend(Text, " [%ux%ux%u mem %s pitch %u/%u]", Surface->Width, Surface->Height, Surface->Depth,
                         Surface->pSysMem != nullptr ? "set" : "none", Surface->SysMemPitch,
                         Surface->SysMemSlicePitch);
            }
            break;
        }
        case CkDdiLock:
        {
            const D3DDDIARG_LOCK* Args = reinterpret_cast<const D3DDDIARG_LOCK*>(A[1]);

            CkAppend(Text, " resource %s sub %u flags %X box %u %u %u %u %u %u", CkName(Args->hResource).c_str(),
                     Args->SubResourceIndex, Args->Flags.Value, Args->Box.Left, Args->Box.Top, Args->Box.Right,
                     Args->Box.Bottom, Args->Box.Front, Args->Box.Back);
            break;
        }
        case CkDdiUnlock:
        {
            const D3DDDIARG_UNLOCK* Args = reinterpret_cast<const D3DDDIARG_UNLOCK*>(A[1]);

            CkAppend(Text, " resource %s sub %u flags %X", CkName(Args->hResource).c_str(), Args->SubResourceIndex,
                     Args->Flags.Value);
            break;
        }
        case CkDdiDestroyResource:
            CkAppend(Text, " resource %s", CkName(reinterpret_cast<VOID*>(A[1])).c_str());
            break;
        case CkDdiCreateQuery:
            CkAppend(Text, " type %u", reinterpret_cast<const D3DDDIARG_CREATEQUERY*>(A[1])->QueryType);
            break;
        case CkDdiDestroyQuery:
            CkAppend(Text, " query %s", CkName(reinterpret_cast<VOID*>(A[1])).c_str());
            break;
        case CkDdiIssueQuery:
        {
            const D3DDDIARG_ISSUEQUERY* Args = reinterpret_cast<const D3DDDIARG_ISSUEQUERY*>(A[1]);

            CkAppend(Text, " query %s flags %X", CkName(Args->hQuery).c_str(), Args->Flags.Value);
            break;
        }
        case CkDdiGetQueryData:
            CkAppend(Text, " query %s", CkName(reinterpret_cast<const D3DDDIARG_GETQUERYDATA*>(A[1])->hQuery).c_str());
            break;
        case CkDdiLockAsync:
        {
            const D3DDDIARG_LOCKASYNC* Args = reinterpret_cast<const D3DDDIARG_LOCKASYNC*>(A[1]);

            CkAppend(Text, " resource %s sub %u flags %X box %u %u %u %u %u %u", CkName(Args->hResource).c_str(),
                     Args->SubResourceIndex, Args->Flags.Value, Args->Box.Left, Args->Box.Top, Args->Box.Right,
                     Args->Box.Bottom, Args->Box.Front, Args->Box.Back);
            break;
        }
        case CkDdiUnlockAsync:
        {
            const D3DDDIARG_UNLOCKASYNC* Args = reinterpret_cast<const D3DDDIARG_UNLOCKASYNC*>(A[1]);

            CkAppend(Text, " resource %s sub %u flags %X", CkName(Args->hResource).c_str(), Args->SubResourceIndex,
                     Args->Flags.Value);
            break;
        }
        case CkDdiRename:
        {
            const D3DDDIARG_RENAME* Args = reinterpret_cast<const D3DDDIARG_RENAME*>(A[1]);

            /* A cookie can take the address of a freed one, so it prints only whether it is there */
            CkAppend(Text, " resource %s sub %u cookie %s", CkName(Args->hResource).c_str(), Args->SubResourceIndex,
                     Args->hCookie != nullptr ? "set" : "none");
            break;
        }
        default:
            break;
    }
}

/* What a device DDI call handed back */
static
VOID
CkDescribeDdiResult(
    _Inout_ std::string* Text,
    _In_ UINT Slot,
    _In_reads_(CkArgumentCount) const UINT_PTR* A)
{
    switch (Slot)
    {
        case CkDdiCreateResource2:
        {
            const D3DDDIARG_CREATERESOURCE2* Args = reinterpret_cast<const D3DDDIARG_CREATERESOURCE2*>(A[1]);

            if (Args->hResource == reinterpret_cast<HANDLE>(1))
            {
                CkAppend(Text, " -> null");
            }
            else
            {
                /*
                 * The runtime keeps two buffers each for user memory vertices and indices and takes whichever
                 * the GPU is done with, so both get one name.
                 */
                BOOLEAN UserVertices = Args->Format == D3DDDIFMT_VERTEXDATA && Args->SurfCount == 1 &&
                                       Args->pSurfList[0].Width == 32768 && Args->Flags.Value == 0x80044;
                BOOLEAN UserIndices = Args->Format == D3DDDIFMT_INDEX16 && Args->SurfCount == 1 &&
                                      Args->pSurfList[0].Width == 16384 && Args->Flags.Value == 0x100044;

                AcquireSRWLockExclusive(&CkHooks.Lock);
                CkHooks.Ids.erase(Args->hResource);
                if (UserVertices || UserIndices)
                    CkHooks.Ids[Args->hResource] = UserVertices ? "user-vertices" : "user-indices";
                ReleaseSRWLockExclusive(&CkHooks.Lock);
                CkAppend(Text, " -> %s", CkName(Args->hResource).c_str());
            }
            break;
        }
        case CkDdiLock:
        {
            const D3DDDIARG_LOCK* Args = reinterpret_cast<const D3DDDIARG_LOCK*>(A[1]);

            CkAppend(Text, " -> data %s pitch %u slice %u", Args->pSurfData != nullptr ? "set" : "none", Args->Pitch,
                     Args->SlicePitch);
            break;
        }
        case CkDdiCreateQuery:
        {
            const D3DDDIARG_CREATEQUERY* Args = reinterpret_cast<const D3DDDIARG_CREATEQUERY*>(A[1]);

            AcquireSRWLockExclusive(&CkHooks.Lock);
            CkHooks.Ids.erase(Args->hQuery);
            ReleaseSRWLockExclusive(&CkHooks.Lock);
            CkAppend(Text, " -> %s", CkName(Args->hQuery).c_str());
            break;
        }
        case CkDdiLockAsync:
        {
            const D3DDDIARG_LOCKASYNC* Args = reinterpret_cast<const D3DDDIARG_LOCKASYNC*>(A[1]);

            CkAppend(Text, " -> data %s pitch %u slice %u cookie %s", Args->pSurfData != nullptr ? "set" : "none",
                     Args->Pitch, Args->SlicePitch, Args->hCookie != nullptr ? "set" : "none");
            break;
        }
        default:
            break;
    }
}

/* Private entries that hand back nothing */
static
BOOLEAN
CkPrivateHasResult(
    _In_ UINT Slot)
{
    return Slot != 11 && Slot != 18 && Slot != 20 && Slot != 21 && Slot != 22;
}

/* The arguments of a private entry the runtime called */
static
VOID
CkDescribePrivateCall(
    _Inout_ std::string* Text,
    _In_ UINT Slot,
    _In_reads_(CkArgumentCount) const UINT_PTR* A)
{
    switch (Slot)
    {
        case 13:
        {
            const D3DDDIARG_OPENRESOURCE* Args = reinterpret_cast<const D3DDDIARG_OPENRESOURCE*>(A[1]);

            CkAppend(Text, " allocations %u kmt %u data %u rotation %u flags %X", Args->NumAllocations,
                     Args->hKMResource, Args->PrivateDriverDataSize, Args->Rotation, Args->Flags.Value);
            break;
        }
        default:
            break;
    }
}

/* What a private entry handed back */
static
VOID
CkDescribePrivateResult(
    _Inout_ std::string* Text,
    _In_ UINT Slot,
    _In_reads_(CkArgumentCount) const UINT_PTR* A,
    _In_ UINT_PTR Result)
{
    if (Slot == 13 && SUCCEEDED(static_cast<HRESULT>(Result)))
    {
        const D3DDDIARG_OPENRESOURCE* Args = reinterpret_cast<const D3DDDIARG_OPENRESOURCE*>(A[1]);

        AcquireSRWLockExclusive(&CkHooks.Lock);
        CkHooks.Ids.erase(Args->hResource);
        ReleaseSRWLockExclusive(&CkHooks.Lock);
        CkAppend(Text, " -> %s", CkName(Args->hResource).c_str());
    }
}

/* Device DDI entries that only record state and make no Direct3D 12 calls */
static
BOOLEAN
CkIsStateSlot(
    _In_ UINT Slot)
{
    static const UINT StateSlots[] = { 0, 1, 3, 6, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 48, 49, 50, 65, 66 };

    for (UINT Index = 0; Index < _countof(StateSlots); Index++)
    {
        if (StateSlots[Index] == Slot)
            return TRUE;
    }
    return FALSE;
}

VOID
CkFlushState(VOID)
{
    AcquireSRWLockExclusive(&CkHooks.Lock);
    for (const auto& Call : CkHooks.StateCalls)
        printf("state %s x%u\n", Call.first.c_str(), Call.second);
    CkHooks.StateCalls.clear();
    ReleaseSRWLockExclusive(&CkHooks.Lock);
}

static
UINT_PTR
CkDispatch(
    _In_ CkFamily Family,
    _In_ UINT Slot,
    _In_reads_(CkArgumentCount) UINT_PTR* A)
{
    std::string Text;
    std::string Line;
    UINT_PTR Result;
    UINT Depth = CkDepth;
    UINT PrintedBefore = CkHooks.Printed;

    if (Family == CkFamilyDdi)
    {
        CkAppend(&Text, "ddi %s", CkDdiNames[Slot]);
        CkDescribeDdiCall(&Text, Slot, A);
    }
    else if (Family == CkFamilyDevice)
    {
        CkAppend(&Text, "d3d12 %s", CkDeviceNames[Slot]);
        CkDescribeDeviceCall(&Text, Slot, A);
    }
    else if (Family == CkFamilyQueue)
    {
        CkAppend(&Text, "queue %s %s", CkName(reinterpret_cast<VOID*>(A[0])).c_str(), CkQueueNames[Slot]);
        CkDescribeQueueCall(&Text, Slot, A);
    }
    else
    {
        CkAppend(&Text, "private %s", CkPrivateNames[Slot]);
        CkDescribePrivateCall(&Text, Slot, A);
    }

    CkDepth += 1;
    if (Family == CkFamilyDdi)
    {
        UINT Outer = CkDdiSlot;

        CkDdiSlot = Slot;
        Result = CkHooks.Originals[Family][Slot](A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[8], A[9], A[10], A[11]);
        CkDdiSlot = Outer;
    }
    else
    {
        Result = CkHooks.Originals[Family][Slot](A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[8], A[9], A[10], A[11]);
    }
    CkDepth -= 1;

    if (Family == CkFamilyDdi && SUCCEEDED(static_cast<HRESULT>(Result)))
        CkDescribeDdiResult(&Text, Slot, A);

    if (Family == CkFamilyPrivate)
        CkDescribePrivateResult(&Text, Slot, A, Result);

    /*
     * The translation layer makes another allocator only when the ones it has are still in flight,
     * which depends on GPU timing, so allocators share one name and their creation does not print.
     */
    if (Family == CkFamilyDevice && Slot == CkDeviceCreateCommandAllocator)
    {
        if (SUCCEEDED(static_cast<HRESULT>(Result)) && A[3] != 0 && *reinterpret_cast<VOID**>(A[3]) != nullptr)
        {
            AcquireSRWLockExclusive(&CkHooks.Lock);
            CkHooks.Ids[*reinterpret_cast<VOID**>(A[3])] = "allocator";
            ReleaseSRWLockExclusive(&CkHooks.Lock);
        }
        return Result;
    }
    if (Family == CkFamilyDevice && Slot == CkDeviceCreateCommittedResource && CkIsUploadChunk(A))
    {
        if (SUCCEEDED(static_cast<HRESULT>(Result)) && A[7] != 0 && *reinterpret_cast<VOID**>(A[7]) != nullptr)
        {
            AcquireSRWLockExclusive(&CkHooks.Lock);
            CkHooks.Ids[*reinterpret_cast<VOID**>(A[7])] = "upload";
            ReleaseSRWLockExclusive(&CkHooks.Lock);
        }
        return Result;
    }

    if (Family == CkFamilyDevice)
    {
        UINT_PTR Out = CkDeviceOutPointer(Slot, A);

        if (Out != 0 && SUCCEEDED(static_cast<HRESULT>(Result)))
            CkAppend(&Text, " -> %s", CkOutId(Out).c_str());
    }

    Line.assign(Depth * 2, ' ');
    Line += Text;

    /* Methods without a result print none */
    if (!((Family == CkFamilyDdi && (Slot == 141 || Slot == 142)) ||
          (Family == CkFamilyDevice && ((Slot >= 17 && Slot <= 24) || Slot == 38 || Slot == 42 || Slot == 63 ||
                                        Slot == 72)) ||
          (Family == CkFamilyQueue && Slot >= 8 && Slot <= 13) || (Family == CkFamilyPrivate && !CkPrivateHasResult(Slot))))
    {
        CkAppend(&Line, " = %08lX", static_cast<ULONG>(Result));
    }

    /*
     * The runtime hands state calls to a worker thread at varying points, so they are counted
     * instead of printed in order.
     */
    /* How often a query answers not ready yet depends on the GPU, so only its final answer prints */
    if (Family == CkFamilyDdi && Slot == CkDdiGetQueryData && Result == S_FALSE)
        return Result;
    if (CkHooks.Polling && Family == CkFamilyDdi && Slot == CkDdiFlush1 && CkHooks.Printed == PrintedBefore)
        return Result;

    if (Family == CkFamilyDdi && Depth == 0 && CkIsStateSlot(Slot))
    {
        AcquireSRWLockExclusive(&CkHooks.Lock);
        CkHooks.StateCalls[Text.substr(4) + Line.substr(Line.rfind(" = "))] += 1;
        ReleaseSRWLockExclusive(&CkHooks.Lock);
        return Result;
    }

    AcquireSRWLockExclusive(&CkHooks.Lock);
    printf("%s\n", Line.c_str());
    CkHooks.Printed += 1;
    ReleaseSRWLockExclusive(&CkHooks.Lock);
    return Result;
}

VOID
CkSetPolling(
    _In_ BOOLEAN Polling)
{
    AcquireSRWLockExclusive(&CkHooks.Lock);
    CkHooks.Polling = Polling;
    ReleaseSRWLockExclusive(&CkHooks.Lock);
}

template <UINT Family, UINT Slot>
static
UINT_PTR
APIENTRY
CkThunk(
    _In_ UINT_PTR A0,
    _In_ UINT_PTR A1,
    _In_ UINT_PTR A2,
    _In_ UINT_PTR A3,
    _In_ UINT_PTR A4,
    _In_ UINT_PTR A5,
    _In_ UINT_PTR A6,
    _In_ UINT_PTR A7,
    _In_ UINT_PTR A8,
    _In_ UINT_PTR A9,
    _In_ UINT_PTR A10,
    _In_ UINT_PTR A11)
{
    UINT_PTR Arguments[CkArgumentCount] = { A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11 };

    return CkDispatch(static_cast<CkFamily>(Family), Slot, Arguments);
}

template <UINT Family, UINT... Slots>
static
const PFN_CK_FORWARD*
CkThunkTable(
    _In_ std::integer_sequence<UINT, Slots...>)
{
    static const PFN_CK_FORWARD Table[] = { &CkThunk<Family, Slots>... };
    return Table;
}

static
const PFN_CK_FORWARD*
CkThunks(
    _In_ CkFamily Family)
{
    static const PFN_CK_FORWARD* Tables[CkFamilyCount] =
    {
        CkThunkTable<CkFamilyDdi>(std::make_integer_sequence<UINT, CkSlotLimit>()),
        CkThunkTable<CkFamilyDevice>(std::make_integer_sequence<UINT, CkSlotLimit>()),
        CkThunkTable<CkFamilyQueue>(std::make_integer_sequence<UINT, CkSlotLimit>()),
        CkThunkTable<CkFamilyPrivate>(std::make_integer_sequence<UINT, CkSlotLimit>()),
    };
    return Tables[Family];
}

VOID
CkWrapDeviceTable(
    _Inout_ D3DDDI_DEVICEFUNCS* Functions)
{
    PFN_CK_FORWARD* Slots = reinterpret_cast<PFN_CK_FORWARD*>(Functions);
    const PFN_CK_FORWARD* Thunks = CkThunks(CkFamilyDdi);
    std::string Empty;

    for (UINT Slot = 0; Slot < _countof(CkDdiNames); Slot++)
    {
        if (Slots[Slot] == nullptr)
        {
            CkAppend(&Empty, " %s", CkDdiNames[Slot]);
            continue;
        }
        if (Slots[Slot] == Thunks[Slot])
            continue;
        CkHooks.Originals[CkFamilyDdi][Slot] = Slots[Slot];
        Slots[Slot] = Thunks[Slot];
    }
    printf("  device functions empty:%s\n", Empty.c_str());
}

VOID
CkWrapPrivateTable(
    _Inout_updates_(Count) PVOID* Slots,
    _In_ UINT Count)
{
    const PFN_CK_FORWARD* Thunks = CkThunks(CkFamilyPrivate);

    for (UINT Slot = 0; Slot < Count && Slot < _countof(CkPrivateNames); Slot++)
    {
        if (CkPrivateNames[Slot] == nullptr || Slots[Slot] == nullptr)
            continue;
        if (Slots[Slot] == reinterpret_cast<PVOID>(Thunks[Slot]))
            continue;
        CkHooks.Originals[CkFamilyPrivate][Slot] = reinterpret_cast<PFN_CK_FORWARD>(Slots[Slot]);
        Slots[Slot] = reinterpret_cast<PVOID>(Thunks[Slot]);
    }
}

/* Rewrites the named slots of a vtable shared by every object of its class */
static
BOOLEAN
CkPatchVtable(
    _In_ CkFamily Family,
    _In_ IUnknown* Object,
    _In_reads_(Count) const PCSTR* Names,
    _In_ UINT Count)
{
    PFN_CK_FORWARD* Table = *reinterpret_cast<PFN_CK_FORWARD**>(Object);
    const PFN_CK_FORWARD* Thunks = CkThunks(Family);
    DWORD Old;

    if (!VirtualProtect(Table, Count * sizeof(*Table), PAGE_READWRITE, &Old))
        return FALSE;
    for (UINT Slot = 0; Slot < Count; Slot++)
    {
        if (Names[Slot] == nullptr || Table[Slot] == Thunks[Slot])
            continue;
        CkHooks.Originals[Family][Slot] = Table[Slot];
        Table[Slot] = Thunks[Slot];
    }
    VirtualProtect(Table, Count * sizeof(*Table), Old, &Old);
    return TRUE;
}

BOOLEAN
CkHookD3D12(VOID)
{
    D3D12_COMMAND_QUEUE_DESC Desc = {};
    ID3D12Device10* Newest = nullptr;
    ID3D12Device* Device = nullptr;
    ID3D12CommandQueue* Queue = nullptr;
    BOOLEAN Hooked = FALSE;

    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
        return FALSE;

    /* Only the slots this device class has are touched */
    if (SUCCEEDED(Device->QueryInterface(IID_PPV_ARGS(&Newest))) &&
        SUCCEEDED(Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Queue))))
    {
        Hooked = CkPatchVtable(CkFamilyDevice, Device, CkDeviceNames, _countof(CkDeviceNames)) &&
                 CkPatchVtable(CkFamilyQueue, Queue, CkQueueNames, _countof(CkQueueNames));
    }

    /* The device and queue stay alive so the patched vtables stay mapped */
    if (Newest != nullptr)
        Newest->Release();
    return Hooked;
}
