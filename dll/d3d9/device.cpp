/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The device object: creation, its swap chain and surfaces, state, clears and presents
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* What a render state slot holds before anything sets it */
static constexpr DWORD D9UnsetState = 0xBAADCAFE;

/* Render states the runtime no longer defines; asking for them fails */
static const BYTE D9RetiredStates[] =
{
    1, 2, 3, 4, 5, 6, 10, 11, 12, 13, 17, 18, 21, 30, 31, 32, 33, 39, 41, 43, 44, 45, 46, 49, 51,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
    90, 91, 92, 93, 94, 95, 144, 153, 164, 196, 197,
};

/* Every render state a new device starts with; Z enable and the point size limit come from the device */
static const struct
{
    D3DRENDERSTATETYPE State;
    DWORD Value;
} D9DefaultStates[] =
{
    { D3DRS_ZENABLE, D3DZB_FALSE }, { D3DRS_FILLMODE, D3DFILL_SOLID }, { D3DRS_SHADEMODE, D3DSHADE_GOURAUD },
    { D3DRS_ZWRITEENABLE, TRUE }, { D3DRS_ALPHATESTENABLE, FALSE }, { D3DRS_LASTPIXEL, TRUE },
    { D3DRS_SRCBLEND, D3DBLEND_ONE }, { D3DRS_DESTBLEND, D3DBLEND_ZERO }, { D3DRS_CULLMODE, D3DCULL_CCW },
    { D3DRS_ZFUNC, D3DCMP_LESSEQUAL }, { D3DRS_ALPHAREF, 0 }, { D3DRS_ALPHAFUNC, D3DCMP_ALWAYS },
    { D3DRS_DITHERENABLE, FALSE }, { D3DRS_ALPHABLENDENABLE, FALSE }, { D3DRS_FOGENABLE, FALSE },
    { D3DRS_SPECULARENABLE, FALSE }, { D3DRS_FOGCOLOR, 0 }, { D3DRS_FOGTABLEMODE, D3DFOG_NONE },
    { D3DRS_FOGSTART, 0 }, { D3DRS_FOGEND, 0x3F800000 }, { D3DRS_FOGDENSITY, 0x3F800000 },
    { static_cast<D3DRENDERSTATETYPE>(40), 0 },
    { D3DRS_RANGEFOGENABLE, FALSE }, { D3DRS_STENCILENABLE, FALSE }, { D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP },
    { D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP }, { D3DRS_STENCILPASS, D3DSTENCILOP_KEEP },
    { D3DRS_STENCILFUNC, D3DCMP_ALWAYS }, { D3DRS_STENCILREF, 0 }, { D3DRS_STENCILMASK, 0xFFFFFFFF },
    { D3DRS_STENCILWRITEMASK, 0xFFFFFFFF }, { D3DRS_TEXTUREFACTOR, 0xFFFFFFFF }, { D3DRS_WRAP0, 0 },
    { D3DRS_WRAP1, 0 }, { D3DRS_WRAP2, 0 }, { D3DRS_WRAP3, 0 }, { D3DRS_WRAP4, 0 }, { D3DRS_WRAP5, 0 },
    { D3DRS_WRAP6, 0 }, { D3DRS_WRAP7, 0 }, { D3DRS_CLIPPING, TRUE }, { D3DRS_LIGHTING, TRUE },
    { D3DRS_AMBIENT, 0 }, { D3DRS_FOGVERTEXMODE, D3DFOG_NONE }, { D3DRS_COLORVERTEX, TRUE },
    { D3DRS_LOCALVIEWER, TRUE }, { D3DRS_NORMALIZENORMALS, FALSE }, { D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1 },
    { D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2 }, { D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL },
    { D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL }, { D3DRS_VERTEXBLEND, D3DVBF_DISABLE },
    { D3DRS_CLIPPLANEENABLE, 0 }, { D3DRS_POINTSIZE, 0x3F800000 }, { D3DRS_POINTSIZE_MIN, 0x3F800000 },
    { D3DRS_POINTSPRITEENABLE, FALSE }, { D3DRS_POINTSCALEENABLE, FALSE }, { D3DRS_POINTSCALE_A, 0x3F800000 },
    { D3DRS_POINTSCALE_B, 0 }, { D3DRS_POINTSCALE_C, 0 }, { D3DRS_MULTISAMPLEANTIALIAS, TRUE },
    { D3DRS_MULTISAMPLEMASK, 0xFFFFFFFF }, { D3DRS_PATCHEDGESTYLE, D3DPATCHEDGE_DISCRETE },
    { D3DRS_POINTSIZE_MAX, 0 }, { D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE }, { D3DRS_COLORWRITEENABLE, 0x0000000F },
    { D3DRS_TWEENFACTOR, 0 }, { D3DRS_BLENDOP, D3DBLENDOP_ADD }, { D3DRS_POSITIONDEGREE, D3DDEGREE_CUBIC },
    { D3DRS_NORMALDEGREE, D3DDEGREE_LINEAR }, { D3DRS_SCISSORTESTENABLE, FALSE }, { D3DRS_SLOPESCALEDEPTHBIAS, 0 },
    { D3DRS_ANTIALIASEDLINEENABLE, FALSE }, { D3DRS_MINTESSELLATIONLEVEL, 0x3F800000 },
    { D3DRS_MAXTESSELLATIONLEVEL, 0x3F800000 }, { D3DRS_ADAPTIVETESS_X, 0 }, { D3DRS_ADAPTIVETESS_Y, 0 },
    { D3DRS_ADAPTIVETESS_Z, 0x3F800000 }, { D3DRS_ADAPTIVETESS_W, 0 }, { D3DRS_ENABLEADAPTIVETESSELLATION, FALSE },
    { D3DRS_TWOSIDEDSTENCILMODE, FALSE }, { D3DRS_CCW_STENCILFAIL, D3DSTENCILOP_KEEP },
    { D3DRS_CCW_STENCILZFAIL, D3DSTENCILOP_KEEP }, { D3DRS_CCW_STENCILPASS, D3DSTENCILOP_KEEP },
    { D3DRS_CCW_STENCILFUNC, D3DCMP_ALWAYS }, { D3DRS_COLORWRITEENABLE1, 0x0000000F },
    { D3DRS_COLORWRITEENABLE2, 0x0000000F }, { D3DRS_COLORWRITEENABLE3, 0x0000000F }, { D3DRS_BLENDFACTOR, 0xFFFFFFFF },
    { D3DRS_SRGBWRITEENABLE, 0 }, { D3DRS_DEPTHBIAS, 0 }, { D3DRS_WRAP8, 0 }, { D3DRS_WRAP9, 0 },
    { D3DRS_WRAP10, 0 }, { D3DRS_WRAP11, 0 }, { D3DRS_WRAP12, 0 }, { D3DRS_WRAP13, 0 }, { D3DRS_WRAP14, 0 },
    { D3DRS_WRAP15, 0 }, { D3DRS_SEPARATEALPHABLENDENABLE, FALSE }, { D3DRS_SRCBLENDALPHA, D3DBLEND_ONE },
    { D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO }, { D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD },
};

