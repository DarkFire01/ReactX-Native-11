/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Draws: input assembly, vertex shading, clipping, rasterization and output merging
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"

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

static
UINT
RxWarpLaneCount(
    _In_ UINT Mask)
{
    UINT Count = 0;

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        Count += (Mask >> Lane) & 1;
    return Count;
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

    template <typename Test>
    VOID
    Cover(
        _In_ INT MinX,
        _In_ INT MinY,
        _In_ INT MaxX,
        _In_ INT MaxY,
        _In_ Test Inside);

    VOID
    ShadeQuad(
        _In_ INT X,
        _In_ INT Y,
        _In_ UINT Covered);

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
        _In_reads_(4) const UINT *Color1);

    RxWarpDevice *Device;
    RX_WARP_PIPELINE *Pipeline;
    RxWarpShader *VertexShader = nullptr;
    RxWarpShader *PixelShader = nullptr;
    RX_WARP_STAGE_BINDINGS VertexBindings = {};
    RX_WARP_STAGE_BINDINGS PixelBindings = {};
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
    RX_WARP_STAGE_STATE *State = &Pipeline->Stages[Stage];

    ZeroMemory(Bindings, sizeof(*Bindings));
    for (UINT Slot = 0; Slot < RxWarpConstantBufferSlots; Slot++)
    {
        RX_WARP_CONSTANT_BINDING *Binding = &State->ConstantBuffers[Slot];
        UINT Available;

        if (Binding->Resource == nullptr || Binding->Resource->Memory == nullptr)
            continue;
        Available = (UINT)(Binding->Resource->MemorySize / 16);
        if (Binding->First >= Available)
            continue;
        Available -= Binding->First;
        Bindings->ConstantBuffer[Slot] = Binding->Resource->Memory + (SIZE_T)Binding->First * 16;
        Bindings->ConstantCount[Slot] = (Binding->Count < Available) ? Binding->Count : Available;
    }
    memcpy(Bindings->Views, State->Views, sizeof(Bindings->Views));
    for (UINT Slot = 0; Slot < 16; Slot++)
        Bindings->Samplers[Slot] = State->Samplers[Slot] ? &State->Samplers[Slot]->State : nullptr;
}

