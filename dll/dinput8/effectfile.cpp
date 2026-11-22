/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Force feedback effect files: a RIFF form of effect lists read and written through MMIO
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>

/* Flags the effect file calls accept */
#define DI_FILE_FLAGS_VALID     (DIFEF_INCLUDENONSTANDARD | DIFEF_MODIFYIFNEEDED)

/* Effect files carry their own GUID right after the form type */
static const GUID DiEffectFileGuid = { 0x197E775C, 0x34BA, 0x11D3, { 0xAB, 0xD5, 0x00, 0xC0, 0x4F, 0x8E, 0xC6, 0x27 } };

#define DI_FOURCC_FORM          mmioFOURCC('F', 'O', 'R', 'C')
#define DI_FOURCC_EFFECT        mmioFOURCC('e', 'f', 'c', 't')
#define DI_FOURCC_DATA          mmioFOURCC('d', 'a', 't', 'a')

/* How the parameters of one effect are stored: a DIEFFECT as it looked with 32 bit pointers */
struct DI_FILE_PARAMS
{
    DWORD Size;
    DWORD Flags;
    DWORD Duration;
    DWORD SamplePeriod;
    DWORD Gain;
    DWORD TriggerButton;
    DWORD TriggerRepeatInterval;
    DWORD Axes;
    DWORD AxesSlot;
    DWORD DirectionSlot;
    DWORD EnvelopeSlot;
    DWORD TypeSpecificSize;
    DWORD TypeSpecificSlot;
    DWORD StartDelay;
};

static_assert(sizeof(DI_FILE_PARAMS) == 56, "effect file layout");

/* The name field keeps its first 64 bytes in the file */
#define DI_FILE_NAME_BYTES      64

/* Axis and direction space handed to the enumeration callback */
#define DI_FILE_MAX_AXES        32

/* Type specific data is stored after this marker in place of a pointer */
#define DI_FILE_SPECIFIC_MARK   0x100

static
HRESULT
DiMmioResult(
    _In_ UINT Code)
{
    switch (Code)
    {
        case MMSYSERR_NOERROR:
            return S_OK;
        case MMIOERR_FILENOTFOUND:
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        case MMIOERR_OUTOFMEMORY:
            return E_OUTOFMEMORY;
        case MMIOERR_CANNOTOPEN:
            return HRESULT_FROM_WIN32(ERROR_OPEN_FAILED);
        case MMIOERR_CANNOTCLOSE:
            return S_FALSE;
        case MMIOERR_CANNOTREAD:
            return HRESULT_FROM_WIN32(ERROR_READ_FAULT);
        case MMIOERR_CANNOTWRITE:
            return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        case MMIOERR_CANNOTSEEK:
        case MMIOERR_CANNOTEXPAND:
            return HRESULT_FROM_WIN32(ERROR_SEEK);
        case MMIOERR_CHUNKNOTFOUND:
            return HRESULT_FROM_WIN32(ERROR_SECTOR_NOT_FOUND);
        case MMIOERR_PATHNOTFOUND:
            return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        case MMIOERR_ACCESSDENIED:
            return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
        case MMIOERR_SHARINGVIOLATION:
            return HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION);
        case MMIOERR_NETWORKERROR:
            return HRESULT_FROM_WIN32(ERROR_UNEXP_NET_ERR);
        case MMIOERR_TOOMANYOPENFILES:
            return HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES);
        case MMIOERR_INVALIDFILE:
            return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
        default:
            return E_FAIL;
    }
}

static
HRESULT
DiFileRead(
    _In_ HMMIO File,
    _Out_writes_bytes_(Size) VOID* Data,
    _In_ LONG Size)
{
    return mmioRead(File, static_cast<HPSTR>(Data), Size) == Size ? S_OK : HRESULT_FROM_WIN32(ERROR_READ_FAULT);
}

static
HRESULT
DiFileWrite(
    _In_ HMMIO File,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ LONG Size)
{
    return mmioWrite(File, static_cast<const char*>(Data), Size) == Size ? S_OK : HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
}

static
HRESULT
DiFileCreateChunk(
    _In_ HMMIO File,
    _Inout_ MMCKINFO* Chunk,
    _In_ UINT Flags)
{
    Chunk->cksize = 0;
    return DiMmioResult(mmioCreateChunk(File, Chunk, Flags));
}

