/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 10 faces of resources, views, states, shaders and queries
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d10convert.hpp"
#include "devicechild.hpp"

/* Lifetime, device and private data all belong to the Direct3D 11 object the face is part of */
template <typename Interface>
class RxD3D10Child : public Interface, public RxD3D10FaceBase
{
public:
    RxD3D10Child(
        _In_ ID3D11DeviceChild* Object,
        _In_ RxDevice& Device)
        : Object(Object),
          Device(Device)
    {
    }

    IUnknown*
    GetUnknown() override
    {
        return static_cast<Interface*>(this);
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override
    {
        return Object->QueryInterface(riid, ppvObject);
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return Object->AddRef();
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        return Object->Release();
    }

    VOID
    STDMETHODCALLTYPE
    GetDevice(
        _Out_ ID3D10Device** ppDevice) override
    {
        Device.QueryInterface(IID_PPV_ARGS(ppDevice));
    }

    HRESULT
    STDMETHODCALLTYPE
    GetPrivateData(
        _In_ REFGUID guid,
        _Inout_ UINT* pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void* pData) override
    {
        return Object->GetPrivateData(guid, pDataSize, pData);
    }

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateData(
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void* pData) override
    {
        return Object->SetPrivateData(guid, DataSize, pData);
    }

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateDataInterface(
        _In_ REFGUID guid,
        _In_opt_ const IUnknown* pData) override
    {
        return Object->SetPrivateDataInterface(guid, pData);
    }

protected:
    ID3D11DeviceChild* Object;
    RxDevice& Device;
};

/* Resources: Map and Unmap go through the immediate context */

template <typename Interface>
class RxD3D10Resource : public RxD3D10Child<Interface>
{
public:
    using RxD3D10Child<Interface>::RxD3D10Child;

    VOID
    STDMETHODCALLTYPE
    GetType(
        _Out_ D3D10_RESOURCE_DIMENSION* rType) override
    {
        D3D11_RESOURCE_DIMENSION Type;

        static_cast<ID3D11Resource*>(Resource())->GetType(&Type);
        *rType = (D3D10_RESOURCE_DIMENSION)Type;
    }

    VOID
    STDMETHODCALLTYPE
    SetEvictionPriority(
        _In_ UINT EvictionPriority) override
    {
        Resource()->SetEvictionPriority(EvictionPriority);
    }

    UINT
    STDMETHODCALLTYPE
    GetEvictionPriority(VOID) override
    {
        return Resource()->GetEvictionPriority();
    }

protected:
    ID3D11Resource*
    Resource() const
    {
        return static_cast<ID3D11Resource*>(this->Object);
    }

    HRESULT
    MapSubresource(
        _In_ UINT Subresource,
        _In_ D3D10_MAP MapType,
        _In_ UINT MapFlags,
        _Out_ D3D11_MAPPED_SUBRESOURCE* Mapped)
    {
        return this->Device.GetContext().Map(Resource(), Subresource, (D3D11_MAP)MapType, MapFlags, Mapped);
    }

    VOID
    UnmapSubresource(
        _In_ UINT Subresource)
    {
        this->Device.GetContext().Unmap(Resource(), Subresource);
    }
};

class RxD3D10Buffer final : public RxD3D10Resource<ID3D10Buffer>
{
public:
    using RxD3D10Resource<ID3D10Buffer>::RxD3D10Resource;

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10Resource) || riid == __uuidof(ID3D10Buffer);
    }

    HRESULT
    STDMETHODCALLTYPE
    Map(
        _In_ D3D10_MAP MapType,
        _In_ UINT MapFlags,
        _Out_ void** ppData) override
    {
        D3D11_MAPPED_SUBRESOURCE Mapped = {};
        HRESULT hr = MapSubresource(0, MapType, MapFlags, &Mapped);

        *ppData = Mapped.pData;
        return hr;
    }

    VOID
    STDMETHODCALLTYPE
    Unmap(VOID) override
    {
        UnmapSubresource(0);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ D3D10_BUFFER_DESC* pDesc) override
    {
        D3D11_BUFFER_DESC Desc;

        static_cast<ID3D11Buffer*>(Object)->GetDesc(&Desc);
        *pDesc = RxToD3D10Desc(Desc);
    }
};

