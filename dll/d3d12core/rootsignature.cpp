/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Root signature serialization and deserializers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "core.hpp"
#include "blob.hpp"
#include "dxbchash.hpp"

#include <stdio.h>

/* The RTS0 part, little endian 32 bit fields throughout */
constexpr UINT RxRtsHeaderSize = 6 * sizeof(UINT);
constexpr UINT RxRtsParameterSize = 3 * sizeof(UINT);
constexpr UINT RxRtsTableSize = 2 * sizeof(UINT);
constexpr UINT RxRtsConstantsSize = 3 * sizeof(UINT);
constexpr UINT RxContainerHeaderSize = 4 + 16 + 4 + 4 + 4;
constexpr UINT RxPartHeaderSize = 2 * sizeof(UINT);
constexpr UINT RxFourCcDxbc = 0x43425844;
constexpr UINT RxFourCcRts0 = 0x30535452;

static
UINT
RxRangeSize(
    _In_ D3D_ROOT_SIGNATURE_VERSION Version)
{
    return Version == D3D_ROOT_SIGNATURE_VERSION_1_0 ? 5 * sizeof(UINT) : 6 * sizeof(UINT);
}

static
UINT
RxRootDescriptorSize(
    _In_ D3D_ROOT_SIGNATURE_VERSION Version)
{
    return Version == D3D_ROOT_SIGNATURE_VERSION_1_0 ? 2 * sizeof(UINT) : 3 * sizeof(UINT);
}

static
UINT
RxSamplerSize(
    _In_ D3D_ROOT_SIGNATURE_VERSION Version)
{
    return Version == D3D_ROOT_SIGNATURE_VERSION_1_2 ? sizeof(D3D12_STATIC_SAMPLER_DESC1)
                                                     : sizeof(D3D12_STATIC_SAMPLER_DESC);
}

static
UINT
RxRtsVersion(
    _In_ D3D_ROOT_SIGNATURE_VERSION Version)
{
    switch (Version)
    {
        case D3D_ROOT_SIGNATURE_VERSION_1_0:
            return 1;
        case D3D_ROOT_SIGNATURE_VERSION_1_1:
            return 2;
        default:
            return 3;
    }
}

/*
 * Every desc version is read through these, so validation and writing are written once.
 * Version 1.0 parameters and ranges have no flags; 1.1 and 1.2 share the parameter layout.
 */
struct RxRootView
{
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* Desc;

    BOOLEAN
    IsOld() const
    {
        return Desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    UINT
    ParameterCount() const
    {
        return IsOld() ? Desc->Desc_1_0.NumParameters : Desc->Desc_1_1.NumParameters;
    }

    UINT
    SamplerCount() const
    {
        switch (Desc->Version)
        {
            case D3D_ROOT_SIGNATURE_VERSION_1_0:
                return Desc->Desc_1_0.NumStaticSamplers;
            case D3D_ROOT_SIGNATURE_VERSION_1_1:
                return Desc->Desc_1_1.NumStaticSamplers;
            default:
                return Desc->Desc_1_2.NumStaticSamplers;
        }
    }

    D3D12_ROOT_SIGNATURE_FLAGS
    Flags() const
    {
        switch (Desc->Version)
        {
            case D3D_ROOT_SIGNATURE_VERSION_1_0:
                return Desc->Desc_1_0.Flags;
            case D3D_ROOT_SIGNATURE_VERSION_1_1:
                return Desc->Desc_1_1.Flags;
            default:
                return Desc->Desc_1_2.Flags;
        }
    }

    BOOLEAN
    HasParameters() const
    {
        return IsOld() ? Desc->Desc_1_0.pParameters != nullptr : Desc->Desc_1_1.pParameters != nullptr;
    }

    BOOLEAN
    HasSamplers() const
    {
        switch (Desc->Version)
        {
            case D3D_ROOT_SIGNATURE_VERSION_1_0:
                return Desc->Desc_1_0.pStaticSamplers != nullptr;
            case D3D_ROOT_SIGNATURE_VERSION_1_1:
                return Desc->Desc_1_1.pStaticSamplers != nullptr;
            default:
                return Desc->Desc_1_2.pStaticSamplers != nullptr;
        }
    }

    D3D12_ROOT_PARAMETER_TYPE
    Type(
        _In_ UINT Index) const
    {
        return IsOld() ? Desc->Desc_1_0.pParameters[Index].ParameterType
                       : Desc->Desc_1_1.pParameters[Index].ParameterType;
    }

    D3D12_SHADER_VISIBILITY
    Visibility(
        _In_ UINT Index) const
    {
        return IsOld() ? Desc->Desc_1_0.pParameters[Index].ShaderVisibility
                       : Desc->Desc_1_1.pParameters[Index].ShaderVisibility;
    }

    UINT
    RangeCount(
        _In_ UINT Index) const
    {
        return IsOld() ? Desc->Desc_1_0.pParameters[Index].DescriptorTable.NumDescriptorRanges
                       : Desc->Desc_1_1.pParameters[Index].DescriptorTable.NumDescriptorRanges;
    }

    BOOLEAN
    HasRanges(
        _In_ UINT Index) const
    {
        return IsOld() ? Desc->Desc_1_0.pParameters[Index].DescriptorTable.pDescriptorRanges != nullptr
                       : Desc->Desc_1_1.pParameters[Index].DescriptorTable.pDescriptorRanges != nullptr;
    }

    /* A range widened to the 1.1 shape; 1.0 ranges report no flags */
    D3D12_DESCRIPTOR_RANGE1
    Range(
        _In_ UINT Index,
        _In_ UINT RangeIndex) const
    {
        D3D12_DESCRIPTOR_RANGE1 Result = {};

        if (IsOld())
        {
            const D3D12_DESCRIPTOR_RANGE& Old = Desc->Desc_1_0.pParameters[Index].DescriptorTable.pDescriptorRanges[RangeIndex];

            Result.RangeType = Old.RangeType;
            Result.NumDescriptors = Old.NumDescriptors;
            Result.BaseShaderRegister = Old.BaseShaderRegister;
            Result.RegisterSpace = Old.RegisterSpace;
            Result.OffsetInDescriptorsFromTableStart = Old.OffsetInDescriptorsFromTableStart;
            return Result;
        }

        return Desc->Desc_1_1.pParameters[Index].DescriptorTable.pDescriptorRanges[RangeIndex];
    }

    D3D12_ROOT_CONSTANTS
    Constants(
        _In_ UINT Index) const
    {
        return IsOld() ? Desc->Desc_1_0.pParameters[Index].Constants : Desc->Desc_1_1.pParameters[Index].Constants;
    }

    D3D12_ROOT_DESCRIPTOR1
    Descriptor(
        _In_ UINT Index) const
    {
        D3D12_ROOT_DESCRIPTOR1 Result = {};

        if (IsOld())
        {
            Result.ShaderRegister = Desc->Desc_1_0.pParameters[Index].Descriptor.ShaderRegister;
            Result.RegisterSpace = Desc->Desc_1_0.pParameters[Index].Descriptor.RegisterSpace;
            return Result;
        }

        return Desc->Desc_1_1.pParameters[Index].Descriptor;
    }

    /* A sampler widened to the 1.2 shape */
    D3D12_STATIC_SAMPLER_DESC1
    Sampler(
        _In_ UINT Index) const
    {
        const D3D12_STATIC_SAMPLER_DESC* Old;
        D3D12_STATIC_SAMPLER_DESC1 Result = {};

        if (Desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_2)
            return Desc->Desc_1_2.pStaticSamplers[Index];

        Old = Desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_0 ? &Desc->Desc_1_0.pStaticSamplers[Index]
                                                              : &Desc->Desc_1_1.pStaticSamplers[Index];
        CopyMemory(&Result, Old, sizeof(*Old));
        return Result;
    }
};

/* Validation */

/* One register range a root signature binds, for the overlap check */
struct RxBinding
{
    D3D12_DESCRIPTOR_RANGE_TYPE Class;
    D3D12_SHADER_VISIBILITY Visibility;
    UINT Space;
    UINT First;
    UINT64 Last;
};

struct RxValidator
{
    RxBinding* Bindings = nullptr;
    UINT BindingCount = 0;
    UINT BindingCapacity = 0;
    char Message[256] = "";

