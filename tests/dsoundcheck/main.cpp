/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DirectSound answers for every entry point and argument shape, printed for comparing runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * dsoundcheck [-runtime system|reactx|<path>] [-section <name>]
 */

#define INITGUID
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <ks.h>
#include <ksmedia.h>
#include <dsound.h>
#include <dsconf.h>

/* I3DL2 reverb is marked deprecated by the header that declares it */
#pragma warning(disable: 4995)

#include <cstdio>
#include <math.h>
#include <string>
#include <vector>

typedef HRESULT (WINAPI* PDC_CREATE)(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN);
typedef HRESULT (WINAPI* PDC_CREATE8)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
typedef HRESULT (WINAPI* PDC_CAPTURE_CREATE)(LPCGUID, LPDIRECTSOUNDCAPTURE*, LPUNKNOWN);
typedef HRESULT (WINAPI* PDC_ENUMERATE_W)(LPDSENUMCALLBACKW, LPVOID);
typedef HRESULT (WINAPI* PDC_ENUMERATE_A)(LPDSENUMCALLBACKA, LPVOID);
typedef HRESULT (WINAPI* PDC_GET_DEVICE_ID)(LPCGUID, LPGUID);
typedef HRESULT (WINAPI* PDC_GET_CLASS_OBJECT)(REFCLSID, REFIID, LPVOID*);
typedef HRESULT (WINAPI* PDC_CAN_UNLOAD)(VOID);
typedef HRESULT (WINAPI* PDC_FULL_DUPLEX)(LPCGUID, LPCGUID, LPCDSCBUFFERDESC, LPCDSBUFFERDESC, HWND, DWORD,
                                          LPDIRECTSOUNDFULLDUPLEX*, LPDIRECTSOUNDCAPTUREBUFFER8*, LPDIRECTSOUNDBUFFER8*,
                                          LPUNKNOWN);

static HMODULE DcModule;
static HWND DcWindow;
static PCWSTR DcSection;
static UINT DcFirstMixCase;

static
BOOLEAN
DcWant(
    _In_z_ PCWSTR Name)
{
    return DcSection == nullptr || _wcsicmp(DcSection, Name) == 0;
}

static
VOID
DcBytes(
    _In_z_ PCSTR Name,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    printf("%s =", Name);
    for (SIZE_T Index = 0; Index < Size; Index++)
        printf(" %02X", static_cast<const BYTE*>(Data)[Index]);
    printf("\n");
}

static
VOID
DcGuid(
    _In_z_ PCSTR Name,
    _In_opt_ const GUID* Guid)
{
    if (Guid == nullptr)
    {
        printf("%s = null\n", Name);
        return;
    }
    printf("%s = {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n", Name, Guid->Data1, Guid->Data2, Guid->Data3,
           Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3], Guid->Data4[4], Guid->Data4[5],
           Guid->Data4[6], Guid->Data4[7]);
}

static
std::wstring
DcRuntimePath(
    _In_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"dsound.dll";

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\dsound.dll";
    }

    return Runtime;
}

static
VOID
DcFormat(
    _Out_ WAVEFORMATEX* Format,
    _In_ WORD Tag,
    _In_ WORD Channels,
    _In_ DWORD Rate,
    _In_ WORD Bits)
{
    Format->wFormatTag = Tag;
    Format->nChannels = Channels;
    Format->nSamplesPerSec = Rate;
    Format->wBitsPerSample = Bits;
    Format->nBlockAlign = static_cast<WORD>(Channels * Bits / 8);
    Format->nAvgBytesPerSec = Rate * Format->nBlockAlign;
    Format->cbSize = 0;
}

static
VOID
DcExtensible(
    _Out_ WAVEFORMATEXTENSIBLE* Format,
    _In_ const GUID& SubFormat,
    _In_ WORD Channels,
    _In_ DWORD Rate,
    _In_ WORD Bits,
    _In_ WORD ValidBits,
    _In_ DWORD Mask)
{
    DcFormat(&Format->Format, WAVE_FORMAT_EXTENSIBLE, Channels, Rate, Bits);
    Format->Format.cbSize = 22;
    Format->Samples.wValidBitsPerSample = ValidBits;
    Format->dwChannelMask = Mask;
    Format->SubFormat = SubFormat;
}

static
HRESULT
DcCreate8(
    _Out_ IDirectSound8** Sound)
{
    PDC_CREATE8 Create8 = reinterpret_cast<PDC_CREATE8>(GetProcAddress(DcModule, "DirectSoundCreate8"));

    *Sound = nullptr;
    return Create8(nullptr, Sound, nullptr);
}

static
HRESULT
DcCreate(
    _Out_ IDirectSound** Sound)
{
    PDC_CREATE Create = reinterpret_cast<PDC_CREATE>(GetProcAddress(DcModule, "DirectSoundCreate"));

    *Sound = nullptr;
    return Create(nullptr, Sound, nullptr);
}

static
BOOL
CALLBACK
DcEnumW(
    _In_opt_ LPGUID Guid,
    _In_z_ LPCWSTR Description,
    _In_z_ LPCWSTR Module,
    _In_opt_ LPVOID Context)
{
    ULONG* Count = static_cast<ULONG*>(Context);

    printf("enumw[%lu]\n", *Count);
    DcGuid("  guid", Guid);
    printf("  description = %ls\n", Description);
    printf("  module = %ls\n", Module);
    (*Count)++;
    return TRUE;
}

static
BOOL
CALLBACK
DcEnumA(
    _In_opt_ LPGUID Guid,
    _In_z_ LPCSTR Description,
    _In_z_ LPCSTR Module,
    _In_opt_ LPVOID Context)
{
    ULONG* Count = static_cast<ULONG*>(Context);

    printf("enuma[%lu]\n", *Count);
    DcGuid("  guid", Guid);
    printf("  description = %s\n", Description);
    printf("  module = %s\n", Module);
    (*Count)++;
    return TRUE;
}

static
BOOL
CALLBACK
DcEnumStop(
    _In_opt_ LPGUID Guid,
    _In_z_ LPCWSTR Description,
    _In_z_ LPCWSTR Module,
    _In_opt_ LPVOID Context)
{
    UNREFERENCED_PARAMETER(Guid);
    UNREFERENCED_PARAMETER(Description);
    UNREFERENCED_PARAMETER(Module);

    (*static_cast<ULONG*>(Context))++;
    return FALSE;
}

static
VOID
DcCheckEnumerate(VOID)
{
    static const PCSTR Names[] = { "DirectSoundEnumerate", "DirectSoundCaptureEnumerate" };

    for (UINT Kind = 0; Kind < 2; Kind++)
    {
        CHAR Name[64];
        ULONG Count = 0;
        PDC_ENUMERATE_W EnumW;
        PDC_ENUMERATE_A EnumA;

        sprintf_s(Name, "%sW", Names[Kind]);
        EnumW = reinterpret_cast<PDC_ENUMERATE_W>(GetProcAddress(DcModule, Name));
        sprintf_s(Name, "%sA", Names[Kind]);
        EnumA = reinterpret_cast<PDC_ENUMERATE_A>(GetProcAddress(DcModule, Name));

        printf("%s\n", Names[Kind]);
        { HRESULT Rc = EnumW(DcEnumW, &Count); printf("enumw = %08lX count %lu\n", Rc, Count); }
        Count = 0;
        { HRESULT Rc = EnumA(DcEnumA, &Count); printf("enuma = %08lX count %lu\n", Rc, Count); }
        printf("enumw_null = %08lX\n", EnumW(nullptr, nullptr));
        printf("enuma_null = %08lX\n", EnumA(nullptr, nullptr));
        Count = 0;
        { HRESULT Rc = EnumW(DcEnumStop, &Count); printf("enum_stop = %08lX count %lu\n", Rc, Count); }
    }
}

static
VOID
DcCheckDeviceId(VOID)
{
    static const GUID* Ids[] =
    {
        &DSDEVID_DefaultPlayback, &DSDEVID_DefaultCapture, &DSDEVID_DefaultVoicePlayback,
        &DSDEVID_DefaultVoiceCapture, &GUID_NULL, &IID_IDirectSound,
    };
    PDC_GET_DEVICE_ID GetDeviceID = reinterpret_cast<PDC_GET_DEVICE_ID>(GetProcAddress(DcModule, "GetDeviceID"));
    GUID Result;

    for (UINT Index = 0; Index < ARRAYSIZE(Ids); Index++)
    {
        memset(&Result, 0xCC, sizeof(Result));
        printf("getdeviceid[%u] = %08lX\n", Index, GetDeviceID(Ids[Index], &Result));
        DcGuid("  id", &Result);
    }
    printf("getdeviceid_null_src = %08lX\n", GetDeviceID(nullptr, &Result));
    printf("getdeviceid_null_dst = %08lX\n", GetDeviceID(&DSDEVID_DefaultPlayback, nullptr));
}

static
VOID
DcCheckInterfaces(
    _In_z_ PCSTR Name,
    _In_ IUnknown* Object)
{
    static const struct
    {
        const IID* Iid;
        PCSTR Name;
    } Iids[] =
    {
        { &IID_IUnknown, "IUnknown" },
        { &IID_IDirectSound, "IDirectSound" },
        { &IID_IDirectSound8, "IDirectSound8" },
        { &IID_IDirectSoundBuffer, "IDirectSoundBuffer" },
        { &IID_IDirectSoundBuffer8, "IDirectSoundBuffer8" },
        { &IID_IDirectSound3DBuffer, "IDirectSound3DBuffer" },
        { &IID_IDirectSound3DListener, "IDirectSound3DListener" },
        { &IID_IDirectSoundNotify, "IDirectSoundNotify" },
        { &IID_IKsPropertySet, "IKsPropertySet" },
        { &IID_IDirectSoundCapture, "IDirectSoundCapture" },
        { &IID_IDirectSoundCaptureBuffer, "IDirectSoundCaptureBuffer" },
        { &IID_IDirectSoundCaptureBuffer8, "IDirectSoundCaptureBuffer8" },
        { &IID_IDirectSoundFullDuplex, "IDirectSoundFullDuplex" },
        { &IID_IClassFactory, "IClassFactory" },
    };

    for (UINT Index = 0; Index < ARRAYSIZE(Iids); Index++)
    {
        IUnknown* Found = nullptr;
        HRESULT hr = Object->QueryInterface(*Iids[Index].Iid, reinterpret_cast<void**>(&Found));

        if (SUCCEEDED(hr))
        {
            ULONG Count = Found->AddRef();

            printf("%s qi %s = %08lX same %d ref %lu\n", Name, Iids[Index].Name, hr, Found == Object, Count);
            Found->Release();
            Found->Release();
        }
        else
        {
            printf("%s qi %s = %08lX null %d\n", Name, Iids[Index].Name, hr, Found == nullptr);
        }
    }
    printf("%s qi_null = %08lX\n", Name, Object->QueryInterface(IID_IUnknown, nullptr));
}

static
VOID
DcCheckCreate(VOID)
{
    PDC_CREATE Create = reinterpret_cast<PDC_CREATE>(GetProcAddress(DcModule, "DirectSoundCreate"));
    PDC_CREATE8 Create8 = reinterpret_cast<PDC_CREATE8>(GetProcAddress(DcModule, "DirectSoundCreate8"));
    static const GUID* Devices[] =
    {
        nullptr, &GUID_NULL, &DSDEVID_DefaultPlayback, &DSDEVID_DefaultVoicePlayback, &DSDEVID_DefaultCapture,
        &IID_IDirectSound,
    };
    IDirectSound* Sound;
    IDirectSound8* Sound8;
    IUnknown* Outer = reinterpret_cast<IUnknown*>(1);

    printf("create_null = %08lX\n", Create(nullptr, nullptr, nullptr));
    Sound = reinterpret_cast<IDirectSound*>(1);
    printf("create_outer = %08lX\n", Create(nullptr, &Sound, Outer));
    printf("  out %d\n", Sound == reinterpret_cast<IDirectSound*>(1) ? 1 : Sound == nullptr ? 0 : 2);
    printf("create8_null = %08lX\n", Create8(nullptr, nullptr, nullptr));
    for (UINT Index = 0; Index < ARRAYSIZE(Devices); Index++)
    {
        Sound = reinterpret_cast<IDirectSound*>(1);
        HRESULT hr = Create(Devices[Index], &Sound, nullptr);

        printf("create[%u] = %08lX out %d\n", Index, hr, Sound == nullptr ? 0 : Sound == reinterpret_cast<IDirectSound*>(1) ? 1 : 2);
        if (SUCCEEDED(hr))
            printf("  release %lu\n", Sound->Release());
        Sound8 = reinterpret_cast<IDirectSound8*>(1);
        hr = Create8(Devices[Index], &Sound8, nullptr);
        printf("create8[%u] = %08lX out %d\n", Index, hr, Sound8 == nullptr ? 0 : Sound8 == reinterpret_cast<IDirectSound8*>(1) ? 1 : 2);
        if (SUCCEEDED(hr))
            printf("  release %lu\n", Sound8->Release());
    }

    if (SUCCEEDED(DcCreate(&Sound)))
    {
        printf("ds addref %lu\n", Sound->AddRef());
        printf("ds release %lu\n", Sound->Release());
        DcCheckInterfaces("ds", Sound);
        printf("ds initialize = %08lX\n", Sound->Initialize(nullptr));
        printf("ds release %lu\n", Sound->Release());
    }
    if (SUCCEEDED(DcCreate8(&Sound8)))
    {
        IUnknown* Unknown = nullptr;

        DcCheckInterfaces("ds8", Sound8);
        printf("ds8 initialize = %08lX\n", Sound8->Initialize(nullptr));
        printf("ds8 verify_null = %08lX\n", Sound8->VerifyCertification(nullptr));
        {
            DWORD Certified = 0xCCCCCCCC;

            { HRESULT Rc = Sound8->VerifyCertification(&Certified); printf("ds8 verify = %08lX %08lX\n", Rc, Certified); }
        }
        Sound8->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&Unknown));
        printf("ds8 unknown addref %lu\n", Unknown->AddRef());
        printf("ds8 unknown release %lu\n", Unknown->Release());
        printf("ds8 unknown release %lu\n", Unknown->Release());
        printf("ds8 release %lu\n", Sound8->Release());
    }
}

static
VOID
DcCheckClassFactory(VOID)
{
    PDC_GET_CLASS_OBJECT GetClassObject = reinterpret_cast<PDC_GET_CLASS_OBJECT>(GetProcAddress(DcModule, "DllGetClassObject"));
    PDC_CAN_UNLOAD CanUnload = reinterpret_cast<PDC_CAN_UNLOAD>(GetProcAddress(DcModule, "DllCanUnloadNow"));
    static const struct
    {
        const CLSID* Clsid;
        PCSTR Name;
    } Classes[] =
    {
        { &CLSID_DirectSound, "DirectSound" },
        { &CLSID_DirectSound8, "DirectSound8" },
        { &CLSID_DirectSoundCapture, "DirectSoundCapture" },
        { &CLSID_DirectSoundCapture8, "DirectSoundCapture8" },
        { &CLSID_DirectSoundFullDuplex, "DirectSoundFullDuplex" },
        { &IID_IDirectSound, "bogus" },
    };
    IClassFactory* Factory;

    printf("getclassobject_null = %08lX\n", GetClassObject(CLSID_DirectSound, IID_IClassFactory, nullptr));
    for (UINT Index = 0; Index < ARRAYSIZE(Classes); Index++)
    {
        HRESULT hr;

        Factory = reinterpret_cast<IClassFactory*>(1);
        hr = GetClassObject(*Classes[Index].Clsid, IID_IClassFactory, reinterpret_cast<void**>(&Factory));
        printf("getclassobject %s = %08lX null %d\n", Classes[Index].Name, hr, Factory == nullptr);
        if (FAILED(hr))
            continue;

        printf("  addref %lu\n", Factory->AddRef());
        printf("  release %lu\n", Factory->Release());
        printf("  lock = %08lX\n", Factory->LockServer(TRUE));
        printf("  canunload = %08lX\n", CanUnload());
        printf("  unlock = %08lX\n", Factory->LockServer(FALSE));
        {
            IUnknown* Object = reinterpret_cast<IUnknown*>(1);

            hr = Factory->CreateInstance(reinterpret_cast<IUnknown*>(1), IID_IUnknown, reinterpret_cast<void**>(&Object));
            printf("  create_outer = %08lX null %d\n", hr, Object == nullptr);
            printf("  create_null = %08lX\n", Factory->CreateInstance(nullptr, IID_IUnknown, nullptr));
        }
        if (Index < 2)
        {
            IDirectSound8* Sound = nullptr;

            hr = Factory->CreateInstance(nullptr, Index == 0 ? IID_IDirectSound : IID_IDirectSound8, reinterpret_cast<void**>(&Sound));
            printf("  create = %08lX\n", hr);
            if (SUCCEEDED(hr))
            {
                DSCAPS Caps = {};
                DWORD Config = 0;
                IDirectSoundBuffer* Buffer = reinterpret_cast<IDirectSoundBuffer*>(1);
                DSBUFFERDESC Desc = {};

                Caps.dwSize = sizeof(Caps);
                Desc.dwSize = sizeof(Desc);
                Desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
                printf("  uninit getcaps = %08lX\n", Sound->GetCaps(&Caps));
                printf("  uninit getcaps_null = %08lX\n", Sound->GetCaps(nullptr));
                printf("  uninit coop = %08lX\n", Sound->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY));
                printf("  uninit compact = %08lX\n", Sound->Compact());
                printf("  uninit speaker = %08lX\n", Sound->GetSpeakerConfig(&Config));
                printf("  uninit create = %08lX\n", Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr));
                printf("  uninit buffer %d\n", Buffer == nullptr ? 0 : Buffer == reinterpret_cast<IDirectSoundBuffer*>(1) ? 1 : 2);
                if (Index == 1)
                {
                    DWORD Certified = 0;

                    printf("  uninit verify = %08lX\n", Sound->VerifyCertification(&Certified));
                }
                DcCheckInterfaces("  uninit", Sound);
                printf("  initialize = %08lX\n", Sound->Initialize(nullptr));
                printf("  initialize_again = %08lX\n", Sound->Initialize(nullptr));
                printf("  getcaps = %08lX\n", Sound->GetCaps(&Caps));
                DcCheckInterfaces("  init", Sound);
                printf("  release %lu\n", Sound->Release());
            }
        }
        if (Index == 2 || Index == 3)
        {
            IDirectSoundCapture* Capture = nullptr;

            hr = Factory->CreateInstance(nullptr, IID_IDirectSoundCapture, reinterpret_cast<void**>(&Capture));
            printf("  create = %08lX\n", hr);
            if (SUCCEEDED(hr))
            {
                DSCCAPS Caps = {};

                Caps.dwSize = sizeof(Caps);
                printf("  uninit getcaps = %08lX\n", Capture->GetCaps(&Caps));
                printf("  initialize = %08lX\n", Capture->Initialize(nullptr));
                printf("  initialize_again = %08lX\n", Capture->Initialize(nullptr));
                printf("  release %lu\n", Capture->Release());
            }
        }
        printf("  release %lu\n", Factory->Release());
    }
    printf("canunload = %08lX\n", CanUnload());
}

