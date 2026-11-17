/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Draws: input assembly, vertex shading, clipping, rasterization and output merging
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"
#include "tess.hpp"
#include "sample.hpp"

/* The view volume's six planes and up to eight from the shader */
static constexpr UINT RxWarpClipPlanes = 14;

/* A shaded vertex: every output register of the vertex shader */
typedef struct _RX_WARP_VERTEX
{
    UINT Registers[RxWarpMaxRegisters][4];
} RX_WARP_VERTEX;

/* A vertex after the viewport transform, in the rasterizer's 24.8 fixed point */
typedef struct _RX_WARP_SCREEN_VERTEX
{
    INT X;
    INT Y;
    FLOAT Z;
    FLOAT Rhw;
    const RX_WARP_VERTEX *Source;
} RX_WARP_SCREEN_VERTEX;

/* a * x + b * y + c over the triangle, in the biased screen space */
typedef struct _RX_WARP_PLANE
{
    FLOAT A;
    FLOAT B;
    FLOAT C;
} RX_WARP_PLANE;

static const D3D11_1_DDI_BLEND_DESC RxWarpDefaultBlend =
{
    FALSE, FALSE,
    {
        { FALSE, FALSE, D3D10_DDI_BLEND_ONE, D3D10_DDI_BLEND_ZERO, D3D10_DDI_BLEND_OP_ADD,
          D3D10_DDI_BLEND_ONE, D3D10_DDI_BLEND_ZERO, D3D10_DDI_BLEND_OP_ADD, D3D11_1_DDI_LOGIC_OP_NOOP, 0xF },
    }
};

