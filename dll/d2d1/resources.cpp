/*
 * PROJECT:     ReactX Direct2D
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Bitmaps, brushes and gradient stop collections
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d2dobjects.hpp"

#include <cmath>

/* Private interfaces our objects answer to, so internal code can find them behind a public pointer */
static constexpr GUID RxBitmapIid = { 0x2c7b45d1, 0x8e60, 0x4f3a, { 0x9d, 0x21, 0x0b, 0x5f, 0x77, 0xa4, 0xe3, 0x18 } };
static constexpr GUID RxSolidBrushIid = { 0x4e9a0b62, 0x1f37, 0x4c85, { 0xa6, 0x0d, 0x93, 0x2e, 0x51, 0x7c, 0x48, 0xb0 } };
static constexpr GUID RxLinearBrushIid = { 0x7d31c8f4, 0x5a26, 0x4b09, { 0x8e, 0x77, 0x14, 0xc0, 0x6b, 0x2d, 0xf9, 0x53 } };
static constexpr GUID RxStopsIid = { 0x9b05e2a7, 0x3c48, 0x4d1e, { 0xb2, 0x6f, 0x80, 0x4a, 0x17, 0xd3, 0x5c, 0x9e } };

template <typename T>
static
T*
RxFindObject(
    _In_opt_ IUnknown* Object,
    _In_ REFIID Iid)
{
    T* Result = nullptr;

    if (Object == nullptr || FAILED(Object->QueryInterface(Iid, reinterpret_cast<void**>(&Result))))
        return nullptr;
    Result->Release();
    return Result;
}

/* RxD2DBitmap */

HRESULT
RxD2DBitmap::Create(
    _In_ RxD2DDevice* Device,
    _In_ ID3D11Texture2D* Texture2D,
    _In_ const D2D1_BITMAP_PROPERTIES1& BitmapProperties,
    _Outptr_ RxD2DBitmap** Bitmap)
{
    RxD2DBitmap* Result;
    D3D11_TEXTURE2D_DESC Desc;
    HRESULT hr;

    *Bitmap = nullptr;
    Result = new (std::nothrow) RxD2DBitmap();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    Texture2D->GetDesc(&Desc);
    Result->OwnerDevice = RxComPtr<RxD2DDevice>(Device);
    Result->Texture = RxComPtr<ID3D11Texture2D>(Texture2D);
    Result->Properties = BitmapProperties;
    Result->Properties.colorContext = nullptr;
    Result->Width = Desc.Width;
    Result->Height = Desc.Height;

    if (!(BitmapProperties.bitmapOptions & D2D1_BITMAP_OPTIONS_CANNOT_DRAW) && (Desc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
    {
        hr = Device->Renderer().Device()->CreateShaderResourceView(Texture2D, nullptr, Result->ShaderView.Put());
        if (FAILED(hr))
        {
            Result->Release();
            return hr;
        }
    }

    *Bitmap = Result;
    return S_OK;
}

RxD2DBitmap*
RxD2DBitmap::FromInterface(
    _In_opt_ IUnknown* Object)
{
    return RxFindObject<RxD2DBitmap>(Object, RxBitmapIid);
}

ID3D11RenderTargetView*
RxD2DBitmap::TargetView(VOID)
{
    if (!RenderView.Get())
        OwnerDevice->Renderer().Device()->CreateRenderTargetView(Texture.Get(), nullptr, RenderView.Put());
    return RenderView.Get();
}

HRESULT
STDMETHODCALLTYPE
RxD2DBitmap::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == RxBitmapIid || riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Resource) ||
        riid == __uuidof(ID2D1Image) || riid == __uuidof(ID2D1Bitmap) || riid == __uuidof(ID2D1Bitmap1))
    {
        *ppvObject = static_cast<ID2D1Bitmap1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxD2DBitmap)

VOID
STDMETHODCALLTYPE
RxD2DBitmap::GetFactory(
    _Outptr_ ID2D1Factory** factory) const
{
    *factory = OwnerDevice->Factory();
    (*factory)->AddRef();
}

D2D1_SIZE_F
STDMETHODCALLTYPE
RxD2DBitmap::GetSize(VOID) const
{
    return { Width * 96.0f / Properties.dpiX, Height * 96.0f / Properties.dpiY };
}