/* Hardware vertex processing goes through the runtime's fixed function shaders and reports their limits */
static constexpr DWORD D9ShaderPipelineVertexCaps = 0x0000017B;
static constexpr DWORD D9ShaderPipelineLights = 10;
static constexpr DWORD D9ShaderPipelineBlendIndex = 8;

/* Depth formats the application may lock */
static
BOOLEAN
D9IsLockableDepth(
    _In_ D3DFORMAT Format)
{
    return Format == D3DFMT_D16_LOCKABLE || Format == D3DFMT_D32F_LOCKABLE || Format == D3DFMT_D32_LOCKABLE;
}

static
BOOLEAN
D9IsRetiredState(
    _In_ DWORD State)
{
    for (BYTE Retired : D9RetiredStates)
    {
        if (Retired == State)
            return TRUE;
    }
    return FALSE;
}

/* Holds the device lock for applications that asked for a multithreaded device */
class D9DeviceLock
{
public:
    D9DeviceLock(
        _In_ CRITICAL_SECTION* Section,
        _In_ BOOLEAN Needed)
        : Held(Needed ? Section : nullptr)
    {
        if (Held != nullptr)
            EnterCriticalSection(Held);
    }

    ~D9DeviceLock()
    {
        if (Held != nullptr)
            LeaveCriticalSection(Held);
    }

    D9DeviceLock(const D9DeviceLock&) = delete;
    D9DeviceLock& operator=(const D9DeviceLock&) = delete;

private:
    CRITICAL_SECTION* Held;
};

#define D9_DEVICE_LOCK() D9DeviceLock Scope(&Lock, (BehaviorFlags & D3DCREATE_MULTITHREADED) != 0)

D9Device::D9Device(
    _In_ D9Enum* Enum,
    _In_ const D9Adapter* Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_opt_ HWND FocusWindow,
    _In_ DWORD BehaviorFlags,
    _In_ BOOLEAN IsEx)
    : Enum(Enum),
      Adapter(Adapter),
      DeviceType(DeviceType),
      FocusWindow(FocusWindow),
      BehaviorFlags(BehaviorFlags),
      IsEx(IsEx)
{
    InitializeCriticalSection(&Lock);
    for (DWORD& Value : RenderStates)
        Value = D9UnsetState;
    Enum->AddRef();
}

D9Device::~D9Device()
{
    delete AutoDepthStencil;
    delete ImplicitChain;
    DestroyHardware();
    DeleteCriticalSection(&Lock);
    Enum->Release();
}

/* Copies the adapter's caps and applies what this device's vertex processing changes */
VOID
D9Device::ComputeCaps(VOID)
{
    Enum->GetDeviceCaps(Adapter->Ordinal, DeviceType, &Caps);
    if ((BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) == 0)
    {
        Caps.VertexProcessingCaps = D9ShaderPipelineVertexCaps;
        Caps.MaxActiveLights = D9ShaderPipelineLights;
        Caps.MaxVertexBlendMatrixIndex = D9ShaderPipelineBlendIndex;
    }
}

HRESULT
D9Device::CreateAutoDepthStencil(
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    D3DSURFACE_DESC Desc = {};
    HRESULT hr;

    delete AutoDepthStencil;
    AutoDepthStencil = nullptr;
    if (!Parameters->EnableAutoDepthStencil)
        return S_OK;

    Desc.Format = Parameters->AutoDepthStencilFormat;
    Desc.Type = D3DRTYPE_SURFACE;
    Desc.Usage = D3DUSAGE_DEPTHSTENCIL;
    Desc.Pool = D3DPOOL_DEFAULT;
    Desc.MultiSampleType = Parameters->MultiSampleType;
    Desc.MultiSampleQuality = Parameters->MultiSampleQuality;
    Desc.Width = Parameters->BackBufferWidth;
    Desc.Height = Parameters->BackBufferHeight;

    AutoDepthStencil = new (std::nothrow) D9Surface(this, nullptr, &Desc, D9IsLockableDepth(Desc.Format));
    if (AutoDepthStencil == nullptr)
        return E_OUTOFMEMORY;

    hr = AutoDepthStencil->Create();
    if (FAILED(hr))
        D9Trace(L"CreateDevice: the automatic depth buffer could not be made (0x%08lX)", hr);
    return hr;
}