static
FLOAT
RxWarpBitsAsFloat(
    _In_ UINT Bits)
{
    FLOAT Value;

    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static
UINT
RxWarpFloatAsBits(
    _In_ FLOAT Value)
{
    UINT Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

/* Round to nearest, ties to even, as the rasterizer snaps vertices */
static
INT
RxWarpRoundEven(
    _In_ FLOAT Value)
{
    return (INT)nearbyintf(Value);
}

/* The standard sample positions for each count, from the pixel center in 1/256 pixel; one sample sits at the center */
static
VOID
RxWarpSamplePattern(
    _In_ UINT Count,
    _Out_writes_(16) INT (*Offset)[2])
{
    static const INT8 Two[2][2] = { { 4, 4 }, { -4, -4 } };
    static const INT8 Four[4][2] = { { -2, -6 }, { 6, -2 }, { -6, 2 }, { 2, 6 } };
    static const INT8 Eight[8][2] = { { 1, -3 }, { -1, 3 }, { 5, 1 }, { -3, -5 }, { -5, 5 }, { -7, -1 }, { 3, 7 }, { 7, -7 } };
    static const INT8 Sixteen[16][2] =
    {
        { 1, 1 }, { -1, -3 }, { -3, 2 }, { 4, -1 }, { -5, -2 }, { 2, 5 }, { 5, 3 }, { 3, -5 },
        { -2, 6 }, { 0, -7 }, { -4, -6 }, { -6, 4 }, { -8, 0 }, { 7, -4 }, { 6, 7 }, { -7, -8 },
    };
    const INT8 (*Table)[2] = nullptr;

    ZeroMemory(Offset, sizeof(INT) * 2 * 16);
    switch (Count)
    {
        case 2:
            Table = Two;
            break;
        case 4:
            Table = Four;
            break;
        case 8:
            Table = Eight;
            break;
        case 16:
            Table = Sixteen;
            break;
        default:
            return;
    }
    for (UINT Index = 0; Index < Count; Index++)
    {
        Offset[Index][0] = Table[Index][0] * 16;
        Offset[Index][1] = Table[Index][1] * 16;
    }
}

/* Samples alpha to coverage keeps: sample i needs alpha above (dither + 2i + 1) / (2 * count), dithered over an 8x8 tile */
static
UINT
RxWarpAlphaCoverage(
    _In_ FLOAT Alpha,
    _In_ INT X,
    _In_ INT Y,
    _In_ UINT Count)
{
    static const FLOAT Dither[64] =
    {
#include "dithertable.inl"
    };
    FLOAT Step = 1.0f / ((FLOAT)Count + (FLOAT)Count);
    FLOAT Offset = Dither[(Y & 7) * 8 + (X & 7)];
    UINT Mask = 0;

    for (UINT Index = 0; Index < Count && Index < 32; Index++)
    {
        if (Alpha > (Offset + (FLOAT)(2 * Index + 1)) * Step)
            Mask |= 1u << Index;
    }
    return Mask;
}

class RxWarpDraw
{
public:
    RxWarpDraw(
        _In_ RxWarpDevice *Owner)
        : Device(Owner), Pipeline(&Owner->Pipeline)
    {
    }

    VOID
    Run(
        _In_ UINT VertexCount,
        _In_ UINT StartVertex,
        _In_ BOOLEAN Indexed,
        _In_ INT BaseVertex,
        _In_ UINT InstanceCount,
        _In_ UINT StartInstance);

private:
    BOOLEAN
    Prepare(VOID);

    VOID
    BindStage(
        _In_ RX_WARP_STAGE Stage,
        _Out_ RX_WARP_STAGE_BINDINGS *Bindings);

    UINT
    FetchIndex(
        _In_ UINT Position);

    VOID
    ShadeVertices(
        _In_reads_(Count) const UINT *Indices,
        _In_ UINT Count,
        _In_ INT BaseVertex,
        _In_ UINT Instance,
        _In_ UINT StartInstance);

    VOID
    FetchVertex(
        _In_ UINT Index,
        _In_ UINT Instance,
        _In_ UINT StartInstance,
        _In_ UINT Lane);

    VOID
    ClipPrimitive(
        _In_reads_(Count) const RX_WARP_VERTEX *const *Corners,
        _In_ UINT Count);

    VOID
    ClipDistances(
        _In_ const RX_WARP_VERTEX *Vertex,
        _Out_writes_(RxWarpClipPlanes) FLOAT *Distance);

    BOOLEAN
    SelectViewport(
        _In_ UINT Index);

    VOID
    Assemble(
        _In_reads_(Count) const RX_WARP_VERTEX *const *Corners,
        _In_ UINT Count,
        _In_reads_(FullCount) const RX_WARP_VERTEX *const *Full,
        _In_ UINT FullCount);

    VOID
    RunGeometry(
        _In_reads_(Count) const RX_WARP_VERTEX *const *Full,
        _In_ UINT Count);

    VOID
    RunPatch(
        _In_reads_(Count) const RX_WARP_VERTEX *const *Points,
        _In_ UINT Count);

    VOID
    FlushStrip(
        _In_ UINT Stream);

    VOID
    OutputPrimitive(
        _In_ UINT Stream,
        _In_reads_(Count) const RX_WARP_VERTEX *const *Corners,
        _In_ UINT Count);

    VOID
    StreamOut(
        _In_ UINT Stream,
        _In_reads_(Count) const RX_WARP_VERTEX *const *Corners,
        _In_ UINT Count);

    static
    VOID
    GeometryEmit(
        _In_ VOID *Context,
        _In_ RxWarpInvocation *Source,
        _In_ UINT Stream,
        _In_ UINT Lane,
        _In_ BOOLEAN Emit,
        _In_ BOOLEAN Cut);

    VOID
    ToScreen(
        _In_ const RX_WARP_VERTEX *Vertex,
        _Out_ RX_WARP_SCREEN_VERTEX *Screen);

    VOID
    Rasterize(
        _In_ const RX_WARP_VERTEX *A,
        _In_ const RX_WARP_VERTEX *B,
        _In_ const RX_WARP_VERTEX *C);

    VOID
    RasterizeLine(
        _In_ const RX_WARP_VERTEX *A,
        _In_ const RX_WARP_VERTEX *B);

    VOID
    RasterizePoint(
        _In_ const RX_WARP_VERTEX *A);

    VOID
    BiasDepth(
        _In_ FLOAT LargestDepth);

    template <typename Test>
    VOID
    Cover(
        _In_ INT MinX,
        _In_ INT MinY,
        _In_ INT MaxX,
        _In_ INT MaxY,
        _In_ BOOLEAN PerSample,
        _In_ Test Inside);

    VOID
    ShadeQuad(
        _In_ INT X,
        _In_ INT Y,
        _In_ UINT Covered,
        _In_reads_(RxWarpLanes) const UINT *Samples);

    VOID
    ShadePass(
        _In_ INT X,
        _In_ INT Y,
        _In_ UINT Covered,
        _In_reads_(RxWarpLanes) const UINT *Samples,
        _In_ UINT Pass);

    VOID
    Interpolate(
        _In_ UINT Register,
        _In_ FLOAT X,
        _In_ FLOAT Y,
        _Out_writes_(4) UINT *Value,
        _In_ BOOLEAN CenterW = FALSE);

    static
    BOOLEAN
    PreciseInput(
        _In_ VOID *Context,
        _In_ UINT Lane,
        _In_ UINT Register,
        _Out_writes_(4) UINT *Value);

    static
    VOID
    EvaluateInput(
        _In_ VOID *Context,
        _In_ UINT Lane,
        _In_ UINT Register,
        _In_ RX_WARP_EVALUATE_KIND Kind,
        _In_reads_(2) const INT *Parameter,
        _Out_writes_(4) UINT *Value);

    VOID
    WritePixel(
        _In_ INT X,
        _In_ INT Y,
        _In_ UINT Lane,
        _In_ FLOAT Depth);

    BOOLEAN
    DepthStencilTest(
        _In_ INT X,
        _In_ INT Y,
        _In_ FLOAT Depth);

    VOID
    Blend(
        _In_ UINT Target,
        _In_ INT X,
        _In_ INT Y,
        _In_reads_(4) const UINT *Color,
        _In_reads_(4) const UINT *Color1,
        _In_reads_opt_(4) const INT *Shadow);

    RxWarpDevice *Device;
    RX_WARP_PIPELINE *Pipeline;
    RxWarpShader *VertexShader = nullptr;
    RxWarpShader *PixelShader = nullptr;
    RxWarpShader *GeometryShader = nullptr;
    RxWarpShader *StreamShader = nullptr;
    RxWarpShader *HullShader = nullptr;
    RxWarpShader *DomainShader = nullptr;
    RxWarpShader *LastStage = nullptr;
    RX_WARP_STAGE_BINDINGS VertexBindings = {};
    RX_WARP_STAGE_BINDINGS HullBindings = {};
    RX_WARP_STAGE_BINDINGS DomainBindings = {};
    RxWarpTessellator Tessellator;
    std::vector<RX_WARP_VERTEX> HullPoints;
    std::vector<RX_WARP_VERTEX> DomainPoints;
    UINT PatchConstants[RxWarpMaxRegisters][4] = {};
    RX_WARP_STAGE_BINDINGS PixelBindings = {};
    RX_WARP_STAGE_BINDINGS GeometryBindings = {};
    RxWarpInvocation GeometryInvocation;
    std::vector<RX_WARP_VERTEX> Strips[4];
    UINT Emitted = 0;
    BOOLEAN CanRasterize = FALSE;
    UINT RasterizedStream = 0;
    UINT TargetWidth = 0;
    UINT TargetHeight = 0;
    UINT ClipSlots[8] = {};             /* register * 4 + component of each clip distance */
    UINT ClipCount = 0;
    UINT CullSlots[8] = {};
    UINT CullCount = 0;
    UINT ViewportRegister = UINT_MAX;
    UINT SliceRegister = UINT_MAX;
    UINT TargetSlice = 0;
    UINT SampleCount = 1;
    INT SampleOffset[16][2] = {};       /* from the pixel center, in 1/256 pixel */
    UINT Sample = 0;                    /* the sample being tested and written */
    INT QuadX = 0;
    INT QuadY = 0;
    UINT EvaluatingLane = 0;
    INT SortedOffset[16][2] = {};       /* the pattern ordered top to bottom, as the reference stores samples */
    FLOAT CentroidAt[RxWarpLanes][2] = {};
    BOOLEAN Triangle = FALSE;           /* the primitive being shaded has three distinct corners */
    D3DWDDM2_0DDI_RASTERIZER_DESC Raster = {};
    D3D10_DDI_DEPTH_STENCIL_DESC DepthState = {};
    const D3D11_1_DDI_BLEND_DESC *BlendState = nullptr;
    FLOAT Scale[3] = {};
    FLOAT Bias[3] = {};
    FLOAT MinDepth = 0.0f;
    FLOAT MaxDepth = 1.0f;
    INT Left = 0;
    INT Top = 0;
    INT Right = 0;
    INT Bottom = 0;
    UINT PositionRegister = 0;
    UINT Primitive = 0;
    std::vector<RX_WARP_VERTEX> Vertices;
    RxWarpInvocation Invocation;

    /* Per triangle state used while shading quads */
    RX_WARP_SCREEN_VERTEX Corner[3] = {};
    const RX_WARP_VERTEX *Provoking = nullptr;
    RX_WARP_PLANE Planes[RxWarpMaxRegisters][4] = {};
    RX_WARP_PLANE DepthPlane = {};
    RX_WARP_PLANE RhwPlane = {};
    BOOLEAN FrontFacing = TRUE;
    BOOLEAN PointSprite = FALSE;
};

VOID
RxWarpDraw::BindStage(
    _In_ RX_WARP_STAGE Stage,
    _Out_ RX_WARP_STAGE_BINDINGS *Bindings)
{
    RxWarpBindStage(&Pipeline->Stages[Stage], Bindings);
}

/* Gathers state for the draw; FALSE when there is nothing to run at all */
BOOLEAN
RxWarpDraw::Prepare(VOID)
{
    UINT Width = UINT_MAX;
    UINT Height = UINT_MAX;

    VertexShader = Pipeline->Stages[RxWarpStageVertex].Shader;
    PixelShader = Pipeline->Stages[RxWarpStagePixel].Shader;
    GeometryShader = Pipeline->Stages[RxWarpStageGeometry].Shader;
    HullShader = Pipeline->Stages[RxWarpStageHull].Shader;
    DomainShader = Pipeline->Stages[RxWarpStageDomain].Shader;
    if (VertexShader == nullptr)
        return FALSE;
    if (HullShader == nullptr || DomainShader == nullptr)
        HullShader = DomainShader = nullptr;

    /* A stream output shader without code only captures what the vertex shader wrote */
    StreamShader = GeometryShader;
    if (GeometryShader != nullptr && GeometryShader->PassThrough)
        GeometryShader = nullptr;
    LastStage = (GeometryShader != nullptr) ? GeometryShader : (DomainShader != nullptr) ? DomainShader : VertexShader;
    RasterizedStream = (StreamShader != nullptr) ? StreamShader->RasterizedStream : 0;
    PositionRegister = LastStage->PositionOutput;

    ViewportRegister = UINT_MAX;
    SliceRegister = UINT_MAX;
    ClipCount = CullCount = 0;
    for (UINT Register = 0; Register < LastStage->OutputCount; Register++)
    {
        for (UINT Component = 0; Component < 4; Component++)
        {
            if (((LastStage->ClipMask[Register] >> Component) & 1) && ClipCount < 8)
                ClipSlots[ClipCount++] = Register * 4 + Component;
            if (((LastStage->CullMask[Register] >> Component) & 1) && CullCount < 8)
                CullSlots[CullCount++] = Register * 4 + Component;
        }
        if (LastStage->OutputName[Register] == D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX)
            ViewportRegister = Register;
        if (LastStage->OutputName[Register] == D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX)
            SliceRegister = Register;
    }

    BindStage(RxWarpStageVertex, &VertexBindings);
    BindStage(RxWarpStagePixel, &PixelBindings);
    BindStage(RxWarpStageGeometry, &GeometryBindings);
    BindStage(RxWarpStageHull, &HullBindings);
    BindStage(RxWarpStageDomain, &DomainBindings);

    if (Pipeline->Rasterizer != nullptr)
    {
        Raster = Pipeline->Rasterizer->Desc;
    }
    else
    {
        Raster = {};
        Raster.FillMode = D3D10_DDI_FILL_SOLID;
        Raster.CullMode = D3D10_DDI_CULL_BACK;
        Raster.DepthClipEnable = TRUE;
    }
    if (Pipeline->DepthStencil != nullptr)
    {
        DepthState = Pipeline->DepthStencil->Desc;
    }
    else
    {
        DepthState = {};
        DepthState.DepthEnable = TRUE;
        DepthState.DepthWriteMask = D3D10_DDI_DEPTH_WRITE_MASK_ALL;
        DepthState.DepthFunc = D3D10_DDI_COMPARISON_LESS;
    }
    BlendState = (Pipeline->Blend != nullptr) ? &Pipeline->Blend->Desc : &RxWarpDefaultBlend;

    for (UINT Index = 0; Index < Pipeline->RenderTargetCount; Index++)
    {
        RxWarpView *View = Pipeline->RenderTargets[Index];
        RX_WARP_SUBRESOURCE *Sub;

        if (View == nullptr || View->Resource == nullptr)
            continue;
        Sub = View->Resource->Subresource(View->FirstSlice * View->Resource->MipLevels + View->MipSlice);
        if (Sub == nullptr)
            continue;
        Width = (Sub->Width < Width) ? Sub->Width : Width;
        Height = (Sub->Height < Height) ? Sub->Height : Height;
    }
    if (Pipeline->DepthStencilView != nullptr && Pipeline->DepthStencilView->Resource != nullptr)
    {
        RxWarpView *View = Pipeline->DepthStencilView;
        RX_WARP_SUBRESOURCE *Sub = View->Resource->Subresource(View->FirstSlice * View->Resource->MipLevels + View->MipSlice);

        if (Sub != nullptr)
        {
            Width = (Sub->Width < Width) ? Sub->Width : Width;
            Height = (Sub->Height < Height) ? Sub->Height : Height;
        }
    }
    if (Width == UINT_MAX)
        Width = Height = 16384;

    /* The targets' sample count decides the raster pattern */
    SampleCount = 1;
    if (Pipeline->RenderTargetCount != 0 && Pipeline->RenderTargets[0] != nullptr && Pipeline->RenderTargets[0]->Resource != nullptr)
        SampleCount = Pipeline->RenderTargets[0]->Resource->SampleCount;
    else if (Pipeline->DepthStencilView != nullptr && Pipeline->DepthStencilView->Resource != nullptr)
        SampleCount = Pipeline->DepthStencilView->Resource->SampleCount;
    RxWarpSamplePattern(SampleCount, SampleOffset);
    memcpy(SortedOffset, SampleOffset, sizeof(SortedOffset));
    for (UINT Index = 1; Index < SampleCount && Index < 16; Index++)
    {
        INT Moving[2] = { SortedOffset[Index][0], SortedOffset[Index][1] };
        UINT Place = Index;

        while (Place > 0 && SortedOffset[Place - 1][1] > Moving[1])
        {
            SortedOffset[Place][0] = SortedOffset[Place - 1][0];
            SortedOffset[Place][1] = SortedOffset[Place - 1][1];
            Place--;
        }
        SortedOffset[Place][0] = Moving[0];
        SortedOffset[Place][1] = Moving[1];
    }
    TargetWidth = Width;
    TargetHeight = Height;

    CanRasterize = (RasterizedStream != UINT_MAX && PositionRegister != UINT_MAX && Pipeline->ViewportCount != 0) &&
                   SelectViewport(0);
    return TRUE;
}

/* Viewport and scissor for one primitive; FALSE when they leave nothing to draw */
BOOLEAN
RxWarpDraw::SelectViewport(
    _In_ UINT Index)
{
    const D3D10_DDI_VIEWPORT *Viewport;

    if (Index >= Pipeline->ViewportCount)
        Index = 0;
    Viewport = &Pipeline->Viewports[Index];
    Scale[0] = Viewport->Width * 0.5f;
    Bias[0] = Viewport->Width * 0.5f + Viewport->TopLeftX;
    Scale[1] = -Viewport->Height * 0.5f;
    Bias[1] = Viewport->Height * 0.5f + Viewport->TopLeftY;
    Scale[2] = Viewport->MaxDepth - Viewport->MinDepth;
    Bias[2] = Viewport->MinDepth;
    MinDepth = (Viewport->MinDepth < Viewport->MaxDepth) ? Viewport->MinDepth : Viewport->MaxDepth;
    MaxDepth = (Viewport->MinDepth < Viewport->MaxDepth) ? Viewport->MaxDepth : Viewport->MinDepth;

    /* Pixels never leave the viewport, the bound targets or the scissor */
    Left = (INT)floorf(Viewport->TopLeftX);
    Top = (INT)floorf(Viewport->TopLeftY);
    Right = (INT)ceilf(Viewport->TopLeftX + Viewport->Width);
    Bottom = (INT)ceilf(Viewport->TopLeftY + Viewport->Height);
    Left = (Left > 0) ? Left : 0;
    Top = (Top > 0) ? Top : 0;
    Right = (Right < (INT)TargetWidth) ? Right : (INT)TargetWidth;
    Bottom = (Bottom < (INT)TargetHeight) ? Bottom : (INT)TargetHeight;
    if (Raster.ScissorEnable)
    {
        const D3D10_DDI_RECT *Scissor;

        if (Index >= Pipeline->ScissorCount)
            return FALSE;
        Scissor = &Pipeline->Scissors[Index];
        Left = (Scissor->left > Left) ? Scissor->left : Left;
        Top = (Scissor->top > Top) ? Scissor->top : Top;
        Right = (Scissor->right < Right) ? Scissor->right : Right;
        Bottom = (Scissor->bottom < Bottom) ? Scissor->bottom : Bottom;
    }
    return (Left < Right && Top < Bottom);
}

UINT
RxWarpDraw::FetchIndex(
    _In_ UINT Position)
{
    RxWarpResource *Buffer = Pipeline->IndexBuffer;
    UINT Size = (Pipeline->IndexFormat == DXGI_FORMAT_R16_UINT) ? 2 : 4;
    SIZE_T Offset = Pipeline->IndexOffset + (SIZE_T)Position * Size;
    UINT Value = 0;

    if (Buffer == nullptr || Buffer->Memory == nullptr || Offset + Size > Buffer->MemorySize)
        return 0;
    memcpy(&Value, Buffer->Memory + Offset, Size);
    return Value;
}

VOID
RxWarpDraw::FetchVertex(
    _In_ UINT Index,
    _In_ UINT Instance,
    _In_ UINT StartInstance,
    _In_ UINT Lane)
{
    RxWarpElementLayout *Layout = Pipeline->Layout;

    for (UINT Register = 0; Register < RxWarpMaxRegisters; Register++)
    {
        UINT *Input = Invocation.Input(Register, Lane);

        Input[0] = Input[1] = Input[2] = Input[3] = 0;
        if (VertexShader->InputName[Register] == D3D10_SB_NAME_VERTEX_ID)
            Input[0] = Input[1] = Input[2] = Input[3] = Index;
        else if (VertexShader->InputName[Register] == D3D10_SB_NAME_INSTANCE_ID)
            Input[0] = Input[1] = Input[2] = Input[3] = Instance;
    }

    if (Layout == nullptr)
        return;

    for (const D3D10DDIARG_INPUT_ELEMENT_DESC &Element : Layout->Elements)
    {
        RX_WARP_BUFFER_BINDING *Binding;
        const RX_WARP_FORMAT_INFO *Info;
        BYTE Raw[16] = {};
        UINT *Input;
        UINT Item;
        SIZE_T Offset;

        if (Element.InputSlot >= 32 || Element.InputRegister >= RxWarpMaxRegisters)
            continue;
        Binding = &Pipeline->VertexBuffers[Element.InputSlot];
        Info = RxWarpFormatInfo(Element.Format);
        Input = Invocation.Input(Element.InputRegister, Lane);
        if (Binding->Resource == nullptr || Binding->Resource->Memory == nullptr || Info == nullptr)
        {
            UINT Defaults[4];

            RxWarpDecodeTexel(Element.Format, Raw, Defaults);
            memcpy(Input, Defaults, sizeof(Defaults));
            continue;
        }

        if (Element.InputSlotClass == D3D10_DDI_INPUT_PER_INSTANCE_DATA)
            Item = (Element.InstanceDataStepRate == 0) ? StartInstance : StartInstance + Instance / Element.InstanceDataStepRate;
        else
            Item = Index;

        Offset = (SIZE_T)Binding->Offset + (SIZE_T)Item * Binding->Stride + Element.AlignedByteOffset;
        if (Offset + Info->Bytes <= Binding->Resource->MemorySize)
            memcpy(Raw, Binding->Resource->Memory + Offset, (Info->Bytes <= 16) ? Info->Bytes : 16);
        RxWarpDecodeTexel(Element.Format, Raw, Input);
    }
}

VOID
RxWarpDraw::ShadeVertices(
    _In_reads_(Count) const UINT *Indices,
    _In_ UINT Count,
    _In_ INT BaseVertex,
    _In_ UINT Instance,
    _In_ UINT StartInstance)
{
    Vertices.resize(Count);

    for (UINT First = 0; First < Count; First += RxWarpLanes)
    {
        UINT Lanes = (Count - First < RxWarpLanes) ? Count - First : RxWarpLanes;

        Invocation.Prepare(VertexShader);
        Invocation.Live = (1u << Lanes) - 1;
        Invocation.Helper = 0;
        for (UINT Lane = 0; Lane < Lanes; Lane++)
            FetchVertex((UINT)((INT)Indices[First + Lane] + BaseVertex), Instance, StartInstance, Lane);
        Invocation.Run(VertexShader, &VertexBindings);
        Device->Counters[RxWarpCountVsInvocations] += Lanes;

        for (UINT Lane = 0; Lane < Lanes; Lane++)
        {
            for (UINT Register = 0; Register < RxWarpMaxRegisters; Register++)
                memcpy(Vertices[First + Lane].Registers[Register], Invocation.Output(Register, Lane), 16);
        }
    }
}

VOID
RxWarpDraw::ToScreen(
    _In_ const RX_WARP_VERTEX *Vertex,
    _Out_ RX_WARP_SCREEN_VERTEX *Screen)
{
    const UINT *Position = Vertex->Registers[PositionRegister];
    FLOAT X = RxWarpBitsAsFloat(Position[0]);
    FLOAT Y = RxWarpBitsAsFloat(Position[1]);
    FLOAT Z = RxWarpBitsAsFloat(Position[2]);
    FLOAT W = RxWarpBitsAsFloat(Position[3]);
    FLOAT Rhw;

    /* Clipping leaves points on the planes; keep float error from pushing them past */
    if (!(W > FLT_MIN))
        W = FLT_MIN;
    X = (X <= -W) ? -W : X;
    X = (X >= W) ? W : X;
    Y = (Y <= -W) ? -W : Y;
    Y = (Y >= W) ? W : Y;
    if (Raster.DepthClipEnable)
    {
        Z = (Z > 0.0f) ? Z : 0.0f;
        Z = (Z >= W) ? W : Z;
    }

    Rhw = 1.0f / W;
    Screen->X = RxWarpRoundEven((Rhw * X * Scale[0] + Bias[0]) * 256.0f);
    Screen->Y = RxWarpRoundEven((Rhw * Y * Scale[1] + Bias[1]) * 256.0f);
    Screen->Z = Rhw * Z * Scale[2] + Bias[2];
    Screen->Rhw = Rhw;
    Screen->Source = Vertex;
}

/* Distance to each clip plane, the view volume first and then the shader's clip distances; negative is outside */
VOID
RxWarpDraw::ClipDistances(
    _In_ const RX_WARP_VERTEX *Vertex,
    _Out_writes_(RxWarpClipPlanes) FLOAT *Distance)
{
    const UINT *Position = Vertex->Registers[PositionRegister];
    FLOAT X = RxWarpBitsAsFloat(Position[0]);
    FLOAT Y = RxWarpBitsAsFloat(Position[1]);
    FLOAT Z = RxWarpBitsAsFloat(Position[2]);
    FLOAT W = RxWarpBitsAsFloat(Position[3]);

    Distance[0] = Raster.DepthClipEnable ? Z : 0.0f;
    Distance[1] = Raster.DepthClipEnable ? W - Z : 0.0f;
    Distance[2] = W + X;
    Distance[3] = W - X;
    Distance[4] = W - Y;
    Distance[5] = W + Y;
    for (UINT Index = 0; Index < ClipCount; Index++)
        Distance[6 + Index] = RxWarpBitsAsFloat(Vertex->Registers[ClipSlots[Index] / 4][ClipSlots[Index] % 4]);
}

/* Interpolates every register from Inner toward Outer, stopping on the plane */
static
VOID
RxWarpClipPoint(
    _In_ const RX_WARP_VERTEX &Inner,
    _In_ const RX_WARP_VERTEX &Outer,
    _In_ FLOAT InnerDistance,
    _In_ FLOAT OuterDistance,
    _Out_ RX_WARP_VERTEX *Point)
{
    FLOAT Span = InnerDistance - OuterDistance;

    for (UINT Register = 0; Register < RxWarpMaxRegisters; Register++)
    {
        for (UINT Component = 0; Component < 4; Component++)
        {
            FLOAT P = RxWarpBitsAsFloat(Inner.Registers[Register][Component]);
            FLOAT Q = RxWarpBitsAsFloat(Outer.Registers[Register][Component]);

            Point->Registers[Register][Component] = RxWarpFloatAsBits((Q - P) * InnerDistance / Span + P);
        }
    }
}

VOID
RxWarpDraw::ClipPrimitive(
    _In_reads_(Count) const RX_WARP_VERTEX *const *Corners,
    _In_ UINT Count)
{
    FLOAT Distance[RxWarpClipPlanes];
    UINT PlaneCount = 6 + ClipCount;
    UINT Outside[3] = {};
    UINT Any;
    std::vector<RX_WARP_VERTEX> Polygon;
    std::vector<RX_WARP_VERTEX> Next;

    Device->Counters[RxWarpCountClipInvocations]++;

    /* The leading vertex picks the viewport and the render target slice */
    TargetSlice = (SliceRegister != UINT_MAX) ? Corners[0]->Registers[SliceRegister][0] : 0;
    if (ViewportRegister != UINT_MAX && !SelectViewport(Corners[0]->Registers[ViewportRegister][0]))
        return;

    /* Culled when every vertex is behind the same cull distance */
    for (UINT Slot = 0; Slot < CullCount; Slot++)
    {
        UINT Behind = 0;

        for (UINT Index = 0; Index < Count; Index++)
        {
            if (!(RxWarpBitsAsFloat(Corners[Index]->Registers[CullSlots[Slot] / 4][CullSlots[Slot] % 4]) >= 0.0f))
                Behind++;
        }
        if (Behind == Count)
            return;
    }

    for (UINT Index = 0; Index < Count; Index++)
    {
        ClipDistances(Corners[Index], Distance);
        for (UINT Plane = 0; Plane < PlaneCount; Plane++)
        {
            if (Distance[Plane] < 0.0f)
                Outside[Index] |= 1u << Plane;
        }
    }

    Provoking = Corners[0];
    Any = Outside[0] | ((Count > 1) ? Outside[1] : 0) | ((Count > 2) ? Outside[2] : 0);
    if ((Outside[0] & ((Count > 1) ? Outside[1] : ~0u) & ((Count > 2) ? Outside[2] : ~0u)) != 0)
        return;

    if (Any == 0)
    {
        Device->Counters[RxWarpCountClipPrimitives]++;
        if (Count == 1)
            RasterizePoint(Corners[0]);
        else if (Count == 2)
            RasterizeLine(Corners[0], Corners[1]);
        else
            Rasterize(Corners[0], Corners[1], Corners[2]);
        return;
    }

    /* A point is all or nothing */
    if (Count == 1)
        return;

    for (UINT Index = 0; Index < Count; Index++)
        Polygon.push_back(*Corners[Index]);

    for (UINT Plane = 0; Plane < PlaneCount && Polygon.size() >= Count; Plane++)
    {
        SIZE_T Edges = (Count == 2) ? 1 : Polygon.size();

        if (!(Any & (1u << Plane)))
            continue;

        Next.clear();
        for (SIZE_T Index = 0; Index < Edges; Index++)
        {
            const RX_WARP_VERTEX &From = Polygon[Index];
            const RX_WARP_VERTEX &To = Polygon[(Index + 1) % Polygon.size()];
            FLOAT FromDistance[RxWarpClipPlanes];
            FLOAT ToDistance[RxWarpClipPlanes];
            BOOLEAN FromIn;
            BOOLEAN ToIn;
            RX_WARP_VERTEX Point;

            ClipDistances(&From, FromDistance);
            ClipDistances(&To, ToDistance);
            FromIn = !(FromDistance[Plane] < 0.0f);
            ToIn = !(ToDistance[Plane] < 0.0f);

            if (FromIn)
                Next.push_back(From);
            if (FromIn != ToIn)
            {
                /* The new point always interpolates from the inside vertex */
                if (FromIn)
                    RxWarpClipPoint(From, To, FromDistance[Plane], ToDistance[Plane], &Point);
                else
                    RxWarpClipPoint(To, From, ToDistance[Plane], FromDistance[Plane], &Point);
                Next.push_back(Point);
            }
            if (Count == 2 && ToIn)
                Next.push_back(To);
        }
        Polygon.swap(Next);
    }

    if (Count == 2)
    {
        if (Polygon.size() == 2)
        {
            Device->Counters[RxWarpCountClipPrimitives]++;
            RasterizeLine(&Polygon[0], &Polygon[1]);
        }
        return;
    }

    /* Flat attributes still come from the original first vertex */
    for (SIZE_T Index = 1; Index + 1 < Polygon.size(); Index++)
    {
        Device->Counters[RxWarpCountClipPrimitives]++;
        Rasterize(&Polygon[0], &Polygon[Index], &Polygon[Index + 1]);
    }
}

/* The same plane setup the reference uses: offsets from the first vertex, divided by the area */
static
RX_WARP_PLANE
RxWarpMakePlane(
    _In_reads_(3) const RX_WARP_SCREEN_VERTEX *Corner,
    _In_ FLOAT InverseArea,
    _In_ FLOAT V0,
    _In_ FLOAT V1,
    _In_ FLOAT V2)
{
    FLOAT X0 = (FLOAT)Corner[0].X * (1.0f / 256.0f);
    FLOAT Y0 = (FLOAT)Corner[0].Y * (1.0f / 256.0f);
    FLOAT Dx10 = (FLOAT)(Corner[1].X - Corner[0].X) * (1.0f / 256.0f);
    FLOAT Dx02 = (FLOAT)(Corner[0].X - Corner[2].X) * (1.0f / 256.0f);
    FLOAT Dy01 = (FLOAT)(Corner[0].Y - Corner[1].Y) * (1.0f / 256.0f);
    FLOAT Dy20 = (FLOAT)(Corner[2].Y - Corner[0].Y) * (1.0f / 256.0f);
    FLOAT D1 = V1 - V0;
    FLOAT D2 = V2 - V0;
    RX_WARP_PLANE Plane;

    Plane.A = (Dy01 * D2 + Dy20 * D1) * InverseArea;
    Plane.B = (Dx10 * D2 + Dx02 * D1) * InverseArea;
    Plane.C = (V0 - X0 * Plane.A) - Y0 * Plane.B;
    return Plane;
}

static
FLOAT
RxWarpEvaluate(
    _In_ const RX_WARP_PLANE &Plane,
    _In_ FLOAT X,
    _In_ FLOAT Y)
{
    FLOAT Partial = Plane.C + Plane.B * Y;

    /* Constant, then y, then x: the order the reference sums in */
    return Partial + Plane.A * X;
}

/* Edge test in 24.8: inside is non negative, and only top and left edges keep their exact zeros */
typedef struct _RX_WARP_EDGE
{
    INT64 Dx;
    INT64 Dy;
    INT64 X0;
    INT64 Y0;
    INT64 Bias;
} RX_WARP_EDGE;

/* The magnitude of whichever value is larger, compared as bits */
static
FLOAT
RxWarpLargerMagnitude(
    _In_ FLOAT First,
    _In_ FLOAT Second)
{
    UINT A = RxWarpFloatAsBits(First) & 0x7FFFFFFF;
    UINT B = RxWarpFloatAsBits(Second) & 0x7FFFFFFF;

    return RxWarpBitsAsFloat((A > B) ? A : B);
}

/* Moves the depth plane by the rasterizer state's bias; one unit depends on the depth format */
VOID
RxWarpDraw::BiasDepth(
    _In_ FLOAT LargestDepth)
{
    DXGI_FORMAT Format = (Pipeline->DepthStencilView != nullptr) ? Pipeline->DepthStencilView->Format : DXGI_FORMAT_UNKNOWN;
    FLOAT Constant = (FLOAT)Raster.DepthBias;
    FLOAT Slope = Raster.SlopeScaledDepthBias;
    FLOAT Clamp = Raster.DepthBiasClamp;
    UINT Unit;
    FLOAT Shift;

    if (Raster.DepthBias == 0 && Slope == 0.0f)
        return;

    if (Format == DXGI_FORMAT_D16_UNORM)
    {
        Unit = 0x37800000;
    }
    else if (Format == DXGI_FORMAT_D24_UNORM_S8_UINT)
    {
        /* The reference doubles the unit for a bias of exactly one */
        Unit = (Constant == 1.0f || Constant == -1.0f) ? 0x34000000 : 0x33800000;
    }
    else
    {
        UINT Exponent = (RxWarpFloatAsBits(LargestDepth) >> 23) & 0xFF;

        Unit = (Exponent > 23) ? (Exponent - 23) << 23 : 0;
    }

    if (Slope != 0.0f)
        Slope = RxWarpLargerMagnitude(DepthPlane.A, DepthPlane.B) * Slope;
    Shift = RxWarpBitsAsFloat(Unit) * Constant;
    Shift = Shift + Slope;
    if (Clamp > 0.0f && Clamp < Shift)
        Shift = Clamp;
    else if (Clamp < 0.0f && Clamp > Shift)
        Shift = Clamp;
    DepthPlane.C = DepthPlane.C + Shift;
}

static
RX_WARP_EDGE
RxWarpMakeEdge(
    _In_ const RX_WARP_SCREEN_VERTEX &From,
    _In_ const RX_WARP_SCREEN_VERTEX &To)
{
    RX_WARP_EDGE Edge;

    Edge.Dx = (INT64)To.X - From.X;
    Edge.Dy = (INT64)To.Y - From.Y;
    Edge.X0 = From.X;
    Edge.Y0 = From.Y;
    Edge.Bias = (Edge.Dy < 0 || (Edge.Dy == 0 && Edge.Dx > 0)) ? 0 : -1;
    return Edge;
}

static
BOOLEAN
RxWarpInsideEdge(
    _In_ const RX_WARP_EDGE &Edge,
    _In_ INT64 X,
    _In_ INT64 Y)
{
    return (Edge.Dx * (Y - Edge.Y0) - Edge.Dy * (X - Edge.X0) + Edge.Bias) >= 0;
}

/* Walks the 2x2 quads of a pixel rectangle and shades the ones Inside covers */
template <typename Test>
VOID
RxWarpDraw::Cover(
    _In_ INT MinX,
    _In_ INT MinY,
    _In_ INT MaxX,
    _In_ INT MaxY,
    _In_ BOOLEAN PerSample,
    _In_ Test Inside)
{
    UINT AllSamples = (SampleCount >= 32) ? UINT_MAX : (1u << SampleCount) - 1;

    MinX = (MinX > Left) ? MinX : Left;
    MinY = (MinY > Top) ? MinY : Top;
    MaxX = (MaxX < Right) ? MaxX : Right;
    MaxY = (MaxY < Bottom) ? MaxY : Bottom;
    if (MinX >= MaxX || MinY >= MaxY)
        return;
    if (SampleCount <= 1)
        PerSample = FALSE;

    for (INT Y = MinY & ~1; Y < MaxY; Y += 2)
    {
        for (INT X = MinX & ~1; X < MaxX; X += 2)
        {
            UINT Covered = 0;
            UINT Samples[RxWarpLanes] = {};

            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                INT Px = X + (INT)(Lane & 1);
                INT Py = Y + (INT)(Lane >> 1);
                INT64 Cx = ((INT64)Px << 8) + 128;
                INT64 Cy = ((INT64)Py << 8) + 128;

                if (Px < MinX || Px >= MaxX || Py < MinY || Py >= MaxY)
                    continue;

                if (PerSample)
                {
                    for (UINT Index = 0; Index < SampleCount; Index++)
                    {
                        if (Inside(Cx + SampleOffset[Index][0], Cy + SampleOffset[Index][1]))
                            Samples[Lane] |= 1u << Index;
                    }
                }
                else if (Inside(Cx, Cy))
                {
                    /* Aliased coverage stands for every sample */
                    Samples[Lane] = AllSamples;
                }
                if (Samples[Lane] != 0)
                    Covered |= 1u << Lane;
            }
            if (Covered != 0)
                ShadeQuad(X, Y, Covered, Samples);
        }
    }
}

VOID
RxWarpDraw::Rasterize(
    _In_ const RX_WARP_VERTEX *A,
    _In_ const RX_WARP_VERTEX *B,
    _In_ const RX_WARP_VERTEX *C)
{
    RX_WARP_SCREEN_VERTEX Ordered[3];
    RX_WARP_EDGE Edges[3];
    INT64 Area;
    FLOAT InverseArea;
    BOOLEAN Clockwise;
    INT MinX;
    INT MinY;
    INT MaxX;
    INT MaxY;

    ToScreen(A, &Corner[0]);
    ToScreen(B, &Corner[1]);
    ToScreen(C, &Corner[2]);

    Area = ((INT64)Corner[2].Y - Corner[0].Y) * ((INT64)Corner[1].X - Corner[0].X) -
           ((INT64)Corner[0].Y - Corner[1].Y) * ((INT64)Corner[0].X - Corner[2].X);
    if (Area == 0)
        return;

    /* Positive area winds clockwise on screen */
    Clockwise = (Area > 0);
    FrontFacing = Raster.FrontCounterClockwise ? !Clockwise : Clockwise;
    if (Raster.CullMode == D3D10_DDI_CULL_BACK && !FrontFacing)
        return;
    if (Raster.CullMode == D3D10_DDI_CULL_FRONT && FrontFacing)
        return;

    InverseArea = 1.0f / ((FLOAT)Area * (1.0f / 65536.0f));

    /* Interpolation planes for every pixel shader input */
    if (PixelShader != nullptr)
    {
        for (UINT Register = 0; Register < PixelShader->InputCount; Register++)
        {
            UINT Mode = PixelShader->InputMode[Register];
            BOOLEAN Perspective = (Mode == D3D10_SB_INTERPOLATION_LINEAR || Mode == D3D10_SB_INTERPOLATION_LINEAR_CENTROID ||
                                   Mode == D3D10_SB_INTERPOLATION_LINEAR_SAMPLE);

            for (UINT Component = 0; Component < 4; Component++)
            {
                FLOAT V[3];

                for (UINT Index = 0; Index < 3; Index++)
                {
                    V[Index] = RxWarpBitsAsFloat(Corner[Index].Source->Registers[Register][Component]);
                    if (Perspective)
                        V[Index] = Corner[Index].Rhw * V[Index];
                }
                Planes[Register][Component] = RxWarpMakePlane(Corner, InverseArea, V[0], V[1], V[2]);
            }
        }
    }
    DepthPlane = RxWarpMakePlane(Corner, InverseArea, Corner[0].Z, Corner[1].Z, Corner[2].Z);
    BiasDepth(RxWarpLargerMagnitude(RxWarpLargerMagnitude(Corner[1].Z, Corner[2].Z), Corner[0].Z));
    RhwPlane = RxWarpMakePlane(Corner, InverseArea, Corner[0].Rhw, Corner[1].Rhw, Corner[2].Rhw);

    /* Coverage wants a consistent winding */
    Ordered[0] = Corner[0];
    Ordered[1] = Clockwise ? Corner[1] : Corner[2];
    Ordered[2] = Clockwise ? Corner[2] : Corner[1];
    Edges[0] = RxWarpMakeEdge(Ordered[0], Ordered[1]);
    Edges[1] = RxWarpMakeEdge(Ordered[1], Ordered[2]);
    Edges[2] = RxWarpMakeEdge(Ordered[2], Ordered[0]);

    MinX = Corner[0].X;
    MaxX = Corner[0].X;
    MinY = Corner[0].Y;
    MaxY = Corner[0].Y;
    for (UINT Index = 1; Index < 3; Index++)
    {
        MinX = (Corner[Index].X < MinX) ? Corner[Index].X : MinX;
        MaxX = (Corner[Index].X > MaxX) ? Corner[Index].X : MaxX;
        MinY = (Corner[Index].Y < MinY) ? Corner[Index].Y : MinY;
        MaxY = (Corner[Index].Y > MaxY) ? Corner[Index].Y : MaxY;
    }
    PointSprite = FALSE;
    Triangle = TRUE;
    Cover((MinX >> 8) - 1, (MinY >> 8) - 1, (MaxX + 255) >> 8, (MaxY + 255) >> 8, TRUE,
          [&Edges](INT64 Sx, INT64 Sy)
          {
              return RxWarpInsideEdge(Edges[0], Sx, Sy) && RxWarpInsideEdge(Edges[1], Sx, Sy) && RxWarpInsideEdge(Edges[2], Sx, Sy);
          });
}

/* A line plane follows the major axis only, as the reference sets lines up */
static
RX_WARP_PLANE
RxWarpMakeLinePlane(
    _In_ BOOLEAN XMajor,
    _In_ FLOAT Start,
    _In_ FLOAT InverseLength,
    _In_ FLOAT V0,
    _In_ FLOAT V1)
{
    RX_WARP_PLANE Plane;
    FLOAT Slope = (V1 - V0) * InverseLength;

    Plane.A = XMajor ? Slope : 0.0f;
    Plane.B = XMajor ? 0.0f : Slope;
    Plane.C = V0 - Start * Slope;
    return Plane;
}

/* A point in the diamond around a pixel center; its lower edges, and the right corner for steep lines, count as inside */
static
BOOLEAN
RxWarpPointInDiamond(
    _In_ BOOLEAN XMajor,
    _In_ FLOAT X,
    _In_ FLOAT Y,
    _In_ FLOAT Cx,
    _In_ FLOAT Cy)
{
    FLOAT Distance = fabsf(X - Cx) + fabsf(Y - Cy);
    BOOLEAN RightCorner = (Y == Cy && !XMajor && X > Cx);

    if (!RightCorner && Y <= Cy)
        return Distance < 0.5f;
    return Distance <= 0.5f;
}

/*
 * The line as a start point and a direction with each end pulled to just short of the center
 * of a pixel whose diamond holds it: the start pixel is drawn and the end pixel is left alone.
 */
static
VOID
RxWarpLineCoverage(
    _In_ FLOAT X0,
    _In_ FLOAT X1,
    _In_ FLOAT Y0,
    _In_ FLOAT Y1,
    _Out_writes_(4) FLOAT *Line,
    _Out_ UINT *Inclusive)
{
    FLOAT Dx = X1 - X0;
    FLOAT Dy = Y1 - Y0;
    BOOLEAN XMajor = fabsf(X0 - X1) >= fabsf(Y0 - Y1);
    FLOAT StartX = ceilf(X0) - 0.5f;
    FLOAT StartY = ceilf(Y0) - 0.5f;
    FLOAT EndX = ceilf(X1) - 0.5f;
    FLOAT EndY = ceilf(Y1) - 0.5f;
    BOOLEAN StartIn = RxWarpPointInDiamond(XMajor, X0, Y0, StartX, StartY);
    BOOLEAN EndIn = RxWarpPointInDiamond(XMajor, X1, Y1, EndX, EndY);

    *Inclusive = XMajor ? 4 : 12;
    if (StartIn && EndIn && StartX == EndX && StartY == EndY)
    {
        Line[0] = 0.0f;
        Line[1] = 0.0f;
        Line[2] = -0.5f;
        Line[3] = -0.5f;
        return;
    }

    if (StartIn)
    {
        FLOAT Mx;
        FLOAT My;

        if (XMajor)
        {
            Mx = (X0 - StartX) + ((Dx <= 0.0f) ? -1.0f / 256.0f : 1.0f / 256.0f);
            My = (Dy / Dx) * Mx;
        }
        else
        {
            My = (Y0 - StartY) + ((Dy <= 0.0f) ? -1.0f / 256.0f : 1.0f / 256.0f);
            Mx = (Dx / Dy) * My;
        }
        X0 = X0 - Mx;
        Y0 = Y0 - My;
        Dx = Dx + Mx;
        Dy = Dy + My;
    }
    if (EndIn)
    {
        FLOAT Mx;
        FLOAT My;

        if (XMajor)
        {
            Mx = ((X0 + Dx) - EndX) + ((Dx <= 0.0f) ? -1.0f / 256.0f : 1.0f / 256.0f);
            My = (Dy / Dx) * Mx;
        }
        else
        {
            My = ((Y0 + Dy) - EndY) + ((Dy <= 0.0f) ? -1.0f / 256.0f : 1.0f / 256.0f);
            Mx = (Dx / Dy) * My;
        }
        Dx = Dx - Mx;
        Dy = Dy - My;
    }
    Line[0] = Dx;
    Line[1] = Dy;
    Line[2] = X0;
    Line[3] = Y0;
}

/* Inside when the pixel center projects onto the line along either axis no more than half a pixel off */
static
BOOLEAN
RxWarpLineCovers(
    _In_reads_(4) const FLOAT *Line,
    _In_ UINT Inclusive,
    _In_ FLOAT X,
    _In_ FLOAT Y)
{
    FLOAT Wide = nextafterf(0.5f, 1.0f);
    FLOAT AcrossX = (Line[2] - X) * (-1.0f / Line[0]);
    FLOAT AcrossY = (Line[3] - Y) * (-1.0f / Line[1]);
    FLOAT OffY = (Line[3] - Y) + Line[1] * AcrossX;
    FLOAT OffX = (Line[2] - X) + Line[0] * AcrossY;
    BOOLEAN AlongX;
    BOOLEAN AlongY;

    AlongX = AcrossX >= 0.0f && AcrossX <= 1.0f && OffY > -((Inclusive & 1) ? Wide : 0.5f) && OffY < ((Inclusive & 4) ? Wide : 0.5f);
    AlongY = AcrossY >= 0.0f && AcrossY <= 1.0f && OffX > -((Inclusive & 2) ? Wide : 0.5f) && OffX < ((Inclusive & 8) ? Wide : 0.5f);
    return AlongX || AlongY;
}

/* Aliased lines: the diamond exit rule, with the last pixel left for the next line */
VOID
RxWarpDraw::RasterizeLine(
    _In_ const RX_WARP_VERTEX *A,
    _In_ const RX_WARP_VERTEX *B)
{
    FLOAT X0;
    FLOAT Y0;
    FLOAT X1;
    FLOAT Y1;
    BOOLEAN XMajor;
    FLOAT InverseLength;
    FLOAT Start;
    INT64 Fx0;
    INT64 Fy0;
    INT64 Fx1;
    INT64 Fy1;
    FLOAT Line[4];
    UINT Inclusive;

    ToScreen(A, &Corner[0]);
    ToScreen(B, &Corner[1]);
    Corner[2] = Corner[1];

    X0 = (FLOAT)Corner[0].X * (1.0f / 256.0f);
    Y0 = (FLOAT)Corner[0].Y * (1.0f / 256.0f);
    X1 = (FLOAT)Corner[1].X * (1.0f / 256.0f);
    Y1 = (FLOAT)Corner[1].Y * (1.0f / 256.0f);
    if (X0 == X1 && Y0 == Y1)
        return;

    XMajor = !(fabsf(X0 - X1) < fabsf(Y0 - Y1));
    InverseLength = 1.0f / (XMajor ? X1 - X0 : Y1 - Y0);
    Start = XMajor ? X0 : Y0;
    FrontFacing = TRUE;

    if (PixelShader != nullptr)
    {
        for (UINT Register = 0; Register < PixelShader->InputCount; Register++)
        {
            UINT Mode = PixelShader->InputMode[Register];
            BOOLEAN Perspective = (Mode == D3D10_SB_INTERPOLATION_LINEAR || Mode == D3D10_SB_INTERPOLATION_LINEAR_CENTROID ||
                                   Mode == D3D10_SB_INTERPOLATION_LINEAR_SAMPLE);

            for (UINT Component = 0; Component < 4; Component++)
            {
                FLOAT V0 = RxWarpBitsAsFloat(Corner[0].Source->Registers[Register][Component]);
                FLOAT V1 = RxWarpBitsAsFloat(Corner[1].Source->Registers[Register][Component]);

                if (Perspective)
                {
                    V0 = Corner[0].Rhw * V0;
                    V1 = Corner[1].Rhw * V1;
                }
                Planes[Register][Component] = RxWarpMakeLinePlane(XMajor, Start, InverseLength, V0, V1);
            }
        }
    }
    DepthPlane = RxWarpMakeLinePlane(XMajor, Start, InverseLength, Corner[0].Z, Corner[1].Z);
    BiasDepth(RxWarpLargerMagnitude(Corner[0].Z, Corner[1].Z));
    RhwPlane = RxWarpMakeLinePlane(XMajor, Start, InverseLength, Corner[0].Rhw, Corner[1].Rhw);

    Fx0 = Corner[0].X;
    Fy0 = Corner[0].Y;
    Fx1 = Corner[1].X;
    Fy1 = Corner[1].Y;
    RxWarpLineCoverage(X0, X1, Y0, Y1, Line, &Inclusive);
    PointSprite = FALSE;
    Triangle = FALSE;
    Cover((INT)(((Fx0 < Fx1) ? Fx0 : Fx1) >> 8) - 1, (INT)(((Fy0 < Fy1) ? Fy0 : Fy1) >> 8) - 1,
          (INT)((((Fx0 > Fx1) ? Fx0 : Fx1) + 255) >> 8), (INT)((((Fy0 > Fy1) ? Fy0 : Fy1) + 255) >> 8), FALSE,
          [=](INT64 Sx, INT64 Sy)
          {
              return RxWarpLineCovers(Line, Inclusive, (FLOAT)Sx * (1.0f / 256.0f), (FLOAT)Sy * (1.0f / 256.0f));
          });
}

/* Points cover a one pixel square around the vertex, with the triangle fill rule on its edges */
VOID
RxWarpDraw::RasterizePoint(
    _In_ const RX_WARP_VERTEX *A)
{
    INT64 SquareLeft;
    INT64 SquareTop;

    ToScreen(A, &Corner[0]);
    Corner[1] = Corner[0];
    Corner[2] = Corner[0];
    FrontFacing = TRUE;

    if (PixelShader != nullptr)
    {
        for (UINT Register = 0; Register < PixelShader->InputCount; Register++)
        {
            for (UINT Component = 0; Component < 4; Component++)
            {
                Planes[Register][Component].A = 0.0f;
                Planes[Register][Component].B = 0.0f;
                Planes[Register][Component].C = RxWarpBitsAsFloat(Corner[0].Source->Registers[Register][Component]);
            }
        }
    }
    DepthPlane = { 0.0f, 0.0f, Corner[0].Z };
    RhwPlane = { 0.0f, 0.0f, Corner[0].Rhw };

    SquareLeft = (INT64)Corner[0].X - 128;
    SquareTop = (INT64)Corner[0].Y - 128;
    PointSprite = TRUE;
    Triangle = FALSE;
    Cover((Corner[0].X - 256) >> 8, (Corner[0].Y - 256) >> 8, (Corner[0].X + 511) >> 8, (Corner[0].Y + 511) >> 8, TRUE,
          [=](INT64 Sx, INT64 Sy)
          {
              return Sx >= SquareLeft && Sx < SquareLeft + 256 && Sy >= SquareTop && Sy < SquareTop + 256;
          });
}

/* One input's interpolated value at a point, divided back by w when it is perspective correct */
VOID
RxWarpDraw::Interpolate(
    _In_ UINT Register,
    _In_ FLOAT X,
    _In_ FLOAT Y,
    _Out_writes_(4) UINT *Value,
    _In_ BOOLEAN CenterW)
{
    UINT Mode = PixelShader->InputMode[Register];
    BOOLEAN Perspective = !PointSprite && (Mode == D3D10_SB_INTERPOLATION_LINEAR || Mode == D3D10_SB_INTERPOLATION_LINEAR_CENTROID ||
                                           Mode == D3D10_SB_INTERPOLATION_LINEAR_SAMPLE);
    FLOAT W = 1.0f;

    /* eval_* divides by w at the pixel center, wherever it evaluates */
    if (Perspective && CenterW)
        W = 1.0f / RxWarpEvaluate(RhwPlane, (FLOAT)QuadX + (FLOAT)(EvaluatingLane & 1) + 0.5f, (FLOAT)QuadY + (FLOAT)(EvaluatingLane >> 1) + 0.5f);
    else if (Perspective)
        W = 1.0f / RxWarpEvaluate(RhwPlane, X, Y);

    if (Mode == D3D10_SB_INTERPOLATION_CONSTANT || Mode == D3D10_SB_INTERPOLATION_UNDEFINED)
    {
        memcpy(Value, Provoking->Registers[Register], 16);
        return;
    }
    for (UINT Component = 0; Component < 4; Component++)
    {
        FLOAT Result = RxWarpEvaluate(Planes[Register][Component], X, Y);

        if (Perspective)
            Result = Result * W;
        Value[Component] = RxWarpFloatAsBits(Result);
    }
}

VOID
RxWarpDraw::EvaluateInput(
    _In_ VOID *Context,
    _In_ UINT Lane,
    _In_ UINT Register,
    _In_ RX_WARP_EVALUATE_KIND Kind,
    _In_reads_(2) const INT *Parameter,
    _Out_writes_(4) UINT *Value)
{
    RxWarpDraw *Draw = static_cast<RxWarpDraw *>(Context);
    FLOAT X = (FLOAT)(Draw->QuadX + (INT)(Lane & 1)) + 0.5f;
    FLOAT Y = (FLOAT)(Draw->QuadY + (INT)(Lane >> 1)) + 0.5f;

    if (Register >= RxWarpMaxRegisters)
    {
        Value[0] = Value[1] = Value[2] = Value[3] = 0;
        return;
    }
    switch (Kind)
    {
        case RxWarpEvaluateSnapped:
            /* Sixteenths of a pixel from the center, four signed bits each */
            X = X + (FLOAT)((INT)((UINT)Parameter[0] << 28) >> 28) * (1.0f / 16.0f);
            Y = Y + (FLOAT)((INT)((UINT)Parameter[1] << 28) >> 28) * (1.0f / 16.0f);
            break;
        case RxWarpEvaluateSample:
            /* The reference looks the index up in its top to bottom sample order */
            if ((UINT)Parameter[0] < Draw->SampleCount)
            {
                X = X + (FLOAT)Draw->SortedOffset[Parameter[0]][0] * (1.0f / 256.0f);
                Y = Y + (FLOAT)Draw->SortedOffset[Parameter[0]][1] * (1.0f / 256.0f);
            }
            break;
        case RxWarpEvaluateCentroid:
            X = Draw->CentroidAt[Lane][0];
            Y = Draw->CentroidAt[Lane][1];
            break;
    }
    Draw->EvaluatingLane = Lane;
    Draw->Interpolate(Register, X, Y, Value, TRUE);
}

/* An input at the lane's interpolation point, from the three corners in double precision */
BOOLEAN
RxWarpDraw::PreciseInput(
    _In_ VOID *Context,
    _In_ UINT Lane,
    _In_ UINT Register,
    _Out_writes_(4) UINT *Value)
{
    RxWarpDraw *Draw = static_cast<RxWarpDraw *>(Context);
    const RX_WARP_SCREEN_VERTEX *Corner = Draw->Corner;
    UINT Mode;
    BOOLEAN Perspective;
    BOOLEAN Centroid;
    double X;
    double Y;
    double X0;
    double Y0;
    double X1;
    double Y1;
    double X2;
    double Y2;
    double Area;
    double Weight[3];
    double Divisor = 0.0;

    if (!Draw->Triangle || Draw->PixelShader == nullptr || Register >= Draw->PixelShader->InputCount)
        return FALSE;
    Mode = Draw->PixelShader->InputMode[Register];
    if (Mode != D3D10_SB_INTERPOLATION_LINEAR && Mode != D3D10_SB_INTERPOLATION_LINEAR_CENTROID &&
        Mode != D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE && Mode != D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_CENTROID)
    {
        return FALSE;
    }
    if (Draw->PixelShader->InputName[Register] != D3D10_SB_NAME_UNDEFINED)
        return FALSE;
    Perspective = (Mode == D3D10_SB_INTERPOLATION_LINEAR || Mode == D3D10_SB_INTERPOLATION_LINEAR_CENTROID);
    Centroid = (Mode == D3D10_SB_INTERPOLATION_LINEAR_CENTROID || Mode == D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_CENTROID);

    X = Centroid ? Draw->CentroidAt[Lane][0] : (double)(Draw->QuadX + (INT)(Lane & 1)) + 0.5;
    Y = Centroid ? Draw->CentroidAt[Lane][1] : (double)(Draw->QuadY + (INT)(Lane >> 1)) + 0.5;
    X0 = Corner[0].X / 256.0;
    Y0 = Corner[0].Y / 256.0;
    X1 = Corner[1].X / 256.0;
    Y1 = Corner[1].Y / 256.0;
    X2 = Corner[2].X / 256.0;
    Y2 = Corner[2].Y / 256.0;
    Area = (X1 - X0) * (Y2 - Y0) - (Y1 - Y0) * (X2 - X0);
    if (Area == 0.0)
        return FALSE;
    Weight[1] = ((X - X0) * (Y2 - Y0) - (Y - Y0) * (X2 - X0)) / Area;
    Weight[2] = ((X1 - X0) * (Y - Y0) - (Y1 - Y0) * (X - X0)) / Area;
    Weight[0] = 1.0 - Weight[1] - Weight[2];
    if (Perspective)
    {
        for (UINT Index = 0; Index < 3; Index++)
        {
            Weight[Index] *= (double)Corner[Index].Rhw;
            Divisor += Weight[Index];
        }
        if (Divisor == 0.0)
            return FALSE;
    }

    for (UINT Component = 0; Component < 4; Component++)
    {
        double Sum = 0.0;

        for (UINT Index = 0; Index < 3; Index++)
            Sum += Weight[Index] * (double)RxWarpBitsAsFloat(Corner[Index].Source->Registers[Register][Component]);
        if (Perspective)
            Value[Component] = RxWarpFloatAsBits((FLOAT)Sum * (1.0f / (FLOAT)Divisor));
        else
            Value[Component] = RxWarpFloatAsBits((FLOAT)Sum);
    }
    return TRUE;
}

/* Shades once per pixel, or once per sample when the shader asks for sample rate */
VOID
RxWarpDraw::ShadeQuad(
    _In_ INT X,
    _In_ INT Y,
    _In_ UINT Covered,
    _In_reads_(RxWarpLanes) const UINT *Samples)
{
    if (SampleCount <= 1 || PixelShader == nullptr || !PixelShader->SampleRate)
    {
        ShadePass(X, Y, Covered, Samples, UINT_MAX);
        return;
    }

    for (UINT Pass = 0; Pass < SampleCount; Pass++)
    {
        UINT Only[RxWarpLanes];
        UINT Lanes = 0;

        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        {
            Only[Lane] = Samples[Lane] & (1u << Pass);
            if (Only[Lane] != 0)
                Lanes |= 1u << Lane;
        }
        if (Lanes != 0)
            ShadePass(X, Y, Lanes, Only, Pass);
    }
}

VOID
RxWarpDraw::ShadePass(
    _In_ INT X,
    _In_ INT Y,
    _In_ UINT Covered,
    _In_reads_(RxWarpLanes) const UINT *Samples,
    _In_ UINT Pass)
{
    FLOAT Depth[RxWarpLanes];
    UINT Survivors = Covered;
    UINT AllSamples = (SampleCount >= 32) ? UINT_MAX : (1u << SampleCount) - 1;
    FLOAT PassX = (Pass != UINT_MAX) ? (FLOAT)SampleOffset[Pass][0] * (1.0f / 256.0f) : 0.0f;
    FLOAT PassY = (Pass != UINT_MAX) ? (FLOAT)SampleOffset[Pass][1] * (1.0f / 256.0f) : 0.0f;

    QuadX = X;
    QuadY = Y;
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        FLOAT Sx = (FLOAT)(X + (INT)(Lane & 1)) + 0.5f;
        FLOAT Sy = (FLOAT)(Y + (INT)(Lane >> 1)) + 0.5f;

        Depth[Lane] = RxWarpEvaluate(DepthPlane, Sx, Sy);
    }

    if (PixelShader != nullptr)
    {
        Invocation.Prepare(PixelShader);
        Invocation.RasterSamples = SampleCount;
        Invocation.Live = Covered;
        Invocation.Helper = 0xF & ~Covered;
        Invocation.PrimitiveId = Primitive;
        Invocation.EvaluateRoutine = EvaluateInput;
        Invocation.EvaluateContext = this;
        Invocation.PreciseRoutine = Triangle ? PreciseInput : nullptr;

        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        {
            FLOAT Sx = (FLOAT)(X + (INT)(Lane & 1)) + 0.5f;
            FLOAT Sy = (FLOAT)(Y + (INT)(Lane >> 1)) + 0.5f;
            FLOAT W = 1.0f / RxWarpEvaluate(RhwPlane, Sx, Sy);

            Invocation.CoverageIn[Lane] = Samples[Lane] & Pipeline->SampleMask;
            Invocation.SampleIndex[Lane] = (Pass != UINT_MAX) ? Pass : 0;

            /* Pixels the sample mask or the edges leave partly covered take centroid attributes at their first covered sample */
            CentroidAt[Lane][0] = Sx;
            CentroidAt[Lane][1] = Sy;
            if (Invocation.CoverageIn[Lane] != 0 && Invocation.CoverageIn[Lane] != AllSamples)
            {
                ULONG First;

                _BitScanForward(&First, Invocation.CoverageIn[Lane]);
                CentroidAt[Lane][0] = Sx + (FLOAT)SampleOffset[First][0] * (1.0f / 256.0f);
                CentroidAt[Lane][1] = Sy + (FLOAT)SampleOffset[First][1] * (1.0f / 256.0f);
            }

            for (UINT Register = 0; Register < PixelShader->InputCount; Register++)
            {
                UINT *Input = Invocation.Input(Register, Lane);
                UINT Mode = PixelShader->InputMode[Register];
                UINT Name = PixelShader->InputName[Register];
                BOOLEAN AtSample = (Pass != UINT_MAX) && (Mode == D3D10_SB_INTERPOLATION_LINEAR_SAMPLE ||
                                                          Mode == D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_SAMPLE);

                if (Name == D3D10_SB_NAME_POSITION)
                {
                    Input[0] = RxWarpFloatAsBits(AtSample ? Sx + PassX : Sx);
                    Input[1] = RxWarpFloatAsBits(AtSample ? Sy + PassY : Sy);
                    Input[2] = RxWarpFloatAsBits(Depth[Lane]);
                    Input[3] = RxWarpFloatAsBits(W);
                    continue;
                }
                if (Name == D3D10_SB_NAME_IS_FRONT_FACE)
                {
                    Input[0] = Input[1] = Input[2] = Input[3] = FrontFacing ? UINT_MAX : 0;
                    continue;
                }
                if (Name == D3D10_SB_NAME_PRIMITIVE_ID)
                {
                    Input[0] = Input[1] = Input[2] = Input[3] = Primitive;
                    continue;
                }
                if (Name == D3D10_SB_NAME_SAMPLE_INDEX)
                {
                    Input[0] = Input[1] = Input[2] = Input[3] = Invocation.SampleIndex[Lane];
                    continue;
                }
                if (Mode == D3D10_SB_INTERPOLATION_LINEAR_CENTROID || Mode == D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_CENTROID)
                    Interpolate(Register, CentroidAt[Lane][0], CentroidAt[Lane][1], Input);
                else if (AtSample)
                    Interpolate(Register, Sx + PassX, Sy + PassY, Input);
                else
                    Interpolate(Register, Sx, Sy, Input);
            }
        }

        Invocation.Run(PixelShader, &PixelBindings);
        Invocation.EvaluateRoutine = nullptr;
        Invocation.PreciseRoutine = nullptr;

        /* The reference counts every lane of a shaded quad, helpers included */
        Device->Counters[RxWarpCountPsInvocations] += RxWarpLanes;
        Survivors &= ~Invocation.Discarded;
        if (PixelShader->WritesDepth)
        {
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
                Depth[Lane] = Invocation.Depth[Lane];
        }
    }

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        UINT Mask = Samples[Lane] & Pipeline->SampleMask;

        if (!(Survivors & (1u << Lane)))
            continue;
        if (PixelShader != nullptr && PixelShader->WritesCoverage)
            Mask &= Invocation.CoverageOut[Lane];
        else if (PixelShader != nullptr && BlendState->AlphaToCoverageEnable && PixelShader->OutputMask[0] != 0)
            Mask &= RxWarpAlphaCoverage(RxWarpBitsAsFloat(Invocation.Output(0, Lane)[3]), X + (INT)(Lane & 1), Y + (INT)(Lane >> 1), SampleCount);

        for (Sample = 0; Sample < SampleCount; Sample++)
        {
            FLOAT Value = Depth[Lane];

            if (!(Mask & (1u << Sample)))
                continue;

            /* Each sample moves the center depth along the plane's slopes, unless the shader wrote one */
            if (SampleCount > 1 && (PixelShader == nullptr || !PixelShader->WritesDepth))
            {
                FLOAT Ox = (FLOAT)SampleOffset[Sample][0] * (1.0f / 256.0f);
                FLOAT Oy = (FLOAT)SampleOffset[Sample][1] * (1.0f / 256.0f);

                Value = Value + (DepthPlane.A * Ox + DepthPlane.B * Oy);
            }
            Value = (Value < MinDepth) ? MinDepth : Value;
            Value = (Value > MaxDepth) ? MaxDepth : Value;
            WritePixel(X + (INT)(Lane & 1), Y + (INT)(Lane >> 1), Lane, Value);
        }
    }
    Sample = 0;
}

