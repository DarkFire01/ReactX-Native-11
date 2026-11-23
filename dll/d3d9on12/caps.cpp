/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What the adapter reports: capabilities, formats, queries and multisampling
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9on12.hpp"

#include <float.h>

/* The DirectDraw caps a runtime asks for first: Caps, Caps2, CKeyCaps, FXCaps and no Caps3 */
static const DWORD NineDirectDrawCaps[] = { 0x00000005, 0x00000010, 0x00000003, 0x00000003, 0x00000000 };

/* The Direct3D 3 global driver data: the size, the version 1 device description and empty tails */
static const DWORD NineD3D3Caps[] =
{
    0x000000C8,
    0x000000AC, 0x000001E3, 0x00000002, 0x000BBFF1, 0x00000000, 0x00000000, 0x00000001,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000038, 0x00000002, 0x003379B1, 0x000000FF, 0x00001FFF, 0x00001FFF, 0x000000FF,
    0x000C528A, 0x00000CDD, 0x0303073F, 0x000000CF, 0x0000001F, 0x00000000, 0x00000000,
    0x00000038, 0x00000072, 0x003379B1, 0x000000FF, 0x00001FFF, 0x00001FFF, 0x000000FF,
    0x000C528A, 0x00000CDD, 0x0703073F, 0x000000CF, 0x0000001F, 0x00000000, 0x00000000,
    0x00000500, 0x00000700, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* The Direct3D 7 extended caps: texture limits, guard band, stencil, vertex and stage counts */
static const DWORD NineD3D7Caps[] =
{
    0x00000074, 0x00000001, 0x00004000, 0x00000001, 0x00004000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00002000, 0x00002000, 0x00000010, 0xCCBEBC20, 0xCCBEBC20, 0x4CBEBC20, 0x4CBEBC20,
    0x00000000, 0x000000FF, 0x00000008, 0x00FFFFFF, 0x00080008, 0x00000008, 0x501502F9, 0x00040008,
    0x0000003B, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* The query types every adapter answers */
static const D3DDDIQUERYTYPE NineQueryTypes[] =
{
    D3DDDIQUERYTYPE_VCACHE,
    D3DDDIQUERYTYPE_EVENT,
    D3DDDIQUERYTYPE_OCCLUSION,
    D3DDDIQUERYTYPE_TIMESTAMP,
    D3DDDIQUERYTYPE_TIMESTAMPDISJOINT,
    D3DDDIQUERYTYPE_TIMESTAMPFREQ,
};

/* The mode specific DirectDraw caps are all zero */
static constexpr SIZE_T NineModeCapsSize = 28;

/* The quality levels reported for nonmaskable multisampling */
static constexpr UINT NineNonmaskableLevels = 3;

/* How much of the Direct3D 9 caps the Direct3D 8 caps keep */
static constexpr SIZE_T NineD3D8CapsSize = offsetof(D3DCAPS9, PixelShader1xMaxValue) + sizeof(float);

/* The Direct3D 9 caps are the same on every adapter: a shader model 3 part with Direct3D 10 limits */
static
VOID
NineFillD3D9Caps(
    _Out_ D3DCAPS9* Caps)
{
    ZeroMemory(Caps, sizeof(*Caps));
    Caps->DeviceType = D3DDEVTYPE_HAL;
    Caps->Caps = 0x00020100;
    Caps->Caps2 = 0xE0020000;
    Caps->Caps3 = 0x000007A0;
    Caps->PresentationIntervals = 0x8000000F;
    Caps->CursorCaps = D3DCURSORCAPS_COLOR;
    Caps->DevCaps = 0x061BBFF0;
    Caps->PrimitiveMiscCaps = 0x002EEFF2;
    Caps->RasterCaps = 0x0F732191;
    Caps->ZCmpCaps = 0x000000FF;
    Caps->SrcBlendCaps = 0x0000FFFF;
    Caps->DestBlendCaps = 0x0000FFFF;
    Caps->AlphaCmpCaps = 0x000000FF;
    Caps->ShadeCaps = 0x00084208;
    Caps->TextureCaps = 0x0001ECDD;
    Caps->TextureFilterCaps = 0x07030700;
    Caps->CubeTextureFilterCaps = 0x07030700;
    Caps->VolumeTextureFilterCaps = 0x07030700;
    Caps->TextureAddressCaps = 0x0000003F;
    Caps->VolumeTextureAddressCaps = 0x0000003F;
    Caps->LineCaps = 0x0000001F;
    Caps->MaxTextureWidth = 16384;
    Caps->MaxTextureHeight = 16384;
    Caps->MaxVolumeExtent = 16384;
    Caps->MaxTextureRepeat = 8192;
    Caps->MaxTextureAspectRatio = 8192;
    Caps->MaxAnisotropy = 16;
    Caps->MaxVertexW = 1.0e10f;
    Caps->GuardBandLeft = -1.0e8f;
    Caps->GuardBandTop = -1.0e8f;
    Caps->GuardBandRight = 1.0e8f;
    Caps->GuardBandBottom = 1.0e8f;
    Caps->StencilCaps = 0x000001FF;
    Caps->FVFCaps = 0x00100008;
    Caps->TextureOpCaps = 0x03FFFFFF;
    Caps->MaxTextureBlendStages = 8;
    Caps->MaxSimultaneousTextures = 8;
    Caps->VertexProcessingCaps = 0x0000017B;
    Caps->MaxActiveLights = 8;
    Caps->MaxUserClipPlanes = 8;
    Caps->MaxVertexBlendMatrices = 4;
    Caps->MaxPointSize = 8192.0f;
    Caps->MaxPrimitiveCount = 0x007FFFFF;
    Caps->MaxVertexIndex = 0x00FFFFFF;
    Caps->MaxStreams = 16;
    Caps->MaxStreamStride = 255;
    Caps->VertexShaderVersion = D3DVS_VERSION(3, 0);
    Caps->MaxVertexShaderConst = 256;
    Caps->PixelShaderVersion = D3DPS_VERSION(3, 0);
    Caps->PixelShader1xMaxValue = FLT_MAX;
    Caps->DevCaps2 = 0x00000041;
    Caps->MaxNpatchTessellationLevel = 1.0f;
    Caps->DeclTypes = 0x000003FF;
    Caps->NumSimultaneousRTs = 4;
    Caps->StretchRectFilterCaps = 0x03000300;
    Caps->VS20Caps.Caps = D3DVS20CAPS_PREDICATION;
    Caps->VS20Caps.DynamicFlowControlDepth = 24;
    Caps->VS20Caps.NumTemps = 32;
    Caps->VS20Caps.StaticFlowControlDepth = 4;
    Caps->PS20Caps.Caps = 0x0000001F;
    Caps->PS20Caps.DynamicFlowControlDepth = 24;
    Caps->PS20Caps.NumTemps = 32;
    Caps->PS20Caps.StaticFlowControlDepth = 4;
    Caps->PS20Caps.NumInstructionSlots = 512;
    Caps->VertexTextureFilterCaps = 0x03030300;
    Caps->MaxVShaderInstructionsExecuted = 0xFFFFFFFF;
    Caps->MaxPShaderInstructionsExecuted = 0xFFFFFFFF;
    Caps->MaxVertexShader30InstructionSlots = 32768;
    Caps->MaxPixelShader30InstructionSlots = 32768;
}

/* The Direct3D 8 caps: the Direct3D 9 caps up to the pixel shader range, narrowed to what version 8 defines */
static
VOID
NineFillD3D8Caps(
    _Out_writes_bytes_all_(NineD3D8CapsSize) VOID* Data)
{
    D3DCAPS9 Caps;

    NineFillD3D9Caps(&Caps);
    Caps.Caps = 0x94C203C1;
    Caps.Caps2 = 0x606A9E01;
    Caps.Caps3 = 0x000000A0;
    Caps.PresentationIntervals &= 0x8000000F;
    Caps.CursorCaps &= 0x00000003;
    Caps.DevCaps &= 0x0FFBFFF0;
    Caps.PrimitiveMiscCaps &= 0x00003FF6;
    Caps.RasterCaps &= 0x00F7F19F;
    Caps.ZCmpCaps &= 0x000000FF;
    Caps.SrcBlendCaps &= 0x00001FFF;
    Caps.DestBlendCaps &= 0x00001FFF;
    Caps.AlphaCmpCaps &= 0x000000FF;
    Caps.ShadeCaps &= 0x00084208;
    Caps.TextureCaps &= 0x0007EDE7;
    Caps.TextureFilterCaps &= 0x1F030700;
    Caps.CubeTextureFilterCaps &= 0x1F030700;
    Caps.VolumeTextureFilterCaps &= 0x1F030700;
    Caps.TextureAddressCaps &= 0x0000003F;
    Caps.VolumeTextureAddressCaps &= 0x0000003F;
    Caps.LineCaps &= 0x0000001F;
    Caps.StencilCaps &= 0x000000FF;
    Caps.FVFCaps &= 0x0018FFFF;
    Caps.TextureOpCaps &= 0x03FFFFFF;
    Caps.VertexProcessingCaps &= 0x000000FB;
    Caps.VertexShaderVersion = D3DVS_VERSION(1, 1);
    Caps.PixelShaderVersion = D3DPS_VERSION(1, 4);
    CopyMemory(Data, &Caps, NineD3D8CapsSize);
}

/* Quality levels Direct3D 12 offers for a format and sample count */
static
UINT
NineQualityLevels(
    _In_ const NineAdapter* Adapter,
    _In_ const DDIMULTISAMPLEQUALITYLEVELSDATA* Query)
{
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS Levels = {};

    if (Query->MsType == D3DDDIMULTISAMPLE_NONE)
        return 0;
    if (Query->MsType == D3DDDIMULTISAMPLE_NONMASKABLE)
        return NineNonmaskableLevels;

    Levels.Format = NineDxgiFormat(Query->Format);
    Levels.SampleCount = Query->MsType;
    Levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    Levels.NumQualityLevels = 1;
    Adapter->Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &Levels, sizeof(Levels));
    return Levels.NumQualityLevels;
}

HRESULT
APIENTRY
NineGetCaps(
    _In_ HANDLE hAdapter,
    _Inout_ const D3DDDIARG_GETCAPS* Args)
{
    const NineAdapter* Adapter = static_cast<const NineAdapter*>(hAdapter);
    HRESULT hr = S_OK;

    if (Adapter == nullptr)
        return E_INVALIDARG;

    switch (Args->Type)
    {
        case D3DDDICAPS_DDRAW:
            CopyMemory(Args->pData, NineDirectDrawCaps, sizeof(NineDirectDrawCaps));
            break;

        case D3DDDICAPS_DDRAW_MODE_SPECIFIC:
            ZeroMemory(Args->pData, NineModeCapsSize);
            break;

        case D3DDDICAPS_GETFORMATCOUNT:
            *static_cast<UINT*>(Args->pData) = NineCollectFormatOps(Adapter, nullptr, 0, &hr);
            break;

        case D3DDDICAPS_GETFORMATDATA:
            NineCollectFormatOps(Adapter, static_cast<FORMATOP*>(Args->pData), Args->DataSize / sizeof(FORMATOP),
                                 &hr);
            break;

        case D3DDDICAPS_GETMULTISAMPLEQUALITYLEVELS:
        {
            DDIMULTISAMPLEQUALITYLEVELSDATA* Query = static_cast<DDIMULTISAMPLEQUALITYLEVELSDATA*>(Args->pData);

            Query->QualityLevels = NineQualityLevels(Adapter, Query);
            break;
        }

        case D3DDDICAPS_GETD3DQUERYCOUNT:
            *static_cast<UINT*>(Args->pData) = _countof(NineQueryTypes);
            break;

        case D3DDDICAPS_GETD3DQUERYDATA:
            if (Args->pData != nullptr)
            {
                SIZE_T Count = Args->DataSize / sizeof(D3DDDIQUERYTYPE);

                if (Count > _countof(NineQueryTypes))
                    Count = _countof(NineQueryTypes);
                CopyMemory(Args->pData, NineQueryTypes, Count * sizeof(D3DDDIQUERYTYPE));
            }
            break;

        case D3DDDICAPS_GETD3D3CAPS:
            CopyMemory(Args->pData, NineD3D3Caps, sizeof(NineD3D3Caps));
            break;

        case D3DDDICAPS_GETD3D7CAPS:
            CopyMemory(Args->pData, NineD3D7Caps, sizeof(NineD3D7Caps));
            break;

        case D3DDDICAPS_GETD3D8CAPS:
            NineFillD3D8Caps(Args->pData);
            break;

        case D3DDDICAPS_GETD3D9CAPS:
            NineFillD3D9Caps(static_cast<D3DCAPS9*>(Args->pData));
            break;

        case D3DDDICAPS_GETGAMMARAMPCAPS:
            *static_cast<DWORD*>(Args->pData) = 1;
            break;

        case D3DDDICAPS_GETCONTENTPROTECTIONCAPS:
        case D3DDDICAPS_GET_ARCHITECTURE_INFO:
            *static_cast<DWORD*>(Args->pData) = 0;
            break;

        default:
            if ((Args->Type >= D3DDDICAPS_GETDECODEGUIDCOUNT && Args->Type <= D3DDDICAPS_GETEXTENSIONCAPS) ||
                (Args->Type >= D3DDDICAPS_DXVAHD_GETVPDEVCAPS && Args->Type <= D3DDDICAPS_DXVAHD_GETVPFILTERRANGE))
            {
                NINE_TRACE_ONCE(L"video caps are not implemented yet (type %u)", Args->Type);
                return E_NOTIMPL;
            }

            NineTrace(L"GetCaps type %u is unknown", Args->Type);
            return E_FAIL;
    }

    return NineDxgiResult(hr);
}
