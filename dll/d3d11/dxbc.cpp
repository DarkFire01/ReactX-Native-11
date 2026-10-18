/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Reads compiled shader containers: the program and its signatures
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxbc.hpp"

constexpr UINT RxFourCc(char A, char B, char C, char D)
{
    return (UINT)(BYTE)A | ((UINT)(BYTE)B << 8) | ((UINT)(BYTE)C << 16) | ((UINT)(BYTE)D << 24);
}

/* Container layout: magic, a 16 byte hash, a version of 1, total size, chunk count, chunk offsets */
constexpr UINT RxContainerMagic = RxFourCc('D', 'X', 'B', 'C');
constexpr SIZE_T RxContainerHeaderSize = 32;

/* Signature element sizes for the three chunk generations */
constexpr UINT RxSignatureElementSize = 24;
constexpr UINT RxSignatureStreamElementSize = 28;
constexpr UINT RxSignaturePrecisionElementSize = 32;

/* System values past the ones the token format names only describe pixel shader outputs */
constexpr UINT RxFirstOutputOnlyName = 64;

static
BOOLEAN
RxReadUint(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ SIZE_T Size,
    _In_ SIZE_T Offset,
    _Out_ UINT* Value)
{
    if (Offset > Size || Size - Offset < sizeof(UINT))
        return FALSE;

    memcpy(Value, Data + Offset, sizeof(UINT));
    return TRUE;
}

_Ret_maybenull_
const RxSignatureElement*
RxShaderSignature::Find(
    _In_z_ PCSTR SemanticName,
    _In_ UINT SemanticIndex) const
{
    for (const RxSignatureElement& Element : Elements)
    {
        if (Element.SemanticIndex == SemanticIndex && _stricmp(Element.SemanticName.c_str(), SemanticName) == 0)
            return &Element;
    }

    return nullptr;
}

std::vector<D3D11_1DDIARG_SIGNATURE_ENTRY2>
RxShaderSignature::GetEntries() const
{
    std::vector<D3D11_1DDIARG_SIGNATURE_ENTRY2> Entries;

    Entries.reserve(Elements.size());
    for (const RxSignatureElement& Element : Elements)
        Entries.push_back(Element.Entry);

    return Entries;
}

HRESULT
RxShaderSignature::Parse(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ SIZE_T Size,
    _In_ UINT ElementSize,
    _In_ BOOLEAN HasStream,
    _In_ BOOLEAN HasMinPrecision)
{
    RxSignatureElement Element;
    SIZE_T Offset;
    UINT Count;
    UINT First;
    UINT Index;
    UINT Stream = 0;
    UINT NameOffset;
    UINT SystemValue;
    UINT ComponentType;
    UINT MinPrecision = 0;
    SIZE_T NameEnd;

    if (!RxReadUint(Data, Size, 0, &Count) || !RxReadUint(Data, Size, 4, &First))
        return E_INVALIDARG;
    if (First > Size || (Size - First) / ElementSize < Count)
        return E_INVALIDARG;

    Elements.clear();
    for (Index = 0; Index < Count; Index++)
    {
        Offset = First + (SIZE_T)Index * ElementSize;
        if (HasStream)
        {
            RxReadUint(Data, Size, Offset, &Stream);
            Offset += sizeof(UINT);
        }

        RxReadUint(Data, Size, Offset, &NameOffset);
        RxReadUint(Data, Size, Offset + 4, &Element.SemanticIndex);
        RxReadUint(Data, Size, Offset + 8, &SystemValue);
        RxReadUint(Data, Size, Offset + 12, &ComponentType);
        RxReadUint(Data, Size, Offset + 16, &Element.Entry.Register);
        Element.Entry.Mask = Data[Offset + 20] & 0xF;
        if (HasMinPrecision)
            RxReadUint(Data, Size, Offset + 24, &MinPrecision);

        /* Names are NUL terminated strings inside the chunk */
        if (NameOffset >= Size)
            return E_INVALIDARG;
        NameEnd = NameOffset;
        while (NameEnd < Size && Data[NameEnd] != 0)
            NameEnd++;
        if (NameEnd == Size)
            return E_INVALIDARG;

        Element.SemanticName.assign(reinterpret_cast<const char*>(Data + NameOffset), NameEnd - NameOffset);
        Element.Entry.SystemValue = (SystemValue >= RxFirstOutputOnlyName) ? D3D10_SB_NAME_UNDEFINED
                                                                         : (D3D10_SB_NAME)SystemValue;
        Element.Entry.Stream = (BYTE)Stream;
        Element.Entry.RegisterComponentType = (D3D10_SB_REGISTER_COMPONENT_TYPE)ComponentType;
        Element.Entry.MinPrecision = (D3D11_SB_OPERAND_MIN_PRECISION)MinPrecision;
        Elements.push_back(Element);
    }

    return S_OK;
}

