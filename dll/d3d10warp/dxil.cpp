/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXIL programs lowered to the tokenized program the executor runs
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

/*
 * Built with the vendored compiler's flags: LLVM stays inside this file.
 * Every SSA scalar gets a temp component; structured flow comes back from the dominator trees.
 */

#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include "dxc/Support/Global.h"
#include "dxc/Support/WinIncludes.h"
#include "dxc/DXIL/DxilModule.h"
#include "dxc/DXIL/DxilOperations.h"
#include "dxc/DXIL/DxilResource.h"
#include "dxc/DXIL/DxilSignatureElement.h"
#include "dxc/DXIL/DxilCBuffer.h"
#include "dxc/DXIL/DxilSampler.h"
#include "dxc/DXIL/DxilResourceProperties.h"

#include <d3d11TokenizedProgramFormat.hpp>
#include <algorithm>
#include <unordered_map>
#include <vector>

#include "dxil.hpp"

using hlsl::DXIL::OpCode;

namespace
{

typedef std::vector<UINT> RxTokens;

constexpr UINT RxMaskMode = D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE;
constexpr UINT RxSwizzleMode = D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE;
constexpr UINT RxSelectMode = D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE;
constexpr UINT RxWarpMaxHeapBinds = 4;

/* TRUE when a call returns or takes a 16 bit value, alone or as a structure's first member */
BOOLEAN
RxNamesSixteen(
    _In_ llvm::CallInst *Call)
{
    auto Sixteen = [](llvm::Type *Type)
    {
        if (llvm::StructType *Members = llvm::dyn_cast<llvm::StructType>(Type))
            Type = (Members->getNumElements() != 0) ? Members->getElementType(0) : Type;
        return Type->isHalfTy() || Type->isIntegerTy(16);
    };

    if (Sixteen(Call->getType()))
        return TRUE;
    for (UINT Argument = 1; Argument < Call->getNumArgOperands(); Argument++)
    {
        if (Sixteen(Call->getArgOperand(Argument)->getType()))
            return TRUE;
    }
    return FALSE;
}

UINT
RxHeapOperandType(
    _In_ UINT Class)
{
    switch ((hlsl::DXIL::ResourceClass)Class)
    {
        case hlsl::DXIL::ResourceClass::SRV:
            return D3D10_SB_OPERAND_TYPE_RESOURCE;
        case hlsl::DXIL::ResourceClass::UAV:
            return D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW;
        case hlsl::DXIL::ResourceClass::CBuffer:
            return D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER;
        default:
            return D3D10_SB_OPERAND_TYPE_SAMPLER;
    }
}

UINT
RxOperandHead(
    _In_ UINT Type,
    _In_ UINT Dimensions)
{
    UINT Token = ENCODE_D3D10_SB_OPERAND_TYPE(Type) | ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(Dimensions);

    for (UINT Index = 0; Index < Dimensions; Index++)
        Token |= ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(Index, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
    return Token;
}

UINT
RxFourComponents(
    _In_ UINT Mode,
    _In_ UINT Bits)
{
    UINT Token = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                 ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(Mode);

    if (Mode == RxMaskMode)
        Token |= Bits << D3D10_SB_OPERAND_4_COMPONENT_MASK_SHIFT;
    else if (Mode == RxSwizzleMode)
        Token |= ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(Bits & 3, (Bits >> 2) & 3, (Bits >> 4) & 3, (Bits >> 6) & 3);
    else
        Token |= ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(Bits);
    return Token;
}

constexpr UINT RxXyzw = 0xE4;

/* A register operand with up to two immediate indices */
RxTokens
RxRegister(
    _In_ UINT Type,
    _In_ UINT Mode,
    _In_ UINT Bits,
    _In_ std::initializer_list<UINT> Indices)
{
    RxTokens Tokens;

    Tokens.push_back(RxOperandHead(Type, (UINT)Indices.size()) | RxFourComponents(Mode, Bits));
    Tokens.insert(Tokens.end(), Indices.begin(), Indices.end());
    return Tokens;
}

/* Registers with no components: samplers, resources in declarations, the null destination */
RxTokens
RxBare(
    _In_ UINT Type,
    _In_ std::initializer_list<UINT> Indices)
{
    RxTokens Tokens;

    Tokens.push_back(RxOperandHead(Type, (UINT)Indices.size()) | ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_0_COMPONENT));
    Tokens.insert(Tokens.end(), Indices.begin(), Indices.end());
    return Tokens;
}

RxTokens
RxImmediate(
    _In_ UINT Value)
{
    return { ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
             ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D) |
             ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT), Value };
}

/* Doubles and 64 bit integers take two neighboring components, low word first */
BOOLEAN
RxIsWide(
    _In_ llvm::Type *Type)
{
    return Type->isDoubleTy() || Type->isIntegerTy(64);
}

/* A 64 bit constant as both pairs of a four component immediate */
RxTokens
RxWideImmediate(
    _In_ UINT64 Value)
{
    return { ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
             ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D) | RxFourComponents(RxSwizzleMode, RxXyzw),
             (UINT)Value, (UINT)(Value >> 32), (UINT)Value, (UINT)(Value >> 32) };
}

/* Reads the pair at First into both halves of a register: first, next, first, next */
UINT
RxPairSwizzle(
    _In_ UINT First)
{
    return First | (First + 1) << 2 | First << 4 | (First + 1) << 6;
}

RxTokens
RxModified(
    _In_ RxTokens Tokens,
    _In_ D3D10_SB_OPERAND_MODIFIER Modifier)
{
    Tokens[0] |= ENCODE_D3D10_SB_OPERAND_EXTENDED(1);
    Tokens.insert(Tokens.begin() + 1, ENCODE_D3D10_SB_EXTENDED_OPERAND_TYPE(D3D10_SB_EXTENDED_OPERAND_MODIFIER) |
                                      ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(Modifier));
    return Tokens;
}

typedef struct _RX_HANDLE
{
    UINT Class;             /* hlsl::DXIL::ResourceClass */
    UINT Slot;
    UINT Kind;              /* hlsl::DXIL::ResourceKind */
    llvm::Value *Index;     /* a register index the program computes, or null */
    UINT Lower;             /* the range's first register */
    UINT Count;             /* registers in the range */
    llvm::Value *Heap;      /* the descriptor heap index of a heap handle, or null */
} RX_HANDLE;

/* A slot a descriptor heap handle borrows, bound from the heap before each use */
typedef struct _RX_HEAP_SLOT
{
    UINT Class;
    UINT Slot;
    UINT Kind;
    llvm::Value *Index;
} RX_HEAP_SLOT;

/* A pointer as the object it points into, a constant byte offset and scaled dynamic terms */
typedef struct _RX_POINTER
{
    llvm::Value *Base;
    UINT Bytes;
    std::vector<std::pair<llvm::Value *, UINT>> Terms;
} RX_POINTER;

typedef struct _RX_LOOP
{
    llvm::BasicBlock *Header;
    llvm::BasicBlock *Exit;
    llvm::Loop *Loop;
} RX_LOOP;

class RxLowering
{
public:
    RxLowering(
        _In_ hlsl::DxilModule &Module)
        : Dxil(Module), Op(Module.GetOP())
    {
    }

    BOOLEAN
    Run(
        _Out_ RxTokens *Program);

private:
    UINT
    Fresh(VOID)
    {
        return NextSlot++;
    }

    UINT
    Fresh4(VOID)
    {
        NextSlot = (NextSlot + 3) & ~3u;
        NextSlot += 4;
        return NextSlot / 4 - 1;
    }

    /* Two components that start at x or z */
    UINT
    Fresh2(VOID)
    {
        NextSlot = (NextSlot + 1) & ~1u;
        NextSlot += 2;
        return NextSlot - 2;
    }

    UINT
    FreshFor(
        _In_ llvm::Type *Type)
    {
        return RxIsWide(Type) ? Fresh2() : Fresh();
    }

    UINT
    SlotOf(
        _In_ const llvm::Value *Value);

    RxTokens
    Source(
        _In_ llvm::Value *Value);

    /* A slot as a destination, both components of a pair when Wide */
    RxTokens
    SlotDest(
        _In_ UINT Slot,
        _In_ BOOLEAN Wide)
    {
        return RxRegister(D3D10_SB_OPERAND_TYPE_TEMP, RxMaskMode, (Wide ? 3u : 1u) << (Slot & 3), { Slot / 4 });
    }

    RxTokens
    SlotSource(
        _In_ UINT Slot,
        _In_ BOOLEAN Wide)
    {
        if (Wide)
            return Temp(Slot / 4, RxSwizzleMode, RxPairSwizzle(Slot & 3));
        return Temp(Slot / 4, RxSelectMode, Slot & 3);
    }

    RxTokens
    Dest(
        _In_ const llvm::Value *Value)
    {
        return SlotDest(SlotOf(Value), RxIsWide(Value->getType()));
    }

    /* One word of a 64 bit value: the low one, or the high one when High */
    RxTokens
    Half(
        _In_ llvm::Value *Value,
        _In_ BOOLEAN High);

    BOOLEAN
    LowerWide(
        _In_ llvm::Instruction *Instruction);

    BOOLEAN
    LowerWideCall(
        _In_ llvm::CallInst *Call,
        _In_ OpCode Code);

    VOID
    EmitLong(
        _In_ llvm::Value *Target,
        _In_ UINT Kind,
        _In_ RxTokens First,
        _In_ RxTokens Second = RxImmediate(0),
        _In_ RxTokens Third = RxImmediate(0))
    {
        Emit(RxWarpOpcodeLong, { Dest(Target), First, Second, Third, RxImmediate(Kind) });
    }

    BOOLEAN
    WideAccess(
        _In_ llvm::CallInst *Call,
        _In_ OpCode Code,
        _In_ const RX_HANDLE &Handle);

    RxTokens
    Temp(
        _In_ UINT Register,
        _In_ UINT Mode,
        _In_ UINT Bits)
    {
        return RxRegister(D3D10_SB_OPERAND_TYPE_TEMP, Mode, Bits, { Register });
    }

    VOID
    Emit(
        _In_ UINT Opcode,
        _In_ std::initializer_list<RxTokens> Operands,
        _In_ UINT Flags = 0,
        _In_opt_ const RxTokens *Extended = nullptr);

    VOID
    Declare(
        _In_ UINT Opcode,
        _In_ std::initializer_list<RxTokens> Operands,
        _In_ UINT Flags = 0);

    /* Moves scalars into one register's components; FALSE fills the rest with zero */
    UINT
    Gather(
        _In_ std::initializer_list<llvm::Value *> Values);

    UINT
    GatherRange(
        _In_ llvm::CallInst *Call,
        _In_ UINT First,
        _In_ UINT Count);

    BOOLEAN
    Declarations(VOID);

    BOOLEAN
    Signature(
        _In_ const hlsl::DxilSignature &Elements,
        _In_ BOOLEAN Input);

    BOOLEAN
    Resources(VOID);

    BOOLEAN
    HeapResources(VOID);

    BOOLEAN
    Lower(
        _In_ llvm::Instruction *Instruction);

    BOOLEAN
    LowerPlain(
        _In_ llvm::Instruction *Instruction);

    VOID
    Narrow(
        _In_ llvm::Value *Value);

    RxTokens
    SignedSource(
        _In_ llvm::Value *Value);

    BOOLEAN
    LowerCall(
        _In_ llvm::CallInst *Call);

    BOOLEAN
    LowerResource(
        _In_ llvm::CallInst *Call,
        _In_ OpCode Code);

    BOOLEAN
    LowerWave(
        _In_ llvm::CallInst *Call,
        _In_ OpCode Code);

    BOOLEAN
    HandleOf(
        _In_ llvm::Value *Value,
        _Out_ RX_HANDLE *Handle);

    UINT
    Place(
        _In_ UINT Class,
        _In_ UINT Range,
        _In_ UINT Space,
        _In_ UINT Lower,
        _In_ UINT Count,
        _In_ UINT Limit);

    BOOLEAN
    BindingRange(
        _In_ llvm::Constant *Binding,
        _Out_ UINT *Class,
        _Out_ UINT *Range);

    BOOLEAN
    LowerDynamic(
        _In_ llvm::CallInst *Call,
        _In_ llvm::Value *Handle,
        _In_ const RX_HANDLE &Range);

    /* A register for the status a resource access returns when the program reads it, UINT_MAX otherwise */
    UINT
    StatusRegister(
        _In_ const llvm::Value *Value)
    {
        UINT Register;

        for (const llvm::User *User : Value->users())
        {
            const llvm::ExtractValueInst *Extract = llvm::dyn_cast<llvm::ExtractValueInst>(User);

            if (Extract != nullptr && Extract->getIndices()[0] == 4)
            {
                Register = Fresh4();
                StatusSlots[Value] = Register * 4;
                return Register;
            }
        }
        return UINT_MAX;
    }

    UINT
    ResultRegister(
        _In_ const llvm::Value *Value)
    {
        auto Found = Slots.find(Value);
        UINT Register;

        if (Found != Slots.end())
            return Found->second / 4;
        Register = Fresh4();
        Slots[Value] = Register * 4;
        return Register;
    }

    RxTokens
    ResourceOperand(
        _In_ const RX_HANDLE &Handle,
        _In_ UINT Mode = RxSwizzleMode,
        _In_ UINT Bits = RxXyzw)
    {
        return RxRegister((Handle.Class == (UINT)hlsl::DXIL::ResourceClass::UAV) ? D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW :
                                                                                   D3D10_SB_OPERAND_TYPE_RESOURCE,
                          Mode, Bits, { Handle.Slot });
    }

    VOID
    Edge(
        _In_ llvm::BasicBlock *From,
        _In_ llvm::BasicBlock *To);

    BOOLEAN
    Region(
        _In_ llvm::BasicBlock *Block,
        _In_opt_ llvm::BasicBlock *Stop);

    BOOLEAN
    Arm(
        _In_ llvm::BasicBlock *From,
        _In_ llvm::BasicBlock *Target,
        _In_opt_ llvm::BasicBlock *Merge);

    BOOLEAN
    InputCoordinates(
        _In_ llvm::CallInst *Call,
        _In_ UINT First,
        _In_ UINT Count,
        _Out_ RxTokens *Operand);

    BOOLEAN
    Leaves(
        _In_ llvm::BasicBlock *From,
        _In_ llvm::BasicBlock *Target);

    BOOLEAN
    Cases(
        _In_ llvm::BasicBlock *Block,
        _In_ llvm::SwitchInst *Switch,
        _In_opt_ llvm::BasicBlock *Merge);

    BOOLEAN
    Resolve(
        _In_ llvm::Value *Pointer,
        _Out_ RX_POINTER *Place);

    RxTokens
    Offset(
        _In_ const RX_POINTER &Place,
        _In_ UINT Unit);

    BOOLEAN
    Memory(
        _In_ llvm::Instruction *Instruction);

    BOOLEAN
    SharedAtomic(
        _In_ llvm::Instruction *Instruction);

    hlsl::DxilModule &Dxil;
    hlsl::OP *Op;
    RxTokens Decls;
    RxTokens Body;
    std::unordered_map<const llvm::Value *, UINT> Slots;
    std::unordered_map<const llvm::Value *, UINT> StatusSlots;     /* the residency status a resource access returns last */
    std::unordered_map<const llvm::Value *, UINT> SharedRegisters;
    std::unordered_map<const llvm::Value *, UINT> Arrays;
    std::unordered_map<UINT64, UINT> Bases;
    std::unordered_map<const llvm::Value *, UINT> Overrides;
    std::unordered_map<const llvm::Value *, RX_HEAP_SLOT> HeapSlots;
    std::vector<RX_HEAP_SLOT> PendingHeap;
    UINT HighSlot[4] = { 128, 64, 14, 16 };
    std::unordered_map<const llvm::Value *, std::pair<UINT, UINT>> Inputs;
    UINT NextSlot = 0;
    llvm::DominatorTreeBase<llvm::BasicBlock> PostDominators{ true };
    llvm::DominatorTree Dominators;
    llvm::LoopInfo Loops;
    std::vector<RX_LOOP> Open;
    BOOLEAN PixelShader = FALSE;
    BOOLEAN Failed = FALSE;

public:
    /* Class, slot, space and register of every slot not named by its own register */
    std::vector<UINT> Sources;
};

UINT
RxLowering::SlotOf(
    _In_ const llvm::Value *Value)
{
    auto Found = Slots.find(Value);

    if (Found != Slots.end())
        return Found->second;
    UINT Slot = FreshFor(Value->getType());

    Slots[Value] = Slot;
    return Slot;
}

/* Constants become immediates; anything else reads its temp component, or its pair */
RxTokens
RxLowering::Source(
    _In_ llvm::Value *Value)
{
    if (llvm::ConstantInt *Integer = llvm::dyn_cast<llvm::ConstantInt>(Value))
    {
        if (Integer->getBitWidth() == 64)
            return RxWideImmediate(Integer->getZExtValue());
        return RxImmediate(Integer->getBitWidth() == 1 ? (Integer->isZero() ? 0u : ~0u) : (UINT)Integer->getZExtValue());
    }
    if (llvm::ConstantFP *Float = llvm::dyn_cast<llvm::ConstantFP>(Value))
    {
        llvm::APFloat Single = Float->getValueAPF();
        bool Lost;

        if (Float->getType()->isDoubleTy())
            return RxWideImmediate(Single.bitcastToAPInt().getZExtValue());
        Single.convert(llvm::APFloat::IEEEsingle, llvm::APFloat::rmNearestTiesToEven, &Lost);
        return RxImmediate((UINT)Single.bitcastToAPInt().getZExtValue());
    }
    if (llvm::isa<llvm::UndefValue>(Value) || llvm::isa<llvm::ConstantAggregateZero>(Value))
        return RxImmediate(0);
    return SlotSource(SlotOf(Value), RxIsWide(Value->getType()));
}

RxTokens
RxLowering::Half(
    _In_ llvm::Value *Value,
    _In_ BOOLEAN High)
{
    UINT Slot;

    if (llvm::isa<llvm::Constant>(Value) && !llvm::isa<llvm::GlobalValue>(Value))
    {
        RxTokens Wide = Source(Value);

        return RxImmediate((Wide.size() == 5) ? Wide[High ? 2 : 1] : Wide[1]);
    }
    Slot = SlotOf(Value);
    return Temp(Slot / 4, RxSelectMode, (Slot & 3) + (High ? 1 : 0));
}

