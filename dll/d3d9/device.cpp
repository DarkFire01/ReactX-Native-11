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

/* Retired render states the runtime still hands the driver once, with the values it gives them */
static const struct
{
    BYTE State;
    DWORD Value;
} D9RetiredInitialValues[] =
{
    { 10, 0 }, { 30, 0 }, { 164, 0 }, { 196, 0xFFFFFFFF }, { 197, 0xFFFFFFFF },
};

/* The order a new device first hands its render states to the driver: pixel states before the
   texture stages, the rest after the clip planes */
static const BYTE D9InitialPixelStates[] =
{
    7, 8, 9, 10, 14, 15, 16, 19, 20, 22, 23, 24, 25, 26, 27, 28, 30, 34, 35, 36, 37, 38, 40, 48, 52, 53, 54, 55,
    56, 57, 58, 59, 185, 186, 187, 188, 189, 193, 194, 206, 207, 208, 209, 128, 129, 130, 131, 132, 133, 134,
    135, 198, 199, 200, 201, 202, 203, 204, 205,
};

static const BYTE D9InitialLaterStates[] =
{
    166, 154, 155, 156, 162, 161, 163, 164, 168, 171, 172, 173, 174, 195, 175, 176, 196, 197, 178, 179, 180,
    181, 182, 183, 184, 190, 191, 192,
};

/* With software vertex processing the driver also sees the vertex pipeline states */
static const BYTE D9InitialVertexStates[] =
{
    137, 139, 140, 141, 142, 143, 145, 146, 147, 148, 151,
};

static const BYTE D9InitialSoftwareLaterStates[] =
{
    166, 154, 155, 156, 157, 158, 159, 160, 162, 161, 163, 164, 167, 168, 170, 171, 172, 173, 174, 195, 175,
    176, 196, 197, 178, 179, 180, 181, 182, 183, 184, 190, 191, 192,
};

/* Sampler states in the order the driver first sees them, with their defaults */
static const struct
{
    BYTE State;
    BYTE Value;
} D9InitialSamplerStates[] =
{
    { D3DDDITSS_ADDRESSU, D3DTADDRESS_WRAP }, { D3DDDITSS_ADDRESSV, D3DTADDRESS_WRAP },
    { D3DDDITSS_ADDRESSW, D3DTADDRESS_WRAP }, { D3DDDITSS_BORDERCOLOR, 0 }, { D3DDDITSS_MAGFILTER, D3DTEXF_POINT },
    { D3DDDITSS_MINFILTER, D3DTEXF_POINT }, { D3DDDITSS_MIPFILTER, D3DTEXF_NONE },
    { D3DDDITSS_MIPMAPLODBIAS, 0 }, { D3DDDITSS_MAXMIPLEVEL, 0 }, { D3DDDITSS_MAXANISOTROPY, 1 },
    { D3DDDITSS_SRGBTEXTURE, 0 }, { D3DDDITSS_DMAPOFFSET, 0 }, { D3DDDITSS_ELEMENTINDEX, 0 },
};

/* Texture stages the fixed function pixel constants cover */
static constexpr UINT D9PixelStages = 8;

/* Where the runtime keeps fixed function pixel values in pixel shader constants */
static constexpr UINT D9ConstTextureFactor = 2;
static constexpr UINT D9ConstStageConstant = 3;
static constexpr UINT D9ConstLuminance = 11;
static constexpr UINT D9ConstBumpMatrix = 15;

/* Samplers: the sixteen pixel samplers, the displacement map sampler and the four vertex samplers */
static constexpr UINT D9PixelSamplers = 16;
static constexpr UINT D9VertexSamplers = 4;

/* Vertex processing caps the runtime's own vertex pipelines report, with their light and blend limits */
static constexpr DWORD D9PipelineVertexCaps = 0x0000017B;
static constexpr DWORD D9ShaderPipelineLights = 10;
static constexpr DWORD D9ShaderPipelineBlendIndex = 8;
static constexpr DWORD D9SoftwarePipelineLights = 0xFF;
static constexpr DWORD D9SoftwarePipelineBlendIndex = 0xFF;

/* Fog coordinates in the vertex format, a driver cap the runtime never shows */
static constexpr DWORD D9MiscCapsFogInFvf = 0x00002000;

/* Device caps the software vertex pipeline clears from what the driver reports */
static constexpr DWORD D9SoftwareDevCapsCleared = 0x07400000;

/* Depth formats the application may lock */
static
BOOLEAN
D9IsLockableDepth(
    _In_ D3DFORMAT Format)
{
    return Format == D3DFMT_D16_LOCKABLE || Format == D3DFMT_D32F_LOCKABLE || Format == D3DFMT_D32_LOCKABLE;
}

