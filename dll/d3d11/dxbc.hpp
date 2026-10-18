/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Reads compiled shader containers: the program and its signatures
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

#include <string>
#include <vector>

/* One signature element: what the driver gets, plus the semantic that names it */
struct RxSignatureElement
{
    std::string SemanticName;
    UINT SemanticIndex = 0;
    D3D11_1DDIARG_SIGNATURE_ENTRY2 Entry = {};
};

class RxShaderSignature
{
public:
    const std::vector<RxSignatureElement>&
    GetElements() const
    {
        return Elements;
    }

    /* The register a semantic lives in, matched without regard to case */
    _Ret_maybenull_
    const RxSignatureElement*
    Find(
        _In_z_ PCSTR SemanticName,
        _In_ UINT SemanticIndex) const;

    /* Entries laid out the way the DDI takes them */
    std::vector<D3D11_1DDIARG_SIGNATURE_ENTRY2>
    GetEntries() const;

    HRESULT
    Parse(
        _In_reads_bytes_(Size) const BYTE* Data,
        _In_ SIZE_T Size,
        _In_ UINT ElementSize,
        _In_ BOOLEAN HasStream,
        _In_ BOOLEAN HasMinPrecision);

private:
    std::vector<RxSignatureElement> Elements;
};

class RxShaderBytecode
{
public:
    HRESULT
    Parse(
        _In_reads_bytes_(Size) const VOID* Bytecode,
        _In_ SIZE_T Size);

    /* The token stream; the second token is its length in tokens */
    const UINT*
    GetProgram() const
    {
        return Program.data();
    }

    /* Program type from the version token: D3D10_SB_PIXEL_SHADER and so on; UINT_MAX without a program */
    UINT
    GetProgramType() const;

    const RxShaderSignature&
    GetInput() const
    {
        return Input;
    }

    const RxShaderSignature&
    GetOutput() const
    {
        return Output;
    }

    const RxShaderSignature&
    GetPatchConstant() const
    {
        return PatchConstant;
    }

private:
    std::vector<UINT> Program;
    RxShaderSignature Input;
    RxShaderSignature Output;
    RxShaderSignature PatchConstant;
};