VOID
RxLowering::Emit(
    _In_ UINT Opcode,
    _In_ std::initializer_list<RxTokens> Operands,
    _In_ UINT Flags,
    _In_opt_ const RxTokens *Extended)
{
    UINT Length = 1 + (Extended ? (UINT)Extended->size() : 0);
    BOOLEAN Named = FALSE;

    for (const RxTokens &Operand : Operands)
    {
        UINT Type = DECODE_D3D10_SB_OPERAND_TYPE(Operand[0]);
        UINT First = DECODE_IS_D3D10_SB_OPERAND_EXTENDED(Operand[0]) ? 2 : 1;

        Length += (UINT)Operand.size();
        for (const RX_HEAP_SLOT &Heap : PendingHeap)
        {
            if (Operand.size() > First && Operand[First] == Heap.Slot && Type == RxHeapOperandType(Heap.Class))
                Named = TRUE;
        }
    }
    if (Named)
    {
        UINT Prefix = 1;

        for (const RX_HEAP_SLOT &Heap : PendingHeap)
            Prefix += (UINT)Source(Heap.Index).size() + 2;
        Body.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(RxWarpOpcodeHeap) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(Prefix));
        for (const RX_HEAP_SLOT &Heap : PendingHeap)
        {
            RxTokens Index = Source(Heap.Index);
            RxTokens Target = RxImmediate(Heap.Class << 16 | Heap.Slot);

            Body.insert(Body.end(), Index.begin(), Index.end());
            Body.insert(Body.end(), Target.begin(), Target.end());
        }
    }
    Body.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(Opcode) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(Length) | Flags |
                   (Extended ? ENCODE_D3D10_SB_OPCODE_EXTENDED(1) : 0));
    if (Extended)
        Body.insert(Body.end(), Extended->begin(), Extended->end());
    for (const RxTokens &Operand : Operands)
        Body.insert(Body.end(), Operand.begin(), Operand.end());
}

VOID
RxLowering::Declare(
    _In_ UINT Opcode,
    _In_ std::initializer_list<RxTokens> Operands,
    _In_ UINT Flags)
{
    UINT Length = 1;

    for (const RxTokens &Operand : Operands)
        Length += (UINT)Operand.size();
    Decls.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(Opcode) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(Length) | Flags);
    for (const RxTokens &Operand : Operands)
        Decls.insert(Decls.end(), Operand.begin(), Operand.end());
}

UINT
RxLowering::Gather(
    _In_ std::initializer_list<llvm::Value *> Values)
{
    UINT Register = Fresh4();
    UINT Component = 0;

    for (llvm::Value *Value : Values)
    {
        if (Component < 4)
            Emit(D3D10_SB_OPCODE_MOV, { Temp(Register, RxMaskMode, 1u << Component), Source(Value) });
        Component++;
    }
    for (; Component < 4; Component++)
        Emit(D3D10_SB_OPCODE_MOV, { Temp(Register, RxMaskMode, 1u << Component), RxImmediate(0) });
    return Register;
}

UINT
RxLowering::GatherRange(
    _In_ llvm::CallInst *Call,
    _In_ UINT First,
    _In_ UINT Count)
{
    UINT Register = Fresh4();

    for (UINT Component = 0; Component < 4; Component++)
    {
        RxTokens Value = (Component < Count) ? Source(Call->getArgOperand(First + Component)) : RxImmediate(0);

        Emit(D3D10_SB_OPCODE_MOV, { Temp(Register, RxMaskMode, 1u << Component), Value });
    }
    return Register;
}

UINT
RxName(
    _In_ hlsl::Semantic::Kind Kind)
{
    switch (Kind)
    {
        case hlsl::Semantic::Kind::Position: return D3D10_SB_NAME_POSITION;
        case hlsl::Semantic::Kind::ClipDistance: return D3D10_SB_NAME_CLIP_DISTANCE;
        case hlsl::Semantic::Kind::CullDistance: return D3D10_SB_NAME_CULL_DISTANCE;
        case hlsl::Semantic::Kind::RenderTargetArrayIndex: return D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX;
        case hlsl::Semantic::Kind::ViewPortArrayIndex: return D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX;
        case hlsl::Semantic::Kind::VertexID: return D3D10_SB_NAME_VERTEX_ID;
        case hlsl::Semantic::Kind::PrimitiveID: return D3D10_SB_NAME_PRIMITIVE_ID;
        case hlsl::Semantic::Kind::InstanceID: return D3D10_SB_NAME_INSTANCE_ID;
        case hlsl::Semantic::Kind::IsFrontFace: return D3D10_SB_NAME_IS_FRONT_FACE;
        case hlsl::Semantic::Kind::SampleIndex: return D3D10_SB_NAME_SAMPLE_INDEX;
        default: return D3D10_SB_NAME_UNDEFINED;
    }
}

/* Inputs and outputs keep the rows and columns the signature packed them into */
BOOLEAN
RxLowering::Signature(
    _In_ const hlsl::DxilSignature &Elements,
    _In_ BOOLEAN Input)
{
    for (const std::unique_ptr<hlsl::DxilSignatureElement> &Element : Elements.GetElements())
    {
        hlsl::Semantic::Kind Kind = Element->GetKind();
        UINT Name = RxName(Kind);
        UINT Mask = (UINT)Element->GetColsAsMask();

        if (!Input && (Kind == hlsl::Semantic::Kind::Depth || Kind == hlsl::Semantic::Kind::DepthLessEqual ||
                       Kind == hlsl::Semantic::Kind::DepthGreaterEqual))
        {
            Declare(D3D10_SB_OPCODE_DCL_OUTPUT, { { ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH) |
                                                    ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) } });
            continue;
        }
        if (!Input && Kind == hlsl::Semantic::Kind::Coverage)
        {
            Declare(D3D10_SB_OPCODE_DCL_OUTPUT, { { ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT_COVERAGE_MASK) |
                                                    ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) } });
            continue;
        }
        if (Element->GetStartRow() < 0)
            return FALSE;
        for (UINT Row = 0; Row < Element->GetRows(); Row++)
        {
            RxTokens Register = RxRegister(Input ? D3D10_SB_OPERAND_TYPE_INPUT : D3D10_SB_OPERAND_TYPE_OUTPUT, RxMaskMode, Mask,
                                           { (UINT)Element->GetStartRow() + Row });
            UINT Opcode;

            if (Input && PixelShader)
            {
                UINT Mode = (UINT)Element->GetInterpolationMode()->GetKind();

                Opcode = (Name != D3D10_SB_NAME_UNDEFINED) ? D3D10_SB_OPCODE_DCL_INPUT_PS_SIV : D3D10_SB_OPCODE_DCL_INPUT_PS;
                if (Kind == hlsl::Semantic::Kind::IsFrontFace || Kind == hlsl::Semantic::Kind::SampleIndex ||
                    Kind == hlsl::Semantic::Kind::PrimitiveID)
                {
                    Opcode = D3D10_SB_OPCODE_DCL_INPUT_PS_SGV;
                    Mode = D3D10_SB_INTERPOLATION_CONSTANT;
                }
                if (Opcode == D3D10_SB_OPCODE_DCL_INPUT_PS)
                    Declare(Opcode, { Register }, ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(Mode));
                else
                    Declare(Opcode, { Register, { ENCODE_D3D10_SB_NAME(Name) } }, ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(Mode));
            }
            else if (Input)
            {
                Opcode = (Name != D3D10_SB_NAME_UNDEFINED) ? D3D10_SB_OPCODE_DCL_INPUT_SGV : D3D10_SB_OPCODE_DCL_INPUT;
                if (Opcode == D3D10_SB_OPCODE_DCL_INPUT)
                    Declare(Opcode, { Register });
                else
                    Declare(Opcode, { Register, { ENCODE_D3D10_SB_NAME(Name) } });
            }
            else
            {
                Opcode = (Name != D3D10_SB_NAME_UNDEFINED) ? D3D10_SB_OPCODE_DCL_OUTPUT_SIV : D3D10_SB_OPCODE_DCL_OUTPUT;
                if (Opcode == D3D10_SB_OPCODE_DCL_OUTPUT)
                    Declare(Opcode, { Register });
                else
                    Declare(Opcode, { Register, { ENCODE_D3D10_SB_NAME(Name) } });
            }
        }
    }
    return TRUE;
}

UINT
RxDimension(
    _In_ hlsl::DXIL::ResourceKind Kind)
{
    switch (Kind)
    {
        case hlsl::DXIL::ResourceKind::Texture1D: return D3D10_SB_RESOURCE_DIMENSION_TEXTURE1D;
        case hlsl::DXIL::ResourceKind::Texture2D: return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D;
        case hlsl::DXIL::ResourceKind::Texture2DMS: return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMS;
        case hlsl::DXIL::ResourceKind::Texture3D: return D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D;
        case hlsl::DXIL::ResourceKind::TextureCube: return D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE;
        case hlsl::DXIL::ResourceKind::Texture1DArray: return D3D10_SB_RESOURCE_DIMENSION_TEXTURE1DARRAY;
        case hlsl::DXIL::ResourceKind::Texture2DArray: return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY;
        case hlsl::DXIL::ResourceKind::Texture2DMSArray: return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMSARRAY;
        case hlsl::DXIL::ResourceKind::TextureCubeArray: return D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBEARRAY;
        case hlsl::DXIL::ResourceKind::TypedBuffer: return D3D10_SB_RESOURCE_DIMENSION_BUFFER;
        default: return D3D10_SB_RESOURCE_DIMENSION_UNKNOWN;
    }
}

UINT
RxReturnType(
    _In_ hlsl::CompType Type)
{
    switch (Type.GetKind())
    {
        case hlsl::CompType::Kind::UNormF32:
        case hlsl::CompType::Kind::UNormF16: return D3D10_SB_RETURN_TYPE_UNORM;
        case hlsl::CompType::Kind::SNormF32:
        case hlsl::CompType::Kind::SNormF16: return D3D10_SB_RETURN_TYPE_SNORM;
        case hlsl::CompType::Kind::I32:
        case hlsl::CompType::Kind::I16: return D3D10_SB_RETURN_TYPE_SINT;
        case hlsl::CompType::Kind::U32:
        case hlsl::CompType::Kind::U16: return D3D10_SB_RETURN_TYPE_UINT;
        default: return D3D10_SB_RETURN_TYPE_FLOAT;
    }
}

/* Resources declared in space 0 keep their register numbers */
/*
 * Executor slots for one resource range. Space 0 ranges keep their registers as slots;
 * other spaces, and registers past the executor's slots, take slots from the top down.
 */
UINT
RxLowering::Place(
    _In_ UINT Class,
    _In_ UINT Range,
    _In_ UINT Space,
    _In_ UINT Lower,
    _In_ UINT Count,
    _In_ UINT Limit)
{
    UINT Base;

    if (Count > Limit)
        return UINT_MAX;
    if (Space == 0 && Lower <= Limit - Count)
    {
        Base = Lower;
    }
    else
    {
        if (HighSlot[Class] < Count)
            return UINT_MAX;
        HighSlot[Class] -= Count;
        Base = HighSlot[Class];
        for (UINT Index = 0; Index < Count; Index++)
            Sources.insert(Sources.end(), { Class, Base + Index, Space, Lower + Index });
    }
    Bases[((UINT64)Class << 32) | Range] = Base;
    return Base;
}

BOOLEAN
RxLowering::Resources(VOID)
{
    for (const std::unique_ptr<hlsl::DxilCBuffer> &Buffer : Dxil.GetCBuffers())
    {
        UINT Base = Place((UINT)hlsl::DXIL::ResourceClass::CBuffer, Buffer->GetID(), Buffer->GetSpaceID(), Buffer->GetLowerBound(),
                          Buffer->GetRangeSize(), 14);

        if (Base == UINT_MAX)
            return FALSE;
        for (UINT Index = 0; Index < Buffer->GetRangeSize(); Index++)
            Declare(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER, { RxRegister(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, RxSwizzleMode, RxXyzw,
                                                                      { Base + Index, (Buffer->GetSize() + 15) / 16 }) });
    }
    for (const std::unique_ptr<hlsl::DxilSampler> &Sampler : Dxil.GetSamplers())
    {
        UINT Mode = (Sampler->GetSamplerKind() == hlsl::DXIL::SamplerKind::Comparison) ? D3D10_SB_SAMPLER_MODE_COMPARISON :
                                                                                         D3D10_SB_SAMPLER_MODE_DEFAULT;

        UINT Base = Place((UINT)hlsl::DXIL::ResourceClass::Sampler, Sampler->GetID(), Sampler->GetSpaceID(), Sampler->GetLowerBound(),
                          Sampler->GetRangeSize(), 16);

        if (Base == UINT_MAX)
            return FALSE;
        for (UINT Index = 0; Index < Sampler->GetRangeSize(); Index++)
            Declare(D3D10_SB_OPCODE_DCL_SAMPLER, { RxBare(D3D10_SB_OPERAND_TYPE_SAMPLER, { Base + Index }) }, ENCODE_D3D10_SB_SAMPLER_MODE(Mode));
    }
    for (UINT Pass = 0; Pass < 2; Pass++)
    {
        const std::vector<std::unique_ptr<hlsl::DxilResource>> &List = Pass ? Dxil.GetUAVs() : Dxil.GetSRVs();
        UINT Type = Pass ? D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW : D3D10_SB_OPERAND_TYPE_RESOURCE;

        for (const std::unique_ptr<hlsl::DxilResource> &Resource : List)
        {
            hlsl::DXIL::ResourceKind Kind = Resource->GetKind();
            UINT Return = RxReturnType(Resource->GetCompType());
            RxTokens Returns = { ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(Return, 0) | ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(Return, 1) |
                                 ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(Return, 2) | ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(Return, 3) };

            UINT Base = Place(Pass ? (UINT)hlsl::DXIL::ResourceClass::UAV : (UINT)hlsl::DXIL::ResourceClass::SRV, Resource->GetID(),
                              Resource->GetSpaceID(), Resource->GetLowerBound(), Resource->GetRangeSize(), Pass ? 64 : 128);

            if (Base == UINT_MAX)
                return FALSE;
            for (UINT Index = 0; Index < Resource->GetRangeSize(); Index++)
            {
                RxTokens Register = RxBare(Type, { Base + Index });

                if (Kind == hlsl::DXIL::ResourceKind::RawBuffer)
                    Declare(Pass ? D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW : D3D11_SB_OPCODE_DCL_RESOURCE_RAW, { Register });
                else if (Kind == hlsl::DXIL::ResourceKind::StructuredBuffer)
                    Declare(Pass ? D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED : D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED,
                            { Register, { Resource->GetElementStride() } });
                else if (RxDimension(Kind) == D3D10_SB_RESOURCE_DIMENSION_UNKNOWN)
                    return FALSE;
                else
                    Declare(Pass ? D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED : D3D10_SB_OPCODE_DCL_RESOURCE, { Register, Returns },
                            ENCODE_D3D10_SB_RESOURCE_DIMENSION(RxDimension(Kind)) |
                            ENCODE_D3D10_SB_RESOURCE_SAMPLE_COUNT(Resource->GetSampleCount()));
            }
        }
    }
    return HeapResources();
}

/* Each annotated descriptor heap handle takes a slot from the top, declared from its properties */
BOOLEAN
RxLowering::HeapResources(VOID)
{
    for (llvm::BasicBlock &Block : *Dxil.GetEntryFunction())
    {
        for (llvm::Instruction &Instruction : Block)
        {
            llvm::CallInst *Call = llvm::dyn_cast<llvm::CallInst>(&Instruction);
            llvm::CallInst *Created;
            hlsl::DxilResourceProperties Properties;
            RX_HEAP_SLOT Heap;
            RxTokens Register;

            if (Call == nullptr || !hlsl::OP::IsDxilOpFuncCallInst(Call) || hlsl::OP::getOpCode(Call) != OpCode::AnnotateHandle)
                continue;
            Created = llvm::dyn_cast<llvm::CallInst>(Call->getArgOperand(1));
            if (Created == nullptr || !hlsl::OP::IsDxilOpFuncCallInst(Created) || hlsl::OP::getOpCode(Created) != OpCode::CreateHandleFromHeap)
                continue;
            Properties = hlsl::resource_helper::loadPropsFromConstant(*llvm::cast<llvm::Constant>(Call->getArgOperand(2)));
            Heap.Class = (UINT)Properties.getResourceClass();
            Heap.Kind = (UINT)Properties.getResourceKind();
            Heap.Index = Created->getArgOperand(1);
            if (HighSlot[Heap.Class] == 0)
                return FALSE;
            HighSlot[Heap.Class] -= 1;
            Heap.Slot = HighSlot[Heap.Class];
            HeapSlots[Call] = Heap;

            switch ((hlsl::DXIL::ResourceClass)Heap.Class)
            {
                case hlsl::DXIL::ResourceClass::CBuffer:
                    Declare(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER, { RxRegister(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, RxSwizzleMode, RxXyzw,
                                                                              { Heap.Slot, (Properties.CBufferSizeInBytes + 15) / 16 }) });
                    break;
                case hlsl::DXIL::ResourceClass::Sampler:
                    Declare(D3D10_SB_OPCODE_DCL_SAMPLER, { RxBare(D3D10_SB_OPERAND_TYPE_SAMPLER, { Heap.Slot }) },
                            ENCODE_D3D10_SB_SAMPLER_MODE(Properties.Basic.SamplerCmpOrHasCounter ? D3D10_SB_SAMPLER_MODE_COMPARISON :
                                                                                                    D3D10_SB_SAMPLER_MODE_DEFAULT));
                    break;
                default:
                {
                    BOOLEAN Access = (Heap.Class == (UINT)hlsl::DXIL::ResourceClass::UAV);
                    hlsl::DXIL::ResourceKind Kind = (hlsl::DXIL::ResourceKind)Heap.Kind;
                    UINT Return = (Kind == hlsl::DXIL::ResourceKind::RawBuffer || Kind == hlsl::DXIL::ResourceKind::StructuredBuffer) ? 0 :
                                  RxReturnType(hlsl::CompType(Properties.getCompType()));
                    RxTokens Returns = { ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(Return, 0) | ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(Return, 1) |
                                         ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(Return, 2) | ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(Return, 3) };

                    Register = RxBare(Access ? D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW : D3D10_SB_OPERAND_TYPE_RESOURCE, { Heap.Slot });
                    if (Kind == hlsl::DXIL::ResourceKind::RawBuffer)
                        Declare(Access ? D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW : D3D11_SB_OPCODE_DCL_RESOURCE_RAW, { Register });
                    else if (Kind == hlsl::DXIL::ResourceKind::StructuredBuffer)
                        Declare(Access ? D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED : D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED,
                                { Register, { Properties.getElementStride() } });
                    else if (RxDimension(Kind) == D3D10_SB_RESOURCE_DIMENSION_UNKNOWN)
                        return FALSE;
                    else
                        Declare(Access ? D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED : D3D10_SB_OPCODE_DCL_RESOURCE, { Register, Returns },
                                ENCODE_D3D10_SB_RESOURCE_DIMENSION(RxDimension(Kind)) |
                                ENCODE_D3D10_SB_RESOURCE_SAMPLE_COUNT(Properties.Typed.SampleCount));
                    break;
                }
            }
        }
    }
    return TRUE;
}

