/*
 * PROJECT:     ReactX build tools
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Builds the shaders the Direct3D 12 core creates for itself and writes them as a header
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * internalshaders <header>
 *
 * The DXIL modules are put together with the vendored LLVM IR classes and its bitcode writer, then wrapped
 * in containers with the parts a validated shader carries. The DXBC fallbacks are written token by token.
 * Every container is hashed with the ReactX DXBC hash.
 */

#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include "dxc/Support/Global.h"
#include "dxc/Support/WinIncludes.h"

#include <cstdio>
#include <string>
#include <vector>

#include "dxbchash.hpp"

using llvm::ConstantAsMetadata;
using llvm::LLVMContext;
using llvm::MDNode;
using llvm::MDString;
using llvm::Metadata;

typedef std::vector<BYTE> RxBytes;

static
VOID
RxPut32(
    _Inout_ RxBytes& Out,
    _In_ UINT Value)
{
    for (UINT Shift = 0; Shift < 32; Shift += 8)
        Out.push_back((BYTE)(Value >> Shift));
}

static
VOID
RxPutText(
    _Inout_ RxBytes& Out,
    _In_z_ const char* Text,
    _In_ BYTE Pad)
{
    for (const char* Each = Text; *Each != '\0'; Each++)
        Out.push_back((BYTE)*Each);
    Out.push_back(0);
    while ((Out.size() & 3) != 0)
        Out.push_back(Pad);
}

static
UINT
RxFourCc(
    _In_reads_(4) const char* Name)
{
    return (UINT)(BYTE)Name[0] | ((UINT)(BYTE)Name[1] << 8) | ((UINT)(BYTE)Name[2] << 16) |
           ((UINT)(BYTE)Name[3] << 24);
}

struct RxPart
{
    UINT FourCc;
    RxBytes Data;
};

/* A container around the parts, version 1.0, with its hash filled in */
static
RxBytes
RxContainer(
    _In_ const std::vector<RxPart>& Parts)
{
    const UINT HeaderSize = 32 + 4 * (UINT)Parts.size();
    RxBytes Out;
    UINT Offset = HeaderSize;
    UINT Total = HeaderSize;

    for (const RxPart& Part : Parts)
        Total += 8 + (UINT)Part.Data.size();

    RxPut32(Out, RxFourCc("DXBC"));
    Out.resize(Out.size() + 16, 0);
    RxPut32(Out, 1);
    RxPut32(Out, Total);
    RxPut32(Out, (UINT)Parts.size());
    for (const RxPart& Part : Parts)
    {
        RxPut32(Out, Offset);
        Offset += 8 + (UINT)Part.Data.size();
    }
    for (const RxPart& Part : Parts)
    {
        RxPut32(Out, Part.FourCc);
        RxPut32(Out, (UINT)Part.Data.size());
        Out.insert(Out.end(), Part.Data.begin(), Part.Data.end());
    }

    RxDxbcHash(Out.data() + 20, (UINT)Out.size() - 20, Out.data() + 4);
    return Out;
}

/* Signature parts: no inputs, and at most SV_Position written as four floats */
static
RxBytes
RxDxilSignature(
    _In_ BOOLEAN HasPosition)
{
    RxBytes Out;

    RxPut32(Out, HasPosition ? 1 : 0);
    RxPut32(Out, 8);
    if (!HasPosition)
        return Out;

    RxPut32(Out, 0);
    RxPut32(Out, 8 + 32);
    RxPut32(Out, 0);
    RxPut32(Out, 1);
    RxPut32(Out, 3);
    RxPut32(Out, 0);
    RxPut32(Out, 0x0000F00F);
    RxPut32(Out, 0);
    RxPutText(Out, "SV_Position", 0);
    return Out;
}

static
RxBytes
RxDxbcSignature(
    _In_ BOOLEAN HasPosition)
{
    RxBytes Out;

    RxPut32(Out, HasPosition ? 1 : 0);
    RxPut32(Out, 8);
    if (!HasPosition)
        return Out;

    RxPut32(Out, 8 + 24);
    RxPut32(Out, 0);
    RxPut32(Out, 1);
    RxPut32(Out, 3);
    RxPut32(Out, 0);
    RxPut32(Out, 0x0000000F);
    RxPutText(Out, "SV_Position", 0);
    return Out;
}