D2D1_SIZE_U
STDMETHODCALLTYPE
RxD2DBitmap::GetPixelSize(VOID) const
{
    return { Width, Height };
}

D2D1_PIXEL_FORMAT
STDMETHODCALLTYPE
RxD2DBitmap::GetPixelFormat(VOID) const
{
    return Properties.pixelFormat;
}

VOID
STDMETHODCALLTYPE
RxD2DBitmap::GetDpi(
    _Out_ FLOAT* dpiX,
    _Out_ FLOAT* dpiY) const
{
    *dpiX = Properties.dpiX;
    *dpiY = Properties.dpiY;
}

HRESULT
STDMETHODCALLTYPE
RxD2DBitmap::CopyFromMemory(
    _In_opt_ CONST D2D1_RECT_U* dstRect,
    _In_ CONST void* srcData,
    UINT32 pitch)
{
    D3D11_BOX Box = { 0, 0, 0, Width, Height, 1 };
    RxRenderer& Renderer = OwnerDevice->Renderer();

    if (srcData == nullptr)
        return E_INVALIDARG;
    if (dstRect != nullptr)
    {
        if (dstRect->right > Width || dstRect->bottom > Height || dstRect->left > dstRect->right ||
            dstRect->top > dstRect->bottom)
            return E_INVALIDARG;
        Box = { dstRect->left, dstRect->top, 0, dstRect->right, dstRect->bottom, 1 };
    }

    AcquireSRWLockExclusive(&Renderer.Lock);
    Renderer.Context()->UpdateSubresource(Texture.Get(), 0, &Box, srcData, pitch, 0);
    ReleaseSRWLockExclusive(&Renderer.Lock);
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxD2DBitmap::GetColorContext(
    _Outptr_result_maybenull_ ID2D1ColorContext** colorContext) const
{
    *colorContext = nullptr;
}

D2D1_BITMAP_OPTIONS
STDMETHODCALLTYPE
RxD2DBitmap::GetOptions(VOID) const
{
    return Properties.bitmapOptions;
}

HRESULT
STDMETHODCALLTYPE
RxD2DBitmap::GetSurface(
    _COM_Outptr_result_maybenull_ IDXGISurface** dxgiSurface) const
{
    return Texture->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(dxgiSurface));
}

/* RxSolidBrush */

HRESULT
RxSolidBrush::Create(
    _In_ RxD2DFactory* Factory,
    _In_ const D2D1_COLOR_F& Value,
    _In_opt_ const D2D1_BRUSH_PROPERTIES* Properties,
    _Outptr_ RxSolidBrush** Brush)
{
    *Brush = new (std::nothrow) RxSolidBrush();
    if (*Brush == nullptr)
        return E_OUTOFMEMORY;
    (*Brush)->Owner = RxComPtr<RxD2DFactory>(Factory);
    (*Brush)->Color = Value;
    (*Brush)->Common.Initialize(Properties);
    return S_OK;
}

RxSolidBrush*
RxSolidBrush::FromInterface(
    _In_opt_ IUnknown* Object)
{
    return RxFindObject<RxSolidBrush>(Object, RxSolidBrushIid);
}

HRESULT
STDMETHODCALLTYPE
RxSolidBrush::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == RxSolidBrushIid || riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Resource) ||
        riid == __uuidof(ID2D1Brush) || riid == __uuidof(ID2D1SolidColorBrush))
    {
        *ppvObject = static_cast<ID2D1SolidColorBrush*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxSolidBrush)

VOID
STDMETHODCALLTYPE
RxSolidBrush::GetFactory(
    _Outptr_ ID2D1Factory** factory) const
{
    *factory = Owner.Copy();
}

VOID
STDMETHODCALLTYPE
RxSolidBrush::SetOpacity(
    FLOAT opacity)
{
    Common.Opacity = opacity;
}

VOID
STDMETHODCALLTYPE
RxSolidBrush::SetTransform(
    _In_ CONST D2D1_MATRIX_3X2_F* transform)
{
    Common.Transform = *transform;
}

FLOAT
STDMETHODCALLTYPE
RxSolidBrush::GetOpacity(VOID) const
{
    return Common.Opacity;
}

