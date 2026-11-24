/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The device DDI table and the entries with a fixed answer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Entries the reference answers the same way every time, once the device handle is valid */
template <HRESULT Answer, typename... Rest>
static
HRESULT
APIENTRY
NineFixedAnswer(
    _In_ HANDLE hDevice,
    Rest...)
{
    if (hDevice == NULL)
        return E_INVALIDARG;
    return NineDxgiResult(Answer);
}

/* Entries that answer without looking at the device */
template <typename... Rest>
static
HRESULT
APIENTRY
NineAlwaysDone(
    _In_ HANDLE hDevice,
    Rest...)
{
    UNREFERENCED_PARAMETER(hDevice);
    return NineDxgiResult(S_OK);
}

/* Entries that are not implemented yet report it once */
template <UINT Slot, typename... Rest>
static
HRESULT
APIENTRY
NineMissingDdi(
    _In_ HANDLE hDevice,
    Rest...)
{
    UNREFERENCED_PARAMETER(hDevice);
    NINE_TRACE_ONCE(L"device DDI %u is not implemented", Slot);
    return E_NOTIMPL;
}

template <UINT Slot>
static
VOID
APIENTRY
NineMissingSyncToken(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SYNCTOKEN* Args)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Args);
    NINE_TRACE_ONCE(L"device DDI %u is not implemented", Slot);
}

static
HRESULT
APIENTRY
NineValidateDevice(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_VALIDATETEXTURESTAGESTATE* Args)
{
    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    Args->NumPasses = 1;
    return NineDxgiResult(S_OK);
}

static
VOID
APIENTRY
NineCheckCounterInfo(
    _In_ HANDLE hDevice,
    _Out_ D3DDDIARG_COUNTER_INFO* Info)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Info);
}

#define NINE_MISSING(Field, Slot) Functions->Field = NineMissingDdi<Slot>