/* Points the driver at the first back buffer, the automatic depth buffer and a viewport covering them */
VOID
D9Device::BindRenderTargets(VOID)
{
    const D3DPRESENT_PARAMETERS& Parameters = ImplicitChain->PresentParameters();
    D3DDDIARG_SETRENDERTARGET Target = {};
    D3DDDIARG_SETDEPTHSTENCIL Depth = {};
    D3DDDIARG_VIEWPORTINFO Viewport = {};
    D3DDDIARG_ZRANGE Range = {};

    for (D9Surface*& Bound : RenderTargets)
        Bound = nullptr;
    RenderTargets[0] = ImplicitChain->BackBuffer(0);
    DepthStencil = AutoDepthStencil;

    Target.RenderTargetIndex = 0;
    Target.hRenderTarget = RenderTargets[0]->DriverResource()->DriverResource;
    DeviceFunctions.pfnSetRenderTarget(DriverDevice, &Target);
    Depth.hZBuffer = DepthStencil != nullptr ? DepthStencil->DriverResource()->DriverResource : NULL;
    DeviceFunctions.pfnSetDepthStencil(DriverDevice, &Depth);

    Viewport.Width = Parameters.BackBufferWidth;
    Viewport.Height = Parameters.BackBufferHeight;
    DeviceFunctions.pfnSetViewport(DriverDevice, &Viewport);
    Range.MinZ = 0.0f;
    Range.MaxZ = 1.0f;
    DeviceFunctions.pfnSetZRange(DriverDevice, &Range);
}

/* Fills the render state table with its defaults and hands each one to the driver */
VOID
D9Device::SetDefaultState(
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    FLOAT PointSizeMax = Caps.MaxPointSize;

    for (DWORD& Value : RenderStates)
        Value = D9UnsetState;

    for (const auto& Default : D9DefaultStates)
    {
        D3DDDIARG_RENDERSTATE State = {};

        State.State = static_cast<D3DDDIRENDERSTATETYPE>(Default.State);
        State.Value = Default.Value;
        if (Default.State == D3DRS_ZENABLE)
            State.Value = Parameters->EnableAutoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE;
        else if (Default.State == D3DRS_POINTSIZE_MAX)
            CopyMemory(&State.Value, &PointSizeMax, sizeof(State.Value));

        RenderStates[Default.State] = State.Value;
        DeviceFunctions.pfnSetRenderState(DriverDevice, &State);
    }
}

HRESULT
D9Device::Initialize(
    _Inout_ D3DPRESENT_PARAMETERS* Parameters)
{
    HRESULT hr;

    hr = CreateKernelDevice();
    if (FAILED(hr))
        return hr;
    hr = OpenDriverAdapter();
    if (FAILED(hr))
        return hr;
    hr = CreateDriverDevice(Parameters->Windowed != FALSE);
    if (FAILED(hr))
        return hr;
    ComputeCaps();

    ImplicitChain = new (std::nothrow) D9SwapChain(this, 0);
    if (ImplicitChain == nullptr)
        return E_OUTOFMEMORY;
    hr = ImplicitChain->Reset(Parameters);
    if (FAILED(hr))
        return hr;
    hr = CreateAutoDepthStencil(Parameters);
    if (FAILED(hr))
        return hr;

    BindRenderTargets();
    SetDefaultState(Parameters);
    return S_OK;
}

STDMETHODIMP
D9Device::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Object)
{
    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    if (IsEqualGUID(Iid, __uuidof(IUnknown)) || IsEqualGUID(Iid, __uuidof(IDirect3DDevice9)) ||
        (IsEx && IsEqualGUID(Iid, __uuidof(IDirect3DDevice9Ex))))
    {
        *Object = static_cast<IDirect3DDevice9Ex*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
D9Device::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&RefCount));
}

STDMETHODIMP_(ULONG)
D9Device::Release()
{
    LONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        delete this;
    return static_cast<ULONG>(Count);
}

STDMETHODIMP
D9Device::TestCooperativeLevel()
{
    D9_DEVICE_LOCK();

    if (Lost && !IsEx)
        return D3DERR_DEVICELOST;
    return S_OK;
}

STDMETHODIMP
D9Device::GetDirect3D(
    _Out_ IDirect3D9** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Enum;
    Enum->AddRef();
    return S_OK;
}

STDMETHODIMP
D9Device::GetDeviceCaps(
    _Out_ D3DCAPS9* Result)
{
    D9_DEVICE_LOCK();

    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Caps;
    return S_OK;
}

STDMETHODIMP
D9Device::GetDisplayMode(
    _In_ UINT Chain,
    _Out_ D3DDISPLAYMODE* Mode)
{
    D9_DEVICE_LOCK();

    if (Chain != 0 || Mode == nullptr)
        return D3DERR_INVALIDCALL;
    return ImplicitChain->GetDisplayMode(Mode);
}

STDMETHODIMP
D9Device::GetDisplayModeEx(
    _In_ UINT Chain,
    _Out_ D3DDISPLAYMODEEX* Mode,
    _Out_ D3DDISPLAYROTATION* Rotation)
{
    D9_DEVICE_LOCK();

    if (Chain != 0 || Mode == nullptr)
        return D3DERR_INVALIDCALL;
    return ImplicitChain->GetDisplayModeEx(Mode, Rotation);
}

STDMETHODIMP
D9Device::GetCreationParameters(
    _Out_ D3DDEVICE_CREATION_PARAMETERS* Parameters)
{
    D9_DEVICE_LOCK();

    if (Parameters == nullptr)
        return D3DERR_INVALIDCALL;

    Parameters->AdapterOrdinal = Adapter->Ordinal;
    Parameters->DeviceType = DeviceType;
    Parameters->hFocusWindow = FocusWindow;
    Parameters->BehaviorFlags = BehaviorFlags;
    return S_OK;
}