BOOLEAN
RxLowering::Declarations(VOID)
{
    const hlsl::ShaderModel *Model = Dxil.GetShaderModel();

    PixelShader = Model->IsPS();
    if (!Resources())
        return FALSE;
    if (!Model->IsCS() && (!Signature(Dxil.GetInputSignature(), TRUE) || !Signature(Dxil.GetOutputSignature(), FALSE)))
        return FALSE;
    if (Model->IsCS())
        Decls.insert(Decls.end(), { ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DCL_THREAD_GROUP) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4),
                                    Dxil.GetNumThreads(0), Dxil.GetNumThreads(1), Dxil.GetNumThreads(2) });

    /* Group shared arrays become raw g# regions */
    for (llvm::GlobalVariable &Global : Dxil.GetModule()->globals())
    {
        llvm::PointerType *Pointer = Global.getType();
        UINT Bytes;

        if (Pointer->getAddressSpace() != hlsl::DXIL::kTGSMAddrSpace)
            continue;
        Bytes = (UINT)Dxil.GetModule()->getDataLayout().getTypeAllocSize(Pointer->getElementType());
        SharedRegisters[&Global] = (UINT)SharedRegisters.size();
        Decls.insert(Decls.end(), { ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_RAW) |
                                    ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4),
                                    RxOperandHead(D3D11_SB_OPERAND_TYPE_THREAD_GROUP_SHARED_MEMORY, 1) |
                                    ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_0_COMPONENT),
                                    SharedRegisters[&Global], (Bytes + 3) & ~3u });
    }
    return TRUE;
}

BOOLEAN
RxLowering::HandleOf(
    _In_ llvm::Value *Value,
    _Out_ RX_HANDLE *Handle)
{
    llvm::CallInst *Call = llvm::dyn_cast<llvm::CallInst>(Value);
    llvm::Value *Register;
    llvm::ConstantInt *Index;
    UINT Range;
    const hlsl::DxilResourceBase *Base;

    ZeroMemory(Handle, sizeof(*Handle));

    auto Heap = HeapSlots.find(Value);

    if (Heap != HeapSlots.end())
    {
        Handle->Class = Heap->second.Class;
        Handle->Slot = Heap->second.Slot;
        Handle->Kind = Heap->second.Kind;
        Handle->Count = 1;
        Handle->Heap = Heap->second.Index;
        return TRUE;
    }

    /* Shader model 6.6 handles carry their properties in an annotation around the binding */
    while (Call != nullptr && hlsl::OP::IsDxilOpFuncCallInst(Call) && hlsl::OP::getOpCode(Call) == OpCode::AnnotateHandle)
        Call = llvm::dyn_cast<llvm::CallInst>(Call->getArgOperand(1));
    if (Call == nullptr || !hlsl::OP::IsDxilOpFuncCallInst(Call))
        return FALSE;
    if (hlsl::OP::getOpCode(Call) == OpCode::CreateHandle)
    {
        Handle->Class = (UINT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(1))->getZExtValue();
        Range = (UINT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(2))->getZExtValue();
        Register = Call->getArgOperand(3);
    }
    else if (hlsl::OP::getOpCode(Call) == OpCode::CreateHandleFromBinding)
    {
        if (!BindingRange(llvm::cast<llvm::Constant>(Call->getArgOperand(1)), &Handle->Class, &Range))
            return FALSE;
        Register = Call->getArgOperand(2);
    }
    else
    {
        return FALSE;
    }
    Index = llvm::dyn_cast<llvm::ConstantInt>(Register);
    if (Bases.count(((UINT64)Handle->Class << 32) | Range) == 0)
        return FALSE;
    switch ((hlsl::DXIL::ResourceClass)Handle->Class)
    {
        case hlsl::DXIL::ResourceClass::SRV:
            Base = &Dxil.GetSRV(Range);
            Handle->Kind = (UINT)Dxil.GetSRV(Range).GetKind();
            break;
        case hlsl::DXIL::ResourceClass::UAV:
            Base = &Dxil.GetUAV(Range);
            Handle->Kind = (UINT)Dxil.GetUAV(Range).GetKind();
            break;
        case hlsl::DXIL::ResourceClass::CBuffer:
            Base = &Dxil.GetCBuffer(Range);
            break;
        default:
            Base = &Dxil.GetSampler(Range);
            break;
    }
    Handle->Lower = Base->GetLowerBound();
    Handle->Count = Base->GetRangeSize();
    Handle->Slot = Bases[((UINT64)Handle->Class << 32) | Range];

    /* A lowering of one case of a computed index fixes the slot */
    auto Fixed = Overrides.find(Value);

    if (Fixed != Overrides.end())
    {
        Handle->Slot = Fixed->second;
        return TRUE;
    }
    if (Index == nullptr)
    {
        Handle->Index = Register;
        return TRUE;
    }
    if ((UINT)Index->getZExtValue() >= Handle->Lower)
        Handle->Slot += (UINT)Index->getZExtValue() - Handle->Lower;
    else
        Handle->Slot += (UINT)Index->getZExtValue();
    return TRUE;
}

/* The declared range a shader model 6.6 binding names, found by class, space and first register */
BOOLEAN
RxLowering::BindingRange(
    _In_ llvm::Constant *Binding,
    _Out_ UINT *Class,
    _Out_ UINT *Range)
{
    UINT Lower = (UINT)llvm::cast<llvm::ConstantInt>(Binding->getAggregateElement(0u))->getZExtValue();
    UINT Space = (UINT)llvm::cast<llvm::ConstantInt>(Binding->getAggregateElement(2u))->getZExtValue();

    *Class = (UINT)llvm::cast<llvm::ConstantInt>(Binding->getAggregateElement(3u))->getZExtValue();
    *Range = 0;
    switch ((hlsl::DXIL::ResourceClass)*Class)
    {
        case hlsl::DXIL::ResourceClass::SRV:
            for (const std::unique_ptr<hlsl::DxilResource> &Resource : Dxil.GetSRVs())
            {
                if (Resource->GetLowerBound() == Lower && Resource->GetSpaceID() == Space)
                {
                    *Range = Resource->GetID();
                    return TRUE;
                }
            }
            return FALSE;
        case hlsl::DXIL::ResourceClass::UAV:
            for (const std::unique_ptr<hlsl::DxilResource> &Resource : Dxil.GetUAVs())
            {
                if (Resource->GetLowerBound() == Lower && Resource->GetSpaceID() == Space)
                {
                    *Range = Resource->GetID();
                    return TRUE;
                }
            }
            return FALSE;
        case hlsl::DXIL::ResourceClass::CBuffer:
            for (const std::unique_ptr<hlsl::DxilCBuffer> &Resource : Dxil.GetCBuffers())
            {
                if (Resource->GetLowerBound() == Lower && Resource->GetSpaceID() == Space)
                {
                    *Range = Resource->GetID();
                    return TRUE;
                }
            }
            return FALSE;
        default:
            for (const std::unique_ptr<hlsl::DxilSampler> &Resource : Dxil.GetSamplers())
            {
                if (Resource->GetLowerBound() == Lower && Resource->GetSpaceID() == Space)
                {
                    *Range = Resource->GetID();
                    return TRUE;
                }
            }
            return FALSE;
    }
}

/*
 * A resource picked by a computed index: one test per register of its range, each lowering
 * the operation with that register's slot. Lanes with an index past the range do nothing.
 */
BOOLEAN
RxLowering::LowerDynamic(
    _In_ llvm::CallInst *Call,
    _In_ llvm::Value *Handle,
    _In_ const RX_HANDLE &Range)
{
    UINT Relative = Fresh();
    BOOLEAN Lowered = TRUE;

    if (Range.Count > 64)
        return FALSE;
    Emit(D3D10_SB_OPCODE_IADD, { Temp(Relative / 4, RxMaskMode, 1u << (Relative & 3)), Source(Range.Index), RxImmediate(0u - Range.Lower) });
    for (UINT Index = 0; Index < Range.Count && Lowered; Index++)
    {
        UINT Test = Fresh();

        Emit(D3D10_SB_OPCODE_IEQ, { Temp(Test / 4, RxMaskMode, 1u << (Test & 3)), Temp(Relative / 4, RxSelectMode, Relative & 3),
                                    RxImmediate(Index) });
        Emit(D3D10_SB_OPCODE_IF, { Temp(Test / 4, RxSelectMode, Test & 3) }, ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(D3D10_SB_INSTRUCTION_TEST_NONZERO));
        Overrides[Handle] = Range.Slot + Index;
        Lowered = LowerCall(Call);
        Overrides.erase(Handle);
        Emit(D3D10_SB_OPCODE_ENDIF, {});
    }
    return Lowered;
}

/* The executor's opcode for each LLVM operator, and whether the operands swap or the second negates */
typedef struct _RX_BINARY
{
    UINT Opcode;
    BOOLEAN Swap;
    BOOLEAN Negate;
} RX_BINARY;

BOOLEAN
RxBinaryOf(
    _In_ llvm::Instruction *Instruction,
    _Out_ RX_BINARY *Binary)
{
    *Binary = { 0, FALSE, FALSE };
    switch (Instruction->getOpcode())
    {
        case llvm::Instruction::FAdd: Binary->Opcode = D3D10_SB_OPCODE_ADD; return TRUE;
        case llvm::Instruction::FSub: Binary->Opcode = D3D10_SB_OPCODE_ADD; Binary->Negate = TRUE; return TRUE;
        case llvm::Instruction::FMul: Binary->Opcode = D3D10_SB_OPCODE_MUL; return TRUE;
        case llvm::Instruction::FDiv: Binary->Opcode = D3D10_SB_OPCODE_DIV; return TRUE;
        case llvm::Instruction::Add: Binary->Opcode = D3D10_SB_OPCODE_IADD; return TRUE;
        case llvm::Instruction::Sub: Binary->Opcode = D3D10_SB_OPCODE_IADD; Binary->Negate = TRUE; return TRUE;
        case llvm::Instruction::Shl: Binary->Opcode = D3D10_SB_OPCODE_ISHL; return TRUE;
        case llvm::Instruction::LShr: Binary->Opcode = D3D10_SB_OPCODE_USHR; return TRUE;
        case llvm::Instruction::AShr: Binary->Opcode = D3D10_SB_OPCODE_ISHR; return TRUE;
        case llvm::Instruction::And: Binary->Opcode = D3D10_SB_OPCODE_AND; return TRUE;
        case llvm::Instruction::Or: Binary->Opcode = D3D10_SB_OPCODE_OR; return TRUE;
        case llvm::Instruction::Xor: Binary->Opcode = D3D10_SB_OPCODE_XOR; return TRUE;
        default: break;
    }
    if (llvm::ICmpInst *Compare = llvm::dyn_cast<llvm::ICmpInst>(Instruction))
    {
        switch (Compare->getPredicate())
        {
            case llvm::CmpInst::ICMP_EQ: Binary->Opcode = D3D10_SB_OPCODE_IEQ; return TRUE;
            case llvm::CmpInst::ICMP_NE: Binary->Opcode = D3D10_SB_OPCODE_INE; return TRUE;
            case llvm::CmpInst::ICMP_SLT: Binary->Opcode = D3D10_SB_OPCODE_ILT; return TRUE;
            case llvm::CmpInst::ICMP_SGE: Binary->Opcode = D3D10_SB_OPCODE_IGE; return TRUE;
            case llvm::CmpInst::ICMP_SGT: Binary->Opcode = D3D10_SB_OPCODE_ILT; Binary->Swap = TRUE; return TRUE;
            case llvm::CmpInst::ICMP_SLE: Binary->Opcode = D3D10_SB_OPCODE_IGE; Binary->Swap = TRUE; return TRUE;
            case llvm::CmpInst::ICMP_ULT: Binary->Opcode = D3D10_SB_OPCODE_ULT; return TRUE;
            case llvm::CmpInst::ICMP_UGE: Binary->Opcode = D3D10_SB_OPCODE_UGE; return TRUE;
            case llvm::CmpInst::ICMP_UGT: Binary->Opcode = D3D10_SB_OPCODE_ULT; Binary->Swap = TRUE; return TRUE;
            case llvm::CmpInst::ICMP_ULE: Binary->Opcode = D3D10_SB_OPCODE_UGE; Binary->Swap = TRUE; return TRUE;
            default: return FALSE;
        }
    }
    if (llvm::FCmpInst *Compare = llvm::dyn_cast<llvm::FCmpInst>(Instruction))
    {
        switch (Compare->getPredicate())
        {
            case llvm::CmpInst::FCMP_OEQ:
            case llvm::CmpInst::FCMP_UEQ: Binary->Opcode = D3D10_SB_OPCODE_EQ; return TRUE;
            case llvm::CmpInst::FCMP_ONE:
            case llvm::CmpInst::FCMP_UNE: Binary->Opcode = D3D10_SB_OPCODE_NE; return TRUE;
            case llvm::CmpInst::FCMP_OLT:
            case llvm::CmpInst::FCMP_ULT: Binary->Opcode = D3D10_SB_OPCODE_LT; return TRUE;
            case llvm::CmpInst::FCMP_OGE:
            case llvm::CmpInst::FCMP_UGE: Binary->Opcode = D3D10_SB_OPCODE_GE; return TRUE;
            case llvm::CmpInst::FCMP_OGT:
            case llvm::CmpInst::FCMP_UGT: Binary->Opcode = D3D10_SB_OPCODE_LT; Binary->Swap = TRUE; return TRUE;
            case llvm::CmpInst::FCMP_OLE:
            case llvm::CmpInst::FCMP_ULE: Binary->Opcode = D3D10_SB_OPCODE_GE; Binary->Swap = TRUE; return TRUE;
            default: return FALSE;
        }
    }
    return FALSE;
}

BOOLEAN
RxLowering::Lower(
    _In_ llvm::Instruction *Instruction)
{
    if (llvm::isa<llvm::PHINode>(Instruction) || llvm::isa<llvm::TerminatorInst>(Instruction))
        return TRUE;
    if (llvm::CallInst *Call = llvm::dyn_cast<llvm::CallInst>(Instruction))
    {
        if (!LowerCall(Call))
            return FALSE;
        Narrow(Call);
        return TRUE;
    }
    if (!Instruction->getType()->isVoidTy() && !Instruction->getType()->isFloatTy() && !Instruction->getType()->isIntegerTy(32) &&
        !Instruction->getType()->isIntegerTy(1) && !Instruction->getType()->isStructTy() && !Instruction->getType()->isPointerTy() &&
        !Instruction->getType()->isHalfTy() && !Instruction->getType()->isIntegerTy(16) && !RxIsWide(Instruction->getType()))
    {
        return FALSE;
    }
    if (!LowerPlain(Instruction))
        return FALSE;
    Narrow(Instruction);
    return TRUE;
}

/*
 * 16 bit values sit in 32 bit components: halves as the float they round to, 16 bit
 * integers zero extended. Every operation that makes one rounds or wraps its result.
 */
VOID
RxLowering::Narrow(
    _In_ llvm::Value *Value)
{
    UINT Bits;

    if (Value->getType()->isHalfTy())
    {
        Bits = Fresh();
        Emit(D3D11_SB_OPCODE_F32TOF16, { Temp(Bits / 4, RxMaskMode, 1u << (Bits & 3)), Source(Value) });
        Emit(D3D11_SB_OPCODE_F16TOF32, { Dest(Value), Temp(Bits / 4, RxSelectMode, Bits & 3) });
    }
    else if (Value->getType()->isIntegerTy(16))
    {
        Emit(D3D10_SB_OPCODE_AND, { Dest(Value), Source(Value), RxImmediate(0xFFFF) });
    }
}

/* A 16 bit integer sign extended for operations that read it as signed; wider values as they are */
RxTokens
RxLowering::SignedSource(
    _In_ llvm::Value *Value)
{
    UINT Wide;

    if (!Value->getType()->isIntegerTy(16) || llvm::isa<llvm::Constant>(Value))
    {
        if (llvm::ConstantInt *Integer = llvm::dyn_cast<llvm::ConstantInt>(Value))
            return RxImmediate((UINT)Integer->getSExtValue());
        return Source(Value);
    }
    Wide = Fresh();
    Emit(D3D11_SB_OPCODE_IBFE, { Temp(Wide / 4, RxMaskMode, 1u << (Wide & 3)), RxImmediate(16), RxImmediate(0), Source(Value) });
    return Temp(Wide / 4, RxSelectMode, Wide & 3);
}

/*
 * Doubles run on the executor's double operations and 64 bit integers on its long operation;
 * bitwise logic and moves work on both words at once.
 */