static
BYTE *
RxWarpViewTexel(
    _In_ RxWarpView *View,
    _In_ INT X,
    _In_ INT Y,
    _In_ UINT Slice,
    _In_ UINT Sample,
    _Out_ UINT *Bytes)
{
    RxWarpResource *Resource = View->Resource;
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(View->Format);
    RX_WARP_SUBRESOURCE *Sub;
    SIZE_T Depth = 0;

    *Bytes = 0;
    if (Resource == nullptr || Info == nullptr || Resource->Memory == nullptr)
        return nullptr;

    /* Slices outside the view fall back to its first one */
    if (Slice >= (View->SliceCount ? View->SliceCount : 1))
        Slice = 0;
    if (Resource->Dimension == D3D10DDIRESOURCE_TEXTURE3D)
    {
        Sub = Resource->Subresource(View->MipSlice);
        if (Sub == nullptr || View->FirstSlice + Slice >= Sub->Depth)
            return nullptr;
        Depth = (SIZE_T)(View->FirstSlice + Slice) * Sub->DepthPitch;
    }
    else
    {
        Sub = Resource->Subresource((View->FirstSlice + Slice) * Resource->MipLevels + View->MipSlice);
    }
    if (Sub == nullptr || X < 0 || Y < 0 || (UINT)X >= Sub->Width || (UINT)Y >= Sub->Height)
        return nullptr;
    *Bytes = Info->Bytes;
    if (Sample >= Resource->SampleCount)
        Sample = 0;
    return Resource->Memory + Sub->Offset + Depth + (SIZE_T)Y * Sub->RowPitch +
           ((SIZE_T)X * Resource->SampleCount + Sample) * Info->Bytes;
}