VOID
STDMETHODCALLTYPE
RxSolidBrush::GetTransform(
    _Out_ D2D1_MATRIX_3X2_F* transform) const
{
    *transform = Common.Transform;
}

VOID
STDMETHODCALLTYPE
RxSolidBrush::SetColor(
    _In_ CONST D2D1_COLOR_F* color)
{
    Color = *color;
}

D2D1_COLOR_F
STDMETHODCALLTYPE
RxSolidBrush::GetColor(VOID) const
{
    return Color;
}

/* RxGradientStops */

HRESULT
RxGradientStops::Create(
    _In_ RxD2DDevice* Owner,
    _In_reads_(Count) const D2D1_GRADIENT_STOP* Stops,
    _In_ UINT32 Count,
    _In_ D2D1_GAMMA Gamma,
    _In_ D2D1_EXTEND_MODE Extend,
    _Outptr_ RxGradientStops** Collection)
{
    RxGradientStops* Result;

    *Collection = nullptr;
    if (Count == 0 || Stops == nullptr || static_cast<UINT32>(Gamma) > D2D1_GAMMA_1_0 ||
        static_cast<UINT32>(Extend) > D2D1_EXTEND_MODE_MIRROR)
        return E_INVALIDARG;

    Result = new (std::nothrow) RxGradientStops();
    if (Result == nullptr)
        return E_OUTOFMEMORY;
    Result->Device = RxComPtr<RxD2DDevice>(Owner);
    Result->Stops.assign(Stops, Stops + Count);
    Result->Gamma = Gamma;
    Result->ExtendMode = Extend;
    *Collection = Result;
    return S_OK;
}

RxGradientStops*
RxGradientStops::FromInterface(
    _In_opt_ IUnknown* Object)
{
    return RxFindObject<RxGradientStops>(Object, RxStopsIid);
}

HRESULT
STDMETHODCALLTYPE
RxGradientStops::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == RxStopsIid || riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Resource) ||
        riid == __uuidof(ID2D1GradientStopCollection) || riid == __uuidof(ID2D1GradientStopCollection1))
    {
        *ppvObject = static_cast<ID2D1GradientStopCollection1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxGradientStops)

VOID
STDMETHODCALLTYPE
RxGradientStops::GetFactory(
    _Outptr_ ID2D1Factory** factory) const
{
    *factory = Device->Factory();
    (*factory)->AddRef();
}

UINT32
STDMETHODCALLTYPE
RxGradientStops::GetGradientStopCount(VOID) const
{
    return static_cast<UINT32>(Stops.size());
}

VOID
STDMETHODCALLTYPE
RxGradientStops::GetGradientStops(
    D2D1_GRADIENT_STOP* gradientStops,
    UINT32 gradientStopsCount) const
{
    for (UINT32 Index = 0; Index < gradientStopsCount && Index < Stops.size(); Index++)
        gradientStops[Index] = Stops[Index];
}

D2D1_GAMMA
STDMETHODCALLTYPE
RxGradientStops::GetColorInterpolationGamma(VOID) const
{
    return Gamma;
}

D2D1_EXTEND_MODE
STDMETHODCALLTYPE
RxGradientStops::GetExtendMode(VOID) const
{
    return ExtendMode;
}

VOID
STDMETHODCALLTYPE
RxGradientStops::GetGradientStops1(
    D2D1_GRADIENT_STOP* gradientStops,
    UINT32 gradientStopsCount) const
{
    GetGradientStops(gradientStops, gradientStopsCount);
}

D2D1_COLOR_SPACE
STDMETHODCALLTYPE
RxGradientStops::GetPreInterpolationSpace(VOID) const
{
    return Gamma == D2D1_GAMMA_1_0 ? D2D1_COLOR_SPACE_SCRGB : D2D1_COLOR_SPACE_SRGB;
}

D2D1_COLOR_SPACE
STDMETHODCALLTYPE
RxGradientStops::GetPostInterpolationSpace(VOID) const
{
    return D2D1_COLOR_SPACE_SRGB;
}

D2D1_BUFFER_PRECISION
STDMETHODCALLTYPE
RxGradientStops::GetBufferPrecision(VOID) const
{
    return D2D1_BUFFER_PRECISION_8BPC_UNORM;
}

