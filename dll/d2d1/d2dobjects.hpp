/*
 * PROJECT:     ReactX Direct2D
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct2D objects and the Direct3D renderer behind them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

/* Errors */
constexpr HRESULT RxErrorWrongState = static_cast<HRESULT>(0x88990001L);
constexpr HRESULT RxErrorRecreateTarget = static_cast<HRESULT>(0x8899000CL);
constexpr HRESULT RxErrorWrongDomain = static_cast<HRESULT>(0x88990012L);
constexpr HRESULT RxErrorPopMismatch = static_cast<HRESULT>(0x88990014L);
constexpr HRESULT RxErrorUnbalanced = static_cast<HRESULT>(0x88990016L);
constexpr HRESULT RxErrorCannotDraw = static_cast<HRESULT>(0x88990021L);
constexpr HRESULT RxErrorInvalidTarget = static_cast<HRESULT>(0x88990024L);
constexpr HRESULT RxErrorUnsupportedFormat = static_cast<HRESULT>(0x88982F80L);

class RxD2DFactory;
class RxD2DDevice;
class RxD2DBitmap;

/* A device space vertex; coverage is 0 to 1 */
struct RxVertex
{
    FLOAT X;
    FLOAT Y;
    FLOAT Coverage;
};

/* What a primitive is filled with */
struct RxPaint
{
    /* Premultiplied color for solid fills */
    FLOAT Color[4];
    /* Texture fills: device position to texture coordinates, rows are u then v */
    ID3D11ShaderResourceView* Texture;
    FLOAT Uv[6];
    FLOAT Opacity;
    UINT32 Sampler;
};

constexpr UINT32 RxSamplerLinearClamp = 0;
constexpr UINT32 RxSamplerPointClamp = 1;
constexpr UINT32 RxSamplerLinearWrap = 2;
constexpr UINT32 RxSamplerLinearMirror = 3;
constexpr UINT32 RxSamplerCount = 4;

/* Direct3D 11 objects one device draws with, used inside its own context state */
class RxRenderer
{
public:
    RxRenderer();
    ~RxRenderer();

    HRESULT
    Initialize(
        _In_ ID3D11Device1* Device);

    /* Swaps our state in and binds the target */
    HRESULT
    Begin(
        _In_ ID3D11RenderTargetView* Target,
        _In_ UINT32 Width,
        _In_ UINT32 Height);

    /* Swaps the application's state back */
    VOID
    End(VOID);

    VOID
    SetClip(
        _In_ const RECT& Clip);

    /* Triangles, three vertices each */
    VOID
    Draw(
        _In_reads_(Count) const RxVertex* Vertices,
        _In_ UINT32 Count,
        _In_ const RxPaint& Paint,
        _In_ BOOLEAN Copy);

    VOID
    ClearTarget(
        _In_reads_(4) const FLOAT* Color);

    /* A shader readable BGRA texture at least this big, its top left filled from Pixels */
    ID3D11ShaderResourceView*
    Upload(
        _In_reads_(Width * Height) const UINT32* Pixels,
        _In_ UINT32 Width,
        _In_ UINT32 Height,
        _Out_ UINT32* TextureWidth,
        _Out_ UINT32* TextureHeight);

    ID3D11Device1*
    Device(VOID) const
    {
        return D3DDevice.Get();
    }

    ID3D11DeviceContext1*
    Context(VOID) const
    {
        return D3DContext.Get();
    }

