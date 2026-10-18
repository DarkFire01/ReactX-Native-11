/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Win32 window, menus, flip model swap chain and video processor output
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "videodecoder.hpp"

using Microsoft::WRL::ComPtr;

enum PresenterCommandId : UINT
{
    CmdNone = 0,
    CmdOpen = 100,
    CmdClose,
    CmdExportFrame,
    CmdStreamInfo,
    CmdExit,
    CmdPause,
    CmdStep,
    CmdRestart,
    CmdSeekBack,
    CmdSeekForward,
    CmdFast,
    CmdLoop,
    CmdOverlay,
    CmdPrintStats,
};

struct PresenterCommand
{
    PresenterCommandId Id;

    /* Set for CmdOpen when a file was dropped on the window */
    std::wstring Path;
};

struct PresenterChecks
{
    BOOLEAN IsPaused;
    BOOLEAN IsFast;
    BOOLEAN IsLooping;
    BOOLEAN ShowOverlay;
    BOOLEAN HasVideo;
};

class Presenter
{
public:
    ~Presenter();

    HRESULT
    Initialize(
        _In_ ID3D11Device* Device,
        _In_ ID3D11DeviceContext* Context,
        _In_z_ PCWSTR Title);

    /* Builds the video processor for a decoder's surfaces and sizes the window to the picture */
    HRESULT
    SetVideo(
        _In_ ID3D11Texture2D* DecodeTexture,
        _In_ const VideoFormatInfo& Format,
        _In_ double FrameRate);

    VOID
    ClearVideo();

    /* Handles queued window messages; FALSE once the window is closed */
    BOOLEAN
    PumpMessages();

    BOOLEAN
    TakeCommand(
        _Out_ PresenterCommand& Command);

    HRESULT
    Present(
        _In_ UINT Slice,
        _In_ const RECT& Visible,
        _In_opt_ const std::wstring* Overlay,
        _In_ BOOLEAN WaitForVBlank);

    /* Shows the last frame again, used while paused or after a resize */
    HRESULT
    Redraw(
        _In_opt_ const std::wstring* Overlay);

    HRESULT
    PresentIdle(
        _In_z_ PCWSTR Message);

    /* Converts a surface to RGB at its display size and saves it through WIC */
    HRESULT
    ExportFrame(
        _In_ UINT Slice,
        _In_ const RECT& Visible,
        _In_z_ PCWSTR Path);

    VOID
    UpdateMenu(
        _In_ const PresenterChecks& Checks);

    VOID
    SetTitle(
        _In_z_ PCWSTR Text);

    HWND
    GetWindow() const
    {
        return Window;
    }

private:
    static
    LRESULT
    CALLBACK
    WindowProc(
        _In_ HWND Window,
        _In_ UINT Message,
        _In_ WPARAM WParam,
        _In_ LPARAM LParam);

    VOID
    PushCommand(
        _In_ PresenterCommandId Id);

    HMENU
    BuildMenu();

    HRESULT
    CreateSizeDependent();

    VOID
    ReleaseSizeDependent();

    HRESULT
    DrawOverlay(
        _In_ const std::wstring& Text);

    VOID
    FitWindowToVideo();

    HWND Window = NULL;
    HMENU Menu = NULL;
    BOOLEAN IsClosed = FALSE;
    BOOLEAN NeedsResize = FALSE;
    UINT ClientWidth = 0;
    UINT ClientHeight = 0;
    std::vector<PresenterCommand> Commands;
    std::wstring IdleMessage;

    UINT LastSlice = 0;
    RECT LastVisible = {};
    BOOLEAN HasFrame = FALSE;
    UINT VideoWidth = 0;
    UINT VideoHeight = 0;
    double PixelAspect = 1.0;

    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11VideoDevice> VideoDevice;
    ComPtr<ID3D11VideoContext> VideoContext;
    ComPtr<IDXGISwapChain1> SwapChain;
    ComPtr<ID3D11RenderTargetView> BackBufferView;

    ComPtr<ID3D11Texture2D> DecodeTexture;
    ComPtr<ID3D11VideoProcessorEnumerator> Enumerator;
    ComPtr<ID3D11VideoProcessor> Processor;
    std::vector<ComPtr<ID3D11VideoProcessorInputView>> InputViews;
    ComPtr<ID3D11VideoProcessorOutputView> OutputView;

    ComPtr<ID2D1Factory> D2dFactory;
    ComPtr<ID2D1RenderTarget> OverlayTarget;
    ComPtr<ID2D1SolidColorBrush> TextBrush;
    ComPtr<ID2D1SolidColorBrush> PanelBrush;
    ComPtr<IDWriteFactory> DwriteFactory;
    ComPtr<IDWriteTextFormat> TextFormat;
    ComPtr<IDWriteTextFormat> CenteredFormat;
};

/* Common dialogs; FALSE when the user cancels */
BOOLEAN
NvdBrowseForVideo(
    _In_opt_ HWND Owner,
    _Out_ std::wstring& Path);

BOOLEAN
NvdBrowseForExport(
    _In_opt_ HWND Owner,
    _In_z_ PCWSTR SuggestedName,
    _Out_ std::wstring& Path);
