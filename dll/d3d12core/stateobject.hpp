/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Raytracing state objects, the DXIL libraries they are built from, and their driver form
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "objects.hpp"

/* Bump allocation whose memory lives as long as its owner */
class RxArena
{
public:
    RxArena() = default;
    ~RxArena();

    RxArena(const RxArena&) = delete;
    RxArena& operator=(const RxArena&) = delete;

    /* Zeroed and 16 byte aligned */
    _Ret_maybenull_
    VOID*
    Allocate(
        _In_ SIZE_T Size);

    template <typename Type>
    _Ret_maybenull_
    Type*
    AllocateArray(
        _In_ SIZE_T Count)
    {
        return static_cast<Type*>(Allocate(sizeof(Type) * (Count ? Count : 1)));
    }

    /* A null string stays null */
    _Ret_maybenull_
    LPWSTR
    CopyString(
        _In_opt_z_ LPCWSTR Text);

    /* UTF-8 widened; mangled names keep their leading 0x01 marker, which drivers expect */
    _Ret_maybenull_
    LPWSTR
    Widen(
        _In_z_ const char* Text);

private:
    struct RxBlock
    {
        RxBlock* Next;
        SIZE_T Used;
        SIZE_T Size;
    };

    RxBlock* Head = nullptr;
};

/* DXIL shader kinds as runtime data stores them */
constexpr UINT RxDxilKindLibrary = 6;
constexpr UINT RxDxilKindRayGeneration = 7;
constexpr UINT RxDxilKindIntersection = 8;
constexpr UINT RxDxilKindAnyHit = 9;
constexpr UINT RxDxilKindClosestHit = 10;
constexpr UINT RxDxilKindMiss = 11;
constexpr UINT RxDxilKindCallable = 12;

/* Resource classes as runtime data stores them */
constexpr UINT RxDxilClassSrv = 0;
constexpr UINT RxDxilClassUav = 1;
constexpr UINT RxDxilClassCbv = 2;
constexpr UINT RxDxilClassSampler = 3;

/* Subobject kinds a library may declare */
constexpr UINT RxDxilSubobjectConfig = 0;
constexpr UINT RxDxilSubobjectGlobalRoot = 1;
constexpr UINT RxDxilSubobjectLocalRoot = 2;
constexpr UINT RxDxilSubobjectAssociation = 8;
constexpr UINT RxDxilSubobjectShaderConfig = 9;
constexpr UINT RxDxilSubobjectPipelineConfig = 10;
constexpr UINT RxDxilSubobjectHitGroup = 11;
constexpr UINT RxDxilSubobjectPipelineConfig1 = 12;

/* One register range a function reads */
struct RxDxilBinding
{
    UINT Class;
    UINT Space;
    UINT Lower;
    UINT Upper;
};

/* A function in a library, with its names in driver form */
struct RxDxilFunction
{
    LPCWSTR Name;
    LPCWSTR Mangled;
    UINT Kind;
    UINT BindingCount;
    const RxDxilBinding* Bindings;
    UINT DependencyCount;
    LPCWSTR* Dependencies;
};

/* A subobject a library declares */
struct RxDxilSubobject
{
    UINT Kind;
    LPCWSTR Name;
    UINT Values[2];
    LPCWSTR Strings[3];
    const BYTE* Bytes;
    UINT ByteCount;
    UINT ExportCount;
    LPCWSTR* Exports;
};

/* A DXIL library container read once into arena memory */
class RxDxilLibrary
{
public:
    /* The container is copied into the arena; the driver is handed the copy */
    HRESULT
    Load(
        _In_ RxArena& Arena,
        _In_reads_bytes_(Size) const VOID* Container,
        _In_ SIZE_T Size);

    /* The program part the driver compiles */
    const UINT*
    GetProgram() const
    {
        return Program;
    }

    /* Program header version: kind in the high half, then major and minor nibbles */
    UINT
    GetVersion() const
    {
        return Version;
    }

    UINT FunctionCount = 0;
    RxDxilFunction* Functions = nullptr;
    UINT SubobjectCount = 0;
    RxDxilSubobject* Subobjects = nullptr;

private:
    const UINT* Program = nullptr;
    UINT Version = 0;
};

/* A mangled name with its identifier replaced, as exports that rename a function carry it */
_Ret_maybenull_
LPWSTR
RxRenameMangled(
    _In_ RxArena& Arena,
    _In_z_ LPCWSTR Mangled,
    _In_z_ LPCWSTR OldName,
    _In_z_ LPCWSTR NewName);

/* Association slots: the kinds of subobject that apply to exports */
enum RxSlot
{
    RxSlotGlobalRoot,
    RxSlotLocalRoot,
    RxSlotNodeMask,
    RxSlotShaderConfig,
    RxSlotPipelineConfig,
    RxSlotConfig,
    RxSlotCount,
    RxSlotNone = RxSlotCount,
};

/* No library: a definition from the API, or an export from a collection */
constexpr UINT RxNoLibrary = ~0u;

/* A subobject a state object can associate, in API and driver form */
struct RxSubobjectRecord
{
    RxSlot Slot;
    /* Where the definition came from: an API entry, or a library and the name it gave */
    const D3D12_STATE_SUBOBJECT* Api;
    UINT LibraryIndex;
    LPCWSTR Name;
    /* Position among the desc's definitions, which orders default associations */
    UINT Order;
    BOOLEAN Referenced;
    BOOLEAN DefaultForState;
    /* The library an empty DXIL association made this the default of */
    UINT DefaultLibrary;
    RxRootSignature* Root;
    UINT Values[2];
    D3D12DDI_STATE_SUBOBJECT_TYPE DdiType;
    VOID* DdiDesc;
};

