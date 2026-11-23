/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Container level conversion: pick the program and signatures, assemble the output
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxilconv.hpp"

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

    DxcTrace(L"program translation is not implemented yet");
    return E_NOTIMPL;
}
