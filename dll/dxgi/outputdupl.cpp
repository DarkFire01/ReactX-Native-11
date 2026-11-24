/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Desktop duplication of one output, fed by the kernel from the DWM's frames
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "outputdupl.hpp"

#include <algorithm>

/* The kernel writes a frame while it holds key 0 and hands it over by releasing key 1 */
constexpr UINT64 RxFrameFreeKey = 0;
constexpr UINT64 RxFrameReadyKey = 1;

/* Size the rectangle buffer of a format converting duplication starts with */
constexpr SIZE_T RxInitialCopyRectsSize = 160;

/* What the kernel's duplication calls report, in the terms DXGI gives callers */
static
HRESULT
RxDuplicationResult(
    _In_ NTSTATUS Status)
{
    if (Status == STATUS_SUCCESS)
        return S_OK;

    switch (Status)
    {
        case STATUS_INSTANCE_NOT_AVAILABLE:
            return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        case STATUS_ALREADY_DISCONNECTED:
            return DXGI_ERROR_SESSION_DISCONNECTED;
        case STATUS_NO_MEMORY:
            return E_OUTOFMEMORY;
        case STATUS_ACCESS_DENIED:
            return E_ACCESSDENIED;
        case STATUS_BUFFER_TOO_SMALL:
            return DXGI_ERROR_MORE_DATA;
        case STATUS_NOT_FOUND:
        case STATUS_DEVICE_REMOVED:
            return DXGI_ERROR_ACCESS_LOST;
        case STATUS_ALREADY_REGISTERED:
            return E_INVALIDARG;
        default:
            return E_UNEXPECTED;
    }
}

/* Creating a duplication reports a removed device as such rather than as a lost duplication */
static
HRESULT
RxCreateDuplicationResult(
    _In_ NTSTATUS Status)
{
    return Status == STATUS_DEVICE_REMOVED ? DXGI_ERROR_DEVICE_REMOVED : RxDuplicationResult(Status);
}