/*
 * Opens an effect file for reading or creates one for writing. A bare name resolves against
 * the current directory first, and a file opened for reading must exist and carry the form.
 */
static
HRESULT
DiOpenEffectFile(
    _In_z_ PCSTR Name,
    _In_ BOOLEAN Writing,
    _Out_ HMMIO* Opened,
    _Out_ MMCKINFO* Form)
{
    CHAR FullPath[MAX_PATH];
    MMIOINFO Info = {};
    PCSTR Path = Name;
    DWORD Length;
    HMMIO File;
    HRESULT Result = S_OK;

    ZeroMemory(Form, sizeof(*Form));
    *Opened = nullptr;
    if (strchr(Name, '\\') == nullptr)
    {
        Length = GetFullPathNameA(Name, _countof(FullPath), FullPath, nullptr);
        if (Length >= 1 && Length <= MAX_PATH - 1)
            Path = FullPath;
    }
    if (!Writing)
    {
        HANDLE Probe = CreateFileA(Path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (Probe == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError() & 0xFFFF);
        CloseHandle(Probe);
    }

    File = mmioOpenA(const_cast<LPSTR>(Path), &Info, Writing ? MMIO_CREATE | MMIO_WRITE | MMIO_ALLOCBUF : MMIO_ALLOCBUF);
    if (Info.wErrorRet != 0)
        Result = DiMmioResult(Info.wErrorRet);
    Form->fccType = DI_FOURCC_FORM;
    if (!Writing)
    {
        if (SUCCEEDED(Result))
            Result = DiMmioResult(mmioDescend(File, Form, nullptr, MMIO_FINDRIFF));
        if (SUCCEEDED(Result))
        {
            GUID Signature = {};

            Result = DiFileRead(File, &Signature, sizeof(Signature));
            if (SUCCEEDED(Result) && !IsEqualGUID(Signature, DiEffectFileGuid))
                Result = HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
        }
    }
    else
    {
        /* Creation goes ahead even when the open failed, and its own error wins */
        Result = DiFileCreateChunk(File, Form, MMIO_CREATERIFF);
        if (SUCCEEDED(Result))
            Result = DiFileWrite(File, &DiEffectFileGuid, sizeof(DiEffectFileGuid));
    }
    *Opened = File;
    return Result;
}

static
BOOLEAN
DiIsStandardEffect(
    _In_ REFGUID Effect)
{
    static const GUID* Standard[] =
    {
        &GUID_Sine, &GUID_Triangle, &GUID_ConstantForce, &GUID_RampForce, &GUID_Square, &GUID_SawtoothUp,
        &GUID_SawtoothDown, &GUID_Spring, &GUID_Damper, &GUID_Inertia, &GUID_Friction, &GUID_CustomForce,
    };

    for (const GUID* Known : Standard)
    {
        if (IsEqualGUID(Effect, *Known))
            return TRUE;
    }
    return FALSE;
}

/* Reads one data chunk into a caller buffer, the way every optional block is stored */
static
HRESULT
DiReadDataChunk(
    _In_ HMMIO File,
    _Inout_ MMCKINFO* Chunk,
    _Out_writes_bytes_(Size) VOID* Data,
    _In_ LONG Size)
{
    HRESULT Result;

    Chunk->ckid = DI_FOURCC_DATA;
    Result = DiMmioResult(mmioDescend(File, Chunk, nullptr, MMIO_FINDCHUNK));
    if (FAILED(Result))
        return Result;
    mmioRead(File, static_cast<HPSTR>(Data), Size);
    return DiMmioResult(mmioAscend(File, Chunk, 0));
}

static
HRESULT
DiReadEffect(
    _In_ HMMIO File,
    _Inout_ LPDIFILEEFFECT Effect)
{
    LPDIEFFECT Params = const_cast<LPDIEFFECT>(Effect->lpDiEffect);
    MMCKINFO List = {};
    MMCKINFO Chunk = {};
    DI_FILE_PARAMS Stored = {};
    DWORD Reserved = 0;
    HRESULT Result;

    List.fccType = DI_FOURCC_EFFECT;
    Result = DiMmioResult(mmioDescend(File, &List, nullptr, MMIO_FINDLIST));
    if (FAILED(Result))
        return Result;
    Result = DiFileRead(File, Effect->szFriendlyName, DI_FILE_NAME_BYTES);
    if (FAILED(Result))
        return Result;

    /* A block of another size is skipped without its read result counting */
    Result = DiFileRead(File, &Stored, sizeof(Stored));
    if (Stored.Size == sizeof(Stored))
    {
        Params->dwSize = sizeof(DIEFFECT);
        Params->dwFlags = Stored.Flags;
        Params->dwDuration = Stored.Duration;
        Params->dwSamplePeriod = Stored.SamplePeriod;
        Params->dwGain = Stored.Gain;
        Params->dwTriggerButton = Stored.TriggerButton;
        Params->dwTriggerRepeatInterval = Stored.TriggerRepeatInterval;
        Params->cAxes = Stored.Axes;
        Params->cbTypeSpecificParams = Stored.TypeSpecificSize;
        Params->lpvTypeSpecificParams = reinterpret_cast<VOID*>(static_cast<ULONG_PTR>(Stored.TypeSpecificSlot));
        Params->dwStartDelay = Stored.StartDelay;
        if (FAILED(Result))
            return Result;
    }
    Result = DiFileRead(File, &Effect->GuidEffect, sizeof(Effect->GuidEffect));
    if (SUCCEEDED(Result))
        Result = DiFileRead(File, &Reserved, sizeof(Reserved));
    if (FAILED(Result))
        return Result;

    if (Params->rgdwAxes != nullptr)
    {
        Result = DiReadDataChunk(File, &Chunk, Params->rgdwAxes, 4 * (Params->cAxes < DI_FILE_MAX_AXES ? Params->cAxes : DI_FILE_MAX_AXES));
        if (FAILED(Result))
            return Result;
    }
    if (Params->rglDirection != nullptr)
    {
        Result = DiReadDataChunk(File, &Chunk, Params->rglDirection, 4 * (Params->cAxes < DI_FILE_MAX_AXES ? Params->cAxes : DI_FILE_MAX_AXES));
        if (FAILED(Result))
            return Result;
    }
    if (Params->lpEnvelope != nullptr)
    {
        Result = DiReadDataChunk(File, &Chunk, Params->lpEnvelope, sizeof(DIENVELOPE));
        if (FAILED(Result))
            return Result;
    }
    if (Params->cbTypeSpecificParams != 0)
    {
        Params->lpvTypeSpecificParams = calloc(1, Params->cbTypeSpecificParams);
        if (Params->lpvTypeSpecificParams == nullptr)
            return E_OUTOFMEMORY;
        Result = DiReadDataChunk(File, &Chunk, Params->lpvTypeSpecificParams, Params->cbTypeSpecificParams);
        if (FAILED(Result))
            return Result;
    }
    return DiMmioResult(mmioAscend(File, &List, 0));
}

static
HRESULT
DiWriteDataChunk(
    _In_ HMMIO File,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ LONG Size)
{
    MMCKINFO Chunk = {};
    HRESULT Result;

    Chunk.ckid = DI_FOURCC_DATA;
    Result = DiFileCreateChunk(File, &Chunk, 0);
    if (FAILED(Result))
        return Result;
    mmioWrite(File, static_cast<const char*>(Data), Size);
    return DiMmioResult(mmioAscend(File, &Chunk, 0));
}

static
HRESULT
DiWriteEffect(
    _In_ HMMIO File,
    _In_ LPCDIFILEEFFECT Effect)
{
    LPCDIEFFECT Params = Effect->lpDiEffect;
    BOOLEAN Specific = Params->cbTypeSpecificParams != 0 && Params->lpvTypeSpecificParams != nullptr;
    DI_FILE_PARAMS Stored = {};
    MMCKINFO List = {};
    DWORD Version = 1;
    HRESULT Result;

    List.fccType = DI_FOURCC_EFFECT;
    Result = DiFileCreateChunk(File, &List, MMIO_CREATELIST);
    if (FAILED(Result))
        return Result;
    Result = DiFileWrite(File, Effect->szFriendlyName, DI_FILE_NAME_BYTES);
    if (FAILED(Result))
        return Result;

    /* Pointers do not survive the file; only the type specific block leaves a marker */
    Stored.Size = sizeof(Stored);
    Stored.Flags = Params->dwFlags;
    Stored.Duration = Params->dwDuration;
    Stored.SamplePeriod = Params->dwSamplePeriod;
    Stored.Gain = Params->dwGain;
    Stored.TriggerButton = Params->dwTriggerButton;
    Stored.TriggerRepeatInterval = Params->dwTriggerRepeatInterval;
    Stored.Axes = Params->cAxes;
    Stored.TypeSpecificSize = Params->cbTypeSpecificParams;
    Stored.TypeSpecificSlot = Specific ? DI_FILE_SPECIFIC_MARK :
                                         static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(Params->lpvTypeSpecificParams));
    Stored.StartDelay = Params->dwStartDelay;
    Result = DiFileWrite(File, &Stored, sizeof(Stored));
    if (SUCCEEDED(Result))
        Result = DiFileWrite(File, &Effect->GuidEffect, sizeof(Effect->GuidEffect));
    if (SUCCEEDED(Result))
        Result = DiFileWrite(File, &Version, sizeof(Version));
    if (FAILED(Result))
        return Result;

    if (Params->rgdwAxes != nullptr)
    {
        Result = DiWriteDataChunk(File, Params->rgdwAxes, 4 * Params->cAxes);
        if (FAILED(Result))
            return Result;
    }
    if (Params->rglDirection != nullptr)
    {
        Result = DiWriteDataChunk(File, Params->rglDirection, 4 * Params->cAxes);
        if (FAILED(Result))
            return Result;
    }
    if (Params->lpEnvelope != nullptr)
    {
        Result = DiWriteDataChunk(File, Params->lpEnvelope, Params->lpEnvelope->dwSize);
        if (FAILED(Result))
            return Result;
    }
    if (Specific)
    {
        Result = DiWriteDataChunk(File, Params->lpvTypeSpecificParams, Params->cbTypeSpecificParams);
        if (FAILED(Result))
            return Result;
    }
    return DiMmioResult(mmioAscend(File, &List, 0));
}

