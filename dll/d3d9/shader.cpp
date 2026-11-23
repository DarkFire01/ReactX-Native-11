/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader constants, shaders and vertex declarations
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* What the runtime's own vertex pipeline and the reference rasterizer hold */
static constexpr UINT D9WideFloatRegisters = 8192;
static constexpr UINT D9WideIntRegisters = 2048;

/* Registers a version 2 or 3 machine has of integer and boolean constants */
static constexpr UINT D9ShaderModelIntRegisters = 16;

/* A range that runs past the registers the device has; the sum cannot wrap */
static
BOOLEAN
D9OutsideRegisters(
    _In_ UINT Start,
    _In_ UINT Count,
    _In_ UINT Registers)
{
    return static_cast<ULONGLONG>(Start) + Count > Registers;
}

/* Works out how many constants of each kind the device has from its caps and vertex processing, then makes
   room for them, all zero */
HRESULT
D9Device::CreateConstants(VOID)
{
    BOOLEAN Reference = DeviceType == D3DDEVTYPE_REF || DeviceType == D3DDEVTYPE_NULLREF;
    BOOLEAN SoftwareType = DeviceType == D3DDEVTYPE_SW;
    DWORD VertexVersion = Caps.VertexShaderVersion;
    DWORD PixelVersion = Caps.PixelShaderVersion;
    D9ConstantLimits* Kept = &Limits;

    ZeroMemory(Kept, sizeof(*Kept));
    if (VertexVersion > D3DVS_VERSION(3, 0) || Reference || (SoftwareType && VertexVersion >= D3DVS_VERSION(3, 0)))
    {
        Kept->DriverVertexFloats = D9WideFloatRegisters;
        Kept->DriverVertexInts = D9WideIntRegisters;
    }
    else
    {
        Kept->DriverVertexFloats = Caps.MaxVertexShaderConst;
        if (VertexVersion >= D3DVS_VERSION(2, 0))
            Kept->DriverVertexInts = D9ShaderModelIntRegisters;
    }
    Kept->VertexFloats = Kept->DriverVertexFloats;
    Kept->VertexInts = Kept->DriverVertexInts;

    /* The runtime's own vertex pipeline has the widest machine whatever the driver reports */
    if ((BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)) != 0)
    {
        if ((BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) != 0 || Kept->VertexFloats < D9WideFloatRegisters)
            Kept->VertexFloats = D9WideFloatRegisters;
        Kept->VertexInts = D9WideIntRegisters;
    }
    Kept->VertexBools = Kept->VertexInts;

    if (PixelVersion > D3DPS_VERSION(3, 0) || Reference || (SoftwareType && PixelVersion >= D3DPS_VERSION(3, 0)))
    {
        Kept->PixelFloats = D9WideFloatRegisters;
        Kept->PixelInts = D9WideIntRegisters;
    }
    else if (PixelVersion >= D3DPS_VERSION(1, 1) && PixelVersion <= D3DPS_VERSION(1, 4))
    {
        Kept->PixelFloats = 8;
    }
    else if (PixelVersion == D3DPS_VERSION(2, 0))
    {
        /* Version 2 pixel machines only have integer and boolean registers with static flow control */
        Kept->PixelFloats = 32;
        if (Caps.PS20Caps.StaticFlowControlDepth != 0)
            Kept->PixelInts = D9ShaderModelIntRegisters;
    }
    else if (PixelVersion == D3DPS_VERSION(3, 0))
    {
        Kept->PixelFloats = 224;
        Kept->PixelInts = D9ShaderModelIntRegisters;
    }
    Kept->PixelBools = Kept->PixelInts;

    VertexFloats = new (std::nothrow) FLOAT[Kept->VertexFloats + 1][4]();
    VertexInts = new (std::nothrow) INT[Kept->VertexInts + 1][4]();
    VertexBools = new (std::nothrow) BOOL[Kept->VertexBools + 1]();
    PixelFloats = new (std::nothrow) FLOAT[Kept->PixelFloats + 1][4]();
    PixelInts = new (std::nothrow) INT[Kept->PixelInts + 1][4]();
    PixelBools = new (std::nothrow) BOOL[Kept->PixelBools + 1]();
    if (VertexFloats == nullptr || VertexInts == nullptr || VertexBools == nullptr || PixelFloats == nullptr ||
        PixelInts == nullptr || PixelBools == nullptr)
    {
        D9Trace(L"CreateConstants: no memory for %u vertex constants", Kept->VertexFloats);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

/* A plain device's reset starts every constant over at zero */
VOID
D9Device::ClearConstants(VOID)
{
    ZeroMemory(VertexFloats, Limits.VertexFloats * sizeof(*VertexFloats));
    ZeroMemory(VertexInts, Limits.VertexInts * sizeof(*VertexInts));
    ZeroMemory(VertexBools, Limits.VertexBools * sizeof(*VertexBools));
    ZeroMemory(PixelFloats, Limits.PixelFloats * sizeof(*PixelFloats));
    ZeroMemory(PixelInts, Limits.PixelInts * sizeof(*PixelInts));
    ZeroMemory(PixelBools, Limits.PixelBools * sizeof(*PixelBools));
    VertexConstantsPending = FALSE;
}

/* The driver's pipeline may hold fewer vertex registers than a mixed device's own; while the runtime processes
   vertices itself the constants wait for the hardware pipeline */
VOID
D9Device::SendVertexConstants(
    _In_ D9BatchCall Call,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_bytes_opt_(Count * Stride) const VOID* Data,
    _In_ UINT Stride)
{
    UINT Registers = Call == D9BatchCall::VertexShaderConst ? Limits.DriverVertexFloats : Limits.DriverVertexInts;

    if (SoftwareVertices)
    {
        VertexConstantsPending = TRUE;
        return;
    }

    /* The highest registers the application wrote come back when its shader replaces the converter's, which
       holds them back meanwhile */
    if (Call == D9BatchCall::VertexShaderConst && Switch.HasVertexConverter)
        Switch.VertexFloatsUsed = Start + Count > Switch.VertexFloatsUsed ? Start + Count : Switch.VertexFloatsUsed;
    else if (Call == D9BatchCall::VertexShaderConstI)
        Switch.VertexIntsUsed = Start + Count > Switch.VertexIntsUsed ? Start + Count : Switch.VertexIntsUsed;
    if (Call != D9BatchCall::VertexShaderConstB && Switch.VertexConverting)
        return;
    if (D9OutsideRegisters(Start, Count, Registers))
    {
        if (Start >= Registers)
            return;
        Count = Registers - Start;
    }
    if (VertexShader == nullptr)
    {
        DdiShaderConstants(Call, Start, Count, Data, Count * Stride);
        return;
    }
    if (Call == D9BatchCall::VertexShaderConst)
        SendAroundDefs(Call, Start, Count, Data, Stride, VertexShader->Shape()->FloatDefs,
                       VertexShader->Shape()->FloatDefCount);
    else if (Call == D9BatchCall::VertexShaderConstI)
        SendAroundDefs(Call, Start, Count, Data, Stride, VertexShader->Shape()->IntDefs,
                       VertexShader->Shape()->IntDefCount);
    else
        SendAroundDefs(Call, Start, Count, Data, Stride, VertexShader->Shape()->BoolDefs,
                       VertexShader->Shape()->BoolDefCount);
}

/* The registers a bound shader defines keep its own values; the rest of a range goes out in pieces */
VOID
D9Device::SendAroundDefs(
    _In_ D9BatchCall Call,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_bytes_opt_(Count * Stride) const VOID* Data,
    _In_ UINT Stride,
    _In_reads_(DefCount) const USHORT* Defs,
    _In_ UINT DefCount)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    UINT End = Start + Count;
    UINT Piece = Start;

    for (UINT Index = 0; Index < DefCount && Piece < End; Index++)
    {
        UINT Defined = Defs[Index];

        if (Defined < Piece)
            continue;
        if (Defined >= End)
            break;
        if (Defined > Piece && Call == D9BatchCall::PixelShaderConst)
            SendPixelFloats(Piece, Defined - Piece, reinterpret_cast<const FLOAT*>(Bytes + (Piece - Start) * Stride));
        else if (Defined > Piece)
            DdiShaderConstants(Call, Piece, Defined - Piece, Bytes + (Piece - Start) * Stride, (Defined - Piece) * Stride);
        Piece = Defined + 1;
    }
    if (Piece < End && Call == D9BatchCall::PixelShaderConst)
        SendPixelFloats(Piece, End - Piece, reinterpret_cast<const FLOAT*>(Bytes + (Piece - Start) * Stride));
    else if (Piece < End)
        DdiShaderConstants(Call, Piece, End - Piece, Bytes + (Piece - Start) * Stride, (End - Piece) * Stride);
}

