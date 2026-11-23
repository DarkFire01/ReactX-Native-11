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
