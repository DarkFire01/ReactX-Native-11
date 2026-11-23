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

    NINE_MISSING(pfnSetRenderState, 0);
    NINE_MISSING(pfnUpdateWInfo, 1);
    Functions->pfnValidateDevice = NineValidateDevice;
    NINE_MISSING(pfnSetTextureStageState, 3);
    NINE_MISSING(pfnSetTexture, 4);
    NINE_MISSING(pfnSetPixelShader, 5);
    NINE_MISSING(pfnSetPixelShaderConst, 6);
    NINE_MISSING(pfnSetStreamSourceUm, 7);
    NINE_MISSING(pfnSetIndices, 8);
    NINE_MISSING(pfnSetIndicesUm, 9);
    NINE_MISSING(pfnDrawPrimitive, 10);
    NINE_MISSING(pfnDrawIndexedPrimitive, 11);
    Functions->pfnDrawRectPatch = NineFixedAnswer<NotDone>;
    Functions->pfnDrawTriPatch = NineFixedAnswer<NotDone>;
    NINE_MISSING(pfnDrawPrimitive2, 14);
    NINE_MISSING(pfnDrawIndexedPrimitive2, 15);
    Functions->pfnVolBlt = nullptr;
    Functions->pfnBufBlt = nullptr;
    Functions->pfnTexBlt = nullptr;
    Functions->pfnStateSet = NineFixedAnswer<S_OK>;
    Functions->pfnSetPriority = NineFixedAnswer<NotDone>;
    NINE_MISSING(pfnClear, 21);
    Functions->pfnUpdatePalette = NineFixedAnswer<NotDone>;
    Functions->pfnSetPalette = NineFixedAnswer<NotDone>;
    NINE_MISSING(pfnSetVertexShaderConst, 24);
    Functions->pfnMultiplyTransform = NineFixedAnswer<S_OK>;
    Functions->pfnSetTransform = NineFixedAnswer<S_OK>;
    NINE_MISSING(pfnSetViewport, 27);
    NINE_MISSING(pfnSetZRange, 28);
    Functions->pfnSetMaterial = NineFixedAnswer<S_OK>;
    Functions->pfnSetLight = NineFixedAnswer<S_OK>;
    Functions->pfnCreateLight = NineFixedAnswer<S_OK>;
    Functions->pfnDestroyLight = NineFixedAnswer<S_OK>;
    NINE_MISSING(pfnSetClipPlane, 33);
    Functions->pfnGetInfo = NineFixedAnswer<NotDone>;
    NINE_MISSING(pfnLock, 35);
    NINE_MISSING(pfnUnlock, 36);
    Functions->pfnCreateResource = nullptr;
    NINE_MISSING(pfnDestroyResource, 38);
    Functions->pfnSetDisplayMode = NineFixedAnswer<S_OK>;
    Functions->pfnPresent = nullptr;
    Functions->pfnFlush = nullptr;
    NINE_MISSING(pfnCreateVertexShaderFunc, 42);
    NINE_MISSING(pfnDeleteVertexShaderFunc, 43);
    NINE_MISSING(pfnSetVertexShaderFunc, 44);
    NINE_MISSING(pfnCreateVertexShaderDecl, 45);
    NINE_MISSING(pfnDeleteVertexShaderDecl, 46);
    NINE_MISSING(pfnSetVertexShaderDecl, 47);
    NINE_MISSING(pfnSetVertexShaderConstI, 48);
    NINE_MISSING(pfnSetVertexShaderConstB, 49);
    NINE_MISSING(pfnSetScissorRect, 50);
    NINE_MISSING(pfnSetStreamSource, 51);
    NINE_MISSING(pfnSetStreamSourceFreq, 52);
    Functions->pfnSetConvolutionKernelMono = NineFixedAnswer<NotDone>;
    Functions->pfnComposeRects = NineFixedAnswer<NotDone>;
    NINE_MISSING(pfnBlt, 55);
    NINE_MISSING(pfnColorFill, 56);
    NINE_MISSING(pfnDepthFill, 57);
    NINE_MISSING(pfnCreateQuery, 58);
    NINE_MISSING(pfnDestroyQuery, 59);
    NINE_MISSING(pfnIssueQuery, 60);
    NINE_MISSING(pfnGetQueryData, 61);
    NINE_MISSING(pfnSetRenderTarget, 62);
    NINE_MISSING(pfnSetDepthStencil, 63);
    NINE_MISSING(pfnGenerateMipSubLevels, 64);
    NINE_MISSING(pfnSetPixelShaderConstI, 65);
    NINE_MISSING(pfnSetPixelShaderConstB, 66);
    NINE_MISSING(pfnCreatePixelShader, 67);
    NINE_MISSING(pfnDeletePixelShader, 68);
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
    NINE_MISSING(pfnLockAsync, 96);
    NINE_MISSING(pfnUnlockAsync, 97);
    NINE_MISSING(pfnRename, 98);
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
    NINE_MISSING(pfnVolBlt1, 121);
    NINE_MISSING(pfnBufBlt1, 122);
    NINE_MISSING(pfnTexBlt1, 123);
    Functions->pfnDiscard = NineFixedAnswer<NotDone>;
    Functions->pfnOfferResources = NineFixedAnswer<NotDone>;
    Functions->pfnReclaimResources = NineFixedAnswer<NotDone>;
    Functions->pfnCheckDirectFlipSupport = NineFixedAnswer<NotDone>;
    NINE_MISSING(pfnCreateResource2, 128);
    Functions->pfnCheckMultiPlaneOverlaySupport = NineFixedAnswer<NotDone>;
    Functions->pfnPresentMultiPlaneOverlay = NineFixedAnswer<NotDone>;
    Functions->pfnReserved1 = nullptr;
    NINE_MISSING(pfnFlush1, 132);
    Functions->pfnCheckCounterInfo = NineCheckCounterInfo;
    Functions->pfnCheckCounter = NineAlwaysDone<>;
    NINE_MISSING(pfnUpdateSubresourceUP, 135);
    NINE_MISSING(pfnPresent1, 136);
    Functions->pfnCheckPresentDurationSupport = NineFixedAnswer<NotDone>;
    Functions->pfnSetMarker = NineAlwaysDone<>;
    Functions->pfnSetMarkerMode = NineAlwaysDone<>;
    Functions->pfnTrimResidencySet = nullptr;
    Functions->pfnAcquireResource = NineMissingSyncToken<141>;
    Functions->pfnReleaseResource = NineMissingSyncToken<142>;

    /* A single threaded runtime never renames or locks asynchronously */
    if (NineGetSettings().SingleThread)
    {
        Functions->pfnLockAsync = nullptr;
        Functions->pfnUnlockAsync = nullptr;
        Functions->pfnRename = nullptr;
    }
}