/* Pipeline state validation data, the version 1 runtime info a 1.4 validator writes */
static
RxBytes
RxValidationPart(
    _In_ BYTE Stage,
    _In_ BOOLEAN HasPosition)
{
    RxBytes Out;

    RxPut32(Out, 36);
    RxPut32(Out, HasPosition ? 1 : 0);
    RxPut32(Out, 0);
    RxPut32(Out, 0);
    RxPut32(Out, 0);
    RxPut32(Out, 0);
    RxPut32(Out, 0xFFFFFFFF);
    RxPut32(Out, Stage);
    RxPut32(Out, HasPosition ? 0x00000100 : 0);
    RxPut32(Out, HasPosition ? 1 : 0);

    /* No resources, an empty string table and the semantic index table */
    RxPut32(Out, 0);
    RxPut32(Out, 4);
    RxPut32(Out, 0);
    RxPut32(Out, HasPosition ? 1 : 0);
    if (HasPosition)
    {
        RxPut32(Out, 0);

        /* One element: row 0, four columns, position, float, no perspective */
        RxPut32(Out, 16);
        RxPut32(Out, 0);
        RxPut32(Out, 0);
        RxPut32(Out, 0x03440001);
        RxPut32(Out, 0x00000403);
    }
    return Out;
}

/* The DXIL part: program header, then the module's bitcode */
static
RxBytes
RxProgramPart(
    _In_ UINT Kind,
    _In_ const llvm::Module& Module)
{
    llvm::SmallVector<char, 4096> Bitcode;
    RxBytes Out;

    {
        llvm::raw_svector_ostream Stream(Bitcode);

        llvm::WriteBitcodeToFile(&Module, Stream);
        Stream.flush();
    }

    RxPut32(Out, (Kind << 16) | (6 << 4));
    RxPut32(Out, (UINT)(24 + Bitcode.size() + 3) / 4);
    RxPut32(Out, RxFourCc("DXIL"));
    RxPut32(Out, 0x100);
    RxPut32(Out, 16);
    RxPut32(Out, (UINT)Bitcode.size());
    Out.insert(Out.end(), Bitcode.begin(), Bitcode.end());
    while ((Out.size() & 3) != 0)
        Out.push_back(0);
    return Out;
}

static
Metadata*
RxInt(
    _In_ LLVMContext& Context,
    _In_ UINT Bits,
    _In_ UINT Value)
{
    return ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::IntegerType::get(Context, Bits), Value));
}

/* What every module carries: target, identity and versions, then the shader model and type annotations */
static
VOID
RxDescribeModule(
    _Inout_ llvm::Module& Module,
    _In_ llvm::Function* Entry,
    _In_z_ const char* Model)
{
    LLVMContext& Context = Module.getContext();
    Metadata* Empty = MDNode::get(Context, {});
    Metadata* Annotation[] = { RxInt(Context, 32, 0), Empty, Empty };
    Metadata* Annotations[] = { MDNode::get(Context, Annotation) };
    Metadata* Function[] = { RxInt(Context, 32, 1), llvm::ValueAsMetadata::get(Entry), MDNode::get(Context, Annotations) };
    Metadata* Ident[] = { MDString::get(Context, "dxc 1.2") };
    Metadata* Version[] = { RxInt(Context, 32, 1), RxInt(Context, 32, 0) };
    Metadata* Validator[] = { RxInt(Context, 32, 1), RxInt(Context, 32, 4) };
    Metadata* ShaderModel[] = { MDString::get(Context, Model), RxInt(Context, 32, 6), RxInt(Context, 32, 0) };

    Module.getOrInsertNamedMetadata("llvm.ident")->addOperand(MDNode::get(Context, Ident));
    Module.getOrInsertNamedMetadata("dx.version")->addOperand(MDNode::get(Context, Version));
    Module.getOrInsertNamedMetadata("dx.valver")->addOperand(MDNode::get(Context, Validator));
    Module.getOrInsertNamedMetadata("dx.shaderModel")->addOperand(MDNode::get(Context, ShaderModel));
    Module.getOrInsertNamedMetadata("dx.typeAnnotations")->addOperand(MDNode::get(Context, Function));
}

