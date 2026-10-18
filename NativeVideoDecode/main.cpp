/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Hardware video playback through D3D11 video, with a small player UI and statistics
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <timeapi.h>
#include <objbase.h>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <memory>
#include <stdio.h>
#include <string>

#include "player.hpp"
#include "presenter.hpp"

using Microsoft::WRL::ComPtr;

static const WCHAR IdleMessage[] = L"File > Open (Ctrl+O) or drop a video file here";

static const WCHAR Usage[] =
    L"usage: NativeVideoDecode [file.mp4|file.webm|file.mkv|file.h264] [options]\n"
    L"  without a file the player window opens empty; use File > Open or drag a file in\n"
    L"  -runtime system|reactx|<path>  d3d11.dll to drive (default system)\n"
    L"  -adapter <n>                   DXGI adapter index (default 0)\n"
    L"  -headless                      decode only, no window (needs a file)\n"
    L"  -fast                          ignore timestamps, decode and show as fast as possible\n"
    L"  -loop                          start over at end of stream\n"
    L"  -frames <n>                    exit after n displayed frames\n"
    L"  -fps <rate>                    override the frame rate\n"
    L"  -csv <path>                    per picture statistics\n"
    L"  -crc                           read every frame back and checksum it\n"
    L"  -dump <dir> [-dumpcount <n>]   write the first n frames (default 10) as raw NV12\n"
    L"  -nogpu                         skip GPU completion timing queries\n"
    L"  -v                             trace output from the ReactX runtime\n"
    L"keys: Ctrl+O open, Ctrl+E export frame, Ctrl+I info, Space pause, . step, R restart,\n"
    L"      Left/Right seek 5 s, F unpaced, L loop, I overlay, S stats, Esc quit\n";

struct Options
{
    std::wstring Input;
    std::wstring Runtime = L"system";
    UINT Adapter = 0;
    BOOLEAN Headless = FALSE;
    BOOLEAN Fast = FALSE;
    BOOLEAN Loop = FALSE;
    UINT64 FrameLimit = 0;
    PlayerOptions Player;
};

struct UiState
{
    BOOLEAN IsPaused = FALSE;
    BOOLEAN IsFast = FALSE;
    BOOLEAN IsLooping = FALSE;
    BOOLEAN ShowOverlay = TRUE;
    BOOLEAN IsFinished = FALSE;
    BOOLEAN WantsStep = FALSE;
};

static
BOOLEAN
NvdParseOptions(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv,
    _Out_ Options& Out)
{
    int Index;

    for (Index = 1; Index < argc; Index++)
    {
        std::wstring Argument = argv[Index];
        BOOLEAN HasValue = (Index + 1 < argc);

        if (Argument == L"-runtime" && HasValue)
            Out.Runtime = argv[++Index];
        else if (Argument == L"-adapter" && HasValue)
            Out.Adapter = (UINT)_wtoi(argv[++Index]);
        else if (Argument == L"-headless")
            Out.Headless = TRUE;
        else if (Argument == L"-fast")
            Out.Fast = TRUE;
        else if (Argument == L"-loop")
            Out.Loop = TRUE;
        else if (Argument == L"-frames" && HasValue)
            Out.FrameLimit = _wtoi64(argv[++Index]);
        else if (Argument == L"-fps" && HasValue)
            Out.Player.FrameRate = _wtof(argv[++Index]);
        else if (Argument == L"-csv" && HasValue)
            Out.Player.CsvPath = argv[++Index];
        else if (Argument == L"-crc")
            Out.Player.Checksum = TRUE;
        else if (Argument == L"-dump" && HasValue)
            Out.Player.DumpDirectory = argv[++Index];
        else if (Argument == L"-dumpcount" && HasValue)
            Out.Player.DumpCount = (UINT)_wtoi(argv[++Index]);
        else if (Argument == L"-nogpu")
            Out.Player.GpuTiming = FALSE;
        else if (Argument == L"-v")
            SetEnvironmentVariableW(L"REACTX_LOG", L"1");
        else if (Argument[0] != L'-' && Out.Input.empty())
            Out.Input = Argument;
        else
            return FALSE;
    }

    return !Out.Headless || !Out.Input.empty();
}

