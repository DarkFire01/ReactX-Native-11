/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Work graphs: nodes from node shaders, the checks a graph must pass, its driver desc and properties
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "stateobject.hpp"

/* No node of that identity */
constexpr UINT RxNoNode = UINT_MAX;

/* An output array of this size reaches every node of its name from the base index up */
constexpr UINT RxUnboundedArray = UINT_MAX;

/* What the queries answer for an index that is out of range */
constexpr UINT RxNoAnswer = UINT_MAX;

static_assert(sizeof(D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS) == sizeof(D3D12DDI_WORK_GRAPH_MEMORY_REQUIREMENTS_0108));

/* Overrides in one shape whatever launch type they were written for */
struct RxNodeOverrides
{
    const UINT* LocalRootIndex;
    const BOOL* ProgramEntry;
    const D3D12_NODE_ID* NewName;
    const D3D12_NODE_ID* ShareInputOf;
    const UINT* DispatchGrid;
    const UINT* MaxDispatchGrid;
    UINT OutputCount;
    const D3D12_NODE_OUTPUT_OVERRIDES* Outputs;
};

/* What building tracks per node beyond the node itself */
struct RxNodeLinks
{
    BOOLEAN Populated;
    BOOLEAN SelfRecursive;
    BOOLEAN Visited;
    UINT ShareTarget;
    /* The first node of this name, which stands for the whole node array */
    UINT Leader;
    BOOLEAN InProgress;
    UINT Pass;
    UINT Depth;
    RxGrowList<UINT> Producers;
    RxGrowList<UINT> Sharers;
    /* Per output, the nodes it reaches */
    RxGrowList<UINT>* Targets;
};

template <typename Overrides>
static
VOID
RxCopyCommonOverrides(
    _In_ const Overrides& Source,
    _Out_ RxNodeOverrides* Out)
{
    Out->LocalRootIndex = Source.pLocalRootArgumentsTableIndex;
    Out->ProgramEntry = Source.pProgramEntry;
    Out->NewName = Source.pNewName;
    Out->ShareInputOf = Source.pShareInputOf;
    Out->OutputCount = Source.NumOutputOverrides;
    Out->Outputs = Source.pOutputOverrides;
}

/* Typed overrides must be for the shader's launch type; common ones fit any */
static
BOOLEAN
RxReadOverrides(
    _In_ const D3D12_SHADER_NODE& Shader,
    _In_ UINT Launch,
    _Out_ RxNodeOverrides* Out)
{
    ZeroMemory(Out, sizeof(*Out));

    switch (Shader.OverridesType)
    {
        case D3D12_NODE_OVERRIDES_TYPE_NONE:
            return TRUE;

        case D3D12_NODE_OVERRIDES_TYPE_BROADCASTING_LAUNCH:
            if (Launch != D3D12DDI_NODE_PROPERTIES_TYPE_BROADCASTING_LAUNCH_0108 || Shader.pBroadcastingLaunchOverrides == nullptr)
                return FALSE;
            RxCopyCommonOverrides(*Shader.pBroadcastingLaunchOverrides, Out);
            Out->DispatchGrid = Shader.pBroadcastingLaunchOverrides->pDispatchGrid;
            Out->MaxDispatchGrid = Shader.pBroadcastingLaunchOverrides->pMaxDispatchGrid;
            break;

        case D3D12_NODE_OVERRIDES_TYPE_COALESCING_LAUNCH:
            if (Launch != D3D12DDI_NODE_PROPERTIES_TYPE_COALESCING_LAUNCH_0108 || Shader.pCoalescingLaunchOverrides == nullptr)
                return FALSE;
            RxCopyCommonOverrides(*Shader.pCoalescingLaunchOverrides, Out);
            break;

        case D3D12_NODE_OVERRIDES_TYPE_THREAD_LAUNCH:
            if (Launch != D3D12DDI_NODE_PROPERTIES_TYPE_THREAD_LAUNCH_0108 || Shader.pThreadLaunchOverrides == nullptr)
                return FALSE;
            RxCopyCommonOverrides(*Shader.pThreadLaunchOverrides, Out);
            break;

        case D3D12_NODE_OVERRIDES_TYPE_COMMON_COMPUTE:
            if (Shader.pCommonComputeNodeOverrides == nullptr)
                return FALSE;
            RxCopyCommonOverrides(*Shader.pCommonComputeNodeOverrides, Out);
            break;

        default:
            return FALSE;
    }

    return Out->OutputCount == 0 || Out->Outputs != nullptr;
}

static
BOOLEAN
RxSameGrid(
    _In_ const RxDxilNodeRecord& Output,
    _In_ const RxDxilNodeRecord& Input)
{
    if (Output.HasGrid != Input.HasGrid)
        return FALSE;
    if (!Output.HasGrid)
        return TRUE;
    return Output.Grid.ByteOffset == Input.Grid.ByteOffset && Output.Grid.NumComponents == Input.Grid.NumComponents &&
           Output.Grid.b16BitsPerComponent == Input.Grid.b16BitsPerComponent;
}

static
BOOLEAN
RxHasLocalRoot(
    _In_ const RxFunctionExport& Function)
{
    for (UINT Index = 0; Index < Function.AssociationCount; Index++)
    {
        if (Function.Associations[Index]->Slot == RxSlotLocalRoot)
            return TRUE;
    }
    return FALSE;
}

class RxGraphBuilder
{
public:
    RxGraphBuilder(
        _In_ RxArena& Owner,
        _In_ const D3D12_WORK_GRAPH_DESC& GraphDesc,
        _In_ UINT Count,
        _In_reads_(Count) const RxFunctionExport* List)
        : Arena(Owner),
          Desc(GraphDesc),
          FunctionCount(Count),
          Functions(List)
    {
    }

    HRESULT
    Build(
        _Out_ RxWorkGraph* Graph);

private:
    BOOLEAN
    IsNodeShader(
        _In_ UINT Function) const
    {
        return Functions[Function].Kind == RxDxilKindNode && Functions[Function].Source != nullptr &&
               Functions[Function].Source->Node != nullptr;
    }

    UINT
    FindNode(
        _In_z_ LPCWSTR Name,
        _In_ UINT Index) const;

    UINT
    FindShader(
        _In_z_ LPCWSTR Name,
        _In_ UINT Index) const;

    HRESULT
    AddNode(
        _In_ UINT Function,
        _In_opt_ const RxNodeOverrides* Overrides,
        _In_ BOOLEAN Explicit,
        _Out_ UINT* Added);