BOOLEAN
RxLowering::LowerWide(
    _In_ llvm::Instruction *Instruction)
{
    llvm::Value *First = Instruction->getOperand(0);
    llvm::Value *Second = (Instruction->getNumOperands() > 1) ? Instruction->getOperand(1) : nullptr;
    BOOLEAN Real = First->getType()->isDoubleTy();
    UINT Opcode = Instruction->getOpcode();
    UINT Kind = UINT_MAX;

    switch (Opcode)
    {
        case llvm::Instruction::FAdd:
        case llvm::Instruction::FSub:
            Emit(D3D11_SB_OPCODE_DADD, { Dest(Instruction), Source(First), (Opcode == llvm::Instruction::FSub) ?
                                         RxModified(Source(Second), D3D10_SB_OPERAND_MODIFIER_NEG) : Source(Second) });
            return TRUE;
        case llvm::Instruction::FMul:
            Emit(D3D11_SB_OPCODE_DMUL, { Dest(Instruction), Source(First), Source(Second) });
            return TRUE;
        case llvm::Instruction::FDiv:
            Emit(D3D11_1_SB_OPCODE_DDIV, { Dest(Instruction), Source(First), Source(Second) });
            return TRUE;
        case llvm::Instruction::And:
        case llvm::Instruction::Or:
        case llvm::Instruction::Xor:
            Emit((Opcode == llvm::Instruction::And) ? D3D10_SB_OPCODE_AND : (Opcode == llvm::Instruction::Or) ? D3D10_SB_OPCODE_OR :
                                                                                                              D3D10_SB_OPCODE_XOR,
                 { Dest(Instruction), Source(First), Source(Second) });
            return TRUE;
        case llvm::Instruction::Add: Kind = RxWarpLongAdd; break;
        case llvm::Instruction::Sub: Kind = RxWarpLongSubtract; break;
        case llvm::Instruction::Mul: Kind = RxWarpLongMultiply; break;
        case llvm::Instruction::UDiv: Kind = RxWarpLongDivide; break;
        case llvm::Instruction::URem: Kind = RxWarpLongRemainder; break;
        case llvm::Instruction::SDiv: Kind = RxWarpLongSignedDivide; break;
        case llvm::Instruction::SRem: Kind = RxWarpLongSignedRemainder; break;
        case llvm::Instruction::Shl: Kind = RxWarpLongShiftLeft; break;
        case llvm::Instruction::LShr: Kind = RxWarpLongShiftRight; break;
        case llvm::Instruction::AShr: Kind = RxWarpLongSignedShiftRight; break;
        case llvm::Instruction::Select:
            Emit(D3D10_SB_OPCODE_MOVC, { Dest(Instruction), Source(First), Source(Second), Source(Instruction->getOperand(2)) });
            return TRUE;
        case llvm::Instruction::BitCast:
            Emit(D3D10_SB_OPCODE_MOV, { Dest(Instruction), Source(First) });
            return TRUE;
        case llvm::Instruction::FPExt:
            Emit(D3D11_SB_OPCODE_FTOD, { Dest(Instruction), Source(First) });
            return TRUE;
        case llvm::Instruction::FPTrunc:
            Emit(D3D11_SB_OPCODE_DTOF, { Dest(Instruction), Source(First) });
            return TRUE;
        case llvm::Instruction::Trunc:
            if (Instruction->getType()->isIntegerTy(1))
            {
                UINT Low = Fresh();

                Emit(D3D10_SB_OPCODE_AND, { SlotDest(Low, FALSE), Half(First, FALSE), RxImmediate(1) });
                Emit(D3D10_SB_OPCODE_INE, { Dest(Instruction), SlotSource(Low, FALSE), RxImmediate(0) });
                return TRUE;
            }
            Emit(D3D10_SB_OPCODE_MOV, { Dest(Instruction), Half(First, FALSE) });
            return TRUE;
        case llvm::Instruction::ZExt:
        case llvm::Instruction::SExt:
        {
            RxTokens Narrow = (Opcode == llvm::Instruction::SExt) ? SignedSource(First) : Source(First);

            if (First->getType()->isIntegerTy(1) && Opcode == llvm::Instruction::ZExt)
            {
                UINT Low = Fresh();

                Emit(D3D10_SB_OPCODE_AND, { SlotDest(Low, FALSE), Source(First), RxImmediate(1) });
                Narrow = SlotSource(Low, FALSE);
            }
            EmitLong(Instruction, (Opcode == llvm::Instruction::SExt) ? RxWarpLongSignExtend : RxWarpLongZeroExtend, Narrow);
            return TRUE;
        }
        case llvm::Instruction::SIToFP:
        case llvm::Instruction::UIToFP:
        {
            BOOLEAN Signed = Opcode == llvm::Instruction::SIToFP;

            if (RxIsWide(First->getType()))
            {
                if (Instruction->getType()->isDoubleTy())
                    EmitLong(Instruction, Signed ? RxWarpLongSignedToDouble : RxWarpLongToDouble, Source(First));
                else
                    EmitLong(Instruction, Signed ? RxWarpLongSignedToFloat : RxWarpLongToFloat, Source(First));
                return TRUE;
            }
            if (First->getType()->isIntegerTy(1))
            {
                UINT Low = Fresh();

                Emit(D3D10_SB_OPCODE_AND, { SlotDest(Low, FALSE), Source(First), RxImmediate(1) });
                Emit(D3D11_1_SB_OPCODE_UTOD, { Dest(Instruction), SlotSource(Low, FALSE) });
                return TRUE;
            }
            Emit(Signed ? D3D11_1_SB_OPCODE_ITOD : D3D11_1_SB_OPCODE_UTOD, { Dest(Instruction), Signed ? SignedSource(First) : Source(First) });
            return TRUE;
        }
        case llvm::Instruction::FPToSI:
        case llvm::Instruction::FPToUI:
        {
            BOOLEAN Signed = Opcode == llvm::Instruction::FPToSI;

            if (RxIsWide(Instruction->getType()))
            {
                if (Real)
                    EmitLong(Instruction, Signed ? RxWarpLongSignedFromDouble : RxWarpLongFromDouble, Source(First));
                else
                    EmitLong(Instruction, Signed ? RxWarpLongSignedFromFloat : RxWarpLongFromFloat, Source(First));
                return TRUE;
            }
            Emit(Signed ? D3D11_1_SB_OPCODE_DTOI : D3D11_1_SB_OPCODE_DTOU, { Dest(Instruction), Source(First) });
            return TRUE;
        }
        default:
            break;
    }

    if (llvm::FCmpInst *Compare = llvm::dyn_cast<llvm::FCmpInst>(Instruction))
    {
        BOOLEAN Swap = FALSE;
        UINT Test;

        switch (Compare->getPredicate())
        {
            case llvm::CmpInst::FCMP_OEQ:
            case llvm::CmpInst::FCMP_UEQ: Test = D3D11_SB_OPCODE_DEQ; break;
            case llvm::CmpInst::FCMP_ONE:
            case llvm::CmpInst::FCMP_UNE: Test = D3D11_SB_OPCODE_DNE; break;
            case llvm::CmpInst::FCMP_OLT:
            case llvm::CmpInst::FCMP_ULT: Test = D3D11_SB_OPCODE_DLT; break;
            case llvm::CmpInst::FCMP_OGE:
            case llvm::CmpInst::FCMP_UGE: Test = D3D11_SB_OPCODE_DGE; break;
            case llvm::CmpInst::FCMP_OGT:
            case llvm::CmpInst::FCMP_UGT: Test = D3D11_SB_OPCODE_DLT; Swap = TRUE; break;
            case llvm::CmpInst::FCMP_OLE:
            case llvm::CmpInst::FCMP_ULE: Test = D3D11_SB_OPCODE_DGE; Swap = TRUE; break;
            default: return FALSE;
        }
        Emit(Test, { Dest(Instruction), Source(Swap ? Second : First), Source(Swap ? First : Second) });
        return TRUE;
    }
    if (llvm::ICmpInst *Compare = llvm::dyn_cast<llvm::ICmpInst>(Instruction))
    {
        BOOLEAN Swap = FALSE;

        switch (Compare->getPredicate())
        {
            case llvm::CmpInst::ICMP_EQ: Kind = RxWarpLongEqual; break;
            case llvm::CmpInst::ICMP_NE: Kind = RxWarpLongNotEqual; break;
            case llvm::CmpInst::ICMP_SLT: Kind = RxWarpLongSignedLess; break;
            case llvm::CmpInst::ICMP_SGE: Kind = RxWarpLongSignedGreaterEqual; break;
            case llvm::CmpInst::ICMP_SGT: Kind = RxWarpLongSignedLess; Swap = TRUE; break;
            case llvm::CmpInst::ICMP_SLE: Kind = RxWarpLongSignedGreaterEqual; Swap = TRUE; break;
            case llvm::CmpInst::ICMP_ULT: Kind = RxWarpLongLess; break;
            case llvm::CmpInst::ICMP_UGE: Kind = RxWarpLongGreaterEqual; break;
            case llvm::CmpInst::ICMP_UGT: Kind = RxWarpLongLess; Swap = TRUE; break;
            case llvm::CmpInst::ICMP_ULE: Kind = RxWarpLongGreaterEqual; Swap = TRUE; break;
            default: return FALSE;
        }
        EmitLong(Instruction, Kind, Source(Swap ? Second : First), Source(Swap ? First : Second));
        return TRUE;
    }
    if (Kind == UINT_MAX)
        return FALSE;
    EmitLong(Instruction, Kind, Source(First), Source(Second));
    return TRUE;
}

/* DXIL operations on doubles and 64 bit integers, and the calls that build and split doubles */
BOOLEAN
RxLowering::LowerWideCall(
    _In_ llvm::CallInst *Call,
    _In_ OpCode Code)
{
    UINT Kind;

    switch (Code)
    {
        case OpCode::MakeDouble:
        {
            UINT Slot = SlotOf(Call);

            Emit(D3D10_SB_OPCODE_MOV, { Temp(Slot / 4, RxMaskMode, 1u << (Slot & 3)), Source(Call->getArgOperand(1)) });
            Emit(D3D10_SB_OPCODE_MOV, { Temp(Slot / 4, RxMaskMode, 2u << (Slot & 3)), Source(Call->getArgOperand(2)) });
            return TRUE;
        }
        case OpCode::SplitDouble:
            Emit(D3D10_SB_OPCODE_MOV, { Temp(ResultRegister(Call), RxMaskMode, 3), Source(Call->getArgOperand(1)) });
            return TRUE;
        case OpCode::FAbs:
            Emit(D3D11_SB_OPCODE_DMOV, { Dest(Call), RxModified(Source(Call->getArgOperand(1)), D3D10_SB_OPERAND_MODIFIER_ABS) });
            return TRUE;
        case OpCode::FMax:
        case OpCode::FMin:
            Emit((Code == OpCode::FMax) ? D3D11_SB_OPCODE_DMAX : D3D11_SB_OPCODE_DMIN,
                 { Dest(Call), Source(Call->getArgOperand(1)), Source(Call->getArgOperand(2)) });
            return TRUE;
        case OpCode::FMad:
            EmitLong(Call, RxWarpLongFusedDouble, Source(Call->getArgOperand(1)), Source(Call->getArgOperand(2)), Source(Call->getArgOperand(3)));
            return TRUE;
        case OpCode::Fma:
            /* The reference's fma shares dfma's quirk of computing only the first two lanes */
            Emit(D3D11_1_SB_OPCODE_DFMA, { Dest(Call), Source(Call->getArgOperand(1)), Source(Call->getArgOperand(2)), Source(Call->getArgOperand(3)) });
            return TRUE;
        case OpCode::Saturate: Kind = RxWarpLongSaturateDouble; break;
        case OpCode::IMax: Kind = RxWarpLongSignedMax; break;
        case OpCode::IMin: Kind = RxWarpLongSignedMin; break;
        case OpCode::UMax: Kind = RxWarpLongMax; break;
        case OpCode::UMin: Kind = RxWarpLongMin; break;
        case OpCode::Countbits: Kind = RxWarpLongCountBits; break;
        case OpCode::FirstbitHi: Kind = RxWarpLongFirstHigh; break;
        case OpCode::FirstbitLo: Kind = RxWarpLongFirstLow; break;
        case OpCode::FirstbitSHi: Kind = RxWarpLongSignedFirstHigh; break;
        default:
            return FALSE;
    }
    EmitLong(Call, Kind, Source(Call->getArgOperand(1)), (Call->getNumArgOperands() > 2) ? Source(Call->getArgOperand(2)) : RxImmediate(0));
    return TRUE;
}

BOOLEAN
RxLowering::LowerPlain(
    _In_ llvm::Instruction *Instruction)
{
    RX_BINARY Binary;
    UINT Opcode = Instruction->getOpcode();

    /* Arithmetic on or making 64 bit values; memory and aggregates take the common paths */
    if (Opcode != llvm::Instruction::ExtractValue && Opcode != llvm::Instruction::GetElementPtr && Opcode != llvm::Instruction::Alloca &&
        Opcode != llvm::Instruction::Load && Opcode != llvm::Instruction::Store && Instruction->getNumOperands() != 0 &&
        (RxIsWide(Instruction->getType()) || RxIsWide(Instruction->getOperand(0)->getType())))
    {
        return LowerWide(Instruction);
    }
    if (Instruction->getOpcode() == llvm::Instruction::Mul)
    {
        Emit(D3D10_SB_OPCODE_IMUL, { RxBare(D3D10_SB_OPERAND_TYPE_NULL, {}), Dest(Instruction), Source(Instruction->getOperand(0)),
                                     Source(Instruction->getOperand(1)) });
        return TRUE;
    }
    if (Instruction->getOpcode() == llvm::Instruction::UDiv || Instruction->getOpcode() == llvm::Instruction::URem)
    {
        BOOLEAN Quotient = Instruction->getOpcode() == llvm::Instruction::UDiv;

        Emit(D3D10_SB_OPCODE_UDIV, { Quotient ? Dest(Instruction) : RxBare(D3D10_SB_OPERAND_TYPE_NULL, {}),
                                     Quotient ? RxBare(D3D10_SB_OPERAND_TYPE_NULL, {}) : Dest(Instruction),
                                     Source(Instruction->getOperand(0)), Source(Instruction->getOperand(1)) });
        return TRUE;
    }
    if (RxBinaryOf(Instruction, &Binary))
    {
        llvm::Value *First = Instruction->getOperand(Binary.Swap ? 1 : 0);
        llvm::Value *Second = Instruction->getOperand(Binary.Swap ? 0 : 1);
        BOOLEAN Signed = (Binary.Opcode == D3D10_SB_OPCODE_ILT || Binary.Opcode == D3D10_SB_OPCODE_IGE || Binary.Opcode == D3D10_SB_OPCODE_ISHR);
        RxTokens Left = Signed ? SignedSource(First) : Source(First);
        RxTokens Right = (Signed && Binary.Opcode != D3D10_SB_OPCODE_ISHR) ? SignedSource(Second) : Source(Second);

        if (Binary.Negate)
            Right = RxModified(Right, D3D10_SB_OPERAND_MODIFIER_NEG);
        Emit(Binary.Opcode, { Dest(Instruction), Left, Right });
        return TRUE;
    }

    switch (Instruction->getOpcode())
    {
        case llvm::Instruction::Select:
            Emit(D3D10_SB_OPCODE_MOVC, { Dest(Instruction), Source(Instruction->getOperand(0)), Source(Instruction->getOperand(1)),
                                         Source(Instruction->getOperand(2)) });
            return TRUE;
        case llvm::Instruction::BitCast:
            if (Instruction->getType()->isHalfTy())
                Emit(D3D11_SB_OPCODE_F16TOF32, { Dest(Instruction), Source(Instruction->getOperand(0)) });
            else if (Instruction->getOperand(0)->getType()->isHalfTy())
                Emit(D3D11_SB_OPCODE_F32TOF16, { Dest(Instruction), Source(Instruction->getOperand(0)) });
            else
                Emit(D3D10_SB_OPCODE_MOV, { Dest(Instruction), Source(Instruction->getOperand(0)) });
            return TRUE;
        case llvm::Instruction::FPExt:
            Emit(D3D10_SB_OPCODE_MOV, { Dest(Instruction), Source(Instruction->getOperand(0)) });
            return TRUE;
        case llvm::Instruction::FPTrunc:
        {
            /* The reference converts fast math results toward zero and everything else to nearest */
            llvm::FPMathOperator *Math = llvm::dyn_cast<llvm::FPMathOperator>(Instruction->getOperand(0));

            if (Math != nullptr && Math->hasUnsafeAlgebra())
                Emit(RxWarpOpcodeHalfTowardZero, { Dest(Instruction), Source(Instruction->getOperand(0)) });
            else
                Emit(D3D10_SB_OPCODE_MOV, { Dest(Instruction), Source(Instruction->getOperand(0)) });
            return TRUE;
        }
        case llvm::Instruction::ZExt:
            if (Instruction->getOperand(0)->getType()->isIntegerTy(1))
                Emit(D3D10_SB_OPCODE_AND, { Dest(Instruction), Source(Instruction->getOperand(0)), RxImmediate(1) });
            else
                Emit(D3D10_SB_OPCODE_MOV, { Dest(Instruction), Source(Instruction->getOperand(0)) });
            return TRUE;
        case llvm::Instruction::SExt:
            Emit(D3D10_SB_OPCODE_MOV, { Dest(Instruction), SignedSource(Instruction->getOperand(0)) });
            return TRUE;
        case llvm::Instruction::Trunc:
        {
            UINT Low = Fresh();

            if (Instruction->getType()->isIntegerTy(16))
            {
                Emit(D3D10_SB_OPCODE_MOV, { Dest(Instruction), Source(Instruction->getOperand(0)) });
                return TRUE;
            }

            Emit(D3D10_SB_OPCODE_AND, { Temp(Low / 4, RxMaskMode, 1u << (Low & 3)), Source(Instruction->getOperand(0)), RxImmediate(1) });
            Emit(D3D10_SB_OPCODE_INE, { Dest(Instruction), Temp(Low / 4, RxSelectMode, Low & 3), RxImmediate(0) });
            return TRUE;
        }
        case llvm::Instruction::FPToSI:
            Emit(D3D10_SB_OPCODE_FTOI, { Dest(Instruction), Source(Instruction->getOperand(0)) });
            return TRUE;
        case llvm::Instruction::FPToUI:
            Emit(D3D10_SB_OPCODE_FTOU, { Dest(Instruction), Source(Instruction->getOperand(0)) });
            return TRUE;
        case llvm::Instruction::SIToFP:
            Emit(D3D10_SB_OPCODE_ITOF, { Dest(Instruction), SignedSource(Instruction->getOperand(0)) });
            return TRUE;
        case llvm::Instruction::UIToFP:
            if (Instruction->getOperand(0)->getType()->isIntegerTy(1))
            {
                UINT Low = Fresh();

                Emit(D3D10_SB_OPCODE_AND, { Temp(Low / 4, RxMaskMode, 1u << (Low & 3)), Source(Instruction->getOperand(0)), RxImmediate(1) });
                Emit(D3D10_SB_OPCODE_UTOF, { Dest(Instruction), Temp(Low / 4, RxSelectMode, Low & 3) });
                return TRUE;
            }
            Emit(D3D10_SB_OPCODE_UTOF, { Dest(Instruction), Source(Instruction->getOperand(0)) });
            return TRUE;
        case llvm::Instruction::ExtractValue:
        {
            llvm::ExtractValueInst *Extract = llvm::cast<llvm::ExtractValueInst>(Instruction);
            auto Status = StatusSlots.find(Extract->getAggregateOperand());

            if (Extract->getIndices()[0] == 4 && Status != StatusSlots.end())
            {
                Slots[Instruction] = Status->second;
                return TRUE;
            }
            Slots[Instruction] = SlotOf(Extract->getAggregateOperand()) + Extract->getIndices()[0] * (RxIsWide(Instruction->getType()) ? 2 : 1);
            return TRUE;
        }
        case llvm::Instruction::GetElementPtr:
            return TRUE;
        case llvm::Instruction::Alloca:
        {
            llvm::AllocaInst *Alloca = llvm::cast<llvm::AllocaInst>(Instruction);
            const llvm::DataLayout &Layout = Dxil.GetModule()->getDataLayout();
            llvm::ConstantInt *Count = llvm::dyn_cast<llvm::ConstantInt>(Alloca->getArraySize());
            UINT Bytes;
            UINT Register = (UINT)Arrays.size();

            if (Count == nullptr)
                return FALSE;
            Bytes = (UINT)(Layout.getTypeAllocSize(Alloca->getAllocatedType()) * Count->getZExtValue());
            if (Bytes == 0 || (Bytes & 3) != 0)
                return FALSE;
            Arrays[Alloca] = Register;
            Decls.insert(Decls.end(), { ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP) |
                                        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4), Register, Bytes / 4, 4 });
            return TRUE;
        }
        case llvm::Instruction::Load:
        case llvm::Instruction::Store:
            return Memory(Instruction);
        case llvm::Instruction::AtomicRMW:
        case llvm::Instruction::AtomicCmpXchg:
            return SharedAtomic(Instruction);
        default:
            return FALSE;
    }
}