/* Reads a name the caller passed, which must end within the first 256 characters */
static
HRESULT
DiCheckFileName(
    _In_opt_z_ PCSTR Name)
{
    if (Name == nullptr || strnlen(Name, 256) == 256)
        return E_INVALIDARG;
    return S_OK;
}

static
HRESULT
DiCheckFileNameWide(
    _In_opt_z_ PCWSTR Name)
{
    if (Name == nullptr || wcsnlen(Name, 256) == 256)
        return E_INVALIDARG;
    return S_OK;
}

/*
 * An effect the device cannot create as stored is skipped when the caller asks for effects
 * fitted to the device and the device has no force feedback axes to fit it to.
 */
static
BOOLEAN
DiEffectFits(
    _In_ DiDevice* Device,
    _In_ LPCDIFILEEFFECT Effect)
{
    LPDIRECTINPUTEFFECT Created = nullptr;
    HRESULT Result = Device->CreateEffect(Effect->GuidEffect, Effect->lpDiEffect, &Created, nullptr);

    if (SUCCEEDED(Result))
    {
        Created->Release();
        return TRUE;
    }
    if (Result != E_INVALIDARG)
        return TRUE;
    DI_TRACE_ONCE(L"effect file entries cannot be fitted to a device without actuators");
    return FALSE;
}

