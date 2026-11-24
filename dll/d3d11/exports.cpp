/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The rest of the d3d11.dll export table: layers, the null driver and annotations
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <d3d11on12.h>

#include <algorithm>
#include <vector>

/*
 * d3d11.dll also answers as a user mode driver of its own, the null reference adapter.
 * Its kernel thunks succeed without doing anything, apart from the few below.
 */
extern "C"
NTSTATUS
APIENTRY
RxNullKmtCall(
    _In_opt_ PVOID Args)
{
    UNREFERENCED_PARAMETER(Args);
    return STATUS_SUCCESS;
}

extern "C"
NTSTATUS
APIENTRY
RxNullKmtEscape(
    _In_ const D3DKMT_ESCAPE* Escape)
{
    if (Escape->Type == D3DKMT_ESCAPE_HISTORY_BUFFER_STATUS)
        *static_cast<BYTE*>(Escape->pPrivateDriverData) = 1;
    return STATUS_SUCCESS;
}

extern "C"
NTSTATUS
APIENTRY
RxNullKmtGetDeviceState(
    _Inout_ D3DKMT_GETDEVICESTATE* State)
{
    switch (State->StateType)
    {
        case D3DKMT_DEVICESTATE_EXECUTION:
            State->ExecutionState = D3DKMT_DEVICEEXECUTION_ACTIVE;
            return STATUS_SUCCESS;

        case D3DKMT_DEVICESTATE_PRESENT:
            static_assert(sizeof(State->PresentState) == 40);
            ZeroMemory(&State->PresentState, sizeof(State->PresentState));
            return STATUS_SUCCESS;

        default:
            return STATUS_UNSUCCESSFUL;
    }
}

/* Every display the null adapter is asked about gets the same handle and a fresh LUID */
static constexpr D3DKMT_HANDLE RxNullAdapterHandle = 0xF0F;

extern "C"
NTSTATUS
APIENTRY
RxNullKmtOpenAdapterFromHdc(
    _Inout_ D3DKMT_OPENADAPTERFROMHDC* Open)
{
    Open->hAdapter = RxNullAdapterHandle;
    AllocateLocallyUniqueId(&Open->AdapterLuid);
    return STATUS_SUCCESS;
}

extern "C"
NTSTATUS
APIENTRY
RxNullKmtRejectResource(
    _In_opt_ PVOID Args)
{
    UNREFERENCED_PARAMETER(Args);
    return STATUS_INVALID_PARAMETER;
}

extern "C" IMAGE_DOS_HEADER __ImageBase;

extern "C"
NTSTATUS
APIENTRY
RxNullKmtQueryAdapterInfo(
    _Inout_ const D3DKMT_QUERYADAPTERINFO* Query)
{
    D3DKMT_UMDFILENAMEINFO* FileName;

    switch (Query->Type)
    {
        /* The null adapter's driver is this module, for every runtime but Direct3D 9 */
        case KMTQAITYPE_UMDRIVERNAME:
            FileName = static_cast<D3DKMT_UMDFILENAMEINFO*>(Query->pPrivateDriverData);
            if (FileName->Version == KMTUMDVERSION_DX9)
                return STATUS_UNSUCCESSFUL;
            if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), FileName->UmdFileName, MAX_PATH) == 0)
                return STATUS_UNSUCCESSFUL;
            break;

        case KMTQAITYPE_CHECKDRIVERUPDATESTATUS:
            *static_cast<BOOL*>(Query->pPrivateDriverData) = FALSE;
            break;

        case KMTQAITYPE_WDDM_2_0_CAPS:
        {
            D3DKMT_WDDM_2_0_CAPS* Caps = static_cast<D3DKMT_WDDM_2_0_CAPS*>(Query->pPrivateDriverData);

            if (Query->PrivateDriverDataSize != sizeof(*Caps))
                return STATUS_INVALID_PARAMETER;
            Caps->Support64BitAtomics = 1;
            Caps->GpuMmuSupported = 0;
            Caps->IoMmuSupported = 1;
            break;
        }

        default:
            break;
    }

    return STATUS_SUCCESS;
}