/* Depth formats with stencil bits */
BOOLEAN
D9HasStencil(
    _In_ D3DFORMAT Format)
{
    return Format == D3DFMT_D15S1 || Format == D3DFMT_D24S8 || Format == D3DFMT_D24X4S4 ||
           Format == D3DFMT_D24FS8 || Format == D3DFMT_S8_LOCKABLE;
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

D9Device::D9Device(
    _In_ D9Enum* Enum,
    _In_ const D9Adapter* Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_opt_ HWND FocusWindow,
    _In_ DWORD BehaviorFlags,
    _In_ BOOLEAN IsEx,
    _In_ BOOLEAN SwapEffectUpgrade)
    : Enum(Enum),
      Adapter(Adapter),
      DeviceType(DeviceType),
      FocusWindow(FocusWindow),
      BehaviorFlags(BehaviorFlags),
      IsEx(IsEx),
      SwapEffectUpgrade(SwapEffectUpgrade)
{
    InitializeCriticalSection(&Lock);
    for (DWORD& Value : RenderStates)
        Value = D9UnsetState;
    Enum->AddRef();
}

D9Device::~D9Device()
{
    UnbindTextures();
    StopBatch();
    delete AutoDepthStencil;
    delete ImplicitChain;
    if (DriverDevice != NULL)
        DestroyStagingBuffers();
    DestroyHardware();
    DeleteCriticalSection(&Lock);
    Enum->Release();
}

/* Copies the adapter's caps and applies what this device's vertex processing changes */
VOID
D9Device::ComputeCaps(VOID)
{
    Enum->GetDeviceCaps(Adapter->Ordinal, DeviceType, &Caps);
    /* Hardware vertex processing runs the fixed function vertex pipeline as the runtime's own shaders */
    if ((BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) == 0)
    {
        Caps.VertexProcessingCaps = D9PipelineVertexCaps;
        Caps.MaxActiveLights = D9ShaderPipelineLights;
        Caps.MaxVertexBlendMatrices = 4;
        Caps.MaxVertexBlendMatrixIndex = D9ShaderPipelineBlendIndex;
        return;
    }

    /* Software vertex processing reports what the runtime's pipeline can do, whatever the driver says */
    Caps.DevCaps &= ~D9SoftwareDevCapsCleared;
    Caps.DevCaps2 |= D3DDEVCAPS2_STREAMOFFSET | D3DDEVCAPS2_VERTEXELEMENTSCANSHARESTREAMOFFSET;
    Caps.PrimitiveMiscCaps = (Caps.PrimitiveMiscCaps & ~(D9MiscCapsFogInFvf | D3DPMISCCAPS_CLIPPLANESCALEDPOINTS)) |
                             D3DPMISCCAPS_CLIPTLVERTS;
    Caps.RasterCaps |= D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGRANGE;
    Caps.FVFCaps |= D3DFVFCAPS_PSIZE;
    if (Caps.MaxPointSize == 0.0f)
        Caps.MaxPointSize = 64.0f;
    Caps.VertexProcessingCaps = D9PipelineVertexCaps;
    Caps.MaxActiveLights = D9SoftwarePipelineLights;
    Caps.MaxUserClipPlanes = 6;
    Caps.MaxVertexBlendMatrices = 4;
    Caps.MaxVertexBlendMatrixIndex = D9SoftwarePipelineBlendIndex;
    Caps.MaxStreams = 16;
    Caps.VertexShaderVersion = D3DVS_VERSION(3, 0);
    Caps.MaxVertexShaderConst = 8192;
    Caps.DeclTypes |= 0x000003FF;
    Caps.VS20Caps.Caps = D3DVS20CAPS_PREDICATION;
    Caps.VS20Caps.DynamicFlowControlDepth = D3DVS20_MAX_DYNAMICFLOWCONTROLDEPTH;
    Caps.VS20Caps.NumTemps = D3DVS20_MAX_NUMTEMPS;
    Caps.VS20Caps.StaticFlowControlDepth = D3DVS20_MAX_STATICFLOWCONTROLDEPTH;
    Caps.VertexTextureFilterCaps = 0x1B031B00;
    Caps.MaxVShaderInstructionsExecuted = 0xFFFFFFFF;
    Caps.MaxVertexShader30InstructionSlots = D3DMAX30SHADERINSTRUCTIONS;
}

HRESULT
D9Device::CreateAutoDepthStencil(
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    D3DSURFACE_DESC Desc = {};
    HRESULT hr;

    if (AutoDepthStencil != nullptr)
        AutoDepthStencil->Abandon();
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

/* Binds the first back buffer and the automatic depth buffer; the driver learns of them before they are next written */
VOID
D9Device::BindRenderTargets(VOID)
{
    for (D9Surface*& Bound : RenderTargets)
        Bound = nullptr;
    RenderTargets[0] = ImplicitChain->BackBuffer(0);
    DepthStencil = AutoDepthStencil;
    TargetsDirty = TRUE;
}

VOID
D9Device::SendTargets(VOID)
{
    D3DDDIARG_SETRENDERTARGET Target = {};
    D3DDDIARG_SETDEPTHSTENCIL Depth = {};

    if (!TargetsDirty)
        return;

    Target.RenderTargetIndex = 0;
    Target.hRenderTarget = RenderTargets[0]->DriverResource()->DriverResource;
    DdiRenderTarget(&Target);
    Depth.hZBuffer = DepthStencil != nullptr ? DepthStencil->DriverResource()->DriverResource : NULL;
    DdiDepthStencil(&Depth);
    TargetsDirty = FALSE;
}

/* The viewport covering the back buffer, its depth range and its scissor rectangle */
VOID
D9Device::SendViewport(VOID)
{
    const D3DPRESENT_PARAMETERS& Parameters = ImplicitChain->PresentParameters();
    RECT Scissor;

    SendViewportRange();
    SetRect(&Scissor, 0, 0, Parameters.BackBufferWidth, Parameters.BackBufferHeight);
    DdiScissorRect(&Scissor);
}

VOID
D9Device::SendViewportRange(VOID)
{
    const D3DPRESENT_PARAMETERS& Parameters = ImplicitChain->PresentParameters();
    D3DDDIARG_VIEWPORTINFO Viewport = {};
    D3DDDIARG_ZRANGE Range = {};

    Viewport.Width = Parameters.BackBufferWidth;
    Viewport.Height = Parameters.BackBufferHeight;
    Range.MinZ = 0.0f;
    Range.MaxZ = 1.0f;
    DdiViewport(&Viewport);
    DdiZRange(&Range);
}

/* Fills the render state table with its defaults; the driver keeps what it was last sent */
VOID
D9Device::SetDefaultState(
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    FLOAT PointSizeMax = Caps.MaxPointSize;

    for (DWORD& Value : RenderStates)
        Value = D9UnsetState;

    for (const auto& Default : D9DefaultStates)
    {
        DWORD Value = Default.Value;

        if (Default.State == D3DRS_ZENABLE)
            Value = Parameters->EnableAutoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE;
        else if (Default.State == D3DRS_POINTSIZE_MAX)
            CopyMemory(&Value, &PointSizeMax, sizeof(Value));
        RenderStates[Default.State] = Value;
    }
}

DWORD
D9Device::InitialStateValue(
    _In_ UINT State) const
{
    for (const auto& Retired : D9RetiredInitialValues)
    {
        if (Retired.State == State)
            return Retired.Value;
    }
    return RenderStates[State];
}

VOID
D9Device::SendRenderState(
    _In_ UINT State)
{
    D3DDDIARG_RENDERSTATE Args = {};

    Args.State = static_cast<D3DDDIRENDERSTATETYPE>(State);
    Args.Value = InitialStateValue(State);
    DdiRenderState(&Args);
}

VOID
D9Device::SendTextureStageState(
    _In_ UINT Stage,
    _In_ UINT State,
    _In_ UINT Value)
{
    D3DDDIARG_TEXTURESTAGESTATE Args = {};

    Args.Stage = Stage;
    Args.State = static_cast<D3DDDITEXTURESTAGESTATETYPE>(State);
    Args.Value = Value;
    DdiTextureStageState(&Args);
}

VOID
D9Device::SendPixelConstant(
    _In_ UINT Register,
    _In_ FLOAT X,
    _In_ FLOAT Y,
    _In_ FLOAT Z,
    _In_ FLOAT W)
{
    D3DDDIARG_SETPIXELSHADERCONST Args = {};
    const FLOAT Values[4] = { X, Y, Z, W };

    Args.Register = Register;
    Args.Count = 1;
    DdiPixelShaderConst(&Args, Values);
}

/* Every user clip plane cleared; hardware vertex processing sends each twice */
VOID
D9Device::SendClipPlanes(
    _In_ BOOLEAN Twice)
{
    for (UINT Plane = 0; Plane < Caps.MaxUserClipPlanes; Plane++)
    {
        D3DDDIARG_SETCLIPPLANE Args = {};

        Args.Index = Plane;
        DdiClipPlane(&Args);
        if (Twice)
            DdiClipPlane(&Args);
    }
}

/* What a new device tells its driver before the application draws: the fixed function pixel constants,
   viewport, render states, texture stages, samplers and clip planes, in the order the system sends them */
VOID
D9Device::SendInitialState(VOID)
{
    static const FLOAT Scales[8] = { 0.5f, 2.0f, 4.0f, 1.0f / 255.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    D3DDDIARG_SETPIXELSHADERCONST Scale = {};
    D3DDDIARG_WINFO WInfo = {};
    DWORD Factor = RenderStates[D3DRS_TEXTUREFACTOR];
    UINT Samplers[D9PixelSamplers + 1 + D9VertexSamplers];
    UINT SamplerCount = 0;

    Scale.Register = 0;
    Scale.Count = 2;
    DdiPixelShaderConst(&Scale, Scales);

    /* Drivers that leave threading to the runtime get their batch once the first viewport is in place */
    SendViewport();
    StartBatch();
    SendViewportRange();

    for (BYTE State : D9InitialPixelStates)
        SendRenderState(State);

    SendPixelConstant(D9ConstTextureFactor, ((Factor >> 16) & 0xFF) / 255.0f, ((Factor >> 8) & 0xFF) / 255.0f,
                      (Factor & 0xFF) / 255.0f, (Factor >> 24) / 255.0f);
    for (UINT Stage = 0; Stage < D9PixelStages; Stage++)
    {
        UINT Matrix = D9ConstBumpMatrix + Stage * 2;
        UINT Luminance = D9ConstLuminance + Stage / 2;

        /* Each bump matrix entry and luminance value refreshes the constant that holds it */
        SendPixelConstant(Matrix, 0.0f, 0.0f, 0.0f, 0.0f);
        SendTextureStageState(Stage, D3DDDITSS_BUMPENVMAT00, 0);
        SendPixelConstant(Matrix + 1, 0.0f, 0.0f, 0.0f, 0.0f);
        SendTextureStageState(Stage, D3DDDITSS_BUMPENVMAT01, 0);
        SendPixelConstant(Matrix, 0.0f, 0.0f, 0.0f, 0.0f);
        SendTextureStageState(Stage, D3DDDITSS_BUMPENVMAT10, 0);
        SendPixelConstant(Matrix + 1, 0.0f, 0.0f, 0.0f, 0.0f);
        SendTextureStageState(Stage, D3DDDITSS_BUMPENVMAT11, 0);
        SendTextureStageState(Stage, D3DDDITSS_TEXCOORDINDEX, Stage);
        SendPixelConstant(Luminance, 0.0f, 0.0f, 0.0f, 0.0f);
        SendTextureStageState(Stage, D3DDDITSS_BUMPENVLSCALE, 0);
        SendPixelConstant(Luminance, 0.0f, 0.0f, 0.0f, 0.0f);
        SendTextureStageState(Stage, D3DDDITSS_BUMPENVLOFFSET, 0);
        SendTextureStageState(Stage, D3DDDITSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        SendPixelConstant(D9ConstStageConstant + Stage, 0.0f, 0.0f, 0.0f, 0.0f);
    }

    for (UINT Sampler = 0; Sampler < D9PixelSamplers; Sampler++)
    {
        Samplers[SamplerCount] = Sampler;
        SamplerCount += 1;
    }
    if ((Caps.DevCaps2 & (D3DDEVCAPS2_DMAPNPATCH | D3DDEVCAPS2_PRESAMPLEDDMAPNPATCH)) != 0)
    {
        Samplers[SamplerCount] = D3DDMAPSAMPLER;
        SamplerCount += 1;
    }
    for (UINT Sampler = 0; Sampler < D9VertexSamplers; Sampler++)
    {
        Samplers[SamplerCount] = D3DVERTEXTEXTURESAMPLER0 + Sampler;
        SamplerCount += 1;
    }
    for (UINT Index = 0; Index < SamplerCount; Index++)
    {
        for (const auto& State : D9InitialSamplerStates)
            SendTextureStageState(Samplers[Index], State.State, State.Value);
    }

    WInfo.WNear = 1.0f;
    WInfo.WFar = 1.0f;
    SendRenderState(D3DRS_CLIPPING);
    if ((BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) != 0)
    {
        for (BYTE State : D9InitialVertexStates)
            SendRenderState(State);
        SendRenderState(D3DRS_CLIPPLANEENABLE);
        SendClipPlanes(FALSE);
        for (BYTE State : D9InitialSoftwareLaterStates)
            SendRenderState(State);
        DdiWInfo(&WInfo);
        return;
    }

    /* The runtime's vertex shaders see the range fog and clip plane states again */
    SendRenderState(D3DRS_CLIPPLANEENABLE);
    SendClipPlanes(TRUE);
    for (BYTE State : D9InitialLaterStates)
        SendRenderState(State);
    DdiWInfo(&WInfo);
    SendRenderState(D3DRS_RANGEFOGENABLE);
    SendRenderState(D3DRS_CLIPPLANEENABLE);
    SendClipPlanes(TRUE);
}

/* The runtime's buffers for drawing from application memory: a vertex buffer that may be read back, two
   write only ones and an index buffer */
HRESULT
D9Device::CreateStagingBuffers(VOID)
{
    static const struct
    {
        D3DDDIFORMAT Format;
        UINT Size;
        UINT Flags;
    } Buffers[D9StagingBufferCount] =
    {
        { D3DDDIFMT_VERTEXDATA, 0x10000, 0x02080004 },
        { D3DDDIFMT_VERTEXDATA, 0x10000, 0x02080044 },
        { D3DDDIFMT_VERTEXDATA, 0x10000, 0x02080044 },
        { D3DDDIFMT_INDEX16, 0x1000, 0x02100044 },
    };
    HRESULT hr;

    for (UINT Index = 0; Index < D9StagingBufferCount; Index++)
    {
        hr = CreateDriverBuffer(Buffers[Index].Format, Buffers[Index].Size, Buffers[Index].Flags,
                                &StagingBuffers[Index]);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* The system frees these in this order */
VOID
D9Device::DestroyStagingBuffers(VOID)
{
    static const BYTE Order[D9StagingBufferCount] = { 1, 0, 3, 2 };

    for (BYTE Index : Order)
        DestroyDriverSurface(&StagingBuffers[Index]);
}

HRESULT
D9Device::Initialize(
    _Inout_ D3DPRESENT_PARAMETERS* Parameters)
{
    HRESULT hr;

    if (D9DriverUpdating(Adapter->KernelAdapter))
    {
        D9Trace(L"the driver of %S is being updated", Adapter->DeviceName);
        return D3DERR_NOTAVAILABLE;
    }
    hr = CreateKernelDevice();
    if (FAILED(hr))
        return hr;
    hr = OpenDriverAdapter();
    if (FAILED(hr))
        return hr;
    hr = CreateDriverDevice(Parameters->Windowed != FALSE);
    if (FAILED(hr))
        return hr;
    hr = ReadDriverAnswers();
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
    hr = CreateStagingBuffers();
    if (FAILED(hr))
        return hr;

    BindRenderTargets();
    SetDefaultState(Parameters);
    SendInitialState();
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
    if (NotReset)
        return D3DERR_DEVICENOTRESET;
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

    if (NotReset)
        return D3DERR_INVALIDCALL;
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
    if ((Flags & D3DCLEAR_STENCIL) != 0 && !D9HasStencil(DepthStencil->Describe().Format))
    {
        D9_TRACE_ONCE(L"Clear: the depth buffer has no stencil bits");
        return D3DERR_INVALIDCALL;
    }

    /* The driver always gets rectangles; without any the viewport is the one */
    SetRect(&Viewport, 0, 0, Parameters.BackBufferWidth, Parameters.BackBufferHeight);
    if (Count == 0 || Rects == nullptr)
    {
        Count = 1;
        Rects = nullptr;
    }
    Clipped = new (std::nothrow) RECT[Count];
    if (Clipped == nullptr)
        return E_OUTOFMEMORY;
    if (Rects == nullptr)
    {
        Clipped[0] = Viewport;
        Kept = 1;
    }
    else
    {
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
    SendTargets();
    hr = DdiClear(&Args, Kept, Clipped);
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

    if (static_cast<DWORD>(State) >= D9RenderStateCount)
    {
        D9_TRACE_ONCE(L"SetRenderState: state %u is not valid", State);
        return D3DERR_INVALIDCALL;
    }

    /* Retired states are accepted and dropped */
    if (D9IsRetiredState(State))
        return S_OK;

    RenderStates[State] = Value;
    Args.State = static_cast<D3DDDIRENDERSTATETYPE>(State);
    Args.Value = Value;
    DdiRenderState(&Args);
    return S_OK;
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

    /* The extended device looks at its state first: a removed device stays removed, a failing one rereads its caps */
    if (IsEx)
    {
        hr = QueryDeviceState(NULL);
        if (hr == D3DERR_DEVICEREMOVED)
            return hr;
        if (FAILED(hr))
        {
            D9Adapter* Answers = D9CopyAdapter(Adapter);

            if (Answers != nullptr)
            {
                Answers->Functions = AdapterFunctions;
                Answers->DriverAdapter = DriverAdapter;
                D9RereadDriverCaps(Answers, IsEx);
                D9FreeDriverAnswers(Answers);
                delete Answers;
            }
        }
    }

    hr = Enum->ValidateReset(Adapter->Ordinal, DeviceType, BehaviorFlags, Parameters, FullscreenMode);
    if (FAILED(hr))
        return hr;
    if (!Parameters->Windowed)
    {
        D9_TRACE_ONCE(L"Reset: fullscreen devices are not implemented");
        return D3DERR_NOTAVAILABLE;
    }

    /* The plain object's devices cannot reset while the application holds default pool resources or the buffers
       of the swap chain; the device then waits for a reset that succeeds */
    if (!IsEx && (DefaultResources > 0 || ImplicitChain->BuffersHeld() ||
                  (AutoDepthStencil != nullptr && AutoDepthStencil->IsHeld())))
    {
        D9Trace(L"Reset: default pool resources are still alive");
        NotReset = TRUE;
        return D3DERR_INVALIDCALL;
    }

    /* The depth buffer goes before the back buffers, then the device gives up any display it owned; the plain
       object's device also starts over with its batch and drawing buffers */
    if (!IsEx)
        StopBatch();
    for (D9Surface*& Bound : RenderTargets)
        Bound = nullptr;
    DepthStencil = nullptr;
    if (AutoDepthStencil != nullptr)
        AutoDepthStencil->Abandon();
    AutoDepthStencil = nullptr;
    ImplicitChain->ReleaseBuffers();
    if (!IsEx)
        DestroyStagingBuffers();
    ReleaseVidPnOwnership();

    hr = ImplicitChain->Reset(Parameters);
    if (SUCCEEDED(hr))
        hr = CreateAutoDepthStencil(Parameters);
    if (SUCCEEDED(hr) && !IsEx)
        hr = CreateStagingBuffers();
    if (FAILED(hr))
    {
        Lost = TRUE;
        return hr;
    }

    NotReset = FALSE;
    BindRenderTargets();
    if (IsEx)
    {
        SendViewport();
        SetDefaultState(Parameters);
        return S_OK;
    }

    SetDefaultState(Parameters);
    SendInitialState();
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

/* What the kernel says about the device running: a stopped device is removed, a faulted one hung */
HRESULT
D9Device::ExecutionStatus(VOID)
{
    typedef NTSTATUS (APIENTRY* PD9_GET_DEVICE_STATE)(D3DKMT_GETDEVICESTATE*);
    static PD9_GET_DEVICE_STATE GetState = reinterpret_cast<PD9_GET_DEVICE_STATE>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTGetDeviceState"));
    D3DKMT_GETDEVICESTATE State = {};

    State.hDevice = DeviceHandle;
    State.StateType = D3DKMT_DEVICESTATE_EXECUTION;
    if (GetState != nullptr && NT_SUCCESS(GetState(&State)))
        ExecutionState = State.ExecutionState;

    switch (ExecutionState)
    {
    case D3DKMT_DEVICEEXECUTION_STOPPED:
        return D3DERR_DEVICEREMOVED;
    case D3DKMT_DEVICEEXECUTION_HUNG:
    case D3DKMT_DEVICEEXECUTION_ERROR_OUTOFMEMORY:
    case D3DKMT_DEVICEEXECUTION_ERROR_DMAFAULT:
    case D3DKMT_DEVICEEXECUTION_ERROR_DMAPAGEFAULT:
        return D3DERR_DEVICEHUNG;
    default:
        return S_OK;
    }
}

/* Whether the device window can be seen; with no window, whether the display is on and not taken */
HRESULT
D9Device::QueryDeviceState(
    _In_opt_ HWND DestinationWindow)
{
    typedef BOOLEAN (APIENTRY* PD9_CHECK_EXCLUSIVE)(VOID);
    typedef NTSTATUS (APIENTRY* PD9_CHECK_POWER)(const D3DKMT_CHECKMONITORPOWERSTATE*);
    static PD9_CHECK_EXCLUSIVE CheckExclusive = reinterpret_cast<PD9_CHECK_EXCLUSIVE>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTCheckExclusiveOwnership"));
    static PD9_CHECK_POWER CheckPower = reinterpret_cast<PD9_CHECK_POWER>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTCheckMonitorPowerState"));
    D3DKMT_CHECKMONITORPOWERSTATE Power = {};
    BOOLEAN Occluded = FALSE;
    BOOLEAN Showing = FALSE;
    RECT Client = {};
    NTSTATUS Status;
    HRESULT hr;

    if (Lost)
        return D3DERR_DEVICELOST;

    hr = ExecutionStatus();
    if (FAILED(hr))
    {
        Lost = TRUE;
        return hr;
    }

    /* A windowed device has no exclusive display, so only the window decides */
    if (DestinationWindow != NULL)
    {
        if (GetClientRect(DestinationWindow, &Client) && (Client.left >= Client.right || Client.top >= Client.bottom))
            return S_PRESENT_OCCLUDED;
        hr = D9CheckOcclusion(DestinationWindow);
        if (FAILED(hr) || hr == S_PRESENT_OCCLUDED)
            return hr;
        return ModeChanged ? S_PRESENT_MODE_CHANGED : S_OK;
    }

    if (CheckExclusive != nullptr && CheckExclusive())
        return S_PRESENT_OCCLUDED;

    Power.hAdapter = Adapter->KernelAdapter;
    Power.VidPnSourceId = Adapter->SourceId;
    Status = CheckPower != nullptr ? CheckPower(&Power) : STATUS_NOT_SUPPORTED;
    if (Status == STATUS_GRAPHICS_PRESENT_MODE_CHANGED)
        ModeChanged = TRUE;
    else if (Status == STATUS_GRAPHICS_PRESENT_OCCLUDED || Status == STATUS_GRAPHICS_LINK_CONFIGURATION_IN_PROGRESS)
        Occluded = TRUE;
    else if (Status == STATUS_NOT_SUPPORTED)
        Showing = TRUE;
    else
    {
        hr = D9HresultFromStatus(Status);
        if (FAILED(hr))
            return hr;
        Showing = TRUE;
    }

    if (Occluded && !Showing)
        return S_PRESENT_OCCLUDED;
    return ModeChanged ? S_PRESENT_MODE_CHANGED : S_OK;
}

STDMETHODIMP
D9Device::CheckDeviceState(
    _In_opt_ HWND DestinationWindow)
{
    D9_DEVICE_LOCK();

    if (!IsEx)
        return D3DERR_INVALIDCALL;
    return QueryDeviceState(DestinationWindow);
}

/* Gives up every display source the device owned; windowed devices own none, so this only tells the kernel */
VOID
D9Device::ReleaseVidPnOwnership(VOID)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_SETVIDPNSOURCEOWNER1 Owner = {};
    NTSTATUS Status;

    if (Kmt == nullptr || Kmt->SetVidPnSourceOwner1 == nullptr)
        return;

    Owner.Version0.hDevice = DeviceHandle;
    Status = Kmt->SetVidPnSourceOwner1(&Owner);
    if (!NT_SUCCESS(Status))
        D9Trace(L"releasing the display sources failed 0x%08lX", Status);
}

STDMETHODIMP
D9Device::GetGPUThreadPriority(
    _Out_ INT* Priority)
{
    D9_DEVICE_LOCK();

    if (!IsEx)
        return D3DERR_INVALIDCALL;

    if (Lost)
    {
        *Priority = GpuPriority;
        return S_OK;
    }

    /* Queued work goes to the driver first; the kernel holds the value once the driver has a context */
    FlushDriver();
    if (KernelContextCount != 0)
    {
        D3DKMT_GETCONTEXTSCHEDULINGPRIORITY Query = {};
        NTSTATUS Status;

        Query.hContext = KernelContexts[KernelContextCount - 1];
        Status = RxGetKmtThunks()->GetContextSchedulingPriority(&Query);
        if (!NT_SUCCESS(Status))
        {
            D9Trace(L"D3DKMTGetContextSchedulingPriority failed 0x%08lX", Status);
            return D9HresultFromStatus(Status);
        }
        *Priority = Query.Priority;
        return S_OK;
    }

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

    for (UINT Remaining = Lost ? 0 : KernelContextCount; Remaining > 0; Remaining -= 1)
    {
        D3DKMT_SETCONTEXTSCHEDULINGPRIORITY Set = {};
        NTSTATUS Status;

        Set.hContext = KernelContexts[Remaining - 1];
        Set.Priority = Priority;
        Status = RxGetKmtThunks()->SetContextSchedulingPriority(&Set);
        if (!NT_SUCCESS(Status))
        {
            D9Trace(L"D3DKMTSetContextSchedulingPriority(%d) failed 0x%08lX", Priority, Status);
            return D9HresultFromStatus(Status);
        }
    }

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
    if (MaxLatency > D3DPRESENT_BACK_BUFFERS_MAX_EX)
    {
        D9_TRACE_ONCE(L"SetMaximumFrameLatency: %u is more than the back buffer limit", MaxLatency);
        return D3DERR_INVALIDCALL;
    }

    /* Zero restores the kernel's default queue of three presents */
    if (!Lost)
    {
        D3DKMT_SETQUEUEDLIMIT Limit = {};
        NTSTATUS Status;

        Limit.hDevice = DeviceHandle;
        Limit.Type = D3DKMT_SET_QUEUEDLIMIT_PRESENT;
        Limit.QueuedPresentLimit = MaxLatency != 0 ? MaxLatency : 3;
        Status = RxGetKmtThunks()->SetQueuedLimit(&Limit);
        if (!NT_SUCCESS(Status))
        {
            D9Trace(L"D3DKMTSetQueuedLimit(%u) failed 0x%08lX", Limit.QueuedPresentLimit, Status);
            return D9HresultFromStatus(Status);
        }
    }

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

/* The kernel's segment sizes together, capped at four gigabytes and rounded to whole megabytes */
STDMETHODIMP_(UINT)
D9Device::GetAvailableTextureMem()
{
    static constexpr UINT RoundedCeiling = 0xFFF80000;
    UINT64 Available = Adapter->VideoMemory;

    D9_DEVICE_LOCK();

    if (Available > MAXUINT)
        Available = MAXUINT;
    if (Available >= RoundedCeiling)
        return 0xFFF00000;
    return (static_cast<UINT>(Available) + 0x80000) & 0xFFF00000;
}

/* Entry points that are not implemented yet */

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

/* Usage bits the extended surface creation calls accept */
static constexpr DWORD D9SurfaceUsageAllowed = D3DUSAGE_RESTRICTED_CONTENT | D3DUSAGE_RESTRICT_SHARED_RESOURCE |
                                               D3DUSAGE_RESTRICT_SHARED_RESOURCE_DRIVER | D3DUSAGE_NONSECURE;

/* Usage bits only a surface made to be shared may carry */
static constexpr DWORD D9SurfaceUsageShared = D3DUSAGE_RESTRICT_SHARED_RESOURCE |
                                              D3DUSAGE_RESTRICT_SHARED_RESOURCE_DRIVER | D3DUSAGE_NONSECURE;

/* The size of the system memory a surface needs stays within two gigabytes */
static constexpr UINT64 D9SurfaceMemoryLimit = 0x80000000ULL;

/* Depth formats the application can never lock */
BOOLEAN
D9IsHiddenDepth(
    _In_ D3DFORMAT Format)
{
    switch (Format)
    {
    case D3DFMT_D32:
    case D3DFMT_D15S1:
    case D3DFMT_D24S8:
    case D3DFMT_D24X8:
    case D3DFMT_D24X4S4:
    case D3DFMT_D16:
    case D3DFMT_D24FS8:
        return TRUE;
    default:
        return FALSE;
    }
}

BOOLEAN
D9IsDxtFormat(
    _In_ D3DFORMAT Format)
{
    return Format == D3DFMT_DXT1 || Format == D3DFMT_DXT2 || Format == D3DFMT_DXT3 || Format == D3DFMT_DXT4 ||
           Format == D3DFMT_DXT5;
}

/* Asks the object whether surfaces of a format and usage work under the display format */
HRESULT
D9Device::CheckSurfaceFormat(
    _In_ DWORD Usage,
    _In_ D3DFORMAT Format)
{
    D3DDISPLAYMODE Mode = {};

    Enum->GetAdapterDisplayMode(Adapter->Ordinal, &Mode);
    return Enum->CheckDeviceFormat(Adapter->Ordinal, DeviceType, Mode.Format, Usage, D3DRTYPE_SURFACE, Format);
}

/* Restricted content needs a driver that protects content; its caps are asked for once */
HRESULT
D9Device::CheckProtectedContent(VOID)
{
    DDICONTENTPROTECTIONCAPS Info = {};
    D3DCONTENTPROTECTIONCAPS Protection = {};
    D3DDDIARG_GETCAPS Args = {};

    if (!ProtectionQueried)
    {
        ProtectionQueried = TRUE;
        Args.Type = D3DDDICAPS_GETCONTENTPROTECTIONCAPS;
        Args.pInfo = &Info;
        Args.pData = &Protection;
        Args.DataSize = sizeof(Protection);
        if (SUCCEEDED(AdapterFunctions.pfnGetCaps(DriverAdapter, &Args)))
            ProtectionSupported = (Protection.Caps & (D3DCPCAPS_SOFTWARE | D3DCPCAPS_HARDWARE)) != 0;
    }
    if (!ProtectionSupported)
    {
        D9_TRACE_ONCE(L"the driver cannot protect restricted content");
        return D3DERR_INVALIDCALL;
    }
    return S_OK;
}

/* Wraps a description in a surface object the application owns */
HRESULT
D9Device::AddSurface(
    _In_ const D3DSURFACE_DESC* Desc,
    _In_ BOOLEAN Lockable,
    _In_ BOOLEAN Discard,
    _Out_ IDirect3DSurface9** Surface)
{
    D9Surface* Made = new (std::nothrow) D9Surface(this, nullptr, Desc, Lockable, Discard, TRUE);
    HRESULT hr;

    if (Made == nullptr)
        return E_OUTOFMEMORY;

    hr = Made->Create();
    if (FAILED(hr))
    {
        D9Trace(L"a surface of format %u could not be made (0x%08lX)", Desc->Format, hr);
        delete Made;
        return hr;
    }
    if (Desc->Pool == D3DPOOL_DEFAULT)
        Made->CountAsDefault();
    Made->AddRef();
    *Surface = Made;
    return S_OK;
}

/* Render targets and depth stencil surfaces share their checks; Usage says which one */
HRESULT
D9Device::CreateTargetSurface(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ D3DFORMAT Format,
    _In_ DWORD Usage,
    _In_ D3DMULTISAMPLE_TYPE MultiSample,
    _In_ DWORD MultisampleQuality,
    _In_ BOOLEAN Lockable,
    _In_ BOOLEAN Discard,
    _Out_ IDirect3DSurface9** Surface,
    _Inout_opt_ HANDLE* SharedHandle)
{
    D3DSURFACE_DESC Desc = {};
    DWORD Levels = 0;
    HRESULT hr;

    if (SharedHandle != nullptr)
    {
        if (!IsEx)
        {
            D9_TRACE_ONCE(L"only extended devices share surfaces");
            return D3DERR_INVALIDCALL;
        }
        RX_UNIMPLEMENTED();
        return D3DERR_INVALIDCALL;
    }
    if (D9IsDxtFormat(Format) && ((Width % 4) != 0 || (Height % 4) != 0))
        return D3DERR_INVALIDCALL;
    if (Width == 0 || Height == 0)
        return D3DERR_INVALIDCALL;
    if (FAILED(CheckSurfaceFormat(Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL), Format)))
    {
        D9_TRACE_ONCE(L"format %u cannot make this kind of surface", Format);
        return D3DERR_INVALIDCALL;
    }
    if ((Usage & D9SurfaceUsageShared) != 0)
        return D3DERR_INVALIDCALL;

    if ((Usage & D3DUSAGE_DEPTHSTENCIL) != 0)
        Lockable = !D9IsHiddenDepth(Format);
    if (MultiSample != D3DMULTISAMPLE_NONE && Lockable)
    {
        D9_TRACE_ONCE(L"multisampled surfaces cannot be lockable");
        return D3DERR_INVALIDCALL;
    }

    if (MultiSample != D3DMULTISAMPLE_NONE)
    {
        hr = Enum->CheckDeviceMultiSampleType(Adapter->Ordinal, DeviceType, Format,
                                              ImplicitChain->PresentParameters().Windowed, MultiSample, &Levels);
        if (FAILED(hr) || MultisampleQuality >= Levels)
        {
            D9_TRACE_ONCE(L"multisample type %u quality %lu is not supported", MultiSample, MultisampleQuality);
            return D3DERR_INVALIDCALL;
        }
    }
    if ((Usage & D3DUSAGE_RESTRICTED_CONTENT) != 0)
    {
        hr = CheckProtectedContent();
        if (FAILED(hr))
            return hr;
    }

    Desc.Format = Format;
    Desc.Type = D3DRTYPE_SURFACE;
    Desc.Usage = Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
    Desc.Pool = D3DPOOL_DEFAULT;
    Desc.MultiSampleType = MultiSample;
    Desc.MultiSampleQuality = MultisampleQuality;
    Desc.Width = Width;
    Desc.Height = Height;
    return AddSurface(&Desc, Lockable, Discard, Surface);
}

/* Offscreen plain surfaces; scratch ones only need a format the runtime knows the size of */
HRESULT
D9Device::CreatePlainSurface(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ D3DFORMAT Format,
    _In_ D3DPOOL Pool,
    _In_ DWORD Usage,
    _Out_ IDirect3DSurface9** Surface,
    _Inout_opt_ HANDLE* SharedHandle)
{
    D3DSURFACE_DESC Desc = {};
    UINT64 Pitch;

    if (Format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    if (Pool != D3DPOOL_DEFAULT && Pool != D3DPOOL_SYSTEMMEM && Pool != D3DPOOL_SCRATCH)
    {
        D9_TRACE_ONCE(L"CreateOffscreenPlainSurface: pool %u is not allowed", Pool);
        return D3DERR_INVALIDCALL;
    }
    if (Pool == D3DPOOL_SCRATCH ? D9PixelStride(Format) == 0 : FAILED(CheckSurfaceFormat(0, Format)))
    {
        D9_TRACE_ONCE(L"CreateOffscreenPlainSurface: format %u is not supported", Format);
        return D3DERR_INVALIDCALL;
    }
    if (Format == D3DFMT_BINARYBUFFER && Height > 1)
        return D3DERR_INVALIDCALL;

    if (SharedHandle != nullptr)
    {
        if (!IsEx)
        {
            D9_TRACE_ONCE(L"only extended devices share surfaces");
            return D3DERR_INVALIDCALL;
        }
        RX_UNIMPLEMENTED();
        return D3DERR_INVALIDCALL;
    }
    if (D9IsHiddenDepth(Format))
        return D3DERR_INVALIDCALL;
    if (D9IsDxtFormat(Format) && ((Width % 4) != 0 || (Height % 4) != 0))
        return D3DERR_INVALIDCALL;
    if (Width == 0 || Height == 0)
        return D3DERR_INVALIDCALL;
    if ((Usage & ~D9SurfaceUsageAllowed) != 0 || (Usage & D9SurfaceUsageShared) != 0)
        return D3DERR_INVALIDCALL;

    if (Pool == D3DPOOL_SYSTEMMEM)
    {
        Pitch = D9MemoryPitch(Format, Width);
        if (Pitch > D9SurfaceMemoryLimit || Pitch * D9MemoryRows(Format, Height) > D9SurfaceMemoryLimit)
            return D3DERR_INVALIDCALL;
    }
    if (Pool != D3DPOOL_SCRATCH && (Usage & D3DUSAGE_RESTRICTED_CONTENT) != 0)
    {
        HRESULT hr = CheckProtectedContent();

        if (FAILED(hr))
            return hr;
    }

    Desc.Format = Format;
    Desc.Type = D3DRTYPE_SURFACE;
    Desc.Pool = Pool;
    Desc.Width = Width;
    Desc.Height = Height;
    return AddSurface(&Desc, TRUE, FALSE, Surface);
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
    D9_DEVICE_LOCK();

    *Surface = nullptr;
    if (Format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    return CreateTargetSurface(Width, Height, Format, D3DUSAGE_RENDERTARGET, MultiSample, MultisampleQuality,
                               Lockable != FALSE, FALSE, Surface, SharedHandle);
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
    D9_DEVICE_LOCK();

    if ((Usage & ~D9SurfaceUsageAllowed) != 0)
    {
        D9_TRACE_ONCE(L"CreateRenderTargetEx: invalid usage 0x%08lX", Usage);
        return D3DERR_INVALIDCALL;
    }
    *Surface = nullptr;
    if (Format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;
    return CreateTargetSurface(Width, Height, Format, Usage | D3DUSAGE_RENDERTARGET, MultiSample,
                               MultisampleQuality, Lockable != FALSE, FALSE, Surface, SharedHandle);
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
    return CreateDepthStencilSurfaceEx(Width, Height, Format, MultiSample, MultisampleQuality, Discard, Surface,
                                       SharedHandle, 0);
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
    D9_DEVICE_LOCK();

    if ((Usage & ~D9SurfaceUsageAllowed) != 0)
    {
        D9_TRACE_ONCE(L"CreateDepthStencilSurfaceEx: invalid usage 0x%08lX", Usage);
        return D3DERR_INVALIDCALL;
    }
    *Surface = nullptr;
    if (Format == D3DFMT_UNKNOWN)
        return D3DERR_INVALIDCALL;

    /* Discarded depth contents can never be read back */
    if (Discard && !D9IsHiddenDepth(Format))
    {
        D9_TRACE_ONCE(L"a discardable depth stencil surface cannot be lockable");
        return D3DERR_INVALIDCALL;
    }
    return CreateTargetSurface(Width, Height, Format, Usage | D3DUSAGE_DEPTHSTENCIL, MultiSample,
                               MultisampleQuality, FALSE, Discard != FALSE, Surface, SharedHandle);
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
    return CreateOffscreenPlainSurfaceEx(Width, Height, Format, Pool, Surface, SharedHandle, 0);
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
    HRESULT hr;

    D9_DEVICE_LOCK();

    hr = CreatePlainSurface(Width, Height, Format, Pool, Usage, Surface, SharedHandle);
    if (FAILED(hr))
        *Surface = nullptr;
    return hr;
}

/* Usage bits vertex and index buffers accept */
static constexpr DWORD D9BufferUsageAllowed = D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING | D3DUSAGE_DONOTCLIP |
                                              D3DUSAGE_POINTS | D3DUSAGE_RTPATCHES | D3DUSAGE_NPATCHES |
                                              D3DUSAGE_DYNAMIC | D3DUSAGE_RESTRICTED_CONTENT |
                                              D3DUSAGE_RESTRICT_SHARED_RESOURCE |
                                              D3DUSAGE_RESTRICT_SHARED_RESOURCE_DRIVER | D3DUSAGE_NONSECURE |
                                              D3DUSAGE_TEXTAPI;

/* Driver caps bits that say buffers may live in video memory; GetDeviceCaps never shows them */
static constexpr DWORD D9DevCapsVideoVertexBuffers = 0x02000000;
static constexpr DWORD D9DevCapsVideoIndexBuffers = 0x04000000;

/* Another spelling of the managed pool; the extended object accepts it and reports it back as given */
static constexpr D3DPOOL D9PoolManagedAlias = static_cast<D3DPOOL>(6);

/* Bytes one vertex of a flexible vertex format takes */
static
UINT
D9VertexSize(
    _In_ DWORD Fvf)
{
    static const BYTE PositionBytes[] = { 0, 12, 16, 16, 20, 24, 28, 32 };
    UINT Size = PositionBytes[(Fvf & D3DFVF_XYZB5) >> 1];
    UINT Textures = (Fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;

    if ((Fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZW)
        Size = 16;
    if ((Fvf & D3DFVF_NORMAL) != 0)
        Size += 12;
    if ((Fvf & D3DFVF_PSIZE) != 0)
        Size += 4;
    if ((Fvf & D3DFVF_DIFFUSE) != 0)
        Size += 4;
    if ((Fvf & D3DFVF_SPECULAR) != 0)
        Size += 4;

    /* Each set's coordinate count sits in two bits from bit 16; zero stands for two */
    for (UINT Set = 0; Set < Textures; Set++)
    {
        static const BYTE CoordBytes[] = { 8, 12, 16, 4 };

        Size += CoordBytes[(Fvf >> (16 + Set * 2)) & 3];
    }
    return Size;
}

/* Where a buffer lives: software vertex processing and missing driver support keep it in system memory */
D3DDDI_POOL
D9Device::BufferPlacement(
    _In_ BOOLEAN IsVertex,
    _In_ DWORD Usage,
    _In_ DWORD Fvf,
    _In_ D3DPOOL Pool) const
{
    DWORD DevCaps = Adapter->DriverCaps.DevCaps;
    BOOLEAN Software = (BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) != 0;
    BOOLEAN Transformed = IsVertex && (Fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;

    if (Pool != D3DPOOL_DEFAULT)
        return D3DDDIPOOL_SYSTEMMEM;
    if ((DevCaps & (IsVertex ? D9DevCapsVideoVertexBuffers : D9DevCapsVideoIndexBuffers)) == 0)
        return D3DDDIPOOL_SYSTEMMEM;

    /* Clipped vertices go through the runtime's pipeline, which reads them from system memory */
    if (Software || (Usage & D3DUSAGE_SOFTWAREPROCESSING) != 0)
    {
        if (IsVertex ? !(Transformed && (Usage & D3DUSAGE_DONOTCLIP) != 0) : (Usage & D3DUSAGE_DONOTCLIP) == 0)
            return D3DDDIPOOL_SYSTEMMEM;
    }
    if ((Usage & D3DUSAGE_NPATCHES) != 0 && (DevCaps & D3DDEVCAPS_NPATCHES) == 0)
        return D3DDDIPOOL_SYSTEMMEM;
    return D3DDDIPOOL_VIDEOMEMORY;
}

/* Makes the buffer, trying system memory when the driver could not make it in video memory; Pool is what
   the description reports and Placed the pool it behaves as */
template <class Buffer>
HRESULT
D9Device::AddBuffer(
    _In_ UINT Length,
    _In_ DWORD Usage,
    _In_ D3DFORMAT Format,
    _In_ DWORD Fvf,
    _In_ D3DPOOL Pool,
    _In_ D3DPOOL Placed,
    _Outptr_ Buffer** Made)
{
    D3DDDI_POOL Placement = BufferPlacement(Format == D3DFMT_VERTEXDATA, Usage, Fvf, Placed);
    Buffer* Object;
    HRESULT hr;

    if ((Usage & D3DUSAGE_RESTRICTED_CONTENT) != 0)
    {
        hr = CheckProtectedContent();
        if (FAILED(hr))
            return hr;
    }

    Object = new (std::nothrow) Buffer(this, Length, Usage, Format, Fvf, Pool);
    if (Object == nullptr)
        return E_OUTOFMEMORY;

    hr = Object->Create(Placement);
    if (FAILED(hr) && Placement != D3DDDIPOOL_SYSTEMMEM && hr != D3DERR_OUTOFVIDEOMEMORY)
    {
        delete Object;
        Object = new (std::nothrow) Buffer(this, Length, Usage, Format, Fvf, Pool);
        if (Object == nullptr)
            return E_OUTOFMEMORY;
        hr = Object->Create(D3DDDIPOOL_SYSTEMMEM);
    }
    if (FAILED(hr))
    {
        D9Trace(L"a buffer of %u bytes could not be made (0x%08lX)", Length, hr);
        delete Object;
        return hr;
    }
    Object->AddRef();
    *Made = Object;
    return S_OK;
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
    D9VertexBuffer* Made = nullptr;
    HRESULT hr;

    D9_DEVICE_LOCK();

    D3DPOOL Placed = Pool == D9PoolManagedAlias ? D3DPOOL_MANAGED : Pool;

    if (Pool == D3DPOOL_MANAGED && Enum->IsExtended())
    {
        D9_TRACE_ONCE(L"CreateVertexBuffer: the extended object has no managed pool");
        return D3DERR_INVALIDCALL;
    }
    if (SharedHandle == nullptr)
        *VertexBuffer = nullptr;
    if ((Usage & ~D9BufferUsageAllowed) != 0)
    {
        D9_TRACE_ONCE(L"CreateVertexBuffer: invalid usage 0x%08lX", Usage);
        return D3DERR_INVALIDCALL;
    }

    *VertexBuffer = nullptr;
    if (Length == 0 || Placed > D3DPOOL_SYSTEMMEM)
        return D3DERR_INVALIDCALL;
    if ((Usage & D3DUSAGE_SOFTWAREPROCESSING) != 0 &&
        (BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)) == 0)
    {
        D9_TRACE_ONCE(L"CreateVertexBuffer: software processing needs a mixed or software device");
        return D3DERR_INVALIDCALL;
    }
    if ((Usage & D3DUSAGE_DYNAMIC) != 0 && Placed == D3DPOOL_MANAGED)
        return D3DERR_INVALIDCALL;
    if ((Usage & D9SurfaceUsageShared) != 0)
        return D3DERR_INVALIDCALL;
    if (FVF != 0 && Length < D9VertexSize(FVF))
    {
        D9_TRACE_ONCE(L"CreateVertexBuffer: %u bytes do not hold one vertex", Length);
        return D3DERR_INVALIDCALL;
    }
    if (SharedHandle != nullptr)
    {
        RX_UNIMPLEMENTED();
        return D3DERR_NOTAVAILABLE;
    }

    hr = AddBuffer(Length, Usage, D3DFMT_VERTEXDATA, FVF, Enum->IsExtended() ? Pool : Placed, Placed, &Made);
    if (SUCCEEDED(hr))
        *VertexBuffer = Made;
    return hr;
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
    D3DPOOL Placed = Pool == D9PoolManagedAlias ? D3DPOOL_MANAGED : Pool;
    D9IndexBuffer* Made = nullptr;
    HRESULT hr;

    D9_DEVICE_LOCK();

    if (SharedHandle == nullptr)
        *IndexBuffer = nullptr;
    if ((Usage & ~D9BufferUsageAllowed) != 0)
    {
        D9_TRACE_ONCE(L"CreateIndexBuffer: invalid usage 0x%08lX", Usage);
        return D3DERR_INVALIDCALL;
    }
    if (Pool == D3DPOOL_MANAGED && Enum->IsExtended())
    {
        D9_TRACE_ONCE(L"CreateIndexBuffer: the extended object has no managed pool");
        return D3DERR_INVALIDCALL;
    }

    *IndexBuffer = nullptr;
    if ((Format != D3DFMT_INDEX16 && Format != D3DFMT_INDEX32) || Length < (Format == D3DFMT_INDEX16 ? 2U : 4U))
        return D3DERR_INVALIDCALL;
    if (Placed > D3DPOOL_SYSTEMMEM)
        return D3DERR_INVALIDCALL;
    if ((Usage & D3DUSAGE_SOFTWAREPROCESSING) != 0 &&
        (BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)) == 0)
    {
        D9_TRACE_ONCE(L"CreateIndexBuffer: software processing needs a mixed or software device");
        return D3DERR_INVALIDCALL;
    }
    if ((Usage & D3DUSAGE_DYNAMIC) != 0 && Placed == D3DPOOL_MANAGED)
        return D3DERR_INVALIDCALL;
    if ((Usage & D9SurfaceUsageShared) != 0)
        return D3DERR_INVALIDCALL;
    if (SharedHandle != nullptr)
    {
        RX_UNIMPLEMENTED();
        return D3DERR_NOTAVAILABLE;
    }

    hr = AddBuffer(Length, Usage, Format, 0, Enum->IsExtended() ? Pool : Placed, Placed, &Made);
    if (SUCCEEDED(hr))
        *IndexBuffer = Made;
    return hr;
}

STDMETHODIMP
D9Device::GetFrontBufferData(
    _In_ UINT SwapChain,
    _Out_ IDirect3DSurface9* DestSurface)
{
    D9_DEVICE_LOCK();

    if (NotReset || SwapChain != 0)
        return D3DERR_INVALIDCALL;
    return ImplicitChain->GetFrontBufferData(DestSurface);
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
    D9_DEVICE_LOCK();
    D3DDDIARG_RENDERSTATE Capture = {};

    if (NotReset || InScene)
        return D3DERR_INVALIDCALL;

    /* The driver hears of a scene through the scene capture state */
    Capture.State = D3DDDIRS_SCENECAPTURE;
    Capture.Value = TRUE;
    DdiRenderState(&Capture);
    InScene = TRUE;
    return S_OK;
}

STDMETHODIMP
D9Device::EndScene()
{
    D9_DEVICE_LOCK();
    D3DDDIARG_RENDERSTATE Capture = {};

    if (NotReset || !InScene)
        return D3DERR_INVALIDCALL;

    InScene = FALSE;
    Capture.State = D3DDDIRS_SCENECAPTURE;
    Capture.Value = FALSE;
    DdiRenderState(&Capture);
    return S_OK;
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