static
std::wstring
NvdRuntimePath(
    _In_ const std::wstring& Choice)
{
    WCHAR Path[MAX_PATH];
    PWSTR Slash;

    if (Choice == L"system")
    {
        GetSystemDirectoryW(Path, _countof(Path));
        wcscat_s(Path, L"\\d3d11.dll");
        return Path;
    }

    if (Choice == L"reactx")
    {
        GetModuleFileNameW(NULL, Path, _countof(Path));
        Slash = wcsrchr(Path, L'\\');
        if (Slash != nullptr)
            Slash[1] = UNICODE_NULL;
        wcscat_s(Path, L"reactx\\d3d11.dll");
        return Path;
    }

    return Choice;
}

static
VOID
NvdPrintAdapter(
    _In_ IDXGIAdapter1* Adapter,
    _In_ UINT Index)
{
    DXGI_ADAPTER_DESC1 Desc;
    LARGE_INTEGER UmdVersion = {};

    Adapter->GetDesc1(&Desc);
    Adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &UmdVersion);
    wprintf(L"  adapter %u: %s\n", Index, Desc.Description);
    wprintf(L"    vendor %04X device %04X, %llu MB dedicated, LUID %08lX:%08lX, UMD %u.%u.%u.%u%s\n",
            Desc.VendorId,
            Desc.DeviceId,
            (UINT64)Desc.DedicatedVideoMemory >> 20,
            Desc.AdapterLuid.HighPart,
            Desc.AdapterLuid.LowPart,
            HIWORD(UmdVersion.HighPart),
            LOWORD(UmdVersion.HighPart),
            HIWORD(UmdVersion.LowPart),
            LOWORD(UmdVersion.LowPart),
            (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? L" [software]" : L"");
}

static
VOID
NvdFormatTime(
    _In_ INT64 Time,
    _Out_writes_(Count) PWSTR Text,
    _In_ SIZE_T Count)
{
    double Seconds = Time / 1e7;

    swprintf_s(Text, Count, L"%02d:%04.1f", (int)(Seconds / 60), Seconds - 60 * (int)(Seconds / 60));
}

static
VOID
NvdUpdateTitle(
    _In_ Presenter& Window,
    _In_opt_ Player* Current,
    _In_ const UiState& State)
{
    WCHAR Position[32];
    WCHAR Duration[32];
    WCHAR Title[MAX_PATH + 128];
    PCWSTR Name;

    if (Current == nullptr)
    {
        Window.SetTitle(L"NativeVideoDecode");
        return;
    }

    Name = wcsrchr(Current->GetPath().c_str(), L'\\');
    Name = (Name != nullptr) ? Name + 1 : Current->GetPath().c_str();
    NvdFormatTime(Current->GetPosition(), Position, _countof(Position));
    NvdFormatTime(Current->GetDuration(), Duration, _countof(Duration));
    swprintf_s(Title, L"%s  %s / %s  frame %llu%s%s - NativeVideoDecode",
               Name, Position, Duration,
               Current->HasDisplayedFrame() ? Current->GetDisplayedFrame().DisplayIndex : 0,
               State.IsPaused ? L"  [paused]" : L"",
               State.IsFinished ? L"  [end]" : L"");
    Window.SetTitle(Title);
}

static
std::unique_ptr<Player>
NvdOpenFile(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context,
    _In_ const PlayerOptions& Options,
    _In_z_ PCWSTR Path)
{
    std::unique_ptr<Player> NewPlayer = std::make_unique<Player>(Device, Context, Options);

    wprintf(L"\nopening %s\n", Path);
    if (FAILED(NewPlayer->Open(Path)))
        return nullptr;

    return NewPlayer;
}

