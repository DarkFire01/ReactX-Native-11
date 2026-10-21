/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDXGIOutput: one display, its modes and its vertical blank
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxgiobjects.hpp"

#include <algorithm>
#include <cmath>

/* The kernel's mode formats DXGI can name; X formats are folded into their A twins first */
static const struct
{
    D3DDDIFORMAT Kernel;
    DXGI_FORMAT Dxgi;
} RxModeFormats[] =
{
    { D3DDDIFMT_A8R8G8B8, DXGI_FORMAT_B8G8R8A8_UNORM },
    { D3DDDIFMT_A8B8G8R8, DXGI_FORMAT_R8G8B8A8_UNORM },
    { D3DDDIFMT_A2B10G10R10, DXGI_FORMAT_R10G10B10A2_UNORM },
    { D3DDDIFMT_A16B16G16R16F, DXGI_FORMAT_R16G16B16A16_FLOAT },
    { D3DDDIFMT_A16B16G16R16, DXGI_FORMAT_R16G16B16A16_UNORM },
    { D3DDDIFMT_R5G6B5, DXGI_FORMAT_B5G6R5_UNORM },
    { D3DDDIFMT_A1R5G5B5, DXGI_FORMAT_B5G5R5A1_UNORM },
};

static
DXGI_FORMAT
RxModeFormat(
    _In_ D3DDDIFORMAT Format)
{
    switch (Format)
    {
        case D3DDDIFMT_X8R8G8B8:
            Format = D3DDDIFMT_A8R8G8B8;
            break;
        case D3DDDIFMT_X1R5G5B5:
            Format = D3DDDIFMT_A1R5G5B5;
            break;
        case D3DDDIFMT_X8B8G8R8:
            Format = D3DDDIFMT_A8B8G8R8;
            break;
        default:
            break;
    }

    for (const auto& Entry : RxModeFormats)
    {
        if (Entry.Kernel == Format)
            return Entry.Dxgi;
    }

    return DXGI_FORMAT_UNKNOWN;
}

/* Bits in the widest channel, which is what closeness between formats is measured in */
static
UINT
RxChannelBits(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            return 16;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return 10;
        case DXGI_FORMAT_B5G6R5_UNORM:
            return 6;
        case DXGI_FORMAT_B5G5R5A1_UNORM:
            return 5;
        default:
            return 8;
    }
}

/* The kernel's fixed output setting as a DXGI scaling value */
static
DXGI_MODE_SCALING
RxModeScaling(
    _In_ UINT FixedOutput)
{
    switch (FixedOutput)
    {
        case 1:
            return DXGI_MODE_SCALING_STRETCHED;
        case 2:
            return DXGI_MODE_SCALING_CENTERED;
        default:
            return DXGI_MODE_SCALING_UNSPECIFIED;
    }
}

static
BOOLEAN
RxModeLess(
    _In_ const DXGI_MODE_DESC1& First,
    _In_ const DXGI_MODE_DESC1& Second)
{
    if (First.Width != Second.Width)
        return First.Width < Second.Width;
    if (First.Height != Second.Height)
        return First.Height < Second.Height;
    if (First.Format != Second.Format)
        return First.Format < Second.Format;
    if (First.RefreshRate.Numerator != Second.RefreshRate.Numerator)
        return First.RefreshRate.Numerator < Second.RefreshRate.Numerator;
    if (First.RefreshRate.Denominator != Second.RefreshRate.Denominator)
        return First.RefreshRate.Denominator < Second.RefreshRate.Denominator;
    if (First.ScanlineOrdering != Second.ScanlineOrdering)
        return First.ScanlineOrdering < Second.ScanlineOrdering;
    if (First.Scaling != Second.Scaling)
        return First.Scaling < Second.Scaling;
    return First.Stereo < Second.Stereo;
}

static
BOOLEAN
RxModeEqual(
    _In_ const DXGI_MODE_DESC1& First,
    _In_ const DXGI_MODE_DESC1& Second)
{
    return !RxModeLess(First, Second) && !RxModeLess(Second, First);
}