/* Follows casts and element pointers back to the alloca or group shared array they index */
BOOLEAN
RxLowering::Resolve(
    _In_ llvm::Value *Pointer,
    _Out_ RX_POINTER *Place)
{
    const llvm::DataLayout &Layout = Dxil.GetModule()->getDataLayout();

    Place->Base = nullptr;
    Place->Bytes = 0;
    Place->Terms.clear();
    for (;;)
    {
        llvm::GEPOperator *Element;
        llvm::Type *Type;

        if (llvm::isa<llvm::AllocaInst>(Pointer) || llvm::isa<llvm::GlobalVariable>(Pointer))
        {
            Place->Base = Pointer;
            return TRUE;
        }
        if (llvm::isa<llvm::BitCastOperator>(Pointer) || llvm::isa<llvm::AddrSpaceCastInst>(Pointer))
        {
            Pointer = llvm::cast<llvm::User>(Pointer)->getOperand(0);
            continue;
        }
        if (llvm::ConstantExpr *Expression = llvm::dyn_cast<llvm::ConstantExpr>(Pointer))
        {
            if (Expression->getOpcode() == llvm::Instruction::AddrSpaceCast)
            {
                Pointer = Expression->getOperand(0);
                continue;
            }
        }
        Element = llvm::dyn_cast<llvm::GEPOperator>(Pointer);
        if (Element == nullptr)
            return FALSE;

        Type = Element->getPointerOperandType();
        for (UINT Index = 1; Index < Element->getNumOperands(); Index++)
        {
            llvm::Value *Step = Element->getOperand(Index);
            llvm::ConstantInt *Fixed = llvm::dyn_cast<llvm::ConstantInt>(Step);
            UINT Size;

            if (llvm::StructType *Structure = llvm::dyn_cast<llvm::StructType>(Type))
            {
                if (Fixed == nullptr)
                    return FALSE;
                Place->Bytes += (UINT)Layout.getStructLayout(Structure)->getElementOffset((UINT)Fixed->getZExtValue());
                Type = Structure->getElementType((UINT)Fixed->getZExtValue());
                continue;
            }
            Type = llvm::cast<llvm::SequentialType>(Type)->getElementType();
            Size = (UINT)Layout.getTypeAllocSize(Type);
            if (Fixed != nullptr)
                Place->Bytes += (UINT)Fixed->getSExtValue() * Size;
            else
                Place->Terms.push_back({ Step, Size });
        }
        Pointer = Element->getPointerOperand();
    }
}

/* The place's offset in units of Unit bytes: an immediate, or a temp the terms are summed into */
RxTokens
RxLowering::Offset(
    _In_ const RX_POINTER &Place,
    _In_ UINT Unit)
{
    RxTokens Sum = RxImmediate(Place.Bytes / Unit);

    for (const std::pair<llvm::Value *, UINT> &Term : Place.Terms)
    {
        UINT Slot = Fresh();

        Emit(D3D10_SB_OPCODE_IMAD, { Temp(Slot / 4, RxMaskMode, 1u << (Slot & 3)), Source(Term.first), RxImmediate(Term.second / Unit), Sum });
        Sum = Temp(Slot / 4, RxSelectMode, Slot & 3);
    }
    return Sum;
}

/* Loads and stores: allocas live in indexable temps, one element per 32 bits; group shared arrays in raw g# regions */
BOOLEAN
RxLowering::Memory(
    _In_ llvm::Instruction *Instruction)
{
    BOOLEAN Loading = Instruction->getOpcode() == llvm::Instruction::Load;
    llvm::Value *Pointer = Loading ? Instruction->getOperand(0) : Instruction->getOperand(1);
    llvm::Type *Element = llvm::cast<llvm::PointerType>(Pointer->getType())->getElementType();
    UINT64 Size = Dxil.GetModule()->getDataLayout().getTypeAllocSize(Element);
    llvm::Value *Value = Loading ? Instruction : Instruction->getOperand(0);
    RX_POINTER Place;
    RxTokens Address;

    if (!Resolve(Pointer, &Place) || (Size != 4 && Size != 8))
        return FALSE;

    auto Array = Arrays.find(Place.Base);

    if (Array != Arrays.end())
    {
        UINT Mode = Loading ? RxSelectMode : RxMaskMode;
        UINT Bits = Loading ? 0 : 1;

        if ((Place.Bytes & 3) != 0)
            return FALSE;
        for (const std::pair<llvm::Value *, UINT> &Term : Place.Terms)
        {
            if ((Term.second & 3) != 0)
                return FALSE;
        }
        Address = Offset(Place, 4);

        /* A 64 bit element spans two cells, low word first */
        RxTokens Cells[2];

        for (UINT Word = 0; Word < Size / 4; Word++)
        {
            if (Place.Terms.empty())
            {
                Cells[Word] = RxRegister(D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, Mode, Bits, { Array->second, Place.Bytes / 4 + Word });
            }
            else
            {
                Cells[Word] = { RxOperandHead(D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 2) | RxFourComponents(Mode, Bits) |
                                ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE), Array->second, Word };
                Cells[Word].insert(Cells[Word].end(), Address.begin(), Address.end());
            }
        }
        if (Size == 4)
        {
            if (Loading)
                Emit(D3D10_SB_OPCODE_MOV, { Dest(Instruction), Cells[0] });
            else
                Emit(D3D10_SB_OPCODE_MOV, { Cells[0], Source(Value) });
            return TRUE;
        }
        if (Loading)
        {
            UINT Slot = SlotOf(Value);

            Emit(D3D10_SB_OPCODE_MOV, { Temp(Slot / 4, RxMaskMode, 1u << (Slot & 3)), Cells[0] });
            Emit(D3D10_SB_OPCODE_MOV, { Temp(Slot / 4, RxMaskMode, 2u << (Slot & 3)), Cells[1] });
        }
        else if (!Place.Terms.empty())
        {
            Emit(RxWarpOpcodePairScatter, { Cells[0], Cells[1], Source(Value) });
        }
        else
        {
            Emit(D3D10_SB_OPCODE_MOV, { Cells[0], Half(Value, FALSE) });
            Emit(D3D10_SB_OPCODE_MOV, { Cells[1], Half(Value, TRUE) });
        }
        return TRUE;
    }

    auto Shared = SharedRegisters.find(Place.Base);

    if (Shared == SharedRegisters.end())
        return FALSE;
    Address = Offset(Place, 1);
    if (Loading)
    {
        Emit(D3D11_SB_OPCODE_LD_RAW, { Dest(Instruction), Address,
                                       (Size == 8) ? RxRegister(D3D11_SB_OPERAND_TYPE_THREAD_GROUP_SHARED_MEMORY, RxSwizzleMode, RxPairSwizzle(0),
                                                                { Shared->second }) :
                                                     RxRegister(D3D11_SB_OPERAND_TYPE_THREAD_GROUP_SHARED_MEMORY, RxSelectMode, 0, { Shared->second }) });
    }
    else
    {
        Emit(D3D11_SB_OPCODE_STORE_RAW, { RxRegister(D3D11_SB_OPERAND_TYPE_THREAD_GROUP_SHARED_MEMORY, RxMaskMode, (Size == 8) ? 3 : 1,
                                                     { Shared->second }),
                                          Address, Source(Value) });
    }
    return TRUE;
}

/* Atomics on group shared memory; a compare exchange also reports whether it stored */
BOOLEAN
RxLowering::SharedAtomic(
    _In_ llvm::Instruction *Instruction)
{
    llvm::AtomicCmpXchgInst *Exchange = llvm::dyn_cast<llvm::AtomicCmpXchgInst>(Instruction);
    llvm::Value *Pointer = Exchange ? Exchange->getPointerOperand() : llvm::cast<llvm::AtomicRMWInst>(Instruction)->getPointerOperand();
    RX_POINTER Place;
    RxTokens Address;
    RxTokens Region;
    UINT Opcode;

    if (!Resolve(Pointer, &Place))
        return FALSE;

    auto Shared = SharedRegisters.find(Place.Base);

    if (Shared == SharedRegisters.end())
        return FALSE;
    Address = Offset(Place, 1);
    Region = RxRegister(D3D11_SB_OPERAND_TYPE_THREAD_GROUP_SHARED_MEMORY, RxMaskMode, 1, { Shared->second });
    if (Exchange != nullptr)
    {
        UINT Register = ResultRegister(Instruction);

        Emit(D3D11_SB_OPCODE_IMM_ATOMIC_CMP_EXCH, { Temp(Register, RxMaskMode, 1), Region, Address, Source(Exchange->getCompareOperand()),
                                                    Source(Exchange->getNewValOperand()) });
        Emit(D3D10_SB_OPCODE_IEQ, { Temp(Register, RxMaskMode, 2), Temp(Register, RxSelectMode, 0), Source(Exchange->getCompareOperand()) });
        return TRUE;
    }
    switch (llvm::cast<llvm::AtomicRMWInst>(Instruction)->getOperation())
    {
        case llvm::AtomicRMWInst::Xchg: Opcode = D3D11_SB_OPCODE_IMM_ATOMIC_EXCH; break;
        case llvm::AtomicRMWInst::Add: Opcode = D3D11_SB_OPCODE_IMM_ATOMIC_IADD; break;
        case llvm::AtomicRMWInst::And: Opcode = D3D11_SB_OPCODE_IMM_ATOMIC_AND; break;
        case llvm::AtomicRMWInst::Or: Opcode = D3D11_SB_OPCODE_IMM_ATOMIC_OR; break;
        case llvm::AtomicRMWInst::Xor: Opcode = D3D11_SB_OPCODE_IMM_ATOMIC_XOR; break;
        case llvm::AtomicRMWInst::Max: Opcode = D3D11_SB_OPCODE_IMM_ATOMIC_IMAX; break;
        case llvm::AtomicRMWInst::Min: Opcode = D3D11_SB_OPCODE_IMM_ATOMIC_IMIN; break;
        case llvm::AtomicRMWInst::UMax: Opcode = D3D11_SB_OPCODE_IMM_ATOMIC_UMAX; break;
        case llvm::AtomicRMWInst::UMin: Opcode = D3D11_SB_OPCODE_IMM_ATOMIC_UMIN; break;
        default: return FALSE;
    }
    Emit(Opcode, { Dest(Instruction), Region, Address, Source(Instruction->getOperand(1)) });
    return TRUE;
}

/* A call that only reads a value per lane: one DXBC instruction with the same operands */
typedef struct _RX_SIMPLE
{
    OpCode Code;
    UINT Opcode;
    UINT Operands;
    UINT Flags;
    D3D10_SB_OPERAND_MODIFIER Modifier;
} RX_SIMPLE;

