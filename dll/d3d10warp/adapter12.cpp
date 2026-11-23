/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 adapter: caps, DDI tables and device creation
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device12.hpp"
#include <initializer_list>

/* Interface and version as one number, the way the supported version list orders them */
#define RX_WARP12_LEVEL(Minor, Build) ((0xC0000ull + (Minor)) << 32 | (UINT64)(Build) << 16)

/* The interface the adapter reports until a device is created */
static constexpr UINT RxWarp12OpenInterface = 0xC0002;

/* Kernel adapter flags */
static constexpr UINT RxWarpKernelSoftwareOnly = 0x4;
static constexpr UINT RxWarpKernelDisplay = 0x8;
static constexpr UINT RxWarpKernelComputeOnly = 0x40;
static constexpr UINT RxWarpKernelNativeFence = 0x80;

/* Video and content protection are not offered, and DXIL is release only */
static constexpr BOOLEAN RxWarp12Video = FALSE;
static constexpr BOOLEAN RxWarp12ContentProtection = FALSE;
static constexpr BOOLEAN RxWarp12ExperimentalDxil = FALSE;

/* Pipeline levels the texture layout caps answer per layout */
static const BYTE RxWarp12LayoutLevels[5] = { 0, 1, 1, 2, 2 };

/* Texels taken from the high bits of the swizzle pattern, per layout */
static const BYTE RxWarp12SwizzleHighBits[3] = { 8, 7, 6 };

/* The software content protection system and the decode profiles it pairs with */
static const GUID RxWarp12ProtectionSystem =
    { 0xF4637010, 0x03C3, 0x42CD, { 0xB9, 0x32, 0xB4, 0x8A, 0xDF, 0x3A, 0x6A, 0x54 } };

static const GUID RxWarp12ProtectedProfiles[4] =
{
    { 0x1B81BE68, 0xA0C7, 0x11D3, { 0xB9, 0x84, 0x00, 0xC0, 0x4F, 0x2E, 0x73, 0xC5 } },
    { 0xD79BE8DA, 0x0CF1, 0x4C81, { 0xB8, 0x2A, 0x69, 0xA4, 0xE2, 0x36, 0xF4, 0x3D } },
    { 0xF9AACCBB, 0xC2B6, 0x4CFC, { 0x87, 0x79, 0x57, 0x07, 0xB1, 0x76, 0x05, 0x52 } },
    { 0x705B9D82, 0x76CF, 0x49D6, { 0xB7, 0xE6, 0xAC, 0x88, 0x72, 0xDB, 0x01, 0x3C } },
};

/* The one meta command whose caps can be queried */
static const GUID RxWarp12MetaCommand =
    { 0x936F7F01, 0x203F, 0x44D3, { 0x9E, 0x18, 0x41, 0x98, 0xD2, 0x43, 0xB4, 0xEA } };

static const GUID RxWarp12HardwareProtected =
    { 0x62B0084E, 0xC70E, 0x4DAA, { 0xA1, 0x09, 0x30, 0xFF, 0x8D, 0x5A, 0x04, 0x82 } };

/* Shader models from 5.1 up to 6.8 */
static const UINT RxWarp12ShaderModels[10] =
{
    0x50015, 0x60005, 0x60015, 0x60025, 0x60035, 0x60045, 0x60055, 0x60065, 0x60075, 0x60085,
};

static const UINT RxWarp12ExperimentalShaderModels[10] =
{
    0x50015, 0x60000, 0x60010, 0x60020, 0x60030, 0x60040, 0x60050, 0x60060, 0x60070, 0x60080,
};

/* Device table sizes by the first interface level that uses them */
typedef struct _RX_WARP12_TABLE_STEP
{
    UINT64 Level;
    SIZE_T Size;
} RX_WARP12_TABLE_STEP;

