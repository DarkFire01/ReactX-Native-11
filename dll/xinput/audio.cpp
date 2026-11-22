/*
 * PROJECT:     ReactX XInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The headset audio endpoints that share a controller's container
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

/* The device property keys and endpoint classes get their storage here */
#include <initguid.h>
#include "xinput.hpp"

#include <devquery.h>
#include <mmdeviceapi.h>
#include <strsafe.h>

typedef HRESULT (WINAPI* PXI_CREATE_QUERY)(DEV_OBJECT_TYPE, ULONG, ULONG, const DEVPROPCOMPKEY*, ULONG,
                                          const DEVPROP_FILTER_EXPRESSION*, PDEV_QUERY_RESULT_CALLBACK, PVOID, PHDEVQUERY);
typedef VOID (WINAPI* PXI_CLOSE_QUERY)(HDEVQUERY);

/* The endpoint property a query asks for, the interface ID of the endpoint */
static const DEVPROPKEY XiDeviceInterfaceIdKey = { { 0x9C119480, 0xDDC2, 0x4954, { 0xA1, 0x50, 0x5B, 0xD2, 0x40, 0xD4, 0x54, 0xAD } }, 1 };

static const GUID XiMediaClass = { 0x4D36E96C, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } };

/* The endpoint's interface ID, which the first result of a query writes out */
typedef struct _XI_ENDPOINT_QUERY
{
    HANDLE Done;
    DEVPROPCOMPKEY Property;
    LPWSTR Id;
    UINT* Count;
} XI_ENDPOINT_QUERY;

static
VOID
WINAPI
XiEndpointCallback(
    _In_ HDEVQUERY Query,
    _In_opt_ PVOID Context,
    _In_ const DEV_QUERY_RESULT_ACTION_DATA* Action)
{
    XI_ENDPOINT_QUERY* Endpoint = static_cast<XI_ENDPOINT_QUERY*>(Context);

    UNREFERENCED_PARAMETER(Query);

    if (Action == nullptr || Endpoint == nullptr)
        return;

    if (Action->Action == DevQueryResultAdd)
    {
        PCWSTR Id = Action->Data.DeviceObject.pszObjectId;
        UINT Length;

        if (Endpoint->Count == nullptr || Id == nullptr)
            return;
        Length = (UINT)wcslen(Id) + 1;
        if (Endpoint->Id != nullptr)
            memcpy(Endpoint->Id, Id, ((Length < *Endpoint->Count) ? Length : *Endpoint->Count) * sizeof(WCHAR));
        *Endpoint->Count = Length;
        Endpoint->Count = nullptr;
    }
    else if (Action->Action == DevQueryResultStateChange)
    {
        SetEvent(Endpoint->Done);
    }
}

