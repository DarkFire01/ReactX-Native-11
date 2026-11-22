/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     XInput answers for every entry point and argument shape, printed for comparing runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * xinputcheck [-runtime system|reactx|<path>] [-nine]
 */

#include <windows.h>
#include <xinput.h>

#include <cstdio>
#include <string>

typedef DWORD (WINAPI* PXC_GET_STATE)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI* PXC_SET_STATE)(DWORD, XINPUT_VIBRATION*);
typedef DWORD (WINAPI* PXC_GET_CAPABILITIES)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef DWORD (WINAPI* PXC_GET_CAPABILITIES_EX)(DWORD, DWORD, DWORD, BYTE*);
typedef VOID (WINAPI* PXC_ENABLE)(BOOL);
typedef DWORD (WINAPI* PXC_GET_KEYSTROKE)(DWORD, DWORD, PXINPUT_KEYSTROKE);
typedef DWORD (WINAPI* PXC_GET_BATTERY)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
typedef DWORD (WINAPI* PXC_GET_AUDIO_IDS)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);
typedef DWORD (WINAPI* PXC_GET_DSOUND_GUIDS)(DWORD, GUID*, GUID*);
typedef DWORD (WINAPI* PXC_USER)(DWORD);
typedef DWORD (WINAPI* PXC_USER_BUFFER)(DWORD, BYTE*);
typedef DWORD (WINAPI* PXC_WAIT_GUIDE)(DWORD, HANDLE, BYTE*);

static
VOID
XcBytes(
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
std::wstring
XcRuntimePath(
    _In_ PCWSTR Runtime,
    _In_ BOOLEAN Nine)
{
    PCWSTR Name = Nine ? L"xinput9_1_0.dll" : L"xinput1_4.dll";
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return Name;

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\" + Name;
    }

    return Runtime;
}

static
VOID
XcCheckState(
    _In_ HMODULE Module)
{
    PXC_GET_STATE GetState = reinterpret_cast<PXC_GET_STATE>(GetProcAddress(Module, "XInputGetState"));
    PXC_GET_STATE GetStateEx = reinterpret_cast<PXC_GET_STATE>(GetProcAddress(Module, MAKEINTRESOURCEA(100)));
    PXC_SET_STATE SetState = reinterpret_cast<PXC_SET_STATE>(GetProcAddress(Module, "XInputSetState"));

    for (DWORD User = 0; User < 6; User++)
    {
        XINPUT_STATE State;
        XINPUT_VIBRATION Vibration = { 0x1234, 0x5678 };

        memset(&State, 0xCC, sizeof(State));
        printf("getstate[%lu] = %lu\n", User, GetState(User, &State));
        XcBytes("  state", &State, sizeof(State));
        printf("getstate_null[%lu] = %lu\n", User, GetState(User, nullptr));
        printf("setstate[%lu] = %lu\n", User, SetState(User, &Vibration));
        printf("setstate_null[%lu] = %lu\n", User, SetState(User, nullptr));
        if (GetStateEx != nullptr)
        {
            memset(&State, 0xCC, sizeof(State));
            printf("getstateex[%lu] = %lu\n", User, GetStateEx(User, &State));
            XcBytes("  stateex", &State, sizeof(State));
            printf("getstateex_null[%lu] = %lu\n", User, GetStateEx(User, nullptr));
        }
    }
    printf("getstate_any = %lu\n", GetState(XUSER_INDEX_ANY, nullptr));
}

static
VOID
XcCheckCapabilities(
    _In_ HMODULE Module)
{
    PXC_GET_CAPABILITIES GetCaps = reinterpret_cast<PXC_GET_CAPABILITIES>(GetProcAddress(Module, "XInputGetCapabilities"));
    PXC_GET_CAPABILITIES_EX GetCapsEx = reinterpret_cast<PXC_GET_CAPABILITIES_EX>(GetProcAddress(Module, MAKEINTRESOURCEA(108)));

    for (DWORD User = 0; User < 5; User++)
    {
        for (DWORD Flags = 0; Flags < 4; Flags++)
        {
            XINPUT_CAPABILITIES Caps;

            memset(&Caps, 0xCC, sizeof(Caps));
            printf("caps[%lu][%lu] = %lu\n", User, Flags, GetCaps(User, Flags, &Caps));
            XcBytes("  caps", &Caps, sizeof(Caps));
        }
        printf("caps_null[%lu] = %lu\n", User, GetCaps(User, 0, nullptr));
        if (GetCapsEx != nullptr)
        {
            for (DWORD Revision = 0; Revision < 3; Revision++)
            {
                BYTE Caps[32];

                memset(Caps, 0xCC, sizeof(Caps));
                printf("capsex[%lu][%lu] = %lu\n", User, Revision, GetCapsEx(Revision, User, 0, Caps));
                XcBytes("  capsex", Caps, sizeof(Caps));
            }
            printf("capsex_flags[%lu] = %lu\n", User, GetCapsEx(1, User, 2, nullptr));
            printf("capsex_null[%lu] = %lu\n", User, GetCapsEx(1, User, 0, nullptr));
        }
    }
}

