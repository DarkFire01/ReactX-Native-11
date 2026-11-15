/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     TrueType instruction interpreter
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "facetables.hpp"

/* 26.6 fixed point pixels */
typedef INT32 RxF26Dot6;

/* A point the hinter moves, in 26.6 pixels */
struct RxHintPoint
{
    RxF26Dot6 X;
    RxF26Dot6 Y;
    BYTE OnCurve;
};

/* Rasterizer state the font sets with SCANCTRL and SCANTYPE */
struct RxDropoutControl
{
    BOOLEAN Enabled;
    /* 0 to 6, the rule numbers of the TrueType scan converter */
    USHORT Mode;
};

/* How the rasterizer wants a size hinted */
struct RxHintRequest
{
    /* Font units to 26.6, as 16.16 multipliers */
    INT32 ScaleX;
    INT32 ScaleY;
    USHORT PpemX;
    USHORT PpemY;
    /* 26.6, what MPS reports */
    RxF26Dot6 PointSize;
    BOOLEAN Rotated;
    BOOLEAN Stretched;
    /* Rendering at more than one sample per pixel */
    BOOLEAN Oversampled;
    /*
     * ClearType state GETINFO reports: 0x01 ClearType, 0x02 compatible widths, 0x04 symmetric smoothing,
     * 0x08 BGR, 0x10 subpixel positioned, 0x20 symmetric rendering, 0x80 gray ClearType
     */
    UINT32 ClearTypeFlags;
};

constexpr UINT32 RxClearTypeEnabled = 0x01;
constexpr UINT32 RxClearTypeCompatibleWidths = 0x02;
constexpr UINT32 RxClearTypeSubpixelPositioned = 0x10;
constexpr UINT32 RxClearTypeSymmetric = 0x20;
constexpr UINT32 RxClearTypeGray = 0x80;

class RxTrueTypeHinter
{
public:
    RxTrueTypeHinter();
    ~RxTrueTypeHinter();

    /* Runs the font program; a face whose fpgm fails is drawn unhinted */
    HRESULT
    Initialize(
        _In_ RxFaceTables* Face);

    /* Scales the control values and runs the prep program for a size */
    HRESULT
    SetSize(
        _In_ const RxHintRequest& Request);

    /* Hints a glyph; Points receives the outline plus the four phantom points */
    HRESULT
    HintGlyph(
        _In_ USHORT Glyph,
        _Inout_ std::vector<RxHintPoint>& Points,
        _Inout_ std::vector<USHORT>& ContourEnds);

    const RxDropoutControl&
    Dropout(VOID) const
    {
        return GlyphDropout;
    }

    BOOLEAN
    IsUsable(VOID) const
    {
        return FontReady && SizeReady;
    }

private:
    struct Vector
    {
        INT32 X;
        INT32 Y;
    };

    struct GraphicsState
    {
        Vector Projection;
        Vector Freedom;
        Vector Dual;
        UINT32 RoundState;
        RxF26Dot6 Period;
        RxF26Dot6 Phase;
        RxF26Dot6 Threshold;
        BOOLEAN AutoFlip;
        RxF26Dot6 CvtCutIn;
        RxF26Dot6 SingleWidthCutIn;
        RxF26Dot6 SingleWidth;
        INT32 DeltaBase;
        INT32 DeltaShift;
        RxF26Dot6 MinimumDistance;
        INT32 Loop;
        UINT32 Rp0;
        UINT32 Rp1;
        UINT32 Rp2;
        UINT32 Zp0;
        UINT32 Zp1;
        UINT32 Zp2;
        UINT32 InstructControl;
        UINT32 ScanControl;
        UINT32 ScanType;
    };

    /* Zone 0 is the twilight zone, zone 1 the glyph */
    struct Zone
    {
        std::vector<RxF26Dot6> CurrentX;
        std::vector<RxF26Dot6> CurrentY;
        std::vector<RxF26Dot6> OriginalX;
        std::vector<RxF26Dot6> OriginalY;
        /* Font unit positions, for IUP and IP ratios */
        std::vector<INT32> UnscaledX;
        std::vector<INT32> UnscaledY;
        std::vector<BYTE> Flags;
        std::vector<USHORT> ContourEnds;

        UINT32
        Count(VOID) const
        {
            return static_cast<UINT32>(CurrentX.size());
        }

        VOID
        Resize(
            _In_ UINT32 Size);
    };

    struct Definition
    {
        const BYTE* Start;
        UINT32 Length;
        UINT32 Opcode;
        BOOLEAN Active;
    };

    struct Program
    {
        const BYTE* Code;
        UINT32 Length;
        UINT32 Ip;
    };

    enum class Context
    {
        Font,
        Prep,
        Glyph
    };

    HRESULT
    Execute(
        _In_reads_bytes_(Length) const BYTE* Code,
        _In_ UINT32 Length,
        _In_ Context Where);