static
HRESULT
DiEnumFileEffects(
    _In_ DiDevice* Device,
    _In_z_ PCSTR FileName,
    _In_ LPDIENUMEFFECTSINFILECALLBACK Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    DWORD Axes[DI_FILE_MAX_AXES];
    LONG Direction[DI_FILE_MAX_AXES];
    DIENVELOPE Envelope;
    DIEFFECT Params;
    DIFILEEFFECT Effect;
    MMCKINFO Form;
    HMMIO File;
    BOOL Continue = DIENUM_CONTINUE;
    HRESULT Result;

    Result = DiOpenEffectFile(FileName, FALSE, &File, &Form);
    if (FAILED(Result))
    {
        if (File != nullptr)
            mmioClose(File, 0);
        DI_TRACE_ONCE(L"cannot open effect file %S (%08lX)", FileName, Result);
        return Result;
    }

    ZeroMemory(&Params, sizeof(Params));
    ZeroMemory(&Envelope, sizeof(Envelope));
    ZeroMemory(&Effect, sizeof(Effect));
    Params.rgdwAxes = Axes;
    Params.rglDirection = Direction;
    Params.lpEnvelope = &Envelope;
    Effect.dwSize = sizeof(Effect);
    Effect.lpDiEffect = &Params;
    do
    {
        BOOLEAN Wanted = TRUE;

        Result = DiReadEffect(File, &Effect);
        if (FAILED(Result))
            break;
        if ((Flags & DIFEF_MODIFYIFNEEDED) && !DiEffectFits(Device, &Effect))
            Wanted = FALSE;
        if (!(Flags & DIFEF_INCLUDENONSTANDARD) && !DiIsStandardEffect(Effect.GuidEffect))
            Wanted = FALSE;
        Continue = Wanted ? Callback(&Effect, Context) : DIENUM_CONTINUE;
        if (Params.cbTypeSpecificParams != 0)
        {
            free(Params.lpvTypeSpecificParams);
            Params.cbTypeSpecificParams = 0;
        }
    } while (Continue == DIENUM_CONTINUE);
    mmioClose(File, 0);
    if (Result == HRESULT_FROM_WIN32(ERROR_SECTOR_NOT_FOUND))
        return S_OK;
    return Result;
}

