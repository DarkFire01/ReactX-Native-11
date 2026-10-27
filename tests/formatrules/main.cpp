/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Format rules against made up driver answers, such as a driver without typed UAV writes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * formatrules
 */

#include "rxcommon.hpp"

#include <d3d11.h>
#include <d3d10umddi.h>

#include <cstdio>
#include <initializer_list>

#include "formatrules.hpp"

/* Every capability a driver can report for a format */
#define FR_DRIVER_ALL 0x003FFFFFu

/* VirtualBox's VBoxDX reports everything but typed UAV writes */
#define FR_DRIVER_NO_UAV (FR_DRIVER_ALL & ~D3D11_1DDI_FORMAT_SUPPORT_UAV_WRITES)

static UINT FrFailures;

static
UINT
FrSupport(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ RxDdiGeneration Generation,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Driver,
    _In_ BOOLEAN IsSoftware)
{
    const RxFormatRule* Rule = RxFindFormatRule(Level, Generation, Format);

    if (Rule == nullptr)
        return 0;

    return RxApplyFormatRule(Rule, Level, Driver, IsSoftware);
}

static
VOID
FrExpect(
    _In_z_ const char* Name,
    _In_ UINT Support,
    _In_ UINT Set,
    _In_ UINT Clear)
{
    if ((Support & Set) == Set && (Support & Clear) == 0)
    {
        printf("PASS %s\n", Name);
        return;
    }

    printf("FAIL %s: support 0x%08X, wanted 0x%08X set and 0x%08X clear\n", Name, Support, Set, Clear);
    FrFailures++;
}

int
__cdecl
main(VOID)
{
    static const RxDdiGeneration Generations[] = { RxDdiD3D11_1, RxDdiWddm1_3, RxDdiWddm2_0, RxDdiWddm2_3 };
    static const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1 };
    char Name[96];
    UINT Support;

    /* Typed UAVs of these formats are required at feature level 11, whatever the driver says */
    for (D3D_FEATURE_LEVEL Level : Levels)
    {
        for (RxDdiGeneration Generation : Generations)
        {
            for (DXGI_FORMAT Format : { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT })
            {
                sprintf_s(Name, "nouav.level%X.gen%u.format%u", Level, (UINT)Generation, (UINT)Format);
                Support = FrSupport(Level, Generation, Format, FR_DRIVER_NO_UAV, FALSE);
                FrExpect(Name, Support, D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW | D3D11_FORMAT_SUPPORT_TEXTURE2D, 0);
            }
        }
    }

    /* Below feature level 11 there are no typed UAVs at all */
    Support = FrSupport(D3D_FEATURE_LEVEL_10_1, RxDdiWddm2_3, DXGI_FORMAT_R8G8B8A8_UNORM, FR_DRIVER_ALL, FALSE);
    FrExpect("level10_1.rgba8", Support, D3D11_FORMAT_SUPPORT_RENDER_TARGET, D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW);

    /* B8G8R8A8 typed UAVs are the driver's choice from WDDM 2.3 on, and unknown before */
    Support = FrSupport(D3D_FEATURE_LEVEL_11_1, RxDdiWddm2_3, DXGI_FORMAT_B8G8R8A8_UNORM, FR_DRIVER_NO_UAV, FALSE);
    FrExpect("bgra8.wddm2_3.nouav", Support, D3D11_FORMAT_SUPPORT_RENDER_TARGET, D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW);
    Support = FrSupport(D3D_FEATURE_LEVEL_11_1, RxDdiWddm2_3, DXGI_FORMAT_B8G8R8A8_UNORM, FR_DRIVER_ALL, FALSE);
    FrExpect("bgra8.wddm2_3.uav", Support, D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW, 0);
    Support = FrSupport(D3D_FEATURE_LEVEL_11_1, RxDdiWddm1_3, DXGI_FORMAT_B8G8R8A8_UNORM, FR_DRIVER_ALL, FALSE);
    FrExpect("bgra8.wddm1_3.uav", Support, D3D11_FORMAT_SUPPORT_RENDER_TARGET, D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW);

    /* An optional render target the driver turns down takes blending and mip generation with it */
    Support = FrSupport(D3D_FEATURE_LEVEL_11_1, RxDdiWddm2_3, DXGI_FORMAT_R32G32B32_FLOAT,
                        FR_DRIVER_ALL & ~D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET, FALSE);
    FrExpect("rgb32f.nort", Support, D3D11_FORMAT_SUPPORT_SHADER_SAMPLE,
             D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_BLENDABLE | D3D11_FORMAT_SUPPORT_MIP_AUTOGEN);
    Support = FrSupport(D3D_FEATURE_LEVEL_11_1, RxDdiWddm2_3, DXGI_FORMAT_R32G32B32_FLOAT, FR_DRIVER_ALL, FALSE);
    FrExpect("rgb32f.rt", Support, D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_MIP_AUTOGEN, 0);

    /* Required video support is the hardware's; software adapters must report it */
    Support = FrSupport(D3D_FEATURE_LEVEL_11_1, RxDdiWddm2_3, DXGI_FORMAT_NV12, 0, FALSE);
    FrExpect("nv12.hardware", Support, D3D11_FORMAT_SUPPORT_DECODER_OUTPUT | D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT, 0);
    Support = FrSupport(D3D_FEATURE_LEVEL_11_1, RxDdiWddm2_3, DXGI_FORMAT_NV12, 0, TRUE);
    FrExpect("nv12.software", Support, D3D11_FORMAT_SUPPORT_TEXTURE2D, D3D11_FORMAT_SUPPORT_DECODER_OUTPUT);

    /* Formats a level does not have */
    FrExpect("p208.missing", FrSupport(D3D_FEATURE_LEVEL_11_1, RxDdiWddm2_3, DXGI_FORMAT_P208, FR_DRIVER_ALL, FALSE), 0, ~0u);
    FrExpect("bc7.level10", FrSupport(D3D_FEATURE_LEVEL_10_0, RxDdiWddm2_3, DXGI_FORMAT_BC7_UNORM, FR_DRIVER_ALL, FALSE), 0, ~0u);

    printf("%u failures\n", FrFailures);
    return FrFailures != 0 ? 1 : 0;
}
