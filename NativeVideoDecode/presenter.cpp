/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Win32 window, menus, flip model swap chain and video processor output
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "presenter.hpp"

#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>

#include <algorithm>

static const WCHAR WindowClassName[] = L"ReactXNativeVideoDecode";
static const WCHAR DefaultIdleMessage[] = L"File > Open (Ctrl+O) or drop a video file here";

Presenter::~Presenter()
{
    ClearVideo();
    ReleaseSizeDependent();
    if (Window != NULL)
        DestroyWindow(Window);
}

VOID
Presenter::PushCommand(
    _In_ PresenterCommandId Id)
{
    Commands.push_back({ Id, std::wstring() });
}

LRESULT
CALLBACK
Presenter::WindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    Presenter* Self = reinterpret_cast<Presenter*>(GetWindowLongPtrW(Window, GWLP_USERDATA));
    WCHAR DroppedPath[MAX_PATH];
    BOOLEAN IsControl;

    switch (Message)
    {
        case WM_NCCREATE:
            SetWindowLongPtrW(Window,
                              GWLP_USERDATA,
                              (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(LParam)->lpCreateParams);
            break;

        case WM_SIZE:
            if (Self != nullptr && WParam != SIZE_MINIMIZED)
            {
                Self->ClientWidth = std::max<UINT>(LOWORD(LParam), 1);
                Self->ClientHeight = std::max<UINT>(HIWORD(LParam), 1);
                Self->NeedsResize = TRUE;
            }
            return 0;

        case WM_COMMAND:
            if (Self != nullptr && HIWORD(WParam) == 0)
                Self->PushCommand((PresenterCommandId)LOWORD(WParam));
            return 0;

        case WM_DROPFILES:
            if (Self != nullptr && DragQueryFileW((HDROP)WParam, 0, DroppedPath, _countof(DroppedPath)))
                Self->Commands.push_back({ CmdOpen, DroppedPath });
            DragFinish((HDROP)WParam);
            return 0;

        case WM_KEYDOWN:
            if (Self == nullptr)
                break;

            IsControl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            switch (WParam)
            {
                case 'O':
                    if (IsControl)
                        Self->PushCommand(CmdOpen);
                    break;

                case 'E':
                    if (IsControl)
                        Self->PushCommand(CmdExportFrame);
                    break;

                case 'I':
                    Self->PushCommand(IsControl ? CmdStreamInfo : CmdOverlay);
                    break;

                case VK_ESCAPE:
                    Self->PushCommand(CmdExit);
                    break;

                case VK_SPACE:
                    Self->PushCommand(CmdPause);
                    break;

                case VK_OEM_PERIOD:
                    Self->PushCommand(CmdStep);
                    break;

                case 'R':
                    Self->PushCommand(CmdRestart);
                    break;

                case VK_LEFT:
                    Self->PushCommand(CmdSeekBack);
                    break;

                case VK_RIGHT:
                    Self->PushCommand(CmdSeekForward);
                    break;

                case 'F':
                    Self->PushCommand(CmdFast);
                    break;

                case 'L':
                    Self->PushCommand(CmdLoop);
                    break;

                case 'S':
                    Self->PushCommand(CmdPrintStats);
                    break;
            }
            return 0;

        case WM_CLOSE:
            if (Self != nullptr)
            {
                Self->PushCommand(CmdExit);
                return 0;
            }
            break;

        case WM_DESTROY:
            if (Self != nullptr)
            {
                Self->IsClosed = TRUE;
                Self->Window = NULL;
            }
            return 0;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

HMENU
Presenter::BuildMenu()
{
    HMENU Bar = CreateMenu();
    HMENU File = CreatePopupMenu();
    HMENU Playback = CreatePopupMenu();
    HMENU View = CreatePopupMenu();

    AppendMenuW(File, MF_STRING, CmdOpen, L"&Open...\tCtrl+O");
    AppendMenuW(File, MF_STRING, CmdExportFrame, L"&Export Frame...\tCtrl+E");
    AppendMenuW(File, MF_STRING, CmdStreamInfo, L"Stream &Info...\tCtrl+I");
    AppendMenuW(File, MF_STRING, CmdClose, L"&Close");
    AppendMenuW(File, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(File, MF_STRING, CmdExit, L"E&xit\tEsc");

    AppendMenuW(Playback, MF_STRING, CmdPause, L"&Pause\tSpace");
    AppendMenuW(Playback, MF_STRING, CmdStep, L"&Step Frame\t.");
    AppendMenuW(Playback, MF_STRING, CmdRestart, L"&Restart\tR");
    AppendMenuW(Playback, MF_STRING, CmdSeekBack, L"&Back 5 s\tLeft");
    AppendMenuW(Playback, MF_STRING, CmdSeekForward, L"&Forward 5 s\tRight");
    AppendMenuW(Playback, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(Playback, MF_STRING, CmdFast, L"&Unpaced (decode speed)\tF");
    AppendMenuW(Playback, MF_STRING, CmdLoop, L"&Loop\tL");

    AppendMenuW(View, MF_STRING, CmdOverlay, L"Stats &Overlay\tI");
    AppendMenuW(View, MF_STRING, CmdPrintStats, L"&Print Stats to Console\tS");

    AppendMenuW(Bar, MF_POPUP, (UINT_PTR)File, L"&File");
    AppendMenuW(Bar, MF_POPUP, (UINT_PTR)Playback, L"&Playback");
    AppendMenuW(Bar, MF_POPUP, (UINT_PTR)View, L"&View");
    return Bar;
}

HRESULT
Presenter::Initialize(
    _In_ ID3D11Device* NewDevice,
    _In_ ID3D11DeviceContext* NewContext,
    _In_z_ PCWSTR Title)
{
    DXGI_SWAP_CHAIN_DESC1 SwapDesc = {};
    ComPtr<IDXGIDevice> DxgiDevice;
    ComPtr<IDXGIAdapter> Adapter;
    ComPtr<IDXGIFactory2> Factory;
    WNDCLASSEXW WindowClass = {};
    RECT Frame = { 0, 0, 960, 540 };
    HRESULT hr;

    Device = NewDevice;
    Context = NewContext;
    IdleMessage = DefaultIdleMessage;

    hr = Device.As(&VideoDevice);
    if (SUCCEEDED(hr))
        hr = Context.As(&VideoContext);
    if (SUCCEEDED(hr))
        hr = Device.As(&DxgiDevice);
    if (SUCCEEDED(hr))
        hr = DxgiDevice->GetAdapter(&Adapter);
    if (SUCCEEDED(hr))
        hr = Adapter->GetParent(IID_PPV_ARGS(&Factory));
    if (FAILED(hr))
    {
        wprintf(L"note: runtime has no DXGI device or video context (0x%08lX), no window\n", hr);
        return hr;
    }

    WindowClass.cbSize = sizeof(WindowClass);
    WindowClass.lpfnWndProc = WindowProc;
    WindowClass.hInstance = GetModuleHandleW(NULL);
    WindowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    WindowClass.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    WindowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    WindowClass.lpszClassName = WindowClassName;
    RegisterClassExW(&WindowClass);

    Menu = BuildMenu();
    AdjustWindowRect(&Frame, WS_OVERLAPPEDWINDOW, TRUE);
    Window = CreateWindowExW(WS_EX_ACCEPTFILES,
                             WindowClassName,
                             Title,
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             Frame.right - Frame.left,
                             Frame.bottom - Frame.top,
                             NULL,
                             Menu,
                             WindowClass.hInstance,
                             this);
    if (Window == NULL)
        return HRESULT_FROM_WIN32(GetLastError());

    GetClientRect(Window, &Frame);
    ClientWidth = std::max<UINT>(Frame.right, 1);
    ClientHeight = std::max<UINT>(Frame.bottom, 1);

    SwapDesc.Width = ClientWidth;
    SwapDesc.Height = ClientHeight;
    SwapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    SwapDesc.SampleDesc.Count = 1;
    SwapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapDesc.BufferCount = 2;
    SwapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    SwapDesc.Scaling = DXGI_SCALING_STRETCH;

    hr = Factory->CreateSwapChainForHwnd(Device.Get(), Window, &SwapDesc, nullptr, nullptr, &SwapChain);
    if (FAILED(hr))
    {
        wprintf(L"note: CreateSwapChainForHwnd failed 0x%08lX, no window\n", hr);
        return hr;
    }
    Factory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER);

    /* Text is optional; playback works without Direct2D */
    if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, D2dFactory.GetAddressOf())) &&
        SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                      __uuidof(IDWriteFactory),
                                      reinterpret_cast<IUnknown**>(DwriteFactory.GetAddressOf()))))
    {
        DwriteFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                        14.0f, L"en-us", &TextFormat);
        if (SUCCEEDED(DwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                      18.0f, L"en-us", &CenteredFormat)))
        {
            CenteredFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            CenteredFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    hr = CreateSizeDependent();
    if (FAILED(hr))
        return hr;

    ShowWindow(Window, SW_SHOW);
    return PresentIdle(IdleMessage.c_str());
}