/* Float pixel constants share the driver's registers with the runtime's fixed function pixel shaders, so
   they wait until the application binds a pixel shader of its own */
VOID
D9Device::SendPixelConstants(
    _In_ D9BatchCall Call,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_bytes_opt_(Count * Stride) const VOID* Data,
    _In_ UINT Stride)
{
    if (PixelShader == nullptr && Call == D9BatchCall::PixelShaderConst)
        SendPixelFloats(Start, Count, static_cast<const FLOAT*>(Data));
    else if (PixelShader == nullptr)
        DdiShaderConstants(Call, Start, Count, Data, Count * Stride);
    else if (Call == D9BatchCall::PixelShaderConst)
        SendAroundDefs(Call, Start, Count, Data, Stride, PixelShader->Shape()->FloatDefs,
                       PixelShader->Shape()->FloatDefCount);
    else if (Call == D9BatchCall::PixelShaderConstI)
        SendAroundDefs(Call, Start, Count, Data, Stride, PixelShader->Shape()->IntDefs,
                       PixelShader->Shape()->IntDefCount);
    else
        SendAroundDefs(Call, Start, Count, Data, Stride, PixelShader->Shape()->BoolDefs,
                       PixelShader->Shape()->BoolDefCount);
}

STDMETHODIMP
D9Device::SetVertexShaderConstantF(
    _In_ UINT StartRegister,
    _In_opt_ const float* ConstantData,
    _In_ UINT Vector4fCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, Vector4fCount, Limits.VertexFloats))
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return RecordConstants(D9BlockOp::VertexFloats, StartRegister, Vector4fCount, ConstantData, sizeof(*VertexFloats));
    CopyMemory(VertexFloats[StartRegister], ConstantData, Vector4fCount * sizeof(*VertexFloats));
    SendVertexConstants(D9BatchCall::VertexShaderConst, StartRegister, Vector4fCount, ConstantData,
                        sizeof(*VertexFloats));
    return S_OK;
}