    SRWLOCK Lock;

private:
    RxComPtr<ID3D11Device1> D3DDevice;
    RxComPtr<ID3D11DeviceContext1> D3DContext;
    RxComPtr<ID3DDeviceContextState> OwnState;
    RxComPtr<ID3DDeviceContextState> SavedState;
    RxComPtr<ID3D11VertexShader> VertexShader;
    RxComPtr<ID3D11PixelShader> SolidShader;
    RxComPtr<ID3D11PixelShader> TextureShader;
    RxComPtr<ID3D11InputLayout> Layout;
    RxComPtr<ID3D11Buffer> Vertices;
    RxComPtr<ID3D11Buffer> VertexConstants;
    RxComPtr<ID3D11Buffer> PixelConstants;
    RxComPtr<ID3D11BlendState> SourceOver;
    RxComPtr<ID3D11BlendState> CopyBlend;
    RxComPtr<ID3D11RasterizerState> Rasterizer;
    RxComPtr<ID3D11SamplerState> Samplers[RxSamplerCount];
    RxComPtr<ID3D11Texture2D> Scratch;
    RxComPtr<ID3D11ShaderResourceView> ScratchView;
    UINT32 ScratchWidth;
    UINT32 ScratchHeight;
    UINT32 VertexCapacity;
    UINT32 VertexOffset;
    BOOLEAN Swapped;
};

class RxD2DFactory final : public ID2D1Factory1
{
public:
    static
    HRESULT
    Create(
        _In_ D2D1_FACTORY_TYPE Type,
        _Outptr_ RxD2DFactory** Factory);

    RX_DECLARE_IUNKNOWN

#include "factory.inl"

    /* The shared DirectWrite factory text draws through */
    IDWriteFactory2*
    Write(VOID);

private:
    RxD2DFactory() : RefCount(1), Type(D2D1_FACTORY_TYPE_SINGLE_THREADED), WriteReady(INIT_ONCE_STATIC_INIT) {}

    LONG RefCount;
    D2D1_FACTORY_TYPE Type;
    INIT_ONCE WriteReady;
};

class RxD2DDevice final : public ID2D1Device
{
public:
    static
    HRESULT
    Create(
        _In_ RxD2DFactory* Factory,
        _In_ IDXGIDevice* Dxgi,
        _Outptr_ RxD2DDevice** Device);

    RX_DECLARE_IUNKNOWN

#include "device.inl"

    RxRenderer&
    Renderer(VOID)
    {
        return Draw;
    }

    RxD2DFactory*
    Factory(VOID) const
    {
        return Owner.Get();
    }

private:
    RxD2DDevice() : RefCount(1), TextureMemory(0) {}

    LONG RefCount;
    RxComPtr<RxD2DFactory> Owner;
    RxRenderer Draw;
    UINT64 TextureMemory;
};

class RxD2DBitmap final : public ID2D1Bitmap1
{
public:
    static
    HRESULT
    Create(
        _In_ RxD2DDevice* Device,
        _In_ ID3D11Texture2D* Texture,
        _In_ const D2D1_BITMAP_PROPERTIES1& Properties,
        _Outptr_ RxD2DBitmap** Bitmap);

    static
    RxD2DBitmap*
    FromInterface(
        _In_opt_ IUnknown* Object);

    RX_DECLARE_IUNKNOWN

#include "bitmap.inl"

    RxD2DDevice*
    Owner(VOID) const
    {
        return OwnerDevice.Get();
    }

    ID3D11ShaderResourceView*
    View(VOID) const
    {
        return ShaderView.Get();
    }

    /* Created on first use */
    ID3D11RenderTargetView*
    TargetView(VOID);

private:
    RxD2DBitmap() : RefCount(1), Properties{}, Width(0), Height(0) {}

    LONG RefCount;
    RxComPtr<RxD2DDevice> OwnerDevice;
    RxComPtr<ID3D11Texture2D> Texture;
    RxComPtr<ID3D11ShaderResourceView> ShaderView;
    RxComPtr<ID3D11RenderTargetView> RenderView;
    D2D1_BITMAP_PROPERTIES1 Properties;
    UINT32 Width;
    UINT32 Height;
};

/* Opacity and transform every brush carries */
struct RxBrushState
{
    FLOAT Opacity;
    D2D1_MATRIX_3X2_F Transform;

