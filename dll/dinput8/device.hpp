/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The generic IDirectInputDevice8 object every device class sits behind
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "dinput8.hpp"

class DiDevice : public DiObject, public IDirectInputDevice8W
{
public:
    class AnsiFace : public IDirectInputDevice8A
    {
    public:
        DiDevice* Owner;

        STDMETHODIMP
        QueryInterface(
            _In_ REFIID Riid,
            _Outptr_ VOID** Object) override
        {
            return Owner->OuterQuery(Riid, Object);
        }

        STDMETHODIMP_(ULONG)
        AddRef() override
        {
            return Owner->OuterAddRef();
        }

        STDMETHODIMP_(ULONG)
        Release() override
        {
            return Owner->OuterRelease();
        }

        STDMETHODIMP
        GetCapabilities(
            _Inout_ LPDIDEVCAPS Caps) override
        {
            return Owner->GetCapabilities(Caps);
        }

        STDMETHODIMP
        EnumObjects(
            _In_ LPDIENUMDEVICEOBJECTSCALLBACKA Callback,
            _In_opt_ VOID* Context,
            _In_ DWORD Flags) override;

        STDMETHODIMP
        GetProperty(
            _In_ REFGUID Property,
            _Inout_ LPDIPROPHEADER Header) override;

        STDMETHODIMP
        SetProperty(
            _In_ REFGUID Property,
            _In_ LPCDIPROPHEADER Header) override;

        STDMETHODIMP
        Acquire() override
        {
            return Owner->Acquire();
        }

        STDMETHODIMP
        Unacquire() override
        {
            return Owner->Unacquire();
        }

        STDMETHODIMP
        GetDeviceState(
            _In_ DWORD Size,
            _Out_writes_bytes_(Size) VOID* Data) override
        {
            return Owner->GetDeviceState(Size, Data);
        }

        STDMETHODIMP
        GetDeviceData(
            _In_ DWORD ObjectSize,
            _Out_writes_opt_(*Count) LPDIDEVICEOBJECTDATA Data,
            _Inout_ LPDWORD Count,
            _In_ DWORD Flags) override
        {
            return Owner->GetDeviceData(ObjectSize, Data, Count, Flags);
        }

        STDMETHODIMP
        SetDataFormat(
            _In_ LPCDIDATAFORMAT Format) override
        {
            return Owner->SetDataFormat(Format);
        }

        STDMETHODIMP
        SetEventNotification(
            _In_opt_ HANDLE Event) override
        {
            return Owner->SetEventNotification(Event);
        }

        STDMETHODIMP
        SetCooperativeLevel(
            _In_opt_ HWND Window,
            _In_ DWORD Flags) override
        {
            return Owner->SetCooperativeLevel(Window, Flags);
        }

        STDMETHODIMP
        GetObjectInfo(
            _Inout_ LPDIDEVICEOBJECTINSTANCEA Info,
            _In_ DWORD Object,
            _In_ DWORD How) override;

        STDMETHODIMP
        GetDeviceInfo(
            _Inout_ LPDIDEVICEINSTANCEA Info) override;

        STDMETHODIMP
        RunControlPanel(
            _In_opt_ HWND Window,
            _In_ DWORD Flags) override
        {
            return Owner->RunControlPanel(Window, Flags);
        }

        STDMETHODIMP
        Initialize(
            _In_ HINSTANCE Instance,
            _In_ DWORD Version,
            _In_ REFGUID Guid) override
        {
            return Owner->Initialize(Instance, Version, Guid);
        }

        STDMETHODIMP
        CreateEffect(
            _In_ REFGUID Effect,
            _In_opt_ LPCDIEFFECT Params,
            _Out_ LPDIRECTINPUTEFFECT* Created,
            _In_opt_ LPUNKNOWN Outer) override
        {
            return Owner->CreateEffect(Effect, Params, Created, Outer);
        }

        STDMETHODIMP
        EnumEffects(
            _In_ LPDIENUMEFFECTSCALLBACKA Callback,
            _In_opt_ VOID* Context,
            _In_ DWORD Type) override;

        STDMETHODIMP
        GetEffectInfo(
            _Inout_ LPDIEFFECTINFOA Info,
            _In_ REFGUID Effect) override;

        STDMETHODIMP
        GetForceFeedbackState(
            _Out_ LPDWORD State) override
        {
            return Owner->GetForceFeedbackState(State);
        }

        STDMETHODIMP
        SendForceFeedbackCommand(
            _In_ DWORD Command) override
        {
            return Owner->SendForceFeedbackCommand(Command);
        }

        STDMETHODIMP
        EnumCreatedEffectObjects(
            _In_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK Callback,
            _In_opt_ VOID* Context,
            _In_ DWORD Flags) override
        {
            return Owner->EnumCreatedEffectObjects(Callback, Context, Flags);
        }

        STDMETHODIMP
        Escape(
            _Inout_ LPDIEFFESCAPE Escape) override
        {
            return Owner->Escape(Escape);
        }

        STDMETHODIMP
        Poll() override
        {
            return Owner->Poll();
        }

        STDMETHODIMP
        SendDeviceData(
            _In_ DWORD ObjectSize,
            _In_reads_(*Count) LPCDIDEVICEOBJECTDATA Data,
            _Inout_ LPDWORD Count,
            _In_ DWORD Flags) override
        {
            return Owner->SendDeviceData(ObjectSize, Data, Count, Flags);
        }

        STDMETHODIMP
        EnumEffectsInFile(
            _In_z_ LPCSTR FileName,
            _In_ LPDIENUMEFFECTSINFILECALLBACK Callback,
            _In_opt_ VOID* Context,
            _In_ DWORD Flags) override;