STDMETHODIMP
D9Device::GetVertexShaderConstantF(
    _In_ UINT StartRegister,
    _Out_ float* ConstantData,
    _In_ UINT Vector4fCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, Vector4fCount, Limits.VertexFloats))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, VertexFloats[StartRegister], Vector4fCount * sizeof(*VertexFloats));
    return S_OK;
}

STDMETHODIMP
D9Device::SetVertexShaderConstantI(
    _In_ UINT StartRegister,
    _In_opt_ const int* ConstantData,
    _In_ UINT Vector4iCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, Vector4iCount, Limits.VertexInts))
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return RecordConstants(D9BlockOp::VertexInts, StartRegister, Vector4iCount, ConstantData, sizeof(*VertexInts));
    CopyMemory(VertexInts[StartRegister], ConstantData, Vector4iCount * sizeof(*VertexInts));
    SendVertexConstants(D9BatchCall::VertexShaderConstI, StartRegister, Vector4iCount, ConstantData,
                        sizeof(*VertexInts));
    return S_OK;
}

STDMETHODIMP
D9Device::GetVertexShaderConstantI(
    _In_ UINT StartRegister,
    _Out_ int* ConstantData,
    _In_ UINT Vector4iCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, Vector4iCount, Limits.VertexInts))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, VertexInts[StartRegister], Vector4iCount * sizeof(*VertexInts));
    return S_OK;
}

STDMETHODIMP
D9Device::SetVertexShaderConstantB(
    _In_ UINT StartRegister,
    _In_opt_ const BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, BoolCount, Limits.VertexBools))
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return RecordConstants(D9BlockOp::VertexBools, StartRegister, BoolCount, ConstantData, sizeof(*VertexBools));
    CopyMemory(&VertexBools[StartRegister], ConstantData, BoolCount * sizeof(*VertexBools));
    SendVertexConstants(D9BatchCall::VertexShaderConstB, StartRegister, BoolCount, ConstantData,
                        sizeof(*VertexBools));
    return S_OK;
}

STDMETHODIMP
D9Device::GetVertexShaderConstantB(
    _In_ UINT StartRegister,
    _Out_ BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, BoolCount, Limits.VertexBools))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, &VertexBools[StartRegister], BoolCount * sizeof(*VertexBools));
    return S_OK;
}

