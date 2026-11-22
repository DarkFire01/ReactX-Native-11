/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Force feedback effect files in RIFF form
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput.hpp"

#include <mmsystem.h>

/* The file is a FORC RIFF form with one efct list per effect */
#define DI_FOURCC_FORCE     mmioFOURCC('F', 'O', 'R', 'C')
#define DI_FOURCC_EFFECT    mmioFOURCC('e', 'f', 'c', 't')
#define DI_FOURCC_DATA      mmioFOURCC('d', 'a', 't', 'a')

#define DI_FILE_NAME_BYTES  64
#define DI_FILE_LIMIT       256
#define DI_FILE_FLAGS_VALID (DIFEF_INCLUDENONSTANDARD | DIFEF_MODIFYIFNEEDED)

/* Room the reader keeps for axes, directions and the envelope of one effect */
#define DI_FILE_AXIS_LIMIT  32

/* Marks the file format, written right after the form header */
static const GUID DiFileEffectGuid = { 0x197E775C, 0x34BA, 0x11D3, { 0xAB, 0xD5, 0x00, 0xC0, 0x4F, 0x8E, 0xC6, 0x27 } };

/* DIEFFECT as files store it, with 32 bit pointer slots */
typedef struct _RX_DI_FILE_EFFECT
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwDuration;
    DWORD dwSamplePeriod;
    DWORD dwGain;
    DWORD dwTriggerButton;
    DWORD dwTriggerRepeatInterval;
    DWORD cAxes;
    DWORD Axes;
    DWORD Direction;
    DWORD Envelope;
    DWORD cbTypeSpecificParams;
    DWORD TypeSpecificParams;
    DWORD dwStartDelay;
} RX_DI_FILE_EFFECT;

static_assert(sizeof(RX_DI_FILE_EFFECT) == 56, "stored effect record");

static
HRESULT
DiMmioResult(
    _In_ MMRESULT Result)
{
    switch (Result)
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
            return E_ACCESSDENIED;
        case MMIOERR_SHARINGVIOLATION:
            return HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION);
        case MMIOERR_NETWORKERROR:
            return HRESULT_FROM_WIN32(ERROR_UNEXP_NET_ERR);
        case MMIOERR_TOOMANYOPENFILES:
            return HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES);
        case MMIOERR_INVALIDFILE:
            return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
    }
    return E_FAIL;
}

/* Opens a file for reading or creates one, positioned after the format marker */
static
HRESULT
DiOpenEffectFile(
    _In_z_ LPCSTR Name,
    _In_ DWORD Mode,
    _Out_ HMMIO* File,
    _Out_ MMCKINFO* Form)
{
    MMIOINFO Info = {};
    GUID Marker = {};
    HRESULT hr = S_OK;
    HMMIO Opened;

    ZeroMemory(Form, sizeof(*Form));
    Opened = mmioOpenA(const_cast<LPSTR>(Name), &Info, Mode);
    if (Info.wErrorRet != 0)
        hr = DiMmioResult(Info.wErrorRet);

    if (Mode == MMIO_ALLOCBUF)
    {
        if (SUCCEEDED(hr))
        {
            Form->fccType = DI_FOURCC_FORCE;
            hr = DiMmioResult(mmioDescend(Opened, Form, nullptr, MMIO_FINDRIFF));
            if (SUCCEEDED(hr))
            {
                if (mmioRead(Opened, reinterpret_cast<HPSTR>(&Marker), sizeof(Marker)) != sizeof(Marker))
                    hr = HRESULT_FROM_WIN32(ERROR_READ_FAULT);
                else if (!IsEqualGUID(Marker, DiFileEffectGuid))
                    hr = HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
            }
        }
    }
    else if ((Mode & MMIO_WRITE) == 0 || (Form->fccType = DI_FOURCC_FORCE, Opened == nullptr))
    {
        hr = E_FAIL;
    }
    else
    {
        Form->cksize = 0;
        hr = DiMmioResult(mmioCreateChunk(Opened, Form, MMIO_CREATERIFF));
        if (SUCCEEDED(hr))
        {
            hr = (mmioWrite(Opened, reinterpret_cast<const char*>(&DiFileEffectGuid), sizeof(DiFileEffectGuid)) == sizeof(DiFileEffectGuid))
                     ? S_OK : HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        }
    }

    if (FAILED(hr))
        DiTrace(L"effect file %S failed to open, 0x%08lx", Name, hr);
    *File = Opened;
    return hr;
}