    VOID
    Initialize(
        _In_opt_ const D2D1_BRUSH_PROPERTIES* Properties)
    {
        Opacity = Properties != nullptr ? Properties->opacity : 1.0f;
        if (Properties != nullptr)
            Transform = Properties->transform;
        else
            Transform = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    }
};

class RxSolidBrush final : public ID2D1SolidColorBrush
{
public:
    static
    HRESULT
    Create(
        _In_ RxD2DFactory* Factory,
        _In_ const D2D1_COLOR_F& Color,
        _In_opt_ const D2D1_BRUSH_PROPERTIES* Properties,
        _Outptr_ RxSolidBrush** Brush);

    static
    RxSolidBrush*
    FromInterface(
        _In_opt_ IUnknown* Object);

    RX_DECLARE_IUNKNOWN

#include "solidbrush.inl"

    const D2D1_COLOR_F&
    Value(VOID) const
    {
        return Color;
    }

    const RxBrushState&
    State(VOID) const
    {
        return Common;
    }

private:
    RxSolidBrush() : RefCount(1), Color{}, Common{} {}

    LONG RefCount;
    RxComPtr<RxD2DFactory> Owner;
    D2D1_COLOR_F Color;
    RxBrushState Common;
};

class RxGradientStops final : public ID2D1GradientStopCollection1
{
public:
    static
    HRESULT
    Create(
        _In_ RxD2DDevice* Device,
        _In_reads_(Count) const D2D1_GRADIENT_STOP* Stops,
        _In_ UINT32 Count,
        _In_ D2D1_GAMMA Gamma,
        _In_ D2D1_EXTEND_MODE Extend,
        _Outptr_ RxGradientStops** Collection);

    static
    RxGradientStops*
    FromInterface(
        _In_opt_ IUnknown* Object);

    RX_DECLARE_IUNKNOWN

#include "gradientstops.inl"

    /* The gradient realized at a texture width, built on first use */
    ID3D11ShaderResourceView*
    Realize(
        _In_ UINT32 Width);

    D2D1_EXTEND_MODE
    Extend(VOID) const
    {
        return ExtendMode;
    }

private:
    RxGradientStops() : RefCount(1), Gamma(D2D1_GAMMA_2_2), ExtendMode(D2D1_EXTEND_MODE_CLAMP) {}

    struct Level
    {
        UINT32 Width;
        RxComPtr<ID3D11ShaderResourceView> View;
    };

    LONG RefCount;
    RxComPtr<RxD2DDevice> Device;
    std::vector<D2D1_GRADIENT_STOP> Stops;
    D2D1_GAMMA Gamma;
    D2D1_EXTEND_MODE ExtendMode;
    std::vector<Level> Levels;
};

class RxLinearBrush final : public ID2D1LinearGradientBrush
{
public:
    static
    HRESULT
    Create(
        _In_ RxD2DFactory* Factory,
        _In_ const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& Line,
        _In_opt_ const D2D1_BRUSH_PROPERTIES* Properties,
        _In_ RxGradientStops* Stops,
        _Outptr_ RxLinearBrush** Brush);

    static
    RxLinearBrush*
    FromInterface(
        _In_opt_ IUnknown* Object);

    RX_DECLARE_IUNKNOWN

#include "linearbrush.inl"

    const D2D1_POINT_2F&
    Start(VOID) const
    {
        return StartPoint;
    }

    const D2D1_POINT_2F&
    End(VOID) const
    {
        return EndPoint;
    }

    RxGradientStops*
    Collection(VOID) const
    {
        return Stops.Get();
    }

    const RxBrushState&
    State(VOID) const
    {
        return Common;
    }

private:
    RxLinearBrush() : RefCount(1), StartPoint{}, EndPoint{}, Common{} {}

    LONG RefCount;
    RxComPtr<RxD2DFactory> Owner;
    RxComPtr<RxGradientStops> Stops;
    D2D1_POINT_2F StartPoint;
    D2D1_POINT_2F EndPoint;
    RxBrushState Common;
};