class RxD3D10Texture1D final : public RxD3D10Resource<ID3D10Texture1D>
{
public:
    using RxD3D10Resource<ID3D10Texture1D>::RxD3D10Resource;

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10Resource) ||
               riid == __uuidof(ID3D10Texture1D);
    }

    HRESULT
    STDMETHODCALLTYPE
    Map(
        _In_ UINT Subresource,
        _In_ D3D10_MAP MapType,
        _In_ UINT MapFlags,
        _Out_ void** ppData) override
    {
        D3D11_MAPPED_SUBRESOURCE Mapped = {};
        HRESULT hr = MapSubresource(Subresource, MapType, MapFlags, &Mapped);

        *ppData = Mapped.pData;
        return hr;
    }

    VOID
    STDMETHODCALLTYPE
    Unmap(
        _In_ UINT Subresource) override
    {
        UnmapSubresource(Subresource);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ D3D10_TEXTURE1D_DESC* pDesc) override
    {
        D3D11_TEXTURE1D_DESC Desc;

        static_cast<ID3D11Texture1D*>(Object)->GetDesc(&Desc);
        *pDesc = RxToD3D10Desc(Desc);
    }
};

class RxD3D10Texture2D final : public RxD3D10Resource<ID3D10Texture2D>
{
public:
    using RxD3D10Resource<ID3D10Texture2D>::RxD3D10Resource;

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10Resource) ||
               riid == __uuidof(ID3D10Texture2D);
    }

    HRESULT
    STDMETHODCALLTYPE
    Map(
        _In_ UINT Subresource,
        _In_ D3D10_MAP MapType,
        _In_ UINT MapFlags,
        _Out_ D3D10_MAPPED_TEXTURE2D* pMappedTex2D) override
    {
        D3D11_MAPPED_SUBRESOURCE Mapped = {};
        HRESULT hr = MapSubresource(Subresource, MapType, MapFlags, &Mapped);

        pMappedTex2D->pData = Mapped.pData;
        pMappedTex2D->RowPitch = Mapped.RowPitch;
        return hr;
    }

    VOID
    STDMETHODCALLTYPE
    Unmap(
        _In_ UINT Subresource) override
    {
        UnmapSubresource(Subresource);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ D3D10_TEXTURE2D_DESC* pDesc) override
    {
        D3D11_TEXTURE2D_DESC Desc;

        static_cast<ID3D11Texture2D*>(Object)->GetDesc(&Desc);
        *pDesc = RxToD3D10Desc(Desc);
    }
};

class RxD3D10Texture3D final : public RxD3D10Resource<ID3D10Texture3D>
{
public:
    using RxD3D10Resource<ID3D10Texture3D>::RxD3D10Resource;

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10Resource) ||
               riid == __uuidof(ID3D10Texture3D);
    }

    HRESULT
    STDMETHODCALLTYPE
    Map(
        _In_ UINT Subresource,
        _In_ D3D10_MAP MapType,
        _In_ UINT MapFlags,
        _Out_ D3D10_MAPPED_TEXTURE3D* pMappedTex3D) override
    {
        D3D11_MAPPED_SUBRESOURCE Mapped = {};
        HRESULT hr = MapSubresource(Subresource, MapType, MapFlags, &Mapped);

        pMappedTex3D->pData = Mapped.pData;
        pMappedTex3D->RowPitch = Mapped.RowPitch;
        pMappedTex3D->DepthPitch = Mapped.DepthPitch;
        return hr;
    }

    VOID
    STDMETHODCALLTYPE
    Unmap(
        _In_ UINT Subresource) override
    {
        UnmapSubresource(Subresource);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ D3D10_TEXTURE3D_DESC* pDesc) override
    {
        D3D11_TEXTURE3D_DESC Desc;

        static_cast<ID3D11Texture3D*>(Object)->GetDesc(&Desc);
        *pDesc = RxToD3D10Desc(Desc);
    }
};

/* Views */

template <typename Interface>
class RxD3D10View : public RxD3D10Child<Interface>
{
public:
    using RxD3D10Child<Interface>::RxD3D10Child;

