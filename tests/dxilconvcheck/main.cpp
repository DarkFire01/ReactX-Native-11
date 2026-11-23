/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXBC to DXIL converter answers and output containers, printed for comparing runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * dxilconvcheck [-runtime system|reactx|<path>] [-dump <dir>]
 */

#include <windows.h>
#include <objbase.h>
#include <d3dcompiler.h>
#include <dxcapi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "corpus.hpp"

/* The converter interface as the D3D12 runtime and the translation layers call it */
struct __declspec(uuid("5F956ED5-78D1-4B15-8247-F7187614A041")) IDxbcConverter : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Convert(
        _In_reads_bytes_(DxbcSize) LPCVOID Dxbc,
        _In_ UINT32 DxbcSize,
        _In_opt_z_ LPCWSTR ExtraOptions,
        _Outptr_result_bytebuffer_maybenull_(*DxilSize) LPVOID* Dxil,
        _Out_ UINT32* DxilSize,
        _Outptr_opt_result_maybenull_z_ LPWSTR* Diag) = 0;

    virtual HRESULT STDMETHODCALLTYPE ConvertInDriver(
        _In_reads_(8) const UINT32* Bytecode,
        _In_opt_ LPCVOID InputSignature,
        _In_ UINT32 InputCount,
        _In_opt_ LPCVOID OutputSignature,
        _In_ UINT32 OutputCount,
        _In_opt_ LPCVOID PatchSignature,
        _In_ UINT32 PatchCount,
        _In_opt_z_ LPCWSTR ExtraOptions,
        _Out_ IDxcBlob** Blob,
        _Outptr_opt_result_maybenull_z_ LPWSTR* Diag) = 0;
};

static const CLSID DcClsidConverter = { 0x4900391E, 0xB752, 0x4EDD, { 0xA8, 0x85, 0x6F, 0xB7, 0x6E, 0x25, 0xAD, 0xDB } };

static DxcCreateInstanceProc DcCreate;
static DxcCreateInstance2Proc DcCreate2;
static PCWSTR DcDumpDirectory;

/* FNV-1a over a byte range, so equal output prints an equal value */
static
ULONG
DcHash(
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    ULONG Hash = 0x811C9DC5;

    for (SIZE_T Index = 0; Index < Size; Index++)
    {
        Hash ^= Bytes[Index];
        Hash *= 0x01000193;
    }

    return Hash;
}

static
VOID
DcFourCC(
    _In_ ULONG Code,
    _Out_writes_(5) PSTR Text)
{
    for (ULONG Index = 0; Index < 4; Index++)
    {
        CHAR Letter = static_cast<CHAR>((Code >> (Index * 8)) & 0xFF);
        Text[Index] = (Letter >= 0x20 && Letter < 0x7F) ? Letter : '?';
    }
    Text[4] = '\0';
}

/* Prints the container header and every part with its size and content hash */
static
VOID
DcPrintContainer(
    _In_z_ PCSTR Label,
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    ULONG PartCount;

    printf("  %s size=%zu", Label, Size);
    if (Data == nullptr || Size < 32)
    {
        printf("\n");
        return;
    }

    printf(" fnv=%08lX magic=%08lX digest=", DcHash(Data, Size), *reinterpret_cast<const ULONG*>(Bytes));
    for (ULONG Index = 4; Index < 20; Index++)
        printf("%02X", Bytes[Index]);

    PartCount = *reinterpret_cast<const ULONG*>(Bytes + 28);
    printf(" version=%u.%u total=%lu parts=%lu\n",
           *reinterpret_cast<const USHORT*>(Bytes + 20),
           *reinterpret_cast<const USHORT*>(Bytes + 22),
           *reinterpret_cast<const ULONG*>(Bytes + 24),
           PartCount);

    for (ULONG Part = 0; Part < PartCount && 32 + (Part + 1) * 4 <= Size; Part++)
    {
        ULONG Offset = *reinterpret_cast<const ULONG*>(Bytes + 32 + Part * 4);
        ULONG Code;
        ULONG PartSize;
        CHAR Name[5];

        if (Offset + 8 > Size)
        {
            printf("    part %lu offset=%lu out of range\n", Part, Offset);
            continue;
        }

        Code = *reinterpret_cast<const ULONG*>(Bytes + Offset);
        PartSize = *reinterpret_cast<const ULONG*>(Bytes + Offset + 4);
        DcFourCC(Code, Name);
        printf("    part %lu %s offset=%lu size=%lu", Part, Name, Offset, PartSize);
        if (Offset + 8 + static_cast<SIZE_T>(PartSize) <= Size)
            printf(" fnv=%08lX", DcHash(Bytes + Offset + 8, PartSize));
        printf("\n");
    }
}