/* The typeless group a format copies within, for the formats a desktop can be in */
static
DXGI_FORMAT
RxCopyGroup(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_SINT:
            return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UINT:
            return DXGI_FORMAT_R10G10B10A2_TYPELESS;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
            return DXGI_FORMAT_R16G16B16A16_TYPELESS;
        default:
            return Format;
    }
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::DuplicateOutput(
    _In_ IUnknown* pDevice,
    _COM_Outptr_ IDXGIOutputDuplication** ppOutputDuplication)
{
    static const DXGI_FORMAT DesktopFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

    return DuplicateOutput1(pDevice, 0, 1, &DesktopFormat, ppOutputDuplication);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::DuplicateOutput1(
    _In_ IUnknown* pDevice,
    _In_ UINT Flags,
    _In_ UINT SupportedFormatsCount,
    _In_reads_(SupportedFormatsCount) const DXGI_FORMAT* pSupportedFormats,
    _COM_Outptr_ IDXGIOutputDuplication** ppOutputDuplication)
{
    DXGI_OUTDUPL_DESC Desc = {};
    DEVMODEW Current = {};
    RxOutputDuplication* Duplication;
    HRESULT hr;

    if (ppOutputDuplication == nullptr || pDevice == nullptr)
        return E_INVALIDARG;

    *ppOutputDuplication = nullptr;

    /* The mode is the desktop's as it is now; the DWM always composes it in BGRA */
    Current.dmSize = sizeof(Current);
    if (!EnumDisplaySettingsW(Info.DeviceName, ENUM_CURRENT_SETTINGS, &Current))
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    Desc.ModeDesc.Width = Current.dmPelsWidth;
    Desc.ModeDesc.Height = Current.dmPelsHeight;
    Desc.ModeDesc.RefreshRate.Numerator = Current.dmDisplayFrequency > 1 ? Current.dmDisplayFrequency : 60;
    Desc.ModeDesc.RefreshRate.Denominator = 1;
    Desc.ModeDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.ModeDesc.ScanlineOrdering = (Current.dmDisplayFlags & DM_INTERLACED)
                                         ? DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST
                                         : DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    Desc.ModeDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    Desc.Rotation = (DXGI_MODE_ROTATION)(Current.dmDisplayOrientation + DXGI_MODE_ROTATION_IDENTITY);
    Desc.DesktopImageInSystemMemory = FALSE;

    hr = RxOutputDuplication::Create(this, pDevice, Desc, Flags, SupportedFormatsCount, pSupportedFormats,
                                     &Duplication);
    if (FAILED(hr))
    {
        RX_TRACE(L"DuplicateOutput on source %u failed 0x%08lX", Info.VidPnSourceId, hr);
        return hr;
    }

    *ppOutputDuplication = Duplication;
    return S_OK;
}

HRESULT
RxOutputDuplication::Create(
    _In_ RxDxgiOutput* Output,
    _In_ IUnknown* Device,
    _In_ const DXGI_OUTDUPL_DESC& Desc,
    _In_ UINT Flags,
    _In_ UINT FormatCount,
    _In_reads_(FormatCount) const DXGI_FORMAT* Formats,
    _Outptr_ RxOutputDuplication** Duplication)
{
    RxOutputDuplication* NewDuplication;
    HRESULT hr;

    *Duplication = nullptr;

    NewDuplication = new (std::nothrow) RxOutputDuplication(Output);
    if (NewDuplication == nullptr)
        return E_OUTOFMEMORY;

    NewDuplication->Desc = Desc;
    hr = NewDuplication->Initialize(Device, Flags, FormatCount, Formats);
    if (FAILED(hr))
    {
        NewDuplication->Release();
        return hr;
    }

    RX_TRACE(L"Duplicating source %u: %ux%u, %u frame textures%s", Output->GetVidPnSourceId(),
             Desc.ModeDesc.Width, Desc.ModeDesc.Height, NewDuplication->FrameCount,
             NewDuplication->CopyTarget.Get() != nullptr ? L", copied out" : L"");
    *Duplication = NewDuplication;
    return S_OK;
}

RxOutputDuplication::RxOutputDuplication(
    _In_ RxDxgiOutput* Owner)
    : Output(Owner)
{
    Output->AddRef();
}

RxOutputDuplication::~RxOutputDuplication()
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_DESTROY_OUTPUTDUPL Destroy = {};

    /* The kernel lets go of the textures before the device does */
    if (IsRegistered)
    {
        Destroy.hAdapter = Output->GetKernelAdapter();
        Destroy.VidPnSourceId = Output->GetVidPnSourceId();
        Destroy.bDestroyAllContexts = FALSE;
        Kmt->DestroyOutputDupl(&Destroy);
    }

    for (HANDLE& Handle : FrameHandles)
    {
        if (Handle != NULL)
            CloseHandle(Handle);
        Handle = NULL;
    }

    Output->Release();
}