const RX_SIMPLE RxSimpleOps[] =
{
    { OpCode::FAbs, D3D10_SB_OPCODE_MOV, 1, 0, D3D10_SB_OPERAND_MODIFIER_ABS },
    { OpCode::BitcastF32toI32, D3D10_SB_OPCODE_MOV, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::BitcastI32toF32, D3D10_SB_OPCODE_MOV, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Saturate, D3D10_SB_OPCODE_MOV, 1, D3D10_SB_INSTRUCTION_SATURATE_MASK, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Exp, D3D10_SB_OPCODE_EXP, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Log, D3D10_SB_OPCODE_LOG, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Sqrt, D3D10_SB_OPCODE_SQRT, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Rsqrt, D3D10_SB_OPCODE_RSQ, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Frc, D3D10_SB_OPCODE_FRC, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Round_ne, D3D10_SB_OPCODE_ROUND_NE, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Round_ni, D3D10_SB_OPCODE_ROUND_NI, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Round_pi, D3D10_SB_OPCODE_ROUND_PI, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Round_z, D3D10_SB_OPCODE_ROUND_Z, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Bfrev, D3D11_SB_OPCODE_BFREV, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Countbits, D3D11_SB_OPCODE_COUNTBITS, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::FirstbitLo, D3D11_SB_OPCODE_FIRSTBIT_LO, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::FirstbitHi, D3D11_SB_OPCODE_FIRSTBIT_HI, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::FirstbitSHi, D3D11_SB_OPCODE_FIRSTBIT_SHI, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::DerivCoarseX, D3D11_SB_OPCODE_DERIV_RTX_COARSE, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::DerivCoarseY, D3D11_SB_OPCODE_DERIV_RTY_COARSE, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::DerivFineX, D3D11_SB_OPCODE_DERIV_RTX_FINE, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::DerivFineY, D3D11_SB_OPCODE_DERIV_RTY_FINE, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::LegacyF32ToF16, D3D11_SB_OPCODE_F32TOF16, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::LegacyF16ToF32, D3D11_SB_OPCODE_F16TOF32, 1, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::FMax, D3D10_SB_OPCODE_MAX, 2, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::FMin, D3D10_SB_OPCODE_MIN, 2, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::IMax, D3D10_SB_OPCODE_IMAX, 2, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::IMin, D3D10_SB_OPCODE_IMIN, 2, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::UMax, D3D10_SB_OPCODE_UMAX, 2, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::UMin, D3D10_SB_OPCODE_UMIN, 2, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::FMad, D3D10_SB_OPCODE_MAD, 3, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::IMad, D3D10_SB_OPCODE_IMAD, 3, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::UMad, D3D10_SB_OPCODE_UMAD, 3, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Ibfe, D3D11_SB_OPCODE_IBFE, 3, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Ubfe, D3D11_SB_OPCODE_UBFE, 3, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
    { OpCode::Bfi, D3D11_SB_OPCODE_BFI, 4, 0, D3D10_SB_OPERAND_MODIFIER_NONE },
};

BOOLEAN
RxLowering::LowerCall(
    _In_ llvm::CallInst *Call)
{
    OpCode Code;
    UINT Register;

    if (!hlsl::OP::IsDxilOpFuncCallInst(Call))
    {
        llvm::StringRef Name = Call->getCalledFunction() ? Call->getCalledFunction()->getName() : llvm::StringRef();

        return Name.startswith("llvm.dbg.") || Name.startswith("llvm.lifetime.");
    }
    Code = hlsl::OP::getOpCode(Call);

    /* Instructions reading a heap handle get a prefix that binds it for each set of lanes */
    if (Code != OpCode::AnnotateHandle && Code != OpCode::CreateHandleFromHeap && PendingHeap.empty())
    {
        BOOLEAN Lowered;

        for (UINT Argument = 1; Argument < Call->getNumArgOperands(); Argument++)
        {
            auto Heap = HeapSlots.find(Call->getArgOperand(Argument));

            if (Heap != HeapSlots.end() && PendingHeap.size() < RxWarpMaxHeapBinds)
                PendingHeap.push_back(Heap->second);
            else if (Heap != HeapSlots.end())
                return FALSE;
        }
        if (!PendingHeap.empty())
        {
            Lowered = LowerCall(Call);
            PendingHeap.clear();
            return Lowered;
        }
    }

    /* Operations on a resource picked at run time run once per register it may name */
    if (Code != OpCode::CreateHandle && Code != OpCode::AnnotateHandle && Code != OpCode::CreateHandleFromBinding)
    {
        for (UINT Argument = 1; Argument < Call->getNumArgOperands() && Argument < 3; Argument++)
        {
            RX_HANDLE Range;

            if (Call->getArgOperand(Argument)->getType() == Op->GetHandleType() && HandleOf(Call->getArgOperand(Argument), &Range) &&
                Range.Index != nullptr)
            {
                return LowerDynamic(Call, Call->getArgOperand(Argument), Range);
            }
        }
    }

    if (Code == OpCode::MakeDouble || Code == OpCode::SplitDouble ||
        (Call->getNumArgOperands() > 1 && RxIsWide(Call->getArgOperand(1)->getType()) && Code != OpCode::WaveActiveAllEqual &&
         Code != OpCode::WaveReadLaneAt && Code != OpCode::WaveReadLaneFirst && Code != OpCode::WaveActiveOp &&
         Code != OpCode::WaveActiveBit && Code != OpCode::WavePrefixOp && Code != OpCode::QuadReadLaneAt && Code != OpCode::QuadOp &&
         Code != OpCode::WaveMatch && Code != OpCode::WaveMultiPrefixOp))
    {
        return LowerWideCall(Call, Code);
    }
    if ((Code == OpCode::IMax || Code == OpCode::IMin) && Call->getType()->isIntegerTy(16))
    {
        Emit((Code == OpCode::IMax) ? D3D10_SB_OPCODE_IMAX : D3D10_SB_OPCODE_IMIN,
             { Dest(Call), SignedSource(Call->getArgOperand(1)), SignedSource(Call->getArgOperand(2)) });
        return TRUE;
    }
    for (const RX_SIMPLE &Simple : RxSimpleOps)
    {
        if (Simple.Code != Code)
            continue;
        RxTokens First = Source(Call->getArgOperand(1));

        if (Simple.Modifier != D3D10_SB_OPERAND_MODIFIER_NONE)
            First = RxModified(First, Simple.Modifier);
        if (Simple.Operands == 1)
            Emit(Simple.Opcode, { Dest(Call), First }, Simple.Flags);
        else if (Simple.Operands == 2)
            Emit(Simple.Opcode, { Dest(Call), First, Source(Call->getArgOperand(2)) }, Simple.Flags);
        else if (Simple.Operands == 3)
            Emit(Simple.Opcode, { Dest(Call), First, Source(Call->getArgOperand(2)), Source(Call->getArgOperand(3)) }, Simple.Flags);
        else
            Emit(Simple.Opcode, { Dest(Call), First, Source(Call->getArgOperand(2)), Source(Call->getArgOperand(3)),
                                  Source(Call->getArgOperand(4)) }, Simple.Flags);
        return TRUE;
    }

    switch (Code)
    {
        case OpCode::LoadInput:
        {
            const hlsl::DxilSignatureElement &Element =
                Dxil.GetInputSignature().GetElement((UINT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(1))->getZExtValue());
            llvm::ConstantInt *Row = llvm::dyn_cast<llvm::ConstantInt>(Call->getArgOperand(2));
            UINT Column = (UINT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(3))->getZExtValue() + Element.GetStartCol();

            if (Row == nullptr || !llvm::isa<llvm::UndefValue>(Call->getArgOperand(4)))
                return FALSE;
            Inputs[Call] = { (UINT)Element.GetStartRow() + (UINT)Row->getZExtValue(), Column };
            Emit(D3D10_SB_OPCODE_MOV, { Dest(Call), RxRegister(D3D10_SB_OPERAND_TYPE_INPUT, RxSelectMode, Column,
                                                               { (UINT)Element.GetStartRow() + (UINT)Row->getZExtValue() }) });
            return TRUE;
        }
        case OpCode::StoreOutput:
        {
            const hlsl::DxilSignatureElement &Element =
                Dxil.GetOutputSignature().GetElement((UINT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(1))->getZExtValue());
            llvm::ConstantInt *Row = llvm::dyn_cast<llvm::ConstantInt>(Call->getArgOperand(2));
            UINT Column = (UINT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(3))->getZExtValue() + Element.GetStartCol();
            hlsl::Semantic::Kind Kind = Element.GetKind();
            RxTokens Target;

            if (Row == nullptr)
                return FALSE;
            if (Kind == hlsl::Semantic::Kind::Depth || Kind == hlsl::Semantic::Kind::DepthLessEqual || Kind == hlsl::Semantic::Kind::DepthGreaterEqual)
                Target = { ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH) | ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) };
            else if (Kind == hlsl::Semantic::Kind::Coverage)
                Target = { ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT_COVERAGE_MASK) | ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) };
            else
                Target = RxRegister(D3D10_SB_OPERAND_TYPE_OUTPUT, RxMaskMode, 1u << Column, { (UINT)Element.GetStartRow() + (UINT)Row->getZExtValue() });
            Emit(D3D10_SB_OPCODE_MOV, { Target, Source(Call->getArgOperand(4)) });
            return TRUE;
        }
        case OpCode::CreateHandle:
        case OpCode::CreateHandleFromBinding:
        case OpCode::CreateHandleFromHeap:
        case OpCode::AnnotateHandle:
            return TRUE;
        case OpCode::CBufferLoadLegacy:
        {
            RX_HANDLE Handle;
            llvm::Value *Row = Call->getArgOperand(2);
            RxTokens Buffer;

            if (!HandleOf(Call->getArgOperand(1), &Handle))
                return FALSE;
            Register = ResultRegister(Call);
            if (llvm::ConstantInt *Fixed = llvm::dyn_cast<llvm::ConstantInt>(Row))
            {
                Buffer = RxRegister(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, RxSwizzleMode, RxXyzw, { Handle.Slot, (UINT)Fixed->getZExtValue() });
            }
            else
            {
                UINT Slot = SlotOf(Row);

                Buffer = { RxOperandHead(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 2) | RxFourComponents(RxSwizzleMode, RxXyzw) |
                           ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE), Handle.Slot, 0 };
                RxTokens Relative = Temp(Slot / 4, RxSelectMode, Slot & 3);

                Buffer.insert(Buffer.end(), Relative.begin(), Relative.end());
            }
            Emit(D3D10_SB_OPCODE_MOV, { Temp(Register, RxMaskMode, 0xF), Buffer });
            return TRUE;
        }
        case OpCode::ThreadId:
        case OpCode::GroupId:
        case OpCode::ThreadIdInGroup:
        {
            UINT Type = (Code == OpCode::ThreadId) ? D3D11_SB_OPERAND_TYPE_INPUT_THREAD_ID :
                        (Code == OpCode::GroupId) ? D3D11_SB_OPERAND_TYPE_INPUT_THREAD_GROUP_ID : D3D11_SB_OPERAND_TYPE_INPUT_THREAD_ID_IN_GROUP;

            Emit(D3D10_SB_OPCODE_MOV, { Dest(Call), RxRegister(Type, RxSelectMode,
                                                               (UINT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(1))->getZExtValue(), {}) });
            return TRUE;
        }
        case OpCode::FlattenedThreadIdInGroup:
            Emit(D3D10_SB_OPCODE_MOV, { Dest(Call), RxRegister(D3D11_SB_OPERAND_TYPE_INPUT_THREAD_ID_IN_GROUP_FLATTENED, RxSelectMode, 0, {}) });
            return TRUE;
        case OpCode::Barrier:
        {
            UINT Mode = (UINT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(1))->getZExtValue();
            UINT Flags = 0;

            if (Mode & (UINT)hlsl::DXIL::BarrierMode::SyncThreadGroup)
                Flags |= D3D11_SB_SYNC_THREADS_IN_GROUP;
            if (Mode & (UINT)hlsl::DXIL::BarrierMode::TGSMFence)
                Flags |= D3D11_SB_SYNC_THREAD_GROUP_SHARED_MEMORY;
            if (Mode & (UINT)hlsl::DXIL::BarrierMode::UAVFenceThreadGroup)
                Flags |= D3D11_SB_SYNC_UNORDERED_ACCESS_VIEW_MEMORY_GROUP;
            if (Mode & (UINT)hlsl::DXIL::BarrierMode::UAVFenceGlobal)
                Flags |= D3D11_SB_SYNC_UNORDERED_ACCESS_VIEW_MEMORY_GLOBAL;
            Emit(D3D11_SB_OPCODE_SYNC, {}, ENCODE_D3D11_SB_SYNC_FLAGS(Flags));
            return TRUE;
        }
        case OpCode::Discard:
            Emit(D3D10_SB_OPCODE_DISCARD, { Source(Call->getArgOperand(1)) }, ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(D3D10_SB_INSTRUCTION_TEST_NONZERO));
            return TRUE;
        case OpCode::UDiv:
        case OpCode::UMul:
        case OpCode::IMul:
        case OpCode::UAddc:
        case OpCode::USubb:
        {
            UINT Opcode = (Code == OpCode::UDiv) ? D3D10_SB_OPCODE_UDIV : (Code == OpCode::UMul) ? D3D10_SB_OPCODE_UMUL :
                          (Code == OpCode::IMul) ? D3D10_SB_OPCODE_IMUL : (Code == OpCode::UAddc) ? D3D11_SB_OPCODE_UADDC : D3D11_SB_OPCODE_USUBB;

            /* Both results land in one register, the first in x and the second in y */
            Register = ResultRegister(Call);
            Emit(Opcode, { Temp(Register, RxMaskMode, 1), Temp(Register, RxMaskMode, 2), Source(Call->getArgOperand(1)),
                           Source(Call->getArgOperand(2)) });
            return TRUE;
        }
        case OpCode::Cos:
        case OpCode::Sin:
            Emit(D3D10_SB_OPCODE_SINCOS, { (Code == OpCode::Sin) ? Dest(Call) : RxBare(D3D10_SB_OPERAND_TYPE_NULL, {}),
                                           (Code == OpCode::Cos) ? Dest(Call) : RxBare(D3D10_SB_OPERAND_TYPE_NULL, {}),
                                           Source(Call->getArgOperand(1)) });
            return TRUE;
        case OpCode::CheckAccessFullyMapped:
            Emit(D3DWDDM1_3_SB_OPCODE_CHECK_ACCESS_FULLY_MAPPED, { Dest(Call), Source(Call->getArgOperand(1)) });
            return TRUE;
        case OpCode::Dot2:
        case OpCode::Dot3:
        case OpCode::Dot4:
        {
            UINT Width = (Code == OpCode::Dot2) ? 2 : (Code == OpCode::Dot3) ? 3 : 4;
            UINT Left = GatherRange(Call, 1, Width);
            UINT Right = GatherRange(Call, 1 + Width, Width);
            UINT Opcode = (Width == 2) ? D3D10_SB_OPCODE_DP2 : (Width == 3) ? D3D10_SB_OPCODE_DP3 : D3D10_SB_OPCODE_DP4;

            Emit(Opcode, { Dest(Call), Temp(Left, RxSwizzleMode, RxXyzw), Temp(Right, RxSwizzleMode, RxXyzw) });
            return TRUE;
        }
        case OpCode::WaveGetLaneCount:
            Emit(D3D10_SB_OPCODE_MOV, { Dest(Call), RxImmediate(RxWarpWaveLanes) });
            return TRUE;
        case OpCode::WaveGetLaneIndex:
        case OpCode::WaveIsFirstLane:
        case OpCode::WaveAnyTrue:
        case OpCode::WaveAllTrue:
        case OpCode::WaveActiveAllEqual:
        case OpCode::WaveActiveBallot:
        case OpCode::WaveReadLaneAt:
        case OpCode::WaveReadLaneFirst:
        case OpCode::WaveActiveOp:
        case OpCode::WaveActiveBit:
        case OpCode::WavePrefixOp:
        case OpCode::WaveAllBitCount:
        case OpCode::WavePrefixBitCount:
        case OpCode::QuadReadLaneAt:
        case OpCode::QuadOp:
        case OpCode::WaveMatch:
        case OpCode::WaveMultiPrefixOp:
        case OpCode::WaveMultiPrefixBitCount:
            return LowerWave(Call, Code);
        default:
            /* Native 16 bit resource data has its own memory layout, not lowered yet */
            if (!Dxil.GetUseMinPrecision() && RxNamesSixteen(Call))
                return FALSE;
            return LowerResource(Call, Code);
    }
}

/* Cross lane operations on 32 bit and boolean scalars */
BOOLEAN
RxLowering::LowerWave(
    _In_ llvm::CallInst *Call,
    _In_ OpCode Code)
{
    llvm::Value *Value = (Call->getNumArgOperands() > 1) ? Call->getArgOperand(1) : nullptr;
    RxTokens Lane = RxImmediate(0);
    RxTokens Target;
    UINT Kind;
    UINT Variant = 0;
    UINT Type = RxWarpWaveUnsigned;
    UINT Wide = 0;

    /* Halves sit in floats and 16 bit integers in 32 bit words, so only signed ones need widening */
    if (Value != nullptr)
    {
        llvm::Type *Scalar = Value->getType();

        if (Scalar->isFloatTy() || Scalar->isHalfTy() || Scalar->isDoubleTy())
            Type = RxWarpWaveFloat;
        else if (!Scalar->isIntegerTy(32) && !Scalar->isIntegerTy(1) && !Scalar->isIntegerTy(16) && !Scalar->isIntegerTy(64))
            return FALSE;
        Wide = RxIsWide(Scalar) ? RxWarpWaveWide : 0;
    }

    switch (Code)
    {
        case OpCode::WaveGetLaneIndex: Kind = RxWarpWaveLaneIndex; break;
        case OpCode::WaveIsFirstLane: Kind = RxWarpWaveFirstLane; break;
        case OpCode::WaveAnyTrue: Kind = RxWarpWaveAnyTrue; break;
        case OpCode::WaveAllTrue: Kind = RxWarpWaveAllTrue; break;
        case OpCode::WaveActiveAllEqual: Kind = RxWarpWaveAllEqual; break;
        case OpCode::WaveActiveBallot: Kind = RxWarpWaveBallot; break;
        case OpCode::WaveReadLaneAt: Kind = RxWarpWaveReadLane; break;
        case OpCode::WaveReadLaneFirst: Kind = RxWarpWaveReadFirst; break;
        case OpCode::WaveActiveOp: Kind = RxWarpWaveReduce; break;
        case OpCode::WaveActiveBit: Kind = RxWarpWaveBits; break;
        case OpCode::WavePrefixOp: Kind = RxWarpWavePrefix; break;
        case OpCode::WaveAllBitCount: Kind = RxWarpWaveCount; break;
        case OpCode::WavePrefixBitCount: Kind = RxWarpWavePrefixCount; break;
        case OpCode::QuadReadLaneAt: Kind = RxWarpWaveQuadRead; break;
        case OpCode::WaveMatch: Kind = RxWarpWaveMatch; break;
        case OpCode::WaveMultiPrefixOp: Kind = RxWarpWaveMultiPrefix; break;
        case OpCode::WaveMultiPrefixBitCount: Kind = RxWarpWaveMultiCount; break;
        default: Kind = RxWarpWaveQuadSwap; break;
    }

    /* The operation the call names, then whether integer reductions and prefixes are signed */
    if (Kind == RxWarpWaveReduce || Kind == RxWarpWaveBits || Kind == RxWarpWavePrefix || Kind == RxWarpWaveQuadSwap)
    {
        llvm::ConstantInt *Operation = llvm::dyn_cast<llvm::ConstantInt>(Call->getArgOperand(2));

        if (Operation == nullptr)
            return FALSE;
        Variant = (UINT)Operation->getZExtValue();
        if (Variant > 3 || (Kind == RxWarpWavePrefix && Variant > 1))
            return FALSE;
    }
    if (Kind == RxWarpWaveMultiPrefix)
    {
        llvm::ConstantInt *Operation = llvm::dyn_cast<llvm::ConstantInt>(Call->getArgOperand(6));

        if (Operation == nullptr || Operation->getZExtValue() > 4)
            return FALSE;
        Variant = (UINT)Operation->getZExtValue();
    }
    if ((Kind == RxWarpWaveReduce || Kind == RxWarpWavePrefix || Kind == RxWarpWaveMultiPrefix) && Type != RxWarpWaveFloat)
    {
        llvm::ConstantInt *Signed = llvm::dyn_cast<llvm::ConstantInt>(Call->getArgOperand((Kind == RxWarpWaveMultiPrefix) ? 7 : 3));

        if (Signed == nullptr)
            return FALSE;
        Type = (Signed->getZExtValue() == (UINT64)hlsl::DXIL::SignedOpKind::Signed) ? RxWarpWaveSigned : RxWarpWaveUnsigned;
    }
    if (Kind == RxWarpWaveReadLane || Kind == RxWarpWaveQuadRead || Kind == RxWarpWaveMultiPrefix || Kind == RxWarpWaveMultiCount)
        Lane = Source(Call->getArgOperand(2));

    if (Kind == RxWarpWaveBallot || Kind == RxWarpWaveMatch)
        Target = Temp(ResultRegister(Call), RxMaskMode, 0xF);
    else
        Target = Dest(Call);
    Emit(RxWarpOpcodeWave, { Target, (Value == nullptr) ? RxImmediate(0) : (Type == RxWarpWaveSigned) ? SignedSource(Value) : Source(Value),
                             Lane, RxImmediate(Kind | Variant << 8 | Type << 16 | Wide) });
    return TRUE;
}

UINT
RxCoordinates(
    _In_ UINT Kind)
{
    switch ((hlsl::DXIL::ResourceKind)Kind)
    {
        case hlsl::DXIL::ResourceKind::Texture1D:
        case hlsl::DXIL::ResourceKind::TypedBuffer: return 1;
        case hlsl::DXIL::ResourceKind::Texture1DArray:
        case hlsl::DXIL::ResourceKind::Texture2D:
        case hlsl::DXIL::ResourceKind::Texture2DMS: return 2;
        case hlsl::DXIL::ResourceKind::TextureCubeArray: return 4;
        default: return 3;
    }
}

/* Coordinates read straight from one input register stay an input operand, which the sampler interpolates again */
BOOLEAN
RxLowering::InputCoordinates(
    _In_ llvm::CallInst *Call,
    _In_ UINT First,
    _In_ UINT Count,
    _Out_ RxTokens *Operand)
{
    UINT Swizzle = 0;
    UINT Row = 0;

    for (UINT Component = 0; Component < 4; Component++)
    {
        auto Found = Inputs.find(Call->getArgOperand(First + ((Component < Count) ? Component : Count - 1)));

        if (!PixelShader || Found == Inputs.end() || (Component != 0 && Found->second.first != Row))
            return FALSE;
        Row = Found->second.first;
        Swizzle |= Found->second.second << (Component * 2);
    }
    *Operand = RxRegister(D3D10_SB_OPERAND_TYPE_INPUT, RxSwizzleMode, Swizzle, { Row });
    return TRUE;
}

/* Spatial axes of a texture kind: what gradients and gather offsets cover */
UINT
RxSpatialAxes(
    _In_ UINT Kind)
{
    switch ((hlsl::DXIL::ResourceKind)Kind)
    {
        case hlsl::DXIL::ResourceKind::Texture1D:
        case hlsl::DXIL::ResourceKind::Texture1DArray: return 1;
        case hlsl::DXIL::ResourceKind::Texture3D:
        case hlsl::DXIL::ResourceKind::TextureCube:
        case hlsl::DXIL::ResourceKind::TextureCubeArray: return 3;
        default: return 2;
    }
}