static
VOID
DcCheckDevice(VOID)
{
    IDirectSound8* Sound;
    DSCAPS Caps;
    DWORD Config;

    if (FAILED(DcCreate8(&Sound)))
        return;

    for (DWORD Size = 0; Size < 2; Size++)
    {
        memset(&Caps, 0xCC, sizeof(Caps));
        Caps.dwSize = Size == 0 ? sizeof(Caps) : sizeof(Caps) - 4;
        printf("getcaps[%lu] = %08lX\n", Size, Sound->GetCaps(&Caps));
        DcBytes("  caps", &Caps, sizeof(Caps));
    }
    printf("getcaps_null = %08lX\n", Sound->GetCaps(nullptr));

    Config = 0xCCCCCCCC;
    printf("getspeaker = %08lX\n", Sound->GetSpeakerConfig(&Config));
    printf("  config = %08lX\n", Config);
    printf("getspeaker_null = %08lX\n", Sound->GetSpeakerConfig(nullptr));
    {
        static const DWORD Bad[] = { 10, 0x100, 0x01000000, DSSPEAKER_COMBINED(DSSPEAKER_QUAD, 5),
                                     DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 0xB5), DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 0xFF) };

        for (UINT Index = 0; Index < ARRAYSIZE(Bad); Index++)
            printf("setspeaker_bad[%u] = %08lX\n", Index, Sound->SetSpeakerConfig(Bad[Index]));
    }
    printf("setspeaker_same = %08lX\n", Sound->SetSpeakerConfig(Config));

    printf("compact_nocoop = %08lX\n", Sound->Compact());
    printf("coop_nullwnd = %08lX\n", Sound->SetCooperativeLevel(nullptr, DSSCL_NORMAL));
    for (DWORD Level = 0; Level < 6; Level++)
    {
        printf("coop[%lu] = %08lX\n", Level, Sound->SetCooperativeLevel(DcWindow, Level));
        printf("  compact = %08lX\n", Sound->Compact());
    }
    printf("coop_desktop = %08lX\n", Sound->SetCooperativeLevel(GetDesktopWindow(), DSSCL_PRIORITY));
    printf("release %lu\n", Sound->Release());
}

static
VOID
DcDescribe(
    _In_ IDirectSoundBuffer* Buffer)
{
    DSBCAPS Caps;
    BYTE Format[64];
    DWORD Size = 0;
    DWORD Status = 0xCCCCCCCC;
    LONG Value;
    DWORD Frequency;
    DWORD Play;
    DWORD Write;
    HRESULT hr;

    memset(&Caps, 0xCC, sizeof(Caps));
    Caps.dwSize = sizeof(Caps);
    printf("  getcaps = %08lX\n", Buffer->GetCaps(&Caps));
    DcBytes("    caps", &Caps, sizeof(Caps));
    memset(Format, 0xCC, sizeof(Format));
    hr = Buffer->GetFormat(nullptr, 0, &Size);
    printf("  getformat_size = %08lX %lu\n", hr, Size);
    Size = 0xCCCC;
    hr = Buffer->GetFormat(reinterpret_cast<WAVEFORMATEX*>(Format), sizeof(Format), &Size);
    printf("  getformat = %08lX %lu\n", hr, Size);
    DcBytes("    format", Format, 40);
    Size = 0xCCCC;
    hr = Buffer->GetFormat(reinterpret_cast<WAVEFORMATEX*>(Format), 4, &Size);
    printf("  getformat_small = %08lX %lu\n", hr, Size);
    printf("  getformat_none = %08lX\n", Buffer->GetFormat(nullptr, 0, nullptr));
    { HRESULT Rc = Buffer->GetStatus(&Status); printf("  getstatus = %08lX %08lX\n", Rc, Status); }
    Value = 0x1234;
    { HRESULT Rc = Buffer->GetVolume(&Value); printf("  getvolume = %08lX %ld\n", Rc, Value); }
    Value = 0x1234;
    { HRESULT Rc = Buffer->GetPan(&Value); printf("  getpan = %08lX %ld\n", Rc, Value); }
    Frequency = 0x1234;
    { HRESULT Rc = Buffer->GetFrequency(&Frequency); printf("  getfrequency = %08lX %lu\n", Rc, Frequency); }
    Play = Write = 0xCCCC;
    { HRESULT Rc = Buffer->GetCurrentPosition(&Play, &Write); printf("  getposition = %08lX %lu %lu\n", Rc, Play, Write); }
}

static
HRESULT
DcMakeBuffer(
    _In_ IDirectSound8* Sound,
    _In_ DWORD Flags,
    _In_ DWORD Bytes,
    _In_opt_ WAVEFORMATEX* Format,
    _Out_ IDirectSoundBuffer** Buffer)
{
    DSBUFFERDESC Desc = {};

    Desc.dwSize = sizeof(Desc);
    Desc.dwFlags = Flags;
    Desc.dwBufferBytes = Bytes;
    Desc.lpwfxFormat = Format;
    *Buffer = nullptr;
    return Sound->CreateSoundBuffer(&Desc, Buffer, nullptr);
}

static
VOID
DcCheckBufferDescs(VOID)
{
    IDirectSound8* Sound;
    IDirectSound* Sound7;
    WAVEFORMATEX Pcm16;
    WAVEFORMATEXTENSIBLE Ext;
    IDirectSoundBuffer* Buffer;
    DSBUFFERDESC Desc;
    HRESULT hr;

    if (FAILED(DcCreate8(&Sound)))
        return;

    DcFormat(&Pcm16, WAVE_FORMAT_PCM, 2, 44100, 16);
    printf("create_nocoop = %08lX\n", hr = DcMakeBuffer(Sound, 0, 4096, &Pcm16, &Buffer));
    if (SUCCEEDED(hr))
        printf("  release %lu\n", Buffer->Release());
    Sound->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY);

    printf("create_nulldesc = %08lX\n", Sound->CreateSoundBuffer(nullptr, &Buffer, nullptr));
    Desc = {};
    Desc.dwSize = sizeof(Desc);
    Desc.dwBufferBytes = 4096;
    Desc.lpwfxFormat = &Pcm16;
    printf("create_nullout = %08lX\n", Sound->CreateSoundBuffer(&Desc, nullptr, nullptr));
    Buffer = reinterpret_cast<IDirectSoundBuffer*>(1);
    printf("create_outer = %08lX\n", Sound->CreateSoundBuffer(&Desc, &Buffer, reinterpret_cast<IUnknown*>(1)));
    printf("  out %d\n", Buffer == nullptr ? 0 : Buffer == reinterpret_cast<IDirectSoundBuffer*>(1) ? 1 : 2);

    for (DWORD Size = 0; Size <= 44; Size += 4)
    {
        Desc.dwSize = Size;
        Buffer = nullptr;
        hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
        printf("create_size[%lu] = %08lX\n", Size, hr);
        if (SUCCEEDED(hr))
            Buffer->Release();
    }
    Desc.dwSize = sizeof(Desc);
    Desc.dwReserved = 1;
    printf("create_reserved = %08lX\n", Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr));
    Desc.dwReserved = 0;

    {
        static const DWORD Sizes[] = { 0, 1, 3, 4, 5, 4095, 0x0FFFFFFF + 1, 0xFFFFFFFF };

        for (UINT Index = 0; Index < ARRAYSIZE(Sizes); Index++)
        {
            Desc.dwBufferBytes = Sizes[Index];
            Buffer = nullptr;
            hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
            printf("create_bytes[%lu] = %08lX\n", Sizes[Index], hr);
            if (SUCCEEDED(hr))
            {
                DSBCAPS Caps = { sizeof(Caps) };

                Buffer->GetCaps(&Caps);
                printf("  bytes %lu\n", Caps.dwBufferBytes);
                Buffer->Release();
            }
        }
        Desc.dwBufferBytes = 4096;
    }

    for (DWORD Bit = 0; Bit < 32; Bit++)
    {
        Desc.dwFlags = 1UL << Bit;
        Buffer = nullptr;
        hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
        printf("create_flag[%lu] = %08lX\n", Bit, hr);
        if (SUCCEEDED(hr))
        {
            DSBCAPS Caps = { sizeof(Caps) };

            Buffer->GetCaps(&Caps);
            printf("  flags %08lX\n", Caps.dwFlags);
            Buffer->Release();
        }
    }
    {
        static const DWORD Combos[] =
        {
            DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE, DSBCAPS_LOCDEFER | DSBCAPS_LOCSOFTWARE,
            DSBCAPS_MUTE3DATMAXDISTANCE, DSBCAPS_MUTE3DATMAXDISTANCE | DSBCAPS_CTRL3D, DSBCAPS_STATIC | DSBCAPS_CTRLFX,
            DSBCAPS_CTRL3D | DSBCAPS_CTRLPAN, DSBCAPS_CTRLFX | DSBCAPS_CTRLVOLUME, DSBCAPS_CTRLFX | DSBCAPS_CTRLFREQUENCY,
            DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPOSITIONNOTIFY,
            DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_STICKYFOCUS,
        };

        for (UINT Index = 0; Index < ARRAYSIZE(Combos); Index++)
        {
            Desc.dwFlags = Combos[Index];
            Buffer = nullptr;
            hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
            printf("create_combo[%u] = %08lX\n", Index, hr);
            if (SUCCEEDED(hr))
            {
                DSBCAPS Caps = { sizeof(Caps) };

                Buffer->GetCaps(&Caps);
                printf("  flags %08lX\n", Caps.dwFlags);
                Buffer->Release();
            }
        }
        Desc.dwFlags = 0;
    }

    {
        static const struct
        {
            WORD Tag;
            WORD Channels;
            DWORD Rate;
            WORD Bits;
        } Formats[] =
        {
            { WAVE_FORMAT_PCM, 1, 8000, 8 }, { WAVE_FORMAT_PCM, 2, 11025, 16 }, { WAVE_FORMAT_PCM, 2, 48000, 24 },
            { WAVE_FORMAT_PCM, 2, 48000, 32 }, { WAVE_FORMAT_PCM, 6, 48000, 16 }, { WAVE_FORMAT_PCM, 0, 48000, 16 },
            { WAVE_FORMAT_PCM, 2, 48000, 12 }, { WAVE_FORMAT_PCM, 2, 99, 16 }, { WAVE_FORMAT_PCM, 2, 100, 16 },
            { WAVE_FORMAT_PCM, 2, 200000, 16 }, { WAVE_FORMAT_PCM, 2, 200001, 16 }, { WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 32 },
            { WAVE_FORMAT_IEEE_FLOAT, 1, 44100, 32 }, { WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 16 }, { WAVE_FORMAT_ADPCM, 2, 22050, 4 },
            { 0x55, 2, 44100, 0 }, { WAVE_FORMAT_PCM, 3, 44100, 16 }, { WAVE_FORMAT_PCM, 1, 22050, 16 },
        };

        for (UINT Index = 0; Index < ARRAYSIZE(Formats); Index++)
        {
            WAVEFORMATEX Format;

            DcFormat(&Format, Formats[Index].Tag, Formats[Index].Channels, Formats[Index].Rate, Formats[Index].Bits);
            Desc.lpwfxFormat = &Format;
            Desc.dwBufferBytes = 4800;
            for (UINT Ctrl = 0; Ctrl < 3; Ctrl++)
            {
                Desc.dwFlags = Ctrl == 0 ? 0 : Ctrl == 1 ? DSBCAPS_CTRL3D : DSBCAPS_CTRLFX;
                /* The system divides by the block alignment here and faults on zero */
                if (Ctrl == 2 && Format.nBlockAlign == 0)
                    continue;
                Buffer = nullptr;
                hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
                printf("create_format[%u][%u] = %08lX\n", Index, Ctrl, hr);
                if (SUCCEEDED(hr))
                {
                    if (Ctrl == 0)
                        DcDescribe(Buffer);
                    Buffer->Release();
                }
            }
        }
        Desc.dwFlags = 0;

        {
            WAVEFORMATEX Format;

            DcFormat(&Format, WAVE_FORMAT_PCM, 2, 44100, 16);
            Format.nBlockAlign = 3;
            Desc.lpwfxFormat = &Format;
            printf("create_badalign = %08lX\n", Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr));
            DcFormat(&Format, WAVE_FORMAT_PCM, 2, 44100, 16);
            Format.nAvgBytesPerSec = 44100 * 4 * 21 / 20 + 4;
            printf("create_badavg_high = %08lX\n", Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr));
            Format.nAvgBytesPerSec = 44100 * 4 * 21 / 20 - 4;
            hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
            printf("create_okavg = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Buffer->Release();
            Format.nAvgBytesPerSec = 44100 * 4 * 19 / 20 - 4;
            printf("create_badavg_low = %08lX\n", Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr));
            DcFormat(&Format, WAVE_FORMAT_PCM, 2, 44100, 16);
            Desc.dwBufferBytes = 4097;
            hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
            printf("create_oddbytes = %08lX\n", hr);
            if (SUCCEEDED(hr))
            {
                DSBCAPS Caps = { sizeof(Caps) };

                Buffer->GetCaps(&Caps);
                printf("  bytes %lu\n", Caps.dwBufferBytes);
                Buffer->Release();
            }
            Desc.dwBufferBytes = 4096;
            Desc.dwFlags = DSBCAPS_CTRLFX;
            Desc.dwBufferBytes = 44100 * 4 * 150 / 1000 - 4;
            printf("create_fx_small = %08lX\n", Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr));
            Desc.dwBufferBytes = 44100 * 4 * 150 / 1000;
            hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
            printf("create_fx_ok = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Buffer->Release();
            Desc.dwFlags = DSBCAPS_CTRL3D;
            Desc.guid3DAlgorithm = DS3DALG_HRTF_FULL;
            Desc.dwBufferBytes = 4096;
            DcFormat(&Format, WAVE_FORMAT_PCM, 1, 44100, 16);
            hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
            printf("create_hrtf = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Buffer->Release();
            Desc.guid3DAlgorithm = DS3DALG_HRTF_LIGHT;
            Desc.dwFlags = 0;
            hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
            printf("create_alg_no3d = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Buffer->Release();
            Desc.guid3DAlgorithm = IID_IUnknown;
            Desc.dwFlags = DSBCAPS_CTRL3D;
            hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
            printf("create_alg_bogus = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Buffer->Release();
            Desc.guid3DAlgorithm = GUID_NULL;
            Desc.dwFlags = 0;
        }

        {
            static const struct
            {
                const GUID* SubFormat;
                WORD Channels;
                WORD Bits;
                WORD Valid;
                DWORD Mask;
                WORD CbSize;
            } Exts[] =
            {
                { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 16, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 16, 3, 20 },
                { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 17, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 16, 7, 22 },
                { &GUID_NULL, 2, 16, 16, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 2, 32, 32, 3, 22 },
                { &KSDATAFORMAT_SUBTYPE_PCM, 6, 16, 16, 0x3F, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 6, 16, 16, 0, 22 },
                { &KSDATAFORMAT_SUBTYPE_PCM, 2, 24, 20, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 2, 32, 24, 3, 22 },
                { &KSDATAFORMAT_SUBTYPE_PCM, 2, 32, 28, 3, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 2, 16, 0, 3, 22 },
                { &KSDATAFORMAT_SUBTYPE_PCM, 1, 16, 16, 0, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 4, 16, 16, 0x33, 22 },
                { &KSDATAFORMAT_SUBTYPE_PCM, 8, 16, 16, 0x63F, 22 }, { &KSDATAFORMAT_SUBTYPE_PCM, 3, 16, 16, 3, 22 },
            };

            for (UINT Index = 0; Index < ARRAYSIZE(Exts); Index++)
            {
                DcExtensible(&Ext, *Exts[Index].SubFormat, Exts[Index].Channels, 48000, Exts[Index].Bits,
                             Exts[Index].Valid, Exts[Index].Mask);
                Ext.Format.cbSize = Exts[Index].CbSize;
                Desc.lpwfxFormat = &Ext.Format;
                Desc.dwBufferBytes = 4800;
                for (UINT Ctrl = 0; Ctrl < 2; Ctrl++)
                {
                    Desc.dwFlags = Ctrl == 0 ? 0 : DSBCAPS_CTRL3D;
                    Buffer = nullptr;
                    hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
                    printf("create_ext[%u][%u] = %08lX\n", Index, Ctrl, hr);
                    if (SUCCEEDED(hr))
                    {
                        if (Ctrl == 0)
                            DcDescribe(Buffer);
                        Buffer->Release();
                    }
                }
            }
            Desc.dwFlags = 0;
        }
    }

    Desc = {};
    Desc.dwSize = sizeof(Desc);
    Desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    {
        static const DWORD Flags[] =
        {
            0, DSBCAPS_CTRLVOLUME, DSBCAPS_CTRLPAN, DSBCAPS_CTRL3D, DSBCAPS_CTRLFREQUENCY, DSBCAPS_STATIC,
            DSBCAPS_CTRLPOSITIONNOTIFY, DSBCAPS_CTRLFX, DSBCAPS_GLOBALFOCUS, DSBCAPS_STICKYFOCUS, DSBCAPS_LOCSOFTWARE,
            DSBCAPS_LOCHARDWARE, DSBCAPS_GETCURRENTPOSITION2, DSBCAPS_LOCDEFER, DSBCAPS_TRUEPLAYPOSITION,
        };

        for (UINT Index = 0; Index < ARRAYSIZE(Flags); Index++)
        {
            Desc.dwFlags = DSBCAPS_PRIMARYBUFFER | Flags[Index];
            Buffer = nullptr;
            hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
            printf("create_primary[%u] = %08lX\n", Index, hr);
            if (SUCCEEDED(hr))
            {
                DSBCAPS Caps = { sizeof(Caps) };

                Buffer->GetCaps(&Caps);
                printf("  flags %08lX bytes %lu\n", Caps.dwFlags, Caps.dwBufferBytes);
                printf("  release %lu\n", Buffer->Release());
            }
        }
    }
    Desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    Desc.dwBufferBytes = 4;
    printf("create_primary_bytes = %08lX\n", Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr));
    Desc.dwBufferBytes = 0;
    Desc.lpwfxFormat = &Pcm16;
    printf("create_primary_format = %08lX\n", Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr));
    Desc.lpwfxFormat = nullptr;
    Desc.guid3DAlgorithm = DS3DALG_HRTF_FULL;
    printf("create_primary_alg = %08lX\n", Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr));
    Desc.guid3DAlgorithm = GUID_NULL;
    printf("release %lu\n", Sound->Release());

    /* The flags only DirectSound 8 takes */
    if (SUCCEEDED(DcCreate(&Sound7)))
    {
        Sound7->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY);
        Desc = {};
        Desc.dwSize = sizeof(Desc);
        Desc.dwBufferBytes = 44100;
        Desc.lpwfxFormat = &Pcm16;
        {
            static const DWORD Flags[] = { DSBCAPS_CTRLFX, DSBCAPS_LOCDEFER, DSBCAPS_CTRL3D, DSBCAPS_TRUEPLAYPOSITION };

            for (UINT Index = 0; Index < ARRAYSIZE(Flags); Index++)
            {
                Desc.dwFlags = Flags[Index];
                hr = Sound7->CreateSoundBuffer(&Desc, &Buffer, nullptr);
                printf("ds7 create[%u] = %08lX\n", Index, hr);
                if (SUCCEEDED(hr))
                {
                    DcCheckInterfaces("ds7 buffer", Buffer);
                    Buffer->Release();
                }
            }
        }
        Desc.dwSize = sizeof(DSBUFFERDESC1);
        Desc.dwFlags = 0;
        hr = Sound7->CreateSoundBuffer(&Desc, &Buffer, nullptr);
        printf("ds7 create_desc1 = %08lX\n", hr);
        if (SUCCEEDED(hr))
            Buffer->Release();
        printf("ds7 release %lu\n", Sound7->Release());
    }
}