static const RX_WARP12_TABLE_STEP RxWarp12DeviceSteps[] =
{
    { RX_WARP12_LEVEL(80, 116), 1024 }, { RX_WARP12_LEVEL(80, 113), 1024 }, { RX_WARP12_LEVEL(80, 112), 1000 },
    { RX_WARP12_LEVEL(80, 111), 1000 }, { RX_WARP12_LEVEL(80, 109), 992 }, { RX_WARP12_LEVEL(80, 108), 992 },
    { RX_WARP12_LEVEL(80, 102), 976 }, { RX_WARP12_LEVEL(80, 100), 976 }, { RX_WARP12_LEVEL(80, 99), 976 },
    { RX_WARP12_LEVEL(80, 95), 976 }, { RX_WARP12_LEVEL(80, 0), 976 }, { RX_WARP12_LEVEL(70, 5), 968 },
    { RX_WARP12_LEVEL(70, 4), 968 }, { RX_WARP12_LEVEL(70, 3), 944 }, { RX_WARP12_LEVEL(70, 2), 936 },
    { RX_WARP12_LEVEL(60, 3), 920 }, { RX_WARP12_LEVEL(60, 2), 920 }, { RX_WARP12_LEVEL(50, 4), 912 },
    { RX_WARP12_LEVEL(50, 2), 840 }, { RX_WARP12_LEVEL(50, 0), 792 }, { RX_WARP12_LEVEL(40, 3), 768 },
    { RX_WARP12_LEVEL(40, 0), 768 }, { RX_WARP12_LEVEL(30, 3), 736 }, { RX_WARP12_LEVEL(30, 0), 736 },
    { RX_WARP12_LEVEL(20, 6), 736 }, { RX_WARP12_LEVEL(20, 5), 736 }, { RX_WARP12_LEVEL(20, 3), 736 },
    { RX_WARP12_LEVEL(20, 2), 736 }, { RX_WARP12_LEVEL(20, 1), 736 }, { RX_WARP12_LEVEL(10, 4), 736 },
};

/* Command list table sizes; anything older than the last step still gets the first release table */
static const RX_WARP12_TABLE_STEP RxWarp12ListSteps[] =
{
    { RX_WARP12_LEVEL(80, 114), 600 }, { RX_WARP12_LEVEL(80, 108), 600 }, { RX_WARP12_LEVEL(80, 99), 584 },
    { RX_WARP12_LEVEL(80, 95), 568 }, { RX_WARP12_LEVEL(80, 94), 560 }, { RX_WARP12_LEVEL(80, 92), 560 },
    { RX_WARP12_LEVEL(80, 8), 552 }, { RX_WARP12_LEVEL(70, 4), 544 }, { RX_WARP12_LEVEL(60, 2), 536 },
    { RX_WARP12_LEVEL(50, 4), 520 }, { RX_WARP12_LEVEL(50, 2), 480 }, { RX_WARP12_LEVEL(50, 1), 464 },
    { RX_WARP12_LEVEL(40, 0), 464 }, { RX_WARP12_LEVEL(30, 3), 464 }, { RX_WARP12_LEVEL(30, 2), 456 },
    { RX_WARP12_LEVEL(30, 0), 448 }, { RX_WARP12_LEVEL(20, 8), 440 }, { RX_WARP12_LEVEL(20, 7), 440 },
    { RX_WARP12_LEVEL(20, 5), 424 }, { RX_WARP12_LEVEL(20, 2), 416 }, { 0, 408 },
};

static
UINT64
RxWarp12Level(
    _In_ const RxWarpAdapter *Adapter)
{
    return (UINT64)Adapter->Interface << 32 | Adapter->Version;
}

static
SIZE_T
RxWarp12TableSize(
    _In_reads_(Count) const RX_WARP12_TABLE_STEP *Steps,
    _In_ UINT Count,
    _In_ UINT64 Level)
{
    for (UINT Index = 0; Index < Count; Index++)
    {
        if (Level >= Steps[Index].Level)
            return Steps[Index].Size;
    }
    return 0;
}

static
VOID
RxWarp12Write(
    _Out_writes_bytes_(sizeof(UINT)) VOID *Data,
    _In_ SIZE_T Offset,
    _In_ UINT Value)
{
    memcpy(static_cast<BYTE *>(Data) + Offset, &Value, sizeof(Value));
}

/* Writes consecutive UINTs starting at Offset */
static
VOID
RxWarp12WriteList(
    _Out_ VOID *Data,
    _In_ SIZE_T Offset,
    _In_ std::initializer_list<UINT> Values)
{
    for (UINT Value : Values)
    {
        RxWarp12Write(Data, Offset, Value);
        Offset += sizeof(Value);
    }
}

/* Index of the first protected profile that matches, or the count when none does */
static
UINT
RxWarp12FindProtectedProfile(
    _In_ const GUID *Profile,
    _In_ const GUID *System)
{
    UINT Index;

    for (Index = 0; Index < ARRAYSIZE(RxWarp12ProtectedProfiles); Index++)
    {
        if (memcmp(&RxWarp12ProtectionSystem, Profile, sizeof(*Profile)) == 0 &&
            memcmp(&RxWarp12ProtectedProfiles[Index], System, sizeof(*System)) == 0)
            break;
    }
    return Index;
}

