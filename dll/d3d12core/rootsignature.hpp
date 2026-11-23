/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Root signature blobs read back into descs, for deserializers and devices
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* How a reader treats the flags it finds; deserializers and devices read differently */
struct RxRootReadRules
{
    /* Bits cleared from range, root descriptor and root signature flags */
    UINT RangeMask;
    UINT DescriptorMask;
    UINT SignatureMask;
    /* RxRootFill* below */
    UINT Options;
};

/* Ranges and root descriptors without data flags get the 1.1 defaults spelled out */
constexpr UINT RxRootFillDefaults = 0x1;
/* Static descriptors that keep bounds checks become volatile, for drivers that predate them */
constexpr UINT RxRootFillBoundsChecks = 0x2;
/* Unbounded ranges declared static become volatile */
constexpr UINT RxRootFillUnbounded = 0x4;

/* What the public deserializers apply */
constexpr RxRootReadRules RxPublicReadRules = { 0, 0, 0x80000000u, 0 };

struct RxRootView;

/* A root signature held as every version, each built from the one the blob carries */
class RxRootSignatureDesc
{
public:
    ~RxRootSignatureDesc();

    HRESULT
    Parse(
        _In_reads_bytes_(Size) const BYTE* Part,
        _In_ UINT Size,
        _In_ const RxRootReadRules& Rules);

    _Ret_maybenull_
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*
    At(
        _In_ D3D_ROOT_SIGNATURE_VERSION Version);

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*
    Unconverted() const
    {
        return &Descs[Original - 1];
    }

private:
    VOID
    Release(
        _In_ UINT Index);

    HRESULT
    Build(
        _In_ D3D_ROOT_SIGNATURE_VERSION Version,
        _In_ const RxRootView& From);

    /* Indexed by version - 1 */
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC Descs[3] = {};
    BOOLEAN Built[3] = {};
    D3D_ROOT_SIGNATURE_VERSION Original = D3D_ROOT_SIGNATURE_VERSION_1_0;
};

/* A part of a DXBC style container by its four character code */
BOOLEAN
RxFindContainerPart(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ SIZE_T Size,
    _In_ UINT FourCc,
    _Out_ const BYTE** Part,
    _Out_ UINT* PartSize);

/* The RTS0 part inside a container, which may also hold a whole shader */
BOOLEAN
RxFindRootPart(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ SIZE_T Size,
    _Out_ const BYTE** Part,
    _Out_ UINT* PartSize);

/* The register and flag rules serialization applies, run on a desc read back from a blob */
BOOLEAN
RxVerifyRootSignature(
    _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* Desc);