static
BOOLEAN
RxWarpCompareValues(
    _In_ UINT Function,
    _In_ FLOAT Source,
    _In_ FLOAT Dest)
{
    switch (Function)
    {
        case D3D10_DDI_COMPARISON_NEVER:
            return FALSE;
        case D3D10_DDI_COMPARISON_LESS:
            return Source < Dest;
        case D3D10_DDI_COMPARISON_EQUAL:
            return Source == Dest;
        case D3D10_DDI_COMPARISON_LESS_EQUAL:
            return Source <= Dest;
        case D3D10_DDI_COMPARISON_GREATER:
            return Source > Dest;
        case D3D10_DDI_COMPARISON_NOT_EQUAL:
            return Source != Dest;
        case D3D10_DDI_COMPARISON_GREATER_EQUAL:
            return Source >= Dest;
        default:
            return TRUE;
    }
}

static
UINT
RxWarpStencilOp(
    _In_ UINT Operation,
    _In_ UINT Value,
    _In_ UINT Reference)
{
    switch (Operation)
    {
        case D3D10_DDI_STENCIL_OP_ZERO:
            return 0;
        case D3D10_DDI_STENCIL_OP_REPLACE:
            return Reference & 0xFF;
        case D3D10_DDI_STENCIL_OP_INCR_SAT:
            return (Value < 0xFF) ? Value + 1 : 0xFF;
        case D3D10_DDI_STENCIL_OP_DECR_SAT:
            return (Value > 0) ? Value - 1 : 0;
        case D3D10_DDI_STENCIL_OP_INVERT:
            return ~Value & 0xFF;
        case D3D10_DDI_STENCIL_OP_INCR:
            return (Value + 1) & 0xFF;
        case D3D10_DDI_STENCIL_OP_DECR:
            return (Value - 1) & 0xFF;
        default:
            return Value;
    }
}