D2D1_COLOR_INTERPOLATION_MODE
STDMETHODCALLTYPE
RxGradientStops::GetColorInterpolationMode(VOID) const
{
    return D2D1_COLOR_INTERPOLATION_MODE_STRAIGHT;
}

static
FLOAT
RxClamp01(
    _In_ FLOAT Value)
{
    return Value < 0.0f ? 0.0f : (Value > 1.0f ? 1.0f : Value);
}

static
FLOAT
RxSrgbToLinear(
    _In_ FLOAT Value)
{
    return Value <= 0.04045f ? Value / 12.92f : std::pow((Value + 0.055f) / 1.055f, 2.4f);
}

static
FLOAT
RxLinearToSrgb(
    _In_ FLOAT Value)
{
    return Value <= 0.0031308f ? Value * 12.92f : 1.055f * std::pow(Value, 1.0f / 2.4f) - 0.055f;
}

static
D2D1_COLOR_F
RxLerp(
    _In_ const D2D1_COLOR_F& A,
    _In_ const D2D1_COLOR_F& B,
    _In_ FLOAT T)
{
    return { A.r + (B.r - A.r) * T, A.g + (B.g - A.g) * T, A.b + (B.b - A.b) * T, A.a + (B.a - A.a) * T };
}

ID3D11ShaderResourceView*
RxGradientStops::Realize(
    _In_ UINT32 Width)
{
    std::vector<D2D1_GRADIENT_STOP> Sorted;
    std::vector<D2D1_GRADIENT_STOP> Line;
    D2D1_COLOR_F BeforeColor;
    D2D1_COLOR_F AfterColor;
    std::vector<UINT32> Texels(Width);
    BOOLEAN Clamp = ExtendMode == D2D1_EXTEND_MODE_CLAMP;
    const FLOAT Epsilon = 1.1921e-6f;

    for (const auto& Entry : Levels)
    {
        if (Entry.Width == Width)
            return Entry.View.Get();
    }

    /* Clamped, and linear first for gamma 1.0 */
    Sorted = Stops;
    for (auto& Stop : Sorted)
    {
        Stop.color = { RxClamp01(Stop.color.r), RxClamp01(Stop.color.g), RxClamp01(Stop.color.b), RxClamp01(Stop.color.a) };
        if (Gamma == D2D1_GAMMA_1_0)
            Stop.color = { RxSrgbToLinear(Stop.color.r), RxSrgbToLinear(Stop.color.g), RxSrgbToLinear(Stop.color.b),
                           Stop.color.a };
    }
    std::stable_sort(Sorted.begin(), Sorted.end(),
                     [](const D2D1_GRADIENT_STOP& A, const D2D1_GRADIENT_STOP& B) { return A.position < B.position; });

    /* Normalize to [0, 1], keeping the colors the borders show */
    {
        SIZE_T Count = Sorted.size();
        SIZE_T First = 0;

        while (First < Count && Sorted[First].position < -Epsilon)
            First++;

        if (First == Count)
        {
            Line.push_back({ 0.0f, Sorted.back().color });
            BeforeColor = Sorted.back().color;
        }
        else if (First > 0 && Sorted[First].position > Epsilon)
        {
            const D2D1_GRADIENT_STOP& A = Sorted[First - 1];
            const D2D1_GRADIENT_STOP& B = Sorted[First];
            D2D1_COLOR_F Middle = RxLerp(A.color, B.color, (0.0f - A.position) / (B.position - A.position));

            Line.push_back({ 0.0f, Middle });
            BeforeColor = Middle;
        }
        else if (std::fabs(Sorted[First].position) <= Epsilon)
        {
            SIZE_T Last = First;

            while (Last + 1 < Count && std::fabs(Sorted[Last + 1].position) <= Epsilon)
                Last++;
            BeforeColor = Sorted[First].color;
            Line.push_back({ 0.0f, Sorted[Last].color });
            First = Last + 1;
        }
        else
        {
            BeforeColor = Sorted[First].color;
            Line.push_back({ 0.0f, Sorted[First].color });
        }

        for (SIZE_T Index = First; Index < Count && Sorted[Index].position < 1.0f - Epsilon; Index++)
        {
            if (Sorted[Index].position > Epsilon)
                Line.push_back(Sorted[Index]);
        }

        {
            SIZE_T Index = 0;

            while (Index < Count && Sorted[Index].position < 1.0f - Epsilon)
                Index++;

            if (Index == Count)
            {
                Line.push_back({ 1.0f, Line.back().color });
                AfterColor = Line.back().color;
            }
            else if (std::fabs(Sorted[Index].position - 1.0f) <= Epsilon)
            {
                SIZE_T Last = Index;

                while (Last + 1 < Count && std::fabs(Sorted[Last + 1].position - 1.0f) <= Epsilon)
                    Last++;
                Line.push_back({ 1.0f, Sorted[Index].color });
                AfterColor = Sorted[Last].color;
            }
            else
            {
                const D2D1_GRADIENT_STOP& A = Line.back();
                const D2D1_GRADIENT_STOP& B = Sorted[Index];
                D2D1_COLOR_F Middle = RxLerp(A.color, B.color, (1.0f - A.position) / (B.position - A.position));

                Line.push_back({ 1.0f, Middle });
                AfterColor = Middle;
            }
        }
    }

    auto ColorAt = [&](FLOAT T)
    {
        for (SIZE_T Index = 0; Index + 1 < Line.size(); Index++)
        {
            if (T <= Line[Index + 1].position)
            {
                FLOAT Span = Line[Index + 1].position - Line[Index].position;

                return Span > 0.0f ? RxLerp(Line[Index].color, Line[Index + 1].color, (T - Line[Index].position) / Span) :
                                     Line[Index + 1].color;
            }
        }
        return Line.back().color;
    };

    auto Encode = [&](D2D1_COLOR_F Color)
    {
        UINT32 Bytes[4];
        FLOAT Channels[4];

        if (Gamma == D2D1_GAMMA_1_0)
            Color = { RxLinearToSrgb(Color.r), RxLinearToSrgb(Color.g), RxLinearToSrgb(Color.b), Color.a };
        Color = { RxClamp01(Color.r), RxClamp01(Color.g), RxClamp01(Color.b), RxClamp01(Color.a) };
        Channels[0] = Color.a;
        Channels[1] = Color.r * Color.a;
        Channels[2] = Color.g * Color.a;
        Channels[3] = Color.b * Color.a;
        for (UINT32 Index = 0; Index < 4; Index++)
            Bytes[Index] = static_cast<UINT32>(std::min(255.0f, std::floor(Channels[Index] * 255.0f + 0.5f)));
        return (Bytes[0] << 24) | (Bytes[1] << 16) | (Bytes[2] << 8) | Bytes[3];
    };

    /* Box filter each interior texel over its share of the line */
    {
        UINT32 First = (Clamp && Width > 1) ? 1 : 0;
        UINT32 Interior = (Clamp && Width > 1) ? Width - 2 : Width;

        if (Clamp && Width == 1)
        {
            D2D1_COLOR_F Mixed = RxLerp(BeforeColor, AfterColor, 0.5f);

            Texels[0] = Encode(Mixed);
        }
        else
        {
            for (UINT32 Texel = 0; Texel < Interior; Texel++)
            {
                FLOAT From = static_cast<FLOAT>(Texel) / Interior;
                FLOAT To = static_cast<FLOAT>(Texel + 1) / Interior;
                D2D1_COLOR_F Sum = { 0, 0, 0, 0 };
                FLOAT Position = From;

                for (SIZE_T Stop = 0; Stop <= Line.size() && Position < To; Stop++)
                {
                    FLOAT Next = To;
                    D2D1_COLOR_F Color;

                    for (const auto& Entry : Line)
                    {
                        if (Entry.position > Position && Entry.position < Next)
                            Next = Entry.position;
                    }
                    Color = ColorAt((Position + Next) / 2.0f);
                    FLOAT Weight = (Next - Position) * Interior;
                    Sum = { Sum.r + Color.r * Weight, Sum.g + Color.g * Weight, Sum.b + Color.b * Weight,
                            Sum.a + Color.a * Weight };
                    Position = Next;
                }
                Texels[First + Texel] = Encode(Sum);
            }
            if (Clamp && Width > 1)
            {
                Texels[0] = Encode(BeforeColor);
                Texels[Width - 1] = Encode(AfterColor);
            }
        }
    }

    {
        D3D11_TEXTURE2D_DESC Desc = {};
        D3D11_SUBRESOURCE_DATA Data = { Texels.data(), Width * 4, 0 };
        RxComPtr<ID3D11Texture2D> Texture;
        Level Entry;

        Desc.Width = Width;
        Desc.Height = 1;
        Desc.MipLevels = 1;
        Desc.ArraySize = 1;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_IMMUTABLE;
        Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(Device->Renderer().Device()->CreateTexture2D(&Desc, &Data, Texture.Put())))
            return nullptr;
        if (FAILED(Device->Renderer().Device()->CreateShaderResourceView(Texture.Get(), nullptr, Entry.View.Put())))
            return nullptr;
        Entry.Width = Width;
        Levels.push_back(Entry);
        return Levels.back().View.Get();
    }
}