/* The null reference adapter's DDI */

static
SIZE_T
APIENTRY
RxNullCalcPrivateDeviceSize(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ const D3D10DDIARG_CALCPRIVATEDEVICESIZE* Args)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Args);
    return 8;
}

static
HRESULT
APIENTRY
RxNullCreateDevice(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ D3D10DDIARG_CREATEDEVICE* Args)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Args);
    RX_TRACE(L"the null reference device is not implemented");
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
RxNullCloseAdapter(
    _In_ D3D10DDI_HADAPTER Adapter)
{
    UNREFERENCED_PARAMETER(Adapter);
    return S_OK;
}

/* Interface and build pairs the null reference adapter speaks, newest first */
static constexpr UINT64 RxNullDdiVersions[] =
{
    0x000B002200020000,
    0x000B002000090000,
    0x000B001000010000,
    0x000B000F00000000,
    0x000B000A00020000,
    0x000A000200010000,
    0x000A000700000000,
    0x000A000100040000,
    0x000A000600000000,
};

static
HRESULT
APIENTRY
RxNullGetSupportedVersions(
    _In_ D3D10DDI_HADAPTER Adapter,
    _Inout_ UINT32* Count,
    _Out_writes_opt_(*Count) UINT64* Versions)
{
    UNREFERENCED_PARAMETER(Adapter);

    if (Versions != nullptr && *Count < _countof(RxNullDdiVersions))
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    *Count = _countof(RxNullDdiVersions);
    if (Versions != nullptr)
        memcpy(Versions, RxNullDdiVersions, sizeof(RxNullDdiVersions));
    return S_OK;
}

/* Writes one UINT cap when the caller sized it right */
static
HRESULT
RxNullUintCap(
    _In_ const D3D10_2DDIARG_GETCAPS* Caps,
    _In_ UINT Value)
{
    if (Caps->DataSize != sizeof(UINT))
        return E_UNEXPECTED;

    *static_cast<UINT*>(Caps->pData) = Value;
    return S_OK;
}

/* Clears a cap structure of the expected size */
static
HRESULT
RxNullZeroCap(
    _In_ const D3D10_2DDIARG_GETCAPS* Caps,
    _In_ UINT Size)
{
    if (Caps->DataSize != Size)
        return E_UNEXPECTED;

    ZeroMemory(Caps->pData, Size);
    return S_OK;
}

static
HRESULT
APIENTRY
RxNullGetCaps(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ const D3D10_2DDIARG_GETCAPS* Caps)
{
    UNREFERENCED_PARAMETER(Adapter);

    switch (static_cast<UINT>(Caps->Type))
    {
        case D3D11DDICAPS_THREADING:
        case D3DWDDM1_3DDICAPS_D3D11_OPTIONS1:
            return RxNullUintCap(Caps, 7);

        case D3D11DDICAPS_SHADER:
            return RxNullUintCap(Caps, 115);

        case D3D11DDICAPS_3DPIPELINESUPPORT:
            return RxNullUintCap(Caps, 399);

        case D3D11_1DDICAPS_D3D11_OPTIONS:
            if (Caps->DataSize != sizeof(UINT64))
                return E_UNEXPECTED;
            *static_cast<UINT64*>(Caps->pData) = 0x100000001;
            return S_OK;

        case D3D11_1DDICAPS_ARCHITECTURE_INFO:
            return RxNullUintCap(Caps, 0);

        case D3D11_1DDICAPS_SHADER_MIN_PRECISION_SUPPORT:
        case D3DWDDM2_0DDICAPS_MEMORY_ARCHITECTURE:
            return RxNullZeroCap(Caps, sizeof(UINT64));

        case D3DWDDM1_3DDICAPS_MARKER:
            return RxNullUintCap(Caps, 2);

        /* One past the 2.0 options; the header leaves it unnamed */
        case 144:
            return RxNullZeroCap(Caps, 16);

        case D3DWDDM2_0DDICAPS_TEXTURE_LAYOUT:
            if (Caps->pInfo == nullptr)
                return E_UNEXPECTED;
            return RxNullZeroCap(Caps, 12);

        case D3DWDDM2_0DDICAPS_D3D11_OPTIONS3:
            return RxNullUintCap(Caps, 1);

        case D3DWDDM2_2DDICAPS_TEXTURE_LAYOUT:
            if (Caps->pInfo != nullptr)
                return E_UNEXPECTED;
            return RxNullZeroCap(Caps, 16);

        default:
            return E_NOTIMPL;
    }
}

