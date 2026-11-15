/*
 * PROJECT:     ReactX Direct2D
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Matrices and the tessellation of filled shapes into coverage triangles
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d2dobjects.hpp"

#include <cmath>

D2D1_MATRIX_3X2_F
RxMultiply(
    _In_ const D2D1_MATRIX_3X2_F& A,
    _In_ const D2D1_MATRIX_3X2_F& B)
{
    D2D1_MATRIX_3X2_F Result;

    Result._11 = A._11 * B._11 + A._12 * B._21;
    Result._12 = A._11 * B._12 + A._12 * B._22;
    Result._21 = A._21 * B._11 + A._22 * B._21;
    Result._22 = A._21 * B._12 + A._22 * B._22;
    Result._31 = A._31 * B._11 + A._32 * B._21 + B._31;
    Result._32 = A._31 * B._12 + A._32 * B._22 + B._32;
    return Result;
}

D2D1_POINT_2F
RxTransformPoint(
    _In_ const D2D1_MATRIX_3X2_F& M,
    _In_ D2D1_POINT_2F P)
{
    return { P.x * M._11 + P.y * M._21 + M._31, P.x * M._12 + P.y * M._22 + M._32 };
}

VOID
RxInvertMatrix(
    _In_ const D2D1_MATRIX_3X2_F& M,
    _Out_ D2D1_MATRIX_3X2_F* Inverse,
    _Out_ BOOLEAN* Valid)
{
    double Determinant = static_cast<double>(M._11) * M._22 - static_cast<double>(M._12) * M._21;

    *Valid = Determinant != 0.0 && std::isfinite(Determinant);
    if (!*Valid)
    {
        *Inverse = { 1, 0, 0, 1, 0, 0 };
        return;
    }

    Inverse->_11 = static_cast<FLOAT>(M._22 / Determinant);
    Inverse->_12 = static_cast<FLOAT>(-M._12 / Determinant);
    Inverse->_21 = static_cast<FLOAT>(-M._21 / Determinant);
    Inverse->_22 = static_cast<FLOAT>(M._11 / Determinant);
    Inverse->_31 = static_cast<FLOAT>((M._21 * M._32 - M._22 * M._31) / Determinant);
    Inverse->_32 = static_cast<FLOAT>((M._12 * M._31 - M._11 * M._32) / Determinant);
}

namespace
{

/*
 * A frame where one unit is one device pixel along the shape's own axes.
 * Similarity transforms keep the antialiasing ramp one pixel wide this way.
 */
struct RxFrame
{
    FLOAT Scale;
    /* Local pixel units to device: rotation and translation */
    D2D1_MATRIX_3X2_F Place;
};

RxFrame
RxMakeFrame(
    _In_ const D2D1_MATRIX_3X2_F& M)
{
    RxFrame Frame;
    BOOLEAN Similar = M._11 == M._22 && M._12 == -M._21;
    FLOAT Scale = std::sqrt(M._11 * M._11 + M._21 * M._21);

    if (Similar && Scale > 0.0f)
    {
        Frame.Scale = Scale;
        Frame.Place = { M._11 / Scale, M._12 / Scale, M._21 / Scale, M._22 / Scale, M._31, M._32 };
    }
    else
    {
        /* Anything else is drawn in its own units; the ramp is then only roughly one pixel */
        Frame.Scale = 1.0f;
        Frame.Place = M;
    }
    return Frame;
}

VOID
RxEmit(
    _Inout_ std::vector<RxVertex>& Out,
    _In_ const RxFrame& Frame,
    _In_ FLOAT X,
    _In_ FLOAT Y,
    _In_ FLOAT Coverage)
{
    D2D1_POINT_2F Device = RxTransformPoint(Frame.Place, { X, Y });

    Out.push_back({ Device.x, Device.y, Coverage });
}

VOID
RxEmitTriangle(
    _Inout_ std::vector<RxVertex>& Out,
    _In_ const RxFrame& Frame,
    _In_ const FLOAT* A,
    _In_ const FLOAT* B,
    _In_ const FLOAT* C)
{
    RxEmit(Out, Frame, A[0], A[1], A[2]);
    RxEmit(Out, Frame, B[0], B[1], B[2]);
    RxEmit(Out, Frame, C[0], C[1], C[2]);
}

}