/* RxLinearBrush */

HRESULT
RxLinearBrush::Create(
    _In_ RxD2DFactory* Factory,
    _In_ const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& Line,
    _In_opt_ const D2D1_BRUSH_PROPERTIES* Properties,
    _In_ RxGradientStops* Collection,
    _Outptr_ RxLinearBrush** Brush)
{
    *Brush = new (std::nothrow) RxLinearBrush();
    if (*Brush == nullptr)
        return E_OUTOFMEMORY;
    (*Brush)->Owner = RxComPtr<RxD2DFactory>(Factory);
    (*Brush)->Stops = RxComPtr<RxGradientStops>(Collection);
    (*Brush)->StartPoint = Line.startPoint;
    (*Brush)->EndPoint = Line.endPoint;
    (*Brush)->Common.Initialize(Properties);
    return S_OK;
}

RxLinearBrush*
RxLinearBrush::FromInterface(
    _In_opt_ IUnknown* Object)
{
    return RxFindObject<RxLinearBrush>(Object, RxLinearBrushIid);
}

HRESULT
STDMETHODCALLTYPE
RxLinearBrush::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == RxLinearBrushIid || riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Resource) ||
        riid == __uuidof(ID2D1Brush) || riid == __uuidof(ID2D1LinearGradientBrush))
    {
        *ppvObject = static_cast<ID2D1LinearGradientBrush*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxLinearBrush)