/* Interfaces of the endpoint class whose container is the controller's */
static
HRESULT
XiQueryEndpoint(
    _Inout_ XI_ENDPOINT_QUERY* Endpoint,
    _In_ const GUID* Container,
    _In_ const GUID* EndpointClass,
    _In_opt_ LPWSTR Id,
    _In_ UINT* Count)
{
    static PXI_CREATE_QUERY CreateQuery;
    static PXI_CLOSE_QUERY CloseQuery;
    DEVPROP_FILTER_EXPRESSION Filters[4] = {};
    GUID ContainerValue = *Container;
    GUID ClassValue = *EndpointClass;
    HDEVQUERY Query = nullptr;
    HRESULT hr;

    if (CreateQuery == nullptr || CloseQuery == nullptr)
    {
        HMODULE Module = LoadLibraryExW(L"cfgmgr32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

        if (Module == nullptr)
        {
            XI_TRACE(L"cfgmgr32.dll did not load (%lu), no audio endpoints", GetLastError());
            return E_FAIL;
        }
        CreateQuery = reinterpret_cast<PXI_CREATE_QUERY>(GetProcAddress(Module, "DevCreateObjectQuery"));
        CloseQuery = reinterpret_cast<PXI_CLOSE_QUERY>(GetProcAddress(Module, "DevCloseObjectQuery"));
        if (CreateQuery == nullptr || CloseQuery == nullptr)
        {
            XI_TRACE(L"cfgmgr32.dll has no device queries, no audio endpoints");
            return E_FAIL;
        }
    }

    Filters[0].Operator = DEVPROP_OPERATOR_AND_OPEN;
    Filters[1].Operator = DEVPROP_OPERATOR_EQUALS;
    Filters[1].Property.CompKey.Key = DEVPKEY_Device_ContainerId;
    Filters[1].Property.CompKey.Store = DEVPROP_STORE_SYSTEM;
    Filters[1].Property.Type = DEVPROP_TYPE_GUID;
    Filters[1].Property.BufferSize = sizeof(ContainerValue);
    Filters[1].Property.Buffer = &ContainerValue;
    Filters[2].Operator = DEVPROP_OPERATOR_EQUALS;
    Filters[2].Property.CompKey.Key = DEVPKEY_DeviceInterface_ClassGuid;
    Filters[2].Property.CompKey.Store = DEVPROP_STORE_SYSTEM;
    Filters[2].Property.Type = DEVPROP_TYPE_GUID;
    Filters[2].Property.BufferSize = sizeof(ClassValue);
    Filters[2].Property.Buffer = &ClassValue;
    Filters[3].Operator = DEVPROP_OPERATOR_AND_CLOSE;

    ResetEvent(Endpoint->Done);
    Endpoint->Id = Id;
    Endpoint->Count = Count;
    hr = CreateQuery(DevObjectTypeDeviceInterface, DevQueryFlagUpdateResults, 1, &Endpoint->Property, ARRAYSIZE(Filters),
                     Filters, XiEndpointCallback, Endpoint, &Query);
    if (SUCCEEDED(hr))
        hr = (WaitForSingleObject(Endpoint->Done, 10000) == WAIT_OBJECT_0) ? S_OK : E_FAIL;
    if (FAILED(hr))
        XI_TRACE(L"audio endpoint query failed or timed out (0x%08lX)", hr);
    if (Query != nullptr)
        CloseQuery(Query);
    Endpoint->Id = nullptr;
    Endpoint->Count = nullptr;
    return hr;
}

/* The USB function the controller's audio interface became, found by its instance ID prefix */
static
HRESULT
XiFindContainer(
    _In_z_ PCWSTR Prefix,
    _Out_ GUID* Container)
{
    HDEVINFO List;
    SP_DEVINFO_DATA DeviceData = {};
    HRESULT hr = S_OK;
    DWORD Index;

    ZeroMemory(Container, sizeof(*Container));
    List = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (List == INVALID_HANDLE_VALUE || List == nullptr)
        return E_FAIL;
    if (SetupDiGetClassDevsExW(&XiMediaClass, Prefix, nullptr, DIGCF_PRESENT, List, nullptr, nullptr) == INVALID_HANDLE_VALUE)
    {
        SetupDiDestroyDeviceInfoList(List);
        return E_FAIL;
    }

    for (Index = 0;; Index++)
    {
        PWSTR InstanceId;
        BOOLEAN Match;

        DeviceData.cbSize = sizeof(DeviceData);
        if (!SetupDiEnumDeviceInfo(List, Index, &DeviceData))
            break;
        InstanceId = static_cast<PWSTR>(XiDeviceProperty(List, &DeviceData, &DEVPKEY_Device_InstanceId));
        if (InstanceId == nullptr)
            continue;
        Match = wcslen(InstanceId) >= 27 && _wcsnicmp(InstanceId, Prefix, 27) == 0;
        XiFreeProperty(InstanceId);
        if (Match)
        {
            GUID* Value = static_cast<GUID*>(XiDeviceProperty(List, &DeviceData, &DEVPKEY_Device_ContainerId));

            if (Value != nullptr)
            {
                *Container = *Value;
                XiFreeProperty(Value);
            }
            else
            {
                XI_TRACE(L"%s has no container", Prefix);
                hr = E_FAIL;
            }
            break;
        }
    }

    if (SUCCEEDED(hr) && IsEqualGUID(*Container, GUID{}))
        XI_TRACE(L"no audio function matches %s", Prefix);
    SetupDiDestroyDeviceInfoList(List);
    return hr;
}

HRESULT
XiGetAudioDevice(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    XI_AUDIO_REQUEST* Request = static_cast<XI_AUDIO_REQUEST*>(Context);
    WCHAR Prefix[56];
    USHORT VendorId;
    USHORT ProductId;
    UCHAR Interface;
    GUID Container;
    XI_ENDPOINT_QUERY Endpoint = {};
    HRESULT hr;

    if (Request->RenderId != nullptr)
        *Request->RenderId = UNICODE_NULL;
    else if (Request->RenderCount != nullptr)
        *Request->RenderCount = 0;
    if (Request->CaptureId != nullptr)
        *Request->CaptureId = UNICODE_NULL;
    else if (Request->CaptureCount != nullptr)
        *Request->CaptureCount = 0;

    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return E_FAIL;

    if (Device->Version >= 0x102)
    {
        if (FAILED(XiQueryAudioInformation(Device, &VendorId, &ProductId, &Interface)) || Device->AudioInterface == XI_NONE)
            return E_FAIL;
    }
    else
    {
        VendorId = Device->VendorId;
        ProductId = Device->ProductId;
        hr = XiReadState(Device);
        if (FAILED(hr))
            return hr;
        if (!(Device->Flags & XI_DEVICE_CONNECTED))
            return E_FAIL;
        Interface = Device->AudioInterface;
    }
    if (Interface == XI_NONE)
        return S_OK;

    if (FAILED(StringCchPrintfW(Prefix, ARRAYSIZE(Prefix), L"USB\\VID_%04X&PID_%04X&IA_%02X", VendorId, ProductId, Interface)))
        return E_FAIL;
    hr = XiFindContainer(Prefix, &Container);

    Endpoint.Property.Key = XiDeviceInterfaceIdKey;
    Endpoint.Property.Store = DEVPROP_STORE_SYSTEM;
    Endpoint.Done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (Endpoint.Done == nullptr)
        return E_OUTOFMEMORY;

    if (Request->RenderCount != nullptr && SUCCEEDED(hr))
        hr = XiQueryEndpoint(&Endpoint, &Container, &DEVINTERFACE_AUDIO_RENDER, Request->RenderId, Request->RenderCount);
    if (Request->CaptureCount != nullptr && SUCCEEDED(hr))
        hr = XiQueryEndpoint(&Endpoint, &Container, &DEVINTERFACE_AUDIO_CAPTURE, Request->CaptureId, Request->CaptureCount);
    CloseHandle(Endpoint.Done);
    return hr;
}
