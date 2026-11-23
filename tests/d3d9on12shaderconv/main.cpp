/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compares the Direct3D 9 on 12 shader converter against the system driver's, in process
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * d3d9on12shaderconv [-fuzz <count>] [-only <name>] [-dump]
 *
 * The system d3d9on12.dll is mapped and its converter is called by address with the same arguments
 * the ReactX converter gets. Shaders come from D3DAssemble in d3dcompiler_47.dll. Every output is
 * compared: the result, the tokens, the inline constants, the buffer sizes, the counters and the
 * vertex inputs and outputs written back. Only the build of the system driver the address was taken
 * from is used.
 */

#include <windows.h>
#include <d3dcommon.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "../../dll/d3d9on12/shaderconv.hpp"
#include "corpus.hpp"

typedef HRESULT (WINAPI* PFN_SC_ASSEMBLE)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, UINT,
                                          ID3DBlob**, ID3DBlob**);
typedef HRESULT (*PFN_SC_CONVERT)(PVOID*, NineScArgs*);

/* The system driver build the converter address belongs to */
static constexpr DWORD ScSystemTimeStamp = 0x8FECDEF1;
static constexpr DWORD ScSystemImageSize = 0x196000;
static constexpr ULONG ScConvertOffset = 0x434D8;

/* The device's runtime version the converter is created with */
static constexpr UINT ScRuntimeVersion = 0x11001;

struct ScContext
{
    PFN_SC_ASSEMBLE Assemble;
    PFN_SC_CONVERT SystemConvert;
    PVOID SystemState[4];
    NineScTranslator* Translator;
    BOOL Dump;
    UINT Cases;
    UINT Failures;
    UINT Seed;
};

/* One full set of inputs and what the converter gave back */
struct ScRun
{
    HRESULT Result;
    NineScArgs Args;
    NineScInputs Inputs;
    NineScOutputs Outputs;
};

static
UINT
ScRandom(
    _Inout_ ScContext* Context)
{
    Context->Seed = Context->Seed * 1103515245 + 12345;
    return (Context->Seed >> 8) & 0xFFFFFF;
}

static
BOOL
ScLoad(
    _Out_ ScContext* Context)
{
    HMODULE Compiler;
    HMODULE System;
    PIMAGE_DOS_HEADER Dos;
    PIMAGE_NT_HEADERS Nt;

    ZeroMemory(Context, sizeof(*Context));
    Context->Seed = 1;

    Compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!Compiler)
    {
        printf("d3dcompiler_47.dll did not load\n");
        return FALSE;
    }
    Context->Assemble = reinterpret_cast<PFN_SC_ASSEMBLE>(GetProcAddress(Compiler, "D3DAssemble"));
    if (!Context->Assemble)
    {
        printf("d3dcompiler_47.dll has no D3DAssemble\n");
        return FALSE;
    }

    System = LoadLibraryExW(L"C:\\Windows\\System32\\d3d9on12.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!System)
    {
        printf("the system d3d9on12.dll did not load\n");
        return FALSE;
    }
    Dos = reinterpret_cast<PIMAGE_DOS_HEADER>(System);
    Nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<PBYTE>(System) + Dos->e_lfanew);
    if (Nt->FileHeader.TimeDateStamp != ScSystemTimeStamp || Nt->OptionalHeader.SizeOfImage != ScSystemImageSize)
    {
        printf("the system d3d9on12.dll is build %08lX, the converter address is for %08lX\n",
               Nt->FileHeader.TimeDateStamp, ScSystemTimeStamp);
        return FALSE;
    }
    Context->SystemConvert = reinterpret_cast<PFN_SC_CONVERT>(reinterpret_cast<PBYTE>(System) + ScConvertOffset);
    return TRUE;
}

