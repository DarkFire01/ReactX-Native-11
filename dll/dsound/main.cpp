/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Exported entry points and the class factories
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

CRITICAL_SECTION DsApiLock;
HMODULE DsModule;

static LONG DsServerLocks;
LONG DsLiveObjects;

/* The kinds of object a factory makes */
enum DsFactoryKind
{
    DsMakeSound,
    DsMakeSound8,
    DsMakeCapture,
    DsMakeCapture8,
    DsMakeFullDuplex,
    DsMakePrivate,
};

class DsFactory;

class DsFactoryFace final : public IClassFactory
{
public:
    DsFaceLink Link;
    DsFactory* Self;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    CreateInstance(
        _In_opt_ IUnknown* Outer,
        _In_ REFIID Iid,
        _COM_Outptr_ void** Object) override;

    HRESULT
    STDMETHODCALLTYPE
    LockServer(
        _In_ BOOL Lock) override;
};

class DsFactory final : public DsObject
{
public:
    DsFactory(
        _In_ DsFactoryKind NewKind) : Kind(NewKind)
    {
        Face.Link.Object = this;
        Face.Link.Count = 0;
        Face.Self = this;
        Expose(IID_IClassFactory, &Face, &Face.Link);
    }

    DsFactoryKind Kind;
    DsFactoryFace Face;
};

HRESULT
STDMETHODCALLTYPE
DsFactoryFace::CreateInstance(
    _In_opt_ IUnknown* Outer,
    _In_ REFIID Iid,
    _COM_Outptr_ void** Object)
{
    DsApiGuard Guard;

    if (Object == nullptr)
        return E_INVALIDARG;
    *Object = nullptr;
    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;

    switch (Self->Kind)
    {
        case DsMakeSound:
            return DsCreateSound(DS_VERSION_7, nullptr, FALSE, Iid, Object);
        case DsMakeSound8:
            return DsCreateSound(DS_VERSION_8, nullptr, FALSE, Iid, Object);
        case DsMakeCapture:
            return DsCreateCapture(DS_VERSION_7, nullptr, FALSE, Iid, Object);
        case DsMakeCapture8:
            return DsCreateCapture(DS_VERSION_8, nullptr, FALSE, Iid, Object);
        case DsMakePrivate:
            return DsCreatePrivate(Iid, Object);
        case DsMakeFullDuplex:
        {
            IDirectSoundFullDuplex* Duplex = nullptr;
            HRESULT hr = DsCreateFullDuplex(nullptr, nullptr, nullptr, nullptr, nullptr, 0, FALSE, nullptr, &Duplex, nullptr, nullptr);

            if (FAILED(hr))
                return hr;
            hr = Duplex->QueryInterface(Iid, Object);
            Duplex->Release();
            return hr;
        }
    }
    return E_FAIL;
}

HRESULT
STDMETHODCALLTYPE
DsFactoryFace::LockServer(
    _In_ BOOL Lock)
{
    if (Lock)
        InterlockedIncrement(&DsServerLocks);
    else if (DsServerLocks != 0)
        InterlockedDecrement(&DsServerLocks);
    return S_OK;
}

extern "C"
BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ PVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);

    switch (Reason)
    {
        case DLL_PROCESS_ATTACH:
            DsModule = Instance;
            DisableThreadLibraryCalls(Instance);
            InitializeCriticalSection(&DsApiLock);
            break;
        case DLL_PROCESS_DETACH:
            if (Reserved == nullptr)
                DeleteCriticalSection(&DsApiLock);
            break;
    }
    return TRUE;
}

extern "C"
HRESULT
WINAPI
DirectSoundCreate(
    _In_opt_ LPCGUID Device,
    _Outptr_ LPDIRECTSOUND* Sound,
    _Pre_null_ LPUNKNOWN Outer)
{
    DsApiGuard Guard;
    void* Created = nullptr;
    HRESULT hr;

    if (Sound == nullptr)
        return E_INVALIDARG;
    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;
    hr = DsCreateSound(DS_VERSION_7, Device, TRUE, IID_IDirectSound, &Created);
    if (SUCCEEDED(hr))
        *Sound = static_cast<LPDIRECTSOUND>(Created);
    return hr;
}

extern "C"
HRESULT
WINAPI
DirectSoundCreate8(
    _In_opt_ LPCGUID Device,
    _Outptr_ LPDIRECTSOUND8* Sound,
    _Pre_null_ LPUNKNOWN Outer)
{
    DsApiGuard Guard;
    void* Created = nullptr;
    HRESULT hr;

    if (Sound == nullptr)
        return E_INVALIDARG;
    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;
    hr = DsCreateSound(DS_VERSION_8, Device, TRUE, IID_IDirectSound8, &Created);
    if (SUCCEEDED(hr))
        *Sound = static_cast<LPDIRECTSOUND8>(Created);
    return hr;
}

