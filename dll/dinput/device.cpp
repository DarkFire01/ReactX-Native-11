/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The IDirectInputDevice object shared by every kind of device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput.hpp"

#define DI_DEFAULT_MAX_BUFFER   1023
#define DI_DEFAULT_GAIN         10000
#define DI_INSTANCE_MASK        0x0000FF00
#define DI_INSTANCE_MASK_7      0x00FFFF00
#define DI_ENUM_ALL_TYPES       0x000000DF
#define DI_ENUM_INVALID_FLAGS   0xE0000020
#define DI_EFFECT_TYPE_INVALID  0xFBFFFF00
#define DI_FF_COMMAND_MASK      0x0000003F

/* Sizes and rules for the properties the runtime knows about */
#define DI_PROPF_NOT_DEVICE     0x01
#define DI_PROPF_NOT_OBJECT     0x02
#define DI_PROPF_READ_ONLY      0x04
#define DI_PROPF_NEEDS_SINK     0x08
#define DI_PROPF_NOT_ACQUIRED   0x10

typedef struct _RX_DI_PROPERTY_RULE
{
    ULONG_PTR Id;
    ULONG Size;
    ULONG Rules;
} RX_DI_PROPERTY_RULE;

static const RX_DI_PROPERTY_RULE DiPropertyRules[] =
{
    { 1, sizeof(DIPROPDWORD), 0x1A },               /* DIPROP_BUFFERSIZE */
    { 2, sizeof(DIPROPDWORD), 0x1A },               /* DIPROP_AXISMODE */
    { 3, sizeof(DIPROPDWORD), 0x15 },               /* DIPROP_GRANULARITY */
    { 4, sizeof(DIPROPRANGE), 0x11 },               /* DIPROP_RANGE */
    { 5, sizeof(DIPROPDWORD), 0x10 },               /* DIPROP_DEADZONE */
    { 6, sizeof(DIPROPDWORD), 0x10 },               /* DIPROP_SATURATION */
    { 7, sizeof(DIPROPDWORD), 0x02 },               /* DIPROP_FFGAIN */
    { 8, sizeof(DIPROPDWORD), 0x06 },               /* DIPROP_FFLOAD */
    { 9, sizeof(DIPROPDWORD), 0x12 },               /* DIPROP_AUTOCENTER */
    { 10, sizeof(DIPROPDWORD), 0x12 },              /* DIPROP_CALIBRATIONMODE */
    { 11, sizeof(DIPROPCAL), 0x11 },                /* DIPROP_CALIBRATION */
    { 12, sizeof(DIPROPGUIDANDPATH), 0x06 },        /* DIPROP_GUIDANDPATH */
    { 13, sizeof(DIPROPSTRING), 0x02 },             /* DIPROP_INSTANCENAME */
    { 14, sizeof(DIPROPSTRING), 0x02 },             /* DIPROP_PRODUCTNAME */
    { DI_PROP_MAX_BUFFER, sizeof(DIPROPDWORD), 0x1A },
    { 15, sizeof(DIPROPDWORD), 0x12 },              /* DIPROP_JOYSTICKID */
    { 16, sizeof(DIPROPSTRING), 0x06 },             /* DIPROP_GETPORTDISPLAYNAME */
    { DI_PROP_ENABLE_OBJECT, sizeof(DIPROPDWORD), 0x00 },
};

/* Calibration accepts the point list form as well */
#define DI_PROPCALPOV_SIZE  56

/* The generic joystick layout old applications were matched against */
#define DI_LEGACY_AXIS      (DI_TYPE_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE)
#define DI_LEGACY_POV       (DI_TYPE_OPTIONAL | DIDFT_POV | DIDFT_ANYINSTANCE)
#define DI_LEGACY_BUTTON    (DI_TYPE_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE)

static const DIOBJECTDATAFORMAT DiLegacyJoystickObjects[] =
{
    { &GUID_XAxis, 0, DI_LEGACY_AXIS, DIDOI_ASPECTPOSITION },
    { &GUID_YAxis, 4, DI_LEGACY_AXIS, DIDOI_ASPECTPOSITION },
    { &GUID_ZAxis, 8, DI_LEGACY_AXIS, DIDOI_ASPECTPOSITION },
    { &GUID_RxAxis, 12, DI_LEGACY_AXIS, DIDOI_ASPECTPOSITION },
    { &GUID_RyAxis, 16, DI_LEGACY_AXIS, DIDOI_ASPECTPOSITION },
    { &GUID_RzAxis, 20, DI_LEGACY_AXIS, DIDOI_ASPECTPOSITION },
    { &GUID_Slider, 24, DI_LEGACY_AXIS, DIDOI_ASPECTPOSITION },
    { &GUID_Slider, 28, DI_LEGACY_AXIS, DIDOI_ASPECTPOSITION },
    { &GUID_POV, 32, DI_LEGACY_POV, 0 },
    { &GUID_POV, 36, DI_LEGACY_POV, 0 },
    { &GUID_POV, 40, DI_LEGACY_POV, 0 },
    { &GUID_POV, 44, DI_LEGACY_POV, 0 },
    { nullptr, 48, DI_LEGACY_BUTTON, 0 },
    { nullptr, 49, DI_LEGACY_BUTTON, 0 },
    { nullptr, 50, DI_LEGACY_BUTTON, 0 },
    { nullptr, 51, DI_LEGACY_BUTTON, 0 },
    { nullptr, 52, DI_LEGACY_BUTTON, 0 },
    { nullptr, 53, DI_LEGACY_BUTTON, 0 },
    { nullptr, 54, DI_LEGACY_BUTTON, 0 },
    { nullptr, 55, DI_LEGACY_BUTTON, 0 },
    { nullptr, 56, DI_LEGACY_BUTTON, 0 },
    { nullptr, 57, DI_LEGACY_BUTTON, 0 },
    { nullptr, 58, DI_LEGACY_BUTTON, 0 },
    { nullptr, 59, DI_LEGACY_BUTTON, 0 },
    { nullptr, 60, DI_LEGACY_BUTTON, 0 },
    { nullptr, 61, DI_LEGACY_BUTTON, 0 },
    { nullptr, 62, DI_LEGACY_BUTTON, 0 },
    { nullptr, 63, DI_LEGACY_BUTTON, 0 },
    { nullptr, 64, DI_LEGACY_BUTTON, 0 },
    { nullptr, 65, DI_LEGACY_BUTTON, 0 },
    { nullptr, 66, DI_LEGACY_BUTTON, 0 },
    { nullptr, 67, DI_LEGACY_BUTTON, 0 },
    { nullptr, 68, DI_LEGACY_BUTTON, 0 },
    { nullptr, 69, DI_LEGACY_BUTTON, 0 },
    { nullptr, 70, DI_LEGACY_BUTTON, 0 },
    { nullptr, 71, DI_LEGACY_BUTTON, 0 },
    { nullptr, 72, DI_LEGACY_BUTTON, 0 },
    { nullptr, 73, DI_LEGACY_BUTTON, 0 },
    { nullptr, 74, DI_LEGACY_BUTTON, 0 },
    { nullptr, 75, DI_LEGACY_BUTTON, 0 },
    { nullptr, 76, DI_LEGACY_BUTTON, 0 },
    { nullptr, 77, DI_LEGACY_BUTTON, 0 },
    { nullptr, 78, DI_LEGACY_BUTTON, 0 },
    { nullptr, 79, DI_LEGACY_BUTTON, 0 },
};

static const DIDATAFORMAT DiLegacyJoystick =
{
    sizeof(DIDATAFORMAT),
    sizeof(DIOBJECTDATAFORMAT),
    DIDF_ABSAXIS,
    80,
    _countof(DiLegacyJoystickObjects),
    const_cast<LPDIOBJECTDATAFORMAT>(DiLegacyJoystickObjects)
};

/* Stands in for a backend until Initialize picks one */
class RxDiNilBackend final : public RxDiBackend
{
public:
    HRESULT GetVersions(_Inout_ LPDIDRIVERVERSIONS) override { return DIERR_NOTINITIALIZED; }
    HRESULT GetDataFormat(_Outptr_ const DIDATAFORMAT** Format) override { *Format = nullptr; return DIERR_NOTINITIALIZED; }
    HRESULT GetObjectInfo(_In_ const RX_DI_PROP_TARGET*, _Inout_ LPDIDEVICEOBJECTINSTANCEW) override { return DIERR_NOTINITIALIZED; }
    HRESULT GetCapabilities(_Inout_ LPDIDEVCAPS) override { return DIERR_NOTINITIALIZED; }
    HRESULT Acquire(VOID) override { return DIERR_NOTINITIALIZED; }
    HRESULT Unacquire(VOID) override { return DIERR_NOTINITIALIZED; }
    HRESULT GetDeviceState(_Out_ PVOID) override { return DIERR_NOTINITIALIZED; }
    HRESULT GetDeviceInfo(_Inout_ LPDIDEVICEINSTANCEW) override { return DIERR_NOTINITIALIZED; }
    HRESULT GetProperty(_In_ const RX_DI_PROP_TARGET*, _Inout_ LPDIPROPHEADER) override { return DIERR_NOTINITIALIZED; }
    HRESULT SetProperty(_In_ const RX_DI_PROP_TARGET*, _In_ LPCDIPROPHEADER) override { return DIERR_NOTINITIALIZED; }
    HRESULT SetEventNotification(_In_opt_ HANDLE) override { return DIERR_NOTINITIALIZED; }
    HRESULT SetCooperativeLevel(_In_opt_ HWND, _In_ DWORD) override { return DIERR_NOTINITIALIZED; }
    HRESULT RunControlPanel(_In_opt_ HWND, _In_ DWORD) override { return DIERR_NOTINITIALIZED; }
    HRESULT CookDeviceData(_In_ ULONG, _Inout_opt_ DIDEVICEOBJECTDATA_DX3*) override { return DIERR_NOTINITIALIZED; }
    HRESULT CreateEffectDriver(_Outptr_ RxDiShepherd** Driver) override { *Driver = nullptr; return DIERR_NOTINITIALIZED; }
    HRESULT GetFFConfigKey(_In_ REGSAM, _Out_ HKEY* Key) override { *Key = nullptr; return DIERR_NOTINITIALIZED; }
    HRESULT SendDeviceData(_In_reads_(*Count) LPCDIDEVICEOBJECTDATA, _Inout_ LPDWORD Count, _In_ DWORD) override { *Count = 0; return DIERR_NOTINITIALIZED; }
    HRESULT Poll(VOID) override { return DIERR_NOTINITIALIZED; }
    HRESULT MapUsage(_In_ ULONG, _Out_ PULONG Index) override { *Index = 0; return DIERR_NOTINITIALIZED; }
    HRESULT SetDIData(_In_ DWORD, _In_ RX_DI_APP_HACKS*) override { return DIERR_NOTINITIALIZED; }
};

static RxDiNilBackend DiNilBackend;

/* Devices acquired in foreground mode, watched for losing activation */
static RxDiDevice** DiForegroundDevices;
static LONG DiForegroundCount;
static LONG DiForegroundCapacity;
static HHOOK DiForegroundHook;
static BOOL DiLostFocus;

