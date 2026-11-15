/*
 * PROJECT:     ReactX Direct2D
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Factory and device objects, and the exported entry points
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d2dobjects.hpp"

#include <cmath>

extern const WCHAR RxTraceModule[] = L"d2d1";

/* RxD2DFactory */

HRESULT
RxD2DFactory::Create(
    _In_ D2D1_FACTORY_TYPE FactoryType,
    _Outptr_ RxD2DFactory** Factory)
{
    *Factory = nullptr;
    if (FactoryType != D2D1_FACTORY_TYPE_SINGLE_THREADED && FactoryType != D2D1_FACTORY_TYPE_MULTI_THREADED)
        return E_INVALIDARG;

    *Factory = new (std::nothrow) RxD2DFactory();
    if (*Factory == nullptr)
        return E_OUTOFMEMORY;
    (*Factory)->Type = FactoryType;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxD2DFactory::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Factory) || riid == __uuidof(ID2D1Factory1))
    {
        *ppvObject = static_cast<ID2D1Factory1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxD2DFactory)

static
BOOL
CALLBACK
RxCreateWriteFactory(
    _Inout_ PINIT_ONCE InitOnce,
    _Inout_opt_ PVOID Parameter,
    _Outptr_opt_result_maybenull_ PVOID* Context)
{
    typedef HRESULT (WINAPI *RxCreateFactory)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
    HMODULE Module = LoadLibraryW(L"dwrite.dll");
    RxCreateFactory Create;
    IUnknown* Factory = nullptr;

    *Context = nullptr;
    if (Module == nullptr)
        return TRUE;

    Create = reinterpret_cast<RxCreateFactory>(GetProcAddress(Module, "DWriteCreateFactory"));
    if (Create != nullptr && SUCCEEDED(Create(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), &Factory)))
        *Context = Factory;
    return TRUE;
}

IDWriteFactory2*
RxD2DFactory::Write(VOID)
{
    PVOID Context = nullptr;

    /* The shared DirectWrite factory lives for the process, so its reference is never dropped */
    if (!InitOnceExecuteOnce(&WriteReady, RxCreateWriteFactory, nullptr, &Context))
        return nullptr;
    return static_cast<IDWriteFactory2*>(Context);
}

HRESULT
STDMETHODCALLTYPE
RxD2DFactory::ReloadSystemMetrics(VOID)
{
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxD2DFactory::GetDesktopDpi(
    _Out_ FLOAT* dpiX,
    _Out_ FLOAT* dpiY)
{
    HDC Screen = GetDC(nullptr);

    *dpiX = *dpiY = 96.0f;
    if (Screen != nullptr)
    {
        *dpiX = static_cast<FLOAT>(GetDeviceCaps(Screen, LOGPIXELSX));
        *dpiY = static_cast<FLOAT>(GetDeviceCaps(Screen, LOGPIXELSY));
        ReleaseDC(nullptr, Screen);
    }
}

HRESULT
STDMETHODCALLTYPE
RxD2DFactory::CreateDevice(
    _In_ IDXGIDevice* dxgiDevice,
    _COM_Outptr_ ID2D1Device** d2dDevice)
{
    RxD2DDevice* Device;
    HRESULT hr;

    if (d2dDevice == nullptr)
        return E_INVALIDARG;
    *d2dDevice = nullptr;
    if (dxgiDevice == nullptr)
        return E_INVALIDARG;

    hr = RxD2DDevice::Create(this, dxgiDevice, &Device);
    if (FAILED(hr))
        return hr;
    *d2dDevice = Device;
    return S_OK;
}

/* RxD2DDevice */

HRESULT
RxD2DDevice::Create(
    _In_ RxD2DFactory* Factory,
    _In_ IDXGIDevice* Dxgi,
    _Outptr_ RxD2DDevice** Device)
{
    RxComPtr<ID3D11Device1> D3D;
    RxD2DDevice* Result;
    HRESULT hr;

    *Device = nullptr;
    hr = Dxgi->QueryInterface(__uuidof(ID3D11Device1), D3D.PutVoid());
    if (FAILED(hr))
        return hr;

    Result = new (std::nothrow) RxD2DDevice();
    if (Result == nullptr)
        return E_OUTOFMEMORY;
    Result->Owner = RxComPtr<RxD2DFactory>(Factory);

    hr = Result->Draw.Initialize(D3D.Get());
    if (FAILED(hr))
    {
        Result->Release();
        return hr;
    }

    *Device = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxD2DDevice::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Resource) || riid == __uuidof(ID2D1Device))
    {
        *ppvObject = static_cast<ID2D1Device*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxD2DDevice)