/* Prints one instruction per line: the opcode and its tokens */
static
VOID
ScPrintTokens(
    _In_z_ PCSTR Label,
    _In_ const ScRun* Run)
{
    const UINT* Words = static_cast<const UINT*>(Run->Args.Tokens);
    SIZE_T Count = Run->Args.TokenSize / 4;
    SIZE_T Index = 2;

    printf("  %s:\n", Label);
    if (!Words || Count < 2)
    {
        printf("    (no tokens)\n");
        return;
    }

    printf("    version %08X length %u\n", Words[0], Words[1]);
    while (Index < Count)
    {
        UINT Opcode = Words[Index] & 0x7FF;
        SIZE_T Length = (Words[Index] >> 24) & 0x7F;

        if (Opcode == 0x35 || (Opcode == 0x6A && Length == 0))
            Length = Index + 1 < Count ? Words[Index + 1] : 1;
        if (Length == 0 || Index + Length > Count)
            Length = Count - Index;

        printf("    %3u:", Opcode);
        for (SIZE_T Word = 0; Word < Length; Word += 1)
            printf(" %08X", Words[Index + Word]);
        printf("\n");
        Index += Length;
    }
}

static
VOID
ScPrepare(
    _Out_ ScRun* Run,
    _In_ const ScCase* Case,
    _In_ const std::vector<DWORD>& Code,
    _In_ const NineScRasterStates* Raster,
    _In_ const NineScOutputs* PixelInputs)
{
    Run->Result = E_FAIL;
    Run->Args = NineScArgs();
    Run->Inputs = NineScInputs();
    ZeroMemory(&Run->Outputs, sizeof(Run->Outputs));

    Run->Inputs.Limit = 16;
    for (UINT Index = 0; Index < Case->InputCount; Index += 1)
    {
        Run->Inputs.Inputs.push_back(Case->Inputs[Index]);
        Run->Inputs.UsageMask |= 1u << NineScInputUsage(Case->Inputs[Index]);
    }

    Run->Args.PixelShader = Case->Pixel;
    Run->Args.Flags = Case->Flags;
    Run->Args.Version = ScRuntimeVersion;
    Run->Args.Raster = Raster;
    Run->Args.PixelInputs = Case->Pixel ? PixelInputs : NULL;
    Run->Args.VertexInputs = &Run->Inputs;
    Run->Args.Code = Code.data();
    Run->Args.CodeSize = Code.size() * sizeof(DWORD);
    Run->Args.VertexOutputs = &Run->Outputs;
}

