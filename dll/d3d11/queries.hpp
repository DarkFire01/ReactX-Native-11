/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Queries and predicates
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "devicechild.hpp"

/* One class serves both; only predicate kinds answer for ID3D11Predicate */
class RxQuery final : public RxDeviceChild<ID3D11Predicate>
{
public:
    static constexpr GUID PrivateId = { 0x4f2a9c61, 0xd83e, 0x4b17, { 0xa5, 0x0c, 0x92, 0x6e, 0x1b, 0xf4, 0x37, 0xd8 } };

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ const D3D11_QUERY_DESC& Desc,
        _Outptr_ RxQuery** Query);

    /* Bytes GetData returns for a query kind, 0 for kinds that do not exist */
    static
    UINT
    DataSizeOf(
        _In_ D3D11_QUERY Kind);

    static
    BOOLEAN
    IsPredicateKind(
        _In_ D3D11_QUERY Kind);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    UINT
    STDMETHODCALLTYPE
    GetDataSize(VOID) override
    {
        return DataSizeOf(Desc.Query);
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ D3D11_QUERY_DESC* pDesc) override
    {
        *pDesc = Desc;
    }

    D3D10DDI_HQUERY
    GetHandle() const
    {
        return Handle;
    }

    const D3D11_QUERY_DESC&
    GetQueryDesc() const
    {
        return Desc;
    }

    /* End has been called at least once, so GetData has something to wait for */
    BOOLEAN IsIssued = FALSE;

    /* Between Begin and End */
    BOOLEAN IsBuilding = FALSE;

private:
    explicit RxQuery(
        _In_ RxDevice& Device)
        : RxDeviceChild<ID3D11Predicate>(Device)
    {
    }

    ~RxQuery() override;

    D3D11_QUERY_DESC Desc = {};
    RxDriverMemory Memory;
    D3D10DDI_HQUERY Handle = {};
    BOOLEAN IsCreated = FALSE;
};