VOID
STDMETHODCALLTYPE
RxLinearBrush::GetFactory(
    _Outptr_ ID2D1Factory** factory) const
{
    *factory = Owner.Copy();
}

VOID
STDMETHODCALLTYPE
RxLinearBrush::SetOpacity(
    FLOAT opacity)
{
    Common.Opacity = opacity;
}

VOID
STDMETHODCALLTYPE
RxLinearBrush::SetTransform(
    _In_ CONST D2D1_MATRIX_3X2_F* transform)
{
    Common.Transform = *transform;
}

FLOAT
STDMETHODCALLTYPE
RxLinearBrush::GetOpacity(VOID) const
{
    return Common.Opacity;
}

VOID
STDMETHODCALLTYPE
RxLinearBrush::GetTransform(
    _Out_ D2D1_MATRIX_3X2_F* transform) const
{
    *transform = Common.Transform;
}

VOID
STDMETHODCALLTYPE
RxLinearBrush::SetStartPoint(
    D2D1_POINT_2F startPoint)
{
    StartPoint = startPoint;
}

VOID
STDMETHODCALLTYPE
RxLinearBrush::SetEndPoint(
    D2D1_POINT_2F endPoint)
{
    EndPoint = endPoint;
}

D2D1_POINT_2F
STDMETHODCALLTYPE
RxLinearBrush::GetStartPoint(VOID) const
{
    return StartPoint;
}

D2D1_POINT_2F
STDMETHODCALLTYPE
RxLinearBrush::GetEndPoint(VOID) const
{
    return EndPoint;
}

VOID
STDMETHODCALLTYPE
RxLinearBrush::GetGradientStopCollection(
    _Outptr_ ID2D1GradientStopCollection** gradientStopCollection) const
{
    *gradientStopCollection = Stops.Copy();
}
