/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The Direct3D 9 device on top of the translation layer's immediate context
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "d3d9on12.hpp"

/* A piece of an upload buffer: where the GPU and the CPU see it */
struct NineUploadSlice
{
    TL::Resource* Buffer;
    BYTE* Cpu;
    UINT Offset;
};

/* Hands out aligned pieces of one mapped upload buffer, replacing the buffer when it runs out */
struct NineUploadRing
{
    NineDevice* Owner;
    UINT Capacity;
    UINT Alignment;
    UINT Cursor;
    TL::unique_comptr<TL::Resource> Buffer;
    BYTE* Mapped;
};

/* Constant buffers each shader stage reads: floats, integers and booleans */
enum NineConstantKind
{
    NineConstantFloat,
    NineConstantInteger,
    NineConstantBoolean,
    NineConstantKindCount
};

/* Texture stages: 16 pixel samplers, the displacement map sampler and 4 vertex samplers */
static constexpr UINT NineStageCount = 21;
static constexpr UINT NineStageStateCount = 35;
static constexpr UINT NineRenderStateCount = 256;
static constexpr UINT NineClipPlaneCount = 8;

/* Registers each constant kind holds, and the bytes one register takes */
static constexpr UINT NineConstantRegisters[NineConstantKindCount] = { 256, 16, 16 };
static constexpr UINT NineConstantStride[NineConstantKindCount] = { 16, 16, 4 };

/* One stage's constants as the application set them, before they are uploaded */
struct NineStageConstants
{
    BYTE Float[256 * 16];
    BYTE Integer[16 * 16];
    BYTE Boolean[16 * 4];
    BOOLEAN Dirty[NineConstantKindCount];
};

/* The fixed function and pipeline state the DDI sets between draws */
struct NineDeviceState
{
    DWORD RenderStates[NineRenderStateCount];
    DWORD StageStates[NineStageCount][NineStageStateCount];
    NineStageConstants VertexConstants;
    NineStageConstants PixelConstants;
    FLOAT ClipPlanes[NineClipPlaneCount][4];
    BOOLEAN ClipPlanesDirty;
    FLOAT Viewport[4];
    D3DDDIARG_ZRANGE ZRange;
    BOOLEAN ViewportDirty;
    RECT Scissor;
    D3DDDIARG_WINFO WInfo;
};

struct NineDevice
{
    NineAdapter* Adapter;

    /* The runtime's own handle and callbacks, kept to call back into d3d9.dll */
    HANDLE RuntimeDevice;
    UINT RuntimeVersion;
    D3DDDI_DEVICECALLBACKS Callbacks;

    ID3D12Device* Device12;
    ID3D12CommandQueue* Queue;

    std::optional<TL::ImmediateContext> Context;
    std::optional<TL::SharedResourceHelpers> Sharing;

    D3D12_FEATURE_DATA_D3D12_OPTIONS19 Options19;

    /* The buffer the zeroed constants live in until a stage sets its own */
    NineUploadRing ConstantRing;
    NineUploadSlice ZeroConstants;
    NineUploadSlice VertexConstants[NineConstantKindCount];
    NineUploadSlice PixelConstants[NineConstantKindCount];

    NineDeviceState State;
};

/* device.cpp */
HRESULT
APIENTRY
NineCreateDevice(
    _In_ HANDLE hAdapter,
    _Inout_ D3DDDIARG_CREATEDEVICE* Args);

HRESULT
APIENTRY
NineDestroyDevice(
    _In_ HANDLE hDevice);

/* ddi.cpp */
VOID
NineFillDeviceFuncs(
    _Out_ D3DDDI_DEVICEFUNCS* Functions);

/* state.cpp */
HRESULT
APIENTRY
NineSetRenderState(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_RENDERSTATE* Args);

HRESULT
APIENTRY
NineUpdateWInfo(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_WINFO* Args);

HRESULT
APIENTRY
NineSetTextureStageState(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_TEXTURESTAGESTATE* Args);

HRESULT
APIENTRY
NineSetPixelShaderConst(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETPIXELSHADERCONST* Args,
    _In_ const FLOAT* Values);

HRESULT
APIENTRY
NineSetPixelShaderConstI(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETPIXELSHADERCONSTI* Args,
    _In_ const INT* Values);

HRESULT
APIENTRY
NineSetPixelShaderConstB(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETPIXELSHADERCONSTB* Args,
    _In_ const BOOL* Values);

HRESULT
APIENTRY
NineSetVertexShaderConst(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETVERTEXSHADERCONST* Args,
    _In_ const VOID* Values);

HRESULT
APIENTRY
NineSetVertexShaderConstI(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETVERTEXSHADERCONSTI* Args,
    _In_ const INT* Values);

HRESULT
APIENTRY
NineSetVertexShaderConstB(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETVERTEXSHADERCONSTB* Args,
    _In_ const BOOL* Values);

HRESULT
APIENTRY
NineSetViewport(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_VIEWPORTINFO* Args);

HRESULT
APIENTRY
NineSetZRange(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_ZRANGE* Args);

HRESULT
APIENTRY
NineSetScissorRect(
    _In_ HANDLE hDevice,
    _In_ const RECT* Rect);

HRESULT
APIENTRY
NineSetClipPlane(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETCLIPPLANE* Args);

HRESULT
APIENTRY
NineFlush1(
    _In_ HANDLE hDevice,
    _In_ UINT FlushFlags);