/* The HMONITOR for a GDI device name, if that display is on the desktop */
static
HMONITOR
RxFindMonitor(
    _In_z_ PCWSTR DeviceName,
    _Out_opt_ RECT* Bounds)
{
    struct Search
    {
        PCWSTR Name;
        HMONITOR Found;
        RECT Bounds;
    } State = { DeviceName, NULL, {} };

    EnumDisplayMonitors(NULL, nullptr,
                        [](HMONITOR Monitor, HDC, LPRECT, LPARAM Parameter) -> BOOL
                        {
                            Search* Each = reinterpret_cast<Search*>(Parameter);
                            MONITORINFOEXW Info = {};

                            Info.cbSize = sizeof(Info);
                            if (!GetMonitorInfoW(Monitor, &Info) || _wcsicmp(Info.szDevice, Each->Name) != 0)
                                return TRUE;

                            Each->Found = Monitor;
                            Each->Bounds = Info.rcMonitor;
                            return FALSE;
                        },
                        reinterpret_cast<LPARAM>(&State));

    if (Bounds != nullptr)
        *Bounds = State.Bounds;
    return State.Found;
}

RxDxgiOutput::RxDxgiOutput(
    _In_ RxDxgiAdapter* Owner)
    : Adapter(Owner)
{
    Adapter->AddRef();
}

RxDxgiOutput::~RxDxgiOutput()
{
    Adapter->Release();
}

HRESULT
RxDxgiOutput::Create(
    _In_ RxDxgiAdapter* Adapter,
    _In_ const RxOutputInfo& Info,
    _Outptr_ RxDxgiOutput** Output)
{
    RxDxgiOutput* NewOutput;
    HRESULT hr;

    *Output = nullptr;

    NewOutput = new (std::nothrow) RxDxgiOutput(Adapter);
    if (NewOutput == nullptr)
        return E_OUTOFMEMORY;

    /* The kernel handle answers mode list and vertical blank queries for this source */
    NewOutput->Info = Info;
    hr = RxOpenKernelAdapter(Adapter->GetLuid(), &NewOutput->KmtAdapter);
    if (FAILED(hr))
    {
        NewOutput->Release();
        return hr;
    }

    *Output = NewOutput;
    return S_OK;
}