class RxD2DContext final : public ID2D1DeviceContext
{
public:
    static
    HRESULT
    Create(
        _In_ RxD2DDevice* Device,
        _In_ D2D1_DEVICE_CONTEXT_OPTIONS Options,
        _Outptr_ RxD2DContext** Context);

    RX_DECLARE_IUNKNOWN

#include "context.inl"

    /* World to device pixels, DPI included */
    D2D1_MATRIX_3X2_F
    DeviceTransform(VOID) const;

    /* Fill a rectangle in DIPs with the context's transform and antialiasing */
    VOID
    FillRect(
        _In_ const D2D1_RECT_F& Rect,
        _In_ const RxPaint& Paint);

private:
    RxD2DContext();
    ~RxD2DContext();

    /* Pending error bookkeeping; TRUE when drawing may go ahead */
    BOOLEAN
    CanDraw(VOID);

    VOID
    SetError(
        _In_ HRESULT Error);

    /* Brush to paint; FALSE for brushes we cannot draw */
    BOOLEAN
    PaintFor(
        _In_ ID2D1Brush* Brush,
        _Out_ RxPaint* Paint);

    VOID
    FillTriangles(
        _In_ const std::vector<RxVertex>& Vertices,
        _In_ const RxPaint& Paint);

    VOID
    ApplyClip(VOID);

    struct Clip
    {
        RECT Device;
    };

    LONG RefCount;
    RxComPtr<RxD2DDevice> Device;
    D2D1_DEVICE_CONTEXT_OPTIONS Options;
    RxComPtr<RxD2DBitmap> Target;
    D2D1_MATRIX_3X2_F Transform;
    D2D1_ANTIALIAS_MODE Antialias;
    D2D1_TEXT_ANTIALIAS_MODE TextAntialias;
    D2D1_PRIMITIVE_BLEND Blend;
    D2D1_UNIT_MODE Units;
    FLOAT DpiX;
    FLOAT DpiY;
    D2D1_TAG Tag1;
    D2D1_TAG Tag2;
    RxComPtr<IDWriteRenderingParams> TextParams;
    BOOLEAN Drawing;
    BOOLEAN Rendering;
    HRESULT Pending;
    D2D1_TAG PendingTag1;
    D2D1_TAG PendingTag2;
    std::vector<Clip> Clips;
};

/* Texture coordinate mapping helpers */
VOID
RxInvertMatrix(
    _In_ const D2D1_MATRIX_3X2_F& Matrix,
    _Out_ D2D1_MATRIX_3X2_F* Inverse,
    _Out_ BOOLEAN* Valid);

D2D1_MATRIX_3X2_F
RxMultiply(
    _In_ const D2D1_MATRIX_3X2_F& First,
    _In_ const D2D1_MATRIX_3X2_F& Second);

D2D1_POINT_2F
RxTransformPoint(
    _In_ const D2D1_MATRIX_3X2_F& Matrix,
    _In_ D2D1_POINT_2F Point);

/* Geometry */
VOID
RxTessellateRect(
    _In_ const D2D1_RECT_F& Rect,
    _In_ const D2D1_MATRIX_3X2_F& Device,
    _In_ BOOLEAN Antialias,
    _Inout_ std::vector<RxVertex>& Out);

VOID
RxTessellateRoundedRect(
    _In_ const D2D1_ROUNDED_RECT& Rect,
    _In_ const D2D1_MATRIX_3X2_F& Device,
    _In_ BOOLEAN Antialias,
    _Inout_ std::vector<RxVertex>& Out);

VOID
RxTessellateEllipse(
    _In_ const D2D1_ELLIPSE& Ellipse,
    _In_ const D2D1_MATRIX_3X2_F& Device,
    _In_ BOOLEAN Antialias,
    _Inout_ std::vector<RxVertex>& Out);