static
ULONG
DiTypeSize(
    _In_ ULONG Type)
{
    return (Type & DI_TYPE_HAS_DWORD) ? sizeof(DWORD) : sizeof(BYTE);
}

static
VOID
DiObjectInfoToA(
    _Inout_ LPDIDEVICEOBJECTINSTANCEA Target,
    _In_ const DIDEVICEOBJECTINSTANCEW* Source)
{
    Target->guidType = Source->guidType;
    Target->dwOfs = Source->dwOfs;
    Target->dwType = Source->dwType;
    Target->dwFlags = Source->dwFlags;
    WideCharToMultiByte(CP_ACP, 0, Source->tszName, -1, Target->tszName, MAX_PATH, nullptr, nullptr);
    if (Target->dwSize >= sizeof(DIDEVICEOBJECTINSTANCEA))
    {
        Target->dwFFMaxForce = Source->dwFFMaxForce;
        Target->dwFFForceResolution = Source->dwFFForceResolution;
        Target->wCollectionNumber = Source->wCollectionNumber;
        Target->wDesignatorIndex = Source->wDesignatorIndex;
        Target->wUsagePage = Source->wUsagePage;
        Target->wUsage = Source->wUsage;
        Target->dwDimension = Source->dwDimension;
        Target->wExponent = Source->wExponent;
        Target->wReportId = Source->wReportId;
    }
}

typedef struct _RX_DI_FORWARD_A
{
    PVOID Callback;
    LPVOID Context;
} RX_DI_FORWARD_A;

static
BOOL
CALLBACK
DiEnumObjectsToA(
    _In_ LPCDIDEVICEOBJECTINSTANCEW Instance,
    _In_ LPVOID Context)
{
    RX_DI_FORWARD_A* Forward = static_cast<RX_DI_FORWARD_A*>(Context);
    DIDEVICEOBJECTINSTANCEA Narrow = {};

    Narrow.dwSize = sizeof(Narrow);
    DiObjectInfoToA(&Narrow, Instance);
    return reinterpret_cast<LPDIENUMDEVICEOBJECTSCALLBACKA>(Forward->Callback)(&Narrow, Forward->Context);
}

static
BOOL
CALLBACK
DiEnumEffectsToA(
    _In_ LPCDIEFFECTINFOW Info,
    _In_ LPVOID Context)
{
    RX_DI_FORWARD_A* Forward = static_cast<RX_DI_FORWARD_A*>(Context);
    DIEFFECTINFOA Narrow = {};

    Narrow.dwSize = sizeof(Narrow);
    Narrow.guid = Info->guid;
    Narrow.dwEffType = Info->dwEffType;
    Narrow.dwStaticParams = Info->dwStaticParams;
    Narrow.dwDynamicParams = Info->dwDynamicParams;
    WideCharToMultiByte(CP_ACP, 0, Info->tszName, -1, Narrow.tszName, MAX_PATH, nullptr, nullptr);
    return reinterpret_cast<LPDIENUMEFFECTSCALLBACKA>(Forward->Callback)(&Narrow, Forward->Context);
}

RxDiDevice::RxDiDevice(
    _In_opt_ IUnknown* OuterUnknown)
    : RxDiUnknown(OuterUnknown)
{
    Backend = &DiNilBackend;
    State = 0;
    Window = nullptr;
    CoopFlags = 0;
    Event = nullptr;
    CopyMode = StateCopy::Slow;
    RelativeAxes = FALSE;
    ObjectToApp = nullptr;
    AppToObject = nullptr;
    AppDataSize = 0;
    LegacyObjectToApp = nullptr;
    LegacyAppToObject = nullptr;
    LegacyDataSize = 0;
    ZeroMemory(&Native, sizeof(Native));
    CopyDelta = 0;
    CopyStart = 0;
    CopyLength = 0;
    Scratch = nullptr;
    Baseline = nullptr;
    RelativeAxisCount = 0;
    RelativeAxisOffsets = nullptr;
    Sink = nullptr;
    PolledFlag = 0;
    LostResult = S_OK;
    BufferSize = 0;
    PovFill = nullptr;
    PovFillCount = 0;
    EffectList = nullptr;
    EffectCount = 0;
    ForceFeedbackCaps = 0;
    ZeroMemory(&FFAttributes, sizeof(FFAttributes));
    MaxBufferSize = DI_DEFAULT_MAX_BUFFER;
    EffectDriver = nullptr;
    ZeroMemory(&DriverId, sizeof(DriverId));
    CreatedVersion = 0;
    CreatedEffects = nullptr;
    CreatedEffectCount = 0;
    Gain = 0;
    AutoCenter = 0;
    InstanceMask = 0;
    LockDepth = 0;
    LockOwner = 0;
    InitializeCriticalSection(&Lock);
    State |= DI_DEV_LOCK_READY;
    InstanceGuid = GUID_NULL;
    ZeroMemory(&Hacks, sizeof(Hacks));
}

RxDiDevice::~RxDiDevice()
{
    ResetState();
    if (State & DI_DEV_LOCK_READY)
        DeleteCriticalSection(&Lock);
}

HRESULT
RxDiDevice::Create(
    _In_opt_ IUnknown* OuterUnknown,
    _In_ REFIID Iid,
    _COM_Outptr_ PVOID* Object)
{
    RxDiDevice* Created;
    HRESULT hr;

    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    hr = DiStartup();
    if (FAILED(hr))
        return hr;

    if (&Iid == nullptr)
        return E_POINTER;
    if (OuterUnknown != nullptr && !IsEqualIID(Iid, IID_IUnknown))
        return CLASS_E_NOAGGREGATION;

    Created = new RxDiDevice(OuterUnknown);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    if (OuterUnknown != nullptr)
    {
        *Object = Created->InnerUnknown();
        return S_OK;
    }

    hr = Created->InnerUnknown()->QueryInterface(Iid, Object);
    Created->InnerUnknown()->Release();
    return hr;
}

PVOID
RxDiDevice::QueryOwn(
    _In_ REFIID Iid)
{
    if (IsEqualIID(Iid, IID_IDirectInputDevice7W) || IsEqualIID(Iid, IID_IDirectInputDeviceW) ||
        IsEqualIID(Iid, IID_IDirectInputDevice2W))
    {
        return static_cast<IDirectInputDevice7W*>(this);
    }

    if (IsEqualIID(Iid, IID_IDirectInputDevice7A) || IsEqualIID(Iid, IID_IDirectInputDeviceA) ||
        IsEqualIID(Iid, IID_IDirectInputDevice2A))
    {
        return static_cast<IDirectInputDevice7A*>(this);
    }

    return nullptr;
}