/* Hands out the null reference adapter; the 10.2 form adds versions and caps */
static
VOID
RxNullFillAdapter(
    _Inout_ D3D10DDIARG_OPENADAPTER* Open,
    _In_ BOOLEAN IsVersion2)
{
    D3D10_2DDI_ADAPTERFUNCS* Funcs = Open->pAdapterFuncs_2;

    Funcs->pfnCalcPrivateDeviceSize = RxNullCalcPrivateDeviceSize;
    Funcs->pfnCreateDevice = RxNullCreateDevice;
    Funcs->pfnCloseAdapter = RxNullCloseAdapter;
    if (IsVersion2)
    {
        Funcs->pfnGetSupportedVersions = RxNullGetSupportedVersions;
        Funcs->pfnGetCaps = RxNullGetCaps;
    }

    Open->hAdapter.pDrvPrivate = reinterpret_cast<VOID*>(static_cast<ULONG_PTR>(0xF07));
}

extern "C"
HRESULT
APIENTRY
OpenAdapter10(
    _Inout_ D3D10DDIARG_OPENADAPTER* pOpenData)
{
    RxNullFillAdapter(pOpenData, FALSE);
    return S_OK;
}

extern "C"
HRESULT
APIENTRY
OpenAdapter10_2(
    _Inout_ D3D10DDIARG_OPENADAPTER* pOpenData)
{
    RxNullFillAdapter(pOpenData, TRUE);
    return S_OK;
}

extern "C"
BOOL
WINAPI
EnableFeatureLevelUpgrade(VOID)
{
    return TRUE;
}

/* D3DPerformance_* annotate through the context the caller hands in */

static
ID3D11DeviceContext2*
RxAnnotationContext(
    _In_opt_ IUnknown* Context)
{
    ID3D11DeviceContext2* Found = nullptr;

    if (Context == nullptr)
        return nullptr;
    if (FAILED(Context->QueryInterface(IID_PPV_ARGS(&Found))))
        return nullptr;
    return Found;
}

extern "C"
INT
WINAPI
D3DPerformance_BeginEvent(
    _In_opt_ IUnknown* pContext,
    _In_opt_ LPCWSTR Name)
{
    ID3D11DeviceContext2* Context = RxAnnotationContext(pContext);

    if (Context != nullptr)
    {
        Context->BeginEventInt(Name, 0);
        Context->Release();
    }

    return -1;
}

extern "C"
INT
WINAPI
D3DPerformance_EndEvent(
    _In_opt_ IUnknown* pContext)
{
    ID3D11DeviceContext2* Context = RxAnnotationContext(pContext);

    if (Context != nullptr)
    {
        Context->EndEvent();
        Context->Release();
    }

    return -1;
}

extern "C"
VOID
WINAPI
D3DPerformance_SetMarker(
    _In_opt_ IUnknown* pContext,
    _In_opt_ LPCWSTR Name)
{
    ID3D11DeviceContext2* Context = RxAnnotationContext(pContext);

    if (Context != nullptr)
    {
        Context->SetMarkerInt(Name, 0);
        Context->Release();
    }
}

extern "C"
BOOL
WINAPI
D3DPerformance_GetStatus(
    _In_opt_ IUnknown* pContext)
{
    ID3D11DeviceContext2* Context = RxAnnotationContext(pContext);
    BOOL Enabled;

    if (Context == nullptr)
        return FALSE;

    Enabled = Context->IsAnnotationEnabled();
    Context->Release();
    return Enabled;
}

/*
 * Layer registrations: each entry is keyed by its first UINT and carries the layer's
 * size and create callbacks. A layered device's arguments point at two UINT lists; the
 * head of each names the layer to run next and what to pass it.
 */