static
int
NvdRunHeadless(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context,
    _In_ const Options& Opts)
{
    std::unique_ptr<Player> Current;
    PlayerOptions Settings = Opts.Player;
    DecodedFrame Frame;
    UINT64 Shown = 0;
    DWORD WaitMs;
    TickResult Result;

    Settings.ConsumerSurfaces = 2;
    Current = NvdOpenFile(Device, Context, Settings, Opts.Input.c_str());
    if (Current == nullptr)
        return 1;
    Current->SetLooping(Opts.Loop);

    for (;;)
    {
        Result = Current->Tick(TRUE, FALSE, &Frame, &WaitMs);
        if (Result != TickResult::Show)
            break;

        Current->GetStats().OnFramePresented(0);
        Current->FrameShown(Frame);
        if (Opts.FrameLimit != 0 && ++Shown >= Opts.FrameLimit)
            break;
    }

    Current->GetStats().PrintSummary();
    return (Result == TickResult::Failed || Current->GetStats().GetDecodedCount() == 0) ? 1 : 0;
}

static
int
NvdRunWindowed(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context,
    _In_ const Options& Opts)
{
    std::unique_ptr<Player> Current;
    PresenterCommand Command;
    PresenterCommand Pending;
    BOOLEAN HasPending;
    PresenterChecks Checks;
    Presenter Window;
    UiState State;
    DecodedFrame Frame;
    std::wstring Overlay;
    std::wstring Path;
    std::wstring Info;
    WCHAR Suggested[MAX_PATH];
    PCWSTR Name;
    UINT64 Shown = 0;
    double LastTitleMs = 0;
    DWORD WaitMs;
    TickResult Result;
    HRESULT hr;

    hr = Window.Initialize(Device, Context, L"NativeVideoDecode");
    if (FAILED(hr))
    {
        if (Opts.Input.empty())
        {
            fwprintf(stderr, L"error: this runtime cannot open a window; pass a file with -headless\n");
            return 1;
        }

        wprintf(L"falling back to headless decode\n");
        return NvdRunHeadless(Device, Context, Opts);
    }

    State.IsFast = Opts.Fast;
    State.IsLooping = Opts.Loop;

    /* A file named on the command line goes through the same path as File > Open */
    HasPending = !Opts.Input.empty();
    Pending = { CmdOpen, Opts.Input };

    for (;;)
    {
        if (HasPending)
        {
            Command = Pending;
            HasPending = FALSE;
        }
        else if (!Window.TakeCommand(Command))
        {
            Command.Id = CmdNone;
        }

        switch (Command.Id)
        {
            case CmdOpen:
                Path = Command.Path;
                if (Path.empty() && !NvdBrowseForVideo(Window.GetWindow(), Path))
                    break;

                Window.ClearVideo();
                Current.reset();
                Current = NvdOpenFile(Device, Context, Opts.Player, Path.c_str());
                if (Current == nullptr ||
                    FAILED(Window.SetVideo(Current->GetDecoder().GetTexture(),
                                           Current->GetVideoFormat(),
                                           Current->GetFrameRate())))
                {
                    Current.reset();
                    MessageBoxW(Window.GetWindow(),
                                L"This file could not be decoded. The console has the details.",
                                L"NativeVideoDecode", MB_ICONERROR);
                    Window.PresentIdle(IdleMessage);
                    break;
                }

                Current->SetLooping(State.IsLooping);
                State.IsPaused = FALSE;
                State.IsFinished = FALSE;
                Shown = 0;
                break;

            case CmdClose:
                Window.ClearVideo();
                Current.reset();
                Window.PresentIdle(IdleMessage);
                break;

            case CmdExportFrame:
                if (Current == nullptr || !Current->HasDisplayedFrame())
                    break;

                Name = wcsrchr(Current->GetPath().c_str(), L'\\');
                Name = (Name != nullptr) ? Name + 1 : Current->GetPath().c_str();
                swprintf_s(Suggested, L"%.*s_frame%05llu.png",
                           (int)(wcsrchr(Name, L'.') ? wcsrchr(Name, L'.') - Name : wcslen(Name)), Name,
                           Current->GetDisplayedFrame().DisplayIndex);
                if (!NvdBrowseForExport(Window.GetWindow(), Suggested, Path))
                    break;

                hr = Window.ExportFrame(Current->GetDisplayedFrame().SurfaceIndex,
                                        Current->GetDisplayedFrame().Visible,
                                        Path.c_str());
                if (SUCCEEDED(hr))
                {
                    wprintf(L"exported frame %llu to %s\n",
                            Current->GetDisplayedFrame().DisplayIndex, Path.c_str());
                }
                else
                {
                    swprintf_s(Suggested, L"Export failed (0x%08lX).", hr);
                    MessageBoxW(Window.GetWindow(), Suggested, L"NativeVideoDecode", MB_ICONERROR);
                }
                Window.Redraw(nullptr);
                break;

            case CmdStreamInfo:
                if (Current == nullptr)
                    break;

                Info = Current->DescribeStream() + L"\n" + Current->GetStats().OverlayText();
                MessageBoxW(Window.GetWindow(), Info.c_str(), L"Stream Info", MB_ICONINFORMATION);
                break;

            case CmdExit:
                goto Done;

            case CmdPause:
                if (Current == nullptr)
                    break;
                State.IsPaused = !State.IsPaused;
                Current->SetPaused(State.IsPaused);
                break;

            case CmdStep:
                if (Current == nullptr)
                    break;
                if (!State.IsPaused)
                {
                    State.IsPaused = TRUE;
                    Current->SetPaused(TRUE);
                }
                State.WantsStep = TRUE;
                break;

            case CmdRestart:
            case CmdSeekBack:
            case CmdSeekForward:
                if (Current == nullptr)
                    break;

                if (Command.Id == CmdRestart)
                    hr = Current->SeekTo(0);
                else
                    hr = Current->SeekRelative((Command.Id == CmdSeekBack) ? -5.0 : 5.0);

                if (FAILED(hr))
                    wprintf(L"note: this container cannot seek\n");
                State.IsFinished = FALSE;

                /* While paused, show where the seek landed */
                if (State.IsPaused)
                    State.WantsStep = TRUE;
                break;

            case CmdFast:
                State.IsFast = !State.IsFast;
                break;

            case CmdLoop:
                State.IsLooping = !State.IsLooping;
                if (Current != nullptr)
                    Current->SetLooping(State.IsLooping);
                break;

            case CmdOverlay:
                State.ShowOverlay = !State.ShowOverlay;
                if (State.IsPaused || State.IsFinished)
                {
                    Overlay = (Current != nullptr) ? Current->GetStats().OverlayText() : L"";
                    Window.Redraw(State.ShowOverlay ? &Overlay : nullptr);
                }
                break;

            case CmdPrintStats:
                if (Current != nullptr)
                    Current->GetStats().PrintSummary();
                break;

            default:
                break;
        }

        if (Command.Id != CmdNone)
        {
            Checks = { State.IsPaused, State.IsFast, State.IsLooping, State.ShowOverlay, Current != nullptr };
            Window.UpdateMenu(Checks);
            NvdUpdateTitle(Window, Current.get(), State);
            continue;
        }

        if (!Window.PumpMessages())
            break;

        if (NvdNowMs() - LastTitleMs > 250)
        {
            LastTitleMs = NvdNowMs();
            NvdUpdateTitle(Window, Current.get(), State);
        }

        if (Current == nullptr || State.IsFinished || (State.IsPaused && !State.WantsStep))
        {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 30, QS_ALLINPUT);
            continue;
        }

        Result = Current->Tick(State.IsFast, State.WantsStep, &Frame, &WaitMs);
        switch (Result)
        {
            case TickResult::Wait:
                MsgWaitForMultipleObjects(0, nullptr, FALSE, WaitMs, QS_ALLINPUT);
                break;

            case TickResult::Show:
                Overlay = Current->GetStats().OverlayText();
                hr = Window.Present(Frame.SurfaceIndex,
                                    Frame.Visible,
                                    State.ShowOverlay ? &Overlay : nullptr,
                                    !State.IsFast);
                if (State.IsFast || State.WantsStep)
                    Current->GetStats().OnFramePresented(0);
                Current->FrameShown(Frame);
                State.WantsStep = FALSE;
                if (FAILED(hr) || (Opts.FrameLimit != 0 && ++Shown >= Opts.FrameLimit))
                    goto Done;
                break;

            case TickResult::Finished:
                State.IsFinished = TRUE;
                Current->GetStats().PrintSummary();
                NvdUpdateTitle(Window, Current.get(), State);
                if (Opts.FrameLimit != 0)
                    goto Done;
                break;

            case TickResult::Failed:
                Window.ClearVideo();
                Current.reset();
                MessageBoxW(Window.GetWindow(), L"Decoding failed. The console has the details.",
                            L"NativeVideoDecode", MB_ICONERROR);
                Window.PresentIdle(IdleMessage);
                break;
        }
    }

