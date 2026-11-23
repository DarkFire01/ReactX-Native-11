/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Container level conversion: pick the program and signatures, assemble the output
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxilconv.hpp"
#include "translate.hpp"

/* The DXIL part: program header, bitcode, then zero fill to a 16 byte boundary */
static
VOID
DxcBuildProgramPart(
    _In_ const DXC_TRANSLATION* Translation,
    _Out_ std::vector<BYTE>& Part)
{
    ULONG Header[6];
    ULONG Bitcode = static_cast<ULONG>(Translation->Bitcode.size());

    Header[0] = Translation->Minor | (Translation->Major << 4) | (Translation->ProgramKind << 16);
    Header[1] = 6 + (Bitcode >> 2) + ((Bitcode & 3) != 0 ? 1 : 0);
    Header[2] = DXC_FOURCC('D', 'X', 'I', 'L');
    Header[3] = 0x100;
    Header[4] = 16;
    Header[5] = Bitcode;

    Part.assign(reinterpret_cast<const BYTE*>(Header), reinterpret_cast<const BYTE*>(Header) + sizeof(Header));
    Part.insert(Part.end(), Translation->Bitcode.begin(), Translation->Bitcode.end());
    while ((Part.size() & 0xF) != 0)
        Part.push_back(0);
}

/* Pipeline state validation data, the version 0 layout with no resources yet */
static
VOID
DxcBuildStatePart(
    _In_ const DXC_TRANSLATION* Translation,
    _Out_ std::vector<BYTE>& Part)
{
    ULONG Words[2] = { 0, 0xFFFFFFFF };
    ULONG InfoSize = 24;
    ULONG Resources = 0;

    Part.clear();
    Part.insert(Part.end(), reinterpret_cast<const BYTE*>(&InfoSize), reinterpret_cast<const BYTE*>(&InfoSize) + 4);
    Part.insert(Part.end(), Translation->StageInfo, Translation->StageInfo + sizeof(Translation->StageInfo));
    Part.insert(Part.end(), reinterpret_cast<const BYTE*>(Words), reinterpret_cast<const BYTE*>(Words) + sizeof(Words));
    Part.insert(Part.end(), reinterpret_cast<const BYTE*>(&Resources), reinterpret_cast<const BYTE*>(&Resources) + 4);
}

/**
 * Output order: program, state validation, the signatures copied as they came,
 * the root signature if any, then the feature flags.
 */
static
HRESULT
DxcAssembleContainer(
    _In_ const DXC_CONTAINER* Source,
    _In_ const DXC_TRANSLATION* Translation,
    _Out_ LPVOID* Dxil,
    _Out_ UINT32* DxilSize)
{
    static const ULONG CopiedCodes[] =
    {
        DXC_FOURCC('I', 'S', 'G', '1'),
        DXC_FOURCC('I', 'S', 'G', 'N'),
        DXC_FOURCC('O', 'S', 'G', '1'),
        DXC_FOURCC('O', 'S', 'G', '5'),
        DXC_FOURCC('O', 'S', 'G', 'N'),
        DXC_FOURCC('P', 'S', 'G', '1'),
        DXC_FOURCC('P', 'C', 'S', 'G'),
        DXC_FOURCC('R', 'T', 'S', '0'),
    };
    CDxcContainerWriter Writer;
    std::vector<BYTE> Part;
    DXC_PART Existing;
    BYTE* Out;
    ULONG Total;
    BOOLEAN Added;

    DxcBuildProgramPart(Translation, Part);
    Added = Writer.AddPart(DXC_FOURCC('D', 'X', 'I', 'L'), Part.data(), static_cast<ULONG>(Part.size()));

    DxcBuildStatePart(Translation, Part);
    Added = Added && Writer.AddPart(DXC_FOURCC('P', 'S', 'V', '0'), Part.data(), static_cast<ULONG>(Part.size()));

    for (ULONG Index = 0; Index < _countof(CopiedCodes); Index++)
    {
        if (DxcFindPart(Source, CopiedCodes[Index], &Existing))
            Added = Added && Writer.AddPart(Existing.Code, Existing.Data, Existing.Size);
    }

    if (DxcFindPart(Source, DXC_FOURCC('S', 'F', 'I', '0'), &Existing))
        Added = Added && Writer.AddPart(Existing.Code, Existing.Data, Existing.Size);
    else
        Added = Added && Writer.AddPart(DXC_FOURCC('S', 'F', 'I', '0'), &Translation->Features, sizeof(Translation->Features));

    if (!Added)
        return E_OUTOFMEMORY;

    Total = Writer.TotalSize();
    Out = static_cast<BYTE*>(CoTaskMemAlloc(Total));
    if (Out == nullptr)
        return E_OUTOFMEMORY;

    Writer.Write(Out);
    *Dxil = Out;
    *DxilSize = Total;
    return S_OK;
}