    HRESULT
    AddExplicitNodes();

    HRESULT
    AddEntrypoints();

    HRESULT
    Populate(
        _In_ UINT Node);

    HRESULT
    Connect();

    HRESULT
    Visit(
        _In_ UINT Node,
        _In_ UINT Depth);

    HRESULT
    CheckDepth();

    VOID
    AssignLocalRootIndices();

    HRESULT
    Finish(
        _Out_ RxWorkGraph* Graph);

    _Ret_maybenull_
    const D3D12DDI_NODE_LIST_ENTRY_0108*
    MakeList(
        _In_ UINT Count,
        _In_reads_opt_(Count) const UINT* Indices,
        _In_reads_(NodeCount) D3D12DDI_NODE_0108* const* DdiNodes);

    RxArena& Arena;
    RxArena Scratch;
    const D3D12_WORK_GRAPH_DESC& Desc;
    UINT FunctionCount;
    const RxFunctionExport* Functions;
    BOOLEAN* Used = nullptr;
    RxGrowList<RxGraphNode> Nodes;
    RxGrowList<RxNodeLinks> Links;
    UINT Pass = 0;
    UINT NodeCount = 0;
};

UINT
RxGraphBuilder::FindNode(
    _In_z_ LPCWSTR Name,
    _In_ UINT Index) const
{
    for (UINT Node = 0; Node < Nodes.Count; Node++)
    {
        if (Nodes.Items[Node].Index == Index && wcscmp(Nodes.Items[Node].Name, Name) == 0)
            return Node;
    }
    return RxNoNode;
}

/* A node shader not in the graph yet whose own identity is the one asked for */
UINT
RxGraphBuilder::FindShader(
    _In_z_ LPCWSTR Name,
    _In_ UINT Index) const
{
    for (UINT Function = 0; Function < FunctionCount; Function++)
    {
        const RxDxilNode* Node;

        if (!IsNodeShader(Function) || Used[Function])
            continue;
        Node = Functions[Function].Source->Node;
        if (Node->Index == Index && wcscmp(Node->Name, Name) == 0)
            return Function;
    }
    return RxNoNode;
}

HRESULT
RxGraphBuilder::AddNode(
    _In_ UINT Function,
    _In_opt_ const RxNodeOverrides* Overrides,
    _In_ BOOLEAN Explicit,
    _Out_ UINT* Added)
{
    const RxDxilNode* Source = Functions[Function].Source->Node;
    RxGraphNode Node = {};
    RxNodeLinks Link = {};
    HRESULT hr;

    *Added = RxNoNode;

    Node.Function = &Functions[Function];
    Node.Source = Source;
    Node.Explicit = Explicit;
    Node.Name = Source->Name;
    Node.Index = Source->Index;
    Node.ProgramEntry = Source->ProgramEntry;
    Node.HasLocalRootIndex = Source->HasLocalRootIndex;
    Node.LocalRootIndex = Source->LocalRootIndex;
    Node.ShareInputOf = Source->ShareInputOf;
    Node.ShareInputIndex = Source->ShareInputIndex;
    Node.DispatchGrid = Source->HasDispatchGrid ? Source->DispatchGrid : nullptr;
    Node.MaxDispatchGrid = Source->HasMaxDispatchGrid ? Source->MaxDispatchGrid : nullptr;

    Node.OutputCount = Source->OutputCount;
    Node.Outputs = Arena.AllocateArray<RxGraphOutput>(Source->OutputCount);
    Link.Targets = Scratch.AllocateArray<RxGrowList<UINT>>(Source->OutputCount);
    if (Node.Outputs == nullptr || Link.Targets == nullptr)
        return E_OUTOFMEMORY;

    for (UINT Each = 0; Each < Source->OutputCount; Each++)
    {
        const RxDxilNodeRecord& Record = Source->Outputs[Each];
        RxGraphOutput& Output = Node.Outputs[Each];

        Output.Source = &Record;
        Output.Target = Record.Target;
        Output.TargetIndex = Record.TargetIndex;
        Output.ArraySize = Record.ArraySize != 0 ? Record.ArraySize : 1;
        Output.AllowSparse = Record.AllowSparse;
        Output.HasMaxRecords = Record.HasMaxRecords;
        Output.MaxRecords = Record.MaxRecords;
        Output.HasSharedWith = Record.HasSharedWith;
        Output.SharedWith = Record.SharedWith;
    }

    if (Overrides != nullptr)
    {
        if (Overrides->NewName != nullptr)
        {
            if (Overrides->NewName->Name == nullptr)
                return E_INVALIDARG;
            Node.Name = Arena.CopyString(Overrides->NewName->Name);
            Node.Index = Overrides->NewName->ArrayIndex;
        }
        if (Overrides->ProgramEntry != nullptr)
            Node.ProgramEntry = *Overrides->ProgramEntry != FALSE;
        if (Overrides->LocalRootIndex != nullptr)
        {
            Node.HasLocalRootIndex = TRUE;
            Node.LocalRootIndex = *Overrides->LocalRootIndex;
        }
        if (Overrides->ShareInputOf != nullptr)
        {
            if (Overrides->ShareInputOf->Name == nullptr)
                return E_INVALIDARG;
            Node.ShareInputOf = Arena.CopyString(Overrides->ShareInputOf->Name);
            Node.ShareInputIndex = Overrides->ShareInputOf->ArrayIndex;
        }
        if (Overrides->DispatchGrid != nullptr)
        {
            UINT* Grid = Arena.AllocateArray<UINT>(3);

            if (Grid == nullptr)
                return E_OUTOFMEMORY;
            CopyMemory(Grid, Overrides->DispatchGrid, sizeof(*Grid) * 3);
            Node.DispatchGrid = Grid;
        }
        if (Overrides->MaxDispatchGrid != nullptr)
        {
            UINT* Grid = Arena.AllocateArray<UINT>(3);

            if (Grid == nullptr)
                return E_OUTOFMEMORY;
            CopyMemory(Grid, Overrides->MaxDispatchGrid, sizeof(*Grid) * 3);
            Node.MaxDispatchGrid = Grid;
        }

        for (UINT Each = 0; Each < Overrides->OutputCount; Each++)
        {
            const D3D12_NODE_OUTPUT_OVERRIDES& Change = Overrides->Outputs[Each];
            RxGraphOutput* Output;

            if (Change.OutputIndex >= Node.OutputCount)
            {
                RX_TRACE(L"work graph: override names output %u of a node with %u", Change.OutputIndex, Node.OutputCount);
                return E_INVALIDARG;
            }

            Output = &Node.Outputs[Change.OutputIndex];
            if (Change.pNewName != nullptr)
            {
                if (Change.pNewName->Name == nullptr)
                    return E_INVALIDARG;
                Output->Target = Arena.CopyString(Change.pNewName->Name);
                Output->TargetIndex = Change.pNewName->ArrayIndex;
            }
            if (Change.pAllowSparseNodes != nullptr)
                Output->AllowSparse = *Change.pAllowSparseNodes != FALSE;
            if (Change.pMaxRecords != nullptr)
            {
                Output->HasMaxRecords = TRUE;
                Output->MaxRecords = *Change.pMaxRecords;
                Output->HasSharedWith = FALSE;
            }
            if (Change.pMaxRecordsSharedWithOutputIndex != nullptr)
            {
                Output->HasSharedWith = TRUE;
                Output->SharedWith = *Change.pMaxRecordsSharedWithOutputIndex;
                Output->HasMaxRecords = FALSE;
            }
        }
    }

    if (Node.Name == nullptr)
        return E_OUTOFMEMORY;
    if (FindNode(Node.Name, Node.Index) != RxNoNode)
    {
        RX_TRACE(L"work graph: node %ls[%u] is defined twice", Node.Name, Node.Index);
        return E_INVALIDARG;
    }

    Link.ShareTarget = RxNoNode;
    Link.Leader = Nodes.Count;
    for (UINT Other = 0; Other < Nodes.Count; Other++)
    {
        if (wcscmp(Nodes.Items[Other].Name, Node.Name) == 0)
        {
            Link.Leader = Links.Items[Other].Leader;
            break;
        }
    }

    hr = Nodes.Push(Scratch, Node);
    if (SUCCEEDED(hr))
        hr = Links.Push(Scratch, Link);
    if (FAILED(hr))
        return hr;

    Used[Function] = TRUE;
    *Added = Nodes.Count - 1;
    return S_OK;
}

