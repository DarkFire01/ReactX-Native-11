/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The last step of a shader conversion: signatures, the input layout and the signed container
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "scprivate.hpp"
#include "scfinal.hpp"

#include <DxbcBuilder.hpp>
#include <dxbchash.hpp>

#include <algorithm>

/* One parameter of an ISG1 or OSG1 chunk */
struct ScSignatureParameter
{
    UINT Stream;
    UINT NameOffset;
    UINT SemanticIndex;
    UINT SystemValue;
    UINT ComponentType;
    UINT Register;
    BYTE Mask;
    BYTE NeverWritten;
    BYTE Reserved[2];
    UINT MinPrecision;
};

static_assert(sizeof(ScSignatureParameter) == 32, "signature parameters are 32 bytes");

/* Size of the count and offset words in front of the parameters */
static constexpr UINT ScSignatureHeaderSize = 8;

/* A parameter before it is sorted by register, with the name it gets */
struct ScSignatureEntry
{
    PCSTR Name;
    D3D12_INPUT_ELEMENT_DESC Element;
    ScSignatureParameter Parameter;
};

/* Component types of ISG1 and OSG1 parameters */
static constexpr UINT ScComponentUnknown = 0;
static constexpr UINT ScComponentUint = 1;
static constexpr UINT ScComponentSint = 2;
static constexpr UINT ScComponentFloat = 3;

/* Direct3D 9 usages the output side names on its own */
static constexpr UINT ScUsagePosition = 0;
static constexpr UINT ScUsagePositionT = 15;
static constexpr UINT ScUsageFace = 14;
static constexpr UINT ScUsageClip = 16;

static
PCSTR
ScUsageName(
    _In_ UINT Usage)
{
    static const PCSTR Names[] =
    {
        "POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD", "TANGENT", "BINORMAL",
        "TESSFACTOR", "", "COLOR", "FOG", "DEPTH", "SAMPLE",
    };

    return Usage < _countof(Names) ? Names[Usage] : "";
}

/* The name a vertex shader output is linked to the next stage by */
static
PCSTR
ScOutputName(
    _In_ UINT Usage)
{
    switch (Usage)
    {
        case ScUsagePosition:
        case ScUsagePositionT:
            return "SV_POSITION";
        case ScUsageFace:
            return "SV_IsFrontFace";
        case ScUsageClip:
            return "SV_ClipDistance";
        case 17:
            return "POINTSPRITE";
        default:
            return ScUsageName(Usage);
    }
}

static
UINT
ScOutputSystemValue(
    _In_ UINT Usage)
{
    switch (Usage)
    {
        case ScUsagePosition:
        case ScUsagePositionT:
            return D3D_NAME_POSITION;
        case ScUsageFace:
            return D3D_NAME_IS_FRONT_FACE;
        case ScUsageClip:
            return D3D_NAME_CLIP_DISTANCE;
        default:
            return D3D_NAME_UNDEFINED;
    }
}

/* D3DDECLTYPE values that do not read as floats */
static constexpr UINT ScDeclUbyte4 = 5;
static constexpr UINT ScDeclShort2 = 6;
static constexpr UINT ScDeclShort4 = 7;
static constexpr UINT ScDeclUdec3 = 13;
static constexpr UINT ScDeclUnused = 17;

/* The DXGI format and the component type each D3DDECLTYPE reads as */
static
VOID
ScElementFormat(
    _In_ UINT Type,
    _Out_ DXGI_FORMAT* Format,
    _Out_ UINT* ComponentType)
{
    static const DXGI_FORMAT Formats[] =
    {
        DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UINT,
        DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R16G16B16A16_SINT, DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16G16_SNORM, DXGI_FORMAT_R16G16B16A16_SNORM, DXGI_FORMAT_R16G16_UNORM,
        DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R10G10B10A2_UINT, DXGI_FORMAT_R10G10B10A2_UINT,
        DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_UNKNOWN,
    };

    *Format = Type < _countof(Formats) ? Formats[Type] : DXGI_FORMAT_UNKNOWN;
    switch (Type)
    {
        case ScDeclUbyte4:
        case ScDeclUdec3:
            *ComponentType = ScComponentUint;
            break;
        case ScDeclShort2:
        case ScDeclShort4:
            *ComponentType = ScComponentSint;
            break;
        default:
            *ComponentType = Type < ScDeclUnused ? ScComponentFloat : ScComponentUnknown;
            break;
    }
}

static
BOOL
ScRegisterBefore(
    _In_ const ScSignatureEntry& Left,
    _In_ const ScSignatureEntry& Right)
{
    return Left.Parameter.Register < Right.Parameter.Register;
}

/* Lays the parameters and their names out as one chunk, padded to whole words */
static
VOID
ScWriteSignature(
    _In_ const std::vector<ScSignatureEntry>& Entries,
    _Out_ NineScBlob* Signature)
{
    UINT Count = static_cast<UINT>(Entries.size());
    UINT Size = ScSignatureHeaderSize + Count * sizeof(ScSignatureParameter);
    UINT NameAt;
    UINT* Words;

    for (const ScSignatureEntry& Entry : Entries)
        Size += static_cast<UINT>(strlen(Entry.Name)) + 1;

    Signature->assign((Size + 3) & ~3u, 0);
    Words = reinterpret_cast<UINT*>(Signature->data());
    Words[0] = Count;
    Words[1] = ScSignatureHeaderSize;

    NameAt = ScSignatureHeaderSize + Count * sizeof(ScSignatureParameter);
    for (UINT Index = 0; Index < Count; Index += 1)
    {
        ScSignatureParameter Parameter = Entries[Index].Parameter;
        SIZE_T Length = strlen(Entries[Index].Name) + 1;

        Parameter.NameOffset = NameAt;
        memcpy(Signature->data() + ScSignatureHeaderSize + Index * sizeof(Parameter), &Parameter, sizeof(Parameter));
        memcpy(Signature->data() + NameAt, Entries[Index].Name, Length);
        NameAt += static_cast<UINT>(Length);
    }
}