static
VOID
RxSetTarget(
    _Inout_ llvm::Module& Module)
{
    Module.setDataLayout("e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64");
    Module.setTargetTriple("dxil-ms-dx");
}

/* VS: writes zero to all four components of SV_Position */
static
RxBytes
RxVertexDxil(VOID)
{
    LLVMContext Context;
    llvm::Module Module("VS", Context);
    llvm::Type* Void = llvm::Type::getVoidTy(Context);
    llvm::Type* I32 = llvm::Type::getInt32Ty(Context);
    llvm::Type* I8 = llvm::Type::getInt8Ty(Context);
    llvm::Type* Float = llvm::Type::getFloatTy(Context);
    llvm::Type* StoreArguments[] = { I32, I32, I32, I8, Float };
    llvm::Function* Entry;
    llvm::Function* Store;
    std::vector<RxPart> Parts;

    RxSetTarget(Module);
    Entry = llvm::Function::Create(llvm::FunctionType::get(Void, false), llvm::GlobalValue::ExternalLinkage, "VS",
                                   &Module);
    Store = llvm::Function::Create(llvm::FunctionType::get(Void, StoreArguments, false),
                                   llvm::GlobalValue::ExternalLinkage, "dx.op.storeOutput.f32", &Module);
    Store->addFnAttr(llvm::Attribute::NoUnwind);

    {
        llvm::IRBuilder<> Builder(llvm::BasicBlock::Create(Context, "", Entry));

        for (UINT Component = 0; Component < 4; Component++)
        {
            llvm::Value* Arguments[] = { llvm::ConstantInt::get(I32, 5), llvm::ConstantInt::get(I32, 0),
                                         llvm::ConstantInt::get(I32, 0), llvm::ConstantInt::get(I8, Component),
                                         llvm::ConstantFP::get(Float, 0.0) };

            Builder.CreateCall(Store, Arguments);
        }
        Builder.CreateRetVoid();
    }

    RxDescribeModule(Module, Entry, "vs");

    {
        Metadata* Index[] = { RxInt(Context, 32, 0) };
        Metadata* Position[] = { RxInt(Context, 32, 0), MDString::get(Context, "SV_Position"), RxInt(Context, 8, 9),
                                 RxInt(Context, 8, 3), MDNode::get(Context, Index), RxInt(Context, 8, 4),
                                 RxInt(Context, 32, 1), RxInt(Context, 8, 4), RxInt(Context, 32, 0),
                                 RxInt(Context, 8, 0), nullptr };
        Metadata* Outputs[] = { MDNode::get(Context, Position) };
        Metadata* Signatures[] = { nullptr, MDNode::get(Context, Outputs), nullptr };
        Metadata* EntryPoint[] = { llvm::ValueAsMetadata::get(Entry), MDString::get(Context, "VS"),
                                   MDNode::get(Context, Signatures), nullptr, nullptr };
        UINT ViewIdWords[] = { 0, 4 };
        Metadata* ViewId[] = { ConstantAsMetadata::get(llvm::ConstantDataArray::get(Context, ViewIdWords)) };

        Module.getOrInsertNamedMetadata("dx.viewIdState")->addOperand(MDNode::get(Context, ViewId));
        Module.getOrInsertNamedMetadata("dx.entryPoints")->addOperand(MDNode::get(Context, EntryPoint));
    }

    Parts.push_back({ RxFourCc("SFI0"), RxBytes(8, 0) });
    Parts.push_back({ RxFourCc("ISG1"), RxDxilSignature(FALSE) });
    Parts.push_back({ RxFourCc("OSG1"), RxDxilSignature(TRUE) });
    Parts.push_back({ RxFourCc("PSV0"), RxValidationPart(1, TRUE) });
    Parts.push_back({ RxFourCc("DXIL"), RxProgramPart(1, Module) });
    return RxContainer(Parts);
}