/* The swizzle pattern: low bits first, then the high bits for the layout */
static
VOID
RxWarp12WriteSwizzle(
    _Out_writes_bytes_(16) BYTE *Pattern,
    _In_ UINT Layout)
{
    UINT High = RxWarp12SwizzleHighBits[Layout];
    UINT Position = 0;

    for (UINT Bit = 0; Bit < 16 - High; Bit++)
        Pattern[Position++] = (BYTE)((Bit << 3) | 1);
    for (UINT Bit = 0; Bit < High; Bit++)
        Pattern[Position++] = (BYTE)((Bit << 3) | 3);
}

/* The base Direct3D 12 options block: its layout grew with every interface */
static
HRESULT
RxWarp12Options(
    _In_ UINT64 Level,
    _Out_writes_bytes_(Size) VOID *Data,
    _In_ UINT Size)
{
    UINT Video = RxWarp12Video ? 55 : 7;

    if (Level >= RX_WARP12_LEVEL(70, 3))
    {
        if (Size < 100)
            return E_UNEXPECTED;
        RxWarp12WriteList(Data, 0, { 3, 3, Level >= RX_WARP12_LEVEL(80, 117) ? 4u : 3u, 0, 1, 1, 2, 1, 2, 1, Video,
                                     1, 1, 1, 1, 1, 0, 12, 2, 1, 1, 8, 0, 10, 100 });
        if (Level >= RX_WARP12_LEVEL(80, 0))
            RxWarp12Write(Data, 100, 0);
        if (Level >= RX_WARP12_LEVEL(80, 1))
            RxWarp12Write(Data, 104, 1);
        if (Level >= RX_WARP12_LEVEL(80, 3))
            RxWarp12WriteList(Data, 108, { 1, 1 });
        if (Level >= RX_WARP12_LEVEL(80, 6))
            RxWarp12WriteList(Data, 108, { 1, 1, 0 });
        if (Level >= RX_WARP12_LEVEL(80, 9))
            RxWarp12Write(Data, 120, 1);
        return S_OK;
    }

    if (Level >= RX_WARP12_LEVEL(60, 2))
    {
        if (Size != 92)
            return E_UNEXPECTED;
        RxWarp12WriteList(Data, 0, { 3, 3, 3, 0, 1, 1, 2, 1, 2, 1, Video, 1, 1, 1, 1, 1, 1, 12, 2, 1, 1, 8, 0 });
        return S_OK;
    }

    if (Level >= RX_WARP12_LEVEL(50, 2))
    {
        static const UINT Sizes[3] = { 64, 68, 72 };
        UINT Step = Level >= RX_WARP12_LEVEL(50, 4) ? 2 : (Level >= RX_WARP12_LEVEL(50, 3) ? 1 : 0);

        if (Size != Sizes[Step])
            return E_UNEXPECTED;
        RxWarp12WriteList(Data, 0, { 3, 3, 3, 0, 1, 1, 2, 1, 2, 1, Video, 1, 1, 1, 1, 1 });
        if (Step > 0)
            RxWarp12Write(Data, 64, 1);
        if (Step > 1)
            RxWarp12Write(Data, 68, 12);
        return S_OK;
    }

    if (Level >= RX_WARP12_LEVEL(40, 1))
    {
        if (Size != 60)
            return E_UNEXPECTED;
        RxWarp12WriteList(Data, 0, { 3, 3, 3, 0, 1, 1, 2, 1, 2, 1, Video, 1, 1, 1, 1 });
        return S_OK;
    }

    if (Level >= RX_WARP12_LEVEL(30, 3))
    {
        if (Size != 52)
            return E_UNEXPECTED;
        RxWarp12WriteList(Data, 0, { 3, 3, 3, 0, 1, 1, 2, 1, 2, 1, Video, 1, 1 });
        return S_OK;
    }

    if (Level >= RX_WARP12_LEVEL(30, 2))
    {
        if (Size != 44)
            return E_UNEXPECTED;
        RxWarp12WriteList(Data, 0, { 3, 3, 3, 0, 1, 1, 2, 1, 2, 1, 7 });
        return S_OK;
    }

    if (Level >= RX_WARP12_LEVEL(30, 1))
    {
        if (Size != 40)
            return E_UNEXPECTED;
        RxWarp12WriteList(Data, 0, { 3, 3, 3, 0, 1, 1, 2, 1, 2, 1 });
        return S_OK;
    }

    if (Level >= RX_WARP12_LEVEL(20, 7))
    {
        if (Size != 36)
            return E_UNEXPECTED;
        RxWarp12WriteList(Data, 0, { 3, 3, 3, 0, 1, 1, 2, 1, 2 });
        return S_OK;
    }

    if (Level >= RX_WARP12_LEVEL(20, 5))
    {
        if (Size != 32)
            return E_UNEXPECTED;
        RxWarp12WriteList(Data, 0, { 3, 3, 3, 0, 1, 1, 2, 1 });
        return S_OK;
    }

    if (Size != 28)
        return E_UNEXPECTED;
    RxWarp12WriteList(Data, 0, { 3, 3, 3, 0, 1, 1, 2 });
    return S_OK;
}