    VOID
    STDMETHODCALLTYPE
    GetResource(
        _Out_ ID3D10Resource** ppResource) override
    {
        ID3D11Resource* Resource = nullptr;

        *ppResource = nullptr;
        static_cast<ID3D11View*>(this->Object)->GetResource(&Resource);
        if (Resource != nullptr)
        {
            Resource->QueryInterface(IID_PPV_ARGS(ppResource));
            Resource->Release();
        }
    }
};

class RxD3D10ShaderResourceView final : public RxD3D10View<ID3D10ShaderResourceView1>
{
public:
    using RxD3D10View<ID3D10ShaderResourceView1>::RxD3D10View;

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10View) ||
               riid == __uuidof(ID3D10ShaderResourceView) || riid == __uuidof(ID3D10ShaderResourceView1);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ D3D10_SHADER_RESOURCE_VIEW_DESC* pDesc) override
    {
        D3D10_SHADER_RESOURCE_VIEW_DESC1 Desc1;

        GetDesc1(&Desc1);
        memcpy(pDesc, &Desc1, sizeof(*pDesc));
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc1(
        _Out_ D3D10_SHADER_RESOURCE_VIEW_DESC1* pDesc) override
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC Desc;

        static_cast<ID3D11ShaderResourceView*>(Object)->GetDesc(&Desc);
        *pDesc = RxToD3D10Desc(Desc);
    }
};

class RxD3D10RenderTargetView final : public RxD3D10View<ID3D10RenderTargetView>
{
public:
    using RxD3D10View<ID3D10RenderTargetView>::RxD3D10View;

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10View) ||
               riid == __uuidof(ID3D10RenderTargetView);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ D3D10_RENDER_TARGET_VIEW_DESC* pDesc) override
    {
        D3D11_RENDER_TARGET_VIEW_DESC Desc;

        static_cast<ID3D11RenderTargetView*>(Object)->GetDesc(&Desc);
        *pDesc = RxToD3D10Desc(Desc);
    }
};

class RxD3D10DepthStencilView final : public RxD3D10View<ID3D10DepthStencilView>
{
public:
    using RxD3D10View<ID3D10DepthStencilView>::RxD3D10View;

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10View) ||
               riid == __uuidof(ID3D10DepthStencilView);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ D3D10_DEPTH_STENCIL_VIEW_DESC* pDesc) override
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC Desc;

        static_cast<ID3D11DepthStencilView*>(Object)->GetDesc(&Desc);
        *pDesc = RxToD3D10Desc(Desc);
    }
};

/* States */

class RxD3D10BlendState final : public RxD3D10Child<ID3D10BlendState1>
{
public:
    using RxD3D10Child<ID3D10BlendState1>::RxD3D10Child;

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10BlendState) ||
               riid == __uuidof(ID3D10BlendState1);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ D3D10_BLEND_DESC* pDesc) override
    {
        D3D11_BLEND_DESC Desc;

        static_cast<ID3D11BlendState*>(Object)->GetDesc(&Desc);
        *pDesc = RxToD3D10Desc(Desc);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc1(
        _Out_ D3D10_BLEND_DESC1* pDesc) override
    {
        D3D11_BLEND_DESC Desc;

        static_cast<ID3D11BlendState*>(Object)->GetDesc(&Desc);
        static_assert(sizeof(D3D10_BLEND_DESC1) == sizeof(D3D11_BLEND_DESC));
        memcpy(pDesc, &Desc, sizeof(*pDesc));
    }
};

/* The remaining state descs share their layout with Direct3D 11 */
template <typename Interface, typename Interface11, typename Desc10, typename Desc11>
class RxD3D10PlainState final : public RxD3D10Child<Interface>
{
public:
    using RxD3D10Child<Interface>::RxD3D10Child;

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(Interface);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ Desc10* pDesc) override
    {
        Desc11 Desc;

        static_assert(sizeof(Desc10) == sizeof(Desc11));
        static_cast<Interface11*>(this->Object)->GetDesc(&Desc);
        memcpy(pDesc, &Desc, sizeof(*pDesc));
    }
};

using RxD3D10DepthStencilState = RxD3D10PlainState<ID3D10DepthStencilState, ID3D11DepthStencilState,
                                                   D3D10_DEPTH_STENCIL_DESC, D3D11_DEPTH_STENCIL_DESC>;