typedef SIZE_T (APIENTRY* RXLAYERSIZE)(UINT, const VOID*, SIZE_T);
typedef HRESULT (APIENTRY* RXLAYERCREATE)(UINT, const VOID*, SIZE_T, VOID*, VOID*, VOID**);

struct RxLayerEntry
{
    UINT Id;
    UINT Reserved;
    VOID* Context;
    RXLAYERSIZE GetSize;
    RXLAYERCREATE Create;
};

static_assert(sizeof(RxLayerEntry) == 32);

/* The layered device arguments are a 56 byte block; these are the two list cursors in it */
struct RxLayerArgs
{
    BYTE Head[16];
    const UINT* Parameters;
    const UINT* Indices;
    BYTE Tail[24];
};

static_assert(sizeof(RxLayerArgs) == 56);

static CRITICAL_SECTION RxLayerLock;
static std::vector<RxLayerEntry>* RxLayers;

/* Runs once from DllMain */
VOID
RxInitializeLayers(VOID)
{
    InitializeCriticalSection(&RxLayerLock);
}

/* The entry the head index names, or nullptr past the end */
static
const RxLayerEntry*
RxNextLayer(
    _In_ const RxLayerArgs& Args)
{
    if (RxLayers == nullptr || *Args.Indices >= RxLayers->size())
        return nullptr;

    return &(*RxLayers)[*Args.Indices];
}

extern "C"
HRESULT
APIENTRY
D3D11CoreRegisterLayers(
    _In_reads_opt_(Count) const VOID* pLayers,
    _In_ DWORD Count)
{
    const RxLayerEntry* Entries = static_cast<const RxLayerEntry*>(pLayers);

    if (Count == 0)
        return S_OK;
    if (Entries == nullptr)
        return E_INVALIDARG;

    EnterCriticalSection(&RxLayerLock);

    if (RxLayers == nullptr)
        RxLayers = new (std::nothrow) std::vector<RxLayerEntry>();

    if (RxLayers != nullptr)
    {
        RxLayers->insert(RxLayers->end(), Entries, Entries + Count);
        std::sort(RxLayers->begin(), RxLayers->end(),
                  [](const RxLayerEntry& Left, const RxLayerEntry& Right) { return Left.Id < Right.Id; });
        RxLayers->erase(std::unique(RxLayers->begin(), RxLayers->end(),
                                    [](const RxLayerEntry& Left, const RxLayerEntry& Right)
                                    {
                                        return Left.Id == Right.Id;
                                    }),
                        RxLayers->end());
    }

    LeaveCriticalSection(&RxLayerLock);
    return RxLayers != nullptr ? S_OK : E_OUTOFMEMORY;
}

extern "C"
SIZE_T
APIENTRY
D3D11CoreGetLayeredDeviceSize(
    _In_reads_bytes_(ArgsSize) const VOID* pArgs,
    _In_ SIZE_T ArgsSize)
{
    const RxLayerArgs* Args = static_cast<const RxLayerArgs*>(pArgs);
    const RxLayerEntry* Layer;
    RxLayerArgs Next;
    SIZE_T Size = 0;

    if (Args == nullptr || ArgsSize != sizeof(*Args) || Args->Parameters == nullptr || Args->Indices == nullptr)
        return 0;

    EnterCriticalSection(&RxLayerLock);

    Layer = RxNextLayer(*Args);
    if (Layer != nullptr)
    {
        /* The layer sees the lists with its own entry taken off */
        Next = *Args;
        Next.Parameters += 1;
        Next.Indices += 1;
        Size = Layer->GetSize(*Args->Parameters, &Next, sizeof(Next));
    }

    LeaveCriticalSection(&RxLayerLock);
    return Size;
}

