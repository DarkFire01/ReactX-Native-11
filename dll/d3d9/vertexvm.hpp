/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Vertex shader interpreter of the software vertex pipeline
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d9.h>

/* Register counts of one register file */
static constexpr UINT D9VmInputCount = 16;
static constexpr UINT D9VmTempCount = 32;
static constexpr UINT D9VmOutputCount = 16;
static constexpr UINT D9VmSamplerCount = 4;

/* Instructions and labels a compiled program can hold */
static constexpr UINT D9VmLabelCount = 2048;

/* Nesting the machine tracks: loops beyond the deepest reuse its slot, calls beyond the deepest are dropped */
static constexpr UINT D9VmLoopDepth = 4;
static constexpr UINT D9VmCallDepth = 8;

/* One register; instructions read and write it as floats or as raw bits */
union D9VmValue
{
    FLOAT Float;
    DWORD Bits;
    INT Int;
};

typedef D9VmValue D9VmVector[4];

/* What one run reads and writes besides the constants; it persists between runs like the system's does */
struct D9VmRegisters
{
    D9VmVector Inputs[D9VmInputCount];
    D9VmVector Temps[D9VmTempCount];
    D9VmVector Address;
    D9VmVector Position;
    D9VmVector Fog;
    D9VmVector PointSize;
    D9VmVector Colors[2];
    D9VmVector Outputs[D9VmOutputCount];
    D9VmVector Predicate;
    D9VmVector Samplers[D9VmSamplerCount];
};

/* Looks up a texel for texldl: the sampler number, the coordinates and the level of detail */
typedef VOID (*D9VmSampleRoutine)(
    _In_opt_ VOID* Context,
    _In_ UINT Sampler,
    _In_reads_(4) const FLOAT* Coordinates,
    _In_ FLOAT Lod,
    _Out_writes_(4) D9VmValue* Texel);

/* A register a dcl instruction declares, with its usage and write mask */
struct D9VmDeclared
{
    BYTE Register;
    BYTE Usage;
    BYTE Index;
    BYTE Mask;
};

/* Rasterizer and color outputs a version 1 or 2 shader writes */
static constexpr DWORD D9VmWritesSpecular = 0x1;
static constexpr DWORD D9VmWritesDiffuse = 0x2;
static constexpr DWORD D9VmWritesPointSize = 0x8;
static constexpr DWORD D9VmWritesFog = 0x10;

/* Declarations and definitions one program can hold */
static constexpr UINT D9VmDeclaredLimit = 32;
static constexpr UINT D9VmDefinitionLimit = 512;

/* One instruction of a compiled program: where its tokens start and, for flow control, where it goes */
struct D9VmStep
{
    DWORD Opcode;
    UINT Operands;
    UINT Target;
};

/* A vertex shader ready to interpret: its tokens, its instructions and where each label starts */
class D9VmProgram
{
public:
    D9VmProgram() = default;

    ~D9VmProgram();

    D9VmProgram(const D9VmProgram&) = delete;
    D9VmProgram& operator=(const D9VmProgram&) = delete;

    HRESULT
    Compile(
        _In_reads_(Count) const DWORD* Tokens,
        _In_ UINT Count);

    DWORD Version = 0;
    DWORD* Code = nullptr;
    D9VmStep* Steps = nullptr;
    UINT StepCount = 0;
    UINT Labels[D9VmLabelCount] = {};

    /* Declared inputs and outputs, where the definitions sit in Code and the outputs written */
    D9VmDeclared Inputs[D9VmDeclaredLimit] = {};
    UINT InputCount = 0;
    D9VmDeclared Outputs[D9VmDeclaredLimit] = {};
    UINT OutputCount = 0;
    UINT Definitions[D9VmDefinitionLimit] = {};
    UINT DefinitionCount = 0;
    DWORD Written = 0;

private:
    VOID
    Note(
        _In_ UINT Position);
};

/* Runs vertex shaders one vertex at a time with the system's arithmetic */
class D9VertexMachine
{
public:
    D9VertexMachine() = default;

    ~D9VertexMachine();

    D9VertexMachine(const D9VertexMachine&) = delete;
    D9VertexMachine& operator=(const D9VertexMachine&) = delete;

    HRESULT
    Initialize(
        _In_ UINT Floats,
        _In_ UINT Ints,
        _In_ UINT Bools);

    HRESULT
    Run(
        _In_ const D9VmProgram* Program);

    D9VmRegisters Registers = {};
    D9VmVector* FloatConstants = nullptr;
    D9VmVector* IntConstants = nullptr;
    D9VmValue* BoolConstants = nullptr;
    UINT FloatCount = 0;
    UINT IntCount = 0;
    UINT BoolCount = 0;

    D9VmSampleRoutine Sample = nullptr;
    VOID* SampleContext = nullptr;

private:
    /* One active loop: its counter register, its step, the iterations left and the step that starts it */
    struct Loop
    {
        INT Counter;
        INT Step;
        INT Remaining;
        UINT Start;
    };

    D9VmValue*
    Locate(
        _In_ UINT Type,
        _In_ UINT Number,
        _Out_opt_ UINT* Following);

    BOOLEAN
    ReadSource(
        _Inout_ UINT* Cursor,
        _In_ UINT Slot,
        _In_ UINT Rows);

    BOOLEAN
    ReadDestination(
        _Inout_ UINT* Cursor);

    VOID
    Store(
        _In_reads_(4) const D9VmValue* Result,
        _In_ BOOLEAN Saturate);

    VOID
    EnterLoop(
        _In_ INT Iterations,
        _In_ INT Counter,
        _In_ INT Step,
        _In_ UINT Start);

    BOOLEAN
    LeaveLoop(
        _In_ BOOLEAN Breaking);

    BOOLEAN
    Arithmetic(
        _In_ DWORD Opcode,
        _In_ UINT Cursor);

    const D9VmProgram* Current = nullptr;
    const DWORD* Code = nullptr;

    /* Sources as the last instruction read them; slots keep stale values like the system's */
    D9VmVector Sources[8] = {};

    D9VmValue* Target = nullptr;
    UINT TargetMask = 0;
    BOOLEAN TargetSaturates = FALSE;
    BOOLEAN Predicated = FALSE;

    /* Slot 0 is the loop outside every loop */
    Loop Loops[D9VmLoopDepth + 1] = {};
    UINT LoopLevel = 0;
};