HRESULT
RxGraphBuilder::AddExplicitNodes()
{
    HRESULT hr;

    for (UINT Each = 0; Each < Desc.NumExplicitlyDefinedNodes; Each++)
    {
        const D3D12_NODE& Explicit = Desc.pExplicitlyDefinedNodes[Each];
        RxNodeOverrides Overrides;
        UINT Function = RxNoNode;
        UINT Added;

        if (Explicit.NodeType != D3D12_NODE_TYPE_SHADER || Explicit.Shader.Shader == nullptr)
        {
            RX_TRACE(L"work graph %ls: node %u is not a shader node", Desc.ProgramName, Each);
            return E_INVALIDARG;
        }

        for (UINT Index = 0; Index < FunctionCount && Function == RxNoNode; Index++)
        {
            if (IsNodeShader(Index) && wcscmp(Functions[Index].Name, Explicit.Shader.Shader) == 0)
                Function = Index;
        }
        if (Function == RxNoNode)
        {
            RX_TRACE(L"work graph %ls: node %u names shader %ls, which is not a node shader here", Desc.ProgramName, Each,
                     Explicit.Shader.Shader);
            return E_INVALIDARG;
        }

        if (!RxReadOverrides(Explicit.Shader, Functions[Function].Source->Node->Launch, &Overrides))
        {
            RX_TRACE(L"work graph %ls: overrides of node %ls do not fit its launch type", Desc.ProgramName,
                     Explicit.Shader.Shader);
            return E_INVALIDARG;
        }

        hr = AddNode(Function, &Overrides, TRUE, &Added);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* Listed entries are found among the nodes so far, or added from a shader of that identity */
HRESULT
RxGraphBuilder::AddEntrypoints()
{
    HRESULT hr;

    for (UINT Each = 0; Each < Desc.NumEntrypoints; Each++)
    {
        const D3D12_NODE_ID& Id = Desc.pEntrypoints[Each];
        UINT Node;

        if (Id.Name == nullptr)
            return E_INVALIDARG;

        Node = FindNode(Id.Name, Id.ArrayIndex);
        if (Node == RxNoNode)
        {
            UINT Function = FindShader(Id.Name, Id.ArrayIndex);

            if (Function == RxNoNode)
            {
                RX_TRACE(L"work graph %ls: entry %ls[%u] is not a node", Desc.ProgramName, Id.Name, Id.ArrayIndex);
                return E_INVALIDARG;
            }
            hr = AddNode(Function, nullptr, FALSE, &Node);
            if (FAILED(hr))
                return hr;
        }
        Nodes.Items[Node].ProgramEntry = TRUE;
    }
    return S_OK;
}

/* Adds the shaders a node's outputs reach that are not nodes yet, depth first */
HRESULT
RxGraphBuilder::Populate(
    _In_ UINT Node)
{
    HRESULT hr;

    if (Links.Items[Node].Populated)
        return S_OK;
    Links.Items[Node].Populated = TRUE;

    for (UINT Each = 0; Each < Nodes.Items[Node].OutputCount; Each++)
    {
        /* Copied, as adding nodes moves them */
        RxGraphOutput Output = Nodes.Items[Node].Outputs[Each];

        if (Output.Target == nullptr)
            continue;

        if (Output.ArraySize == RxUnboundedArray)
        {
            for (UINT Other = 0; Other < Nodes.Count; Other++)
            {
                if (Nodes.Items[Other].Index < Output.TargetIndex || wcscmp(Nodes.Items[Other].Name, Output.Target) != 0)
                    continue;
                hr = Populate(Other);
                if (FAILED(hr))
                    return hr;
            }

            for (UINT Function = 0; Function < FunctionCount; Function++)
            {
                const RxDxilNode* Shader;
                UINT Added;

                if (!IsNodeShader(Function) || Used[Function])
                    continue;
                Shader = Functions[Function].Source->Node;
                if (Shader->Index < Output.TargetIndex || wcscmp(Shader->Name, Output.Target) != 0 ||
                    FindNode(Shader->Name, Shader->Index) != RxNoNode)
                {
                    continue;
                }

                hr = AddNode(Function, nullptr, FALSE, &Added);
                if (SUCCEEDED(hr))
                    hr = Populate(Added);
                if (FAILED(hr))
                    return hr;
            }
            continue;
        }

        for (UINT Offset = 0; Offset < Output.ArraySize && Output.TargetIndex + Offset >= Output.TargetIndex; Offset++)
        {
            UINT Index = Output.TargetIndex + Offset;
            UINT Existing = FindNode(Output.Target, Index);
            UINT Function;
            UINT Added;

            if (Existing != RxNoNode)
            {
                hr = Populate(Existing);
                if (FAILED(hr))
                    return hr;
                continue;
            }

            /* A target that is nowhere is reported when the graph is connected */
            Function = FindShader(Output.Target, Index);
            if (Function == RxNoNode)
                continue;

            hr = AddNode(Function, nullptr, FALSE, &Added);
            if (SUCCEEDED(hr))
                hr = Populate(Added);
            if (FAILED(hr))
                return hr;
        }
    }
    return S_OK;
}

/* Finds what each output reaches, checks the records agree, and settles which nodes are entries */
HRESULT
RxGraphBuilder::Connect()
{
    HRESULT hr;

    for (UINT Node = 0; Node < Nodes.Count; Node++)
    {
        RxGraphNode& Producer = Nodes.Items[Node];

        for (UINT Each = 0; Each < Producer.OutputCount; Each++)
        {
            const RxGraphOutput& Output = Producer.Outputs[Each];
            RxGrowList<UINT>& Targets = Links.Items[Node].Targets[Each];

            if (Output.Target == nullptr)
            {
                RX_TRACE(L"work graph %ls: output %u of %ls has no target", Desc.ProgramName, Each, Producer.Name);
                return E_INVALIDARG;
            }

            if (Output.ArraySize == RxUnboundedArray)
            {
                for (UINT Other = 0; Other < Nodes.Count; Other++)
                {
                    if (Nodes.Items[Other].Index >= Output.TargetIndex && wcscmp(Nodes.Items[Other].Name, Output.Target) == 0)
                    {
                        hr = Targets.Push(Scratch, Other);
                        if (FAILED(hr))
                            return hr;
                    }
                }
            }
            else
            {
                for (UINT Offset = 0; Offset < Output.ArraySize; Offset++)
                {
                    UINT Target = Output.TargetIndex + Offset < Output.TargetIndex
                                      ? RxNoNode
                                      : FindNode(Output.Target, Output.TargetIndex + Offset);

                    if (Target == RxNoNode)
                    {
                        if (Output.AllowSparse)
                            continue;
                        RX_TRACE(L"work graph %ls: %ls targets %ls[%u], which is not a node", Desc.ProgramName, Producer.Name,
                                 Output.Target, Output.TargetIndex + Offset);
                        return E_INVALIDARG;
                    }
                    hr = Targets.Push(Scratch, Target);
                    if (FAILED(hr))
                        return hr;
                }
            }

            for (UINT Reached = 0; Reached < Targets.Count; Reached++)
            {
                UINT Target = Targets.Items[Reached];
                const RxGraphNode& Consumer = Nodes.Items[Target];
                RxGrowList<UINT>& Producers = Links.Items[Target].Producers;

                if (!Consumer.Source->HasInput || Consumer.Source->Input.Size != Output.Source->Size ||
                    !RxSameGrid(*Output.Source, Consumer.Source->Input))
                {
                    RX_TRACE(L"work graph %ls: %ls output %u does not match the input of %ls[%u]", Desc.ProgramName,
                             Producer.Name, Each, Consumer.Name, Consumer.Index);
                    return E_INVALIDARG;
                }

                if (Target == Node)
                    Links.Items[Node].SelfRecursive = TRUE;
                if (Producers.Count == 0 || Producers.Items[Producers.Count - 1] != Node)
                {
                    hr = Producers.Push(Scratch, Node);
                    if (FAILED(hr))
                        return hr;
                }
            }
        }
    }

    for (UINT Node = 0; Node < Nodes.Count; Node++)
    {
        RxGraphNode& Sharer = Nodes.Items[Node];
        UINT Shared;

        if (Sharer.ShareInputOf == nullptr)
            continue;

        Shared = FindNode(Sharer.ShareInputOf, Sharer.ShareInputIndex);
        if (Shared == RxNoNode || Shared == Node)
        {
            RX_TRACE(L"work graph %ls: %ls shares the input of %ls[%u], which is not another node", Desc.ProgramName,
                     Sharer.Name, Sharer.ShareInputOf, Sharer.ShareInputIndex);
            return E_INVALIDARG;
        }

        Links.Items[Node].ShareTarget = Shared;
        hr = Links.Items[Shared].Sharers.Push(Scratch, Node);
        if (FAILED(hr))
            return hr;
    }

    /* Nodes nothing feeds are entries; ones the application named must say so themselves */
    for (UINT Node = 0; Node < Nodes.Count; Node++)
    {
        RxGraphNode& Each = Nodes.Items[Node];
        BOOLEAN Fed = Links.Items[Node].Producers.Count != 0 || Links.Items[Node].ShareTarget != RxNoNode;

        if (Each.Explicit && !Fed && !Each.ProgramEntry)
        {
            RX_TRACE(L"work graph %ls: %ls[%u] has no input source and is not declared an entry", Desc.ProgramName, Each.Name,
                     Each.Index);
            return E_INVALIDARG;
        }
        Each.Entry = Each.ProgramEntry || !Fed;
    }
    return S_OK;
}

/* Walks what a node feeds; self recursion adds its declared depth, other cycles are refused */
HRESULT
RxGraphBuilder::Visit(
    _In_ UINT Node,
    _In_ UINT Depth)
{
    RxNodeLinks& Link = Links.Items[Node];
    const RxGraphNode& Each = Nodes.Items[Node];
    RxNodeLinks& Array = Links.Items[Link.Leader];
    HRESULT hr;

    if (Link.SelfRecursive)
    {
        if (Each.Source->MaxRecursionDepth == 0)
        {
            RX_TRACE(L"work graph %ls: %ls recurses without a declared depth", Desc.ProgramName, Each.Name);
            return E_INVALIDARG;
        }
        Depth += Each.Source->MaxRecursionDepth;
    }
    if (Depth > D3D12_WORK_GRAPHS_MAX_NODE_DEPTH)
    {
        RX_TRACE(L"work graph %ls: %ls is %u deep", Desc.ProgramName, Each.Name, Depth);
        return E_INVALIDARG;
    }

    Link.Visited = TRUE;
    if (Array.InProgress)
    {
        RX_TRACE(L"work graph %ls: %ls is in a cycle", Desc.ProgramName, Each.Name);
        return E_INVALIDARG;
    }
    if (Link.Pass == Pass && Depth <= Link.Depth)
        return S_OK;

    Link.Pass = Pass;
    Link.Depth = Depth;
    Array.InProgress = TRUE;

    for (UINT Output = 0; Output < Each.OutputCount; Output++)
    {
        const RxGrowList<UINT>& Targets = Links.Items[Node].Targets[Output];

        for (UINT Reached = 0; Reached < Targets.Count; Reached++)
        {
            UINT Target = Targets.Items[Reached];
            const RxGrowList<UINT>& Sharers = Links.Items[Target].Sharers;

            if (Target == Node)
                continue;

            hr = Visit(Target, Depth + 1);
            for (UINT Sharer = 0; Sharer < Sharers.Count && SUCCEEDED(hr); Sharer++)
                hr = Visit(Sharers.Items[Sharer], Depth + 1);
            if (FAILED(hr))
                return hr;
        }
    }

    Links.Items[Link.Leader].InProgress = FALSE;
    return S_OK;
}

/* Every node must be reached from an entry within the depth limit */
HRESULT
RxGraphBuilder::CheckDepth()
{
    HRESULT hr;

    for (UINT Node = 0; Node < Nodes.Count; Node++)
    {
        const RxGrowList<UINT>& Sharers = Links.Items[Node].Sharers;

        if (!Nodes.Items[Node].Entry)
            continue;

        Pass++;
        hr = Visit(Node, 1);
        for (UINT Sharer = 0; Sharer < Sharers.Count && SUCCEEDED(hr); Sharer++)
        {
            Pass++;
            hr = Visit(Sharers.Items[Sharer], 1);
        }
        if (FAILED(hr))
            return hr;
    }

    for (UINT Node = 0; Node < Nodes.Count; Node++)
    {
        if (!Links.Items[Node].Visited)
        {
            RX_TRACE(L"work graph %ls: %ls[%u] is not reached from any entry", Desc.ProgramName, Nodes.Items[Node].Name,
                     Nodes.Items[Node].Index);
            return E_INVALIDARG;
        }
    }
    return S_OK;
}

/* Nodes with a local root signature and no index of their own get the lowest ones nobody claimed */
VOID
RxGraphBuilder::AssignLocalRootIndices()
{
    UINT Next = 0;

    for (UINT Node = 0; Node < Nodes.Count; Node++)
    {
        RxGraphNode& Each = Nodes.Items[Node];
        BOOLEAN Taken;

        if (Each.HasLocalRootIndex || !RxHasLocalRoot(*Each.Function))
            continue;

        do
        {
            Taken = FALSE;
            for (UINT Other = 0; Other < Nodes.Count && !Taken; Other++)
            {
                const RxGraphNode& Claimed = Nodes.Items[Other];

                Taken = Claimed.HasLocalRootIndex && Claimed.LocalRootIndex == Next;
            }
            if (Taken)
                Next++;
        } while (Taken);

        Each.HasLocalRootIndex = TRUE;
        Each.LocalRootIndex = Next++;
    }
}

_Ret_maybenull_
const D3D12DDI_NODE_LIST_ENTRY_0108*
RxGraphBuilder::MakeList(
    _In_ UINT Count,
    _In_reads_opt_(Count) const UINT* Indices,
    _In_reads_(NodeCount) D3D12DDI_NODE_0108* const* DdiNodes)
{
    D3D12DDI_NODE_LIST_ENTRY_0108* Entries;

    if (Count == 0)
        return nullptr;

    Entries = Arena.AllocateArray<D3D12DDI_NODE_LIST_ENTRY_0108>(Count);
    if (Entries == nullptr)
        return nullptr;

    for (UINT Each = 0; Each < Count; Each++)
    {
        Entries[Each].pNode = DdiNodes[Indices != nullptr ? Indices[Each] : Each];
        Entries[Each].pNext = Each + 1 < Count ? &Entries[Each + 1] : nullptr;
    }
    return Entries;
}

/* The driver desc: nodes in graph order, each with its inputs, sharers and output targets linked */
HRESULT
RxGraphBuilder::Finish(
    _Out_ RxWorkGraph* Graph)
{
    RxGraphNode* Final = Arena.AllocateArray<RxGraphNode>(Nodes.Count);
    D3D12DDI_NODE_0108** DdiNodes = Scratch.AllocateArray<D3D12DDI_NODE_0108*>(Nodes.Count);
    D3D12DDI_WORK_GRAPH_DESC_0108* Ddi = Arena.AllocateArray<D3D12DDI_WORK_GRAPH_DESC_0108>(1);
    RxGrowList<UINT> Entries;
    HRESULT hr;

    if (Final == nullptr || DdiNodes == nullptr || Ddi == nullptr)
        return E_OUTOFMEMORY;
    CopyMemory(Final, Nodes.Items, sizeof(*Final) * Nodes.Count);
    NodeCount = Nodes.Count;

    for (UINT Node = 0; Node < NodeCount; Node++)
    {
        DdiNodes[Node] = Arena.AllocateArray<D3D12DDI_NODE_0108>(1);
        if (DdiNodes[Node] == nullptr)
            return E_OUTOFMEMORY;
        if (Final[Node].Entry)
        {
            hr = Entries.Push(Scratch, Node);
            if (FAILED(hr))
                return hr;
        }
    }

    for (UINT Node = 0; Node < NodeCount; Node++)
    {
        RxGraphNode& Each = Final[Node];
        const RxNodeLinks& Link = Links.Items[Node];
        const RxDxilNode& Source = *Each.Source;
        D3D12DDI_NODE_0108& Driver = *DdiNodes[Node];
        D3D12DDI_NODE_OUTPUT_0108* Outputs = Arena.AllocateArray<D3D12DDI_NODE_OUTPUT_0108>(Each.OutputCount);
        D3D12DDI_NODE_ID_0108 Name;
        const UINT* LocalRootIndex = Each.HasLocalRootIndex ? &Each.LocalRootIndex : nullptr;
        const D3D12DDI_NODE_0108* SharedNode = Link.ShareTarget != RxNoNode ? DdiNodes[Link.ShareTarget] : nullptr;
        const D3D12DDI_NODE_LIST_ENTRY_0108* Inputs = MakeList(Link.Producers.Count, Link.Producers.Items, DdiNodes);
        const D3D12DDI_NODE_LIST_ENTRY_0108* Sharers = MakeList(Link.Sharers.Count, Link.Sharers.Items, DdiNodes);
        UINT InputKind = Source.HasInput ? Source.Input.Kind : 0;
        UINT InputFlags = Source.HasInput ? Source.Input.Flags : 0;
        UINT InputSize = Source.HasInput ? Source.Input.Size : 0;

        if (Outputs == nullptr || (Link.Producers.Count != 0 && Inputs == nullptr) || (Link.Sharers.Count != 0 && Sharers == nullptr))
            return E_OUTOFMEMORY;

        Name.Name = Each.Name;
        Name.ArrayIndex = Each.Index;

        for (UINT Output = 0; Output < Each.OutputCount; Output++)
        {
            const RxGraphOutput& From = Each.Outputs[Output];
            const RxGrowList<UINT>& Targets = Link.Targets[Output];
            D3D12DDI_NODE_OUTPUT_0108& To = Outputs[Output];

            To.FinalName.Name = From.Target;
            To.FinalName.ArrayIndex = From.TargetIndex;
            To.OutputIndex = Output;
            To.NodeIOKind = (D3D12DDI_NODE_IO_KIND_0108)From.Source->Kind;
            To.NodeIOFlags = From.Source->Flags;
            To.RecordSizeInBytes = From.Source->Size;
            To.bAllowSparseNodes = From.AllowSparse;
            To.pRecordDispatchGrid = From.Source->HasGrid ? &From.Source->Grid : nullptr;
            To.pMaxRecords = From.HasMaxRecords ? &From.MaxRecords : nullptr;
            To.pMaxRecordsSharedWithOutputIndex = From.HasSharedWith ? &From.SharedWith : nullptr;
            To.ArraySize = From.ArraySize;
            To.pOutputs = MakeList(Targets.Count, Targets.Items, DdiNodes);
            if (Targets.Count != 0 && To.pOutputs == nullptr)
                return E_OUTOFMEMORY;
        }

        Driver.VersionAdded = 0;
        Driver.NodeType = D3D12DDI_NODE_TYPE_SHADER_0108;
        Driver.Shader.Shader = Each.Function->Name;
        Driver.Shader.PropertiesType = (D3D12DDI_NODE_PROPERTIES_TYPE_0108)Source.Launch;

        switch (Source.Launch)
        {
            case D3D12DDI_NODE_PROPERTIES_TYPE_BROADCASTING_LAUNCH_0108:
            {
                D3D12DDI_BROADCASTING_LAUNCH_NODE_PROPERTIES_0108* Properties =
                    Arena.AllocateArray<D3D12DDI_BROADCASTING_LAUNCH_NODE_PROPERTIES_0108>(1);

                if (Properties == nullptr)
                    return E_OUTOFMEMORY;
                Properties->FinalName = Name;
                Properties->bProgramEntry = Each.Entry;
                Properties->InputNodeIOKind = (D3D12DDI_NODE_IO_KIND_0108)InputKind;
                Properties->InputNodeIOFlags = InputFlags;
                Properties->InputRecordSizeInBytes = InputSize;
                Properties->GroupSharedUsageInBytes = Source.GroupShared;
                Properties->MaxRecursionDepth = Source.MaxRecursionDepth;
                Properties->pLocalRootArgumentsTableIndex = LocalRootIndex;
                Properties->pShareInputOfNode = SharedNode;
                Properties->pDispatchGrid = Each.DispatchGrid;
                Properties->pMaxDispatchGrid = Each.MaxDispatchGrid;
                Properties->pRecordDispatchGrid = Source.HasInput && Source.Input.HasGrid ? &Source.Input.Grid : nullptr;
                Properties->pInputNodes = Inputs;
                Properties->pNodesSharingInputWithThisNode = Sharers;
                Properties->NumOutputs = Each.OutputCount;
                Properties->pOutputs = Outputs;
                Driver.Shader.pBroadcastingLaunchNodeProperties = Properties;
                break;
            }

            case D3D12DDI_NODE_PROPERTIES_TYPE_COALESCING_LAUNCH_0108:
            {
                D3D12DDI_COALESCING_LAUNCH_NODE_PROPERTIES_0108* Properties =
                    Arena.AllocateArray<D3D12DDI_COALESCING_LAUNCH_NODE_PROPERTIES_0108>(1);

                if (Properties == nullptr)
                    return E_OUTOFMEMORY;
                Properties->FinalName = Name;
                Properties->bProgramEntry = Each.Entry;
                Properties->InputNodeIOKind = (D3D12DDI_NODE_IO_KIND_0108)InputKind;
                Properties->InputNodeIOFlags = InputFlags;
                Properties->InputRecordSizeInBytes = InputSize;
                Properties->InputMaxRecordArraySize = Source.HasInput && Source.Input.HasMaxRecords ? Source.Input.MaxRecords : 1;
                Properties->GroupSharedUsageInBytes = Source.GroupShared;
                Properties->MaxRecursionDepth = Source.MaxRecursionDepth;
                Properties->pLocalRootArgumentsTableIndex = LocalRootIndex;
                Properties->pShareInputOfNode = SharedNode;
                Properties->pInputNodes = Inputs;
                Properties->pNodesSharingInputWithThisNode = Sharers;
                Properties->NumOutputs = Each.OutputCount;
                Properties->pOutputs = Outputs;
                Driver.Shader.pCoalescingLaunchNodeProperties = Properties;
                break;
            }

            default:
            {
                D3D12DDI_THREAD_LAUNCH_NODE_PROPERTIES_0108* Properties =
                    Arena.AllocateArray<D3D12DDI_THREAD_LAUNCH_NODE_PROPERTIES_0108>(1);

                if (Properties == nullptr)
                    return E_OUTOFMEMORY;
                Properties->FinalName = Name;
                Properties->bProgramEntry = Each.Entry;
                Properties->InputNodeIOKind = (D3D12DDI_NODE_IO_KIND_0108)InputKind;
                Properties->InputNodeIOFlags = InputFlags;
                Properties->InputRecordSizeInBytes = InputSize;
                Properties->GroupSharedUsageInBytes = Source.GroupShared;
                Properties->MaxRecursionDepth = Source.MaxRecursionDepth;
                Properties->pLocalRootArgumentsTableIndex = LocalRootIndex;
                Properties->pShareInputOfNode = SharedNode;
                Properties->pInputNodes = Inputs;
                Properties->pNodesSharingInputWithThisNode = Sharers;
                Properties->NumOutputs = Each.OutputCount;
                Properties->pOutputs = Outputs;
                Driver.Shader.pThreadLaunchNodeProperties = Properties;
                break;
            }
        }
        Each.Ddi = &Driver;
    }

    Graph->Name = Arena.CopyString(Desc.ProgramName);
    Graph->NodeCount = NodeCount;
    Graph->Nodes = Final;
    Graph->EntryCount = Entries.Count;
    Graph->Entries = Arena.AllocateArray<UINT>(Entries.Count);
    if (Graph->Name == nullptr || Graph->Entries == nullptr)
        return E_OUTOFMEMORY;
    if (Entries.Count != 0)
        CopyMemory(Graph->Entries, Entries.Items, sizeof(*Entries.Items) * Entries.Count);

    Ddi->ProgramName = Graph->Name;
    Ddi->Version = 0;
    Ddi->Flags = D3D12DDI_WORK_GRAPH_FLAG_NONE;
    Ddi->NumNodes = NodeCount;
    Ddi->pNodes = MakeList(NodeCount, nullptr, DdiNodes);
    Ddi->NumEntrypoints = Entries.Count;
    Ddi->pEntrypoints = MakeList(Entries.Count, Entries.Items, DdiNodes);
    if (Ddi->pNodes == nullptr || (Entries.Count != 0 && Ddi->pEntrypoints == nullptr))
        return E_OUTOFMEMORY;

    Graph->Ddi = Ddi;
    return S_OK;
}

HRESULT
RxGraphBuilder::Build(
    _Out_ RxWorkGraph* Graph)
{
    HRESULT hr;

    ZeroMemory(Graph, sizeof(*Graph));

    if (Desc.ProgramName == nullptr || Desc.ProgramName[0] == L'\0')
    {
        RX_TRACE(L"work graph: no program name");
        return E_INVALIDARG;
    }
    if ((Desc.Flags & ~D3D12_WORK_GRAPH_FLAG_INCLUDE_ALL_AVAILABLE_NODES) != 0 ||
        (Desc.NumEntrypoints != 0 && Desc.pEntrypoints == nullptr) ||
        (Desc.NumExplicitlyDefinedNodes != 0 && Desc.pExplicitlyDefinedNodes == nullptr))
    {
        RX_TRACE(L"work graph %ls: flags 0x%X or node lists are not valid", Desc.ProgramName, Desc.Flags);
        return E_INVALIDARG;
    }

    Used = Scratch.AllocateArray<BOOLEAN>(FunctionCount);
    if (Used == nullptr)
        return E_OUTOFMEMORY;

    hr = AddExplicitNodes();
    if (SUCCEEDED(hr))
        hr = AddEntrypoints();

    /* What entries reach comes in, then everything left when asked for */
    for (UINT Node = 0; Node < Nodes.Count && SUCCEEDED(hr); Node++)
    {
        if (Nodes.Items[Node].ProgramEntry)
            hr = Populate(Node);
    }

    if (SUCCEEDED(hr) && (Desc.Flags & D3D12_WORK_GRAPH_FLAG_INCLUDE_ALL_AVAILABLE_NODES) != 0)
    {
        for (UINT Function = 0; Function < FunctionCount && SUCCEEDED(hr); Function++)
        {
            UINT Added;

            if (!IsNodeShader(Function) || Used[Function])
                continue;
            if (FindNode(Functions[Function].Source->Node->Name, Functions[Function].Source->Node->Index) != RxNoNode)
                continue;

            hr = AddNode(Function, nullptr, FALSE, &Added);
            if (SUCCEEDED(hr))
                hr = Populate(Added);
        }
    }
    if (FAILED(hr))
        return hr;

    if (Nodes.Count == 0)
    {
        RX_TRACE(L"work graph %ls has no nodes", Desc.ProgramName);
        return E_INVALIDARG;
    }

    hr = Connect();
    if (SUCCEEDED(hr))
        hr = CheckDepth();
    if (FAILED(hr))
        return hr;

    AssignLocalRootIndices();
    return Finish(Graph);
}

HRESULT
RxBuildWorkGraph(
    _In_ RxArena& Arena,
    _In_ const D3D12_WORK_GRAPH_DESC& Desc,
    _In_ UINT FunctionCount,
    _In_reads_(FunctionCount) const RxFunctionExport* Functions,
    _Out_ RxWorkGraph* Graph)
{
    RxGraphBuilder Builder(Arena, Desc, FunctionCount, Functions);

    return Builder.Build(Graph);
}

/* Properties */

_Ret_maybenull_
const RxWorkGraph*
RxStateObject::FindWorkGraph(
    _In_z_ LPCWSTR Name) const
{
    for (UINT Index = 0; Index < WorkGraphCount; Index++)
    {
        if (wcscmp(WorkGraphs[Index].Name, Name) == 0)
            return &WorkGraphs[Index];
    }
    return nullptr;
}

UINT
STDMETHODCALLTYPE
RxStateObject::GetNumWorkGraphs(VOID)
{
    return WorkGraphCount;
}

LPCWSTR
STDMETHODCALLTYPE
RxStateObject::GetProgramName(
    UINT WorkGraphIndex)
{
    return WorkGraphIndex < WorkGraphCount ? WorkGraphs[WorkGraphIndex].Name : nullptr;
}

UINT
STDMETHODCALLTYPE
RxStateObject::GetWorkGraphIndex(
    LPCWSTR pProgramName)
{
    const RxWorkGraph* Graph = pProgramName != nullptr ? FindWorkGraph(pProgramName) : nullptr;

    return Graph != nullptr ? (UINT)(Graph - WorkGraphs) : RxNoAnswer;
}

UINT
STDMETHODCALLTYPE
RxStateObject::GetNumNodes(
    UINT WorkGraphIndex)
{
    return WorkGraphIndex < WorkGraphCount ? WorkGraphs[WorkGraphIndex].NodeCount : 0;
}

D3D12_NODE_ID
STDMETHODCALLTYPE
RxStateObject::GetNodeID(
    UINT WorkGraphIndex,
    UINT NodeIndex)
{
    D3D12_NODE_ID Id = {};

    if (WorkGraphIndex < WorkGraphCount && NodeIndex < WorkGraphs[WorkGraphIndex].NodeCount)
    {
        Id.Name = WorkGraphs[WorkGraphIndex].Nodes[NodeIndex].Name;
        Id.ArrayIndex = WorkGraphs[WorkGraphIndex].Nodes[NodeIndex].Index;
    }
    return Id;
}

UINT
STDMETHODCALLTYPE
RxStateObject::GetNodeIndex(
    UINT WorkGraphIndex,
    D3D12_NODE_ID NodeID)
{
    if (WorkGraphIndex >= WorkGraphCount || NodeID.Name == nullptr)
        return RxNoAnswer;

    for (UINT Node = 0; Node < WorkGraphs[WorkGraphIndex].NodeCount; Node++)
    {
        const RxGraphNode& Each = WorkGraphs[WorkGraphIndex].Nodes[Node];

        if (Each.Index == NodeID.ArrayIndex && wcscmp(Each.Name, NodeID.Name) == 0)
            return Node;
    }
    return RxNoAnswer;
}

UINT
STDMETHODCALLTYPE
RxStateObject::GetNodeLocalRootArgumentsTableIndex(
    UINT WorkGraphIndex,
    UINT NodeIndex)
{
    const RxGraphNode* Node;

    if (WorkGraphIndex >= WorkGraphCount || NodeIndex >= WorkGraphs[WorkGraphIndex].NodeCount)
        return RxNoAnswer;

    Node = &WorkGraphs[WorkGraphIndex].Nodes[NodeIndex];
    return Node->HasLocalRootIndex ? Node->LocalRootIndex : RxNoAnswer;
}

/* Unlike the node count, a bad graph index answers all ones */
UINT
STDMETHODCALLTYPE
RxStateObject::GetNumEntrypoints(
    UINT WorkGraphIndex)
{
    return WorkGraphIndex < WorkGraphCount ? WorkGraphs[WorkGraphIndex].EntryCount : RxNoAnswer;
}

D3D12_NODE_ID
STDMETHODCALLTYPE
RxStateObject::GetEntrypointID(
    UINT WorkGraphIndex,
    UINT EntrypointIndex)
{
    D3D12_NODE_ID Id = {};

    if (WorkGraphIndex < WorkGraphCount && EntrypointIndex < WorkGraphs[WorkGraphIndex].EntryCount)
    {
        const RxWorkGraph& Graph = WorkGraphs[WorkGraphIndex];

        Id.Name = Graph.Nodes[Graph.Entries[EntrypointIndex]].Name;
        Id.ArrayIndex = Graph.Nodes[Graph.Entries[EntrypointIndex]].Index;
    }
    return Id;
}

UINT
STDMETHODCALLTYPE
RxStateObject::GetEntrypointIndex(
    UINT WorkGraphIndex,
    D3D12_NODE_ID NodeID)
{
    if (WorkGraphIndex >= WorkGraphCount || NodeID.Name == nullptr)
        return RxNoAnswer;

    for (UINT Entry = 0; Entry < WorkGraphs[WorkGraphIndex].EntryCount; Entry++)
    {
        const RxWorkGraph& Graph = WorkGraphs[WorkGraphIndex];
        const RxGraphNode& Each = Graph.Nodes[Graph.Entries[Entry]];

        if (Each.Index == NodeID.ArrayIndex && wcscmp(Each.Name, NodeID.Name) == 0)
            return Entry;
    }
    return RxNoAnswer;
}

/* Entries without an input record answer zero */
UINT
STDMETHODCALLTYPE
RxStateObject::GetEntrypointRecordSizeInBytes(
    UINT WorkGraphIndex,
    UINT EntrypointIndex)
{
    const RxDxilNode* Source;

    if (WorkGraphIndex >= WorkGraphCount || EntrypointIndex >= WorkGraphs[WorkGraphIndex].EntryCount)
        return RxNoAnswer;

    Source = WorkGraphs[WorkGraphIndex].Nodes[WorkGraphs[WorkGraphIndex].Entries[EntrypointIndex]].Source;
    return Source->HasInput ? Source->Input.Size : 0;
}

UINT
STDMETHODCALLTYPE
RxStateObject::GetEntrypointRecordAlignmentInBytes(
    UINT WorkGraphIndex,
    UINT EntrypointIndex)
{
    const RxDxilNode* Source;

    if (WorkGraphIndex >= WorkGraphCount || EntrypointIndex >= WorkGraphs[WorkGraphIndex].EntryCount)
        return RxNoAnswer;

    Source = WorkGraphs[WorkGraphIndex].Nodes[WorkGraphs[WorkGraphIndex].Entries[EntrypointIndex]].Source;
    return Source->HasInput ? Source->Input.Alignment : 0;
}

VOID
STDMETHODCALLTYPE
RxStateObject::GetWorkGraphMemoryRequirements(
    UINT WorkGraphIndex,
    _Out_ D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS* pWorkGraphMemoryRequirements)
{
    if (WorkGraphIndex >= WorkGraphCount)
    {
        if (pWorkGraphMemoryRequirements != nullptr)
            ZeroMemory(pWorkGraphMemoryRequirements, sizeof(*pWorkGraphMemoryRequirements));
        return;
    }
    if (Device.GetWorkGraphsTier() < D3D12DDI_WORK_GRAPHS_TIER_1_0 || Device.Ddi().pfnGetWorkGraphMemoryRequirements == nullptr)
        return;

    AcquireSRWLockExclusive(&Lock);
    Device.Ddi().pfnGetWorkGraphMemoryRequirements(
        DriverObject, WorkGraphs[WorkGraphIndex].Name,
        reinterpret_cast<D3D12DDI_WORK_GRAPH_MEMORY_REQUIREMENTS_0108*>(pWorkGraphMemoryRequirements));
    ReleaseSRWLockExclusive(&Lock);
}

/* Identifiers come from the driver, and only executable objects have programs */
D3D12_PROGRAM_IDENTIFIER
STDMETHODCALLTYPE
RxStateObject::GetProgramIdentifier(
    LPCWSTR pProgramName)
{
    D3D12_PROGRAM_IDENTIFIER Identifier = {};
    D3D12DDI_PROGRAM_IDENTIFIER_0108 Driver;

    if (Type != D3D12_STATE_OBJECT_TYPE_EXECUTABLE)
    {
        RX_TRACE(L"state object: program identifiers exist only in executable state objects");
        return Identifier;
    }
    if (pProgramName == nullptr || FindWorkGraph(pProgramName) == nullptr)
        return Identifier;
    if (Device.GetWorkGraphsTier() < D3D12DDI_WORK_GRAPHS_TIER_1_0 || Device.Ddi().pfnGetProgramIdentifier == nullptr)
        return Identifier;

    AcquireSRWLockExclusive(&Lock);
    Driver = Device.Ddi().pfnGetProgramIdentifier(DriverObject, pProgramName);
    ReleaseSRWLockExclusive(&Lock);

    CopyMemory(&Identifier, &Driver, sizeof(Identifier));
    return Identifier;
}