HRESULT
Presenter::SetVideo(
    _In_ ID3D11Texture2D* NewDecodeTexture,
    _In_ const VideoFormatInfo& Format,
    _In_ double FrameRate)
{
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC Content = {};
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ViewDesc = {};
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE InputSpace = {};
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE OutputSpace = {};
    D3D11_TEXTURE2D_DESC TextureDesc;
    D3D11_VIDEO_COLOR Black = {};
    ComPtr<ID3D11Texture2D> BackBuffer;
    BOOLEAN IsBt709;
    HRESULT hr;

    ClearVideo();

    DecodeTexture = NewDecodeTexture;
    LastVisible = Format.Visible;
    VideoWidth = Format.Visible.right - Format.Visible.left;
    VideoHeight = Format.Visible.bottom - Format.Visible.top;
    PixelAspect = (Format.SarWidth != 0 && Format.SarHeight != 0) ? (double)Format.SarWidth / Format.SarHeight
                                                                 : 1.0;

    DecodeTexture->GetDesc(&TextureDesc);
    Content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    Content.InputFrameRate = { (UINT)(FrameRate * 1000), 1000 };
    Content.InputWidth = TextureDesc.Width;
    Content.InputHeight = TextureDesc.Height;
    Content.OutputFrameRate = Content.InputFrameRate;
    Content.OutputWidth = VideoWidth;
    Content.OutputHeight = VideoHeight;
    Content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    hr = VideoDevice->CreateVideoProcessorEnumerator(&Content, &Enumerator);
    if (SUCCEEDED(hr))
        hr = VideoDevice->CreateVideoProcessor(Enumerator.Get(), 0, &Processor);
    if (FAILED(hr))
    {
        wprintf(L"error: video processor unavailable (0x%08lX)\n", hr);
        ClearVideo();
        return hr;
    }

    InputViews.resize(TextureDesc.ArraySize);

    /* Untagged streams follow the usual convention: BT.709 for HD, BT.601 below it */
    IsBt709 = Format.HasColourDescription ? (Format.MatrixCoefficients == 1) : (VideoHeight >= 720);
    InputSpace.YCbCr_Matrix = IsBt709 ? 1 : 0;
    InputSpace.Nominal_Range = Format.IsFullRange ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
                                                  : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    OutputSpace.RGB_Range = 0;

    VideoContext->VideoProcessorSetStreamColorSpace(Processor.Get(), 0, &InputSpace);
    VideoContext->VideoProcessorSetOutputColorSpace(Processor.Get(), &OutputSpace);
    VideoContext->VideoProcessorSetStreamFrameFormat(Processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    VideoContext->VideoProcessorSetStreamAutoProcessingMode(Processor.Get(), 0, FALSE);
    Black.RGBA.A = 1.0f;
    VideoContext->VideoProcessorSetOutputBackgroundColor(Processor.Get(), FALSE, &Black);

    hr = SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer));
    if (SUCCEEDED(hr))
    {
        ViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        hr = VideoDevice->CreateVideoProcessorOutputView(BackBuffer.Get(), Enumerator.Get(), &ViewDesc, &OutputView);
    }
    if (FAILED(hr))
    {
        wprintf(L"error: CreateVideoProcessorOutputView failed 0x%08lX\n", hr);
        ClearVideo();
        return hr;
    }

    FitWindowToVideo();
    return S_OK;
}