/* cs: an empty compute shader of one thread */
static
RxBytes
RxComputeDxil(VOID)
{
    LLVMContext Context;
    llvm::Module Module("cs", Context);
    llvm::Function* Entry;
    std::vector<RxPart> Parts;

    RxSetTarget(Module);
    Entry = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false),
                                   llvm::GlobalValue::ExternalLinkage, "cs", &Module);
    llvm::IRBuilder<>(llvm::BasicBlock::Create(Context, "", Entry)).CreateRetVoid();

    RxDescribeModule(Module, Entry, "cs");

    {
        Metadata* Threads[] = { RxInt(Context, 32, 1), RxInt(Context, 32, 1), RxInt(Context, 32, 1) };
        Metadata* Properties[] = { RxInt(Context, 32, 4), MDNode::get(Context, Threads) };
        Metadata* EntryPoint[] = { llvm::ValueAsMetadata::get(Entry), MDString::get(Context, "cs"), nullptr, nullptr,
                                   MDNode::get(Context, Properties) };

        Module.getOrInsertNamedMetadata("dx.entryPoints")->addOperand(MDNode::get(Context, EntryPoint));
    }

    Parts.push_back({ RxFourCc("SFI0"), RxBytes(8, 0) });
    Parts.push_back({ RxFourCc("ISG1"), RxDxilSignature(FALSE) });
    Parts.push_back({ RxFourCc("OSG1"), RxDxilSignature(FALSE) });
    Parts.push_back({ RxFourCc("PSV0"), RxValidationPart(5, FALSE) });
    Parts.push_back({ RxFourCc("DXIL"), RxProgramPart(5, Module) });
    return RxContainer(Parts);
}

/* Resource definitions with nothing bound, naming the compiler the original was built with */
static
RxBytes
RxResourceDefinitions(
    _In_ UINT Target,
    _In_z_ const char* Creator,
    _In_ BOOLEAN HasExtendedHeader)
{
    const UINT HeaderSize = HasExtendedHeader ? 60 : 28;
    RxBytes Out;

    RxPut32(Out, 0);
    RxPut32(Out, 0);
    RxPut32(Out, 0);
    RxPut32(Out, HeaderSize);
    RxPut32(Out, Target);
    RxPut32(Out, 0x100);
    RxPut32(Out, HeaderSize);
    if (HasExtendedHeader)
    {
        RxPut32(Out, RxFourCc("RD11"));
        RxPut32(Out, 60);
        RxPut32(Out, 24);
        RxPut32(Out, 32);
        RxPut32(Out, 40);
        RxPut32(Out, 36);
        RxPut32(Out, 12);
        RxPut32(Out, 0);
    }
    RxPutText(Out, Creator, 0xAB);
    return Out;
}

/* Statistics, with the counts the original compile reported */
static
RxBytes
RxStatistics(
    _In_ UINT Words,
    _In_ UINT Instructions,
    _In_ UINT Declarations,
    _In_ UINT Moves)
{
    RxBytes Out;

    for (UINT Index = 0; Index < Words; Index++)
    {
        UINT Value = 0;

        if (Index == 0)
            Value = Instructions;
        else if (Index == 3)
            Value = Declarations;
        else if (Index == 7)
            Value = 1;
        else if (Index == 19)
            Value = Moves;
        RxPut32(Out, Value);
    }
    return Out;
}

/* vs_4_0: dcl_output_siv o0.xyzw, position; mov o0.xyzw, l(0, 0, 0, 0); ret */
static
RxBytes
RxVertexDxbc(VOID)
{
    static const UINT Tokens[] = {
        0x00010040, 15,
        0x04000067, 0x001020F2, 0x00000000, 0x00000001,
        0x08000036, 0x001020F2, 0x00000000, 0x00004002, 0, 0, 0, 0,
        0x0100003E,
    };
    std::vector<RxPart> Parts;
    RxBytes Code;

    for (UINT Token : Tokens)
        RxPut32(Code, Token);

    Parts.push_back({ RxFourCc("RDEF"), RxResourceDefinitions(0xFFFE0400, "Microsoft (R) HLSL Shader Compiler 6.4.9840.0",
                                                              FALSE) });
    Parts.push_back({ RxFourCc("ISGN"), RxDxbcSignature(FALSE) });
    Parts.push_back({ RxFourCc("OSGN"), RxDxbcSignature(TRUE) });
    Parts.push_back({ RxFourCc("SHDR"), Code });
    Parts.push_back({ RxFourCc("STAT"), RxStatistics(29, 2, 1, 1) });
    return RxContainer(Parts);
}