Done:
    if (Current != nullptr && !State.IsFinished)
        Current->GetStats().PrintSummary();

    /* The window's views reference the decoder's texture, so they go first */
    Window.ClearVideo();
    Current.reset();
    return 0;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    static const D3D_FEATURE_LEVEL Levels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    PFN_D3D11_CREATE_DEVICE CreateDevice;
    ComPtr<IDXGIFactory1> Factory;
    ComPtr<IDXGIAdapter1> Adapter;
    ComPtr<IDXGIAdapter1> Candidate;
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    D3D_FEATURE_LEVEL FeatureLevel;
    std::wstring RuntimePath;
    HMODULE Runtime;
    Options Opts;
    UINT Flags;
    UINT Index;
    HRESULT hr;
    int ExitCode;

    if (!NvdParseOptions(argc, argv, Opts))
    {
        fwprintf(stderr, L"%s", Usage);
        return 2;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    RuntimePath = NvdRuntimePath(Opts.Runtime);
    Runtime = LoadLibraryW(RuntimePath.c_str());
    CreateDevice = Runtime ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(Runtime, "D3D11CreateDevice"))
                           : nullptr;
    if (CreateDevice == nullptr)
    {
        fwprintf(stderr, L"error: cannot use %s (%lu)\n", RuntimePath.c_str(), GetLastError());
        return 1;
    }

    wprintf(L"NativeVideoDecode\n");
    wprintf(L"runtime: %s\n", RuntimePath.c_str());

    hr = CreateDXGIFactory1(IID_PPV_ARGS(&Factory));
    if (FAILED(hr))
    {
        fwprintf(stderr, L"error: CreateDXGIFactory1 failed 0x%08lX\n", hr);
        return 1;
    }

    wprintf(L"adapters:\n");
    for (Index = 0; Factory->EnumAdapters1(Index, &Candidate) != DXGI_ERROR_NOT_FOUND; Index++)
    {
        NvdPrintAdapter(Candidate.Get(), Index);
        if (Index == Opts.Adapter)
            Adapter = Candidate;
        Candidate.Reset();
    }

    if (Adapter == nullptr)
    {
        fwprintf(stderr, L"error: adapter %u does not exist\n", Opts.Adapter);
        return 1;
    }

    /* BGRA support is only for the Direct2D text; retry without it */
    Flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    hr = CreateDevice(Adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, NULL, Flags, Levels, _countof(Levels),
                      D3D11_SDK_VERSION, &Device, &FeatureLevel, &Context);
    if (FAILED(hr))
    {
        Flags &= ~D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        hr = CreateDevice(Adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, NULL, Flags, Levels, _countof(Levels),
                          D3D11_SDK_VERSION, &Device, &FeatureLevel, &Context);
    }
    if (FAILED(hr))
    {
        fwprintf(stderr, L"error: D3D11CreateDevice failed 0x%08lX\n", hr);
        return 1;
    }

    wprintf(L"device: feature level %X.%X, flags 0x%X, adapter %u\n",
            (FeatureLevel >> 12) & 0xF, (FeatureLevel >> 8) & 0xF, Flags, Opts.Adapter);

    timeBeginPeriod(1);
    if (Opts.Headless)
        ExitCode = NvdRunHeadless(Device.Get(), Context.Get(), Opts);
    else
        ExitCode = NvdRunWindowed(Device.Get(), Context.Get(), Opts);
    timeEndPeriod(1);

    Context.Reset();
    Device.Reset();
    CoUninitialize();
    return ExitCode;
}