VOID
NineScBuildInputSignature(
    _In_ const NineScInputs* Inputs,
    _In_reads_opt_(ElementCount) const NineScElement* Elements,
    _In_ UINT ElementCount,
    _In_reads_(NineScStreamCount) const UINT* Frequencies,
    _Inout_ std::vector<D3D12_INPUT_ELEMENT_DESC>* Layout,
    _Out_ NineScBlob* Signature)
{
    std::vector<ScSignatureEntry> Entries;

    if (Inputs->Inputs.empty() || !Elements)
    {
        ScWriteSignature(Entries, Signature);
        return;
    }

    Layout->clear();
    Layout->reserve(ElementCount);
    for (UINT Index = 0; Index < ElementCount; Index += 1)
    {
        const NineScElement* Element = &Elements[Index];
        const NineScInput* Input = nullptr;
        ScSignatureEntry Entry = {};
        UINT Frequency;

        for (const NineScInput& Candidate : Inputs->Inputs)
        {
            if (NineScInputUsage(Candidate) == Element->Usage && NineScInputIndex(Candidate) == Element->UsageIndex)
            {
                Input = &Candidate;
                break;
            }
        }
        if (!Input)
            continue;

        Entry.Name = ScUsageName(Element->Usage);
        Entry.Parameter.SemanticIndex = Element->UsageIndex;
        Entry.Parameter.Register = NineScInputRegister(*Input);
        Entry.Parameter.Mask = 0xF;
        Entry.Parameter.NeverWritten = 0xF;
        ScElementFormat(Element->Type, &Entry.Element.Format, &Entry.Parameter.ComponentType);

        /* The high bit of a stream frequency marks instance data, the rest is the step rate */
        Frequency = Frequencies[Element->Stream];
        Entry.Element.SemanticName = Entry.Name;
        Entry.Element.SemanticIndex = Element->UsageIndex;
        Entry.Element.InputSlot = Element->Stream;
        Entry.Element.AlignedByteOffset = Element->Offset;
        if (Frequency & 0x80000000)
        {
            Entry.Element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
            Entry.Element.InstanceDataStepRate = Frequency & 0x7FFFFFFF;
        }
        else
        {
            Entry.Element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        }
        Entries.push_back(Entry);
    }

    std::sort(Entries.begin(), Entries.end(), ScRegisterBefore);
    for (const ScSignatureEntry& Entry : Entries)
        Layout->push_back(Entry.Element);
    ScWriteSignature(Entries, Signature);
}

VOID
NineScBuildOutputSignature(
    _In_ const NineScOutputs* Outputs,
    _Out_ NineScBlob* Signature)
{
    std::vector<ScSignatureEntry> Entries(Outputs->Count);

    for (UINT Index = 0; Index < Outputs->Count; Index += 1)
    {
        NineScOutput Output = Outputs->Outputs[Index];
        UINT Usage = Output & 0x1F;
        ScSignatureEntry* Entry = &Entries[Index];

        Entry->Name = ScOutputName(Usage);
        Entry->Parameter.SemanticIndex = (Output >> 5) & 0xF;
        Entry->Parameter.SystemValue = ScOutputSystemValue(Usage);
        Entry->Parameter.ComponentType = Usage == ScUsageFace ? ScComponentUint : ScComponentFloat;
        Entry->Parameter.Register = (Output >> 9) & 0xF;
        Entry->Parameter.Mask = static_cast<BYTE>((Output >> 13) & 0xF);
    }

    std::sort(Entries.begin(), Entries.end(), ScRegisterBefore);
    ScWriteSignature(Entries, Signature);
}

HRESULT
NineScPackageShader(
    _In_reads_bytes_(CodeSize) const VOID* Code,
    _In_ SIZE_T CodeSize,
    _In_ const NineScBlob* InputSignature,
    _In_ const NineScBlob* OutputSignature,
    _In_opt_ PFN_NINE_SC_SIGN Sign,
    _Out_ NineScBlob* Container)
{
    CDXBCBuilder Builder(false);
    UINT32 Size = 0;
    HRESULT hr;

    Container->clear();
    hr = Builder.AppendBlob(DXBC_InputSignature11_1, static_cast<UINT32>(InputSignature->size()),
                            InputSignature->data());
    if (SUCCEEDED(hr))
        hr = Builder.AppendBlob(DXBC_OutputSignature11_1, static_cast<UINT32>(OutputSignature->size()),
                                OutputSignature->data());
    if (SUCCEEDED(hr))
        hr = Builder.AppendBlob(DXBC_GenericShader, static_cast<UINT32>(CodeSize), Code);
    if (SUCCEEDED(hr))
        hr = Builder.GetFinalDXBC(nullptr, &Size);
    if (FAILED(hr) || Size == 0)
        return hr;

    Container->resize(Size);
    hr = Builder.GetFinalDXBC(Container->data(), &Size);
    if (FAILED(hr))
    {
        Container->clear();
        return hr;
    }

    if (Sign)
    {
        hr = Sign(Container->data(), Size);
    }
    else
    {
        RxDxbcHash(Container->data() + 20, Size - 20, Container->data() + 4);
        hr = S_OK;
    }
    if (FAILED(hr))
        Container->clear();
    return hr;
}