static
VOID
XcCheckKeystroke(
    _In_ HMODULE Module)
{
    PXC_GET_KEYSTROKE GetKeystroke = reinterpret_cast<PXC_GET_KEYSTROKE>(GetProcAddress(Module, "XInputGetKeystroke"));

    if (GetKeystroke == nullptr)
        return;
    for (DWORD User : { 0ul, 1ul, 3ul, 4ul, 5ul, (DWORD)XUSER_INDEX_ANY })
    {
        XINPUT_KEYSTROKE Keystroke;

        memset(&Keystroke, 0xCC, sizeof(Keystroke));
        printf("keystroke[%lu] = %lu\n", User, GetKeystroke(User, 0, &Keystroke));
        XcBytes("  keystroke", &Keystroke, sizeof(Keystroke));
        printf("keystroke_null[%lu] = %lu\n", User, GetKeystroke(User, 0, nullptr));
    }
}

static
VOID
XcCheckBattery(
    _In_ HMODULE Module)
{
    PXC_GET_BATTERY GetBattery = reinterpret_cast<PXC_GET_BATTERY>(GetProcAddress(Module, "XInputGetBatteryInformation"));

    if (GetBattery == nullptr)
        return;
    for (DWORD User = 0; User < 5; User++)
    {
        for (BYTE Type = 0; Type < 3; Type++)
        {
            XINPUT_BATTERY_INFORMATION Battery;

            memset(&Battery, 0xCC, sizeof(Battery));
            printf("battery[%lu][%u] = %lu\n", User, Type, GetBattery(User, Type, &Battery));
            XcBytes("  battery", &Battery, sizeof(Battery));
        }
        printf("battery_null[%lu] = %lu\n", User, GetBattery(User, 0, nullptr));
    }
}

static
VOID
XcCheckAudio(
    _In_ HMODULE Module)
{
    PXC_GET_AUDIO_IDS GetIds = reinterpret_cast<PXC_GET_AUDIO_IDS>(GetProcAddress(Module, "XInputGetAudioDeviceIds"));
    PXC_GET_DSOUND_GUIDS GetGuids = reinterpret_cast<PXC_GET_DSOUND_GUIDS>(GetProcAddress(Module, "XInputGetDSoundAudioDeviceGuids"));

    if (GetIds != nullptr)
    {
        for (DWORD User = 0; User < 5; User++)
        {
            WCHAR Render[64];
            WCHAR Capture[64];
            UINT RenderCount = 64;
            UINT CaptureCount = 64;

            memset(Render, 0xCC, sizeof(Render));
            memset(Capture, 0xCC, sizeof(Capture));
            printf("audio[%lu] = %lu\n", User, GetIds(User, Render, &RenderCount, Capture, &CaptureCount));
            printf("  counts = %u %u first %04X %04X\n", RenderCount, CaptureCount, Render[0], Capture[0]);
            RenderCount = 64;
            printf("audio_render[%lu] = %lu count %u\n", User, GetIds(User, nullptr, &RenderCount, nullptr, nullptr), RenderCount);
            printf("audio_none[%lu] = %lu\n", User, GetIds(User, nullptr, nullptr, nullptr, nullptr));
            printf("audio_badrender[%lu] = %lu\n", User, GetIds(User, Render, nullptr, nullptr, &CaptureCount));
            printf("audio_badcapture[%lu] = %lu\n", User, GetIds(User, nullptr, &RenderCount, Capture, nullptr));
        }
    }
    if (GetGuids != nullptr)
    {
        for (DWORD User = 0; User < 5; User++)
        {
            GUID Render;
            GUID Capture;

            memset(&Render, 0xCC, sizeof(Render));
            memset(&Capture, 0xCC, sizeof(Capture));
            printf("dsound[%lu] = %lu\n", User, GetGuids(User, &Render, &Capture));
            XcBytes("  render", &Render, sizeof(Render));
            printf("dsound_null[%lu] = %lu\n", User, GetGuids(User, nullptr, &Capture));
        }
    }
}