/* Shader caps: the wave lane count follows the processor count */
static
HRESULT
RxWarp12ShaderCaps(
    _In_ const RxWarpAdapter *Adapter,
    _Out_writes_bytes_(Size) VOID *Data,
    _In_ UINT Size)
{
    UINT Lanes = Adapter->ProcessorCount * 4 > 128 ? Adapter->ProcessorCount * 4 : 128;

    if (Size != 20 && Size != 36 && Size != 40 && Size != 44 && Size != 60 && Size != 64)
        return E_INVALIDARG;

    if (Size >= 64)
        RxWarp12Write(Data, 60, 1);
    if (Size >= 60)
        RxWarp12WriteList(Data, 44, { 1, 1, 1, 0 });
    if (Size >= 44)
        RxWarp12Write(Data, 40, 1);
    if (Size >= 40)
        RxWarp12Write(Data, 36, 1);
    if (Size >= 36)
        RxWarp12WriteList(Data, 20, { 1, 4, 128, Lanes });
    RxWarp12WriteList(Data, 0, { 3, 1, 1, 1, 1 });
    return S_OK;
}

static
HRESULT
APIENTRY
RxWarp12GetCaps(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ const D3D12DDIARG_GETCAPS *Args)
{
    RxWarpLock Lock;
    RxWarpAdapter *This = static_cast<RxWarpAdapter *>(Adapter.pDrvPrivate);
    UINT64 Level = RxWarp12Level(This);
    BYTE *Data = static_cast<BYTE *>(Args->pData);
    const UINT *Info = static_cast<const UINT *>(Args->pInfo);
    UINT Size = Args->DataSize;
    BOOLEAN ComputeOnlyDisplay = (This->Kernel.Flags & RxWarpKernelDisplay) && (This->Kernel.Flags & RxWarpKernelComputeOnly);

    switch ((UINT)Args->Type)
    {
        case D3D12DDICAPS_TYPE_TEXTURE_LAYOUT:
            if (Info != nullptr)
            {
                UINT Level0;

                if (Size != 8 || *Info >= ARRAYSIZE(RxWarp12LayoutLevels))
                    return E_UNEXPECTED;
                Level0 = RxWarp12LayoutLevels[*Info] | 0x100;
                RxWarp12WriteList(Data, 0, { Level0, Level0 });
                return S_OK;
            }
            if (Size == 16)
            {
                RxWarp12WriteList(Data, 0, { 5, 3, 1, 1 });
                return S_OK;
            }
            if (Size != 12)
                return E_UNEXPECTED;
            RxWarp12WriteList(Data, 0, { 5, 3, 1 });
            return S_OK;

        case D3D12DDICAPS_TYPE_SWIZZLE_PATTERN:
            if (Size != 100 && Size != 104)
                return E_UNEXPECTED;
            memset(Data, 0, Size);
            if (*Info >= ARRAYSIZE(RxWarp12SwizzleHighBits))
                return E_UNEXPECTED;
            RxWarp12WriteSwizzle(Data, *Info);
            return S_OK;

        case D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE:
            if (Level < RX_WARP12_LEVEL(40, 1))
            {
                if (Size != 12)
                    return E_UNEXPECTED;
            }
            else
            {
                if (Size != 20)
                    return E_UNEXPECTED;
                RxWarp12WriteList(Data, 12, { 1, 1 });
            }
            RxWarp12WriteList(Data, 0, { 1, 1, 1 });
            return S_OK;

        case D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS:
            if (Info == nullptr || Info[0] != 1 || Info[1] != 0 || Size != 20)
                return E_UNEXPECTED;
            RxWarp12WriteList(Data, 0, { 0x100000 | 0xFFFF, 0x100010, 0xFFFF, 0, 1 });
            return S_OK;

        case D3D12DDICAPS_TYPE_SHADER:
            return RxWarp12ShaderCaps(This, Data, Size);

        case D3D12DDICAPS_TYPE_ARCHITECTURE_INFO:
        case D3D12DDICAPS_TYPE_0023_UMD_BASED_COMMAND_QUEUE_PRIORITY:
        case D3D12DDICAPS_TYPE_0050_HARDWARE_SCHEDULING_CAPS:
        case D3D12DDICAPS_TYPE_GUID_TEXTURE_LAYOUT_TIER_0111:
        case D3D12DDI_APPLICATION_SPECIFIC_DRIVER_STATE_113:
        case D3D12DDICAPS_TYPE_OPTIONS_0118:
            if (Size != 4)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 0, 0);
            return S_OK;

        case D3D12DDICAPS_TYPE_D3D12_OPTIONS:
            return RxWarp12Options(Level, Data, Size);

        case D3D12DDICAPS_TYPE_3DPIPELINESUPPORT:
            if (Size != 4)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 0, ComputeOnlyDisplay ? 2 : 13);
            return S_OK;

        case D3D12DDICAPS_TYPE_GPUVA_CAPS:
            if (Size != 4)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 0, 40);
            return S_OK;

        case D3D12DDICAPS_TYPE_0011_SHADER_MODELS:
        {
            D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 *Models = reinterpret_cast<D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 *>(Data);

            if (Size != sizeof(*Models))
                return E_UNEXPECTED;
            if (Models->pShaderModelsSupported != nullptr && Models->pNumShaderModelsSupported != nullptr &&
                *Models->pNumShaderModelsSupported >= ARRAYSIZE(RxWarp12ShaderModels))
            {
                memcpy(Models->pShaderModelsSupported,
                       RxWarp12ExperimentalDxil ? RxWarp12ExperimentalShaderModels : RxWarp12ShaderModels,
                       sizeof(RxWarp12ShaderModels));
            }
            if (Models->pNumShaderModelsSupported != nullptr)
                *Models->pNumShaderModelsSupported = ARRAYSIZE(RxWarp12ShaderModels);
            return S_OK;
        }

        case D3D12DDICAPS_TYPE_OPTIONS1_0103:
            if (Size != 4)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 0, 10);
            return S_OK;

        case D3D12DDICAPS_TYPE_0030_PROTECTED_RESOURCE_SESSION_SUPPORT:
        case D3D12DDICAPS_TYPE_0030_CONTENT_PROTECTION_SYSTEM_COUNT:
        case D3D12DDICAPS_TYPE_0074_PROTECTED_RESOURCE_SESSION_TYPE_COUNT:
            if (Size != 8)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 4, 1);
            return S_OK;

        case D3D12DDICAPS_TYPE_0030_CRYPTO_SESSION_SUPPORT:
        {
            D3D12DDI_VIDEO_CRYPTO_SESSION_SUPPORT_DATA_0030 *Session = reinterpret_cast<D3D12DDI_VIDEO_CRYPTO_SESSION_SUPPORT_DATA_0030 *>(Data);

            if (Size != sizeof(*Session))
                return E_UNEXPECTED;
            if (RxWarp12FindProtectedProfile(&Session->DecodeProfile, &Session->ContentProtectionSystem) == 1)
            {
                Session->Support = (D3D12DDI_CRYPTO_SESSION_SUPPORT_FLAGS_0030)0;
                return S_OK;
            }
            Session->KeyBaseDataSize = 8;
            Session->Support = (D3D12DDI_CRYPTO_SESSION_SUPPORT_FLAGS_0030)1;
            return S_OK;
        }

        case D3D12DDICAPS_TYPE_0022_CPU_PAGE_TABLE_FALSE_POSITIVES:
            if (Size != 4)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 0, 23);
            return S_OK;

        case D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT:
            if (Info != nullptr)
            {
                UINT Level0;

                if (Size != 72 || *Info >= ARRAYSIZE(RxWarp12LayoutLevels))
                    return E_UNEXPECTED;
                Level0 = RxWarp12LayoutLevels[*Info] | 0x100;
                for (UINT Offset = 0; Offset < 72; Offset += sizeof(UINT))
                    RxWarp12Write(Data, Offset, Level0);
                return S_OK;
            }
            if (Size == 20)
            {
                RxWarp12WriteList(Data, 0, { 5, 3, 1, 1, 0 });
                return S_OK;
            }
            if (Size != 16)
                return E_UNEXPECTED;
            RxWarp12WriteList(Data, 0, { 5, 3, 1, 0 });
            return S_OK;

        case D3D12DDICAPS_TYPE_0022_SWIZZLE_PATTERN:
            if (Size != 232)
                return E_UNEXPECTED;
            memset(Data, 0, Size);
            if (*Info >= ARRAYSIZE(RxWarp12SwizzleHighBits))
                return E_UNEXPECTED;
            RxWarp12WriteSwizzle(Data, *Info);
            return S_OK;

        case D3D12DDICAPS_TYPE_0030_CONTENT_PROTECTION_SYSTEM_SUPPORT:
        {
            D3D12DDI_VIDEO_CONTENT_PROTECTION_SYSTEM_SUPPORT_DATA_0030 *Systems = reinterpret_cast<D3D12DDI_VIDEO_CONTENT_PROTECTION_SYSTEM_SUPPORT_DATA_0030 *>(Data);

            if (Size != sizeof(*Systems) || Systems->ContentProtectionSystemCount != 1)
                return E_UNEXPECTED;
            *Systems->pContentProtectionSystems = RxWarp12ProtectionSystem;
            return S_OK;
        }

        case D3D12DDICAPS_TYPE_0030_CRYPTO_SESSION_TRANSFORM_SUPPORT:
        {
            D3D12DDI_VIDEO_CRYPTO_SESSION_TRANSFORM_SUPPORT_DATA_0030 *Transform = reinterpret_cast<D3D12DDI_VIDEO_CRYPTO_SESSION_TRANSFORM_SUPPORT_DATA_0030 *>(Data);

            if (Size != sizeof(*Transform))
                return E_UNEXPECTED;
            Transform->ProtectedOutputRequired = FALSE;
            Transform->InputAlignment = 0;
            Transform->InputPreambleSize = 0;
            Transform->OutputAlignment = 0;
            Transform->OutputPreambleSize = 0;
            Transform->Support = (D3D12DDI_CRYPTO_SESSION_TRANSFORM_SUPPORT_FLAGS_0030)
                (RxWarp12FindProtectedProfile(&Transform->DecodeProfile, &Transform->ContentProtectionSystem) != 1);
            return S_OK;
        }

        case D3D12DDICAPS_TYPE_QUERY_META_COMMAND_CAPS_0061:
        {
            D3D12DDICAPS_QUERY_META_COMMAND_CAPS_0061 *Query = reinterpret_cast<D3D12DDICAPS_QUERY_META_COMMAND_CAPS_0061 *>(Data);
            UINT Stage;

            if (Size != sizeof(*Query))
                return E_UNEXPECTED;
            if (memcmp(&Query->CommandId, &RxWarp12MetaCommand, sizeof(Query->CommandId)) != 0)
                return E_INVALIDARG;

            Stage = *static_cast<const UINT *>(Query->pQueryInputData);
            if (Stage == 0 || Stage == 3 || Stage == 4)
                RxWarp12Write(Query->pQueryOutputData, 0, 0);
            else if (Stage == 6)
                memset(Query->pQueryOutputData, 0, 16);
            return S_OK;
        }

        case D3D12DDICAPS_TYPE_0073_SUPPORT_BATCHED_MARKERS:
            if (Size != 4)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 0, 1);
            This->BatchedMarkers = TRUE;
            return S_OK;

        case D3D12DDICAPS_TYPE_0074_PROTECTED_RESOURCE_SESSION_TYPES:
        {
            const UINT *Count = reinterpret_cast<const UINT *>(Data + 4);
            GUID **Types = reinterpret_cast<GUID **>(Data + 8);

            if (Size != 16 || *Count != 1)
                return E_UNEXPECTED;
            **Types = RxWarp12HardwareProtected;
            return S_OK;
        }

        case D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1:
        {
            UINT Requested;
            UINT Supported;

            if (Size != 8)
                return E_UNEXPECTED;
            memcpy(&Requested, Data, sizeof(Requested));
            if (This->Kernel.Flags & RxWarpKernelSoftwareOnly)
                Supported = 13;
            else
                Supported = Requested < 14 ? 14 : Requested;
            if (ComputeOnlyDisplay)
                Supported = 2;
            RxWarp12Write(Data, 4, Supported);
            return S_OK;
        }

        case D3D12DDICAPS_TYPE_0103_WAVE_MMA:
            if (Size != 32)
                return E_UNEXPECTED;
            RxWarp12WriteList(Data, 12, { 0, 0, 0, 0, 0 });
            return S_OK;

        case D3D12DDICAPS_TYPE_OPTIONS_0090:
        case D3D12DDICAPS_TYPE_OPTIONS_0109:
        case D3D12DDICAPS_TYPE_TIGHT_ALIGNMENT_TIER_0111:
            if (Size != 4)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 0, 1);
            return S_OK;

        case D3D12DDICAPS_TYPE_OPTIONS_0091:
            if (Size != 16)
                return E_UNEXPECTED;
            RxWarp12WriteList(Data, 0, { 1, 1, 1, 0 });
            return S_OK;

        case D3D12DDICAPS_TYPE_OPTIONS_0093:
        case D3D12DDICAPS_TYPE_SHADER_MODEL_6_8_OPTIONS_0110:
            if (Size != 8)
                return E_UNEXPECTED;
            RxWarp12WriteList(Data, 0, { 1, 1 });
            return S_OK;

        case D3D12DDICAPS_TYPE_OPTIONS_0102:
            if (Size != 16)
                return E_UNEXPECTED;
            RxWarp12WriteList(Data, 0, { 0x1F, 0x200000, 0x200000, 0x200000 });
            return S_OK;

        case D3D12DDICAPS_TYPE_OPTIONS_0110:
            if (Size != 4)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 0, 11);
            return S_OK;

        case D3D12DDICAPS_TYPE_GUID_TEXTURE_LAYOUT_GUID_SUPPORT_0111:
            if (Size != 16)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 12, 0);
            return S_OK;

        case D3D12DDICAPS_TYPE_0112_NATIVE_FENCE_SUPPORT:
            if (Size != 4)
                return E_UNEXPECTED;
            RxWarp12Write(Data, 0, (This->Kernel.Flags & RxWarpKernelNativeFence) != 0);
            return S_OK;

        default:
            return E_UNEXPECTED;
    }
}

