/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What the reference rasterizer reports: its caps, format table and queries
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

#include <float.h>
#include <stdio.h>

/* A private format the reference rasterizer lists with a 32 bit pixel size */
static constexpr UINT D9RefPrivateFormat = 0xFF000004;

/* One row per format, in the order the rasterizer reports them; the last row only for the extended object */
static const FORMATOP D9RefFormats[] =
{
    { D3DDDIFMT_R8G8B8, 0x00D3881F, 0, 0x0108, 0 },
    { D3DDDIFMT_X8R8G8B8, 0x00D3881F, 0, 0x0108, 0 },
    { D3DDDIFMT_A8R8G8B8, 0x00D3811F, 0, 0x0108, 0 },
    { D3DDDIFMT_R5G6B5, 0x00D3881F, 0, 0x0108, 0 },
    { D3DDDIFMT_X1R5G5B5, 0x00D3881F, 0, 0x0108, 0 },
    { D3DDDIFMT_A1R5G5B5, 0x00D3811F, 0, 0x0108, 0 },
    { D3DDDIFMT_A4R4G4B4, 0x00D3801F, 0, 0x0108, 0 },
    { D3DDDIFMT_X4R4G4B4, 0x00D3801F, 0, 0x0108, 0 },
    { D3DDDIFMT_R3G3B2, 0x00D3801F, 0, 0x0108, 0 },
    { D3DDDIFMT_A8R3G3B2, 0x00D3801F, 0, 0x0108, 0 },
    { D3DDDIFMT_A8P8, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_P8, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_L8, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_L16, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_A8L8, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_A4L4, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_A8, 0x0083C007, 0, 0, 0 },
    { D3DDDIFMT_UYVY, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_YUY2, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_R8G8_B8G8, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_G8R8_G8B8, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_DXT1, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_DXT2, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_DXT3, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_DXT4, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_DXT5, 0x00838007, 0, 0, 0 },
    { D3DDDIFMT_V8U8, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_L6V5U5, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_X8L8V8U8, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_Q8W8V8U8, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_V16U16, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_W11V11U10, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_A8B8G8R8, 0x00D3801F, 0, 0, 0 },
    { D3DDDIFMT_X8B8G8R8, 0x00D3801F, 0, 0, 0 },
    { D3DDDIFMT_A2W10V10U10, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_A2B10G10R10, 0x00D3801F, 0, 0, 0 },
    { D3DDDIFMT_G16R16, 0x00D3801F, 0, 0, 0 },
    { D3DDDIFMT_A2R10G10B10, 0x00D3801F, 0, 0, 0 },
    { D3DDDIFMT_S8D24, 0x000000C0, 0, 0x0108, 0 },
    { D3DDDIFMT_S1D15, 0x000000C0, 0, 0x0108, 0 },
    { D3DDDIFMT_D24X4S4, 0x000000C0, 0, 0x0108, 0 },
    { D3DDDIFMT_D16_LOCKABLE, 0x00004000, 0, 0, 0 },
    { D3DDDIFMT_D32_LOCKABLE, 0x00004000, 0, 0, 0 },
    { D3DDDIFMT_D32F_LOCKABLE, 0x00004000, 0, 0, 0 },
    { D3DDDIFMT_S8_LOCKABLE, 0x00004000, 0, 0, 0 },
    { D3DDDIFMT_D16_LOCKABLE, 0x000000C0, 0, 0x0108, 0 },
    { D3DDDIFMT_D32, 0x000000C0, 0, 0x0108, 0 },
    { D3DDDIFMT_X8D24, 0x000000C0, 0, 0x0108, 0 },
    { static_cast<D3DDDIFORMAT>(D9RefPrivateFormat), 0x008010C1, 0, 0, 32 },
    { D3DDDIFMT_MULTI2_ARGB8, 0x00400109, 0, 0, 0 },
    { D3DDDIFMT_A16B16G16R16, 0x00D3801F, 0, 0, 0 },
    { D3DDDIFMT_Q16W16V16U16, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_R16F, 0x00C3001F, 0, 0, 0 },
    { D3DDDIFMT_G16R16F, 0x00C3001F, 0, 0, 0 },
    { D3DDDIFMT_A16B16G16R16F, 0x00C3001F, 0, 0, 0 },
    { D3DDDIFMT_R32F, 0x00C3001F, 0, 0, 0 },
    { D3DDDIFMT_G32R32F, 0x00C3001F, 0, 0, 0 },
    { D3DDDIFMT_A32B32G32R32F, 0x00C3001F, 0, 0, 0 },
    { D3DDDIFMT_CxV8U8, 0x00830007, 0, 0, 0 },
    { D3DDDIFMT_D32F_LOCKABLE, 0x000000C0, 0, 0x0108, 0 },
    { D3DDDIFMT_D24FS8, 0x000000C0, 0, 0x0108, 0 },
    { D3DDDIFMT_A1, 0x00004009, 0, 0, 0 },
};

