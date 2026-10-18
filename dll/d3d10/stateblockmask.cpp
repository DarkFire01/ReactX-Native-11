/*
 * PROJECT:     ReactX Direct3D 10 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3D10_STATE_BLOCK_MASK helpers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d10runtime.hpp"

/* Where one kind of state lives in the mask, and how many slots it has */
struct RxMaskField
{
    BYTE* Bits;
    UINT Slots;
};

static
BOOLEAN
RxFindField(
    _In_ D3D10_STATE_BLOCK_MASK* Mask,
    _In_ D3D10_DEVICE_STATE_TYPES Type,
    _Out_ RxMaskField* Field)
{
    switch (Type)
    {
        case D3D10_DST_SO_BUFFERS:
            *Field = { &Mask->SOBuffers, D3D10_SO_BUFFER_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_OM_RENDER_TARGETS:
            *Field = { &Mask->OMRenderTargets, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT };
            return TRUE;
        case D3D10_DST_OM_DEPTH_STENCIL_STATE:
            *Field = { &Mask->OMDepthStencilState, 1 };
            return TRUE;
        case D3D10_DST_OM_BLEND_STATE:
            *Field = { &Mask->OMBlendState, 1 };
            return TRUE;
        case D3D10_DST_VS:
            *Field = { &Mask->VS, 1 };
            return TRUE;
        case D3D10_DST_VS_SAMPLERS:
            *Field = { Mask->VSSamplers, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_VS_SHADER_RESOURCES:
            *Field = { Mask->VSShaderResources, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_VS_CONSTANT_BUFFERS:
            *Field = { Mask->VSConstantBuffers, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_GS:
            *Field = { &Mask->GS, 1 };
            return TRUE;
        case D3D10_DST_GS_SAMPLERS:
            *Field = { Mask->GSSamplers, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_GS_SHADER_RESOURCES:
            *Field = { Mask->GSShaderResources, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_GS_CONSTANT_BUFFERS:
            *Field = { Mask->GSConstantBuffers, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_PS:
            *Field = { &Mask->PS, 1 };
            return TRUE;
        case D3D10_DST_PS_SAMPLERS:
            *Field = { Mask->PSSamplers, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_PS_SHADER_RESOURCES:
            *Field = { Mask->PSShaderResources, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_PS_CONSTANT_BUFFERS:
            *Field = { Mask->PSConstantBuffers, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_IA_VERTEX_BUFFERS:
            *Field = { Mask->IAVertexBuffers, D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT };
            return TRUE;
        case D3D10_DST_IA_INDEX_BUFFER:
            *Field = { &Mask->IAIndexBuffer, 1 };
            return TRUE;
        case D3D10_DST_IA_INPUT_LAYOUT:
            *Field = { &Mask->IAInputLayout, 1 };
            return TRUE;
        case D3D10_DST_IA_PRIMITIVE_TOPOLOGY:
            *Field = { &Mask->IAPrimitiveTopology, 1 };
            return TRUE;
        case D3D10_DST_RS_VIEWPORTS:
            *Field = { &Mask->RSViewports, 1 };
            return TRUE;
        case D3D10_DST_RS_SCISSOR_RECTS:
            *Field = { &Mask->RSScissorRects, 1 };
            return TRUE;
        case D3D10_DST_RS_RASTERIZER_STATE:
            *Field = { &Mask->RSRasterizerState, 1 };
            return TRUE;
        case D3D10_DST_PREDICATION:
            *Field = { &Mask->Predication, 1 };
            return TRUE;
        default:
            return FALSE;
    }
}

static
VOID
RxSetBits(
    _In_ const RxMaskField& Field,
    _In_ UINT Start,
    _In_ UINT Length,
    _In_ BOOLEAN Value)
{
    UINT Slot;

    for (Slot = Start; Slot < Start + Length; Slot++)
    {
        if (Value)
            Field.Bits[Slot / 8] |= (BYTE)(1u << (Slot % 8));
        else
            Field.Bits[Slot / 8] &= (BYTE)~(1u << (Slot % 8));
    }
}

static
HRESULT
RxSetCapture(
    _In_opt_ D3D10_STATE_BLOCK_MASK* pMask,
    _In_ D3D10_DEVICE_STATE_TYPES StateType,
    _In_ UINT RangeStart,
    _In_ UINT RangeLength,
    _In_ BOOLEAN Value)
{
    RxMaskField Field;

    if (pMask == nullptr || !RxFindField(pMask, StateType, &Field) || RangeStart >= Field.Slots ||
        RangeLength > Field.Slots - RangeStart)
    {
        return E_INVALIDARG;
    }

    RxSetBits(Field, RangeStart, RangeLength, Value);
    return S_OK;
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskEnableCapture(
    _Inout_ D3D10_STATE_BLOCK_MASK* pMask,
    _In_ D3D10_DEVICE_STATE_TYPES StateType,
    _In_ UINT RangeStart,
    _In_ UINT RangeLength)
{
    return RxSetCapture(pMask, StateType, RangeStart, RangeLength, TRUE);
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskDisableCapture(
    _Inout_ D3D10_STATE_BLOCK_MASK* pMask,
    _In_ D3D10_DEVICE_STATE_TYPES StateType,
    _In_ UINT RangeStart,
    _In_ UINT RangeLength)
{
    return RxSetCapture(pMask, StateType, RangeStart, RangeLength, FALSE);
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskEnableAll(
    _Out_ D3D10_STATE_BLOCK_MASK* pMask)
{
    if (pMask == nullptr)
        return E_INVALIDARG;

    /* Every bit of the mask, spare ones included, as Windows does it */
    FillMemory(pMask, sizeof(*pMask), 0xFF);
    return S_OK;
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskDisableAll(
    _Out_ D3D10_STATE_BLOCK_MASK* pMask)
{
    if (pMask == nullptr)
        return E_INVALIDARG;

    ZeroMemory(pMask, sizeof(*pMask));
    return S_OK;
}

extern "C"
BOOL
WINAPI
D3D10StateBlockMaskGetSetting(
    _In_ D3D10_STATE_BLOCK_MASK* pMask,
    _In_ D3D10_DEVICE_STATE_TYPES StateType,
    _In_ UINT Entry)
{
    RxMaskField Field;

    if (pMask == nullptr || !RxFindField(pMask, StateType, &Field) || Entry >= Field.Slots)
        return FALSE;

    /* The bit itself, not a normalized TRUE */
    return Field.Bits[Entry / 8] & (1u << (Entry % 8));
}

/* Byte by byte over the whole mask */
template <typename Combine>
static
HRESULT
RxCombineMasks(
    _In_opt_ const D3D10_STATE_BLOCK_MASK* pA,
    _In_opt_ const D3D10_STATE_BLOCK_MASK* pB,
    _Out_opt_ D3D10_STATE_BLOCK_MASK* pResult,
    _In_ Combine Operation)
{
    const BYTE* A = reinterpret_cast<const BYTE*>(pA);
    const BYTE* B = reinterpret_cast<const BYTE*>(pB);
    BYTE* Result = reinterpret_cast<BYTE*>(pResult);
    SIZE_T Index;

    if (pA == nullptr || pB == nullptr || pResult == nullptr)
        return E_INVALIDARG;

    for (Index = 0; Index < sizeof(*pResult); Index++)
        Result[Index] = Operation(A[Index], B[Index]);

    return S_OK;
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskDifference(
    _In_ D3D10_STATE_BLOCK_MASK* pA,
    _In_ D3D10_STATE_BLOCK_MASK* pB,
    _Out_ D3D10_STATE_BLOCK_MASK* pResult)
{
    return RxCombineMasks(pA, pB, pResult, [](BYTE A, BYTE B) { return (BYTE)(A ^ B); });
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskIntersect(
    _In_ D3D10_STATE_BLOCK_MASK* pA,
    _In_ D3D10_STATE_BLOCK_MASK* pB,
    _Out_ D3D10_STATE_BLOCK_MASK* pResult)
{
    return RxCombineMasks(pA, pB, pResult, [](BYTE A, BYTE B) { return (BYTE)(A & B); });
}

extern "C"
HRESULT
WINAPI
D3D10StateBlockMaskUnion(
    _In_ D3D10_STATE_BLOCK_MASK* pA,
    _In_ D3D10_STATE_BLOCK_MASK* pB,
    _Out_ D3D10_STATE_BLOCK_MASK* pResult)
{
    return RxCombineMasks(pA, pB, pResult, [](BYTE A, BYTE B) { return (BYTE)(A | B); });
}