static
VOID
DcPrintDiag(
    _In_opt_z_ PCWSTR Diag)
{
    if (Diag == nullptr)
    {
        printf("  diag=null\n");
        return;
    }

    printf("  diag=\"");
    for (PCWSTR Cursor = Diag; *Cursor != UNICODE_NULL; Cursor++)
    {
        if (*Cursor == L'\n')
            printf("\\n");
        else if (*Cursor < 0x80)
            printf("%c", static_cast<char>(*Cursor));
        else
            printf("\\u%04X", *Cursor);
    }
    printf("\"\n");
}

static
VOID
DcDump(
    _In_z_ PCSTR Name,
    _In_z_ PCSTR Suffix,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;
    DWORD Written;

    if (DcDumpDirectory == nullptr)
        return;

    _snwprintf_s(Path, _countof(Path), _TRUNCATE, L"%s\\%S.%S", DcDumpDirectory, Name, Suffix);
    File = CreateFileW(Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File == INVALID_HANDLE_VALUE)
        return;

    WriteFile(File, Data, static_cast<DWORD>(Size), &Written, nullptr);
    CloseHandle(File);
}

/* Calls Convert and turns a fault inside it into a printable code */
static
HRESULT
DcGuardedConvert(
    _In_ IDxbcConverter* Converter,
    _In_reads_bytes_opt_(Size) const VOID* Dxbc,
    _In_ UINT32 Size,
    _In_opt_z_ PCWSTR Options,
    _Out_opt_ LPVOID* Dxil,
    _Out_opt_ UINT32* DxilSize,
    _Out_opt_ LPWSTR* Diag,
    _Out_ PULONG Fault)
{
    *Fault = 0;
    __try
    {
        return Converter->Convert(Dxbc, Size, Options, Dxil, DxilSize, Diag);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *Fault = GetExceptionCode();
    }

    return E_UNEXPECTED;
}

static
IDxbcConverter*
DcNewConverter(VOID)
{
    IDxbcConverter* Converter = nullptr;

    if (FAILED(DcCreate(DcClsidConverter, __uuidof(IDxbcConverter), reinterpret_cast<void**>(&Converter))))
        return nullptr;

    return Converter;
}

/* Runs Convert once and prints what came back; the output is freed with CoTaskMemFree */
static
VOID
DcConvert(
    _In_z_ PCSTR Label,
    _In_reads_bytes_opt_(Size) const VOID* Dxbc,
    _In_ UINT32 Size,
    _In_opt_z_ PCWSTR Options,
    _In_ BOOLEAN WantDiag)
{
    IDxbcConverter* Converter = DcNewConverter();
    LPVOID Dxil = reinterpret_cast<LPVOID>(1);
    UINT32 DxilSize = 0xCDCDCDCD;
    LPWSTR Diag = reinterpret_cast<LPWSTR>(1);
    ULONG Fault;
    HRESULT hr;

    if (Converter == nullptr)
    {
        printf("%s: no converter\n", Label);
        return;
    }

    hr = DcGuardedConvert(Converter, Dxbc, Size, Options, &Dxil, &DxilSize, WantDiag ? &Diag : nullptr, &Fault);
    if (Fault != 0)
    {
        printf("%s: fault=%08lX\n", Label, Fault);
        return;
    }

    printf("%s: hr=%08lX dxil=%s size=%u\n",
           Label,
           hr,
           Dxil == nullptr ? "null" : (Dxil == reinterpret_cast<LPVOID>(1) ? "untouched" : "set"),
           DxilSize);

    if (Dxil != nullptr && Dxil != reinterpret_cast<LPVOID>(1))
    {
        DcPrintContainer("out", Dxil, DxilSize);
        DcDump(Label, "dxil", Dxil, DxilSize);
        CoTaskMemFree(Dxil);
    }

    if (WantDiag)
    {
        if (Diag == reinterpret_cast<LPWSTR>(1))
        {
            printf("  diag=untouched\n");
        }
        else
        {
            DcPrintDiag(Diag);
            CoTaskMemFree(Diag);
        }
    }

    Converter->Release();
}