static
SIZE_T
APIENTRY
RxWarp12CalcPrivateDeviceSize(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ const D3D12DDIARG_CALCPRIVATEDEVICESIZE *Args)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Args);
    return 4000;
}

static
HRESULT
APIENTRY
RxWarp12CreateDevice(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ const D3D12DDIARG_CREATEDEVICE_0003 *Args)
{
    RxWarpLock Lock;
    RxWarpAdapter *This = static_cast<RxWarpAdapter *>(Adapter.pDrvPrivate);

    /* Caps answer for the interface the runtime settled on from here on */
    This->Interface = Args->Interface;
    This->Version = Args->Version;
    return RxWarp12Device::Create(This, Args);
}

static
HRESULT
APIENTRY
RxWarp12GetOptionalDDITables(
    _In_ D3D12DDI_HADAPTER Adapter,
    _Inout_ UINT32 *Entries,
    _Out_writes_opt_(*Entries) D3D12DDI_TABLE_REQUEST *Requests)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Requests);
    if (Entries == nullptr)
        return E_INVALIDARG;
    *Entries = 0;
    return S_OK;
}

static
HRESULT
APIENTRY
RxWarp12FillDDITable(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ D3D12DDI_TABLE_TYPE Type,
    _Inout_ VOID *Table,
    _In_ SIZE_T Size,
    _In_ UINT Index,
    _In_opt_ D3D12DDI_HRTTABLE RuntimeTable)
{
    RxWarpLock Lock;
    RxWarpAdapter *This = static_cast<RxWarpAdapter *>(Adapter.pDrvPrivate);
    UINT64 Level = RxWarp12Level(This);
    SIZE_T Needed;

    switch (Type)
    {
        case D3D12DDI_TABLE_TYPE_DEVICE_CORE:
            Needed = RxWarp12TableSize(RxWarp12DeviceSteps, ARRAYSIZE(RxWarp12DeviceSteps), Level);
            if (Needed == 0)
                return E_UNEXPECTED;
            if (Size < Needed)
                return E_INVALIDARG;
            memcpy(Table, RxWarp12Device::DeviceTable(), Needed);
            return S_OK;

        case D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D:
            Needed = RxWarp12TableSize(RxWarp12ListSteps, ARRAYSIZE(RxWarp12ListSteps), Level);
            if (Size < Needed)
                return E_INVALIDARG;
            memcpy(Table, RxWarp12Device::CommandListTable(), Needed);
            if (Index < ARRAYSIZE(This->CommandListTables))
                This->CommandListTables[Index] = const_cast<VOID *>(RuntimeTable.handle);
            return S_OK;

        case D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D:
            if (Size < sizeof(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001))
                return E_INVALIDARG;
            memcpy(Table, RxWarp12Device::CommandQueueTable(), sizeof(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001));
            return S_OK;

        case D3D12DDI_TABLE_TYPE_DXGI:
            if (Size < sizeof(DXGI1_4_DDI_BASE_FUNCTIONS))
                return E_INVALIDARG;
            memcpy(Table, RxWarp12Device::DxgiTable(), sizeof(DXGI1_4_DDI_BASE_FUNCTIONS));
            return S_OK;

        case D3D12DDI_TABLE_TYPE_0020_EXTENDED_FEATURES:
            if (Level >= RX_WARP12_LEVEL(20, 1))
            {
                if (Size < 4 * sizeof(VOID *))
                    return E_INVALIDARG;
                memcpy(Table, RxWarp12Device::ExtendedFeaturesTable(), 4 * sizeof(VOID *));
                return S_OK;
            }
            if (Level < RX_WARP12_LEVEL(20, 0))
                return E_UNEXPECTED;
            if (Size < 3 * sizeof(VOID *))
                return E_INVALIDARG;
            memcpy(Table, RxWarp12Device::ExtendedFeaturesTable(), 3 * sizeof(VOID *));
            return S_OK;

        case D3D12DDI_TABLE_TYPE_0096_EXTENDED_FEATURES:
            if (Level < RX_WARP12_LEVEL(80, 96))
                return E_UNEXPECTED;
            if (Size < sizeof(D3D12DDI_EXTENDED_FEATURES_FUNCS_0096))
                return E_INVALIDARG;
            memcpy(Table, RxWarp12Device::ExtendedFeaturesTable(), sizeof(D3D12DDI_EXTENDED_FEATURES_FUNCS_0096));
            return S_OK;

        /* Video and content protection tables exist only when those features are on */
        case D3D12DDI_TABLE_TYPE_0020_DEVICE_VIDEO:
        case D3D12DDI_TABLE_TYPE_0022_COMMAND_QUEUE_VIDEO_DECODE:
        case D3D12DDI_TABLE_TYPE_0022_COMMAND_LIST_VIDEO_DECODE:
        case D3D12DDI_TABLE_TYPE_0022_COMMAND_QUEUE_VIDEO_PROCESS:
        case D3D12DDI_TABLE_TYPE_0022_COMMAND_LIST_VIDEO_PROCESS:
        case D3D12DDI_TABLE_TYPE_0053_COMMAND_LIST_VIDEO_ENCODE:
        case D3D12DDI_TABLE_TYPE_0053_COMMAND_QUEUE_VIDEO_ENCODE:
            static_assert(!RxWarp12Video, "video tables are not written yet");
            return E_INVALIDARG;

        case D3D12DDI_TABLE_TYPE_0030_DEVICE_CONTENT_PROTECTION_RESOURCES:
            static_assert(!RxWarp12ContentProtection, "content protection tables are not written yet");
            return E_INVALIDARG;

        default:
            return E_UNEXPECTED;
    }
}