/* Event, occlusion and the three timestamp queries */
static const D3DDDIQUERYTYPE D9RefQueries[] =
{
    D3DDDIQUERYTYPE_EVENT,
    D3DDDIQUERYTYPE_OCCLUSION,
    D3DDDIQUERYTYPE_TIMESTAMP,
    D3DDDIQUERYTYPE_TIMESTAMPDISJOINT,
    D3DDDIQUERYTYPE_TIMESTAMPFREQ,
};

/* A caps field is named, then its bits follow; a value step replaces the field instead of adding to it */
typedef enum _D9REF_STEP_KIND
{
    D9RefStepField,
    D9RefStepBit,
    D9RefStepValue,
    D9RefStepFloat,
} D9REF_STEP_KIND;

typedef struct _D9REF_CAP_STEP
{
    D9REF_STEP_KIND Kind;
    UINT Offset;
    PCSTR Name;
    DWORD Bits;
    FLOAT Default;
} D9REF_CAP_STEP;

#include "referencecaps.inc"

/* The caps profile named in the registry, if any */
static
BOOLEAN
D9RefCapsFile(
    _Out_writes_z_(128) CHAR* Path)
{
    HKEY Key;
    DWORD Type = 0;
    DWORD Size = 128;
    LONG Status;

    Path[0] = '\0';
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D\\ReferenceDevice", 0, KEY_QUERY_VALUE,
                      &Key) != ERROR_SUCCESS)
    {
        return FALSE;
    }
    Status = RegQueryValueExA(Key, "CapsFile", nullptr, &Type, reinterpret_cast<BYTE*>(Path), &Size);
    RegCloseKey(Key);
    if (Status != ERROR_SUCCESS || Type != REG_SZ)
    {
        Path[0] = '\0';
        return FALSE;
    }
    Path[Size + 1 < 128 ? Size : 127] = '\0';
    return TRUE;
}

/* Whether a disable list names a flag as a whole identifier */
static
BOOLEAN
D9RefFlagDisabled(
    _In_z_ PCSTR List,
    _In_z_ PCSTR Flag)
{
    SIZE_T Length = strlen(Flag);
    PCSTR Search = List;

    for (;;)
    {
        PCSTR Found = strstr(Search, Flag);
        CHAR Next;

        if (Found == nullptr)
            return FALSE;
        Next = Found[Length];
        if (!(Next > 0 && (isalnum(Next) || Next == '_')))
            return TRUE;
        Search = Found + 1;
    }
}

/* Runs a list of caps steps; a profile value overrides a whole field and a disable list drops single bits */
static
VOID
D9RefRunSteps(
    _In_reads_(Count) const D9REF_CAP_STEP* Steps,
    _In_ UINT Count,
    _In_opt_z_ PCSTR Profile,
    _Inout_ D3DCAPS9* Caps)
{
    CHAR Disabled[1024];
    DWORD DisabledLength = 0;
    BOOLEAN Overridden = FALSE;
    DWORD* Field = nullptr;

    for (UINT Index = 0; Index < Count; Index++)
    {
        const D9REF_CAP_STEP* Step = &Steps[Index];
        DWORD* Target = reinterpret_cast<DWORD*>(reinterpret_cast<BYTE*>(Caps) + Step->Offset);
        CHAR Text[1024];
        DWORD Length;

        switch (Step->Kind)
        {
        case D9RefStepField:
            Field = Target;
            Overridden = FALSE;
            if (Profile != nullptr)
            {
                Length = GetPrivateProfileStringA("RefCapValue", Step->Name, "", Text, 1023, Profile);
                if (Length != 0)
                {
                    Text[Length] = '\0';
                    sscanf_s(Text, "%i", reinterpret_cast<int*>(Field));
                    Overridden = TRUE;
                    break;
                }
            }
            DisabledLength = GetPrivateProfileStringA("RefCapDisable", Step->Name, "", Disabled, 1023, Profile);
            Disabled[DisabledLength] = '\0';
            break;

        case D9RefStepBit:
            if (!Overridden && (DisabledLength == 0 || !D9RefFlagDisabled(Disabled, Step->Name)))
                *Field |= Step->Bits;
            break;

        case D9RefStepValue:
            if (!Overridden)
                *Field = Step->Bits;
            break;

        case D9RefStepFloat:
        {
            FLOAT Value = 0.0f;

            *reinterpret_cast<FLOAT*>(Target) = Step->Default;
            if (Profile == nullptr)
                break;
            Length = GetPrivateProfileStringA("RefCapValue", Step->Name, "", Text, 1024, Profile);
            if (Length != 0 && sscanf_s(Text, "%f", &Value) != 0)
                *reinterpret_cast<FLOAT*>(Target) = Value;
            break;
        }
        }
    }
}