HRESULT
RxShaderBytecode::Parse(
    _In_reads_bytes_(Size) const VOID* Bytecode,
    _In_ SIZE_T Size)
{
    const BYTE* Data = static_cast<const BYTE*>(Bytecode);
    UINT Magic;
    UINT TotalSize;
    UINT ChunkCount;
    UINT Chunk;
    UINT ChunkOffset;
    UINT ChunkType;
    UINT ChunkSize;
    UINT Tokens;
    const BYTE* Body;
    HRESULT hr = S_OK;

    if (Data == nullptr || Size < RxContainerHeaderSize || !RxReadUint(Data, Size, 0, &Magic) ||
        Magic != RxContainerMagic || !RxReadUint(Data, Size, 24, &TotalSize) || TotalSize > Size ||
        !RxReadUint(Data, Size, 28, &ChunkCount))
    {
        return E_INVALIDARG;
    }

    Size = TotalSize;
    for (Chunk = 0; Chunk < ChunkCount && SUCCEEDED(hr); Chunk++)
    {
        if (!RxReadUint(Data, Size, RxContainerHeaderSize + (SIZE_T)Chunk * 4, &ChunkOffset) ||
            !RxReadUint(Data, Size, ChunkOffset, &ChunkType) || !RxReadUint(Data, Size, ChunkOffset + 4, &ChunkSize) ||
            ChunkSize > Size - ChunkOffset - 8)
        {
            return E_INVALIDARG;
        }

        Body = Data + ChunkOffset + 8;
        switch (ChunkType)
        {
            case RxFourCc('S', 'H', 'D', 'R'):
            case RxFourCc('S', 'H', 'E', 'X'):
                /* The program carries its own length; it must fit inside the chunk */
                if (!RxReadUint(Body, ChunkSize, 4, &Tokens) || Tokens < 2 || Tokens > ChunkSize / sizeof(UINT))
                    return E_INVALIDARG;
                Program.assign(reinterpret_cast<const UINT*>(Body), reinterpret_cast<const UINT*>(Body) + Tokens);
                break;

            case RxFourCc('I', 'S', 'G', 'N'):
                hr = Input.Parse(Body, ChunkSize, RxSignatureElementSize, FALSE, FALSE);
                break;
            case RxFourCc('I', 'S', 'G', '1'):
                hr = Input.Parse(Body, ChunkSize, RxSignaturePrecisionElementSize, TRUE, TRUE);
                break;
            case RxFourCc('O', 'S', 'G', 'N'):
                hr = Output.Parse(Body, ChunkSize, RxSignatureElementSize, FALSE, FALSE);
                break;
            case RxFourCc('O', 'S', 'G', '5'):
                hr = Output.Parse(Body, ChunkSize, RxSignatureStreamElementSize, TRUE, FALSE);
                break;
            case RxFourCc('O', 'S', 'G', '1'):
                hr = Output.Parse(Body, ChunkSize, RxSignaturePrecisionElementSize, TRUE, TRUE);
                break;
            case RxFourCc('P', 'C', 'S', 'G'):
                hr = PatchConstant.Parse(Body, ChunkSize, RxSignatureElementSize, FALSE, FALSE);
                break;
            case RxFourCc('P', 'S', 'G', '1'):
                hr = PatchConstant.Parse(Body, ChunkSize, RxSignaturePrecisionElementSize, TRUE, TRUE);
                break;
            default:
                break;
        }
    }

    /* A container may hold only signatures, as input layouts are often given */
    return hr;
}

UINT
RxShaderBytecode::GetProgramType() const
{
    if (Program.empty())
        return UINT_MAX;

    return DECODE_D3D10_SB_TOKENIZED_PROGRAM_TYPE(Program[0]);
}