class RxStateObject;

/* A function callers and hit groups find by name */
struct RxFunctionExport
{
    LPCWSTR Name;
    LPCWSTR Mangled;
    UINT Kind;
    const RxDxilFunction* Source;
    RxStateObject* Owner;
    UINT LibraryIndex;
    /* From a collection that keeps its definitions to itself */
    BOOLEAN Sealed;
    UINT Flags;
    UINT AssociationCount;
    RxSubobjectRecord** Associations;
    /* The driver node of a collection export that needs nothing more */
    const D3D12DDI_FUNCTION_SUMMARY_NODE_0054* Node;
};

/* A hit group; a library defined one is handed to the driver right after its library */
struct RxHitGroupExport
{
    LPCWSTR Name;
    D3D12_HIT_GROUP_TYPE Type;
    LPCWSTR Imports[3];
    RxStateObject* Owner;
    UINT LibraryIndex;
    UINT Flags;
    D3D12DDI_HIT_GROUP_DESC_0054* Ddi;
};

/* Hit group import slots */
constexpr UINT RxImportAnyHit = 0;
constexpr UINT RxImportClosestHit = 1;
constexpr UINT RxImportIntersection = 2;

class RxStateObject final : public RxDeviceChild12<ID3D12StateObject>, public ID3D12StateObjectProperties1
{
public:
    static constexpr GUID PrivateId = {0x6b3e91d4, 0x2a57, 0x4c08, {0x9f, 0x16, 0xd8, 0x40, 0x7c, 0xe3, 0x52, 0xa9}};

    /* Builds and checks the desc; Parent is the object an addition grows from */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_STATE_OBJECT_DESC& Desc,
        _In_opt_ RxStateObject* Parent,
        _Outptr_ RxStateObject** Object);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return RxDeviceChild12::AddRef();
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        return RxDeviceChild12::Release();
    }

    void*
    STDMETHODCALLTYPE
    GetShaderIdentifier(
        _In_ LPCWSTR pExportName) override;

    UINT64
    STDMETHODCALLTYPE
    GetShaderStackSize(
        _In_ LPCWSTR pExportName) override;

    UINT64
    STDMETHODCALLTYPE
    GetPipelineStackSize(VOID) override;

    void
    STDMETHODCALLTYPE
    SetPipelineStackSize(
        UINT64 PipelineStackSizeInBytes) override;

    D3D12_PROGRAM_IDENTIFIER
    STDMETHODCALLTYPE
    GetProgramIdentifier(
        LPCWSTR pProgramName) override;

    D3D12_STATE_OBJECT_TYPE
    GetType() const
    {
        return Type;
    }

    UINT
    GetFlags() const
    {
        return Flags;
    }

    D3D12DDI_HSTATEOBJECT_0054
    GetDriverHandle() const
    {
        return DriverObject;
    }

    /* What list entries take: a bypass header that leads the driver to its state object */
    D3D12DDI_API_HSTATEOBJECT
    Api()
    {
        D3D12DDI_API_HSTATEOBJECT Handle;

        Handle.pDrvPrivate = &Bypass;
        return Handle;
    }

    /* Lookups across this object and the ones it grew from */
    _Ret_maybenull_
    const RxHitGroupExport*
    FindHitGroup(
        _In_z_ LPCWSTR Name) const;

    /* The one function of this name, or nullptr with Count telling how many there were */
    _Ret_maybenull_
    const RxFunctionExport*
    FindFunction(
        _In_z_ LPCWSTR Name,
        _Out_opt_ UINT* Count) const;

    /* The definition shared by every raytracing shader, or nullptr when none has one */
    _Ret_maybenull_
    const RxSubobjectRecord*
    GetShared(
        _In_ RxSlot Slot) const
    {
        return Shared[Slot];
    }

    UINT FunctionCount = 0;
    RxFunctionExport* Functions = nullptr;
    UINT HitGroupCount = 0;
    RxHitGroupExport* HitGroups = nullptr;

    /* The summary the driver was given, which state objects using this one point into */
    UINT NodeCount = 0;
    const D3D12DDI_FUNCTION_SUMMARY_NODE_0054* Nodes = nullptr;

private:
    friend class RxStateBuilder;

    using RxDeviceChild12::RxDeviceChild12;
    ~RxStateObject() override;

    RxArena Arena;
    D3D12_STATE_OBJECT_TYPE Type = D3D12_STATE_OBJECT_TYPE_COLLECTION;
    UINT Flags = 0;
    RxStateObject* Parent = nullptr;
    const RxSubobjectRecord* Shared[RxSlotCount] = {};

    /* References this object holds on what the driver form names */
    UINT RootCount = 0;
    RxRootSignature** Roots = nullptr;
    UINT CollectionCount = 0;
    RxStateObject** Collections = nullptr;

    RxDriverMemory12 Memory;
    D3D12DDI_HSTATEOBJECT_0054 DriverObject = {};
    D3D12DDI_RUNTIME_BYPASS_HEADER Bypass = {};
    BOOLEAN IsDriverObjectCreated = FALSE;
    SRWLOCK Lock = SRWLOCK_INIT;
};