/* Pixel shader overrides read after the caps are filled */
static
VOID
D9RefShaderOverrides(
    _Inout_ D3DCAPS9* Caps)
{
    HKEY Key;
    DWORD Type = 0;
    DWORD Size = sizeof(DWORD);
    DWORD Version = 0;
    CHAR Text[128];

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D\\ReferenceDevice", 0, KEY_QUERY_VALUE,
                      &Key) != ERROR_SUCCESS)
    {
        return;
    }
    if (RegQueryValueExA(Key, "PixelShaderVersion", nullptr, &Type, reinterpret_cast<BYTE*>(&Version), &Size) ==
            ERROR_SUCCESS &&
        Type == REG_DWORD)
    {
        Caps->PixelShaderVersion = Version;
    }
    Size = sizeof(Text);
    if (RegQueryValueExA(Key, "PixelShader1xMaxValue", nullptr, &Type, reinterpret_cast<BYTE*>(Text), &Size) ==
            ERROR_SUCCESS &&
        Type == REG_SZ)
    {
        Text[sizeof(Text) - 1] = '\0';
        sscanf_s(Text, "%f", &Caps->PixelShader1xMaxValue);
    }
    RegCloseKey(Key);
}

/* The DDI level the rasterizer pretends to be, 6 to 9; below 8 the old DirectX 8 rasterizer would answer instead */
static
BOOLEAN
D9RefDriverStyle(
    _Out_ UINT* Style)
{
    static HMODULE OldRasterizer;
    HKEY Key;
    DWORD Type = 0;
    DWORD Size = sizeof(DWORD);
    DWORD Value = 0;

    *Style = 9;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D", 0, KEY_QUERY_VALUE, &Key) ==
        ERROR_SUCCESS)
    {
        if (RegQueryValueExA(Key, "DriverStyle", nullptr, &Type, reinterpret_cast<BYTE*>(&Value), &Size) ==
                ERROR_SUCCESS &&
            Type == REG_DWORD && Value != 0)
        {
            *Style = Value > 9 ? 9 : (Value < 6 ? 6 : Value);
        }
        RegCloseKey(Key);
    }
    if (*Style >= 8)
        return TRUE;

    if (OldRasterizer == NULL)
    {
        OldRasterizer = LoadLibraryA("D3DREF8.DLL");
        if (OldRasterizer == NULL)
        {
            *Style = 9;
            return TRUE;
        }
    }
    if (GetProcAddress(OldRasterizer, "D3D8GetSWInfo") != nullptr)
    {
        D9_TRACE_ONCE(L"the DirectX 8 reference rasterizer is not supported");
        return FALSE;
    }
    FreeLibrary(OldRasterizer);
    OldRasterizer = NULL;
    *Style = 9;
    return TRUE;
}

/* The caps as the rasterizer fills them, profile and registry overrides included */
static
BOOLEAN
D9RefRawCaps(
    _Out_ D3DCAPS9* Caps)
{
    CHAR Profile[128];
    PCSTR ProfilePath = nullptr;
    UINT Style;

    ZeroMemory(Caps, sizeof(*Caps));
    if (!D9RefDriverStyle(&Style))
        return FALSE;

    if (D9RefCapsFile(Profile) && Profile[0] != '\0')
        ProfilePath = Profile;
    D9RefRunSteps(D9RefCapsCommon, _countof(D9RefCapsCommon), ProfilePath, Caps);
    if (Style == 8)
        D9RefRunSteps(D9RefCapsStyle8, _countof(D9RefCapsStyle8), ProfilePath, Caps);
    else if (Style == 9)
        D9RefRunSteps(D9RefCapsStyle9, _countof(D9RefCapsStyle9), ProfilePath, Caps);
    D9RefShaderOverrides(Caps);
    return TRUE;
}