using RxD3D10RasterizerState = RxD3D10PlainState<ID3D10RasterizerState, ID3D11RasterizerState,
                                                 D3D10_RASTERIZER_DESC, D3D11_RASTERIZER_DESC>;
using RxD3D10SamplerState = RxD3D10PlainState<ID3D10SamplerState, ID3D11SamplerState,
                                              D3D10_SAMPLER_DESC, D3D11_SAMPLER_DESC>;

/* Shaders and input layouts have nothing past the device child */
template <typename Interface>
class RxD3D10Bare final : public RxD3D10Child<Interface>
{
public:
    using RxD3D10Child<Interface>::RxD3D10Child;

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(Interface);
    }
};

/* Queries: Direct3D 10 begins, ends and reads them on the object itself */

class RxD3D10Query final : public RxD3D10Child<ID3D10Predicate>
{
public:
    RxD3D10Query(
        _In_ ID3D11DeviceChild* Object,
        _In_ RxDevice& Device,
        _In_ BOOLEAN IsPredicate)
        : RxD3D10Child<ID3D10Predicate>(Object, Device),
          IsPredicate(IsPredicate)
    {
    }

    BOOLEAN
    Answers(
        _In_ REFIID riid) override
    {
        return riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10Asynchronous) ||
               riid == __uuidof(ID3D10Query) || (IsPredicate && riid == __uuidof(ID3D10Predicate));
    }

    VOID
    STDMETHODCALLTYPE
    Begin(VOID) override
    {
        Device.GetContext().Begin(static_cast<ID3D11Asynchronous*>(Object));
    }

    VOID
    STDMETHODCALLTYPE
    End(VOID) override
    {
        Device.GetContext().End(static_cast<ID3D11Asynchronous*>(Object));
    }

    HRESULT
    STDMETHODCALLTYPE
    GetData(
        _Out_writes_bytes_opt_(DataSize) void* pData,
        _In_ UINT DataSize,
        _In_ UINT GetDataFlags) override
    {
        return Device.GetContext().GetData(static_cast<ID3D11Asynchronous*>(Object), pData, DataSize, GetDataFlags);
    }

    UINT
    STDMETHODCALLTYPE
    GetDataSize(VOID) override
    {
        return static_cast<ID3D11Asynchronous*>(Object)->GetDataSize();
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ D3D10_QUERY_DESC* pDesc) override
    {
        D3D11_QUERY_DESC Desc;

        static_cast<ID3D11Query*>(Object)->GetDesc(&Desc);
        pDesc->Query = (D3D10_QUERY)Desc.Query;
        pDesc->MiscFlags = Desc.MiscFlags;
    }

private:
    BOOLEAN IsPredicate;
};

/* Which face an object gets follows from the Direct3D 11 interface it has */
template <typename Interface11>
static
BOOLEAN
RxIs(
    _In_ ID3D11DeviceChild* Object)
{
    Interface11* Found = nullptr;

    if (FAILED(Object->QueryInterface(IID_PPV_ARGS(&Found))))
        return FALSE;

    Found->Release();
    return TRUE;
}