HRESULT
RxDxgiOutput::ListModes(
    _Out_ std::vector<DXGI_MODE_DESC1>& Modes)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_GETDISPLAYMODELIST Query = {};
    std::vector<D3DKMT_DISPLAYMODE> Kernel;
    DXGI_MODE_DESC1 Mode;
    DEVMODEW Desktop = {};
    D3DDDI_ROTATION Rotation;
    BOOLEAN IsCurrent;
    NTSTATUS Status;

    Modes.clear();
    if (Kmt->GetDisplayModeList == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Query.hAdapter = KmtAdapter;
    Query.VidPnSourceId = Info.VidPnSourceId;
    Status = Kmt->GetDisplayModeList(&Query);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    Kernel.resize(Query.ModeCount);
    Query.pModeList = Kernel.data();
    Status = Kmt->GetDisplayModeList(&Query);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);
    Kernel.resize(Query.ModeCount);

    /* Only modes for the way the display is turned right now count */
    Desktop.dmSize = sizeof(Desktop);
    EnumDisplaySettingsW(Info.DeviceName, ENUM_CURRENT_SETTINGS, &Desktop);
    Rotation = (D3DDDI_ROTATION)(Desktop.dmDisplayOrientation + D3DDDI_ROTATION_IDENTITY);

    for (const D3DKMT_DISPLAYMODE& Each : Kernel)
    {
        if (Each.DisplayOrientation != Rotation)
            continue;

        Mode = {};
        Mode.Format = RxModeFormat(Each.Format);
        if (Mode.Format == DXGI_FORMAT_UNKNOWN)
            continue;

        Mode.Width = Each.Width;
        Mode.Height = Each.Height;
        if (Rotation == D3DDDI_ROTATION_90 || Rotation == D3DDDI_ROTATION_270)
            std::swap(Mode.Width, Mode.Height);

        Mode.RefreshRate.Numerator = Each.RefreshRate.Numerator;
        Mode.RefreshRate.Denominator = Each.RefreshRate.Denominator;
        Mode.ScanlineOrdering = (Each.ScanLineOrdering >= D3DDDI_VSSLO_PROGRESSIVE &&
                                 Each.ScanLineOrdering <= D3DDDI_VSSLO_INTERLACED_LOWERFIELDFIRST)
                                    ? (DXGI_MODE_SCANLINE_ORDER)Each.ScanLineOrdering
                                    : DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        Mode.Scaling = RxModeScaling(Each.DisplayFixedOutput);
        Mode.Stereo = Each.Flags.Stereo;

        /* A mode the monitor never confirmed survives only as the desktop mode itself */
        if (!Each.Flags.ValidatedAgainstMonitorCaps)
        {
            IsCurrent = !Mode.Stereo && Mode.Width == Desktop.dmPelsWidth && Mode.Height == Desktop.dmPelsHeight &&
                        (UINT64)Mode.RefreshRate.Numerator == (UINT64)Desktop.dmDisplayFrequency *
                                                                  Mode.RefreshRate.Denominator;
            if (!IsCurrent)
                continue;
        }

        Modes.push_back(Mode);

        /* The eight bit formats are offered as sRGB too */
        if (Mode.Format == DXGI_FORMAT_R8G8B8A8_UNORM || Mode.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
        {
            Mode.Format = (Mode.Format == DXGI_FORMAT_R8G8B8A8_UNORM) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                                      : DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            Modes.push_back(Mode);
        }
    }

    std::sort(Modes.begin(), Modes.end(), RxModeLess);
    Modes.erase(std::unique(Modes.begin(), Modes.end(), RxModeEqual), Modes.end());
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIOutput) ||
        riid == __uuidof(IDXGIOutput1) || riid == __uuidof(IDXGIOutput2) || riid == __uuidof(IDXGIOutput3) ||
        riid == __uuidof(IDXGIOutput4) || riid == __uuidof(IDXGIOutput5) || riid == __uuidof(IDXGIOutput6))
    {
        *ppvObject = static_cast<IDXGIOutput6*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IDXGIOutputDWM))
    {
        *ppvObject = static_cast<IDXGIOutputDWM*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG
STDMETHODCALLTYPE
RxDxgiOutput::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

ULONG
STDMETHODCALLTYPE
RxDxgiOutput::Release(VOID)
{
    ULONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        delete this;
    return Count;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::SetPrivateData(
    _In_ REFGUID Name,
    _In_ UINT DataSize,
    _In_reads_bytes_(DataSize) const void* pData)
{
    return PrivateData.Set(Name, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::SetPrivateDataInterface(
    _In_ REFGUID Name,
    _In_opt_ const IUnknown* pUnknown)
{
    return PrivateData.Set(Name, 0, nullptr, pUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetPrivateData(
    _In_ REFGUID Name,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_(*pDataSize) void* pData)
{
    return PrivateData.Get(Name, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetParent(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppParent)
{
    if (ppParent == nullptr)
        return E_INVALIDARG;

    return Adapter->QueryInterface(riid, ppParent);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetDesc(
    _Out_ DXGI_OUTPUT_DESC* pDesc)
{
    DEVMODEW Current = {};

    if (pDesc == nullptr)
        return E_INVALIDARG;

    /* Placement and rotation are read live; only the name comes from the snapshot */
    *pDesc = {};
    wcscpy_s(pDesc->DeviceName, Info.DeviceName);
    pDesc->Monitor = RxFindMonitor(Info.DeviceName, &pDesc->DesktopCoordinates);

    Current.dmSize = sizeof(Current);
    if (EnumDisplaySettingsW(Info.DeviceName, ENUM_CURRENT_SETTINGS, &Current))
    {
        pDesc->AttachedToDesktop = (pDesc->Monitor != NULL);
        pDesc->Rotation = (DXGI_MODE_ROTATION)(Current.dmDisplayOrientation + DXGI_MODE_ROTATION_IDENTITY);
    }

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetDisplayModeList1(
    _In_ DXGI_FORMAT EnumFormat,
    _In_ UINT Flags,
    _Inout_ UINT* pNumModes,
    _Out_writes_to_opt_(*pNumModes, *pNumModes) DXGI_MODE_DESC1* pDesc)
{
    std::vector<DXGI_MODE_DESC1> Modes;
    std::vector<DXGI_MODE_DESC1> Wanted;
    UINT Index;
    HRESULT hr;

    if (pNumModes == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    hr = ListModes(Modes);
    if (FAILED(hr))
        return hr;

    /* Interlaced, stretched and stereo modes each need their flag */
    for (const DXGI_MODE_DESC1& Mode : Modes)
    {
        if (Mode.Format != EnumFormat)
            continue;
        if (Mode.ScanlineOrdering > DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE && !(Flags & DXGI_ENUM_MODES_INTERLACED))
            continue;
        if (Mode.Scaling > DXGI_MODE_SCALING_CENTERED && !(Flags & DXGI_ENUM_MODES_SCALING))
            continue;
        if (Mode.Stereo && !(Flags & (DXGI_ENUM_MODES_STEREO | DXGI_ENUM_MODES_DISABLED_STEREO)))
            continue;

        Wanted.push_back(Mode);
    }

    if (pDesc == nullptr)
    {
        *pNumModes = (UINT)Wanted.size();
        return S_OK;
    }

    /* A short buffer gets what fits and keeps the count it came with */
    for (Index = 0; Index < *pNumModes && Index < Wanted.size(); Index++)
        pDesc[Index] = Wanted[Index];
    if (*pNumModes < Wanted.size())
        return DXGI_ERROR_MORE_DATA;

    *pNumModes = (UINT)Wanted.size();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetDisplayModeList(
    _In_ DXGI_FORMAT EnumFormat,
    _In_ UINT Flags,
    _Inout_ UINT* pNumModes,
    _Out_writes_to_opt_(*pNumModes, *pNumModes) DXGI_MODE_DESC* pDesc)
{
    std::vector<DXGI_MODE_DESC1> Modes;
    UINT Index;
    HRESULT hr;

    /* Stereo needs the DXGI 1.2 description */
    if (pNumModes == nullptr || (Flags & DXGI_ENUM_MODES_STEREO))
        return DXGI_ERROR_INVALID_CALL;

    if (pDesc == nullptr)
        return GetDisplayModeList1(EnumFormat, Flags, pNumModes, nullptr);

    Modes.resize(*pNumModes);
    hr = GetDisplayModeList1(EnumFormat, Flags, pNumModes, Modes.data());
    if (FAILED(hr) && hr != DXGI_ERROR_MORE_DATA)
        return hr;

    for (Index = 0; Index < *pNumModes && Index < Modes.size(); Index++)
        memcpy(&pDesc[Index], &Modes[Index], sizeof(pDesc[Index]));
    return hr;
}

/* The criteria FindClosestMatchingMode settles one after another */
enum class RxModeCriterion
{
    Scanline,
    Scaling,
    Format,
    Resolution,
    Refresh
};

/* How far a candidate is from the target on one criterion; smaller is closer */
static
double
RxModeDistance(
    _In_ RxModeCriterion Criterion,
    _In_ const DXGI_MODE_DESC1& Target,
    _In_ const DXGI_MODE_DESC1& Candidate)
{
    static const UINT ProgressiveFirst[] = { 3, 0, 1, 2 };
    static const UINT UpperFirst[] = { 3, 2, 0, 1 };
    static const UINT LowerFirst[] = { 3, 2, 1, 0 };
    static const UINT StretchedFirst[] = { 2, 1, 0 };
    static const UINT CenteredFirst[] = { 2, 0, 1 };
    double Wanted;
    double Rate;
    UINT Have;
    UINT Need;

    switch (Criterion)
    {
        case RxModeCriterion::Scanline:
            if (Target.ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST)
                return UpperFirst[Candidate.ScanlineOrdering & 3];
            if (Target.ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST)
                return LowerFirst[Candidate.ScanlineOrdering & 3];
            return ProgressiveFirst[Candidate.ScanlineOrdering & 3];

        case RxModeCriterion::Scaling:
            if (Target.Scaling == DXGI_MODE_SCALING_CENTERED)
                return CenteredFirst[Candidate.Scaling % 3];
            return StretchedFirst[Candidate.Scaling % 3];

        case RxModeCriterion::Format:
            if (Candidate.Format == Target.Format)
                return 0;
            Have = RxChannelBits(Candidate.Format);
            Need = RxChannelBits(Target.Format);
            return (Have >= Need) ? 1.0 + (Have - Need) : 1000.0 + (Need - Have);

        case RxModeCriterion::Resolution:
            if (Candidate.Width == Target.Width && Candidate.Height == Target.Height)
                return 0;
            return 1.0 + std::fabs((double)Candidate.Width * Candidate.Height - (double)Target.Width * Target.Height);

        default:
            if (Candidate.RefreshRate.Numerator == 0 || Candidate.RefreshRate.Denominator == 0)
                return 1.0e30;
            Rate = (double)Candidate.RefreshRate.Numerator / Candidate.RefreshRate.Denominator;
            Wanted = (double)Target.RefreshRate.Numerator / Target.RefreshRate.Denominator;
            return std::fabs(Rate - Wanted);
    }
}

static
BOOLEAN
RxModeSameOn(
    _In_ RxModeCriterion Criterion,
    _In_ const DXGI_MODE_DESC1& First,
    _In_ const DXGI_MODE_DESC1& Second)
{
    switch (Criterion)
    {
        case RxModeCriterion::Scanline:
            return First.ScanlineOrdering == Second.ScanlineOrdering;
        case RxModeCriterion::Scaling:
            return First.Scaling == Second.Scaling;
        case RxModeCriterion::Format:
            return First.Format == Second.Format;
        case RxModeCriterion::Resolution:
            return First.Width == Second.Width && First.Height == Second.Height;
        default:
            return First.RefreshRate.Numerator == Second.RefreshRate.Numerator &&
                   First.RefreshRate.Denominator == Second.RefreshRate.Denominator;
    }
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::FindClosestMatchingMode1(
    _In_ const DXGI_MODE_DESC1* pModeToMatch,
    _Out_ DXGI_MODE_DESC1* pClosestMatch,
    _In_opt_ IUnknown* pConcernedDevice)
{
    static const RxModeCriterion Order[] = { RxModeCriterion::Scanline, RxModeCriterion::Scaling,
                                             RxModeCriterion::Format, RxModeCriterion::Resolution,
                                             RxModeCriterion::Refresh };
    std::vector<DXGI_MODE_DESC1> Modes;
    std::vector<DXGI_MODE_DESC1> Candidates;
    std::vector<RxModeCriterion> Steps;
    DXGI_MODE_DESC1 Target;
    DEVMODEW Desktop = {};
    BOOLEAN Specified[_countof(Order)];
    const DXGI_MODE_DESC1* Best;
    UINT Pass;
    UINT Index;
    HRESULT hr;

    if (pModeToMatch == nullptr || pClosestMatch == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    Target = *pModeToMatch;
    if ((Target.Width == 0) != (Target.Height == 0))
        return DXGI_ERROR_INVALID_CALL;
    if (Target.RefreshRate.Numerator == 0)
        Target.RefreshRate.Denominator = 1;
    else if (Target.RefreshRate.Denominator == 0)
        return DXGI_ERROR_INVALID_CALL;
    if (Target.ScanlineOrdering > DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST ||
        Target.Scaling > DXGI_MODE_SCALING_STRETCHED)
    {
        return DXGI_ERROR_INVALID_CALL;
    }
    if (Target.Format == DXGI_FORMAT_UNKNOWN && pConcernedDevice == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    hr = ListModes(Modes);
    if (FAILED(hr))
        return hr;

    for (const DXGI_MODE_DESC1& Mode : Modes)
    {
        if (Mode.Stereo != Target.Stereo)
            continue;
        if (Target.Format != DXGI_FORMAT_UNKNOWN && Mode.Format != Target.Format)
            continue;
        Candidates.push_back(Mode);
    }
    if (Candidates.empty())
        return DXGI_ERROR_NOT_FOUND;

    /* What the caller left zero comes from the registry desktop mode */
    Specified[0] = Target.ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    Specified[1] = Target.Scaling != DXGI_MODE_SCALING_UNSPECIFIED;
    Specified[2] = Target.Format != DXGI_FORMAT_UNKNOWN;
    Specified[3] = Target.Width != 0;
    Specified[4] = Target.RefreshRate.Numerator != 0;

    Desktop.dmSize = sizeof(Desktop);
    EnumDisplaySettingsW(Info.DeviceName, ENUM_REGISTRY_SETTINGS, &Desktop);
    if (!Specified[0])
        Target.ScanlineOrdering = (Desktop.dmDisplayFlags & DM_INTERLACED) ? DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST
                                                                           : DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    if (!Specified[1])
        Target.Scaling = RxModeScaling(Desktop.dmDisplayFixedOutput);
    if (!Specified[2])
        Target.Format = (Desktop.dmBitsPerPel == 16) ? DXGI_FORMAT_B5G6R5_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    if (!Specified[3])
    {
        Target.Width = Desktop.dmPelsWidth;
        Target.Height = Desktop.dmPelsHeight;
    }
    if (!Specified[4])
        Target.RefreshRate = { Desktop.dmDisplayFrequency, 1 };

    /* What the caller asked for is settled first, then the defaults, each in the fixed order */
    for (Pass = 0; Pass < 2; Pass++)
    {
        for (Index = 0; Index < _countof(Order); Index++)
        {
            if (Specified[Index] == (Pass == 0))
                Steps.push_back(Order[Index]);
        }
    }

    for (RxModeCriterion Step : Steps)
    {
        Best = &*std::min_element(Candidates.begin(), Candidates.end(),
                                  [&](const DXGI_MODE_DESC1& First, const DXGI_MODE_DESC1& Second)
                                  {
                                      return RxModeDistance(Step, Target, First) < RxModeDistance(Step, Target, Second);
                                  });

        /* Later criteria only choose among modes that agree with this pick */
        std::erase_if(Candidates,
                      [&, Kept = *Best](const DXGI_MODE_DESC1& Mode) { return !RxModeSameOn(Step, Mode, Kept); });
    }

    if (Candidates.empty() || Candidates[0].Format == DXGI_FORMAT_UNKNOWN || Candidates[0].Width == 0 ||
        Candidates[0].Height == 0)
    {
        return DXGI_ERROR_NOT_FOUND;
    }

    *pClosestMatch = Candidates[0];
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::FindClosestMatchingMode(
    _In_ const DXGI_MODE_DESC* pModeToMatch,
    _Out_ DXGI_MODE_DESC* pClosestMatch,
    _In_opt_ IUnknown* pConcernedDevice)
{
    DXGI_MODE_DESC1 Wanted = {};
    DXGI_MODE_DESC1 Found;
    HRESULT hr;

    if (pModeToMatch == nullptr || pClosestMatch == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    memcpy(&Wanted, pModeToMatch, sizeof(*pModeToMatch));
    hr = FindClosestMatchingMode1(&Wanted, &Found, pConcernedDevice);
    if (SUCCEEDED(hr))
        memcpy(pClosestMatch, &Found, sizeof(*pClosestMatch));
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::WaitForVBlank(VOID)
{
    D3DKMT_WAITFORVERTICALBLANKEVENT Wait = {};
    const KmtThunks* Kmt = RxGetKmtThunks();

    if (Kmt->WaitForVerticalBlankEvent == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Wait.hAdapter = KmtAdapter;
    Wait.VidPnSourceId = Info.VidPnSourceId;
    return RxHresultFromStatus(Kmt->WaitForVerticalBlankEvent(&Wait));
}