    HRESULT
    Run(
        _Inout_ Program& Current,
        _In_ ULONG Depth);

    HRESULT
    Step(
        _Inout_ Program& Current,
        _In_ BYTE Opcode,
        _In_ ULONG Depth);

    HRESULT
    HintComposite(
        _In_ USHORT Glyph,
        _In_ ULONG Depth,
        _Inout_ std::vector<RxHintPoint>& Points,
        _Inout_ std::vector<USHORT>& ContourEnds,
        _Out_ RxF26Dot6* PhantomX);

    HRESULT
    HintSimple(
        _In_ USHORT Glyph,
        _In_ const RxGlyphOutline& Outline,
        _Inout_ std::vector<RxHintPoint>& Points,
        _Inout_ std::vector<USHORT>& ContourEnds);

    HRESULT
    RunGlyphProgram(
        _In_ const RxSpan& Instructions,
        _In_ UINT32 FirstPoint,
        _In_reads_(Count) const INT32* UnscaledX,
        _In_reads_(Count) const INT32* UnscaledY,
        _In_ UINT32 Count,
        _Inout_ std::vector<RxHintPoint>& Points,
        _In_ const std::vector<USHORT>& ContourEnds);

    /* Stack */
    BOOLEAN Push(_In_ INT32 Value);
    BOOLEAN Pop(_Out_ INT32* Value);

    /* Geometry */
    RxF26Dot6 Project(_In_ RxF26Dot6 Dx, _In_ RxF26Dot6 Dy) const;
    RxF26Dot6 DualProject(_In_ RxF26Dot6 Dx, _In_ RxF26Dot6 Dy) const;
    RxF26Dot6 Round(_In_ RxF26Dot6 Distance) const;
    RxF26Dot6 RoundWith(_In_ UINT32 State, _In_ RxF26Dot6 Distance) const;
    VOID SetSuperRound(_In_ INT32 Selector, _In_ RxF26Dot6 GridPeriod);
    BOOLEAN ValidPoint(_In_ UINT32 ZoneIndex, _In_ UINT32 Point) const;
    VOID MovePoint(_In_ UINT32 ZoneIndex, _In_ UINT32 Point, _In_ RxF26Dot6 Distance, _In_ BOOLEAN Touch);
    VOID MoveOriginal(_In_ UINT32 ZoneIndex, _In_ UINT32 Point, _In_ RxF26Dot6 Distance);
    RxF26Dot6 CurrentProjection(_In_ UINT32 ZoneIndex, _In_ UINT32 Point) const;
    RxF26Dot6 OriginalProjection(_In_ UINT32 ZoneIndex, _In_ UINT32 Point) const;
    BOOLEAN UnitVector(_In_ RxF26Dot6 Dx, _In_ RxF26Dot6 Dy, _Out_ Vector* Result) const;
    VOID UpdateProjectionDot(VOID);
    RxF26Dot6 ScaledCvt(_In_ UINT32 Index) const;
    RxF26Dot6 CurrentPpem(VOID) const;
    BOOLEAN Compatibility(VOID) const;

    /* Instruction groups */
    HRESULT DoShift(_In_ BYTE Opcode);
    HRESULT DoDelta(_In_ BYTE Opcode);
    HRESULT DoMoveDirect(_In_ BYTE Opcode);
    HRESULT DoMoveIndirect(_In_ BYTE Opcode);
    HRESULT DoInterpolateUntouched(_In_ BOOLEAN XAxis);
    HRESULT DoInterpolatePoints(VOID);
    HRESULT DoIntersect(VOID);
    HRESULT DoGetInfo(VOID);
    BOOLEAN SkipBranch(_Inout_ Program& Current, _In_ BOOLEAN StopAtElse);
    BOOLEAN SkipPush(_Inout_ Program& Current, _In_ BYTE Opcode);

    RxComPtr<IDWriteFontFile> KeepFile;
    RxFaceTables* Face;
    BOOLEAN FontReady;
    BOOLEAN SizeReady;

    std::vector<INT32> Stack;
    UINT32 StackTop;
    std::vector<INT32> Storage;
    std::vector<INT32> OriginalCvt;
    std::vector<RxF26Dot6> Cvt;
    std::vector<RxF26Dot6> PrepCvt;
    std::vector<INT32> PrepStorage;
    std::vector<Definition> Functions;
    std::vector<Definition> Instructions;

    GraphicsState State;
    GraphicsState DefaultState;
    Zone Zones[2];
    INT32 ProjectionDot;
    RxHintRequest Size;
    Context Where;
    BOOLEAN NativeClearType;
    BOOLEAN DidIupX;
    BOOLEAN DidIupY;
    ULONG InstructionBudget;
    RxDropoutControl GlyphDropout;
    std::vector<BYTE> FontProgram;
    std::vector<BYTE> PrepProgram;
};
