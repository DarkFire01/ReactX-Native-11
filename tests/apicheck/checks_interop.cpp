/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Windows.Graphics.DirectX.Direct3D11 interop wrappers for DXGI devices and surfaces
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "check.hpp"

#include <inspectable.h>
#include <weakreference.h>
#include <winstring.h>
#include <windows.foundation.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <stdio.h>

using ABI::Windows::Foundation::IClosable;
using ABI::Windows::Graphics::DirectX::Direct3D11::Direct3DSurfaceDescription;
using ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface;
using Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;

typedef HRESULT (WINAPI* PFN_AC_CREATE_INTEROP)(IUnknown* Source, IInspectable** Wrapper);

/* What a pointer the call should not touch is left holding */
static IInspectable* const AcSentinel = reinterpret_cast<IInspectable*>(static_cast<ULONG_PTR>(0x1234));

/* Answers QueryInterface for nothing, so a wrapper can be fed something that is not DXGI */
class AcBareObject final : public IUnknown
{
public:
    ULONG QueryCount = 0;

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override
    {
        UNREFERENCED_PARAMETER(riid);
        QueryCount++;
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return ++RefCount;
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        return --RefCount;
    }

    ULONG RefCount = 1;
};

static
PCSTR
AcOutcome(
    _In_opt_ IInspectable* Value)
{
    if (Value == AcSentinel)
        return "untouched";
    return Value == nullptr ? "null" : "set";
}