static
VOID
DcCheckBufferMethods(VOID)
{
    IDirectSound8* Sound;
    IDirectSoundBuffer* Buffer;
    IDirectSoundBuffer* Other;
    WAVEFORMATEX Format;
    HRESULT hr;

    if (FAILED(DcCreate8(&Sound)))
        return;
    Sound->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY);
    DcFormat(&Format, WAVE_FORMAT_PCM, 2, 22050, 16);

    for (UINT Ctrl = 0; Ctrl < 2; Ctrl++)
    {
        DWORD Flags = Ctrl == 0 ? 0 : DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPOSITIONNOTIFY;
        static const LONG Volumes[] = { 1, 0, -1, -10000, -10001, LONG_MIN };
        static const LONG Pans[] = { 10001, 10000, 0, -10000, -10001 };
        static const DWORD Frequencies[] = { 0, 1, 99, 100, 22050, 200000, 200001 };

        if (FAILED(DcMakeBuffer(Sound, Flags, 8820, &Format, &Buffer)))
            continue;
        printf("methods[%u]\n", Ctrl);
        DcCheckInterfaces("  buffer", Buffer);
        printf("  addref %lu\n", Buffer->AddRef());
        printf("  release %lu\n", Buffer->Release());
        for (UINT Index = 0; Index < ARRAYSIZE(Volumes); Index++)
        {
            LONG Value = 0x1234;

            hr = Buffer->SetVolume(Volumes[Index]);
            Buffer->GetVolume(&Value);
            printf("  setvolume[%ld] = %08lX get %ld\n", Volumes[Index], hr, Value);
        }
        printf("  getvolume_null = %08lX\n", Buffer->GetVolume(nullptr));
        for (UINT Index = 0; Index < ARRAYSIZE(Pans); Index++)
        {
            LONG Value = 0x1234;

            hr = Buffer->SetPan(Pans[Index]);
            Buffer->GetPan(&Value);
            printf("  setpan[%ld] = %08lX get %ld\n", Pans[Index], hr, Value);
        }
        printf("  getpan_null = %08lX\n", Buffer->GetPan(nullptr));
        for (UINT Index = 0; Index < ARRAYSIZE(Frequencies); Index++)
        {
            DWORD Value = 0x1234;

            hr = Buffer->SetFrequency(Frequencies[Index]);
            Buffer->GetFrequency(&Value);
            printf("  setfrequency[%lu] = %08lX get %lu\n", Frequencies[Index], hr, Value);
        }
        printf("  getfrequency_null = %08lX\n", Buffer->GetFrequency(nullptr));
        printf("  setformat = %08lX\n", Buffer->SetFormat(&Format));
        printf("  setformat_null = %08lX\n", Buffer->SetFormat(nullptr));
        printf("  getstatus_null = %08lX\n", Buffer->GetStatus(nullptr));
        printf("  getcaps_null = %08lX\n", Buffer->GetCaps(nullptr));
        {
            DSBCAPS Caps = {};

            Caps.dwSize = sizeof(Caps) + 4;
            printf("  getcaps_big = %08lX\n", Buffer->GetCaps(&Caps));
        }
        printf("  initialize = %08lX\n", Buffer->Initialize(Sound, nullptr));
        printf("  restore = %08lX\n", Buffer->Restore());

        {
            static const DWORD Positions[] = { 0, 1, 4, 8819, 8820, 0xFFFFFFFF };

            for (UINT Index = 0; Index < ARRAYSIZE(Positions); Index++)
            {
                DWORD Play = 0xCCCC;
                DWORD Write = 0xCCCC;

                hr = Buffer->SetCurrentPosition(Positions[Index]);
                Buffer->GetCurrentPosition(&Play, &Write);
                printf("  setposition[%lu] = %08lX get %lu %lu\n", Positions[Index], hr, Play, Write);
            }
            printf("  getposition_none = %08lX\n", Buffer->GetCurrentPosition(nullptr, nullptr));
            {
                DWORD Write = 0xCCCC;

                { HRESULT Rc = Buffer->GetCurrentPosition(nullptr, &Write); printf("  getposition_write = %08lX %lu\n", Rc, Write); }
            }
        }

        {
            static const struct
            {
                DWORD Offset;
                DWORD Bytes;
                DWORD Flags;
            } Locks[] =
            {
                { 0, 100, 0 }, { 8800, 100, 0 }, { 8820, 1, 0 }, { 0, 8820, 0 }, { 0, 8821, 0 }, { 0, 0, 0 },
                { 0, 0, DSBLOCK_ENTIREBUFFER }, { 100, 0, DSBLOCK_FROMWRITECURSOR }, { 0, 100, 4 }, { 1, 3, 0 },
                { 8819, 8820, 0 }, { 0, 100, DSBLOCK_FROMWRITECURSOR | DSBLOCK_ENTIREBUFFER },
            };

            for (UINT Index = 0; Index < ARRAYSIZE(Locks); Index++)
            {
                VOID* Part1 = reinterpret_cast<VOID*>(1);
                VOID* Part2 = reinterpret_cast<VOID*>(1);
                DWORD Size1 = 0xCCCC;
                DWORD Size2 = 0xCCCC;
                BYTE* Base;

                hr = Buffer->Lock(Locks[Index].Offset, Locks[Index].Bytes, &Part1, &Size1, &Part2, &Size2, Locks[Index].Flags);
                Base = static_cast<BYTE*>(Part1);
                printf("  lock[%u] = %08lX size %lu %lu part1 %d part2 %s\n", Index, hr, Size1, Size2, Part1 != nullptr,
                       Part2 == nullptr ? "null" : Part2 == reinterpret_cast<VOID*>(1) ? "kept" : "set");
                if (SUCCEEDED(hr))
                {
                    if (Part2 != nullptr && Part1 != nullptr)
                        printf("    gap %lld\n", static_cast<long long>(static_cast<BYTE*>(Part1) - static_cast<BYTE*>(Part2)));
                    printf("    first %02X\n", Base[0]);
                    printf("    unlock = %08lX\n", Buffer->Unlock(Part1, Size1, Part2, Size2));
                }
            }
        }

        {
            VOID* Part1;
            VOID* Part2;
            DWORD Size1;
            DWORD Size2;
            VOID* Other1;
            DWORD OtherSize;

            printf("  lock_null1 = %08lX\n", Buffer->Lock(0, 100, nullptr, &Size1, nullptr, nullptr, 0));
            printf("  lock_nullsize1 = %08lX\n", Buffer->Lock(0, 100, &Part1, nullptr, nullptr, nullptr, 0));
            hr = Buffer->Lock(0, 100, &Part1, &Size1, nullptr, nullptr, 0);
            printf("  lock_onlypart1 = %08lX %lu\n", hr, Size1);
            hr = Buffer->Lock(50, 100, &Other1, &OtherSize, nullptr, nullptr, 0);
            printf("  lock_overlap = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Buffer->Unlock(Other1, OtherSize, nullptr, 0);
            hr = Buffer->Lock(100, 100, &Other1, &OtherSize, nullptr, nullptr, 0);
            printf("  lock_adjacent = %08lX\n", hr);
            if (SUCCEEDED(hr))
                printf("  unlock_adjacent = %08lX\n", Buffer->Unlock(Other1, OtherSize, nullptr, 0));
            hr = Buffer->Lock(8800, 100, &Other1, &OtherSize, &Part2, &Size2, 0);
            printf("  lock_wrapping_overlap = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Buffer->Unlock(Other1, OtherSize, Part2, Size2);
            printf("  unlock_wrongptr = %08lX\n", Buffer->Unlock(static_cast<BYTE*>(Part1) + 4, Size1, nullptr, 0));
            printf("  unlock_bigger = %08lX\n", Buffer->Unlock(Part1, Size1 + 100, nullptr, 0));
            printf("  unlock_again = %08lX\n", Buffer->Unlock(Part1, Size1, nullptr, 0));
            printf("  unlock_null = %08lX\n", Buffer->Unlock(nullptr, 0, nullptr, 0));
            printf("  unlock_null_size = %08lX\n", Buffer->Unlock(nullptr, 4, nullptr, 0));
            printf("  unlock_null2_size = %08lX\n", Buffer->Unlock(Part1, 4, nullptr, 4));
        }

        {
            static const DWORD PlayFlags[] = { 0x40, DSBPLAY_LOCHARDWARE, DSBPLAY_TERMINATEBY_TIME, 0x18 };

            for (UINT Index = 0; Index < ARRAYSIZE(PlayFlags); Index++)
                printf("  play_flags[%u] = %08lX\n", Index, Buffer->Play(0, 0, PlayFlags[Index]));
            printf("  play_reserved = %08lX\n", Buffer->Play(1, 0, 0));
            printf("  play_priority = %08lX\n", Buffer->Play(0, 1, 0));
        }

        if (Ctrl == 1)
        {
            IDirectSoundNotify* Notify = nullptr;

            hr = Buffer->QueryInterface(IID_IDirectSoundNotify, reinterpret_cast<void**>(&Notify));
            printf("  notify = %08lX\n", hr);
            if (SUCCEEDED(hr))
            {
                HANDLE Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                DSBPOSITIONNOTIFY Positions[4];

                printf("  notify_none = %08lX\n", Notify->SetNotificationPositions(0, nullptr));
                printf("  notify_null = %08lX\n", Notify->SetNotificationPositions(1, nullptr));
                Positions[0] = { 0, Event };
                Positions[1] = { 4410, Event };
                printf("  notify_two = %08lX\n", Notify->SetNotificationPositions(2, Positions));
                Positions[1] = { 0, Event };
                printf("  notify_dup = %08lX\n", Notify->SetNotificationPositions(2, Positions));
                Positions[0] = { 8820, Event };
                printf("  notify_past = %08lX\n", Notify->SetNotificationPositions(1, Positions));
                Positions[0] = { 100, nullptr };
                printf("  notify_nullevent = %08lX\n", Notify->SetNotificationPositions(1, Positions));
                Positions[0] = { DSBPN_OFFSETSTOP, Event };
                Positions[1] = { 100, Event };
                printf("  notify_stop_first = %08lX\n", Notify->SetNotificationPositions(2, Positions));
                Positions[0] = { 100, Event };
                Positions[1] = { DSBPN_OFFSETSTOP, Event };
                printf("  notify_stop_last = %08lX\n", Notify->SetNotificationPositions(2, Positions));
                Positions[0] = { DSBPN_OFFSETSTOP, Event };
                Positions[1] = { DSBPN_OFFSETSTOP, Event };
                printf("  notify_stop_twice = %08lX\n", Notify->SetNotificationPositions(2, Positions));
                Positions[0] = { 4411, Event };
                Positions[1] = { 3, Event };
                printf("  notify_unsorted = %08lX\n", Notify->SetNotificationPositions(2, Positions));
                printf("  notify addref %lu\n", Notify->AddRef());
                printf("  notify release %lu\n", Notify->Release());
                printf("  notify release %lu\n", Notify->Release());
                CloseHandle(Event);
            }
        }

        {
            IDirectSoundBuffer8* Buffer8 = nullptr;

            if (SUCCEEDED(Buffer->QueryInterface(IID_IDirectSoundBuffer8, reinterpret_cast<void**>(&Buffer8))))
            {
                DSEFFECTDESC Effect = {};
                DWORD Results[2] = { 0xCCCC, 0xCCCC };
                IUnknown* Object = reinterpret_cast<IUnknown*>(1);

                Effect.dwSize = sizeof(Effect);
                Effect.guidDSFXClass = GUID_DSFX_STANDARD_ECHO;
                printf("  setfx_none = %08lX\n", Buffer8->SetFX(0, nullptr, nullptr));
                printf("  setfx_mismatch = %08lX\n", Buffer8->SetFX(1, nullptr, nullptr));
                printf("  setfx_echo = %08lX %08lX\n", Buffer8->SetFX(1, &Effect, Results), Results[0]);
                printf("  setfx_noctl_results = %08lX\n", Buffer8->SetFX(0, nullptr, Results));
                printf("  acquire = %08lX\n", Buffer8->AcquireResources(0, 0, nullptr));
                printf("  acquire_bad = %08lX\n", Buffer8->AcquireResources(0x40, 0, nullptr));
                printf("  acquire_mismatch = %08lX\n", Buffer8->AcquireResources(0, 1, nullptr));
                printf("  getobject = %08lX\n", Buffer8->GetObjectInPath(GUID_DSFX_STANDARD_ECHO, 0, IID_IUnknown, reinterpret_cast<void**>(&Object)));
                printf("  getobject_null = %08lX\n", Buffer8->GetObjectInPath(GUID_DSFX_STANDARD_ECHO, 0, IID_IUnknown, nullptr));
                Buffer8->Release();
            }
        }

        hr = Sound->DuplicateSoundBuffer(Buffer, &Other);
        printf("  duplicate = %08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            DcDescribe(Other);
            DcCheckInterfaces("  duplicate", Other);
            printf("  duplicate release %lu\n", Other->Release());
        }
        printf("  duplicate_null = %08lX\n", Sound->DuplicateSoundBuffer(Buffer, nullptr));
        printf("  duplicate_nullsrc = %08lX\n", Sound->DuplicateSoundBuffer(nullptr, &Other));
        printf("  release %lu\n", Buffer->Release());
    }

    {
        WAVEFORMATEX Mono8;

        DcFormat(&Mono8, WAVE_FORMAT_PCM, 1, 8000, 8);
        if (SUCCEEDED(DcMakeBuffer(Sound, 0, 800, &Mono8, &Buffer)))
        {
            VOID* Part1;
            DWORD Size1;

            if (SUCCEEDED(Buffer->Lock(0, 0, &Part1, &Size1, nullptr, nullptr, DSBLOCK_ENTIREBUFFER)))
            {
                printf("silence8 %02X %02X\n", static_cast<BYTE*>(Part1)[0], static_cast<BYTE*>(Part1)[799]);
                Buffer->Unlock(Part1, Size1, nullptr, 0);
            }
            Buffer->Release();
        }
    }

    {
        IDirectSoundBuffer* Primary = nullptr;
        IDirectSoundBuffer* Primary2 = nullptr;
        DSBUFFERDESC Desc = {};

        Desc.dwSize = sizeof(Desc);
        Desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN;
        if (SUCCEEDED(Sound->CreateSoundBuffer(&Desc, &Primary, nullptr)))
        {
            WAVEFORMATEX Wanted;
            LONG Value;
            DWORD Play;
            DWORD Write;
            VOID* Part1;
            DWORD Size1;

            printf("primary\n");
            DcDescribe(Primary);
            DcCheckInterfaces("  primary", Primary);
            printf("  addref %lu\n", Primary->AddRef());
            printf("  release %lu\n", Primary->Release());
            hr = Sound->CreateSoundBuffer(&Desc, &Primary2, nullptr);
            printf("  again = %08lX same %d\n", hr, Primary2 == Primary);
            if (SUCCEEDED(hr))
                printf("  again release %lu\n", Primary2->Release());
            Desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
            hr = Sound->CreateSoundBuffer(&Desc, &Primary2, nullptr);
            printf("  again_other_flags = %08lX same %d\n", hr, Primary2 == Primary);
            if (SUCCEEDED(hr))
            {
                DSBCAPS Caps = { sizeof(Caps) };

                Primary2->GetCaps(&Caps);
                printf("    flags %08lX\n", Caps.dwFlags);
                printf("  again release %lu\n", Primary2->Release());
            }
            Value = 0x1234;
            printf("  setvolume = %08lX\n", Primary->SetVolume(-100));
            { HRESULT Rc = Primary->GetVolume(&Value); printf("  getvolume = %08lX %ld\n", Rc, Value); }
            printf("  setvolume_back = %08lX\n", Primary->SetVolume(0));
            printf("  setpan = %08lX\n", Primary->SetPan(0));
            printf("  setfrequency = %08lX\n", Primary->SetFrequency(22050));
            printf("  setposition = %08lX\n", Primary->SetCurrentPosition(0));
            printf("  lock = %08lX\n", Primary->Lock(0, 4, &Part1, &Size1, nullptr, nullptr, 0));
            printf("  unlock = %08lX\n", Primary->Unlock(nullptr, 0, nullptr, 0));
            printf("  play_0 = %08lX\n", Primary->Play(0, 0, 0));
            printf("  play = %08lX\n", Primary->Play(0, 0, DSBPLAY_LOOPING));
            {
                DWORD Status = 0;

                { HRESULT Rc = Primary->GetStatus(&Status); printf("  status = %08lX %08lX\n", Rc, Status); }
            }
            printf("  stop = %08lX\n", Primary->Stop());
            printf("  getposition = %08lX\n", Primary->GetCurrentPosition(&Play, &Write));
            DcFormat(&Wanted, WAVE_FORMAT_PCM, 2, 44100, 16);
            printf("  setformat = %08lX\n", Primary->SetFormat(&Wanted));
            DcDescribe(Primary);
            DcFormat(&Wanted, WAVE_FORMAT_PCM, 1, 11025, 8);
            printf("  setformat_mono8 = %08lX\n", Primary->SetFormat(&Wanted));
            DcDescribe(Primary);
            DcFormat(&Wanted, WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 32);
            printf("  setformat_float = %08lX\n", Primary->SetFormat(&Wanted));
            DcDescribe(Primary);
            DcFormat(&Wanted, WAVE_FORMAT_PCM, 2, 99, 16);
            printf("  setformat_bad = %08lX\n", Primary->SetFormat(&Wanted));
            printf("  setformat_null = %08lX\n", Primary->SetFormat(nullptr));
            Sound->SetCooperativeLevel(DcWindow, DSSCL_NORMAL);
            DcFormat(&Wanted, WAVE_FORMAT_PCM, 2, 44100, 16);
            printf("  setformat_normal = %08lX\n", Primary->SetFormat(&Wanted));
            Sound->SetCooperativeLevel(DcWindow, DSSCL_WRITEPRIMARY);
            DcDescribe(Primary);
            hr = Primary->Lock(0, 0, &Part1, &Size1, nullptr, nullptr, DSBLOCK_ENTIREBUFFER);
            printf("  writeprimary lock = %08lX %lu\n", hr, Size1);
            if (SUCCEEDED(hr))
                printf("  writeprimary unlock = %08lX\n", Primary->Unlock(Part1, Size1, nullptr, 0));
            hr = DcMakeBuffer(Sound, 0, 8820, &Format, &Buffer);
            printf("  writeprimary secondary = %08lX\n", hr);
            if (SUCCEEDED(hr))
            {
                printf("    play = %08lX\n", Buffer->Play(0, 0, 0));
                Buffer->Release();
            }
            Sound->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY);
            printf("  release %lu\n", Primary->Release());
        }
    }
    printf("release %lu\n", Sound->Release());
}

