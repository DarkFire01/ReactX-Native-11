/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Small Direct3D 11 scenes, each exercising a little more of the pipeline
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <memory>

using Microsoft::WRL::ComPtr;

struct SceneTarget
{
    ID3D11Device* Device;
    ID3D11DeviceContext* Context;
    ID3D11RenderTargetView* BackBuffer;
    ID3D11Texture2D* BackBufferTexture;
    UINT Width;
    UINT Height;
};

/* A scene draws the same picture for the same frame number on any runtime */
class Scene
{
public:
    virtual ~Scene() = default;

    virtual
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) = 0;

    virtual
    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) = 0;
};

struct SceneInfo
{
    PCWSTR Name;
    PCWSTR Purpose;
    std::unique_ptr<Scene> (*Create)();
};

const SceneInfo*
TdFindScene(
    _In_z_ PCWSTR Name);

VOID
TdListScenes(VOID);

/* Prints the failing call and hands the HRESULT back, so a scene can bail out in one line */
HRESULT
TdCheck(
    _In_ HRESULT Result,
    _In_z_ PCSTR What);