STDMETHODIMP
D9Device::GetSwapChain(
    _In_ UINT Chain,
    _Out_ IDirect3DSwapChain9** Result)
{
    D9_DEVICE_LOCK();

    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = nullptr;
    if (Chain != 0)
    {
        D9_TRACE_ONCE(L"GetSwapChain: swap chain %u does not exist", Chain);
        return D3DERR_INVALIDCALL;
    }

    *Result = ImplicitChain;
    ImplicitChain->AddRef();
    return S_OK;
}

STDMETHODIMP_(UINT)
D9Device::GetNumberOfSwapChains()
{
    return 1;
}

STDMETHODIMP
D9Device::GetBackBuffer(
    _In_ UINT Chain,
    _In_ UINT BackBuffer,
    _In_ D3DBACKBUFFER_TYPE Type,
    _Out_ IDirect3DSurface9** Result)
{
    D9_DEVICE_LOCK();

    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = nullptr;
    if (Chain != 0)
        return D3DERR_INVALIDCALL;
    return ImplicitChain->GetBackBuffer(BackBuffer, Type, Result);
}

STDMETHODIMP
D9Device::GetRasterStatus(
    _In_ UINT Chain,
    _Out_ D3DRASTER_STATUS* RasterStatus)
{
    D9_DEVICE_LOCK();

    if (Chain != 0)
        return D3DERR_INVALIDCALL;
    return ImplicitChain->GetRasterStatus(RasterStatus);
}

STDMETHODIMP
D9Device::GetRenderTarget(
    _In_ DWORD Index,
    _Out_ IDirect3DSurface9** Result)
{
    D9_DEVICE_LOCK();

    if (Result == nullptr || Index >= Caps.NumSimultaneousRTs)
        return D3DERR_INVALIDCALL;

    *Result = RenderTargets[Index];
    if (RenderTargets[Index] == nullptr)
        return D3DERR_NOTFOUND;

    RenderTargets[Index]->AddRef();
    return S_OK;
}

STDMETHODIMP
D9Device::GetDepthStencilSurface(
    _Out_ IDirect3DSurface9** Result)
{
    D9_DEVICE_LOCK();

    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = DepthStencil;
    if (DepthStencil == nullptr)
        return D3DERR_NOTFOUND;

    DepthStencil->AddRef();
    return S_OK;
}

