/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader constants, shaders and vertex declarations
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* What the runtime's own vertex pipeline and the reference rasterizer hold */
static constexpr UINT D9WideFloatRegisters = 8192;
static constexpr UINT D9WideIntRegisters = 2048;

/* Registers a version 2 or 3 machine has of integer and boolean constants */
static constexpr UINT D9ShaderModelIntRegisters = 16;

/* A range that runs past the registers the device has; the sum cannot wrap */
static
BOOLEAN
D9OutsideRegisters(
    _In_ UINT Start,
    _In_ UINT Count,
    _In_ UINT Registers)
{
    return static_cast<ULONGLONG>(Start) + Count > Registers;
}

/* Works out how many constants of each kind the device has from its caps and vertex processing, then makes
   room for them, all zero */
HRESULT
D9Device::CreateConstants(VOID)
{
    BOOLEAN Reference = DeviceType == D3DDEVTYPE_REF || DeviceType == D3DDEVTYPE_NULLREF;
    BOOLEAN SoftwareType = DeviceType == D3DDEVTYPE_SW;
    DWORD VertexVersion = Caps.VertexShaderVersion;
    DWORD PixelVersion = Caps.PixelShaderVersion;
    D9ConstantLimits* Kept = &Limits;

    ZeroMemory(Kept, sizeof(*Kept));
    if (VertexVersion > D3DVS_VERSION(3, 0) || Reference || (SoftwareType && VertexVersion >= D3DVS_VERSION(3, 0)))
    {
        Kept->DriverVertexFloats = D9WideFloatRegisters;
        Kept->DriverVertexInts = D9WideIntRegisters;
    }
    else
    {
        Kept->DriverVertexFloats = Caps.MaxVertexShaderConst;
        if (VertexVersion >= D3DVS_VERSION(2, 0))
            Kept->DriverVertexInts = D9ShaderModelIntRegisters;
    }
    Kept->VertexFloats = Kept->DriverVertexFloats;
    Kept->VertexInts = Kept->DriverVertexInts;

    /* The runtime's own vertex pipeline has the widest machine whatever the driver reports */
    if ((BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)) != 0)
    {
        if ((BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) != 0 || Kept->VertexFloats < D9WideFloatRegisters)
            Kept->VertexFloats = D9WideFloatRegisters;
        Kept->VertexInts = D9WideIntRegisters;
    }
    Kept->VertexBools = Kept->VertexInts;

    if (PixelVersion > D3DPS_VERSION(3, 0) || Reference || (SoftwareType && PixelVersion >= D3DPS_VERSION(3, 0)))
    {
        Kept->PixelFloats = D9WideFloatRegisters;
        Kept->PixelInts = D9WideIntRegisters;
    }
    else if (PixelVersion >= D3DPS_VERSION(1, 1) && PixelVersion <= D3DPS_VERSION(1, 4))
    {
        Kept->PixelFloats = 8;
    }
    else if (PixelVersion == D3DPS_VERSION(2, 0))
    {
        /* Version 2 pixel machines only have integer and boolean registers with static flow control */
        Kept->PixelFloats = 32;
        if (Caps.PS20Caps.StaticFlowControlDepth != 0)
            Kept->PixelInts = D9ShaderModelIntRegisters;
    }
    else if (PixelVersion == D3DPS_VERSION(3, 0))
    {
        Kept->PixelFloats = 224;
        Kept->PixelInts = D9ShaderModelIntRegisters;
    }
    Kept->PixelBools = Kept->PixelInts;

    VertexFloats = new (std::nothrow) FLOAT[Kept->VertexFloats + 1][4]();
    VertexInts = new (std::nothrow) INT[Kept->VertexInts + 1][4]();
    VertexBools = new (std::nothrow) BOOL[Kept->VertexBools + 1]();
    PixelFloats = new (std::nothrow) FLOAT[Kept->PixelFloats + 1][4]();
    PixelInts = new (std::nothrow) INT[Kept->PixelInts + 1][4]();
    PixelBools = new (std::nothrow) BOOL[Kept->PixelBools + 1]();
    if (VertexFloats == nullptr || VertexInts == nullptr || VertexBools == nullptr || PixelFloats == nullptr ||
        PixelInts == nullptr || PixelBools == nullptr)
    {
        D9Trace(L"CreateConstants: no memory for %u vertex constants", Kept->VertexFloats);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

/* A plain device's reset starts every constant over at zero */
VOID
D9Device::ClearConstants(VOID)
{
    ZeroMemory(VertexFloats, Limits.VertexFloats * sizeof(*VertexFloats));
    ZeroMemory(VertexInts, Limits.VertexInts * sizeof(*VertexInts));
    ZeroMemory(VertexBools, Limits.VertexBools * sizeof(*VertexBools));
    ZeroMemory(PixelFloats, Limits.PixelFloats * sizeof(*PixelFloats));
    ZeroMemory(PixelInts, Limits.PixelInts * sizeof(*PixelInts));
    ZeroMemory(PixelBools, Limits.PixelBools * sizeof(*PixelBools));
    VertexConstantsPending = FALSE;
}

/* The driver's pipeline may hold fewer vertex registers than a mixed device's own; while the runtime processes
   vertices itself the constants wait for the hardware pipeline */
VOID
D9Device::SendVertexConstants(
    _In_ D9BatchCall Call,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_bytes_opt_(Count * Stride) const VOID* Data,
    _In_ UINT Stride)
{
    UINT Registers = Call == D9BatchCall::VertexShaderConst ? Limits.DriverVertexFloats : Limits.DriverVertexInts;

    if (SoftwareVertices)
    {
        VertexConstantsPending = TRUE;
        return;
    }
    if (D9OutsideRegisters(Start, Count, Registers))
    {
        if (Start >= Registers)
            return;
        Count = Registers - Start;
    }
    DdiShaderConstants(Call, Start, Count, Data, Count * Stride);
}

/* Float pixel constants share the driver's registers with the runtime's fixed function pixel shaders, so
   they wait until the application binds a pixel shader of its own */
VOID
D9Device::SendPixelConstants(
    _In_ D9BatchCall Call,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_bytes_opt_(Count * Stride) const VOID* Data,
    _In_ UINT Stride)
{
    if (Call == D9BatchCall::PixelShaderConst)
        return;
    DdiShaderConstants(Call, Start, Count, Data, Count * Stride);
}

STDMETHODIMP
D9Device::SetVertexShaderConstantF(
    _In_ UINT StartRegister,
    _In_opt_ const float* ConstantData,
    _In_ UINT Vector4fCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, Vector4fCount, Limits.VertexFloats))
        return D3DERR_INVALIDCALL;
    CopyMemory(VertexFloats[StartRegister], ConstantData, Vector4fCount * sizeof(*VertexFloats));
    SendVertexConstants(D9BatchCall::VertexShaderConst, StartRegister, Vector4fCount, ConstantData,
                        sizeof(*VertexFloats));
    return S_OK;
}