VOID
RxTessellateRect(
    _In_ const D2D1_RECT_F& Rect,
    _In_ const D2D1_MATRIX_3X2_F& Device,
    _In_ BOOLEAN Antialias,
    _Inout_ std::vector<RxVertex>& Out)
{
    FLOAT Left = std::min(Rect.left, Rect.right);
    FLOAT Right = std::max(Rect.left, Rect.right);
    FLOAT Top = std::min(Rect.top, Rect.bottom);
    FLOAT Bottom = std::max(Rect.top, Rect.bottom);
    RxFrame Frame;
    FLOAT Xs[4];
    FLOAT Ys[4];
    FLOAT Px;
    FLOAT Py;

    if (!(Right > Left) || !(Bottom > Top))
        return;

    if (!Antialias)
    {
        RxFrame Plain = { 1.0f, Device };
        FLOAT A[3] = { Left, Top, 1.0f };
        FLOAT B[3] = { Right, Top, 1.0f };
        FLOAT C[3] = { Right, Bottom, 1.0f };
        FLOAT D[3] = { Left, Bottom, 1.0f };

        RxEmitTriangle(Out, Plain, A, B, C);
        RxEmitTriangle(Out, Plain, A, C, D);
        return;
    }

    Frame = RxMakeFrame(Device);
    Left *= Frame.Scale;
    Right *= Frame.Scale;
    Top *= Frame.Scale;
    Bottom *= Frame.Scale;

    /* Pixel aligned rectangles need no ramps */
    if (Device._12 == 0.0f && Device._21 == 0.0f)
    {
        D2D1_POINT_2F A = RxTransformPoint(Frame.Place, { Left, Top });
        D2D1_POINT_2F C = RxTransformPoint(Frame.Place, { Right, Bottom });

        if (A.x == std::floor(A.x) && A.y == std::floor(A.y) && C.x == std::floor(C.x) && C.y == std::floor(C.y))
        {
            FLOAT P0[3] = { Left, Top, 1.0f };
            FLOAT P1[3] = { Right, Top, 1.0f };
            FLOAT P2[3] = { Right, Bottom, 1.0f };
            FLOAT P3[3] = { Left, Bottom, 1.0f };

            RxEmitTriangle(Out, Frame, P0, P1, P2);
            RxEmitTriangle(Out, Frame, P0, P2, P3);
            return;
        }
    }

    /* Ramps run from half a pixel outside each edge to half a pixel inside; thin shapes peak lower */
    {
        FLOAT Width = Right - Left;
        FLOAT Height = Bottom - Top;

        Xs[0] = Left - 0.5f;
        Xs[3] = Right + 0.5f;
        Ys[0] = Top - 0.5f;
        Ys[3] = Bottom + 0.5f;

        if (Width < 1.0f)
        {
            Xs[1] = Xs[0] + Width;
            Xs[2] = Xs[3] - Width;
            Px = std::floor(Width * 256.0f) / 256.0f;
        }
        else
        {
            Xs[1] = Left + 0.5f;
            Xs[2] = Right - 0.5f;
            Px = 1.0f;
        }

        if (Height < 1.0f)
        {
            Ys[1] = Ys[0] + Height;
            Ys[2] = Ys[3] - Height;
            Py = std::floor(Height * 256.0f) / 256.0f;
        }
        else
        {
            Ys[1] = Top + 0.5f;
            Ys[2] = Bottom - 0.5f;
            Py = 1.0f;
        }
    }

    /* Nine cells: the corners fan around their center, where coverage is a quarter of the peak */
    for (UINT32 Row = 0; Row < 3; Row++)
    {
        for (UINT32 Column = 0; Column < 3; Column++)
        {
            FLOAT Cx[2] = { Column == 0 ? 0.0f : Px, Column == 2 ? 0.0f : Px };
            FLOAT Cy[2] = { Row == 0 ? 0.0f : Py, Row == 2 ? 0.0f : Py };
            FLOAT A[3] = { Xs[Column], Ys[Row], Cx[0] * Cy[0] };
            FLOAT B[3] = { Xs[Column + 1], Ys[Row], Cx[1] * Cy[0] };
            FLOAT C[3] = { Xs[Column + 1], Ys[Row + 1], Cx[1] * Cy[1] };
            FLOAT D[3] = { Xs[Column], Ys[Row + 1], Cx[0] * Cy[1] };

            if (Xs[Column + 1] <= Xs[Column] || Ys[Row + 1] <= Ys[Row])
                continue;

            if (Row != 1 && Column != 1)
            {
                FLOAT Center[3] = { (Xs[Column] + Xs[Column + 1]) / 2.0f, (Ys[Row] + Ys[Row + 1]) / 2.0f,
                                    (A[2] + B[2] + C[2] + D[2]) / 4.0f };

                RxEmitTriangle(Out, Frame, Center, A, B);
                RxEmitTriangle(Out, Frame, Center, B, C);
                RxEmitTriangle(Out, Frame, Center, C, D);
                RxEmitTriangle(Out, Frame, Center, D, A);
            }
            else
            {
                RxEmitTriangle(Out, Frame, A, B, C);
                RxEmitTriangle(Out, Frame, A, C, D);
            }
        }
    }
}