HRESULT
RxOutputDuplication::Initialize(
    _In_ IUnknown* CallerDevice,
    _In_ UINT Flags,
    _In_ UINT FormatCount,
    _In_reads_(FormatCount) const DXGI_FORMAT* Formats)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CREATE_OUTPUTDUPL Register = {};
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    RxComPtr<IDXGIAdapter> CallerAdapter;
    DXGI_ADAPTER_DESC AdapterDesc = {};
    const LUID& OutputLuid = Output->GetAdapterLuid();
    BOOLEAN NeedsCopy;
    NTSTATUS Status;
    UINT Index;
    HRESULT hr;

    if ((Flags & ~DXGI_OUTDUPL_COMPOSITED_UI_CAPTURE_ONLY) != 0)
        return E_INVALIDARG;
    if (FormatCount == 0 || Formats == nullptr)
        return E_INVALIDARG;

    hr = CallerDevice->QueryInterface(__uuidof(ID3D11Device), Device.PutVoid());
    if (FAILED(hr))
        return hr;
    hr = CallerDevice->QueryInterface(__uuidof(IDXGIDevice2), DxgiDevice.PutVoid());
    if (FAILED(hr))
        return hr;

    /* The kernel copies on the output's adapter, so the frame textures have to live there */
    hr = DxgiDevice->GetAdapter(CallerAdapter.Put());
    if (SUCCEEDED(hr))
        hr = CallerAdapter->GetDesc(&AdapterDesc);
    if (FAILED(hr))
        return hr;
    if (AdapterDesc.AdapterLuid.LowPart != OutputLuid.LowPart || AdapterDesc.AdapterLuid.HighPart != OutputLuid.HighPart)
        return E_INVALIDARG;

    if (Kmt->CreateOutputDupl == nullptr || Kmt->DestroyOutputDupl == nullptr || Kmt->OutputDuplGetFrameInfo == nullptr ||
        Kmt->OutputDuplGetMetaData == nullptr || Kmt->OutputDuplGetPointerShapeData == nullptr ||
        Kmt->OutputDuplReleaseFrame == nullptr)
    {
        return DXGI_ERROR_UNSUPPORTED;
    }

    Device->GetImmediateContext(Context.Put());
    Context->QueryInterface(__uuidof(ID3D11Multithread), DeviceLock.PutVoid());

    /* Without textures the kernel only says how many it rotates through, one for a DWM owned source */
    Register.hAdapter = Output->GetKernelAdapter();
    Register.VidPnSourceId = Output->GetVidPnSourceId();
    Status = Kmt->CreateOutputDupl(&Register);
    hr = RxCreateDuplicationResult(Status);
    if (FAILED(hr))
    {
        RX_TRACE(L"D3DKMTCreateOutputDupl query on source %u failed 0x%08lX", Register.VidPnSourceId, Status);
        return hr;
    }
    if (Register.RequiredKeyedMutexCount == 0 || Register.RequiredKeyedMutexCount > Frames.size())
    {
        RX_TRACE(L"D3DKMTCreateOutputDupl asked for %u textures", Register.RequiredKeyedMutexCount);
        return E_UNEXPECTED;
    }
    FrameCount = Register.RequiredKeyedMutexCount;

    /* Frames are scanout shaped: a turned display keeps its unturned size */
    if (Desc.Rotation == DXGI_MODE_ROTATION_ROTATE90 || Desc.Rotation == DXGI_MODE_ROTATION_ROTATE270)
    {
        TextureDesc.Width = Desc.ModeDesc.Height;
        TextureDesc.Height = Desc.ModeDesc.Width;
    }
    else
    {
        TextureDesc.Width = Desc.ModeDesc.Width;
        TextureDesc.Height = Desc.ModeDesc.Height;
    }
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.Format = Desc.ModeDesc.Format;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    TextureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    /* A caller that cannot take the desktop format, or wants only composed UI, reads a private copy */
    NeedsCopy = (Flags & DXGI_OUTDUPL_COMPOSITED_UI_CAPTURE_ONLY) ||
                std::find(Formats, Formats + FormatCount, Desc.ModeDesc.Format) == Formats + FormatCount;

    for (Index = 0; Index < FrameCount; Index++)
    {
        hr = CreateFrameTexture(TextureDesc, Index);
        if (FAILED(hr))
        {
            RX_TRACE(L"Duplication frame texture %u failed 0x%08lX", Index, hr);
            return hr;
        }
        Register.KeyedMutexs[Index].hSharedSurfaceNt = FrameHandles[Index];
    }

    if (NeedsCopy)
    {
        hr = CreateCopyTarget(TextureDesc, FormatCount, Formats);
        if (FAILED(hr))
            return hr;
    }

    Register.KeyedMutexCount = FrameCount;
    Register.Flags.CompositionUiCaptureOnly = (Flags & DXGI_OUTDUPL_COMPOSITED_UI_CAPTURE_ONLY) ? 1 : 0;
    Status = Kmt->CreateOutputDupl(&Register);
    hr = RxCreateDuplicationResult(Status);
    if (FAILED(hr))
    {
        RX_TRACE(L"D3DKMTCreateOutputDupl on source %u with %u textures failed 0x%08lX", Register.VidPnSourceId,
                 FrameCount, Status);
        return hr;
    }

    IsRegistered = TRUE;
    return S_OK;
}

HRESULT
RxOutputDuplication::CreateFrameTexture(
    _In_ const D3D11_TEXTURE2D_DESC& TextureDesc,
    _In_ UINT Index)
{
    RxComPtr<IDXGIResource1> Shared;
    HRESULT hr;

    hr = Device->CreateTexture2D(&TextureDesc, nullptr, Frames[Index].Put());
    if (FAILED(hr))
        return hr;

    hr = Frames[Index]->QueryInterface(__uuidof(IDXGIKeyedMutex), FrameMutexes[Index].PutVoid());
    if (FAILED(hr))
        return hr;

    /* The handle carries the keyed mutex and its fence along with the texture */
    hr = Frames[Index]->QueryInterface(__uuidof(IDXGIResource1), Shared.PutVoid());
    if (FAILED(hr))
        return hr;

    return Shared->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &FrameHandles[Index]);
}