VOID
Presenter::ClearVideo()
{
    InputViews.clear();
    OutputView.Reset();
    Processor.Reset();
    Enumerator.Reset();
    DecodeTexture.Reset();
    HasFrame = FALSE;
}

VOID
Presenter::FitWindowToVideo()
{
    RECT WorkArea;
    RECT Frame;
    RECT Current;
    double Width = VideoWidth * PixelAspect;
    double Height = VideoHeight;
    double Scale;

    /* Fit inside 80 percent of the work area, never upscaling */
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &WorkArea, 0);
    Scale = std::min({ 1.0,
                       0.8 * (WorkArea.right - WorkArea.left) / Width,
                       0.8 * (WorkArea.bottom - WorkArea.top) / Height });

    Frame = { 0, 0, (LONG)(Width * Scale), (LONG)(Height * Scale) };
    AdjustWindowRect(&Frame, WS_OVERLAPPEDWINDOW, TRUE);
    GetWindowRect(Window, &Current);
    SetWindowPos(Window,
                 NULL,
                 std::max(Current.left, WorkArea.left),
                 std::max(Current.top, WorkArea.top),
                 Frame.right - Frame.left,
                 Frame.bottom - Frame.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

HRESULT
Presenter::CreateSizeDependent()
{
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ViewDesc = {};
    D2D1_RENDER_TARGET_PROPERTIES Properties;
    ComPtr<ID3D11Texture2D> BackBuffer;
    ComPtr<IDXGISurface> Surface;
    HRESULT hr;

    /* With D3D11 flip model buffer 0 always names the current back buffer */
    hr = SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer));
    if (FAILED(hr))
        return hr;

    hr = Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, &BackBufferView);
    if (FAILED(hr))
        return hr;

    if (Enumerator != nullptr)
    {
        ViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        hr = VideoDevice->CreateVideoProcessorOutputView(BackBuffer.Get(), Enumerator.Get(), &ViewDesc, &OutputView);
        if (FAILED(hr))
            return hr;
    }

    if (D2dFactory != nullptr && TextFormat != nullptr && SUCCEEDED(BackBuffer.As(&Surface)))
    {
        Properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                  D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                    D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (SUCCEEDED(D2dFactory->CreateDxgiSurfaceRenderTarget(Surface.Get(), &Properties, &OverlayTarget)))
        {
            OverlayTarget->CreateSolidColorBrush(D2D1::ColorF(0.85f, 1.0f, 0.85f), &TextBrush);
            OverlayTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.6f), &PanelBrush);
        }
    }

    return S_OK;
}