        STDMETHODIMP
        WriteEffectToFile(
            _In_z_ LPCSTR FileName,
            _In_ DWORD Count,
            _In_reads_(Count) LPDIFILEEFFECT Effects,
            _In_ DWORD Flags) override;

        STDMETHODIMP
        BuildActionMap(
            _Inout_ LPDIACTIONFORMATA Format,
            _In_opt_z_ LPCSTR UserName,
            _In_ DWORD Flags) override;

        STDMETHODIMP
        SetActionMap(
            _In_ LPDIACTIONFORMATA Format,
            _In_opt_z_ LPCSTR UserName,
            _In_ DWORD Flags) override;

        STDMETHODIMP
        GetImageInfo(
            _Inout_ LPDIDEVICEIMAGEINFOHEADERA Header) override;
    };

    AnsiFace Ansi;
    CRITICAL_SECTION Lock;

    /* The device class behind this object, or nullptr until Initialize succeeds */
    DiBackend* Backend;
    GUID Instance;
    DWORD Version;
    DWORD CapsFlags;

    DiDevice(
        _In_opt_ IUnknown* Outer);

    ~DiDevice() override;

    VOID*
    FindInterface(
        _In_ REFIID Riid) override;

    /* IUnknown */

    STDMETHODIMP
    QueryInterface(
        _In_ REFIID Riid,
        _Outptr_ VOID** Object) override
    {
        return OuterQuery(Riid, Object);
    }

    STDMETHODIMP_(ULONG)
    AddRef() override
    {
        return OuterAddRef();
    }

    STDMETHODIMP_(ULONG)
    Release() override
    {
        return OuterRelease();
    }

    /* IDirectInputDevice8W */

    STDMETHODIMP
    GetCapabilities(
        _Inout_ LPDIDEVCAPS Caps) override;

    STDMETHODIMP
    EnumObjects(
        _In_ LPDIENUMDEVICEOBJECTSCALLBACKW Callback,
        _In_opt_ VOID* Context,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    GetProperty(
        _In_ REFGUID Property,
        _Inout_ LPDIPROPHEADER Header) override;

    STDMETHODIMP
    SetProperty(
        _In_ REFGUID Property,
        _In_ LPCDIPROPHEADER Header) override;

    STDMETHODIMP
    Acquire() override;

    STDMETHODIMP
    Unacquire() override;

    STDMETHODIMP
    GetDeviceState(
        _In_ DWORD Size,
        _Out_writes_bytes_(Size) VOID* Data) override;

    STDMETHODIMP
    GetDeviceData(
        _In_ DWORD ObjectSize,
        _Out_writes_opt_(*Count) LPDIDEVICEOBJECTDATA Data,
        _Inout_ LPDWORD Count,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    SetDataFormat(
        _In_ LPCDIDATAFORMAT Format) override;

    STDMETHODIMP
    SetEventNotification(
        _In_opt_ HANDLE Event) override;

    STDMETHODIMP
    SetCooperativeLevel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    GetObjectInfo(
        _Inout_ LPDIDEVICEOBJECTINSTANCEW Info,
        _In_ DWORD Object,
        _In_ DWORD How) override;

    STDMETHODIMP
    GetDeviceInfo(
        _Inout_ LPDIDEVICEINSTANCEW Info) override;

    STDMETHODIMP
    RunControlPanel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    Initialize(
        _In_ HINSTANCE Module,
        _In_ DWORD NewVersion,
        _In_ REFGUID Guid) override;

    STDMETHODIMP
    CreateEffect(
        _In_ REFGUID Effect,
        _In_opt_ LPCDIEFFECT Params,
        _Out_ LPDIRECTINPUTEFFECT* Created,
        _In_opt_ LPUNKNOWN Outer) override;

    STDMETHODIMP
    EnumEffects(
        _In_ LPDIENUMEFFECTSCALLBACKW Callback,
        _In_opt_ VOID* Context,
        _In_ DWORD Type) override;

    STDMETHODIMP
    GetEffectInfo(
        _Inout_ LPDIEFFECTINFOW Info,
        _In_ REFGUID Effect) override;

    STDMETHODIMP
    GetForceFeedbackState(
        _Out_ LPDWORD State) override;

    STDMETHODIMP
    SendForceFeedbackCommand(
        _In_ DWORD Command) override;

    STDMETHODIMP
    EnumCreatedEffectObjects(
        _In_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK Callback,
        _In_opt_ VOID* Context,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    Escape(
        _Inout_ LPDIEFFESCAPE Escape) override;

    STDMETHODIMP
    Poll() override;

    STDMETHODIMP
    SendDeviceData(
        _In_ DWORD ObjectSize,
        _In_reads_(*Count) LPCDIDEVICEOBJECTDATA Data,
        _Inout_ LPDWORD Count,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    EnumEffectsInFile(
        _In_z_ LPCWSTR FileName,
        _In_ LPDIENUMEFFECTSINFILECALLBACK Callback,
        _In_opt_ VOID* Context,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    WriteEffectToFile(
        _In_z_ LPCWSTR FileName,
        _In_ DWORD Count,
        _In_reads_(Count) LPDIFILEEFFECT Effects,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    BuildActionMap(
        _Inout_ LPDIACTIONFORMATW Format,
        _In_opt_z_ LPCWSTR UserName,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    SetActionMap(
        _In_ LPDIACTIONFORMATW Format,
        _In_opt_z_ LPCWSTR UserName,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    GetImageInfo(
        _Inout_ LPDIDEVICEIMAGEINFOHEADERW Header) override;

private:
    VOID
    Reset(VOID);
};