/*
 * Rounded outlines: a fan from the center over the outline inset by half a pixel, then a one pixel strip
 * pushed outward along each segment's normal, going from full coverage to none.
 */
static
VOID
RxTessellateRounded(
    _In_ FLOAT Left,
    _In_ FLOAT Top,
    _In_ FLOAT Right,
    _In_ FLOAT Bottom,
    _In_ FLOAT RadiusX,
    _In_ FLOAT RadiusY,
    _In_ const RxFrame& Frame,
    _In_ BOOLEAN Antialias,
    _Inout_ std::vector<RxVertex>& Out)
{
    std::vector<D2D1_POINT_2F> Outline;
    FLOAT CenterX;
    FLOAT CenterY;
    const double Pi = 3.14159265358979323846;

    if (Antialias)
    {
        Left += 0.5f;
        Top += 0.5f;
        Right -= 0.5f;
        Bottom -= 0.5f;
        RadiusX = std::fabs(RadiusX - 0.5f);
        RadiusY = std::fabs(RadiusY - 0.5f);
    }
    if (Right < Left || Bottom < Top)
    {
        Right = Left = (Left + Right) / 2.0f;
        Bottom = Top = (Top + Bottom) / 2.0f;
    }
    RadiusX = std::min(RadiusX, (Right - Left) / 2.0f);
    RadiusY = std::min(RadiusY, (Bottom - Top) / 2.0f);
    CenterX = (Left + Right) / 2.0f;
    CenterY = (Top + Bottom) / 2.0f;

    /* Four quarter arcs, clockwise from the top right, flattened to a quarter pixel */
    {
        FLOAT Radius = std::max(RadiusX, RadiusY);
        UINT32 Steps = 1;

        if (Radius > 0.25f)
        {
            double Step = 2.0 * std::acos(std::max(-1.0, 1.0 - 0.25 / Radius));

            Steps = static_cast<UINT32>(std::ceil((Pi / 2.0) / std::max(Step, 1e-3)));
            Steps = std::max<UINT32>(1, std::min<UINT32>(Steps, 64));
        }

        const FLOAT Centers[4][2] =
        {
            { Right - RadiusX, Top + RadiusY },
            { Right - RadiusX, Bottom - RadiusY },
            { Left + RadiusX, Bottom - RadiusY },
            { Left + RadiusX, Top + RadiusY },
        };

        for (UINT32 Corner = 0; Corner < 4; Corner++)
        {
            double Start = -Pi / 2.0 + Corner * (Pi / 2.0);

            for (UINT32 Step = 0; Step <= Steps; Step++)
            {
                double Angle = Start + (Pi / 2.0) * Step / Steps;
                D2D1_POINT_2F Point = { static_cast<FLOAT>(Centers[Corner][0] + RadiusX * std::cos(Angle)),
                                        static_cast<FLOAT>(Centers[Corner][1] + RadiusY * std::sin(Angle)) };

                if (Outline.empty() || Outline.back().x != Point.x || Outline.back().y != Point.y)
                    Outline.push_back(Point);
            }
        }
        if (Outline.size() > 1 && Outline.front().x == Outline.back().x && Outline.front().y == Outline.back().y)
            Outline.pop_back();
    }

    for (SIZE_T Index = 0; Index < Outline.size(); Index++)
    {
        const D2D1_POINT_2F& A = Outline[Index];
        const D2D1_POINT_2F& B = Outline[(Index + 1) % Outline.size()];
        FLOAT Center[3] = { CenterX, CenterY, 1.0f };
        FLOAT Pa[3] = { A.x, A.y, 1.0f };
        FLOAT Pb[3] = { B.x, B.y, 1.0f };

        RxEmitTriangle(Out, Frame, Center, Pa, Pb);

        if (Antialias)
        {
            FLOAT Dx = B.x - A.x;
            FLOAT Dy = B.y - A.y;
            FLOAT Length = std::sqrt(Dx * Dx + Dy * Dy);
            FLOAT Nx;
            FLOAT Ny;

            if (Length <= 0.0f)
                continue;
            /* Outline runs clockwise on screen, so the outward normal is to the left of travel */
            Nx = -Dy / Length;
            Ny = Dx / Length;

            {
                FLOAT Oa[3] = { A.x - Nx, A.y - Ny, 0.0f };
                FLOAT Ob[3] = { B.x - Nx, B.y - Ny, 0.0f };

                RxEmitTriangle(Out, Frame, Pa, Pb, Ob);
                RxEmitTriangle(Out, Frame, Pa, Ob, Oa);
            }

            /* Fill the wedge where this strip meets the next one */
            {
                const D2D1_POINT_2F& C = Outline[(Index + 2) % Outline.size()];
                FLOAT Ex = C.x - B.x;
                FLOAT Ey = C.y - B.y;
                FLOAT Next = std::sqrt(Ex * Ex + Ey * Ey);

                if (Next > 0.0f)
                {
                    FLOAT Mx = -Ey / Next;
                    FLOAT My = Ex / Next;
                    FLOAT Ob[3] = { B.x - Nx, B.y - Ny, 0.0f };
                    FLOAT Oc[3] = { B.x - Mx, B.y - My, 0.0f };

                    RxEmitTriangle(Out, Frame, Pb, Ob, Oc);
                }
            }
        }
    }
}