VOID
Presenter::ReleaseSizeDependent()
{
    TextBrush.Reset();
    PanelBrush.Reset();
    OverlayTarget.Reset();
    OutputView.Reset();
    BackBufferView.Reset();
}

BOOLEAN
Presenter::PumpMessages()
{
    MSG Message;

    while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }

    if (NeedsResize && SwapChain != nullptr && !IsClosed)
    {
        NeedsResize = FALSE;
        ReleaseSizeDependent();
        Context->ClearState();
        Context->Flush();
        if (SUCCEEDED(SwapChain->ResizeBuffers(0, ClientWidth, ClientHeight, DXGI_FORMAT_UNKNOWN, 0)))
            CreateSizeDependent();

        if (HasFrame)
            Redraw(nullptr);
        else
            PresentIdle(IdleMessage.c_str());
    }

    return !IsClosed;
}

BOOLEAN
Presenter::TakeCommand(
    _Out_ PresenterCommand& Command)
{
    if (Commands.empty())
        return FALSE;

    Command = Commands.front();
    Commands.erase(Commands.begin());
    return TRUE;
}

HRESULT
Presenter::DrawOverlay(
    _In_ const std::wstring& Text)
{
    D2D1_RECT_F Panel = D2D1::RectF(8, 8, 540, 112);

    OverlayTarget->BeginDraw();
    OverlayTarget->FillRectangle(Panel, PanelBrush.Get());
    OverlayTarget->DrawText(Text.c_str(),
                            (UINT32)Text.size(),
                            TextFormat.Get(),
                            D2D1::RectF(16, 12, 532, 108),
                            TextBrush.Get());
    return OverlayTarget->EndDraw();
}