/* Constant texel offsets go in the extended opcode; others are not lowered */
BOOLEAN
RxOffsets(
    _In_ llvm::CallInst *Call,
    _In_ UINT First,
    _Out_ RxTokens *Extended,
    _In_ UINT Axes = 3)
{
    UINT Token = ENCODE_D3D10_SB_EXTENDED_OPCODE_TYPE(D3D10_SB_EXTENDED_OPCODE_SAMPLE_CONTROLS);
    BOOLEAN Any = FALSE;

    Extended->clear();
    for (UINT Axis = 0; Axis < Axes; Axis++)
    {
        llvm::Value *Value = Call->getArgOperand(First + Axis);
        llvm::ConstantInt *Fixed = llvm::dyn_cast<llvm::ConstantInt>(Value);

        if (llvm::isa<llvm::UndefValue>(Value))
            continue;
        if (Fixed == nullptr)
            return FALSE;
        Token |= ENCODE_IMMEDIATE_D3D10_SB_ADDRESS_OFFSET(Axis, (INT)Fixed->getSExtValue());
        Any |= Fixed->getSExtValue() != 0;
    }
    if (Any)
        Extended->push_back(Token);
    return TRUE;
}

BOOLEAN
RxLowering::LowerResource(
    _In_ llvm::CallInst *Call,
    _In_ OpCode Code)
{
    RX_HANDLE Handle;
    RX_HANDLE Sampler = {};
    RxTokens Extended;
    UINT Result;
    UINT Address;

    if (Call->getNumArgOperands() < 2 || !HandleOf(Call->getArgOperand(1), &Handle))
        return FALSE;
    if ((Code == OpCode::RawBufferLoad && RxIsWide(llvm::cast<llvm::StructType>(Call->getType())->getElementType(0))) ||
        (Code == OpCode::RawBufferStore && RxIsWide(Call->getArgOperand(4)->getType())))
    {
        return WideAccess(Call, Code, Handle);
    }
    switch (Code)
    {
        case OpCode::Sample:
        case OpCode::SampleBias:
        case OpCode::SampleLevel:
        case OpCode::SampleGrad:
        case OpCode::SampleCmp:
        case OpCode::SampleCmpLevelZero:
        {
            UINT Count = RxCoordinates(Handle.Kind);
            UINT Clamp = (Code == OpCode::Sample) ? 10 : (Code == OpCode::SampleBias || Code == OpCode::SampleCmp) ? 11 :
                         (Code == OpCode::SampleGrad) ? 16 : 0;
            RxTokens SamplerOperand;
            RxTokens Coordinates;
            BOOLEAN Clamped;
            UINT Status;

            if (!HandleOf(Call->getArgOperand(2), &Sampler) || !RxOffsets(Call, 7, &Extended))
                return FALSE;
            Clamped = Clamp != 0 && !llvm::isa<llvm::UndefValue>(Call->getArgOperand(Clamp));
            if (!InputCoordinates(Call, 3, Count, &Coordinates))
            {
                Address = GatherRange(Call, 3, Count);
                Coordinates = Temp(Address, RxSwizzleMode, RxXyzw);
            }
            Result = ResultRegister(Call);
            Status = StatusRegister(Call);
            SamplerOperand = RxBare(D3D10_SB_OPERAND_TYPE_SAMPLER, { Sampler.Slot });

            /* A level clamp or a status the program reads takes the residency forms */
            if (Clamped || Status != UINT_MAX)
            {
                RxTokens Feedback = (Status != UINT_MAX) ? Temp(Status, RxMaskMode, 1) : RxBare(D3D10_SB_OPERAND_TYPE_NULL, {});
                RxTokens Lowest = Clamped ? Source(Call->getArgOperand(Clamp)) : RxImmediate(0xFF800000);
                BOOLEAN Single = (Code == OpCode::SampleCmp || Code == OpCode::SampleCmpLevelZero);
                RxTokens Target = Temp(Result, RxMaskMode, Single ? 1 : 0xF);
                RxTokens Resource = Single ? ResourceOperand(Handle, RxSelectMode, 0) : ResourceOperand(Handle);
                const RxTokens *Offsets = Extended.empty() ? nullptr : &Extended;

                switch (Code)
                {
                    case OpCode::Sample:
                        Emit(D3DWDDM1_3_SB_OPCODE_SAMPLE_CLAMP_FEEDBACK, { Target, Feedback, Coordinates, Resource, SamplerOperand, Lowest },
                             0, Offsets);
                        break;
                    case OpCode::SampleBias:
                        Emit(D3DWDDM1_3_SB_OPCODE_SAMPLE_B_CLAMP_FEEDBACK, { Target, Feedback, Coordinates, Resource, SamplerOperand,
                                                                             Source(Call->getArgOperand(10)), Lowest }, 0, Offsets);
                        break;
                    case OpCode::SampleCmp:
                        Emit(D3DWDDM1_3_SB_OPCODE_SAMPLE_C_CLAMP_FEEDBACK, { Target, Feedback, Coordinates, Resource, SamplerOperand,
                                                                             Source(Call->getArgOperand(10)), Lowest }, 0, Offsets);
                        break;
                    case OpCode::SampleGrad:
                    {
                        UINT Across = GatherRange(Call, 10, RxSpatialAxes(Handle.Kind));
                        UINT Down = GatherRange(Call, 13, RxSpatialAxes(Handle.Kind));

                        Emit(D3DWDDM1_3_SB_OPCODE_SAMPLE_D_CLAMP_FEEDBACK, { Target, Feedback, Coordinates, Resource, SamplerOperand,
                                                                             Temp(Across, RxSwizzleMode, RxXyzw), Temp(Down, RxSwizzleMode, RxXyzw),
                                                                             Lowest }, 0, Offsets);
                        break;
                    }
                    case OpCode::SampleLevel:
                        Emit(D3DWDDM1_3_SB_OPCODE_SAMPLE_L_FEEDBACK, { Target, Feedback, Coordinates, Resource, SamplerOperand,
                                                                       Source(Call->getArgOperand(10)) }, 0, Offsets);
                        break;
                    default:
                        Emit(D3DWDDM1_3_SB_OPCODE_SAMPLE_C_LZ_FEEDBACK, { Target, Feedback, Coordinates, Resource, SamplerOperand,
                                                                          Source(Call->getArgOperand(10)) }, 0, Offsets);
                        break;
                }
                return TRUE;
            }
            switch (Code)
            {
                case OpCode::Sample:
                    Emit(D3D10_SB_OPCODE_SAMPLE, { Temp(Result, RxMaskMode, 0xF), Coordinates, ResourceOperand(Handle),
                                                   SamplerOperand }, 0, Extended.empty() ? nullptr : &Extended);
                    break;
                case OpCode::SampleBias:
                case OpCode::SampleLevel:
                    Emit((Code == OpCode::SampleBias) ? D3D10_SB_OPCODE_SAMPLE_B : D3D10_SB_OPCODE_SAMPLE_L,
                         { Temp(Result, RxMaskMode, 0xF), Coordinates, ResourceOperand(Handle), SamplerOperand,
                           Source(Call->getArgOperand(10)) }, 0, Extended.empty() ? nullptr : &Extended);
                    break;
                case OpCode::SampleGrad:
                {
                    UINT Across = GatherRange(Call, 10, RxSpatialAxes(Handle.Kind));
                    UINT Down = GatherRange(Call, 13, RxSpatialAxes(Handle.Kind));

                    Emit(D3D10_SB_OPCODE_SAMPLE_D, { Temp(Result, RxMaskMode, 0xF), Coordinates, ResourceOperand(Handle), SamplerOperand,
                                                     Temp(Across, RxSwizzleMode, RxXyzw), Temp(Down, RxSwizzleMode, RxXyzw) }, 0,
                         Extended.empty() ? nullptr : &Extended);
                    break;
                }
                default:
                    Emit((Code == OpCode::SampleCmp) ? D3D10_SB_OPCODE_SAMPLE_C : D3D10_SB_OPCODE_SAMPLE_C_LZ,
                         { Temp(Result, RxMaskMode, 1), Coordinates, ResourceOperand(Handle, RxSelectMode, 0),
                           SamplerOperand, Source(Call->getArgOperand(10)) }, 0, Extended.empty() ? nullptr : &Extended);
                    break;
            }
            return TRUE;
        }
        case OpCode::TextureGather:
        case OpCode::TextureGatherCmp:
        {
            UINT Count = RxCoordinates(Handle.Kind);
            llvm::ConstantInt *Channel = llvm::dyn_cast<llvm::ConstantInt>(Call->getArgOperand(9));
            BOOLEAN Compare = Code == OpCode::TextureGatherCmp;
            RxTokens SamplerOperand;
            RxTokens Resource;

            /* The sampler operand's component picks the channel; offsets the program computes take the _po form */
            if (!HandleOf(Call->getArgOperand(2), &Sampler) || Channel == nullptr)
                return FALSE;
            Address = GatherRange(Call, 3, Count);
            Result = ResultRegister(Call);
            SamplerOperand = RxRegister(D3D10_SB_OPERAND_TYPE_SAMPLER, RxSelectMode, Compare ? 0 : (UINT)Channel->getZExtValue() & 3, { Sampler.Slot });
            Resource = ResourceOperand(Handle);
            if (RxOffsets(Call, 7, &Extended, 2))
            {
                if (Compare)
                    Emit(D3D11_SB_OPCODE_GATHER4_C, { Temp(Result, RxMaskMode, 0xF), Temp(Address, RxSwizzleMode, RxXyzw), Resource, SamplerOperand,
                                                      Source(Call->getArgOperand(10)) }, 0, Extended.empty() ? nullptr : &Extended);
                else
                    Emit(D3D10_1_SB_OPCODE_GATHER4, { Temp(Result, RxMaskMode, 0xF), Temp(Address, RxSwizzleMode, RxXyzw), Resource, SamplerOperand },
                         0, Extended.empty() ? nullptr : &Extended);
                return TRUE;
            }
            UINT Offsets = GatherRange(Call, 7, 2);

            if (Compare)
                Emit(D3D11_SB_OPCODE_GATHER4_PO_C, { Temp(Result, RxMaskMode, 0xF), Temp(Address, RxSwizzleMode, RxXyzw),
                                                     Temp(Offsets, RxSwizzleMode, RxXyzw), Resource, SamplerOperand, Source(Call->getArgOperand(10)) });
            else
                Emit(D3D11_SB_OPCODE_GATHER4_PO, { Temp(Result, RxMaskMode, 0xF), Temp(Address, RxSwizzleMode, RxXyzw),
                                                   Temp(Offsets, RxSwizzleMode, RxXyzw), Resource, SamplerOperand });
            return TRUE;
        }
        case OpCode::AtomicBinOp:
        case OpCode::AtomicCompareExchange:
        {
            static const UINT Opcodes[] = { D3D11_SB_OPCODE_IMM_ATOMIC_IADD, D3D11_SB_OPCODE_IMM_ATOMIC_AND, D3D11_SB_OPCODE_IMM_ATOMIC_OR,
                                            D3D11_SB_OPCODE_IMM_ATOMIC_XOR, D3D11_SB_OPCODE_IMM_ATOMIC_IMIN, D3D11_SB_OPCODE_IMM_ATOMIC_IMAX,
                                            D3D11_SB_OPCODE_IMM_ATOMIC_UMIN, D3D11_SB_OPCODE_IMM_ATOMIC_UMAX, D3D11_SB_OPCODE_IMM_ATOMIC_EXCH };
            BOOLEAN Exchange = Code == OpCode::AtomicCompareExchange;
            UINT First = Exchange ? 2 : 3;
            UINT Kind = Handle.Kind;
            UINT Count = (Kind == (UINT)hlsl::DXIL::ResourceKind::StructuredBuffer) ? 2 :
                         (Kind == (UINT)hlsl::DXIL::ResourceKind::RawBuffer || Kind == (UINT)hlsl::DXIL::ResourceKind::TypedBuffer) ? 1 :
                         RxCoordinates(Kind);
            RxTokens Target = ResourceOperand(Handle, RxMaskMode, 1);
            UINT Operation;

            Address = GatherRange(Call, First, Count);
            if (Exchange)
            {
                Emit(D3D11_SB_OPCODE_IMM_ATOMIC_CMP_EXCH, { Dest(Call), Target, Temp(Address, RxSwizzleMode, RxXyzw),
                                                            Source(Call->getArgOperand(5)), Source(Call->getArgOperand(6)) });
                return TRUE;
            }
            Operation = (UINT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(2))->getZExtValue();
            if (Operation >= ARRAYSIZE(Opcodes))
                return FALSE;
            Emit(Opcodes[Operation], { Dest(Call), Target, Temp(Address, RxSwizzleMode, RxXyzw), Source(Call->getArgOperand(6)) });
            return TRUE;
        }
        case OpCode::BufferUpdateCounter:
        {
            INT Step = (INT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(2))->getSExtValue();

            Emit((Step > 0) ? D3D11_SB_OPCODE_IMM_ATOMIC_ALLOC : D3D11_SB_OPCODE_IMM_ATOMIC_CONSUME,
                 { Dest(Call), ResourceOperand(Handle, RxMaskMode, 1) });
            return TRUE;
        }
        case OpCode::GetDimensions:
        {
            hlsl::DXIL::ResourceKind Kind = (hlsl::DXIL::ResourceKind)Handle.Kind;

            /* Buffers count elements, bytes when raw; textures give sizes and levels, multisampled ones samples */
            Result = ResultRegister(Call);
            if (Kind == hlsl::DXIL::ResourceKind::TypedBuffer || Kind == hlsl::DXIL::ResourceKind::RawBuffer ||
                Kind == hlsl::DXIL::ResourceKind::StructuredBuffer)
            {
                Emit(D3D11_SB_OPCODE_BUFINFO, { Temp(Result, RxMaskMode, 1), ResourceOperand(Handle, RxSelectMode, 0) });
                Emit(D3D10_SB_OPCODE_MOV, { Temp(Result, RxMaskMode, 0xE), RxImmediate(0) });
                return TRUE;
            }
            BOOLEAN Multisampled = Kind == hlsl::DXIL::ResourceKind::Texture2DMS || Kind == hlsl::DXIL::ResourceKind::Texture2DMSArray;
            RxTokens Level = Multisampled ? RxImmediate(0) : Source(Call->getArgOperand(2));
            UINT Valid = Fresh4();

            /* A level past the last one reads as all zero, its level count too */
            Emit(D3D10_SB_OPCODE_RESINFO, { Temp(Result, RxMaskMode, 0xF), Level, ResourceOperand(Handle) },
                 ENCODE_D3D10_SB_RESINFO_INSTRUCTION_RETURN_TYPE(D3D10_SB_RESINFO_INSTRUCTION_RETURN_UINT));
            Emit(D3D10_SB_OPCODE_ULT, { Temp(Valid, RxMaskMode, 1), Level, Temp(Result, RxSelectMode, 3) });
            Emit(D3D10_SB_OPCODE_AND, { Temp(Result, RxMaskMode, 0xF), Temp(Result, RxSwizzleMode, RxXyzw), Temp(Valid, RxSwizzleMode, 0) });
            if (Multisampled)
                Emit(D3D10_1_SB_OPCODE_SAMPLE_INFO, { Temp(Result, RxMaskMode, 8), ResourceOperand(Handle, RxSelectMode, 0) },
                     ENCODE_D3D10_SB_INSTRUCTION_RETURN_TYPE(D3D10_SB_INSTRUCTION_RETURN_UINT));
            return TRUE;
        }
        case OpCode::TextureLoad:
        {
            BOOLEAN Multisampled = Handle.Kind == (UINT)hlsl::DXIL::ResourceKind::Texture2DMS ||
                                   Handle.Kind == (UINT)hlsl::DXIL::ResourceKind::Texture2DMSArray;
            UINT Count = RxCoordinates(Handle.Kind);

            if (Count > 3 || !RxOffsets(Call, 6, &Extended))
                return FALSE;
            Address = Fresh4();
            for (UINT Component = 0; Component < 3; Component++)
                Emit(D3D10_SB_OPCODE_MOV, { Temp(Address, RxMaskMode, 1u << Component),
                                            (Component < Count) ? Source(Call->getArgOperand(3 + Component)) : RxImmediate(0) });
            Emit(D3D10_SB_OPCODE_MOV, { Temp(Address, RxMaskMode, 8), Multisampled ? RxImmediate(0) : Source(Call->getArgOperand(2)) });
            Result = ResultRegister(Call);
            if (Handle.Class == (UINT)hlsl::DXIL::ResourceClass::UAV)
            {
                Emit(D3D11_SB_OPCODE_LD_UAV_TYPED, { Temp(Result, RxMaskMode, 0xF), Temp(Address, RxSwizzleMode, RxXyzw), ResourceOperand(Handle) });
                return TRUE;
            }
            if (Multisampled)
                Emit(D3D10_SB_OPCODE_LD_MS, { Temp(Result, RxMaskMode, 0xF), Temp(Address, RxSwizzleMode, RxXyzw), ResourceOperand(Handle),
                                              Source(Call->getArgOperand(2)) }, 0, Extended.empty() ? nullptr : &Extended);
            else
                Emit(D3D10_SB_OPCODE_LD, { Temp(Result, RxMaskMode, 0xF), Temp(Address, RxSwizzleMode, RxXyzw), ResourceOperand(Handle) }, 0,
                     Extended.empty() ? nullptr : &Extended);
            return TRUE;
        }
        case OpCode::BufferLoad:
        case OpCode::RawBufferLoad:
        {
            UINT Kind = Handle.Kind;

            Result = ResultRegister(Call);
            if (Kind == (UINT)hlsl::DXIL::ResourceKind::RawBuffer)
                Emit(D3D11_SB_OPCODE_LD_RAW, { Temp(Result, RxMaskMode, 0xF), Source(Call->getArgOperand(2)), ResourceOperand(Handle) });
            else if (Kind == (UINT)hlsl::DXIL::ResourceKind::StructuredBuffer)
                Emit(D3D11_SB_OPCODE_LD_STRUCTURED, { Temp(Result, RxMaskMode, 0xF), Source(Call->getArgOperand(2)),
                                                      Source(Call->getArgOperand(3)), ResourceOperand(Handle) });
            else if (Handle.Class == (UINT)hlsl::DXIL::ResourceClass::UAV)
                Emit(D3D11_SB_OPCODE_LD_UAV_TYPED, { Temp(Result, RxMaskMode, 0xF), Source(Call->getArgOperand(2)), ResourceOperand(Handle) });
            else
                Emit(D3D10_SB_OPCODE_LD, { Temp(Result, RxMaskMode, 0xF), Source(Call->getArgOperand(2)), ResourceOperand(Handle) });
            return TRUE;
        }
        case OpCode::BufferStore:
        case OpCode::RawBufferStore:
        case OpCode::TextureStore:
        {
            BOOLEAN Texture = Code == OpCode::TextureStore;
            UINT First = Texture ? 5 : 4;
            UINT Mask = (UINT)llvm::cast<llvm::ConstantInt>(Call->getArgOperand(First + 4))->getZExtValue();
            UINT Values = GatherRange(Call, First, 4);
            UINT Kind = Handle.Kind;

            if (Texture)
            {
                Address = GatherRange(Call, 2, RxCoordinates(Kind));
                Emit(D3D11_SB_OPCODE_STORE_UAV_TYPED, { ResourceOperand(Handle, RxMaskMode, 0xF), Temp(Address, RxSwizzleMode, RxXyzw),
                                                        Temp(Values, RxSwizzleMode, RxXyzw) });
            }
            else if (Kind == (UINT)hlsl::DXIL::ResourceKind::RawBuffer)
            {
                Emit(D3D11_SB_OPCODE_STORE_RAW, { ResourceOperand(Handle, RxMaskMode, Mask), Source(Call->getArgOperand(2)),
                                                  Temp(Values, RxSwizzleMode, RxXyzw) });
            }
            else if (Kind == (UINT)hlsl::DXIL::ResourceKind::StructuredBuffer)
            {
                Emit(D3D11_SB_OPCODE_STORE_STRUCTURED, { ResourceOperand(Handle, RxMaskMode, Mask), Source(Call->getArgOperand(2)),
                                                         Source(Call->getArgOperand(3)), Temp(Values, RxSwizzleMode, RxXyzw) });
            }
            else
            {
                Address = Gather({ Call->getArgOperand(2), Call->getArgOperand(2), Call->getArgOperand(2), Call->getArgOperand(2) });
                Emit(D3D11_SB_OPCODE_STORE_UAV_TYPED, { ResourceOperand(Handle, RxMaskMode, 0xF), Temp(Address, RxSwizzleMode, RxXyzw),
                                                        Temp(Values, RxSwizzleMode, RxXyzw) });
            }
            return TRUE;
        }
        default:
            return FALSE;
    }
}

