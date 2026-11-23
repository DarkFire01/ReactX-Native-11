/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 9 shader bytecode to Direct3D 10 tokens: the converter's inputs and outputs
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <vector>

/*
 * The structures below keep the layout the system driver's converter uses, so the test can hand
 * the same arguments to both.
 */

/* A constant the shader defines itself: register offset in components and the four values */
struct NineScConstant
{
    UINT Offset;
    UINT Value[4];
};

/* Constant buffers the converter fills: floats, integers, booleans */
static constexpr UINT NineScFloatBuffer = 0;
static constexpr UINT NineScIntegerBuffer = 1;
static constexpr UINT NineScBooleanBuffer = 2;
static constexpr UINT NineScBufferCount = 3;

/*
 * One vertex shader input: usage in bits 0 to 3, usage index in 4 to 7, input register in 8 to 11,
 * a flag in bit 12 and the element conversion in bits 13 and 14.
 */
typedef USHORT NineScInput;

static
inline
UINT
NineScInputUsage(
    _In_ NineScInput Input)
{
    return Input & 0xF;
}

static
inline
UINT
NineScInputIndex(
    _In_ NineScInput Input)
{
    return (Input >> 4) & 0xF;
}

static
inline
UINT
NineScInputRegister(
    _In_ NineScInput Input)
{
    return (Input >> 8) & 0xF;
}

static
inline
UINT
NineScInputConversion(
    _In_ NineScInput Input)
{
    return (Input >> 13) & 3;
}

/* The vertex shader inputs: a mask of declared usages and the inputs themselves */
struct NineScInputs
{
    UINT UsageMask;
    std::vector<NineScInput> Inputs;
    UINT Limit;
};

/*
 * One vertex shader output: usage in bits 0 to 4, usage index in 5 to 8, output register in
 * 9 to 12 and the component mask in 13 to 16.
 */
typedef UINT NineScOutput;

static constexpr UINT NineScMaxOutputs = 45;

#pragma pack(push, 4)
struct NineScOutputs
{
    UINT Flags;
    UINT64 Indexed;
    NineScOutput Outputs[NineScMaxOutputs];
    UINT Count;
};
#pragma pack(pop)

static_assert(sizeof(NineScOutputs) == 196, "outputs keep the converter's layout");

/* Pipeline state a converted shader depends on */
struct NineScRasterStates
{
    BYTE Samplers[16];
    UINT Words[5];
};

static_assert(sizeof(NineScRasterStates) == 36, "raster states keep the converter's layout");

/* Everything one conversion takes and gives back */
struct NineScArgs
{
    BOOL PixelShader;
    UINT Flags;
    UINT Version;
    const NineScRasterStates* Raster;
    const NineScOutputs* PixelInputs;
    NineScInputs* VertexInputs;
    const DWORD* Code;
    SIZE_T CodeSize;
    NineScOutputs* VertexOutputs;
    PVOID Tokens;
    SIZE_T TokenSize;
    BOOLEAN PixelFlag;
    std::vector<NineScConstant> Constants[NineScBufferCount];
    UINT BufferSizes[NineScBufferCount];
    UINT Emitted;
    UINT Patched;
    std::vector<UINT> Extra;
};

static_assert(sizeof(NineScArgs) == 208, "arguments keep the converter's layout");

class NineScTranslator;

/* Converts one shader; Translator is created on first use and kept by the caller */
HRESULT
NineScConvertShader(
    _Inout_ NineScTranslator** Translator,
    _Inout_ NineScArgs* Args);

/* Frees a translator made by NineScConvertShader */
VOID
NineScDestroyTranslator(
    _In_opt_ NineScTranslator* Translator);
