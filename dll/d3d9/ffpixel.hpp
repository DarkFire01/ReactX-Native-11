/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pixel shaders standing in for the fixed function texture stages
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d9types.h>

/* The texture stages a fixed function pixel shader covers */
static constexpr UINT D9FixedStages = 8;

/* Tokens the longest generated shader can take */
static constexpr UINT D9FixedPixelTokenLimit = 2048;

/*
 * One stage as the generated shader depends on it.
 * Blend: color op in bits 0-4, alpha op in bits 5-9, color args 0, 1 and 2 in bits 10-15, 16-21 and 22-27.
 * Inputs: alpha args 0, 1 and 2 in bits 0-5, 6-11 and 12-17, the result arg in bits 18-23, the coordinate
 * index in bits 24-26, the texture kind in bits 27-28, a two texel packed format in bit 29, projection in
 * bit 30 and, by coordinate set rather than by stage, a set of three floats in bit 31.
 */
typedef struct _D9_FIXED_PIXEL_STAGE
{
    DWORD Blend;
    DWORD Inputs;
} D9_FIXED_PIXEL_STAGE;

/*
 * What picks a generated shader. Size counts the bytes compared: itself, Flags and the active stages.
 * Flags: two bits per coordinate set for its float count less one, the set count in bits 16-19, the stage
 * count in bits 20-23, specular added in bit 24, point sprites in bit 25, diffuse and specular inputs in
 * bits 26 and 27.
 */
typedef struct _D9_FIXED_PIXEL_KEY
{
    DWORD Size;
    DWORD Flags;
    D9_FIXED_PIXEL_STAGE Stages[D9FixedStages];
} D9_FIXED_PIXEL_KEY;

static constexpr DWORD D9FixedSpecularAdd = 0x01000000;
static constexpr DWORD D9FixedPointSprite = 0x02000000;
static constexpr DWORD D9FixedHasDiffuse = 0x04000000;
static constexpr DWORD D9FixedHasSpecular = 0x08000000;
static constexpr DWORD D9FixedNeverMatches = 0x20000000;

/* Texture kinds a stage samples */
static constexpr DWORD D9FixedTexNone = 0;
static constexpr DWORD D9FixedTex2D = 1;
static constexpr DWORD D9FixedTexCube = 2;
static constexpr DWORD D9FixedTexVolume = 3;

/* The alpha op kept when the application set the largest value */
static constexpr DWORD D9FixedAlphaUnset = 27;

/* The pixel constants the generated shaders read */
static constexpr UINT D9FixedConstFactor = 2;
static constexpr UINT D9FixedConstStage = 3;
static constexpr UINT D9FixedConstLuminance = 11;
static constexpr UINT D9FixedConstBump = 15;
static constexpr UINT D9FixedConstCount = 31;

/* The most shaders the converter keeps before it lets the oldest go with each new one */
static constexpr UINT D9FixedCacheLimit = 1001;

typedef struct _D9_FIXED_PIXEL_ENTRY
{
    D9_FIXED_PIXEL_KEY Key;
    HANDLE Shader;
} D9_FIXED_PIXEL_ENTRY;

/* What the fixed function pixel shaders depend on, the constants they read and the shaders made so far */
class D9PixelConverter
{
public:
    ~D9PixelConverter()
    {
        delete[] Cache;
    }

    /**
     * @brief Takes the state a new device starts with.
     * @param Stages The default stage states, by stage and type.
     * @param Factor The default texture factor.
     */
    VOID
    Start(
        _In_reads_(D9FixedStages * 33) const DWORD* Stages,
        _In_ DWORD Factor);

    /**
     * @brief Takes a stage state.
     * @return TRUE when the active stage count was worked out again.
     */
    BOOLEAN
    SetStage(
        _In_ UINT Stage,
        _In_ UINT Type,
        _In_ DWORD Value);

    VOID
    SetFactor(
        _In_ DWORD Color);

    VOID
    SetSpecular(
        _In_ BOOLEAN Enable);

    VOID
    SetPointSprite(
        _In_ BOOLEAN Enable);

    VOID
    SetFvf(
        _In_ DWORD Fvf);

    VOID
    SetElements(
        _In_ const D3DVERTEXELEMENT9* Elements);

    VOID
    SetTexture(
        _In_ UINT Stage,
        _In_ DWORD Kind,
        _In_ DWORD Format);

    /* The stages before the first disabled one */
    UINT
    ActiveStages(VOID) const
    {
        return StageCount;
    }

    /**
     * @brief Looks the current key up among the shaders made so far.
     * @return TRUE when Shader holds one; FALSE when one has to be built.
     */
    BOOLEAN
    Find(
        _Out_ HANDLE* Shader);

    /**
     * @brief Keeps a shader made for the current key.
     * @param Evicted Receives the oldest shader when the cache let it go, or NULL.
     * @return FALSE when there is no memory for the cache.
     */
    BOOLEAN
    Keep(
        _In_ HANDLE Shader,
        _Out_ HANDLE* Evicted);

    /**
     * @brief Hands out the cached shaders oldest first and forgets them.
     * @return FALSE when none is left.
     */
    BOOLEAN
    TakeOldest(
        _Out_ HANDLE* Shader);

    D9_FIXED_PIXEL_KEY Key = {};
    BOOLEAN Dirty = TRUE;
    BOOLEAN StageZeroTextured = FALSE;

    /* The pixel constants the shaders read, and one past the highest register ever set */
    FLOAT Constants[D9FixedConstCount][4] = {};
    UINT ConstantMark = 0;

    /* The shader the last draw used and the key it was made for */
    D9_FIXED_PIXEL_KEY UsedKey = {};
    HANDLE UsedShader = NULL;

private:
    VOID
    KeepConstant(
        _In_ UINT Register);

    UINT StageCount = 0;
    DWORD Fvf = 0;
    DWORD TextureKinds[D9FixedStages] = {};
    DWORD TextureFormats[D9FixedStages] = {};
    D9_FIXED_PIXEL_ENTRY* Cache = nullptr;
    UINT CacheFirst = 0;
    UINT CacheCount = 0;
};

/**
 * @brief Builds the ps_2_0 tokens for a key.
 * @param Key The stages and inputs.
 * @param StageZeroTextured Stage 0 has a texture bound, which picks the second alpha arg for an unset op.
 * @param Tokens Receives the shader.
 * @param Count Receives the token count.
 * @return FALSE when the shader does not fit.
 */
BOOLEAN
D9BuildFixedPixelShader(
    _In_ const D9_FIXED_PIXEL_KEY* Key,
    _In_ BOOLEAN StageZeroTextured,
    _Out_writes_(D9FixedPixelTokenLimit) DWORD* Tokens,
    _Out_ UINT* Count);