VOID
RxTessellateRoundedRect(
    _In_ const D2D1_ROUNDED_RECT& Rounded,
    _In_ const D2D1_MATRIX_3X2_F& Device,
    _In_ BOOLEAN Antialias,
    _Inout_ std::vector<RxVertex>& Out)
{
    const D2D1_RECT_F& Rect = Rounded.rect;
    RxFrame Frame = RxMakeFrame(Device);
    FLOAT Left = std::min(Rect.left, Rect.right) * Frame.Scale;
    FLOAT Right = std::max(Rect.left, Rect.right) * Frame.Scale;
    FLOAT Top = std::min(Rect.top, Rect.bottom) * Frame.Scale;
    FLOAT Bottom = std::max(Rect.top, Rect.bottom) * Frame.Scale;
    FLOAT RadiusX = std::fabs(Rounded.radiusX) * Frame.Scale;
    FLOAT RadiusY = std::fabs(Rounded.radiusY) * Frame.Scale;

    if (!(Right > Left) || !(Bottom > Top))
        return;

    /* Square corners are plain rectangles */
    if (RadiusX <= 0.0f || RadiusY <= 0.0f)
    {
        RxTessellateRect(Rect, Device, Antialias, Out);
        return;
    }

    /* The fast path averages equal radii */
    if (std::fabs(Rounded.radiusX - Rounded.radiusY) < 0.0005f)
        RadiusX = RadiusY = (RadiusX + RadiusY) / 2.0f;

    RxTessellateRounded(Left, Top, Right, Bottom, RadiusX, RadiusY, Frame, Antialias, Out);
}

VOID
RxTessellateEllipse(
    _In_ const D2D1_ELLIPSE& Ellipse,
    _In_ const D2D1_MATRIX_3X2_F& Device,
    _In_ BOOLEAN Antialias,
    _Inout_ std::vector<RxVertex>& Out)
{
    RxFrame Frame = RxMakeFrame(Device);
    FLOAT RadiusX = std::fabs(Ellipse.radiusX) * Frame.Scale;
    FLOAT RadiusY = std::fabs(Ellipse.radiusY) * Frame.Scale;
    FLOAT X = Ellipse.point.x * Frame.Scale;
    FLOAT Y = Ellipse.point.y * Frame.Scale;

    if (!(RadiusX > 0.0f) || !(RadiusY > 0.0f))
        return;
    RxTessellateRounded(X - RadiusX, Y - RadiusY, X + RadiusX, Y + RadiusY, RadiusX, RadiusY, Frame, Antialias, Out);
}