/* Prints the first difference between the two runs, if any */
static
BOOL
ScCompare(
    _In_ const ScRun* System,
    _In_ const ScRun* ReactX)
{
    BOOL Same = TRUE;

    if (System->Result != ReactX->Result)
    {
        printf("  result %08lX, reactx %08lX\n", System->Result, ReactX->Result);
        Same = FALSE;
    }
    if (System->Args.TokenSize != ReactX->Args.TokenSize ||
        (System->Args.TokenSize &&
         memcmp(System->Args.Tokens, ReactX->Args.Tokens, System->Args.TokenSize) != 0))
    {
        const UINT* Left = static_cast<const UINT*>(System->Args.Tokens);
        const UINT* Right = static_cast<const UINT*>(ReactX->Args.Tokens);
        SIZE_T Count = (System->Args.TokenSize < ReactX->Args.TokenSize ? System->Args.TokenSize
                                                                         : ReactX->Args.TokenSize) / 4;
        SIZE_T Index = 0;

        while (Index < Count && Left[Index] == Right[Index])
            Index += 1;
        printf("  tokens differ at dword %Iu: size %Iu, reactx %Iu\n", Index, System->Args.TokenSize,
               ReactX->Args.TokenSize);
        Same = FALSE;
    }
    for (UINT Buffer = 0; Buffer < NineScBufferCount; Buffer += 1)
    {
        const std::vector<NineScConstant>& Left = System->Args.Constants[Buffer];
        const std::vector<NineScConstant>& Right = ReactX->Args.Constants[Buffer];
        BOOL Differs = Left.size() != Right.size();

        /*
         * The system converter fills only the register and the value of a boolean constant; the other
         * three words are whatever its stack held, so they are not compared.
         */
        for (SIZE_T Index = 0; !Differs && Index < Left.size(); Index += 1)
        {
            if (Buffer == NineScBooleanBuffer)
                Differs = Left[Index].Offset != Right[Index].Offset || Left[Index].Value[0] != Right[Index].Value[0];
            else
                Differs = memcmp(&Left[Index], &Right[Index], sizeof(Left[Index])) != 0;
        }

        if (Differs)
        {
            printf("  constants %u differ: %Iu, reactx %Iu\n", Buffer, Left.size(), Right.size());
            for (const NineScConstant& Constant : Left)
                printf("    system %u: %08X %08X %08X %08X\n", Constant.Offset, Constant.Value[0], Constant.Value[1],
                       Constant.Value[2], Constant.Value[3]);
            for (const NineScConstant& Constant : Right)
                printf("    reactx %u: %08X %08X %08X %08X\n", Constant.Offset, Constant.Value[0], Constant.Value[1],
                       Constant.Value[2], Constant.Value[3]);
            Same = FALSE;
        }
        if (System->Args.BufferSizes[Buffer] != ReactX->Args.BufferSizes[Buffer])
        {
            printf("  buffer %u size %u, reactx %u\n", Buffer, System->Args.BufferSizes[Buffer],
                   ReactX->Args.BufferSizes[Buffer]);
            Same = FALSE;
        }
    }
    if (System->Args.PixelFlag != ReactX->Args.PixelFlag || System->Args.Emitted != ReactX->Args.Emitted ||
        System->Args.Patched != ReactX->Args.Patched)
    {
        printf("  flag %u counters %u %u, reactx %u %u %u\n", System->Args.PixelFlag, System->Args.Emitted,
               System->Args.Patched, ReactX->Args.PixelFlag, ReactX->Args.Emitted, ReactX->Args.Patched);
        Same = FALSE;
    }
    if (System->Inputs.UsageMask != ReactX->Inputs.UsageMask || System->Inputs.Limit != ReactX->Inputs.Limit ||
        System->Inputs.Inputs != ReactX->Inputs.Inputs)
    {
        printf("  vertex inputs differ: mask %08X count %Iu, reactx %08X %Iu\n", System->Inputs.UsageMask,
               System->Inputs.Inputs.size(), ReactX->Inputs.UsageMask, ReactX->Inputs.Inputs.size());
        Same = FALSE;
    }
    /* The indexed mask is a 45 bit field; the system leaves the bits above it as its heap held them */
    if (System->Outputs.Flags != ReactX->Outputs.Flags || System->Outputs.Count != ReactX->Outputs.Count ||
        ((System->Outputs.Indexed ^ ReactX->Outputs.Indexed) & ((1ull << 45) - 1)) != 0 ||
        memcmp(System->Outputs.Outputs, ReactX->Outputs.Outputs, sizeof(System->Outputs.Outputs)) != 0)
    {
        printf("  vertex outputs differ: flags %08X indexed %llX count %u, reactx %08X %llX %u\n",
               System->Outputs.Flags, System->Outputs.Indexed, System->Outputs.Count, ReactX->Outputs.Flags,
               ReactX->Outputs.Indexed, ReactX->Outputs.Count);
        for (UINT Index = 0; Index < NineScMaxOutputs; Index += 1)
        {
            if (System->Outputs.Outputs[Index] != ReactX->Outputs.Outputs[Index])
                printf("    output %u: %08X, reactx %08X\n", Index, System->Outputs.Outputs[Index],
                       ReactX->Outputs.Outputs[Index]);
        }
        Same = FALSE;
    }
    return Same;
}

static
VOID
ScRunCase(
    _Inout_ ScContext* Context,
    _In_ const ScCase* Case,
    _In_ const std::vector<DWORD>& Code,
    _In_ const NineScRasterStates* Raster,
    _In_ const NineScOutputs* PixelInputs,
    _In_z_ PCSTR Label)
{
    static ScRun System;
    static ScRun ReactX;

    ScPrepare(&System, Case, Code, Raster, PixelInputs);
    ScPrepare(&ReactX, Case, Code, Raster, PixelInputs);

    System.Result = Context->SystemConvert(Context->SystemState, &System.Args);
    try
    {
        ReactX.Result = NineScConvertShader(&Context->Translator, &ReactX.Args);
    }
    catch (...)
    {
        ReactX.Result = E_UNEXPECTED;
    }

    Context->Cases += 1;
    if (ScCompare(&System, &ReactX))
    {
        if (Context->Dump)
        {
            printf("%s%s: same\n", Case->Name, Label);
            ScPrintTokens("tokens", &System);
        }
    }
    else
    {
        printf("%s%s: differs\n", Case->Name, Label);
        ScPrintTokens("system", &System);
        ScPrintTokens("reactx", &ReactX);
        Context->Failures += 1;
    }

    /* Each converter allocated its own copy; both come from the process heap */
    free(System.Args.Tokens);
    free(ReactX.Args.Tokens);
}

