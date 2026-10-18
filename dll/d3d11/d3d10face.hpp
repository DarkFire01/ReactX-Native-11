/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The Direct3D 10 face of a device made for d3d10.dll and d3d10_1.dll
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

class RxDevice;
class RxChildLifetime;
class RxImmediateContext;

/* A Direct3D 10 face of one object; the object owns it */
class RxD3D10FaceBase
{
public:
    virtual ~RxD3D10FaceBase() = default;

    virtual
    BOOLEAN
    Answers(
        _In_ REFIID riid) = 0;

    virtual
    IUnknown*
    GetUnknown() = 0;
};

/* What d3d10.dll and d3d10_1.dll add to the creation flags so the device answers as Direct3D 10 */
constexpr UINT RxD3D10DeviceFlag = 0x40000000;

/* A tear-off of RxDevice: same lifetime, QueryInterface goes back to the device */
class RxD3D10Device final : public ID3D10Device1
{
public:
    explicit RxD3D10Device(
        _In_ RxDevice& Owner)
        : Device(Owner)
    {
    }

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

#include "d3d10device.inl"

private:
    RxImmediateContext&
    Context();

    RxDevice& Device;
    UINT TextFilterWidth = 0;
    UINT TextFilterHeight = 0;
};

/*
 * Where an object's QueryInterface ends up when it did not know the IID: on a Direct3D 10
 * device the object also has a Direct3D 10 face, made the first time one is asked for.
 */
HRESULT
RxQueryD3D10Face(
    _In_ RxChildLifetime& Lifetime,
    _In_ ID3D11DeviceChild* Object,
    _In_ RxDevice& Device,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject);