    ~RxValidator()
    {
        delete[] Bindings;
    }

    BOOLEAN
    Fail(
        _In_z_ _Printf_format_string_ PCSTR Format,
        ...)
    {
        va_list Arguments;

        va_start(Arguments, Format);
        vsprintf_s(Message, Format, Arguments);
        va_end(Arguments);
        return FALSE;
    }

    BOOLEAN
    Add(
        _In_ D3D12_DESCRIPTOR_RANGE_TYPE Class,
        _In_ D3D12_SHADER_VISIBILITY Visibility,
        _In_ UINT Space,
        _In_ UINT First,
        _In_ UINT Count)
    {
        static const char Letters[] = "tubs";
        RxBinding Binding = { Class, Visibility, Space, First,
                              Count == UINT_MAX ? UINT64_MAX : (UINT64)First + Count - 1 };
        UINT Index;

        for (Index = 0; Index < BindingCount; Index++)
        {
            const RxBinding& Other = Bindings[Index];

            if (Other.Class != Class || Other.Space != Space)
                continue;
            if (Other.Visibility != Visibility && Other.Visibility != D3D12_SHADER_VISIBILITY_ALL &&
                Visibility != D3D12_SHADER_VISIBILITY_ALL)
            {
                continue;
            }
            if (Binding.First <= Other.Last && Other.First <= Binding.Last)
            {
                return Fail("register %c%u in space %u is bound twice for the same shader stage", Letters[Class], First, Space);
            }
        }

        if (BindingCount == BindingCapacity)
        {
            UINT NewCapacity = BindingCapacity ? BindingCapacity * 2 : 16;
            RxBinding* NewBindings = new (std::nothrow) RxBinding[NewCapacity];

            if (NewBindings == nullptr)
                return Fail("out of memory while checking registers");
            if (BindingCount != 0)
                CopyMemory(NewBindings, Bindings, BindingCount * sizeof(*Bindings));
            delete[] Bindings;
            Bindings = NewBindings;
            BindingCapacity = NewCapacity;
        }

        Bindings[BindingCount++] = Binding;
        return TRUE;
    }