static
BOOL
ScAssemble(
    _In_ ScContext* Context,
    _In_ const ScCase* Case,
    _Out_ std::vector<DWORD>* Code)
{
    ID3DBlob* Shader = NULL;
    ID3DBlob* Errors = NULL;
    HRESULT hr;

    hr = Context->Assemble(Case->Source, strlen(Case->Source), Case->Name, NULL, NULL, 0, &Shader, &Errors);
    if (FAILED(hr))
    {
        printf("%s: does not assemble %08lX %s\n", Case->Name, hr,
               Errors ? static_cast<PCSTR>(Errors->GetBufferPointer()) : "");
        if (Errors)
            Errors->Release();
        return FALSE;
    }
    if (Errors)
        Errors->Release();

    Code->assign(static_cast<const DWORD*>(Shader->GetBufferPointer()),
                 static_cast<const DWORD*>(Shader->GetBufferPointer()) + Shader->GetBufferSize() / 4);
    Shader->Release();
    return TRUE;
}

/* The pixel shader inputs a typical vertex shader leaves: position, two colors and eight coordinates */
static
VOID
ScDefaultPixelInputs(
    _Out_ NineScOutputs* Outputs)
{
    ZeroMemory(Outputs, sizeof(*Outputs));
    Outputs->Outputs[Outputs->Count++] = 0 | (0 << 9) | (0xF << 13);
    Outputs->Outputs[Outputs->Count++] = 10 | (0 << 5) | (1 << 9) | (0xF << 13);
    Outputs->Outputs[Outputs->Count++] = 10 | (1 << 5) | (2 << 9) | (0xF << 13);
    for (UINT Index = 0; Index < 8; Index += 1)
        Outputs->Outputs[Outputs->Count++] = 5 | (Index << 5) | ((3 + Index) << 9) | (0xF << 13);
}

int
__cdecl
main(
    _In_ int argc,
    _In_reads_(argc) char** argv)
{
    ScContext Context;
    NineScRasterStates Raster;
    NineScOutputs PixelInputs;
    PCSTR Only = NULL;
    UINT Fuzz = 0;
    BOOL Dump = FALSE;

    for (int Index = 1; Index < argc; Index += 1)
    {
        if (strcmp(argv[Index], "-fuzz") == 0 && Index + 1 < argc)
            Fuzz = strtoul(argv[++Index], NULL, 0);
        else if (strcmp(argv[Index], "-only") == 0 && Index + 1 < argc)
            Only = argv[++Index];
        else if (strcmp(argv[Index], "-dump") == 0)
            Dump = TRUE;
    }

    if (!ScLoad(&Context))
        return 1;
    Context.Dump = Dump;

    ZeroMemory(&Raster, sizeof(Raster));
    for (UINT Index = 0; Index < _countof(Raster.Samplers); Index += 1)
        Raster.Samplers[Index] = 1;
    ScDefaultPixelInputs(&PixelInputs);

    for (UINT Index = 0; Index < _countof(ScCorpus); Index += 1)
    {
        const ScCase* Case = &ScCorpus[Index];
        std::vector<DWORD> Code;

        if (Only && strcmp(Only, Case->Name) != 0)
            continue;
        if (!ScAssemble(&Context, Case, &Code))
        {
            Context.Failures += 1;
            continue;
        }

        ScRunCase(&Context, Case, Code, &Raster, &PixelInputs, "");

        for (UINT Round = 0; Round < Fuzz; Round += 1)
        {
            NineScRasterStates Random;
            char Label[32];

            for (UINT Byte = 0; Byte < sizeof(Random); Byte += 1)
                reinterpret_cast<PBYTE>(&Random)[Byte] = static_cast<BYTE>(ScRandom(&Context));
            sprintf_s(Label, " raster %u", Round);
            ScRunCase(&Context, Case, Code, &Random, &PixelInputs, Label);
        }
    }

    printf("%u cases, %u differ\n", Context.Cases, Context.Failures);
    NineScDestroyTranslator(Context.Translator);
    return Context.Failures ? 1 : 0;
}