/* cs_5_0: dcl_globalFlags refactoringAllowed; dcl_thread_group 1, 1, 1; ret */
static
RxBytes
RxComputeDxbc(VOID)
{
    static const UINT Tokens[] = {
        0x00050050, 8,
        0x0100086A,
        0x0400009B, 1, 1, 1,
        0x0100003E,
    };
    std::vector<RxPart> Parts;
    RxBytes Code;

    for (UINT Token : Tokens)
        RxPut32(Code, Token);

    Parts.push_back({ RxFourCc("RDEF"), RxResourceDefinitions(0x43530500,
                                                              "Microsoft (R) HLSL Shader Compiler 10.0.10011.0", TRUE) });
    Parts.push_back({ RxFourCc("ISGN"), RxDxbcSignature(FALSE) });
    Parts.push_back({ RxFourCc("OSGN"), RxDxbcSignature(FALSE) });
    Parts.push_back({ RxFourCc("SHEX"), Code });
    Parts.push_back({ RxFourCc("STAT"), RxStatistics(37, 1, 0, 0) });
    return RxContainer(Parts);
}

static
VOID
RxWriteArray(
    _Inout_ std::string& Text,
    _In_z_ const char* Name,
    _In_ const RxBytes& Bytes)
{
    char Line[16];

    Text += "static const BYTE ";
    Text += Name;
    Text += "[] =\n{";
    for (size_t Index = 0; Index < Bytes.size(); Index++)
    {
        sprintf_s(Line, "%s0x%02X,", Index % 16 == 0 ? "\n    " : " ", Bytes[Index]);
        Text += Line;
    }
    Text += "\n};\n\n";
}

static
VOID
RxReportHash(
    _In_z_ const char* Name,
    _In_ const RxBytes& Bytes)
{
    printf("internalshaders: %s %zu bytes hash ", Name, Bytes.size());
    for (UINT Index = 4; Index < 20; Index++)
        printf("%02X", Bytes[Index]);
    printf("\n");
}

int
main(
    _In_ int argc,
    _In_reads_(argc) char** argv)
{
    RxBytes VertexDxil;
    RxBytes ComputeDxil;
    RxBytes VertexDxbc;
    RxBytes ComputeDxbc;
    std::string Text;
    FILE* Out;

    if (argc != 2)
    {
        fprintf(stderr, "usage: internalshaders <header>\n");
        return 1;
    }

    if (FAILED(DxcInitThreadMalloc()))
        return 1;
    DxcSetThreadMallocToDefault();

    VertexDxil = RxVertexDxil();
    ComputeDxil = RxComputeDxil();
    VertexDxbc = RxVertexDxbc();
    ComputeDxbc = RxComputeDxbc();

    RxReportHash("vs dxil", VertexDxil);
    RxReportHash("cs dxil", ComputeDxil);
    RxReportHash("vs dxbc", VertexDxbc);
    RxReportHash("cs dxbc", ComputeDxbc);

    Text = "/* Generated by tools/internalshaders. Do not edit. */\n\n#pragma once\n\n";
    RxWriteArray(Text, "RxInternalVertexDxil", VertexDxil);
    RxWriteArray(Text, "RxInternalComputeDxil", ComputeDxil);
    RxWriteArray(Text, "RxInternalVertexDxbc", VertexDxbc);
    RxWriteArray(Text, "RxInternalComputeDxbc", ComputeDxbc);

    if (fopen_s(&Out, argv[1], "wb") != 0)
    {
        fprintf(stderr, "internalshaders: cannot write %s\n", argv[1]);
        return 1;
    }
    fwrite(Text.data(), 1, Text.size(), Out);
    fclose(Out);

    DxcClearThreadMalloc();
    DxcCleanupThreadMalloc();
    return 0;
}