extern "C"
HRESULT
APIENTRY
D3D11CoreCreateLayeredDevice(
    _In_reads_bytes_(ArgsSize) const VOID* pArgs,
    _In_ SIZE_T ArgsSize,
    _In_opt_ VOID* pCallbacks,
    _In_ VOID* pMemory,
    _Outptr_ VOID** ppDevice)
{
    const RxLayerArgs* Args = static_cast<const RxLayerArgs*>(pArgs);
    const RxLayerEntry* Layer;
    RxLayerArgs Next;
    HRESULT hr;

    if (ppDevice == nullptr)
        return E_UNEXPECTED;
    *ppDevice = nullptr;

    if (Args == nullptr || ArgsSize != sizeof(*Args) || Args->Parameters == nullptr || Args->Indices == nullptr ||
        pMemory == nullptr)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&RxLayerLock);

    Layer = RxNextLayer(*Args);
    if (Layer == nullptr)
    {
        hr = E_INVALIDARG;
    }
    else
    {
        Next = *Args;
        Next.Parameters += 1;
        Next.Indices += 1;
        hr = Layer->Create(*Args->Parameters, &Next, sizeof(Next), pCallbacks, pMemory, ppDevice);
    }

    LeaveCriticalSection(&RxLayerLock);
    return hr;
}

/* A layer list as D3D11CoreCreateDevice takes it */
struct RxLayerList
{
    const VOID* Layers;
    UINT Count;
};

extern "C"
HRESULT
WINAPI
D3D11CoreCreateDevice(
    _In_opt_ const RxLayerList* pLayerList,
    _In_opt_ IDXGIAdapter* pAdapter,
    _In_ D3D_DRIVER_TYPE DriverType,
    _In_opt_ HMODULE Software,
    _In_ UINT Flags,
    _In_reads_opt_(FeatureLevels) const D3D_FEATURE_LEVEL* pFeatureLevels,
    _In_ UINT FeatureLevels,
    _In_ UINT SDKVersion,
    _COM_Outptr_opt_ ID3D11Device** ppDevice,
    _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel)
{
    RxLayerList List = {};

    UNREFERENCED_PARAMETER(SDKVersion);

    if (ppDevice != nullptr)
        *ppDevice = nullptr;
    if (pFeatureLevel != nullptr)
        *pFeatureLevel = (D3D_FEATURE_LEVEL)0;

    if (pLayerList != nullptr)
        List = *pLayerList;
    if (List.Count >= 0xFF || (List.Layers == nullptr && List.Count != 0))
        return E_INVALIDARG;

    return D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                             D3D11_SDK_VERSION, ppDevice, pFeatureLevel, nullptr);
}

/* D3D11On12CreateDevice without queues, checked the way this entry point checks */
extern "C"
HRESULT
WINAPI
D3D11CreateDeviceForD3D12(
    _In_ IUnknown* pDevice,
    _In_ UINT Flags,
    _In_reads_opt_(FeatureLevels) const D3D_FEATURE_LEVEL* pFeatureLevels,
    _In_ UINT FeatureLevels,
    _In_ UINT SDKVersion,
    _In_ UINT NodeMask,
    _COM_Outptr_opt_ ID3D11Device** ppDevice,
    _COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext,
    _Out_opt_ D3D_FEATURE_LEVEL* pChosenFeatureLevel)
{
    ID3D12Device* Device12 = nullptr;

    if (ppDevice != nullptr)
        *ppDevice = nullptr;
    if (ppImmediateContext != nullptr)
        *ppImmediateContext = nullptr;

    if (SDKVersion != D3D11_SDK_VERSION)
        return E_INVALIDARG;

    if (pDevice == nullptr)
    {
        RX_TRACE(L"D3D11CreateDeviceForD3D12: no D3D12 device");
        return E_INVALIDARG;
    }

    if (FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&Device12))) || Device12 == nullptr)
    {
        RX_TRACE(L"D3D11CreateDeviceForD3D12: not a D3D12 device");
        return E_INVALIDARG;
    }
    Device12->Release();

    if (NodeMask == 0 || (NodeMask & (NodeMask - 1)) != 0)
    {
        RX_TRACE(L"D3D11CreateDeviceForD3D12: node mask 0x%X is not a single node", NodeMask);
        return E_INVALIDARG;
    }

    return D3D11On12CreateDevice(pDevice, Flags, pFeatureLevels, FeatureLevels, nullptr, 0, NodeMask, ppDevice,
                                 ppImmediateContext, pChosenFeatureLevel);
}