/* Whether the hardware table lists the format with all of the operations */
static
BOOLEAN
D9RefHardwareHas(
    _In_ const D9Adapter* Hardware,
    _In_ D3DDDIFORMAT Format,
    _In_ UINT Operations)
{
    for (UINT Index = 0; Index < Hardware->FormatCount; Index++)
    {
        const FORMATOP* Op = &Hardware->Formats[Index];

        if (Op->Format == Format && (Operations & ~Op->Operations) == 0)
            return TRUE;
    }
    return FALSE;
}

/* Software tables only render to what the hardware can show, and convert like the hardware's desktop formats */
static
BOOLEAN
D9RefFixFormats(
    _In_ const D9Adapter* Hardware,
    _Inout_updates_(Count) FORMATOP* Formats,
    _In_ UINT Count)
{
    D9NormalizeFormatOps(Formats, Count);
    for (UINT Index = 0; Index < Count; Index++)
    {
        FORMATOP* Op = &Formats[Index];

        if ((Op->Operations & FORMATOP_DISPLAYMODE) != 0)
        {
            D9Trace(L"a software rasterizer claims a display mode format and is not used");
            return FALSE;
        }
        if (Op->Format >= D3DDDIFMT_A8R8G8B8 && Op->Format <= D3DDDIFMT_A1R5G5B5)
            Op->Operations |= FORMATOP_OFFSCREENPLAIN;
    }

    for (UINT Index = 0; Index < Count; Index++)
    {
        FORMATOP* Op = &Formats[Index];

        if ((Op->Operations & FORMATOP_3DACCELERATION) == 0)
            continue;
        if (D9RefHardwareHas(Hardware, Op->Format, FORMATOP_DISPLAYMODE))
            Op->Operations |= FORMATOP_DISPLAYMODE;
        else
            Op->Operations &= ~FORMATOP_3DACCELERATION;
    }

    for (UINT Index = 0; Index < Count; Index++)
    {
        FORMATOP* Op = &Formats[Index];

        switch (Op->Format)
        {
        case D3DDDIFMT_X8R8G8B8:
        case D3DDDIFMT_R5G6B5:
        case D3DDDIFMT_X1R5G5B5:
            if ((Op->Operations & FORMATOP_DISPLAYMODE) != 0)
                Op->Operations |= FORMATOP_MEMBEROFGROUP_ARGB;
            break;
        case D3DDDIFMT_A8R8G8B8:
        case D3DDDIFMT_A1R5G5B5:
        case D3DDDIFMT_A2R10G10B10:
            break;
        default:
            continue;
        }
        if ((Op->Operations & FORMATOP_OFFSCREEN_RENDERTARGET) != 0)
            Op->Operations |= FORMATOP_CONVERT_TO_ARGB;
    }
    return TRUE;
}

/* Builds the reference rasterizer's answers for an adapter once; REF and NULLREF share them */
_Ret_maybenull_
const D9Adapter*
D9Enum::ReferenceTable(
    _In_ UINT Adapter)
{
    const D9Adapter* Hardware = &Adapters[Adapter];
    UINT FormatCount = _countof(D9RefFormats) - (IsEx ? 0 : 1);
    D9Adapter* Table;

    if (ReferenceTried[Adapter])
        return ReferenceTables[Adapter];
    ReferenceTried[Adapter] = TRUE;

    Table = D9CopyAdapter(Hardware);
    if (Table == nullptr)
        return nullptr;
    Table->HasDriver = FALSE;
    Table->Formats = new (std::nothrow) FORMATOP[FormatCount];
    Table->Queries = new (std::nothrow) D3DDDIQUERYTYPE[_countof(D9RefQueries)];
    if (Table->Formats == nullptr || Table->Queries == nullptr || !D9RefRawCaps(&Table->DriverCaps))
    {
        D9FreeDriverAnswers(Table);
        delete Table;
        return nullptr;
    }
    CopyMemory(Table->Formats, D9RefFormats, FormatCount * sizeof(*Table->Formats));
    CopyMemory(Table->Queries, D9RefQueries, sizeof(D9RefQueries));
    Table->FormatCount = FormatCount;
    Table->QueryCount = _countof(D9RefQueries);

    D9MaskDriverCaps(&Table->DriverCaps, FALSE);
    if (!D9RefFixFormats(Hardware, Table->Formats, Table->FormatCount))
    {
        D9FreeDriverAnswers(Table);
        delete Table;
        return nullptr;
    }
    ReferenceTables[Adapter] = Table;
    return Table;
}
