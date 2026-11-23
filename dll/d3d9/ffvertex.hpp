/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Vertex shaders standing in for the fixed function vertex pipeline
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d9types.h>

/* The texture stages and coordinate sets the generated shaders cover */
static constexpr UINT D9VertexStages = 8;

/* Tokens the longest generated shader can take */
static constexpr UINT D9FixedVertexTokenLimit = 4096;

/* Registers the converter keeps; the lights and blend matrices follow the fixed block */
static constexpr UINT D9VertexFixedRegisters = 105;
static constexpr UINT D9VertexRegisterLimit = 1024;

/*
 * What picks a generated shader. Size counts the bytes compared: itself, Inputs, Modes and one byte per active
 * stage.
 * Inputs: position, second position, normal, second normal, diffuse, specular, point size and blend weights in
 * bits 0-7, the coordinate sets present in bits 8-15, blend indices in bit 16, the outputs in bits 17-21
 * (position, diffuse, specular, point size, fog), blending in bit 22, tweening in bit 23, then lighting, local
 * viewer, indexed blending, fog, range fog, specular, color vertex and normalization in bits 24-31.
 * Modes: directional, point and spot lights in bits 0-2, a single light in bit 3, point scaling in bit 4, a
 * point list in bit 5, the active stage count in bits 6-9, the material sources in bits 10-17, the blend weight
 * count in bits 18-19, the vertex fog mode in bits 20-21, table fog in bit 22 and projected stages in bits 24-31.
 * Stages: by stage, the coordinate source or generation in bits 0-3 and the transform count in bits 4-5; by
 * coordinate set, the float count less one in bits 6-7.
 */
typedef struct _D9_FIXED_VERTEX_KEY
{
    DWORD Size;
    DWORD Inputs;
    DWORD Modes;
    BYTE Stages[D9VertexStages];
    DWORD Outputs;
    DWORD Needs;
} D9_FIXED_VERTEX_KEY;

/* Needs worked out from the key: an eye space position, an eye space normal, blending and indexed blending */
static constexpr DWORD D9VertexNeedsPosition = 0x1;
static constexpr DWORD D9VertexNeedsNormal = 0x2;
static constexpr DWORD D9VertexNeedsBlend = 0x4;
static constexpr DWORD D9VertexNeedsIndices = 0x8;

/* One of the three ranges of registers waiting for the driver */
typedef struct _D9_VERTEX_RANGE
{
    UINT First;
    UINT End;
} D9_VERTEX_RANGE;

typedef struct _D9_FIXED_VERTEX_ENTRY
{
    D9_FIXED_VERTEX_KEY Key;
    HANDLE Shader;
} D9_FIXED_VERTEX_ENTRY;

/* A light the application set, and whether it is enabled */
typedef struct _D9_VS_LIGHT
{
    DWORD Index;
    BOOLEAN Enabled;
    D3DLIGHT9 Light;
} D9_VS_LIGHT;

/* The most lights the converter remembers */
static constexpr UINT D9VertexLightCapacity = 256;

/* The most shaders the converter keeps before it lets the oldest go with each new one */
static constexpr UINT D9VertexCacheLimit = 1001;

/* What the fixed function vertex shaders depend on, the registers they read and the shaders made so far */
class D9VertexConverter
{
public:
    ~D9VertexConverter()
    {
        delete[] Cache;
        delete[] Lights;
    }

    /**
     * @brief Starts over with the registers and matrices a new device has.
     * @param Registers The vertex registers the driver offers.
     * @param FlowControl The driver nests dynamic flow control deeper than one level.
     * @param Projected The driver divides projected coordinates itself.
     */
    VOID
    Start(
        _In_ UINT Registers,
        _In_ BOOLEAN FlowControl,
        _In_ BOOLEAN Projected);

    /**
     * @brief Takes a render state.
     * @return TRUE when the state belongs to the converter alone.
     */
    BOOLEAN
    SetRenderState(
        _In_ UINT State,
        _In_ DWORD Value);

    VOID
    SetStage(
        _In_ UINT Stage,
        _In_ UINT Type,
        _In_ DWORD Value);

    VOID
    SetTransform(
        _In_ DWORD Type,
        _In_ const D3DMATRIX* Matrix);

    VOID
    SetFvf(
        _In_ DWORD Fvf);

    VOID
    SetElements(
        _In_ const D3DVERTEXELEMENT9* Elements);

    VOID
    SetPointList(
        _In_ BOOLEAN Points);