STDMETHODIMP
D9Device::GetVertexShaderConstantF(
    _In_ UINT StartRegister,
    _Out_ float* ConstantData,
    _In_ UINT Vector4fCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, Vector4fCount, Limits.VertexFloats))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, VertexFloats[StartRegister], Vector4fCount * sizeof(*VertexFloats));
    return S_OK;
}

STDMETHODIMP
D9Device::SetVertexShaderConstantI(
    _In_ UINT StartRegister,
    _In_opt_ const int* ConstantData,
    _In_ UINT Vector4iCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, Vector4iCount, Limits.VertexInts))
        return D3DERR_INVALIDCALL;
    CopyMemory(VertexInts[StartRegister], ConstantData, Vector4iCount * sizeof(*VertexInts));
    SendVertexConstants(D9BatchCall::VertexShaderConstI, StartRegister, Vector4iCount, ConstantData,
                        sizeof(*VertexInts));
    return S_OK;
}

STDMETHODIMP
D9Device::GetVertexShaderConstantI(
    _In_ UINT StartRegister,
    _Out_ int* ConstantData,
    _In_ UINT Vector4iCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, Vector4iCount, Limits.VertexInts))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, VertexInts[StartRegister], Vector4iCount * sizeof(*VertexInts));
    return S_OK;
}

