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
typedef HRESULT (*PFN_SC_CONVERT_TL)(PVOID*, NineScTlArgs*);

/* The system driver build the converter addresses belong to */
static constexpr DWORD ScSystemTimeStamp = 0x8FECDEF1;
static constexpr DWORD ScSystemImageSize = 0x196000;
static constexpr ULONG ScConvertOffset = 0x434D8;
static constexpr ULONG ScConvertTlOffset = 0x789B0;

/* A runtime version below 9, for the paths older runtimes take */
static constexpr UINT ScOldRuntimeVersion = 8;

/* The device's runtime version the converter is created with */
static constexpr UINT ScRuntimeVersion = 0x11001;

struct ScContext
{
    PFN_SC_ASSEMBLE Assemble;
    PFN_SC_CONVERT SystemConvert;
    PFN_SC_CONVERT_TL SystemConvertTl;
    PVOID SystemState[4];
    PVOID SystemOldState[4];
    NineScTranslator* Translator;
    NineScTranslator* OldTranslator;
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
    Context->SystemConvertTl = reinterpret_cast<PFN_SC_CONVERT_TL>(reinterpret_cast<PBYTE>(System) + ScConvertTlOffset);
    return TRUE;
}

/* Prints one instruction per line: the opcode and its tokens */
static
VOID
ScPrintTokens(
    _In_z_ PCSTR Label,
    _In_opt_ const VOID* Tokens,
    _In_ SIZE_T Size)
{
    const UINT* Words = static_cast<const UINT*>(Tokens);
    SIZE_T Count = Size / 4;
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
            ScPrintTokens("tokens", System.Args.Tokens, System.Args.TokenSize);
        }
    }
    else
    {
        printf("%s%s: differs\n", Case->Name, Label);
        ScPrintTokens("system", System.Args.Tokens, System.Args.TokenSize);
        ScPrintTokens("reactx", ReactX.Args.Tokens, ReactX.Args.TokenSize);
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

/* Adds one vertex shader output with the flag its usage sets */
static
VOID
ScAddLinked(
    _Inout_ NineScOutputs* Outputs,
    _In_ UINT Usage,
    _In_ UINT Index,
    _In_ UINT Register,
    _In_ UINT Mask)
{
    Outputs->Outputs[Outputs->Count++] = Usage | (Index << 5) | (Register << 9) | (Mask << 13);
    switch (Usage)
    {
        case 0:
            if (Index == 0)
                Outputs->Flags |= 1;
            break;
        case 5:
            Outputs->Flags |= 8u << Index;
            break;
        case 10:
            if (Index <= 1)
                Outputs->Flags |= 2u << Index;
            break;
        case 11:
            if (Index == 0)
                Outputs->Flags |= 0x80000;
            break;
    }
}

/* Number of vertex shader output sets the pixel shaders are linked against */
static constexpr UINT ScPixelInputSets = 4;

/*
 * The vertex shader outputs a pixel shader is linked against: the usual position, two colors and eight
 * coordinates, the same with fog, a shuffled set with partial masks, and a sparse one.
 */
static
VOID
ScPixelInputs(
    _Out_ NineScOutputs* Outputs,
    _In_ UINT Set)
{
    ZeroMemory(Outputs, sizeof(*Outputs));
    switch (Set)
    {
        case 0:
        case 1:
            ScAddLinked(Outputs, 0, 0, 0, 0xF);
            ScAddLinked(Outputs, 10, 0, 1, 0xF);
            ScAddLinked(Outputs, 10, 1, 2, 0xF);
            for (UINT Index = 0; Index < 8; Index += 1)
                ScAddLinked(Outputs, 5, Index, 3 + Index, 0xF);
            if (Set == 1)
                ScAddLinked(Outputs, 11, 0, 11, 0x1);
            break;

        case 2:
            ScAddLinked(Outputs, 0, 0, 0, 0xF);
            ScAddLinked(Outputs, 11, 0, 1, 0x1);
            ScAddLinked(Outputs, 10, 0, 2, 0xF);
            for (UINT Index = 0; Index < 8; Index += 1)
                ScAddLinked(Outputs, 5, Index, 10 - Index, Index & 1 ? 0x3 : 0xF);
            break;

        default:
            ScAddLinked(Outputs, 5, 0, 0, 0x3);
            ScAddLinked(Outputs, 0, 0, 1, 0xF);
            ScAddLinked(Outputs, 10, 0, 2, 0x7);
            ScAddLinked(Outputs, 10, 1, 3, 0xF);
            ScAddLinked(Outputs, 5, 2, 4, 0x7);
            break;
    }
}

/* One pretransformed vertex shader run: the declaration it is built from and what came back */
struct ScTlRun
{
    HRESULT Result;
    NineScTlArgs Args;
    NineScInputs Inputs;
    NineScOutputs Outputs;
};

static
VOID
ScPrepareTl(
    _Out_ ScTlRun* Run,
    _In_ const NineScInputs* Inputs,
    _In_ UINT Version,
    _In_ UINT Flags)
{
    Run->Result = E_FAIL;
    ZeroMemory(&Run->Args, sizeof(Run->Args));
    Run->Inputs = *Inputs;
    memset(&Run->Outputs, 0xA5, sizeof(Run->Outputs));
    Run->Args.Version = Version;
    Run->Args.Flags = Flags;
    Run->Args.Inputs = &Run->Inputs;
    Run->Args.Outputs = &Run->Outputs;
}

static
BOOL
ScCompareTl(
    _In_ const ScTlRun* System,
    _In_ const ScTlRun* ReactX)
{
    BOOL Same = TRUE;

    if (System->Result != ReactX->Result)
    {
        printf("  result %08lX, reactx %08lX\n", System->Result, ReactX->Result);
        Same = FALSE;
    }
    if (System->Args.TokenSize != ReactX->Args.TokenSize ||
        (System->Args.TokenSize && memcmp(System->Args.Tokens, ReactX->Args.Tokens, System->Args.TokenSize) != 0))
    {
        printf("  tokens differ: size %Iu, reactx %Iu\n", System->Args.TokenSize, ReactX->Args.TokenSize);
        Same = FALSE;
    }
    if (System->Args.Emitted != ReactX->Args.Emitted || System->Args.Patched != ReactX->Args.Patched)
    {
        printf("  counters %u %u, reactx %u %u\n", System->Args.Emitted, System->Args.Patched, ReactX->Args.Emitted,
               ReactX->Args.Patched);
        Same = FALSE;
    }
    if (memcmp(&System->Outputs, &ReactX->Outputs, sizeof(System->Outputs)) != 0)
    {
        printf("  outputs differ: flags %08X indexed %llX count %u, reactx %08X %llX %u\n", System->Outputs.Flags,
               System->Outputs.Indexed, System->Outputs.Count, ReactX->Outputs.Flags, ReactX->Outputs.Indexed,
               ReactX->Outputs.Count);
        Same = FALSE;
    }
    return Same;
}

static
VOID
ScRunTl(
    _Inout_ ScContext* Context,
    _In_ const NineScInputs* Inputs,
    _In_ BOOL Old,
    _In_ UINT Flags,
    _In_z_ PCSTR Label)
{
    static ScTlRun System;
    static ScTlRun ReactX;
    UINT Version = Old ? ScOldRuntimeVersion : ScRuntimeVersion;

    ScPrepareTl(&System, Inputs, Version, Flags);
    ScPrepareTl(&ReactX, Inputs, Version, Flags);

    System.Result = Context->SystemConvertTl(Old ? Context->SystemOldState : Context->SystemState, &System.Args);
    try
    {
        ReactX.Result = NineScConvertTlShader(Old ? &Context->OldTranslator : &Context->Translator, &ReactX.Args);
    }
    catch (...)
    {
        ReactX.Result = E_UNEXPECTED;
    }

    Context->Cases += 1;
    if (ScCompareTl(&System, &ReactX))
    {
        if (Context->Dump)
        {
            printf("%s: same\n", Label);
            ScPrintTokens("tokens", System.Args.Tokens, System.Args.TokenSize);
        }
    }
    else
    {
        printf("%s: differs\n", Label);
        ScPrintTokens("system", System.Args.Tokens, System.Args.TokenSize);
        ScPrintTokens("reactx", ReactX.Args.Tokens, ReactX.Args.TokenSize);
        Context->Failures += 1;
    }
    free(System.Args.Tokens);
    free(ReactX.Args.Tokens);
}

static
VOID
ScAddTlInput(
    _Inout_ NineScInputs* Inputs,
    _In_ UINT Usage,
    _In_ UINT Index,
    _In_ UINT Register,
    _In_ UINT Flag)
{
    Inputs->Inputs.push_back(static_cast<NineScInput>(Usage | (Index << 4) | (Register << 8) | (Flag << 12)));
    Inputs->UsageMask |= 1u << Usage;
}

/* Declarations of pretransformed and plain vertices, then random ones */
static
VOID
ScRunTlCases(
    _Inout_ ScContext* Context,
    _In_ UINT Fuzz)
{
    NineScInputs Inputs;
    char Label[48];

    for (UINT Set = 0; Set < 4; Set += 1)
    {
        Inputs = NineScInputs();
        Inputs.Limit = 16;
        switch (Set)
        {
            case 0:
                /* XYZRHW, diffuse, specular and two coordinates */
                ScAddTlInput(&Inputs, 0, 0, 0, 1);
                ScAddTlInput(&Inputs, 10, 0, 1, 0);
                ScAddTlInput(&Inputs, 10, 1, 2, 0);
                ScAddTlInput(&Inputs, 5, 0, 3, 0);
                ScAddTlInput(&Inputs, 5, 1, 4, 0);
                break;
            case 1:
                /* The position not in screen space, fog and point size */
                ScAddTlInput(&Inputs, 0, 0, 0, 0);
                ScAddTlInput(&Inputs, 11, 0, 1, 0);
                ScAddTlInput(&Inputs, 4, 0, 2, 0);
                break;
            case 2:
                /* POSITIONT is left out, the second position is passed through */
                ScAddTlInput(&Inputs, 9, 0, 0, 0);
                ScAddTlInput(&Inputs, 0, 1, 1, 1);
                ScAddTlInput(&Inputs, 9, 1, 2, 0);
                ScAddTlInput(&Inputs, 10, 0, 3, 0);
                break;
            default:
                /* Nothing declared */
                break;
        }
        for (UINT Variant = 0; Variant < 4; Variant += 1)
        {
            sprintf_s(Label, "tl set %u%s%s", Set, Variant & 1 ? " old" : "", Variant & 2 ? " patched" : "");
            ScRunTl(Context, &Inputs, Variant & 1, Variant & 2 ? 1 : 0, Label);
        }
    }

    for (UINT Round = 0; Round < Fuzz * 8; Round += 1)
    {
        UINT Count = ScRandom(Context) % 17;
        UINT Used = 0;

        Inputs = NineScInputs();
        Inputs.Limit = 16;
        for (UINT Element = 0; Element < Count; Element += 1)
        {
            UINT Register = ScRandom(Context) % 16;

            /* Registers are unique within a declaration */
            while (Used & (1u << Register))
                Register = (Register + 1) % 16;
            Used |= 1u << Register;
            ScAddTlInput(&Inputs, ScRandom(Context) % 14, ScRandom(Context) % 4 ? 0 : ScRandom(Context) % 8, Register,
                         ScRandom(Context) & 1);
        }
        sprintf_s(Label, "tl random %u", Round);
        ScRunTl(Context, &Inputs, Round & 1, (Round >> 1) & 1, Label);
    }
}

typedef HRESULT (*PFN_SC_CREATE_GS)(PVOID*, NineScGsArgs*);

static constexpr ULONG ScCreateGsOffset = 0x78C08;

/* One geometry shader run */
struct ScGsRun
{
    HRESULT Result;
    NineScGsArgs Args;
    NineScOutputs Outputs;
};

static
BOOL
ScCompareGs(
    _In_ const ScGsRun* System,
    _In_ const ScGsRun* ReactX)
{
    BOOL Same = TRUE;

    if (System->Result != ReactX->Result)
    {
        printf("  result %08lX, reactx %08lX\n", System->Result, ReactX->Result);
        Same = FALSE;
    }
    if (System->Args.TokenSize != ReactX->Args.TokenSize ||
        (System->Args.TokenSize && memcmp(System->Args.Tokens, ReactX->Args.Tokens, System->Args.TokenSize) != 0))
    {
        printf("  tokens differ: size %Iu, reactx %Iu\n", System->Args.TokenSize, ReactX->Args.TokenSize);
        Same = FALSE;
    }
    /* The bits above the 45 bit indexed mask are left as the system's stack held them */
    if (System->Outputs.Flags != ReactX->Outputs.Flags || System->Outputs.Count != ReactX->Outputs.Count ||
        ((System->Outputs.Indexed ^ ReactX->Outputs.Indexed) & ((1ull << 45) - 1)) != 0 ||
        memcmp(System->Outputs.Outputs, ReactX->Outputs.Outputs, sizeof(System->Outputs.Outputs)) != 0)
    {
        printf("  outputs differ: flags %08X count %u, reactx %08X %u\n", System->Outputs.Flags, System->Outputs.Count,
               ReactX->Outputs.Flags, ReactX->Outputs.Count);
        Same = FALSE;
    }
    return Same;
}

/*
 * A triangle that is not expanded reads its third vertex's registers 13 to 15 from bytes the system
 * converter never wrote, so those cases are left out.
 */
static
BOOL
ScGsReadsUninitialized(
    _In_ const NineScOutputs* Vertex,
    _In_ const NineScRasterStates* Raster)
{
    UINT Words1 = Raster->Words[1];
    UINT Primitive = Words1 & 0x1C0000;
    BOOL Triangle = Primitive != 0x40000 && Primitive != 0x80000 && Primitive != 0xC0000;
    BOOL PointFill = (Words1 & 0xC000) == 0x4000 && (Words1 & 0x100000);

    if (!Triangle || PointFill)
        return FALSE;
    for (UINT Slot = 0; Slot < Vertex->Count; Slot += 1)
    {
        if ((Vertex->Outputs[Slot] & 0x1F) != 4 && ((Vertex->Outputs[Slot] >> 9) & 0xF) >= 13)
            return TRUE;
    }
    return FALSE;
}

static
VOID
ScRunGs(
    _Inout_ ScContext* Context,
    _In_ PFN_SC_CREATE_GS SystemCreate,
    _In_ const NineScOutputs* Vertex,
    _In_ const NineScRasterStates* Raster,
    _In_ BOOL Old,
    _In_ UINT Flags,
    _In_z_ PCSTR Label)
{
    static ScGsRun System;
    static ScGsRun ReactX;
    ScGsRun* Runs[2] = { &System, &ReactX };

    for (ScGsRun* Run : Runs)
    {
        Run->Result = E_FAIL;
        ZeroMemory(&Run->Args, sizeof(Run->Args));
        memset(&Run->Outputs, 0x5A, sizeof(Run->Outputs));
        Run->Args.Flags = Flags;
        Run->Args.Version = Old ? ScOldRuntimeVersion : ScRuntimeVersion;
        Run->Args.VertexOutputs = Vertex;
        Run->Args.Outputs = &Run->Outputs;
        Run->Args.Raster = Raster;
    }

    System.Result = SystemCreate(Old ? Context->SystemOldState : Context->SystemState, &System.Args);
    try
    {
        ReactX.Result = NineScCreateGeometryShader(Old ? Context->OldTranslator : Context->Translator, &ReactX.Args);
    }
    catch (...)
    {
        ReactX.Result = E_UNEXPECTED;
    }

    Context->Cases += 1;
    if (ScCompareGs(&System, &ReactX))
    {
        if (Context->Dump)
        {
            printf("%s: same\n", Label);
            ScPrintTokens("tokens", System.Args.Tokens, System.Args.TokenSize);
        }
    }
    else
    {
        printf("%s: differs\n", Label);
        ScPrintTokens("system", System.Args.Tokens, System.Args.TokenSize);
        ScPrintTokens("reactx", ReactX.Args.Tokens, ReactX.Args.TokenSize);
        Context->Failures += 1;
    }
    free(System.Args.Tokens);
    free(ReactX.Args.Tokens);
}

/* The vertex shader outputs a geometry shader reads: position, colors, coordinates, fog, size, clip */
static
VOID
ScGsVertexOutputs(
    _Inout_ ScContext* Context,
    _Out_ NineScOutputs* Outputs,
    _In_ UINT Set)
{
    ZeroMemory(Outputs, sizeof(*Outputs));
    if (Set < ScPixelInputSets)
    {
        ScPixelInputs(Outputs, Set);
        return;
    }

    ScAddLinked(Outputs, 0, 0, 0, 0xF);
    if (ScRandom(Context) & 1)
        ScAddLinked(Outputs, 10, 0, 1, 0xF);
    if (ScRandom(Context) & 1)
        ScAddLinked(Outputs, 10, 1, 2, ScRandom(Context) & 1 ? 0xF : 0x7);
    for (UINT Index = 0; Index < 8; Index += 1)
    {
        if (ScRandom(Context) % 3 == 0)
            ScAddLinked(Outputs, 5, Index, 3 + Index, 1 + ScRandom(Context) % 15);
    }
    if (ScRandom(Context) & 1)
        ScAddLinked(Outputs, 11, 0, 11, 0x1);
    if (ScRandom(Context) & 1)
    {
        ScAddLinked(Outputs, 4, 0, 11, 0x2);
        Outputs->Flags |= 0x100000;
    }
    if (ScRandom(Context) % 4 == 0)
    {
        ScAddLinked(Outputs, 16, 0, 13, 0xF);
        ScAddLinked(Outputs, 16, 1, 14, 0x3);
    }
}

/* Raster states that pick each geometry shader path, then random ones */
static
VOID
ScRunGsCases(
    _Inout_ ScContext* Context,
    _In_ UINT Fuzz)
{
    static const UINT Words1[] =
    {
        0x00040000, 0x40040000, 0x80040000, 0x00080000, 0x000C0000, 0x00000000, 0x00104000, 0x00114000,
        0x80104000, 0x00083F00, 0x00040300, 0x00100000,
    };
    PFN_SC_CREATE_GS SystemCreate =
        reinterpret_cast<PFN_SC_CREATE_GS>(reinterpret_cast<PBYTE>(Context->SystemConvert) - ScConvertOffset +
                                           ScCreateGsOffset);
    NineScOutputs Vertex;
    NineScRasterStates Raster;
    UINT Skipped = 0;
    char Label[48];

    for (UINT Set = 0; Set < ScPixelInputSets; Set += 1)
    {
        ScGsVertexOutputs(Context, &Vertex, Set);
        for (UINT Word = 0; Word < _countof(Words1); Word += 1)
        {
            for (UINT Variant = 0; Variant < 4; Variant += 1)
            {
                ZeroMemory(&Raster, sizeof(Raster));
                for (UINT Sampler = 0; Sampler < 16; Sampler += 1)
                    Raster.Samplers[Sampler] = Variant & 2 ? static_cast<BYTE>(1 | (Sampler << 2)) : 1;
                Raster.Words[1] = Words1[Word];
                Raster.Words[2] = Variant & 1 ? 0x400 : 0;
                if (ScGsReadsUninitialized(&Vertex, &Raster))
                {
                    Skipped += 1;
                    continue;
                }
                sprintf_s(Label, "gs set %u words %08X variant %u", Set, Words1[Word], Variant);
                ScRunGs(Context, SystemCreate, &Vertex, &Raster, Variant & 1, 0, Label);
            }
        }
    }

    for (UINT Round = 0; Round < Fuzz * 32; Round += 1)
    {
        ScGsVertexOutputs(Context, &Vertex, ScPixelInputSets + Round);
        for (UINT Byte = 0; Byte < sizeof(Raster); Byte += 1)
            reinterpret_cast<PBYTE>(&Raster)[Byte] = static_cast<BYTE>(ScRandom(Context));
        if (ScGsReadsUninitialized(&Vertex, &Raster))
        {
            Skipped += 1;
            continue;
        }
        sprintf_s(Label, "gs random %u", Round);
        ScRunGs(Context, SystemCreate, &Vertex, &Raster, Round & 1, (Round >> 1) & 1, Label);
    }
    printf("gs cases left out because the system reads uninitialized memory: %u\n", Skipped);
}

int
__cdecl
main(
    _In_ int argc,
    _In_reads_(argc) char** argv)
{
    ScContext Context;
    NineScRasterStates Raster;
    NineScOutputs Linked[ScPixelInputSets];
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
    for (UINT Set = 0; Set < ScPixelInputSets; Set += 1)
        ScPixelInputs(&Linked[Set], Set);

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

        ScRunCase(&Context, Case, Code, &Raster, &Linked[0], "");
        if (Case->Pixel)
        {
            for (UINT Set = 1; Set < ScPixelInputSets; Set += 1)
            {
                char Label[32];

                sprintf_s(Label, " inputs %u", Set);
                ScRunCase(&Context, Case, Code, &Raster, &Linked[Set], Label);
            }
        }

        for (UINT Round = 0; Round < Fuzz; Round += 1)
        {
            NineScRasterStates Random;
            char Label[32];

            for (UINT Byte = 0; Byte < sizeof(Random); Byte += 1)
                reinterpret_cast<PBYTE>(&Random)[Byte] = static_cast<BYTE>(ScRandom(&Context));
            sprintf_s(Label, " raster %u", Round);
            ScRunCase(&Context, Case, Code, &Random, &Linked[Round % ScPixelInputSets], Label);

        }
    }

    /* The pretransformed cases also make both translators, which the geometry shader needs */
    if (!Only || strcmp(Only, "tl") == 0 || strcmp(Only, "gs") == 0)
        ScRunTlCases(&Context, Fuzz);
    if (!Only || strcmp(Only, "gs") == 0)
        ScRunGsCases(&Context, Fuzz);

    printf("%u cases, %u differ\n", Context.Cases, Context.Failures);
    NineScDestroyTranslator(Context.Translator);
    NineScDestroyTranslator(Context.OldTranslator);
    return Context.Failures ? 1 : 0;
}