/* Plays silent buffers and reports cursor movement that does not depend on timing */
static
VOID
DcCheckPlayback(VOID)
{
    IDirectSound8* Sound;
    IDirectSoundBuffer* Buffer;
    WAVEFORMATEX Format;
    HRESULT hr;

    if (FAILED(DcCreate8(&Sound)))
        return;
    Sound->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY);

    for (UINT Rate = 0; Rate < 2; Rate++)
    {
        DWORD Bytes;

        DcFormat(&Format, WAVE_FORMAT_PCM, 2, Rate == 0 ? 48000 : 22050, 16);
        Bytes = Format.nAvgBytesPerSec;
        hr = DcMakeBuffer(Sound, DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY, Bytes, &Format, &Buffer);
        if (FAILED(hr))
            continue;

        printf("loop[%u]\n", Rate);
        Buffer->SetVolume(DSBVOLUME_MIN);
        {
            DWORD Play = 0;
            DWORD Write = 0;
            DWORD Status = 0;
            DWORD LastPlay = 0;
            BOOL Monotonic = TRUE;
            DWORD MaxLead = 0;
            DWORD MinLead = 0xFFFFFFFF;

            Buffer->SetCurrentPosition(Bytes / 4);
            printf("  play = %08lX\n", Buffer->Play(0, 0, DSBPLAY_LOOPING));
            Buffer->GetStatus(&Status);
            printf("  status %08lX\n", Status);
            Buffer->GetCurrentPosition(&Play, &Write);
            printf("  start play %lu write_ahead %d\n", Play, Write >= Play);
            LastPlay = Play;
            for (UINT Step = 0; Step < 30; Step++)
            {
                DWORD Lead;

                Sleep(10);
                Buffer->GetCurrentPosition(&Play, &Write);
                if (Play < LastPlay)
                    Monotonic = FALSE;
                LastPlay = Play;
                Lead = (Write + Bytes - Play) % Bytes;
                if (Step >= 10)
                {
                    if (Lead > MaxLead)
                        MaxLead = Lead;
                    if (Lead < MinLead)
                        MinLead = Lead;
                }
            }
            printf("  moved %d monotonic %d\n", Play != Bytes / 4, Monotonic);
            printf("  aligned %d %d\n", Play % Format.nBlockAlign == 0, Write % Format.nBlockAlign == 0);
            printf("  lead_ms %lu %lu\n", MinLead * 1000 / Format.nAvgBytesPerSec, MaxLead * 1000 / Format.nAvgBytesPerSec);
            {
                VOID* Part1;
                VOID* Part2;
                DWORD Size1;
                DWORD Size2;

                hr = Buffer->Lock(0, 100, &Part1, &Size1, &Part2, &Size2, DSBLOCK_FROMWRITECURSOR);
                printf("  lock_write = %08lX %lu\n", hr, Size1 + Size2);
                if (SUCCEEDED(hr))
                    Buffer->Unlock(Part1, Size1, Part2, Size2);
            }
            printf("  play_again = %08lX\n", Buffer->Play(0, 0, 0));
            Buffer->GetStatus(&Status);
            printf("  status_nonloop %08lX\n", Status);
            printf("  stop = %08lX\n", Buffer->Stop());
            Buffer->GetCurrentPosition(&Play, &Write);
            printf("  stopped equal %d\n", Play == Write);
            Buffer->GetStatus(&Status);
            printf("  status %08lX\n", Status);
            printf("  stop_again = %08lX\n", Buffer->Stop());
            {
                DWORD Play2;
                DWORD Write2;

                Sleep(50);
                Buffer->GetCurrentPosition(&Play2, &Write2);
                printf("  stopped still %d\n", Play2 == Play && Write2 == Write);
            }
            Buffer->SetCurrentPosition(0);
            Buffer->GetCurrentPosition(&Play, &Write);
            printf("  reset %lu %lu\n", Play, Write);
        }
        Buffer->Release();
    }

    /* A short one shot runs out, stops by itself and fires its notifications in order */
    DcFormat(&Format, WAVE_FORMAT_PCM, 1, 48000, 16);
    hr = DcMakeBuffer(Sound, DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPOSITIONNOTIFY, 9600, &Format, &Buffer);
    if (SUCCEEDED(hr))
    {
        IDirectSoundNotify* Notify;
        HANDLE Events[4];
        DSBPOSITIONNOTIFY Positions[4];
        DWORD Status = 0;
        DWORD Play;
        DWORD Write;

        for (UINT Index = 0; Index < 4; Index++)
            Events[Index] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Positions[0] = { 0, Events[0] };
        Positions[1] = { 4800, Events[1] };
        Positions[2] = { 9598, Events[2] };
        Positions[3] = { DSBPN_OFFSETSTOP, Events[3] };
        Buffer->SetVolume(DSBVOLUME_MIN);
        Buffer->QueryInterface(IID_IDirectSoundNotify, reinterpret_cast<void**>(&Notify));
        printf("oneshot notify = %08lX\n", Notify->SetNotificationPositions(4, Positions));
        printf("oneshot play = %08lX\n", Buffer->Play(0, 0, 0));
        printf("  notify_playing = %08lX\n", Notify->SetNotificationPositions(4, Positions));
        for (UINT Index = 0; Index < 4; Index++)
            printf("  event[%u] %s\n", Index, WaitForSingleObject(Events[Index], 2000) == WAIT_OBJECT_0 ? "fired" : "timeout");
        Sleep(100);
        Buffer->GetStatus(&Status);
        printf("  status %08lX\n", Status);
        Buffer->GetCurrentPosition(&Play, &Write);
        printf("  position %lu %lu\n", Play, Write);
        printf("  replay = %08lX\n", Buffer->Play(0, 0, 0));
        printf("  stop = %08lX\n", Buffer->Stop());
        printf("  stop_event %s\n", WaitForSingleObject(Events[3], 500) == WAIT_OBJECT_0 ? "fired" : "timeout");
        Notify->Release();
        for (UINT Index = 0; Index < 4; Index++)
            CloseHandle(Events[Index]);
        Buffer->Release();
    }
    printf("release %lu\n", Sound->Release());
}

static
VOID
DcCheck3d(VOID)
{
    IDirectSound8* Sound;
    IDirectSoundBuffer* Buffer;
    IDirectSoundBuffer* Primary;
    WAVEFORMATEX Format;
    DSBUFFERDESC Desc = {};
    HRESULT hr;

    if (FAILED(DcCreate8(&Sound)))
        return;
    Sound->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY);
    DcFormat(&Format, WAVE_FORMAT_PCM, 1, 22050, 16);

    Desc.dwSize = sizeof(Desc);
    Desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    if (SUCCEEDED(Sound->CreateSoundBuffer(&Desc, &Primary, nullptr)))
    {
        IDirectSound3DListener* Listener = nullptr;

        hr = Primary->QueryInterface(IID_IDirectSound3DListener, reinterpret_cast<void**>(&Listener));
        printf("listener = %08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            DS3DLISTENER All;
            D3DVECTOR Vector;
            D3DVALUE Value;

            memset(&All, 0xCC, sizeof(All));
            All.dwSize = sizeof(All);
            printf("  getall = %08lX\n", Listener->GetAllParameters(&All));
            DcBytes("    all", &All, sizeof(All));
            All.dwSize = 0;
            printf("  getall_size0 = %08lX\n", Listener->GetAllParameters(&All));
            printf("  getall_null = %08lX\n", Listener->GetAllParameters(nullptr));
            printf("  setdistance[0] = %08lX\n", Listener->SetDistanceFactor(0.0f, DS3D_IMMEDIATE));
            printf("  setdistance[-1] = %08lX\n", Listener->SetDistanceFactor(-1.0f, DS3D_IMMEDIATE));
            printf("  setdistance[2] = %08lX\n", Listener->SetDistanceFactor(2.0f, DS3D_IMMEDIATE));
            printf("  setdistance_flags = %08lX\n", Listener->SetDistanceFactor(2.0f, 2));
            Listener->GetDistanceFactor(&Value);
            printf("  getdistance %g\n", Value);
            printf("  setdoppler[11] = %08lX\n", Listener->SetDopplerFactor(11.0f, DS3D_IMMEDIATE));
            printf("  setdoppler[10] = %08lX\n", Listener->SetDopplerFactor(10.0f, DS3D_IMMEDIATE));
            printf("  setrolloff[11] = %08lX\n", Listener->SetRolloffFactor(11.0f, DS3D_IMMEDIATE));
            printf("  setrolloff[-1] = %08lX\n", Listener->SetRolloffFactor(-1.0f, DS3D_IMMEDIATE));
            printf("  setrolloff[3] = %08lX\n", Listener->SetRolloffFactor(3.0f, DS3D_DEFERRED));
            Listener->GetRolloffFactor(&Value);
            printf("  getrolloff_deferred %g\n", Value);
            printf("  commit = %08lX\n", Listener->CommitDeferredSettings());
            Listener->GetRolloffFactor(&Value);
            printf("  getrolloff_committed %g\n", Value);
            printf("  setposition = %08lX\n", Listener->SetPosition(1.0f, 2.0f, 3.0f, DS3D_IMMEDIATE));
            Listener->GetPosition(&Vector);
            printf("  getposition %g %g %g\n", Vector.x, Vector.y, Vector.z);
            printf("  setorientation_zero = %08lX\n", Listener->SetOrientation(0, 0, 0, 0, 1, 0, DS3D_IMMEDIATE));
            printf("  setorientation_parallel = %08lX\n", Listener->SetOrientation(0, 1, 0, 0, 2, 0, DS3D_IMMEDIATE));
            printf("  setorientation = %08lX\n", Listener->SetOrientation(1, 0, 0, 0, 1, 0, DS3D_IMMEDIATE));
            {
                D3DVECTOR Front;
                D3DVECTOR Top;

                Listener->GetOrientation(&Front, &Top);
                printf("  getorientation %g %g %g %g %g %g\n", Front.x, Front.y, Front.z, Top.x, Top.y, Top.z);
            }
            printf("  getposition_null = %08lX\n", Listener->GetPosition(nullptr));
            memset(&All, 0, sizeof(All));
            All.dwSize = sizeof(All);
            printf("  setall_zero = %08lX\n", Listener->SetAllParameters(&All, DS3D_IMMEDIATE));
            Listener->Release();
        }
        Primary->Release();
    }

    Desc.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY;
    Desc.dwBufferBytes = 22050;
    Desc.lpwfxFormat = &Format;
    hr = Sound->CreateSoundBuffer(&Desc, &Buffer, nullptr);
    printf("buffer3d = %08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        IDirectSound3DBuffer* Buffer3d = nullptr;

        DcCheckInterfaces("  buffer3d", Buffer);
        hr = Buffer->QueryInterface(IID_IDirectSound3DBuffer, reinterpret_cast<void**>(&Buffer3d));
        if (SUCCEEDED(hr))
        {
            DS3DBUFFER All;
            DWORD Inside;
            DWORD Outside;
            DWORD Mode;
            D3DVALUE Value;

            memset(&All, 0xCC, sizeof(All));
            All.dwSize = sizeof(All);
            printf("  getall = %08lX\n", Buffer3d->GetAllParameters(&All));
            DcBytes("    all", &All, sizeof(All));
            printf("  setcone[10,20] = %08lX\n", Buffer3d->SetConeAngles(10, 20, DS3D_IMMEDIATE));
            printf("  setcone[20,10] = %08lX\n", Buffer3d->SetConeAngles(20, 10, DS3D_IMMEDIATE));
            printf("  setcone[0,361] = %08lX\n", Buffer3d->SetConeAngles(0, 361, DS3D_IMMEDIATE));
            Buffer3d->GetConeAngles(&Inside, &Outside);
            printf("  getcone %lu %lu\n", Inside, Outside);
            printf("  setconevolume[1] = %08lX\n", Buffer3d->SetConeOutsideVolume(1, DS3D_IMMEDIATE));
            printf("  setconevolume[-10001] = %08lX\n", Buffer3d->SetConeOutsideVolume(-10001, DS3D_IMMEDIATE));
            printf("  setmin[0] = %08lX\n", Buffer3d->SetMinDistance(0.0f, DS3D_IMMEDIATE));
            printf("  setmin[-1] = %08lX\n", Buffer3d->SetMinDistance(-1.0f, DS3D_IMMEDIATE));
            printf("  setmax[0] = %08lX\n", Buffer3d->SetMaxDistance(0.0f, DS3D_IMMEDIATE));
            printf("  setmax[5] = %08lX\n", Buffer3d->SetMaxDistance(5.0f, DS3D_IMMEDIATE));
            Buffer3d->GetMaxDistance(&Value);
            printf("  getmax %g\n", Value);
            for (DWORD Index = 0; Index < 4; Index++)
                printf("  setmode[%lu] = %08lX\n", Index, Buffer3d->SetMode(Index, DS3D_IMMEDIATE));
            Buffer3d->GetMode(&Mode);
            printf("  getmode %lu\n", Mode);
            printf("  setposition = %08lX\n", Buffer3d->SetPosition(1, 2, 3, DS3D_DEFERRED));
            {
                D3DVECTOR Vector;

                Buffer3d->GetPosition(&Vector);
                printf("  getposition %g %g %g\n", Vector.x, Vector.y, Vector.z);
            }
            {
                DWORD Frequency = 0;

                printf("  setfrequency = %08lX\n", Buffer->SetFrequency(44100));
                Buffer->GetFrequency(&Frequency);
                printf("  getfrequency %lu\n", Frequency);
            }
            memset(&All, 0, sizeof(All));
            All.dwSize = sizeof(All);
            printf("  setall_zero = %08lX\n", Buffer3d->SetAllParameters(&All, DS3D_IMMEDIATE));
            printf("  addref %lu\n", Buffer3d->AddRef());
            printf("  release %lu\n", Buffer3d->Release());
            Buffer3d->Release();
        }
        Buffer->Release();
    }
    printf("release %lu\n", Sound->Release());
}