static
BOOLEAN
DiIsStandardEffect(
    _In_ REFGUID Guid)
{
    static const GUID* const Standard[] =
    {
        &GUID_Sine, &GUID_Triangle, &GUID_ConstantForce, &GUID_RampForce, &GUID_Square, &GUID_SawtoothUp,
        &GUID_SawtoothDown, &GUID_Spring, &GUID_Damper, &GUID_Inertia, &GUID_Friction, &GUID_CustomForce,
    };

    for (ULONG Index = 0; Index < _countof(Standard); Index++)
    {
        if (IsEqualGUID(Guid, *Standard[Index]))
            return TRUE;
    }
    return FALSE;
}

/* Writes one sized data chunk */
static
HRESULT
DiWriteData(
    _In_ HMMIO File,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ ULONG Size)
{
    MMCKINFO Chunk = {};
    HRESULT hr;

    Chunk.ckid = DI_FOURCC_DATA;
    hr = DiMmioResult(mmioCreateChunk(File, &Chunk, 0));
    if (FAILED(hr))
        return hr;

    mmioWrite(File, static_cast<const char*>(Data), Size);
    return DiMmioResult(mmioAscend(File, &Chunk, 0));
}

static
HRESULT
DiWriteEffect(
    _In_ HMMIO File,
    _In_ LPDIFILEEFFECT Entry)
{
    LPDIEFFECT Effect = const_cast<LPDIEFFECT>(Entry->lpDiEffect);
    RX_DI_FILE_EFFECT Stored = {};
    MMCKINFO List = {};
    DWORD Version = 1;
    HRESULT hr;

    List.fccType = DI_FOURCC_EFFECT;
    hr = DiMmioResult(mmioCreateChunk(File, &List, MMIO_CREATELIST));
    if (FAILED(hr))
        return hr;

    hr = DiCheckStringA(Entry->szFriendlyName, DI_FILE_LIMIT);
    if (FAILED(hr))
        return hr;

    /* Only the first part of the friendly name is kept */
    if (mmioWrite(File, Entry->szFriendlyName, DI_FILE_NAME_BYTES) != DI_FILE_NAME_BYTES)
        return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);

    Stored.dwSize = sizeof(Stored);
    Stored.dwFlags = Effect->dwFlags;
    Stored.dwDuration = Effect->dwDuration;
    Stored.dwSamplePeriod = Effect->dwSamplePeriod;
    Stored.dwGain = Effect->dwGain;
    Stored.dwTriggerButton = Effect->dwTriggerButton;
    Stored.dwTriggerRepeatInterval = Effect->dwTriggerRepeatInterval;
    Stored.cAxes = Effect->cAxes;
    Stored.cbTypeSpecificParams = Effect->cbTypeSpecificParams;
    /* The pointer slot holds a marker when data follows, else the low half of the pointer as given */
    Stored.TypeSpecificParams = (Effect->cbTypeSpecificParams != 0 && Effect->lpvTypeSpecificParams != nullptr) ? 0x100 :
                                static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(Effect->lpvTypeSpecificParams));
    Stored.dwStartDelay = Effect->dwStartDelay;
    if (mmioWrite(File, reinterpret_cast<const char*>(&Stored), sizeof(Stored)) != sizeof(Stored))
        return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);

    if (mmioWrite(File, reinterpret_cast<const char*>(&Entry->GuidEffect), sizeof(GUID)) != sizeof(GUID) ||
        mmioWrite(File, reinterpret_cast<const char*>(&Version), sizeof(Version)) != sizeof(Version))
    {
        return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }

    if (Effect->rgdwAxes != nullptr)
    {
        hr = DiWriteData(File, Effect->rgdwAxes, sizeof(DWORD) * Effect->cAxes);
        if (FAILED(hr))
            return hr;
    }

    if (Effect->rglDirection != nullptr)
    {
        hr = DiWriteData(File, Effect->rglDirection, sizeof(LONG) * Effect->cAxes);
        if (FAILED(hr))
            return hr;
    }

    if (Effect->lpEnvelope != nullptr)
    {
        hr = DiWriteData(File, Effect->lpEnvelope, Effect->lpEnvelope->dwSize);
        if (FAILED(hr))
            return hr;
    }

    if (Effect->cbTypeSpecificParams != 0 && Effect->lpvTypeSpecificParams != nullptr)
    {
        hr = DiWriteData(File, Effect->lpvTypeSpecificParams, Effect->cbTypeSpecificParams);
        if (FAILED(hr))
            return hr;
    }

    return DiMmioResult(mmioAscend(File, &List, 0));
}