VOID
RxDiDevice::OnFinalRelease(VOID)
{
    PVOID* Effects;
    ULONG Count;

    if (State & DI_DEV_ACQUIRED)
        InternalUnacquire();

    Enter();
    Effects = CreatedEffects;
    Count = CreatedEffectCount;
    CreatedEffects = nullptr;
    CreatedEffectCount = 0;
    Leave();

    DiReleaseCreatedEffects(Effects, Count);
    DiFree(reinterpret_cast<PVOID*>(&Effects));
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ PVOID* Object)
{
    if (&Iid == nullptr || Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    return Outer->QueryInterface(Iid, Object);
}

ULONG
STDMETHODCALLTYPE
RxDiDevice::AddRef(VOID)
{
    return Outer->AddRef();
}

ULONG
STDMETHODCALLTYPE
RxDiDevice::Release(VOID)
{
    return Outer->Release();
}

VOID
RxDiDevice::Enter(VOID)
{
    EnterCriticalSection(&Lock);
    LockOwner = GetCurrentThreadId();
    InterlockedIncrement(&LockDepth);
}

VOID
RxDiDevice::Leave(VOID)
{
    if (InterlockedDecrement(&LockDepth) == 0)
        LockOwner = 0;
    LeaveCriticalSection(&Lock);
}

VOID
RxDiDevice::MarkLost(VOID)
{
    State |= DI_DEV_LOST;
    if (Event != nullptr)
        SetEvent(Event);
}

HRESULT
RxDiDevice::ResetState(VOID)
{
    if (State & DI_DEV_ACQUIRED)
        return DIERR_ACQUIRED;
    if (CreatedEffectCount != 0)
        return DIERR_HASEFFECTS;

    if (EffectDriver != nullptr)
    {
        RxDiShepherd* Driver = EffectDriver;

        EffectDriver = nullptr;
        delete Driver;
    }

    if (Backend != &DiNilBackend)
    {
        RxDiBackend* Old = Backend;

        Backend = &DiNilBackend;
        delete Old;
    }

    DiFree(reinterpret_cast<PVOID*>(&ObjectToApp));
    DiFree(reinterpret_cast<PVOID*>(&AppToObject));
    DiFree(reinterpret_cast<PVOID*>(&LegacyObjectToApp));
    DiFree(reinterpret_cast<PVOID*>(&LegacyAppToObject));
    DiFree(reinterpret_cast<PVOID*>(&Scratch));
    DiFree(reinterpret_cast<PVOID*>(&Baseline));
    DiFree(reinterpret_cast<PVOID*>(&RelativeAxisOffsets));
    DiFree(reinterpret_cast<PVOID*>(&EffectList));
    DiFree(reinterpret_cast<PVOID*>(&PovFill));
    if (Event != nullptr)
        CloseHandle(Event);

    Window = nullptr;
    CoopFlags = 0;
    Event = nullptr;
    CopyMode = StateCopy::Slow;
    AppDataSize = 0;
    LegacyDataSize = 0;
    ZeroMemory(&Native, sizeof(Native));
    CopyDelta = 0;
    CopyStart = 0;
    CopyLength = 0;
    RelativeAxisCount = 0;
    Sink = nullptr;
    PolledFlag = 0;
    BufferSize = 0;
    PovFillCount = 0;
    EffectCount = 0;
    ForceFeedbackCaps = 0;
    ZeroMemory(&FFAttributes, sizeof(FFAttributes));
    InstanceGuid = GUID_NULL;

    MaxBufferSize = DI_DEFAULT_MAX_BUFFER;
    RelativeAxes = FALSE;
    LostResult = DIERR_NOTACQUIRED;
    State &= ~DI_DEV_COOKS_DATA;
    Gain = DI_DEFAULT_GAIN;
    AutoCenter = 1;
    InstanceMask = DI_INSTANCE_MASK;
    return S_OK;
}

HRESULT
RxDiDevice::LoadDeviceFormat(VOID)
{
    const DIDATAFORMAT* Format = nullptr;
    HRESULT hr;

    hr = Backend->GetDataFormat(&Format);
    if (FAILED(hr))
        return hr;

    Native = *Format;

    hr = DiAllocate(Native.dwDataSize, reinterpret_cast<PVOID*>(&Baseline));
    if (FAILED(hr))
        return hr;

    hr = DiAllocate(sizeof(ULONG) * Native.dwNumObjs, reinterpret_cast<PVOID*>(&RelativeAxisOffsets));
    if (FAILED(hr))
        return hr;

    /* Axes keep a baseline so relative mode can report movement since the last read */
    RelativeAxisCount = 0;
    for (ULONG Index = 0; Index < Native.dwNumObjs; Index++)
    {
        if (Native.rgodf[Index].dwType & DIDFT_AXIS)
            RelativeAxisOffsets[RelativeAxisCount++] = Native.rgodf[Index].dwOfs;
    }

    return S_OK;
}

HRESULT
RxDiDevice::InitForceFeedback(VOID)
{
    HKEY Key = nullptr;
    HKEY Effects = nullptr;
    DWORD Count = 0;
    HRESULT hr;

    hr = Backend->GetFFConfigKey(KEY_READ, &Key);
    if (hr == S_FALSE)
    {
        hr = CreateEffectDriver();
        if (FAILED(hr))
            return hr;

        hr = Backend->GetFFConfigKey(KEY_READ, &Key);
        if (hr == S_FALSE)
            return E_FAIL;
    }

    if (FAILED(hr))
        return hr;

    if (DiReadAttributes(Key, &FFAttributes) != S_OK)
        ZeroMemory(&FFAttributes, sizeof(FFAttributes));

    if (RegOpenKeyExW(Key, L"Effects", 0, KEY_READ, &Effects) != ERROR_SUCCESS)
    {
        hr = E_UNEXPECTED;
    }
    else
    {
        if (RegQueryInfoKeyW(Effects, nullptr, nullptr, nullptr, &Count, nullptr, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
        {
            hr = E_FAIL;
        }
        else
        {
            EffectList = nullptr;
            hr = DiAllocate(sizeof(RX_DI_EFFECT_ENTRY) * Count, reinterpret_cast<PVOID*>(&EffectList));
            if (SUCCEEDED(hr))
            {
                EffectCount = 0;
                for (DWORD Index = 0; Index < Count; Index++)
                {
                    if (DiReadEffectEntry(Effects, Index, &EffectList[EffectCount]))
                        EffectCount++;
                }

                ForceFeedbackCaps &= 0xFF00;
                if (EffectCount != 0)
                    ForceFeedbackCaps |= DIDC_FORCEFEEDBACK;
                hr = S_OK;
            }
        }
        RegCloseKey(Effects);
    }

    RegCloseKey(Key);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::Initialize(
    HINSTANCE Instance,
    DWORD Version,
    REFGUID Guid)
{
    PRX_DI_BACKEND_CREATE CreateBackend = nullptr;
    RxDiBackend* Created = nullptr;
    DIDEVCAPS Caps;
    HRESULT hr;

    if (&Guid == nullptr)
        return E_POINTER;

    Enter();

    hr = DiCheckVersion(Instance, Version);
    if (FAILED(hr))
        goto Done;

    hr = DiFindBackend(Guid, &CreateBackend);
    if (FAILED(hr))
        goto Done;

    hr = ResetState();
    if (FAILED(hr))
        goto Done;

    hr = CreateBackend(Guid, &Created);
    if (FAILED(hr))
        goto Done;
    Backend = Created;

    hr = LoadDeviceFormat();
    if (FAILED(hr))
        goto Done;

    ZeroMemory(&Caps, sizeof(Caps));
    Caps.dwSize = sizeof(DIDEVCAPS_DX3);
    hr = Backend->GetCapabilities(&Caps);
    if (FAILED(hr))
        goto Done;

    PolledFlag = (Caps.dwFlags & DIDC_POLLEDDEVICE) ? DI_POLLEDDEVICE : DI_OK;
    Sink = Backend->GetSink();
    CreatedVersion = Version;

    DiGetAppHacks(&Hacks);
    Backend->SetDIData(Version, &Hacks);

    /* Old applications reading a joystick through the generic layout get it pre-parsed */
    if (Version < 0x0700 && Version != 0x05B2 && Hacks.DataFormatHint == DIDEVTYPE_JOYSTICK)
        ParseLegacyJoystickFormat();

    if (Version >= 0x0700)
        InstanceMask = DI_INSTANCE_MASK_7;

    InstanceGuid = Guid;
    if (Sink != nullptr && (Sink->Flags & DI_SINK_EMULATED))
        Sink->Device = this;

    if (SUCCEEDED(Backend->CookDeviceData(0, nullptr)))
        State |= DI_DEV_COOKS_DATA;

    InitForceFeedback();
    hr = S_OK;

Done:
    if (FAILED(hr))
        DI_TRACE_ONCE(L"device initialize failed, 0x%08lx", hr);
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetCapabilities(
    LPDIDEVCAPS Caps)
{
    DIDRIVERVERSIONS Versions;
    HRESULT hr;

    if (Caps == nullptr)
        return E_POINTER;
    if (Caps->dwSize != sizeof(DIDEVCAPS_DX3) && Caps->dwSize != sizeof(DIDEVCAPS))
        return E_INVALIDARG;

    Enter();
    ZeroMemory(reinterpret_cast<PBYTE>(Caps) + sizeof(DWORD), Caps->dwSize - sizeof(DWORD));

    hr = Backend->GetCapabilities(Caps);
    if (SUCCEEDED(hr))
    {
        if (Sink != nullptr && (Sink->Flags & DI_SINK_EMULATED))
            Caps->dwFlags |= DIDC_EMULATED;
        if (State & DI_DEV_POLLED_FORMAT)
            Caps->dwFlags |= DIDC_POLLEDDATAFORMAT;
        Caps->dwFlags |= ForceFeedbackCaps;

        if (Caps->dwSize >= sizeof(DIDEVCAPS))
        {
            Caps->dwFFSamplePeriod = FFAttributes.dwFFSamplePeriod;
            Caps->dwFFMinTimeResolution = FFAttributes.dwFFMinTimeResolution;

            ZeroMemory(&Versions, sizeof(Versions));
            Versions.dwSize = sizeof(Versions);
            if (FAILED(CreateEffectDriver()) || FAILED(EffectDriver->GetVersions(&Versions)))
                Backend->GetVersions(&Versions);

            Caps->dwFirmwareRevision = Versions.dwFirmwareRevision;
            Caps->dwHardwareRevision = Versions.dwHardwareRevision;
            Caps->dwFFDriverVersion = Versions.dwFFDriverVersion;
        }
        hr = S_OK;
    }

    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetDeviceInfo(
    LPDIDEVICEINSTANCEW Info)
{
    HRESULT hr;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(DIDEVICEINSTANCE_DX3W) && Info->dwSize != sizeof(DIDEVICEINSTANCEW))
        return E_INVALIDARG;

    Enter();
    Info->guidInstance = InstanceGuid;
    Info->guidProduct = InstanceGuid;
    ZeroMemory(&Info->dwDevType, Info->dwSize - FIELD_OFFSET(DIDEVICEINSTANCEW, dwDevType));
    hr = Backend->GetDeviceInfo(Info);
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetDeviceInfo(
    LPDIDEVICEINSTANCEA Info)
{
    DIDEVICEINSTANCEW Wide = {};
    HRESULT hr;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(DIDEVICEINSTANCE_DX3A) && Info->dwSize != sizeof(DIDEVICEINSTANCEA))
        return E_INVALIDARG;

    Wide.dwSize = sizeof(Wide);
    hr = GetDeviceInfo(&Wide);
    if (FAILED(hr))
        return hr;

    Info->guidInstance = Wide.guidInstance;
    Info->guidProduct = Wide.guidProduct;
    Info->dwDevType = Wide.dwDevType;
    WideCharToMultiByte(CP_ACP, 0, Wide.tszInstanceName, -1, Info->tszInstanceName, MAX_PATH, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, Wide.tszProductName, -1, Info->tszProductName, MAX_PATH, nullptr, nullptr);
    if (Info->dwSize >= sizeof(DIDEVICEINSTANCEA))
    {
        Info->guidFFDriver = Wide.guidFFDriver;
        Info->wUsage = Wide.wUsage;
        Info->wUsagePage = Wide.wUsagePage;
    }
    return S_OK;
}

ULONG
RxDiDevice::FindObject(
    _In_ const DIOBJECTDATAFORMAT* Wanted,
    _In_ const ULONG* Used)
{
    for (ULONG Index = 0; Index < Native.dwNumObjs; Index++)
    {
        const DIOBJECTDATAFORMAT* Have = &Native.rgodf[Index];
        BOOL GuidMatches;

        if ((Have->dwType & Wanted->dwType & 0xFF) == 0)
            continue;
        if ((Have->dwType & Wanted->dwType & DI_TYPE_ATTRIBUTE_MASK) != (Wanted->dwType & DI_TYPE_ATTRIBUTE_MASK))
            continue;
        if (Used[Index] != MAXULONG)
            continue;

        if (Wanted->pguid == nullptr)
            GuidMatches = TRUE;
        else if (Wanted->dwFlags & DIDOI_GUIDISUSAGE)
            GuidMatches = (reinterpret_cast<ULONG_PTR>(Wanted->pguid) == Backend->GetUsage(Index));
        else
            GuidMatches = IsEqualGUID(*Wanted->pguid, GUID_NULL) || IsEqualGUID(*Wanted->pguid, *Have->pguid);

        if (!GuidMatches)
            continue;

        /* Either any instance or the one asked for */
        if ((InstanceMask & ~Wanted->dwType) != 0 && ((Have->dwType ^ Wanted->dwType) & InstanceMask) != 0)
            continue;

        if ((Wanted->dwFlags & DIDOI_ASPECTMASK) == 0 || (Have->dwFlags & DIDOI_ASPECTMASK) == 0 ||
            ((Have->dwFlags ^ Wanted->dwFlags) & DIDOI_ASPECTMASK) == 0)
        {
            return Index;
        }
    }

    return MAXULONG;
}

HRESULT
RxDiDevice::ParseFormat(
    _In_ LPCDIDATAFORMAT Format)
{
    PULONG ObjectMap = nullptr;
    PULONG AppMap = nullptr;
    PULONG SinkMap = nullptr;
    ULONG Limit = (Format->dwDataSize < 0x10000) ? Format->dwDataSize : 0x10000;
    HRESULT hr;

    hr = DiAllocate(sizeof(ULONG) * Native.dwNumObjs, reinterpret_cast<PVOID*>(&ObjectMap));
    if (SUCCEEDED(hr))
        hr = DiAllocate(sizeof(ULONG) * Native.dwDataSize, reinterpret_cast<PVOID*>(&SinkMap));
    if (SUCCEEDED(hr))
        hr = DiAllocate(sizeof(ULONG) * Format->dwDataSize, reinterpret_cast<PVOID*>(&AppMap));
    if (SUCCEEDED(hr))
        hr = DiAllocate(sizeof(ULONG) * Format->dwNumObjs, reinterpret_cast<PVOID*>(&PovFill));
    if (FAILED(hr))
        goto Done;

    FillMemory(ObjectMap, sizeof(ULONG) * Native.dwNumObjs, 0xFF);
    FillMemory(SinkMap, sizeof(ULONG) * Native.dwDataSize, 0xFF);
    FillMemory(AppMap, sizeof(ULONG) * Format->dwDataSize, 0xFF);

    for (ULONG Index = 0; Index < Format->dwNumObjs; Index++)
    {
        const DIOBJECTDATAFORMAT* Wanted = &Format->rgodf[Index];
        const DIOBJECTDATAFORMAT* Have;
        DIPROPDWORD Enable;
        ULONG Found;

        if (Wanted->dwOfs >= Limit)
        {
            hr = E_INVALIDARG;
            goto Done;
        }

        Found = FindObject(Wanted, ObjectMap);

        /* DirectInput 5 and older matched a Z axis request to a slider too */
        if (Found == MAXULONG && Wanted->pguid != nullptr && CreatedVersion < 0x0600 &&
            IsEqualGUID(*Wanted->pguid, GUID_ZAxis))
        {
            DIOBJECTDATAFORMAT Slider = *Wanted;

            Slider.pguid = &GUID_Slider;
            Found = FindObject(&Slider, ObjectMap);
        }

        if (Found == MAXULONG)
        {
            if ((Wanted->dwType & DI_TYPE_OPTIONAL) == 0)
            {
                hr = E_INVALIDARG;
                goto Done;
            }

            if (Wanted->dwType & DIDFT_POV)
                PovFill[PovFillCount++] = Wanted->dwOfs;
            continue;
        }

        Have = &Native.rgodf[Found];
        if ((Have->dwType & DI_TYPE_HAS_DWORD) && (Wanted->dwOfs & 3) != 0)
        {
            hr = E_INVALIDARG;
            goto Done;
        }

        if (State & DI_DEV_COOKS_DATA)
            SinkMap[Have->dwOfs] = (Wanted->dwOfs & 0xFFFF) | ((Have->dwType & 0xFFFF) << 16);
        else
            SinkMap[Have->dwOfs] = Wanted->dwOfs;

        ObjectMap[Found] = Wanted->dwOfs;
        AppMap[Wanted->dwOfs] = Found;
        if (Have->dwFlags & DIDOI_POLLED)
            State |= DI_DEV_POLLED_FORMAT;

        Enable.diph.dwSize = sizeof(Enable);
        Enable.diph.dwHeaderSize = sizeof(Enable.diph);
        Enable.diph.dwObj = Have->dwType;
        Enable.diph.dwHow = DIPH_BYID;
        Enable.dwData = 1;
        ApplyProperty(DI_PROP_ENABLE_OBJECT, &Enable.diph);
    }

    hr = DiAllocate(sizeof(ULONG) * PovFillCount, reinterpret_cast<PVOID*>(&PovFill));
    if (FAILED(hr))
        goto Done;

    if (Sink != nullptr)
    {
        if ((Sink->Flags & DI_SINK_EMULATED) == 0)
        {
            hr = DIERR_UNSUPPORTED_SINK;
            goto Done;
        }

        hr = DiSetSinkFormat(Sink, Native.dwDataSize, SinkMap);
        if (FAILED(hr))
            goto Done;
    }

    ObjectToApp = ObjectMap;
    AppToObject = AppMap;
    AppDataSize = Format->dwDataSize;
    ObjectMap = nullptr;
    AppMap = nullptr;
    hr = S_OK;

Done:
    DiFree(reinterpret_cast<PVOID*>(&ObjectMap));
    DiFree(reinterpret_cast<PVOID*>(&AppMap));
    DiFree(reinterpret_cast<PVOID*>(&SinkMap));
    return hr;
}

HRESULT
RxDiDevice::ParseLegacyJoystickFormat(VOID)
{
    const DIDATAFORMAT* Generic = &DiLegacyJoystick;
    PULONG ObjectMap = nullptr;
    PULONG AppMap = nullptr;
    HRESULT hr;

    hr = DiAllocate(sizeof(ULONG) * Native.dwNumObjs, reinterpret_cast<PVOID*>(&ObjectMap));
    if (SUCCEEDED(hr))
        hr = DiAllocate(sizeof(ULONG) * Generic->dwDataSize, reinterpret_cast<PVOID*>(&AppMap));
    if (FAILED(hr))
    {
        hr = E_OUTOFMEMORY;
        goto Done;
    }

    FillMemory(ObjectMap, sizeof(ULONG) * Native.dwNumObjs, 0xFF);
    FillMemory(AppMap, sizeof(ULONG) * Generic->dwDataSize, 0xFF);

    for (ULONG Index = 0; Index < Generic->dwNumObjs; Index++)
    {
        const DIOBJECTDATAFORMAT* Wanted = &Generic->rgodf[Index];
        ULONG Found;

        if (Wanted->dwOfs >= Generic->dwDataSize)
        {
            hr = E_INVALIDARG;
            goto Done;
        }

        Found = FindObject(Wanted, ObjectMap);
        if (Found == MAXULONG)
        {
            if ((Wanted->dwType & DI_TYPE_OPTIONAL) == 0)
            {
                hr = E_INVALIDARG;
                goto Done;
            }
            continue;
        }

        if ((Native.rgodf[Found].dwType & DI_TYPE_HAS_DWORD) && (Wanted->dwOfs & 3) != 0)
        {
            hr = E_INVALIDARG;
            goto Done;
        }

        ObjectMap[Found] = Wanted->dwOfs;
        AppMap[Wanted->dwOfs] = Found;
    }

    LegacyObjectToApp = ObjectMap;
    LegacyAppToObject = AppMap;
    LegacyDataSize = Generic->dwDataSize;
    ObjectMap = nullptr;
    AppMap = nullptr;
    hr = S_OK;

Done:
    DiFree(reinterpret_cast<PVOID*>(&ObjectMap));
    DiFree(reinterpret_cast<PVOID*>(&AppMap));
    return hr;
}

VOID
RxDiDevice::OptimizeFormat(VOID)
{
    LONG Delta = 0;
    ULONG Lowest = MAXULONG;
    ULONG End = 0;
    BOOLEAN Mapped = FALSE;
    BOOLEAN Uniform = TRUE;
    ULONG ScratchSize;

    /* A layout that is the device layout shifted by a constant can be copied in one go */
    for (ULONG Index = Native.dwNumObjs; Index-- > 0;)
    {
        const DIOBJECTDATAFORMAT* Have = &Native.rgodf[Index];
        LONG ThisDelta;

        if (ObjectToApp[Index] == MAXULONG)
            continue;

        ThisDelta = static_cast<LONG>(ObjectToApp[Index] - Have->dwOfs);
        if (Mapped && ThisDelta != Delta)
        {
            Uniform = FALSE;
            break;
        }

        Mapped = TRUE;
        Delta = ThisDelta;
        if (Have->dwOfs < Lowest)
            Lowest = Have->dwOfs;
        if (Have->dwOfs + DiTypeSize(Have->dwType) > End)
            End = Have->dwOfs + DiTypeSize(Have->dwType);
    }

    if (!Mapped || !Uniform)
    {
        CopyMode = StateCopy::Slow;
    }
    else
    {
        CopyDelta = Delta;
        CopyStart = Lowest;
        CopyLength = End - Lowest;

        if (Delta < 0 || Native.dwDataSize + Delta > AppDataSize)
            CopyMode = StateCopy::Matched;
        else if (Delta == 0 && AppDataSize == Native.dwDataSize)
            CopyMode = StateCopy::Equal;
        else
            CopyMode = StateCopy::Direct;
    }

    ScratchSize = (CopyMode == StateCopy::Slow || CopyMode == StateCopy::Matched) ? Native.dwDataSize : 0;
    if (FAILED(DiAllocate(ScratchSize, reinterpret_cast<PVOID*>(&Scratch))))
        DiFree(reinterpret_cast<PVOID*>(&ObjectToApp));
}

HRESULT
RxDiDevice::ReadNative(
    _Out_ PVOID Target)
{
    HRESULT hr = Backend->GetDeviceState(Target);

    if (FAILED(hr) || !RelativeAxes)
        return hr;

    for (ULONG Index = 0; Index < RelativeAxisCount; Index++)
    {
        ULONG Offset = RelativeAxisOffsets[Index];
        LONG* Value = reinterpret_cast<LONG*>(static_cast<PBYTE>(Target) + Offset);
        LONG* Previous = reinterpret_cast<LONG*>(Baseline + Offset);
        LONG Now = *Value;

        *Value = Now - *Previous;
        *Previous = Now;
    }
    return S_OK;
}

HRESULT
RxDiDevice::ReadState(
    _Out_ PVOID Target)
{
    PBYTE App = static_cast<PBYTE>(Target);
    HRESULT hr;

    switch (CopyMode)
    {
        case StateCopy::Equal:
            return ReadNative(Target);

        case StateCopy::Direct:
            ZeroMemory(App, AppDataSize);
            return ReadNative(App + CopyDelta);

        case StateCopy::Matched:
            hr = ReadNative(Scratch);
            if (SUCCEEDED(hr))
            {
                ZeroMemory(App, AppDataSize);
                CopyMemory(App + CopyDelta + CopyStart, Scratch + CopyStart, CopyLength);
            }
            return hr;

        default:
            break;
    }

    hr = ReadNative(Scratch);
    if (FAILED(hr))
        return hr;

    ZeroMemory(App, AppDataSize);
    for (ULONG Index = Native.dwNumObjs; Index-- > 0;)
    {
        const DIOBJECTDATAFORMAT* Have = &Native.rgodf[Index];
        ULONG Offset = ObjectToApp[Index];

        if (Offset == MAXULONG)
            continue;

        if (Have->dwType & DI_TYPE_HAS_DWORD)
            *reinterpret_cast<DWORD UNALIGNED*>(App + Offset) = *reinterpret_cast<DWORD UNALIGNED*>(Scratch + Have->dwOfs);
        else
            App[Offset] = Scratch[Have->dwOfs];
    }
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::SetDataFormat(
    LPCDIDATAFORMAT Format)
{
    DIPROPDWORD Value;
    HRESULT hr;

    if (Format == nullptr)
        return E_POINTER;
    if (Format->dwSize != sizeof(DIDATAFORMAT))
        return E_INVALIDARG;
    if ((Format->dwFlags & ~(DIDF_ABSAXIS | DIDF_RELAXIS)) != 0 || (Format->dwDataSize & 3) != 0 ||
        Format->dwObjSize != sizeof(DIOBJECTDATAFORMAT))
    {
        return E_INVALIDARG;
    }
    if (Format->rgodf == nullptr && Format->dwNumObjs != 0)
        return E_POINTER;

    Enter();

    if (State & DI_DEV_ACQUIRED)
    {
        hr = DIERR_ACQUIRED;
        goto Done;
    }

    DiFree(reinterpret_cast<PVOID*>(&ObjectToApp));
    DiFree(reinterpret_cast<PVOID*>(&AppToObject));
    PovFillCount = 0;
    State &= ~DI_DEV_POLLED_FORMAT;

    /* Tell the backend every object is off before turning the used ones back on */
    ZeroMemory(&Value, sizeof(Value));
    Value.diph.dwSize = sizeof(Value);
    Value.diph.dwHeaderSize = sizeof(Value.diph);
    hr = ApplyProperty(DI_PROP_ENABLE_OBJECT, &Value.diph);
    if (FAILED(hr) && hr != E_NOTIMPL)
        goto Done;

    hr = ParseFormat(Format);
    if (FAILED(hr))
        goto Done;

    OptimizeFormat();

    switch (Format->dwFlags)
    {
        case 0:
            hr = S_OK;
            break;

        case DIDF_ABSAXIS:
        case DIDF_RELAXIS:
            Value.diph.dwObj = 0;
            Value.diph.dwHow = DIPH_DEVICE;
            Value.dwData = (Format->dwFlags == DIDF_RELAXIS) ? DIPROPAXISMODE_REL : DIPROPAXISMODE_ABS;
            hr = ApplyProperty(reinterpret_cast<ULONG_PTR>(&DIPROP_AXISMODE), &Value.diph);
            if (SUCCEEDED(hr))
                hr = S_OK;
            break;

        default:
            hr = E_INVALIDARG;
            break;
    }

Done:
    Leave();
    return hr;
}

HRESULT
RxDiDevice::MapHow(
    _In_ DWORD Object,
    _In_ DWORD How,
    _Inout_ RX_DI_PROP_TARGET* Target)
{
    if (Backend == &DiNilBackend)
        return DIERR_NOTINITIALIZED;

    switch (How)
    {
        case DIPH_DEVICE:
            if (Object != 0)
                return E_INVALIDARG;
            Target->Index = MAXULONG;
            Target->Type = 0;
            return S_OK;

        case DIPH_BYOFFSET:
            if (ObjectToApp == nullptr || AppToObject == nullptr || Object >= AppDataSize)
                return DIERR_OBJECTNOTFOUND;
            if (static_cast<LONG>(AppToObject[Object]) < 0)
                return DIERR_OBJECTNOTFOUND;
            Target->Index = AppToObject[Object];
            Target->Type = Native.rgodf[Target->Index].dwType;
            return S_OK;

        case DIPH_BYID:
            for (ULONG Index = Native.dwNumObjs; Index-- > 0;)
            {
                if (((Native.rgodf[Index].dwType ^ Object) & 0x00FFFFFF) == 0)
                {
                    Target->Index = Index;
                    Target->Type = Native.rgodf[Index].dwType;
                    return S_OK;
                }
            }
            return DIERR_OBJECTNOTFOUND;

        case DIPH_BYUSAGE:
        {
            HRESULT hr = Backend->MapUsage(Object, &Target->Index);

            if (SUCCEEDED(hr))
                Target->Type = Native.rgodf[Target->Index].dwType;
            return hr;
        }
    }

    return E_INVALIDARG;
}

HRESULT
RxDiDevice::ValidateProperty(
    _In_ ULONG_PTR Property,
    _In_ LPCDIPROPHEADER Header,
    _Inout_ RX_DI_PROP_TARGET* Target)
{
    HRESULT hr;

    if (Header == nullptr || Header->dwHeaderSize != sizeof(DIPROPHEADER))
        return E_INVALIDARG;
    if ((Header->dwSize & 3) != 0 || Header->dwSize < sizeof(DIPROPHEADER))
        return E_INVALIDARG;

    hr = MapHow(Header->dwObj, Header->dwHow, Target);
    if (FAILED(hr))
        return hr;

    /* Properties named by GUID go straight to the backend */
    if (Property > 0xFFFF)
        return S_OK;

    for (ULONG Index = 0; Index < _countof(DiPropertyRules); Index++)
    {
        if (DiPropertyRules[Index].Id != Property)
            continue;

        if (Header->dwSize == DiPropertyRules[Index].Size)
            return S_OK;
        if (Property == 11 && Header->dwSize == DI_PROPCALPOV_SIZE)
            return S_OK;
        return E_INVALIDARG;
    }

    return E_NOTIMPL;
}

HRESULT
RxDiDevice::CheckDefaultProperty(
    _In_ const RX_DI_PROP_TARGET* Target,
    _In_ BOOLEAN Set)
{
    ULONG Asked;
    ULONG Hit;

    for (ULONG Index = 0; Index < _countof(DiPropertyRules); Index++)
    {
        if (DiPropertyRules[Index].Id != Target->Property)
            continue;

        Asked = (Target->Index == MAXULONG) ? DI_PROPF_NOT_DEVICE : DI_PROPF_NOT_OBJECT;
        if (Sink == nullptr)
            Asked |= DI_PROPF_NEEDS_SINK;
        if (Set)
        {
            Asked |= DI_PROPF_READ_ONLY;
            if (State & DI_DEV_ACQUIRED)
                Asked |= DI_PROPF_NOT_ACQUIRED;
        }

        Hit = DiPropertyRules[Index].Rules & Asked;
        if (Hit == 0)
            return S_OK;
        if (Hit & DI_PROPF_READ_ONLY)
            return E_ACCESSDENIED;
        if (Hit & DI_PROPF_NOT_ACQUIRED)
            return DIERR_ACQUIRED;
        return E_NOTIMPL;
    }

    return E_NOTIMPL;
}

HRESULT
RxDiDevice::DefaultGetProperty(
    _In_ const RX_DI_PROP_TARGET* Target,
    _Inout_ LPDIPROPHEADER Header)
{
    LPDIPROPDWORD Value = reinterpret_cast<LPDIPROPDWORD>(Header);
    HRESULT hr;

    hr = CheckDefaultProperty(Target, FALSE);
    if (FAILED(hr))
        return hr;

    switch (Target->Property)
    {
        case 1:
            Value->dwData = BufferSize;
            return S_OK;

        case 2:
            Value->dwData = (Sink->Flags & DI_SINK_RELATIVE) ? DIPROPAXISMODE_REL : DIPROPAXISMODE_ABS;
            return S_OK;

        case 3:
            if ((Target->Type & DIDFT_AXIS) == 0)
                return E_NOTIMPL;
            Value->dwData = 1;
            return S_OK;

        case 4:
            if ((Target->Type & DIDFT_RELAXIS) == 0)
                return E_NOTIMPL;
            reinterpret_cast<LPDIPROPRANGE>(Header)->lMin = DIPROPRANGE_NOMIN;
            reinterpret_cast<LPDIPROPRANGE>(Header)->lMax = DIPROPRANGE_NOMAX;
            return S_OK;

        case 7:
            Value->dwData = Gain;
            return S_OK;

        case 8:
        {
            DIDEVICESTATE Status = {};

            hr = CreateEffectDriver();
            if (FAILED(hr))
                return hr;

            Enter();
            Status.dwSize = sizeof(Status);
            hr = EffectDriver->GetForceFeedbackState(&DriverId, &Status);
            Value->dwData = Status.dwLoad;
            Leave();
            return hr;
        }

        case 9:
            if ((ForceFeedbackCaps & DIDC_FORCEFEEDBACK) == 0)
                return E_NOTIMPL;
            Value->dwData = AutoCenter;
            return S_OK;

        case DI_PROP_MAX_BUFFER:
            Value->dwData = MaxBufferSize;
            return S_OK;
    }

    return E_NOTIMPL;
}

HRESULT
RxDiDevice::DefaultSetProperty(
    _In_ const RX_DI_PROP_TARGET* Target,
    _In_ LPCDIPROPHEADER Header)
{
    const DIPROPDWORD* Value = reinterpret_cast<const DIPROPDWORD*>(Header);
    RX_DI_PROP_TARGET Each;
    HRESULT hr;

    hr = CheckDefaultProperty(Target, TRUE);
    if (FAILED(hr))
        return hr;

    switch (Target->Property)
    {
        case 1:
        {
            ULONG Count = (Value->dwData > MaxBufferSize) ? MaxBufferSize : Value->dwData;

            if ((Sink->Flags & DI_SINK_EMULATED) == 0)
                return DIERR_UNSUPPORTED_SINK;

            hr = DiSetSinkBuffer(Sink, Count);
            if (FAILED(hr))
                return hr;

            BufferSize = Value->dwData;
            return PolledFlag;
        }

        case 2:
            if (Value->dwData & ~DIPROPAXISMODE_REL)
                return E_INVALIDARG;

            RelativeAxes = (Value->dwData & DIPROPAXISMODE_REL) != 0;
            if (RelativeAxes)
                Sink->Flags |= DI_SINK_RELATIVE;
            else
                Sink->Flags &= ~DI_SINK_RELATIVE;
            if (Hacks.NativeAxisOnly)
                Sink->Flags |= DI_SINK_NATIVE_AXES;

            /* Nothing changes for a device without axes */
            return (RelativeAxisCount == 0) ? DI_PROPNOEFFECT : DI_OK;

        case 4:
        case 5:
        case 6:
        case 10:
        case 11:
            if (Target->Type != 0)
                return E_NOTIMPL;

            Each = *Target;
            for (ULONG Index = 0; Index < Native.dwNumObjs; Index++)
            {
                ULONG Type = Native.rgodf[Index].dwType;

                if ((Type & DIDFT_ABSAXIS) == 0)
                    continue;

                Each.Index = Index;
                Each.Type = Type;
                hr = Backend->SetProperty(&Each, Header);
                if (FAILED(hr) && hr != E_NOTIMPL)
                    return hr;
            }
            return S_OK;

        case 7:
            if (Value->dwData & 0xFFFF0000)
                return E_INVALIDARG;
            Gain = Value->dwData;
            RefreshGain();
            return S_OK;

        case 9:
            if (Value->dwData & ~DIPROPAUTOCENTER_ON)
                return E_INVALIDARG;
            if ((ForceFeedbackCaps & DIDC_FORCEFEEDBACK) == 0)
                return E_NOTIMPL;
            if (Value->dwData == DIPROPAUTOCENTER_OFF)
            {
                hr = CreateEffectDriver();
                if (FAILED(hr))
                    return hr;
            }
            AutoCenter = Value->dwData;
            return S_OK;

        case DI_PROP_MAX_BUFFER:
            MaxBufferSize = Value->dwData;
            return S_OK;
    }

    return E_NOTIMPL;
}

HRESULT
RxDiDevice::ApplyProperty(
    _In_ ULONG_PTR Property,
    _In_ LPCDIPROPHEADER Header)
{
    RX_DI_PROP_TARGET Target = {};
    HRESULT hr;

    Enter();
    Target.Property = Property;
    hr = ValidateProperty(Property, Header, &Target);
    if (SUCCEEDED(hr))
    {
        hr = Backend->SetProperty(&Target, Header);
        if (hr == E_NOTIMPL)
            hr = DefaultSetProperty(&Target, Header);
    }
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetProperty(
    REFGUID Property,
    LPDIPROPHEADER Header)
{
    RX_DI_PROP_TARGET Target = {};
    HRESULT hr;

    Enter();
    Target.Property = reinterpret_cast<ULONG_PTR>(&Property);
    hr = ValidateProperty(Target.Property, Header, &Target);
    if (SUCCEEDED(hr))
    {
        hr = Backend->GetProperty(&Target, Header);
        if (hr == E_NOTIMPL)
            hr = DefaultGetProperty(&Target, Header);
    }
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::SetProperty(
    REFGUID Property,
    LPCDIPROPHEADER Header)
{
    return ApplyProperty(reinterpret_cast<ULONG_PTR>(&Property), Header);
}

HRESULT
RxDiDevice::FillObjectInfo(
    _In_ const RX_DI_PROP_TARGET* Target,
    _Inout_ LPDIDEVICEOBJECTINSTANCEW Info)
{
    const DIOBJECTDATAFORMAT* Have = &Native.rgodf[Target->Index];
    PULONG Map = ObjectToApp;

    Info->guidType = *Have->pguid;
    Info->dwType = Have->dwType;
    Info->dwFlags = Have->dwFlags;

    if (Map == nullptr && CreatedVersion < 0x0700 && CreatedVersion != 0x05B2)
        Map = LegacyObjectToApp;
    Info->dwOfs = (Map != nullptr) ? Map[Target->Index] : Have->dwOfs;

    ZeroMemory(Info->tszName, Info->dwSize - FIELD_OFFSET(DIDEVICEOBJECTINSTANCEW, tszName));
    return Backend->GetObjectInfo(Target, Info);
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::EnumObjects(
    LPDIENUMDEVICEOBJECTSCALLBACKW Callback,
    LPVOID Context,
    DWORD Flags)
{
    RX_DI_PROP_TARGET Target = {};
    DIDEVICEOBJECTINSTANCEW Info;
    const DIOBJECTDATAFORMAT* Objects;
    ULONG ObjectCount;
    ULONG Excluded;
    ULONG Wanted;
    ULONG Collection;
    BOOLEAN UseLegacy;
    HRESULT hr = S_OK;

    if (Callback == nullptr || (Flags & DI_ENUM_INVALID_FLAGS) != 0)
        return E_INVALIDARG;

    Enter();
    if (Backend == &DiNilBackend)
    {
        Leave();
        return DIERR_NOTINITIALIZED;
    }
    ObjectCount = Native.dwNumObjs;
    Objects = Native.rgodf;
    Leave();

    /* Aliased and vendor defined objects only show up when asked for */
    if (CreatedVersion > 0x0300)
    {
        Excluded = 0;
        if (Flags & DIDFT_ALIAS)
            Flags &= ~DIDFT_ALIAS;
        else
            Excluded |= DIDFT_ALIAS;

        if (Flags & DIDFT_VENDORDEFINED)
            Flags &= ~DIDFT_VENDORDEFINED;
        else
            Excluded |= DIDFT_VENDORDEFINED;
    }
    else
    {
        Excluded = DIDFT_NODATA | DIDFT_COLLECTION;
    }

    Wanted = (Flags != 0) ? Flags : DI_ENUM_ALL_TYPES;
    if ((Wanted & 0x00FFFF00) == 0)
        Collection = 0xFFFF;
    else if (((Wanted >> 8) & 0xFFFF) == 0xFFFF)
        Collection = 0;
    else
        Collection = (Wanted >> 8) & 0xFFFF;

    UseLegacy = CreatedVersion < 0x0700 && CreatedVersion != 0x05B2 &&
                LegacyObjectToApp != nullptr && LegacyAppToObject != nullptr;

    for (ULONG Step = 0; ; Step++)
    {
        ULONG Index;
        ULONG Type;

        if (UseLegacy)
        {
            if (Step >= LegacyDataSize)
                break;
            Index = LegacyAppToObject[Step];
            if (Index == MAXULONG)
                continue;
        }
        else
        {
            if (Step >= ObjectCount)
                break;
            Index = Step;
        }

        Type = Objects[Index].dwType;
        if ((Type & Wanted & 0xFF) == 0 || (Type & Wanted & 0xFF000000) != (Wanted & 0xFF000000) ||
            (Type & Excluded) != 0)
        {
            continue;
        }

        Target.Index = Index;
        Target.Type = Type;
        ZeroMemory(&Info, sizeof(Info));
        Info.dwSize = sizeof(Info);
        hr = FillObjectInfo(&Target, &Info);
        if (FAILED(hr) || (Collection != 0xFFFF && Collection != Info.wCollectionNumber))
            return hr;

        if (!Callback(&Info, Context))
            break;
    }

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::EnumObjects(
    LPDIENUMDEVICEOBJECTSCALLBACKA Callback,
    LPVOID Context,
    DWORD Flags)
{
    RX_DI_FORWARD_A Forward;

    if (Callback == nullptr)
        return E_INVALIDARG;

    Forward.Callback = Callback;
    Forward.Context = Context;
    return EnumObjects(DiEnumObjectsToA, &Forward, Flags);
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetObjectInfo(
    LPDIDEVICEOBJECTINSTANCEW Info,
    DWORD Object,
    DWORD How)
{
    RX_DI_PROP_TARGET Target = {};
    HRESULT hr;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(DIDEVICEOBJECTINSTANCE_DX3W) && Info->dwSize != sizeof(DIDEVICEOBJECTINSTANCEW))
        return E_INVALIDARG;

    Enter();
    hr = MapHow(Object, How, &Target);
    if (SUCCEEDED(hr))
        hr = (How != DIPH_DEVICE) ? FillObjectInfo(&Target, Info) : E_INVALIDARG;
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetObjectInfo(
    LPDIDEVICEOBJECTINSTANCEA Info,
    DWORD Object,
    DWORD How)
{
    DIDEVICEOBJECTINSTANCEW Wide = {};
    HRESULT hr;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(DIDEVICEOBJECTINSTANCE_DX3A) && Info->dwSize != sizeof(DIDEVICEOBJECTINSTANCEA))
        return E_INVALIDARG;

    Wide.dwSize = sizeof(Wide);
    hr = GetObjectInfo(&Wide, Object, How);
    if (FAILED(hr))
        return hr;

    DiObjectInfoToA(Info, &Wide);
    return S_OK;
}

HRESULT
RxDiDevice::CanAcquire(VOID)
{
    if ((CoopFlags & DISCL_FOREGROUND) == 0)
        return S_OK;

    if (Window != GetForegroundWindow() || IsIconic(Window))
        return DIERR_OTHERAPPHASPRIO;

    if (!DiIsWindowOurs(Window))
    {
        Window = reinterpret_cast<HWND>(-1);
        return E_INVALIDARG;
    }
    return S_OK;
}

static
LRESULT
CALLBACK
DiForegroundProc(
    _In_ int Code,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    const CWPSTRUCT* Message = reinterpret_cast<const CWPSTRUCT*>(LParam);
    BOOL Minimized = FALSE;
    RxDiDevice** Devices;
    LONG Count;
    HHOOK Hook;

    switch (Message->message)
    {
        case WM_KILLFOCUS:
            DiLostFocus = TRUE;
            break;

        case WM_SIZE:
            Minimized = (Message->wParam == SIZE_MINIMIZED) && DiLostFocus;
            DiLostFocus = FALSE;
            break;

        case WM_SETFOCUS:
            DiLostFocus = FALSE;
            break;
    }

    if (Code != HC_ACTION || (Message->message != WM_ACTIVATE && !Minimized))
        return CallNextHookEx(DiForegroundHook, Code, WParam, LParam);

    /* The window is going away from the foreground, give every device up */
    EnterCriticalSection(&DiLock);
    Devices = DiForegroundDevices;
    Count = DiForegroundCount;
    DiForegroundDevices = nullptr;
    DiForegroundCount = 0;
    DiForegroundCapacity = 0;
    LeaveCriticalSection(&DiLock);

    if (Count != 0)
        DiTrace(L"window deactivated, unacquiring %ld foreground devices", Count);

    for (LONG Index = 0; Index < Count; Index++)
    {
        Devices[Index]->InternalUnacquire();
        Devices[Index]->Unhold();
    }
    DiFree(reinterpret_cast<PVOID*>(&Devices));

    Hook = DiForegroundHook;
    EnterCriticalSection(&DiLock);
    if (DiForegroundHook != nullptr)
    {
        UnhookWindowsHookEx(DiForegroundHook);
        DiForegroundHook = nullptr;
    }
    LeaveCriticalSection(&DiLock);

    return CallNextHookEx(Hook, 0, WParam, LParam);
}

HRESULT
RxDiDevice::WatchForeground(VOID)
{
    HRESULT hr = S_OK;

    if ((CoopFlags & DISCL_FOREGROUND) == 0)
        return S_OK;

    if (FAILED(CanAcquire()))
        return DIERR_OTHERAPPHASPRIO;

    EnterCriticalSection(&DiLock);
    if (DiForegroundCount >= DiForegroundCapacity)
    {
        LONG Grown = DiForegroundCapacity + 8;

        hr = DiAllocate(sizeof(RxDiDevice*) * Grown, reinterpret_cast<PVOID*>(&DiForegroundDevices));
        if (SUCCEEDED(hr))
            DiForegroundCapacity = Grown;
    }
    if (SUCCEEDED(hr))
        DiForegroundDevices[DiForegroundCount++] = this;
    Hold();
    LeaveCriticalSection(&DiLock);

    if (FAILED(hr))
        return hr;

    EnterCriticalSection(&DiLock);
    if (DiForegroundHook == nullptr)
    {
        DiForegroundHook = SetWindowsHookExW(WH_CALLWNDPROC, DiForegroundProc, DiModule,
                                             GetWindowThreadProcessId(Window, nullptr));
        if (DiForegroundHook == nullptr)
            DiTrace(L"foreground hook failed, error %lu", GetLastError());
    }
    LeaveCriticalSection(&DiLock);

    hr = CanAcquire();
    if (FAILED(hr))
        InternalUnacquire();
    return hr;
}

HRESULT
RxDiDevice::ReleaseBackendAcquire(VOID)
{
    HRESULT hr = Backend->Unacquire();

    if (hr != S_FALSE)
        return hr;

    if ((State & DI_DEV_SINK_ACQUIRED) == 0)
        return S_OK;

    State &= ~DI_DEV_SINK_ACQUIRED;
    if ((Sink->Flags & DI_SINK_EMULATED) == 0)
        return DIERR_UNSUPPORTED_SINK;
    return DiUnacquireSink(Sink, 0);
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::Acquire(VOID)
{
    HRESULT hr;

    Enter();
    LostResult = DIERR_NOTACQUIRED;

    if (ObjectToApp == nullptr || Sink == nullptr)
    {
        hr = E_INVALIDARG;
        goto Done;
    }

    if (Sink->Flags & DI_SINK_ACQUIRED)
    {
        hr = S_FALSE;
        goto Done;
    }

    hr = CanAcquire();
    if (FAILED(hr))
        goto Done;

    hr = DiAcquireExclusive(&InstanceGuid, Window, CoopFlags);
    if (FAILED(hr))
        goto Done;

    hr = AcquireForceFeedback();
    if (FAILED(hr))
        goto Done;

    hr = Backend->Acquire();
    if (FAILED(hr))
    {
        ReleaseBackendAcquire();
        goto Done;
    }

    /* S_FALSE asks for the emulation layer to carry the input */
    if (hr == S_FALSE)
    {
        hr = (Sink->Flags & DI_SINK_EMULATED) ? DiAcquireSink(Sink) : DIERR_UNSUPPORTED_SINK;
        if (FAILED(hr))
        {
            ReleaseBackendAcquire();
            goto Done;
        }

        State |= DI_DEV_SINK_ACQUIRED;
        if (Sink->Flags & DI_SINK_RELATIVE)
        {
            hr = Backend->GetDeviceState(Baseline);
            if (FAILED(hr))
            {
                ReleaseBackendAcquire();
                goto Done;
            }
        }
    }

    hr = WatchForeground();
    if (FAILED(hr))
    {
        ReleaseBackendAcquire();
        goto Done;
    }

    State |= DI_DEV_ACQUIRED | DI_DEV_WANTS_ACQUIRE;
    LostResult = DIERR_INPUTLOST;
    hr = S_OK;

Done:
    if (Hacks.SucceedAcquire && FAILED(hr))
    {
        hr = S_OK;
        State |= DI_DEV_WANTS_ACQUIRE;
    }
    Leave();
    return hr;
}

HRESULT
RxDiDevice::InternalUnacquire(VOID)
{
    HRESULT hr;

    Enter();
    if ((State & DI_DEV_ACQUIRED) == 0)
    {
        Leave();
        return S_FALSE;
    }

    State &= ~DI_DEV_ACQUIRED;
    DiReleaseExclusive(&InstanceGuid, Window, CoopFlags);

    if (CoopFlags & DISCL_FOREGROUND)
    {
        LONG Found = -1;

        EnterCriticalSection(&DiLock);
        for (LONG Index = 0; Index < DiForegroundCount; Index++)
        {
            if (DiForegroundDevices[Index] == this)
            {
                Found = Index;
                break;
            }
        }

        if (Found >= 0)
        {
            DiForegroundDevices[Found] = DiForegroundDevices[--DiForegroundCount];
            if (DiForegroundCount == 0)
            {
                DiFree(reinterpret_cast<PVOID*>(&DiForegroundDevices));
                DiForegroundCapacity = 0;
                if (DiForegroundHook != nullptr)
                {
                    UnhookWindowsHookEx(DiForegroundHook);
                    DiForegroundHook = nullptr;
                }
            }
        }
        LeaveCriticalSection(&DiLock);

        if (Found >= 0)
            Unhold();

        if (IsIconic(Window))
            State |= DI_DEV_ICONIC_LOST;
    }

    if (EffectDriver != nullptr && (CoopFlags & DISCL_EXCLUSIVE))
    {
        EffectDriver->SetForceFeedbackState(&DriverId, DISFFC_RESET);
        DriverId.Tag = 0;
    }

    hr = ReleaseBackendAcquire();
    if (LostResult == DIERR_INPUTLOST && Event != nullptr)
        SetEvent(Event);

    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::Unacquire(VOID)
{
    LostResult = DIERR_NOTACQUIRED;
    return InternalUnacquire();
}

VOID
RxDiDevice::RetryAcquire(VOID)
{
    if (Hacks.ReAcquire && (State & DI_DEV_ACQUIRED) == 0 && (State & (DI_DEV_WANTS_ACQUIRE | DI_DEV_LOST)) != 0)
        Acquire();

    if ((State & DI_DEV_ICONIC_LOST) && !IsIconic(Window) && SUCCEEDED(Acquire()))
        State &= ~DI_DEV_ICONIC_LOST;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetDeviceState(
    DWORD Size,
    LPVOID Target)
{
    HRESULT hr;

    Enter();
    RetryAcquire();

    if ((State & DI_DEV_ACQUIRED) == 0)
    {
        hr = LostResult;
    }
    else if (Size != AppDataSize)
    {
        hr = E_INVALIDARG;
    }
    else
    {
        hr = ReadState(Target);
        if (FAILED(hr))
        {
            if (hr == DIERR_INPUTLOST)
                InternalUnacquire();
        }
        else
        {
            /* Missing optional POVs read as centered */
            for (ULONG Index = 0; Index < PovFillCount; Index++)
                *reinterpret_cast<DWORD UNALIGNED*>(static_cast<PBYTE>(Target) + PovFill[Index]) = MAXDWORD;
            hr = S_OK;
        }
    }

    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetDeviceData(
    DWORD ObjectSize,
    LPDIDEVICEOBJECTDATA Data,
    LPDWORD Count,
    DWORD Flags)
{
    HRESULT hr = S_OK;

    Enter();

    if (Flags & ~(DIGDD_PEEK | DI_GDD_RESIDUAL))
    {
        hr = E_INVALIDARG;
        goto Done;
    }

    /* Only the DirectInput 3 record size is understood, anything else reads nothing */
    if (ObjectSize != sizeof(DIDEVICEOBJECTDATA_DX3))
        goto Done;

    if (BufferSize == 0)
    {
        hr = DIERR_NOTBUFFERED;
        goto Done;
    }

    if (Sink == nullptr)
    {
        hr = E_NOTIMPL;
        goto Done;
    }

    RetryAcquire();

    if (((State & DI_DEV_ACQUIRED) && (Sink->Flags & DI_SINK_ACQUIRED)) || (Flags & DI_GDD_RESIDUAL))
    {
        DIDEVICEOBJECTDATA_DX3* Out = reinterpret_cast<DIDEVICEOBJECTDATA_DX3*>(Data);
        DIDEVICEOBJECTDATA_DX3* Read = Sink->Tail;
        DIDEVICEOBJECTDATA_DX3* Write = Sink->Head;
        ULONG Room = (*Count < BufferSize) ? *Count : BufferSize;
        ULONG Copied = 0;

        /* The ring wraps at most once between the read and write positions */
        for (ULONG Pass = 0; Pass < 2; Pass++)
        {
            ULONG Available;
            ULONG Take;

            if (Pass == 0 && Read < Write)
                continue;

            Available = static_cast<ULONG>((Pass == 0 ? Sink->BufferEnd : Write) - Read);
            if (Pass == 0 && Read == Write)
                Available = 0;
            Take = (Available < Room) ? Available : Room;

            if (Out != nullptr)
            {
                CopyMemory(Out, Read, Take * sizeof(*Out));
                Out += Take;
            }
            Read += Take;
            Copied += Take;
            Room -= Take;
            if (Read == Sink->BufferEnd)
                Read = Sink->Buffer;
        }

        *Count = Copied;
        if ((Flags & DIGDD_PEEK) == 0)
            Sink->Tail = Read;

        if (Data != nullptr && Copied != 0 && (State & DI_DEV_COOKS_DATA))
        {
            DIDEVICEOBJECTDATA_DX3* Cooked = reinterpret_cast<DIDEVICEOBJECTDATA_DX3*>(Data);

            if ((Sink->Flags & DI_SINK_RELATIVE) == 0)
                Backend->CookDeviceData(Copied, Cooked);
            for (ULONG Index = 0; Index < Copied; Index++)
                Cooked[Index].dwOfs &= 0xFFFF;
        }

        hr = InterlockedExchange(&Sink->Overflow, 0);
    }
    else if ((State & DI_DEV_ACQUIRED) == 0 || (Sink->Flags & DI_SINK_ACQUIRED))
    {
        hr = LostResult;
    }
    else
    {
        hr = DIERR_INPUTLOST;
        InternalUnacquire();
    }

Done:
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::SetEventNotification(
    HANDLE NewEvent)
{
    HANDLE Copy = nullptr;
    HANDLE Discard;
    HRESULT hr;

    Enter();

    if (State & DI_DEV_ACQUIRED)
    {
        hr = DIERR_ACQUIRED;
        goto Done;
    }

    if (NewEvent != nullptr &&
        !DuplicateHandle(GetCurrentProcess(), NewEvent, GetCurrentProcess(), &Copy, EVENT_MODIFY_STATE, FALSE, 0))
    {
        hr = HRESULT_FROM_WIN32(GetLastError() & 0xFFFF);
        goto Done;
    }

    Discard = Copy;
    if (Copy != nullptr && !ResetEvent(Copy))
    {
        hr = E_HANDLE;
    }
    else if (Event != nullptr && Copy != nullptr)
    {
        hr = DIERR_HANDLEEXISTS;
    }
    else
    {
        hr = Backend->SetEventNotification(Copy);
        if (Sink != nullptr)
        {
            Discard = static_cast<HANDLE>(InterlockedExchangePointer(&Event, Copy));
            hr = PolledFlag;
        }
    }

    if (Discard != nullptr)
        CloseHandle(Discard);

Done:
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::SetCooperativeLevel(
    HWND NewWindow,
    DWORD Flags)
{
    DIDEVCAPS Caps;
    BOOL Allowed;
    HRESULT hr;

    Enter();

    if (State & DI_DEV_ACQUIRED)
    {
        hr = DIERR_ACQUIRED;
        goto Done;
    }

    /* Exactly one of exclusive and nonexclusive, and one of foreground and background */
    if ((Flags & ~0x1F) != 0 ||
        ((Flags & 3) != DISCL_EXCLUSIVE && (Flags & 3) != DISCL_NONEXCLUSIVE) ||
        ((Flags & 0xC) != DISCL_FOREGROUND && (Flags & 0xC) != DISCL_BACKGROUND))
    {
        hr = E_INVALIDARG;
        goto Done;
    }

    if (NewWindow != nullptr &&
        (!IsWindow(NewWindow) || (GetWindowLongW(NewWindow, GWL_STYLE) & WS_CHILD) != 0))
    {
        hr = E_HANDLE;
        goto Done;
    }

    if ((Flags & (DISCL_EXCLUSIVE | DISCL_FOREGROUND)) != 0 && NewWindow == nullptr)
    {
        hr = E_HANDLE;
        goto Done;
    }

    /* Before DirectInput 7 the Windows key and exclusive keyboards were off limits */
    Allowed = CreatedVersion >= 0x0700;
    if (!Allowed && (Flags & DISCL_NOWINKEY) == 0)
    {
        Allowed = TRUE;
        if (Flags & DISCL_EXCLUSIVE)
        {
            ZeroMemory(&Caps, sizeof(Caps));
            Caps.dwSize = sizeof(DIDEVCAPS_DX3);
            if (SUCCEEDED(Backend->GetCapabilities(&Caps)) && GET_DIDEVICE_TYPE(Caps.dwDevType) == DIDEVTYPE_KEYBOARD)
                Allowed = FALSE;
        }
    }

    if (!Allowed)
    {
        hr = E_INVALIDARG;
        goto Done;
    }

    hr = Backend->SetCooperativeLevel(NewWindow, Flags);
    if (SUCCEEDED(hr))
    {
        CoopFlags = Flags;
        Window = NewWindow;
        if (Sink != nullptr)
            Sink->Window = NewWindow;
        DiTrace(L"cooperative level 0x%lx on window %p", Flags, NewWindow);
    }

Done:
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::RunControlPanel(
    HWND Owner,
    DWORD Flags)
{
    RxDiBackend* Current;

    if (Owner != nullptr && !IsWindow(Owner))
        return E_HANDLE;
    if (Flags != 0)
        return E_INVALIDARG;

    Enter();
    Current = Backend;
    Leave();
    return Current->RunControlPanel(Owner, 0);
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::Poll(VOID)
{
    HRESULT hr;

    if (State & DI_DEV_POLLED_FORMAT)
    {
        Enter();
        hr = (State & DI_DEV_ACQUIRED) ? Backend->Poll() : LostResult;
        Leave();
        return hr;
    }

    /* Polling a device that needs none says so from DirectInput 5.2 on */
    if (State & DI_DEV_ACQUIRED)
        return (CreatedVersion >= 0x05B2) ? DI_NOEFFECT : DI_OK;
    return LostResult;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::SendDeviceData(
    DWORD ObjectSize,
    LPCDIDEVICEOBJECTDATA Data,
    LPDWORD Count,
    DWORD Flags)
{
    HRESULT hr = S_OK;

    Enter();

    if (Flags & ~DISDD_CONTINUE)
    {
        hr = E_INVALIDARG;
    }
    else if (ObjectSize == sizeof(DIDEVICEOBJECTDATA_DX3))
    {
        if (State & DI_DEV_ACQUIRED)
        {
            DIDEVICEOBJECTDATA_DX3* Records = const_cast<DIDEVICEOBJECTDATA_DX3*>(
                reinterpret_cast<const DIDEVICEOBJECTDATA_DX3*>(Data));

            /* The backend wants object ids where the application wrote offsets */
            for (ULONG Index = 0; Index < *Count; Index++)
            {
                ULONG Offset = Records[Index].dwOfs;
                ULONG Object = (Offset < AppDataSize) ? AppToObject[Offset] : MAXULONG;

                Records[Index].dwOfs = (Object < Native.dwNumObjs) ? Native.rgodf[Object].dwType : 0;
            }

            hr = Backend->SendDeviceData(Data, Count, Flags);
        }
        else
        {
            hr = LostResult;
        }
    }

    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::EnumEffects(
    LPDIENUMEFFECTSCALLBACKW Callback,
    LPVOID Context,
    DWORD Type)
{
    RX_DI_EFFECT_ENTRY* Copy = nullptr;
    ULONG Count;
    HRESULT hr;

    if (Callback == nullptr || (Type & DI_EFFECT_TYPE_INVALID) != 0)
        return E_INVALIDARG;

    Enter();
    Count = EffectCount;
    hr = DiAllocate(sizeof(*Copy) * Count, reinterpret_cast<PVOID*>(&Copy));
    if (SUCCEEDED(hr) && Count != 0)
        CopyMemory(Copy, EffectList, sizeof(*Copy) * Count);
    Leave();

    if (FAILED(hr))
        return hr;

    for (ULONG Index = 0; Index < Count; Index++)
    {
        DIEFFECTINFOW Info;

        if (Type != 0 && Type != DIEFT_GETTYPE(Copy[Index].EffectType))
            continue;

        ZeroMemory(&Info, sizeof(Info));
        Info.dwSize = sizeof(Info);
        Info.guid = Copy[Index].Guid;
        Info.dwEffType = Copy[Index].EffectType;
        Info.dwStaticParams = Copy[Index].StaticParams;
        Info.dwDynamicParams = Copy[Index].DynamicParams;
        CopyMemory(Info.tszName, Copy[Index].Name, sizeof(Info.tszName));
        if (!Callback(&Info, Context))
            break;
    }

    DiFree(reinterpret_cast<PVOID*>(&Copy));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::EnumEffects(
    LPDIENUMEFFECTSCALLBACKA Callback,
    LPVOID Context,
    DWORD Type)
{
    RX_DI_FORWARD_A Forward;

    if (Callback == nullptr)
        return E_INVALIDARG;

    Forward.Callback = Callback;
    Forward.Context = Context;
    return EnumEffects(DiEnumEffectsToA, &Forward, Type);
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetEffectInfo(
    LPDIEFFECTINFOW Info,
    REFGUID Guid)
{
    HRESULT hr = DIERR_DEVICENOTREG;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(DIEFFECTINFOW))
        return E_INVALIDARG;

    Enter();
    for (ULONG Index = 0; Index < EffectCount; Index++)
    {
        if (!IsEqualGUID(Guid, EffectList[Index].Guid))
            continue;

        Info->guid = EffectList[Index].Guid;
        Info->dwEffType = EffectList[Index].EffectType;
        Info->dwStaticParams = EffectList[Index].StaticParams;
        Info->dwDynamicParams = EffectList[Index].DynamicParams;
        CopyMemory(Info->tszName, EffectList[Index].Name, sizeof(Info->tszName));
        hr = S_OK;
        break;
    }
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetEffectInfo(
    LPDIEFFECTINFOA Info,
    REFGUID Guid)
{
    DIEFFECTINFOW Wide = {};
    HRESULT hr;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(DIEFFECTINFOA))
        return E_INVALIDARG;

    Wide.dwSize = sizeof(Wide);
    hr = GetEffectInfo(&Wide, Guid);
    if (FAILED(hr))
        return hr;

    Info->guid = Wide.guid;
    Info->dwEffType = Wide.dwEffType;
    Info->dwStaticParams = Wide.dwStaticParams;
    Info->dwDynamicParams = Wide.dwDynamicParams;
    WideCharToMultiByte(CP_ACP, 0, Wide.tszName, -1, Info->tszName, MAX_PATH, nullptr, nullptr);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::GetForceFeedbackState(
    LPDWORD Out)
{
    DIDEVICESTATE Status = {};
    HRESULT hr;

    if (Out == nullptr)
        return E_POINTER;

    *Out = 0;
    hr = CreateEffectDriver();
    if (FAILED(hr))
        return hr;

    Enter();
    Status.dwSize = sizeof(Status);
    hr = EffectDriver->GetForceFeedbackState(&DriverId, &Status);
    if (SUCCEEDED(hr))
    {
        *Out = Status.dwState;
        hr = S_OK;
    }
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::SendForceFeedbackCommand(
    DWORD Command)
{
    HRESULT hr;

    /* One command bit at a time */
    if (Command == 0 || (Command & ~DI_FF_COMMAND_MASK) != 0 || (Command & (Command - 1)) != 0)
        return E_INVALIDARG;

    hr = CreateEffectDriver();
    if (FAILED(hr))
        return hr;

    Enter();
    hr = EffectDriver->SetForceFeedbackState(&DriverId, Command);
    if (SUCCEEDED(hr) && (Command & DISFFC_RESET))
        RefreshGain();
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::EnumCreatedEffectObjects(
    LPDIENUMCREATEDEFFECTOBJECTSCALLBACK Callback,
    LPVOID Context,
    DWORD Flags)
{
    LPDIRECTINPUTEFFECT* Copy = nullptr;
    ULONG Count = 0;
    HRESULT hr;

    if (Callback == nullptr || Flags != 0)
        return E_INVALIDARG;

    Enter();
    hr = DiAllocate(sizeof(*Copy) * CreatedEffectCount, reinterpret_cast<PVOID*>(&Copy));
    if (SUCCEEDED(hr))
    {
        Count = CreatedEffectCount;
        for (ULONG Index = 0; Index < Count; Index++)
            Copy[Index] = DiHoldEffect(CreatedEffects[Index]);
    }
    Leave();

    if (SUCCEEDED(hr))
    {
        for (ULONG Index = 0; Index < Count; Index++)
        {
            if (!Callback(Copy[Index], Context))
                break;
        }
        for (ULONG Index = 0; Index < Count; Index++)
            DiUnholdEffect(Copy[Index]);
        DiFree(reinterpret_cast<PVOID*>(&Copy));
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::Escape(
    LPDIEFFESCAPE Request)
{
    HRESULT hr;

    if (Request == nullptr)
        return E_POINTER;
    if (Request->dwSize != sizeof(DIEFFESCAPE))
        return E_INVALIDARG;
    if ((Request->lpvInBuffer == nullptr && Request->cbInBuffer != 0) ||
        (Request->lpvOutBuffer == nullptr && Request->cbOutBuffer != 0))
    {
        return E_POINTER;
    }

    hr = CreateEffectDriver();
    if (FAILED(hr))
        return hr;

    Enter();
    hr = EffectDriver->DeviceEscape(&DriverId, Request);
    Leave();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::CreateEffect(
    REFGUID Guid,
    LPCDIEFFECT Params,
    LPDIRECTINPUTEFFECT* Effect,
    LPUNKNOWN OuterUnknown)
{
    HRESULT hr;

    if (Effect == nullptr)
        return E_POINTER;

    *Effect = nullptr;
    hr = CreateEffectDriver();
    *Effect = nullptr;
    if (FAILED(hr))
        return hr;

    return DiCreateEffect(this, Guid, Params, Effect, OuterUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::EnumEffectsInFile(
    LPCWSTR Name,
    LPDIENUMEFFECTSINFILECALLBACK Callback,
    LPVOID Context,
    DWORD Flags)
{
    CHAR Narrow[MAX_PATH];
    HRESULT hr;

    hr = DiCheckString(Name, 256);
    if (FAILED(hr))
        return hr;
    if (Callback == nullptr || (Flags & ~(DIFEF_INCLUDENONSTANDARD | DIFEF_MODIFYIFNEEDED)) != 0)
        return E_INVALIDARG;

    WideCharToMultiByte(CP_ACP, 0, Name, -1, Narrow, _countof(Narrow), nullptr, nullptr);
    return EnumEffectsInFile(Narrow, Callback, Context, Flags);
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::EnumEffectsInFile(
    LPCSTR Name,
    LPDIENUMEFFECTSINFILECALLBACK Callback,
    LPVOID Context,
    DWORD Flags)
{
    return DiEnumEffectsInFile(this, Name, Callback, Context, Flags);
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::WriteEffectToFile(
    LPCWSTR Name,
    DWORD Count,
    LPDIFILEEFFECT Effects,
    DWORD Flags)
{
    CHAR Narrow[MAX_PATH];
    HRESULT hr;

    hr = DiCheckString(Name, 256);
    if (FAILED(hr))
        return hr;

    WideCharToMultiByte(CP_ACP, 0, Name, -1, Narrow, _countof(Narrow), nullptr, nullptr);
    return WriteEffectToFile(Narrow, Count, Effects, Flags);
}

HRESULT
STDMETHODCALLTYPE
RxDiDevice::WriteEffectToFile(
    LPCSTR Name,
    DWORD Count,
    LPDIFILEEFFECT Effects,
    DWORD Flags)
{
    return DiWriteEffectToFile(this, Name, Count, Effects, Flags);
}

HRESULT
RxDiDevice::CreateEffectDriver(VOID)
{
    HRESULT hr = S_OK;

    Enter();
    if (EffectDriver == nullptr)
    {
        hr = Backend->CreateEffectDriver(&EffectDriver);
        if (SUCCEEDED(hr) && (State & DI_DEV_ACQUIRED))
            AcquireForceFeedback();
        if (SUCCEEDED(hr))
            hr = S_OK;
    }
    Leave();
    return hr;
}

HRESULT
RxDiDevice::AcquireForceFeedback(VOID)
{
    HRESULT hr;

    if (EffectDriver == nullptr || (CoopFlags & DISCL_EXCLUSIVE) == 0)
        return S_OK;

    hr = EffectDriver->SetForceFeedbackState(&DriverId, DI_FF_ACQUIRE);
    if (FAILED(hr))
        return hr;

    RefreshGain();
    if (AutoCenter == 0)
        EffectDriver->SetForceFeedbackState(&DriverId, DISFFC_STOPALL);
    return S_OK;
}

VOID
RxDiDevice::RefreshGain(VOID)
{
    if (EffectDriver != nullptr)
        EffectDriver->SetGain(&DriverId, Gain);
}