HRESULT
Presenter::PresentIdle(
    _In_z_ PCWSTR Message)
{
    static const FLOAT Background[4] = { 0.08f, 0.08f, 0.1f, 1.0f };

    IdleMessage = Message;
    if (BackBufferView == nullptr)
        return E_UNEXPECTED;

    Context->ClearRenderTargetView(BackBufferView.Get(), Background);
    if (OverlayTarget != nullptr && CenteredFormat != nullptr)
    {
        OverlayTarget->BeginDraw();
        OverlayTarget->DrawText(Message,
                                (UINT32)wcslen(Message),
                                CenteredFormat.Get(),
                                D2D1::RectF(0, 0, (FLOAT)ClientWidth, (FLOAT)ClientHeight),
                                TextBrush.Get());
        OverlayTarget->EndDraw();
    }

    return SwapChain->Present(1, 0);
}

HRESULT
Presenter::Present(
    _In_ UINT Slice,
    _In_ const RECT& Visible,
    _In_opt_ const std::wstring* Overlay,
    _In_ BOOLEAN WaitForVBlank)
{
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC InputDesc = {};
    D3D11_VIDEO_PROCESSOR_STREAM Stream = {};
    RECT Target = { 0, 0, (LONG)ClientWidth, (LONG)ClientHeight };
    RECT Destination;
    double DisplayWidth;
    double Scale;
    LONG Width;
    LONG Height;
    HRESULT hr;

    if (OutputView == nullptr || Slice >= InputViews.size())
        return E_UNEXPECTED;

    if (InputViews[Slice] == nullptr)
    {
        InputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        InputDesc.Texture2D.ArraySlice = Slice;
        hr = VideoDevice->CreateVideoProcessorInputView(DecodeTexture.Get(),
                                                        Enumerator.Get(),
                                                        &InputDesc,
                                                        &InputViews[Slice]);
        if (FAILED(hr))
        {
            wprintf(L"error: CreateVideoProcessorInputView(%u) failed 0x%08lX\n", Slice, hr);
            return hr;
        }
    }

    /* AV1 pictures may change size; letterbox each one with the sample aspect ratio applied */
    VideoWidth = Visible.right - Visible.left;
    VideoHeight = Visible.bottom - Visible.top;
    DisplayWidth = VideoWidth * PixelAspect;
    Scale = std::min(ClientWidth / DisplayWidth, (double)ClientHeight / VideoHeight);
    Width = (LONG)(DisplayWidth * Scale);
    Height = (LONG)(VideoHeight * Scale);
    Destination.left = ((LONG)ClientWidth - Width) / 2;
    Destination.top = ((LONG)ClientHeight - Height) / 2;
    Destination.right = Destination.left + Width;
    Destination.bottom = Destination.top + Height;

    VideoContext->VideoProcessorSetStreamSourceRect(Processor.Get(), 0, TRUE, &Visible);
    VideoContext->VideoProcessorSetOutputTargetRect(Processor.Get(), TRUE, &Target);
    VideoContext->VideoProcessorSetStreamDestRect(Processor.Get(), 0, TRUE, &Destination);

    Stream.Enable = TRUE;
    Stream.pInputSurface = InputViews[Slice].Get();
    hr = VideoContext->VideoProcessorBlt(Processor.Get(), OutputView.Get(), 0, 1, &Stream);
    if (FAILED(hr))
    {
        wprintf(L"error: VideoProcessorBlt failed 0x%08lX\n", hr);
        return hr;
    }

    LastSlice = Slice;
    LastVisible = Visible;
    HasFrame = TRUE;

    if (Overlay != nullptr && OverlayTarget != nullptr)
        DrawOverlay(*Overlay);

    return SwapChain->Present(WaitForVBlank ? 1 : 0, 0);
}