HRESULT
RxOutputDuplication::CreateCopyTarget(
    _In_ const D3D11_TEXTURE2D_DESC& TextureDesc,
    _In_ UINT FormatCount,
    _In_reads_(FormatCount) const DXGI_FORMAT* Formats)
{
    D3D11_TEXTURE2D_DESC CopyDesc = TextureDesc;
    DXGI_FORMAT Chosen = DXGI_FORMAT_UNKNOWN;
    UINT Support;
    UINT Index;
    HRESULT hr;

    /* The first format offered that the display could show is the one handed out */
    for (Index = 0; Index < FormatCount && Chosen == DXGI_FORMAT_UNKNOWN; Index++)
    {
        Support = 0;
        hr = Device->CheckFormatSupport(Formats[Index], &Support);
        if (FAILED(hr))
            return hr;
        if (Support & D3D11_FORMAT_SUPPORT_DISPLAY)
            Chosen = Formats[Index];
    }
    if (Chosen == DXGI_FORMAT_UNKNOWN)
        return E_INVALIDARG;

    /* Frames are brought over by copies, which cannot change how the bits are laid out */
    if (RxCopyGroup(Chosen) != RxCopyGroup(TextureDesc.Format))
    {
        RX_TRACE(L"Duplication cannot convert format %u to %u", TextureDesc.Format, Chosen);
        return DXGI_ERROR_UNSUPPORTED;
    }

    CopyDesc.Format = Chosen;
    CopyDesc.MiscFlags = 0;
    CopyRects.resize(RxInitialCopyRectsSize);
    return Device->CreateTexture2D(&CopyDesc, nullptr, CopyTarget.Put());
}

HRESULT
RxOutputDuplication::QueryMetadata(
    _In_ D3DKMT_OUTPUTDUPL_METADATATYPE Type,
    _In_ UINT BufferSize,
    _Out_writes_bytes_to_(BufferSize, *SizeRequired) VOID* Buffer,
    _Out_ UINT* SizeRequired)
{
    D3DKMT_OUTPUTDUPL_METADATA Query = {};
    NTSTATUS Status;

    Query.hAdapter = Output->GetKernelAdapter();
    Query.VidPnSourceId = Output->GetVidPnSourceId();
    Query.Type = Type;
    Query.BufferSizeSupplied = BufferSize;
    Query.pBuffer = Buffer;
    Status = RxGetKmtThunks()->OutputDuplGetMetaData(&Query);

    /* A short buffer still learns the size it needs */
    *SizeRequired = Query.BufferSizeRequired;
    return RxDuplicationResult(Status);
}

