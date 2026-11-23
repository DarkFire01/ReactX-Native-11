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
    SRWLOCK Lock;
};

static CkHookState CkHooks = { {}, {}, {}, 0, SRWLOCK_INIT };

/* How deep in wrapped calls the current thread is, for indenting the calls a call makes */
static thread_local UINT CkDepth;

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
        {
            const D3D12_GRAPHICS_PIPELINE_STATE_DESC* Desc =
                reinterpret_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(A[1]);

            CkAppend(Text, " root %s vs %zu ps %zu inputs %u topology %u targets %u rt0 %u ds %u samples %u",
                     CkName(Desc->pRootSignature).c_str(), Desc->VS.BytecodeLength, Desc->PS.BytecodeLength,
                     Desc->InputLayout.NumElements, Desc->PrimitiveTopologyType, Desc->NumRenderTargets,
                     Desc->RTVFormats[0], Desc->DSVFormat, Desc->SampleDesc.Count);
            break;
        }
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
            CkAppend(Text, " node %u size %zu", static_cast<UINT>(A[1]), static_cast<SIZE_T>(A[3]));
            break;
        case 18:
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

            CkAppend(Text, " resource %s sub %u cookie %s", CkName(Args->hResource).c_str(), Args->SubResourceIndex,
                     CkName(Args->hCookie).c_str());
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
                AcquireSRWLockExclusive(&CkHooks.Lock);
                CkHooks.Ids.erase(Args->hResource);
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
    else
    {
        CkAppend(&Text, "queue %s %s", CkName(reinterpret_cast<VOID*>(A[0])).c_str(), CkQueueNames[Slot]);
        CkDescribeQueueCall(&Text, Slot, A);
    }

    CkDepth += 1;
    Result = CkHooks.Originals[Family][Slot](A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[8], A[9], A[10], A[11]);
    CkDepth -= 1;

    if (Family == CkFamilyDdi && SUCCEEDED(static_cast<HRESULT>(Result)))
        CkDescribeDdiResult(&Text, Slot, A);

    if (Family == CkFamilyDevice)
    {
        UINT_PTR Out = CkDeviceOutPointer(Slot, A);

        if (Out != 0 && SUCCEEDED(static_cast<HRESULT>(Result)))
            CkAppend(&Text, " -> %s", CkOutId(Out).c_str());
    }

    Line.assign(Depth * 2, ' ');
    Line += Text;

    /* Methods without a result print none */
    if (!((Family == CkFamilyDevice && ((Slot >= 17 && Slot <= 24) || Slot == 38 || Slot == 42 || Slot == 63 ||
                                        Slot == 72)) ||
          (Family == CkFamilyQueue && Slot >= 8 && Slot <= 13)))
    {
        CkAppend(&Line, " = %08lX", static_cast<ULONG>(Result));
    }

    /*
     * The runtime hands state calls to a worker thread at varying points, so they are counted
     * instead of printed in order.
     */
    if (Family == CkFamilyDdi && Depth == 0 && CkIsStateSlot(Slot))
    {
        AcquireSRWLockExclusive(&CkHooks.Lock);
        CkHooks.StateCalls[Text.substr(4) + Line.substr(Line.rfind(" = "))] += 1;
        ReleaseSRWLockExclusive(&CkHooks.Lock);
        return Result;
    }

    AcquireSRWLockExclusive(&CkHooks.Lock);
    printf("%s\n", Line.c_str());
    ReleaseSRWLockExclusive(&CkHooks.Lock);
    return Result;
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