VOID
NineFillDeviceFuncs(
    _Out_ D3DDDI_DEVICEFUNCS* Functions)
{
    constexpr HRESULT NotDone = E_NOTIMPL;

    Functions->pfnSetRenderState = NineSetRenderState;
    Functions->pfnUpdateWInfo = NineUpdateWInfo;
    Functions->pfnValidateDevice = NineValidateDevice;
    Functions->pfnSetTextureStageState = NineSetTextureStageState;
    Functions->pfnSetTexture = NineSetTexture;
    Functions->pfnSetPixelShader = NineSetPixelShader;
    Functions->pfnSetPixelShaderConst = NineSetPixelShaderConst;
    Functions->pfnSetStreamSourceUm = NineSetStreamSourceUm;
    Functions->pfnSetIndices = NineSetIndices;
    Functions->pfnSetIndicesUm = NineSetIndicesUm;
    Functions->pfnDrawPrimitive = NineDrawPrimitive;
    Functions->pfnDrawIndexedPrimitive = NineDrawIndexedPrimitive;
    Functions->pfnDrawRectPatch = NineFixedAnswer<NotDone>;
    Functions->pfnDrawTriPatch = NineFixedAnswer<NotDone>;
    Functions->pfnDrawPrimitive2 = NineDrawPrimitive2;
    Functions->pfnDrawIndexedPrimitive2 = NineDrawIndexedPrimitive2;
    Functions->pfnVolBlt = nullptr;
    Functions->pfnBufBlt = nullptr;
    Functions->pfnTexBlt = nullptr;
    Functions->pfnStateSet = NineFixedAnswer<S_OK>;
    Functions->pfnSetPriority = NineFixedAnswer<NotDone>;
    Functions->pfnClear = NineClear;
    Functions->pfnUpdatePalette = NineFixedAnswer<NotDone>;
    Functions->pfnSetPalette = NineFixedAnswer<NotDone>;
    Functions->pfnSetVertexShaderConst = NineSetVertexShaderConst;
    Functions->pfnMultiplyTransform = NineFixedAnswer<S_OK>;
    Functions->pfnSetTransform = NineFixedAnswer<S_OK>;
    Functions->pfnSetViewport = NineSetViewport;
    Functions->pfnSetZRange = NineSetZRange;
    Functions->pfnSetMaterial = NineFixedAnswer<S_OK>;
    Functions->pfnSetLight = NineFixedAnswer<S_OK>;
    Functions->pfnCreateLight = NineFixedAnswer<S_OK>;
    Functions->pfnDestroyLight = NineFixedAnswer<S_OK>;
    Functions->pfnSetClipPlane = NineSetClipPlane;
    Functions->pfnGetInfo = NineFixedAnswer<NotDone>;
    Functions->pfnLock = NineLock;
    Functions->pfnUnlock = NineUnlock;
    Functions->pfnCreateResource = nullptr;
    Functions->pfnDestroyResource = NineDestroyResource;
    Functions->pfnSetDisplayMode = NineFixedAnswer<S_OK>;
    Functions->pfnPresent = nullptr;
    Functions->pfnFlush = nullptr;
    Functions->pfnCreateVertexShaderFunc = NineCreateVertexShaderFunc;
    Functions->pfnDeleteVertexShaderFunc = NineDeleteVertexShaderFunc;
    Functions->pfnSetVertexShaderFunc = NineSetVertexShaderFunc;
    Functions->pfnCreateVertexShaderDecl = NineCreateVertexShaderDecl;
    Functions->pfnDeleteVertexShaderDecl = NineDeleteVertexShaderDecl;
    Functions->pfnSetVertexShaderDecl = NineSetVertexShaderDecl;
    Functions->pfnSetVertexShaderConstI = NineSetVertexShaderConstI;
    Functions->pfnSetVertexShaderConstB = NineSetVertexShaderConstB;
    Functions->pfnSetScissorRect = NineSetScissorRect;
    Functions->pfnSetStreamSource = NineSetStreamSource;
    Functions->pfnSetStreamSourceFreq = NineSetStreamSourceFreq;
    Functions->pfnSetConvolutionKernelMono = NineFixedAnswer<NotDone>;
    Functions->pfnComposeRects = NineFixedAnswer<NotDone>;
    Functions->pfnBlt = NineBlt;
    Functions->pfnColorFill = NineColorFill;
    NINE_MISSING(pfnDepthFill, 57);
    Functions->pfnCreateQuery = NineCreateQuery;
    Functions->pfnDestroyQuery = NineDestroyQuery;
    Functions->pfnIssueQuery = NineIssueQuery;
    Functions->pfnGetQueryData = NineGetQueryData;
    Functions->pfnSetRenderTarget = NineSetRenderTarget;
    Functions->pfnSetDepthStencil = NineSetDepthStencil;
    NINE_MISSING(pfnGenerateMipSubLevels, 64);
    Functions->pfnSetPixelShaderConstI = NineSetPixelShaderConstI;
    Functions->pfnSetPixelShaderConstB = NineSetPixelShaderConstB;
    Functions->pfnCreatePixelShader = NineCreatePixelShader;
    Functions->pfnDeletePixelShader = NineDeletePixelShader;
    NINE_MISSING(pfnCreateDecodeDevice, 69);
    NINE_MISSING(pfnDestroyDecodeDevice, 70);
    NINE_MISSING(pfnSetDecodeRenderTarget, 71);
    NINE_MISSING(pfnDecodeBeginFrame, 72);
    NINE_MISSING(pfnDecodeEndFrame, 73);
    NINE_MISSING(pfnDecodeExecute, 74);
    NINE_MISSING(pfnDecodeExtensionExecute, 75);
    NINE_MISSING(pfnCreateVideoProcessDevice, 76);
    NINE_MISSING(pfnDestroyVideoProcessDevice, 77);
    NINE_MISSING(pfnVideoProcessBeginFrame, 78);
    NINE_MISSING(pfnVideoProcessEndFrame, 79);
    NINE_MISSING(pfnSetVideoProcessRenderTarget, 80);
    NINE_MISSING(pfnVideoProcessBlt, 81);
    Functions->pfnCreateExtensionDevice = NineFixedAnswer<NotDone>;
    Functions->pfnDestroyExtensionDevice = NineFixedAnswer<NotDone>;
    Functions->pfnExtensionExecute = NineFixedAnswer<NotDone>;
    Functions->pfnCreateOverlay = NineFixedAnswer<NotDone>;
    Functions->pfnUpdateOverlay = NineFixedAnswer<NotDone>;
    Functions->pfnFlipOverlay = NineFixedAnswer<NotDone>;
    Functions->pfnGetOverlayColorControls = NineFixedAnswer<NotDone>;
    Functions->pfnSetOverlayColorControls = NineFixedAnswer<NotDone>;
    Functions->pfnDestroyOverlay = NineFixedAnswer<NotDone>;
    Functions->pfnDestroyDevice = NineDestroyDevice;
    NINE_MISSING(pfnQueryResourceResidency, 92);
    NINE_MISSING(pfnOpenResource, 93);
    Functions->pfnGetCaptureAllocationHandle = NineFixedAnswer<NotDone>;
    Functions->pfnCaptureToSysMem = NineFixedAnswer<NotDone>;
    Functions->pfnLockAsync = NineLockAsync;
    Functions->pfnUnlockAsync = NineUnlockAsync;
    Functions->pfnRename = NineRename;
    NINE_MISSING(pfnCreateVideoProcessor, 99);
    NINE_MISSING(pfnSetVideoProcessBltState, 100);
    NINE_MISSING(pfnGetVideoProcessBltStatePrivate, 101);
    NINE_MISSING(pfnSetVideoProcessStreamState, 102);
    NINE_MISSING(pfnGetVideoProcessStreamStatePrivate, 103);
    NINE_MISSING(pfnVideoProcessBltHD, 104);
    NINE_MISSING(pfnDestroyVideoProcessor, 105);
    Functions->pfnCreateAuthenticatedChannel = NineFixedAnswer<NotDone>;
    Functions->pfnAuthenticatedChannelKeyExchange = NineFixedAnswer<NotDone>;
    Functions->pfnQueryAuthenticatedChannel = NineFixedAnswer<NotDone>;
    Functions->pfnConfigureAuthenticatedChannel = NineFixedAnswer<NotDone>;
    Functions->pfnDestroyAuthenticatedChannel = NineFixedAnswer<NotDone>;
    Functions->pfnCreateCryptoSession = NineFixedAnswer<NotDone>;
    Functions->pfnCryptoSessionKeyExchange = NineFixedAnswer<NotDone>;
    Functions->pfnDestroyCryptoSession = NineFixedAnswer<NotDone>;
    Functions->pfnEncryptionBlt = NineFixedAnswer<NotDone>;
    Functions->pfnGetPitch = NineFixedAnswer<NotDone>;
    Functions->pfnStartSessionKeyRefresh = NineFixedAnswer<NotDone>;
    Functions->pfnFinishSessionKeyRefresh = NineFixedAnswer<NotDone>;
    Functions->pfnGetEncryptionBltKey = NineFixedAnswer<NotDone>;
    Functions->pfnDecryptionBlt = NineFixedAnswer<NotDone>;
    Functions->pfnResolveSharedResource = NineFixedAnswer<NotDone>;
    Functions->pfnVolBlt1 = NineVolBlt1;
    Functions->pfnBufBlt1 = NineBufBlt1;
    Functions->pfnTexBlt1 = NineTexBlt1;
    Functions->pfnDiscard = NineFixedAnswer<NotDone>;
    Functions->pfnOfferResources = NineFixedAnswer<NotDone>;
    Functions->pfnReclaimResources = NineFixedAnswer<NotDone>;
    Functions->pfnCheckDirectFlipSupport = NineFixedAnswer<NotDone>;
    Functions->pfnCreateResource2 = NineCreateResource2;
    Functions->pfnCheckMultiPlaneOverlaySupport = NineFixedAnswer<NotDone>;
    Functions->pfnPresentMultiPlaneOverlay = NineFixedAnswer<NotDone>;
    Functions->pfnReserved1 = nullptr;
    Functions->pfnFlush1 = NineFlush1;
    Functions->pfnCheckCounterInfo = NineCheckCounterInfo;
    Functions->pfnCheckCounter = NineAlwaysDone<>;
    NINE_MISSING(pfnUpdateSubresourceUP, 135);
    Functions->pfnPresent1 = NinePresent1;
    Functions->pfnCheckPresentDurationSupport = NineFixedAnswer<NotDone>;
    Functions->pfnSetMarker = NineAlwaysDone<>;
    Functions->pfnSetMarkerMode = NineAlwaysDone<>;
    Functions->pfnTrimResidencySet = nullptr;
    Functions->pfnAcquireResource = NineAcquireResource;
    Functions->pfnReleaseResource = NineReleaseResource;

    /* A single threaded runtime never renames or locks asynchronously */
    if (NineGetSettings().SingleThread)
    {
        Functions->pfnLockAsync = nullptr;
        Functions->pfnUnlockAsync = nullptr;
        Functions->pfnRename = nullptr;
    }
}
