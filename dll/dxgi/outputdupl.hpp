/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Desktop duplication of one output, fed by the kernel from the DWM's frames
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "dxgiobjects.hpp"
#include "rxcomptr.hpp"

#include <d3d11_4.h>
#include <d3d10umddi.h>
#include <array>

/*
 * The caller's device owns the frame textures and shares each with the kernel by NT handle.
 * The kernel copies every frame the DWM presents into the current texture and passes it over
 * through the texture's keyed mutex; ReleaseFrame gives it back and names the next one.
 */
class RxOutputDuplication final : public IDXGIOutputDuplication
{
public:
    static
    HRESULT
    Create(
        _In_ RxDxgiOutput* Output,
        _In_ IUnknown* Device,
        _In_ const DXGI_OUTDUPL_DESC& Desc,
        _In_ UINT Flags,
        _In_ UINT FormatCount,
        _In_reads_(FormatCount) const DXGI_FORMAT* Formats,
        _Outptr_ RxOutputDuplication** Duplication);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateData(
        _In_ REFGUID Name,
        _In_ UINT DataSize,
        _In_reads_bytes_(DataSize) const void* pData) override;

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateDataInterface(
        _In_ REFGUID Name,
        _In_opt_ const IUnknown* pUnknown) override;

    HRESULT
    STDMETHODCALLTYPE
    GetPrivateData(
        _In_ REFGUID Name,
        _Inout_ UINT* pDataSize,
        _Out_writes_bytes_(*pDataSize) void* pData) override;

    HRESULT
    STDMETHODCALLTYPE
    GetParent(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppParent) override;

    void
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ DXGI_OUTDUPL_DESC* pDesc) override;

    HRESULT
    STDMETHODCALLTYPE
    AcquireNextFrame(
        _In_ UINT TimeoutInMilliseconds,
        _Out_ DXGI_OUTDUPL_FRAME_INFO* pFrameInfo,
        _COM_Outptr_ IDXGIResource** ppDesktopResource) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFrameDirtyRects(
        _In_ UINT DirtyRectsBufferSize,
        _Out_writes_bytes_to_(DirtyRectsBufferSize, *pDirtyRectsBufferSizeRequired) RECT* pDirtyRectsBuffer,
        _Out_ UINT* pDirtyRectsBufferSizeRequired) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFrameMoveRects(
        _In_ UINT MoveRectsBufferSize,
        _Out_writes_bytes_to_(MoveRectsBufferSize, *pMoveRectsBufferSizeRequired) DXGI_OUTDUPL_MOVE_RECT* pMoveRectBuffer,
        _Out_ UINT* pMoveRectsBufferSizeRequired) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFramePointerShape(
        _In_ UINT PointerShapeBufferSize,
        _Out_writes_bytes_to_(PointerShapeBufferSize, *pPointerShapeBufferSizeRequired) void* pPointerShapeBuffer,
        _Out_ UINT* pPointerShapeBufferSizeRequired,
        _Out_ DXGI_OUTDUPL_POINTER_SHAPE_INFO* pPointerShapeInfo) override;

    HRESULT
    STDMETHODCALLTYPE
    MapDesktopSurface(
        _Out_ DXGI_MAPPED_RECT* pLockedRect) override;

    HRESULT
    STDMETHODCALLTYPE
    UnMapDesktopSurface(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    ReleaseFrame(VOID) override;

private:
    explicit RxOutputDuplication(
        _In_ RxDxgiOutput* Output);

    ~RxOutputDuplication();

    HRESULT
    Initialize(
        _In_ IUnknown* Device,
        _In_ UINT Flags,
        _In_ UINT FormatCount,
        _In_reads_(FormatCount) const DXGI_FORMAT* Formats);

    /* One frame texture with its keyed mutex, and the NT handle the kernel opens it by */
    HRESULT
    CreateFrameTexture(
        _In_ const D3D11_TEXTURE2D_DESC& TextureDesc,
        _In_ UINT Index);

    /* The texture handed out when the caller cannot take the desktop format or wants a private copy */
    HRESULT
    CreateCopyTarget(
        _In_ const D3D11_TEXTURE2D_DESC& TextureDesc,
        _In_ UINT FormatCount,
        _In_reads_(FormatCount) const DXGI_FORMAT* Formats);

    /* The kernel's metadata for the frame in hand; the caller holds Lock */
    HRESULT
    QueryMetadata(
        _In_ D3DKMT_OUTPUTDUPL_METADATATYPE Type,
        _In_ UINT BufferSize,
        _Out_writes_bytes_to_(BufferSize, *SizeRequired) VOID* Buffer,
        _Out_ UINT* SizeRequired);

    /* Brings what changed in the acquired frame over to the copy target */
    HRESULT
    UpdateCopyTarget(
        _In_ ID3D11Texture2D* Frame);

    /* What AcquireNextFrame does once the wait on the frame's keyed mutex returned; the caller holds Lock */
    HRESULT
    TakeFrame(
        _In_ HRESULT WaitResult,
        _In_ UINT Index,
        _Out_ DXGI_OUTDUPL_FRAME_INFO* FrameInfo,
        _COM_Outptr_ IDXGIResource** DesktopResource);

    LONG RefCount = 1;
    RxDxgiOutput* Output;
    SRWLOCK Lock = SRWLOCK_INIT;
    DXGI_OUTDUPL_DESC Desc = {};
    RxComPtr<ID3D11Device> Device;
    RxComPtr<IDXGIDevice2> DxgiDevice;
    RxComPtr<ID3D11DeviceContext> Context;
    RxComPtr<ID3D11Multithread> DeviceLock;
    std::array<RxComPtr<ID3D11Texture2D>, OUTPUTDUPL_CREATE_MAX_KEYEDMUTXES> Frames;
    std::array<RxComPtr<IDXGIKeyedMutex>, OUTPUTDUPL_CREATE_MAX_KEYEDMUTXES> FrameMutexes;
    std::array<HANDLE, OUTPUTDUPL_CREATE_MAX_KEYEDMUTXES> FrameHandles = {};
    UINT FrameCount = 0;
    UINT CurrentFrame = 0;
    RxComPtr<ID3D11Texture2D> CopyTarget;
    std::vector<BYTE> CopyRects;
    BOOLEAN IsRegistered = FALSE;
    BOOLEAN IsAcquiring = FALSE;
    BOOLEAN IsFrameHeld = FALSE;
    RxPrivateDataStore PrivateData;
};