STDMETHODIMP
D9Device::SetPixelShaderConstantF(
    _In_ UINT StartRegister,
    _In_opt_ const float* ConstantData,
    _In_ UINT Vector4fCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, Vector4fCount, Limits.PixelFloats))
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return RecordConstants(D9BlockOp::PixelFloats, StartRegister, Vector4fCount, ConstantData, sizeof(*PixelFloats));
    CopyMemory(PixelFloats[StartRegister], ConstantData, Vector4fCount * sizeof(*PixelFloats));
    SendPixelConstants(D9BatchCall::PixelShaderConst, StartRegister, Vector4fCount, ConstantData,
                       sizeof(*PixelFloats));
    return S_OK;
}

STDMETHODIMP
D9Device::GetPixelShaderConstantF(
    _In_ UINT StartRegister,
    _Out_ float* ConstantData,
    _In_ UINT Vector4fCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, Vector4fCount, Limits.PixelFloats))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, PixelFloats[StartRegister], Vector4fCount * sizeof(*PixelFloats));
    return S_OK;
}

STDMETHODIMP
D9Device::SetPixelShaderConstantI(
    _In_ UINT StartRegister,
    _In_opt_ const int* ConstantData,
    _In_ UINT Vector4iCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, Vector4iCount, Limits.PixelInts))
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return RecordConstants(D9BlockOp::PixelInts, StartRegister, Vector4iCount, ConstantData, sizeof(*PixelInts));
    CopyMemory(PixelInts[StartRegister], ConstantData, Vector4iCount * sizeof(*PixelInts));
    SendPixelConstants(D9BatchCall::PixelShaderConstI, StartRegister, Vector4iCount, ConstantData,
                       sizeof(*PixelInts));
    return S_OK;
}

STDMETHODIMP
D9Device::GetPixelShaderConstantI(
    _In_ UINT StartRegister,
    _Out_ int* ConstantData,
    _In_ UINT Vector4iCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, Vector4iCount, Limits.PixelInts))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, PixelInts[StartRegister], Vector4iCount * sizeof(*PixelInts));
    return S_OK;
}

STDMETHODIMP
D9Device::SetPixelShaderConstantB(
    _In_ UINT StartRegister,
    _In_opt_ const BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    D9_DEVICE_LOCK();

    if (D9OutsideRegisters(StartRegister, BoolCount, Limits.PixelBools))
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return RecordConstants(D9BlockOp::PixelBools, StartRegister, BoolCount, ConstantData, sizeof(*PixelBools));
    CopyMemory(&PixelBools[StartRegister], ConstantData, BoolCount * sizeof(*PixelBools));
    SendPixelConstants(D9BatchCall::PixelShaderConstB, StartRegister, BoolCount, ConstantData,
                       sizeof(*PixelBools));
    return S_OK;
}

STDMETHODIMP
D9Device::GetPixelShaderConstantB(
    _In_ UINT StartRegister,
    _Out_ BOOL* ConstantData,
    _In_ UINT BoolCount)
{
    D9_DEVICE_LOCK();

    if (KeepsNoState())
        return E_NOTIMPL;
    if (D9OutsideRegisters(StartRegister, BoolCount, Limits.PixelBools))
        return D3DERR_INVALIDCALL;
    CopyMemory(ConstantData, &PixelBools[StartRegister], BoolCount * sizeof(*PixelBools));
    return S_OK;
}

/* Only a device that processes every vertex in hardware gives its driver vertex shaders as they are made */
BOOLEAN
D9Device::DriverHasVertexShaders(VOID) const
{
    return (BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)) == 0;
}

template <class Interface>
D9Shader<Interface>::~D9Shader()
{
    BOOLEAN Pixel = __uuidof(Interface) == __uuidof(IDirect3DPixelShader9);

    if (DriverHandle != NULL)
        Device->DestroyDriverShader(Pixel, DriverHandle);
    delete Program;
    delete[] Function;
    delete[] Code;
}

/* Keeps the application's function and the copy the driver gets, which leaves out comments and names the
   extended version 2 pixel machine as version 2 */