static
VOID
DcCheckCapture(VOID)
{
    PDC_CAPTURE_CREATE Create = reinterpret_cast<PDC_CAPTURE_CREATE>(GetProcAddress(DcModule, "DirectSoundCaptureCreate"));
    PDC_CAPTURE_CREATE Create8 = reinterpret_cast<PDC_CAPTURE_CREATE>(GetProcAddress(DcModule, "DirectSoundCaptureCreate8"));
    IDirectSoundCapture* Capture = nullptr;
    HRESULT hr;

    printf("capture_null = %08lX\n", Create(nullptr, nullptr, nullptr));
    printf("capture_outer = %08lX\n", Create(nullptr, &Capture, reinterpret_cast<IUnknown*>(1)));
    hr = Create(&DSDEVID_DefaultPlayback, &Capture, nullptr);
    printf("capture_render = %08lX\n", hr);
    if (SUCCEEDED(hr))
        Capture->Release();
    for (UINT Kind = 0; Kind < 2; Kind++)
    {
        hr = (Kind == 0 ? Create : Create8)(nullptr, &Capture, nullptr);
        printf("capture[%u] = %08lX\n", Kind, hr);
        if (FAILED(hr))
            continue;

        DcCheckInterfaces("  capture", Capture);
        {
            DSCCAPS Caps;

            memset(&Caps, 0xCC, sizeof(Caps));
            Caps.dwSize = sizeof(Caps);
            printf("  getcaps = %08lX\n", Capture->GetCaps(&Caps));
            DcBytes("    caps", &Caps, sizeof(Caps));
            Caps.dwSize = 0;
            printf("  getcaps_size0 = %08lX\n", Capture->GetCaps(&Caps));
        }
        {
            WAVEFORMATEX Format;
            DSCBUFFERDESC Desc = {};
            IDirectSoundCaptureBuffer* Buffer;

            DcFormat(&Format, WAVE_FORMAT_PCM, 1, 44100, 16);
            Desc.dwSize = sizeof(Desc);
            Desc.dwBufferBytes = 44100;
            Desc.lpwfxFormat = &Format;
            printf("  createbuffer_null = %08lX\n", Capture->CreateCaptureBuffer(nullptr, &Buffer, nullptr));
            printf("  createbuffer_nullout = %08lX\n", Capture->CreateCaptureBuffer(&Desc, nullptr, nullptr));
            Desc.dwSize = sizeof(DSCBUFFERDESC1);
            hr = Capture->CreateCaptureBuffer(&Desc, &Buffer, nullptr);
            printf("  createbuffer_desc1 = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Buffer->Release();
            Desc.dwSize = 12;
            printf("  createbuffer_size12 = %08lX\n", Capture->CreateCaptureBuffer(&Desc, &Buffer, nullptr));
            Desc.dwSize = sizeof(Desc);
            Desc.dwBufferBytes = 0;
            printf("  createbuffer_bytes0 = %08lX\n", Capture->CreateCaptureBuffer(&Desc, &Buffer, nullptr));
            Desc.dwBufferBytes = 44101;
            hr = Capture->CreateCaptureBuffer(&Desc, &Buffer, nullptr);
            printf("  createbuffer_odd = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Buffer->Release();
            Desc.dwBufferBytes = 44100;
            Desc.dwFlags = 0x10;
            printf("  createbuffer_badflag = %08lX\n", Capture->CreateCaptureBuffer(&Desc, &Buffer, nullptr));
            Desc.dwFlags = DSCBCAPS_WAVEMAPPED;
            hr = Capture->CreateCaptureBuffer(&Desc, &Buffer, nullptr);
            printf("  createbuffer_wavemapped = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Buffer->Release();
            Desc.dwFlags = 0;
            Desc.lpwfxFormat = nullptr;
            printf("  createbuffer_nullformat = %08lX\n", Capture->CreateCaptureBuffer(&Desc, &Buffer, nullptr));
            Desc.lpwfxFormat = &Format;
            hr = Capture->CreateCaptureBuffer(&Desc, &Buffer, nullptr);
            printf("  createbuffer = %08lX\n", hr);
            if (SUCCEEDED(hr))
            {
                IDirectSoundCaptureBuffer* Second = nullptr;
                DSCBCAPS Caps;
                BYTE Bytes[64];
                DWORD Size = 0;
                DWORD Status = 0xCCCC;
                DWORD Capture1 = 0xCCCC;
                DWORD Read = 0xCCCC;

                DcCheckInterfaces("  buffer", Buffer);
                hr = Capture->CreateCaptureBuffer(&Desc, &Second, nullptr);
                printf("  createbuffer_second = %08lX\n", hr);
                if (SUCCEEDED(hr))
                    Second->Release();
                memset(&Caps, 0xCC, sizeof(Caps));
                Caps.dwSize = sizeof(Caps);
                printf("  buffer getcaps = %08lX\n", Buffer->GetCaps(&Caps));
                DcBytes("    caps", &Caps, sizeof(Caps));
                printf("  buffer getformat_size = %08lX\n", Buffer->GetFormat(nullptr, 0, &Size));
                printf("    size %lu\n", Size);
                memset(Bytes, 0xCC, sizeof(Bytes));
                printf("  buffer getformat = %08lX\n", Buffer->GetFormat(reinterpret_cast<WAVEFORMATEX*>(Bytes), sizeof(Bytes), &Size));
                DcBytes("    format", Bytes, 24);
                { HRESULT Rc = Buffer->GetStatus(&Status); printf("  buffer getstatus = %08lX %08lX\n", Rc, Status); }
                { HRESULT Rc = Buffer->GetCurrentPosition(&Capture1, &Read); printf("  buffer getposition = %08lX %lu %lu\n", Rc, Capture1, Read); }
                printf("  buffer initialize = %08lX\n", Buffer->Initialize(Capture, &Desc));
                {
                    VOID* Part1;
                    DWORD Size1;
                    VOID* Part2;
                    DWORD Size2;

                    hr = Buffer->Lock(44000, 200, &Part1, &Size1, &Part2, &Size2, 0);
                    printf("  buffer lock = %08lX %lu %lu\n", hr, Size1, Size2);
                    if (SUCCEEDED(hr))
                        printf("  buffer unlock = %08lX\n", Buffer->Unlock(Part1, Size1, Part2, Size2));
                    printf("  buffer lock_big = %08lX\n", Buffer->Lock(0, 44101, &Part1, &Size1, nullptr, nullptr, 0));
                    printf("  buffer lock_badflag = %08lX\n", Buffer->Lock(0, 4, &Part1, &Size1, nullptr, nullptr, 2));
                }
                printf("  buffer start_bad = %08lX\n", Buffer->Start(2));
                printf("  buffer start = %08lX\n", Buffer->Start(DSCBSTART_LOOPING));
                Sleep(200);
                Buffer->GetStatus(&Status);
                printf("  buffer status %08lX\n", Status);
                Buffer->GetCurrentPosition(&Capture1, &Read);
                printf("  buffer moved %d aligned %d %d\n", Read != 0, Capture1 % 2 == 0, Read % 2 == 0);
                printf("  buffer stop = %08lX\n", Buffer->Stop());
                Buffer->GetStatus(&Status);
                printf("  buffer status %08lX\n", Status);
                printf("  buffer release %lu\n", Buffer->Release());
            }
        }
        printf("  release %lu\n", Capture->Release());
    }
}

static
VOID
DcCheckFullDuplex(VOID)
{
    PDC_FULL_DUPLEX Create = reinterpret_cast<PDC_FULL_DUPLEX>(GetProcAddress(DcModule, "DirectSoundFullDuplexCreate"));
    WAVEFORMATEX Format;
    DSCBUFFERDESC CaptureDesc = {};
    DSBUFFERDESC RenderDesc = {};
    IDirectSoundFullDuplex* Duplex = nullptr;
    IDirectSoundCaptureBuffer8* CaptureBuffer = nullptr;
    IDirectSoundBuffer8* RenderBuffer = nullptr;
    HRESULT hr;

    DcFormat(&Format, WAVE_FORMAT_PCM, 1, 44100, 16);
    CaptureDesc.dwSize = sizeof(CaptureDesc);
    CaptureDesc.dwBufferBytes = 44100;
    CaptureDesc.lpwfxFormat = &Format;
    RenderDesc.dwSize = sizeof(RenderDesc);
    RenderDesc.dwBufferBytes = 44100;
    RenderDesc.lpwfxFormat = &Format;
    printf("duplex_nulldesc = %08lX\n", Create(nullptr, nullptr, nullptr, &RenderDesc, DcWindow, DSSCL_PRIORITY, &Duplex, &CaptureBuffer, &RenderBuffer, nullptr));
    printf("duplex_nullwnd = %08lX\n", Create(nullptr, nullptr, &CaptureDesc, &RenderDesc, nullptr, DSSCL_PRIORITY, &Duplex, &CaptureBuffer, &RenderBuffer, nullptr));
    printf("duplex_badlevel = %08lX\n", Create(nullptr, nullptr, &CaptureDesc, &RenderDesc, DcWindow, 0, &Duplex, &CaptureBuffer, &RenderBuffer, nullptr));
    printf("duplex_nullout = %08lX\n", Create(nullptr, nullptr, &CaptureDesc, &RenderDesc, DcWindow, DSSCL_PRIORITY, nullptr, &CaptureBuffer, &RenderBuffer, nullptr));
    printf("duplex_outer = %08lX\n", Create(nullptr, nullptr, &CaptureDesc, &RenderDesc, DcWindow, DSSCL_PRIORITY, &Duplex, &CaptureBuffer, &RenderBuffer, reinterpret_cast<IUnknown*>(1)));
    RenderDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    printf("duplex_primary = %08lX\n", Create(nullptr, nullptr, &CaptureDesc, &RenderDesc, DcWindow, DSSCL_PRIORITY, &Duplex, &CaptureBuffer, &RenderBuffer, nullptr));
    RenderDesc.dwFlags = 0;
    hr = Create(nullptr, nullptr, &CaptureDesc, &RenderDesc, DcWindow, DSSCL_PRIORITY, &Duplex, &CaptureBuffer, &RenderBuffer, nullptr);
    printf("duplex = %08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        DcCheckInterfaces("  duplex", Duplex);
        printf("  initialize = %08lX\n", Duplex->Initialize(nullptr, nullptr, &CaptureDesc, &RenderDesc, DcWindow, DSSCL_PRIORITY, &CaptureBuffer, &RenderBuffer));
        printf("  render release %lu\n", RenderBuffer->Release());
        printf("  capture release %lu\n", CaptureBuffer->Release());
        printf("  release %lu\n", Duplex->Release());
    }
}

/* Effects on a buffer made for them: loading, result codes, finding them again and their parameters */
static
VOID
DcCheckEffects(VOID)
{
    static const GUID* Classes[] =
    {
        &GUID_DSFX_STANDARD_ECHO, &GUID_DSFX_STANDARD_CHORUS, &GUID_DSFX_STANDARD_DISTORTION, &GUID_DSFX_STANDARD_GARGLE,
        &GUID_DSFX_STANDARD_COMPRESSOR, &GUID_DSFX_STANDARD_FLANGER, &GUID_DSFX_STANDARD_PARAMEQ,
        &GUID_DSFX_WAVES_REVERB, &GUID_DSFX_STANDARD_I3DL2REVERB, &IID_IUnknown,
    };
    IDirectSound8* Sound;
    IDirectSoundBuffer* Buffer;
    IDirectSoundBuffer8* Buffer8 = nullptr;
    WAVEFORMATEX Format;
    HRESULT hr;

    if (FAILED(DcCreate8(&Sound)))
        return;
    Sound->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY);
    DcFormat(&Format, WAVE_FORMAT_PCM, 2, 44100, 16);
    hr = DcMakeBuffer(Sound, DSBCAPS_CTRLFX | DSBCAPS_CTRLVOLUME, Format.nAvgBytesPerSec / 2, &Format, &Buffer);
    printf("fx create = %08lX\n", hr);
    if (SUCCEEDED(hr) && SUCCEEDED(Buffer->QueryInterface(IID_IDirectSoundBuffer8, reinterpret_cast<void**>(&Buffer8))))
    {
        for (UINT Index = 0; Index < ARRAYSIZE(Classes); Index++)
        {
            DSEFFECTDESC Effect = {};
            DWORD Result = 0xCCCC;

            Effect.dwSize = sizeof(Effect);
            Effect.guidDSFXClass = *Classes[Index];
            hr = Buffer8->SetFX(1, &Effect, &Result);
            printf("fx set[%u] = %08lX result %lu\n", Index, hr, Result);
        }
        {
            DSEFFECTDESC Effects[2] = {};
            DWORD Results[2] = { 0xCCCC, 0xCCCC };
            IDirectSoundFXEcho* Echo = nullptr;
            IUnknown* Object = nullptr;

            Effects[0].dwSize = sizeof(Effects[0]);
            Effects[0].guidDSFXClass = GUID_DSFX_STANDARD_ECHO;
            Effects[1].dwSize = sizeof(Effects[1]);
            Effects[1].guidDSFXClass = GUID_DSFX_STANDARD_GARGLE;
            hr = Buffer8->SetFX(2, Effects, Results);
            printf("fx set_two = %08lX results %lu %lu\n", hr, Results[0], Results[1]);
            Effects[0].dwFlags = 4;
            printf("fx set_badflags = %08lX\n", Buffer8->SetFX(1, Effects, Results));
            Effects[0].dwFlags = 0;
            Effects[0].dwSize = 12;
            printf("fx set_badsize = %08lX\n", Buffer8->SetFX(1, Effects, Results));
            Effects[0].dwSize = sizeof(Effects[0]);
            hr = Buffer8->GetObjectInPath(GUID_DSFX_STANDARD_ECHO, 0, IID_IDirectSoundFXEcho, reinterpret_cast<void**>(&Echo));
            printf("fx getobject = %08lX\n", hr);
            if (SUCCEEDED(hr))
            {
                DSFXEcho Params = {};

                printf("fx echo get = %08lX\n", Echo->GetAllParameters(&Params));
                printf("  echo %g %g %g %g %ld\n", Params.fWetDryMix, Params.fFeedback, Params.fLeftDelay, Params.fRightDelay, Params.lPanDelay);
                Echo->Release();
            }
            printf("fx getobject_index1 = %08lX\n", Buffer8->GetObjectInPath(GUID_DSFX_STANDARD_ECHO, 1, IID_IUnknown, reinterpret_cast<void**>(&Object)));
            hr = Buffer8->GetObjectInPath(GUID_All_Objects, 1, IID_IUnknown, reinterpret_cast<void**>(&Object));
            printf("fx getobject_all1 = %08lX\n", hr);
            if (SUCCEEDED(hr))
                Object->Release();
            printf("fx getobject_badiid = %08lX\n", Buffer8->GetObjectInPath(GUID_DSFX_STANDARD_ECHO, 0, IID_IDirectSound, reinterpret_cast<void**>(&Object)));
            Buffer8->SetVolume(DSBVOLUME_MIN);
            Buffer8->Play(0, 0, DSBPLAY_LOOPING);
            printf("fx set_playing = %08lX\n", Buffer8->SetFX(0, nullptr, nullptr));
            Buffer8->Stop();
            {
                VOID* Part1;
                DWORD Size1;

                Buffer8->Lock(0, 100, &Part1, &Size1, nullptr, nullptr, 0);
                printf("fx set_locked = %08lX\n", Buffer8->SetFX(0, nullptr, nullptr));
                Buffer8->Unlock(Part1, Size1, nullptr, 0);
            }
            printf("fx clear = %08lX\n", Buffer8->SetFX(0, nullptr, nullptr));
            printf("fx getobject_cleared = %08lX\n", Buffer8->GetObjectInPath(GUID_DSFX_STANDARD_ECHO, 0, IID_IUnknown, reinterpret_cast<void**>(&Object)));
            printf("fx duplicate = %08lX\n", Sound->DuplicateSoundBuffer(Buffer, reinterpret_cast<IDirectSoundBuffer**>(&Object)));
        }
        Buffer8->Release();
    }
    if (SUCCEEDED(hr) || Buffer != nullptr)
        Buffer->Release();
    printf("release %lu\n", Sound->Release());
}

/* EAX, voice manager and ZoomFX sets, which games probe buffers for */
static const GUID DcEaxListenerSet = { 0x4A4E6FC1, 0xC341, 0x11D1, { 0xB7, 0x3A, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID DcEaxBufferSet = { 0x4A4E6FC0, 0xC341, 0x11D1, { 0xB7, 0x3A, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID DcEax2ListenerSet = { 0x0306A6A8, 0xB224, 0x11D2, { 0x99, 0xE5, 0x00, 0x00, 0xE8, 0xD8, 0xC7, 0x22 } };
static const GUID DcEax2BufferSet = { 0x0306A6A7, 0xB224, 0x11D2, { 0x99, 0xE5, 0x00, 0x00, 0xE8, 0xD8, 0xC7, 0x22 } };
static const GUID DcVoiceManagerSet = { 0x62A69BAE, 0xDF9D, 0x11D1, { 0x99, 0xA6, 0x00, 0xC0, 0x4F, 0xC9, 0x9D, 0x46 } };
static const GUID DcZoomFxSet = { 0xCD5368E0, 0x3450, 0x11D3, { 0x8B, 0x6E, 0x00, 0x10, 0xA4, 0xF1, 0xF1, 0x2B } };

/* Asks one property set interface about each set and id, reading only, then tries writes where none are supported */
static
VOID
DcProbeProperties(
    _In_z_ PCSTR Name,
    _In_ IKsPropertySet* Properties,
    _In_ BOOLEAN Writes)
{
    static const struct
    {
        const GUID* Set;
        PCSTR Name;
    } Sets[] =
    {
        { &DSPROPSETID_DirectSoundDevice, "device" },
        { &DcEaxListenerSet, "eax" },
        { &DcEaxBufferSet, "eaxbuffer" },
        { &DcEax2ListenerSet, "eax2" },
        { &DcEax2BufferSet, "eax2buffer" },
        { &DcVoiceManagerSet, "voicemanager" },
        { &DcZoomFxSet, "zoomfx" },
        { &KSPROPSETID_Audio, "ksaudio" },
        { &GUID_NULL, "null" },
    };
    BYTE Data[64];
    ULONG Support;
    ULONG Returned;
    HRESULT hr;

    for (UINT Index = 0; Index < ARRAYSIZE(Sets); Index++)
    {
        for (ULONG Id = 0; Id < 4; Id++)
        {
            Support = 0xCCCC;
            hr = Properties->QuerySupport(*Sets[Index].Set, Id, &Support);
            printf("%s %s[%lu] querysupport = %08lX %lX", Name, Sets[Index].Name, Id, hr, Support);
            ZeroMemory(Data, sizeof(Data));
            Returned = 0xCCCC;
            hr = Properties->Get(*Sets[Index].Set, Id, nullptr, 0, Data, sizeof(Data), &Returned);
            printf(" get = %08lX %lu", hr, Returned);
            Returned = 0xCCCC;
            hr = Properties->Get(*Sets[Index].Set, Id, nullptr, 0, Data, 0, &Returned);
            printf(" get_empty = %08lX %lu\n", hr, Returned);
            if (Writes)
            {
                ZeroMemory(Data, sizeof(Data));
                printf("%s %s[%lu] set = %08lX set_null = %08lX\n", Name, Sets[Index].Name, Id,
                       Properties->Set(*Sets[Index].Set, Id, nullptr, 0, Data, sizeof(Data)),
                       Properties->Set(*Sets[Index].Set, Id, nullptr, 0, nullptr, 0));
            }
        }
    }

    Support = 0xCCCC;
    hr = Properties->QuerySupport(*static_cast<const GUID*>(nullptr), 0, &Support);
    printf("%s querysupport_nullset = %08lX %lX\n", Name, hr, Support);
    printf("%s querysupport_nullsupport = %08lX\n", Name, Properties->QuerySupport(DSPROPSETID_DirectSoundDevice, 1, nullptr));
    Returned = 0xCCCC;
    hr = Properties->Get(*static_cast<const GUID*>(nullptr), 0, nullptr, 0, Data, sizeof(Data), &Returned);
    printf("%s get_nullset = %08lX %lu\n", Name, hr, Returned);
    Returned = 0xCCCC;
    hr = Properties->Get(DSPROPSETID_DirectSoundDevice, 1, nullptr, 0, nullptr, sizeof(Data), &Returned);
    printf("%s get_nulldata = %08lX %lu\n", Name, hr, Returned);
    hr = Properties->Get(DcEaxBufferSet, 0, Data, sizeof(Data), Data, sizeof(Data), nullptr);
    printf("%s get_noreturned = %08lX\n", Name, hr);
    if (Writes)
        printf("%s set_nullset = %08lX\n", Name, Properties->Set(*static_cast<const GUID*>(nullptr), 0, nullptr, 0, Data, sizeof(Data)));
}

/* The property sets of the DirectSoundPrivate object, private ones named by their GUID */
static const GUID DcPrivateSets[] =
{
    { 0x84624F80, 0x25EC, 0x11D1, { 0xA4, 0xD8, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } },
    { 0x84624F82, 0x25EC, 0x11D1, { 0xA4, 0xD8, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } },
    { 0x1AEAA606, 0x35F0, 0x11D1, { 0xB1, 0x61, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } },
    { 0xF2957840, 0x260C, 0x11D1, { 0xA4, 0xD8, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } },
    { 0x1BE55C3E, 0x36AB, 0x11D1, { 0xB1, 0x62, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } },
    { 0x50393DEA, 0x51AD, 0x11D2, { 0x91, 0xB2, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } },
    { 0x4BE334F0, 0x1C8E, 0x4D91, { 0x8C, 0x17, 0x58, 0x89, 0xD6, 0xF0, 0xB1, 0x54 } },
    { 0x84624F81, 0x25EC, 0x11D1, { 0xA4, 0xD8, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA } },
};

static
VOID
DcShowDescriptionW(
    _In_z_ PCSTR Name,
    _In_ const DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA* Data)
{
    printf("%s type %d flow %d", Name, Data->Type, Data->DataFlow);
    DcGuid(" id", &Data->DeviceId);
    printf("%s description %ls module %ls interface %ls wave %lu\n", Name,
           Data->Description != nullptr ? Data->Description : L"(null)", Data->Module != nullptr ? Data->Module : L"(null)",
           Data->Interface != nullptr ? Data->Interface : L"(null)", Data->WaveDeviceId);
}

static
VOID
DcShowDescriptionA(
    _In_z_ PCSTR Name,
    _In_ const DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA* Data)
{
    printf("%s type %d flow %d", Name, Data->Type, Data->DataFlow);
    DcGuid(" id", &Data->DeviceId);
    printf("%s description %s module %s interface %s wave %lu\n", Name,
           Data->Description != nullptr ? Data->Description : "(null)", Data->Module != nullptr ? Data->Module : "(null)",
           Data->Interface != nullptr ? Data->Interface : "(null)", Data->WaveDeviceId);
}

static
BOOL
CALLBACK
DcEnumDescriptionW(
    _In_ PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA Data,
    _In_ LPVOID Context)
{
    DcShowDescriptionW("  enumw", Data);
    *static_cast<UINT*>(Context) += 1;
    return TRUE;
}

static
BOOL
CALLBACK
DcEnumDescriptionA(
    _In_ PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA Data,
    _In_ LPVOID Context)
{
    DcShowDescriptionA("  enuma", Data);
    *static_cast<UINT*>(Context) += 1;
    return *static_cast<UINT*>(Context) < 1;
}

/* The DirectSoundPrivate class: its interfaces, which properties it answers and how big they are, and the device set */
static
VOID
DcCheckPrivate(VOID)
{
    PDC_GET_CLASS_OBJECT GetClassObject = reinterpret_cast<PDC_GET_CLASS_OBJECT>(GetProcAddress(DcModule, "DllGetClassObject"));
    IClassFactory* Factory = nullptr;
    IKsPropertySet* Properties = nullptr;
    IUnknown* Other = nullptr;
    HRESULT hr;

    hr = GetClassObject(CLSID_DirectSoundPrivate, IID_IClassFactory, reinterpret_cast<void**>(&Factory));
    printf("private factory = %08lX\n", hr);
    if (FAILED(hr))
        return;
    hr = Factory->CreateInstance(nullptr, IID_IKsPropertySet, reinterpret_cast<void**>(&Properties));
    printf("private create = %08lX\n", hr);
    Factory->Release();
    if (FAILED(hr))
        return;
    hr = Properties->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&Other));
    printf("private qi IUnknown = %08lX\n", hr);
    if (SUCCEEDED(hr))
        Other->Release();
    hr = Properties->QueryInterface(IID_IDirectSound, reinterpret_cast<void**>(&Other));
    printf("private qi IDirectSound = %08lX %s\n", hr, Other == nullptr ? "null" : "set");
    if (SUCCEEDED(hr))
        Other->Release();

    for (UINT Set = 0; Set < ARRAYSIZE(DcPrivateSets); Set++)
    {
        for (ULONG Id = 0; Id < 13; Id++)
        {
            ULONG Support = 0xCCCC;
            ULONG Returned = 0xCCCC;
            BYTE Data[4] = {};
            HRESULT Query = Properties->QuerySupport(DcPrivateSets[Set], Id, &Support);

            hr = Properties->Get(DcPrivateSets[Set], Id, nullptr, 0, Data, 0, &Returned);
            printf("private set%u[%lu] querysupport = %08lX %lX get_empty = %08lX %lu\n", Set, Id, Query, Support, hr, Returned);
        }
    }
    {
        BYTE Data[64] = {};
        ULONG Returned = 0xCCCC;

        hr = Properties->Get(DcPrivateSets[1], DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W, nullptr, 0, Data, 40, &Returned);
        printf("private description_short = %08lX %lu\n", hr, Returned);
        printf("private set_nodata = %08lX\n", Properties->Set(DcPrivateSets[1], DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W, nullptr, 0, nullptr, 0));
        printf("private querysupport_nullsupport = %08lX\n", Properties->QuerySupport(DcPrivateSets[1], 1, nullptr));
    }

    {
        static const struct
        {
            const GUID* Device;
            DIRECTSOUNDDEVICE_DATAFLOW Flow;
            PCSTR Name;
        } Devices[] =
        {
            { &DSDEVID_DefaultPlayback, DIRECTSOUNDDEVICE_DATAFLOW_RENDER, "playback" },
            { &DSDEVID_DefaultCapture, DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE, "capture" },
            { &DSDEVID_DefaultVoicePlayback, DIRECTSOUNDDEVICE_DATAFLOW_RENDER, "voiceplayback" },
            { &GUID_NULL, DIRECTSOUNDDEVICE_DATAFLOW_RENDER, "nullrender" },
            { &GUID_NULL, DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE, "nullcapture" },
            { &IID_IUnknown, DIRECTSOUNDDEVICE_DATAFLOW_RENDER, "bogus" },
        };

        for (UINT Index = 0; Index < ARRAYSIZE(Devices); Index++)
        {
            union
            {
                DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA W;
                DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA A;
                BYTE Space[4096];
            } Data;
            ULONG Returned;
            CHAR Name[64];

            ZeroMemory(&Data, sizeof(Data));
            Data.W.DeviceId = *Devices[Index].Device;
            Data.W.DataFlow = Devices[Index].Flow;
            Returned = 0xCCCC;
            hr = Properties->Get(DSPROPSETID_DirectSoundDevice, DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W, nullptr, 0, &Data,
                                 sizeof(Data.W), &Returned);
            printf("private %s description_w_fixed = %08lX %s\n", Devices[Index].Name, hr, Returned > sizeof(Data.W) ? "more" : "exact");
            Returned = 0xCCCC;
            hr = Properties->Get(DSPROPSETID_DirectSoundDevice, DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W, nullptr, 0, &Data,
                                 sizeof(Data), &Returned);
            printf("private %s description_w = %08lX\n", Devices[Index].Name, hr);
            sprintf_s(Name, "private %s w", Devices[Index].Name);
            if (SUCCEEDED(hr))
                DcShowDescriptionW(Name, &Data.W);

            ZeroMemory(&Data, sizeof(Data));
            Data.A.DeviceId = *Devices[Index].Device;
            Data.A.DataFlow = Devices[Index].Flow;
            hr = Properties->Get(DSPROPSETID_DirectSoundDevice, DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A, nullptr, 0, &Data,
                                 sizeof(Data), &Returned);
            printf("private %s description_a = %08lX\n", Devices[Index].Name, hr);
            sprintf_s(Name, "private %s a", Devices[Index].Name);
            if (SUCCEEDED(hr))
                DcShowDescriptionA(Name, &Data.A);
        }
    }

    {
        DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA EnumW = { DcEnumDescriptionW, nullptr };
        DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A_DATA EnumA = { DcEnumDescriptionA, nullptr };
        UINT Count = 0;
        ULONG Returned = 0xCCCC;

        EnumW.Context = &Count;
        hr = Properties->Get(DSPROPSETID_DirectSoundDevice, DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W, nullptr, 0, &EnumW,
                             sizeof(EnumW), &Returned);
        printf("private enumerate_w = %08lX %lu count %u\n", hr, Returned, Count);
        Count = 0;
        EnumA.Context = &Count;
        hr = Properties->Get(DSPROPSETID_DirectSoundDevice, DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A, nullptr, 0, &EnumA,
                             sizeof(EnumA), &Returned);
        printf("private enumerate_a = %08lX %lu count %u\n", hr, Returned, Count);
    }

    {
        WAVEOUTCAPSW Caps = {};
        DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA MapW = {};
        DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA MapA = {};
        CHAR NameA[MAXPNAMELEN];
        WCHAR Bogus[] = L"no such device";

        waveOutGetDevCapsW(0, &Caps, sizeof(Caps));
        WideCharToMultiByte(CP_ACP, 0, Caps.szPname, -1, NameA, sizeof(NameA), nullptr, nullptr);
        MapW.DeviceName = Caps.szPname;
        MapW.DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
        hr = Properties->Get(DSPROPSETID_DirectSoundDevice, DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W, nullptr, 0, &MapW,
                             sizeof(MapW), nullptr);
        printf("private mapping_w = %08lX", hr);
        DcGuid(" id", &MapW.DeviceId);
        MapA.DeviceName = NameA;
        MapA.DataFlow = DIRECTSOUNDDEVICE_DATAFLOW_RENDER;
        hr = Properties->Get(DSPROPSETID_DirectSoundDevice, DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A, nullptr, 0, &MapA,
                             sizeof(MapA), nullptr);
        printf("private mapping_a = %08lX", hr);
        DcGuid(" id", &MapA.DeviceId);
        ZeroMemory(&MapW, sizeof(MapW));
        MapW.DeviceName = Bogus;
        hr = Properties->Get(DSPROPSETID_DirectSoundDevice, DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W, nullptr, 0, &MapW,
                             sizeof(MapW), nullptr);
        printf("private mapping_bogus = %08lX\n", hr);
        MapW.DataFlow = static_cast<DIRECTSOUNDDEVICE_DATAFLOW>(5);
        hr = Properties->Get(DSPROPSETID_DirectSoundDevice, DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W, nullptr, 0, &MapW,
                             sizeof(MapW), nullptr);
        printf("private mapping_badflow = %08lX\n", hr);
    }
    printf("private release %lu\n", Properties->Release());
}

/* IKsPropertySet on primary, secondary and 3D buffers */
static
VOID
DcCheckProperties(VOID)
{
    static const struct
    {
        PCSTR Name;
        DWORD Flags;
        WORD Channels;
    } Buffers[] =
    {
        { "primary", DSBCAPS_PRIMARYBUFFER, 0 },
        { "secondary", DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN, 2 },
        { "buffer3d", DSBCAPS_CTRL3D, 1 },
    };
    IDirectSound8* Sound;

    if (FAILED(DcCreate8(&Sound)))
        return;
    Sound->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY);

    for (UINT Index = 0; Index < ARRAYSIZE(Buffers); Index++)
    {
        IDirectSoundBuffer* Buffer = nullptr;
        IKsPropertySet* Properties = nullptr;
        WAVEFORMATEX Format;
        HRESULT hr;

        DcFormat(&Format, WAVE_FORMAT_PCM, Buffers[Index].Channels, 44100, 16);
        hr = DcMakeBuffer(Sound, Buffers[Index].Flags, Buffers[Index].Channels != 0 ? 8820 : 0,
                          Buffers[Index].Channels != 0 ? &Format : nullptr, &Buffer);
        printf("properties %s create = %08lX\n", Buffers[Index].Name, hr);
        if (FAILED(hr))
            continue;
        hr = Buffer->QueryInterface(IID_IKsPropertySet, reinterpret_cast<void**>(&Properties));
        printf("properties %s qi = %08lX\n", Buffers[Index].Name, hr);
        if (SUCCEEDED(hr))
        {
            DcProbeProperties(Buffers[Index].Name, Properties, TRUE);
            Properties->Release();
        }
        Buffer->Release();
    }
    printf("release %lu\n", Sound->Release());
    DcCheckPrivate();
}

/* Records what the default endpoint plays, which carries the mixer output */
class DcLoopback
{
public:
    DcLoopback() : Client(nullptr), Capture(nullptr), Format(nullptr) {}

    ~DcLoopback()
    {
        if (Client != nullptr)
            Client->Stop();
        if (Capture != nullptr)
            Capture->Release();
        if (Client != nullptr)
            Client->Release();
        CoTaskMemFree(Format);
    }

    HRESULT
    Start(VOID)
    {
        IMMDeviceEnumerator* Enumerator = nullptr;
        IMMDevice* Endpoint = nullptr;
        HRESULT hr;

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&Enumerator));
        if (SUCCEEDED(hr))
            hr = Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &Endpoint);
        if (SUCCEEDED(hr))
        {
            IAudioEndpointVolume* Volume = nullptr;

            if (SUCCEEDED(Endpoint->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&Volume))))
            {
                BOOL Mute = FALSE;
                float Level = 0.0f;

                Volume->GetMute(&Mute);
                Volume->GetMasterVolumeLevelScalar(&Level);
                fprintf(stderr, "endpoint mute %d level %g\n", Mute, Level);
                Volume->Release();
            }
            hr = Endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&Client));
        }
        if (SUCCEEDED(hr))
            hr = Client->GetMixFormat(&Format);
        if (SUCCEEDED(hr))
            hr = Client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, Format, nullptr);
        if (SUCCEEDED(hr))
            hr = Client->GetService(IID_PPV_ARGS(&Capture));
        if (SUCCEEDED(hr))
            hr = Client->Start();
        if (Endpoint != nullptr)
            Endpoint->Release();
        if (Enumerator != nullptr)
            Enumerator->Release();
        return hr;
    }

    VOID
    Collect(
        _In_ DWORD Milliseconds)
    {
        ULONGLONG End = GetTickCount64() + Milliseconds;

        while (GetTickCount64() < End)
        {
            UINT32 Packet = 0;

            Sleep(5);
            while (SUCCEEDED(Capture->GetNextPacketSize(&Packet)) && Packet != 0)
            {
                BYTE* Data;
                UINT32 Frames;
                DWORD Flags;

                if (FAILED(Capture->GetBuffer(&Data, &Frames, &Flags, nullptr, nullptr)))
                    break;
                for (UINT32 Index = 0; Index < Frames * Format->nChannels; Index++)
                    Samples.push_back((Flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.0f : reinterpret_cast<float*>(Data)[Index]);
                Capture->ReleaseBuffer(Frames);
            }
        }
    }

    IAudioClient* Client;
    IAudioCaptureClient* Capture;
    WAVEFORMATEX* Format;
    std::vector<float> Samples;
};

