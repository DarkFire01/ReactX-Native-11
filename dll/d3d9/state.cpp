/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Fixed function and pipeline state the application sets and reads back
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Render states past the last one land on it */
static constexpr UINT D9LastRenderState = D9RenderStateCount - 1;

/* Vertex pipeline states the driver never sees while the runtime processes vertices itself */
static const BYTE D9SoftwareOnlyStates[] =
{
    48, 137, 139, 140, 141, 142, 143, 145, 146, 147, 148, 151, 152, 157, 158, 159, 160, 167, 170,
};

/* Vertex pipeline states the runtime turns into vertex shaders instead of handing on */
static const BYTE D9ShaderPipelineStates[] =
{
    137, 139, 140, 141, 142, 143, 145, 146, 147, 148, 151, 157, 158, 159, 160, 167, 170,
};

/* Where the texture factor lives among the fixed function pixel constants */
static constexpr UINT D9FactorRegister = 2;

static
BOOLEAN
D9ListHolds(
    _In_reads_(Count) const BYTE* List,
    _In_ SIZE_T Count,
    _In_ UINT State)
{
    for (SIZE_T Index = 0; Index < Count; Index++)
    {
        if (List[Index] == State)
            return TRUE;
    }
    return FALSE;
}

/* One color channel as the fixed function pixel constants hold it */
static
FLOAT
D9Channel(
    _In_ DWORD Color,
    _In_ UINT Shift)
{
    return static_cast<FLOAT>(((Color >> Shift) & 0xFF) / 255.0);
}

/* A pure device keeps no state only when its driver lacks version 2 shaders; otherwise it runs as any other */
BOOLEAN
D9Device::KeepsNoState(VOID) const
{
    if ((BehaviorFlags & D3DCREATE_PUREDEVICE) == 0)
        return FALSE;
    return Caps.VertexShaderVersion < D3DVS_VERSION(2, 0) && Caps.PixelShaderVersion < D3DPS_VERSION(2, 0);
}

/* Hands a render state on, unless the runtime's own pipelines consume it */
VOID
D9Device::SendStateValue(
    _In_ UINT State,
    _In_ DWORD Value)
{
    D3DDDIARG_RENDERSTATE Args = {};

    if (SoftwareVertices && D9ListHolds(D9SoftwareOnlyStates, _countof(D9SoftwareOnlyStates), State))
        return;
    if (!SoftwareVertices && D9ListHolds(D9ShaderPipelineStates, _countof(D9ShaderPipelineStates), State))
        return;

    /* The pixel shaders standing in for the fixed function stages take these two */
    if (State == D3DRS_SPECULARENABLE)
        return;
    if (State == D3DRS_TEXTUREFACTOR)
    {
        SendPixelConstant(D9FactorRegister, D9Channel(Value, 16), D9Channel(Value, 8), D9Channel(Value, 0),
                          D9Channel(Value, 24));
        return;
    }

    Args.State = static_cast<D3DDDIRENDERSTATETYPE>(State);
    Args.Value = Value;
    DdiRenderState(&Args);
}

STDMETHODIMP
D9Device::SetRenderState(
    _In_ D3DRENDERSTATETYPE State,
    _In_ DWORD Value)
{
    D9_DEVICE_LOCK();
    UINT Slot = static_cast<UINT>(State) > D9LastRenderState ? D9LastRenderState : static_cast<UINT>(State);

    /* Undefined and retired states reach the driver like any other */
    if (!KeepsNoState())
    {
        if (RenderStates[Slot] == Value)
            return S_OK;
        RenderStates[Slot] = Value;
    }
    SendStateValue(Slot, Value);
    return S_OK;
}

STDMETHODIMP
D9Device::GetRenderState(
    _In_ D3DRENDERSTATETYPE State,
    _Out_ DWORD* Value)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState() || static_cast<DWORD>(State) >= D9RenderStateCount || D9IsRetiredState(State))
        return D3DERR_INVALIDCALL;

    *Value = RenderStates[State];
    return S_OK;
}