HRESULT
Presenter::Redraw(
    _In_opt_ const std::wstring* Overlay)
{
    if (!HasFrame)
        return S_FALSE;

    return Present(LastSlice, LastVisible, Overlay, TRUE);
}

static
GUID
NvdContainerForPath(
    _In_z_ PCWSTR Path)
{
    PCWSTR Extension = wcsrchr(Path, L'.');

    if (Extension != nullptr && (_wcsicmp(Extension, L".jpg") == 0 || _wcsicmp(Extension, L".jpeg") == 0))
        return GUID_ContainerFormatJpeg;
    if (Extension != nullptr && _wcsicmp(Extension, L".bmp") == 0)
        return GUID_ContainerFormatBmp;
    return GUID_ContainerFormatPng;
}

HRESULT
Presenter::ExportFrame(
    _In_ UINT Slice,
    _In_ const RECT& Visible,
    _In_z_ PCWSTR Path)
{
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC InputDesc = {};
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputDesc = {};
    D3D11_VIDEO_PROCESSOR_STREAM Stream = {};
    D3D11_TEXTURE2D_DESC Desc = {};
    D3D11_MAPPED_SUBRESOURCE Mapped;
    ComPtr<ID3D11VideoProcessorOutputView> ExportView;
    ComPtr<ID3D11Texture2D> Target;
    ComPtr<ID3D11Texture2D> Staging;
    ComPtr<IWICImagingFactory> Wic;
    ComPtr<IWICStream> WicStream;
    ComPtr<IWICBitmapEncoder> Encoder;
    ComPtr<IWICBitmapFrameEncode> FrameEncode;
    ComPtr<IWICBitmap> Bitmap;
    WICPixelFormatGUID Format = GUID_WICPixelFormat32bppBGR;
    UINT ExportWidth = Visible.right - Visible.left;
    UINT ExportHeight = Visible.bottom - Visible.top;
    RECT Full;
    HRESULT hr;

    if (Processor == nullptr || Slice >= InputViews.size())
        return E_UNEXPECTED;

    /* Native display size: the coded picture with cropping applied, no window scaling */
    Desc.Width = ExportWidth;
    Desc.Height = ExportHeight;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = Device->CreateTexture2D(&Desc, nullptr, &Target);
    if (FAILED(hr))
        return hr;

    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = Device->CreateTexture2D(&Desc, nullptr, &Staging);
    if (FAILED(hr))
        return hr;

    OutputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    hr = VideoDevice->CreateVideoProcessorOutputView(Target.Get(), Enumerator.Get(), &OutputDesc, &ExportView);
    if (FAILED(hr))
        return hr;

    if (InputViews[Slice] == nullptr)
    {
        InputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        InputDesc.Texture2D.ArraySlice = Slice;
        hr = VideoDevice->CreateVideoProcessorInputView(DecodeTexture.Get(), Enumerator.Get(), &InputDesc,
                                                        &InputViews[Slice]);
        if (FAILED(hr))
            return hr;
    }

    /* Present sets these again for the window every frame */
    Full = { 0, 0, (LONG)ExportWidth, (LONG)ExportHeight };
    VideoContext->VideoProcessorSetStreamSourceRect(Processor.Get(), 0, TRUE, &Visible);
    VideoContext->VideoProcessorSetOutputTargetRect(Processor.Get(), TRUE, &Full);
    VideoContext->VideoProcessorSetStreamDestRect(Processor.Get(), 0, TRUE, &Full);

    Stream.Enable = TRUE;
    Stream.pInputSurface = InputViews[Slice].Get();
    hr = VideoContext->VideoProcessorBlt(Processor.Get(), ExportView.Get(), 0, 1, &Stream);
    if (FAILED(hr))
        return hr;

    Context->CopyResource(Staging.Get(), Target.Get());
    hr = Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped);
    if (FAILED(hr))
        return hr;

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Wic));
    if (SUCCEEDED(hr))
    {
        hr = Wic->CreateBitmapFromMemory(ExportWidth, ExportHeight, GUID_WICPixelFormat32bppBGR,
                                         Mapped.RowPitch, Mapped.RowPitch * ExportHeight,
                                         (BYTE*)Mapped.pData, &Bitmap);
    }
    Context->Unmap(Staging.Get(), 0);

    if (SUCCEEDED(hr))
        hr = Wic->CreateStream(&WicStream);
    if (SUCCEEDED(hr))
        hr = WicStream->InitializeFromFilename(Path, GENERIC_WRITE);
    if (SUCCEEDED(hr))
        hr = Wic->CreateEncoder(NvdContainerForPath(Path), nullptr, &Encoder);
    if (SUCCEEDED(hr))
        hr = Encoder->Initialize(WicStream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = Encoder->CreateNewFrame(&FrameEncode, nullptr);
    if (SUCCEEDED(hr))
        hr = FrameEncode->Initialize(nullptr);
    if (SUCCEEDED(hr))
        hr = FrameEncode->SetSize(ExportWidth, ExportHeight);

    /* The encoder may pick another layout (24 bit for JPEG); WriteSource converts to it */
    if (SUCCEEDED(hr))
        hr = FrameEncode->SetPixelFormat(&Format);
    if (SUCCEEDED(hr))
        hr = FrameEncode->WriteSource(Bitmap.Get(), nullptr);
    if (SUCCEEDED(hr))
        hr = FrameEncode->Commit();
    if (SUCCEEDED(hr))
        hr = Encoder->Commit();

    return hr;
}