HRESULT
RxWarpAdapter::Open12(
    _Inout_ D3D12DDIARG_OPENADAPTER *Args)
{
    SYSTEM_INFO System;
    RxWarpAdapter *Adapter;

    Adapter = new (std::nothrow) RxWarpAdapter();
    if (Adapter == nullptr)
        return E_OUTOFMEMORY;

    Adapter->RuntimeAdapter = Args->hRTAdapter;
    if (Args->pAdapterCallbacks != nullptr)
        Adapter->Callbacks = *Args->pAdapterCallbacks;
    GetSystemInfo(&System);
    Adapter->ProcessorCount = System.dwNumberOfProcessors;
    Adapter->QueryKernel();
    Adapter->Interface = RxWarp12OpenInterface;
    Adapter->Version = 0;

    Args->hAdapter.pDrvPrivate = Adapter;
    Args->pAdapterFuncs->pfnCalcPrivateDeviceSize = RxWarp12CalcPrivateDeviceSize;
    Args->pAdapterFuncs->pfnCreateDevice = RxWarp12CreateDevice;
    Args->pAdapterFuncs->pfnCloseAdapter = CloseAdapter;
    Args->pAdapterFuncs->pfnGetSupportedVersions = GetSupportedVersions;
    Args->pAdapterFuncs->pfnGetCaps = RxWarp12GetCaps;
    Args->pAdapterFuncs->pfnGetOptionalDDITables = RxWarp12GetOptionalDDITables;
    Args->pAdapterFuncs->pfnFillDDITable = RxWarp12FillDDITable;
    Args->pAdapterFuncs->pfnDestroyDevice = RxWarp12Device::Destroy;
    return S_OK;
}
