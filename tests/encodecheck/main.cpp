/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runs what OBS Studio's hardware encoders do with a Direct3D 11 device on one runtime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * encodecheck [-runtime system|reactx]
 *
 * Encoded bitstreams differ from run to run on hardware, so only their sizes are printed.
 */

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <codecapi.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

static PFN_D3D11_CREATE_DEVICE EcCreateDevice;

/* The frame every path encodes */
static constexpr UINT EcWidth = 640;
static constexpr UINT EcHeight = 480;
static constexpr UINT EcFrames = 8;

static
VOID
EcReport(
    _In_z_ PCSTR Key,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    va_list Arguments;

    printf("%s = ", Key);
    va_start(Arguments, Format);
    vprintf(Format, Arguments);
    va_end(Arguments);
    printf("\n");
}

static
std::string
EcGuidText(
    _In_ REFGUID Guid)
{
    WCHAR Wide[40];
    CHAR Narrow[40];

    StringFromGUID2(Guid, Wide, _countof(Wide));
    WideCharToMultiByte(CP_UTF8, 0, Wide, -1, Narrow, sizeof(Narrow), nullptr, nullptr);
    return Narrow;
}

/* Made as libobs-d3d11 makes its device: the first adapter, BGRA support, 11_0 down to 10_0 */
static
HRESULT
EcMakeDevice(
    _In_ UINT Flags,
    _COM_Outptr_ ID3D11Device** Device,
    _COM_Outptr_ ID3D11DeviceContext** Context)
{
    static const D3D_FEATURE_LEVEL Levels[] =
    {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };

    return EcCreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, Flags, Levels, _countof(Levels),
                          D3D11_SDK_VERSION, Device, nullptr, Context);
}

static
VOID
EcCheckInterfaces(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context)
{
    static const struct
    {
        PCSTR Name;
        IID Iid;
        BOOLEAN OnContext;
    } Asked[] =
    {
        { "device.ID3D11Device1", __uuidof(ID3D11Device1), FALSE },
        { "device.ID3D11Device2", __uuidof(ID3D11Device2), FALSE },
        { "device.ID3D11Device3", __uuidof(ID3D11Device3), FALSE },
        { "device.ID3D11Device4", __uuidof(ID3D11Device4), FALSE },
        { "device.ID3D11Device5", __uuidof(ID3D11Device5), FALSE },
        { "device.IDXGIDevice1", __uuidof(IDXGIDevice1), FALSE },
        { "device.IDXGIDevice2", __uuidof(IDXGIDevice2), FALSE },
        { "device.IDXGIDevice3", __uuidof(IDXGIDevice3), FALSE },
        { "device.IDXGIDevice4", __uuidof(IDXGIDevice4), FALSE },
        { "device.ID3D10Multithread", __uuidof(ID3D10Multithread), FALSE },
        { "device.ID3D11Multithread", __uuidof(ID3D11Multithread), FALSE },
        { "device.ID3D11VideoDevice", __uuidof(ID3D11VideoDevice), FALSE },
        { "device.ID3D11VideoDevice1", __uuidof(ID3D11VideoDevice1), FALSE },
        { "device.ID3D11VideoDevice2", __uuidof(ID3D11VideoDevice2), FALSE },
        { "context.ID3D11DeviceContext1", __uuidof(ID3D11DeviceContext1), TRUE },
        { "context.ID3D11DeviceContext2", __uuidof(ID3D11DeviceContext2), TRUE },
        { "context.ID3D11DeviceContext3", __uuidof(ID3D11DeviceContext3), TRUE },
        { "context.ID3D11DeviceContext4", __uuidof(ID3D11DeviceContext4), TRUE },
        { "context.ID3D11Multithread", __uuidof(ID3D11Multithread), TRUE },
        { "context.ID3D11VideoContext", __uuidof(ID3D11VideoContext), TRUE },
        { "context.ID3D11VideoContext1", __uuidof(ID3D11VideoContext1), TRUE },
        { "context.ID3D11VideoContext2", __uuidof(ID3D11VideoContext2), TRUE },
        { "context.ID3D11VideoContext3", __uuidof(ID3D11VideoContext3), TRUE },
    };
    CHAR Key[96];

    for (const auto& Each : Asked)
    {
        ComPtr<IUnknown> Found;
        HRESULT hr = Each.OnContext ? Context->QueryInterface(Each.Iid, &Found) : Device->QueryInterface(Each.Iid, &Found);

        sprintf_s(Key, "qi.%s", Each.Name);
        EcReport(Key, "0x%08lX", hr);
    }
}