/* Looks up the first part present out of a preference list */
static
BOOLEAN
DxcFindAnyPart(
    _In_ const DXC_CONTAINER* Container,
    _In_reads_(Count) const ULONG* Codes,
    _In_ ULONG Count,
    _Out_ DXC_PART* Part)
{
    for (ULONG Index = 0; Index < Count; Index++)
    {
        if (DxcFindPart(Container, Codes[Index], Part))
            return TRUE;
    }

    return FALSE;
}

HRESULT
CDxbcConverter::ConvertContainer(
    _In_reads_bytes_(DxbcSize) const BYTE* Dxbc,
    _In_ UINT32 DxbcSize,
    _Out_ LPVOID* Dxil,
    _Out_ UINT32* DxilSize)
{
    static const ULONG ProgramCodes[] = { DXC_FOURCC('S', 'H', 'E', 'X'), DXC_FOURCC('S', 'H', 'D', 'R') };
    static const ULONG InputCodes[] = { DXC_FOURCC('I', 'S', 'G', '1'), DXC_FOURCC('I', 'S', 'G', 'N') };
    static const ULONG OutputCodes[] =
    {
        DXC_FOURCC('O', 'S', 'G', '1'),
        DXC_FOURCC('O', 'S', 'G', '5'),
        DXC_FOURCC('O', 'S', 'G', 'N'),
    };
    DXC_CONTAINER Container;
    DXC_PART Program;
    DXC_PART Inputs;
    DXC_PART Outputs;
    HRESULT hr;

    *Dxil = nullptr;
    *DxilSize = 0;

    hr = DxcOpenContainer(Dxbc, DxbcSize, &Container);
    if (FAILED(hr))
        return hr;

    if (!DxcFindAnyPart(&Container, ProgramCodes, _countof(ProgramCodes), &Program))
        return DXC_STATUS_MISSING_PART;

    if (!DxcFindAnyPart(&Container, InputCodes, _countof(InputCodes), &Inputs))
        return DXC_STATUS_MISSING_PART;

    if (!DxcFindAnyPart(&Container, OutputCodes, _countof(OutputCodes), &Outputs))
        return DXC_STATUS_MISSING_PART;

    std::vector<DXC_SIGNATURE_ELEMENT> InputElements;
    std::vector<DXC_SIGNATURE_ELEMENT> OutputElements;
    DXC_TRANSLATION Translation;

    hr = DxcReadSignature(&Inputs, Inputs.Code == InputCodes[0] ? 32 : 24, InputElements);
    if (SUCCEEDED(hr))
    {
        ULONG Stride = 24;

        if (Outputs.Code == OutputCodes[0])
            Stride = 32;
        else if (Outputs.Code == OutputCodes[1])
            Stride = 28;

        hr = DxcReadSignature(&Outputs, Stride, OutputElements);
    }
    if (FAILED(hr))
        return hr;

    hr = DxcTranslateProgram(reinterpret_cast<const ULONG*>(Program.Data),
                             Program.Size / 4,
                             InputElements,
                             OutputElements,
                             &Translation);
    if (FAILED(hr))
        return hr;

    return DxcAssembleContainer(&Container, &Translation, Dxil, DxilSize);
}