/* Gathers state for the draw; FALSE when nothing can be drawn */
BOOLEAN
RxWarpDraw::Prepare(VOID)
{
    const D3D10_DDI_VIEWPORT *Viewport;
    UINT Width = UINT_MAX;
    UINT Height = UINT_MAX;

    VertexShader = Pipeline->Stages[RxWarpStageVertex].Shader;
    PixelShader = Pipeline->Stages[RxWarpStagePixel].Shader;
    if (VertexShader == nullptr || VertexShader->PositionOutput == UINT_MAX || Pipeline->ViewportCount == 0)
        return FALSE;
    PositionRegister = VertexShader->PositionOutput;

    BindStage(RxWarpStageVertex, &VertexBindings);
    BindStage(RxWarpStagePixel, &PixelBindings);

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

    Viewport = &Pipeline->Viewports[0];
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
    Left = (Left > 0) ? Left : 0;
    Top = (Top > 0) ? Top : 0;
    Right = (Right < (INT)Width) ? Right : (INT)Width;
    Bottom = (Bottom < (INT)Height) ? Bottom : (INT)Height;
    if (Raster.ScissorEnable)
    {
        const D3D10_DDI_RECT *Scissor = &Pipeline->Scissors[0];

        if (Pipeline->ScissorCount == 0)
            return FALSE;
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
    Screen->X = RxWarpRoundEven((Rhw * X * Scale[0] + Bias[0] + 0.5f) * 256.0f);
    Screen->Y = RxWarpRoundEven((Rhw * Y * Scale[1] + Bias[1] + 0.5f) * 256.0f);
    Screen->Z = Rhw * Z * Scale[2] + Bias[2];
    Screen->Rhw = Rhw;
    Screen->Source = Vertex;
}

/* Distance to each clip plane; negative is outside */
static
VOID
RxWarpClipDistances(
    _In_reads_(4) const UINT *Position,
    _In_ BOOLEAN DepthClip,
    _Out_writes_(6) FLOAT *Distance)
{
    FLOAT X = RxWarpBitsAsFloat(Position[0]);
    FLOAT Y = RxWarpBitsAsFloat(Position[1]);
    FLOAT Z = RxWarpBitsAsFloat(Position[2]);
    FLOAT W = RxWarpBitsAsFloat(Position[3]);

    Distance[0] = DepthClip ? Z : 1.0f;
    Distance[1] = DepthClip ? W - Z : 1.0f;
    Distance[2] = W + X;
    Distance[3] = W - X;
    Distance[4] = W - Y;
    Distance[5] = W + Y;
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
    FLOAT Distance[6];
    UINT Outside[3] = {};
    UINT Any;
    std::vector<RX_WARP_VERTEX> Polygon;
    std::vector<RX_WARP_VERTEX> Next;

    Device->Counters[RxWarpCountClipInvocations]++;
    for (UINT Index = 0; Index < Count; Index++)
    {
        RxWarpClipDistances(Corners[Index]->Registers[PositionRegister], Raster.DepthClipEnable != FALSE, Distance);
        for (UINT Plane = 0; Plane < 6; Plane++)
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

    for (UINT Plane = 0; Plane < 6 && Polygon.size() >= Count; Plane++)
    {
        SIZE_T Edges = (Count == 2) ? 1 : Polygon.size();

        if (!(Any & (1u << Plane)))
            continue;

        Next.clear();
        for (SIZE_T Index = 0; Index < Edges; Index++)
        {
            const RX_WARP_VERTEX &From = Polygon[Index];
            const RX_WARP_VERTEX &To = Polygon[(Index + 1) % Polygon.size()];
            FLOAT FromDistance[6];
            FLOAT ToDistance[6];
            BOOLEAN FromIn;
            BOOLEAN ToIn;
            RX_WARP_VERTEX Point;

            RxWarpClipDistances(From.Registers[PositionRegister], Raster.DepthClipEnable != FALSE, FromDistance);
            RxWarpClipDistances(To.Registers[PositionRegister], Raster.DepthClipEnable != FALSE, ToDistance);
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
    return Plane.A * X + Plane.B * Y + Plane.C;
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
    _In_ Test Inside)
{
    MinX = (MinX > Left) ? MinX : Left;
    MinY = (MinY > Top) ? MinY : Top;
    MaxX = (MaxX < Right) ? MaxX : Right;
    MaxY = (MaxY < Bottom) ? MaxY : Bottom;
    if (MinX >= MaxX || MinY >= MaxY)
        return;

    for (INT Y = MinY & ~1; Y < MaxY; Y += 2)
    {
        for (INT X = MinX & ~1; X < MaxX; X += 2)
        {
            UINT Covered = 0;

            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                INT Px = X + (INT)(Lane & 1);
                INT Py = Y + (INT)(Lane >> 1);

                if (Px < MinX || Px >= MaxX || Py < MinY || Py >= MaxY)
                    continue;

                /* Sample points sit one whole unit in, because of the half pixel bias */
                if (Inside(((INT64)Px + 1) << 8, ((INT64)Py + 1) << 8))
                    Covered |= 1u << Lane;
            }
            if (Covered != 0)
                ShadeQuad(X, Y, Covered);
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
    Cover((MinX >> 8) - 1, (MinY >> 8) - 1, (MaxX + 255) >> 8, (MaxY + 255) >> 8,
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

static
BOOLEAN
RxWarpInDiamond(
    _In_ INT64 X,
    _In_ INT64 Y,
    _In_ INT64 Cx,
    _In_ INT64 Cy)
{
    INT64 Dx = X - Cx;
    INT64 Dy = Y - Cy;

    return ((Dx < 0) ? -Dx : Dx) + ((Dy < 0) ? -Dy : Dy) < 128;
}

/* Whether the segment passes through the open diamond around a pixel center, in 24.8 */
static
BOOLEAN
RxWarpSegmentMeetsDiamond(
    _In_ INT64 X0,
    _In_ INT64 Y0,
    _In_ INT64 X1,
    _In_ INT64 Y1,
    _In_ INT64 Cx,
    _In_ INT64 Cy)
{
    INT64 Dx = X1 - X0;
    INT64 Dy = Y1 - Y0;

    if (RxWarpInDiamond(X0, Y0, Cx, Cy) || RxWarpInDiamond(X1, Y1, Cx, Cy))
        return TRUE;

    /* The distance is smallest where the segment crosses the diamond's axes */
    if (Dx != 0 && ((Cx >= X0 && Cx <= X1) || (Cx <= X0 && Cx >= X1)))
    {
        INT64 Offset = (Y0 - Cy) * Dx + (Cx - X0) * Dy;
        INT64 Limit = 128 * ((Dx < 0) ? -Dx : Dx);

        if (((Offset < 0) ? -Offset : Offset) < Limit)
            return TRUE;
    }
    if (Dy != 0 && ((Cy >= Y0 && Cy <= Y1) || (Cy <= Y0 && Cy >= Y1)))
    {
        INT64 Offset = (X0 - Cx) * Dy + (Cy - Y0) * Dx;
        INT64 Limit = 128 * ((Dy < 0) ? -Dy : Dy);

        if (((Offset < 0) ? -Offset : Offset) < Limit)
            return TRUE;
    }
    return FALSE;
}

/* Aliased lines: a pixel is drawn when the line leaves its diamond, so the last pixel is left for the next line */
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
    RhwPlane = RxWarpMakeLinePlane(XMajor, Start, InverseLength, Corner[0].Rhw, Corner[1].Rhw);

    Fx0 = Corner[0].X;
    Fy0 = Corner[0].Y;
    Fx1 = Corner[1].X;
    Fy1 = Corner[1].Y;
    PointSprite = FALSE;
    Cover((INT)(((Fx0 < Fx1) ? Fx0 : Fx1) >> 8) - 1, (INT)(((Fy0 < Fy1) ? Fy0 : Fy1) >> 8) - 1,
          (INT)((((Fx0 > Fx1) ? Fx0 : Fx1) + 255) >> 8), (INT)((((Fy0 > Fy1) ? Fy0 : Fy1) + 255) >> 8),
          [=](INT64 Sx, INT64 Sy)
          {
              return RxWarpSegmentMeetsDiamond(Fx0, Fy0, Fx1, Fy1, Sx, Sy) && !RxWarpInDiamond(Fx1, Fy1, Sx, Sy);
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
    Cover((Corner[0].X - 256) >> 8, (Corner[0].Y - 256) >> 8, (Corner[0].X + 511) >> 8, (Corner[0].Y + 511) >> 8,
          [=](INT64 Sx, INT64 Sy)
          {
              return Sx >= SquareLeft && Sx < SquareLeft + 256 && Sy >= SquareTop && Sy < SquareTop + 256;
          });
}

VOID
RxWarpDraw::ShadeQuad(
    _In_ INT X,
    _In_ INT Y,
    _In_ UINT Covered)
{
    FLOAT Depth[RxWarpLanes];
    UINT Survivors = Covered;

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        FLOAT Sx = (FLOAT)(X + (INT)(Lane & 1)) + 1.0f;
        FLOAT Sy = (FLOAT)(Y + (INT)(Lane >> 1)) + 1.0f;

        Depth[Lane] = RxWarpEvaluate(DepthPlane, Sx, Sy);
    }

    if (PixelShader != nullptr)
    {
        Invocation.Prepare(PixelShader);
        Invocation.Live = Covered;
        Invocation.Helper = 0xF & ~Covered;
        Invocation.PrimitiveId = Primitive;

        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        {
            FLOAT Sx = (FLOAT)(X + (INT)(Lane & 1)) + 1.0f;
            FLOAT Sy = (FLOAT)(Y + (INT)(Lane >> 1)) + 1.0f;
            FLOAT Rhw = RxWarpEvaluate(RhwPlane, Sx, Sy);

            for (UINT Register = 0; Register < PixelShader->InputCount; Register++)
            {
                UINT *Input = Invocation.Input(Register, Lane);
                UINT Mode = PixelShader->InputMode[Register];
                UINT Name = PixelShader->InputName[Register];

                if (Name == D3D10_SB_NAME_POSITION)
                {
                    Input[0] = RxWarpFloatAsBits((FLOAT)(X + (INT)(Lane & 1)) + 0.5f);
                    Input[1] = RxWarpFloatAsBits((FLOAT)(Y + (INT)(Lane >> 1)) + 0.5f);
                    Input[2] = RxWarpFloatAsBits(Depth[Lane]);
                    Input[3] = RxWarpFloatAsBits(1.0f / Rhw);
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
                if (Mode == D3D10_SB_INTERPOLATION_CONSTANT || Mode == D3D10_SB_INTERPOLATION_UNDEFINED)
                {
                    memcpy(Input, Provoking->Registers[Register], 16);
                    continue;
                }
                for (UINT Component = 0; Component < 4; Component++)
                {
                    FLOAT Value = RxWarpEvaluate(Planes[Register][Component], Sx, Sy);

                    if (!PointSprite && (Mode == D3D10_SB_INTERPOLATION_LINEAR || Mode == D3D10_SB_INTERPOLATION_LINEAR_CENTROID ||
                                         Mode == D3D10_SB_INTERPOLATION_LINEAR_SAMPLE))
                    {
                        Value = Value / Rhw;
                    }
                    Input[Component] = RxWarpFloatAsBits(Value);
                }
            }
        }

        Invocation.Run(PixelShader, &PixelBindings);
        Device->Counters[RxWarpCountPsInvocations] += RxWarpLaneCount(Covered);
        Survivors &= ~Invocation.Discarded;
        if (PixelShader->WritesDepth)
        {
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
                Depth[Lane] = Invocation.Depth[Lane];
        }
    }

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        FLOAT Value = Depth[Lane];

        if (!(Survivors & (1u << Lane)))
            continue;
        Value = (Value < MinDepth) ? MinDepth : Value;
        Value = (Value > MaxDepth) ? MaxDepth : Value;
        WritePixel(X + (INT)(Lane & 1), Y + (INT)(Lane >> 1), Lane, Value);
    }
}

static
BYTE *
RxWarpViewTexel(
    _In_ RxWarpView *View,
    _In_ INT X,
    _In_ INT Y,
    _Out_ UINT *Bytes)
{
    RxWarpResource *Resource = View->Resource;
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(View->Format);
    RX_WARP_SUBRESOURCE *Sub;

    *Bytes = 0;
    if (Resource == nullptr || Info == nullptr || Resource->Memory == nullptr)
        return nullptr;
    Sub = Resource->Subresource(View->FirstSlice * Resource->MipLevels + View->MipSlice);
    if (Sub == nullptr || X < 0 || Y < 0 || (UINT)X >= Sub->Width || (UINT)Y >= Sub->Height)
        return nullptr;
    *Bytes = Info->Bytes;
    return Resource->Memory + Sub->Offset + (SIZE_T)Y * Sub->RowPitch + (SIZE_T)X * Info->Bytes;
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
    Texel = RxWarpViewTexel(View, X, Y, &Bytes);
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
    _In_reads_(4) const UINT *Color1)
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
    Texel = RxWarpViewTexel(View, X, Y, &Bytes);
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
        Blend(Target, X, Y, Invocation.Output(Target, Lane), Color1);
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

    if (Device->Predicated())
        return;
    if (!RxWarpTopologyOf(Pipeline->Topology, &Shape))
        return;
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

                    for (UINT Index = 0; Index < Shape.Size; Index++)
                        Corners[Index] = &Vertices[Run[Shape.Adjacency ? Index * ((Shape.Size == 2) ? 1 : 2) + ((Shape.Size == 2) ? 1 : 0) : Index]];
                    Device->Counters[RxWarpCountIaPrimitives]++;
                    ClipPrimitive(Corners, Shape.Size);
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
                    ClipPrimitive(Corners, 2);
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
                    ClipPrimitive(Corners, 3);
                    Primitive++;
                }
            }
            else
            {
                for (SIZE_T Index = 0; Index * 2 + 4 < Count; Index++)
                {
                    SIZE_T Base = Index * 2;
                    const RX_WARP_VERTEX *Corners[3] = { &Vertices[Run[Base]], &Vertices[Run[Base + ((Index & 1) ? 4 : 2)]],
                                                         &Vertices[Run[Base + ((Index & 1) ? 2 : 4)]] };

                    Device->Counters[RxWarpCountIaPrimitives]++;
                    ClipPrimitive(Corners, 3);
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