/* Text: draws a glyph run through DirectWrite coverage */
VOID
RxDrawGlyphRun(
    _In_ RxD2DContext* Context,
    _In_ RxRenderer& Renderer,
    _In_ IDWriteFactory2* Write,
    _In_ const D2D1_MATRIX_3X2_F& World,
    _In_ FLOAT DpiX,
    _In_ FLOAT DpiY,
    _In_ D2D1_POINT_2F Baseline,
    _In_ const DWRITE_GLYPH_RUN* Run,
    _In_ const RxPaint& Paint,
    _In_ BOOLEAN IsSolid,
    _In_ const D2D1_COLOR_F& Color,
    _In_ FLOAT Opacity,
    _In_ DWRITE_MEASURING_MODE Measuring,
    _In_ D2D1_TEXT_ANTIALIAS_MODE TextAntialias,
    _In_ IDWriteRenderingParams* Params);

/* Renderer DrawTextLayout hands to the layout */
class RxTextRenderer final : public IDWriteTextRenderer
{
public:
    RxTextRenderer(
        _In_ ID2D1DeviceContext* Context,
        _In_ ID2D1Brush* Brush,
        _In_ D2D1_DRAW_TEXT_OPTIONS Options,
        _In_ FLOAT DpiX,
        _In_ FLOAT DpiY,
        _In_ BOOLEAN Pixels);

    HRESULT STDMETHODCALLTYPE QueryInterface(_In_ REFIID riid, _COM_Outptr_ void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef(VOID) override;
    ULONG STDMETHODCALLTYPE Release(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    IsPixelSnappingDisabled(
        _In_opt_ void* clientDrawingContext,
        _Out_ BOOL* isDisabled) override;

    HRESULT
    STDMETHODCALLTYPE
    GetCurrentTransform(
        _In_opt_ void* clientDrawingContext,
        _Out_ DWRITE_MATRIX* transform) override;

    HRESULT
    STDMETHODCALLTYPE
    GetPixelsPerDip(
        _In_opt_ void* clientDrawingContext,
        _Out_ FLOAT* pixelsPerDip) override;

    HRESULT
    STDMETHODCALLTYPE
    DrawGlyphRun(
        _In_opt_ void* clientDrawingContext,
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        DWRITE_MEASURING_MODE measuringMode,
        _In_ DWRITE_GLYPH_RUN const* glyphRun,
        _In_ DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
        _In_opt_ IUnknown* clientDrawingEffect) override;

    HRESULT
    STDMETHODCALLTYPE
    DrawUnderline(
        _In_opt_ void* clientDrawingContext,
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        _In_ DWRITE_UNDERLINE const* underline,
        _In_opt_ IUnknown* clientDrawingEffect) override;

    HRESULT
    STDMETHODCALLTYPE
    DrawStrikethrough(
        _In_opt_ void* clientDrawingContext,
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        _In_ DWRITE_STRIKETHROUGH const* strikethrough,
        _In_opt_ IUnknown* clientDrawingEffect) override;

    HRESULT
    STDMETHODCALLTYPE
    DrawInlineObject(
        _In_opt_ void* clientDrawingContext,
        FLOAT originX,
        FLOAT originY,
        _In_ IDWriteInlineObject* inlineObject,
        BOOL isSideways,
        BOOL isRightToLeft,
        _In_opt_ IUnknown* clientDrawingEffect) override;

private:
    HRESULT
    BrushFor(
        _In_opt_ IUnknown* Effect,
        _Outptr_ ID2D1Brush** Brush);

    HRESULT
    FillLine(
        _In_ FLOAT X,
        _In_ FLOAT Y,
        _In_ FLOAT Width,
        _In_ FLOAT Offset,
        _In_ FLOAT Thickness,
        _In_ DWRITE_MEASURING_MODE Measuring,
        _In_opt_ IUnknown* Effect);

    ID2D1DeviceContext* Context;
    ID2D1Brush* DefaultBrush;
    D2D1_DRAW_TEXT_OPTIONS Options;
    FLOAT DpiX;
    FLOAT DpiY;
    BOOLEAN Pixels;
};