/* Quiet noise with a marker frame first, so the start lines up between runtimes */
static
VOID
DcFillNoise(
    _Out_writes_bytes_(Bytes) BYTE* Data,
    _In_ DWORD Bytes,
    _In_ const WAVEFORMATEX* Format,
    _In_ ULONG Seed)
{
    UINT Width = Format->wBitsPerSample / 8;
    DWORD Samples = Bytes / Width;
    BOOLEAN Float = Format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                    (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                     reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

    for (DWORD Index = 0; Index < Samples; Index++)
    {
        float Value;

        Seed = Seed * 1103515245 + 12345;
        Value = (static_cast<float>((Seed >> 16) & 0x7FFF) / 32767.0f - 0.5f) * 0.008f;
        if (Index < Format->nChannels)
            Value = 0.004f;
        if (Float)
        {
            reinterpret_cast<float*>(Data)[Index] = Value;
        }
        else if (Width == 1)
        {
            Data[Index] = static_cast<BYTE>(128 + static_cast<int>(Value * 256.0f));
        }
        else if (Width == 2)
        {
            reinterpret_cast<SHORT*>(Data)[Index] = static_cast<SHORT>(Value * 32768.0f);
        }
        else if (Width == 3)
        {
            LONG Sample = static_cast<LONG>(Value * 8388608.0f);

            Data[Index * 3] = static_cast<BYTE>(Sample);
            Data[Index * 3 + 1] = static_cast<BYTE>(Sample >> 8);
            Data[Index * 3 + 2] = static_cast<BYTE>(Sample >> 16);
        }
    }
}

/* Puts the listener and a 3D buffer into one of the measured arrangements */
static
VOID
DcSetScene(
    _In_ IDirectSound8* Sound,
    _In_ IDirectSoundBuffer* Buffer,
    _In_ UINT Scene)
{
    IDirectSoundBuffer* Primary = nullptr;
    IDirectSound3DListener* Listener = nullptr;
    IDirectSound3DBuffer* Spatial = nullptr;
    DSBUFFERDESC Desc = {};

    Desc.dwSize = sizeof(Desc);
    Desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    if (FAILED(Sound->CreateSoundBuffer(&Desc, &Primary, nullptr)))
        return;
    Primary->QueryInterface(IID_IDirectSound3DListener, reinterpret_cast<void**>(&Listener));
    Buffer->QueryInterface(IID_IDirectSound3DBuffer, reinterpret_cast<void**>(&Spatial));
    if (Listener == nullptr || Spatial == nullptr)
    {
        printf("  scene interfaces missing\n");
        if (Listener != nullptr)
            Listener->Release();
        if (Spatial != nullptr)
            Spatial->Release();
        Primary->Release();
        return;
    }

    Listener->SetPosition(0.0f, 0.0f, 0.0f, DS3D_DEFERRED);
    Listener->SetVelocity(0.0f, 0.0f, 0.0f, DS3D_DEFERRED);
    Listener->SetOrientation(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, DS3D_DEFERRED);
    Listener->SetRolloffFactor(1.0f, DS3D_DEFERRED);
    Listener->SetDopplerFactor(1.0f, DS3D_DEFERRED);
    Listener->SetDistanceFactor(1.0f, DS3D_DEFERRED);

    switch (Scene)
    {
        case 1:
            break;
        case 2:
            Spatial->SetPosition(3.0f, 0.0f, 4.0f, DS3D_DEFERRED);
            break;
        case 3:
            Spatial->SetPosition(-1.2f, 0.5f, 0.8f, DS3D_DEFERRED);
            break;
        case 4:
            Listener->SetOrientation(1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, DS3D_DEFERRED);
            Listener->SetPosition(1.0f, 0.0f, -1.0f, DS3D_DEFERRED);
            Spatial->SetPosition(0.0f, 0.0f, 2.0f, DS3D_DEFERRED);
            break;
        case 5:
            Spatial->SetMode(DS3DMODE_HEADRELATIVE, DS3D_DEFERRED);
            Spatial->SetPosition(0.5f, 0.0f, -2.0f, DS3D_DEFERRED);
            Spatial->SetConeAngles(90, 180, DS3D_DEFERRED);
            Spatial->SetConeOutsideVolume(-1500, DS3D_DEFERRED);
            Spatial->SetConeOrientation(0.0f, 0.0f, 1.0f, DS3D_DEFERRED);
            break;
        case 6:
            Spatial->SetMode(DS3DMODE_DISABLE, DS3D_DEFERRED);
            Spatial->SetPosition(5.0f, 0.0f, 0.0f, DS3D_DEFERRED);
            Buffer->SetVolume(-800);
            break;
        case 7:
            Listener->SetRolloffFactor(2.0f, DS3D_DEFERRED);
            Spatial->SetMinDistance(2.0f, DS3D_DEFERRED);
            Spatial->SetPosition(0.0f, 3.0f, 6.0f, DS3D_DEFERRED);
            Spatial->SetConeAngles(30, 120, DS3D_DEFERRED);
            Spatial->SetConeOrientation(0.0f, 0.0f, -1.0f, DS3D_DEFERRED);
            Spatial->SetConeOutsideVolume(-2000, DS3D_DEFERRED);
            break;
        case 8:
            Listener->SetOrientation(0.3f, -0.2f, -0.9f, 0.1f, 0.9f, -0.2f, DS3D_DEFERRED);
            Spatial->SetPosition(-4.0f, 1.0f, -7.0f, DS3D_DEFERRED);
            break;
        case 9:
            Spatial->SetPosition(0.2f, 0.0f, 0.0f, DS3D_DEFERRED);
            Spatial->SetMaxDistance(0.1f, DS3D_DEFERRED);
            break;
        case 62:
            Spatial->SetPosition(0.0f, 0.0f, 5.0f, DS3D_DEFERRED);
            Spatial->SetVelocity(0.0f, 0.0f, -40.0f, DS3D_DEFERRED);
            break;
    }
    Listener->CommitDeferredSettings();
    Spatial->Release();
    Listener->Release();
    Primary->Release();
}

/* A new device object with its stream already running on a silent warm up, stereo unless a format is given */
static
HRESULT
DcOpenMixDevice(
    _Out_ IDirectSound8** Sound,
    _In_opt_ WAVEFORMATEX* WarmUp = nullptr)
{
    WAVEFORMATEX Format;
    IDirectSoundBuffer* Buffer;
    HRESULT hr;

    hr = DcCreate8(Sound);
    if (FAILED(hr))
        return hr;
    (*Sound)->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY);

    DcFormat(&Format, WAVE_FORMAT_PCM, 2, 48000, 16);
    if (WarmUp == nullptr)
        WarmUp = &Format;
    if (SUCCEEDED(DcMakeBuffer(*Sound, DSBCAPS_GLOBALFOCUS, WarmUp->nBlockAlign * 2400, WarmUp, &Buffer)))
    {
        Buffer->Play(0, 0, 0);
        Sleep(300);
        Buffer->Release();
    }
    return S_OK;
}