    BOOLEAN
    CheckRangeFlags(
        _In_ const RxRootView& View,
        _In_ const D3D12_DESCRIPTOR_RANGE1& Range)
    {
        UINT Allowed;
        UINT DataFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE |
                         D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE |
                         D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
        UINT Data = Range.Flags & DataFlags;

        if (View.IsOld())
            return TRUE;

        Allowed = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
                  D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS;
        if (Range.RangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
            Allowed |= DataFlags;
        if ((Range.Flags & ~Allowed) != 0 || (Data & (Data - 1)) != 0)
            return Fail("descriptor range flags 0x%x are not valid for this range", Range.Flags);

        if ((Range.Flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS) != 0 &&
            (Range.Flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE) != 0)
        {
            return Fail("descriptor range flags 0x%x are not valid for this range", Range.Flags);
        }

        return TRUE;
    }

    BOOLEAN
    Check(
        _In_ const RxRootView& View)
    {
        UINT Index;
        UINT RangeIndex;

        if ((View.Flags() & ~0xFFFu) != 0)
            return Fail("root signature flags 0x%x include unknown bits", View.Flags());
        if (View.ParameterCount() != 0 && !View.HasParameters())
            return Fail("%u parameters given without a parameter array", View.ParameterCount());
        if (View.SamplerCount() != 0 && !View.HasSamplers())
            return Fail("%u static samplers given without a sampler array", View.SamplerCount());

        for (Index = 0; Index < View.ParameterCount(); Index++)
        {
            D3D12_SHADER_VISIBILITY Visibility = View.Visibility(Index);

            if ((UINT)Visibility > D3D12_SHADER_VISIBILITY_MESH)
                return Fail("parameter %u has unknown shader visibility %u", Index, Visibility);

            switch (View.Type(Index))
            {
                case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                {
                    BOOLEAN HasSampler = FALSE;
                    BOOLEAN HasOther = FALSE;

                    if (View.RangeCount(Index) != 0 && !View.HasRanges(Index))
                        return Fail("parameter %u is a table without a range array", Index);

                    for (RangeIndex = 0; RangeIndex < View.RangeCount(Index); RangeIndex++)
                    {
                        D3D12_DESCRIPTOR_RANGE1 Range = View.Range(Index, RangeIndex);

                        if ((UINT)Range.RangeType > D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                            return Fail("parameter %u has a range of unknown type %u", Index, Range.RangeType);
                        if (Range.NumDescriptors == 0)
                        {
                            return Fail("range %u of parameter %u holds no descriptors", RangeIndex, Index);
                        }
                        if (!CheckRangeFlags(View, Range))
                            return FALSE;

                        if (Range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                            HasSampler = TRUE;
                        else
                            HasOther = TRUE;
                        if (HasSampler && HasOther)
                        {
                            return Fail("parameter %u puts samplers and views in one table", Index);
                        }

                        if (!Add(Range.RangeType, Visibility, Range.RegisterSpace, Range.BaseShaderRegister,
                                 Range.NumDescriptors))
                        {
                            return FALSE;
                        }
                    }
                    break;
                }
                case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                {
                    D3D12_ROOT_CONSTANTS Constants = View.Constants(Index);

                    if (!Add(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, Visibility, Constants.RegisterSpace,
                             Constants.ShaderRegister, 1))
                    {
                        return FALSE;
                    }
                    break;
                }
                case D3D12_ROOT_PARAMETER_TYPE_CBV:
                case D3D12_ROOT_PARAMETER_TYPE_SRV:
                case D3D12_ROOT_PARAMETER_TYPE_UAV:
                {
                    static const D3D12_DESCRIPTOR_RANGE_TYPE Classes[] = { D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
                                                                           D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                                                                           D3D12_DESCRIPTOR_RANGE_TYPE_UAV };
                    D3D12_ROOT_DESCRIPTOR1 Descriptor = View.Descriptor(Index);
                    UINT Allowed = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE |
                                   D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE |
                                   D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
                    UINT Data = Descriptor.Flags & Allowed;

                    if ((Descriptor.Flags & ~Allowed) != 0 || (Data & (Data - 1)) != 0)
                        return Fail("root descriptor flags 0x%x are not valid", Descriptor.Flags);
                    if (!Add(Classes[View.Type(Index) - D3D12_ROOT_PARAMETER_TYPE_CBV], Visibility,
                             Descriptor.RegisterSpace, Descriptor.ShaderRegister, 1))
                    {
                        return FALSE;
                    }
                    break;
                }
                default:
                    return Fail("parameter %u has unknown type %u", Index, View.Type(Index));
            }
        }

        for (Index = 0; Index < View.SamplerCount(); Index++)
        {
            D3D12_STATIC_SAMPLER_DESC1 Sampler = View.Sampler(Index);

            if ((UINT)Sampler.ShaderVisibility > D3D12_SHADER_VISIBILITY_MESH)
                return Fail("static sampler %u has unknown shader visibility %u", Index, Sampler.ShaderVisibility);
            if (!Add(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, Sampler.ShaderVisibility, Sampler.RegisterSpace,
                     Sampler.ShaderRegister, 1))
            {
                return FALSE;
            }
        }

        return TRUE;
    }
};

/* Writing */

struct RxWriter
{
    BYTE* Base;
    UINT Size;

    VOID
    Put(
        _In_ UINT Offset,
        _In_ UINT Value)
    {
        CopyMemory(Base + Offset, &Value, sizeof(Value));
    }
};

static
UINT
RxPartSize(
    _In_ const RxRootView& View,
    _In_ D3D_ROOT_SIGNATURE_VERSION Version)
{
    UINT Size = RxRtsHeaderSize + RxRtsParameterSize * View.ParameterCount();
    UINT Index;

    for (Index = 0; Index < View.ParameterCount(); Index++)
    {
        switch (View.Type(Index))
        {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                Size += RxRtsTableSize + RxRangeSize(Version) * View.RangeCount(Index);
                break;
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                Size += RxRtsConstantsSize;
                break;
            default:
                Size += RxRootDescriptorSize(Version);
                break;
        }
    }

    return Size + RxSamplerSize(Version) * View.SamplerCount();
}

static
VOID
RxWritePart(
    _In_ const RxRootView& View,
    _In_ D3D_ROOT_SIGNATURE_VERSION Version,
    _Inout_ RxWriter& Out)
{
    UINT Parameters = RxRtsHeaderSize;
    UINT Payload = Parameters + RxRtsParameterSize * View.ParameterCount();
    UINT Index;
    UINT RangeIndex;

    Out.Put(0, RxRtsVersion(Version));
    Out.Put(4, View.ParameterCount());
    Out.Put(8, Parameters);
    Out.Put(12, View.SamplerCount());
    Out.Put(20, View.Flags());

    for (Index = 0; Index < View.ParameterCount(); Index++)
    {
        UINT Entry = Parameters + Index * RxRtsParameterSize;

        Out.Put(Entry, View.Type(Index));
        Out.Put(Entry + 4, View.Visibility(Index));
        Out.Put(Entry + 8, Payload);

        switch (View.Type(Index))
        {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                Out.Put(Payload, View.RangeCount(Index));
                Out.Put(Payload + 4, Payload + RxRtsTableSize);
                Payload += RxRtsTableSize;
                for (RangeIndex = 0; RangeIndex < View.RangeCount(Index); RangeIndex++)
                {
                    D3D12_DESCRIPTOR_RANGE1 Range = View.Range(Index, RangeIndex);

                    Out.Put(Payload, Range.RangeType);
                    Out.Put(Payload + 4, Range.NumDescriptors);
                    Out.Put(Payload + 8, Range.BaseShaderRegister);
                    Out.Put(Payload + 12, Range.RegisterSpace);
                    if (Version == D3D_ROOT_SIGNATURE_VERSION_1_0)
                    {
                        Out.Put(Payload + 16, Range.OffsetInDescriptorsFromTableStart);
                    }
                    else
                    {
                        Out.Put(Payload + 16, Range.Flags);
                        Out.Put(Payload + 20, Range.OffsetInDescriptorsFromTableStart);
                    }
                    Payload += RxRangeSize(Version);
                }
                break;
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            {
                D3D12_ROOT_CONSTANTS Constants = View.Constants(Index);

                Out.Put(Payload, Constants.ShaderRegister);
                Out.Put(Payload + 4, Constants.RegisterSpace);
                Out.Put(Payload + 8, Constants.Num32BitValues);
                Payload += RxRtsConstantsSize;
                break;
            }
            default:
            {
                D3D12_ROOT_DESCRIPTOR1 Descriptor = View.Descriptor(Index);

                Out.Put(Payload, Descriptor.ShaderRegister);
                Out.Put(Payload + 4, Descriptor.RegisterSpace);
                if (Version != D3D_ROOT_SIGNATURE_VERSION_1_0)
                    Out.Put(Payload + 8, Descriptor.Flags);
                Payload += RxRootDescriptorSize(Version);
                break;
            }
        }
    }

    Out.Put(16, Payload);
    for (Index = 0; Index < View.SamplerCount(); Index++)
    {
        D3D12_STATIC_SAMPLER_DESC1 Sampler = View.Sampler(Index);

        CopyMemory(Out.Base + Payload, &Sampler, RxSamplerSize(Version));
        Payload += RxSamplerSize(Version);
    }
}

static
VOID
RxReportError(
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob,
    _In_z_ PCSTR Message)
{
    RxBlob* Blob;
    SIZE_T Length = strlen(Message) + 1;

    if (ppErrorBlob == nullptr)
        return;

    if (SUCCEEDED(RxBlob::Create(Length, &Blob)))
    {
        CopyMemory(Blob->GetBufferPointer(), Message, Length);
        *ppErrorBlob = Blob;
    }
}

static
HRESULT
RxSerialize(
    _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* Desc,
    _Out_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob)
{
    RxRootView View = { Desc };
    RxValidator Validator;
    RxWriter Out;
    RxBlob* Blob;
    UINT PartSize;
    HRESULT hr;

    if (ppErrorBlob != nullptr)
        *ppErrorBlob = nullptr;
    if (ppBlob == nullptr)
        return E_INVALIDARG;
    *ppBlob = nullptr;

    if (!Validator.Check(View))
    {
        RX_TRACE(L"root signature rejected: %S", Validator.Message);
        RxReportError(ppErrorBlob, Validator.Message);
        return E_INVALIDARG;
    }

    PartSize = RxPartSize(View, Desc->Version);
    hr = RxBlob::Create(RxContainerHeaderSize + sizeof(UINT) + RxPartHeaderSize + PartSize, &Blob);
    if (FAILED(hr))
        return hr;

    Out.Base = static_cast<BYTE*>(Blob->GetBufferPointer());
    Out.Size = (UINT)Blob->GetBufferSize();
    Out.Put(0, RxFourCcDxbc);
    Out.Put(20, 1);
    Out.Put(24, Out.Size);
    Out.Put(28, 1);
    Out.Put(32, RxContainerHeaderSize + sizeof(UINT));
    Out.Put(36, RxFourCcRts0);
    Out.Put(40, PartSize);

    {
        RxWriter Part = { Out.Base + 44, PartSize };

        RxWritePart(View, Desc->Version, Part);
    }

    RxDxbcHash(Out.Base + 20, Out.Size - 20, Out.Base + 4);
    *ppBlob = Blob;
    return S_OK;
}

HRESULT
WINAPI
RxSerializeRootSignature(
    _In_ const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
    _In_ D3D_ROOT_SIGNATURE_VERSION Version,
    _Out_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob)
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC Desc = {};

    if (pRootSignature == nullptr || Version != D3D_ROOT_SIGNATURE_VERSION_1_0)
    {
        if (ppBlob != nullptr)
            *ppBlob = nullptr;
        if (ppErrorBlob != nullptr)
            *ppErrorBlob = nullptr;
        return E_INVALIDARG;
    }

    Desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
    Desc.Desc_1_0 = *pRootSignature;
    return RxSerialize(&Desc, ppBlob, ppErrorBlob);
}

HRESULT
WINAPI
RxSerializeVersionedRootSignature(
    _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignature,
    _Out_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob)
{
    if (pRootSignature == nullptr || pRootSignature->Version < D3D_ROOT_SIGNATURE_VERSION_1_0 ||
        pRootSignature->Version > D3D_ROOT_SIGNATURE_VERSION_1_2)
    {
        if (ppBlob != nullptr)
            *ppBlob = nullptr;
        if (ppErrorBlob != nullptr)
            *ppErrorBlob = nullptr;
        return E_INVALIDARG;
    }

    return RxSerialize(pRootSignature, ppBlob, ppErrorBlob);
}

/* Reading */

/* The RTS0 part inside a container, which may also hold a whole shader */
static
BOOLEAN
RxFindRootPart(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ SIZE_T Size,
    _Out_ const BYTE** Part,
    _Out_ UINT* PartSize)
{
    UINT Value;
    UINT Count;
    UINT Index;

    *Part = nullptr;
    *PartSize = 0;
    if (Data == nullptr || Size < RxContainerHeaderSize)
        return FALSE;

    CopyMemory(&Value, Data, sizeof(Value));
    if (Value != RxFourCcDxbc)
        return FALSE;

    CopyMemory(&Value, Data + 24, sizeof(Value));
    CopyMemory(&Count, Data + 28, sizeof(Count));
    if (Value > Size || Count > (Value - RxContainerHeaderSize) / sizeof(UINT))
        return FALSE;
    Size = Value;

    for (Index = 0; Index < Count; Index++)
    {
        UINT Offset;
        UINT FourCc;
        UINT Length;

        CopyMemory(&Offset, Data + RxContainerHeaderSize + Index * sizeof(UINT), sizeof(Offset));
        if (Offset > Size || Size - Offset < RxPartHeaderSize)
            return FALSE;

        CopyMemory(&FourCc, Data + Offset, sizeof(FourCc));
        CopyMemory(&Length, Data + Offset + 4, sizeof(Length));
        if (Length > Size - Offset - RxPartHeaderSize)
            return FALSE;

        if (FourCc == RxFourCcRts0)
        {
            *Part = Data + Offset + RxPartHeaderSize;
            *PartSize = Length;
            return TRUE;
        }
    }

    return FALSE;
}

/* A root signature held as every version, each built from the one the blob carries */
class RxRootSignatureDesc
{
public:
    ~RxRootSignatureDesc()
    {
        UINT Index;

        for (Index = 0; Index < _countof(Descs); Index++)
            Release(Index);
    }

    HRESULT
    Parse(
        _In_reads_bytes_(Size) const BYTE* Part,
        _In_ UINT Size);

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

VOID
RxRootSignatureDesc::Release(
    _In_ UINT Index)
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC& Desc = Descs[Index];
    UINT Parameter;

    if (!Built[Index])
        return;

    if (Index == 0)
    {
        for (Parameter = 0; Parameter < Desc.Desc_1_0.NumParameters; Parameter++)
        {
            if (Desc.Desc_1_0.pParameters[Parameter].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                delete[] Desc.Desc_1_0.pParameters[Parameter].DescriptorTable.pDescriptorRanges;
        }
        delete[] Desc.Desc_1_0.pParameters;
        delete[] Desc.Desc_1_0.pStaticSamplers;
    }
    else
    {
        for (Parameter = 0; Parameter < Desc.Desc_1_1.NumParameters; Parameter++)
        {
            if (Desc.Desc_1_1.pParameters[Parameter].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                delete[] Desc.Desc_1_1.pParameters[Parameter].DescriptorTable.pDescriptorRanges;
        }
        delete[] Desc.Desc_1_1.pParameters;
        if (Index == 1)
            delete[] Desc.Desc_1_1.pStaticSamplers;
        else
            delete[] Desc.Desc_1_2.pStaticSamplers;
    }

    Built[Index] = FALSE;
}

/*
 * Version 1.0 behaves like 1.1 with volatile descriptors and data, so widening a 1.0 desc spells that out:
 * sampler ranges get volatile descriptors, other ranges volatile descriptors and data, root descriptors volatile data.
 */
HRESULT
RxRootSignatureDesc::Build(
    _In_ D3D_ROOT_SIGNATURE_VERSION Version,
    _In_ const RxRootView& From)
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC& Desc = Descs[Version - 1];
    UINT Count = From.ParameterCount();
    UINT Samplers = From.SamplerCount();
    UINT Index;
    UINT RangeIndex;

    Desc.Version = Version;

    if (Version == D3D_ROOT_SIGNATURE_VERSION_1_0)
    {
        D3D12_ROOT_PARAMETER* Parameters = Count ? new (std::nothrow) D3D12_ROOT_PARAMETER[Count]() : nullptr;
        D3D12_STATIC_SAMPLER_DESC* Static = Samplers ? new (std::nothrow) D3D12_STATIC_SAMPLER_DESC[Samplers]() : nullptr;

        Desc.Desc_1_0.NumParameters = 0;
        Desc.Desc_1_0.pParameters = Parameters;
        Desc.Desc_1_0.NumStaticSamplers = Samplers;
        Desc.Desc_1_0.pStaticSamplers = Static;
        Desc.Desc_1_0.Flags = From.Flags();
        Built[0] = TRUE;
        if ((Count && Parameters == nullptr) || (Samplers && Static == nullptr))
            return E_OUTOFMEMORY;

        for (Index = 0; Index < Count; Index++)
        {
            D3D12_ROOT_PARAMETER& Parameter = Parameters[Index];

            Parameter.ParameterType = From.Type(Index);
            Parameter.ShaderVisibility = From.Visibility(Index);
            Desc.Desc_1_0.NumParameters = Index + 1;

            if (Parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            {
                UINT Ranges = From.RangeCount(Index);
                D3D12_DESCRIPTOR_RANGE* Table = Ranges ? new (std::nothrow) D3D12_DESCRIPTOR_RANGE[Ranges]() : nullptr;

                if (Ranges && Table == nullptr)
                    return E_OUTOFMEMORY;
                for (RangeIndex = 0; RangeIndex < Ranges; RangeIndex++)
                {
                    D3D12_DESCRIPTOR_RANGE1 Range = From.Range(Index, RangeIndex);

                    Table[RangeIndex].RangeType = Range.RangeType;
                    Table[RangeIndex].NumDescriptors = Range.NumDescriptors;
                    Table[RangeIndex].BaseShaderRegister = Range.BaseShaderRegister;
                    Table[RangeIndex].RegisterSpace = Range.RegisterSpace;
                    Table[RangeIndex].OffsetInDescriptorsFromTableStart = Range.OffsetInDescriptorsFromTableStart;
                }
                Parameter.DescriptorTable.NumDescriptorRanges = Ranges;
                Parameter.DescriptorTable.pDescriptorRanges = Table;
            }
            else if (Parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
            {
                Parameter.Constants = From.Constants(Index);
            }
            else
            {
                D3D12_ROOT_DESCRIPTOR1 Descriptor = From.Descriptor(Index);

                Parameter.Descriptor.ShaderRegister = Descriptor.ShaderRegister;
                Parameter.Descriptor.RegisterSpace = Descriptor.RegisterSpace;
            }
        }

        for (Index = 0; Index < Samplers; Index++)
        {
            D3D12_STATIC_SAMPLER_DESC1 Sampler = From.Sampler(Index);

            CopyMemory(&Static[Index], &Sampler, sizeof(Static[Index]));
        }
        return S_OK;
    }

    {
        D3D12_ROOT_PARAMETER1* Parameters = Count ? new (std::nothrow) D3D12_ROOT_PARAMETER1[Count]() : nullptr;
        void* Static;

        if (Version == D3D_ROOT_SIGNATURE_VERSION_1_1)
            Static = Samplers ? new (std::nothrow) D3D12_STATIC_SAMPLER_DESC[Samplers]() : nullptr;
        else
            Static = Samplers ? new (std::nothrow) D3D12_STATIC_SAMPLER_DESC1[Samplers]() : nullptr;

        Desc.Desc_1_1.NumParameters = 0;
        Desc.Desc_1_1.pParameters = Parameters;
        Desc.Desc_1_1.NumStaticSamplers = Samplers;
        Desc.Desc_1_1.pStaticSamplers = static_cast<D3D12_STATIC_SAMPLER_DESC*>(Static);
        Desc.Desc_1_1.Flags = From.Flags();
        Built[Version - 1] = TRUE;
        if ((Count && Parameters == nullptr) || (Samplers && Static == nullptr))
            return E_OUTOFMEMORY;

        for (Index = 0; Index < Count; Index++)
        {
            D3D12_ROOT_PARAMETER1& Parameter = Parameters[Index];

            Parameter.ParameterType = From.Type(Index);
            Parameter.ShaderVisibility = From.Visibility(Index);
            Desc.Desc_1_1.NumParameters = Index + 1;

            if (Parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            {
                UINT Ranges = From.RangeCount(Index);
                D3D12_DESCRIPTOR_RANGE1* Table = Ranges ? new (std::nothrow) D3D12_DESCRIPTOR_RANGE1[Ranges]() : nullptr;

                if (Ranges && Table == nullptr)
                    return E_OUTOFMEMORY;
                for (RangeIndex = 0; RangeIndex < Ranges; RangeIndex++)
                {
                    Table[RangeIndex] = From.Range(Index, RangeIndex);
                    if (From.IsOld())
                    {
                        Table[RangeIndex].Flags = Table[RangeIndex].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
                                                      ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
                                                      : D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
                                                            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
                    }
                }
                Parameter.DescriptorTable.NumDescriptorRanges = Ranges;
                Parameter.DescriptorTable.pDescriptorRanges = Table;
            }
            else if (Parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
            {
                Parameter.Constants = From.Constants(Index);
            }
            else
            {
                Parameter.Descriptor = From.Descriptor(Index);
                if (From.IsOld())
                    Parameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
            }
        }

        for (Index = 0; Index < Samplers; Index++)
        {
            D3D12_STATIC_SAMPLER_DESC1 Sampler = From.Sampler(Index);

            if (Version == D3D_ROOT_SIGNATURE_VERSION_1_1)
                CopyMemory(&static_cast<D3D12_STATIC_SAMPLER_DESC*>(Static)[Index], &Sampler, sizeof(D3D12_STATIC_SAMPLER_DESC));
            else
                static_cast<D3D12_STATIC_SAMPLER_DESC1*>(Static)[Index] = Sampler;
        }
    }

    return S_OK;
}

HRESULT
RxRootSignatureDesc::Parse(
    _In_reads_bytes_(Size) const BYTE* Part,
    _In_ UINT Size)
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC& Desc = Descs[0];
    UINT Header[6];
    UINT RangeSize;
    UINT DescriptorSize;
    UINT SamplerSize;
    UINT Index;
    UINT RangeIndex;
    HRESULT hr;

    auto Read = [&](UINT Offset, UINT Length, void* Out) -> BOOLEAN
    {
        if (Offset > Size || Length > Size - Offset)
            return FALSE;
        CopyMemory(Out, Part + Offset, Length);
        return TRUE;
    };

    if (!Read(0, sizeof(Header), Header) || Header[0] < 1 || Header[0] > 3)
        return E_INVALIDARG;

    Original = (D3D_ROOT_SIGNATURE_VERSION)Header[0];
    RangeSize = RxRangeSize(Original);
    DescriptorSize = RxRootDescriptorSize(Original);
    SamplerSize = RxSamplerSize(Original);
    if (Header[1] > Size / RxRtsParameterSize || Header[3] > Size / SamplerSize)
        return E_INVALIDARG;

    /* Read straight into the slot of the blob's own version, in the 1.1 or 1.2 shape */
    {
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC& Own = Descs[Original - 1];
        D3D12_ROOT_PARAMETER1* Parameters = Header[1] ? new (std::nothrow) D3D12_ROOT_PARAMETER1[Header[1]]() : nullptr;
        D3D12_STATIC_SAMPLER_DESC1* Samplers = Header[3] ? new (std::nothrow) D3D12_STATIC_SAMPLER_DESC1[Header[3]]()
                                                         : nullptr;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC Wide = {};

        Wide.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
        Wide.Desc_1_2.pParameters = Parameters;
        Wide.Desc_1_2.NumStaticSamplers = Header[3];
        Wide.Desc_1_2.pStaticSamplers = Samplers;
        Wide.Desc_1_2.Flags = (D3D12_ROOT_SIGNATURE_FLAGS)Header[5];

        hr = S_OK;
        if ((Header[1] && Parameters == nullptr) || (Header[3] && Samplers == nullptr))
            hr = E_OUTOFMEMORY;

        for (Index = 0; SUCCEEDED(hr) && Index < Header[1]; Index++)
        {
            D3D12_ROOT_PARAMETER1& Parameter = Parameters[Index];
            UINT Entry[3];

            Wide.Desc_1_2.NumParameters = Index + 1;
            if (!Read(Header[2] + Index * RxRtsParameterSize, sizeof(Entry), Entry) ||
                Entry[0] > D3D12_ROOT_PARAMETER_TYPE_UAV)
            {
                hr = E_INVALIDARG;
                break;
            }

            Parameter.ParameterType = (D3D12_ROOT_PARAMETER_TYPE)Entry[0];
            Parameter.ShaderVisibility = (D3D12_SHADER_VISIBILITY)Entry[1];
            if (Parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            {
                UINT Table[2];
                D3D12_DESCRIPTOR_RANGE1* Ranges;

                if (!Read(Entry[2], sizeof(Table), Table) || Table[0] > Size / RangeSize)
                {
                    hr = E_INVALIDARG;
                    break;
                }

                Ranges = Table[0] ? new (std::nothrow) D3D12_DESCRIPTOR_RANGE1[Table[0]]() : nullptr;
                if (Table[0] && Ranges == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                Parameter.DescriptorTable.NumDescriptorRanges = Table[0];
                Parameter.DescriptorTable.pDescriptorRanges = Ranges;

                for (RangeIndex = 0; RangeIndex < Table[0]; RangeIndex++)
                {
                    UINT Range[6] = {};

                    if (!Read(Table[1] + RangeIndex * RangeSize, RangeSize, Range))
                    {
                        hr = E_INVALIDARG;
                        break;
                    }
                    Ranges[RangeIndex].RangeType = (D3D12_DESCRIPTOR_RANGE_TYPE)Range[0];
                    Ranges[RangeIndex].NumDescriptors = Range[1];
                    Ranges[RangeIndex].BaseShaderRegister = Range[2];
                    Ranges[RangeIndex].RegisterSpace = Range[3];
                    if (Original == D3D_ROOT_SIGNATURE_VERSION_1_0)
                    {
                        Ranges[RangeIndex].OffsetInDescriptorsFromTableStart = Range[4];
                    }
                    else
                    {
                        Ranges[RangeIndex].Flags = (D3D12_DESCRIPTOR_RANGE_FLAGS)Range[4];
                        Ranges[RangeIndex].OffsetInDescriptorsFromTableStart = Range[5];
                    }
                }
            }
            else if (Parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
            {
                if (!Read(Entry[2], sizeof(Parameter.Constants), &Parameter.Constants))
                    hr = E_INVALIDARG;
            }
            else
            {
                if (!Read(Entry[2], DescriptorSize, &Parameter.Descriptor))
                    hr = E_INVALIDARG;
            }
        }

        for (Index = 0; SUCCEEDED(hr) && Index < Header[3]; Index++)
        {
            if (!Read(Header[4] + Index * SamplerSize, SamplerSize, &Samplers[Index]))
                hr = E_INVALIDARG;
        }

        /* The wide copy becomes the blob's own version, then that one is what the others are made from */
        if (SUCCEEDED(hr))
        {
            RxRootView WideView = { &Wide };

            if (Original == D3D_ROOT_SIGNATURE_VERSION_1_0)
            {
                Wide.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
                hr = Build(D3D_ROOT_SIGNATURE_VERSION_1_0, WideView);
            }
            else
            {
                hr = Build(Original, WideView);
            }
        }

        for (Index = 0; Index < Wide.Desc_1_2.NumParameters; Index++)
        {
            if (Parameters[Index].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                delete[] Parameters[Index].DescriptorTable.pDescriptorRanges;
        }
        delete[] Parameters;
        delete[] Samplers;
        UNREFERENCED_PARAMETER(Own);
    }

    UNREFERENCED_PARAMETER(Desc);
    return hr;
}

_Ret_maybenull_
const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*
RxRootSignatureDesc::At(
    _In_ D3D_ROOT_SIGNATURE_VERSION Version)
{
    RxRootView From = { Unconverted() };

    if (Version < D3D_ROOT_SIGNATURE_VERSION_1_0 || Version > D3D_ROOT_SIGNATURE_VERSION_1_2)
        return nullptr;

    if (!Built[Version - 1] && FAILED(Build(Version, From)))
    {
        Release(Version - 1);
        return nullptr;
    }

    return &Descs[Version - 1];
}

/* Both deserializer interfaces over one parsed desc */
class RxRootSignatureDeserializer final : public ID3D12RootSignatureDeserializer,
                                          public ID3D12VersionedRootSignatureDeserializer
{
public:
    static
    HRESULT
    Create(
        _In_reads_bytes_(Size) LPCVOID Data,
        _In_ SIZE_T Size,
        _In_ REFIID riid,
        _Out_ void** ppObject)
    {
        RxRootSignatureDeserializer* Object;
        const BYTE* Part;
        UINT PartSize;
        HRESULT hr;

        if (ppObject == nullptr)
            return E_INVALIDARG;
        *ppObject = nullptr;

        if (!RxFindRootPart(static_cast<const BYTE*>(Data), Size, &Part, &PartSize))
            return E_INVALIDARG;

        Object = new (std::nothrow) RxRootSignatureDeserializer();
        if (Object == nullptr)
            return E_OUTOFMEMORY;

        hr = Object->Desc.Parse(Part, PartSize);
        if (SUCCEEDED(hr))
            hr = Object->QueryInterface(riid, ppObject);

        static_cast<ID3D12RootSignatureDeserializer*>(Object)->Release();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12RootSignatureDeserializer))
            *ppvObject = static_cast<ID3D12RootSignatureDeserializer*>(this);
        else if (riid == __uuidof(ID3D12VersionedRootSignatureDeserializer))
            *ppvObject = static_cast<ID3D12VersionedRootSignatureDeserializer*>(this);
        else
        {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return InterlockedIncrement(&RefCount);
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        ULONG Count = InterlockedDecrement(&RefCount);

        if (Count == 0)
            delete this;
        return Count;
    }

    const D3D12_ROOT_SIGNATURE_DESC*
    STDMETHODCALLTYPE
    GetRootSignatureDesc(VOID) override
    {
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* Old;

        AcquireSRWLockExclusive(&Lock);
        Old = Desc.At(D3D_ROOT_SIGNATURE_VERSION_1_0);
        ReleaseSRWLockExclusive(&Lock);
        return Old != nullptr ? &Old->Desc_1_0 : nullptr;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetRootSignatureDescAtVersion(
        D3D_ROOT_SIGNATURE_VERSION convertToVersion,
        _Out_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC** ppDesc) override
    {
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* Result;

        if (ppDesc == nullptr)
            return E_INVALIDARG;

        AcquireSRWLockExclusive(&Lock);
        Result = Desc.At(convertToVersion);
        ReleaseSRWLockExclusive(&Lock);

        *ppDesc = Result;
        return Result != nullptr ? S_OK : E_INVALIDARG;
    }

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*
    STDMETHODCALLTYPE
    GetUnconvertedRootSignatureDesc(VOID) override
    {
        return Desc.Unconverted();
    }

private:
    RxRootSignatureDeserializer() = default;
    ~RxRootSignatureDeserializer() = default;

    LONG RefCount = 1;
    SRWLOCK Lock = SRWLOCK_INIT;
    RxRootSignatureDesc Desc;
};

HRESULT
WINAPI
RxCreateRootSignatureDeserializer(
    _In_reads_bytes_(SrcDataSizeInBytes) LPCVOID pSrcData,
    _In_ SIZE_T SrcDataSizeInBytes,
    _In_ REFIID pRootSignatureDeserializerInterface,
    _Out_ void** ppRootSignatureDeserializer)
{
    return RxRootSignatureDeserializer::Create(pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface,
                                               ppRootSignatureDeserializer);
}

HRESULT
WINAPI
RxCreateVersionedRootSignatureDeserializer(
    _In_reads_bytes_(SrcDataSizeInBytes) LPCVOID pSrcData,
    _In_ SIZE_T SrcDataSizeInBytes,
    _In_ REFIID pRootSignatureDeserializerInterface,
    _Out_ void** ppRootSignatureDeserializer)
{
    return RxRootSignatureDeserializer::Create(pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface,
                                               ppRootSignatureDeserializer);
}