/*
 * Raw and structured accesses of 64 bit elements: the mask counts elements, two words each,
 * so four elements take two registers and a second access 16 bytes on.
 */
BOOLEAN
RxLowering::WideAccess(
    _In_ llvm::CallInst *Call,
    _In_ OpCode Code,
    _In_ const RX_HANDLE &Handle)
{
    BOOLEAN Structured = Handle.Kind == (UINT)hlsl::DXIL::ResourceKind::StructuredBuffer;
    BOOLEAN Loading = Code == OpCode::RawBufferLoad;
    llvm::ConstantInt *Bits = llvm::dyn_cast<llvm::ConstantInt>(Call->getArgOperand(Loading ? 4 : 8));
    llvm::Value *Place = Call->getArgOperand(Structured ? 3 : 2);
    UINT Mask;
    UINT Register[2];

    if (Bits == nullptr || (!Structured && Handle.Kind != (UINT)hlsl::DXIL::ResourceKind::RawBuffer))
        return FALSE;
    Mask = (UINT)Bits->getZExtValue() & 0xF;
    Register[0] = Fresh4();
    Register[1] = Fresh4();
    if (Loading)
        Slots[Call] = Register[0] * 4;

    for (UINT Part = 0; Part < 2; Part++)
    {
        UINT Elements = (Mask >> (Part * 2)) & 3;
        UINT Words = ((Elements & 1) ? 3 : 0) | ((Elements & 2) ? 0xC : 0);
        RxTokens Offset = Source(Place);

        if (Elements == 0)
            continue;
        if (Part != 0)
        {
            UINT Moved = Fresh();

            Emit(D3D10_SB_OPCODE_IADD, { SlotDest(Moved, FALSE), Source(Place), RxImmediate(16) });
            Offset = SlotSource(Moved, FALSE);
        }
        if (Loading)
        {
            RxTokens Target = Temp(Register[Part], RxMaskMode, (Elements & 2) ? 0xF : 3);

            if (Structured)
                Emit(D3D11_SB_OPCODE_LD_STRUCTURED, { Target, Source(Call->getArgOperand(2)), Offset, ResourceOperand(Handle) });
            else
                Emit(D3D11_SB_OPCODE_LD_RAW, { Target, Offset, ResourceOperand(Handle) });
            continue;
        }
        for (UINT Element = 0; Element < 2; Element++)
        {
            if (Elements & (1u << Element))
                Emit(D3D10_SB_OPCODE_MOV, { Temp(Register[Part], RxMaskMode, 3u << (Element * 2)),
                                            Source(Call->getArgOperand(4 + Part * 2 + Element)) });
        }
        if (Structured)
            Emit(D3D11_SB_OPCODE_STORE_STRUCTURED, { ResourceOperand(Handle, RxMaskMode, Words), Source(Call->getArgOperand(2)), Offset,
                                                     Temp(Register[Part], RxSwizzleMode, RxXyzw) });
        else
            Emit(D3D11_SB_OPCODE_STORE_RAW, { ResourceOperand(Handle, RxMaskMode, Words), Offset, Temp(Register[Part], RxSwizzleMode, RxXyzw) });
    }
    return TRUE;
}

/* Phi values for the edge From to To; copied through fresh temps so swaps stay right */
VOID
RxLowering::Edge(
    _In_ llvm::BasicBlock *From,
    _In_ llvm::BasicBlock *To)
{
    std::vector<std::pair<llvm::PHINode *, UINT>> Moves;

    for (llvm::Instruction &Instruction : *To)
    {
        llvm::PHINode *Phi = llvm::dyn_cast<llvm::PHINode>(&Instruction);
        UINT Staging;

        if (Phi == nullptr)
            break;
        Staging = FreshFor(Phi->getType());
        Emit(D3D10_SB_OPCODE_MOV, { SlotDest(Staging, RxIsWide(Phi->getType())), Source(Phi->getIncomingValueForBlock(From)) });
        Moves.push_back({ Phi, Staging });
    }
    for (const std::pair<llvm::PHINode *, UINT> &Move : Moves)
        Emit(D3D10_SB_OPCODE_MOV, { Dest(Move.first), SlotSource(Move.second, RxIsWide(Move.first->getType())) });
}

/* Branching to the open loop's header continues it, to its exit breaks out */
BOOLEAN
RxLowering::Leaves(
    _In_ llvm::BasicBlock *From,
    _In_ llvm::BasicBlock *Target)
{
    if (Open.empty())
        return FALSE;
    if (Target == Open.back().Header)
    {
        Edge(From, Target);
        Emit(D3D10_SB_OPCODE_CONTINUE, {});
        return TRUE;
    }
    if (Target == Open.back().Exit)
    {
        Edge(From, Target);
        Emit(D3D10_SB_OPCODE_BREAK, {});
        return TRUE;
    }

    /* A side exit runs its blocks here; they end by reaching the exit, which breaks */
    if (!Open.back().Loop->contains(Target))
    {
        Edge(From, Target);
        if (!Region(Target, nullptr))
            Failed = TRUE;
        return TRUE;
    }
    return FALSE;
}

BOOLEAN
RxLowering::Arm(
    _In_ llvm::BasicBlock *From,
    _In_ llvm::BasicBlock *Target,
    _In_opt_ llvm::BasicBlock *Merge)
{
    if (Leaves(From, Target))
        return TRUE;
    if (Target == Merge)
    {
        Edge(From, Target);
        return TRUE;
    }
    Edge(From, Target);
    return Region(Target, Merge);
}

/*
 * A switch becomes a chain of tests, one per target block, each comparing the selector with
 * every case value that leads there; the default target closes the chain.
 */
BOOLEAN
RxLowering::Cases(
    _In_ llvm::BasicBlock *Block,
    _In_ llvm::SwitchInst *Switch,
    _In_opt_ llvm::BasicBlock *Merge)
{
    std::vector<llvm::BasicBlock *> Targets;
    UINT Depth = 0;

    for (auto Case = Switch->case_begin(); Case != Switch->case_end(); ++Case)
    {
        llvm::BasicBlock *Target = Case.getCaseSuccessor();

        if (Target != Switch->getDefaultDest() && std::find(Targets.begin(), Targets.end(), Target) == Targets.end())
            Targets.push_back(Target);
    }
    for (llvm::BasicBlock *Target : Targets)
    {
        RxTokens Test;

        for (auto Case = Switch->case_begin(); Case != Switch->case_end(); ++Case)
        {
            UINT Slot;

            if (Case.getCaseSuccessor() != Target)
                continue;
            Slot = Fresh();
            Emit(D3D10_SB_OPCODE_IEQ, { Temp(Slot / 4, RxMaskMode, 1u << (Slot & 3)), Source(Switch->getCondition()),
                                        RxImmediate((UINT)Case.getCaseValue()->getZExtValue()) });
            if (!Test.empty())
                Emit(D3D10_SB_OPCODE_OR, { Temp(Slot / 4, RxMaskMode, 1u << (Slot & 3)), Temp(Slot / 4, RxSelectMode, Slot & 3), Test });
            Test = Temp(Slot / 4, RxSelectMode, Slot & 3);
        }
        Emit(D3D10_SB_OPCODE_IF, { Test }, ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(D3D10_SB_INSTRUCTION_TEST_NONZERO));
        if (!Arm(Block, Target, Merge))
            return FALSE;
        Emit(D3D10_SB_OPCODE_ELSE, {});
        Depth += 1;
    }
    if (!Arm(Block, Switch->getDefaultDest(), Merge))
        return FALSE;
    for (UINT Remaining = Depth; Remaining > 0; Remaining -= 1)
        Emit(D3D10_SB_OPCODE_ENDIF, {});
    return TRUE;
}

BOOLEAN
RxLowering::Region(
    _In_ llvm::BasicBlock *Block,
    _In_opt_ llvm::BasicBlock *Stop)
{
    while (Block != nullptr && Block != Stop)
    {
        llvm::Loop *Loop = Loops.getLoopFor(Block);
        llvm::TerminatorInst *Terminator;

        /* Entering a loop: its body runs until a break */
        if (Loop != nullptr && Loop->getHeader() == Block && (Open.empty() || Open.back().Header != Block))
        {
            llvm::SmallVector<llvm::BasicBlock *, 4> Exits;
            llvm::BasicBlock *Exit;

            /* Several exits meet where they all join; the others run inside the loop and then break */
            Loop->getExitBlocks(Exits);
            if (Exits.empty())
                return FALSE;
            Exit = Exits[0];
            for (llvm::BasicBlock *Other : Exits)
            {
                Exit = PostDominators.findNearestCommonDominator(Exit, Other);
                if (Exit == nullptr)
                    return FALSE;
            }
            if (Loop->contains(Exit))
                return FALSE;
            Open.push_back({ Block, Exit, Loop });
            Emit(D3D10_SB_OPCODE_LOOP, {});
            if (!Region(Block, nullptr))
                return FALSE;
            Emit(D3D10_SB_OPCODE_ENDLOOP, {});
            Open.pop_back();
            Block = Exit;
            continue;
        }

        for (llvm::Instruction &Instruction : *Block)
        {
            if (!Lower(&Instruction))
            {
                std::string Text;
                llvm::raw_string_ostream Stream(Text);

                Instruction.print(Stream);
                Stream.flush();
                OutputDebugStringA(("WARP: DXIL instruction not lowered: " + Text + "\n").c_str());
                return FALSE;
            }
        }

        Terminator = Block->getTerminator();
        if (llvm::isa<llvm::ReturnInst>(Terminator))
        {
            Emit(D3D10_SB_OPCODE_RET, {});
            return TRUE;
        }
        if (llvm::SwitchInst *Switch = llvm::dyn_cast<llvm::SwitchInst>(Terminator))
        {
            llvm::DomTreeNodeBase<llvm::BasicBlock> *Node = PostDominators.getNode(Block);
            llvm::BasicBlock *Merge = (Node && Node->getIDom()) ? Node->getIDom()->getBlock() : nullptr;

            if (Merge != nullptr && !Open.empty() && !Open.back().Loop->contains(Merge))
                Merge = nullptr;
            if (!Cases(Block, Switch, Merge))
                return FALSE;
            if (Merge == nullptr)
                return TRUE;
            Block = Merge;
            continue;
        }
        llvm::BranchInst *Branch = llvm::dyn_cast<llvm::BranchInst>(Terminator);
        if (Branch == nullptr)
            return FALSE;
        if (Branch->isUnconditional())
        {
            llvm::BasicBlock *Next = Branch->getSuccessor(0);

            if (Leaves(Block, Next))
                return TRUE;
            Edge(Block, Next);
            Block = Next;
            continue;
        }

        llvm::DomTreeNodeBase<llvm::BasicBlock> *Node = PostDominators.getNode(Block);
        llvm::BasicBlock *Merge = (Node && Node->getIDom()) ? Node->getIDom()->getBlock() : nullptr;

        /* A merge outside the open loop means both arms leave the iteration */
        if (Merge != nullptr && !Open.empty() && !Open.back().Loop->contains(Merge))
            Merge = nullptr;
        Emit(D3D10_SB_OPCODE_IF, { Source(Branch->getCondition()) }, ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(D3D10_SB_INSTRUCTION_TEST_NONZERO));
        if (!Arm(Block, Branch->getSuccessor(0), Merge))
            return FALSE;
        Emit(D3D10_SB_OPCODE_ELSE, {});
        if (!Arm(Block, Branch->getSuccessor(1), Merge))
            return FALSE;
        Emit(D3D10_SB_OPCODE_ENDIF, {});
        if (Merge == nullptr)
            return TRUE;
        Block = Merge;
    }
    return TRUE;
}

BOOLEAN
RxLowering::Run(
    _Out_ RxTokens *Program)
{
    llvm::Function *Entry = Dxil.GetEntryFunction();
    const hlsl::ShaderModel *Model = Dxil.GetShaderModel();
    UINT Type;

    if (Entry == nullptr)
        return FALSE;
    if (Model->IsPS())
        Type = D3D10_SB_PIXEL_SHADER;
    else if (Model->IsVS())
        Type = D3D10_SB_VERTEX_SHADER;
    else if (Model->IsGS())
        Type = D3D10_SB_GEOMETRY_SHADER;
    else if (Model->IsCS())
        Type = D3D11_SB_COMPUTE_SHADER;
    else
        return FALSE;

    if (!Declarations())
        return FALSE;
    Dominators.recalculate(*Entry);
    PostDominators.recalculate(*Entry);
    Loops.Analyze(Dominators);
    if (!Region(&Entry->getEntryBlock(), nullptr) || Failed)
        return FALSE;

    Program->clear();
    Program->push_back(ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(Type, 5, 0));
    Program->push_back(0);
    Program->insert(Program->end(), Decls.begin(), Decls.end());
    Program->insert(Program->end(), { ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_TEMPS) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2),
                                      (NextSlot + 3) / 4 });
    Program->insert(Program->end(), Body.begin(), Body.end());
    (*Program)[1] = (UINT)Program->size();
    return TRUE;
}

} /* namespace */

BOOLEAN
RxWarpIsDxil(
    _In_ const UINT *Code)
{
    return Code != nullptr && Code[2] == RxWarpDxilMagic;
}

BOOLEAN
RxWarpDxilAttach(VOID)
{
    if (FAILED(DxcInitThreadMalloc()))
        return FALSE;
    DxcSetThreadMallocToDefault();
    if (llvm::sys::fs::SetupPerThreadFileSystem())
    {
        DxcClearThreadMalloc();
        DxcCleanupThreadMalloc();
        return FALSE;
    }
    DxcClearThreadMalloc();
    return TRUE;
}

VOID
RxWarpDxilDetach(VOID)
{
    DxcSetThreadMallocToDefault();
    llvm::sys::fs::CleanupPerThreadFileSystem();
    llvm::llvm_shutdown();
    DxcClearThreadMalloc();
    DxcCleanupThreadMalloc();
}

UINT *
RxWarpLowerDxil(
    _In_ const UINT *Code)
{
    DxcThreadMalloc Scope(nullptr);
    const BYTE *Bitcode = reinterpret_cast<const BYTE *>(Code) + 8 + Code[4];
    UINT *Tokens = nullptr;

    try
    {
        llvm::LLVMContext Context;
        llvm::MemoryBufferRef Buffer(llvm::StringRef(reinterpret_cast<const char *>(Bitcode), Code[5]), "");
        llvm::ErrorOr<std::unique_ptr<llvm::Module>> Module = llvm::parseBitcodeFile(Buffer, Context);
        RxTokens Program;

        if (!Module)
            return nullptr;
        hlsl::DxilModule &Dxil = (*Module)->GetOrCreateDxilModule();
        RxLowering Lowering(Dxil);

        if (!Lowering.Run(&Program))
            return nullptr;

        /* The program, then how many slot sources follow it, then the sources */
        Program.push_back((UINT)(Lowering.Sources.size() / 4));
        Program.insert(Program.end(), Lowering.Sources.begin(), Lowering.Sources.end());
        Tokens = static_cast<UINT *>(HeapAlloc(GetProcessHeap(), 0, Program.size() * sizeof(UINT)));
        if (Tokens != nullptr)
            memcpy(Tokens, Program.data(), Program.size() * sizeof(UINT));
    }
    catch (...)
    {
        return nullptr;
    }
    return Tokens;
}

VOID
RxWarpFreeLowered(
    _In_opt_ UINT *Tokens)
{
    if (Tokens != nullptr)
        HeapFree(GetProcessHeap(), 0, Tokens);
}