VOID
Presenter::UpdateMenu(
    _In_ const PresenterChecks& Checks)
{
    UINT Enabled = Checks.HasVideo ? MF_ENABLED : MF_GRAYED;

    CheckMenuItem(Menu, CmdPause, Checks.IsPaused ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(Menu, CmdFast, Checks.IsFast ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(Menu, CmdLoop, Checks.IsLooping ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(Menu, CmdOverlay, Checks.ShowOverlay ? MF_CHECKED : MF_UNCHECKED);

    EnableMenuItem(Menu, CmdExportFrame, Enabled);
    EnableMenuItem(Menu, CmdStreamInfo, Enabled);
    EnableMenuItem(Menu, CmdClose, Enabled);
    EnableMenuItem(Menu, CmdPause, Enabled);
    EnableMenuItem(Menu, CmdStep, Enabled);
    EnableMenuItem(Menu, CmdRestart, Enabled);
    EnableMenuItem(Menu, CmdSeekBack, Enabled);
    EnableMenuItem(Menu, CmdSeekForward, Enabled);
}

VOID
Presenter::SetTitle(
    _In_z_ PCWSTR Text)
{
    if (Window != NULL)
        SetWindowTextW(Window, Text);
}

BOOLEAN
NvdBrowseForVideo(
    _In_opt_ HWND Owner,
    _Out_ std::wstring& Path)
{
    OPENFILENAMEW Dialog = {};
    WCHAR Buffer[MAX_PATH] = L"";

    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = Owner;
    Dialog.lpstrFilter = L"Video (*.mp4;*.m4v;*.mov;*.webm;*.mkv;*.h264;*.264)\0"
                         L"*.mp4;*.m4v;*.mov;*.webm;*.mkv;*.h264;*.264\0"
                         L"All files (*.*)\0*.*\0";
    Dialog.lpstrFile = Buffer;
    Dialog.nMaxFile = _countof(Buffer);
    Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&Dialog))
        return FALSE;

    Path = Buffer;
    return TRUE;
}

BOOLEAN
NvdBrowseForExport(
    _In_opt_ HWND Owner,
    _In_z_ PCWSTR SuggestedName,
    _Out_ std::wstring& Path)
{
    OPENFILENAMEW Dialog = {};
    WCHAR Buffer[MAX_PATH];

    wcscpy_s(Buffer, SuggestedName);
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = Owner;
    Dialog.lpstrFilter = L"PNG image (*.png)\0*.png\0JPEG image (*.jpg)\0*.jpg\0Bitmap (*.bmp)\0*.bmp\0";
    Dialog.lpstrDefExt = L"png";
    Dialog.lpstrFile = Buffer;
    Dialog.nMaxFile = _countof(Buffer);
    Dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&Dialog))
        return FALSE;

    Path = Buffer;
    return TRUE;
}