static
HRESULT
DiWriteFileEffects(
    _In_z_ PCSTR FileName,
    _In_ DWORD Count,
    _In_ LPDIFILEEFFECT Effects,
    _In_ DWORD Flags)
{
    MMCKINFO Form;
    HMMIO File;
    HRESULT Result;

    Result = DiOpenEffectFile(FileName, TRUE, &File, &Form);
    if (FAILED(Result))
    {
        DI_TRACE_ONCE(L"cannot create effect file %S (%08lX)", FileName, Result);
    }
    else
    {
        for (DWORD Index = 0; Index < Count; Index++)
        {
            if (!(Flags & DIFEF_INCLUDENONSTANDARD) && !DiIsStandardEffect(Effects[Index].GuidEffect))
            {
                Result = S_OK;
                continue;
            }
            Result = DiWriteEffect(File, &Effects[Index]);
            if (FAILED(Result))
                break;
        }
    }

    /* The form is never closed off, so its size stays as created */
    mmioClose(File, 0);
    return Result;
}

STDMETHODIMP
DiDevice::EnumEffectsInFile(
    _In_z_ LPCWSTR FileName,
    _In_ LPDIENUMEFFECTSINFILECALLBACK Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD FileFlags)
{
    CHAR Narrow[MAX_PATH];
    HRESULT Result = DiCheckFileNameWide(FileName);

    if (FAILED(Result))
        return Result;
    if (Callback == nullptr || (FileFlags & ~DI_FILE_FLAGS_VALID) != 0)
        return E_INVALIDARG;
    WideCharToMultiByte(CP_ACP, 0, FileName, -1, Narrow, _countof(Narrow), nullptr, nullptr);
    return Ansi.EnumEffectsInFile(Narrow, Callback, Context, FileFlags);
}

STDMETHODIMP
DiDevice::AnsiFace::EnumEffectsInFile(
    _In_z_ LPCSTR FileName,
    _In_ LPDIENUMEFFECTSINFILECALLBACK Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD FileFlags)
{
    HRESULT Result = DiCheckFileName(FileName);

    if (FAILED(Result))
        return Result;
    if (Callback == nullptr || (FileFlags & ~DI_FILE_FLAGS_VALID) != 0)
        return E_INVALIDARG;
    return DiEnumFileEffects(Owner, FileName, Callback, Context, FileFlags);
}

STDMETHODIMP
DiDevice::WriteEffectToFile(
    _In_z_ LPCWSTR FileName,
    _In_ DWORD Count,
    _In_reads_(Count) LPDIFILEEFFECT Effects,
    _In_ DWORD FileFlags)
{
    CHAR Narrow[MAX_PATH];
    HRESULT Result = DiCheckFileNameWide(FileName);

    if (FAILED(Result))
        return Result;
    WideCharToMultiByte(CP_ACP, 0, FileName, -1, Narrow, _countof(Narrow), nullptr, nullptr);
    return Ansi.WriteEffectToFile(Narrow, Count, Effects, FileFlags);
}

STDMETHODIMP
DiDevice::AnsiFace::WriteEffectToFile(
    _In_z_ LPCSTR FileName,
    _In_ DWORD Count,
    _In_reads_(Count) LPDIFILEEFFECT Effects,
    _In_ DWORD FileFlags)
{
    HRESULT Result = DiCheckFileName(FileName);

    if (FAILED(Result))
        return Result;
    if ((FileFlags & ~DI_FILE_FLAGS_VALID) != 0)
        return E_INVALIDARG;
    if (Effects == nullptr)
        return E_POINTER;
    return DiWriteFileEffects(FileName, Count, Effects, FileFlags);
}