/* A null out pointer may fault; that is reported instead of taking the check down */
static
HRESULT
AcQueryGuarded(
    _In_ IUnknown* Object,
    _In_ REFIID Interface,
    _Out_ BOOLEAN* Faulted)
{
    HRESULT hr = E_FAIL;

    *Faulted = FALSE;
    __try
    {
        hr = Object->QueryInterface(Interface, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *Faulted = TRUE;
    }

    return hr;
}

static
HRESULT
AcAccessGuarded(
    _In_ IDirect3DDxgiInterfaceAccess* Access,
    _Out_ BOOLEAN* Faulted)
{
    HRESULT hr = E_FAIL;

    *Faulted = FALSE;
    __try
    {
        hr = Access->GetInterface(__uuidof(IUnknown), nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *Faulted = TRUE;
    }

    return hr;
}

static
HRESULT
AcCreateGuarded(
    _In_ PFN_AC_CREATE_INTEROP Create,
    _In_opt_ IUnknown* Source,
    _Out_ BOOLEAN* Faulted)
{
    HRESULT hr = E_FAIL;

    *Faulted = FALSE;
    __try
    {
        hr = Create(Source, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *Faulted = TRUE;
    }

    return hr;
}

static
BOOLEAN
AcSameObject(
    _In_ IUnknown* First,
    _In_ IUnknown* Second)
{
    ComPtr<IUnknown> A;
    ComPtr<IUnknown> B;

    First->QueryInterface(IID_PPV_ARGS(&A));
    Second->QueryInterface(IID_PPV_ARGS(&B));
    return A.Get() == B.Get();
}

static
VOID
AcReportGuid(
    _In_ CheckContext& Check,
    _In_z_ PCSTR Key,
    _In_ REFGUID Id)
{
    Check.Report(Key, "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", Id.Data1, Id.Data2, Id.Data3, Id.Data4[0],
                 Id.Data4[1], Id.Data4[2], Id.Data4[3], Id.Data4[4], Id.Data4[5], Id.Data4[6], Id.Data4[7]);
}

/* Creation argument handling, the same for both exports */
static
VOID
AcCheckCreateArguments(
    _In_ CheckContext& Check,
    _In_z_ PCSTR Prefix,
    _In_ PFN_AC_CREATE_INTEROP Create,
    _In_ IUnknown* Source)
{
    AcBareObject Bare;
    IInspectable* Out;
    BOOLEAN Faulted;
    char Key[64];
    HRESULT hr;

    Out = AcSentinel;
    hr = Create(nullptr, &Out);
    sprintf_s(Key, "%s.nullsource", Prefix);
    Check.Report(Key, "0x%08lX %s", hr, AcOutcome(Out));

    hr = AcCreateGuarded(Create, Source, &Faulted);
    sprintf_s(Key, "%s.nullout", Prefix);
    if (Faulted)
        Check.Report(Key, "fault");
    else
        Check.Report(Key, "0x%08lX", hr);

    Out = AcSentinel;
    hr = Create(&Bare, &Out);
    sprintf_s(Key, "%s.bare", Prefix);
    Check.Report(Key, "0x%08lX %s queries %lu refs %lu", hr, AcOutcome(Out), Bare.QueryCount, Bare.RefCount);
    if (SUCCEEDED(hr) && Out != nullptr && Out != AcSentinel)
    {
        ComPtr<IDirect3DDxgiInterfaceAccess> Access;
        ComPtr<IUnknown> Inner;

        if (SUCCEEDED(Out->QueryInterface(IID_PPV_ARGS(&Access))))
        {
            hr = Access->GetInterface(IID_PPV_ARGS(&Inner));
            sprintf_s(Key, "%s.bare.getinterface", Prefix);
            Check.Report(Key, "0x%08lX queries %lu", hr, Bare.QueryCount);
        }

        Access.Reset();
        Out->Release();
        sprintf_s(Key, "%s.bare.released", Prefix);
        Check.Report(Key, "refs %lu", Bare.RefCount);
    }
}

/* What the wrapper answers, how it names itself and how Close, weak references and marshaling behave; consumes Wrapper */
static
VOID
AcCheckWrapper(
    _In_ CheckContext& Check,
    _In_z_ PCSTR Prefix,
    _In_ IInspectable* Wrapper,
    _In_ REFIID Primary)
{
    static const struct
    {
        PCSTR Name;
        GUID Id;
    } Probes[] =
    {
        { "unknown", __uuidof(IUnknown) },
        { "inspectable", __uuidof(IInspectable) },
        { "d3ddevice", __uuidof(IDirect3DDevice) },
        { "d3dsurface", __uuidof(IDirect3DSurface) },
        { "closable", __uuidof(IClosable) },
        { "access", __uuidof(IDirect3DDxgiInterfaceAccess) },
        { "weaksource", __uuidof(IWeakReferenceSource) },
        { "agile", __uuidof(IAgileObject) },
        { "marshal", __uuidof(IMarshal) },
        { "marshal2", __uuidof(IMarshal2) },
        { "nomarshal", __uuidof(INoMarshal) },
        { "stdmarshalinfo", __uuidof(IStdMarshalInfo) },
        { "classinfo", __uuidof(IProvideClassInfo) },
        { "dxgiobject", __uuidof(IDXGIObject) },
        { "dxgidevice", __uuidof(IDXGIDevice) },
        { "dxgisurface", __uuidof(IDXGISurface) },
        { "d3d11device", __uuidof(ID3D11Device) },
    };
    ComPtr<IInspectable> PrimaryFace;
    ComPtr<IInspectable> InspectableFace;
    ComPtr<IClosable> Closable;
    ComPtr<IDirect3DDxgiInterfaceAccess> Access;
    ComPtr<IWeakReferenceSource> WeakSource;
    ComPtr<IWeakReference> Weak;
    ComPtr<IMarshal> Marshal;
    ComPtr<IUnknown> Found;
    HSTRING Name = nullptr;
    TrustLevel Trust = FullTrust;
    ULONG IidCount = 0;
    IID* Iids = nullptr;
    CLSID Unmarshal = {};
    BOOLEAN Faulted;
    BOOLEAN IsLast;
    char Key[64];
    HRESULT hr;
    ULONG Index;

    sprintf_s(Key, "%s.refs", Prefix);
    Check.Report(Key, "%lu", AcRefCount(Wrapper));

    for (const auto& Probe : Probes)
    {
        hr = Wrapper->QueryInterface(Probe.Id, reinterpret_cast<void**>(Found.ReleaseAndGetAddressOf()));
        sprintf_s(Key, "%s.qi.%s", Prefix, Probe.Name);
        Check.Report(Key, "0x%08lX", hr);
        if (SUCCEEDED(hr))
        {
            sprintf_s(Key, "%s.qi.%s.identity", Prefix, Probe.Name);
            Check.Report(Key, "%d", AcSameObject(Found.Get(), Wrapper));
        }
    }

    hr = AcQueryGuarded(Wrapper, Primary, &Faulted);
    sprintf_s(Key, "%s.qi.nullout", Prefix);
    if (Faulted)
        Check.Report(Key, "fault");
    else
        Check.Report(Key, "0x%08lX", hr);

    /* Which face the export hands out */
    Wrapper->QueryInterface(Primary, reinterpret_cast<void**>(PrimaryFace.GetAddressOf()));
    Wrapper->QueryInterface(IID_PPV_ARGS(&InspectableFace));
    sprintf_s(Key, "%s.face", Prefix);
    Check.Report(Key, "primary %d inspectable %d", PrimaryFace.Get() == Wrapper, InspectableFace.Get() == Wrapper);
    PrimaryFace.Reset();
    InspectableFace.Reset();

    hr = Wrapper->GetIids(&IidCount, &Iids);
    sprintf_s(Key, "%s.iids", Prefix);
    Check.Report(Key, "0x%08lX %lu", hr, IidCount);
    for (Index = 0; SUCCEEDED(hr) && Index < IidCount; Index++)
    {
        sprintf_s(Key, "%s.iids.%lu", Prefix, Index);
        AcReportGuid(Check, Key, Iids[Index]);
    }
    CoTaskMemFree(Iids);

    hr = Wrapper->GetRuntimeClassName(&Name);
    sprintf_s(Key, "%s.classname", Prefix);
    if (SUCCEEDED(hr))
        Check.Report(Key, "0x%08lX %ls", hr, WindowsGetStringRawBuffer(Name, nullptr));
    else
        Check.Report(Key, "0x%08lX %s", hr, Name == nullptr ? "null" : "set");
    WindowsDeleteString(Name);

    hr = Wrapper->GetTrustLevel(&Trust);
    sprintf_s(Key, "%s.trust", Prefix);
    Check.Report(Key, "0x%08lX %d", hr, SUCCEEDED(hr) ? (int)Trust : -1);

    if (SUCCEEDED(Wrapper->QueryInterface(IID_PPV_ARGS(&Marshal))))
    {
        hr = Marshal->GetUnmarshalClass(__uuidof(IUnknown), Wrapper, MSHCTX_INPROC, nullptr, MSHLFLAGS_NORMAL,
                                        &Unmarshal);
        sprintf_s(Key, "%s.marshal.class", Prefix);
        Check.Report(Key, "0x%08lX", hr);
        sprintf_s(Key, "%s.marshal.clsid", Prefix);
        AcReportGuid(Check, Key, Unmarshal);
        hr = Marshal->QueryInterface(IID_PPV_ARGS(&Found));
        sprintf_s(Key, "%s.marshal.identity", Prefix);
        Check.Report(Key, "0x%08lX %d", hr, SUCCEEDED(hr) && Found.Get() != nullptr && AcSameObject(Found.Get(), Wrapper));
        Marshal.Reset();
    }

    if (SUCCEEDED(Wrapper->QueryInterface(IID_PPV_ARGS(&WeakSource))))
    {
        ComPtr<IInspectable> Resolved;

        hr = WeakSource->GetWeakReference(&Weak);
        sprintf_s(Key, "%s.weak.get", Prefix);
        Check.Report(Key, "0x%08lX", hr);
        if (SUCCEEDED(hr))
        {
            ComPtr<IWeakReference> Again;

            WeakSource->GetWeakReference(&Again);
            sprintf_s(Key, "%s.weak.same", Prefix);
            Check.Report(Key, "%d", Again.Get() == Weak.Get());

            hr = Weak->Resolve(Primary, &Resolved);
            sprintf_s(Key, "%s.weak.resolve", Prefix);
            Check.Report(Key, "0x%08lX %d", hr, Resolved.Get() != nullptr && AcSameObject(Resolved.Get(), Wrapper));
            Resolved.Reset();

            hr = Weak->Resolve(__uuidof(IDXGIDevice), &Resolved);
            sprintf_s(Key, "%s.weak.resolvewrong", Prefix);
            Check.Report(Key, "0x%08lX %d", hr, Resolved.Get() != nullptr);
            Resolved.Reset();
        }
        sprintf_s(Key, "%s.weak.refs", Prefix);
        Check.Report(Key, "%lu", AcRefCount(Wrapper) - 1);
        WeakSource.Reset();
    }

    hr = Wrapper->QueryInterface(IID_PPV_ARGS(&Access));
    if (SUCCEEDED(hr))
    {
        hr = AcAccessGuarded(Access.Get(), &Faulted);
        sprintf_s(Key, "%s.getinterface.nullout", Prefix);
        if (Faulted)
            Check.Report(Key, "fault");
        else
            Check.Report(Key, "0x%08lX", hr);
    }

    if (SUCCEEDED(Wrapper->QueryInterface(IID_PPV_ARGS(&Closable))))
    {
        hr = Closable->Close();
        sprintf_s(Key, "%s.close", Prefix);
        Check.Report(Key, "0x%08lX", hr);
        hr = Closable->Close();
        sprintf_s(Key, "%s.close.again", Prefix);
        Check.Report(Key, "0x%08lX", hr);

        if (Access != nullptr)
        {
            Found.Reset();
            hr = Access->GetInterface(IID_PPV_ARGS(&Found));
            sprintf_s(Key, "%s.close.getinterface", Prefix);
            Check.Report(Key, "0x%08lX %s", hr, Found.Get() == nullptr ? "null" : "set");
        }

        hr = Wrapper->QueryInterface(IID_PPV_ARGS(&Found));
        sprintf_s(Key, "%s.close.qi", Prefix);
        Check.Report(Key, "0x%08lX", hr);
        Found.Reset();
    }

    Access.Reset();
    Closable.Reset();

    /* The caller's reference is dropped here; with it gone the weak reference resolves to nothing */
    Wrapper->AddRef();
    IsLast = Wrapper->Release() == 1;
    Wrapper->Release();
    if (Weak != nullptr)
    {
        IInspectable* Resolved = nullptr;

        sprintf_s(Key, "%s.weak.dead", Prefix);
        if (IsLast)
        {
            hr = Weak->Resolve(Primary, &Resolved);
            Check.Report(Key, "0x%08lX %s", hr, Resolved == nullptr ? "null" : "set");
        }
        else
        {
            Check.Report(Key, "skipped");
        }
    }
}

/* device: CreateDirect3D11DeviceFromDXGIDevice and the IDirect3DDevice it returns */

static
VOID
AcCheckInteropDevice(
    _In_ CheckContext& Check)
{
    PFN_AC_CREATE_INTEROP Create;
    ComPtr<IDXGIDevice> DxgiDevice;
    ComPtr<IDXGIDevice> Back;
    ComPtr<ID3D11Device> D3DBack;
    ComPtr<IDirect3DDxgiInterfaceAccess> Access;
    ComPtr<IDirect3DDevice> Device;
    ComPtr<IDXGISurface> NotThere;
    IInspectable* First = nullptr;
    IInspectable* Second = nullptr;
    IInspectable* Out;
    ULONG Before;
    HRESULT hr;

    Create = reinterpret_cast<PFN_AC_CREATE_INTEROP>(GetProcAddress(Check.Runtime, "CreateDirect3D11DeviceFromDXGIDevice"));
    if (!Check.Expect(Create != nullptr, "CreateDirect3D11DeviceFromDXGIDevice is exported"))
        return;
    if (FAILED(Check.Device->QueryInterface(IID_PPV_ARGS(&DxgiDevice))))
        return;

    AcCheckCreateArguments(Check, "create", Create, DxgiDevice.Get());

    Before = AcRefCount(DxgiDevice.Get());
    Out = AcSentinel;
    hr = Create(DxgiDevice.Get(), &Out);
    Check.Report("create", "0x%08lX %s", hr, AcOutcome(Out));
    if (FAILED(hr) || Out == AcSentinel || Out == nullptr)
        return;
    First = Out;
    Check.Report("create.devicerefs", "+%ld", (LONG)(AcRefCount(DxgiDevice.Get()) - Before));

    hr = Create(DxgiDevice.Get(), &Second);
    Check.Report("create.twice", "0x%08lX same %d", hr, SUCCEEDED(hr) && Second == First);
    if (SUCCEEDED(hr))
        Second->Release();

    if (SUCCEEDED(First->QueryInterface(IID_PPV_ARGS(&Access))))
    {
        hr = Access->GetInterface(IID_PPV_ARGS(&Back));
        Check.Report("getinterface.dxgidevice", "0x%08lX same %d", hr,
                     SUCCEEDED(hr) && AcSameObject(Back.Get(), DxgiDevice.Get()));
        hr = Access->GetInterface(IID_PPV_ARGS(&D3DBack));
        Check.Report("getinterface.d3d11device", "0x%08lX same %d", hr,
                     SUCCEEDED(hr) && AcSameObject(D3DBack.Get(), Check.Device));
        hr = Access->GetInterface(IID_PPV_ARGS(&NotThere));
        Check.Report("getinterface.dxgisurface", "0x%08lX %s", hr, NotThere.Get() == nullptr ? "null" : "set");
        Access.Reset();
    }

    if (SUCCEEDED(First->QueryInterface(IID_PPV_ARGS(&Device))))
    {
        Check.Report("trim", "0x%08lX", Device->Trim());
        Device.Reset();
    }

    Back.Reset();
    D3DBack.Reset();
    AcCheckWrapper(Check, "device", First, __uuidof(IDirect3DDevice));
    Check.Report("create.released", "+%ld", (LONG)(AcRefCount(DxgiDevice.Get()) - Before));

    /* Trim on a closed device */
    if (SUCCEEDED(Create(DxgiDevice.Get(), &First)))
    {
        ComPtr<IClosable> Closable;

        First->QueryInterface(IID_PPV_ARGS(&Closable));
        First->QueryInterface(IID_PPV_ARGS(&Device));
        Closable->Close();
        Check.Report("close.devicerefs", "+%ld", (LONG)(AcRefCount(DxgiDevice.Get()) - Before));
        Check.Report("close.trim", "0x%08lX", Device->Trim());
        Device.Reset();
        Closable.Reset();
        First->Release();
    }
}

/* surface: CreateDirect3D11SurfaceFromDXGISurface and the IDirect3DSurface it returns */

static
VOID
AcCheckInteropSurface(
    _In_ CheckContext& Check)
{
    PFN_AC_CREATE_INTEROP Create;
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<ID3D11Texture2D> TextureBack;
    ComPtr<IDXGISurface> DxgiSurface;
    ComPtr<IDXGISurface> Back;
    ComPtr<IDXGIDevice> NotThere;
    ComPtr<IDirect3DDxgiInterfaceAccess> Access;
    ComPtr<IDirect3DSurface> Surface;
    ComPtr<IClosable> Closable;
    Direct3DSurfaceDescription Desc;
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    IInspectable* First = nullptr;
    IInspectable* Second = nullptr;
    IInspectable* Out;
    ULONG Before;
    HRESULT hr;

    Create = reinterpret_cast<PFN_AC_CREATE_INTEROP>(GetProcAddress(Check.Runtime, "CreateDirect3D11SurfaceFromDXGISurface"));
    if (!Check.Expect(Create != nullptr, "CreateDirect3D11SurfaceFromDXGISurface is exported"))
        return;

    TextureDesc.Width = 64;
    TextureDesc.Height = 32;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(Check.Device->CreateTexture2D(&TextureDesc, nullptr, &Texture)) ||
        FAILED(Texture.As(&DxgiSurface)))
    {
        return;
    }

    AcCheckCreateArguments(Check, "create", Create, DxgiSurface.Get());

    Before = AcRefCount(DxgiSurface.Get());
    Out = AcSentinel;
    hr = Create(DxgiSurface.Get(), &Out);
    Check.Report("create", "0x%08lX %s", hr, AcOutcome(Out));
    if (FAILED(hr) || Out == AcSentinel || Out == nullptr)
        return;
    First = Out;
    Check.Report("create.surfacerefs", "+%ld", (LONG)(AcRefCount(DxgiSurface.Get()) - Before));

    hr = Create(DxgiSurface.Get(), &Second);
    Check.Report("create.twice", "0x%08lX same %d", hr, SUCCEEDED(hr) && Second == First);
    if (SUCCEEDED(hr))
        Second->Release();

    if (SUCCEEDED(First->QueryInterface(IID_PPV_ARGS(&Access))))
    {
        hr = Access->GetInterface(IID_PPV_ARGS(&Back));
        Check.Report("getinterface.dxgisurface", "0x%08lX same %d", hr,
                     SUCCEEDED(hr) && AcSameObject(Back.Get(), DxgiSurface.Get()));
        hr = Access->GetInterface(IID_PPV_ARGS(&TextureBack));
        Check.Report("getinterface.texture", "0x%08lX same %d", hr,
                     SUCCEEDED(hr) && AcSameObject(TextureBack.Get(), Texture.Get()));
        hr = Access->GetInterface(IID_PPV_ARGS(&NotThere));
        Check.Report("getinterface.dxgidevice", "0x%08lX %s", hr, NotThere.Get() == nullptr ? "null" : "set");
        Access.Reset();
    }

    if (SUCCEEDED(First->QueryInterface(IID_PPV_ARGS(&Surface))))
    {
        memset(&Desc, 0xCD, sizeof(Desc));
        hr = Surface->get_Description(&Desc);
        Check.Report("description", "0x%08lX %d x %d format %d samples %d %d", hr, Desc.Width, Desc.Height,
                     (int)Desc.Format, Desc.MultisampleDescription.Count, Desc.MultisampleDescription.Quality);
        Check.Report("description.null", "0x%08lX", Surface->get_Description(nullptr));
        Surface.Reset();
    }

    Back.Reset();
    TextureBack.Reset();
    AcCheckWrapper(Check, "surface", First, __uuidof(IDirect3DSurface));
    Check.Report("create.released", "+%ld", (LONG)(AcRefCount(DxgiSurface.Get()) - Before));

    if (SUCCEEDED(Create(DxgiSurface.Get(), &First)))
    {
        First->QueryInterface(IID_PPV_ARGS(&Closable));
        First->QueryInterface(IID_PPV_ARGS(&Surface));
        Closable->Close();
        Check.Report("close.surfacerefs", "+%ld", (LONG)(AcRefCount(DxgiSurface.Get()) - Before));
        memset(&Desc, 0xCD, sizeof(Desc));
        hr = Surface->get_Description(&Desc);
        Check.Report("close.description", "0x%08lX width %d", hr, Desc.Width);
        Check.Report("close.description.null", "0x%08lX", Surface->get_Description(nullptr));
        Surface.Reset();
        Closable.Reset();
        First->Release();
    }
}

static const CheckInfo InteropChecks[] =
{
    { "interopdevice", AcCheckInteropDevice },
    { "interopsurface", AcCheckInteropSurface },
};

const CheckInfo*
AcInteropChecks(
    _Out_ UINT* Count)
{
    *Count = _countof(InteropChecks);
    return InteropChecks;
}