/* Depth and stencil test with their writes; FALSE when the pixel is rejected */
BOOLEAN
RxWarpDraw::DepthStencilTest(
    _In_ INT X,
    _In_ INT Y,
    _In_ FLOAT Depth)
{
    RxWarpView *View = Pipeline->DepthStencilView;
    const D3D10_DDI_DEPTH_STENCILOP_DESC *Face;
    BYTE *Texel;
    UINT Bytes;
    UINT Stored[4];
    BYTE Element[16] = {};
    FLOAT StoredDepth;
    UINT Stencil;
    UINT NewStencil;
    BOOLEAN StencilPass = TRUE;
    BOOLEAN DepthPass = TRUE;
    BOOLEAN WriteDepth;
    BOOLEAN WriteStencil;
    FLOAT Values[4];

    if (View == nullptr || (!DepthState.DepthEnable && !DepthState.StencilEnable))
        return TRUE;
    Texel = RxWarpViewTexel(View, X, Y, TargetSlice, Sample, &Bytes);
    if (Texel == nullptr)
        return TRUE;

    memcpy(Element, Texel, Bytes);
    RxWarpDecodeTexel(View->Format, Element, Stored);
    StoredDepth = RxWarpBitsAsFloat(Stored[0]);
    Stencil = Stored[1] & 0xFF;
    Face = FrontFacing ? &DepthState.FrontFace : &DepthState.BackFace;

    if (DepthState.StencilEnable)
    {
        StencilPass = RxWarpCompareValues(Face->StencilFunc, (FLOAT)(Pipeline->StencilRef & DepthState.StencilReadMask),
                                          (FLOAT)(Stencil & DepthState.StencilReadMask));
    }
    if (DepthState.DepthEnable && StencilPass)
        DepthPass = RxWarpCompareValues(DepthState.DepthFunc, Depth, StoredDepth);

    NewStencil = Stencil;
    if (DepthState.StencilEnable)
    {
        UINT Operation = !StencilPass ? Face->StencilFailOp : !DepthPass ? Face->StencilDepthFailOp : Face->StencilPassOp;

        NewStencil = RxWarpStencilOp(Operation, Stencil, Pipeline->StencilRef);
        NewStencil = (Stencil & ~DepthState.StencilWriteMask) | (NewStencil & DepthState.StencilWriteMask);
    }

    WriteDepth = DepthState.DepthEnable && StencilPass && DepthPass &&
                 DepthState.DepthWriteMask == D3D10_DDI_DEPTH_WRITE_MASK_ALL && !(View->Flags & D3D11_DDI_CREATE_DSV_READ_ONLY_DEPTH);
    WriteStencil = DepthState.StencilEnable && NewStencil != Stencil && !(View->Flags & D3D11_DDI_CREATE_DSV_READ_ONLY_STENCIL);
    if (WriteDepth || WriteStencil)
    {
        Values[0] = WriteDepth ? Depth : StoredDepth;
        Values[1] = (FLOAT)NewStencil;
        Values[2] = 0.0f;
        Values[3] = 0.0f;
        if (RxWarpEncodeColor(View->Format, Values, Element))
            memcpy(Texel, Element, Bytes);
    }
    return StencilPass && DepthPass;
}

