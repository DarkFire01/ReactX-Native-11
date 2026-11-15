/*
 * PROJECT:     ReactX Direct2D
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device context: drawing state, targets, clips and drawing
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d2dobjects.hpp"

#include <cmath>

static
FLOAT
RxClampUnit(
    _In_ FLOAT Value)
{
    return Value < 0.0f ? 0.0f : (Value > 1.0f ? 1.0f : Value);
}

HRESULT
RxD2DContext::Create(
    _In_ RxD2DDevice* Owner,
    _In_ D2D1_DEVICE_CONTEXT_OPTIONS ContextOptions,
    _Outptr_ RxD2DContext** Context)
{
    *Context = new (std::nothrow) RxD2DContext();
    if (*Context == nullptr)
        return E_OUTOFMEMORY;
    (*Context)->Device = RxComPtr<RxD2DDevice>(Owner);
    (*Context)->Options = ContextOptions;
    return S_OK;
}

RxD2DContext::RxD2DContext() :
    RefCount(1), Options(D2D1_DEVICE_CONTEXT_OPTIONS_NONE), Transform{ 1, 0, 0, 1, 0, 0 },
    Antialias(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE), TextAntialias(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT),
    Blend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER), Units(D2D1_UNIT_MODE_DIPS), DpiX(96.0f), DpiY(96.0f), Tag1(0), Tag2(0),
    Drawing(FALSE), Rendering(FALSE), Pending(S_OK), PendingTag1(0), PendingTag2(0)
{
}

RxD2DContext::~RxD2DContext()
{
    if (Rendering)
    {
        Device->Renderer().End();
        ReleaseSRWLockExclusive(&Device->Renderer().Lock);
    }
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Resource) || riid == __uuidof(ID2D1RenderTarget) ||
        riid == __uuidof(ID2D1DeviceContext))
    {
        *ppvObject = static_cast<ID2D1DeviceContext*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxD2DContext)

/* State */

VOID
RxD2DContext::SetError(
    _In_ HRESULT Error)
{
    if (SUCCEEDED(Pending))
    {
        Pending = Error;
        PendingTag1 = Tag1;
        PendingTag2 = Tag2;
    }
}

BOOLEAN
RxD2DContext::CanDraw(VOID)
{
    if (FAILED(Pending))
        return FALSE;
    if (!Drawing || !Target.Get())
    {
        SetError(RxErrorWrongState);
        return FALSE;
    }

    if (!Rendering)
    {
        ID3D11RenderTargetView* View = Target->TargetView();
        D2D1_SIZE_U Size = Target->GetPixelSize();

        if (View == nullptr)
        {
            SetError(RxErrorInvalidTarget);
            return FALSE;
        }
        AcquireSRWLockExclusive(&Device->Renderer().Lock);
        Device->Renderer().Begin(View, Size.width, Size.height);
        Rendering = TRUE;
        ApplyClip();
    }
    return TRUE;
}

D2D1_MATRIX_3X2_F
RxD2DContext::DeviceTransform(VOID) const
{
    FLOAT ScaleX = Units == D2D1_UNIT_MODE_PIXELS ? 1.0f : DpiX / 96.0f;
    FLOAT ScaleY = Units == D2D1_UNIT_MODE_PIXELS ? 1.0f : DpiY / 96.0f;
    D2D1_MATRIX_3X2_F Result = Transform;

    Result._11 *= ScaleX;
    Result._21 *= ScaleX;
    Result._31 *= ScaleX;
    Result._12 *= ScaleY;
    Result._22 *= ScaleY;
    Result._32 *= ScaleY;
    return Result;
}