template <class Interface>
HRESULT
D9Shader<Interface>::Initialize(
    _In_ const DWORD* Source,
    _In_ const D9ShaderLayout* Shape)
{
    const DWORD* Token = Source + 1;
    DWORD* Out;

    Layout = *Shape;
    Function = new (std::nothrow) DWORD[Layout.Size / sizeof(DWORD)];
    Code = new (std::nothrow) DWORD[Layout.StrippedSize / sizeof(DWORD)];
    if (Function == nullptr || Code == nullptr)
    {
        D9Trace(L"CreateShader: no memory for %u bytes of code", Layout.Size);
        return E_OUTOFMEMORY;
    }
    CopyMemory(Function, Source, Layout.Size);

    Out = Code;
    *Out++ = Source[0] == D3DPS_VERSION(2, 1) ? D3DPS_VERSION(2, 0) : Source[0];
    while (Token < Source + Layout.Size / sizeof(DWORD))
    {
        if ((*Token & 0xFFFF) == 0xFFFE)
        {
            Token += ((*Token >> 16) & 0x7FFF) + 1;
            continue;
        }
        *Out++ = *Token++;
    }
    return S_OK;
}

template <class Interface>
STDMETHODIMP
D9Shader<Interface>::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Object)
{
    if (Object == nullptr)
        return D3DERR_INVALIDCALL;

    *Object = nullptr;
    if (IsEqualGUID(Iid, __uuidof(IUnknown)) || IsEqualGUID(Iid, __uuidof(Interface)))
    {
        *Object = static_cast<Interface*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

template <class Interface>
STDMETHODIMP_(ULONG)
D9Shader<Interface>::AddRef()
{
    return AddRefChild();
}

template <class Interface>
STDMETHODIMP_(ULONG)
D9Shader<Interface>::Release()
{
    D9Device* Owner = Device;
    LONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
    {
        if (BindCount > 0)
            Orphaned = TRUE;
        else
            delete this;
        Owner->Release();
    }
    return static_cast<ULONG>(Count);
}

template <class Interface>
VOID
D9Shader<Interface>::Unbind(VOID)
{
    BindCount -= 1;
    if (BindCount == 0 && Orphaned)
        delete this;
}

template <class Interface>
STDMETHODIMP
D9Shader<Interface>::GetDevice(
    _Out_ IDirect3DDevice9** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Device;
    Device->AddRef();
    return S_OK;
}

/* The whole function is copied whatever size the caller says it has room for */
template <class Interface>
STDMETHODIMP
D9Shader<Interface>::GetFunction(
    _Out_writes_bytes_opt_(*SizeOfData) VOID* Data,
    _Inout_ UINT* SizeOfData)
{
    if (Data != nullptr)
        CopyMemory(Data, Function, Layout.Size);
    *SizeOfData = Layout.Size;
    return S_OK;
}

template class D9Shader<IDirect3DVertexShader9>;
template class D9Shader<IDirect3DPixelShader9>;

/* Validates the tokens against the device, then makes the shader and, where the device
   hands shaders to its driver, the driver's copy */
template <class Shader>
static
HRESULT
D9MakeShader(
    _In_ D9Device* Device,
    _In_ const DWORD* Function,
    _In_ BOOLEAN Pixel,
    _In_ BOOLEAN ToDriver,
    _In_ const D9_SHADER_CHECK_DEVICE* Checked,
    _Out_ Shader** Made)
{
    D9ShaderLayout* Shape = new (std::nothrow) D9ShaderLayout;
    Shader* Object;
    HRESULT hr;

    *Made = nullptr;
    if (Shape == nullptr)
        return E_OUTOFMEMORY;
    if (!D9CheckShaderTokens(Function, Pixel, Checked, Shape))
    {
        delete Shape;
        return D3DERR_INVALIDCALL;
    }

    Object = new (std::nothrow) Shader(Device);
    if (Object == nullptr)
    {
        delete Shape;
        return E_OUTOFMEMORY;
    }
    hr = Object->Initialize(Function, Shape);
    if (SUCCEEDED(hr) && ToDriver)
        hr = Device->CreateDriverShader(Pixel, Object->DriverCode(), Shape->StrippedSize, &Object->DriverHandle);
    delete Shape;
    if (FAILED(hr))
    {
        delete Object;
        return hr;
    }
    Object->AddRef();
    *Made = Object;
    return S_OK;
}

STDMETHODIMP
D9Device::CreateVertexShader(
    _In_opt_ const DWORD* Function,
    _Out_ IDirect3DVertexShader9** Shader)
{
    D9_DEVICE_LOCK();
    D9_SHADER_CHECK_DEVICE Checked = { &Caps, BehaviorFlags, DeviceType };
    D9VertexShader* Made;
    HRESULT hr;

    hr = D9MakeShader(this, Function, FALSE, DriverHasVertexShaders(), &Checked, &Made);
    *Shader = Made;
    return hr;
}

STDMETHODIMP
D9Device::CreatePixelShader(
    _In_opt_ const DWORD* Function,
    _Out_ IDirect3DPixelShader9** Shader)
{
    D9_DEVICE_LOCK();
    D9_SHADER_CHECK_DEVICE Checked = { &Caps, BehaviorFlags, DeviceType };
    D9PixelShader* Made;
    HRESULT hr;

    hr = D9MakeShader(this, Function, TRUE, TRUE, &Checked, &Made);
    *Shader = Made;
    return hr;
}

/* The constants the old shader defined go back to the application's values before the driver switches; a null
   shader reaches the driver only when something draws with the fixed function pipeline */
STDMETHODIMP
D9Device::SetVertexShader(
    _In_opt_ IDirect3DVertexShader9* Shader)
{
    D9_DEVICE_LOCK();
    D9VertexShader* Next = static_cast<D9VertexShader*>(Shader);
    D9VertexShader* Previous = VertexShader;

    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::VertexShader, 0, 0, &Shader, sizeof(Shader));
    if (Next == Previous)
        return S_OK;

    if (Next != nullptr)
        Next->Bind();
    VertexShader = nullptr;
    if (Previous != nullptr)
        RestoreShaderDefs(FALSE, Previous->Shape(), Next != nullptr ? Next->Shape() : nullptr);
    VertexShader = Next;

    /* Without a driver copy the shader runs in the runtime's pipeline, which picks it up at the next draw; a
       mixed device hands the driver its shaders only when it draws */
    if (Next != nullptr && (BehaviorFlags & D3DCREATE_MIXED_VERTEXPROCESSING) != 0)
        DrawDirty |= D9DrawVertexInput;
    else if (Next != nullptr && Next->DriverHandle != NULL)
        SwitchVertexShader(Next->DriverHandle, Next->Shape()->Outputs);
    else if (Next != nullptr)
        DrawDirty |= D9DrawVertexInput;
    else if (DriverHasVertexShaders())
        SwitchVertexShader(NULL, 0);
    else if ((BehaviorFlags & D3DCREATE_MIXED_VERTEXPROCESSING) != 0 && !SoftwareVertices)
        SwitchVertexShader(NULL, 0);
    if (Next == nullptr && !DriverHasVertexShaders())
        DrawDirty |= D9DrawVertexInput;
    if (Previous != nullptr)
        Previous->Unbind();
    return S_OK;
}

STDMETHODIMP
D9Device::GetVertexShader(
    _Out_ IDirect3DVertexShader9** Shader)
{
    D9_DEVICE_LOCK();

    if (Shader == nullptr)
        return D3DERR_INVALIDCALL;
    *Shader = VertexShader;
    if (VertexShader != nullptr)
        VertexShader->AddRef();
    return S_OK;
}

/* The constants the old shader defined go back to the application's values; the driver gets the new shader
   unless the fixed function one stands in */
STDMETHODIMP
D9Device::SetPixelShader(
    _In_opt_ IDirect3DPixelShader9* Shader)
{
    D9_DEVICE_LOCK();
    D9PixelShader* Next = static_cast<D9PixelShader*>(Shader);
    D9PixelShader* Previous = PixelShader;

    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::PixelShader, 0, 0, &Shader, sizeof(Shader));
    if (Next == Previous)
        return S_OK;
    if (Next != nullptr)
        Next->Bind();
    PixelShader = nullptr;
    if (Previous != nullptr)
        RestoreShaderDefs(TRUE, Previous->Shape(), Next != nullptr ? Next->Shape() : nullptr);
    PixelShader = Next;
    SwitchPixelShader(Next != nullptr ? Next->DriverHandle : NULL, 0);
    if (Previous != nullptr)
        Previous->Unbind();
    return S_OK;
}

STDMETHODIMP
D9Device::GetPixelShader(
    _Out_ IDirect3DPixelShader9** Shader)
{
    D9_DEVICE_LOCK();

    if (Shader == nullptr)
        return D3DERR_INVALIDCALL;
    *Shader = PixelShader;
    if (PixelShader != nullptr)
        PixelShader->AddRef();
    return S_OK;
}