/* Render target formats of eight bit normalized channels */
static
BOOLEAN
RxWarpIsByteTarget(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_A8_UNORM:
            return TRUE;
        default:
            return FALSE;
    }
}

static
VOID
RxWarpBlendFactor(
    _In_ UINT Factor,
    _In_reads_(4) const FLOAT *Source,
    _In_reads_(4) const FLOAT *Source1,
    _In_reads_(4) const FLOAT *Dest,
    _In_reads_(4) const FLOAT *Constant,
    _In_ UINT Component,
    _Out_ FLOAT *Result)
{
    FLOAT Saturated;

    switch (Factor)
    {
        case D3D10_DDI_BLEND_ZERO:
            *Result = 0.0f;
            break;
        case D3D10_DDI_BLEND_ONE:
            *Result = 1.0f;
            break;
        case D3D10_DDI_BLEND_SRC_COLOR:
            *Result = Source[Component];
            break;
        case D3D10_DDI_BLEND_INV_SRC_COLOR:
            *Result = 1.0f - Source[Component];
            break;
        case D3D10_DDI_BLEND_SRC_ALPHA:
            *Result = Source[3];
            break;
        case D3D10_DDI_BLEND_INV_SRC_ALPHA:
            *Result = 1.0f - Source[3];
            break;
        case D3D10_DDI_BLEND_DEST_ALPHA:
            *Result = Dest[3];
            break;
        case D3D10_DDI_BLEND_INV_DEST_ALPHA:
            *Result = 1.0f - Dest[3];
            break;
        case D3D10_DDI_BLEND_DEST_COLOR:
            *Result = Dest[Component];
            break;
        case D3D10_DDI_BLEND_INV_DEST_COLOR:
            *Result = 1.0f - Dest[Component];
            break;
        case D3D10_DDI_BLEND_SRC_ALPHASAT:
            Saturated = 1.0f - Dest[3];
            Saturated = (Source[3] < Saturated) ? Source[3] : Saturated;
            *Result = (Component == 3) ? 1.0f : Saturated;
            break;
        case D3D10_DDI_BLEND_BLEND_FACTOR:
            *Result = Constant[Component];
            break;
        case D3D10_DDI_BLEND_INVBLEND_FACTOR:
            *Result = 1.0f - Constant[Component];
            break;
        case D3D10_DDI_BLEND_SRC1_COLOR:
            *Result = Source1[Component];
            break;
        case D3D10_DDI_BLEND_INV_SRC1_COLOR:
            *Result = 1.0f - Source1[Component];
            break;
        case D3D10_DDI_BLEND_SRC1_ALPHA:
            *Result = Source1[3];
            break;
        case D3D10_DDI_BLEND_INV_SRC1_ALPHA:
            *Result = 1.0f - Source1[3];
            break;
        default:
            *Result = 0.0f;
            break;
    }
}

static
FLOAT
RxWarpBlendOp(
    _In_ UINT Operation,
    _In_ FLOAT Source,
    _In_ FLOAT Dest)
{
    switch (Operation)
    {
        case D3D10_DDI_BLEND_OP_SUBTRACT:
            return Source - Dest;
        case D3D10_DDI_BLEND_OP_REV_SUBTRACT:
            return Dest - Source;
        case D3D10_DDI_BLEND_OP_MIN:
            return fminf(Source, Dest);
        case D3D10_DDI_BLEND_OP_MAX:
            return fmaxf(Source, Dest);
        default:
            return Source + Dest;
    }
}

/* Normalized targets clamp what they receive to their range */
static
VOID
RxWarpClampToFormat(
    _In_ DXGI_FORMAT Format,
    _Inout_updates_(4) FLOAT *Color)
{
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);
    FLOAT Low;

    if (Info == nullptr || Info->Kind != RxKindPlain || Info->ChannelCount == 0)
        return;
    if (Info->Channels[0].Type == RxChannelUnorm || Info->Channels[0].Type == RxChannelSrgb)
        Low = 0.0f;
    else if (Info->Channels[0].Type == RxChannelSnorm)
        Low = -1.0f;
    else
        return;
    for (UINT Component = 0; Component < 4; Component++)
    {
        if (!(Color[Component] > Low))
            Color[Component] = Low;
        if (Color[Component] > 1.0f)
            Color[Component] = 1.0f;
    }
}

VOID
RxWarpDraw::Blend(
    _In_ UINT Target,
    _In_ INT X,
    _In_ INT Y,
    _In_reads_(4) const UINT *Color,
    _In_reads_(4) const UINT *Color1,
    _In_reads_opt_(4) const INT *Shadow)
{
    RxWarpView *View = Pipeline->RenderTargets[Target];
    const D3D11_1_DDI_RENDER_TARGET_BLEND_DESC *Desc = &BlendState->RenderTarget[Target];
    BYTE *Texel;
    UINT Bytes;
    BYTE Element[16] = {};
    BYTE Written[16] = {};
    BYTE Mask[16] = {};
    FLOAT Source[4];
    FLOAT Source1[4];
    FLOAT Dest[4];
    FLOAT Result[4];
    FLOAT Constant[4];
    UINT Raw[4];
    UINT WriteMask = Desc->RenderTargetWriteMask & 0xF;

    if (View == nullptr || WriteMask == 0)
        return;
    Texel = RxWarpViewTexel(View, X, Y, TargetSlice, Sample, &Bytes);
    if (Texel == nullptr)
        return;
    memcpy(Element, Texel, Bytes);

    if (RxWarpFormatIsInteger(View->Format))
    {
        if (!RxWarpEncodeUint(View->Format, Color, Written))
            return;
    }
    else
    {
        for (UINT Component = 0; Component < 4; Component++)
        {
            Source[Component] = RxWarpBitsAsFloat(Color[Component]);
            Source1[Component] = RxWarpBitsAsFloat(Color1[Component]);
            Constant[Component] = Pipeline->BlendFactor[Component];
        }
        RxWarpClampToFormat(View->Format, Source);
        RxWarpClampToFormat(View->Format, Source1);
        RxWarpClampToFormat(View->Format, Constant);
        memcpy(Result, Source, sizeof(Result));

        if (Desc->BlendEnable)
        {
            RxWarpDecodeTexel(View->Format, Element, Raw);
            for (UINT Component = 0; Component < 4; Component++)
                Dest[Component] = RxWarpBitsAsFloat(Raw[Component]);

            for (UINT Component = 0; Component < 4; Component++)
            {
                BOOLEAN Alpha = (Component == 3);
                FLOAT SourceFactor;
                FLOAT DestFactor;

                RxWarpBlendFactor(Alpha ? Desc->SrcBlendAlpha : Desc->SrcBlend, Source, Source1, Dest, Constant, Component, &SourceFactor);
                RxWarpBlendFactor(Alpha ? Desc->DestBlendAlpha : Desc->DestBlend, Source, Source1, Dest, Constant, Component, &DestFactor);
                Result[Component] = RxWarpBlendOp(Alpha ? Desc->BlendOpAlpha : Desc->BlendOp,
                                                  Source[Component] * SourceFactor, Dest[Component] * DestFactor);
            }
            RxWarpClampToFormat(View->Format, Result);
        }
        else if (Shadow != nullptr && RxWarpIsByteTarget(View->Format))
        {
            /* Unblended, a filtered value the shader left alone goes to eight bits by dropping its fraction */
            for (UINT Component = 0; Component < 4; Component++)
            {
                if (Shadow[Component] >= 0 && RxWarpFloatAsBits(RxWarpByteResult(Shadow[Component])) == Color[Component])
                    Result[Component] = (FLOAT)(Shadow[Component] >> 7) * (1.0f / 255.0f);
            }
        }
        if (!RxWarpEncodeColor(View->Format, Result, Written))
            return;
    }

    /* Write mask per component, as bit masks over the element; packed float layouts take all or nothing */
    for (UINT Component = 0; Component < 4; Component++)
    {
        UINT AllBits[4] = {};
        BYTE Full[16];

        if (!(WriteMask & (1u << Component)))
            continue;
        AllBits[Component] = UINT_MAX;
        if (!RxWarpEncodeUint(View->Format, AllBits, Full))
        {
            memset(Mask, 0xFF, sizeof(Mask));
            break;
        }
        for (UINT Byte = 0; Byte < 16; Byte++)
            Mask[Byte] |= Full[Byte];
    }
    for (UINT Byte = 0; Byte < Bytes; Byte++)
        Texel[Byte] = (BYTE)((Element[Byte] & ~Mask[Byte]) | (Written[Byte] & Mask[Byte]));
}