VOID
STDMETHODCALLTYPE
RxD2DDevice::GetFactory(
    _Outptr_ ID2D1Factory** factory) const
{
    *factory = Owner.Copy();
}

HRESULT
STDMETHODCALLTYPE
RxD2DDevice::CreateDeviceContext(
    D2D1_DEVICE_CONTEXT_OPTIONS options,
    _COM_Outptr_ ID2D1DeviceContext** deviceContext)
{
    RxD2DContext* Context;
    HRESULT hr;

    if (deviceContext == nullptr)
        return E_INVALIDARG;
    *deviceContext = nullptr;

    hr = RxD2DContext::Create(this, options, &Context);
    if (FAILED(hr))
        return hr;
    *deviceContext = Context;
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxD2DDevice::SetMaximumTextureMemory(
    UINT64 maximumInBytes)
{
    TextureMemory = maximumInBytes;
}

UINT64
STDMETHODCALLTYPE
RxD2DDevice::GetMaximumTextureMemory(VOID) const
{
    return TextureMemory;
}

VOID
STDMETHODCALLTYPE
RxD2DDevice::ClearResources(
    UINT32 millisecondsSinceUse)
{
}

/* Exports */

extern "C"
HRESULT
WINAPI
D2D1CreateFactory(
    _In_ D2D1_FACTORY_TYPE factoryType,
    _In_ REFIID riid,
    _In_opt_ CONST D2D1_FACTORY_OPTIONS* pFactoryOptions,
    _Out_ void** ppIFactory)
{
    RxD2DFactory* Factory;
    HRESULT hr;

    if (ppIFactory == nullptr)
        return E_INVALIDARG;
    *ppIFactory = nullptr;

    hr = RxD2DFactory::Create(factoryType, &Factory);
    if (FAILED(hr))
        return hr;
    hr = Factory->QueryInterface(riid, ppIFactory);
    Factory->Release();
    return hr;
}

extern "C"
HRESULT
WINAPI
D2D1CreateDevice(
    _In_ IDXGIDevice* dxgiDevice,
    _In_opt_ CONST D2D1_CREATION_PROPERTIES* creationProperties,
    _Outptr_ ID2D1Device** d2dDevice)
{
    RxComPtr<ID2D1Factory1> Factory;
    HRESULT hr;

    if (d2dDevice == nullptr)
        return E_INVALIDARG;
    *d2dDevice = nullptr;

    hr = D2D1CreateFactory(creationProperties != nullptr && creationProperties->threadingMode == D2D1_THREADING_MODE_MULTI_THREADED ?
                               D2D1_FACTORY_TYPE_MULTI_THREADED : D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           __uuidof(ID2D1Factory1), nullptr, Factory.PutVoid());
    if (FAILED(hr))
        return hr;
    return Factory->CreateDevice(dxgiDevice, d2dDevice);
}

extern "C"
HRESULT
WINAPI
D2D1CreateDeviceContext(
    _In_ IDXGISurface* dxgiSurface,
    _In_opt_ CONST D2D1_CREATION_PROPERTIES* creationProperties,
    _Outptr_ ID2D1DeviceContext** d2dDeviceContext)
{
    RxComPtr<ID3D11Resource> Resource;
    RxComPtr<ID3D11Device> D3D;
    RxComPtr<IDXGIDevice> Dxgi;
    RxComPtr<ID2D1Device> Device;
    RxComPtr<ID2D1Bitmap1> Target;
    HRESULT hr;

    if (d2dDeviceContext == nullptr)
        return E_INVALIDARG;
    *d2dDeviceContext = nullptr;
    if (dxgiSurface == nullptr)
        return E_INVALIDARG;

    hr = dxgiSurface->QueryInterface(__uuidof(ID3D11Resource), Resource.PutVoid());
    if (FAILED(hr))
        return hr;
    Resource->GetDevice(D3D.Put());
    hr = D3D->QueryInterface(__uuidof(IDXGIDevice), Dxgi.PutVoid());
    if (SUCCEEDED(hr))
        hr = D2D1CreateDevice(Dxgi.Get(), creationProperties, Device.Put());
    if (SUCCEEDED(hr))
        hr = Device->CreateDeviceContext(creationProperties != nullptr ? creationProperties->options :
                                                                         D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                         d2dDeviceContext);
    if (SUCCEEDED(hr))
        hr = (*d2dDeviceContext)->CreateBitmapFromDxgiSurface(dxgiSurface, nullptr, Target.Put());
    if (SUCCEEDED(hr))
        (*d2dDeviceContext)->SetTarget(Target.Get());
    return hr;
}

extern "C"
VOID
WINAPI
D2D1MakeRotateMatrix(
    _In_ FLOAT angle,
    _In_ D2D1_POINT_2F center,
    _Out_ D2D1_MATRIX_3X2_F* matrix)
{
    double Radians = angle * 3.14159265358979323846 / 180.0;
    FLOAT Sin = static_cast<FLOAT>(std::sin(Radians));
    FLOAT Cos = static_cast<FLOAT>(std::cos(Radians));

    matrix->_11 = Cos;
    matrix->_12 = Sin;
    matrix->_21 = -Sin;
    matrix->_22 = Cos;
    matrix->_31 = center.x - center.x * Cos + center.y * Sin;
    matrix->_32 = center.y - center.x * Sin - center.y * Cos;
}

extern "C"
VOID
WINAPI
D2D1MakeSkewMatrix(
    _In_ FLOAT angleX,
    _In_ FLOAT angleY,
    _In_ D2D1_POINT_2F center,
    _Out_ D2D1_MATRIX_3X2_F* matrix)
{
    FLOAT TanX = static_cast<FLOAT>(std::tan(angleX * 3.14159265358979323846 / 180.0));
    FLOAT TanY = static_cast<FLOAT>(std::tan(angleY * 3.14159265358979323846 / 180.0));

    matrix->_11 = 1.0f;
    matrix->_12 = TanY;
    matrix->_21 = TanX;
    matrix->_22 = 1.0f;
    matrix->_31 = -center.y * TanX;
    matrix->_32 = -center.x * TanY;
}

extern "C"
BOOL
WINAPI
D2D1IsMatrixInvertible(
    _In_ CONST D2D1_MATRIX_3X2_F* matrix)
{
    D2D1_MATRIX_3X2_F Inverse;
    BOOLEAN Valid;

    RxInvertMatrix(*matrix, &Inverse, &Valid);
    return Valid;
}

extern "C"
BOOL
WINAPI
D2D1InvertMatrix(
    _Inout_ D2D1_MATRIX_3X2_F* matrix)
{
    D2D1_MATRIX_3X2_F Inverse;
    BOOLEAN Valid;

    RxInvertMatrix(*matrix, &Inverse, &Valid);
    if (Valid)
        *matrix = Inverse;
    return Valid;
}

extern "C"
VOID
WINAPI
D2D1SinCos(
    _In_ FLOAT angle,
    _Out_ FLOAT* s,
    _Out_ FLOAT* c)
{
    *s = std::sin(angle);
    *c = std::cos(angle);
}

extern "C"
FLOAT
WINAPI
D2D1Tan(
    _In_ FLOAT angle)
{
    return std::tan(angle);
}

extern "C"
FLOAT
WINAPI
D2D1Vec3Length(
    _In_ FLOAT x,
    _In_ FLOAT y,
    _In_ FLOAT z)
{
    return std::sqrt(x * x + y * y + z * z);
}

extern "C"
D2D1_COLOR_F
WINAPI
D2D1ConvertColorSpace(
    D2D1_COLOR_SPACE sourceColorSpace,
    D2D1_COLOR_SPACE destinationColorSpace,
    _In_ CONST D2D1_COLOR_F* color)
{
    D2D1_COLOR_F Result = *color;
    auto ToLinear = [](FLOAT V) { return V <= 0.04045f ? V / 12.92f : std::pow((V + 0.055f) / 1.055f, 2.4f); };
    auto ToSrgb = [](FLOAT V) { return V <= 0.0031308f ? V * 12.92f : 1.055f * std::pow(V, 1.0f / 2.4f) - 0.055f; };

    if (sourceColorSpace == D2D1_COLOR_SPACE_SRGB && destinationColorSpace == D2D1_COLOR_SPACE_SCRGB)
        Result = { ToLinear(color->r), ToLinear(color->g), ToLinear(color->b), color->a };
    else if (sourceColorSpace == D2D1_COLOR_SPACE_SCRGB && destinationColorSpace == D2D1_COLOR_SPACE_SRGB)
        Result = { ToSrgb(color->r), ToSrgb(color->g), ToSrgb(color->b), color->a };
    return Result;
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(Instance);
    return TRUE;
}