static
HRESULT
EcMakeTexture(
    _In_ ID3D11Device* Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Bind,
    _In_ UINT Misc,
    _COM_Outptr_ ID3D11Texture2D** Texture)
{
    D3D11_TEXTURE2D_DESC Desc = {};

    Desc.Width = EcWidth;
    Desc.Height = EcHeight;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.BindFlags = Bind;
    Desc.MiscFlags = Misc;
    return Device->CreateTexture2D(&Desc, nullptr, Texture);
}

/* Reads one byte of plane 0 back through a staging copy */
static
INT
EcReadByte(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context,
    _In_ ID3D11Texture2D* Texture,
    _In_ UINT X,
    _In_ UINT Y)
{
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    ComPtr<ID3D11Texture2D> Staging;
    INT Value = -1;

    Texture->GetDesc(&Desc);
    Desc.BindFlags = 0;
    Desc.MiscFlags = 0;
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &Staging)))
        return -2;

    Context->CopyResource(Staging.Get(), Texture);
    if (SUCCEEDED(Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped)))
    {
        Value = static_cast<const BYTE*>(Mapped.pData)[Y * Mapped.RowPitch + X];
        Context->Unmap(Staging.Get(), 0);
    }

    return Value;
}

/* The video processor OBS and the MFTs use to turn BGRA into NV12 */
static
VOID
EcCheckVideo(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context)
{
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC Content = {};
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC InputDesc = {};
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputDesc = {};
    D3D11_VIDEO_PROCESSOR_STREAM Stream = {};
    D3D11_VIDEO_PROCESSOR_CAPS Caps = {};
    ComPtr<ID3D11VideoDevice> Video;
    ComPtr<ID3D11VideoContext> VideoContext;
    ComPtr<ID3D11VideoProcessorEnumerator> Enumerator;
    ComPtr<ID3D11VideoProcessor> Processor;
    ComPtr<ID3D11VideoProcessorInputView> Input;
    ComPtr<ID3D11VideoProcessorOutputView> Output;
    ComPtr<ID3D11Texture2D> Bgra;
    ComPtr<ID3D11Texture2D> Nv12;
    ComPtr<ID3D11RenderTargetView> Target;
    const FLOAT Gray[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    UINT Count = 0;
    UINT Flags = 0;
    BOOL Supported = FALSE;
    GUID Profile;
    HRESULT hr;

    hr = Device->QueryInterface(IID_PPV_ARGS(&Video));
    EcReport("video.device", "0x%08lX", hr);
    if (FAILED(hr))
        return;
    hr = Context->QueryInterface(IID_PPV_ARGS(&VideoContext));
    EcReport("video.context", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Count = Video->GetVideoDecoderProfileCount();
    EcReport("video.decoder_profiles", "%u", Count);
    for (UINT Index = 0; Index < Count; Index++)
    {
        if (FAILED(Video->GetVideoDecoderProfile(Index, &Profile)))
            continue;
        if (Profile == D3D11_DECODER_PROFILE_H264_VLD_NOFGT)
        {
            hr = Video->CheckVideoDecoderFormat(&Profile, DXGI_FORMAT_NV12, &Supported);
            EcReport("video.h264_nv12", "0x%08lX %d", hr, Supported);
        }
    }

    Content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    Content.InputWidth = EcWidth;
    Content.InputHeight = EcHeight;
    Content.OutputWidth = EcWidth;
    Content.OutputHeight = EcHeight;
    Content.InputFrameRate = { 30, 1 };
    Content.OutputFrameRate = { 30, 1 };
    Content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    hr = Video->CreateVideoProcessorEnumerator(&Content, &Enumerator);
    EcReport("video.enumerator", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &Flags);
    EcReport("video.format.bgra", "0x%X", Flags);
    Flags = 0;
    Enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &Flags);
    EcReport("video.format.nv12", "0x%X", Flags);
    hr = Enumerator->GetVideoProcessorCaps(&Caps);
    EcReport("video.caps", "0x%08lX rates %u", hr, Caps.RateConversionCapsCount);

    hr = Video->CreateVideoProcessor(Enumerator.Get(), 0, &Processor);
    EcReport("video.processor", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    EcMakeTexture(Device, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, &Bgra);
    hr = EcMakeTexture(Device, DXGI_FORMAT_NV12, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, &Nv12);
    EcReport("video.nv12_texture", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Device->CreateRenderTargetView(Bgra.Get(), nullptr, &Target);
    Context->ClearRenderTargetView(Target.Get(), Gray);

    InputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    hr = Video->CreateVideoProcessorInputView(Bgra.Get(), Enumerator.Get(), &InputDesc, &Input);
    EcReport("video.input_view", "0x%08lX", hr);
    OutputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    hr = Video->CreateVideoProcessorOutputView(Nv12.Get(), Enumerator.Get(), &OutputDesc, &Output);
    EcReport("video.output_view", "0x%08lX", hr);
    if (Input == nullptr || Output == nullptr)
        return;

    Stream.Enable = TRUE;
    Stream.pInputSurface = Input.Get();
    hr = VideoContext->VideoProcessorBlt(Processor.Get(), Output.Get(), 0, 1, &Stream);
    EcReport("video.blt", "0x%08lX", hr);
    EcReport("video.blt.luma", "%d", EcReadByte(Device, Context, Nv12.Get(), EcWidth / 2, EcHeight / 2));
}

/* The keyed mutex textures OBS hands between its own devices */
static
VOID
EcCheckShared(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context)
{
    static const struct
    {
        PCSTR Name;
        DXGI_FORMAT Format;
        UINT Misc;
    } Kinds[] =
    {
        { "bgra.keyed", DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX },
        { "bgra.nt_keyed", DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE },
        { "nv12.shared", DXGI_FORMAT_NV12, D3D11_RESOURCE_MISC_SHARED },
        { "nv12.keyed", DXGI_FORMAT_NV12, D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX },
        { "nv12.nt_keyed", DXGI_FORMAT_NV12, D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE },
    };
    ComPtr<ID3D11Device> Other;
    ComPtr<ID3D11DeviceContext> OtherContext;
    ComPtr<ID3D11Device1> Other1;
    CHAR Key[96];
    HRESULT hr;

    hr = EcMakeDevice(0, &Other, &OtherContext);
    EcReport("shared.second_device", "0x%08lX", hr);
    if (FAILED(hr))
        return;
    Other.As(&Other1);

    for (const auto& Kind : Kinds)
    {
        ComPtr<ID3D11Texture2D> Texture;
        ComPtr<ID3D11Texture2D> Opened;
        ComPtr<IDXGIKeyedMutex> Mutex;
        ComPtr<IDXGIKeyedMutex> OtherMutex;
        ComPtr<IDXGIResource> Resource;
        ComPtr<IDXGIResource1> Resource1;
        ComPtr<ID3D11RenderTargetView> Target;
        HANDLE Handle = nullptr;
        BOOLEAN IsNt = (Kind.Misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0;
        UINT Bind = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        const FLOAT Color[4] = { 0.25f, 0.5f, 0.75f, 1.0f };

        hr = EcMakeTexture(Device, Kind.Format, Bind, Kind.Misc, &Texture);
        sprintf_s(Key, "shared.%s.create", Kind.Name);
        EcReport(Key, "0x%08lX", hr);
        if (FAILED(hr))
            continue;

        Texture.As(&Mutex);
        sprintf_s(Key, "shared.%s.mutex", Kind.Name);
        EcReport(Key, "%s", Mutex != nullptr ? "yes" : "no");

        if (IsNt)
        {
            Texture.As(&Resource1);
            hr = Resource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                               &Handle);
        }
        else
        {
            Texture.As(&Resource);
            hr = Resource->GetSharedHandle(&Handle);
        }
        sprintf_s(Key, "shared.%s.handle", Kind.Name);
        EcReport(Key, "0x%08lX", hr);
        if (FAILED(hr))
            continue;

        /* The creator writes under key 0 and hands over with key 1 */
        if (Mutex != nullptr)
        {
            sprintf_s(Key, "shared.%s.acquire0", Kind.Name);
            EcReport(Key, "0x%08lX", Mutex->AcquireSync(0, 1000));
        }
        if (Kind.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
        {
            Device->CreateRenderTargetView(Texture.Get(), nullptr, &Target);
            Context->ClearRenderTargetView(Target.Get(), Color);
        }
        if (Mutex != nullptr)
        {
            sprintf_s(Key, "shared.%s.release1", Kind.Name);
            EcReport(Key, "0x%08lX", Mutex->ReleaseSync(1));
        }
        else
        {
            Context->Flush();
        }

        if (IsNt)
            hr = Other1->OpenSharedResource1(Handle, IID_PPV_ARGS(&Opened));
        else
            hr = Other->OpenSharedResource(Handle, IID_PPV_ARGS(&Opened));
        sprintf_s(Key, "shared.%s.open", Kind.Name);
        EcReport(Key, "0x%08lX", hr);
        if (IsNt)
            CloseHandle(Handle);
        if (FAILED(hr))
            continue;

        Opened.As(&OtherMutex);
        if (OtherMutex != nullptr)
        {
            sprintf_s(Key, "shared.%s.other_acquire1", Kind.Name);
            EcReport(Key, "0x%08lX", OtherMutex->AcquireSync(1, 1000));
        }
        sprintf_s(Key, "shared.%s.read", Kind.Name);
        EcReport(Key, "%d", EcReadByte(Other.Get(), OtherContext.Get(), Opened.Get(), 4, 4));
        if (OtherMutex != nullptr)
        {
            sprintf_s(Key, "shared.%s.other_release0", Kind.Name);
            EcReport(Key, "0x%08lX", OtherMutex->ReleaseSync(0));
        }
    }
}

/*
 * NVENC, declared from NVIDIA's public API documentation. Only the session, GUID and
 * caps entries are used; their structures are small and versioned.
 */
typedef INT NVENCSTATUS;

struct EcNvencFunctions
{
    UINT32 Version;
    UINT32 Reserved;
    PVOID OpenEncodeSession;
    NVENCSTATUS (WINAPI* GetEncodeGUIDCount)(PVOID, UINT32*);
    PVOID GetEncodeProfileGUIDCount;
    PVOID GetEncodeProfileGUIDs;
    NVENCSTATUS (WINAPI* GetEncodeGUIDs)(PVOID, GUID*, UINT32, UINT32*);
    NVENCSTATUS (WINAPI* GetInputFormatCount)(PVOID, GUID, UINT32*);
    NVENCSTATUS (WINAPI* GetInputFormats)(PVOID, GUID, INT*, UINT32, UINT32*);
    NVENCSTATUS (WINAPI* GetEncodeCaps)(PVOID, GUID, PVOID, INT*);
    NVENCSTATUS (WINAPI* GetEncodePresetCount)(PVOID, GUID, UINT32*);
    PVOID Unused1[18];
    NVENCSTATUS (WINAPI* DestroyEncoder)(PVOID);
    PVOID InvalidateRefFrames;
    NVENCSTATUS (WINAPI* OpenEncodeSessionEx)(PVOID, PVOID*);
    PVOID Rest[319];
};

struct EcNvencOpenParams
{
    UINT32 Version;
    UINT32 DeviceType;
    PVOID Device;
    PVOID Reserved;
    UINT32 ApiVersion;
    UINT32 Reserved1[253];
    PVOID Reserved2[64];
};

struct EcNvencCapsParam
{
    UINT32 Version;
    INT CapsToQuery;
    UINT32 Reserved[62];
};

static
UINT32
EcNvencStructVersion(
    _In_ UINT32 ApiVersion,
    _In_ UINT32 Revision)
{
    return ApiVersion | (Revision << 16) | (0x7u << 28);
}

static
VOID
EcCheckNvenc(
    _In_ ID3D11Device* Device)
{
#if defined(_M_ARM64)
    static const WCHAR Library[] = L"nvEncodeAPIa64.dll";
#else
    static const WCHAR Library[] = L"nvEncodeAPI64.dll";
#endif
    NVENCSTATUS (WINAPI* GetMaxVersion)(UINT32*);
    NVENCSTATUS (WINAPI* CreateInstance)(EcNvencFunctions*);
    EcNvencFunctions* Funcs;
    EcNvencOpenParams Open = {};
    UINT32 MaxVersion = 0;
    UINT32 ApiVersion;
    UINT32 Count = 0;
    GUID Codecs[16];
    PVOID Session = nullptr;
    NVENCSTATUS Status;
    HMODULE Module;
    CHAR Key[96];

    Module = LoadLibraryW(Library);
    EcReport("nvenc.library", "%s", Module != nullptr ? "loaded" : "missing");
    if (Module == nullptr)
        return;

    GetMaxVersion = reinterpret_cast<decltype(GetMaxVersion)>(GetProcAddress(Module, "NvEncodeAPIGetMaxSupportedVersion"));
    CreateInstance = reinterpret_cast<decltype(CreateInstance)>(GetProcAddress(Module, "NvEncodeAPICreateInstance"));
    if (GetMaxVersion == nullptr || CreateInstance == nullptr)
        return;

    Status = GetMaxVersion(&MaxVersion);
    EcReport("nvenc.max_version", "%d %u.%u", Status, MaxVersion >> 4, MaxVersion & 0xF);

    /* The API version word keeps the major in the low byte and the minor in the high byte */
    ApiVersion = (MaxVersion >> 4) | ((MaxVersion & 0xF) << 24);
    Funcs = static_cast<EcNvencFunctions*>(calloc(1, sizeof(*Funcs)));
    Funcs->Version = EcNvencStructVersion(ApiVersion, 2);
    Status = CreateInstance(Funcs);
    EcReport("nvenc.instance", "%d", Status);
    if (Status != 0 || Funcs->OpenEncodeSessionEx == nullptr)
    {
        free(Funcs);
        return;
    }

    Open.Version = EcNvencStructVersion(ApiVersion, 1);
    Open.DeviceType = 0;
    Open.Device = Device;
    Open.ApiVersion = ApiVersion;
    Status = Funcs->OpenEncodeSessionEx(&Open, &Session);
    EcReport("nvenc.open_session", "%d", Status);
    if (Status != 0)
    {
        free(Funcs);
        return;
    }

    Status = Funcs->GetEncodeGUIDCount(Session, &Count);
    EcReport("nvenc.codec_count", "%d %u", Status, Count);
    if (Count > _countof(Codecs))
        Count = _countof(Codecs);
    Status = Funcs->GetEncodeGUIDs(Session, Codecs, Count, &Count);
    EcReport("nvenc.codecs", "%d %u", Status, Count);
    for (UINT32 Index = 0; Index < Count; Index++)
    {
        EcNvencCapsParam Caps = {};
        UINT32 Formats = 0;
        UINT32 Presets = 0;
        INT Value = -1;

        sprintf_s(Key, "nvenc.codec%u", Index);
        EcReport(Key, "%s", EcGuidText(Codecs[Index]).c_str());

        Funcs->GetInputFormatCount(Session, Codecs[Index], &Formats);
        Funcs->GetEncodePresetCount(Session, Codecs[Index], &Presets);
        sprintf_s(Key, "nvenc.codec%u.counts", Index);
        EcReport(Key, "formats %u presets %u", Formats, Presets);

        /* One numbered caps value, compared as is */
        Caps.Version = EcNvencStructVersion(ApiVersion, 1);
        Caps.CapsToQuery = 15;
        Status = Funcs->GetEncodeCaps(Session, Codecs[Index], &Caps, &Value);
        sprintf_s(Key, "nvenc.codec%u.caps15", Index);
        EcReport(Key, "%d %d", Status, Value);
    }

    EcReport("nvenc.destroy", "%d", Funcs->DestroyEncoder(Session));
    free(Funcs);
}

/* Feeds NV12 frames to the first hardware H.264 encoder Media Foundation lists */
static
VOID
EcCheckMediaFoundation(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context)
{
    MFT_REGISTER_TYPE_INFO InputInfo = { MFMediaType_Video, MFVideoFormat_NV12 };
    MFT_REGISTER_TYPE_INFO OutputInfo = { MFMediaType_Video, MFVideoFormat_H264 };
    IMFActivate** Activates = nullptr;
    UINT32 ActivateCount = 0;
    ComPtr<IMFTransform> Encoder;
    ComPtr<IMFAttributes> Attributes;
    ComPtr<IMFDXGIDeviceManager> Manager;
    ComPtr<IMFMediaEventGenerator> Events;
    ComPtr<IMFMediaType> OutputType;
    ComPtr<IMFMediaType> InputType;
    ComPtr<ID3D10Multithread> Multithread;
    UINT ResetToken = 0;
    UINT32 IsAware = 0;
    UINT Sent = 0;
    UINT Received = 0;
    UINT64 Bytes = 0;
    BOOLEAN IsDraining = FALSE;
    BOOLEAN IsDone = FALSE;
    ULONGLONG Deadline;
    WCHAR Name[128] = L"";
    HRESULT hr;

    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    EcReport("mf.startup", "0x%08lX", hr);

    hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, &InputInfo,
                   &OutputInfo, &Activates, &ActivateCount);
    EcReport("mf.enum", "0x%08lX %u", hr, ActivateCount);
    if (FAILED(hr) || ActivateCount == 0)
        return;

    Activates[0]->GetString(MFT_FRIENDLY_NAME_Attribute, Name, _countof(Name), nullptr);
    EcReport("mf.encoder", "%ls", Name);
    hr = Activates[0]->ActivateObject(IID_PPV_ARGS(&Encoder));
    EcReport("mf.activate", "0x%08lX", hr);
    for (UINT32 Index = 0; Index < ActivateCount; Index++)
        Activates[Index]->Release();
    CoTaskMemFree(Activates);
    if (FAILED(hr))
        return;

    Encoder->GetAttributes(&Attributes);
    Attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
    Attributes->GetUINT32(MF_SA_D3D11_AWARE, &IsAware);
    EcReport("mf.d3d11_aware", "%u", IsAware);

    /* The device manager needs a device that takes its own lock */
    hr = Device->QueryInterface(IID_PPV_ARGS(&Multithread));
    EcReport("mf.multithread", "0x%08lX", hr);
    if (Multithread != nullptr)
        Multithread->SetMultithreadProtected(TRUE);

    hr = MFCreateDXGIDeviceManager(&ResetToken, &Manager);
    if (SUCCEEDED(hr))
        hr = Manager->ResetDevice(Device, ResetToken);
    EcReport("mf.reset_device", "0x%08lX", hr);
    hr = Encoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(Manager.Get()));
    EcReport("mf.set_manager", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    MFCreateMediaType(&OutputType);
    OutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    OutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    OutputType->SetUINT32(MF_MT_AVG_BITRATE, 2000000);
    OutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(OutputType.Get(), MF_MT_FRAME_SIZE, EcWidth, EcHeight);
    MFSetAttributeRatio(OutputType.Get(), MF_MT_FRAME_RATE, 30, 1);
    MFSetAttributeRatio(OutputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = Encoder->SetOutputType(0, OutputType.Get(), 0);
    EcReport("mf.output_type", "0x%08lX", hr);

    MFCreateMediaType(&InputType);
    InputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    InputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    InputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(InputType.Get(), MF_MT_FRAME_SIZE, EcWidth, EcHeight);
    MFSetAttributeRatio(InputType.Get(), MF_MT_FRAME_RATE, 30, 1);
    MFSetAttributeRatio(InputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = Encoder->SetInputType(0, InputType.Get(), 0);
    EcReport("mf.input_type", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Encoder.As(&Events);
    EcReport("mf.begin", "0x%08lX", Encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
    EcReport("mf.start", "0x%08lX", Encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

    /* Hardware encoders are asynchronous: they ask for input and announce output */
    Deadline = GetTickCount64() + 15000;
    while (!IsDone)
    {
        ComPtr<IMFMediaEvent> Event;
        MediaEventType Type;

        /* A stalled encoder must not hang the run */
        hr = Events->GetEvent(MF_EVENT_FLAG_NO_WAIT, &Event);
        if (hr == MF_E_NO_EVENTS_AVAILABLE && GetTickCount64() < Deadline)
        {
            Sleep(5);
            continue;
        }
        if (FAILED(hr))
        {
            EcReport("mf.event_error", "0x%08lX", hr);
            break;
        }
        Event->GetType(&Type);

        if (Type == METransformNeedInput && Sent < EcFrames)
        {
            ComPtr<ID3D11Texture2D> Frame;
            ComPtr<IMFMediaBuffer> Buffer;
            ComPtr<IMFSample> Sample;
            DWORD Length = 0;

            EcMakeTexture(Device, DXGI_FORMAT_NV12, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, &Frame);
            MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), Frame.Get(), 0, FALSE, &Buffer);
            ComPtr<IMF2DBuffer> Buffer2D;

            if (SUCCEEDED(Buffer.As(&Buffer2D)))
                Buffer2D->GetContiguousLength(&Length);
            Buffer->SetCurrentLength(Length);
            MFCreateSample(&Sample);
            Sample->AddBuffer(Buffer.Get());
            Sample->SetSampleTime(Sent * 333333);
            Sample->SetSampleDuration(333333);
            hr = Encoder->ProcessInput(0, Sample.Get(), 0);
            if (FAILED(hr))
                EcReport("mf.input_error", "0x%08lX", hr);
            Sent += 1;
        }
        else if (Type == METransformNeedInput && !IsDraining)
        {
            Encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            Encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
            IsDraining = TRUE;
        }
        else if (Type == METransformHaveOutput)
        {
            MFT_OUTPUT_DATA_BUFFER Output = {};
            ComPtr<IMFMediaBuffer> Buffer;
            DWORD OutputStatus = 0;
            DWORD Length = 0;

            hr = Encoder->ProcessOutput(0, 1, &Output, &OutputStatus);
            if (SUCCEEDED(hr) && Output.pSample != nullptr)
            {
                Output.pSample->ConvertToContiguousBuffer(&Buffer);
                Buffer->GetCurrentLength(&Length);
                Bytes += Length;
                Received += 1;
            }
            else
            {
                EcReport("mf.output_error", "0x%08lX", hr);
            }
            if (Output.pSample != nullptr)
                Output.pSample->Release();
            if (Output.pEvents != nullptr)
                Output.pEvents->Release();
        }
        else if (Type == METransformDrainComplete)
        {
            IsDone = TRUE;
        }
    }

    EcReport("mf.frames", "sent %u received %u", Sent, Received);
    fprintf(stderr, "mf bytes: %llu\n", Bytes);
    EcReport("mf.has_bytes", "%d", Bytes != 0);
    Encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    Encoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, 0);
    Context->Flush();
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    PCWSTR Runtime = L"system";
    std::wstring Folder;
    WCHAR Module[MAX_PATH];
    WCHAR Path[MAX_PATH] = L"";
    HMODULE D3D11;
    HRESULT hr;

    setvbuf(stdout, nullptr, _IONBF, 0);
    for (int Index = 1; Index + 1 < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0)
            Runtime = argv[Index + 1];
    }

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(nullptr, Module, _countof(Module));
        *(wcsrchr(Module, L'\\') + 1) = L'\0';
        Folder = std::wstring(Module) + L"reactx\\";
        LoadLibraryW((Folder + L"dxgi.dll").c_str());
    }

    D3D11 = LoadLibraryW((Folder + L"d3d11.dll").c_str());
    EcCreateDevice = D3D11 ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(D3D11, "D3D11CreateDevice")) : nullptr;
    if (EcCreateDevice == nullptr)
    {
        fprintf(stderr, "error: cannot load the runtime\n");
        return 1;
    }
    GetModuleFileNameW(D3D11, Path, _countof(Path));
    fprintf(stderr, "loaded d3d11: %ls\n", Path);
    GetModuleFileNameW(GetModuleHandleW(L"dxgi.dll"), Path, _countof(Path));
    fprintf(stderr, "loaded dxgi: %ls\n", Path);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    hr = EcMakeDevice(D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, &Device, &Context);
    EcReport("d3d11.create", "0x%08lX", hr);
    if (FAILED(hr))
        return 1;

    EcCheckInterfaces(Device.Get(), Context.Get());
    EcCheckVideo(Device.Get(), Context.Get());
    EcCheckShared(Device.Get(), Context.Get());
    EcCheckNvenc(Device.Get());
    EcCheckMediaFoundation(Device.Get(), Context.Get());
    return 0;
}