VOID
RxWarpDraw::WritePixel(
    _In_ INT X,
    _In_ INT Y,
    _In_ UINT Lane,
    _In_ FLOAT Depth)
{
    UINT Zero[4] = {};

    if (!DepthStencilTest(X, Y, Depth))
        return;
    Device->Counters[RxWarpCountSamplesPassed]++;
    if (PixelShader == nullptr)
        return;

    for (UINT Target = 0; Target < Pipeline->RenderTargetCount; Target++)
    {
        const UINT *Color1 = (Target == 0 && PixelShader->OutputCount > 1) ? Invocation.Output(1, Lane) : Zero;

        if (!(PixelShader->OutputMask[Target]))
            continue;
        Blend(Target, X, Y, Invocation.Output(Target, Lane), Color1, Invocation.ByteShadow[Target][Lane]);
    }
}

/* How a topology groups vertices: primitive size, vertices per step, and where adjacency puts the real ones */
typedef struct _RX_WARP_TOPOLOGY
{
    UINT Size;
    BOOLEAN Strip;
    BOOLEAN Adjacency;
} RX_WARP_TOPOLOGY;

static
BOOLEAN
RxWarpTopologyOf(
    _In_ D3D10_DDI_PRIMITIVE_TOPOLOGY Topology,
    _Out_ RX_WARP_TOPOLOGY *Shape)
{
    switch (Topology)
    {
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_POINTLIST:
            *Shape = { 1, FALSE, FALSE };
            return TRUE;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINELIST:
            *Shape = { 2, FALSE, FALSE };
            return TRUE;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINESTRIP:
            *Shape = { 2, TRUE, FALSE };
            return TRUE;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
            *Shape = { 3, FALSE, FALSE };
            return TRUE;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
            *Shape = { 3, TRUE, FALSE };
            return TRUE;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
            *Shape = { 2, FALSE, TRUE };
            return TRUE;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
            *Shape = { 2, TRUE, TRUE };
            return TRUE;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
            *Shape = { 3, FALSE, TRUE };
            return TRUE;
        case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
            *Shape = { 3, TRUE, TRUE };
            return TRUE;
        default:
            return FALSE;
    }
}

/* An input primitive: through the geometry shader when there is one, else to stream output and the rasterizer */
VOID
RxWarpDraw::Assemble(
    _In_reads_(Count) const RX_WARP_VERTEX *const *Corners,
    _In_ UINT Count,
    _In_reads_(FullCount) const RX_WARP_VERTEX *const *Full,
    _In_ UINT FullCount)
{
    if (GeometryShader != nullptr)
    {
        RunGeometry(Full, FullCount);
        return;
    }
    if (StreamShader != nullptr)
        StreamOut(0, Corners, Count);
    if (CanRasterize && RasterizedStream == 0)
        ClipPrimitive(Corners, Count);
}

VOID
RxWarpDraw::GeometryEmit(
    _In_ VOID *Context,
    _In_ RxWarpInvocation *Source,
    _In_ UINT Stream,
    _In_ UINT Lane,
    _In_ BOOLEAN Emit,
    _In_ BOOLEAN Cut)
{
    RxWarpDraw *Draw = static_cast<RxWarpDraw *>(Context);

    if (Stream >= 4)
        return;
    if (Emit && Draw->Emitted < Draw->GeometryShader->GsMaxVertices)
    {
        RX_WARP_VERTEX Vertex;

        for (UINT Register = 0; Register < RxWarpMaxRegisters; Register++)
            memcpy(Vertex.Registers[Register], Source->Output(Register, Lane), 16);
        Draw->Strips[Stream].push_back(Vertex);
        Draw->Emitted++;
    }
    if (Cut)
        Draw->FlushStrip(Stream);
}

VOID
RxWarpDraw::RunGeometry(
    _In_reads_(Count) const RX_WARP_VERTEX *const *Full,
    _In_ UINT Count)
{
    RxWarpShader *Shader = GeometryShader;

    for (UINT Instance = 0; Instance < Shader->GsInstances; Instance++)
    {
        GeometryInvocation.Prepare(Shader);
        GeometryInvocation.Live = 1;
        GeometryInvocation.Helper = 0;
        GeometryInvocation.PrimitiveId = Primitive;
        GeometryInvocation.GsInstance = Instance;
        GeometryInvocation.InputVertexCount = Count;
        for (UINT Index = 0; Index < Count && Index < 32; Index++)
            GeometryInvocation.InputVertex[Index] = Full[Index]->Registers[0];
        GeometryInvocation.EmitRoutine = GeometryEmit;
        GeometryInvocation.EmitContext = this;
        Emitted = 0;
        for (UINT Stream = 0; Stream < 4; Stream++)
            Strips[Stream].clear();

        GeometryInvocation.Run(Shader, &GeometryBindings);
        Device->Counters[RxWarpCountGsInvocations]++;

        /* Whatever was left open ends with the program */
        for (UINT Stream = 0; Stream < 4; Stream++)
            FlushStrip(Stream);
    }
}

/* One patch: hull shader phases, the tessellator, then the domain shader at every index it produced */
VOID
RxWarpDraw::RunPatch(
    _In_reads_(Count) const RX_WARP_VERTEX *const *Points,
    _In_ UINT Count)
{
    static const UINT8 QuadNames[6] = { 0, 1, 2, 3, 4, 5 };
    static const UINT8 TriangleNames[4] = { 6, 7, 8, 9 };
    static const UINT8 LineNames[2] = { 10, 11 };
    RxWarpShader *Hull = HullShader;
    UINT OutputCount = (Hull->OutputControlPoints < 32) ? Hull->OutputControlPoints : 32;
    const RxWarpShader *ControlPhase = nullptr;
    const UINT8 *Names = nullptr;
    UINT NameCount = 0;
    FLOAT Factors[6] = {};
    UINT PrimitiveSize;

    for (const RxWarpShader &Phase : Hull->Phases)
    {
        if (Phase.PhaseKind == D3D11_SB_OPCODE_HS_CONTROL_POINT_PHASE)
            ControlPhase = &Phase;
    }

    /* Output control points; without a control point phase they are the input points */
    HullPoints.resize(OutputCount);
    for (UINT First = 0; First < OutputCount; First += RxWarpLanes)
    {
        UINT Lanes = (OutputCount - First < RxWarpLanes) ? OutputCount - First : RxWarpLanes;

        if (ControlPhase == nullptr)
        {
            for (UINT Lane = 0; Lane < Lanes; Lane++)
            {
                if (First + Lane < Count)
                    HullPoints[First + Lane] = *Points[First + Lane];
                else
                    ZeroMemory(&HullPoints[First + Lane], sizeof(HullPoints[First + Lane]));
            }
            continue;
        }
        Invocation.Prepare(ControlPhase);
        Invocation.Live = (1u << Lanes) - 1;
        Invocation.Helper = 0;
        Invocation.PrimitiveId = Primitive;
        Invocation.InputVertexCount = Count;
        for (UINT Index = 0; Index < Count && Index < 32; Index++)
            Invocation.InputVertex[Index] = Points[Index]->Registers[0];
        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            Invocation.ControlPointId[Lane] = First + Lane;
        Invocation.Run(ControlPhase, &HullBindings);
        for (UINT Lane = 0; Lane < Lanes; Lane++)
        {
            for (UINT Register = 0; Register < RxWarpMaxRegisters; Register++)
                memcpy(HullPoints[First + Lane].Registers[Register], Invocation.Output(Register, Lane), 16);
        }
    }

    /* Fork and join phases build the patch constants, one instance at a time */
    ZeroMemory(PatchConstants, sizeof(PatchConstants));
    for (const RxWarpShader &Phase : Hull->Phases)
    {
        if (Phase.PhaseKind == D3D11_SB_OPCODE_HS_CONTROL_POINT_PHASE)
            continue;
        for (UINT Instance = 0; Instance < Phase.PhaseInstances; Instance++)
        {
            Invocation.Prepare(&Phase);
            Invocation.Live = 1;
            Invocation.Helper = 0;
            Invocation.PrimitiveId = Primitive;
            Invocation.PhaseInstance = Instance;
            Invocation.InputVertexCount = Count;
            for (UINT Index = 0; Index < Count && Index < 32; Index++)
                Invocation.InputVertex[Index] = Points[Index]->Registers[0];
            Invocation.OutputPointCount = OutputCount;
            for (UINT Index = 0; Index < OutputCount; Index++)
                Invocation.OutputPoint[Index] = HullPoints[Index].Registers[0];
            Invocation.PatchConstants = PatchConstants[0];
            for (UINT Register = 0; Register < RxWarpMaxRegisters; Register++)
                memcpy(Invocation.Output(Register, 0), PatchConstants[Register], 16);
            Invocation.Run(&Phase, &HullBindings);
            for (UINT Register = 0; Register < RxWarpMaxRegisters; Register++)
                memcpy(PatchConstants[Register], Invocation.Output(Register, 0), 16);
        }
    }
    Invocation.PatchConstants = nullptr;
    Invocation.OutputPointCount = 0;
    Device->Counters[RxWarpCountHsInvocations]++;

    switch (Hull->TessDomain)
    {
        case D3D11_SB_TESSELLATOR_DOMAIN_QUAD:
            Names = QuadNames;
            NameCount = 6;
            break;
        case D3D11_SB_TESSELLATOR_DOMAIN_TRI:
            Names = TriangleNames;
            NameCount = 4;
            break;
        case D3D11_SB_TESSELLATOR_DOMAIN_ISOLINE:
            Names = LineNames;
            NameCount = 2;
            break;
        default:
            return;
    }
    for (UINT Index = 0; Index < NameCount; Index++)
    {
        UINT Slot = Hull->TessFactorSlot[Names[Index]];

        if (Slot != 0)
            memcpy(&Factors[Index], &PatchConstants[(Slot - 1) / 4][(Slot - 1) % 4], sizeof(FLOAT));
    }
    Tessellator.Tessellate(Hull->TessDomain, Hull->TessPartitioning, Hull->TessOutput, Factors);

    /* The domain shader runs once per index, not once per point */
    DomainPoints.resize(Tessellator.Indices.size());
    for (SIZE_T First = 0; First < Tessellator.Indices.size(); First += RxWarpLanes)
    {
        SIZE_T Remaining = Tessellator.Indices.size() - First;
        UINT Lanes = (Remaining < RxWarpLanes) ? (UINT)Remaining : RxWarpLanes;

        Invocation.Prepare(DomainShader);
        Invocation.Live = (1u << Lanes) - 1;
        Invocation.Helper = 0;
        Invocation.PrimitiveId = Primitive;
        Invocation.InputVertexCount = OutputCount;
        for (UINT Index = 0; Index < OutputCount; Index++)
            Invocation.InputVertex[Index] = HullPoints[Index].Registers[0];
        Invocation.PatchConstants = PatchConstants[0];
        for (UINT Lane = 0; Lane < Lanes; Lane++)
        {
            UINT Point = Tessellator.Indices[First + Lane];
            RX_WARP_DOMAIN_POINT Where = {};

            if (Point < Tessellator.Points.size())
                Where = Tessellator.Points[Point];
            Invocation.Domain[Lane][0] = Where.U;
            Invocation.Domain[Lane][1] = Where.V;
            Invocation.Domain[Lane][2] = (Hull->TessDomain == D3D11_SB_TESSELLATOR_DOMAIN_TRI) ? (1.0f - Where.V) - Where.U : 0.0f;
            Invocation.Domain[Lane][3] = 0.0f;
        }
        Invocation.Run(DomainShader, &DomainBindings);
        Device->Counters[RxWarpCountDsInvocations] += Lanes;
        for (UINT Lane = 0; Lane < Lanes; Lane++)
        {
            for (UINT Register = 0; Register < RxWarpMaxRegisters; Register++)
                memcpy(DomainPoints[First + Lane].Registers[Register], Invocation.Output(Register, Lane), 16);
        }
    }
    Invocation.PatchConstants = nullptr;
    Invocation.InputVertexCount = 0;

    if (Hull->TessOutput == D3D11_SB_TESSELLATOR_OUTPUT_POINT)
        PrimitiveSize = 1;
    else if (Hull->TessDomain == D3D11_SB_TESSELLATOR_DOMAIN_ISOLINE)
        PrimitiveSize = 2;
    else
        PrimitiveSize = 3;
    for (SIZE_T First = 0; First + PrimitiveSize <= DomainPoints.size(); First += PrimitiveSize)
    {
        const RX_WARP_VERTEX *Corners[3];

        for (UINT Slot = 0; Slot < PrimitiveSize; Slot++)
            Corners[Slot] = &DomainPoints[First + Slot];
        Assemble(Corners, PrimitiveSize, Corners, PrimitiveSize);
    }
}

/* Turns the vertices emitted since the last cut into primitives of the declared output topology */
VOID
RxWarpDraw::FlushStrip(
    _In_ UINT Stream)
{
    std::vector<RX_WARP_VERTEX> Strip;

    Strip.swap(Strips[Stream]);
    switch (GeometryShader->GsOutputTopology)
    {
        case D3D10_SB_PRIMITIVE_TOPOLOGY_POINTLIST:
            for (SIZE_T Index = 0; Index < Strip.size(); Index++)
            {
                const RX_WARP_VERTEX *Corners[1] = { &Strip[Index] };

                OutputPrimitive(Stream, Corners, 1);
            }
            break;
        case D3D10_SB_PRIMITIVE_TOPOLOGY_LINESTRIP:
            for (SIZE_T Index = 0; Index + 1 < Strip.size(); Index++)
            {
                const RX_WARP_VERTEX *Corners[2] = { &Strip[Index], &Strip[Index + 1] };

                OutputPrimitive(Stream, Corners, 2);
            }
            break;
        default:
            for (SIZE_T Index = 0; Index + 2 < Strip.size(); Index++)
            {
                const RX_WARP_VERTEX *Corners[3] = { &Strip[Index], &Strip[Index + ((Index & 1) ? 2 : 1)], &Strip[Index + ((Index & 1) ? 1 : 2)] };

                OutputPrimitive(Stream, Corners, 3);
            }
            break;
    }
}