/* Waits until the mixer has moved the write cursor the given number of times, right after the last move */
static
DWORD
DcWaitForPasses(
    _In_ IDirectSoundBuffer* Buffer,
    _In_ UINT Passes)
{
    ULONGLONG End = GetTickCount64() + 1000;
    DWORD Last = 0;
    DWORD Write = 0;

    Buffer->GetCurrentPosition(nullptr, &Last);
    while (Passes > 0 && GetTickCount64() < End)
    {
        Buffer->GetCurrentPosition(nullptr, &Write);
        if (Write != Last)
        {
            Last = Write;
            Passes -= 1;
        }
    }
    return Last;
}

/* Plays short quiet buffers and compares what reaches the endpoint, sample for sample */
static
VOID
DcCheckMix(VOID)
{
    static const struct
    {
        WORD Tag;
        WORD Channels;
        BOOLEAN NativeRate;
        DWORD Rate;
        WORD Bits;
        LONG Volume;
        LONG Pan;
        DWORD Focus;
        UINT Scene;
    } Cases[] =
    {
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 16, -1234, 2500, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 16, -300, -7000, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, -500, 1500, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 8, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 24, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_IEEE_FLOAT, 2, TRUE, 0, 32, -600, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 1, FALSE, 22050, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, FALSE, 44100, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 1, FALSE, 8000, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, FALSE, 11025, 16, -400, 800, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 1, FALSE, 32000, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, FALSE, 96000, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, FALSE, 22050, 8, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_IEEE_FLOAT, 1, FALSE, 44100, 32, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 1, FALSE, 7999, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, FALSE, 88201, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 16, 0, 0, 0, 0 },
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 16, 0, 0, DSBCAPS_STICKYFOCUS, 0 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 1 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 2 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 3 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 4 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 5 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 6 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 7 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 8 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 9 },
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 50 },
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 16, -300, 0, DSBCAPS_GLOBALFOCUS, 51 },
        /* Rate changes last: a large one moves the whole device to another converter */
        { WAVE_FORMAT_PCM, 1, FALSE, 22050, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 60 },
        { WAVE_FORMAT_PCM, 1, FALSE, 22050, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 61 },
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_PCM, 2, FALSE, 44100, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 0 },
        { WAVE_FORMAT_IEEE_FLOAT, 2, FALSE, 32000, 32, 0, 0, DSBCAPS_GLOBALFOCUS, 61 },
        { WAVE_FORMAT_PCM, 1, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 62 },
        /* Rate changes during play, each on a new device that has not left the polyphase converter */
        { WAVE_FORMAT_PCM, 1, FALSE, 22050, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 63 },
        { WAVE_FORMAT_PCM, 2, FALSE, 44100, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 63 },
        { WAVE_FORMAT_PCM, 1, FALSE, 22050, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 64 },
        { WAVE_FORMAT_PCM, 1, FALSE, 22050, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 65 },
        { WAVE_FORMAT_PCM, 2, FALSE, 44100, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 65 },
        /* The last frames of a one shot buffer, through each converter */
        { WAVE_FORMAT_PCM, 1, FALSE, 22050, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 66 },
        { WAVE_FORMAT_PCM, 2, FALSE, 44100, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 66 },
        { WAVE_FORMAT_PCM, 1, FALSE, 22094, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 66 },
        { WAVE_FORMAT_PCM, 2, TRUE, 0, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 66 },
        { WAVE_FORMAT_PCM, 1, FALSE, 22050, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 67 },
        { WAVE_FORMAT_IEEE_FLOAT, 2, FALSE, 32000, 32, 0, 0, DSBCAPS_GLOBALFOCUS, 67 },
        /* Ends late in an engine pass, where the filter would ring on into the next one */
        { WAVE_FORMAT_PCM, 1, FALSE, 22050, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 68 },
        { WAVE_FORMAT_PCM, 2, FALSE, 44100, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 68 },
        /* Several passes through the cubic converter for each kind of sample */
        { WAVE_FORMAT_PCM, 2, FALSE, 22050, 8, 0, 0, DSBCAPS_GLOBALFOCUS, 69 },
        { WAVE_FORMAT_PCM, 1, FALSE, 32000, 24, 0, 0, DSBCAPS_GLOBALFOCUS, 69 },
        { WAVE_FORMAT_PCM, 2, FALSE, 22050, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 69 },
        { WAVE_FORMAT_PCM, 1, FALSE, 11025, 16, 0, 0, DSBCAPS_GLOBALFOCUS, 69 },
        { WAVE_FORMAT_IEEE_FLOAT, 1, FALSE, 44100, 32, 0, 0, DSBCAPS_GLOBALFOCUS, 69 },
    };
    IDirectSound8* Sound;

    /* A silent warm up gets the stream running, so its start does not land in a measurement */
    if (FAILED(DcOpenMixDevice(&Sound)))
        return;

    for (UINT Case = DcFirstMixCase; Case < ARRAYSIZE(Cases); Case++)
    {
        UINT Scene = Cases[Case].Scene;
        BOOLEAN Later = Scene >= 63 && Scene <= 65;
        BOOLEAN Ending = Scene >= 66 && Scene <= 68;
        BOOLEAN Long = Scene == 69;
        size_t Shown = Later || Long ? 1800 : 200;
        DcLoopback Loopback;
        WAVEFORMATEX Format;
        IDirectSoundBuffer* Buffer;
        DWORD Bytes;
        VOID* Part1;
        DWORD Size1;
        HRESULT hr;
        UINT Channels;
        size_t Onset;
        size_t Frames;

        if (Later || Ending || Long)
        {
            Sound->Release();
            if (FAILED(DcOpenMixDevice(&Sound)))
                return;
        }
        hr = Loopback.Start();
        if (FAILED(hr))
        {
            printf("mix[%u] loopback = %08lX\n", Case, hr);
            continue;
        }
        Channels = Loopback.Format->nChannels;
        DcFormat(&Format, Cases[Case].Tag, Cases[Case].Channels,
                 Cases[Case].NativeRate ? Loopback.Format->nSamplesPerSec : Cases[Case].Rate, Cases[Case].Bits);
        Bytes = Format.nAvgBytesPerSec / 4 / Format.nBlockAlign * Format.nBlockAlign;
        if (Scene == 68)
            Bytes += 30 * Format.nBlockAlign;
        hr = DcMakeBuffer(Sound,
                          DSBCAPS_CTRLVOLUME | Cases[Case].Focus |
                              (Cases[Case].Scene == 60 || Cases[Case].Scene == 61 || Later || Ending || Long ? DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN :
                               Cases[Case].Scene >= 50 && Cases[Case].Scene < 60 ? DSBCAPS_CTRLFX :
                               Cases[Case].Scene != 0 ? DSBCAPS_CTRL3D | DSBCAPS_CTRLFREQUENCY : DSBCAPS_CTRLPAN),
                          Bytes, &Format, &Buffer);
        if (FAILED(hr))
        {
            printf("mix[%u] create = %08lX\n", Case, hr);
            continue;
        }
        Buffer->Lock(0, 0, &Part1, &Size1, nullptr, nullptr, DSBLOCK_ENTIREBUFFER);
        DcFillNoise(static_cast<BYTE*>(Part1), Size1, &Format, 12345);
        Buffer->Unlock(Part1, Size1, nullptr, 0);
        Buffer->SetVolume(Cases[Case].Volume);
        if (Scene == 67 || Long)
        {
            printf("  setfrequency = %08lX\n", Buffer->SetFrequency(Format.nSamplesPerSec * 3 / 2));
        }
        else if (Cases[Case].Scene == 60 || Cases[Case].Scene == 61)
        {
            /* A tenth of a percent stays within the converter, half again does not */
            DWORD Rate = Format.nSamplesPerSec;

            printf("  setfrequency = %08lX\n", Buffer->SetFrequency(Cases[Case].Scene == 60 ? Rate + Rate / 1000 : Rate * 3 / 2));
        }
        else if (Cases[Case].Scene >= 50 && Cases[Case].Scene < 60)
        {
            IDirectSoundBuffer8* Buffer8 = nullptr;
            DSEFFECTDESC Effect = {};

            Effect.dwSize = sizeof(Effect);
            Effect.guidDSFXClass = Cases[Case].Scene == 50 ? GUID_DSFX_STANDARD_GARGLE : GUID_DSFX_STANDARD_ECHO;
            Buffer->QueryInterface(IID_IDirectSoundBuffer8, reinterpret_cast<void**>(&Buffer8));
            if (Buffer8 != nullptr)
            {
                printf("  setfx = %08lX\n", Buffer8->SetFX(1, &Effect, nullptr));
                Buffer8->Release();
            }
        }
        else if (Cases[Case].Scene != 0)
            DcSetScene(Sound, Buffer, Cases[Case].Scene);
        else
            Buffer->SetPan(Cases[Case].Pan);

        Loopback.Collect(100);
        hr = Buffer->Play(0, 0, 0);
        if (FAILED(hr))
            fprintf(stderr, "mix[%u] play = %08lX\n", Case, hr);
        if (Later)
        {
            /* The change lands between two mixer passes, a few passes into play */
            DWORD Rate = Format.nSamplesPerSec;
            DWORD Write = DcWaitForPasses(Buffer, 3);
            DWORD NewRate = Scene == 63 ? Rate + Rate / 1000 : Scene == 64 ? Rate + Rate / 500 : Rate * 3 / 2;

            hr = Buffer->SetFrequency(NewRate);
            printf("  setfrequency %lu after write %lu = %08lX\n", NewRate, Write, hr);
        }
        Loopback.Collect(500);
        {
            float Peak = 0.0f;

            for (float Value : Loopback.Samples)
                Peak = fabsf(Value) > Peak ? fabsf(Value) : Peak;
            fprintf(stderr, "mix[%u] peak %g\n", Case, Peak);
        }
        Buffer->Release();

        for (Onset = 0; Onset < Loopback.Samples.size(); Onset++)
        {
            if (fabsf(Loopback.Samples[Onset]) > 1e-4f)
                break;
        }
        Onset -= Onset % Channels;
        Frames = (Loopback.Samples.size() - Onset) / Channels;
        printf("mix[%u] format %u/%u/%lu/%u volume %ld pan %ld scene %u\n", Case, Format.wFormatTag, Format.nChannels,
               Format.nSamplesPerSec, Format.wBitsPerSample, Cases[Case].Volume, Cases[Case].Pan, Cases[Case].Scene);
        if (Onset >= Loopback.Samples.size())
        {
            fprintf(stderr, "mix[%u] heard nothing in %zu samples\n", Case, Loopback.Samples.size());
            printf("  silent\n");
            continue;
        }
        {
            const float* Start = &Loopback.Samples[Onset];
            size_t Count = Frames < 8000 ? Frames : 8000;
            double Sum[8] = {};
            double Power[8] = {};

            for (size_t Frame = 0; Frame < Count; Frame++)
            {
                for (UINT Channel = 0; Channel < Channels && Channel < 8; Channel++)
                {
                    Sum[Channel] += Start[Frame * Channels + Channel];
                    Power[Channel] += static_cast<double>(Start[Frame * Channels + Channel]) * Start[Frame * Channels + Channel];
                }
            }
            for (UINT Channel = 0; Channel < Channels && Channel < 8; Channel++)
                printf("  channel %u sum %.9g power %.9g\n", Channel, Sum[Channel], Power[Channel]);
            if (Ending)
            {
                /* Only the end is shown, from a little before where the input runs out */
                DWORD Played = Scene == 67 ? Format.nSamplesPerSec * 3 / 2 : Format.nSamplesPerSec;
                size_t End = static_cast<size_t>(static_cast<ULONGLONG>(Bytes / Format.nBlockAlign) * Loopback.Format->nSamplesPerSec / Played);
                size_t Skip = End > 120 ? End - 120 : 0;

                Skip = Skip < Frames ? Skip : Frames;
                Start += Skip * Channels;
                Frames -= Skip;
                Shown = 200;
            }
            for (size_t Frame = 0; Frame < Shown && Frame < Frames; Frame++)
            {
                printf("  frame %2zu", Frame);
                for (UINT Channel = 0; Channel < Channels && Channel < 8; Channel++)
                    printf(" %08lX", *reinterpret_cast<const DWORD*>(&Start[Frame * Channels + Channel]));
                printf("\n");
            }
        }
    }
    printf("release %lu\n", Sound->Release());
}