STDMETHODIMP
D9Device::SetVertexShaderConstantB(
    _In_ UINT StartRegister,
    _In_opt_ const BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, BoolCount, Limits.VertexBools))
        return D3DERR_INVALIDCALL;
    CopyMemory(&VertexBools[StartRegister], ConstantData, BoolCount * sizeof(*VertexBools));
    SendVertexConstants(D9BatchCall::VertexShaderConstB, StartRegister, BoolCount, ConstantData,
                        sizeof(*VertexBools));
    return S_OK;
}

STDMETHODIMP
D9Device::GetVertexShaderConstantB(
    _In_ UINT StartRegister,
    _Out_ BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, BoolCount, Limits.VertexBools))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, &VertexBools[StartRegister], BoolCount * sizeof(*VertexBools));
    return S_OK;
}

STDMETHODIMP
D9Device::SetPixelShaderConstantF(
    _In_ UINT StartRegister,
    _In_opt_ const float* ConstantData,
    _In_ UINT Vector4fCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, Vector4fCount, Limits.PixelFloats))
        return D3DERR_INVALIDCALL;
    CopyMemory(PixelFloats[StartRegister], ConstantData, Vector4fCount * sizeof(*PixelFloats));
    SendPixelConstants(D9BatchCall::PixelShaderConst, StartRegister, Vector4fCount, ConstantData,
                       sizeof(*PixelFloats));
    return S_OK;
}

STDMETHODIMP
D9Device::GetPixelShaderConstantF(
    _In_ UINT StartRegister,
    _Out_ float* ConstantData,
    _In_ UINT Vector4fCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, Vector4fCount, Limits.PixelFloats))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, PixelFloats[StartRegister], Vector4fCount * sizeof(*PixelFloats));
    return S_OK;
}

STDMETHODIMP
D9Device::SetPixelShaderConstantI(
    _In_ UINT StartRegister,
    _In_opt_ const int* ConstantData,
    _In_ UINT Vector4iCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, Vector4iCount, Limits.PixelInts))
        return D3DERR_INVALIDCALL;
    CopyMemory(PixelInts[StartRegister], ConstantData, Vector4iCount * sizeof(*PixelInts));
    SendPixelConstants(D9BatchCall::PixelShaderConstI, StartRegister, Vector4iCount, ConstantData,
                       sizeof(*PixelInts));
    return S_OK;
}

STDMETHODIMP
D9Device::GetPixelShaderConstantI(
    _In_ UINT StartRegister,
    _Out_ int* ConstantData,
    _In_ UINT Vector4iCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, Vector4iCount, Limits.PixelInts))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, PixelInts[StartRegister], Vector4iCount * sizeof(*PixelInts));
    return S_OK;
}

STDMETHODIMP
D9Device::SetPixelShaderConstantB(
    _In_ UINT StartRegister,
    _In_opt_ const BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, BoolCount, Limits.PixelBools))
        return D3DERR_INVALIDCALL;
    CopyMemory(&PixelBools[StartRegister], ConstantData, BoolCount * sizeof(*PixelBools));
    SendPixelConstants(D9BatchCall::PixelShaderConstB, StartRegister, BoolCount, ConstantData,
                       sizeof(*PixelBools));
    return S_OK;
}

STDMETHODIMP
D9Device::GetPixelShaderConstantB(
    _In_ UINT StartRegister,
    _Out_ BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, BoolCount, Limits.PixelBools))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, &PixelBools[StartRegister], BoolCount * sizeof(*PixelBools));
    return S_OK;
}