VOID
RxD2DContext::ApplyClip(VOID)
{
    RECT Full = { 0, 0, 0, 0 };

    if (!Rendering)
        return;
    if (Target.Get())
    {
        D2D1_SIZE_U Size = Target->GetPixelSize();

        Full.right = static_cast<LONG>(Size.width);
        Full.bottom = static_cast<LONG>(Size.height);
    }
    Device->Renderer().SetClip(Clips.empty() ? Full : Clips.back().Device);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::GetFactory(
    _Outptr_ ID2D1Factory** factory) const
{
    *factory = Device->Factory();
    (*factory)->AddRef();
}

VOID
STDMETHODCALLTYPE
RxD2DContext::GetDevice(
    _Outptr_ ID2D1Device** device) const
{
    *device = Device.Copy();
}

VOID
STDMETHODCALLTYPE
RxD2DContext::SetTransform(
    _In_ CONST D2D1_MATRIX_3X2_F* transform)
{
    Transform = *transform;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::GetTransform(
    _Out_ D2D1_MATRIX_3X2_F* transform) const
{
    *transform = Transform;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::SetAntialiasMode(
    D2D1_ANTIALIAS_MODE antialiasMode)
{
    if (static_cast<UINT32>(antialiasMode) <= D2D1_ANTIALIAS_MODE_ALIASED)
        Antialias = antialiasMode;
}

D2D1_ANTIALIAS_MODE
STDMETHODCALLTYPE
RxD2DContext::GetAntialiasMode(VOID) const
{
    return Antialias;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::SetTextAntialiasMode(
    D2D1_TEXT_ANTIALIAS_MODE textAntialiasMode)
{
    if (static_cast<UINT32>(textAntialiasMode) <= D2D1_TEXT_ANTIALIAS_MODE_ALIASED)
        TextAntialias = textAntialiasMode;
}

D2D1_TEXT_ANTIALIAS_MODE
STDMETHODCALLTYPE
RxD2DContext::GetTextAntialiasMode(VOID) const
{
    return TextAntialias;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::SetTextRenderingParams(
    _In_opt_ IDWriteRenderingParams* textRenderingParams)
{
    TextParams = RxComPtr<IDWriteRenderingParams>(textRenderingParams);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::GetTextRenderingParams(
    _Outptr_result_maybenull_ IDWriteRenderingParams** textRenderingParams) const
{
    *textRenderingParams = TextParams.Copy();
}

VOID
STDMETHODCALLTYPE
RxD2DContext::SetTags(
    D2D1_TAG tag1,
    D2D1_TAG tag2)
{
    Tag1 = tag1;
    Tag2 = tag2;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::GetTags(
    _Out_opt_ D2D1_TAG* tag1,
    _Out_opt_ D2D1_TAG* tag2) const
{
    if (tag1 != nullptr)
        *tag1 = Tag1;
    if (tag2 != nullptr)
        *tag2 = Tag2;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::SetPrimitiveBlend(
    D2D1_PRIMITIVE_BLEND primitiveBlend)
{
    if (static_cast<UINT32>(primitiveBlend) <= D2D1_PRIMITIVE_BLEND_MAX)
        Blend = primitiveBlend;
}

D2D1_PRIMITIVE_BLEND
STDMETHODCALLTYPE
RxD2DContext::GetPrimitiveBlend(VOID) const
{
    return Blend;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::SetUnitMode(
    D2D1_UNIT_MODE unitMode)
{
    if (static_cast<UINT32>(unitMode) <= D2D1_UNIT_MODE_PIXELS)
        Units = unitMode;
}

D2D1_UNIT_MODE
STDMETHODCALLTYPE
RxD2DContext::GetUnitMode(VOID) const
{
    return Units;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::SetDpi(
    FLOAT dpiX,
    FLOAT dpiY)
{
    if (dpiX == 0.0f && dpiY == 0.0f)
    {
        Device->Factory()->GetDesktopDpi(&DpiX, &DpiY);
        return;
    }
    if (dpiX > 0.0f && dpiY > 0.0f)
    {
        DpiX = dpiX;
        DpiY = dpiY;
    }
}

VOID
STDMETHODCALLTYPE
RxD2DContext::GetDpi(
    _Out_ FLOAT* dpiX,
    _Out_ FLOAT* dpiY) const
{
    *dpiX = DpiX;
    *dpiY = DpiY;
}

D2D1_PIXEL_FORMAT
STDMETHODCALLTYPE
RxD2DContext::GetPixelFormat(VOID) const
{
    if (Target.Get())
        return Target->GetPixelFormat();
    return { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
}

D2D1_SIZE_F
STDMETHODCALLTYPE
RxD2DContext::GetSize(VOID) const
{
    D2D1_SIZE_U Pixels = GetPixelSize();

    return { Pixels.width * 96.0f / DpiX, Pixels.height * 96.0f / DpiY };
}

D2D1_SIZE_U
STDMETHODCALLTYPE
RxD2DContext::GetPixelSize(VOID) const
{
    if (Target.Get())
        return Target->GetPixelSize();
    return { 0, 0 };
}

UINT32
STDMETHODCALLTYPE
RxD2DContext::GetMaximumBitmapSize(VOID) const
{
    return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
}

BOOL
STDMETHODCALLTYPE
RxD2DContext::IsSupported(
    _In_ CONST D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties) const
{
    if (renderTargetProperties == nullptr)
        return FALSE;
    return renderTargetProperties->pixelFormat.format == DXGI_FORMAT_UNKNOWN ||
           renderTargetProperties->pixelFormat.format == DXGI_FORMAT_B8G8R8A8_UNORM ||
           renderTargetProperties->pixelFormat.format == DXGI_FORMAT_R8G8B8A8_UNORM;
}

BOOL
STDMETHODCALLTYPE
RxD2DContext::IsDxgiFormatSupported(
    DXGI_FORMAT format) const
{
    UINT Support = 0;

    if (FAILED(Device->Renderer().Device()->CheckFormatSupport(format, &Support)))
        return FALSE;
    return (Support & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0;
}

BOOL
STDMETHODCALLTYPE
RxD2DContext::IsBufferPrecisionSupported(
    D2D1_BUFFER_PRECISION bufferPrecision) const
{
    return bufferPrecision == D2D1_BUFFER_PRECISION_8BPC_UNORM ||
           bufferPrecision == D2D1_BUFFER_PRECISION_8BPC_UNORM_SRGB;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::SaveDrawingState(
    _Inout_ ID2D1DrawingStateBlock* drawingStateBlock) const
{
    RX_UNIMPLEMENTED();
}

VOID
STDMETHODCALLTYPE
RxD2DContext::RestoreDrawingState(
    _In_ ID2D1DrawingStateBlock* drawingStateBlock)
{
    RX_UNIMPLEMENTED();
}

/* Targets and frames */

VOID
STDMETHODCALLTYPE
RxD2DContext::SetTarget(
    _In_opt_ ID2D1Image* image)
{
    RxD2DBitmap* Bitmap = RxD2DBitmap::FromInterface(image);

    if (image != nullptr)
    {
        if (Bitmap == nullptr)
        {
            SetError(RxErrorInvalidTarget);
            return;
        }
        if (Bitmap->Owner() != Device.Get())
        {
            SetError(RxErrorWrongDomain);
            return;
        }
        if (!(Bitmap->GetOptions() & D2D1_BITMAP_OPTIONS_TARGET))
        {
            SetError(RxErrorInvalidTarget);
            return;
        }
    }

    /* Drawing continues on the new target from the next draw */
    if (Rendering)
    {
        Device->Renderer().End();
        ReleaseSRWLockExclusive(&Device->Renderer().Lock);
        Rendering = FALSE;
    }
    Target = RxComPtr<RxD2DBitmap>(Bitmap);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::GetTarget(
    _Outptr_result_maybenull_ ID2D1Image** image) const
{
    *image = Target.Get() != nullptr ? static_cast<ID2D1Image*>(Target.Copy()) : nullptr;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::BeginDraw(VOID)
{
    if (Drawing)
    {
        SetError(RxErrorWrongState);
        return;
    }

    /* An error from outside the frame costs that whole frame */
    if (FAILED(Pending))
        return;

    Drawing = TRUE;
}

static
HRESULT
RxMapDeviceError(
    _In_ HRESULT Error)
{
    if (Error == DXGI_ERROR_INVALID_CALL || Error == DXGI_ERROR_DEVICE_REMOVED || Error == DXGI_ERROR_DEVICE_HUNG ||
        Error == DXGI_ERROR_DEVICE_RESET || Error == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
        return RxErrorRecreateTarget;
    return Error;
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::Flush(
    _Out_opt_ D2D1_TAG* tag1,
    _Out_opt_ D2D1_TAG* tag2)
{
    HRESULT Result;

    if (!Drawing)
        SetError(RxErrorWrongState);
    else if (Rendering)
        Device->Renderer().Context()->Flush();

    Result = RxMapDeviceError(Pending);
    if (tag1 != nullptr)
        *tag1 = FAILED(Result) ? PendingTag1 : 0;
    if (tag2 != nullptr)
        *tag2 = FAILED(Result) ? PendingTag2 : 0;
    Pending = S_OK;
    return Result;
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::EndDraw(
    _Out_opt_ D2D1_TAG* tag1,
    _Out_opt_ D2D1_TAG* tag2)
{
    HRESULT Result;

    if (!Drawing)
    {
        SetError(RxErrorWrongState);
    }
    else
    {
        if (Rendering)
        {
            Device->Renderer().End();
            ReleaseSRWLockExclusive(&Device->Renderer().Lock);
            Rendering = FALSE;
        }
        if (SUCCEEDED(Pending) && !Clips.empty())
            SetError(RxErrorUnbalanced);
        Clips.clear();
        Drawing = FALSE;
    }

    Result = RxMapDeviceError(Pending);
    if (tag1 != nullptr)
        *tag1 = FAILED(Result) ? PendingTag1 : 0;
    if (tag2 != nullptr)
        *tag2 = FAILED(Result) ? PendingTag2 : 0;
    Pending = S_OK;
    return Result;
}

/* Clips */

VOID
STDMETHODCALLTYPE
RxD2DContext::PushAxisAlignedClip(
    _In_ CONST D2D1_RECT_F* clipRect,
    D2D1_ANTIALIAS_MODE antialiasMode)
{
    D2D1_MATRIX_3X2_F Matrix = DeviceTransform();
    D2D1_POINT_2F Corners[4];
    FLOAT Left;
    FLOAT Top;
    FLOAT Right;
    FLOAT Bottom;
    Clip Entry;
    RECT Parent;
    D2D1_SIZE_U Size;

    if (!CanDraw())
        return;

    Corners[0] = RxTransformPoint(Matrix, { clipRect->left, clipRect->top });
    Corners[1] = RxTransformPoint(Matrix, { clipRect->right, clipRect->top });
    Corners[2] = RxTransformPoint(Matrix, { clipRect->right, clipRect->bottom });
    Corners[3] = RxTransformPoint(Matrix, { clipRect->left, clipRect->bottom });
    Left = Right = Corners[0].x;
    Top = Bottom = Corners[0].y;
    for (const auto& Corner : Corners)
    {
        Left = std::min(Left, Corner.x);
        Right = std::max(Right, Corner.x);
        Top = std::min(Top, Corner.y);
        Bottom = std::max(Bottom, Corner.y);
    }

    /* Aliased edges keep pixels whose centers are inside; antialiased ones round outward */
    auto Saturate = [](double Value) { return static_cast<LONG>(std::min(2147483647.0, std::max(-2147483648.0, Value))); };
    if (antialiasMode == D2D1_ANTIALIAS_MODE_ALIASED)
    {
        Entry.Device = { Saturate(std::ceil(Left - 0.5)), Saturate(std::ceil(Top - 0.5)), Saturate(std::ceil(Right - 0.5)),
                         Saturate(std::ceil(Bottom - 0.5)) };
    }
    else
    {
        Entry.Device = { Saturate(std::floor(Left)), Saturate(std::floor(Top)), Saturate(std::ceil(Right)),
                         Saturate(std::ceil(Bottom)) };
    }

    Size = Target->GetPixelSize();
    Parent = Clips.empty() ? RECT{ 0, 0, static_cast<LONG>(Size.width), static_cast<LONG>(Size.height) } : Clips.back().Device;
    Entry.Device.left = std::max(Entry.Device.left, Parent.left);
    Entry.Device.top = std::max(Entry.Device.top, Parent.top);
    Entry.Device.right = std::min(Entry.Device.right, Parent.right);
    Entry.Device.bottom = std::min(Entry.Device.bottom, Parent.bottom);
    if (Entry.Device.right < Entry.Device.left)
        Entry.Device.right = Entry.Device.left;
    if (Entry.Device.bottom < Entry.Device.top)
        Entry.Device.bottom = Entry.Device.top;

    Clips.push_back(Entry);
    ApplyClip();
}

VOID
STDMETHODCALLTYPE
RxD2DContext::PopAxisAlignedClip(VOID)
{
    if (!CanDraw())
        return;
    if (Clips.empty())
    {
        SetError(RxErrorUnbalanced);
        return;
    }
    Clips.pop_back();
    ApplyClip();
}

/* Drawing */

BOOLEAN
RxD2DContext::PaintFor(
    _In_ ID2D1Brush* Brush,
    _Out_ RxPaint* Paint)
{
    RxSolidBrush* Solid = RxSolidBrush::FromInterface(Brush);
    RxLinearBrush* Linear;

    *Paint = RxPaint{};
    Paint->Opacity = 1.0f;

    if (Solid != nullptr)
    {
        const D2D1_COLOR_F& Color = Solid->Value();
        FLOAT Alpha = RxClampUnit(Color.a) * RxClampUnit(Solid->State().Opacity);

        Paint->Color[0] = RxClampUnit(Color.r) * Alpha;
        Paint->Color[1] = RxClampUnit(Color.g) * Alpha;
        Paint->Color[2] = RxClampUnit(Color.b) * Alpha;
        Paint->Color[3] = Alpha;
        return TRUE;
    }

    Linear = RxLinearBrush::FromInterface(Brush);
    if (Linear != nullptr && Linear->Collection() != nullptr)
    {
        D2D1_MATRIX_3X2_F Matrix = RxMultiply(Linear->State().Transform, DeviceTransform());
        D2D1_POINT_2F Start = Linear->Start();
        D2D1_POINT_2F End = Linear->End();
        D2D1_POINT_2F S = RxTransformPoint(Matrix, Start);
        D2D1_POINT_2F E = RxTransformPoint(Matrix, End);
        D2D1_POINT_2F Q = RxTransformPoint(Matrix, { Start.x - (End.y - Start.y), Start.y + (End.x - Start.x) });
        D2D1_EXTEND_MODE Extend = Linear->Collection()->Extend();
        double Nx = -(Q.y - S.y);
        double Ny = Q.x - S.x;
        double Normal = std::sqrt(Nx * Nx + Ny * Ny);
        double Dx;
        double Dy;
        double LengthSquared;
        double Length;
        UINT32 Width;
        UINT32 Count;
        double Offset;
        double Span;

        /* Keep the gradient axis perpendicular to its iso lines under skew */
        if (Normal > 0.0)
        {
            double Along;

            Nx /= Normal;
            Ny /= Normal;
            Along = Nx * (E.x - S.x) + Ny * (E.y - S.y);
            E = { static_cast<FLOAT>(S.x + Nx * Along), static_cast<FLOAT>(S.y + Ny * Along) };
        }

        Dx = E.x - S.x;
        Dy = E.y - S.y;
        LengthSquared = Dx * Dx + Dy * Dy;
        Length = std::sqrt(LengthSquared);

        Count = static_cast<UINT32>(std::floor(std::min(std::max(std::isfinite(Length) ? Length : 0.0, 0.0), 1024.0)));
        if (Extend == D2D1_EXTEND_MODE_CLAMP)
            Count += 2;
        else
            Count = std::max<UINT32>(Count, 1);
        for (Width = 1; Width < Count && Width < 1024; Width <<= 1)
            ;

        Paint->Texture = Linear->Collection()->Realize(Width);
        if (Paint->Texture == nullptr)
            return FALSE;

        Offset = Extend == D2D1_EXTEND_MODE_CLAMP ? 1.0 : 0.0;
        Span = Extend == D2D1_EXTEND_MODE_CLAMP ? Width - 2.0 : Width;
        if (Extend == D2D1_EXTEND_MODE_CLAMP && Width == 1)
            Span = 1.0;

        if (LengthSquared > 0.0 && std::isfinite(LengthSquared))
        {
            double A = Span * Dx / LengthSquared / Width;
            double B = Span * Dy / LengthSquared / Width;

            Paint->Uv[0] = static_cast<FLOAT>(A);
            Paint->Uv[1] = static_cast<FLOAT>(B);
            Paint->Uv[2] = static_cast<FLOAT>(Offset / Width - A * S.x - B * S.y);
        }
        Paint->Uv[5] = 0.5f;
        Paint->Opacity = RxClampUnit(Linear->State().Opacity);
        Paint->Sampler = Extend == D2D1_EXTEND_MODE_WRAP ? RxSamplerLinearWrap :
                         (Extend == D2D1_EXTEND_MODE_MIRROR ? RxSamplerLinearMirror : RxSamplerLinearClamp);
        return TRUE;
    }

    RX_UNIMPLEMENTED();
    return FALSE;
}

VOID
RxD2DContext::FillTriangles(
    _In_ const std::vector<RxVertex>& Vertices,
    _In_ const RxPaint& Paint)
{
    if (!Vertices.empty())
        Device->Renderer().Draw(Vertices.data(), static_cast<UINT32>(Vertices.size()), Paint,
                                Blend == D2D1_PRIMITIVE_BLEND_COPY);
}

VOID
RxD2DContext::FillRect(
    _In_ const D2D1_RECT_F& Rect,
    _In_ const RxPaint& Paint)
{
    std::vector<RxVertex> Vertices;

    RxTessellateRect(Rect, DeviceTransform(), Antialias == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, Vertices);
    FillTriangles(Vertices, Paint);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::Clear(
    _In_opt_ CONST D2D1_COLOR_F* clearColor)
{
    D2D1_COLOR_F Color = clearColor != nullptr ? *clearColor : D2D1_COLOR_F{ 0.0f, 0.0f, 0.0f, 0.0f };
    FLOAT Alpha;
    FLOAT Premultiplied[4];
    D2D1_SIZE_U Size;

    if (!CanDraw())
        return;

    Alpha = RxClampUnit(Color.a);
    if (Target->GetPixelFormat().alphaMode == D2D1_ALPHA_MODE_IGNORE)
        Alpha = 1.0f;
    Premultiplied[0] = RxClampUnit(Color.r) * Alpha;
    Premultiplied[1] = RxClampUnit(Color.g) * Alpha;
    Premultiplied[2] = RxClampUnit(Color.b) * Alpha;
    Premultiplied[3] = Alpha;

    Size = Target->GetPixelSize();
    if (Clips.empty() || (Clips.back().Device.left <= 0 && Clips.back().Device.top <= 0 &&
                          Clips.back().Device.right >= static_cast<LONG>(Size.width) &&
                          Clips.back().Device.bottom >= static_cast<LONG>(Size.height)))
    {
        Device->Renderer().ClearTarget(Premultiplied);
        return;
    }

    /* Inside a clip the color replaces what is there, whatever the blend */
    {
        const RECT& Box = Clips.back().Device;
        FLOAT L = static_cast<FLOAT>(Box.left);
        FLOAT T = static_cast<FLOAT>(Box.top);
        FLOAT R = static_cast<FLOAT>(Box.right);
        FLOAT B = static_cast<FLOAT>(Box.bottom);
        RxVertex Quad[6] = { { L, T, 1.0f }, { R, T, 1.0f }, { R, B, 1.0f }, { L, T, 1.0f }, { R, B, 1.0f }, { L, B, 1.0f } };
        RxPaint Paint = {};

        if (Box.right <= Box.left || Box.bottom <= Box.top)
            return;
        memcpy(Paint.Color, Premultiplied, sizeof(Paint.Color));
        Device->Renderer().Draw(Quad, _countof(Quad), Paint, TRUE);
    }
}

VOID
STDMETHODCALLTYPE
RxD2DContext::FillRectangle(
    _In_ CONST D2D1_RECT_F* rect,
    _In_ ID2D1Brush* brush)
{
    RxPaint Paint;

    if (!CanDraw() || rect == nullptr || !PaintFor(brush, &Paint))
        return;
    FillRect(*rect, Paint);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::FillRoundedRectangle(
    _In_ CONST D2D1_ROUNDED_RECT* roundedRect,
    _In_ ID2D1Brush* brush)
{
    std::vector<RxVertex> Vertices;
    RxPaint Paint;

    if (!CanDraw() || roundedRect == nullptr || !PaintFor(brush, &Paint))
        return;
    RxTessellateRoundedRect(*roundedRect, DeviceTransform(), Antialias == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, Vertices);
    FillTriangles(Vertices, Paint);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::FillEllipse(
    _In_ CONST D2D1_ELLIPSE* ellipse,
    _In_ ID2D1Brush* brush)
{
    std::vector<RxVertex> Vertices;
    RxPaint Paint;

    if (!CanDraw() || ellipse == nullptr || !PaintFor(brush, &Paint))
        return;
    RxTessellateEllipse(*ellipse, DeviceTransform(), Antialias == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, Vertices);
    FillTriangles(Vertices, Paint);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::DrawLine(
    D2D1_POINT_2F point0,
    D2D1_POINT_2F point1,
    _In_ ID2D1Brush* brush,
    FLOAT strokeWidth,
    _In_opt_ ID2D1StrokeStyle* strokeStyle)
{
    std::vector<RxVertex> Vertices;
    RxPaint Paint;
    FLOAT Dx = point1.x - point0.x;
    FLOAT Dy = point1.y - point0.y;
    FLOAT Length = std::sqrt(Dx * Dx + Dy * Dy);
    D2D1_MATRIX_3X2_F Along;

    if (!CanDraw() || !PaintFor(brush, &Paint) || Length <= 0.0f)
        return;

    /* A line is a rectangle laid along it, flat caps */
    Along = { Dx / Length, Dy / Length, -Dy / Length, Dx / Length, point0.x, point0.y };
    RxTessellateRect({ 0.0f, -strokeWidth / 2.0f, Length, strokeWidth / 2.0f }, RxMultiply(Along, DeviceTransform()),
                     Antialias == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, Vertices);
    FillTriangles(Vertices, Paint);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::DrawRectangle(
    _In_ CONST D2D1_RECT_F* rect,
    _In_ ID2D1Brush* brush,
    FLOAT strokeWidth,
    _In_opt_ ID2D1StrokeStyle* strokeStyle)
{
    RxPaint Paint;
    FLOAT Half = strokeWidth / 2.0f;
    FLOAT L;
    FLOAT T;
    FLOAT R;
    FLOAT B;

    if (!CanDraw() || rect == nullptr || !PaintFor(brush, &Paint))
        return;

    L = std::min(rect->left, rect->right);
    R = std::max(rect->left, rect->right);
    T = std::min(rect->top, rect->bottom);
    B = std::max(rect->top, rect->bottom);

    /* Four sides centered on the outline, mitered corners filled by the top and bottom bands */
    FillRect({ L - Half, T - Half, R + Half, T + Half }, Paint);
    FillRect({ L - Half, B - Half, R + Half, B + Half }, Paint);
    FillRect({ L - Half, T + Half, L + Half, B - Half }, Paint);
    FillRect({ R - Half, T + Half, R + Half, B - Half }, Paint);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::DrawRoundedRectangle(
    _In_ CONST D2D1_ROUNDED_RECT* roundedRect,
    _In_ ID2D1Brush* brush,
    FLOAT strokeWidth,
    _In_opt_ ID2D1StrokeStyle* strokeStyle)
{
    RX_UNIMPLEMENTED();
    if (roundedRect != nullptr)
        DrawRectangle(&roundedRect->rect, brush, strokeWidth, strokeStyle);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::DrawEllipse(
    _In_ CONST D2D1_ELLIPSE* ellipse,
    _In_ ID2D1Brush* brush,
    FLOAT strokeWidth,
    _In_opt_ ID2D1StrokeStyle* strokeStyle)
{
    RX_UNIMPLEMENTED();
}

/* Bitmaps */

static
VOID
RxDrawBitmapCore(
    _In_ RxD2DContext* Context,
    _In_ RxD2DBitmap* Bitmap,
    _In_opt_ const D2D1_RECT_F* Destination,
    _In_ FLOAT Opacity,
    _In_ BOOLEAN Linear,
    _In_opt_ const D2D1_RECT_F* Source)
{
    D2D1_SIZE_F Dips = Bitmap->GetSize();
    D2D1_RECT_F From = { 0.0f, 0.0f, Dips.width, Dips.height };
    D2D1_RECT_F To;
    D2D1_MATRIX_3X2_F Place;
    D2D1_MATRIX_3X2_F Inverse;
    BOOLEAN Valid;
    RxPaint Paint = {};
    FLOAT SourceWidth;
    FLOAT SourceHeight;
    FLOAT ScaleX;
    FLOAT ScaleY;

    if (Source != nullptr)
    {
        From.left = std::max(0.0f, std::min(Source->left, Source->right));
        From.top = std::max(0.0f, std::min(Source->top, Source->bottom));
        From.right = std::min(Dips.width, std::max(Source->left, Source->right));
        From.bottom = std::min(Dips.height, std::max(Source->top, Source->bottom));
    }
    SourceWidth = std::max(0.0f, From.right - From.left);
    SourceHeight = std::max(0.0f, From.bottom - From.top);
    if (SourceWidth <= 0.0f || SourceHeight <= 0.0f)
        return;

    if (Destination != nullptr)
        To = { std::min(Destination->left, Destination->right), std::min(Destination->top, Destination->bottom),
               std::max(Destination->left, Destination->right), std::max(Destination->top, Destination->bottom) };
    else
        To = { 0.0f, 0.0f, SourceWidth, SourceHeight };

    /* Bitmap DIPs to destination, then to device; its inverse finds the texel under each pixel */
    ScaleX = (To.right - To.left) / SourceWidth;
    ScaleY = (To.bottom - To.top) / SourceHeight;
    Place = { ScaleX, 0.0f, 0.0f, ScaleY, To.left - From.left * ScaleX, To.top - From.top * ScaleY };
    Place = RxMultiply(Place, Context->DeviceTransform());
    RxInvertMatrix(Place, &Inverse, &Valid);
    if (!Valid)
        return;

    Paint.Texture = Bitmap->View();
    Paint.Sampler = Linear ? RxSamplerLinearClamp : RxSamplerPointClamp;
    Paint.Opacity = RxClampUnit(Opacity);
    Paint.Uv[0] = Inverse._11 / Dips.width;
    Paint.Uv[1] = Inverse._21 / Dips.width;
    Paint.Uv[2] = Inverse._31 / Dips.width;
    Paint.Uv[3] = Inverse._12 / Dips.height;
    Paint.Uv[4] = Inverse._22 / Dips.height;
    Paint.Uv[5] = Inverse._32 / Dips.height;
    Context->FillRect(To, Paint);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::DrawBitmap(
    _In_ ID2D1Bitmap* bitmap,
    _In_opt_ CONST D2D1_RECT_F* destinationRectangle,
    FLOAT opacity,
    D2D1_BITMAP_INTERPOLATION_MODE interpolationMode,
    _In_opt_ CONST D2D1_RECT_F* sourceRectangle)
{
    RxD2DBitmap* Bitmap = RxD2DBitmap::FromInterface(bitmap);

    if (!CanDraw())
        return;
    if (static_cast<UINT32>(interpolationMode) > D2D1_BITMAP_INTERPOLATION_MODE_LINEAR || Bitmap == nullptr)
    {
        SetError(E_INVALIDARG);
        return;
    }
    if (Bitmap->Owner() != Device.Get())
    {
        SetError(RxErrorWrongDomain);
        return;
    }
    if (Bitmap->View() == nullptr)
    {
        SetError(RxErrorCannotDraw);
        return;
    }
    RxDrawBitmapCore(this, Bitmap, destinationRectangle, opacity,
                     interpolationMode == D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, sourceRectangle);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::DrawBitmap(
    _In_ ID2D1Bitmap* bitmap,
    _In_opt_ CONST D2D1_RECT_F* destinationRectangle,
    FLOAT opacity,
    D2D1_INTERPOLATION_MODE interpolationMode,
    _In_opt_ CONST D2D1_RECT_F* sourceRectangle,
    _In_opt_ CONST D2D1_MATRIX_4X4_F* perspectiveTransform)
{
    RxD2DBitmap* Bitmap = RxD2DBitmap::FromInterface(bitmap);

    if (!CanDraw())
        return;
    if (static_cast<UINT32>(interpolationMode) > D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC || Bitmap == nullptr)
    {
        SetError(E_INVALIDARG);
        return;
    }
    if (Bitmap->Owner() != Device.Get())
    {
        SetError(RxErrorWrongDomain);
        return;
    }
    if (Bitmap->View() == nullptr)
    {
        SetError(RxErrorCannotDraw);
        return;
    }
    if (perspectiveTransform != nullptr)
        RX_UNIMPLEMENTED();

    /* Filters past linear sample as linear */
    RxDrawBitmapCore(this, Bitmap, destinationRectangle, opacity,
                     interpolationMode != D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, sourceRectangle);
}

/* Text */

VOID
STDMETHODCALLTYPE
RxD2DContext::DrawGlyphRun(
    D2D1_POINT_2F baselineOrigin,
    _In_ CONST DWRITE_GLYPH_RUN* glyphRun,
    _In_ ID2D1Brush* foregroundBrush,
    DWRITE_MEASURING_MODE measuringMode)
{
    DrawGlyphRun(baselineOrigin, glyphRun, nullptr, foregroundBrush, measuringMode);
}

VOID
STDMETHODCALLTYPE
RxD2DContext::DrawGlyphRun(
    D2D1_POINT_2F baselineOrigin,
    _In_ CONST DWRITE_GLYPH_RUN* glyphRun,
    _In_opt_ CONST DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription,
    _In_ ID2D1Brush* foregroundBrush,
    DWRITE_MEASURING_MODE measuringMode)
{
    RxSolidBrush* Solid = RxSolidBrush::FromInterface(foregroundBrush);
    IDWriteFactory2* Write = Device->Factory()->Write();
    RxPaint Paint;

    if (!CanDraw() || glyphRun == nullptr || !PaintFor(foregroundBrush, &Paint) || Write == nullptr)
        return;

    if (!TextParams.Get())
        Write->CreateRenderingParams(TextParams.Put());
    if (!TextParams.Get())
        return;

    RxDrawGlyphRun(this, Device->Renderer(), Write, Transform, Units == D2D1_UNIT_MODE_PIXELS ? 96.0f : DpiX,
                   Units == D2D1_UNIT_MODE_PIXELS ? 96.0f : DpiY, baselineOrigin, glyphRun, Paint, Solid != nullptr,
                   Solid != nullptr ? Solid->Value() : D2D1_COLOR_F{ 0, 0, 0, 1 },
                   Solid != nullptr ? Solid->State().Opacity : 1.0f, measuringMode, TextAntialias, TextParams.Get());
}

VOID
STDMETHODCALLTYPE
RxD2DContext::DrawTextLayout(
    D2D1_POINT_2F origin,
    _In_ IDWriteTextLayout* textLayout,
    _In_ ID2D1Brush* defaultFillBrush,
    D2D1_DRAW_TEXT_OPTIONS options)
{
    D2D1_ANTIALIAS_MODE Saved = Antialias;
    BOOLEAN Clipped = FALSE;

    if (textLayout == nullptr || defaultFillBrush == nullptr || (options & ~0xF))
        return;
    if (!CanDraw())
        return;

    if (options & D2D1_DRAW_TEXT_OPTIONS_CLIP)
    {
        DWRITE_TEXT_METRICS Metrics;

        if (SUCCEEDED(textLayout->GetMetrics(&Metrics)))
        {
            D2D1_RECT_F Box = { origin.x, origin.y, origin.x + Metrics.layoutWidth, origin.y + Metrics.layoutHeight };

            PushAxisAlignedClip(&Box, Antialias);
            Clipped = TRUE;
        }
    }

    {
        RxTextRenderer Renderer(this, defaultFillBrush, options, DpiX, DpiY, Units == D2D1_UNIT_MODE_PIXELS);

        textLayout->Draw(nullptr, &Renderer, origin.x, origin.y);
    }

    if (Clipped)
        PopAxisAlignedClip();
    Antialias = Saved;
}

VOID
STDMETHODCALLTYPE
RxD2DContext::DrawText(
    _In_reads_(stringLength) CONST WCHAR* string,
    UINT32 stringLength,
    _In_ IDWriteTextFormat* textFormat,
    _In_ CONST D2D1_RECT_F* layoutRect,
    _In_ ID2D1Brush* defaultFillBrush,
    D2D1_DRAW_TEXT_OPTIONS options,
    DWRITE_MEASURING_MODE measuringMode)
{
    IDWriteFactory2* Write = Device->Factory()->Write();
    RxComPtr<IDWriteTextLayout> Layout;
    HRESULT hr;

    if (string == nullptr || textFormat == nullptr || layoutRect == nullptr || Write == nullptr)
        return;

    if (measuringMode == DWRITE_MEASURING_MODE_NATURAL)
        hr = Write->CreateTextLayout(string, stringLength, textFormat, std::max(0.0f, layoutRect->right - layoutRect->left),
                                     std::max(0.0f, layoutRect->bottom - layoutRect->top), Layout.Put());
    else
        hr = Write->CreateGdiCompatibleTextLayout(string, stringLength, textFormat,
                                                  std::max(0.0f, layoutRect->right - layoutRect->left),
                                                  std::max(0.0f, layoutRect->bottom - layoutRect->top), DpiY / 96.0f,
                                                  nullptr, measuringMode == DWRITE_MEASURING_MODE_GDI_NATURAL,
                                                  Layout.Put());
    if (FAILED(hr))
    {
        SetError(hr);
        return;
    }
    DrawTextLayout({ layoutRect->left, layoutRect->top }, Layout.Get(), defaultFillBrush, options);
}

/* Resources */

HRESULT
STDMETHODCALLTYPE
RxD2DContext::CreateSolidColorBrush(
    _In_ CONST D2D1_COLOR_F* color,
    _In_opt_ CONST D2D1_BRUSH_PROPERTIES* brushProperties,
    _COM_Outptr_ ID2D1SolidColorBrush** solidColorBrush)
{
    RxSolidBrush* Brush;
    HRESULT hr;

    if (solidColorBrush == nullptr)
        return E_INVALIDARG;
    *solidColorBrush = nullptr;
    if (color == nullptr)
        return E_INVALIDARG;

    hr = RxSolidBrush::Create(Device->Factory(), *color, brushProperties, &Brush);
    if (FAILED(hr))
        return hr;
    *solidColorBrush = Brush;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::CreateGradientStopCollection(
    _In_reads_(gradientStopsCount) CONST D2D1_GRADIENT_STOP* gradientStops,
    _In_range_(>=,1) UINT32 gradientStopsCount,
    D2D1_GAMMA colorInterpolationGamma,
    D2D1_EXTEND_MODE extendMode,
    _COM_Outptr_ ID2D1GradientStopCollection** gradientStopCollection)
{
    RxGradientStops* Collection;
    HRESULT hr;

    if (gradientStopCollection == nullptr)
        return E_INVALIDARG;
    *gradientStopCollection = nullptr;

    hr = RxGradientStops::Create(Device.Get(), gradientStops, gradientStopsCount, colorInterpolationGamma, extendMode,
                                 &Collection);
    if (FAILED(hr))
        return hr;
    *gradientStopCollection = Collection;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::CreateGradientStopCollection(
    _In_reads_(straightAlphaGradientStopsCount) CONST D2D1_GRADIENT_STOP* straightAlphaGradientStops,
    _In_range_(>=,1) UINT32 straightAlphaGradientStopsCount,
    D2D1_COLOR_SPACE preInterpolationSpace,
    D2D1_COLOR_SPACE postInterpolationSpace,
    D2D1_BUFFER_PRECISION bufferPrecision,
    D2D1_EXTEND_MODE extendMode,
    D2D1_COLOR_INTERPOLATION_MODE colorInterpolationMode,
    _COM_Outptr_ ID2D1GradientStopCollection1** gradientStopCollection1)
{
    RxGradientStops* Collection;
    HRESULT hr;

    if (gradientStopCollection1 == nullptr)
        return E_INVALIDARG;
    *gradientStopCollection1 = nullptr;
    if (preInterpolationSpace == D2D1_COLOR_SPACE_CUSTOM || postInterpolationSpace == D2D1_COLOR_SPACE_CUSTOM)
        return E_INVALIDARG;

    /* Interpolating in scRGB is what gamma 1.0 does */
    hr = RxGradientStops::Create(Device.Get(), straightAlphaGradientStops, straightAlphaGradientStopsCount,
                                 preInterpolationSpace == D2D1_COLOR_SPACE_SCRGB ? D2D1_GAMMA_1_0 : D2D1_GAMMA_2_2,
                                 extendMode, &Collection);
    if (FAILED(hr))
        return hr;
    *gradientStopCollection1 = Collection;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::CreateLinearGradientBrush(
    _In_ CONST D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES* linearGradientBrushProperties,
    _In_opt_ CONST D2D1_BRUSH_PROPERTIES* brushProperties,
    _In_ ID2D1GradientStopCollection* gradientStopCollection,
    _COM_Outptr_ ID2D1LinearGradientBrush** linearGradientBrush)
{
    RxGradientStops* Stops = RxGradientStops::FromInterface(gradientStopCollection);
    RxLinearBrush* Brush;
    HRESULT hr;

    if (linearGradientBrush == nullptr)
        return E_INVALIDARG;
    *linearGradientBrush = nullptr;
    if (linearGradientBrushProperties == nullptr || Stops == nullptr)
        return E_INVALIDARG;

    hr = RxLinearBrush::Create(Device->Factory(), *linearGradientBrushProperties, brushProperties, Stops, &Brush);
    if (FAILED(hr))
        return hr;
    *linearGradientBrush = Brush;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::CreateBitmap(
    D2D1_SIZE_U size,
    _In_opt_ CONST void* srcData,
    UINT32 pitch,
    _In_ CONST D2D1_BITMAP_PROPERTIES* bitmapProperties,
    _COM_Outptr_ ID2D1Bitmap** bitmap)
{
    D2D1_BITMAP_PROPERTIES1 Properties = {};
    RxComPtr<ID2D1Bitmap1> Result;
    HRESULT hr;

    if (bitmap == nullptr)
        return E_INVALIDARG;
    *bitmap = nullptr;
    if (bitmapProperties == nullptr)
        return E_INVALIDARG;

    Properties.pixelFormat = bitmapProperties->pixelFormat;
    Properties.dpiX = bitmapProperties->dpiX;
    Properties.dpiY = bitmapProperties->dpiY;
    hr = CreateBitmap(size, srcData, pitch, &Properties, Result.Put());
    if (FAILED(hr))
        return hr;
    *bitmap = Result.Copy();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::CreateBitmap(
    D2D1_SIZE_U size,
    _In_opt_ CONST void* sourceData,
    UINT32 pitch,
    _In_ CONST D2D1_BITMAP_PROPERTIES1* bitmapProperties,
    _COM_Outptr_ ID2D1Bitmap1** bitmap)
{
    D2D1_BITMAP_PROPERTIES1 Properties;
    D3D11_TEXTURE2D_DESC Desc = {};
    D3D11_SUBRESOURCE_DATA Data = { sourceData, pitch, 0 };
    RxComPtr<ID3D11Texture2D> Texture;
    RxD2DBitmap* Result;
    HRESULT hr;

    if (bitmap == nullptr)
        return E_INVALIDARG;
    *bitmap = nullptr;
    if (bitmapProperties == nullptr || size.width == 0 || size.height == 0)
        return E_INVALIDARG;

    Properties = *bitmapProperties;
    if (Properties.bitmapOptions & ~0xF)
        return E_INVALIDARG;
    if ((Properties.bitmapOptions & D2D1_BITMAP_OPTIONS_TARGET) && (Properties.bitmapOptions & D2D1_BITMAP_OPTIONS_CPU_READ))
        return E_INVALIDARG;
    if (Properties.pixelFormat.format == DXGI_FORMAT_UNKNOWN)
        Properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (Properties.pixelFormat.alphaMode == D2D1_ALPHA_MODE_UNKNOWN)
        Properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    if (Properties.dpiX == 0.0f && Properties.dpiY == 0.0f)
    {
        Properties.dpiX = DpiX;
        Properties.dpiY = DpiY;
    }
    if (!(Properties.dpiX > 0.0f) || !(Properties.dpiY > 0.0f))
        return E_INVALIDARG;

    Desc.Width = size.width;
    Desc.Height = size.height;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = Properties.pixelFormat.format;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    if (!(Properties.bitmapOptions & D2D1_BITMAP_OPTIONS_CANNOT_DRAW))
        Desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (Properties.bitmapOptions & D2D1_BITMAP_OPTIONS_TARGET)
        Desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (Properties.bitmapOptions & D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE)
        Desc.MiscFlags |= D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    if (Properties.bitmapOptions & D2D1_BITMAP_OPTIONS_CPU_READ)
    {
        Desc.Usage = D3D11_USAGE_STAGING;
        Desc.BindFlags = 0;
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    }

    hr = Device->Renderer().Device()->CreateTexture2D(&Desc, sourceData != nullptr ? &Data : nullptr, Texture.Put());
    if (FAILED(hr))
        return hr;

    hr = RxD2DBitmap::Create(Device.Get(), Texture.Get(), Properties, &Result);
    if (FAILED(hr))
        return hr;
    *bitmap = Result;
    return S_OK;
}

/* WIC formats a bitmap can be made from, with what they become */
static
BOOLEAN
RxWicFormat(
    _In_ REFGUID Format,
    _Out_ DXGI_FORMAT* Dxgi,
    _Out_ D2D1_ALPHA_MODE* Alpha)
{
    static const struct
    {
        const GUID* Wic;
        DXGI_FORMAT Dxgi;
        D2D1_ALPHA_MODE Alpha;
    } Table[] =
    {
        { &GUID_WICPixelFormat32bppPBGRA, DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED },
        { &GUID_WICPixelFormat32bppBGR, DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE },
        { &GUID_WICPixelFormat32bppBGRA, DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_STRAIGHT },
        { &GUID_WICPixelFormat32bppPRGBA, DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED },
        { &GUID_WICPixelFormat32bppRGB, DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE },
        { &GUID_WICPixelFormat32bppRGBA, DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_STRAIGHT },
        { &GUID_WICPixelFormat8bppAlpha, DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED },
    };

    for (const auto& Entry : Table)
    {
        if (IsEqualGUID(*Entry.Wic, Format))
        {
            *Dxgi = Entry.Dxgi;
            *Alpha = Entry.Alpha;
            return TRUE;
        }
    }
    return FALSE;
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::CreateBitmapFromWicBitmap(
    _In_ IWICBitmapSource* wicBitmapSource,
    _In_opt_ CONST D2D1_BITMAP_PROPERTIES* bitmapProperties,
    _COM_Outptr_ ID2D1Bitmap** bitmap)
{
    D2D1_BITMAP_PROPERTIES1 Properties = {};
    RxComPtr<ID2D1Bitmap1> Result;
    HRESULT hr;

    if (bitmap == nullptr)
        return E_INVALIDARG;
    *bitmap = nullptr;

    if (bitmapProperties != nullptr)
    {
        Properties.pixelFormat = bitmapProperties->pixelFormat;
        Properties.dpiX = bitmapProperties->dpiX;
        Properties.dpiY = bitmapProperties->dpiY;
    }
    hr = CreateBitmapFromWicBitmap(wicBitmapSource, bitmapProperties != nullptr ? &Properties : nullptr, Result.Put());
    if (FAILED(hr))
        return hr;
    *bitmap = Result.Copy();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::CreateBitmapFromWicBitmap(
    _In_ IWICBitmapSource* wicBitmapSource,
    _In_opt_ CONST D2D1_BITMAP_PROPERTIES1* bitmapProperties,
    _COM_Outptr_ ID2D1Bitmap1** bitmap)
{
    D2D1_BITMAP_PROPERTIES1 Properties = {};
    WICPixelFormatGUID Format;
    DXGI_FORMAT Dxgi;
    D2D1_ALPHA_MODE Alpha;
    UINT Width;
    UINT Height;
    UINT Stride;
    std::vector<BYTE> Pixels;
    HRESULT hr;

    if (bitmap == nullptr)
        return E_INVALIDARG;
    *bitmap = nullptr;
    if (wicBitmapSource == nullptr)
        return E_INVALIDARG;

    hr = wicBitmapSource->GetPixelFormat(&Format);
    if (FAILED(hr))
        return hr;
    if (!RxWicFormat(Format, &Dxgi, &Alpha))
        return RxErrorUnsupportedFormat;

    if (bitmapProperties != nullptr)
    {
        Properties = *bitmapProperties;
        if (Properties.pixelFormat.format != DXGI_FORMAT_UNKNOWN && Properties.pixelFormat.format != Dxgi)
            return E_INVALIDARG;
        if (Properties.pixelFormat.alphaMode != D2D1_ALPHA_MODE_UNKNOWN)
            Alpha = Properties.pixelFormat.alphaMode;
    }
    else
    {
        Properties.dpiX = Properties.dpiY = 96.0f;
    }

    /* Straight alpha only stands for alpha only bitmaps */
    if (Alpha == D2D1_ALPHA_MODE_STRAIGHT && Dxgi != DXGI_FORMAT_A8_UNORM)
        return RxErrorUnsupportedFormat;

    Properties.pixelFormat.format = Dxgi;
    Properties.pixelFormat.alphaMode = Alpha;

    hr = wicBitmapSource->GetSize(&Width, &Height);
    if (FAILED(hr))
        return hr;
    Stride = Width * (Dxgi == DXGI_FORMAT_A8_UNORM ? 1 : 4);
    Pixels.resize(static_cast<SIZE_T>(Stride) * Height);
    hr = wicBitmapSource->CopyPixels(nullptr, Stride, static_cast<UINT>(Pixels.size()), Pixels.data());
    if (FAILED(hr))
        return hr;

    return CreateBitmap({ Width, Height }, Pixels.data(), Stride, &Properties, bitmap);
}

HRESULT
STDMETHODCALLTYPE
RxD2DContext::CreateBitmapFromDxgiSurface(
    _In_ IDXGISurface* surface,
    _In_opt_ CONST D2D1_BITMAP_PROPERTIES1* bitmapProperties,
    _COM_Outptr_ ID2D1Bitmap1** bitmap)
{
    RxComPtr<ID3D11Texture2D> Texture;
    RxComPtr<ID3D11Device> Owner;
    D3D11_TEXTURE2D_DESC Desc;
    D2D1_BITMAP_PROPERTIES1 Properties = {};
    UINT32 Allowed = 0;
    RxD2DBitmap* Result;
    HRESULT hr;

    if (bitmap == nullptr)
        return E_INVALIDARG;
    *bitmap = nullptr;
    if (surface == nullptr)
        return E_INVALIDARG;

    hr = surface->QueryInterface(__uuidof(ID3D11Texture2D), Texture.PutVoid());
    if (FAILED(hr))
        return hr;
    Texture->GetDesc(&Desc);
    Texture->GetDevice(Owner.Put());
    if (Owner.Get() != static_cast<ID3D11Device*>(Device->Renderer().Device()))
        return D2DERR_UNSUPPORTED_OPERATION;
    if (Desc.Format == DXGI_FORMAT_UNKNOWN)
        return RxErrorUnsupportedFormat;

    /* Options the texture allows */
    if (Desc.BindFlags & D3D11_BIND_RENDER_TARGET)
        Allowed |= D2D1_BITMAP_OPTIONS_TARGET;
    if (!(Desc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
        Allowed |= D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    if ((Desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) && Desc.Usage != D3D11_USAGE_DEFAULT)
        Allowed |= D2D1_BITMAP_OPTIONS_CPU_READ;
    if (Desc.MiscFlags & D3D11_RESOURCE_MISC_GDI_COMPATIBLE)
        Allowed |= D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE;

    if (bitmapProperties == nullptr)
    {
        Properties.pixelFormat = { Desc.Format, D2D1_ALPHA_MODE_PREMULTIPLIED };
        Properties.dpiX = Properties.dpiY = 96.0f;
        Properties.bitmapOptions = static_cast<D2D1_BITMAP_OPTIONS>(Allowed);
    }
    else
    {
        Properties = *bitmapProperties;
        if (Properties.pixelFormat.format != DXGI_FORMAT_UNKNOWN && Properties.pixelFormat.format != Desc.Format)
            return E_INVALIDARG;
        if (Properties.pixelFormat.alphaMode == D2D1_ALPHA_MODE_UNKNOWN)
            return RxErrorUnsupportedFormat;
        if (((Properties.bitmapOptions ^ D2D1_BITMAP_OPTIONS_CANNOT_DRAW) & ~(Allowed ^ D2D1_BITMAP_OPTIONS_CANNOT_DRAW)) != 0)
            return E_INVALIDARG;
        Properties.pixelFormat.format = Desc.Format;
        if (Properties.dpiX == 0.0f && Properties.dpiY == 0.0f)
            Properties.dpiX = Properties.dpiY = 96.0f;
    }
    Properties.colorContext = nullptr;

    hr = RxD2DBitmap::Create(Device.Get(), Texture.Get(), Properties, &Result);
    if (FAILED(hr))
        return hr;
    *bitmap = Result;
    return S_OK;
}