static
VOID
DcCheckFactory(VOID)
{
    static const IID Unknown = __uuidof(IUnknown);
    static const IID Converter = __uuidof(IDxbcConverter);
    static const IID Blob = __uuidof(IDxcBlob);
    static const IID Marshal = { 0x00000003, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    static const IID NoMarshal = __uuidof(INoMarshal);
    static const IID Random = { 0x12345678, 0x1234, 0x5678, { 0x9A, 0xBC, 0xDE, 0xF0, 0x12, 0x34, 0x56, 0x78 } };
    const struct
    {
        PCSTR Name;
        const CLSID* Class;
        const IID* Interface;
    } Cases[] =
    {
        { "converter/converter", &DcClsidConverter, &Converter },
        { "converter/unknown", &DcClsidConverter, &Unknown },
        { "converter/blob", &DcClsidConverter, &Blob },
        { "converter/marshal", &DcClsidConverter, &Marshal },
        { "converter/nomarshal", &DcClsidConverter, &NoMarshal },
        { "converter/random", &DcClsidConverter, &Random },
        { "compiler/unknown", &CLSID_DxcCompiler, &Unknown },
        { "validator/unknown", &CLSID_DxcValidator, &Unknown },
        { "library/unknown", &CLSID_DxcLibrary, &Unknown },
        { "random/unknown", &Random, &Unknown },
    };
    IUnknown* Object;
    HRESULT hr;

    for (ULONG Index = 0; Index < _countof(Cases); Index++)
    {
        Object = reinterpret_cast<IUnknown*>(1);
        hr = DcCreate(*Cases[Index].Class, *Cases[Index].Interface, reinterpret_cast<void**>(&Object));
        printf("create %s: hr=%08lX out=%s\n",
               Cases[Index].Name,
               hr,
               Object == nullptr ? "null" : (Object == reinterpret_cast<IUnknown*>(1) ? "untouched" : "set"));
        if (SUCCEEDED(hr) && Object != nullptr)
            Object->Release();

        if (DcCreate2 == nullptr)
            continue;

        Object = reinterpret_cast<IUnknown*>(1);
        hr = DcCreate2(nullptr, *Cases[Index].Class, *Cases[Index].Interface, reinterpret_cast<void**>(&Object));
        printf("create2 %s: hr=%08lX out=%s\n",
               Cases[Index].Name,
               hr,
               Object == nullptr ? "null" : (Object == reinterpret_cast<IUnknown*>(1) ? "untouched" : "set"));
        if (SUCCEEDED(hr) && Object != nullptr)
            Object->Release();
    }

    IMalloc* Malloc = nullptr;
    if (SUCCEEDED(CoGetMalloc(1, &Malloc)))
    {
        Object = nullptr;
        hr = DcCreate2(Malloc, DcClsidConverter, Converter, reinterpret_cast<void**>(&Object));
        printf("create2 malloc: hr=%08lX out=%s\n", hr, Object == nullptr ? "null" : "set");
        if (SUCCEEDED(hr) && Object != nullptr)
            Object->Release();
        Malloc->Release();
    }

    IDxbcConverter* Instance = DcNewConverter();
    if (Instance == nullptr)
        return;

    ULONG Counts[4];

    Counts[0] = Instance->AddRef();
    Counts[1] = Instance->AddRef();
    Counts[2] = Instance->Release();
    Counts[3] = Instance->Release();
    printf("addref=%lu addref=%lu release=%lu release=%lu\n", Counts[0], Counts[1], Counts[2], Counts[3]);

    const struct
    {
        PCSTR Name;
        const IID* Interface;
    } Queries[] =
    {
        { "unknown", &Unknown },
        { "converter", &Converter },
        { "blob", &Blob },
        { "marshal", &Marshal },
        { "nomarshal", &NoMarshal },
        { "random", &Random },
    };

    for (ULONG Index = 0; Index < _countof(Queries); Index++)
    {
        Object = reinterpret_cast<IUnknown*>(1);
        hr = Instance->QueryInterface(*Queries[Index].Interface, reinterpret_cast<void**>(&Object));
        printf("query %s: hr=%08lX out=%s same=%d\n",
               Queries[Index].Name,
               hr,
               Object == nullptr ? "null" : (Object == reinterpret_cast<IUnknown*>(1) ? "untouched" : "set"),
               Object == static_cast<IUnknown*>(Instance));
        if (SUCCEEDED(hr) && Object != nullptr)
            Object->Release();
    }

    printf("final release=%lu\n", Instance->Release());
}

static
BOOLEAN
DcCompile(
    _In_ const DC_SHADER* Shader,
    _Out_ ID3DBlob** Code)
{
    ID3DBlob* Errors = nullptr;
    HRESULT hr;

    *Code = nullptr;
    hr = D3DCompile(Shader->Source,
                    strlen(Shader->Source),
                    Shader->Name,
                    nullptr,
                    nullptr,
                    Shader->Entry,
                    Shader->Profile,
                    0,
                    0,
                    Code,
                    &Errors);
    if (Errors != nullptr)
    {
        if (FAILED(hr))
            printf("%s: compile errors %s\n", Shader->Name, static_cast<PCSTR>(Errors->GetBufferPointer()));
        Errors->Release();
    }

    if (FAILED(hr))
    {
        printf("%s: compile hr=%08lX\n", Shader->Name, hr);
        return FALSE;
    }

    return TRUE;
}

/* Bad input shapes: missing pointers, short sizes, broken headers */
static
VOID
DcCheckArguments(
    _In_ ID3DBlob* Good)
{
    const BYTE* Source = static_cast<const BYTE*>(Good->GetBufferPointer());
    UINT32 Size = static_cast<UINT32>(Good->GetBufferSize());
    std::vector<BYTE> Copy(Source, Source + Size);
    IDxbcConverter* Converter;
    LPVOID Dxil;
    UINT32 DxilSize;
    ULONG Fault;
    HRESULT hr;

    DcConvert("args null data", nullptr, Size, nullptr, TRUE);
    DcConvert("args zero size", Source, 0, nullptr, TRUE);
    DcConvert("args header only", Source, 32, nullptr, TRUE);
    DcConvert("args short", Source, Size - 4, nullptr, TRUE);
    DcConvert("args long", Copy.data(), Size, nullptr, FALSE);

    Copy[0] = 'X';
    DcConvert("args bad magic", Copy.data(), Size, nullptr, TRUE);
    Copy[0] = Source[0];

    Copy[4] ^= 0xFF;
    DcConvert("args bad digest", Copy.data(), Size, nullptr, TRUE);
    Copy[4] = Source[4];

    Copy[20] = 2;
    DcConvert("args bad version", Copy.data(), Size, nullptr, TRUE);
    Copy[20] = Source[20];

    *reinterpret_cast<ULONG*>(&Copy[28]) = 0;
    DcConvert("args no parts", Copy.data(), Size, nullptr, TRUE);
    memcpy(Copy.data(), Source, Size);

    /* With the digest check off the layout and part checks are reached */
    Copy[4] ^= 0xFF;
    DcConvert("nohash bad digest", Copy.data(), Size, L"-disableHashCheck", TRUE);
    Copy[4] = Source[4];

    Copy[20] = 2;
    DcConvert("nohash bad version", Copy.data(), Size, L"-disableHashCheck", TRUE);
    Copy[20] = Source[20];

    *reinterpret_cast<ULONG*>(&Copy[28]) = 0;
    DcConvert("nohash no parts", Copy.data(), Size, L"-disableHashCheck", TRUE);
    memcpy(Copy.data(), Source, Size);

    *reinterpret_cast<ULONG*>(&Copy[32]) += 4;
    DcConvert("nohash part gap", Copy.data(), Size, L"-disableHashCheck", TRUE);
    memcpy(Copy.data(), Source, Size);

    *reinterpret_cast<ULONG*>(&Copy[24]) -= 4;
    DcConvert("nohash short total", Copy.data(), Size, L"-disableHashCheck", TRUE);
    DcConvert("nohash short both", Copy.data(), Size - 4, L"-disableHashCheck", TRUE);
    memcpy(Copy.data(), Source, Size);

    const ULONG Renames[] =
    {
        DXC_FOURCC('S', 'H', 'D', 'R'),
        DXC_FOURCC('S', 'H', 'E', 'X'),
        DXC_FOURCC('I', 'S', 'G', 'N'),
        DXC_FOURCC('O', 'S', 'G', 'N'),
        DXC_FOURCC('R', 'D', 'E', 'F'),
        DXC_FOURCC('S', 'T', 'A', 'T'),
    };
    ULONG Parts = *reinterpret_cast<const ULONG*>(Source + 28);

    for (ULONG Rename = 0; Rename < _countof(Renames); Rename++)
    {
        CHAR Name[5];
        CHAR Label[64];

        for (ULONG Part = 0; Part < Parts; Part++)
        {
            ULONG Offset = *reinterpret_cast<const ULONG*>(Source + 32 + Part * 4);

            if (*reinterpret_cast<const ULONG*>(Source + Offset) == Renames[Rename])
                *reinterpret_cast<ULONG*>(&Copy[Offset]) = DXC_FOURCC('X', 'X', 'X', 'X');
        }

        DcFourCC(Renames[Rename], Name);
        sprintf_s(Label, "nohash without %s", Name);
        DcConvert(Label, Copy.data(), Size, L"-disableHashCheck", TRUE);
        memcpy(Copy.data(), Source, Size);
    }

    DcConvert("args options empty", Source, Size, L"", TRUE);
    DcConvert("args options unknown", Source, Size, L"-bogus", TRUE);
    DcConvert("args options disable opt", Source, Size, L"-disableOptimizations", TRUE);
    DcConvert("args no diag", Source, Size, nullptr, FALSE);

    Converter = DcNewConverter();
    if (Converter == nullptr)
        return;

    DxilSize = 0xCDCDCDCD;
    hr = DcGuardedConvert(Converter, Source, Size, nullptr, nullptr, &DxilSize, nullptr, &Fault);
    printf("args null out: hr=%08lX fault=%08lX size=%u\n", hr, Fault, DxilSize);

    Dxil = reinterpret_cast<LPVOID>(1);
    hr = DcGuardedConvert(Converter, Source, Size, nullptr, &Dxil, nullptr, nullptr, &Fault);
    printf("args null size: hr=%08lX fault=%08lX dxil=%s\n",
           hr,
           Fault,
           Dxil == nullptr ? "null" : (Dxil == reinterpret_cast<LPVOID>(1) ? "untouched" : "set"));
    if (Dxil != nullptr && Dxil != reinterpret_cast<LPVOID>(1))
        CoTaskMemFree(Dxil);

    Converter->Release();
}

/* One signature element the way the D3D12 runtime hands it to a driver (D3D12DDIARG_SIGNATURE_ENTRY_0012) */
typedef struct _DC_DDI_ELEMENT
{
    ULONG SystemValue;
    ULONG Register;
    BYTE Mask;
    BYTE Stream;
    ULONG ComponentType;
    ULONG MinPrecision;
} DC_DDI_ELEMENT;

/* The signature arrays for one shader, null pointers where a signature is left out */
typedef struct _DC_DDI_SIGNATURES
{
    const DC_DDI_ELEMENT* Input;
    UINT32 InputCount;
    const DC_DDI_ELEMENT* Output;
    UINT32 OutputCount;
    const DC_DDI_ELEMENT* Patch;
    UINT32 PatchCount;
} DC_DDI_SIGNATURES;

/* Calls ConvertInDriver and turns a fault inside it into a printable code */
static
HRESULT
DcGuardedDriverConvert(
    _In_ IDxbcConverter* Converter,
    _In_opt_ const UINT32* Bytecode,
    _In_ const DC_DDI_SIGNATURES* Signatures,
    _In_opt_z_ PCWSTR Options,
    _Out_opt_ IDxcBlob** Blob,
    _Out_opt_ LPWSTR* Diag,
    _Out_ PULONG Fault)
{
    *Fault = 0;
    __try
    {
        return Converter->ConvertInDriver(Bytecode,
                                          Signatures->Input,
                                          Signatures->InputCount,
                                          Signatures->Output,
                                          Signatures->OutputCount,
                                          Signatures->Patch,
                                          Signatures->PatchCount,
                                          Options,
                                          Blob,
                                          Diag);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *Fault = GetExceptionCode();
    }

    return E_UNEXPECTED;
}

/* The driver entry point takes a bare token stream and signature arrays */
static
VOID
DcCheckDriverArguments(
    _In_ ID3DBlob* Good)
{
    const BYTE* Source = static_cast<const BYTE*>(Good->GetBufferPointer());
    ULONG Parts = *reinterpret_cast<const ULONG*>(Source + 28);
    const UINT32* Program = nullptr;
    UINT32 Short[2] = { 0, 1 };
    DC_DDI_SIGNATURES NoSignatures = {};
    const struct
    {
        PCSTR Name;
        const UINT32* Bytecode;
        BOOLEAN WantBlob;
    } Cases[] =
    {
        { "driver null bytecode", nullptr, TRUE },
        { "driver null blob", Short, FALSE },
        { "driver length one", Short, TRUE },
    };

    for (ULONG Part = 0; Part < Parts; Part++)
    {
        ULONG Offset = *reinterpret_cast<const ULONG*>(Source + 32 + Part * 4);
        ULONG Code = *reinterpret_cast<const ULONG*>(Source + Offset);

        if (Code == DXC_FOURCC('S', 'H', 'D', 'R') || Code == DXC_FOURCC('S', 'H', 'E', 'X'))
            Program = reinterpret_cast<const UINT32*>(Source + Offset + 8);
    }

    for (ULONG Index = 0; Index < _countof(Cases) + 1; Index++)
    {
        IDxbcConverter* Converter = DcNewConverter();
        IDxcBlob* Blob = reinterpret_cast<IDxcBlob*>(1);
        LPWSTR Diag = reinterpret_cast<LPWSTR>(1);
        PCSTR Name = Index < _countof(Cases) ? Cases[Index].Name : "driver no signatures";
        const UINT32* Bytecode = Index < _countof(Cases) ? Cases[Index].Bytecode : Program;
        BOOLEAN WantBlob = Index < _countof(Cases) ? Cases[Index].WantBlob : TRUE;
        ULONG Fault;
        HRESULT hr;

        if (Converter == nullptr)
            return;

        hr = DcGuardedDriverConvert(Converter, Bytecode, &NoSignatures, nullptr, WantBlob ? &Blob : nullptr, &Diag, &Fault);
        printf("%s: hr=%08lX fault=%08lX blob=%s diag=%s\n",
               Name,
               hr,
               Fault,
               Blob == nullptr ? "null" : (Blob == reinterpret_cast<IDxcBlob*>(1) ? "untouched" : "set"),
               Diag == nullptr ? "null" : (Diag == reinterpret_cast<LPWSTR>(1) ? "untouched" : "set"));

        if (Fault == 0 && SUCCEEDED(hr) && Blob != nullptr && Blob != reinterpret_cast<IDxcBlob*>(1))
        {
            printf("  blob size=%zu fnv=%08lX\n", Blob->GetBufferSize(), DcHash(Blob->GetBufferPointer(), Blob->GetBufferSize()));
            Blob->Release();
        }

        Converter->Release();
    }
}

/* Finds the first part with one of the codes, in preference order */
static
const BYTE*
DcFindPart(
    _In_ ID3DBlob* Code,
    _In_reads_(Count) const ULONG* Codes,
    _In_ ULONG Count,
    _Out_ PULONG Found,
    _Out_ PULONG Size)
{
    const BYTE* Source = static_cast<const BYTE*>(Code->GetBufferPointer());
    ULONG Parts = *reinterpret_cast<const ULONG*>(Source + 28);

    for (ULONG Choice = 0; Choice < Count; Choice++)
    {
        for (ULONG Part = 0; Part < Parts; Part++)
        {
            ULONG Offset = *reinterpret_cast<const ULONG*>(Source + 32 + Part * 4);

            if (*reinterpret_cast<const ULONG*>(Source + Offset) != Codes[Choice])
                continue;

            *Found = Codes[Choice];
            *Size = *reinterpret_cast<const ULONG*>(Source + Offset + 4);
            return Source + Offset + 8;
        }
    }

    *Found = 0;
    *Size = 0;
    return nullptr;
}

/* D3D_NAME to the token name the DDI uses; tessellation factors split per component there */
static
ULONG
DcDdiSystemValue(
    _In_ ULONG Name,
    _In_ ULONG SemanticIndex)
{
    switch (Name)
    {
    case 11:
        return 11 + SemanticIndex;
    case 12:
        return 15 + SemanticIndex;
    case 13:
        return 17 + SemanticIndex;
    case 14:
        return 20;
    case 15:
        return 21;
    case 16:
        return 22;
    default:
        return Name < 64 ? Name : 0;
    }
}

/**
 * Turns a container signature part into DDI elements. ISG1, OSG1 and PSG1 rows are 32 bytes
 * with a leading stream and a trailing min precision; OSG5 rows are 28 with the stream only.
 */
static
VOID
DcReadSignature(
    _In_ ID3DBlob* Code,
    _In_reads_(Count) const ULONG* Codes,
    _In_ ULONG Count,
    _Out_ std::vector<DC_DDI_ELEMENT>& Elements)
{
    const BYTE* Data;
    ULONG Found;
    ULONG Size;
    ULONG Stride;
    ULONG Base;
    ULONG Rows;

    Elements.clear();
    Data = DcFindPart(Code, Codes, Count, &Found, &Size);
    if (Data == nullptr || Size < 8)
        return;

    if (Found == DXC_FOURCC('I', 'S', 'G', '1') || Found == DXC_FOURCC('O', 'S', 'G', '1') ||
        Found == DXC_FOURCC('P', 'S', 'G', '1'))
    {
        Stride = 32;
        Base = 4;
    }
    else if (Found == DXC_FOURCC('O', 'S', 'G', '5'))
    {
        Stride = 28;
        Base = 4;
    }
    else
    {
        Stride = 24;
        Base = 0;
    }

    Rows = *reinterpret_cast<const ULONG*>(Data);
    for (ULONG Row = 0; Row < Rows; Row++)
    {
        const BYTE* At = Data + *reinterpret_cast<const ULONG*>(Data + 4) + Row * Stride;
        DC_DDI_ELEMENT Element;

        if (At + Stride > Data + Size)
            break;

        Element.SystemValue = DcDdiSystemValue(*reinterpret_cast<const ULONG*>(At + Base + 8),
                                               *reinterpret_cast<const ULONG*>(At + Base + 4));
        Element.ComponentType = *reinterpret_cast<const ULONG*>(At + Base + 12);
        Element.Register = *reinterpret_cast<const ULONG*>(At + Base + 16);
        Element.Mask = At[Base + 20];
        Element.Stream = Base != 0 ? static_cast<BYTE>(*reinterpret_cast<const ULONG*>(At)) : 0;
        Element.MinPrecision = Stride == 32 ? *reinterpret_cast<const ULONG*>(At + 28) : 0;
        Elements.push_back(Element);
    }
}

/* Runs ConvertInDriver on one shader and prints the module it returns */
static
VOID
DcDriverConvert(
    _In_z_ PCSTR Label,
    _In_ const UINT32* Program,
    _In_ const DC_DDI_SIGNATURES* Signatures,
    _In_opt_z_ PCWSTR Options,
    _In_ BOOLEAN Leak)
{
    IDxbcConverter* Converter = DcNewConverter();
    IDxcBlob* Blob = reinterpret_cast<IDxcBlob*>(1);
    LPWSTR Diag = reinterpret_cast<LPWSTR>(1);
    ULONG Fault;
    HRESULT hr;

    if (Converter == nullptr)
    {
        printf("%s: no converter\n", Label);
        return;
    }

    hr = DcGuardedDriverConvert(Converter, Program, Signatures, Options, &Blob, &Diag, &Fault);
    if (Fault != 0)
    {
        /* A converter that faulted part way is not released, its state is unknown */
        printf("%s: fault=%08lX\n", Label, Fault);
        return;
    }

    printf("%s: hr=%08lX blob=%s\n",
           Label,
           hr,
           Blob == nullptr ? "null" : (Blob == reinterpret_cast<IDxcBlob*>(1) ? "untouched" : "set"));

    if (Blob != nullptr && Blob != reinterpret_cast<IDxcBlob*>(1))
    {
        printf("  module size=%zu fnv=%08lX\n", Blob->GetBufferSize(), DcHash(Blob->GetBufferPointer(), Blob->GetBufferSize()));
        DcDump(Label, "bc", Blob->GetBufferPointer(), Blob->GetBufferSize());
        Blob->Release();
    }

    if (Diag == reinterpret_cast<LPWSTR>(1))
    {
        printf("  diag=untouched\n");
    }
    else
    {
        DcPrintDiag(Diag);
        if (Diag != nullptr)
            CoTaskMemFree(Diag);
    }

    if (!Leak)
        Converter->Release();
}

static const ULONG DcProgramCodes[] = { DXC_FOURCC('S', 'H', 'E', 'X'), DXC_FOURCC('S', 'H', 'D', 'R') };
static const ULONG DcInputCodes[] = { DXC_FOURCC('I', 'S', 'G', '1'), DXC_FOURCC('I', 'S', 'G', 'N') };
static const ULONG DcOutputCodes[] =
{
    DXC_FOURCC('O', 'S', 'G', '1'),
    DXC_FOURCC('O', 'S', 'G', '5'),
    DXC_FOURCC('O', 'S', 'G', 'N'),
};
static const ULONG DcPatchCodes[] = { DXC_FOURCC('P', 'S', 'G', '1'), DXC_FOURCC('P', 'C', 'S', 'G') };

/**
 * Feeds a compiled shader through ConvertInDriver with its signatures as DDI arrays.
 * NullMask drops the input (1), output (2) or patch constant (4) array but keeps its count.
 */
static
VOID
DcDriverShader(
    _In_z_ PCSTR Name,
    _In_ ID3DBlob* Code,
    _In_ ULONG NullMask)
{
    std::vector<DC_DDI_ELEMENT> Input;
    std::vector<DC_DDI_ELEMENT> Output;
    std::vector<DC_DDI_ELEMENT> Patch;
    DC_DDI_SIGNATURES Signatures;
    const UINT32* Program;
    CHAR Label[96];
    ULONG Found;
    ULONG Size;

    Program = reinterpret_cast<const UINT32*>(DcFindPart(Code, DcProgramCodes, _countof(DcProgramCodes), &Found, &Size));
    if (Program == nullptr)
        return;

    DcReadSignature(Code, DcInputCodes, _countof(DcInputCodes), Input);
    DcReadSignature(Code, DcOutputCodes, _countof(DcOutputCodes), Output);
    DcReadSignature(Code, DcPatchCodes, _countof(DcPatchCodes), Patch);

    Signatures.Input = (NullMask & 1) != 0 ? nullptr : Input.data();
    Signatures.InputCount = static_cast<UINT32>(Input.size());
    Signatures.Output = (NullMask & 2) != 0 ? nullptr : Output.data();
    Signatures.OutputCount = static_cast<UINT32>(Output.size());
    Signatures.Patch = (NullMask & 4) != 0 ? nullptr : Patch.data();
    Signatures.PatchCount = static_cast<UINT32>(Patch.size());

    if (NullMask == 0)
    {
        sprintf_s(Label, "%s driver", Name);
        printf("%s signatures in=%u out=%u patch=%u\n", Label, Signatures.InputCount, Signatures.OutputCount, Signatures.PatchCount);
        DcDriverConvert(Label, Program, &Signatures, nullptr, FALSE);

        sprintf_s(Label, "%s driver nocleanup", Name);
        DcDriverConvert(Label, Program, &Signatures, L"-no-dxil-cleanup", FALSE);
        return;
    }

    sprintf_s(Label, "%s driver null%s%s%s", Name, (NullMask & 1) != 0 ? " in" : "", (NullMask & 2) != 0 ? " out" : "", (NullMask & 4) != 0 ? " patch" : "");
    DcDriverConvert(Label, Program, &Signatures, nullptr, TRUE);
}

static
std::wstring
DcRuntimePath(
    _In_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"dxilconv.dll";

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(nullptr, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\dxilconv.dll";
    }

    return Runtime;
}

int
__cdecl
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    PCWSTR Runtime = L"system";
    WCHAR Loaded[MAX_PATH];
    HMODULE Module;
    ID3DBlob* Codes[_countof(DcCorpus)] = {};
    ID3DBlob* First = nullptr;

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
            Runtime = argv[++Index];
        else if (_wcsicmp(argv[Index], L"-dump") == 0 && Index + 1 < argc)
            DcDumpDirectory = argv[++Index];
    }

    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    Module = LoadLibraryExW(DcRuntimePath(Runtime).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (Module == nullptr)
    {
        fprintf(stderr, "dxilconvcheck: load failed %lu\n", GetLastError());
        return 1;
    }

    GetModuleFileNameW(Module, Loaded, _countof(Loaded));
    fwprintf(stderr, L"dxilconvcheck: loaded %s\n", Loaded);

    DcCreate = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(Module, "DxcCreateInstance"));
    DcCreate2 = reinterpret_cast<DxcCreateInstance2Proc>(GetProcAddress(Module, "DxcCreateInstance2"));
    printf("exports create=%d create2=%d\n", DcCreate != nullptr, DcCreate2 != nullptr);
    if (DcCreate == nullptr)
        return 1;

    DcCheckFactory();

    for (ULONG Index = 0; Index < _countof(DcCorpus); Index++)
    {
        ID3DBlob* Code;

        if (!DcCompile(&DcCorpus[Index], &Code))
            continue;

        printf("== %s %s\n", DcCorpus[Index].Name, DcCorpus[Index].Profile);
        DcPrintContainer("dxbc", Code->GetBufferPointer(), Code->GetBufferSize());
        DcDump(DcCorpus[Index].Name, "dxbc", Code->GetBufferPointer(), Code->GetBufferSize());
        DcConvert(DcCorpus[Index].Name,
                  Code->GetBufferPointer(),
                  static_cast<UINT32>(Code->GetBufferSize()),
                  nullptr,
                  TRUE);
        DcDriverShader(DcCorpus[Index].Name, Code, 0);

        Codes[Index] = Code;
        if (First == nullptr)
            First = Code;
    }

    if (First != nullptr)
    {
        DcCheckArguments(First);
        DcCheckDriverArguments(First);
    }

    /* Null arrays with a nonzero count last, the reference faults reading them */
    for (ULONG Index = 0; Index < _countof(DcCorpus); Index++)
    {
        if (Codes[Index] == nullptr)
            continue;

        for (ULONG NullMask = 1; NullMask <= 4; NullMask <<= 1)
            DcDriverShader(DcCorpus[Index].Name, Codes[Index], NullMask);

        Codes[Index]->Release();
    }

    return 0;
}