HRESULT
RxOutputDuplication::UpdateCopyTarget(
    _In_ ID3D11Texture2D* Frame)
{
    static const D3DKMT_OUTPUTDUPL_METADATATYPE Kinds[] = { D3DKMT_OUTPUTDUPL_METADATATYPE_MOVE_RECTS,
                                                            D3DKMT_OUTPUTDUPL_METADATATYPE_DIRTY_RECTS };
    D3D11_TEXTURE2D_DESC FrameDesc;
    const RECT* Area;
    D3D11_BOX Box;
    RECT Clipped;
    RECT Bounds;
    UINT Required;
    UINT Stride;
    UINT Index;
    HRESULT hr;

    Frame->GetDesc(&FrameDesc);
    Bounds = { 0, 0, (LONG)FrameDesc.Width, (LONG)FrameDesc.Height };

    /* Moved areas first, then dirty ones, each copied to where it sits in the new frame */
    for (D3DKMT_OUTPUTDUPL_METADATATYPE Kind : Kinds)
    {
        do
        {
            hr = QueryMetadata(Kind, (UINT)CopyRects.size(), CopyRects.data(), &Required);
            if (hr == DXGI_ERROR_MORE_DATA)
                CopyRects.resize(Required);
        } while (hr == DXGI_ERROR_MORE_DATA);
        if (FAILED(hr))
            return hr;

        Stride = (Kind == D3DKMT_OUTPUTDUPL_METADATATYPE_MOVE_RECTS) ? (UINT)sizeof(DXGI_OUTDUPL_MOVE_RECT)
                                                                     : (UINT)sizeof(RECT);
        for (Index = 0; Index < Required / Stride; Index++)
        {
            if (Kind == D3DKMT_OUTPUTDUPL_METADATATYPE_MOVE_RECTS)
                Area = &reinterpret_cast<const DXGI_OUTDUPL_MOVE_RECT*>(CopyRects.data())[Index].DestinationRect;
            else
                Area = &reinterpret_cast<const RECT*>(CopyRects.data())[Index];

            if (!IntersectRect(&Clipped, Area, &Bounds))
                continue;

            Box = { (UINT)Clipped.left, (UINT)Clipped.top, 0, (UINT)Clipped.right, (UINT)Clipped.bottom, 1 };
            Context->CopySubresourceRegion(CopyTarget.Get(), 0, Box.left, Box.top, 0, Frame, 0, &Box);
        }
    }

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIOutputDuplication))
    {
        *ppvObject = static_cast<IDXGIOutputDuplication*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG
STDMETHODCALLTYPE
RxOutputDuplication::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

ULONG
STDMETHODCALLTYPE
RxOutputDuplication::Release(VOID)
{
    ULONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        delete this;
    return Count;
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::SetPrivateData(
    _In_ REFGUID Name,
    _In_ UINT DataSize,
    _In_reads_bytes_(DataSize) const void* pData)
{
    return PrivateData.Set(Name, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::SetPrivateDataInterface(
    _In_ REFGUID Name,
    _In_opt_ const IUnknown* pUnknown)
{
    return PrivateData.Set(Name, 0, nullptr, pUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::GetPrivateData(
    _In_ REFGUID Name,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_(*pDataSize) void* pData)
{
    return PrivateData.Get(Name, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::GetParent(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppParent)
{
    if (ppParent == nullptr)
        return E_INVALIDARG;

    return Output->QueryInterface(riid, ppParent);
}

void
STDMETHODCALLTYPE
RxOutputDuplication::GetDesc(
    _Out_ DXGI_OUTDUPL_DESC* pDesc)
{
    if (pDesc != nullptr)
        *pDesc = Desc;
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::AcquireNextFrame(
    _In_ UINT TimeoutInMilliseconds,
    _Out_ DXGI_OUTDUPL_FRAME_INFO* pFrameInfo,
    _COM_Outptr_ IDXGIResource** ppDesktopResource)
{
    IDXGIKeyedMutex* Mutex;
    UINT Index;
    HRESULT hr;

    AcquireSRWLockExclusive(&Lock);
    if (IsFrameHeld || IsAcquiring)
        hr = DXGI_ERROR_INVALID_CALL;
    else if (pFrameInfo == nullptr || ppDesktopResource == nullptr)
        hr = E_INVALIDARG;
    else
        hr = S_OK;

    if (FAILED(hr))
    {
        ReleaseSRWLockExclusive(&Lock);
        return hr;
    }

    /* The wait runs unlocked, so the duplication still answers while it blocks */
    IsAcquiring = TRUE;
    Index = CurrentFrame;
    Mutex = FrameMutexes[Index].Get();
    ReleaseSRWLockExclusive(&Lock);

    hr = Mutex->AcquireSync(RxFrameReadyKey, TimeoutInMilliseconds);

    if (DeviceLock.Get() != nullptr)
        DeviceLock->Enter();
    AcquireSRWLockExclusive(&Lock);
    IsAcquiring = FALSE;
    hr = TakeFrame(hr, Index, pFrameInfo, ppDesktopResource);
    ReleaseSRWLockExclusive(&Lock);
    if (DeviceLock.Get() != nullptr)
        DeviceLock->Leave();

    return hr;
}

HRESULT
RxOutputDuplication::TakeFrame(
    _In_ HRESULT WaitResult,
    _In_ UINT Index,
    _Out_ DXGI_OUTDUPL_FRAME_INFO* FrameInfo,
    _COM_Outptr_ IDXGIResource** DesktopResource)
{
    D3DKMT_OUTPUTDUPL_GET_FRAMEINFO Query = {};
    const D3DKMT_OUTPUTDUPL_FRAMEINFO& Kernel = Query.FrameInfo;
    HRESULT hr;

    if (WaitResult == (HRESULT)WAIT_TIMEOUT)
        return DXGI_ERROR_WAIT_TIMEOUT;
    if (WaitResult == (HRESULT)WAIT_ABANDONED)
        return DXGI_ERROR_ACCESS_LOST;
    if (FAILED(WaitResult))
        return WaitResult;

    Query.hAdapter = Output->GetKernelAdapter();
    Query.VidPnSourceId = Output->GetVidPnSourceId();
    hr = RxDuplicationResult(RxGetKmtThunks()->OutputDuplGetFrameInfo(&Query));
    if (SUCCEEDED(hr))
    {
        *DesktopResource = nullptr;

        /* A frame that only moved the pointer has nothing new to copy, so the frame texture itself goes out */
        if (CopyTarget.Get() != nullptr && Kernel.AccumulatedFrames != 0)
        {
            hr = UpdateCopyTarget(Frames[Index].Get());
            if (SUCCEEDED(hr))
                hr = CopyTarget->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(DesktopResource));
        }
        else
        {
            hr = Frames[Index]->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(DesktopResource));
        }
    }

    /* The texture goes back at once, so the kernel's next frame is not held up behind a failed acquire */
    if (FAILED(hr))
    {
        RX_TRACE(L"AcquireNextFrame on source %u failed 0x%08lX", Output->GetVidPnSourceId(), hr);
        FrameMutexes[Index]->ReleaseSync(RxFrameFreeKey);
        return hr;
    }

    FrameInfo->LastPresentTime = Kernel.LastPresentTime;
    FrameInfo->LastMouseUpdateTime = Kernel.LastMouseUpdateTime;
    FrameInfo->AccumulatedFrames = Kernel.AccumulatedFrames;
    FrameInfo->RectsCoalesced = Kernel.RectsCoalesced;
    FrameInfo->ProtectedContentMaskedOut = Kernel.ProtectedContentMaskedOut;
    FrameInfo->PointerPosition.Position = Kernel.PointerPosition.Position;
    FrameInfo->PointerPosition.Visible = Kernel.PointerPosition.Visible;
    FrameInfo->TotalMetadataBufferSize = Kernel.TotalMetadataBufferSize;
    FrameInfo->PointerShapeBufferSize = Kernel.PointerShapeBufferSize;
    IsFrameHeld = TRUE;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::GetFrameDirtyRects(
    _In_ UINT DirtyRectsBufferSize,
    _Out_writes_bytes_to_(DirtyRectsBufferSize, *pDirtyRectsBufferSizeRequired) RECT* pDirtyRectsBuffer,
    _Out_ UINT* pDirtyRectsBufferSizeRequired)
{
    HRESULT hr;

    AcquireSRWLockExclusive(&Lock);
    if (!IsFrameHeld)
        hr = DXGI_ERROR_INVALID_CALL;
    else if (pDirtyRectsBuffer == nullptr || pDirtyRectsBufferSizeRequired == nullptr)
        hr = E_INVALIDARG;
    else
        hr = QueryMetadata(D3DKMT_OUTPUTDUPL_METADATATYPE_DIRTY_RECTS, DirtyRectsBufferSize, pDirtyRectsBuffer,
                           pDirtyRectsBufferSizeRequired);
    ReleaseSRWLockExclusive(&Lock);

    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::GetFrameMoveRects(
    _In_ UINT MoveRectsBufferSize,
    _Out_writes_bytes_to_(MoveRectsBufferSize, *pMoveRectsBufferSizeRequired) DXGI_OUTDUPL_MOVE_RECT* pMoveRectBuffer,
    _Out_ UINT* pMoveRectsBufferSizeRequired)
{
    HRESULT hr;

    AcquireSRWLockExclusive(&Lock);
    if (!IsFrameHeld)
        hr = DXGI_ERROR_INVALID_CALL;
    else if (pMoveRectBuffer == nullptr || pMoveRectsBufferSizeRequired == nullptr)
        hr = E_INVALIDARG;
    else
        hr = QueryMetadata(D3DKMT_OUTPUTDUPL_METADATATYPE_MOVE_RECTS, MoveRectsBufferSize, pMoveRectBuffer,
                           pMoveRectsBufferSizeRequired);
    ReleaseSRWLockExclusive(&Lock);

    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::GetFramePointerShape(
    _In_ UINT PointerShapeBufferSize,
    _Out_writes_bytes_to_(PointerShapeBufferSize, *pPointerShapeBufferSizeRequired) void* pPointerShapeBuffer,
    _Out_ UINT* pPointerShapeBufferSizeRequired,
    _Out_ DXGI_OUTDUPL_POINTER_SHAPE_INFO* pPointerShapeInfo)
{
    D3DKMT_OUTPUTDUPL_GET_POINTER_SHAPE_DATA Query = {};
    HRESULT hr;

    AcquireSRWLockExclusive(&Lock);
    if (!IsFrameHeld)
    {
        ReleaseSRWLockExclusive(&Lock);
        return DXGI_ERROR_INVALID_CALL;
    }
    if (pPointerShapeBuffer == nullptr || pPointerShapeBufferSizeRequired == nullptr || pPointerShapeInfo == nullptr)
    {
        ReleaseSRWLockExclusive(&Lock);
        return E_INVALIDARG;
    }

    Query.hAdapter = Output->GetKernelAdapter();
    Query.VidPnSourceId = Output->GetVidPnSourceId();
    Query.BufferSizeSupplied = PointerShapeBufferSize;
    Query.pShapeBuffer = pPointerShapeBuffer;
    hr = RxDuplicationResult(RxGetKmtThunks()->OutputDuplGetPointerShapeData(&Query));
    *pPointerShapeBufferSizeRequired = Query.BufferSizeRequired;
    if (SUCCEEDED(hr))
    {
        pPointerShapeInfo->Type = Query.ShapeInfo.Type;
        pPointerShapeInfo->Width = Query.ShapeInfo.Width;
        pPointerShapeInfo->Height = Query.ShapeInfo.Height;
        pPointerShapeInfo->Pitch = Query.ShapeInfo.Pitch;
        pPointerShapeInfo->HotSpot = Query.ShapeInfo.HotSpot;
    }
    ReleaseSRWLockExclusive(&Lock);

    return hr;
}

/* Frames always stay in video memory, so there is never a system memory image to map */
HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::MapDesktopSurface(
    _Out_ DXGI_MAPPED_RECT* pLockedRect)
{
    if (pLockedRect == nullptr)
        return E_INVALIDARG;

    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::UnMapDesktopSurface(VOID)
{
    return DXGI_ERROR_INVALID_CALL;
}

HRESULT
STDMETHODCALLTYPE
RxOutputDuplication::ReleaseFrame(VOID)
{
    D3DKMT_OUTPUTDUPL_RELEASE_FRAME Release = {};
    HRESULT hr;

    AcquireSRWLockExclusive(&Lock);
    if (!IsFrameHeld)
    {
        ReleaseSRWLockExclusive(&Lock);
        return DXGI_ERROR_INVALID_CALL;
    }

    /* Key 0 lets the kernel write the next frame; the kernel then names the texture it will use */
    hr = FrameMutexes[CurrentFrame]->ReleaseSync(RxFrameFreeKey);
    if (hr == D3DDDIERR_DEVICEREMOVED || hr == DXGI_ERROR_DEVICE_REMOVED)
        hr = DXGI_ERROR_ACCESS_LOST;
    if (SUCCEEDED(hr))
    {
        Release.hAdapter = Output->GetKernelAdapter();
        Release.VidPnSourceId = Output->GetVidPnSourceId();
        hr = RxDuplicationResult(RxGetKmtThunks()->OutputDuplReleaseFrame(&Release));
    }
    if (SUCCEEDED(hr))
    {
        CurrentFrame = Release.NextKeyMutexIdx < FrameCount ? Release.NextKeyMutexIdx : 0;
        IsFrameHeld = FALSE;
    }
    ReleaseSRWLockExclusive(&Lock);

    return hr;
}