static
VOID
XcCheckPrivate(
    _In_ HMODULE Module)
{
    PXC_USER PowerOff = reinterpret_cast<PXC_USER>(GetProcAddress(Module, MAKEINTRESOURCEA(103)));
    PXC_USER Cancel = reinterpret_cast<PXC_USER>(GetProcAddress(Module, MAKEINTRESOURCEA(102)));
    PXC_USER_BUFFER BaseBus = reinterpret_cast<PXC_USER_BUFFER>(GetProcAddress(Module, MAKEINTRESOURCEA(104)));
    PXC_USER_BUFFER SystemButtons = reinterpret_cast<PXC_USER_BUFFER>(GetProcAddress(Module, MAKEINTRESOURCEA(109)));
    PXC_WAIT_GUIDE WaitGuide = reinterpret_cast<PXC_WAIT_GUIDE>(GetProcAddress(Module, MAKEINTRESOURCEA(101)));

    if (PowerOff == nullptr)
        return;
    for (DWORD User = 0; User < 5; User++)
    {
        BYTE Buffer[64];

        printf("poweroff[%lu] = %lu\n", User, PowerOff(User));
        printf("cancelguide[%lu] = %lu\n", User, Cancel(User));
        memset(Buffer, 0xCC, sizeof(Buffer));
        printf("systembuttons[%lu] = %lu\n", User, SystemButtons(User, Buffer));
        XcBytes("  systembuttons", Buffer, 32);
        printf("systembuttons_null[%lu] = %lu\n", User, SystemButtons(User, nullptr));
        memset(Buffer, 0xCC, sizeof(Buffer));
        printf("waitguide[%lu] = %lu\n", User, WaitGuide(User, NULL, Buffer));
        XcBytes("  waitguide", Buffer, 20);
        printf("waitguide_null[%lu] = %lu\n", User, WaitGuide(User, NULL, nullptr));
    }
    for (DWORD Bus : { 0ul, 1ul, 15ul, 16ul })
    {
        BYTE Buffer[64];

        memset(Buffer, 0xCC, sizeof(Buffer));
        printf("basebus[%lu] = %lu\n", Bus, BaseBus(Bus, Buffer));
        XcBytes("  basebus", Buffer, 16);
        printf("basebus_null[%lu] = %lu\n", Bus, BaseBus(Bus, nullptr));
    }
}

static
VOID
XcCheckEnable(
    _In_ HMODULE Module)
{
    PXC_ENABLE Enable = reinterpret_cast<PXC_ENABLE>(GetProcAddress(Module, "XInputEnable"));
    PXC_GET_STATE GetState = reinterpret_cast<PXC_GET_STATE>(GetProcAddress(Module, "XInputGetState"));

    if (Enable == nullptr)
        return;
    Enable(FALSE);
    for (DWORD User = 0; User < 4; User++)
    {
        XINPUT_STATE State;

        memset(&State, 0xCC, sizeof(State));
        printf("disabled_getstate[%lu] = %lu\n", User, GetState(User, &State));
    }
    Enable(TRUE);
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    PCWSTR Runtime = L"system";
    BOOLEAN Nine = FALSE;
    HMODULE Module;

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
            Runtime = argv[++Index];
        else if (_wcsicmp(argv[Index], L"-nine") == 0)
            Nine = TRUE;
    }

    Module = LoadLibraryW(XcRuntimePath(Runtime, Nine).c_str());
    if (Module == NULL)
    {
        fprintf(stderr, "error: cannot load the runtime (%lu)\n", GetLastError());
        return 1;
    }
    {
        WCHAR Path[MAX_PATH];

        GetModuleFileNameW(Module, Path, _countof(Path));
        fprintf(stderr, "runtime %ls\n", Path);
    }

    XcCheckState(Module);
    XcCheckCapabilities(Module);
    XcCheckKeystroke(Module);
    XcCheckBattery(Module);
    XcCheckAudio(Module);
    XcCheckPrivate(Module);
    XcCheckEnable(Module);
    FreeLibrary(Module);
    return 0;
}