/* Prints the sums, powers and first frames of what the loopback heard, from the first sound on */
static
VOID
DcShowHeard(
    _In_ UINT Case,
    _In_ DcLoopback* Loopback,
    _In_ size_t Shown)
{
    UINT Channels = Loopback->Format->nChannels;
    size_t Onset;
    size_t Frames;
    size_t Count;
    double Sum[8] = {};
    double Power[8] = {};
    const float* Start;

    for (Onset = 0; Onset < Loopback->Samples.size(); Onset++)
    {
        if (fabsf(Loopback->Samples[Onset]) > 1e-4f)
            break;
    }
    if (Onset >= Loopback->Samples.size())
    {
        fprintf(stderr, "mix[%u] heard nothing in %zu samples\n", Case, Loopback->Samples.size());
        printf("  silent\n");
        return;
    }
    Onset -= Onset % Channels;
    Frames = (Loopback->Samples.size() - Onset) / Channels;
    Start = &Loopback->Samples[Onset];
    Count = Frames < 8000 ? Frames : 8000;

    for (size_t Frame = 0; Frame < Count; Frame++)
    {
        for (UINT Channel = 0; Channel < Channels && Channel < 8; Channel++)
        {
            Sum[Channel] += Start[Frame * Channels + Channel];
            Power[Channel] += static_cast<double>(Start[Frame * Channels + Channel]) * Start[Frame * Channels + Channel];
        }
    }
    for (UINT Channel = 0; Channel < Channels && Channel < 8; Channel++)
        printf("  channel %u sum %.9g power %.9g\n", Channel, Sum[Channel], Power[Channel]);
    for (size_t Frame = 0; Frame < Shown && Frame < Frames; Frame++)
    {
        printf("  frame %2zu", Frame);
        for (UINT Channel = 0; Channel < Channels && Channel < 8; Channel++)
            printf(" %08lX", *reinterpret_cast<const DWORD*>(&Start[Frame * Channels + Channel]));
        printf("\n");
    }
}

/* Several buffers at once, and buffers wider than the endpoint that pick the final mix layout */
static
VOID
DcCheckMixLayouts(VOID)
{
    struct DC_LAYOUT_BUFFER
    {
        WORD Tag;
        WORD Channels;
        DWORD Mask;
        DWORD Rate;
        WORD Bits;
        LONG Volume;
        LONG Pan;
        BOOLEAN Quiet;
    };
    static const struct
    {
        /* The warm up plays stereo, the first buffer's format so the final mix starts in its layout, or nothing */
        UCHAR WarmUp;
        UINT Count;
        DC_LAYOUT_BUFFER Buffers[3];
    } Cases[] =
    {
        { 1, 1, { { WAVE_FORMAT_PCM, 6, KSAUDIO_SPEAKER_5POINT1, 0, 16, 0, 0 } } },
        { 1, 1, { { WAVE_FORMAT_PCM, 6, KSAUDIO_SPEAKER_5POINT1, 0, 16, -600, 2000 } } },
        { 1, 1, { { WAVE_FORMAT_PCM, 4, KSAUDIO_SPEAKER_QUAD, 0, 16, 0, -3000 } } },
        { 1, 1, { { WAVE_FORMAT_IEEE_FLOAT, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND, 44100, 32, -300, 700 } } },
        { 1, 1, { { WAVE_FORMAT_IEEE_FLOAT, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND, 0, 32, -300, 700 } } },
        /* The second buffer locks the endpoint layout before the stream ever starts */
        { 2, 2, { { WAVE_FORMAT_PCM, 6, KSAUDIO_SPEAKER_5POINT1, 0, 16, 0, 1500 },
                  { WAVE_FORMAT_PCM, 2, 0, 0, 16, -1200, 0 } } },
        { 1, 2, { { WAVE_FORMAT_PCM, 6, KSAUDIO_SPEAKER_5POINT1, 0, 16, -400, -1500 },
                  { WAVE_FORMAT_PCM, 4, KSAUDIO_SPEAKER_QUAD, 0, 16, -800, 0 } } },
        { 0, 2, { { WAVE_FORMAT_PCM, 1, 0, 0, 16, -600, -2500 },
                  { WAVE_FORMAT_PCM, 2, 0, 0, 16, -900, 0 } } },
        { 0, 2, { { WAVE_FORMAT_PCM, 1, 0, 22050, 16, -600, 0 },
                  { WAVE_FORMAT_PCM, 2, 0, 44100, 16, -600, 1200 } } },
        { 0, 2, { { WAVE_FORMAT_PCM, 2, 0, 0, 8, -600, 0 },
                  { WAVE_FORMAT_IEEE_FLOAT, 1, 0, 32000, 32, -600, 0 } } },
        { 0, 2, { { WAVE_FORMAT_PCM, 6, KSAUDIO_SPEAKER_5POINT1, 0, 16, -600, 800 },
                  { WAVE_FORMAT_PCM, 1, 0, 0, 16, -600, 0 } } },
        /* A third buffer moves the whole device to the cubic converter for good, so it comes last. Only one plays,
           since starting a cubic voice can take the system longer than an engine pass */
        { 0, 3, { { WAVE_FORMAT_PCM, 1, 0, 22050, 16, -900, 0 },
                  { WAVE_FORMAT_PCM, 2, 0, 44100, 16, -900, 0, TRUE },
                  { WAVE_FORMAT_PCM, 1, 0, 0, 16, -900, 3000, TRUE } } },
        { 0, 3, { { WAVE_FORMAT_PCM, 2, 0, 44100, 16, -900, 600 },
                  { WAVE_FORMAT_PCM, 1, 0, 0, 16, -900, 0, TRUE },
                  { WAVE_FORMAT_PCM, 1, 0, 0, 16, -900, 0, TRUE } } },
    };
    const UINT First = 55;

    for (UINT Index = 0; Index < ARRAYSIZE(Cases); Index++)
    {
        UINT Case = First + Index;
        IDirectSoundBuffer* Buffers[3] = {};
        WAVEFORMATEXTENSIBLE Formats[3];
        IDirectSound8* Sound = nullptr;
        IDirectSound8* Keeper = nullptr;
        DcLoopback Loopback;
        HRESULT hr;

        if (Case < DcFirstMixCase)
            continue;
        hr = Loopback.Start();
        if (FAILED(hr))
        {
            printf("mix[%u] loopback = %08lX\n", Case, hr);
            continue;
        }

        for (UINT Slot = 0; Slot < Cases[Index].Count; Slot++)
        {
            const DC_LAYOUT_BUFFER* Want = &Cases[Index].Buffers[Slot];
            DWORD Rate = Want->Rate != 0 ? Want->Rate : Loopback.Format->nSamplesPerSec;

            if (Want->Mask != 0)
            {
                DcExtensible(&Formats[Slot], Want->Tag == WAVE_FORMAT_IEEE_FLOAT ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM,
                             Want->Channels, Rate, Want->Bits, Want->Bits, Want->Mask);
            }
            else
            {
                DcFormat(&Formats[Slot].Format, Want->Tag, Want->Channels, Rate, Want->Bits);
            }
        }
        /* Another device object keeps the endpoint running, so this one starts its stream on a warm engine */
        if (Cases[Index].WarmUp == 2)
        {
            if (FAILED(DcOpenMixDevice(&Keeper)))
                return;
            if (FAILED(DcCreate8(&Sound)))
            {
                Keeper->Release();
                return;
            }
            Sound->SetCooperativeLevel(DcWindow, DSSCL_PRIORITY);
        }
        else if (FAILED(DcOpenMixDevice(&Sound, Cases[Index].WarmUp == 1 ? &Formats[0].Format : nullptr)))
        {
            return;
        }

        printf("mix[%u] buffers %u\n", Case, Cases[Index].Count);
        for (UINT Slot = 0; Slot < Cases[Index].Count; Slot++)
        {
            const DC_LAYOUT_BUFFER* Want = &Cases[Index].Buffers[Slot];
            WAVEFORMATEX* Format = &Formats[Slot].Format;
            DWORD Bytes = Format->nAvgBytesPerSec / 4 / Format->nBlockAlign * Format->nBlockAlign;
            VOID* Part1;
            DWORD Size1;

            hr = DcMakeBuffer(Sound, DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_GLOBALFOCUS, Bytes, Format, &Buffers[Slot]);
            printf("  buffer %u format %u/%u/%lX/%lu/%u volume %ld pan %ld%s = %08lX\n", Slot, Want->Tag, Want->Channels, Want->Mask,
                   Format->nSamplesPerSec, Want->Bits, Want->Volume, Want->Pan, Want->Quiet ? " unplayed" : "", hr);
            if (FAILED(hr))
                continue;
            Buffers[Slot]->Lock(0, 0, &Part1, &Size1, nullptr, nullptr, DSBLOCK_ENTIREBUFFER);
            DcFillNoise(static_cast<BYTE*>(Part1), Size1, Format, 12345 + Slot * 777);
            Buffers[Slot]->Unlock(Part1, Size1, nullptr, 0);
            Buffers[Slot]->SetVolume(Want->Volume);
            Buffers[Slot]->SetPan(Want->Pan);
        }

        Loopback.Collect(100);
        for (UINT Slot = 0; Slot < Cases[Index].Count; Slot++)
        {
            if (Buffers[Slot] != nullptr && !Cases[Index].Buffers[Slot].Quiet)
            {
                hr = Buffers[Slot]->Play(0, 0, 0);
                if (FAILED(hr))
                    fprintf(stderr, "mix[%u] play %u = %08lX\n", Case, Slot, hr);
            }
        }
        Loopback.Collect(500);
        for (UINT Slot = 0; Slot < Cases[Index].Count; Slot++)
        {
            if (Buffers[Slot] != nullptr)
                Buffers[Slot]->Release();
        }
        DcShowHeard(Case, &Loopback, 200);
        Sound->Release();
        if (Keeper != nullptr)
            Keeper->Release();
    }
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    PCWSTR Runtime = L"system";

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
            Runtime = argv[++Index];
        else if (_wcsicmp(argv[Index], L"-section") == 0 && Index + 1 < argc)
            DcSection = argv[++Index];
        else if (_wcsicmp(argv[Index], L"-from") == 0 && Index + 1 < argc)
            DcFirstMixCase = static_cast<UINT>(_wtoi(argv[++Index]));
    }

    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DcModule = LoadLibraryW(DcRuntimePath(Runtime).c_str());
    if (DcModule == NULL)
    {
        fprintf(stderr, "error: cannot load the runtime (%lu)\n", GetLastError());
        return 1;
    }
    {
        WCHAR Path[MAX_PATH];

        GetModuleFileNameW(DcModule, Path, _countof(Path));
        fprintf(stderr, "runtime %ls\n", Path);
    }
    DcWindow = CreateWindowExW(0, L"STATIC", L"dsoundcheck", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);

    if (DcWant(L"enumerate"))
        DcCheckEnumerate();
    if (DcWant(L"deviceid"))
        DcCheckDeviceId();
    if (DcWant(L"create"))
        DcCheckCreate();
    if (DcWant(L"factory"))
        DcCheckClassFactory();
    if (DcWant(L"device"))
        DcCheckDevice();
    if (DcWant(L"descs"))
        DcCheckBufferDescs();
    if (DcWant(L"methods"))
        DcCheckBufferMethods();
    if (DcWant(L"playback"))
        DcCheckPlayback();
    if (DcWant(L"3d"))
        DcCheck3d();
    if (DcWant(L"capture"))
        DcCheckCapture();
    if (DcWant(L"duplex"))
        DcCheckFullDuplex();
    if (DcWant(L"effects"))
        DcCheckEffects();
    if (DcWant(L"properties"))
        DcCheckProperties();

    /* Audible, so only on request */
    if (DcSection != nullptr && _wcsicmp(DcSection, L"mix") == 0)
    {
        DcCheckMix();
        DcCheckMixLayouts();
    }

    DestroyWindow(DcWindow);
    FreeLibrary(DcModule);
    CoUninitialize();
    return 0;
}