/* Reads a data chunk into a buffer the caller sized */
static
HRESULT
DiReadData(
    _In_ HMMIO File,
    _Out_writes_bytes_(Size) PVOID Data,
    _In_ ULONG Size)
{
    MMCKINFO Chunk = {};
    HRESULT hr;

    Chunk.ckid = DI_FOURCC_DATA;
    hr = DiMmioResult(mmioDescend(File, &Chunk, nullptr, MMIO_FINDCHUNK));
    if (FAILED(hr))
        return hr;

    mmioRead(File, static_cast<HPSTR>(Data), Size);
    return DiMmioResult(mmioAscend(File, &Chunk, 0));
}

static
HRESULT
DiReadEffect(
    _In_ HMMIO File,
    _Inout_ LPDIFILEEFFECT Entry)
{
    LPDIEFFECT Effect = const_cast<LPDIEFFECT>(Entry->lpDiEffect);
    RX_DI_FILE_EFFECT Stored = {};
    MMCKINFO List = {};
    DWORD Version = 0;
    HRESULT hr;

    List.fccType = DI_FOURCC_EFFECT;
    hr = DiMmioResult(mmioDescend(File, &List, nullptr, MMIO_FINDLIST));
    if (FAILED(hr))
        return hr;

    if (mmioRead(File, Entry->szFriendlyName, DI_FILE_NAME_BYTES) != DI_FILE_NAME_BYTES)
        return HRESULT_FROM_WIN32(ERROR_READ_FAULT);

    /* A record of another size leaves the effect as it was */
    if (mmioRead(File, reinterpret_cast<HPSTR>(&Stored), sizeof(Stored)) == sizeof(Stored) && Stored.dwSize == sizeof(Stored))
    {
        Effect->dwSize = sizeof(DIEFFECT);
        Effect->dwFlags = Stored.dwFlags;
        Effect->dwDuration = Stored.dwDuration;
        Effect->dwSamplePeriod = Stored.dwSamplePeriod;
        Effect->dwGain = Stored.dwGain;
        Effect->dwTriggerButton = Stored.dwTriggerButton;
        Effect->dwTriggerRepeatInterval = Stored.dwTriggerRepeatInterval;
        Effect->cAxes = Stored.cAxes;
        Effect->lpvTypeSpecificParams = reinterpret_cast<LPVOID>(static_cast<ULONG_PTR>(Stored.TypeSpecificParams));
        Effect->cbTypeSpecificParams = Stored.cbTypeSpecificParams;
        Effect->dwStartDelay = Stored.dwStartDelay;
    }
    else if (Stored.dwSize == sizeof(Stored))
    {
        return HRESULT_FROM_WIN32(ERROR_READ_FAULT);
    }

    if (mmioRead(File, reinterpret_cast<HPSTR>(&Entry->GuidEffect), sizeof(GUID)) != sizeof(GUID) ||
        mmioRead(File, reinterpret_cast<HPSTR>(&Version), sizeof(Version)) != sizeof(Version))
    {
        return HRESULT_FROM_WIN32(ERROR_READ_FAULT);
    }

    if (Effect->rgdwAxes != nullptr)
    {
        hr = DiReadData(File, Effect->rgdwAxes, sizeof(DWORD) * Effect->cAxes);
        if (FAILED(hr))
            return hr;
    }

    if (Effect->rglDirection != nullptr)
    {
        hr = DiReadData(File, Effect->rglDirection, sizeof(LONG) * Effect->cAxes);
        if (FAILED(hr))
            return hr;
    }

    if (Effect->lpEnvelope != nullptr)
    {
        hr = DiReadData(File, Effect->lpEnvelope, sizeof(DIENVELOPE));
        if (FAILED(hr))
            return hr;
    }

    if (Effect->cbTypeSpecificParams != 0)
    {
        Effect->lpvTypeSpecificParams = nullptr;
        hr = DiAllocate(Effect->cbTypeSpecificParams, &Effect->lpvTypeSpecificParams);
        if (FAILED(hr))
            return hr;

        hr = DiReadData(File, Effect->lpvTypeSpecificParams, Effect->cbTypeSpecificParams);
        if (FAILED(hr))
            return hr;
    }

    return DiMmioResult(mmioAscend(File, &List, 0));
}