_Ret_maybenull_
static
RxD3D10FaceBase*
RxMakeD3D10Face(
    _In_ ID3D11DeviceChild* Object,
    _In_ RxDevice& Device)
{
    if (RxIs<ID3D11Buffer>(Object))
        return new (std::nothrow) RxD3D10Buffer(Object, Device);
    if (RxIs<ID3D11Texture1D>(Object))
        return new (std::nothrow) RxD3D10Texture1D(Object, Device);
    if (RxIs<ID3D11Texture2D>(Object))
        return new (std::nothrow) RxD3D10Texture2D(Object, Device);
    if (RxIs<ID3D11Texture3D>(Object))
        return new (std::nothrow) RxD3D10Texture3D(Object, Device);
    if (RxIs<ID3D11ShaderResourceView>(Object))
        return new (std::nothrow) RxD3D10ShaderResourceView(Object, Device);
    if (RxIs<ID3D11RenderTargetView>(Object))
        return new (std::nothrow) RxD3D10RenderTargetView(Object, Device);
    if (RxIs<ID3D11DepthStencilView>(Object))
        return new (std::nothrow) RxD3D10DepthStencilView(Object, Device);
    if (RxIs<ID3D11BlendState>(Object))
        return new (std::nothrow) RxD3D10BlendState(Object, Device);
    if (RxIs<ID3D11DepthStencilState>(Object))
        return new (std::nothrow) RxD3D10DepthStencilState(Object, Device);
    if (RxIs<ID3D11RasterizerState>(Object))
        return new (std::nothrow) RxD3D10RasterizerState(Object, Device);
    if (RxIs<ID3D11SamplerState>(Object))
        return new (std::nothrow) RxD3D10SamplerState(Object, Device);
    if (RxIs<ID3D11VertexShader>(Object))
        return new (std::nothrow) RxD3D10Bare<ID3D10VertexShader>(Object, Device);
    if (RxIs<ID3D11GeometryShader>(Object))
        return new (std::nothrow) RxD3D10Bare<ID3D10GeometryShader>(Object, Device);
    if (RxIs<ID3D11PixelShader>(Object))
        return new (std::nothrow) RxD3D10Bare<ID3D10PixelShader>(Object, Device);
    if (RxIs<ID3D11InputLayout>(Object))
        return new (std::nothrow) RxD3D10Bare<ID3D10InputLayout>(Object, Device);
    if (RxIs<ID3D11Predicate>(Object))
        return new (std::nothrow) RxD3D10Query(Object, Device, TRUE);
    if (RxIs<ID3D11Query>(Object))
        return new (std::nothrow) RxD3D10Query(Object, Device, FALSE);

    return nullptr;
}

/* Only these ask for a face; everything else is left to the object */
static
BOOLEAN
RxIsD3D10FaceIid(
    _In_ REFIID riid)
{
    static const GUID* const Known[] =
    {
        &__uuidof(ID3D10DeviceChild), &__uuidof(ID3D10Resource), &__uuidof(ID3D10Buffer),
        &__uuidof(ID3D10Texture1D), &__uuidof(ID3D10Texture2D), &__uuidof(ID3D10Texture3D),
        &__uuidof(ID3D10View), &__uuidof(ID3D10ShaderResourceView), &__uuidof(ID3D10ShaderResourceView1),
        &__uuidof(ID3D10RenderTargetView), &__uuidof(ID3D10DepthStencilView), &__uuidof(ID3D10BlendState),
        &__uuidof(ID3D10BlendState1), &__uuidof(ID3D10DepthStencilState), &__uuidof(ID3D10RasterizerState),
        &__uuidof(ID3D10SamplerState), &__uuidof(ID3D10VertexShader), &__uuidof(ID3D10GeometryShader),
        &__uuidof(ID3D10PixelShader), &__uuidof(ID3D10InputLayout), &__uuidof(ID3D10Asynchronous),
        &__uuidof(ID3D10Query), &__uuidof(ID3D10Predicate),
    };

    for (const GUID* Id : Known)
    {
        if (riid == *Id)
            return TRUE;
    }

    return FALSE;
}

HRESULT
RxQueryD3D10Face(
    _In_ RxChildLifetime& Lifetime,
    _In_ ID3D11DeviceChild* Object,
    _In_ RxDevice& Device,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    RxD3D10FaceBase* Face;

    RxD3D10FaceBase* Made = nullptr;

    *ppvObject = nullptr;
    if (!Device.IsD3D10() || !RxIsD3D10FaceIid(riid))
        return E_NOINTERFACE;

    /* The face is made once and lives as long as the object; making it asks the object questions */
    AcquireSRWLockShared(&Device.D3D10FaceLock);
    Face = Lifetime.D3D10Face;
    ReleaseSRWLockShared(&Device.D3D10FaceLock);

    if (Face == nullptr)
    {
        Made = RxMakeD3D10Face(Object, Device);
        AcquireSRWLockExclusive(&Device.D3D10FaceLock);
        if (Lifetime.D3D10Face == nullptr)
        {
            Lifetime.D3D10Face = Made;
            Made = nullptr;
        }
        Face = Lifetime.D3D10Face;
        ReleaseSRWLockExclusive(&Device.D3D10FaceLock);
        delete Made;
    }

    if (Face == nullptr || !Face->Answers(riid))
        return E_NOINTERFACE;

    *ppvObject = Face->GetUnknown();
    Object->AddRef();
    return S_OK;
}