VOID
RxWarpDraw::OutputPrimitive(
    _In_ UINT Stream,
    _In_reads_(Count) const RX_WARP_VERTEX *const *Corners,
    _In_ UINT Count)
{
    if (Stream == 0)
        Device->Counters[RxWarpCountGsPrimitives]++;
    StreamOut(Stream, Corners, Count);
    if (CanRasterize && Stream == RasterizedStream)
        ClipPrimitive(Corners, Count);
}

/* Writes a whole primitive to the stream output buffers, or nothing when one of them would overflow */
VOID
RxWarpDraw::StreamOut(
    _In_ UINT Stream,
    _In_reads_(Count) const RX_WARP_VERTEX *const *Corners,
    _In_ UINT Count)
{
    UINT Strides[4] = {};
    BOOLEAN Used[4] = {};

    if (StreamShader == nullptr || StreamShader->StreamDecl.empty())
        return;

    for (const D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY &Entry : StreamShader->StreamDecl)
    {
        UINT Components = 0;

        if (Entry.Stream != Stream || Entry.OutputSlot >= 4)
            continue;
        for (UINT Bit = 0; Bit < 4; Bit++)
            Components += (Entry.RegisterMask >> Bit) & 1;
        Strides[Entry.OutputSlot] += Components * 4;
        Used[Entry.OutputSlot] = TRUE;
    }

    if (Stream == 0)
        Device->Counters[RxWarpCountSoNeeded]++;
    for (UINT Slot = 0; Slot < 4; Slot++)
    {
        RxWarpResource *Target = Pipeline->StreamTargets[Slot];

        if (StreamShader->StreamStrides[Slot] != 0)
            Strides[Slot] = StreamShader->StreamStrides[Slot];
        if (Used[Slot] && Target != nullptr && (SIZE_T)Target->StreamFilled + (SIZE_T)Strides[Slot] * Count > Target->MemorySize)
            return;
    }

    for (UINT Vertex = 0; Vertex < Count; Vertex++)
    {
        UINT Written[4] = {};

        for (const D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY &Entry : StreamShader->StreamDecl)
        {
            RxWarpResource *Target;

            if (Entry.Stream != Stream || Entry.OutputSlot >= 4)
                continue;
            Target = Pipeline->StreamTargets[Entry.OutputSlot];
            for (UINT Bit = 0; Bit < 4; Bit++)
            {
                if (!((Entry.RegisterMask >> Bit) & 1))
                    continue;
                /* A register index of all ones is a gap that only moves the write position */
                if (Target != nullptr && Entry.RegisterIndex < RxWarpMaxRegisters)
                {
                    memcpy(Target->Memory + Target->StreamFilled + Written[Entry.OutputSlot],
                           &Corners[Vertex]->Registers[Entry.RegisterIndex][Bit], 4);
                }
                Written[Entry.OutputSlot] += 4;
            }
        }
        for (UINT Slot = 0; Slot < 4; Slot++)
        {
            if (Used[Slot] && Pipeline->StreamTargets[Slot] != nullptr)
                Pipeline->StreamTargets[Slot]->StreamFilled += Strides[Slot];
        }
    }
    if (Stream == 0)
        Device->Counters[RxWarpCountSoWritten]++;
}

VOID
RxWarpDraw::Run(
    _In_ UINT VertexCount,
    _In_ UINT StartVertex,
    _In_ BOOLEAN Indexed,
    _In_ INT BaseVertex,
    _In_ UINT InstanceCount,
    _In_ UINT StartInstance)
{
    std::vector<UINT> Indices(VertexCount);
    UINT Restart = (Pipeline->IndexFormat == DXGI_FORMAT_R16_UINT) ? 0xFFFF : UINT_MAX;
    RX_WARP_TOPOLOGY Shape;

    UINT PatchPoints = 0;

    if (Device->Predicated())
        return;
    if (Pipeline->Topology >= D3D11_DDI_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST &&
        Pipeline->Topology <= D3D11_DDI_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST)
    {
        PatchPoints = Pipeline->Topology - D3D11_DDI_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1;
        Shape = { PatchPoints, FALSE, FALSE };
    }
    else if (!RxWarpTopologyOf(Pipeline->Topology, &Shape))
    {
        return;
    }
    if (!Prepare())
        return;

    for (UINT Index = 0; Index < VertexCount; Index++)
        Indices[Index] = Indexed ? FetchIndex(StartVertex + Index) : StartVertex + Index;

    for (UINT Instance = 0; Instance < InstanceCount; Instance++)
    {
        std::vector<UINT> Run;

        Primitive = 0;
        Device->Counters[RxWarpCountIaVertices] += VertexCount;
        ShadeVertices(Indices.data(), VertexCount, Indexed ? BaseVertex : 0, Instance, StartInstance);

        /* Patches need both tessellation stages; without them nothing is drawn */
        if (PatchPoints != 0)
        {
            for (UINT First = 0; HullShader != nullptr && First + PatchPoints <= VertexCount; First += PatchPoints)
            {
                const RX_WARP_VERTEX *Points[32];

                for (UINT Index = 0; Index < PatchPoints; Index++)
                    Points[Index] = &Vertices[First + Index];
                Device->Counters[RxWarpCountIaPrimitives]++;
                RunPatch(Points, PatchPoints);
                Primitive++;
            }
            continue;
        }

        /* Strips break at the cut index; lists only take whole primitives */
        for (UINT Position = 0; Position <= VertexCount; Position++)
        {
            UINT Width = Shape.Adjacency ? Shape.Size * 2 : Shape.Size;
            SIZE_T Count;

            if (Position < VertexCount && !(Shape.Strip && Indexed && Indices[Position] == Restart))
            {
                Run.push_back(Position);
                if (Shape.Strip || Run.size() < Width)
                    continue;
            }

            Count = Run.size();
            if (!Shape.Strip)
            {
                if (Count == Width)
                {
                    const RX_WARP_VERTEX *Corners[3];
                    const RX_WARP_VERTEX *Full[6];

                    for (UINT Index = 0; Index < Shape.Size; Index++)
                        Corners[Index] = &Vertices[Run[Shape.Adjacency ? Index * ((Shape.Size == 2) ? 1 : 2) + ((Shape.Size == 2) ? 1 : 0) : Index]];
                    for (UINT Index = 0; Index < Width; Index++)
                        Full[Index] = &Vertices[Run[Index]];
                    Device->Counters[RxWarpCountIaPrimitives]++;
                    Assemble(Corners, Shape.Size, Full, Width);
                    Primitive++;
                }
                Run.clear();
                continue;
            }

            /* A strip ended: walk it */
            if (Shape.Size == 2)
            {
                SIZE_T First = Shape.Adjacency ? 1 : 0;
                SIZE_T Last = (Shape.Adjacency && Count != 0) ? Count - 1 : Count;

                for (SIZE_T Index = First; Index + 1 < Last; Index++)
                {
                    const RX_WARP_VERTEX *Corners[2] = { &Vertices[Run[Index]], &Vertices[Run[Index + 1]] };

                    Device->Counters[RxWarpCountIaPrimitives]++;
                    if (Shape.Adjacency)
                    {
                        const RX_WARP_VERTEX *Full[4] = { &Vertices[Run[Index - 1]], Corners[0], Corners[1], &Vertices[Run[Index + 2]] };

                        Assemble(Corners, 2, Full, 4);
                    }
                    else
                    {
                        Assemble(Corners, 2, Corners, 2);
                    }
                    Primitive++;
                }
            }
            else if (!Shape.Adjacency)
            {
                /* Odd triangles swap their last two vertices so every triangle keeps its winding */
                for (SIZE_T Index = 0; Index + 2 < Count; Index++)
                {
                    const RX_WARP_VERTEX *Corners[3] = { &Vertices[Run[Index]], &Vertices[Run[Index + ((Index & 1) ? 2 : 1)]],
                                                         &Vertices[Run[Index + ((Index & 1) ? 1 : 2)]] };

                    Device->Counters[RxWarpCountIaPrimitives]++;
                    Assemble(Corners, 3, Corners, 3);
                    Primitive++;
                }
            }
            else
            {
                SIZE_T Triangles = (Count >= 6) ? (Count - 4) / 2 : 0;

                for (SIZE_T Index = 0; Index < Triangles; Index++)
                {
                    SIZE_T Base = Index * 2;
                    const RX_WARP_VERTEX *Corners[3] = { &Vertices[Run[Base]], &Vertices[Run[Base + ((Index & 1) ? 4 : 2)]],
                                                         &Vertices[Run[Base + ((Index & 1) ? 2 : 4)]] };
                    SIZE_T Order[6];
                    const RX_WARP_VERTEX *Full[6];

                    /*
                     * The geometry shader sees vertex, adjacent, vertex, adjacent, vertex, adjacent.
                     * The ends of the strip take their missing neighbours from the far side.
                     */
                    if (Triangles == 1)
                    {
                        SIZE_T One[6] = { 0, 1, 2, 5, 4, 3 };

                        memcpy(Order, One, sizeof(Order));
                    }
                    else if (Index == 0)
                    {
                        SIZE_T First[6] = { 0, 1, 2, 6, 4, 3 };

                        memcpy(Order, First, sizeof(Order));
                    }
                    else if (Index == Triangles - 1)
                    {
                        if (Index & 1)
                        {
                            SIZE_T Last[6] = { Base + 2, Base - 2, Base, Base + 3, Base + 4, Base + 5 };

                            memcpy(Order, Last, sizeof(Order));
                        }
                        else
                        {
                            SIZE_T Last[6] = { Base, Base - 2, Base + 2, Base + 5, Base + 4, Base + 3 };

                            memcpy(Order, Last, sizeof(Order));
                        }
                    }
                    else if (Index & 1)
                    {
                        SIZE_T Middle[6] = { Base + 2, Base - 2, Base, Base + 3, Base + 4, Base + 6 };

                        memcpy(Order, Middle, sizeof(Order));
                    }
                    else
                    {
                        SIZE_T Middle[6] = { Base, Base - 2, Base + 2, Base + 6, Base + 4, Base + 3 };

                        memcpy(Order, Middle, sizeof(Order));
                    }
                    for (UINT Slot = 0; Slot < 6; Slot++)
                        Full[Slot] = &Vertices[Run[(Order[Slot] < Count) ? Order[Slot] : Count - 1]];

                    Device->Counters[RxWarpCountIaPrimitives]++;
                    Assemble(Corners, 3, Full, 6);
                    Primitive++;
                }
            }
            Run.clear();
        }
    }
}

static
RxWarpDevice *
RxWarpGetDevice(
    _In_ D3D10DDI_HDEVICE Device)
{
    return RxWarpObject<RxWarpDevice>(Device);
}

VOID
APIENTRY
RxWarpDdi::Draw(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT VertexCount,
    _In_ UINT StartVertex)
{
    RxWarpLock Lock;
    RxWarpDraw Work(RxWarpGetDevice(Device));

    Work.Run(VertexCount, StartVertex, FALSE, 0, 1, 0);
}

VOID
APIENTRY
RxWarpDdi::DrawIndexed(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT IndexCount,
    _In_ UINT StartIndex,
    _In_ INT BaseVertex)
{
    RxWarpLock Lock;
    RxWarpDraw Work(RxWarpGetDevice(Device));

    Work.Run(IndexCount, StartIndex, TRUE, BaseVertex, 1, 0);
}

VOID
APIENTRY
RxWarpDdi::DrawInstanced(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT VertexCount,
    _In_ UINT InstanceCount,
    _In_ UINT StartVertex,
    _In_ UINT StartInstance)
{
    RxWarpLock Lock;
    RxWarpDraw Work(RxWarpGetDevice(Device));

    Work.Run(VertexCount, StartVertex, FALSE, 0, InstanceCount, StartInstance);
}

VOID
APIENTRY
RxWarpDdi::DrawIndexedInstanced(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT IndexCount,
    _In_ UINT InstanceCount,
    _In_ UINT StartIndex,
    _In_ INT BaseVertex,
    _In_ UINT StartInstance)
{
    RxWarpLock Lock;
    RxWarpDraw Work(RxWarpGetDevice(Device));

    Work.Run(IndexCount, StartIndex, TRUE, BaseVertex, InstanceCount, StartInstance);
}

/* Arguments come from a buffer the GPU may have written: count, instances, start, then base and start instance */
VOID
APIENTRY
RxWarpDdi::DrawInstancedIndirect(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Buffer,
    _In_ UINT Offset)
{
    RxWarpLock Lock;
    RxWarpResource *Arguments = RxWarpObject<RxWarpResource>(Buffer);
    UINT Values[4];

    if (Arguments == nullptr || Arguments->Memory == nullptr || (SIZE_T)Offset + sizeof(Values) > Arguments->MemorySize)
        return;
    memcpy(Values, Arguments->Memory + Offset, sizeof(Values));

    RxWarpDraw Work(RxWarpGetDevice(Device));
    Work.Run(Values[0], Values[2], FALSE, 0, Values[1], Values[3]);
}

VOID
APIENTRY
RxWarpDdi::DrawIndexedInstancedIndirect(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRESOURCE Buffer,
    _In_ UINT Offset)
{
    RxWarpLock Lock;
    RxWarpResource *Arguments = RxWarpObject<RxWarpResource>(Buffer);
    UINT Values[5];

    if (Arguments == nullptr || Arguments->Memory == nullptr || (SIZE_T)Offset + sizeof(Values) > Arguments->MemorySize)
        return;
    memcpy(Values, Arguments->Memory + Offset, sizeof(Values));

    RxWarpDraw Work(RxWarpGetDevice(Device));
    Work.Run(Values[0], Values[2], TRUE, (INT)Values[3], Values[1], Values[4]);
}

/* Draws what stream output left in the buffer bound to the first vertex slot */
VOID
APIENTRY
RxWarpDdi::DrawAuto(
    _In_ D3D10DDI_HDEVICE Device)
{
    RxWarpLock Lock;
    RxWarpDevice *Owner = RxWarpGetDevice(Device);
    RX_WARP_BUFFER_BINDING *Binding = &Owner->Pipeline.VertexBuffers[0];
    UINT Count;

    if (Binding->Resource == nullptr || Binding->Stride == 0 || Binding->Resource->StreamFilled <= Binding->Offset)
        return;
    Count = (Binding->Resource->StreamFilled - Binding->Offset) / Binding->Stride;

    RxWarpDraw Work(Owner);
    Work.Run(Count, 0, FALSE, 0, 1, 0);
}