HRESULT
DiEnumEffectsInFile(
    _In_ RxDiDevice* Device,
    _In_opt_z_ LPCSTR Name,
    _In_opt_ LPDIENUMEFFECTSINFILECALLBACK Callback,
    _In_opt_ LPVOID Context,
    _In_ DWORD Flags)
{
    DWORD Axes[DI_FILE_AXIS_LIMIT] = {};
    LONG Directions[DI_FILE_AXIS_LIMIT] = {};
    DIENVELOPE Envelope = {};
    DIEFFECT Effect = {};
    DIFILEEFFECT Entry = {};
    MMCKINFO Form;
    HMMIO File = nullptr;
    HRESULT Read;
    HRESULT hr;
    BOOL Continue;

    hr = DiCheckStringA(Name, DI_FILE_LIMIT);
    if (FAILED(hr))
        return hr;
    if (Callback == nullptr || (Flags & ~DI_FILE_FLAGS_VALID) != 0)
        return E_INVALIDARG;

    hr = DiOpenEffectFile(Name, MMIO_ALLOCBUF, &File, &Form);
    if (FAILED(hr))
        return hr;

    Effect.rgdwAxes = Axes;
    Effect.rglDirection = Directions;
    Effect.lpEnvelope = &Envelope;
    Entry.dwSize = sizeof(Entry);
    Entry.lpDiEffect = &Effect;

    do
    {
        BOOLEAN Wanted = TRUE;

        Read = DiReadEffect(File, &Entry);
        if (FAILED(Read))
            break;

        Continue = DIENUM_CONTINUE;
        if ((Flags & DIFEF_MODIFYIFNEEDED) && FAILED(Device->ModifyEffectParams(&Effect, Entry.GuidEffect)))
            Wanted = FALSE;
        if (Wanted && (Flags & DIFEF_INCLUDENONSTANDARD) == 0 && !DiIsStandardEffect(Entry.GuidEffect))
            Wanted = FALSE;

        if (Wanted)
            Continue = Callback(&Entry, Context);

        if (Effect.cbTypeSpecificParams != 0)
        {
            DiFree(&Effect.lpvTypeSpecificParams);
            Effect.cbTypeSpecificParams = 0;
        }
    } while (Continue == DIENUM_CONTINUE);

    mmioClose(File, 0);

    /* Running out of effects is the normal end */
    return (Read == HRESULT_FROM_WIN32(ERROR_SECTOR_NOT_FOUND)) ? S_OK : Read;
}

HRESULT
DiWriteEffectToFile(
    _In_ RxDiDevice* Device,
    _In_opt_z_ LPCSTR Name,
    _In_ DWORD Count,
    _In_reads_opt_(Count) LPDIFILEEFFECT Effects,
    _In_ DWORD Flags)
{
    MMCKINFO Form;
    HMMIO File = nullptr;
    HRESULT hr;

    UNREFERENCED_PARAMETER(Device);

    hr = DiCheckStringA(Name, DI_FILE_LIMIT);
    if (FAILED(hr))
        return hr;
    if ((Flags & ~DI_FILE_FLAGS_VALID) != 0)
        return E_INVALIDARG;
    if (Effects == nullptr)
        return E_POINTER;

    hr = DiOpenEffectFile(Name, MMIO_CREATE | MMIO_WRITE | MMIO_ALLOCBUF, &File, &Form);
    if (FAILED(hr))
        return hr;

    for (DWORD Index = 0; Index < Count; Index++)
    {
        if ((Flags & DIFEF_INCLUDENONSTANDARD) || DiIsStandardEffect(Effects[Index].GuidEffect))
        {
            hr = DiWriteEffect(File, &Effects[Index]);
            if (FAILED(hr))
                break;
        }
        else
        {
            hr = S_OK;
        }
    }

    /* The form is closed without ascending, as the original writer left it */
    mmioClose(File, 0);
    return hr;
}