    VOID
    SetMaterial(
        _In_ const D3DMATERIAL9* Material);

    VOID
    SetViewportSize(
        _In_ DWORD Width,
        _In_ DWORD Height);

    /**
     * @brief Takes a light the application set.
     * @return FALSE when there is no room for another light.
     */
    BOOLEAN
    SetLight(
        _In_ DWORD Index,
        _In_ const D3DLIGHT9* Light);

    BOOLEAN
    EnableLight(
        _In_ DWORD Index,
        _In_ BOOLEAN Enable);

    /**
     * @brief Counts the enabled lights again, sorts their registers by kind and fills the loop registers.
     * @return TRUE when the driver needs the loop registers.
     */
    BOOLEAN
    GatherLights(VOID);

    /**
     * @brief Works out the key the next draw needs.
     * @return TRUE when it differs from the key the last draw used.
     */
    BOOLEAN
    RefreshKey(VOID);

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

    /**
     * @brief Brings the registers the matrices and states feed up to date.
     */
    VOID
    UpdateRegisters(VOID);

    /**
     * @brief Hands out the next range of registers waiting for the driver, in the order the driver gets them.
     * @return FALSE when none waits.
     */
    BOOLEAN
    TakeRange(
        _Out_ UINT* First,
        _Out_ UINT* Count);

    D9_FIXED_VERTEX_KEY Key = {};
    BOOLEAN Dirty = TRUE;
    BOOLEAN FlowControl = FALSE;
    BOOLEAN Projected = FALSE;

    /* Registers the shaders read, the highest one ever written and the integer registers of the light loops */
    FLOAT Registers[D9VertexRegisterLimit][4] = {};
    UINT Mark = 0;
    INT Loops[3][4] = {};

    /* The shader the last draw used and the key it was made for */
    D9_FIXED_VERTEX_KEY UsedKey = {};
    HANDLE UsedShader = NULL;

    /* Matrices and states that wait to reach the registers */
    DWORD Stale = 0xFFFFFFFF;
    DWORD Updated = 0;

private:
    VOID
    Touch(
        _In_ UINT Range,
        _In_ UINT First,
        _In_ UINT End);

    VOID
    WriteMatrix(
        _In_ UINT Register,
        _In_ const D3DMATRIX* Matrix);

    VOID
    ApplyMaterialSources(VOID);

    D9_VS_LIGHT*
    FindLight(
        _In_ DWORD Index);

    VOID
    WriteLight(
        _In_ UINT Slot,
        _In_ const D3DLIGHT9* Light);

    VOID
    TransformLights(VOID);

    D9_VERTEX_RANGE Ranges[3] = {};
    UINT LightLimit = 0;
    UINT MatrixLimit = 0;
    UINT DisabledStages = 0xFF;
    BOOLEAN Lighting = FALSE;
    BOOLEAN ColorVertex = FALSE;
    DWORD Sources[4] = {};
    FLOAT FogStart = 0.0f;
    FLOAT FogEnd = 0.0f;
    D3DMATRIX View = {};
    D3DMATRIX Projection = {};
    D3DMATRIX World = {};
    D3DMATRIX WorldView = {};
    D9_VS_LIGHT* Lights = nullptr;
    UINT LightCount = 0;
    UINT Enabled[D9VertexLightCapacity] = {};
    UINT EnabledCount = 0;
    const D9_VS_LIGHT* Active[D9VertexLightCapacity] = {};
    UINT ActiveSlots[D9VertexLightCapacity] = {};
    UINT ActiveCount = 0;
    D9_FIXED_VERTEX_ENTRY* Cache = nullptr;
    UINT CacheFirst = 0;
    UINT CacheCount = 0;
};

/**
 * @brief Builds the vs_2_0 tokens for a key.
 * @param Key The inputs, modes and stages; its Outputs member receives what the shader writes.
 * @param FlowControl The shader may branch on point sizes.
 * @param Projected The driver divides projected coordinates itself.
 * @param Tokens Receives the shader.
 * @param Count Receives the token count.
 * @return FALSE when the key needs a part the converter does not make.
 */
BOOLEAN
D9BuildFixedVertexShader(
    _Inout_ D9_FIXED_VERTEX_KEY* Key,
    _In_ BOOLEAN FlowControl,
    _In_ BOOLEAN Projected,
    _Out_writes_(D9FixedVertexTokenLimit) DWORD* Tokens,
    _Out_ UINT* Count);