/* Clears the bound targets inside the viewport, optionally limited to rectangles */
STDMETHODIMP
D9Device::Clear(
    _In_ DWORD Count,
    _In_opt_ const D3DRECT* Rects,
    _In_ DWORD Flags,
    _In_ D3DCOLOR Color,
    _In_ FLOAT Z,
    _In_ DWORD Stencil)
{
    D9_DEVICE_LOCK();
    const D3DPRESENT_PARAMETERS& Parameters = ImplicitChain->PresentParameters();
    D3DDDIARG_CLEAR Args = {};
    RECT Viewport;
    RECT* Clipped = nullptr;
    UINT Kept = 0;
    HRESULT hr;

    if (Flags == 0 || (Flags & ~(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) != 0)
    {
        D9_TRACE_ONCE(L"Clear: flags 0x%08lX are not valid", Flags);
        return D3DERR_INVALIDCALL;
    }
    if ((Flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) != 0 && DepthStencil == nullptr)
    {
        D9_TRACE_ONCE(L"Clear: there is no depth buffer to clear");
        return D3DERR_INVALIDCALL;
    }

    SetRect(&Viewport, 0, 0, Parameters.BackBufferWidth, Parameters.BackBufferHeight);
    if (Count != 0 && Rects != nullptr)
    {
        Clipped = new (std::nothrow) RECT[Count];
        if (Clipped == nullptr)
            return E_OUTOFMEMORY;
        for (DWORD Index = 0; Index < Count; Index++)
        {
            RECT Area = { Rects[Index].x1, Rects[Index].y1, Rects[Index].x2, Rects[Index].y2 };

            if (IntersectRect(&Clipped[Kept], &Area, &Viewport))
                Kept += 1;
        }
        if (Kept == 0)
        {
            delete[] Clipped;
            return S_OK;
        }
    }

    Args.Flags = Flags;
    Args.FillColor = Color;
    Args.FillDepth = Z;
    Args.FillStencil = Stencil;
    hr = DeviceFunctions.pfnClear(DriverDevice, &Args, Kept, Clipped);
    delete[] Clipped;
    if (FAILED(hr))
        D9Trace(L"UMD Clear failed 0x%08lX", hr);
    return hr;
}

STDMETHODIMP
D9Device::SetRenderState(
    _In_ D3DRENDERSTATETYPE State,
    _In_ DWORD Value)
{
    D9_DEVICE_LOCK();
    D3DDDIARG_RENDERSTATE Args = {};

    if (static_cast<DWORD>(State) >= D9RenderStateCount || D9IsRetiredState(State))
    {
        D9_TRACE_ONCE(L"SetRenderState: state %u is not valid", State);
        return D3DERR_INVALIDCALL;
    }

    RenderStates[State] = Value;
    Args.State = static_cast<D3DDDIRENDERSTATETYPE>(State);
    Args.Value = Value;
    return DeviceFunctions.pfnSetRenderState(DriverDevice, &Args);
}

STDMETHODIMP
D9Device::GetRenderState(
    _In_ D3DRENDERSTATETYPE State,
    _Out_ DWORD* Value)
{
    D9_DEVICE_LOCK();

    if ((BehaviorFlags & D3DCREATE_PUREDEVICE) != 0 || static_cast<DWORD>(State) >= D9RenderStateCount ||
        D9IsRetiredState(State))
    {
        return D3DERR_INVALIDCALL;
    }

    *Value = RenderStates[State];
    return S_OK;
}

STDMETHODIMP
D9Device::Present(
    _In_opt_ const RECT* SourceRect,
    _In_opt_ const RECT* DestRect,
    _In_opt_ HWND DestWindowOverride,
    _In_opt_ const RGNDATA* DirtyRegion)
{
    D9_DEVICE_LOCK();

    return ImplicitChain->PresentWindowed(SourceRect, DestRect, DestWindowOverride, DirtyRegion, 0);
}

STDMETHODIMP
D9Device::PresentEx(
    _In_opt_ const RECT* SourceRect,
    _In_opt_ const RECT* DestRect,
    _In_opt_ HWND DestWindowOverride,
    _In_opt_ const RGNDATA* DirtyRegion,
    _In_ DWORD Flags)
{
    D9_DEVICE_LOCK();

    return ImplicitChain->PresentWindowed(SourceRect, DestRect, DestWindowOverride, DirtyRegion, Flags);
}

/* Remakes the swap chain and depth buffer for new parameters; only windowed devices exist so far */
HRESULT
D9Device::ResetCommon(
    _Inout_ D3DPRESENT_PARAMETERS* Parameters,
    _In_opt_ const D3DDISPLAYMODEEX* FullscreenMode)
{
    HRESULT hr;

    if (Parameters == nullptr)
        return D3DERR_INVALIDCALL;

    hr = Enum->ValidateReset(Adapter->Ordinal, DeviceType, BehaviorFlags, Parameters, FullscreenMode);
    if (FAILED(hr))
        return hr;
    if (!Parameters->Windowed)
    {
        D9_TRACE_ONCE(L"Reset: fullscreen devices are not implemented");
        return D3DERR_NOTAVAILABLE;
    }

    for (D9Surface*& Bound : RenderTargets)
        Bound = nullptr;
    DepthStencil = nullptr;

    hr = ImplicitChain->Reset(Parameters);
    if (SUCCEEDED(hr))
        hr = CreateAutoDepthStencil(Parameters);
    if (FAILED(hr))
    {
        Lost = TRUE;
        return hr;
    }

    BindRenderTargets();
    SetDefaultState(Parameters);
    return S_OK;
}

STDMETHODIMP
D9Device::Reset(
    _In_opt_ D3DPRESENT_PARAMETERS* PresentationParameters)
{
    D9_DEVICE_LOCK();

    return ResetCommon(PresentationParameters, nullptr);
}

STDMETHODIMP
D9Device::ResetEx(
    _In_opt_ D3DPRESENT_PARAMETERS* PresentationParameters,
    _In_opt_ D3DDISPLAYMODEEX* FullscreenDisplayMode)
{
    D9_DEVICE_LOCK();

    return ResetCommon(PresentationParameters, FullscreenDisplayMode);
}

/* Whether the device window can be seen; with no window, whether the display itself is on */
STDMETHODIMP
D9Device::CheckDeviceState(
    _In_opt_ HWND DestinationWindow)
{
    typedef BOOLEAN (APIENTRY* PD9_CHECK_EXCLUSIVE)(VOID);
    typedef NTSTATUS (APIENTRY* PD9_CHECK_POWER)(const D3DKMT_CHECKMONITORPOWERSTATE*);
    static PD9_CHECK_EXCLUSIVE CheckExclusive = reinterpret_cast<PD9_CHECK_EXCLUSIVE>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTCheckExclusiveOwnership"));
    static PD9_CHECK_POWER CheckPower = reinterpret_cast<PD9_CHECK_POWER>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTCheckMonitorPowerState"));
    D9_DEVICE_LOCK();
    D3DKMT_CHECKMONITORPOWERSTATE Power = {};
    RECT Client = {};
    NTSTATUS Status;

    if (!IsEx)
        return D3DERR_INVALIDCALL;
    if (Lost)
        return D3DERR_DEVICELOST;

    if (DestinationWindow != NULL)
    {
        if (GetClientRect(DestinationWindow, &Client) && IsRectEmpty(&Client))
            return S_PRESENT_OCCLUDED;
        return D9CheckOcclusion(DestinationWindow);
    }

    if (CheckExclusive != nullptr && CheckExclusive())
        return S_PRESENT_OCCLUDED;
    if (CheckPower == nullptr)
        return S_OK;

    Power.hAdapter = Adapter->KernelAdapter;
    Power.VidPnSourceId = Adapter->SourceId;
    Status = CheckPower(&Power);
    if (Status == static_cast<NTSTATUS>(0xC01E0005) || Status == static_cast<NTSTATUS>(0xC01E0006) ||
        Status == static_cast<NTSTATUS>(0x801E0000))
    {
        return S_PRESENT_OCCLUDED;
    }
    if (Status == STATUS_INVALID_PARAMETER || NT_SUCCESS(Status))
        return S_OK;
    return D9HresultFromStatus(Status);
}

STDMETHODIMP
D9Device::GetGPUThreadPriority(
    _Out_ INT* Priority)
{
    D9_DEVICE_LOCK();

    if (!IsEx)
        return D3DERR_INVALIDCALL;

    *Priority = GpuPriority;
    return S_OK;
}

STDMETHODIMP
D9Device::SetGPUThreadPriority(
    _In_ INT Priority)
{
    D9_DEVICE_LOCK();

    if (!IsEx)
        return D3DERR_INVALIDCALL;
    if (Priority < -7 || Priority > 7)
        return E_INVALIDARG;

    GpuPriority = Priority;
    return S_OK;
}

STDMETHODIMP
D9Device::SetMaximumFrameLatency(
    _In_ UINT MaxLatency)
{
    D9_DEVICE_LOCK();

    if (!IsEx)
        return D3DERR_INVALIDCALL;

    FrameLatency = MaxLatency;
    return S_OK;
}

STDMETHODIMP
D9Device::GetMaximumFrameLatency(
    _Out_ UINT* MaxLatency)
{
    D9_DEVICE_LOCK();

    if (!IsEx)
        return D3DERR_INVALIDCALL;

    *MaxLatency = FrameLatency;
    return S_OK;
}

/* Entry points that are not implemented yet */

STDMETHODIMP_(UINT)
D9Device::GetAvailableTextureMem()
{
    RX_UNIMPLEMENTED();
    return 0;
}

STDMETHODIMP
D9Device::EvictManagedResources()
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetCursorProperties(
    _In_ UINT XHotSpot,
    _In_ UINT YHotSpot,
    _In_opt_ IDirect3DSurface9* CursorBitmap)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP_(VOID)
D9Device::SetCursorPosition(
    _In_ INT X,
    _In_ INT Y,
    _In_ DWORD Flags)
{
    RX_UNIMPLEMENTED();
}

STDMETHODIMP_(BOOL)
D9Device::ShowCursor(
    _In_ BOOL Show)
{
    RX_UNIMPLEMENTED();
    return 0;
}

STDMETHODIMP
D9Device::CreateAdditionalSwapChain(
    _In_opt_ D3DPRESENT_PARAMETERS* PresentationParameters,
    _Out_ IDirect3DSwapChain9** Result)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetDialogBoxMode(
    _In_ BOOL EnableDialogs)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP_(VOID)
D9Device::SetGammaRamp(
    _In_ UINT SwapChain,
    _In_ DWORD Flags,
    _In_opt_ const D3DGAMMARAMP* Ramp)
{
    RX_UNIMPLEMENTED();
}

STDMETHODIMP_(VOID)
D9Device::GetGammaRamp(
    _In_ UINT SwapChain,
    _Out_ D3DGAMMARAMP* Ramp)
{
    RX_UNIMPLEMENTED();
}

STDMETHODIMP
D9Device::CreateTexture(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Levels,
    _In_ DWORD Usage,
    _In_ D3DFORMAT Format,
    _In_ D3DPOOL Pool,
    _Out_ IDirect3DTexture9** Texture,
    _Inout_opt_ HANDLE* SharedHandle)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateVolumeTexture(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Depth,
    _In_ UINT Levels,
    _In_ DWORD Usage,
    _In_ D3DFORMAT Format,
    _In_ D3DPOOL Pool,
    _Out_ IDirect3DVolumeTexture9** VolumeTexture,
    _Inout_opt_ HANDLE* SharedHandle)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateCubeTexture(
    _In_ UINT EdgeLength,
    _In_ UINT Levels,
    _In_ DWORD Usage,
    _In_ D3DFORMAT Format,
    _In_ D3DPOOL Pool,
    _Out_ IDirect3DCubeTexture9** CubeTexture,
    _Inout_opt_ HANDLE* SharedHandle)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateVertexBuffer(
    _In_ UINT Length,
    _In_ DWORD Usage,
    _In_ DWORD FVF,
    _In_ D3DPOOL Pool,
    _Out_ IDirect3DVertexBuffer9** VertexBuffer,
    _Inout_opt_ HANDLE* SharedHandle)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateIndexBuffer(
    _In_ UINT Length,
    _In_ DWORD Usage,
    _In_ D3DFORMAT Format,
    _In_ D3DPOOL Pool,
    _Out_ IDirect3DIndexBuffer9** IndexBuffer,
    _Inout_opt_ HANDLE* SharedHandle)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateRenderTarget(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ D3DFORMAT Format,
    _In_ D3DMULTISAMPLE_TYPE MultiSample,
    _In_ DWORD MultisampleQuality,
    _In_ BOOL Lockable,
    _Out_ IDirect3DSurface9** Surface,
    _Inout_opt_ HANDLE* SharedHandle)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateDepthStencilSurface(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ D3DFORMAT Format,
    _In_ D3DMULTISAMPLE_TYPE MultiSample,
    _In_ DWORD MultisampleQuality,
    _In_ BOOL Discard,
    _Out_ IDirect3DSurface9** Surface,
    _Inout_opt_ HANDLE* SharedHandle)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::UpdateSurface(
    _In_opt_ IDirect3DSurface9* SourceSurface,
    _In_opt_ const RECT* SourceRect,
    _In_opt_ IDirect3DSurface9* DestinationSurface,
    _In_opt_ const POINT* DestPoint)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::UpdateTexture(
    _In_opt_ IDirect3DBaseTexture9* SourceTexture,
    _In_opt_ IDirect3DBaseTexture9* DestinationTexture)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetRenderTargetData(
    _Out_ IDirect3DSurface9* RenderTarget,
    _Out_ IDirect3DSurface9* DestSurface)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetFrontBufferData(
    _In_ UINT SwapChain,
    _Out_ IDirect3DSurface9* DestSurface)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::StretchRect(
    _In_opt_ IDirect3DSurface9* SourceSurface,
    _In_opt_ const RECT* SourceRect,
    _In_opt_ IDirect3DSurface9* DestSurface,
    _In_opt_ const RECT* DestRect,
    _In_ D3DTEXTUREFILTERTYPE Filter)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::ColorFill(
    _In_opt_ IDirect3DSurface9* Surface,
    _In_opt_ const RECT* Rect,
    _In_ D3DCOLOR Color)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateOffscreenPlainSurface(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ D3DFORMAT Format,
    _In_ D3DPOOL Pool,
    _Out_ IDirect3DSurface9** Surface,
    _Inout_opt_ HANDLE* SharedHandle)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetRenderTarget(
    _In_ DWORD RenderTargetIndex,
    _In_opt_ IDirect3DSurface9* RenderTarget)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetDepthStencilSurface(
    _In_opt_ IDirect3DSurface9* NewZStencil)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::BeginScene()
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::EndScene()
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetTransform(
    _In_ D3DTRANSFORMSTATETYPE State,
    _In_opt_ const D3DMATRIX* Matrix)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetTransform(
    _In_ D3DTRANSFORMSTATETYPE State,
    _Out_ D3DMATRIX* Matrix)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::MultiplyTransform(
    _In_ D3DTRANSFORMSTATETYPE State,
    _In_opt_ const D3DMATRIX* Matrix)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetViewport(
    _In_opt_ const D3DVIEWPORT9* Viewport)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetViewport(
    _Out_ D3DVIEWPORT9* Viewport)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetMaterial(
    _In_opt_ const D3DMATERIAL9* Material)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetMaterial(
    _Out_ D3DMATERIAL9* Material)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetLight(
    _In_ DWORD Index,
    _In_opt_ const D3DLIGHT9* Light)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetLight(
    _In_ DWORD Index,
    _Out_ D3DLIGHT9* Light)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::LightEnable(
    _In_ DWORD Index,
    _In_ BOOL Enable)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetLightEnable(
    _In_ DWORD Index,
    _Out_ BOOL* Enable)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetClipPlane(
    _In_ DWORD Index,
    _In_opt_ const float* Plane)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetClipPlane(
    _In_ DWORD Index,
    _Out_ float* Plane)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateStateBlock(
    _In_ D3DSTATEBLOCKTYPE Type,
    _Out_ IDirect3DStateBlock9** SB)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::BeginStateBlock()
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::EndStateBlock(
    _In_ IDirect3DStateBlock9** SB)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetClipStatus(
    _In_opt_ const D3DCLIPSTATUS9* ClipStatus)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetClipStatus(
    _Out_ D3DCLIPSTATUS9* ClipStatus)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetTexture(
    _In_ DWORD Stage,
    _Out_ IDirect3DBaseTexture9** Texture)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetTexture(
    _In_ DWORD Stage,
    _In_opt_ IDirect3DBaseTexture9* Texture)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetTextureStageState(
    _In_ DWORD Stage,
    _In_ D3DTEXTURESTAGESTATETYPE Type,
    _Out_ DWORD* Value)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetTextureStageState(
    _In_ DWORD Stage,
    _In_ D3DTEXTURESTAGESTATETYPE Type,
    _In_ DWORD Value)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetSamplerState(
    _In_ DWORD Sampler,
    _In_ D3DSAMPLERSTATETYPE Type,
    _Out_ DWORD* Value)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetSamplerState(
    _In_ DWORD Sampler,
    _In_ D3DSAMPLERSTATETYPE Type,
    _In_ DWORD Value)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::ValidateDevice(
    _In_opt_ DWORD* NumPasses)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetPaletteEntries(
    _In_ UINT PaletteNumber,
    _In_opt_ const PALETTEENTRY* Entries)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetPaletteEntries(
    _In_ UINT PaletteNumber,
    _Out_ PALETTEENTRY* Entries)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetCurrentTexturePalette(
    _In_ UINT PaletteNumber)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetCurrentTexturePalette(
    _Out_ UINT* PaletteNumber)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetScissorRect(
    _In_opt_ const RECT* Rect)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetScissorRect(
    _Out_ RECT* Rect)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetSoftwareVertexProcessing(
    _In_ BOOL Software)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP_(BOOL)
D9Device::GetSoftwareVertexProcessing()
{
    RX_UNIMPLEMENTED();
    return 0;
}

STDMETHODIMP
D9Device::SetNPatchMode(
    _In_ FLOAT NSegments)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP_(float)
D9Device::GetNPatchMode()
{
    RX_UNIMPLEMENTED();
    return 0;
}

STDMETHODIMP
D9Device::DrawPrimitive(
    _In_ D3DPRIMITIVETYPE PrimitiveType,
    _In_ UINT StartVertex,
    _In_ UINT PrimitiveCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::DrawIndexedPrimitive(
    _In_ D3DPRIMITIVETYPE PrimitiveType,
    _In_ INT BaseVertexIndex,
    _In_ UINT MinVertexIndex,
    _In_ UINT NumVertices,
    _In_ UINT StartIndex,
    _In_ UINT PrimCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::DrawPrimitiveUP(
    _In_ D3DPRIMITIVETYPE PrimitiveType,
    _In_ UINT PrimitiveCount,
    _In_opt_ const VOID* VertexStreamZeroData,
    _In_ UINT VertexStreamZeroStride)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::DrawIndexedPrimitiveUP(
    _In_ D3DPRIMITIVETYPE PrimitiveType,
    _In_ UINT MinVertexIndex,
    _In_ UINT NumVertices,
    _In_ UINT PrimitiveCount,
    _In_opt_ const VOID* IndexData,
    _In_ D3DFORMAT IndexDataFormat,
    _In_opt_ const VOID* VertexStreamZeroData,
    _In_ UINT VertexStreamZeroStride)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::ProcessVertices(
    _In_ UINT SrcStartIndex,
    _In_ UINT DestIndex,
    _In_ UINT VertexCount,
    _In_opt_ IDirect3DVertexBuffer9* DestBuffer,
    _In_opt_ IDirect3DVertexDeclaration9* VertexDecl,
    _In_ DWORD Flags)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateVertexDeclaration(
    _In_opt_ const D3DVERTEXELEMENT9* VertexElements,
    _Out_ IDirect3DVertexDeclaration9** Decl)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetVertexDeclaration(
    _In_opt_ IDirect3DVertexDeclaration9* Decl)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetVertexDeclaration(
    _Out_ IDirect3DVertexDeclaration9** Decl)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetFVF(
    _In_ DWORD FVF)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetFVF(
    _Out_ DWORD* FVF)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateVertexShader(
    _In_opt_ const DWORD* Function,
    _Out_ IDirect3DVertexShader9** Shader)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetVertexShader(
    _In_opt_ IDirect3DVertexShader9* Shader)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetVertexShader(
    _Out_ IDirect3DVertexShader9** Shader)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetVertexShaderConstantF(
    _In_ UINT StartRegister,
    _In_opt_ const float* ConstantData,
    _In_ UINT Vector4fCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetVertexShaderConstantF(
    _In_ UINT StartRegister,
    _Out_ float* ConstantData,
    _In_ UINT Vector4fCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetVertexShaderConstantI(
    _In_ UINT StartRegister,
    _In_opt_ const int* ConstantData,
    _In_ UINT Vector4iCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetVertexShaderConstantI(
    _In_ UINT StartRegister,
    _Out_ int* ConstantData,
    _In_ UINT Vector4iCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetVertexShaderConstantB(
    _In_ UINT StartRegister,
    _In_opt_ const BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetVertexShaderConstantB(
    _In_ UINT StartRegister,
    _Out_ BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetStreamSource(
    _In_ UINT StreamNumber,
    _In_opt_ IDirect3DVertexBuffer9* StreamData,
    _In_ UINT OffsetInBytes,
    _In_ UINT Stride)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetStreamSource(
    _In_ UINT StreamNumber,
    _Out_ IDirect3DVertexBuffer9** StreamData,
    _Out_ UINT* OffsetInBytes,
    _Out_ UINT* Stride)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetStreamSourceFreq(
    _In_ UINT StreamNumber,
    _In_ UINT Setting)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetStreamSourceFreq(
    _In_ UINT StreamNumber,
    _Out_ UINT* Setting)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetIndices(
    _In_opt_ IDirect3DIndexBuffer9* IndexData)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetIndices(
    _Out_ IDirect3DIndexBuffer9** IndexData)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreatePixelShader(
    _In_opt_ const DWORD* Function,
    _Out_ IDirect3DPixelShader9** Shader)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetPixelShader(
    _In_opt_ IDirect3DPixelShader9* Shader)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetPixelShader(
    _Out_ IDirect3DPixelShader9** Shader)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetPixelShaderConstantF(
    _In_ UINT StartRegister,
    _In_opt_ const float* ConstantData,
    _In_ UINT Vector4fCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetPixelShaderConstantF(
    _In_ UINT StartRegister,
    _Out_ float* ConstantData,
    _In_ UINT Vector4fCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetPixelShaderConstantI(
    _In_ UINT StartRegister,
    _In_opt_ const int* ConstantData,
    _In_ UINT Vector4iCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetPixelShaderConstantI(
    _In_ UINT StartRegister,
    _Out_ int* ConstantData,
    _In_ UINT Vector4iCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetPixelShaderConstantB(
    _In_ UINT StartRegister,
    _In_opt_ const BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::GetPixelShaderConstantB(
    _In_ UINT StartRegister,
    _Out_ BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::DrawRectPatch(
    _In_ UINT Handle,
    _In_opt_ const float* NumSegs,
    _In_opt_ const D3DRECTPATCH_INFO* RectPatchInfo)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::DrawTriPatch(
    _In_ UINT Handle,
    _In_opt_ const float* NumSegs,
    _In_opt_ const D3DTRIPATCH_INFO* TriPatchInfo)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::DeletePatch(
    _In_ UINT Handle)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateQuery(
    _In_ D3DQUERYTYPE Type,
    _Out_ IDirect3DQuery9** Query)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::SetConvolutionMonoKernel(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_opt_ float* Rows,
    _In_opt_ float* Columns)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::ComposeRects(
    _In_opt_ IDirect3DSurface9* Src,
    _In_opt_ IDirect3DSurface9* Dst,
    _In_opt_ IDirect3DVertexBuffer9* SrcRectDescs,
    _In_ UINT NumRects,
    _In_opt_ IDirect3DVertexBuffer9* DstRectDescs,
    _In_ D3DCOMPOSERECTSOP Operation,
    _In_ INT OffsetX,
    _In_ INT OffsetY)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::WaitForVBlank(
    _In_ UINT Chain)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CheckResourceResidency(
    _In_ IDirect3DResource9** ResourceArray,
    _In_ UINT32 NumResources)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateRenderTargetEx(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ D3DFORMAT Format,
    _In_ D3DMULTISAMPLE_TYPE MultiSample,
    _In_ DWORD MultisampleQuality,
    _In_ BOOL Lockable,
    _Out_ IDirect3DSurface9** Surface,
    _Inout_opt_ HANDLE* SharedHandle,
    _In_ DWORD Usage)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateOffscreenPlainSurfaceEx(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ D3DFORMAT Format,
    _In_ D3DPOOL Pool,
    _Out_ IDirect3DSurface9** Surface,
    _Inout_opt_ HANDLE* SharedHandle,
    _In_ DWORD Usage)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9Device::CreateDepthStencilSurfaceEx(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ D3DFORMAT Format,
    _In_ D3DMULTISAMPLE_TYPE MultiSample,
    _In_ DWORD MultisampleQuality,
    _In_ BOOL Discard,
    _Out_ IDirect3DSurface9** Surface,
    _Inout_opt_ HANDLE* SharedHandle,
    _In_ DWORD Usage)
{
    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}