extern "C"
HRESULT
WINAPI
DirectSoundCaptureCreate(
    _In_opt_ LPCGUID Device,
    _Outptr_ LPDIRECTSOUNDCAPTURE* Capture,
    _Pre_null_ LPUNKNOWN Outer)
{
    DsApiGuard Guard;
    void* Created = nullptr;
    HRESULT hr;

    if (Capture == nullptr)
        return E_INVALIDARG;
    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;
    hr = DsCreateCapture(DS_VERSION_7, Device, TRUE, IID_IDirectSoundCapture, &Created);
    if (SUCCEEDED(hr))
        *Capture = static_cast<LPDIRECTSOUNDCAPTURE>(Created);
    return hr;
}

extern "C"
HRESULT
WINAPI
DirectSoundCaptureCreate8(
    _In_opt_ LPCGUID Device,
    _Outptr_ LPDIRECTSOUNDCAPTURE8* Capture,
    _Pre_null_ LPUNKNOWN Outer)
{
    DsApiGuard Guard;
    void* Created = nullptr;
    HRESULT hr;

    if (Capture == nullptr)
        return E_INVALIDARG;
    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;
    hr = DsCreateCapture(DS_VERSION_8, Device, TRUE, IID_IDirectSoundCapture8, &Created);
    if (SUCCEEDED(hr))
        *Capture = static_cast<LPDIRECTSOUNDCAPTURE8>(Created);
    return hr;
}

extern "C"
HRESULT
WINAPI
DirectSoundFullDuplexCreate(
    _In_opt_ LPCGUID CaptureDevice,
    _In_opt_ LPCGUID RenderDevice,
    _In_ LPCDSCBUFFERDESC CaptureDesc,
    _In_ LPCDSBUFFERDESC RenderDesc,
    _In_ HWND Window,
    _In_ DWORD Level,
    _Outptr_ LPDIRECTSOUNDFULLDUPLEX* Duplex,
    _Outptr_ LPDIRECTSOUNDCAPTUREBUFFER8* CaptureBuffer,
    _Outptr_ LPDIRECTSOUNDBUFFER8* RenderBuffer,
    _Pre_null_ LPUNKNOWN Outer)
{
    DsApiGuard Guard;

    return DsCreateFullDuplex(CaptureDevice, RenderDevice, CaptureDesc, RenderDesc, Window, Level, TRUE, Outer, Duplex,
                              CaptureBuffer, RenderBuffer);
}

extern "C"
HRESULT
WINAPI
DirectSoundEnumerateA(
    _In_ LPDSENUMCALLBACKA Callback,
    _In_opt_ LPVOID Context)
{
    DsApiGuard Guard;

    return DsEnumerateEndpoints(eRender, Callback, nullptr, Context);
}

extern "C"
HRESULT
WINAPI
DirectSoundEnumerateW(
    _In_ LPDSENUMCALLBACKW Callback,
    _In_opt_ LPVOID Context)
{
    DsApiGuard Guard;

    return DsEnumerateEndpoints(eRender, nullptr, Callback, Context);
}

extern "C"
HRESULT
WINAPI
DirectSoundCaptureEnumerateA(
    _In_ LPDSENUMCALLBACKA Callback,
    _In_opt_ LPVOID Context)
{
    DsApiGuard Guard;

    return DsEnumerateEndpoints(eCapture, Callback, nullptr, Context);
}

extern "C"
HRESULT
WINAPI
DirectSoundCaptureEnumerateW(
    _In_ LPDSENUMCALLBACKW Callback,
    _In_opt_ LPVOID Context)
{
    DsApiGuard Guard;

    return DsEnumerateEndpoints(eCapture, nullptr, Callback, Context);
}

extern "C"
HRESULT
WINAPI
GetDeviceID(
    _In_ LPCGUID Source,
    _Out_ LPGUID Destination)
{
    DsApiGuard Guard;

    if (Source == nullptr || Destination == nullptr)
        return E_INVALIDARG;
    return DsResolveDeviceId(Source, Destination);
}

extern "C"
HRESULT
WINAPI
DllGetClassObject(
    _In_ REFCLSID Class,
    _In_ REFIID Iid,
    _Outptr_ LPVOID* Object)
{
    DsApiGuard Guard;
    DsFactoryKind Kind;
    DsFactory* Factory;
    HRESULT hr;

    if (Object == nullptr)
        return E_INVALIDARG;
    *Object = nullptr;

    if (Class == CLSID_DirectSound)
        Kind = DsMakeSound;
    else if (Class == CLSID_DirectSound8)
        Kind = DsMakeSound8;
    else if (Class == CLSID_DirectSoundCapture)
        Kind = DsMakeCapture;
    else if (Class == CLSID_DirectSoundCapture8)
        Kind = DsMakeCapture8;
    else if (Class == DsPrivateClass)
        Kind = DsMakePrivate;
    else if (Class == CLSID_DirectSoundFullDuplex)
        Kind = DsMakeFullDuplex;
    else
        return CLASS_E_CLASSNOTAVAILABLE;

    Factory = new (std::nothrow) DsFactory(Kind);
    if (Factory == nullptr)
        return E_OUTOFMEMORY;
    hr = Factory->Query(Iid, TRUE, Object);
    if (FAILED(hr))
        delete Factory;
    return hr;
}

extern "C"
HRESULT
WINAPI
DllCanUnloadNow(VOID)
{
    DsApiGuard Guard;

    return DsServerLocks != 0 || DsLiveObjects != 0 ? S_FALSE : S_OK;
}
