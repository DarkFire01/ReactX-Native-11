/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runs API behavior checks against one Direct3D 11 runtime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * apicheck [-runtime system|reactx|<path>] [check ...]
 * apicheck -list
 */

#include "check.hpp"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

VOID
CheckContext::Report(
    _In_z_ PCSTR Key,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    va_list Arguments;

    printf("%s.%s = ", CheckName, Key);
    va_start(Arguments, Format);
    vprintf(Format, Arguments);
    va_end(Arguments);
    printf("\n");
}

BOOLEAN
CheckContext::Expect(
    _In_ BOOLEAN Condition,
    _In_z_ PCSTR What)
{
    if (!Condition)
    {
        printf("FAIL %s: %s\n", CheckName, What);
        Failures++;
    }

    return Condition;
}

ULONG
AcRefCount(
    _In_ IUnknown* Object)
{
    Object->AddRef();
    return Object->Release();
}

static
std::wstring
AcRuntimePath(
    _In_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"d3d11.dll";

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\d3d11.dll";
    }

    return Runtime;
}

static
std::vector<CheckInfo>
AcAllChecks(VOID)
{
    std::vector<CheckInfo> All;
    const CheckInfo* Table;
    UINT Count;

    Table = AcStateChecks(&Count);
    All.insert(All.end(), Table, Table + Count);
    Table = AcComputeChecks(&Count);
    All.insert(All.end(), Table, Table + Count);
    Table = AcFormatChecks(&Count);
    All.insert(All.end(), Table, Table + Count);
    Table = AcInterfaceChecks(&Count);
    All.insert(All.end(), Table, Table + Count);
    return All;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    static const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    std::vector<CheckInfo> Checks = AcAllChecks();
    std::vector<std::string> Wanted;
    PFN_D3D11_CREATE_DEVICE CreateDevice;
    PCWSTR Runtime = L"system";
    CheckContext Check;
    HMODULE Module;
    UINT Failures = 0;
    int Index;
    char Name[128];

    /* A crash in a check must not take the facts already printed with it */
    setvbuf(stdout, nullptr, _IONBF, 0);

    for (Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-list") == 0)
        {
            for (const CheckInfo& Info : Checks)
                printf("%s\n", Info.Name);
            return 0;
        }

        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Runtime = argv[++Index];
            continue;
        }

        WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
        Wanted.push_back(Name);
    }

    Module = LoadLibraryW(AcRuntimePath(Runtime).c_str());
    CreateDevice = Module ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"))
                          : nullptr;
    if (CreateDevice == nullptr)
    {
        fprintf(stderr, "error: cannot load the runtime\n");
        return 1;
    }

    /* Every check gets a fresh device, so one cannot leave state behind for the next */
    for (const CheckInfo& Info : Checks)
    {
        ComPtr<ID3D11Device> Device;
        ComPtr<ID3D11DeviceContext> Context;
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Each : Wanted)
            IsWanted = IsWanted || Each == Info.Name;
        if (!IsWanted)
            continue;

        if (FAILED(CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, Levels, _countof(Levels),
                                D3D11_SDK_VERSION, &Device, nullptr, &Context)))
        {
            fprintf(stderr, "error: D3D11CreateDevice failed\n");
            return 1;
        }

        Check = {};
        Check.Device = Device.Get();
        Check.Context = Context.Get();
        Check.CheckName = Info.Name;
        Info.Run(Check);
        Failures += Check.Failures;
    }

    printf("failures = %u\n", Failures);
    return Failures ? 1 : 0;
}
